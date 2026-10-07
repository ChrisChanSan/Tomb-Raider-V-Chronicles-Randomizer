/*
 * Modified 2026 by ChrisChanSan.
 *
 * The structure of the level build (read the .TRG, one buffer for the whole
 * level, a range table per group, three passes per room) and the draw rule
 * are carried over from TROne (Copyright 2026 Todesengel, GPLv3, TROne-main
 * (copy of 2026-10-06, commit 75c4c843), host\render\game_renderer.cpp,
 * hd_load and hd_draw_room), TROne's own design. The renderer under it is
 * new: TROne is OpenGL 3.3 with vertex arrays, texture arrays and runtime
 * GLSL, this is Direct3D 9 with a vertex declaration, one texture per page
 * and compiled bytecode, beside a fixed-function neighbour that must find its
 * state exactly as it left it. See THIRD-PARTY.md.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "hd_render.h"
#include "hd_pagecache.h"
#include "hd_book.h"

#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include "hd_shader/hd_room_vs.inc"
#include "hd_shader/hd_room_ps.inc"
#include "hd_shader/hd_room_gloss_ps.inc"
#include "hd_shader/hd_glow_ps.inc"
#include "hd_shader/hd_shadow_receive_ps.inc"
#include "hd_shader/hd_shadow_receive2_ps.inc"
#include "hd_shader/hd_shadow_receive_mirror_ps.inc"
#include "hd_gloss_math.h"
#include "hd_gloss_pages.h"
#include "hd_gloss_seed.h"
#include "hd_gloss_field.h"
#include "hd_shadow.h"			// HD_SHADOW_MAP, the projected shadow's map

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	/* The vertex on the card is the vertex on disk: 20 bytes, same fields,
	 * same meaning, plus the four water mark bytes. Written out field by
	 * field rather than memcpy'd from TrgVertex, because that struct belongs
	 * to the vendored reader and its layout may change.
	 *
	 * The one thing that is not a straight copy is the colour byte order.
	 * D3DDECLTYPE_D3DCOLOR reads the four bytes as a little-endian
	 * 0xAARRGGBB DWORD, so the byte at offset 0 of the pair is blue and the
	 * byte at offset 3 is alpha. Writing (b, g, r, coordinate) here lets
	 * hd_room.hlsl read the triple as plain .rgb and the coordinate as .a.
	 * The swap happens once, in this struct; a second swap in the shader
	 * would paint the level with red and blue exchanged.
	 *
	 * The fourth byte of each colour pair is a texture coordinate, 0..255 in
	 * every file: counted over all fifteen, byte 15 follows the classic u and
	 * byte 19 the classic v within a face (3,157,175 agreeing pairs against
	 * 1,465,989, and 3,354,530 against 1,340,180; the crossed pairing comes
	 * out even; TrgProbe -uvbytes). */
	struct HDVertex
	{
		short x, y, z, sway;
		unsigned char nx, ny, nz, layer;
		unsigned char c1b, c1g, c1r, u;		// D3DCOLOR order: B, G, R, A
		unsigned char c2b, c2g, c2r, v;
		/* The four water mark bytes - see HDWaterMark in the header. They
		 * are 0 or 1, not 0 or 255: D3DDECLTYPE_UBYTE4 is unnormalised, so
		 * whatever is written here is what the shader adds up. The fourth is
		 * the constant 1, which lets a selector address a whole room without
		 * a mark on every vertex - see gSel1 in hd_room.hlsl. */
		unsigned char wChoppy, wShimmer, wRoomShimmer, wSpare;
	};

	/* One draw call: a run of indices that all name the same page and the same
	 * UV scroll. A .TRG part carries a material and four parameters, and
	 * material 1 is a scroll whose param.xy is a speed in page units per
	 * second (Trg.h has the part layout; hd_room.hlsl has the calibration
	 * against the classic UVROT branch). The scroll is part of the sort key,
	 * because a range is one SetTexture and one constant and a triangle cannot
	 * have half of each. Costs nothing where nothing scrolls: over all 15
	 * levels only 5 to 65 parts per level carry one. */
	/* The .TRG materials this file knows by number, measured over all 15 TR5
	 * levels (Preparation\Tools\Diagnose); everything else is drawn the same
	 * way and needs no name here.
	 *
	 *    1  the flow material: a UV scroll, speed in param.xy - waterfalls,
	 *       rivers, conveyor belts. Its vertices may also carry a bend, and
	 *       that bend is the water's own
	 *    7  a wind material. param[0] is 0.5 in all 7 of its per-level
	 *       tuples, and is the one slot read here (MaterialAlphaRef); the
	 *       other three run
	 *       0..10, 5..32 and 0..24 (TrgProbe -mat 7) and are not read
	 *    8  the other wind material, the same param shape - param[0] 0.5 in
	 *       25 of its 26 per-level tuples (ANDY2's alpha-pass one 0.12), the
	 *       others 0..12, 4..80 and 0..40 (TrgProbe -mat 8) - foliage, ivy,
	 *       banners, kelp. 7 and 8 are the same thing in two numbers: no
	 *       level carries both, ANDREA3 has only 7 and JOBY4 only 8, and
	 *       TITLE - which is a montage of every level - has both
	 *    6  the plain cut-out material - gratings, nets, railings, ladders.
	 *       param[0] is its alpha reference and is 0.5 in 10 of its 11
	 *       per-level tuples (ANDY1 carries one 0.503)
	 *   13  the remaster's own four-frame flipbook, the other three pages in
	 *       param[0..2] (MaterialFlipTime below)
	 *    2  the glow material: light shafts, lamp cones, window glows and
	 *   14  the lit decal - both draw in the additive pass only
	 *   15  the book material: a flipbook of consecutive pages that begins at
	 *       the part's own page, param (phase, page count, seconds per page,
	 *       0) - a fan's turning shadow and a bank of monitors in the opaque
	 *       pass, flames, smoke and rain rings in the additive one
	 *       (MaterialBookTime below)
	 *   16  the water material: the remaster's own still water surface -
	 *       every basin, cistern and pool the .TRG builds a sheet for, and
	 *       the sea of JOBY4 (MaterialRipple below) */
#define HD_TRG_MATERIAL_FLOW		1
#define HD_TRG_MATERIAL_MASK		6
#define HD_TRG_MATERIAL_WIND		8
#define HD_TRG_MATERIAL_WIND2		7
#define HD_TRG_MATERIAL_GLOW		2
#define HD_TRG_MATERIAL_DECAL		14
#define HD_TRG_MATERIAL_BOOK		15
#define HD_TRG_MATERIAL_WATER		16
#define HD_TRG_MATERIAL_FLIPBOOK	13

	/* The alpha reference of one part.
	 *
	 * The page cannot say whether to alpha-test: many opaque pages carry a
	 * mask in their alpha channel (gloss, wetness) while their RGB is a whole
	 * surface - ANDREA1's stucco facades (page 201, 95 % of it below alpha
	 * 128, and page 211, 96 %), JOBY2's snow (page 480, 99 %), JOBY3's
	 * bulkheads (pages 3219 to 4823, 41 to 71 %) and ANDY1's gravel (pages
	 * 3268 to 3272, 60 to 88 %). Testing those would throw away most of a
	 * Roman shop front, the wall under the Caffe Recco's awning, and the
	 * puddles of the crane yard.
	 *
	 * The material says it, and the material alone. Measured over the 15
	 * .TRG files with TrgProbe -all -mat <n>, which prints one line per
	 * level and distinct (material, param) tuple - the per-level tuples
	 * counted here and in the material list above:
	 *
	 *   material 6   param[0] 0.500 in 10 of its 11 per-level tuples,
	 *                ANDY1's other one 0.503; opaque only, 513,647 tris
	 *   material 7   param[0] 0.500 in all 7, opaque, 88,726 tris
	 *   material 8   param[0] 0.500 in 25 of 26, ANDY2's one 0.120 and in
	 *                the alpha pass; 1,308,731 tris opaque, 154 alpha
	 *                (that one), 738 additive
	 *   material 3   param[0] 1, 2, 4, 26, 32, 35, 39, 56, 69, 78, 107,
	 *                109, 110, 141, 161, 191 - a specular exponent
	 *   material 17  param[0] 22, 25, 36, 38, 63, 70, 72 - the same shape
	 *   material 13  param[0..2] are page numbers, material 14 likewise
	 *   material 2   param[0] 0.5, 1.0, 1.5, but additive in every one of
	 *                its 36 per-level tuples, where no alpha test runs
	 *
	 * So 6, 7 and 8 are the three materials whose param[0] is an alpha
	 * reference, and every other material's param[0] is measurably
	 * something else. The test is switched on for those three and reads its
	 * threshold out of the file rather than assuming 0.5, which keeps ANDY1's
	 * 0.503 right as well.
	 *
	 * Zero means "no test": the shader clips on texel.a - gPass.x, and an
	 * alpha is never below zero.
	 *
	 * Compared whole, not by its low byte. A destructible static carries its
	 * material with bit 15 set and a counter in bits 8 to 14 - 0x8003 and
	 * 0x820E are material 3 and material 14 of RICH3. Over the 15 files the
	 * only low bytes that ever appear with the high bit are 0, 3 and 14, and
	 * none of the three is a cut-out material, so masking would change
	 * nothing. */
	inline float MaterialAlphaRef(const trf::remaster::TrgPart& part)
	{
		if (part.material != HD_TRG_MATERIAL_MASK &&
			part.material != HD_TRG_MATERIAL_WIND &&
			part.material != HD_TRG_MATERIAL_WIND2)
			return 0.0f;

		/* A reference outside 0..1 would clip everything or nothing; no part
		 * in the game has one, and this says so rather than trusting it. */
		return part.param[0] > 0.0f && part.param[0] < 1.0f
			? part.param[0] : 0.5f;
	}

	/* Whether a range is drawn with its back faces culled.
	 *
	 * The opaque surfaces of a room are drawn one-sided. Drawn with no
	 * culling, the back of a wall paints over whatever
	 * stands behind it - RICH2's X-ray room, looked at from room 6 through
	 * the scanner glass, shows the rear face of group 11's corridor wall as
	 * a flat green pane where the corridor, its crates and the scanned Lara
	 * should be. D3DCULL_CCW culls exactly those faces: the .TRG winds its
	 * front faces clockwise on the screen, which is Direct3D's own front
	 * (RoomProbe: D3DCULL_CW at the same view shows the pane and loses the
	 * corridor).
	 *
	 * The art is made for it. Over one view from the middle of every third
	 * room of ANDREA1, JOBY2 and RICH2 (192 pictures each way) culling turns
	 * 25,660 pixels to the clear colour, four views more than 500 each: a
	 * near object's back at the edge of JOBY2 room 21's view, a wall's back
	 * that hid RICH2 room 108's corridor, and the one place it opens a wall
	 * - two window openings of one JOBY2 building (rooms 138 and 153),
	 * through which the back of the opening's own side wall was seen, and
	 * behind which no room is drawn from there.
	 *
	 * Only the opaque ranges: pass 0 with no alpha test. A cut-out is a
	 * flag, a leaf or a railing drawn as one card that is seen from both
	 * sides; the alpha pass is a cut-out throughout (the 0.5 reference in
	 * draw_pass); the glass and the additive light are sheets with no
	 * thickness. A water surface is seen from above and from below, and the
	 * sky dome is seen from inside, so neither is culled whatever pass it
	 * is in. */
	inline int RangeCulls(int pass, float alphaRef, float blend, bool water,
		bool sky)
	{
		return pass == 0 && alphaRef == 0.0f && blend == 0.0f && !water &&
			!sky ? 1 : 0;
	}

	/* The weight of one additive part.
	 *
	 * The additive pass is blended ONE / ONE with the depth write off, so
	 * overlapping parts sum. The remaster's light shafts and lamp cones are
	 * fans of dozens of quads that all meet at the lamp, and its caustic net
	 * is one sheet over a whole sea bed, so a pass that paints texel x light
	 * with no weight of its own saturates wherever the artist stacked
	 * geometry. Unweighted, measured through RoomProbe on the additive pass
	 * alone, over the pixels it covers:
	 *
	 *     JOBY4  room 19   caustic net    98.1 % mean, 86.9 % of it white
	 *     ANDY3  room 126  window glow    72.5 % mean, 22.1 % of it white
	 *     RICH2  room 25   wall lamps     21.9 % mean,  4.3 % of it white
	 *     RICH1  room 136  ray fan        22.7 % mean,  2.2 % of it white
	 *
	 * The remaster's own picture of the same corridor carries soft streaks of
	 * about a quarter of white with nothing saturated at all.
	 *
	 * A .TRG part carries a material and four floats, and the four mean
	 * different things per material: material 13's param[0..2] are texture
	 * layer numbers (the other three frames of the flipbook, layers 123 to
	 * 126 of JOBY2 = pages 3180 to 3183) and its param[3] is 0.15, the
	 * classic flipbook's own cadence of five frames at 30 Hz to within a
	 * sixtieth of a second. So the weight slot is found per material, slot by
	 * slot, over the per-level (material, param) tuples of all fifteen levels:
	 *
	 *   material  2  p0 in {0.5, 1.0, 1.5} and p2 in {0, 0.1, 0.4, 0.6, 0.8,
	 *                1.0, 2.0} move together and with the page - a preset;
	 *                p1 in [0, 2.68] and p3 in [0.05, 16] vary inside one
	 *                preset. p3 is a time (see MaterialAddPeriod), so p1 is
	 *                the weight: 0.3 for the ray fans of ANDREA1, RICH2 and
	 *                RICH3, 0.2 for RICH1's, 0.05 for the little candle
	 *                glows of page 2002.
	 *   material 14  p2 and p3 are zero in all sixteen tuples, p0 is an
	 *                integer layer number (0, 12, 26, 32, 38, 45, 50, 61,
	 *                102, 139), so p1 in [0.108, 1.0] is the only float
	 *                left - and it never leaves 0..1.
	 *   material 15  p0 is a phase, p1 a page count (14, 29 or 30) and p3
	 *                zero in every tuple - a book of pages
	 *                (MaterialBookTime) - and p2 the seconds a page stands,
	 *                0.025 to 0.1 on the additive books. The file carries no
	 *                weight for a book, so p2 doubles as it (see the switch
	 *                in MaterialAddWeight).
	 *   material 16  p2 in [0.035, 0.2], inside the range of material 15's
	 *                page time (0.025 to 0.2 over all its books) and on the
	 *                same slot; p0 and p1 pair up with the page.
	 *
	 * Where the file gives no usable number, the preset answers. Four
	 * material 2 tuples carry a weight outside (0, 1); taken as full
	 * brightness they turn RICH1's lift shaft into a white block and ANDY's
	 * window glows far too bright. Measured through RoomProbe on the additive
	 * pass alone over a black background, mean brightness as a share of
	 * white, against the remaster's own 16 to 31 percent:
	 *
	 *                                  at weight 1         with this rule
	 *     RICH1 room 13, from below   36.3 %, 7.3 white   21.4 %, 0.1
	 *     RICH1 room 13, inside it    73.4 %, 35.5        48.7 %, 7.5
	 *     ANDY1 room 194              85.5 %, 11.8        51.2 %, 0.0
	 *     ANDY2 room 34               69.1 %, 23.8        34.2 %, 0.0
	 *     ANDY3 room 86               38.6 %,  0.2        17.8 %, 0.0
	 *     ANDY3 room 150              26.9 %,  0.0        15.7 %, 0.0
	 *
	 * The second line is the camera inside the fan of quads, where every one
	 * of them overlaps every other and a ONE / ONE sum saturates whatever
	 * weight it is given - that is stacking and not weighting, and the
	 * classic path has no answer to it either.
	 *
	 * The rule comes out of the distribution: TrgProbe -mat 2 over the
	 * fifteen files prints 36 per-level tuples, 32 of them with p1 in (0, 1).
	 * Grouped by the preset - the (param[0], param[2]) pair that moves with
	 * the page - the weight is all but constant inside a preset:
	 *
	 *     (0.5, 0.0)  0.10                              1 tuple
	 *     (0.5, 0.6)  0.40 0.40 0.55 0.55 0.55 0.75 0.75    median 0.55
	 *     (0.5, 0.8)  0.40 0.40 0.40                        all three
	 *     (0.5, 1.0)  0.20                              1 tuple
	 *     (1.0, 0.1)  0.05 x5, 0.20 x4                      median 0.05
	 *     (1.0, 2.0)  0.20 0.20
	 *     (1.5, 0.0)  0.10 0.10 0.10 0.10                   all four
	 *     (1.5, 0.4)  0.30 0.30 0.30 0.30 0.30              all five
	 *
	 * and the four tuples that carry no weight belong to just two of those
	 * presets: ANDY1, ANDY2 and ANDY3's window glows are (0.5, 0.8), whose
	 * three in-range members are 0.40 to the digit, and RICH1's shaft is
	 * (0.5, 0.6), whose seven run 0.40 to 0.75 with a median of 0.55. So the
	 * part takes its own preset's weight instead of full white.
	 *
	 * The last return is a floor and not a reading: a preset nothing in this
	 * game names gets 0.20, the median of all 32 in-range material 2 weights,
	 * so that "no number" never means "full white" in a level this table has
	 * not seen.
	 *
	 * Materials 14 and 15 need none of this. 15 has no tuple outside its
	 * range at all, and 14's only ones are exactly 1.0 (RICH1 and RICH3,
	 * 2,304 triangles) beside weights that run to 0.8182 - the top of a range
	 * and not a missing number - so they keep the 1 the guard gives them.
	 *
	 * Everything else - materials 0, 1, 6, 7, 8, 13, 16 and the destructible
	 * statics' high-bit numbers - has no slot that behaves like a weight and
	 * draws at weight 1. Material 1 is the largest of those by far (116,128
	 * additive triangles in JOBY5 alone): its four floats are two scroll
	 * speeds and two numbers that behave like times. */
	struct HDAddPreset
	{
		float p0, p2, weight;
	};

	/* The material number without the destructible bits. Bit 15 marks a
	 * destructible copy and bits 8..14 count the copies, so a destructible
	 * pane of material 14 arrives as 32782 and would match nothing. No
	 * material in any of the fifteen files is above 22, so the low byte is
	 * the material. */
	inline unsigned int MaterialBase(const trf::remaster::TrgPart& part)
	{
		return part.material & 0xFFu;
	}

	/* How see-through this part is, or 0 for one that is not see-through at
	 * all.
	 *
	 * An alpha test - `clip(texel.a - 0.5)` and the surface drawn solid - is
	 * right for a grating and wrong for glass: a texel at alpha 0.55 comes
	 * out fully opaque and one at 0.45 disappears, so a smooth pane becomes a
	 * hard black-and-white mask of itself (ANDREA2's glass wall turns into a
	 * dark pattern of shards).
	 *
	 * The alpha pass of all fifteen files carries five materials, and only
	 * one of them is glass:
	 *
	 *     0, 6, 7, 8  the cut-outs - gratings, foliage, nets. Their pages
	 *                 really do cut: ANDREA2's page 3007 is 80.6 % at alpha
	 *                 below 16 and its glass is the rest.
	 *     1           the sky's cloud layers, in the sky group alone
	 *    22           the sky dome itself, exactly 1280 triangles per level
	 *    14           the decal material, and this is the glass: 4,292
	 *                 triangles in ANDREA2, 15,904 in JOBY3, 19,028 in
	 *                 JOBY5, 2,510 in RICH2, 3,894 in RICH3, 432 in
	 *                 RICHCUT2, 576 in TITLE
	 *
	 * `param[1]` of a material 14 part is its opacity - the same field
	 * MaterialAddWeight reads as the additive weight of the same material -
	 * and the game's values are 0.1, 0.2333, 0.8417 and 1.0. So the glass is drawn
	 * with SRCALPHA / INVSRCALPHA, no depth write, and an alpha of the page's
	 * own times that number, after everything opaque.
	 *
	 * Only the alpha pass, and only material 14: the cut-outs keep the
	 * alpha test. A floor whose page is part-way clear in every texel is
	 * blended too, whatever its material, but decided per triangle out of
	 * its page rather than per part out of its material, and drawn in a
	 * phase of its own - see HD_PASS_CLEAR in hd_render.h. */
	bool g_no_blend = false;	// see HDRenderBlendOverride
	bool g_no_clear = false;	// see HDRenderClearOverride
	bool g_no_upright = false;	// see HDRenderUprightOverride

	inline float MaterialBlend(const trf::remaster::TrgPart& part, int pass)
	{
		if (g_no_blend || pass != 1 ||
			MaterialBase(part) != HD_TRG_MATERIAL_DECAL)
			return 0.0f;

		const float opacity = part.param[1];

		// Out of range means "the page's own alpha and nothing on top".
		if (opacity <= 0.0f || opacity > 1.0f)
			return 1.0f;

		return opacity;
	}

	inline float MaterialAddWeight(const trf::remaster::TrgPart& part)
	{
		float weight;

		/* The sun shafts of the great hall. At the file's own weight the
		 * shafts in ANDREA1's hall with the gladiator statue (room 181) are
		 * far too strong, while the VCI corridor's streaks are right. The two
		 * can be told apart by the file:
		 *
		 *     the hall     material 2, (1.5, 0.3, 0.4, 16.0)
		 *     the corridor material 2, (1.0, 0.2, 0.1, 1.0)
		 *
		 * param[3] = 16 seconds is the slowest drift in the whole game and
		 * belongs to exactly one tuple, which 34 parts in ANDREA1 (12
		 * groups), JOBY2 (21) and RICH3 (1) carry: the sun shafts. The next
		 * slowest is 5 s, so the tuple is the key and no room number is
		 * needed.
		 *
		 * Measured on room 181's own additive pass, against black: 6.8 % mean
		 * and a peak of 58 % of white at the file's own 0.3. A streak is held
		 * to 16 to 31 % of white and these should be very faint, so the
		 * weight puts the peak at about 15 %: 0.3 * 15 / 58 = 0.078.
		 *
		 * RICH2's and ANDREA2's shafts share the preset but not the time
		 * (2.5 s and 3.07 s) and are untouched, which is why the whole tuple
		 * and not the preset is the key. */
		static const struct { float p[4]; float weight; } kDim[] =
		{
			{ { 1.5f, 0.3f, 0.4f, 16.0f }, 0.08f },
		};

		if (part.material == HD_TRG_MATERIAL_GLOW)
			for (int i = 0; i < (int)(sizeof(kDim) / sizeof(kDim[0])); i++)
				if (part.param[0] == kDim[i].p[0] && part.param[1] == kDim[i].p[1] &&
					part.param[2] == kDim[i].p[2] && part.param[3] == kDim[i].p[3])
					return kDim[i].weight;

		/* Material 16 is not a weight even though its param[2], 0.035 to
		 * 0.2, lies inside the range of material 15's page time (0.025 to
		 * 0.2): it is the remaster's still water surface (TrgProbe -matparts
		 * 16) - 28 parts in six levels, 17 of them a double sheet two units
		 * apart with opposite normals, every one sitting 1 to 20 units off
		 * the classic minfloor of a ROOM_REFLECT room, its parameters scaling
		 * with the area of the body of water. It falls through to 1.0 like
		 * every other material; MaterialRipple reads its three numbers. */
		switch (part.material)
		{
		case HD_TRG_MATERIAL_GLOW:
		case HD_TRG_MATERIAL_DECAL:
			weight = part.param[1];
			break;

		/* A book's param[2] is its page time, the seconds a page stands
		 * (MaterialBookTime), and doubles as its additive weight: the file
		 * carries no weight for a book, and no reference exists to measure
		 * one against. */
		case HD_TRG_MATERIAL_BOOK:
			weight = part.param[2];
			break;

		default:
			return 1.0f;
		}

		if (weight > 0.0f && weight < 1.0f)
			return weight;

		if (part.material != HD_TRG_MATERIAL_GLOW)
			return 1.0f;

		/* The two presets the game's own four unreadable tuples belong to,
		 * and the median that catches anything else. */
		static const HDAddPreset kPresets[] =
		{
			{ 0.5f, 0.8f, 0.40f },
			{ 0.5f, 0.6f, 0.55f },
		};

		for (int i = 0; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); i++)
			if (part.param[0] == kPresets[i].p0 && part.param[2] == kPresets[i].p2)
				return kPresets[i].weight;

		return 0.20f;
	}

	/* How long one swing of an additive part takes, in seconds, or 0 for a
	 * still one.
	 *
	 * The remaster's light streaks move: two shots of the same corridor three
	 * seconds apart show them in different places. The only slot in the file
	 * that is measurably a time is param[3], and material 13's 0.15 seconds
	 * shows it (see above). Material 2 fills the same slot with
	 * 0.05 to 16 seconds - a candle's flicker at one end and a sun shaft's
	 * slow drift at the other - and it is the only additive material that
	 * fills it at all: 14, 15 and 16 carry a hard zero there in every tuple
	 * of every level, and they stay still.
	 *
	 * The range is fenced off rather than trusted: below a twentieth of a
	 * second a swing is faster than the frame it is drawn in, and above a
	 * minute it is not motion any more. No part in the game is outside it.
	 *
	 * And a swing faster than twice a second draws still. The wave is a drift
	 * of a few per cent along the shaft; at the shortest periods of the file
	 * it turns into a flicker instead - ANDREA2 room 131's window shaft
	 * carries 0.06 s, which makes its swing a 16.7 Hz pulse of +-1.4 per cent
	 * of the pixel (RoomProbe -time over that shaft's additive pass). The
	 * material 2 parts under half a second are 85 over eight levels
	 * (RoomProbe's "still" count per level): the candle halos of page 2002 at
	 * 0.2 s and the window glows and shafts of ANDREA1 to ANDREA3, ANDY1 to
	 * ANDY3, RICH1 and TITLE at 0.05 to 0.35 s. Still is the one reading of
	 * those numbers that cannot flicker; param[3] stays a period for every
	 * part at or above the fence. */
	bool g_keep_fast_swing = false;	// see HDRenderSwingOverride

	const float kAddStillBelow = 0.5f;

	inline float MaterialAddPeriod(const trf::remaster::TrgPart& part)
	{
		if (part.material != HD_TRG_MATERIAL_GLOW)
			return 0.0f;

		if (part.param[3] < 0.05f || part.param[3] > 60.0f)
			return 0.0f;

		if (part.param[3] < kAddStillBelow && !g_keep_fast_swing)
			return 0.0f;

		return part.param[3];
	}

	/* The glow shader's gain on a material 2 part's weight, per preset - the
	 * whole parameter tuple, exactly as the .TRG stores it - and 1 for a tuple
	 * the table does not name.
	 *
	 * The strength follows the church mist of ANDY2 room 6 (page 2100,
	 * (1, 0.2, 2, 1)), which stays barely visible. Its share of the frame -
	 * the frame with material 2 drawn against the frame with no additive
	 * material at all, in luma - is 5.1 of 255 on average and 8.0 at the 95th
	 * percentile over the pixels its glow lights above 2 (RoomProbe -glow,
	 * -addmat 2 against -addmat 99, room 6 from 8150 7000 2100, yaw -94,
	 * pitch 2.2, fov 86.2). Each gain below puts the same 95th percentile of
	 * its tuple's brightest RoomProbe view, named beside it, at 8 of 255
	 * within 0.3; the mean of that view then stays under the mist's 5.1,
	 * because the shafts and cones are streaked where the mist is flat. The
	 * gains are found by drawing and not by division: an 8-bit frame drops
	 * a card's share once it falls under half a step, so at these strengths
	 * the picture does not follow the gain in proportion. The tuples need
	 * gains far apart - ANDREA2 room 131's window shaft 0.0273, the title's
	 * ray fans 0.171, JOBY5 room 115's glow 0.584 - so no single factor does.
	 *
	 * Not in the table, and so at 1: the mist itself, the candle halos of
	 * page 2002 (1, 0.05, 0.1, 0.2) that burn beside it in room 6, ANDREA1's
	 * haze of page 3232 (0.5, 0.1, 0, 0.663), which after the fades lights
	 * 1.9 per cent of ANDREA1 room 19's view above 2 of 255, at 6.7 on
	 * average there, and RICH2 room 78's glow of page 3225
	 * (1.5, 0.1, 0, 1.933), 6.9 at the 95th percentile in its own view. */
	inline float GlowGain(const trf::remaster::TrgPart& part)
	{
		static const struct { float p[4]; float gain; } kGains[] =
		{
			// window shafts and window glows of pages 3037, 3038 and 3232
			{ { 0.5f, 0.4f, 0.6f, 0.1f }, 0.0146f },		// ANDREA2 room 225
			{ { 0.5f, 0.4f, 0.6f, 0.5f }, 0.0313f },		// ANDREA2 room 234
			{ { 0.5f, 0.55f, 0.6f, 0.06f }, 0.0273f },		// ANDREA2 room 131
			{ { 0.5f, 0.55f, 0.6f, 0.13f }, 0.0523f },		// ANDREA1 room 157
			{ { 0.5f, 2.68333244f, 0.6f, 0.326666653f }, 0.0352f },	// RICH1 room 3
			{ { 0.5f, 0.4f, 0.8f, 0.05f }, 0.0221f },		// ANDREA3 room 20
			{ { 0.5f, 0.4f, 0.8f, 0.08f }, 0.0245f },		// ANDREA3 room 34
			{ { 0.5f, 0.4f, 0.8f, 0.2f }, 0.0128f },		// ANDREA3 room 36
			// the underwater rays of page 3256
			{ { 0.5f, 0.75f, 0.6f, 0.5f }, 0.123f },		// JOBY5 room 65
			{ { 0.5f, 0.75f, 0.6f, 0.8f }, 0.0694f },		// JOBY4 room 19
			// glows of page 3225
			{ { 0.5f, 0.0f, 0.8f, 0.05f }, 0.0435f },		// ANDY2 room 34
			{ { 0.5f, 0.0f, 0.8f, 0.213333309f }, 0.0683f },	// ANDY1 room 194
			{ { 1.5f, 0.1f, 0.0f, 1.5333333f }, 0.584f },		// JOBY5 room 115
			{ { 1.5f, 0.1f, 0.0f, 5.0f }, 0.394f },		// JOBY3 room 10
			// the title's ray fans of page 2146 and its glows of page 2134
			{ { 1.0f, 0.2f, 2.0f, 0.35f }, 0.171f },		// TITLE room 6
			{ { 0.5f, 0.2f, 1.0f, 0.1f }, 0.118f },		// TITLE room 19
			// lamp glows and lamp cones of page 3070; RICH1 room 17's glow,
			// which no camera in its room sees, takes its sibling's gain
			{ { 1.0f, 0.2f, 0.1f, 0.6f }, 0.276f },		// RICH1 room 15
			{ { 1.0f, 0.2f, 0.1f, 1.0f }, 0.276f },		// RICH1 room 15
			{ { 1.0f, 0.2f, 0.1f, 2.0f }, 0.333f },		// TITLE room 52
			{ { 1.5f, 0.3f, 0.4f, 2.5f }, 0.211f },		// RICH2 room 120
			{ { 1.5f, 0.3f, 0.4f, 3.06666636f }, 0.458f },	// ANDREA2 room 68
			{ { 1.5f, 0.3f, 0.4f, 16.0f }, 0.396f },		// RICH3 room 65
		};

		for (int i = 0; i < (int)(sizeof(kGains) / sizeof(kGains[0])); i++)
			if (part.param[0] == kGains[i].p[0] && part.param[1] == kGains[i].p[1] &&
				part.param[2] == kGains[i].p[2] && part.param[3] == kGains[i].p[3])
				return kGains[i].gain;

		return 1.0f;
	}

	/* The stacked sea: an additive material 16 part whose triangles come in
	 * three or more exact copies of one another, stacked in y.
	 *
	 * JOBY4's sea (group 19, part 3) is 73,728 triangles and only 18,048
	 * distinct footprints: every triangle is there four times, at y -4269,
	 * -4253 and twice at -4237. JOBY5 carries the same fourfold sheet in
	 * groups 65 and 82 (part 2, y -10004, -9996 and twice -9988) and a
	 * threefold one in group 117 (part 8, y 3839, 3841, 3843). Drawn ONE /
	 * ONE at weight 1 with a vertex red of 0, four copies clip green and blue
	 * and lock the hue to cyan: 40.2 per cent of the additive pixels of the
	 * view up at JOBY4 room 19's ceiling have a channel at 250 or more
	 * (RoomProbe, the additive pass alone). Measured over all fifteen .TRG
	 * files (a footprint count through the vendored TRForge reader, and
	 * RoomProbe's additive block per level), no other material 16 part
	 * stacks more than twice.
	 *
	 * The two-sheet stacks - a surface two units above its twin, 17 of the
	 * 28 material 16 parts - are the double sheets MaterialAddWeight speaks
	 * of, and stay as they are: a copy count of two never qualifies.
	 *
	 * A copy is found by a footprint key: the three corners' x and z, sorted,
	 * and the two other corners' height above the first - so only an exact
	 * vertical translate of a triangle is its copy. Where a footprint holds
	 * three or more triangles, the ones at the smallest y (the topmost - y
	 * counts downwards) are kept and every higher-y copy is marked. JOBY4's
	 * 192 footprints with twelve triangles are three distinct triangles four
	 * times over; the three at the smallest y stay.
	 *
	 * `drop` comes back with one flag per triangle of the run and empty when
	 * nothing is stacked, which is every part of thirteen levels. */
	bool g_keep_stacks = false;	// see HDRenderStackOverride

	bool MarkStackedCopies(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart::Pass& run, std::vector<unsigned char>& drop)
	{
		struct Copy
		{
			int key[8];
			int ysum;
			unsigned int tri;
		};

		std::vector<Copy> copies;

		drop.clear();
		copies.reserve(run.count / 3);

		for (unsigned int at = 0; at + 2 < run.count; at += 3)
		{
			const unsigned int first = run.start + at;

			if (first + 2 >= trg.indices.size())
				break;

			int corner[3][3];
			bool inside = true;

			for (int k = 0; k < 3; k++)
			{
				const unsigned int index = trg.indices[first + (unsigned int)k];

				if (index >= trg.vertices.size())
				{
					inside = false;
					break;
				}

				const trf::remaster::TrgVertex& v = trg.vertices[index];

				corner[k][0] = v.x;
				corner[k][1] = v.z;
				corner[k][2] = v.y;
			}

			if (!inside)
				continue;

			// The corners in (x, z) order, so the key does not depend on
			// which corner the file names first.
			for (int i = 0; i < 2; i++)
				for (int j = 0; j < 2 - i; j++)
					if (corner[j][0] > corner[j + 1][0] ||
						(corner[j][0] == corner[j + 1][0] && corner[j][1] > corner[j + 1][1]))
						for (int c = 0; c < 3; c++)
						{
							const int t = corner[j][c];
							corner[j][c] = corner[j + 1][c];
							corner[j + 1][c] = t;
						}

			Copy copy;

			copy.key[0] = corner[0][0]; copy.key[1] = corner[0][1];
			copy.key[2] = corner[1][0]; copy.key[3] = corner[1][1];
			copy.key[4] = corner[2][0]; copy.key[5] = corner[2][1];
			copy.key[6] = corner[1][2] - corner[0][2];
			copy.key[7] = corner[2][2] - corner[0][2];
			copy.ysum = corner[0][2] + corner[1][2] + corner[2][2];
			copy.tri = at / 3;
			copies.push_back(copy);
		}

		std::sort(copies.begin(), copies.end(),
			[](const Copy& a, const Copy& b)
			{
				for (int k = 0; k < 8; k++)
					if (a.key[k] != b.key[k])
						return a.key[k] < b.key[k];

				return a.ysum < b.ysum;
			});

		bool any = false;

		for (size_t i = 0; i < copies.size(); )
		{
			size_t j = i + 1;

			while (j < copies.size() &&
				std::memcmp(copies[j].key, copies[i].key, sizeof(copies[i].key)) == 0)
				j++;

			if (j - i >= 3)
				for (size_t k = i; k < j; k++)
					if (copies[k].ysum > copies[i].ysum)
					{
						if (!any)
							drop.assign(run.count / 3, 0);

						drop[copies[k].tri] = 1;
						any = true;
					}

			i = j;
		}

		return any;
	}

	/* The UV span of one part, in page units.
	 *
	 * A .TRG vertex carries its two texture coordinates as the alpha byte of
	 * each of its two baked colours (see the note in hd_room.hlsl), so a
	 * part's own rectangle on its page is the min and max of those bytes over
	 * the vertices its three index runs name. 23 of the 28 material 16 parts
	 * in the game live in a sub-rectangle of their page - the smallest is 40
	 * of 255 across, JOBY5's group 4 - and the sampler is on CLAMP for a
	 * still surface, so anything that moves a coordinate has to know how far
	 * it may move it before it walks off its own rectangle. The cap in
	 * MaterialRipple is a tenth of that rectangle's smaller side; it is a bound on the travel
	 * and not a proof that the sample stays inside the rectangle, because a
	 * pixel already on the rim can be pushed that tenth over it.
	 *
	 * Walked over all three passes, so the answer is the part's and not the
	 * pass's; it is asked once per material 16 part at level build, which is
	 * 28 parts in the whole game. Holding the ripple's travel to the part's
	 * own rectangle is TROne's own design for its material 16 water
	 * (Copyright 2026 Todesengel, GPLv3, TROne-main, copy of 2026-10-06,
	 * commit 75c4c843); the tenth and the anchors are this file's.
	 *
	 * The rectangle rests on u and v being the alpha bytes at +15 and +19,
	 * which TrgProbe -uvbytes counts on the files (hd_room.hlsl, the vertex
	 * note). */
	inline bool PartUvSpan(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part, float& spanU, float& spanV)
	{
		int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
		bool any = false;

		for (int pass = 0; pass < 3; pass++)
		{
			const trf::remaster::TrgPart::Pass& run = part.pass[pass];

			for (unsigned int at = 0; at < run.count; at++)
			{
				const unsigned int index = run.start + at;

				if (index >= trg.indices.size())
					break;

				const unsigned int v = trg.indices[index];

				if (v >= trg.vertices.size())
					continue;

				const int u = (int)trg.vertices[v].u;
				const int w = (int)trg.vertices[v].v;

				if (u < u0) u0 = u;
				if (u > u1) u1 = u;
				if (w < v0) v0 = w;
				if (w > v1) v1 = w;
				any = true;
			}
		}

		spanU = any ? (float)(u1 - u0) * (1.0f / 255.0f) : 0.0f;
		spanV = any ? (float)(v1 - v0) * (1.0f / 255.0f) : 0.0f;
		return any;
	}

	/* The ripple of one still water surface.
	 *
	 * Material 16 is a surface of standing water: its 28 parts are fountains,
	 * basins, mill ponds and pools plus the two great seas of JOBY4 and
	 * JOBY5; 17 of them are a double sheet two units apart whose halves face
	 * up and down (the view out of the air and out of the water); all sit
	 * within 20 units of the classic minfloor of a reflecting room; and page
	 * 3255, which 90 % of the material's triangles wear, is a grey noise
	 * field with no hole in it.
	 *
	 * Its three numbers scale with the area of the body of water,
	 * monotonically, over the whole game:
	 *
	 *     3538 squares (JOBY4's sea)    param 0.1  1.0   0.035
	 *      900 squares (JOBY5's sea)          0.3  1.0   0.08
	 *       36 squares (a large pool)         0.8  0.7   0.1
	 *     1-28 squares (a basin)              0.8  0.7   0.2
	 *        4 squares (a puddle)             5.0  0.1   0.5
	 *
	 * A brightness would not do that; a wave would. param[0] behaves like a
	 * spatial frequency (a big sea has long waves, a puddle short ones) and
	 * param[2] like a rate (a big sea's waves are slow, a puddle's quick).
	 * param[1] is the one that does not follow the area, so it is read as an
	 * amplitude.
	 *
	 * Origin. That param[1] is the travel is our own reading of the table
	 * above: it is the one slot that does not follow the area. Which of the
	 * other two is the frequency and which the rate is a FREE CHOICE of this
	 * project, not a result: over the game as a whole both are smallest on
	 * the two seas and largest on the puddle (param[0] from 0.1 to 5.0,
	 * param[2] from 0.035 to 0.5, TrgProbe -mat 16), though not in step in
	 * every pair of tuples - (0.3, 0.08) against (0.4, 0.04), (0.8, 0.2)
	 * against (1.0, 0.1) - and no count of ours separates them. The order
	 * settled on - param[0] the scale, param[1] the travel, param[2] the rate
	 * - coincides with the order TROne's prose names for these three slots
	 * (TROne-main, copy of 2026-10-05). TROne states that it read that order
	 * off the Remaster's own shader programs, which this project uses neither
	 * at first hand nor at second, so the coincidence is written down here
	 * and is not taken as evidence; for that one bit there is no measurement
	 * of our own. If the sea ripples too finely and too fast while the
	 * fountain is too coarse and too slow, the choice is the wrong way round.
	 * The reversal is not a swap of out[0] and out[2]: each anchor below
	 * belongs to its parameter, so the sources change places and keep their
	 * anchors - the scale reads param[2] through the 0.2 anchor (floored at
	 * 0.0125), the rate reads param[0] through the 0.8 anchor. No constant of
	 * TROne's is used; every number below comes from a TR5 measurement. The
	 * structure around them - the travel held to the part's own UV span
	 * (PartUvSpan) and no movement in the vertex - is TROne's own design
	 * (Copyright 2026 Todesengel, GPLv3, TROne-main (copy of 2026-10-06,
	 * commit 75c4c843), its own water rule for material 16), and the wave in
	 * hd_room.hlsl is a different curve with different terms.
	 *
	 * The three anchors:
	 *
	 *   the scale   param[0] = 0.8 - the basin value, and the commonest in
	 *               the game - is one wave per one and a half floor squares,
	 *               1536 units, the size of a wave on a swimming pool.
	 *               Everything else follows in proportion. Floored at 0.05
	 *               so that a part whose parameters are all zero (JOBY5 group
	 *               115's second part, the twin of JOBY3's) still has a
	 *               finite wavelength.
	 *   the rate    the classic water surface walks one WaterTable row per
	 *               game step (wibble += 4 in control.cpp, and the row is
	 *               (wibble >> 2) & 0x3F), so its own period is 64 steps of
	 *               a 30 Hz clock: 2.13 seconds. param[2] = 0.2, the basin
	 *               value, is put on that period, and the rest follows in
	 *               proportion - 0.5 gives 0.85 s, 0.1 gives 4.3 s, 0.035
	 *               gives 12 s for JOBY4's sea, a swell rather than a ripple.
	 *   the travel  param[1] = 1 moves the coordinate by a tenth of the
	 *               part's own smaller UV span, and less for a smaller
	 *               number: on the tightest part in the game, JOBY5's group 4
	 *               at 40 of 255, that is 4 texels of a 256 page. A pixel
	 *               already on the rim can still be pushed over it; that does
	 *               not show, because the pages material 16 uses are noise
	 *               fields that fill the page. A visible bleed at the edge of
	 *               a small sheet is answered by clamping the sampled uv into
	 *               the part's rectangle, not by a smaller travel for all.
	 *
	 * Nothing here touches the vertex: `sway` is 0 on 27 of the 28 parts, and
	 * a sheet that bobbed would tear away from the stone rim it is let into.
	 * This is a texture ripple and nothing else - TROne's own reasoning for
	 * its material 16 water, and the same count here.
	 *
	 * Returns false for every other material in the game, and then out[] is
	 * three zeroes and the pixel shader's one compare skips the whole thing.
	 */
	inline bool MaterialRipple(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part, float* out)
	{
		out[0] = out[1] = out[2] = 0.0f;

		if (MaterialBase(part) != HD_TRG_MATERIAL_WATER)
			return false;

		float spanU = 0.0f, spanV = 0.0f;

		if (!PartUvSpan(trg, part, spanU, spanV))
			return false;

		const float span = spanU < spanV ? spanU : spanV;
		float scale = part.param[0];
		float travel = part.param[1];

		if (scale < 0.05f)
			scale = 0.05f;

		if (travel < 0.0f)
			travel = 0.0f;
		else if (travel > 1.0f)
			travel = 1.0f;

		// 0.8 -> one wave per 1536 units, counted in waves per unit: the
		// shader's own wave has period 1, not 2 pi.
		out[0] = scale * (1.0f / 1536.0f) / 0.8f;
		// param[1] = 1 -> a tenth of the smaller span, in page units.
		out[1] = travel * 0.1f * span;
		// 0.2 -> the classic surface's own 64 steps of a 30 Hz clock, in
		// waves a second.
		out[2] = part.param[2] > 0.0f
			? part.param[2] * (30.0f / 64.0f) / 0.2f : 0.0f;

		return out[1] > 0.0f;
	}

	/* The flipbook of one part.
	 *
	 * Material 13's param[0..2] are layer numbers - indices into the file's
	 * own texture list, exactly like TrgVertex::layer, counted from one.
	 * Measured over all fifteen levels, every material 13 part in the game
	 * (27 of them, in five levels):
	 *
	 *   JOBY2   room 58   4 opaque parts, layers 118..121, pages 3176..3179,
	 *                     param[3] = 9, 7, 11 and 6 seconds
	 *                     4 additive parts, layers 123..126, pages 3180..3183,
	 *                     param[3] = 0.15 s on all four
	 *           room 104  the same eight, 18 triangles each
	 *   JOBY3   room 4    layers 15..17, 0.2 s, 1,728 triangles
	 *           room 49   layers 87..89, 0.05 s, 90 triangles
	 *   JOBY5   rooms 4, 5  layers 57..59, 0.2 s
	 *           room 49   layers 115..117, 0.05 s
	 *   RICH1   7 rooms   layers 68, 69, 70, 57, pages 3064..3067, 0.2 s
	 *   RICH2   rooms 11, 159  layers 96..99, pages 3064..3067, 0.2 s
	 *           room 21   layers 120..123, pages 3310..3313, 0.5 s
	 *
	 * The sequence is (own layer, param[0], param[1], param[2]) and the parts
	 * of one surface are the same cycle at four phases: JOBY2's four opaque
	 * parts read 118 -> (119,120,121), 119 -> (120,121,118), 121 ->
	 * (118,119,120) and 120 -> (121,118,119). So a part's own page is frame 0
	 * and the three parameters are frames 1, 2 and 3, and the whole surface
	 * steps together. A repeat inside a sequence is a ping-pong and is left as
	 * it is written: JOBY3's 15 -> (16,17,16) is a three frame swing.
	 *
	 * param[3] is seconds per frame, the six to eleven included: room 58 of
	 * JOBY2 is a VCI control room whose wall is a bank of closed circuit
	 * monitors. Its opaque flipbook is the camera feed on those screens,
	 * switching every six to eleven seconds, and its additive one is the scan
	 * line glow over them at 0.15 s - the classic flipbook's own cadence of
	 * five frames at 30 Hz to within a sixtieth. Nothing else in the game
	 * reaches past 0.5 s. So the slot is a time over the whole measured
	 * range, fenced only against a file that says something absurd.
	 *
	 * A part whose numbers do not resolve - a layer outside the list, a
	 * non-integer, a page the cache has no slot for - keeps its own page for
	 * ever and is counted as a loss in the log. */
	inline float MaterialFlipTime(const trf::remaster::TrgPart& part)
	{
		if (part.material != HD_TRG_MATERIAL_FLIPBOOK)
			return 0.0f;

		return part.param[3] >= 0.02f && part.param[3] <= 60.0f
			? part.param[3] : 0.0f;
	}

	/* Frames 1, 2 and 3 of a material 13 part as page SLOTS. Frame 0 is the
	 * triangle's own page and is not in here - it is the range's pageSlot,
	 * which is read from the vertex like every other draw call's. False when
	 * the file's numbers do not resolve; `slots` is then untouched. */
	inline bool MaterialFlipSlots(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part, int* slots)
	{
		int found[3];

		for (int at = 0; at < 3; at++)
		{
			const float value = part.param[at];

			if (value < 1.0f || value > (float)trg.textures.size())
				return false;

			const int layer = (int)value;

			if ((float)layer != value)
				return false;

			found[at] = HDPageCacheSlotOf((int)trg.textures[layer - 1]);

			if (found[at] < 0)
				return false;
		}

		slots[0] = found[0];
		slots[1] = found[1];
		slots[2] = found[2];
		return true;
	}

	/* The book of one part.
	 *
	 * Material 15 is a flipbook of N consecutive pages. Its own page - the
	 * page its triangles name through their layer - is page 0, and page k is
	 * the page NUMBER page0 + k: param[0] is where in the cycle the part
	 * starts, param[1] how many pages the book holds, param[2] how many
	 * seconds each page stands, and param[3] is 0. Measured over all fifteen
	 * levels, 168 part-passes:
	 *
	 *   RICH1     pass 0, pages 3071..3100, 30 pages at 0.05 s, phase 0 - the
	 *             floor under each of the five ceiling fans, the blade shadow
	 *             turned 12 degrees anticlockwise from one page to the next
	 *   RICHCUT2  pass 0, pages 1139..1168 (30 at 0.2 s) and 1140..1168 (29,
	 *             the same loop one page on) - the bank of monitors
	 *   8 levels  pass 2, pages 2101..2130, 30 pages at 0.025 to 0.1 s,
	 *             phases 0, 1, 7, 15 and 22 - an atlas of flames, smoke, gas
	 *             and candle flames and a turning wireframe figure
	 *   ANDY1..3  pass 2, pages 3273..3286, 14 pages at 0.06 s - rain
	 *             rings spreading on wet ground
	 *
	 * By page number and not by layer: RICHCUT2 group 0 part 5 names layer 51
	 * = page 1139, and layers 52 to 58 hold parquet, carpet and water, while
	 * pages 1139..1168 are one clean loop of the monitors' picture. The page
	 * rule resolves all 168 part-passes, the layer rule breaks that one.
	 *
	 * The time is param[2], fenced like material 13's: a count of 2 to 64
	 * pages, a page of 0.02 to 0.5 seconds and a phase that is a whole number
	 * below the count, or 0 - "no book" - for anything else, which is every
	 * part in the game that is not material 15. */
	inline float MaterialBookTime(const trf::remaster::TrgPart& part)
	{
		if (part.material != HD_TRG_MATERIAL_BOOK)
			return 0.0f;

		const float count = part.param[1];
		const float phase = part.param[0];
		const float step = part.param[2];

		if (count < 2.0f || count > 64.0f || (float)(int)count != count)
			return 0.0f;

		if (phase < 0.0f || phase >= count || (float)(int)phase != phase)
			return 0.0f;

		return step >= 0.02f && step <= 0.5f ? step : 0.0f;
	}

	/* The page slots of every book of the level, pages 1 to N - 1 of each one
	 * after the other; page 0 is the range's own pageSlot, read from the
	 * vertex like every other draw call's. A book the level holds twice is
	 * resolved once: parts that share a first page and a count share an
	 * entry, which is also what lets them share a draw call. */
	std::vector<int> g_book_slots;

	struct HDBookRun
	{
		int page0;
		int count;
		int first;		// index of page 1 in g_book_slots
	};
	std::vector<HDBookRun> g_book_runs;

	/* Pages 1 to count - 1 of the book that begins at page `page0`, as page
	 * cache slots in g_book_slots: the index of page 1 there, or -1 when a
	 * page is off the file's texture list or has no slot in the cache. The
	 * cache is made from the file's own list, every layer of it, so a page
	 * the list does not hold has no slot either. */
	inline int MaterialBookSlots(int page0, int count)
	{
		for (size_t i = 0; i < g_book_runs.size(); i++)
			if (g_book_runs[i].page0 == page0 && g_book_runs[i].count == count)
				return g_book_runs[i].first;

		int found[64];

		for (int k = 1; k < count; k++)
		{
			found[k - 1] = HDPageCacheSlotOf(page0 + k);

			if (found[k - 1] < 0)
				return -1;
		}

		HDBookRun run;

		run.page0 = page0;
		run.count = count;
		run.first = (int)g_book_slots.size();

		for (int k = 1; k < count; k++)
			g_book_slots.push_back(found[k - 1]);

		g_book_runs.push_back(run);
		return run.first;
	}

	/* The page a part's run begins on - the page its first triangle names
	 * through the layer of its first corner - or -1 for an empty run or a
	 * layer that names no page. Every material 15 run of the game names one
	 * page throughout (TrgProbe -parts), and a triangle on any other page
	 * keeps its own page (see the part loop). */
	inline int RunFirstPage(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart::Pass& run)
	{
		if (!run.count || run.start >= trg.indices.size())
			return -1;

		const unsigned int corner = trg.indices[run.start];

		if (corner >= trg.vertices.size())
			return -1;

		const unsigned char layer = trg.vertices[corner].layer;

		return layer && (size_t)layer <= trg.textures.size()
			? (int)trg.textures[layer - 1] : -1;
	}

	/* The gloss of one part.
	 *
	 * Origin: the reading below is our own measurement, and nothing here is
	 * carried over from TROne. TROne-main (copy of 2026-10-06, commit
	 * 75c4c843) has a planar reflection and an environment rule for material
	 * 3 of its own; neither is used here, and our material 3 reading differs
	 * from TROne's. The one TROne remark this touches - that an opaque page's
	 * DDS alpha is a sparkle mask, not coverage (TROne-main, copy of
	 * 2026-10-06, commit 75c4c843, k_hd_frag) - is held against our own pages
	 * in the next paragraph.
	 *
	 * The alpha mask of a page is not a gloss source: RICH2's marble floors
	 * draw with pages 1010, 1032 and 1033, and all three are alpha 255 on all
	 * 262,144 texels of the top mip (DdsProbe -alpha).
	 *
	 * The file itself: two materials carry a pair that behaves like a
	 * specular one, over the distinct tuples of all fifteen levels:
	 *
	 *   material  3   param[0] an integer 1, 2, 4, 26, 32, 35, 39, 56, 69,
	 *                 78, 107, 109, 110, 141, 161, 191 - an exponent - and
	 *                 param[2] a fraction 0.333 to 1.0 - a strength.
	 *                 165,897 triangles: ANDREA1's marble halls (exponent 110,
	 *                 strength 0.85 over 42 rooms), ANDREA2, ANDY1 to ANDY3
	 *                 and JOBY2. param[3] is zero in every tuple.
	 *   material 17   param[0] 22, 25, 36, 38, 63, 70, 72 and param[1] 0.05 or
	 *                 0.1 - the same shape on the other slot. 17,453
	 *                 triangles, JOBY3, JOBY4 and JOBY5: the submarine's
	 *                 painted steel.
	 *
	 * RICH1 and RICH2 carry neither material, not one triangle; RICH3 carries
	 * material 3 only as destructible pieces - 640 triangles of group 46 on
	 * page 10084 as 0x8003, 0x8203, 0x8303 and 0x8403 (TrgProbe -mat) - so
	 * the lookup is on the base material (hd_gloss_math.h,
	 * HDGlossFromMaterial). Of the other material lookups, MaterialAlphaRef,
	 * MaterialAddWeight and MaterialBookTime compare the whole number and
	 * MaterialBlend the base: a destructible piece exists over the bases 0,
	 * 3 and 14 only, none of them a cut-out or a book, so the alpha reference
	 * and the book see no difference, and a destructible decal (base 14)
	 * draws its additive pass at weight 1 where its base part would take
	 * MaterialAddWeight's - left as it is.
	 *
	 * Zero means "no gloss", and a strength outside (0, 1] falls through to
	 * zero rather than being trusted - the same shape as MaterialAlphaRef and
	 * MaterialAddWeight above. The strength, the exponent and the base
	 * material go into the sort key; the class and F0 of the sheen follow
	 * from the base (ResolveSheen). */
	inline void MaterialGloss(const trf::remaster::TrgPart& part,
		float& strength, float& power, int& base)
	{
		HDGloss gloss;

		strength = 0.0f;
		power = 0.0f;
		base = 0;

		if (!HDGlossFromMaterial(part.material, part.param, &gloss))
			return;

		strength = gloss.strength;
		power = gloss.power;
		base = (int)HDGlossBaseMaterial(part.material);
	}

	/* Whether this part is lit at all (the sky dome).
	 *
	 * The remaster's sky is a group of its own past the last room, and the
	 * band of sky itself is one part of it - 1,280 triangles on a sphere of
	 * about four blocks' radius, wearing a 4096 x 2048 panorama out of the
	 * page block 7000..7011 (ANDREA1's is Rome at sunset, ANDY's the Irish
	 * hills at dusk), alpha tested so that the void above and below the band
	 * is cut away. Its baked light is exactly zero on every vertex of every
	 * level - measured, all 1,280 triangles, thirteen levels. Drawn through
	 * the room formula it would be black, so a part with no light anywhere is
	 * drawn at light 1 and its page is the whole picture.
	 *
	 * The test is on the data and not on a material number on purpose: the
	 * same group's cloud layers are material 1 and do carry light (0.26 to
	 * 2.00 over the game - ANDY's night sky is dark, ANDREA3's is bright),
	 * and RICH3's sky group carries one 50-triangle material 0 part that is
	 * unlit as well. Restricted to sky groups by the caller, because a room
	 * part with no light is a different thing: it is a part the remaster
	 * meant to be invisible. */
	inline bool PartIsUnlit(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part)
	{
		for (int pass = 0; pass < 3; pass++)
			for (unsigned int at = 0; at < part.pass[pass].count; at++)
			{
				const unsigned int index = part.pass[pass].start + at;

				if (index >= trg.indices.size())
					continue;

				const unsigned int corner = trg.indices[index];

				if (corner >= trg.vertices.size())
					continue;

				const trf::remaster::TrgVertex& v = trg.vertices[corner];

				if (v.colour1[0] | v.colour1[1] | v.colour1[2] |
					v.colour2[0] | v.colour2[1] | v.colour2[2])
					return false;
			}

		return true;
	}

	struct HDRange
	{
		unsigned int start = 0;
		unsigned int count = 0;
		int pageSlot = -1;
		float scrollU = 0.0f;
		float scrollV = 0.0f;
		/* The alpha reference of the part this range came out of, or 0 for
		 * "no test" - see MaterialAlphaRef above. Part of the sort key with
		 * the page and the scroll, for the same reason they are: it is one
		 * pixel shader constant per draw call and a triangle cannot have half
		 * of it. */
		float alphaRef = 0.0f;
		/* The additive weight and the swing time of the part this range came
		 * out of - see MaterialAddWeight above. Both are 1 and 0 outside the
		 * additive pass, and both are part of the sort key for the same
		 * reason the alpha reference is. */
		float addWeight = 1.0f;
		float addPeriod = 0.0f;
		/* 1 where the part carries no baked light at all and its own texture
		 * is the whole picture - the sky dome, and nothing else in the game
		 * (PartIsUnlit above). Sorted first so that the dome is painted
		 * before the cloud layers that share its group. */
		float flatLight = 0.0f;
		/* The flipbook of this draw call. `flipTime` is how long one frame
		 * stands, in seconds, and 0 means "no flipbook" - the range then
		 * keeps its own page for ever. `flipSlot` is frames 1, 2 and 3; frame
		 * 0 is `pageSlot`. Part of the sort key like the page itself, because
		 * a draw call is one SetTexture. */
		float flipTime = 0.0f;
		int flipSlot[3] = { -1, -1, -1 };
		/* The book of this draw call. `bookTime` is how long one page
		 * stands, in seconds, and 0 means "no book"; `bookFirst` is page 1's
		 * index in g_book_slots, `bookCount` the pages the book holds and
		 * `bookPhase` the page the part shows at time 0 - see
		 * MaterialBookTime. All four are in the sort key, because a draw call
		 * shows one page: parts with the same book, step and phase share one,
		 * parts that differ in any of them never do. `bookPart` is the .TRG
		 * part of the first triangle, for the log alone and not in the key. */
		float bookTime = 0.0f;
		int bookFirst = -1;
		int bookCount = 0;
		int bookPhase = 0;
		int bookPart = -1;
		/* The gloss of this draw call - the .TRG material's own strength and
		 * exponent, 0 for every part that names none. One pixel shader
		 * constant, hence part of the sort key. */
		float gloss = 0.0f;
		float glossPower = 0.0f;
		/* The base material the gloss above came from, 3 or 17, and 0 where
		 * it is zero - the class of a material's gloss follows it (see
		 * HDGlossFromMaterial). In the sort key beside the gloss. */
		int glossMat = 0;
		/* The sheen of this draw call: the gloss variant's constants
		 * (hd_room.hlsl, "The sheen"), resolved once the level's ranges are
		 * built (ResolveSheen below). `sheen` is the strength, and 0 keeps
		 * the draw call on the plain variant with the gloss above - which is
		 * every draw call while hd_reflect is off. `sheenMask` is 1 where the
		 * page's alpha is a gloss mask. Not in the sort key: a page names
		 * one sheen, and the page is in the key already. */
		float sheen = 0.0f;
		float sheenPower = 0.0f;
		float sheenF0 = 0.0f;
		float sheenTint = 0.0f;
		float sheenMask = 0.0f;
		int sheenClass = 0;
		int sheenSource = 0;
		/* The ripple of this draw call - the wave scale, the travel and the
		 * rate of a material 16 water surface, and three zeroes for every
		 * other draw call. In the sort key like every other per-draw-call
		 * constant. See MaterialRipple above. */
		float ripple[3] = { 0.0f, 0.0f, 0.0f };
		/* How see-through this draw call is, or 0 for one that is drawn
		 * solid. Non-zero puts the range in the blend pass: SRCALPHA /
		 * INVSRCALPHA, no depth write, drawn after everything opaque - see
		 * MaterialBlend above. In the sort key like every other per-draw-call
		 * constant. */
		float blend = 0.0f;
		/* 1 where the range is a clear floor - an alpha-pass range on a page
		 * HDPageCacheSlotClear answers true for, drawn with blend 1 (the
		 * page's own alpha and nothing on top) in HD_PASS_CLEAR instead of
		 * the glass's phase. In the sort key, so a floor and a pane never
		 * share a draw call. */
		int clear = 0;
		/* Which destructible piece these triangles belong to, or -1 for the
		 * room itself. In the sort key, so a piece's triangles never share a
		 * draw call with the wall behind them. */
		int destructId = -1;
		/* The .TRG material of an additive draw call, without the
		 * destructible bits, and 0 in the other two passes. In the sort key, so
		 * an additive draw call is one material. */
		int material = 0;
		/* The glow part of a material 2 draw call in the additive pass - its
		 * entry in g_glow_parts - and -1 for every other draw call. In the
		 * sort key, because the glow shader's three registers are per part. */
		int glow = -1;
		/* Which half of the room this draw call is: 0 for the open half,
		 * and for triangles that stand inside another room one plus the
		 * piece of the intruding art they belong to - see HDIntrusion and
		 * HDPairRange in hd_render.h. In the sort key, because the pieces are
		 * drawn with different scissor states and a triangle cannot have half
		 * of one.
		 *
		 * The overhang has no draw calls of its own: it is drawn over the
		 * whole screen along with everything else. It is still counted per
		 * triangle (HDGroup::overhang) for the hollow-room test. */
		int intruding = 0;
		/* 0, or one plus the stand range these triangles belong to: art that
		 * stands on the faces, or in the box, of a room drawn classic
		 * (hd_cover.h, HDStandRange). In the
		 * sort key, because a frame draws a stand range with its group, in
		 * its holder's window or not at all, on an answer of its own. */
		int stand = 0;
		// The inside/overhang split, filled always and in the sort key only
		// while HDRenderSplitOverhang is on - see the note over it.
		int overhang = 0;
		/* 1 where the triangles of this range are drawn with their back
		 * faces culled - see RangeCulls below. In the sort key, because the
		 * cull mode is one render state per draw call. */
		int cull = 0;
		/* The lowest vertex number this range names and how many it reaches
		 * over. With a 32 bit index buffer they are only a measurement; with
		 * a 16 bit one they are the draw call - the indices
		 * are written relative to `baseVertex` and the vertex stream is moved
		 * to that vertex before the draw, which is how 16 bit indices address
		 * a level with more than 65536 vertices. */
		unsigned int baseVertex = 0;
		unsigned int vertexSpan = 0;
	};

	/* Where a group has any geometry at all.
	 *
	 * A sorted list of the 256-unit cells the group's own vertices fall in -
	 * a quarter of a floor square on a side, packed ten bits per axis with
	 * the room-local short shifted down by eight and biased by 128, so the
	 * whole int16 range fits and the key is one unsigned int.
	 *
	 * It answers whether the .TRG draws anything where a static mesh
	 * stands. A part's bounding box cannot
	 * answer it - one part of a Roman room spans the whole room - and walking
	 * the triangles per static would be a million tests per lantern. This is
	 * built once while the index buffer is filled, costs about 25 bytes per
	 * thousand vertices, and a query is a binary search per cell of the
	 * static's own box.
	 */
	/* One height at which a group draws flat art, and which sectors of that
	 * height it covers. Room local, classic units.
	 *
	 * Sectors and not one box: JOBY2's room 66 draws 24 triangles at the
	 * water's own height, and all of them are in two corners of the room -
	 * the coping, not the water. A single box round them spans the whole
	 * room and would answer "the remaster built this pool", which it did
	 * not. A classic water face is exactly one 1024-unit sector, so the
	 * sector is the unit the question is asked in. */
	struct HDFlatPlane
	{
		int y;
		std::vector<unsigned int> cells;	// sorted, unique - SectorKey
	};

	/* One 1024-unit floor square of a room-local position, as one key. The
	 * bias makes it non-negative for every short there is. */
	inline unsigned int SectorKey(int x, int z)
	{
		return (unsigned int)((((x >> 10) + 64) & 0x7F) << 7) |
			(unsigned int)(((z >> 10) + 64) & 0x7F);
	}

	struct HDGroup
	{
		std::vector<HDRange> pass[3];
		/* How many of the alpha pass's ranges are clear floors
		 * (HDRange::clear) - 0 for every group of a level without such a
		 * page, which is what lets the frame skip HD_PASS_CLEAR for it. */
		int clearRanges = 0;
		std::vector<HDRenderSample> samples;
		std::vector<unsigned int> cells;	// sorted, unique - see above
		/* The same list built from the inside triangles alone. The overlap
		 * list is asked of this one, because the overhang is drawn without a
		 * window whatever the answer. */
		std::vector<unsigned int> cellsIn;
		/* And split by how a frame draws the triangles, for the static
		 * meshes (HDRenderStaticArt): `cellsFree` holds the overhang - the
		 * triangles outside the group's own room box - of no intrusion piece
		 * and no stand range, drawn over the whole screen in every frame that
		 * lists the group, and `standCells` one list per stand range, in the
		 * order of `standRanges`. */
		std::vector<unsigned int> cellsFree;
		std::vector<std::vector<unsigned int> > standCells;
		unsigned int triangles[3] = { 0, 0, 0 };
		unsigned int overhang[3] = { 0, 0, 0 };		// of those, outside the box
		/* And of those, the ones standing inside another
		 * room - the only art in the game the scissor test is ever on for.
		 * A triangle can be overhang and intruding at once; the two counts
		 * answer different questions and neither is a subset of the other. */
		unsigned int intruding[3] = { 0, 0, 0 };
		// The entries of the intrusion list that name this group, worked out
		// once at the top of the group's own build.
		std::vector<int> intrusionAt;
		// And the classic faces of this group's room its intruding triangles
		// are held against, gathered the same way.
		std::vector<int> faceAt;
		/* The pieces the intruding triangles were split into - see
		 * HDPairRange in hd_render.h. A range's `intruding` is one plus its
		 * piece's index here, 0 for the open half. `pairWhole` is this
		 * frame's answer per piece, set by HDRenderPairWhole. */
		std::vector<HDPairRange> pairRanges;
		std::vector<unsigned char> pairWhole;
		/* The stand ranges - see HDStandRange in hd_render.h. A range's
		 * `stand` is one plus its index here. `standWith` is this frame's
		 * answer per range (HD_STAND_OUT, _WITH or _BEHIND), set by
		 * HDRenderStandWith; HD_STAND_WITH until a frame sets it. */
		std::vector<HDStandRange> standRanges;
		std::vector<unsigned char> standWith;
		/* The same for the cut boxes, and how many triangles
		 * this group lost to them. The count is not per pass - a cut
		 * triangle never reaches a pass's list, it is left out of the build
		 * altogether, so there is nothing per pass to hold it against. */
		std::vector<int> cutAt;
		unsigned int cutTris = 0;
		// The room's own box, room local, or all zeroes when none was handed
		// in - then nothing is overhang and the group draws as one piece.
		float roomMins[3] = { 0, 0, 0 };
		float roomMaxs[3] = { 0, 0, 0 };
		bool hasRoomBox = false;
		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };
		bool hasBounds = false;
		HDRenderGroupMotion motion = {};

		/* The shore of this room's water.
		 *
		 * The boxes the shimmer bits were flooded into - x and z, room local,
		 * with the height each belongs to. The vertex shader tapers the swell
		 * to nothing inside one square of the box's edge, so the surface meets
		 * the bank instead of climbing over it, and prepare() hands over the
		 * box of the plane that is the water surface - the one at the room's
		 * own surfaceY. waterPlanes 0 means this room has no water and the
		 * taper is switched off with one compare.
		 *
		 * Four is above the measured need: over the fifteen files no group
		 * carries more than two (room, height) planes of either bit. */
		enum { kMaxPlanes = 4 };
		float waterBox[kMaxPlanes][4];		// minX, minZ, maxX, maxZ
		float waterY[kMaxPlanes];
		int waterPlanes = 0;

		/* Where this group has horizontal art.
		 *
		 * One row per height at which the group draws a triangle whose three
		 * corners share their y, with the sectors they cover. It answers one
		 * question: does the remaster's art cover the plane the classic level
		 * draws its water surface in? Measured over the fifteen files, the
		 * answer is no in 61 of 93 reflecting rooms - the VCI pools have no
		 * surface in the .TRG at all - and a room whose water is missing shows
		 * the pool floor as dark washed-out blotches with nothing on it.
		 *
		 * A vector and not a map: the biggest room in the game has 46 such
		 * heights, the lookup runs once per water face at level build, and a
		 * sorted vector of 8 bytes is cheaper than a map in both. */
		std::vector<HDFlatPlane> flats;
		/* The same table out of the material 16 triangles alone - the
		 * remaster's own still water. Empty in nine of the fifteen levels,
		 * and in every group of the other six but the 28 that carry a
		 * sheet. */
		std::vector<HDFlatPlane> waterFlats;
	};

	// One 256-unit cell of a room-local position, as one key. The bias makes
	// the shifted value non-negative for every short there is.
	inline unsigned int CellKey(int x, int y, int z)
	{
		const unsigned int cx = (unsigned int)(((x >> 8) + 128) & 0x3FF);
		const unsigned int cy = (unsigned int)(((y >> 8) + 128) & 0x3FF);
		const unsigned int cz = (unsigned int)(((z >> 8) + 128) & 0x3FF);

		return (cx << 20) | (cy << 10) | cz;
	}

	/* One destructible piece of a room's art.
	 *
	 * Some parts carry a material with bit 15 set and a counter under it.
	 * Measured over all fifteen levels, there are 69 such parts in 6 levels
	 * and 32 rooms - ANDREA1 6, ANDREA2 5, JOBY2 3, RICH1 5, RICH2 14,
	 * RICH3 36 - and the encoding comes apart cleanly:
	 *
	 *     bit 15      set
	 *     bits 8..14  a counter, 0 to 5
	 *     bits 0..7   an ordinary material number - 0, 3 or 14, and nothing
	 *                 else in the whole game
	 *
	 * The counter is an instance index and not a damage stage, as the boxes
	 * show: JOBY2's room 57 carries 0x8000, 0x8100 and 0x8200 with 552
	 * triangles each, standing side by side at x 1024..2048, 2048..3072 and
	 * 3072..4096, same y, same z, 25 units thick. RICH2's room 151 carries
	 * three 2,226-triangle copies at three corners of the room, RICH3's room
	 * 204 four of them. One number per copy is what a renderer needs to make
	 * one of them disappear when Lara shoots the static it belongs to.
	 *
	 * So each (group, material) is one piece, its triangles are kept in
	 * ranges of their own - the sort key below carries the id, so they never
	 * merge with the room around them - and the piece can be switched off.
	 * hd_level.cpp pairs a piece with the classic MESH_INFO standing inside
	 * its box, and the engine's own `Flags & 1`, which control.cpp clears
	 * when Lara shoots a static of slot 50 to 57, decides per frame. */
	struct HDDestructible
	{
		int group = -1;
		unsigned int material = 0;
		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };
		bool hidden = false;
		bool hasBox = false;
	};

	std::vector<HDDestructible> g_destruct;

	/* What the classic path left on the device. Read back, not remembered
	 * from a table of what it "should" be: the fixed-function path changes
	 * its own states per bucket (polyinsert.cpp) and per level (function
	 * table), and a hard-coded restore would put back a state the caller
	 * never had. */
	struct SavedState
	{
		IDirect3DVertexShader9* vs = 0;
		IDirect3DPixelShader9* ps = 0;
		IDirect3DVertexDeclaration9* decl = 0;
		IDirect3DVertexBuffer9* stream = 0;
		IDirect3DIndexBuffer9* indices = 0;
		IDirect3DBaseTexture9* texture = 0;
		UINT streamOffset = 0;
		UINT streamStride = 0;
		DWORD fvf = 0;
		DWORD rs[32] = {};
		DWORD samp[16] = {};
		/* Samplers 1 and 2, the light field's (hd_room.hlsl, "The sheen"):
		 * their textures and their states, put back like sampler 0's. */
		IDirect3DBaseTexture9* fieldTexture[2] = { 0, 0 };
		DWORD fieldSamp[2][16] = {};
		/* The scissor rectangle. D3DRS_SCISSORTESTENABLE is in the render
		 * state list below; the rectangle beside it is a device state of its
		 * own with its own Get/Set pair, and the HD path writes it. A device
		 * with no scissor cap answers the Get
		 * with a failure - then nothing is put back, because nothing was
		 * taken and nothing was set. */
		RECT scissor = { 0, 0, 0, 0 };
		bool scissorValid = false;
	};

	const D3DRENDERSTATETYPE kSavedRenderStates[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
		D3DRS_ALPHATESTENABLE, D3DRS_ALPHAFUNC, D3DRS_ALPHAREF,
		D3DRS_CULLMODE, D3DRS_CLIPPING, D3DRS_SPECULARENABLE, D3DRS_FOGENABLE,
		D3DRS_LIGHTING, D3DRS_COLORVERTEX, D3DRS_SHADEMODE, D3DRS_FILLMODE,
		D3DRS_DITHERENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_SRGBWRITEENABLE, D3DRS_MULTISAMPLEANTIALIAS
	};

	const D3DSAMPLERSTATETYPE kSavedSamplerStates[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW,
		D3DSAMP_MAXANISOTROPY, D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL,
		D3DSAMP_SRGBTEXTURE
	};

	const int kRenderStateCount = (int)(sizeof(kSavedRenderStates) / sizeof(kSavedRenderStates[0]));
	const int kSamplerStateCount = (int)(sizeof(kSavedSamplerStates) / sizeof(kSavedSamplerStates[0]));

	IDirect3DDevice9* g_device = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	// The glow shader, material 2's additive draw calls (hd_glow.hlsl).
	IDirect3DPixelShader9* g_glow_ps = 0;
	/* The room shader's gloss variant (hd_room.hlsl compiled with HD_GLOSS,
	 * see "The sheen" there): the draw calls whose surface shines while
	 * hd_reflect is on. Null when the device refused it - every draw call
	 * then keeps the plain variant, as with hd_reflect off. */
	IDirect3DPixelShader9* g_gloss_ps = 0;
	// The water sheet's own pair (hd_room.hlsl, "the sheet"). Null when the
	// device refused either: then no sheet is built.
	IDirect3DVertexShader9* g_water_vs = 0;
	IDirect3DPixelShader9* g_water_ps = 0;
	/* The projected shadow's receive shader (hd_shadow_receive.hlsl). Null
	 * when the device refused it; the projected shadow then does not run and
	 * the blob stays. */
	IDirect3DPixelShader9* g_shadow_receive_ps = 0;
	/* Its two-map variant (the same file's Receive2PS), for a frame whose
	 * second map was drawn. Null when the device refused it; the creatures
	 * then keep their blobs (the frame's decision asks
	 * HDRenderShadowReceive2Ready beside the second map, hd_laradraw.cpp). */
	IDirect3DPixelShader9* g_shadow_receive2_ps = 0;
	/* Its mirror's variant (the same file's MirrorReceivePS), for the copy
	 * behind a mirror's glass. Null when the device refused it; the copy then
	 * takes the ordinary receive, as the room in front of it does. */
	IDirect3DPixelShader9* g_shadow_receive_mirror_ps = 0;
	/* Whether the rooms' receive cleared the stencil inside the open
	 * bracket: the models' receive then finds its own bit 0 everywhere and
	 * clears nothing. Set by the rooms' Begin, cleared by HDRenderFrameBegin. */
	bool g_receive_cleared = false;
	IDirect3DVertexDeclaration9* g_decl = 0;
	IDirect3DVertexBuffer9* g_vb = 0;
	IDirect3DIndexBuffer9* g_ib = 0;

	std::vector<HDGroup> g_groups;
	int g_room_groups = 0;
	int g_vertex_count = 0;
	bool g_index32 = false;
	D3DCAPS9 g_caps;

	/* The two caps the diagnostic tool is allowed to bend.
	 * Zero means "leave the device's own answer alone", which is what the
	 * engine always uses. */
	int g_cap_max_side = 0;
	int g_cap_no_index32 = 0;

	SavedState g_saved;
	bool g_in_frame = false;
	/* The scissor rectangle the HD path has set inside the bracket
	 * it is in, and whether the test is on at all. Kept so that a room whose
	 * window has not moved costs no device call - a frame draws its rooms
	 * twice, once for the solid passes and once for the glows, and a level
	 * with a hundred rooms would otherwise make four hundred of them. */
	bool g_scissor_on = false;
	RECT g_scissor = { 0, 0, 0, 0 };
	int g_draw_calls = 0;
	// Draw calls of the opaque pass that ran with the alpha test on
	// because their part is a cut-out material. Reset with g_draw_calls.
	int g_cutout_calls = 0;
	// Once per level: the opaque parts of materials 6, 7 and 8.
	int g_cutout_parts = 0;
	/* Once per level: the additive parts whose .TRG material gave them
	 * a weight below 1, the smallest of those weights, and how many of them
	 * also carry a swing time. */
	int g_addweight_parts = 0;
	int g_addswing_parts = 0;
	float g_addweight_min = 1.0f;
	/* Once per level: the material 2 parts whose swing is under half a
	 * second and which therefore draw still (MaterialAddPeriod), and the
	 * stacked seas drawn once - the parts and the copies left out
	 * (MarkStackedCopies). */
	int g_addstill_parts = 0;
	int g_stacked_parts = 0;
	int g_stacked_tris = 0;
	/* HDRenderAdditiveOnly: the one material whose additive draw calls are
	 * drawn, or -1 for all of them, which is what the engine always uses. */
	int g_add_only = -1;
	/* HDRenderGlowOverride: material 2 summed ONE / ONE like every other
	 * additive material instead of composed as screen, and sampled without
	 * the anisotropic filter. Both false in the engine. */
	bool g_glow_sum = false;
	bool g_glow_no_aniso = false;
	// Once per level: the sky parts that draw at light 1.
	int g_flat_parts = 0;
	/* Once per level: the material 13 parts whose four pages and frame
	 * time all resolved and are therefore drawn as a flipbook, the ones that
	 * did not and keep their own page, and the draw calls the resolved ones
	 * cost. `g_flip_calls` is per frame and resets with g_draw_calls. */
	// Once per level: the opaque parts of materials 3 and 17 whose
	// own numbers gave them a gloss.
	int g_gloss_parts = 0;
	/* Once per level: the distinct horizontal planes the art covers, over
	 * every group - what the classic water surface is held against. */
	int g_flat_planes = 0;
	/* Once per level: the see-through parts of the alpha pass and the
	 * draw calls they cost - material 14, the glass. */
	int g_blend_parts = 0;
	int g_blend_ranges = 0;
	/* Once per level: the clear floors - the alpha-pass parts with a
	 * triangle on a page HDPageCacheSlotClear answers true for, the distinct
	 * such pages and the draw calls they cost. `g_clear_seen` is one byte
	 * per page slot, so a page is counted once however many parts use it. */
	int g_clear_parts = 0;
	int g_clear_pages = 0;
	int g_clear_ranges = 0;
	std::vector<unsigned char> g_clear_seen;
	/* Once per level: the still water surfaces - material 16. The parts
	 * (counted per pass block, which is how TrgProbe -matparts counts them),
	 * how the file splits them between the passes, their triangles, how many
	 * of them live in a sub-rectangle of their page, the smallest such span
	 * in 255ths and the largest UV travel any of them was given. Per frame,
	 * with g_draw_calls: the draw calls that carried a ripple. */
	int g_water_parts = 0;
	int g_water_opaque = 0;
	int g_water_additive = 0;
	int g_water_tris = 0;
	int g_water_subrect = 0;
	int g_water_still = 0;		// of those, parts with no ripple at all
	int g_water_span = 255;		// the tightest UV span, in 255ths
	float g_water_cap = 0.0f;
	int g_water_calls = 0;
	int g_flip_parts = 0;
	int g_flip_dropped = 0;
	int g_flip_ranges = 0;
	int g_flip_calls = 0;
	/* Once per level: what the build decided for every material 15 part and
	 * pass (HDRenderBookPartInfo), and the draw calls the turning ones cost.
	 * Per frame, with g_draw_calls: the draw calls that turned a book, and
	 * the clock they turned on - the drawn frame's time, not the tick clock
	 * (hd_book.h, HDBookClock). */
	std::vector<HDRenderBookPart> g_book_parts;
	int g_book_ranges = 0;
	int g_book_calls = 0;
	double g_book_clock = 0.0;
	/* HDRenderBookPageOverride: the page every book shows, or -1 for the
	 * clock's, which is what the engine always uses. */
	int g_book_page = -1;
	/* The book the log follows (HDRenderBookWatch): the first book draw call
	 * drawn in the level, its group, and the page it showed last. The ranges
	 * are not moved after the build, so the address names the draw call. */
	const HDRange* g_book_watch = 0;
	int g_book_watch_group = -1;
	int g_book_watch_part = -1;
	int g_book_watch_page = 0;
	// Draw calls that carried a material's gloss, draw calls drawn on the
	// gloss variant, and the hd_reflect switch as a float, set once per
	// frame beside the motion switch.
	int g_gloss_calls = 0;
	int g_sheen_calls = 0;
	// Draw calls that drew a clear floor (HD_PASS_CLEAR).
	int g_clear_calls = 0;
	float g_reflect = 1.0f;

	/* --- the classic per-vertex light terms ---------------------------- */

	// WATERTAB (global\types.h). Two signed bytes and two unsigned ones,
	// and the mixture is not an accident: init_water_table writes -3 into the
	// unsigned `abs` (gamemain.cpp), aRoomletTransformLight reads it back
	// as 253, and the shimmer is 253 brighter for that table row. Copied, not
	// corrected.
	struct HDWaterTab
	{
		signed char shimmer;
		signed char choppy;
		unsigned char random;
		unsigned char abs;
	};

	const HDWaterMark* g_marks = 0;
	int g_mark_count = 0;
	/* The rooms' own boxes for the level being built, and the slack a
	 * triangle is allowed before it counts as overhang.
	 *
	 * 128 units, measured. A wall face sits exactly on the box's edge, so
	 * with no slack at all the remaster's own relief - a moulding, a cornice,
	 * the depth of a brick - would make a wall overhang: that reading calls
	 * 73 % of ANDREA2 overhang and the window stops meaning anything. Over
	 * the fifteen files the distances come in two clumps, one below 64 units
	 * (a million triangles of ANDREA2 alone: the relief) and one past a whole
	 * sector (the facades and the piers). 128 is between them and takes
	 * ANDREA2 from 77.6 % to 4.5 %. */
	const HDRoomBox* g_boxes = 0;
	int g_box_count = 0;
	#define HD_OVERHANG_SLACK	128.0f

	/* The classic floor squares, borrowed for the length of the build like
	 * the boxes above (HDRenderSetSectors). */
	const HDRoomGrid* g_grids = 0;
	int g_grid_count = 0;
	const HDSectorFloor* g_sectors = 0;
	int g_sector_count = 0;

	/* What the sheen is lit with: the level's light field
	 * (hd_gloss_field.h), built with the level out of every room's .TRG
	 * point lights in world units, in two volume textures the gloss variant
	 * reads on samplers 1 and 2. Null where the level has none - no lamp,
	 * no rooms' boxes, or a device without filtered volume textures - and the
	 * gloss variant then draws its environment term alone.
	 *
	 * `g_group_origin` is each room group's origin, x and z (r->x, r->z; a
	 * TR5 room's y origin is 0), kept from the build's floor grids: a room's
	 * own coordinates plus it are the world coordinates the field is in,
	 * whatever matrix the caller draws the room with (prepare(), the gloss
	 * rows). */
	IDirect3DVolumeTexture9* g_field_dir = 0;
	IDirect3DVolumeTexture9* g_field_rgb = 0;
	HDGlossFieldGrid g_field_grid;
	HDGlossFieldStats g_field_stats;
	float g_field_ms = 0.0f;
	int g_field_bytes = 0;
	std::vector<float> g_group_origin;
	// HDRenderGlossFieldView: RoomProbe's -glossfield picture. 0 in the engine.
	int g_field_view = 0;
	/* HDRenderGlossFieldSight: RoomProbe's -fieldsight, the field built with
	 * the sight test by the rooms (HDGlossFieldSight) for a comparison.
	 * Empty in the engine. */
	std::vector<unsigned char> g_field_near;
	int g_field_near_rooms = 0;
	/* Once per level: the lamps that went into the field, and the light
	 * cubes the .TRG holds for the floor squares (0 when their count is not
	 * the squares'), for the GLOSSLOG level line. */
	int g_lamp_count = 0;
	int g_cube_count = 0;

	/* The rooms that may be holding another room's art,
	 * and how many triangles each of those boxes really caught. The list is
	 * borrowed for the length of the build like the boxes above; the hit
	 * counts outlive it, because the level's own log line is written after
	 * the build and has to say what the rule found. */
	const HDIntrusion* g_intrusions = 0;
	int g_intrusion_count = 0;
	/* The classic faces those triangles are held against, borrowed on the
	 * same terms, and each face's box grown by HD_PAIR_FACE_REACH - a
	 * centroid outside it cannot belong to the face, which spares the
	 * distance for all but a few faces per triangle. */
	const HDPairFace* g_pair_faces = 0;
	int g_pair_face_count = 0;
	std::vector<float> g_pair_face_box;
	// The diagnostic inside/overhang split - see HDRenderSplitOverhang.
	int g_split_overhang = 0;
	/* Whether the ranges RangeCulls picks are drawn culled. 1 in the engine
	 * always; RoomProbe's -cull none sets 0 for the two-sided picture out
	 * of the same build - see HDRenderCullOpaque. */
	int g_cull_opaque = 1;
	std::vector<int> g_intrusion_hits;
	/* Per entry of the intrusion list, the triangles it holds that no face
	 * claims and that stand outside the box one sector smaller - the ones
	 * the index fill hands to the open half, never asked per frame. Counted
	 * on the same terms as g_intrusion_hits, for the level's log line. */
	std::vector<int> g_intrusion_band;

	/* The classic faces another group's art may stand on (HDStandRoom,
	 * HDStandHolder, HDCoverFace in hd_render.h), borrowed for the length of
	 * the build like the lists above, and what the build made of them: the
	 * tagged triangles with their holders, kept for hd_level.cpp's coverage
	 * test, the holders it kept, and how many tagged triangles an intrusion
	 * entry had asked about too. `g_stand_pick` is the one range
	 * HD_WINDOW_STAND draws, -1 outside HDRenderRoomStand, and
	 * `g_stand_pick_behind` whether it is drawn pushed back.
	 * `g_stand_covers` is HDStandCover per (group, holder) with the offset of
	 * its face flags in `g_stand_cover_faces`. */
	const HDStandRoom* g_stand_rooms = 0;
	int g_stand_room_count = 0;
	const HDStandHolder* g_stand_holders = 0;
	int g_stand_holder_count = 0;
	const HDCoverFace* g_stand_faces = 0;
	int g_stand_face_count = 0;
	std::vector<HDCoverTri> g_stand_tris;
	std::vector<int> g_stand_tri_holder;
	int g_stand_kept = 0;
	int g_stand_from_intrusions = 0;
	int g_stand_pick = -1;
	bool g_stand_pick_behind = false;

	/* The static meshes' places (HDRenderSetStaticPlaces), borrowed until
	 * HDRenderStaticPiecesUpload, and what the build kept of the art around
	 * them for the pieces (HDRenderStaticArt): `g_place_sectors` the places by
	 * the 1024-unit floor squares their grown boxes reach, sorted by the key;
	 * `g_place_tris` every triangle of a group's overhang of no intrusion
	 * piece, or of a stand range, whose centroid lies within
	 * HD_STATIC_PIECE_REACH of the box of a static of another room - its
	 * place, group, pass, range, stand range, where its corners stand in the
	 * sorted index stream and, after the rebase, their indices, and the
	 * corners themselves, group local. The pieces made of them are drawn on
	 * their own: `g_static_pieces` holds per piece its ranges as a group holds
	 * its own, out of `g_piece_ib`, `g_static_piece_group` the group whose
	 * vertices, matrix and lighting they are drawn with. */
	struct PlaceTri
	{
		int place;
		int group;
		int pass;
		int range;
		int stand;
		unsigned int at;
		unsigned int index[3];
		float corner[9];
	};

	const HDStaticPlace* g_places = 0;
	int g_place_count = 0;
	std::vector<std::pair<unsigned int, int> > g_place_sectors;
	std::vector<PlaceTri> g_place_tris;
	std::vector<HDGroup> g_static_pieces;
	std::vector<int> g_static_piece_group;
	std::vector<unsigned int> g_piece_indices;
	IDirect3DIndexBuffer9* g_piece_ib = 0;

	struct StandCoverBuilt
	{
		HDStandCover cover;
		size_t firstFlag;
	};

	std::vector<StandCoverBuilt> g_stand_covers;
	std::vector<unsigned char> g_stand_cover_faces;

	/* The openings the randomizer cut, and how many triangles
	 * each of those boxes took away. Borrowed and counted on exactly the same
	 * terms as the intrusions above. */
	const HDCut* g_cuts = 0;
	int g_cut_count = 0;
	std::vector<int> g_cut_hits;

	/* The shape of every glow, for the glow pixel shader (hd_glow.hlsl).
	 *
	 * One entry per material 2 part with triangles in the additive pass,
	 * taken once at level build out of the part's own vertices:
	 *
	 *   the card sides  the u range the part's triangles use. A part that uses
	 *                   less than 90 per cent of its page's u cuts its cards
	 *                   out of the middle of the page, where the page is still
	 *                   bright, and its outer 15 per cent on either side fades.
	 *                   The parts that do: ANDREA1 room 157, ANDREA2 rooms 93,
	 *                   131 and 217, ANDREA3's page 3037, ANDY2 room 34.
	 *   the normal      the file normal, unless it disagrees with the part's
	 *                   faces - |N.f| under 0.9 on more than half of the
	 *                   part's corners - and then the face normal: 31 of the
	 *                   253 parts of the fifteen levels, JOBY2's 21 lamp cones,
	 *                   ANDREA3's seven window slabs of page 3232, ANDY2 room
	 *                   34, ANDY3 room 150 and TITLE room 43.
	 *   the ends        the page-v axis, the mean of the direction v runs in
	 *                   over the part's triangles, and the extent of the ASSET
	 *                   along it. An asset is every part of one material tuple
	 *                   and one page whose world boxes overlap or come within
	 *                   a sector of each other - ANDREA2's window shaft is the
	 *                   three parts of rooms 93, 131 and 217 - because an end
	 *                   fade per part would fade each room's piece of a shaft
	 *                   at the room's border and cut seams across it. The fade
	 *                   is 512 units long, and never more than a quarter of the
	 *                   asset's extent, so ANDREA1's window slabs of page 3232,
	 *                   810 units along their axis, keep their middle. Only an
	 *                   asset with a part cut out of the page in v fades at its
	 *                   ends (see below): 94 of the 253 parts, in 78 assets.
	 *
	 * The rooms' origins come with the room boxes (HDRoomBox). Without them
	 * no two groups can be put side by side, and an asset is then the parts of
	 * one group alone. */
	struct HDGlowPart
	{
		int group = -1;
		int part = -1;
		int page = -1;
		int u0 = 255, u1 = 0;
		int v0 = 255, v1 = 0;
		bool edge = false;
		bool face = false;
		int corners = 0, disagree = 0;
		int asset = -1;
		float axis[3] = { 0.0f, 0.0f, 0.0f };
		float lo = 0.0f, hi = 0.0f;	// the asset's ends, world units
		float shift = 0.0f;		// dot(the group's origin, axis)
		float fade = 0.0f;
		float gradient[3] = { 0.0f, 0.0f, 0.0f };
		float mins[3] = { 0.0f, 0.0f, 0.0f };
		float maxs[3] = { 0.0f, 0.0f, 0.0f };
		float origin[3] = { 0.0f, 0.0f, 0.0f };
	};

	std::vector<HDGlowPart> g_glow_parts;
	int g_glow_assets = 0;
	/* HDRenderGlowOverride: material 2 drawn with the room pixel shader, its
	 * draw calls not split by part. False in the engine. */
	bool g_glow_no_shader = false;

	const float kGlowEdgeBelow = 0.9f;
	const float kGlowEdgeShare = 0.15f;
	const float kGlowAgree = 0.9f;
	const float kGlowEndFade = 512.0f;
	const float kGlowEndShare = 0.25f;
	const float kGlowTouch = 1024.0f;

	/* The corners of one additive triangle of a part, or false where the
	 * file's indices run off its own arrays. */
	bool glow_triangle(const trf::remaster::Trg& trg, unsigned int first,
		const trf::remaster::TrgVertex** out)
	{
		if (first + 2 >= trg.indices.size())
			return false;

		for (int k = 0; k < 3; k++)
		{
			const unsigned int index = trg.indices[first + (unsigned int)k];

			if (index >= trg.vertices.size())
				return false;

			out[k] = &trg.vertices[index];
		}

		return true;
	}

	/* Fills g_glow_parts and, per group and part, the entry's number or -1:
	 * the build loop hands that number to the part's additive triangles,
	 * where it keeps the parts in draw calls of their own. */
	void BuildGlowParts(const trf::remaster::Trg& trg,
		std::vector<std::vector<int> >& glowOf)
	{
		g_glow_parts.clear();
		g_glow_assets = 0;
		glowOf.assign(trg.groups.size(), std::vector<int>());

		if (g_glow_no_shader)
			return;

		for (size_t g = 0; g < trg.groups.size(); g++)
		{
			const trf::remaster::TrgGroup& source = trg.groups[g];

			glowOf[g].assign(source.parts.size(), -1);

			for (size_t p = 0; p < source.parts.size(); p++)
			{
				const trf::remaster::TrgPart& part = source.parts[p];
				const trf::remaster::TrgPart::Pass& run = part.pass[2];

				if (MaterialBase(part) != HD_TRG_MATERIAL_GLOW || run.count < 3)
					continue;

				HDGlowPart glow;
				bool any = false;
				float ref[3] = { 0.0f, 0.0f, 0.0f };
				bool has_ref = false;

				glow.group = (int)g;
				glow.part = (int)p;

				if (g_boxes && (int)g < g_box_count)
				{
					glow.origin[0] = g_boxes[g].originX;
					glow.origin[2] = g_boxes[g].originZ;
				}

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const trf::remaster::TrgVertex* v[3];

					if (!glow_triangle(trg, run.start + at, v))
						break;

					if (glow.page < 0 && v[0]->layer &&
						(size_t)v[0]->layer <= trg.textures.size())
						glow.page = (int)trg.textures[v[0]->layer - 1];

					float P[3][3];

					for (int k = 0; k < 3; k++)
					{
						P[k][0] = (float)v[k]->x + glow.origin[0];
						P[k][1] = (float)v[k]->y;
						P[k][2] = (float)v[k]->z + glow.origin[2];

						if ((int)v[k]->u < glow.u0) glow.u0 = (int)v[k]->u;
						if ((int)v[k]->u > glow.u1) glow.u1 = (int)v[k]->u;
						if ((int)v[k]->v < glow.v0) glow.v0 = (int)v[k]->v;
						if ((int)v[k]->v > glow.v1) glow.v1 = (int)v[k]->v;

						for (int axis = 0; axis < 3; axis++)
						{
							if (!any || P[k][axis] < glow.mins[axis]) glow.mins[axis] = P[k][axis];
							if (!any || P[k][axis] > glow.maxs[axis]) glow.maxs[axis] = P[k][axis];
						}

						any = true;
					}

					const float e1[3] = { P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2] };
					const float e2[3] = { P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2] };
					const float fn[3] =
					{
						e1[1] * e2[2] - e1[2] * e2[1],
						e1[2] * e2[0] - e1[0] * e2[2],
						e1[0] * e2[1] - e1[1] * e2[0]
					};
					const float fl = sqrtf(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);

					/* Does the file's normal agree with the face? A corner
					 * with no normal at all agrees with nothing. */
					if (fl > 0.0f)
						for (int k = 0; k < 3; k++)
						{
							const float n[3] = { v[k]->normal_x(), v[k]->normal_y(), v[k]->normal_z() };
							const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
							const float agree = nl > 0.0f
								? fabsf(n[0] * fn[0] + n[1] * fn[1] + n[2] * fn[2]) / (nl * fl)
								: 0.0f;

							glow.corners++;

							if (agree < kGlowAgree)
								glow.disagree++;
						}

					/* The direction v runs in on this triangle: dP/dv out of
					 * the two edges and their UV differences, signed the same
					 * way as the part's first one. */
					const float du1 = (float)v[1]->u - (float)v[0]->u;
					const float du2 = (float)v[2]->u - (float)v[0]->u;
					const float dv1 = (float)v[1]->v - (float)v[0]->v;
					const float dv2 = (float)v[2]->v - (float)v[0]->v;
					const float det = du1 * dv2 - du2 * dv1;

					if (fabsf(det) < 1e-6f)
						continue;

					float d[3];

					for (int axis = 0; axis < 3; axis++)
						d[axis] = (e2[axis] * du1 - e1[axis] * du2) / det;

					const float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

					if (dl <= 0.0f)
						continue;

					if (!has_ref)
					{
						has_ref = true;
						ref[0] = d[0]; ref[1] = d[1]; ref[2] = d[2];
					}

					const float sign = d[0] * ref[0] + d[1] * ref[1] + d[2] * ref[2] < 0.0f
						? -1.0f : 1.0f;

					for (int axis = 0; axis < 3; axis++)
						glow.gradient[axis] += sign * d[axis] / dl;
				}

				if (!any)
					continue;

				glow.edge = (float)(glow.u1 - glow.u0) < kGlowEdgeBelow * 255.0f;
				glow.face = glow.disagree * 2 > glow.corners;

				glowOf[g][p] = (int)g_glow_parts.size();
				g_glow_parts.push_back(glow);
			}
		}

		/* The assets: parts of one tuple and one page whose world boxes come
		 * within a sector of each other, joined transitively. */
		const size_t count = g_glow_parts.size();
		std::vector<size_t> root(count);

		for (size_t i = 0; i < count; i++)
			root[i] = i;

		struct Find
		{
			static size_t of(std::vector<size_t>& r, size_t i)
			{
				while (r[i] != i)
				{
					r[i] = r[r[i]];
					i = r[i];
				}

				return i;
			}
		};

		for (size_t i = 0; i < count; i++)
			for (size_t j = i + 1; j < count; j++)
			{
				const HDGlowPart& a = g_glow_parts[i];
				const HDGlowPart& b = g_glow_parts[j];
				const trf::remaster::TrgPart& pa = trg.groups[(size_t)a.group].parts[(size_t)a.part];
				const trf::remaster::TrgPart& pb = trg.groups[(size_t)b.group].parts[(size_t)b.part];

				if (pa.material != pb.material || a.page != b.page ||
					pa.param[0] != pb.param[0] || pa.param[1] != pb.param[1] ||
					pa.param[2] != pb.param[2] || pa.param[3] != pb.param[3])
					continue;

				if (a.group != b.group && !(g_boxes && a.group < g_box_count &&
					b.group < g_box_count))
					continue;

				bool touch = true;

				for (int axis = 0; axis < 3; axis++)
					if (a.mins[axis] > b.maxs[axis] + kGlowTouch ||
						b.mins[axis] > a.maxs[axis] + kGlowTouch)
						touch = false;

				if (touch)
					root[Find::of(root, i)] = Find::of(root, j);
			}

		/* Per asset: the axis out of its parts' directions, each part's turned
		 * to agree with the first, and the extent along it out of every
		 * vertex of every part. */
		for (size_t i = 0; i < count; i++)
		{
			if (Find::of(root, i) != i)
				continue;

			const int asset = g_glow_assets++;
			float axis[3] = { 0.0f, 0.0f, 0.0f };
			const float* first = 0;
			bool cut = false;

			for (size_t j = 0; j < count; j++)
			{
				if (Find::of(root, j) != i)
					continue;

				const float* d = g_glow_parts[j].gradient;

				if ((float)(g_glow_parts[j].v1 - g_glow_parts[j].v0) <
					kGlowEdgeBelow * 255.0f)
					cut = true;

				if (!first)
					first = d;

				const float sign = d[0] * first[0] + d[1] * first[1] + d[2] * first[2] < 0.0f
					? -1.0f : 1.0f;

				for (int k = 0; k < 3; k++)
					axis[k] += sign * d[k];
			}

			const float al = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
			bool seen = false;
			float lo = 0.0f, hi = 0.0f;

			/* Only an asset whose cards are cut out of the page in v has ends
			 * to fade - a part of it uses less than 90 per cent of the page's
			 * v. Where every part uses the whole page the ends are the page's
			 * own, which fade by themselves: JOBY2's lamp cones, whose page
			 * 3070 runs from the lamp to nothing, lose the light at the lamp
			 * under an end fade (RoomProbe, JOBY2 room 17 from 1229 -2637
			 * 2048: no pixel of the glow stays above 8 of 255, where 42,061
			 * pixels averaged 13.1). */
			if (al > 0.0f && cut)
			{
				for (int k = 0; k < 3; k++)
					axis[k] /= al;

				for (size_t j = 0; j < count; j++)
				{
					if (Find::of(root, j) != i)
						continue;

					const HDGlowPart& glow = g_glow_parts[j];
					const trf::remaster::TrgPart::Pass& run =
						trg.groups[(size_t)glow.group].parts[(size_t)glow.part].pass[2];

					for (unsigned int at = 0; at + 2 < run.count; at += 3)
					{
						const trf::remaster::TrgVertex* v[3];

						if (!glow_triangle(trg, run.start + at, v))
							break;

						for (int k = 0; k < 3; k++)
						{
							const float t =
								((float)v[k]->x + glow.origin[0]) * axis[0] +
								(float)v[k]->y * axis[1] +
								((float)v[k]->z + glow.origin[2]) * axis[2];

							if (!seen || t < lo) lo = t;
							if (!seen || t > hi) hi = t;
							seen = true;
						}
					}
				}
			}

			for (size_t j = 0; j < count; j++)
			{
				if (Find::of(root, j) != i)
					continue;

				HDGlowPart& glow = g_glow_parts[j];

				glow.asset = asset;

				if (!seen)
					continue;

				glow.axis[0] = axis[0];
				glow.axis[1] = axis[1];
				glow.axis[2] = axis[2];
				glow.lo = lo;
				glow.hi = hi;
				glow.shift = glow.origin[0] * axis[0] + glow.origin[2] * axis[2];
				glow.fade = (hi - lo) * kGlowEndShare < kGlowEndFade
					? (hi - lo) * kGlowEndShare : kGlowEndFade;
			}
		}
	}

	/* The glow shader's three registers for one part (hd_glow.hlsl, c77 to
	 * c79). A fade that does not apply gets a range the pixel never leaves. */
	void GlowConstants(const HDGlowPart& glow, float* out)
	{
		const bool ends = glow.fade > 0.0f;
		const bool edge = glow.edge && glow.u1 > glow.u0;
		const float width = kGlowEdgeShare * (float)(glow.u1 - glow.u0) / 255.0f;

		out[0] = ends ? glow.axis[0] : 0.0f;
		out[1] = ends ? glow.axis[1] : 0.0f;
		out[2] = ends ? glow.axis[2] : 0.0f;
		out[3] = ends ? 1.0f / glow.fade : 1.0f;
		out[4] = ends ? glow.lo - glow.shift : -1.0f;
		out[5] = ends ? glow.hi - glow.shift : 1.0f;
		out[6] = edge ? (float)glow.u0 / 255.0f : -1.0f;
		out[7] = edge ? (float)glow.u1 / 255.0f : 2.0f;
		out[8] = edge ? 1.0f / width : 1.0f;
		out[9] = glow.face ? 1.0f : 0.0f;
		out[10] = 0.0f;
		out[11] = 0.0f;
	}

	/* Whether a triangle really meets an axis-aligned box.
	 *
	 * The separating axis test, thirteen axes: the box's three face normals,
	 * the triangle's own plane, and the nine cross products of the box's
	 * three axes with the triangle's three edges. If any one of them
	 * separates the two, they do not touch.
	 *
	 * The two cheaper tests each get the doorway wrong:
	 *
	 *   A vertex inside the box would leave the doorway shut. One big wall
	 *   triangle can cross the opening with all three of its corners outside
	 *   the box, and that is exactly the triangle that has to go.
	 *
	 *   The triangle's own box overlapping would eat more than the opening.
	 *   A slanted triangle passing a doorway by has a bounding box that
	 *   reaches into it, and a wall would lose art that was never in the way.
	 *
	 * The exact test costs a few dozen multiplies per triangle and runs once
	 * per level build, on the triangles of a level that has any cut at all -
	 * which is the two or three openings of one secret list. */
	bool tri_meets_box(const float* a, const float* b, const float* c,
		const float* mins, const float* maxs)
	{
		float centre[3], half[3];
		float v[3][3];

		for (int i = 0; i < 3; i++)
		{
			centre[i] = (mins[i] + maxs[i]) * 0.5f;
			half[i] = (maxs[i] - mins[i]) * 0.5f;
			v[0][i] = a[i] - centre[i];
			v[1][i] = b[i] - centre[i];
			v[2][i] = c[i] - centre[i];
		}

		// 1. The box's own three axes: the triangle's box against it.
		for (int i = 0; i < 3; i++)
		{
			float lo = v[0][i], hi = v[0][i];

			for (int k = 1; k < 3; k++)
			{
				if (v[k][i] < lo) lo = v[k][i];
				if (v[k][i] > hi) hi = v[k][i];
			}

			if (lo > half[i] || hi < -half[i])
				return false;
		}

		const float e[3][3] =
		{
			{ v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2] },
			{ v[2][0] - v[1][0], v[2][1] - v[1][1], v[2][2] - v[1][2] },
			{ v[0][0] - v[2][0], v[0][1] - v[2][1], v[0][2] - v[2][2] }
		};

		// 2. The triangle's plane against the box.
		{
			const float n[3] =
			{
				e[0][1] * e[1][2] - e[0][2] * e[1][1],
				e[0][2] * e[1][0] - e[0][0] * e[1][2],
				e[0][0] * e[1][1] - e[0][1] * e[1][0]
			};
			const float d = n[0] * v[0][0] + n[1] * v[0][1] + n[2] * v[0][2];
			const float r = fabsf(n[0]) * half[0] + fabsf(n[1]) * half[1] +
				fabsf(n[2]) * half[2];

			if (d > r || d < -r)
				return false;
		}

		/* 3. The nine edge-cross axes. `u` is the box axis, `w` the other
		 * two components of it, and the cross product of a unit axis with an
		 * edge is written out rather than computed - which is where the
		 * signs come from. */
		for (int k = 0; k < 3; k++)
			for (int u = 0; u < 3; u++)
			{
				const int w1 = (u + 1) % 3;
				const int w2 = (u + 2) % 3;
				/* axis = box_u x e[k] */
				const float ax1 = -e[k][w2];
				const float ax2 = e[k][w1];
				float lo = 0.0f, hi = 0.0f;

				for (int t = 0; t < 3; t++)
				{
					const float p = ax1 * v[t][w1] + ax2 * v[t][w2];

					if (!t || p < lo) lo = p;
					if (!t || p > hi) hi = p;
				}

				const float r = fabsf(ax1) * half[w1] + fabsf(ax2) * half[w2];

				if (lo > r || hi < -r)
					return false;
			}

		return true;
	}

	const void* g_water_table_up = 0;	// what is on the card right now
	int g_frame_bulbs = 0;
	bool g_frame_has_light = false;

	/* The register the three motions ride on, kept here because its first
	 * two components change per draw call and the other two per frame -
	 * so the frame writes into this array and every range rewrites register
	 * 13 out of it. `g_wrapping` remembers whether the sampler is currently in
	 * WRAP, so a level with no scroll in it never touches the state at all. */
	float g_motion[4] = { 0, 0, 0, 0 };
	bool g_wrapping = false;
	int g_scroll_parts = 0;

	/* The diagnostic tool's -noshore: hand the shader a shore box no vertex
	 * can be near the edge of, so the swell runs to the water's edge - the
	 * comparison picture out of the same build. Never set by the engine. */
	bool g_no_shore = false;

	/* Set at the top of every pass, so the first draw call of a room always
	 * writes register 13 even when its offset happens to equal the last
	 * room's: the model shader's gJoints occupy c8 to c103
	 * (hd_model.hlsl), so an object drawn between two rooms overwrites this
	 * register, and a skipped upload would then hand the room shader a joint
	 * matrix where it expects a clock. Within one room the compare still
	 * saves the upload, which is where the thousand draw calls are. */
	bool g_motion_dirty = true;

	const float kIdentity[16] =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	// Identity to start with, so that a FrameBegin(0, 0) - which only the
	// tests do - draws in raw clip space instead of through whatever was in
	// this memory.
	float g_view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	float g_proj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	float g_viewproj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

	std::string g_error;

	double now_ms()
	{
		return std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::string join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return b;
		std::string out = a;
		while (!out.empty() && (out.back() == '\\' || out.back() == '/'))
			out.pop_back();
		out += '\\';
		out += b;
		return out;
	}

	// Row vector convention, the same one D3DMATRIX and the engine use.
	void multiply(const float* a, const float* b, float* out)
	{
		float tmp[16];

		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
			{
				float sum = 0;
				for (int k = 0; k < 4; k++)
					sum += a[row * 4 + k] * b[k * 4 + col];
				tmp[row * 4 + col] = sum;
			}

		std::memcpy(out, tmp, sizeof(tmp));
	}

	/* A shader constant register holds a column of the matrix (fxc packs a
	 * float4x4 column major by default, and the generated code confirms it:
	 * dp4 o.x, position, c0). A D3DMATRIX holds rows. So the transpose goes
	 * to the device - the single most common way to get a black screen out of
	 * an otherwise correct Direct3D 9 shader path. */
	void set_matrix_constant(int reg, const float* rowMajor)
	{
		float columns[16];

		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		g_device->SetVertexShaderConstantF((UINT)reg, columns, 4);
	}

	/* The group's matrix in c0 as prepare wrote it, or pushed back in depth
	 * for a stand range answered HD_STAND_BEHIND (hd_render.h): the clip z
	 * raised by HD_STAND_PUSH times the projection's b. With depth = a - b / z
	 * (HDRenderMakeProjection) the depth becomes a - b (1 - HD_STAND_PUSH) / z,
	 * the depth of the point HD_STAND_PUSH times its distance further away;
	 * x, y and w are untouched, so the art keeps its pixels and loses the
	 * depth test to a classic face it lies within that distance in front of.
	 * Returns whether c0 now holds the pushed matrix. */
	bool set_stand_matrix(const float* wvp, bool behind)
	{
		if (!wvp)
			return false;

		float moved[16];

		std::memcpy(moved, wvp, sizeof(moved));

		if (behind)
			moved[14] -= HD_STAND_PUSH * g_proj[14];

		set_matrix_constant(0, moved);
		return behind;
	}

	/* The classic D3DMView for this room, as three columns rather than four.
	 * The shader declares it float4x3 and only wants the xyz of the product;
	 * a fourth register here would land on one of fxc's own def constants and
	 * overwrite a literal - which is silent, and looks like a maths error
	 * three files away. */
	void set_worldview_constant(const float* rowMajor)
	{
		float columns[12];

		for (int col = 0; col < 3; col++)
			for (int row = 0; row < 4; row++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		g_device->SetVertexShaderConstantF(5, columns, 3);
	}

	/* --- the parity constants ------------------------------------------
	 *
	 * The register table lives in hd_room.hlsl and is repeated nowhere:
	 * these two functions are the only writers, and they write every one of
	 * their registers on every call - a constant left over from the room
	 * before is the whole reason this is not done "only when it changes",
	 * with the single exception of the 64 register water table, which is a
	 * kilobyte and is keyed on the pointer the engine hands over. */
	void apply_room_lighting(const HDRoomLighting* r)
	{
		float origin[4] = { 0, 0, 0, -1.0e9f };	// a surface no vertex sits on
		float mode[4] = { 0, 0, 0, 0 };
		float sel[4] = { 0, 0, 0, 0 };
		float counts[4] = { 0, (float)g_frame_bulbs, 0, 0 };
		float lights[HD_MAX_LIGHTS * 2 * 4] = { 0 };
		const void* table = 0;

		if (r)
		{
			origin[0] = r->originX;
			origin[1] = r->originY;
			origin[2] = r->originZ;
			origin[3] = r->surfaceY;

			mode[0] = r->underwater ? 1.0f : 0.0f;
			mode[1] = r->choppy ? 1.0f : 0.0f;
			mode[2] = r->surfaceGate ? 1.0f : 0.0f;
			mode[3] = r->redAlert ? 1.0f : 0.0f;

			sel[0] = r->sel1[0];
			sel[1] = r->sel1[1];
			sel[2] = r->sel1[2];
			sel[3] = r->sel1[3];

			int count = r->lights ? r->lightCount : 0;

			if (count > HD_MAX_LIGHTS)
				count = HD_MAX_LIGHTS;

			for (int i = 0; i < count; i++)
			{
				const HDLightSource& l = r->lights[i];

				lights[i * 8 + 0] = l.x;
				lights[i * 8 + 1] = l.y;
				lights[i * 8 + 2] = l.z;
				lights[i * 8 + 3] = l.falloff;
				lights[i * 8 + 4] = l.r;
				lights[i * 8 + 5] = l.g;
				lights[i * 8 + 6] = l.b;
				lights[i * 8 + 7] = l.falloff != 0 ? 1.0f / l.falloff : 0.0f;
			}

			counts[0] = (float)count;
			table = r->waterTable;
		}

		if (table != g_water_table_up)
		{
			float rows[64 * 4];
			const HDWaterTab* in = (const HDWaterTab*)table;

			for (int i = 0; i < 64; i++)
			{
				rows[i * 4 + 0] = in ? (float)in[i].shimmer : 0.0f;
				rows[i * 4 + 1] = in ? (float)in[i].choppy : 0.0f;
				rows[i * 4 + 2] = in ? (float)in[i].random : 0.0f;
				rows[i * 4 + 3] = in ? (float)in[i].abs : 0.0f;
			}

			g_device->SetVertexShaderConstantF(32, rows, 64);
			g_water_table_up = table;
		}

		g_device->SetVertexShaderConstantF(10, origin, 1);
		g_device->SetVertexShaderConstantF(11, mode, 1);
		g_device->SetVertexShaderConstantF(12, sel, 1);
		/* The room origin goes into the pixel file as well, because the
		 * caustic is computed per pixel out of the world position and the two
		 * constant files are separate in Direct3D 9. One more upload per room,
		 * and the alternative - a seventh interpolator carrying the world
		 * position - would cost every pixel of every room instead. */
		g_device->SetPixelShaderConstantF(10, origin, 1);
		g_device->SetPixelShaderConstantF(17, counts, 1);
		g_device->SetPixelShaderConstantF(20, lights, HD_MAX_LIGHTS * 2);
	}

	void apply_frame_lighting(const HDFrameLighting* f)
	{
		float screen[4] = { 1, 1, 0, 0 };
		float fog[4] = { 1.0e30f, 0, 0, 0 };	// nothing is ever behind it
		float wibble[32 * 4] = { 0 };
		float bulbs[HD_MAX_BULBS * 2 * 4] = { 0 };

		g_frame_bulbs = 0;
		g_frame_has_light = f != 0;

		if (f)
		{
			screen[0] = f->centerX != 0 ? f->centerX : 1.0f;
			screen[1] = f->centerY != 0 ? f->centerY : 1.0f;
			screen[2] = f->wibble;
			screen[3] = f->cameraUnderwater ? 1.0f : 0.0f;

			if (f->distanceFogStart > 0)
			{
				fog[0] = f->distanceFogStart;
				fog[1] = 255.0f / f->distanceFogStart;
			}

			if (f->wibbleTable)
				for (int i = 0; i < 32; i++)
					wibble[i * 4] = f->wibbleTable[i];

			int count = f->bulbs ? f->bulbCount : 0;

			if (count > HD_MAX_BULBS)
				count = HD_MAX_BULBS;

			for (int j = 0; j < count; j++)
			{
				const HDFogBulb& b = f->bulbs[j];

				bulbs[j * 8 + 0] = b.x;
				bulbs[j * 8 + 1] = b.y;
				bulbs[j * 8 + 2] = b.z;
				bulbs[j * 8 + 3] = b.rad;
				bulbs[j * 8 + 4] = b.r;
				bulbs[j * 8 + 5] = b.g;
				bulbs[j * 8 + 6] = b.b;
				bulbs[j * 8 + 7] = b.density;
			}

			g_frame_bulbs = count;
		}

		/* The clock and the switch. The scroll offset in xy belongs
		 * to a draw call and is written by draw_pass; here only the two the
		 * whole frame shares. With motion off both are zero and every term in
		 * the shader that reads them is multiplied out. */
		g_motion[0] = 0.0f;
		g_motion[1] = 0.0f;
		g_motion[2] = f ? f->clock : 0.0f;
		g_motion[3] = (f && f->motion) ? 1.0f : 0.0f;
		// And the books' clock, which the draw reads on the processor.
		g_book_clock = f ? f->bookClock : 0.0;

		/* hd_reflect, the one switch over every gloss in the frame. Zero
		 * makes the shader's specular term vanish, to the bit. */
		g_reflect = (!f || f->reflect) ? 1.0f : 0.0f;

		g_device->SetVertexShaderConstantF(9, screen, 1);
		g_device->SetVertexShaderConstantF(13, g_motion, 1);
		g_device->SetVertexShaderConstantF(96, wibble, 32);
		g_device->SetPixelShaderConstantF(16, fog, 1);
		g_device->SetPixelShaderConstantF(40, bulbs, HD_MAX_BULBS * 2);

		// And a room with no lighting of its own: neutral, never the last
		// room's.
		apply_room_lighting(0);
	}

	/* The mirrors of the level being drawn - see HDMirror in hd_render.h and
	 * FindMirrors below. Released with the groups. */
	std::vector<HDMirror> g_mirrors;

	/* The rule that finds them.
	 *
	 * A mirror is a wall of a room with three things at once, all read out
	 * of the art:
	 *
	 *   - a pane: triangles of the group lying in the wall's plane, within
	 *     HD_MIRROR_PANE_TOL of it, in the additive pass or of material 14
	 *     in the alpha pass - the glass or its sheen;
	 *   - a room's worth of the group's own art behind the plane: at least
	 *     HD_MIRROR_BEHIND triangles, reaching HD_MIRROR_DEPTH or more past
	 *     it - a copy of a room, not the thickness of a wall;
	 *   - and that art is the reflection of the level's art in front: at
	 *     least HD_MIRROR_MATCH per cent of those triangles, reflected in the
	 *     plane, land on the centroid of a triangle of the level to within
	 *     one 8-unit cell.
	 *
	 * Only the four walls of the room's own inside box are asked (HDRoomBox:
	 * one sector in from the grid's edge), which is where a mirror hangs.
	 *
	 * Measured over the fifteen files with the classic rooms' boxes: RICH2
	 * room 134, west wall x = 33792, pane 12, 6,161 triangles behind reaching
	 * 2,054 deep, 6,107 of them (99 %) reflected; room 135, west wall
	 * x = 30720, pane 16, 6,237 behind, 2,051 deep, 6,201 (99 %). Nothing
	 * else in the game has all three. The walls with a pane that come
	 * nearest are ANDREA2 rooms 65 and 132 - a material 14 window with 692
	 * triangles behind it, every one reflected, but 26 units deep: the
	 * other face of the window's own frame - and RICH3 rooms 73 and 74 (120
	 * behind, 22 deep, 80 %). The walls without a pane that reflect the most
	 * are shared walls whose art both rooms carry (JOBY2 room 80, 9,164
	 * behind, 98 %), and those have no glass to be seen through. */
	#define HD_MIRROR_PANE_TOL	2.0f
	#define HD_MIRROR_BEHIND	100
	#define HD_MIRROR_DEPTH		1024.0f
	#define HD_MIRROR_MATCH		90

	inline unsigned long long MirrorCell(float x, float y, float z)
	{
		const long long cx = (long long)floorf(x / 8.0f + 0.5f);
		const long long cy = (long long)floorf(y / 8.0f + 0.5f);
		const long long cz = (long long)floorf(z / 8.0f + 0.5f);

		return ((unsigned long long)(cx & 0x1FFFFF) << 42) |
			((unsigned long long)(cy & 0x1FFFFF) << 21) |
			(unsigned long long)(cz & 0x1FFFFF);
	}

	void FindMirrors(const trf::remaster::Trg& trg)
	{
		g_mirrors.clear();

		if (!g_boxes || g_box_count <= 0)
			return;

		const int rooms = g_box_count < g_room_groups ? g_box_count : g_room_groups;

		for (int g = 0; g < rooms && g < (int)trg.groups.size(); g++)
		{
			const HDRoomBox& box = g_boxes[g];
			const float origin[3] = { box.originX, 0.0f, box.originZ };

			if (box.maxs[0] <= box.mins[0] || box.maxs[2] <= box.mins[2])
				continue;

			// The four walls: west, east, south, north.
			HDMirror wall[4];

			for (int w = 0; w < 4; w++)
			{
				HDMirror& m = wall[w];

				m.group = g;
				m.axis = w < 2 ? 0 : 2;
				m.side = (w & 1) ? 1 : -1;
				m.plane = origin[m.axis] +
					((w & 1) ? box.maxs[m.axis] : box.mins[m.axis]);
				m.pane = m.behind = m.mirrored = 0;

				for (int k = 0; k < 3; k++)
				{
					m.mins[k] = 1.0e9f;
					m.maxs[k] = -1.0e9f;
				}
			}

			float depth[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			// One walk over the group for all four walls.
			for (const trf::remaster::TrgPart& part : trg.groups[(size_t)g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];
					const bool glass = pass == 2 || (pass == 1 &&
						MaterialBase(part) == HD_TRG_MATERIAL_DECAL);

					for (unsigned int at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned int first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						float p[3][3];
						bool ok = true;

						for (int c = 0; c < 3; c++)
						{
							const unsigned int index = trg.indices[first + (unsigned int)c];

							if (index >= trg.vertices.size())
							{
								ok = false;
								break;
							}

							const trf::remaster::TrgVertex& v = trg.vertices[index];

							p[c][0] = (float)v.x + origin[0];
							p[c][1] = (float)v.y;
							p[c][2] = (float)v.z + origin[2];
						}

						if (!ok)
							continue;

						for (int w = 0; w < 4; w++)
						{
							HDMirror& m = wall[w];
							const int a = m.axis;

							if (glass &&
								fabsf(p[0][a] - m.plane) <= HD_MIRROR_PANE_TOL &&
								fabsf(p[1][a] - m.plane) <= HD_MIRROR_PANE_TOL &&
								fabsf(p[2][a] - m.plane) <= HD_MIRROR_PANE_TOL)
								m.pane++;

							const float d = ((p[0][a] + p[1][a] + p[2][a]) / 3.0f -
								m.plane) * (float)m.side;

							if (d <= 16.0f)
								continue;

							m.behind++;

							if (d > depth[w])
								depth[w] = d;

							for (int c = 0; c < 3; c++)
								for (int k = 0; k < 3; k++)
								{
									if (p[c][k] < m.mins[k]) m.mins[k] = p[c][k];
									if (p[c][k] > m.maxs[k]) m.maxs[k] = p[c][k];
								}
						}
					}
				}

			for (int w = 0; w < 4; w++)
			{
				HDMirror& m = wall[w];
				const int a = m.axis;

				if (!m.pane || m.behind < HD_MIRROR_BEHIND ||
					depth[w] < HD_MIRROR_DEPTH)
					continue;

				/* The art in front that the copy could be a reflection of:
				 * every room's triangles whose group box meets the copy's box
				 * reflected in the plane. A handful of groups - only a wall
				 * that already has a pane and a room behind it gets this far,
				 * which is two walls in the game. */
				float want_min[3], want_max[3];

				for (int k = 0; k < 3; k++)
				{
					want_min[k] = m.mins[k] - 32.0f;
					want_max[k] = m.maxs[k] + 32.0f;
				}

				want_min[a] = 2.0f * m.plane - m.maxs[a] - 32.0f;
				want_max[a] = 2.0f * m.plane - m.mins[a] + 32.0f;

				std::unordered_set<unsigned long long> cells;

				for (int o = 0; o < rooms && o < (int)trg.groups.size() &&
					o < (int)g_groups.size(); o++)
				{
					const HDGroup& other = g_groups[(size_t)o];
					const float oo[3] = { g_boxes[o].originX, 0.0f, g_boxes[o].originZ };

					if (!other.hasBounds)
						continue;

					bool meets = true;

					for (int k = 0; k < 3; k++)
						if (other.mins[k] + oo[k] > want_max[k] ||
							other.maxs[k] + oo[k] < want_min[k])
							meets = false;

					if (!meets)
						continue;

					for (const trf::remaster::TrgPart& part : trg.groups[(size_t)o].parts)
						for (int pass = 0; pass < 3; pass++)
						{
							const trf::remaster::TrgPart::Pass& run = part.pass[pass];

							for (unsigned int at = 0; at + 2 < run.count; at += 3)
							{
								const unsigned int first = run.start + at;

								if (first + 2 >= trg.indices.size())
									break;

								float c[3] = { 0.0f, 0.0f, 0.0f };

								for (int v = 0; v < 3; v++)
								{
									const unsigned int index =
										trg.indices[first + (unsigned int)v];

									if (index >= trg.vertices.size())
										continue;

									c[0] += (float)trg.vertices[index].x + oo[0];
									c[1] += (float)trg.vertices[index].y;
									c[2] += (float)trg.vertices[index].z + oo[2];
								}

								cells.insert(MirrorCell(c[0] / 3.0f, c[1] / 3.0f,
									c[2] / 3.0f));
							}
						}
				}

				// And the copy's own triangles, reflected, against them.
				for (const trf::remaster::TrgPart& part : trg.groups[(size_t)g].parts)
					for (int pass = 0; pass < 3; pass++)
					{
						const trf::remaster::TrgPart::Pass& run = part.pass[pass];

						for (unsigned int at = 0; at + 2 < run.count; at += 3)
						{
							const unsigned int first = run.start + at;

							if (first + 2 >= trg.indices.size())
								break;

							float c[3] = { 0.0f, 0.0f, 0.0f };

							for (int v = 0; v < 3; v++)
							{
								const unsigned int index =
									trg.indices[first + (unsigned int)v];

								if (index >= trg.vertices.size())
									continue;

								c[0] += (float)trg.vertices[index].x + origin[0];
								c[1] += (float)trg.vertices[index].y;
								c[2] += (float)trg.vertices[index].z + origin[2];
							}

							for (int k = 0; k < 3; k++)
								c[k] /= 3.0f;

							if ((c[a] - m.plane) * (float)m.side <= 16.0f)
								continue;

							c[a] = 2.0f * m.plane - c[a];

							bool found = false;

							for (int dx = -1; dx <= 1 && !found; dx++)
								for (int dy = -1; dy <= 1 && !found; dy++)
									for (int dz = -1; dz <= 1 && !found; dz++)
										found = cells.count(MirrorCell(
											c[0] + 8.0f * (float)dx,
											c[1] + 8.0f * (float)dy,
											c[2] + 8.0f * (float)dz)) != 0;

							if (found)
								m.mirrored++;
						}
					}

				if (m.mirrored * 100 >= m.behind * HD_MIRROR_MATCH)
					g_mirrors.push_back(m);
			}
		}
	}

	/* Why a draw call keeps the plain variant whatever its page names, or
	 * HD_PLAIN_NONE where it may take the gloss variant. The two surface
	 * passes of the rooms only - an additive part paints light and the sky
	 * is no room - and none of the draw calls whose feature the variant does
	 * not carry: the clear floors and the glass (the blend phase), the water
	 * surfaces (the ripple, whose slots the sheen takes) and the sky band's
	 * flat light. Those keep the plain variant and, where their material
	 * names one, its grazing-angle lift. The variant carries the caustic, and
	 * the scroll, the flipbook and the book are a texture address, a texture
	 * and an offset the processor sets for either variant alike, so a range
	 * with one of them takes the sheen its page names. */
	enum
	{
		HD_PLAIN_NONE,
		HD_PLAIN_NOT_A_SURFACE,	// the additive pass, the sky
		HD_PLAIN_GLASS,
		HD_PLAIN_CLEAR,
		HD_PLAIN_RIPPLE,
		HD_PLAIN_FLAT
	};

	int RangeKeepsPlain(const HDRange& r, int pass, bool sky)
	{
		if (sky || pass == 2)
			return HD_PLAIN_NOT_A_SURFACE;

		if (r.clear)
			return HD_PLAIN_CLEAR;

		if (r.blend != 0.0f)
			return HD_PLAIN_GLASS;

		if (r.ripple[0] != 0.0f || r.ripple[1] != 0.0f || r.ripple[2] != 0.0f)
			return HD_PLAIN_RIPPLE;

		if (r.flatLight != 0.0f)
			return HD_PLAIN_FLAT;

		return HD_PLAIN_NONE;
	}

	/* Once per level: the draw calls that take the gloss variant, and the
	 * GLOSSLOG skipped line - the draw calls whose page names a gloss and
	 * that keep the plain variant for each reason, and the clear floors'
	 * draw calls, which keep it by their class whatever names their page. */
	int g_sheen_ranges = 0;
	HDRenderSheenSkipped g_sheen_skipped;

	/* Which surfaces shine - the four sources of hd_gloss_math.h.
	 *
	 * `g_gloss_path` is the override file handed in for the next build
	 * (HDRenderGlossFile), `g_gloss_file` what the build read out of it.
	 * `g_votes` is the seed's walk over the level (hd_gloss_seed.h), and
	 * `g_page_gloss` the answer for a page's plain triangles, worked out once
	 * per page. The part counts and the page lines are the level's GLOSSLOG. */
	std::string g_gloss_path;
	HDGlossFile g_gloss_file;
	std::unordered_map<int, HDGlossVotes> g_votes;
	std::unordered_map<int, HDGloss> g_page_gloss;
	std::vector<HDRenderGlossPage> g_gloss_pages;
	int g_gloss_parts_by[5] = { 0, 0, 0, 0, 0 };
	// HDRenderGlossOverride: every sheen at its upper bound. False in the engine.
	bool g_gloss_max = false;

	/* The resolver for one page (HDGlossResolve), with the page's slot for the
	 * engine's second bar: a page part-way clear in every texel is a floor
	 * of the blend phase (HD_PASS_CLEAR) and never shines, whatever names it
	 * - RICH2's, RICHCUT2's and TITLE's clear floors, beside the lobby pages
	 * HDGlossPageBarred names. */
	HDGloss ResolvePage(int page, int slot, const HDGloss* mat)
	{
		bool removes = false;
		const HDGlossPage* file = g_gloss_file.find(page, &removes);
		const bool barred = page < 0 || HDGlossPageBarred(page) ||
			HDPageCacheSlotClear(slot);

		return HDGlossResolve(mat, file, removes, HDGlossTableFind(page),
			HDGlossSeedOf(g_votes, page), barred);
	}

	// The same for the triangles of a page no material names, remembered.
	HDGloss PageGloss(int page)
	{
		const std::unordered_map<int, HDGloss>::const_iterator at = g_page_gloss.find(page);

		if (at != g_page_gloss.end())
			return at->second;

		const HDGloss got = ResolvePage(page, HDPageCacheSlotOf(page), 0);

		g_page_gloss[page] = got;
		return got;
	}

	/* Reads the override file for the build that is about to run. A file
	 * that is not there is no file (`present` false); one that is there is
	 * read whole and parsed line by line (HDGlossParseText) - a bad line is
	 * counted and the rest are kept. */
	void ReadGlossFile()
	{
		g_gloss_file = HDGlossFile();

		if (g_gloss_path.empty())
			return;

		std::FILE* file = std::fopen(g_gloss_path.c_str(), "rb");

		if (!file)
			return;

		std::string text;
		char buffer[4096];
		size_t got;

		while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0 &&
			text.size() < (1u << 20))
			text.append(buffer, got);

		std::fclose(file);
		HDGlossParseText(text.c_str(), &g_gloss_file);
	}

	/* The sheen of every draw call, once the level's ranges are built.
	 *
	 * A draw call is one page of one room, so its sheen is its page's: the
	 * material's where a material 3 or 17 part drew it (MaterialGloss, and
	 * the class follows from the material - 17 the submarine's painted
	 * steel, metal; 3's masks select window panes, puddles and ice, a
	 * dielectric), otherwise what the file, the table or the seed name for
	 * the page (ResolvePage, in that order). It is worked out AFTER the sort,
	 * and nothing of it is in the sort key: the ranges, their order and their
	 * draw calls are the ones the level had before, and the page is in the
	 * key already. Whether the page's alpha masks the sheen is the page's
	 * own smallest alpha against HD_GLOSS_MASK_BELOW. What a glossy draw call
	 * is lit with is the level's light field, read per pixel - nothing of it
	 * is chosen here, so every room that wears a page shines alike, a room
	 * with a clear floor as well; the lobby's own pages are barred by
	 * page (HDGlossPageBarred) and the clear floors by their class. */
	void ResolveSheen()
	{
		g_sheen_ranges = 0;
		g_gloss_pages.clear();
		std::memset(&g_sheen_skipped, 0, sizeof(g_sheen_skipped));

		std::map<long long, size_t> line_of;

		for (size_t g = 0; g < g_groups.size(); g++)
		{
			const bool sky = (int)g >= g_room_groups;

			for (int pass = 0; pass < 2; pass++)
				for (HDRange& range : g_groups[g].pass[pass])
				{
					range.sheen = 0.0f;

					const int plain = RangeKeepsPlain(range, pass, sky);

					if (plain == HD_PLAIN_NOT_A_SURFACE || plain == HD_PLAIN_GLASS)
						continue;

					if (plain == HD_PLAIN_CLEAR)
					{
						g_sheen_skipped.clear++;
						continue;
					}

					const int page = HDPageCachePageOfSlot(range.pageSlot);
					HDGloss got;

					if (range.gloss > 0.0f)
					{
						HDGloss mat;

						HDGlossClassDefaults(HDGlossMaterialClass((unsigned int)range.glossMat), &mat);
						mat.source = HD_GLOSS_FROM_MAT;
						mat.strength = range.gloss;
						mat.power = range.glossPower;
						got = ResolvePage(page, range.pageSlot, &mat);
					}
					else
						got = PageGloss(page);

					// A water surface or the sky band: counted, kept plain.
					if (plain != HD_PLAIN_NONE)
					{
						if (got.strength > 0.0f && plain == HD_PLAIN_RIPPLE)
							g_sheen_skipped.ripple++;
						else if (got.strength > 0.0f && plain == HD_PLAIN_FLAT)
							g_sheen_skipped.flat++;

						continue;
					}

					// The upper bound, for the tool's picture alone.
					if (g_gloss_max)
					{
						if (got.strength <= 0.0f)
							HDGlossClassDefaults(HD_GLOSS_METAL, &got);

						got.strength = 1.0f;
						got.f0 = 1.0f;
					}

					if (got.strength <= 0.0f)
						continue;

					/* The level's GLOSSLOG page lines: one per page and source,
					 * with the triangles that take it. */
					const long long key = ((long long)page << 3) | got.source;
					const std::map<long long, size_t>::const_iterator at = line_of.find(key);

					if (at == line_of.end())
					{
						HDRenderGlossPage line;

						line.page = page;
						line.cls = got.cls;
						line.source = got.source;
						line.strength = got.strength;
						line.power = got.power;
						line.f0 = got.f0;
						line.tris = (int)(range.count / 3);
						line_of[key] = g_gloss_pages.size();
						g_gloss_pages.push_back(line);
					}
					else
						g_gloss_pages[at->second].tris += (int)(range.count / 3);

					range.sheen = got.strength;
					range.sheenPower = got.power;
					range.sheenF0 = got.f0;
					range.sheenTint = got.tint;
					range.sheenClass = got.cls;
					range.sheenSource = got.source;
					range.sheenMask = HDPageCacheSlotAlphaMin(range.pageSlot) <
						HD_GLOSS_MASK_BELOW ? 1.0f : 0.0f;
					g_sheen_ranges++;
				}
		}
	}

	void release_field()
	{
		if (g_field_dir) { g_field_dir->Release(); g_field_dir = 0; }
		if (g_field_rgb) { g_field_rgb->Release(); g_field_rgb = 0; }

		std::memset(&g_field_grid, 0, sizeof(g_field_grid));
		std::memset(&g_field_stats, 0, sizeof(g_field_stats));
		g_field_ms = 0.0f;
		g_field_bytes = 0;
	}

	// One build array into one volume texture of the field's size.
	IDirect3DVolumeTexture9* MakeFieldTexture(const std::vector<unsigned int>& texels)
	{
		const HDGlossFieldGrid& grid = g_field_grid;
		IDirect3DVolumeTexture9* texture = 0;

		if (FAILED(g_device->CreateVolumeTexture((UINT)grid.dims[0], (UINT)grid.dims[1],
			(UINT)grid.dims[2], 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, 0)))
			return 0;

		D3DLOCKED_BOX box;

		if (FAILED(texture->LockBox(0, &box, 0, 0)))
		{
			texture->Release();
			return 0;
		}

		for (int z = 0; z < grid.dims[2]; z++)
			for (int y = 0; y < grid.dims[1]; y++)
				std::memcpy((unsigned char*)box.pBits + (size_t)z * box.SlicePitch +
					(size_t)y * box.RowPitch, &texels[HDGlossFieldIndex(grid, 0, y, z)],
					(size_t)grid.dims[0] * sizeof(unsigned int));

		texture->UnlockBox(0);
		return texture;
	}

	/* The level's light field (hd_gloss_field.h): every room's .TRG point
	 * lights with a colour, in world units as the file stores them, over the
	 * rooms' boxes made world, into the two volume textures. A device that
	 * cannot filter a volume texture, or asks for powers of two past its
	 * largest side, gets none. The time it takes is the HDLOG build's and is
	 * logged on its own as GLOSSLOG's field_ms. */
	void BuildField(const trf::remaster::Trg& trg)
	{
		const double started = now_ms();

		release_field();
		g_lamp_count = 0;
		g_group_origin.assign(trg.groups.size() * 2, 0.0f);

		for (int g = 0; g < g_grid_count && (size_t)g < trg.groups.size(); g++)
		{
			g_group_origin[(size_t)g * 2] = g_grids[g].originX;
			g_group_origin[(size_t)g * 2 + 1] = g_grids[g].originZ;
		}

		std::vector<HDGlossLamp> lamps;
		std::vector<int> lamp_room;

		for (size_t g = 0; g < trg.groups.size() && (int)g < g_room_groups; g++)
			for (size_t i = 0; i < trg.groups[g].volumes.size(); i++)
			{
				const trf::remaster::TrgGroup::PointLight light = trg.groups[g].light(i);
				const HDGlossLamp lamp =
				{
					{ light.x, light.y, light.z }, { light.r, light.g, light.b }
				};

				if (light.r > 0.0f || light.g > 0.0f || light.b > 0.0f)
				{
					lamps.push_back(lamp);
					lamp_room.push_back((int)g);
				}
			}

		g_lamp_count = (int)lamps.size();

		const DWORD filters = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR;

		if (!g_device || lamps.empty() || !g_boxes || g_box_count <= 0 ||
			g_grid_count <= 0 || !(g_caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) ||
			(g_caps.VolumeTextureFilterCaps & filters) != filters)
			return;

		std::vector<float> boxes((size_t)g_box_count * 6);

		for (int b = 0; b < g_box_count; b++)
		{
			const HDRoomBox& room = g_boxes[b];
			float* out = &boxes[(size_t)b * 6];

			out[0] = room.originX + room.mins[0];
			out[1] = room.mins[1];
			out[2] = room.originZ + room.mins[2];
			out[3] = room.originX + room.maxs[0];
			out[4] = room.maxs[1];
			out[5] = room.originZ + room.maxs[2];
		}

		const bool pow2 = (g_caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP_POW2) != 0;
		std::vector<unsigned int> dir, rgb;

		if (!HDGlossFieldLayout(&boxes[0], g_box_count, &lamps[0], (int)lamps.size(), pow2,
			(int)g_caps.MaxVolumeExtent, &g_field_grid))
			return;

		const bool sighted = g_field_near_rooms == g_box_count &&
			g_field_near.size() == (size_t)g_box_count * (size_t)g_box_count;
		const HDGlossFieldSight sight = { &boxes[0], g_box_count, &lamp_room[0],
			sighted ? &g_field_near[0] : 0 };

		HDGlossFieldBuild(g_field_grid, &lamps[0], (int)lamps.size(), dir, rgb, &g_field_stats,
			sighted ? &sight : 0);
		g_field_dir = MakeFieldTexture(dir);
		g_field_rgb = MakeFieldTexture(rgb);

		if (!g_field_dir || !g_field_rgb)
		{
			release_field();
			return;
		}

		g_field_bytes = (int)(dir.size() * sizeof(unsigned int) * 2);
		g_field_ms = (float)(now_ms() - started);
	}

	void release_buffers()
	{
		if (g_vb) { g_vb->Release(); g_vb = 0; }
		if (g_ib) { g_ib->Release(); g_ib = 0; }
		if (g_piece_ib) { g_piece_ib->Release(); g_piece_ib = 0; }

		release_field();
		g_groups.clear();
		g_place_tris.clear();
		g_static_pieces.clear();
		g_static_piece_group.clear();
		g_piece_indices.clear();
		g_mirrors.clear();
		g_stand_tris.clear();
		g_stand_tri_holder.clear();
		g_stand_covers.clear();
		g_stand_cover_faces.clear();
		g_stand_kept = 0;
		g_stand_from_intrusions = 0;
		g_group_origin.clear();
		g_votes.clear();
		g_page_gloss.clear();
		g_gloss_pages.clear();
		g_room_groups = 0;
		g_vertex_count = 0;
		g_index32 = false;
	}
}

/* ---------------------------------------------------------------- set up -- */

bool HDRenderInit(IDirect3DDevice9* device, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	HDRenderShutdown();

	if (!device)
	{
		*why = "no device";
		return false;
	}

	g_device = device;
	std::memset(&g_caps, 0, sizeof(g_caps));

	if (FAILED(device->GetDeviceCaps(&g_caps)))
	{
		*why = "GetDeviceCaps failed";
		g_device = 0;
		return false;
	}

	/* The overrides, if a diagnostic tool asked for them. They are applied to
	 * the copy of the caps, so everything below - the level build, the page
	 * budget, the draw calls - reads the small card's answer and not the real
	 * one. */
	if (g_cap_max_side > 0)
	{
		g_caps.MaxTextureWidth = (DWORD)g_cap_max_side;
		g_caps.MaxTextureHeight = (DWORD)g_cap_max_side;
	}

	if (g_cap_no_index32)
		g_caps.MaxVertexIndex = 0xFFFF;

	/* What the HD path needs of the card, all of it 2004 hardware:
	 * vs_3_0 / ps_3_0, and the two packed declaration types the 20 byte
	 * vertex is made of. */
	if (g_caps.VertexShaderVersion < D3DVS_VERSION(3, 0) ||
		g_caps.PixelShaderVersion < D3DPS_VERSION(3, 0))
	{
		*why = "the device has no vertex/pixel shader 3.0";
		g_device = 0;
		return false;
	}

	/* bshader.bat compiles three pixel shaders for ps_3_0, the room shader at
	 * 476 instruction slots, the model shader at 353 and the glow shader at
	 * 479, and the room shader's gloss variant beside them at 502
	 * (hd_room_gloss_ps.inc). The ps_3_0 figure is
	 * MaxPixelShader30InstructionSlots - PS20Caps is the ps_2_x one - and
	 * shader model 3 sets its floor at D3DMIN30SHADERINSTRUCTIONS, 512. fxc
	 * does not hold a ps_3_0 shader to 512: a 24-bulb room shader compiles
	 * at 524 slots without a warning. The three shaders stay inside the
	 * guarantee only because of their own size, 476, 353 and 479, so anyone who
	 * grows one has to read the "instruction slots used" line of its .inc.
	 * A device that answers less than 512 could fail CreatePixelShader below
	 * with nothing but an HRESULT, so it is refused here with a reason in
	 * the log. A device that leaves the
	 * field at 0 is not refused here; CreatePixelShader then decides. */
	if (g_caps.MaxPixelShader30InstructionSlots &&
		g_caps.MaxPixelShader30InstructionSlots < D3DMIN30SHADERINSTRUCTIONS)
	{
		*why = "the device allows too few pixel shader instruction slots";
		g_device = 0;
		return false;
	}

	if (!(g_caps.DeclTypes & D3DDTCAPS_UBYTE4))
	{
		*why = "the device has no UBYTE4 vertex element";
		g_device = 0;
		return false;
	}

	static const D3DVERTEXELEMENT9 elements[] =
	{
		{ 0,  0, D3DDECLTYPE_SHORT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0,  8, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },
		{ 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
		{ 0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    1 },
		{ 0, 20, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
		D3DDECL_END()
	};

	if (FAILED(device->CreateVertexDeclaration(elements, &g_decl)))
	{
		*why = "CreateVertexDeclaration failed";
		g_decl = 0;
		g_device = 0;
		return false;
	}

	if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomVS, &g_vs)))
	{
		*why = "CreateVertexShader failed";
		HDRenderShutdown();
		return false;
	}

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomPS, &g_ps)))
	{
		*why = "CreatePixelShader failed";
		HDRenderShutdown();
		return false;
	}

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDGlowPS, &g_glow_ps)))
	{
		*why = "CreatePixelShader failed for the glow shader";
		HDRenderShutdown();
		return false;
	}

	/* The gloss variant is not a condition of the HD path: a device that
	 * refuses it draws every surface with the plain variant, which is the
	 * picture hd_reflect 0 draws. */
	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomGlossPS, &g_gloss_ps)))
		g_gloss_ps = 0;

	// Nor is the projected shadow's: without it the blob is drawn.
	if (FAILED(device->CreatePixelShader((const DWORD*)kHDShadowReceivePS,
		&g_shadow_receive_ps)))
		g_shadow_receive_ps = 0;

	// Nor is its two-map variant: without it the creatures keep their blobs.
	if (FAILED(device->CreatePixelShader((const DWORD*)kHDShadowReceive2PS,
		&g_shadow_receive2_ps)))
		g_shadow_receive2_ps = 0;

	// Nor is the mirror's variant: without it the copy takes the ordinary one.
	if (FAILED(device->CreatePixelShader((const DWORD*)kHDShadowReceiveMirrorPS,
		&g_shadow_receive_mirror_ps)))
		g_shadow_receive_mirror_ps = 0;

	return true;
}

void HDRenderCapOverride(int maxTextureSide, int noIndex32)
{
	g_cap_max_side = maxTextureSide > 0 ? maxTextureSide : 0;
	g_cap_no_index32 = noIndex32 ? 1 : 0;
}

void HDRenderShoreOverride(int off)
{
	g_no_shore = off ? true : false;
}

void HDRenderBlendOverride(int off)
{
	g_no_blend = off ? true : false;
}

void HDRenderClearOverride(int off)
{
	g_no_clear = off ? true : false;
}

void HDRenderUprightOverride(int off)
{
	g_no_upright = off ? true : false;
}

void HDRenderStackOverride(int keep)
{
	g_keep_stacks = keep ? true : false;
}

void HDRenderSwingOverride(int keepFast)
{
	g_keep_fast_swing = keepFast ? true : false;
}

void HDRenderAdditiveOnly(int material)
{
	g_add_only = material >= 0 ? material : -1;
}

void HDRenderGlowOverride(int sum, int noAniso, int noShader)
{
	g_glow_sum = sum ? true : false;
	g_glow_no_aniso = noAniso ? true : false;
	g_glow_no_shader = noShader ? true : false;
}

int HDRenderGlowPartCount()
{
	return (int)g_glow_parts.size();
}

bool HDRenderGlowPartInfo(int at, HDRenderGlowPart* out)
{
	if (!out || at < 0 || at >= (int)g_glow_parts.size())
		return false;

	const HDGlowPart& glow = g_glow_parts[(size_t)at];

	out->group = glow.group;
	out->part = glow.part;
	out->page = glow.page;
	out->u0 = glow.u0;
	out->u1 = glow.u1;
	out->v0 = glow.v0;
	out->v1 = glow.v1;
	out->edge = glow.edge ? 1 : 0;
	out->face = glow.face ? 1 : 0;
	out->corners = glow.corners;
	out->disagree = glow.disagree;
	out->asset = glow.asset;
	out->axis[0] = glow.axis[0];
	out->axis[1] = glow.axis[1];
	out->axis[2] = glow.axis[2];
	out->lo = glow.lo;
	out->hi = glow.hi;
	out->fade = glow.fade;
	return true;
}

int HDRenderBookPartCount()
{
	return (int)g_book_parts.size();
}

bool HDRenderBookPartInfo(int at, HDRenderBookPart* out)
{
	if (!out || at < 0 || at >= (int)g_book_parts.size())
		return false;

	*out = g_book_parts[(size_t)at];
	return true;
}

bool HDRenderBookWatch(int* group, int* part, int* page)
{
	if (!g_book_watch)
		return false;

	if (group)
		*group = g_book_watch_group;
	if (part)
		*part = g_book_watch_part;
	if (page)
		*page = g_book_watch_page;

	return true;
}

void HDRenderBookPageOverride(int page)
{
	g_book_page = page >= 0 ? page : -1;
}

static void ShadowForgetRefusal();

void HDRenderShutdown()
{
	release_buffers();
	HDPageCacheDestroy();

	/* The projected shadow's map belongs to the device that is going away,
	 * and a refusal was that device's answer: the next device is asked
	 * afresh. */
	HDRenderShadowTargetRelease();
	ShadowForgetRefusal();

	if (g_glow_ps) { g_glow_ps->Release(); g_glow_ps = 0; }
	if (g_gloss_ps) { g_gloss_ps->Release(); g_gloss_ps = 0; }
	if (g_shadow_receive_ps) { g_shadow_receive_ps->Release(); g_shadow_receive_ps = 0; }
	if (g_shadow_receive2_ps) { g_shadow_receive2_ps->Release(); g_shadow_receive2_ps = 0; }
	if (g_shadow_receive_mirror_ps)
	{
		g_shadow_receive_mirror_ps->Release();
		g_shadow_receive_mirror_ps = 0;
	}
	if (g_ps) { g_ps->Release(); g_ps = 0; }
	if (g_vs) { g_vs->Release(); g_vs = 0; }
	if (g_decl) { g_decl->Release(); g_decl = 0; }

	g_device = 0;
	g_in_frame = false;
	g_draw_calls = 0;
	g_cutout_calls = 0;
	g_flip_calls = 0;
	g_book_calls = 0;
	g_gloss_calls = 0;
	g_sheen_calls = 0;
	g_clear_calls = 0;
	g_water_calls = 0;
	g_error.clear();
}

bool HDRenderReady()
{
	return g_device && g_vs && g_ps && g_glow_ps && g_decl;
}

/* ---------------------------------------------------------------- level -- */

void HDRenderSetWaterMarks(const HDWaterMark* marks, int count)
{
	g_marks = count > 0 ? marks : 0;
	g_mark_count = marks ? count : 0;
}

void HDRenderSetRoomBoxes(const HDRoomBox* boxes, int count)
{
	g_boxes = count > 0 ? boxes : 0;
	g_box_count = boxes ? count : 0;
}

void HDRenderSetSectors(const HDRoomGrid* rooms, int roomCount,
	const HDSectorFloor* sectors, int sectorCount)
{
	const bool ok = rooms && roomCount > 0 && sectors && sectorCount > 0;

	g_grids = ok ? rooms : 0;
	g_grid_count = ok ? roomCount : 0;
	g_sectors = ok ? sectors : 0;
	g_sector_count = ok ? sectorCount : 0;
}

void HDRenderSplitOverhang(int on)
{
	g_split_overhang = on ? 1 : 0;
}

void HDRenderCullOpaque(int on)
{
	g_cull_opaque = on ? 1 : 0;
}

void HDRenderCullRanges(int* opaque, int* twoSided)
{
	int culled = 0;
	int both = 0;

	for (size_t g = 0; g < g_groups.size(); g++)
		for (int pass = 0; pass < 3; pass++)
			for (const HDRange& range : g_groups[g].pass[pass])
			{
				if (range.cull)
					culled++;
				else
					both++;
			}

	if (opaque)
		*opaque = culled;

	if (twoSided)
		*twoSided = both;
}

void HDRenderSetIntrusions(const HDIntrusion* list, int count)
{
	g_intrusions = count > 0 ? list : 0;
	g_intrusion_count = list ? count : 0;
	g_intrusion_hits.assign((size_t)(g_intrusion_count > 0 ? g_intrusion_count : 0), 0);
	g_intrusion_band.assign(g_intrusion_hits.size(), 0);
}

int HDRenderIntrusionHits(int at)
{
	return at >= 0 && at < (int)g_intrusion_hits.size()
		? g_intrusion_hits[(size_t)at] : 0;
}

int HDRenderIntrusionBand(int at)
{
	return at >= 0 && at < (int)g_intrusion_band.size()
		? g_intrusion_band[(size_t)at] : 0;
}

void HDRenderSetPairFaces(const HDPairFace* faces, int count)
{
	g_pair_faces = count > 0 ? faces : 0;
	g_pair_face_count = faces && count > 0 ? count : 0;
	g_pair_face_box.assign((size_t)g_pair_face_count * 6, 0.0f);

	for (int f = 0; f < g_pair_face_count; f++)
	{
		const HDPairFace& face = g_pair_faces[f];
		float* box = &g_pair_face_box[(size_t)f * 6];

		for (int axis = 0; axis < 3; axis++)
		{
			box[axis] = box[axis + 3] = face.v[0][axis];

			for (int c = 1; c < face.corners && c < 4; c++)
			{
				if (face.v[c][axis] < box[axis]) box[axis] = face.v[c][axis];
				if (face.v[c][axis] > box[axis + 3]) box[axis + 3] = face.v[c][axis];
			}

			box[axis] -= (float)HD_PAIR_FACE_REACH;
			box[axis + 3] += (float)HD_PAIR_FACE_REACH;
		}
	}
}

int HDRenderGroupPairRanges(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return (int)g_groups[(size_t)group].pairRanges.size();
}

bool HDRenderGroupPairRange(int group, int index, HDPairRange* out)
{
	if (group < 0 || group >= (int)g_groups.size() || !out)
		return false;

	const HDGroup& g = g_groups[(size_t)group];

	if (index < 0 || index >= (int)g.pairRanges.size())
		return false;

	*out = g.pairRanges[(size_t)index];
	return true;
}

void HDRenderPairWhole(int group, int index, int whole)
{
	if (group < 0 || group >= (int)g_groups.size())
		return;

	HDGroup& g = g_groups[(size_t)group];

	if (index >= 0 && index < (int)g.pairWhole.size())
		g.pairWhole[(size_t)index] = whole ? 1 : 0;
}

void HDRenderSetStandFaces(const HDStandRoom* rooms, int roomCount,
	const HDStandHolder* holders, int holderCount, const HDCoverFace* faces,
	int faceCount)
{
	const bool all = rooms && roomCount > 0 && holders && holderCount > 0 &&
		faces && faceCount > 0;

	g_stand_rooms = all ? rooms : 0;
	g_stand_room_count = all ? roomCount : 0;
	g_stand_holders = all ? holders : 0;
	g_stand_holder_count = all ? holderCount : 0;
	g_stand_faces = all ? faces : 0;
	g_stand_face_count = all ? faceCount : 0;
}

int HDRenderGroupStandRanges(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return (int)g_groups[(size_t)group].standRanges.size();
}

bool HDRenderGroupStandRange(int group, int index, HDStandRange* out)
{
	if (group < 0 || group >= (int)g_groups.size() || !out)
		return false;

	const HDGroup& g = g_groups[(size_t)group];

	if (index < 0 || index >= (int)g.standRanges.size())
		return false;

	*out = g.standRanges[(size_t)index];
	return true;
}

int HDRenderStandTriangleCount()
{
	return (int)g_stand_tris.size();
}

bool HDRenderStandTriangleAt(int at, HDCoverTri* tri, int* holder)
{
	if (at < 0 || at >= (int)g_stand_tris.size())
		return false;

	if (tri)
		*tri = g_stand_tris[(size_t)at];

	if (holder)
		*holder = g_stand_tri_holder[(size_t)at];

	return true;
}

int HDRenderStandCovers()
{
	return (int)g_stand_covers.size();
}

bool HDRenderStandCoverAt(int at, HDStandCover* out)
{
	if (at < 0 || at >= (int)g_stand_covers.size() || !out)
		return false;

	const StandCoverBuilt& one = g_stand_covers[(size_t)at];

	*out = one.cover;
	out->covered = one.cover.faceCount > 0 &&
		one.firstFlag + (size_t)one.cover.faceCount <= g_stand_cover_faces.size()
		? &g_stand_cover_faces[one.firstFlag] : 0;
	return true;
}

int HDRenderStandHolders()
{
	return g_stand_kept;
}

int HDRenderStandFromIntrusions()
{
	return g_stand_from_intrusions;
}

void HDRenderStandWith(int group, int index, int with)
{
	if (group < 0 || group >= (int)g_groups.size())
		return;

	HDGroup& g = g_groups[(size_t)group];

	if (index >= 0 && index < (int)g.standWith.size())
		g.standWith[(size_t)index] = (unsigned char)(with == HD_STAND_BEHIND
			? HD_STAND_BEHIND : with ? HD_STAND_WITH : HD_STAND_OUT);
}

// The key of a 1024-unit floor square of a world position.
static unsigned int PlaceSector(float x, float z)
{
	return ((unsigned int)(((int)floorf(x) >> 10) & 0xFFFF) << 16) |
		(unsigned int)(((int)floorf(z) >> 10) & 0xFFFF);
}

/* One triangle of group `g` that a static's piece may take: noted for every
 * place of another room whose box, grown by HD_STATIC_PIECE_REACH, holds its
 * centroid. `at` is where its first corner stands in the sorted index stream,
 * `first` where it starts in the .TRG's own. */
static void NotePlaceTri(const trf::remaster::Trg& trg, int g, int pass, int range,
	int stand, unsigned int at, unsigned int first)
{
	if (!g_place_count || g < 0 || g >= g_grid_count || !g_grids)
		return;

	PlaceTri tri;
	float centre[3] = { 0.0f, 0.0f, 0.0f };

	for (int k = 0; k < 3; k++)
	{
		const unsigned int index = trg.indices[first + (unsigned int)k];

		if (index >= trg.vertices.size())
			return;

		const trf::remaster::TrgVertex& v = trg.vertices[index];

		tri.corner[k * 3 + 0] = (float)v.x;
		tri.corner[k * 3 + 1] = (float)v.y;
		tri.corner[k * 3 + 2] = (float)v.z;

		for (int axis = 0; axis < 3; axis++)
			centre[axis] += tri.corner[k * 3 + axis] * (1.0f / 3.0f);
	}

	const float wx = centre[0] + g_grids[g].originX;
	const float wz = centre[2] + g_grids[g].originZ;
	const float reach = HD_STATIC_PIECE_REACH;
	const unsigned int key = PlaceSector(wx, wz);
	std::vector<std::pair<unsigned int, int> >::const_iterator it = std::lower_bound(
		g_place_sectors.begin(), g_place_sectors.end(), std::make_pair(key, -1));

	for (; it != g_place_sectors.end() && it->first == key; ++it)
	{
		const HDStaticPlace& place = g_places[it->second];

		if (place.room == g ||
			wx < place.mins[0] - reach || wx > place.maxs[0] + reach ||
			centre[1] < place.mins[1] - reach || centre[1] > place.maxs[1] + reach ||
			wz < place.mins[2] - reach || wz > place.maxs[2] + reach)
			continue;

		tri.place = it->second;
		tri.group = g;
		tri.pass = pass;
		tri.range = range;
		tri.stand = stand;
		tri.at = at;
		tri.index[0] = tri.index[1] = tri.index[2] = 0;
		g_place_tris.push_back(tri);
	}
}

void HDRenderSetStaticPlaces(const HDStaticPlace* places, int count)
{
	g_places = places && count > 0 ? places : 0;
	g_place_count = g_places ? count : 0;
	g_place_sectors.clear();

	// Every floor square a place's box, grown by the reach, touches.
	for (int p = 0; p < g_place_count; p++)
	{
		const HDStaticPlace& place = g_places[p];

		if (place.maxs[0] < place.mins[0] || place.maxs[2] < place.mins[2])
			continue;

		const int x0 = (int)floorf(place.mins[0] - HD_STATIC_PIECE_REACH) >> 10;
		const int x1 = (int)floorf(place.maxs[0] + HD_STATIC_PIECE_REACH) >> 10;
		const int z0 = (int)floorf(place.mins[2] - HD_STATIC_PIECE_REACH) >> 10;
		const int z1 = (int)floorf(place.maxs[2] + HD_STATIC_PIECE_REACH) >> 10;

		for (int x = x0; x <= x1; x++)
			for (int z = z0; z <= z1; z++)
				g_place_sectors.push_back(std::make_pair(
					((unsigned int)(x & 0xFFFF) << 16) | (unsigned int)(z & 0xFFFF), p));
	}

	std::sort(g_place_sectors.begin(), g_place_sectors.end());
}

void HDRenderSetCuts(const HDCut* list, int count)
{
	g_cuts = count > 0 ? list : 0;
	g_cut_count = list ? count : 0;
	g_cut_hits.assign((size_t)(g_cut_count > 0 ? g_cut_count : 0), 0);
}

int HDRenderCutHits(int at)
{
	return at >= 0 && at < (int)g_cut_hits.size()
		? g_cut_hits[(size_t)at] : 0;
}

int HDRenderGroupCutTriangles(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return (int)g_groups[(size_t)group].cutTris;
}

int HDRenderGroupIntrudingTriangles(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	const HDGroup& g = g_groups[(size_t)group];

	return (int)(g.intruding[0] + g.intruding[1] + g.intruding[2]);
}

bool HDRenderLevelBegin(const char* installRoot, const char* stem, int numRooms,
	bool managedPages, int threads, HDRenderStats* stats, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	release_buffers();
	HDPageCacheDestroy();

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!HDRenderReady())
	{
		*why = "HDRenderInit was not called or failed";
		return false;
	}

	if (!installRoot || !*installRoot || !stem || !*stem)
	{
		*why = "no install or no level name";
		return false;
	}

	const std::string trg_path = join(join(installRoot, "DATA"), std::string(stem) + ".TRG");

	const double read_started = now_ms();

	trf::remaster::Trg trg;
	std::string error;

	if (!trf::remaster::read_trg(trg_path, trg, error))
	{
		g_error = error;
		*why = g_error.c_str();
		return false;
	}

	const double read_ms = now_ms() - read_started;
	const double build_started = now_ms();

	g_vertex_count = (int)trg.vertices.size();
	g_room_groups = numRooms > 0 && numRooms < (int)trg.groups.size()
		? numRooms : (int)trg.groups.size();

	if (g_vertex_count <= 0 || trg.indices.empty())
	{
		*why = "the .TRG holds no geometry";
		return false;
	}

	/* What the sheen is lit with - see g_field_dir. The field needs the
	 * rooms' origins to put the rooms' own coordinates into the world and
	 * the rooms' boxes to know where to lie; both are handed in before the
	 * build (HDRenderSetSectors, HDRenderSetRoomBoxes). */
	BuildField(trg);

	g_cube_count = g_sector_count > 0 && (size_t)g_sector_count == trg.light_cubes.size()
		? (int)trg.light_cubes.size() : 0;

	/* Which surfaces shine: the override file and the seed, before the
	 * build asks for a page's answer (PageGloss). */
	ReadGlossFile();
	HDGlossSeedWalk(trg, g_room_groups, g_grids, g_grid_count, g_sectors,
		g_sector_count, g_votes);
	g_page_gloss.clear();

	for (int k = 0; k < 5; k++)
		g_gloss_parts_by[k] = 0;

	/* 32 bit indices unless the level is small enough not to need them, or
	 * the device cannot do them. ANDREA2 has 5,471,013 indices over 1.1 M
	 * vertices; there is no 16 bit reading of that as one stream.
	 *
	 * A device that answers 65535 to MaxVertexIndex is still served. Every
	 * draw call in this renderer is one range, and a range reaches over far
	 * fewer vertices than the level has - so the 16 bit path rebases each
	 * range's indices on the lowest vertex it names and moves the stream to
	 * that vertex instead, with SetStreamSource's
	 * byte offset. Sixteen bit indices then address 65536 vertices from
	 * wherever the range sits, and the level's 1.1 million are reached a
	 * window at a time. Whether every range really fits in such a window is a
	 * measurement, taken below and refused out loud when it fails.
	 *
	 * The stream offset was chosen over DrawIndexedPrimitive's BaseVertexIndex
	 * on purpose: a device that reports 65535 is stating what it can
	 * address, and adding a base to the index does not obviously stay under
	 * that ceiling, while a moved stream plainly does - it never shows the
	 * device more than 65536 vertices at once. */
	const bool device_index32 = g_caps.MaxVertexIndex > 0xFFFFu;

	g_index32 = g_vertex_count > 65535 && device_index32;

	/* The highest index, not the count: MaxVertexIndex is a DWORD and a card
	 * that reports 0xFFFFFFFF makes "count > MaxVertexIndex + 1" wrap to
	 * "count > 0" and refuses every level there is. Only the 32 bit path has
	 * to pass it; the 16 bit path never offers the device more than one
	 * window. */
	if (g_index32 && (unsigned)(g_vertex_count - 1) > g_caps.MaxVertexIndex)
	{
		*why = "the level has more vertices than the device can index";
		return false;
	}

	/* --- the pages ----------------------------------------------------
	 *
	 * The device's size limit goes into the policy here and not in
	 * hd_level: the format and the budget are the player's settings, but
	 * how wide a texture this card can hold is a fact of the device, and this
	 * is the one place that has both the caps and the cache in hand. Eleven
	 * pages of 5\TEX (and eight of 4\TEX) are 4096 wide by 2048 high, the
	 * widest the bundle ships (TrgProbe -ddssizes), and a card that stops at
	 * 2048 gets them one level down instead of not at all. */
	{
		HDPageCachePolicy policy;

		HDPageCachePolicyNow(&policy);
		policy.maxSide = g_caps.MaxTextureWidth && g_caps.MaxTextureHeight
			? (int)(g_caps.MaxTextureWidth < g_caps.MaxTextureHeight
				? g_caps.MaxTextureWidth : g_caps.MaxTextureHeight)
			: 0;
		HDPageCacheConfigure(&policy);
	}

	std::vector<int> pages;
	pages.reserve(trg.textures.size());

	for (unsigned short page : trg.textures)
		pages.push_back((int)page);

	HDPageCacheStats page_stats;

	if (!HDPageCacheCreate(g_device, installRoot, pages.empty() ? 0 : &pages[0],
		(int)pages.size(), managedPages, threads, &page_stats, why))
		return false;

	/* --- which group owns which vertex --------------------------------
	 *
	 * Only needed for the water marks, and only built when there are any. A
	 * .TRG names its vertices per group through the index runs and nowhere
	 * else, so the ownership is read out of the runs - the same walk TROne
	 * makes for its own water flag (TROne-main (copy of 2026-10-06, commit
	 * 75c4c843), hd_load). A vertex named by two groups keeps the last one;
	 * the marks are per room and a shared vertex sits on a portal, where both
	 * answers agree. */
	std::vector<int> vertex_group;
	std::unordered_map<unsigned long long, unsigned char> mark_map;
	/* One room's marked water surface at one height, as a box in x and z
	 * - see the note where it is filled. */
	struct HDMarkPlane { short minX, maxX, minZ, maxZ; };
	std::unordered_map<unsigned int, HDMarkPlane> plane_map;
	// The same, for the whole-room shimmer bit - see the note below.
	std::unordered_map<unsigned int, HDMarkPlane> room_plane_map;

	if (g_marks && g_mark_count > 0)
	{
		vertex_group.assign((size_t)g_vertex_count, -1);

		for (size_t g = 0; g < trg.groups.size(); g++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned int at = 0; at < run.count; at++)
					{
						const unsigned int at_index = run.start + at;

						if (at_index >= trg.indices.size())
							break;

						const unsigned int v = trg.indices[at_index];

						if (v < (unsigned int)g_vertex_count)
							vertex_group[v] = (int)g;
					}
				}

		mark_map.reserve((size_t)g_mark_count * 2);

		for (int i = 0; i < g_mark_count; i++)
		{
			const HDWaterMark& m = g_marks[i];
			const unsigned long long key =
				((unsigned long long)(unsigned short)m.room << 48) |
				((unsigned long long)(unsigned short)m.x << 32) |
				((unsigned long long)(unsigned short)m.y << 16) |
				(unsigned long long)(unsigned short)m.z;

			mark_map[key] |= m.bits;
		}

		/* --- the surface plane, not the corners ---------------------------
		 *
		 * The classic water answer carried to the HD vertex at the same
		 * position is right for every vertex the two meshes share - which on
		 * a water surface is the corners of the classic quads and nothing
		 * else. The remaster remeshes a room: where Core laid one quad over a
		 * whole pool, the .TRG lays a grid, and every vertex of that grid that
		 * is not on a classic corner finds no mark. A triangle whose three
		 * corners are all unmarked gets weight zero over its whole area, so
		 * the shimmer and the UV wobble would reach a sliver of the surface
		 * and the rest of the pool would stand still.
		 *
		 * Measured over ten level loads: the classic side offers 0 to 564
		 * marked vertices per level (0 in RICH1, RICHCUT2 and TITLE, 12 in
		 * JOBY4, 31 in RICH2), of which the HD side stamps 0 to 809 - and
		 * `choppy`, the roomlet's own nWVtx water set, is zero in every level
		 * of the game. TR5 does not displace water vertices at all; what moves
		 * its water is the animated texture range.
		 *
		 * The rule. The classic answer for the surface bit is a decision about
		 * a plane: aRoomletTransformLight (drawroom.cpp) opens the whole branch with
		 * `pos.y == CurrentRoomPtr->minfloor`, and the portal walk under it
		 * asks whether that square of that plane has water below. So the
		 * answer for an HD vertex the classic mesh never had is the answer of
		 * the piece of plane it lies in, and this floods it there: per room
		 * and per height, the axis aligned box of the marked corners, and
		 * every HD vertex of that room at that height inside that box gets the
		 * bit. A box and not the whole plane, so that a walkway at the water's
		 * own height on the far side of the room stays dry; per height, so
		 * that two pools at two levels in one room stay apart.
		 *
		 * Both shimmer bits are flooded. Which bit the classic answer comes
		 * out on depends on the level:
		 *
		 *     level     stamped   surface bit   whole-room bit
		 *     JOBY4          32            32                0
		 *     RICH2          47            47                0
		 *     ANDY1         218           218                0
		 *     ANDREA1       267           145              122
		 *     ANDREA3       291             0              291
		 *     ANDREA2       809            69              740
		 *
		 * In the Rome levels the water is a ROOM_REFLECT + ROOM_REFLECT_CEILING
		 * room and the classic answer for it comes out on the whole-room bit;
		 * flooding the surface bit alone would leave the Rome water with the
		 * sliver of marks the classic corners give it, standing still.
		 *
		 * The classic predicate behind the whole-room bit (drawroom.cpp,
		 * aRoomletTransformLight)
		 * is `!IsMistVert(pos) || |y - minfloor| > 1536`, and IsMistVert asks
		 * whether the vertex is shared with the room above through the ceiling
		 * portal - a question about the classic mesh's topology, which an HD
		 * vertex the classic mesh never had cannot answer for itself. What can
		 * be carried is the same thing the surface bit carries: the answer of
		 * the piece of plane the vertex lies in. So the whole-room bit gets
		 * its own box per room and height, filled and read exactly like the
		 * surface bit's, and a vertex is marked only at a height and inside an
		 * extent the classic mesh already marked. Bit 0, the roomlet's own
		 * nWVtx set, is empty in all of TR5 and has no box. */
		for (int i = 0; i < g_mark_count; i++)
		{
			const HDWaterMark& m = g_marks[i];

			for (int bit = 0; bit < 2; bit++)
			{
				if (!(m.bits & (bit ? 4 : 2)))
					continue;

				const unsigned int key =
					((unsigned int)(unsigned short)m.room << 16) |
					(unsigned int)(unsigned short)m.y;
				std::unordered_map<unsigned int, HDMarkPlane>& map =
					bit ? room_plane_map : plane_map;
				std::unordered_map<unsigned int, HDMarkPlane>::iterator at =
					map.find(key);

				if (at == map.end())
				{
					HDMarkPlane plane;

					plane.minX = plane.maxX = m.x;
					plane.minZ = plane.maxZ = m.z;
					map[key] = plane;
				}
				else
				{
					if (m.x < at->second.minX) at->second.minX = m.x;
					if (m.x > at->second.maxX) at->second.maxX = m.x;
					if (m.z < at->second.minZ) at->second.minZ = m.z;
					if (m.z > at->second.maxZ) at->second.maxZ = m.z;
				}
			}
		}
	}

	/* --- the wind belongs to materials 1, 7 and 8 ----------------------
	 *
	 * TrgVertex::sway is a weight and nothing else - it says how far a vertex
	 * may travel, not that it travels at all. Read as permission, it makes
	 * lamp posts and a long list of statics and animatings sway.
	 *
	 * Measured over all 15 levels, vertices with a non-zero sway, by the
	 * material of the part that names them:
	 *
	 *     material  8   416,153     material  0   160,620
	 *     material  1    92,896     material 14     2,016
	 *     material  7    46,611     material  3       867
	 *                               material 16       594
	 *                               material  6       222
	 *
	 * The left column moves and the right column must not, and the parts
	 * themselves say which is which:
	 *
	 *   8 and 7 are the wind materials, the same thing under two numbers -
	 *     param[0] 0.5 in both (the only slot read; the ranges of the other
	 *     three are in the material list at the top of this file), the cut-out
	 *     sheets of leaves, and no level carries both (ANDREA3 has only 7,
	 *     JOBY4 only 8; TITLE, a montage of every level, has both). Ivy,
	 *     banners, grass, kelp.
	 *   1 is the flow material, the UV scroll of a river or a waterfall, and
	 *     its bend is the water's own - JOBY5's sub pens are 63,198 of those
	 *     92,896. Taking it away would leave water without its animation.
	 *   0, 3, 6, 14 and 16 are ordinary geometry, glass, glow and light
	 *     shafts. In ANDREA1 material 0 alone is 61,208 of the level's 84,408
	 *     swaying vertices - four fifths of them, lamp posts included.
	 *
	 * The weight is therefore taken away from every vertex that no part of
	 * material 1, 7 or 8 names, once, here, before the vertex buffer is
	 * filled - so the shader keeps its one multiplication, the log's counters
	 * and HDRenderGroupMotionInfo report what really moves, and nothing
	 * downstream has to know about materials.
	 *
	 * A vertex named by both a moving part and a still one keeps its weight:
	 * it is part of the moving mesh, and the alternative would be to split
	 * it. */
	int wind_parts = 0;
	int wind_vertices = 0;
	int sway_dropped = 0;

	{
		std::vector<unsigned char> wind_ok(trg.vertices.size(), 0);

		for (const trf::remaster::TrgGroup& group : trg.groups)
			for (const trf::remaster::TrgPart& part : group.parts)
			{
				if (part.material != HD_TRG_MATERIAL_WIND &&
					part.material != HD_TRG_MATERIAL_WIND2 &&
					part.material != HD_TRG_MATERIAL_FLOW)
					continue;

				wind_parts++;

				for (int p = 0; p < 3; p++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[p];

					for (unsigned int at = run.start;
						at < run.start + run.count && at < trg.indices.size(); at++)
					{
						const unsigned int v = trg.indices[at];

						if (v < wind_ok.size() && !wind_ok[v])
						{
							wind_ok[v] = 1;
							wind_vertices++;
						}
					}
				}
			}

		for (size_t i = 0; i < trg.vertices.size(); i++)
			if (trg.vertices[i].sway && !wind_ok[i])
			{
				trg.vertices[i].sway = 0;
				sway_dropped++;
			}
	}

	int marked_vertices = 0;
	int marked_choppy = 0;
	// Of the shimmer marks, the ones the plane flood added.
	int flooded_vertices = 0;
	int marked_wall = 0;		// see the note beside it below
	// Flood planes a group could not keep - see the note beside the shore
	// box below.
	int water_multi_planes = 0;
	int sway_vertices = 0;
	g_scroll_parts = 0;
	g_cutout_parts = 0;
	g_addweight_parts = 0;
	g_addswing_parts = 0;
	g_addweight_min = 1.0f;
	g_addstill_parts = 0;
	g_stacked_parts = 0;
	g_stacked_tris = 0;
	g_flat_parts = 0;
	g_gloss_parts = 0;
	g_flat_planes = 0;
	g_blend_parts = 0;
	g_blend_ranges = 0;
	g_clear_parts = 0;
	g_clear_pages = 0;
	g_clear_ranges = 0;
	g_clear_seen.assign((size_t)HDPageCacheSlots(), 0);
	g_water_parts = 0;
	g_water_opaque = 0;
	g_water_additive = 0;
	g_water_tris = 0;
	g_water_subrect = 0;
	g_water_still = 0;
	g_water_span = 255;
	g_water_cap = 0.0f;
	g_destruct.clear();
	g_flip_parts = 0;
	g_flip_dropped = 0;
	g_flip_ranges = 0;
	g_book_slots.clear();
	g_book_runs.clear();
	g_book_parts.clear();
	g_book_ranges = 0;
	g_book_watch = 0;
	g_book_watch_group = -1;
	g_book_watch_part = -1;
	g_book_watch_page = 0;
	int marked_shimmer = 0;
	int marked_room_shimmer = 0;

	/* --- the vertex buffer -------------------------------------------- */
	if (FAILED(g_device->CreateVertexBuffer((UINT)(g_vertex_count * (int)sizeof(HDVertex)),
		D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &g_vb, 0)))
	{
		*why = "CreateVertexBuffer failed";
		release_buffers();
		return false;
	}

	{
		void* raw = 0;

		if (FAILED(g_vb->Lock(0, 0, &raw, 0)))
		{
			*why = "locking the vertex buffer failed";
			release_buffers();
			return false;
		}

		HDVertex* out = (HDVertex*)raw;

		for (int i = 0; i < g_vertex_count; i++)
		{
			const trf::remaster::TrgVertex& in = trg.vertices[(size_t)i];

			out[i].x = in.x;
			out[i].y = in.y;
			out[i].z = in.z;
			out[i].sway = in.sway;

			// The shader reads the weight; the count is for the log and for
			// the tool, and it is the whole level in one number.
			if (in.sway)
				sway_vertices++;

			out[i].nx = in.nx;
			out[i].ny = in.ny;
			out[i].nz = in.nz;
			out[i].layer = in.layer;
			out[i].c1b = in.colour1[2];
			out[i].c1g = in.colour1[1];
			out[i].c1r = in.colour1[0];
			out[i].u = in.u;
			out[i].c2b = in.colour2[2];
			out[i].c2g = in.colour2[1];
			out[i].c2r = in.colour2[0];
			out[i].v = in.v;

			unsigned char bits = 0;

			if (!vertex_group.empty() && vertex_group[(size_t)i] >= 0)
			{
				const unsigned long long key =
					((unsigned long long)(unsigned short)vertex_group[(size_t)i] << 48) |
					((unsigned long long)(unsigned short)in.x << 32) |
					((unsigned long long)(unsigned short)in.y << 16) |
					(unsigned long long)(unsigned short)in.z;

				std::unordered_map<unsigned long long, unsigned char>::const_iterator
					found = mark_map.find(key);

				if (found != mark_map.end())
					bits = found->second;

				/* If the corner search found nothing, the plane the vertex
				 * lies in answers for it - see the note beside plane_map -
				 * for both shimmer bits, each out of its own map. Counted, so
				 * that the log says how much of a level's water the classic
				 * corners were missing. */
				const unsigned int plane_key =
					((unsigned int)(unsigned short)vertex_group[(size_t)i] << 16) |
					(unsigned int)(unsigned short)in.y;

				for (int bit = 0; bit < 2; bit++)
				{
					const unsigned char want = bit ? 4 : 2;

					if (bits & want)
						continue;

					const std::unordered_map<unsigned int, HDMarkPlane>& map =
						bit ? room_plane_map : plane_map;
					std::unordered_map<unsigned int, HDMarkPlane>::const_iterator
						plane = map.find(plane_key);

					if (plane != map.end() &&
						in.x >= plane->second.minX && in.x <= plane->second.maxX &&
						in.z >= plane->second.minZ && in.z <= plane->second.maxZ)
					{
						bits |= want;
						flooded_vertices++;
					}
				}
			}

			/* Of the marked set, the ones that are not a horizontal
			 * surface. The mark is carried by position - the classic corner
			 * match and the plane flood both - and a wall vertex standing at
			 * exactly the water's height inside the pool's box answers yes to
			 * both. With the surface's UV wobble and caustic, a wall triangle
			 * whose other two corners are lower becomes a sheared, darkened
			 * black wedge at the waterline.
			 *
			 * The vertex's own normal tells the two apart - a water surface
			 * is flat and its normal is vertical - and the shader takes that
			 * road, because the colour term must keep the classic answer for
			 * exactly these vertices (aRoomletTransformLight asks the height and
			 * not the normal) and only the geometry half may drop them. This
			 * counts them, so the log says how much of a level it is. */
			if (bits & 6)
			{
				const int upright = (int)in.ny - 127;

				if (upright > -77 && upright < 77)
					marked_wall++;
			}

			out[i].wChoppy = (unsigned char)(bits & 1);
			out[i].wShimmer = (unsigned char)((bits >> 1) & 1);
			out[i].wRoomShimmer = (unsigned char)((bits >> 2) & 1);
			out[i].wSpare = 1;

			if (bits)
				marked_vertices++;

			marked_choppy += out[i].wChoppy;
			marked_shimmer += out[i].wShimmer;
			marked_room_shimmer += out[i].wRoomShimmer;
		}

		g_vb->Unlock();
	}

	/* --- the index buffer, sorted -------------------------------------
	 *
	 * The .TRG's index stream is not sorted by page: walking ANDREA1 as it
	 * lies costs 32,291 texture changes. Bucketing every group's triangles by
	 * page, stably, brings that down to 1,859 ranges - and a range is exactly
	 * one draw call. The sort is stable so that two triangles on the same
	 * page keep the order the file gave them, which keeps coplanar surfaces
	 * drawing in the artist's order.
	 *
	 * The page a triangle belongs to is its first corner's layer. No triangle
	 * in any of the 15 TR5 levels has three corners that disagree (measured),
	 * so this is a lookup, not a decision. */
	std::vector<unsigned int> sorted;
	sorted.reserve(trg.indices.size());

	g_groups.assign(trg.groups.size(), HDGroup());

	/* The glow parts - see BuildGlowParts. Before the build loop, because an
	 * asset reaches across groups. */
	std::vector<std::vector<int> > glow_of;

	BuildGlowParts(trg, glow_of);

	/* The shore of each room's water.
	 *
	 * The shader lifts every marked vertex by up to one and a half times
	 * HD_WATER_SWELL - 36 units upwards (y counts downwards in this engine).
	 * The vertices at the water's own edge are marked like the rest, so
	 * without a taper the surface rises over the bank it should meet: in the
	 * Rome cistern a dark travelling band would cover the lit coping.
	 *
	 * So the flood box is kept, per group, and the vertex shader fades the
	 * swell to nothing inside one square of its edge (hd_room.hlsl,
	 * HD_WATER_SHORE). The middle of the pool moves at full height.
	 *
	 * One entry per (room, height), out of both flood maps: a room can hold a
	 * pool at one height and a reflecting ceiling at another, and prepare()
	 * picks the one at the room's own surfaceY - which is the only plane the
	 * swell ever moves (hd_room.hlsl: the swell rides on `surfaceOn`, the
	 * marked set at the surface plane). A plane past the fourth is dropped
	 * and counted; over the fifteen files there is none. */
	{
		int dropped = 0;

		for (int bit = 0; bit < 2; bit++)
		{
			const std::unordered_map<unsigned int, HDMarkPlane>& map =
				bit ? room_plane_map : plane_map;

			for (std::unordered_map<unsigned int, HDMarkPlane>::const_iterator at =
				map.begin(); at != map.end(); ++at)
			{
				const size_t group = (size_t)(at->first >> 16);

				if (group >= g_groups.size())
					continue;

				HDGroup& g = g_groups[group];
				const float y = (float)(short)(at->first & 0xFFFF);
				int slot = -1;

				for (int s = 0; s < g.waterPlanes; s++)
					if (g.waterY[s] == y)
						slot = s;

				if (slot < 0)
				{
					if (g.waterPlanes >= HDGroup::kMaxPlanes)
					{
						dropped++;
						continue;
					}

					slot = g.waterPlanes++;
					g.waterY[slot] = y;
					g.waterBox[slot][0] = (float)at->second.minX;
					g.waterBox[slot][1] = (float)at->second.minZ;
					g.waterBox[slot][2] = (float)at->second.maxX;
					g.waterBox[slot][3] = (float)at->second.maxZ;
					continue;
				}

				// The two bits can name the same plane; the box is the union.
				if ((float)at->second.minX < g.waterBox[slot][0]) g.waterBox[slot][0] = (float)at->second.minX;
				if ((float)at->second.minZ < g.waterBox[slot][1]) g.waterBox[slot][1] = (float)at->second.minZ;
				if ((float)at->second.maxX > g.waterBox[slot][2]) g.waterBox[slot][2] = (float)at->second.maxX;
				if ((float)at->second.maxZ > g.waterBox[slot][3]) g.waterBox[slot][3] = (float)at->second.maxZ;
			}
		}

		water_multi_planes = dropped;
	}

	int split_ranges = 0;
	int biggest = 0;
	int fallback_tris = 0;
	const unsigned int max_tris = g_caps.MaxPrimitiveCount ? g_caps.MaxPrimitiveCount : 0xFFFFFFFFu;

	/* page slot, the part's scroll, first index - the sort key of a draw call
	 * and the triangle it names. */
	struct HDTri
	{
		int slot;
		float scrollU, scrollV;
		float alphaRef;			// see MaterialAlphaRef
		float addWeight;		// see MaterialAddWeight
		float addPeriod;		// see MaterialAddPeriod
		float flatLight;		// see PartIsUnlit
		float flipTime;			// see MaterialFlipTime
		int flipSlot[3];		// see MaterialFlipSlots
		float bookTime;			// see MaterialBookTime
		int bookFirst, bookCount, bookPhase;	// see MaterialBookSlots
		int bookPart;			// the .TRG part, for the log
		float gloss, glossPower;	// see MaterialGloss
		int glossMat;			// see MaterialGloss
		float ripple[3];		// see MaterialRipple
		float blend;			// see MaterialBlend
		int clear;			// see HDRange::clear
		int destructId;			// see HDDestructible
		int material;			// the additive pass's material, else 0
		int glow;			// see BuildGlowParts, else -1
		int overhang;			// see HDRoomBox
		int intruding;			// see HDIntrusion
		int stand;			// see HDStandRange
		int cull;			// see RangeCulls
		unsigned int first;
	};

	std::vector<HDTri> tris;
	std::vector<unsigned char> sway_seen(trg.vertices.size(), 0);

	/* --- the holders of stand art -------------------------------------
	 *
	 * Of the rooms handed to HDRenderSetStandFaces, the ones the engine will
	 * draw classic, as far as the .TRG says before the build: no group, or a
	 * group none of whose triangles has its centroid inside the room's box
	 * with HD_OVERHANG_SLACK - the empty and the hollow rule of hd_level.cpp
	 * asked of every triangle of the file. The build only ever leaves
	 * triangles out, so a room the engine draws HD is never among them; a
	 * marked room is never handed in. Each face's box, grown by
	 * HD_COVER_REACH, spares the overlap test for all but a few faces per
	 * triangle. */
	g_stand_tris.clear();
	g_stand_tri_holder.clear();
	g_stand_covers.clear();
	g_stand_cover_faces.clear();
	g_stand_kept = 0;
	g_stand_from_intrusions = 0;

	std::vector<int> stand_keep;
	std::vector<float> stand_face_box((size_t)g_stand_face_count * 6, 0.0f);

	for (int k = 0; k < g_stand_holder_count; k++)
	{
		const int r = g_stand_holders[k].room;
		bool inside = false;

		if (r < 0 || r >= g_stand_room_count)
			continue;

		if (r < (int)trg.groups.size())
		{
			const bool boxed = g_boxes && r < g_box_count;

			for (const trf::remaster::TrgPart& part : trg.groups[(size_t)r].parts)
				for (int pass = 0; pass < 3 && !inside; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned int at = 0; at + 2 < run.count && !inside; at += 3)
					{
						const unsigned int first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						if (!boxed)
						{
							inside = true;	// no box: only an empty group is classic
							break;
						}

						float centre[3] = { 0.0f, 0.0f, 0.0f };

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& tv =
								trg.vertices[trg.indices[first + c]];

							centre[0] += (float)tv.x;
							centre[1] += (float)tv.y;
							centre[2] += (float)tv.z;
						}

						bool in = true;

						for (int axis = 0; axis < 3; axis++)
						{
							const float p = centre[axis] / 3.0f;

							if (p < g_boxes[r].mins[axis] - HD_OVERHANG_SLACK ||
								p > g_boxes[r].maxs[axis] + HD_OVERHANG_SLACK)
								in = false;
						}

						inside = in;
					}
				}
		}

		if (inside)
			continue;

		stand_keep.push_back(k);

		for (int f = 0; f < g_stand_holders[k].faceCount; f++)
		{
			const int at = g_stand_holders[k].firstFace + f;

			if (at < 0 || at >= g_stand_face_count)
				continue;

			const HDCoverFace& face = g_stand_faces[at];
			float* box = &stand_face_box[(size_t)at * 6];

			for (int axis = 0; axis < 3; axis++)
			{
				double lo = face.v[0][axis], hi = face.v[0][axis];

				for (int c = 1; c < face.corners && c < 4; c++)
				{
					if (face.v[c][axis] < lo) lo = face.v[c][axis];
					if (face.v[c][axis] > hi) hi = face.v[c][axis];
				}

				box[axis] = (float)(lo - HD_COVER_REACH);
				box[axis + 3] = (float)(hi + HD_COVER_REACH);
			}
		}
	}

	g_stand_kept = (int)stand_keep.size();

	for (size_t g = 0; g < trg.groups.size(); g++)
	{
		HDGroup& group = g_groups[g];
		const trf::remaster::TrgGroup& source = trg.groups[g];
		const size_t group_first_index = sorted.size();

		/* The room's own box, if the caller handed one in.
		 * A sky group has no room and never gets one, so a sky group is one
		 * piece and is never windowed - which is right: the dome rides on
		 * the camera and belongs to no room's portal walk. */
		if (g_boxes && (int)g < g_box_count)
		{
			group.hasRoomBox = true;

			for (int axis = 0; axis < 3; axis++)
			{
				group.roomMins[axis] = g_boxes[g].mins[axis];
				group.roomMaxs[axis] = g_boxes[g].maxs[axis];
			}
		}

		/* Which rooms this group's art might be standing
		 * inside. Gathered once per group rather than looked up per triangle
		 * - the list holds a handful of entries per level and a level draws a
		 * million triangles. Empty for all but a few groups, and empty for
		 * every group of every level when no list was handed in, which is
		 * what the diagnostic tool does unless it is asked for the rule. */
		for (int k = 0; k < g_intrusion_count; k++)
			if (g_intrusions[k].group == (int)g)
				group.intrusionAt.push_back(k);

		/* And the holders of stand art this group's triangles are asked
		 * against: every one but its own room and its flipmap twin, which
		 * is the same place at another moment and never drawn with it. Empty
		 * for the sky and in every level whose classic rooms have no face. */
		std::vector<int> standAt;
		float stand_origin[3] = { 0.0f, 0.0f, 0.0f };

		if ((int)g < g_room_groups && (int)g < g_stand_room_count)
		{
			for (int k = 0; k < 3; k++)
				stand_origin[k] = g_stand_rooms[g].origin[k];

			for (size_t k = 0; k < stand_keep.size(); k++)
			{
				const int h = g_stand_holders[stand_keep[k]].room;

				if (h == (int)g || g_stand_rooms[h].twin == (int)g ||
					g_stand_rooms[g].twin == h)
					continue;

				standAt.push_back(stand_keep[k]);
			}
		}

		/* The stand tag of one triangle out of its three room-local corners.
		 * Does it stand on a face of a room drawn classic (HDCoverOverlaps)?
		 * The first holder of standAt whose face it stands on takes it:
		 * `stand_at`, else -1, with `cover` the triangle in world units. On no
		 * face: does its centroid stand in a holder's box all the same? That
		 * is the relief and the reveals of a room drawn as another group's art
		 * - a planter, the jambs of an arch - which the covered room needs in
		 * its window as much as its floor (HDStandDraw, kind 2): `box_at`,
		 * else -1. Asked by the pass over the group below and by the index
		 * fill alike, so the two tag alike. */
		const auto stand_tag = [&](const float v[3][3], int& stand_at, int& box_at,
			HDCoverTri& cover)
		{
			stand_at = -1;
			box_at = -1;

			if (standAt.empty())
				return;

			float lo[3], hi[3];

			for (int axis = 0; axis < 3; axis++)
			{
				lo[axis] = hi[axis] = v[0][axis] + stand_origin[axis];

				for (int c = 1; c < 3; c++)
				{
					const float p = v[c][axis] + stand_origin[axis];

					if (p < lo[axis]) lo[axis] = p;
					if (p > hi[axis]) hi[axis] = p;
				}
			}

			bool made = false, noArea = false;

			for (size_t k = 0; k < standAt.size() && stand_at < 0 && !noArea; k++)
			{
				const HDStandHolder& holder = g_stand_holders[standAt[k]];
				bool near_box = true;

				for (int axis = 0; axis < 3; axis++)
					if (hi[axis] < holder.mins[axis] || lo[axis] > holder.maxs[axis])
						near_box = false;

				if (!near_box)
					continue;

				for (int f = 0; f < holder.faceCount && stand_at < 0; f++)
				{
					const int at_face = holder.firstFace + f;

					if (at_face < 0 || at_face >= g_stand_face_count)
						continue;

					const float* fb = &stand_face_box[(size_t)at_face * 6];

					if (hi[0] < fb[0] || lo[0] > fb[3] ||
						hi[1] < fb[1] || lo[1] > fb[4] ||
						hi[2] < fb[2] || lo[2] > fb[5])
						continue;

					if (!made)
					{
						made = true;

						if (!HDCoverMakeTri(v, stand_origin, (int)g, &cover))
						{
							noArea = true;	// it stands on nothing
							break;
						}
					}

					if (HDCoverOverlaps(cover, g_stand_faces[at_face],
						HD_COVER_REACH))
						stand_at = standAt[k];
				}
			}

			for (size_t k = 0; k < standAt.size() && stand_at < 0 && box_at < 0; k++)
			{
				const HDStandHolder& holder = g_stand_holders[standAt[k]];
				bool in = true;

				for (int axis = 0; axis < 3; axis++)
				{
					const float c = (v[0][axis] + v[1][axis] + v[2][axis]) / 3.0f +
						stand_origin[axis];

					if (c < holder.mins[axis] || c > holder.maxs[axis])
						in = false;
				}

				if (in)
					box_at = standAt[k];
			}
		};

		// And the classic faces those triangles may belong to - only of a
		// group that has an entry at all.
		if (!group.intrusionAt.empty())
			for (int f = 0; f < g_pair_face_count; f++)
				if (g_pair_faces[f].group == (int)g)
					group.faceAt.push_back(f);

		/* And the openings that were cut into this group's
		 * room. Gathered the same way and for the same reason - a level
		 * carries a handful of them and draws a million triangles. Empty for
		 * every group of every vanilla level. */
		for (int k = 0; k < g_cut_count; k++)
			if (g_cuts[k].group == (int)g)
				group.cutAt.push_back(k);

		/* Which faces of each holder this group's own tagged art covers, before
		 * the ranges are made: one pass over the group's triangles with the
		 * index fill's own exclusions (a triangle in an opening, a higher copy
		 * of a stacked sea) and its tag, then HDCoverFaces per holder. Where
		 * the group covers some of a holder's faces and not all, the index fill
		 * below splits its ranges on that holder by the faces each triangle
		 * stands on, so that hd_level.cpp can leave the covered faces' classic
		 * twins out and keep the others (HD_STAND_FACES_*). Empty for every
		 * group without a holder to ask. */
		std::vector<int> stand_split(standAt.size(), 0);
		std::vector<size_t> stand_flags(standAt.size(), 0);

		if (!standAt.empty())
		{
			std::vector<std::vector<HDCoverTri> > on((size_t)standAt.size());

			for (int pass = 0; pass < 3; pass++)
				for (const trf::remaster::TrgPart& part : source.parts)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];
					std::vector<unsigned char> stacked;

					if (pass == 2 && MaterialBase(part) == HD_TRG_MATERIAL_WATER &&
						run.count && !g_keep_stacks)
						MarkStackedCopies(trg, run, stacked);

					for (unsigned int at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned int first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						float v[3][3];

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& tv =
								trg.vertices[trg.indices[first + c]];

							v[c][0] = (float)tv.x;
							v[c][1] = (float)tv.y;
							v[c][2] = (float)tv.z;
						}

						bool cut = false;

						for (size_t k = 0; k < group.cutAt.size() && !cut; k++)
						{
							const HDCut& box = g_cuts[(size_t)group.cutAt[k]];

							cut = tri_meets_box(v[0], v[1], v[2], box.mins, box.maxs);
						}

						if (cut || (!stacked.empty() && stacked[at / 3]))
							continue;

						int stand_at, box_at;
						HDCoverTri cover;

						stand_tag(v, stand_at, box_at, cover);

						for (size_t k = 0; k < standAt.size() && stand_at >= 0; k++)
							if (standAt[k] == stand_at)
							{
								on[k].push_back(cover);
								break;
							}
					}
				}

			for (size_t k = 0; k < standAt.size(); k++)
			{
				if (on[k].empty())
					continue;

				const HDStandHolder& holder = g_stand_holders[standAt[k]];

				if (holder.firstFace < 0 || holder.faceCount <= 0 ||
					holder.firstFace + holder.faceCount > g_stand_face_count)
					continue;

				HDCoverSet set;

				set.tris = on[k];
				set.build();

				const HDCoverCount count = HDCoverFaces(&g_stand_faces[holder.firstFace],
					holder.faceCount, set, HD_COVER_REACH, HD_COVER_STEP, 1);
				StandCoverBuilt made;

				made.cover.group = (int)g;
				made.cover.holder = holder.room;
				made.cover.tris = (int)on[k].size();
				made.cover.faces = count.faces;
				made.cover.facesCovered = count.facesCovered;
				made.cover.faceCount = holder.faceCount;
				made.cover.covered = 0;
				made.firstFlag = g_stand_cover_faces.size();

				for (int f = 0; f < holder.faceCount; f++)
					g_stand_cover_faces.push_back((unsigned char)(
						(size_t)f < count.faceShare.size() &&
						HDCoverFaceCovered(count.faceShare[(size_t)f]) ? 1 : 0));

				g_stand_covers.push_back(made);
				stand_flags[k] = made.firstFlag;
				stand_split[k] = HDCoverRoomState(count.faces, count.facesCovered) ==
					HD_STAND_ROOM_PART ? 1 : 0;
			}
		}

		/* (height, sector) of every horizontal triangle of this group,
		 * merged into the group's own table below. */
		std::vector<std::pair<int, unsigned int> > flats;
		/* And the same again out of the material 16 triangles alone - the
		 * remaster's own still water surfaces. The first list answers "did
		 * the remaster build anything in this plane at all" and is taken by
		 * majority; a sheet in the second list answers "the remaster built
		 * the water here" and needs no majority. See BuildWaterSurfaces. */
		std::vector<std::pair<int, unsigned int> > waterflats;
		/* The GLOSSLOG part counts of this group, added to the level's once
		 * its passes are built. */
		int group_parts[5] = { 0, 0, 0, 0, 0 };

		for (int pass = 0; pass < 3; pass++)
		{
			tris.clear();

			for (const trf::remaster::TrgPart& part : source.parts)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				/* Material 1 is the UV scroll; everything else draws with
				 * a still offset. */
				const bool scrolls = (part.material == HD_TRG_MATERIAL_FLOW) &&
					(part.param[0] != 0.0f || part.param[1] != 0.0f);
				const float su = scrolls ? part.param[0] : 0.0f;
				const float sv = scrolls ? part.param[1] : 0.0f;
				/* The cut-out materials only, and only where the test
				 * belongs - the alpha pass has its own reference and the
				 * additive pass has none. */
				const float aref = pass == 0 ? MaterialAlphaRef(part) : 0.0f;
				/* The page a material 15 part's book begins at: the page
				 * of the run's first triangle - see MaterialBookTime. -1
				 * for every other part, which never asks. */
				const int book_page0 = part.material == HD_TRG_MATERIAL_BOOK
					? RunFirstPage(trg, run) : -1;
				/* The weight and the swing belong to the additive pass
				 * and to nothing else - the same part's opaque and alpha
				 * runs are a surface and keep their picture to the bit. */
				const float aweight = pass == 2 ? MaterialAddWeight(part) : 1.0f;
				const float aperiod = pass == 2 ? MaterialAddPeriod(part) : 0.0f;

				if (pass == 0 && aref != 0.0f)
					g_cutout_parts++;

				if (pass == 2 && aweight != 1.0f)
				{
					g_addweight_parts++;

					if (aweight < g_addweight_min)
						g_addweight_min = aweight;
				}

				if (pass == 2 && aperiod != 0.0f)
					g_addswing_parts++;

				/* The water ripple. Material 16 in whatever pass the file
				 * sorts it into - 25 of its 28 parts are additive and 3 are
				 * opaque, and the file's own choice is kept - and three zeroes
				 * for everything else, which is every part of nine of the
				 * fifteen levels. */
				float ripple[3] = { 0.0f, 0.0f, 0.0f };
				const bool ripples = MaterialRipple(trg, part, ripple);
				/* Counted as a water surface whether or not it ripples: the
				 * one part of the game whose four numbers are all zero -
				 * JOBY5's group 115, the twin of JOBY3's puddle - is a still
				 * water surface and belongs in the count. */
				const bool water =
					MaterialBase(part) == HD_TRG_MATERIAL_WATER;

				/* A sea stacked in the file is drawn once - see
				 * MarkStackedCopies. The additive pass only: that is where
				 * the copies sum. */
				std::vector<unsigned char> stacked;

				if (pass == 2 && water && run.count && !g_keep_stacks &&
					MarkStackedCopies(trg, run, stacked))
					g_stacked_parts++;

				/* An additive draw call holds one material: material 2
				 * composes as screen and every other one as a sum (see
				 * draw_pass), and one material's glow can be drawn alone
				 * (HDRenderAdditiveOnly). Zero in the other two passes, whose
				 * sort is unchanged. */
				const int add_material = pass == 2 ? (int)MaterialBase(part) : 0;
				// The glow part, in the additive pass only.
				const size_t part_at = (size_t)(&part - &source.parts[0]);
				const int glow_id = pass == 2 && part_at < glow_of[g].size()
					? glow_of[g][part_at] : -1;
				// And its weight, with the glow shader's gain on it.
				const float draw_weight = glow_id >= 0
					? aweight * GlowGain(part) : aweight;

				if (pass == 2 && part.material == HD_TRG_MATERIAL_GLOW &&
					part.param[3] >= 0.05f && part.param[3] < kAddStillBelow &&
					aperiod == 0.0f)
					g_addstill_parts++;

				if (water && run.count)
				{
					float spanU = 0.0f, spanV = 0.0f;

					PartUvSpan(trg, part, spanU, spanV);

					const int span = (int)((spanU < spanV ? spanU : spanV) *
						255.0f + 0.5f);

					g_water_parts++;
					g_water_tris += (int)(run.count / 3);

					if (pass == 0)
						g_water_opaque++;
					else if (pass == 2)
						g_water_additive++;

					if (!ripples)
						g_water_still++;

					if (span < 255)
						g_water_subrect++;

					if (span < g_water_span)
						g_water_span = span;

					if (ripple[1] > g_water_cap)
						g_water_cap = ripple[1];
				}

				/* The sky band draws at light 1 - see PartIsUnlit. Only
				 * in a sky group, and the answer is the same for all three
				 * of the part's passes, so it is asked once. */
				const float flat = (int)g >= g_room_groups &&
					PartIsUnlit(trg, part) ? 1.0f : 0.0f;

				if (pass == 0 && flat != 0.0f)
					g_flat_parts++;

				/* The flipbook. The three further pages and the frame time
				 * of a material 13 part, or (0, -1, -1, -1) for every other
				 * part in the game - see MaterialFlipTime above. A part
				 * whose numbers do not resolve keeps its own page and is
				 * counted once. */
				float ftime = MaterialFlipTime(part);
				int fslot[3] = { -1, -1, -1 };
				/* The gloss belongs to the two surface passes; an
				 * additive part paints light and has nothing to reflect. */
				float gloss = 0.0f;
				float gloss_power = 0.0f;
				int gloss_mat = 0;

				if (pass != 2)
					MaterialGloss(part, gloss, gloss_power, gloss_mat);

				if (pass == 0 && gloss != 0.0f)
					g_gloss_parts++;

				/* The see-through parts of the alpha pass - material 14,
				 * and nothing else in the game. */
				const float blend = MaterialBlend(part, pass);

				// A part can name a pass it puts no triangle in; counting
				// those would say a level has glass where it has none.
				if (blend != 0.0f && run.count)
					g_blend_parts++;

				// One-sided or two-sided - see RangeCulls.
				const int cull = RangeCulls(pass, aref, blend, water,
					(int)g >= g_room_groups);

				/* The destructible pieces - see HDDestructible above.
				 * One id per (group, material); the box is filled from the
				 * part's own vertices as they are sorted in below. */
				int destruct_id = -1;

				if (part.material & 0x8000u)
				{
					for (size_t at = 0; at < g_destruct.size(); at++)
						if (g_destruct[at].group == (int)g &&
							g_destruct[at].material == part.material)
						{
							destruct_id = (int)at;
							break;
						}

					if (destruct_id < 0)
					{
						HDDestructible piece;

						piece.group = (int)g;
						piece.material = part.material;
						destruct_id = (int)g_destruct.size();
						g_destruct.push_back(piece);
					}
				}

				if (ftime != 0.0f && !MaterialFlipSlots(trg, part, fslot))
					ftime = 0.0f;

				if (pass == 0 && part.material == HD_TRG_MATERIAL_FLIPBOOK)
				{
					group.motion.flipbookParts++;

					if (ftime != 0.0f)
						g_flip_parts++;
					else
						g_flip_dropped++;
				}

				/* The book. How long a page stands, where the part starts
				 * in the cycle, how many pages and where pages 1 to N - 1
				 * stand in g_book_slots - in every pass the file puts the
				 * part in, the opaque fan floors and monitors as much as the
				 * additive flames. A part whose numbers or pages do not all
				 * resolve keeps its own page and is counted as dropped; every
				 * part that is not material 15 has no book at all. Only the
				 * triangles on the book's own first page turn with it. */
				float btime = MaterialBookTime(part);
				const int bcount = btime != 0.0f ? (int)part.param[1] : 0;
				const int bphase = btime != 0.0f ? (int)part.param[0] : 0;
				const int bslot0 = book_page0 >= 0
					? HDPageCacheSlotOf(book_page0) : -1;
				int bfirst = -1;

				if (btime != 0.0f)
				{
					if (bslot0 >= 0)
						bfirst = MaterialBookSlots(book_page0, bcount);

					if (bfirst < 0)
						btime = 0.0f;
				}

				if (part.material == HD_TRG_MATERIAL_BOOK && run.count)
				{
					HDRenderBookPart book;

					book.group = (int)g;
					book.part = (int)part_at;
					book.pass = pass;
					book.page0 = book_page0;
					book.pages = btime != 0.0f ? bcount : 1;
					book.pageLast = book_page0 + book.pages - 1;
					book.step = btime;
					book.phase = bphase;
					book.weight = draw_weight;
					book.dropped = btime != 0.0f ? 0 : 1;
					g_book_parts.push_back(book);
				}

				if (pass == 0 && scrolls)
					g_scroll_parts++;

				// Whether this part has been counted as a clear floor.
				bool part_clear = false;

				/* Which sources name the pages of this part - the GLOSSLOG
				 * level line's part counts. Asked of the parts whose draw
				 * calls can take the sheen at all (RangeKeepsPlain: no
				 * additive part, no sky, no glass, no water surface), per
				 * triangle, because a part runs over several pages; a clear
				 * floor's page names nothing (ResolvePage). */
				const bool part_sheen = pass != 2 && (int)g < g_room_groups &&
					!water && flat == 0.0f && blend == 0.0f && run.count;
				unsigned int part_sources = 0;

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned int first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					/* Does this triangle stand in an opening? Asked first in
					 * the loop: a cut triangle never reaches the sort list,
					 * so it costs no index, no range, no horizontal-plane
					 * row and no overhang or intrusion count, and every
					 * number this build prints afterwards describes the
					 * geometry really in the buffers.
					 *
					 * For a level with no cut list - every vanilla level and
					 * every randomized level that opened no wall - the
					 * group's own list is empty and the test is one
					 * compare. */
					if (!group.cutAt.empty())
					{
						const trf::remaster::TrgVertex& ca =
							trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& cb =
							trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& cc =
							trg.vertices[trg.indices[first + 2]];
						const float pa[3] = { (float)ca.x, (float)ca.y, (float)ca.z };
						const float pb[3] = { (float)cb.x, (float)cb.y, (float)cb.z };
						const float pc[3] = { (float)cc.x, (float)cc.y, (float)cc.z };
						bool cut = false;

						for (size_t k = 0; k < group.cutAt.size(); k++)
						{
							const HDCut& box = g_cuts[(size_t)group.cutAt[k]];

							if (!tri_meets_box(pa, pb, pc, box.mins, box.maxs))
								continue;

							cut = true;

							if ((size_t)group.cutAt[k] < g_cut_hits.size())
								g_cut_hits[(size_t)group.cutAt[k]]++;
						}

						if (cut)
						{
							group.cutTris++;
							continue;
						}
					}

					const unsigned int corner = trg.indices[first];
					int slot = -1;

					if (corner < trg.vertices.size())
					{
						const unsigned char layer = trg.vertices[corner].layer;

						if (layer && (size_t)layer <= trg.textures.size())
							slot = HDPageCacheSlotOf((int)trg.textures[layer - 1]);
						else if (!trg.textures.empty())
						{
							/* Layer 0 means "no page named", and TROne's rule
							 * for it is the level's first page rather than
							 * nothing (game_renderer.cpp, hd_load). Dropping
							 * the triangle instead would leave a hole that
							 * looks exactly like a rendering bug, and the
							 * count below says how often it happens. */
							slot = HDPageCacheSlotOf((int)trg.textures[0]);
							fallback_tris++;
						}
					}

					/* Is this a horizontal triangle, and if so, which
					 * plane of the group does it widen? Asked here because
					 * this is the one loop that already has the triangle in
					 * its hands; the rows are merged into the group's own
					 * table further down. */
					{
						const trf::remaster::TrgVertex& a = trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& b = trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& c = trg.vertices[trg.indices[first + 2]];

						if (a.y == b.y && b.y == c.y)
						{
							const int x0 = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
							const int x1 = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
							const int z0 = a.z < b.z ? (a.z < c.z ? a.z : c.z) : (b.z < c.z ? b.z : c.z);
							const int z1 = a.z > b.z ? (a.z > c.z ? a.z : c.z) : (b.z > c.z ? b.z : c.z);

							/* Every sector the triangle's own box touches -
							 * the remaster remeshes a room, so one of its
							 * triangles can cross a classic sector line. */
							const int lastX = (x1 > x0 ? x1 - 1 : x0) >> 10;
							const int lastZ = (z1 > z0 ? z1 - 1 : z0) >> 10;

							const bool sheet =
								MaterialBase(part) == HD_TRG_MATERIAL_WATER;

							for (int cx = x0 >> 10; cx <= lastX; cx++)
								for (int cz = z0 >> 10; cz <= lastZ; cz++)
								{
									flats.push_back(std::make_pair((int)a.y,
										SectorKey(cx << 10, cz << 10)));

									if (sheet)
										waterflats.push_back(std::make_pair(
											(int)a.y, SectorKey(cx << 10, cz << 10)));
								}
						}
					}

					/* A higher copy of a stacked sea. Skipped after the
					 * horizontal-plane rows above and not before them: those
					 * rows answer whether the remaster built anything in a
					 * plane, and every copy is an answer to that. */
					if (!stacked.empty() && stacked[at / 3])
					{
						g_stacked_tris++;
						continue;
					}

					/* Inside the room's own box, or overhang? The centroid
					 * decides, with the slack over g_boxes. Counted per
					 * triangle; the hollow-room test - a group with nothing
					 * at all inside its own room - is the one thing asked of
					 * it.
					 *
					 * And the same centroid against the inside of every
					 * other room this group's art might be standing in.
					 * That answer is part of the sort key below,
					 * so that a draw call is all of one kind: the scissor
					 * test is on for the intruding triangles and off for
					 * everything else. */
					int overhang = 0;
					int intruding = 0;
					float centre[3] = { 0.0f, 0.0f, 0.0f };
					const bool need_centre =
						group.hasRoomBox || !group.intrusionAt.empty();

					if (need_centre)
					{
						const trf::remaster::TrgVertex& ta = trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& tb = trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& tc = trg.vertices[trg.indices[first + 2]];

						centre[0] = ((float)ta.x + (float)tb.x + (float)tc.x) / 3.0f;
						centre[1] = ((float)ta.y + (float)tb.y + (float)tc.y) / 3.0f;
						centre[2] = ((float)ta.z + (float)tb.z + (float)tc.z) / 3.0f;
					}

					if (group.hasRoomBox)
						for (int axis = 0; axis < 3; axis++)
							if (centre[axis] < group.roomMins[axis] - HD_OVERHANG_SLACK ||
								centre[axis] > group.roomMaxs[axis] + HD_OVERHANG_SLACK)
								overhang = 1;

					/* No slack here, and that is the difference between this
					 * test and the one above it. The overhang question is
					 * "did the artist put this face a brick's depth past the
					 * wall", and it needs a tolerance. This one is "is this
					 * triangle standing in a different room", and the box is
					 * that room's own inside, a sector in from its grid's
					 * edge - a tolerance on top of it would reach into the
					 * wall ring two rooms that share a wall both stand on.
					 *
					 * The first entry whose box holds the centroid is the
					 * triangle's pair; every entry that holds it counts it,
					 * which is what the level's log line says per pair. */
					int pair_at = -1;

					for (size_t k = 0; k < group.intrusionAt.size(); k++)
					{
						const HDIntrusion& box =
							g_intrusions[(size_t)group.intrusionAt[k]];
						bool in = true;

						for (int axis = 0; axis < 3; axis++)
							if (centre[axis] < box.mins[axis] ||
								centre[axis] > box.maxs[axis])
								in = false;

						if (in)
						{
							if (pair_at < 0)
								pair_at = group.intrusionAt[k];

							if ((size_t)group.intrusionAt[k] < g_intrusion_hits.size())
								g_intrusion_hits[(size_t)group.intrusionAt[k]]++;
						}
					}

					/* Does the triangle stand on a face of a room drawn classic
					 * (HDCoverOverlaps)? The first holder whose face it stands
					 * on takes it. Such a triangle leaves the open and the
					 * intruding half - a stand range is drawn on an answer of
					 * its own (hd_level.cpp, HDStandDraw) - and is kept in world
					 * units for the coverage test. The hits of the intrusion
					 * list above are counted all the same: they say what its
					 * boxes caught. */
					int stand_at = -1;
					int box_at = -1;
					HDCoverTri cover;

					if (!standAt.empty())
					{
						float v[3][3];

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& tv =
								trg.vertices[trg.indices[first + c]];

							v[c][0] = (float)tv.x;
							v[c][1] = (float)tv.y;
							v[c][2] = (float)tv.z;
						}

						stand_tag(v, stand_at, box_at, cover);
					}

					if (stand_at >= 0)
					{
						if (pair_at >= 0)
							g_stand_from_intrusions++;

						pair_at = -1;
					}

					/* The classic face the triangle belongs to - the nearest
					 * one that claims it, see HDPairFace - and the piece of
					 * (pair, face, smaller box) it is drawn with. The pieces
					 * are numbered in the order the build meets them. */
					int face_at = -1;
					int inner = 0;

					if (pair_at >= 0)
					{
						double nearest = 0.0;
						const HDIntrusion& box = g_intrusions[(size_t)pair_at];

						/* Whether the triangle also stands in the box one
						 * sector smaller than the inside, in x and z. That
						 * smaller box is the answer for the triangles no face
						 * claims below, and for every triangle of a room the
						 * walk gives an empty window whose holder is not on
						 * the frame's list (DecidePairs). */
						inner = centre[0] >= box.mins[0] + 1024.0f &&
							centre[0] <= box.maxs[0] - 1024.0f &&
							centre[2] >= box.mins[2] + 1024.0f &&
							centre[2] <= box.maxs[2] - 1024.0f ? 1 : 0;

						for (size_t k = 0; k < group.faceAt.size(); k++)
						{
							const int f = group.faceAt[k];
							const float* fb = &g_pair_face_box[(size_t)f * 6];

							if (centre[0] < fb[0] || centre[0] > fb[3] ||
								centre[1] < fb[1] || centre[1] > fb[4] ||
								centre[2] < fb[2] || centre[2] > fb[5])
								continue;

							const double d = HDPairFaceDistance(g_pair_faces[f], centre);

							if (d >= 0.0 && d <= HD_PAIR_FACE_REACH &&
								(face_at < 0 || d < nearest))
							{
								face_at = f;
								nearest = d;
							}
						}

						/* A triangle no classic face claims has nothing that
						 * could say "drawn whole" per frame, so it keeps the
						 * answer a box one sector smaller than the inside
						 * gives in x and z: windowed when it stands that far
						 * in, drawn with the open half otherwise. Windowed in
						 * the whole inside, the unclaimed art of a room that
						 * reaches into its holder's wall ring - a palm's
						 * fronds, a spiral stair, the wall under a
						 * balustrade - is cut at the window's rim on views
						 * that draw it whole with the smaller box. */
						if (face_at < 0 && !inner)
						{
							if ((size_t)pair_at < g_intrusion_band.size())
								g_intrusion_band[(size_t)pair_at]++;

							pair_at = -1;
						}
					}

					if (pair_at >= 0)
					{
						int piece = -1;

						for (size_t j = 0; j < group.pairRanges.size(); j++)
							if (group.pairRanges[j].intrusion == pair_at &&
								group.pairRanges[j].face == face_at &&
								group.pairRanges[j].inner == inner)
							{
								piece = (int)j;
								break;
							}

						if (piece < 0)
						{
							HDPairRange made;

							made.intrusion = pair_at;
							made.holder = g_intrusions[pair_at].holder;
							made.face = face_at;
							made.inner = inner;
							made.triangles = 0;
							piece = (int)group.pairRanges.size();
							group.pairRanges.push_back(made);
							group.pairWhole.push_back(0);
						}

						group.pairRanges[(size_t)piece].triangles++;
						intruding = piece + 1;
					}

					int stand = 0;

					// In a box only when no intrusion piece has it.
					if (stand_at < 0 && pair_at >= 0)
						box_at = -1;

					if (stand_at >= 0 || box_at >= 0)
					{
						/* Reaching onto the holder's faces (kind 0), lying on
						 * them whole (1) or standing in its box on no face (2):
						 * answered apart (HDStandDraw), so ranges of their own. */
						const HDStandHolder& at = g_stand_holders[stand_at >= 0 ? stand_at : box_at];
						const int holder = at.room;
						const int kind = stand_at < 0 ? 2 : HDCoverTriOnFaces(cover,
							&g_stand_faces[at.firstFace], at.faceCount,
							HD_COVER_REACH) ? 1 : 0;
						/* And on a holder this group covers in part, the faces
						 * the triangle stands on: covered ones, kept ones or
						 * both (HD_STAND_FACES_*), each answered apart. */
						int faces = 0;

						for (size_t k = 0; k < standAt.size() && stand_at >= 0; k++)
						{
							if (standAt[k] != stand_at || !stand_split[k])
								continue;

							for (int f = 0; f < at.faceCount; f++)
							{
								const int at_face = at.firstFace + f;
								const float* fb = &stand_face_box[(size_t)at_face * 6];

								if (cover.hi[0] < fb[0] || cover.lo[0] > fb[3] ||
									cover.hi[1] < fb[1] || cover.lo[1] > fb[4] ||
									cover.hi[2] < fb[2] || cover.lo[2] > fb[5] ||
									!HDCoverOverlaps(cover, g_stand_faces[at_face],
									HD_COVER_REACH))
									continue;

								faces |= g_stand_cover_faces[stand_flags[k] + (size_t)f]
									? HD_STAND_FACES_COVERED : HD_STAND_FACES_KEPT;
							}

							if (!faces)
								faces = HD_STAND_FACES_KEPT;

							break;
						}

						int piece = -1;

						for (size_t j = 0; j < group.standRanges.size(); j++)
							if (group.standRanges[j].holder == holder &&
								group.standRanges[j].kind == kind &&
								group.standRanges[j].faces == faces)
							{
								piece = (int)j;
								break;
							}

						if (piece < 0)
						{
							HDStandRange made;

							made.holder = holder;
							made.kind = kind;
							made.triangles = 0;
							made.faces = faces;
							piece = (int)group.standRanges.size();
							group.standRanges.push_back(made);
							group.standWith.push_back(HD_STAND_WITH);
						}

						group.standRanges[(size_t)piece].triangles++;
						stand = piece + 1;

						// The coverage test asks only the art on the faces.
						if (stand_at >= 0)
						{
							g_stand_tris.push_back(cover);
							g_stand_tri_holder.push_back(holder);
						}
					}

					HDTri tri;

					tri.overhang = overhang;
					tri.intruding = intruding;
					tri.stand = stand;
					tri.cull = cull;
					tri.slot = slot;
					tri.scrollU = su;
					tri.scrollV = sv;
					tri.alphaRef = aref;
					tri.addWeight = draw_weight;
					tri.addPeriod = aperiod;
					tri.flatLight = flat;
					tri.flipTime = ftime;
					tri.flipSlot[0] = fslot[0];
					tri.flipSlot[1] = fslot[1];
					tri.flipSlot[2] = fslot[2];
					{
						const bool turns = btime != 0.0f && slot == bslot0;

						tri.bookTime = turns ? btime : 0.0f;
						tri.bookFirst = turns ? bfirst : -1;
						tri.bookCount = turns ? bcount : 0;
						tri.bookPhase = turns ? bphase : 0;
						tri.bookPart = turns ? (int)part_at : -1;
					}
					tri.gloss = gloss;
					tri.glossPower = gloss_power;
					tri.glossMat = gloss_mat;
					tri.ripple[0] = ripple[0];
					tri.ripple[1] = ripple[1];
					tri.ripple[2] = ripple[2];

					/* A clear floor: a triangle of a room's alpha pass that is
					 * not glass already and whose page is part-way clear in
					 * every texel (hd_pageclass.h). Asked per triangle and not
					 * per part, because the page is the triangle's own - a
					 * RICH2 floor part runs over pages 1047 and 1045 alike,
					 * and a part that crossed onto an ordinary page would keep
					 * the alpha test there. The sky is left out: its alpha pass
					 * blends on a rule of its own (draw_pass, `sky`). Blend 1
					 * is the page's own alpha and nothing on top. */
					const bool clear_tri = pass == 1 && blend == 0.0f &&
						!g_no_clear && (int)g < g_room_groups &&
						HDPageCacheSlotClear(slot);

					if (clear_tri)
					{
						if (!part_clear)
						{
							part_clear = true;
							g_clear_parts++;
						}

						if ((size_t)slot < g_clear_seen.size() &&
							!g_clear_seen[(size_t)slot])
						{
							g_clear_seen[(size_t)slot] = 1;
							g_clear_pages++;
						}
					}

					tri.blend = clear_tri ? 1.0f : blend;
					tri.clear = clear_tri ? 1 : 0;
					tri.destructId = destruct_id;
					tri.material = add_material;
					tri.glow = glow_id;
					tri.first = first;
					tris.push_back(tri);

					if (part_sheen && gloss == 0.0f)
					{
						const HDGloss named = PageGloss(HDPageCachePageOfSlot(slot));

						if (named.strength > 0.0f)
							part_sources |= 1u << named.source;
					}
				}

				if (part_sheen && gloss > 0.0f)
					group_parts[HD_GLOSS_FROM_MAT]++;

				for (int source = HD_GLOSS_FROM_SEED; source <= HD_GLOSS_FROM_FILE; source++)
					if (part_sources & (1u << source))
						group_parts[source]++;
			}

			if (tris.empty())
				continue;

			std::stable_sort(tris.begin(), tris.end(),
				[](const HDTri& a, const HDTri& b)
				{
					/* The unlit band of the sky before the cloud layers
					 * that share its group - the sky is drawn back to front
					 * and this is the only place the order can be set. Zero
					 * for every room range, so a room's sort is unchanged. */
					if (a.flatLight != b.flatLight) return a.flatLight > b.flatLight;
					if (a.slot != b.slot) return a.slot < b.slot;
					if (a.scrollU != b.scrollU) return a.scrollU < b.scrollU;
					if (a.scrollV != b.scrollV) return a.scrollV < b.scrollV;
					if (a.alphaRef != b.alphaRef) return a.alphaRef < b.alphaRef;
					if (a.addWeight != b.addWeight) return a.addWeight < b.addWeight;
					if (a.addPeriod != b.addPeriod) return a.addPeriod < b.addPeriod;
					// The flipbook is one SetTexture per frame, so it
					// belongs in the key beside the page it starts on.
					if (a.flipTime != b.flipTime) return a.flipTime < b.flipTime;
					if (a.flipSlot[0] != b.flipSlot[0]) return a.flipSlot[0] < b.flipSlot[0];
					if (a.flipSlot[1] != b.flipSlot[1]) return a.flipSlot[1] < b.flipSlot[1];
					if (a.flipSlot[2] != b.flipSlot[2]) return a.flipSlot[2] < b.flipSlot[2];
					/* And a book is one SetTexture per frame as well: one
					 * draw call shows one page, so the book, its step and
					 * its phase all belong in the key. Zero and -1 on every
					 * part that is not material 15, so nothing else moves. */
					if (a.bookTime != b.bookTime) return a.bookTime < b.bookTime;
					if (a.bookFirst != b.bookFirst) return a.bookFirst < b.bookFirst;
					if (a.bookCount != b.bookCount) return a.bookCount < b.bookCount;
					if (a.bookPhase != b.bookPhase) return a.bookPhase < b.bookPhase;
					// The gloss is one more pixel constant per draw call.
					if (a.gloss != b.gloss) return a.gloss < b.gloss;
					if (a.glossPower != b.glossPower) return a.glossPower < b.glossPower;
					/* And the material the gloss came from, whose class the
					 * sheen takes. 0 on every part without a gloss, and 3 and
					 * 17 never share a level, so nothing else moves. */
					if (a.glossMat != b.glossMat) return a.glossMat < b.glossMat;
					/* And the water ripple is one more pixel constant per
					 * draw call - three zeroes on every part of the game that
					 * is not a water surface, so nothing else moves in the
					 * key. */
					if (a.ripple[0] != b.ripple[0]) return a.ripple[0] < b.ripple[0];
					if (a.ripple[1] != b.ripple[1]) return a.ripple[1] < b.ripple[1];
					if (a.ripple[2] != b.ripple[2]) return a.ripple[2] < b.ripple[2];
					/* The solid part of the alpha pass first, the
					 * see-through part after it - they are drawn in two
					 * different phases of the frame. */
					if (a.blend != b.blend) return a.blend < b.blend;
					// A clear floor and a pane of glass are drawn in two
					// different phases too.
					if (a.clear != b.clear) return a.clear < b.clear;
					// A destructible piece keeps its own draw calls.
					if (a.destructId != b.destructId) return a.destructId < b.destructId;
					// One material per additive draw call - see add_material.
					if (a.material != b.material) return a.material < b.material;
					/* One glow part per draw call. The parts are numbered in
					 * the file's order, so the triangles keep that order. */
					if (a.glow != b.glow) return a.glow < b.glow;
					/* One cull mode per draw call. Within pass 0 it follows
					 * the alpha reference and the ripple, which are in the
					 * key already; only a still water sheet on a wall's own
					 * page can come out as a draw call of its own. */
					if (a.cull != b.cull) return a.cull < b.cull;
					/* And the art that stands inside another room never
					 * shares a draw call with the art that does not, nor one
					 * piece of it with another - each piece is drawn whole or
					 * inside the window on an answer of its own. Last
					 * in the key, so a level built with no intrusion list -
					 * every triangle answering 0 - sorts on the rest alone. */
					if (g_split_overhang && a.overhang != b.overhang)
						return a.overhang < b.overhang;

					if (a.intruding != b.intruding)
						return a.intruding < b.intruding;

					/* And a stand range never shares a draw call with
					 * anything else. After everything, so a group without
					 * one - every triangle answering 0 - sorts as before. */
					return a.stand < b.stand;
				});

			group.triangles[pass] = (unsigned int)tris.size();

			// And how many of them stand outside the room's own box, and
			// how many stand inside another room altogether.
			for (size_t i = 0; i < tris.size(); i++)
			{
				if (tris[i].overhang)
					group.overhang[pass]++;

				if (tris[i].intruding)
					group.intruding[pass]++;
			}

			HDRange range;
			range.pageSlot = tris[0].slot;
			range.scrollU = tris[0].scrollU;
			range.scrollV = tris[0].scrollV;
			range.alphaRef = tris[0].alphaRef;
			range.addWeight = tris[0].addWeight;
			range.addPeriod = tris[0].addPeriod;
			range.flatLight = tris[0].flatLight;
			range.flipTime = tris[0].flipTime;
			range.flipSlot[0] = tris[0].flipSlot[0];
			range.flipSlot[1] = tris[0].flipSlot[1];
			range.flipSlot[2] = tris[0].flipSlot[2];
			range.bookTime = tris[0].bookTime;
			range.bookFirst = tris[0].bookFirst;
			range.bookCount = tris[0].bookCount;
			range.bookPhase = tris[0].bookPhase;
			range.bookPart = tris[0].bookPart;
			range.gloss = tris[0].gloss;
			range.glossPower = tris[0].glossPower;
			range.glossMat = tris[0].glossMat;
			range.ripple[0] = tris[0].ripple[0];
			range.ripple[1] = tris[0].ripple[1];
			range.ripple[2] = tris[0].ripple[2];
			range.blend = tris[0].blend;
			range.clear = tris[0].clear;
			range.destructId = tris[0].destructId;
			range.material = tris[0].material;
			range.glow = tris[0].glow;
			range.intruding = tris[0].intruding;
			range.stand = tris[0].stand;
			range.overhang = tris[0].overhang;
			range.cull = tris[0].cull;
			range.start = (unsigned int)sorted.size();
			range.count = 0;

			for (size_t i = 0; i < tris.size(); i++)
			{
				const bool same = tris[i].slot == range.pageSlot &&
					tris[i].scrollU == range.scrollU &&
					tris[i].scrollV == range.scrollV &&
					tris[i].alphaRef == range.alphaRef &&
					tris[i].addWeight == range.addWeight &&
					tris[i].addPeriod == range.addPeriod &&
					tris[i].flatLight == range.flatLight &&
					tris[i].flipTime == range.flipTime &&
					tris[i].flipSlot[0] == range.flipSlot[0] &&
					tris[i].flipSlot[1] == range.flipSlot[1] &&
					tris[i].flipSlot[2] == range.flipSlot[2] &&
					tris[i].bookTime == range.bookTime &&
					tris[i].bookFirst == range.bookFirst &&
					tris[i].bookCount == range.bookCount &&
					tris[i].bookPhase == range.bookPhase &&
					tris[i].gloss == range.gloss &&
					tris[i].glossPower == range.glossPower &&
					tris[i].glossMat == range.glossMat &&
					tris[i].ripple[0] == range.ripple[0] &&
					tris[i].ripple[1] == range.ripple[1] &&
					tris[i].ripple[2] == range.ripple[2] &&
					tris[i].blend == range.blend &&
					tris[i].clear == range.clear &&
					tris[i].destructId == range.destructId &&
					tris[i].material == range.material &&
					tris[i].glow == range.glow &&
					tris[i].intruding == range.intruding &&
					tris[i].stand == range.stand &&
					tris[i].cull == range.cull &&
					(!g_split_overhang ||
						tris[i].overhang == range.overhang);

				if (!same || range.count / 3 >= max_tris)
				{
					if (range.count)
					{
						if (same)
							split_ranges++;

						group.pass[pass].push_back(range);

						if (range.flipTime != 0.0f)
							g_flip_ranges++;

						if (range.bookTime != 0.0f)
							g_book_ranges++;

						if (range.blend != 0.0f && !range.clear)
							g_blend_ranges++;

						if (range.clear)
						{
							g_clear_ranges++;
							group.clearRanges++;
						}

						if (range.scrollU != 0.0f || range.scrollV != 0.0f)
						{
							group.motion.scrollRanges++;
							group.motion.scrollTris += (int)(range.count / 3);

							if (fabsf(range.scrollU) + fabsf(range.scrollV) >
								fabsf(group.motion.scrollU) + fabsf(group.motion.scrollV))
							{
								group.motion.scrollU = range.scrollU;
								group.motion.scrollV = range.scrollV;
							}
						}

						if ((int)(range.count / 3) > biggest)
							biggest = (int)(range.count / 3);
					}

					range.pageSlot = tris[i].slot;
					range.scrollU = tris[i].scrollU;
					range.scrollV = tris[i].scrollV;
					range.alphaRef = tris[i].alphaRef;
					range.addWeight = tris[i].addWeight;
					range.addPeriod = tris[i].addPeriod;
					range.flatLight = tris[i].flatLight;
					range.flipTime = tris[i].flipTime;
					range.flipSlot[0] = tris[i].flipSlot[0];
					range.flipSlot[1] = tris[i].flipSlot[1];
					range.flipSlot[2] = tris[i].flipSlot[2];
					range.bookTime = tris[i].bookTime;
					range.bookFirst = tris[i].bookFirst;
					range.bookCount = tris[i].bookCount;
					range.bookPhase = tris[i].bookPhase;
					range.bookPart = tris[i].bookPart;
					range.gloss = tris[i].gloss;
					range.glossPower = tris[i].glossPower;
					range.glossMat = tris[i].glossMat;
					range.ripple[0] = tris[i].ripple[0];
					range.ripple[1] = tris[i].ripple[1];
					range.ripple[2] = tris[i].ripple[2];
					range.blend = tris[i].blend;
					range.clear = tris[i].clear;
					range.destructId = tris[i].destructId;
					range.material = tris[i].material;
					range.glow = tris[i].glow;
					range.intruding = tris[i].intruding;
					range.stand = tris[i].stand;
					range.overhang = tris[i].overhang;
					range.cull = tris[i].cull;
					range.start = (unsigned int)sorted.size();
					range.count = 0;
				}

				const unsigned int first = tris[i].first;

				/* The art a static's piece may take: the overhang of no
				 * intrusion piece and the stand ranges, the parts of a group
				 * HDRenderStaticArt asks. The range is the one being filled,
				 * pushed at this index. */
				if (g_place_count && (tris[i].stand > 0 ||
					(tris[i].overhang && !tris[i].intruding)))
					NotePlaceTri(trg, (int)g, pass, (int)group.pass[pass].size(),
						tris[i].stand, (unsigned int)sorted.size(), first);

				for (int corner = 0; corner < 3; corner++)
				{
					const unsigned int index = trg.indices[first + (unsigned int)corner];
					sorted.push_back(index);

					if (index < trg.vertices.size())
					{
						const trf::remaster::TrgVertex& v = trg.vertices[index];
						const float p[3] = { (float)v.x, (float)v.y, (float)v.z };

						/* Per room: which of this room's vertices
						 * carry a wind weight and how strong the strongest
						 * is. Counted here because this is the one place a
						 * vertex and its group are both in hand, and counted
						 * once per vertex rather than once per triangle -
						 * hence the seen flag. */
						if (v.sway && index < sway_seen.size() && !sway_seen[index])
						{
							const int weight = v.sway < 0 ? -(int)v.sway : (int)v.sway;

							sway_seen[index] = 1;
							group.motion.swayVertices++;

							if (weight > group.motion.swayMax)
								group.motion.swayMax = weight;
						}

						/* The group's occupancy, and the box of the
						 * destructible piece these triangles belong to. Both
						 * ride on a walk that was already happening.
						 *
						 * And the same list for the inside half alone - the
						 * overlap list is asked of that one. */
						group.cells.push_back(CellKey(v.x, v.y, v.z));

						if (!tris[i].overhang)
							group.cellsIn.push_back(CellKey(v.x, v.y, v.z));

						if (tris[i].stand > 0)
						{
							const size_t at = (size_t)tris[i].stand - 1;

							if (group.standCells.size() <= at)
								group.standCells.resize(at + 1);

							group.standCells[at].push_back(CellKey(v.x, v.y, v.z));
						}
						else if (!tris[i].intruding && tris[i].overhang)
							group.cellsFree.push_back(CellKey(v.x, v.y, v.z));

						if (tris[i].destructId >= 0)
						{
							HDDestructible& piece = g_destruct[(size_t)tris[i].destructId];

							if (!piece.hasBox)
							{
								piece.hasBox = true;
								piece.mins[0] = piece.maxs[0] = p[0];
								piece.mins[1] = piece.maxs[1] = p[1];
								piece.mins[2] = piece.maxs[2] = p[2];
							}
							else
								for (int axis = 0; axis < 3; axis++)
								{
									if (p[axis] < piece.mins[axis]) piece.mins[axis] = p[axis];
									if (p[axis] > piece.maxs[axis]) piece.maxs[axis] = p[axis];
								}
						}

						if (!group.hasBounds)
						{
							group.hasBounds = true;
							group.mins[0] = group.maxs[0] = p[0];
							group.mins[1] = group.maxs[1] = p[1];
							group.mins[2] = group.maxs[2] = p[2];
						}
						else
							for (int axis = 0; axis < 3; axis++)
							{
								if (p[axis] < group.mins[axis]) group.mins[axis] = p[axis];
								if (p[axis] > group.maxs[axis]) group.maxs[axis] = p[axis];
							}
					}
				}

				range.count += 3;
			}

			if (range.count)
			{
				group.pass[pass].push_back(range);

				if (range.flipTime != 0.0f)
					g_flip_ranges++;

				if (range.bookTime != 0.0f)
					g_book_ranges++;

				if (range.blend != 0.0f && !range.clear)
					g_blend_ranges++;

				if (range.clear)
				{
					g_clear_ranges++;
					group.clearRanges++;
				}

				if (range.scrollU != 0.0f || range.scrollV != 0.0f)
				{
					group.motion.scrollRanges++;
					group.motion.scrollTris += (int)(range.count / 3);

					if (fabsf(range.scrollU) + fabsf(range.scrollV) >
						fabsf(group.motion.scrollU) + fabsf(group.motion.scrollV))
					{
						group.motion.scrollU = range.scrollU;
						group.motion.scrollV = range.scrollV;
					}
				}

				if ((int)(range.count / 3) > biggest)
					biggest = (int)(range.count / 3);
			}
		}

		for (int k = 0; k < 5; k++)
			g_gloss_parts_by[k] += group_parts[k];

		/* The occupancy, sorted and made unique once the group's three
		 * passes have been walked - a query is a binary search from here on,
		 * and the duplicates a shared vertex leaves behind are gone. */
		std::sort(group.cells.begin(), group.cells.end());
		group.cells.erase(std::unique(group.cells.begin(), group.cells.end()),
			group.cells.end());
		group.cells.shrink_to_fit();

		std::sort(group.cellsIn.begin(), group.cellsIn.end());
		group.cellsIn.erase(std::unique(group.cellsIn.begin(),
			group.cellsIn.end()), group.cellsIn.end());
		group.cellsIn.shrink_to_fit();

		std::sort(group.cellsFree.begin(), group.cellsFree.end());
		group.cellsFree.erase(std::unique(group.cellsFree.begin(),
			group.cellsFree.end()), group.cellsFree.end());
		group.cellsFree.shrink_to_fit();

		for (size_t s = 0; s < group.standCells.size(); s++)
		{
			std::vector<unsigned int>& list = group.standCells[s];

			std::sort(list.begin(), list.end());
			list.erase(std::unique(list.begin(), list.end()), list.end());
			list.shrink_to_fit();
		}

		/* The horizontal planes, one row per height, sorted by it. The
		 * per-triangle rows are merged here rather than while they are found,
		 * because a room has tens of thousands of triangles and at most a few
		 * dozen distinct heights. */
		if (!flats.empty())
		{
			std::sort(flats.begin(), flats.end());
			flats.erase(std::unique(flats.begin(), flats.end()), flats.end());

			for (size_t i = 0; i < flats.size(); i++)
			{
				if (group.flats.empty() || group.flats.back().y != flats[i].first)
				{
					HDFlatPlane row;

					row.y = flats[i].first;
					group.flats.push_back(row);
				}

				group.flats.back().cells.push_back(flats[i].second);
			}

			group.flats.shrink_to_fit();
			g_flat_planes += (int)group.flats.size();
		}

		// The same merge over the water sheets alone.
		if (!waterflats.empty())
		{
			std::sort(waterflats.begin(), waterflats.end());
			waterflats.erase(std::unique(waterflats.begin(), waterflats.end()),
				waterflats.end());

			for (size_t i = 0; i < waterflats.size(); i++)
			{
				if (group.waterFlats.empty() ||
					group.waterFlats.back().y != waterflats[i].first)
				{
					HDFlatPlane row;

					row.y = waterflats[i].first;
					group.waterFlats.push_back(row);
				}

				group.waterFlats.back().cells.push_back(waterflats[i].second);
			}

			group.waterFlats.shrink_to_fit();
		}

		/* The samples for the matrix proof. The .TRG dies with this function
		 * and the vertex buffer is WRITEONLY, so the only chance to keep a
		 * few of a room's own vertices is here. Evenly spaced over the
		 * group's index run rather than the first hundred in a row, so the
		 * proof reaches the far corners of the room and not one wall. */
		{
			const size_t span = sorted.size() - group_first_index;

			if (span)
			{
				const size_t want = span < (size_t)HD_RENDER_SAMPLES
					? span : (size_t)HD_RENDER_SAMPLES;

				group.samples.reserve(want);

				for (size_t s = 0; s < want; s++)
				{
					const unsigned int index = sorted[group_first_index + s * span / want];

					if (index >= trg.vertices.size())
						continue;

					const trf::remaster::TrgVertex& v = trg.vertices[index];
					HDRenderSample sample;

					sample.x = v.x;
					sample.y = v.y;
					sample.z = v.z;
					sample.nx = v.nx;
					sample.ny = v.ny;
					sample.nz = v.nz;
					sample.c1[0] = v.colour1[0];
					sample.c1[1] = v.colour1[1];
					sample.c1[2] = v.colour1[2];
					sample.c2[0] = v.colour2[0];
					sample.c2[1] = v.colour2[1];
					sample.c2[2] = v.colour2[2];
					sample.bits = 0;

					if (!vertex_group.empty())
					{
						const unsigned long long key =
							((unsigned long long)(unsigned short)g << 48) |
							((unsigned long long)(unsigned short)v.x << 32) |
							((unsigned long long)(unsigned short)v.y << 16) |
							(unsigned long long)(unsigned short)v.z;

						std::unordered_map<unsigned long long, unsigned char>::const_iterator
							found = mark_map.find(key);

						if (found != mark_map.end())
							sample.bits = found->second;
					}

					group.samples.push_back(sample);
				}
			}
		}
	}

	if (sorted.empty())
	{
		// Every part of every group was empty. CreateIndexBuffer(0) would
		// fail with a less helpful message than this one.
		*why = "the .TRG names no triangle in any group";
		release_buffers();
		return false;
	}

	// The mirrors in the rooms' walls, while the .TRG and the boxes are
	// still in hand - see FindMirrors.
	FindMirrors(trg);

	// And the sheen of every draw call - see ResolveSheen.
	ResolveSheen();

	/* --- the vertex window of every draw call ---------------------------
	 *
	 * The lowest and highest vertex number each range names. On a normal card
	 * this is a number for the log; on a card with no 32 bit index buffer it
	 * is the whole question, because a 16 bit index reaches 65536 vertices
	 * from wherever the stream is moved to. One pass over the sorted stream,
	 * a few milliseconds on the largest level, and it answers both. */
	int index_window = 0;
	int triangle_window = 0;

	for (HDGroup& group : g_groups)
		for (int pass = 0; pass < 3; pass++)
			for (HDRange& range : group.pass[pass])
			{
				unsigned int lo = 0xFFFFFFFFu;
				unsigned int hi = 0;

				for (unsigned int at = 0; at + 2 < range.count; at += 3)
				{
					const unsigned int a = sorted[range.start + at];
					const unsigned int b = sorted[range.start + at + 1];
					const unsigned int c = sorted[range.start + at + 2];

					unsigned int tlo = a < b ? a : b;
					unsigned int thi = a > b ? a : b;

					if (c < tlo) tlo = c;
					if (c > thi) thi = c;

					if ((int)(thi - tlo + 1) > triangle_window)
						triangle_window = (int)(thi - tlo + 1);

					if (tlo < lo) lo = tlo;
					if (thi > hi) hi = thi;
				}

				if (!range.count)
					continue;

				range.baseVertex = lo;
				range.vertexSpan = hi - lo + 1;

				if ((int)range.vertexSpan > index_window)
					index_window = (int)range.vertexSpan;
			}

	/* --- what a card with no 32 bit index buffer can do ----------------
	 *
	 * Almost nothing, on this art, as measured. Cutting a draw call into
	 * pieces that each stay inside a 65536 vertex window and moving the
	 * vertex stream to each piece has a floor of one triangle, which cannot
	 * be cut. Measured
	 * over all fifteen levels, the widest single triangle reaches over
	 * 33,585 vertices in RICHCUT2 and between 235,507 (JOBY4) and 1,391,696
	 * (RICH2) in every other one - the .TRG deduplicates its vertices over
	 * the whole level rather than writing them room by room, so the three
	 * corners of one wall tile can sit a million entries apart.
	 *
	 * So RICHCUT2 draws in HD on such a card and the other fourteen levels
	 * stay classic, with the number in the log. Changing that would need a
	 * vertex buffer of its own for the 16 bit path, compacted per room - a
	 * second buffer layout for hardware that has not shipped since about
	 * 2002, which is not built.
	 */
	if (!g_index32 && triangle_window > 65536)
	{
		char line[220];

		std::snprintf(line, sizeof(line),
			"the device has no 32 bit index buffer and one triangle of this "
			"level reaches over %d vertices - no cut can go below that",
			triangle_window);
		g_error = line;
		*why = g_error.c_str();
		release_buffers();
		return false;
	}

	if (!g_index32 && index_window > 65536)
	{
		/* Cannot be reached while the test above stands - a range is at least
		 * as wide as its widest triangle - but a .TRG that broke that would
		 * otherwise write indices no card could follow. */
		g_error = "one draw call reaches over more than 65536 vertices and the "
			"device has no 32 bit index buffer";
		*why = g_error.c_str();
		release_buffers();
		return false;
	}

	/* The rebase. With 16 bit indices every range's numbers are written
	 * relative to the lowest vertex it names, and the draw call moves the
	 * stream to that vertex - see HDRenderRoomPass. Done in place because
	 * `sorted` is read after this only as the index buffer holds it. */
	if (!g_index32)
		for (const HDGroup& group : g_groups)
			for (int pass = 0; pass < 3; pass++)
				for (const HDRange& range : group.pass[pass])
					for (unsigned int at = 0; at < range.count; at++)
						sorted[range.start + at] -= range.baseVertex;

	// The indices of the triangles a static's piece may take, as the index
	// buffer holds them: a piece's range draws with its source range's base.
	for (PlaceTri& tri : g_place_tris)
		for (int k = 0; k < 3; k++)
			tri.index[k] = (size_t)tri.at + (size_t)k < sorted.size()
				? sorted[(size_t)tri.at + (size_t)k] : 0;

	const size_t index_bytes = sorted.size() * (g_index32 ? 4u : 2u);

	if (FAILED(g_device->CreateIndexBuffer((UINT)index_bytes, D3DUSAGE_WRITEONLY,
		g_index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_ib, 0)))
	{
		*why = "CreateIndexBuffer failed";
		release_buffers();
		return false;
	}

	{
		void* raw = 0;

		if (FAILED(g_ib->Lock(0, 0, &raw, 0)))
		{
			*why = "locking the index buffer failed";
			release_buffers();
			return false;
		}

		if (g_index32)
			std::memcpy(raw, sorted.empty() ? 0 : &sorted[0], index_bytes);
		else
		{
			unsigned short* out = (unsigned short*)raw;

			for (size_t i = 0; i < sorted.size(); i++)
				out[i] = (unsigned short)sorted[i];
		}

		g_ib->Unlock();
	}

	if (stats)
	{
		stats->trgVersion = trg.version;
		stats->groups = (int)trg.groups.size();
		stats->rooms = numRooms;
		stats->skyGroups = (int)trg.groups.size() > numRooms
			? (int)trg.groups.size() - numRooms : 0;
		stats->vertices = g_vertex_count;
		stats->indices = (int)sorted.size();
		stats->index32 = g_index32 ? 1 : 0;
		stats->indexWindow = index_window;
		stats->triangleWindow = triangle_window;
		stats->maxTextureSide = (int)(g_caps.MaxTextureWidth < g_caps.MaxTextureHeight
			? g_caps.MaxTextureWidth : g_caps.MaxTextureHeight);
		stats->waterMarks = g_mark_count;
		stats->markedVertices = marked_vertices;
		stats->markedChoppy = marked_choppy;
		stats->markedShimmer = marked_shimmer;
		stats->markedRoomShimmer = marked_room_shimmer;
		stats->markedFlooded = flooded_vertices;
		stats->markedWall = marked_wall;
		stats->waterPlanesDropped = water_multi_planes;
		stats->splitRanges = split_ranges;
		stats->biggestRangeTris = biggest;
		stats->fallbackPageTris = fallback_tris;
		stats->scrollParts = g_scroll_parts;
		stats->cutoutParts = g_cutout_parts;
		stats->addWeightParts = g_addweight_parts;
		stats->addSwingParts = g_addswing_parts;
		stats->addWeightMin = g_addweight_min;
		stats->addStillParts = g_addstill_parts;
		stats->stackedParts = g_stacked_parts;
		stats->stackedTris = g_stacked_tris;
		stats->glowParts = (int)g_glow_parts.size();
		stats->glowAssets = g_glow_assets;

		for (size_t i = 0; i < g_glow_parts.size(); i++)
		{
			stats->glowEdgeParts += g_glow_parts[i].edge ? 1 : 0;
			stats->glowFaceParts += g_glow_parts[i].face ? 1 : 0;
		}
		stats->skyFlatParts = g_flat_parts;
		stats->flipbookDrawn = g_flip_parts;
		stats->flipbookDropped = g_flip_dropped;
		stats->flipbookRanges = g_flip_ranges;

		{
			std::vector<int> pages;

			for (size_t i = 0; i < g_book_parts.size(); i++)
			{
				const HDRenderBookPart& book = g_book_parts[i];

				if (book.dropped)
				{
					stats->bookDropped++;
					continue;
				}

				stats->bookParts++;

				if (book.pass == 2)
					stats->bookAdditive++;

				if (stats->bookParts == 1 || book.step < stats->bookStepMin)
					stats->bookStepMin = book.step;

				if (stats->bookParts == 1 || book.step > stats->bookStepMax)
					stats->bookStepMax = book.step;

				for (int k = 0; k < book.pages; k++)
					pages.push_back(book.page0 + k);
			}

			std::sort(pages.begin(), pages.end());
			stats->bookPages = (int)(std::unique(pages.begin(), pages.end()) -
				pages.begin());
			stats->bookRanges = g_book_ranges;
		}
		stats->glossParts = g_gloss_parts;
		stats->flatPlanes = g_flat_planes;
		stats->blendParts = g_blend_parts;
		stats->blendRanges = g_blend_ranges;
		stats->clearParts = g_clear_parts;
		stats->clearPages = g_clear_pages;
		stats->clearRanges = g_clear_ranges;
		stats->waterParts = g_water_parts;
		stats->waterOpaqueParts = g_water_opaque;
		stats->waterAdditiveParts = g_water_additive;
		stats->waterTris = g_water_tris;
		stats->waterSubRectParts = g_water_subrect;
		stats->waterSmallestSpan = g_water_span;
		stats->waterStillParts = g_water_still;
		stats->waterRippleCap = g_water_cap;
		stats->scrollRanges = 0;
		stats->scrollTris = 0;
		stats->swayVertices = sway_vertices;
		stats->swayParts = wind_parts;
		(void)wind_vertices;
		stats->swayDropped = sway_dropped;
		stats->flipbookParts = 0;

		for (size_t g = 0; g < g_groups.size(); g++)
		{
			stats->scrollRanges += g_groups[g].motion.scrollRanges;
			stats->scrollTris += g_groups[g].motion.scrollTris;
			stats->flipbookParts += g_groups[g].motion.flipbookParts;
		}
		stats->vertexBytes = (double)g_vertex_count * (double)sizeof(HDVertex);
		stats->indexBytes = (double)index_bytes;
		stats->readMs = read_ms;
		stats->pages = page_stats;

		for (int g = 0; g < g_room_groups; g++)
		{
			unsigned int here = 0;
			unsigned int out = 0;
			unsigned int in_other = 0;

			for (int pass = 0; pass < 3; pass++)
			{
				stats->ranges += (int)g_groups[(size_t)g].pass[pass].size();
				here += g_groups[(size_t)g].triangles[pass];
				out += g_groups[(size_t)g].overhang[pass];

				// The draw calls the windowed half costs. The overhang
				// keeps no draw calls of its own - it is drawn over the
				// whole screen with everything else - so its only count is
				// per triangle, overhangTris, for the hollow-room test.
				for (size_t r = 0; r < g_groups[(size_t)g].pass[pass].size(); r++)
					if (g_groups[(size_t)g].pass[pass][r].intruding)
						stats->intrudingRanges++;

				in_other += g_groups[(size_t)g].intruding[pass];
			}

			if (!here)
				stats->emptyGroups++;
			else if (here == out)
				stats->hollowGroups++;	// nothing at all inside the room

			if (in_other)
				stats->intrudingGroups++;

			stats->overhangTris += (int)out;
			stats->intrudingTris += (int)in_other;

			// And what the openings took away before any of the counts
			// above could see it.
			if (g_groups[(size_t)g].cutTris)
			{
				stats->cutGroups++;
				stats->cutTris += (int)g_groups[(size_t)g].cutTris;
			}
		}

		stats->buildMs = now_ms() - build_started;
	}

	return true;
}

void HDRenderLevelEnd()
{
	release_buffers();
	HDPageCacheDestroy();

	// The followed book's draw call goes with the level's ranges.
	g_book_watch = 0;
	g_book_watch_group = -1;
	g_book_watch_part = -1;
}

/* ---------------------------------------------------------------- frame -- */

/* The state bracket, with no room in it. Everything the classic path set or
 * relies on, saved and put back; everything the HD path needs, set. It sits
 * here rather than in each HD module because there is one HD path and one
 * list of states - hd_model.cpp draws inside this very bracket, and a second
 * list would be a second chance to forget a state. */
void HDRenderStateSave()
{
	if (!g_device)
		return;

	/* --- save ---------------------------------------------------------- */
	g_saved = SavedState();

	g_device->GetVertexShader(&g_saved.vs);
	g_device->GetPixelShader(&g_saved.ps);
	g_device->GetVertexDeclaration(&g_saved.decl);
	g_device->GetFVF(&g_saved.fvf);
	g_device->GetStreamSource(0, &g_saved.stream, &g_saved.streamOffset, &g_saved.streamStride);
	g_device->GetIndices(&g_saved.indices);
	g_device->GetTexture(0, &g_saved.texture);

	for (int i = 0; i < kRenderStateCount; i++)
		g_device->GetRenderState(kSavedRenderStates[i], &g_saved.rs[i]);

	for (int i = 0; i < kSamplerStateCount; i++)
		g_device->GetSamplerState(0, kSavedSamplerStates[i], &g_saved.samp[i]);

	for (int s = 0; s < 2; s++)
	{
		g_device->GetTexture((DWORD)(1 + s), &g_saved.fieldTexture[s]);

		for (int i = 0; i < kSamplerStateCount; i++)
			g_device->GetSamplerState((DWORD)(1 + s), kSavedSamplerStates[i],
				&g_saved.fieldSamp[s][i]);
	}

	/* The scissor rectangle beside D3DRS_SCISSORTESTENABLE, which is
	 * two lines above in the render state list. Read back like everything
	 * else - the classic path never sets it, so what comes back is the
	 * device's own default of the whole render target, and that is exactly
	 * what has to go back. */
	g_saved.scissorValid = SUCCEEDED(g_device->GetScissorRect(&g_saved.scissor));

	/* --- the HD path's own state --------------------------------------- */
	// Real geometry has to be clipped; the classic path switched clipping off
	// because it hands the device vertices it has already clipped itself.
	g_device->SetRenderState(D3DRS_CLIPPING, TRUE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);	// the shader clips
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	// A pixel shader gets no fixed-function fog: with ps_3_0 the fog factor
	// would have to come out of the vertex shader, and the HD one writes none.
	// Distance fog is shader arithmetic instead.
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	/* The bracket starts with no window. The room path switches it on per
	 * room and off again; a caller that never touches it - the sky dome, the
	 * diagnostic tool - draws over the whole frame. The rectangle itself is
	 * left alone here: it means nothing while
	 * the test is off, and setting it would be a device call per bracket for
	 * a value the next HDRenderScissor overwrites anyway. */
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_scissor_on = false;
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	g_device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);

	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	// Mip filtering, unlike the classic path: an HD page is 512x512 where the
	// classic one was 256x256, and a wall at a distance without mips crawls.
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_wrapping = false;			// the scroll switches this per range
	g_device->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
	g_device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
	g_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

	/* The light field's samplers: linear, no mips (the field has one
	 * level), clamped at the field's edges. Their textures are bound by the
	 * room bracket (HDRenderFrameBegin) and put back with the states. */
	for (DWORD s = 1; s <= 2; s++)
	{
		g_device->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		g_device->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		g_device->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		g_device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(s, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
	}
}

void HDRenderFrameBegin(const D3DMATRIX* view, const D3DMATRIX* projection,
	const HDFrameLighting* frame)
{
	if (!HDRenderReady() || g_in_frame)
		return;

	g_in_frame = true;
	g_draw_calls = 0;
	g_cutout_calls = 0;
	g_flip_calls = 0;
	g_book_calls = 0;
	g_gloss_calls = 0;
	g_sheen_calls = 0;
	g_clear_calls = 0;
	g_water_calls = 0;
	g_receive_cleared = false;

	if (view)
		std::memcpy(g_view, view, sizeof(g_view));
	if (projection)
		std::memcpy(g_proj, projection, sizeof(g_proj));

	multiply(g_view, g_proj, g_viewproj);

	HDRenderStateSave();

	/* The room path's own buffers and shaders. Everything above is shared
	 * with hd_model.cpp; the lines below are what makes the bracket a room
	 * bracket, and hd_model sets its own shaders and buffers in their place. */
	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);
	g_device->SetStreamSource(0, g_vb, 0, sizeof(HDVertex));
	g_device->SetIndices(g_ib);
	// The light field, which the gloss variant alone samples.
	g_device->SetTexture(1, g_field_dir);
	g_device->SetTexture(2, g_field_rgb);

	/* The lighting constants. They are set here, inside the bracket, and
	 * nowhere else: with hd_enable off this function is not reached and not
	 * one SetShaderConstant is made. */
	g_water_table_up = (const void*)~(size_t)0;	// force the first upload
	apply_frame_lighting(frame);
}

/* The window one room is drawn in. `left/top` is the first pixel
 * drawn, `right/bottom` the first one that is not - Direct3D's own rule, and
 * the caller has already turned the engine's inclusive window into it.
 *
 * Both calls are cheap on purpose. A frame draws its rooms twice (the solid
 * phase and the glow phase), and a level with a hundred rooms would make four
 * hundred device calls a frame if every one of them were unconditional; so
 * the rectangle is only written when it really moved, and the enable only
 * when it really changed. The first call inside a bracket always writes both,
 * because HDRenderStateSave cleared g_scissor_on and HDRenderStateRestore put
 * the caller's own rectangle back. */
bool HDRenderScissorAvailable()
{
	return g_device && (g_caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0;
}

void HDRenderScissor(int left, int top, int right, int bottom)
{
	if (!HDRenderScissorAvailable())
		return;

	RECT want;

	want.left = (LONG)left;
	want.top = (LONG)top;
	want.right = (LONG)right;
	want.bottom = (LONG)bottom;

	if (!g_scissor_on || g_scissor.left != want.left || g_scissor.top != want.top ||
		g_scissor.right != want.right || g_scissor.bottom != want.bottom)
	{
		g_device->SetScissorRect(&want);
		g_scissor = want;
	}

	if (!g_scissor_on)
	{
		g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
		g_scissor_on = true;
	}
}

void HDRenderScissorOff()
{
	if (!g_device || !g_scissor_on)
		return;

	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_scissor_on = false;
}

void HDRenderStateRestore()
{
	if (!g_device)
		return;

	/* The rectangle first and the enable with the other render
	 * states below, so that the caller cannot see a frame in which its own
	 * scissor test is on and the HD rectangle is still in force. */
	if (g_saved.scissorValid)
		g_device->SetScissorRect(&g_saved.scissor);

	g_scissor_on = false;

	for (int i = 0; i < kRenderStateCount; i++)
		g_device->SetRenderState(kSavedRenderStates[i], g_saved.rs[i]);

	for (int i = 0; i < kSamplerStateCount; i++)
		g_device->SetSamplerState(0, kSavedSamplerStates[i], g_saved.samp[i]);

	for (int s = 0; s < 2; s++)
	{
		for (int i = 0; i < kSamplerStateCount; i++)
			g_device->SetSamplerState((DWORD)(1 + s), kSavedSamplerStates[i],
				g_saved.fieldSamp[s][i]);

		g_device->SetTexture((DWORD)(1 + s), g_saved.fieldTexture[s]);

		if (g_saved.fieldTexture[s])
		{
			g_saved.fieldTexture[s]->Release();
			g_saved.fieldTexture[s] = 0;
		}
	}

	g_device->SetTexture(0, g_saved.texture);
	g_device->SetIndices(g_saved.indices);

	/* The classic path has no stream bound - it draws with DrawPrimitiveUP -
	 * so this usually unbinds stream 0 again. One runtime quirk (RoomProbe's
	 * state-bracket check shows it): after SetStreamSource(0, NULL, 0, 0) the
	 * runtime still answers 20 to GetStreamSource's stride. It keeps the last
	 * stride it was given and there is no way to put the old one back - which
	 * costs nothing, because the stride of an unbound stream is read by
	 * nobody. */
	g_device->SetStreamSource(0, g_saved.stream, g_saved.streamOffset, g_saved.streamStride);

	/* The declaration goes back through the FVF whenever there was one - that
	 * is the shape the classic path set it in (function_table.cpp,
	 * SetFVF(FVF)), and putting the FVF back is what makes GetFVF answer
	 * again for whoever asks next. A caller that really had a declaration of
	 * its own gets that instead. */
	if (g_saved.fvf)
		g_device->SetFVF(g_saved.fvf);
	else
		g_device->SetVertexDeclaration(g_saved.decl);

	g_device->SetVertexShader(g_saved.vs);
	g_device->SetPixelShader(g_saved.ps);

	if (g_saved.texture) { g_saved.texture->Release(); g_saved.texture = 0; }
	if (g_saved.indices) { g_saved.indices->Release(); g_saved.indices = 0; }
	if (g_saved.stream) { g_saved.stream->Release(); g_saved.stream = 0; }
	if (g_saved.decl) { g_saved.decl->Release(); g_saved.decl = 0; }
	if (g_saved.vs) { g_saved.vs->Release(); g_saved.vs = 0; }
	if (g_saved.ps) { g_saved.ps->Release(); g_saved.ps = 0; }
}

void HDRenderFrameEnd()
{
	if (!g_in_frame)
		return;

	g_in_frame = false;
	HDRenderStateRestore();
}

// See the note in hd_render.h.
bool HDRenderInFrame()
{
	return g_in_frame;
}

/* ------------------------------------------------- the projected shadow -- */

namespace
{
	IDirect3DTexture9* g_shadow_texture = 0;
	IDirect3DSurface9* g_shadow_colour = 0;
	IDirect3DSurface9* g_shadow_depth = 0;
	HDShadowTargetInfo g_shadow_info = { HD_SHADOW_TARGET_NONE, "ok", "-", "-", 0, 0 };

	/* The second map, the other casters', beside hers: the same kind of
	 * pair, HD_SHADOW_OTHERS_MAP texels a side, with a life of its own - a
	 * refusal of it leaves hers running. */
	IDirect3DTexture9* g_others_texture = 0;
	IDirect3DSurface9* g_others_colour = 0;
	IDirect3DSurface9* g_others_depth = 0;
	HDShadowTargetInfo g_others_info = { HD_SHADOW_TARGET_NONE, "ok", "-", "-", 0, 0 };

	/* The vertex registers the cast pass may write: gProj (c0 - c3) and
	 * everything ModelVS reads up to gSoft (c163 - c166). */
	const int kShadowVertexRegisters = 167;

	/* What the cast pass changes, as it was. The render states are the ones
	 * HDRenderShadowCastBegin sets; every other state the pass leaves alone. */
	const D3DRENDERSTATETYPE kShadowRenderStates[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_CULLMODE,
		D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE,
		D3DRS_SCISSORTESTENABLE, D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE,
		D3DRS_CLIPPING, D3DRS_FILLMODE, D3DRS_FOGENABLE
	};

	const int kShadowRenderStateCount =
		(int)(sizeof(kShadowRenderStates) / sizeof(kShadowRenderStates[0]));

	struct ShadowSaved
	{
		bool open = false;
		IDirect3DSurface9* colour = 0;
		IDirect3DSurface9* depth = 0;
		D3DVIEWPORT9 viewport = {};
		RECT scissor = { 0, 0, 0, 0 };
		bool scissorValid = false;
		DWORD rs[16] = {};
		IDirect3DVertexShader9* vs = 0;
		IDirect3DPixelShader9* ps = 0;
		IDirect3DVertexDeclaration9* decl = 0;
		DWORD fvf = 0;
		IDirect3DVertexBuffer9* stream = 0;
		UINT streamOffset = 0;
		UINT streamStride = 0;
		IDirect3DIndexBuffer9* indices = 0;
		IDirect3DBaseTexture9* texture = 0;
		float vertex[kShadowVertexRegisters * 4] = {};
		float pixel[4] = {};
	};

	ShadowSaved g_shadow_saved;

	void shadow_state(int state, const char* reason)
	{
		g_shadow_info.state = state;
		g_shadow_info.reason = reason;
		g_shadow_info.events++;
	}

	bool shadow_refuse(const char* reason)
	{
		HDRenderShadowTargetRelease();
		shadow_state(HD_SHADOW_TARGET_REFUSED, reason);
		return false;
	}

	void others_state(int state, const char* reason)
	{
		g_others_info.state = state;
		g_others_info.reason = reason;
		g_others_info.events++;
	}

	/* The second pair alone: hers stays as it is. */
	void others_release()
	{
		const bool had = g_others_texture || g_others_colour || g_others_depth;

		if (g_others_depth) { g_others_depth->Release(); g_others_depth = 0; }
		if (g_others_colour) { g_others_colour->Release(); g_others_colour = 0; }
		if (g_others_texture) { g_others_texture->Release(); g_others_texture = 0; }

		if (had)
			others_state(HD_SHADOW_TARGET_RELEASED, "ok");
	}

	bool others_refuse(const char* reason)
	{
		others_release();
		others_state(HD_SHADOW_TARGET_REFUSED, reason);
		return false;
	}

	/* One pair made: an R32F render target texture `size` texels a side and
	 * a depth surface of its own beside it, both D3DPOOL_DEFAULT. 0 when
	 * both were made, otherwise the word for what refused - the caller
	 * releases what was made and refuses with it. `depthName` comes back
	 * with the depth format's name.
	 *
	 * The format first, of the adapter the device runs on: R32F as a render
	 * target texture, then a depth format that goes with it. The adapter
	 * format is the display mode's, which is the back buffer's family on a
	 * windowed device. */
	const char* shadow_pair_make(int size, IDirect3DTexture9*& texture,
		IDirect3DSurface9*& colour, IDirect3DSurface9*& depthSurface, const char** depthName)
	{
		if (!g_device)
			return "no_device";

		IDirect3D9* d3d = 0;
		D3DDEVICE_CREATION_PARAMETERS made;
		D3DDISPLAYMODE mode;

		if (FAILED(g_device->GetDirect3D(&d3d)) || !d3d)
			return "no_device";

		const bool known = SUCCEEDED(g_device->GetCreationParameters(&made)) &&
			SUCCEEDED(g_device->GetDisplayMode(0, &mode));
		D3DFORMAT depth = D3DFMT_UNKNOWN;
		const char* refused = 0;

		if (!known)
			refused = "no_device";
		else if (FAILED(d3d->CheckDeviceFormat(made.AdapterOrdinal, made.DeviceType,
			mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F)))
			refused = "format";
		else
		{
			static const D3DFORMAT kDepth[2] = { D3DFMT_D24X8, D3DFMT_D16 };

			for (int i = 0; i < 2 && depth == D3DFMT_UNKNOWN; i++)
				if (SUCCEEDED(d3d->CheckDeviceFormat(made.AdapterOrdinal, made.DeviceType,
					mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, kDepth[i])))
					depth = kDepth[i];

			if (depth == D3DFMT_UNKNOWN)
				refused = "depth";
			else if (FAILED(d3d->CheckDepthStencilMatch(made.AdapterOrdinal,
				made.DeviceType, mode.Format, D3DFMT_R32F, depth)))
				refused = "match";
		}

		d3d->Release();

		if (refused)
			return refused;

		if (FAILED(g_device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET,
			D3DFMT_R32F, D3DPOOL_DEFAULT, &texture, 0)))
		{
			texture = 0;
			return "create";
		}

		if (FAILED(texture->GetSurfaceLevel(0, &colour)))
		{
			colour = 0;
			return "surface";
		}

		/* Not multisampled, like the texture, and discarded: the pass clears
		 * it every frame and nothing reads it afterwards. */
		if (FAILED(g_device->CreateDepthStencilSurface(size, size, depth,
			D3DMULTISAMPLE_NONE, 0, TRUE, &depthSurface, 0)))
		{
			depthSurface = 0;
			return "depth_create";
		}

		*depthName = depth == D3DFMT_D24X8 ? "D24X8" : "D16";
		return 0;
	}
}

bool HDRenderShadowTargetReady()
{
	if (g_shadow_texture && g_shadow_colour && g_shadow_depth)
		return true;

	if (g_shadow_info.state == HD_SHADOW_TARGET_REFUSED)
		return false;

	const char* depth = "-";
	const char* refused = shadow_pair_make(HD_SHADOW_MAP, g_shadow_texture, g_shadow_colour,
		g_shadow_depth, &depth);

	if (refused)
		return shadow_refuse(refused);

	g_shadow_info.format = "R32F";
	g_shadow_info.depth = depth;
	g_shadow_info.size = HD_SHADOW_MAP;
	shadow_state(HD_SHADOW_TARGET_MADE, "ok");
	return true;
}

/* Hers and, with it, the second: every road that releases her map - a
 * refusal of hers, the device going away, a probe's own call - releases the
 * second too, so no D3DPOOL_DEFAULT surface of the pair outlives the other
 * across a Reset. */
void HDRenderShadowTargetRelease()
{
	const bool had = g_shadow_texture || g_shadow_colour || g_shadow_depth;

	others_release();

	if (g_shadow_depth) { g_shadow_depth->Release(); g_shadow_depth = 0; }
	if (g_shadow_colour) { g_shadow_colour->Release(); g_shadow_colour = 0; }
	if (g_shadow_texture) { g_shadow_texture->Release(); g_shadow_texture = 0; }

	if (had)
		shadow_state(HD_SHADOW_TARGET_RELEASED, "ok");
}

static void ShadowForgetRefusal()
{
	if (g_shadow_info.state == HD_SHADOW_TARGET_REFUSED)
		shadow_state(HD_SHADOW_TARGET_RELEASED, "ok");

	if (g_others_info.state == HD_SHADOW_TARGET_REFUSED)
		others_state(HD_SHADOW_TARGET_RELEASED, "ok");
}

void HDRenderShadowTargetInfo(HDShadowTargetInfo* out)
{
	if (out)
		*out = g_shadow_info;
}

IDirect3DTexture9* HDRenderShadowMap()
{
	return g_shadow_saved.open ? 0 : g_shadow_texture;
}

bool HDRenderShadowOthersReady()
{
	if (g_others_texture && g_others_colour && g_others_depth)
		return true;

	if (g_others_info.state == HD_SHADOW_TARGET_REFUSED)
		return false;

	const char* depth = "-";
	const char* refused = shadow_pair_make(HD_SHADOW_OTHERS_MAP, g_others_texture,
		g_others_colour, g_others_depth, &depth);

	if (refused)
		return others_refuse(refused);

	g_others_info.format = "R32F";
	g_others_info.depth = depth;
	g_others_info.size = HD_SHADOW_OTHERS_MAP;
	others_state(HD_SHADOW_TARGET_MADE, "ok");
	return true;
}

void HDRenderShadowOthersRelease()
{
	others_release();
}

void HDRenderShadowOthersInfo(HDShadowTargetInfo* out)
{
	if (out)
		*out = g_others_info;
}

IDirect3DTexture9* HDRenderShadowOthersMap()
{
	return g_shadow_saved.open ? 0 : g_others_texture;
}

/* The cast bracket's opening, on either pair: everything the pass changes
 * remembered, the pair set, the viewport its whole side, the colour cleared
 * to 1 and the depth to 1. */
static bool ShadowCastOpen(IDirect3DTexture9* texture, IDirect3DSurface9* colour,
	IDirect3DSurface9* depth, int size)
{
	if (!g_device || !g_in_frame || g_shadow_saved.open || !texture || !colour || !depth)
		return false;

	ShadowSaved& s = g_shadow_saved;

	s = ShadowSaved();

	if (FAILED(g_device->GetRenderTarget(0, &s.colour)))
		return false;

	// A device without a depth surface answers with a failure and a null.
	if (FAILED(g_device->GetDepthStencilSurface(&s.depth)))
		s.depth = 0;

	g_device->GetViewport(&s.viewport);
	s.scissorValid = SUCCEEDED(g_device->GetScissorRect(&s.scissor));

	for (int i = 0; i < kShadowRenderStateCount; i++)
		g_device->GetRenderState(kShadowRenderStates[i], &s.rs[i]);

	g_device->GetVertexShader(&s.vs);
	g_device->GetPixelShader(&s.ps);
	g_device->GetVertexDeclaration(&s.decl);
	g_device->GetFVF(&s.fvf);
	g_device->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
	g_device->GetIndices(&s.indices);
	g_device->GetTexture(0, &s.texture);
	g_device->GetVertexShaderConstantF(0, s.vertex, kShadowVertexRegisters);
	g_device->GetPixelShaderConstantF(4, s.pixel, 1);
	s.open = true;

	/* SetRenderTarget puts the viewport and the scissor rectangle to the
	 * whole map; both are put back from the copies above. */
	g_device->SetRenderTarget(0, colour);
	g_device->SetDepthStencilSurface(depth);

	D3DVIEWPORT9 whole = { 0, 0, (DWORD)size, (DWORD)size, 0.0f, 1.0f };

	g_device->SetViewport(&whole);

	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	g_device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CLIPPING, TRUE);
	g_device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);

	// White is 1.0 in the red channel of an R32F target: nothing cast.
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f, 0);
	return true;
}

bool HDRenderShadowCastBegin()
{
	return ShadowCastOpen(g_shadow_texture, g_shadow_colour, g_shadow_depth, HD_SHADOW_MAP);
}

bool HDRenderShadowOthersCastBegin()
{
	return ShadowCastOpen(g_others_texture, g_others_colour, g_others_depth,
		HD_SHADOW_OTHERS_MAP);
}

void HDRenderShadowCastEnd()
{
	ShadowSaved& s = g_shadow_saved;

	if (!s.open || !g_device)
		return;

	g_device->SetRenderTarget(0, s.colour);
	g_device->SetDepthStencilSurface(s.depth);
	g_device->SetViewport(&s.viewport);

	if (s.scissorValid)
		g_device->SetScissorRect(&s.scissor);

	for (int i = 0; i < kShadowRenderStateCount; i++)
		g_device->SetRenderState(kShadowRenderStates[i], s.rs[i]);

	g_device->SetVertexShaderConstantF(0, s.vertex, kShadowVertexRegisters);
	g_device->SetPixelShaderConstantF(4, s.pixel, 1);
	g_device->SetTexture(0, s.texture);
	g_device->SetIndices(s.indices);
	g_device->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);

	// The declaration as HDRenderStateRestore puts it back.
	if (s.fvf)
		g_device->SetFVF(s.fvf);
	else
		g_device->SetVertexDeclaration(s.decl);

	g_device->SetVertexShader(s.vs);
	g_device->SetPixelShader(s.ps);

	if (s.colour) { s.colour->Release(); s.colour = 0; }
	if (s.depth) { s.depth->Release(); s.depth = 0; }
	if (s.vs) { s.vs->Release(); s.vs = 0; }
	if (s.ps) { s.ps->Release(); s.ps = 0; }
	if (s.decl) { s.decl->Release(); s.decl = 0; }
	if (s.stream) { s.stream->Release(); s.stream = 0; }
	if (s.indices) { s.indices->Release(); s.indices = 0; }
	if (s.texture) { s.texture->Release(); s.texture = 0; }

	s.open = false;
}

namespace
{
	/* The two sets of rows the gloss variant reads its world out of (see
	 * "The sheen" in hd_room.hlsl), once per room: gGlossAxes (c86 - c88),
	 * which turn a view-space direction into world axes, and gGlossField
	 * (c89 - c91), which turn a view-space position into the light field's
	 * texture coordinate. `m` is the room's world-view matrix (a D3DMATRIX,
	 * row vectors on the left: view = x * row 0 + y * row 1 + z * row 2 +
	 * row 3), so the room's own coordinate along axis i is
	 * (view - row 3) . row i / |row i|^2, and the world's is that plus the
	 * group's origin. In the engine the matrix is the finished D3DMView and
	 * the room's own coordinates are room local; in RoomProbe the matrix is
	 * the tool's view of a room it draws at its own origin - both undo to the
	 * group's own coordinates, and the origin kept from the build makes them
	 * world. Every register every room: a constant another room left behind
	 * would put the field somewhere else. Written while the plain variant is
	 * on the device, which reads none of these registers. */
	void upload_gloss_rows(int group, const float* m)
	{
		float regs[6 * 4] = { 0 };
		const float origin[3] =
		{
			(size_t)group * 2 + 1 < g_group_origin.size() ? g_group_origin[(size_t)group * 2] : 0.0f,
			0.0f,
			(size_t)group * 2 + 1 < g_group_origin.size() ? g_group_origin[(size_t)group * 2 + 1] : 0.0f
		};

		for (int i = 0; i < 3; i++)
		{
			const float* r = &m[i * 4];
			const float len2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];

			if (!(len2 > 0.0f))
				continue;

			const float len = sqrtf(len2);
			const float side = (float)g_field_grid.dims[i] * HD_GLOSS_FIELD_CELL;
			const float inv = g_field_dir && side > 0.0f ? 1.0f / side : 0.0f;
			const float along = (m[12] * r[0] + m[13] * r[1] + m[14] * r[2]) / len2;

			for (int c = 0; c < 3; c++)
			{
				regs[i * 4 + c] = r[c] / len;
				regs[(3 + i) * 4 + c] = r[c] / len2 * inv;
			}

			regs[(3 + i) * 4 + 3] = (origin[i] - along - g_field_grid.origin[i]) * inv;
		}

		g_device->SetPixelShaderConstantF(86, regs, 6);
	}

	/* `blendOnly`: the alpha pass is drawn twice in a frame, once for its
	 * solid ranges - the cut-outs, with the alpha test - and once, after
	 * everything opaque, for its see-through ones. Both other passes are
	 * called with it false and neither has a blended range in it.
	 *
	 * `clearOnly` splits the see-through half once more: true draws the clear
	 * floors alone, with the depth write on (HD_PASS_CLEAR), false the glass
	 * alone, without it (HD_PASS_BLEND). It means nothing unless `blendOnly`
	 * is true. */
	void draw_pass(const HDGroup& group, int pass, const float* wvp,
		bool blendOnly = false, int window = HD_WINDOW_ALL, bool sky = false,
		bool clearOnly = false)
	{
		if (group.pass[pass].empty())
			return;

		/* gPass: x is the alpha reference - a texel below it is thrown away
		 * and zero throws nothing away - y the additive weighting, z unread
		 * by the room shaders (0), w the specular add - the
		 * classic path switches D3DRS_SPECULARENABLE off for its additive
		 * draw type (HWR_DrawSortList, polyinsert.cpp) and the overbright half of the room
		 * colour goes off with it. The PIXEL constant file only: the room
		 * vertex shader reads none of the four, and fxc keeps a literal of
		 * its own in c4 of that shader (the def list in hd_room_vs.inc).
		 *
		 * The alpha pass's 0.5 is the classic path's own reference
		 * (function_table.cpp, D3DRS_ALPHAREF 128 over a 0..255 alpha); the
		 * opaque pass starts at zero and is raised per range below. */
		/* And zero in the blend phase. The alpha test would turn a pane of
		 * glass into a mask of itself; in this phase the texel's own alpha
		 * decides how much of it is there. */
		float constants[4] =
		{
			(pass == 1 && !blendOnly) ? 0.5f : 0.0f,
			pass == 2 ? 1.0f : 0.0f,
			0.0f,
			pass == 2 ? 0.0f : 1.0f
		};

		g_device->SetPixelShaderConstantF(4, constants, 1);

		/* The opaque pass turns the alpha test on for the ranges whose .TRG
		 * material is one of the three cut-out materials, and for no others
		 * - see MaterialAlphaRef at the top of this file for why the page
		 * cannot decide it. The constant is written only when it changes,
		 * and the ranges are sorted by it, so a room pays for it once per
		 * run. */
		float alpha_ref = constants[0];
		const bool cutout_pass = pass == 0;

		/* gFlat, the vertex side's own per draw call constant. x is 1
		 * where the part carries no baked light and its page is the whole
		 * picture - the sky band, and nothing else in the game. Written at
		 * the top of every pass whatever it was, for the same reason gMotion
		 * is: a model draw between two rooms owns c8 to c103 of the vertex
		 * constant file and leaves nothing of the room's in it. */
		/* gFlat.w is the "a water surface is flat" gate, 1 in the engine
		 * and 0 only for the diagnostic tool's comparison picture - see
		 * HDRenderUprightOverride. */
		float flat[4] = { 0.0f, 0.0f, 0.0f, g_no_upright ? 0.0f : 1.0f };
		bool flat_dirty = true;

		/* gAdd, the additive pass's own constant. x is this draw call's
		 * weight, y the game clock in seconds, z the swing time in seconds
		 * (0 = still) and w the hd_motion switch, so that hd_motion 0 draws
		 * the same still frame the weight alone gives.
		 * Written per range, exactly like the alpha reference above and for
		 * the same reason; the other two passes write it once as (1, ., 0, .)
		 * so the shader's one line costs them a multiply by one. */
		float add[4] = { 1.0f, g_motion[2], 0.0f, g_motion[3] };
		bool add_dirty = true;
		/* The gloss constant - see the note beside the write below. */
		float spec[4] = { 0.0f, 0.0f, g_reflect, 1.0f };
		bool spec_dirty = true;
		/* The gloss variant: whether it is on the device, and its gGloss
		 * (c36) - see the note beside the switch below. */
		bool sheen_ps_on = false;
		float sheen[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		bool sheen_dirty = true;
		/* gRipple: the wave scale, the travel and the rate of a material
		 * 16 water surface. Three zeroes for every other draw call in the
		 * game, where the pixel shader's one compare on .y skips the whole
		 * of it - so a level with no water pays one compare per pixel and
		 * uploads this constant once per pass. */
		float ripple[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		bool ripple_dirty = true;
		const bool additive_pass = pass == 2;
		// Whether c0 holds the pushed-back matrix - see set_stand_matrix.
		bool behind_on = false;

		/* The glow: material 2's draw calls in the additive pass - the light
		 * shafts, the lamp cones and the window glows.
		 *
		 * They compose as SCREEN, dst + src - dst * src, where every other
		 * additive material sums ONE / ONE. Direct3D 9 has it as a blend
		 * state and nothing else: SRCBLEND INVDESTCOLOR against DESTBLEND ONE
		 * is src * (1 - dst) + dst. A shaft is a fan of cards that overlap
		 * where it is brightest, and a sum of them clips: ANDREA2 room 131's
		 * window shaft is about 130 cards, 6.2 of them over the average pixel,
		 * whose unclamped sum averages 1.22 and peaks at 3.54 times white, so
		 * 56.7 per cent of the shaft's pixels clip in the additive pass alone
		 * and the core is a flat white slab with the cards' own edges cut into
		 * it (RoomProbe, the additive pass over black). Screen takes each card
		 * a share of what is still left to white, so a stack comes near white
		 * and never reaches it, and a single card over black is exactly what
		 * the sum drew. The screen composition of the glow is TROne's own
		 * rule: TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931.
		 * Screen and the sum do not commute, so where a glow overlaps another
		 * additive material - JOBY4 room 19's rays over its sea - the picture
		 * depends on which is drawn first: within a group the sort key's
		 * order, led by the page slot, and between groups the order the
		 * groups are drawn in.
		 *
		 * And they are sampled ANISOTROPICALLY. A card is seen along its
		 * streaks: the page footprint of a pixel is 5.6 to 6.2 times longer
		 * than it is wide (median over ANDREA2 room 131's and RICH1 room 3's
		 * shafts and JOBY4 room 19's rays, their own triangles projected
		 * through RoomProbe's camera), and the trilinear filter picks its mip
		 * by the long side - an area-weighted 1.66 mip levels too blurry
		 * across ANDREA2's streaks and 1.79 across RICH1's. Up to 16
		 * taps, as many as the device offers, sample across the streaks at
		 * the finer level. The gain in the picture is slight: against the
		 * trilinear filter the glow alone moves by at most 9 of 255 in TITLE
		 * room 6's church view and 15 of 255 in ANDREA2 room 131's view from
		 * 4811 -1250 5864, by more than 4 on under 1.4 per cent of the
		 * glow's pixels (RoomProbe -glow, -aniso 0 against 1).
		 *
		 * Both are set when the run of glow draw calls starts and put back
		 * when it ends, so every other draw call of the pass - and every
		 * pixel of the other two - sees the states it always had. */
		bool glow_on = false;
		DWORD glow_taps = 0;
		DWORD kept_taps = 1;
		/* And they are drawn with the glow pixel shader (hd_glow.hlsl), which
		 * fades a glow near the eye, where its card is seen edge-on, at its
		 * card sides and at the ends of its asset - see BuildGlowParts for
		 * what the level build decides per part. The room shader comes back
		 * for every other draw call, gRipple with it: the glow shader keeps a
		 * literal in c36, gRipple's register. `glow_up` is the part whose
		 * three registers are on the device. */
		bool glow_ps_on = false;
		int glow_up = -1;
		/* The cull mode on the device. The bracket installs D3DCULL_NONE,
		 * the ranges RangeCulls picks switch it to D3DCULL_CCW, and the end
		 * of the pass puts NONE back, so a model drawn between two rooms
		 * finds the state the bracket installed. */
		DWORD cull_now = D3DCULL_NONE;

		if (additive_pass && !g_glow_no_aniso &&
			(g_caps.RasterCaps & D3DPRASTERCAPS_ANISOTROPY) &&
			(g_caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC))
		{
			glow_taps = g_caps.MaxAnisotropy < 16 ? g_caps.MaxAnisotropy : 16;

			if (glow_taps < 2)
				glow_taps = 0;
		}

		g_motion_dirty = true;		// see the note beside g_motion_dirty

		if (pass == 2)
		{
			// The additive pass paints light, not surface: it must not hide
			// what is behind it, so it writes no depth.
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
			g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		}
		else if (sky && pass == 1)
		{
			/* The sky's alpha pass blends; it is not cut.
			 *
			 * The sky group's two alpha-pass parts are the painted dome and
			 * a cloud sheet, and measured over their own pages the alpha of
			 * both is a soft ramp: 7003, the dome, has 33.5 % of its texels
			 * under 16 and 59.2 % over 240 with every value in between, and
			 * 7101, the sheet, 42.0 % under 16 and 35.9 % over 240. An alpha
			 * test at 0.5 - made for the cut-out materials, where a texel is
			 * either wholly there or wholly gone - leaves a feathered cloud
			 * as a hard-edged silhouette (RoomProbe -skyonly shows it).
			 *
			 * So the sky's alpha pass is drawn the way the glass is: the
			 * source's own alpha against one minus it, and no test. The
			 * depth write stays on, unlike the glass - the sky sorts inside
			 * itself in its own depth slice (see HDRenderSky), the dome is
			 * in front of the sheets at the horizon and behind them
			 * overhead, and taking the depth write away would lose that
			 * order. Nothing outside the sky can be affected either way:
			 * every pixel of the world is nearer than HD_SKY_MINZ. */
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			constants[0] = 0.0f;
			alpha_ref = 0.0f;
			g_device->SetPixelShaderConstantF(4, constants, 1);
		}
		else if (blendOnly)
		{
			/* The glass. Ordinary translucency - the source's own alpha
			 * against one minus it - and no depth write, because a pane you
			 * can see through must not hide what stands behind it from the
			 * draws that come after. The alpha test is left off here, so a
			 * texel at 0.45 is half there and not gone.
			 *
			 * The clear floors blend the same way and keep the depth write -
			 * see HD_PASS_CLEAR in hd_render.h for why a floor needs it. */
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, clearOnly ? TRUE : FALSE);
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}

		for (const HDRange& range : group.pass[pass])
		{
			// This phase draws the see-through ranges or the solid ones,
			// never both - see the note on `blendOnly`.
			if ((range.blend != 0.0f) != blendOnly)
				continue;

			// And the see-through half draws its floors or its glass.
			if (blendOnly && (range.clear != 0) != clearOnly)
				continue;

			/* And this call draws the art that is drawn whole or the art
			 * that is drawn inside the window, never both - the two are
			 * drawn with different scissor states. A piece of the intruding
			 * art this frame's HDRenderPairWhole answered for goes with the
			 * open half. HD_WINDOW_ALL draws every range, which is what every
			 * room but a handful is drawn with. */
			const bool windowed = range.intruding &&
				!group.pairWhole[(size_t)range.intruding - 1];

			/* A stand range: alone in HD_WINDOW_STAND, and otherwise with
			 * the open half while this frame's HDRenderStandWith says so -
			 * see HDStandDraw in hd_cover.h. */
			if (window == HD_WINDOW_STAND)
			{
				if (range.stand != g_stand_pick + 1)
					continue;
			}
			else if (range.stand && !group.standWith[(size_t)range.stand - 1])
				continue;

			if ((window == HD_WINDOW_OPEN && windowed) ||
				(window == HD_WINDOW_INTRUDING && !windowed))
				continue;

			// And the inside/overhang split, for RoomProbe's -tround alone.
			if ((window == HD_WINDOW_INSIDE && range.overhang) ||
				(window == HD_WINDOW_OVERHANG && !range.overhang))
				continue;

			// And one material's glow alone, for RoomProbe's -addmat alone.
			if (additive_pass && g_add_only >= 0 && range.material != g_add_only)
				continue;

			/* A destructible piece Lara has shot away. The classic path
			 * clears MESH_INFO::Flags bit 0 where it shatters a static (for a
			 * shot, GetTargetOnLOS in control.cpp) and stops
			 * drawing the static; this stops drawing the art that was baked in
			 * its place. -1, which is every draw call in the game but 69
			 * parts of six levels, skips the test. */
			if (range.destructId >= 0 &&
				g_destruct[(size_t)range.destructId].hidden)
				continue;

			/* The flipbook. Four pages, one of them per frame time, the
			 * range's own page first - see MaterialFlipTime for what the
			 * .TRG says. The clock is the same monotonic game clock the other
			 * motions ride on, and it is taken modulo the whole cycle before
			 * the divide so that an hour of play never leaves the range of an
			 * int. Zero flipTime - every draw call in the game but 27 parts of
			 * five levels - skips the whole block, and hd_motion 0 leaves the
			 * picture on frame 0. */
			int page_slot = range.pageSlot;

			if (range.flipTime > 0.0f && g_motion[3] != 0.0f)
			{
				const float cycle = range.flipTime * 4.0f;
				const int frame = (int)(fmodf(g_motion[2], cycle) / range.flipTime);

				if (frame > 0 && frame < 4 && range.flipSlot[frame - 1] >= 0)
					page_slot = range.flipSlot[frame - 1];

				g_flip_calls++;
			}

			/* The book. The page the clock names - the drawn frame's own
			 * time, whole steps since the level began plus the phase,
			 * modulo the count (hd_book.h, HDBookPage) - and page 0, the
			 * range's own, where hd_motion is 0. One SetTexture per draw call
			 * whatever the page, so a book costs no draw call more than a
			 * still page. Zero bookTime - every draw call that is not
			 * material 15 - skips the whole block. */
			if (range.bookTime > 0.0f && (g_motion[3] != 0.0f || g_book_page >= 0))
			{
				const int page = g_book_page >= 0
					? g_book_page % range.bookCount
					: HDBookPage(g_book_clock, range.bookTime, range.bookCount,
						range.bookPhase);

				if (page > 0 && range.bookFirst >= 0 &&
					(size_t)(range.bookFirst + page - 1) < g_book_slots.size())
					page_slot = g_book_slots[(size_t)(range.bookFirst + page - 1)];

				/* The book the log follows: the first one drawn in the
				 * level, and the page it shows now. */
				if (!g_book_watch)
				{
					g_book_watch = &range;
					g_book_watch_group = (int)(&group - &g_groups[0]);
					g_book_watch_part = range.bookPart;
				}

				if (g_book_watch == &range)
					g_book_watch_page = page;

				g_book_calls++;
			}

			IDirect3DTexture9* page = HDPageCacheSlotTexture(page_slot);

			if (!page)
				continue;

			/* The gloss variant, for a draw call whose surface resolved to a
			 * sheen (ResolveSheen), while hd_reflect is on. With hd_reflect
			 * off no draw call takes it and every draw call is drawn as it
			 * always was.
			 *
			 * The order of the switch. Each variant keeps literals of its own
			 * (fxc's `def` constants) in registers the other names, and a
			 * `def` literal is local to its shader in Direct3D 9: it applies
			 * while that shader is bound, and a value written through
			 * SetPixelShaderConstantF stays in the register for the other.
			 * The switch keeps an order anyway, as a precaution:
			 * leaving the variant happens here, before a plain draw call's
			 * writes; entering it happens after a glossy one's, right before
			 * the draw; between two glossy draw calls only c4, c19 and c36
			 * change; and the pass ends on the plain variant, so the next
			 * room's constants are written with the plain variant on. */
			const bool want_sheen = range.sheen > 0.0f && g_gloss_ps &&
				g_reflect != 0.0f && !blendOnly && !sky && !additive_pass;

			if (!want_sheen && sheen_ps_on)
			{
				g_device->SetPixelShader(g_ps);
				sheen_ps_on = false;
			}

			/* Into the glow shader or out of it, ahead of this draw call's
			 * constants, so that gRipple is written after the room shader is
			 * back. */
			const bool want_glow_ps = additive_pass && range.glow >= 0 &&
				!g_glow_no_shader && g_glow_ps;

			if (want_glow_ps != glow_ps_on)
			{
				glow_ps_on = want_glow_ps;
				g_device->SetPixelShader(glow_ps_on ? g_glow_ps : g_ps);
				glow_up = -1;

				if (!glow_ps_on)
					ripple_dirty = true;
			}

			if (glow_ps_on && range.glow != glow_up)
			{
				float shape[12];

				GlowConstants(g_glow_parts[(size_t)range.glow], shape);
				g_device->SetPixelShaderConstantF(77, shape, 3);
				glow_up = range.glow;
			}

			if (cutout_pass)
			{
				if (range.alphaRef != alpha_ref)
				{
					alpha_ref = range.alphaRef;
					constants[0] = range.alphaRef;
					g_device->SetPixelShaderConstantF(4, constants, 1);
				}

				g_cutout_calls += range.alphaRef != 0.0f ? 1 : 0;
			}

			/* gSpec, the gloss of this draw call - x the strength out of
			 * the material, y its exponent, z the hd_reflect switch. Written
			 * per range like the additive weight above and for the same
			 * reason, and (0, 0, .) for every part that names no gloss, where
			 * the shader's one branch costs a compare. */
			/* gSpec.w is the see-through weight of this draw call, and 1
			 * for every draw call that is not glass - the pixel shader
			 * multiplies its alpha output by it, and outside the blend phase
			 * that output is not looked at, so the picture of every other
			 * pass is unchanged to the bit. */
			const float want_blend = range.blend != 0.0f ? range.blend : 1.0f;
			/* A glossy draw call on the gloss variant carries its resolved
			 * sheen in x and y; every other draw call the material's gloss,
			 * as it always has. */
			const float want_gloss = want_sheen ? range.sheen : range.gloss;
			const float want_power = want_sheen ? range.sheenPower : range.glossPower;

			if (spec_dirty || spec[0] != want_gloss ||
				spec[1] != want_power || spec[3] != want_blend)
			{
				spec[0] = want_gloss;
				spec[1] = want_power;
				spec[3] = want_blend;
				spec_dirty = false;
				g_device->SetPixelShaderConstantF(19, spec, 1);
			}

			if (range.gloss != 0.0f)
				g_gloss_calls++;

			if (want_sheen)
				g_sheen_calls++;

			/* gGloss, in gRipple's register: F0, how far the sheen takes the
			 * texel's colour, whether the page's alpha masks the sheen, and
			 * RoomProbe's -glossfield picture (0 in the engine). The plain
			 * draw call after it writes its ripple back. Nothing else is
			 * written per glossy draw call: the light comes out of the field,
			 * per pixel, and the rows that find it went up with the room. */
			if (want_sheen)
			{
				const float want[4] = { range.sheenF0, range.sheenTint,
					range.sheenMask, (float)g_field_view };

				if (sheen_dirty || memcmp(want, sheen, sizeof(sheen)) != 0)
				{
					memcpy(sheen, want, sizeof(sheen));
					sheen_dirty = false;
					g_device->SetPixelShaderConstantF(36, sheen, 1);
				}

				ripple_dirty = true;
			}

			const float want_weight = additive_pass ? range.addWeight : 1.0f;
			const float want_period = additive_pass ? range.addPeriod : 0.0f;

			if (add_dirty || add[0] != want_weight || add[2] != want_period)
			{
				add[0] = want_weight;
				add[2] = want_period;
				add_dirty = false;
				g_device->SetPixelShaderConstantF(18, add, 1);
			}

			/* The ripple of this draw call. The three numbers are
			 * material 16's own (MaterialRipple), and zero everywhere else -
			 * so this compare is false for every draw call in nine of the
			 * fifteen levels and the constant is written once per pass. */
			if (!want_sheen && (ripple_dirty || ripple[0] != range.ripple[0] ||
				ripple[1] != range.ripple[1] || ripple[2] != range.ripple[2]))
			{
				ripple[0] = range.ripple[0];
				ripple[1] = range.ripple[1];
				ripple[2] = range.ripple[2];
				ripple_dirty = false;
				sheen_dirty = true;	// c36 holds the ripple now
				g_device->SetPixelShaderConstantF(36, ripple, 1);
			}

			if (range.ripple[1] != 0.0f)
				g_water_calls++;

			if (flat_dirty || flat[0] != range.flatLight)
			{
				flat[0] = range.flatLight;
				flat_dirty = false;
				g_device->SetVertexShaderConstantF(14, flat, 1);
			}

			/* The scroll of this draw call. The offset is taken
			 * modulo one page here rather than in the shader: a game running
			 * for an hour would otherwise hand a float a number in the
			 * thousands and spend its whole mantissa on the integer part,
			 * and the texture would visibly quantise. fmodf keeps it in
			 * (-1, 1), which is where a wrapped coordinate belongs.
			 *
			 * The address mode follows the scroll and nothing else: an HD
			 * page is one whole texture, not an atlas, so WRAP is safe, but
			 * a still surface whose UV sits exactly on 0 or 1 filters
			 * differently under the two modes - so the mode is switched only
			 * for the ranges that need it, and switched back. */
			const bool scroll = g_motion[3] != 0.0f &&
				(range.scrollU != 0.0f || range.scrollV != 0.0f);

			if (scroll != g_wrapping)
			{
				const DWORD mode = scroll ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;

				g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, mode);
				g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, mode);
				g_wrapping = scroll;
			}

			const float u = scroll ? fmodf(range.scrollU * g_motion[2], 1.0f) : 0.0f;
			const float v = scroll ? fmodf(range.scrollV * g_motion[2], 1.0f) : 0.0f;

			if (g_motion_dirty || u != g_motion[0] || v != g_motion[1])
			{
				g_motion[0] = u;
				g_motion[1] = v;
				g_motion_dirty = false;
				g_device->SetVertexShaderConstantF(13, g_motion, 1);
			}

			/* Into the glow's composition and sampling, or out of them - see
			 * the note above the loop. */
			if (additive_pass &&
				(range.material == HD_TRG_MATERIAL_GLOW) != glow_on)
			{
				glow_on = !glow_on;

				if (!g_glow_sum)
					g_device->SetRenderState(D3DRS_SRCBLEND,
						glow_on ? D3DBLEND_INVDESTCOLOR : D3DBLEND_ONE);

				if (glow_taps && glow_on)
				{
					g_device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &kept_taps);
					g_device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, glow_taps);
					g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
				}
				else if (glow_taps)
				{
					g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
					g_device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, kept_taps);
				}
			}

			g_device->SetTexture(0, page);

			// One-sided or two-sided - see RangeCulls.
			const DWORD want_cull = range.cull && g_cull_opaque
				? D3DCULL_CCW : D3DCULL_NONE;

			if (want_cull != cull_now)
			{
				cull_now = want_cull;
				g_device->SetRenderState(D3DRS_CULLMODE, cull_now);
			}

			// Into the gloss variant, after the writes - see the note above.
			if (want_sheen && !sheen_ps_on)
			{
				g_device->SetPixelShader(g_gloss_ps);
				sheen_ps_on = true;
			}

			// Pushed back in depth where this frame's answer says so - see
			// set_stand_matrix; a range drawn alone as HDRenderRoomStand asks.
			const bool want_behind = window == HD_WINDOW_STAND ? g_stand_pick_behind :
				range.stand && group.standWith[(size_t)range.stand - 1] == HD_STAND_BEHIND;

			if (want_behind != behind_on)
				behind_on = set_stand_matrix(wvp, want_behind);

			if (g_index32)
				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
					(UINT)g_vertex_count, range.start, range.count / 3);
			else
			{
				/* The 16 bit path. The indices of this range were written
				 * relative to its own lowest vertex, so the stream is moved
				 * there and the device is shown that window and no more -
				 * what a card that cannot address more than 65535 vertices
				 * needs. On a level
				 * small enough to fit in 16 bits anyway the base is simply
				 * the first vertex the range names and this costs one extra
				 * SetStreamSource per draw call. */
				g_device->SetStreamSource(0, g_vb,
					range.baseVertex * (UINT)sizeof(HDVertex), sizeof(HDVertex));
				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
					range.vertexSpan, range.start, range.count / 3);
			}

			g_draw_calls++;
			g_clear_calls += range.clear ? 1 : 0;
		}

		if (g_wrapping)
		{
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			g_wrapping = false;
		}

		if (cull_now != D3DCULL_NONE)
			g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

		// The next pass of this group draws with the matrix prepare wrote.
		if (behind_on)
			set_stand_matrix(wvp, false);

		// A pass that ended on a glow draw call leaves its states behind.
		if (glow_ps_on)
			g_device->SetPixelShader(g_ps);

		// And one that ended on a glossy draw call goes back to the plain
		// variant, before the next room's constants are written.
		if (sheen_ps_on)
			g_device->SetPixelShader(g_ps);

		if (glow_on)
		{
			if (!g_glow_sum)
				g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);

			if (glow_taps)
			{
				g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
				g_device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, kept_taps);
			}
		}

		if (pass == 2 || blendOnly)
		{
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		}
		else if (sky && pass == 1)
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}

	bool prepare(int group, const D3DMATRIX* world, const HDRoomLighting* lighting,
		float* wvp)
	{
		if (!g_in_frame || !g_vb || !g_ib)
			return false;

		if (group < 0 || group >= (int)g_groups.size())
			return false;

		const float* row = world ? (const float*)world : kIdentity;

		/* The view space the fog and the bulbs live in. The classic
		 * "stash" is the vertex through D3DMView, which is view * world - so
		 * that is what goes up, not the world matrix alone. In the engine the
		 * two are the same thing (the caller hands the finished D3DMView in
		 * and the view is identity); in the diagnostic tool they are not (the
		 * world is identity and the view is real), and a fog that only worked
		 * in one of the two would be a fog nobody could measure. */
		float worldview[16];

		multiply(row, g_view, worldview);
		multiply(row, g_viewproj, wvp);
		set_matrix_constant(0, wvp);
		set_worldview_constant(worldview);
		upload_gloss_rows(group, worldview);
		apply_room_lighting(lighting);

		/* c15, the shore box of this room - where the
		 * surface bit was flooded to, in room-local x and z, so that the
		 * vertex shader can let the swell die where the water meets the bank.
		 * A room with no water gets a box no vertex can be near the edge of,
		 * which makes the taper exactly 1. */
		{
			const HDGroup& g = g_groups[(size_t)group];
			float box[4] = { -1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f };

			if (!g_no_shore && lighting)
			{
				for (int s = 0; s < g.waterPlanes; s++)
				{
					if (g.waterY[s] < lighting->surfaceY - 0.5f ||
						g.waterY[s] > lighting->surfaceY + 0.5f)
						continue;

					box[0] = g.waterBox[s][0];
					box[1] = g.waterBox[s][1];
					box[2] = g.waterBox[s][2];
					box[3] = g.waterBox[s][3];
					break;
				}
			}

			g_device->SetVertexShaderConstantF(15, box, 1);
		}

		return true;
	}
}

/* ------------------------------------------------------ the water sheets --
 *
 * This project's own water surface. hd_render.h (HDWaterRoom) says where
 * the squares come from and hd_room.hlsl ("the sheet") what the shader does
 * with them; this block finds the squares, joins them into bodies, classes
 * every body, works out its look and builds and draws the grid. The numbers
 * in the tables are this project's own choices, made on RoomProbe pictures
 * of ANDREA2 and JOBY2 and on reference pictures supplied for this project,
 * except where a line names a measurement. */
namespace
{
	/* One class of body.
	 *
	 * lambda, height, angle  the four wave rows: wavelength and height in
	 *            units, direction in radians off the body's own direction,
	 *            spread so that no two rows run in step and the sum does
	 *            not read as a grid
	 * calm       the share of the deep-water speed the rows run at,
	 *            omega = calm * sqrt(g k) - see kWaterG
	 * current    a channel's flow along its long axis, units a second
	 * swell      the share of the wave height the vertex rises by (0: the
	 *            sheet lies flat and only its normals move)
	 * taper      the distance from the shore over which the swell grows in
	 * absorb     the path through the water that leaves 1/e of the floor's
	 *            light: a clear basin, a murky channel, a dark sea
	 * foam, band the shore foam's strength and its width in units
	 * grain      the scale of the foam's blotch pattern, the basin's 1
	 * caps, capAt  the whitecaps' strength, and the share of the whole wave
	 *            height above which a crest breaks
	 * specExp, specStrength  the highlight's Blinn-Phong exponent, strength
	 * sunElev, sunAzim  the highlight's light, degrees. TR5 has no sun:
	 *            indoors it stands high, as the lamps and light shafts of
	 *            the Rome halls hang overhead; out of doors low, the
	 *            brightest band of the JOBY2 dome being its horizon
	 *            (RoomProbe -skyonly)
	 * baseAngle  the body's own direction for a basin and a sea; a channel
	 *            takes its long axis */
	struct HDWaterClass
	{
		const char* name;
		float lambda[4];
		float height[4];
		float angle[4];
		float calm;
		float current;
		float swell;
		float taper;
		float absorb;
		float foam;
		float band;
		float grain;
		float caps;
		float capAt;
		float specExp;
		float specStrength;
		float sunElev, sunAzim;
		float baseAngle;
	};

	/* A basin is a calm fountain: short, low, slow ripples in clear water
	 * and no foam. A channel is a sewer: livelier, carried by its current,
	 * murky, foam along its banks. A sea is open water: long, high waves,
	 * foam at every edge and on the crests. The foam blotches of a channel
	 * and a sea are 1.5 times finer than the basin's scale. */
	const HDWaterClass kWaterClass[HD_WATER_CLASSES] =
	{
		{ "basin", { 260.0f, 150.0f, 85.0f, 47.0f },
			{ 2.0f, 1.4f, 0.8f, 0.45f }, { 0.0f, 1.13f, -0.79f, 2.51f },
			0.6f, 0.0f, 0.0f, 512.0f, 4000.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			1.0f, 90.0f, 0.8f, 55.0f, 40.0f, 0.3f },
		{ "channel", { 420.0f, 230.0f, 120.0f, 64.0f },
			{ 3.0f, 1.8f, 0.9f, 0.45f }, { 0.0f, 0.42f, -0.37f, 0.9f },
			0.8f, 180.0f, 0.6f, 512.0f, 1500.0f, 0.7f, 300.0f, 1.5f, 0.0f,
			1.0f, 70.0f, 0.8f, 55.0f, 40.0f, 0.0f },
		{ "sea", { 1500.0f, 700.0f, 330.0f, 150.0f },
			{ 38.0f, 16.0f, 7.0f, 2.8f }, { 0.0f, 0.61f, -0.52f, 1.37f },
			1.0f, 0.0f, 1.0f, 1024.0f, 900.0f, 0.85f, 300.0f, 1.5f, 0.6f,
			0.5f, 90.0f, 0.5f, 22.0f, 200.0f, 0.6f },
	};

	/* The sections' colours, RGB 0..255: base the shallow water, deep the
	 * water where no floor shows through, high the highlight, foam the foam.
	 * Rome warm green and yellow, Russia cold blue and green, Ireland a
	 * neutral dark green and white, VCI a dark blue. All four are lit by the
	 * body's light (water_look), so a dark sewer has dark water. */
	struct HDWaterPalette
	{
		const char* name;
		unsigned char base[3], deep[3], high[3], foam[3];
	};

	const HDWaterPalette kWaterPalette[HD_WATER_SECTIONS] =
	{
		{ "Rome", { 150, 170, 90 }, { 62, 98, 50 }, { 255, 232, 170 },
			{ 238, 232, 200 } },
		{ "Russia", { 96, 150, 160 }, { 28, 62, 80 }, { 205, 228, 255 },
			{ 240, 248, 255 } },
		{ "Ireland", { 100, 125, 98 }, { 34, 60, 44 }, { 235, 240, 230 },
			{ 248, 248, 242 } },
		{ "VCI", { 64, 92, 150 }, { 14, 28, 70 }, { 190, 210, 255 },
			{ 215, 225, 245 } },
	};

	/* What an open-air body reflects: the sky dome's colour at the horizon
	 * and overhead, out of RoomProbe -sky -skyonly pictures. JOBY2: the
	 * horizon the mean of the rows just above it, overhead chosen darker.
	 * ANDY1 and ANDY3, whose dome is one and the same dusk: the mean of the
	 * rows 6 to 40 pixels above the horizon toward four directions, and
	 * overhead the mean of the dome's clouds, which cover 41 % of the view
	 * straight up. Every level with an open-air body has a row; one without
	 * reflects its body's own light, like an indoor body. */
	struct HDWaterSky
	{
		const char* stem;
		unsigned char horizon[3], zenith[3];
	};

	const HDWaterSky kWaterSky[] =
	{
		{ "JOBY2", { 163, 165, 178 }, { 100, 104, 122 } },
		{ "ANDY1", { 113, 78, 67 }, { 11, 19, 32 } },
		{ "ANDY3", { 113, 78, 67 }, { 11, 19, 32 } },
	};

	/* The compiled override rows: level and room, then the class (-1 keeps
	 * the class), the tint of the shallow and deep colour, the scale of the
	 * wave heights and of the foam. A row tunes the whole body its room
	 * belongs to. The rows below pin the classes the rule gives today, so
	 * that a later change of the rule cannot move these places. */
	struct HDWaterOverride
	{
		const char* stem;
		int room;
		int cls;
		float tint[3];
		float waves;
		float foam;
	};

	const HDWaterOverride kWaterOverride[] =
	{
		{ "ANDREA2", 144, HD_WATER_BASIN, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "ANDREA2", 194, HD_WATER_BASIN, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "ANDREA2", 60, HD_WATER_CHANNEL, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "JOBY2", 42, HD_WATER_SEA, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "JOBY2", 143, HD_WATER_SEA, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
	};

	// Units a second squared: 9.81 m/s^2 at 2.3 mm a unit, Lara's 768
	// units standing for 1.77 m.
	const float kWaterG = 4300.0f;

	/* How a room's light becomes the water's: times the gain and never under
	 * the floor. The mean of colour1 + colour2 over a room's samples runs
	 * 0.09 to 0.55 over the water rooms of ANDREA2 and JOBY2 (RoomProbe
	 * -hdwater), because it counts the dark pool floor and the shadowed
	 * corners beside the lit walls; the gain holds the water to what the lit
	 * walls round it show. Both chosen on the pictures. */
	const float kWaterLightGain = 1.8f;
	const float kWaterLightFloor = 0.30f;

	struct WaterSquare
	{
		int room;
		int sx, sz;		// the square in its room
		int wx, wz, wy;		// world: the square's corner, the surface
		int localY;		// the surface, room local
		int depth;		// down to the floor below, units
		int open;		// its room is open air
		int sheet;		// it draws a sheet
		int body;
	};

	struct WaterBody
	{
		HDRenderWaterBody info;
		int minX, maxX, minZ, maxZ;	// world, the squares' corners
	};

	// One draw: the sheet squares of one body in one group.
	struct SheetRange
	{
		unsigned int start, tris, minVertex, vertices;
		int body;
	};

	// A body's constants: the four rows, the swell and the pixel rows.
	struct WaterLook
	{
		float rows[16];		// gWaveV and gWaveP: kx, kz, omega, height
		float swell[4];		// gSwell
		float pixel[8 * 4];	// c96 - c103
	};

	// An override row, the compiled table's or the file's.
	struct WaterRow
	{
		char stem[16];
		int room;
		int cls;		// -1 keeps the class
		float tint[3];
		float waves, foam;
		int file;		// 1 for a row of hd_water.txt
	};

	std::vector<WaterSquare> g_wsquares;
	std::vector<WaterBody> g_wbodies;
	std::vector<WaterLook> g_wlooks;
	std::vector<std::vector<SheetRange> > g_sheet_ranges;	// per group
	std::vector<int> g_group_body;		// per group, its largest body
	std::vector<float> g_caustic_top;	// per group, room local y
	std::unordered_set<unsigned long long> g_sheet_at;
	HDRenderWaterLevel g_wlevel = {};
	IDirect3DVertexBuffer9* g_sheet_vb = 0;
	IDirect3DIndexBuffer9* g_sheet_ib = 0;
	float g_basin_rows[16];
	bool g_basin_rows_set = false;
	/* The diagnostic tool's sheet timing: GPU timestamps round every sheet
	 * draw, waited for at once - a measurement that stalls the pipeline,
	 * never on in the engine. */
	bool g_sheet_timing = false;
	double g_sheet_gpu_ms = 0.0;
	int g_sheet_timed = 0;

	// A world square and a surface height, as one key.
	unsigned long long WaterKey(int wx, int wz, int wy)
	{
		return ((unsigned long long)((unsigned)(wx >> 10) & 0xFFFFu) << 32) |
			((unsigned long long)((unsigned)(wz >> 10) & 0xFFFFu) << 16) |
			(unsigned long long)(unsigned short)(short)wy;
	}

	// A square of a group, as one key.
	unsigned long long SheetKey(int group, int sx, int sz)
	{
		return ((unsigned long long)(unsigned)group << 32) |
			((unsigned long long)((unsigned)sx & 0xFFFFu) << 16) |
			(unsigned long long)((unsigned)sz & 0xFFFFu);
	}

	void release_water_sheets()
	{
		if (g_sheet_vb) { g_sheet_vb->Release(); g_sheet_vb = 0; }
		if (g_sheet_ib) { g_sheet_ib->Release(); g_sheet_ib = 0; }
		g_wsquares.clear();
		g_wbodies.clear();
		g_wlooks.clear();
		g_sheet_ranges.clear();
		g_group_body.clear();
		g_caustic_top.clear();
		g_sheet_at.clear();
		g_wlevel = HDRenderWaterLevel();
		g_wlevel.fileLines = -1;
	}

	/* The data rule for a body's class.
	 *
	 *   sea      any of its rooms open to the sky (ROOM_OUTSIDE or
	 *            ROOM_NOT_INSIDE, the flag game\hair.cpp reads for the wind)
	 *   channel  indoors, 16 squares or more, and narrow: twice the area
	 *            over the perimeter under 2 (a strip two squares wide comes
	 *            to 1.6 to 1.9, a pool four or more wide to 2.2 and over)
	 *   basin    everything else
	 *
	 * The thresholds are chosen on the bodies of ANDREA2 and JOBY2: the
	 * sewer under room 60, 44 squares at 1.83, is a channel; the fountain of
	 * rooms 144, 229 and 231, 36 at 2.25, a basin; JOBY2's water along the
	 * submarine, open air, a sea. */
	int WaterClassOf(const HDRenderWaterBody& b)
	{
		if (b.openAir)
			return HD_WATER_SEA;

		if (b.sectors >= 16 && b.perimeter > 0 &&
			2.0f * (float)b.sectors / (float)b.perimeter < 2.0f)
			return HD_WATER_CHANNEL;

		return HD_WATER_BASIN;
	}

	/* How deep the water stands over a square: the walk down through the
	 * rooms below to the first floor that is not a further water portal. A
	 * wall, a square outside the room or a floor at or above the surface
	 * is depth 0, and so is a walk that has not ended six rooms down. */
	int water_depth(const HDWaterRoom* rooms, int count, int at, int wx,
		int wz, int wy)
	{
		for (int hop = 0; hop < 6; hop++)
		{
			const HDWaterRoom& b = rooms[at];
			const int bx = (wx - b.originX) >> 10;
			const int bz = (wz - b.originZ) >> 10;

			if (!b.floor || !b.below || bx < 0 || bz < 0 || bx >= b.sizeX ||
				bz >= b.sizeZ)
				return 0;

			const int i = bx * b.sizeZ + bz;
			const int floor = b.floor[i];

			if (floor == -127 || floor * 256 <= wy)
				return 0;

			const int pit = b.below[i];

			if (pit >= 0 && pit < count &&
				(rooms[pit].flags & HD_WATER_ROOM_UNDERWATER))
			{
				at = pit;
				continue;
			}

			return floor * 256 - wy;
		}

		return 0;
	}

	// A room's own light: the mean of colour1 + colour2 over its samples,
	// 0..2 a channel - the scale the room shader multiplies its texels by.
	void group_light(int group, float* out)
	{
		out[0] = out[1] = out[2] = 0.5f;

		if (group < 0 || group >= (int)g_groups.size() ||
			g_groups[(size_t)group].samples.empty())
			return;

		const std::vector<HDRenderSample>& s = g_groups[(size_t)group].samples;
		double sum[3] = { 0, 0, 0 };

		for (size_t i = 0; i < s.size(); i++)
			for (int k = 0; k < 3; k++)
				sum[k] += (s[i].c1[k] + s[i].c2[k]) / 255.0;

		for (int k = 0; k < 3; k++)
			out[k] = (float)(sum[k] / (double)s.size());
	}

	// The four rows of a class, turned by `base`, heights times `waves`.
	void water_rows(const HDWaterClass& c, float base, float waves,
		float* rows, float* total)
	{
		*total = 0.0f;

		for (int i = 0; i < 4; i++)
		{
			const float k = 6.2831853f / c.lambda[i];
			const float th = base + c.angle[i];

			rows[i * 4 + 0] = k * cosf(th);
			rows[i * 4 + 1] = k * sinf(th);
			rows[i * 4 + 2] = c.calm * sqrtf(kWaterG * k) +
				k * c.current * cosf(c.angle[i]);
			rows[i * 4 + 3] = c.height[i] * waves;
			*total += rows[i * 4 + 3];
		}
	}

	// A body's constants out of its class, its section's colours, its light
	// and its level's sky.
	void water_look(WaterLook& out, const WaterBody& wb, int section,
		const char* stem)
	{
		const HDRenderWaterBody& b = wb.info;
		const HDWaterClass& c = kWaterClass[b.cls];
		const HDWaterPalette& pal = kWaterPalette[section];
		const float* amb = b.light;

		// The body's own direction: a channel's long axis, else the class's.
		float base = c.baseAngle;

		if (b.cls == HD_WATER_CHANNEL)
			base = wb.maxX - wb.minX >= wb.maxZ - wb.minZ ? 0.0f : 1.5707963f;

		float total = 0.0f;

		water_rows(c, base, b.waves, out.rows, &total);
		out.swell[0] = c.swell;
		out.swell[1] = c.taper;
		out.swell[2] = 0.0f;
		out.swell[3] = 0.0f;

		const HDWaterSky* sky = 0;

		for (size_t i = 0; i < sizeof(kWaterSky) / sizeof(kWaterSky[0]); i++)
			if (stem && !_stricmp(stem, kWaterSky[i].stem))
				sky = &kWaterSky[i];

		const bool open = b.openAir && sky;
		float* deep = out.pixel;
		float* shallow = out.pixel + 4;
		float* refl = out.pixel + 8;
		float* zen = out.pixel + 12;
		float* high = out.pixel + 16;
		float* sun = out.pixel + 20;
		float* foam = out.pixel + 24;
		float* misc = out.pixel + 28;

		for (int k = 0; k < 3; k++)
		{
			deep[k] = pal.deep[k] / 255.0f * amb[k] * b.tint[k];
			shallow[k] = pal.base[k] / 255.0f * amb[k] * b.tint[k];
			refl[k] = open ? sky->horizon[k] / 255.0f : amb[k] * 0.55f;
			zen[k] = open ? sky->zenith[k] / 255.0f : amb[k] * 0.30f;
			high[k] = pal.high[k] / 255.0f * c.specStrength *
				(open ? 1.0f : amb[k]);
			foam[k] = pal.foam[k] / 255.0f * (open ? 0.85f : amb[k] * 0.9f);
		}

		deep[3] = 1.0f / c.absorb;
		shallow[3] = c.grain;
		refl[3] = 0.02f;	// F0 of water, n = 1.33
		zen[3] = 0.0f;
		high[3] = c.specExp;

		const float el = c.sunElev * 0.017453293f;
		const float az = c.sunAzim * 0.017453293f;

		sun[0] = cosf(el) * cosf(az);
		sun[1] = -sinf(el);		// y counts down
		sun[2] = cosf(el) * sinf(az);
		sun[3] = c.capAt;
		foam[3] = c.foam * b.foam;
		misc[0] = c.band;
		misc[1] = c.caps * b.foam;
		misc[2] = 0.0f;			// the clock, per draw
		misc[3] = total;
	}

	// One row applied to every body its room has water in.
	bool apply_water_row(const WaterRow& row)
	{
		std::vector<int> hit;

		for (size_t i = 0; i < g_wsquares.size(); i++)
			if (g_wsquares[i].room == row.room && g_wsquares[i].body >= 0 &&
				std::find(hit.begin(), hit.end(), g_wsquares[i].body) ==
				hit.end())
				hit.push_back(g_wsquares[i].body);

		for (size_t i = 0; i < hit.size(); i++)
		{
			HDRenderWaterBody& b = g_wbodies[(size_t)hit[i]].info;

			if (row.cls >= 0)
				b.cls = row.cls;

			for (int k = 0; k < 3; k++)
				b.tint[k] = row.tint[k];

			b.waves = row.waves;
			b.foam = row.foam;
			b.source = row.file ? 2 : 1;
			b.sourceRoom = row.room;
		}

		return !hit.empty();
	}
}

void HDRenderSetWaterSheets(const HDWaterRoom* rooms, int count, int section,
	const char* stem, int sheets)
{
	release_water_sheets();

	if (!rooms || count <= 0 || g_groups.empty())
		return;

	if (section < 0 || section >= HD_WATER_SECTIONS)
		section = HD_WATER_ROME;

	g_wlevel.section = section;

	const int groups = (int)g_groups.size();

	g_caustic_top.assign((size_t)groups, -1.0e9f);
	g_group_body.assign((size_t)groups, -1);
	g_sheet_ranges.assign((size_t)groups, std::vector<SheetRange>());

	/* 1. The water squares. A square draws a sheet when sheets are asked
	 * for, its room draws its own HD group and the remaster has no flat art
	 * in that plane over it but its material 16 water. The room below gets
	 * the caustic from the surface down - the lowest surface, should two
	 * stand over it. */
	for (int r = 0; r < count; r++)
	{
		const HDWaterRoom& rm = rooms[r];

		if ((rm.flags & HD_WATER_ROOM_UNDERWATER) || !rm.floor || !rm.below)
			continue;

		for (int sx = 0; sx < rm.sizeX; sx++)
			for (int sz = 0; sz < rm.sizeZ; sz++)
			{
				const int at = sx * rm.sizeZ + sz;
				const int pit = rm.below[at];

				if (pit < 0 || pit >= count || rm.floor[at] == -127 ||
					!(rooms[pit].flags & HD_WATER_ROOM_UNDERWATER))
					continue;

				WaterSquare s = {};

				s.room = r;
				s.sx = sx;
				s.sz = sz;
				s.wx = rm.originX + sx * 1024;
				s.wz = rm.originZ + sz * 1024;
				s.wy = rm.floor[at] * 256;
				s.localY = s.wy - rm.originY;
				s.depth = water_depth(rooms, count, pit, s.wx, s.wz, s.wy);
				s.open = (rm.flags & HD_WATER_ROOM_OPEN) ? 1 : 0;
				s.body = -1;

				const int x0 = sx * 1024, z0 = sz * 1024;

				s.sheet = sheets && rm.drawn && r < groups &&
					!(HDRenderGroupFlatArt(r, s.localY, x0, z0, x0 + 1024,
					z0 + 1024) && !HDRenderGroupWaterArt(r, s.localY, x0, z0,
					x0 + 1024, z0 + 1024)) ? 1 : 0;
				g_wsquares.push_back(s);

				if (pit < groups)
				{
					const float top = (float)(s.wy - rooms[pit].originY);

					if (top > g_caustic_top[(size_t)pit])
						g_caustic_top[(size_t)pit] = top;
				}
			}
	}

	g_wlevel.sectors = (int)g_wsquares.size();

	for (int g = 0; g < groups; g++)
		g_wlevel.causticRooms += g_caustic_top[(size_t)g] > -1.0e8f ? 1 : 0;

	/* 2. The squares by world place and height. A flipmap pair stands at one
	 * place twice: the first answers the map and both draw. Two rooms that
	 * are no pair and hold one square at one height are counted. */
	std::unordered_map<unsigned long long, int> at;

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];
		const unsigned long long key = WaterKey(s.wx, s.wz, s.wy);
		const std::unordered_map<unsigned long long, int>::const_iterator f =
			at.find(key);

		if (f == at.end())
		{
			at[key] = (int)i;
			continue;
		}

		const int other = g_wsquares[(size_t)f->second].room;

		if (other != s.room && rooms[s.room].flip != other &&
			rooms[other].flip != s.room)
			g_wlevel.overlaps++;
	}

	// 3. The bodies: four-connected at one height, across rooms.
	static const int step[4][2] =
		{ { 1024, 0 }, { -1024, 0 }, { 0, 1024 }, { 0, -1024 } };

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& seed = g_wsquares[i];

		if (seed.body >= 0 || at[WaterKey(seed.wx, seed.wz, seed.wy)] != (int)i)
			continue;		// a body's already, or a twin, below

		WaterBody b = {};

		b.minX = b.minZ = 1 << 30;
		b.maxX = b.maxZ = -(1 << 30);
		b.info.surfaceY = seed.wy;
		b.info.sourceRoom = -1;
		b.info.tint[0] = b.info.tint[1] = b.info.tint[2] = 1.0f;
		b.info.waves = b.info.foam = 1.0f;

		const int id = (int)g_wbodies.size();
		std::vector<size_t> stack(1, i);

		g_wsquares[i].body = id;

		while (!stack.empty())
		{
			const WaterSquare s = g_wsquares[stack.back()];

			stack.pop_back();
			b.info.sectors++;
			b.info.openAir |= s.open;
			b.minX = s.wx < b.minX ? s.wx : b.minX;
			b.maxX = s.wx > b.maxX ? s.wx : b.maxX;
			b.minZ = s.wz < b.minZ ? s.wz : b.minZ;
			b.maxZ = s.wz > b.maxZ ? s.wz : b.maxZ;

			for (int n = 0; n < 4; n++)
			{
				const std::unordered_map<unsigned long long, int>::
					const_iterator f = at.find(WaterKey(s.wx + step[n][0],
					s.wz + step[n][1], s.wy));

				if (f == at.end())
				{
					b.info.perimeter++;
					continue;
				}

				if (g_wsquares[(size_t)f->second].body < 0)
				{
					g_wsquares[(size_t)f->second].body = id;
					stack.push_back((size_t)f->second);
				}
			}
		}

		g_wbodies.push_back(b);
	}

	// The twins join the body of the square that answers their place.
	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		WaterSquare& s = g_wsquares[i];

		if (s.body < 0)
			s.body = g_wsquares[(size_t)at[WaterKey(s.wx, s.wz, s.wy)]].body;
	}

	/* 4. Per body: its rooms, its squares that draw, its deepest square, the
	 * open air a twin may add, and its light - the rooms' lights weighted by
	 * their squares, over the rooms that draw HD. */
	std::vector<double> light(g_wbodies.size() * 3, 0.0);
	std::vector<int> lit(g_wbodies.size(), 0);
	std::vector<std::vector<int> > roomsOf(g_wbodies.size());

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];
		HDRenderWaterBody& b = g_wbodies[(size_t)s.body].info;

		b.sheets += s.sheet;
		b.openAir |= s.open;
		b.depthMax = s.depth > b.depthMax ? s.depth : b.depthMax;
		roomsOf[(size_t)s.body].push_back(s.room);

		if (!rooms[s.room].drawn || s.room >= groups)
			continue;

		float l[3];

		group_light(s.room, l);

		for (int k = 0; k < 3; k++)
			light[(size_t)s.body * 3 + k] += l[k];

		lit[(size_t)s.body]++;
	}

	for (size_t b = 0; b < g_wbodies.size(); b++)
	{
		HDRenderWaterBody& info = g_wbodies[b].info;
		std::vector<int>& list = roomsOf[b];

		std::sort(list.begin(), list.end());
		list.erase(std::unique(list.begin(), list.end()), list.end());
		info.roomCount = (int)list.size();

		for (size_t k = 0; k < list.size() && k < HD_WATER_BODY_ROOMS; k++)
			info.rooms[k] = list[k];

		for (int k = 0; k < 3; k++)
		{
			const float raw = lit[b] ?
				(float)(light[b * 3 + k] / (double)lit[b]) : 0.5f;
			const float l = raw * kWaterLightGain;

			info.light[k] = l < kWaterLightFloor ? kWaterLightFloor :
				l > 1.5f ? 1.5f : l;
		}

		info.ruleCls = WaterClassOf(info);
		info.cls = info.ruleCls;
		g_wlevel.sheets += info.sheets;
	}

	g_wlevel.bodies = (int)g_wbodies.size();

	/* 5. The override rows of this level: the compiled table's, then the
	 * file's, in their order, so a row of the file wins over one of the
	 * table. */
	std::vector<WaterRow> rows;

	for (size_t o = 0; o < sizeof(kWaterOverride) / sizeof(kWaterOverride[0]);
		o++)
	{
		const HDWaterOverride& w = kWaterOverride[o];
		WaterRow row = {};

		strcpy_s(row.stem, sizeof(row.stem), w.stem);
		row.room = w.room;
		row.cls = w.cls;

		for (int k = 0; k < 3; k++)
			row.tint[k] = w.tint[k];

		row.waves = w.waves;
		row.foam = w.foam;
		rows.push_back(row);
	}

	for (size_t r = 0; r < rows.size(); r++)
	{
		if (!stem || _stricmp(stem, rows[r].stem))
			continue;

		if (apply_water_row(rows[r]))
			g_wlevel.rowsApplied++;
		else
			g_wlevel.rowsUnmatched++;
	}

	// 6. Every body's look, and every group's largest body.
	g_wlooks.assign(g_wbodies.size(), WaterLook());

	for (size_t b = 0; b < g_wbodies.size(); b++)
		water_look(g_wlooks[b], g_wbodies[b], section, stem);

	std::vector<std::vector<int> > squaresOf((size_t)groups);

	for (size_t i = 0; i < g_wsquares.size(); i++)
		if (g_wsquares[i].room < groups)
			squaresOf[(size_t)g_wsquares[i].room].push_back((int)i);

	for (int g = 0; g < groups; g++)
	{
		int best = 0;

		for (size_t b = 0; b < g_wbodies.size(); b++)
		{
			int n = 0;

			for (size_t k = 0; k < squaresOf[(size_t)g].size(); k++)
				n += g_wsquares[(size_t)squaresOf[(size_t)g][k]].body ==
					(int)b ? 1 : 0;

			if (n > best)
			{
				best = n;
				g_group_body[(size_t)g] = (int)b;
			}
		}
	}

	/* 7. The grid: 5 x 5 vertices a square, every 256 units, one range per
	 * body of a group. The vertex is the room vertex with colour1 read as
	 * the depth over 32 (red) and the distance to the nearest square with no
	 * water at this height, within three squares, over 16 (green); the
	 * depth is the mean of the squares that meet at the vertex, so the
	 * colour runs on across a square's edge. 16-bit indices: a level's
	 * squares stop at 65535 vertices, and those left over draw no sheet. */
	if (!sheets || !g_device || !g_water_vs || !g_water_ps)
		return;

	std::vector<HDVertex> verts;
	std::vector<unsigned short> idx;

	for (int g = 0; g < groups; g++)
		for (size_t b = 0; b < g_wbodies.size(); b++)
		{
			SheetRange range = {};

			range.start = (unsigned int)idx.size();
			range.minVertex = (unsigned int)verts.size();
			range.body = (int)b;

			for (size_t k = 0; k < squaresOf[(size_t)g].size(); k++)
			{
				const WaterSquare& s =
					g_wsquares[(size_t)squaresOf[(size_t)g][k]];

				if (!s.sheet || s.body != (int)b || verts.size() + 25 > 65535)
					continue;

				const HDWaterRoom& rm = rooms[s.room];
				const unsigned short first = (unsigned short)verts.size();

				for (int j = 0; j <= 4; j++)
					for (int i = 0; i <= 4; i++)
					{
						const int lx = s.sx * 1024 + i * 256;
						const int lz = s.sz * 1024 + j * 256;
						const int wx = rm.originX + lx;
						const int wz = rm.originZ + lz;
						float depth = 0.0f;
						int touching = 0;

						for (int q = 0; q < 4; q++)
						{
							const std::unordered_map<unsigned long long,
								int>::const_iterator f = at.find(WaterKey(
								wx + ((q & 1) ? 1 : -1),
								wz + ((q & 2) ? 1 : -1), s.wy));

							if (f == at.end())
								continue;

							depth += (float)g_wsquares[(size_t)f->second].depth;
							touching++;
						}

						depth = touching ? depth / (float)touching :
							(float)s.depth;

						float shore = 3072.0f;

						for (int qz = -3; qz <= 3; qz++)
							for (int qx = -3; qx <= 3; qx++)
							{
								const int x0 = ((wx >> 10) + qx) << 10;
								const int z0 = ((wz >> 10) + qz) << 10;

								if (at.count(WaterKey(x0, z0, s.wy)))
									continue;

								const float ex = (float)(wx < x0 ? x0 - wx :
									wx > x0 + 1024 ? wx - x0 - 1024 : 0);
								const float ez = (float)(wz < z0 ? z0 - wz :
									wz > z0 + 1024 ? wz - z0 - 1024 : 0);
								const float e = sqrtf(ex * ex + ez * ez);

								shore = e < shore ? e : shore;
							}

						const float dq = depth / 32.0f + 0.5f;
						const float sq = shore / 16.0f + 0.5f;
						HDVertex v = {};

						v.x = (short)lx;
						v.y = (short)s.localY;
						v.z = (short)lz;
						v.nx = 127;
						v.nz = 127;
						v.layer = (unsigned char)g_wbodies[b].info.cls;
						v.c1r = (unsigned char)(dq > 255.0f ? 255.0f : dq);
						v.c1g = (unsigned char)(sq > 255.0f ? 255.0f : sq);
						v.wSpare = 1;
						verts.push_back(v);
					}

				for (int j = 0; j < 4; j++)
					for (int i = 0; i < 4; i++)
					{
						const int a = first + j * 5 + i;

						idx.push_back((unsigned short)a);
						idx.push_back((unsigned short)(a + 1));
						idx.push_back((unsigned short)(a + 5));
						idx.push_back((unsigned short)(a + 1));
						idx.push_back((unsigned short)(a + 6));
						idx.push_back((unsigned short)(a + 5));
					}

				g_sheet_at.insert(SheetKey(g, s.sx, s.sz));
			}

			range.tris = ((unsigned int)idx.size() - range.start) / 3;
			range.vertices = (unsigned int)verts.size() - range.minVertex;

			if (range.tris)
				g_sheet_ranges[(size_t)g].push_back(range);
		}

	if (verts.empty() || idx.empty() ||
		FAILED(g_device->CreateVertexBuffer(
		(UINT)(verts.size() * sizeof(HDVertex)), D3DUSAGE_WRITEONLY, 0,
		D3DPOOL_MANAGED, &g_sheet_vb, 0)) ||
		FAILED(g_device->CreateIndexBuffer(
		(UINT)(idx.size() * sizeof(unsigned short)), D3DUSAGE_WRITEONLY,
		D3DFMT_INDEX16, D3DPOOL_MANAGED, &g_sheet_ib, 0)))
	{
		if (g_sheet_vb) { g_sheet_vb->Release(); g_sheet_vb = 0; }
		if (g_sheet_ib) { g_sheet_ib->Release(); g_sheet_ib = 0; }

		for (int g = 0; g < groups; g++)
			g_sheet_ranges[(size_t)g].clear();

		g_sheet_at.clear();
		return;
	}

	void* p = 0;

	if (SUCCEEDED(g_sheet_vb->Lock(0, 0, &p, 0)))
	{
		std::memcpy(p, &verts[0], verts.size() * sizeof(HDVertex));
		g_sheet_vb->Unlock();
	}

	if (SUCCEEDED(g_sheet_ib->Lock(0, 0, &p, 0)))
	{
		std::memcpy(p, &idx[0], idx.size() * sizeof(unsigned short));
		g_sheet_ib->Unlock();
	}

	g_wlevel.built = 1;
	g_wlevel.vertices = (int)verts.size();
	g_wlevel.triangles = (int)idx.size() / 3;
}

bool HDRenderWaterSheetAt(int group, int x0, int z0, int x1, int z1)
{
	if (g_sheet_at.empty())
		return false;

	const int sx = (int)floorf((float)(x0 + x1) * 0.5f / 1024.0f);
	const int sz = (int)floorf((float)(z0 + z1) * 0.5f / 1024.0f);

	return sx >= 0 && sz >= 0 && g_sheet_at.count(SheetKey(group, sx, sz));
}

bool HDRenderWaterSheetsAvailable()
{
	return g_water_vs && g_water_ps;
}

int HDRenderWaterBodies()
{
	return (int)g_wbodies.size();
}

bool HDRenderWaterBodyInfo(int at, HDRenderWaterBody* out)
{
	if (!out || at < 0 || at >= (int)g_wbodies.size())
		return false;

	*out = g_wbodies[(size_t)at].info;
	return true;
}

void HDRenderWaterLevelInfo(HDRenderWaterLevel* out)
{
	if (out)
		*out = g_wlevel;
}

const char* HDRenderWaterClassName(int cls)
{
	return cls >= 0 && cls < HD_WATER_CLASSES ? kWaterClass[cls].name : "?";
}

const char* HDRenderWaterSectionName(int section)
{
	return section >= 0 && section < HD_WATER_SECTIONS ?
		kWaterPalette[section].name : "?";
}

/* WATERLOG. The level line: the option, the section, the water squares and
 * of those the ones drawing a sheet, whether the sheets' buffers were
 * built, the bodies by class, hd_water.txt's lines (none without the file),
 * the rows it gave and the lines it could not read, the override rows of
 * this level that found a body and those that did not, the squares two
 * rooms that are no flipmap pair both hold, the underwater rooms with the
 * caustic, the sheets' vertices and triangles. A body line: its rooms
 * (+n for more than sixteen), the world y of its surface, its squares and
 * those drawing, twice the area over the perimeter, open air, the class
 * drawn and the rule's, the deepest square, the light its colours are lit
 * by, the override row that applied - table or file with its room - or
 * none, and the tint, wave and foam scales in force. */
bool HDRenderWaterLogLine(int body, const char* stem, int option, char* out,
	size_t size)
{
	if (!out || !size)
		return false;

	if (body < 0)
	{
		int cls[HD_WATER_CLASSES] = { 0, 0, 0 };
		char file[16];

		for (size_t b = 0; b < g_wbodies.size(); b++)
			cls[g_wbodies[b].info.cls]++;

		if (g_wlevel.fileLines >= 0)
			snprintf(file, sizeof(file), "%d", g_wlevel.fileLines);
		else
			snprintf(file, sizeof(file), "none");

		snprintf(out, size, "WATERLOG level=%s option=%d section=%s "
			"sectors=%d sheets=%d built=%d bodies=%d basin=%d channel=%d "
			"sea=%d file=%s rows=%d bad=%d applied=%d unmatched=%d "
			"overlaps=%d caustic_rooms=%d verts=%d tris=%d",
			stem ? stem : "?", option,
			HDRenderWaterSectionName(g_wlevel.section), g_wlevel.sectors,
			g_wlevel.sheets, g_wlevel.built, g_wlevel.bodies,
			cls[HD_WATER_BASIN], cls[HD_WATER_CHANNEL], cls[HD_WATER_SEA],
			file, g_wlevel.fileRows, g_wlevel.fileBad, g_wlevel.rowsApplied,
			g_wlevel.rowsUnmatched, g_wlevel.overlaps, g_wlevel.causticRooms,
			g_wlevel.vertices, g_wlevel.triangles);
		return true;
	}

	if (body >= (int)g_wbodies.size())
		return false;

	const HDRenderWaterBody& b = g_wbodies[(size_t)body].info;
	char list[128];
	char from[24];
	int at = 0;

	list[0] = 0;

	for (int k = 0; k < b.roomCount && k < HD_WATER_BODY_ROOMS; k++)
		at += snprintf(list + at, sizeof(list) - (size_t)at,
			k ? ",%d" : "%d", b.rooms[k]);

	if (b.roomCount > HD_WATER_BODY_ROOMS)
		snprintf(list + at, sizeof(list) - (size_t)at, ",+%d",
			b.roomCount - HD_WATER_BODY_ROOMS);

	if (b.source)
		snprintf(from, sizeof(from), "%s:%d", b.source == 2 ? "file" :
			"table", b.sourceRoom);
	else
		snprintf(from, sizeof(from), "none");

	snprintf(out, size, "WATERLOG body=%d rooms=%s surface=%d sectors=%d "
		"sheets=%d shape=%.2f open=%d class=%s rule=%s depth=%d "
		"light=%.2f,%.2f,%.2f override=%s tint=%.2f,%.2f,%.2f waves=%.2f "
		"foam=%.2f", body, list, b.surfaceY, b.sectors, b.sheets,
		b.perimeter ? 2.0 * b.sectors / b.perimeter : 0.0, b.openAir,
		HDRenderWaterClassName(b.cls), HDRenderWaterClassName(b.ruleCls),
		b.depthMax, b.light[0], b.light[1], b.light[2], from, b.tint[0],
		b.tint[1], b.tint[2], b.waves, b.foam);
	return true;
}

void HDRenderWaterTiming(int on, double* ms, int* draws)
{
	if (on >= 0)
	{
		g_sheet_timing = on ? true : false;
		g_sheet_gpu_ms = 0.0;
		g_sheet_timed = 0;
	}

	if (ms)
		*ms = g_sheet_gpu_ms;

	if (draws)
		*draws = g_sheet_timed;
}

bool HDRenderRoomMask(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes, int window)
{
	float wvp[16];

	if (!prepare(group, world, lighting, wvp))
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.triangles[0] && !room.triangles[1] && !room.triangles[2])
		return false;	// the .TRG has nothing here - that room stays classic

	/* A group past the last room is the sky, and its alpha pass is blended
	 * rather than cut - see the note in draw_pass. It is asked here because
	 * this is where the group number is still in hand. */
	const bool sky = group >= g_room_groups;

	/* The passes the caller asked for, in their own order. One prepare per
	 * call, so a room drawn in two phases pays two matrix multiplies and one
	 * light upload more - nothing against a frame's draw calls. */
	for (int pass = 0; pass < 3; pass++)
	{
		if (passes & (1u << pass))
			draw_pass(room, pass, wvp, false, window, sky);

		/* The clear floors, after the room's opaque and alpha passes and
		 * before its additive one - the place HD_PASS_CLEAR has in the
		 * frame, where every room's solid art comes first. */
		if (pass == 1 && (passes & HD_PASS_CLEAR) && room.clearRanges)
			draw_pass(room, 1, wvp, true, window, false, true);
	}

	/* And the see-through half of the alpha pass, which is a phase of
	 * its own because it must come after everything opaque - see
	 * MaterialBlend and the note on draw_pass's `blendOnly`. */
	if (passes & HD_PASS_BLEND)
		draw_pass(room, 1, wvp, true, window);

	return true;
}

bool HDRenderRoom(int group, const D3DMATRIX* world, const HDRoomLighting* lighting)
{
	return HDRenderRoomMask(group, world, lighting, HD_PASS_ALL);
}

bool HDRenderRoomStand(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes, int index, bool behind)
{
	if (group < 0 || group >= (int)g_groups.size() || index < 0 ||
		index >= (int)g_groups[(size_t)group].standRanges.size())
		return false;

	g_stand_pick = index;
	g_stand_pick_behind = behind;

	const bool drew = HDRenderRoomMask(group, world, lighting, passes,
		HD_WINDOW_STAND);

	g_stand_pick = -1;
	g_stand_pick_behind = false;
	return drew;
}

/* The sky dome.
 *
 * Every .TRG whose level has a sky ends in one group past the
 * last room - two in TITLE, none in JOBY3, JOBY5 and the two rooms-only
 * levels - and that group is the remaster's sky: a sphere of about four
 * blocks' radius wearing a 4096 x 2048 panorama (measured: 1,280 triangles,
 * 21 rings, radius 1060 at the poles and 4100 at the equator), plus one to
 * three flat cloud discs of up to 24,000 units' radius hanging at fixed
 * heights above the camera, which are material 1 and therefore already
 * scroll on their own parameters. The band's page is alpha 0 above the
 * horizon line and opaque below it, so the alpha pass cuts the void away and
 * leaves a picture band.
 *
 * Where it goes: the group is centred on the origin, so the caller hands in
 * the matrix the classic path builds for its own horizon mesh -
 * phd_TranslateAbs(camera.pos) with no rotation - and the dome rides on the
 * camera exactly as the horizon did. No scaling: the sphere's four blocks sit
 * far inside the far plane of 20480, and the cloud discs reach past it, which
 * only cuts the disc where the band already covers it.
 *
 * The depth: the sky must be behind everything and must sort inside
 * itself - the discs hang in front of the band overhead and behind it at the
 * horizon, and both are drawn in the opaque and the alpha pass, so there is
 * no draw order that gets both right. So it is drawn with its own depth range
 * instead: the viewport's MinZ is pushed to HD_SKY_MINZ for the length of
 * this call, which folds the whole sky into the last thousandth of the depth
 * buffer. Inside that slice the depth test sorts the sky properly; outside
 * it, every pixel of the world is nearer, because the classic depth of the
 * far plane itself is only 0.995 (SetupZRange(20, 20480): a - b/z = 0.99500)
 * and the deepest thing the engine can draw therefore never reaches the
 * slice. The viewport is read back and put together again before the call
 * returns. */
#define HD_SKY_MINZ	0.996f

int HDRenderSkyGroups()
{
	const int groups = (int)g_groups.size();

	return groups > g_room_groups ? groups - g_room_groups : 0;
}

bool HDRenderSky(const D3DMATRIX* world)
{
	if (!g_device || !HDRenderSkyGroups())
		return false;

	D3DVIEWPORT9 saved;

	if (FAILED(g_device->GetViewport(&saved)))
		return false;

	D3DVIEWPORT9 slice = saved;
	slice.MinZ = HD_SKY_MINZ;
	slice.MaxZ = 1.0f;
	g_device->SetViewport(&slice);

	bool drew = false;

	for (int g = g_room_groups; g < (int)g_groups.size(); g++)
		drew = HDRenderRoom(g, world, 0) || drew;

	g_device->SetViewport(&saved);
	return drew;
}

bool HDRenderRoomPass(int group, int pass, const D3DMATRIX* world,
	const HDRoomLighting* lighting)
{
	float wvp[16];

	if (pass < 0 || pass > 2 || !prepare(group, world, lighting, wvp))
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.triangles[pass])
		return false;

	draw_pass(room, pass, wvp);

	// The alpha pass's picture keeps its clear floors, blended as the frame
	// draws them; the glass stays out of it as before.
	if (pass == 1 && room.clearRanges)
		draw_pass(room, 1, wvp, true, HD_WINDOW_ALL, false, true);

	return true;
}

/* ------------------------------------- the projected shadow's receive -- */

namespace
{
	/* What the receive pass changes, as it was: the render states below,
	 * sampler stage 3's states and texture, and the pixel shader. The vertex
	 * shader, the declaration, stream 0 and the indices are set to the room
	 * path's own, which is what the frame's bracket holds while rooms draw. */
	const D3DRENDERSTATETYPE kReceiveRenderStates[] =
	{
		D3DRS_ZFUNC, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND,
		D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_STENCILENABLE, D3DRS_TWOSIDEDSTENCILMODE, D3DRS_STENCILFUNC,
		D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
		D3DRS_STENCILPASS, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL
	};

	const int kReceiveRenderStateCount =
		(int)(sizeof(kReceiveRenderStates) / sizeof(kReceiveRenderStates[0]));

	const D3DSAMPLERSTATETYPE kReceiveSamplerStates[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU,
		D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL
	};

	const int kReceiveSamplerStateCount =
		(int)(sizeof(kReceiveSamplerStates) / sizeof(kReceiveSamplerStates[0]));

	// The stage the map is sampled from (hd_shadow_receive.hlsl, sMap).
	const DWORD kReceiveStage = 3;

	// The stage the second map is sampled from (Receive2PS, sOthers).
	const DWORD kReceiveOthersStage = 4;

	/* What a receive's Begin found and End puts back. `two` is a receive with
	 * both maps; `samp2` and `texture2` are stage 4's, remembered only
	 * then. `blocks` are the receive's own pixel registers as Begin wrote
	 * them - her block and, in a receive with both maps, the second map's -
	 * which a mirror's draw (HDRenderShadowReceiveMirrorBegin) replaces and
	 * its End writes back; `mirror` is such a draw open. */
	struct ReceiveSaved
	{
		bool open = false;
		bool stencil = false;
		bool two = false;
		bool mirror = false;
		int calls = 0;
		float blocks[(HD_SHADOW_RECEIVE_COUNT + HD_SHADOW_RECEIVE2_COUNT) * 4] = {};
		DWORD rs[19] = {};
		DWORD samp[7] = {};
		DWORD samp2[7] = {};
		IDirect3DBaseTexture9* texture = 0;
		IDirect3DBaseTexture9* texture2 = 0;
		IDirect3DPixelShader9* ps = 0;
	};

	ReceiveSaved g_receive;

	/* The page a range shows this frame: draw_pass's flipbook and book
	 * choice, without its counters and its watch - the alpha test of a
	 * cut-out must see the page the picture saw. */
	int receive_page(const HDRange& range)
	{
		int page_slot = range.pageSlot;

		if (range.flipTime > 0.0f && g_motion[3] != 0.0f)
		{
			const float cycle = range.flipTime * 4.0f;
			const int frame = (int)(fmodf(g_motion[2], cycle) / range.flipTime);

			if (frame > 0 && frame < 4 && range.flipSlot[frame - 1] >= 0)
				page_slot = range.flipSlot[frame - 1];
		}

		if (range.bookTime > 0.0f && (g_motion[3] != 0.0f || g_book_page >= 0))
		{
			const int page = g_book_page >= 0
				? g_book_page % range.bookCount
				: HDBookPage(g_book_clock, range.bookTime, range.bookCount, range.bookPhase);

			if (page > 0 && range.bookFirst >= 0 &&
				(size_t)(range.bookFirst + page - 1) < g_book_slots.size())
				page_slot = g_book_slots[(size_t)(range.bookFirst + page - 1)];
		}

		return page_slot;
	}

	/* One pass of a group again, for the receive: the ranges draw_pass draws
	 * in that pass and phase, with every vertex constant draw_pass writes per
	 * range (gMotion, gFlat, the pushed-back matrix of HD_STAND_BEHIND)
	 * and its cull, so RoomVS puts each triangle on the very depth it wrote;
	 * and the page and the alpha reference, so a cut-out keeps its holes.
	 * `clearOnly` is the clear floors' half of the alpha pass, which writes
	 * depth too. Nothing of the pixel shader's own constants is touched here. */
	void draw_receive(const HDGroup& group, int pass, const float* wvp, int window,
		bool clearOnly)
	{
		if (group.pass[pass].empty())
			return;

		float constants[4] =
		{
			(pass == 1 && !clearOnly) ? 0.5f : 0.0f,
			0.0f,
			0.0f,
			1.0f
		};

		g_device->SetPixelShaderConstantF(4, constants, 1);

		float alpha_ref = constants[0];
		float flat[4] = { 0.0f, 0.0f, 0.0f, g_no_upright ? 0.0f : 1.0f };
		bool flat_dirty = true;
		DWORD cull_now = D3DCULL_NONE;
		bool behind_on = false;

		g_motion_dirty = true;

		for (const HDRange& range : group.pass[pass])
		{
			if ((range.blend != 0.0f) != clearOnly)
				continue;

			if (clearOnly && !range.clear)
				continue;

			const bool windowed = range.intruding &&
				!group.pairWhole[(size_t)range.intruding - 1];

			// A stand range takes the receive only where draw_pass drew it.
			if (window == HD_WINDOW_STAND)
			{
				if (range.stand != g_stand_pick + 1)
					continue;
			}
			else if (range.stand && !group.standWith[(size_t)range.stand - 1])
				continue;

			if ((window == HD_WINDOW_OPEN && windowed) ||
				(window == HD_WINDOW_INTRUDING && !windowed))
				continue;

			if ((window == HD_WINDOW_INSIDE && range.overhang) ||
				(window == HD_WINDOW_OVERHANG && !range.overhang))
				continue;

			if (range.destructId >= 0 && g_destruct[(size_t)range.destructId].hidden)
				continue;

			// And at the depth draw_pass drew it at.
			const bool want_behind = window == HD_WINDOW_STAND ? g_stand_pick_behind :
				range.stand && group.standWith[(size_t)range.stand - 1] == HD_STAND_BEHIND;

			if (want_behind != behind_on)
				behind_on = set_stand_matrix(wvp, want_behind);

			IDirect3DTexture9* page = HDPageCacheSlotTexture(receive_page(range));

			if (!page)
				continue;

			if (pass == 0 && range.alphaRef != alpha_ref)
			{
				alpha_ref = range.alphaRef;
				constants[0] = range.alphaRef;
				g_device->SetPixelShaderConstantF(4, constants, 1);
			}

			if (flat_dirty || flat[0] != range.flatLight)
			{
				flat[0] = range.flatLight;
				flat_dirty = false;
				g_device->SetVertexShaderConstantF(14, flat, 1);
			}

			const bool scroll = g_motion[3] != 0.0f &&
				(range.scrollU != 0.0f || range.scrollV != 0.0f);

			if (scroll != g_wrapping)
			{
				const DWORD mode = scroll ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;

				g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, mode);
				g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, mode);
				g_wrapping = scroll;
			}

			const float u = scroll ? fmodf(range.scrollU * g_motion[2], 1.0f) : 0.0f;
			const float v = scroll ? fmodf(range.scrollV * g_motion[2], 1.0f) : 0.0f;

			if (g_motion_dirty || u != g_motion[0] || v != g_motion[1])
			{
				g_motion[0] = u;
				g_motion[1] = v;
				g_motion_dirty = false;
				g_device->SetVertexShaderConstantF(13, g_motion, 1);
			}

			g_device->SetTexture(0, page);

			const DWORD want_cull = range.cull && g_cull_opaque ? D3DCULL_CCW : D3DCULL_NONE;

			if (want_cull != cull_now)
			{
				cull_now = want_cull;
				g_device->SetRenderState(D3DRS_CULLMODE, cull_now);
			}

			if (g_index32)
				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
					(UINT)g_vertex_count, range.start, range.count / 3);
			else
			{
				g_device->SetStreamSource(0, g_vb,
					range.baseVertex * (UINT)sizeof(HDVertex), sizeof(HDVertex));
				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
					range.vertexSpan, range.start, range.count / 3);
			}

			g_receive.calls++;
		}

		if (g_wrapping)
		{
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			g_wrapping = false;
		}

		if (cull_now != D3DCULL_NONE)
			g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

		if (behind_on)
			set_stand_matrix(wvp, false);
	}

	// A depth surface with a stencil to guard the pixels with.
	bool depth_has_stencil()
	{
		IDirect3DSurface9* depth = 0;
		D3DSURFACE_DESC desc;
		bool stencil = false;

		if (SUCCEEDED(g_device->GetDepthStencilSurface(&depth)) && depth)
		{
			if (SUCCEEDED(depth->GetDesc(&desc)))
				stencil = desc.Format == D3DFMT_D24S8 || desc.Format == D3DFMT_D24FS8 ||
					desc.Format == D3DFMT_D24X4S4 || desc.Format == D3DFMT_D15S1;

			depth->Release();
		}

		return stencil;
	}
}

bool HDRenderShadowReceiveReady()
{
	return g_shadow_receive_ps != 0;
}

bool HDRenderShadowReceive2Ready()
{
	return g_shadow_receive2_ps != 0;
}

/* One map's sampler on a stage, as the receive reads it: point sampled,
 * clamped, no sRGB conversion, the top level only. */
static void receive_stage(DWORD stage, IDirect3DTexture9* map)
{
	g_device->SetTexture(stage, map);
	g_device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	g_device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
	g_device->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
}

/* The receive's Begin for one map (`others` null: ReceivePS, her map on
 * stage 3) or for both (Receive2PS, the second map on stage 4 and its
 * registers from HD_SHADOW_RECEIVE2_FIRST on). The one-map receive makes
 * exactly the device calls it makes without the second map; the two-map one
 * makes them and, beside them, the second map's. */
static bool receive_open(const float* constants, const float* others)
{
	const bool two = others != 0;
	IDirect3DPixelShader9* shader = two ? g_shadow_receive2_ps : g_shadow_receive_ps;

	if (!g_device || !g_in_frame || g_receive.open || g_shadow_saved.open ||
		!shader || !g_shadow_texture || (two && !g_others_texture) || !constants || !g_vs ||
		!g_decl || !g_vb || !g_ib)
		return false;

	ReceiveSaved& s = g_receive;

	s = ReceiveSaved();

	// The rooms' windows are done; the receive sets each room's again.
	HDRenderScissorOff();

	for (int i = 0; i < kReceiveRenderStateCount; i++)
		g_device->GetRenderState(kReceiveRenderStates[i], &s.rs[i]);

	for (int i = 0; i < kReceiveSamplerStateCount; i++)
		g_device->GetSamplerState(kReceiveStage, kReceiveSamplerStates[i], &s.samp[i]);

	g_device->GetTexture(kReceiveStage, &s.texture);

	if (two)
	{
		for (int i = 0; i < kReceiveSamplerStateCount; i++)
			g_device->GetSamplerState(kReceiveOthersStage, kReceiveSamplerStates[i],
				&s.samp2[i]);

		g_device->GetTexture(kReceiveOthersStage, &s.texture2);
	}

	g_device->GetPixelShader(&s.ps);
	s.open = true;
	s.two = two;
	std::memcpy(s.blocks, constants, sizeof(float) * HD_SHADOW_RECEIVE_COUNT * 4);

	if (two)
		std::memcpy(s.blocks + HD_SHADOW_RECEIVE_COUNT * 4, others,
			sizeof(float) * HD_SHADOW_RECEIVE2_COUNT * 4);

	g_device->SetVertexShader(g_vs);
	g_device->SetVertexDeclaration(g_decl);
	g_device->SetStreamSource(0, g_vb, 0, sizeof(HDVertex));
	g_device->SetIndices(g_ib);
	g_device->SetPixelShader(shader);
	g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE_FIRST, constants,
		HD_SHADOW_RECEIVE_COUNT);

	if (two)
		g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE2_FIRST, others,
			HD_SHADOW_RECEIVE2_COUNT);

	receive_stage(kReceiveStage, g_shadow_texture);

	if (two)
		receive_stage(kReceiveOthersStage, g_others_texture);

	/* The picture times the factor, where the face is the one the room loop
	 * left visible: the same depth to the bit, EQUAL, nothing written but
	 * the colour. */
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_EQUAL);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	/* Once per pixel. Two faces of the room art can lie in one plane - a
	 * cut-out laid over a wall, a decal on a floor, the faces two rooms
	 * share at a portal - and both pass EQUAL; without a guard the factor
	 * would multiply twice there. The first face to darken a pixel marks it
	 * in the stencil and the test turns every later one away. Nothing but
	 * the two receives uses the stencil, so the whole of it is cleared here,
	 * and the models' receive later in the same bracket guards with a bit of
	 * its own (HDRenderShadowModelsBegin); a depth surface without one
	 * leaves the guard off. */
	s.stencil = depth_has_stencil();

	if (s.stencil)
	{
		g_device->Clear(0, 0, D3DCLEAR_STENCIL, 0, 1.0f, 0);
		g_receive_cleared = true;
		g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		g_device->SetRenderState(D3DRS_STENCILREF, 0);
		g_device->SetRenderState(D3DRS_STENCILMASK, 1);
		g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 1);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INVERT);
		g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
		g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	}
	else
		g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	return true;
}

bool HDRenderShadowReceiveBegin(const float* constants)
{
	return receive_open(constants, 0);
}

bool HDRenderShadowReceive2Begin(const float* constants, const float* others)
{
	return others ? receive_open(constants, others) : false;
}

bool HDRenderRoomShadow(int group, const D3DMATRIX* world, const HDRoomLighting* lighting,
	int window)
{
	float wvp[16];

	if (!g_receive.open || !prepare(group, world, lighting, wvp))
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.triangles[0] && !room.triangles[1])
		return false;

	draw_receive(room, 0, wvp, window, false);
	draw_receive(room, 1, wvp, window, false);

	if (room.clearRanges)
		draw_receive(room, 1, wvp, window, true);

	return true;
}

bool HDRenderRoomShadowStand(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, int index, bool behind)
{
	if (group < 0 || group >= (int)g_groups.size() || index < 0 ||
		index >= (int)g_groups[(size_t)group].standRanges.size())
		return false;

	g_stand_pick = index;
	g_stand_pick_behind = behind;

	const bool drew = HDRenderRoomShadow(group, world, lighting, HD_WINDOW_STAND);

	g_stand_pick = -1;
	g_stand_pick_behind = false;
	return drew;
}

bool HDRenderShadowReceiveMirrorReady()
{
	return g_shadow_receive_mirror_ps != 0;
}

bool HDRenderShadowReceiveMirrorBegin(const float* hers, const float* others,
	const float* plane)
{
	ReceiveSaved& s = g_receive;

	/* Only with the stencil guard: it is what keeps the ordinary draw that
	 * follows off the copy, which would otherwise take both lookups. */
	if (!g_device || !s.open || s.mirror || !s.stencil || !g_shadow_receive_mirror_ps ||
		!hers || !others || !plane)
		return false;

	g_device->SetPixelShader(g_shadow_receive_mirror_ps);
	g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE_FIRST, hers, HD_SHADOW_RECEIVE_COUNT);
	g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE2_FIRST, others,
		HD_SHADOW_RECEIVE2_COUNT);
	g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE_MIRROR, plane, 1);
	s.mirror = true;
	return true;
}

void HDRenderShadowReceiveMirrorEnd()
{
	ReceiveSaved& s = g_receive;

	if (!g_device || !s.open || !s.mirror)
		return;

	/* The receive's own shader and blocks again. In a receive with one map
	 * the second block's registers keep the mirror's values, which ReceivePS
	 * does not read, and c108 keeps the plane, which only this variant
	 * reads. */
	g_device->SetPixelShader(s.two ? g_shadow_receive2_ps : g_shadow_receive_ps);
	g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE_FIRST, s.blocks,
		HD_SHADOW_RECEIVE_COUNT);

	if (s.two)
		g_device->SetPixelShaderConstantF(HD_SHADOW_RECEIVE2_FIRST,
			s.blocks + HD_SHADOW_RECEIVE_COUNT * 4, HD_SHADOW_RECEIVE2_COUNT);

	s.mirror = false;
}

void HDRenderShadowReceiveEnd()
{
	ReceiveSaved& s = g_receive;

	if (!s.open || !g_device)
		return;

	HDRenderShadowReceiveMirrorEnd();

	for (int i = 0; i < kReceiveRenderStateCount; i++)
		g_device->SetRenderState(kReceiveRenderStates[i], s.rs[i]);

	for (int i = 0; i < kReceiveSamplerStateCount; i++)
		g_device->SetSamplerState(kReceiveStage, kReceiveSamplerStates[i], s.samp[i]);

	// The map leaves the stage: whatever was bound there before, which in the
	// HD path is nothing.
	g_device->SetTexture(kReceiveStage, s.texture);

	// The second map's stage the same way, after a receive with both maps.
	if (s.two)
	{
		for (int i = 0; i < kReceiveSamplerStateCount; i++)
			g_device->SetSamplerState(kReceiveOthersStage, kReceiveSamplerStates[i],
				s.samp2[i]);

		g_device->SetTexture(kReceiveOthersStage, s.texture2);
	}

	g_device->SetPixelShader(s.ps);

	if (s.texture) { s.texture->Release(); s.texture = 0; }
	if (s.texture2) { s.texture2->Release(); s.texture2 = 0; }
	if (s.ps) { s.ps->Release(); s.ps = 0; }

	s.open = false;
}

int HDRenderShadowReceiveCalls()
{
	return g_receive.calls;
}

bool HDRenderShadowReceiveStencil()
{
	return g_receive.stencil;
}

/* ------------------------------ the projected shadow on the HD models -- */

namespace
{
	/* What the models' receive changes, as it was: the receive's render
	 * states and the scissor test, sampler stages 3 and 4 with their
	 * textures, texture 0, the shaders, the declaration, stream 0, the
	 * indices, every vertex register ModelVS reads (c0 - c166) and the pixel
	 * registers the model draw and the receive write (c4 - c7, c96 - c107). */
	const int kModelsVertexRegisters = 167;
	const int kModelsPixelFirst = HD_SHADOW_RECEIVE_FIRST;
	const int kModelsPixelCount = HD_SHADOW_RECEIVE_COUNT + HD_SHADOW_RECEIVE2_COUNT;

	// The stencil bit the models' receive guards each pixel with.
	const DWORD kModelsStencilBit = 2;

	struct ModelsSaved
	{
		bool open = false;
		bool stencil = false;
		DWORD rs[19] = {};
		DWORD scissorTest = 0;
		DWORD samp[2][7] = {};
		IDirect3DBaseTexture9* texture[2] = { 0, 0 };
		IDirect3DBaseTexture9* page = 0;
		IDirect3DVertexShader9* vs = 0;
		IDirect3DPixelShader9* ps = 0;
		IDirect3DVertexDeclaration9* decl = 0;
		DWORD fvf = 0;
		IDirect3DVertexBuffer9* stream = 0;
		UINT streamOffset = 0;
		UINT streamStride = 0;
		IDirect3DIndexBuffer9* indices = 0;
		float vertex[kModelsVertexRegisters * 4] = {};
		float pass[4 * 4] = {};
		float blocks[kModelsPixelCount * 4] = {};
		// The two blocks as last uploaded, so an unchanged pair costs nothing.
		float now[kModelsPixelCount * 4] = {};
		bool nowValid = false;
	};

	ModelsSaved g_models;
}

bool HDRenderShadowModelsBegin(bool hers, bool others)
{
	if (!g_device || !g_in_frame || g_models.open || g_receive.open || g_shadow_saved.open ||
		(hers && !g_shadow_texture) || (others && !g_others_texture) || !(hers || others))
		return false;

	ModelsSaved& s = g_models;
	const DWORD stages[2] = { kReceiveStage, kReceiveOthersStage };

	s = ModelsSaved();

	for (int i = 0; i < kReceiveRenderStateCount; i++)
		g_device->GetRenderState(kReceiveRenderStates[i], &s.rs[i]);

	g_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &s.scissorTest);

	for (int k = 0; k < 2; k++)
	{
		for (int i = 0; i < kReceiveSamplerStateCount; i++)
			g_device->GetSamplerState(stages[k], kReceiveSamplerStates[i], &s.samp[k][i]);

		g_device->GetTexture(stages[k], &s.texture[k]);
	}

	g_device->GetTexture(0, &s.page);
	g_device->GetVertexShader(&s.vs);
	g_device->GetPixelShader(&s.ps);
	g_device->GetVertexDeclaration(&s.decl);
	g_device->GetFVF(&s.fvf);
	g_device->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
	g_device->GetIndices(&s.indices);
	g_device->GetVertexShaderConstantF(0, s.vertex, kModelsVertexRegisters);
	g_device->GetPixelShaderConstantF(4, s.pass, 4);
	g_device->GetPixelShaderConstantF(kModelsPixelFirst, s.blocks, kModelsPixelCount);
	s.open = true;

	// Every model's whole surface: the solid pass drew each without a window.
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

	/* A map this frame did not draw is bound to nothing: every entry's block
	 * of it is silenced (hd_shadow.h, HDShadowReceiverTakes), so the shader
	 * never samples it. */
	receive_stage(kReceiveStage, hers ? g_shadow_texture : 0);
	receive_stage(kReceiveOthersStage, others ? g_others_texture : 0);

	/* The picture times the factor where a model's face is the one the solid
	 * pass left visible: the same depth to the bit, EQUAL, nothing written
	 * but the colour - the rooms' receive's states. The cull mode is the
	 * draw's own, set per entry as the solid pass sets it. */
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_EQUAL);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	/* Once per pixel, as on the rooms: a model's faces can lie in one plane
	 * (a holster over the hip, a decal over a body) and both pass EQUAL. The
	 * rooms' receive leaves its bit set on the room pixels behind the
	 * models, so the models guard with a bit of their own, which that
	 * receive's clear left 0 everywhere; in a bracket whose rooms' receive
	 * cleared nothing the stencil is cleared here. */
	s.stencil = depth_has_stencil();

	if (s.stencil)
	{
		if (!g_receive_cleared)
		{
			g_device->Clear(0, 0, D3DCLEAR_STENCIL, 0, 1.0f, 0);
			g_receive_cleared = true;
		}

		g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		g_device->SetRenderState(D3DRS_STENCILREF, 0);
		g_device->SetRenderState(D3DRS_STENCILMASK, kModelsStencilBit);
		g_device->SetRenderState(D3DRS_STENCILWRITEMASK, kModelsStencilBit);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INVERT);
		g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
		g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	}
	else
		g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	return true;
}

void HDRenderShadowModelsBlocks(const float* hers, const float* others)
{
	ModelsSaved& s = g_models;

	if (!s.open || !hers || !others)
		return;

	float want[kModelsPixelCount * 4];

	std::memcpy(want, hers, sizeof(float) * HD_SHADOW_RECEIVE_COUNT * 4);
	std::memcpy(want + HD_SHADOW_RECEIVE_COUNT * 4, others,
		sizeof(float) * HD_SHADOW_RECEIVE2_COUNT * 4);

	if (s.nowValid && !std::memcmp(want, s.now, sizeof(want)))
		return;

	std::memcpy(s.now, want, sizeof(want));
	s.nowValid = true;
	g_device->SetPixelShaderConstantF(kModelsPixelFirst, want, kModelsPixelCount);
}

void HDRenderShadowModelsEnd()
{
	ModelsSaved& s = g_models;
	const DWORD stages[2] = { kReceiveStage, kReceiveOthersStage };

	if (!s.open || !g_device)
		return;

	for (int i = 0; i < kReceiveRenderStateCount; i++)
		g_device->SetRenderState(kReceiveRenderStates[i], s.rs[i]);

	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, s.scissorTest);

	for (int k = 0; k < 2; k++)
	{
		for (int i = 0; i < kReceiveSamplerStateCount; i++)
			g_device->SetSamplerState(stages[k], kReceiveSamplerStates[i], s.samp[k][i]);

		g_device->SetTexture(stages[k], s.texture[k]);
	}

	g_device->SetTexture(0, s.page);
	g_device->SetVertexShaderConstantF(0, s.vertex, kModelsVertexRegisters);
	g_device->SetPixelShaderConstantF(4, s.pass, 4);
	g_device->SetPixelShaderConstantF(kModelsPixelFirst, s.blocks, kModelsPixelCount);
	g_device->SetIndices(s.indices);
	g_device->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);

	// The declaration as HDRenderStateRestore puts it back.
	if (s.fvf)
		g_device->SetFVF(s.fvf);
	else
		g_device->SetVertexDeclaration(s.decl);

	g_device->SetVertexShader(s.vs);
	g_device->SetPixelShader(s.ps);

	for (int k = 0; k < 2; k++)
		if (s.texture[k]) { s.texture[k]->Release(); s.texture[k] = 0; }

	if (s.page) { s.page->Release(); s.page = 0; }
	if (s.vs) { s.vs->Release(); s.vs = 0; }
	if (s.ps) { s.ps->Release(); s.ps = 0; }
	if (s.decl) { s.decl->Release(); s.decl = 0; }
	if (s.stream) { s.stream->Release(); s.stream = 0; }
	if (s.indices) { s.indices->Release(); s.indices = 0; }

	s.open = false;
}

bool HDRenderShadowModelsStencil()
{
	return g_models.stencil;
}

int HDRenderGroupCount()
{
	return (int)g_groups.size();
}

int HDRenderGroupTriangles(int group, int pass)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	if (pass >= 0 && pass <= 2)
		return (int)g_groups[(size_t)group].triangles[pass];

	return (int)(g_groups[(size_t)group].triangles[0] +
		g_groups[(size_t)group].triangles[1] +
		g_groups[(size_t)group].triangles[2]);
}

int HDRenderGroupRanges(int group, int pass)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	if (pass >= 0 && pass <= 2)
		return (int)g_groups[(size_t)group].pass[pass].size();

	return (int)(g_groups[(size_t)group].pass[0].size() +
		g_groups[(size_t)group].pass[1].size() +
		g_groups[(size_t)group].pass[2].size());
}

bool HDRenderGroupMotionInfo(int group, HDRenderGroupMotion* out)
{
	if (!out || group < 0 || group >= (int)g_groups.size())
		return false;

	*out = g_groups[(size_t)group].motion;
	return true;
}

int HDRenderGroupSampleData(int group, HDRenderSample* out, int maxVerts)
{
	if (group < 0 || group >= (int)g_groups.size() || !out || maxVerts <= 0)
		return 0;

	const HDGroup& g = g_groups[(size_t)group];
	const int count = (int)g.samples.size() < maxVerts ? (int)g.samples.size() : maxVerts;

	for (int i = 0; i < count; i++)
		out[i] = g.samples[(size_t)i];

	return count;
}

// No caller. The / 3 dates from samples as 3 shorts per vertex: this returns a third.
int HDRenderGroupSampleCount(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return (int)(g_groups[(size_t)group].samples.size() / 3);
}

int HDRenderGroupSamples(int group, short* xyz, int maxVerts)
{
	if (group < 0 || group >= (int)g_groups.size() || !xyz || maxVerts <= 0)
		return 0;

	const HDGroup& g = g_groups[(size_t)group];
	const int count = (int)g.samples.size() < maxVerts ? (int)g.samples.size() : maxVerts;

	for (int i = 0; i < count; i++)
	{
		xyz[i * 3 + 0] = g.samples[(size_t)i].x;
		xyz[i * 3 + 1] = g.samples[(size_t)i].y;
		xyz[i * 3 + 2] = g.samples[(size_t)i].z;
	}

	return count;
}

bool HDRenderGroupBounds(int group, float* mins, float* maxs)
{
	if (group < 0 || group >= (int)g_groups.size())
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.hasBounds)
		return false;

	if (mins)
		std::memcpy(mins, room.mins, sizeof(room.mins));
	if (maxs)
		std::memcpy(maxs, room.maxs, sizeof(room.maxs));

	return true;
}

// For the log: of those, how many turned the opaque pass's alpha test on.
int HDRenderCutoutCalls()
{
	return g_cutout_calls;
}

// For the log: of those, how many drew a flipbook frame.
int HDRenderFlipbookCalls()
{
	return g_flip_calls;
}

// And how many turned a material 15 book.
int HDRenderBookCalls()
{
	return g_book_calls;
}

// And how many carried a gloss out of their material.
int HDRenderGlossCalls()
{
	return g_gloss_calls;
}

// And how many were drawn on the gloss variant.
int HDRenderSheenCalls()
{
	return g_sheen_calls;
}

void HDRenderGlossFile(const char* path)
{
	g_gloss_path = path ? path : "";
}

void HDRenderGlossOverride(int max)
{
	g_gloss_max = max ? true : false;
}

void HDRenderGlossLevelInfo(HDRenderGlossLevel* out)
{
	if (!out)
		return;

	std::unordered_set<int> pages;

	for (size_t i = 0; i < g_gloss_pages.size(); i++)
		pages.insert(g_gloss_pages[i].page);

	out->tableLines = HDGlossTableSize();
	out->filePages = g_gloss_file.present ? (int)g_gloss_file.entries.size() : -1;
	out->fileBad = g_gloss_file.bad;
	out->partsMat = g_gloss_parts_by[HD_GLOSS_FROM_MAT];
	out->partsSeed = g_gloss_parts_by[HD_GLOSS_FROM_SEED];
	out->partsTable = g_gloss_parts_by[HD_GLOSS_FROM_TABLE];
	out->partsFile = g_gloss_parts_by[HD_GLOSS_FROM_FILE];
	out->lamps = g_lamp_count;
	out->cubes = g_cube_count;
	out->pages = (int)pages.size();
	out->draws = g_sheen_ranges;

	for (int axis = 0; axis < 3; axis++)
		out->fieldDims[axis] = g_field_dir ? g_field_grid.dims[axis] : 0;

	out->fieldLit = g_field_dir ? g_field_stats.lit : 0;
	out->fieldWeak = g_field_dir ? g_field_stats.weak : 0;
	out->fieldBytes = g_field_bytes;
	out->fieldMs = g_field_ms;
	out->skipped = g_sheen_skipped;
}

void HDRenderGlossFieldView(int mode)
{
	g_field_view = mode == 1 || mode == 2 ? mode : 0;
}

void HDRenderGlossFieldSight(const unsigned char* nearby, int rooms)
{
	g_field_near.clear();
	g_field_near_rooms = 0;

	if (!nearby || rooms <= 0)
		return;

	g_field_near.assign(nearby, nearby + (size_t)rooms * (size_t)rooms);
	g_field_near_rooms = rooms;
}

int HDRenderGlossPageCount()
{
	return (int)g_gloss_pages.size();
}

bool HDRenderGlossPageInfo(int at, HDRenderGlossPage* out)
{
	if (!out || at < 0 || at >= (int)g_gloss_pages.size())
		return false;

	*out = g_gloss_pages[(size_t)at];
	return true;
}

// And how many drew a clear floor.
int HDRenderClearCalls()
{
	return g_clear_calls;
}

// For the log: of those, how many drew a still water surface.
int HDRenderWaterCalls()
{
	return g_water_calls;
}

/* ---------------------------------------------------------- the statics -- */

/* Whether this group draws anything in that horizontal plane.
 *
 * The question the classic water surface has to answer before it is drawn on
 * top of an HD room. `slack` is how far off the classic height the remaster's
 * own art may sit and still count as the same surface: measured, ANDY3's room
 * 17 carries its water at 10492 and 10494 where the classic plane is 10496,
 * and the next plane of that room is 256 units away - so anything from about
 * 8 to 100 gives the same answer everywhere, and 32 is the middle of it.
 *
 * The box test is an overlap and not a containment: the remaster remeshes a
 * room, and a plane it covers with one big triangle answers for every classic
 * quad inside it. */
/* The two questions share their walk; the caller picks the table. */
static bool GroupFlatArt(const std::vector<HDFlatPlane>& flats, int y,
	int x0, int z0, int x1, int z1, int slack)
{
	for (size_t i = 0; i < flats.size(); i++)
	{
		const HDFlatPlane& row = flats[i];
		const int d = row.y > y ? row.y - y : y - row.y;

		if (d > slack)
			continue;

		/* Half open in x and z: a face that runs from 2048 to 3072 lives in
		 * the sector at 2048 and not in the one that begins at 3072. */
		const int lastX = (x1 > x0 ? x1 - 1 : x0) >> 10;
		const int lastZ = (z1 > z0 ? z1 - 1 : z0) >> 10;

		for (int cx = x0 >> 10; cx <= lastX; cx++)
			for (int cz = z0 >> 10; cz <= lastZ; cz++)
				if (std::binary_search(row.cells.begin(), row.cells.end(),
					SectorKey(cx << 10, cz << 10)))
					return true;
	}

	return false;
}

bool HDRenderGroupFlatArt(int group, int y, int x0, int z0, int x1, int z1, int slack)
{
	if (group < 0 || group >= (int)g_groups.size())
		return false;

	return GroupFlatArt(g_groups[(size_t)group].flats, y, x0, z0, x1, z1, slack);
}

/* Whether the .TRG builds the water itself here.
 *
 * The same walk over the material 16 table alone. The distinction matters
 * because the flat-art question - "is there any horizontal art in this
 * plane" - is answered yes by a stone coping and by a pool floor as readily
 * as by a water surface, which is why it is taken by majority per room. This
 * one is answered by the remaster's own water and by nothing else, so a yes
 * is final for the face that asked: the classic surface would stand one to
 * twenty units from the .TRG's, and two water surfaces over one piece of
 * ground is worse than none. The rest of the room is decided
 * separately - see BuildWaterSurfaces for why the face and the room are two
 * questions and not one. */
bool HDRenderGroupWaterArt(int group, int y, int x0, int z0, int x1, int z1, int slack)
{
	if (group < 0 || group >= (int)g_groups.size())
		return false;

	return GroupFlatArt(g_groups[(size_t)group].waterFlats, y, x0, z0, x1, z1,
		slack);
}

int HDRenderGroupFlatPlanes(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return (int)g_groups[(size_t)group].flats.size();
}

bool HDRenderGroupHasClear(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return false;

	return g_groups[(size_t)group].clearRanges != 0;
}

int HDRenderGroupSheenRanges(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	int n = 0;

	for (int pass = 0; pass < 2; pass++)
		for (const HDRange& range : g_groups[(size_t)group].pass[pass])
			n += range.sheen > 0.0f ? 1 : 0;

	return n;
}

int HDRenderMirrorCount()
{
	return (int)g_mirrors.size();
}

bool HDRenderMirrorInfo(int at, HDMirror* out)
{
	if (!out || at < 0 || at >= (int)g_mirrors.size())
		return false;

	*out = g_mirrors[(size_t)at];
	return true;
}

bool HDRenderGroupHasGeometry(int group, const float* mins, const float* maxs)
{
	if (group < 0 || group >= (int)g_groups.size() || !mins || !maxs)
		return false;

	const std::vector<unsigned int>& cells = g_groups[(size_t)group].cells;

	if (cells.empty())
		return false;

	/* The box in 256-unit cells, one cell of slack on every side: a static's
	 * own box is the artist's visibility box and the remaster's mesh is not
	 * obliged to fill it to the last unit. Clamped to the short range the
	 * cell key can hold. */
	int lo[3], hi[3];

	for (int axis = 0; axis < 3; axis++)
	{
		float a = mins[axis];
		float b = maxs[axis];

		if (a > b)
		{
			const float t = a;
			a = b;
			b = t;
		}

		if (a < -32768.0f) a = -32768.0f;
		if (b > 32767.0f) b = 32767.0f;

		lo[axis] = ((int)a >> 8) - 1;
		hi[axis] = ((int)b >> 8) + 1;

		if (lo[axis] < -128) lo[axis] = -128;
		if (hi[axis] > 127) hi[axis] = 127;
		if (lo[axis] > hi[axis]) return false;
	}

	for (int x = lo[0]; x <= hi[0]; x++)
		for (int y = lo[1]; y <= hi[1]; y++)
		{
			const unsigned int base =
				((unsigned int)((x + 128) & 0x3FF) << 20) |
				((unsigned int)((y + 128) & 0x3FF) << 10);
			const unsigned int first = base | (unsigned int)((lo[2] + 128) & 0x3FF);
			const unsigned int last = base | (unsigned int)((hi[2] + 128) & 0x3FF);

			std::vector<unsigned int>::const_iterator at =
				std::lower_bound(cells.begin(), cells.end(), first);

			if (at != cells.end() && *at <= last)
				return true;
		}

	return false;
}

/* How much of a group's art stands in a room-local box.
 *
 * The counting form of the call above. It walks the group's own cell list
 * rather than the box's cells - the boxes asked about are whole rooms, and a
 * room's box holds tens of thousands of cells while a group's
 * list holds a few hundred - so the cost is the group's art and not the
 * question's size. No slack here, unlike HDRenderGroupHasGeometry: the caller
 * compares the count with the box's own size and a cell of slack on every
 * side would flatter a small box.
 *
 * `interiorOnly` asks the list built from the inside triangles alone. */
int HDRenderGroupCellsIn(int group, const float* mins, const float* maxs,
	int interiorOnly)
{
	if (group < 0 || group >= (int)g_groups.size() || !mins || !maxs)
		return 0;

	const std::vector<unsigned int>& cells = interiorOnly
		? g_groups[(size_t)group].cellsIn : g_groups[(size_t)group].cells;
	int found = 0;

	for (size_t i = 0; i < cells.size(); i++)
	{
		const unsigned int key = cells[i];
		const float p[3] =
		{
			(float)((int)((key >> 20) & 0x3FF) - 128) * 256.0f,
			(float)((int)((key >> 10) & 0x3FF) - 128) * 256.0f,
			(float)((int)(key & 0x3FF) - 128) * 256.0f
		};
		bool in = true;

		for (int axis = 0; axis < 3; axis++)
			if (p[axis] + 255.0f < mins[axis] || p[axis] > maxs[axis])
				in = false;

		if (in)
			found++;
	}

	return found;
}

/* How many of a static's own vertices stand in a 256-unit cell that holds a
 * vertex of the chosen parts of a group's art: `free` its overhang of no
 * intrusion piece and no stand range (`cellsFree`), and every stand range
 * whose flag in `take` is set. The vertices come in the group's own frame. */
static int GroupVertsIn(const HDGroup& group, const float* verts, int count,
	bool free, const std::vector<unsigned char>& take)
{
	int found = 0;

	for (int v = 0; v < count; v++)
	{
		const float* p = &verts[v * 3];

		if (p[0] < -32768.0f || p[0] > 32767.0f || p[1] < -32768.0f ||
			p[1] > 32767.0f || p[2] < -32768.0f || p[2] > 32767.0f)
			continue;

		const unsigned int key = CellKey((int)floorf(p[0]), (int)floorf(p[1]),
			(int)floorf(p[2]));
		bool in = free && std::binary_search(group.cellsFree.begin(),
			group.cellsFree.end(), key);

		for (size_t s = 0; !in && s < take.size() && s < group.standCells.size(); s++)
			in = take[s] && std::binary_search(group.standCells[s].begin(),
				group.standCells[s].end(), key);

		if (in)
			found++;
	}

	return found;
}

/* Whether a group-local point lies in a place's box moved into that group's
 * frame and grown by HD_STATIC_PIECE_SLACK on every side. */
static bool PlaceHolds(int place, float originX, float originZ, const float* p)
{
	const HDStaticPlace& box = g_places[place];
	const float q[3] = { p[0] + originX, p[1], p[2] + originZ };

	if (box.maxs[0] < box.mins[0])
		return false;

	for (int axis = 0; axis < 3; axis++)
		if (q[axis] < box.mins[axis] - HD_STATIC_PIECE_SLACK ||
			q[axis] > box.maxs[axis] + HD_STATIC_PIECE_SLACK)
			return false;

	return true;
}

/* The piece of a static held by other groups (hd_cover.h, over
 * HD_STATIC_PIECE_REACH): the triangles of `group` that make the object at
 * place `place`, as ranges of their own out of the piece index list. Taken are
 * the triangles the build noted at the place - the group's overhang of no
 * intrusion piece and its stand ranges a frame draws whenever the group is
 * listed (`usable`), less those lying whole on a classic room's faces, which
 * are that room's floor or wall - whose centroid lies in the static's box
 * (grown by HD_STATIC_PIECE_SLACK; of several such boxes the nearest centre's,
 * so no two pieces share a triangle and a static the art of its own room
 * holds keeps its triangles out of the piece of the static above it), and
 * those whose centroid lies in no static's box but whose connected run of
 * triangles (corners welded by position) has at least as many centroids in
 * this box as in all others - the leaf tips past the box. The piece is kept
 * only while it is the object alone - no run it reaches into goes on, outside
 * every static's box, past HD_STATIC_PIECE_OPEN (`pieceOpen` counts those
 * triangles) - and its own triangles hold the static as the group's art did
 * (HDStaticOtherHolds): `verts` in the group's frame. */
static void MakeStaticPiece(int place, int group, const std::vector<unsigned char>& usable,
	float originX, float originZ, const float* verts, int vertCount, HDStaticArt* out)
{
	const HDGroup& art = g_groups[(size_t)group];
	std::vector<int> mine;

	out->pieceGroup = group;

	for (size_t i = 0; i < g_place_tris.size(); i++)
	{
		const PlaceTri& tri = g_place_tris[i];

		if (tri.place != place || tri.group != group)
			continue;

		if (tri.stand > 0 && ((size_t)tri.stand > usable.size() ||
			!usable[(size_t)tri.stand - 1] ||
			art.standRanges[(size_t)tri.stand - 1].kind == 1))
			continue;

		mine.push_back((int)i);
	}

	if (mine.empty())
		return;

	/* Per triangle: 1 in this box, 2 in another static's box, 0 in none; and
	 * the runs, by a union over the welded corners. */
	const size_t n = mine.size();
	std::vector<unsigned char> home(n, 0);
	std::vector<int> parent(n);
	std::vector<std::pair<unsigned long long, int> > welded;

	for (size_t i = 0; i < n; i++)
	{
		const PlaceTri& tri = g_place_tris[(size_t)mine[i]];
		float centre[3];

		for (int axis = 0; axis < 3; axis++)
			centre[axis] = (tri.corner[axis] + tri.corner[3 + axis] +
				tri.corner[6 + axis]) * (1.0f / 3.0f);

		parent[i] = (int)i;

		/* The box that holds the centroid; of several, the one whose centre
		 * is nearest, the first place on a tie - so boxes that overlap share
		 * no triangle. */
		const unsigned int key = PlaceSector(centre[0] + originX, centre[2] + originZ);
		std::vector<std::pair<unsigned int, int> >::const_iterator it =
			std::lower_bound(g_place_sectors.begin(), g_place_sectors.end(),
			std::make_pair(key, -1));
		int best = -1;
		float bestDistance = 0.0f;

		for (; it != g_place_sectors.end() && it->first == key; ++it)
		{
			if (!PlaceHolds(it->second, originX, originZ, centre))
				continue;

			const HDStaticPlace& box = g_places[it->second];
			float distance = 0.0f;

			for (int axis = 0; axis < 3; axis++)
			{
				const float shift = axis == 0 ? originX : axis == 2 ? originZ : 0.0f;
				const float d = centre[axis] + shift - (box.mins[axis] + box.maxs[axis]) * 0.5f;

				distance += d * d;
			}

			if (best < 0 || distance < bestDistance ||
				(distance == bestDistance && it->second < best))
			{
				best = it->second;
				bestDistance = distance;
			}
		}

		home[i] = (unsigned char)(best < 0 ? 0 : best == place ? 1 : 2);

		for (int k = 0; k < 3; k++)
		{
			const float* c = &tri.corner[k * 3];
			const unsigned long long x = (unsigned long long)((long long)c[0] + (1 << 20)) & 0x1FFFFF;
			const unsigned long long y = (unsigned long long)((long long)c[1] + (1 << 20)) & 0x1FFFFF;
			const unsigned long long z = (unsigned long long)((long long)c[2] + (1 << 20)) & 0x1FFFFF;

			welded.push_back(std::make_pair((x << 42) | (y << 21) | z, (int)i));
		}
	}

	const auto root = [&](int i)
	{
		while (parent[(size_t)i] != i)
		{
			parent[(size_t)i] = parent[(size_t)parent[(size_t)i]];
			i = parent[(size_t)i];
		}

		return i;
	};

	std::sort(welded.begin(), welded.end());

	for (size_t w = 1; w < welded.size(); w++)
		if (welded[w].first == welded[w - 1].first)
		{
			const int a = root(welded[w].second);
			const int b = root(welded[w - 1].second);

			if (a != b)
				parent[(size_t)a] = b;
		}

	std::vector<int> here(n, 0), there(n, 0);

	for (size_t i = 0; i < n; i++)
	{
		const int r = root((int)i);

		if (home[i] == 1)
			here[(size_t)r]++;
		else if (home[i] == 2)
			there[(size_t)r]++;
	}

	std::vector<int> taken;
	const HDStaticPlace& box = g_places[place];

	for (size_t i = 0; i < n; i++)
	{
		const int r = root((int)i);

		if (home[i] == 1 || (home[i] == 0 && here[(size_t)r] > 0 &&
			here[(size_t)r] >= there[(size_t)r]))
			taken.push_back(mine[i]);

		/* A run that reaches into the box and, outside every static's box,
		 * on past HD_STATIC_PIECE_OPEN is a larger structure the object is
		 * part of - a hut's walls, a roof - and not the object alone. */
		if (home[i] != 0 || here[(size_t)r] <= 0)
			continue;

		const PlaceTri& tri = g_place_tris[(size_t)mine[i]];
		bool beyond = false;

		for (int k = 0; k < 3 && !beyond; k++)
			for (int axis = 0; axis < 3 && !beyond; axis++)
			{
				const float shift = axis == 0 ? originX : axis == 2 ? originZ : 0.0f;
				const float q = tri.corner[k * 3 + axis] + shift;

				beyond = q < box.mins[axis] - HD_STATIC_PIECE_OPEN ||
					q > box.maxs[axis] + HD_STATIC_PIECE_OPEN;
			}

		if (beyond)
			out->pieceOpen++;
	}

	// The static's vertices the piece's own cells hold.
	std::vector<unsigned int> cells;

	for (size_t i = 0; i < taken.size(); i++)
	{
		const PlaceTri& tri = g_place_tris[(size_t)taken[i]];

		for (int k = 0; k < 3; k++)
			cells.push_back(CellKey((int)tri.corner[k * 3], (int)tri.corner[k * 3 + 1],
				(int)tri.corner[k * 3 + 2]));
	}

	std::sort(cells.begin(), cells.end());
	cells.erase(std::unique(cells.begin(), cells.end()), cells.end());

	for (int v = 0; v < vertCount; v++)
	{
		const float* p = &verts[v * 3];

		if (std::binary_search(cells.begin(), cells.end(), CellKey((int)floorf(p[0]),
			(int)floorf(p[1]), (int)floorf(p[2]))))
			out->pieceHeld++;
	}

	if (taken.empty() || out->pieceOpen > 0 ||
		!HDStaticOtherHolds(out->pieceHeld, vertCount))
		return;

	// The ranges, in the order the group's own draw calls hold the triangles.
	std::sort(taken.begin(), taken.end(), [](int a, int b)
	{
		const PlaceTri& x = g_place_tris[(size_t)a];
		const PlaceTri& y = g_place_tris[(size_t)b];

		if (x.pass != y.pass) return x.pass < y.pass;
		if (x.range != y.range) return x.range < y.range;
		return x.at < y.at;
	});

	HDGroup piece;

	for (size_t i = 0; i < taken.size(); i++)
	{
		const PlaceTri& tri = g_place_tris[(size_t)taken[i]];

		if (tri.pass < 0 || tri.pass > 2 || tri.range < 0 ||
			(size_t)tri.range >= art.pass[tri.pass].size())
			continue;

		std::vector<HDRange>& list = piece.pass[tri.pass];
		const bool same = i > 0 && !list.empty() &&
			g_place_tris[(size_t)taken[i - 1]].pass == tri.pass &&
			g_place_tris[(size_t)taken[i - 1]].range == tri.range;

		if (!same)
		{
			HDRange range = art.pass[tri.pass][(size_t)tri.range];

			range.start = (unsigned int)g_piece_indices.size();
			range.count = 0;
			range.intruding = 0;
			range.stand = 0;
			list.push_back(range);

			if (range.clear)
				piece.clearRanges++;
		}

		for (int k = 0; k < 3; k++)
			g_piece_indices.push_back(tri.index[k]);

		list.back().count += 3;
		piece.triangles[tri.pass]++;
		out->pieceTris++;
	}

	out->piece = (int)g_static_pieces.size();
	g_static_pieces.push_back(piece);
	g_static_piece_group.push_back(group);
}

void HDRenderStaticArt(int room, const float* mins, const float* maxs,
	const float* verts, int vertCount, const HDStaticGroup* groups,
	int groupCount, int place, HDStaticArt* out)
{
	if (!out)
		return;

	std::memset(out, 0, sizeof(*out));
	out->answer = HD_STATIC_CLASSIC;
	out->carrier = -1;
	out->piece = -1;
	out->pieceGroup = -1;
	out->boxCells = 1;
	out->verts = vertCount > 0 ? vertCount : 0;

	for (int k = 0; k < HD_STATIC_MAX_GROUPS; k++)
		out->group[k] = -1;

	if (!mins || !maxs || !groups || room < 0 || room >= groupCount)
		return;

	for (int axis = 0; axis < 3; axis++)
	{
		const int lo = (int)mins[axis] >> 8;
		const int hi = (int)maxs[axis] >> 8;

		out->boxCells *= (hi - lo) + 1;
	}

	const HDStaticGroup& self = groups[room];
	const int count = (int)g_groups.size() < groupCount ? (int)g_groups.size() : groupCount;

	// 1. The room's own group, drawn in every frame the room is listed.
	if (self.hd && room < count)
		out->own = HDRenderGroupCellsIn(room, mins, maxs, 0);

	if (!verts || vertCount <= 0)
	{
		out->answer = HDStaticAnswer(out->own, out->boxCells, 0, 0, 0, 0,
			out->holds);
		return;
	}

	// The static's vertices in another group's frame: x and z from that
	// room's origin.
	std::vector<float> moved((size_t)vertCount * 3);

	const auto shifted = [&](int g)
	{
		const float dx = self.originX - groups[g].originX;
		const float dz = self.originZ - groups[g].originZ;

		for (int v = 0; v < vertCount; v++)
		{
			moved[(size_t)v * 3 + 0] = verts[v * 3 + 0] + dx;
			moved[(size_t)v * 3 + 1] = verts[v * 3 + 1];
			moved[(size_t)v * 3 + 2] = verts[v * 3 + 2] + dz;
		}
	};

	// 2. The carrier's stand ranges in this room that every frame listing
	// the room draws, with the carrier's group or in the room's window.
	const int carrier = self.carrier;

	if (!self.hd && self.state != HD_STAND_ROOM_CLASSIC && carrier >= 0 &&
		carrier < count && groups[carrier].hd)
	{
		const HDGroup& art = g_groups[(size_t)carrier];
		std::vector<unsigned char> take(art.standRanges.size(), 0);

		for (size_t s = 0; s < art.standRanges.size(); s++)
			take[s] = (unsigned char)(art.standRanges[s].holder == room &&
				HDStaticStandSure(self.state, true, art.standRanges[s].kind,
				art.standRanges[s].faces));

		shifted(carrier);
		out->carrier = carrier;
		out->sure = GroupVertsIn(art, &moved[0], vertCount, false, take);
	}

	/* 3. Every other group drawn HD: the art it draws in every frame that
	 * lists it, at the static's vertices. Kept the most vertices first, the
	 * lowest group first on a tie. Not for a flat static (HDStaticFlat). */
	out->flat = HDStaticFlat(mins, maxs) ? 1 : 0;

	for (int g = 0; g < count && !out->flat; g++)
	{
		if (g == room || !groups[g].hd)
			continue;

		const HDGroup& art = g_groups[(size_t)g];

		if (!art.hasBounds)
			continue;

		const float dx = self.originX - groups[g].originX;
		const float dz = self.originZ - groups[g].originZ;

		if (art.maxs[0] + 256.0f < mins[0] + dx || art.mins[0] > maxs[0] + dx + 256.0f ||
			art.maxs[1] + 256.0f < mins[1] || art.mins[1] > maxs[1] + 256.0f ||
			art.maxs[2] + 256.0f < mins[2] + dz || art.mins[2] > maxs[2] + dz + 256.0f)
			continue;

		std::vector<unsigned char> take(art.standRanges.size(), 0);

		for (size_t s = 0; s < art.standRanges.size(); s++)
		{
			const HDStandRange& range = art.standRanges[s];
			const bool known = range.holder >= 0 && range.holder < groupCount;
			const int state = known ? groups[range.holder].state : HD_STAND_ROOM_CLASSIC;
			const bool carries = known && groups[range.holder].carrier == g;

			take[s] = (unsigned char)HDStaticStandWithGroup(state, carries,
				range.kind, range.faces);
		}

		shifted(g);

		const int found = GroupVertsIn(art, &moved[0], vertCount, true, take);

		if (found <= 0)
			continue;

		int at = out->groupCount;

		while (at > 0 && out->groupVerts[at - 1] < found)
			at--;

		if (at >= HD_STATIC_MAX_GROUPS)
			continue;

		const int last = out->groupCount < HD_STATIC_MAX_GROUPS ?
			out->groupCount : HD_STATIC_MAX_GROUPS - 1;

		for (int k = last; k > at; k--)
		{
			out->group[k] = out->group[k - 1];
			out->groupVerts[k] = out->groupVerts[k - 1];
		}

		out->group[at] = g;
		out->groupVerts[at] = found;

		if (out->groupCount < HD_STATIC_MAX_GROUPS)
			out->groupCount++;
	}

	out->answer = HDStaticAnswer(out->own, out->boxCells, out->sure,
		out->groupVerts, out->groupCount, out->verts, out->holds);

	/* 4. The piece of a static other groups hold, out of the group that
	 * holds the most of its vertices - the first holder, the list being kept
	 * the most first and the lowest group first on a tie. */
	if (out->answer != HD_STATIC_GROUPS || place < 0 || place >= g_place_count)
		return;

	int holder = -1;

	for (int k = 0; k < out->groupCount && holder < 0; k++)
		if (out->holds[k])
			holder = out->group[k];

	if (holder < 0 || holder >= count)
		return;

	const HDGroup& art = g_groups[(size_t)holder];
	std::vector<unsigned char> usable(art.standRanges.size(), 0);

	for (size_t s = 0; s < art.standRanges.size(); s++)
	{
		const HDStandRange& range = art.standRanges[s];
		const bool known = range.holder >= 0 && range.holder < groupCount;
		const int state = known ? groups[range.holder].state : HD_STAND_ROOM_CLASSIC;
		const bool carries = known && groups[range.holder].carrier == holder;

		usable[s] = (unsigned char)HDStaticStandWithGroup(state, carries,
			range.kind, range.faces);
	}

	shifted(holder);
	MakeStaticPiece(place, holder, usable, groups[holder].originX,
		groups[holder].originZ, &moved[0], vertCount, out);
}

void HDRenderStaticPiecesWhole(const int* rooms, HDStaticArt* arts, int count)
{
	if (!rooms || !arts)
		return;

	for (int i = 0; i < count; i++)
	{
		// A static held by groups that asked for a piece and has none.
		if (arts[i].answer != HD_STATIC_GROUPS || arts[i].pieceGroup < 0 ||
			arts[i].piece >= 0)
			continue;

		for (int j = 0; j < count; j++)
			if (j != i && rooms[j] == rooms[i] &&
				arts[j].answer == HD_STATIC_GROUPS &&
				arts[j].pieceGroup == arts[i].pieceGroup)
				arts[j].pieceOthers++;
	}

	for (int i = 0; i < count; i++)
		if (arts[i].pieceOthers > 0)
			arts[i].piece = -1;
}

int HDRenderStaticPiecesUpload()
{
	/* What the pieces took is in g_piece_indices; the triangles noted for them
	 * are not needed again. */
	std::vector<PlaceTri>().swap(g_place_tris);

	if (g_piece_ib)
	{
		g_piece_ib->Release();
		g_piece_ib = 0;
	}

	if (!g_device || g_piece_indices.empty())
	{
		std::vector<unsigned int>().swap(g_piece_indices);
		return 0;
	}

	const size_t bytes = g_piece_indices.size() * (g_index32 ? 4u : 2u);
	void* raw = 0;

	if (FAILED(g_device->CreateIndexBuffer((UINT)bytes, D3DUSAGE_WRITEONLY,
		g_index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_piece_ib, 0)) ||
		FAILED(g_piece_ib->Lock(0, 0, &raw, 0)))
	{
		if (g_piece_ib)
		{
			g_piece_ib->Release();
			g_piece_ib = 0;
		}

		// Without the buffer no piece can be drawn: the statics keep the
		// classic fallback (HDRenderStaticPieceReady).
		std::vector<unsigned int>().swap(g_piece_indices);
		return 0;
	}

	if (g_index32)
		std::memcpy(raw, &g_piece_indices[0], bytes);
	else
	{
		unsigned short* to = (unsigned short*)raw;

		for (size_t i = 0; i < g_piece_indices.size(); i++)
			to[i] = (unsigned short)g_piece_indices[i];
	}

	g_piece_ib->Unlock();
	std::vector<unsigned int>().swap(g_piece_indices);
	return (int)g_static_pieces.size();
}

bool HDRenderStaticPieceReady(int piece)
{
	return g_piece_ib && piece >= 0 && piece < (int)g_static_pieces.size();
}

bool HDRenderStaticPiece(int piece, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes)
{
	float wvp[16];

	if (!HDRenderStaticPieceReady(piece) ||
		!prepare(g_static_piece_group[(size_t)piece], world, lighting, wvp))
		return false;

	const HDGroup& art = g_static_pieces[(size_t)piece];

	// The piece's own index list for its ranges, the level's back after them.
	g_device->SetIndices(g_piece_ib);

	for (int pass = 0; pass < 3; pass++)
	{
		if (passes & (1u << pass))
			draw_pass(art, pass, wvp);

		if (pass == 1 && (passes & HD_PASS_CLEAR) && art.clearRanges)
			draw_pass(art, 1, wvp, true, HD_WINDOW_ALL, false, true);
	}

	if (passes & HD_PASS_BLEND)
		draw_pass(art, 1, wvp, true);

	g_device->SetIndices(g_ib);
	return true;
}

bool HDRenderStaticPieceShadow(int piece, const D3DMATRIX* world,
	const HDRoomLighting* lighting)
{
	float wvp[16];

	if (!g_receive.open || !HDRenderStaticPieceReady(piece) ||
		!prepare(g_static_piece_group[(size_t)piece], world, lighting, wvp))
		return false;

	const HDGroup& art = g_static_pieces[(size_t)piece];

	g_device->SetIndices(g_piece_ib);
	draw_receive(art, 0, wvp, HD_WINDOW_ALL, false);
	draw_receive(art, 1, wvp, HD_WINDOW_ALL, false);

	if (art.clearRanges)
		draw_receive(art, 1, wvp, HD_WINDOW_ALL, true);

	g_device->SetIndices(g_ib);
	return true;
}

int HDRenderGroupOverhangTriangles(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	const HDGroup& g = g_groups[(size_t)group];

	return (int)(g.overhang[0] + g.overhang[1] + g.overhang[2]);
}

int HDRenderDestructibleCount()
{
	return (int)g_destruct.size();
}

bool HDRenderDestructibleInfo(int at, int* group, unsigned int* material,
	float* mins, float* maxs)
{
	if (at < 0 || at >= (int)g_destruct.size() || !g_destruct[(size_t)at].hasBox)
		return false;

	const HDDestructible& piece = g_destruct[(size_t)at];

	if (group) *group = piece.group;
	if (material) *material = piece.material;

	for (int axis = 0; axis < 3; axis++)
	{
		if (mins) mins[axis] = piece.mins[axis];
		if (maxs) maxs[axis] = piece.maxs[axis];
	}

	return true;
}

void HDRenderDestructibleHide(int at, bool hidden)
{
	if (at >= 0 && at < (int)g_destruct.size())
		g_destruct[(size_t)at].hidden = hidden;
}

int HDRenderDrawCalls()
{
	return g_draw_calls;
}

int HDRenderVertexBytes()
{
	return (int)sizeof(HDVertex);
}

/* ----------------------------------------------------------- projection -- */

void HDRenderZConstants(double mznear, double mzfar, double* a, double* b, double* boo)
{
	/* SetupZRange (specific\3dmath.cpp), line for line and in single
	 * precision, because that is what the classic path writes into the depth
	 * buffer and the aim is to match it, not to be more accurate than it. */
	const float f_mznear = (float)mznear;
	const float f_mzfar = (float)mzfar;

	float f_b = f_mzfar * f_mznear * 0.99f / (f_mznear - f_mzfar);
	const float f_a = 0.005f - f_b / f_mznear;
	f_b = -f_b;
	const float f_boo = f_b / 2048.0f;	// mone

	if (a) *a = f_a;
	if (b) *b = f_b;
	if (boo) *boo = f_boo;
}

void HDRenderMakeProjection(double persp, double centerX, double centerY,
	double a, double b, D3DMATRIX* out)
{
	if (!out)
		return;

	std::memset(out, 0, sizeof(*out));

	/* X and y. The classic path writes screen pixels straight into an
	 * XYZRHW vertex: sx = x * persp / z + centerx. The transformed path goes
	 * through the viewport instead: sx = (ndc + 1) * width / 2. With
	 * centerx = width / 2 the two are the same line as long as
	 * _11 = persp / centerx, and there is no half pixel anywhere - Direct3D 9
	 * puts pre-transformed and transformed vertices into the same screen
	 * space, so a shift here would be a shift against the classic path.
	 *
	 * Y is negated because screen y counts downwards and normalised device y
	 * counts upwards. */
	out->_11 = (float)(persp / centerX);
	out->_22 = (float)(-persp / centerY);

	/* Z. This is the whole reason the two paths can share one depth buffer:
	 *
	 *     classic   sz    = f_a - f_b / z
	 *     matrix    z_clip = f_a * z - f_b,  w_clip = z
	 *               depth  = z_clip / w_clip = f_a - f_b / z
	 *
	 * identical, not approximately. The classic range is 0.005 at the near
	 * plane and 0.995 at the far one - Core's own 1 % margin - and the matrix
	 * inherits it, so a viewport with MinZ 0 and MaxZ 1 needs no adjustment
	 * either. */
	out->_33 = (float)a;
	out->_43 = (float)-b;
	out->_34 = 1.0f;
	out->_44 = 0.0f;
}

double HDRenderSelfTest(double mznear, double mzfar, double persp,
	HDRenderZSample* rows, int rowCount)
{
	double a = 0, b = 0, boo = 0;
	HDRenderZConstants(mznear, mzfar, &a, &b, &boo);

	const float f_persp = (float)persp;
	const float f_moneopersp = 2048.0f / f_persp;
	const float f_a = (float)a;
	const float f_boo = (float)boo;

	const int samples = 50;
	double worst = 0;

	for (int i = 0; i < samples; i++)
	{
		/* Spread over the reciprocal, not over z: that is where a depth
		 * buffer's resolution lives, and where a mismatch would show first. */
		const double t = (double)i / (double)(samples - 1);
		const double inv = (1.0 / mznear) + t * ((1.0 / mzfar) - (1.0 / mznear));
		const double z = 1.0 / inv;

		// The classic path, in its own arithmetic (aRoomletTransformLight in
		// drawroom.cpp for the rhw, the clip routines of polyinsert.cpp for
		// the sz).
		const float zv = f_persp / (float)z;
		const float rhw = zv * f_moneopersp;
		const float classic = f_a - f_boo * rhw;

		// The device: z_clip / w_clip, then the viewport's MinZ 0 / MaxZ 1.
		const float z_clip = (float)a * (float)z + (float)(-b);
		const float w_clip = (float)z;
		const float matrix = z_clip / w_clip;

		const double diff = fabs((double)classic - (double)matrix);

		if (diff > worst)
			worst = diff;

		if (rows && i < rowCount)
		{
			rows[i].zView = z;
			rows[i].classic = classic;
			rows[i].matrix = matrix;
			rows[i].diff = diff;
		}
	}

	return worst;
}

unsigned long HDRenderPixelSlots()
{
	return (unsigned long)g_caps.MaxPixelShader30InstructionSlots;
}

void HDRenderDepthTruth(IDirect3DDevice9* device, unsigned long* zenable,
	unsigned long* zfunc, unsigned long* zwrite, float* minZ, float* maxZ)
{
	DWORD value = 0;
	D3DVIEWPORT9 viewport;

	if (zenable) *zenable = 0;
	if (zfunc) *zfunc = 0;
	if (zwrite) *zwrite = 0;
	if (minZ) *minZ = 0;
	if (maxZ) *maxZ = 0;

	if (!device)
		return;

	if (zenable && SUCCEEDED(device->GetRenderState(D3DRS_ZENABLE, &value)))
		*zenable = value;
	if (zfunc && SUCCEEDED(device->GetRenderState(D3DRS_ZFUNC, &value)))
		*zfunc = value;
	if (zwrite && SUCCEEDED(device->GetRenderState(D3DRS_ZWRITEENABLE, &value)))
		*zwrite = value;

	std::memset(&viewport, 0, sizeof(viewport));

	if (SUCCEEDED(device->GetViewport(&viewport)))
	{
		if (minZ) *minZ = viewport.MinZ;
		if (maxZ) *maxZ = viewport.MaxZ;
	}
}

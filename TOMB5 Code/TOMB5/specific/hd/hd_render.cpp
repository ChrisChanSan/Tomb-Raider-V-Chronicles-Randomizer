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
#include "hd_vis.h"
#include "hd_pagecache.h"
#include "hd_book.h"
#include "hd_scan.h"		// HDScanTakeTrg - the .TRG read once a load

#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include "hd_shader/hd_room_vs.inc"
#include "hd_shader/hd_room_ps.inc"
#include "hd_shader/hd_room_gloss_ps.inc"
#include "hd_shader/hd_room_water_vs.inc"
#include "hd_shader/hd_room_water_ps.inc"
#include "hd_shader/hd_room_water_depth_ps.inc"
#include "hd_shader/hd_room_water_view_ps.inc"
#include "hd_shader/hd_room_water_under_ps.inc"
#include "hd_shader/hd_room_water_foot_ps.inc"
#include "hd_shader/hd_room_water_river_ps.inc"
#include "hd_shader/hd_room_water_foam_ps.inc"
#include "hd_shader/hd_room_water_foam_depth_ps.inc"
#include "hd_shader/hd_water_look_vs.inc"	// the sea seen from below
#include "hd_shader/hd_water_look_ps.inc"
#include "hd_shader/hd_room_card_vs.inc"
#include "hd_shader/hd_room_card_ps.inc"
#include "hd_shader/hd_wet_ps.inc"
#include "hd_shader/hd_wet_debug_ps.inc"
#include "hd_shader/hd_wet_marble_ps.inc"
#include "hd_shader/hd_glow_ps.inc"
#include "hd_shader/hd_shadow_receive_ps.inc"
#include "hd_shader/hd_shadow_receive2_ps.inc"
#include "hd_shader/hd_shadow_receive_mirror_ps.inc"
#include "hd_gloss_math.h"
#include "hd_gloss_pages.h"
#include "hd_gloss_seed.h"
#include "hd_gloss_field.h"
#include "hd_shadow.h"			// HD_SHADOW_MAP, the projected shadow's map
#include "hd_splash_math.h"		// the water's cards
#include "hd_water_under.h"		// the eye's depth under the water
#include "hd_spring.h"			// a row's `spring` key
#include "hd_sky_cap.h"			// the sky band's closed poles

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <queue>
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
	 *       the sea of JOBY4 (HDWaterArt below) */
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
		 * every other material; its ripple is the water's (HDWaterArt). */
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
	 * it may move it before it walks off its own rectangle. The ripple's cap
	 * (HDArtRipple) is a tenth of that rectangle's smaller side; it is a
	 * bound on the travel and not a proof that the sample stays inside the
	 * rectangle, because a pixel already on the rim can be pushed that
	 * tenth over it.
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

	/* The remaster's still water art, part by part.
	 *
	 * Material 16 is a surface of standing water: its 28 parts are fountains,
	 * basins, mill ponds and pools, the soup in JOBY3's and JOBY5's kitchen
	 * pots, plus the two great seas of JOBY4 and JOBY5; 17 of them are a
	 * double sheet two units apart whose halves face up and down (the view out
	 * of the air and out of the water); and page 3255, which 90 % of the
	 * material's triangles wear, is a grey noise field with no hole in it.
	 * TrgProbe -mat 16 prints the four numbers the file gives each part; the
	 * ripple reads none of them.
	 *
	 * The ripple is a texture ripple and nothing else: the pixel shader moves
	 * the part's texture coordinates by a small travel along a slow wave
	 * (hd_room.hlsl, the note over HD_RIPPLE_OCTAVE), and nothing touches the
	 * vertex - `sway` is 0 on 27 of the 28 parts, and a sheet that bobbed would
	 * tear away from the stone rim it is let into. That structure - the travel
	 * held to the part's own UV span (PartUvSpan) and no movement in the
	 * vertex - is TROne's own design (Copyright 2026 Todesengel, GPLv3,
	 * TROne-main (copy of 2026-10-06, commit 75c4c843), its own water rule for
	 * material 16), and the same count here; the wave in hd_room.hlsl is a
	 * different curve with different terms.
	 *
	 * Its three numbers - the wave's scale, the travel and the rate - are this
	 * project's own, out of the water the part lies over (see "the art's
	 * ripple" over water_art_ripples): the body's class and squares, the
	 * class's longest wave row and calm in kWaterClass, its row's wave scale,
	 * the part's own span (HDArtRipple, hd_splash_math.h). Until the bodies are
	 * known - the build of the groups comes first, and RoomProbe without
	 * -hdwater never builds them - a part stands for its own water: the
	 * squares it covers and the class the data rule gives them indoors
	 * (WaterArtOf below).
	 *
	 * HDWaterArt holds one part as the ripple asks for it: its index in the
	 * group, its smaller UV span in page units, its box (room x and z, world
	 * y), the squares that box spans along x and z, and the squares its
	 * triangles that face up cover - never more than the box spans, so a sea
	 * the file stacks counts once. `body`, `cls`, `squares` and `ripple` are
	 * what the water gave it: the body under it or -1, the class, the squares
	 * and the three numbers of gRipple. */
	struct HDWaterArt
	{
		int part = -1;
		float span = 0.0f;
		int box[6] = { 0, 0, 0, 0, 0, 0 };	// x0 x1 z0 z1 y0 y1
		int boxX = 0, boxZ = 0;
		float area = 0.0f;
		int body = -1;
		int cls = 0;
		float squares = 0.0f;
		float ripple[3] = { 0.0f, 0.0f, 0.0f };
	};

	// With the water sheets, where the classes are (see "the art's ripple").
	int ArtRippleClass(int openAir, const HDWaterArt& art);
	bool ArtRippleOf(int cls, float squares, float waves, float span,
		float* out);

	/* One material 16 part as HDWaterArt, with the ripple it has until the
	 * bodies are known. False for every other material in the game, and for a
	 * part with no vertex; its ripple is then three zeroes and the pixel
	 * shader's one compare skips the whole thing. A vertex faces up where its
	 * normal's y byte is over 160, as TrgProbe counts it. */
	inline bool WaterArtOf(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part, int at, HDWaterArt& art)
	{
		art = HDWaterArt();
		art.part = at;

		if (MaterialBase(part) != HD_TRG_MATERIAL_WATER)
			return false;

		float spanU = 0.0f, spanV = 0.0f;

		if (!PartUvSpan(trg, part, spanU, spanV))
			return false;

		art.span = spanU < spanV ? spanU : spanV;

		bool any = false;
		double area = 0.0;

		for (int pass = 0; pass < 3; pass++)
		{
			const trf::remaster::TrgPart::Pass& run = part.pass[pass];

			for (unsigned int k = 0; k + 2 < run.count; k += 3)
			{
				const trf::remaster::TrgVertex* v[3];
				bool ok = true;

				for (int c = 0; c < 3 && ok; c++)
				{
					const unsigned int index = run.start + k + (unsigned int)c;

					ok = index < trg.indices.size() &&
						trg.indices[index] < trg.vertices.size();
					v[c] = ok ? &trg.vertices[trg.indices[index]] : 0;
				}

				if (!ok)
					continue;

				for (int c = 0; c < 3; c++)
				{
					const int p[3] = { v[c]->x, v[c]->z, v[c]->y };

					for (int a = 0; a < 3; a++)
					{
						if (!any || p[a] < art.box[a * 2])
							art.box[a * 2] = p[a];

						if (!any || p[a] > art.box[a * 2 + 1])
							art.box[a * 2 + 1] = p[a];
					}

					any = true;
				}

				if (v[0]->ny > 160 && v[1]->ny > 160 && v[2]->ny > 160)
					area += 0.5 * fabs(
						(double)(v[1]->x - v[0]->x) * (v[2]->z - v[0]->z) -
						(double)(v[2]->x - v[0]->x) * (v[1]->z - v[0]->z));
			}
		}

		if (!any)
			return false;

		art.boxX = (art.box[1] - art.box[0] + 1023) / 1024;
		art.boxZ = (art.box[3] - art.box[2] + 1023) / 1024;

		if (art.boxX < 1)
			art.boxX = 1;

		if (art.boxZ < 1)
			art.boxZ = 1;

		const float spans = (float)(art.boxX * art.boxZ);

		art.area = (float)(area / (1024.0 * 1024.0));

		if (art.area <= 0.0f || art.area > spans)
			art.area = spans;

		art.cls = ArtRippleClass(0, art);
		art.squares = art.area;
		ArtRippleOf(art.cls, art.squares, 1.0f, art.span, art.ripple);
		return true;
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

	/* MaterialBookSlots' answer for a run it has made already, -1 for none:
	 * the index fill's form of it, which writes nothing - the build makes
	 * every run before the fill, in the fill's own order. */
	inline int MaterialBookSlotsMade(int page0, int count)
	{
		for (size_t i = 0; i < g_book_runs.size(); i++)
			if (g_book_runs[i].page0 == page0 && g_book_runs[i].count == count)
				return g_book_runs[i].first;

		return -1;
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
		 * constant. See HDWaterArt above; `ripplePart` is the part's index
		 * in its group and -1 elsewhere, in the key too, so a part's ranges
		 * are its own and take the ripple of the water under it
		 * (water_art_ripples). */
		float ripple[3] = { 0.0f, 0.0f, 0.0f };
		int ripplePart = -1;
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
		/* A clear floor on a page of the mirror column, drawn opaque while
		 * the marble floors are on (see "the marble floors"). Set after the
		 * build and not in the sort key: the page is in the key already. */
		bool marbleOpaque = false;
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
		/* The remaster's own water art in this draw call: its triangles that
		 * are horizontal and either material 16 or water-marked at all three
		 * corners (the marks RoomVS swells), the lowest and the highest y of
		 * them and their box in room x and z. Not in the sort key - the draw
		 * calls stay what they are; a call that is wholly such art can be
		 * hidden under a sheet (HDRenderSetWaterSheets, "the art over a
		 * sheet"), which sets `artHidden` for the level. */
		int artTris = 0;
		int artY = 0, artYMax = 0;
		int artMinX = 0, artMaxX = 0, artMinZ = 0, artMaxZ = 0;
		bool artHidden = false;
		/* The floor squares that art lies over: the SectorKey of every
		 * square an art triangle's own box covers, half open like the
		 * flats' (HDFlatPlane), sorted and unique. The hide asks whether a
		 * sheet's square is one of them; the box above also spans every
		 * square between two pieces of art. `artSpared` is set where the
		 * box reaches a sheet's square and no art triangle lies over it -
		 * or, for a call of the flow material, no flow triangle (flowCells)
		 * - the level's art_kept. */
		std::vector<unsigned int> artCells;
		bool artSpared = false;
		/* Its horizontal triangles of the flow material (1): the remaster's
		 * moving water surface, which a river's sheet takes the place of -
		 * counted, with their heights and box, the way artTris is. */
		int flowTris = 0;
		int flowY = 0, flowYMax = 0;
		int flowMinX = 0, flowMaxX = 0, flowMinZ = 0, flowMaxZ = 0;
		// The floor squares those triangles lie over, as artCells.
		std::vector<unsigned int> flowCells;
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
		/* 1 where the triangles are a hull's art in unlinked air that no
		 * entry's box holds (MarkAirHulls), drawn pushed back in depth as a
		 * stand range answered HD_STAND_BEHIND is (set_stand_matrix). In
		 * the sort key, because the matrix is one constant per draw call. */
		int behind = 0;
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

	/* Every square a triangle's box x0..x1, z0..z1 touches, a box edge on
	 * a square's line not counted, into a sorted list of unique SectorKeys
	 * (HDRange::artCells and flowCells). */
	inline void NoteBoxCells(std::vector<unsigned int>& cells, int x0,
		int x1, int z0, int z1)
	{
		const int lastX = (x1 > x0 ? x1 - 1 : x0) >> 10;
		const int lastZ = (z1 > z0 ? z1 - 1 : z0) >> 10;

		for (int cx = x0 >> 10; cx <= lastX; cx++)
			for (int cz = z0 >> 10; cz <= lastZ; cz++)
			{
				const unsigned int key = SectorKey(cx << 10, cz << 10);
				const std::vector<unsigned int>::iterator at =
					std::lower_bound(cells.begin(), cells.end(), key);

				if (at == cells.end() || *at != key)
					cells.insert(at, key);
			}
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
		/* Its triangles in unlinked air (HDRoomAir), all three passes, and
		 * of them those an intrusion entry's box holds. */
		int airTris = 0;
		int airBoxed = 0;
		// Of them a hull's drawn pushed back (HDRange::behind).
		int hullBehind = 0;
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
		// Its material 16 parts, as the ripple asks for them (HDWaterArt).
		std::vector<HDWaterArt> waterArt;
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
		/* And out of its clear floors (HD_PASS_CLEAR) alone: where the art
		 * lets what lies under a floor show through it. */
		std::vector<HDFlatPlane> clearFlats;
		/* Its mirror faces (see "the marble floors"): nine floats a face,
		 * the corners in the group's own space, and each face's floor
		 * height, the corners' mean y rounded. Empty while the marble floors
		 * are off. */
		std::vector<float> mirrorFaces;
		std::vector<int> mirrorY;
		// The art's copy triangles left out under them (marble_copies).
		int mirrorCopies = 0;
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
	// Its pixel shader that reads the depth texture as well. Null when the
	// device refused it: the sheets then draw with g_water_ps.
	IDirect3DPixelShader9* g_water_depth_ps = 0;
	// That one with RoomProbe's depth views (HD_WATER_VIEW), made the first
	// time a view is asked for; the game never asks.
	IDirect3DPixelShader9* g_water_view_ps = 0;
	/* The depth variant drawn while the eye is under the water
	 * (HD_WATER_UNDER, hd_room.hlsl "under the water"). Null when the device
	 * refused it: the sheets then draw with g_water_depth_ps. */
	IDirect3DPixelShader9* g_water_under_ps = 0;
	/* The waterfalls' feet (hd_room.hlsl, FootPS), a second draw over a
	 * sheet range with a curtain near it. Null when the device refused it:
	 * the sheets then draw without the foot's foam; the rings and the drops
	 * stay. */
	IDirect3DPixelShader9* g_water_foot_ps = 0;
	/* The rivers' streaks and the foam where they end (hd_room.hlsl,
	 * RiverPS), a second draw over a river's sheet range. Null when the
	 * device refused it: a river then draws its rows alone. */
	IDirect3DPixelShader9* g_water_river_ps = 0;
	/* The foam's own draw over a sheet range whose body has foam
	 * (hd_room.hlsl, "the foam's draw"), plain and with the depth texture.
	 * Null when the device refused one: those sheets then draw without
	 * foam. */
	IDirect3DPixelShader9* g_water_foam_ps = 0;
	IDirect3DPixelShader9* g_water_foam_depth_ps = 0;
	/* The water's cards (hd_room.hlsl, CardVS and CardPS; "the cards"
	 * below). Null when the device refused either: then no splash or spray
	 * is taken from the classic path, which draws its own. */
	IDirect3DVertexShader9* g_card_vs = 0;
	IDirect3DPixelShader9* g_card_ps = 0;
	/* The wet floor's pixel half (hd_wet.hlsl, WetPS; "the wet floor"
	 * below), drawn with CardVS. Null when the device refused it: then no
	 * floor is drawn wet. */
	IDirect3DPixelShader9* g_wet_ps = 0;
	/* The marble floors' mirror laid over the floor (hd_wet.hlsl, MarblePS;
	 * "the marble floors"), drawn with CardVS. Null when the device refused
	 * it: then no floor is mirrored. */
	IDirect3DPixelShader9* g_marble_ps = 0;
	/* RoomProbe's march debugger (hd_wet.hlsl built with HD_WET_DEBUG) and
	 * the map it draws in the mirror's place (-tankmarch), 0 none. */
	IDirect3DPixelShader9* g_wet_debug_ps = 0;
	int g_wet_debug = 0;
	// The debugger's trial: fit, growth, first step (0: the game's).
	float g_wet_try[3] = { 0.0f, 0.0f, 0.0f };
	/* Where the art's falling water ends: per flow part (material 1) of a
	 * room's group that stands more than kFlowFall tall, its vertices
	 * within HD_WATER_FOOT_BAND of its lowest, room local, at most
	 * kFlowFootKept a part - HDRenderWaterFalls puts a waterfall's foot
	 * there when the art draws the fall (see "the foot of a waterfall").
	 * Gathered in HDRenderLevelBegin, the only time the .TRG's vertices
	 * are on the processor. Two clicks tall and 64 points are free
	 * choices: ANDREA2 room 231's two curtains stand about 3,600 units,
	 * the spray at their foot 234 (TrgProbe -matparts 1), and a part of a
	 * river or a belt lies nearly flat; 64 points mark a curtain's foot to
	 * a few dozen units. */
	struct FlowFoot
	{
		int group;
		float x, y, z;
	};

	std::vector<FlowFoot> g_flow_feet;
	const float kFlowFall = 512.0f;		// units
	const size_t kFlowFootKept = 64;
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
	/* Per pair face, 1 when it lies inside a larger face of a room its room
	 * intrudes in, in the same plane and facing the same way - two classic
	 * rooms with no portal between them that share a stretch of wall
	 * (MarkCoveredFaces). */
	std::vector<unsigned char> g_face_behind;
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
	/* The rooms' air (HDRoomAir), borrowed for the build like the list, and
	 * the pairs of rooms that are not asked (`g_air_links`, one byte per
	 * pair, row by the art's room: the same room, rooms with a link, flipmap
	 * twins). `g_air_cells` holds every air square by the world's 1024-unit
	 * floor square it covers - key and room, sorted - so that a centroid
	 * finds the rooms whose air it may stand in with one search.
	 * `g_air_pairs` counts per (group, host) the triangles standing in the
	 * host's air, and `g_air_ticks` is the counter ticks the build spent
	 * asking; both for the level's log line. */
	const HDRoomAir* g_room_air = 0;
	int g_room_air_count = 0;
	const unsigned char* g_air_links = 0;
	std::vector<std::pair<long long, int> > g_air_cells;
	std::map<std::pair<int, int>, int> g_air_pairs;
	long long g_air_ticks = 0;

	/* The key of the world's floor square (x, z), counted in 1024-unit
	 * squares: both shifted by 65536 squares, so that any square within 64
	 * million units of the origin - TR5's coordinates are 32 bit, a level
	 * spans under a hundred squares - gives a positive number, and z taking
	 * 131072 of them, so that no two squares share a key. */
	long long AirCellKey(int x, int z)
	{
		return ((long long)x + 65536) * 131072 + ((long long)z + 65536);
	}

	typedef std::vector<std::pair<long long, int> > AirCells;
	typedef std::vector<std::pair<unsigned int, int> > AirHosts;

	/* Which of group g's triangles stand in the air of a room g's room has
	 * no portal, door or vertical link with, not even through a flipmap twin
	 * (HDRoomAir), and in whose: (first index, host) per triangle and host,
	 * sorted. Every triangle of the group is asked, with the index fill's
	 * own centroid put into world units for x and z and the heights of its
	 * corners for y (HDRoomAirReaches); the floor square under the centroid
	 * names the rooms to ask (g_air_cells). */
	void AirHostsOf(const trf::remaster::Trg& trg,
		const trf::remaster::TrgGroup& source, int g, AirHosts& out)
	{
		const HDRoomAir& own = g_room_air[g];
		const unsigned char* links =
			g_air_links + (size_t)g * g_room_air_count;

		for (int pass = 0; pass < 3; pass++)
			for (const trf::remaster::TrgPart& part : source.parts)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned int first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex& ta =
						trg.vertices[trg.indices[first]];
					const trf::remaster::TrgVertex& tb =
						trg.vertices[trg.indices[first + 1]];
					const trf::remaster::TrgVertex& tc =
						trg.vertices[trg.indices[first + 2]];
					const float x = ((float)ta.x + (float)tb.x +
						(float)tc.x) / 3.0f + own.x;
					const float z = ((float)ta.z + (float)tb.z +
						(float)tc.z) / 3.0f + own.z;
					const float y0 = (float)(ta.y < tb.y ? (ta.y < tc.y ?
						ta.y : tc.y) : (tb.y < tc.y ? tb.y : tc.y)) + own.y;
					const float y1 = (float)(ta.y > tb.y ? (ta.y > tc.y ?
						ta.y : tc.y) : (tb.y > tc.y ? tb.y : tc.y)) + own.y;
					const long long key = AirCellKey(
						(int)floorf(x / 1024.0f), (int)floorf(z / 1024.0f));
					AirCells::const_iterator it = std::lower_bound(
						g_air_cells.begin(), g_air_cells.end(),
						std::make_pair(key, -1));

					for (; it != g_air_cells.end() && it->first == key; ++it)
						if (!links[it->second] && HDRoomAirReaches(
							g_room_air[it->second], x, y0, y1, z,
							HD_AIR_MARGIN))
							out.push_back(std::make_pair(first, it->second));
				}
			}

		std::sort(out.begin(), out.end());
	}

	/* The hulls, with tomb5.hd_air 2 (HDRenderSetAirHulls): a group whose art
	 * in the air of a room its own room has no link with, in no entry's box,
	 * stands mostly where that room's own art stands - a second shell of
	 * other rooms, drawn whole wherever its own room is listed. Asked per
	 * triangle: its centroid's 256-unit world cell (the overlap list's grid)
	 * holds a corner of the host's own art. Measured with TrgProbe
	 * -roomspace over the fifteen levels: 85 groups have such art; RICH2's
	 * lift shaft, room 61, has 51,547 triangles of it, 36,285 on its hosts'
	 * own art (0.70) - its second hull of the shaft's floors 62 to 65 -,
	 * ANDREA1's 68 in 111 0.98, JOBY2's 110 and 135 1.00, RICH2's 26 0.68,
	 * nine small ones 0.59 to 1.00; ANDREA2's street art bundles 10, 131,
	 * 217 and 236 0.00 to 0.06, its palms 117 and 127 0.00, the highest
	 * below them RICH2's 57 at 0.44. A group is a hull above one half, the
	 * middle of the gap between 0.44 and 0.59. The share of a group's art
	 * outside its own room does not tell them apart: 0.891 for room 61,
	 * 0.997 and more for the bundles, 0.899 for the palms of 127.
	 * `g_hull_air` and `g_hull_covered` are the two counts per group, for
	 * the log. */
	bool g_air_hulls = false;
	std::vector<unsigned char> g_group_hull;
	std::vector<int> g_hull_air, g_hull_covered;

	long long HullCellKey(float x, float y, float z)
	{
		return ((long long)((int)floorf(x / 256.0f) + 32768) << 34) |
			((long long)((int)floorf(y / 256.0f) + 32768) << 17) |
			(long long)((int)floorf(z / 256.0f) + 32768);
	}

	/* One pair face inside a larger face of its holder (MarkCoveredFaces):
	 * the face, its room and the holder's, the face's area, and the area of
	 * each room's art over the face - its centroid within the face's
	 * outline - in three layers along the face's normal: [0] every corner
	 * within a unit of the plane, [1] all on the normal's side, [2] all on
	 * the other, both within HD_COVER_SIDE. Then its centre and unit normal,
	 * in which of the two rooms' air the point 128 units off its centre
	 * stands (bit 1 the face's room on the normal's side, 2 on the other,
	 * 4 and 8 the holder's), and whether its art is pushed back. For the
	 * rule and the HDLOG covered lines. */
	struct HDCoveredFace
	{
		int face, group, holder;
		float area;
		float own[3], held[3];
		float centre[3], normal[3];
		int airs;
		int behind;
	};

	std::vector<HDCoveredFace> g_cover_faces;

	/* Group g's art over pair face f, in the three layers of HDCoveredFace;
	 * `n` the face's world plane (unit normal, then n.p of its points). */
	void CoveredFaceArt(const trf::remaster::Trg& trg, int g, int f,
		const float* n, float* out)
	{
		out[0] = out[1] = out[2] = 0.0f;

		if (g < 0 || g >= (int)trg.groups.size() || g >= g_box_count)
			return;

		const HDRoomBox& own = g_boxes[g];
		const HDPairFace& face = g_pair_faces[f];
		const HDRoomBox& at = g_boxes[face.group];

		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			for (int pass = 0; pass < 3; pass++)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned int k = 0; k + 2 < run.count; k += 3)
				{
					const unsigned int first = run.start + k;

					if (first + 2 >= trg.indices.size())
						break;

					float p[3][3];
					float lo = 0.0f, hi = 0.0f;

					for (int c = 0; c < 3; c++)
					{
						const trf::remaster::TrgVertex& v =
							trg.vertices[trg.indices[first + c]];

						p[c][0] = (float)v.x + own.originX;
						p[c][1] = (float)v.y;
						p[c][2] = (float)v.z + own.originZ;

						const float d = n[0] * p[c][0] + n[1] * p[c][1] +
							n[2] * p[c][2] - n[3];

						lo = c == 0 || d < lo ? d : lo;
						hi = c == 0 || d > hi ? d : hi;
					}

					const float mid[3] =
					{
						(p[0][0] + p[1][0] + p[2][0]) / 3.0f - at.originX,
						(p[0][1] + p[1][1] + p[2][1]) / 3.0f,
						(p[0][2] + p[1][2] + p[2][2]) / 3.0f - at.originZ
					};

					if (HDPairFaceDistance(face, mid) < 0.0)
						continue;

					const float a[3] = { p[1][0] - p[0][0],
						p[1][1] - p[0][1], p[1][2] - p[0][2] };
					const float b[3] = { p[2][0] - p[0][0],
						p[2][1] - p[0][1], p[2][2] - p[0][2] };
					const float x = a[1] * b[2] - a[2] * b[1];
					const float y = a[2] * b[0] - a[0] * b[2];
					const float z = a[0] * b[1] - a[1] * b[0];
					const float size = 0.5f * sqrtf(x * x + y * y + z * z);

					if (lo >= -1.0f && hi <= 1.0f)
						out[0] += size;
					else if (lo > 1.0f && hi <= HD_COVER_SIDE)
						out[1] += size;
					else if (hi < -1.0f && lo >= -HD_COVER_SIDE)
						out[2] += size;
				}
			}
	}

	/* The pair faces that lie inside a larger face of the holder of an
	 * entry their room intrudes in: the same plane within a unit, facing
	 * the same way, every corner within the larger face's outline. Two
	 * rooms with no portal between them that stand in one another can
	 * share a stretch of wall, each with a classic face on it and art for
	 * it in its group: both are drawn and fight for the depth, on two
	 * pages, in the classic frame as in this one (ANDREA1's room 12 has a
	 * face of 1024 by 1024 inside room 212's of 1024 by 1280 at z 27,648,
	 * x 37,888 to 38,912 - the flicker on room 212's sector x1/z0 - and
	 * their groups' art lies within a unit of each other on 25,142 of the
	 * pixels both draw there at 640x360, page 126 against page 146). The
	 * art standing on the smaller face is drawn pushed back as a stand
	 * range answered HD_STAND_BEHIND is, so the larger face's art wins;
	 * two faces alike in size keep both, there being no larger one. Asked
	 * at all 40 such faces of the fifteen levels (an eye 768 units off
	 * each on its open side, both rooms drawn whole, the eye moved one unit
	 * along x and along z): 582,474 of the pixels the push changes fight
	 * without it and 1,562 with it. It ends the fight at 27 faces and adds
	 * 4 pixels to one; at twelve no fight showed where it changes the
	 * picture, seven of them under 600 pixels, five 9,744 to 113,537
	 * (JOBY3's faces 87, 562 and 564, RICH1's 139 and 444).
	 * Every face's art lies on the plane within a unit for both rooms (the
	 * HDLOG covered lines); 16 faces have only the holder's air in front of
	 * them - the holder's floor - and 24 both rooms'. Only with the air on:
	 * tomb5.hd_air 0 is the answer before the air rule. */
	void MarkCoveredFaces(const trf::remaster::Trg& trg)
	{
		g_face_behind.assign((size_t)g_pair_face_count, 0);
		g_cover_faces.clear();

		if (g_air_cells.empty() || !g_boxes || g_pair_face_count <= 0)
			return;

		std::vector<float> world((size_t)g_pair_face_count * 12, 0.0f);
		std::vector<float> normal((size_t)g_pair_face_count * 4, 0.0f);
		std::vector<float> area((size_t)g_pair_face_count, 0.0f);

		for (int f = 0; f < g_pair_face_count; f++)
		{
			const HDPairFace& face = g_pair_faces[f];
			float* w = &world[(size_t)f * 12];

			if (face.group < 0 || face.group >= g_box_count)
				continue;

			for (int c = 0; c < 4; c++)
			{
				w[c * 3] = face.v[c][0] + g_boxes[face.group].originX;
				w[c * 3 + 1] = face.v[c][1];
				w[c * 3 + 2] = face.v[c][2] + g_boxes[face.group].originZ;
			}

			// The plane by the corners' cross product, the area with it.
			float n[3] = { 0.0f, 0.0f, 0.0f };

			for (int c = 1; c + 1 < face.corners; c++)
			{
				const float a[3] = { w[c * 3] - w[0], w[c * 3 + 1] - w[1],
					w[c * 3 + 2] - w[2] };
				const float b[3] = { w[c * 3 + 3] - w[0],
					w[c * 3 + 4] - w[1], w[c * 3 + 5] - w[2] };

				n[0] += a[1] * b[2] - a[2] * b[1];
				n[1] += a[2] * b[0] - a[0] * b[2];
				n[2] += a[0] * b[1] - a[1] * b[0];
			}

			const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

			if (len <= 0.0f)
				continue;

			area[(size_t)f] = len * 0.5f;

			for (int k = 0; k < 3; k++)
				normal[(size_t)f * 4 + k] = n[k] / len;

			normal[(size_t)f * 4 + 3] = (n[0] * w[0] + n[1] * w[1] +
				n[2] * w[2]) / len;
		}

		for (int f = 0; f < g_pair_face_count; f++)
		{
			const int b = g_pair_faces[f].group;
			const float* nf = &normal[(size_t)f * 4];

			if (area[(size_t)f] <= 0.0f)
				continue;

			bool found = false;

			for (int h = 0; h < g_pair_face_count && !found; h++)
			{
				const int a = g_pair_faces[h].group;
				const float* nh = &normal[(size_t)h * 4];

				if (a == b || area[(size_t)h] <= area[(size_t)f] + 1.0f ||
					nf[0] * nh[0] + nf[1] * nh[1] + nf[2] * nh[2] < 0.999f ||
					fabsf(nf[3] - nh[3]) > 1.0f)
					continue;

				bool entry = false;

				for (int k = 0; k < g_intrusion_count && !entry; k++)
					entry = g_intrusions[k].group == b &&
						g_intrusions[k].holder == a;

				if (!entry)
					continue;

				// Every corner of f within h's outline, a unit's slack.
				int drop = 0;

				for (int axis = 1; axis < 3; axis++)
					if (fabsf(nh[axis]) > fabsf(nh[drop]))
						drop = axis;

				const int u = drop == 0 ? 1 : 0;
				const int v = drop == 2 ? 1 : 2;
				const float* wf = &world[(size_t)f * 12];
				const float* wh = &world[(size_t)h * 12];
				const int nhc = g_pair_faces[h].corners;
				bool inside = true;

				for (int c = 0; c < g_pair_faces[f].corners && inside; c++)
				{
					const float pu = wf[c * 3 + u], pv = wf[c * 3 + v];
					float lo = 0.0f, hi = 0.0f;
					bool first = true;

					// Convex outline: every edge's side test agrees.
					for (int e = 0; e < nhc && inside; e++)
					{
						const int e1 = (e + 1) % nhc;
						const float eu = wh[e1 * 3 + u] - wh[e * 3 + u];
						const float ev = wh[e1 * 3 + v] - wh[e * 3 + v];
						const float el = sqrtf(eu * eu + ev * ev);

						if (el <= 0.0f)
							continue;

						const float side = (eu * (pv - wh[e * 3 + v]) -
							ev * (pu - wh[e * 3 + u])) / el;

						if (first)
						{
							lo = hi = side;
							first = false;
						}

						lo = side < lo ? side : lo;
						hi = side > hi ? side : hi;
						inside = !(lo < -1.0f && hi > 1.0f);
					}
				}

				if (!inside)
					continue;

				HDCoveredFace made;

				found = true;

				made.face = f;
				made.group = b;
				made.holder = a;
				made.area = area[(size_t)f];
				made.airs = 0;

				{
					float c[3] = { 0.0f, 0.0f, 0.0f };

					for (int k = 0; k < g_pair_faces[f].corners; k++)
						for (int i = 0; i < 3; i++)
							c[i] += wf[k * 3 + i] / g_pair_faces[f].corners;

					for (int i = 0; i < 3; i++)
					{
						made.centre[i] = c[i];
						made.normal[i] = nf[i];
					}

					for (int s = 0; s < 2; s++)
					{
						const float o = s ? -128.0f : 128.0f;
						const float px = c[0] + nf[0] * o;
						const float py = c[1] + nf[1] * o;
						const float pz = c[2] + nf[2] * o;

						if (b < g_room_air_count && HDRoomAirReaches(
							g_room_air[b], px, py, py, pz, 0.0f))
							made.airs |= 1 << s;

						if (a < g_room_air_count && HDRoomAirReaches(
							g_room_air[a], px, py, py, pz, 0.0f))
							made.airs |= 4 << s;
					}
				}

				CoveredFaceArt(trg, a, f, nf, made.held);
				CoveredFaceArt(trg, b, f, nf, made.own);
				made.behind = 1;
				g_face_behind[(size_t)f] = made.behind;
				g_cover_faces.push_back(made);
			}
		}
	}

	/* Marks the hulls before the groups are filled: the art of every group
	 * in world cells, then per group the triangles AirHostsOf finds that no
	 * entry's box holds, as the fill asks a box (by the centroid, or by its
	 * x and z with the triangle reaching into the holder's air). With the
	 * air on, whatever g_air_hulls says: a hull's such art is drawn pushed
	 * back at tomb5.hd_air 1 and 2 alike (HDRange::behind). */
	void MarkAirHulls(const trf::remaster::Trg& trg)
	{
		const size_t groups = trg.groups.size();

		g_group_hull.assign(groups, 0);
		g_hull_air.assign(groups, 0);
		g_hull_covered.assign(groups, 0);

		if (g_air_cells.empty() || !g_boxes)
			return;

		std::vector<std::pair<long long, int> > cells;

		for (size_t g = 0; g < groups && (int)g < g_box_count &&
			(int)g < g_room_air_count; g++)
		{
			const HDRoomBox& own = g_boxes[g];

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned int at = 0; at < run.count; at++)
					{
						if (run.start + at >= trg.indices.size())
							break;

						const trf::remaster::TrgVertex& v =
							trg.vertices[trg.indices[run.start + at]];

						cells.push_back(std::make_pair(HullCellKey(
							(float)v.x + own.originX, (float)v.y,
							(float)v.z + own.originZ), (int)g));
					}
				}
		}

		std::sort(cells.begin(), cells.end());
		cells.erase(std::unique(cells.begin(), cells.end()), cells.end());

		for (size_t g = 0; g < groups && (int)g < g_box_count &&
			(int)g < g_room_air_count; g++)
		{
			AirHosts hosts;

			AirHostsOf(trg, trg.groups[g], (int)g, hosts);

			const HDRoomBox& own = g_boxes[g];

			for (size_t i = 0; i < hosts.size(); )
			{
				const unsigned int first = hosts[i].first;
				size_t end = i;

				while (end < hosts.size() && hosts[end].first == first)
					end++;

				const trf::remaster::TrgVertex& ta =
					trg.vertices[trg.indices[first]];
				const trf::remaster::TrgVertex& tb =
					trg.vertices[trg.indices[first + 1]];
				const trf::remaster::TrgVertex& tc =
					trg.vertices[trg.indices[first + 2]];
				const float c[3] = {
					((float)ta.x + (float)tb.x + (float)tc.x) / 3.0f,
					((float)ta.y + (float)tb.y + (float)tc.y) / 3.0f,
					((float)ta.z + (float)tb.z + (float)tc.z) / 3.0f };
				bool boxed = false;

				for (int k = 0; k < g_intrusion_count && !boxed; k++)
				{
					const HDIntrusion& box = g_intrusions[k];

					if (box.group != (int)g)
						continue;

					if (c[0] < box.mins[0] || c[0] > box.maxs[0] ||
						c[2] < box.mins[2] || c[2] > box.maxs[2])
						continue;

					boxed = (c[1] >= box.mins[1] && c[1] <= box.maxs[1]) ||
						std::binary_search(hosts.begin(), hosts.end(),
							std::make_pair(first, box.holder));
				}

				if (!boxed)
				{
					bool covered = false;
					const long long key = HullCellKey(c[0] + own.originX,
						c[1], c[2] + own.originZ);

					for (size_t j = i; j < end && !covered; j++)
						covered = std::binary_search(cells.begin(),
							cells.end(), std::make_pair(key, hosts[j].second));

					g_hull_air[g]++;
					g_hull_covered[g] += covered ? 1 : 0;
				}

				i = end;
			}

			g_group_hull[g] = g_hull_covered[g] * 2 > g_hull_air[g] ? 1 : 0;
		}
	}

	/* The rooms under the sky and the flipmap twins (HDRenderSetCarryHosts),
	 * borrowed for the length of the build, the rooms under the sky in room
	 * order, and the art the build found them carrying: triangles per
	 * (group, host), and how many of them are copies (HDCarryCopyKey).
	 * `g_carry_cells` is every triangle's centroid cell with its group,
	 * sorted, made before the groups are filled (MarkCarryCells). */
	const unsigned char* g_carry_sky = 0;
	const short* g_carry_twin = 0;
	int g_carry_count = 0;
	std::vector<int> g_carry_hosts;
	std::map<std::pair<int, int>, int> g_carry_pairs, g_carry_copies;
	std::vector<std::pair<long long, int> > g_carry_cells;

	/* Every triangle's centroid cell (HDCarryCopyKey) with its group, for
	 * the copies among the carried art. Only when a room carries. */
	void MarkCarryCells(const trf::remaster::Trg& trg)
	{
		g_carry_cells.clear();

		if (g_carry_hosts.empty() || !g_boxes)
			return;

		for (size_t g = 0; g < trg.groups.size() && (int)g < g_box_count; g++)
		{
			const HDRoomBox& own = g_boxes[g];

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned int at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned int first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						const trf::remaster::TrgVertex& ta =
							trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& tb =
							trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& tc =
							trg.vertices[trg.indices[first + 2]];

						g_carry_cells.push_back(std::make_pair(HDCarryCopyKey(
							((float)ta.x + (float)tb.x + (float)tc.x) / 3.0f +
							own.originX,
							((float)ta.y + (float)tb.y + (float)tc.y) / 3.0f,
							((float)ta.z + (float)tb.z + (float)tc.z) / 3.0f +
							own.originZ), (int)g));
					}
				}
		}

		std::sort(g_carry_cells.begin(), g_carry_cells.end());
	}

	/* Which rooms under the sky group g's art stands in (HDCarryBoxHolds):
	 * one host per triangle and room, for every triangle whose centroid
	 * stands outside g's own inside and inside the inside of a room under
	 * the sky that is not g's flipmap twin; in `copies` the same host once
	 * more for such a triangle another group has a copy of (MarkCarryCells).
	 */
	void CarryHostsOf(const trf::remaster::Trg& trg,
		const trf::remaster::TrgGroup& source, int g, std::vector<int>& out,
		std::vector<int>& copies)
	{
		if (g_carry_hosts.empty() || !g_boxes || g >= g_box_count ||
			g >= g_carry_count)
			return;

		const HDRoomBox& own = g_boxes[g];

		for (int pass = 0; pass < 3; pass++)
			for (const trf::remaster::TrgPart& part : source.parts)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned int first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex& ta =
						trg.vertices[trg.indices[first]];
					const trf::remaster::TrgVertex& tb =
						trg.vertices[trg.indices[first + 1]];
					const trf::remaster::TrgVertex& tc =
						trg.vertices[trg.indices[first + 2]];
					float c[3] = {
						((float)ta.x + (float)tb.x + (float)tc.x) / 3.0f,
						((float)ta.y + (float)tb.y + (float)tc.y) / 3.0f,
						((float)ta.z + (float)tb.z + (float)tc.z) / 3.0f };

					if (HDCarryBoxHolds(own, c))
						continue;

					c[0] += own.originX;
					c[2] += own.originZ;

					const long long key = HDCarryCopyKey(c[0], c[1], c[2]);
					int copy = -1;

					for (int a : g_carry_hosts)
					{
						if (a == g || a >= g_box_count ||
							g_carry_twin[a] == g || g_carry_twin[g] == a)
							continue;

						HDRoomBox host = g_boxes[a];

						host.mins[0] += host.originX;
						host.maxs[0] += host.originX;
						host.mins[2] += host.originZ;
						host.maxs[2] += host.originZ;

						if (!HDCarryBoxHolds(host, c))
							continue;

						out.push_back(a);

						if (copy < 0)
						{
							std::vector<std::pair<long long, int> >::
								const_iterator it = std::lower_bound(
								g_carry_cells.begin(), g_carry_cells.end(),
								std::make_pair(key, -1));

							copy = 0;

							for (; it != g_carry_cells.end() &&
								it->first == key && !copy; ++it)
								copy = it->second != g ? 1 : 0;
						}

						if (copy)
							copies.push_back(a);
					}
				}
			}
	}

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
	/* The x and z of the room origin the last room's lighting gave the
	 * shaders (gRoomOrigin): the world origin of the classic room in the
	 * engine, 0 in RoomProbe, whose world is the drawn room's own. */
	float g_room_origin[2] = { 0, 0 };
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
		g_room_origin[0] = origin[0];
		g_room_origin[1] = origin[2];
		g_device->SetPixelShaderConstantF(17, counts, 1);
		g_device->SetPixelShaderConstantF(20, lights, HD_MAX_LIGHTS * 2);
	}

	/* Whether this frame's eye is under the water (see "under the water"),
	 * which picks the sheets' HD_WATER_UNDER variant; and the constants it
	 * takes, defined with the water. */
	bool g_under_on = false;

	bool water_under_constants(const HDFrameLighting* f, float* out);

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

		/* c134 - c136 and c145, the eye under the water (hd_room.hlsl,
		 * "under the water"): all sixteen zero but in a frame whose eye is
		 * in a water room of a level with sheets - and all zero leave every
		 * shader's colour as it was, to the bit. */
		float under[16] = {};

		g_under_on = water_under_constants(f, under);
		g_device->SetPixelShaderConstantF(134, under, 3);
		g_device->SetPixelShaderConstantF(145, under + 12, 1);

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

	void FindMirrors(const trf::remaster::Trg& trg, int threads)
	{
		g_mirrors.clear();

		if (!g_boxes || g_box_count <= 0)
			return;

		const int rooms = g_box_count < g_room_groups ? g_box_count : g_room_groups;
		const int asked = rooms < (int)trg.groups.size()
			? rooms : (int)trg.groups.size();

		/* One room at a time on the pool (HDPoolRun): a room reads the .TRG,
		 * the boxes and the built groups' bounds, which nothing writes now,
		 * and keeps its mirrors in its own row; the rows are put together in
		 * room order after the join, so the list does not depend on the
		 * thread count. */
		std::vector<std::vector<HDMirror> > found(
			(size_t)(asked > 0 ? asked : 0));

		HDPoolRun(asked, threads, [&](int g)
		{
			const HDRoomBox& box = g_boxes[g];
			const float origin[3] = { box.originX, 0.0f, box.originZ };

			if (box.maxs[0] <= box.mins[0] || box.maxs[2] <= box.mins[2])
				return;

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

				/* The cells the other rooms' triangles stand in, sorted and
				 * made unique once and asked by binary search - the answers
				 * of a hashed set, without a hash insert per triangle. */
				std::vector<unsigned long long> cells;

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

								cells.push_back(MirrorCell(c[0] / 3.0f,
									c[1] / 3.0f, c[2] / 3.0f));
							}
						}
				}

				std::sort(cells.begin(), cells.end());
				cells.erase(std::unique(cells.begin(), cells.end()),
					cells.end());

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
										found = std::binary_search(
											cells.begin(), cells.end(),
											MirrorCell(
											c[0] + 8.0f * (float)dx,
											c[1] + 8.0f * (float)dy,
											c[2] + 8.0f * (float)dz));

							if (found)
								m.mirrored++;
						}
					}

				if (m.mirrored * 100 >= m.behind * HD_MIRROR_MATCH)
					found[(size_t)g].push_back(m);
			}
		});

		for (const std::vector<HDMirror>& row : found)
			g_mirrors.insert(g_mirrors.end(), row.begin(), row.end());
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

	/* The marble floors (see "the marble floors"): whether the level is
	 * built and drawn with them (tomb5.hd_marble, HDRenderMarbleSet), and
	 * the most floor heights a frame mirrors (tomb5.hd_marble_planes). */
	bool g_marble_on = false;
	int g_marble_cap = 2;
	// RoomProbe's -marblecopies 1: the copies kept (HDRenderMarbleKeepCopies).
	bool g_marble_keep_copies = false;

	/* The frame's room, stand and static draws as they were made, for the
	 * mirrored pass to make again (marble_note); `g_marble_replay` while it
	 * does, `g_marble_spent` once a frame's mirror was asked for, so the
	 * next frame's first bracket starts the list over. */
	struct MarbleDraw
	{
		int kind;		// 0 a room, 1 a stand range, 2 a static piece
		int id;			// the group or the piece
		D3DMATRIX world;
		const HDRoomLighting* lighting;
		unsigned int passes;
		int window;
		int stand;
		bool behind;
	};

	std::vector<MarbleDraw> g_marble_draws;
	bool g_marble_replay = false;
	bool g_marble_spent = false;
	// The bracket's frame lighting, put back after the mirrored models.
	const HDFrameLighting* g_marble_frame = 0;

	// Whether a page's flat floors are a mirror: the mirror column.
	bool MirrorPage(int page)
	{
		return page >= 0 && HDGlossMirror(g_gloss_file.mirror(page),
			HDGlossTableFind(page));
	}

	// The art's copies under the polished floors (see "the marble floors").
	int marble_copies(const trf::remaster::Trg& trg,
		const trf::remaster::TrgGroup& source, int g,
		std::vector<std::vector<unsigned char> >& copies);

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

	/* PageGloss for the index fill, which runs on several threads and so
	 * may not write the memory: the answer the build worked out before the
	 * fill, or worked out again here (ResolvePage writes nothing). */
	HDGloss PageGlossKnown(int page)
	{
		const std::unordered_map<int, HDGloss>::const_iterator at =
			g_page_gloss.find(page);

		return at != g_page_gloss.end() ? at->second
			: ResolvePage(page, HDPageCacheSlotOf(page), 0);
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

	void release_water_sheets();

	/* Every buffer of the level, the water sheets' among them - at a new
	 * level, at a shutdown and on a lost device (HDLevelDeviceLost). */
	void release_buffers()
	{
		release_water_sheets();
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

	/* bshader.bat compiles four pixel shaders for ps_3_0, the room shader at
	 * 482 instruction slots, the model shader at 360, the glow shader at 483
	 * and the water sheet's at 360 (406 with the depth texture, 430 in
	 * RoomProbe's view variant, 408 under the water; its foam's own draw
	 * 251 and 293, RiverPS 435), and the room shader's gloss variant
	 * beside them at 502 (hd_room_gloss_ps.inc). The ps_3_0 figure is
	 * MaxPixelShader30InstructionSlots - PS20Caps is the ps_2_x one - and
	 * shader model 3 sets its floor at D3DMIN30SHADERINSTRUCTIONS, 512. fxc
	 * does not hold a ps_3_0 shader to 512: a 24-bulb room shader compiles
	 * at 524 slots without a warning. The shaders stay inside the guarantee
	 * only because of their own size, so anyone who grows one has to read
	 * the "instruction slots used" line of its .inc. The vertex shaders are
	 * not checked here; the room's, at 412, and the water sheet's, at 287,
	 * lie inside the 512 slots vs_3_0 guarantees.
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

	// Nor is the water sheet's pair: without it no sheet is built and the
	// classic water faces are restored (hd_level.cpp, BuildWaterSurfaces).
	if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomWaterVS,
		&g_water_vs)))
		g_water_vs = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomWaterPS,
		&g_water_ps)))
		g_water_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomWaterDepthPS,
		&g_water_depth_ps)))
		g_water_depth_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomWaterUnderPS,
		&g_water_under_ps)))
		g_water_under_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomWaterFootPS,
		&g_water_foot_ps)))
		g_water_foot_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomWaterRiverPS,
		&g_water_river_ps)))
		g_water_river_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomWaterFoamPS,
		&g_water_foam_ps)))
		g_water_foam_ps = 0;

	if (FAILED(device->CreatePixelShader(
		(const DWORD*)kHDRoomWaterFoamDepthPS, &g_water_foam_depth_ps)))
		g_water_foam_depth_ps = 0;

	// Nor are the cards': without them the classic splash and spray stay.
	if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomCardVS,
		&g_card_vs)))
		g_card_vs = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomCardPS,
		&g_card_ps)))
		g_card_ps = 0;

	// Nor is the wet floor's: without it no floor is drawn wet.
	if (FAILED(device->CreatePixelShader((const DWORD*)kHDWetPS, &g_wet_ps)))
		g_wet_ps = 0;

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDWetDebugPS,
		&g_wet_debug_ps)))
		g_wet_debug_ps = 0;

	// Nor is the marble floors': without it no floor is mirrored.
	if (FAILED(device->CreatePixelShader((const DWORD*)kHDWetMarblePS,
		&g_marble_ps)))
		g_marble_ps = 0;

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
	if (g_water_vs) { g_water_vs->Release(); g_water_vs = 0; }
	if (g_water_ps) { g_water_ps->Release(); g_water_ps = 0; }
	if (g_water_depth_ps) { g_water_depth_ps->Release(); g_water_depth_ps = 0; }
	if (g_water_view_ps) { g_water_view_ps->Release(); g_water_view_ps = 0; }
	if (g_water_under_ps) { g_water_under_ps->Release(); g_water_under_ps = 0; }
	if (g_water_foot_ps) { g_water_foot_ps->Release(); g_water_foot_ps = 0; }
	if (g_water_river_ps) { g_water_river_ps->Release(); g_water_river_ps = 0; }
	if (g_water_foam_ps) { g_water_foam_ps->Release(); g_water_foam_ps = 0; }
	if (g_water_foam_depth_ps)
	{
		g_water_foam_depth_ps->Release();
		g_water_foam_depth_ps = 0;
	}
	if (g_card_vs) { g_card_vs->Release(); g_card_vs = 0; }
	if (g_card_ps) { g_card_ps->Release(); g_card_ps = 0; }
	if (g_wet_ps) { g_wet_ps->Release(); g_wet_ps = 0; }
	if (g_marble_ps) { g_marble_ps->Release(); g_marble_ps = 0; }
	if (g_wet_debug_ps) { g_wet_debug_ps->Release(); g_wet_debug_ps = 0; }
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

void HDRenderSetRoomAir(const HDRoomAir* rooms, int count,
	const unsigned char* links)
{
	g_room_air = count > 0 && links ? rooms : 0;
	g_room_air_count = g_room_air ? count : 0;
	g_air_links = g_room_air ? links : 0;
	g_air_cells.clear();
	g_air_pairs.clear();
	g_air_ticks = 0;

	/* Every air square under every world floor square it covers: one for
	 * a room whose origin lies on the 1024-unit grid, as every classic
	 * room's does, two or four for one that does not. */
	for (int r = 0; r < g_room_air_count; r++)
	{
		const HDRoomAir& air = g_room_air[r];

		if (!air.sectors)
			continue;

		for (int i = 0; i < air.nx; i++)
			for (int k = 0; k < air.nz; k++)
			{
				const float* s = air.sectors + ((size_t)i * air.nz + k) * 2;

				if (!(s[0] < s[1]))
					continue;

				const float x0 = air.x + (float)(i << 10);
				const float z0 = air.z + (float)(k << 10);
				const int cx0 = (int)floorf(x0 / 1024.0f);
				const int cx1 = (int)floorf((x0 + 1023.0f) / 1024.0f);
				const int cz0 = (int)floorf(z0 / 1024.0f);
				const int cz1 = (int)floorf((z0 + 1023.0f) / 1024.0f);

				for (int cx = cx0; cx <= cx1; cx++)
					for (int cz = cz0; cz <= cz1; cz++)
						g_air_cells.push_back(std::make_pair(
							AirCellKey(cx, cz), r));
			}
	}

	std::sort(g_air_cells.begin(), g_air_cells.end());
	g_air_cells.erase(std::unique(g_air_cells.begin(), g_air_cells.end()),
		g_air_cells.end());
}

int HDRenderAirPairs()
{
	return (int)g_air_pairs.size();
}

void HDRenderSetCarryHosts(const unsigned char* sky, const short* twin,
	int count)
{
	g_carry_sky = count > 0 && twin ? sky : 0;
	g_carry_twin = g_carry_sky ? twin : 0;
	g_carry_count = g_carry_sky ? count : 0;
	g_carry_hosts.clear();
	g_carry_pairs.clear();
	g_carry_copies.clear();

	for (int r = 0; r < g_carry_count; r++)
		if (g_carry_sky[r])
			g_carry_hosts.push_back(r);
}

int HDRenderCarryPairs()
{
	return (int)g_carry_pairs.size();
}

bool HDRenderCarryPair(int index, int* group, int* host, int* tris)
{
	if (index < 0 || index >= (int)g_carry_pairs.size())
		return false;

	std::map<std::pair<int, int>, int>::const_iterator it =
		g_carry_pairs.begin();

	std::advance(it, index);
	*group = it->first.first;
	*host = it->first.second;
	*tris = it->second;
	return true;
}

void HDRenderSetAirHulls(int on)
{
	g_air_hulls = on != 0;
}

bool HDRenderGroupHull(int group, int* air, int* covered)
{
	if (group < 0 || group >= (int)g_group_hull.size())
		return false;

	*air = g_hull_air[(size_t)group];
	*covered = g_hull_covered[(size_t)group];
	return g_group_hull[(size_t)group] != 0;
}

int HDRenderCoveredFaces()
{
	int n = 0;

	for (size_t f = 0; f < g_face_behind.size(); f++)
		n += g_face_behind[f] ? 1 : 0;

	return n;
}

bool HDRenderCoveredFace(int index, int* face, int* group, int* holder,
	float* area, float own[3], float held[3], int* airs, float centre[3],
	float normal[3], int* behind)
{
	if (index < 0 || index >= (int)g_cover_faces.size())
		return false;

	const HDCoveredFace& one = g_cover_faces[(size_t)index];

	*face = one.face;
	*group = one.group;
	*holder = one.holder;
	*area = one.area;
	*airs = one.airs;

	for (int k = 0; k < 3; k++)
	{
		centre[k] = one.centre[k];
		normal[k] = one.normal[k];
	}
	*behind = one.behind;

	for (int k = 0; k < 3; k++)
	{
		own[k] = one.own[k];
		held[k] = one.held[k];
	}

	return true;
}

int HDRenderGroupHullBehind(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return g_groups[(size_t)group].hullBehind;
}

bool HDRenderAirPair(int index, int* group, int* host, int* tris)
{
	if (index < 0 || index >= (int)g_air_pairs.size())
		return false;

	std::map<std::pair<int, int>, int>::const_iterator it = g_air_pairs.begin();

	std::advance(it, index);
	*group = it->first.first;
	*host = it->first.second;
	*tris = it->second;
	return true;
}

int HDRenderGroupAirTriangles(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return g_groups[(size_t)group].airTris;
}

int HDRenderGroupAirBoxed(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return g_groups[(size_t)group].airBoxed;
}

double HDRenderAirMicroseconds()
{
	LARGE_INTEGER rate;

	if (!QueryPerformanceFrequency(&rate) || !rate.QuadPart)
		return 0.0;

	return (double)g_air_ticks * 1.0e6 / (double)rate.QuadPart;
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
	int stand, unsigned int at, unsigned int first, std::vector<PlaceTri>& out)
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
		out.push_back(tri);
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

// HDRenderBuildHash: the fingerprint of the builds that follow, on or off.
static int g_build_hash = 0;

void HDRenderBuildHash(int on)
{
	g_build_hash = on ? 1 : 0;
}

/* One vertex as the vertex buffer holds it: the .TRG's vertex and the three
 * water mark bits the build found for it (bit 0 choppy, 1 shimmer, 2 room
 * shimmer). */
static HDVertex ComposeVertex(const trf::remaster::TrgVertex& in,
	unsigned char bits)
{
	HDVertex out;

	out.x = in.x;
	out.y = in.y;
	out.z = in.z;
	out.sway = in.sway;
	out.nx = in.nx;
	out.ny = in.ny;
	out.nz = in.nz;
	out.layer = in.layer;
	out.c1b = in.colour1[2];
	out.c1g = in.colour1[1];
	out.c1r = in.colour1[0];
	out.u = in.u;
	out.c2b = in.colour2[2];
	out.c2g = in.colour2[1];
	out.c2r = in.colour2[0];
	out.v = in.v;
	out.wChoppy = (unsigned char)(bits & 1);
	out.wShimmer = (unsigned char)((bits >> 1) & 1);
	out.wRoomShimmer = (unsigned char)((bits >> 2) & 1);
	out.wSpare = 1;
	return out;
}

// One draw call's fields and the index buffer's bytes it names.
static void HashRange(HDFnv64& f, const HDRange& r,
	const std::vector<unsigned int>& sorted)
{
	f.put(r.start);
	f.put(r.count);
	f.put(r.pageSlot);
	f.put(r.scrollU);
	f.put(r.scrollV);
	f.put(r.alphaRef);
	f.put(r.addWeight);
	f.put(r.addPeriod);
	f.put(r.flatLight);
	f.put(r.flipTime);
	f.put(r.flipSlot);
	f.put(r.bookTime);
	f.put(r.bookFirst);
	f.put(r.bookCount);
	f.put(r.bookPhase);
	f.put(r.bookPart);
	f.put(r.gloss);
	f.put(r.glossPower);
	f.put(r.glossMat);
	f.put(r.sheen);
	f.put(r.sheenPower);
	f.put(r.sheenF0);
	f.put(r.sheenTint);
	f.put(r.sheenMask);
	f.put(r.sheenClass);
	f.put(r.sheenSource);
	f.put(r.ripple);
	f.put(r.blend);
	f.put(r.clear);
	f.put(r.destructId);
	f.put(r.material);
	f.put(r.glow);
	f.put(r.artTris);
	f.put(r.artY);
	f.put(r.artYMax);
	f.put(r.artMinX);
	f.put(r.artMaxX);
	f.put(r.artMinZ);
	f.put(r.artMaxZ);
	f.put((int)r.artHidden);
	f.list(r.artCells);
	f.put((int)r.artSpared);
	f.put(r.flowTris);
	f.put(r.flowY);
	f.put(r.flowYMax);
	f.put(r.flowMinX);
	f.put(r.flowMaxX);
	f.put(r.flowMinZ);
	f.put(r.flowMaxZ);
	f.list(r.flowCells);
	f.put(r.intruding);
	f.put(r.stand);
	f.put(r.overhang);
	f.put(r.cull);
	f.put(r.baseVertex);
	f.put(r.vertexSpan);

	if (r.count && (size_t)r.start + r.count <= sorted.size())
		f.add(&sorted[r.start], (size_t)r.count * sizeof(unsigned int));
}

// One group: its draw calls and the tables the index fill made for it.
static unsigned long long HashGroup(const HDGroup& g,
	const std::vector<unsigned int>& sorted)
{
	HDFnv64 f;

	for (int pass = 0; pass < 3; pass++)
	{
		f.put((unsigned int)g.pass[pass].size());

		for (const HDRange& r : g.pass[pass])
			HashRange(f, r, sorted);
	}

	f.put(g.clearRanges);
	f.list(g.samples);
	f.list(g.cells);
	f.list(g.cellsIn);
	f.list(g.cellsFree);
	f.put((unsigned int)g.standCells.size());

	for (const std::vector<unsigned int>& cells : g.standCells)
		f.list(cells);

	f.put(g.triangles);
	f.put(g.overhang);
	f.put(g.intruding);
	f.list(g.intrusionAt);
	f.list(g.faceAt);
	f.list(g.pairRanges);
	f.list(g.pairWhole);
	f.put(g.airTris);
	f.put(g.airBoxed);
	f.list(g.standRanges);
	f.list(g.standWith);
	f.list(g.cutAt);
	f.put(g.cutTris);
	f.put(g.roomMins);
	f.put(g.roomMaxs);
	f.put((int)g.hasRoomBox);
	f.put(g.mins);
	f.put(g.maxs);
	f.put((int)g.hasBounds);
	f.put(g.motion);
	f.put(g.waterPlanes);

	for (int k = 0; k < g.waterPlanes && k < HDGroup::kMaxPlanes; k++)
	{
		f.put(g.waterBox[k]);
		f.put(g.waterY[k]);
	}

	for (const std::vector<HDFlatPlane>* rows : { &g.flats, &g.waterFlats })
	{
		f.put((unsigned int)rows->size());

		for (const HDFlatPlane& row : *rows)
		{
			f.put(row.y);
			f.list(row.cells);
		}
	}

	return f.h;
}

/* The level's fingerprint (HDRenderBuildHash): the vertex buffer out of the
 * .TRG and the mark bits the build kept, every group, then the level tables
 * the index fill writes and its counts. */
static unsigned long long BuildHash(const trf::remaster::Trg& trg,
	const std::vector<unsigned char>& bits,
	const std::vector<unsigned int>& sorted, int threads)
{
	HDFnv64 level;
	const size_t block = 65536;
	const size_t count = (size_t)g_vertex_count;
	const size_t blocks = (count + block - 1) / block;

	/* The blocks and the groups are hashed on the pool, each into its own
	 * slot, and fed into the level's hash in their order afterwards. */
	std::vector<unsigned long long> part(blocks + g_groups.size(), 0);

	HDPoolRun((int)part.size(), threads, [&](int at)
	{
		if ((size_t)at >= blocks)
		{
			part[(size_t)at] = HashGroup(g_groups[(size_t)at - blocks], sorted);
			return;
		}

		HDFnv64 f;
		const size_t first = (size_t)at * block;

		for (size_t i = first; i < first + block && i < count; i++)
			f.put(ComposeVertex(trg.vertices[i],
				i < bits.size() ? bits[i] : (unsigned char)0));

		part[(size_t)at] = f.h;
	});

	for (unsigned long long h : part)
		level.put(h);

	level.put((unsigned int)g_destruct.size());

	for (const HDDestructible& d : g_destruct)
	{
		level.put(d.group);
		level.put(d.material);
		level.put(d.mins);
		level.put(d.maxs);
		level.put((int)d.hasBox);
	}

	level.list(g_stand_tris);
	level.list(g_stand_tri_holder);
	level.put((unsigned int)g_stand_covers.size());

	for (const StandCoverBuilt& c : g_stand_covers)
	{
		level.put(c.cover.group);
		level.put(c.cover.holder);
		level.put(c.cover.tris);
		level.put(c.cover.faces);
		level.put(c.cover.facesCovered);
		level.put(c.cover.faceCount);
		level.put((unsigned long long)c.firstFlag);
	}

	level.list(g_stand_cover_faces);
	level.list(g_book_parts);
	level.list(g_book_slots);
	level.list(g_place_tris);
	level.list(g_clear_seen);
	level.list(g_flow_feet);
	level.list(g_book_runs);
	level.list(g_intrusion_hits);
	level.list(g_intrusion_band);
	level.list(g_cut_hits);
	level.put((unsigned int)g_air_pairs.size());

	for (const std::pair<const std::pair<int, int>, int>& pair : g_air_pairs)
	{
		level.put(pair.first.first);
		level.put(pair.first.second);
		level.put(pair.second);
	}

	const int counts[] =
	{
		g_stand_kept, g_stand_from_intrusions, g_cutout_parts,
		g_addweight_parts, g_addswing_parts, g_addstill_parts,
		g_stacked_parts, g_stacked_tris, g_flat_parts, g_gloss_parts,
		g_flat_planes, g_blend_parts, g_blend_ranges, g_clear_parts,
		g_clear_pages, g_clear_ranges, g_water_parts, g_water_opaque,
		g_water_additive, g_water_tris, g_water_subrect, g_water_still,
		g_water_span, g_flip_parts, g_flip_dropped, g_flip_ranges,
		g_book_ranges, g_scroll_parts
	};

	level.put(counts);
	level.put(g_addweight_min);
	level.put(g_water_cap);
	level.put(g_gloss_parts_by);
	return level.h;
}

const char* HDRenderBeginPhaseName(int phase)
{
	static const char* const names[HD_BEGIN_PHASES] =
	{
		"prep", "pages", "owners", "verts", "setup", "fill", "index"
	};

	return phase >= 0 && phase < HD_BEGIN_PHASES ? names[phase] : "?";
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

	// The parse of the load thread's scan, when it kept one of this file.
	if (!HDScanTakeTrg(trg_path.c_str(), &trg) &&
		!trf::remaster::read_trg(trg_path, trg, error))
	{
		g_error = error;
		*why = g_error.c_str();
		return false;
	}

	const double read_ms = now_ms() - read_started;
	const double build_started = now_ms();

	// The build's parts for the log (HD_BEGIN_*): each lap books the time
	// since the previous one to its phase.
	double phase_ms[HD_BEGIN_PHASES] = {};
	double lap_at = build_started;
	auto lap = [&](int phase)
	{
		const double now = now_ms();

		phase_ms[phase] += now - lap_at;
		lap_at = now;
	};

	// The sky band's poles closed (HDRenderSkyCaps, the sky dome below).
	HDRenderSkyCaps(trg, numRooms);

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

	lap(HD_BEGIN_PREP);

	if (!HDPageCacheCreate(g_device, installRoot, pages.empty() ? 0 : &pages[0],
		(int)pages.size(), managedPages, threads, &page_stats, why))
		return false;

	lap(HD_BEGIN_PAGES);

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

	/* The art's falling water, for the waterfalls' feet (g_flow_feet). Only
	 * the rooms' groups: the sky has no water under it. */
	g_flow_feet.clear();

	for (size_t g = 0; g < trg.groups.size() && (int)g < g_room_groups; g++)
		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
		{
			if (part.material != HD_TRG_MATERIAL_FLOW)
				continue;

			int lo = 1 << 30, hi = -(1 << 30);
			std::vector<unsigned int> foot;

			for (int pass = 0; pass < 2; pass++)
				for (int p = 0; p < 3; p++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[p];

					for (unsigned int at = run.start; at < run.start +
						run.count && at < trg.indices.size(); at++)
					{
						const unsigned int v = trg.indices[at];

						if (v >= trg.vertices.size())
							continue;

						const int y = trg.vertices[v].y;

						if (!pass)
						{
							lo = y < lo ? y : lo;
							hi = y > hi ? y : hi;
						}
						else if (y >= hi - HD_WATER_FOOT_BAND)
							foot.push_back(v);
					}
				}

			if ((float)(hi - lo) <= kFlowFall)
				continue;

			// Each vertex once, then evenly over the foot's own order.
			std::sort(foot.begin(), foot.end());
			foot.erase(std::unique(foot.begin(), foot.end()), foot.end());

			const size_t step = foot.size() / kFlowFootKept + 1;

			for (size_t k = 0; k < foot.size(); k += step)
			{
				const trf::remaster::TrgVertex& at = trg.vertices[foot[k]];
				FlowFoot f = { (int)g, (float)at.x, (float)at.y, (float)at.z };

				g_flow_feet.push_back(f);
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

	/* Which vertices carry either shimmer mark, kept for the triangle walk
	 * below (the art over a sheet, HDRange::artTris): the vertex buffer is
	 * write-only. */
	std::vector<unsigned char> vertex_marked((size_t)g_vertex_count, 0);
	/* And every vertex's three mark bits, kept for the fingerprint alone
	 * (HDRenderBuildHash) - empty while it is off. */
	std::vector<unsigned char> vertex_bits(g_build_hash
		? (size_t)g_vertex_count : 0, 0);

	lap(HD_BEGIN_OWNERS);

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

		/* In blocks of 65,536 vertices on the pool (HDPoolRun). A vertex
		 * reads the .TRG and the mark tables, which nothing writes any more,
		 * and writes only its own slot of the buffer, of vertex_marked and
		 * of vertex_bits; each block counts into its own row, and the rows
		 * are added up after the join. The vertex is put together on the
		 * stack and stored whole (ComposeVertex): the buffer is WRITEONLY
		 * memory and is never read back. */
		struct VertexCounts
		{
			int sway, marked, choppy, shimmer, roomShimmer, flooded, wall;
		};

		const auto one = [&](int i, VertexCounts& c)
		{
			const trf::remaster::TrgVertex& in = trg.vertices[(size_t)i];

			// The shader reads the weight; the count is for the log and for
			// the tool, and it is the whole level in one number.
			if (in.sway)
				c.sway++;

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
						c.flooded++;
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
					c.wall++;
			}

			const HDVertex vertex = ComposeVertex(in, bits);

			out[i] = vertex;
			vertex_marked[(size_t)i] = (unsigned char)((bits & 6) ? 1 : 0);

			if (!vertex_bits.empty())
				vertex_bits[(size_t)i] = bits;

			if (bits)
				c.marked++;

			c.choppy += vertex.wChoppy;
			c.shimmer += vertex.wShimmer;
			c.roomShimmer += vertex.wRoomShimmer;
		};

		const int block = 65536;
		const int blocks = (g_vertex_count + block - 1) / block;
		std::vector<VertexCounts> counts((size_t)blocks, VertexCounts());

		HDPoolRun(blocks, threads, [&](int b)
		{
			const int last = g_vertex_count - b * block < block
				? g_vertex_count : b * block + block;

			for (int i = b * block; i < last; i++)
				one(i, counts[(size_t)b]);
		});

		for (const VertexCounts& c : counts)
		{
			sway_vertices += c.sway;
			marked_vertices += c.marked;
			marked_choppy += c.choppy;
			marked_shimmer += c.shimmer;
			marked_room_shimmer += c.roomShimmer;
			flooded_vertices += c.flooded;
			marked_wall += c.wall;
		}

		g_vb->Unlock();
	}

	lap(HD_BEGIN_VERTS);

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
	 * The shader moves every marked vertex up and down by up to the class's
	 * whole wave height times its swell share - 64 units in a sea, under 4 in
	 * a channel, none in a basin (hd_room.hlsl, gSwell; y counts downwards
	 * in this engine). The vertices at the water's own edge are marked like
	 * the rest, so without a taper the surface rises over the bank it should
	 * meet: in the
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
		float ripple[3];		// see HDWaterArt
		int ripplePart;			// see HDRange::ripplePart
		int water16;			// a material 16 part, still or not
		float blend;			// see MaterialBlend
		int clear;			// see HDRange::clear
		int destructId;			// see HDDestructible
		int material;			// the additive pass's material, else 0
		int glow;			// see BuildGlowParts, else -1
		int overhang;			// see HDRoomBox
		int intruding;			// see HDIntrusion
		int openAir;			// a piece only for the air, see open_air
		int stand;			// see HDStandRange
		int cull;			// see RangeCulls
		int behind;			// see HDRange::behind
		unsigned int first;
	};

	std::vector<unsigned char> sway_seen(trg.vertices.size(), 0);

	/* What one group's index fill leaves for the join (see fill_group):
	 * its index run, its entries of the level-wide lists - with the ids and
	 * the indices into them its own until the join moves them - and its
	 * hits and counts. */
	struct FillOut
	{
		std::vector<unsigned int> sorted;
		std::vector<HDDestructible> destruct;
		std::vector<HDRenderBookPart> bookParts;
		std::vector<StandCoverBuilt> standCovers;
		std::vector<unsigned char> standCoverFaces;
		std::vector<HDCoverTri> standTris;
		std::vector<int> standTriHolder;
		std::vector<PlaceTri> placeTris;
		std::vector<unsigned int> swayHits;	// each weighted corner, in order
		std::vector<unsigned char> clearSeen;	// per page slot, this group
		std::vector<int> clearSlots;		// first seen here, in order
		std::vector<int> cutHits, intrusionHits, intrusionBand, airPairs;
		std::vector<int> carryHosts;		// CarryHostsOf, one per triangle
		std::vector<int> carryCopies;		// and those that are copies
		long long airTicks = 0;
		int cutoutParts = 0, addweightParts = 0, addswingParts = 0;
		int addstillParts = 0, stackedParts = 0, stackedTris = 0;
		int waterParts = 0, waterTris = 0, waterOpaque = 0;
		int waterAdditive = 0, waterStill = 0, waterSubrect = 0;
		int waterSpan = 255;
		float waterCap = 0.0f;
		float addweightMin = 1.0f;
		int flatParts = 0, glossParts = 0, blendParts = 0, flipParts = 0;
		int flipDropped = 0, scrollParts = 0, clearParts = 0;
		int flipRanges = 0, bookRanges = 0, blendRanges = 0, clearRanges = 0;
		int flatPlanes = 0, standFromIntrusions = 0;
		int splitRanges = 0, biggest = 0, fallbackTris = 0;
		int glossPartsBy[5] = { 0, 0, 0, 0, 0 };
	};

	std::vector<FillOut> fill_out(trg.groups.size());

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

	lap(HD_BEGIN_SETUP);

	/* --- the index fill, one group at a time on the pool -------------
	 *
	 * The groups are independent: a group's fill reads the .TRG, the tables
	 * handed in before the build (boxes, intrusions, pair faces, cuts, stand
	 * holders, air), the vertex marks and the page cache, none of which it
	 * or another group writes, and it writes its own slot of g_groups and
	 * its own FillOut. The join after it puts the groups together in group
	 * order, so the buffers, the tables and every count come out the same
	 * whatever the thread count
	 * (HDRenderBuildHash says so per level). */
	const auto fill_group = [&](size_t g)
	{
		FillOut& o = fill_out[g];
		HDGroup& group = g_groups[g];
		const trf::remaster::TrgGroup& source = trg.groups[g];
		/* The group's own index run and sort list; the join appends the runs
		 * in group order. */
		std::vector<unsigned int>& sorted = o.sorted;
		std::vector<HDTri> tris;
		const size_t group_first_index = sorted.size();

		o.clearSeen.assign((size_t)HDPageCacheSlots(), 0);

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
				made.firstFlag = o.standCoverFaces.size();

				for (int f = 0; f < holder.faceCount; f++)
					o.standCoverFaces.push_back((unsigned char)(
						(size_t)f < count.faceShare.size() &&
						HDCoverFaceCovered(count.faceShare[(size_t)f]) ? 1 : 0));

				o.standCovers.push_back(made);
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
		/* And out of its clear floors alone (HD_PASS_CLEAR): the planes the
		 * art lets what lies under them show through (the wet floor's copy
		 * rule, wet_copied). */
		std::vector<std::pair<int, unsigned int> > clearflats;
		/* The GLOSSLOG part counts of this group, added to the level's once
		 * its passes are built. */
		int group_parts[5] = { 0, 0, 0, 0, 0 };

		/* The art of this group in unlinked air (AirHostsOf), read back by
		 * the index fill below - asked before the passes, so that the time
		 * it takes is one stretch per group. */
		AirHosts airHosts;

		if ((int)g < g_room_air_count && !g_air_cells.empty())
		{
			LARGE_INTEGER from, to;

			QueryPerformanceCounter(&from);
			AirHostsOf(trg, source, (int)g, airHosts);
			QueryPerformanceCounter(&to);
			o.airTicks += to.QuadPart - from.QuadPart;
		}

		// And the rooms under the sky its art stands in (CarryHostsOf).
		CarryHostsOf(trg, source, (int)g, o.carryHosts, o.carryCopies);

		/* The art's copy of the room under its polished floors, left out
		 * of the level while the marble floors are on (marble_copies). */
		std::vector<std::vector<unsigned char> > marbleCopies;

		group.mirrorCopies = g_marble_on && !g_marble_keep_copies &&
			(int)g < g_room_groups ? marble_copies(trg, source, (int)g,
			marbleCopies) : 0;

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
					o.cutoutParts++;

				if (pass == 2 && aweight != 1.0f)
				{
					o.addweightParts++;

					if (aweight < o.addweightMin)
						o.addweightMin = aweight;
				}

				if (pass == 2 && aperiod != 0.0f)
					o.addswingParts++;

				/* The water ripple. Material 16 in whatever pass the file
				 * sorts it into - 25 of its 28 parts are additive and 3 are
				 * opaque, and the file's own choice is kept - and three zeroes
				 * for everything else, which is every part of nine of the
				 * fifteen levels. The group keeps each part once, for the
				 * water to give its ripple (water_art_ripples). */
				const int art_at = (int)(&part - &source.parts[0]);
				HDWaterArt art;
				const bool water_art = WaterArtOf(trg, part, art_at, art);
				const float* ripple = art.ripple;
				const bool ripples = ripple[1] > 0.0f;

				if (water_art && run.count)
				{
					bool known = false;

					for (const HDWaterArt& a : group.waterArt)
						known = known || a.part == art_at;

					if (!known)
						group.waterArt.push_back(art);
				}

				// Counted as a water surface whether or not it ripples.
				const bool water =
					MaterialBase(part) == HD_TRG_MATERIAL_WATER;

				/* A sea stacked in the file is drawn once - see
				 * MarkStackedCopies. The additive pass only: that is where
				 * the copies sum. */
				std::vector<unsigned char> stacked;

				if (pass == 2 && water && run.count && !g_keep_stacks &&
					MarkStackedCopies(trg, run, stacked))
					o.stackedParts++;

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
					o.addstillParts++;

				if (water && run.count)
				{
					float spanU = 0.0f, spanV = 0.0f;

					PartUvSpan(trg, part, spanU, spanV);

					const int span = (int)((spanU < spanV ? spanU : spanV) *
						255.0f + 0.5f);

					o.waterParts++;
					o.waterTris += (int)(run.count / 3);

					if (pass == 0)
						o.waterOpaque++;
					else if (pass == 2)
						o.waterAdditive++;

					if (!ripples)
						o.waterStill++;

					if (span < 255)
						o.waterSubrect++;

					if (span < o.waterSpan)
						o.waterSpan = span;

					if (ripple[1] > o.waterCap)
						o.waterCap = ripple[1];
				}

				/* The sky band draws at light 1 - see PartIsUnlit. Only
				 * in a sky group, and the answer is the same for all three
				 * of the part's passes, so it is asked once. */
				const float flat = (int)g >= g_room_groups &&
					PartIsUnlit(trg, part) ? 1.0f : 0.0f;

				if (pass == 0 && flat != 0.0f)
					o.flatParts++;

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
					o.glossParts++;

				/* The see-through parts of the alpha pass - material 14,
				 * and nothing else in the game. */
				const float blend = MaterialBlend(part, pass);

				// A part can name a pass it puts no triangle in; counting
				// those would say a level has glass where it has none.
				if (blend != 0.0f && run.count)
					o.blendParts++;

				// One-sided or two-sided - see RangeCulls.
				const int cull = RangeCulls(pass, aref, blend, water,
					(int)g >= g_room_groups);

				/* The destructible pieces - see HDDestructible above.
				 * One id per (group, material); the box is filled from the
				 * part's own vertices as they are sorted in below. */
				int destruct_id = -1;

				if (part.material & 0x8000u)
				{
					for (size_t at = 0; at < o.destruct.size(); at++)
						if (o.destruct[at].group == (int)g &&
							o.destruct[at].material == part.material)
						{
							destruct_id = (int)at;
							break;
						}

					if (destruct_id < 0)
					{
						HDDestructible piece;

						piece.group = (int)g;
						piece.material = part.material;
						destruct_id = (int)o.destruct.size();
						o.destruct.push_back(piece);
					}
				}

				if (ftime != 0.0f && !MaterialFlipSlots(trg, part, fslot))
					ftime = 0.0f;

				if (pass == 0 && part.material == HD_TRG_MATERIAL_FLIPBOOK)
				{
					group.motion.flipbookParts++;

					if (ftime != 0.0f)
						o.flipParts++;
					else
						o.flipDropped++;
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
						bfirst = MaterialBookSlotsMade(book_page0, bcount);

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
					o.bookParts.push_back(book);
				}

				if (pass == 0 && scrolls)
					o.scrollParts++;

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

					// A copy under a polished floor (marble_copies).
					if (!marbleCopies.empty() &&
						marbleCopies[part_at * 3 + (size_t)pass][at / 3])
						continue;

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

							o.cutHits.push_back(group.cutAt[k]);
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
							o.fallbackTris++;
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
							/* A clear floor, by the test of clear_tri below;
							 * one of the mirror column is drawn opaque while
							 * the marble floors are on (see "the marble
							 * floors"), and nothing shows through it. */
							const bool clearFloor = pass == 1 &&
								blend == 0.0f && !g_no_clear &&
								(int)g < g_room_groups &&
								HDPageCacheSlotClear(slot) && !(g_marble_on &&
								MirrorPage(HDPageCachePageOfSlot(slot)));

							for (int cx = x0 >> 10; cx <= lastX; cx++)
								for (int cz = z0 >> 10; cz <= lastZ; cz++)
								{
									flats.push_back(std::make_pair((int)a.y,
										SectorKey(cx << 10, cz << 10)));

									if (sheet)
										waterflats.push_back(std::make_pair(
											(int)a.y, SectorKey(cx << 10, cz << 10)));

									if (clearFloor)
										clearflats.push_back(std::make_pair(
											(int)a.y, SectorKey(cx << 10,
											cz << 10)));
								}
						}
					}

					/* A mirror face of the marble floors (see "the marble
					 * floors"): a triangle of a room's opaque pass or of its
					 * clear floors, on a page of the mirror column, flat and
					 * facing up (HDMarbleFace). */
					if (g_marble_on && (int)g < g_room_groups &&
						(pass == 0 || (pass == 1 && blend == 0.0f &&
						!g_no_clear && HDPageCacheSlotClear(slot))) &&
						MirrorPage(HDPageCachePageOfSlot(slot)))
					{
						float p[9];
						float ny = 0.0f;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];

							p[c * 3] = (float)v.x;
							p[c * 3 + 1] = (float)v.y;
							p[c * 3 + 2] = (float)v.z;
							ny += v.normal_y();
						}

						if (HDMarbleFace(p, p + 3, p + 6, ny / 3.0f))
						{
							group.mirrorFaces.insert(
								group.mirrorFaces.end(), p, p + 9);
							group.mirrorY.push_back((int)lroundf(
								(p[1] + p[4] + p[7]) / 3.0f));
						}
					}

					/* A higher copy of a stacked sea. Skipped after the
					 * horizontal-plane rows above and not before them: those
					 * rows answer whether the remaster built anything in a
					 * plane, and every copy is an answer to that. */
					if (!stacked.empty() && stacked[at / 3])
					{
						o.stackedTris++;
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
					 * which is what the level's log line says per pair.
					 *
					 * A box holds a triangle as well when the centroid lies
					 * in it in x and z and the triangle reaches into the
					 * holder's air (AirHostsOf, HDRoomAirReaches) with its
					 * centroid above or below the box: the foot of a wall
					 * whose centroid stands over the holder's ceiling. Held
					 * by its centroid alone, ANDREA1's facade of room 101
					 * hangs 86 units of it into room 93 below its ceiling,
					 * drawn with the open half - a strip of facade seen from
					 * room 93 where the classic frame shows room 93's own
					 * ceiling. With tomb5.hd_air off no air is asked and
					 * the centroid alone decides. */
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

						if (!in && !airHosts.empty() &&
							centre[0] >= box.mins[0] &&
							centre[0] <= box.maxs[0] &&
							centre[2] >= box.mins[2] &&
							centre[2] <= box.maxs[2])
							in = std::binary_search(airHosts.begin(),
								airHosts.end(), std::make_pair(first,
									box.holder));

						if (in)
						{
							if (pair_at < 0)
								pair_at = group.intrusionAt[k];

							o.intrusionHits.push_back(group.intrusionAt[k]);
						}
					}

					/* Whether it stands in the air of a room this group's
					 * room has no link with, out of the asking above the
					 * passes: counted per (group, host), and once more when
					 * an entry's box holds it too - the art the air holds to
					 * its own room's window, but what a stand range or a
					 * static's box takes below. */
					int air = 0;

					if (!airHosts.empty())
					{
						AirHosts::const_iterator it = std::lower_bound(
							airHosts.begin(), airHosts.end(),
							std::make_pair(first, -1));

						for (; it != airHosts.end() && it->first == first; ++it)
						{
							air = 1;
							o.airPairs.push_back(it->second);
						}
					}

					if (air)
						group.airTris++;

					if (air && pair_at >= 0)
						group.airBoxed++;

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
							o.standFromIntrusions++;

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
							o.intrusionBand.push_back(pair_at);

							if (!air || box_at >= 0)
								pair_at = -1;
						}
					}

					/* Art in unlinked air that an entry's box holds, which the
					 * box's answer would draw with the open half (band art, no
					 * face claiming it outside the smaller box), becomes a
					 * piece of its own so that DecidePairs can hold it to its
					 * own room's window. Art a stand range or a static's box
					 * has keeps that answer. Art in no entry's box keeps the
					 * open half: most of it hangs far outside its own room
					 * where the classic frame shows it (ANDREA2's street art
					 * and courtyard palms); held to its window it took
					 * 1,188,671 of the air rule's 1,379,566 pixels on the 131
					 * room-centre views (hd_level.cpp, BuildRoomIntrusions). */
					const bool open_air = air && pair_at >= 0 &&
						stand_at < 0 && box_at < 0 && face_at < 0 && !inner;
					/* A hull (MarkAirHulls) is for the most part a second
					 * copy of its hosts' own faces, cut into other
					 * triangles, its walls as well as the art in their air:
					 * where both are drawn the two fight for the depth in a
					 * speckle that moves with the eye (RICH2's shaft from
					 * room 63: 61's art lies within 4 units of rooms 62 to
					 * 65's at 140,401 of the 219,084 pixels both draw at
					 * 640x360; the eye moved one unit changes 45,146 pixels
					 * at 1280x720, 9,019 without 61). So all of a hull's art
					 * outside its own room, or in unlinked air, is drawn
					 * pushed back as a stand range answered HD_STAND_BEHIND
					 * is: the host's faces win wherever the two lie within
					 * the push, and the hull's art stays where it stands
					 * alone or in front (10,552 pixels at that step; with
					 * the air's art alone pushed, 33,503). */
					const bool hull = g < g_group_hull.size() &&
						g_group_hull[g];
					const bool hull_back = hull && (air || overhang);
					/* And with tomb5.hd_air 2 that art is a piece of its own
					 * with no entry, no holder and no face, which DecidePairs
					 * holds to its own room's window like the one above. */
					const bool hull_air = hull && air && pair_at < 0 &&
						stand_at < 0 && box_at < 0 && g_air_hulls;

					if (pair_at >= 0)
					{
						int piece = -1;

						for (size_t j = 0; j < group.pairRanges.size(); j++)
							if (group.pairRanges[j].intrusion == pair_at &&
								group.pairRanges[j].face == face_at &&
								group.pairRanges[j].inner == inner &&
								group.pairRanges[j].air == air)
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
							made.air = air;
							made.behind = face_at >= 0 &&
								(size_t)face_at < g_face_behind.size()
								? g_face_behind[(size_t)face_at] : 0;
							piece = (int)group.pairRanges.size();
							group.pairRanges.push_back(made);
							group.pairWhole.push_back(0);
						}

						group.pairRanges[(size_t)piece].triangles++;
						intruding = piece + 1;
					}
					else if (hull_air)
					{
						int piece = -1;

						for (size_t j = 0; j < group.pairRanges.size(); j++)
							if (group.pairRanges[j].intrusion < 0)
							{
								piece = (int)j;
								break;
							}

						if (piece < 0)
						{
							HDPairRange made;

							made.intrusion = -1;
							made.holder = -1;
							made.face = -1;
							made.inner = 0;
							made.triangles = 0;
							made.air = 1;
							made.behind = 0;
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

								faces |= o.standCoverFaces[stand_flags[k] +
									(size_t)f]
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
							o.standTris.push_back(cover);
							o.standTriHolder.push_back(holder);
						}
					}

					HDTri tri;

					tri.overhang = overhang;
					tri.intruding = intruding;
					tri.openAir = open_air || hull_air ? 1 : 0;
					tri.stand = stand;
					tri.cull = cull;
					tri.behind = hull_back ? 1 : 0;

					if (hull_back)
						group.hullBehind++;
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
					tri.ripplePart = water ? art_at : -1;
					tri.water16 = water ? 1 : 0;

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
							o.clearParts++;
						}

						if ((size_t)slot < o.clearSeen.size() &&
							!o.clearSeen[(size_t)slot])
						{
							o.clearSeen[(size_t)slot] = 1;
							o.clearSlots.push_back(slot);
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
						const HDGloss named =
							PageGlossKnown(HDPageCachePageOfSlot(slot));

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

			/* The sort key of a draw call. Sorted stably over the triangles'
			 * numbers and not over the records themselves - a record is 33
			 * words and std::stable_sort moves what it sorts - then laid out
			 * in that order in place, one cycle of the permutation at a time:
			 * the same order a stable sort of the records gives, without a
			 * second list of them. */
			const auto tri_less = [](const HDTri& a, const HDTri& b)
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
					 * draw call, and a material 16 part's ranges are its own:
					 * the water under each part gives it its ripple after the
					 * build (water_art_ripples). Every draw call without one
					 * first, then the parts, the later part of a group first -
					 * a free choice; JOBY5's room 49 holds the game's only
					 * two on one page and in one pass, and their order decides
					 * which vertices the group's samples take (group_light). */
					if ((a.ripplePart >= 0) != (b.ripplePart >= 0))
						return b.ripplePart >= 0;
					if (a.ripplePart != b.ripplePart)
						return a.ripplePart > b.ripplePart;
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
					if (a.stand != b.stand)
						return a.stand < b.stand;

					// The same for a hull's art drawn pushed back.
					return a.behind < b.behind;
				};

			{
				std::vector<unsigned int> order(tris.size());

				for (size_t i = 0; i < order.size(); i++)
					order[i] = (unsigned int)i;

				std::stable_sort(order.begin(), order.end(),
					[&](unsigned int a, unsigned int b)
					{
						return tri_less(tris[a], tris[b]);
					});

				// The record at i becomes the one at order[i].
				std::vector<unsigned char> laid(order.size(), 0);

				for (size_t i = 0; i < order.size(); i++)
				{
					if (laid[i])
						continue;

					const HDTri first = tris[i];
					size_t at = i;

					for (;;)
					{
						const size_t from = order[at];

						laid[at] = 1;

						if (from == i)
						{
							tris[at] = first;
							break;
						}

						tris[at] = tris[from];
						at = from;
					}
				}
			}

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
			range.ripplePart = tris[0].ripplePart;
			range.blend = tris[0].blend;
			range.clear = tris[0].clear;
			range.destructId = tris[0].destructId;
			range.material = tris[0].material;
			range.glow = tris[0].glow;
			range.intruding = tris[0].intruding;
			range.stand = tris[0].stand;
			range.behind = tris[0].behind;
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
					tris[i].ripplePart == range.ripplePart &&
					tris[i].blend == range.blend &&
					tris[i].clear == range.clear &&
					tris[i].destructId == range.destructId &&
					tris[i].material == range.material &&
					tris[i].glow == range.glow &&
					tris[i].intruding == range.intruding &&
					tris[i].stand == range.stand &&
					tris[i].behind == range.behind &&
					tris[i].cull == range.cull &&
					(!g_split_overhang ||
						tris[i].overhang == range.overhang);

				if (!same || range.count / 3 >= max_tris)
				{
					if (range.count)
					{
						if (same)
							o.splitRanges++;

						group.pass[pass].push_back(range);

						if (range.flipTime != 0.0f)
							o.flipRanges++;

						if (range.bookTime != 0.0f)
							o.bookRanges++;

						if (range.blend != 0.0f && !range.clear)
							o.blendRanges++;

						if (range.clear)
						{
							o.clearRanges++;
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

						if ((int)(range.count / 3) > o.biggest)
							o.biggest = (int)(range.count / 3);
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
					range.ripplePart = tris[i].ripplePart;
					range.blend = tris[i].blend;
					range.clear = tris[i].clear;
					range.destructId = tris[i].destructId;
					range.material = tris[i].material;
					range.glow = tris[i].glow;
					range.intruding = tris[i].intruding;
					range.stand = tris[i].stand;
					range.behind = tris[i].behind;
					range.overhang = tris[i].overhang;
					range.cull = tris[i].cull;
					range.start = (unsigned int)sorted.size();
					range.count = 0;
					range.artTris = 0;
					range.artCells.clear();
					range.artSpared = false;
					range.flowTris = 0;
					range.flowCells.clear();
				}

				const unsigned int first = tris[i].first;

				/* The remaster's water art (HDRange::artTris): horizontal,
				 * and material 16 or marked at every corner. */
				{
					const unsigned int ia = trg.indices[first];
					const unsigned int ib = trg.indices[first + 1];
					const unsigned int ic = trg.indices[first + 2];

					if (ia < trg.vertices.size() && ib < trg.vertices.size() &&
						ic < trg.vertices.size())
					{
						const trf::remaster::TrgVertex& a = trg.vertices[ia];
						const trf::remaster::TrgVertex& b = trg.vertices[ib];
						const trf::remaster::TrgVertex& c = trg.vertices[ic];
						const bool marked = ia < vertex_marked.size() &&
							ib < vertex_marked.size() &&
							ic < vertex_marked.size() && vertex_marked[ia] &&
							vertex_marked[ib] && vertex_marked[ic];

						if (a.y == b.y && b.y == c.y &&
							(tris[i].water16 || marked))
						{
							auto lo = [](int p, int q)
							{
								return p < q ? p : q;
							};
							auto hi = [](int p, int q)
							{
								return p > q ? p : q;
							};
							const int x0 = lo(lo(a.x, b.x), c.x);
							const int x1 = hi(hi(a.x, b.x), c.x);
							const int z0 = lo(lo(a.z, b.z), c.z);
							const int z1 = hi(hi(a.z, b.z), c.z);

							if (!range.artTris)
							{
								range.artY = range.artYMax = (int)a.y;
								range.artMinX = x0;
								range.artMaxX = x1;
								range.artMinZ = z0;
								range.artMaxZ = z1;
							}
							else
							{
								range.artY = lo(range.artY, (int)a.y);
								range.artYMax = hi(range.artYMax, (int)a.y);
								range.artMinX = lo(range.artMinX, x0);
								range.artMaxX = hi(range.artMaxX, x1);
								range.artMinZ = lo(range.artMinZ, z0);
								range.artMaxZ = hi(range.artMaxZ, z1);
							}

							range.artTris++;

							// Its squares (HDRange::artCells).
							NoteBoxCells(range.artCells, x0, x1, z0, z1);
						}

						/* The flow material's flat triangles (HDRange::
						 * flowTris) - the additive pass is the one that
						 * names its material. */
						if (a.y == b.y && b.y == c.y && range.material == 1)
						{
							const int x0 = a.x < b.x ? (a.x < c.x ? a.x : c.x) :
								(b.x < c.x ? b.x : c.x);
							const int x1 = a.x > b.x ? (a.x > c.x ? a.x : c.x) :
								(b.x > c.x ? b.x : c.x);
							const int z0 = a.z < b.z ? (a.z < c.z ? a.z : c.z) :
								(b.z < c.z ? b.z : c.z);
							const int z1 = a.z > b.z ? (a.z > c.z ? a.z : c.z) :
								(b.z > c.z ? b.z : c.z);

							if (!range.flowTris)
							{
								range.flowY = range.flowYMax = (int)a.y;
								range.flowMinX = x0;
								range.flowMaxX = x1;
								range.flowMinZ = z0;
								range.flowMaxZ = z1;
							}
							else
							{
								range.flowY = (int)a.y < range.flowY ?
									(int)a.y : range.flowY;
								range.flowYMax = (int)a.y > range.flowYMax ?
									(int)a.y : range.flowYMax;
								range.flowMinX = x0 < range.flowMinX ? x0 :
									range.flowMinX;
								range.flowMaxX = x1 > range.flowMaxX ? x1 :
									range.flowMaxX;
								range.flowMinZ = z0 < range.flowMinZ ? z0 :
									range.flowMinZ;
								range.flowMaxZ = z1 > range.flowMaxZ ? z1 :
									range.flowMaxZ;
							}

							range.flowTris++;
							NoteBoxCells(range.flowCells, x0, x1, z0, z1);
						}
					}
				}

				/* The art a static's piece may take: the overhang of no
				 * intrusion piece and the stand ranges, the parts of a group
				 * HDRenderStaticArt asks. The range is the one being filled,
				 * pushed at this index. A piece made only for the air counts
				 * as no piece here: without the air the open half has its
				 * art, and a static that takes it without the air - a palm
				 * of a courtyard - would go classic with it, its classic
				 * mesh drawn over the art. */
				if (g_place_count && (tris[i].stand > 0 ||
					(tris[i].overhang &&
					(!tris[i].intruding || tris[i].openAir))))
					NotePlaceTri(trg, (int)g, pass, (int)group.pass[pass].size(),
						tris[i].stand, (unsigned int)sorted.size(), first,
						o.placeTris);

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
						 * is. Noted here because this is the one place a
						 * vertex and its group are both in hand, and counted
						 * once per vertex rather than once per triangle by
						 * the join, which walks these notes in group order
						 * with the seen flag. */
						if (v.sway && index < sway_seen.size())
							o.swayHits.push_back(index);

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
						else if ((!tris[i].intruding || tris[i].openAir) &&
							tris[i].overhang)
							group.cellsFree.push_back(CellKey(v.x, v.y, v.z));

						if (tris[i].destructId >= 0)
						{
							HDDestructible& piece =
								o.destruct[(size_t)tris[i].destructId];

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
					o.flipRanges++;

				if (range.bookTime != 0.0f)
					o.bookRanges++;

				if (range.blend != 0.0f && !range.clear)
					o.blendRanges++;

				if (range.clear)
				{
					o.clearRanges++;
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

				if ((int)(range.count / 3) > o.biggest)
					o.biggest = (int)(range.count / 3);
			}
		}

		for (int k = 0; k < 5; k++)
			o.glossPartsBy[k] += group_parts[k];

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
			o.flatPlanes += (int)group.flats.size();
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

		// And over the clear floors alone.
		if (!clearflats.empty())
		{
			std::sort(clearflats.begin(), clearflats.end());
			clearflats.erase(std::unique(clearflats.begin(), clearflats.end()),
				clearflats.end());

			for (size_t i = 0; i < clearflats.size(); i++)
			{
				if (group.clearFlats.empty() ||
					group.clearFlats.back().y != clearflats[i].first)
				{
					HDFlatPlane row;

					row.y = clearflats[i].first;
					group.clearFlats.push_back(row);
				}

				group.clearFlats.back().cells.push_back(clearflats[i].second);
			}

			group.clearFlats.shrink_to_fit();
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
	};

	/* The book runs and the plain pages' sheen, made before the groups run:
	 * both are level-wide tables the fill asks (MaterialBookSlotsMade,
	 * PageGlossKnown) and would otherwise make on first use. The runs are
	 * made in the order the fill asks for them - group, pass, part - so their
	 * numbers do not depend on the thread count. */
	for (size_t g = 0; g < trg.groups.size(); g++)
		for (int pass = 0; pass < 3; pass++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			{
				const int page0 = part.material == HD_TRG_MATERIAL_BOOK
					? RunFirstPage(trg, part.pass[pass]) : -1;
				const float step = MaterialBookTime(part);

				if (step != 0.0f && page0 >= 0 && HDPageCacheSlotOf(page0) >= 0)
					MaterialBookSlots(page0, (int)part.param[1]);
			}

	for (unsigned short page : trg.textures)
		PageGloss(HDPageCachePageOfSlot(HDPageCacheSlotOf((int)page)));

	PageGloss(HDPageCachePageOfSlot(-1));

	// The hulls, asked of every group before any is filled (MarkAirHulls).
	MarkAirHulls(trg);
	// And the pair faces a larger face of the holder covers.
	MarkCoveredFaces(trg);
	// And the copies among the carried art (MarkCarryCells).
	MarkCarryCells(trg);
	// The visibility path's tables, when the level asks for them (hd_vis.h).
	HDVisLevelBuild(trg);

	/* The biggest groups first, so that no thread is left alone with a big
	 * one at the end. Only the order the work is handed out in: every group
	 * writes its own slot, and the join below is in group order. */
	std::vector<int> fill_order(trg.groups.size());
	std::vector<unsigned int> fill_size(trg.groups.size(), 0);

	for (size_t g = 0; g < trg.groups.size(); g++)
	{
		fill_order[g] = (int)g;

		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			for (int pass = 0; pass < 3; pass++)
				fill_size[g] += part.pass[pass].count;
	}

	std::stable_sort(fill_order.begin(), fill_order.end(),
		[&](int a, int b)
		{
			return fill_size[(size_t)a] > fill_size[(size_t)b];
		});

	/* The groups in flight hold their triangle records at once, a group's
	 * about its triangle count times an HDTri and its sort list. They are
	 * gated by bytes: a group starts when the groups running and it stay
	 * under kFillBudget, or when nothing else runs - the page decode's batch
	 * budget (hd_pagecache.cpp), the transient a level load already allows
	 * itself in a 32-bit address space. Measured with RoomProbe on TITLE:
	 * without the gate the peak commit of sixteen threads stood 160 MiB above
	 * one thread's. The gate decides only when a group starts; a group's
	 * share is given back however its fill ends (HDByteGate::Share), so a
	 * throw reaches the caller and leaves no thread waiting for room. */
	const double kFillBudget = 96.0 * 1048576.0;
	HDByteGate fill_gate(kFillBudget);

	HDPoolRun((int)fill_order.size(), threads, [&](int k)
	{
		const size_t g = (size_t)fill_order[(size_t)k];
		const double need = (double)(fill_size[g] / 3) *
			(double)(sizeof(HDTri) + 2 * sizeof(unsigned int) + 1);
		const HDByteGate::Share share(fill_gate, need);

		fill_group(g);
	});

	/* The join, in group order: the index runs appended with every range's
	 * start and every static piece's index moved by the run's place, the
	 * destructibles and the stand covers appended with the ids and first
	 * flags that point into them moved the same way, the hits and counts
	 * added, and each wind-weighted vertex counted for the first group that
	 * names it, in group order whatever the thread count. */
	for (size_t g = 0; g < trg.groups.size(); g++)
	{
		FillOut& o = fill_out[g];
		HDGroup& group = g_groups[g];
		const unsigned int base = (unsigned int)sorted.size();
		const int destruct_base = (int)g_destruct.size();
		const size_t flag_base = g_stand_cover_faces.size();

		sorted.insert(sorted.end(), o.sorted.begin(), o.sorted.end());
		std::vector<unsigned int>().swap(o.sorted);

		for (int pass = 0; pass < 3; pass++)
			for (HDRange& range : group.pass[pass])
			{
				range.start += base;

				if (range.destructId >= 0)
					range.destructId += destruct_base;
			}

		g_destruct.insert(g_destruct.end(), o.destruct.begin(),
			o.destruct.end());
		g_book_parts.insert(g_book_parts.end(), o.bookParts.begin(),
			o.bookParts.end());

		for (StandCoverBuilt& made : o.standCovers)
		{
			made.firstFlag += flag_base;
			g_stand_covers.push_back(made);
		}

		g_stand_cover_faces.insert(g_stand_cover_faces.end(),
			o.standCoverFaces.begin(), o.standCoverFaces.end());
		g_stand_tris.insert(g_stand_tris.end(), o.standTris.begin(),
			o.standTris.end());
		g_stand_tri_holder.insert(g_stand_tri_holder.end(),
			o.standTriHolder.begin(), o.standTriHolder.end());

		for (PlaceTri& tri : o.placeTris)
		{
			tri.at += base;
			g_place_tris.push_back(tri);
		}

		for (unsigned int index : o.swayHits)
		{
			if (sway_seen[index])
				continue;

			const short sway = trg.vertices[index].sway;
			const int weight = sway < 0 ? -(int)sway : (int)sway;

			sway_seen[index] = 1;
			group.motion.swayVertices++;

			if (weight > group.motion.swayMax)
				group.motion.swayMax = weight;
		}

		for (int slot : o.clearSlots)
			if (!g_clear_seen[(size_t)slot])
			{
				g_clear_seen[(size_t)slot] = 1;
				g_clear_pages++;
			}

		for (int k : o.cutHits)
			if ((size_t)k < g_cut_hits.size())
				g_cut_hits[(size_t)k]++;

		for (int k : o.intrusionHits)
			if ((size_t)k < g_intrusion_hits.size())
				g_intrusion_hits[(size_t)k]++;

		for (int k : o.intrusionBand)
			if ((size_t)k < g_intrusion_band.size())
				g_intrusion_band[(size_t)k]++;

		for (int host : o.airPairs)
			g_air_pairs[std::make_pair((int)g, host)]++;

		for (int host : o.carryHosts)
			g_carry_pairs[std::make_pair((int)g, host)]++;

		for (int host : o.carryCopies)
			g_carry_copies[std::make_pair((int)g, host)]++;

		g_air_ticks += o.airTicks;
		g_cutout_parts += o.cutoutParts;
		g_addweight_parts += o.addweightParts;
		g_addswing_parts += o.addswingParts;
		g_addstill_parts += o.addstillParts;
		g_stacked_parts += o.stackedParts;
		g_stacked_tris += o.stackedTris;
		g_water_parts += o.waterParts;
		g_water_tris += o.waterTris;
		g_water_opaque += o.waterOpaque;
		g_water_additive += o.waterAdditive;
		g_water_still += o.waterStill;
		g_water_subrect += o.waterSubrect;
		g_flat_parts += o.flatParts;
		g_gloss_parts += o.glossParts;
		g_blend_parts += o.blendParts;
		g_flip_parts += o.flipParts;
		g_flip_dropped += o.flipDropped;
		g_scroll_parts += o.scrollParts;
		g_clear_parts += o.clearParts;
		g_flip_ranges += o.flipRanges;
		g_book_ranges += o.bookRanges;
		g_blend_ranges += o.blendRanges;
		g_clear_ranges += o.clearRanges;
		g_flat_planes += o.flatPlanes;
		g_stand_from_intrusions += o.standFromIntrusions;
		split_ranges += o.splitRanges;
		fallback_tris += o.fallbackTris;

		for (int k = 0; k < 5; k++)
			g_gloss_parts_by[k] += o.glossPartsBy[k];

		if (o.biggest > biggest)
			biggest = o.biggest;

		if (o.waterSpan < g_water_span)
			g_water_span = o.waterSpan;

		if (o.waterCap > g_water_cap)
			g_water_cap = o.waterCap;

		if (o.addweightMin < g_addweight_min)
			g_addweight_min = o.addweightMin;
	}

	std::vector<FillOut>().swap(fill_out);

	/* A pair whose art in its host is more than half copies of another
	 * group's faces carries nothing the frame lacks: the street art bundles
	 * of ANDREA2 hold the same faces as one another (room 10 in room 153,
	 * 1,635 of 1,866), and drawn again over them they fight for the depth.
	 * The brick tower, room 222 in room 232, is 20 of 286. 132 of the 583
	 * pairs of the fifteen levels go. */
	for (std::map<std::pair<int, int>, int>::iterator it =
		g_carry_pairs.begin(); it != g_carry_pairs.end(); )
		if (g_carry_copies[it->first] * 2 > it->second)
			it = g_carry_pairs.erase(it);
		else
			++it;

	std::vector<std::pair<long long, int> >().swap(g_carry_cells);

	lap(HD_BEGIN_FILL);

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
	FindMirrors(trg, threads);

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

	lap(HD_BEGIN_INDEX);

	if (stats)
	{
		for (int p = 0; p < HD_BEGIN_PHASES; p++)
			stats->phaseMs[p] = phase_ms[p];

		if (g_build_hash)
		{
			const double hash_started = now_ms();

			stats->hash = BuildHash(trg, vertex_bits, sorted, threads);
			stats->hashMs = now_ms() - hash_started;
		}

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

		stats->buildMs = now_ms() - build_started - stats->hashMs;
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
	g_flow_feet.clear();
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

	// A new frame's first bracket: the marble floors' list starts over.
	if (g_marble_spent)
	{
		g_marble_draws.clear();
		g_marble_spent = false;
	}

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
	g_marble_frame = frame;
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

namespace
{
	void water_bracket_end();
}

void HDRenderFrameEnd()
{
	if (!g_in_frame)
		return;

	g_in_frame = false;
	water_bracket_end();
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
		// c18 of the pixel file, the caster's place (HDRenderShadowPlace).
		float place[4] = {};
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

	/* One pair made: a render target texture of `format` `size` texels a
	 * side - R32F for her map, G32R32F for the second, whose green channel
	 * holds each caster's place (hd_shadow.hlsl) - and a depth surface of its
	 * own beside it, both D3DPOOL_DEFAULT. 0 when both were made, otherwise
	 * the word for what refused - the caller releases what was made and
	 * refuses with it. `depthName` comes back with the depth format's name.
	 *
	 * The format first, of the adapter the device runs on: `format` as a
	 * render target texture, then a depth format that goes with it. The
	 * adapter format is the display mode's, which is the back buffer's family
	 * on a windowed device. */
	const char* shadow_pair_make(int size, D3DFORMAT format,
		IDirect3DTexture9*& texture, IDirect3DSurface9*& colour,
		IDirect3DSurface9*& depthSurface, const char** depthName)
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
			mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, format)))
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
				made.DeviceType, mode.Format, format, depth)))
				refused = "match";
		}

		d3d->Release();

		if (refused)
			return refused;

		if (FAILED(g_device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET,
			format, D3DPOOL_DEFAULT, &texture, 0)))
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
	const char* refused = shadow_pair_make(HD_SHADOW_MAP, D3DFMT_R32F,
		g_shadow_texture, g_shadow_colour, g_shadow_depth, &depth);

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
	const char* refused = shadow_pair_make(HD_SHADOW_OTHERS_MAP, D3DFMT_G32R32F,
		g_others_texture, g_others_colour, g_others_depth, &depth);

	if (refused)
		return others_refuse(refused);

	g_others_info.format = "G32R32F";
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
	g_device->GetPixelShaderConstantF(18, s.place, 1);
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

	/* White is 1.0 in every channel of a float target: nothing cast, and a
	 * place no lookup reads, since only a casting texel's place counts. */
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1.0f, 0);
	return true;
}

void HDRenderShadowPlace(float depth)
{
	if (!g_device || !g_shadow_saved.open)
		return;

	const float c[4] = { depth, 0.0f, 0.0f, 0.0f };

	g_device->SetPixelShaderConstantF(18, c, 1);
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
	g_device->SetPixelShaderConstantF(18, s.place, 1);
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
		// Whether the blend is off for an opaque clear floor (see below).
		bool opaque_on = false;
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

			// The remaster's water art under a sheet's place (step 8 of
			// HDRenderSetWaterSheets) - false in every draw call without one.
			if (range.artHidden)
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
			 * always was. Nor while the frame's eye is under the water
			 * (g_under_on): the variant has no slots left for the extinction
			 * (hd_room.hlsl, "under the water"), and its faces kept their
			 * own colour beside the faded rest - the walls, crates and floor
			 * plates of JOBY2's flooded base against a flat water colour
			 * between them (RoomProbe -underwater, -noreflect and
			 * -glossfield at the same camera). RoomPS draws them then,
			 * without the sheen: the sheen is what the water costs.
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
				g_reflect != 0.0f && !blendOnly && !sky && !additive_pass &&
				!g_under_on;

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

			/* The ripple of this draw call. The three numbers are the
			 * water's under a material 16 part (water_art_ripples), and
			 * zero everywhere else -
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
			const bool stand_behind = range.stand &&
				group.standWith[(size_t)range.stand - 1] == HD_STAND_BEHIND;
			const bool piece_behind = range.intruding > 0 &&
				(size_t)range.intruding <= group.pairRanges.size() &&
				group.pairRanges[(size_t)range.intruding - 1].behind;
			const bool want_behind = window == HD_WINDOW_STAND ?
				g_stand_pick_behind
				: range.behind || stand_behind || piece_behind;

			if (want_behind != behind_on)
				behind_on = set_stand_matrix(wvp, want_behind);

			/* A clear floor of the mirror column is drawn opaque while the
			 * marble floors are on (see "the marble floors"): the floor
			 * the planar mirror lies on, the art's copy under it hidden. */
			const bool opaque = clearOnly && range.marbleOpaque &&
				g_marble_on;

			if (opaque != opaque_on)
			{
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE,
					opaque ? FALSE : TRUE);
				opaque_on = opaque;
			}

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

	void upload_water_room(int group, const float* worldview);
	void draw_water_sheet(int group);

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

		// c16 - c23, the water's - see upload_water_room.
		upload_water_room(group, worldview);
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
	 * lambda, height, angle  the eight wave rows: wavelength and height in
	 *            units, direction in radians off the body's own direction,
	 *            spread so that no two rows run in step and the sum does
	 *            not read as a grid. The first four are the rows RoomVS
	 *            moves the remaster's marked water art with; the sheet takes
	 *            all eight (water_rows_now), turned into the fan (`fan`)
	 * groups     how deep the rows' slow envelope breathes, 0..1: the share
	 *            of a row's height that comes and goes over its period
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
	 *            takes its long axis
	 * eyeGain    how much steeper the sheet's slopes are drawn where the
	 *            view runs flat over the water (hd_room.hlsl, "the body at
	 *            eye level"), 1 changing nothing
	 * fan        the width of the sheet's fan of directions, radians (see
	 *            "the crests" below); `angle` stays the straight table that
	 *            RoomVS moves the remaster's marked art with and that a
	 *            row's `crest 0` gives the sheet back
	 * crestLen   a crest cell's length, wavelengths of its own row (a
	 *            piece fills half to all of it, see "the crests")
	 * crestDepth the pieces' share of a row, 0..1: 0 the straight rows */
	struct HDWaterClass
	{
		const char* name;
		float lambda[8];
		float height[8];
		float angle[8];
		float groups;
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
		float eyeGain;
		float fan;
		float crestLen;
		float crestDepth;
	};

	/* A basin is a calm fountain: short, low, slow ripples in clear water
	 * and no foam. A channel is a sewer: livelier, carried by its current,
	 * murky, foam along its banks. A sea is open water: long, high waves,
	 * foam at every edge and on the crests. The foam blotches of a channel
	 * and a sea are 1.5 times finer than the basin's scale.
	 *
	 * A channel's highlight is 0.4. Its water hides its floor within a
	 * square and a half, so its waves stand at their whole height over a
	 * dark body, and every slope that meets the highlight's light lights.
	 * In ANDREA2's canal seen from above (RoomProbe) the water that shows
	 * no floor has a median luma of 33 and its brightest 2 % 66 without a
	 * highlight, 45 and 69 with 0.4, 50 and 103 with 0.8 - a yellow sheen
	 * over the canal. A free choice, set by eye on those pictures.
	 *
	 * The rows five to eight fill the spectrum between and below the first
	 * four: three wavelengths at the geometric mean of two neighbours and
	 * one at the shortest over 1.5 (moved by two units where it came near a
	 * whole fraction of another row's: 31, 43 and 100 against 63, 88 and
	 * 1500), each as high as 0.8 of its neighbours' mean
	 * steepness (height over wavelength) times its wavelength, so the
	 * heights fall with the wave number and the first four stay on top.
	 * Their directions keep at least 0.15 radians from every other row's.
	 * The envelope's depth - basin 0.30, channel 0.35, sea 0.50 - and the
	 * directions are free choices, set by eye on the round's RoomProbe
	 * pictures. So are the gains at eye level, basin 3, channel 2, sea 1:
	 * the calmer the class, the flatter it lies seen flat; the sea's rows
	 * are high enough already.
	 *
	 * A river is water a current pulls along: its rows are stretched along
	 * the flow, every one of them 1.11 to 1.25 radians off it, so that a
	 * crest runs along the current 2 to 3 times as far as across it (the
	 * tangent of the angle), its base is the body's own flow and its
	 * current the body's speed (water_look); no bank foam and no whitecaps
	 * - foam does not stay on a fast river; its sheen and the foam where it
	 * ends are RiverPS's. Its rows are carried as one: calm 0.2, a quarter
	 * of the channel's, so their own travel (calm sqrt(g k), 362 units a
	 * second across the flow for the 300-unit row at 0.8) stays under the
	 * current's - at 0.8 the relief walked sideways and boiled and did not
	 * slide downstream (RoomProbe, ANDY3's river from its bank, the motion
	 * of its pixels from 3 to 3.1 seconds). Its heights are a channel's
	 * steepness (height over wavelength 0.0086 at most against 0.0078) and
	 * its highlight 0.4, as the channel's: in a quarter to one and a
	 * half squares of water the bend of the floor is the slope times a
	 * quarter of the path,
	 * and 1.2 over 300 moved it by one or two units; at 0.8 the highlight of
	 * the steeper rows went white from above. The wavelengths are the
	 * channel's kind of spread, moved off whole ratios; the heights, the
	 * angles, the calm, the highlight and the 240 units a second of a row's
	 * `river` without a flow are free choices, set by eye on the round's
	 * RoomProbe strips. */
	const HDWaterClass kWaterClass[HD_WATER_CLASSES] =
	{
		{ "basin", { 260.0f, 150.0f, 85.0f, 47.0f, 197.0f, 113.0f, 63.0f,
			29.0f },
			{ 2.0f, 1.4f, 0.8f, 0.45f, 1.34f, 0.85f, 0.48f, 0.22f },
			{ 0.0f, 1.13f, -0.79f, 2.51f, 1.87f, -1.61f, 0.41f, -2.83f },
			0.30f,
			0.6f, 0.0f, 0.0f, 512.0f, 4000.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			1.0f, 90.0f, 0.8f, 55.0f, 40.0f, 0.3f, 3.0f,
			1.5f, 3.0f, 1.0f },
		{ "channel", { 420.0f, 230.0f, 120.0f, 64.0f, 311.0f, 166.0f, 88.0f,
			41.0f },
			{ 3.0f, 1.8f, 0.9f, 0.45f, 1.86f, 1.02f, 0.51f, 0.23f },
			{ 0.0f, 0.42f, -0.37f, 0.9f, -0.19f, 0.63f, -0.71f, 1.21f },
			0.35f,
			0.8f, 180.0f, 0.6f, 512.0f, 1500.0f, 0.7f, 300.0f, 1.5f, 0.0f,
			1.0f, 70.0f, 0.4f, 55.0f, 40.0f, 0.0f, 2.0f,
			1.2f, 3.0f, 1.0f },
		{ "sea", { 1500.0f, 700.0f, 330.0f, 150.0f, 1025.0f, 481.0f, 222.0f,
			98.0f },
			{ 38.0f, 16.0f, 7.0f, 2.8f, 19.75f, 8.47f, 3.55f, 1.47f },
			{ 0.0f, 0.61f, -0.52f, 1.37f, -0.29f, 0.97f, -1.13f, 0.27f },
			0.50f,
			1.0f, 0.0f, 1.0f, 1024.0f, 900.0f, 0.85f, 300.0f, 1.5f, 0.6f,
			0.5f, 90.0f, 0.5f, 22.0f, 200.0f, 0.6f, 1.0f,
			1.2f, 3.0f, 1.0f },
		{ "river", { 300.0f, 187.0f, 117.0f, 73.0f, 237.0f, 149.0f, 93.0f,
			47.0f },
			{ 2.4f, 1.6f, 1.0f, 0.56f, 2.0f, 1.3f, 0.76f, 0.36f },
			{ 1.18f, -1.15f, 1.23f, -1.21f, 1.12f, -1.25f, 1.2f, -1.13f },
			0.35f,
			0.2f, 240.0f, 0.3f, 512.0f, 1500.0f, 0.0f, 300.0f, 1.5f, 0.0f,
			1.0f, 70.0f, 0.4f, 55.0f, 40.0f, 0.0f, 2.0f,
			1.2f, 3.0f, 1.0f },
	};

	/* The rows' slow envelope (water_rows_now): a row's height swells and
	 * fades over its own period, 8 to 15 seconds and different for every
	 * row, so the eight never return to one pattern together; the periods
	 * and the start of each envelope (row i at i times the golden angle)
	 * are free choices, set by eye on the round's RoomProbe strips. The
	 * rows five to eight start their wave a multiple of the golden angle
	 * further on, the first four where they always have. */
	const float kWaterGroupPeriod[8] =
		{ 8.0f, 13.7f, 10.3f, 15.0f, 9.1f, 11.9f, 14.3f, 12.6f };
	const float kWaterGolden = 2.3999632f;	// radians, 2 pi (1 - 1 / phi)

	/* The crests. A sum of a few straight waves crossing at fixed angles
	 * draws a lattice: its crests run straight over many wavelengths, and
	 * where two long rows a few tens of degrees apart cross, the highlight
	 * and the reflection's lobe light the cells of their beat - the
	 * difference of their wave vectors, 337 to 851 units for the
	 * channel's 420, 311 and 230 - again and again, quadrilaterals a
	 * tenth of the picture wide from a swimmer's view. A pattern made of
	 * periodic curves only moves that lattice about. So the sheet's rows
	 * are cut into pieces placed by a hash and spread:
	 *
	 * the pieces - every row's crests are cut into cells, kCrestAcross
	 * crests a cell across and crestLen of the row's wavelengths (at most
	 * kCrestReach units) along, staggered by half a cell from one cell to
	 * the next across; the hash of a cell's two numbers gives its piece -
	 * a short train of crests - its height, length, place, lean and shift
	 * (hd_room.hlsl, CellHash and CrestRows), so no two pieces of a row
	 * stand at one distance and nothing repeats short of the hash's 4096
	 * cells. A crest keeps its cell while it travels: the processor hands
	 * each row the whole cells its phase has run (kCrestOffset apart from
	 * row to row, so no two rows count alike). C, the pieces' share, is
	 * the class's crestDepth; the pieces' heights times kCrestGain;
	 *
	 * the squeeze - the place is pushed along a value noise of cells 1.5
	 * of the longest wavelength wide by up to kBendPush of that wavelength
	 * (hd_room.hlsl, WaveBend), so crests crowd and part and never the
	 * same way twice;
	 *
	 * the fan - the rows ranked by wavelength, longest first, lie at
	 * kWaterFan times the class's fan width round the body's own
	 * direction: the longest on it, the shortest widest, the negative side
	 * half a step further out than the positive, so that no two rows are
	 * mirror images of each other about the body's direction (their
	 * angles' sum stays an eighth of the width off 0) and no two run
	 * within a quarter of the width of each other.
	 *
	 * A row's `crest s` (0 to 1) takes the body from the straight rows
	 * (the `angle` table, s 0) to the fan (s 1) and scales the pieces'
	 * share and the squeeze by s; at 0 the rows are straight and unbent.
	 *
	 * The widths, the cell's length, the share, the gain and the push are
	 * free choices, set by eye on the round's RoomProbe pictures against
	 * the lattice measures of the rounds (the share of the surface light's
	 * power in its six strongest frequencies, and in two directions).
	 * Widths: basin 1.5 - a crest's line is a direction modulo pi, so the
	 * basin's rows then lie round 171 of the 180 degrees, ripples from all
	 * round a fountain -, channel, sea and river 1.2: crests mostly across
	 * a canal, the wind's waves and a river's pieces across and askew to
	 * the current, each spread over about 70 degrees either way. 1.2 and
	 * 1.5 keep the rule above modulo pi as well - no two crest lines within
	 * 0.30 (0.33), no mirror pair within 0.15 (0.19); 0.64 for the sea drew
	 * its pieces as parallel streaks, 1.6 and 2.4 broke the rule modulo
	 * pi. A cell of 3 wavelengths holds a piece of 1.5 to 3; at 1.6 with
	 * pieces of 0.6 to 1.6 and one crest a cell across the pieces were
	 * round bumps, the sheet pitted. */
	const float kWaterFan[8] =
		{ 0.0f, 0.25f, -0.375f, 0.5f, -0.625f, 0.75f, -0.875f, 1.0f };
	/* The gain of the pieces' heights: a piece fills part of its cell -
	 * along half to all of it, across its middle crest fully and the two
	 * beside it by a quarter - at 0.4 to 1 of the gain, so at 1 the rows
	 * would keep a twentieth of their mean square. 4.49 keeps it (0.52 the
	 * heights' mean square, 0.258 the window's along over the lengths,
	 * 0.371 across: 1 / sqrt(0.52 x 0.258 x 0.371)); 6, a third more, a
	 * free choice set by eye on the round's RoomProbe pictures - at 4.49
	 * the pieces read faint against the floor seen through the water. */
	const float kCrestGain = 6.0f;
	/* The same gain for WaterVS's swell, which lifts and sinks the sheet
	 * itself: at 1 a piece rises and sinks no further than its straight row
	 * does, so the sheet's edge keeps to the shallows as the clip and the
	 * lift were set for. At 6 a long row's trough sank the pond's sheet
	 * under the ground beside the island: of twelve RoomProbe views of
	 * ANDY3's pond at 1, 3 and 5 seconds, the pixels where the sheet lies
	 * under the ground, 37,416 in one picture at 6, 9,237 at most at 1. */
	const float kCrestSwellGain = 1.0f;
	/* How far the squeeze pushes at most, of the longest wavelength - a
	 * free choice, set by eye on the round's RoomProbe pictures. */
	const float kBendPush = 0.3f;
	// Row i's crests are counted from i times this; odd, a free choice.
	const double kCrestOffset = 977.0;
	// The crests a cell holds across its row (hd_room.hlsl, CrestRows).
	const double kCrestAcross = 3.0;
	/* The longest a cell gets, units: a sea's longest rows (1,500 and
	 * 1,025 units) at crestLen wavelengths would draw pieces longer than
	 * most of its bodies are wide - ANDY3's pond is about six squares
	 * across -, straight bands over the whole body again. A square and a
	 * half: a free choice, set by eye on the round's RoomProbe pictures of
	 * the pond. */
	const float kCrestReach = 1536.0f;

	/* The sections' colours, RGB 0..255: base the shallow water, deep the
	 * water where no floor shows through, high the highlight, foam the foam.
	 * Rome warm green and yellow, Russia cold blue and green, Ireland a
	 * neutral dark green and white, VCI a dark blue. All four are lit by the
	 * body's light (water_look), so a dark sewer has dark water.
	 *
	 * night: 1 where the section's levels are night levels, indoors and out
	 * - the Ireland levels ANDY1 to ANDY3, a project decision on how those
	 * levels look in game. Their water takes the moon's light (water_lit,
	 * "the night"). */
	struct HDWaterPalette
	{
		const char* name;
		unsigned char base[3], deep[3], high[3], foam[3];
		int night;
	};

	const HDWaterPalette kWaterPalette[HD_WATER_SECTIONS] =
	{
		{ "Rome", { 150, 170, 90 }, { 62, 98, 50 }, { 255, 232, 170 },
			{ 238, 232, 200 }, 0 },
		{ "Russia", { 96, 150, 160 }, { 28, 62, 80 }, { 205, 228, 255 },
			{ 240, 248, 255 }, 0 },
		{ "Ireland", { 100, 125, 98 }, { 34, 60, 44 }, { 235, 240, 230 },
			{ 248, 248, 242 }, 1 },
		{ "VCI", { 64, 92, 150 }, { 14, 28, 70 }, { 190, 210, 255 },
			{ 215, 225, 245 }, 0 },
	};

	/* The night. A night level's water is lit by the moon, or by a lamp the
	 * eye sees as the moon's colour at night: the light a body's colours
	 * take is the luma of its rooms' light (0.299, 0.587, 0.114) times
	 * kWaterMoon, a cool tint of luma 1 (0.269 + 0.593 + 0.139), so a body
	 * keeps its brightness and loses the gold of a torch-lit room (ANDY2's
	 * channels stand at 0.95,0.45,0.30 and 1.06,0.48,0.30, the WATERLOG
	 * light=). Its highlight and its foam take the same tint, and under the
	 * sky the highlight is the moon's: kNightHigh of the class's sun.
	 *
	 * The caustic. RoomPS lays it on the floor and the walls under the water
	 * as the floor's own light times the pattern, so its bright lines take
	 * the floor's colour - on ANDY3's red-orange stream beds a gold lattice
	 * (RoomProbe, the caustic alone taken off: the lattice goes, the beds'
	 * hue stays at 15 to 17 degrees). A moon colour there would cost slots
	 * RoomPS does not have, and the moon casts no caustic the eye resolves:
	 * at night RoomPS's caustic is off (upload_water_room). A river keeps
	 * the one it carries downstream (RiverPS), laid at kNightCaustic of the
	 * day's strength as the floor's luma times kWaterMoon.
	 *
	 * The tint, kNightHigh, the foam's kNightFoam and kNightCaustic are free
	 * choices, set by eye on the round's RoomProbe pictures of ANDY1 to
	 * ANDY3. The sky rows (kWaterSky) are the domes as measured and stay. */
	const float kWaterMoon[3] = { 0.90f, 1.01f, 1.22f };
	const float kNightHigh = 0.4f;
	const float kNightFoam = 0.6f;
	const float kNightCaustic = 0.5f;

	/* What an open-air body reflects: the sky dome's colour at the horizon
	 * and overhead, out of RoomProbe -sky -skyonly -black pictures at 960
	 * by 540, one method for every row: the camera at 1024 0 1024 in JOBY2's
	 * room 42, ANDY1's 132 and ANDY3's 17; the horizon the mean of the rows
	 * 6 to 40 pixels above the picture's middle toward four directions (yaw
	 * 0, 90, 180 and 270, pitch 0), overhead the mean of the dome straight
	 * up (pitch -85) where it covers the view - all of it in JOBY2; in ANDY1
	 * and ANDY3, whose dome is one and the same dusk, its clouds, 41 % of
	 * the view. Black, where no dome is drawn, is left out. Every level with
	 * an open-air body has a row; one without reflects its body's own light,
	 * like an indoor body. */
	struct HDWaterSky
	{
		const char* stem;
		unsigned char horizon[3], zenith[3];
	};

	const HDWaterSky kWaterSky[] =
	{
		{ "JOBY2", { 152, 154, 171 }, { 96, 93, 116 } },
		{ "ANDY1", { 113, 78, 67 }, { 11, 19, 32 } },
		{ "ANDY3", { 113, 78, 67 }, { 11, 19, 32 } },
	};

	/* The compiled override rows: level and room, then the class (-1 keeps
	 * the class, HD_WATER_NONE draws no sheet), the tint of the shallow and
	 * deep colour, the scale of the wave heights and of the foam. A row
	 * tunes every body its room has water in. The rows below pin the
	 * classes the rule gives today, so that a later change of the rule
	 * cannot move these places.
	 *
	 * `ripple`, where its size is above 0, is a row's ripple source as
	 * hd_water.txt says it ('ripple x z size period'), with the sheets
	 * alone; a row of the file with a ripple for the same room takes its
	 * place. ANDREA2 room 194's fountain basin, over water room 120 (six
	 * squares, x 54,272 to 57,344 and z 31,744 to 33,792), has its spring
	 * in the springs' table (hd_spring.h, kHDSprings), which rings it.
	 *
	 * JOBY2's three seas - the open sea over rooms 42 and 76, the one over
	 * 143 and its flip twin 45, and the rain basin over room 7 (room 13
	 * under it, water in the level as the game loads it) - take half the
	 * foam, bank foam and whitecaps alike: chosen on RoomProbe pictures of
	 * the rain basin and the open sea with foam 1, 0.5, 0.25 and 0. Room
	 * 7's row keeps the class the rule gives. */
	struct HDWaterOverride
	{
		const char* stem;
		int room;
		int cls;
		float tint[3];
		float waves;
		float foam;
		float ripple[4];	// world x, z, size, period; size 0 for none
	};

	const HDWaterOverride kWaterOverride[] =
	{
		{ "ANDREA2", 144, HD_WATER_BASIN, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "ANDREA2", 194, HD_WATER_BASIN, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "ANDREA2", 60, HD_WATER_CHANNEL, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f },
		{ "JOBY2", 42, HD_WATER_SEA, { 1.0f, 1.0f, 1.0f }, 1.0f, 0.5f },
		{ "JOBY2", 143, HD_WATER_SEA, { 1.0f, 1.0f, 1.0f }, 1.0f, 0.5f },
		{ "JOBY2", 7, -1, { 1.0f, 1.0f, 1.0f }, 1.0f, 0.5f },
	};

	/* The compiled river rows: level and room, the way the water runs along
	 * world x and z and its speed in units a second - a row of class river
	 * with a flow, as hd_water.txt would say it ('ANDY3 66 river flow 0 -1
	 * 400'), applied after the override rows and before the file, whose
	 * rows still win. The room is the water room under the stream: a
	 * shallow stream has no sink, and its dry room holds other water too.
	 * ANDY3's three streams beside its pond, one square wide and 256 units
	 * deep (floors 11,264 under a surface of 11,008, the randomizer's
	 * Tr5Level on the classic level), are bodies of their own, apart from
	 * the pond: over water room 66 three squares of room 34 and one of 38
	 * along z, over 70 three of room 34 along x, over 67 one of room 34
	 * with room 96 beyond it.
	 *
	 * Their ways are the arrows of the reference pictures supplied for this
	 * project, placed in the level by the editor camera each picture was
	 * taken with: where a picture gives the camera's position, yaw and
	 * pitch, RoomProbe draws the same view from them (the editor's yaw is
	 * RoomProbe's, 0 along +z and 90 along +x; its pitch counts the other
	 * way), and where it does not, the view is found among RoomProbe
	 * cameras round the stream by the open sky and the trigger squares the
	 * editor marks. 66 runs -z, away from room 38's square; 70 runs -x, into
	 * the pond's water room 73 it joins at its west end; 67 runs +x, into
	 * room 96. The floors under the three are flat (every corner at its
	 * square's height), so they give no way of their own; the water rooms
	 * agree with the arrows: room 59 joins the river to 66's square of
	 * room 38 under the bank, 66 and 67 meet under the land at z 28,672 to
	 * 29,696, and room 96's floor falls a click from x 48,128 to 49,152 -
	 * one way down from the river through 66 and 67 to the east. 400 units
	 * a second: a wading stream, slower than the sink rivers' 640 and 1,063
	 * - a free choice, set by eye on the round's RoomProbe strips.
	 *
	 * `cls` is the class a row gives its body: the three streams are
	 * rivers; -1 keeps the body's class and gives it the current alone.
	 * ANDREA2's sewer canal (rooms 14, 19, 20, 60 and 171, a channel of 44
	 * squares) runs +z: a channel's rows run along its longer side
	 * (water_look; the canal is 15 squares along z, 7 along x), toward +z
	 * (WaveSum8's phase falls with the clock, so a crest moves along its
	 * row's way), and its bank foam drifts the same way. At the +z end the
	 * water goes on under the end wall (water room 133, its ceiling at the
	 * surface - SectorCheck on the classic ANDREA2.TRC): the drain. A
	 * channel's look reads no flow (water_look), so the row moves its
	 * floats alone (hd_life.cpp) and names the current in the WATERLOG
	 * line. 90 units a second, about 0.2 m/s at 2.3 mm a unit, a slow sewer:
	 * half the 180 its rows run at, and half again the floats' own wander
	 * of 60, so they are seen to drift with the water - a free choice with
	 * those reasons, set on RoomProbe series. */
	struct HDWaterFlowRow
	{
		const char* stem;
		int room;
		float dx, dz, speed;
		int cls;
	};

	const HDWaterFlowRow kWaterFlow[] =
	{
		{ "ANDY3", 66, 0.0f, -1.0f, 400.0f, HD_WATER_RIVER },
		{ "ANDY3", 70, -1.0f, 0.0f, 400.0f, HD_WATER_RIVER },
		{ "ANDY3", 67, 1.0f, 0.0f, 400.0f, HD_WATER_RIVER },
		{ "ANDREA2", 60, 0.0f, 1.0f, 90.0f, -1 },
	};

	/* The clip (WaterPS, "the clip"): where the frame's depth lies less
	 * than this many units under a sheet's surface at rest, the pixel is
	 * left to the frame - a deck, a rim, a floe's top the remaster laid at
	 * the water's plane. 8: the depth's own error is far under it - on
	 * JOBY2 room 66's flat pool floor at 2560 x 1440, about 2,600 units
	 * away, `rest` is one value to a sixteenth of a unit over the whole
	 * floor (RoomProbe, `rest` as grey), and 24 bits over the near and far
	 * planes of 20 and 20,480 give about 1.2 units at the far plane - and 8
	 * takes the remaster's own planes, which stand two to nine units off
	 * the classic height (ANDY3's water 2 and 4, ANDREA2 231's 7 to 9). A
	 * bank row's or a file row's `clip` sets it per body (kWaterBank). */
	const int kWaterClip = 8;

	/* The compiled bank rows: level and room, how far the sheets of every
	 * body over that room stand over the water's top (the lift, units),
	 * their clip (units, see kWaterClip) and how much of the class's swell
	 * their vertices take (1 the whole, 0 none) - a row as hd_water.txt
	 * would say it ('ANDY3 73 - lift 0 clip 0 swell 1') that sets those
	 * three alone, applied after the river rows and before the file, whose
	 * rows still win.
	 *
	 * ANDY3's pond. The remaster's art lays its bank as a ramp of its
	 * ground that leaves the water's top inside the water squares and
	 * climbs through their edge (the art's ground over the pond's 224 shore
	 * points against the classic floor there: bank points at the edge 15 to
	 * 443 units over the water, median 145), and it bakes the ground under
	 * the water dark: the baked light of the art's top triangle, sampled
	 * every 32 units over the pond, has a luma of 67 at the median under
	 * the water's top, 130 over it inside the water squares and 96 on the
	 * land within a square of them. The sheet stands at the water's top,
	 * lift 0, as the reference pictures supplied for this project show the
	 * water there.
	 *
	 * Its clip is 0. With 8 the sheet was cut off the ground for its last 8
	 * units under the water, and along the whole shore that dark ground lay
	 * bare between the land and the water (RoomProbe, the depth under the
	 * surface as grey: in a view over the pebble beach east of the lower
	 * lobe, 308 units up, 18,956 of 960 x 540 pixels). At 0 the sheet ends
	 * where the depth test ends it, on the line where the ground comes out
	 * of the water. The clip has nothing to spare there: no flat triangle
	 * of a square's own group lies within one unit over to nine under the
	 * surface over any of the pond's 24 squares (counted on the .TRG), and
	 * no other body of the fifteen levels has such a ramp: their edges are
	 * walls a click or more high or flat at the water.
	 *
	 * Its swell is 1, the class's whole, a free choice set by eye in the
	 * game against the sheet at rest (`swell 0`): with the clip at 0 both
	 * edges hold, and the waves taking a little from the edge now and then
	 * look the better water. What they take is the ground's own line
	 * showing for a moment: the class's swell tapers to nothing only at
	 * the classic shore, while the ground meets the water at a median 256
	 * units inside it and, round the island in the pond, up to 1,539 (the
	 * art's 0-line sampled every 32 units, 1,143 points), so a trough
	 * sinks the sheet under ground lying just under the water - in twelve
	 * RoomProbe views of the pond at 1, 3 and 5 seconds up to 17,165
	 * pixels of that ground with no water drawn over them, none with the
	 * sheet at rest (the depth views: the sheet's swell and clip against
	 * the ground at rest). A row's `swell 0` lays the sheet at rest, the
	 * waves then playing in its pixels alone.
	 *
	 * The row names water room 73, the room under 20 of the pond's 24
	 * squares: room 34 holds the three streams beside the pond too, which
	 * keep the clip and the swell of every body. */
	struct HDWaterBankRow
	{
		const char* stem;
		int room;
		int lift;
		int clip;
		float swell;
	};

	const HDWaterBankRow kWaterBank[] =
	{
		{ "ANDY3", 73, 0, 0, 1.0f },
	};

	/* The skirt of a lifted sheet (HDRenderSetWaterSheets, "the skirt"):
	 * how many land squares out from the body's squares its level surface
	 * may run on over the land under it, and how far over the lift a land
	 * square's lowest classic corner may stand and still carry it. 3: the
	 * land under 96 beside ANDY3's pond reaches its last square, the hollow
	 * east of the upper ring, three squares out (the classic corners of the
	 * land squares round the pond, walked out square by square); at 2 the
	 * art's ground stood under the plane at 11 of 544 points along the
	 * drawn surface's outer edge, at 3 at the 5 where a wall stands beside
	 * the water - 16 sample points to a side, the art's ground as the
	 * highest flat triangle over the point. 16 units: more than the 5 units
	 * the art's ground stands under the classic land at nine in ten of the
	 * pond's 152 low edge points, so a square whose corner stands just over
	 * the surface but whose art dips under it still carries the water; every
	 * land square round the pond has its corners at the water's top or a
	 * click and more over it, so 16 decides nothing there. */
	const int kWaterSkirtReach = 3;
	const int kWaterSkirtMargin = 16;

	// Units a second squared: 9.81 m/s^2 at 2.3 mm a unit, Lara's 768
	// units standing for 1.77 m.
	const float kWaterG = 4300.0f;

	// Units: how high the thickest foam stands for its light (hd_room.hlsl,
	// "the foam's body") - a free choice, set by eye on the round's
	// RoomProbe pictures of 8, 24 and 48: 8 hardly shows, 48 turns the
	// blotches' edges into a grain.
	const float kWaterFoamRelief = 24.0f;

	// The share of a channel's current the bank foam's patches drift at:
	// the water by a bank runs slower than in the middle of the channel. A
	// free choice; a basin and a sea have no current and no drift.
	const float kWaterPatchDrift = 0.3f;

	/* Where the waterline gives way to Lara (hd_room.hlsl, gWaterLara):
	 * none within kWaterLaraRadius of her place, back in full kWaterLaraEdge
	 * further out. She is 768 units tall and lies about 700 long when she
	 * swims, round her place; 320 and 64 are free choices that cover her. */
	const float kWaterLaraRadius = 320.0f;
	const float kWaterLaraEdge = 64.0f;

	/* The rivers. A sink of strength 1 pulls Lara at 120 units a second:
	 * LaraWaterCurrent (game\laraswim.cpp) steers her velocity, in 1/256
	 * units a tick, toward strength * 4096 / 4 along the way to the sink
	 * and moves her by a 256th of it, 4 units a tick for strength 1, at the
	 * game's 30 ticks a second. ANDY3's two rivers come to 1080 and 480
	 * (strengths 10, 8 and 4; RoomProbe -hdwater, the WATERLOG flow=).
	 *
	 * What the water shows of that speed is held to kRiverLookCap: rows of
	 * a few hundred units carried at a square a second slide past as a
	 * smear; the cap is a free choice, set by eye on the round's RoomProbe
	 * strips. The sheen on the skin (hd_room.hlsl, RiverPS) adds at most
	 * kRiverSheen of the highlight's colour on its lighter lines and takes
	 * 0.6 of that share off on its darker ones - lighter and darker than
	 * the water, never white -; the relief's light adds kRiverRelief times
	 * the rows' slope toward the class light of the same colour, and takes
	 * 0.6 of it off away from it - the waves' own streaks, seen at every
	 * angle because neither goes through the reflection; and the foam
	 * where a river ends against a wall or a drop reaches kRiverEdge
	 * upstream from that end (0.75 of a square, inside the half to one
	 * square a weir's foam takes on the reference pictures supplied for
	 * this project). Free choices, set by eye on the round's RoomProbe
	 * pictures; the sheen's and the relief's on the river's pixels outside
	 * its end foam, whose brightest stays well under white: with
	 * kRiverRelief 7 and the moon's kNightHigh 0.4, ANDY3's river and
	 * streams from the bank, from above and at eye level reach a luma of
	 * 113 at most and 80 at the 99th percentile (RoomProbe at 960 by 540,
	 * the clock at 3 s). */
	const float kSinkUnitsPerSecond = 120.0f;
	/* Sinks lying in a body's water make it a river when their trigger
	 * squares number at least a quarter of its own (HDRenderSetWaterSheets,
	 * "a sink in the water"): ANDY3's river of rooms 38 and 41 stands at 1.0
	 * and its sea of rooms 10 to 17 at 0.05 - a free choice between the
	 * two, a count on our data. */
	const double kRiverCover = 0.25;
	const float kRiverLookCap = 720.0f;
	const float kRiverSheen = 0.35f;
	const float kRiverRelief = 7.0f;
	const float kRiverEdge = 768.0f;

	/* The creatures' mark: the stencil bit HDRenderWaterMarkBegin writes
	 * under every HD object and the sheets' second draw tests - the rooms'
	 * receive owns bit 1 and the models' bit 2 (the projected shadow's
	 * receive), so this is the next. */
	const DWORD kCreatureStencilBit = 4;

	// The stencil states the mark and the sheets' split set and put back.
	const D3DRENDERSTATETYPE kMarkStates[] =
	{
		D3DRS_STENCILENABLE, D3DRS_TWOSIDEDSTENCILMODE, D3DRS_STENCILFUNC,
		D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
		D3DRS_STENCILPASS, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL
	};

	const int kMarkStateCount =
		(int)(sizeof(kMarkStates) / sizeof(kMarkStates[0]));

	bool depth_has_stencil();	// with the projected shadow's receive

	/* How a room's light becomes the water's: times the gain, never under
	 * the floor and never over the cap. The mean of colour1 + colour2 over a
	 * body's rooms, weighted by their squares, runs 0.09 to 0.55 a channel
	 * over the bodies of ANDREA2 and JOBY2 - the lowest ANDREA2's room 196,
	 * the highest JOBY2's 143 with its flipmap twin 45 (RoomProbe -hdwater 1
	 * -nodraw, WATERLOG raw=) - because it counts the dark pool floor and the
	 * shadowed corners beside the lit walls; the gain holds
	 * the water to what the lit walls round it show. Gain and floor are
	 * chosen on the pictures; the cap, which holds a brightly lit body's
	 * water to one and a half times the palette's colour, is a free choice,
	 * set by eye on the RoomProbe pictures of the water. */
	const float kWaterLightGain = 1.8f;
	const float kWaterLightFloor = 0.30f;
	const float kWaterLightCap = 1.5f;

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
		int sink;		// the sink its water names, or -1
		int pit;		// the water room under it
		/* 1 + the room under it whose flipmap alternate holds the water,
		 * 0 for water every flip state holds (see "the flipped water"). */
		int alt;
	};

	/* A land square a lifted sheet's level surface runs on over
	 * (HDRenderSetWaterSheets, "the skirt"): the square in its land room
	 * (room, sx, sz, localY; sheet 1, no depth, no sink) and that land's
	 * classic corners, world y, x0 z0, x1 z0, x0 z1, x1 z1. */
	struct SkirtSquare
	{
		WaterSquare s;
		int land[4];
	};

	struct WaterBody
	{
		HDRenderWaterBody info;
		int minX, maxX, minZ, maxZ;	// world, the squares' corners
		int alt;			// its squares' alt, all one
	};

	/* One draw: the sheet squares of one body in one group, and the world
	 * box of those squares with their surface - where a ring is near - and
	 * the classic origin of the group's room. */
	struct SheetRange
	{
		unsigned int start, tris, minVertex, vertices;
		int body;
		int minX, maxX, minZ, maxZ, surfaceY;
		int originX, originZ;
		int endNear;	// a vertex sees the body's end ahead (draw_river)
	};

	/* A body's constants: the sheet's eight rows, RoomVS's four, the swell
	 * and the pixel rows. */
	struct WaterLook
	{
		float rows[32];		// kx, kz, omega, height
		// RoomVS's four on the remaster's marked art: the straight table's.
		float artRows[16];
		float crest;		// the body's `crest`, 0..1
		float crestLen;		// the cells' length, wavelengths
		float crestDepth;	// the pieces' share times `crest`
		float phase0[8];	// each row's own start, radians
		float groups;		// the envelope's depth (HDWaterClass)
		float swell[4];		// gSwell
		float pixel[11 * 4];	// c96 - c106, c105 and c106 per draw
		float life[2];		// gWaterLife x, y: the stand-in's swing
		float foamLight;	// gWaterLife w: the foam's body
		float eyeGain;		// gWaterStage y plus one: the class's
		float drift[2];		// the patches' drift, units a second
		float river[16];	// RiverPS's c132, c133, c146, c147
		int isRiver;
		int hasEnd;		// a channel with a current: its end's foam
	};

	// An override row, the compiled table's or the file's.
	struct WaterRow
	{
		char stem[16];
		int room;
		int cls;		// -1 keeps the class
		float tint[3];
		float waves, foam;
		float bend;		// the refraction's scale
		int file;		// 1 for a row of hd_water.txt
		int ripple;		// 1 with a ripple source
		float rx, rz, rsize, rperiod;
		int art;		// 1 for `art keep`, 0 hides the art (default)
		int foot;		// 1 with a foot of its own, -1 for `foot off`
		float footAt[5];	// x1 z1 x2 z2 reach
		int old;		// the key `swirl`, read as `foot`
		int flow;		// 1 with `flow dx dz speed`
		float fx, fz, fspeed;
		int lift;		// 1 with `lift units`
		int liftUnits;
		int clip;		// 1 with `clip units`
		int clipUnits;
		int swell;		// 1 with `swell s`
		float swellScale;
		int crest;		// 1 with `crest s`
		float crestScale;
		// 1 for a compiled bank row: sets the lift and the clip, nothing else
		int bankOnly;
		/* 1 with `spring x z strength [seconds]`, its numbers as
		 * HDSpringParseKey reads them, and 1 when it is the row's one key
		 * and its class is -: the row tunes no body then. */
		int spring;
		float springKey[4];
		int springOnly;
	};

	std::vector<WaterSquare> g_wsquares;
	std::vector<WaterBody> g_wbodies;
	/* Per room, 1 while FlipMap has swapped its record with its pair's
	 * (HDRenderWaterFlipRoom); all 0 at the level's build. */
	std::vector<unsigned char> g_wflip;

	/* Whether a body draws now: a body of flipped water only while the room
	 * whose alternate holds it is swapped. */
	bool body_shown(int body)
	{
		if (body < 0 || body >= (int)g_wbodies.size() ||
			!g_wbodies[(size_t)body].alt)
			return true;

		const size_t room = (size_t)(g_wbodies[(size_t)body].alt - 1);

		return room < g_wflip.size() && g_wflip[room] != 0;
	}
	std::vector<WaterLook> g_wlooks;
	// The deep look of a level's water rooms, where the level takes one
	// (see "under the water", "the deep look").
	WaterLook g_wlook_deep;
	bool g_wdeep_on = false;
	// The highest top of the level's water rooms, world y, for the deep look.
	float g_wdeep_top = 0.0f;
	std::vector<std::vector<SheetRange> > g_sheet_ranges;	// per group
	std::vector<int> g_group_body;		// per group, its largest body
	std::vector<float> g_caustic_top;	// per group, room local y
	// Per group, 1 for a water room looked through a river's body.
	std::vector<unsigned char> g_caustic_river;
	/* Whether that caustic is drawn: only with the sheets. With
	 * tomb5.hd_water 0 the floors under a surface carry no caustic, as
	 * without the sheets. */
	bool g_caustic_on = false;
	std::unordered_set<unsigned long long> g_sheet_at;
	// The squares that draw a sheet, by world square (WaterKey, y 0): the
	// surface height - HDRenderWaterSheetUnder.
	std::unordered_multimap<unsigned long long, int> g_sheet_world;

	/* The rings (hd_room.hlsl, RingSlope).
	 *
	 * The game's own ripples (ripples[], game\tomb4fx.cpp, set up by
	 * SetupRipple and grown by UpdateSplashes, game\effect2.cpp) are where
	 * something meets the water: Lara wading or swimming, a drip, a bubble
	 * reaching the surface, a rat, a shell. HDRenderWaterRipples sees the
	 * live ones every frame and starts a ring where a slot is new - it was
	 * empty, its init counted up from 1 again or its size fell - aged by
	 * what its init has counted since: SetupRipple sets init to 1 and
	 * UpdateSplashes adds 4 a tick (8 with flags 2) while the ripple grows,
	 * so (init - 1) / 4 ticks of 1/30 s have passed, exact while it grows;
	 * its size then less that growth (4 a tick, 2 with flags 2) is the
	 * size it began with. From there the ring keeps its own clock, the
	 * frame's, and lives kRingLife whatever the slot does: the game frees a
	 * slot after about a second.
	 *
	 * One body makes one ring. A wading Lara sets up a ripple every four
	 * ticks (game\effects.cpp, WadeSplash: flags 18 while she moves, 16
	 * while she stands), a bubble train one after another; a ring for each
	 * would be a quick train of rings, all alike. So a new ripple of the
	 * same kind - a body's (flags 16) or a small one's (a bubble, a rat, a
	 * shell) - whose centre lies within kRingMerge of a live ring's and its
	 * height within kRingHeight goes into that ring: younger than
	 * kRingRestart, the ring takes it as its own and goes on; older, the
	 * ring starts again from the new ripple's place, size and age. A
	 * wading Lara has one ring that starts again every 0.6 s at most, and
	 * when she stands it runs out its life.
	 *
	 * A ring's front runs out from half the ripple's first size at
	 * kRingSpeed; its strength, a slope, is kRingSlope times its first size
	 * over 128 (a wading Lara's ripple is 112 to 127), times the square of
	 * what is left of its life - a slow fade that leaves the last second
	 * faint. A ripple that starts over 128 is a floating body's (the
	 * water's life rings a barrel once a bob at 240 to 255, hd_life_rules.h;
	 * nothing of the game starts one so big, and a ripple row of
	 * hd_water.txt over 128 is taken as one): its size counts on past 128,
	 * and the strength is kRingBody times more. A body's ring is one wave
	 * sent out a bob, where a wading Lara's is started again every 0.6 s
	 * and stays young; at night the water round it mirrors a dark sky
	 * nearly alike at every slope, and a ring shows only where its slope
	 * turns the mirror to the brighter horizon. kRingBody, 4, is a free
	 * choice, set by eye on the round's RoomProbe pictures of ANDY3's lake
	 * at night from the bank: at 4 a barrel's ring reads from a wading
	 * Lara's camera at 1.5 and 2 s, at 2 it does not; its slope at the
	 * ring's start stands far steeper than a real wave's, so near the
	 * barrel it reads as an outline (hd_splash_math.h, HDRingStrength). A
	 * sheet draw takes the kRingsPerDraw youngest whose centre lies within
	 * kRingNear of its squares and whose height within kRingHeight of its
	 * surface. Speed, life, slope, the merge
	 * distance, the restart and the two other distances are free choices,
	 * set by eye on the round's RoomProbe pictures: a wading Lara's ring
	 * slower than a quick splash, as strong at one second as a ring with a
	 * straight fade over two seconds and still there in the third, gone in
	 * three and a half. */
	const double kRingLife = 3.5;		// seconds
	const float kRingSpeed = 150.0f;	// units a second
	const float kRingSlope = 0.5f;
	const float kRingBody = 4.0f;
	const float kRingMerge = 192.0f;	// units
	const double kRingRestart = 0.6;	// seconds
	const int kRingNear = 512;		// units
	const int kRingHeight = 256;		// units
	const int kRingsPerDraw = 8;		// gWaterRings[8]
	const size_t kRingsKept = 64;

	struct WaterRing
	{
		float x, y, z;		// world; y the water's height there
		double born;		// on the frame clock, seconds
		float size;		// the ripple's size when it began, units
		int body;		// 1 for a body's ripple (flags 16), 0 small
	};

	// What a ripple slot held the frame before.
	struct RippleSeen
	{
		int alive, init, size;
	};

	// A ripple row of hd_water.txt: a ring every `period` seconds.
	struct WaterSource
	{
		float x, y, z, size, period, phase;
	};

	// RoomProbe -ripple: a ring of a given age in every picture.
	struct ProbeRing
	{
		float x, z, age, size;
	};

	/* The foot of a waterfall.
	 *
	 * Where a waterfall object's foot stands over a sheet (HDRenderWaterFalls)
	 * the water it drops into is not left as calm as the rest of the body:
	 * round the curtain's footprint lies one zone of the sheet's own foam,
	 * pushed away from the footprint and stretched as it goes (hd_room.hlsl,
	 * FootPS); rings start from points along it again and again as a ripple
	 * row's do, and drops are thrown up from those points on the logic tick
	 * (hd_level.cpp, HDWaterFallsTick, the engine's own TriggerSmallSplash).
	 *
	 * Which objects: WATERFALL1 to 3 and WATERFALLSS1 and 2, whose texture
	 * the game scrolls as falling water (game\objects.cpp,
	 * AnimateWaterfalls) - FISHTANK, which it scrolls as well, is a tank's
	 * glass. Its foot is where its classic mesh, placed as the game places
	 * it, ends: the vertices within HD_WATER_FOOT_BAND of the lowest. An
	 * object whose mesh is flat (no more than that band from its highest
	 * vertex to its lowest) is a scrolling face and not a fall -
	 * ANDREA1's WATERFALLSS1 is its fountain's upper basin. The foot stands
	 * over a sheet when one of its points lies over a square that draws a
	 * sheet with the surface no more than kFootAbove over the lowest vertex
	 * and no more than kFootBelow under it - the gap the log line gives. In
	 * the classic data every waterfall ends at its surface: the 31 objects of
	 * the fifteen levels whose foot stands over a sheet all show a gap of 0
	 * (RoomProbe -hdwater 1 -nodraw, the WATERLOG fall= lines), ANDREA2 room
	 * 231's curtain ending at 256 over a surface at 256. 256 and 1024 are
	 * free choices that take a fall ending a click under its surface and one
	 * hanging a square over it.
	 *
	 * The curtain. The objects over one body whose foot points come within
	 * kFootJoin of each other's are one curtain: two jets side by side land
	 * as one. Its points are taken in order along the principal axis of
	 * their places and gathered every kFootStep along it; the mean of every
	 * step that holds points is a corner of the curtain's footprint, a
	 * polyline, whose segments FootPS draws the zone round. A segment that
	 * spans a step without points is a gap where no water lands - ANDREA2
	 * room 231's two strands, about 1,250 units apart (the WATERLOG seg=
	 * lines) - and is open water: it keeps its place in the curtain's line
	 * with wet=0.00 and draws no foam, so each strand lands in a zone of its
	 * own, two ovals with water between them, as a sketch of the foot from
	 * above shows them (reference pictures supplied for this project, not
	 * in the repository). In a curtain with a gap every wet
	 * stretch closes in a head at both its ends: past its last point the
	 * zone reaches only kFootHead of its reach along the line, and beside a
	 * gap never more than kFootHeadGap of the gap's length over the reach -
	 * two heads then reach 0.8 of the gap, 0.92 at the outline's widest
	 * swing of 1.15 (FootPS's spread), so two stretches touch only where
	 * their strands do; a stretch of one corner is a segment of 2 units
	 * along the axis with those heads. A curtain without a gap keeps round
	 * ends, and so does a curtain of one step, a segment of 2 units along
	 * the axis. Its reach is kFootReachBase plus kFootReachGrow times its
	 * longest wet stretch - a strand's width -, held to kFootReachMin ..
	 * kFootReachMax: 0.6 of a square from a narrow jet, 0.9 from a strand a
	 * square wide, 1.2 from one of two. kFootJoin is 512 units, half a
	 * square; the step and the reach's numbers are free choices, set by eye
	 * on the round's RoomProbe pictures (at 0.6 of the whole length the
	 * reach covered the room); kFootHead's half a reach is a free choice by
	 * the sketch, kFootHeadGap a free choice that keeps an eighth of the gap
	 * open at the widest swing.
	 *
	 * The rings and the drops come from points along the footprint's wet
	 * stretches, its corners joined by segments that are no bridge: a
	 * stretch of length L gets L / kFootSourceStep points, rounded, at least
	 * one, at the middles of that many equal parts, each with half a part as
	 * the span its drops scatter over (a quarter of the reach for a stretch
	 * of one corner). A source's rings start every kFootRingMin seconds plus
	 * up to kFootRingMore more, by a hash of its place, so two neighbouring
	 * ones do not beat in step; kFootRingSize is the size a ripple of the
	 * game would carry - 64, half a wading Lara's, so the rings stay low.
	 * Free choices.
	 *
	 * A row's `foot x1 z1 x2 z2 reach` is a curtain of its own, that one
	 * segment with that reach; `foot off` takes the objects' curtains of the
	 * row's bodies away. `swirl x z r` reads as a foot of length 0 at x, z
	 * with reach r and `swirl off` as `foot off` (the level's log line
	 * counts them, old=). RoomProbe's -foot and -swirl are a row's. */
	const float kFootAbove = 256.0f;	// units
	const float kFootBelow = 1024.0f;
	const float kFootJoin = 512.0f;
	const float kFootStep = 512.0f;
	const float kFootHead = 0.5f;		// of the reach, past a stretch's end
	const float kFootHeadGap = 0.4f;	// of the gap, over the reach
	const float kFootReachBase = 614.0f;	// 0.6 of a square
	const float kFootReachGrow = 0.3f;
	const float kFootReachMin = 614.0f;
	const float kFootReachMax = 1229.0f;	// 1.2 squares
	const float kFootSourceStep = 512.0f;
	const float kFootSnap = 1024.0f;	// see HDRenderWaterFalls
	const float kFootRingMin = 0.4f;	// seconds
	const float kFootRingMore = 0.4f;
	const float kFootRingSize = 64.0f;
	const int kFootSegsPerDraw = 8;		// gFootSeg[8]

	// A source of rings and drops along a curtain's footprint.
	struct WaterFoot
	{
		float x, y, z;		// world; y the surface
		float dx, dz;		// along the footprint, a unit vector
		float half;		// half the stretch its drops scatter over
		float reach;		// its curtain's
		float period, phase;	// the rings
		int body;
		int curtain;
		int fromRow;		// 1 for a row's or RoomProbe's
	};

	// One segment of a curtain's footprint, which FootPS draws round.
	struct FootSeg
	{
		float x1, z1, x2, z2;	// world
		float y;		// the surface
		float reach;
		float wet;		// 1, or 0 over a gap where no water lands
		float headA, headB;	// FootPS's heads at its ends, 1 round
		int body;
		int curtain;
		int fromRow;
	};

	// One waterfall object as HDRenderWaterFalls found it, for its log line.
	struct FallSeen
	{
		int item, slot, room;
		float top, bottom;
		int points;
		int surface;		// the surface under the foot, or the lowest seen
		int over;		// 1 when the foot stands over a sheet
		int body;
		int sources;		// its curtain's sources
		int off;		// 1 when a row's `foot off` took them away
		int art;		// the art's foot points that took the place
		int curtain;		// its curtain, or -1
	};

	std::vector<WaterRing> g_rings;
	RippleSeen g_ripple_seen[HD_WATER_RIPPLE_SLOTS] = {};
	std::vector<WaterSource> g_wsources;
	// The level's `spring` rows of hd_water.txt (HDRenderWaterSpringRows).
	std::vector<HDRenderSpringRow> g_wspring_rows;
	std::vector<ProbeRing> g_ring_probe;
	std::vector<WaterFoot> g_wfeet;
	std::vector<FootSeg> g_wsegs;
	int g_curtains = 0;
	std::vector<FallSeen> g_wfalls;
	// Per room: its classic origin x and z, and 1 when it draws HD.
	std::vector<int> g_wroom_origin;
	std::vector<unsigned char> g_wroom_drawn;
	// Per room: the body an eye in that underwater room looks through, -1
	// for none (see "under the water").
	std::vector<int> g_wroom_body;
	// Per room: 1 where that body is the level's largest for want of a
	// sheet over the room; the dry room the sheet over it lies in, -1 for
	// none (see "under the water").
	std::vector<unsigned char> g_wroom_fallback;
	std::vector<int> g_wroom_air;
	// Lara's place for the waterline's gap round her (HDRenderWaterLara).
	bool g_lara_in_water = false;
	float g_lara_x = 0.0f, g_lara_z = 0.0f;
	// The level's sinks (HDRenderWaterSinks), for the rivers.
	std::vector<HDWaterSink> g_wsinks;

	/* The cards.
	 *
	 * A dive splash (SetupSplash) and a fish tank's spray
	 * (TriggerFishtankSpray) drawn as the water's own instead of the classic
	 * sprites: a splash is two rings on the sheet under it (gather_rings)
	 * and a crown and drops of cards, a spark of the tank's spray a card of
	 * its rising mist. The cards are quads facing the eye with a
	 * window of one of the remaster's pages - its spray, 3242 and 3243, and
	 * its drops, 3160 (DATA of the player's installation, read through the
	 * page cache) - cut round and soft in CardPS, tinted with the foam colour
	 * of the body they belong to, so a splash in a dark canal is a dark
	 * canal's water. Where they stand and how they move is
	 * hd_splash_math.h's, a function of their age alone: an HD splash or
	 * spark is its start and its seed, and a frame works out every card
	 * afresh. All of it is this project's own design; only the pages are the
	 * remaster's.
	 *
	 * kCardCap cards a frame: a splash at its peak is kSplashCards (52), the
	 * game keeps four splashes at once (splashes[4]), 208; a tank makes 4 or
	 * 5 sparks a tick (ControlFishtank), each a mist card for 14 to 17
	 * ticks, at most 85 live, RICH2's two tanks 170 should both run at
	 * once, and a tank's burst is kTankBurstCards (64) over its first
	 * 1.2 s, two tanks 128. 208 + 170 + 128 = 506 is under 512; past the
	 * cap the latest wait. kCardSoft
	 * the depth over which a card fades into what stands behind it,
	 * kSprayKept the sparks kept (a spark's card is gone after 17 ticks,
	 * so a tank's last 0.6 s, 18 ticks of 4.5, for two tanks 162),
	 * kSplashesKept the splashes (four live and four ending) - free choices
	 * that hold the counts above. */
	const int kCardCap = 512;
	const float kCardSoft = 64.0f;		// units
	const size_t kSprayKept = 256;
	const size_t kSplashesKept = 8;

	struct LiveSplash
	{
		HDSplashSetup setup;
		double born;		// on the frame clock
		unsigned int seed;
		int body;
	};

	struct LiveSpray
	{
		float x, y, z;
		int strength;
		double born;
		unsigned int seed;
		int body;		// whose foam colour it wears, or -1
	};

	std::vector<LiveSplash> g_splashes;
	std::vector<LiveSpray> g_sprays;
	unsigned int g_card_seed = 0;
	HDRenderSplashStats g_splash_stats = {};

	/* The tank's water.
	 *
	 * When Lara shoots a fish tank's glass (a shatter static over the
	 * trigger that starts the FISHTANK), the game draws the tank's water as
	 * the FISHTANK's own classic mesh, scaled in y by item_flags[1] / 4096
	 * (DrawScaledSpike, game\traps.cpp): a flat top behind the glass and a
	 * curved apron down to the floor in front of it, whole on the first
	 * tick, sinking for 105 ticks and gone on the 106th. The HD path draws
	 * that water as its own, and the classic mesh is left out
	 * (game\traps.cpp): the same mesh, scaled as the classic scales it,
	 * its apron coming out of the glass over the first quarter second and
	 * the whole fading over the last half second before KillItem
	 * (hd_splash_math.h, HDTankDomeAt), drawn with the cards' shaders as a
	 * see-through sheet - more of it where the eye looks along the surface
	 * than where it looks through it (HDTankDomeShare), the spray page
	 * running down it -, and the burst: on the tank's first ticks its
	 * water is thrown out of the opened glass as cards (HDTankBurst; spray
	 * and drops on the cards' pages). All of it in the tank's frame as its
	 * mesh gives it (HDTankShapeOf), tinted as the film of that tank is.
	 *
	 * The engine hands each tank in from the classic draw of its mesh
	 * (game\traps.cpp through hd_level.cpp, HDWaterTank), so the HD form
	 * draws exactly when the classic would; RoomProbe's -tank hands in its
	 * own. A tank's age is its ticks out of item_flags[1]
	 * (HDTankTicksOf), run on smoothly between the ticks by the frame's
	 * clock and held within the tick the game is in. */
	struct LiveTank
	{
		int item;
		float x, y, z;		// the item's place
		float c, s;		// its turn's cosine and sine
		int flags1;
		double born;		// on the frame clock
		double last;		// when it was last handed in
		unsigned int seed;
		bool seen;		// handed in for this frame
	};

	std::vector<LiveTank> g_tanks;
	std::vector<float> g_tank_xyz;
	std::vector<int> g_tank_quads;
	HDTankShape g_tank_shape = {};

	/* The tank's film.
	 *
	 * The water a breaking fish tank pours over its room's floor, as a
	 * sheet of this water: a basin body of its own over the squares of the
	 * floor lasers the tank shorts out (HDWaterFilm, hd_level.cpp builds the
	 * list), at rest kFilmHair over the floor, drawn with the sheets' own
	 * shaders in its room's blend phase - the reflection, the highlight and
	 * the waterline are the sheet's, and its edge takes a channel's bank
	 * foam (strength and band of kWaterClass's channel row), a rim where
	 * the water runs out on the dry floor. It is no water square for
	 * anything else: not counted, not in the WATERLOG lines, no ring
	 * source, no ripple gate, no hidden art, no caustic (a room's caustic
	 * would light the whole floor at every moment), no eye under it.
	 *
	 * Its strength a frame (HDRenderWaterFilmWet, hd_splash_math.h
	 * HDFilmStrength) sets three things. The blend factor: SRCBLEND
	 * BLENDFACTOR, DESTBLEND INVBLENDFACTOR - with the frame's copy bound
	 * the sheet's pixel shader writes the water and the floor behind it
	 * whole, alpha 1 (WaterPS, "with the copy"), so that blend is an exact
	 * cross-fade between the film and the floor; without the copy the film
	 * is not drawn. The height: the surface stands kFilmRise times the
	 * strength over its rest, through WaterVS's crest lift (gSwell z, with
	 * w so low that every vertex takes all of it), tapering in from the
	 * edge over the basin's taper and still while tomb5.hd_motion is off -
	 * deepest while the tank pours, sinking as the lasers' count runs
	 * down; a deeper film bends and darkens the floor seen through it more.
	 * The foam: its strength times the strength, a density, so the rim
	 * thins as the film fades and the foam's own draw stays premultiplied.
	 *
	 * kFilmHair, 8 units, is about 2 cm at 2.3 mm a unit - a film over the
	 * classic floor and well under the lasers' 128 - a free choice. Its
	 * depth byte (8 over 32, rounded down) is 0: only the variant without
	 * the depth texture reads that byte, and to it the film has no depth.
	 * kFilmRise, 16 units more, about 4 cm, is a free choice, set by eye on
	 * the round's RoomProbe pictures. The film's clip is 0, not kWaterClip:
	 * with the clip at the film's own height the film in RICH2 room 23 lost
	 * a band in front of the tank's plinth, where the frame lies less than
	 * 8 units under its surface (RoomProbe -tank 33 1.5 at 960 x 540, the
	 * film at rest: 73,132 pixels differ from the dry view with the clip
	 * 8, 84,559 with 0). */
	const int kFilmHair = 8;		// units
	const float kFilmRise = 16.0f;	// units, at the whole strength

	std::vector<HDWaterFilm> g_film_in;
	// Per film of this level: its body, or -1 when it has none, and its wet.
	std::vector<int> g_film_body;
	std::vector<float> g_film_wet;
	// The first film body: the bodies before it are the level's own.
	int g_film_first = -1;

	// The film a body is, or -1.
	int film_of_body(int body)
	{
		if (g_film_first < 0 || body < g_film_first)
			return -1;

		for (size_t f = 0; f < g_film_body.size(); f++)
			if (g_film_body[f] == body)
				return (int)f;

		return -1;
	}

	/* The wet floor.
	 *
	 * When a fish tank breaks, its water runs out over the floor in front
	 * of it, and the floor stays wet: darker, and a mirror at a grazing
	 * view (hd_wet.hlsl). Which floor: the classic squares the water would
	 * reach, out of the floor data of every room (HDWaterRoom). It lands
	 * on the squares under the dome's foot - the apron's footprint on the
	 * floor in the tank's frame, between the glass and the foot, as wide
	 * as the foot (HDTankShapeOf) - and runs on from square to square, to
	 * a flat square at the same height or one step lower (kWetStep, one
	 * click: water runs down a step and never up one, nor down a drop it
	 * would fall into another room through), never behind the glass, into
	 * the tank - and over a sloping square as far as its floor runs level
	 * or down ("the sloping floors" below). A room with a higher dry part
	 * keeps it dry; the run goes
	 * through doors into the rooms the tank's row names (kWetRooms) while
	 * their floor goes on level, the classic floor's own answer to where
	 * water on it would run, and stops kWetRadius past the dome's foot.
	 *
	 * Drawn as a layer kWetLift over the floor - a grid of points every
	 * 256 units, five by five a square, shared between squares - twice a
	 * frame: the darkening over the opaque frame (HDRenderWaterWetDraw),
	 * the mirror after the rooms' glass and glows and before the cards
	 * (HDRenderWaterWetMirror), out of a copy of the frame as it stands
	 * there, so the rooms' glows and what the glass shows are mirrored too,
	 * and the dome, the spray and the objects' glows lie over the mirror as
	 * over the floor. Lara and the objects are mirrored as geometry, drawn
	 * through the floor's reflection into a target of their own just
	 * before the mirror ("the models in the mirror"). Over a polished floor
	 * the marble floors' mirror is the water's ("the polished floor under
	 * the water"). Each point keeps its
	 * way: how far the water runs
	 * from the glass to it, through the grid (the shortest way along
	 * neighbours, diagonals at their own length, a step down at no cost),
	 * pushed back by up to kWetRagged out of a hash of its place; and how
	 * deep the water stands there, a damp place mirroring kWetDamp of a
	 * pool, the rest out of a second hash. How wet it is at a moment is
	 * HDWetAt over HDWetFront (hd_splash_math.h): under the dome as the
	 * apron comes out, past it at a shallow flood's speed. Once the tank
	 * is shot the floor stays wet; a tank that is first seen shot (a
	 * loaded game) is wet out to the radius at once, and so is every floor
	 * from its break while tomb5.hd_motion is off - the water's run is
	 * motion, the wet floor is the game's state.
	 *
	 * The numbers. kWetLift, 4 units (about 1 cm), stays over the floor in
	 * the depth buffer to the far plane (24 bits over 20 to 20,480 units
	 * resolve about 1.2 units at 20,000). Where the frame lies more than
	 * kWetHole under the layer - a drain, a gap of the art - the water
	 * fades out over the next kWetHole. kWetF0 is water's, as the sheets'.
	 * kWetWalls and kWetCap are the polished walls' own (hd_room.hlsl,
	 * HD_GLOSS_ENV and HD_GLOSS_CAP). The lamps' highlight is sharper than
	 * polished marble's (exponent 64) and as much brighter at its peak as
	 * its lobe is narrower: kWetHighlight is the textbook normalisation of
	 * the Blinn-Phong lobe, (n + 8) / 8, at kWetPower. kWetPower, kWetDamp
	 * and kWetHole are free choices, set by eye on RoomProbe pictures.
	 *
	 * The mirror's march (hd_wet.hlsl) takes HD_WET_STEPS (22) steps from
	 * kWetFirstStep, each kWetGrowth times the last - 32 units past the
	 * floor's own depth step, out to about 8,700 units, eight squares -,
	 * halves each crossing HD_WET_REFINE (8) times and takes it where the
	 * frame's depth fits the ray's within kWetFit and kWetFitShare of the
	 * depth. Set on RoomProbe's march debugger (-tankmarch 5) against a
	 * march of 8-unit steps halved ten times, which itself matches the true
	 * mirror - the scene seen from the eye mirrored in the floor - at
	 * RICH2's tank eyes. A step longer than a thin thing - a machine's foot,
	 * a bench's rail - passes it by, and the march takes what lies behind
	 * it: steps that grow less find it. Over the floor the march runs on,
	 * at four eyes of the tank's corridor and a 10-degree fan round two of
	 * them, this march's hit lies more than 2 pixels from the fine one's
	 * for 0 to 2.2 per cent of the fine march's hits (0 to 3.5 with 16
	 * steps of 1.3 halved five times), and the pixels whose mirror is off
	 * the true one where the fine march's is not number 5,751 (8,735); a
	 * tighter tolerance (half, a quarter) or a march that takes the first
	 * crossing behind an edge lowered neither (RoomProbe -tankmarch
	 * map,fit,growth,first; round three's march, 12 steps of 1.5 unhalved,
	 * lay within 2 pixels for only 7 to 27 per cent of its hits).
	 *
	 * The look's two strengths. kWetDark, the share of the floor's light
	 * the water keeps back, and kWetMirror, a factor on the mirror's
	 * weight, stand a third over 0.3 and 1, set by eye on RoomProbe
	 * pictures of RICH2's two tanks: at 0.4 the dark marble there (a luma
	 * of 37 to 58 dry under the wet) still reads as wet stone and not as a
	 * black floor, and the mirror at 4/3 stays under kWetCap's soft hold. */
	const float kWetLift = 4.0f;		// units
	const float kWetHole = 32.0f;		// units
	const int kWetStep = 256;		// units
	const float kWetDark = 0.4f;
	const float kWetMirror = 4.0f / 3.0f;
	const float kWetF0 = 0.02f;
	const float kWetWalls = 0.35f;
	const float kWetPower = 128.0f;
	const float kWetHighlight = (kWetPower + 8.0f) / 8.0f;
	const float kWetCap = 0.75f;
	const float kWetDamp = 0.5f;
	const float kWetFirstStep = 32.0f;	// units
	const float kWetGrowth = 1.2f;
	const float kWetFit = 32.0f;		// units
	const float kWetFitShare = 0.02f;	// of the view depth
	/* The soft mirror: the blur's radius per pixel the march's last step
	 * spans on the screen (hd_wet.hlsl, gWetBlur); RoomProbe -wetblur
	 * draws with another, 0 sharp as before. */
	const float kWetBlur = 1.0f;
	float g_wet_blur = kWetBlur;
	/* And how far from the eye it comes in whole: nearer, the radius shrinks
	 * with the distance - a near ray's steps span many pixels, but there the
	 * strip spans more and the mirror holds still (the owner: fine near). */
	const float kWetBlurNear = 2048.0f;	// units
	float g_wet_blur_near = kWetBlurNear;

	/* Where a tank's water may run: the rooms its row names, by level and
	 * the room the tank stands in - a square is theirs when it lies inside
	 * one of them or its flip twin, off its ring of walls and doors, at that
	 * floor's height - and no further from the glass, along the water's
	 * way, than the dome's foot and kWetRadius past it. A tank without a row
	 * keeps to its own room. The front stops there, so the edge is the
	 * front's own: kWetRamp wide and ragged by kWetRagged (kWetRadius and
	 * its reason are in hd_splash_math.h, with the front's other numbers).
	 *
	 * The rows. RICH2's tank in room 122 runs over 122 and the rooms round
	 * it, 121, 45 and the corridor 46; the lasers' tank in room 23 over that
	 * room's lowered floor and its plinth (the run never leaves room 23
	 * there). A row names the rooms; the radius keeps the water near. */
	struct WetRooms
	{
		const char* stem;	// the level file's stem
		int tank;		// the room the tank stands in
		int rooms[6];		// the rooms its water runs over, -1 past them
	};

	const WetRooms kWetRooms[] =
	{
		{ "RICH2", 122, { 122, 121, 45, 46, -1, -1 } },
		{ "RICH2", 23, { 23, -1, -1, -1, -1, -1 } },
	};

	struct WetPoint
	{
		float x, y, z;		// world
		float way;		// units from the glass along the water's way
		float puddle;		// the share of the mirror there
		float march;		// 0 where the art mirrors the room itself
		// 1 on a polished floor's mirror face (wet_polished), else 0
		float polished;
		float light;		// its room's light, for the fog
		float field[3];		// the light field's texture coordinate
	};

	// A floor's numbers, for its SPLASHLOG line (HDRenderWaterWetLine).
	struct WetInfo
	{
		int item;		// the FISHTANK
		int squares;		// wet squares
		int vertices, triangles;
		int rooms[16];		// the rooms whose floor they are, -1 past them
		int heights;		// distinct floor heights among them
		float reach;		// the dome's foot from the glass, units
		float furthest;		// the furthest point along the water's way
		int found;		// the squares the run found before the radius
		int polished;		// its cells on a polished floor (wet_polished)
	};

	/* A square of a wet floor for RoomProbe's map (HDRenderWaterWetSquare):
	 * its corner, floor, room, its nearest point's way from the glass before
	 * the ragged push, and whether the radius keeps it. */
	struct WetSquare
	{
		int x, z, y, room;
		float way;
		bool kept;
		int under, mirrored;	// the art's cells (see wet_copied)
		bool clear;		// its floor a clear floor (wet_clear)
		bool copied;
		int slope;		// its floor's slope function, 0 flat
		int polished;		// its cells on a polished floor (wet_polished)
	};

	// The world box of wet points (HDRenderWaterWetDraw).
	struct WetBox
	{
		float x0, z0, x1, z1;
	};

	struct WetFloor
	{
		WetInfo info;
		std::vector<WetPoint> points;
		std::vector<unsigned short> tris;
		std::vector<WetSquare> squares;
		float limit;		// the furthest the front runs, along the way
		int state;		// 0 whole, 1 pouring, 2 shot and done
		bool started;		// born is known
		double born;		// on the frame clock
	};

	std::vector<HDWaterWetTank> g_wet_in;
	std::vector<WetFloor> g_wet;
	float g_wet_fixed = -1.0f;
	// RoomProbe's -tankwetall: the run without its rooms and radius.
	bool g_wet_limited = true;

	/* The creatures' mark: whether this frame marked anything (the sheets
	 * then draw their second, line-less pass) and the stencil states the
	 * bracket found, put back by HDRenderWaterMarkEnd. */
	bool g_mark_frame = false;
	bool g_mark_open = false;
	DWORD g_mark_saved[kMarkStateCount] = {};
	/* What the mark's log line (HDRenderWaterMarkLine) reports of the last
	 * frame: why the bracket opened or not (0 open, 1 no device, 2 no sheets
	 * built, 3 no depth texture, 4 no depth variant, 5 no stencil), whether
	 * the depth-stencil surface the mark wrote to and the one the sheets
	 * tested were the INTZ surface, the objects drawn in the bracket, the
	 * sheet draws and the split draws; logged once a level. */
	int g_mark_why = 0;
	int g_mark_surface = 0;
	int g_mark_objects = 0;
	// Triangles HDRenderWaterMarkTriangles marked since the last line.
	int g_mark_late = 0;
	int g_mark_sheet_surface = 0;
	int g_mark_sheets = 0;
	int g_mark_split = 0;
	// The foam's own draws (draw_water_sheet) since the last line.
	int g_mark_foam = 0;
	bool g_mark_logged = false;
	HDRenderWaterLevel g_wlevel = {};
	IDirect3DVertexBuffer9* g_sheet_vb = 0;
	IDirect3DIndexBuffer9* g_sheet_ib = 0;
	std::string g_water_path;
	float g_basin_rows[16];
	bool g_basin_rows_set = false;
	/* The diagnostic tool's sheet timing: GPU timestamps round every sheet
	 * draw, waited for at once - a measurement that stalls the pipeline,
	 * never on in the engine. */
	bool g_sheet_timing = false;
	double g_sheet_gpu_ms = 0.0;
	int g_sheet_timed = 0;
	double g_copy_gpu_ms = 0.0;
	int g_copy_timed = 0;
	// And of the feet's second draws, with the segments they carried.
	double g_foot_gpu_ms = 0.0;
	int g_foot_timed = 0;
	int g_foot_segs = 0;
	// And of the rivers' second draws.
	double g_river_gpu_ms = 0.0;
	int g_river_timed = 0;
	// And of the foam's own draws.
	double g_foam_gpu_ms = 0.0;
	int g_foam_timed = 0;

	/* The frame behind the sheets (HDRenderWaterCopy) and the depth texture
	 * (HDRenderWaterDepthBind), both D3DPOOL_DEFAULT and released with the
	 * level's buffers (release_water_sheets), so a new level, a shutdown
	 * and a lost device take them along before a Reset. g_copy_valid
	 * says the copy holds this bracket's frame; HDRenderFrameEnd clears it.
	 * g_depth_own is the device's own depth-stencil, held while the INTZ
	 * surface stands in its place. */
	IDirect3DTexture9* g_copy_texture = 0;
	IDirect3DSurface9* g_copy_surface = 0;
	D3DSURFACE_DESC g_copy_desc = {};
	bool g_copy_valid = false;
	IDirect3DTexture9* g_depth_texture = 0;
	IDirect3DSurface9* g_depth_surface = 0;
	IDirect3DSurface9* g_depth_own = 0;
	// RoomProbe's switches (HDRenderWaterStage2); the engine keeps them on.
	bool g_copy_allowed = true;
	bool g_depth_allowed = true;
	int g_depth_view = 0;
	float g_bend_scale = 1.0f;

	const D3DFORMAT kIntz = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');

	/* The later copy the wet floor's mirror reads (wet_copy_late),
	 * of the frame's size and format, D3DPOOL_DEFAULT as the opaque frame's
	 * and released with it; g_wet_ready while the frame owes the wet
	 * floors their second draw (HDRenderWaterWetMirror). */
	IDirect3DTexture9* g_wet_late = 0;
	IDirect3DSurface9* g_wet_late_surface = 0;
	D3DSURFACE_DESC g_wet_late_desc = {};
	bool g_wet_late_valid = false;
	bool g_wet_ready = false;

	/* The models through the floor's mirror (HDRenderWaterWetModels): a
	 * target of the frame's size, its colour the models drawn through the
	 * mirror and its alpha 0 where the frame drew a model, 1 elsewhere, and
	 * a depth
	 * texture of its own, INTZ as the frame's, so the mirror's pixel shader
	 * reads the mirrored models' depth; D3DPOOL_DEFAULT and released with
	 * the later copy. g_wet_models while this frame's models are in them,
	 * g_wet_plane the plane they were mirrored in (view space: the world's
	 * up, its height along it). The first draw keeps the world-to-view
	 * matrix it drew in (g_wet_view) and the floor height most of its drawn
	 * points stand at (g_wet_height): the plane. */
	IDirect3DTexture9* g_wet_model = 0;
	IDirect3DSurface9* g_wet_model_surface = 0;
	IDirect3DTexture9* g_wet_model_depth = 0;
	IDirect3DSurface9* g_wet_model_depth_surface = 0;
	D3DSURFACE_DESC g_wet_model_desc = {};
	bool g_wet_models = false;
	/* The room behind the models: the frame's colour taken before the
	 * objects are drawn (HDRenderWaterWetRoomCopy, from
	 * HDRenderWaterMarkBegin), while the last frame's wet floor mirrored
	 * models. A model hides in the frame what a reflected ray passing
	 * behind it meets; the march takes this copy's colour where it went
	 * behind a model's place and met nothing else (WetMarch, why 4).
	 * g_wet_room_valid for this frame's copy, g_wet_room_want while the
	 * models' mirror ran. */
	IDirect3DTexture9* g_wet_room = 0;
	IDirect3DSurface9* g_wet_room_surface = 0;
	D3DSURFACE_DESC g_wet_room_desc = {};
	bool g_wet_room_valid = false;
	bool g_wet_room_want = false;
	/* The soft mirror's layer (wet_layer): the mirror drawn alone, its
	 * alpha the blur's radius, at the frame's size in 32 bits. */
	IDirect3DTexture9* g_wet_soft = 0;
	IDirect3DSurface9* g_wet_soft_surface = 0;
	D3DSURFACE_DESC g_wet_soft_desc = {};
	/* Whether this level's frames have drawn the objects once through the
	 * models' clip planes (wet_warm_models); cleared at the level's build. */
	bool g_wet_models_warmed = false;
	float g_wet_plane[4] = {};
	float g_wet_view[16] = {};
	int g_wet_height = 0;
	bool g_wet_height_set = false;
	/* The world box (x0, z0, x1, z1) of the drawn wet points on that plane,
	 * and the clip planes made of it: view space for RoomProbe's stand-in,
	 * which the fixed function draws (HDRenderWaterProbeStand), and the
	 * mirrored projection's clip space; g_wet_clip_on while they stand. */
	float g_wet_box[4] = {};
	float g_wet_clip_view[5][4] = {};
	float g_wet_clip[5][4] = {};
	bool g_wet_clip_on = false;

	void release_wet_models()
	{
		if (g_wet_model_surface)
		{
			g_wet_model_surface->Release();
			g_wet_model_surface = 0;
		}

		if (g_wet_model) { g_wet_model->Release(); g_wet_model = 0; }

		if (g_wet_model_depth_surface)
		{
			g_wet_model_depth_surface->Release();
			g_wet_model_depth_surface = 0;
		}

		if (g_wet_model_depth)
		{
			g_wet_model_depth->Release();
			g_wet_model_depth = 0;
		}

		g_wet_models = false;

		if (g_wet_room_surface)
		{
			g_wet_room_surface->Release();
			g_wet_room_surface = 0;
		}

		if (g_wet_room) { g_wet_room->Release(); g_wet_room = 0; }
		g_wet_room_valid = false;

		if (g_wet_soft_surface)
		{
			g_wet_soft_surface->Release();
			g_wet_soft_surface = 0;
		}

		if (g_wet_soft) { g_wet_soft->Release(); g_wet_soft = 0; }
	}

	void release_wet_late()
	{
		if (g_wet_late_surface)
		{
			g_wet_late_surface->Release();
			g_wet_late_surface = 0;
		}

		if (g_wet_late) { g_wet_late->Release(); g_wet_late = 0; }
		g_wet_late_valid = false;
		release_wet_models();
	}

	// The marble floors' target (see "the marble floors").
	void release_marble();
	void marble_build(const HDWaterRoom* rooms, int count);

	void release_water_copy()
	{
		if (g_copy_surface) { g_copy_surface->Release(); g_copy_surface = 0; }
		if (g_copy_texture) { g_copy_texture->Release(); g_copy_texture = 0; }
		g_copy_valid = false;
		release_wet_late();
		release_marble();
	}

	// The copies belong to the bracket they were taken in (HDRenderFrameEnd).
	void water_bracket_end()
	{
		g_copy_valid = false;
		g_wet_late_valid = false;
		g_wet_ready = false;
		g_wet_models = false;
		g_wet_room_valid = false;
	}

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
		release_water_copy();
		HDRenderWaterDepthUnbind();
		if (g_sheet_vb) { g_sheet_vb->Release(); g_sheet_vb = 0; }
		if (g_sheet_ib) { g_sheet_ib->Release(); g_sheet_ib = 0; }
		g_wsquares.clear();
		g_wbodies.clear();
		g_wlooks.clear();
		g_sheet_ranges.clear();
		g_group_body.clear();
		g_caustic_top.clear();
		g_caustic_river.clear();
		g_caustic_on = false;
		g_sheet_at.clear();
		g_sheet_world.clear();
		g_wsources.clear();
		g_wspring_rows.clear();
		g_rings.clear();
		g_ring_probe.clear();
		g_splashes.clear();
		g_sprays.clear();
		g_film_body.clear();
		g_film_wet.clear();
		g_film_first = -1;
		g_wet.clear();
		g_splash_stats = HDRenderSplashStats();
		g_wfeet.clear();
		g_wsegs.clear();
		g_curtains = 0;
		g_wfalls.clear();
		g_wroom_origin.clear();
		g_wroom_drawn.clear();
		g_wroom_body.clear();
		g_wroom_fallback.clear();
		g_wroom_air.clear();
		g_wdeep_on = false;
		g_wdeep_top = 0.0f;
		g_lara_in_water = false;
		g_mark_frame = false;
		g_mark_sheets = 0;
		g_mark_split = 0;
		g_mark_foam = 0;
		g_mark_logged = false;
		std::memset(g_ripple_seen, 0, sizeof(g_ripple_seen));
		g_wlevel = HDRenderWaterLevel();
		g_wlevel.fileLines = -1;

		// The remaster's water art the sheets hid draws again.
		for (HDGroup& group : g_groups)
			for (int pass = 0; pass < 3; pass++)
				for (HDRange& range : group.pass[pass])
				{
					range.artHidden = false;
					range.artSpared = false;
				}
	}

	/* The data rule for a body's class.
	 *
	 *   river    the water under any of its squares names a sink (a TO_SINK
	 *            trigger): the classic level pulls Lara through it
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
	 * submarine, open air, a sea. The river comes first: a current is the
	 * level's own word that the water flows, open air or not - ANDY3's two
	 * rivers lie out of doors. */
	int WaterClassOf(const HDRenderWaterBody& b)
	{
		if (b.sinks)
			return HD_WATER_RIVER;

		if (b.openAir)
			return HD_WATER_SEA;

		if (b.sectors >= 16 && b.perimeter > 0 &&
			2.0f * (float)b.sectors / (float)b.perimeter < 2.0f)
			return HD_WATER_CHANNEL;

		return HD_WATER_BASIN;
	}

	/* The art's ripple: the three numbers of gRipple for the remaster's still
	 * water art (HDWaterArt), out of this project's water data alone.
	 *
	 * The water under a part is the body whose squares in the part's own room
	 * lie under its box and within 32 units of its height (the slack of the
	 * art over a sheet, step 8 of HDRenderSetWaterSheets) - the body with the
	 * most such squares. 22 of the game's 28 parts lie over one: ANDREA2's
	 * fountain of rooms 144, 229 and 231, ANDY2's two and nineteen of JOBY5's.
	 * The six that lie over none stand for their own water - the squares
	 * their triangles cover, and the class the data rule (WaterClassOf) gives
	 * them with their room's open air and no sink: JOBY4's sea (3,538
	 * squares, open air) and JOBY5's two (900) are seas; the soup in the
	 * kitchen pots of JOBY3 and JOBY5 and ANDREA1's fountain bowls in room 59
	 * are basins. The nearest body would not do: JOBY4's one body is a basin
	 * of two squares, 16,000 units under its sea.
	 *
	 * The numbers are HDArtRipple's (hd_splash_math.h): the body's squares,
	 * its class's longest wave row - kWaterClass's first wavelength and
	 * height - and calm, its row's `waves` (1 without a row), the sea row's
	 * steepness as the travel's measure, kWaterG, and the part's own span. The
	 * part's UV span bounds the travel and does not set the scale: the parts'
	 * maps onto their pages are not planar - JOBY5's small basins lay 48
	 * texels over 944 units along x and over 3,826 along z - so no length on
	 * the page stands for a length in the room. A row of hd_water.txt reaches
	 * the art through its body, as its class and its `waves`. */
	int ArtRippleClass(int openAir, const HDWaterArt& art)
	{
		HDRenderWaterBody b = {};

		b.openAir = openAir;
		b.sectors = (int)(art.area + 0.5f);
		b.perimeter = 2 * (art.boxX + art.boxZ);
		return WaterClassOf(b);
	}

	bool ArtRippleOf(int cls, float squares, float waves, float span,
		float* out)
	{
		if (cls < 0 || cls >= HD_WATER_CLASSES)
			cls = HD_WATER_BASIN;

		const HDWaterClass& c = kWaterClass[cls];
		const HDWaterClass& sea = kWaterClass[HD_WATER_SEA];
		HDArtRippleWater w;

		w.squares = squares;
		w.lambda = c.lambda[0];
		w.height = c.height[0];
		w.calm = c.calm;
		w.waves = waves;
		return HDArtRipple(w, sea.height[0] / sea.lambda[0], kWaterG, span,
			out);
	}

	// Every group's art, after the bodies, and each range of a part.
	void water_art_ripples(const HDWaterRoom* rooms, int count)
	{
		for (size_t g = 0; g < g_groups.size(); g++)
		{
			HDGroup& group = g_groups[g];

			for (HDWaterArt& art : group.waterArt)
			{
				std::vector<int> under(g_wbodies.size(), 0);
				int best = -1;

				for (const WaterSquare& s : g_wsquares)
				{
					const int x0 = s.sx * 1024;
					const int z0 = s.sz * 1024;

					if (s.room != (int)g || s.body < 0 ||
						s.body >= (int)under.size() ||
						s.wy < art.box[4] - 32 || s.wy > art.box[5] + 32 ||
						x0 >= art.box[1] || x0 + 1024 <= art.box[0] ||
						z0 >= art.box[3] || z0 + 1024 <= art.box[2])
						continue;

					under[(size_t)s.body]++;
				}

				for (size_t b = 0; b < under.size(); b++)
					if (under[b] > (best < 0 ? 0 : under[(size_t)best]))
						best = (int)b;

				float waves = 1.0f;

				art.body = best;

				if (best >= 0)
				{
					const HDRenderWaterBody& b = g_wbodies[(size_t)best].info;

					art.cls = b.cls;
					art.squares = (float)b.sectors;
					waves = b.waves;
				}
				else
				{
					const int open = (int)g < count &&
						(rooms[g].flags & HD_WATER_ROOM_OPEN) ? 1 : 0;

					art.cls = ArtRippleClass(open, art);
					art.squares = art.area;
				}

				ArtRippleOf(art.cls, art.squares, waves, art.span, art.ripple);

				for (int pass = 0; pass < 3; pass++)
					for (HDRange& range : group.pass[pass])
						if (range.ripplePart == art.part)
						{
							range.ripple[0] = art.ripple[0];
							range.ripple[1] = art.ripple[1];
							range.ripple[2] = art.ripple[2];
						}
			}
		}
	}

	/* hd_water.txt, one row a line:
	 *
	 *   <LEVEL> <room> <class|->  [tint <r> <g> <b>] [waves <s>] [foam <s>]
	 *                             [bend <s>] [swell <s>]
	 *                             [ripple <x> <z> <size> <seconds>]
	 *                             [art keep|hide]
	 *                             [foot <x1> <z1> <x2> <z2> <reach> |
	 *                              foot off]
	 *                             [flow <dx> <dz> <speed>]
	 *                             [lift <units>] [clip <units>]
	 *                             [crest <s>]
	 *                             [spring <x> <z> <strength> [<seconds>]]
	 *
	 * LEVEL is the level file's name without its extension (ANDREA2), room
	 * a room with water of the bodies the row tunes - every body the room
	 * has water in - class basin, channel, sea or river, - to keep the
	 * class, or
	 * none for no sheet over those bodies (they keep their class; a later
	 * row of any other class, - included, gives the sheet back). tint
	 * multiplies the shallow and the deep colour, waves the wave heights,
	 * foam the shore foam and the whitecaps, bend how far the waves move
	 * the floor seen through them (the refraction), swell how far the
	 * sheet's vertices rise and fall with the waves (0 lays the sheet at
	 * rest, its pixels keep the waves; a row without the key leaves it as
	 * it is); each 0 to 4, 1 changing nothing. ripple starts a ring at world x,
	 * z (units, as the game's own ripples and trview give them) every `seconds`
	 * (0.1 to 60) while the level runs, `size` (1 to 255) as a ripple's size -
	 * a fountain's jet landing, where the game itself makes no ripple; the rest
	 * of the row applies as any row's. art keep draws the remaster's own water
	 * art over the sheets of the row's bodies, art hide (what a row without the
	 * key says) leaves it out - step 8 of HDRenderSetWaterSheets. foot lays
	 * the foot of a waterfall (see "the foot of a waterfall") along the line
	 * from world x1, z1 to x2, z2 with that reach (64 to 2048 units) on the
	 * surface of the row's room's square under its middle - a fountain's jet
	 * that has no waterfall object -, and foot off takes the waterfall
	 * objects' own feet in the row's bodies away; the key swirl x z r is
	 * read as a foot from x, z to x, z with reach r and swirl off as foot
	 * off. flow sets the bodies' current by hand: the way along world x
	 * and z (-1 to 1 each, not both 0; it is made a unit vector) and the
	 * speed in units a second (0 to 4000), in place of the one their sinks
	 * give - what a river draws with (a body of another class keeps it for
	 * the log). lift stands the bodies' sheets that many units over the
	 * water's top (-256 to 256, whole units; 0 takes a lift away), level,
	 * and runs them on over the land lower than that (see "the lift" and
	 * "the skirt" in HDRenderSetWaterSheets); clip sets how far under the
	 * surface at rest the frame may lie and still be left to itself (0 to
	 * 256, whole units; see kWaterClip); crest how far the rows are short
	 * crests in a fan (0 to 1: 0 the straight rows, 1 what a body without
	 * the key draws; see "the crests" over kWaterFan). A row without lift,
	 * clip or crest leaves that value as it is. spring sets a spring at
	 * world x, z with that strength (0 to 1; 0 takes away the table's
	 * spring of the row's room) ringing every `seconds` (0.1 to 60; without
	 * them the table's, else 0.7 over the strength) - a row whose one key it
	 * is, with class -, tunes no body (hd_spring.h, HDSpringFileApply). '#'
	 * starts a comment.
	 * Returns 1 for a row, 0 for a line without one, -1 for a line that
	 * cannot be read. */
	int ParseWaterLine(const char* text, WaterRow* out)
	{
		char line[256];
		char* tok[39];
		char* next = 0;
		int n = 0;

		strncpy_s(line, sizeof(line), text, _TRUNCATE);

		if (char* hash = std::strchr(line, '#'))
			*hash = 0;

		// The longest row: the three words, tint's four, waves', foam's,
		// bend's and swell's two each, ripple's five, art's two, foot's six,
		// flow's four, lift's, clip's and crest's two - 38 tokens.
		for (char* t = strtok_s(line, " \t", &next); t && n < 39;
			t = strtok_s(0, " \t", &next))
			tok[n++] = t;

		if (!n)
			return 0;

		if (n < 3 || n > 38 || std::strlen(tok[0]) >= sizeof(out->stem))
			return -1;

		WaterRow row = {};
		char* end = 0;
		const long room = std::strtol(tok[1], &end, 10);

		if (*end || end == tok[1] || room < 0 || room > 1023)
			return -1;

		strcpy_s(row.stem, sizeof(row.stem), tok[0]);
		row.room = (int)room;
		bool known = !std::strcmp(tok[2], "-");

		row.cls = -1;

		if (!_stricmp(tok[2], "none"))
			row.cls = HD_WATER_NONE, known = true;

		for (int c = 0; c < HD_WATER_CLASSES; c++)
			if (!_stricmp(tok[2], kWaterClass[c].name))
				row.cls = c, known = true;

		if (!known)
			return -1;

		row.tint[0] = row.tint[1] = row.tint[2] = 1.0f;
		row.waves = row.foam = row.bend = 1.0f;

		for (int i = 3; i < n; )
		{
			float* into = 0;
			int count = 0;
			float rip[5];
			const bool ripple = !_stricmp(tok[i], "ripple");
			const bool swirl = !_stricmp(tok[i], "swirl");
			const bool foot = !_stricmp(tok[i], "foot");
			const bool flow = !_stricmp(tok[i], "flow");

			// foot off: a word; foot x1 z1 x2 z2 reach: five numbers; the
			// key swirl the same, swirl x z r three.
			if ((foot || swirl) && i + 1 < n && !_stricmp(tok[i + 1], "off"))
			{
				row.foot = -1;
				row.old += swirl ? 1 : 0;
				i += 2;
				continue;
			}

			// art keep | art hide: a word, not a number.
			if (!_stricmp(tok[i], "art"))
			{
				if (i + 1 >= n)
					return -1;

				if (!_stricmp(tok[i + 1], "keep"))
					row.art = 1;
				else if (!_stricmp(tok[i + 1], "hide"))
					row.art = 0;
				else
					return -1;

				i += 2;
				continue;
			}

			// lift <units>: a whole number of units, -256 to 256 - a click
			// either way.
			if (!_stricmp(tok[i], "lift"))
			{
				if (i + 1 >= n)
					return -1;

				const long units = std::strtol(tok[i + 1], &end, 10);

				if (*end || end == tok[i + 1] || units < -256 || units > 256)
					return -1;

				row.lift = 1;
				row.liftUnits = (int)units;
				i += 2;
				continue;
			}

			// clip <units>: a whole number of units, 0 to 256 - a click.
			if (!_stricmp(tok[i], "clip"))
			{
				if (i + 1 >= n)
					return -1;

				const long units = std::strtol(tok[i + 1], &end, 10);

				if (*end || end == tok[i + 1] || units < 0 || units > 256)
					return -1;

				row.clip = 1;
				row.clipUnits = (int)units;
				i += 2;
				continue;
			}

			// crest <s>: 0 to 1, the straight rows to the short crests.
			if (!_stricmp(tok[i], "crest"))
			{
				if (i + 1 >= n)
					return -1;

				const double s = std::strtod(tok[i + 1], &end);

				if (*end || end == tok[i + 1] || !(s >= 0.0 && s <= 1.0))
					return -1;

				row.crest = 1;
				row.crestScale = (float)s;
				i += 2;
				continue;
			}

			/* spring x z strength [seconds]: a spring at world x, z for the
			 * springs (hd_spring.h, HDSpringParseKey and HDSpringFileApply). */
			if (!_stricmp(tok[i], "spring"))
			{
				const int took = HDSpringParseKey(tok, n, i, row.springKey);

				if (took < 0)
					return -1;

				row.spring = 1;
				row.springOnly = i == 3 && i + took == n && row.cls == -1;
				i += took;
				continue;
			}

			if (!_stricmp(tok[i], "tint"))
				into = row.tint, count = 3;
			else if (!_stricmp(tok[i], "waves"))
				into = &row.waves, count = 1;
			else if (!_stricmp(tok[i], "foam"))
				into = &row.foam, count = 1;
			else if (!_stricmp(tok[i], "bend"))
				into = &row.bend, count = 1;
			else if (!_stricmp(tok[i], "swell"))
				into = &row.swellScale, count = 1, row.swell = 1;
			else if (ripple)
				into = rip, count = 4;
			else if (swirl)
				into = rip, count = 3;
			else if (foot)
				into = rip, count = 5;
			else if (flow)
				into = rip, count = 3;

			if (!into || i + count >= n)
				return -1;

			for (int k = 0; k < count; k++)
			{
				const char* num = tok[i + 1 + k];
				const double v = std::strtod(num, &end);
				/* x and z anywhere in a level, the size and the seconds
				 * as above, a foot's reach (a swirl's radius) 64 to 2048,
				 * a flow's way -1 to 1 along x and z and its speed 0 to
				 * 4000 units a second (a sink of the classic levels'
				 * strongest, 10, pulls at 1200), every scale 0 to 4. */
				const bool place = ((ripple || swirl) && k < 2) ||
					(foot && k < 4);
				const double lo = place ? -1.0e7 : ripple ?
					(k == 2 ? 1.0 : 0.1) : swirl || foot ? 64.0 : flow ?
					(k < 2 ? -1.0 : 0.0) : 0.0;
				const double hi = place ? 1.0e7 : ripple ?
					(k == 2 ? 255.0 : 60.0) : swirl || foot ? 2048.0 :
					flow ? (k < 2 ? 1.0 : 4000.0) : 4.0;

				if (*end || end == num || !(v >= lo && v <= hi))
					return -1;

				into[k] = (float)v;
			}

			if (ripple)
			{
				row.ripple = 1;
				row.rx = rip[0];
				row.rz = rip[1];
				row.rsize = rip[2];
				row.rperiod = rip[3];
			}

			// swirl x z r: a foot of length 0, counted (the log's old=).
			if (swirl || foot)
			{
				row.foot = 1;
				row.footAt[0] = rip[0];
				row.footAt[1] = rip[1];
				row.footAt[2] = swirl ? rip[0] : rip[2];
				row.footAt[3] = swirl ? rip[1] : rip[3];
				row.footAt[4] = swirl ? rip[2] : rip[4];
				row.old += swirl ? 1 : 0;
			}

			// A way of length 0 has no direction: not a flow.
			if (flow)
			{
				if (rip[0] == 0.0f && rip[1] == 0.0f)
					return -1;

				row.flow = 1;
				row.fx = rip[0];
				row.fz = rip[1];
				row.fspeed = rip[2];
			}

			i += 1 + count;
		}

		*out = row;
		return 1;
	}

	// The whole file, line by line (CR, LF or both end a line).
	void read_water_file(std::vector<WaterRow>& rows)
	{
		if (g_water_path.empty())
			return;

		std::FILE* file = std::fopen(g_water_path.c_str(), "rb");

		if (!file)
			return;

		std::string text;
		char buffer[4096];
		size_t got;

		while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0 &&
			text.size() < (1u << 20))
			text.append(buffer, got);

		std::fclose(file);
		g_wlevel.fileLines = 0;

		const char* at = text.c_str();

		// "UTF-8 with BOM" puts three bytes before the first line.
		if ((unsigned char)at[0] == 0xEF && (unsigned char)at[1] == 0xBB &&
			(unsigned char)at[2] == 0xBF)
			at += 3;

		while (*at)
		{
			const char* end = at;

			while (*end && *end != '\n' && *end != '\r')
				end++;

			const std::string line(at, end);
			WaterRow row;
			const int kind = line.size() < 255 ?
				ParseWaterLine(line.c_str(), &row) : -1;

			g_wlevel.fileLines++;

			if (kind < 0)
				g_wlevel.fileBad++;
			else if (kind > 0)
			{
				row.file = 1;
				rows.push_back(row);
				g_wlevel.fileRows++;
				g_wlevel.oldKeys += row.old;
			}

			if (*end == '\r' && end[1] == '\n')
				end += 2;
			else if (*end)
				end++;

			at = end;
		}
	}

	/* A split's height step, five signed bits of the type word, as GetHeight
	 * and GetCeiling sign-extend it (game\control.cpp). */
	int split_step(unsigned short type, int shift)
	{
		int hadj = (type >> shift) & 0x1F;

		return (hadj & 0x10) ? hadj - 32 : hadj;
	}

	/* The corner heights of a square (HDRenderWaterCorners). The engine asks
	 * GetHeight and GetCeiling for a point inside a square, where they work
	 * with (v & 1023) and ((-1 - v) & 1023); at a corner those become the
	 * corner's own 0 or 1024 and 1024 less it, the same linear function on
	 * the square's edge, and a split's triangle is the one a point a unit
	 * inside the corner lies in - the substitution the randomizer's
	 * Tr5Level::cornerFloor and cornerCeiling make, which matched the faces
	 * of all sixteen classic levels corner for corner. The slope words are
	 * the entry the engine reads (HDWaterRoom::slope). */
	void water_corners(int clicks, unsigned short type, unsigned short data,
		int ceiling, int* out)
	{
		const int fn = type & 0x1F;
		const int t[4] = { data & 0xF, (data >> 4) & 0xF, (data >> 8) & 0xF,
			(data >> 12) & 0xF };

		for (int k = 0; k < 4; k++)
		{
			const int cx = (k & 1) ? 1024 : 0;
			const int cz = (k & 2) ? 1024 : 0;
			const int px = cx ? 1023 : 1;
			const int pz = cz ? 1023 : 1;
			int h = clicks * 256;
			int a = 0, b = 0;

			if (!ceiling && fn == 2)		// TILT_TYPE
			{
				a = (short)data >> 8;
				b = (signed char)(data & 0xFF);
			}
			else if (!ceiling && (fn == 7 || fn == 8 ||
				(fn >= 11 && fn <= 14)))	// SPLIT1/2, NOCOLF*
			{
				const bool fam1 = fn == 7 || fn == 11 || fn == 12;

				if (fam1 ? px > 1024 - pz : px > pz)
				{
					h += split_step(type, 5) * 256;
					a = t[3] - t[0];
					b = fam1 ? t[3] - t[2] : t[0] - t[1];
				}
				else
				{
					h += split_step(type, 10) * 256;
					a = t[2] - t[1];
					b = fam1 ? t[0] - t[1] : t[3] - t[2];
				}
			}
			else if (ceiling && fn == 3)		// ROOF_TYPE
			{
				a = (short)data >> 8;
				b = (signed char)(data & 0xFF);
			}
			else if (ceiling && (fn == 9 || fn == 10 ||
				(fn >= 15 && fn <= 18)))	// SPLIT3/4, NOCOLC*
			{
				// The ceiling's nibbles go in negated, its steps the
				// other way round from the floor's.
				const bool fam1 = fn == 9 || fn == 15 || fn == 16;

				if (fam1 ? px <= 1024 - pz : px <= pz)
				{
					h += 256 * split_step(type, 10);
					a = t[1] - t[2];
					b = fam1 ? t[2] - t[3] : t[1] - t[0];
				}
				else
				{
					h += 256 * split_step(type, 5);
					a = t[0] - t[3];
					b = fam1 ? t[1] - t[0] : t[2] - t[3];
				}
			}

			if (!ceiling)
			{
				h += a < 0 ? -((a * cz) >> 2) : (a * (1024 - cz)) >> 2;
				h += b < 0 ? -((b * cx) >> 2) : (b * (1024 - cx)) >> 2;
			}
			else
			{
				h += a < 0 ? (a * cz) >> 2 : -((a * (1024 - cz)) >> 2);
				h += b < 0 ? (b * (1024 - cx)) >> 2 : -((b * cx) >> 2);
			}

			out[k] = h;
		}
	}

	/* The surface of a water square and whether it is one (HDWaterRoom).
	 * The top is the highest corner of the water room's ceiling over the
	 * square - the flat part of a split ceiling; the other part is the
	 * underside of a raised floor -, found in the room below at the
	 * square's place. The dry floor's lowest corner is held against it:
	 * more than a click (256) over the top at every corner, the square is
	 * an island or a rim and no water (false). Without the words the top
	 * is the square's own floor, as for a feeder that hands none. */
	bool water_square(const HDWaterRoom* rooms, int count, int r, int at,
		int pit, int wx, int wz, int* top)
	{
		const HDWaterRoom& rm = rooms[r];

		*top = rm.floor[at] * 256;

		if (!rm.slope || pit < 0 || pit >= count || !rooms[pit].ceiling ||
			!rooms[pit].slope || !rooms[pit].floor)
			return true;

		const HDWaterRoom& b = rooms[pit];
		const int bx = (wx - b.originX) >> 10;
		const int bz = (wz - b.originZ) >> 10;

		if (bx < 0 || bz < 0 || bx >= b.sizeX || bz >= b.sizeZ ||
			b.ceiling[bx * b.sizeZ + bz] == -127)
			return true;

		const int i = bx * b.sizeZ + bz;
		int c[4], f[4];

		water_corners(b.ceiling[i], b.slope[i * 4 + 2], b.slope[i * 4 + 3], 1,
			c);
		water_corners(rm.floor[at], rm.slope[at * 4], rm.slope[at * 4 + 1], 0,
			f);

		int high = c[0], low = f[0];

		for (int k = 1; k < 4; k++)
		{
			high = c[k] < high ? c[k] : high;
			low = f[k] > low ? f[k] : low;
		}

		*top = high;
		return low >= high - 256;
	}

	/* The sink the water under a square names: the first of the squares
	 * straight under it, room by room down through the underwater rooms -
	 * lara.current_active is set where she swims, in the water room, and
	 * a sink's square may lie a room further down. -1 for none. */
	int water_sink(const HDWaterRoom* rooms, int count, int at, int wx,
		int wz)
	{
		for (int hop = 0; hop < 6 && at >= 0 && at < count; hop++)
		{
			const HDWaterRoom& b = rooms[at];
			const int bx = (wx - b.originX) >> 10;
			const int bz = (wz - b.originZ) >> 10;

			if (!(b.flags & HD_WATER_ROOM_UNDERWATER) || !b.below || bx < 0 ||
				bz < 0 || bx >= b.sizeX || bz >= b.sizeZ)
				return -1;

			const int i = bx * b.sizeZ + bz;

			if (b.sink && b.sink[i] >= 0 && b.sink[i] < (int)g_wsinks.size())
				return b.sink[i];

			at = b.below[i];
		}

		return -1;
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

	/* The land square at world sqx, sqz beside a lifted sheet
	 * (HDRenderSetWaterSheets, "the skirt"): the square of the given room,
	 * or of another dry room holding it within 2048 units of the surface, a
	 * square inside a room before one on its rim (a rim square is a wall or
	 * a portal), the nearer floor first. Its corners (water_corners) go to
	 * c, world y, x0 z0, x1 z0, x0 z1, x1 z1 as the classic floor stands;
	 * returns the room, -1 for a wall or no room holding the square. */
	int water_land(const HDWaterRoom* rooms, int count, int own, int sqx,
		int sqz, int wy, int* c)
	{
		int best = -1, bestAt = 0, bestGap = 0;
		bool bestInner = false;

		for (int k = -1; k < count; k++)
		{
			const int r = k < 0 ? own : k;

			if (r < 0 || r >= count || (k >= 0 && r == own))
				continue;

			const HDWaterRoom& rm = rooms[r];

			if ((rm.flags & HD_WATER_ROOM_UNDERWATER) || !rm.floor)
				continue;

			const int sx = (sqx - rm.originX) >> 10;
			const int sz = (sqz - rm.originZ) >> 10;

			if (sx < 0 || sz < 0 || sx >= rm.sizeX || sz >= rm.sizeZ ||
				rm.floor[sx * rm.sizeZ + sz] == -127)
				continue;

			const int i = sx * rm.sizeZ + sz;
			const int gap = rm.floor[i] * 256 > wy ?
				rm.floor[i] * 256 - wy : wy - rm.floor[i] * 256;
			const bool inner = sx > 0 && sz > 0 && sx < rm.sizeX - 1 &&
				sz < rm.sizeZ - 1;

			if (gap > 2048 || (best >= 0 && (bestInner && !inner)) ||
				(best >= 0 && bestInner == inner && gap >= bestGap))
				continue;

			best = r;
			bestAt = i;
			bestGap = gap;
			bestInner = inner;
		}

		if (best < 0)
			return -1;

		const HDWaterRoom& rm = rooms[best];

		if (rm.slope)
			water_corners(rm.floor[bestAt], rm.slope[bestAt * 4],
				rm.slope[bestAt * 4 + 1], 0, c);
		else
			c[0] = c[1] = c[2] = c[3] = rm.floor[bestAt] * 256;

		return best;
	}

	/* The classic land's height over the water's top at world wx, wz of a
	 * land square whose corners water_land gave: the corners interpolated
	 * across the square, units. */
	float water_land_over(const int* c, int sqx, int sqz, int wx, int wz,
		int wy)
	{
		const float u = (float)(wx - sqx) * (1.0f / 1024.0f);
		const float v = (float)(wz - sqz) * (1.0f / 1024.0f);
		const float y = ((float)c[0] * (1.0f - u) + (float)c[1] * u) *
			(1.0f - v) + ((float)c[2] * (1.0f - u) + (float)c[3] * u) * v;

		return (float)wy - y;
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

	/* The wet floor of every tank handed in (see "the wet floor"), out of
	 * the classic floor of every room: the squares, then the points and
	 * their way from the glass. */
	/* The art's own mirror. A room's art can carry a copy of the room
	 * upside down under a floor it draws part-way clear (HD_PASS_CLEAR),
	 * so that the copy shows through the floor as the room's reflection:
	 * RICH2's corridor, room 46 and its flip twin 75, does so west of x
	 * 41,984 for 1,536 units of depth (RoomProbe -tankart, every triangle
	 * of the .TRG: 24 to 874 under each of the corridor's squares, 40 to
	 * 100 per cent of them mirroring one over it within 32 units), and
	 * -tankcensus counts 387 squares of 55 rooms of RICH2 with such a copy
	 * under a clear floor (RICHCUT2 5, TITLE 74, no other level). There
	 * the water adds no mirror of its own: the art's is the room's
	 * reflection, and one marched out of the frame would be a second
	 * picture of it over the first. The wet floor keeps its darkening.
	 * Under an opaque floor the copy does not show, and the square marches
	 * as any other (wet_clear): the corridor's copy reaches on under its
	 * own opaque floor at the mouth, x 44,032 to 48,127, and under room
	 * 122's two squares there.
	 *
	 * Asked of what the level keeps of its art, the 256-unit cells that
	 * hold a vertex of a group (HDGroup::cells): in a square's column, the
	 * cells under its floor - down to kWetCopyDepth - of every room whose
	 * flat floor lies there at that height, and of those the ones with a
	 * cell over the floor at the mirrored height. A square is copied when
	 * its floor is a clear floor, at least kWetCopyCells of those cells are
	 * mirrored and they are a third of what lies under it. Counted on RICH2
	 * (-tankwetmap -tankwetall, the copy= field): the corridor's 36 squares
	 * 21 to 38 cells under, 12 to 34 of them mirrored; room 122's two at
	 * its mouth 30 and 32 under, 15 and 16 mirrored; every other square of
	 * either tank none under but one of room 23's, 3 under and 1 mirrored.
	 * Of the squares the tanks' water reaches, none has a clear floor.
	 * 2,048 units hold the corridor's 1,536 with room to spare;
	 * kWetCopyCells, two of each of a square's four rows of cells, and the
	 * third are free choices in the wide gap between the two kinds of
	 * square. */
	const int kWetCopyDepth = 8;		// cells, 2,048 units
	const int kWetCopyCells = 8;

	// Whether a group has a vertex in a cell, cell indices room local.
	bool wet_cell(const std::vector<unsigned int>& cells, int cx, int cy,
		int cz)
	{
		return std::binary_search(cells.begin(), cells.end(),
			CellKey(cx * 256, cy * 256, cz * 256));
	}

	/* The cells of the art of `list`'s rooms under square sx, sz at floor
	 * y, and of those the ones mirrored by any of them, into the counts
	 * (see above): the copy under a room's floor can belong to the group of
	 * another room that shares the square - room 46's copy runs under room
	 * 122's floor at the corridor's mouth, where room 122 draws what it
	 * mirrors. A point y0 + d under the floor mirrors to y0 - d over it: a
	 * cell j under the floor's own to cell j + 1 over it, or j at its top
	 * edge. */
	void wet_copied(const HDWaterRoom* rooms, const std::vector<int>& list,
		int sx, int sz, int y, int* under, int* mirrored)
	{
		const int fy = y / 256;
		// Whether any listed room has art in a world cell of the column.
		const auto any = [&](int i, int cy, int k)
		{
			for (int r : list)
				if (wet_cell(g_groups[(size_t)r].cells,
					(sx * 1024 - rooms[r].originX) / 256 + i, cy,
					(sz * 1024 - rooms[r].originZ) / 256 + k))
					return true;

			return false;
		};

		for (int i = 0; i < 4; i++)
			for (int k = 0; k < 4; k++)
				for (int j = 1; j <= kWetCopyDepth; j++)
				{
					if (!any(i, fy + j, k))
						continue;

					(*under)++;

					if (any(i, fy - j, k) || any(i, fy - j - 1, k))
						(*mirrored)++;
				}
	}

	/* The polished floor under the water. Where the marble floors are on,
	 * a polished floor already mirrors the room as a plane: the room art,
	 * the statics and the models drawn once more through the floor
	 * ("the marble floors"). The wet floor's own mirror is marched over the
	 * screen and finds only what the frame shows. Laid over a polished floor
	 * as over any other, the water takes the marble's mirror away under
	 * every pixel of its layer (HDRenderMarbleDraw clears its stencil where
	 * the layer's wet is over 0) and lays its own, weaker and blurred, in
	 * its place: at RICH2's tank (RoomProbe, 960 x 540, the tank 60 s after
	 * it broke) the marble's mirror lifts the floor where the water lies by
	 * 11.5 levels on average, the water's own by 4.3, so the wet floor reads
	 * drier than the floor round it, and its outline - the front's ragged
	 * ramp, soft in the darkening - turns into a hard line of 256-unit steps
	 * where the marble's mirror stops at the first wet pixel: a jump of 59
	 * levels between neighbouring pixels (the median over the rows across
	 * it). And a thing whose reflection the march needs leaves the frame as
	 * the eye turns: the march mirrors the vending machine's lit screen in
	 * 536 pixels with the eye 28 degrees down, in none at 43.
	 *
	 * Water on polished stone lies flat in the same plane, 4 units over it,
	 * and mirrors the same room: one planar mirror stands for both. So a
	 * wet point on a polished floor keeps the marble's mirror: the layer's
	 * points the marble's mask reads carry no wet there (g_wet_verts), and
	 * where the marble mirrored that floor's height this frame the point's
	 * own mirror - the march, its light-field stand-in and the mirrored
	 * models - gives way (wet_give_way); its darkening and the lamps'
	 * highlight stay, so the floor reads wet by its darkening over a mirror
	 * that runs on unbroken from the dry floor. Where the marble did not
	 * mirror the height - the frame's planes are capped - or the floor is no
	 * polished floor, or the marble floors are off, the water marches as
	 * before. A polished floor is the marble's own extent, its mirror
	 * faces, asked per 256-unit cell of the layer (wet_polished): a cell
	 * whose wet corners stand at one height and which a face overlaps.
	 * RICH2: every flat square of either tank's floor, 16 cells of 16, and
	 * the flat half of the six splits round the raised blocks, 10 of 16;
	 * 316 cells of tank 182's 352 and all 192 of tank 33's. Where every
	 * drawn point gives way, the models are not mirrored for the water. */
	/* Whether the 256-unit cell at world x0, z0 lies on a polished floor
	 * (see "the polished floor under the water"): a mirror face of the
	 * marble floors (HDGroup::mirrorFaces, "the marble floors") of any of
	 * `list`'s rooms, at floor y within a click's sixteenth, overlaps it
	 * held a unit in, so a face that only meets its edge does not count
	 * (HDWetCellOnFace, hd_water_under.h). No face while the marble floors
	 * are off. */
	bool wet_polished(const std::vector<int>& list, int x0, int z0, int y)
	{
		for (int r : list)
		{
			const HDGroup& g = g_groups[(size_t)r];
			const bool at = (size_t)r * 2 + 1 < g_group_origin.size();
			const float ox = at ? g_group_origin[(size_t)r * 2] : 0.0f;
			const float oz = at ? g_group_origin[(size_t)r * 2 + 1] : 0.0f;

			for (size_t f = 0; f < g.mirrorY.size(); f++)
			{
				if (abs(g.mirrorY[f] - y) > 16)
					continue;

				const float* p = &g.mirrorFaces[f * 9];
				const float ax[3] = { p[0] + ox, p[3] + ox, p[6] + ox };
				const float az[3] = { p[2] + oz, p[5] + oz, p[8] + oz };

				if (HDWetCellOnFace(ax, az, (float)x0, (float)z0, 256.0f,
					1.0f))
					return true;
			}
		}

		return false;
	}

	/* Whether the floor of square sx, sz at height y is a clear floor in
	 * the art of any of `list`'s rooms: a clear triangle of a group's own
	 * horizontal planes (HDGroup::clearFlats) within a click's sixteenth
	 * of it, as -tankcensus asks of the floor's faces. */
	bool wet_clear(const HDWaterRoom* rooms, const std::vector<int>& list,
		int sx, int sz, int y)
	{
		for (int r : list)
			for (const HDFlatPlane& row : g_groups[(size_t)r].clearFlats)
				if (abs(row.y - y) <= 16 && std::binary_search(
					row.cells.begin(), row.cells.end(),
					SectorKey(sx * 1024 - rooms[r].originX,
					sz * 1024 - rooms[r].originZ)))
					return true;

		return false;
	}

	/* The sloping floors. A square whose floor entry is a TILT, a SPLIT1/2
	 * or a NOCOLF (HDWetSlope, hd_splash_math.h) takes the water across an
	 * edge as a flat one does: where that edge lies at the height of the
	 * edge it comes from, or a click under it, at both corners
	 * (HDWetPasses). Water runs down a slope and never up one: of the
	 * square, the points no higher than the edge it came in by (within
	 * kWetSlopeRise) are wet, and it runs on only across an edge whose
	 * corners are wet - a ramp rising from a wet floor keeps its foot wet
	 * and its head dry, the flat half of a split stays as wet as the floor
	 * beside it, a slope falling from it is wet to its foot and passes the
	 * water on to the floor there. Each point lies on the square's own
	 * floor, the plane of its triangle (HDWetHeight, over the corners
	 * water_corners makes), and its cells are halved along the diagonal
	 * its crease follows (HDWetCell), so the layer lies on the floor and a
	 * split's wet half is covered to its crease. RICH2's rooms by tank 182
	 * hold six such squares round its raised blocks in rooms 45 and 121,
	 * splits with one corner 768 up and their other half flat at the wet
	 * floor's height (RoomProbe -tankslopes). */
	/* The two corners of a square's edge toward step k of the run (+x,
	 * -x, +z, -z), z0 or x0 first; `facing` the neighbour's edge that
	 * faces it. */
	void wet_edge(const int* c, int k, bool facing, int* a, int* b)
	{
		static const int side[4][2][2] =
		{
			{ { 1, 3 }, { 0, 2 } }, { { 0, 2 }, { 1, 3 } },
			{ { 2, 3 }, { 0, 1 } }, { { 0, 1 }, { 2, 3 } }
		};

		*a = c[side[k][facing ? 1 : 0][0]];
		*b = c[side[k][facing ? 1 : 0][1]];
	}

	void build_wet_floors(const HDWaterRoom* rooms, int count,
		const char* stem)
	{
		g_wet.clear();

		if (!g_tank_shape.ok || g_wet_in.empty())
			return;

		/* Every floor of every room by world square, flat or sloping: its
		 * highest point, room, whether the square lies inside the room, off
		 * its ring, the slope entry's function (0 flat) and the corners'
		 * heights (HDRenderWaterCorners' order). */
		struct Floor
		{
			int y, room;
			bool inner;
			int fn;
			int c[4];
		};

		const auto square = [](int sx, int sz)
		{
			return ((unsigned long long)(unsigned int)sx << 32) |
				(unsigned long long)(unsigned int)sz;
		};
		// A square or a point at one floor's height.
		const auto level = [](int sx, int sz, int y)
		{
			return ((unsigned long long)(sx & 0xFFFF) << 48) |
				((unsigned long long)(sz & 0xFFFF) << 32) |
				(unsigned long long)(unsigned int)y;
		};
		std::unordered_map<unsigned long long, std::vector<Floor> > floors;

		for (int r = 0; rooms && r < count; r++)
		{
			const HDWaterRoom& w = rooms[r];

			for (int sx = 0; w.floor && sx < w.sizeX; sx++)
				for (int sz = 0; sz < w.sizeZ; sz++)
				{
					const int at = sx * w.sizeZ + sz;
					const int fn = w.slope ? w.slope[at * 4] & 0x1F : 0;

					// A wall (NO_HEIGHT), or a floor of another kind.
					if (w.floor[at] == -127 || (fn && !HDWetSlope(fn)))
						continue;

					Floor f = { (int)w.floor[at] * 256, r, sx > 0 &&
						sz > 0 && sx < w.sizeX - 1 && sz < w.sizeZ - 1, fn };

					for (int k = 0; k < 4; k++)
						f.c[k] = f.y;

					if (fn)
					{
						water_corners(w.floor[at], w.slope[at * 4],
							w.slope[at * 4 + 1], 0, f.c);
						f.y = f.c[0];

						for (int k = 1; k < 4; k++)
							f.y = f.c[k] < f.y ? f.c[k] : f.y;
					}

					floors[square((w.originX >> 10) + sx,
						(w.originZ >> 10) + sz)].push_back(f);
				}
		}

		const HDTankShape& sh = g_tank_shape;

		for (const HDWaterWetTank& t : g_wet_in)
		{
			const float turn = (float)t.yRot * (6.2831853f / 65536.0f);
			const float c = cosf(turn), s = sinf(turn);

			// A world place in the tank's frame (x along the glass, z out).
			const auto local = [&](float wx, float wz, float* lx, float* lz)
			{
				const float dx = wx - (float)t.x, dz = wz - (float)t.z;

				*lx = dx * c - dz * s;
				*lz = dx * s + dz * c;
			};
			const auto inside = [&](float lx, float lz)
			{
				return lx >= sh.footX0 && lx <= sh.footX1 &&
					lz >= sh.glassZ && lz <= sh.footZ;
			};
			const auto behind = [&](int sx, int sz)
			{
				float lx, lz;

				local((float)(sx * 1024 + 512), (float)(sz * 1024 + 512),
					&lx, &lz);
				return lz < sh.glassZ && lx >= sh.footX0 && lx <= sh.footX1;
			};

			// The rooms this tank's water may run over (kWetRooms).
			int own[6] = { t.room, -1, -1, -1, -1, -1 };
			const int* list = own;

			for (const WetRooms& row : kWetRooms)
				if (stem && !_stricmp(row.stem, stem) && row.tank == t.room)
					list = row.rooms;

			const auto listed = [&](int room)
			{
				for (int k = 0; k < 6 && list[k] >= 0; k++)
					if (list[k] == room || (list[k] < count &&
						rooms[list[k]].flip == room))
						return true;

				return false;
			};
			// A floor at height y of square sx, sz inside a listed room.
			const auto allowed = [&](const std::vector<Floor>& at, int y)
			{
				if (!g_wet_limited)
					return true;

				for (const Floor& f : at)
					if (f.y == y && f.inner && listed(f.room))
						return true;

				return false;
			};

			/* The squares under the dome's foot, at the floor the tank
			 * stands on or a step either side of it. */
			struct Wet
			{
				int sx, sz, y, room;
				int fn, c[4];
				int top;	// the edge it came in by, its highest corner
			};

			std::vector<Wet> wet;
			std::unordered_set<unsigned long long> seen;
			const int tx = t.x >> 10, tz = t.z >> 10;
			const int span = 1 + (int)((fabsf(sh.footX0) + fabsf(sh.footX1) +
				fabsf(sh.footZ)) / 1024.0f);

			for (int sx = tx - span; sx <= tx + span; sx++)
				for (int sz = tz - span; sz <= tz + span; sz++)
				{
					bool under = false;

					for (int k = 0; k < 25 && !under; k++)
					{
						float lx, lz;

						local((float)(sx * 1024 + (k % 5) * 256),
							(float)(sz * 1024 + (k / 5) * 256), &lx, &lz);
						under = inside(lx, lz);
					}

					const auto at = floors.find(square(sx, sz));

					if (!under || behind(sx, sz) || at == floors.end())
						continue;

					for (const Floor& f : at->second)
						if (!f.fn && abs(f.y - t.y) <= kWetStep &&
							allowed(at->second, f.y) &&
							seen.insert(level(sx, sz, f.y)).second)
							wet.push_back({ sx, sz, f.y, f.room, 0,
								{ f.y, f.y, f.y, f.y }, f.y });
				}

			/* On over the floor: across an edge level with the one the
			 * water comes from or a step under it, from an edge that is
			 * wet (see "the sloping floors"). */
			for (size_t i = 0; i < wet.size(); i++)
			{
				static const int step[4][2] =
				{
					{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
				};

				for (int k = 0; k < 4; k++)
				{
					const Wet from = wet[i];
					const int nx = from.sx + step[k][0];
					const int nz = from.sz + step[k][1];
					const auto at = floors.find(square(nx, nz));
					int a0, a1;

					wet_edge(from.c, k, false, &a0, &a1);

					if (at == floors.end() || behind(nx, nz) ||
						a0 < from.top - kWetSlopeRise ||
						a1 < from.top - kWetSlopeRise)
						continue;

					for (const Floor& f : at->second)
					{
						int b0, b1;

						wet_edge(f.c, k, true, &b0, &b1);

						if (HDWetPasses(a0, a1, b0, b1, kWetStep) &&
							allowed(at->second, f.y) &&
							seen.insert(level(nx, nz, f.y)).second)
							wet.push_back({ nx, nz, f.y, f.room, f.fn,
								{ f.c[0], f.c[1], f.c[2], f.c[3] },
								b0 < b1 ? b0 : b1 });
					}
				}
			}

			if (wet.empty())
				continue;

			// The points, five by five a square, shared.
			WetFloor w = {};
			std::unordered_map<unsigned long long, int> pointAt;
			std::unordered_map<unsigned long long, std::vector<int> > column;
			std::vector<std::array<int, 25> > grid;
			std::vector<int> heights;

			w.info.item = t.item;
			w.info.reach = sh.footZ - sh.glassZ;

			for (int k = 0; k < 16; k++)
				w.info.rooms[k] = -1;

			for (const Wet& q : wet)
			{
				std::array<int, 25> g;
				float l[3];

				group_light(q.room, l);

				for (int k = 0; k < 25; k++)
				{
					const int px = q.sx * 4 + k % 5, pz = q.sz * 4 + k / 5;
					// On the square's own floor; dry over a slope's entry.
					const int y = q.fn ? (int)lroundf(HDWetHeight(q.fn, q.c,
						(float)(k % 5 * 256), (float)(k / 5 * 256))) : q.y;

					if (y < q.top - kWetSlopeRise)
					{
						g[(size_t)k] = -1;
						continue;
					}

					const unsigned long long key = level(px, pz, y);
					const auto found = pointAt.find(key);

					if (found != pointAt.end())
					{
						g[(size_t)k] = found->second;
						continue;
					}

					WetPoint p = {};

					p.x = (float)(px * 256);
					p.y = (float)y - kWetLift;
					p.z = (float)(pz * 256);
					p.way = 1.0e9f;
					p.light = l[0] > l[1] ? (l[0] > l[2] ? l[0] : l[2]) :
						(l[1] > l[2] ? l[1] : l[2]);

					const unsigned int place = (unsigned int)px * 73856093u ^
						(unsigned int)pz * 19349663u ^ (unsigned int)y;

					p.puddle = kWetDamp + (1.0f - kWetDamp) *
						HDCardShare(place, 2u);
					p.march = 1.0f;

					for (int a = 0; a < 3; a++)
					{
						const float pos = a == 0 ? p.x : a == 1 ? p.y : p.z;
						const float side = (float)g_field_grid.dims[a] *
							HD_GLOSS_FIELD_CELL;

						p.field[a] = side > 0.0f ?
							(pos - g_field_grid.origin[a]) / side : 0.0f;
					}

					float lx, lz;

					local(p.x, p.z, &lx, &lz);

					if (inside(lx, lz))
						p.way = lz - sh.glassZ;

					g[(size_t)k] = (int)w.points.size();
					pointAt[key] = g[(size_t)k];
					column[square(px, pz)].push_back(g[(size_t)k]);
					w.points.push_back(p);
				}

				grid.push_back(g);
			}

			/* The way: the shortest run from the dome's footprint over the
			 * grid - neighbours, diagonals at their own length, and a step
			 * down between two points at one place at no cost. */
			std::vector<std::vector<std::pair<int, float> > > next(
				w.points.size());
			const auto link = [&](int a, int b, float cost)
			{
				// A dry point of a slope (-1) is no point.
				if (a < 0 || b < 0)
					return;

				next[(size_t)a].push_back({ b, cost });
				next[(size_t)b].push_back({ a, cost });
			};

			for (const std::array<int, 25>& g : grid)
				for (int k = 0; k < 25; k++)
				{
					const int i = k % 5, j = k / 5;
					const size_t at = (size_t)k;

					if (i < 4)
						link(g[at], g[at + 1], 256.0f);

					if (j < 4)
						link(g[at], g[at + 5], 256.0f);

					if (i < 4 && j < 4)
					{
						link(g[at], g[at + 6], 362.0f);
						link(g[at + 1], g[at + 5], 362.0f);
					}
				}

			for (const auto& col : column)
				for (size_t a = 0; a < col.second.size(); a++)
					for (size_t b = a + 1; b < col.second.size(); b++)
						link(col.second[a], col.second[b], 0.0f);

			typedef std::pair<float, int> Reach;
			std::priority_queue<Reach, std::vector<Reach>,
				std::greater<Reach> > open;

			for (size_t i = 0; i < w.points.size(); i++)
				if (w.points[i].way < 1.0e9f)
					open.push({ w.points[i].way, (int)i });

			while (!open.empty())
			{
				const Reach r = open.top();

				open.pop();

				if (r.first > w.points[(size_t)r.second].way)
					continue;

				for (const std::pair<int, float>& e :
					next[(size_t)r.second])
					if (r.first + e.second < w.points[(size_t)e.first].way)
					{
						w.points[(size_t)e.first].way = r.first + e.second;
						open.push({ w.points[(size_t)e.first].way, e.first });
					}
			}

			// Each square's nearest point along the way, for the map.
			std::vector<float> nearest(grid.size(), 1.0e9f);

			for (size_t s = 0; s < grid.size(); s++)
				for (int k = 0; k < 25; k++)
				{
					if (grid[s][(size_t)k] < 0)
						continue;

					const float way =
						w.points[(size_t)grid[s][(size_t)k]].way;

					nearest[s] = way < nearest[s] ? way : nearest[s];
				}

			// The ragged edge.
			for (WetPoint& p : w.points)
			{
				const unsigned int place =
					(unsigned int)(int)(p.x / 256.0f) * 73856093u ^
					(unsigned int)(int)(p.z / 256.0f) * 19349663u ^
					(unsigned int)(int)(p.y + kWetLift);

				p.way += kWetRagged * HDCardShare(place, 1u);
			}

			if (w.points.size() > 65535)
				continue;

			/* The radius: a square stays while a point of it is wet with the
			 * front at the limit; its points and triangles, renumbered. */
			w.limit = g_wet_limited ? w.info.reach + kWetRadius : 1.0e9f;

			std::vector<int> renumber(w.points.size(), -1);
			std::vector<WetPoint> kept;

			/* The squares the radius keeps, and the art's own mirror under
			 * them: a copied square's points take no march mirror, so the
			 * march fades out over the grid's step next to it. */
			for (size_t s = 0; s < grid.size(); s++)
			{
				const std::array<int, 25>& g = grid[s];
				const Wet& q = wet[s];
				const std::vector<Floor>& here = floors[square(q.sx, q.sz)];
				WetSquare map = { q.sx * 1024, q.sz * 1024, q.y, q.room,
					nearest[s], false, 0, 0, false, false, q.fn };

				for (int k = 0; k < 25; k++)
					map.kept = map.kept || (g[(size_t)k] >= 0 &&
						w.points[(size_t)g[(size_t)k]].way < w.limit);

				for (const Floor& f : here)
					if (f.y == q.y && f.inner)
					{
						map.room = f.room;
						break;
					}

				std::vector<int> list;

				for (const Floor& f : here)
					if (f.y == q.y && f.room >= 0 && f.room < count &&
						f.room < (int)g_groups.size() &&
						std::find(list.begin(), list.end(), f.room) ==
						list.end())
						list.push_back(f.room);

				// A copy lies under a flat floor (see "the art's own mirror").
				if (!q.fn)
					wet_copied(rooms, list, q.sx, q.sz, q.y, &map.under,
						&map.mirrored);

				map.clear = !q.fn && wet_clear(rooms, list, q.sx, q.sz, q.y);
				map.copied = map.clear && map.mirrored >= kWetCopyCells &&
					map.mirrored * 3 >= map.under;

				for (int k = 0; k < 25 && map.kept && map.copied; k++)
					if (g[(size_t)k] >= 0)
						w.points[(size_t)g[(size_t)k]].march = 0.0f;

				/* The cells on a polished floor, and the points round them: a
				 * cell whose wet corners - three at least - stand at one
				 * height, so a split's flat half counts and its slope not. */
				for (int c = 0; c < 16 && map.kept; c++)
				{
					const int i = c % 4, j = c / 4;
					int corner[4], n = 0, y = 0;
					bool flat = true;

					for (int k = 0; k < 4; k++)
					{
						corner[k] = g[(size_t)((j + k / 2) * 5 + i + k % 2)];

						if (corner[k] < 0)
							continue;

						const int h = (int)lroundf(
							w.points[(size_t)corner[k]].y + kWetLift);

						flat = flat && (n == 0 || h == y);
						y = h;
						n++;
					}

					if (n < 3 || !flat || !wet_polished(list, q.sx * 1024 +
						i * 256, q.sz * 1024 + j * 256, y))
						continue;

					map.polished++;
					w.info.polished++;

					for (int k = 0; k < 4; k++)
						if (corner[k] >= 0)
							w.points[(size_t)corner[k]].polished = 1.0f;
				}

				w.squares.push_back(map);
			}

			for (size_t s = 0; s < grid.size(); s++)
			{
				const std::array<int, 25>& g = grid[s];
				const Wet& q = wet[s];
				const std::vector<Floor>& here = floors[square(q.sx, q.sz)];

				if (!w.squares[s].kept)
					continue;

				w.info.squares++;

				// Every room whose floor this square is, for the log.
				for (const Floor& f : here)
				{
					bool known = f.y != q.y;

					for (int k = 0; k < 16 && w.info.rooms[k] >= 0; k++)
						known = known || w.info.rooms[k] == f.room;

					for (int k = 0; k < 16 && !known; k++)
						if (w.info.rooms[k] < 0)
						{
							w.info.rooms[k] = f.room;
							known = true;
						}
				}

				if (std::find(heights.begin(), heights.end(), q.y) ==
					heights.end())
					heights.push_back(q.y);

				for (int k = 0; k < 25; k++)
				{
					const size_t at = (size_t)g[(size_t)k];

					if (g[(size_t)k] < 0 || renumber[at] >= 0)
						continue;

					renumber[at] = (int)kept.size();
					kept.push_back(w.points[at]);
					w.info.furthest = w.points[at].way > w.info.furthest ?
						w.points[at].way : w.info.furthest;
				}

				// A grid point's new number, -1 for a slope's dry one.
				const auto number = [&](int k)
				{
					return g[(size_t)k] < 0 ? -1 :
						renumber[(size_t)g[(size_t)k]];
				};

				for (int j = 0; j < 4; j++)
					for (int i = 0; i < 4; i++)
					{
						int tri[6];
						const int n = HDWetCell(q.fn, number(j * 5 + i),
							number(j * 5 + i + 1), number(j * 5 + i + 5),
							number(j * 5 + i + 6), tri);

						for (int k = 0; k < n; k++)
							w.tris.push_back((unsigned short)tri[k]);
					}
			}

			w.points.swap(kept);
			w.info.found = (int)wet.size();
			w.info.vertices = (int)w.points.size();
			w.info.triangles = (int)w.tris.size() / 3;
			w.info.heights = (int)heights.size();
			g_wet.push_back(w);
		}
	}

	/* Row i's place in the fan (see "the crests" over kWaterFan), radians
	 * off the body's direction: its rank by wavelength, longest first. */
	float water_fan(const HDWaterClass& c, int i)
	{
		int rank = 0;

		for (int j = 0; j < 8; j++)
			if (c.lambda[j] > c.lambda[i] || (c.lambda[j] == c.lambda[i] &&
				j < i))
				rank++;

		return kWaterFan[rank] * c.fan;
	}

	/* The first `count` rows of a class, turned by `base`, heights times
	 * `waves`; `total` the sum of the first four heights. The rows ride a
	 * current along `base` of the class's speed, or of `current` where it is
	 * not negative (a river's own). `crest` takes the directions from the
	 * straight table (0) to the fan (1). */
	void water_rows(const HDWaterClass& c, float base, float waves,
		float* rows, float* total, int count = 4, float current = -1.0f,
		float crest = 0.0f)
	{
		*total = 0.0f;

		const float speed = current < 0.0f ? c.current : current;

		for (int i = 0; i < count; i++)
		{
			const float k = 6.2831853f / c.lambda[i];
			const float off = c.angle[i] + crest * (water_fan(c, i) -
				c.angle[i]);
			const float th = base + off;

			rows[i * 4 + 0] = k * cosf(th);
			rows[i * 4 + 1] = k * sinf(th);
			rows[i * 4 + 2] = c.calm * sqrtf(kWaterG * k) +
				k * speed * cosf(off);
			rows[i * 4 + 3] = c.height[i] * waves;
			*total += i < 4 ? rows[i * 4 + 3] : 0.0f;
		}
	}

	/* The sheet's rows at the clock `t`: all eight in the phase form
	 * (kx, kz, phase, height) WaveSum8 reads - the phase omega t plus the
	 * row's start, wrapped to kCrestAcross turns - one cell of crests
	 * across (hd_room.hlsl, CrestRows) - in double precision; the height
	 * times the row's envelope, 1 - e + e (0.5 + 0.5 sin(2 pi t / P + i
	 * golden)) with e the class's depth and P the row's period
	 * (kWaterGroupPeriod). `heights` is the sum of the eight heights now,
	 * the whitecaps' measure: a crest breaks where the waves stand high
	 * together, more often while the envelopes swell. */
	void water_rows_now(const WaterLook& look, double t, float* out,
		float* heights)
	{
		*heights = 0.0f;

		for (int i = 0; i < 8; i++)
		{
			const float* r = look.rows + i * 4;
			const double turn = 6.283185307179586;
			const double ph = std::fmod((double)r[2] * t +
				(double)look.phase0[i], turn * kCrestAcross);
			const double env = 1.0 - look.groups + look.groups * (0.5 +
				0.5 * std::sin(turn * t / (double)kWaterGroupPeriod[i] +
				(double)kWaterGolden * i));

			out[i * 4 + 0] = r[0];
			out[i * 4 + 1] = r[1];
			out[i * 4 + 2] = (float)(ph < 0.0 ? ph + turn * kCrestAcross : ph);
			out[i * 4 + 3] = (float)((double)r[3] * env);
			*heights += out[i * 4 + 3];
		}
	}

	// The light a body's colours take: its rooms' light, or at night the
	// moon's at that light's luma (see "the night" over kWaterMoon).
	void water_lit(const float* light, int section, float* out)
	{
		const bool night = section >= 0 && section < HD_WATER_SECTIONS &&
			kWaterPalette[section].night;
		const float luma = 0.299f * light[0] + 0.587f * light[1] +
			0.114f * light[2];

		for (int k = 0; k < 3; k++)
			out[k] = night ? luma * kWaterMoon[k] : light[k];
	}

	// A body's constants out of its class, its section's colours, its light
	// and its level's sky.
	void water_look(WaterLook& out, const WaterBody& wb, int section,
		const char* stem)
	{
		const HDRenderWaterBody& b = wb.info;
		const HDWaterClass& c = kWaterClass[b.cls];
		const HDWaterPalette& pal = kWaterPalette[section];
		float amb[3];

		water_lit(b.light, section, amb);

		/* The body's own direction: a channel's long axis, a river's flow -
		 * its long axis where it has none -, else the class's. A river's
		 * rows ride its own speed, held to kRiverLookCap. */
		float base = c.baseAngle;
		float current = -1.0f;

		if (b.cls == HD_WATER_CHANNEL || (b.cls == HD_WATER_RIVER &&
			b.flow[0] == 0.0f && b.flow[1] == 0.0f))
			base = wb.maxX - wb.minX >= wb.maxZ - wb.minZ ? 0.0f : 1.5707963f;

		out.isRiver = b.cls == HD_WATER_RIVER ? 1 : 0;

		/* A channel with a current of its own (a flow row: ANDREA2's
		 * canal, whose water runs on under its +z end wall through a grate
		 * into water room 133) ends where that current meets a wall, and
		 * there it takes a river's end foam: RiverPS with the end alone
		 * (river[] below, draw_river). A channel without a current has no
		 * end and keeps its look. */
		out.hasEnd = b.cls == HD_WATER_CHANNEL &&
			(b.flow[0] != 0.0f || b.flow[1] != 0.0f) ? 1 : 0;

		if (out.isRiver)
		{
			const bool own = b.flow[0] != 0.0f || b.flow[1] != 0.0f;

			if (own)
				base = atan2f(b.flow[1], b.flow[0]);

			current = own ? b.speed : c.current;
			current = current < kRiverLookCap ? current : kRiverLookCap;
		}

		float total = 0.0f;

		water_rows(c, base, b.waves, out.rows, &total, 8, current, b.crest);

		float artTotal = 0.0f;

		water_rows(c, base, b.waves, out.artRows, &artTotal, 4, current);
		out.crest = b.crest;
		out.crestLen = c.crestLen;
		out.crestDepth = c.crestDepth * b.crest;

		for (int i = 0; i < 8; i++)
			out.phase0[i] = i < 4 ? 0.0f :
				fmodf(kWaterGolden * (float)(i - 3), 6.2831853f);

		out.groups = c.groups;
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
			refl[k] = open ? sky->horizon[k] / 255.0f : amb[k] * 0.65f;
			zen[k] = open ? sky->zenith[k] / 255.0f : amb[k] * 0.25f;
			high[k] = pal.high[k] / 255.0f * c.specStrength *
				(open ? 1.0f : amb[k]);
			foam[k] = pal.foam[k] / 255.0f * (open ? 0.85f : amb[k] * 0.9f);

			// The moon over an open-air body at night (indoors `amb` is
			// moonlit already).
			if (open && pal.night)
			{
				high[k] *= kWaterMoon[k] * kNightHigh;
				foam[k] *= kWaterMoon[k] * kNightFoam;
			}
		}

		deep[3] = 1.0f / c.absorb;
		shallow[3] = c.grain;
		refl[3] = 0.02f;	// F0 of water, n = 1.33
		// The light the fog is measured against: the sky band's flat 1, or
		// the body's own (WaterPS, the note over WATER_OUT).
		zen[3] = open ? 1.0f : (amb[0] + amb[1] + amb[2]) / 3.0f;
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
		misc[3] = total;		// per draw, with the envelope

		/* The stand-in's pattern (hd_room.hlsl, WaterPS): indoors a swing
		 * of +-40 % with a finer second pattern for 0.4 of it, under the
		 * sky the +-25 % of one pattern that reflects the sky's own rows -
		 * free choices, set by eye on the round's RoomProbe strips. With
		 * the horizon at 0.65 and overhead at 0.25 of the body's light
		 * indoors (above), a basin seen at eye level shows its waves. */
		out.life[0] = open ? 0.25f : 0.40f;
		out.life[1] = open ? 0.0f : 0.40f;

		/* The foam's body (hd_room.hlsl): the relief times 0.45 over the
		 * light a flat layer gets, 0.55 + 0.45 times the elevation's sine
		 * (-sun[1]), so that a flat layer keeps the foam's colour. */
		out.foamLight = 0.45f * kWaterFoamRelief / (0.55f - 0.45f * sun[1]);
		out.eyeGain = c.eyeGain;

		/* The bank foam's patches (hd_room.hlsl, PatchField) drift with a
		 * channel's current, along the direction its rows run with it. */
		out.drift[0] = kWaterPatchDrift * c.current * cosf(base);
		out.drift[1] = kWaterPatchDrift * c.current * sinf(base);

		/* RiverPS's c132 and c133 (hd_room.hlsl, "the river"): the way the
		 * river runs, its current and the sheen's strength; the reach of the
		 * foam where it ends, its strength - times the body's foam scale (a
		 * row's `foam`) - and the speed that pushes it there - the bank
		 * patches' share of the current, the water piled at a wall running
		 * slower as the water by a bank does; the last, the floor's caustic
		 * laid by RiverPS, per draw (draw_river). c146: at night the moon's
		 * tint the caustic takes and 1, by day nothing; c147.x the relief's
		 * light (kRiverRelief). */
		std::memset(out.river, 0, sizeof(out.river));

		if (out.isRiver)
		{
			out.river[0] = cosf(base);
			out.river[1] = sinf(base);
			out.river[2] = current;
			out.river[3] = kRiverSheen;
			out.river[4] = kRiverEdge;
			out.river[5] = b.foam;
			out.river[6] = current * kWaterPatchDrift;

			if (pal.night)
				for (int k = 0; k < 3; k++)
					out.river[8 + k] = kWaterMoon[k];

			out.river[11] = pal.night ? 1.0f : 0.0f;
			out.river[12] = kRiverRelief;
		}
		else if (out.hasEnd)
		{
			/* The end alone: the way and the current (held to
			 * kRiverLookCap as a river's), the end foam's reach, strength
			 * and carry as a river's; no sheen (w), no relief (c147.x), no
			 * caustic of its own (c133.w, draw_river) - the channel's own
			 * sheet draws its water. */
			const float current = b.speed < kRiverLookCap ? b.speed :
				kRiverLookCap;

			out.river[0] = b.flow[0];
			out.river[1] = b.flow[1];
			out.river[2] = current;
			out.river[4] = kRiverEdge;
			out.river[5] = b.foam;
			out.river[6] = current * kWaterPatchDrift;
		}

		/* gWaterAbsorb: the absorption per channel in the ratio of
		 * ln(shallow / deep) of the section's palette, its mean the class's
		 * 1 / absorb - the deep colour is what a long path leaves of the
		 * shallow one, channel by channel (hd_room.hlsl, "the frame behind
		 * the sheet"). Out of the palette alone: the body's light and the
		 * tint multiply both colours alike. A channel the palette does not
		 * darken (ratio 1 or below) takes a sixteenth of the mean, so it
		 * still fades over a long path - a free choice, a guard that no row
		 * of the four palettes reaches. Rome comes out 1.31 / 0.82 / 0.87
		 * of the mean, Russia 1.32 / 0.94 / 0.74, Ireland 1.24 / 0.84 /
		 * 0.92, VCI 1.31 / 1.03 / 0.66. w is the refraction's strength,
		 * the absorption length times 1 - 1 / 1.33 (water's refractive
		 * index, the one F0 is taken from): times 1 - T it is the refracted
		 * move of the floor for a short path (hd_room.hlsl, RefractMove) -
		 * 992 units for a basin, 372 for a channel, 223 for a sea - times
		 * the row's bend (1 unless hd_water.txt says otherwise). */
		float* absorb = out.pixel + 32;
		float r[3];
		float mean = 0.0f;

		for (int k = 0; k < 3; k++)
		{
			const float s = pal.base[k] > 0 ? (float)pal.base[k] : 1.0f;
			const float d = pal.deep[k] > 0 ? (float)pal.deep[k] : 1.0f;

			r[k] = logf(s / d);
			mean += r[k] / 3.0f;
		}

		for (int k = 0; k < 3; k++)
		{
			const float share = mean > 0.0f ? r[k] / mean : 1.0f;

			absorb[k] = (share > 0.0625f ? share : 0.0625f) / c.absorb;
		}

		absorb[3] = c.absorb * (1.0f - 1.0f / 1.33f) * b.bend;
	}

	/* Under the water (hd_room.hlsl, "under the water"). An eye in a water
	 * room looks through the body over that room (g_wroom_body, built in
	 * HDRenderSetWaterSheets). The constants are that body's sheet's own:
	 * sigma per channel its gWaterAbsorb and sigma_s its gWaterDeep.w, both
	 * times kUnderReach; the scattered colour its shallow colour, lit by the
	 * body's light and tinted by its row, its brightest channel held to
	 * kUnderLightTop; the upward look's gain kUnderUp; world up in view
	 * space and the eye's depth under the body's surface from the frame -
	 * at least one unit, so that a point under the eye always lies in the
	 * water; and the clear zone kUnderClear.
	 * A fallback room - no sheet lies over it, its body is the level's
	 * largest - has no surface the level knows over it: its depth is
	 * kUnderNoSurface, so every point the eye sees lies in the water. With
	 * the body's surface there instead, a surface under the eye held the
	 * depth to 1 and every point higher than the eye kept its colour: a
	 * hard line across the view at the eye's height (RoomProbe -underwater
	 * in JOBY2's room 14, whose water has no sheet over it, and every room
	 * of JOBY4). Where the level takes the deep look (g_wdeep_on) every
	 * water room takes that look's numbers.
	 *
	 * kUnderReach: the sheet's absorption lengths are those of a view down
	 * through the water onto its floor (a basin 4,000 units, a channel
	 * 1,500, a sea 900); a view along a flooded corridor runs ten squares
	 * and more, and at the sheet's own numbers a channel would keep a
	 * quarter of the light at two squares (exp(-2048 / 1500)). 0.3 lets a
	 * room four to eight squares away keep its texture under the water's
	 * colour and takes the far into it (0.35 turned the far end of
	 * JOBY2's flooded base into flat water, 0.25 thinned ANDREA2's
	 * corridor's yellow-green depth); a free choice, set by eye on the
	 * round's RoomProbe pictures, as kUnderUp is:
	 * looking straight up 1.6 times the scattered light, straight down
	 * 0.4.
	 *
	 * kUnderClear: what lies within two squares of the eye keeps its own
	 * colour - the third-person camera's reach, so Lara near the camera
	 * never changes hers; the extinction runs over the rest of the way. A
	 * free choice, the same for rooms, models and the sheet seen from
	 * below. kUnderLightTop caps the scattered colour, the most the water
	 * adds to a pixel: measured before it, in JOBY2's flooded base (rooms
	 * 13 and 14 of the randomized level, under the body of room 7, lit 1.45
	 * by the open sky) the scattered colour was 0.55, 0.85, 0.94, the
	 * scatter took 0.7 of the mix at three squares and the crates and walls
	 * there were a flat near-white cyan (RoomProbe -underwater). 0.45 holds
	 * it to the brightness of a body lit from inside - ANDREA2's channel
	 * (0.30 at the most) and JOBY2's sea of rooms 42 and 76 (0.41) keep
	 * theirs, the body of room 7 is scaled by 0.48; a free choice, set by
	 * eye on the round's RoomProbe pictures of that base and of ANDREA2's
	 * flooded corridor. Folded in here, no shader slot. kUnderNoSurface:
	 * more than any level's height, so saturate(D / h) is 1 for every
	 * point. */
	const float kUnderReach = 0.3f;
	const float kUnderUp = 0.6f;
	const float kUnderClear = 2048.0f;
	const float kUnderLightTop = 0.45f;
	const float kUnderNoSurface = 1.0e6f;
	const int kUnderDeepShare = 4;		// see "the deep look"

	/* The deep look under the water (JOBY4's dive, see "the deep look"):
	 * the scattered light is the section's DEEP colour - the water where
	 * no floor shows through, not the shallow one a pool shows over its
	 * floor - dimmed by the eye's depth under the highest top of the
	 * level's water rooms, exp(-depth / kDeepDim) and never under
	 * kDeepFloor; its reach kDeepReach in place of kUnderReach and its
	 * upward gain kDeepUp in place of kUnderUp. Measured first, at the start
	 * cavern (room 123) and in rooms 22 and 55 along the dive (RoomProbe
	 * -underwater): the scattered colour was the shallow one held to
	 * kUnderLightTop, 0.27, 0.42, 0.45 - a pale cyan the far walls turned
	 * into, while the void behind them stays black (the frame is cleared to
	 * black and TR5's distance fog has no colour); the level's water rooms
	 * reach from y -1,280 to 19,200 and the dive runs at 12,000 to 19,200,
	 * 13,000 to 20,000 units under that top (the randomizer's Tr5Level,
	 * ROOM_INFO's ceiling and floor). 20,000 units dims the start cavern's
	 * light to about a half and the deepest rooms' to about 0.4; 0.25 keeps
	 * a lit floor in the deepest; 0.5 takes the walls six to eight squares
	 * off into the dark water and keeps Lara's surroundings; 0.8 brightens
	 * the view up toward the surface 1.8 times and darkens the view down
	 * to 0.2. Free choices, set by eye on the round's RoomProbe pictures of
	 * the three places. */
	const float kDeepDim = 30000.0f;
	const float kDeepFloor = 0.25f;
	const float kDeepReach = 0.4f;
	const float kDeepUp = 0.8f;

	// ModelProbe's -under (HDRenderWaterUnderProbe), and this frame's twelve.
	float g_under_probe[9] = {};
	bool g_under_probe_set = false;
	float g_under_now[12] = {};

	/* Along a swim. A water room no sheet lies over has no body of its
	 * own; the level's largest stands in, and its light can be far from
	 * the pools' on either side: in ANDREA1 the scattered light's luma is
	 * 61 (x 255) in room 32, 106 in room 26 and 106 in room 34 by the
	 * largest body, 61 by room 32's (RoomProbe -underwater per room). Such
	 * a room keeps the body of the last room the eye was in that has one
	 * of its own (g_under_body, -1 for none yet: the largest), and the
	 * scattered light and the extinction follow the body's over
	 * kUnderTween seconds of the game clock, a share 1 - e^(-dt /
	 * kUnderTween) a frame - 6.4 % at 60 frames a second, under 3 of luma
	 * for the jump of 45. A frame with the eye out of the water forgets
	 * the tween, so the eye going under takes the look at once, as the
	 * classic picture does; a new level forgets both. The tween's time is a
	 * free choice, the same as Lara's ambient's (hd_laradraw.cpp). */
	const float kUnderTween = 0.25f;
	int g_under_body = -1;
	bool g_under_known = false;
	float g_under_clock = 0.0f;
	float g_under_drawn[8] = {};

	/* `out` is c134 - c136 and c145, sixteen floats: what the shaders read,
	 * or all zero with the eye out of the water. ModelProbe's -under hands
	 * the first nine by hand (the scattered colour already held to
	 * kUnderLightTop, as RoomProbe -underwater prints it); the clear zone
	 * is the engine's. */
	bool water_under_constants(const HDFrameLighting* f, float* out)
	{
		if (f && f->under && g_under_probe_set)
		{
			for (int k = 0; k < 8; k++)
				out[k] = g_under_probe[k];

			for (int k = 0; k < 3; k++)
				out[8 + k] = f->underUp[k];

			out[11] = g_under_probe[8] > 1.0f ? g_under_probe[8] : 1.0f;
			out[12] = kUnderClear;
			std::memcpy(g_under_now, out, sizeof(g_under_now));
			return true;
		}

		std::memset(g_under_now, 0, sizeof(g_under_now));

		if (!f || !f->under || !g_wlevel.built || f->underRoom < 0 ||
			f->underRoom >= (int)g_wroom_body.size())
		{
			g_under_known = false;
			return false;
		}

		int b = g_wroom_body[(size_t)f->underRoom];

		if (b < 0 || b >= (int)g_wlooks.size() || b >= (int)g_wbodies.size())
		{
			g_under_known = false;
			return false;
		}

		const bool fallback = (size_t)f->underRoom <
			g_wroom_fallback.size() && g_wroom_fallback[(size_t)f->underRoom];

		// A room without a body of its own keeps the last one (see "along
		// a swim").
		if (!fallback)
			g_under_body = b;
		else if (g_under_body >= 0 && g_under_body < (int)g_wlooks.size() &&
			g_under_body < (int)g_wbodies.size())
			b = g_under_body;
		const WaterLook& look = g_wdeep_on ? g_wlook_deep :
			g_wlooks[(size_t)b];
		// The eye's depth under the water drawn: a lifted body's surface
		// stands its lift over the water's top.
		const float surface = (float)(g_wbodies[(size_t)b].info.surfaceY -
			g_wbodies[(size_t)b].info.lift);
		/* A room no sheet lies over takes kUnderNoSurface, every point in
		 * the water - but not in a level with the deep look, whose water is
		 * one sea: there the body's surface counts wherever it lies above
		 * the eye, whichever room the eye is in (HDWaterUnderDepth,
		 * hd_water_under.h). JOBY4's sea surface is the remaster's
		 * additive art of room 19 at y -4,253; from room 6 (no sheet over
		 * it) the whole way to it was water, exp(-0.000419 x 18,800) =
		 * 0.0004 of it was left and it went black, while from room 123
		 * under the basin of room 7 it showed (RoomProbe -underwater, the
		 * frame drawn with and without room 19). */
		const float depth = HDWaterUnderDepth(fallback ? 1 : 0,
			g_wdeep_on ? 1 : 0, f->underEyeY, surface, kUnderNoSurface);

		/* The deep look's light: its deep colour, dimmed with the eye's
		 * depth under the water's top (kDeepDim). */
		const float* lit = look.pixel + (g_wdeep_on ? 0 : 4);
		const float top = lit[0] > lit[1] ? (lit[0] > lit[2] ? lit[0] :
			lit[2]) : (lit[1] > lit[2] ? lit[1] : lit[2]);
		const float dim = g_wdeep_on ? expf(-(f->underEyeY - g_wdeep_top >
			0.0f ? f->underEyeY - g_wdeep_top : 0.0f) / kDeepDim) : 1.0f;
		const float held = (top > kUnderLightTop ? kUnderLightTop / top :
			1.0f) * (dim > kDeepFloor ? dim : kDeepFloor);
		const float reach = g_wdeep_on ? kDeepReach : kUnderReach;

		for (int k = 0; k < 3; k++)
		{
			out[k] = look.pixel[32 + k] * reach;
			out[4 + k] = lit[k] * held;
			out[8 + k] = f->underUp[k];
		}

		out[3] = look.pixel[3] * reach;
		out[7] = g_wdeep_on ? kDeepUp : kUnderUp;
		out[11] = depth > 1.0f ? depth : 1.0f;
		out[12] = kUnderClear;

		// The tween of the extinction, the scattered light and its upward
		// gain (see "along a swim"); a clock that stands draws as it stood.
		const float dt = f->clock - g_under_clock;
		const float share = !g_under_known ? 1.0f : dt > 0.0f ?
			1.0f - expf(-dt / kUnderTween) : 0.0f;

		for (int k = 0; k < 8; k++)
		{
			g_under_drawn[k] += (out[k] - g_under_drawn[k]) * share;
			out[k] = g_under_drawn[k];
		}

		if (!g_under_known || dt > 0.0f)
			g_under_clock = f->clock;

		g_under_known = true;
		std::memcpy(g_under_now, out, sizeof(g_under_now));
		return true;
	}

	/* A foot's rings: every kFootRingMin plus up to kFootRingMore seconds by
	 * one hash of its place, starting at a phase by another, as a ripple
	 * row's source starts at one of its own. */
	void foot_clock(WaterFoot& f)
	{
		const float h = fmodf(fabsf(f.x * 0.0013f + f.z * 0.0029f), 1.0f);

		f.period = kFootRingMin + kFootRingMore * h;
		f.phase = fmodf(fabsf(f.x * 0.0071f + f.z * 0.0037f), 1.0f) * f.period;
	}

	/* The sources along one wet stretch of a curtain's footprint (see "the
	 * foot of a waterfall"): `at` its corners, x and z in turn, `ax, az` the
	 * curtain's axis for a stretch of one corner. Returns how many. */
	int stretch_sources(const std::vector<float>& at, float y, float reach,
		int body, int curtain, int fromRow, float ax, float az)
	{
		const size_t corners = at.size() / 2;
		float length = 0.0f;

		for (size_t k = 1; k < corners; k++)
			length += sqrtf((at[k * 2] - at[k * 2 - 2]) *
				(at[k * 2] - at[k * 2 - 2]) + (at[k * 2 + 1] - at[k * 2 - 1]) *
				(at[k * 2 + 1] - at[k * 2 - 1]));

		const int rounded = (int)(length / kFootSourceStep + 0.5f);
		const int n = rounded > 1 ? rounded : 1;
		const float part = length / (float)n;

		for (int i = 0; i < n; i++)
		{
			// The middle of part i, walked along the corners.
			float want = part * ((float)i + 0.5f);
			WaterFoot foot = {};

			foot.x = at[0];
			foot.z = at[1];
			foot.dx = ax;
			foot.dz = az;

			for (size_t k = 1; k < corners; k++)
			{
				const float ex = at[k * 2] - at[k * 2 - 2];
				const float ez = at[k * 2 + 1] - at[k * 2 - 1];
				const float len = sqrtf(ex * ex + ez * ez);

				if (len <= 0.0f)
					continue;

				foot.dx = ex / len;
				foot.dz = ez / len;

				if (want <= len || k + 1 == corners)
				{
					const float f = want < len ? want / len : 1.0f;

					foot.x = at[k * 2 - 2] + ex * f;
					foot.z = at[k * 2 - 1] + ez * f;
					break;
				}

				want -= len;
			}

			foot.y = y;
			foot.half = length > 0.0f ? 0.5f * part : 0.25f * reach;
			foot.reach = reach;
			foot.body = body;
			foot.curtain = curtain;
			foot.fromRow = fromRow;
			foot_clock(foot);
			g_wfeet.push_back(foot);
		}

		return n;
	}

	/* A curtain's footprint into FootPS's segments and its sources: `at`
	 * its corners in order, x and z in turn, `wet` per segment 1, or 0 for
	 * a gap where no water lands; `ax, az` its axis, a unit vector, which a
	 * footprint of one corner is laid along. Returns the sources. */
	int lay_curtain(const std::vector<float>& at, const std::vector<int>& wet,
		float reach, float y, int body, int fromRow, float ax, float az)
	{
		const int curtain = g_curtains++;
		const size_t corners = at.size() / 2;
		int sources = 0;
		bool gaps = false;

		for (size_t k = 1; k < corners; k++)
			gaps = gaps || !wet[k - 1];

		/* The head at a corner toward segment `side`, the one from corner
		 * side - 1 to corner side: round inside a wet stretch and in a
		 * curtain without a gap; kFootHead at the curtain's ends and,
		 * beside a gap, no more than kFootHeadGap of that gap over the
		 * reach (see "the foot of a waterfall"). */
		const auto head = [&](size_t side) -> float
		{
			if (!gaps)
				return 1.0f;

			if (side < 1 || side >= corners)
				return kFootHead;

			if (wet[side - 1])
				return 1.0f;

			const float gx = at[side * 2] - at[side * 2 - 2];
			const float gz = at[side * 2 + 1] - at[side * 2 - 1];
			const float fit = kFootHeadGap * sqrtf(gx * gx + gz * gz) /
				(reach > 1.0f ? reach : 1.0f);

			return fit < kFootHead ? fit : kFootHead;
		};

		for (size_t k = 0; k < corners; k++)
		{
			// A corner alone: no wet segment before it nor after it.
			const bool before = k > 0 && wet[k - 1];
			const bool after = k + 1 < corners && wet[k];

			if (before || after || (corners > 1 && !gaps))
				continue;

			const FootSeg s = { at[k * 2] - ax, at[k * 2 + 1] - az,
				at[k * 2] + ax, at[k * 2 + 1] + az, y, reach, 1.0f,
				head(k), head(k + 1), body, curtain, fromRow };

			g_wsegs.push_back(s);
		}

		for (size_t k = 1; k < corners; k++)
		{
			const bool w = wet[k - 1] != 0;
			const FootSeg s = { at[k * 2 - 2], at[k * 2 - 1], at[k * 2],
				at[k * 2 + 1], y, reach, w ? 1.0f : 0.0f,
				w ? head(k - 1) : 1.0f, w ? head(k + 1) : 1.0f, body,
				curtain, fromRow };

			g_wsegs.push_back(s);
		}

		// The wet stretches: corners joined by segments that are no bridge.
		std::vector<float> stretch;

		for (size_t k = 0; k < corners; k++)
		{
			stretch.push_back(at[k * 2]);
			stretch.push_back(at[k * 2 + 1]);

			if (k + 1 == corners || !wet[k])
			{
				sources += stretch_sources(stretch, y, reach, body, curtain,
					fromRow, ax, az);
				stretch.clear();
			}
		}

		return sources;
	}

	/* A waterfall curtain's foot points into its footprint (see "the foot of
	 * a waterfall"): the principal axis of the points, so a curtain turned
	 * by 45 degrees is laid along itself and not along x; the points in
	 * order along it, gathered every kFootStep; a corner at the mean of every
	 * step that holds points, and a bridge where steps between two corners
	 * hold none. Returns the sources. */
	int lay_fall_curtain(const std::vector<float>& px,
		const std::vector<float>& pz, float y, int body)
	{
		const size_t n = px.size();
		float mx = 0.0f, mz = 0.0f;

		for (size_t k = 0; k < n; k++)
		{
			mx += px[k] / (float)n;
			mz += pz[k] / (float)n;
		}

		float cxx = 0.0f, czz = 0.0f, cxz = 0.0f;

		for (size_t k = 0; k < n; k++)
		{
			cxx += (px[k] - mx) * (px[k] - mx);
			czz += (pz[k] - mz) * (pz[k] - mz);
			cxz += (px[k] - mx) * (pz[k] - mz);
		}

		const float ang = 0.5f * atan2f(2.0f * cxz, cxx - czz);
		const float ax = cosf(ang), az = sinf(ang);
		std::vector<std::pair<float, size_t> > along(n);

		for (size_t k = 0; k < n; k++)
			along[k] = std::make_pair((px[k] - mx) * ax + (pz[k] - mz) * az,
				k);

		std::sort(along.begin(), along.end());

		std::vector<float> at;
		std::vector<int> steps;

		for (size_t a = 0; a < n; )
		{
			const int step = (int)floorf((along[a].first - along[0].first) /
				kFootStep);
			float sx = 0.0f, sz = 0.0f;
			size_t b = a;

			while (b < n && (int)floorf((along[b].first - along[0].first) /
				kFootStep) == step)
			{
				sx += px[along[b].second];
				sz += pz[along[b].second];
				b++;
			}

			at.push_back(sx / (float)(b - a));
			at.push_back(sz / (float)(b - a));
			steps.push_back(step);
			a = b;
		}

		// The bridges, and the longest wet stretch - a strand's width.
		std::vector<int> wet;
		float length = 0.0f, stretch = 0.0f;

		for (size_t k = 1; k < steps.size(); k++)
		{
			wet.push_back(steps[k] - steps[k - 1] <= 1 ? 1 : 0);
			stretch = wet.back() ? stretch + sqrtf((at[k * 2] -
				at[k * 2 - 2]) * (at[k * 2] - at[k * 2 - 2]) +
				(at[k * 2 + 1] - at[k * 2 - 1]) *
				(at[k * 2 + 1] - at[k * 2 - 1])) : 0.0f;
			length = stretch > length ? stretch : length;
		}

		wet.push_back(1);

		const float grown = kFootReachBase + kFootReachGrow * length;
		const float reach = grown < kFootReachMin ? kFootReachMin :
			grown > kFootReachMax ? kFootReachMax : grown;

		return lay_curtain(at, wet, reach, y, body, 0, ax, az);
	}

	// A row's foot (`foot x1 z1 x2 z2 reach`): one segment, or a footprint
	// of one corner where its ends are one place.
	void lay_row_foot(const float* f, float y, int body)
	{
		const float ex = f[2] - f[0], ez = f[3] - f[1];
		const float len = sqrtf(ex * ex + ez * ez);
		const std::vector<float> at(f, f + (len > 0.0f ? 4 : 2));
		const std::vector<int> wet(2, 1);

		lay_curtain(at, wet, f[4], y, body, 1, len > 0.0f ? ex / len : 1.0f,
			len > 0.0f ? ez / len : 0.0f);
	}

	// One row applied to every body its room has water in.
	/* Whether a row's room names the square: its own room, or - for a room
	 * that holds no square of its own, a water room - the water room under
	 * it. A row that names a dry room with water squares binds what it
	 * always bound; one that names the water room under a stretch binds the
	 * bodies over that room alone (a dry room can hold a pond and its
	 * streams, each over a water room of its own). */
	bool water_row_names(const WaterRow& row, const WaterSquare& s, bool own)
	{
		return own ? s.room == row.room : s.pit == row.room;
	}

	bool apply_water_row(const WaterRow& row)
	{
		std::vector<int> hit;
		bool own = false;

		for (size_t i = 0; i < g_wsquares.size() && !own; i++)
			own = g_wsquares[i].room == row.room;

		for (size_t i = 0; i < g_wsquares.size(); i++)
			if (water_row_names(row, g_wsquares[i], own) &&
				g_wsquares[i].body >= 0 &&
				std::find(hit.begin(), hit.end(), g_wsquares[i].body) ==
				hit.end())
				hit.push_back(g_wsquares[i].body);

		for (size_t i = 0; i < hit.size(); i++)
		{
			HDRenderWaterBody& b = g_wbodies[(size_t)hit[i]].info;

			if (row.lift)
				b.lift = row.liftUnits;

			if (row.clip)
				b.clip = row.clipUnits;

			if (row.swell)
				b.swell = row.swellScale;

			if (row.crest)
				b.crest = row.crestScale;

			// A compiled bank row sets the lift, the clip and the swell and
			// leaves the rest of the body as the rows before it left it.
			if (row.bankOnly)
				continue;

			if (row.cls >= 0)
				b.cls = row.cls;

			b.noSheet = row.cls == HD_WATER_NONE ? 1 : 0;

			for (int k = 0; k < 3; k++)
				b.tint[k] = row.tint[k];

			b.waves = row.waves;
			b.foam = row.foam;
			b.bend = row.bend;
			b.artKeep = row.art;
			b.source = row.file ? 2 : 1;
			b.sourceRoom = row.room;

			if (row.foot < 0)
				b.footOff = 1;

			if (row.flow)
			{
				const float len = sqrtf(row.fx * row.fx + row.fz * row.fz);

				b.flow[0] = row.fx / len;
				b.flow[1] = row.fz / len;
				b.speed = row.fspeed;
			}
		}

		/* A row's foot: a curtain of its own on the surface of the row's
		 * room's square under its middle, or of the room's first body. */
		if (row.foot > 0 && !hit.empty())
		{
			const float mx = 0.5f * (row.footAt[0] + row.footAt[2]);
			const float mz = 0.5f * (row.footAt[1] + row.footAt[3]);
			float y = (float)g_wbodies[(size_t)hit[0]].info.surfaceY;
			int body = hit[0];

			for (size_t i = 0; i < g_wsquares.size(); i++)
			{
				const WaterSquare& s = g_wsquares[i];

				if (water_row_names(row, s, own) && mx >= (float)s.wx &&
					mx < (float)(s.wx + 1024) && mz >= (float)s.wz &&
					mz < (float)(s.wz + 1024))
				{
					y = (float)s.wy;
					body = s.body;
				}
			}

			lay_row_foot(row.footAt, y, body);
		}

		/* A ripple source stands at the surface of the row's room's square
		 * under it, or at the surface of the room's first body; its rings
		 * start at a phase of their own, so two sources do not beat in
		 * step. The body it stands in keeps the first such source as its
		 * spring (HDRenderWaterBody::spring). */
		if (row.ripple && !hit.empty())
		{
			WaterSource src = {};
			int body = hit[0];

			src.x = row.rx;
			src.z = row.rz;
			src.y = (float)g_wbodies[(size_t)hit[0]].info.surfaceY;
			src.size = row.rsize;
			src.period = row.rperiod;
			src.phase = fmodf(fabsf(row.rx * 0.0013f + row.rz * 0.0029f),
				1.0f) * row.rperiod;

			for (size_t i = 0; i < g_wsquares.size(); i++)
			{
				const WaterSquare& s = g_wsquares[i];

				if (water_row_names(row, s, own) && row.rx >= (float)s.wx &&
					row.rx < (float)(s.wx + 1024) && row.rz >= (float)s.wz &&
					row.rz < (float)(s.wz + 1024))
				{
					src.y = (float)s.wy;
					body = s.body >= 0 ? s.body : body;
				}
			}

			g_wsources.push_back(src);
			g_wlevel.ringSources++;

			HDRenderWaterBody& spring = g_wbodies[(size_t)body].info;

			if (!spring.spring)
			{
				spring.spring = 1;
				spring.springAt[0] = row.rx;
				spring.springAt[1] = row.rz;
			}
		}

		return !hit.empty();
	}
}

void HDRenderWaterFile(const char* path)
{
	g_water_path = path ? path : "";
}

void HDRenderWaterSinks(const HDWaterSink* sinks, int count)
{
	g_wsinks.assign(sinks && count > 0 ? sinks : 0,
		sinks && count > 0 ? sinks + count : 0);
}

void HDRenderWaterCorners(int clicks, unsigned short type,
	unsigned short data, int ceiling, int* out)
{
	if (out)
		water_corners(clicks, type, data, ceiling, out);
}

void HDRenderSetWaterSheets(const HDWaterRoom* rooms, int count, int section,
	const char* stem, int sheets)
{
	release_water_sheets();

	if (!rooms || count <= 0 || g_groups.empty())
		return;

	// The marble floors' faces and clear floors, by the rooms' flags.
	marble_build(rooms, count);

	if (section < 0 || section >= HD_WATER_SECTIONS)
		section = HD_WATER_ROME;

	g_wlevel.section = section;
	g_wflip.assign((size_t)count, 0);

	const int groups = (int)g_groups.size();

	// Each room's origin and HD, for the art's falls (HDRenderWaterFalls).
	g_wroom_origin.assign((size_t)count * 2, 0);
	g_wroom_drawn.assign((size_t)count, 0);

	for (int r = 0; r < count; r++)
	{
		g_wroom_origin[(size_t)r * 2] = rooms[r].originX;
		g_wroom_origin[(size_t)r * 2 + 1] = rooms[r].originZ;
		g_wroom_drawn[(size_t)r] = rooms[r].drawn ? 1 : 0;
	}

	g_caustic_top.assign((size_t)groups, -1.0e9f);
	g_caustic_on = sheets != 0;
	g_group_body.assign((size_t)groups, -1);
	g_sheet_ranges.assign((size_t)groups, std::vector<SheetRange>());

	/* 1. The water squares. A square draws a sheet when sheets are asked
	 * for and its room draws its own HD group, whatever flat art the
	 * remaster lays in that plane over it (an override row of class none
	 * takes the sheet away, step 5). Its surface is the water room's top
	 * there, and a square whose dry floor stands over that top by more than
	 * a click at every corner is none (water_square); the WATERLOG counts
	 * both. The room below gets the caustic from the surface down - the
	 * lowest surface, should two stand over it.
	 *
	 * The flipped water: a room under the square that is dry while its
	 * flipmap alternate is an underwater room holds water only once
	 * FlipMap has swapped the two (JOBY5's corridor floor, room 38, and
	 * its flooded alternate 3). The square is water then, its water the
	 * alternate's - a pair stands at one place, so its top and depth are
	 * read there - and it is marked with the room whose flip floods it:
	 * its body draws only while that room is swapped (body_shown,
	 * HDRenderWaterFlipRoom), as the art of a pair follows the swap. Its
	 * squares come after all others, so its bodies are numbered after the
	 * bodies every flip state holds. */
	std::vector<WaterSquare> flipped;

	for (int r = 0; r < count; r++)
	{
		const HDWaterRoom& rm = rooms[r];

		if ((rm.flags & HD_WATER_ROOM_UNDERWATER) || !rm.floor || !rm.below)
			continue;

		for (int sx = 0; sx < rm.sizeX; sx++)
			for (int sz = 0; sz < rm.sizeZ; sz++)
			{
				const int at = sx * rm.sizeZ + sz;
				int pit = rm.below[at];
				int alt = 0;

				if (pit < 0 || pit >= count || rm.floor[at] == -127)
					continue;

				if (!(rooms[pit].flags & HD_WATER_ROOM_UNDERWATER))
				{
					const int a = rooms[pit].flip;

					if (a < 0 || a >= count ||
						!(rooms[a].flags & HD_WATER_ROOM_UNDERWATER))
						continue;

					alt = pit + 1;
					pit = a;
				}

				WaterSquare s = {};

				s.room = r;
				s.sx = sx;
				s.sz = sz;
				s.wx = rm.originX + sx * 1024;
				s.wz = rm.originZ + sz * 1024;

				/* The water's top, and the rivers' sinks, with the sheets
				 * alone: with tomb5.hd_water 0 the bodies stand at the
				 * squares' floors and carry no current, as without them -
				 * their rows move the remaster's water art (step 6). */
				s.wy = rm.floor[at] * 256;
				s.sink = -1;
				s.pit = pit;
				s.alt = alt;

				if (sheets && !water_square(rooms, count, r, at, pit, s.wx,
					s.wz, &s.wy))
				{
					g_wlevel.dropped++;
					continue;
				}

				if (sheets)
				{
					g_wlevel.lowered += s.wy != rm.floor[at] * 256 ? 1 : 0;
					s.sink = water_sink(rooms, count, pit, s.wx, s.wz);
				}
				s.localY = s.wy - rm.originY;
				s.depth = water_depth(rooms, count, pit, s.wx, s.wz, s.wy);
				s.open = (rm.flags & HD_WATER_ROOM_OPEN) ? 1 : 0;
				s.body = -1;
				s.sheet = sheets && rm.drawn && r < groups ? 1 : 0;
				(alt ? flipped : g_wsquares).push_back(s);

				if (pit < groups)
				{
					const float top = (float)(s.wy - rooms[pit].originY);

					if (top > g_caustic_top[(size_t)pit])
						g_caustic_top[(size_t)pit] = top;
				}
			}
	}

	g_wsquares.insert(g_wsquares.end(), flipped.begin(), flipped.end());

	for (int g = 0; g < groups; g++)
		g_wlevel.causticRooms += g_caustic_top[(size_t)g] > -1.0e8f ? 1 : 0;

	/* 2. The squares by world place and height. A flipmap pair stands at one
	 * place twice: the first answers the map and both draw; the second is
	 * counted as a twin. Two rooms that are no pair and hold one square at
	 * one height are counted as an overlap. */
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
		else
			g_wlevel.twins++;
	}

	g_wlevel.sectors = (int)at.size();

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
		b.info.waves = b.info.foam = b.info.bend = 1.0f;
		b.info.clip = kWaterClip;
		b.info.swell = 1.0f;
		b.alt = seed.alt;
		b.info.alt = seed.alt - 1;
		b.info.crest = 1.0f;

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

				if (f == at.end() ||
					g_wsquares[(size_t)f->second].alt != s.alt)
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

	/* 4. Per body: its rooms, its twins, its deepest square, the open air a
	 * twin may add, and its light - the rooms' lights weighted by their
	 * squares, over the rooms that draw HD. */
	std::vector<double> light(g_wbodies.size() * 3, 0.0);
	std::vector<int> lit(g_wbodies.size(), 0);
	std::vector<std::vector<int> > roomsOf(g_wbodies.size());
	// Per body: the ways toward the sinks summed, and their speeds.
	std::vector<double> pull(g_wbodies.size() * 3, 0.0);

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];
		HDRenderWaterBody& b = g_wbodies[(size_t)s.body].info;
		const int first = at[WaterKey(s.wx, s.wz, s.wy)];
		const int other = g_wsquares[(size_t)first].room;

		/* A place whose water names a sink pulls toward it: the way from the
		 * square's middle to the sink, as LaraWaterCurrent steers her, and
		 * its speed, each place once. */
		if (first == (int)i && s.sink >= 0)
		{
			const HDWaterSink& k = g_wsinks[(size_t)s.sink];
			const double dx = (double)k.x - (s.wx + 512);
			const double dz = (double)k.z - (s.wz + 512);
			const double len = sqrt(dx * dx + dz * dz);

			if (len > 1.0)
			{
				pull[(size_t)s.body * 3] += dx / len;
				pull[(size_t)s.body * 3 + 1] += dz / len;
			}

			pull[(size_t)s.body * 3 + 2] += kSinkUnitsPerSecond * k.strength;
			b.sinks++;
		}

		if (first != (int)i && (other == s.room ||
			rooms[s.room].flip == other || rooms[other].flip == s.room))
			b.twins++;

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

	/* A sink in the water. A sink of the camera table - an entry a TO_SINK
	 * command names - that lies in a body's water, inside one of its squares,
	 * at or under its surface and above the floor there, pulls that body too,
	 * wherever its trigger squares lie: the mean way from each of them to
	 * the sink (the way LaraWaterCurrent steers her there) and the speed its
	 * strength gives. A body is a river by them only when their trigger
	 * squares number at least kRiverCover of its own squares - a current
	 * along the body, not a drain in a pool: in ANDY3 the river of rooms 38
	 * and 41 holds sinks 7, 8 and 18, named by 10 squares against its 10,
	 * and the sea of rooms 10 to 17 holds sink 1, named by 2 squares
	 * against its 42 - refused (the camera table and every TO_SINK
	 * command of the classic level, counted with the randomizer's
	 * Tr5Level; RoomProbe -hdwater 1 -nodraw, `lying=` and `refused=`). */
	if (sheets && !g_wsinks.empty())
	{
		const size_t nb = g_wbodies.size();
		std::vector<int> body(g_wsinks.size(), -1);
		std::vector<int> named(g_wsinks.size(), 0);
		std::vector<int> places(nb, 0);
		std::vector<int> cover(nb, 0);

		for (size_t i = 0; i < g_wsquares.size(); i++)
			if (at[WaterKey(g_wsquares[i].wx, g_wsquares[i].wz,
				g_wsquares[i].wy)] == (int)i)
				places[(size_t)g_wsquares[i].body]++;

		for (int r = 0; r < count; r++)
			for (int i = 0; rooms[r].sink && i < rooms[r].sizeX *
				rooms[r].sizeZ; i++)
				if (rooms[r].sink[i] >= 0 &&
					rooms[r].sink[i] < (int)named.size())
					named[(size_t)rooms[r].sink[i]]++;

		for (size_t c = 0; c < g_wsinks.size(); c++)
		{
			const HDWaterSink& k = g_wsinks[c];

			for (size_t i = 0; named[c] && i < g_wsquares.size(); i++)
			{
				const WaterSquare& s = g_wsquares[i];

				if (k.x < s.wx || k.x >= s.wx + 1024 || k.z < s.wz ||
					k.z >= s.wz + 1024 || k.y < s.wy ||
					k.y > s.wy + s.depth)
					continue;

				body[c] = s.body;
				cover[(size_t)s.body] += named[c];
				break;
			}
		}

		for (size_t c = 0; c < g_wsinks.size(); c++)
		{
			if (body[c] < 0)
				continue;

			const HDWaterSink& k = g_wsinks[c];
			const size_t b = (size_t)body[c];
			HDRenderWaterBody& info = g_wbodies[b].info;

			if (cover[b] < kRiverCover * places[b])
			{
				info.refused++;
				continue;
			}

			for (int r = 0; r < count; r++)
				for (int i = 0; rooms[r].sink && i < rooms[r].sizeX *
					rooms[r].sizeZ; i++)
				{
					if (rooms[r].sink[i] != (int)c)
						continue;

					const double dx = (double)k.x - (rooms[r].originX +
						(i / rooms[r].sizeZ) * 1024 + 512);
					const double dz = (double)k.z - (rooms[r].originZ +
						(i % rooms[r].sizeZ) * 1024 + 512);
					const double len = sqrt(dx * dx + dz * dz);

					if (len > 1.0)
					{
						pull[b * 3] += dx / len;
						pull[b * 3 + 1] += dz / len;
					}
				}

			pull[b * 3 + 2] += kSinkUnitsPerSecond * k.strength;
			info.sinks++;
			info.lying++;
		}
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

			info.raw[k] = raw;
			info.light[k] = l < kWaterLightFloor ? kWaterLightFloor :
				l > kWaterLightCap ? kWaterLightCap : l;
		}

		// The current: the mean way toward the sinks, their mean speed.
		if (info.sinks)
		{
			const double len = sqrt(pull[b * 3] * pull[b * 3] +
				pull[b * 3 + 1] * pull[b * 3 + 1]);

			info.flow[0] = len > 1.0e-6 ? (float)(pull[b * 3] / len) : 0.0f;
			info.flow[1] = len > 1.0e-6 ? (float)(pull[b * 3 + 1] / len) :
				0.0f;
			info.speed = (float)(pull[b * 3 + 2] / info.sinks);
		}

		info.ruleCls = WaterClassOf(info);
		info.cls = info.ruleCls;
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
		row.bend = 1.0f;

		// Its ripple source with the sheets alone, like the river rows.
		if (sheets && w.ripple[2] > 0.0f)
		{
			row.ripple = 1;
			row.rx = w.ripple[0];
			row.rz = w.ripple[1];
			row.rsize = w.ripple[2];
			row.rperiod = w.ripple[3];
		}

		rows.push_back(row);
	}

	/* The compiled river rows: their class (river, or -1 to keep it) with
	 * their flow, the rest of the row as one that sets nothing else - with
	 * the sheets alone, as the sinks make rivers with the sheets alone
	 * (step 4). */
	for (size_t o = 0; sheets &&
		o < sizeof(kWaterFlow) / sizeof(kWaterFlow[0]); o++)
	{
		const HDWaterFlowRow& w = kWaterFlow[o];
		WaterRow row = {};

		strcpy_s(row.stem, sizeof(row.stem), w.stem);
		row.room = w.room;
		row.cls = w.cls;
		row.tint[0] = row.tint[1] = row.tint[2] = 1.0f;
		row.waves = row.foam = row.bend = 1.0f;
		row.flow = 1;
		row.fx = w.dx;
		row.fz = w.dz;
		row.fspeed = w.speed;
		rows.push_back(row);
	}

	// The compiled bank rows, with the sheets alone like the river rows.
	for (size_t o = 0; sheets &&
		o < sizeof(kWaterBank) / sizeof(kWaterBank[0]); o++)
	{
		const HDWaterBankRow& w = kWaterBank[o];
		WaterRow row = {};

		strcpy_s(row.stem, sizeof(row.stem), w.stem);
		row.room = w.room;
		row.cls = -1;
		row.lift = 1;
		row.liftUnits = w.lift;
		row.clip = 1;
		row.clipUnits = w.clip;
		row.swell = 1;
		row.swellScale = w.swell;
		row.bankOnly = 1;
		rows.push_back(row);
	}

	read_water_file(rows);

	/* A row of the file with a ripple for a room takes the place of the
	 * compiled row's source there, so the two do not ring twice at one
	 * place. */
	for (size_t r = 0; r < rows.size(); r++)
		for (size_t f = 0; rows[r].ripple && !rows[r].file &&
			f < rows.size(); f++)
			if (rows[f].file && rows[f].ripple &&
				rows[f].room == rows[r].room &&
				!_stricmp(rows[f].stem, rows[r].stem))
				rows[r].ripple = 0;

	for (size_t r = 0; r < rows.size(); r++)
	{
		if (!stem || _stricmp(stem, rows[r].stem))
			continue;

		if (rows[r].spring)
		{
			HDRenderSpringRow s;

			s.room = rows[r].room;
			std::memcpy(s.key, rows[r].springKey, sizeof(s.key));
			g_wspring_rows.push_back(s);
		}

		if (rows[r].springOnly)
			continue;

		if (apply_water_row(rows[r]))
			g_wlevel.rowsApplied++;
		else
			g_wlevel.rowsUnmatched++;
	}

	// A lift is the sheets' alone: with tomb5.hd_water 0 nothing stands
	// lifted, a file's row included.
	for (size_t b = 0; !sheets && b < g_wbodies.size(); b++)
		g_wbodies[b].info.lift = 0;

	/* The squares of a body a row of class none reached draw no sheet, and
	 * the classic faces there come back (HDRenderWaterSheetAt). Then the
	 * sheets per body, each place once: a place draws when any square at it
	 * does. */
	std::unordered_set<unsigned long long> drawing;

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		WaterSquare& s = g_wsquares[i];

		if (g_wbodies[(size_t)s.body].info.noSheet)
			s.sheet = 0;

		if (s.sheet)
			drawing.insert(WaterKey(s.wx, s.wz, s.wy));
	}

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];
		const unsigned long long key = WaterKey(s.wx, s.wz, s.wy);

		if (at[key] == (int)i && drawing.count(key))
		{
			g_wbodies[(size_t)s.body].info.sheets++;
			g_wlevel.sheets++;
		}
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

	// The art's ripple, now that the bodies and their classes stand.
	water_art_ripples(rooms, count);

	/* The eye under the water (see "under the water"): the body each
	 * underwater room is looked through. A room under sheet squares takes
	 * the body most of them belong to, a room further down the body of the
	 * room over it - walked down six rooms, as water_depth walks -, and a
	 * water room no sheet lies over at all the level's largest body, a free
	 * choice: its water is the level's water. The same walk gives each such
	 * room its air room, the dry room most of those squares lie in - where
	 * Lara stands when she wades in that water (HDRenderWaterAirRoom). */
	g_wroom_body.assign((size_t)count, -1);
	g_wroom_fallback.assign((size_t)count, 0);
	g_wroom_air.assign((size_t)count, -1);
	// A new level's water forgets the last body and the tween ("along a
	// swim").
	g_under_body = -1;
	g_under_known = false;

	{
		const size_t nb = g_wbodies.size();
		std::vector<int> votes((size_t)count * nb, 0);
		std::map<std::pair<int, int>, int> airVotes;

		for (const WaterSquare& s : g_wsquares)
		{
			if (s.body < 0 || s.room < 0 || s.room >= count ||
				!rooms[s.room].below)
				continue;

			const HDWaterRoom& rm = rooms[s.room];
			const int pit = rm.below[s.sx * rm.sizeZ + s.sz];

			if (pit >= 0 && pit < count &&
				(rooms[pit].flags & HD_WATER_ROOM_UNDERWATER))
			{
				votes[(size_t)pit * nb + (size_t)s.body]++;

				const int n = ++airVotes[std::make_pair(pit, s.room)];
				const int now = g_wroom_air[(size_t)pit];

				if (now < 0 || n > airVotes[std::make_pair(pit, now)])
					g_wroom_air[(size_t)pit] = s.room;
			}
		}

		for (int r = 0; r < count; r++)
			for (size_t b = 0; b < nb; b++)
			{
				const int now = g_wroom_body[(size_t)r];

				if (votes[(size_t)r * nb + b] > 0 && (now < 0 ||
					votes[(size_t)r * nb + b] > votes[(size_t)r * nb +
					(size_t)now]))
					g_wroom_body[(size_t)r] = (int)b;
			}

		for (int hop = 0; hop < 6; hop++)
			for (int r = 0; r < count; r++)
			{
				const HDWaterRoom& rm = rooms[r];

				if (g_wroom_body[(size_t)r] < 0 || !rm.below ||
					!(rm.flags & HD_WATER_ROOM_UNDERWATER))
					continue;

				for (int i = 0; i < rm.sizeX * rm.sizeZ; i++)
				{
					const int q = rm.below[i];

					if (q >= 0 && q < count &&
						(rooms[q].flags & HD_WATER_ROOM_UNDERWATER) &&
						g_wroom_body[(size_t)q] < 0)
					{
						g_wroom_body[(size_t)q] = g_wroom_body[(size_t)r];
						g_wroom_air[(size_t)q] = g_wroom_air[(size_t)r];
					}
				}
			}

		int largest = -1;

		for (size_t b = 0; b < nb; b++)
			if (largest < 0 || g_wbodies[b].info.sectors >
				g_wbodies[(size_t)largest].info.sectors)
				largest = (int)b;

		for (int r = 0; r < count; r++)
			if (rooms[r].flags & HD_WATER_ROOM_UNDERWATER)
			{
				if (g_wroom_body[(size_t)r] >= 0)
					g_wlevel.underRooms++;
				else if (largest >= 0)
				{
					g_wroom_body[(size_t)r] = largest;
					g_wroom_fallback[(size_t)r] = 1;
					g_wlevel.underFallback++;
				}
			}

		/* The water rooms a river is seen through (upload_water_room):
		 * their body a river's, and a sheet over them. */
		g_caustic_river.assign((size_t)groups, 0);

		for (int r = 0; r < count && r < groups; r++)
		{
			const int b = g_wroom_body[(size_t)r];

			g_caustic_river[(size_t)r] = b >= 0 && b < (int)nb &&
				!g_wroom_fallback[(size_t)r] &&
				g_wbodies[(size_t)b].info.cls == HD_WATER_RIVER ? 1 : 0;
		}

		/* The deep look. Where the fallback rooms outnumber the rooms under
		 * a sheet more than kUnderDeepShare times, the level's water is
		 * mostly water no sheet lies over - JOBY4, 132 rooms against the
		 * two under its one basin of two squares, a level swum in the
		 * diving suit -, and the basin's look is no look for it: every one
		 * of its water rooms, the two under the basin with them (the start
		 * cavern, room 123, is one: RoomProbe -underwater prints its air
		 * room 7), takes the sea class's absorption and the section's
		 * palette at a light of 1, the palette's own colour (the basin's
		 * 1.50 is a sunlit room's far from the dive). 4 is a free choice
		 * between JOBY4's 66 and the 1.33 of RICH2 and 1.22 of ANDY3, the
		 * next ones over the fifteen levels (RoomProbe -hdwater 1 -nodraw,
		 * the level line's under= and under_fallback=).
		 *
		 * And only a sea: a level whose water has a surface over the dive,
		 * the art of one water room laid as the sea's surface far over its
		 * own squares (HDWaterSurfaceRoom, hd_water_under.h, asked of the
		 * rooms as built - JOBY4's room 19, its art 851 units under the
		 * water's top). A dive looks up to such a surface; a flooded room
		 * has its own ceiling over its water and its art is its own floor.
		 * RICH3 has one water room, 190 (flooded by a flip), and no sheet
		 * over any, so it passed the share with 1 against 0; its art lies
		 * 4,864 units under its top, no sea's surface, and it takes the
		 * look of its level's one body, a channel (HDWaterDeepLook). */
		std::vector<int> surfWet((size_t)count, 0);
		std::vector<int> surfArt((size_t)count, 0);
		std::vector<float> surfCeil((size_t)count, 1.0e9f);
		std::vector<float> surfTop((size_t)count, 0.0f);

		for (int r = 0; r < count; r++)
		{
			const HDWaterRoom& rm = rooms[r];

			surfWet[(size_t)r] = (rm.flags & HD_WATER_ROOM_UNDERWATER) ? 1 : 0;

			// A square of the water build: floor and ceiling both a height.
			for (int i = 0; rm.ceiling && rm.floor && i < rm.sizeX * rm.sizeZ;
				i++)
				if (rm.floor[i] != -127 && rm.ceiling[i] != -127 &&
					rm.ceiling[i] * 256.0f < surfCeil[(size_t)r])
					surfCeil[(size_t)r] = rm.ceiling[i] * 256.0f;

			if (r < groups && g_groups[(size_t)r].hasBounds)
			{
				surfArt[(size_t)r] = 1;
				surfTop[(size_t)r] = g_groups[(size_t)r].mins[1] +
					(float)rm.originY;
			}
		}

		const int surface = HDWaterSurfaceRoom(&surfWet[0], &surfCeil[0],
			&surfArt[0], &surfTop[0], count, kHDWaterSurfaceReach);

		g_wdeep_on = largest >= 0 && HDWaterDeepLook(g_wlevel.underFallback,
			g_wlevel.underRooms, kUnderDeepShare, surface);

		if (g_wdeep_on)
		{
			WaterBody deep = g_wbodies[(size_t)largest];

			/* The water's top over the dive: the highest ceiling of a
			 * square of any water room (y counts down). */
			g_wdeep_top = 1.0e9f;

			for (int r = 0; r < count; r++)
			{
				const HDWaterRoom& rm = rooms[r];

				for (int i = 0; rm.ceiling && rm.floor &&
					(rm.flags & HD_WATER_ROOM_UNDERWATER) &&
					i < rm.sizeX * rm.sizeZ; i++)
					if (rm.floor[i] != -127 && rm.ceiling[i] != -127 &&
						rm.ceiling[i] * 256.0f < g_wdeep_top)
						g_wdeep_top = rm.ceiling[i] * 256.0f;
			}

			deep.info.cls = HD_WATER_SEA;
			deep.info.openAir = 0;

			for (int k = 0; k < 3; k++)
				deep.info.light[k] = 1.0f;

			water_look(g_wlook_deep, deep, section, stem);
		}

		g_wlevel.underDeep = g_wdeep_on ? 1 : 0;
	}

	/* 7. The grid: 5 x 5 vertices a square, every 256 units, one range per
	 * body of a group. The vertex is the room vertex with colour1 read as
	 * the depth over 32 (red) and the distance to the nearest square with no
	 * water at this height, within three squares, over 16 (green); the
	 * depth is the mean of the squares that meet at the vertex, so the
	 * colour runs on across a square's edge. 16-bit indices: a level's
	 * squares stop at 65535 vertices, and those left over draw no sheet.
	 *
	 * The lift (a row's `lift`, kWaterBank): a lifted body's grid stands
	 * level, that many units over the water's top at every point, and its
	 * depth colour is that much deeper. The free surface of still water is
	 * level: where the remaster's ground leaves the water's top inside the
	 * squares and meets their edge higher, the water stands at the lift and
	 * the bank climbs out of it. Everything the sheet's pixels decide from
	 * the frame's depth under the sheet - the clip, the waterline, the
	 * patches, the shallow water's clearness - follows the grid by itself.
	 * What the game itself places at the water's top keeps its height and
	 * the tests that match it: the game's ripples and their rings, the
	 * classic ripple sprites, the falls' feet and the art the sheets hide
	 * (all within kRingHeight or a few units of the data's top, and a lift
	 * stays within a click); the rings and the feet are drawn on the
	 * sheet's own grid, so they lie on the lifted water.
	 *
	 * The skirt. Where the land beside a lifted body lies under its surface,
	 * the level water runs on over it: a land square beside the body's
	 * squares, or beside one of its skirt squares, up to kWaterSkirtReach
	 * squares out, whose lowest classic corner stands less than the lift
	 * and kWaterSkirtMargin over the water's top (water_land: the square of
	 * the neighbour's room or of a dry room holding it) carries the same
	 * plane, the same rows and the same shader, in the range of its land
	 * room's group, with the depth test against the room's art like every
	 * square - where the remaster's ground rises through the plane the
	 * sheet is hidden pixel by pixel, and the waterline runs along that line
	 * by the rule of every bank (WaterPS, the frame's depth under the
	 * surface). Beside ANDY3's pond the remaster's ground follows the classic
	 * land within 5 units under it to 17 over it at 8 in 10 of the 152 low
	 * points of its edge (the art's ground against the classic floor, both
	 * sampled on the edge), so the classic corners stand in for it. A skirt
	 * square draws and does nothing else: it is no water square for the
	 * classic faces, the rings, the ripple sprites, the art the sheets hide,
	 * the sinks, the eye under the water or the counts; its vertex depth is
	 * the lift less the classic land under the vertex (what the sheet reads
	 * without the depth texture) where no water square meets the vertex,
	 * its shore distance 0. A flipmap twin of its land room that holds the
	 * same square gets it too, counted once (skirt). A body without a lift
	 * has none: the classic land beside a water square stands at the
	 * water's top, a click and more over it, or under it where a floor drops
	 * away, in all fifteen levels. `held` stays 0, the log's column. */
	std::vector<SkirtSquare> skirts;

	for (size_t b = 0; b < g_wbodies.size(); b++)
	{
		HDRenderWaterBody& info = g_wbodies[b].info;
		std::unordered_set<unsigned long long> seen;
		std::vector<WaterSquare> from;

		info.held = 0;
		info.skirt = 0;

		if (!sheets || info.lift <= 0)
			continue;

		for (size_t i = 0; i < g_wsquares.size(); i++)
			if (g_wsquares[i].body == (int)b && g_wsquares[i].sheet)
			{
				from.push_back(g_wsquares[i]);
				seen.insert(WaterKey(g_wsquares[i].wx, g_wsquares[i].wz, 0));
			}

		for (int reach = 0; reach < kWaterSkirtReach && !from.empty();
			reach++)
		{
			std::vector<WaterSquare> next;

			for (size_t f = 0; f < from.size(); f++)
				for (int n = 0; n < 4; n++)
				{
					const WaterSquare& s = from[f];
					const int wx = s.wx + step[n][0];
					const int wz = s.wz + step[n][1];
					int c[4];

					if (seen.count(WaterKey(wx, wz, 0)) ||
						at.count(WaterKey(wx, wz, s.wy)))
						continue;

					seen.insert(WaterKey(wx, wz, 0));

					const int r = water_land(rooms, count, s.room, wx, wz,
						s.wy, c);

					if (r < 0)
						continue;

					// The lowest corner: y counts down.
					int low = c[0];

					for (int q = 1; q < 4; q++)
						low = c[q] > low ? c[q] : low;

					if (s.wy - low >= info.lift + kWaterSkirtMargin)
						continue;

					WaterSquare k = {};

					k.room = r;
					k.wx = wx;
					k.wz = wz;
					k.wy = s.wy;
					k.open = s.open;
					k.sheet = 1;
					k.body = (int)b;
					k.sink = -1;
					k.pit = -1;
					next.push_back(k);
					info.skirt++;

					// The land room and its flipmap twins, where drawn HD.
					for (int q = -1; q < count; q++)
					{
						const int t = q < 0 ? r : q;

						if (q >= 0 && (t == r || (rooms[r].flip != t &&
							rooms[t].flip != r)))
							continue;

						const HDWaterRoom& rm = rooms[t];
						const int sx = (wx - rm.originX) >> 10;
						const int sz = (wz - rm.originZ) >> 10;

						if (t >= groups || !rm.drawn || !rm.floor || sx < 0 ||
							sz < 0 || sx >= rm.sizeX || sz >= rm.sizeZ ||
							rm.floor[sx * rm.sizeZ + sz] == -127 ||
							(rm.flags & HD_WATER_ROOM_UNDERWATER))
							continue;

						SkirtSquare land = {};

						land.s = k;
						land.s.room = t;
						land.s.sx = sx;
						land.s.sz = sz;
						land.s.localY = k.wy - rm.originY;

						for (int e = 0; e < 4; e++)
							land.land[e] = c[e];

						skirts.push_back(land);
					}
				}

			from.swap(next);
		}
	}

	std::vector<std::vector<int> > skirtOf((size_t)groups);

	for (size_t i = 0; i < skirts.size(); i++)
		skirtOf[(size_t)skirts[i].s.room].push_back((int)i);

	/* The tanks' films (see "the tank's film"): a basin body each, after
	 * the level's own bodies, so every number and count of those stays as
	 * it is. Lit as a body is, by its room's light. */
	g_film_first = (int)g_wbodies.size();
	g_film_body.assign(g_film_in.size(), -1);
	g_film_wet.assign(g_film_in.size(), 0.0f);

	for (size_t f = 0; sheets && f < g_film_in.size(); f++)
	{
		const HDWaterFilm& in = g_film_in[f];

		if (in.room < 0 || in.room >= count || in.room >= groups ||
			in.x1 <= in.x0 || in.z1 <= in.z0)
			continue;

		WaterBody b = {};
		float l[3];

		b.minX = in.x0;
		b.maxX = in.x1 - 1024;
		b.minZ = in.z0;
		b.maxZ = in.z1 - 1024;
		b.info.cls = b.info.ruleCls = HD_WATER_BASIN;
		b.info.sectors = ((in.x1 - in.x0) >> 10) * ((in.z1 - in.z0) >> 10);
		b.info.sheets = b.info.sectors;
		b.info.perimeter = 2 * (((in.x1 - in.x0) >> 10) +
			((in.z1 - in.z0) >> 10));
		b.info.surfaceY = in.y - kFilmHair;
		b.info.rooms[0] = in.room;
		b.info.roomCount = 1;
		b.info.depthMax = kFilmHair;
		b.info.sourceRoom = -1;
		b.info.tint[0] = b.info.tint[1] = b.info.tint[2] = 1.0f;
		b.info.waves = b.info.foam = b.info.bend = 1.0f;
		b.info.clip = 0;	// see "the tank's film"
		b.info.swell = 1.0f;
		b.info.crest = 1.0f;
		group_light(in.room, l);

		for (int k = 0; k < 3; k++)
		{
			const float lit = l[k] * kWaterLightGain;

			b.info.raw[k] = l[k];
			b.info.light[k] = lit < kWaterLightFloor ? kWaterLightFloor :
				lit > kWaterLightCap ? kWaterLightCap : lit;
		}

		g_film_body[f] = (int)g_wbodies.size();
		g_wbodies.push_back(b);
		g_wlooks.push_back(WaterLook());
		water_look(g_wlooks.back(), g_wbodies.back(), section, stem);

		// The rim: a channel's bank foam, its strength and its band.
		g_wlooks.back().pixel[24 + 3] = kWaterClass[HD_WATER_CHANNEL].foam;
		g_wlooks.back().pixel[28 + 0] = kWaterClass[HD_WATER_CHANNEL].band;
	}

	// The tanks' wet floors (see "the wet floor"), with the sheets alone.
	g_wet.clear();
	g_wet_models_warmed = false;

	if (sheets)
		build_wet_floors(rooms, count, stem);

	if (!sheets || !g_device || !g_water_vs || !g_water_ps)
		return;

	std::vector<HDVertex> verts;
	std::vector<unsigned short> idx;

	/* A channel's end (WaterLook::hasEnd) lies downstream, where its
	 * current leaves it: under ANDREA2's canal's +z end wall. A bank that
	 * faces the current elsewhere - the south bank of the canal's west arm,
	 * which a current in +z meets as well - is no end. So a channel's end
	 * ahead counts only where it falls within the body's last square along
	 * its current: `downstream`, the furthest any of its squares reaches
	 * along the way. */
	std::vector<float> downstream(g_wbodies.size(), -1.0e30f);

	for (const WaterSquare& s : g_wsquares)
		if (s.sheet && s.body >= 0 && s.body < (int)g_wlooks.size() &&
			g_wlooks[(size_t)s.body].hasEnd)
		{
			const WaterLook& wl = g_wlooks[(size_t)s.body];

			for (int c = 0; c < 4; c++)
			{
				const float along = (float)(s.wx + ((c & 1) ? 1024 : 0)) *
					wl.river[0] + (float)(s.wz + ((c & 2) ? 1024 : 0)) *
					wl.river[1];
				float& d = downstream[(size_t)s.body];

				d = along > d ? along : d;
			}
		}

	for (int g = 0; g < groups; g++)
		for (size_t b = 0; b < g_wbodies.size(); b++)
		{
			SheetRange range = {};

			range.start = (unsigned int)idx.size();
			range.minVertex = (unsigned int)verts.size();
			range.body = (int)b;
			range.minX = range.minZ = 0x7FFFFFFF;
			range.maxX = range.maxZ = -0x7FFFFFFF;
			range.surfaceY = g_wbodies[b].info.surfaceY;
			range.originX = g < count ? rooms[g].originX : 0;
			range.originZ = g < count ? rooms[g].originZ : 0;

			// The body's water squares in this group, then its skirt's.
			const size_t water = squaresOf[(size_t)g].size();

			for (size_t k = 0; k < water + skirtOf[(size_t)g].size(); k++)
			{
				const bool skirt = k >= water;
				const SkirtSquare* land = skirt ?
					&skirts[(size_t)skirtOf[(size_t)g][k - water]] : 0;
				const WaterSquare& s = skirt ? land->s :
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

						/* A river's end ahead: the way along its flow to the
						 * first point with no water at this height, in steps
						 * of 32 units, up to 4080 - colour1's blue over 16
						 * (RiverPS, the foam where it ends). A vertex on a
						 * bank runs along the edge between water and land,
						 * so a point counts as water when water lies a unit
						 * to either side of it. The same for a channel with
						 * a current (WaterLook::hasEnd); 4080 for every
						 * other body. */
						float ahead = 4080.0f;
						const WaterLook& wl = g_wlooks[b];

						if (wl.isRiver || wl.hasEnd)
							for (float u = 1.0f; u < 4080.0f; u += 32.0f)
							{
								bool wet = false;

								for (int side = -1; side <= 1; side += 2)
								{
									const float px = (float)wx +
										wl.river[0] * u - wl.river[1] * side;
									const float pz = (float)wz +
										wl.river[1] * u + wl.river[0] * side;

									wet = wet || at.count(WaterKey(
										(int)floorf(px), (int)floorf(pz),
										s.wy)) != 0;
								}

								if (!wet)
								{
									ahead = u;
									break;
								}
							}

						if (wl.hasEnd && ahead < 4080.0f &&
							((float)wx + wl.river[0] * ahead) * wl.river[0] +
							((float)wz + wl.river[1] * ahead) * wl.river[1] <
							downstream[b] - 1024.0f)
							ahead = 4080.0f;

						const int lift = g_wbodies[b].info.lift;
						const float deep = skirt && !touching ? (float)lift -
							water_land_over(land->land, s.wx, s.wz, wx, wz,
							s.wy) : depth + (float)lift;
						const float dq = (deep > 0.0f ? deep : 0.0f) / 32.0f +
							0.5f;
						const float sq = shore / 16.0f + 0.5f;
						const float aq = ahead / 16.0f + 0.5f;

						/* The end foam reaches 1.5 kRiverEdge up at most
						 * (RiverPS, the reach's swing): a room whose sheet
						 * lies further from the end takes no end draw. */
						range.endNear |= ahead < 2.0f * kRiverEdge ? 1 : 0;
						HDVertex v = {};

						v.x = (short)lx;
						v.y = (short)(s.localY - lift);
						v.z = (short)lz;
						v.nx = 127;
						v.nz = 127;
						v.layer = (unsigned char)g_wbodies[b].info.cls;
						v.c1r = (unsigned char)(dq > 255.0f ? 255.0f : dq);
						v.c1g = (unsigned char)(sq > 255.0f ? 255.0f : sq);
						v.c1b = (unsigned char)(aq > 255.0f ? 255.0f : aq);
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

				if (skirt)
					continue;

				g_sheet_at.insert(SheetKey(g, s.sx, s.sz));
				g_sheet_world.insert(std::make_pair(
					WaterKey(s.wx, s.wz, 0), s.wy));
				range.minX = s.wx < range.minX ? s.wx : range.minX;
				range.maxX = s.wx + 1024 > range.maxX ? s.wx + 1024 :
					range.maxX;
				range.minZ = s.wz < range.minZ ? s.wz : range.minZ;
				range.maxZ = s.wz + 1024 > range.maxZ ? s.wz + 1024 :
					range.maxZ;
			}

			range.tris = ((unsigned int)idx.size() - range.start) / 3;
			range.vertices = (unsigned int)verts.size() - range.minVertex;

			if (range.tris)
				g_sheet_ranges[(size_t)g].push_back(range);
		}

	// The level's own grid, for the log; the films' follow it.
	const size_t levelVerts = verts.size();
	const size_t levelIdx = idx.size();

	/* The films' grids, the same 5 x 5 vertices a square: in the group of
	 * the floor lasers' room and in its flipmap partner's, whichever the
	 * frame draws; the depth byte kFilmHair in 32-unit steps everywhere -
	 * 0, see "the tank's film" -, the shore the distance to the film's own
	 * edge (the floor beyond it is dry). */
	for (size_t f = 0; f < g_film_body.size(); f++)
	{
		const int b = g_film_body[f];

		if (b < 0)
			continue;

		const HDWaterFilm& in = g_film_in[f];
		const int partner = rooms[in.room].flip;

		for (int pass = 0; pass < 2; pass++)
		{
			const int g = pass ? partner : in.room;

			if (g < 0 || g >= groups || g >= count ||
				(pass && partner == in.room))
				continue;

			SheetRange range = {};
			const HDWaterRoom& rm = rooms[g];

			range.start = (unsigned int)idx.size();
			range.minVertex = (unsigned int)verts.size();
			range.body = b;
			range.minX = in.x0;
			range.maxX = in.x1;
			range.minZ = in.z0;
			range.maxZ = in.z1;
			range.surfaceY = g_wbodies[(size_t)b].info.surfaceY;
			range.originX = rm.originX;
			range.originZ = rm.originZ;

			for (int wz0 = in.z0; wz0 < in.z1; wz0 += 1024)
				for (int wx0 = in.x0; wx0 < in.x1; wx0 += 1024)
				{
					if (verts.size() + 25 > 65535)
						continue;

					const unsigned short first =
						(unsigned short)verts.size();

					for (int j = 0; j <= 4; j++)
						for (int i = 0; i <= 4; i++)
						{
							const int wx = wx0 + i * 256;
							const int wz = wz0 + j * 256;
							int edge = wx - in.x0;

							edge = in.x1 - wx < edge ? in.x1 - wx : edge;
							edge = wz - in.z0 < edge ? wz - in.z0 : edge;
							edge = in.z1 - wz < edge ? in.z1 - wz : edge;

							const float dq = (float)kFilmHair / 32.0f + 0.5f;
							const float sq = (float)edge / 16.0f + 0.5f;
							HDVertex v = {};

							v.x = (short)(wx - rm.originX);
							v.y = (short)(range.surfaceY - rm.originY);
							v.z = (short)(wz - rm.originZ);
							v.nx = 127;
							v.nz = 127;
							v.layer = (unsigned char)HD_WATER_BASIN;
							v.c1r = (unsigned char)dq;
							v.c1g = (unsigned char)(sq > 255.0f ? 255.0f : sq);
							v.c1b = 255;
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
				}

			range.tris = ((unsigned int)idx.size() - range.start) / 3;
			range.vertices = (unsigned int)verts.size() - range.minVertex;

			if (range.tris)
				g_sheet_ranges[(size_t)g].push_back(range);
		}
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
		g_sheet_world.clear();
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

	/* 8. The art over a sheet. Where a sheet draws, the remaster's own water
	 * art in that plane - its material 16 parts and the flat art its
	 * vertices' water marks name (HDRange::artTris) - is left out, so a
	 * body of water has one look and not two at once, its half under the
	 * remaster's art and its half the sheet's. A draw call is hidden when
	 * every triangle of it is such art, all of it within 32 units of the
	 * sheet's surface (the slack HDRenderGroupFlatArt allows the remaster's
	 * planes: ANDY3's water stands two and four units off the classic
	 * height, the next plane 256 away; ANDREA2 231's material 16 grid,
	 * whose triangles stand at more than one height, 7 to 9 units over it)
	 * and over a square whose sheet draws; a draw call with anything else
	 * in it is kept whole. A row's
	 * `art keep` keeps the art of its bodies (apply_water_row). The classic
	 * faces are not concerned: no restored face lies under a sheet.
	 *
	 * "Over a square" is asked of the art's own triangles: the square must
	 * be one of the call's art squares (HDRange::artCells), the squares its
	 * art triangles' boxes cover. The call's box is the cheap test first,
	 * and not the answer: a call whose art lies on two squares either side
	 * of a sheet's square has a box over that square too. JOBY5's room 4
	 * is such a call - the deck's plates on squares 3,2 and 5,2 at the
	 * water's own height, marked as water art because their classic
	 * corners border the water, one draw call, round the water of square
	 * 4,2 between them (TrgProbe -squares); hidden by its box, the deck
	 * would show two holes. A call the box reaches and no art triangle of
	 * which lies over the square stays drawn (HDRange::artSpared, the
	 * level line's art_kept). */
	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];

		if (!s.sheet || s.room >= groups ||
			g_wbodies[(size_t)s.body].info.artKeep ||
			!g_sheet_at.count(SheetKey(s.room, s.sx, s.sz)))
			continue;

		const int x0 = s.sx * 1024;
		const int z0 = s.sz * 1024;

		for (int pass = 0; pass < 3; pass++)
			for (HDRange& range : g_groups[(size_t)s.room].pass[pass])
			{
				if (range.artHidden || !range.artTris ||
					(unsigned int)range.artTris * 3 != range.count ||
					range.artY < s.wy - 32 || range.artYMax > s.wy + 32 ||
					range.artMinX >= x0 + 1024 ||
					range.artMaxX <= x0 || range.artMinZ >= z0 + 1024 ||
					range.artMaxZ <= z0)
					continue;

				if (!std::binary_search(range.artCells.begin(),
					range.artCells.end(), SectorKey(x0, z0)))
				{
					range.artSpared = true;
					continue;
				}

				range.artHidden = true;
				g_wlevel.artHidden += range.artTris;
			}

		/* Over a river the remaster's moving surface goes the same way: a
		 * draw call wholly of flat flow-material triangles (HDRange::
		 * flowTris) within the same 32 units of the surface. ANDY3's rivers
		 * carry one each in rooms 38, 41, 42 and 43, three units over the
		 * classic height (TrgProbe -parts); under the sheet it is a second
		 * surface scrolling a texture of its own through the river's. Over
		 * a square by the same rule as the art: the box first, then the
		 * call's own flow squares (HDRange::flowCells); a call the box
		 * reaches and no flow triangle of which lies over the square stays
		 * drawn and counts as kept. */
		if (g_wbodies[(size_t)s.body].info.cls != HD_WATER_RIVER)
			continue;

		for (int pass = 0; pass < 3; pass++)
			for (HDRange& range : g_groups[(size_t)s.room].pass[pass])
			{
				if (range.artHidden || !range.flowTris ||
					(unsigned int)range.flowTris * 3 != range.count ||
					range.flowY < s.wy - 32 || range.flowYMax > s.wy + 32 ||
					range.flowMinX >= x0 + 1024 ||
					range.flowMaxX <= x0 || range.flowMinZ >= z0 + 1024 ||
					range.flowMaxZ <= z0)
					continue;

				if (!std::binary_search(range.flowCells.begin(),
					range.flowCells.end(), SectorKey(x0, z0)))
				{
					range.artSpared = true;
					continue;
				}

				range.artHidden = true;
				g_wlevel.artHidden += range.flowTris;
			}
	}

	// The calls the box reached and the art squares kept, for the log.
	for (int g = 0; g < groups; g++)
		for (int pass = 0; pass < 3; pass++)
			for (const HDRange& range : g_groups[(size_t)g].pass[pass])
				if (range.artSpared && !range.artHidden)
					g_wlevel.artKept++;

	g_wlevel.built = 1;
	g_wlevel.vertices = (int)levelVerts;
	g_wlevel.triangles = (int)levelIdx / 3;
	/* What the sheets will read: the frame's copy, and the depth texture
	 * where the adapter offers it and the variant exists - until a bind
	 * refuses (HDRenderWaterDepthBind). */
	g_wlevel.copy = g_copy_allowed ? 1 : 0;
	g_wlevel.depth = g_depth_allowed && g_water_depth_ps &&
		HDRenderWaterDepthOffered() ? 1 : 0;
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

bool HDRenderWaterSheetsBuilt()
{
	return g_sheet_vb && g_sheet_ib && !g_sheet_world.empty();
}

bool HDRenderWaterUnderOn(int room)
{
	return g_wlevel.built && room >= 0 &&
		room < (int)g_wroom_body.size() && g_wroom_body[(size_t)room] >= 0;
}

int HDRenderWaterAirRoom(int room)
{
	return g_wlevel.built && room >= 0 && room < (int)g_wroom_air.size() ?
		g_wroom_air[(size_t)room] : -1;
}

void HDRenderWaterUnderProbe(const float* nine)
{
	g_under_probe_set = nine != 0;

	for (int k = 0; k < 9; k++)
		g_under_probe[k] = nine ? nine[k] : 0.0f;
}

void HDRenderWaterUnderNow(float* twelve)
{
	if (twelve)
		std::memcpy(twelve, g_under_now, sizeof(g_under_now));
}

bool HDRenderWaterSheetUnder(int x, int y, int z)
{
	if (g_sheet_world.empty())
		return false;

	const auto at = g_sheet_world.equal_range(WaterKey(x, z, 0));

	for (auto i = at.first; i != at.second; ++i)
		if (i->second - y <= kRingHeight && y - i->second <= kRingHeight)
			return true;

	return false;
}

void HDRenderWaterRipples(const HDRenderRipple* live, int count)
{
	const double now = g_motion[2];
	bool seen[HD_WATER_RIPPLE_SLOTS] = {};

	for (int i = 0; live && i < count; i++)
	{
		const HDRenderRipple& r = live[i];

		if (r.slot < 0 || r.slot >= HD_WATER_RIPPLE_SLOTS || seen[r.slot])
			continue;

		RippleSeen& s = g_ripple_seen[r.slot];
		const bool fresh = !s.alive || (!s.init && r.init) || r.size < s.size;

		if (fresh)
		{
			const int count_step = (r.flags & 2) ? 8 : 4;
			const int grow_step = (r.flags & 2) ? 2 : 4;
			const int ticks = r.init > 0 ? (r.init - 1) / count_step : 0;
			WaterRing ring;

			ring.x = (float)r.x;
			ring.y = (float)r.y;
			ring.z = (float)r.z;
			ring.born = now - ticks / 30.0;
			ring.size = (float)(r.size - grow_step * ticks > 1 ?
				r.size - grow_step * ticks : 1);
			ring.body = (r.flags & 0x10) ? 1 : 0;

			// The nearest live ring of the same kind it goes into.
			WaterRing* into = 0;
			float best = kRingMerge * kRingMerge;

			for (WaterRing& g : g_rings)
			{
				const float dx = g.x - ring.x;
				const float dz = g.z - ring.z;
				const float d2 = dx * dx + dz * dz;

				if (g.body == ring.body && d2 <= best &&
					fabsf(g.y - ring.y) <= (float)kRingHeight &&
					now - g.born <= kRingLife)
				{
					best = d2;
					into = &g;
				}
			}

			if (into && ring.born - into->born >= kRingRestart)
				*into = ring;
			else if (!into)
			{
				if (g_rings.size() >= kRingsKept)
					g_rings.erase(g_rings.begin());

				g_rings.push_back(ring);
			}
		}

		s.alive = 1;
		s.init = r.init;
		s.size = r.size;
		seen[r.slot] = true;
	}

	for (int i = 0; i < HD_WATER_RIPPLE_SLOTS; i++)
		if (!seen[i])
			g_ripple_seen[i].alive = 0;

	/* Rings past their life go, and so does every ring when the clock has
	 * gone back (a new level, a loaded game). */
	for (size_t i = g_rings.size(); i-- > 0; )
		if (now - g_rings[i].born > kRingLife || g_rings[i].born > now + 0.1)
			g_rings.erase(g_rings.begin() + (std::ptrdiff_t)i);
}

namespace
{
	/* The squares that draw a sheet under world x, z: the nearest surface
	 * to y within `above` over it and `below` under it, its body into
	 * `body`; -1e9 where none lies there. */
	float sheet_surface_near(float x, float z, float y, float above,
		float below, int* body)
	{
		float best = -1.0e9f;
		float bestGap = 1.0e9f;
		const int wx = (int)floorf(x);
		const int wz = (int)floorf(z);

		for (size_t i = 0; i < g_wsquares.size(); i++)
		{
			const WaterSquare& s = g_wsquares[i];

			if (!s.sheet || wx < s.wx || wx >= s.wx + 1024 || wz < s.wz ||
				wz >= s.wz + 1024 ||
				!g_sheet_at.count(SheetKey(s.room, s.sx, s.sz)))
				continue;

			const float gap = (float)s.wy - y;

			if (gap < -above || gap > below || fabsf(gap) >= bestGap)
				continue;

			bestGap = fabsf(gap);
			best = (float)s.wy;

			if (body)
				*body = s.body;
		}

		return best;
	}
}

void HDRenderWaterRing(float x, float z, float age, float size)
{
	ProbeRing ring = { x, z, age, size };

	g_ring_probe.push_back(ring);
}

namespace
{
	// A card's corner: view space, the page and corner, the tint, the light.
	struct CardVertex
	{
		float x, y, z;
		float u, v, cx, cz;		// TEXCOORD0
		float r, g, b, a;		// TEXCOORD1
		float light;			// TEXCOORD2
	};

	const DWORD kCardFVF = D3DFVF_XYZ | D3DFVF_TEX3 |
		D3DFVF_TEXCOORDSIZE4(0) | D3DFVF_TEXCOORDSIZE4(1) |
		D3DFVF_TEXCOORDSIZE1(2);

	/* The wet floors of this frame (HDRenderWaterWetDraw): their points in
	 * view space, their triangles and the world's axes there, kept from the
	 * first draw for the second (g_wet_layer). g_wet_verts holds the same
	 * points as the marble floors' mask reads them (HDRenderMarbleDraw):
	 * the wet in place of where the water's own mirror lies, 0 on a
	 * polished floor, which keeps its marble mirror under the water ("the
	 * polished floor under the water"). g_wet_polish: each point's floor
	 * height where it lies on a polished floor, kNotPolished elsewhere. */
	std::vector<CardVertex> g_wet_layer;
	std::vector<CardVertex> g_wet_verts;
	std::vector<unsigned short> g_wet_tris;
	std::vector<int> g_wet_polish;
	const int kNotPolished = 0x7FFFFFFF;
	float g_wet_axes[12] = {};

	/* The models' target and its INTZ depth (HDRenderWaterWetModels) for a
	 * frame of `desc`'s size, kept while the size holds. */
	bool wet_make_models(const D3DSURFACE_DESC& desc)
	{
		if (g_wet_model && (g_wet_model_desc.Width != desc.Width ||
			g_wet_model_desc.Height != desc.Height))
			release_wet_models();

		if (g_wet_model)
			return true;

		if (FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
			&g_wet_model, 0)) ||
			FAILED(g_wet_model->GetSurfaceLevel(0, &g_wet_model_surface)) ||
			FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
			D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT,
			&g_wet_model_depth, 0)) ||
			FAILED(g_wet_model_depth->GetSurfaceLevel(0,
			&g_wet_model_depth_surface)))
		{
			release_wet_models();
			return false;
		}

		g_wet_model_desc = desc;
		return true;
	}

	// The room behind the models' copy (HDRenderWaterWetRoomCopy), the same.
	bool wet_make_room(const D3DSURFACE_DESC& desc)
	{
		if (g_wet_room && (g_wet_room_desc.Width != desc.Width ||
			g_wet_room_desc.Height != desc.Height ||
			g_wet_room_desc.Format != desc.Format))
		{
			g_wet_room_surface->Release();
			g_wet_room_surface = 0;
			g_wet_room->Release();
			g_wet_room = 0;
		}

		if (g_wet_room)
			return true;

		if (FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
			D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &g_wet_room,
			0)) || FAILED(g_wet_room->GetSurfaceLevel(0, &g_wet_room_surface)))
		{
			if (g_wet_room) { g_wet_room->Release(); g_wet_room = 0; }
			return false;
		}

		g_wet_room_desc = desc;
		return true;
	}

	// The soft mirror's layer, the same.
	bool wet_make_soft(const D3DSURFACE_DESC& desc)
	{
		if (g_wet_soft && (g_wet_soft_desc.Width != desc.Width ||
			g_wet_soft_desc.Height != desc.Height))
		{
			g_wet_soft_surface->Release();
			g_wet_soft_surface = 0;
			g_wet_soft->Release();
			g_wet_soft = 0;
		}

		if (g_wet_soft)
			return true;

		if (FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
			&g_wet_soft, 0)) ||
			FAILED(g_wet_soft->GetSurfaceLevel(0, &g_wet_soft_surface)))
		{
			if (g_wet_soft) { g_wet_soft->Release(); g_wet_soft = 0; }
			return false;
		}

		g_wet_soft_desc = desc;
		return true;
	}

	/* The render target as it stands into g_wet_late, as HDRenderWaterCopy
	 * takes the opaque frame: StretchRect between two surfaces of one size
	 * and format is a plain copy. */
	bool wet_copy_late()
	{
		IDirect3DSurface9* target = 0;
		D3DSURFACE_DESC desc;

		g_wet_late_valid = false;

		if (!g_device || FAILED(g_device->GetRenderTarget(0, &target)) ||
			!target)
			return false;

		if (FAILED(target->GetDesc(&desc)) ||
			desc.MultiSampleType != D3DMULTISAMPLE_NONE)
		{
			target->Release();
			return false;
		}

		if (g_wet_late && (g_wet_late_desc.Width != desc.Width ||
			g_wet_late_desc.Height != desc.Height ||
			g_wet_late_desc.Format != desc.Format))
			release_wet_late();

		if (!g_wet_late)
		{
			if (FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
				D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
				&g_wet_late, 0)) ||
				FAILED(g_wet_late->GetSurfaceLevel(0, &g_wet_late_surface)))
			{
				release_wet_late();
				target->Release();
				return false;
			}

			g_wet_late_desc = desc;
		}

		g_wet_late_valid = SUCCEEDED(g_device->StretchRect(target, 0,
			g_wet_late_surface, 0, D3DTEXF_NONE));
		target->Release();
		return g_wet_late_valid;
	}

	// The pages by HD_CARD_SPRAY, HD_CARD_SPRAY2 and HD_CARD_DROPS.
	const int kCardPages[3] = { 3242, 3243, 3160 };

	/* A body's cards wear its foam colour (gWaterFoam, c102 of its look)
	 * times kCardLight and its fog light (gWaterZen.w, c99): the spray of a
	 * dark canal is that canal's foam, a little brighter for the light it
	 * catches in the air - 1.5 a free choice, set by eye on the round's
	 * RoomProbe pictures. White and 1 for none. */
	const float kCardLight = 1.5f;

	void card_tint(int body, float* rgb, float* light)
	{
		if (body >= 0 && body < (int)g_wlooks.size())
		{
			const WaterLook& l = g_wlooks[(size_t)body];

			rgb[0] = l.pixel[24] * kCardLight;
			rgb[1] = l.pixel[25] * kCardLight;
			rgb[2] = l.pixel[26] * kCardLight;
			*light = l.pixel[15];
			return;
		}

		rgb[0] = rgb[1] = rgb[2] = 1.0f;
		*light = 1.0f;
	}
}

bool HDRenderWaterCardsReady()
{
	if (!g_device || !g_card_vs || !g_card_ps || !g_wlevel.built)
		return false;

	for (int p = 0; p < 3; p++)
		if (!HDPageCacheTexture(kCardPages[p]))
			return false;

	return true;
}

int HDRenderWaterCardPages(int* pages, int max)
{
	int n = 0;

	for (int p = 0; p < 3 && pages && n < max; p++)
		pages[n++] = kCardPages[p];

	return n;
}

bool HDRenderWaterSplash(const HDSplashSetup* setup, float age)
{
	if (!setup || !HDRenderWaterCardsReady())
		return false;

	int body = -1;

	if (sheet_surface_near(setup->x, setup->z, setup->y, (float)kRingHeight,
		(float)kRingHeight, &body) < -1.0e8f)
		return false;

	if (g_splashes.size() >= kSplashesKept)
		g_splashes.erase(g_splashes.begin());

	LiveSplash s;

	s.setup = *setup;
	s.born = (double)g_motion[2] - age;
	s.seed = HDCardHash(++g_card_seed, 1u);
	s.body = body;
	g_splashes.push_back(s);
	return true;
}

/* A spark wears the colour of the film it rises from, else of a sheet
 * within four squares under it. */
bool HDRenderWaterSpray(float x, float y, float z, int strength, float age)
{
	if (!HDRenderWaterCardsReady())
		return false;

	LiveSpray s;

	s.x = x;
	s.y = y;
	s.z = z;
	s.strength = strength;
	s.born = (double)g_motion[2] - age;
	s.seed = HDCardHash(++g_card_seed, 2u);
	s.body = -1;

	for (size_t f = 0; f < g_film_body.size(); f++)
	{
		const HDWaterFilm& in = g_film_in[f];

		if (g_film_body[f] < 0 || x < (float)in.x0 || x >= (float)in.x1 ||
			z < (float)in.z0 || z >= (float)in.z1)
			continue;

		s.body = g_film_body[f];
		break;
	}

	if (s.body < 0)
		sheet_surface_near(x, z, y, 0.0f, 4096.0f, &s.body);

	if (g_sprays.size() >= kSprayKept)
		g_sprays.erase(g_sprays.begin());

	g_sprays.push_back(s);
	return true;
}

void HDRenderWaterFilms(const HDWaterFilm* films, int count)
{
	g_film_in.clear();

	for (int i = 0; films && i < count; i++)
		g_film_in.push_back(films[i]);
}

void HDRenderWaterFilmWet(int film, float strength)
{
	if (film >= 0 && film < (int)g_film_wet.size())
		g_film_wet[(size_t)film] = strength < 0.0f ? 0.0f :
			strength > 1.0f ? 1.0f : strength;
}

int HDRenderWaterFilmCount()
{
	return (int)g_film_in.size();
}

bool HDRenderWaterTankMesh(const float* xyz, int vertices, const int* quads,
	int count)
{
	g_tanks.clear();
	g_tank_xyz.clear();
	g_tank_quads.clear();
	g_tank_shape = HDTankShapeOf(xyz, vertices);

	if (!g_tank_shape.ok)
		return false;

	g_tank_xyz.assign(xyz, xyz + (size_t)vertices * 3);

	for (int q = 0; quads && q < count; q++)
	{
		bool fits = true;

		for (int k = 0; k < 4; k++)
			fits = fits && quads[q * 4 + k] >= 0 &&
				quads[q * 4 + k] < vertices;

		if (fits)
			g_tank_quads.insert(g_tank_quads.end(), quads + q * 4,
				quads + q * 4 + 4);
	}

	return true;
}

bool HDRenderWaterTank(int item, float x, float y, float z, int yRot,
	int flags1, float age)
{
	if (!g_tank_shape.ok || !HDRenderWaterCardsReady())
		return false;

	const double now = (double)g_motion[2];
	const int ticks = HDTankTicksOf(flags1);
	LiveTank* t = 0;

	for (LiveTank& l : g_tanks)
		if (l.item == item)
			t = &l;

	if (!t)
	{
		LiveTank l = {};

		l.item = item;
		l.seed = HDCardHash(++g_card_seed, 3u);
		l.born = now - (double)ticks / 30.0;
		g_tanks.push_back(l);
		t = &g_tanks.back();
	}

	const float turn = (float)yRot * (6.2831853f / 65536.0f);

	t->x = x;
	t->y = y;
	t->z = z;
	t->c = cosf(turn);
	t->s = sinf(turn);
	t->flags1 = flags1;

	if (age >= 0.0f)
		t->born = now - (double)age;
	else
	{
		// Within the tick the game is in: ticks / 30 .. (ticks + 1) / 30.
		const double earliest = now - (double)(ticks + 1) / 30.0;
		const double latest = now - (double)ticks / 30.0;

		t->born = t->born < earliest ? earliest : t->born > latest ?
			latest : t->born;
	}

	t->seen = true;
	t->last = now;
	return true;
}

void HDRenderWaterWetTanks(const HDWaterWetTank* tanks, int count)
{
	g_wet_in.assign(tanks && count > 0 ? tanks : 0,
		tanks && count > 0 ? tanks + count : 0);
}

void HDRenderWaterWet(int item, int state, int flags1, float age)
{
	const double now = (double)g_motion[2];

	for (WetFloor& w : g_wet)
	{
		if (w.info.item != item)
			continue;

		// The clock went back: a new level or a loaded game.
		if (w.started && w.born > now + 0.1)
			w.started = false;

		w.state = state > 0 ? state : 0;

		if (state <= 0)
			w.started = false;
		else if (age >= 0.0f)
		{
			w.born = now - (double)age;
			w.started = true;
		}
		else if (!w.started)
		{
			// Pouring: its own ticks. Done when first seen: long done.
			w.born = state == 1 ?
				now - (double)HDTankTicksOf(flags1) / 30.0 : now - 1.0e6;
			w.started = true;
		}
	}
}

void HDRenderWaterWetFixed(float strength)
{
	g_wet_fixed = strength;
}

void HDRenderWaterWetLimited(int on)
{
	g_wet_limited = on != 0;
}

void HDRenderWaterWetBlur(float strength, float whole)
{
	g_wet_blur = strength < 0.0f ? kWetBlur : strength;
	g_wet_blur_near = whole < 0.0f ? kWetBlurNear : whole;
}

void HDRenderWaterWetMarch(int map, const float* trial)
{
	g_wet_debug = map > 0 ? map : 0;

	for (int i = 0; i < 3; i++)
		g_wet_try[i] = trial ? trial[i] : 0.0f;
}

bool HDRenderWaterWetSquare(int floor, int at, HDWaterWetSquare* out)
{
	if (!out || floor < 0 || floor >= (int)g_wet.size() || at < 0 ||
		at >= (int)g_wet[(size_t)floor].squares.size())
		return false;

	const WetFloor& w = g_wet[(size_t)floor];
	const WetSquare& s = w.squares[(size_t)at];

	out->item = w.info.item;
	out->x = s.x;
	out->z = s.z;
	out->y = s.y;
	out->room = s.room;
	out->way = s.way;
	out->kept = s.kept ? 1 : 0;
	out->under = s.under;
	out->mirrored = s.mirrored;
	out->copied = s.copied ? 1 : 0;
	out->slope = s.slope;
	out->clear = s.clear ? 1 : 0;
	out->reach = w.info.reach;
	out->limit = w.limit;
	return true;
}

/* SPLASHLOG, a tank's wet floor, once at the level's load: the tank, the
 * squares its water reaches, the rooms whose floor they are (the first
 * sixteen: every room whose floor data holds such a square at its height,
 * so a room whose ring of walls and doors or whose flip twin lies on it
 * is named too, though the square is the tank's rooms'), how many floor
 * heights among them (a sloping square's its highest corner's), the
 * points and triangles of
 * its grid, the dome's foot from the glass and the furthest point along
 * the water's way (units, the ragged push included); then the squares the
 * run found in its rooms before the radius dropped the far ones, the
 * furthest along the way the front runs (the foot and kWetRadius), and the
 * kept squares' 256-unit cells on a polished floor (wet_polished; none
 * while the marble floors are off). */
bool HDRenderWaterWetLine(int at, const char* stem, char* out, size_t size)
{
	if (!out || !size || at < 0 || at >= (int)g_wet.size())
		return false;

	const WetInfo& w = g_wet[(size_t)at].info;
	char rooms[128] = "";

	for (int k = 0; k < 16 && w.rooms[k] >= 0; k++)
	{
		const size_t used = strlen(rooms);

		snprintf(rooms + used, sizeof(rooms) - used, k ? ",%d" : "%d",
			w.rooms[k]);
	}

	snprintf(out, size, "SPLASHLOG level=%s wet tank=%d squares=%d rooms=%s "
		"heights=%d points=%d tris=%d reach=%.0f far=%.0f found=%d "
		"limit=%.0f polished=%d", stem ? stem : "?", w.item, w.squares, rooms,
		w.heights, w.vertices, w.triangles, w.reach, w.furthest, w.found,
		g_wet[(size_t)at].limit, w.polished);
	return true;
}

namespace
{
	// Wall-clock milliseconds, and the device's queue drained to its end.
	double warm_now_ms()
	{
		LARGE_INTEGER f, c;

		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&c);
		return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
	}

	void warm_drain()
	{
		IDirect3DQuery9* q = 0;

		if (FAILED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &q)) || !q)
			return;

		q->Issue(D3DISSUE_END);

		while (q->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE) {}

		q->Release();
	}
}

/* The model path's shaders (hd_model.cpp), here beside their one use: the
 * wet floor's warm-up makes them once more out of the same bytecode. */
#include "hd_shader/hd_model_vs.inc"
#include "hd_shader/hd_model_ps.inc"

/* The models' shaders drawn once through five user clip planes that cut
 * everything away, into the warm-up's target (see the wet floor's part of
 * HDRenderWaterWarmUp): made out of the model path's bytecode, with its
 * vertex declaration - a place, a normal, three bones and their weights,
 * 24 bytes - and released again; the device's planes put back. False when
 * a shader could not be made. */
static bool wet_warm_model_shaders()
{
	static const D3DVERTEXELEMENT9 elements[] =
	{
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT,
			D3DDECLUSAGE_POSITION, 0 },
		{ 0, 12, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT,
			D3DDECLUSAGE_NORMAL, 0 },
		{ 0, 16, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT,
			D3DDECLUSAGE_BLENDINDICES, 0 },
		{ 0, 20, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT,
			D3DDECLUSAGE_BLENDWEIGHT, 0 },
		D3DDECL_END()
	};
	// w >= 0 for every point before the eye: each plane cuts all away.
	const float none[4] = { 0.0f, 0.0f, 0.0f, -1.0f };
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	const float triangle[18] = {};
	float planes[5][4];
	DWORD clip = 0;
	bool drawn = false;

	g_device->GetRenderState(D3DRS_CLIPPLANEENABLE, &clip);

	for (int k = 0; k < 5; k++)
	{
		g_device->GetClipPlane((DWORD)k, planes[k]);
		g_device->SetClipPlane((DWORD)k, none);
	}

	if (SUCCEEDED(g_device->CreateVertexDeclaration(elements, &decl)) &&
		SUCCEEDED(g_device->CreateVertexShader((const DWORD*)kHDModelVS,
		&vs)) && SUCCEEDED(g_device->CreatePixelShader(
		(const DWORD*)kHDModelPS, &ps)))
	{
		g_device->SetVertexDeclaration(decl);
		g_device->SetVertexShader(vs);
		g_device->SetPixelShader(ps);
		g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0x1F);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, 24);
		warm_drain();
		drawn = true;
	}

	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, clip);

	for (int k = 0; k < 5; k++)
		g_device->SetClipPlane((DWORD)k, planes[k]);

	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
	return drawn;
}

/* The warm-up.
 *
 * Without it the first draw of a water sheet in a process holds its frame
 * for about 42 ms: RoomProbe with a scratch timer round each sheet draw,
 * to the GPU's end, measured 41.9, 42.7 and 43.9 ms for the first, 0.35 to 0.49
 * for the second and 0.10 for the third (RICH2's tank view, 2560 x 1440),
 * with the draw call itself under a third of a millisecond. The wait is
 * the driver readying the water's vertex shader the first time a draw uses
 * it: drawn first with the rooms' vertex shader, the sheet's pixel shaders
 * took 0.4 to 0.7 ms (WaterDepthPS) and 0.05 to 0.16 (WaterPS), then the
 * first draw with WaterVS 60 to 73 ms on a busy machine; the sheet's
 * buffers' upload (PreLoad) 0.4 to 0.9 ms; the cards' pair 2.4 to 4.4.
 *
 * So once a level, when its sheets are built and the depth texture is
 * bound, each of the water's pixel shaders draws one triangle of the
 * level's first sheet range with WaterVS - its declaration and textures
 * bound as the frame binds them - and the cards' pair one card, into a
 * render target of its own of 16 x 16 in the frame's format and without
 * a depth buffer; the queue is drained after each, the device's state put
 * back. Nothing of it reaches the frame. `ms` takes the time of each
 * (sheet, depth, under, foot, river, foam, foam depth, cards, the wet
 * floor's targets and pixel shader, the models' shaders through the wet
 * floor's planes; -1 for one not drawn). Returns the whole time, or -1
 * when nothing was warmed (no sheets, no device). */
double HDRenderWaterWarmUp(double* ms, int max)
{
	for (int i = 0; ms && i < max; i++)
		ms[i] = -1.0;

	const SheetRange* range = 0;

	for (size_t g = 0; g < g_sheet_ranges.size() && !range; g++)
		if (!g_sheet_ranges[g].empty())
			range = &g_sheet_ranges[g][0];

	if (!g_device || !range || !g_sheet_vb || !g_sheet_ib || !g_water_vs ||
		!g_decl)
		return -1.0;

	const double start = warm_now_ms();
	IDirect3DSurface9* target = 0;
	IDirect3DSurface9* depth = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return -1.0;

	g_device->GetDepthStencilSurface(&depth);

	// The target, and a second texture in the frame copy's place.
	IDirect3DTexture9* warm = 0;
	IDirect3DTexture9* behind = 0;
	IDirect3DSurface9* warmSurface = 0;

	if (FAILED(target->GetDesc(&desc)) ||
		FAILED(g_device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET,
		desc.Format, D3DPOOL_DEFAULT, &warm, 0)) ||
		FAILED(g_device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET,
		desc.Format, D3DPOOL_DEFAULT, &behind, 0)) ||
		FAILED(warm->GetSurfaceLevel(0, &warmSurface)))
	{
		if (warm) warm->Release();
		if (behind) behind->Release();
		if (depth) depth->Release();
		target->Release();
		return -1.0;
	}

	// The device's state as the level left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex[3] = { 0, 0, 0 };
	const DWORD stage[3] = { 0, 5, 6 };
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* indices = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0;
	D3DVIEWPORT9 port = {};
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_CULLMODE, D3DRS_STENCILENABLE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	DWORD saved[stateCount];

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);
	g_device->GetIndices(&indices);
	g_device->GetViewport(&port);

	for (int i = 0; i < 3; i++)
		g_device->GetTexture(stage[i], &tex[i]);

	for (int i = 0; i < stateCount; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	g_device->SetRenderTarget(0, warmSurface);
	g_device->SetDepthStencilSurface(0);
	g_device->BeginScene();
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	// The water's pixel shaders over the sheet's own vertices.
	IDirect3DPixelShader9* const sheets[7] = { g_water_ps, g_water_depth_ps,
		g_water_under_ps, g_water_foot_ps, g_water_river_ps, g_water_foam_ps,
		g_water_foam_depth_ps };

	g_device->SetVertexDeclaration(g_decl);
	g_device->SetVertexShader(g_water_vs);
	g_device->SetStreamSource(0, g_sheet_vb, 0, sizeof(HDVertex));
	g_device->SetIndices(g_sheet_ib);
	g_device->SetTexture(5, g_copy_texture ? g_copy_texture : behind);
	g_device->SetTexture(6, g_depth_texture);
	warm_drain();

	for (int i = 0; i < 7; i++)
	{
		if (!sheets[i])
			continue;

		const double t0 = warm_now_ms();

		g_device->SetPixelShader(sheets[i]);
		g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
			range->minVertex, range->vertices, range->start, 1);
		warm_drain();

		if (ms && i < max)
			ms[i] = warm_now_ms() - t0;
	}

	// The cards' pair with one card.
	if (g_card_vs && g_card_ps)
	{
		const double t0 = warm_now_ms();
		CardVertex card[3] = {};

		for (int k = 0; k < 3; k++)
		{
			card[k].x = (float)(k & 1);
			card[k].y = (float)(k >> 1);
			card[k].z = 10.0f;
			card[k].a = 1.0f;
			card[k].light = 1.0f;
		}

		g_device->SetTexture(6, g_depth_texture);
		g_device->SetVertexShader(g_card_vs);
		g_device->SetPixelShader(g_card_ps);
		g_device->SetFVF(kCardFVF);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, card,
			sizeof(CardVertex));
		warm_drain();

		if (ms && 7 < max)
			ms[7] = warm_now_ms() - t0;
	}

	/* A level with a tank's wet floor: the floor's targets at the frame's
	 * size - the models' target and depth, the room behind them, the
	 * later copy and the soft mirror's layer (42 MiB together at 2560 x
	 * 1440 with a 32-bit frame) -
	 * and its pixel shader drawn once with them bound as its second draw
	 * binds them, so the frame a tank first wets does not make them and
	 * wait for the driver: RoomProbe -wetagain measured that frame's two
	 * wet draws at 80 to 122 and 100 to 117 ms (three runs, 2560 x 1440),
	 * a second frame's at 0.2 to 0.4 and 0.8 to 1.0. */
	if (!g_wet.empty() && g_wet_ps && g_card_vs && wet_make_models(desc) &&
		wet_make_room(desc) && wet_make_soft(desc))
	{
		const double t0 = warm_now_ms();
		CardVertex card[3] = {};
		IDirect3DBaseTexture9* had[5] = { 0, 0, 0, 0, 0 };
		const DWORD wetStage[5] = { 1, 2, 3, 4, 7 };

		for (int i = 0; i < 5; i++)
			g_device->GetTexture(wetStage[i], &had[i]);

		/* The later copy is the frame's as it stands, taken outside the
		 * scene the warm-up opened. */
		g_device->EndScene();
		g_device->SetRenderTarget(0, target);
		wet_copy_late();
		g_device->SetRenderTarget(0, warmSurface);
		g_device->BeginScene();

		for (int k = 0; k < 3; k++)
		{
			card[k].x = (float)(k & 1);
			card[k].y = (float)(k >> 1);
			card[k].z = 10.0f;
			card[k].a = 1.0f;
			card[k].light = 1.0f;
		}

		g_device->SetTexture(0, g_wet_model);
		g_device->SetTexture(1, g_field_dir);
		g_device->SetTexture(2, g_field_rgb);
		g_device->SetTexture(3, g_wet_model_depth);
		g_device->SetTexture(4, g_wet_room);
		g_device->SetTexture(5, g_wet_late ? g_wet_late : behind);
		g_device->SetTexture(6, g_depth_texture);
		g_device->SetVertexShader(g_card_vs);
		g_device->SetPixelShader(g_wet_ps);
		g_device->SetFVF(kCardFVF);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, card,
			sizeof(CardVertex));
		warm_drain();

		for (int i = 0; i < 5; i++)
		{
			g_device->SetTexture(wetStage[i], had[i]);

			if (had[i])
				had[i]->Release();
		}

		if (ms && 8 < max)
			ms[8] = warm_now_ms() - t0;

		/* The models through the floor's mirror (HDRenderWaterWetModels)
		 * are drawn with five user clip planes, and the driver readies the
		 * models' shaders for the planes at the first such draw: 77 to 107
		 * ms of the first wet frame (RoomProbe -wetagain, 2560 x 1440). The
		 * level's own models are built after this warm-up, so the models'
		 * shaders are made here once more out of the same bytecode
		 * (hd_model_vs.inc and hd_model_ps.inc, the model path's
		 * declaration) and draw one triangle through five planes that cut
		 * it away: the driver readies the shaders by their code, and the
		 * first wet frame's models' draw then takes under 1 ms. */
		const double t1 = warm_now_ms();

		if (wet_warm_model_shaders() && ms && 9 < max)
			ms[9] = warm_now_ms() - t1;
	}

	g_device->EndScene();
	g_device->SetRenderTarget(0, target);
	g_device->SetDepthStencilSurface(depth);
	g_device->SetViewport(&port);

	for (int i = 0; i < stateCount; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int i = 0; i < 3; i++)
	{
		g_device->SetTexture(stage[i], tex[i]);

		if (tex[i])
			tex[i]->Release();
	}

	g_device->SetStreamSource(0, stream, streamOffset, streamStride);
	g_device->SetIndices(indices);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (stream) stream->Release();
	if (indices) indices->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
	if (depth) depth->Release();
	target->Release();
	warmSurface->Release();
	warm->Release();
	behind->Release();
	return warm_now_ms() - start;
}

void HDRenderSplashStatsNow(HDRenderSplashStats* out)
{
	if (out)
		*out = g_splash_stats;
}

void HDRenderWaterCardsDraw(const float* toFrame)
{
	const double now = (double)g_motion[2];

	// The splashes and sparks past their life go, all of them when the
	// clock went back (a new level, a loaded game).
	for (size_t i = g_splashes.size(); i-- > 0; )
		if (g_splashes[i].born > now + 0.1 || !HDSplashAt(
			g_splashes[i].setup, (float)(now - g_splashes[i].born)).alive)
			g_splashes.erase(g_splashes.begin() + (std::ptrdiff_t)i);

	for (size_t i = g_sprays.size(); i-- > 0; )
		if (g_sprays[i].born > now + 0.1 || now - g_sprays[i].born > 0.6)
			g_sprays.erase(g_sprays.begin() + (std::ptrdiff_t)i);

	g_splash_stats.splashes = (int)g_splashes.size();
	g_splash_stats.rings = 2 * (int)g_splashes.size();
	g_splash_stats.sprays = (int)g_sprays.size();
	g_splash_stats.cards = 0;
	g_splash_stats.tank = -1;
	g_splash_stats.wet = 0.0f;

	for (size_t f = 0; f < g_film_wet.size(); f++)
		if (g_film_wet[f] > 0.0f && g_film_body[f] >= 0)
		{
			g_splash_stats.tank = g_film_in[f].tank;
			g_splash_stats.wet = g_film_wet[f];
			break;
		}

	/* The tanks handed in for this frame, each with the body of its film
	 * for its colour; the list forgets them for the next frame, and a tank
	 * not handed in for a second goes. */
	std::vector<LiveTank> tanks;
	std::vector<int> tankBody;

	for (size_t i = g_tanks.size(); i-- > 0; )
	{
		LiveTank& t = g_tanks[i];

		if (t.seen)
		{
			int body = -1;

			for (size_t f = 0; f < g_film_in.size(); f++)
				if (g_film_in[f].tank == t.item && g_film_body[f] >= 0)
					body = g_film_body[f];

			tanks.push_back(t);
			tankBody.push_back(body);
		}

		t.seen = false;

		if (t.last > now + 0.1 || now - t.last > 1.0)
			g_tanks.erase(g_tanks.begin() + (std::ptrdiff_t)i);
	}

	g_splash_stats.tanks = (int)tanks.size();

	if (!g_device || !g_card_vs || !g_card_ps || !g_in_frame || !toFrame ||
		(g_splashes.empty() && g_sprays.empty() && tanks.empty()))
		return;

	// Every card of the frame, with the body whose colour it wears.
	std::vector<HDCard> cards;
	std::vector<int> owner;
	HDCard some[kSplashCards];

	for (const LiveSplash& s : g_splashes)
	{
		const int n = HDSplashCards(s.setup, s.seed, (float)(now - s.born),
			some, kSplashCards);

		for (int k = 0; k < n && (int)cards.size() < kCardCap; k++)
		{
			cards.push_back(some[k]);
			owner.push_back(s.body);
		}
	}

	for (const LiveSpray& s : g_sprays)
	{
		const float age = (float)(now - s.born);
		HDCard c;

		if ((int)cards.size() < kCardCap && HDTankPuff(s.x, s.y, s.z,
			s.strength, s.seed, age, &c))
		{
			cards.push_back(c);
			owner.push_back(s.body);
		}
	}

	// The tanks' bursts, out of the tank's frame into the world.
	for (size_t i = 0; i < tanks.size(); i++)
	{
		const LiveTank& t = tanks[i];
		HDCard burst[kTankBurstCards];
		const int n = HDTankBurst(g_tank_shape, t.seed,
			(float)(now - t.born), burst, kTankBurstCards);

		for (int k = 0; k < n && (int)cards.size() < kCardCap; k++)
		{
			HDCard c = burst[k];

			c.x = t.x + burst[k].x * t.c + burst[k].z * t.s;
			c.y = t.y + burst[k].y;
			c.z = t.z + burst[k].z * t.c - burst[k].x * t.s;
			cards.push_back(c);
			owner.push_back(tankBody[i]);
		}
	}

	if (cards.empty() && tanks.empty())
		return;

	/* Each card's middle into the frame's world and on into view space,
	 * where it faces the eye: the corners lie in the view's own x and y. */
	std::vector<CardVertex> verts[3];
	const float* m = toFrame;

	for (size_t i = 0; i < cards.size(); i++)
	{
		const HDCard& c = cards[i];
		const float fx = c.x * m[0] + c.y * m[4] + c.z * m[8] + m[12];
		const float fy = c.x * m[1] + c.y * m[5] + c.z * m[9] + m[13];
		const float fz = c.x * m[2] + c.y * m[6] + c.z * m[10] + m[14];
		const float vx = fx * g_view[0] + fy * g_view[4] + fz * g_view[8] +
			g_view[12];
		const float vy = fx * g_view[1] + fy * g_view[5] + fz * g_view[9] +
			g_view[13];
		const float vz = fx * g_view[2] + fy * g_view[6] + fz * g_view[10] +
			g_view[14];

		if (vz < 1.0f || c.alpha <= 0.0f || c.page < 0 || c.page > 2)
			continue;

		float tint[3], light;

		card_tint(owner[i], tint, &light);

		const float cs = cosf(c.turn);
		const float sn = sinf(c.turn);
		static const float corner[6][2] = { { -1, -1 }, { 1, -1 },
			{ 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

		for (int k = 0; k < 6; k++)
		{
			const float a = corner[k][0];
			const float b = corner[k][1];
			CardVertex v;

			v.x = vx + a * c.half * cs - b * c.tall * sn;
			v.y = vy + a * c.half * sn + b * c.tall * cs;
			v.z = vz;
			v.u = c.u + c.size * 0.5f * (a + 1.0f);
			v.v = c.v + c.size * 0.5f * (b + 1.0f);
			v.cx = a;
			v.cz = b;
			v.r = tint[0];
			v.g = tint[1];
			v.b = tint[2];
			v.a = c.alpha > 1.0f ? 1.0f : c.alpha;
			v.light = light;
			verts[c.page].push_back(v);
		}

		g_splash_stats.cards++;
	}

	/* The tanks' water: each quad of the classic mesh as two triangles,
	 * every vertex at its moment (hd_splash_math.h, HDTankDomePoint) into
	 * the world and on into view space; its share out of the angle between
	 * the eye and the surface there (the mean of its quads' normals, taken
	 * across their diagonals, so a quad's winding does not matter) and the
	 * page running down the water. Drawn before the cards, which stand in
	 * front of it. */
	std::vector<CardVertex> dome;
	const size_t meshVerts = g_tank_xyz.size() / 3;
	std::vector<float> at(meshVerts * 3), way(meshVerts), normal;

	for (size_t i = 0; i < tanks.size() && meshVerts; i++)
	{
		const LiveTank& t = tanks[i];
		const float age = (float)(now - t.born);
		const HDTankDome d = HDTankDomeAt(age);

		if (!d.alive || d.share <= 0.0f)
			continue;

		for (size_t v = 0; v < meshVerts; v++)
		{
			float p[4];

			HDTankDomePoint(g_tank_shape, d, &g_tank_xyz[v * 3], p);

			const float wx = t.x + p[0] * t.c + p[2] * t.s;
			const float wy = t.y + p[1];
			const float wz = t.z + p[2] * t.c - p[0] * t.s;
			const float fx = wx * m[0] + wy * m[4] + wz * m[8] + m[12];
			const float fy = wx * m[1] + wy * m[5] + wz * m[9] + m[13];
			const float fz = wx * m[2] + wy * m[6] + wz * m[10] + m[14];

			for (int k = 0; k < 3; k++)
				at[v * 3 + (size_t)k] = fx * g_view[k] + fy * g_view[4 + k] +
					fz * g_view[8 + k] + g_view[12 + k];

			way[v] = p[3];
		}

		normal.assign(meshVerts * 3, 0.0f);

		for (size_t q = 0; q + 3 < g_tank_quads.size(); q += 4)
		{
			const float* a = &at[(size_t)g_tank_quads[q] * 3];
			const float* b = &at[(size_t)g_tank_quads[q + 1] * 3];
			const float* c = &at[(size_t)g_tank_quads[q + 2] * 3];
			const float* e = &at[(size_t)g_tank_quads[q + 3] * 3];
			const float u[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
			const float w[3] = { e[0] - b[0], e[1] - b[1], e[2] - b[2] };
			const float n[3] = { u[1] * w[2] - u[2] * w[1],
				u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
			const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

			for (int k = 0; k < 4 && len > 0.0f; k++)
				for (int j = 0; j < 3; j++)
					normal[(size_t)g_tank_quads[q + (size_t)k] * 3 + j] +=
						n[j] / len;
		}

		float tint[3], light;

		card_tint(tankBody[i], tint, &light);

		static const int kQuadTriangles[6] = { 0, 1, 2, 0, 2, 3 };

		for (size_t q = 0; q + 3 < g_tank_quads.size(); q += 4)
			for (int k = 0; k < 6; k++)
			{
				const size_t v = (size_t)g_tank_quads[q +
					(size_t)kQuadTriangles[k]];
				const float* p = &at[v * 3];
				const float* n = &normal[v * 3];
				const float pl = sqrtf(p[0] * p[0] + p[1] * p[1] +
					p[2] * p[2]);
				const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] +
					n[2] * n[2]);
				const float cosine = pl > 0.0f && nl > 0.0f ? -(p[0] * n[0] +
					p[1] * n[1] + p[2] * n[2]) / (pl * nl) : 1.0f;
				CardVertex cv;

				cv.x = p[0];
				cv.y = p[1];
				cv.z = p[2];
				cv.u = g_tank_xyz[v * 3] / kTankDomeAcross;
				cv.v = (way[v] - kTankDomeFall * age) / kTankDomeAlong;
				cv.cx = 0.0f;
				cv.cz = 0.0f;
				cv.r = tint[0];
				cv.g = tint[1];
				cv.b = tint[2];
				cv.a = HDTankDomeShare(d, cosine);
				cv.light = light;
				dome.push_back(cv);
			}
	}

	// The device's state as the frame left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex0 = 0;
	IDirect3DBaseTexture9* tex6 = 0;
	IDirect3DVertexBuffer9* stream = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
		D3DRS_BLENDOP, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_FOGENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE
	};
	const D3DSAMPLERSTATETYPE samps[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	const int sampCount = (int)(sizeof(samps) / sizeof(samps[0]));
	DWORD saved[stateCount], saved0[sampCount], saved6[sampCount];

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetTexture(0, &tex0);
	g_device->GetTexture(6, &tex6);
	g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);

	for (int i = 0; i < stateCount; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int i = 0; i < sampCount; i++)
	{
		g_device->GetSamplerState(0, samps[i], &saved0[i]);
		g_device->GetSamplerState(6, samps[i], &saved6[i]);
	}

	D3DVIEWPORT9 vp = {};

	g_device->GetViewport(&vp);

	const bool soft = g_depth_texture != 0;
	const float screen[4] = { vp.Width ? 1.0f / (float)vp.Width : 0.0f,
		vp.Height ? 1.0f / (float)vp.Height : 0.0f, g_proj[10], -g_proj[14] };
	const float mode[4] = { soft ? 1.0f : 0.0f, 1.0f / kCardSoft, 0.0f,
		0.0f };

	g_device->SetVertexShader(g_card_vs);
	g_device->SetPixelShader(g_card_ps);
	g_device->SetFVF(kCardFVF);
	set_matrix_constant(0, g_proj);
	g_device->SetPixelShaderConstantF(92, screen, 1);
	g_device->SetPixelShaderConstantF(93, mode, 1);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	g_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
	g_device->SetSamplerState(6, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(6, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(6, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	g_device->SetSamplerState(6, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(6, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(6, D3DSAMP_SRGBTEXTURE, FALSE);
	g_device->SetTexture(6, soft ? g_depth_texture : 0);

	if (!dome.empty() && HDPageCacheTexture(kCardPages[HD_CARD_SPRAY2]))
	{
		g_device->SetTexture(0, HDPageCacheTexture(kCardPages[HD_CARD_SPRAY2]));
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(dome.size() / 3),
			&dome[0], sizeof(CardVertex));
	}

	for (int p = 0; p < 3; p++)
	{
		IDirect3DTexture9* page = HDPageCacheTexture(kCardPages[p]);

		if (!page || verts[p].empty())
			continue;

		g_device->SetTexture(0, page);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
			(UINT)(verts[p].size() / 3), &verts[p][0], sizeof(CardVertex));
	}

	for (int i = 0; i < stateCount; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int i = 0; i < sampCount; i++)
	{
		g_device->SetSamplerState(0, samps[i], saved0[i]);
		g_device->SetSamplerState(6, samps[i], saved6[i]);
	}

	g_device->SetTexture(0, tex0);
	g_device->SetTexture(6, tex6);
	g_device->SetStreamSource(0, stream, streamOffset, streamStride);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (tex0) tex0->Release();
	if (tex6) tex6->Release();
	if (stream) stream->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
}

static void wet_layer(int part);

/* The frame's wet points on a polished floor give their own mirror to the
 * marble floors' where those mirrored that floor's height this frame
 * (HDRenderMarbleDraw has drawn by now; see "the polished floor under the
 * water"): the point's share of the water's mirror - the march, the light
 * field's stand-in and the mirrored models - goes to 0, its darkening and
 * the lamps' highlight stay. Whether any drawn point still marches. */
static bool wet_give_way()
{
	HDRenderMarbleStats marble;
	bool marches = false;

	HDRenderMarbleStatsNow(&marble);

	for (size_t i = 0; i < g_wet_layer.size(); i++)
	{
		if (g_wet_polish[i] != kNotPolished && HDWetGivesWay(g_wet_polish[i],
			marble.plane_y, marble.drawn, 8, 16))
			g_wet_layer[i].a = 0.0f;
	}

	for (size_t i = 0; i < g_wet_tris.size() && !marches; i++)
		marches = g_wet_layer[g_wet_tris[i]].a > 0.0f;

	return marches;
}

/* The frame's wet floors (see "the wet floor"): every point of a shot
 * tank's floor at this moment into view space, as the cards' are, and the
 * triangles with any wet corner, kept for the frame's two draws - CardVS
 * and WetPS, the light field on s1 and s2, a copy of the frame on s5 and
 * its depth texture on s6. This first draws the darkening over the opaque
 * frame. The opaque frame's copy is taken here, the first of the frame,
 * so the sheets drawn after the floor read that same copy: a sheet over a
 * wet floor - only a tank's film, while it lasts - sees the floor under
 * it dry. Where a film lies over the floor its strength takes its share
 * off the mirror, which the second draw lays over the film. */
void HDRenderWaterWetDraw(const float* toFrame)
{
	const double now = (double)g_motion[2];

	g_splash_stats.floor = -1;
	g_splash_stats.floorWet = 0.0f;
	g_wet_ready = false;
	g_wet_models = false;
	g_wet_height_set = false;
	g_wet_layer.clear();
	g_wet_verts.clear();
	g_wet_tris.clear();
	g_wet_polish.clear();

	if (!g_device || !g_card_vs || !g_wet_ps || !g_in_frame || !toFrame ||
		g_wet.empty())
		return;

	// The world to view: toFrame, then the frame's view.
	float m[16];

	for (int r = 0; r < 4; r++)
		for (int k = 0; k < 4; k++)
			m[r * 4 + k] = toFrame[r * 4 + 0] * g_view[k] +
				toFrame[r * 4 + 1] * g_view[4 + k] +
				toFrame[r * 4 + 2] * g_view[8 + k] +
				toFrame[r * 4 + 3] * g_view[12 + k];

	/* The world's axes in view space, each row made a unit long: the light
	 * field's direction and the floor's up into view space (WetPS). */
	float* axes = g_wet_axes;

	std::memcpy(g_wet_view, m, sizeof(g_wet_view));

	// The floor heights of the points drawn wet, for the models' plane.
	std::map<int, int> heights;
	std::map<int, WetBox> boxes;

	for (int r = 0; r < 12; r++)
		axes[r] = 0.0f;

	for (int r = 0; r < 3; r++)
	{
		const float l = sqrtf(m[r * 4] * m[r * 4] +
			m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]);

		for (int k = 0; k < 3 && l > 0.0f; k++)
			axes[r * 4 + k] = m[r * 4 + k] / l;
	}

	std::vector<CardVertex>& verts = g_wet_layer;
	std::vector<unsigned short>& tris = g_wet_tris;
	std::vector<float> wet;

	/* The share of the mirror a film over a point leaves (see above). The
	 * film's box holds whole squares, x1 and z1 the first past them; a
	 * point on x1 or z1 is a corner of the film's last square and lies
	 * under it, as one on x0 or z0 does, so both edges count it in. */
	const auto open = [](const WetPoint& p)
	{
		float share = 1.0f;

		for (size_t f = 0; f < g_film_in.size() && f < g_film_wet.size();
			f++)
		{
			const HDWaterFilm& film = g_film_in[f];
			const float s = g_film_wet[f];

			if (p.x >= (float)film.x0 && p.x <= (float)film.x1 &&
				p.z >= (float)film.z0 && p.z <= (float)film.z1 &&
				fabsf(p.y - (float)film.y) <= 1024.0f)
				share *= 1.0f - (s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s);
		}

		return share;
	};

	for (const WetFloor& w : g_wet)
	{
		if (w.state <= 0 || !w.started || w.points.empty())
			continue;

		/* The front runs on the frame clock while tomb5.hd_motion is on;
		 * with it off it stands past every point, and the floor is wet all
		 * over from the frame the tank breaks: the run is motion, the wet
		 * the game's state, as the film's strength is. */
		const float run = g_motion[3] != 0.0f ?
			HDWetFront((float)(now - w.born), w.info.reach) : 1.0e9f;
		// And it stops at the radius (see "the wet floor").
		const float front = run < w.limit ? run : w.limit;
		const size_t base = verts.size();
		double sum = 0.0;

		if (base + w.points.size() > 65535)
			break;

		wet.resize(w.points.size());

		/* The sides of the view every point lies past - behind the eye,
		 * left, right, above, below; a floor wholly past one is not in the
		 * frame and is left out, its copy and its draw with it. */
		unsigned int past = 31u;

		for (size_t i = 0; i < w.points.size(); i++)
		{
			const WetPoint& p = w.points[i];
			CardVertex v;

			wet[i] = g_wet_fixed >= 0.0f ?
				g_wet_fixed * HDWetAt(w.limit, p.way) : HDWetAt(front, p.way);
			sum += wet[i];
			v.x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
			v.y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
			v.z = p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14];

			const float cx = v.x * g_proj[0] + v.z * g_proj[8];
			const float cy = v.y * g_proj[5] + v.z * g_proj[9];

			past &= (v.z < 1.0f ? 1u : 0u) | (cx < -v.z ? 2u : 0u) |
				(cx > v.z ? 4u : 0u) | (cy < -v.z ? 8u : 0u) |
				(cy > v.z ? 16u : 0u);
			v.u = v.x;
			v.v = v.y;
			v.cx = v.z;
			v.cz = wet[i];
			v.r = p.field[0];
			v.g = p.field[1];
			v.b = p.field[2];
			v.a = p.puddle * p.march * open(p);
			v.light = p.light;
			verts.push_back(v);
			g_wet_polish.push_back(p.polished > 0.0f ?
				(int)lroundf(p.y + kWetLift) : kNotPolished);

			if (wet[i] > 0.0f)
			{
				const int h = (int)lroundf(p.y + kWetLift);
				WetBox& b = boxes[h];

				if (heights[h]++ == 0)
					b = { p.x, p.z, p.x, p.z };

				b.x0 = p.x < b.x0 ? p.x : b.x0;
				b.z0 = p.z < b.z0 ? p.z : b.z0;
				b.x1 = p.x > b.x1 ? p.x : b.x1;
				b.z1 = p.z > b.z1 ? p.z : b.z1;
			}
		}

		if (g_splash_stats.floor < 0 && sum > 0.0)
		{
			g_splash_stats.floor = w.info.item;
			g_splash_stats.floorWet = (float)(sum / (double)w.points.size());
		}

		if (past)
		{
			verts.resize(base);
			g_wet_polish.resize(base);
			continue;
		}

		for (size_t i = 0; i + 2 < w.tris.size(); i += 3)
			if (wet[w.tris[i]] > 0.0f || wet[w.tris[i + 1]] > 0.0f ||
				wet[w.tris[i + 2]] > 0.0f)
				for (int j = 0; j < 3; j++)
					tris.push_back((unsigned short)(base + w.tris[i + j]));
	}

	if (tris.empty())
		return;

	for (const std::pair<const int, int>& h : heights)
		if (!g_wet_height_set || h.second > heights[g_wet_height])
		{
			g_wet_height = h.first;
			g_wet_height_set = true;
		}

	if (g_wet_height_set)
	{
		const WetBox& b = boxes[g_wet_height];

		g_wet_box[0] = b.x0;
		g_wet_box[1] = b.z0;
		g_wet_box[2] = b.x1;
		g_wet_box[3] = b.z1;
	}

	// The points as the marble floors' mask reads them (see g_wet_verts).
	g_wet_verts = g_wet_layer;

	for (size_t i = 0; i < g_wet_verts.size(); i++)
		if (g_wet_polish[i] != kNotPolished)
			g_wet_verts[i].cz = 0.0f;

	// The opaque frame, for the sheets (and the march debugger).
	HDRenderWaterCopy();
	g_wet_ready = true;
	wet_layer(0);
}

/* The models in the mirror. The march mirrors what the frame shows, and a
 * model is a thin thing in front of the room: a ray reflected past Lara's
 * feet meets her own pixels in the copy, which stand nearer than the room
 * the ray would meet behind her, and the march laid her dark colour on the
 * floor beside her (a grey-black shape the camera's angle moves) or took
 * her for a wall in the mirror of the room behind her. So the models are
 * mirrored as geometry, as a planar mirror is: the frame's object list
 * drawn once more with the floor's reflection folded into the projection
 * (the models' lighting is the frame's, worked out before the projection,
 * so the reflection shows them lit as they stand), and once as the frame
 * drew them into the target's alpha alone, the places the march goes past:
 * blended with both alpha factors zero, so every pixel a model writes
 * leaves 0 over the clear 1, whatever alpha its shader returns (a model's
 * pixel shader returns its page's alpha, a gloss mask: on her body page a
 * tenth of the texels lie under 64).
 * Both are drawn flipped left to right as well: two flips keep a model's
 * winding, so a culled model keeps its front faces. The mirror takes them
 * only on the plane they were mirrored in - the floor height most of the
 * drawn wet points stand at - under the floor and nearer than the march's
 * hit (WetPS). The mirrored draw is clipped to what stands over the wet
 * floor: above the plane, and inside the box of the drawn wet points grown
 * by one square (kWetModelReach) on every side, so a model on the outermost
 * wet square keeps what it reaches out with. A model elsewhere in the
 * frame's list - another room's, behind a wall the march cannot see when
 * its ray leaves the screen before the wall (the hit's depth stands at
 * infinity then) - is not mirrored, nor a part of a model under the floor,
 * which the reflection would turn above it. Drawn here, after the rooms'
 * passes and before the second wet draw, into the target and depth of their
 * own; the device's target, depth, viewport and states are put back. */
/* The models' mirrored draw warmed. The first draw of the objects through
 * the five user clip planes held its frame for about 107 ms (RoomProbe
 * -wetagain, two runs at 2560 x 1440: 107.1 and 107.7 ms, the same draw
 * without the planes 0.24 and 0.13): the driver readies the models' vertex
 * shader for the planes then. The level's load readies them now
 * (HDRenderWaterWarmUp, wet_warm_model_shaders); the first frame of a
 * level with a tank's wet floor still draws its objects once into the
 * models' target (made at the water's warm-up) with the same states and
 * five planes that cut everything away - nothing is written - for a
 * driver that readies each shader object apart: after the load's warm-up
 * that draw takes 0.6 to 0.7 ms (RoomProbe -wetagain, three runs). */
static void wet_warm_models(HDWetModelDraw draw, const void* projection,
	int bulbs)
{
	if (g_wet_models_warmed || g_wet.empty() || !g_wet_model_surface ||
		!g_wet_model_depth_surface)
		return;

	g_wet_models_warmed = true;

	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_FOGENABLE,
		D3DRS_SCISSORTESTENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_CLIPPLANEENABLE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	DWORD saved[stateCount];
	float planes[5][4];
	// w >= 0 for every point before the eye: each plane cuts all away.
	const float none[4] = { 0.0f, 0.0f, 0.0f, -1.0f };
	IDirect3DSurface9* target = 0;
	IDirect3DSurface9* depth = 0;
	D3DVIEWPORT9 vp = {};

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return;

	for (int i = 0; i < stateCount; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int k = 0; k < 5; k++)
	{
		g_device->GetClipPlane((DWORD)k, planes[k]);
		g_device->SetClipPlane((DWORD)k, none);
	}

	g_device->GetViewport(&vp);
	g_device->GetDepthStencilSurface(&depth);
	g_device->SetRenderTarget(0, g_wet_model_surface);
	g_device->SetDepthStencilSurface(g_wet_model_depth_surface);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE);
	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0x1F);
	draw(projection, bulbs, HD_PASS_SOLID);
	g_device->SetRenderTarget(0, target);
	g_device->SetDepthStencilSurface(depth);
	g_device->SetViewport(&vp);

	for (int k = 0; k < 5; k++)
		g_device->SetClipPlane((DWORD)k, planes[k]);

	for (int i = 0; i < stateCount; i++)
		g_device->SetRenderState(states[i], saved[i]);

	target->Release();

	if (depth)
		depth->Release();
}

void HDRenderWaterWetModels(HDWetModelDraw draw, const void* projection,
	int bulbs)
{
	g_wet_models = false;

	if (draw && projection && g_device && g_in_frame)
		wet_warm_models(draw, projection, bulbs);

	if (!draw || !projection || !g_wet_ready || !g_device || !g_in_frame ||
		g_wet_tris.empty() || !g_wet_height_set)
		return;

	// Where every drawn point gives way to the marble, nothing is mirrored.
	if (!wet_give_way())
		return;

	IDirect3DSurface9* target = 0;
	IDirect3DSurface9* depth = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return;

	if (FAILED(target->GetDesc(&desc)) ||
		desc.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		target->Release();
		return;
	}

	if (!wet_make_models(desc))
	{
		target->Release();
		return;
	}

	/* The plane in view space: the world's up (its y runs down) and a
	 * point of the floor, out of the first draw's world-to-view. */
	const float* m = g_wet_view;
	float n[3] = { -m[4], -m[5], -m[6] };
	const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

	if (l <= 0.0f)
	{
		target->Release();
		return;
	}

	const float h = (float)g_wet_height;
	const float p0[3] = { h * m[4] + m[12], h * m[5] + m[13],
		h * m[6] + m[14] };

	for (int k = 0; k < 3; k++)
		n[k] /= l;

	const float dist = n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2];

	/* The reflection in the plane, for a row vector: p - 2 (p.n - d) n,
	 * then the projection, then x turned (the second flip); and the
	 * projection alone with x turned, for the models as the frame drew
	 * them. */
	float k4[16] = {};

	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			k4[r * 4 + c] = (r == c ? 1.0f : 0.0f) - 2.0f * n[r] * n[c];

	for (int c = 0; c < 3; c++)
		k4[12 + c] = 2.0f * dist * n[c];

	k4[15] = 1.0f;

	const float* proj = (const float*)projection;
	float mirrored[16], plain[16];
	const float kWetModelReach = 1024.0f;	// units, one square

	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
		{
			float s = 0.0f;

			for (int i = 0; i < 4; i++)
				s += k4[r * 4 + i] * proj[i * 4 + c];

			mirrored[r * 4 + c] = c == 0 ? -s : s;
			plain[r * 4 + c] = c == 0 ? -proj[r * 4 + c] : proj[r * 4 + c];
		}

	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_FOGENABLE,
		D3DRS_SCISSORTESTENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_CLIPPLANEENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA,
		D3DRS_BLENDOPALPHA
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	DWORD saved[stateCount];
	D3DVIEWPORT9 vp = {};

	/* The five planes in the world (y runs down): above the floor, and
	 * the box's four sides; to view space through the first draw's
	 * world-to-view, to clip space through the mirrored projection. */
	const float world[5][4] =
	{
		{ 0.0f, -1.0f, 0.0f, h },
		{ 1.0f, 0.0f, 0.0f, kWetModelReach - g_wet_box[0] },
		{ 0.0f, 0.0f, 1.0f, kWetModelReach - g_wet_box[1] },
		{ -1.0f, 0.0f, 0.0f, g_wet_box[2] + kWetModelReach },
		{ 0.0f, 0.0f, -1.0f, g_wet_box[3] + kWetModelReach }
	};
	bool clip = true;

	for (int k = 0; k < 5; k++)
		clip = clip && HDWetPlaneTo(g_wet_view, world[k],
			g_wet_clip_view[k]) && HDWetPlaneTo(mirrored, g_wet_clip_view[k],
			g_wet_clip[k]);

	for (int i = 0; i < stateCount; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	g_device->GetViewport(&vp);
	g_device->GetDepthStencilSurface(&depth);
	g_device->SetRenderTarget(0, g_wet_model_surface);
	g_device->SetDepthStencilSurface(g_wet_model_depth_surface);
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER |
		D3DCLEAR_STENCIL, 0xFF000000, 1.0f, 0);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_ALPHA);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
	g_device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
	g_device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	draw(plain, bulbs, HD_PASS_SOLID);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->Clear(0, 0, D3DCLEAR_ZBUFFER, 0x00000000, 1.0f, 0);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE);

	if (clip)
	{
		for (int k = 0; k < 5; k++)
			g_device->SetClipPlane((DWORD)k, g_wet_clip[k]);

		g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0x1F);
		g_wet_clip_on = true;
	}

	draw(mirrored, bulbs, HD_PASS_SOLID);
	g_wet_clip_on = false;
	g_device->SetRenderTarget(0, target);
	g_device->SetDepthStencilSurface(depth);
	g_device->SetViewport(&vp);

	for (int i = 0; i < stateCount; i++)
		g_device->SetRenderState(states[i], saved[i]);

	target->Release();

	if (depth)
		depth->Release();

	g_wet_plane[0] = n[0];
	g_wet_plane[1] = n[1];
	g_wet_plane[2] = n[2];
	g_wet_plane[3] = dist;
	g_wet_models = true;
	g_wet_room_want = true;
}

void HDRenderWaterWetRoomCopy(bool force)
{
	const bool want = force || g_wet_room_want;

	g_wet_room_valid = false;
	g_wet_room_want = false;

	if (!want || !g_device || !g_in_frame)
		return;

	IDirect3DSurface9* target = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return;

	if (FAILED(target->GetDesc(&desc)) ||
		desc.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		target->Release();
		return;
	}

	if (!wet_make_room(desc))
	{
		target->Release();
		return;
	}

	g_wet_room_valid = SUCCEEDED(g_device->StretchRect(target, 0,
		g_wet_room_surface, 0, D3DTEXF_POINT));
	target->Release();
}

namespace
{
	/* The look's sixteen constants (hd_water_look.hlsl, c200 - c215):
	 * the view's axes, the projection, the eye and its surface's depth,
	 * the light's direction (the look's highlight direction, gWaterSun) and
	 * its colour (the hue of `light`, its brightest channel 1), the gains
	 * and the clock, the body's first four wave rows, the extinction
	 * (`sigma`, the frame's c134), the shafts' numbers, the waves' fade
	 * and the window rim's width; c215 is left 0, a register the shader
	 * does not declare, so that the block goes up sixteen registers wide.
	 * `knobs` in the order of HDWaterLookKnobs. */
	void water_look_constants(const WaterLook& look, const float* view,
		float persp, float centreX, float centreY, float eyeX, float eyeZ,
		float depth, const float* sigma, const float* light,
		const float* knobs, float* c)
	{
		const float* high = light;
		const float top = high[0] > high[1] ? (high[0] > high[2] ?
			high[0] : high[2]) : (high[1] > high[2] ? high[1] : high[2]);

		std::memset(c, 0, 16 * 4 * sizeof(float));

		for (int i = 0; i < 3; i++)
			for (int k = 0; k < 3; k++)
				c[i * 4 + k] = view[i * 4 + k];

		c[12] = 1.0f / persp;
		c[13] = 1.0f / persp;
		c[14] = centreX;
		c[15] = centreY;
		c[16] = eyeX;
		c[18] = eyeZ;
		c[19] = depth;
		c[20] = look.pixel[20];
		c[21] = look.pixel[21];
		c[22] = look.pixel[22];
		c[23] = knobs[HD_LOOK_POWER];
		c[24] = knobs[HD_LOOK_DISC];
		c[25] = knobs[HD_LOOK_SHAFTS];
		c[26] = knobs[HD_LOOK_UNDERSIDE];
		c[27] = g_motion[2];

		for (int k = 0; k < 3; k++)
			c[28 + k] = top > 0.0f ? high[k] / top : 1.0f;

		c[31] = knobs[HD_LOOK_REACH];
		std::memcpy(c + 32, look.rows, 16 * sizeof(float));

		for (int k = 0; k < 4; k++)
			c[48 + k] = sigma[k];

		c[52] = knobs[HD_LOOK_CELL];
		c[53] = knobs[HD_LOOK_FLOOR];
		c[54] = knobs[HD_LOOK_DEEP];
		c[55] = knobs[HD_LOOK_LENGTH];
		c[56] = knobs[HD_LOOK_WAVES];
		c[57] = knobs[HD_LOOK_RIM];
	}

	/* The look's draws of the whole picture, added to it, the shaders and
	 * the declaration already set; the render states they need are set
	 * here and are the caller's to put back. The disc and the underside lie
	 * at the surface, behind everything the frame drew: their corners stand
	 * at a depth of 0.9999, tested LESS, so they show only where the depth
	 * buffer still holds its clear or the sky's slice (HD_SKY_MINZ; the
	 * remaster's surface art is additive and writes none). The shafts lie in
	 * the water between the eye and whatever it sees and are drawn untested,
	 * their march held to the knobs' length; on the game's path they are
	 * drawn with the sky, before the rooms, so every opaque thing of the
	 * frame stands over them. Where the band leaves no clear - its poles
	 * closed (HDRenderSkyCaps) - the two draws are the look's whole
	 * picture. `gpuMs` takes the time between two GPU timestamps round
	 * them, -1 untimed. */
	bool water_look_draws(float* c, double* gpuMs)
	{
		const float quad[4][4] = {
			{ -1.0f, 1.0f, 0.9999f, 1.0f }, { 1.0f, 1.0f, 0.9999f, 1.0f },
			{ -1.0f, -1.0f, 0.9999f, 1.0f }, { 1.0f, -1.0f, 0.9999f, 1.0f }
		};
		const float shafts[4] = { 0.0f, c[25], 0.0f, c[27] };

		c[25] = 0.0f;
		g_device->SetPixelShaderConstantF(200, c, 16);
		g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
		g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
		g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);

		IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };
		const bool timed = gpuMs &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,
			&q[0])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,
			&q[3]));

		if (timed)
		{
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
		}

		bool drawn = SUCCEEDED(g_device->DrawPrimitiveUP(
			D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])));

		g_device->SetPixelShaderConstantF(206, shafts, 1);
		g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		drawn = SUCCEEDED(g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2,
			quad, sizeof(quad[0]))) && drawn;

		if (timed)
		{
			UINT64 t0 = 0, t1 = 0, freq = 0;

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			q[3]->Issue(D3DISSUE_END);

			while (q[3]->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[1]->GetData(&t0, sizeof(t0), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[2]->GetData(&t1, sizeof(t1), D3DGETDATA_FLUSH) ==
				S_FALSE) {}

			if (freq && t1 >= t0)
				*gpuMs = (double)(t1 - t0) * 1000.0 / (double)freq;
		}

		for (int i = 0; i < 4; i++)
			if (q[i])
				q[i]->Release();

		return drawn;
	}

	const D3DVERTEXELEMENT9 kLookCorner[] = {
		{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT,
			D3DDECLUSAGE_POSITION, 0 },
		D3DDECL_END()
	};

	/* The game's look shaders (HDRenderWaterLook), made on first use on the
	 * device in hand and made again, the old ones released, for a new one;
	 * shaders outlive a device reset, so nothing else has to drop them. */
	IDirect3DDevice9* g_look_device = 0;
	IDirect3DVertexShader9* g_look_vs = 0;
	IDirect3DPixelShader9* g_look_ps = 0;
	IDirect3DVertexDeclaration9* g_look_decl = 0;

	bool water_look_shaders()
	{
		if (g_look_device != g_device)
		{
			if (g_look_vs)
				g_look_vs->Release();
			if (g_look_ps)
				g_look_ps->Release();
			if (g_look_decl)
				g_look_decl->Release();

			g_look_vs = 0;
			g_look_ps = 0;
			g_look_decl = 0;
			g_look_device = g_device;
		}

		if (!g_look_vs && FAILED(g_device->CreateVertexShader(
			(const DWORD*)kHDWaterLookVS, &g_look_vs)))
			g_look_vs = 0;

		if (!g_look_ps && FAILED(g_device->CreatePixelShader(
			(const DWORD*)kHDWaterLookPS, &g_look_ps)))
			g_look_ps = 0;

		if (!g_look_decl && FAILED(g_device->CreateVertexDeclaration(
			kLookCorner, &g_look_decl)))
			g_look_decl = 0;

		return g_look_vs && g_look_ps && g_look_decl;
	}
}

/* RoomProbe's -underlookset: the sea seen from below, the study with other
 * strengths (hd_water_look.hlsl, the look HDRenderWaterLook draws on the
 * game's path). Two draws over the whole picture added to it, inside the
 * frame's bracket after everything else, with this frame's constants under
 * the water (g_under_now: the eye's depth under the body's surface, the
 * extinction) - so only a frame drawn with the eye under the water takes
 * it. The waves are the body's own first four rows, the deep look's in a
 * level that takes it. `knobs` in the order of HDWaterLookKnobs. The
 * device's whole state is held in a state block and put back. `gpuMs`
 * takes the pass's time between two GPU timestamps, -1 untimed. */
bool HDRenderWaterProbeLook(const float* view, float persp, float centreX,
	float centreY, float eyeX, float eyeZ, const float* knobs, double* gpuMs)
{
	if (gpuMs)
		*gpuMs = -1.0;

	if (!g_device || !view || !knobs || g_under_now[11] <= 0.0f)
		return false;

	const WaterLook* look = g_wdeep_on ? &g_wlook_deep :
		g_under_body >= 0 && g_under_body < (int)g_wlooks.size() ?
		&g_wlooks[(size_t)g_under_body] : g_wlooks.empty() ? 0 :
		&g_wlooks[0];

	if (!look)
		return false;

	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DStateBlock9* held = 0;

	if (FAILED(g_device->CreateVertexShader((const DWORD*)kHDWaterLookVS,
		&vs)) || FAILED(g_device->CreatePixelShader(
		(const DWORD*)kHDWaterLookPS, &ps)) ||
		FAILED(g_device->CreateVertexDeclaration(kLookCorner, &decl)) ||
		FAILED(g_device->CreateStateBlock(D3DSBT_ALL, &held)))
	{
		if (vs)
			vs->Release();
		if (ps)
			ps->Release();
		if (decl)
			decl->Release();
		return false;
	}

	float c[16 * 4];

	water_look_constants(*look, view, persp, centreX, centreY, eyeX, eyeZ,
		g_under_now[11], g_under_now, look->pixel + 16, knobs, c);
	held->Capture();
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);
	g_device->SetVertexDeclaration(decl);

	const bool drawn = water_look_draws(c, gpuMs);

	held->Apply();
	held->Release();
	decl->Release();
	ps->Release();
	vs->Release();
	return drawn;
}

/* The sea seen from below on the game's path: the look of the study above
 * at the strengths of HDWaterLookKnobs, drawn by HDFrameSky right after the
 * sky in a level that takes the deep look, with the sheets on
 * (tomb5.hd_water, which g_caustic_on holds) and the eye under the water.
 * `under` is a frame block with its under-the-water fields filled as
 * HDFrameDraw fills them (the eye's room, its world y, world up in view
 * space); the extinction and the water's scattered colour come from it
 * through water_under_constants. The look's surface is the water's top
 * over the dive (HDWaterLookDepth), not the basin the extinction measures
 * against, so that its disc, its window and its shafts stand where the
 * sea's surface is drawn; its light is the hue of the scattered colour,
 * the water's own. Drawn in the sky's place, before the rooms: the surface
 * art of the remaster (room 19 in JOBY4, additive) is added over it later,
 * and every opaque thing of the frame covers it - the shafts' light lies
 * only on open water. The band's poles are closed by its own caps
 * (HDRenderSkyCaps), with the script's layers out the only sky overhead,
 * and the look's light lies over them as over the band: nothing of the
 * frame is left at its clear. `knobs` stands in for HDWaterLookKnobs when
 * given (RoomProbe -underlookknobs). The render states, the shaders, the
 * declaration and stream 0 are put back as they were. `gpuMs` as in
 * HDRenderWaterProbeLook. */
bool HDRenderWaterLook(const HDFrameLighting* under, const float* view,
	float persp, float centreX, float centreY, float eyeX, float eyeZ,
	const float* knobs, double* gpuMs)
{
	if (gpuMs)
		*gpuMs = -1.0;

	if (!g_device || !under || !view || !g_wdeep_on || !g_caustic_on)
		return false;

	float u[16] = {};
	float own[HD_LOOK_KNOBS];

	if (!water_under_constants(under, u) || !water_look_shaders())
		return false;

	if (!knobs)
	{
		HDWaterLookKnobs(own);
		knobs = own;
	}

	const D3DRENDERSTATETYPE kept[] = {
		D3DRS_ZENABLE, D3DRS_ZFUNC, D3DRS_ZWRITEENABLE,
		D3DRS_ALPHABLENDENABLE, D3DRS_BLENDOP, D3DRS_SRCBLEND,
		D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE,
		D3DRS_SCISSORTESTENABLE, D3DRS_CULLMODE, D3DRS_FOGENABLE,
		D3DRS_COLORWRITEENABLE
	};
	const int states = (int)(sizeof(kept) / sizeof(kept[0]));
	DWORD was[sizeof(kept) / sizeof(kept[0])];
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexBuffer9* stream = 0;
	UINT offset = 0, stride = 0;

	for (int i = 0; i < states; i++)
		g_device->GetRenderState(kept[i], &was[i]);

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetStreamSource(0, &stream, &offset, &stride);

	float c[16 * 4];

	water_look_constants(g_wlook_deep, view, persp, centreX, centreY, eyeX,
		eyeZ, HDWaterLookDepth(under->underEyeY, g_wdeep_top), u, u + 4,
		knobs, c);
	g_device->SetVertexShader(g_look_vs);
	g_device->SetPixelShader(g_look_ps);
	g_device->SetVertexDeclaration(g_look_decl);

	const bool drawn = water_look_draws(c, gpuMs);

	for (int i = 0; i < states; i++)
		g_device->SetRenderState(kept[i], was[i]);

	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);
	g_device->SetVertexDeclaration(decl);
	g_device->SetStreamSource(0, stream, offset, stride);

	if (vs)
		vs->Release();
	if (ps)
		ps->Release();
	if (decl)
		decl->Release();
	if (stream)
		stream->Release();
	return drawn;
}

/* RoomProbe's stand-in on a wet floor (-wetstand): a dark box 160 across
 * and `height` high standing on the wet square under world x, z, drawn
 * with the fixed function in the frame's view and `projection` (the
 * frame's own when 0) - in the picture before the wet floor's first draw,
 * and through HDRenderWaterWetModels as a model. False where no wet square
 * lies under it. */
bool HDRenderWaterProbeStand(float x, float z, float height, float offX,
	float offZ, const float* projection, int alpha)
{
	if (!g_device)
		return false;

	bool found = false;
	float floorY = 0.0f;

	for (const WetFloor& w : g_wet)
		for (const WetSquare& s : w.squares)
			if (!found && s.kept && x >= (float)s.x &&
				x < (float)(s.x + 1024) && z >= (float)s.z &&
				z < (float)(s.z + 1024))
			{
				floorY = (float)s.y;
				found = true;
			}

	if (!found)
		return false;

	struct Corner
	{
		float x, y, z;
		D3DCOLOR c;
	};

	Corner v[8];

	for (int i = 0; i < 8; i++)
	{
		v[i].x = x - offX + ((i & 1) ? 80.0f : -80.0f);
		v[i].y = floorY - ((i & 2) ? 0.0f : height);
		v[i].z = z - offZ + ((i & 4) ? 80.0f : -80.0f);
		// Dark as Lara's suit, its head lighter, so its mirror shows which
		// way it stands.
		v[i].c = (i & 2) ? D3DCOLOR_ARGB(alpha, 30, 28, 26) :
			D3DCOLOR_ARGB(alpha, 90, 80, 70);
	}

	static const unsigned short faces[36] =
	{
		0, 1, 3, 0, 3, 2,  4, 6, 7, 4, 7, 5,  0, 4, 5, 0, 5, 1,
		2, 3, 7, 2, 7, 6,  0, 2, 6, 0, 6, 4,  1, 5, 7, 1, 7, 3
	};

	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex = 0;
	DWORD fvf = 0;
	D3DMATRIX world, view, proj;
	const D3DRENDERSTATETYPE states[] = { D3DRS_LIGHTING, D3DRS_FOGENABLE };
	const D3DTEXTURESTAGESTATETYPE stages[] =
	{
		D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1
	};
	DWORD saved[2], stage[4];
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* index = 0;
	UINT streamOffset = 0, streamStride = 0;

	g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);
	g_device->GetIndices(&index);
	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetTexture(0, &tex);
	g_device->GetTransform(D3DTS_WORLD, &world);
	g_device->GetTransform(D3DTS_VIEW, &view);
	g_device->GetTransform(D3DTS_PROJECTION, &proj);

	for (int i = 0; i < 2; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int i = 0; i < 4; i++)
		g_device->GetTextureStageState(0, stages[i], &stage[i]);

	D3DMATRIX identity = {};

	identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
	g_device->SetVertexShader(0);
	g_device->SetPixelShader(0);
	g_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	g_device->SetTexture(0, 0);
	g_device->SetTransform(D3DTS_WORLD, &identity);
	g_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX*)g_view);
	g_device->SetTransform(D3DTS_PROJECTION,
		(const D3DMATRIX*)(projection ? projection : g_proj));
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

	/* In the models' mirror the fixed function takes a clip plane in the
	 * world it draws in, not in clip space: the view-space planes through
	 * the view, for the draw, and the clip-space planes back after it. */
	if (g_wet_clip_on)
		for (int k = 0; k < 5; k++)
		{
			float ff[4];

			for (int r = 0; r < 4; r++)
				ff[r] = g_view[r * 4] * g_wet_clip_view[k][0] +
					g_view[r * 4 + 1] * g_wet_clip_view[k][1] +
					g_view[r * 4 + 2] * g_wet_clip_view[k][2] +
					g_view[r * 4 + 3] * g_wet_clip_view[k][3];

			g_device->SetClipPlane((DWORD)k, ff);
		}

	g_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 8, 12, faces,
		D3DFMT_INDEX16, v, sizeof(Corner));

	if (g_wet_clip_on)
		for (int k = 0; k < 5; k++)
			g_device->SetClipPlane((DWORD)k, g_wet_clip[k]);

	for (int i = 0; i < 2; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int i = 0; i < 4; i++)
		g_device->SetTextureStageState(0, stages[i], stage[i]);

	g_device->SetTransform(D3DTS_WORLD, &world);
	g_device->SetTransform(D3DTS_VIEW, &view);
	g_device->SetTransform(D3DTS_PROJECTION, &proj);
	g_device->SetTexture(0, tex);
	// DrawIndexedPrimitiveUP leaves stream 0 and the indices unset.
	g_device->SetStreamSource(0, stream, streamOffset, streamStride);
	g_device->SetIndices(index);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (stream) stream->Release();
	if (index) index->Release();
	if (tex) tex->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();

	return true;
}

/* ------------------------------------------------ the marble floors -- */

/* The marble floors. A polished floor mirrors the room over it: the room
 * art, the statics and the models are drawn once more through the floor's
 * plane, into a target of their own, and laid over the floor with the wet
 * floor's composition at a dry floor's numbers (hd_wet.hlsl, MarblePS).
 *
 * Which floors. The page decides - the data alone does not tell polished
 * stone (the marble measurement: the footstep material names marble in
 * three levels only, RICH2's marble is stone there) - so the sheen's page
 * table carries a mirror column (hd_gloss_pages.h, hd_gloss.txt's
 * "mirror" word). A mirror face is a triangle of a room's art, of its
 * opaque pass or of its clear floors, on such a page, flat and facing up
 * (HDMarbleFace, hd_splash_math.h), in a room that is no water room. The
 * mirror's extent is those faces, and their heights are the planes: never
 * the classic squares, whose edges cut the mirror where Core's floor runs
 * diagonally (RICH2 room 121, the wedge under the tank's wall). Built with
 * the level (the triangle loop, marble_build) while tomb5.hd_marble is on.
 *
 * The clear floors. A face of the column whose page is part-way clear -
 * RICH2's lobby and corridor, the title's floors - stands over the art's
 * own mirrored copy of the room. It is drawn opaque (draw_pass, `opaque`):
 * the planar mirror takes the copy's place, and the copy is left out of
 * the level (marble_copies); what of it the rule leaves lies hidden under
 * the opaque floor and is no mirror face (marble_build).
 *
 * The frame. Every room, stand range and static piece the frame draws is
 * noted as it is drawn (marble_note); the faces of the rooms drawn are
 * taken into view space and summed by height into planes, each by the
 * share of the screen its faces cover (HDMarbleCoverage), and the planes
 * covering the most are mirrored, at most g_marble_cap of them
 * (HDMarblePlanesPick): for each, the noted draws made again through the
 * projection folded with the plane's reflection and turned left to right
 * (two flips keep the winding), one clip plane keeping what stands over
 * the floor, the models with them (the frame's object list through the
 * same projection), into one target and its INTZ depth used again for the
 * next plane. Then over the frame: the plane's faces mark stencil bit
 * kMarbleStencilBit under the depth test, a wet floor's points of this
 * frame take it away again (the water's mirror is the wet floor's), and
 * the faces are drawn once more through MarblePS where the bit stands,
 * clearing it. Called once a frame between the rooms' glows and the wet
 * floor's models (hd_level.cpp, HDFrameDrawGlow). */
namespace
{
	/* The stencil bit of the mirror's mask: bits 1, 2, 4 and 8 are the
	 * shadow's receive, the creatures' mark and the crack's; 16 is free. */
	const DWORD kMarbleStencilBit = 0x10;
	/* The mirror's reflectance face on: the textbook dielectric's
	 * ((n - 1) / (n + 1))^2 at n = 1.5, polished stone (hd_gloss_math.h). */
	const float kMarbleF0 = 0.04f;
	/* The mirrored draw keeps what stands more than this over the floor's
	 * plane. At the plane itself the floor's own triangles lie at a distance
	 * of nothing to the clip, kept or cut by rounding along a line through
	 * each, and where kept they cover the reflection in the target - a
	 * floor that blinks from frame to frame as the eye moves. The offset is
	 * the layer's own lift, so the mirror starts where the laid faces
	 * stand; a mirror face's corners lie within it (marble_build). */
	const float kMarbleClipOver = kWetLift;
	// The clip's offset in use: kMarbleClipOver, or RoomProbe's -marbleclip.
	float g_marble_clip = kMarbleClipOver;
	/* A square's triangles under a mirror plane are the art's copy when at
	 * least this many are mirrored, and a third or more (marble_copies):
	 * RoomProbe -tankcensus's numbers, the wet floor's (kWetCopyCells). */
	const int kMarbleCopyMirrored = 8;
	/* How far a copy's corner turned about the plane may lie from the
	 * original's, in units: the plane is its faces' mean height rounded,
	 * their corners within a unit of it (marble_build). */
	const float kMarbleCopyNear = 2.0f;
	/* How far past each corner, out from its centroid, a copy's triangle
	 * must still lie under the floor (marble_copies), in units. */
	const float kMarbleCopyMargin = 64.0f;
	/* Two face heights this close, in units, are one floor (marble_build):
	 * a corner lies within a unit of its face's rounded mean, so two faces
	 * of one flat floor differ by at most two. The plane's clip, 4 units
	 * over it, still clears every corner (a unit and two). */
	const int kMarbleHeightJoin = 2;
	/* How far in to its centroid a buried face's corners and edges' middles
	 * are asked (marble_build): a face is buried only where it lies under
	 * higher faces nearly to its edge, not by a point that a shared edge's
	 * rounding could put on either side. */
	const float kMarbleBuriedIn = 0.125f;

	// The level's mirror faces and the opaque clear floors (marble_build).
	int g_marble_faces = 0;
	int g_marble_water = 0;
	int g_marble_opaque = 0;
	int g_marble_heights = 0;
	/* The farthest a kept face's corner lies from its plane, in units, and
	 * the faces left out for a corner farther than kMarbleClipOver. */
	int g_marble_spread = 0;
	int g_marble_rising = 0;
	// The copy triangles left out, and of them in water rooms (none known).
	int g_marble_copies = 0;
	int g_marble_copies_water = 0;
	// The faces left out for lying under a floor of their own room.
	int g_marble_buried = 0;
	// The faces moved to a neighbouring height of their floor (marble_build).
	int g_marble_joined = 0;

	// The target the mirrored rooms are drawn into, and its depth.
	IDirect3DTexture9* g_marble_tex = 0;
	IDirect3DSurface9* g_marble_surface = 0;
	IDirect3DTexture9* g_marble_ztex = 0;
	IDirect3DSurface9* g_marble_zsurface = 0;
	D3DSURFACE_DESC g_marble_desc = {};

	HDRenderMarbleStats g_marble_stats = {};
	// RoomProbe's warm-up draw: the mirrored rooms alone, nothing laid.
	bool g_marble_quiet = false;
	// The mirror target's rows a frame's row (marble_make): 1 or 2.
	int g_marble_rows = 2;

	void release_marble()
	{
		IUnknown** held[4] =
		{
			(IUnknown**)&g_marble_surface, (IUnknown**)&g_marble_tex,
			(IUnknown**)&g_marble_zsurface, (IUnknown**)&g_marble_ztex
		};

		for (int i = 0; i < 4; i++)
			if (*held[i])
			{
				(*held[i])->Release();
				*held[i] = 0;
			}
	}

	/* The target is the frame's width and g_marble_rows times its height:
	 * drawn into that many rows a pixel and read bilinearly half way
	 * between them, a reflected edge is smoothed down the screen, where
	 * the grazing view stretches it (see "the marble floors' edges"). */
	bool marble_make(const D3DSURFACE_DESC& desc)
	{
		const UINT height = desc.Height * (UINT)g_marble_rows;

		if (g_marble_tex && (g_marble_desc.Width != desc.Width ||
			g_marble_desc.Height != height))
			release_marble();

		if (g_marble_tex)
			return true;

		if (FAILED(g_device->CreateTexture(desc.Width, height, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
			&g_marble_tex, 0)) ||
			FAILED(g_marble_tex->GetSurfaceLevel(0, &g_marble_surface)) ||
			FAILED(g_device->CreateTexture(desc.Width, height, 1,
			D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT, &g_marble_ztex,
			0)) || FAILED(g_marble_ztex->GetSurfaceLevel(0,
			&g_marble_zsurface)))
		{
			release_marble();
			return false;
		}

		g_marble_desc = desc;
		g_marble_desc.Height = height;
		return true;
	}

	/* After the level's build and its rooms' flags: a water room's faces
	 * leave the mirror (the tank interiors and the pool), and the clear
	 * floors of the column elsewhere are drawn opaque.
	 *
	 * A face with a corner farther than kMarbleClipOver from its plane
	 * leaves too: it rises across itself (a ramp, a step's nose, a roof's
	 * slope), stands partly over the clip and so in its own mirror, and a
	 * level floor's reflection is not its own. The faces lie in two kinds,
	 * nothing between: every corner within a unit of the plane, or the
	 * face rising 15 units and more (RICH2 561 of 42,228, ANDREA2 30,
	 * ANDREA3 4, RICH3 54, RICHCUT2 27; RoomProbe -marble 1's spread). */
	void marble_build(const HDWaterRoom* rooms, int count)
	{
		std::unordered_set<int> heights;

		g_marble_faces = 0;
		g_marble_water = 0;
		g_marble_opaque = 0;
		g_marble_spread = 0;
		g_marble_rising = 0;
		g_marble_copies = 0;
		g_marble_copies_water = 0;
		g_marble_buried = 0;

		for (int g = 0; g < g_room_groups && g < (int)g_groups.size(); g++)
		{
			HDGroup& group = g_groups[(size_t)g];
			const bool water = g < count &&
				(rooms[g].flags & HD_WATER_ROOM_UNDERWATER) != 0;

			g_marble_copies += group.mirrorCopies;
			g_marble_copies_water += water ? group.mirrorCopies : 0;

			if (water)
			{
				g_marble_water += (int)group.mirrorY.size();
				group.mirrorFaces.clear();
				group.mirrorY.clear();
			}

			size_t kept = 0;

			for (size_t f = 0; f < group.mirrorY.size(); f++)
			{
				float most = 0.0f;

				for (int c = 0; c < 3; c++)
				{
					const float d = fabsf(group.mirrorFaces[f * 9 + c * 3 + 1] -
						(float)group.mirrorY[f]);

					most = d > most ? d : most;
				}

				if (most > kMarbleClipOver)
				{
					g_marble_rising++;
					continue;
				}

				if ((int)ceilf(most) > g_marble_spread)
					g_marble_spread = (int)ceilf(most);

				for (int k = 0; k < 9; k++)
					group.mirrorFaces[kept * 9 + k] =
						group.mirrorFaces[f * 9 + k];

				group.mirrorY[kept++] = group.mirrorY[f];
			}

			group.mirrorFaces.resize(kept * 9);
			group.mirrorY.resize(kept);

			/* A face lying wholly under higher faces of its own room is no
			 * mirror: hidden under the floor over it, it would yet be a
			 * floor height of its own and take a plane of the cap. So goes
			 * what is left of the art's copy where marble_copies' square
			 * rule did not take it (a square under one panel of the
			 * ceiling). Wholly: seven points of the face - its centroid,
			 * and its corners and its edges' middles drawn kMarbleBuriedIn
			 * of the way in to it - each inside a face 16 units and more
			 * over it, seen from above. A floor running out from under a
			 * higher one shows past its edge and stays. The higher faces
			 * are asked by the 1,024-unit squares their boxes touch. */
			std::unordered_map<long long, std::vector<size_t> > over;
			const float* faces = group.mirrorFaces.data();
			const auto key = [](int sx, int sz)
			{
				return ((long long)sx << 32) ^ (long long)(unsigned int)sz;
			};

			for (size_t f = 0; f < group.mirrorY.size(); f++)
			{
				const float* p = faces + f * 9;
				const int x0 = (int)floorf(fminf(p[0], fminf(p[3], p[6])) /
					1024.0f);
				const int x1 = (int)floorf(fmaxf(p[0], fmaxf(p[3], p[6])) /
					1024.0f);
				const int z0 = (int)floorf(fminf(p[2], fminf(p[5], p[8])) /
					1024.0f);
				const int z1 = (int)floorf(fmaxf(p[2], fmaxf(p[5], p[8])) /
					1024.0f);

				for (int sx = x0; sx <= x1; sx++)
					for (int sz = z0; sz <= z1; sz++)
						over[key(sx, sz)].push_back(f);
			}

			// Whether (x, z) lies inside a face 16 units and more over f.
			const auto covered = [&](size_t f, float x, float z)
			{
				const std::vector<size_t>& above = over[key(
					(int)floorf(x / 1024.0f), (int)floorf(z / 1024.0f))];

				for (size_t k = 0; k < above.size(); k++)
				{
					const float* q = faces + above[k] * 9;

					if (group.mirrorY[above[k]] >= group.mirrorY[f] - 16)
						continue;

					// The point's side of each edge, seen from above.
					float s[3];

					for (int e = 0; e < 3; e++)
					{
						const float* a = q + e * 3;
						const float* b = q + (e + 1) % 3 * 3;

						s[e] = (b[0] - a[0]) * (z - a[2]) -
							(b[2] - a[2]) * (x - a[0]);
					}

					if ((s[0] >= 0.0f && s[1] >= 0.0f && s[2] >= 0.0f) ||
						(s[0] <= 0.0f && s[1] <= 0.0f && s[2] <= 0.0f))
						return true;
				}

				return false;
			};
			std::vector<char> under(group.mirrorY.size(), 0);

			for (size_t f = 0; f < group.mirrorY.size(); f++)
			{
				const float* p = faces + f * 9;
				const float cx = (p[0] + p[3] + p[6]) / 3.0f;
				const float cz = (p[2] + p[5] + p[8]) / 3.0f;
				bool buried = covered(f, cx, cz);

				// The corners (0 to 2) and the edges' middles (3 to 5).
				for (int c = 0; c < 6 && buried; c++)
				{
					const float* a = p + c % 3 * 3;
					const float* b = p + (c < 3 ? c : (c + 1) % 3) * 3;
					const float x = (a[0] + b[0]) / 2.0f;
					const float z = (a[2] + b[2]) / 2.0f;

					buried = covered(f, x + (cx - x) * kMarbleBuriedIn,
						z + (cz - z) * kMarbleBuriedIn);
				}

				under[f] = buried ? 1 : 0;
			}

			kept = 0;

			for (size_t f = 0; f < group.mirrorY.size(); f++)
			{
				if (under[f])
				{
					g_marble_buried++;
					continue;
				}

				for (int k = 0; k < 9; k++)
					group.mirrorFaces[kept * 9 + k] =
						group.mirrorFaces[f * 9 + k];

				group.mirrorY[kept++] = group.mirrorY[f];
			}

			group.mirrorFaces.resize(kept * 9);
			group.mirrorY.resize(kept);
			g_marble_faces += (int)group.mirrorY.size();
			heights.insert(group.mirrorY.begin(), group.mirrorY.end());

			for (HDRange& r : group.pass[1])
			{
				r.marbleOpaque = g_marble_on && !water && r.clear &&
					MirrorPage(HDPageCachePageOfSlot(r.pageSlot));
				g_marble_opaque += r.marbleOpaque ? 1 : 0;
			}
		}

		/* One floor, one height. A face's height is its corners' mean
		 * rounded, and the corners lie within a unit of it, so one floor's
		 * faces fall on neighbouring heights (RICH2's pool surround at
		 * -2816 and -2817) and the cap mirrored a part of it and left the
		 * rest. Heights within kMarbleHeightJoin of the next are one plane,
		 * at the one most faces hold. */
		std::map<int, int> faces;

		for (int g = 0; g < g_room_groups && g < (int)g_groups.size(); g++)
			for (int y : g_groups[(size_t)g].mirrorY)
				faces[y]++;

		std::map<int, int> join;
		std::vector<std::pair<int, int> > run;

		for (std::map<int, int>::const_iterator it = faces.begin();
			it != faces.end(); ++it)
		{
			if (!run.empty() && it->first - run.back().first >
				kMarbleHeightJoin)
			{
				int best = run[0].first, most = run[0].second;

				for (const std::pair<int, int>& r : run)
					if (r.second > most)
					{
						best = r.first;
						most = r.second;
					}

				for (const std::pair<int, int>& r : run)
					join[r.first] = best;

				run.clear();
			}

			run.push_back(*it);

			if (std::next(it) == faces.end())
			{
				int best = run[0].first, most = run[0].second;

				for (const std::pair<int, int>& r : run)
					if (r.second > most)
					{
						best = r.first;
						most = r.second;
					}

				for (const std::pair<int, int>& r : run)
					join[r.first] = best;
			}
		}

		heights.clear();
		g_marble_joined = 0;

		for (int g = 0; g < g_room_groups && g < (int)g_groups.size(); g++)
			for (int& y : g_groups[(size_t)g].mirrorY)
			{
				g_marble_joined += join[y] != y ? 1 : 0;
				y = join[y];
				heights.insert(y);
			}

		g_marble_heights = (int)heights.size();
	}

	/* The art's copy of a room under its polished floor. RICH2 and the
	 * title, and no other level, build a room a second time (119,319 and
	 * 18,160 triangles by the rule below), turned upside down under a
	 * part-way clear floor, so the floor shows it as a reflection; the
	 * planar mirror takes that copy's place (the clear floors of the column
	 * are drawn opaque), and the copy, hidden under them, is still drawn,
	 * made again in every mirrored draw and its own faces a floor height of
	 * their own. So while the marble floors are on it is left out of the
	 * level.
	 *
	 * A copy is told by its triangles: a triangle of the group under one of
	 * its mirror planes - its centroid 16 to 2,048 units under the plane,
	 * in a 1,024-unit square a mirror face of that plane lies over - is
	 * mirrored when its three corners turned about the plane each fall
	 * within kMarbleCopyNear units of a corner of one triangle of the group
	 * on the same page (the copy is the room turned, corner for corner); a
	 * square is a copy's when at least kMarbleCopyMirrored of the
	 * triangles under it are mirrored and they are a third or more of them
	 * (RoomProbe -tankcensus's square rule), and its mirrored triangles
	 * leave. A copy lies wholly under its floor: a triangle with a corner,
	 * pushed kMarbleCopyMargin out from its centroid, under no mirror face
	 * of the plane seen from above can show past the floor's edge or through
	 * a gap in it from a room beside (RICH2's corridor seen from room 121)
	 * and stays. And a copy lies where no room is: a triangle whose
	 * centroid stands inside a classic room's box - in its height, and
	 * within HD_OVERHANG_SLACK of it across - is art of that room (a wall
	 * of the corridor under a room above, panelled alike over and under
	 * the floor's height) and stays. One list of flags a part and pass
	 * (part * 3 + pass), a flag a triangle; empty where the group holds no
	 * copy. Returns the triangles flagged. */
	int marble_copies(const trf::remaster::Trg& trg,
		const trf::remaster::TrgGroup& source, int g,
		std::vector<std::vector<unsigned char> >& copies)
	{
		copies.clear();

		if (!g_boxes || g < 0 || g >= g_box_count)
			return 0;

		struct Tri
		{
			float x, y, z;
			unsigned int list, at;
			float p[9];
			int page;
		};

		std::vector<Tri> tris;
		// Per plane: its faces seen from above, six floats a face, by square.
		std::map<int, std::unordered_map<long long, std::vector<float> > >
			under;
		const auto square = [](float x, float z)
		{
			return ((long long)(int)floorf(x / 1024.0f) << 32) ^
				(long long)(unsigned int)(int)floorf(z / 1024.0f);
		};
		const auto cell = [](float x, float y, float z)
		{
			return ((long long)((int)floorf(x / 32.0f) & 0xFFFFF) << 40) |
				((long long)((int)floorf(y / 32.0f) & 0xFFFFF) << 20) |
				(long long)((int)floorf(z / 32.0f) & 0xFFFFF);
		};

		for (size_t pi = 0; pi < source.parts.size(); pi++)
			for (int pass = 0; pass < 3; pass++)
			{
				const trf::remaster::TrgPart& part = source.parts[pi];
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];
				const float blend = MaterialBlend(part, pass);

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned int first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					float p[9];
					float ny = 0.0f;
					int page = -1;

					for (int c = 0; c < 3; c++)
					{
						const trf::remaster::TrgVertex& v =
							trg.vertices[trg.indices[first + c]];

						p[c * 3] = (float)v.x;
						p[c * 3 + 1] = (float)v.y;
						p[c * 3 + 2] = (float)v.z;
						ny += v.normal_y();

						if (c == 0)
							page = v.layer && v.layer <= trg.textures.size() ?
								(int)trg.textures[v.layer - 1u] :
								trg.textures.empty() ? -1 :
								(int)trg.textures[0];
					}

					Tri t = { (p[0] + p[3] + p[6]) / 3.0f,
						(p[1] + p[4] + p[7]) / 3.0f,
						(p[2] + p[5] + p[8]) / 3.0f,
						(unsigned int)(pi * 3 + (size_t)pass), at / 3 };

					memcpy(t.p, p, sizeof(p));
					t.page = page;
					tris.push_back(t);

					// A mirror face, as the triangle loop takes one.
					const int slot = HDPageCacheSlotOf(page);

					if (!(pass == 0 || (pass == 1 && blend == 0.0f &&
						!g_no_clear && HDPageCacheSlotClear(slot))) ||
						!MirrorPage(page) ||
						!HDMarbleFace(p, p + 3, p + 6, ny / 3.0f))
						continue;

					const int h = (int)lroundf(t.y);
					bool level = true;

					for (int c = 0; c < 3; c++)
						level = level &&
							fabsf(p[c * 3 + 1] - (float)h) <= kMarbleClipOver;

					if (!level)
						continue;

					const float x0 = fminf(p[0], fminf(p[3], p[6]));
					const float x1 = fmaxf(p[0], fmaxf(p[3], p[6]));
					const float z0 = fminf(p[2], fminf(p[5], p[8]));
					const float z1 = fmaxf(p[2], fmaxf(p[5], p[8]));

					for (float x = floorf(x0 / 1024.0f) * 1024.0f; x < x1;
						x += 1024.0f)
						for (float z = floorf(z0 / 1024.0f) * 1024.0f; z < z1;
							z += 1024.0f)
						{
							std::vector<float>& on = under[h][square(x + 1.0f,
								z + 1.0f)];
							const float xz[6] = { p[0], p[2], p[3], p[5], p[6],
								p[8] };

							on.insert(on.end(), xz, xz + 6);
						}
				}

				copies.push_back(std::vector<unsigned char>(run.count / 3,
					0));
			}

		if (under.empty())
		{
			copies.clear();
			return 0;
		}

		std::unordered_map<long long, std::vector<size_t> > cells;

		for (size_t i = 0; i < tris.size(); i++)
			cells[cell(tris[i].x, tris[i].y, tris[i].z)].push_back(i);

		// Whether a triangle turned about h is one of the group's own.
		const auto turned = [&](const Tri& t, float h)
		{
			const float my = 2.0f * h - t.y;

			for (int d = 0; d < 27; d++)
			{
				std::unordered_map<long long, std::vector<size_t> >::
					const_iterator at = cells.find(cell(
					t.x + 32.0f * (float)(d % 3 - 1),
					my + 32.0f * (float)(d / 3 % 3 - 1),
					t.z + 32.0f * (float)(d / 9 - 1)));

				if (at == cells.end())
					continue;

				for (size_t o : at->second)
				{
					const Tri& u = tris[o];
					bool all = u.page == t.page;

					for (int c = 0; c < 3 && all; c++)
					{
						bool one = false;

						for (int k = 0; k < 3 && !one; k++)
							one = fabsf(u.p[k * 3] - t.p[c * 3]) <=
								kMarbleCopyNear &&
								fabsf(u.p[k * 3 + 1] - (2.0f * h -
								t.p[c * 3 + 1])) <= kMarbleCopyNear &&
								fabsf(u.p[k * 3 + 2] - t.p[c * 3 + 2]) <=
								kMarbleCopyNear;

						all = one;
					}

					if (all)
						return true;
				}
			}

			return false;
		};

		// Whether a point of the group stands in a classic room.
		const auto roomed = [g](const Tri& t)
		{
			const float wx = t.x + g_boxes[g].originX;
			const float wz = t.z + g_boxes[g].originZ;

			for (int r = 0; r < g_box_count; r++)
			{
				const HDRoomBox& b = g_boxes[r];
				const float x = wx - b.originX, z = wz - b.originZ;

				if (t.y >= b.mins[1] && t.y <= b.maxs[1] &&
					x >= b.mins[0] - HD_OVERHANG_SLACK &&
					x <= b.maxs[0] + HD_OVERHANG_SLACK &&
					z >= b.mins[2] - HD_OVERHANG_SLACK &&
					z <= b.maxs[2] + HD_OVERHANG_SLACK)
					return true;
			}

			return false;
		};

		// Whether a triangle, grown at its corners, lies under a plane.
		typedef std::unordered_map<long long, std::vector<float> > Faces;
		const auto covered = [&square](const Tri& t, const Faces& faces)
		{
			for (int c = 0; c < 3; c++)
			{
				float x = t.p[c * 3], z = t.p[c * 3 + 2];
				const float dx = x - t.x, dz = z - t.z;
				const float len = sqrtf(dx * dx + dz * dz);
				const float out = len > 0.0f ? kMarbleCopyMargin / len : 0.0f;

				x += dx * out;
				z += dz * out;

				const Faces::const_iterator at = faces.find(square(x, z));
				bool under = false;

				for (size_t f = 0; at != faces.end() &&
					f + 5 < at->second.size() && !under; f += 6)
				{
					const float* q = &at->second[f];
					float s[3];

					for (int e = 0; e < 3; e++)
					{
						const float* a = q + e * 2;
						const float* b = q + (e + 1) % 3 * 2;

						s[e] = (b[0] - a[0]) * (z - a[1]) -
							(b[1] - a[1]) * (x - a[0]);
					}

					under = (s[0] >= 0.0f && s[1] >= 0.0f && s[2] >= 0.0f) ||
						(s[0] <= 0.0f && s[1] <= 0.0f && s[2] <= 0.0f);
				}

				if (!under)
					return false;
			}

			return true;
		};

		// Per plane and square: the triangles under it, and the mirrored.
		struct Column
		{
			int under = 0;
			std::vector<size_t> mirrored;
		};

		std::map<std::pair<int, long long>, Column> columns;

		for (size_t i = 0; i < tris.size(); i++)
		{
			const Tri& t = tris[i];

			for (const std::pair<const int, Faces>& plane : under)
			{
				const float h = (float)plane.first;

				if (t.y <= h + 16.0f || t.y > h + 2048.0f ||
					!plane.second.count(square(t.x, t.z)))
					continue;

				Column& col = columns[std::make_pair(plane.first,
					square(t.x, t.z))];

				col.under++;

				if (turned(t, h) && !roomed(t) && covered(t, plane.second))
					col.mirrored.push_back(i);
			}
		}

		int flagged = 0;

		for (const std::pair<const std::pair<int, long long>, Column>& c :
			columns)
		{
			const int mirrored = (int)c.second.mirrored.size();

			if (mirrored < kMarbleCopyMirrored ||
				mirrored * 3 < c.second.under)
				continue;

			for (size_t i : c.second.mirrored)
			{
				unsigned char& flag = copies[tris[i].list][tris[i].at];

				flagged += flag ? 0 : 1;
				flag = 1;
			}
		}

		if (!flagged)
			copies.clear();

		return flagged;
	}

	void marble_note(int kind, int id, const D3DMATRIX* world,
		const HDRoomLighting* lighting, unsigned int passes, int window)
	{
		if (!g_marble_on || g_marble_replay || !g_marble_faces ||
			g_marble_draws.size() >= 4096)
			return;

		MarbleDraw d;

		d.kind = window == HD_WINDOW_STAND ? 1 : kind;
		d.id = id;
		// No matrix is the identity, as prepare takes it.
		memset(&d.world, 0, sizeof(d.world));
		d.world._11 = d.world._22 = d.world._33 = d.world._44 = 1.0f;

		if (world)
			d.world = *world;

		d.lighting = lighting;
		d.passes = passes;
		d.window = window;
		d.stand = d.kind == 1 ? g_stand_pick : -1;
		d.behind = d.kind == 1 && g_stand_pick_behind;

		// A frame drawn twice in one list (RoomProbe's warm-up) notes once.
		for (const MarbleDraw& o : g_marble_draws)
			if (o.kind == d.kind && o.id == d.id && o.passes == d.passes &&
				o.window == d.window && o.stand == d.stand &&
				!memcmp(&o.world, &d.world, sizeof(d.world)))
				return;

		g_marble_draws.push_back(d);
	}

	// One plane of a frame: its faces in view space and where it lies.
	struct MarbleLayer
	{
		std::vector<CardVertex> verts;
		float cover = 0.0f;
		float plane[4] = { 0.0f, 0.0f, 0.0f, 0.0f };	// view space, up
		float axes[12] = {};
		// Its cover by room, for the stats' room of the most.
		std::map<int, float> rooms;
	};

	/* The noted draws made again; the projection is the caller's. A sky
	 * group is left out: it is drawn in a depth slice of its own. */
	void marble_replay()
	{
		HDRenderScissorOff();
		g_marble_replay = true;

		for (const MarbleDraw& d : g_marble_draws)
		{
			if (d.kind != 2 && d.id >= g_room_groups)
				continue;

			if (d.kind == 2)
				HDRenderStaticPiece(d.id, &d.world, d.lighting, d.passes);
			else if (d.kind == 1)
				HDRenderRoomStand(d.id, &d.world, d.lighting, d.passes,
					d.stand, d.behind);
			else
				HDRenderRoomMask(d.id, &d.world, d.lighting, d.passes,
					d.window);
		}

		HDRenderScissorOff();
		g_marble_replay = false;
	}

	// The room path's shaders and stream, as the bracket sets them.
	void marble_room_state()
	{
		g_device->SetVertexShader(g_vs);
		g_device->SetPixelShader(g_ps);
		g_device->SetVertexDeclaration(g_decl);
		g_device->SetStreamSource(0, g_vb, 0, sizeof(HDVertex));
		g_device->SetIndices(g_ib);
		g_device->SetTexture(1, g_field_dir);
		g_device->SetTexture(2, g_field_rgb);
	}
}

/* The marble floors' warm-up. The first mirrored draw in a process waited
 * for the driver: the room shaders drawn with a clip plane on are made
 * again for it, and the mirror's target and depth are made at the frame's
 * size - RoomProbe measured that first draw at 22.6 to 26.7 ms. So once a
 * level, after its build, the target is made, each room pixel shader the
 * mirrored rooms take (plain, gloss, glow) draws one triangle of the
 * level's first opaque range with the clip plane on into it, and the
 * layer's pair (CardVS, MarblePS) one card with the mirror's two textures
 * bound, into a target of its own of 16 x 16; the queue is drained after
 * each and the device's state put back. Nothing of it reaches the frame.
 * `ms` takes the time of each (target, plain, gloss, glow, layer; -1 for
 * one not drawn). Returns the whole time, or -1 when the level has no
 * mirror face or the marble floors are off. */
double HDRenderMarbleWarmUp(double* ms, int max)
{
	for (int i = 0; ms && i < max; i++)
		ms[i] = -1.0;

	const HDRange* range = 0;

	for (size_t g = 0; g < g_groups.size() && !range; g++)
		if (!g_groups[g].pass[0].empty())
			range = &g_groups[g].pass[0][0];

	if (!g_marble_on || !g_marble_faces || !g_device || !range || !g_vb ||
		!g_ib || !g_vs || !g_ps || !g_decl || !g_card_vs || !g_marble_ps)
		return -1.0;

	const double start = warm_now_ms();
	IDirect3DSurface9* target = 0;
	IDirect3DSurface9* depth = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return -1.0;

	g_device->GetDepthStencilSurface(&depth);

	IDirect3DTexture9* warm = 0;
	IDirect3DSurface9* warmSurface = 0;

	if (FAILED(target->GetDesc(&desc)) ||
		desc.MultiSampleType != D3DMULTISAMPLE_NONE || !marble_make(desc) ||
		FAILED(g_device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET,
		desc.Format, D3DPOOL_DEFAULT, &warm, 0)) ||
		FAILED(warm->GetSurfaceLevel(0, &warmSurface)))
	{
		if (warm) warm->Release();
		if (depth) depth->Release();
		target->Release();
		return -1.0;
	}

	if (ms && 0 < max)
		ms[0] = warm_now_ms() - start;

	// The device's state as the level left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex[4] = { 0, 0, 0, 0 };
	const DWORD stage[4] = { 1, 2, 3, 5 };
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* indices = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0, clipOn = 0;
	float clip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3DVIEWPORT9 port = {};
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_CULLMODE, D3DRS_STENCILENABLE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	DWORD saved[stateCount];

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);
	g_device->GetIndices(&indices);
	g_device->GetViewport(&port);
	g_device->GetClipPlane(0, clip);
	g_device->GetRenderState(D3DRS_CLIPPLANEENABLE, &clipOn);

	for (int i = 0; i < 4; i++)
		g_device->GetTexture(stage[i], &tex[i]);

	for (int i = 0; i < stateCount; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	// The rooms as the mirror draws them: its target, the clip plane on.
	const D3DVIEWPORT9 corner = { 0, 0, 16, 16, 0.0f, 1.0f };
	const float keepAll[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	g_device->SetRenderTarget(0, g_marble_surface);
	g_device->SetDepthStencilSurface(g_marble_zsurface);
	g_device->SetViewport(&corner);
	g_device->BeginScene();
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER |
		D3DCLEAR_STENCIL, 0x00000000, 1.0f, 0);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetClipPlane(0, keepAll);
	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 1);
	marble_room_state();
	warm_drain();

	IDirect3DPixelShader9* const rooms[3] = { g_ps, g_gloss_ps, g_glow_ps };

	for (int i = 0; i < 3; i++)
	{
		if (!rooms[i])
			continue;

		const double t0 = warm_now_ms();

		g_device->SetPixelShader(rooms[i]);

		if (g_index32)
			g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
				(UINT)g_vertex_count, range->start, 1);
		else
		{
			g_device->SetStreamSource(0, g_vb,
				range->baseVertex * (UINT)sizeof(HDVertex), sizeof(HDVertex));
			g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
				range->vertexSpan, range->start, 1);
		}

		warm_drain();

		if (ms && 1 + i < max)
			ms[1 + i] = warm_now_ms() - t0;
	}

	// The layer's pair with the mirror's colour and depth bound.
	{
		const double t0 = warm_now_ms();
		CardVertex card[3] = {};

		for (int k = 0; k < 3; k++)
		{
			card[k].x = (float)(k & 1);
			card[k].y = (float)(k >> 1);
			card[k].z = 10.0f;
			card[k].a = 1.0f;
			card[k].light = 1.0f;
		}

		g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
		g_device->SetRenderTarget(0, warmSurface);
		g_device->SetDepthStencilSurface(0);
		g_device->SetViewport(&corner);
		g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		g_device->SetTexture(3, g_marble_ztex);
		g_device->SetTexture(5, g_marble_tex);
		g_device->SetVertexShader(g_card_vs);
		g_device->SetPixelShader(g_marble_ps);
		g_device->SetFVF(kCardFVF);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, card,
			sizeof(CardVertex));
		warm_drain();

		if (ms && 4 < max)
			ms[4] = warm_now_ms() - t0;
	}

	g_device->EndScene();
	g_device->SetRenderTarget(0, target);
	g_device->SetDepthStencilSurface(depth);
	g_device->SetViewport(&port);
	g_device->SetClipPlane(0, clip);
	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, clipOn);

	for (int i = 0; i < stateCount; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int i = 0; i < 4; i++)
	{
		g_device->SetTexture(stage[i], tex[i]);

		if (tex[i])
			tex[i]->Release();
	}

	g_device->SetStreamSource(0, stream, streamOffset, streamStride);
	g_device->SetIndices(indices);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (stream) stream->Release();
	if (indices) indices->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();

	warmSurface->Release();
	warm->Release();

	if (depth) depth->Release();

	target->Release();
	return warm_now_ms() - start;
}

void HDRenderMarbleSet(int on, int planes)
{
	g_marble_on = on != 0;
	g_marble_cap = planes < 1 ? 1 : planes > 8 ? 8 : planes;
}

int HDRenderMarbleOn()
{
	return g_marble_on ? 1 : 0;
}

void HDRenderMarbleQuiet(bool on)
{
	g_marble_quiet = on;
}

void HDRenderMarbleRows(int rows)
{
	g_marble_rows = rows > 1 ? 2 : 1;

	if (g_marble_tex)
		release_marble();
}

void HDRenderMarbleClip(float units)
{
	g_marble_clip = units;
}

void HDRenderMarbleKeepCopies(bool keep)
{
	g_marble_keep_copies = keep;
}

void HDRenderMarbleStatsNow(HDRenderMarbleStats* out)
{
	if (!out)
		return;

	*out = g_marble_stats;
	out->faces_level = g_marble_faces;
	out->water = g_marble_water;
	out->opaque = g_marble_opaque;
	out->heights = g_marble_heights;
	out->spread = g_marble_spread;
	out->rising = g_marble_rising;
	out->copies = g_marble_copies;
	out->copies_water = g_marble_copies_water;
	out->buried = g_marble_buried;
	out->joined = g_marble_joined;
}

void HDRenderMarbleDraw(HDWetModelDraw draw, const void* projection,
	int bulbs)
{
	g_marble_spent = true;
	g_marble_stats = HDRenderMarbleStats();

	if (!g_marble_on || !g_device || !g_in_frame || !g_card_vs ||
		!g_marble_ps || g_marble_draws.empty() || !g_marble_faces)
		return;

	IDirect3DSurface9* target = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return;

	if (FAILED(target->GetDesc(&desc)) ||
		desc.MultiSampleType != D3DMULTISAMPLE_NONE || !marble_make(desc))
	{
		target->Release();
		return;
	}

	// The faces of the rooms drawn, by height, in view space.
	std::map<int, MarbleLayer> layers;
	std::vector<char> taken(g_groups.size(), 0);

	for (const MarbleDraw& d : g_marble_draws)
	{
		if (d.kind != 0 || d.id < 0 || d.id >= g_room_groups ||
			taken[(size_t)d.id] || !(d.passes & HD_PASS_SOLID) ||
			(d.window != HD_WINDOW_ALL && d.window != HD_WINDOW_OPEN))
			continue;

		taken[(size_t)d.id] = 1;

		const HDGroup& group = g_groups[(size_t)d.id];

		if (group.mirrorY.empty())
			continue;

		float m[16], l[3];

		multiply((const float*)&d.world, g_view, m);
		group_light(d.id, l);

		const float light = l[0] > l[1] ? (l[0] > l[2] ? l[0] : l[2]) :
			(l[1] > l[2] ? l[1] : l[2]);
		const float origin[2] =
		{
			(size_t)d.id * 2 + 1 < g_group_origin.size() ?
				g_group_origin[(size_t)d.id * 2] : 0.0f,
			(size_t)d.id * 2 + 1 < g_group_origin.size() ?
				g_group_origin[(size_t)d.id * 2 + 1] : 0.0f
		};

		for (size_t f = 0; f < group.mirrorY.size(); f++)
		{
			const float* p = &group.mirrorFaces[f * 9];
			CardVertex v[3];
			float clip[12];

			for (int c = 0; c < 3; c++)
			{
				// Lifted a few units over the floor, as the wet floor is.
				const float x = p[c * 3], y = p[c * 3 + 1] - kWetLift;
				const float z = p[c * 3 + 2];
				const float world[3] = { x + origin[0], y, z + origin[1] };
				CardVertex& o = v[c];

				o.x = x * m[0] + y * m[4] + z * m[8] + m[12];
				o.y = x * m[1] + y * m[5] + z * m[9] + m[13];
				o.z = x * m[2] + y * m[6] + z * m[10] + m[14];

				for (int k = 0; k < 4; k++)
					clip[c * 4 + k] = o.x * g_proj[k] + o.y * g_proj[4 + k] +
						o.z * g_proj[8 + k] + g_proj[12 + k];

				o.u = o.x;
				o.v = o.y;
				o.cx = o.z;
				o.cz = 1.0f;

				float* field[3] = { &o.r, &o.g, &o.b };

				for (int a = 0; a < 3; a++)
				{
					const float side = (float)g_field_grid.dims[a] *
						HD_GLOSS_FIELD_CELL;

					*field[a] = side > 0.0f ?
						(world[a] - g_field_grid.origin[a]) / side : 0.0f;
				}

				o.a = 1.0f;
				o.light = light;
			}

			const float cover = HDMarbleCoverage(clip);

			if (cover <= 0.0f)
				continue;

			MarbleLayer& layer = layers[group.mirrorY[f]];

			if (layer.verts.empty())
			{
				// The world's up in view space and a point of the floor.
				const float h = (float)group.mirrorY[f];
				float n[3] = { -m[4], -m[5], -m[6] };
				const float len = sqrtf(n[0] * n[0] + n[1] * n[1] +
					n[2] * n[2]);

				for (int k = 0; k < 3 && len > 0.0f; k++)
					layer.plane[k] = n[k] / len;

				layer.plane[3] = layer.plane[0] * (h * m[4] + m[12]) +
					layer.plane[1] * (h * m[5] + m[13]) +
					layer.plane[2] * (h * m[6] + m[14]);

				for (int r = 0; r < 3; r++)
				{
					const float a = sqrtf(m[r * 4] * m[r * 4] +
						m[r * 4 + 1] * m[r * 4 + 1] +
						m[r * 4 + 2] * m[r * 4 + 2]);

					for (int k = 0; k < 3 && a > 0.0f; k++)
						layer.axes[r * 4 + k] = m[r * 4 + k] / a;
				}
			}

			layer.cover += cover;
			layer.rooms[d.id] += cover;
			layer.verts.insert(layer.verts.end(), v, v + 3);
		}
	}

	std::vector<HDMarblePlane> planes;

	for (const std::pair<const int, MarbleLayer>& l : layers)
		planes.push_back({ l.first, l.second.cover });

	const int drawn = planes.empty() ? 0 : HDMarblePlanesPick(&planes[0],
		(int)planes.size(), g_marble_cap);

	g_marble_stats.planes = (int)planes.size();
	g_marble_stats.drawn = drawn;

	for (size_t i = 0; i < planes.size() && i < 8; i++)
	{
		g_marble_stats.plane_y[i] = planes[i].y;
		g_marble_stats.plane_cover[i] = planes[i].cover;
		g_marble_stats.plane_room[i] = -1;

		float most = 0.0f;

		for (const std::pair<const int, float>& r :
			layers[planes[i].y].rooms)
			if (r.second > most)
			{
				most = r.second;
				g_marble_stats.plane_room[i] = r.first;
			}
	}

	if (!drawn)
	{
		target->Release();
		return;
	}

	D3DVIEWPORT9 vp = {};
	IDirect3DSurface9* depth = 0;
	float keepClip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	DWORD keepClipOn = 0;
	float keepProj[16];

	g_device->GetViewport(&vp);
	g_device->GetDepthStencilSurface(&depth);
	g_device->GetClipPlane(0, keepClip);
	g_device->GetRenderState(D3DRS_CLIPPLANEENABLE, &keepClipOn);
	std::memcpy(keepProj, g_proj, sizeof(keepProj));

	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
		D3DRS_BLENDOP, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF,
		D3DRS_ALPHAFUNC, D3DRS_CULLMODE, D3DRS_FOGENABLE,
		D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_COLORWRITEENABLE,
		D3DRS_STENCILENABLE, D3DRS_TWOSIDEDSTENCILMODE, D3DRS_STENCILFUNC,
		D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
		D3DRS_STENCILPASS, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	const DWORD stage[5] = { 1, 2, 3, 5, 0 };
	const D3DSAMPLERSTATETYPE samps[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW,
		D3DSAMP_SRGBTEXTURE
	};
	const int sampCount = (int)(sizeof(samps) / sizeof(samps[0]));

	for (int i = 0; i < drawn; i++)
	{
		const MarbleLayer& layer = layers[planes[(size_t)i].y];
		const float* n = layer.plane;
		const float dist = n[3];

		/* The reflection in the plane, p - 2 (p.n - d) n for a row vector,
		 * then the projection, then x turned (the second flip). */
		float k4[16] = {};

		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				k4[r * 4 + c] = (r == c ? 1.0f : 0.0f) - 2.0f * n[r] * n[c];

		for (int c = 0; c < 3; c++)
			k4[12 + c] = 2.0f * dist * n[c];

		k4[15] = 1.0f;

		float mirrored[16];

		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
			{
				float s = 0.0f;

				for (int k = 0; k < 4; k++)
					s += k4[r * 4 + k] * keepProj[k * 4 + c];

				mirrored[r * 4 + c] = c == 0 ? -s : s;
			}

		/* What stands more than kMarbleClipOver over the floor, in the eye's
		 * own view space. */
		const float over[4] = { n[0], n[1], n[2], -dist - g_marble_clip };
		float clip[4];

		if (!HDWetPlaneTo(mirrored, over, clip))
			continue;

		D3DVIEWPORT9 rows = vp;

		rows.Y *= (DWORD)g_marble_rows;
		rows.Height *= (DWORD)g_marble_rows;
		g_device->SetRenderTarget(0, g_marble_surface);
		g_device->SetDepthStencilSurface(g_marble_zsurface);
		g_device->SetViewport(&rows);
		g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER |
			D3DCLEAR_STENCIL, 0x00000000, 1.0f, 0);
		g_device->SetClipPlane(0, clip);
		g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 1);
		std::memcpy(g_proj, mirrored, sizeof(mirrored));
		multiply(g_view, g_proj, g_viewproj);
		marble_room_state();

		const int callsBefore = g_draw_calls;

		marble_replay();
		g_marble_stats.calls += g_draw_calls - callsBefore;

		if (draw && projection)
		{
			draw(mirrored, bulbs, HD_PASS_SOLID);
			marble_room_state();
			g_water_table_up = (const void*)~(size_t)0;
			apply_frame_lighting(g_marble_frame);
		}

		std::memcpy(g_proj, keepProj, sizeof(keepProj));
		multiply(g_view, g_proj, g_viewproj);
		g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, keepClipOn);
		g_device->SetClipPlane(0, keepClip);
		g_device->SetRenderTarget(0, target);
		g_device->SetDepthStencilSurface(depth);
		g_device->SetViewport(&vp);

		if (g_marble_quiet)
			continue;

		// Over the frame: the mask, the wet floor's points out, the mirror.
		IDirect3DVertexShader9* vs = 0;
		IDirect3DPixelShader9* ps = 0;
		IDirect3DVertexDeclaration9* decl = 0;
		IDirect3DBaseTexture9* tex[5] = { 0, 0, 0, 0, 0 };
		DWORD saved[stateCount], savedSamp[5][sampCount];
		IDirect3DVertexBuffer9* stream = 0;
		IDirect3DIndexBuffer9* index = 0;
		UINT streamOffset = 0, streamStride = 0;
		DWORD fvf = 0;

		g_device->GetVertexShader(&vs);
		g_device->GetPixelShader(&ps);
		g_device->GetVertexDeclaration(&decl);
		g_device->GetFVF(&fvf);
		g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);
		g_device->GetIndices(&index);

		for (int s = 0; s < stateCount; s++)
			g_device->GetRenderState(states[s], &saved[s]);

		for (int s = 0; s < 5; s++)
		{
			g_device->GetTexture(stage[s], &tex[s]);

			for (int k = 0; k < sampCount; k++)
				g_device->GetSamplerState(stage[s], samps[k],
					&savedSamp[s][k]);
		}

		const float screen[4] = { vp.Width ? 1.0f / (float)vp.Width : 0.0f,
			vp.Height ? 1.0f / (float)vp.Height : 0.0f, g_proj[10],
			-g_proj[14] };
		const float look[4] = { kMarbleF0, kWetWalls, kWetHighlight,
			kWetPower };
		const float march[4] = { kWetCap, 1.0f, 0.0f, 0.0f };
		const float gain[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
		/* The flip's place in the target (MarblePS). A pixel's centre lies
		 * on the whole number in Direct3D 9, so pixel x of the viewport
		 * shows what the turned projection drew at 2 X + W - x, whose texel
		 * centre is at that plus a half: u = (2 X + W + 0.5 - x) / width. */
		const float width = (float)desc.Width;
		const float flipMask[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		const float flipLook[4] = { 0.0f, 2.0f * (float)vp.X +
			(float)vp.Width + 0.5f, 0.0f,
			width > 0.0f ? 1.0f / width : 0.0f };

		g_device->SetVertexShader(g_card_vs);
		g_device->SetPixelShader(g_marble_ps);
		g_device->SetFVF(kCardFVF);
		set_matrix_constant(0, g_proj);
		g_device->SetPixelShaderConstantF(92, screen, 1);
		g_device->SetPixelShaderConstantF(94, look, 1);
		g_device->SetPixelShaderConstantF(95, march, 1);
		g_device->SetPixelShaderConstantF(96, layer.axes, 3);
		g_device->SetPixelShaderConstantF(100, gain, 1);

		for (int s = 0; s < 5; s++)
		{
			// The mirror read bilinearly, its depth as it stands.
			const bool point = stage[s] == 3;

			g_device->SetSamplerState(stage[s], D3DSAMP_MINFILTER,
				point ? D3DTEXF_POINT : D3DTEXF_LINEAR);
			g_device->SetSamplerState(stage[s], D3DSAMP_MAGFILTER,
				point ? D3DTEXF_POINT : D3DTEXF_LINEAR);
			g_device->SetSamplerState(stage[s], D3DSAMP_MIPFILTER,
				D3DTEXF_NONE);
			g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSU,
				D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSV,
				D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSW,
				D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(stage[s], D3DSAMP_SRGBTEXTURE, FALSE);
		}

		g_device->SetTexture(0, 0);
		g_device->SetTexture(1, g_field_dir);
		g_device->SetTexture(2, g_field_rgb);
		g_device->SetTexture(3, g_marble_ztex);
		g_device->SetTexture(5, g_marble_tex);
		g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
		g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
		g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
		g_device->SetRenderState(D3DRS_STENCILREF, kMarbleStencilBit);
		g_device->SetRenderState(D3DRS_STENCILMASK, kMarbleStencilBit);
		g_device->SetRenderState(D3DRS_STENCILWRITEMASK, kMarbleStencilBit);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
		g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
		g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
		g_device->SetPixelShaderConstantF(105, flipLook, 1);

		const UINT faces = (UINT)(layer.verts.size() / 3);

		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, faces,
			&layer.verts[0], sizeof(CardVertex));

		/* A wet floor's points of this frame (HDRenderWaterWetDraw) take
		 * the bit away again where the water has come: the floor mirrors
		 * there through the wet floor's own draw, and not twice. */
		if (g_wet_ready && !g_wet_tris.empty())
		{
			g_device->SetPixelShaderConstantF(105, flipMask, 1);
			g_device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
			g_device->SetRenderState(D3DRS_ALPHAREF, 0);
			g_device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
			g_device->SetRenderState(D3DRS_STENCILREF, 0);
			g_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0,
				(UINT)g_wet_verts.size(), (UINT)(g_wet_tris.size() / 3),
				&g_wet_tris[0], D3DFMT_INDEX16, &g_wet_verts[0],
				sizeof(CardVertex));
			g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			g_device->SetPixelShaderConstantF(105, flipLook, 1);
			g_marble_stats.wet = 1;
		}

		g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
			D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		g_device->SetRenderState(D3DRS_STENCILREF, kMarbleStencilBit);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_ZERO);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, faces,
			&layer.verts[0], sizeof(CardVertex));
		g_marble_stats.faces += (int)faces;

		for (int s = 0; s < stateCount; s++)
			g_device->SetRenderState(states[s], saved[s]);

		for (int s = 0; s < 5; s++)
		{
			for (int k = 0; k < sampCount; k++)
				g_device->SetSamplerState(stage[s], samps[k],
					savedSamp[s][k]);

			g_device->SetTexture(stage[s], tex[s]);

			if (tex[s])
				tex[s]->Release();
		}

		g_device->SetStreamSource(0, stream, streamOffset, streamStride);
		g_device->SetIndices(index);
		g_device->SetFVF(fvf);
		g_device->SetVertexDeclaration(decl);
		g_device->SetVertexShader(vs);
		g_device->SetPixelShader(ps);

		if (stream) stream->Release();
		if (index) index->Release();
		if (decl) decl->Release();
		if (vs) vs->Release();
		if (ps) ps->Release();
	}

	if (depth)
		depth->Release();

	target->Release();
}

/* The frame's second wet-floor draw (see HDRenderWaterWetDraw), after the
 * rooms' glass and glows and before the cards: a copy of the frame as it
 * stands, and the mirror out of that copy. */
void HDRenderWaterWetMirror()
{
	if (!g_wet_ready || !g_device || !g_in_frame || g_wet_tris.empty())
		return;

	g_wet_ready = false;
	wet_give_way();
	wet_copy_late();
	wet_layer(1);
}

/* One of the wet floors' two draws over the frame's kept points: part 0
 * the darkening, part 1 the mirror out of the later copy - or,
 * with RoomProbe's -tankmarch, the march debugger's colours in its place,
 * written whole, the opaque frame's copy on s7. */
static void wet_layer(int part)
{
	std::vector<CardVertex>& verts = g_wet_layer;
	std::vector<unsigned short>& tris = g_wet_tris;
	const float* axes = g_wet_axes;
	const bool debug = part == 1 && g_wet_debug > 0 && g_wet_debug_ps;
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	const DWORD stage[8] = { 1, 2, 5, 6, 7, 0, 3, 4 };
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* index = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
		D3DRS_BLENDOP, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_FOGENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE
	};
	const D3DSAMPLERSTATETYPE samps[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW,
		D3DSAMP_SRGBTEXTURE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	const int sampCount = (int)(sizeof(samps) / sizeof(samps[0]));
	DWORD saved[stateCount], savedSamp[8][sampCount];

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetStreamSource(0, &stream, &streamOffset, &streamStride);
	g_device->GetIndices(&index);

	for (int i = 0; i < stateCount; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int s = 0; s < 8; s++)
	{
		g_device->GetTexture(stage[s], &tex[s]);

		for (int i = 0; i < sampCount; i++)
			g_device->GetSamplerState(stage[s], samps[i], &savedSamp[s][i]);
	}

	D3DVIEWPORT9 vp = {};

	g_device->GetViewport(&vp);

	const bool soft = g_depth_texture != 0;
	const float screen[4] = { vp.Width ? 1.0f / (float)vp.Width : 0.0f,
		vp.Height ? 1.0f / (float)vp.Height : 0.0f, g_proj[10], -g_proj[14] };
	const float mode[4] = { soft ? 1.0f : 0.0f, kWetLift + kWetHole,
		1.0f / kWetHole, kWetDark };
	const float look[4] = { kWetF0, kWetWalls, kWetHighlight, kWetPower };
	const bool copy = g_wet_late_valid && g_wet_late;
	const float fit = debug && g_wet_try[0] > 0.0f ? g_wet_try[0] : 1.0f;
	const float march[4] = { kWetCap, copy ? 1.0f : 0.0f,
		debug && g_wet_try[2] > 0.0f ? g_wet_try[2] : kWetFirstStep,
		debug && g_wet_try[1] > 0.0f ? g_wet_try[1] : kWetGrowth };
	const float proj[4] = { g_proj[0], g_proj[5], g_proj[8], g_proj[9] };
	const float gain[4] = { kWetMirror, (float)part, kWetFit * fit,
		kWetFitShare * fit };
	const float mapped[4] = { (float)g_wet_debug, 0.0f, 0.0f, 0.0f };

	g_device->SetVertexShader(g_card_vs);
	g_device->SetPixelShader(debug ? g_wet_debug_ps : g_wet_ps);
	g_device->SetPixelShaderConstantF(101, mapped, 1);
	g_device->SetFVF(kCardFVF);
	set_matrix_constant(0, g_proj);
	g_device->SetPixelShaderConstantF(92, screen, 1);
	g_device->SetPixelShaderConstantF(93, mode, 1);
	g_device->SetPixelShaderConstantF(94, look, 1);
	g_device->SetPixelShaderConstantF(95, march, 1);
	g_device->SetPixelShaderConstantF(96, axes, 3);
	g_device->SetPixelShaderConstantF(99, proj, 1);
	g_device->SetPixelShaderConstantF(100, gain, 1);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, debug ? FALSE : TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	for (int s = 0; s < 5; s++)
	{
		if (s == 3)
			continue;

		g_device->SetSamplerState(stage[s], D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		g_device->SetSamplerState(stage[s], D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		g_device->SetSamplerState(stage[s], D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSU,
			D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSV,
			D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSW,
			D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(stage[s], D3DSAMP_SRGBTEXTURE, FALSE);
	}

	g_device->SetTexture(1, g_field_dir);
	g_device->SetTexture(2, g_field_rgb);
	g_device->SetTexture(5, copy ? g_wet_late : 0);
	g_device->SetTexture(7, debug && g_copy_valid ? g_copy_texture : 0);
	g_device->SetSamplerState(6, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(6, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(6, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	g_device->SetSamplerState(6, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(6, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(6, D3DSAMP_SRGBTEXTURE, FALSE);
	g_device->SetTexture(6, soft ? g_depth_texture : 0);

	/* The models through the mirror (HDRenderWaterWetModels), for the
	 * second draw: their target on s0, their depth on s3, read texel for
	 * texel; the plane on c102, and on c103 whether they are there and how
	 * far a pixel's layer may lie from the plane - the layer's own lift and
	 * the rounding a sloping square's corners have (kWetSlopeRise). */
	const bool models = part == 1 && g_wet_models;
	const bool room = models && g_wet_room_valid;
	/* The soft mirror: the mirror into its layer, blurred across into the
	 * later copy (read no more by then), and down into the frame - where
	 * the frame is one sample a pixel, as the layer is. */
	IDirect3DSurface9* frame = 0;
	D3DSURFACE_DESC frameDesc;
	const bool blur = part == 1 && !debug && g_wet_blur > 0.0f && copy &&
		g_wet_late_surface &&
		SUCCEEDED(g_device->GetRenderTarget(0, &frame)) && frame &&
		SUCCEEDED(frame->GetDesc(&frameDesc)) &&
		frameDesc.MultiSampleType == D3DMULTISAMPLE_NONE &&
		wet_make_soft(frameDesc);
	const float modelsOn[4] = { models ? 1.0f : 0.0f,
		kWetLift + (float)kWetSlopeRise, room ? 1.0f : 0.0f,
		blur ? g_wet_blur : 0.0f };

	for (int s = 5; s < 7; s++)
	{
		g_device->SetSamplerState(stage[s], D3DSAMP_MINFILTER, D3DTEXF_POINT);
		g_device->SetSamplerState(stage[s], D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		g_device->SetSamplerState(stage[s], D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSU,
			D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(stage[s], D3DSAMP_ADDRESSV,
			D3DTADDRESS_CLAMP);
		g_device->SetSamplerState(stage[s], D3DSAMP_SRGBTEXTURE, FALSE);
	}

	g_device->SetTexture(0, models ? g_wet_model : 0);
	g_device->SetTexture(3, models ? g_wet_model_depth : 0);
	// The room behind the models on s4, point sampled as the frame's copy.
	g_device->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	g_device->SetSamplerState(4, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	g_device->SetSamplerState(4, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(4, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(4, D3DSAMP_SRGBTEXTURE, FALSE);
	g_device->SetTexture(4, room ? g_wet_room : 0);
	g_device->SetPixelShaderConstantF(102, g_wet_plane, 1);
	g_device->SetPixelShaderConstantF(103, modelsOn, 1);

	// The soft mirror's distance in whole (gWetBlur.w), for the layer.
	const float nearOn[4] = { 0.0f, 0.0f, 0.0f,
		g_wet_blur_near > 1.0f ? 1.0f / g_wet_blur_near : 1.0f };

	g_device->SetPixelShaderConstantF(104, nearOn, 1);

	const auto draw = [&]()
	{
		g_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0,
			(UINT)verts.size(), (UINT)(tris.size() / 3), &tris[0],
			D3DFMT_INDEX16, &verts[0], sizeof(CardVertex));
	};

	if (blur)
	{
		g_device->SetRenderTarget(0, g_wet_soft_surface);
		g_device->SetViewport(&vp);
		g_device->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.0f, 0);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}

	draw();

	if (blur)
	{
		const float across[4] = { screen[0], 0.0f, 0.0f, 0.0f };
		const float down[4] = { 0.0f, screen[1], 1.0f, 0.0f };
		const float pass[4] = { kWetMirror, 2.0f, 0.0f, 0.0f };

		g_device->SetPixelShaderConstantF(100, pass, 1);
		g_device->SetPixelShaderConstantF(104, across, 1);
		g_device->SetRenderTarget(0, g_wet_late_surface);
		g_device->SetViewport(&vp);
		g_device->SetTexture(0, g_wet_soft);
		draw();
		g_device->SetPixelShaderConstantF(104, down, 1);
		g_device->SetRenderTarget(0, frame);
		g_device->SetViewport(&vp);
		g_device->SetTexture(0, g_wet_late);
		g_device->SetTexture(4, g_wet_soft);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		draw();
		g_wet_late_valid = false;
	}

	if (frame)
		frame->Release();

	for (int i = 0; i < stateCount; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int s = 0; s < 8; s++)
	{
		for (int i = 0; i < sampCount; i++)
			g_device->SetSamplerState(stage[s], samps[i], savedSamp[s][i]);

		g_device->SetTexture(stage[s], tex[s]);

		if (tex[s])
			tex[s]->Release();
	}

	// DrawIndexedPrimitiveUP leaves stream 0 and the indices unset.
	g_device->SetStreamSource(0, stream, streamOffset, streamStride);
	g_device->SetIndices(index);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (stream) stream->Release();
	if (index) index->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
}

void HDRenderWaterFalls(const HDWaterFall* falls, int count, const char* stem)
{
	(void)stem;
	g_wfalls.clear();

	for (size_t i = g_wfeet.size(); i-- > 0; )
		if (!g_wfeet[i].fromRow)
			g_wfeet.erase(g_wfeet.begin() + (std::ptrdiff_t)i);

	for (size_t i = g_wsegs.size(); i-- > 0; )
		if (!g_wsegs[i].fromRow)
			g_wsegs.erase(g_wsegs.begin() + (std::ptrdiff_t)i);

	g_wlevel.falls = 0;

	// Each object's foot points over a sheet, for the curtains below.
	std::vector<std::vector<float> > ptsX, ptsZ;

	for (int f = 0; falls && f < count; f++)
	{
		const HDWaterFall& w = falls[f];
		FallSeen seen = {};
		std::vector<float> px, pz;

		seen.item = w.item;
		seen.slot = w.slot;
		seen.room = w.room;
		seen.top = w.top;
		seen.bottom = w.bottom;
		seen.points = w.points;
		seen.surface = -1000000;
		seen.body = -1;
		seen.curtain = -1;

		// A flat object is a scrolling face, not a fall: no foot.
		if (w.bottom - w.top > (float)HD_WATER_FOOT_BAND)
			for (int p = 0; p < w.points && p < HD_WATER_FOOT_POINTS; p++)
			{
				int body = -1;
				const float wy = sheet_surface_near(w.foot[p * 2],
					w.foot[p * 2 + 1], w.bottom, kFootAbove, kFootBelow,
					&body);

				if (wy < -1.0e8f)
					continue;

				if (!seen.over || fabsf(wy - w.bottom) <
					fabsf((float)seen.surface - w.bottom))
				{
					seen.surface = (int)wy;
					seen.body = body;
				}

				seen.over = 1;
				px.push_back(w.foot[p * 2]);
				pz.push_back(w.foot[p * 2 + 1]);
			}

		if (seen.over)
			g_wlevel.falls++;

		if (seen.over && seen.body >= 0 &&
			g_wbodies[(size_t)seen.body].info.footOff)
			seen.off = 1;

		/* Where the art draws the fall, its water lands where the art's
		 * falling water ends and not where the classic mesh would have: in
		 * ANDREA2 room 231 the remaster's curtains land about 450 units
		 * from the classic object's foot, nearer the room's middle (the
		 * WATERLOG foot= lines with and without the art, x 55,518 to 55,585
		 * against 55,967 to 56,028). The art's foot points of a room drawn
		 * HD within kFootSnap of the classic foot and over the same sheet
		 * take the classic ones' place. kFootSnap, a square, is a free
		 * choice: it takes those 450 units, and two waterfalls over a sheet
		 * in one room stand two squares apart at the least (ItemCheck on
		 * the fifteen classic files: JOBY2 room 66). */
		if (seen.over && !seen.off)
		{
			std::vector<float> ax, az;

			for (const FlowFoot& ff : g_flow_feet)
			{
				if (ff.group < 0 || (size_t)ff.group >= g_wroom_drawn.size() ||
					!g_wroom_drawn[(size_t)ff.group])
					continue;

				const float x = ff.x +
					(float)g_wroom_origin[(size_t)ff.group * 2];
				const float z = ff.z +
					(float)g_wroom_origin[(size_t)ff.group * 2 + 1];
				bool close = false;

				for (size_t k = 0; k < px.size() && !close; k++)
					close = (x - px[k]) * (x - px[k]) +
						(z - pz[k]) * (z - pz[k]) <= kFootSnap * kFootSnap;

				if (close && sheet_surface_near(x, z, ff.y, kFootAbove,
					kFootBelow, 0) > -1.0e8f)
				{
					ax.push_back(x);
					az.push_back(z);
				}
			}

			if (!ax.empty())
			{
				px.swap(ax);
				pz.swap(az);
				seen.art = (int)px.size();
			}
		}

		if (!seen.over || seen.off)
		{
			px.clear();
			pz.clear();
		}

		ptsX.push_back(px);
		ptsZ.push_back(pz);
		g_wfalls.push_back(seen);
	}

	/* The curtains: the objects over one body whose points come within
	 * kFootJoin of each other's, joined as long as any pair of them is. */
	const size_t objects = g_wfalls.size();
	std::vector<int> joined(objects, -1);

	for (size_t a = 0; a < objects; a++)
	{
		if (ptsX[a].empty() || joined[a] >= 0)
			continue;

		std::vector<size_t> curtain(1, a);

		joined[a] = (int)a;

		for (size_t at = 0; at < curtain.size(); at++)
		{
			const size_t i = curtain[at];

			for (size_t j = 0; j < objects; j++)
			{
				if (joined[j] >= 0 || ptsX[j].empty() ||
					g_wfalls[j].body != g_wfalls[i].body)
					continue;

				bool touch = false;

				for (size_t p = 0; p < ptsX[i].size() && !touch; p++)
					for (size_t q = 0; q < ptsX[j].size() && !touch; q++)
					{
						const float dx = ptsX[i][p] - ptsX[j][q];
						const float dz = ptsZ[i][p] - ptsZ[j][q];

						touch = dx * dx + dz * dz <= kFootJoin * kFootJoin;
					}

				if (touch)
				{
					joined[j] = (int)a;
					curtain.push_back(j);
				}
			}
		}

		std::vector<float> px, pz;

		for (size_t k = 0; k < curtain.size(); k++)
		{
			px.insert(px.end(), ptsX[curtain[k]].begin(),
				ptsX[curtain[k]].end());
			pz.insert(pz.end(), ptsZ[curtain[k]].begin(),
				ptsZ[curtain[k]].end());
		}

		const int id = g_curtains;
		const int sources = lay_fall_curtain(px, pz,
			(float)g_wfalls[a].surface, g_wfalls[a].body);

		for (size_t k = 0; k < curtain.size(); k++)
		{
			g_wfalls[curtain[k]].curtain = id;
			g_wfalls[curtain[k]].sources = sources;
		}
	}

	g_wlevel.feet = (int)g_wfeet.size();
}

void HDRenderWaterFootAdd(float x1, float z1, float x2, float z2,
	float reach)
{
	int body = -1;
	const float wy = sheet_surface_near(0.5f * (x1 + x2), 0.5f * (z1 + z2),
		0.0f, 1.0e8f, 1.0e8f, &body);
	const float at[5] = { x1, z1, x2, z2, reach };

	if (wy < -1.0e8f)
		return;

	lay_row_foot(at, wy, body);
	g_wlevel.feet = (int)g_wfeet.size();
}

int HDRenderWaterFeet()
{
	return HDRenderWaterSheetsBuilt() ? (int)g_wfeet.size() : 0;
}

bool HDRenderWaterFoot(int at, float* place, float* dir, float* half)
{
	if (at < 0 || at >= HDRenderWaterFeet())
		return false;

	// A foot on flipped water only while its body draws.
	if (!body_shown(g_wfeet[(size_t)at].body))
		return false;

	const WaterFoot& f = g_wfeet[(size_t)at];

	if (place)
	{
		place[0] = f.x;
		place[1] = f.y;
		place[2] = f.z;
	}

	if (dir)
	{
		dir[0] = f.dx;
		dir[1] = f.dz;
	}

	// Half the stretch its drops scatter over (see "the foot of a
	// waterfall").
	if (half)
		*half = f.half;

	return true;
}

/* WATERLOG, one part of the remaster's still water art (HDWaterArt, in group
 * order): its room and its index there, the body under it (-1 for none),
 * the class and the squares its ripple took, and the ripple: its length in
 * units, its period in seconds and its travel in page units, and the part's
 * own span (see "the art's ripple"). False past the last. */
bool HDRenderWaterArtLine(int at, const char* stem, char* out, size_t size)
{
	if (!out || !size || at < 0)
		return false;

	for (size_t g = 0; g < g_groups.size(); g++)
	{
		const std::vector<HDWaterArt>& arts = g_groups[g].waterArt;

		if (at >= (int)arts.size())
		{
			at -= (int)arts.size();
			continue;
		}

		const HDWaterArt& a = arts[(size_t)at];

		snprintf(out, size, "WATERLOG level=%s art room=%d part=%d body=%d "
			"class=%s squares=%.1f length=%.0f period=%.2f travel=%.4f "
			"span=%.3f", stem ? stem : "?", (int)g, a.part, a.body,
			HDRenderWaterClassName(a.cls), a.squares,
			a.ripple[0] > 0.0f ? 1.0f / a.ripple[0] : 0.0f,
			a.ripple[2] > 0.0f ? 1.0f / a.ripple[2] : 0.0f, a.ripple[1],
			a.span);
		return true;
	}

	return false;
}

/* WATERLOG, a waterfall object: its number in items[], its slot and room,
 * the highest and the lowest y of its classic mesh as placed, the foot
 * points, 1 when the foot stands over a sheet, the surface there and the gap
 * (the surface less the lowest y: over 0 the fall ends above the water, under
 * 0 it reaches into it; -1000000 and - where no sheet lies under it), the
 * body, the sources of its curtain, 1 when a row's `foot off` took them,
 * how many foot points of the art's own falling water took the classic
 * ones' place (0: the classic mesh's foot) and its curtain, -1 for none -
 * objects joined into one curtain show the same number (see "the foot of a
 * waterfall"). */
bool HDRenderWaterFallLine(int at, const char* stem, char* out, size_t size)
{
	if (!out || !size || at < 0 || at >= (int)g_wfalls.size())
		return false;

	const FallSeen& f = g_wfalls[(size_t)at];
	char gap[16];

	if (f.over)
		snprintf(gap, sizeof(gap), "%d", f.surface - (int)f.bottom);
	else
		snprintf(gap, sizeof(gap), "-");

	snprintf(out, size, "WATERLOG level=%s fall=%d item=%d slot=%d room=%d "
		"top=%d bottom=%d points=%d over=%d surface=%d gap=%s body=%d "
		"sources=%d off=%d art=%d curtain=%d", stem ? stem : "?", at, f.item,
		f.slot, f.room, (int)f.top, (int)f.bottom, f.points, f.over,
		f.surface, gap, f.body, f.sources, f.off, f.art, f.curtain);
	return true;
}

/* WATERLOG, a source of rings and drops: its world x, y (the surface) and
 * z, its curtain's reach (the field keeps its name, radius), half the
 * stretch its drops scatter over, its body, 1 for a row's or RoomProbe's,
 * and its curtain. */
bool HDRenderWaterFootLine(int at, const char* stem, char* out, size_t size)
{
	if (!out || !size || at < 0 || at >= (int)g_wfeet.size())
		return false;

	const WaterFoot& f = g_wfeet[(size_t)at];

	snprintf(out, size, "WATERLOG level=%s foot=%d x=%d y=%d z=%d radius=%d "
		"half=%d body=%d row=%d curtain=%d", stem ? stem : "?", at, (int)f.x,
		(int)f.y, (int)f.z, (int)f.reach, (int)f.half, f.body, f.fromRow,
		f.curtain);
	return true;
}

/* WATERLOG, a segment of a curtain's footprint: its number, its curtain,
 * its ends (world x and z), its surface, its curtain's reach, its share of
 * the foam (1, or 0 over a gap where no water lands), its body, 1 for a
 * row's or RoomProbe's - the places a `foot` row needs - and the heads at
 * its start and end (1 round, see "the foot of a waterfall"). */
bool HDRenderWaterSegLine(int at, const char* stem, char* out, size_t size)
{
	if (!out || !size || at < 0 || at >= (int)g_wsegs.size())
		return false;

	const FootSeg& s = g_wsegs[(size_t)at];

	snprintf(out, size, "WATERLOG level=%s seg=%d curtain=%d x1=%d z1=%d "
		"x2=%d z2=%d y=%d reach=%d wet=%.2f body=%d row=%d head=%.2f,%.2f",
		stem ? stem : "?", at, s.curtain, (int)s.x1, (int)s.z1, (int)s.x2,
		(int)s.z2, (int)s.y, (int)s.reach, s.wet, s.body, s.fromRow, s.headA,
		s.headB);
	return true;
}

/* The mist over a waterfall's foot (hd_level.cpp, MoveWaterfallMist): the
 * nearest point to world x, z on a wet segment of a curtain whose feet the
 * art's falling water gave (see "the foot of a waterfall": the art lands
 * apart from the classic object), within kFootSnap - the same square that
 * takes the art's feet for the classic ones. False where none lies so near,
 * and with no sheets (tomb5.hd_water 0 lays no curtain). */
bool HDRenderWaterFootNearest(float x, float z, float* nx, float* nz)
{
	float best = kFootSnap * kFootSnap;
	bool found = false;

	for (const FootSeg& s : g_wsegs)
	{
		if (s.fromRow || s.wet <= 0.0f || s.curtain < 0)
			continue;

		bool art = false;

		for (const FallSeen& f : g_wfalls)
			art = art || (f.curtain == s.curtain && f.art > 0);

		if (!art)
			continue;

		const float ex = s.x2 - s.x1, ez = s.z2 - s.z1;
		const float len2 = ex * ex + ez * ez;
		float u = len2 > 0.0f ? ((x - s.x1) * ex + (z - s.z1) * ez) / len2 :
			0.0f;

		u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;

		const float px = s.x1 + u * ex, pz = s.z1 + u * ez;
		const float d2 = (px - x) * (px - x) + (pz - z) * (pz - z);

		if (d2 <= best)
		{
			best = d2;
			found = true;

			if (nx) *nx = px;
			if (nz) *nz = pz;
		}
	}

	return found;
}

void HDRenderWaterLara(int inWater, float x, float z)
{
	g_lara_in_water = inWater != 0;
	g_lara_x = x;
	g_lara_z = z;
}

/* The creatures' mark. The waterline of the sheets' depth variant is a wet
 * line wherever the frame lies within 24 to 48 units under the surface:
 * round a post, a bank, a floe - and on a swimming body, where it is white
 * paint. Lara's own gap (gWaterLara) is a circle round her place; every
 * other creature in the water - a swimming rat lies 50 units under the
 * surface (game\rat.cpp) and its belly 60 - needs its own. So what the HD
 * objects draw marks its pixels in the stencil, bit kCreatureStencilBit,
 * and the sheets draw the marked pixels in a second pass with no line
 * (draw_water_sheet): a mark costs the depth variant no slot, where a list
 * of creatures read in a loop would take it over its 512.
 *
 * The bit is the frame's own: the rooms' receive clears the whole stencil
 * at its start, and where no receive ran this bracket the mark clears it
 * itself and says so (g_receive_cleared), so the models' receive after it
 * clears nothing. Both receives keep to their own bits. The classic
 * buckets drawn between the two phases (output.cpp, DrawBuckets) are not
 * marked: they hold the statics of the rooms that stay classic, whose wet
 * line is a bank's; every creature that swims in TR5 - the rat - has an HD
 * model (MapProbe, the RAT slot in every level). */
/* Whether the depth-stencil surface now set is the sheets' INTZ surface. */
static int depth_is_intz()
{
	IDirect3DSurface9* now = 0;

	if (!g_device || FAILED(g_device->GetDepthStencilSurface(&now)) || !now)
		return 0;

	const int same = now == g_depth_surface ? 1 : 0;

	now->Release();
	return same;
}

bool HDRenderWaterMarkLine(const char* stem, char* out, size_t size)
{
	if (!out || !size || g_mark_logged || !g_mark_sheets)
		return false;

	g_mark_logged = true;
	snprintf(out, size, "WATERLOG level=%s mark=%d why=%d mark_intz=%d "
		"objects=%d sheet_intz=%d sheets=%d split=%d late=%d foam=%d",
		stem ? stem : "?",
		g_mark_frame ? 1 : 0, g_mark_why, g_mark_surface, g_mark_objects,
		g_mark_sheet_surface, g_mark_sheets, g_mark_split, g_mark_late,
		g_mark_foam);
	return true;
}

void HDRenderWaterMarkBegin()
{
	// The room behind the models, before they are drawn.
	HDRenderWaterWetRoomCopy(false);

	g_mark_frame = false;
	g_mark_open = false;
	g_mark_surface = 0;
	g_mark_objects = 0;
	g_mark_late = 0;
	g_mark_sheet_surface = 0;
	g_mark_sheets = 0;
	g_mark_split = 0;
	g_mark_foam = 0;
	g_mark_why = !g_device ? 1 : !g_wlevel.built ? 2 : !g_depth_texture ? 3 :
		!g_water_depth_ps ? 4 : !depth_has_stencil() ? 5 : 0;

	if (g_mark_why)
		return;

	g_mark_surface = depth_is_intz();

	for (int i = 0; i < kMarkStateCount; i++)
		g_device->GetRenderState(kMarkStates[i], &g_mark_saved[i]);

	if (!g_receive_cleared)
	{
		g_device->Clear(0, 0, D3DCLEAR_STENCIL, 0, 1.0f, 0);
		g_receive_cleared = true;
	}

	g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
	g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	g_device->SetRenderState(D3DRS_STENCILREF, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILMASK, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILWRITEMASK, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
	g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	g_mark_frame = true;
	g_mark_open = true;
}

void HDRenderWaterMarkEnd(int objects)
{
	if (!g_mark_open)
		return;

	g_mark_objects = objects;

	for (int i = 0; i < kMarkStateCount; i++)
		g_device->SetRenderState(kMarkStates[i], g_mark_saved[i]);

	g_mark_open = false;
}

/* RoomProbe's stand-in: a box 120 units across, from 60 under the surface
 * (a swimming rat's belly) to 100 over it, grey, drawn with the fixed
 * function in the frame's view and projection - everything it sets is put
 * back. A wader's box stands on the floor under the square, the water's top
 * and the square's depth down, 768 units high as Lara stands and 160
 * across (a free choice, about her hips' width). A swimmer's box (`wader`
 * 2) lies wholly under the water, its top `below` units under the surface,
 * 160 across and 200 deep (free choices, a body swimming under the
 * surface). */
bool HDRenderWaterProbeCreature(float x, float z, float offX, float offZ,
	int mark, int wader, float below)
{
	if (!g_device)
		return false;

	float y = 0.0f;
	float floorY = 0.0f;
	bool found = false;
	const std::unordered_multimap<unsigned long long, int>::const_iterator f =
		g_sheet_world.find(WaterKey((int)floorf(x), (int)floorf(z), 0));

	if (f != g_sheet_world.end())
	{
		y = (float)f->second;
		found = true;
	}

	if (!found)
		return false;

	for (size_t i = 0; wader == 1 && i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];

		if (s.sheet && s.wy == f->second && x >= (float)s.wx &&
			x < (float)(s.wx + 1024) && z >= (float)s.wz &&
			z < (float)(s.wz + 1024))
			floorY = (float)(s.wy + s.depth);
	}

	struct Corner
	{
		float x, y, z;
		D3DCOLOR c;
	};

	const float lx = x - offX, lz = z - offZ;
	Corner v[8];

	const float half = wader ? 80.0f : 60.0f;

	for (int i = 0; i < 8; i++)
	{
		v[i].x = lx + ((i & 1) ? half : -half);
		v[i].y = wader == 2 ? y + below + ((i & 2) ? 200.0f : 0.0f) :
			wader ? floorY - ((i & 2) ? 0.0f : 768.0f) :
			y + ((i & 2) ? 60.0f : -100.0f);
		v[i].z = lz + ((i & 4) ? half : -half);
		v[i].c = D3DCOLOR_XRGB(120, 112, 100);
	}

	static const unsigned short faces[36] =
	{
		0, 1, 3, 0, 3, 2,  4, 6, 7, 4, 7, 5,  0, 4, 5, 0, 5, 1,
		2, 3, 7, 2, 7, 6,  0, 2, 6, 0, 6, 4,  1, 5, 7, 1, 7, 3
	};

	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex = 0;
	DWORD fvf = 0;
	D3DMATRIX world, view, proj;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_LIGHTING, D3DRS_CULLMODE, D3DRS_ALPHABLENDENABLE,
		D3DRS_ALPHATESTENABLE, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE,
		D3DRS_ZFUNC, D3DRS_FOGENABLE, D3DRS_COLORWRITEENABLE
	};
	const D3DTEXTURESTAGESTATETYPE stages[] =
	{
		D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1
	};
	DWORD saved[9], stage[4];

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetTexture(0, &tex);
	g_device->GetTransform(D3DTS_WORLD, &world);
	g_device->GetTransform(D3DTS_VIEW, &view);
	g_device->GetTransform(D3DTS_PROJECTION, &proj);

	for (int i = 0; i < 9; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int i = 0; i < 4; i++)
		g_device->GetTextureStageState(0, stages[i], &stage[i]);

	D3DMATRIX identity = {};

	identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
	g_device->SetVertexShader(0);
	g_device->SetPixelShader(0);
	g_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	g_device->SetTexture(0, 0);
	g_device->SetTransform(D3DTS_WORLD, &identity);
	g_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX*)g_view);
	g_device->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX*)g_proj);
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

	if (mark)
		HDRenderWaterMarkBegin();

	g_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 8, 12, faces,
		D3DFMT_INDEX16, v, sizeof(Corner));

	if (mark)
		HDRenderWaterMarkEnd();

	g_device->SetTransform(D3DTS_WORLD, &world);
	g_device->SetTransform(D3DTS_VIEW, &view);
	g_device->SetTransform(D3DTS_PROJECTION, &proj);

	for (int i = 0; i < 9; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int i = 0; i < 4; i++)
		g_device->SetTextureStageState(0, stages[i], stage[i]);

	g_device->SetTexture(0, tex);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (tex) tex->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();

	return true;
}

/* The mark for what the classic buckets draw (see "the creatures' mark"):
 * a swimming rat is no HD object - DrawRats hands its mesh to the classic
 * path, which draws it between the two brackets, outside the mark - so its
 * pixels took the waterline and the bank's foam. Its triangles are drawn
 * here once more, into the creature bit alone: no colour, no depth write,
 * the depth test against the frame's depth, which holds the classic draw's
 * own. The caller hands them already pulled toward the eye by a fraction
 * of their distance, so they pass against their own depth. In the frame's
 * view (g_view), with the fixed function and every state put back. */
int HDRenderWaterMarkTriangles(const float* xyz, int vertices)
{
	if (!g_device || !xyz || vertices < 3 || !g_wlevel.built ||
		!g_depth_texture || !g_water_depth_ps || !depth_has_stencil())
		return 0;

	struct Corner
	{
		float x, y, z;
		D3DCOLOR c;
	};

	std::vector<Corner> v((size_t)(vertices - vertices % 3));

	for (size_t i = 0; i < v.size(); i++)
	{
		v[i].x = xyz[i * 3];
		v[i].y = xyz[i * 3 + 1];
		v[i].z = xyz[i * 3 + 2];
		v[i].c = 0;
	}

	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex = 0;
	DWORD fvf = 0;
	D3DMATRIX world, view, proj;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_LIGHTING, D3DRS_CULLMODE, D3DRS_ALPHABLENDENABLE,
		D3DRS_ALPHATESTENABLE, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE,
		D3DRS_ZFUNC, D3DRS_FOGENABLE, D3DRS_COLORWRITEENABLE
	};
	const int count = (int)(sizeof(states) / sizeof(states[0]));
	DWORD saved[count], mark[kMarkStateCount];

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetTexture(0, &tex);
	g_device->GetTransform(D3DTS_WORLD, &world);
	g_device->GetTransform(D3DTS_VIEW, &view);
	g_device->GetTransform(D3DTS_PROJECTION, &proj);

	for (int i = 0; i < count; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int i = 0; i < kMarkStateCount; i++)
		g_device->GetRenderState(kMarkStates[i], &mark[i]);

	D3DMATRIX identity = {};

	identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
	g_device->SetVertexShader(0);
	g_device->SetPixelShader(0);
	g_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
	g_device->SetTexture(0, 0);
	g_device->SetTransform(D3DTS_WORLD, &identity);
	g_device->SetTransform(D3DTS_VIEW, (const D3DMATRIX*)g_view);
	g_device->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX*)g_proj);
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
	g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
	g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	g_device->SetRenderState(D3DRS_STENCILREF, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILMASK, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILWRITEMASK, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
	g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);

	const int tris = (int)v.size() / 3;

	g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)tris, &v[0],
		sizeof(Corner));
	g_mark_late += tris;

	for (int i = 0; i < kMarkStateCount; i++)
		g_device->SetRenderState(kMarkStates[i], mark[i]);

	g_device->SetTransform(D3DTS_WORLD, &world);
	g_device->SetTransform(D3DTS_VIEW, &view);
	g_device->SetTransform(D3DTS_PROJECTION, &proj);

	for (int i = 0; i < count; i++)
		g_device->SetRenderState(states[i], saved[i]);

	g_device->SetTexture(0, tex);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (tex) tex->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();

	return tris;
}

/* RoomProbe's -latemark: the stand-in box of -creature as a triangle list,
 * marked through HDRenderWaterMarkTriangles - drawn where the classic
 * buckets would draw a rat, after the HD objects' bracket. */
bool HDRenderWaterProbeCreatureLate(float x, float z, float offX, float offZ)
{
	const std::unordered_multimap<unsigned long long, int>::const_iterator f =
		g_sheet_world.find(WaterKey((int)floorf(x), (int)floorf(z), 0));

	if (f == g_sheet_world.end())
		return false;

	static const unsigned short faces[36] =
	{
		0, 1, 3, 0, 3, 2,  4, 6, 7, 4, 7, 5,  0, 4, 5, 0, 5, 1,
		2, 3, 7, 2, 7, 6,  0, 2, 6, 0, 6, 4,  1, 5, 7, 1, 7, 3
	};
	const float y = (float)f->second;
	float corner[8][3];
	float tris[36 * 3];

	for (int i = 0; i < 8; i++)
	{
		corner[i][0] = x - offX + ((i & 1) ? 60.0f : -60.0f);
		corner[i][1] = y + ((i & 2) ? 60.0f : -100.0f);
		corner[i][2] = z - offZ + ((i & 4) ? 60.0f : -60.0f);
	}

	for (int i = 0; i < 36; i++)
		for (int k = 0; k < 3; k++)
			tris[i * 3 + k] = corner[faces[i]][k];

	return HDRenderWaterMarkTriangles(tris, 36) > 0;
}

bool HDRenderWaterMarkView()
{
	if (!g_device || !depth_has_stencil())
		return false;

	D3DVIEWPORT9 vp = {};

	if (FAILED(g_device->GetViewport(&vp)))
		return false;

	struct Corner
	{
		float x, y, z, rhw;
		D3DCOLOR c;
	};

	const float x0 = (float)vp.X - 0.5f, y0 = (float)vp.Y - 0.5f;
	const float x1 = x0 + (float)vp.Width, y1 = y0 + (float)vp.Height;
	const D3DCOLOR red = D3DCOLOR_ARGB(140, 255, 0, 0);
	const Corner v[4] =
	{
		{ x0, y0, 0.0f, 1.0f, red }, { x1, y0, 0.0f, 1.0f, red },
		{ x0, y1, 0.0f, 1.0f, red }, { x1, y1, 0.0f, 1.0f, red }
	};
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE,
		D3DRS_CULLMODE, D3DRS_LIGHTING, D3DRS_FOGENABLE
	};
	DWORD saved[9], stencil[kMarkStateCount], stage[4], fvf = 0;
	const D3DTEXTURESTAGESTATETYPE stages[] =
	{
		D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1
	};
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex = 0;

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetTexture(0, &tex);

	for (int i = 0; i < 9; i++)
		g_device->GetRenderState(states[i], &saved[i]);

	for (int i = 0; i < kMarkStateCount; i++)
		g_device->GetRenderState(kMarkStates[i], &stencil[i]);

	for (int i = 0; i < 4; i++)
		g_device->GetTextureStageState(0, stages[i], &stage[i]);

	g_device->SetVertexShader(0);
	g_device->SetPixelShader(0);
	g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	g_device->SetTexture(0, 0);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
	g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
	g_device->SetRenderState(D3DRS_STENCILREF, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILMASK, kCreatureStencilBit);
	g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
	g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Corner));

	for (int i = 0; i < 9; i++)
		g_device->SetRenderState(states[i], saved[i]);

	for (int i = 0; i < kMarkStateCount; i++)
		g_device->SetRenderState(kMarkStates[i], stencil[i]);

	for (int i = 0; i < 4; i++)
		g_device->SetTextureStageState(0, stages[i], stage[i]);

	g_device->SetTexture(0, tex);
	g_device->SetFVF(fvf);
	g_device->SetVertexDeclaration(decl);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);

	if (tex) tex->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();

	return true;
}

// The level's own bodies; the tanks' films that follow them are no body
// of the log's.
int HDRenderWaterBodies()
{
	return g_film_first >= 0 && g_film_first < (int)g_wbodies.size() ?
		g_film_first : (int)g_wbodies.size();
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

/* WATERLOG. The level line: the option, the section, the water squares
 * (sectors, each place once) and of those places the ones drawing a sheet,
 * whether the sheets' buffers were built, the bodies by class, hd_water.txt's
 * lines (none without the file), the rows it gave and the lines it could not
 * read, the override rows of this level that found a body and those that did
 * not, the squares two rooms that are no flipmap pair both hold, the
 * underwater rooms with a surface above them (the caustic's, drawn with the
 * sheets only), the sheets' vertices and triangles, the squares of a
 * flipmap twin at a place already counted, and 1 or 0 for whether the
 * sheets read the frame's copy and the depth texture (copy, depth: what the
 * level was built for and the adapter offers; a bind the device refuses sets
 * depth back to 0 - the engine writes the line after its bind, RoomProbe
 * before its picture), the wave rows a sheet sums (waverows), the rings a
 * sheet draw can show (rings), the ripple rows, the table's or the
 * file's, that found a body (sources), the triangles of the remaster's
 * own water art left out under the sheets (art_hidden) and 1 when the
 * sheets clip where the frame's depth lies at their surface - the
 * depth variant, so it follows
 * depth (clip), the waterfall objects whose foot stands over a sheet
 * (falls; a line each, HDRenderWaterFallLine), the squares whose surface is
 * their water room's top and not their own floor (lowered) and those left
 * out as an island or a rim (dropped), both counted per room, the
 * bodies of class river (river), the waterfalls' curtains and the segments
 * of their footprints (curtains, segs), the rows of the file that use the
 * key swirl (old), and the underwater rooms an eye looks through a body
 * from - by the sheet over them or a room above them (under) and by the
 * level's largest body for want of one (under_fallback; see "under the
 * water"), 1 when the level's water rooms take the deep look
 * (under_deep, the same note), 1 when its section is a night one
 * (night; see "the night" over kWaterMoon) and the draw calls of the
 * remaster's water art or a river's flow material whose box reaches a
 * sheet's square without one of their triangles over it, which stay drawn
 * (art_kept; see "the art over a sheet" in HDRenderSetWaterSheets). A
 * body line: its rooms (+n for
 * more than sixteen), the world y of its surface, its squares (each place
 * once) and of those places the ones drawing a sheet, twice the area over
 * the perimeter, open air, the class drawn and the rule's, the deepest
 * square, the light its colours are lit by, the override row that applied
 * - table or file with its room - or none, the tint, wave and foam scales in
 * force, its flipmap twins' squares, the light before gain, floor and cap,
 * 1 when a row of class none took its sheet away, whether the
 * remaster's water art over its sheets is kept or hidden (art), the
 * refraction's scale in force (bend), whether its waterfalls get their
 * foot or a row's `foot off` took it away (the field keeps its name,
 * swirl), its current - the way
 * along world x and z and the speed in units a second, 0,0,0 without one
 * (flow) - and its places whose water names a sink (sinks); the light
 * its colours take, its light or at night the moon's at its luma (lit);
 * last `held`, always 0 - a lifted sheet stands level at every grid
 * point -, the lift, units over the water's top (lift),
 * the land squares its level surface runs on over, each place once
 * (skirt; see "the lift" and "the skirt" in HDRenderSetWaterSheets), how
 * far under its surface the frame is left to itself, units (clip; see
 * kWaterClip), the share of its class's swell its sheet's vertices
 * take (swell; see kWaterBank), how far its rows are short crests in
 * a fan (crest; see "the crests" over kWaterFan), and the world x and z
 * of the ripple row's source its floats drift away from, none without one
 * (spring; HDRenderWaterBody::spring - the line is written before the
 * springs of hd_spring.cpp mark theirs, which their SPRINGLOG lines name). */
bool HDRenderWaterLogLine(int body, const char* stem, int option, char* out,
	size_t size)
{
	if (!out || !size)
		return false;

	if (body < 0)
	{
		int cls[HD_WATER_CLASSES] = {};
		char file[16];

		for (int b = 0; b < HDRenderWaterBodies(); b++)
			cls[g_wbodies[(size_t)b].info.cls]++;

		if (g_wlevel.fileLines >= 0)
			snprintf(file, sizeof(file), "%d", g_wlevel.fileLines);
		else
			snprintf(file, sizeof(file), "none");

		snprintf(out, size, "WATERLOG level=%s option=%d section=%s "
			"sectors=%d sheets=%d built=%d bodies=%d basin=%d channel=%d "
			"sea=%d file=%s rows=%d bad=%d applied=%d unmatched=%d "
			"overlaps=%d caustic_rooms=%d verts=%d tris=%d twins=%d "
			"copy=%d depth=%d waverows=8 rings=%d sources=%d art_hidden=%d "
			"clip=%d falls=%d lowered=%d dropped=%d river=%d curtains=%d "
			"segs=%d old=%d under=%d under_fallback=%d under_deep=%d "
			"night=%d art_kept=%d",
			stem ? stem : "?", option,
			HDRenderWaterSectionName(g_wlevel.section), g_wlevel.sectors,
			g_wlevel.sheets, g_wlevel.built, g_wlevel.bodies,
			cls[HD_WATER_BASIN], cls[HD_WATER_CHANNEL], cls[HD_WATER_SEA],
			file, g_wlevel.fileRows, g_wlevel.fileBad, g_wlevel.rowsApplied,
			g_wlevel.rowsUnmatched, g_wlevel.overlaps, g_wlevel.causticRooms,
			g_wlevel.vertices, g_wlevel.triangles, g_wlevel.twins,
			g_wlevel.copy, g_wlevel.depth, kRingsPerDraw,
			g_wlevel.ringSources, g_wlevel.artHidden, g_wlevel.depth,
			g_wlevel.falls, g_wlevel.lowered, g_wlevel.dropped,
			cls[HD_WATER_RIVER], g_curtains, (int)g_wsegs.size(),
			g_wlevel.oldKeys, g_wlevel.underRooms, g_wlevel.underFallback,
			g_wlevel.underDeep, kWaterPalette[g_wlevel.section].night,
			g_wlevel.artKept);
		return true;
	}

	if (body >= (int)g_wbodies.size())
		return false;

	const HDRenderWaterBody& b = g_wbodies[(size_t)body].info;
	char list[128];
	char from[24];
	float lit[3];
	int at = 0;

	list[0] = 0;
	water_lit(b.light, g_wlevel.section, lit);

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

	char spring[32];

	if (b.spring)
		snprintf(spring, sizeof(spring), "%.0f,%.0f", b.springAt[0],
			b.springAt[1]);
	else
		snprintf(spring, sizeof(spring), "none");

	snprintf(out, size, "WATERLOG body=%d rooms=%s surface=%d sectors=%d "
		"sheets=%d shape=%.2f open=%d class=%s rule=%s depth=%d "
		"light=%.2f,%.2f,%.2f override=%s tint=%.2f,%.2f,%.2f waves=%.2f "
		"foam=%.2f twins=%d raw=%.2f,%.2f,%.2f none=%d art=%s bend=%.2f "
		"swirl=%s flow=%.2f,%.2f,%.0f sinks=%d lying=%d refused=%d "
		"lit=%.2f,%.2f,%.2f held=%d lift=%d skirt=%d clip=%d swell=%.2f "
		"crest=%.2f spring=%s alt=%d",
		body, list,
		b.surfaceY, b.sectors, b.sheets,
		b.perimeter ? 2.0 * b.sectors / b.perimeter : 0.0, b.openAir,
		HDRenderWaterClassName(b.cls), HDRenderWaterClassName(b.ruleCls),
		b.depthMax, b.light[0], b.light[1], b.light[2], from, b.tint[0],
		b.tint[1], b.tint[2], b.waves, b.foam, b.twins, b.raw[0], b.raw[1],
		b.raw[2], b.noSheet, b.artKeep ? "keep" : "hide", b.bend,
		b.footOff ? "off" : "on", b.flow[0], b.flow[1], b.speed, b.sinks,
		b.lying, b.refused, lit[0], lit[1], lit[2], b.held, b.lift,
		b.skirt, b.clip, b.swell, b.crest, spring, b.alt);
	return true;
}

void HDRenderWaterFlipRoom(int room, int other)
{
	if (room >= 0 && (size_t)room < g_wflip.size())
		g_wflip[(size_t)room] ^= 1;

	if (other >= 0 && (size_t)other < g_wflip.size())
		g_wflip[(size_t)other] ^= 1;
}

void HDRenderWaterTiming(int on, double* ms, int* draws)
{
	if (on >= 0)
	{
		g_sheet_timing = on ? true : false;
		g_sheet_gpu_ms = 0.0;
		g_sheet_timed = 0;
		g_copy_gpu_ms = 0.0;
		g_copy_timed = 0;
		g_foot_gpu_ms = 0.0;
		g_foot_timed = 0;
		g_foot_segs = 0;
		g_river_gpu_ms = 0.0;
		g_river_timed = 0;
		g_foam_gpu_ms = 0.0;
		g_foam_timed = 0;
	}

	if (ms)
		*ms = g_sheet_gpu_ms;

	if (draws)
		*draws = g_sheet_timed;
}

void HDRenderWaterCopyTiming(double* ms, int* copies)
{
	if (ms)
		*ms = g_copy_gpu_ms;

	if (copies)
		*copies = g_copy_timed;
}

void HDRenderWaterFootTiming(double* ms, int* draws, int* segs)
{
	if (ms)
		*ms = g_foot_gpu_ms;

	if (draws)
		*draws = g_foot_timed;

	if (segs)
		*segs = g_foot_segs;
}

void HDRenderWaterRiverTiming(double* ms, int* draws)
{
	if (ms)
		*ms = g_river_gpu_ms;

	if (draws)
		*draws = g_river_timed;
}

void HDRenderWaterFoamTiming(double* ms, int* draws)
{
	if (ms)
		*ms = g_foam_gpu_ms;

	if (draws)
		*draws = g_foam_timed;
}

// A tank's film counts only while it is wet: a dry one asks for no copy.
bool HDRenderGroupHasSheet(int group)
{
	if (group < 0 || group >= (int)g_sheet_ranges.size())
		return false;

	for (const SheetRange& r : g_sheet_ranges[(size_t)group])
	{
		const int film = film_of_body(r.body);

		if (body_shown(r.body) &&
			(film < 0 || g_film_wet[(size_t)film] > 0.0f))
			return true;
	}

	return false;
}

/* The copy: render target 0 as it stands, whole and unfiltered, into a
 * texture of its own size and format. StretchRect between two surfaces of
 * one size and format is a plain copy; the back buffer is not multisampled
 * (dxshell.cpp leaves MultiSampleType at none) and holds the colours in the
 * space the shaders write, with sRGB off everywhere, so the copy is read as
 * it is. RoomProbe's timing brackets it with GPU timestamps. */
bool HDRenderWaterCopy()
{
	if (!g_device || !g_copy_allowed || g_copy_valid || !g_wlevel.built)
		return false;

	IDirect3DSurface9* target = 0;
	D3DSURFACE_DESC desc;

	if (FAILED(g_device->GetRenderTarget(0, &target)) || !target)
		return false;

	if (FAILED(target->GetDesc(&desc)) ||
		desc.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		target->Release();
		return false;
	}

	if (g_copy_texture && (g_copy_desc.Width != desc.Width ||
		g_copy_desc.Height != desc.Height || g_copy_desc.Format != desc.Format))
		release_water_copy();

	if (!g_copy_texture)
	{
		if (FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
			D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
			&g_copy_texture, 0)) ||
			FAILED(g_copy_texture->GetSurfaceLevel(0, &g_copy_surface)))
		{
			release_water_copy();
			target->Release();
			return false;
		}

		g_copy_desc = desc;
	}

	IDirect3DQuery9* q[3] = { 0, 0, 0 };

	if (g_sheet_timing &&
		SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,
		&q[0])) &&
		SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
		SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])))
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	g_copy_valid = SUCCEEDED(g_device->StretchRect(target, 0, g_copy_surface,
		0, D3DTEXF_NONE));
	target->Release();

	IDirect3DQuery9* freq_q = 0;

	if (q[2] && SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,
		&freq_q)))
	{
		UINT64 t0 = 0, t1 = 0, freq = 0;

		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		freq_q->Issue(D3DISSUE_END);

		while (freq_q->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
			S_FALSE) {}
		while (q[1]->GetData(&t0, sizeof(t0), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[2]->GetData(&t1, sizeof(t1), D3DGETDATA_FLUSH) == S_FALSE) {}

		if (freq && t1 > t0)
		{
			g_copy_gpu_ms += (double)(t1 - t0) * 1000.0 / (double)freq;
			g_copy_timed++;
		}

		freq_q->Release();
	}

	for (int i = 0; i < 3; i++)
		if (q[i])
			q[i]->Release();

	return g_copy_valid;
}

/* INTZ: a depth-stencil format the adapter can also hand to a sampler, 24
 * bits of depth and 8 of stencil like the D24S8 it stands in for. Asked of
 * the adapter the device runs on, against its display format. */
bool HDRenderWaterDepthOffered()
{
	if (!g_device)
		return false;

	IDirect3D9* d3d = 0;
	D3DDEVICE_CREATION_PARAMETERS made;
	D3DDISPLAYMODE mode;
	bool offered = false;

	if (FAILED(g_device->GetDirect3D(&d3d)) || !d3d)
		return false;

	if (SUCCEEDED(g_device->GetCreationParameters(&made)) &&
		SUCCEEDED(g_device->GetDisplayMode(0, &mode)))
		offered = SUCCEEDED(d3d->CheckDeviceFormat(made.AdapterOrdinal,
			made.DeviceType, mode.Format, D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_TEXTURE, kIntz));

	d3d->Release();
	return offered;
}

/* The INTZ surface in place of the device's depth-stencil, of its size,
 * cleared once (depth 1, stencil 0, over the whole surface) so that the
 * first frame drawn into it finds what a fresh buffer holds; every frame
 * after that clears it where it cleared the device's own. */
bool HDRenderWaterDepthBind(const char** why)
{
	const char* dummy = 0;

	if (!why)
		why = &dummy;

	*why = "ok";

	if (g_depth_texture)
		return true;

	IDirect3DSurface9* own = 0;
	D3DSURFACE_DESC desc;

	if (!g_device || !g_depth_allowed)
		*why = "off";
	else if (!HDRenderWaterDepthOffered())
		*why = "format";
	else if (FAILED(g_device->GetDepthStencilSurface(&own)) || !own)
		*why = "no_depth";
	else if (FAILED(own->GetDesc(&desc)) ||
		desc.MultiSampleType != D3DMULTISAMPLE_NONE)
		*why = "multisample";
	else if (FAILED(g_device->CreateTexture(desc.Width, desc.Height, 1,
		D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT, &g_depth_texture, 0)) ||
		FAILED(g_depth_texture->GetSurfaceLevel(0, &g_depth_surface)) ||
		FAILED(g_device->SetDepthStencilSurface(g_depth_surface)))
		*why = "create";

	if (std::strcmp(*why, "ok"))
	{
		if (g_depth_surface)
			g_depth_surface->Release();

		if (g_depth_texture)
			g_depth_texture->Release();

		if (own)
			own->Release();

		g_depth_surface = 0;
		g_depth_texture = 0;
		g_wlevel.depth = 0;
		return false;
	}

	g_depth_own = own;

	D3DVIEWPORT9 viewport;
	D3DVIEWPORT9 whole = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
	DWORD scissor = FALSE;
	const bool kept = SUCCEEDED(g_device->GetViewport(&viewport));

	g_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetViewport(&whole);
	g_device->Clear(0, 0, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);

	if (kept)
		g_device->SetViewport(&viewport);

	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
	return true;
}

void HDRenderWaterDepthUnbind()
{
	if (g_device && g_depth_own)
		g_device->SetDepthStencilSurface(g_depth_own);

	if (g_depth_own) { g_depth_own->Release(); g_depth_own = 0; }
	if (g_depth_surface) { g_depth_surface->Release(); g_depth_surface = 0; }
	if (g_depth_texture) { g_depth_texture->Release(); g_depth_texture = 0; }
}

bool HDRenderWaterDepthBound()
{
	return g_depth_texture != 0;
}

void HDRenderWaterStage2(int copy, int depth, int view, float bend)
{
	g_copy_allowed = copy != 0;
	g_depth_allowed = depth != 0;
	g_depth_view = view;
	g_bend_scale = bend;
}

int HDRenderWaterBodySquares(int body, HDRenderWaterSquare* out, int max,
	float* rise)
{
	if (body < 0 || body >= (int)g_wbodies.size())
		return -1;

	const HDRenderWaterBody& b = g_wbodies[(size_t)body].info;

	if (rise)
	{
		const HDWaterClass& c = kWaterClass[b.cls];
		float heights = 0.0f;

		for (int i = 0; i < 8; i++)
			heights += c.height[i] * b.waves;

		*rise = c.swell * heights * b.swell +
			(c.caps > 0.0f ? 0.25f * heights * b.swell : 0.0f);
	}

	int count = 0;
	std::vector<long long> seen;

	for (size_t i = 0; i < g_wsquares.size(); i++)
	{
		const WaterSquare& s = g_wsquares[i];

		if (s.body != body)
			continue;

		const long long key = ((long long)s.wx << 32) ^ (unsigned)s.wz;

		if (std::find(seen.begin(), seen.end(), key) != seen.end())
			continue;

		seen.push_back(key);

		if (out && count < max)
		{
			HDRenderWaterSquare& o = out[count];

			o.x = s.wx;
			o.z = s.wz;
			o.y = s.wy;
			o.depth = s.depth;
			o.room = s.room;
			o.sheet = s.sheet;
			o.open = s.open;
		}

		count++;
	}

	return count;
}

void HDRenderWaterRingAdd(float x, float y, float z, float size, double born,
	int little)
{
	WaterRing ring;

	ring.x = x;
	ring.y = y;
	ring.z = z;
	ring.born = born;
	ring.size = size;
	ring.body = little ? 0 : 1;

	if (g_rings.size() >= kRingsKept)
		g_rings.erase(g_rings.begin());

	g_rings.push_back(ring);
}

int HDRenderWaterSpringRows(HDRenderSpringRow* out, int max)
{
	const int n = (int)g_wspring_rows.size();

	for (int i = 0; out && i < n && i < max; i++)
		out[i] = g_wspring_rows[(size_t)i];

	return n;
}

void HDRenderWaterBodySpring(int body, float x, float z)
{
	if (body < 0 || body >= (int)g_wbodies.size())
		return;

	HDRenderWaterBody& b = g_wbodies[(size_t)body].info;

	if (!b.spring)
	{
		b.spring = 1;
		b.springAt[0] = x;
		b.springAt[1] = z;
	}
}

bool HDRenderWaterSpringFrame(HDRenderSpringFrame* out)
{
	if (!out || !g_device || !g_in_frame || !g_wlevel.built)
		return false;

	D3DVIEWPORT9 vp = {};

	g_device->GetViewport(&vp);
	out->device = g_device;
	std::memcpy(out->view, g_view, sizeof(out->view));
	std::memcpy(out->proj, g_proj, sizeof(out->proj));
	out->copy = g_copy_valid ? g_copy_texture : 0;
	out->depth = g_depth_texture;
	out->width = (int)vp.Width;
	out->height = (int)vp.Height;
	out->clock = g_motion[2];
	out->motion = g_motion[3] != 0.0f ? 1 : 0;
	return true;
}

bool HDRenderWaterSpringLook(int body, float* rgb, float* light,
	float* absorb)
{
	if (body < 0 || body >= (int)g_wlooks.size())
		return false;

	const WaterLook& l = g_wlooks[(size_t)body];

	card_tint(body, rgb, light);

	for (int k = 0; k < 3; k++)
		absorb[k] = l.pixel[32 + k];

	return true;
}

namespace
{
	/* The wave rows and the swell RoomVS reads for a room: its largest
	 * body's, or for a room with marked water and no square of its own the
	 * basin's rows and no swell - the vertex shader reads them on marked
	 * vertices alone. */
	void set_room_rows(int group)
	{
		const int body = group >= 0 && group < (int)g_group_body.size() ?
			g_group_body[(size_t)group] : -1;

		if (body >= 0)
		{
			/* z, RoomVS's flood gate: 1 where the body draws its sheet and
			 * hides the remaster's art, so the flood lays no swell, wobble or
			 * surface caustic on the bank (hd_room.hlsl, "the flood under a
			 * sheet"); 0 with tomb5.hd_water 0, which builds no sheet. */
			const HDRenderWaterBody& info = g_wbodies[(size_t)body].info;
			float swell[4];

			std::memcpy(swell, g_wlooks[(size_t)body].swell, sizeof(swell));
			swell[2] = g_wlevel.built && !info.noSheet && !info.artKeep ?
				1.0f : 0.0f;
			swell[3] = 0.0f;
			g_device->SetVertexShaderConstantF(18, swell, 1);
			g_device->SetVertexShaderConstantF(20,
				g_wlooks[(size_t)body].artRows, 4);
			return;
		}

		if (!g_basin_rows_set)
		{
			float total = 0.0f;
			const HDWaterClass& c = kWaterClass[HD_WATER_BASIN];

			water_rows(c, c.baseAngle, 1.0f, g_basin_rows, &total);
			g_basin_rows_set = true;
		}

		const float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		g_device->SetVertexShaderConstantF(18, none, 1);
		g_device->SetVertexShaderConstantF(20, g_basin_rows, 4);
	}

	/* The room's water constants, vertex side: the caustic under a surface
	 * (zero with tomb5.hd_water 0, see g_caustic_on), the eye in room
	 * coordinates, the swell and the four rows. Called by prepare() for
	 * every room. */
	/* Whether the floor under a river takes its caustic from RiverPS,
	 * carried with the current, rather than from RoomPS, standing still:
	 * with the sheets' caustic on, the eye above the water (RiverPS draws
	 * from above alone) and the frame's copy allowed (it reads the floor's
	 * colour there). */
	bool river_caustic_moves()
	{
		return g_caustic_on && !g_under_on && g_copy_allowed;
	}

	void upload_water_room(int group, const float* m)
	{
		float caustic[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if (g_caustic_on && group >= 0 &&
			group < (int)g_caustic_top.size() &&
			g_caustic_top[(size_t)group] > -1.0e8f)
		{
			/* A water room a river is seen through leaves its floor's
			 * caustic to the river's draw while that one lays it; at night
			 * there is none (see "the night" over kWaterMoon). */
			const bool river = group < (int)g_caustic_river.size() &&
				g_caustic_river[(size_t)group] && river_caustic_moves();
			const bool night = kWaterPalette[g_wlevel.section].night != 0;

			caustic[0] = river || night ? 0.0f : g_motion[3];
			caustic[1] = g_caustic_top[(size_t)group];
			caustic[2] = 1.0f / 2048.0f;	// gone two squares down
		}

		/* The eye: the room point the room-to-view matrix (row vectors)
		 * takes to the view's origin, -t M^-1 - inverted in full, so a
		 * mirrored or scaled matrix is answered as well. */
		const float a = m[0], b = m[1], c = m[2];
		const float d = m[4], e = m[5], f = m[6];
		const float g = m[8], h = m[9], k = m[10];
		const float det = a * (e * k - f * h) - b * (d * k - f * g) +
			c * (d * h - e * g);
		float eye[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if (det != 0.0f)
		{
			const float inv[9] =
			{
				(e * k - f * h) / det, (c * h - b * k) / det,
				(b * f - c * e) / det,
				(f * g - d * k) / det, (a * k - c * g) / det,
				(c * d - a * f) / det,
				(d * h - e * g) / det, (b * g - a * h) / det,
				(a * e - b * d) / det
			};

			for (int j = 0; j < 3; j++)
				eye[j] = -(m[12] * inv[j] + m[13] * inv[3 + j] +
					m[14] * inv[6 + j]);
		}

		// w: the side of the water the eye is on, as c148.y (WaterVS,
		// "round the eye").
		eye[3] = g_under_on ? 0.0f : 1.0f;
		g_device->SetVertexShaderConstantF(16, caustic, 1);
		g_device->SetVertexShaderConstantF(17, eye, 1);
		set_room_rows(group);
	}

	/* The rings one sheet draw shows (see "the rings"): the game's, the
	 * file's sources and RoomProbe's, near its squares and its surface, the
	 * youngest kRingsPerDraw of them, each as (x, z, radius, strength) into
	 * `out`. None while tomb5.hd_motion is off - the water stands still.
	 *
	 * A small ring (a bubble, a shell, a rat) comes after every other: a
	 * swimming rat sets up a ripple one tick in sixteen (game\rat.cpp) and
	 * swims 480 units a second, so nearly every one is a ring of its own -
	 * eight rats in ANDY1's pool start about 15 a second - and by age alone
	 * they pushed a wading Lara's ring, and a fall's, out of the eight. */
	int gather_rings(const SheetRange& r, float* out)
	{
		struct Near
		{
			float x, z, size;
			double age;
			int later;	// 1 a small ring, drawn after the rest
			float front, slope;	// a splash's own, when slope > 0
		};

		/* The kept rings, and room for the sources' and the feet's live
		 * ones - a foot re-armed every 0.4 s has nine within kRingLife;
		 * what does not fit is not drawn, the youngest first being kept
		 * only among those that do. */
		Near found[kRingsKept + 32 + 256];
		int n = 0;
		const int cap = (int)(sizeof(found) / sizeof(found[0]));
		const double now = g_motion[2];

		if (g_motion[3] == 0.0f)
			return 0;

		auto add = [&](float x, float y, float z, bool anyY, float size,
			double age, int later)
		{
			if (n >= cap || age < 0.0 || age > kRingLife ||
				x < (float)(r.minX - kRingNear) ||
				x > (float)(r.maxX + kRingNear) ||
				z < (float)(r.minZ - kRingNear) ||
				z > (float)(r.maxZ + kRingNear) ||
				(!anyY && fabsf(y - (float)r.surfaceY) > (float)kRingHeight))
				return;

			found[n].x = x;
			found[n].z = z;
			found[n].size = size;
			found[n].age = age;
			found[n].later = later;
			found[n].front = 0.0f;
			found[n].slope = 0.0f;
			n++;
		};

		/* A splash's two rings (see "the cards"), the youngest of all: its
		 * own front and strength, out of the classic splash's numbers. */
		for (const LiveSplash& s : g_splashes)
		{
			float ring[4];
			const double age = now - s.born;

			if (!HDSplashRings(s.setup, (float)age, ring))
				continue;

			for (int k = 0; k < 2; k++)
			{
				const int before = n;

				add(s.setup.x, s.setup.y, s.setup.z, false, 128.0f, 0.0, 0);

				if (n > before)
				{
					found[n - 1].front = ring[k * 2];
					found[n - 1].slope = ring[k * 2 + 1];
				}
			}
		}

		for (const WaterRing& g : g_rings)
			add(g.x, g.y, g.z, false, g.size, now - g.born, g.body ? 0 : 1);

		for (const WaterSource& s : g_wsources)
			for (double age = std::fmod(now + s.phase, (double)s.period);
				age <= kRingLife; age += s.period)
				add(s.x, s.y, s.z, false, s.size, age, 0);

		// The waterfalls' feet, each a ripple source of its own.
		for (const WaterFoot& f : g_wfeet)
			for (double age = std::fmod(now + f.phase, (double)f.period);
				age <= kRingLife; age += f.period)
				add(f.x, f.y, f.z, false, kFootRingSize, age, 0);

		// RoomProbe's rings carry no height: the box alone places them.
		for (const ProbeRing& p : g_ring_probe)
			add(p.x, 0.0f, p.z, true, p.size, p.age, 0);

		std::sort(found, found + n, [](const Near& a, const Near& b)
		{
			return a.later != b.later ? a.later < b.later : a.age < b.age;
		});

		const int count = n < kRingsPerDraw ? n : kRingsPerDraw;

		for (int i = 0; i < count; i++)
		{
			const float left = 1.0f - (float)(found[i].age / kRingLife);

			// The centre in the frame the shader's world position is in.
			out[i * 4 + 0] = found[i].x - (float)r.originX + g_room_origin[0];
			out[i * 4 + 1] = found[i].z - (float)r.originZ + g_room_origin[1];
			out[i * 4 + 2] = found[i].size * 0.5f +
				kRingSpeed * (float)found[i].age;
			out[i * 4 + 3] = HDRingStrength(kRingSlope, kRingBody,
				found[i].size, left);

			if (found[i].slope > 0.0f)
			{
				out[i * 4 + 2] = found[i].front;
				out[i * 4 + 3] = found[i].slope;
			}
		}

		return count;
	}

	/* The waterfalls' feet over one sheet draw (see "the foot of a
	 * waterfall"): the segments whose zone reaches its squares and whose
	 * surface is its own, at most kFootSegsPerDraw, drawn as a second pass
	 * of the same triangles with FootPS and the same blend. c124 - c131 hold
	 * them as (x1, z1, x2, z2) in the frame the shader's world position is
	 * in, c137 - c144 as (reach, strength, head at the start, head at the
	 * end), the strength the segment's share times the body's foam scale
	 * (a row's `foam`); a gap where no water lands is left out;
	 * gWaterStage.w the count, which WaterPS does not read. A zone's
	 * outline reaches 1.1 of its reach at the most (FootPS: 1.0 of the
	 * patch field times 1.1 of the lobe field). Nothing while
	 * tomb5.hd_motion is off, as the rings: the water stands still.
	 * `pixel` is the draw's c92 - c123, set already. */
	void draw_feet(const SheetRange& r, float* pixel)
	{
		if (!g_water_foot_ps || g_wsegs.empty() || g_motion[3] == 0.0f)
			return;

		float segs[kFootSegsPerDraw * 4] = {};
		float reach[kFootSegsPerDraw * 4] = {};
		int count = 0;

		for (const FootSeg& s : g_wsegs)
		{
			const float m = 1.1f * s.reach;
			const float x0 = s.x1 < s.x2 ? s.x1 : s.x2;
			const float x1 = s.x1 < s.x2 ? s.x2 : s.x1;
			const float z0 = s.z1 < s.z2 ? s.z1 : s.z2;
			const float z1 = s.z1 < s.z2 ? s.z2 : s.z1;

			if (count >= kFootSegsPerDraw || s.wet <= 0.0f ||
				fabsf(s.y - (float)r.surfaceY) > (float)kRingHeight ||
				x1 < (float)r.minX - m || x0 > (float)r.maxX + m ||
				z1 < (float)r.minZ - m || z0 > (float)r.maxZ + m)
				continue;

			const float foam = s.body >= 0 && s.body < (int)g_wbodies.size() ?
				g_wbodies[(size_t)s.body].info.foam : 1.0f;
			const float ox = g_room_origin[0] - (float)r.originX;
			const float oz = g_room_origin[1] - (float)r.originZ;

			segs[count * 4 + 0] = s.x1 + ox;
			segs[count * 4 + 1] = s.z1 + oz;
			segs[count * 4 + 2] = s.x2 + ox;
			segs[count * 4 + 3] = s.z2 + oz;
			reach[count * 4 + 0] = s.reach;
			reach[count * 4 + 1] = s.wet * foam;
			reach[count * 4 + 2] = s.headA;
			reach[count * 4 + 3] = s.headB;
			count++;
		}

		if (!count)
			return;

		pixel[16 + 10 * 4 + 3] = (float)count;
		g_device->SetPixelShader(g_water_foot_ps);
		g_device->SetPixelShaderConstantF(106, pixel + 16 + 10 * 4, 1);
		g_device->SetPixelShaderConstantF(124, segs, kFootSegsPerDraw);
		g_device->SetPixelShaderConstantF(137, reach, kFootSegsPerDraw);

		IDirect3DQuery9* q[3] = { 0, 0, 0 };

		if (g_sheet_timing &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,
			&q[0])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])))
		{
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
		}

		g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, r.minVertex,
			r.vertices, r.start, r.tris);
		g_draw_calls++;

		IDirect3DQuery9* freq_q = 0;

		if (q[2] && SUCCEEDED(g_device->CreateQuery(
			D3DQUERYTYPE_TIMESTAMPFREQ, &freq_q)))
		{
			UINT64 t0 = 0, t1 = 0, freq = 0;

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			freq_q->Issue(D3DISSUE_END);

			while (freq_q->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[1]->GetData(&t0, sizeof(t0), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[2]->GetData(&t1, sizeof(t1), D3DGETDATA_FLUSH) ==
				S_FALSE) {}

			if (freq && t1 > t0)
			{
				g_foot_gpu_ms += (double)(t1 - t0) * 1000.0 / (double)freq;
				g_foot_timed++;
				g_foot_segs += count;
			}

			freq_q->Release();
		}

		for (int i = 0; i < 3; i++)
			if (q[i])
				q[i]->Release();
	}

	/* The river's second draw (hd_room.hlsl, RiverPS): its streaks and the
	 * foam where it ends, over the sheet just drawn, with the same blend.
	 * c132, c133, c146 and c147 are the look's river constants; the rest of
	 * c92 - c123 is the sheet's, set already - the rows the relief's light
	 * reads among them. Nothing while tomb5.hd_motion is off:
	 * the streaks are the water running. A channel with a current
	 * (WaterLook::hasEnd) takes the same draw with its end foam alone, in
	 * the rooms whose sheet sees the end ahead (SheetRange::endNear).
	 */
	void draw_river(const SheetRange& r)
	{
		const WaterLook& look = g_wlooks[(size_t)r.body];

		if (!g_water_river_ps || (!look.isRiver && !(look.hasEnd &&
			r.endNear)) || g_motion[3] == 0.0f)
			return;

		/* The caustic on the floor rides with the current where RoomPS's
		 * own is off (upload_water_room): the eye above the water and the
		 * frame's copy taken, which RiverPS reads the floor from; at night
		 * at kNightCaustic of its strength (river[11], the night's share). */
		float river[16];

		std::memcpy(river, look.river, sizeof(river));
		river[7] = !look.isRiver || !river_caustic_moves() ? 0.0f :
			river[11] > 0.5f ? kNightCaustic : 1.0f;
		g_device->SetPixelShader(g_water_river_ps);
		g_device->SetPixelShaderConstantF(132, river, 2);
		g_device->SetPixelShaderConstantF(146, river + 8, 2);

		IDirect3DQuery9* q[3] = { 0, 0, 0 };

		if (g_sheet_timing &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,
			&q[0])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])))
		{
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
		}

		g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, r.minVertex,
			r.vertices, r.start, r.tris);
		g_draw_calls++;

		IDirect3DQuery9* freq_q = 0;

		if (q[2] && SUCCEEDED(g_device->CreateQuery(
			D3DQUERYTYPE_TIMESTAMPFREQ, &freq_q)))
		{
			UINT64 t0 = 0, t1 = 0, freq = 0;

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			freq_q->Issue(D3DISSUE_END);

			while (freq_q->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[1]->GetData(&t0, sizeof(t0), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[2]->GetData(&t1, sizeof(t1), D3DGETDATA_FLUSH) ==
				S_FALSE) {}

			if (freq && t1 > t0)
			{
				g_river_gpu_ms += (double)(t1 - t0) * 1000.0 / (double)freq;
				g_river_timed++;
			}

			freq_q->Release();
		}

		for (int i = 0; i < 3; i++)
			if (q[i])
				q[i]->Release();
	}

	/* One sheet draw with the timing round it, when the tool asks. */
	void draw_sheet_range(const SheetRange& r)
	{
		IDirect3DQuery9* q[3] = { 0, 0, 0 };

		if (g_sheet_timing &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,
			&q[0])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,
			&q[1])) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,
			&q[2])))
		{
			q[0]->Issue(D3DISSUE_BEGIN);
			q[1]->Issue(D3DISSUE_END);
		}

		g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, r.minVertex,
			r.vertices, r.start, r.tris);
		g_draw_calls++;

		IDirect3DQuery9* freq_q = 0;

		if (q[2] && SUCCEEDED(g_device->CreateQuery(
			D3DQUERYTYPE_TIMESTAMPFREQ, &freq_q)))
		{
			UINT64 t0 = 0, t1 = 0, freq = 0;

			q[2]->Issue(D3DISSUE_END);
			q[0]->Issue(D3DISSUE_END);
			freq_q->Issue(D3DISSUE_END);

			while (freq_q->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[1]->GetData(&t0, sizeof(t0), D3DGETDATA_FLUSH) ==
				S_FALSE) {}
			while (q[2]->GetData(&t1, sizeof(t1), D3DGETDATA_FLUSH) ==
				S_FALSE) {}

			if (freq && t1 > t0)
			{
				g_sheet_gpu_ms += (double)(t1 - t0) * 1000.0 / (double)freq;
				g_sheet_timed++;
			}

			freq_q->Release();
		}

		for (int i = 0; i < 3; i++)
			if (q[i])
				q[i]->Release();
	}

	/* The sheet's pixel shader: the plain variant without the depth
	 * texture, RoomProbe's view variant when a view is asked for, the
	 * HD_WATER_UNDER variant while the eye is under the water (see "under
	 * the water"), the depth variant otherwise. */
	IDirect3DPixelShader9* sheet_ps(bool depth)
	{
		return !depth ? g_water_ps : g_depth_view && g_water_view_ps ?
			g_water_view_ps : g_under_on && g_water_under_ps ?
			g_water_under_ps : g_water_depth_ps;
	}

	/* The sheet of one room, in its blend phase: one draw per body, with
	 * premultiplied alpha (ONE / INVSRCALPHA), two-sided and without a depth
	 * write, then the states every pass of draw_pass leaves behind. */
	void draw_water_sheet(int group)
	{
		if (!g_sheet_vb || !g_sheet_ib || !g_water_vs || !g_water_ps ||
			group < 0 || group >= (int)g_sheet_ranges.size() ||
			g_sheet_ranges[(size_t)group].empty())
			return;

		/* The depth variant while the INTZ surface is the depth buffer, and
		 * the frame behind the sheet on s5 and the depth texture on s6 for
		 * this draw alone: the depth writes are off while the texture is
		 * read, and both are unbound again below, before anything writes
		 * depth into it. */
		const bool depth = g_depth_texture && g_water_depth_ps;
		D3DSURFACE_DESC size = g_copy_desc;

		if (depth)
			g_depth_texture->GetLevelDesc(0, &size);

		// For the mark's log line: the surface the split tests.
		if (!g_mark_logged && !g_mark_sheets++)
			g_mark_sheet_surface = depth_is_intz();

		// The viewport the projection maps to, for the rows' fade.
		D3DVIEWPORT9 view_port = {};

		if (FAILED(g_device->GetViewport(&view_port)))
		{
			view_port.Width = size.Width;
			view_port.Height = size.Height;
		}

		// A depth view of RoomProbe's draws with the view variant.
		if (depth && g_depth_view && !g_water_view_ps &&
			FAILED(g_device->CreatePixelShader(
			(const DWORD*)kHDRoomWaterViewPS, &g_water_view_ps)))
			g_water_view_ps = 0;

		g_device->SetVertexShader(g_water_vs);
		g_device->SetPixelShader(sheet_ps(depth));
		g_device->SetStreamSource(0, g_sheet_vb, 0, sizeof(HDVertex));
		g_device->SetIndices(g_sheet_ib);
		g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

		for (DWORD s = 5; s <= 6; s++)
		{
			const DWORD filter = s == 5 ? D3DTEXF_LINEAR : D3DTEXF_POINT;

			g_device->SetSamplerState(s, D3DSAMP_MINFILTER, filter);
			g_device->SetSamplerState(s, D3DSAMP_MAGFILTER, filter);
			g_device->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			g_device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
		}

		g_device->SetTexture(5, g_copy_valid ? g_copy_texture : 0);
		g_device->SetTexture(6, depth ? g_depth_texture : 0);

		// The stencil states the split below sets, as it found them.
		const bool split = depth && g_mark_frame;
		DWORD stencil[kMarkStateCount] = {};

		if (split)
			for (int i = 0; i < kMarkStateCount; i++)
				g_device->GetRenderState(kMarkStates[i], &stencil[i]);

		for (const SheetRange& r : g_sheet_ranges[(size_t)group])
		{
			// Flipped water only while its room is swapped.
			if (!body_shown(r.body))
				continue;

			/* A tank's film: only while wet, only with the copy, and
			 * cross-faded by the blend factor (see "the tank's film"). */
			const int film = film_of_body(r.body);
			const float wet = film < 0 ? 1.0f : g_film_wet[(size_t)film];

			if (film >= 0 && (wet <= 0.0f || !g_copy_valid ||
				!(g_caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR)))
				continue;

			const WaterLook& look = g_wlooks[(size_t)r.body];
			float pixel[32 * 4] = {};
			float rows[32];
			float lag[32];
			float heights = 0.0f;
			float lagHeights = 0.0f;
			const double clock = (double)g_motion[2] * g_motion[3];

			// c92 - c95 and c107 - c110: the eight rows now; c111 the two
			// longest half a second ago (gWaterLag).
			water_rows_now(look, clock, rows, &heights);
			water_rows_now(look, clock - 0.5, lag, &lagHeights);
			std::memcpy(pixel, rows, 16 * sizeof(float));
			std::memcpy(pixel + 16, look.pixel, sizeof(look.pixel));
			pixel[16 + 7 * 4 + 2] = (float)clock;	// the clock
			pixel[16 + 7 * 4 + 3] = heights > 1.0f ? heights : 1.0f;
			std::memcpy(pixel + 15 * 4, rows + 16, 16 * sizeof(float));
			pixel[19 * 4 + 0] = lag[2];
			pixel[19 * 4 + 1] = lag[6];
			pixel[19 * 4 + 2] = lag[3];
			pixel[19 * 4 + 3] = lag[7];
			// c112 gWaterLife, c113 - c120 the rings, c121 - c122 gWaveK.
			pixel[20 * 4 + 0] = look.life[0];
			pixel[20 * 4 + 1] = look.life[1];
			pixel[20 * 4 + 2] = (float)gather_rings(r, pixel + 21 * 4);
			pixel[20 * 4 + 3] = look.foamLight;

			for (int i = 0; i < 8; i++)
				pixel[29 * 4 + i] = sqrtf(rows[i * 4] * rows[i * 4] +
					rows[i * 4 + 1] * rows[i * 4 + 1]);

			/* c18, gSwell for WaterVS: the class's swell and taper, and the
			 * breaking crest's lift - a quarter of the whole height now
			 * above the whitecaps' threshold (gWaterSun.w of that height),
			 * where the body has whitecaps at all (gWaterMisc.y). A quarter
			 * is a free choice, set by eye on the round's RoomProbe
			 * pictures. */
			const float rise = g_wbodies[(size_t)r.body].info.swell;
			float swell[4] = { look.swell[0] * rise, look.swell[1], 0.0f,
				0.0f };

			if (look.pixel[28 + 1] > 0.0f)
			{
				swell[2] = 0.25f * heights * rise;
				swell[3] = look.pixel[20 + 3] * heights;
			}

			/* A film's height and its rim's foam follow its strength (see
			 * "the tank's film"). */
			if (film >= 0)
			{
				swell[2] = kFilmRise * wet;
				swell[3] = -1.0e6f;
				pixel[16 + 24 + 3] *= wet;
			}

			// c105, gWaterScreen: the target's pixel and the projection.
			pixel[16 + 9 * 4 + 0] = size.Width ? 1.0f / (float)size.Width :
				0.0f;
			pixel[16 + 9 * 4 + 1] = size.Height ? 1.0f / (float)size.Height :
				0.0f;
			pixel[16 + 9 * 4 + 2] = g_proj[10];
			pixel[16 + 9 * 4 + 3] = -g_proj[14];
			/* c106, gWaterStage: the copy, and RoomProbe's depth view; w
			 * the shallows (WaterPS, "the shallows"): one over the depth
			 * from which a lifted body's rows keep their whole height, 2.56
			 * times that height now - a wave breaks where its height passes
			 * 0.78 of the depth (the breaking index of a solitary wave, the
			 * textbook figure), so the rows' amplitude, half the height, may
			 * reach 0.39 of it. 1e9 for a body without a lift: its rows
			 * keep their height at every depth. FootPS's pass writes its
			 * count over it. */
			pixel[16 + 10 * 4 + 0] = g_copy_valid ? 1.0f : 0.0f;
			pixel[16 + 10 * 4 + 1] = look.eyeGain - 1.0f;
			pixel[16 + 10 * 4 + 2] = depth ? (float)g_depth_view : 0.0f;
			pixel[16 + 10 * 4 + 3] = g_wbodies[(size_t)r.body].info.lift > 0 ?
				1.0f / (2.56f * (heights > 1.0f ? heights : 1.0f)) : 1.0e9f;
			pixel[16 + 8 * 4 + 3] *= g_bend_scale;

			/* c123, gWaterLara: her place in the frame the shader's world
			 * position is in, or far away while she is out of the water. */
			pixel[31 * 4 + 0] = g_lara_in_water ? g_lara_x -
				(float)r.originX + g_room_origin[0] : 1.0e7f;
			pixel[31 * 4 + 1] = g_lara_in_water ? g_lara_z -
				(float)r.originZ + g_room_origin[1] : 1.0e7f;
			pixel[31 * 4 + 2] = kWaterLaraRadius;
			pixel[31 * 4 + 3] = 1.0f / kWaterLaraEdge;

			/* c19, gWaterView: half the viewport in pixels (the rows' fade);
			 * zw how far the patches have drifted by the clock. */
			const float view[4] = { view_port.Width * 0.5f,
				view_port.Height * 0.5f, (float)(look.drift[0] * clock),
				(float)(look.drift[1] * clock) };

			/* The vertex rows in WaterVS's phase form, c20 - c27: the next
			 * room's prepare() writes c20 - c23 back in RoomVS's form before
			 * RoomVS draws again - the sheet is the last draw of its call. */
			g_device->SetVertexShaderConstantF(18, swell, 1);
			g_device->SetVertexShaderConstantF(19, view, 1);
			g_device->SetVertexShaderConstantF(20, rows, 8);
			g_device->SetPixelShaderConstantF(92, pixel, 32);

			/* c148, gWaterClip: x the body's clip, units (kWaterClip, a
			 * row's `clip`); y 1 while the eye is in the air, 0 under the
			 * water, the side the sheet is seen from - read by WaterPS
			 * alone. */
			const float clip[4] = {
				(float)g_wbodies[(size_t)r.body].info.clip,
				g_under_on ? 0.0f : 1.0f, 0.0f, 0.0f };

			g_device->SetPixelShaderConstantF(148, clip, 1);

			/* c149 - c159, and the vertex side's c168 - c178, the crests
			 * (hd_room.hlsl, CrestRows and WaveBend; read by WaterVS,
			 * WaterPS and RiverPS): per four rows the step along the
			 * crest in cells a unit along world x and z - the row's crest
			 * line (-kz, kx) over |k| times the cell's length, crestLen
			 * wavelengths of 2 pi / k or kCrestReach units, whichever is
			 * shorter - and the number its cells across are counted from:
			 * the whole cells (kCrestAcross turns) the row's phase has
			 * run, which water_rows_now takes off, negated, plus
			 * kCrestOffset times the row, modulo 4096, the cells' hash's
			 * period - so a crest keeps its cell while it travels and no
			 * two rows count alike; then 1 - C, C kCrestGain and C (C the
			 * pieces' share, the class's crestDepth times the body's
			 * `crest`) and the squeeze's push, kBendPush of the longest
			 * wavelength times `crest`, over the longest row's wave number
			 * (the shader takes it along that row's wave vector); last the
			 * eight phases and the eight heights four to a register. The
			 * vertex side takes kCrestSwellGain in place of kCrestGain. */
			float crest[11 * 4];
			float k0 = 1.0e-6f;

			for (int i = 0; i < 8; i++)
			{
				const int at = (i < 4 ? 0 : 12) + (i & 3);
				const double turn = 6.283185307179586;
				const double run = std::floor(((double)look.rows[i * 4 + 2] *
					clock + (double)look.phase0[i]) / (turn * kCrestAcross));
				const double from = std::fmod((double)kCrestOffset * i - run,
					4096.0);
				// The cell's length in units, at most kCrestReach.
				const float k = sqrtf(rows[i * 4] * rows[i * 4] +
					rows[i * 4 + 1] * rows[i * 4 + 1]);
				const float own = look.crestLen * 6.2831853f /
					(k > 1.0e-6f ? k : 1.0e-6f);
				const float units = own < kCrestReach ? own : kCrestReach;
				const float perCell = k > 0.0f ? 1.0f / (units * k) : 0.0f;

				crest[at] = -rows[i * 4 + 1] * perCell;
				crest[at + 4] = rows[i * 4 + 0] * perCell;
				crest[at + 8] = (float)(from < 0.0 ? from + 4096.0 : from);
				crest[28 + i] = rows[i * 4 + 2];
				crest[36 + i] = rows[i * 4 + 3];

				if (i == 0 && k > k0)
					k0 = k;
			}

			crest[24] = 1.0f - look.crestDepth;
			crest[25] = kCrestGain * look.crestDepth;
			crest[26] = look.crestDepth;
			crest[27] = kBendPush * 6.2831853f * look.crest / (k0 * k0);
			g_device->SetPixelShaderConstantF(149, crest, 11);
			crest[25] = kCrestSwellGain * look.crestDepth;
			g_device->SetVertexShaderConstantF(168, crest, 11);

			if (film >= 0)
			{
				const DWORD f = (DWORD)(wet >= 1.0f ? 255.0f : wet * 255.0f);

				g_device->SetRenderState(D3DRS_BLENDFACTOR,
					D3DCOLOR_ARGB(f, f, f, f));
				g_device->SetRenderState(D3DRS_SRCBLEND,
					D3DBLEND_BLENDFACTOR);
				g_device->SetRenderState(D3DRS_DESTBLEND,
					D3DBLEND_INVBLENDFACTOR);
				draw_sheet_range(r);
				g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				g_device->SetRenderState(D3DRS_DESTBLEND,
					D3DBLEND_INVSRCALPHA);
				g_device->SetRenderState(D3DRS_BLENDFACTOR, 0xFFFFFFFF);
			}
			else
				draw_sheet_range(r);

			/* The foam's own draw (hd_room.hlsl, "the foam's draw"): over
			 * the sheet just drawn, for a body with bank foam or whitecaps,
			 * from above - seen from below a sheet has no foam - and not
			 * under RoomProbe's depth views, which show no foam.
			 *
			 * The creatures' mark (HDRenderWaterMarkBegin): with the depth
			 * variant and a frame that marked anything, the foam is drawn
			 * on the unmarked pixels alone, split by the stencil. On a
			 * marked body no foam at all: the line, the bank's foam and its
			 * patches and the whitecaps - a rat 50 to 60 units down in a
			 * small pool lies inside the bank's band, and a patch there
			 * would paint its back as the line did. */
			IDirect3DPixelShader9* foam_ps = depth ? g_water_foam_depth_ps :
				g_water_foam_ps;

			if (foam_ps && !g_under_on && !(depth && g_depth_view) &&
				(look.pixel[24 + 3] > 0.0f || look.pixel[28 + 1] > 0.0f))
			{
				if (split)
				{
					g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
					g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,
						FALSE);
					g_device->SetRenderState(D3DRS_STENCILFUNC,
						D3DCMP_EQUAL);
					g_device->SetRenderState(D3DRS_STENCILREF, 0);
					g_device->SetRenderState(D3DRS_STENCILMASK,
						kCreatureStencilBit);
					g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
					g_device->SetRenderState(D3DRS_STENCILPASS,
						D3DSTENCILOP_KEEP);
					g_device->SetRenderState(D3DRS_STENCILFAIL,
						D3DSTENCILOP_KEEP);
					g_device->SetRenderState(D3DRS_STENCILZFAIL,
						D3DSTENCILOP_KEEP);
				}

				// Timed apart from the sheet's draws (RoomProbe -hdwater).
				const double sheetMs = g_sheet_gpu_ms;
				const int sheetTimed = g_sheet_timed;

				g_device->SetPixelShader(foam_ps);
				draw_sheet_range(r);
				g_device->SetPixelShader(sheet_ps(depth));
				g_foam_gpu_ms += g_sheet_gpu_ms - sheetMs;
				g_foam_timed += g_sheet_timed - sheetTimed;
				g_sheet_gpu_ms = sheetMs;
				g_sheet_timed = sheetTimed;
				g_mark_foam++;

				if (split)
				{
					g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
					g_mark_split++;
				}
			}

			if (film >= 0)
				continue;	// a film has no falls' feet and is no river

			draw_feet(r, pixel);
			draw_river(r);
			g_device->SetPixelShader(sheet_ps(depth));
		}

		if (split)
			for (int i = 0; i < kMarkStateCount; i++)
				g_device->SetRenderState(kMarkStates[i], stencil[i]);

		g_device->SetTexture(5, 0);
		g_device->SetTexture(6, 0);
		set_room_rows(group);
		g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		g_device->SetVertexShader(g_vs);
		g_device->SetPixelShader(g_ps);
		g_device->SetStreamSource(0, g_vb, 0, sizeof(HDVertex));
		g_device->SetIndices(g_ib);
	}
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
	{
		draw_pass(room, 1, wvp, true, window);

		/* The water sheet, after the room's glass and once per room: with
		 * the whole room's call or the open half's, never with the pieces
		 * of its intruding art. */
		if (!g_marble_replay &&
			(window == HD_WINDOW_ALL || window == HD_WINDOW_OPEN))
			draw_water_sheet(group);
	}

	marble_note(0, group, world, lighting, passes, window);
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
 * far inside the far plane, and the cloud discs, out to 24,000 units, are
 * drawn whole: the far plane cuts objects and sprites, not this group, and
 * its depth stays under 1 at any distance.
 *
 * The depth: the sky must be behind everything, so it is drawn with its own
 * depth range: the viewport's MinZ is pushed to HD_SKY_MINZ for the length of
 * this call, which folds the whole sky into the last thousandth of the depth
 * buffer. Outside that slice every pixel of the world is nearer, because the
 * depth curve a - b/z is built on ZFAR_SHIPPED (specific\3dmath.cpp,
 * SetupZRange): 0.99500 at 20,480 units, 0.99550 at the far plane of
 * ZFAR_UNITS, and under a = 0.99597 at any distance, so nothing the engine
 * draws reaches the slice. The viewport is read back and put together again
 * before the call returns.
 *
 * The order inside the sky: the flat cloud sheets (RoomProbe -skycensus: a
 * square of 5 x 5 or 10 x 10 tiles of one page, 1,752 to 4,996 units above
 * the camera) are nearer than the band's sphere (radius 4,100; ANDREA3's
 * 4,891, its centre 818 units off the camera) only overhead, above
 * the elevation where their plane meets it (30 to 71 degrees), and the band
 * is see-through there - its page is alpha 0 above about 27 degrees. Sorted
 * by depth, a sheet would be cut off along that circle wherever the band
 * stands in front of it, and the band's own empty half, drawn with the depth
 * write on, hides every sheet behind the sphere. So the alpha pass is drawn
 * as a painter's picture in the order the .TRG lists its parts, which is back
 * to front in every sky level: the sheets from the highest to the lowest,
 * the band last, its haze laid over the sheets towards the horizon as the
 * page's alpha ramp asks. The depth test is off for that pass alone. An
 * opaque sheet (ANDY's and RICH's 7112, ANDREA2's 7109) is nearer than the
 * band above the elevation where it meets the sphere (36.5 to 71.1 degrees),
 * and the band drawn over it there adds nothing: its page is alpha 0 at and
 * above each of those crossings (RoomProbe -skydump's band rows against the
 * band page's alpha). Every pixel of the world still comes after the sky
 * and is nearer. */
#define HD_SKY_MINZ	0.996f

int HDRenderSkyGroups()
{
	const int groups = (int)g_groups.size();

	return groups > g_room_groups ? groups - g_room_groups : 0;
}

/* The sky group's alpha pass in the .TRG's part order. The .TRG writes each
 * part's vertices as one block in part order (RoomProbe -skycensus,
 * vertices=, in all thirteen sky levels), so the lowest vertex a range names
 * keeps that order where the build's range sort put the band first. Sorted
 * once a level; later frames find the list in order and move nothing. */
static void HDRenderSkyPartOrder(std::vector<HDRange>& ranges)
{
	const auto before = [](const HDRange& a, const HDRange& b)
	{
		return a.baseVertex < b.baseVertex;
	};

	if (!std::is_sorted(ranges.begin(), ranges.end(), before))
		std::stable_sort(ranges.begin(), ranges.end(), before);
}

/* The band's closed poles (hd_sky_cap.h, HDSkyCapBuild): the band ends at
 * its last ring on either side and leaves the pole beyond to the frame's
 * clear, which without a cap is a dark disc straight overhead in JOBY4,
 * where the band is the whole sky seen from below the water. Each pole
 * gets a disc in the last ring's own page and texture row, built into the
 * band's own run as the .TRG is read: the same pass, the same draw call,
 * the same depth slice, and the look's light (HDRenderWaterLook) laid over
 * it as over the band. 64 triangles and 64 corners per band, two bands in
 * TITLE; a band row the alpha test cuts away (the cloud levels above about
 * 27 degrees) draws nothing in its disc either. On while HDRenderSkyCapSet
 * leaves it on - always in the game. */
namespace
{
	bool g_sky_cap_on = true;
	HDSkyCapCount g_sky_cap_last = { 0, 0, 0, 0 };
}

void HDRenderSkyCapSet(bool on)
{
	g_sky_cap_on = on;
}

HDSkyCapCount HDRenderSkyCapLast()
{
	return g_sky_cap_last;
}

void HDRenderSkyCaps(trf::remaster::Trg& trg, int numRooms)
{
	const int rooms = numRooms > 0 && numRooms < (int)trg.groups.size()
		? numRooms : (int)trg.groups.size();
	const HDSkyCapCount none = { 0, 0, 0, 0 };

	g_sky_cap_last = g_sky_cap_on ? HDSkyCapBuild(trg, rooms) : none;
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

	DWORD zfunc = D3DCMP_LESSEQUAL;

	g_device->GetRenderState(D3DRS_ZFUNC, &zfunc);

	bool drew = false;

	for (int g = g_room_groups; g < (int)g_groups.size(); g++)
	{
		HDRenderSkyPartOrder(g_groups[(size_t)g].pass[1]);
		drew = HDRenderRoomMask(g, world, 0, HD_PASS_OPAQUE) || drew;
		g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		drew = HDRenderRoomMask(g, world, 0, HD_PASS_ALPHA) || drew;
		g_device->SetRenderState(D3DRS_ZFUNC, zfunc);
		drew = HDRenderRoomMask(g, world, 0,
			HD_PASS_ALL & ~(unsigned int)HD_PASS_SOLID) || drew;
	}

	g_device->SetViewport(&saved);
	return drew;
}

/* Whether the classic flat layers stay out under the HD sky: where it
 * stands for them - a cloud sheet in any sky group, or a band in the
 * opaque pass, TITLE's two whole painted skies - and in a level that takes
 * the deep look with the sheets on (HDSkyLayersLeftOut, hd_water_under.h).
 * The band of every other level is an alpha-pass part with a sheet over
 * it, except JOBY4's (RoomProbe -skycensus), which is the band alone and
 * one flat colour; JOBY4's script layers are the sea's surface seen from
 * below on the level's classic page, and JOBY4 takes the deep look, whose
 * surface the remaster's art of room 19 and HDRenderWaterLook draw
 * instead. The look comes only with the sheets (g_caustic_on,
 * tomb5.hd_water); with them off the layers stay in, as before, drawn
 * over the band and its closed pole (HDRenderSkyCaps).
 * RICH3 takes the deep look too (its one water room no sheet lies over,
 * the flipped room 190, against none under a sheet), and its sheets leave
 * the layers out already. */
bool HDRenderSkyCoversLayers()
{
	int sheet = 0;
	int opaque = 0;

	for (int g = g_room_groups; g < (int)g_groups.size(); g++)
		for (int pass = 0; pass < 2; pass++)
			for (const HDRange& range : g_groups[(size_t)g].pass[pass])
				if (range.count && pass == 0)
					opaque = 1;
				else if (range.count && range.flatLight == 0.0f)
					sheet = 1;

	return HDSkyLayersLeftOut(sheet, opaque,
		g_wdeep_on && g_caustic_on ? 1 : 0);
}

/* The page of the HD sky's nearest cloud sheet: of the alpha pass's sheets
 * the last in part order, which is the lowest and so the nearest over the
 * camera (HDRenderSky). ANDY1 to ANDY3, the levels whose layer 1 carries the
 * lightning, have 7116 at 4,259 and 7113 at 3,550 units and give 7113. 0
 * where the sky has no sheet in its alpha pass. */
IDirect3DTexture9* HDRenderSkyLayerPage()
{
	const HDRange* nearest = 0;

	for (int g = g_room_groups; g < (int)g_groups.size(); g++)
		for (const HDRange& range : g_groups[(size_t)g].pass[1])
			if (range.count && range.flatLight == 0.0f &&
				range.pageSlot >= 0 &&
				(!nearest || range.baseVertex > nearest->baseVertex))
				nearest = &range;

	return nearest ? HDPageCacheSlotTexture(nearest->pageSlot) : 0;
}

#include "hd_shader/hd_sky_layer_vs.inc"
#include "hd_shader/hd_sky_layer_ps.inc"

namespace
{
	IDirect3DVertexShader9* g_sky_layer_vs = 0;
	IDirect3DPixelShader9* g_sky_layer_ps = 0;
	IDirect3DDevice9* g_sky_shader_device = 0;

	/* The sky's probe shaders, made on first use on the device in hand.
	 * Only RoomProbe draws with them, and it keeps one device for its
	 * whole run; the game never reaches this. */
	bool sky_probe_shaders()
	{
		if (g_sky_shader_device != g_device)
		{
			g_sky_layer_vs = 0;
			g_sky_layer_ps = 0;
			g_sky_shader_device = g_device;
		}

		if (!g_sky_layer_vs && FAILED(g_device->CreateVertexShader(
			(const DWORD*)kHDSkyLayerVS, &g_sky_layer_vs)))
			g_sky_layer_vs = 0;

		if (!g_sky_layer_ps && FAILED(g_device->CreatePixelShader(
			(const DWORD*)kHDSkyLayerPS, &g_sky_layer_ps)))
			g_sky_layer_ps = 0;

		return g_sky_layer_vs && g_sky_layer_ps;
	}

	/* What the probe's sky draws change, as it was: the render states and
	 * the stage 0 sampler states below, the shaders, the declaration, stream
	 * 0 (which DrawPrimitiveUP leaves empty), the page and the constants the
	 * layer shader takes (vertex c0 - c3, pixel c0 - c1, which the room
	 * shaders read as well). */
	const D3DRENDERSTATETYPE kSkyProbeStates[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_CULLMODE,
		D3DRS_ALPHATESTENABLE
	};

	const D3DSAMPLERSTATETYPE kSkyProbeSamplers[] =
	{
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER,
		D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER
	};

	struct SkyProbeState
	{
		DWORD rs[sizeof(kSkyProbeStates) / sizeof(kSkyProbeStates[0])];
		DWORD ss[sizeof(kSkyProbeSamplers) / sizeof(kSkyProbeSamplers[0])];
		IDirect3DVertexShader9* vs;
		IDirect3DPixelShader9* ps;
		IDirect3DBaseTexture9* page;
		IDirect3DVertexDeclaration9* decl;
		IDirect3DVertexBuffer9* stream;
		UINT offset, stride;
		float vc[16];
		float pc[8];

		void save()
		{
			for (size_t i = 0; i < sizeof(rs) / sizeof(rs[0]); i++)
				g_device->GetRenderState(kSkyProbeStates[i], &rs[i]);

			for (size_t i = 0; i < sizeof(ss) / sizeof(ss[0]); i++)
				g_device->GetSamplerState(0, kSkyProbeSamplers[i], &ss[i]);

			g_device->GetVertexShader(&vs);
			g_device->GetPixelShader(&ps);
			g_device->GetTexture(0, &page);
			g_device->GetVertexDeclaration(&decl);
			g_device->GetStreamSource(0, &stream, &offset, &stride);
			g_device->GetVertexShaderConstantF(0, vc, 4);
			g_device->GetPixelShaderConstantF(0, pc, 2);
		}

		void restore()
		{
			for (size_t i = 0; i < sizeof(rs) / sizeof(rs[0]); i++)
				g_device->SetRenderState(kSkyProbeStates[i], rs[i]);

			for (size_t i = 0; i < sizeof(ss) / sizeof(ss[0]); i++)
				g_device->SetSamplerState(0, kSkyProbeSamplers[i], ss[i]);

			g_device->SetVertexShader(vs);
			g_device->SetPixelShader(ps);
			g_device->SetTexture(0, page);
			g_device->SetVertexDeclaration(decl);
			g_device->SetStreamSource(0, stream, offset, stride);
			g_device->SetVertexShaderConstantF(0, vc, 4);
			g_device->SetPixelShaderConstantF(0, pc, 2);

			if (vs) vs->Release();
			if (ps) ps->Release();
			if (page) page->Release();
			if (decl) decl->Release();
			if (stream) stream->Release();
		}
	};
}

/* RoomProbe's twin of one classic flat layer (specificfx.cpp, DrawFlatSky),
 * drawn over the HD sky with `page`: the two quads of 9,728 units 1,536 over
 * the camera, the first shifted along x by `skyPos` (UpdateSky's counter)
 * and the second one quad width behind it, layer 1 turned half round
 * (SkyDrawPhase's phd_RotY(32760)), u along x and v along z over each quad;
 * the colour split as CalcColorSplit splits it; layer 1 as draw type 4,
 * the page's alpha over what is there, layer 2 as draw type 2, added with
 * no specular half (polyinsert.cpp); no depth test; the page's top level
 * alone, as the classic sampler takes it (MIPFILTER NONE,
 * function_table.cpp). `world` is the sky's own matrix,
 * the camera's position. The device state goes back as it was. With
 * `windows` it is the PSX sky's twin (DrawPSXSky, DrawSkySegment): `page`
 * is the SKY_GRAPHICS sprites' page and `windows` their four windows, x1 y1
 * x2 y2 each in page units as LoadSprites stores them. */
bool HDRenderSkyLayerProbe(const D3DMATRIX* world, IDirect3DTexture9* page,
	unsigned long colour, float skyPos, int layer, const float* windows)
{
	if (!g_device || !page || !world || !sky_probe_shaders())
		return false;

	const float turn = layer == 1 ? -1.0f : 1.0f;	// phd_RotY(32760)
	float rot[16] =
	{
		turn, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, turn, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	float model[16], wvp[16];

	multiply(rot, (const float*)world, model);
	multiply(model, g_viewproj, wvp);

	struct Corner
	{
		float x, y, z, u, v;
	} quad[32][4];

	/* The flat path: one tile per quad, u and v 0 to 1. The PSX path
	 * (`windows`): 4 x 4 tiles of 2,432 units per quad (SetSkyCoords),
	 * tile (ix, iz) showing sprite def (ix & 1) + 2 (iz & 1), its window
	 * running from x2, y2 at the tile's low x and z to x1, y1 at the high
	 * ones, as DrawSkySegment hands its corners to AddQuadSorted. */
	const int side = windows ? 4 : 1;
	const float step = 9728.0f / (float)side;
	int tiles = 0;

	for (int q = 0; q < 2; q++)
		for (int iz = 0; iz < side; iz++)
			for (int ix = 0; ix < side; ix++, tiles++)
			{
				const float x0 = skyPos - 9728.0f * (float)q - 4864.0f +
					step * (float)ix;
				const float z0 = -4864.0f + step * (float)iz;
				const float* w = windows ?
					windows + 4 * ((ix & 1) + 2 * (iz & 1)) : 0;
				const float cx[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
				const float cz[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

				for (int c = 0; c < 4; c++)
				{
					Corner& k = quad[tiles][c];

					k.x = x0 + step * cx[c];
					k.y = -1536.0f;
					k.z = z0 + step * cz[c];
					k.u = w ? w[2] + (w[0] - w[2]) * cx[c] : cx[c];
					k.v = w ? w[3] + (w[1] - w[3]) * cz[c] : cz[c];
				}
			}

	float diffuse[4], specular[4];

	for (int i = 0; i < 3; i++)
	{
		const int c = (int)((colour >> (16 - 8 * i)) & 0xFF);

		diffuse[i] = c > 128 ? 1.0f : (float)(c << 1) / 255.0f;
		specular[i] = c > 128 && layer != 2 ?
			(float)((c - 128) >> 1) / 255.0f : 0.0f;
	}

	diffuse[3] = specular[3] = 1.0f;

	SkyProbeState saved;

	saved.save();
	set_matrix_constant(0, wvp);
	g_device->SetPixelShaderConstantF(0, diffuse, 1);
	g_device->SetPixelShaderConstantF(1, specular, 1);
	g_device->SetVertexShader(g_sky_layer_vs);
	g_device->SetPixelShader(g_sky_layer_ps);
	g_device->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
	g_device->SetTexture(0, page);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, layer == 2 ? D3DBLEND_ONE
		: D3DBLEND_SRCALPHA);
	g_device->SetRenderState(D3DRS_DESTBLEND, layer == 2 ? D3DBLEND_ONE
		: D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

	for (int t = 0; t < tiles; t++)
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad[t],
			sizeof(Corner));

	saved.restore();
	return true;
}

#include "hd_shader/hd_sky_cloud_vs.inc"
#include "hd_shader/hd_sky_cloud_ps.inc"

/* RoomProbe's volumetric cloud layer (hd_sky.hlsl, CloudPS), a measurement
 * only: one quad over the whole screen, each pixel's ray walked through the
 * slab, composed over the sky with its own alpha, no depth test and no depth
 * write, as the classic layers are, so the rooms drawn after it cover it.
 * `slab` is the floor over the camera, the thickness, the wind's travel and
 * the lattice cell; `shape` the coverage, the density and the horizon fade;
 * `top` and `bottom` the colours (rgb). `world` is the sky's own matrix. */
bool HDRenderSkyCloudProbe(const D3DMATRIX* world, const float* slab,
	const float* shape, const float* top, const float* bottom)
{
	static IDirect3DVertexShader9* vs = 0;
	static IDirect3DPixelShader9* ps = 0;
	static IDirect3DDevice9* made = 0;

	if (!g_device || !world)
		return false;

	if (made != g_device)
	{
		vs = 0;
		ps = 0;
		made = g_device;
	}

	if (!vs && FAILED(g_device->CreateVertexShader(
		(const DWORD*)kHDSkyCloudVS, &vs)))
		vs = 0;

	if (!ps && FAILED(g_device->CreatePixelShader(
		(const DWORD*)kHDSkyCloudPS, &ps)))
		ps = 0;

	if (!vs || !ps)
		return false;

	/* The view's axes in world space: the columns of the rotation that
	 * takes a world direction into the view (row vectors, p * M), each over
	 * the projection's scale on it. */
	float view[16];

	multiply((const float*)world, g_view, view);

	float c[28] = { 0.0f };

	for (int i = 0; i < 3; i++)
	{
		c[i] = view[i * 4] / g_proj[0];
		c[4 + i] = view[i * 4 + 1] / g_proj[5];
		c[8 + i] = view[i * 4 + 2];
	}

	for (int i = 0; i < 4; i++)
	{
		c[12 + i] = slab[i];
		c[16 + i] = shape[i];
	}

	for (int i = 0; i < 3; i++)
	{
		c[20 + i] = top[i];
		c[24 + i] = bottom[i];
	}

	float saved[28];
	DWORD states[4];
	IDirect3DVertexShader9* oldVs = 0;
	IDirect3DPixelShader9* oldPs = 0;
	IDirect3DVertexDeclaration9* oldDecl = 0;
	IDirect3DVertexBuffer9* stream = 0;
	UINT offset = 0, stride = 0;
	const D3DRENDERSTATETYPE kinds[4] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_CULLMODE
	};

	g_device->GetPixelShaderConstantF(0, saved, 7);
	g_device->GetVertexShader(&oldVs);
	g_device->GetPixelShader(&oldPs);
	g_device->GetVertexDeclaration(&oldDecl);
	g_device->GetStreamSource(0, &stream, &offset, &stride);

	for (int i = 0; i < 4; i++)
		g_device->GetRenderState(kinds[i], &states[i]);

	DWORD src = 0, dst = 0;

	g_device->GetRenderState(D3DRS_SRCBLEND, &src);
	g_device->GetRenderState(D3DRS_DESTBLEND, &dst);

	g_device->SetPixelShaderConstantF(0, c, 7);
	g_device->SetVertexShader(vs);
	g_device->SetPixelShader(ps);
	g_device->SetFVF(D3DFVF_XYZW);
	g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	const float corners[16] =
	{
		-1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f,
		1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f
	};

	g_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, corners,
		4 * sizeof(float));

	g_device->SetPixelShaderConstantF(0, saved, 7);
	g_device->SetVertexShader(oldVs);
	g_device->SetPixelShader(oldPs);
	g_device->SetVertexDeclaration(oldDecl);
	g_device->SetStreamSource(0, stream, offset, stride);

	for (int i = 0; i < 4; i++)
		g_device->SetRenderState(kinds[i], states[i]);

	g_device->SetRenderState(D3DRS_SRCBLEND, src);
	g_device->SetRenderState(D3DRS_DESTBLEND, dst);

	if (oldVs) oldVs->Release();
	if (oldPs) oldPs->Release();
	if (oldDecl) oldDecl->Release();
	if (stream) stream->Release();

	return true;
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

			if (range.artHidden)
				continue;

			// And at the depth draw_pass drew it at.
			const bool stand_behind = range.stand &&
				group.standWith[(size_t)range.stand - 1] == HD_STAND_BEHIND;
			const bool piece_behind = range.intruding > 0 &&
				(size_t)range.intruding <= group.pairRanges.size() &&
				group.pairRanges[(size_t)range.intruding - 1].behind;
			const bool want_behind = window == HD_WINDOW_STAND ?
				g_stand_pick_behind
				: range.behind || stand_behind || piece_behind;

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

	/* A depth surface with a stencil to guard the pixels with - the INTZ
	 * surface of the sheets' depth texture among them, 8 bits of stencil
	 * like the D24S8 it stands in for. */
	bool depth_has_stencil()
	{
		IDirect3DSurface9* depth = 0;
		D3DSURFACE_DESC desc;
		bool stencil = false;

		if (SUCCEEDED(g_device->GetDepthStencilSurface(&depth)) && depth)
		{
			if (SUCCEEDED(depth->GetDesc(&desc)))
				stencil = desc.Format == D3DFMT_D24S8 || desc.Format == D3DFMT_D24FS8 ||
					desc.Format == D3DFMT_D24X4S4 ||
					desc.Format == D3DFMT_D15S1 || desc.Format == kIntz;

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
	 * the colour. The factor comes in alpha and the fog bulbs' haze in colour
	 * (hd_shadow_receive.hlsl, ReceiveOut), so the haze in front of a
	 * shadowed floor keeps its own brightness. */
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_EQUAL);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
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
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
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
			range.behind = 0;
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
	marble_note(2, piece, world, lighting, passes, HD_WINDOW_ALL);
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
	 * buffer and the aim is to match it, not to be more accurate than it.
	 * mzfar is the depth curve's scale, which SetupZRange takes from
	 * ZFAR_SHIPPED (20480) and not from the far plane, ZFAR_UNITS: the
	 * engine's f_a and f_b are what this gives for 20 and 20480. */
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

/*
 * Modified 2026 by ChrisChanSan.
 *
 * The structure of the level build (read the .TRG, one buffer for the whole
 * level, a range table per group, three passes per room) and the draw rule
 * are carried over from TROne (Copyright 2026 Todesengel, GPLv3,
 * host\render\game_renderer.cpp, hd_load 1919-2256 and hd_draw_room
 * 2259-2312). The renderer under it is new: TROne is OpenGL 3.3 with vertex
 * arrays, texture arrays and runtime GLSL, this is Direct3D 9 with a vertex
 * declaration, one texture per page and compiled bytecode, beside a
 * fixed-function neighbour that must find its state exactly as it left it.
 * See THIRD-PARTY.md.
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

#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include "hd_shader/hd_room_vs.inc"
#include "hd_shader/hd_room_ps.inc"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
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
	 * would paint the level with red and blue exchanged. */
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
	 *    7  a wind material, param (alphaRef 0.5, strength, scale, scale)
	 *    8  the other wind material, the same param shape - foliage, ivy,
	 *       banners, kelp. 7 and 8 are the same thing in two numbers: no
	 *       level carries both, ANDREA3 has only 7 and JOBY4 only 8, and
	 *       TITLE - which is a montage of every level - has both
	 *    6  the plain cut-out material - gratings, nets, railings, ladders.
	 *       param[0] is its alpha reference and is 0.5 in 10 of the 11
	 *       tuples the game has (ANDY1 carries one 0.503)
	 *   13  the remaster's own four-frame flipbook, the other three pages in
	 *       param[0..2] (MaterialFlipTime below)
	 *    2  the glow material: light shafts, lamp cones, window glows and
	 *   14  the lit decal - both draw in the additive pass only
	 *   15  the haze material - a whole room's worth of additive surface
	 *       (MaterialAddWeight below)
	 *   16  the water material: the remaster's own still water surface -
	 *       every basin, cistern and pool the .TRG builds a sheet for, and
	 *       the sea of JOBY4 (MaterialRipple below) */
#define HD_TRG_MATERIAL_FLOW		1
#define HD_TRG_MATERIAL_MASK		6
#define HD_TRG_MATERIAL_WIND		8
#define HD_TRG_MATERIAL_WIND2		7
#define HD_TRG_MATERIAL_GLOW		2
#define HD_TRG_MATERIAL_DECAL		14
#define HD_TRG_MATERIAL_HAZE		15
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
	 * .TRG files, the distinct (material, param) tuples:
	 *
	 *   material 6   param[0] 0.500 (once 0.503), opaque only, 513,647 tris
	 *   material 7   param[0] 0.500 in all 7 tuples, opaque, 88,726 tris
	 *   material 8   param[0] 0.500 in 10 of 11 tuples, the odd one 0.120
	 *                and in the alpha pass, 1,308,742 tris
	 *   material 3   param[0] 1, 2, 4, 26, 32, 35, 39, 56, 69, 78, 107,
	 *                109, 110, 141, 161, 191 - a specular exponent
	 *   material 17  param[0] 22, 25, 36, 38, 63, 70, 72 - the same shape
	 *   material 13  param[0..2] are page numbers, material 14 likewise
	 *   material 2   param[0] 0.5, 1.0, 1.5, but additive in every one of
	 *                its 20 tuples, where no alpha test runs at all
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
	 * slot, over the distinct (material, param) tuples of all fifteen levels:
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
	 *   material 15  p1 is 14 or 30 in every tuple - two values over eleven,
	 *                a speed and not a weight - and p2 in [0.025, 0.1] is
	 *                the only slot that varies smoothly.
	 *   material 16  p2 in [0.035, 0.2], the same shape as material 15's,
	 *                on the same slot; p0 and p1 pair up with the page.
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
	 * fifteen files prints 36 distinct tuples, 32 of them with p1 in (0, 1).
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
	 * alpha test. */
	bool g_no_blend = false;	// see HDRenderBlendOverride
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

		/* Material 16 is not a weight even though its param[2] lives in the
		 * same range as material 15's (0.035 to 0.2): it is the remaster's
		 * still water surface (TrgProbe -matparts 16) - 28 parts in six
		 * levels, 17 of them a double sheet two units apart with opposite
		 * normals, every one sitting 1 to 20 units off the classic minfloor
		 * of a ROOM_REFLECT room, its parameters scaling with the area of the
		 * body of water. It falls through to 1.0 like every other material;
		 * MaterialRipple reads its three numbers. */
		switch (part.material)
		{
		case HD_TRG_MATERIAL_GLOW:
		case HD_TRG_MATERIAL_DECAL:
			weight = part.param[1];
			break;

		case HD_TRG_MATERIAL_HAZE:
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
	 * minute it is not motion any more. No part in the game is outside it. */
	inline float MaterialAddPeriod(const trf::remaster::TrgPart& part)
	{
		if (part.material != HD_TRG_MATERIAL_GLOW)
			return 0.0f;

		return part.param[3] >= 0.05f && part.param[3] <= 60.0f
			? part.param[3] : 0.0f;
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
	 * 28 parts in the whole game. */
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
	 * Origin: this reading is our own measurement of the table above and
	 * nothing else. TROne names these three slots as well; nothing of
	 * TROne's is used here. The order settled on - param[0] the scale,
	 * param[1] the travel, param[2] the rate - is the same order TROne's
	 * prose names. param[1] follows from the table; which of the two
	 * monotone slots is the frequency and which the rate is unproven,
	 * because both move with the area and one measurement cannot separate
	 * them. If the sea ripples too finely and too fast while the fountain
	 * is too coarse and too slow, they are the wrong way round. The
	 * reversal is not a swap of out[0] and out[2]: each anchor below
	 * belongs to its parameter, so the sources change places and keep
	 * their anchors - the scale reads param[2] through the 0.2 anchor
	 * (floored at 0.0125), the rate reads param[0] through the 0.8 anchor.
	 * No constant of TROne's is used; every number below comes from a TR5
	 * measurement, and the wave in hd_room.hlsl is a different curve with
	 * different terms.
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
	 * This is a texture ripple and nothing else.
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

	/* The gloss of one part.
	 *
	 * Origin: TROne has no reflection and no specular anywhere - its only
	 * related lines are a comment about the water UV wobble "distorting the
	 * reflection" and the note in game_renderer.cpp:373 that an opaque page's
	 * DDS alpha is a sparkle/specular mask, not coverage - and its
	 * SHADER_RULES.md lists six rules, none of them a mirror. So nothing here
	 * is carried over from it; the reading below is our own measurement.
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
	 * RICH1, RICH2 and RICH3 - the three VCI levels, the ones with the marble
	 * floors - carry neither material, not one triangle. Their marble has no
	 * gloss datum in the .TRG and is left matt rather than given an invented
	 * one.
	 *
	 * Zero means "no gloss", and a strength outside (0, 1] falls through to
	 * zero rather than being trusted - the same shape as MaterialAlphaRef and
	 * MaterialAddWeight above. */
	inline void MaterialGloss(const trf::remaster::TrgPart& part,
		float& strength, float& power)
	{
		strength = 0.0f;
		power = 0.0f;

		float s;

		if (part.material == 3)
			s = part.param[2];
		else if (part.material == 17)
			s = part.param[1];
		else
			return;

		if (!(s > 0.0f && s <= 1.0f))
			return;

		/* The exponent is an integer in 1..191 in every tuple of the game, and
		 * anything else would not be one. */
		if (!(part.param[0] >= 1.0f && part.param[0] <= 255.0f) ||
			(float)(int)part.param[0] != part.param[0])
			return;

		strength = s;
		power = part.param[0];
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
		/* The gloss of this draw call - the .TRG material's own strength and
		 * exponent, 0 for every part that names none. One pixel shader
		 * constant, hence part of the sort key. */
		float gloss = 0.0f;
		float glossPower = 0.0f;
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
		/* Which destructible piece these triangles belong to, or -1 for the
		 * room itself. In the sort key, so a piece's triangles never share a
		 * draw call with the wall behind them. */
		int destructId = -1;
		/* Which half of the room this draw call is: 1 where the triangles of
		 * this range stand inside another room - see HDIntrusion in
		 * hd_render.h. In the sort key, because the two halves are drawn with
		 * different scissor states and a triangle cannot have half of one.
		 *
		 * The overhang has no draw calls of its own: it is drawn over the
		 * whole screen along with everything else. It is still counted per
		 * triangle (HDGroup::overhang) for the hollow-room test. */
		int intruding = 0;
		// The inside/overhang split, filled always and in the sort key only
		// while HDRenderSplitOverhang is on - see the note over it.
		int overhang = 0;
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
		std::vector<HDRenderSample> samples;
		std::vector<unsigned int> cells;	// sorted, unique - see above
		/* The same list built from the inside triangles alone. The overlap
		 * list is asked of this one, because the overhang is drawn without a
		 * window whatever the answer. */
		std::vector<unsigned int> cellsIn;
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
	// Draw calls that carried a gloss, and the hd_reflect switch as a
	// float, set once per frame beside the motion switch.
	int g_gloss_calls = 0;
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

	/* The rooms that may be holding another room's art,
	 * and how many triangles each of those boxes really caught. The list is
	 * borrowed for the length of the build like the boxes above; the hit
	 * counts outlive it, because the level's own log line is written after
	 * the build and has to say what the rule found. */
	const HDIntrusion* g_intrusions = 0;
	int g_intrusion_count = 0;
	// The diagnostic inside/overhang split - see HDRenderSplitOverhang.
	int g_split_overhang = 0;
	std::vector<int> g_intrusion_hits;

	/* The openings the randomizer cut, and how many triangles
	 * each of those boxes took away. Borrowed and counted on exactly the same
	 * terms as the intrusions above. */
	const HDCut* g_cuts = 0;
	int g_cut_count = 0;
	std::vector<int> g_cut_hits;

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

	void release_buffers()
	{
		if (g_vb) { g_vb->Release(); g_vb = 0; }
		if (g_ib) { g_ib->Release(); g_ib = 0; }

		g_groups.clear();
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

	/* bshader.bat compiles both pixel shaders for ps_3_0, the room shader at
	 * 476 instruction slots and the model shader at 348. The ps_3_0 figure is
	 * MaxPixelShader30InstructionSlots - PS20Caps is the ps_2_x one - and
	 * shader model 3 sets its floor at D3DMIN30SHADERINSTRUCTIONS, 512, the
	 * same limit fxc compiles ps_3_0 against. A device that answers less
	 * would fail CreatePixelShader below with nothing but an HRESULT, so it
	 * is refused here with a reason in the log. A device that leaves the
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

void HDRenderUprightOverride(int off)
{
	g_no_upright = off ? true : false;
}

void HDRenderShutdown()
{
	release_buffers();
	HDPageCacheDestroy();

	if (g_ps) { g_ps->Release(); g_ps = 0; }
	if (g_vs) { g_vs->Release(); g_vs = 0; }
	if (g_decl) { g_decl->Release(); g_decl = 0; }

	g_device = 0;
	g_in_frame = false;
	g_draw_calls = 0;
	g_cutout_calls = 0;
	g_flip_calls = 0;
	g_gloss_calls = 0;
	g_water_calls = 0;
	g_error.clear();
}

bool HDRenderReady()
{
	return g_device && g_vs && g_ps && g_decl;
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

void HDRenderSplitOverhang(int on)
{
	g_split_overhang = on ? 1 : 0;
}

void HDRenderSetIntrusions(const HDIntrusion* list, int count)
{
	g_intrusions = count > 0 ? list : 0;
	g_intrusion_count = list ? count : 0;
	g_intrusion_hits.assign((size_t)(g_intrusion_count > 0 ? g_intrusion_count : 0), 0);
}

int HDRenderIntrusionHits(int at)
{
	return at >= 0 && at < (int)g_intrusion_hits.size()
		? g_intrusion_hits[(size_t)at] : 0;
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
	 * remaster pages are 4096x4096 and a card that stops at 2048 gets them
	 * one level down instead of not at all. */
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
	 * makes for its own water flag (game_renderer.cpp:2113-2124). A vertex
	 * named by two groups keeps the last one; the marks are per room and a
	 * shared vertex sits on a portal, where both answers agree. */
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
	 *     param (alphaRef 0.5, strength, scale, scale) in both, the cut-out
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
	g_flat_parts = 0;
	g_gloss_parts = 0;
	g_flat_planes = 0;
	g_blend_parts = 0;
	g_blend_ranges = 0;
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
		float gloss, glossPower;	// see MaterialGloss
		float ripple[3];		// see MaterialRipple
		float blend;			// see MaterialBlend
		int destructId;			// see HDDestructible
		int overhang;			// see HDRoomBox
		int intruding;			// see HDIntrusion
		unsigned int first;
	};

	std::vector<HDTri> tris;
	std::vector<unsigned char> sway_seen(trg.vertices.size(), 0);

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

		/* And the openings that were cut into this group's
		 * room. Gathered the same way and for the same reason - a level
		 * carries a handful of them and draws a million triangles. Empty for
		 * every group of every vanilla level. */
		for (int k = 0; k < g_cut_count; k++)
			if (g_cuts[k].group == (int)g)
				group.cutAt.push_back(k);

		/* (height, sector) of every horizontal triangle of this group,
		 * merged into the group's own table below. */
		std::vector<std::pair<int, unsigned int> > flats;
		/* And the same again out of the material 16 triangles alone - the
		 * remaster's own still water surfaces. The first list answers "did
		 * the remaster build anything in this plane at all" and is taken by
		 * majority; a sheet in the second list answers "the remaster built
		 * the water here" and needs no majority. See BuildWaterSurfaces. */
		std::vector<std::pair<int, unsigned int> > waterflats;

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

				if (pass != 2)
					MaterialGloss(part, gloss, gloss_power);

				if (pass == 0 && gloss != 0.0f)
					g_gloss_parts++;

				/* The see-through parts of the alpha pass - material 14,
				 * and nothing else in the game. */
				const float blend = MaterialBlend(part, pass);

				// A part can name a pass it puts no triangle in; counting
				// those would say a level has glass where it has none.
				if (blend != 0.0f && run.count)
					g_blend_parts++;

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

				if (pass == 0 && scrolls)
					g_scroll_parts++;

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
					 * triangle standing in the middle of a different room",
					 * and the box has already been pulled a whole sector in
					 * on every side for exactly that reason - a tolerance on
					 * top of it would only start letting the wall back in. */
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
							intruding = 1;

							if ((size_t)group.intrusionAt[k] < g_intrusion_hits.size())
								g_intrusion_hits[(size_t)group.intrusionAt[k]]++;
						}
					}

					HDTri tri;

					tri.overhang = overhang;
					tri.intruding = intruding;
					tri.slot = slot;
					tri.scrollU = su;
					tri.scrollV = sv;
					tri.alphaRef = aref;
					tri.addWeight = aweight;
					tri.addPeriod = aperiod;
					tri.flatLight = flat;
					tri.flipTime = ftime;
					tri.flipSlot[0] = fslot[0];
					tri.flipSlot[1] = fslot[1];
					tri.flipSlot[2] = fslot[2];
					tri.gloss = gloss;
					tri.glossPower = gloss_power;
					tri.ripple[0] = ripple[0];
					tri.ripple[1] = ripple[1];
					tri.ripple[2] = ripple[2];
					tri.blend = blend;
					tri.destructId = destruct_id;
					tri.first = first;
					tris.push_back(tri);
				}
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
					// The gloss is one more pixel constant per draw call.
					if (a.gloss != b.gloss) return a.gloss < b.gloss;
					if (a.glossPower != b.glossPower) return a.glossPower < b.glossPower;
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
					// A destructible piece keeps its own draw calls.
					if (a.destructId != b.destructId) return a.destructId < b.destructId;
					/* And the art that stands inside another room never
					 * shares a draw call with the art that does not - the
					 * scissor test is on for one and off for the other. Last
					 * in the key, so a level built with no intrusion list -
					 * every triangle answering 0 - sorts on the rest alone. */
					if (g_split_overhang && a.overhang != b.overhang)
						return a.overhang < b.overhang;

					return a.intruding < b.intruding;
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
			range.gloss = tris[0].gloss;
			range.glossPower = tris[0].glossPower;
			range.ripple[0] = tris[0].ripple[0];
			range.ripple[1] = tris[0].ripple[1];
			range.ripple[2] = tris[0].ripple[2];
			range.blend = tris[0].blend;
			range.destructId = tris[0].destructId;
			range.intruding = tris[0].intruding;
			range.overhang = tris[0].overhang;
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
					tris[i].gloss == range.gloss &&
					tris[i].glossPower == range.glossPower &&
					tris[i].ripple[0] == range.ripple[0] &&
					tris[i].ripple[1] == range.ripple[1] &&
					tris[i].ripple[2] == range.ripple[2] &&
					tris[i].blend == range.blend &&
					tris[i].destructId == range.destructId &&
					tris[i].intruding == range.intruding &&
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

						if (range.blend != 0.0f)
							g_blend_ranges++;

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
					range.gloss = tris[i].gloss;
					range.glossPower = tris[i].glossPower;
					range.ripple[0] = tris[i].ripple[0];
					range.ripple[1] = tris[i].ripple[1];
					range.ripple[2] = tris[i].ripple[2];
					range.blend = tris[i].blend;
					range.destructId = tris[i].destructId;
					range.intruding = tris[i].intruding;
					range.overhang = tris[i].overhang;
					range.start = (unsigned int)sorted.size();
					range.count = 0;
				}

				const unsigned int first = tris[i].first;

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

				if (range.blend != 0.0f)
					g_blend_ranges++;

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
	 * `sorted` is not read again after this. */
	if (!g_index32)
		for (const HDGroup& group : g_groups)
			for (int pass = 0; pass < 3; pass++)
				for (const HDRange& range : group.pass[pass])
					for (unsigned int at = 0; at < range.count; at++)
						sorted[range.start + at] -= range.baseVertex;

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
		stats->skyFlatParts = g_flat_parts;
		stats->flipbookDrawn = g_flip_parts;
		stats->flipbookDropped = g_flip_dropped;
		stats->flipbookRanges = g_flip_ranges;
		stats->glossParts = g_gloss_parts;
		stats->flatPlanes = g_flat_planes;
		stats->blendParts = g_blend_parts;
		stats->blendRanges = g_blend_ranges;
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
	g_gloss_calls = 0;
	g_water_calls = 0;

	if (view)
		std::memcpy(g_view, view, sizeof(g_view));
	if (projection)
		std::memcpy(g_proj, projection, sizeof(g_proj));

	multiply(g_view, g_proj, g_viewproj);

	HDRenderStateSave();

	/* The room path's own buffers and shaders. Everything above is shared
	 * with hd_model.cpp; these five lines are what makes the bracket a room
	 * bracket, and hd_model sets its own in their place. */
	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);
	g_device->SetStreamSource(0, g_vb, 0, sizeof(HDVertex));
	g_device->SetIndices(g_ib);

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

namespace
{
	/* `blendOnly`: the alpha pass is drawn twice in a frame, once for its
	 * solid ranges - the cut-outs, with the alpha test - and once, after
	 * everything opaque, for its see-through ones. Both other passes are
	 * called with it false and neither has a blended range in it. */
	void draw_pass(const HDGroup& group, int pass, const float* wvp,
		bool blendOnly = false, int window = HD_WINDOW_ALL, bool sky = false)
	{
		if (group.pass[pass].empty())
			return;

		/* gPass: x is the alpha reference - a texel below it is thrown away
		 * and zero throws nothing away - y the additive weighting, z a spare
		 * scale on the first vertex colour that is always 1 here and that
		 * nothing computes, w the specular add - the
		 * classic path switches D3DRS_SPECULARENABLE off for its additive
		 * draw type (HWR_DrawSortList, polyinsert.cpp) and the overbright half of the room
		 * colour goes off with it.
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
			1.0f,
			pass == 2 ? 0.0f : 1.0f
		};

		g_device->SetVertexShaderConstantF(4, constants, 1);
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
		/* gRipple: the wave scale, the travel and the rate of a material
		 * 16 water surface. Three zeroes for every other draw call in the
		 * game, where the pixel shader's one compare on .y skips the whole
		 * of it - so a level with no water pays one compare per pixel and
		 * uploads this constant once per pass. */
		float ripple[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		bool ripple_dirty = true;
		const bool additive_pass = pass == 2;

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
			g_device->SetVertexShaderConstantF(4, constants, 1);
			g_device->SetPixelShaderConstantF(4, constants, 1);
		}
		else if (blendOnly)
		{
			/* The glass. Ordinary translucency - the source's own alpha
			 * against one minus it - and no depth write, because a pane you
			 * can see through must not hide what stands behind it from the
			 * draws that come after. The alpha test is left off here, so a
			 * texel at 0.45 is half there and not gone. */
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
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

			/* And this call draws the art that stands inside
			 * another room or the art that does not, never both - the two
			 * are drawn with different scissor states. HD_WINDOW_ALL draws
			 * every range, which is what every room but a handful is drawn
			 * with. */
			if ((window == HD_WINDOW_OPEN && range.intruding) ||
				(window == HD_WINDOW_INTRUDING && !range.intruding))
				continue;

			// And the inside/overhang split, for RoomProbe's -tround alone.
			if ((window == HD_WINDOW_INSIDE && range.overhang) ||
				(window == HD_WINDOW_OVERHANG && !range.overhang))
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

			IDirect3DTexture9* page = HDPageCacheSlotTexture(page_slot);

			if (!page)
				continue;

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

			if (spec_dirty || spec[0] != range.gloss ||
				spec[1] != range.glossPower || spec[3] != want_blend)
			{
				spec[0] = range.gloss;
				spec[1] = range.glossPower;
				spec[3] = want_blend;
				spec_dirty = false;
				g_device->SetPixelShaderConstantF(19, spec, 1);
			}

			if (range.gloss != 0.0f)
				g_gloss_calls++;

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
			if (ripple_dirty || ripple[0] != range.ripple[0] ||
				ripple[1] != range.ripple[1] || ripple[2] != range.ripple[2])
			{
				ripple[0] = range.ripple[0];
				ripple[1] = range.ripple[1];
				ripple[2] = range.ripple[2];
				ripple_dirty = false;
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

			g_device->SetTexture(0, page);

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
		}

		if (g_wrapping)
		{
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			g_wrapping = false;
		}

		if (pass == 2 || blendOnly)
		{
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		}
		else if (sky && pass == 1)
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

		(void)wvp;
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
		if (passes & (1u << pass))
			draw_pass(room, pass, wvp, false, window, sky);

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
	return true;
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

// And how many carried a gloss out of their material.
int HDRenderGlossCalls()
{
	return g_gloss_calls;
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

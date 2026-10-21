/*
 * Modified 2026 by ChrisChanSan.
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

/* The springs (hd_spring.cpp): the rising current, a column that bends what
 * stands behind it, the bubbles in the jet and the crack at its foot. All
 * are triangles the processor builds in view space, as the water's cards
 * are, projected by the cards' own vertex shader (hd_room.hlsl, CardVS);
 * this file holds the pixel halves and declares only what they read.
 *
 * Build. bshader.bat compiles it four times:
 *
 *     fxc /nologo /T ps_3_0 /E ColumnPS /O3 /Vn kHDSpringColumnPS
 *         /Fh hd_spring_column_ps.inc hd_spring.hlsl
 *     fxc /nologo /T ps_3_0 /E BubblePS /O3 /Vn kHDSpringBubblePS
 *         /Fh hd_spring_bubble_ps.inc hd_spring.hlsl
 *     fxc /nologo /T ps_3_0 /E CrackPS /O3 /Vn kHDSpringCrackPS
 *         /Fh hd_spring_crack_ps.inc hd_spring.hlsl
 *     fxc /nologo /T ps_3_0 /E HolePS /O3 /Vn kHDSpringHolePS
 *         /Fh hd_spring_hole_ps.inc hd_spring.hlsl
 *
 * and stamps each .inc with the MD5 of this file.
 *
 * The column adds no light: clear water rising through clear water shows
 * only as what stands behind it, moved. It is drawn after the sheets with
 * the blend DESTCOLOR / INVSRCALPHA - the target times rgb plus the target
 * times one less alpha - and writes rgb a r, alpha a for a share a and
 * the ratio r of the frame's copy at the bent place to the copy at the
 * pixel. The target becomes target (1 + a (r - 1)): under the water, where
 * the target is the copy, the copy at the bent place; over the water, the
 * sheet's colour with the floor's pattern in it moved and its tint and
 * mirror kept. Where a is 0 the target stays as it was, to the bit.
 *
 * The bubble is premultiplied, ONE / INVSRCALPHA, as the cards are: a thin
 * bright rim (the light the bubble's wall turns toward the eye at a
 * grazing angle), a faint body and a small highlight.
 *
 * Pixel registers, written before each draw, in the block the sheets' and
 * the cards' draws write again before they read it:
 *
 * c16  gFog          the distance fog (hd_room.hlsl, RoomPS)
 * c92  gSpringScreen x, y one over the target's width and height, z, w the
 *                    projection's a and b (depth = a - b / z)
 * c93  gSpringMode   x 1 while the frame's depth texture is bound on s6,
 *                    y one over the depth over which a quad fades into what
 *                    stands behind it, z the ratio's floor, w one over
 *                    the weight from which the column's share is whole
 * c94  gSpringCell   x, y one over the column's cell across and along its
 *                    line, units, z one over the noise table's side
 * c95  gCrackRay     the view ray through a pixel: x, y of the ray at depth
 *                    1 are vpos.x times x plus z and vpos.y times y plus w
 * c96  gCrackFloor   the crack's bottom as a plane in view space: xyz its
 *                    normal, w its distance (dot(n, p) = w on it)
 * c97  gCrackLook    x one over the height of the dark band over the
 *                    bottom, units: one over the crack's depth
 * c134 gUnderSigma   the water while the eye is under it, the frame's own
 * c135 gUnderLight   (hd_room.hlsl, UnderWater); gUnderSigma.w also moves
 * c136 gUnderUp      the distance fog out of reach there, as RoomPS does
 * c145 gUnderClear
 *
 * c92 - c97 and the vertex shader's c0 - c3 are put back after the crack's
 * draw, which comes before the rooms' blend phase. */
float4 gFog : register(c16);
float4 gSpringScreen : register(c92);
float4 gSpringMode : register(c93);
float4 gSpringCell : register(c94);
float4 gCrackRay : register(c95);
float4 gCrackFloor : register(c96);
float4 gCrackLook : register(c97);
float4 gUnderSigma : register(c134);
float4 gUnderLight : register(c135);
float4 gUnderUp : register(c136);
float4 gUnderClear : register(c145);

sampler2D sCrackPage : register(s0);
sampler2D sSpringCopy : register(s5);
sampler2D sSpringDepth : register(s6);
sampler2D sSpringNoise : register(s7);

struct SPRING_IN
{
	float4 col    : TEXCOORD2;
	float4 uv     : TEXCOORD0;
	float4 viewZ  : TEXCOORD1;
};

// The frame's view depth at a place on the screen.
float SpringDepth(float2 uv)
{
	const float d = tex2Dlod(sSpringDepth, float4(uv, 0.0f, 0.0f)).r;

	return gSpringScreen.w / max(gSpringScreen.z - d, 1.0e-6f);
}

/* Value noise, two at once, one for each way of the bend: the hashed
 * values of the lattice points round a place, blended with a smooth step
 * (the textbook construction). The values are the noise table's texels
 * (hd_spring.h, HDSpringNoiseTexel: the life's mixer of the texel's
 * number, one salt a channel), bound on s7 with wrap and a linear filter;
 * reading at the cell's corner plus the smoothed fraction lets the
 * filter do the blend in one fetch. The table repeats every 64 cells. */
float2 SpringNoise(float2 p)
{
	const float2 i = floor(p);
	const float2 f = p - i;
	const float2 u = f * f * (3.0f - 2.0f * f);

	return tex2Dlod(sSpringNoise, float4((i + u + 0.5f) * gSpringCell.z,
		0.0f, 0.0f)).rg;
}

/* The column. The vertex:
 *
 *   uv   x across the column, -1..1, y along it, 0 at the floor and 1 at
 *        the sheet, z across in units, w the height over the floor in units
 *   col  x the bend at full weight in pixels at this depth, y the weight
 *        of the whole column (its strength, the fades by distance and by
 *        the angle to its line), z the pattern's rise (clock times speed),
 *        w kHDSpringNear */
float4 ColumnPS(SPRING_IN input, float2 vpos : VPOS) : COLOR0
{
	const float across = input.uv.x;
	const float along = input.uv.y;

	/* On the line full, at the edge nothing; falling with height as the
	 * jet's speed falls; faded over the last eighth at each end, so the
	 * quad's ends do not show; soft where the column meets the floor and
	 * the sheet behind it. The weight scales the bend. */
	float core = saturate(1.0f - across * across);

	core *= core;

	float w = core * input.col.y * input.col.w / (input.col.w + input.uv.w) *
		saturate(along * 8.0f) * saturate((1.0f - along) * 8.0f);
	const float2 uv = (vpos + 0.5f) * gSpringScreen.xy;

	if (gSpringMode.x > 0.5f)
		w *= saturate((SpringDepth(uv) - input.viewZ.x) * gSpringMode.y);

	// Two cells of the pattern, the second twice as fine, rising.
	const float2 p = float2(input.uv.z, input.uv.w - input.col.z) *
		gSpringCell.xy;
	const float2 bend = (SpringNoise(p) + 0.5f * SpringNoise(p * 2.0f +
		7.31f)) * (2.0f / 1.5f) - 1.0f;
	float2 moved = uv + bend * input.col.x * w * gSpringScreen.xy;

	/* What lies in front of the column is not seen through it: a bent
	 * place nearer than the column keeps the pixel's own. */
	if (gSpringMode.x > 0.5f && SpringDepth(moved) < input.viewZ.x)
		moved = uv;

	const float3 here = tex2Dlod(sSpringCopy, float4(uv, 0.0f, 0.0f)).rgb;
	const float3 there = tex2Dlod(sSpringCopy, float4(moved, 0.0f,
		0.0f)).rgb;
	const float3 ratio = min((there + gSpringMode.z) / (here + gSpringMode.z),
		2.0f);

	/* The share of the moved picture: whole from gSpringMode.w of the
	 * weight up, none at 0, so where the column ends the target stays as
	 * it was, to the bit. */
	const float a = saturate(w * gSpringMode.w);

	return float4(a * ratio, a);
}

/* The bubble. The vertex as a card's: uv.zw the corner, -1..1, col.rgb its
 * colour (the body's spray colour, through the water over it), col.a its
 * share, viewZ.x the view depth and y the light the fog takes a share of. */
float4 BubblePS(SPRING_IN input, float2 vpos : VPOS) : COLOR0
{
	const float2 c = input.uv.zw;
	const float r2 = dot(c, c);
	const float r = sqrt(r2);

	/* The rim from 0.55 of the radius, brightest at 0.9, gone at the edge;
	 * the body a tenth; the highlight a quarter of the radius up and to
	 * the left of the middle. Free choices, set by eye on
	 * RoomProbe pictures. */
	const float rim = smoothstep(0.55f, 0.9f, r) * (1.0f - smoothstep(0.9f,
		1.0f, r));
	const float body = 0.1f * saturate(1.0f - r2);
	const float hl = saturate(1.0f - length(c + 0.35f) / 0.3f);
	const float spot = hl * hl;
	float keep = input.col.a;

	if (gSpringMode.x > 0.5f)
	{
		const float behind = SpringDepth((vpos + 0.5f) * gSpringScreen.xy);

		keep *= saturate((behind - input.viewZ.x) * gSpringMode.y);
	}

	// The fog as the cards take it, and the water's own under it.
	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);

	keep *= saturate(1.0f - fog / max(input.viewZ.y, 0.05f));
	keep *= exp(-max(input.viewZ.x - gUnderClear.x, 0.0f) * gUnderSigma.w);

	return float4((input.col.rgb * (rim * 0.75f + body) + spot) * keep,
		saturate(rim * 0.75f + body + spot * 0.6f) * keep);
}

/* The crack at the spring's foot (hd_spring.cpp, "the crack"): its walls
 * carry the floor's own page, its bottom is black. Neither the rooms' pixel
 * shader nor the cards' can draw it: the walls are built per frame in view
 * space, outside the rooms' buffer and its per-room constants, and they
 * darken toward the bottom; the bottom writes a depth of its own.
 *
 * The view place of a pixel at view depth z: its ray at depth 1 times z. */
float3 CrackView(float2 vpos, float z)
{
	return float3(vpos * gCrackRay.xy + gCrackRay.zw, 1.0f) * z;
}

/* The water round an eye under it, as the rooms take it (hd_room.hlsl,
 * UnderWater, the same lines): all thirteen numbers zero over the water,
 * where the colour comes out as it went in. */
float3 CrackUnder(float3 col, float3 viewPos)
{
	const float d = length(viewPos);
	const float h = dot(viewPos, gUnderUp.xyz);
	const float path = max(d * saturate(gUnderUp.w / max(h, 1.0e-3f)) -
		gUnderClear.x, 0.0f);
	const float S = 1.0f - exp(-path * gUnderSigma.w);
	const float up = max(1.0f + gUnderLight.w * h / max(d, 1.0f), 0.0f);

	return col * exp(-path * gUnderSigma.rgb) + gUnderLight.rgb * (S * up);
}

/* A wall. The vertex as a card's: uv.xy the page (the floor's own place
 * carried down the wall, hd_spring.cpp), uv.z the way down from the lip and
 * uv.w the height over the bottom, units; col.rgb the floor's light at the
 * rim (colour1 + colour2, as RoomVS sums them), col.a the slit's width
 * across from the wall; viewZ.x the view depth.
 *
 * texel times light, as the floor beside it is drawn, times the share of
 * that light a wall of the slit still sees: the floor's light falls from
 * above, and a wall d units down sees the open sky only above the far lip,
 * w units across - in the slit's cross-section the cosine-weighted share
 * 0.5 (1 - sin a) of a level floor's, a the far lip's height over the
 * wall's horizon, sin a = d / sqrt(d^2 + w^2): a half at the lip, falling
 * as the slit narrows and deepens. Then the dark band from the bottom
 * (gCrackLook): no light of its own reaches into the crack, black at the
 * bottom. Then the distance fog, subtracted as RoomPS subtracts it, and the
 * water round an eye under it. */
float4 CrackPS(SPRING_IN input, float2 vpos : VPOS) : COLOR0
{
	const float3 texel = tex2D(sCrackPage, input.uv.xy).rgb;
	const float d = input.uv.z;
	const float sky = 0.5f * (1.0f - d * rsqrt(d * d + input.col.a *
		input.col.a + 1.0f));
	const float up = saturate(input.uv.w * gCrackLook.x);
	const float fogFrom = gFog.x + gUnderSigma.w * 1.0e30f;
	const float fog = max(0.0f, input.viewZ.x - fogFrom) * gFog.y *
		(2.0f / 255.0f);
	const float3 c = max(input.col.rgb - fog, 0.0f) * (sky * up);

	return float4(CrackUnder(texel * c, CrackView(vpos, input.viewZ.x)),
		1.0f);
}

/* The bottom: black, at the depth of the bottom's plane along the pixel's
 * own ray (gCrackFloor), whatever triangle of the crack's volume is drawn
 * over the pixel - the depth the sheets read there and the walls are tested
 * against. A ray that does not reach the plane in front of the eye takes
 * the far plane's 0.995 (hd_room.hlsl, SceneZ). */
struct HOLE_OUT
{
	float4 col : COLOR0;
	float depth : DEPTH;
};

HOLE_OUT HolePS(SPRING_IN input, float2 vpos : VPOS)
{
	HOLE_OUT o;
	const float3 ray = CrackView(vpos, 1.0f);
	const float along = dot(gCrackFloor.xyz, ray);
	const float z = along > 1.0e-4f ? gCrackFloor.w / along : -1.0f;
	const float depth = z > 1.0f ? gSpringScreen.z - gSpringScreen.w / z :
		0.995f;

	o.col = float4(CrackUnder(0.0f, ray * max(z, 1.0f)), 1.0f);
	o.depth = clamp(depth, 0.0f, 0.995f);
	return o;
}

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
 * stands behind it, and the bubbles in the jet. Both are quads the
 * processor builds in view space, as the water's cards are, projected by
 * the cards' own vertex shader (hd_room.hlsl, CardVS); this file holds the
 * two pixel halves and declares only what they read.
 *
 * Build. bshader.bat compiles it twice:
 *
 *     fxc /nologo /T ps_3_0 /E ColumnPS /O3 /Vn kHDSpringColumnPS
 *         /Fh hd_spring_column_ps.inc hd_spring.hlsl
 *     fxc /nologo /T ps_3_0 /E BubblePS /O3 /Vn kHDSpringBubblePS
 *         /Fh hd_spring_bubble_ps.inc hd_spring.hlsl
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
 * c134 gUnderSigma   the water's extinction while the eye is under the
 * c145 gUnderClear   water, the frame's own (hd_room.hlsl, UnderWater) */
float4 gFog : register(c16);
float4 gSpringScreen : register(c92);
float4 gSpringMode : register(c93);
float4 gSpringCell : register(c94);
float4 gUnderSigma : register(c134);
float4 gUnderClear : register(c145);

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
	 * the left of the middle. Free choices, set by eye on the round's
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

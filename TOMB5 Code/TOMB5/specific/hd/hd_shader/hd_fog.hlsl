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

/* The fog bulbs' body (hd_render.cpp, "the fog bulbs' body"; the rule and
 * its numbers are hd_fog.h's, line for line, where the reasons stand).
 *
 * One pass a frame over the union of the rectangles of the frame's bulbs'
 * fields on the screen (or the whole screen), drawn over the finished HD
 * scene: each pixel walks the frame's bulbs, farthest first. Per bulb its
 * eye ray is cut by the field's sphere - the bulb's reach - and by the
 * frame's depth, the field's density is integrated along the piece that is
 * left, each piece weighted with the bulb's share of the summed density of
 * the frame's fields (hd_fog.h, HDFogFieldShare), and the bulb's veil is
 * laid over the veils of the bulbs behind it, as the quad per bulb of
 * before laid it over the frame. The pixel is veiled by the sum - blended
 * SRC ONE, DEST SRCALPHA, the colour the fog's own light and alpha the
 * light that passes. A ray that meets no field leaves the pixel as it was,
 * to the bit (colour 0, alpha 1).
 *
 * The breath's lattice is read, not hashed: the pass's table (hd_render.cpp,
 * fog_table) holds HDFogHash at the 64 x 64 x 64 points of its fold, and
 * the card's filter blends the eight corners of a cell with the smoothstep
 * of the place inside it - HDFogNoise's own blend, its weights to a 256th.
 *
 * Build. bshader.bat compiles it,
 *
 *     fxc /nologo /T vs_3_0 /E FogVS /O3 /Vn kHDFogVS
 *         /Fh hd_fog_vs.inc hd_fog.hlsl
 *     fxc /nologo /T ps_3_0 /E FogPS /O3 /Vn kHDFogPS
 *         /Fh hd_fog_ps.inc hd_fog.hlsl
 *
 * and stamps the .inc files with the MD5 of this file.
 *
 * Constants, pixel side:
 *   c0 gFogRay     x 1 / _11, y 1 / _22 of the projection (the view ray of
 *                  a pixel at ndc (x, y) is (x gFogRay.x, y gFogRay.y, 1)),
 *                  z and w its a and b (the depth is a - b / z)
 *   c1 gFogScreen  the viewport's x, y, 1 / width, 1 / height
 *   c2 gFogTex     1 / the depth texture's width and height, the clock in
 *                  seconds, 1 for the breath and 0 without it
 *   c3 gFogCount   x the count of the frame's bulbs, 0..16, y and z the
 *                  slots the draw walks, y up to z - 0 and x for the one
 *                  pass; one slot a draw for RoomProbe's -fogquads
 *   c5 gFogEye     the eye in the world
 *   c6 - c8        gFogAxes, the world's x, y and z axes in view space
 *   c10 - c25      gFogFields, the frame's bulbs, farthest first: centre
 *                  in view space, one over the reach squared
 *   c26 - c29      gFogPeaks, their peaks (the density at the centre) four
 *                  a register
 *   c30 - c45      gFogColours, their colour 0..1 and density d times the
 *                  strength
 *   c46 - c49      gFogReach, their reaches four a register
 *   c50 - c53      gFogOwn, their own radii (the breath's cells) four a
 *                  register
 * Samplers: s0 the frame's depth (INTZ), point, clamped; s1 the breath's
 * table, linear, wrapped. */

float4 gFogRay : register(c0);
float4 gFogScreen : register(c1);
float4 gFogTex : register(c2);
float4 gFogCount : register(c3);
float4 gFogEye : register(c5);
float4 gFogAxes[3] : register(c6);
float4 gFogFields[16] : register(c10);
float4 gFogPeaks[4] : register(c26);
float4 gFogColours[16] : register(c30);
float4 gFogReach[4] : register(c46);
float4 gFogOwn[4] : register(c50);
sampler2D sFogDepth : register(s0);
sampler3D sFogHash : register(s1);

// hd_fog.h's HD_FOG_PEAK, HD_FOG_BREATH, HD_FOG_CELL, HD_FOG_RISE,
// HD_FOG_STEPS, HD_FOG_PASS_SLOTS and HD_FOG_TABLE.
#define FOG_PEAK	1.5f
#define FOG_BREATH	0.5f
#define FOG_CELL	0.5f
#define FOG_RISE	0.25f
#define FOG_STEPS	4
#define FOG_SLOTS	16
#define FOG_TABLE	64.0f

struct FogVertex
{
	float4 pos : POSITION;
};

FogVertex FogVS(float4 corner : POSITION)
{
	FogVertex o;

	o.pos = corner;
	return o;
}

/* HDFogNoise: the eight hashed corners of q's cell, smoothly blended - the
 * table's texel (c mod 64) holds HDFogHash(c), and a sample at c + s + 0.5
 * texels blends the corners c and c + 1 of each axis by s, the smoothstep
 * of q's place in its cell. */
float FogNoise(float3 q)
{
	const float3 c = floor(q);
	const float3 f = q - c;
	const float3 s = f * f * (3.0f - 2.0f * f);

	return tex3Dlod(sFogHash, float4((c + s + 0.5f) * (1.0f / FOG_TABLE),
		0.0f)).r;
}

// HDFogColumn: G(s1) - G(s0), G(s) = (1 - beta2) s - s^3 / 3.
float FogColumn(float k, float s0, float s1)
{
	return (k * s1 - s1 * s1 * s1 / 3.0f) - (k * s0 - s0 * s0 * s0 / 3.0f);
}

/* HDFogFieldDensity summed at p (view space) over the frame's fields, the
 * bulb's own (`own`) the first. A field that does not meet the bulb's adds
 * nothing at a point of its reach. Unrolled, as ps_3_0 reads no constant
 * register by a run time index; a slot past the count is skipped by a
 * branch every pixel takes alike. */
float FogFieldSum(float3 p, int own)
{
	const float3 o = p - gFogFields[own].xyz;
	float sum = gFogPeaks[own / 4][own % 4] *
		max(1.0f - dot(o, o) * gFogFields[own].w, 0.0f);

	[unroll]
	for (int j = 0; j < FOG_SLOTS; j++)
	{
		if (j == own)
			continue;

		[branch]
		if ((float)j < gFogCount.x)
		{
			const float3 v = p - gFogFields[j].xyz;

			sum += gFogPeaks[j / 4][j % 4] *
				max(1.0f - dot(v, v) * gFogFields[j].w, 0.0f);
		}
	}

	return sum;
}

float4 FogPS(float2 vpos : VPOS) : COLOR
{
	const float2 pix = vpos + 0.5f;
	const float2 ndc = float2(
		(pix.x - gFogScreen.x) * gFogScreen.z * 2.0f - 1.0f,
		1.0f - (pix.y - gFogScreen.y) * gFogScreen.w * 2.0f);
	const float3 dir = float3(ndc.x * gFogRay.x, ndc.y * gFogRay.y, 1.0f);
	const float depth = tex2Dlod(sFogDepth, float4(pix * gFogTex.xy, 0.0f,
		0.0f)).r;
	// The surface's view depth; a cleared pixel comes out very far away.
	const float zs = gFogRay.w / max(gFogRay.z - depth, 1.0e-6f);
	const float len = length(dir);
	const float3 u = dir / len;
	// The veils laid so far: their light, and the light they let through.
	float3 light = float3(0.0f, 0.0f, 0.0f);
	float through = 1.0f;

	[unroll]
	for (int n = 0; n < FOG_SLOTS; n++)
	{
		[branch]
		if ((float)n >= gFogCount.y && (float)n < gFogCount.z)
		{
			// HDFogSpan: the piece of the ray inside the field's sphere, in
			// reaches from the point nearest the centre.
			const float3 c = gFogFields[n].xyz;
			const float rad = gFogReach[n / 4][n % 4];
			const float m = dot(u, c);
			const float b2 = max((dot(c, c) - m * m) / (rad * rad), 0.0f);
			const float edge = sqrt(max(1.0f - b2, 0.0f));
			const float s0 = max(-m / rad, -edge);
			const float s1 = min((zs * len - m) / rad, edge);

			[branch]
			if (b2 < 1.0f && s1 > s0)
			{
				/* HDFogFieldTau: the column in pieces, each with the bulb's
				 * share of the fields' sum and the breath (cells of its own
				 * radius) at its middle. */
				const float k = 1.0f - b2;
				const float cell = FOG_CELL * max(gFogOwn[n / 4][n % 4],
					1.0f);
				const float peak = gFogPeaks[n / 4][n % 4];
				float column = 0.0f;

				[loop]
				for (int i = 0; i < FOG_STEPS; i++)
				{
					const float a = s0 + (s1 - s0) * (float)i / FOG_STEPS;
					const float b = s0 + (s1 - s0) * (float)(i + 1) /
						FOG_STEPS;
					const float3 p = u * (m + 0.5f * (a + b) * rad);
					const float3 world = gFogEye.xyz + float3(
						dot(p, gFogAxes[0].xyz), dot(p, gFogAxes[1].xyz),
						dot(p, gFogAxes[2].xyz));
					const float3 q = world / cell + float3(0.0f,
						gFogTex.z * FOG_RISE, 0.0f);
					const float sum = FogFieldSum(p, n);
					const float breath = 1.0f - FOG_BREATH * gFogTex.w *
						FogNoise(q);
					// A select: under the peak the weight is the breath to
					// the bit.
					const float w = sum > peak ? breath * (peak / sum) :
						breath;

					column += w * FogColumn(k, a, b);
				}

				const float tau = gFogColours[n].w * FOG_PEAK * rad * column /
					255.0f;
				const float lets = exp(-max(tau, 0.0f));

				// This veil over the ones behind it.
				light = gFogColours[n].rgb * (1.0f - lets) + light * lets;
				through *= lets;
			}
		}
	}

	return float4(light, through);
}

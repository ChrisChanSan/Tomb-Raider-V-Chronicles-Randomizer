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
 * One quad per fog bulb over the sphere's own rectangle on the screen,
 * drawn once a frame over the finished HD scene: each pixel's eye ray is
 * cut by the sphere and by the frame's depth, the bulb's density is
 * integrated along the piece that is left, and the pixel is veiled by it -
 * blended SRC ONE, DEST SRCALPHA, the colour the fog's own light and alpha
 * the light that passes. A ray that misses the bulb leaves the pixel as it
 * was, to the bit (colour 0, alpha 1).
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
 *   c3 gFogBulb    the bulb's centre in view space, its radius
 *   c4 gFogColour  its colour 0..1, its density d
 *   c5 gFogEye     the eye in the world
 *   c6 - c8        gFogAxes, the world's x, y and z axes in view space
 * Sampler s0: the frame's depth (INTZ), point, clamped. */

float4 gFogRay : register(c0);
float4 gFogScreen : register(c1);
float4 gFogTex : register(c2);
float4 gFogBulb : register(c3);
float4 gFogColour : register(c4);
float4 gFogEye : register(c5);
float4 gFogAxes[3] : register(c6);
sampler2D sFogDepth : register(s0);

// hd_fog.h's HD_FOG_PEAK, HD_FOG_BREATH, HD_FOG_CELL, HD_FOG_RISE and
// HD_FOG_STEPS.
#define FOG_PEAK	1.5f
#define FOG_BREATH	0.5f
#define FOG_CELL	0.5f
#define FOG_RISE	0.25f
#define FOG_STEPS	4

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

// HDFogHash: a lattice point's value in 0..1.
float FogHash(float3 cell)
{
	float3 p = cell - 64.0f * floor(cell * (1.0f / 64.0f));

	p = frac(p * float3(0.1361f, 0.1187f, 0.1543f) + 0.317f);
	p = frac(p * (p.yzx * 47.31f + 13.73f));
	p = frac(p * (p.zxy * 31.17f + 7.91f));
	return frac(p.x + p.y * 1.618f + p.z * 2.718f);
}

// HDFogNoise: the eight hashed corners of q's cell, smoothly blended.
float FogNoise(float3 q)
{
	const float3 c = floor(q);
	float3 f = q - c;

	f = f * f * (3.0f - 2.0f * f);

	const float a = lerp(FogHash(c), FogHash(c + float3(1, 0, 0)), f.x);
	const float b = lerp(FogHash(c + float3(0, 1, 0)),
		FogHash(c + float3(1, 1, 0)), f.x);
	const float d = lerp(FogHash(c + float3(0, 0, 1)),
		FogHash(c + float3(1, 0, 1)), f.x);
	const float e = lerp(FogHash(c + float3(0, 1, 1)),
		FogHash(c + float3(1, 1, 1)), f.x);

	return lerp(lerp(a, b, f.y), lerp(d, e, f.y), f.z);
}

// HDFogColumn: G(s1) - G(s0), G(s) = (1 - beta2) s - s^3 / 3.
float FogColumn(float k, float s0, float s1)
{
	return (k * s1 - s1 * s1 * s1 / 3.0f) - (k * s0 - s0 * s0 * s0 / 3.0f);
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

	// HDFogSpan: the piece of the ray inside the sphere, in radii from the
	// point nearest the centre.
	const float3 c = gFogBulb.xyz;
	const float rad = gFogBulb.w;
	const float len = length(dir);
	const float3 u = dir / len;
	const float m = dot(u, c);
	const float b2 = max((dot(c, c) - m * m) / (rad * rad), 0.0f);

	if (b2 >= 1.0f)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);

	const float edge = sqrt(1.0f - b2);
	const float s0 = max(-m / rad, -edge);
	const float s1 = min((zs * len - m) / rad, edge);

	if (s1 <= s0)
		return float4(0.0f, 0.0f, 0.0f, 1.0f);

	// HDFogRayTau: the column in pieces, each with the breath at its middle.
	const float k = 1.0f - b2;
	const float cell = FOG_CELL * max(rad, 1.0f);
	float column = 0.0f;

	[loop]
	for (int i = 0; i < FOG_STEPS; i++)
	{
		const float a = s0 + (s1 - s0) * (float)i / FOG_STEPS;
		const float b = s0 + (s1 - s0) * (float)(i + 1) / FOG_STEPS;
		const float3 p = u * (m + 0.5f * (a + b) * rad);
		const float3 world = gFogEye.xyz + float3(dot(p, gFogAxes[0].xyz),
			dot(p, gFogAxes[1].xyz), dot(p, gFogAxes[2].xyz));
		const float3 q = world / cell + float3(0.0f, gFogTex.z * FOG_RISE,
			0.0f);
		const float w = 1.0f - FOG_BREATH * gFogTex.w * FogNoise(q);

		column += w * FogColumn(k, a, b);
	}

	const float tau = gFogColour.w * FOG_PEAK * rad * column / 255.0f;
	const float through = exp(-max(tau, 0.0f));

	return float4(gFogColour.rgb * (1.0f - through), through);
}

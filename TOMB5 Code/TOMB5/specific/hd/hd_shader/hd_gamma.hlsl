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

/* The gamma row's pass (specific/dxshell.cpp, "the gamma pass"): the whole
 * presented picture, copied out of the back buffer into a texture, drawn
 * back over it through this curve, once a frame, just before the present.
 * The pass never runs at the row's middle, so this file holds the curve
 * and nothing else.
 *
 * Build. bshader.bat compiles it once,
 *
 *     fxc /nologo /T ps_2_0 /E GammaPS /O3 /Vn kHDGammaPS
 *         /Fh hd_gamma_ps.inc hd_gamma.hlsl
 *
 * and stamps the .inc with the MD5 of this file. ps_2_0, not the ps_3_0 of
 * the other shaders: the quad is pretransformed and drawn without a vertex
 * shader, which ps_3_0 does not allow.
 *
 * The curve. The row's number n runs from 0 to 100 and stands for
 *
 *     gamma  = 2 ^ ((n - 50) / 50)       0.5 at 0, 1 at 50, 2 at 100
 *     output = input ^ (1 / gamma)       per channel, 0 to 1
 *
 * so every step of the row is the same factor on gamma, 2 ^ (1/50), and
 * the two ends are one octave each way: 100 lifts a channel of 0.1 to
 * 0.32, 0 sinks one of 0.5 to 0.25. Black stays black and white stays
 * white at every n; the alpha is not read by the present and is written
 * as 1. */

sampler2D gFrame : register(s0);	// the copy, point sampled, clamped
float4 gRow : register(c0);		// x: the row's number, 0 to 100

float4 GammaPS(float2 uv : TEXCOORD0) : COLOR0
{
	const float power = exp2((50.0 - gRow.x) / 50.0);
	const float3 c = tex2D(gFrame, uv).rgb;

	return float4(pow(c, power), 1.0);
}

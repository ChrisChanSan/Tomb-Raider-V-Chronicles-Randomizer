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

/* The projected shadow's maps: the cast shaders. hd_model.cpp draws Lara's
 * composite with them into a D3DFMT_R32F target as seen along the key light,
 * and the creatures into the second map, a D3DFMT_G32R32F target (hd_shadow.h
 * has the arithmetic, hd_render.cpp the targets).
 *
 * This file includes hd_model.hlsl and calls ModelVS as a function, so the
 * map is skinned by the very lines that skin the picture - the bind fold,
 * the three bones, the chest's soft motion - and hd_model.hlsl itself is not
 * changed by it. Only the projection differs: hd_model.cpp puts the map's
 * clip matrix (HDShadowCastMatrix) into gProj, c0 - c3, for the length of
 * the pass, and HDRenderShadowCastEnd puts the frame's back. fxc drops the
 * light, the sheen and the head cut, which nothing below reads.
 *
 * Build. bshader.bat compiles it with
 *
 *     fxc /nologo /T vs_3_0 /E CastVS /O3 /Vn kHDShadowCastVS /Fh hd_shadow_cast_vs.inc hd_shadow.hlsl
 *     fxc /nologo /T ps_3_0 /E CastPS /O3 /Vn kHDShadowCastPS /Fh hd_shadow_cast_ps.inc hd_shadow.hlsl
 *
 * The stamp on both .inc is the MD5 of this file. bshader.bat always
 * compiles every shader together, so an edit to hd_model.hlsl, which be.bat's
 * guard sees through the model shader's own stamp, regenerates these too.
 *
 * Registers: the vertex side reads what ModelVS reads (c0 - c3 the map's
 * clip matrix, c8 - c103 the joints, c163 - c166 the soft motion), the pixel
 * side gPass in c4 (x: the alpha test, set per range exactly as for the
 * picture), gCasterPlace in c18 and the page in s0. */

#include "hd_model.hlsl"

/* x: the depth of the caster's place - the point at its feet, the item's
 * position - in the map being drawn, on the scale of `depth` below.
 * hd_render.cpp sets it per entry for the second map (HDRenderShadowPlace),
 * whose green channel keeps it, so the receive measures how far a pixel
 * lies behind a creature from its feet, whatever part of it the texel holds;
 * her map has no green channel and drops it. c18 is no register of
 * hd_model.hlsl's pixel side; the cast bracket puts the frame's back. */
float4 gCasterPlace : register(c18);

struct CAST_OUT
{
	float4 pos   : POSITION;
	float2 uv    : TEXCOORD0;
	/* The map's depth, 0 at the box's side towards the light and 1 at the
	 * far side - z of the orthographic clip position, whose w is 1. */
	float  depth : TEXCOORD1;
};

struct CAST_IN
{
	float2 uv    : TEXCOORD0;
	float  depth : TEXCOORD1;
};

CAST_OUT CastVS(float4 inPos : POSITION, float4 inNormal : NORMAL,
	float4 inBone : BLENDINDICES, float4 inWeight : BLENDWEIGHT)
{
	VS_OUT m = ModelVS(inPos, inNormal, inBone, inWeight);
	CAST_OUT o;

	o.pos = m.pos;
	o.uv = m.uv;
	o.depth = m.pos.z;

	return o;
}

/* The depth into the red channel and the caster's place into the green,
 * where the alpha test of the part's page lets the fragment through - a
 * cut-out leaves its holes out of the map as it leaves them out of the
 * picture. No head cut: in first person the map still holds her whole
 * body. */
float4 CastPS(CAST_IN input) : COLOR0
{
	float4 texel = tex2D(sPage, input.uv);

	clip(gPass.x * (texel.a - 0.5f));

	return float4(input.depth, gCasterPlace.x, 0.0f, 1.0f);
}

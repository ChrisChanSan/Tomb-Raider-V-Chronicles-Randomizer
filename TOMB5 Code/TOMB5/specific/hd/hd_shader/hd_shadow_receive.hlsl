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

/* The projected shadow on the rooms: the receive pixel shaders. ReceivePS
 * reads her map alone; Receive2PS, at the end of this file, reads her map
 * and the second map, in a frame whose second map was drawn.
 *
 * hd_render.cpp draws the HD room groups near her a second time with the
 * room's own vertex shader (RoomVS in hd_room.hlsl, unchanged), the same
 * vertex constants and the same ranges as their opaque and alpha passes,
 * the depth test EQUAL and no depth write, and this shader in place of
 * RoomPS. The same vertex shader with the same constants gives the same
 * depth to the bit, so EQUAL keeps exactly the pixels that face left
 * visible. What this shader writes is a factor, and the blend (source ZERO,
 * destination SRCCOLOR, colour write RGB only) multiplies the picture with
 * it:
 *
 *     1 - strength * lookup * edge * height * facing
 *
 * hd_shadow.h holds every term as a function of its own (HDShadowLookup,
 * HDShadowEdgeTerm, HDShadowHeightTerm, HDShadowFacingTerm) and the register
 * block (HDShadowReceiveConstants); this file is their transcription.
 *
 * A pixel shader reads the vertex shader's outputs by semantic, so this one
 * declares the three it needs - the texture coordinate (TEXCOORD0), the
 * view-space position (TEXCOORD2) and the view-space normal (TEXCOORD5),
 * which RoomVS hands down for the fog and the sheen - and ignores the rest.
 *
 * Build. bshader.bat compiles it with
 *
 *     fxc /nologo /T ps_3_0 /E ReceivePS /O3 /Vn kHDShadowReceivePS
 *         /Fh hd_shadow_receive_ps.inc hd_shadow_receive.hlsl
 *
 * and stamps the .inc with the MD5 of this file.
 *
 * Registers. The page in s0 and gPass in c4, x the alpha reference, as the
 * room's own passes set them per range: a cut-out's holes stay holes, and a
 * cut-out drawn over a face in the same plane darkens its own texels and no
 * others. The map in s3, a stage no room or model draw samples, point
 * sampled and clamped; hd_render.cpp binds it for the pass and unbinds it
 * after. c96 - c102 are this shader's own (hd_shadow.h,
 * HD_SHADOW_RECEIVE_FIRST): above every register the room, gloss, glow and
 * model pixel shaders name (the highest is the gloss variant's c91), so no
 * later draw expects one of them to have kept its value. */

sampler2D sPage : register(s0);
sampler2D sMap : register(s3);

/* x the alpha reference of the range, as the room's passes set it. */
float4 gPass : register(c4);

/* u, v and the depth along the light, each dot(float4(viewPos, 1), row). */
float4 gMapU : register(c96);
float4 gMapV : register(c97);
float4 gMapD : register(c98);

/* The height term before its saturate, a plane in view space. */
float4 gHeight : register(c99);

/* xyz the unit direction towards the light in view space, w 1 / FACING. */
float4 gToLight : register(c100);

/* x the strength, y 1 / (EDGE / 2), z the bias in depth units, w the map's
 * side in texels. */
float4 gShadow : register(c101);

/* x 1 / the map's side. */
float4 gTexel : register(c102);

struct RECEIVE_IN
{
	float2 uv      : TEXCOORD0;
	float3 viewPos : TEXCOORD2;
	float3 viewNrm : TEXCOORD5;
};

float4 ReceivePS(RECEIVE_IN input) : COLOR0
{
	/* The alpha test of the range, the room shader's own line. */
	float4 texel = tex2D(sPage, input.uv);

	clip(texel.a - gPass.x);

	const float4 at = float4(input.viewPos, 1.0f);
	const float u = dot(at, gMapU);
	const float v = dot(at, gMapV);
	const float depth = dot(at, gMapD);

	/* The three terms. Outside the box in u or v the edge term is 0 already,
	 * which is the factor 1 the branch below then leaves alone. */
	const float edge = saturate(min(min(u, 1.0f - u), min(v, 1.0f - v)) * gShadow.y);
	const float height = saturate(dot(at, gHeight));

	/* viewNrm is not renormalised and a vertex may carry a zero normal; the
	 * room shader's own guard. */
	const float3 n = input.viewNrm * rsqrt(max(dot(input.viewNrm, input.viewNrm), 1e-8f));
	const float facing = saturate(dot(n, gToLight.xyz) * gToLight.w);
	const float terms = edge * height * facing;

	float shade = 0.0f;

	/* The lookup only where it can matter: a dynamic branch, and the taps
	 * read with an explicit level, which a branch allows. The 4 x 4 texels
	 * round the position, weighted 1 - f, 1, 1, f along each axis (sum 9),
	 * so the result is continuous in the position. A texel casts when it is
	 * not the clear value 1 and lies nearer the light than the pixel by more
	 * than the bias. */
	[branch]
	if (terms > 0.0f)
	{
		const float2 s = float2(u, v) * gShadow.w - 0.5f;
		const float2 base = floor(s);
		const float2 f = s - base;
		const float4 wx = float4(1.0f - f.x, 1.0f, 1.0f, f.x);
		const float limit = min(depth - gShadow.z, 1.0f);
		float sum = 0.0f;

		[unroll]
		for (int j = 0; j < 4; j++)
		{
			const float y = (base.y + (float)j - 0.5f) * gTexel.x;
			float4 tap;

			tap.x = tex2Dlod(sMap, float4((base.x - 0.5f) * gTexel.x, y, 0.0f, 0.0f)).r;
			tap.y = tex2Dlod(sMap, float4((base.x + 0.5f) * gTexel.x, y, 0.0f, 0.0f)).r;
			tap.z = tex2Dlod(sMap, float4((base.x + 1.5f) * gTexel.x, y, 0.0f, 0.0f)).r;
			tap.w = tex2Dlod(sMap, float4((base.x + 2.5f) * gTexel.x, y, 0.0f, 0.0f)).r;

			const float row = dot(wx, (tap < limit) ? 1.0f : 0.0f);
			const float wy = j == 0 ? 1.0f - f.y : (j == 3 ? f.y : 1.0f);

			sum += wy * row;
		}

		shade = sum * (1.0f / 9.0f);
	}

	return float4((1.0f - gShadow.x * shade * terms).xxx, 1.0f);
}

/* The two-map variant, Receive2PS: the one-map shader above, line for line,
 * and the second map beside it - the map the creatures and the other items
 * with a blob are cast into, a larger box round hers on the same light.
 * hd_render.cpp draws with it in a frame whose second map was drawn, and
 * with ReceivePS in every other frame. It writes
 *
 *     1 - max(strength * hers, strength * theirs)
 *
 * with `hers` the one-map shader's lookup times its terms, and `theirs` the
 * second map's: the same sixteen taps and weights, each casting tap weighted
 * by how far the pixel lies behind that tap's caster along the light (1 up
 * to OTHERS_FULL, 0 from OTHERS_END on - hd_shadow.h, HDShadowBehindTerm,
 * HDShadowLookupBehind), times the edge term of the second box and the same
 * facing term. A floor under two overlapping shadows is as dark as under
 * the darker one. Where the second map gives nothing the max is her own
 * product, so the pixel is the pixel ReceivePS writes.
 *
 * Build. bshader.bat compiles it out of this file with
 *
 *     fxc /nologo /T ps_3_0 /E Receive2PS /O3 /Vn kHDShadowReceive2PS
 *         /Fh hd_shadow_receive2_ps.inc hd_shadow_receive.hlsl
 *
 * and stamps the .inc with this file's MD5, as it stamps the one-map .inc.
 *
 * Registers. Everything ReceivePS names, and the second map in s4, the next
 * stage no room or model draw samples, point sampled and clamped like s3;
 * c103 - c107 the second map's own (hd_shadow.h, HD_SHADOW_RECEIVE2_FIRST,
 * HDShadowReceiveOthersConstants), right after her block and as far above
 * the room, gloss, glow and model shaders' registers. */

sampler2D sOthers : register(s4);

/* u, v and the depth along the light in the second map. */
float4 gOthersU : register(c103);
float4 gOthersV : register(c104);
float4 gOthersD : register(c105);

/* x the bias in the second map's depth units, y its side in texels, z
 * 1 / its side. */
float4 gOthersMap : register(c106);

/* The distance term as a line in a tap's depth difference: x the second
 * box's depth / (OTHERS_END - OTHERS_FULL), y OTHERS_END / (OTHERS_END -
 * OTHERS_FULL). */
float4 gOthersBehind : register(c107);

float4 Receive2PS(RECEIVE_IN input) : COLOR0
{
	/* Her part: ReceivePS's own lines. */
	float4 texel = tex2D(sPage, input.uv);

	clip(texel.a - gPass.x);

	const float4 at = float4(input.viewPos, 1.0f);
	const float u = dot(at, gMapU);
	const float v = dot(at, gMapV);
	const float depth = dot(at, gMapD);

	const float edge = saturate(min(min(u, 1.0f - u), min(v, 1.0f - v)) * gShadow.y);
	const float height = saturate(dot(at, gHeight));

	const float3 n = input.viewNrm * rsqrt(max(dot(input.viewNrm, input.viewNrm), 1e-8f));
	const float facing = saturate(dot(n, gToLight.xyz) * gToLight.w);
	const float terms = edge * height * facing;

	float shade = 0.0f;

	[branch]
	if (terms > 0.0f)
	{
		const float2 s = float2(u, v) * gShadow.w - 0.5f;
		const float2 base = floor(s);
		const float2 f = s - base;
		const float4 wx = float4(1.0f - f.x, 1.0f, 1.0f, f.x);
		const float limit = min(depth - gShadow.z, 1.0f);
		float sum = 0.0f;

		[unroll]
		for (int j = 0; j < 4; j++)
		{
			const float y = (base.y + (float)j - 0.5f) * gTexel.x;
			float4 tap;

			tap.x = tex2Dlod(sMap, float4((base.x - 0.5f) * gTexel.x, y, 0.0f, 0.0f)).r;
			tap.y = tex2Dlod(sMap, float4((base.x + 0.5f) * gTexel.x, y, 0.0f, 0.0f)).r;
			tap.z = tex2Dlod(sMap, float4((base.x + 1.5f) * gTexel.x, y, 0.0f, 0.0f)).r;
			tap.w = tex2Dlod(sMap, float4((base.x + 2.5f) * gTexel.x, y, 0.0f, 0.0f)).r;

			const float row = dot(wx, (tap < limit) ? 1.0f : 0.0f);
			const float wy = j == 0 ? 1.0f - f.y : (j == 3 ? f.y : 1.0f);

			sum += wy * row;
		}

		shade = sum * (1.0f / 9.0f);
	}

	/* The second map's part: its own place, its own edge, the same facing,
	 * and no height plane - the distance term per tap takes its place. */
	const float ou = dot(at, gOthersU);
	const float ov = dot(at, gOthersV);
	const float odepth = dot(at, gOthersD);
	const float oterms = saturate(min(min(ou, 1.0f - ou), min(ov, 1.0f - ov)) * gShadow.y) *
		facing;

	float oshade = 0.0f;

	[branch]
	if (oterms > 0.0f)
	{
		const float2 s = float2(ou, ov) * gOthersMap.y - 0.5f;
		const float2 base = floor(s);
		const float2 f = s - base;
		const float4 wx = float4(1.0f - f.x, 1.0f, 1.0f, f.x);
		const float limit = min(odepth - gOthersMap.x, 1.0f);
		float sum = 0.0f;

		[unroll]
		for (int j = 0; j < 4; j++)
		{
			const float y = (base.y + (float)j - 0.5f) * gOthersMap.z;
			float4 tap;

			tap.x = tex2Dlod(sOthers, float4((base.x - 0.5f) * gOthersMap.z, y, 0.0f, 0.0f)).r;
			tap.y = tex2Dlod(sOthers, float4((base.x + 0.5f) * gOthersMap.z, y, 0.0f, 0.0f)).r;
			tap.z = tex2Dlod(sOthers, float4((base.x + 1.5f) * gOthersMap.z, y, 0.0f, 0.0f)).r;
			tap.w = tex2Dlod(sOthers, float4((base.x + 2.5f) * gOthersMap.z, y, 0.0f, 0.0f)).r;

			/* A casting tap counts with how far the pixel lies behind its
			 * caster; the clear value 1 never lies below the limit. */
			const float4 behind = saturate(gOthersBehind.y - gOthersBehind.x * (odepth - tap));
			const float row = dot(wx, (tap < limit) ? behind : 0.0f);
			const float wy = j == 0 ? 1.0f - f.y : (j == 3 ? f.y : 1.0f);

			sum += wy * row;
		}

		oshade = sum * (1.0f / 9.0f);
	}

	/* 1 - max(strength * hers, strength * theirs), written as the smaller of
	 * the two factors: her factor is then ReceivePS's own expression, and
	 * where the second map gives nothing the other factor is 1 and the
	 * pixel is ReceivePS's to the bit. */
	const float herFactor = 1.0f - gShadow.x * shade * terms;
	const float theirFactor = 1.0f - gShadow.x * oshade * oterms;

	return float4(min(herFactor, theirFactor).xxx, 1.0f);
}

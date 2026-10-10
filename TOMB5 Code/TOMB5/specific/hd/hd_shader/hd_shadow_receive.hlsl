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

/* The projected shadow on the rooms and the models: the receive pixel
 * shaders. ReceivePS reads her map alone; Receive2PS, further down, reads
 * her map and the second map, in a frame whose second map was drawn;
 * MirrorReceivePS is the two-map body on the copy behind a mirror's glass,
 * through the mirror's blocks; and ModelReceivePS, at the end of this file,
 * is the two-map body on an HD model's vertex outputs.
 *
 * hd_render.cpp draws the HD room groups near her a second time with the
 * room's own vertex shader (RoomVS in hd_room.hlsl, unchanged), the same
 * vertex constants and the same ranges as their opaque and alpha passes,
 * the depth test EQUAL and no depth write, and this shader in place of
 * RoomPS. The same vertex shader with the same constants gives the same
 * depth to the bit, so EQUAL keeps exactly the pixels that face left
 * visible. What this shader writes is a factor in alpha, and the blend
 * (source ONE, destination SRCALPHA, colour write RGB only) multiplies the
 * picture with it:
 *
 *     k = 1 - strength * lookup * edge * height * facing
 *
 * hd_shadow.h holds every term as a function of its own (HDShadowLookup,
 * HDShadowEdgeTerm, HDShadowHeightTerm, HDShadowFacingTerm) and the register
 * block (HDShadowReceiveConstants); this file is their transcription.
 *
 * The fog bulbs. RoomPS and ModelPS draw a surface as texel * light + haze,
 * the haze the bulbs' own colour added in front of it (hd_room.hlsl, "The
 * fog bulbs"): the shadow belongs to the surface and not to the haze in
 * front of it. So the colour this shader writes is the haze times 1 - k,
 * which the blend adds to the picture times k: (surface + haze) * k +
 * haze * (1 - k) = surface * k + haze. Where no bulb stands, the haze and
 * the colour are 0 and the picture is the picture times k, as a blend of
 * ZERO and SRCCOLOR made it. The distance fog needs nothing: it takes light
 * off the surface towards black (no colour is added), and the picture times
 * k fades the shadow out with the surface it lies on, to nothing where the
 * fog has made the surface black.
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
 * later draw expects one of them to have kept its value. The water sheet's
 * WaterPS names c92 - c103 and so overlaps c96 - c102, but it writes all of
 * them before each of its draws, and no sheet is drawn inside a receive
 * pass, so neither shader relies on what the other left there. The bulbs'
 * count in c17 (y) and the bulbs in c40 - c71, as the room's or the model's
 * draw set them - the frame's, the same for every room and model - and
 * gPass.w, the range's switch of the added colour. */

sampler2D sPage : register(s0);
sampler2D sMap : register(s3);

/* x the alpha reference of the range, as the room's passes set it; w 1 where
 * the range adds the bulbs' colour (hd_room.hlsl, gPass). */
float4 gPass : register(c4);

/* y the fog bulbs of the frame (hd_room.hlsl and hd_model.hlsl, gCounts). */
float4 gCounts : register(c17);

/* The bulbs, two registers each, hd_room.hlsl's and hd_model.hlsl's gBulbs:
 * (x, y, z, radius) in view space, then (r, g, b, density). */
#define HD_MAX_BULBS  16
float4 gBulbs[HD_MAX_BULBS * 2] : register(c40);

/* One fog bulb's contribution, line for line hd_room.hlsl's BulbIntensity
 * (and hd_model.hlsl's, which is the same): the length of the piece of the
 * eye ray inside the bulb's sphere times its density, its rim softened by a
 * smoothstep over the outer 0.35 of its own share of the diameter. A copy,
 * because this file stands alone beside the room shader; it must stay the
 * room's, or the haze this shader gives back would not be the haze the
 * picture holds. */
float BulbIntensity(float3 stash, float4 a, float4 b)
{
	float3 P = a.xyz;
	float rad = a.w;
	float sqrad = rad * rad;
	float sqlen = dot(P, P);

	if (P.z + rad <= 0.0f)
		return 0.0f;

	float3 vec = 0.0f;
	float3 vec2 = 0.0f;
	float3 d = P - stash;
	float val = dot(d, d);

	if (sqlen < sqrad)
	{
		if (val < sqrad)
			vec2 = stash;
		else
		{
			float3 u = stash * rsqrt(max(dot(stash, stash), 1e-12f));
			float val2 = dot(P, u);
			vec2 = u * (val2 + sqrt(max(sqrad - (sqlen - val2 * val2), 0.0f)));
		}
	}
	else if (val < sqrad)
	{
		vec2 = stash;
		float3 u = stash * rsqrt(max(dot(stash, stash), 1e-12f));
		float val2 = dot(P, u);
		vec = u * (val2 - sqrt(max(sqrad - (sqlen - val2 * val2), 0.0f)));
	}
	else
	{
		float len2 = dot(stash, stash);
		float3 u = stash * rsqrt(max(len2, 1e-12f));
		float val2 = dot(P, u);

		vec = u;

		if (val2 > 0.0f)
		{
			float val3 = val2 * val2;

			if (len2 > val3)
			{
				float perp = sqlen - val3;

				if (perp < sqrad)
				{
					float chord = sqrt(max(sqrad - perp, 0.0f));
					vec2 = u * (val2 - chord);
					vec = u * (val2 + chord);
				}
				else
				{
					vec = 0.0f;
					vec2 = 0.0f;
				}
			}
		}
	}

	const float kRim = 0.35f;

	float chord = length(vec2 - vec);
	float t = saturate(chord / max(2.0f * rad, 1e-4f));

	return chord * b.w * smoothstep(0.0f, kRim, t);
}

/* What every receive writes for the factor `k` at the view-space point
 * `viewPos`: the bulbs' haze there times 1 - k in colour - the sum RoomPS
 * and ModelPS add, saturated as they saturate it, times gPass.w - and k in
 * alpha. The bulbs are summed only where the pixel is darkened at all. */
float4 ReceiveOut(float k, float3 viewPos)
{
	float3 haze = 0.0f;

	[branch]
	if (k < 1.0f)
	{
		[loop]
		for (int j = 0; j < (int)gCounts.y; j++)
			haze += BulbIntensity(viewPos, gBulbs[j * 2 + 0], gBulbs[j * 2 +
				1]) *
				gBulbs[j * 2 + 1].rgb * (1.0f / 255.0f);

		haze = saturate(haze) * gPass.w;
	}

	return float4(haze * (1.0f - k), k);
}

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

	return ReceiveOut(1.0f - gShadow.x * shade * terms, input.viewPos);
}

/* The two-map variant, Receive2PS: the one-map shader above and the second
 * map beside it - the map the creatures and the other items with a blob are
 * cast into, a larger box round hers on the same light. hd_render.cpp draws
 * with it in a frame whose second map was drawn, and with ReceivePS in every
 * other frame. It writes
 *
 *     1 - max(strength * hers, strength * theirs)
 *
 * with `hers` the one-map shader's lookup times its terms (HerShade, line
 * for line ReceivePS's), and `theirs` the second map's (OthersShade): the
 * same sixteen taps and weights, each casting tap weighted by how far the
 * pixel lies behind that tap's caster along the light (1 up to OTHERS_FULL,
 * 0 from OTHERS_END on - hd_shadow.h, HDShadowBehindTerm) and by the fade of
 * the tap's own depth near the second box's near and far side
 * (HDShadowFadeTerm; both in HDShadowLookupBehind), times the edge term of
 * the second box and the same facing term. A floor under two overlapping
 * shadows is as dark as under the darker one. Where the second map gives
 * nothing the max is her own product, so the pixel is the pixel ReceivePS
 * writes.
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
 * OTHERS_FULL); z 1 / OTHERS_FADE, the fade's slope in a tap's depth. */
float4 gOthersBehind : register(c107);

/* Her part at the view-space point `at` for a face whose facing term is
 * `facing`: x her lookup, y her terms - ReceivePS's own lines, so a caller
 * that writes 1 - gShadow.x * x * y writes ReceivePS's expression. */
float2 HerShade(float4 at, float facing)
{
	const float u = dot(at, gMapU);
	const float v = dot(at, gMapV);
	const float depth = dot(at, gMapD);

	const float edge = saturate(min(min(u, 1.0f - u), min(v, 1.0f - v)) * gShadow.y);
	const float height = saturate(dot(at, gHeight));
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

	return float2(shade, terms);
}

/* The second map's part at `at`: x its lookup, y its terms - its own place,
 * its own edge, the same facing, and no height plane, the distance term per
 * tap takes its place. */
float2 OthersShade(float4 at, float facing)
{
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
			 * caster and with the fade of its caster's depth near the
			 * box's two ends along the light; the clear value 1 never lies
			 * below the limit. */
			const float4 behind = saturate(gOthersBehind.y - gOthersBehind.x * (odepth - tap));
			const float4 fade = saturate(min(tap, 1.0f - tap) * gOthersBehind.z);
			const float row = dot(wx, (tap < limit) ? behind * fade : 0.0f);
			const float wy = j == 0 ? 1.0f - f.y : (j == 3 ? f.y : 1.0f);

			sum += wy * row;
		}

		oshade = sum * (1.0f / 9.0f);
	}

	return float2(oshade, oterms);
}

float4 Receive2PS(RECEIVE_IN input) : COLOR0
{
	/* The alpha test of the range, ReceivePS's own line. */
	float4 texel = tex2D(sPage, input.uv);

	clip(texel.a - gPass.x);

	const float4 at = float4(input.viewPos, 1.0f);

	const float3 n = input.viewNrm * rsqrt(max(dot(input.viewNrm, input.viewNrm), 1e-8f));
	const float facing = saturate(dot(n, gToLight.xyz) * gToLight.w);

	const float2 hers = HerShade(at, facing);
	const float2 theirs = OthersShade(at, facing);

	/* 1 - max(strength * hers, strength * theirs), written as the smaller of
	 * the two factors: her factor is then ReceivePS's own expression, and
	 * where the second map gives nothing the other factor is 1 and the
	 * pixel is ReceivePS's to the bit. */
	const float herFactor = 1.0f - gShadow.x * hers.x * hers.y;
	const float theirFactor = 1.0f - gShadow.x * theirs.x * theirs.y;

	return ReceiveOut(min(herFactor, theirFactor), input.viewPos);
}

/* The mirror's variant, MirrorReceivePS: Receive2PS on the copy behind a
 * mirror's glass alone. A mirror's room group holds the room and, behind the
 * glass, a copy of it reflected in the glass's plane (hd_render.h, HDMirror);
 * the shadows in the copy are the shadows in front of the glass, reflected.
 * hd_level.cpp draws such a group once with this shader, before its ordinary
 * receive draw: the blocks in c96 - c107 are the mirror's (hd_shadow.h,
 * HDShadowMirrorView - the frame's blocks made with the reflected view, so a
 * pixel of the copy looks the maps up at the place it reflects), and the one
 * clip against the glass's plane keeps the pixels behind it. The stencil
 * guard marks every pixel this draw leaves, so the ordinary draw that follows
 * is kept off the copy and darkens the room in front alone. A frame whose
 * second map was not drawn hands in its block silenced
 * (HDShadowReceiveWithoutOthers), so the variant serves both receives.
 *
 * Build. bshader.bat compiles it out of this file with
 *
 *     fxc /nologo /T ps_3_0 /E MirrorReceivePS /O3 /Vn kHDShadowReceiveMirrorPS
 *         /Fh hd_shadow_receive_mirror_ps.inc hd_shadow_receive.hlsl
 *
 * and stamps the .inc with this file's MD5, as it stamps the others.
 *
 * Registers. Everything Receive2PS names, and the glass's plane in c108
 * (hd_shadow.h, HD_SHADOW_RECEIVE_MIRROR, HDShadowMirrorPlane): a view-space
 * point with dot(float4(viewPos, 1), gMirror) >= 0 lies behind the glass. */

/* The glass's plane in view space, positive behind it. */
float4 gMirror : register(c108);

float4 MirrorReceivePS(RECEIVE_IN input) : COLOR0
{
	/* The alpha test of the range, Receive2PS's own line. */
	float4 texel = tex2D(sPage, input.uv);

	clip(texel.a - gPass.x);

	const float4 at = float4(input.viewPos, 1.0f);

	// The copy alone: the room in front of the glass is the ordinary draw's.
	clip(dot(at, gMirror));

	const float3 n = input.viewNrm * rsqrt(max(dot(input.viewNrm, input.viewNrm), 1e-8f));
	const float facing = saturate(dot(n, gToLight.xyz) * gToLight.w);

	const float2 hers = HerShade(at, facing);
	const float2 theirs = OthersShade(at, facing);

	const float herFactor = 1.0f - gShadow.x * hers.x * hers.y;
	const float theirFactor = 1.0f - gShadow.x * theirs.x * theirs.y;

	return ReceiveOut(min(herFactor, theirFactor), input.viewPos);
}

/* The models' variant, ModelReceivePS: the two-map body above on an HD
 * model. hd_model.cpp draws an entry of the frame list again after the
 * models' solid pass with the model's own vertex shader (ModelVS in
 * hd_model.hlsl, unchanged) and the very constants that decide its pixels'
 * depth in the solid draw, the depth test EQUAL, no depth write, and this
 * shader in place of ModelPS; the blend multiplies the picture with what it
 * writes, as for a room.
 *
 * Which maps an entry takes is the caller's (hd_shadow.h,
 * HDShadowReceiverTakes): the block of a map it does not take is silenced
 * through its u row (HDShadowReceiveWithoutHer, HDShadowReceiveWithoutOthers),
 * so that part's edge term is 0, its lookup is never taken and its factor
 * is 1. A mirror's reflection is drawn with its mirror's blocks
 * (HDShadowMirrorView), so it takes the shadows of what it reflects.
 *
 * A pixel shader reads the vertex shader's outputs by semantic, and ModelVS
 * hands them down in a layout of its own: the texture coordinate
 * (TEXCOORD0), the view-space position (TEXCOORD2), the view-space normal,
 * skinned and not renormalised (TEXCOORD3), and the head cut's keep value
 * (TEXCOORD5). The three clips are ModelPS's own, on the registers the
 * solid draw left them in - the alpha test of the range (gPass.x), the head
 * cut (gPass.z) and a mirror's three planes (gClip, c5 - c7) - so a pixel
 * the solid draw discarded is discarded here too.
 *
 * Build. bshader.bat compiles it out of this file with
 *
 *     fxc /nologo /T ps_3_0 /E ModelReceivePS /O3 /Vn kHDShadowReceiveModelPS
 *         /Fh hd_shadow_receive_model_ps.inc hd_shadow_receive.hlsl
 *
 * and stamps the .inc with this file's MD5, as it stamps the other two.
 *
 * Registers. gPass in c4 as the model draw sets it per range (x the alpha
 * test's switch, z the head cut's), gClip in c5 - c7 as HDModelClip sets
 * them, the page in s0, her map in s3 and the second map in s4, and her
 * block and the second map's in c96 - c107: the two-map variant's set, and
 * none of them a register ModelPS reads besides c4 - c7. */

/* A mirror's three clip planes in view space, hd_model.hlsl's gClip. */
float4 gClip[3] : register(c5);

struct MODEL_RECEIVE_IN
{
	float2 uv      : TEXCOORD0;
	float3 viewPos : TEXCOORD2;
	float3 viewNrm : TEXCOORD3;
	float  cut     : TEXCOORD5;
};

float4 ModelReceivePS(MODEL_RECEIVE_IN input) : COLOR0
{
	/* ModelPS's three clips, line for line. */
	float4 texel = tex2D(sPage, input.uv);

	clip(gPass.x * (texel.a - 0.5f));
	clip(lerp(1.0f, input.cut, gPass.z));

	const float4 at = float4(input.viewPos, 1.0f);

	clip(min(min(dot(at, gClip[0]), dot(at, gClip[1])), dot(at, gClip[2])));

	/* The model's normal, with the room shader's guard against a zero one. */
	const float3 n = input.viewNrm * rsqrt(max(dot(input.viewNrm, input.viewNrm), 1e-8f));
	const float facing = saturate(dot(n, gToLight.xyz) * gToLight.w);

	const float2 hers = HerShade(at, facing);
	const float2 theirs = OthersShade(at, facing);

	const float herFactor = 1.0f - gShadow.x * hers.x * hers.y;
	const float theirFactor = 1.0f - gShadow.x * theirs.x * theirs.y;

	return ReceiveOut(min(herFactor, theirFactor), input.viewPos);
}

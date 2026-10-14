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

/* The wet floor (hd_render.cpp, "the wet floor"): the floor a breaking fish
 * tank's water runs over, drawn as a thin layer over it - the floor seen
 * through water: darker, and a mirror at a grazing view. One draw a frame
 * of a grid over the wet squares, a few units over the floor, built by the
 * processor in view space as the cards are and projected by the cards'
 * own vertex shader (hd_room.hlsl, CardVS); this file holds the pixel half
 * alone and declares only what it reads, so no other shader's bytecode
 * moves with it.
 *
 * Build. bshader.bat compiles it with
 *
 *     fxc /nologo /T ps_3_0 /E WetPS /O3 /Vn kHDWetPS /Fh hd_wet_ps.inc
 *         hd_wet.hlsl
 *
 * and stamps the .inc with the MD5 of this file.
 *
 * Per pixel, textbook terms over a flat surface whose normal is the
 * world's up:
 *
 *   the darkening  the floor behind is kept by one less the layer's alpha:
 *                  wet stone loses light to the water in its pores and
 *                  under its film
 *   the weight     Schlick's approximation of Fresnel for water,
 *                  F0 + (1 - F0) (1 - N.V)^5, F0 0.02: nearly nothing seen
 *                  from above, a mirror seen along the floor; times the
 *                  mirror's strength (gWetGain)
 *   the mirror     what the water reflects. The frame itself where the
 *                  reflected ray finds it: the ray is marched through view
 *                  space in steps that grow by a fixed factor, each step's
 *                  point projected and held against the frame's depth
 *                  there; the first point that lies behind what the frame
 *                  drew, by less than twice the step's own length, takes
 *                  the frame's colour there (the copy of the opaque frame the
 *                  sheets read) - a reflection in screen space, the common
 *                  real-time technique for a plane. Where the ray leaves
 *                  the screen or finds nothing, the light round the place
 *                  stands in: the level's light field (hd_gloss_field.h),
 *                  whose colour is the lamps' light there, as the walls it
 *                  lights. Over both, the lamps as a Blinn-Phong highlight
 *                  toward the field's direction, as the polished walls'
 *                  sheen has it (hd_room.hlsl, "The sheen").
 *
 * Premultiplied, ONE / INVSRCALPHA: rgb the mirror, alpha the darkening.
 * The mirror is not bounded by the floor's own colour as the walls' sheen
 * is: water reflects as much off dark stone as off pale. It is held under
 * the walls' cap instead, softly, B (1 - e^(-t / B)) for its largest
 * channel t, so no floor turns white.
 *
 * The vertex (CardVS hands its inputs on):
 *
 *   TEXCOORD0  xyz the point in view space, w the wet, 0 to 1
 *   TEXCOORD1  x the view depth, y the light the fog takes its share of
 *   TEXCOORD2  xyz the light field's texture coordinate, w the share of
 *              the mirror the water's depth there gives (a damp patch
 *              darkens and mirrors less than a pool)
 *
 * Pixel registers, written before the one draw, in the block the sheets'
 * and the cards' draws write again before they read it:
 *
 * c92  gWetScreen  x, y one over the target's width and height, z, w the
 *                  projection's a and b (depth = a - b / z)
 * c93  gWetMode    x 1 while the frame's depth texture is bound on s6, y how
 *                  far under the layer the frame still counts as its floor
 *                  (its height over the floor and a margin), z one over the
 *                  depth past that over which the layer fades out, w the
 *                  darkening
 * c94  gWetLook    x F0, y the walls' share of the field's light, z the
 *                  highlight's gain, w the highlight's exponent
 * c95  gWetMarch   x the cap, y 1 while the frame's copy is bound on s5,
 *                  z the march's first step in units, w its growth a step
 * c96  gWetAxes    three rows: a world direction's view-space form is
 *                  x row 0 + y row 1 + z row 2; the world's up is -row 1
 * c99  gWetProj    the projection's x and y scales and its x and y
 *                  offsets (the third row's x and y): a view-space point
 *                  is on the screen at x' = (x s.x + z s.z) / z
 * c100 gWetGain    x the mirror's strength, a factor on its weight */
float4 gFog : register(c16);
float4 gWetScreen : register(c92);
float4 gWetMode : register(c93);
float4 gWetLook : register(c94);
float4 gWetMarch : register(c95);
float4 gWetAxes[3] : register(c96);
float4 gWetProj : register(c99);
float4 gWetGain : register(c100);

/* The light field: the direction towards the light biased into bytes with
 * the one-sidedness in alpha, and its colour (hd_room.hlsl, "The sheen"). */
sampler3D sFieldDir : register(s1);
sampler3D sFieldRgb : register(s2);
sampler2D sWetCopy : register(s5);
sampler2D sWetDepth : register(s6);

// The steps of the march: 12 at a growth of 1.5 from 32 reach 8,200 units.
#define HD_WET_STEPS 12
/* The brightest a reflected texel stands for, eight times the frame's white
 * (a free choice: a texel of 0.89 and over). */
#define HD_WET_BRIGHT 8.0f

struct WET_IN
{
	float4 view   : TEXCOORD0;
	float4 viewZ  : TEXCOORD1;
	float4 field  : TEXCOORD2;
};

// Where a view-space point falls on the screen, 0..1 down from the top.
float2 WetScreen(float3 p)
{
	const float2 ndc = (p.xy * gWetProj.xy + p.z * gWetProj.zw) / p.z;

	return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

// The frame's view depth at a place on the screen.
float WetDepth(float2 uv)
{
	const float d = tex2Dlod(sWetDepth, float4(uv, 0.0f, 0.0f)).r;

	return gWetScreen.w / max(gWetScreen.z - d, 1.0e-6f);
}

float4 WetPS(WET_IN input, float2 vpos : VPOS) : COLOR0
{
	const float3 P = input.view.xyz;
	const float dist = length(P);
	const float3 V = -P / max(dist, 1.0f);
	const float3 N = -gWetAxes[1].xyz;
	const float nv = saturate(dot(N, V));
	float wet = input.view.w;

	/* The floor under the layer. The layer lies a few units over the floor
	 * the classic level gives; where the frame holds something further
	 * down under it than gWetMode.y - a drain, a gap the art leaves, the
	 * bottom of a step the squares do not know - the water is not there.
	 * The frame's depth behind the pixel, less the layer's, is a stretch
	 * of the eye's ray in view depth; over the ray's share of view depth
	 * it is the stretch itself, and times N.V how far the frame lies under
	 * the layer. */
	if (gWetMode.x > 0.5f)
	{
		const float behind = WetDepth((vpos + 0.5f) * gWetScreen.xy);
		const float under = (behind - input.viewZ.x) * dist /
			max(input.viewZ.x, 1.0f) * nv;

		wet *= saturate(1.0f - (under - gWetMode.y) * gWetMode.z);
	}

	// The light round the place, and where it comes from, in view space.
	const float4 toward = tex3D(sFieldDir, input.field.xyz);
	const float3 colour = tex3D(sFieldRgb, input.field.xyz).rgb;
	float3 Lw = toward.xyz * 2.0f - 1.0f;
	const float len = length(Lw);
	const float3 L = (Lw.x * gWetAxes[0].xyz + Lw.y * gWetAxes[1].xyz +
		Lw.z * gWetAxes[2].xyz) / max(len, 1.0e-3f);

	const float3 LV = L + V;
	const float3 H = LV * rsqrt(max(dot(LV, LV), 1.0e-8f));
	const float lobe = pow(saturate(dot(N, H)), gWetLook.w) *
		saturate(dot(N, L));
	const float g = 1.0f - nv;
	const float g2 = g * g;
	const float F = gWetLook.x + (1.0f - gWetLook.x) * (g2 * g2 * g);

	// What the reflected ray finds: the frame, or the walls' light.
	float3 seen = colour * gWetLook.y;

	if (gWetMarch.y > 0.5f && gWetMode.x > 0.5f)
	{
		const float3 R = reflect(-V, N);
		float step = gWetMarch.z;
		float s = 0.0f;

		[loop]
		for (int i = 0; i < HD_WET_STEPS; i++)
		{
			s += step;

			const float3 q = P + R * s;

			if (q.z < 1.0f)
				break;

			const float2 uv = WetScreen(q);

			if (any(uv < 0.0f) || any(uv > 1.0f))
				break;

			const float frame = WetDepth(uv);

			if (q.z > frame && q.z - frame < step * 2.0f)
			{
				/* The frame holds its light clipped at one: a texel near
				 * white stands for a lamp or a lit screen far brighter
				 * than the frame can hold, and a mirror of a few hundredths
				 * shows such a source and little else. The clip is undone
				 * as Reinhard's curve x / (1 + x) is, x / (1 - x), held at
				 * HD_WET_BRIGHT. Fades out toward the screen's edge, over
				 * its last tenth. */
				const float2 edge = saturate(min(uv, 1.0f - uv) * 10.0f);
				const float3 held = min(tex2Dlod(sWetCopy,
					float4(uv, 0.0f, 0.0f)).rgb, HD_WET_BRIGHT /
					(1.0f + HD_WET_BRIGHT));

				seen = lerp(seen, held / (1.0f - held), edge.x * edge.y);
				break;
			}

			step *= gWetMarch.w;
		}
	}

	float3 mirror = (seen * input.field.w + colour *
		(toward.a * saturate(len) * lobe * gWetLook.z)) * F * gWetGain.x;

	// The cap, softly, keeping the hue.
	const float t = max(mirror.r, max(mirror.g, mirror.b)) / gWetMarch.x;

	mirror *= (1.0f - exp(-t)) / max(t, 1.0e-4f);

	/* The distance fog as the cards take it (hd_room.hlsl, CardPS): a share
	 * of the light the floor carries is left to the mirror; the darkening
	 * multiplies a floor the fog has darkened already. */
	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);

	mirror *= saturate(1.0f - fog / max(input.viewZ.y, 0.05f));

	return float4(mirror * wet, wet * gWetMode.w);
}

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
 * through water: darker, and a mirror at a grazing view. Two draws a frame
 * of a grid over the wet squares, a few units over the floor, built by the
 * processor in view space as the cards are and projected by the cards'
 * own vertex shader (hd_room.hlsl, CardVS); this file holds the pixel half
 * alone and declares only what it reads, so no other shader's bytecode
 * moves with it.
 *
 * Build. bshader.bat compiles it twice,
 *
 *     fxc /nologo /T ps_3_0 /E WetPS /O3 /Vn kHDWetPS /Fh hd_wet_ps.inc
 *         hd_wet.hlsl
 *     fxc /nologo /T ps_3_0 /E WetPS /O3 /D HD_WET_DEBUG=1
 *         /Vn kHDWetDebugPS /Fh hd_wet_debug_ps.inc hd_wet.hlsl
 *
 * and stamps both .inc files with the MD5 of this file. The second is
 * RoomProbe's march debugger (-tankmarch): the mirror's march held against
 * a fine one, as colours; the game never draws it.
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
 *                  there. Where a step passes from in front of what the
 *                  frame drew to behind it, the crossing is halved down
 *                  HD_WET_REFINE times; the frame's colour there is the
 *                  mirror's when the frame's depth at that place fits the
 *                  ray's within the depth tolerance - a surface the ray
 *                  meets. Past that tolerance the ray went behind a thing
 *                  that stands nearer (its edge), and the march goes on
 *                  until the ray comes out in front again. A reflection in
 *                  screen space, the common real-time technique for a
 *                  plane. Where the ray leaves the screen or finds nothing,
 *                  the light round the place stands in: the level's light
 *                  field (hd_gloss_field.h), whose colour is the lamps'
 *                  light there, as the walls it lights; a hit near the
 *                  tolerance or near the screen's edge fades into it. Over
 *                  both, the lamps as a Blinn-Phong highlight toward the
 *                  field's direction, as the polished walls' sheen has it
 *                  (hd_room.hlsl, "The sheen").
 *
 * The two draws. The first, over the opaque frame, keeps the floor back
 * by the darkening and adds nothing. The second, after the rooms' glass
 * and glows and before the cards, adds the mirror out of a copy of the
 * frame as it stands, so the glows and what the glass shows are in it and
 * the dome and the spray are drawn over it.
 * Premultiplied, ONE / INVSRCALPHA, both: rgb 0 and alpha the darkening,
 * then rgb the mirror and alpha 0. The mirror is not bounded by the
 * floor's own colour as the walls' sheen is: water reflects as much off
 * dark stone as off pale. It is held under the walls' cap instead,
 * softly, B (1 - e^(-t / B)) for its largest channel t, so no floor turns
 * white.
 *
 * The vertex (CardVS hands its inputs on):
 *
 *   TEXCOORD0  xyz the point in view space, w the wet, 0 to 1
 *   TEXCOORD1  x the view depth, y the light the fog takes its share of
 *   TEXCOORD2  xyz the light field's texture coordinate, w the share of
 *              the mirror the water's depth there gives (a damp patch
 *              darkens and mirrors less than a pool), 0 where the art
 *              carries its own copy of the room under the floor: no
 *              march there and no light field in its place, the lamps'
 *              highlight alone (hd_render.cpp, "the art's own mirror")
 *
 * Pixel registers, written before each draw, in the block the sheets'
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
 * c100 gWetGain    x the mirror's strength, a factor on its weight; y 0 for
 *                  the first draw, 1 for the second; z the depth tolerance
 *                  in units, w its share of the view depth on top
 * c101 gWetDebug   (the debugger only) x the map, 5 to 8 (WetPS, "the
 *                  march debugger's maps")
 * c102 gWetPlane   the plane the models were mirrored in, in view space:
 *                  xyz the world's up, w its height along it
 * c103 gWetModels  x 1 while the models' mirror is bound on s0 and s3, y how
 *                  far a pixel's layer may lie from that plane and still
 *                  take it, z 1 while the room behind the models is bound
 *                  on s4
 *
 * The models in the mirror (hd_render.cpp, HDRenderWaterWetModels): Lara and
 * the objects the frame drew are drawn once more through the floor's
 * mirror into a target of their own, s0's colour, with their own depth on
 * s3, and once as the frame drew them into s0's alpha alone (0 under a
 * model, 1 elsewhere, whatever alpha the model's page holds). Both are
 * flipped left to right - the flip keeps the models' winding through the
 * mirror - so they are read at 1 - x. The march goes past a place of the
 * frame a model covers, as past a hole in the frame: a model's pixels in
 * the frame are not the room the ray would meet behind it, and a march that
 * took them laid the model's colour on the floor beside its feet. Where a
 * mirrored model lies under a pixel, under the floor and nearer than what
 * the march found, it is the mirror's. */
float4 gFog : register(c16);
float4 gWetScreen : register(c92);
float4 gWetMode : register(c93);
float4 gWetLook : register(c94);
float4 gWetMarch : register(c95);
float4 gWetAxes[3] : register(c96);
float4 gWetProj : register(c99);
float4 gWetGain : register(c100);

/* The light field: the direction towards the light biased into bytes with
 * the one-sidedness in alpha, and its colour (hd_room.hlsl, "The sheen").
 * The copy on s5 is the later one, for the second draw. */
sampler3D sFieldDir : register(s1);
sampler3D sFieldRgb : register(s2);
sampler2D sWetCopy : register(s5);
sampler2D sWetDepth : register(s6);
sampler2D sWetModel : register(s0);
sampler2D sWetModelDepth : register(s3);
// The frame's colour before the models were drawn: the room behind them.
sampler2D sWetRoom : register(s4);
float4 gWetPlane : register(c102);
float4 gWetModels : register(c103);

/* The mirror softened (hd_render.cpp, "the soft mirror"). The march meets a
 * thin bright strip in some rows and steps past it in the next where its
 * step spans more pixels than the strip: the strip's reflection ripples.
 * So the mirror is drawn into a layer of its own, its alpha the blur's
 * radius - the screen length of the march's last step times gWetModels.w,
 * at most HD_WET_RADIUS pixels, coded (1 + 16 r) / 255, 0 where no wet
 * pixel lies - and blurred there across and down (gWetGain.y 2), each
 * pixel over its own radius, a Gaussian of sigma r / 2 over the covered
 * pixels only; the second pass adds it to the frame. Nearer the eye than
 * 1 / gWetBlur.w the radius shrinks with the distance: a near ray's steps
 * span many pixels, but the strip there spans more, and the mirror stays
 * sharp. gWetBlur in the blur: the pass's direction, one pixel in uv, and
 * in z 1 for the pass into the frame, which reads the radius and the cover
 * out of the layer (s4). */
float4 gWetBlur : register(c104);
#define HD_WET_RADIUS 15.0f
#define HD_WET_TAPS 4

/* The steps of the march: 22 at a growth of 1.2 from 32 reach about 8,700
 * units, eight squares, and the crossing halved 8 times lies within a
 * 256th of its step. */
#define HD_WET_STEPS 22
#define HD_WET_REFINE 8
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

// Whether a place lies on the screen.
bool WetOn(float2 uv)
{
	return all(uv >= 0.0f) && all(uv <= 1.0f);
}

// Whether the frame drew a model at a place on the screen (alpha 0 there).
bool WetModelAt(float2 uv)
{
	return gWetModels.x > 0.5f && tex2Dlod(sWetModel,
		float4(1.0f - uv.x, uv.y, 0.0f, 0.0f)).a < 0.5f;
}

/* The march: from P along R, the place the ray meets the frame, and how
 * sure: 1 for a crossing whose depth fits exactly, down to 0 at the
 * tolerance; 0 for none. `why` 0 a hit, 1 off the screen, 2 behind the
 * eye, 3 out of steps, 4 the room behind a model; `steps` the steps taken,
 * `len` the ray's length to the hit. A model's place counts as in front of
 * the frame (WetModelAt). A ray that went behind a model's place and met
 * nothing after it met what the model hides in the frame - the room behind
 * it, which the copy taken before the models holds (sWetRoom): its first
 * such place is the answer then, sure as a hit, where that copy is bound
 * (gWetModels.z); without it the ray finds nothing, as before. `spread`:
 * the last step's stretch on the screen, in uv (the soft mirror's radius). */
float WetMarch(float3 P, float3 R, out float2 at, out float why,
	out float steps, out float len, out float2 spread)
{
	float step = gWetMarch.z;
	float s = 0.0f;
	bool front = true;
	float2 was = WetScreen(P);
	// The first place the ray went behind a model, and its length there.
	float2 hidden = float2(0.0f, 0.0f);
	float hiddenLen = -1.0f;

	at = float2(0.0f, 0.0f);
	why = 3.0f;
	steps = HD_WET_STEPS;
	len = 0.0f;
	spread = float2(0.0f, 0.0f);

	[loop]
	for (int i = 0; i < HD_WET_STEPS; i++)
	{
		const float last = s;

		s += step;
		step *= gWetMarch.w;

		const float3 q = P + R * s;

		if (q.z < 1.0f)
		{
			why = 2.0f;
			steps = i + 1;
			break;
		}

		const float2 uv = WetScreen(q);

		spread = uv - was;
		was = uv;

		if (!WetOn(uv))
		{
			why = 1.0f;
			steps = i + 1;
			break;
		}

		const bool model = WetModelAt(uv);

		if (model && hiddenLen < 0.0f && q.z > WetDepth(uv))
		{
			hidden = uv;
			hiddenLen = s;
		}

		const bool behind = q.z > WetDepth(uv) && !model;

		if (behind && front)
		{
			// The crossing, halved down between the last step and this.
			float a = last, b = s;

			[loop]
			for (int k = 0; k < HD_WET_REFINE; k++)
			{
				const float m = (a + b) * 0.5f;
				const float3 qm = P + R * m;
				const float2 um = WetScreen(qm);

				if (qm.z > WetDepth(um) && !WetModelAt(um))
					b = m;
				else
					a = m;
			}

			const float3 qb = P + R * b;
			const float2 ub = WetScreen(qb);
			const float gap = qb.z - WetDepth(ub);
			const float tol = gWetGain.z + gWetGain.w * qb.z;

			if (gap < tol)
			{
				at = ub;
				why = 0.0f;
				steps = i + 1;
				len = b;
				return saturate(2.0f - 2.0f * gap / tol);
			}
		}

		front = !behind;
	}

	if (gWetModels.z > 0.5f && hiddenLen >= 0.0f)
	{
		at = hidden;
		why = 4.0f;
		len = hiddenLen;
		return 1.0f;
	}

	return 0.0f;
}

// The frame's colour a march found: the room behind a model for why 4.
float3 WetFound(float2 at, float why)
{
	return why > 3.5f ? tex2Dlod(sWetRoom, float4(at, 0.0f, 0.0f)).rgb :
		tex2Dlod(sWetCopy, float4(at, 0.0f, 0.0f)).rgb;
}

#if HD_WET_DEBUG
float4 gWetDebug : register(c101);
// The opaque frame's copy, the one the sheets read.
sampler2D sWetOpaque : register(s7);

/* The fine march, the measure: 8-unit steps out to 10,240 units, every
 * crossing halved ten times, a hit where the frame's depth fits the ray's
 * within 16 units and a hundredth of the depth; past a model's place as
 * the march goes. */
float WetMarchFine(float3 P, float3 R, out float2 at)
{
	float s = 0.0f;
	bool front = true;

	at = float2(0.0f, 0.0f);

	[loop]
	for (int j = 0; j < 5; j++)
	{
		[loop]
		for (int i = 0; i < 256; i++)
		{
			const float last = s;

			s += 8.0f;

			const float3 q = P + R * s;

			if (q.z < 1.0f)
				return 0.0f;

			const float2 uv = WetScreen(q);

			if (!WetOn(uv))
				return 0.0f;

			const bool behind = q.z > WetDepth(uv) && !WetModelAt(uv);

			if (behind && front)
			{
				float a = last, b = s;

				[loop]
				for (int k = 0; k < 10; k++)
				{
					const float m = (a + b) * 0.5f;
					const float3 qm = P + R * m;

					if (qm.z > WetDepth(WetScreen(qm)))
						b = m;
					else
						a = m;
				}

				const float3 qb = P + R * b;
				const float2 ub = WetScreen(qb);

				if (qb.z - WetDepth(ub) < 16.0f + 0.01f * qb.z)
				{
					at = ub;
					return 1.0f;
				}
			}

			front = !behind;
		}
	}

	return 0.0f;
}
#endif

#if !HD_WET_DEBUG
// The soft mirror's blur, one pass (see gWetBlur): the layer on s0.
float4 WetBlur(float2 vpos)
{
	const float2 uv = (vpos + 0.5f) * gWetScreen.xy;
	const float4 c = tex2Dlod(sWetModel, float4(uv, 0.0f, 0.0f));
	// The radius and the cover from the layer itself (s4 in the second).
	const float code = gWetBlur.z > 0.5f ?
		tex2Dlod(sWetRoom, float4(uv, 0.0f, 0.0f)).a : c.a;
	const float r = max(code * 255.0f - 1.0f, 0.0f) / 16.0f;
	float3 sum = c.rgb;
	float total = 1.0f;

	[branch]
	if (r > 0.25f)
	{
		[unroll]
		for (int i = 1; i <= HD_WET_TAPS; i++)
		{
			// A tap i / HD_WET_TAPS of the radius out, sigma half of it.
			const float w = exp(-2.0f * i * i /
				(float)(HD_WET_TAPS * HD_WET_TAPS));
			const float2 o = gWetBlur.xy * (r * i / HD_WET_TAPS);
			const float4 a = tex2Dlod(sWetModel, float4(uv + o, 0.0f, 0.0f));
			const float4 b = tex2Dlod(sWetModel, float4(uv - o, 0.0f, 0.0f));
			const float ca = gWetBlur.z > 0.5f ?
				tex2Dlod(sWetRoom, float4(uv + o, 0.0f, 0.0f)).a : a.a;
			const float cb = gWetBlur.z > 0.5f ?
				tex2Dlod(sWetRoom, float4(uv - o, 0.0f, 0.0f)).a : b.a;
			const float wa = ca > 0.5f / 255.0f ? w : 0.0f;
			const float wb = cb > 0.5f / 255.0f ? w : 0.0f;

			sum += a.rgb * wa + b.rgb * wb;
			total += wa + wb;
		}
	}

	return float4(sum / total, gWetBlur.z > 0.5f ? 0.0f : code);
}
#endif

float4 WetPS(WET_IN input, float2 vpos : VPOS) : COLOR0
{
#if !HD_WET_DEBUG
	[branch]
	if (gWetGain.y > 1.5f)
		return WetBlur(vpos);
#endif

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

#if HD_WET_DEBUG
	/* The march debugger's maps (gWetDebug.x), wet pixels only:
	 *   5  green this march's distance in pixels from the fine march's hit
	 *      (255: a hit on one side only), red 0; blue flags over 255: 1 the
	 *      fine march hits, 4 this one leaves the screen or passes the
	 *      eye, 16 its hit lies on the fine hit's surface (depths within 5
	 *      per cent), 32 its hit shows what the later copy adds over the
	 *      opaque one (s7), 64 something lies over this pixel since the
	 *      opaque copy
	 *   6, 7  what the fine march and this one show, unweighted, black
	 *      where they find nothing; 7 with the mirrored models over it
	 *   8  red where a mirrored model lies under the pixel, green where
	 *      the frame drew a model at the pixel (the models' target) */
	{
		const float3 R = reflect(-V, N);
		const float2 here = (vpos + 0.5f) * gWetScreen.xy;
		const float map = gWetDebug.x;

		if (wet <= 0.0f)
			discard;

		float2 at, ref, spread;
		float why, steps, len;
		const float found = WetMarch(P, R, at, why, steps, len, spread);
		const float fine = WetMarchFine(P, R, ref);

		if (map > 7.5f)
		{
			const float2 flip = float2(1.0f - here.x, here.y);
			const float md = tex2Dlod(sWetModelDepth,
				float4(flip, 0.0f, 0.0f)).r;

			return float4(md < 1.0f && gWetModels.x > 0.5f ? 1.0f : 0.0f,
				WetModelAt(here) ? 1.0f : 0.0f, 0.0f, 1.0f);
		}

		if (map > 5.5f)
		{
			const float2 uv = map < 6.5f ? ref : at;
			const float hit = map < 6.5f ? fine : found;
			const float2 flip = float2(1.0f - here.x, here.y);
			const float md = tex2Dlod(sWetModelDepth,
				float4(flip, 0.0f, 0.0f)).r;
			const float mz = gWetScreen.w / max(gWetScreen.z - md, 1.0e-6f);
			const float hz = hit > 0.0f ? P.z * (1.0f + len / max(dist, 1.0f)) :
				1.0e30f;

			// This march's, the mirrored models over it as WetPS takes them.
			if (map > 6.5f && gWetModels.x > 0.5f && md < 1.0f &&
				mz > input.viewZ.x && mz < hz &&
				abs(dot(P, gWetPlane.xyz) - gWetPlane.w) < gWetModels.y)
				return float4(tex2Dlod(sWetModel,
					float4(flip, 0.0f, 0.0f)).rgb, 1.0f);

			return float4(hit > 0.0f ? (map < 6.5f ? tex2Dlod(sWetCopy,
				float4(uv, 0.0f, 0.0f)).rgb : WetFound(uv, why)) :
				float3(0.0f, 0.0f, 0.0f), 1.0f);
		}

		const float2 px = 1.0f / gWetScreen.xy;
		const float offNew = found > 0.0f && fine > 0.0f ?
			length((at - ref) * px) : found > 0.0f || fine > 0.0f ?
			255.0f : 0.0f;
		const float dr = WetDepth(ref);
		const float3 d = abs(tex2Dlod(sWetCopy, float4(at, 0, 0)).rgb -
			tex2Dlod(sWetOpaque, float4(at, 0, 0)).rgb);
		const float3 e = abs(tex2Dlod(sWetCopy, float4(here, 0, 0)).rgb -
			tex2Dlod(sWetOpaque, float4(here, 0, 0)).rgb *
			(1.0f - wet * gWetMode.w));
		const float flags = (fine > 0.0f ? 1.0f : 0.0f) +
			(found > 0.0f && max(d.r, max(d.g, d.b)) > 4.0f / 255.0f ?
			32.0f : 0.0f) +
			(max(e.r, max(e.g, e.b)) > 4.0f / 255.0f ? 64.0f : 0.0f) +
			(why > 0.5f && why < 2.5f ? 4.0f : 0.0f) +
			(abs(WetDepth(at) - dr) < 0.05f * dr ? 16.0f : 0.0f);

		return float4(0.0f, min(offNew, 255.0f) / 255.0f, flags / 255.0f,
			1.0f);
	}
#endif

	// The first draw: the darkening alone.
	if (gWetGain.y < 0.5f)
		return float4(0.0f, 0.0f, 0.0f, wet * gWetMode.w);

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
	// The view depth the march's hit has seen through the mirror.
	float hitZ = 1.0e30f;
	// The soft mirror's radius in pixels (none without a march).
	float radius = 0.0f;

	if (gWetMarch.y > 0.5f && gWetMode.x > 0.5f && input.field.w > 0.0f)
	{
		float2 at, spread;
		float why, steps, len;
		const float sure = WetMarch(P, reflect(-V, N), at, why, steps, len,
			spread);

		radius = min(length(spread / gWetScreen.xy) * gWetModels.w *
			saturate(dist * gWetBlur.w), HD_WET_RADIUS);

		if (sure > 0.0f)
		{
			/* The frame holds its light clipped at one: a texel near
			 * white stands for a lamp or a lit screen far brighter
			 * than the frame can hold, and a mirror of a few hundredths
			 * shows such a source and little else. The clip is undone
			 * as Reinhard's curve x / (1 + x) is, x / (1 - x), held at
			 * HD_WET_BRIGHT. Fades out toward the screen's edge, over
			 * its last tenth. */
			const float2 edge = saturate(min(at, 1.0f - at) * 10.0f);
			const float3 held = min(WetFound(at, why), HD_WET_BRIGHT /
				(1.0f + HD_WET_BRIGHT));

			seen = lerp(seen, held / (1.0f - held), sure * edge.x * edge.y);
			hitZ = P.z * (1.0f + len / max(dist, 1.0f));
		}
	}

	/* A mirrored model under the pixel: on the plane the models were
	 * mirrored in, under the floor (its depth past the layer's) and nearer
	 * than the march's hit; its clip undone as the frame's is. */
	if (gWetModels.x > 0.5f && input.field.w > 0.0f &&
		abs(dot(P, gWetPlane.xyz) - gWetPlane.w) < gWetModels.y)
	{
		const float2 flip = float2(1.0f - (vpos.x + 0.5f) * gWetScreen.x,
			(vpos.y + 0.5f) * gWetScreen.y);
		const float d = tex2Dlod(sWetModelDepth, float4(flip, 0.0f, 0.0f)).r;
		const float z = gWetScreen.w / max(gWetScreen.z - d, 1.0e-6f);

		if (d < 1.0f && z > input.viewZ.x && z < hitZ)
		{
			const float3 held = min(tex2Dlod(sWetModel,
				float4(flip, 0.0f, 0.0f)).rgb, HD_WET_BRIGHT /
				(1.0f + HD_WET_BRIGHT));

			seen = held / (1.0f - held);
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

	/* Into the soft mirror's layer: the radius coded in alpha; 0 into the
	 * frame, which the mirror is added to. */
	return float4(mirror * wet, gWetModels.w > 0.0f ?
		(1.0f + floor(radius * 16.0f + 0.5f)) / 255.0f : 0.0f);
}

/* The marble floors (hd_render.cpp, "the marble floors"): a planar mirror
 * laid over a polished floor's own faces, drawn with CardVS as the wet
 * floor is and on its registers. The room art, the statics and the models
 * were drawn once more through the floor's plane into a target of their
 * own (s5) and its depth (s3), flipped left to right; a pixel takes what
 * that target holds at its own place turned back, where the depth holds
 * something, and the walls' light where it holds nothing - the wet floor's
 * composition otherwise, at a dry floor's numbers (gWetLook: F0 0.04, no
 * darkening): Schlick's term, the light field's stand-in and the lamps'
 * highlight, the soft cap and the fog. gMarble.x 1 draws the wet floor's
 * points instead, their wetness in alpha, for the stencil they leave the
 * marble out of; gMarble.y and w the flip's place in the target: a
 * pixel's x there is (y - vpos.x) * w. */
float4 gMarble : register(c105);

float4 MarblePS(WET_IN input, float2 vpos : VPOS) : COLOR0
{
	if (gMarble.x > 0.5f)
		return float4(0.0f, 0.0f, 0.0f, input.view.w);

	const float3 P = input.view.xyz;
	const float dist = length(P);
	const float3 V = -P / max(dist, 1.0f);
	const float3 N = -gWetAxes[1].xyz;
	const float nv = saturate(dot(N, V));
	const float4 toward = tex3D(sFieldDir, input.field.xyz);
	const float3 colour = tex3D(sFieldRgb, input.field.xyz).rgb;
	const float3 Lw = toward.xyz * 2.0f - 1.0f;
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
	float3 seen = colour * gWetLook.y;
	const float2 at = float2((gMarble.y - vpos.x) * gMarble.w,
		(vpos.y + 0.5f) * gWetScreen.y);

	if (tex2Dlod(sWetModelDepth, float4(at, 0.0f, 0.0f)).r < 1.0f)
	{
		const float3 held = min(tex2Dlod(sWetCopy, float4(at, 0.0f,
			0.0f)).rgb, HD_WET_BRIGHT / (1.0f + HD_WET_BRIGHT));

		seen = held / (1.0f - held);
	}

	float3 mirror = (seen + colour * (toward.a * saturate(len) * lobe *
		gWetLook.z)) * F * gWetGain.x;
	const float t = max(mirror.r, max(mirror.g, mirror.b)) / gWetMarch.x;

	mirror *= (1.0f - exp(-t)) / max(t, 1.0e-4f);

	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);

	mirror *= saturate(1.0f - fog / max(input.viewZ.y, 0.05f));
	return float4(mirror, 0.0f);
}

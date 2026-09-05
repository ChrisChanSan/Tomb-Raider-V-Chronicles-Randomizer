/*
 * Modified 2026 by ChrisChanSan.
 *
 * The skinning rule below - bind-pose vertices with THREE bone indices into
 * the classic mesh tree, the bind offset subtracted before the node matrix is
 * applied, the alpha test in the alpha pass ONLY and the additive pass
 * weighted by the texture's own alpha - is carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, host\render\game_renderer.cpp, the GLSL
 * shaders k_trm_vert / k_trm_frag). Translated to HLSL for Direct3D 9 and
 * with the bind offset folded into the matrix on the processor instead of
 * subtracted in the shader; not a line of the original is copied. See
 * THIRD-PARTY.md.
 *
 * The lighting below the line "THE PARITY BLOCK" is ours: TROne lights an
 * item with a flat ambient blown up into a six-face cube and up to four point
 * lights of its own invention, while this engine has the classic path's own
 * per-object light list, its distance fog and its fog bulbs - and a torch has
 * to reach an HD guard exactly as far as it reaches the classic wall behind
 * him. So the node colour comes from the engine's own CalculateObjectLighting
 * and the fog and bulb terms are the very ones hd_room.hlsl runs.
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

/* THE HD OBJECT SHADER (#123, stage S3).
 *
 * HOW IT IS BUILT. Like hd_room.hlsl: no fxc step in tomb5.vcxproj, the
 * bytecode checked in beside this file as two C headers, rebuilt with
 *
 *     Preparation\Tools\Diagnose\bshader.bat
 *
 * which for this file is
 *
 *     fxc /nologo /T vs_3_0 /E ModelVS /O3 /Vn kHDModelVS /Fh hd_model_vs.inc hd_model.hlsl
 *     fxc /nologo /T ps_3_0 /E ModelPS /O3 /Vn kHDModelPS /Fh hd_model_ps.inc hd_model.hlsl
 *
 * THE VERTEX IS THE ONE ON DISK, ALL 24 BYTES OF IT. A .TRM vertex needs no
 * repacking at all - only a declaration that reads its bytes with the right
 * meaning:
 *
 *   +0   FLOAT3   x, y, z             POSITION0      bind pose, classic units
 *   +12  UBYTE4   nx, ny, nz, layer   NORMAL0        normal biased by 127;
 *                                                    the layer is not read
 *                                                    here (one page per draw)
 *   +16  UBYTE4   b0, b1, b2, u       BLENDINDICES0  nodes of the classic
 *                                                    mesh tree; u over 255
 *   +20  UBYTE4N  w0, w1, w2, v       BLENDWEIGHT0   weights already divided
 *                                                    by 255; v likewise
 *
 * The two halves of the texture coordinate arrive through two different
 * elements because that is where the remaster put them: u is the fourth byte
 * of the bone triple and v the fourth byte of the weight triple. UBYTE4 is
 * UNnormalised and UBYTE4N is normalised, so u comes in as 0..255 and v as
 * 0..1 - hence the single division below and none for v.
 *
 * THE THREE WEIGHTS ARE NOT OPTIONAL. Measured over the 566 models of game 5:
 * 174,896 of 1,260,496 vertices (13.9 %) carry more than one weight, and the
 * three always sum to exactly 255. Two weights would tear every elbow.
 *
 * WHERE THE BIND POSE WENT. TROne subtracts a per-node bind offset in the
 * shader (uBind[32], one more constant array and one more subtraction per
 * bone per vertex). The offset is a translation and the node matrix is
 * applied right after it, so the two fold into one matrix on the processor:
 *
 *     (p - bind) * M  ==  p * M'   with   M' = M except t' = t - bind * R
 *
 * That is 32 registers and three vector subtractions per vertex saved, and it
 * is done in hd_model.cpp, HDModelDraw. The shader below therefore has no
 * bind pose in it at all.
 */

/* ------------------------------------------------- constants, vertex side --
 *
 * c0 - c3     gProj               per frame  (view -> clip)
 * c4          gPass               per pass   (the same register hd_room uses)
 * c8 - c103   gJoints[96]         per model  (32 nodes, THREE registers each)
 * c104 - c135 gNodeLight[32]      per model  (one colour per node, 0..255)
 *
 * 136 of the 256 vs_3_0 registers. The palette is 32 nodes because the
 * highest bone index a weighted vertex names over all 566 models of game 5 is
 * 28 (GREEN_TEETH); the only two files past that, PIERRE_HEAD and
 * LARSON_HEAD at 40, are facial-rig heads whose numbers belong to the rig and
 * which are drawn rigidly on ONE node. A classic TR5 moveable has at most 32
 * meshes anyway.
 */

/* view -> clip. NOT world -> clip: a node matrix already carries the object
 * into view space, exactly as the classic path's matrix stack does. */
float4x4 gProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = spare,
 * w = specular (overbright) on/off - the same four the room shader carries,
 * in the same register, so that one table describes both.
 *
 * READ BY THE PIXEL SHADER ONLY, and that is not a detail. The room shader
 * reads gPass in both halves; this one does not need it in the vertex half,
 * so fxc drops it there and puts TWO OF ITS OWN def constants in c4 and c5 of
 * the vertex shader (checked in the /Fc listing). Writing gPass to the vertex
 * constant file would overwrite a literal - silently, and it would look like
 * a maths error three files away. hd_model.cpp therefore sets it through
 * SetPixelShaderConstantF alone. */
float4 gPass : register(c4);

/* THE JOINT PALETTE, three registers a node, as an array of float4 and not as
 * an array of float4x3. The three registers of node b are the three COLUMNS
 * of its matrix, so a skinned position is three dot products written out
 * below - which is what the hardware does anyway, and it leaves no room for a
 * disagreement about how fxc packs an array of matrices. hd_model.cpp writes
 * exactly these columns.
 *
 * Each matrix is the node's classic transform - model space to VIEW space,
 * bind offset already folded in. */
float4 gJoints[96] : register(c8);

/* ONE COLOUR PER NODE, in the classic path's 0..255 space: what
 * CalculateObjectLighting / aTransformLightClipMesh computes for this object,
 * evaluated once per node instead of once per vertex. Weighted by the same
 * three weights as the position, so it crosses a joint smoothly. */
float4 gNodeLight[32] : register(c104);

/* --------------------------------------------------- THE PARITY BLOCK ------
 *
 * constants, pixel side
 *
 * c4          gPass               per pass  (the same one)
 * c16         gFog                per frame
 * c17         gCounts             per object
 * c20 - c35   gLights[16]         per object (8 lights, two registers each)
 * c40 - c51   gBulbs[12]          per frame  (6 bulbs, two registers each)
 *
 * The same numbers hd_room.hlsl uses, and for the same things - one table of
 * "register -> meaning" for the whole HD path. The ONE difference is the
 * space: a room vertex is room local and its lights come room local with it,
 * while a skinned vertex lands in VIEW space, so gLights arrives in view
 * space here. hd_model.cpp says so where it writes them.
 */

/* x = tomb5.distance_fog * 1024, y = 255 / that. */
float4 gFog : register(c16);

/* x = dynamic lights, y = fog bulbs. */
float4 gCounts : register(c17);

/* Unrolled against a compile time maximum for the reason hd_room.hlsl gives
 * at length: ps_3_0 has no relative addressing of constant registers, and a
 * run time loop makes fxc emulate every access with a chain of comparisons. */
#define HD_MAX_LIGHTS 8
#define HD_MAX_BULBS  6

/* Two registers per light: (x, y, z, falloff) in VIEW space, then
 * (r, g, b, 1 / falloff). */
float4 gLights[HD_MAX_LIGHTS * 2] : register(c20);

/* Two registers per bulb: (x, y, z, radius) in VIEW space, then
 * (r, g, b, density). */
float4 gBulbs[HD_MAX_BULBS * 2] : register(c40);

sampler2D sPage : register(s0);

/* ---------------------------------------------------------------- shared -- */

struct VS_OUT
{
	float4 pos     : POSITION;
	float2 uv      : TEXCOORD0;
	/* As a TEXCOORD and not as a COLOR: a colour register is clamped to
	 * 0..1 on shader model 3 hardware and this one carries 0..255. */
	float3 light   : TEXCOORD1;
	float3 viewPos : TEXCOORD2;	// the classic "stash"
	float3 viewNrm : TEXCOORD3;	// skinned normal, NOT renormalised
};

/* --------------------------------------------------------------- vertex -- */

/* p * M for the node whose three columns start at register 8 + node * 3.
 * With w = 0 in p this is the 3x3 part alone, which is what a normal wants. */
float3 Skin(float4 p, int node)
{
	int at = node * 3;

	return float3(dot(p, gJoints[at + 0]),
	              dot(p, gJoints[at + 1]),
	              dot(p, gJoints[at + 2]));
}

VS_OUT ModelVS(float4 inPos : POSITION, float4 inNormal : NORMAL,
	float4 inBone : BLENDINDICES, float4 inWeight : BLENDWEIGHT)
{
	VS_OUT o;

	/* The normal is a byte triple biased by 127, the same bias a .TRG vertex
	 * carries. Read as signed bytes it comes out nonsense and quietly. */
	float4 normal = float4((inNormal.xyz - 127.0f) * (1.0f / 128.0f), 0.0f);
	float4 pos = float4(inPos.xyz, 1.0f);

	float3 view = 0.0f;
	float3 nrm = 0.0f;
	float3 light = 0.0f;

	/* THREE BONES. Unrolled, so every gJoints access is a literal register
	 * offset plus one address register - and so that inBone[i] is a component
	 * select rather than an indexed one, which vs_3_0 cannot do on a
	 * temporary at all. */
	[unroll]
	for (int i = 0; i < 3; i++)
	{
		int node = (int)(i == 0 ? inBone.x : (i == 1 ? inBone.y : inBone.z));
		float weight = (i == 0 ? inWeight.x : (i == 1 ? inWeight.y : inWeight.z));

		view += weight * Skin(pos, node);
		nrm += weight * Skin(normal, node);
		light += weight * gNodeLight[node].rgb;
	}

	o.pos = mul(float4(view, 1.0f), gProj);
	/* u is UNnormalised (0..255) and v is normalised (0..1) - see the vertex
	 * table at the top. */
	o.uv = float2(inBone.w * (1.0f / 255.0f), inWeight.w);
	o.light = light;
	o.viewPos = view;
	o.viewNrm = nrm;

	return o;
}

/* ---------------------------------------------------------------- pixel -- */

/* One fog bulb's contribution - the length of the piece of the eye ray inside
 * the bulb's sphere, times its density (drawroom.cpp:553-668). Identical to
 * hd_room.hlsl's BulbIntensity, quirk for quirk: when the bulb is behind the
 * camera `vec` is left holding the unit direction and the length comes out as
 * one, and the classic path truncates that to zero on the way into a long.
 * The two files keep their own copy rather than share an include, because
 * Direct3D 9 shader bytecode is compiled per entry point anyway and an
 * include would put a second file between the source and the .inc that GPLv3
 * asks to sit beside it. */
float BulbIntensity(float3 stash, float4 a, float4 b)
{
	float3 P = a.xyz;
	float rad = a.w;
	float sqrad = rad * rad;
	float sqlen = dot(P, P);

	if (!(stash.z + rad > 0.0f && abs(stash.x) - rad < abs(stash.z) &&
		abs(stash.y) - rad < abs(stash.z)))
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

	return length(vec2 - vec) * b.w;
}

float4 ModelPS(VS_OUT input) : COLOR0
{
	float4 texel = tex2D(sPage, input.uv);

	/* ALPHA PASS ONLY. An opaque-pass tile's alpha channel holds a sparkle or
	 * gloss MASK and not coverage - TROne's own user report was the golden
	 * skull left as a single gold fragment because pass 0 discarded on it. */
	clip(gPass.x * (texel.a - 0.5f));

	/* The node colour arrives in the classic path's 0..255 space already -
	 * unlike a room vertex, whose baked pair is 0..1 and gets multiplied by
	 * 127.5 in hd_room.hlsl. */
	float3 f = input.light;

	/* THE DYNAMIC LIGHTS, per pixel, in VIEW space. The classic object path
	 * does not run this loop: InitDynamicLighting folds the engine's dynamics
	 * into the object's own light list, so the node colour above ALREADY has
	 * the torch in it and the engine passes gCounts.x = 0. The loop is here
	 * for the case S3-2 decides to light an HD object per pixel instead of
	 * per node - a model has fifty times the vertices of the mesh the classic
	 * formula was written for, and a per-node colour cannot show a torch
	 * moving across a face. Which of the two the engine uses is one count,
	 * not two code paths. */
	[unroll]
	for (int i = 0; i < HD_MAX_LIGHTS; i++)
	{
		if (i >= (int)gCounts.x)
			continue;

		float4 a = gLights[i * 2 + 0];
		float4 b = gLights[i * 2 + 1];

		float3 delta = input.viewPos - a.xyz;
		float dist2 = dot(delta, delta);

		if (dist2 < a.w * a.w)
		{
			float dist = sqrt(max(dist2, 1e-12f));
			float ramp = b.w * (a.w - dist);
			float ndl = max(0.0f, dot(normalize(input.viewNrm), -delta / dist));

			f += b.rgb * (ramp * ndl * 255.0f);
		}
	}

	/* THE DISTANCE FOG (output.cpp:172-179 for a mesh, drawroom.cpp:519 for a
	 * room - the same three lines). Linear in the view depth from
	 * tomb5.distance_fog blocks onwards, subtracted from all three channels.
	 * TR5's fog darkens, it does not blend towards a colour. */
	if (input.viewPos.z > gFog.x)
		f -= (input.viewPos.z - gFog.x) * gFog.y;

	/* BACK INTO THE REMASTER'S SCALE - the classic path's 128 split with its
	 * ceiling taken off, the same line hd_room.hlsl carries and for the same
	 * reason (the long note at the top of that file). */
	float3 c = f * (2.0f / 255.0f);
	float3 s = 0.0f;

	/* THE FOG BULBS. These keep the classic split: the density darkens the
	 * multiply and the bulb's own colour is added on top, which is what makes
	 * a bulb glow in front of a dark object instead of tinting it. */
	[loop]
	for (int j = 0; j < (int)gCounts.y; j++)
	{
		float4 a = gBulbs[j * 2 + 0];
		float4 b = gBulbs[j * 2 + 1];
		float intensity = BulbIntensity(input.viewPos, a, b);

		c -= intensity * (1.0f / 255.0f);
		s += intensity * b.rgb * (1.0f / 255.0f);
	}

	c = max(c, 0.0f);
	s = saturate(s);

	float3 lit = texel.rgb * c + s * gPass.w;

	/* Additive pass only: an unweighted glow washes the whole frame out. */
	lit *= lerp(1.0f, texel.a, gPass.y);

	return float4(lit, texel.a);
}

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
 * him. So the ambient comes from the engine's own CalculateObjectLighting, the
 * engine's own light LIST is evaluated per vertex below with the classic
 * formulas, and the fog and bulb terms are the very ones hd_room.hlsl runs.
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
 * c104 - c135 gNodeLight[32]      per model  (the ambient, one per node)
 * c136 - c159 gObjLights[24]      per object (12 lights, two registers each)
 * c160        gLightInfo          per object (count, the sun's factor)
 *
 * THE VERTEX REGISTERS OVERLAP THE ROOM SHADER'S, and on purpose. Direct3D 9
 * has ONE constant register file behind both shaders, and hd_room.hlsl keeps
 * its water table in c32-c95 and its wibble table in c96-c127 - straight
 * through the joint palette above. That is safe because of the ORDER and
 * because of two lines in hd_render.cpp: rooms are drawn before models in
 * every frame, HDRenderFrameBegin forces the water table's first upload of
 * each frame (`g_water_table_up = ~0`), and apply_frame_lighting writes the
 * wibble table every frame as well. So a model can write over both and the
 * next frame's first room puts them back before it needs them. The PIXEL side
 * is the opposite arrangement and just as deliberate: there the two shaders
 * SHARE the registers - c16 fog, c40 bulbs - so one frame's constants serve
 * both. What must never happen is writing a register a shader `def`s for
 * itself, which is why gPass goes to the pixel file alone (see below).
 *
 * 161 of the 256 vs_3_0 registers. The palette is 32 nodes because the
 * highest bone index a weighted vertex names over all 566 models of game 5 is
 * 28 (GREEN_TEETH); the only two files past that, PIERRE_HEAD and
 * LARSON_HEAD at 40, are facial-rig heads whose numbers belong to the rig and
 * which are drawn rigidly on ONE node. A classic TR5 moveable has at most 32
 * meshes anyway.
 */

/* view -> clip. NOT world -> clip: a node matrix already carries the object
 * into view space, exactly as the classic path's matrix stack does. */
float4x4 gProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = the NEAR CUT as a
 * RADIUS around the eye in view units (0 = off, stage S8), w = specular
 * (overbright) on/off. The room shader
 * carries the same register and the same x, y and w; only z differs, and it
 * differs safely - see the note at the cut itself in ModelPS.
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
 * bind offset already folded in.
 *
 * THIS RANGE OVERLAPS THE ROOM SHADER'S c9 to c13 and its water and wibble
 * tables, and that is fine as long as every path writes its own constants
 * before it draws - which is why hd_render.cpp's room draw marks register 13
 * dirty at the top of every pass rather than trusting the value it left there
 * last time (g_motion_dirty). Named here because that is the kind of thing
 * that is only visible from the other file. */
float4 gJoints[96] : register(c8);

/* THE AMBIENT, ONE COLOUR PER NODE, in the classic path's 0..255 space: what
 * InitObjectLighting leaves in aAmbientR/G/B for this object. Weighted by the
 * same three weights as the position, so it crosses a joint smoothly - which
 * matters for Lara alone (LaraNodeAmbient), because for every other object
 * the shadow lights that lower the ambient are measured at the ITEM and the
 * 32 entries are the same number.
 *
 * The DIRECTIONAL part is not in here. It is worked out below, per vertex,
 * out of gObjLights - because that is where the classic path works it out
 * (specific\output.cpp, aTransformLightClipMesh, per vertex with the vertex's
 * own normal), and a flat colour a node would draw a torso whose front and
 * back are equally bright. */
float4 gNodeLight[32] : register(c104);

/* THE OBJECT'S LIGHT LIST, in VIEW space. Two registers each:
 *
 *   (x, y, z, kind)     kind 0 sun: xyz is a unit DIRECTION towards it
 *                       kind 1 point, kind 2 spot: xyz is a POSITION
 *   (r, g, b, invRange) the colour in the classic 0..255 scale;
 *                       1 / Outer for a point, 1 / Cutoff for a spot, 0 sun
 *
 * WHY POSITIONS AND NOT THE ENGINE'S OWN PER-NODE DIRECTIONS. The classic
 * path recomputes a point light's direction and its falloff once per NODE,
 * from that node's world position - the finest it could afford in 1999. The
 * surface point is right here, so both come out of it exactly, and a torch at
 * a guard's feet lights his boots and not his helmet.
 *
 * WHY VIEW SPACE IS ENOUGH FOR THE DOT PRODUCT. The engine's vectors are in
 * the node's MODEL space and its normals with them; a skinned normal arrives
 * here in view space. The node matrix is a rotation and a translation with no
 * scale, so dot(n, L) is the same number in both spaces and one list serves
 * every node of the object.
 */
#define HD_MAX_OBJ_LIGHTS 12

float4 gObjLights[HD_MAX_OBJ_LIGHTS * 2] : register(c136);

/* x = how many of them are filled, y = the factor a SUN is multiplied by
 * (0.75 in the game, 2 in the inventory - output.cpp:150-157). */
float4 gLightInfo : register(c160);

/* --------------------------------------------------- THE PARITY BLOCK ------
 *
 * constants, pixel side
 *
 * c4          gPass               per pass  (the same one)
 * c16         gFog                per frame
 * c17         gCounts             per object
 * c20 - c35   gLights[16]         per object (8 lights, two registers each)
 * c40 - c71   gBulbs[32]          per frame  (16 bulbs, two registers each)
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
 * run time loop makes fxc emulate every access with a chain of comparisons.
 *
 * BOTH NUMBERS MUST MATCH hd_room.hlsl AND HD_MAX_BULBS IN hd_render.h: one
 * frame's constant upload serves both shaders, so a bulb this file does not
 * know about is a register the room shader reads and this one ignores - an
 * object standing in a haze the wall behind it has. The 6 -> 8 raise is
 * measured and explained in hd_room.hlsl. */
#define HD_MAX_LIGHTS 8
#define HD_MAX_BULBS  16

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

	/* THE DIRECTIONAL LIGHT, LINE FOR LINE OUT OF aTransformLightClipMesh
	 * (specific\output.cpp:100-160). Three kinds, and only two shapes behind
	 * them:
	 *
	 *   point  val = ((L.n + 1) * 0.5)^2 * rad,  rad = (Outer - d) / Outer
	 *   spot   val = max(0, L.n)        * rad,   rad = 1 - d / Cutoff
	 *   sun    val = max(0, L.n) * gLightInfo.y, no falloff at all
	 *
	 * The engine's dynamic lights (a torch, a flare, a gun flash) are point
	 * lights of exactly this shape - InitDynamicLighting puts them in the
	 * very same PointLights array - and reach an object the same way.
	 *
	 * The normal is renormalised where the classic path does not have to: a
	 * classic mesh normal is a unit vector on disk, a .TRM normal is a byte
	 * triple that has been through a weighted sum of up to three rotations. */
	float3 unit = nrm * rsqrt(max(dot(nrm, nrm), 1e-12f));

	[unroll]
	for (int j = 0; j < HD_MAX_OBJ_LIGHTS; j++)
	{
		if (j < (int)gLightInfo.x)
		{
			float4 a = gObjLights[j * 2 + 0];
			float4 b = gObjLights[j * 2 + 1];

			/* A sun's direction is already the direction; the other two are
			 * positions and the direction is measured from the surface. */
			float3 delta = a.xyz - view;
			float dist = length(delta);
			float3 L = a.w < 0.5f ? a.xyz : delta / max(dist, 1e-6f);

			float ndl = dot(unit, L);
			float wrap = (ndl + 1.0f) * 0.5f;
			float rad = max(0.0f, 1.0f - dist * b.w);

			/* point: the wrapped square; spot and sun: the plain cosine. */
			float val = a.w > 0.5f && a.w < 1.5f
				? wrap * wrap * rad
				: max(0.0f, ndl) * (a.w < 0.5f ? gLightInfo.y : rad);

			light += val * b.rgb;
		}
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

	/* THE NEAR CUT (#123, stage S8), and it is Lara's alone.
	 *
	 * In first person the camera hangs in her head joint, and the classic
	 * path answers that by leaving the head MESH out. An HD body is one
	 * skinned surface: dropping the head node drops the head, but the neck
	 * stump belongs to the TORSO node and stays - triangles eighteen units
	 * from the eye, inside the near plane, which is what stands in the
	 * picture when she looks down (user finding F3, with a photograph).
	 *
	 * A SPHERE AROUND THE EYE, not a plane across the view, and that is the
	 * whole design of this line. A plane cuts at a fixed DEPTH, so as soon as
	 * she looks down it lies flat across her chest and opens a window through
	 * her - measured, and the two pictures are in the report: at 72 units of
	 * depth the near cut removed her collarbone and one saw the floor through
	 * the hole. A sphere cuts at a fixed DISTANCE, which no view direction can
	 * turn into a hole: the stump is 18 to 42 units from the eye and the
	 * nearest shoulder or chest vertex of any outfit is 68.9, so one radius
	 * separates them from every angle. hd_lara.h carries the measurement.
	 *
	 * ZERO IS OFF, at no cost: a length is never negative, so clip(len - 0)
	 * discards nothing. That also makes this line safe against the room
	 * shader, which reads its own gPass.z in the VERTEX half while
	 * hd_model.cpp writes c4 to the PIXEL file alone.
	 *
	 * Written per DRAW, so only Lara's body carries it: her hands, her
	 * holsters and the weapon on her back are their own models and get 0,
	 * which is why the cut can sit close enough to take the neck without
	 * ever taking a hand that comes up to the camera. */
	clip(length(input.viewPos) - gPass.z);

	/* The light arrives in the classic path's 0..255 space already - unlike a
	 * room vertex, whose baked pair is 0..1 and gets multiplied by 127.5 in
	 * hd_room.hlsl - and it is the ambient plus everything the vertex shader
	 * worked out of gObjLights. */
	float3 f = input.light;

	/* THE ROOM'S DYNAMIC LIGHTS, per pixel, in VIEW space. AN OBJECT NEVER
	 * RUNS THIS LOOP: the engine writes gCounts.x = 0 for every object,
	 * because a dynamic light reaches an object as an ordinary point light in
	 * the vertex shader's own list (InitDynamicLighting folds the engine's
	 * dynamics into the object's light list, and that is where they belong -
	 * the classic path lights an object per VERTEX). The loop is left in
	 * place - not deleted - so that this file stays the room shader's twin
	 * register for register, which is what lets one frame's constants serve
	 * both; the engine turns it off with a count and not with a shader. */
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

	/* THE ADDITIVE PASS HAS A CEILING (#123, fix round B, B2 - the same line
	 * and the same argument as hd_room.hlsl carries since fix round A, A3).
	 *
	 * The classic path builds its colour in bytes and hands the device a
	 * D3DCOLOR: a channel over 255 is simply dropped there. This shader
	 * carries the 128 split as a multiply of up to two, which the opaque pass
	 * wants - stage S2 measured it - but which doubles a glow. gPass.y is 1
	 * in the additive pass and 0 in the other two, so the opaque pass is
	 * untouched, to the bit. Before the fog bulbs, because that is where the
	 * classic path splits. */
	c = lerp(c, min(c, 1.0f), gPass.y);

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

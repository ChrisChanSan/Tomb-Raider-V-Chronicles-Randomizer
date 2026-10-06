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
 * The lighting below the line "the parity block" is ours: TROne lights an
 * item with a flat ambient blown up into a six-face cube and up to four point
 * lights of its own, while this engine has the classic path's own
 * per-object light list, its distance fog and its fog bulbs - and a torch has
 * to reach an HD guard exactly as far as it reaches the classic wall behind
 * him. So the ambient comes from the engine's own CalculateObjectLighting, the
 * engine's own light LIST is evaluated per vertex below with the classic
 * formulas. The fog term is hd_room.hlsl's, and so is the bulb term: the
 * classic chord with hd_room's exact behind-the-camera cull and its softened
 * rim.
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

/* The HD object shader: draws a skinned remaster .TRM model (Lara, enemies,
 * items) on the classic mesh tree, lit by the engine's own object lighting,
 * distance fog and fog bulbs.
 *
 * Build. Like hd_room.hlsl: no fxc step in tomb5.vcxproj, the bytecode is
 * checked in beside this file as two C headers, rebuilt by
 * Preparation\Tools\Diagnose\bshader.bat with
 *
 *     fxc /nologo /T vs_3_0 /E ModelVS /O3 /Vn kHDModelVS /Fh hd_model_vs.inc hd_model.hlsl
 *     fxc /nologo /T ps_3_0 /E ModelPS /O3 /Vn kHDModelPS /Fh hd_model_ps.inc hd_model.hlsl
 *
 * The vertex is the 24-byte .TRM vertex on disk, unchanged - only a
 * declaration that reads its bytes with the right meaning:
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
 * All three weights are needed. Over the 566 models of game 5, 174,896 of
 * 1,260,496 vertices (13.9 %) carry more than one weight, and the three
 * always sum to exactly 255. Two weights would tear every elbow.
 *
 * Where the bind pose went. TROne subtracts a per-node bind offset in the
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
 * c161        gSpec               per draw
 * c162        gNeckCut            per draw
 * c163 - c166 gSoft[4]            per draw   (the chest's secondary motion)
 *
 * The vertex registers overlap the room shader's, on purpose. Direct3D 9
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
 * c163 and up lie above everything hd_room.hlsl declares (its vertex
 * registers end at c127), so the room shader leaves nothing there for a
 * later draw to trip over.
 *
 * 167 of the 256 vs_3_0 registers. The palette is 32 nodes because the
 * highest bone index a weighted vertex names over all 566 models of game 5 is
 * 28 (GREEN_TEETH); the only two files past that, PIERRE_HEAD and
 * LARSON_HEAD at 40, are facial-rig heads whose numbers belong to the rig and
 * which are drawn rigidly on ONE node. A classic TR5 moveable has at most 32
 * meshes anyway.
 */

/* view -> clip. NOT world -> clip: a node matrix already carries the object
 * into view space, exactly as the classic path's matrix stack does. */
float4x4 gProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = the HEAD CUT on/off
 * (the cut itself - node and threshold - is in gNeckCut), w = specular
 * (overbright) on/off. The room shader carries the same register and the
 * same x, y and w and does not read z - see the note at the cut itself in
 * ModelPS.
 *
 * Read by the pixel shader only, and that matters. Neither this shader nor
 * the room shader needs it in the vertex half,
 * so fxc drops it there and puts two of its own def constants in c4 and c5 of
 * the vertex shader (visible in the /Fc listing). Writing gPass to the vertex
 * constant file would silently overwrite a literal. hd_model.cpp therefore
 * sets it through SetPixelShaderConstantF alone. */
float4 gPass : register(c4);

/* The joint palette, three registers a node, as an array of float4 and not as
 * an array of float4x3. The three registers of node b are the three COLUMNS
 * of its matrix, so a skinned position is three dot products written out
 * below - which is what the hardware does anyway, and it leaves no room for a
 * disagreement about how fxc packs an array of matrices. hd_model.cpp writes
 * exactly these columns.
 *
 * Each matrix is the node's classic transform - model space to VIEW space,
 * bind offset already folded in.
 *
 * This range overlaps the room shader's c9 to c13 and its water and wibble
 * tables, which is fine as long as every path writes its own constants
 * before it draws - which is why hd_render.cpp's room draw marks register 13
 * dirty at the top of every pass rather than trusting the value it left there
 * last time (g_motion_dirty). */
float4 gJoints[96] : register(c8);

/* The ambient, one colour per node, in the classic path's 0..255 space: what
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
 * back are equally bright.
 *
 * The fourth component is not light and is not read; it rides along as a
 * zero. */
float4 gNodeLight[32] : register(c104);

/* The object's light list, in VIEW space. Two registers each:
 *
 *   (x, y, z, kind)     kind 0 sun: xyz is a unit DIRECTION towards it
 *                       kind 1 point, kind 2 spot: xyz is a POSITION
 *   (r, g, b, invRange) the colour in the classic 0..255 scale;
 *                       1 / Outer for a point, 1 / Cutoff for a spot, 0 sun
 *
 * Why positions and not the engine's own per-node directions. The classic
 * path recomputes a point light's direction and its falloff once per NODE,
 * from that node's world position. The surface point is right here, so both
 * come out of it exactly, and a torch at a guard's feet lights his boots and
 * not his helmet.
 *
 * Why view space is enough for the dot product. The engine's vectors are in
 * the node's MODEL space and its normals with them; a skinned normal arrives
 * here in view space. The node matrix is a rotation and a translation with no
 * scale, so dot(n, L) is the same number in both spaces and one list serves
 * every node of the object.
 */
#define HD_MAX_OBJ_LIGHTS 12

float4 gObjLights[HD_MAX_OBJ_LIGHTS * 2] : register(c136);

/* x = how many of them are filled, y = the factor a SUN is multiplied by
 * (0.75 in the game, 2 in the inventory - output.cpp). */
float4 gLightInfo : register(c160);

/* The sheen. x = strength, y = the Blinn-Phong
 * exponent. x = 0 is off, which is what every object and every part of the
 * wardrobe that is not meant to shine gets; see HDModelLighting in hd_model.h
 * for where the numbers come from and why they are not read out of the art. */
float4 gSpec : register(c161);

/* The first person head cut, as one bone weight.
 *
 * gNeckCut  x the NODE whose weight decides - Lara's head, node 14 - and y
 *           the threshold: a fragment whose weight on that node REACHES y is
 *           discarded. z and w are unused.
 *
 * The cut is a bone weight rather than a shape in space. A plane fixed to the
 * head tilts with the head and cuts across the chest and shoulders; a shape in
 * the bind pose slides across a skinned neck as the head turns; a sphere about
 * the eye eats whatever comes near the camera. A bone weight is written in the
 * .TRM beside the vertex, is the same number in every pose and at every camera
 * angle, and the vertex shader already has it for the skinning - so the cut
 * costs three comparisons in a loop that runs anyway and nothing on the
 * processor.
 *
 * hd_lara.h carries the threshold and the measurement over the eight wardrobe
 * bodies that picked it. The hole the cut leaves is closed by a separate lid
 * model (hd_lara.h, HD_LARA_NECK_CAP_SEGMENTS). */
float4 gNeckCut : register(c162);

/* The soft motion of the chest: two regions on the body and an offset each.
 *
 *   gSoft[0]  the left region's centre in BIND space, and 1 / its radius
 *   gSoft[1]  the right region's, likewise
 *   gSoft[2]  the left offset in VIEW space, and in w the node whose weight
 *             carries it - Lara's torso, node 7
 *   gSoft[3]  the right offset in view space; w unused
 *
 * A vertex moves by the sum over the two sides of its mask times the side's
 * offset, times its own weight on the torso node. The mask is a smoothstep of
 * 1 - distance / radius from the vertex's bind position, so it is 1 at the
 * centre and 0 from the radius out, and - being a property of the bind pose
 * like the weights - it is the same patch of her in every pose.
 *
 * The region and the weight live in the vertex's bind space and the offset in
 * the space the vertex lands in, so the offset is the only number that changes
 * from frame to frame and from a draw to its mirrored draw (hd_softbody.h,
 * HDSoftConstants). The normal is left as it is: the offset is a few units
 * on a surface whose shading is set by its curvature.
 *
 * Every draw writes all four (hd_model.cpp). Zeros for every draw but Lara's
 * own body parts with the motion on; zero offsets add exactly nothing. */
float4 gSoft[4] : register(c163);

/* --------------------------------------------------- the parity block ------
 *
 * constants, pixel side
 *
 * c4          gPass               per pass  (the same one)
 * c5 - c7     gClip[3]            per object (a mirror's three clip planes;
 *                                 this shader's alone, the room shader
 *                                 has no such register)
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

/* A mirror's three clip planes in VIEW space, (n.x, n.y, n.z, d) each: a pixel
 * is kept where dot(n, viewPos) + d >= 0 for all three - the copy's side of
 * the glass and its two ends along it (game\mirror_math.h, MirrorClipPlanes).
 * (0, 0, 0, 1) three times, which keeps every pixel, for everything that is
 * not a reflection; hd_model.cpp writes it at every bracket's start
 * (HDModelFrameBegin) and per object when it changes (HDModelClip). */
float4 gClip[3] : register(c5);

/* x = tomb5.distance_fog * 1024, y = 255 / that. */
float4 gFog : register(c16);

/* x = dynamic lights, y = fog bulbs. */
float4 gCounts : register(c17);

/* Unrolled against a compile time maximum for the reason hd_room.hlsl gives
 * at length: ps_3_0 has no relative addressing of constant registers, and a
 * run time loop makes fxc emulate every access with a chain of comparisons.
 *
 * Both numbers must match hd_room.hlsl and HD_MAX_BULBS in hd_render.h: one
 * frame's constant upload serves both shaders, so a bulb this file does not
 * know about is a register the room shader reads and this one ignores - an
 * object without the haze the wall behind it has. The bulb count is measured
 * and explained in hd_room.hlsl. */
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
	float3 spec    : TEXCOORD4;	// the sheen, 0 when gSpec.x is 0
	/* The head cut's keep value: the threshold minus
	 * the vertex's own weight on the head node, so it is negative where the
	 * head owns the vertex and positive where it does not, and the pixel
	 * shader's whole share of the cut is one clip of an interpolated number.
	 * Interpolating it is what makes the edge follow the weight across a
	 * triangle rather than take whole triangles at a time - and because the
	 * weights are a property of the SKIN, that edge is the same patch of her
	 * in every pose. */
	float  cut     : TEXCOORD5;
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
	 * carries. Read as signed bytes it comes out silently wrong. */
	float4 normal = float4((inNormal.xyz - 127.0f) * (1.0f / 128.0f), 0.0f);
	float4 pos = float4(inPos.xyz, 1.0f);

	float3 view = 0.0f;
	float3 nrm = 0.0f;
	float3 light = 0.0f;

	// The head cut: this vertex's share of the one
	// node gNeckCut names, added up out of the same three bones the skinning
	// walks.
	float headWeight = 0.0f;

	// The soft motion's share: the vertex's weight on the torso node gSoft
	// names, out of the same three bones.
	float torsoWeight = 0.0f;

	/* Three bones. Unrolled, so every gJoints access is a literal register
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

		headWeight += abs((float)node - gNeckCut.x) < 0.5f ? weight : 0.0f;
		torsoWeight += abs((float)node - gSoft[2].w) < 0.5f ? weight : 0.0f;
	}

	/* The soft motion of the chest, before anything below reads `view`, so
	 * the light and the fog see the surface where it is drawn. The mask is
	 * hd_softbody.h's HDSoftMask line for line. */
	float tl = saturate(1.0f - length(inPos.xyz - gSoft[0].xyz) * gSoft[0].w);
	float tr = saturate(1.0f - length(inPos.xyz - gSoft[1].xyz) * gSoft[1].w);
	float ml = tl * tl * (3.0f - 2.0f * tl);
	float mr = tr * tr * (3.0f - 2.0f * tr);

	view += (ml * gSoft[2].xyz + mr * gSoft[3].xyz) * torsoWeight;

	/* The head cut, Lara's alone.
	 *
	 * One subtraction on a number the loop above already had: `o.cut` is how
	 * far this vertex's head weight is BELOW the threshold, so it is positive
	 * where the vertex is kept and negative where the head owns it. The pixel
	 * shader clips on the interpolated value.
	 *
	 * Off is a threshold of a million (hd_model.cpp), which every weight is
	 * below - so a model drawn with no cut discards nothing even if the pixel
	 * shader's own flag were stale. */
	o.cut = gNeckCut.y - headWeight;

	/* The directional light, line for line out of aTransformLightClipMesh
	 * (specific\output.cpp). Three kinds, and only two shapes behind
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

	/* The sheen. Blinn-Phong, in the VERTEX shader
	 * beside the diffuse it belongs to - the classic path lights an object
	 * per vertex and this is that light's second half, not a second light.
	 *
	 * V is the direction to the eye. The camera sits at the origin of view
	 * space looking down +z, so it is simply -view.
	 *
	 * The light. Every light of the object's own list contributes, with the
	 * SAME direction and the SAME distance falloff the diffuse loop above
	 * worked out - a highlight that ignored the falloff would sit on Lara in
	 * a room whose only lamp is out of range. When the list is EMPTY, which
	 * is an ordinary room lit by its own vertex colours alone, the eye itself
	 * is the light; without that fallback a catsuit would be matt in most
	 * rooms.
	 *
	 * The highlight is NOT multiplied by the texture: latex is black and its
	 * highlight is white. It is added in the pixel shader, after the texel. */
	float3 spec = 0.0f;

	if (gSpec.x > 0.0f)
	{
		float3 V = -view * rsqrt(max(dot(view, view), 1e-12f));

		[unroll]
		for (int k = 0; k < HD_MAX_OBJ_LIGHTS; k++)
		{
			if (k < (int)gLightInfo.x)
			{
				float4 a = gObjLights[k * 2 + 0];
				float4 b = gObjLights[k * 2 + 1];

				float3 delta = a.xyz - view;
				float dist = length(delta);
				float3 L = a.w < 0.5f ? a.xyz : delta / max(dist, 1e-6f);
				float rad = a.w < 0.5f ? 1.0f : max(0.0f, 1.0f - dist * b.w);

				float3 H = L + V;
				H *= rsqrt(max(dot(H, H), 1e-12f));

				spec += b.rgb * (rad * pow(max(0.0f, dot(unit, H)), gSpec.y));
			}
		}

		if ((int)gLightInfo.x == 0)
		{
			/* The eye as the lamp, when the object's list is empty - which is
			 * an ordinary room whose light is all baked into the vertices.
			 * H = normalize(V + V) = V.
			 *
			 * Weighted by the light the vertex ALREADY has, and not by a flat
			 * 255: a highlight is a reflection of what is there, so a catsuit
			 * in an unlit corner must stay dark. `light` is the ambient plus
			 * whatever the loop above added, in the classic 0..255 space, so
			 * this reduces to the full sheen in a bright room and to nothing
			 * in a black one. */
			spec = light * pow(max(0.0f, dot(unit, V)), gSpec.y);
		}

		spec *= gSpec.x * (1.0f / 255.0f);
	}

	o.spec = spec;
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
 * the bulb's sphere, times its density (drawroom.cpp) - line for line
 * hd_room.hlsl's BulbIntensity, so that an HD object and the HD room behind it
 * stand in the same haze. The two shader files keep their own function rather
 * than share an include, because Direct3D 9 shader bytecode is compiled per
 * entry point anyway and an include would put a second file between the
 * source and the .inc that GPLv3 asks to sit beside it.
 *
 * The cull is the room's exact one: a sphere whose every point lies behind
 * the camera plane contributes nothing (P.z + rad <= 0), which also settles a
 * camera inside the bulb. The classic cone of 45 degrees about the view axis
 * is a per-vertex shortcut and, per pixel, a hard line in the frame: over a
 * flat wall 4000 units ahead in a 640 x 360 frame at fov 100, with a bulb of
 * radius 2048 and density 0.02 at (3500, 0, 3000) in view space, it takes
 * away haze worth up to 78.7 of 255 on 2.4 per cent of the frame, and on 5.0
 * per cent with the bulb at (1000, 0, 800), the camera inside it (both terms
 * transcribed to numpy and compared per pixel). hd_room.hlsl says the rest
 * of why. */
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

	/* The rim, softened exactly as hd_room.hlsl softens it: the chord
	 * weighted by a smoothstep over the outer 0.35 of its own share of the
	 * diameter, 1 through the whole core of the bulb. The raw chord stands up
	 * vertically at the silhouette: over a flat wall 6000 units ahead in a
	 * 640 x 360 frame at fov 80, with a bulb of radius 3072 and density 0.02
	 * at (0, 0, 5000) in view space, it draws the rim up to 11.2 of 255
	 * brighter than the softened term (both transcribed to numpy), and on
	 * Lara in ANDREA1 (ModelProbe, a bulb of radius 220 and density 0.4
	 * between the camera and her) the two differ by up to 16 of 255 on 2.7
	 * per cent of her pixels, where the bulb's edge crosses her legs. */
	const float kRim = 0.35f;

	float chord = length(vec2 - vec);
	float t = saturate(chord / max(2.0f * rad, 1e-4f));

	return chord * b.w * smoothstep(0.0f, kRim, t);
}

float4 ModelPS(VS_OUT input) : COLOR0
{
	float4 texel = tex2D(sPage, input.uv);

	/* Alpha pass only. An opaque-pass tile's alpha channel holds a sparkle or
	 * gloss MASK and not coverage - in TROne a test in pass 0 left the golden
	 * skull as a single gold fragment. */
	clip(gPass.x * (texel.a - 0.5f));

	/* The first person head cut, Lara's alone.
	 *
	 * In first person the camera hangs in her head joint, and the classic
	 * path answers that by leaving the head MESH out. An HD body is one
	 * skinned surface: dropping the head node drops the head, but the neck
	 * stump belongs to the TORSO node and stays - triangles eighteen units
	 * from the eye, inside the near plane, standing in the picture when she
	 * looks down.
	 *
	 * What is cut is what her BONE WEIGHTS say is head, not a shape in space
	 * (see gNeckCut for why). The vertex under her chin is half head and half
	 * torso, and a weight moves with the skin in every pose.
	 *
	 * The vertex shader added the weight up (see gNeckCut there); this is the
	 * clip, and gPass.z is 1 when the cut runs and 0 when it does not. lerp to a
	 * flat 1 for the off case, so that a model with no cut discards nothing
	 * whatever is left in the interpolant. hd_model.cpp writes c4 to the
	 * PIXEL file before every pass, so the 0 a room pass leaves in z never
	 * reaches this line.
	 *
	 * Written per DRAW, so only Lara's body carries it: her hands, her
	 * holsters, the weapon on her back and the neck cap itself are their own
	 * models and get 0. */
	clip(lerp(1.0f, input.cut, gPass.z));

	/* A mirror's reflection ends where the copy behind the glass ends: a body
	 * whose origin stands inside the copy reaches past its ends into the real
	 * room beside it, and through the glass into the room in front. viewPos
	 * is the skinned point in view space, the space the planes are given in. */
	float4 clipPos = float4(input.viewPos, 1.0f);

	clip(min(min(dot(clipPos, gClip[0]), dot(clipPos, gClip[1])),
		dot(clipPos, gClip[2])));

	/* The light arrives in the classic path's 0..255 space already - unlike a
	 * room vertex, whose baked pair is 0..1 and gets multiplied by 127.5 in
	 * hd_room.hlsl - and it is the ambient plus everything the vertex shader
	 * worked out of gObjLights. */
	float3 f = input.light;

	/* The room's dynamic lights, per pixel, in VIEW space. An object never
	 * runs this loop: the engine writes gCounts.x = 0 for every object,
	 * because a dynamic light reaches an object as an ordinary point light in
	 * the vertex shader's own list (InitDynamicLighting folds the engine's
	 * dynamics into the object's light list, and that is where they belong -
	 * the classic path lights an object per VERTEX). The loop stays so that
	 * this file remains the room shader's twin register for register, which is what lets one frame's constants serve
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

	/* The distance fog (output.cpp for a mesh, drawroom.cpp for a room - the
	 * same three lines). Linear in the view depth from
	 * tomb5.distance_fog blocks onwards, subtracted from all three channels.
	 * TR5's fog darkens, it does not blend towards a colour. */
	if (input.viewPos.z > gFog.x)
		f -= (input.viewPos.z - gFog.x) * gFog.y;

	/* Back into the remaster's scale - the classic path's 128 split with its
	 * ceiling taken off, the same line hd_room.hlsl carries and for the same
	 * reason (the long note at the top of that file). */
	float3 c = f * (2.0f / 255.0f);

	/* The additive pass has a ceiling - the same line and the same argument
	 * as in hd_room.hlsl.
	 *
	 * The classic path builds its colour in bytes and hands the device a
	 * D3DCOLOR: a channel over 255 is simply dropped there. This shader
	 * carries the 128 split as a multiply of up to two, which the opaque pass
	 * wants but which doubles a glow. gPass.y is 1
	 * in the additive pass and 0 in the other two, so the opaque pass is
	 * untouched, to the bit. Before the fog bulbs, because that is where the
	 * classic path splits. */
	c = lerp(c, min(c, 1.0f), gPass.y);

	float3 s = 0.0f;

	/* The fog bulbs. These keep the classic split: the density darkens the
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

	/* The sheen goes on top of the texel and not through it - a highlight on
	 * black latex is white, and multiplying it by the
	 * texture would be the same as not having one. Never in the ADDITIVE
	 * pass, which is a glow and has no surface to shine. */
	float3 lit = texel.rgb * c + input.spec * (1.0f - gPass.y) + s * gPass.w;

	/* Additive pass only: an unweighted glow washes the whole frame out. */
	lit *= lerp(1.0f, texel.a, gPass.y);

	return float4(lit, texel.a);
}

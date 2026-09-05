/*
 * Modified 2026 by ChrisChanSan.
 *
 * The lighting rule below - room light is the SUM of the two vertex colour
 * triples, the alpha test lives in the alpha pass ONLY, and the additive pass
 * carries its weight in the texture's alpha - is carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, host\render\game_renderer.cpp, the GLSL
 * shaders k_hd_vert / k_hd_frag). Translated to HLSL for Direct3D 9; not a
 * line of the original is copied. See THIRD-PARTY.md.
 *
 * Everything below the line "THE PARITY BLOCK" is ours and has no counterpart
 * in TROne: TROne has no fog of any kind (its set_fog is a logged stub), no
 * fog bulbs (it parses Trg::fog_boxes and drops them), no dynamic lights in
 * rooms, and its water is an invented wave rather than the engine's own
 * WaterTable. Those five are transcriptions of specific\drawroom.cpp,
 * aRoomletTransformLight, line for line.
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

/* THE HD ROOM SHADER (#123, stages S1 and S2).
 *
 * HOW IT IS BUILT. There is no fxc step in tomb5.vcxproj on purpose - the
 * project must build on a machine with nothing but the Visual Studio C++
 * toolset. The bytecode is checked in beside this file as two C headers, and
 * this file is the source GPLv3 requires to sit next to them. Rebuild them
 * with
 *
 *     Preparation\Tools\Diagnose\bshader.bat
 *
 * which is exactly these two calls:
 *
 *     fxc /nologo /T vs_3_0 /E RoomVS /O3 /Vn kHDRoomVS /Fh hd_room_vs.inc hd_room.hlsl
 *     fxc /nologo /T ps_3_0 /E RoomPS /O3 /Vn kHDRoomPS /Fh hd_room_ps.inc hd_room.hlsl
 *
 * fxc.exe comes from the Windows SDK (Windows Kits\10\bin\<version>\x86).
 * Nothing of it is linked in: /Fh writes a plain byte array, and
 * CreateVertexShader / CreatePixelShader take it as it is. No d3dx, no
 * runtime compiler, no extra DLL in the package.
 *
 * THE VERTEX IS THE ONE ON DISK PLUS FOUR FLAG BYTES. A .TRG vertex is 20
 * bytes (Trg.h) and it keeps that size and that meaning on the card - only
 * the three colour bytes of each pair are written back to front, for the
 * reason below. Stage S2 appends a fifth element, four bytes the .TRG does
 * not have: the water marks the classic path keeps in its roomlets.
 *
 *   +0   SHORT4    x, y, z, sway     POSITION   room local, classic units
 *   +8   UBYTE4    nx, ny, nz, layer NORMAL     biased by 127; layer unused
 *                                               here (one page per draw call)
 *   +12  D3DCOLOR  b, g, r, u        COLOR0     first light triple + u
 *   +16  D3DCOLOR  b, g, r, v        COLOR1     second light triple + v
 *   +20  UBYTE4    w, s, r, 1        TEXCOORD0  three water marks, then a 1
 *
 * D3DDECLTYPE_D3DCOLOR reads the four bytes as one little-endian DWORD in
 * 0xAARRGGBB order and hands the shader (R, G, B, A) - so the byte at +0 is
 * BLUE, the byte at +3 is alpha, and the shader gets the triple as .rgb the
 * right way round. hd_render.cpp writes the pair in that order (c1b, c1g,
 * c1r, u) and this file reads .rgb: exactly ONE swap between the file and
 * the pixel. Two swaps - the tempting "the bytes arrive as .bgra, so read
 * .bgr" - cancel out and paint the whole level with red and blue exchanged,
 * which looks like a plausible picture and is the reason this paragraph is
 * this long.
 *
 * The alpha byte, .a, is the texture coordinate, already divided by 255,
 * which is exactly the scale it wants (TROne: u / 255.0f). That the two
 * coordinates sit in two different colours' alpha bytes is the remaster's
 * own packing, not ours. D3DDECLTYPE_UBYTE4 is NOT normalised, so the flag
 * bytes arrive as 0.0 or 1.0 exactly as they were written.
 *
 * THE LIGHT. light = colour1 * roomShadow + colour2, and the pixel is
 * texel * light. A SUM, not a product - TRForge measured that over all 144
 * .TRG files, and getting it wrong costs a factor of 2.65 in brightness.
 * roomShadow is 1 until shadow maps exist.
 *
 * THE THREE PASSES are the three index runs of a .TRG part, in this order:
 *
 *   0 opaque    no alpha test. The alpha channel of an opaque tile carries a
 *               gloss/sparkle MASK, not coverage - testing on it blanks whole
 *               floors (TROne, measured on a TR1 snow floor).
 *   1 alpha     clip(a - 0.5), the punch-through pass.
 *   2 additive  colour * alpha, blended ONE/ONE with the depth write off.
 *
 * All three switches ride in one constant so the shader never branches:
 * gPass.x is 1 in the alpha pass and 0 otherwise (clip(0) discards nothing),
 * gPass.y is 1 in the additive pass and 0 otherwise, gPass.w is 0 in the
 * additive pass and 1 otherwise - the classic path switches
 * D3DRS_SPECULARENABLE off for its additive draw type (polyinsert.cpp:72)
 * and the overbright term below has to go off with it.
 *
 * -------------------------------------------------------- THE PARITY BLOCK
 *
 * WHY THERE IS A 127.5, AND WHY THE 128 SPLIT IS NOT HERE. The classic path
 * computes a room vertex colour in 0..255 and then splits it
 * (drawroom.cpp:520-545): up to 128 it DOUBLES the value into the diffuse
 * colour, above 128 it pins the diffuse at 255 and puts the excess, halved,
 * into the SPECULAR colour, which the fixed function pipeline adds after the
 * texture modulation. So the classic pixel is
 *
 *     texel * (c / 255) + (s / 255),  c, s from that split,
 *
 * the multiply on the texel never passes 1, and the rest arrives as an ADD.
 * The remaster's baked light covers the same total range - 0..2, TRForge over
 * 144 .TRG files, colour1 + colour2 - but as a straight MULTIPLY, and stage
 * S1 draws it that way.
 *
 * The two meet exactly in the region below 128, and that is where the bridge
 * is built:
 *
 *     f = light * 127.5     light = f * 2 / 255
 *
 * Every contribution below is computed in the classic path's own 0..255
 * units, in the same order, and converted back with that one line. Below 128
 * the result is the classic colour to the last bit; above it, the multiply
 * keeps going instead of turning into an add.
 *
 * That is a DELIBERATE deviation, and it is the one place stage S2 does not
 * follow the classic path. The split is a hardware workaround - a way to fit
 * a brightness of 2 into a diffuse byte and a specular byte on a fixed
 * function pipeline - and a shader has floats and does not need it. Following
 * it here would have dulled every bright surface the remaster bakes: measured
 * over ANDREA1 and RICH1, up to 29 % of the vertex light channels of a room
 * are above 1, RICH1 room 12 reaches the full 2.0, and applying the split to
 * them would have turned "texel x 1.5" into "texel x 1 + 0.25" - a visibly
 * flatter room, and a silent change to a picture that was already judged
 * good. RoomProbe -paritycheck measures both: the contribution in the classic
 * path's own units (budget 2 of 255) and the final brightness, where the
 * deviation shows as a number.
 *
 * The fog bulbs are the exception that keeps its split, because for them it
 * is not a workaround: a bulb is haze BETWEEN the eye and the surface, so its
 * density has to darken the multiply and its colour has to be added on top,
 * or a bulb in front of a black wall would not glow.
 *
 * WHAT IS PER PIXEL AND WHAT IS PER VERTEX, AND WHY. The dynamic lights, the
 * fog bulbs and the distance fog are smooth functions of a position and are
 * computed PER PIXEL - a classic roomlet is a handful of vertices and a torch
 * on a long wall was a Gouraud smear; an HD room has the geometry to have
 * done it per vertex too, but per pixel is both better and cheaper to reason
 * about. The WaterTable effects (choppy, shimmer, the underwater darkening,
 * the Red Alert gas) stay PER VERTEX because they are not smooth: the table
 * is indexed by the vertex position quantised to 64 x 64 x 128 units, so per
 * pixel would paint visible 64-unit blocks where the classic path has a
 * smooth interpolation between corners. Same for the underwater screen
 * wobble, which is a per-vertex screen offset by construction.
 */

/* ------------------------------------------------- constants, vertex side --
 *
 * c0  - c3    gWorldViewProj      per room
 * c4          gPass               per pass
 * c5  - c7    gWorldView          per room   (the classic D3DMView: the
 *                                             "stash" the fog and the bulbs
 *                                             are measured in)
 * c9          gScreen             per frame
 * c10         gRoomOrigin         per room
 * c11         gWaterMode          per room
 * c12         gSel1               per room
 * c32 - c95   gWaterTable[64]     per room, only when the row changes
 * c96 - c127  gWibbleTable[32]    per level (it never changes)
 */
float4x4 gWorldViewProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = room shadow,
 * w = specular (overbright) on/off. */
float4 gPass : register(c4);

/* float4x3, not float4x4, and three registers rather than four: only the
 * xyz of the product is ever wanted, and a fourth register here would be a
 * register fxc has already handed to one of its own def constants - the
 * upload would then silently overwrite a literal. Three columns of the
 * transposed matrix go up, and the register table says three. */
float4x3 gWorldView : register(c5);

/* x = f_centerx, y = f_centery, z = wibble (the engine's own counter, as a
 * float), w = 1 while camera.underwater. */
float4 gScreen : register(c9);

/* xyz = the room's origin in world units (r->x, r->y, r->z), w = the room's
 * minfloor made room local (minfloor - r->y) - the water surface plane. */
float4 gRoomOrigin : register(c10);

/* x = 1 when the room carries ROOM_UNDERWATER, y = 1 when the choppy
 * displacement runs at all, z = 1 when the shimmer needs the vertex to sit
 * on the surface plane, w = 1 for the Red Alert gas rooms. */
float4 gWaterMode : register(c11);

/* Which of the baked marks decides "flags |= 1" in this room this frame. The
 * room flag branch of drawroom.cpp:451-475 is a per room decision and it is
 * taken on the processor; what is left for the shader is one dot product. All
 * zero while tomb5.shimmer is off - that is where the option switch lands.
 *
 * The FOURTH component pairs with the vertex's fourth flag byte, which is the
 * constant 1 - so a selector can turn a whole room on without a mark on every
 * vertex. The engine never uses it; the diagnostic tool's -shimmer does, and
 * that is a picture with no classic level behind it. */
float4 gSel1 : register(c12);

/* WaterTable[room->MeshEffect], 64 entries: x = shimmer, y = choppy,
 * z = random, w = abs. The engine hands over its OWN table rather than a
 * rebuilt one: init_water_table (gamemain.cpp:59) seeds rand() with 121197
 * and draws from it in a rejection loop, and a second implementation of that
 * is a second chance to be subtly wrong. */
float4 gWaterTable[64] : register(c32);

/* vert_wibble_table (gamemain.cpp:109), 32 entries, one per register - the
 * index is computed at run time and a float4 would need a component select
 * on top of the relative address. 32 registers of a 256 register file. */
float4 gWibbleTable[32] : register(c96);

/* -------------------------------------------------- constants, pixel side --
 *
 * c4          gPass               per pass  (the same one)
 * c16         gFog                per frame
 * c17         gCounts             per room
 * c20 - c35   gLights[16]         per room  (8 lights, two registers each)
 * c40 - c51   gBulbs[12]          per frame (6 bulbs, two registers each)
 *
 * Deliberately disjoint from the vertex numbers although the two constant
 * files are separate in Direct3D 9: one table of "register -> meaning" that
 * needs no column saying which shader.
 */

/* x = tomb5.distance_fog * 1024, y = 255 / that. No on/off switch beside it:
 * the classic path has none either - the option is a distance in blocks,
 * 3..30 (troyestuff.cpp:582-596), and it always subtracts. */
float4 gFog : register(c16);

/* x = dynamic lights, y = fog bulbs. */
float4 gCounts : register(c17);

/* HOW MANY, AND WHY THE TWO LOOPS ARE UNROLLED. ps_3_0 has no relative
 * addressing of constant registers - only vs_3_0 has. Written as a loop over
 * a run time count, fxc emulates gLights[i] with a chain of 32 cmp
 * instructions PER ACCESS: measured, 285 slots that execute about 140
 * instructions per light per pixel. Unrolled against a compile time maximum,
 * with the run time count as a dynamic if inside, every access is a literal
 * register and an unused slot costs a branch. That is why these two numbers
 * are here rather than 32 and 16.
 *
 * 8 lights: the engine holds 32 dynamics (drawroom.cpp:952) but they are
 * flare, torch, muzzle flash and explosions, and a single room seeing more
 * than eight of them at once does not happen in TR5. 6 bulbs: the classic
 * path's own RoomletFogBulbs is 16, and the Rome levels are the only ones
 * with a real count. Both are logged when they clip, so the number is a
 * measurement and not a hope. */
#define HD_MAX_LIGHTS 8
#define HD_MAX_BULBS  6

/* Two registers per light: (x, y, z, falloff) room local, then
 * (r, g, b, 1 / falloff). */
float4 gLights[HD_MAX_LIGHTS * 2] : register(c20);

/* Two registers per bulb: (x, y, z, radius) in VIEW space, then
 * (r, g, b, density). */
float4 gBulbs[HD_MAX_BULBS * 2] : register(c40);

sampler2D sPage : register(s0);

/* ---------------------------------------------------------------- shared -- */

struct VS_OUT
{
	float4 pos      : POSITION;
	float2 uv       : TEXCOORD0;
	/* The light travels as a TEXCOORD, not as a COLOR: a colour register is
	 * clamped to 0..1 on shader model 3 hardware, and the sum of two triples
	 * reaches 2. Clamping it here would quietly cap every bright room. */
	float3 light    : TEXCOORD1;
	float3 viewPos  : TEXCOORD2;	// the classic "stash"
	float3 roomPos  : TEXCOORD3;	// room local, what the dynamic lights use
	float3 waterAdd : TEXCOORD4;	// the WaterTable term, 0..255 units
	float3 viewNrm  : TEXCOORD5;	// normal * D3DMView, NOT renormalised
};

/* --------------------------------------------------------------- vertex -- */

/* (long)v >> shift, as C does it: truncate towards zero, then an ARITHMETIC
 * shift, which is a floor division and not the truncating one a plain
 * divide would give for negative room y. Shader model 3 has no integer
 * instructions, so this is the float spelling of both steps. */
float ShiftDown(float v, float divisor)
{
	return floor(trunc(v) / divisor);
}

/* x & mask for a two's complement integer, spelled as a modulo. */
float WrapIndex(float v, float count)
{
	return v - floor(v / count) * count;
}

/* The WaterTable's spatial hash (drawroom.cpp:357-360): the world position
 * quantised to 64 x 64 x 128 units, summed, wrapped to 64. */
int WaterHash(float3 world)
{
	float sum = ShiftDown(world.x, 64.0f)
	          + ShiftDown(world.y, 64.0f)
	          + ShiftDown(world.z, 128.0f);

	return (int)WrapIndex(sum, 64.0f);
}

/* The second, dependent lookup: ((wibble >> shift) + random) & 0x3F. */
int WaterPhase(float3 world, float wibbleShift)
{
	float rnd = gWaterTable[WaterHash(world)].z;

	return (int)WrapIndex(ShiftDown(gScreen.z, wibbleShift) + rnd, 64.0f);
}

VS_OUT RoomVS(float4 inPos : POSITION, float4 inNormal : NORMAL,
	float4 inC0 : COLOR0, float4 inC1 : COLOR1, float4 inFlags : TEXCOORD0)
{
	VS_OUT o;

	float3 pos = inPos.xyz;

	/* 1. THE CHOPPY DISPLACEMENT (drawroom.cpp:357-362). The classic path
	 * moves the first nWVtx vertices of every roomlet; inFlags.x is that
	 * same set, matched by position at level build. */
	if (gWaterMode.y * inFlags.x > 0.5f)
		pos.y += gWaterTable[WaterPhase(pos + gRoomOrigin.xyz, 4.0f)].y;

	/* 2. THE WATERTABLE COLOUR TERMS (drawroom.cpp:451-517), in the classic
	 * path's own order: underwater first, then the shimmer, then the Red
	 * Alert gas. Every one of them reads the position AFTER the
	 * displacement, which is why this block sits below it. */
	float atSurface = abs(pos.y - gRoomOrigin.w) < 0.5f ? 1.0f : 0.0f;
	float shimmerOn = dot(inFlags, gSel1) * lerp(1.0f, atSurface, gWaterMode.z);
	float gasOn = gWaterMode.w * (1.0f - atSurface);

	float3 waterAdd = 0.0f;

	if (gWaterMode.x > 0.5f)
	{
		/* -2 * choppy on all three channels: the room is under water. */
		waterAdd = (-2.0f * gWaterTable[WaterPhase(pos + gRoomOrigin.xyz, 4.0f)].y).xxx;
	}
	else if (shimmerOn > 0.5f)
	{
		float4 e = gWaterTable[WaterPhase(pos + gRoomOrigin.xyz, 4.0f)];
		waterAdd = ((e.x + e.w) * 8.0f).xxx;
	}
	else if (gasOn > 0.5f)
	{
		/* Red Alert's gas rooms: slower (wibble >> 3) and green only. */
		float4 e = gWaterTable[WaterPhase(pos + gRoomOrigin.xyz, 8.0f)];
		waterAdd = float3(0.0f, abs((e.x + e.w) * 8.0f), 0.0f);
	}

	/* 3. THE TRANSFORM. */
	float4 clip = mul(float4(pos, 1.0f), gWorldViewProj);
	float3 view = mul(float4(pos, 1.0f), gWorldView);

	/* 4. THE UNDERWATER SCREEN WOBBLE (drawroom.cpp:381-387). A pixel offset
	 * on the projected position, so it is added in clip space scaled by w:
	 * screen x = ndc * centerx + centerx, hence d(clip.x) = dx * w / centerx,
	 * and y the other way round because screen y counts downwards.
	 *
	 * The second lookup reads the ALREADY OFFSET screen x where the first
	 * reads the view space y - that is not a typo here, it is the order the
	 * classic path writes the two lines in, and the sequence is visible in
	 * the picture. */
	if (gScreen.w > 0.5f && clip.w > 1.0f)
	{
		float invw = 1.0f / clip.w;
		float zv2 = 512.0f * invw;

		float dx = gWibbleTable[(int)WrapIndex(ShiftDown(gScreen.z + trunc(zv2 * view.y), 8.0f), 32.0f)].x;
		float sx = clip.x * invw * gScreen.x + gScreen.x + dx;
		float dy = gWibbleTable[(int)WrapIndex(ShiftDown(gScreen.z + trunc(zv2 * sx), 8.0f), 32.0f)].x;

		clip.x += dx * clip.w / gScreen.x;
		clip.y -= dy * clip.w / gScreen.y;
	}

	o.pos = clip;
	o.uv = float2(inC0.a, inC1.a);
	o.light = inC0.rgb * gPass.z + inC1.rgb;
	o.viewPos = view;
	o.roomPos = pos;
	o.waterAdd = waterAdd;

	/* The byte normal, biased by 127, into the same space the classic path
	 * puts it in - view space, by the linear part of D3DMView. It is NOT
	 * renormalised, because the classic path does not renormalise either. */
	o.viewNrm = mul(float4((inNormal.xyz - 127.0f) * (1.0f / 128.0f), 0.0f), gWorldView);

	return o;
}

/* ---------------------------------------------------------------- pixel -- */

/* One fog bulb's contribution: the length of the piece of the eye ray that
 * lies inside the bulb's sphere, times its density (drawroom.cpp:553-668).
 * All of it in view space, where both the bulb centre (aBuildFogBulbList
 * puts it there once per frame) and the surface point live.
 *
 * The three branches are the camera inside the sphere, the surface point
 * inside it, and both outside - and the last one keeps a quirk of the
 * original: when the bulb is behind the camera, or nearer than its own
 * projection, `vec` is left holding the UNIT direction and `vec2` the
 * origin, so the length comes out as 1 and the intensity as one density,
 * about 0.02. The classic path truncates that to zero on the way into a
 * long. Kept rather than tidied away: this function is checked against that
 * one, number for number.
 */
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

float4 RoomPS(VS_OUT input) : COLOR0
{
	float4 texel = tex2D(sPage, input.uv);

	/* Alpha pass only - see the note about the gloss mask above. */
	clip(gPass.x * (texel.a - 0.5f));

	/* Into the classic path's 0..255 space. */
	float3 f = input.light * 127.5f;

	/* THE DYNAMIC LIGHTS (drawroom.cpp:415-436). Torch, flare, muzzle flash,
	 * every explosion - the engine's `dynamics` array, culled against this
	 * room's bounding box on the processor exactly as aBuildRoomletLights
	 * culls against a roomlet's.
	 *
	 * The mixed spaces in the middle line are the original's: the normal is
	 * in VIEW space and the direction to the light is ROOM LOCAL, so the term
	 * turns with the camera. It is Core's own arithmetic and reproducing it
	 * is the whole point - a "corrected" dot product lights the wall
	 * differently from the roomlet next to it. */
	[unroll]
	for (int i = 0; i < HD_MAX_LIGHTS; i++)
	{
		if (i >= (int)gCounts.x)
			continue;

		float4 a = gLights[i * 2 + 0];
		float4 b = gLights[i * 2 + 1];

		float3 delta = input.roomPos - a.xyz;
		float dist2 = dot(delta, delta);

		if (dist2 < a.w * a.w)
		{
			float dist = sqrt(max(dist2, 1e-12f));
			float ramp = b.w * (a.w - dist);
			float3 t = input.viewNrm * (delta / dist);

			f += b.rgb * (ramp * (1.0f - (t.x + t.y + t.z)) * 255.0f);
		}
	}

	/* THE WATERTABLE TERM, computed per vertex above. */
	f += input.waterAdd;

	/* THE DISTANCE FOG (drawroom.cpp:519-524). Linear in the view depth from
	 * tomb5.distance_fog blocks onwards, subtracted from all three channels -
	 * TR5's fog darkens, it does not blend towards a colour, and there is no
	 * fog colour anywhere in the classic path to take one from.
	 * D3DRS_FOGENABLE stays off: with a pixel shader the fixed function fog
	 * factor would have to come out of the vertex shader, and this one writes
	 * none. */
	if (input.viewPos.z > gFog.x)
		f -= (input.viewPos.z - gFog.x) * gFog.y;

	/* BACK INTO THE REMASTER'S SCALE. This is the classic path's split
	 * (drawroom.cpp:526-545) with its ceiling taken off - see the long note
	 * at the top. Below f = 128 the two are the same line. */
	float3 c = f * (2.0f / 255.0f);
	float3 s = 0.0f;

	/* THE FOG BULBS (drawroom.cpp:547-676). These DO keep the classic split:
	 * the density darkens the multiply and the bulb's own colour is added on
	 * top, which is what makes a bulb glow in front of a dark wall instead of
	 * tinting it. The classic path works in 0..255 there, so both terms are
	 * divided by 255 rather than by 127.5. */
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

	/* texel * multiply + add - the shape of the fixed function pipeline the
	 * classic path draws through, with D3DRS_SPECULARENABLE on
	 * (function_table.cpp:104) except for its additive draw type
	 * (polyinsert.cpp:72), which is what gPass.w carries. With no fog bulb in
	 * sight `s` is zero and this is stage S1's line unchanged. */
	float3 lit = texel.rgb * c + s * gPass.w;

	/* Additive pass only: an unweighted glow washes the whole frame out. */
	lit *= lerp(1.0f, texel.a, gPass.y);

	return float4(lit, texel.a);
}

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
 * Everything below the line "the parity block" is ours and has no counterpart
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

/* The HD room shader: draws the geometry of a remaster .TRG room with the
 * classic engine's own room lighting, fog, fog bulbs and water effects.
 *
 * Build. tomb5.vcxproj has no fxc step, so the project builds with the Visual
 * Studio C++ toolset alone. The bytecode is checked in beside this file as two
 * C headers, and this file is their source. Preparation\Tools\Diagnose\
 * bshader.bat rebuilds them with exactly these two calls:
 *
 *     fxc /nologo /T vs_3_0 /E RoomVS /O3 /Vn kHDRoomVS /Fh hd_room_vs.inc hd_room.hlsl
 *     fxc /nologo /T ps_3_0 /E RoomPS /O3 /Vn kHDRoomPS /Fh hd_room_ps.inc hd_room.hlsl
 *
 * fxc.exe comes from the Windows SDK (Windows Kits\10\bin\<version>\x86).
 * /Fh writes a plain byte array that CreateVertexShader / CreatePixelShader
 * take as it is: no d3dx, no runtime compiler, no extra DLL in the package.
 *
 * The vertex is the 20-byte .TRG vertex (Trg.h) unchanged in size and meaning,
 * except that the three colour bytes of each pair are stored back to front,
 * plus a fifth element of four bytes: the water marks the classic path keeps
 * in its roomlets.
 *
 *   +0   SHORT4    x, y, z, sway     POSITION   room local, classic units;
 *                                              sway is the remaster's wind
 *                                              weight
 *   +8   UBYTE4    nx, ny, nz, layer NORMAL     biased by 127; layer unused
 *                                               here (one page per draw call)
 *   +12  D3DCOLOR  b, g, r, u        COLOR0     first light triple + u
 *   +16  D3DCOLOR  b, g, r, v        COLOR1     second light triple + v
 *   +20  UBYTE4    w, s, r, 1        TEXCOORD0  three water marks, then a 1
 *
 * D3DDECLTYPE_D3DCOLOR reads the four bytes as one little-endian DWORD in
 * 0xAARRGGBB order and hands the shader (R, G, B, A): the byte at +0 is blue,
 * the byte at +3 is alpha. hd_render.cpp writes the pair as (c1b, c1g, c1r, u)
 * and this file reads .rgb - exactly one swap between the file and the pixel.
 * Reading .bgr as well would swap twice and paint the whole level with red and
 * blue exchanged, a plausible-looking but wrong picture.
 *
 * The alpha byte, .a, is the texture coordinate already divided by 255, the
 * scale it needs (TROne: u / 255.0f). The two coordinates sitting in two
 * different colours' alpha bytes is the remaster's own packing.
 * D3DDECLTYPE_UBYTE4 is not normalised, so the flag bytes arrive as 0.0 or
 * 1.0 exactly as written.
 *
 * The light. light = colour1 * gPass.z + colour2, and the pixel is
 * texel * light. A SUM, not a product - as TRForge's MIT reader adds the
 * two triples (trf_remaster/HdMesh.cpp, colour_of). gPass.z is a spare
 * scale on the first colour that this build always sets to 1; nothing
 * computes it.
 *
 * The three passes are the three index runs of a .TRG part, in this order:
 *
 *   0 opaque    no alpha test, except on the three cut-out materials 6, 7
 *               and 8, whose param[0] is the reference (hd_render.cpp,
 *               MaterialAlphaRef). On every other material the alpha
 *               channel of an opaque tile carries a gloss/wetness MASK, not
 *               coverage, and testing on it blanks whole floors and facades
 *               (TROne on a TR1 snow floor; ANDREA1's stucco, JOBY2's snow
 *               and JOBY3's bulkheads here).
 *   1 alpha     clip(a - 0.5), the punch-through pass.
 *   2 additive  colour * alpha, blended ONE/ONE with the depth write off.
 *
 * All three switches ride in one constant so the shader never branches:
 * gPass.x is the alpha reference - 0.5 in the alpha pass, the material's own
 * in the opaque pass of a cut-out part, 0 everywhere else (a texel alpha is
 * never below 0, so a zero reference discards nothing). gPass.y is 1 in the
 * additive pass and 0 otherwise; gPass.w is 0 in the additive pass and 1
 * otherwise, because the classic path switches D3DRS_SPECULARENABLE off for
 * its additive draw type (polyinsert.cpp) and the overbright term below goes
 * off with it.
 *
 * -------------------------------------------------------- the parity block
 *
 * Why there is a 127.5 and no 128 split. The classic path computes a room
 * vertex colour in 0..255 and then splits it (drawroom.cpp): up to 128 it
 * doubles the value into the diffuse colour, above 128 it pins the diffuse at
 * 255 and puts the excess, halved, into the SPECULAR colour, which the fixed
 * function pipeline adds after the texture modulation. So the classic pixel is
 *
 *     texel * (c / 255) + (s / 255),  c, s from that split,
 *
 * the multiply on the texel never passes 1, and the rest arrives as an ADD.
 * The remaster's baked light covers the same total range - 0..2, TRForge over
 * 144 .TRG files, colour1 + colour2 - but as a straight MULTIPLY.
 *
 * The two agree exactly below 128, and the bridge between them is
 *
 *     f = light * 127.5     light = f * 2 / 255
 *
 * Every contribution below is computed in the classic path's own 0..255
 * units, in the same order, and converted back with that one line. Below 128
 * the result is the classic colour to the last bit; above it the multiply
 * keeps going instead of turning into an add.
 *
 * That is a deliberate deviation from the classic path. The split is a fixed
 * function workaround for fitting a brightness of 2 into a diffuse byte and a
 * specular byte; a shader has floats. Applying it would dull every bright
 * surface the remaster bakes: over ANDREA1 and RICH1 up to 29 % of the vertex
 * light channels of a room are above 1, RICH1 room 12 reaches the full 2.0,
 * and the split would turn "texel x 1.5" into "texel x 1 + 0.25" - a visibly
 * flatter room. RoomProbe -paritycheck measures both the contribution in the
 * classic path's own units (budget 2 of 255) and the final brightness.
 *
 * The fog bulbs keep the split, because for them it is not a workaround: a
 * bulb is haze BETWEEN the eye and the surface, so its density darkens the
 * multiply and its colour is added on top, or a bulb in front of a black wall
 * would not glow.
 *
 * Per pixel and per vertex. The dynamic lights, the fog bulbs and the distance
 * fog are smooth functions of a position and are computed per pixel, which
 * avoids the Gouraud smear of a torch on a long classic wall. The WaterTable
 * effects (choppy, shimmer, the underwater darkening, the Red Alert gas) stay
 * per vertex because they are not smooth: the table is indexed by the vertex
 * position quantised to 64 x 64 x 128 units, so per pixel would paint visible
 * 64-unit blocks where the classic path interpolates smoothly between corners.
 * The underwater screen wobble is a per-vertex screen offset by construction.
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
 * c13         gMotion             per DRAW CALL
 * c14         gFlat               per DRAW CALL (.w is the flat-surface gate)
 * c15         gWaterBox           per room
 * c32 - c95   gWaterTable[64]     per room, only when the row changes
 * c96 - c127  gWibbleTable[32]    per level (it never changes)
 */
float4x4 gWorldViewProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = room shadow,
 * w = specular (overbright) on/off.
 *
 * hd_model.hlsl shares this register and shares x, y and w with it; only z
 * differs, and there it switches the first person head cut. The two do not
 * collide because this file's z is read in the VERTEX half and written
 * through SetVertexShaderConstantF, while hd_model.cpp writes c4 to the PIXEL
 * constant file alone - and a room is drawn before a model in every frame. */
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
 * minfloor made room local (minfloor - r->y) - the water surface plane.
 *
 * The one constant this file uploads to BOTH register files: the vertex half
 * needs it for the WaterTable's world space hash, the pixel half for the
 * caustic's world position. Same number, same register, two
 * SetShaderConstantF calls per room. */
float4 gRoomOrigin : register(c10);

/* x = 1 when the room carries ROOM_UNDERWATER, y = 1 when the choppy
 * displacement runs at all, z = 1 when the shimmer needs the vertex to sit
 * on the surface plane, w = 1 for the Red Alert gas rooms. */
float4 gWaterMode : register(c11);

/* Which of the baked marks decides "flags |= 1" in this room this frame. The
 * room flag branch of drawroom.cpp (the shimmer selection) is a per room
 * decision taken on the processor; what is left for the shader is one dot
 * product. All zero while tomb5.shimmer is off - that is the option switch.
 *
 * The fourth component pairs with the vertex's fourth flag byte, the constant
 * 1, so a selector can turn a whole room on without a mark on every vertex.
 * The engine never uses it; the diagnostic tool's -shimmer does. */
float4 gSel1 : register(c12);

/* What moves. Three motions, all driven by the .TRG's own data and the
 * engine's own clock:
 *
 *   x, y  the UV offset of THIS draw call in THIS frame. A .TRG part carries
 *         a material and four parameters (Trg.h), and material 1 is a UV
 *         SCROLL whose param.xy is a speed in page units per second - measured
 *         over all 15 levels, and calibrated against the classic path in
 *         RICH2, the one level where both exist: the classic UVROT tile moves
 *         7.5 tile heights a second there, the remaster's own scroll on the
 *         same surface 4.0. The offset is computed and wrapped on the
 *         processor, so this is one add.
 *   z     the game clock in seconds, counted from the engine's own
 *         GlobalCounter (30 Hz, stops when the game stops).
 *   w     1 while tomb5.hd_motion is on, 0 otherwise - the one switch for all
 *         three motions below.
 */
float4 gMotion : register(c13);

/* The wind, in classic units and radians per second. These numbers are a
 * judgement rather than a measurement, which is why they are named here. The
 * shape is TROne's (game_renderer.cpp, k_hd_vert: a sine of x + z, a second
 * one across it); 24 units of travel and a wavelength of about 1.4 blocks is
 * what that shader uses for its own water, and grass, ivy and kelp are the
 * same size of thing. */
#define HD_WIND_TRAVEL     24.0f
#define HD_WIND_WAVELENGTH 0.0045f
#define HD_WIND_SPEED      2.1f

/* The water surface, carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, host\render\game_renderer.cpp, k_hd_vert
 * 153-172 and k_hd_frag 211-223 - the shape of the swell, the UV wobble and
 * the caustic are his; see THIRD-PARTY.md).
 *
 * Why a caustic on top of the classic shimmer. TR5's own shimmer is the
 * WaterTable term, computed PER VERTEX, and a remaster water surface is a few
 * large quads whose corners are its only vertices, so that term reaches the
 * screen as a slow blotch, not a shimmer. TROne notes the same ("a
 * vertex-interpolated shimmer stayed flat and the surface looked still"). The
 * WaterTable term therefore stays untouched, parity and all, and the caustic
 * is added as a ZERO MEAN MODULATION on top of it: no average brightness of
 * its own, only the movement the classic term cannot carry at this vertex
 * count - so the shimmer's brightness is not counted twice.
 *
 * The three amplitudes are TROne's own. The strength is matched to two
 * reference shots of the same VCI pool from the remaster: over the water
 * region the two frames differ by 8.4 of 255 on average, with 17 percent of
 * the pixels more than 16 apart against a mean of 34 - a swing of a third to
 * a half of the local value, which 0.32 either way meets. */
#define HD_WATER_UV        (6.0f / 512.0f)
#define HD_WATER_WAVE      0.0045f
#define HD_WATER_SWELL     24.0f
#define HD_WATER_SPEED_A   2.1f
#define HD_WATER_SPEED_B   1.6f
#define HD_CAUSTIC_SCALE   0.018f
#define HD_CAUSTIC_WEIGHT  0.32f

/* WaterTable[room->MeshEffect], 64 entries: x = shimmer, y = choppy,
 * z = random, w = abs. The engine hands over its OWN table rather than a
 * rebuilt one: init_water_table (gamemain.cpp) seeds rand() with 121197 and
 * draws from it in a rejection loop, and a second implementation of that
 * would be a second chance to be subtly wrong. */
float4 gWaterTable[64] : register(c32);

/* vert_wibble_table (gamemain.cpp), 32 entries, one per register - the
 * index is computed at run time and a float4 would need a component select
 * on top of the relative address. 32 registers of a 256 register file. */
float4 gWibbleTable[32] : register(c96);

/* The sky band's flat light.
 *
 * x is 1 for a draw call whose .TRG part carries no baked light anywhere and
 * whose page is the whole picture - the band of sky, whose vertex light is
 * exactly zero on all 1,280 of its triangles in every level. Through the room
 * formula it would come out black. 0 for every other draw call, where the
 * baked light is the light. See hd_render.cpp, PartIsUnlit.
 *
 * w switches the flat-surface gate of the water motion (see "only where the
 * surface is flat" in RoomVS). */
float4 gFlat : register(c14);

/* Where this room's water stops.
 *
 * xy the smallest and zw the largest x and z, room local, of the box the
 * surface bit was flooded into (hd_render.cpp, "The shore of each room's
 * water"). A room with no water gets a box so large that nothing is ever
 * near its edge, and the taper below is then exactly 1.
 *
 * What it is for. The swell lifts every marked vertex by up to one and a half
 * times HD_WATER_SWELL, 36 units of it UPWARDS (y counts downwards). The
 * vertices at the water's own edge carry the mark too, so without the taper
 * the surface climbs over the coping it should meet and covers it with a
 * moving band of dark water (visible on the Rome cistern). Real water goes
 * flat where it meets the land, and so does this. */
float4 gWaterBox : register(c15);

/* How far in from the shore the swell reaches its full height: one square. A
 * TR5 water quad is one square, so this is the smallest distance over which a
 * grid of them can fade at all - anything shorter would fade inside a single
 * triangle and read as a crease rather than as a shore. */
#define HD_WATER_SHORE     1024.0f

/* -------------------------------------------------- constants, pixel side --
 *
 * c4          gPass               per pass  (the same one)
 * c16         gFog                per frame
 * c17         gCounts             per room
 * c18         gAdd                per DRAW CALL
 * c19         gSpec               per DRAW CALL (.w is the see-through
 *                                 weight)
 * c20 - c35   gLights[16]         per room  (8 lights, two registers each)
 * c36         gRipple             per DRAW CALL
 * c40 - c71   gBulbs[32]          per frame (16 bulbs, two registers each)
 *
 * Deliberately disjoint from the vertex numbers although the two constant
 * files are separate in Direct3D 9: one table of "register -> meaning" that
 * needs no column saying which shader.
 */

/* x = tomb5.distance_fog * 1024, y = 255 / that. No on/off switch beside it:
 * the classic path has none either - the option is a distance in blocks,
 * 3..30 (troyestuff.cpp), and it always subtracts. */
float4 gFog : register(c16);

/* x = dynamic lights, y = fog bulbs. */
float4 gCounts : register(c17);

/* The additive pass's own constant.
 *
 * x = the weight of THIS draw call, out of the .TRG part's own material
 *     parameters (hd_render.cpp, MaterialAddWeight, which says which slot it
 *     is per material). 1 in the opaque and alpha passes, and 1 for every
 *     additive part whose material names no weight.
 * y = the game clock in seconds - gMotion.z on the pixel side.
 * z = the swing time in seconds, 0 for a part that stands still.
 * w = tomb5.hd_motion, so hd_motion 0 draws the still picture the weight
 *     alone gives.
 *
 * The swing. Two reference shots of one VCI corridor from the remaster, three
 * seconds apart, show the same light streaks standing in different places.
 * What is drawn is the smallest thing that reproduces that: the weight
 * breathes by HD_RAY_SWING about its own value, once per gAdd.z seconds, with
 * a phase taken from the surface point so that neighbouring parts of one fan
 * are out of step and the bright places travel along the shaft instead of the
 * whole room pulsing at once. The wave has the same shape as the wind -
 * (x + z) times a wavelength plus clock times a speed - as this file's idiom
 * for a slow travelling wave, not because the two are the same effect. */
float4 gAdd : register(c18);

/* How far the swing carries, either side of the part's own weight, and over
 * what distance one wave of it fits. The .TRG gives only the PERIOD, so
 * neither is read from the file; the amplitude is measured, the wavelength
 * chosen.
 *
 * The amplitude. Two remaster shots of the VCI corridor, three seconds apart,
 * over the shaft band on the far wall (108 columns, x 470 to 900, y 240 to
 * 430):
 *
 *     shot t1   band mean 124.6   column sd 17.9  =  14.3 % of the mean
 *     shot t4   band mean 130.1   column sd 21.5  =  16.5 % of the mean
 *
 * The pattern keeps its strength and the level barely moves: 4.4 percent
 * between the two, an upper bound since the camera moved as well. The
 * remaster's shafts TRAVEL; they do not brighten and dim, which is what the
 * travelling wave is for. A swing of a quarter either way reads as pulsing
 * (the ANDREA church candle glows then move by +7.8 and -9.3 percent of the
 * whole pixel).
 *
 * 0.06 is that 4.4 percent with the head-room a WEIGHT has over a lit
 * surface: the swing multiplies the additive share of a pixel and not the
 * pixel, so a part whose glow is the whole of what is on screen moves by 6
 * percent at most and one drawn over a lit wall by less. RoomProbe on RICH2
 * room 25, eight seconds at one second against the -nomotion still picture:
 * the worst pixel moves 5.4 percent (17.0 with a quarter), and the share of
 * pixels moving more than 5 percent is none (0.82 with a quarter).
 *
 * The wavelength is about three squares, so that neighbouring parts of one
 * fan are out of step. */
#define HD_RAY_SWING       0.06f
#define HD_RAY_WAVELENGTH  0.0021f

/* The gloss of a surface.
 *
 * x = the part's own strength out of its .TRG material (0 = matt, and that is
 *     every part of the game but the 183,350 triangles of materials 3 and 17),
 * y = its exponent, an integer 1..191 (read but unused, see the light loop),
 * z = tomb5.hd_reflect,
 * w = the see-through weight of the blend phase (see the end of RoomPS).
 *
 * Not a mirror: nothing here renders the room a second time, and a planar
 * reflection would need a pass, a target and a clip plane that the classic
 * path shares its depth buffer with. What is drawn is what a polished floor
 * does that a matt one does not: it gets BRIGHTER AT A GRAZING ANGLE, where a
 * surface reflects most of what is in front of it. There is no environment to
 * sample, so what is reflected is the light the surface already carries -
 * Schlick's fifth power of one minus N.V, times the material's strength. A
 * floor seen from above is unchanged; the same floor down a long hall lifts
 * towards its own lit colour.
 *
 * The two constants below are chosen, not measured: how much of the surface's
 * own light the grazing angle may lift, and the ceiling on it. */
float4 gSpec : register(c19);

#define HD_FRESNEL_LIFT    0.6f
#define HD_FRESNEL_MAX     0.9f

/* The ripple of a still water surface.
 *
 * x  how tight the waves are, in WAVES per world unit
 * y  how far the coordinate travels, in page units - 0 for every draw call
 *    that is not a material 16 water surface, and that zero is the switch:
 *    everywhere else this costs one compare
 * z  how fast, in waves a second
 *
 * All three come out of the .TRG part's own three numbers. Where those
 * numbers come from is our own measurement of the material; nothing of
 * TROne's is used - see MaterialRipple in hd_render.cpp.
 *
 * The shape is this file's own: two sheets travelling across each other, and
 * a finer, faster octave over them so that the pattern does not read as one
 * moving grid. They are written on the WORLD position (roomPos plus
 * gRoomOrigin) rather than on the UV, so a sheet built out of a handful of
 * huge triangles ripples as finely as a small basin and the pattern does not
 * slide when the camera moves.
 *
 * The wave is not a sine but the smoothstep of a triangle - abs(frac(p) * 2
 * - 1), then 3f^2 - 2f^3, then doubled and centred - a periodic curve that
 * agrees with a cosine to within 6 % of its own amplitude and has the same
 * zero mean, zero derivative at both turning points and continuous slope
 * everywhere. Four sines take this shader from 453 instruction slots to 512,
 * exactly the number shader model 3 guarantees; four of these take it to 476.
 * On a texture offset of four texels the difference is not visible.
 *
 * Nothing moves in the vertex. `sway` is 0 on 27 of the material's 28 parts,
 * so the sheet itself does not bob: a surface that bobbed would tear away
 * from the stone rim it is let into. This is a texture ripple only.
 *
 * The 1/1.5 puts the sum of the two amplitudes back on 1, so gRipple.y IS
 * the largest offset the sampler can ever be asked for - which is what makes
 * the tenth-of-a-span cap in hd_render.cpp a hard cap. */
#define HD_RIPPLE_OCTAVE   2.3f
#define HD_RIPPLE_NORM     (1.0f / 1.5f)

/* How many, and why the light loop is unrolled. ps_3_0 has no relative
 * addressing of constant registers - only vs_3_0 has. Written as a loop over
 * a run time count, fxc emulates gLights[i] with a chain of 32 cmp
 * instructions PER ACCESS: 285 slots that execute about 140 instructions per
 * light per pixel. Unrolled against a compile time maximum, with the run time
 * count as a dynamic if inside, every access is a literal register and an
 * unused slot costs a branch.
 *
 * 8 lights: the engine holds 32 dynamics (drawroom.cpp) but they are flare,
 * torch, muzzle flash and explosions, and a single room seeing more than
 * eight of them at once does not happen in TR5.
 *
 * 16 bulbs, the measured ceiling over all sixteen vanilla .TRC files: a level
 * holds up to 17 fog bulbs (ANDREA2, TITLE), a single room up to 5 (ANDY3),
 * and - over a sweep of every camera position a level allows - up to 16 of
 * them fall inside the 20480-unit circle aBuildFogBulbList keeps around the
 * camera at once (drawroom.cpp): 16 ANDREA2, 14 RICH2, 13 ANDY1, 11 TITLE, 10
 * RICH1. The classic path has the same ceiling: RoomletFogBulbs is 16 and is
 * filled with no bound check. Each bulb costs about 4.5 instruction slots in
 * each of the two pixel shaders (6 -> 310/295, 8 -> 319/304, 16 -> 355/340 of
 * the 512 ps_3_0 guarantees). The count must match hd_model.hlsl and
 * hd_render.h, which share registers c40-c71. The `clipped` line in the log
 * says when the limit bites, once per level. */
#define HD_MAX_LIGHTS 8
#define HD_MAX_BULBS  16

/* Two registers per light: (x, y, z, falloff) room local, then
 * (r, g, b, 1 / falloff). */
float4 gLights[HD_MAX_LIGHTS * 2] : register(c20);

// See the note over HD_RIPPLE_OCTAVE. (0, 0, 0, 0) for every draw call that
// is not a water surface, and the .y = 0 there is what switches it off.
float4 gRipple : register(c36);

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
	/* xyz the WaterTable term in 0..255 units, w the water surface weight of
	 * this vertex - 0 everywhere but on a marked water surface, and 0 there
	 * too while hd_motion is off. It rides in the fourth
	 * component rather than in an interpolator of its own because ps_3_0 has
	 * ten of those and this file already spends six. */
	float4 waterAdd : TEXCOORD4;
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

/* The WaterTable's spatial hash (drawroom.cpp): the world position
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

	/* 1. The choppy displacement (drawroom.cpp). The classic path
	 * moves the first nWVtx vertices of every roomlet; inFlags.x is that
	 * same set, matched by position at level build. */
	if (gWaterMode.y * inFlags.x > 0.5f)
		pos.y += gWaterTable[WaterPhase(pos + gRoomOrigin.xyz, 4.0f)].y;

	/* 2. The WaterTable colour terms (drawroom.cpp), in the classic
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

	/* 2b. The wind. The fourth SHORT of the position is TrgVertex::sway, the
	 * remaster's own per vertex weight for what moves in the wind or the
	 * current - a wind and water weight over 32767. That is the .TRG field
	 * layout as TRForge's MIT format description gives it, and the range is
	 * re-measured on the player's own files (hd_render.h, HDStats::swayMax).
	 * Over the 15 levels it is the ivy and the banners of Rome, the grass of
	 * Ireland and the kelp of the Deepsea Dive, and it is disjoint from the
	 * classic engine's water vertices except in JOBY4, where the kelp stands
	 * in the water. The classic path has NO counterpart, so the weight is the
	 * remaster's and the wave is ours.
	 *
	 * It moves `drawn`, not `pos`: everything above - the water hash, the
	 * surface test, the WaterTable - reads the position the .TRG stores, which
	 * is what the parity with the classic path is measured against. */
	float wind = inPos.w * (1.0f / 32767.0f) * gMotion.w;
	float3 drawn = pos;

	if (wind > 0.002f)
	{
		float phase = (pos.x + pos.z) * HD_WIND_WAVELENGTH + gMotion.z * HD_WIND_SPEED;

		drawn.x += wind * HD_WIND_TRAVEL * sin(phase);
		drawn.z += wind * HD_WIND_TRAVEL * 0.6f * sin(phase * 0.73f + 1.3f);
	}

	/* 2c. The swell. The same water weight the UV wobble rides on also lifts
	 * the surface - TROne's shape, one wave over about a block and a half.
	 * Like the wind it moves `drawn` and not `pos`, so the water hash, the
	 * surface test, the WaterTable and the classic choppy displacement (which
	 * is in `pos`) are untouched by it. */
	/* What moves is the surface, not the room.
	 *
	 * `shimmerOn` is the classic path's WaterTable COLOUR selector, and in a
	 * ROOM_REFLECT + ROOM_REFLECT_CEILING room it says yes to almost every
	 * vertex there is - walls, ceiling, pillars - because that is what the
	 * classic room shimmer does (drawroom.cpp). Hanging the UV wobble and the
	 * swell on it would move the WALLS (in Rome, for instance). The classic
	 * path displaces nothing but its own nWVtx set, and that set is empty in
	 * all of TR5, so no wall ever moves.
	 *
	 * Geometry and UV therefore ride on the marked set at the surface plane -
	 * gRoomOrigin.w, the room's own minfloor. In a surface-bit room that is
	 * what `shimmerOn` already is (gWaterMode.z carries the same height test);
	 * in a whole-room-bit room it is the pool and nothing else. The colour
	 * term above keeps `shimmerOn` whole, so the room shimmers exactly as the
	 * classic one does. */
	/* And only where the surface is flat.
	 *
	 * The mark is carried from the classic mesh BY POSITION, and a wall
	 * vertex standing at exactly the water's height inside the pool's box
	 * answers yes to it - the corner match and the plane flood both. With the
	 * UV wobble and the caustic, a wall triangle whose other two corners are
	 * lower becomes a sheared, darkened wedge standing on the waterline.
	 *
	 * A water surface is flat, so its normal is vertical and a wall's is not.
	 * The test is on the GEOMETRY half alone; the colour term above keeps
	 * `shimmerOn` whole, because the classic path asks the height and not the
	 * normal (drawroom.cpp). 0.6 is 53 degrees off the horizontal - a bank
	 * steep enough to be a wall fails it and no floor in the game comes near
	 * it. gFlat.w switches the gate. */
	float upright = lerp(1.0f, step(0.6f,
		abs((inNormal.y - 127.0f) * (1.0f / 128.0f))), gFlat.w);
	float surfaceOn = dot(inFlags, gSel1) * atSurface * upright;
	float water = gMotion.w * saturate(max(gWaterMode.y * inFlags.x, surfaceOn));
	float wavA = (pos.x + pos.z) * HD_WATER_WAVE + gMotion.z * HD_WATER_SPEED_A;
	float wavB = (pos.z - pos.y) * HD_WATER_WAVE + gMotion.z * HD_WATER_SPEED_B;

	/* And none of it at the shore. How far this vertex stands inside
	 * the flood box, in squares, clamped to one - see gWaterBox. It multiplies
	 * the SWELL only: the UV wobble and the caustic are a picture on the
	 * surface and may run right up to the bank, it is the geometry that must
	 * not climb it. */
	float2 inset = min(pos.xz - gWaterBox.xy, gWaterBox.zw - pos.xz);
	float shore = saturate(min(inset.x, inset.y) * (1.0f / HD_WATER_SHORE));

	if (water > 0.002f)
		drawn.y += water * shore * HD_WATER_SWELL *
			(sin(wavA) + 0.5f * cos(wavB * 1.7f + gMotion.z));

	/* 3. The transform. */
	float4 clip = mul(float4(drawn, 1.0f), gWorldViewProj);
	float3 view = mul(float4(drawn, 1.0f), gWorldView);

	/* 4. The underwater screen wobble (drawroom.cpp). A pixel offset
	 * on the projected position, so it is added in clip space scaled by w:
	 * screen x = ndc * centerx + centerx, hence d(clip.x) = dx * w / centerx,
	 * and y the other way round because screen y counts downwards.
	 *
	 * The second lookup reads the ALREADY OFFSET screen x where the first
	 * reads the view space y - not a typo, but the order the classic path
	 * writes the two lines in, and the sequence is visible in the picture. */
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

	/* 5. The UV: the texture coordinate plus two motion terms.
	 *
	 * gMotion.xy is this draw call's own scroll - the .TRG part's material 1,
	 * already turned into an offset on the processor. It is the HD twin of
	 * S_AnimateTextures' UVROT branch (output.cpp), which scrolls the water
	 * tile of ANDY3 and RICH2 vertically, and it reaches further than that
	 * branch: the remaster authored a scroll on 14 of the 15 levels, on
	 * waterfalls, drifting sky and light shafts as well.
	 *
	 * The wobble is TROne's water_effect: on a marked water surface the UV
	 * moves a little, so an HD water plane is not a mirror. It rides on the
	 * SAME two waves as the swell - one clock, one shape, so the picture and
	 * the geometry agree - six texels, TROne's own figure. It dies with
	 * tomb5.shimmer, because gSel1 does, and with hd_motion, because gMotion.w
	 * does. The caustic is in the pixel half; the note beside HD_WATER_UV says
	 * why it does not count the classic shimmer twice. */
	o.uv = float2(inC0.a, inC1.a) + gMotion.xy + water * HD_WATER_UV *
		float2(sin(wavA), cos(wavB));

	o.light = lerp(inC0.rgb * gPass.z + inC1.rgb, 1.0f, gFlat.x);
	o.viewPos = view;
	o.roomPos = pos;
	o.waterAdd = float4(waterAdd, water);

	/* The byte normal, biased by 127, into the same space the classic path
	 * puts it in - view space, by the linear part of D3DMView. It is NOT
	 * renormalised, because the classic path does not renormalise either. */
	o.viewNrm = mul(float4((inNormal.xyz - 127.0f) * (1.0f / 128.0f), 0.0f), gWorldView);

	return o;
}

/* ---------------------------------------------------------------- pixel -- */

/* One fog bulb's contribution: the length of the piece of the eye ray that
 * lies inside the bulb's sphere, times its density (drawroom.cpp).
 * All of it in view space, where both the bulb centre (aBuildFogBulbList
 * puts it there once per frame) and the surface point live.
 *
 * The three branches are the camera inside the sphere, the surface point
 * inside it, and both outside - and the last one keeps a quirk of the
 * original: when the bulb is behind the camera, or nearer than its own
 * projection, `vec` is left holding the UNIT direction and `vec2` the
 * origin, so the length comes out as 1 and the intensity as one density,
 * about 0.02. The classic path truncates that to zero on the way into a
 * long. Kept rather than tidied away, so that this function can be checked
 * against that one number for number. Two deliberate differences follow: the
 * classic cone cull is reduced to its exact part, and the rim is softened.
 */
float BulbIntensity(float3 stash, float4 a, float4 b)
{
	float3 P = a.xyz;
	float rad = a.w;
	float sqrad = rad * rad;
	float sqlen = dot(P, P);

	/* The classic path's cull, and why only part of it is kept.
	 *
	 * drawroom.cpp opens this loop with
	 *
	 *     stash.z + rad > 0 && |stash.x| - rad < |stash.z| &&
	 *                          |stash.y| - rad < |stash.z|
	 *
	 * on the VERTEX, and it is a cull, not a rule: a cone of 45 degrees
	 * about the view axis, widened by the bulb's radius, thrown in front of
	 * the expensive arithmetic below. At 4:3 the frame reaches 40 degrees to
	 * the side and the cone never crosses the picture.
	 *
	 * Wider views cross it. AlterFOV (3dmath.cpp) builds the perspective from
	 * height * 320/240 and uses that same value across, so 16:9 turns the
	 * nominal 80 into about 96 degrees wide - 48 to the side - and the first
	 * person view at 90 to 100 nominal is 107 to 118 across, 53 to 59 to the
	 * side. Past 45 the cone cuts INTO the frame, and per pixel that cut is a
	 * hard line down the side of the screen with fog on one side and none on
	 * the other, moving with the camera.
	 *
	 * A cull whose job is to skip vertices has nothing to do in a pixel
	 * shader. What is kept is the one part that is exact rather than
	 * conservative: a sphere whose every point lies behind the camera plane
	 * cannot be crossed by any ray leaving the eye forwards, so it contributes
	 * nothing. P.z + rad <= 0 says exactly that, and it also settles the
	 * camera-inside-the-bulb case on its own - a camera inside means
	 * |P| < rad, and then P.z >= -|P| > -rad. */
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

	/* The rim.
	 *
	 * `chord` above is the exact length of the piece of the eye ray inside
	 * the sphere, and exact is the trouble. For a ray that passes the centre
	 * at distance d it is 2*sqrt(rad^2 - d^2), whose DERIVATIVE against d is
	 * infinite at d = rad. So the intensity does not fade in at the
	 * silhouette, it stands up vertically out of nothing, and the fog shows
	 * the hard outline of a sphere.
	 *
	 * The classic path has the identical formula and does not show it because
	 * it evaluates it once per VERTEX and lets the rasteriser interpolate; a
	 * room vertex is up to a whole block from the next, so the cusp is
	 * smeared over a polygon. Rebuilding that here (evaluate at the three
	 * corners, interpolate) would throw away what per pixel is better at - a
	 * bulb inside a four-vertex room looks like a lit quad in the classic
	 * path.
	 *
	 * So the cusp is removed by weighting the chord with a smoothstep over the
	 * OUTER part of its own range. `t` is the chord as a fraction of the
	 * sphere's diameter, 1 through the centre and 0 at the silhouette; at
	 * t >= kRim - the whole core of the bulb - the weight is exactly 1 and
	 * the result is the classic one to the bit. Below it the sqrt becomes a
	 * curve that leaves zero with zero slope, which is what a fog bank does.
	 *
	 * kRim 0.35 is the smallest value that hides the outline at the sizes
	 * TR5 uses (bulb radii 1024 to 8192): a third of the diameter of a
	 * 4096-unit bulb is 2.7 metres of game world to fade over, the same order
	 * as the classic vertex spacing. A larger value thins the whole bulb:
	 * at 1.0 the weight is 0.5 halfway along the diameter and every bulb
	 * comes out roughly half as thick as the classic path draws it. */
	const float kRim = 0.35f;

	float chord = length(vec2 - vec);
	float t = saturate(chord / max(2.0f * rad, 1e-4f));

	return chord * b.w * smoothstep(0.0f, kRim, t);
}

float4 RoomPS(VS_OUT input) : COLOR0
{
	float2 uv = input.uv;

	/* The ripple of a still water surface - see the note over
	 * HD_RIPPLE_OCTAVE. gRipple.y is 0 for every draw call that is not one of
	 * material 16's 28 parts, so this is one compare there and nothing else.
	 * gAdd.y is the game clock in seconds and gAdd.w the
	 * hd_motion switch, exactly as the ray swing at the end of this function
	 * reads them, so a paused game and hd_motion 0 both stand still. */
	if (gRipple.y > 0.0f)
	{
		float2 w = (input.roomPos.xz + gRoomOrigin.xz) * gRipple.x;
		float t = gAdd.y * gRipple.z * gAdd.w;

		// The two sheets, then the finer and faster octave across them.
		float4 p = float4(w.x + t, w.y + 0.85f * t,
			(w.x + w.y) * HD_RIPPLE_OCTAVE - 1.7f * t,
			(w.y - w.x) * HD_RIPPLE_OCTAVE + 1.9f * t);
		float4 f = abs(frac(p) * 2.0f - 1.0f);
		float4 s = f * f * (3.0f - 2.0f * f) * 2.0f - 1.0f;

		uv += (gRipple.y * HD_RIPPLE_NORM) *
			float2(s.x + 0.5f * s.z, s.y + 0.5f * s.w);
	}

	float4 texel = tex2D(sPage, uv);

	/* The alpha test. gPass.x is the reference, and it is zero wherever
	 * there is no test to run - an alpha is never below zero, so this line
	 * then costs one subtract and discards nothing (the note on the three
	 * passes at the top says why the reference is per material). */
	clip(texel.a - gPass.x);

	/* Into the classic path's 0..255 space. */
	float3 f = input.light * 127.5f;

	/* The dynamic lights (drawroom.cpp). Torch, flare, muzzle flash,
	 * every explosion - the engine's `dynamics` array, culled against this
	 * room's bounding box on the processor exactly as aBuildRoomletLights
	 * culls against a roomlet's.
	 *
	 * The mixed spaces in the middle line are the original's: the normal is
	 * in VIEW space and the direction to the light is ROOM LOCAL, so the term
	 * turns with the camera. It is Core's own arithmetic and reproducing it
	 * is the whole point - a "corrected" dot product lights the wall
	 * differently from the roomlet next to it. */
	/* The gloss of this draw call, once, outside the loop. Zero for every
	 * part the .TRG does not call glossy, and zero with hd_reflect off. */
	float gloss = gSpec.x * gSpec.z;

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

			/* No specular highlight here. Blinn's half vector against this
			 * light raised to gSpec.y would be the second half of a gloss, but
			 * inside this loop, which ps_3_0 forces to be unrolled eight times
			 * because it has no relative constant addressing, one pow per
			 * light adds about 317 instruction slots to the 476 of this
			 * shader - far past the 512 that shader model 3 guarantees. So the
			 * gloss is the grazing angle alone, at the end of this function,
			 * where it costs eleven slots and no loop. The exponent is read,
			 * logged and unused. */
		}
	}

	/* The WaterTable term, computed per vertex above. Its fourth component is
	 * the water weight, which the caustic below reads, not this line. */
	f += input.waterAdd.xyz;

	/* The distance fog (drawroom.cpp). Linear in the view depth from
	 * tomb5.distance_fog blocks onwards, subtracted from all three channels -
	 * TR5's fog darkens, it does not blend towards a colour, and there is no
	 * fog colour anywhere in the classic path to take one from.
	 * D3DRS_FOGENABLE stays off: with a pixel shader the fixed function fog
	 * factor would have to come out of the vertex shader, and this one writes
	 * none. */
	if (input.viewPos.z > gFog.x)
		f -= (input.viewPos.z - gFog.x) * gFog.y;

	/* Back into the remaster's scale. This is the classic path's split
	 * (drawroom.cpp) with its ceiling taken off - see the long note
	 * at the top. Below f = 128 the two are the same line. */
	float3 c = f * (2.0f / 255.0f);

	/* Where the ceiling is not optional.
	 *
	 * The multiply may pass 1 in the OPAQUE pass: a surface the remaster
	 * baked at 1.5 really is meant to be brighter than its texture. In the
	 * ADDITIVE pass it may not.
	 *
	 * The classic path splits at 128 into a diffuse that stops at 255 and a
	 * SPECULAR that carries the rest, and for its additive draw type it
	 * switches the specular off (polyinsert.cpp, D3DRS_SPECULARENABLE
	 * FALSE). So a classic additive polygon is texel * 1 at the very most -
	 * the excess is dropped. Without the ceiling the multiply reaches
	 * texel * 2 and is blended ONE / ONE over a floor that is already lit,
	 * which turns light shafts, window glows and material 2's glow into flat
	 * white surfaces.
	 *
	 * Measured over the remaster's own vertices, channels of colour1+colour2
	 * above 1.0 in the ADDITIVE pass:
	 *
	 *     RICH1    56.3 % of them, mean 1.26, max 2.00
	 *              of that, material 2 (the glow) 81.6 %, max 2.00
	 *     ANDREA1  10.9 %, material 14 alone 41.6 %, max 1.47
	 *     JOBY5     1.2 %, material 1 up to 1.99
	 *
	 * And the pages carry no weight to hold it back: JOBY4's light shaft
	 * page 3255 is a flat grey with alpha 255 over the whole sheet, so the
	 * `lit *= texel.a` line below multiplies it by one. The whole of the
	 * brightness sits in this multiply, and this is where the classic path
	 * stops it.
	 *
	 * gPass.y is 1 in the additive pass and 0 in the other two, so the
	 * opaque pass keeps the deviation argued for at the top, to the bit.
	 * Before the fog bulbs, because the classic path splits before it adds
	 * them (drawroom.cpp). */
	c = lerp(c, min(c, 1.0f), gPass.y);

	float3 s = 0.0f;

	/* The fog bulbs (drawroom.cpp). These DO keep the classic split:
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

	/* The grazing angle - see the note beside gSpec.
	 * Applied to `c`, the multiply, so what the surface lifts towards is its
	 * own lit colour and never a colour of this shader's making; and after the
	 * fog bulbs, so a bulb's haze is not multiplied up with it. */
	if (gloss > 0.0f)
	{
		float3 N = normalize(input.viewNrm);
		float3 V = normalize(-input.viewPos);
		float grazing = pow(1.0f - saturate(dot(N, V)), 5.0f);

		c *= 1.0f + min(HD_FRESNEL_MAX, gloss * grazing * HD_FRESNEL_LIFT);
	}

	/* The caustic (TROne's formula, see the note beside HD_WATER_UV and
	 * THIRD-PARTY.md).
	 *
	 * Per PIXEL and out of the WORLD position: a remaster water quad has its
	 * corners and nothing between them, so a shimmer carried down the
	 * interpolators is a slow blotch however fast its clock runs. From the
	 * world position the net is fine wherever the surface is coarse, and it
	 * does not slide when the camera moves.
	 *
	 * ZERO MEAN, so it adds no brightness of its own: the sum of the two sine
	 * products averages out over any patch bigger than its own wavelength, and
	 * what is left is the movement. It is clamped at zero rather than allowed
	 * to invert the multiply.
	 *
	 * gAdd.y is the game clock in seconds on the pixel side. With hd_motion 0
	 * the weight waterAdd.w is already zero, because the vertex half
	 * multiplied it by gMotion.w. */
	if (input.waterAdd.w > 0.002f)
	{
		float3 world = input.roomPos + gRoomOrigin.xyz;
		float cx = world.x * HD_CAUSTIC_SCALE;
		float cz = world.z * HD_CAUSTIC_SCALE;
		float caustic = sin(cx + gAdd.y * 2.0f) * sin(cz + gAdd.y * 1.7f) +
			0.5f * sin((cx - cz) * 1.3f + gAdd.y * 2.4f);

		c *= max(0.0f, 1.0f + HD_CAUSTIC_WEIGHT * input.waterAdd.w * caustic);
	}

	/* texel * multiply + add - the shape of the fixed function pipeline the
	 * classic path draws through, with D3DRS_SPECULARENABLE on
	 * (function_table.cpp) except for its additive draw type
	 * (polyinsert.cpp), which is what gPass.w carries. With no fog bulb in
	 * sight `s` is zero and this is the plain texel * light. */
	float3 lit = texel.rgb * c + s * gPass.w;

	/* Additive pass only: an unweighted glow washes the whole frame out. */
	lit *= lerp(1.0f, texel.a, gPass.y);

	/* And the part's own weight, with its swing (see gAdd above). gPass.y is 1 in the additive pass and 0 in the other two,
	 * so the two surface passes multiply by exactly one and keep their
	 * picture to the bit. */
	float weight = gAdd.x;

	if (gAdd.z > 0.0f && gAdd.w > 0.0f)
		weight *= 1.0f + HD_RAY_SWING *
			sin((input.roomPos.x + input.roomPos.z) * HD_RAY_WAVELENGTH +
				gAdd.y * (6.2831853f / gAdd.z));

	lit *= lerp(1.0f, weight, gPass.y);

	/* The glass. gSpec.w is this draw call's own see-through weight -
	 * material 14's param[1], and exactly 1 for every other draw call, so
	 * the two solid passes and the additive one carry their picture
	 * through this line untouched (they do not look at alpha at all: their
	 * blend is off or ONE / ONE). In the blend phase it is the pane's
	 * opacity times the page's own alpha, which is what makes a texel at 0.45
	 * half there instead of gone. */
	return float4(lit, texel.a * gSpec.w);
}

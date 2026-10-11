/*
 * Modified 2026 by ChrisChanSan.
 *
 * The lighting rule below - room light is the SUM of the two vertex colour
 * triples - came to this tree by way of TROne and TRForge (THIRD-PARTY.md,
 * the origin ledger, entry 7), and it rests on this project's own count over
 * the fifteen levels: on the classic room vertices a .TRG vertex stands on
 * exactly, the sum puts the vertices in the order of the classic prelight
 * more often than the product or either triple alone does (TrgProbe
 * -shadeorder; the counts are in MODIFICATIONS.md and in "The light" below).
 * The alpha test living in the alpha pass ONLY and the additive pass carrying
 * its weight in the texture's alpha are carried over from TROne (Copyright
 * 2026 Todesengel, GPLv3, TROne-main (copy of 2026-10-06, commit 75c4c843),
 * the GLSL shader k_hd_frag), TROne's own design. Translated to HLSL for
 * Direct3D 9; not a line of the original is copied. See THIRD-PARTY.md.
 *
 * Everything below the line "the parity block" is this project's own: the
 * distance fog, the fog bulbs, the dynamic lights in rooms and the WaterTable
 * effects are transcriptions of specific\drawroom.cpp,
 * aRoomletTransformLight, line for line. TROne-main (copy of 2026-10-06,
 * commit 75c4c843) has a fog model, fog bulbs and room lights of its own
 * (set_fog_model, set_fog_bulbs, set_room_lights); none of them is used here.
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
 * Studio C++ toolset alone. The bytecode is checked in beside this file as
 * three C headers, and this file is their source. Preparation\Tools\Diagnose\
 * bshader.bat rebuilds them with exactly these three calls:
 *
 *     fxc /nologo /T vs_3_0 /E RoomVS /O3 /Vn kHDRoomVS /Fh hd_room_vs.inc hd_room.hlsl
 *     fxc /nologo /T ps_3_0 /E RoomPS /O3 /Vn kHDRoomPS /Fh hd_room_ps.inc hd_room.hlsl
 *     fxc /nologo /T ps_3_0 /E RoomPS /O3 /D HD_GLOSS=1 /Vn kHDRoomGlossPS
 *         /Fh hd_room_gloss_ps.inc hd_room.hlsl
 *
 * The third is the gloss variant (see "The sheen" below); every line that
 * differs between the two pixel shaders sits under #ifdef HD_GLOSS, so the
 * plain variant's bytecode is the same with or without it.
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
 * scale it needs: both bytes run over 0..255 in every one of the fifteen
 * files. That u is the fourth byte of colour1 and v the fourth byte of
 * colour2 is counted on the files themselves (TrgProbe -uvbytes): of the
 * .TRG vertices lying inside a classic face of their room, two vertices
 * are put the same way round by the classic u and by +15 in 3,157,175
 * pairs and the other way in 1,465,989, by the classic v and +19 in
 * 3,354,530 against 1,340,180, while the crossed reading (u in +19, v in
 * +15) splits even, 2,213,580 against 2,213,351 and 2,215,512 against
 * 2,249,987. D3DDECLTYPE_UBYTE4 is not normalised, so the flag bytes arrive
 * as 0.0 or 1.0 exactly as written.
 *
 * The light. light = colour1 + colour2, and the pixel is texel * light. A
 * SUM, not a product: on the 107,118 classic room vertices that a .TRG
 * vertex of the same room stands on exactly, the sum puts the classic
 * prelight's order better than the product in 14 of the 15 levels (rank
 * correlation 0.553 over all of them against 0.460; of the vertex pairs
 * the two put in opposite order, the classic shade sides with the sum in
 * 121,606 and with the product in 56,907) and better than either triple
 * alone (0.397 and 0.527). JOBY4, the sea level, orders by neither. Counted
 * with TrgProbe -shadeorder; MODIFICATIONS.md has the table per level and
 * the picture check beside it.
 *
 * The three passes are the three index runs of a .TRG part, in this order:
 *
 *   0 opaque    no alpha test, except on the three cut-out materials 6, 7
 *               and 8, whose param[0] is the reference (hd_render.cpp,
 *               MaterialAlphaRef). On every other material the alpha
 *               channel of an opaque tile carries a gloss/wetness MASK, not
 *               coverage, and testing on it blanks whole floors and facades
 *               (TROne-main, copy of 2026-10-06, commit 75c4c843,
 *               k_hd_frag, on a TR1 snow floor; ANDREA1's stucco, JOBY2's
 *               snow and JOBY3's bulkheads here).
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
 * The install's baked light covers the same total range - colour1 + colour2
 * reaches 2.0 in eleven of the fifteen files and lies above 1 in 3.3 % of
 * all their channels (TrgProbe -shadeorder) - and this shader applies it
 * as a straight MULTIPLY, its own choice.
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
 * c4          (a literal of fxc's own; gPass is read on the pixel side)
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
 * c16         gCaustic            per room   (RoomVS)
 * c17         gEye                per room   (WaterVS)
 * c18         gSwell              per room, and per sheet draw (x RoomVS
 *                                             and WaterVS, y z w WaterVS
 *                                             alone)
 * c19         gWaterView          per sheet draw (WaterVS alone)
 * c20 - c23   gWaveV[4]           per room, and per sheet draw (RoomVS and
 *                                             WaterVS)
 * c24 - c27   gWaveV2[4]          per sheet draw (WaterVS alone)
 * c32 - c95   gWaterTable[64]     per room, only when the row changes
 * c96 - c127  gWibbleTable[32]    per level (it never changes)
 */
float4x4 gWorldViewProj : register(c0);

/* x = alpha test on/off, y = additive weight on/off, z = not read here,
 * w = specular (overbright) on/off.
 *
 * Read by the pixel shader only. The vertex shader reads none of it, so fxc
 * gives c4 of the vertex file to a literal of its own, and hd_render.cpp
 * writes the register to the pixel file alone.
 *
 * hd_model.hlsl shares this register and shares x, y and w with it; its z
 * switches the first person head cut, and hd_model.cpp writes all four
 * components to the PIXEL constant file before every model pass, so the z a
 * room pass leaves behind never reaches a model. */
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
 * shape is TROne's own (TROne-main, copy of 2026-09-05, k_hd_vert's water
 * block: a sine of x + z, a second one across it); 24 units of travel and a
 * wavelength of about 1.4 blocks is what that shader used for its own water
 * in that copy (TROne dropped that wave since, for the classic WaterTable:
 * TROne-main, copy of 2026-10-06, commit 75c4c843), and grass, ivy and kelp
 * are the same size of thing. */
#define HD_WIND_TRAVEL     24.0f
#define HD_WIND_WAVELENGTH 0.0045f
#define HD_WIND_SPEED      2.1f

/* The water, this project's own design.
 *
 * What moves water in this file comes from one function of the world
 * position and the game clock, WaveSum below: four travelling sines, each
 * with its own direction, wave number, angular speed and height. The rows
 * are per room and per class of body (basin, channel, sea), worked out on
 * the processor (hd_render.cpp, "the water sheets"): the wavelengths,
 * heights and directions are chosen per class, the speeds follow deep-water
 * dispersion, omega = calm * sqrt(g k) with g = 4300 units/s^2 (9.81 m/s^2
 * at 2.3 mm a unit), and a channel adds its current. The sheet sums eight
 * such rows with a slow envelope on each, turned into a fan of directions
 * and broken into short crests placed by a hash (WaveSum8, CrestRows);
 * the remaster's marked art keeps the four straight rows.
 *
 *   the swell     a marked surface rises by the class's share of that
 *                 height, tapered at the shore - a sea and a channel only
 *   the wobble    the texture of a surface the remaster drew moves with the
 *                 slope, as refraction would move it
 *   the normal    WaterPS reads the slope per pixel (see "the sheet")
 *
 * The caustic is a zero-mean modulation of a surface's own light: thin
 * bright lines where two domain-warped waves cancel (CausticOf), with no
 * average brightness of its own, on the submerged floor and walls of a pool
 * (gCaustic) and on a surface the remaster drew. Its mean (0.2310) and
 * spread (0.2964), counted over 400,000 samples of the curve, are divided
 * out, so HD_CAUSTIC_FLOOR and HD_CAUSTIC_SURFACE are the spread in units of
 * the light. The classic WaterTable term stays untouched: all of this is on
 * top of it.
 *
 * HD_CAUSTIC_FOOT: a bright line is a median 25.5 units across at half its
 * height (counted along x and z on 400 lines of the curve), so where a pixel
 * spans more than 10 units the lines fade out, gone at 20, instead of
 * aliasing into stripes at a grazing view.
 *
 * The other four numbers are free choices, set by eye on the RoomProbe
 * pictures of the pools of ANDREA2 and JOBY2:
 *
 *   HD_WATER_UV         the wobble, 4/512 of a page per unit of slope
 *   HD_CAUSTIC_SCALE    0.0224 radians a unit, so one cell of the net is
 *                       2 pi / 0.0224 = 280 units across, a little over a
 *                       quarter of a square
 *   HD_CAUSTIC_FLOOR    the spread on a submerged floor or wall, 0.30
 *   HD_CAUSTIC_SURFACE  on a surface the remaster drew, 0.6 of the floor's,
 *                       0.18 */
#define HD_WATER_UV        (4.0f / 512.0f)	// page units per unit of slope
#define HD_CAUSTIC_SCALE   0.0224f		// radians per unit: a cell of 280
#define HD_CAUSTIC_MEAN    0.2310f
#define HD_CAUSTIC_SD      0.2964f
#define HD_CAUSTIC_FLOOR   0.30f
#define HD_CAUSTIC_SURFACE 0.18f
#define HD_CAUSTIC_FOOT    10.0f		// units a pixel, see above

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
 * What it is for. The swell moves every marked vertex by up to the class's
 * whole wave height, 64 units in a sea, up and down (y counts downwards). The
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

/* The water's vertex constants, per room. c16 - c23 are free in the vertex
 * file; the pixel file's c16 - c23 are the fog and the lights, which is why
 * the pixel half reads the same rows out of its own registers (c92 - c103).
 *
 * gCaustic   x the submerged caustic's weight in this room (1 in an
 *            underwater room with a surface above it, times hd_motion),
 *            y the height of that surface (room local), z one over the
 *            depth it fades out over, w unused
 * gEye       xyz the camera in this room's own coordinates (WaterVS)
 * gSwell     x the share of the wave height the vertex takes (0 basin;
 *            for a sheet draw times the body's own swell, hd_render.cpp
 *            kWaterBank), y the shore distance the swell tapers in
 *            over - read by WaterVS alone, RoomVS tapers over its flood box
 *            (HD_WATER_SHORE); for a sheet draw alone z the most a
 *            breaking crest rises by beside the swell (0 without
 *            whitecaps) and w the height above which a crest breaks, both
 *            in units; for a room draw z is 1 where a sheet carries the
 *            room's water and its art is hidden (RoomVS, "the flood under
 *            a sheet"), else 0, and w stays 0 and is not read
 * gWaveV     the four wave rows (kx, kz, omega, height): wave vector in
 *            radians a unit, angular speed in radians a second, height in
 *            units - the room's largest body's, or the basin's in a room
 *            with no water square of its own
 * gWaveV2    for a sheet draw alone: the rows five to eight of its body;
 *            the sheet's draw writes all eight rows in the phase form
 *            (kx, kz, phase, height) - see WaveSum8
 * gWaterView for a sheet draw alone: x, y half the render target's width
 *            and height in pixels, for the rows' fade (WaterVS); zw how far
 *            the bank foam's patches have drifted (PatchField), in units.
 *            Declared, so fxc keeps its own literals out of c19; RoomVS
 *            never reads it, and a literal another shader keeps there is
 *            its own while that shader is bound */
float4 gCaustic : register(c16);
float4 gEye : register(c17);
float4 gSwell : register(c18);
float4 gWaterView : register(c19);
float4 gWaveV[4] : register(c20);
float4 gWaveV2[4] : register(c24);

/* The crests for WaterVS's swell, per sheet draw: the same eleven
 * registers as the pixel side's c149 - c159 (gCrestA, gCrestB, gCrest,
 * gCrestP, gCrestH), here c168 - c178 - above every vertex register of
 * this file (the wibble table ends at c127) and of hd_model.hlsl (its
 * last, gSoft, ends at c166) -, but for gCrestV.y, the pieces' share times
 * the swell's own gain (hd_render.cpp kCrestSwellGain): the same pieces,
 * risen and sunk no further than their straight rows. */
float4 gCrestVA[3] : register(c168);
float4 gCrestVB[3] : register(c171);
float4 gCrestV : register(c174);
float4 gCrestVP[2] : register(c175);
float4 gCrestVH[2] : register(c177);

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
 * c134 - c136 gUnderSigma, gUnderLight, gUnderUp   per frame (the eye
 *                                 under the water; not in the gloss
 *                                 variant, see "under the water")
 * c145        gUnderClear         per frame (the same, its clear zone)
 *
 * and in the gloss variant alone (compiled with HD_GLOSS, see "The sheen"
 * below):
 *
 * c36         gGloss              per DRAW CALL (in gRipple's place)
 * c86 - c88   gGlossAxes[3]       per room
 * c89 - c91   gGlossField[3]      per room
 * s1, s2      the light field     per level (the two volume textures)
 *
 * and in WaterPS alone (the water sheet, see "the sheet" below), all
 * thirty-two uploaded before every sheet draw:
 *
 * c92 - c95   gWaveP[4]           per DRAW CALL
 * c96 - c103  gWaterDeep .. gWaterMisc   per DRAW CALL
 * c104 - c106 gWaterAbsorb, gWaterScreen, gWaterStage   per DRAW CALL
 * c107 - c110 gWaveP2[4]          per DRAW CALL
 * c111, c112  gWaterLag, gWaterLife                     per DRAW CALL
 * c113 - c120 gWaterRings[8]      per DRAW CALL
 * c121, c122  gWaveK[2]           per DRAW CALL
 * c148        gWaterClip          per DRAW CALL (read by the depth variants)
 * s5         the copy of the frame behind the sheet   per frame
 * s6          the frame's depth texture (the variant compiled with
 *             HD_WATER_DEPTH alone)                     per frame
 *
 * Deliberately disjoint from the vertex numbers although the two constant
 * files are separate in Direct3D 9: one table of "register -> meaning" that
 * needs no column saying which shader. The bulbs, the gloss registers and
 * the water registers are the exceptions - the vertex file's water table
 * holds c32 - c95 and its wibble table c96 - c127, and the water's own
 * vertex rows take c16 - c27, where the pixel file keeps the fog and the
 * lights - and they are pixel registers only (the water's vertex rows
 * vertex registers only). The gloss registers from c86 on lie above every
 * register a named constant of the plain variant or of the glow shader
 * (c77 - c79) takes, and the sheet's from c92 on above the gloss ones.
 *
 * gGloss takes c36 because the gloss variant never ripples, and
 * hd_render.cpp writes c36 on every draw call it changes on: glossy draw
 * calls write gGloss there, and the next plain draw call writes its ripple
 * back. A literal fxc keeps in a `def` register belongs to its own shader -
 * Direct3D 9 applies it while that shader is bound and keeps the value set
 * through SetPixelShaderConstantF for the other - so neither variant can
 * overwrite a literal of the other; hd_render.cpp still switches the
 * variants before a draw call's writes, as a precaution.
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
 *     and in the gloss variant the draw call's resolved strength - the
 *     material's, or its page's (hd_render.cpp, ResolveSheen),
 * y = its exponent, an integer 1..191 (read but unused by the plain variant,
 *     see the light loop; the gloss variant's Blinn-Phong exponent),
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
 * All three come out of the .TRG part's own three numbers. The reading of
 * param[1] as the travel is our own measurement of the material; which of
 * param[0] and param[2] is the frequency and which the rate is this
 * project's free choice, and it coincides with the order TROne's prose
 * names - see MaterialRipple in hd_render.cpp for both. No number of
 * TROne's is used. The structure - a texture ripple with no movement in the
 * vertex, its travel held to the part's own UV span - is TROne's own design
 * (Copyright 2026 Todesengel, GPLv3, TROne-main, copy of 2026-10-06,
 * commit 75c4c843).
 *
 * The curve is this file's own: two sheets travelling across each other, and
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
 * each of the two pixel shaders, room/model (6 -> 431/303, 8 -> 440/312,
 * 16 -> 476/348 of the 512 ps_3_0 guarantees; fxc 10.0.26100).
 * The count must match hd_model.hlsl and hd_render.h, which share registers
 * c40-c71. The `clipped` line in the log says when the limit bites, once per
 * level. */
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

/* The sheet's pixel constants, per draw - read by WaterPS alone; the plain
 * and gloss variants never name them. Above the gloss registers (c86 - c91).
 *
 * gWaveP      c92 - c95, the sheet's first four rows, in WaveSum8's phase
 *             form
 * gWaterDeep  rgb the deep colour, lit (palette x body light); w one over
 *             the absorption length in units
 * gWaterBase  rgb the shallow colour, lit the same way; w the scale of the
 *             foam's blotch pattern
 * gWaterRefl  rgb what the surface reflects at the horizon (the sky's
 *             measured colour out of doors, the body's light indoors); w F0
 * gWaterZen   rgb the same overhead; w the light the distance fog is
 *             measured against: the body's light, the mean of its three
 *             channels, or 1 for a body that reflects the sky
 * gWaterHigh  rgb the highlight colour times its strength; w the exponent
 * gWaterSun   xyz toward the highlight's light (room axes, y down);
 *             w the whitecap threshold, 0..1 of the crest height
 * gWaterFoam  rgb the foam colour, lit; w the shore foam strength
 * gWaterMisc  x the foam band in units, y the whitecap strength, z the
 *             clock in seconds (0 with hd_motion off), w the whole wave
 *             height now, the eight rows' heights summed, in units, at
 *             least 1 (for the crest test) */
float4 gWaveP[4] : register(c92);
float4 gWaterDeep : register(c96);
float4 gWaterBase : register(c97);
float4 gWaterRefl : register(c98);
float4 gWaterZen : register(c99);
float4 gWaterHigh : register(c100);
float4 gWaterSun : register(c101);
float4 gWaterFoam : register(c102);
float4 gWaterMisc : register(c103);

/* What the sheet does with the frame behind it, read by WaterPS alone:
 *
 * gWaterAbsorb  rgb one over the absorption length per channel (units),
 *               w how far the wave's slope moves the floor seen through
 *               it, in units at a slope of 1
 * gWaterScreen  x, y one over the render target's width and height in
 *               pixels; z, w the projection's a and b, the depth a - b / z
 *               of HDRenderMakeProjection
 * gWaterStage   x 1 while this frame's copy is bound to s5, z RoomProbe's
 *               view of the depth texture (0 off, 1 the scene's view depth
 *               over 16384 units, 2 the water depth out of the depth
 *               texture over the vertex depth, times 0.5, 3 the pixels
 *               whose bent place the depth refused, red, 4 the depth under
 *               the surface at rest over 64 units, 5 the bank foam's
 *               patches, green), y the slope's gain at
 *               eye level less one (the class's, see "the body at eye
 *               level" in WaterPS), w the footprint's segments in use (0
 *               to 8), read by FootPS alone
 *
 * Above the pixel registers of every other shader of the blend phase; the
 * shadow receive shaders' c103 - c108 are written by every one of their
 * passes, and hd_model.hlsl's c104 and up are vertex registers. */
float4 gWaterAbsorb : register(c104);
float4 gWaterScreen : register(c105);
float4 gWaterStage : register(c106);

/* gWaterClip c148, per sheet draw, read by WaterPS alone: x how far under
 * the surface at rest the frame's depth may lie and still be left to the
 * frame, units (the body's clip, hd_render.cpp kWaterClip; see "the clip"
 * in WaterPS); y, z, w 0. Named by no other shader: this file's other
 * pixel registers end at c147, hd_model.hlsl's own begin at c160. */
float4 gWaterClip : register(c148);

/* The crests (see CrestRows), per sheet draw, read by WaterPS (its foam
 * variant too) and RiverPS:
 *
 * gCrestA     c149 - c151, rows one to four, one row a component: x the
 *             step along the crest in cells a unit along world x; y the
 *             same along z; z the number the row's cells across are
 *             counted from (0 to 4095)
 * gCrestB     c152 - c154, the same for rows five to eight
 * gCrest      c155: x 1 - C, y C times the pieces' gain, z C (C the
 *             pieces' share), w the squeeze's push in units a unit of the
 *             longest row's wave vector (WaveBend)
 * gCrestP     c156, c157, the phases of rows one to four and five to
 *             eight, one row a component (the rows' z, gWaveP and gWaveP2)
 * gCrestH     c158, c159, their heights (the rows' w)
 *
 * Named by no other shader: hd_model.hlsl's pixel registers begin at
 * c160. */
float4 gCrestA[3] : register(c149);
float4 gCrestB[3] : register(c152);
float4 gCrest : register(c155);
float4 gCrestP[2] : register(c156);
float4 gCrestH[2] : register(c158);

/* What keeps the sheet alive, read by WaterPS alone:
 *
 * gWaveP2      c107 - c110, the rows five to eight (all eight rows of a
 *              sheet draw are in the phase form, see WaveSum8)
 * gWaterLag    the two longest rows 0.5 s earlier: x, y their phases then,
 *              z, w their heights then - where a crest was (the whitecaps)
 * gWaterLife   x the swing of the stand-in's pattern about its mean, y the
 *              share of its finer second pattern, z the rings in use (0 to
 *              8), w the foam's relief times 0.45 over the light a flat
 *              layer of foam gets, 0.55 + 0.45 times the light's
 *              elevation sine (see "the foam's body" in WaterPS)
 * gWaterRings  c113 - c120, one ring a register: x, z its centre (world),
 *              its front's radius and its strength - see RingSlope
 * gWaveK       c121, c122, the eight rows' wave numbers, radians a unit -
 *              the lengths of their (kx, kz); the rows' fade is
 *              WaterVS's
 * gWaterLara   c123, where the waterline gives way to Lara: x, y her world
 *              x and z in the frame of the sheet's world position, z the
 *              radius without a waterline, w one over the width it comes
 *              back over; x, y far away (1e7) when she is not in the
 *              water (see "the foam" in WaterPS); z 1e9 in the draw over
 *              the pixels the creatures' mark took (hd_render.cpp), where
 *              no waterline is then drawn at all
 * gFootSeg     c124 - c131, read by FootPS alone: one segment of a
 *              waterfall's footprint a register, x1, z1, x2, z2 its ends in
 *              the frame of the sheet's world position (see FootPS)
 *
 * c132 and c133, gRiverFlow and gRiverEdge, are read by RiverPS alone and
 * declared with it ("the river"); c134 - c136, the eye under the water,
 * by every shader that applies it ("under the water"); c137 - c144,
 * gFootReach, by FootPS alone ("the waterfall's foot"); c146 and c147,
 * gRiverNight and gRiverLight, by RiverPS alone ("the river"). */
float4 gWaveP2[4] : register(c107);
float4 gWaterLag : register(c111);
float4 gWaterLife : register(c112);
float4 gWaterRings[8] : register(c113);
float4 gWaveK[2] : register(c121);
float4 gWaterLara : register(c123);
float4 gFootSeg[8] : register(c124);

/* The frame behind the sheet, copied out of the render target before the
 * blend phase (HDRenderWaterCopy): linear, clamped, one level. The frame's
 * depth texture, where the adapter offers one (INTZ, HDRenderWaterDepth-
 * Bind): point, clamped, bound with the depth writes off. s0 is the page,
 * s1 and s2 the light field, s3 and s4 the shadow maps. */
sampler2D sWaterCopy : register(s5);
#ifdef HD_WATER_DEPTH
sampler2D sWaterDepth : register(s6);
#endif

#ifdef HD_GLOSS
/* The sheen - the gloss variant of this shader.
 *
 * Compiled a second time with /D HD_GLOSS into hd_room_gloss_ps.inc, with a
 * budget of 512 instruction slots of its own, and set by hd_render.cpp for
 * the draw calls whose surface resolves to a gloss above zero while
 * tomb5.hd_reflect is on; every other draw call keeps the plain variant, whose
 * bytecode the define leaves untouched.
 *
 * The shine belongs to the place. What the sheen of a pixel reads is the
 * pixel's world position, its normal, its page's gloss and the eye - nothing
 * that is chosen per draw call or per room - so two draw calls that meet on
 * one plane, two rooms that meet at a doorway and two rooms whose art lies on
 * the same plane carry the same shine at the same place, and no light rides
 * on the camera. The world position and the world normal come out of the
 * view-space ones the vertex half already hands down (viewPos, viewNrm),
 * through gGlossAxes and gGlossField; the light comes out of the level's
 * light field (hd_gloss_field.h), two volume textures over the whole level.
 * Three textbook terms per pixel:
 *
 *   the highlight   Blinn-Phong: pow(N.H, n) times N.L, n the surface's
 *                   exponent, H the half vector between the direction to the
 *                   eye and the field's direction at this place - a light
 *                   probe's dominant direction, the weighted mean of the
 *                   directions to the lamps in reach - in the field's colour
 *                   there, times how one-sided the light is there (two equal
 *                   lamps on either side give a weak highlight, not two).
 *                   On metal the highlight takes the texel's hue.
 *   the environment the surface's own baked light, which the art keeps
 *                   continuous from room to room, times HD_GLOSS_ENV - on
 *                   metal coloured by the texel, on stone and ice not.
 *   the weight      Schlick's approximation of Fresnel,
 *                   F0 + (1 - F0) * (1 - N.V)^5, with F0 per class: high for
 *                   metal, low for marble and ice, so metal reflects face on
 *                   and stone mostly at a grazing angle.
 *
 * The bound. The sum is held under HD_GLOSS_BOUND times the largest channel
 * of the surface's own lit colour (the texel times its light, after the fog
 * and the bulbs) plus HD_GLOSS_FLOOR, and under HD_GLOSS_CAP whatever the
 * surface - softly, B (1 - e^(-t / B)) for its largest channel t, and the
 * other two scaled with it so that it keeps its hue: a surface the bake left
 * dark stays dark, a lamp behind a wall cannot light it, and Fresnel's rise
 * at a grazing angle cannot wash a wall seen along its length pale. And it
 * is composed as a screen, lit + sheen * (1 - lit), which with the cap below
 * one leaves every surface short of white that was not white before.
 *
 * The sum is ADDED to the surface's own lit colour (as that screen), never
 * blended over it: the surface keeps its picture to the bit wherever the
 * weight is zero, which is what a page's gloss mask asks for - a stucco
 * facade whose mask is black stays exactly as it was and only its window
 * panes shine. The same fog that darkens the surface darkens the sum. Not a
 * mirror: nothing is drawn twice, and no image of the room appears in it.
 *
 * gGloss       x F0, y how far the sheen takes the texel's colour (1 for
 *              metal, 0 for stone and ice), z 1 where the page's alpha is a
 *              gloss mask and 0 where the page is flat, w 0 - or, for
 *              RoomProbe's -glossfield picture alone, 1 to draw the field's
 *              colour and 2 to draw its direction in place of the surface.
 *              gSpec (c19) keeps the strength in x, the exponent in y and
 *              hd_reflect in z.
 * gGlossAxes   the rows that turn a view-space direction into world axes,
 *              the transpose of the room's view rotation.
 * gGlossField  the rows that turn a view-space position (x, y, z, 1) into
 *              the field's texture coordinate: the room's view transform
 *              undone, the room's origin added, the field's corner taken
 *              off and the field's size divided out.
 *
 * The numbers are chosen, not measured, against RoomProbe's pictures of
 * JOBY2, ANDREA1 and RICH1: the highlight at six times the field's colour -
 * the field is bounded below one and the lobe carries no (n + 8) / 8 of its
 * own, the bound holds the peak - the environment at 0.35 of the surface's
 * light, and the bound at two and a half times the surface's own lit colour
 * plus 8 of 255, never above three quarters. A highlight at twice the
 * field's colour under a bound of the surface's own lit colour is hardly
 * seen on a dark steel floor - it adds 23 of 255 at its brightest place in
 * JOBY2 room 90, and 60 with these numbers; the environment is kept low
 * because it lifts a whole surface evenly, which reads as a paler picture
 * and not as a shine. */
#define HD_GLOSS_HIGHLIGHT 6.0f
#define HD_GLOSS_ENV       0.35f
#define HD_GLOSS_BOUND     2.5f
#define HD_GLOSS_FLOOR     (8.0f / 255.0f)
#define HD_GLOSS_CAP       0.75f

float4 gGloss : register(c36);
float4 gGlossAxes[3] : register(c86);
float4 gGlossField[3] : register(c89);

/* The light field: the direction towards the light biased into bytes with
 * the one-sidedness in alpha, and the colour (its strength in alpha is for
 * the tools; this file reads the colour alone). Linear filtering, clamped at
 * the edges - hd_render.cpp sets both. */
sampler3D sFieldDir : register(s1);
sampler3D sFieldRgb : register(s2);
#endif

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

/* A periodic curve of period 2 pi that agrees with a cosine to within 2 % of
 * its amplitude - the smoothstep of a triangle, the ripple's own curve (see
 * HD_RIPPLE_OCTAVE) - two at a time: about six slots where two sines cost
 * sixteen. */
float2 Wave2(float2 x)
{
	const float2 f = abs(frac(x * 0.15915494f) * 2.0f - 1.0f);

	return f * f * (3.0f - 2.0f * f) * 2.0f - 1.0f;
}

/* The water's one wave: four travelling sines, `w` the four rows (kx, kz,
 * omega, height). Returns the height (units, UP positive - y counts down in
 * this engine) and, in `slope`, the slope of the unbent wave along x and z:
 * the bend below moves the crests and is left out of the slope, so the
 * normal leans a little off the drawn shape (no Jacobian of the bend). */
float WaveSum(float2 p, float t, float4 w0, float4 w1, float4 w2, float4 w3,
	out float2 slope)
{
	/* The crests are bent: the position is pushed about by a quarter of the
	 * longest wavelength, along a slow pattern of its own, so that four
	 * straight sines do not read as a corrugated sheet from above. The slope
	 * leaves the push out - the bend is in the shape, not in the tilt. */
	const float k0 = length(w0.xy);
	const float2 warp = Wave2(p.yx * (0.21f * k0) +
		t * float2(0.23f, -0.19f));

	p += warp * (1.6f / max(k0, 1e-4f));

	float4 ph = float4(dot(p, w0.xy), dot(p, w1.xy), dot(p, w2.xy),
		dot(p, w3.xy)) - float4(w0.z, w1.z, w2.z, w3.z) * t;
	float4 s, c;

	sincos(ph, s, c);

	const float4 h = float4(w0.w, w1.w, w2.w, w3.w);
	const float4 hc = h * c;

	slope = hc.x * w0.xy + hc.y * w1.xy + hc.z * w2.xy + hc.w * w3.xy;
	return dot(h, s);
}

/* The cells' hash: two numbers in [0, 1) for every pair of whole numbers
 * (i, j), four pairs at a time - the fractional parts of two sums of
 * products of the indices, crossed (a common graphics idiom for randomness
 * without a table). The multipliers are whole numbers over 4096, exact in
 * single precision, so the hash repeats after exactly 4096 cells either
 * way and nowhere before; its callers give it cell numbers, never places,
 * so the products keep their precision anywhere in a level (no sine of a
 * large argument). The numbers used are the fractional parts of 16, 256
 * and 4096 times the first and of 16 times the second: counted on 128 x
 * 128 cells at the origin and round (900, 1460) and (4000, 6000) - more
 * than a level asks: 65,536 units are 533 cells of a channel's shortest
 * row either way, and the crests' count adds at most 4095 -, each spreads
 * evenly over 64 bins (chi square 41 to 82 on 63 degrees of freedom), its
 * neighbours correlate by under 0.03, its power spectrum is flat (the
 * largest bin 8 to 14 times the mean, as for white noise), and the four
 * correlate with each other by under 0.08. 3.71, 7.13, 5.37 and 9.11 are
 * free choices that passed that count. */
float4 CellHash(float4 i, float4 j, out float4 second)
{
	const float4 a = frac(i * (422.0f / 4096.0f) + j * (1531.0f / 4096.0f));
	const float4 b = frac(j * (481.0f / 4096.0f) + i * (965.0f / 4096.0f));

	second = frac(b * (a + 5.37f) * 9.11f + a);
	return frac(a * (b + 3.71f) * 7.13f + b);
}

/* The squeeze: the place is pushed to and fro along the longest row's
 * direction - `push` units a unit of its wave vector `k0` (k0 times push,
 * at most kBendPush of that wavelength, hd_render.cpp) - along a value
 * noise: the hash at the four corners of a square cell, blended by the
 * smoothstep of the place within it, over cells HD_BEND_CELL of the
 * longest wavelength wide, so its crests crowd and part, and never the
 * same way twice: the noise has no period short of the hash's 4096 cells.
 * It drifts with the clock by HD_BEND_DRIFT cells a second. One way and
 * one octave, about 25 slots: a second way cost 7 more than the sheet's
 * variants have. 1.5, the drift and the push are free choices, set by eye
 * on the round's RoomProbe pictures. RoomVS's WaveSum keeps its own slow
 * bend. */
#define HD_BEND_CELL 1.5f
#define HD_BEND_DRIFT float2(0.031f, -0.023f)
float2 WaveBend(float2 p, float t, float2 k0, float push)
{
	const float2 q = p * (length(k0) * (0.15915494f / HD_BEND_CELL)) +
		t * HD_BEND_DRIFT;
	const float2 f = frac(q);
	const float2 c = q - f;
	float4 g;
	const float4 h = frac(CellHash(c.xxxx + float4(0.0f, 1.0f, 0.0f, 1.0f),
		c.yyyy + float4(0.0f, 0.0f, 1.0f, 1.0f), g) * 16.0f);
	const float2 s = f * f * (3.0f - 2.0f * f);
	const float2 e = lerp(h.xz, h.yw, s.x);

	return p + k0 * ((lerp(e.x, e.y, s.y) * 2.0f - 1.0f) * push);
}

/* The crests' pieces (hd_render.cpp, "the crests" over kWaterFan), four
 * rows at a time. A row's crests are cut into cells. Across the row a cell
 * holds HD_CREST_ACROSS crests, from trough to trough, and is numbered by
 * the crests' own count: `m` adds the whole cells the processor took off
 * the row's phase, so a crest keeps its cell while it travels. Along the
 * crest a cell is crestLen of the row's wavelengths long (`cx`, `cz` the
 * step in cells a unit along world x and z), and every other cell across
 * is moved along by half a cell, so neighbouring cells stand staggered
 * like bricks. The hash of a cell's two numbers (CellHash) gives its
 * piece - a short train of crests:
 *
 *   a height from 0.4 to 1 of the gain;
 *   a length from 1 to 1 / 2 of the cell (1.5 to 3 wavelengths at a cell
 *   of 3) at a place within it: its middle within (n - 1) / 4 of the
 *   cell's centre (n the cell over the length), never more than
 *   (1 - 1 / n) / 2 for n up to 2, so the piece stays in its cell;
 *   a lean of up to HD_CREST_LEAN turns a cell either way - the crests'
 *   line sheared along the piece, up to 15 degrees off the row at a cell
 *   of 3 wavelengths (atan(0.8 / 3)) - and a shift across the crests of
 *   up to HD_CREST_SHIFT of a turn either way.
 *
 * Its shape is the row's curve (Wave2's, 2 q - 1 of the sheared place)
 * under two windows, each the smoothstep of a tent - along the piece (wa)
 * and across the cell (wc) -, both 0 at the cell's edges with their
 * slopes; the shear is taken under the same windows, so at the cell's
 * edges the phase is the row's own and the surface is smooth everywhere.
 * Across, the window weighs the middle crest of the three fully and its
 * two neighbours by a quarter: a crest with its weaker fellows, not a lone
 * bump. The height is h S (1 - C + C G A wa wc), C the pieces' share
 * (`shape` x 1 - C, y C G, z C, G the gain hd_render.cpp kCrestGain), and
 * the slope is exact - `fk` and `fc` its factors of k and of the step
 * along the crest: the windows' rises (the smoothstep's 6 f (1 - f) times
 * the tent's slope) and the shear's. With C 0 these are the straight rows.
 * `lean` false leaves out the lean and the shift - in RiverPS, the views'
 * and the underside's variants, for their slots, and in the foam's draw,
 * which reads the height for its whitecaps and swash and took 0.02 ms
 * more with it over a screen of sea (RoomProbe, 2560 x 1440). The window
 * across one crest alone drew each piece as a round bump - from above a
 * pitted sheet; a sixth of the cells empty left flat stretches with single
 * dimples. 3, the lengths, the heights, the lean, the shift and the
 * bricks' half cell are free choices, set by eye on the round's RoomProbe
 * pictures. */
#define HD_CREST_ACROSS 3.0f
#define HD_CREST_LEAN 0.8f
#define HD_CREST_SHIFT 0.1f
// Which of WaterPS's variants lean their pieces (see `lean` above).
#if defined(HD_WATER_VIEW) || defined(HD_WATER_UNDER)
#define HD_CREST_LEAN_ON false
#elif defined(HD_WATER_FOAM)
#define HD_CREST_LEAN_ON false
#else
#define HD_CREST_LEAN_ON true
#endif
float CrestRows(float2 p, float4 pa, float4 hgt, float4 cx, float4 cz,
	float4 m, float3 shape, bool lean, out float4 fk, out float4 fc)
{
	const float4 v = pa * 0.15915494f + 0.25f;
	const float4 vk = v * (1.0f / HD_CREST_ACROSS);
	const float4 x = frac(vk);
	const float4 j = vk - x + m;
	const float4 along = p.x * cx + p.y * cz + frac(j * 0.5f);
	const float4 a = frac(along);
	float4 g;
	const float4 h = CellHash(along - a, j, g);
	const float4 r1 = frac(h * 16.0f);
	const float4 r2 = frac(h * 256.0f);
	const float4 r3 = frac(h * 4096.0f) - 0.5f;
	const float4 n2 = 2.0f + 2.0f * r2;
	const float4 am = a - 0.5f - r3 * r2 * 0.5f;
	const float4 u = am * n2;
	const float4 fa = saturate(1.0f - abs(u));
	const float4 ux = x * 2.0f - 1.0f;
	const float4 fx = 1.0f - abs(ux);
	const float4 wc = fx * fx * (3.0f - 2.0f * fx);
	const float4 dwc = -12.0f * fx * ux * (1.0f / HD_CREST_ACROSS);
	const float4 wa = fa * fa * (3.0f - 2.0f * fa);
	const float4 dwa = -6.0f * n2 * fa * u;
	const float4 A = (0.4f + 0.6f * r1) * shape.y;
	const float4 E = shape.x + A * wa * wc;
	float4 vs = v;
	float4 dk = 1.0f;
	float4 dc = 0.0f;

	if (lean)
	{
		const float4 sigma = (frac(g * 16.0f) - 0.5f) *
			(2.0f * HD_CREST_LEAN * shape.z);
		const float4 J = r3 * (2.0f * HD_CREST_SHIFT * shape.z) + sigma * am;
		const float4 W = wa * wc;

		vs += J * W;
		dk += J * wa * dwc;
		dc = J * dwa * wc + sigma * W;
	}

	const float4 us = frac(vs) * 2.0f - 1.0f;
	const float4 fs = 1.0f - abs(us);
	const float4 S = 4.0f * fs * fs * (1.5f - fs) - 1.0f;
	const float4 dS = -24.0f * fs * us * hgt * E;

	fk = (dS * dk + hgt * S * A * wa * dwc) * 0.15915494f;
	fc = dS * dc + hgt * S * A * dwa * wc;
	return dot(hgt * E, S);
}

/* The sheet's wave: eight travelling waves, `a` the four rows of WaveSum's
 * and `b` four more, all in the phase form (kx, kz, phase, height), of
 * which the wave vectors are read here; the phases and the heights come
 * four at a time, one row a component (`pha`, `phb`, `ha`, `hb`: gCrestP
 * and gCrestH, the heights times the rows' fade), which spares gathering
 * them. The phase is omega t plus the row's own offset, wrapped to three
 * turns - one cell of crests across, see CrestRows - on the processor in
 * double precision, and the height carries the row's slow envelope
 * (hd_render.cpp, water_rows_now), so the sum breathes and does not repeat.
 * `p` comes bent by WaveBend, so that the whitecaps can read the same
 * place; the slope is that of the unbent sum. The rows' curve is Wave2's
 * (within 2 % of a cosine's amplitude), broken into the crests' pieces
 * (CrestRows; `ca`, `cb` the step along the crest in x and z and the
 * cells' number offset of rows one to four and five to eight, gCrestA and
 * gCrestB, `shape` gCrest.xyz); the slope is exact for the pieces as drawn:
 * the factors CrestRows returns times k and times the step. */
float WaveSum8(float2 p, float4 a0, float4 a1, float4 a2, float4 a3,
	float4 b0, float4 b1, float4 b2, float4 b3, float4 pha, float4 phb,
	float4 ha, float4 hb, float4 ca0, float4 ca1, float4 ca2, float4 cb0,
	float4 cb1, float4 cb2, float3 shape, bool lean, out float2 slope)
{
	const float4 pa = float4(dot(p, a0.xy), dot(p, a1.xy), dot(p, a2.xy),
		dot(p, a3.xy)) - pha;
	const float4 pb = float4(dot(p, b0.xy), dot(p, b1.xy), dot(p, b2.xy),
		dot(p, b3.xy)) - phb;
	float4 ka, la, kb, lb;
	const float h = CrestRows(p, pa, ha, ca0, ca1, ca2, shape, lean, ka,
		la) + CrestRows(p, pb, hb, cb0, cb1, cb2, shape, lean, kb, lb);

	slope = ka.x * a0.xy + ka.y * a1.xy + ka.z * a2.xy + ka.w * a3.xy +
		kb.x * b0.xy + kb.y * b1.xy + kb.z * b2.xy + kb.w * b3.xy +
		float2(dot(la, ca0) + dot(lb, cb0), dot(la, ca1) + dot(lb, cb1));
	return h;
}

/* The caustic: thin bright lines where two domain-warped waves cancel, as a
 * zero-mean number of unit spread (see the note over HD_CAUSTIC_SCALE). */
float CausticOf(float2 xz, float t)
{
	const float2 q = xz * HD_CAUSTIC_SCALE;
	const float2 warp = Wave2(float2(q.y * 0.83f + 1.1f * t,
		q.x * 0.77f - 0.8f * t));
	const float2 ab = Wave2(float2(q.x + 0.6f * warp.x + 0.9f * t,
		q.y * 0.91f - 0.55f * warp.y - 0.7f * t));
	const float r = 1.0f - abs(ab.x + ab.y) * 0.5f;
	const float r2 = r * r;

	return (r2 * r2 * r2 - HD_CAUSTIC_MEAN) * (1.0f / HD_CAUSTIC_SD);
}

/* The caustic on a face of any slope. CausticOf is a pattern of the
 * horizontal plane: a wall reads it along one axis alone and gets vertical
 * streaks. So the pattern is projected along the face normal's major axis
 * - the triplanar projection of texturing, in its one-sample form: the
 * horizontal plane (x, z) where the normal's vertical component is the
 * largest, else the wall's own plane, (z, y) or (x, y), the one it faces.
 * The normal is the face's, the cross product of the position's screen
 * derivatives `fx`, `fy`, so it needs no interpolator; a face is flat, so
 * the plane never changes inside one, and a ramp takes the plane nearer to
 * it. One sample and not a blend of three: a second CausticOf costs about
 * twenty of the 512 slots the room and glow shaders stand near, and the
 * coordinates themselves are never blended - a blend of two coordinate
 * frames raises the pattern's rate of change with the distance from the
 * origin. Zero mean and of unit spread on every face. */
float CausticFace(float3 world, float3 fx, float3 fy, float t)
{
	const float3 n = abs(cross(fx, fy));
	const float2 wall = n.x > n.z ? world.zy : world.xy;

	return CausticOf(n.y >= max(n.x, n.z) ? world.xz : wall, t);
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
	 * current - a wind and water weight over 32767. The field's place is
	 * TRForge's MIT format description (Trg.h); its range is counted on the
	 * files themselves: 0..32767 in thirteen of the fifteen levels, never
	 * below 0, and none at all in RICH1 and RICHCUT2 (TrgProbe -normals), so
	 * over 32767 it is a weight of 0..1. The engine keeps each group's largest
	 * in HDRenderGroupMotion::swayMax (hd_render.h), which RoomProbe's
	 * table of what moves prints.
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
	 * the surface, by the room's own wave (WaveSum, the rows of its class).
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
	/* The flood under a sheet. Where a water sheet carries the room's water
	 * and hides the remaster's art (gSwell.z 1, hd_render.cpp set_room_rows),
	 * what the flood still finds at the water's height is the bank - the
	 * land's own vertices at the surface plane inside the flood box, ANDY3's
	 * pond banks and JOBY5's deck among them (RoomProbe, the round's
	 * pictures) - and the swell, the wobble and the surface caustic would
	 * tear it. So none of the three is laid on there; the colour shimmer
	 * keeps `shimmerOn` as the classic path does. 0 in every room without a
	 * sheet and with tomb5.hd_water 0: nothing changes there. */
	float water = gMotion.w * saturate(max(gWaterMode.y * inFlags.x,
		surfaceOn)) * (1.0f - gSwell.z);
	/* The wave at this vertex - see WaveSum, with the rows of this room's
	 * class (gWaveV). World position, so neighbouring rooms meet. */
	float2 slope = 0.0f;
	float height = 0.0f;

	if (water > 0.002f)
		height = WaveSum(pos.xz + gRoomOrigin.xz, gMotion.z, gWaveV[0],
			gWaveV[1], gWaveV[2], gWaveV[3], slope);

	/* And none of it at the shore. How far this vertex stands inside
	 * the flood box, in squares, clamped to one - see gWaterBox. It multiplies
	 * the SWELL only: the UV wobble and the caustic are a picture on the
	 * surface and may run right up to the bank, it is the geometry that must
	 * not climb it. */
	float2 inset = min(pos.xz - gWaterBox.xy, gWaterBox.zw - pos.xz);
	float shore = saturate(min(inset.x, inset.y) * (1.0f / HD_WATER_SHORE));

	// Up is -y. gSwell.x is the class's share of the height (0 in a basin).
	drawn.y -= water * shore * gSwell.x * height;

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
	 * The wobble: on a marked water surface the UV moves with the slope of
	 * the room's own wave, HD_WATER_UV page units per unit of slope, so an
	 * HD water plane is not a mirror. One clock and one shape with the
	 * swell, so the picture and the geometry agree. It dies with
	 * tomb5.shimmer, because gSel1 does, and with hd_motion, because gMotion.w
	 * does. The caustic is in the pixel half; the note beside HD_WATER_UV says
	 * why it does not count the classic shimmer twice. */
	o.uv = float2(inC0.a, inC1.a) + gMotion.xy + water * HD_WATER_UV * slope;

	o.light = lerp(inC0.rgb + inC1.rgb, 1.0f, gFlat.x);
	o.viewPos = view;
	o.roomPos = pos;
	/* Under the water: the floor and the walls of a pool carry the caustic,
	 * fading out with the depth below the surface (gCaustic, the room's own:
	 * zero everywhere but in an underwater room with a surface above). */
	const float below = pos.y - gCaustic.y;
	const float submerged = gCaustic.x * step(0.0f, below) *
		saturate(1.0f - below * gCaustic.z);

	o.waterAdd = float4(waterAdd,
		max(water * (HD_CAUSTIC_SURFACE / HD_CAUSTIC_FLOOR), submerged));

	/* The byte normal, biased by 127 - read so, 53.9 % of the triangle
	 * corners of the fifteen files lie within 10 degrees of their triangle's
	 * normal, read as signed bytes 0.5 % (TrgProbe -normals) - into the same
	 * space the classic path puts it in - view space, by the linear part of
	 * D3DMView. It is NOT renormalised, because the classic path does not
	 * renormalise either. */
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

#ifdef HD_GLOSS
/* The sheen of one pixel - see "The sheen" above. `light` is the surface's
 * own lit multiply before the fog, the environment's colour; `own` the
 * surface's lit colour after the fog and the bulbs, which bounds the sum;
 * `dark` what the fog and the fog bulbs took off the surface, which the sum
 * loses too. `field` comes back as RoomProbe's -glossfield picture asks
 * for it: the field's colour (gGloss.w 1) or its direction (2). */
float3 GlossSheen(VS_OUT input, float4 texel, float3 light, float3 own, float dark,
	out float3 field)
{
	/* viewNrm is not renormalised and a vertex may carry a zero normal; the
	 * floor under the length keeps such a pixel free of a NaN. */
	float3 n = input.viewNrm * rsqrt(max(dot(input.viewNrm, input.viewNrm), 1e-8f));
	float3 v = normalize(-input.viewPos);
	float3 N = float3(dot(n, gGlossAxes[0].xyz), dot(n, gGlossAxes[1].xyz),
		dot(n, gGlossAxes[2].xyz));
	float3 V = float3(dot(v, gGlossAxes[0].xyz), dot(v, gGlossAxes[1].xyz),
		dot(v, gGlossAxes[2].xyz));
	float nv = dot(N, V);

	/* A two-sided range (the cut-out materials) shows its back face as well;
	 * the normal of the side seen faces the eye. */
	N = nv < 0.0f ? -N : N;
	nv = abs(nv);

	// The light field at this place - a position of the level, not of a room.
	const float4 at = float4(input.viewPos, 1.0f);
	const float3 uvw = float3(dot(at, gGlossField[0]), dot(at, gGlossField[1]),
		dot(at, gGlossField[2]));
	const float4 toward = tex3D(sFieldDir, uvw);
	const float4 colour = tex3D(sFieldRgb, uvw);
	float3 L = toward.xyz * 2.0f - 1.0f;
	const float len = length(L);

	L *= 1.0f / max(len, 1e-3f);

	// L = -V leaves no half vector; N.L is zero there already.
	const float3 LV = L + V;
	const float3 H = LV * rsqrt(max(dot(LV, LV), 1e-8f));
	const float lobe = pow(saturate(dot(N, H)), gSpec.y) * saturate(dot(N, L));
	const float peak = max(texel.r, max(texel.g, texel.b));
	const float3 hue = lerp(1.0f, texel.rgb / max(peak, 1.0f / 16.0f), gGloss.y);
	const float3 hl = colour.rgb * hue *
		(toward.a * saturate(len) * lobe * HD_GLOSS_HIGHLIGHT);

	field = gGloss.w > 1.5f ? L * 0.5f + 0.5f : colour.rgb * toward.a;

	// The environment: the surface's own light.
	const float3 env = light * lerp(1.0f, texel.rgb, gGloss.y) * HD_GLOSS_ENV;

	// Schlick's Fresnel, the strength, the switch and the page's mask.
	float grazing = 1.0f - nv;
	float g2 = grazing * grazing;
	float F = gGloss.x + (1.0f - gGloss.x) * (g2 * g2 * grazing);
	float weight = F * gSpec.x * gSpec.z * lerp(1.0f, texel.a, gGloss.z);
	float3 sheen = max(0.0f, (hl + env) * weight - dark);

	/* The bound - see "The sheen" above: the brightest channel t goes to
	 * B (1 - e^(-t / B)), which follows t where it is small and never
	 * reaches B, and the other two follow it in proportion. */
	const float bound = min(HD_GLOSS_BOUND * max(own.r, max(own.g, own.b)) + HD_GLOSS_FLOOR,
		HD_GLOSS_CAP);
	const float k = max(sheen.r, max(sheen.g, sheen.b)) / bound;

	return sheen * ((1.0f - exp(-k)) / max(k, 1e-4f));
}
#endif

/* -------------------------------------------------- under the water -- */

/* Seen from inside a body of water, what lies further away fades toward the
 * water's own colour - the textbook extinction and in-scattering of a
 * participating medium, per channel:
 *
 *     col = col exp(-L sigma_rgb) + scatter (1 - exp(-L sigma_s))
 *
 * L the length of the eye ray that runs in the water: the whole distance d
 * to a point under the surface, and d D / h to a point above it, D the eye's
 * depth under the surface and h the point's height over the eye along world
 * up - one expression for both, d saturate(D / h) - less the clear zone
 * round the eye (gUnderClear.x), never under 0: what lies within it keeps
 * its own colour, so a figure close to the camera never changes it. The
 * far takes the water's colour; brighter where the ray looks up, toward the
 * light that comes in from above (gUnderLight.w times the ray's upward
 * cosine), darker where it looks down.
 *
 * The numbers are the sheet's own (hd_render.cpp, "under the water"):
 * sigma_rgb its per-channel absorption, sigma_s its one-over-the-absorption
 * length, scatter its shallow colour lit by the body's light - so the water
 * looks the same from inside as from above it.
 *
 * gUnderSigma  rgb sigma per channel, w sigma_s, both per unit
 * gUnderLight  rgb the scattered colour times its largest share of the
 *              mix, w the gain of the upward look
 * gUnderUp     xyz world up (-y) in view space, w D in units
 * gUnderClear  x the clear zone round the eye in units
 *
 * Per frame, written by HDRenderFrameBegin; all thirteen zero unless the eye
 * is in a water room of a level with sheets - and with them zero the path is
 * 0, the transmission exactly 1 and the scatter 0, so the colour comes out
 * to the bit as it went in. c145 and not c137: the waterfall's foot writes
 * c137 - c144 per draw, and nothing writes c145 but the frame. In RoomPS,
 * GlowPS and ModelPS (hd_model.hlsl holds the twin) and the sheet's
 * HD_WATER_UNDER variant; the gloss variant has no slots left for it, and
 * while the eye is under the water its faces draw with RoomPS
 * (hd_render.cpp, "under the water"). `share` is 1 where the surface
 * scatters the water's light toward the eye and 0 in the additive pass, a
 * glow, which the water only dims. */
float4 gUnderSigma : register(c134);
float4 gUnderLight : register(c135);
float4 gUnderUp : register(c136);
float4 gUnderClear : register(c145);

float3 UnderWater(float3 col, float3 viewPos, float share)
{
	const float d = length(viewPos);
	const float h = dot(viewPos, gUnderUp.xyz);
	const float path = max(d * saturate(gUnderUp.w / max(h, 1.0e-3f)) -
		gUnderClear.x, 0.0f);
	const float S = 1.0f - exp(-path * gUnderSigma.w);
	const float up = max(1.0f + gUnderLight.w * h / max(d, 1.0f), 0.0f);

	return col * exp(-path * gUnderSigma.rgb) + gUnderLight.rgb * (S *
		share * up);
}

float4 RoomPS(VS_OUT input) : COLOR0
{
	float2 uv = input.uv;

#ifndef HD_GLOSS
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
#endif
	/* The gloss variant draws no water surface: hd_render.cpp never hands it
	 * a draw call that ripples, and the ripple's slots go to the sheen. */

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
#ifndef HD_GLOSS
	float gloss = gSpec.x * gSpec.z;
#endif

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
			 * plain variant's gloss is the grazing angle alone, at the end of
			 * this function, where it costs eleven slots and no loop, and its
			 * exponent is read, logged and unused. The gloss variant has its
			 * own 512 slots and spends them on the light field (see "The
			 * sheen"); these dynamic lights give no highlight there either,
			 * for the same count. */
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
#ifdef HD_GLOSS
	/* The same subtraction, kept as a number: the sheen loses it too. And the
	 * light before it, which the sheen's environment term reflects. */
	const float3 unfogged = f * (2.0f / 255.0f);
	const float fogged = max(0.0f, input.viewPos.z - gFog.x) * gFog.y;

	f -= fogged;
#else
	/* Under the water the extinction at the end of this function takes the
	 * fog's place: its start moves out past any view (gUnderSigma.w times
	 * 1e30, a distance no eye ray reaches; plus exactly 0 when the eye is
	 * not under water). */
	const float fogFrom = gFog.x + gUnderSigma.w * 1.0e30f;

	if (input.viewPos.z > fogFrom)
		f -= (input.viewPos.z - fogFrom) * gFog.y;
#endif

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
#ifndef HD_GLOSS
	c = lerp(c, min(c, 1.0f), gPass.y);
#endif
	/* The gloss variant never draws the additive pass, so the ceiling is
	 * always the opaque one there and the line drops out. */

	float3 s = 0.0f;
#ifdef HD_GLOSS
	float haze = 0.0f;
#endif

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
#ifdef HD_GLOSS
		haze += intensity;
#endif
	}

	c = max(c, 0.0f);
	s = saturate(s);

#ifndef HD_GLOSS
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
#endif
	/* The gloss variant draws the sheen in place of that lift, further down,
	 * once the caustic has moved the surface's own light. */

	/* The caustic (CausticOf, this project's own - see the note beside
	 * HD_CAUSTIC_SCALE).
	 *
	 * Per PIXEL and out of the WORLD position: a remaster water quad has its
	 * corners and nothing between them, so a shimmer carried down the
	 * interpolators is a slow blotch however fast its clock runs - which is
	 * why a per-pixel term exists at all. That observation is TROne's
	 * (Copyright 2026 Todesengel, GPLv3): its own comment reported it about
	 * TR4 water in its copy of 2026-09-05, and in TROne-main (copy of
	 * 2026-10-06, commit 75c4c843) its basin note says the same of a ripple
	 * (THIRD-PARTY.md, the wind row). From the world position the net is
	 * fine wherever the surface is coarse, and it does not slide when the
	 * camera moves.
	 *
	 * Zero mean, so it adds no brightness of its own: the curve's own mean
	 * is taken off, and what is left is the movement. It is clamped at zero
	 * rather than allowed to invert the multiply, and it fades out where a
	 * pixel spans more than HD_CAUSTIC_FOOT units - the footprint is taken
	 * before the branch, where the gradient is defined.
	 *
	 * gAdd.y is the game clock in seconds on the pixel side. With hd_motion 0
	 * the weight waterAdd.w is already zero, because the vertex half
	 * multiplied it by gMotion.w.
	 *
	 * The plain variant projects it by the face (CausticFace), so a wall
	 * under the water gets the net and not streaks, and its footprint is
	 * the pixel's step in all three axes, which on a floor is the step in
	 * x and z it always was. The gloss variant keeps the horizontal plane:
	 * it stands at 502 of the 512 slots and draws the sheen ranges, the
	 * polished floors. */
#ifdef HD_GLOSS
	const float2 footX = ddx(input.roomPos.xz);
	const float2 footY = ddy(input.roomPos.xz);
#else
	const float3 footX = ddx(input.roomPos);
	const float3 footY = ddy(input.roomPos);
#endif
	const float sharp = saturate(2.0f - sqrt(max(dot(footX, footX),
		dot(footY, footY))) * (1.0f / HD_CAUSTIC_FOOT));

	if (input.waterAdd.w > 0.002f)
	{
		float3 world = input.roomPos + gRoomOrigin.xyz;
#ifdef HD_GLOSS
		c *= max(0.0f, 1.0f + HD_CAUSTIC_FLOOR * input.waterAdd.w * sharp *
			CausticOf(world.xz, gAdd.y));
#else
		c *= max(0.0f, 1.0f + HD_CAUSTIC_FLOOR * input.waterAdd.w * sharp *
			CausticFace(world, footX, footY, gAdd.y));
#endif
	}

	/* texel * multiply + add - the shape of the fixed function pipeline the
	 * classic path draws through, with D3DRS_SPECULARENABLE on
	 * (function_table.cpp) except for its additive draw type
	 * (polyinsert.cpp), which is what gPass.w carries. With no fog bulb in
	 * sight `s` is zero and this is the plain texel * light. */
	float3 lit = texel.rgb * c + s * gPass.w;

#ifdef HD_GLOSS
	/* The sheen - see "The sheen" above: bounded by the surface's own lit
	 * colour `texel * c`, then composed as a screen. The fog in the
	 * remaster's scale and the bulbs' haze in the classic 0..255 one come
	 * off it as the surface lost them. The two additive-pass lines below
	 * have no work in the gloss variant. */
	float3 field;
	const float3 sheen = GlossSheen(input, texel, unfogged, texel.rgb * c,
		fogged * (2.0f / 255.0f) + haze * (1.0f / 255.0f), field);

	lit += sheen * saturate(1.0f - lit);

	// RoomProbe's -glossfield picture: the field in place of the surface.
	lit = gGloss.w > 0.5f ? field : lit;
#else
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

	// Under the water (see above): a glow is dimmed, a surface scatters.
	lit = UnderWater(lit, input.viewPos, 1.0f - gPass.y);
#endif

	/* The glass. gSpec.w is this draw call's own see-through weight -
	 * material 14's param[1], and exactly 1 for every other draw call, so
	 * the two solid passes and the additive one carry their picture
	 * through this line untouched (they do not look at alpha at all: their
	 * blend is off or ONE / ONE). In the blend phase it is the pane's
	 * opacity times the page's own alpha, which is what makes a texel at 0.45
	 * half there instead of gone. */
	return float4(lit, texel.a * gSpec.w);
}

/* ---------------------------------------------------------- the sheet -- */

/* The water sheet: this project's own water surface over the water squares
 * of the classic level (hd_render.h, HDWaterRoom, says which). A grid of
 * 256-unit quads at the classic surface height, drawn in the blend phase of
 * its room, two-sided, with premultiplied alpha (ONE / INVSRCALPHA) and no
 * depth write, over the pool floor the opaque pass has already drawn.
 *
 * The vertex is the room vertex with its bytes read differently: colour1's
 * red is the depth down to the floor below in steps of 32 units, its green
 * the distance to the nearest square with no water in steps of 16, its blue
 * a river's way along its flow to the first point with no water, in steps
 * of 16 - all three counted on the classic floor data at level build.
 * Nothing else is read.
 *
 * Per pixel, five textbook terms - none of them a picture of anything:
 *
 *   the normal      the slope of WaveSum8 (the eight rows of the body's
 *                   class) and of the rings of the game's own ripples
 *   the reflection  Schlick's Fresnel, F0 + (1 - F0)(1 - N.V)^5 with
 *                   F0 = 0.02 for water, weighting what the reflected ray
 *                   would see: the horizon colour to the overhead one by
 *                   how far it climbs (the sky's measured colours out of
 *                   doors, the body's light indoors)
 *   the highlight   Blinn-Phong toward the class's light direction
 *   the water       what comes through from the floor beneath, exp(-path /
 *                   absorption) with the path the depth over the cosine of
 *                   the view, and the scattered colour in its place,
 *                   shallow to deep: a view from above sees the floor, a
 *                   flat one the reflection
 *   the foam        a density at the shore that runs up and draws back,
 *                   in patches along the bank that come and go, a
 *                   pattern that drifts, grows and dissolves; and on
 *                   the crests of a sea, trailing them - in a draw of
 *                   its own over the sheet (HD_WATER_FOAM, "the foam's
 *                   draw" below)
 *
 * Premultiplied: rgb is what the water adds, alpha how much of the floor it
 * hides, 1 - (1 - F) T. Without the frame's copy (gWaterStage.x 0) the floor
 * is dimmed by that one scalar. With it the sheet reads the floor itself, at
 * a place moved by the wave's slope, dims it per channel and writes the whole
 * pixel with alpha 1 - see "the frame behind the sheet" below.
 *
 * Seen from below (the camera under water), the surface shows the world
 * above through it, clear within the critical angle and under a light veil
 * of the shallow colour outside it.
 * The distance fog darkens the water as it darkens a room: a room loses the
 * fog from its light, and is black where the fog reaches that light; the
 * water, whose colours are the body's light times the palette, loses the
 * same share of its colour - the fog over the body's light (gWaterZen.w) -
 * and goes black where its banks do. The fog bulbs and the dynamic lights
 * do not light the sheet. */
struct WATER_OUT
{
	float4 pos     : POSITION;
	float4 roomPos : TEXCOORD0;	// xyz room local (as drawn), w depth
	float4 eye     : TEXCOORD1;	// xyz toward the eye, w shore distance
	float4 viewZ   : TEXCOORD2;	// x the view depth, y the swell's lift,
					// z the variety field, w the patches'
	float4 keepA   : TEXCOORD3;	// the rows' fade, rows one to four
	float4 keepB   : TEXCOORD4;	// and five to eight
	float4 flow    : TEXCOORD5;	// x a river's way to its end, units
};

/* A smooth field of 0..1 over the world that never repeats within a level:
 * two Wave2 layers whose periods (about 6,700 and 8,900 units) stand in no
 * simple ratio. It gives every stretch of a bank its own offset and swash
 * phase of the foam and its own phase of the patches - a field rather than
 * a number per square, which would jump at every square's edge and draw a
 * seam across the foam. Worked out per vertex: one every 256 units carries
 * a field this slow. */
float WaterVariety(float2 xz)
{
	const float2 v = Wave2(xz * float2(0.00094f, 0.00071f) +
		float2(1.3f, 0.4f));

	return saturate(0.5f + 0.25f * (v.x + v.y));
}

/* The bank foam's patches: where along a bank the foam is at all. Three
 * layers of cells, each the product of two Wave2 along axes turned by 0.35,
 * 1.25 and 2.20 radians (so that no layer lines up with the classic grid's
 * banks), of periods 3,800, 5,400 and 7,400 units; each layer times a clock
 * of its own, of 26.3, 33.7 and 45.9 seconds, whose phase turns once, twice
 * and three times over the variety field and a quarter turn with the next
 * layer's cell - so every stretch of a level and neighbouring cells keep
 * different times. A cell grows in place while its clock swings up, stands
 * and dissolves, and the cells of the other sign take over: a patch here,
 * then one elsewhere. No two of the periods stand in a simple ratio, and
 * the layers' sum never returns to one pattern. The sum over 1.5 lies about
 * -1..1; the pixel shader takes the patches where it stands over
 * HD_PATCH_FROM (WaterPS, the foam). Read at the place less the drift of a
 * channel's current (gWaterView.zw). Periods, turns and clocks are free
 * choices, set by eye against the reference pictures supplied for this
 * project; what they give is counted in the note over HD_PATCH_FROM. */
float PatchField(float2 p, float t, float var)
{
	const float2 q = p - gWaterView.zw;
	const float2 a = Wave2(float2(dot(q, float2(0.00155324f, 0.00056697f)),
		dot(q, float2(-0.00056697f, 0.00155324f))) +
		var * float2(2.3f, -1.7f));
	const float2 b = Wave2(float2(dot(q, float2(0.00036690f, 0.00110419f)),
		dot(q, float2(-0.00110419f, 0.00036690f))) +
		var * float2(2.3f, -1.7f));
	const float2 c = Wave2(float2(dot(q, float2(-0.00049969f, 0.00068648f)),
		dot(q, float2(-0.00068648f, -0.00049969f))) +
		var * float2(2.3f, -1.7f));
	const float3 cell = float3(a.x * a.y, b.x * b.y, c.x * c.y);
	const float3 phase = t * float3(0.2389059f, 0.1864446f, 0.1368888f) +
		6.2831853f * (var * float3(1.0f, 2.0f, 3.0f) + 0.25f * cell.yzx);
	const float2 clockA = Wave2(phase.xy);
	const float2 clockB = Wave2(phase.zz);

	return dot(cell, float3(clockA, clockB.x)) * (1.0f / 1.5f);
}

/* A patch is where the field stands over HD_PATCH_FROM; its strength, 0 to
 * 1, is the excess over HD_PATCH_SPAN, and the band reaches 1.5 times the
 * square root of it into the water (WaterPS): nothing at a patch's ends,
 * rising like a half circle where the field crosses the threshold, so a
 * patch is a rounded lobe, and the further the field stands over it the
 * further the lobe reaches. Counted on the same arithmetic over eight bank
 * lines of 24,000 units for ten minutes (a script of this project, steps
 * of 32 units and half a second): foam on 0.45 of a bank's length at any
 * moment; a patch 320 / 864 / 2,080 units long and living 11 / 25 / 68
 * seconds (the tenth, half and ninth tenth of all; two patches that touch
 * count as one); where a patch is, the band reaching 0.49 / 1.17 / 1.5 of
 * its width, a whole width or more on 0.28 of the bank. The threshold, the
 * span and the 1.5 are free choices, set by eye against the reference
 * pictures supplied for this project: foam on parts of a bank, in lobes of
 * different sizes, bare bank between. */
#define HD_PATCH_FROM 0.05f
#define HD_PATCH_SPAN 0.4f

/* The change of the frame's depth under the surface per pixel above which
 * the waterline is whole (WaterPS, "the line where a shape meets the
 * water"), in units: a free choice, set by eye on the round's RoomProbe
 * pictures. */
#define HD_LINE_STEEP 6.0f

WATER_OUT WaterVS(float4 inPos : POSITION, float4 inNormal : NORMAL,
	float4 inC0 : COLOR0, float4 inC1 : COLOR1, float4 inFlags : TEXCOORD0)
{
	WATER_OUT o;

	float3 pos = inPos.xyz;
	const float depth = inC0.r * (255.0f * 32.0f);
	const float shore = inC0.g * (255.0f * 16.0f);
	float2 slope;
	const float h = WaveSum8(WaveBend(pos.xz + gRoomOrigin.xz, gMotion.z,
		gWaveV[0].xy, gCrestV.w), gWaveV[0], gWaveV[1], gWaveV[2],
		gWaveV[3], gWaveV2[0], gWaveV2[1], gWaveV2[2], gWaveV2[3],
		gCrestVP[0], gCrestVP[1], gCrestVH[0], gCrestVH[1], gCrestVA[0],
		gCrestVA[1], gCrestVA[2], gCrestVB[0], gCrestVB[1], gCrestVB[2],
		gCrestV.xyz, true, slope);
	const float taper = saturate(shore / max(gSwell.y, 1.0f));
	/* A crest that breaks stands higher than the sum of the sines: where
	 * the wave stands above the whitecaps' threshold (gSwell.w, the
	 * pixel shader's crest test in units) the vertex rises by up to
	 * gSwell.z more, reached a quarter of the whole height above the
	 * threshold - so the foam on a crest rides a crest. 0 without
	 * whitecaps. */
	const float crest = gSwell.z * saturate((h - gSwell.w) /
		max(gSwell.z, 1.0e-3f));
	// Units the vertex rises by (y counts down): the pixel shader measures
	// the frame's depth from the surface at rest (WaterPS, the clip).
	const float lift = (gSwell.x * h + crest) * taper * gMotion.w;

	pos.y -= lift;

	float4 clip = mul(float4(pos, 1.0f), gWorldViewProj);
	const float3 view = mul(float4(pos, 1.0f), gWorldView);

	/* The rows' fade. A row whose phase moves by more than an eighth of a
	 * turn from one pixel to the next fades out, gone at a quarter, instead
	 * of aliasing into stripes - measured along each row's own direction:
	 * the phase step to the next pixel right and the next pixel down, the
	 * row's wave vector against the world step of one pixel each way. Seen
	 * flat, a pixel spans far more water in depth than across, and a row
	 * whose crests run toward the eye keeps its phase across the pixel; a
	 * fade by the pixel's longest step alone would take every row from
	 * water seen at eye level and leave it flat. The world step comes from
	 * the projection's derivative at this vertex (a row of gWorldViewProj
	 * is the clip position's change per unit of x or z) inverted, in
	 * pixels through gWaterView; it varies slowly over a square and is
	 * carried to the pixel shader per vertex. */
	const float iw = 1.0f / max(clip.w, 1.0f);
	const float2 at = clip.xy * iw;
	const float2 perX = (gWorldViewProj[0].xy - at * gWorldViewProj[0].w) *
		iw * gWaterView.xy;
	const float2 perZ = (gWorldViewProj[2].xy - at * gWorldViewProj[2].w) *
		iw * gWaterView.xy;
	const float det = perX.x * perZ.y - perZ.x * perX.y;
	const float inv = det / (det * det + 1.0e-12f);
	const float2 right = float2(perZ.y, -perX.y) * inv;
	const float2 down = float2(-perZ.x, perX.x) * inv;
	const float4 kxa = float4(gWaveV[0].x, gWaveV[1].x, gWaveV[2].x,
		gWaveV[3].x);
	const float4 kza = float4(gWaveV[0].y, gWaveV[1].y, gWaveV[2].y,
		gWaveV[3].y);
	const float4 kxb = float4(gWaveV2[0].x, gWaveV2[1].x, gWaveV2[2].x,
		gWaveV2[3].x);
	const float4 kzb = float4(gWaveV2[0].y, gWaveV2[1].y, gWaveV2[2].y,
		gWaveV2[3].y);

	o.keepA = saturate(2.0f - 1.3f * max(abs(kxa * right.x + kza * right.y),
		abs(kxa * down.x + kza * down.y)));
	o.keepB = saturate(2.0f - 1.3f * max(abs(kxb * right.x + kzb * right.y),
		abs(kxb * down.x + kzb * down.y)));

	// The underwater screen wobble, as RoomVS draws it (drawroom.cpp).
	if (gScreen.w > 0.5f && clip.w > 1.0f)
	{
		float invw = 1.0f / clip.w;
		float zv2 = 512.0f * invw;

		float dx = gWibbleTable[(int)WrapIndex(ShiftDown(gScreen.z +
			trunc(zv2 * view.y), 8.0f), 32.0f)].x;
		float sx = clip.x * invw * gScreen.x + gScreen.x + dx;
		float dy = gWibbleTable[(int)WrapIndex(ShiftDown(gScreen.z +
			trunc(zv2 * sx), 8.0f), 32.0f)].x;

		clip.x += dx * clip.w / gScreen.x;
		clip.y -= dy * clip.w / gScreen.y;
	}

	o.pos = clip;
	o.roomPos = float4(pos, depth);
	o.eye = float4(gEye.xyz - pos, shore);
	// The variety and the patches at the surface's place, on the clock the
	// pixel shader's foam runs on (still while tomb5.hd_motion is off).
	const float2 at0 = inPos.xz + gRoomOrigin.xz;
	const float var = WaterVariety(at0);

	o.viewZ = float4(view.z, lift, var,
		PatchField(at0, gMotion.z * gMotion.w, var));
	// A river's way ahead to its end, colour1's blue in steps of 16 units
	// (RiverPS, "the river"); 4080 on every other body.
	o.flow = float4(inC0.b * (255.0f * 16.0f), 0.0f, 0.0f, 0.0f);
	return o;
}

/* The frame behind the sheet: the classic and HD opaque frame, Lara and the
 * objects included, as the blend phase found it.
 *
 * The refraction. A wave tilts the surface, and the ray to the floor bends
 * with it: for a slope s the floor point seen moves by about s times the
 * path through the water times 1 - 1 / 1.33 along the slope. The sheet
 * moves it by the slope times gWaterAbsorb.w times what the water hides
 * there (1 - T), with gWaterAbsorb.w the absorption length times
 * 1 - 1 / 1.33: for a short path 1 - T is the path over that length, and
 * the move is the refracted one; for a long path it stops growing where the
 * floor fades out anyway. The move is carried to the screen through the
 * pixel's own footprint, the world step of one pixel right and one pixel
 * down: solving that 2 x 2 system turns world units into pixels, which
 * shrinks the move with the view distance - far water does not swim - and
 * turns it with the view. Two bounds, free choices set by eye on the
 * round's RoomProbe pictures: 256 units (a quarter square) in the world,
 * against a wave scaled up by hd_water.txt, and 4 % of the screen, against
 * a footprint that collapses at a grazing view. RefractMove returns that
 * move in screen units; the caller scales it by the guard below and keeps
 * the place read inside the screen.
 *
 * The guard (the depth variant, seen from above). A thing at the surface -
 * a rat, a bubble, Lara's shoulders, a post - is not under the water, and a
 * deep pixel beside it must not read it: its bent place would show the thing
 * a second time, torn off beside itself and smeared into the water round it.
 * So the bent place is refused, and the pixel's own place read, where the
 * frame there lies within HD_WATER_GUARD under the surface at rest or above
 * it; and the move is the one the shorter of the two paths gives, the path
 * there or here - 1 - T of the shorter over 1 - T of this pixel's own - so a
 * deep pixel beside a shallow floor moves no further than the shallow side
 * allows. 96 units: the deepest thing that swims at the surface must lie
 * inside it. A swimming rat stands 50 units under the surface (game\rat.cpp,
 * the underwater room's ceiling + 50) and its mesh reaches 10 below its
 * place (y -84 to 10, the box of slot RAT in the classic ANDREA2.TRC), so
 * its belly lies 60 units down; at 48 a neighbour's bent place that fell on
 * the lowest 12 units read the rat and drew a piece of it beside itself.
 * 96 takes those 60 and the swell's and a ring's lift with room to spare,
 * and stays under every classic floor, which lies a click (256) down at the
 * least.
 *
 * The absorption per channel (gWaterAbsorb.rgb): the deep colour of the
 * section's palette is what a long path leaves of the shallow one, so the
 * channels are absorbed in the ratio of ln(shallow / deep), scaled so that
 * their mean is the class's one over the absorption length - the palette
 * keeps its meaning and the class its depth (water_look). */
#define HD_WATER_GUARD 96.0f

float2 RefractMove(float2 slope, float amount, float2 fx, float2 fy)
{
	float2 d = slope * (gWaterAbsorb.w * amount);
	const float len = length(d);

	d *= len > 256.0f ? 256.0f / len : 1.0f;

	const float det = fx.x * fy.y - fy.x * fx.y;
	float2 p = abs(det) > 1.0e-6f ? float2(d.x * fy.y - d.y * fy.x,
		d.y * fx.x - d.x * fx.y) / det : float2(0.0f, 0.0f);
	float2 move = p * gWaterScreen.xy;
	const float far = length(move);

	move *= far > 0.04f ? 0.04f / far : 1.0f;
	return move;
}

// A place on the screen, kept half a pixel inside it.
float2 ScreenClamp(float2 uv)
{
	return clamp(uv, 0.5f * gWaterScreen.xy, 1.0f - 0.5f * gWaterScreen.xy);
}

#ifdef HD_WATER_DEPTH
/* The frame's view depth at uv: the texture holds the depth a - b / z of
 * HDRenderMakeProjection, so z = b / (a - d). A pixel nothing was drawn on
 * (cleared to 1, past the far plane's 0.995) comes out very far away. */
float SceneZ(float2 uv)
{
	const float d = tex2Dlod(sWaterDepth, float4(uv, 0.0f, 0.0f)).r;

	return gWaterScreen.w / max(gWaterScreen.z - d, 1.0e-6f);
}
#endif

/* The water under the pixel, straight down in units, and the path the view
 * ray runs through it. With the depth texture both come out of the frame's
 * own depth behind the sheet: the ray from the sheet to what was drawn
 * behind it, its length the view depth between them times the eye distance
 * over the sheet's view depth - a floor of any shape, a step, an ice floe, a
 * statue's foot, and nothing at the bank. Without it out of the vertex depth,
 * the classic floor square's, over the cosine of the view (at least 0.25, so
 * a grazing view does not run to infinity). The one place the sheet asks for
 * the depth, for every term that needs it. */
float WaterDepthBelow(float vertexDepth, float viewZ, float3 eye, float2 uv,
	out float path)
{
	const float down = abs(normalize(eye).y);
#ifdef HD_WATER_DEPTH
	path = max(0.0f, SceneZ(uv) - viewZ) * length(eye) / max(viewZ, 1.0f);
	return path * down;
#else
	path = vertexDepth / max(down, 0.25f);
	return vertexDepth;
#endif
}

/* The rings of the game's own ripples (hd_render.cpp, "the rings"): each a
 * short train of waves behind a front that runs outward, its slope added to
 * the sheet's along the line from the ring's centre. u units behind the
 * front the slope is cos(2 pi u / HD_RING_WAVE) (Wave2's curve) under a
 * window that rises over the first quarter wavelength, holds to the end of
 * the first and is gone two wavelengths in - two long, low swells rather
 * than a quick train of narrow ones; the
 * amplitude is the ring's strength (falling with its age, on the processor)
 * over the square root of 1 + d / 256, d the distance from the centre - the
 * ring's energy spread over a growing circle. The wavelength, the train and
 * the 256 are free choices, set by eye on the round's RoomProbe pictures.
 * A loop over the registers, so the slots do not grow with the count. */
#define HD_RING_WAVE 90.0f

float2 RingSlope(float2 p)
{
	float2 slope = 0.0f;

	[loop]
	for (int i = 0; i < 8; i++)
	{
		if ((float)i >= gWaterLife.z)
			break;

		const float4 r = gWaterRings[i];
		const float2 dv = p - r.xy;
		const float d2 = max(dot(dv, dv), 1.0f);
		const float inv = rsqrt(d2);
		const float d = d2 * inv;
		const float x = (r.z - d) * (1.0f / HD_RING_WAVE);
		const float window = saturate(x * 4.0f) * saturate(2.0f - x);
		const float wave = Wave2(float2(x * 6.2831853f, 0.0f)).x;

		slope += dv * (r.w * window * wave * inv *
			rsqrt(1.0f + d * (1.0f / 256.0f)));
	}

	return slope;
}

float4 WaterPS(WATER_OUT input, float2 vpos : VPOS) : COLOR0
{
	const float3 world = input.roomPos.xyz + gRoomOrigin.xyz;
	const float t = gWaterMisc.z;

	/* A row whose phase moves by more than an eighth of a turn from one
	 * pixel to the next fades out, gone at a quarter, instead of aliasing
	 * into stripes - along its own direction, worked out per vertex
	 * (WaterVS, the rows' fade). dx and dy, the world step of one pixel
	 * right and one down, serve the refraction and the foam. */
	const float2 dx = ddx(world.xz);
	const float2 dy = ddy(world.xz);
	const float4 keep = input.keepA;
	const float4 keep2 = input.keepB;
	const float2 pb = WaveBend(world.xz, t, gWaveP[0].xy, gCrest.w);
	float2 slope;
	const float h = WaveSum8(pb, gWaveP[0], gWaveP[1], gWaveP[2],
		gWaveP[3], gWaveP2[0], gWaveP2[1], gWaveP2[2], gWaveP2[3],
		gCrestP[0], gCrestP[1], gCrestH[0] * keep, gCrestH[1] * keep2,
		gCrestA[0], gCrestA[1], gCrestA[2], gCrestB[0], gCrestB[1],
		gCrestB[2], gCrest.xyz, HD_CREST_LEAN_ON, slope);

	const float3 V = normalize(input.eye.xyz);

	/* The water under the pixel, straight down, and the ray's path through
	 * it (WaterDepthBelow) - the shallows below, the clip and the water's
	 * colour further down read them. */
	const float2 uv = (vpos + 0.5f) * gWaterScreen.xy;
	float path;
	const float depth = WaterDepthBelow(input.roomPos.w, input.viewZ.x,
		input.eye.xyz, uv, path);
	const float lift = input.viewZ.y;

	/* The shallows. A wave's height is held by the depth under it: it
	 * breaks where it passes 0.78 of the depth, so near a gently rising
	 * bank the water lies nearly still. `shoal` is the share of the rows'
	 * amplitude the depth allows, the depth over the depth from which the
	 * rows keep their whole height (gWaterStage.w its inverse, see
	 * hd_render.cpp over c106): 0 at the bank, 1 in open water. It takes
	 * the rows' slope - the reflection, the highlight and the floor's bend
	 * -, the bank foam's patches and the whitecaps; the waterline and the
	 * rings keep theirs. A body without a lift has 1e9 there and keeps its
	 * rows at every depth. Seen from below, the frame behind the sheet lies
	 * above it and far: nothing is held; under the water the variant holds
	 * nothing either. */
#ifdef HD_WATER_UNDER
	const float shoal = 1.0f;
#else
	const float shoal = saturate((depth + 1.0f) * gWaterStage.w);
#endif

	/* The body at eye level. Seen flat, a surface whose slopes are a few
	 * hundredths shows one even reflection; seen from above the same slopes
	 * already bend the floor and break the highlight. So the waves' slope
	 * is raised by the class's gain (gWaterStage.y is the gain less one:
	 * basin 3, channel 2, sea 1) where the view runs within about 9 degrees
	 * of the surface (|V.y| 0.15), not at all where it looks down by 37
	 * degrees or more (0.6), and linearly between: the water takes a body
	 * where it lay flat, and the view from above keeps the look it has.
	 * The gains and the two angles are free choices, set by eye on the
	 * round's RoomProbe pictures. The rings are added after it. */
	slope *= (1.0f + gWaterStage.y * saturate((0.6f - abs(V.y)) *
		(1.0f / 0.45f))) * shoal;
	slope += RingSlope(world.xz);

	// y counts down: the surface's normal points to -y.
	float3 N = normalize(float3(-slope.x, -1.0f, -slope.y));
	const float above = V.y < 0.0f ? 1.0f : 0.0f;

	N = above > 0.5f ? N : -N;

	const float nv = saturate(dot(N, V));
	const float g = 1.0f - nv;
	const float g2 = g * g;
	const float F = gWaterRefl.w + (1.0f - gWaterRefl.w) * (g2 * g2 * g);

	// What the reflected ray would see: horizon to overhead.
	const float3 R = reflect(-V, N);
	float3 refl = lerp(gWaterRefl.rgb, gWaterZen.rgb, saturate(-R.y));

	/* A room is not one colour all round: lit stretches of wall and dark
	 * ones, openings and pillars. A slow pattern over the reflected
	 * direction stands in for them, so that the waves bend something - no
	 * picture of the room, only light and dark, gWaterLife.x about its mean
	 * (+-40 % indoors, +-25 % under the sky). Indoors a finer second
	 * pattern, about 2.6 times as fine, takes gWaterLife.y of it, so a
	 * small wave moves the reflection too. */
	const float2 env = Wave2(R.xz * float2(5.0f, 4.3f) +
		float2(0.0f, 1.7f));
	const float2 fine = Wave2(R.xz * float2(13.1f, 11.3f) +
		float2(0.4f, 2.3f));
	const float pattern = 0.5f + 0.5f * lerp(env.x * env.y,
		fine.x * fine.y, gWaterLife.y);

	refl *= 1.0f + gWaterLife.x * (2.0f * pattern - 1.0f);

	/* And brighter toward the light: what stands round a lamp, or the
	 * bright band of a sky, is brighter than the rest. A broad lobe (the
	 * eighth power of R.L) of the highlight's colour - it is what lets the
	 * waves be seen in a reflection that has no picture in it. */
	const float toward = saturate(dot(R, gWaterSun.xyz));
	const float t2 = toward * toward;

	refl += gWaterHigh.rgb * (0.35f * t2 * t2 * t2 * t2);

	/* The highlight: Blinn-Phong over the waves' normal, and no pattern of
	 * its own over it - a field over the highlight's strength made of two
	 * periodic curves along the world axes is a checkerboard of bright
	 * dots wherever the eye comes close. */
	const float3 H = normalize(gWaterSun.xyz + V);
	const float3 spec = gWaterHigh.rgb *
		pow(saturate(dot(N, H)), gWaterHigh.w);

	/* The water: transmitted from the floor, or scattered in its place.
	 * `through` is what of the frame behind the sheet comes through, per
	 * channel - the floor's share once the copy is there. */
#ifdef HD_WATER_DEPTH
	/* The clip. What the frame drew at the surface's own height - a deck, a
	 * rim, a bank the remaster raised to the water plane inside a classic
	 * water square, a floe's top - is not under the water: where the frame's
	 * depth lies less than the body's clip (gWaterClip.x) under the surface
	 * at rest (the swell's lift taken back, so a crest does not drown the
	 * deck either), the pixel is left to the frame. clip() and not alpha 0:
	 * with the copy the sheet writes the whole pixel, and a texkill leaves
	 * it untouched with no colour to zero. 8 units for every body, 0 for one
	 * whose bank is the remaster's ground sloping through the surface
	 * inside its squares, where 8 only cut the sheet off the ground just
	 * under the water - hd_render.cpp kWaterClip and kWaterBank say why.
	 * RoomProbe -waterdepthview 4 shows `rest` and clips nothing. Seen from
	 * below there is no such floor; the clip is from above alone. */
	const float rest = depth - lift;

#ifdef HD_WATER_VIEW
	clip(above > 0.5f && gWaterStage.z < 3.5f ? rest - gWaterClip.x : 1.0f);
#else
	clip(above > 0.5f ? rest - gWaterClip.x : 1.0f);
#endif
#endif

	const float T = exp(-path * gWaterDeep.w);
	const float3 scatter = lerp(gWaterBase.rgb, gWaterDeep.rgb,
		saturate(1.0f - T));

	float3 col = F * refl + (1.0f - F) * (1.0f - T) * scatter + spec * above;
	float a = 1.0f - (1.0f - F) * T;
	float3 through = (1.0f - F) * exp(-path * gWaterAbsorb.rgb);
	float bend = 1.0f - T;

	/* Seen from below: inside the critical angle (about 49 degrees off the
	 * vertical, N.V above 0.66) the window, outside it the rest of the
	 * surface; the window's edge spread over 0.1 of N.V round that 0.66.
	 * With the copy both show the world above, bent by the waves at the
	 * full slope: the window with a tenth of the reflection colour in it,
	 * the rest under a light veil of the shallow colour, 0.35 of it - the
	 * surface is clear water with the waves' shapes in it, not a milky
	 * plane, and the room's own underwater fog and the screen's wobble do
	 * the rest, as they do for everything under water. Without the copy the
	 * blend shows the frame behind at the same shares. 0.35, 0.1 and the
	 * 0.1 of the edge are free choices, set by eye on the round's RoomProbe
	 * pictures and the reference pictures supplied for this project. */
	if (above < 0.5f)
	{
		const float window = smoothstep(0.61f, 0.71f, nv);

		a = lerp(0.35f, 0.1f, window);
		col = a * lerp(gWaterBase.rgb, refl, window);
		through = 1.0f - a;
		bend = 1.0f;
	}

#ifdef HD_WATER_FOAM
	/* The foam's own draw (HD_WATER_FOAM, see "the foam's draw" below):
	 * everything above that the foam does not read falls away.
	 *
	 * The foam: a density, not a mask - lacy at its outer edge, dense at
	 * the bank, never more than 0.75, so the water shows through it.
	 *
	 * The pattern: two layers of Wave2 at two speeds, the coarse one
	 * drifting slowly and the fine one, warped by it, fast, scaled by the
	 * class's grain (gWaterBase.w); per stretch of bank (WaterVariety) moved
	 * by up to 400 units, so no two stretches look alike. Moved, and not
	 * turned: a turn that varies over the world multiplies its own rate of
	 * change by the distance from the world's origin - 50,000 units out a
	 * blend between two orientations raised the pattern's frequency fifty
	 * times wherever it was half done and drew a comb of thin stripes along
	 * the bank (RoomProbe, the pattern alone as grey). A third, slow layer
	 * gives every region of blotches a phase of its own for a threshold that
	 * swings with a period of 5 seconds: blotches grow and dissolve.
	 *
	 * The reach: the swash, a cycle of 4.5 seconds whose phase runs along
	 * the bank, and the wave's own height there, so the foam runs up the
	 * bank and draws back - over the class's band from the classic shore
	 * (input.eye.w), in both variants - times the patch's lobe (PatchField,
	 * HD_PATCH_FROM): foam on parts of a bank, in lobes of their own sizes
	 * and lives, bare bank between, and not a rim of one width all round.
	 *
	 * The waterline, with the depth texture: a thin wet line where the
	 * frame lies within 24 units under the surface at rest, gone at 48,
	 * broken by the same pattern - round a post, a hull, a floe, wherever
	 * the drawn shape meets the water, and nothing more: a body a click
	 * under the surface is no shore, and a floor rising gently to the bank
	 * takes it only as far as its depth changes fast from pixel to pixel
	 * (see "the line where a shape meets the water" below). Within the
	 * band of a bank and outside
	 * a patch it is held to 0.25, a faint broken line where the foam is
	 * gone, so the bank is not bare; inside a patch and out in the water it
	 * keeps the 0.75 of all foam. Round Lara it gives way (gWaterLara): a
	 * wet line round a swimming or wading body is white paint on her legs
	 * and shoulders; her rings stay. Without the depth texture the line is
	 * the band's innermost 24 units, at 0.25 and broken the same way.
	 *
	 * The numbers here - the speeds, the 400 units, the 5 and 4.5 seconds,
	 * the 24 and 48 units, the line's 0.25 and the density's curves - are
	 * free choices, set by eye on the round's RoomProbe pictures. */
	const float var = input.viewZ.z;
	const float2 fp = (world.xz + var * float2(400.0f, -330.0f)) *
		gWaterBase.w;
	const float2 w1 = Wave2(fp.yx * float2(0.021f, 0.017f) +
		t * float2(0.25f, -0.18f));
	const float2 w2 = Wave2(fp * float2(0.047f, 0.041f) + w1 * 1.7f +
		t * float2(-0.9f, 1.1f));
	const float2 slow = Wave2(fp * float2(0.0083f, 0.0101f) +
		t * float2(0.11f, -0.13f) + var * 5.0f);
	const float2 cycle = Wave2(float2(t * 1.2566371f + slow.x * 3.1f,
		t * 1.3962634f + var * 6.2831853f +
		dot(world.xz, float2(0.0011f, 0.0007f))));
	const float n = saturate(0.5f + 0.5f * (w2.x * w2.y + 0.5f * w1.x));
	const float reach = 0.55f + 0.225f * (1.0f + cycle.y) +
		0.35f * saturate(h / gWaterMisc.w);
	const float patch = saturate((input.viewZ.w - HD_PATCH_FROM) *
		(1.0f / HD_PATCH_SPAN));
	const float edge = saturate(1.0f - input.eye.w /
		max(gWaterMisc.x * reach * 1.5f * sqrt(patch), 1.0f));
	const float thr = lerp(1.25f, -0.05f, edge) + 0.12f * cycle.x;
	// The bank foam's and the whitecaps' strengths, as the shallows hold
	// them.
	const float2 strength = float2(gWaterFoam.w, gWaterMisc.y) * shoal;
	float foam = strength.x * smoothstep(thr - 0.12f, thr + 0.18f, n) *
		(0.4f + 0.6f * edge);
	const float wet = smoothstep(0.25f, 0.6f, n);
#ifdef HD_WATER_DEPTH
	const float bare = (1.0f - saturate(patch * 4.0f)) *
		saturate(1.0f - input.eye.w / max(gWaterMisc.x, 1.0f));
	const float away = saturate((length(world.xz - gWaterLara.xy) -
		gWaterLara.z) * gWaterLara.w);
	/* The line where a shape meets the water, not across the shallows.
	 * Round a post, a hull or a swimming body the frame's depth under the
	 * surface runs down fast from pixel to pixel; on a gently sloping bank
	 * it changes slowly - a 1:10 shore puts the line's 48 units of depth 480
	 * units out into the water, the thick white band round ANDY3's pond
	 * (RoomProbe). So the line is weighed by that change per pixel
	 * (fwidth): whole from HD_LINE_STEEP units a pixel, fading toward a
	 * flat shore. Its fall from 24 to 48 units is a straight ramp. */
	const float steep = saturate(fwidth(rest) * (1.0f / HD_LINE_STEEP));

	foam = max(foam, min(gWaterFoam.w * saturate(2.0f - rest *
		(1.0f / 24.0f)) * steep * wet * away, 0.75f - 0.5f * bare));
#else
	foam = max(foam, min(gWaterFoam.w, 0.25f) *
		saturate(1.0f - input.eye.w * (1.0f / 24.0f)) * wet);
#endif

	/* The whitecaps of a sea: where the eight rows stand above the class's
	 * share of their whole height now (gWaterSun.w, gWaterMisc.w), and,
	 * weaker and broken up by the slow layer, where the two longest
	 * stood half a second earlier (gWaterLag): a cap breaks on the crest,
	 * trails behind it and dissolves. */
	const float2 lag = Wave2(float2(dot(pb, gWaveP[0].xy) - gWaterLag.x,
		dot(pb, gWaveP[1].xy) - gWaterLag.y) - 1.5707963f);
	const float heights = gWaterMisc.w;
	const float crest = saturate((h / heights - gWaterSun.w) * 4.0f);
	const float trail = saturate((dot(lag, gWaterLag.zw) / heights -
		gWaterSun.w) * 3.0f) * saturate(0.5f + 0.5f * slow.y);

	foam += strength.y * max(crest * smoothstep(0.45f, 0.85f, n),
		0.7f * trail * smoothstep(0.55f, 0.9f, n));
	foam = min(foam, 0.75f) * above;

	/* The foam's body. Foam is a thin layer with thickness, not paint: its
	 * cover (the density over its 0.75 cap) times the pattern stands for a
	 * height `lump`, 0 to 1 of the relief (hd_render.cpp, kWaterFoamRelief).
	 * Its rise toward the class's light - the screen gradient of `lump`
	 * against the screen step that one unit toward the light makes (the
	 * pixel's footprint solved the way RefractMove solves it) - tilts the
	 * layer's normal, and a soft diffuse term lights it: 0.55 + 0.45 N.L,
	 * N.L to first order in the tilt, over what a flat layer gets - all of
	 * it folded on the processor into gWaterLife.w, the relief times 0.45
	 * over that flat light. A flat layer keeps the foam's colour, a blotch
	 * is bright on the side toward the light and darker on the other, and
	 * never under 0.3 of it. Where the layer is thin it lies low and is
	 * shaded by what stands round it (0.65 at no thickness), and a blotch
	 * has a darker rim where its cover is half (1 - 1.4 c (1 - c), 0.65 at
	 * the rim). The cover still decides how much of the water it hides.
	 * 0.3, 0.65 and 1.4 are free choices, set by eye on the round's
	 * RoomProbe pictures. */
	const float cover = foam * (1.0f / 0.75f);
	const float lump = cover * (0.5f + 0.5f * n);
	const float det = dx.x * dy.y - dx.y * dy.x;
	const float2 sunStep = float2(gWaterSun.x * dy.y - gWaterSun.z * dy.x,
		gWaterSun.z * dx.x - gWaterSun.x * dx.y) * (det /
		(det * det + 1.0e-12f));
	const float body = max(1.0f - gWaterLife.w * dot(float2(ddx(lump),
		ddy(lump)), sunStep), 0.3f) * (0.65f + 0.35f * lump) *
		(1.0f - 1.4f * cover * (1.0f - cover));

	/* The foam's draw. The foam lay over the water as a share `foam` of
	 * the pixel: the water's colour times 1 - foam, the foam's times foam,
	 * the frame behind the sheet times 1 - foam, then the fog over both
	 * colours. With the blend ONE / INVSRCALPHA that is the sheet drawn
	 * without foam, then this second draw of the same triangles over it,
	 * premultiplied: the fogged foam colour times foam, alpha foam - the
	 * same picture up to the rounding of the first draw's result to the
	 * render target's bits. Two draws because the sheet's pixel shader has
	 * no room for the crests beside the foam (512 slots); the bodies
	 * without foam - a basin, a river - draw only the first
	 * (hd_render.cpp, draw_water_sheet). The clip above is the sheet's own,
	 * so the foam lies where the sheet does. */
	const float fogged = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);

	return float4(gWaterFoam.rgb * body * (foam * saturate(1.0f -
		fogged / max(gWaterZen.w, 0.05f))), foam);
#elif defined(HD_WATER_VIEW)
	// RoomProbe's view 5 shows the patches' strength.
	const float patch = saturate((input.viewZ.w - HD_PATCH_FROM) *
		(1.0f / HD_PATCH_SPAN));
#endif

	/* The distance fog (drawroom.cpp), in the light's own scale as RoomPS
	 * takes it, as a share of the body's light - see the note over
	 * WATER_OUT. The frame behind the sheet is fogged already. */
#ifdef HD_WATER_UNDER
	/* Under the water the extinction takes the fog's place, as in RoomPS:
	 * the eye ray runs in the water all the way to the surface, so its path
	 * is the whole distance and its upward cosine V.y (y counts down, V
	 * points to the eye). What the sheet adds of its own goes as the scene
	 * goes, and its share of the scattered light is its own cover `a`; the
	 * frame behind it, added below, carries its own extinction already. The
	 * clear zone round the eye as in UnderWater. */
	const float under = max(length(input.eye.xyz) - gUnderClear.x, 0.0f);

	col = col * exp(-under * gUnderSigma.rgb) + gUnderLight.rgb * (a *
		(1.0f - exp(-under * gUnderSigma.w)) *
		max(1.0f + gUnderLight.w * V.y, 0.0f));
#else
	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);

	col *= saturate(1.0f - fog / max(gWaterZen.w, 0.05f));
#endif

	/* With the copy: the floor read at the bent place and added with its
	 * share, the pixel written whole. With the depth texture the bent place
	 * passes the guard (over RefractMove) or is refused - from below where
	 * the frame there lies nearer than the sheet, from above where it lies
	 * less than HD_WATER_GUARD under the surface at rest: a leg, a pillar or
	 * a rat in the water is not pulled into the floor beside it; the
	 * pixel's own place is read instead. */
	float refused = 0.0f;

	if (gWaterStage.x > 0.5f)
	{
		float2 move = RefractMove(slope, bend, dx, dy);

#ifdef HD_WATER_DEPTH
		const float zAt = SceneZ(ScreenClamp(uv + move));
		// The path there, along this pixel's own ray.
		const float pathAt = max(0.0f, zAt - input.viewZ.x) *
			length(input.eye.xyz) / max(input.viewZ.x, 1.0f);

		refused = above > 0.5f ?
			(pathAt * abs(V.y) - lift < HD_WATER_GUARD ? 1.0f : 0.0f) :
			(zAt < input.viewZ.x ? 1.0f : 0.0f);
		move *= above > 0.5f ? (1.0f - exp(-min(path, pathAt) *
			gWaterDeep.w)) / max(bend, 1.0e-3f) : 1.0f;
		move *= 1.0f - refused;
#endif
		col += through * tex2Dlod(sWaterCopy,
			float4(ScreenClamp(uv + move), 0.0f, 0.0f)).rgb;
		a = 1.0f;
	}

#ifdef HD_WATER_VIEW
	/* RoomProbe -waterdepthview: 1 the path through the water behind the
	 * sheet over 16384 as grey, 2 the depth from
	 * the frame over the vertex depth times 0.5, 3 the pixels whose bent
	 * place the guard refused, in red over the sheet, 4 the depth under the
	 * surface at rest over 64 units, unclipped, 5 the bank foam's patches'
	 * strength in green over the sheet, unclipped. One block for the five,
	 * in a variant of its own (HD_WATER_VIEW, the depth variant with this
	 * block): the game never draws a view, and the variant it does draw
	 * does not carry the block's slots. */
	if (gWaterStage.z > 0.5f)
	{
		const float m = gWaterStage.z;
		const float grey = m > 3.5f ? rest * (1.0f / 64.0f) :
			m > 1.5f ? 0.5f * depth / max(input.roomPos.w, 1.0f) :
			path * (1.0f / 16384.0f);
		const float green = m > 4.5f ? 1.0f : 0.0f;

		return (m > 2.5f && m < 3.5f) || m > 4.5f ?
			float4(lerp(col, float3(1.0f - green, green, 0.0f),
			lerp(refused, patch, green)), 1.0f) :
			float4(saturate(grey).xxx, 1.0f);
	}
#endif

	return float4(col, a);
}

/* ------------------------------------------------- the waterfall's foot -- */

/* Where a waterfall lands on a sheet (hd_render.cpp, "the foot of a
 * waterfall") the water is churned white in one zone round the curtain's
 * footprint - a polyline of up to eight segments (gFootSeg, gWaterStage.w
 * of them) - and the foam is pushed away from it. A second draw of the
 * sheet's triangles over the first, with the same blend; its own pixel
 * shader because the sheet's depth variant has no room left for a loop.
 *
 * Per segment: the pixel's distance d to the segment, its way u along the
 * segment's line and its signed distance `across` from that line - u and
 * across run on past the segment's ends, so the pattern is one piece all
 * round them. Past an end d counts the way beyond it divided by that
 * end's head (gFootReach.z at the start, .w at the end): 1 is the round
 * cap of a joint or of a curtain without a gap, and an end at a gap
 * where no water lands stops within its head times the reach, so the
 * foam of a strand closes in an oval's head and open water lies between
 * two strands (hd_render.cpp, "the foot of a waterfall"). The zone's reach
 * R is the segment's (gFootReach.x) times
 * 0.8 + 0.2 of the bank foam's patch field here (WaterVS, PatchField) and
 * times 0.85 + 0.3 of the mean of a slow lobe field's two waves: an outline
 * that is irregular and changes slowly. x = d / R runs from 0 at the line
 * to 1 at the rim.
 *
 * The push. The foam leaves the line at HD_FOOT_SPEED and slows down to
 * nothing at the rim, v = v0 sqrt(1 - x); a mark carried by that flow has
 * come w = 2 R (1 - sqrt(1 - x)) from the line (the way it covered, the
 * time integral of v), so the pattern is read at w - v0 t across the line:
 * every blotch moves outward, slows down and gathers toward the rim. Across
 * the line the pattern's scale is HD_FOOT_STRETCH of its scale along it, so
 * near the line a blotch is a streak pointing away from the curtain, and
 * dw/dd = 1 / sqrt(1 - x) shortens it on its way out into the lace of the
 * rim. Nothing turns. The pattern is moved along the line by HD_FOOT_SIDE
 * on one side against the other, smoothly over 160 units either side of
 * the line, so the two sides are no mirror of each other and no seam runs
 * anywhere - a sign of `across` would draw one along the line past the
 * footprint's ends; and the bank foam's two layers ride at two speeds -
 * the fine one at HD_FOOT_LAG of the coarse one's - so they slide over each
 * other and the flow does not read as a belt. The bank foam's slow layer
 * and the threshold that swings with it over 5 seconds make the blotches
 * grow and dissolve.
 *
 * The density is the bank foam's, with the line where the bank is: the
 * threshold from 1.25 at the rim to -0.05 at the line along (1 - x) to the
 * power 0.75 - dense near the line, lace over the outer half -, the
 * smoothstep round it, times 0.4 + 0.6 (1 - x), times the segment's
 * strength (gFootReach.y: the body's foam scale; a gap of the curtain
 * where no water lands is no segment of the draw), the most of all the
 * segments, at most 0.75
 * like all foam. Then the foam's body and the distance fog exactly as
 * WaterPS lays them on. Seen from below nothing: foam is drawn from above
 * alone, as in WaterPS.
 *
 * The speed, the stretch, the side's move, the lag, the power 0.75 and the
 * lobe field (cells of about 900 by 1,300 units drifting at 0.05 a second)
 * are free choices, set by eye on the round's RoomProbe pictures and
 * strips: a power of 0.5 filled the zone, 1 and 2 left a thin line; a
 * stretch of 0.45 drew a comb.
 *
 * gFootReach  c137 - c144, one segment a register: x its reach in units,
 *             y its strength, z and w the heads of its start and end */
#define HD_FOOT_SPEED 220.0f
#define HD_FOOT_STRETCH 0.75f
#define HD_FOOT_SIDE 260.0f
#define HD_FOOT_LAG 0.55f

float4 gFootReach[8] : register(c137);

float4 FootPS(WATER_OUT input, float2 vpos : VPOS) : COLOR0
{
	const float3 world = input.roomPos.xyz + gRoomOrigin.xyz;
	const float t = gWaterMisc.z;
	// y counts down: the eye above the surface lies toward -y.
	const float above = input.eye.y < 0.0f ? 1.0f : 0.0f;
	const float2 lobeW = Wave2(world.xz * float2(0.0035f, 0.0024f) +
		t * float2(0.05f, -0.04f));
	const float spread = (0.8f + 0.2f * clamp(input.viewZ.w, -1.0f, 1.0f)) *
		(0.85f + 0.3f * (lobeW.x + lobeW.y) * 0.5f);
	float foam = 0.0f;
	float nAt = 0.5f;

	[loop]
	for (int i = 0; i < 8; i++)
	{
		if ((float)i >= gWaterStage.w)
			break;

		const float4 s = gFootSeg[i];
		const float4 r = gFootReach[i];
		const float2 ab = s.zw - s.xy;
		const float len = max(length(ab), 1.0f);
		const float2 dir = ab / len;
		const float2 rel = world.xz - s.xy;
		const float u = dot(rel, dir);
		const float across = dir.x * rel.y - dir.y * rel.x;
		const float d = length(float2(across, max(-u, 0.0f) / r.z +
			max(u - len, 0.0f) / r.w));
		const float reach = max(r.x * spread, 1.0f);
		const float x = saturate(d / reach);
		const float w = 2.0f * reach * (1.0f - sqrt(1.0f - min(x, 0.98f)));
		const float own = frac(dot(s.xy, float2(0.0013f, 0.0029f)));
		const float along = u + HD_FOOT_SIDE * smoothstep(-160.0f, 160.0f,
			across) + own * 400.0f;
		const float2 pa = float2(along, HD_FOOT_STRETCH *
			(w - HD_FOOT_SPEED * t)) * gWaterBase.w;
		const float2 pb = float2(along, HD_FOOT_STRETCH *
			(w - HD_FOOT_LAG * HD_FOOT_SPEED * t) - 330.0f) * gWaterBase.w;
		const float2 w1 = Wave2(pa.yx * float2(0.021f, 0.017f));
		const float2 w2 = Wave2(pb * float2(0.047f, 0.041f) + w1 * 1.7f);
		const float2 slow = Wave2(pa * float2(0.0083f, 0.0101f) +
			t * float2(0.11f, -0.13f) + own * 5.0f);
		const float swing = Wave2(float2(t * 1.2566371f + slow.x * 3.1f,
			0.0f)).x;
		const float n = saturate(0.5f + 0.5f * (w2.x * w2.y + 0.5f * w1.x));
		const float thr = lerp(1.25f, -0.05f, pow(1.0f - x, 0.75f)) +
			0.12f * swing;
		// At the rim (x 1) the threshold stands over every n: none.
		const float f = r.y * smoothstep(thr - 0.12f, thr + 0.18f, n) *
			(0.4f + 0.6f * (1.0f - x));

		nAt = f > foam ? n : nAt;
		foam = max(foam, f);
	}

	foam = min(foam, 0.75f) * above;

	// The foam's body, as WaterPS gives it (see "the foam's body" there).
	const float2 dx = ddx(world.xz);
	const float2 dy = ddy(world.xz);
	const float cover = foam * (1.0f / 0.75f);
	const float lump = cover * (0.5f + 0.5f * nAt);
	const float det = dx.x * dy.y - dx.y * dy.x;
	const float2 sunStep = float2(gWaterSun.x * dy.y - gWaterSun.z * dy.x,
		gWaterSun.z * dx.x - gWaterSun.x * dx.y) * (det /
		(det * det + 1.0e-12f));
	const float body = max(1.0f - gWaterLife.w * dot(float2(ddx(lump),
		ddy(lump)), sunStep), 0.3f) * (0.65f + 0.35f * lump) *
		(1.0f - 1.4f * cover * (1.0f - cover));
	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);
	const float3 col = gWaterFoam.rgb * body * foam *
		saturate(1.0f - fog / max(gWaterZen.w, 0.05f));

	// Premultiplied, as the sheet: the foam over what the sheet drew.
	return float4(col, foam);
}

/* ------------------------------------------------------------- the river -- */

/* A river (hd_render.cpp, "the rivers") is water a current pulls through.
 * Its rows are the sheet's own, stretched along the flow and carried with
 * it as one; this second draw of the sheet's triangles, with the same
 * blend, adds what moves with the current where the eye sees it from any
 * angle - the relief's light, a sheen on the skin, the light on the floor -
 * and the foam where it ends.
 *
 * The relief's light: the waves' own streaks. The sheet shows its rows
 * through the reflection, the highlight and the bend of the floor, and all
 * three fade from above: the reflection's share is the Fresnel term's 0.02,
 * the highlight stands at its mirror angle alone, and a stream 256 units
 * deep bends its floor by a few units. So the rows' slope (WaveSum8 at the
 * bent place, each row's height times WaterVS's fade, as WaterPS reads it)
 * is lit here by the class light's way along the surface (gWaterSun.xz):
 * facets toward it lighter by gRiverLight.x times their slope of the
 * highlight's colour, facets away darker through alpha, 0.6 of that share,
 * with no Fresnel and no reflection in it, so the crests read as streaks
 * along the flow from every angle, carried with the current, swelling and
 * fading with the rows' own envelopes.
 *
 * The sheen: long thin lines along the flow, a little lighter and a little
 * darker than the water and nothing white - the skin of a fast stream seen
 * from the bank, whose texture slides and whose highlights streak. Two sets
 * of ridges across the flow, about 410 and 170 units apart, whose places
 * wander slowly with the way down the river, so a line bends and does not
 * run ruler straight; the coarse set lighter, the fine set darker. Each
 * line is broken along the flow into dashes a few hundred units long,
 * carried at gRiverFlow.z (the current) times HD_RIVER_SKIN, faster than
 * the rows - the skin is carried faster than the shape of the waves - with
 * their phase turned by the way across, so neighbouring lines break at
 * their own places, and a slow field whose threshold swings over the bank
 * foam's 5 seconds brings a stretch of lines and dissolves it. A line
 * fades where the pixel's step across the flow passes HD_RIVER_THIN units:
 * a line thinner than a pixel or two would flicker. The lighter lines add
 * gRiverFlow.w of the highlight's colour, the light on the water, the
 * darker ones take 0.6 of that share of what lies behind: no Fresnel and
 * no reflection weighs either, so the sheen reads from above as from the
 * bank.
 *
 * The light on the floor: where the floor shows through the river the
 * caustic (CausticOf, the note beside HD_CAUSTIC_SCALE) is laid on it here,
 * carried with the current, in place of the one RoomPS lays on a floor
 * standing still (hd_render.cpp, upload_water_room: the submerged weight
 * is zero in the water rooms a river is seen through, while the eye is
 * above the water and the frame's copy is taken). Its place is the floor
 * point under the pixel, the view ray run on past the surface by the
 * square's depth (input.roomPos.w) over the ray's downward cosine (at least
 * 0.25, as WaterDepthBelow takes it), moved by the current's way; its
 * strength RoomPS's own, 1 + HD_CAUSTIC_FLOOR w c on the floor's colour,
 * with w gone two squares down as there and faded where a pixel spans more
 * than HD_CAUSTIC_FOOT units. The floor's colour is the frame's copy at the
 * pixel, its share the part of it the sheet lets through: 1 - F (Schlick,
 * F0 0.02) times the transmission over the path. A brighter floor is added,
 * a darker one taken off through alpha. gRiverEdge.w is its strength, 0
 * where RoomPS lays it. At night (gRiverNight.w 1) the light it adds is the
 * moon's, the floor's luma times gRiverNight.rgb, and not the floor's own
 * colour, whose torch-lit beds would turn its lines gold.
 *
 * The end: where the river runs into a wall or over a drop (input.flow.x,
 * the way ahead to the first point with no water, hd_render.cpp) one blob
 * of foam lies across it (see "the foam where it ends" below), carried
 * toward the end at gRiverEdge.z so the blotches pile into it - dense at
 * the end, broken further up and gone at about gRiverEdge.x, times
 * gRiverEdge.y. Nothing at the river's upper end, which the water
 * leaves.
 *
 * The ridges' spacings, the 0.8 and 0.97 of a line's edges, the slow
 * field's cells (about 1,340 units along the flow, 2,030 across) and its
 * threshold of 0.35 to 0.65, HD_RIVER_SKIN, HD_RIVER_THIN and the 0.6 are
 * free choices, set by eye on the round's RoomProbe pictures and strips.
 * Seen from below nothing: the sheen and the end are drawn from above,
 * and under the water RoomPS lays the caustic itself.
 *
 * gRiverFlow  xy the way the river runs (world x, z, a unit vector), z the
 *             current in units a second, w the sheen's strength
 * gRiverEdge  x the end foam's reach in units, y its strength, z the speed
 *             that carries it into the end, units a second, w the strength
 *             of the caustic laid on the floor, 0 to leave it to RoomPS
 * gRiverNight c146: rgb the moon's tint the caustic takes at night, w 1 at
 *             night and 0 by day
 * gRiverLight c147: x the relief's light, the brightness a unit of slope
 *             toward the class light adds; y, z, w unused */
#define HD_RIVER_SKIN 1.4f
#define HD_RIVER_THIN 24.0f

float4 gRiverFlow : register(c132);
float4 gRiverEdge : register(c133);
float4 gRiverNight : register(c146);
float4 gRiverLight : register(c147);

float4 RiverPS(WATER_OUT input, float2 vpos : VPOS) : COLOR0
{
	const float3 world = input.roomPos.xyz + gRoomOrigin.xyz;
	const float t = gWaterMisc.z;
	// y counts down: the eye above the surface lies toward -y.
	const float above = input.eye.y < 0.0f ? 1.0f : 0.0f;
	const float2 dir = gRiverFlow.xy;
	const float across = dot(world.xz, float2(-dir.y, dir.x));
	const float var = input.viewZ.z;
	const float2 dx = ddx(world.xz);
	const float2 dy = ddy(world.xz);
	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);
	const float keepFog = saturate(1.0f - fog / max(gWaterZen.w, 0.05f));

	// The sheen, carried on the skin.
	const float u = dot(world.xz, dir) - gRiverFlow.z * HD_RIVER_SKIN * t;
	const float2 wander = Wave2(float2(u * 0.0021f, u * 0.0013f) +
		var * float2(6.0f, 3.0f));
	const float2 ridge = Wave2(float2(across * 0.0153f + wander.x * 0.7f,
		across * 0.0371f + wander.y * 0.9f + 1.3f));
	const float2 slow = Wave2(float2(u * 0.0047f, across * 0.0031f) +
		t * float2(0.11f, -0.13f) + var * 5.0f);
	const float swing = Wave2(float2(t * 1.2566371f + slow.x * 3.1f,
		0.0f)).x;
	const float2 cut = Wave2(float2(u * 0.0109f + across * 0.0571f,
		u * 0.0067f - across * 0.0337f + 2.1f));
	const float dash = smoothstep(-0.25f, 0.45f, cut.x * cut.y +
		0.35f * cut.x);
	const float wide = max(abs(dot(dx, float2(-dir.y, dir.x))),
		abs(dot(dy, float2(-dir.y, dir.x))));
	const float stretch = smoothstep(0.35f, 0.65f, 0.5f + 0.5f * slow.x *
		slow.y + 0.1f * swing) * saturate(2.0f - wide *
		(1.0f / HD_RIVER_THIN));
	const float light = smoothstep(0.8f, 0.97f, ridge.x) * dash * stretch;
	const float dark = smoothstep(0.8f, 0.97f, ridge.y) * (1.0f - dash) *
		stretch;
	const float3 V = normalize(input.eye.xyz);

	// The relief's light: the rows' slope against the class light.
	float2 slope;

	WaveSum8(world.xz, gWaveP[0], gWaveP[1], gWaveP[2], gWaveP[3],
		gWaveP2[0], gWaveP2[1], gWaveP2[2], gWaveP2[3], gCrestP[0],
		gCrestP[1], gCrestH[0] * input.keepA, gCrestH[1] * input.keepB,
		gCrestA[0], gCrestA[1], gCrestA[2], gCrestB[0], gCrestB[1],
		gCrestB[2], gCrest.xyz, false, slope);

	const float relief = -dot(slope, gWaterSun.xz) * gRiverLight.x;

	float3 col = (gRiverFlow.w * light + max(relief, 0.0f)) * keepFog *
		gWaterHigh.rgb;
	float a = 0.6f * (gRiverFlow.w * dark + max(-relief, 0.0f));

	// The light on the floor, carried with the current.
	const float depth = input.roomPos.w;
	const float down = max(abs(V.y), 0.25f);
	const float2 floorAt = world.xz - V.xz * (depth / down) -
		dir * (gRiverFlow.z * t);
	const float sharp = saturate(2.0f - sqrt(max(dot(dx, dx), dot(dy, dy))) *
		(1.0f / HD_CAUSTIC_FOOT));
	const float m = HD_CAUSTIC_FLOOR * gRiverEdge.w * gWaterStage.x *
		saturate(1.0f - depth * (1.0f / 2048.0f)) * sharp *
		CausticOf(floorAt, t);
	const float g = 1.0f - abs(V.y);
	const float g2 = g * g;
	const float share = (1.0f - (0.02f + 0.98f * g2 * g2 * g)) *
		exp(-(depth / down) * gWaterDeep.w);
	const float2 uv = (vpos + 0.5f) * gWaterScreen.xy;
	const float3 floorCol = tex2Dlod(sWaterCopy, float4(ScreenClamp(uv),
		0.0f, 0.0f)).rgb;
	const float3 causticCol = lerp(floorCol, dot(floorCol,
		float3(0.299f, 0.587f, 0.114f)) * gRiverNight.rgb, gRiverNight.w);

	col += causticCol * (share * max(m, 0.0f));
	a = 1.0f - (1.0f - a) * (1.0f - share * max(-m, 0.0f));

	/* The foam where it ends: one blob the width of the river, not the
	 * bank foam's lattice, whose two layers of products repeat every few
	 * hundred units and laid three alike copies across a river's end.
	 * Three layers of cells about 830, 360 and 150 units across, each read
	 * along its own turned axes (0.6, 2.1 and 1.3 radians off the world's)
	 * and the finer warped by the coarser, carried into the end at
	 * gRiverEdge.z - no two of the scales in a simple ratio, so nothing
	 * repeats across a river. Its reach swings across the river with a
	 * field of its own (0.3 to 1.5 of gRiverEdge.x) and with the coarsest
	 * cells, so the blob's upper rim is ragged; densest at the end, still
	 * holed there, broken up toward the rim. The cell
	 * sizes, the turns and the reach's swing are free choices, set by eye
	 * on the round's RoomProbe pictures. */
	const float2 q = world.xz - dir * (gRiverEdge.z * t) +
		var * float2(400.0f, -330.0f);
	const float2 c1 = Wave2(float2(dot(q, float2(0.00626f, 0.00427f)),
		dot(q, float2(-0.00427f, 0.00626f))) + t * float2(0.07f, -0.05f));
	const float2 c2 = Wave2(float2(dot(q, float2(-0.00881f, 0.01503f)),
		dot(q, float2(-0.01503f, -0.00881f))) + c1 * 1.3f +
		t * float2(-0.21f, 0.17f));
	const float2 c3 = Wave2(float2(dot(q, float2(0.01117f, 0.04034f)),
		dot(q, float2(-0.04034f, 0.01117f))) + c2 * 1.1f +
		t * float2(0.6f, -0.7f));
	const float n = saturate(0.5f + 0.5f * (0.5f * c1.x * c1.y + 0.3f *
		c2.x * c2.y + 0.35f * c3.x * c3.y + 0.25f * c2.x));
	const float2 rim = Wave2(float2(across * 0.0089f + var * 4.0f,
		across * 0.0047f + 1.7f));
	const float edge = saturate(1.0f - input.flow.x / max(gRiverEdge.x *
		(0.9f + 0.4f * rim.x * rim.y + 0.2f * rim.x), 1.0f));
	const float lobe = saturate(edge + 0.3f * c1.x * c1.y);
	const float thr = lerp(1.15f, 0.3f, lobe) + 0.1f * swing;
	const float foam = min(gRiverEdge.y * smoothstep(thr - 0.12f,
		thr + 0.12f, n) * (0.4f + 0.6f * lobe), 0.75f);

	// The foam's body and the fog, as FootPS lays them on.
	const float cover = foam * (1.0f / 0.75f);
	const float lump = cover * (0.5f + 0.5f * n);
	const float det = dx.x * dy.y - dx.y * dy.x;
	const float2 sunStep = float2(gWaterSun.x * dy.y - gWaterSun.z * dy.x,
		gWaterSun.z * dx.x - gWaterSun.x * dx.y) * (det /
		(det * det + 1.0e-12f));
	const float body = max(1.0f - gWaterLife.w * dot(float2(ddx(lump),
		ddy(lump)), sunStep), 0.3f) * (0.65f + 0.35f * lump) *
		(1.0f - 1.4f * cover * (1.0f - cover));

	// Premultiplied, the foam over the sheen and the floor's light.
	col = lerp(col, gWaterFoam.rgb * body * keepFog, foam);
	a = lerp(a, 1.0f, foam);
	return float4(col, a) * above;
}

#ifdef HD_CARD
/* ------------------------------------------------------------- the cards --
 *
 * The water's splash and spray as cards (hd_render.cpp, "the cards"): a
 * splash's crown and drops where something falls into a sheet, the spray of
 * a breaking fish tank. Each card is a quad the processor builds in view
 * space, facing the eye, so the vertex shader only projects it: c0 - c3 hold
 * the projection alone. The page is the remaster's own (spray 3242 and 3243,
 * drops 3160), a window of it per card; the card cuts it round, softly, so
 * no quad's edge shows. Premultiplied (ONE / INVSRCALPHA), tested against
 * the frame's depth, no depth write.
 *
 * Pixel registers, both uploaded before the cards' one draw, in WaterPS's
 * per-draw block, which every sheet draw writes again before it reads it:
 *
 * c92  gCardScreen  x, y one over the render target's width and height, z, w
 *                   the projection's a and b (depth = a - b / z,
 *                   HDRenderMakeProjection)
 * c93  gCardMode    x 1 while the frame's depth texture is bound on s6, y one
 *                   over the depth over which a card fades into what stands
 *                   behind it (kCardSoft)
 *
 * Declared only under HD_CARD, so the room and water shaders compiled from
 * this file keep their own bytecode. */
float4 gCardScreen : register(c92);
float4 gCardMode : register(c93);
sampler2D sCardDepth : register(s6);

struct CARD_OUT
{
	float4 pos    : POSITION;
	float4 col    : TEXCOORD2;	// the tint, a the card's share
	float4 uv     : TEXCOORD0;	// xy the page, zw the corner, -1..1
	float4 viewZ  : TEXCOORD1;	// x the view depth, y the fog's light
};

/* The vertex: its place in view space, the page and the corner, the tint
 * and the share (floats: a body's foam colour may stand over 1), and the
 * light the fog takes a share of. */
CARD_OUT CardVS(float4 inPos : POSITION, float4 inUV : TEXCOORD0,
	float4 inCol : TEXCOORD1, float inLight : TEXCOORD2)
{
	CARD_OUT o;

	o.pos = mul(float4(inPos.xyz, 1.0f), gWorldViewProj);
	o.col = inCol;
	o.uv = inUV;
	o.viewZ = float4(inPos.z, inLight, 0.0f, 0.0f);
	return o;
}

float4 CardPS(CARD_OUT input, float2 vpos : VPOS) : COLOR0
{
	/* The round cut: full in the middle, falling to nothing at the rim as
	 * the square of one less the squared radius - a free choice, set by eye
	 * on the round's RoomProbe pictures. */
	const float r2 = dot(input.uv.zw, input.uv.zw);
	const float round = saturate(1.0f - r2);
	const float4 page = tex2D(sPage, input.uv.xy);
	float a = page.a * round * round * input.col.a;

	/* Soft against the frame: where the card comes within kCardSoft of
	 * what was drawn behind it - the water's surface it rises from, the
	 * floor it falls on - it fades, so no straight line cuts it there. */
	if (gCardMode.x > 0.5f)
	{
		const float2 uv = (vpos + 0.5f) * gCardScreen.xy;
		const float d = tex2Dlod(sCardDepth, float4(uv, 0.0f, 0.0f)).r;
		const float behind = gCardScreen.w / max(gCardScreen.z - d,
			1.0e-6f);

		a *= saturate((behind - input.viewZ.x) * gCardMode.y);
	}

	/* The distance fog as the sheet's foam takes it (FootPS), a share of
	 * the light the card carries. */
	const float fog = max(0.0f, input.viewZ.x - gFog.x) * gFog.y *
		(2.0f / 255.0f);

	a *= saturate(1.0f - fog / max(input.viewZ.y, 0.05f));

	/* The page gives the spray its shape, the body its colour: premultiplied
	 * as the sheet's foam is drawn (FootPS), the foam colour times the
	 * share - spray is the foam of the same water, thrown up. */
	return float4(input.col.rgb * a, a);
}
#endif

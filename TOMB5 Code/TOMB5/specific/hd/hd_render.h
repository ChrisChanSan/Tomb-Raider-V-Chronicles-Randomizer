/*
 * Modified 2026 by ChrisChanSan.
 *
 * The structure of the level build (read the .TRG, one buffer for the whole
 * level, a range table per group, three passes per room) and the draw rule
 * are carried over from TROne (Copyright 2026 Todesengel, GPLv3,
 * host\render\game_renderer.cpp, hd_load 1919-2256 and hd_draw_room
 * 2259-2312). The renderer under it is new: TROne is OpenGL 3.3 with vertex
 * arrays, texture arrays and runtime GLSL, this is Direct3D 9 with a vertex
 * declaration, one texture per page and compiled bytecode, beside a
 * fixed-function neighbour that must find its state exactly as it left it.
 * See THIRD-PARTY.md.
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
#pragma once

#include "hd_pagecache.h"

struct IDirect3DDevice9;
struct _D3DMATRIX;
typedef struct _D3DMATRIX D3DMATRIX;

// THE HD ROOM RENDERER (#123, stage S1-1).
//
// A second draw path beside the classic one. It shares the back buffer and
// the depth buffer with it and nothing else: its own shaders, its own vertex
// declaration, its own buffers, its own textures. Stage S1-1 builds it and
// proves it with a picture out of RoomProbe; the seam that lets the engine
// call it (draw.cpp, one line) is stage S1-2.
//
// WHY THE DEPTH BUFFER IS SHARED AND NOT FOUGHT OVER. The classic path is
// fixed function with pre-transformed vertices and writes its own depth,
//
//     sz = f_a - f_boo * rhw,  rhw = 2048 / z_view,  f_boo = f_b / 2048
//        = f_a - f_b / z_view
//
// which is exactly what a Direct3D projection matrix produces after the w
// division, with _33 = f_a, _43 = -f_b, _34 = 1, _44 = 0. So an HD room and a
// classic item land on the same depth for the same point in space, and they
// can be drawn in any order, in the same frame, into the same buffer. There
// is no sorting heuristic and no depth bias anywhere in this file - that is
// the whole point, and HDRenderSelfTest() is the proof, run at start-up.

/* ------------------------------------------------------ the parity block --
 *
 * Everything the classic path does to a room vertex that the .TRG's baked
 * light does not already contain (#123, stage S2b). The engine fills these in
 * hd_level.cpp out of its own globals; the diagnostic tool fills them by hand.
 * Nothing in here is Direct3D and nothing in here is engine - it is the list
 * of what specific\drawroom.cpp, aRoomletTransformLight, computes per vertex,
 * turned into shader constants.
 *
 * The two maxima are the shader's, not the engine's: ps_3_0 has no relative
 * addressing of constant registers, so the light loop is unrolled against a
 * compile time bound (hd_room.hlsl says why). Both are logged when they clip -
 * once per level with the room that did it, and again as a total at the end of
 * the level, never once per frame.
 */
#define HD_MAX_LIGHTS	8
#define HD_MAX_BULBS	16

// One entry of the engine's `dynamics` array, made room local and scaled the
// way aBuildRoomletLights scales it (drawroom.cpp:952-978).
struct HDLightSource
{
	float x, y, z;		// room local
	float falloff;		// (dynamic->falloff >> 1) + (dynamic->falloff >> 3)
	float r, g, b;		// 0..1
};

// One entry of ActiveFogBulbs, already in VIEW space - aBuildFogBulbList puts
// it there once per frame (drawroom.cpp:738-745) and the shader wants it in
// exactly that space, because the surface point arrives there too.
struct HDFogBulb
{
	float x, y, z;
	float rad;
	float r, g, b;
	float density;		// bulb->d, which is 1 / den
};

// Per frame, set once between HDRenderFrameBegin and the first room.
struct HDFrameLighting
{
	float centerX, centerY;		// f_centerx, f_centery
	float wibble;			// the engine's own counter, as a float
	int cameraUnderwater;		// camera.underwater
	float distanceFogStart;		// tomb5.distance_fog * 1024
	const float* wibbleTable;	// vert_wibble_table, 32 floats
	const HDFogBulb* bulbs;
	int bulbCount;

	/* Stage S5. `clock` is the game clock in seconds - the engine's own
	 * GlobalCounter divided by 30, made monotonic, so it stops with the game
	 * and never wraps. `motion` is tomb5.hd_motion: the UV scroll a .TRG part
	 * carries itself, the wind weight of its vertices and the water surface
	 * wobble, all three at once. Zero on both leaves the frame exactly as
	 * stages S1 and S2 drew it. */
	float clock;
	int motion;

	/* Fix round R3b. tomb5.hd_reflect: the specular the .TRG materials 3
	 * and 17 carry themselves - the marble of Rome, the painted steel of
	 * the submarine. Zero draws fix round R3a's picture to the bit. */
	int reflect;
};

// Per room. `waterTable` is 64 records of four bytes in WATERTAB order
// (signed shimmer, signed choppy, unsigned random, unsigned abs) - the
// engine's own WaterTable[room->MeshEffect] rather than a rebuilt one.
struct HDRoomLighting
{
	float originX, originY, originZ;	// r->x, r->y, r->z
	float surfaceY;				// minfloor - r->y, room local
	int underwater;				// r->flags & ROOM_UNDERWATER
	int choppy;				// run the choppy displacement at all
	int surfaceGate;			// shimmer needs the vertex on surfaceY
	int redAlert;				// the Red Alert gas branch
	float sel1[4];				// which baked mark drives flags & 1
	const void* waterTable;
	const HDLightSource* lights;
	int lightCount;
};

/* THE WATER MARKS. The .TRG marks nothing: no bit in a TrgVertex says "water
 * surface" or "this one shimmers". The classic path knows, because a roomlet
 * keeps its water vertices first (ROOMLET::nWVtx) and because the shimmer
 * predicate walks the neighbouring rooms through the portals
 * (drawroom.cpp:141-320). So the answer is computed ONCE, at level build, on
 * the classic vertices, and carried to the HD vertices by position - the same
 * road TROne takes for its own water flag (game_renderer.cpp:2127-2188, a per
 * room hash of packed int16 x/y/z), and the reason that road is taken here is
 * that it was measured to work there.
 *
 *   bit 0  the vertex is one of the roomlet's nWVtx water vertices - it gets
 *          the choppy displacement
 *   bit 1  the answer of the "vertex sits on the water surface" branch of the
 *          shimmer, WITHOUT the height test: mist, reflection or shore
 *   bit 2  the answer of the "whole room reflects" branch: the room carries
 *          ROOM_REFLECT and ROOM_REFLECT_CEILING and this vertex passes
 *
 * Which of the three the shader looks at is a per room decision, taken per
 * frame on the processor and handed over in HDRoomLighting::sel1 - the room
 * flags can change while a level runs, the geometry cannot.
 */
/* A few of a group's own vertices, kept aside while the vertex buffer is
 * filled - the .TRG dies at the end of HDRenderLevelBegin and the buffer is
 * write only, so this is the only chance. The matrix proof reads the three
 * positions; the parity proof (hd_light.h) reads all of it. */
struct HDRenderSample
{
	short x, y, z;			// room local
	unsigned char nx, ny, nz;	// biased by 127, as on disk
	unsigned char c1[3], c2[3];	// the two baked light triples, R G B
	unsigned char bits;		// the water marks that were found here
};

/* What moves in ONE room (#123, stage S5), for the log and for RoomProbe
 * -animtex. All of it is read out of the .TRG, none of it is a guess: the
 * scroll is material 1's param.xy, the sway is TrgVertex::sway, the flipbook
 * is material 13, whose param[0..2] name three further texture layers of the
 * file's own list - the remaster's own animated texture, and the one thing of
 * stage S5 that is NOT implemented (see MODIFICATIONS.md). */
struct HDRenderGroupMotion
{
	int scrollRanges;		// draw calls in this room that scroll
	int scrollTris;
	float scrollU, scrollV;		// the fastest of them, page units per second
	int swayVertices;		// vertices with a wind weight
	int swayMax;			// the largest weight found, 0..32767
	int flipbookParts;		// material 13 parts, counted and not drawn
};

struct HDWaterMark
{
	short room;
	short x, y, z;		// room local, classic units
	unsigned char bits;
};

/* THE ROOM'S OWN BOX, ROOM LOCAL (#123, fix round T, T1).
 *
 * The box TR5 itself calls the inside of a room - game\draw.cpp:2248-2253,
 * where an item is asked whether it fits wholly inside the room it is in:
 * `r->x + 1024 .. r->x + (y_size - 1) * 1024` across, the same in z out of
 * `x_size`, and `maxceiling .. minfloor` in height. The border ring of wall
 * sectors is NOT in it, and that is the point: the art the remaster puts in
 * that ring - a wall's relief, a corner pier, a doorway's reveal - stands in
 * the neighbouring room's space by construction, and the portal window that
 * was worked out for the room's INSIDE cannot hold it.
 *
 * Handed in room local (the .TRG's own frame: x and z relative to the room's
 * origin, y in world units, which is what the file stores) and in classic
 * units, one entry per room, in room order. Call it before
 * HDRenderLevelBegin; the pointer is not kept past that call. Without it
 * every group is drawn as one piece, which is what fix round T's predecessor
 * did. */
struct HDRoomBox
{
	float mins[3];
	float maxs[3];
};

/* ONE ROOM'S INSIDE, ASKED OF ANOTHER ROOM'S ART (#123, fix round T2, T2-2).
 *
 * WHAT FIX ROUND T GOT HALF RIGHT. T made the portal window the property of a
 * ROOM, and gave one to BOTH partners of every overlapping pair - the holder
 * as well as the intruder. The play test of 2026-09-08 photographed what that
 * costs in ANDREA2's courtyard: rooms 134 (the ground with the palm and the
 * doorways) and 203 (the colonnade over it) are both HOLDERS - 134 holds art
 * of 143, 203 holds art of 248 - and both were therefore scissored to their
 * own portal rectangles, which meet along the courtyard's horizontal room
 * boundary. What the player sees is a BLACK BAND straight across the picture
 * cutting the palm's trunk and the door lintels in half.
 *
 * The holder never needed a window. Nothing of ITS art is standing anywhere
 * it should not be. And the intruder needs one only for the triangles that
 * really do stand inside the other room - not for its whole group.
 *
 * SO THE WINDOW IS A PROPERTY OF A TRIANGLE. One of these says: "the art of
 * `group`, wherever it falls inside this box, is standing inside room
 * `holder` and is to be drawn inside `group`'s own portal window." Every
 * other triangle in the game, the holder's included, draws over the whole
 * screen exactly as it did before group M.
 *
 * The box is the holder's INSIDE - the volume TR5 itself calls the inside of
 * a room (game\draw.cpp:2248-2253), one sector further in on each side
 * because a room's border ring is one sector thick and two rooms that merely
 * share a wall reach into each other by construction. Handed in the GROUP's
 * own local frame, like the room boxes above, and on the same contract: call
 * HDRenderSetIntrusions before HDRenderLevelBegin, the pointer is not kept.
 * Without it nothing is ever tagged and no room is ever windowed. */
struct HDIntrusion
{
	int group;			// whose art is being asked about
	int holder;			// the room whose inside the box is - for the log
	float mins[3];
	float maxs[3];
};

struct HDRenderStats
{
	int trgVersion;
	int groups;
	int rooms;			// as handed in
	int skyGroups;
	int emptyGroups;		// a group with no index at all -> that room classic
	int vertices;
	int indices;
	int ranges;			// draw calls for a whole level, all rooms, all passes
	int splitRanges;		// ranges the device's primitive limit forced apart
	/* Fix round T (T1): triangles standing outside their own room's box, the
	 * draw calls they cost, and the groups that have NOTHING inside their
	 * room at all - ten in the whole game, and every one of them a room the
	 * HD path used to take over and then draw as a black hole. */
	int overhangTris;
	int hollowGroups;
	/* Fix round T2 (T2-2): the triangles that stand inside another room, the
	 * draw calls they cost and the groups that have any at all. These are the
	 * only draw calls in the game the scissor test is ever on for. */
	int intrudingTris;
	int intrudingRanges;
	int intrudingGroups;
	int index32;			// 1 = 32 bit index buffer
	/* The widest span of vertex numbers ONE draw call reaches over (#123,
	 * stage S6). It is what decides whether a card with no 32 bit index
	 * buffer can still draw the level: with 16 bit indices a draw call
	 * addresses 65536 vertices from its own base, so a level whose every
	 * range stays inside that window needs no 32 bit buffer at all. */
	int indexWindow;
	/* And the same for ONE TRIANGLE: how far apart the three corners of the
	 * worst triangle in the level are in the vertex array. It is the floor
	 * under indexWindow - no cutting of ranges can ever go below it - and it
	 * is what says whether a card with only 16 bit indices could draw this
	 * level at all. Measured, and the answer is in MODIFICATIONS.md. */
	int triangleWindow;
	int maxTextureSide;		// what the device said, 0 = it did not say
	int waterMarks;			// classic marks handed in
	int markedVertices;		// HD vertices a mark was found for
	int markedChoppy;		// of those, the nWVtx set
	int markedShimmer;		// of those, the surface shimmer set
	int markedRoomShimmer;		// of those, the whole-room set
	/* Of the shimmer set, the vertices no classic corner named and that the
	 * surface plane answered for (#123, fix round R3b) - see the long note in
	 * hd_render.cpp beside plane_map. */
	int markedFlooded;
	/* Of the marked set, the vertices whose own normal is not vertical - a
	 * wall or a pillar standing at exactly the water's height inside the
	 * pool's box (#123, fix round H, H4). The colour term keeps them, because
	 * the classic path gives them one; the geometry, the UV wobble and the
	 * caustic do not, because a water surface is flat. */
	int markedWall;
	/* E2/E3: flood planes past the fourth a group could not keep, so the
	 * shore taper has no box for them. Measured 0 in all fifteen files; a
	 * level that ever reports one has a room with more water planes than the
	 * table was built for. */
	int waterPlanesDropped;
	int biggestRangeTris;
	int fallbackPageTris;		// layer 0 - drawn on the level's first page
	int scrollParts;		// .TRG parts carrying a material 1 UV scroll
	// Opaque parts of the cut-out materials 6, 7 and 8 - the ones whose
	// param[0] is an alpha reference and that draw with the alpha test on
	// (#123, fix round A2; hd_render.cpp, MaterialAlphaRef).
	int cutoutParts;
	/* Additive parts whose material handed them a weight below 1, the
	 * smallest weight found, and how many of those also carry a swing time
	 * (#123, fix round R3a; hd_render.cpp, MaterialAddWeight). */
	int addWeightParts;
	int addSwingParts;
	float addWeightMin;
	/* Parts of a SKY group that carry no baked light and are therefore drawn
	 * at light 1 - the band of sky itself (#123, fix round R3a). */
	int skyFlatParts;
	int scrollRanges;		// draw calls that scroll, of `ranges`
	int scrollTris;			// triangles in them
	int swayVertices;		// vertices with TrgVertex::sway != 0
	int swayParts;			// parts of the moving materials 1, 7, 8 (A6)
	int swayDropped;		// vertices whose weight was taken away again (A6)
	int flipbookParts;		// material 13 parts - see HDRenderGroupAnim
	/* Of those, the ones whose four pages and frame time all resolved and
	 * that therefore run as a flipbook, the ones that did not and keep their
	 * own page, and the draw calls the running ones cost (#123, fix round
	 * R3b; hd_render.cpp, MaterialFlipTime). */
	int flipbookDrawn;
	int flipbookDropped;
	int flipbookRanges;
	/* Opaque parts of materials 3 and 17 whose own numbers gave them a
	 * gloss (#123, fix round R3b; hd_render.cpp, MaterialGloss). */
	int glossParts;
	/* H1: distinct horizontal planes the art covers, over every group - the
	 * table HDRenderGroupFlatArt answers out of. */
	int flatPlanes;
	/* H2: the see-through parts of the alpha pass - material 14, the glass -
	 * and the draw calls they cost. */
	int blendParts;
	int blendRanges;
	/* N1: the still water surfaces - material 16. The parts, counted per
	 * pass block, how the file splits them between the passes, their
	 * triangles, how many of them live in a sub-rectangle of their page,
	 * the smallest such span in 255ths, and the largest UV travel the ripple
	 * was given, in page units (#123, fix round N; MaterialRipple). */
	int waterParts;
	int waterOpaqueParts;
	int waterAdditiveParts;
	int waterTris;
	int waterSubRectParts;
	int waterStillParts;		// of those, the ones with no ripple at all
	int waterSmallestSpan;
	float waterRippleCap;
	double vertexBytes;
	double indexBytes;
	double readMs;			// reading and parsing the .TRG
	double buildMs;			// sorting and filling the buffers
	HDPageCacheStats pages;
};

// One row of the depth proof.
struct HDRenderZSample
{
	double zView;			// distance along the view axis, classic units
	double classic;			// f_a - f_boo * rhw, as polyinsert.cpp writes it
	double matrix;			// (_33 * z + _43) / z, what the device computes
	double diff;
};

// Creates the shaders and the vertex declaration. Nothing level-specific.
bool HDRenderInit(IDirect3DDevice9* device, const char** why);

/* PRETEND THE CARD IS POORER THAN IT IS (#123, stage S6).
 *
 * The fallbacks for a small card - pages that start below their top level
 * because the device cannot hold 4096 pixels, and 16 bit index buffers with a
 * base vertex per draw call because the device cannot address 1.1 million -
 * are the two places in the HD path that no machine in this house can reach.
 * A path nobody ever runs is a path that does not work, so this call bends the
 * caps that HDRenderInit read from the device, and the diagnostic tool draws
 * the same picture through them.
 *
 * maxTextureSide 0 leaves the device's own answer alone; noIndex32 non-zero
 * makes the renderer believe MaxVertexIndex is 65535. Call it BEFORE
 * HDRenderInit. The engine never calls it - there is no registry key and no
 * option for it, on purpose. */
void HDRenderCapOverride(int maxTextureSide, int noIndex32);

/* THE SHORE TAPER OFF (#123, fix round E, E3), for the diagnostic tool alone.
 * Non-zero hands the shader a shore box no vertex can be near the edge of, so
 * the swell reaches the water's own edge at full height again - which is the
 * picture fix round R3b drew, out of the same build. The engine never calls
 * it; there is no option and no registry key for it. */
void HDRenderShoreOverride(int off);

/* THE GLASS OFF (#123, fix round H, H2), for the diagnostic tool alone.
 * Non-zero leaves material 14 in the alpha pass where stages S1 and S2 had it
 * - an alpha TEST at 0.5, drawn solid - which is the before picture out of
 * the same build. Call it BEFORE HDRenderLevelBegin; the engine never calls
 * it and there is no option and no registry key for it. */
void HDRenderBlendOverride(int off);

/* THE WATER'S "A SURFACE IS FLAT" TEST OFF (#123, fix round H, H4), for the
 * diagnostic tool alone. Non-zero gives the wall vertices at the waterline
 * the surface's UV wobble and its caustic back, which is the picture fix
 * round R3b drew - the before picture out of the same build. May be called at
 * any time; it is one vertex shader constant per draw call. The engine never
 * calls it and there is no option and no registry key for it. */
void HDRenderUprightOverride(int off);
void HDRenderShutdown();
bool HDRenderReady();

// Reads <installRoot>\DATA\<stem>.TRG, builds one vertex buffer and one index
// buffer for the whole level, sorts every group's triangles by (pass, page)
// into ranges, and loads the pages through hd_pagecache. numRooms is the
// classic level's room count: groups at or above it are the sky dome and are
// kept out of the room table (they can still be drawn by number, which is
// what the diagnostic tool does).
bool HDRenderLevelBegin(const char* installRoot, const char* stem, int numRooms,
	bool managedPages, int threads, HDRenderStats* stats, const char** why);

// The classic path's water marks for the level that is about to be built.
// Call it before HDRenderLevelBegin; it keeps a copy of nothing, it only
// remembers the pointer for the length of that call. Passing 0 (or never
// calling it) leaves every HD vertex unmarked, which is what the diagnostic
// tool does unless it is asked for water.
void HDRenderSetWaterMarks(const HDWaterMark* marks, int count);
/* The rooms' own boxes for the level about to be built (#123, fix round T).
 * Same contract as the water marks: call it before HDRenderLevelBegin, the
 * pointer is only remembered for the length of that call. */
void HDRenderSetRoomBoxes(const HDRoomBox* boxes, int count);
/* The rooms that may be holding another room's art (#123, fix round T2).
 * Same contract again - before HDRenderLevelBegin, the pointer is only
 * remembered for the length of that call. After it, HDRenderIntrusionHits
 * says how many triangles each entry caught, which is what the level's log
 * line is made of. */
void HDRenderSetIntrusions(const HDIntrusion* list, int count);
/* Sort the inside half of every group apart from its overhang while the next
 * level is built, as fix round T did (#123, fix round T2). 0 by default and
 * in the engine always: it costs draw calls and buys the engine nothing since
 * the window became a property of the triangle. RoomProbe turns it on to draw
 * round T's own picture. Call it before HDRenderLevelBegin. */
void HDRenderSplitOverhang(int on);
int HDRenderIntrusionHits(int at);
// How many of a group's triangles stand inside some OTHER room, all three
// passes together. Non-zero is the whole of "this room needs a portal
// window"; zero is every other room in the game.
int HDRenderGroupIntrudingTriangles(int group);
void HDRenderLevelEnd();

// --- the frame -------------------------------------------------------------
//
// FrameBegin saves every device state the classic path sets or relies on and
// puts the device into the HD path's own state; FrameEnd puts every one of
// them back, bit for bit. Everything between the two is HD. Forgetting one of
// them is the first source of garbage on the screen, so the list is not a
// judgement call: the states are read back from the device and written back
// unchanged.
void HDRenderFrameBegin(const D3DMATRIX* view, const D3DMATRIX* projection,
	const HDFrameLighting* frame = 0);
void HDRenderFrameEnd();

/* IS A BRACKET OPEN RIGHT NOW? (#123, fix round P2.)
 *
 * HDRenderFrameBegin refuses to nest and returns doing NOTHING when one is
 * already open - which is the right answer, and a silent one. The pickup
 * display opens a bracket of its own in the middle of S_OutputPolyList
 * (specific\hd\hd_object.cpp, the immediate draw), so it has to know the
 * answer BEFORE it tells DrawInventoryItemMe to leave the classic polygons
 * out: an object that suppresses its own classic draw and then finds no
 * bracket to draw in is drawn by nobody. */
bool HDRenderInFrame();

// The bracket alone, with no room in it (#123, stage S3-1). FrameBegin and
// FrameEnd are these two plus the room path's own buffers, shaders and
// constants; hd_model.cpp draws inside the very same bracket, because there
// is ONE HD path and one list of states to save. A caller that opens the
// bracket itself must close it itself, and must not nest it inside a
// FrameBegin.
void HDRenderStateSave();
void HDRenderStateRestore();

/* THE PORTAL WINDOW AS A SCISSOR RECTANGLE (#123, group M).
 *
 * The classic path draws a room only where its own portal walk says the room
 * can be seen: `r->left / top / right / bottom`, a rectangle in BACKBUFFER
 * PIXELS, worked out in game\draw.cpp (SetRoomBounds / GetRoomBounds) and
 * handed to specific\drawroom.cpp as clip_left..clip_bottom. The HD path drew
 * a whole room group whatever the walk said, and in a level whose rooms
 * overlap in space - ANDREA1's 39 and 53 stand in the same world coordinates
 * - the second room's art came through the first room's wall. The depth
 * buffer cannot answer that: both rooms really are at that depth.
 *
 * So every HD room draw is scissored to its own window. `left/top` are the
 * first pixel drawn and `right/bottom` the first pixel NOT drawn, which is
 * Direct3D's own rule for D3DRS_SCISSORTESTENABLE; the caller does the
 * rounding and the margin, because it is the one that knows what the engine's
 * inclusive window meant.
 *
 * HDRenderScissorOff puts the test back off, which is the state the whole HD
 * bracket starts in. The rectangle and the enable are both part of the state
 * bracket - HDRenderStateSave reads them back and HDRenderStateRestore writes
 * them back - so a caller that forgets to switch it off still cannot leave the
 * classic path a device it did not have.
 *
 * HDRenderScissorAvailable is the device cap (D3DPRASTERCAPS_SCISSORTEST).
 * Every card since about 2002 has it; on one that does not, the two calls
 * above do nothing at all and the HD rooms draw as they did before this
 * group, which is the old bug and not a black screen. */
bool HDRenderScissorAvailable();
void HDRenderScissor(int left, int top, int right, int bottom);
void HDRenderScissorOff();

/* WHICH OF THE THREE PASSES A CALL IS TO DRAW (#123, fix round E, E6).
 *
 * The first two write depth and the third does not, and that one fact decides
 * the whole draw order of a frame. Passes 0 and 1 sort themselves out against
 * anything else that writes depth, whatever order they are drawn in; pass 2 -
 * additive, ONE / ONE, D3DRS_ZWRITEENABLE FALSE - leaves the depth buffer as
 * it found it, so any depth-writing draw that comes AFTER it and stands
 * BEHIND it in the world passes the depth test and paints over the glow.
 *
 * Room-major drawing - all three passes of room A, then all three of room B -
 * therefore loses every glow of room A that hangs in front of room B, which
 * on the screen is a lamp cone cut off along the portal's edge. So the frame
 * is drawn pass-major instead: SOLID for every room and every model first,
 * then GLOW for every room and every model, after the classic buckets. See
 * hd_level.cpp, HDFrameDraw and HDFrameDrawGlow. */
#define HD_PASS_OPAQUE		0x1
#define HD_PASS_ALPHA		0x2
#define HD_PASS_ADDITIVE	0x4
/* THE SEE-THROUGH HALF OF THE ALPHA PASS (#123, fix round H, H2). The alpha
 * pass holds two different things: the cut-outs, which are solid where they
 * are drawn at all and write depth like any wall, and the GLASS - material 14
 * - which is translucent and must be drawn after everything opaque, with no
 * depth write, or the wall behind it paints over it. So it is a phase of its
 * own, drawn in the glow phase beside the additive pass. A level with no
 * material 14 in its alpha pass never draws anything here. */
#define HD_PASS_BLEND		0x8
#define HD_PASS_SOLID		(HD_PASS_OPAQUE | HD_PASS_ALPHA)
#define HD_PASS_ALL		(HD_PASS_SOLID | HD_PASS_ADDITIVE | HD_PASS_BLEND)

/* WHICH HALF OF A GROUP A CALL IS TO DRAW (#123, fix round T, T1; fix round
 * T2, T2-2).
 *
 * Round T split a group into the art INSIDE the room's own box and the
 * OVERHANG outside it, and scissored the first half. Round T2 replaced that
 * with the question the window is really about: a group's triangles are
 * sorted into those that stand inside SOME OTHER ROOM - see HDIntrusion above
 * - and all the rest. The two never share a draw call, because they are drawn
 * with different scissor states: the intruding ones inside the portal window
 * the walk gave this room, and everything else over the whole screen, which
 * is what the classic path does with every polygon that straddles the
 * window's rim.
 *
 * HD_WINDOW_ALL is one call for the whole group and the state before group M;
 * every room but a handful is drawn with it and pays nothing. */
#define HD_WINDOW_ALL		0
#define HD_WINDOW_OPEN		1
#define HD_WINDOW_INTRUDING	2
/* AND ROUND T'S OWN TWO HALVES, FOR THE DIAGNOSTIC TOOL AND NOTHING ELSE.
 * The engine never asks for these: since T2-2 the overhang is drawn over the
 * whole screen with everything else and has no draw calls of its own, which
 * is where the ten per cent of draw calls round T's split cost went back. But
 * a picture of what round T DREW is what shows the black band across ANDREA2's
 * courtyard, and that picture needs the old index layout - so the split can
 * be turned back on for one run with HDRenderSplitOverhang. */
#define HD_WINDOW_INSIDE	3
#define HD_WINDOW_OVERHANG	4

// One room, the passes the mask names, world matrix as the classic path would
// set it (the room's origin translation). False when the group is missing or
// empty - the caller then draws that room the classic way.
bool HDRenderRoomMask(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes,
	int window = HD_WINDOW_ALL);

// All three passes - HDRenderRoomMask with HD_PASS_ALL.
bool HDRenderRoom(int group, const D3DMATRIX* world, const HDRoomLighting* lighting = 0);

// The same for a single pass; only the diagnostic tool needs it.
bool HDRenderRoomPass(int group, int pass, const D3DMATRIX* world,
	const HDRoomLighting* lighting = 0);

/* THE SKY DOME (#123, fix round R3a). How many groups past the last room this
 * level has - 0 for a level with no sky, 1 for every level that has one, 2
 * for TITLE - and the call that draws them all, camera centred, behind
 * everything. See the long note in hd_render.cpp for what the group holds and
 * why the sky gets its own slice of the depth buffer. `world` is the matrix
 * the classic path builds for its own horizon mesh: the camera position, no
 * rotation, no scale. False when the level has no sky group or none of them
 * drew. */
int HDRenderSkyGroups();
bool HDRenderSky(const D3DMATRIX* world);

/* --- the statics (#123, fix round R3b) -------------------------------------
 *
 * DOES THE .TRG DRAW ANYTHING WHERE THIS STATIC MESH STANDS? Room local, in
 * classic units, against a 256-unit occupancy of the group's own vertices
 * built while the index buffer is filled. False means the room seam would
 * make the static vanish without putting anything in its place - fix round B
 * measured 21 such statics in ANDREA1 alone - and the caller then draws it
 * the classic way.
 *
 * AND THE DESTRUCTIBLE PIECES. A .TRG part whose material carries bit 15 is
 * one copy of a destructible object, the counter in bits 8 to 14 says which
 * copy, and its triangles are kept in draw calls of their own so that one
 * copy can be switched off. `Info` hands out a piece's room-local box so the
 * caller can pair it with the classic MESH_INFO standing there; `Hide` is
 * what MESH_INFO::Flags bit 0 turns into once Lara has shot it. See the long
 * note beside HDDestructible in hd_render.cpp for the measurement. */
bool HDRenderGroupHasGeometry(int group, const float* mins, const float* maxs);

/* HOW MUCH of the group's art stands in a room-local box (#123, fix round T).
 *
 * The counting form of the call above, and it answers two questions this
 * round asks that a yes/no cannot:
 *
 *   T4, the statics. "Is there ANY art in this lantern's box" turned out to
 *   be too weak a reason to take a static away: ANDREA2's palm has its trunk
 *   in one room and its crown in the room above, and the crown's box catches
 *   FOUR cells of 768 - the wall behind it, not the palm - so the crown was
 *   suppressed and the .TRG had nothing to put in its place. The caller now
 *   asks how much and compares it with the box's own size.
 *
 *   T1, the overlap list. Whose art stands inside whose room. `interiorOnly`
 *   counts only the cells of triangles that are inside the group's OWN room
 *   box - the overhang is drawn without a window anyway, so it cannot be the
 *   reason to give one to a room.
 *
 * The box is room local, in the group's own frame. */
int HDRenderGroupCellsIn(int group, const float* mins, const float* maxs,
	int interiorOnly);
// How many triangles of the group stand outside its own room box, all three
// passes together (#123, fix round T). A group whose whole art is outside is
// a group that draws nothing in its own room, and the caller keeps that room
// classic.
int HDRenderGroupOverhangTriangles(int group);

/* DOES THE GROUP DRAW ANYTHING IN A GIVEN HORIZONTAL PLANE (#123, fix round
 * H, H1)? Room local, classic units, `slack` the height tolerance. It is what
 * decides whether the CLASSIC water surface of a room has to be drawn on top
 * of the HD one: measured over the fifteen files, 61 of 93 reflecting rooms
 * have no .TRG art in the plane their classic water lives in - every VCI pool
 * among them - and there the HD room shows the pool floor with nothing over
 * it. See hd_level.cpp, BuildWaterSurfaces. */
bool HDRenderGroupFlatArt(int group, int y, int x0, int z0, int x1, int z1,
	int slack = 32);
/* AND DOES IT BUILD THE WATER ITSELF (#123, fix round N)? The same question
 * over the MATERIAL 16 triangles alone - the remaster's own still water
 * surface. A yes here is final FOR THAT FACE and needs no majority behind it:
 * the classic face and the .TRG's sheet would stand one to twenty units apart
 * and that piece of pool would carry two surfaces. It is not a verdict on the
 * whole room, though - the room keeps every face the sheet does NOT cover, or
 * a sheet over two sectors would empty a pool of twenty-nine. See
 * BuildWaterSurfaces. */
bool HDRenderGroupWaterArt(int group, int y, int x0, int z0, int x1, int z1,
	int slack = 32);
// How many distinct horizontal planes that group's art covers - for the log.
int HDRenderGroupFlatPlanes(int group);
int HDRenderDestructibleCount();
bool HDRenderDestructibleInfo(int at, int* group, unsigned int* material,
	float* mins, float* maxs);
void HDRenderDestructibleHide(int at, bool hidden);

int HDRenderGroupCount();
int HDRenderGroupTriangles(int group, int pass);	// pass < 0 = all three
int HDRenderGroupRanges(int group, int pass);

// A handful of the group's own vertex positions, room local, spread evenly
// over its index run - the numbers the world-matrix proof is run on. The
// .TRG itself is freed at the end of HDRenderLevelBegin and the vertex buffer
// is write-only, so these are kept aside while the buffer is filled: at most
// HD_RENDER_SAMPLES per group, which is 192 KiB over a whole level.
// Returns how many vertices were written (3 shorts each).
#define HD_RENDER_SAMPLES 128
int HDRenderGroupSampleCount(int group);
int HDRenderGroupSamples(int group, short* xyz, int maxVerts);
// The same samples with their normals, their baked light and their water
// marks - what the parity proof needs to run both formulas on real data.
int HDRenderGroupSampleData(int group, HDRenderSample* out, int maxVerts);
// The room-local bounding box of a group, from the vertices its own indices
// name. False when the group draws nothing.
bool HDRenderGroupBounds(int group, float* mins, float* maxs);

// What moves in one room (#123, stage S5). False for a group that is not
// there; a group that draws nothing answers with all zeroes.
bool HDRenderGroupMotionInfo(int group, HDRenderGroupMotion* out);
int HDRenderDrawCalls();			// since the last FrameBegin
// A2: of those, the opaque draw calls that ran with the alpha test on
// because their .TRG material is a cut-out one (leaves, gratings, nets).
int HDRenderCutoutCalls();
// R3b: of those, the draw calls that picked their page out of a material 13
// flipbook rather than out of their own vertices.
int HDRenderFlipbookCalls();
// R3b: and how many of them carried a gloss out of their material.
int HDRenderGlossCalls();
// N1: and how many drew a still water surface - a material 16 part with a
// ripple on it (#123, fix round N).
int HDRenderWaterCalls();

// The bytes one room vertex takes in the vertex buffer. Named here because the
// texture budget has to price a level's buffers before they are made.
int HDRenderVertexBytes();

// --- the projection --------------------------------------------------------

// The engine's own SetupZRange (specific\3dmath.cpp), as a function without
// the engine. mznear / mzfar are the values in the float pipeline's units -
// 20 and 20480 for TR5, which is what InitWindow hands SetupZRange before the
// W2V shift is taken off again.
void HDRenderZConstants(double mznear, double mzfar, double* a, double* b, double* boo);

// The matrix whose depth after the w division is exactly a - b / z_view.
// persp is f_persp, centerX / centerY are f_centerx / f_centery, and a / b are
// the engine's f_a / f_b as they stand AFTER SetupZRange (f_b already
// negated).
void HDRenderMakeProjection(double persp, double centerX, double centerY,
	double a, double b, D3DMATRIX* out);

// Fills up to `rows` samples between mznear and mzfar and returns the worst
// absolute difference between the classic depth and the matrix depth. This is
// the numerical proof that the two paths write the same depth; anything above
// about 1e-5 means the shared depth buffer is a fiction and the HD path needs
// its own answer.
//
// persp is f_persp. It cancels out algebraically - the classic path divides
// by it and multiplies by it again - but it does so in single precision and
// in two steps, so leaving it out of the test would test a formula instead of
// the code.
double HDRenderSelfTest(double mznear, double mzfar, double persp,
	HDRenderZSample* rows, int rowCount);

// What the device actually compares depth with, read back from it rather than
// assumed: ZENABLE (1 = D3DZB_TRUE, 2 = D3DZB_USEW), ZFUNC, ZWRITEENABLE, and
// the viewport's MinZ / MaxZ. Every one of them changes the meaning of the
// numbers HDRenderSelfTest compares, so the proof names them.
void HDRenderDepthTruth(IDirect3DDevice9* device, unsigned long* zenable,
	unsigned long* zfunc, unsigned long* zwrite, float* minZ, float* maxZ);

// How many instruction slots the card allows a ps_3_0 shader. The room shader
// is 319 of them and shader model 3 guarantees 512, but a card that reports
// less would refuse CreatePixelShader with no useful message, so the number is
// read and logged rather than assumed.
unsigned long HDRenderPixelSlots();

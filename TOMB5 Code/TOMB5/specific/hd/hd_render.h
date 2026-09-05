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

// The bracket alone, with no room in it (#123, stage S3-1). FrameBegin and
// FrameEnd are these two plus the room path's own buffers, shaders and
// constants; hd_model.cpp draws inside the very same bracket, because there
// is ONE HD path and one list of states to save. A caller that opens the
// bracket itself must close it itself, and must not nest it inside a
// FrameBegin.
void HDRenderStateSave();
void HDRenderStateRestore();

// One room, three passes, world matrix as the classic path would set it (the
// room's origin translation). False when the group is missing or empty - the
// caller then draws that room the classic way.
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

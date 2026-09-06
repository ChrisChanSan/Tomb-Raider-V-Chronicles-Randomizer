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
#pragma once

// THE SEAM BETWEEN THE ENGINE AND THE HD PATH (#123).
//
// Everything behind these calls is switched off unless the registry setting
// hd_enable is 1 - which it is not out of the box, so an unchanged
// installation does not read a single byte more than it did before and draws
// exactly the frame it drew before. Each function below says in its own first
// line where that guard sits.
//
// THE LEVEL BRACKET
//
//   HDLevelLoad   specific\file.cpp, LoadLevel, after LoadTextureInfos - on
//                 the LOAD THREAD. Reads the .TRG and the .MAP and measures
//                 them into the log. No Direct3D object is made here.
//   HDLevelBuild  specific\file.cpp, S_LoadLevelFile, beside aInit() - on the
//                 MAIN THREAD, which owns the device. Vertex buffer, index
//                 buffer and page textures are made here, for the same reason
//                 aInit() was moved out of the load thread: two threads on one
//                 device froze this game twice already, and the fix is not to
//                 do it a third time.
//   HDLevelUnload specific\file.cpp, FreeLevel. Frees everything, whatever the
//                 setting says now - a level built with HD on must come down
//                 even if the player switched HD off while playing it.
//
// THE FRAME BRACKET
//
//   HDFrameReset  specific\output.cpp, S_InitialisePolyList. Empties the list.
//   HDFrameRoom   specific\drawroom.cpp, InsertRoom. Takes the room over when
//                 it is an HD room: remembers it with its world matrix and
//                 tells the caller to skip the classic roomlets.
//   HDFrameDraw   specific\output.cpp, S_OutputPolyList, right before
//                 DrawBuckets. Draws every remembered room's SOLID passes
//                 and every HD object's, which is everything that writes
//                 depth.
//   HDFrameDrawGlow
//                 specific\output.cpp, S_OutputPolyList, right AFTER
//                 DrawBuckets (#123, fix round E, E6). Draws the additive
//                 pass of the same rooms and objects, which writes no depth
//                 and therefore has to come after every opaque draw in the
//                 frame - HD and classic alike. Empties the list.
//
// WHY THE ROOMS ARE NOT DRAWN WHERE THEY ARE COLLECTED. The classic path does
// not draw in InsertRoom either - it fills texture buckets and a sort list and
// hands them to DrawBuckets/DrawSortList later. Between the two lies the
// frame's depth clear (S_InitialisePolyList, #127). An HD room drawn in
// InsertRoom would therefore be wiped by that clear in the same frame in every
// call path where the two are not already in the right order, and the HD walls
// would swallow Lara and every item. Collecting and drawing at the one place
// where the classic world is drawn keeps the two paths in one order, whoever
// calls them.

void HDLevelLoad(const char* wadName);
void HDLevelBuild();
void HDLevelUnload();

// The Direct3D device is about to be released or reset (a resolution change,
// Alt+Enter, a lost device). Every HD object lives on THAT device, so all of
// it goes now; the next level load builds it again on the new one.
void HDLevelDeviceLost();

// Whether the HD path is switched on AND an installed Remaster was found.
// Nothing else in the engine may read HD data without asking this first.
bool HDEnabled();

// Whether room `roomIndex` of the level now loaded is drawn by the HD path.
// Decided once at level load, not per frame. False for every room while HD is
// off, while no HD level is built, and for every room the .TRG has nothing
// for. The static meshes of an HD room are already part of its art and must
// not be drawn a second time - that is the one other caller of this.
bool HDRoomIsHD(int roomIndex);

/* ONE CLASSIC WATER FACE AN HD ROOM STILL HAS TO DRAW (#123, fix round H,
 * H1). The remaster's .TRG has no geometry in the plane the classic level
 * puts its water surface in - measured in 61 of the game's 93 reflecting
 * rooms, every VCI pool among them - so an HD room shows the pool floor and
 * nothing over it. Where that is so, and only there, the level's own water
 * face is drawn on top of the HD room by the classic path, with its own
 * texture, its own animated texture range and its own shimmer.
 *
 * `face` counts inside the roomlet's quad list when `quad` is set and inside
 * its triangle list otherwise; the list is sorted by roomlet so that one
 * roomlet is transformed and lit once. See hd_level.cpp, BuildWaterSurfaces. */
struct HDRoomWaterFace
{
	short roomlet;
	short face;
	unsigned char quad;
};

int HDRoomWaterFaceCount(int roomIndex);
const HDRoomWaterFace* HDRoomWaterFaceAt(int roomIndex, int at);

/* THE TWO STATIC MESHES OF AN HD ROOM THAT STILL HAVE TO BE DRAWN (#123, fix
 * round R3b). Both answers are worked out once at level load, against the
 * .TRG's own geometry, and DrawStaticObjects (game\draw.cpp) is their only
 * caller.
 *
 * HDStaticIsClassic - the .TRG draws NOTHING where this static stands. Fix
 * round B measured 21 of them in ANDREA1 alone, in rooms 121 and 221, and
 * under the room seam they simply vanished. Such a static is drawn the
 * classic way inside an HD room; every other one stays suppressed, because
 * its art is baked into the room.
 *
 * `meshIndex` counts ROOM_INFO::mesh from zero. Both return false for a room
 * that is not HD, for an index out of range, and with HD off - a classic room
 * draws all of its statics through the ordinary path. */
bool HDStaticIsClassic(int roomIndex, int meshIndex);

// ONE FlipMap SWAP (#123, fix round A, A1). Called from FlipMap
// (game\control.cpp) at the line where the two ROOM_INFO records change
// places, once per pair. FlipMap swaps the CONTENTS of two rooms and leaves
// their numbers alone, so the HD art of the pair has to change places with
// them or room r goes on being drawn with the art of the room that is no
// longer there - the wall that is gone and whose HD texture still stands.
//
// Safe to call at any time and from anywhere: with HD off, or before a level
// is built, it returns on its first test.
void HDFlipRoom(int roomIndex, int otherIndex);

void HDFrameReset();
bool HDFrameRoom(int roomIndex, const float* viewWorld);

// THE SKY DOME (#123, fix round R3a). Called from SkyDrawPhase (game\draw.cpp)
// where the classic path draws its HORIZON mesh, with the matrix that is on
// the engine's stack there - the camera position, no rotation. Draws the
// .TRG's own sky group (two in TITLE) camera centred, in its own slice of the
// depth buffer, and answers true; the caller then leaves out the horizon mesh
// and the flat colour layers. False with HD off, before a level is built, and
// in a level whose .TRG has no sky group - then the classic sky draws whole.
bool HDFrameSky(const float* viewWorld);

void HDFrameDraw();
void HDFrameDrawGlow();

// THE FRAME CLOCK (#123 diagnostics). Called from DXShowFrame
// (specific\dxshell.cpp) directly after IDirect3DDevice9::Present returns -
// the one function in this engine that presents anything, so the game frame,
// the loading bar, the inventory, the title menu, the credits and an FMV all
// come through it exactly once each.
//
// With -log it writes one `FPSLOG` line per second of wall clock: how many
// frames were presented, the shortest, longest and average gap between two
// presents, how many of those gaps missed the 30 Hz budget, and the heaviest
// HD room / object / draw call counts seen in that second. At the end of a
// level it writes one summary line. Without -log its first line returns and
// nothing at all is measured - it does not even read the clock.
//
// This is NOT behind hd_enable: the frame rate is a property of the whole
// engine, and a report about it has to be possible with the HD path off.
void HDFramePresented();

// The field of view of THIS frame, taken the first time anything HD turns up
// in it - a room in InsertRoom or an item in DrawAnimatingItem. Called by
// both, does its work once, and is here because AlterFOV moves f_persp inside
// a frame while the binoculars are up.
void HDFrameCaptureProjection();

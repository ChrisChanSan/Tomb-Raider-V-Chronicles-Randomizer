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

// HD_REDRESS_ALLOWANCE and the rest of the re-dress rule live in hd_memory.h,
// beside the measure they are made of; included here for the readers of this
// header.
#include "hd_memory.h"

// The seam between the engine and the HD path.
//
// Everything behind these calls is off unless the registry setting hd_enable
// is 1, which it is not by default: an unchanged installation reads no extra
// byte and draws the same frame as the classic engine.
//
// Level bracket:
//   HDLevelLoad   specific\file.cpp, LoadLevel, after LoadTextureInfos, on
//                 the load thread. Reads the .TRG and the .MAP and logs their
//                 measures. No Direct3D object is made here.
//   HDLevelBuild  specific\file.cpp, S_LoadLevelFile, beside aInit(), on the
//                 main thread, which owns the device. Makes the vertex and
//                 index buffers and the page textures there, because two
//                 threads on one device freeze the game.
//   HDLevelUnload specific\file.cpp, FreeLevel. Frees everything whatever the
//                 setting says now: a level built with HD on must come down
//                 even if HD was switched off during play.
//
// Frame bracket:
//   HDFrameReset  specific\output.cpp, S_InitialisePolyList. Empties the list.
//   HDFrameRoom   specific\drawroom.cpp, InsertRoom. Takes an HD room over:
//                 remembers it with its world matrix and tells the caller to
//                 skip the classic roomlets.
//   HDFrameDraw   specific\output.cpp, S_OutputPolyList, right before
//                 DrawBuckets. Draws the solid passes of every remembered
//                 room and HD object - everything that writes depth.
//   HDFrameDrawGlow
//                 specific\output.cpp, S_OutputPolyList, right after
//                 DrawBuckets. Draws the additive pass of the same rooms and
//                 objects, which writes no depth and so must follow every
//                 opaque draw, HD and classic. Empties the list.
//
// The rooms are not drawn where they are collected: the frame's depth clear
// (S_InitialisePolyList) lies between InsertRoom and DrawBuckets, and an HD
// room drawn in InsertRoom would be wiped by it, so the HD walls would swallow
// Lara and every item. Drawing at the one place the classic world is drawn
// keeps both paths in one order.

void HDLevelLoad(const char* wadName);
void HDLevelBuild();

/* Rebuilds the level's models in the running level.
 *
 * The F4 outfit changer writes the chosen key and then puts the new body on
 * Lara without leaving the level: the objects are released and
 * HDObjectLevelBuild runs once more, as a level change does. The rooms, their
 * page cache and every classic table are untouched.
 *
 * Safe on the main thread because there is no other: the load thread is
 * joined before HDLevelBuild runs (specific\file.cpp, end of LoadLevel), and
 * the caller has stopped the world as the pause menu does, so no frame is
 * drawn and nothing reads the models.
 *
 * The page cache never gives a page back (other models hold slot indexes),
 * so every outfit worn in one level stays in it. Measured on ANDREA1 with a
 * full wardrobe: one outfit is at most 20.00 MiB of pages, the dearest single
 * change adds 13.33 MiB, the mean over all 2970 ordered pairs is 2.94 MiB,
 * and all 55 entries in one level come to 106.64 MiB (80 distinct pages).
 * The cap is therefore the bytes the cache has grown since the level began,
 * measured against the rule below.
 *
 * `why` goes into the log line. Does nothing without a built HD level. True
 * when the new body is on her now; false means it waits for the next level,
 * and the caller's footstep change has to wait with it. */
bool HDLevelRedress(const char* why);

/* What the re-dress bytes are measured against.
 *
 * A flat allowance of 64 MiB (HD_REDRESS_ALLOWANCE, the slack AutoBudgetBytes
 * holds back on the card) is too tight: with a budget of 3878 MiB and a cache
 * of 465 to 517 MiB it stops after a few changes of 4 to 13 MiB while
 * gigabytes of the budget are unused. So the rule asks the two real limits;
 * the constants and the arithmetic are in hd_memory.h:
 *
 *   the card       budgetBytes + HD_REDRESS_ALLOWANCE - the cache's bytes.
 *                  The flat 64 MiB stays as the floor, so a level that has
 *                  not spent it is never stopped for the card's sake.
 *   the address    ullAvailVirtual - HD_REDRESS_ADDRESS_RESERVE. This is a
 *                  32 bit process: four gigabytes, whatever the card holds.
 *   one block      the largest free block against one page's own size,
 *                  because an allocation is not made out of a sum.
 *
 * Measured (PoolProbe -pool): the cache's pages are D3DPOOL_DEFAULT and cost
 * the process 0.00 to 0.04 MiB of address space each - the 85.25 MiB of a
 * 4096 page in A8R8G8B8 goes on the card only. A D3DPOOL_MANAGED page would
 * cost the process its own size (85.37 MiB, in one reserve), and the engine
 * makes none. So the card's budget is the real wall; the address side is a
 * guard for the one big block a page load takes from the process (the
 * D3DPOOL_SYSTEMMEM staging texture, hd_pagecache.cpp ensure_staging) and for
 * everything else a level does with its four gigabytes.
 *
 * Example (PoolProbe -grid): a card room of 3477 MiB and, with 3000 MiB of
 * address space free, an address room of 2488 MiB - room for 124 changes of
 * the measured worst. The test is made before a change, so the ceiling is the
 * room plus one change: at most HD_REDRESS_WORST more, measured at 20.00 MiB. */

// Whether another change is allowed - what the F4 menu shows when the answer
// is no, and what HDLevelRedress asks. It is the answer stored when something
// last changed it, not a fresh measurement: the F4 window asks on every frame
// it is open, and the address-space walk is too dear for every frame.
bool HDLevelRedressAllowed();

/* Works the answer out again and stores it. The level build and every
 * completed change do that themselves; this is for a caller that changes
 * nothing and wants a fresh answer - the F4 window, once, as it opens. `why`
 * goes into the log line. Does nothing without a built HD level. */
void HDLevelRedressRefresh(const char* why);

// How many have been done in this level, for the log.
int HDLevelRedressCount();
void HDLevelUnload();

// The Direct3D device is about to be released or reset (a resolution change,
// Alt+Enter, a lost device). Every HD object lives on THAT device, so all of
// it goes now; the next level load builds it again on the new one.
void HDLevelDeviceLost();

// Whether the HD path is switched on and an installed Remaster was found.
// Nothing else in the engine may read HD data without asking this first.
bool HDEnabled();

/* Whether an HD level is built and drawing: HDEnabled() and a .TRG that had
 * rooms in it. The portal walk carries its second rectangle (the HD reach)
 * only while this is true, so a level the remaster has nothing for walks the
 * classic way. */
bool HDLevelActive();

// Whether room `roomIndex` of the level now loaded is drawn by the HD path.
// Decided once at level load, not per frame. False for every room while HD is
// off, while no HD level is built, and for every room the .TRG has nothing
// for. The static meshes of an HD room are already part of its art and must
// not be drawn a second time - that is the one other caller of this.
bool HDRoomIsHD(int roomIndex);

/* One classic water face an HD room still has to draw. The remaster's .TRG
 * has no geometry in the plane of the classic water surface in 61 of the
 * game's 93 reflecting rooms (every VCI pool among them), so an HD room would
 * show the pool floor and nothing over it. There, and only there, the level's
 * own water face is drawn over the HD room by the classic path, with its own
 * texture, animated texture range and shimmer.
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

/* The remaster's own picture of that water.
 *
 * The faces above use the classic level's animated texture range - a
 * 256-pixel tile that steps along its range every six game steps - and the
 * remaster ships an HD picture of each frame. True when it has one for the
 * tile a texinfo shows right now, and `out` is the same record with the HD
 * page and coordinates; false, and the caller draws the classic tile.
 *
 * The only caller is InsertRoomWater (specific\drawroom.cpp), inside the HD
 * branch. With hd_enable off the table is empty and the first line returns. */
struct TEXTURESTRUCT;
bool HDWaterFrameFor(const TEXTURESTRUCT* tex, TEXTURESTRUCT* out);

/* The static meshes of an HD room that still have to be drawn. Worked out
 * once at level load against the .TRG's own geometry; DrawStaticObjects
 * (game\draw.cpp) is the only caller.
 *
 * HDStaticIsClassic - the .TRG draws nothing where this static stands (21 of
 * them in ANDREA1 alone, in rooms 121 and 221, which would otherwise vanish).
 * Such a static is drawn the classic way inside an HD room; every other one
 * stays suppressed, because its art is baked into the room.
 *
 * `meshIndex` counts ROOM_INFO::mesh from zero. Both return false for a room
 * that is not HD, for an index out of range, and with HD off - a classic room
 * draws all of its statics through the ordinary path. */
bool HDStaticIsClassic(int roomIndex, int meshIndex);

// One FlipMap swap. Called from FlipMap (game\control.cpp) where the two
// ROOM_INFO records change places, once per pair. FlipMap swaps the contents
// of two rooms and keeps their numbers, so the HD art of the pair has to swap
// too, or a room goes on drawing the art of the room no longer there - a wall
// that is gone and whose HD texture still stands.
//
// Safe to call at any time: with HD off, or before a level is built, it
// returns on its first test.
void HDFlipRoom(int roomIndex, int otherIndex);

void HDFrameReset();

/* Two hand-overs for the reachart log line, both called from the room walk's
 * log note (game\draw.cpp, NoteRoomWalk) and so only with -log. They store
 * and draw nothing.
 *
 * HDFrameNoteReach keeps the rectangle the reach carried a listed room in on
 * - `box` is left, top, right, bottom, as ROOMLIST prints reach= - because
 * HDFrameRoom is handed the classic window alone. HDFrameNoteDump says that
 * a ROOMLIST whole-list dump was written for a window event, so that this
 * frame writes the reachart lines outside the clip line's throttle. */
void HDFrameNoteReach(int roomIndex, const short* box);
void HDFrameNoteDump();

/* Puts one room on this frame's list, with the window it may be seen in.
 *
 * `left/top/right/bottom` are the engine's own `ROOM_INFO::left/top/right/
 * bottom` - the rectangle the portal walk (game\draw.cpp, SetRoomBounds and
 * GetRoomBounds) worked out for this room in THIS frame, in backbuffer pixels,
 * both ends inclusive. InsertRoom copies them into clip_left..clip_bottom one
 * line further down for the classic roomlets, and the HD path turns them into
 * a scissor rectangle for the same reason: a room that overlaps another one in
 * space (ANDREA1's 39 and 53 stand in the same world coordinates) must not be
 * drawn outside the portal it is seen through, and no depth buffer can decide
 * that - both rooms really are at that depth.
 *
 * The camera's own room comes in as the whole screen, which is what the walk
 * gives it, and is drawn without a scissor at all. */
bool HDFrameRoom(int roomIndex, const float* viewWorld,
	int left, int top, int right, int bottom);

/* Sets the scissor to the window of the room an HD object stands in. Called from
 * hd_object.cpp for each item it is about to draw, with `item->room_number`;
 * -1, a room that is not on this frame's list, and the camera's own
 * full-screen room all leave the scissor test off. It is a call into this file
 * rather than a getter because the frame list, the rounding and the margin
 * live here, and there is one of each. */
void HDFrameScissorForRoom(int roomIndex);

// The sky dome. Called from SkyDrawPhase (game\draw.cpp)
// where the classic path draws its HORIZON mesh, with the matrix that is on
// the engine's stack there - the camera position, no rotation. Draws the
// .TRG's own sky group (two in TITLE) camera centred, in its own slice of the
// depth buffer, and answers true; the caller then leaves out the horizon mesh
// and the flat colour layers. False with HD off, before a level is built, and
// in a level whose .TRG has no sky group - then the classic sky draws whole.
bool HDFrameSky(const float* viewWorld);

void HDFrameDraw();
void HDFrameDrawGlow();

// The frame clock, a diagnostic. Called from DXShowFrame
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
// This is not behind hd_enable: the frame rate is a property of the whole
// engine, and a report about it has to be possible with the HD path off.
void HDFramePresented();

// Takes this frame's field of view the first time anything HD turns up in
// it - a room in InsertRoom or an item in DrawAnimatingItem. Called by both,
// works once per frame; needed because AlterFOV moves f_persp inside a frame
// while the binoculars are up.
void HDFrameCaptureProjection();

/* The three numbers HDRenderMakeProjection is fed with, after
 * HDFrameCaptureProjection has taken them: `persp` is f_mpersp,
 * `centerX`/`centerY` the screen centre this frame's one projection is built
 * about.
 *
 * The inventory ring needs them because it draws every item about a different
 * centre (specific\output.cpp, phd_PutPolygonsPickup moves f_centerx/f_centery
 * to the item's place on the ring) and the HD path has one projection for the
 * whole frame. hd_object.cpp measures the difference against these numbers and
 * hands it to HDModelOffCentre (hd_model.h), which moves the projection itself
 * for the length of one draw call.
 * Zeroes before the frame's projection has been taken. */
void HDFrameProjectionInfo(float* persp, float* centerX, float* centerY);

/* The pickup display's own bracket and projection.
 *
 * The small turning object in the corner after Lara picks something up is
 * drawn from inside S_OutputPolyList (specific\output.cpp), after HDFrameDraw
 * has drawn and emptied this frame's HD list. So it opens a bracket of its
 * own, draws its one object and closes it again, as ModelProbe does.
 *
 * Two things are its own and not the frame's, which is why this takes
 * arguments instead of reading what HDFrameCaptureProjection took:
 *
 *   The field of view. The frame's projection was taken when the world was
 *   collected; the display is drawn at the end of the frame, and AlterFOV
 *   moves f_persp in between while the binoculars are up. The classic display
 *   projects with f_mpersp at that moment (aTransformLightClipMesh), so the
 *   caller reads f_persp / f_centerx / f_centery / f_a / f_b there.
 *
 *   The depth buffer. S_OutputPolyList clears it before the display's
 *   section (the clear that separates the world from the mask, the display
 *   and the fade), so the object stands over the whole world on both paths
 *   and under everything the fade block draws afterwards.
 *
 * No fog bulbs: ActiveFogBulbs holds view-space positions of the world's
 * view, and the display draws through a view of its own (phd_LookAt in
 * S_DrawPickup). The material clock is read here and never stepped -
 * AdvanceClock belongs to FillFrameLighting, once per frame.
 *
 * Begin answers false when the renderer is not up or a bracket is already
 * open, and then nothing was opened and nothing must be closed. */
bool HDFramePickupBegin(float persp, float centreX, float centreY,
	float za, float zb);
void HDFramePickupEnd();

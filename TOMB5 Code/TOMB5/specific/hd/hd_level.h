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
// Everything behind these calls is off unless the build gate is open - the
// registry setting hd_build, 1 by default, and an installed Remaster found
// (HDEnabled). With hd_build 0 the engine reads no extra byte and draws the
// same frame as the classic engine. What is drawn, HD or classic, is the HD
// textures row (hd_enable), which the HD/SD switch flips.
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
 * 4096x4096 texture in A8R8G8B8 goes on the card only. A D3DPOOL_MANAGED
 * page would cost the process its own size (85.37 MiB, in one reserve), and
 * the engine makes none. So the card's budget is the real wall; the address
 * side is a guard for the one big block a page load takes from the process
 * (the D3DPOOL_SYSTEMMEM staging texture, hd_pagecache.cpp ensure_staging)
 * and for everything else a level does with its four gigabytes.
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

// Whether HDLevelUnload keeps the HD data for the level LoadLevel has just
// named: the same keys and the same comparison, asked before FreeLevel, so
// that LoadLevel can keep the level's classic texture pages beside them
// (specific\file.cpp, TexturePagesKeep). False with the build gate off.
bool HDLevelKeepsNext();

// What the coming load does with the HD data (HDLevelKeyEarly).
struct HDLevelNextLoad
{
	int gate;	// the build gate is open: hd_build and an installation
	int art;	// the level's .TRG is there
	int kept;	// the data in memory are of this level: no build
};

// The key of the level about to be loaded, stamped before the loading
// thread starts (specific\file.cpp, S_LoadLevelFile: whether the row off
// shows a screen) and kept for LoadLevel's HDLevelKeyNext, which takes it
// for the same file instead of stamping again: one key for the screen and
// the keep. With the build gate off it stamps nothing and `out` is zero.
void HDLevelKeyEarly(const char* wadName, HDLevelNextLoad* out);

// The Direct3D device is about to be released or reset (a resolution change,
// Alt+Enter, a lost device). Every HD object lives on THAT device, so all of
// it goes now; HDLevelDeviceRecreated builds it again on the new one.
void HDLevelDeviceLost();

// A new device in place of the old one (DXCreate on Alt+Enter or a
// resolution change, after the classic pages have moved): the level that
// was drawing HD is built again from what is still loaded, and the HD font
// with it. Returns whether the level draws HD again.
bool HDLevelDeviceRecreated();

// The build gate: hd_build on and an installed Remaster found. Nothing else
// in the engine may read or build HD data without asking this first. The
// draw - the HD textures row, hd_enable - is not part of it.
bool HDEnabled();

// Whether the level on screen was built with HD rooms, whatever is drawn:
// the HD/SD switch's question whether there is anything to switch.
bool HDLevelBuilt();

/* Whether an HD level is built and drawn HD now: a .TRG that had rooms in
 * it, built past HDEnabled(), and the draw at HD (hd_switch.h, HDDrawn).
 * The draw is read on every call, so the HD textures row and the switch
 * change the answer in a running level. The portal walk carries its second
 * rectangle (the HD reach) only while this is true, so a level the remaster
 * has nothing for, and a level drawn classic, walks the classic way. */
bool HDLevelActive();

/* The level's name for a log line of the main thread: its file name without
 * folder or extension, as FPSLOG prints it, whatever hd_build and hd_enable
 * say - set at every level build, "?" before the first. */
const char* HDLevelLogName();

/* A mirror in a room's wall, as the game side needs it: the room, the plane
 * (`axis` 0 for a plane of constant x, 2 for constant z, `plane` its world
 * coordinate), `side` -1 when the copy behind the glass lies at smaller
 * coordinates than the plane and +1 when at larger, and the copy's box in
 * world units. hd_render.h, HDMirror, says where the rule comes from; none
 * exists while HDLevelActive() is false. */
struct HDLevelMirror
{
	int room;
	int axis;
	int side;
	long plane;
	long mins[3];
	long maxs[3];
};

int HDLevelMirrorCount();
bool HDLevelMirrorAt(int at, HDLevelMirror* out);

// Whether room `roomIndex` of the level now loaded is drawn by its own group
// of HD art. The groups are decided at level load; the answer asks
// HDLevelActive on every call. False for every room while HD is off or the
// world is drawn classic, while no HD level is built, for every room the
// .TRG has nothing for, and for a room drawn as another group's art
// (hd_cover.h) - HDRoomShadowHD, HDRoomArtListed and HDStaticDrawnClassic
// answer for such a room what is drawn there.
bool HDRoomIsHD(int roomIndex);

/* Whether the art draws room `roomIndex` in a frame whose room list is
 * `listed`: a room drawn HD, and a room drawn wholly as its carrier's art
 * while a listed room is drawn by the carrier. The room walk (game\draw.cpp,
 * ReachTrimRooms) asks it of a room the reach lists with no window: such a
 * room shows only its art, and one the art does not draw comes off the list.
 * A room drawn as art in part keeps classic faces the empty window cannot
 * show, and listed it would leave out the carrier's art that lies whole on
 * them, so it answers false. False for every room while HDLevelActive() is
 * false. */
bool HDRoomArtListed(int roomIndex, const short* listed, int listedCount);

/* The rooms whose art stands under room `roomIndex`'s sky (HDCarryBoxHolds
 * in hd_render.h): their count, and in `*rooms` the room numbers. The room
 * walk (game\draw.cpp, CarryArt) takes them in with a room it lists with a
 * classic window. None with tomb5.hd_air 0, for a room not under the sky,
 * and for every room while HDLevelActive() is false. */
int HDRoomCarries(int roomIndex, const short** rooms);

/* The room that draws the carried art of room `group` - a number out of
 * HDRoomCarries, which keeps the numbers of the level as built - in the
 * flipmap state of this moment: the room itself when it has no twin; of a
 * pair, the one of the two that the portals lead to, when the flip state
 * has it drawn by that group; -1 when the group's art is out of the world
 * in this state (FlipMap swapped its room away). */
int HDCarriedRoom(int group);

/* The visibility path (tomb5.hd_vis 1, hd_vis.h), as the room walk
 * (game\draw.cpp) asks it. HDVisWalkOn: the level was built with the path
 * and HDLevelActive() holds - the walk then extends the portals' list and
 * the reach is off. The tables of the group room `roomIndex` holds now:
 * its guests, the sky table's groups after them, and its openings (the
 * count, the entries in `*out`) and the wide opening round its portal
 * `portal` (null for none). And the room
 * that draws group `group` in the flipmap state of this moment, -1 while
 * the group is out of the world (as HDCarriedRoom). The walk asks the four
 * only while HDVisWalkOn() holds; with no tables they answer 0 or null. */
struct HDVisGuest;
struct HDVisOpening;
struct HDVisWide;
bool HDVisWalkOn();
int HDVisRoomGuests(int roomIndex, const HDVisGuest** out);
int HDVisRoomOpenings(int roomIndex, const HDVisOpening** out);
const HDVisWide* HDVisRoomWide(int roomIndex, int portal);
int HDVisRoomOfGroup(int group);

/* The classic faces InsertRoomlet (specific\drawroom.cpp) leaves out of one
 * roomlet of a room drawn classic that another group's art covers in part:
 * one flag per face in the order of its loops - the quads, then the
 * triangles - 1 for a face the art draws instead (hd_cover.h,
 * HDCoverRoomState). Null at once with hd_enable off, before a level is
 * built and for every room without such a face, so the classic path asks one
 * pointer per roomlet. */
const unsigned char* HDRoomletDropped(int roomIndex, int roomlet);

/* Whether the room Lara is in counts as HD for her projected shadow
 * (hd_shadow.h, HDShadowGate): a room drawn HD, a room drawn as another
 * group's art - wholly or in part - that keeps no classic face she can
 * stand on (hd_cover.h, HDCoverFaceFloor), or a room drawn classic that has
 * no classic face at all and no sector she can stand on bare of other
 * groups' art (HDCoverRoomShadowHD's `bareFloor`). In each of them whatever
 * is under her is art that takes the receive. False for every room while
 * HDLevelActive() is false. */
bool HDRoomShadowHD(int roomIndex);

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

/* Whether the classic sprite of a ripple (x, y, z and flags of a
 * RIPPLE_STRUCT) is left out because a water sheet draws its ring there:
 * HD on, tomb5.hd_motion on, a ripple of the kind the
 * rings are made of and a drawing sheet under it (hd_level.cpp,
 * RippleIsRing). The only caller is S_DrawSplashes
 * (specific\specificfx.cpp); with hd_enable off it is false at its first
 * test. */
bool HDWaterRippleOnSheet(long x, long y, long z, int flags);

/* The drops where a waterfall lands on a water sheet (hd_level.cpp; the
 * feet are hd_render.cpp's, "the foot of a waterfall"), once a logic tick:
 * the only caller is the tick body in game\control.cpp, beside
 * AnimateWaterfalls. With hd_enable off it returns at its first test. */
void HDWaterFallsTick();

/* The water's own splash and a fish tank's spray (hd_render.cpp, "the
 * cards"). HDWaterSplash is SetupSplash's (game\effect2.cpp), after the
 * classic struct is filled: true when the HD form takes the splash - HD
 * on, tomb5.hd_motion on, the cards ready and a drawing
 * sheet under it -, and S_DrawSplashes then leaves that splash's sprites
 * out (flags bit 0x10). HDWaterSpray is TriggerFishtankSpray's
 * (game\tower.cpp), after the classic spark is made: true when the cards
 * take it, and S_DrawSparks leaves the spark out (SF_UNUSED2). Neither
 * writes the game's state or takes its random numbers: the splash's timers
 * and the spark run as before. With hd_enable off both are false at their
 * first test. */
struct SPLASH_SETUP;
bool HDWaterSplash(const SPLASH_SETUP* setup);
bool HDWaterSpray(long x, long y, long z, long strength);

/* A breaking fish tank's water (hd_render.cpp, "the tank's water"):
 * DrawScaledSpike's (game\traps.cpp) for a FISHTANK, before the classic
 * mesh is drawn - true when the HD path draws this tank's water this
 * frame (HD on, tomb5.hd_motion on, the cards ready
 * and the level's FISHTANK mesh known), and the classic mesh is then left
 * out. Reads the item only. With hd_enable off it is false at its first
 * test. */
struct ITEM_INFO;
bool HDWaterTank(const ITEM_INFO* item);

/* The destructible parts of the art a breaking static is drawn by, for
 * the pieces it breaks into (hd_debris.h; ShatterObject, game\debris.cpp,
 * asks before it walks the faces): the first `max` into `out`, each with
 * its group, its material and that room's origin. Returns how many there
 * are - 0 with the world drawn classic or for a static with none, whose
 * pieces stay classic. `listRoom` and `listIndex`, when given, take the
 * room whose MESH_INFO list holds the static now and its place there. */
struct MESH_INFO;
struct HDDebrisPart;
int HDStaticBreakParts(const MESH_INFO* mesh, HDDebrisPart* out, int max,
	int* listRoom, int* listIndex);

/* Whether a frame draws a static mesh the classic way. Worked out once at
 * level load against the .TRG's own geometry (hd_level.cpp,
 * BuildStaticTables; the rule: hd_cover.h, HDStaticAnswer);
 * DrawStaticObjects (game\draw.cpp) is the only caller.
 *
 * False where the art draws the static's place in every frame that lists its
 * room - its room's own group, or the carrier's art of a room drawn as it -
 * and where another group's art draws it and `listed`, the frame's room list
 * (draw_rooms), holds a room drawn by that group. False too where other
 * groups hold it, none of them is listed and its piece - the holder's
 * triangles of the object - is drawn this frame in its room's window; the
 * call notes that draw for HDFrameDraw (hd_cover.h, HDStaticDrawSource). True
 * where the art has nothing for it (103 of the game's 2,845 statics, which
 * would otherwise vanish), where other groups hold it but none is listed and
 * it has no piece or its piece cannot be drawn, for an index out of range,
 * before a level is built, in a level built with HD off, and while the
 * world is drawn classic. It reads the level as built and asks
 * HDLevelActive, like HDRoomIsHD, so the HD textures row and the HD/SD
 * switch change the answer from the next frame on.
 *
 * `meshIndex` counts ROOM_INFO::mesh from zero. */
bool HDStaticDrawnClassic(int roomIndex, int meshIndex, const short* listed,
	int listedCount);

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

// Whether the HD sky just drawn stands for the classic flat layers; where it
// does not (JOBY4's band alone), SkyDrawPhase draws the script's layers over
// it as the classic path does.
bool HDFrameSkyCoversLayers();

// The HD sky's nearest cloud sheet page lent to the classic sky page's slot
// for the layers' one OutputSky (true), and given back (false). Answers
// whether a page was lent; nothing is lent where the sky has no sheet. A lent
// page is drawn on the flat layer's quads, whatever PSX_skies says.
bool HDFrameSkyLayerPage(bool lend);

void HDFrameDraw();
void HDFrameDrawGlow();

/* The projected shadow's passes, as HDFrameDraw runs them: her map's cast
 * pass, the second map's cast pass and then the receive pass, after the room
 * loops and before the models' solid passes, and the models' pass after
 * those, in frames whose Lara main draw decided it runs (hd_laradraw.h,
 * HDShadowFrameDecide). Counted since the last call with `clear` set:
 *
 *   frames   frames in which the projected shadow ran
 *   cast     of those, frames in which her map was drawn
 *   parts    models drawn into her map, all frames together
 *   ms       processor time of her map's cast pass, milliseconds, all frames
 *            together
 *   rooms    room groups drawn again by the receive pass, all frames
 *            together
 *   calls    draw calls of the receive pass, all frames together
 *   receiveMs  processor time of the receive pass, milliseconds, all frames
 *            together
 *   othersCast   frames in which the second map was drawn
 *   othersItems  items drawn into the second map, all frames together
 *   othersModels models drawn into the second map (a body and a separate
 *            face are two), all frames together
 *   othersMs     processor time of the second map's cast pass,
 *            milliseconds, all frames together
 *   othersRooms  room groups drawn again by the receive pass in frames whose
 *            second map was read, all those frames together
 *   models   entries of the frame list drawn again by the models' pass, all
 *            frames together
 *   modelCalls  draw calls of the models' pass, all frames together
 *   castOnly items cast into the second map that the picture clipped away
 *            (drawn in no pass), all frames together
 *   mirror   frames in which a mirror's image took the maps through its
 *            mirror's blocks - a mirror's room group or a reflection
 *   modelsMs processor time of the models' pass, milliseconds, all frames
 *            together
 *   arriving frames in which the shadow was still coming in (hd_shadow.h,
 *            HDShadowArrive: its presence below 1) */
struct HDShadowCastStats
{
	int frames;
	int cast;
	int parts;
	double ms;
	int rooms;
	int calls;
	double receiveMs;
	int othersCast;
	int othersItems;
	int othersModels;
	double othersMs;
	int othersRooms;
	int models;
	int modelCalls;
	int castOnly;
	int mirror;
	double modelsMs;
	int arriving;
};

void HDFrameShadowCastStats(HDShadowCastStats* out, bool clear);

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
long HDFramesPresented();	// frames since the start - the hang watch's pulse

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

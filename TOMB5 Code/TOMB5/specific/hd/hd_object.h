/*
 * Modified 2026 by ChrisChanSan.
 *
 * The routing rules below - one .TRM replaces the WHOLE classic model, a node
 * the model does not cover is drawn by NOBODY, and a swap mesh on a covered
 * node loses to the model - are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3, host\render\game_renderer.cpp: the item bracket
 * 1440-1520, `model.covered = ~0u` at 1826-1830, the swap-key rule at
 * 3800-3812 and 4170-4180). See THIRD-PARTY.md.
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

#include "hd_model.h"		// HDObjLight, for the two calls at the bottom

struct ITEM_INFO;
struct OBJECT_INFO;

// THE SEAM BETWEEN DrawAnimatingItem AND THE HD OBJECT PATH (#123, stage S3-2).
//
// The room seam (hd_level.h) could be one line in InsertRoom because a room is
// one thing with one matrix. An object is a mesh tree: the engine walks it,
// pushes and pops a matrix per node, interpolates between two frames and calls
// phd_PutPolygons once per node. The HD model wants exactly those matrices and
// nothing else - so this module rides along on that walk instead of repeating
// it, and the animation, the mesh swaps and gar_RotYXZsuperpack stay the
// game's own.
//
//   HDObjectBegin  after the item's own translation and rotation are on the
//                  stack. Answers whether this item is drawn HD. False and
//                  DrawAnimatingItem carries on exactly as it did.
//   HDObjectNode   in place of phd_PutPolygons, once per node, in the same
//                  place and under the same matrix. Records it.
//   HDObjectEnd    after the node loop. Works out the light and puts the item
//                  on the frame's list.
//
// HDObjectFrameDraw then draws the whole list inside the bracket hd_level.cpp
// already opened for the rooms - one HD bracket a frame, one list of device
// states, drawn at the one place the classic world is drawn.
//
// Everything here is switched off unless hd_enable AND hd_objects are on and
// the level really built an HD side; HDObjectBegin says no at its first line
// otherwise and not a byte of this is entered.

// The level bracket, called from hd_level.cpp.
//
//   HDObjectLevelBuild  inside HDLevelBuild, AFTER HDRenderLevelBegin - the
//                       models' pages are appended to the cache the rooms
//                       built, and a slot number past the end of an emptied
//                       cache is an untextured object and a long afternoon.
//   HDObjectRelease     device work, MAIN THREAD, immediately before
//                       HDRenderLevelEnd empties that cache.
//   HDObjectLevelEnd    inside HDLevelUnload, which runs on the LOAD thread:
//                       flags only, not one Direct3D call. Same rule and same
//                       reason as the rooms.
//   HDObjectDeviceLost  the device is going: release and forget it.
void HDObjectLevelBuild(const char* installRoot, const char* stem);
void HDObjectRelease();
void HDObjectLevelEnd();
void HDObjectDeviceLost();

// Whether a level's HD objects are up at all.
bool HDObjectActive();

// --- the frame -------------------------------------------------------------

void HDObjectFrameReset();

// Is this item drawn HD? Decided per SLOT at level build (the skeleton has to
// fit the model) and per ITEM here (status, the mesh bits). When it answers
// true the caller must not put a single classic polygon in for this item, and
// must call HDObjectEnd() when its node walk is over.
bool HDObjectBegin(ITEM_INFO* item, OBJECT_INFO* obj);

// One node's matrix, taken from the engine's own float stack. `interpolated`
// is the frac branch of DrawAnimatingItem: the matrix that draws there is the
// INTERPOLATED one, which phd_PutPolygons_I makes inside a push of its own -
// so this does the same push and the same interpolation.
void HDObjectNode(int node, bool interpolated);

void HDObjectEnd();

// How many items are waiting. HDFrameDraw asks before it opens the bracket.
int HDObjectFrameCount();

// Inside hd_level.cpp's HD bracket, after the rooms. `projection` is the very
// matrix the rooms were drawn with and `bulbs` this frame's fog bulb count,
// which hd_render already put in the shared registers.
void HDObjectFrameDraw(const void* projection, int bulbs);

// The draw calls the last HDObjectFrameDraw made, for the frame line.
int HDObjectDrawCalls();

// --- what Lara borrows (#123, stage S4-2) ----------------------------------
//
// Lara is not an item and not a slot: she is up to nine models on ONE set of
// matrices, and hd_laradraw.cpp puts her together. It does not need a second
// frame list, a second light conversion or a second bind walk - it needs
// these three, which are the ones this file already had for the objects. The
// draw is then the same draw: her parts sit on the same list, are drawn in
// the same bracket by HDObjectFrameDraw, and are counted in the same line.
//
// The ORDER that follows from it is the classic one and not a choice.
// game\draw.cpp calls DrawLara before the PrintObjects loop, so Lara's parts
// go on the list before the room's items and come off it before them too.

// The slot's classic BIND POSE out of the engine's own mesh tree, three
// floats a node in node order - the walk DrawAnimatingItem does with every
// rotation left out. hd_laradraw needs it for LARA (the body) and HAIR (the
// braid); an ordinary slot gets it from HDObjectLevelBuild itself.
void HDObjectBindPose(int slot, float* out /* nodes * 3 */, int nodes);

// The item's lights in VIEW space - the very conversion an HD object gets,
// suns first, then the dynamics, then the room's points and spots. Returns
// how many were written; `sunScale` comes back with the inventory's doubling
// already in it. `lit` is the item CalculateObjectLighting last worked on,
// which for Lara is lara_item.
int HDObjectLights(ITEM_INFO* lit, HDObjLight* out, int max, float* sunScale);

/* ONE MODEL ON THIS FRAME'S LIST that is not an item. `matrix` is `nodes`
 * row-major 4x4 matrices in view space, `light` a colour triple a node in the
 * classic 0..255 space, `mask` the mesh-bits rule (bit n clear -> node n
 * draws nothing), `nearCut` the first person near cut of stage S8 in view
 * units and `cull` the one-sided draw that goes with it (both 0 for
 * everything but Lara's body in first person; hd_lara.h has the number and
 * hd_model.h the reason the two belong together).
 *
 * False when the list is full or the HD model path is not up - and then the
 * caller has to draw nothing at all rather than half a Lara, which is why
 * hd_laradraw asks HDLaraDrawActive() once for the whole composite instead of
 * per part. */
bool HDObjectPush(int slot, const float* matrix, int nodes, const float* light,
	unsigned int mask, const HDObjLight* lights, int lightCount, float sunScale,
	float nearCut = 0.0f, int cull = HD_MODEL_CULL_NONE);

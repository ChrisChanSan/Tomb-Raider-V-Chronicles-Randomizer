/*
 * Modified 2026 by ChrisChanSan.
 *
 * The routing rules below - one .TRM replaces the whole classic model, a node
 * the model does not cover is drawn by nobody, and a swap mesh on a covered
 * node loses to the model - are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3, host\render\game_renderer.cpp: the item bracket
 * 1440-1520, `model.covered = ~0u` at 1826-1830, the swap-key rule at
 * 3800-3812 and 4170-4180). So is the idea of folding the inventory ring's
 * off-centre projection into the item for the length of one draw
 * (games\tr5\glue\hostglue_2d5.cpp:502-506) - the idea alone: TROne shears the
 * item's matrix with it and this engine may not, because its shader takes the
 * vertex normal through that matrix (see HDInvNode). The offset goes into the
 * projection instead, and the arithmetic is derived from TR5's own projection
 * line. See THIRD-PARTY.md.
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

// The seam between DrawAnimatingItem and the HD object path.
//
// A room is one thing with one matrix; an object is a mesh tree: the engine
// walks it, pushes and pops a matrix per node, interpolates between two frames
// and calls phd_PutPolygons once per node. The HD model wants exactly those
// matrices and nothing else - so this module rides along on that walk instead
// of repeating it, and the animation, the mesh swaps and gar_RotYXZsuperpack
// stay the game's own.
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
// Everything here is switched off unless hd_enable and hd_objects are on and
// the level really built an HD side; HDObjectBegin says no at its first line
// otherwise.

// The level bracket, called from hd_level.cpp.
//
//   HDObjectLevelBuild  inside HDLevelBuild, after HDRenderLevelBegin - the
//                       models' pages are appended to the cache the rooms
//                       built, and a slot number past the end of an emptied
//                       cache is an untextured object.
//   HDObjectRelease     device work, main thread, immediately before
//                       HDRenderLevelEnd empties that cache.
//   HDObjectLevelEnd    inside HDLevelUnload, which runs on the load thread:
//                       flags only, not one Direct3D call, as for the rooms.
//   HDObjectDeviceLost  the device is going: release and forget it.
void HDObjectLevelBuild(const char* installRoot, const char* stem);
void HDObjectRelease();
void HDObjectLevelEnd();
void HDObjectDeviceLost();

// Whether a level's HD objects are up at all.
bool HDObjectActive();

// --- the frame -------------------------------------------------------------

void HDObjectFrameReset();

// Whether this item is drawn HD. Decided per slot at level build (the skeleton
// has to fit the model) and per item here (status, the mesh bits). When it
// answers true the caller must not put a single classic polygon in for this
// item, and must call HDObjectEnd() when its node walk is over.
bool HDObjectBegin(ITEM_INFO* item, OBJECT_INFO* obj);

// One node's matrix, taken from the engine's own float stack. `interpolated`
// is the frac branch of DrawAnimatingItem: the matrix that draws there is the
// interpolated one, which phd_PutPolygons_I makes inside a push of its own -
// so this does the same push and the same interpolation.
void HDObjectNode(int node, bool interpolated);

void HDObjectEnd();

// --- the inventory ring and the pickup display ------------------------------
//
// The ring is the same seam once more, not a second render path:
// game\newinv2.cpp's DrawInventoryItemMe walks a mesh tree exactly as
// DrawAnimatingItem does - a matrix per node, item->mesh_bits deciding which
// node draws - and only the last step differs, phd_PutPolygonsPickup instead of
// phd_PutPolygons. So the three calls below are HDObjectBegin/Node/End with
// three differences, each a property of the ring:
//
//   1. The off-centre projection. The ring draws every item about its own
//      place on the screen: phd_PutPolygonsPickup moves f_centerx/f_centery to
//      the item's x/y for the length of one mesh (specific\output.cpp) and puts
//      it back afterwards. The HD path has one projection per frame
//      (hd_level.cpp, HDRenderMakeProjection out of g_persp/g_centerx/
//      g_centery), so the item carries the difference as two numbers and
//      HDModelOffCentre (hd_model.h) moves the projection for the length of
//      one draw call - never the item's matrices, which the shader also skins
//      the normals with. The derivation stands over HDInvNode in
//      hd_object.cpp. The idea of folding it into the item at all is TROne's
//      (Copyright 2026 Todesengel, GPLv3,
//      games\tr5\glue\hostglue_2d5.cpp:502-506); the way and the arithmetic
//      are this engine's own and are derived from the classic formula, not
//      copied.
//   2. The light. No room, no room cube, no point lights, no dynamics: the
//      classic ring lights an item with one sun of its own and an ambient of
//      8/8/8 (phd_PutPolygonsPickup again), and that is what travels.
//   3. No animation. The ring stands on frame 0 of the slot's first animation
//      and never interpolates, so there is no `interpolated` argument.
//
// `x`/`y` are the item's place on the ring - newinv2.cpp's own xoffset/yoffset,
// which is what it hands phd_PutPolygonsPickup - and `color` its pcbright, the
// ring's own brightness for this item.
//
// Answers false for an item with no HD model and for a slot whose skeleton the
// model does not fit: the caller then draws the classic mesh and the ring has
// no hole in it.
//
// The pickup display takes the same three calls through the same four call
// sites: DrawThreeDeeObject2D has a second caller, S_DrawPickup
// (specific\output.cpp), the little turning object in the corner after Lara
// picks something up. It walks the identical mesh tree; what differs is when.
//
// S_DrawPickup runs inside S_OutputPolyList, after HDFrameDraw has drawn this
// frame's HD list and after HDFrameDrawGlow has finished with it, so an item
// pushed onto that list would suppress its own classic polygons and be drawn
// by nobody. HDInvEnd therefore draws it on the spot, in a bracket of its own
// (hd_level.cpp, HDFramePickupBegin / HDFramePickupEnd), and the frame's list
// is never touched. InventoryActive is the one test that tells the two callers
// apart.
//
// Three things are the display's own rather than the ring's: the projection
// is read at the call out of f_persp / f_centerx / f_centery (the frame's
// captured one belongs to the view the world was collected with, which a
// binocular zoom can have moved since); the sun scale is 0.75 and not 2.0,
// because aTransformLightClipMesh doubles a sun only while InventoryActive and
// the display is drawn with it at 0; and the depth needs nothing, because
// S_OutputPolyList clears the buffer before the display's block and the fade
// comes after it.
bool HDInvBegin(ITEM_INFO* item, OBJECT_INFO* obj, float x, float y, long color);
void HDInvNode(int node);
void HDInvEnd();

// True when this frame's HD items were collected from the ring. hd_level.cpp
// asks before it hands the frame its fog bulbs: a bulb is a world position
// already turned into view space, and the ring's view is the identity, so the
// bulbs of the room Lara stands in would sit in front of her medipacks.
bool HDObjectRingFrame();

// How many items are waiting. HDFrameDraw asks before it opens the bracket.
int HDObjectFrameCount();

/* Which camera the frame's objects were collected with: the lowest and the
 * highest ViewSerial (specific\3dmath.h) over the frame list. For the log
 * alone - a spread says the picture was collected across a view rebuild,
 * which is what a seam that moves with the animation looks like. */
void HDObjectViewSerials(long* lo, long* hi);

// Inside hd_level.cpp's HD bracket, after the rooms. `projection` is the very
// matrix the rooms were drawn with and `bulbs` this frame's fog bulb count,
// which hd_render already put in the shared registers.
//
// `passes` is one of the HD_PASS_ masks of hd_render.h. A frame calls this
// twice - HD_PASS_SOLID beside the rooms' solid
// passes, HD_PASS_ADDITIVE after the classic buckets - and the list stays
// where it is until HDObjectFrameEnd empties it, so both phases see the same
// items. The draw calls of the phases add up.
void HDObjectFrameDraw(const void* projection, int bulbs,
	unsigned int passes = HD_PASS_ALL);

// The frame's list is done with: the log line, and the list emptied. Called
// once, after the last HDObjectFrameDraw of the frame.
void HDObjectFrameEnd();

// The draw calls the last frame's HDObjectFrameDraw calls made, both phases
// together, for the frame line.
int HDObjectDrawCalls();

// --- what Lara borrows ------------------------------------------------------
//
// Lara is not an item and not a slot: she is up to nine models on one set of
// matrices, and hd_laradraw.cpp puts her together. It does not need a second
// frame list, a second light conversion or a second bind walk - it uses these
// three, the same ones the objects use. Her parts sit on the same list, are
// drawn in the same bracket by HDObjectFrameDraw, and are counted in the same
// line.
//
// The order follows the classic one: game\draw.cpp calls DrawLara before the
// PrintObjects loop, so Lara's parts go on the list before the room's items
// and come off it before them too.

// The slot's classic bind pose out of the engine's own mesh tree, three
// floats a node in node order - the walk DrawAnimatingItem does with every
// rotation left out. hd_laradraw needs it for LARA (the body) and HAIR (the
// braid); an ordinary slot gets it from HDObjectLevelBuild itself.
void HDObjectBindPose(int slot, float* out /* nodes * 3 */, int nodes);

// The item's lights in view space - the very conversion an HD object gets,
// suns first, then the dynamics, then the room's points and spots. Returns
// how many were written; `sunScale` comes back with the inventory's doubling
// already in it. `lit` is the item CalculateObjectLighting last worked on,
// which for Lara is lara_item.
int HDObjectLights(ITEM_INFO* lit, HDObjLight* out, int max, float* sunScale);

/* One model on this frame's list that is not an item. `matrix` is `nodes`
 * row-major 4x4 matrices in view space, `light` a colour triple a node in the
 * classic 0..255 space, `mask` the mesh-bits rule (bit n clear -> node n
 * draws nothing), `cut` the first-person neck cut - a cylinder about the neck
 * axis in the bind pose - and `cull` the one-sided draw that goes with it
 * (both empty for everything but Lara's body in first person; hd_lara.h has
 * the numbers and hd_model.h the reason the two belong together).
 *
 * False when the list is full or the HD model path is not up - and then the
 * caller has to draw nothing at all rather than half a Lara, which is why
 * hd_laradraw asks HDLaraDrawActive() once for the whole composite instead of
 * per part. */
bool HDObjectPush(int slot, const float* matrix, int nodes, const float* light,
	unsigned int mask, const HDObjLight* lights, int lightCount, float sunScale,
	const HDModelNeckCut* cut = 0, int cull = HD_MODEL_CULL_NONE,
	float specStrength = 0.0f, float specPower = 1.0f);

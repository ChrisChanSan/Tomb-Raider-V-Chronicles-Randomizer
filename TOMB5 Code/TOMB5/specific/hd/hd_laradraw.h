/*
 * Modified 2026 by ChrisChanSan.
 *
 * The composite itself - which files an outfit is made of, what each covers
 * and which of them a frame needs - is hd_lara.cpp and comes from TROne
 * (Copyright 2026 Todesengel, GPLv3); see hd_lara.h and THIRD-PARTY.md. This
 * file is the SEAM into this engine and is new: TROne has its own renderer
 * and its own Lara state, this one reads TR5's lara_matrices, lara.mesh_ptrs,
 * hairs[0] and LaraNodeAmbient and hands them over.
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

#include "hd_lara.h"

// The seam between DrawLara and the HD model path.
//
// hd_lara.cpp knows the wardrobe and nothing about this engine - it is built
// into ModelProbe as well, which is how the pictures were made. This file is
// the other half: it reads the engine's own state and turns it into the
// arguments hd_lara and hd_model want.
//
//   the skeleton   lara_matrices, fifteen 3x4 matrices in VIEW space, already
//                  built this frame by CalcLaraMatrices. They are in LMX_
//                  order and a .TRM's bone numbers are in LM_ order, so the
//                  one table in the .cpp turns the one into the other.
//   the parts      lara.mesh_ptrs[LM_RHAND] / [LM_LHAND] say which slot owns
//                  each hand THIS frame - as a pointer, so the seam compares
//                  it against meshes[objects[s].mesh_index + 2 * LM_xHAND]
//                  for Lara's own slots, which is what game\larafire.cpp
//                  does itself. LHolster, lara.holster and lara.back_gun are
//                  already slot numbers.
//   the braid      hairs[0][0..5], six links the simulation writes as
//                  ABSOLUTE world positions and angles.
//   the light      CalculateObjectLightingLara has just run; the ambient is
//                  per node through LaraNodeAmbient and the light list is the
//                  one an HD object gets (hd_object.h, HDObjectLights).
//
// What stays classic, and why, all of it visible in the log:
//
//   young Lara            stays classic; there is no HD wardrobe entry for
//                         her. Recognised three ways over, because the
//                         randomizer moves outfits between levels.
//   LARA_PONYTAIL         the randomizer writes that outfit byte BOTH for the
//                         Rome body inside a Russia level AND for every Tomb
//                         Raider III guest, and nothing else in the level
//                         tells the two apart. Until a distinguishing marker
//                         is added to the level header, such a level draws
//                         the classic Lara - a right classic body beats a
//                         wrong HD one.
//   the skeleton cheat    lara.skelebob goes through DrawLara__6, which draws
//                         the classic X-ray body only where the composite
//                         has no X-ray set to draw - see HDLaraDrawXray.
//   the muzzle flash,     drawn by their own routines outside DrawLara
//   the gunshells,        (game\draw.cpp, SetGunFlash / DrawGunshells) and
//   the flare in flight   never part of the composite.
//   the diving suit's     LARA_EXTRA_MESH1, twice, with subsuit.XRot - they
//   two thrusters         are a separate OBJECT and not a node of her body,
//                         and no .TRM is a model of them alone, so they stay
//                         the classic mesh over whatever HD body she wears -
//                         moved along her torso's z to keep the classic
//                         suit's place against that body's back (hd_lara.h,
//                         HDLaraThrusters; HDLaraThrusterShift below).
//   the inventory         newinv2.cpp draws its own ring of objects with
//                         DrawInventoryItem and never calls DrawLara.
//
// The switch is hd_objects, the same one the creatures use. Lara is a model
// like the rest of them and a level's models are one buffer; a second key
// would be a second way to get half a picture.

// --- the level -------------------------------------------------------------
//
// Both are called from hd_object.cpp, inside its own level bracket, because
// the wardrobe rides in the SAME HDModelLevelBegin as the objects.
//
//   HDLaraDrawPlan   before it: the parts this level wants, as HDModelSlot
//                    entries to be appended to the caller's list. Returns how
//                    many were written, 0 for a level whose Lara stays
//                    classic. The bind poses it points at live here and stay
//                    valid until HDLaraDrawEnd.
//   HDLaraDrawBuild  after it: what really arrived, the drift, the log lines.
//   HDLaraDrawEnd    flags only - no Direct3D call, because hd_object.cpp's
//                    own level end runs on the LOAD thread.
int HDLaraDrawPlan(HDModelSlot* out, int max);
void HDLaraDrawBuild(const char* stem);
void HDLaraDrawEnd();

// Whether Lara is drawn HD at all. DrawLara asks this once per call and then
// either draws the whole composite or the whole classic body - never a mix.
bool HDLaraDrawActive();

/* Whether the skeleton cheat's frame is drawn HD: HDLaraDrawActive, and the
 * composite has the X-ray set - her outfit is the X-ray body, or the level's
 * floor carries a SKELETON trigger and the set's body is on the card
 * (hd_lara.h, HDLaraXrayReady). DrawLara__6 asks it once per call, as the
 * other three ask HDLaraDrawActive: either the composite draws the X-ray
 * set through HDLaraDrawFrame, or the classic X-ray body is drawn whole.
 *
 * The set is read only in a level whose floor can X-ray her, so a level
 * without a SKELETON trigger loads nothing more than before. */
bool HDLaraDrawXray();

/* Which outfit this level ended up in, as the
 * HD_LARA_OUTFIT_* number outfit_of_level worked out - the registry key
 * where the player set one, the level's own facts where he did not, and
 * HD_LARA_OUTFIT_NONE when Lara is drawn classic here. The footsteps ask
 * it (hd_feet.h): what she walks on follows the body she is wearing, and
 * only this call knows which body that is. */
int HDLaraDrawOutfit();

/* The diving suit's thrusters (hd_lara.h, HDLaraThrusters): how far along
 * the torso's z DrawLara__5 moves its two units, and game\subsuit.cpp the
 * mist out of their nozzles, to keep the classic suit's place against the
 * back of the body she wears - the level build's measurement while the
 * composite draws her (HDLaraDrawActive), and 0 whenever she is drawn
 * classic, so the classic suit is drawn and blown exactly as before. */
long HDLaraThrusterShift();

/* The drawn braid's roll: six angles for hairs[0][0..5] - one per drawn bone,
 * about the bone's own axis, so no position and no direction changes at all.
 * Why there has to be one is at HD_LARA_BRAID_ROLL_NEAR in hd_lara.h.
 *
 * Two calls and not one, because the rule holds a value from frame to frame
 * and a held value must be advanced exactly once:
 *
 *   HDLaraBraidRollStep   game\hair.cpp calls this at the end of HairControl,
 *                         which is the one place per frame where the chain is
 *                         new. Nothing else may call it.
 *   HDLaraBraidRolls      reads what that worked out. Pure, so the draw and
 *                         HAIRLOG can both ask, and the mirrored Lara asks a
 *                         second time in the same frame without moving
 *                         anything on. Fills six angles, returns how many.
 *
 * It is public because HAIRLOG prints what it did: diagnosing a braid that
 * trembles again needs to be able to say whether the yaw jumped and whether
 * this took the jump out of the picture. */
void HDLaraBraidReset();
void HDLaraBraidRollStep();
int HDLaraBraidRolls(short* out);

// --- the frame -------------------------------------------------------------

void HDLaraDrawFrameReset();

/* The seam itself, called from the three DrawLara variants in place of the
 * skin loop, the joints, the braid, the holsters and the weapon on the back.
 * It puts Lara's parts on the frame list hd_object.cpp keeps, and they are
 * drawn where every HD model is drawn.
 *
 * And from DrawLara__6 while lara.skelebob holds (HDLaraDrawXray): then the
 * parts are the X-ray set's - out of the level's own block, or the outfit's
 * own when she wears the X-ray body - and the borrowed head, the face, the
 * lid, the rig and the braid are not drawn at all. The caller hands her own
 * hands and back gun over, not the classic X-ray swap's.
 *
 *   mirror           DrawLara's own argument. The mirrored Lara is a second
 *                    call with a second set of matrices, exactly as it is for
 *                    the classic path; see the .cpp for why the winding does
 *                    not have to be touched.
 *
 * lara_item->mesh_bits hides nodes for every body, whichever variant calls
 * (hd_lara.h, HDLaraNodesShown): the bits are the cutscene's word about her
 * and not the classic body's, and the two cutscenes that hide her legs
 * inside the diving suit's mean it of any body she wears.
 */
void HDLaraDrawFrame(long mirror);

/* The log of a frame that draws her classic. DrawLara__1, __4, __5 and __6
 * call it where they draw the classic composite, outside a mirror, with
 * their own number, and it writes the SWAPLOG and HIDELOG lines
 * HDLaraDrawFrame writes for the HD one: the same fields and the same state,
 * the last field drawn=DrawLara__<variant> where the HD lines say drawn=hd.
 * The XRAYLOG line as well, on the X-ray look's edges: on with set=classic
 * from __6, off from the others. Behind -log and nothing else: it reads her
 * mesh bits and mesh pointers and draws nothing, so the classic picture is
 * the same with or without it. */
void HDLaraClassicLog(int variant);

// --- the projected shadow --------------------------------------------------

/* Whether the projected shadow runs in this drawn frame (hd_shadow.h,
 * HDShadowGate): the option asks for it, the level is drawn HD, Lara is the
 * HD composite, the room she is in is HD, and the map and its shaders exist
 * on this device - asking makes them. Decided once a frame, by the first call
 * after HDLaraDrawFrameReset, and the same answer for every later call of
 * that frame; Lara's main draw asks it before it pushes her parts, and the
 * caller of her blob shadow may ask it first.
 *
 * A frame in which it does not run lets the next frame that runs start the
 * key light afresh: no glide out of an old direction. */
bool HDShadowFrameDecide();

/* Whether her blob is drawn in this frame: in every frame the projected
 * shadow does not run in, and while it comes in (hd_shadow.h,
 * HDShadowArrive) - decided by HDShadowFrameDecide, which it asks. */
bool HDShadowFrameBlob();

/* Her blob's weight in this frame, 1 minus the projected shadow's presence
 * while it comes in and 1 in every other frame - and 1 for any item that is
 * not her: the factor of the blob's alpha (hd_object.h, HDObjectBlobWeight,
 * asks it). */
struct ITEM_INFO;

float HDShadowFrameBlobWeight(ITEM_INFO* item);

/* The frame's state (hd_shadow.h, HDShadowFrame), filled by Lara's main draw
 * when the projected shadow runs: the key light as drawn, her box and the
 * second map's, the view-to-map and the cast matrices of each, the frame's
 * world-to-view, and `stepped`, which says that this draw has run. `runs`
 * and `stepped` are 0 in every other frame. Never null. */
struct HDShadowFrame;
struct HDShadowFollow;

const HDShadowFrame* HDShadowFrameState();

/* Whether the second map exists on this device in this frame: its target
 * and depth surface (hd_render.h, HDRenderShadowOthersReady) and the two-map
 * receive shader, asked by the decision of a frame the projected shadow runs
 * in - asking makes the target - and false in every other frame. A device
 * that refuses the second map alone answers false while her shadow runs, and
 * the other casters keep their blobs. */
bool HDShadowFrameOthersReady();

// The key light's following across frames, for the log.
HDShadowFollow* HDShadowFollowState();

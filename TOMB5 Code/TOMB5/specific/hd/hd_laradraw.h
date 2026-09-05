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

// THE SEAM BETWEEN DrawLara AND THE HD MODEL PATH (#123, stage S4-2).
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
//                  for Lara's own slots, which is what game\larafire.cpp:331
//                  does itself. LHolster, lara.holster and lara.back_gun are
//                  already slot numbers.
//   the braid      hairs[0][0..5], six links the simulation writes as
//                  ABSOLUTE world positions and angles.
//   the light      CalculateObjectLightingLara has just run; the ambient is
//                  per node through LaraNodeAmbient and the light list is the
//                  one an HD object gets (hd_object.h, HDObjectLights).
//
// WHAT STAYS CLASSIC, and why, all of it visible in the log:
//
//   young Lara            the user's decision (plan section 9). Recognised
//                         three ways over, because the randomizer moves
//                         outfits between levels.
//   LARA_PONYTAIL         the randomizer writes that outfit byte BOTH for the
//                         Rome body inside a Russia level AND for every Tomb
//                         Raider III guest (#122), and nothing else in the
//                         level tells the two apart. Until the marker of
//                         stage S7 arrives in the level header, such a level
//                         draws the classic Lara - a right classic body beats
//                         a wrong HD one.
//   the skeleton cheat    lara.skelebob goes through DrawLara__6, which this
//                         file does not touch at all.
//   the muzzle flash,     drawn by their own routines outside DrawLara
//   the gunshells,        (game\draw.cpp, SetGunFlash / DrawGunshells) and
//   the flare in flight   never part of the composite.
//   the diving suit's     LARA_EXTRA_MESH1, twice, with subsuit.XRot - they
//   two thrusters         are a separate OBJECT and not a node of her body,
//                         so they keep being drawn classically ON TOP of the
//                         HD suit. The wardrobe has no model for them.
//   the inventory         newinv2.cpp draws its own ring of objects with
//                         DrawInventoryItem and never calls DrawLara.
//
// THE SWITCH is hd_objects, the same one the creatures use. Lara is a model
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

// --- the frame -------------------------------------------------------------

void HDLaraDrawFrameReset();

/* THE SEAM ITSELF, called from the three DrawLara variants in place of the
 * skin loop, the joints, the braid, the holsters and the weapon on the back.
 * It puts Lara's parts on the frame list hd_object.cpp keeps, and they are
 * drawn where every HD model is drawn.
 *
 *   mirror           DrawLara's own argument. The mirrored Lara is a second
 *                    call with a second set of matrices, exactly as it is for
 *                    the classic path; see the .cpp for why the winding does
 *                    not have to be touched.
 *   honourMeshBits   whether lara_item->mesh_bits hides nodes. TRUE for
 *                    DrawLara__1, which reads them; FALSE for __4 and __5,
 *                    which do not - and a seam that read them where the
 *                    classic path does not would make the catsuit vanish in
 *                    the very cutscenes that hide the classic joints.
 */
void HDLaraDrawFrame(long mirror, bool honourMeshBits);

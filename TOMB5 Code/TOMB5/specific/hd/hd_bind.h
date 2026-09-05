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

// WHICH .TRM BELONGS TO WHICH SLOT (#123).
//
// The remaster does not repaint an object's classic tiles; it throws the mesh
// away and draws a model of its own, one file per slot in <5>\ITEM. Which file
// that is comes from three places, in this order:
//
//   1. The level's own <LEVEL>.MAP - plain text, KEY=VALUE a line, CRLF. It
//      lists only the DEVIATIONS, which is why it is short (5 lines in
//      RICHCUT2, 65 in JOBY3). A value of DUMMY means "this slot draws
//      nothing", and DUMMY.TRM is a real file with no parts in it.
//   2. Our own hand table for the slots this project added beyond the
//      original 460 (BLUE_GUARD2 -> BLUE_GUARD_JOBY and so on). The remaster
//      has the models; only the slot numbers are ours.
//   3. The slot's own name: SWAT -> SWAT.TRM, DOG -> DOG.TRM.
//
// Lara is not bound here. Her HD body is a composite of an outfit, a head and
// a braid and is stage S4 of the plan; every slot from LARA to HAIR is
// reported as "lara" and left alone. Young Lara stays classic by the user's
// decision (plan section 9, point 6).
//
// Plain C-shaped on purpose - see hd_install.h for why.

// Slots this table knows, which must equal NUMBER_OBJECTS. hd_level.cpp holds
// the static_assert; this file is built without the engine so that the
// diagnostic tools can use exactly the same binding.
#define HD_SLOT_COUNT	471

// The name of a slot, e.g. "BLUE_GUARD2", or 0 when the number is not one.
const char* HDSlotName(int slot);

// True while `slot` belongs to Lara's composite (LARA .. HAIR, 0..30).
bool HDSlotIsLara(int slot);

// One level's LEVELn.MAP. Reading it twice replaces the first reading; there
// is only ever one level. Returns false when the file is not there, which is
// not an error - JOBY4 has twelve lines and RICHCUT2 five, and a level with
// none would simply bind everything by slot name.
bool HDMapLoad(const char* mapPath);
void HDMapFree();
// How many KEY=VALUE lines the map held.
int HDMapCount();
// The value for a key, or 0. Case-insensitive, as the files are all upper
// case anyway.
const char* HDMapValue(const char* key);

// What a slot resolves to.
enum HDBindKind
{
	HD_BIND_NONE = 0,	// not a slot number, or nothing to draw
	HD_BIND_LARA,		// Lara's composite - stage S4, not bound here
	HD_BIND_DUMMY,		// the level says DUMMY: no model on purpose
	HD_BIND_MODEL		// a model name, which may or may not exist on disk
};

// The model name for a slot. `kind` comes back as one of the four above and
// `fromMap` says whether the LEVEL.MAP decided it. The returned pointer is
// valid until the next HDMapFree() and is 0 for everything but HD_BIND_MODEL.
const char* HDBindSlot(int slot, HDBindKind* kind, bool* fromMap);

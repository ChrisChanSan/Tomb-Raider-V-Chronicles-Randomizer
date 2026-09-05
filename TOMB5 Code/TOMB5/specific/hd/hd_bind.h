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
//   2. Our own hand table, for two kinds of slot: the ones this project added
//      beyond the original 460 (BLUE_GUARD2 -> BLUE_GUARD_JOBY and so on),
//      whose models the remaster has and whose NUMBERS are ours alone; and
//      the one vanilla slot whose model exists under no plain name at all -
//      ROMAN_GOD, for which 5\ITEM ships ROMAN_GOD_ANDREA_SWORD and
//      ROMAN_GOD_ANDREA_HAMMER and nothing else, while only ANDREA2 and
//      ANDREA3 say in their .MAP which of the two they mean.
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
// The same, with the path built the way hd_scan.cpp builds it:
// "<installRoot>\DATA\<stem>.MAP". Every caller that has the install root and
// the level stem in hand should use this rather than spell the path again -
// fix round B found the model build binding a whole level by slot name alone
// because the map had been read, freed and never read back.
bool HDMapLoadForLevel(const char* installRoot, const char* stem);
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

// --- a slot whose model came out of another level (#123, stage S7-2) -------
//
// The randomizer copies models between level files: a cross enemy into a
// vanilla slot, a mesh warehouse, a level-specific ANIMATING. The remaster
// then binds by the name THIS level gives the slot, and ANIMATING10 is a
// different thing in every one of the fifteen files - so the .hdm sidecar
// names the donor and the slot the model was taken FROM, and the answer comes
// out of the DONOR'S .MAP instead.
//
// `installRoot` is the remaster's game 5 folder, `donorLevel` a level stem as
// the sidecar spells it ("Andrea1"). The .MAP is read at most once per donor
// and kept until HDDonorMapsFree(); a run that fetched six models from
// Andrea1 opens one file. A donor with no .MAP at all is not an error - the
// source slot then binds by its own name, exactly as it would in its own
// level.
//
// The level's own map (HDMapLoad) is untouched by this: two stores, because
// the level's is asked for every other slot in the same breath.
//
// The returned pointer stays valid until HDDonorMapsFree() - the maps are
// allocated one by one and never move, so an answer taken before the next
// donor was asked about is still an answer afterwards.
const char* HDBindDonorSlot(const char* installRoot, const char* donorLevel,
	int sourceSlot, HDBindKind* kind, bool* fromMap);
void HDDonorMapsFree();
// How many donor .MAP files are held, and how many lines they brought - for
// the log.
int HDDonorMapCount();
int HDDonorMapEntries();

/* THE GUESTS OUT OF TOMB RAIDER III (#92, #123 stage S7-2).
 *
 * Four enemies the randomizer fetches from the user's own Tomb Raider I-III
 * installation at run time and installs into TR5 slots no vanilla level uses.
 * The remaster has HD models for them - in the OTHER bundle, "Tomb Raider
 * I-III Remastered", under 3\ITEM - and the sidecar writes them as
 * `donor <slot> TR3 <slot>`, with the TR5 slot number on both sides, because
 * the model does not come out of a TR5 level at all.
 *
 * So the name cannot come from a .MAP: it comes from this table, which maps
 * the TR5 slot the guest was installed into to the TR3 engine symbol the
 * remaster names its file after. 0 for anything else.
 *
 *   31  SAS       -> TREX      Tomb Raider III slot 287
 *   43  CROW      -> VULTURE   ... 29 (The Lost Artifact; one ITEM folder
 *                                  per game, so it is in 3\ITEM as well)
 *   53  SAILOR    -> COMPY     ... 34
 *   75  REAPER    -> RAPTOR    ... 288
 *
 * How those four names were arrived at without the installation to look in is
 * written down in docs\Engine-Manual.md; the short of it is that the remaster
 * names a model after the engine's own symbol for the slot unless the level's
 * .MAP says otherwise, which is TRForge's measurement over 211 .MAP keys, and
 * the symbols are TRForge's generated TR3 table. */
const char* HDBindTr3Guest(int tr5Slot);

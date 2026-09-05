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

// WHAT THE RANDOMIZER CHANGED, IN THE LEVEL'S OWN WORDS (#123, stage S7-2).
//
// The Remaster's art describes the VANILLA level. Two kinds of change make it
// wrong and neither fits in the level header: a room whose SHAPE the run
// altered (a wall cut open for a secret - the HD art still shows the closed
// wall), and an object slot that now holds a model out of ANOTHER level file
// (the Remaster resolves a slot by the name THIS level gives it, and
// ANIMATING10 means something different in every level). A room bit field
// would need up to 254 bits and a donor list is unbounded, so both go into a
// text sidecar beside the .TRC: data\<LEVEL>.hdm.
//
// THE GRAMMAR, one fact per line, and the whole of it:
//
//     hdm 1                      the version, first line, and 1 is the only
//                                one this reader knows
//     outfit 6                   repeats data[3] - for a person reading the
//                                file. The ENGINE reads the header byte
//     room 34                    that room's shape changed -> draw it classic
//     donor 386 Andrea1 386      slot 386 of this level holds the model that
//                                is slot 386 in Andrea1
//     donor 31 TR3 31            ... or a guest out of Tomb Raider I-III
//
// `room` lines ascend, `donor` lines ascend by slot, so the same seed gives
// the same file byte for byte. A LINE THIS READER DOES NOT KNOW IS IGNORED
// and counted, so a later version of the randomizer can add a fact without
// this engine refusing the file. A MISSING FILE MEANS "NO MARKERS" and is not
// an error - it is what a vanilla installation looks like.
//
// The randomizer writes the file on EVERY run for EVERY level, even when it
// has nothing to say (then it is the version line alone), and deletes it
// again with "restore original". That is what stops a stale file from going
// on talking about a run that is no longer there.
//
// Two kinds of slot never appear in it, and the randomizer is the one place
// that decides so: Lara's own block 0..30, whose HD body comes from the
// outfit path, and 460..470, which this engine's own hand table names
// (hd_bind.cpp). This reader does not filter - it reports what the file says.
//
// Engine-free, like hd_bind and hd_scan: the diagnostic tool MapProbe
// compiles this very file, so what it reports is what the game reads. The
// reference reader the format was agreed against is readHdm() in
// Preparation\Tools\Diagnose\HdmDump.cpp; these two have to answer the same
// way on the same file.

struct HDMarkerDonor
{
	int slot;		// the slot in THIS level
	int fromSlot;		// the slot it was taken from in the donor
	const char* level;	// the donor's level stem, or "TR3"
};

// Reads the sidecar belonging to a level file: the same path with .hdm in
// place of the extension, which is what the randomizer writes. Replaces
// whatever was read before - there is only ever one level.
//
// True when a file was there AND it parsed. False otherwise, and then
// HDMarkersError() says which of the two it was; the caller's answer to both
// is the same, "no markers", and the difference belongs in the log.
bool HDMarkersLoad(const char* levelPath);
void HDMarkersFree();

// Whether a sidecar was read and accepted.
bool HDMarkersPresent();
// The version line the file carried (0 = there was none).
int HDMarkersVersion();
// Why the last HDMarkersLoad said false, or "". Never null.
const char* HDMarkersError();
// The path the last HDMarkersLoad looked at. Never null.
const char* HDMarkersPath();

// The `outfit` line, or 0. FOR THE LOG ONLY: the engine draws from data[3] of
// the level header, which is there before a room is loaded. The two come out
// of one value in the randomizer and can only disagree when one of the two
// halves was written by an older run - which is worth a line and nothing more.
int HDMarkersOutfit();

// Whether the run changed the shape of that room.
bool HDMarkersRoomTouched(int room);
int HDMarkersRoomCount();
int HDMarkersRoom(int at);

int HDMarkersDonorCount();
const HDMarkerDonor* HDMarkersDonor(int at);
// The donor line for a slot of this level, or 0.
const HDMarkerDonor* HDMarkersDonorOfSlot(int slot);

// Lines the file held that this reader has no rule for - for the log, so that
// a newer randomizer talking to an older engine is visible rather than silent.
int HDMarkersUnknownCount();
const char* HDMarkersUnknown(int at);

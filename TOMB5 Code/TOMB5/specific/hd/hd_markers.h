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
//     hdm 2                      the version, first line; this reader knows
//                                1 and 2
//     outfit 6                   repeats data[3] - for a person reading the
//                                file. The ENGINE reads the header byte
//                                (round M2 briefly had a `wear <key>` line
//                                here, for an outfit out of the wardrobe
//                                scan. It is gone: a seed and its settings
//                                travel between players and an installation
//                                does not, so the extra outfits are the
//                                player's choice and live in the registry -
//                                see hd_laradraw.cpp, hd_outfit. A sidecar
//                                the M2 build wrote still carries the line,
//                                and this reader now treats it as what it is:
//                                a line it has no rule for, kept for the log)
//     room 34                    that room's shape changed past keeping ->
//                                draw it classic
//     cut x0 y0 z0 x1 y1 z1      an OPENING the run made, as a box in world
//                                units -> drop the HD art inside it and keep
//                                the room (version 2, #123 fix round Z)
//     donor 386 Andrea1 386      slot 386 of this level holds the model that
//                                is slot 386 in Andrea1
//     donor 31 TR3 31            ... or a guest out of Tomb Raider I-III
//
// `room` lines ascend, `cut` lines ascend by their six numbers, `donor` lines
// ascend by slot, so the same seed gives the same file byte for byte. A LINE
// THIS READER DOES NOT KNOW IS IGNORED and counted, so a later version of the
// randomizer can add a fact without this engine refusing the file. A MISSING
// FILE MEANS "NO MARKERS" and is not an error - it is what a vanilla
// installation looks like.
//
// THE TWO VERSIONS, AND WHY BOTH ARE STILL HERE. Version 1 had only the
// whole-room mark, and the play test of 2026-09-11 showed what that costs: an
// ANDY3 run whose secret list opens two doorways turned FOUR of 211 rooms
// classic, and the player saw HD floors running into classic ones with the
// void showing in the seam. Version 2 says where the opening IS, so the room
// keeps its art and only the box is taken out of it. The randomizer writes
// the LOWEST version that can express what is in the file - a run that opened
// nothing still writes `hdm 1` - so a sidecar and an engine from either side
// of this fix round go on working together wherever they can.
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

/* ONE OPENING THE RUN MADE, AS A BOX (#123, fix round Z).
 *
 * World units, ascending corners, and Y GROWS DOWNWARD as everywhere else in
 * this format - mins[1] is the opening's ceiling and maxs[1] its floor. The
 * randomizer derives it from the bytes its own edit wrote (the view window's
 * four corners, the sector square at the height a vertical link was opened,
 * the corners of the faces @Clear degenerated) and steps it ONE way out and
 * TWO ways in: half a sector, 512, OUTWARD along the opening's own normal,
 * because the remaster builds a wall as a thick solid whose faces do not sit
 * on the classic wall plane - and 128 INWARD along the other two.
 *
 * Inward, since fix round Z1. A cut room keeps its HD art and an HD room
 * never draws its classic roomlets (drawroom.cpp, InsertRoom), so whatever
 * this box takes is replaced by NOTHING; it may hold only what stands inside
 * the opening. A view window runs from its sector's ceiling to its floor by
 * construction, so an outward step there takes the passage's own floor and
 * ceiling quads - which lie exactly in the box's boundary plane - and leaves
 * the void showing. That was measured at 400,950 clear pixels in one frame
 * of ANDY3 before the sign was turned round. The triangle test below is a
 * real intersection, so a wall triangle crossing the opening still meets the
 * smaller box. ApplyPatch.h carries the same description from the writing
 * side.
 *
 * Nothing here had to change for it: the reader takes the box as it comes,
 * and a version 2 file written before fix round Z1 is read exactly as it
 * always was - it simply names a box 256 units wider on two axes.
 *
 * The box belongs to the LEVEL and not to a room. hd_level.cpp asks it of
 * EVERY group, because a neighbour's overhang can stand in a doorway as
 * surely as the room's own wall can - and fix round T measured that the
 * remaster's rooms reach into each other by construction. */
struct HDMarkerCut
{
	int mins[3];
	int maxs[3];
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

// Whether the run changed the shape of that room past keeping its HD art.
bool HDMarkersRoomTouched(int room);
int HDMarkersRoomCount();
int HDMarkersRoom(int at);

// The openings the run made (#123, fix round Z). Empty for a version 1 file,
// which had no way of saying it.
int HDMarkersCutCount();
const HDMarkerCut* HDMarkersCut(int at);

int HDMarkersDonorCount();
const HDMarkerDonor* HDMarkersDonor(int at);
// The donor line for a slot of this level, or 0.
const HDMarkerDonor* HDMarkersDonorOfSlot(int slot);

// Lines the file held that this reader has no rule for - for the log, so that
// a newer randomizer talking to an older engine is visible rather than silent.
int HDMarkersUnknownCount();
const char* HDMarkersUnknown(int at);

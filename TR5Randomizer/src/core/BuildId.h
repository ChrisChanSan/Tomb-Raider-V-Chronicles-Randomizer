/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

//----------------------------------------------------------------------------
// Shared identifier of randomizer, engine and curation data.
//
// Why this exists at all: a seed alone reproduces nothing. Whether seed X
// yields the same level depends on three things - the placement logic in the
// randomizer, the bundled zone/curation data, and the engine that plays the
// result. If someone reports "seed K7M... is unwinnable", that is not
// reproducible without these three. Yet that was the whole point of the
// seed feature.
//
// Rule: RANDO_BUILD_ID is bumped on EVERY change that can alter the outcome
// of a seed. That is more than one first thinks - it includes the curation
// tables in ZoneAnalysis.ps1, because a new ban list moves items. Pure UI
// changes, on the other hand, do not count.
//
// NOTE 2026-08-30: this used to say that the engine carries the same
// constant and that a mismatch makes the randomizer refuse to work. It
// does not. `grep -r RANDO_BUILD_ID "TOMB5 Code"` finds nothing - the
// check was never built on the engine side, so today NOTHING enforces
// that the two halves belong together. The id below is still bumped and
// still lands in spoiler.txt, which makes a bug report readable; it just
// does not stop anyone. Building the check is worth doing.
//
// (original text) The engine carries the same constant. If they do not
// match, the randomizer refuses to work instead of producing a seed
// whose result nobody can
// reproduce. So when bumping it here, always touch the engine as well - the
// two projects live in separate trees, which is why a shared header cannot
// enforce this.
//----------------------------------------------------------------------------

// 2: The curation data now enforces what it has long described - island
//    groups, height limits, banned sectors, weapon-free rooms and "behind
//    its own door". The randomizer used to read and ignore these fields, so
//    every seed comes out differently than under 1.
// 3: Secrets (typeId 223, 36 in the whole game) stay in place for now, and
//    Andy2 is curated. Both are curation data, and that moves placements
//    just like a code change would.
// 4: andy3, rich1 and rich2 curated - and MANUAL_LINK repaired. The table
//    never worked (PowerShell flattened the pairs), so joby2 and rich1 ran
//    without their hand edges the whole time. Zones and spots of the twelve
//    other levels are untouched by that, but rich2 is not, and the zone
//    numbers themselves are input to every placement.
// 5: rich2 now has constraints - before it had none. Puzzle Item3 gets its
//    zone limit, Pickup Item2 plus one bottle and one cloth must go to the
//    start area, and the elevator section carries no key items anymore.
// 6: rich2's first ban list from the trview walkthrough, and Puzzle Item3
//    gets a lower bound - it may only lie from room 128 on and no longer
//    behind the teleporter. 214 -> 145 allowed spots for this type.
// 7: Mandatory injections can carry a fixed sector and stack there. rich2
//    thus gets the HK plus two magazines on 140(1,2).
// 8: Injections can carry a height offset, and rich2 confines bottle and
//    cloth more tightly than the rest of the start area.
// 9: TYPE_Y_OFFSET - types whose mesh origin does not sit on the lower edge
//    are lifted on EVERY placement. So far only the HK (345).
// 10: TYPE_Y_OFFSET becomes TYPE_OFFSET with dx/dy/dz and a level scope.
//     New: Andrea2's Puzzle Item3 moves dz -512 into the sector center.
// 11: Andrea2's Puzzle Item3 stays vanilla - with its OCB zeroed it cannot
//     be picked up, with it kept it needs a wall behind it. KEEP_OCB exists
//     as a mechanism but is empty.
// 12: rich3 curated - six flipmap releases and the two puzzle combos
//     carried by the cyborgs. With that, all 13 levels are done.
// 13: rich3 gets its two island groups - the puzzle combos behind the
//     one-way teleporter, the crossbow into its own area.
// 14: rich3's crossbow is narrowed to three rooms, its ammo stays vanilla
//     for now.
// 15: rich3's first ban list - 27 rooms, 808 -> 528 spots.
// 16: Room 209 joins rich3's ban list - and with that the curation of all
//     thirteen levels is complete.
// 17: The user's final ban list across all levels, plus the new table
//     TYPE_EXCLUDE_SECTORS.
// 18: EQUIP_GATE is enforced instead of merely reported. Andrea1's revolver
//     and lasersight may no longer wander behind the bell.
// 19: Audit round A1 (#172), LENS5C-02 - four commits since Beta 1.0 moved
//     where enemies land or which kinds are rolled, and the id had not
//     followed: 38b8bd5 (ceiling check, #141 part 1), 9b7b827 (Hydra needs
//     five of nine tiles free, #141 part 2), 4dd8c6c (Hydra counts double
//     in the kind roll, #141 part 2b), 5e087f7 (the cyborg joins the cross
//     pool, #146).
#define RANDO_BUILD_ID      19
#define RANDO_BUILD_STRING  "0.19"

// The published version, and a different thing from the build id above.
// That one counts revisions of the curation data and decides whether two
// runs can produce the same levels; this one is what a player calls the
// program they downloaded. Both belong in a bug report, so both are shown.
// The engine carries the same string in its version resource (tomb5.rc);
// the two source trees build separately, so they are kept in step by hand.
#define RANDO_VERSION_STRING "Beta_1.0"

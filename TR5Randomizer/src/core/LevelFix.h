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
#include "Tr5Level.h"

#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Core's own level defects, repaired on EVERY run.
//
// Distinction from the rest of the curation: the zone and range tables in
// ZoneAnalyse.ps1 say WHERE randomness may put things - they work with the
// level as it is. This here is the opposite: places where the level ITSELF
// is broken, independent of any shuffling. Such a defect cannot be curated
// around, it has to go.
//
// Deliberately a short, hand-maintained list and not a rule system. Every
// entry stands for a case someone saw in game, and carries its own
// justification.
//----------------------------------------------------------------------------
namespace LevelFix
{
    // A trigger that is missing on a second route. The target sector gets an
    // identical copy, the source stays untouched.
    struct TriggerCopy
    {
        const char* level;                  // file name without extension
        int fromRoom, fromSx, fromSz;
        int toRoom, toSx, toSz;
        const char* reason;
    };

    //------------------------------------------------------------------------
    // joby5: the floor trigger that arms the Electrical Cables 91 and 136
    // lies in room 138 on sector (1,1) - and only there. Jump Switch 190 in
    // room 165 turns the power back off; its antitrigger, however, only
    // affects cables that are ALREADY armed.
    //
    // On 2026-08-22 a tester reached the switch by the other route, without
    // ever entering room 138. So the antitrigger ran BEFORE the trigger:
    // first it turned nothing off, then the floor trigger armed the cables -
    // and they stayed armed for the rest of the level. The water in rooms
    // 126, 127, 128, 158 and 183 is then permanently lethal (10 hit points
    // per frame, game/joby.cpp ControlElectricalCables).
    //
    // The copy on room 122 sector (1,1) covers that second route: Lara
    // passes it in every case before she can reach the switch.
    //
    // Why this is more than convenience: the curation explicitly allows
    // PUZZLE items in exactly these water rooms (user decision, NO_KEY_ROOMS
    // in ZoneAnalyse.ps1: "the puzzles can stay, only keys matter") - on the
    // grounds that they are only needed at the end, when the power has long
    // been off. The tester case breaks precisely that assumption. Keys are
    // barred anyway; the copy makes the puzzle allowance sound again instead
    // of having to retract it.
    //
    // Measured against the file (SektorPruef): room 138 has exactly ONE
    // trigger, on (1,1), with the object commands 91 and 136. Room 122 is
    // 7x3 sectors, (1,1) is walkable floor without a chain of its own - the
    // copy therefore lands on a free sector.
    //------------------------------------------------------------------------
    static const TriggerCopy kTriggerCopies[] =
    {
        { "joby5", 138, 1, 1, 122, 1, 1,
          "cable trigger (91/136) also on the second route to Jump Switch 190" },
    };

    // Apply all entries for this level. Failed entries end up as warnings in
    // "warnings"; the run continues, because a fix that was not applied does
    // not make the level any worse than before.
    // Returns the number of fixes applied.
    int apply(Tr5Level& level, const std::string& levelName,
                 std::vector<std::string>& warnings);
}

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
#include "Json.h"
#include "Randomizer.h"
#include "Tr5Level.h"

#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Applies a patch (itemEdits / itemAdds / triggerMoves) to a loaded level.
//
// The patch format is the same one the placement logic produces:
//   { "itemEdits":    [ {index, room?, x?, y?, z?, ocb?}, ... ],
//     "itemAdds":     [ {typeId, room, x, y, z, ocb?}, ... ],
//     "triggerMoves": [ {fromRoom, fromSectorX, fromSectorZ,
//                        toRoom,   toSectorX,   toSectorZ}, ... ],
//     "triggerAdds":  [ {room, sectorX, sectorZ, kind?, timer?, oneShot?, mask?,
//                        commands: [ {type, param}, ... ]}, ... ],
//     "soundAdds":    [ {map, fromLevel, fromMap}, ... ] }
//
// For the edits every field except index is optional - whatever is absent
// stays unchanged. That is why a moved item keeps its angle and its
// lighting: we only write what we actually mean.
//
// triggerMoves takes the trigger entry out of the source sector and appends
// it to the target sector's chain - one operation, not two. It exists for
// the coupled move: when an item travels whose pickup trigger also fires
// something else, the trigger travels along so the effect is preserved.
//
// triggerAdds sets NEW triggers. That is the way to add enemies to a level:
// first append the item via itemAdds, then put a trigger on it. The
// defaults are intentional: kind 0 (TRIGGER), mask 31 and oneShot true are
// the standard values because that is exactly a "wake this enemy" trigger.
// oneShot is no fine detail either - see Tr5Level::addTrigger.
//
// There is neither itemRemoves nor triggerRemoves, and neither should ever
// exist - see Tr5Level::addItem and the reasoning in apply().
//----------------------------------------------------------------------------
// soundAdds takes sound samples over from another level. Needed as soon as
// an enemy goes into a level that does not contain it in vanilla: a level
// only carries the samples it needs itself, and without them the enemy
// stands there mute. The source path comes from the caller, because only
// the caller knows where the untouched levels are - see Patch::apply's
// levelSource.
// weaponAdds copies weapon model slots from other levels:
//   "weaponAdds": [ {slot, fromLevel, replace}, ... ]
// replace false means "only what is missing" - if the level already has the
// slot, it stays untouched (that is how rich2 keeps its special HK pose).
// The list arrives fully resolved from the randomizer; the donor rules live
// there. Executed AFTER the outfit, so the outfit donor's holster slots are
// not overwritten again.
struct PatchReport
{
    int edited = 0;
    int added = 0;
    int triggersMoved = 0;
    int triggersAdded = 0;
    int soundsCopied = 0;
    int weaponsCopied = 0;               // weapon slots taken over
    int moveablesCopied = 0;             // cross-enemy slots taken over
    std::string outfitFrom;              // empty = outfit unchanged
    std::vector<std::string> warnings;
};

// Where source levels for soundAdds come from. Receives the name from
// "fromLevel" and returns the full path; an empty return means "does not
// exist".
typedef std::wstring (*LevelSourceFn)(const std::string& name, void* user);

namespace Patch
{
    // levelSource is only needed for soundAdds. Without it, any soundAdds
    // present are reported as warnings and skipped - the patch does not fail
    // on that, because a mute enemy is not a broken level.
    // tr3Dir/tr3GoldDir: the user's installations for the Tomb Raider 3
    // guests (#92). They are parameters and NOT part of the patch: a patch
    // is kept around for reproducing things and must not carry machine
    // paths. If they stay empty, no guest appears in the patch anyway - the
    // randomizer then never puts them into the pool in the first place.
    bool apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error,
               LevelSourceFn levelSource = nullptr, void* levelSourceUser = nullptr,
               const std::wstring& tr3Dir = std::wstring(),
               const std::wstring& tr3GoldDir = std::wstring(),
               // Cache for the extracted packages (#103). Empty means
               // "extract anew every time" - slower, but always correct.
               const std::wstring& cacheDir = std::wstring());

    // Records in the input which sectors can accept a traveling trigger.
    // Must run BEFORE Rando::run, or the placement picks a spot where
    // apply() later aborts.
    //
    // Lives here and not in Randomizer.cpp because the answer only exists in
    // the floordata of the real level - and Randomizer.cpp deliberately
    // knows nothing about Tr5Level (the self-test builds without it). Nor
    // does it live in Tr5Level, because that should know nothing about
    // RandoInput. This is the place that knows both sides anyway.
    //
    // Without coupled items and without enemy types it does nothing.
    void markTriggerSafeSectors(const Tr5Level& level, RandoInput& in);

    // Shuffle the music triggers (checkbox "Shuffle trigger sound/music
    // IDs"): sends all TO_CD tracks of the finished level through a GLOBAL
    // seed permutation over the measured trigger catalog (56 jingles) -
    // jingles thus travel across level borders, but the same track sounds
    // the same in every level. In TWO separate pots, because the engine
    // treats them differently (TriggerNormalCDTrack, control.cpp):
    // 117/118/121/123..130 switch the running atmosphere, everything else
    // plays once with a cd_flags lock. An atmosphere trigger thus stays an
    // atmosphere trigger, only the mood changes. Returns the number of
    // rewritten words.
    long shuffleMusicTriggers(Tr5Level& level, uint64_t seed);
}

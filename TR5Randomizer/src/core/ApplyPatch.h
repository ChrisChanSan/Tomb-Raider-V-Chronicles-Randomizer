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

#include <functional>
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
//                        mirrorFlip?, commands: [ {type, param}, ... ]}, ... ],
//     "soundAdds":    [ {map, fromLevel, fromMap}, ... ] }
//
// For the edits every field except index is optional - whatever is absent
// stays unchanged. That is why a moved item keeps its angle and its
// lighting: only what is actually meant gets written.
//
// triggerMoves takes the trigger entry out of the source sector and appends
// it to the target sector's chain - one operation, not two. It exists for
// the coupled move: when an item travels whose pickup trigger also fires
// something else, the trigger travels along so the effect is preserved.
//
// The trigger's new sector is mirrored into the flipmap partner the
// same way a waker is, and by the same rules - no flag is needed, because a
// travelling trigger always wants to be reachable in both variants. Only
// the writing side is mirrored: the source sector loses its entry alone,
// even when its room has a partner. Reason and measurement in apply().
//
// triggerAdds sets NEW triggers. That is the way to add enemies to a level:
// first append the item via itemAdds, then put a trigger on it. The
// defaults are intentional: kind 0 (TRIGGER), mask 31 and oneShot true are
// the standard values because that is exactly a "wake this enemy" trigger.
// oneShot is no fine detail either - see Tr5Level::addTrigger.
//
// mirrorFlip asks for the second copy in the flipmap partner room.
// The flag is set by whoever knows that the trigger is a plain enemy waker
// and may exist twice; the curated triggers of the secret lists do NOT
// carry it, because those are bound to one particular object at one
// particular place.
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
    // A new trigger in a room that owns a flipmap partner is written into
    // the partner as well - after the flip the base room reads the
    // partner's floordata, and a trigger that lives in only one of the two
    // never fires again. Mirrored counts the copies, mirrorSkipped the
    // cases where the partner tile is a wall, sits at another height or
    // already carries floordata that must not be touched.
    int triggersMirrored = 0;
    int triggersMirrorSkipped = 0;
    // The same for a trigger that travels with its item
    // (triggerMoves). Counted apart from the wakers on purpose - the two
    // cases weigh differently. A waker that misses its copy costs one
    // sleeping enemy; a key whose pickup trigger ends up in the wrong
    // variant of the room costs the key, so a skip here has to be visible
    // on its own and must not be diluted by twenty mirrored wakers.
    int triggersMoveMirrored = 0;
    int triggersMoveMirrorSkipped = 0;
    int soundsCopied = 0;
    int weaponsCopied = 0;               // weapon slots taken over
    int moveablesCopied = 0;             // cross-enemy slots taken over
    std::string outfitFrom;              // empty = outfit unchanged
    // Weather: what the file carried and what was written into it. Both -1
    // as long as the patch carries no "weather" field - that is the signal
    // for "not touched", and it is what every caller tests.
    int weatherFrom = -1;
    int weatherTo = -1;
    // HD outfit marker: which Tomb Raider III outfit this pass put on the
    // level, as the engine's HD_LARA_OUTFIT_* number. 0 =
    // none. Written into the level header (data[3]) by apply() itself; it
    // stands here as well so the sidecar and the status lines can name it
    // without reading the file back.
    int hdOutfit = 0;
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
    // tr3Dir/tr3GoldDir: the player's installations for the Tomb Raider 3
    // guests. They are parameters and NOT part of the patch: a patch is
    // kept around for reproducing things and must not carry machine paths.
    // If they stay empty, no guest appears in the patch anyway - the
    // randomizer then never puts them into the pool in the first place.
    bool apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error,
               LevelSourceFn levelSource = nullptr, void* levelSourceUser = nullptr,
               const std::wstring& tr3Dir = std::wstring(),
               const std::wstring& tr3GoldDir = std::wstring(),
               // Cache for the extracted packages. Empty means "extract
               // anew every time" - slower, but always correct.
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

    // The height reader for models the level does NOT carry yet
    // (RandoInput::modelHeight). A cross enemy's model still lies in its
    // donor file while the placement decides whether it fits under the
    // ceiling somewhere - so the height has to be fetched from there, and
    // exactly where the import fetches the model itself.
    //
    // Same arguments as apply(), and for the same reasons: levelSource
    // resolves a TR5 donor's name to a path, tr3Dir/tr3GoldDir/cacheDir
    // reach the player's Tomb Raider 3 installation for the guests. Whatever
    // is missing simply yields no height, and an unmeasured kind passes.
    //
    // Donor levels are loaded at most ONCE per process and then kept - in a
    // full run every level asks about the same handful of donors.
    std::function<bool(const char* fromLevel, const char* tr3, int slot, int& height)>
    modelHeightFn(LevelSourceFn levelSource = nullptr, void* levelSourceUser = nullptr,
                  const std::wstring& tr3Dir = std::wstring(),
                  const std::wstring& tr3GoldDir = std::wstring(),
                  const std::wstring& cacheDir = std::wstring());

    // HD markers.
    //
    // The engine can draw a level with the Remaster's own geometry and
    // models. That data describes the VANILLA level, so the randomizer has
    // to say where it no longer fits. Five things:
    //
    //   1. THE OUTFIT, in data[3] of the level header. Written by apply()
    //      itself, because it is part of the file. See hdOutfitId.
    //   2. THE TOUCHED ROOMS - every room whose SHAPE changed so far that
    //      none of its HD art is worth keeping: a room lifted by moveRoom,
    //      a room that had no faces at all and was given some.
    //   3. THE CUT BOXES - every OPENING the run made:
    //      a wall cut through, a floor or ceiling opened, a wall face taken
    //      away. Those change one sector's worth of a room and nothing
    //      else, so the room keeps its HD art and only the box around the
    //      opening is dropped out of it.
    //   4. THE DONOR SLOTS - every model that came out of another level
    //      file, because the Remaster resolves a slot by the name THIS
    //      level gives it, and ANIMATING10 means something different in
    //      every level.
    //   5. THE ADDED ITEMS - every item the run appended, by its index in
    //      the file. The Remaster bakes the objects of the slots a level's
    //      .MAP calls DUMMY into the room art, where Core placed them; an
    //      item added to such a slot stands where the art has nothing of
    //      it, and the engine draws it classic instead of not at all.
    //
    // 2 to 5 do not fit in the header: a room bit field would need up
    // to 254 bits, and the cut, donor and item lists are all unbounded. They
    // go into a sidecar file `<level>.hdm` beside the .trc - plain text, one
    // fact per line:
    //
    //     hdm 2
    //     outfit 6
    //     room 34
    //     room 36
    //     cut 38400 8832 25728 39424 10112 26496
    //     donor 386 Andrea1 386
    //     donor 462 Andrea1 405
    //     item 244
    //     item 245
    //
    // The first line is the version, and it is the LOWEST one that can say
    // what the file holds: a file with a `cut` line in it declares 2, a
    // file without one declares 1 and stays readable by an engine that
    // predates cut-line support. `outfit` repeats data[3] for a reader
    // that has the sidecar open anyway (both come out of the same value, so they cannot
    // drift); the engine reads the header byte, which is there before a room
    // is loaded. `room` lines are ascending, `cut` lines ascending by their
    // six numbers, `donor` lines ascending by slot, `item` lines ascending
    // by index and last in the file, so the same seed gives
    // the same file byte for byte. A donor line names the slot in THIS
    // level, the donor level's stem (or "TR3" for a guest out of Tomb
    // Raider III), and the slot the model was taken FROM - which differs
    // whenever a mesh store is copied into an enemy's own slot. An `item`
    // line needs no version of its own: an engine that predates it ignores
    // the line and draws the item by its slot's rule alone, as it did.
    //
    // A `cut` line is an axis-aligned box in WORLD units, lower corner then
    // upper corner, and Y GROWS DOWNWARD as everywhere else in this format
    // - so the first y is the opening's ceiling and the second its floor.
    // It is derived from what the edit itself wrote (the view window's four
    // corners, the sector square at the height a vertical link was opened,
    // the corners of the faces @Clear degenerated) and then stepped ONE way
    // out and TWO ways in:
    //
    //   - 512 OUTWARD, half a sector, along the opening's own normal. The
    //     raw box is FLAT in that axis - a window's four corners share
    //     their x or their z - so the span there comes out at exactly
    //     2 * 512 = 1024, never more. The Remaster builds walls as thick
    //     solids whose faces do not sit on the classic wall plane, and half
    //     a sector reaches them on both sides.
    //   - 128 INWARD along the other two axes, clamped to half the extent.
    //     A cut room keeps its HD art and an HD room never draws its
    //     classic roomlets, so art the box takes is replaced by nothing;
    //     the box may hold only what stands INSIDE the opening. Stepping
    //     outward there took the passage's own floor and ceiling quads with
    //     it and left the void showing.
    //
    // So the example above reads: normal axis x (span 1024), an opening
    // 1280 tall and 768 wide, which is one sector's doorway less the two
    // inward steps.
    //
    // The box belongs to the LEVEL and not to a room: the engine drops
    // every HD triangle that meets it out of every room's art, because the
    // neighbour's overhang can block a doorway as surely as the room's own
    // wall can.
    //
    // Written on every run for every level, even when there is nothing to
    // say - then it is the version line alone. A missing file means "no
    // markers", so a level that once had markers and now has none must
    // still get a file, or the old one would keep talking.
    //
    // Two kinds of slot are deliberately LEFT OUT, and this is the only
    // place that decides it:
    //   - Lara's own block, slots 0..30 (hd_bind.cpp, lara_block_last).
    //     Her HD body is chosen by the outfit path out of data[0] and
    //     data[3], never by a slot name, and a donor line there would tell
    //     the engine to drop back to classic exactly where the outfit
    //     marker had just got it right.
    //   - The slots this project added beyond vanilla, 460..470. The engine
    //     carries its own hand table for those (hd_bind.cpp), measured by
    //     picture; a donor's name table has nothing better to offer.
    //     The brawler's 471 lies past that range on purpose: his meshes
    //     are JOBY2's slot 439, and JOBY2's .MAP names that model, so his
    //     donor line is written like any other.
    //
    // The file is written next to the finished level, so it is the CALLER
    // that calls this - after a successful save, with the same target path.
    // It is not part of save() on purpose: the sidecar belongs to a
    // randomizer RUN, not to every write of a level file, and the
    // diagnostic tools that load and save a level would otherwise scatter
    // sidecars they know nothing about.
    //
    // roomsOut/donorsOut/cutsOut receive what actually went INTO the file,
    // not what the level recorded - the two differ by the slots filtered out
    // above, and a status line that prints the larger number would not match
    // the file that was actually written.
    bool writeHdMarkers(const Tr5Level& level, const std::wstring& levelPath,
                        std::string& error,
                        int* roomsOut = nullptr, int* donorsOut = nullptr,
                        int* cutsOut = nullptr);

    // The wardrobe id of a Tomb Raider III outfit ("tr3_london") as the
    // engine's HD_LARA_OUTFIT_* number, 0 for anything it has no HD twin
    // for. THE TABLE, and the only one - both sides are documented from
    // here:
    //
    //   tr3_london      8  HD_LARA_OUTFIT_TR3_CATSUIT
    //   tr3_jungle      7  HD_LARA_OUTFIT_TR3_COAST
    //   tr3_antarctica  5  HD_LARA_OUTFIT_TR3_ANTARC
    //   tr3_nevada      6  HD_LARA_OUTFIT_TR3_NEVADA
    //
    // The numbers are the engine's own, so data[3] goes into
    // HDLaraOutfitFor unchanged and neither side translates - a translation
    // is the one thing here that could go wrong silently and put the player
    // in a Rome body where he rolled a Nevada one. They start at 5 because
    // 1..4 are the vanilla outfits; a stale or garbled byte therefore
    // cannot be mistaken for a TR3 one, and anything outside the enum is
    // refused by HDLaraOutfitFor's own range test.
    //
    // The London->CATSUIT and Jungle->COAST pairings are the plan's, judged
    // by picture, not by name.
    int hdOutfitId(const std::string& wardrobeId);

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

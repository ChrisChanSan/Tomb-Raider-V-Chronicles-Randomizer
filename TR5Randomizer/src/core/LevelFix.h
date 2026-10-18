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
// ZoneAnalysis.ps1 say WHERE randomness may put things - they work with the
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
    // affects cables that are already armed.
    //
    // A route that reaches the switch without ever entering room 138 runs
    // the antitrigger before the trigger: first it turns nothing off, then
    // the floor trigger arms the cables - and they stay armed for the rest
    // of the level. The water in rooms 126, 127, 128, 158 and 183 is then
    // permanently lethal (10 hit points per frame, game/joby.cpp
    // ControlElectricalCables).
    //
    // The copy on room 122 sector (1,1) covers that second route: Lara
    // passes it in every case before she can reach the switch.
    //
    // Why this is more than convenience: the curation explicitly allows
    // PUZZLE items in exactly these water rooms (NO_KEY_ROOMS in
    // ZoneAnalysis.ps1: "the puzzles can stay, only keys matter"), on the
    // grounds that they are only needed at the end, when the power has long
    // been off. The second-route case breaks precisely that assumption.
    // Keys are barred anyway; the copy makes the puzzle allowance sound
    // again instead of having to retract it.
    //
    // Measured against the file (SectorCheck): room 138 has exactly one
    // trigger, on (1,1), with the object commands 91 and 136. Room 122 is
    // 7x3 sectors, (1,1) is walkable floor without a chain of its own - the
    // copy therefore lands on a free sector.
    //------------------------------------------------------------------------
    static const TriggerCopy kTriggerCopies[] =
    {
        { "joby5", 138, 1, 1, 122, 1, 1,
          "cable trigger (91/136) also on the second route to Jump Switch 190" },
    };

    // A trigger that sits in the wrong place. The source sector keeps
    // everything else in its chain - only the trigger entry travels.
    struct TriggerMove
    {
        const char* level;                  // file name without extension
        int fromRoom, fromSx, fromSz;
        int toRoom, toSx, toSz;
        const char* reason;
    };

    //------------------------------------------------------------------------
    // Andrea2: the propeller has to be running before the switch.
    //
    // Three identical triggers wake propeller 86, all of them
    // [8004 3E00 8056] - mask 31, no one-shot, one object command. Two of
    // them sit in room 61, which is a water room, on the two sectors of a
    // shaft; the third sits in room 60.
    //
    // In Core's route Lara always passes them before she reaches the
    // switch. A rolled run can bring her the other way round, and then she
    // stands at the switch with the propeller not yet running.
    //
    // Target sectors: room 231 for the one from room 60, and rooms 245
    // (1,4) and (2,4) - two neighbours, so the pair keeps the two-sector-
    // wide cover it had in the shaft and Lara trips it whichever side she
    // walks. All three targets are walkable at floor y 0 (SectorCheck);
    // 231 (1,3) is empty, the two in 245 carry a floor split resp. a tilt
    // that the trigger simply follows.
    //------------------------------------------------------------------------
    static const TriggerMove kTriggerMoves[] =
    {
        { "Andrea2", 60, 1, 1, 231, 1, 3,
          "propeller trigger before the switch, not at it" },
        { "Andrea2", 61, 4, 2, 245, 2, 4,
          "propeller trigger out of the water shaft, first of the pair" },
        { "Andrea2", 61, 4, 1, 245, 1, 4,
          "propeller trigger out of the water shaft, second of the pair" },
    };

    // One object command taken OUT of a trigger the level already has.
    // The rest of that trigger stays untouched.
    struct TriggerObjectRemove
    {
        const char* level;                  // file name without extension
        int room, sx, sz;                   // the sector the trigger sits on
        int item;                           // the object command to take out
        const char* reason;
    };

    //------------------------------------------------------------------------
    // rich1: the door out of the sword-statue corridor stays open.
    //
    // Room 71 is the corridor before the laser lab. Its ONLY trigger sits on
    // sector (1,3), is one-shot, and names four things: a music track, the
    // sword statue 203, door 130 at the far end - and door 129 at the near
    // end, the way back.
    //
    // Door 129 is reverse-activated. Its item flags in the file carry all
    // five code bits, and InitialiseItem (items.cpp) turns exactly that into
    // "strip the bits, set IFL_REVERSE, activate". TriggerActive then reads
    // backwards: incomplete bits mean ACTIVE, so the door starts open - and
    // the trigger, by completing the bits, shuts it behind Lara: it starts
    // with all five bits set and open, and the trigger is written to set
    // them again, which should instead leave it with none set.
    //
    // A second trigger cannot help: it would complete the bits too, and by
    // then the one-shot has hung IFL_INVISIBLE on the door, which makes the
    // engine skip every plain trigger on it (control.cpp, top of the
    // TO_OBJECT branch). Measured in game: no effect at all.
    //
    // So only the pointer goes. Music and statue belong to the scene and
    // stay; door 130 is a normal item (flags 0) and keeps being opened.
    // Without the pointer nothing ever completes 129's bits, so it stays
    // open for the whole level - which is what a shuffled run needs, since
    // the CD for slot 4 can lie anywhere behind Lara.
    //------------------------------------------------------------------------
    static const TriggerObjectRemove kTriggerObjectRemoves[] =
    {
        { "rich1", 71, 1, 3, 129,
          "door 129 stays open - the sword-statue corridor must not be one-way" },
    };

    // One object command REPLACED by another. Same shape as the removal,
    // but for triggers where the object command is the only one.
    struct TriggerObjectReplace
    {
        const char* level;                  // file name without extension
        int room, sx, sz;
        int item;                           // the object command to replace
        int newType, newParam;              // what goes in its place
        const char* reason;
    };

    //------------------------------------------------------------------------
    // Andrea2: nothing may activate puzzle 4 from a distance.
    //
    // The heavy trigger on room 69 (4,3), under the guardian, has exactly one
    // command - and it points at item 62, which IS puzzle 4 (type 175, OCB 5).
    // A trigger names an item NUMBER, not a place, so it reaches the puzzle
    // wherever a rolled run has put it.
    //
    // When the puzzle has moved to a room behind the guardian and the
    // trigger fires first, the item either sinks halfway into the floor and
    // is barely pickable, or it goes invisible - and then the level is a
    // softlock.
    //
    // Two ways out exist: move the trigger somewhere unreachable, or take
    // the pointer out and leave the trigger otherwise intact. The second is
    // the one that actually removes the hazard - after it, no trigger in
    // the level names item 62 any more. Moving would only hide the pointer
    // somewhere quieter and bring the bug back the day anything reaches
    // that sector.
    //
    // Dropping the command outright is not possible: it is the trigger's
    // only one. So it becomes a void flipeffect (type 9, effect_routines
    // index 44 in game/effects.cpp) - the trigger stays a valid trigger and
    // does nothing, the same idea as a music command without the sound.
    //
    // Puzzle 4 needs no trigger at all: its item flags are 0, so it lies
    // there visible and can be picked up.
    //------------------------------------------------------------------------
    static const TriggerObjectReplace kTriggerObjectReplaces[] =
    {
        { "Andrea2", 69, 4, 3, 62, 9, 44,
          "puzzle 4 must not be activated from afar - it sinks or vanishes" },
    };

    // A trigger rewritten from scratch: kind, one-shot, mask, one command.
    struct TriggerRewrite
    {
        const char* level;                  // file name without extension
        int room, sx, sz;
        int kind;                           // 0 = TRIGGER, 4 = PICKUP, ...
        bool oneShot;
        int mask;                           // 31 = all five code bits
        int cmdType, cmdParam;              // the single command

        // The item this rewrite sets free, or -1. A pickup trigger's
        // reference word makes the randomizer pin its item
        // (Randomizer.cpp, findTriggerPinnedItems) - and that decision is
        // read from level_*.json, which describes the VANILLA level. The
        // rewrite here cannot reach it, so the item has to be named and
        // taken out of the pin set by the caller.
        int freesItem;

        const char* reason;
    };

    //------------------------------------------------------------------------
    // andy1: the heart may travel.
    //
    // Item 8 (type 172) has a finished KeyItemRange, but it never came into
    // play: its PICKUP trigger on room 51 (2,2) reads
    // [8404 3E00 | 0008 | 000C | B413] - reference word item 8, then door 12
    // and cutscene 19. Because that is more than music, the randomizer nails
    // the heart to its vanilla spot (Randomizer.cpp,
    // findTriggerPinnedItems).
    //
    // Both effects are dispensable: the door is useless, the cutscene is
    // too, and the room behind the door is a NoItemRoom. The cutscene is
    // worse than useless - it can teleport Lara into a softlock, which is
    // also why the trigger must not travel with the item.
    //
    // So the trigger becomes a plain TRIGGER, which beats merely swapping
    // the commands: a PICKUP trigger would keep its reference word, and
    // that word is the pointer at the heart. Without it
    // findTriggerPinnedItems skips the entry outright (refItem < 0) instead
    // of being talked round by a music-only command list.
    //
    // One command has to stay - the engine's command loop is a do-while and
    // always reads one word. Track 45 is the cue andy1 uses most often
    // (MusicCheck: 10, 21, 28, 41, 45, 65, 70, 78, 288 - not one of them an
    // atmosphere, those are 117/118/121/123-130). One-shot so it plays once
    // and is done.
    //------------------------------------------------------------------------
    static const TriggerRewrite kTriggerRewrites[] =
    {
        { "andy1", 51, 2, 2, 0 /*TRIGGER*/, true, 31, 8 /*TO_CD*/, 45, 8,
          "heart 172 free to travel - no door, no teleporting cutscene" },
    };

    // A view window that is missing over an opening the level leaves open.
    // Rendering only: the two rooms' portal lists get one record each, the
    // floordata and the sectors stay (Tr5Level::addSightWindow).
    struct SightWindow
    {
        const char* level;                  // file name without extension
        int room, sx, sz, sx2, sz2;         // the run on room's border ring
        int toRoom;                         // the room behind the opening
        const char* reason;
    };

    //------------------------------------------------------------------------
    // Andrea2: the vault of the arch between rooms 231 and 229 shows black.
    //
    // Room 229 is the passage under the arch, x 58368..59392, its ceiling at
    // -1024; room 102 sits on top of it as the arch's vault, a gable whose
    // ceiling rises from -1024 at both sides to -1536 in the middle (ROOF
    // entries on 102's sectors (1,1) and (1,2)). Room 231 opens onto the
    // passage through its portal 4 to 229, from -1024 down to its floor at
    // 256. Above that portal room 231's wall has no face - its faces at
    // x 58368 stop at -1792 - and neither has room 102 on that side; the
    // arch's own shape there is a static of room 231 (slot 40, its box x
    // 58368..58958, y -1792..-1024). So the gable is open in the geometry,
    // but no window describes it: 231 has none to 102, 102 none to 231.
    //
    // How it shows: the camera follows Lara through the arch at about -1030
    // to -1170, above the passage's ceiling, and from there the only ways
    // into 102 and into 235 (the room behind the arch) are the ceiling
    // windows 229 -> 102, 144 -> 235 and 236 -> 235, which face away from
    // an eye above them. The walk lists neither 102 nor 235, and what is
    // seen of them past the arch's statics is the clear colour - 19,160 to
    // 67,896 pixels of 1280 by 720 at four eyes of the arch (RoomProbe).
    //
    // The window is written as Core writes the other gable end, 102's
    // portal 1 to 235 and 235's portal 6 back: one record per room over
    // both sectors, z 40960..43008, from -1536 to -1024 (the corner-exact
    // opening of 231's sectors (7,3) and (7,4) against 102's (1,1) and
    // (1,2)). The floordata stays: whatever moves through that column
    // already goes 231 -> 229 by the DOOR on 231's (7,3) and (7,4) and up
    // into 102 by 229's vertical link. Besides the drawing, a window only
    // adds a neighbour to the room lists the object tests build from windows
    // (GetCollidedObjects, CollideStaticObjects, LaraBaddieCollision in
    // collide.cpp): room 102 holds no item and no static, and with the
    // camera in 102 the list gains room 231's, which a camera in 229 brings
    // already. No face is opened either, so the Remaster's art of the arch
    // stays whole and no cut is recorded.
    //------------------------------------------------------------------------
    static const SightWindow kSightWindows[] =
    {
        { "Andrea2", 231, 7, 3, 7, 4, 102,
          "the arch's vault: room 102 seen from room 231 and back" },
    };

    // Not in this list, though black shows there as well - in each case the
    // window stands in the data and is right, so no entry here can mend it:
    //   - JOBY2, the double door between rooms 106 and 22 (items 128 and
    //     129): the window is there; the closed door is an object, and an
    //     object farther away than the far plane (20480) is not drawn.
    //   - ANDREA1, rooms 103 and 59 behind 64 and 105, and ANDY3, room 95
    //     behind 39, each for one frame: the windows are there, and the walk
    //     refuses them by their projection against the window it holds.
    //   - JOBY5, rooms 16 and 40: a fixed camera's cut puts the eye on the
    //     floor window's own plane for one frame.

    // Apply all entries for this level. Failed entries end up as warnings in
    // "warnings"; the run continues, because a fix that was not applied does
    // not make the level any worse than before.
    // Returns the number of fixes applied.
    // freedItems, if given, collects the items whose pinning trigger was
    // rewritten. The caller must erase them from RandoInput::triggerPinned -
    // LevelFix deliberately knows nothing about RandoInput.
    int apply(Tr5Level& level, const std::string& levelName,
                 std::vector<std::string>& warnings,
                 std::vector<int>* freedItems = nullptr);
}

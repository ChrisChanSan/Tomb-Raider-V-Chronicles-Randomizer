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
#include <cstdint>
#include <cstddef>

//----------------------------------------------------------------------------
// Level chains and inventory cleanup (stages 2/3 of the plan in Level Infos\
// Level-Order-Plan.md).
//
// THE INSIGHT BEHIND ALL OF IT (name resolution of the object commands,
// 2026-08-22): the inventory slots are GENERIC. PUZZLE_ITEM1 is called
// "Garden Key" in Andrea1, "Chaff flares" in joby4, "Heart" in andy1 and
// "Helipad access key" in rich3 - every level renames the same slot. A
// Garden Key carried along BECOMES Chaff flares in joby4. Therefore:
//
//   1. Chains (kLevelChains): what one level genuinely needs from another.
//      If the donor comes after the taker (or a level in between takes the
//      item away), the script hands it over via GIVEOBJ (give = true) -
//      including the name command and the model. give = false means: just
//      don't clean it up while it travels (story items whose absence
//      probably blocks nothing - playtesting decides).
//   2. Cleanup (kAnbieter + kAufraeumen): at the start of every level,
//      TAKEOBJ removes everything from the key family that an EARLIER
//      level could have offered and that neither this level nor a later
//      one imports via a chain. Plus the "Grappling Gun" - that is the
//      CROSSBOW slot (95 + ammo), which only has a model and anims in
//      rich3 (hard crash when equipping it anywhere else, seen by the
//      user in rich2). Roses (PICKUP_ITEM4) are left untouched.
//
// Sources: the key matrix (ItemCheck) and the object commands per block.
// Curation happens here, not in the UI.
//----------------------------------------------------------------------------
struct LevelChain
{
    const char* taker;
    const char* donor;
    int         slot;       // model slot (objects.h), 0 = do not copy a model
    int         inv;        // inventory number (invobj_types)
    uint8_t     nameCmd;    // name command in SCRIPT.DAT, 0 = none
    bool        give;      // true: hand over via GIVEOBJ; false: just do not clean up
    const char* name;
};

// Opcodes of the name commands (enum gf_commands, gameflow.h): CMD_KEY1 = 0x92,
// CMD_PUZZLE1 = 0x9E, CMD_PICKUP1 = 0xAA.
constexpr uint8_t chainCmdKey(int n)    { return static_cast<uint8_t>(0x92 + (n - 1)); }
constexpr uint8_t chainCmdPuzzle(int n) { return static_cast<uint8_t>(0x9E + (n - 1)); }
constexpr uint8_t chainCmdPickup(int n) { return static_cast<uint8_t>(0xAA + (n - 1)); }

// Inventory numbers (invobj_types, newinv2.h)
constexpr int INV_PUZZLE(int n) { return 29 + (n - 1); }
constexpr int INV_KEY(int n)    { return 53 + (n - 1); }
constexpr int INV_PICKUP(int n) { return 77 + (n - 1); }
constexpr int INV_CROWBAR       = 90;
constexpr int INV_CROSSBOW      = 95;   // "Grappling Gun" in rich3
constexpr int INV_BURNING_TORCH = 89;
constexpr int INV_EXAMINE(int n) { return 91 + (n - 1); }   // 91..93

// The chloroform set from rich1 (13th Floor): the bottle, the cloth, and the
// soaked cloth the two combine into. Three inventory slots of their own,
// sitting behind the examine objects - which is why a sweep over the puzzle,
// key and pickup families never touched them, and why they travelled on into
// rich2 and rich3. Seen by the user in a full playthrough.
static const int kChloroform[] = { 94 /*wet cloth*/, 97 /*cloth*/, 98 /*bottle*/, 0 };

// ARCHITECTURE (user decision after the playtest, 2026-08-22):
// keys/puzzles/pickups NEVER travel between levels - at every level start
// the whole family is reset (cleanupInventory). Only weapons, ammo,
// medipacks and flares are kept. Whatever a level needs from another one it
// therefore ALWAYS receives via GIVEOBJ, regardless of the order - "give"
// is no longer a condition here but the list of grants. Exceptions from the
// reset take care of themselves: what a level's own block grants (the Iris
// in rich2 via the vanilla script, the grants below) is never taken away.
static const LevelChain kLevelChains[] =
{
    { "joby3",   "joby2",   203, INV_KEY(8), chainCmdKey(8), true, "Swipe Card / Key 8 (joby2 -> joby3)" },
    { "Andrea3", "Andrea2", 240, INV_CROWBAR, 0,              true, "Crowbar (Andrea2 -> Andrea3)" },

    // TWO entries that must NOT appear here - both times I had concluded
    // "lock present, item missing, so it must be an import". The level
    // matrix cannot decide that:
    //
    // joby5 / Key 7: the cutscene joby9 grants the silver key inside the
    //   level (deltapak.cpp: joby9_end -> AddDisplayPickup(KEY_ITEM7), the
    //   admiral hands it over). Cutscenes are the second item source - as
    //   is PICKUP_ITEM1 in Andrea3 (deltapak.cpp:1767).
    //
    // Andy2 / Puzzle 2 (catapult): Andy2 does have Puzzle Hole 2 in room 9,
    // and its trigger_flags 1058 (>1024) mean, according to
    // PuzzleHoleCollision, "start cutscene 34 on solving" - which per the
    // cutsel table belongs to LVL5_LABYRINTH. Still, the lock is
    // REPURPOSED: it runs in the background, Lara does not use a catapult
    // there (checked in game, user statement). A catapult in the starting
    // inventory would just be a pointless item.
    //
    // Rule of thumb: a lock without a matching item in the level proves
    // nothing. Before any new chain, ask "who else grants it?" - cutscene,
    // enemy drop, script - and when in doubt, playtest.
};

constexpr size_t kLevelChainCount = sizeof(kLevelChains) / sizeof(kLevelChains[0]);

// Combo halves: when the finished item is taken away, its halves go with it
// (otherwise half a catapult fork from andy1 stays in the inventory forever).
constexpr int INV_PUZZLE_COMBO(int n, int h) { return 37 + 2 * (n - 1) + (h - 1); }
constexpr int INV_KEY_COMBO(int n, int h)    { return 61 + 2 * (n - 1) + (h - 1); }
constexpr int INV_PICKUP_COMBO(int n, int h) { return 81 + 2 * (n - 1) + (h - 1); }

// The crossbow family ("Grappling Gun"): weapon plus all ammo types.
static const int kCrossbowFamily[] = { INV_CROSSBOW, 96, 5, 6, 15, 16, 0 };

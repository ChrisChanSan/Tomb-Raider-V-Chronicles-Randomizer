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
// Level chains and inventory cleanup.
//
// Inventory slots are generic: PUZZLE_ITEM1 is "Garden Key" in Andrea1,
// "Chaff flares" in joby4, "Heart" in andy1 and "Helipad access key" in
// rich3, so a key carried along changes its name. Therefore:
//
//   1. Chains (kLevelChains): what one level genuinely needs from another.
//      If the donor comes after the taker (or a level in between takes the
//      item away), the script hands it over via GIVEOBJ (give = true) with
//      the name command and the model; give = false only keeps it from
//      being cleaned up while it travels.
//   2. Cleanup (cleanupInventory in ScriptDat.cpp): every level start
//      TAKEOBJs the key family an earlier level could have offered and no
//      chain imports, plus the "Grappling Gun" (CROSSBOW slot 95 + ammo,
//      model and anims only in rich3; equipped elsewhere it crashes). Roses
//      (PICKUP_ITEM4) are left untouched.
//
// Sources: the key matrix (ItemCheck) and the object commands per block.
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
// sitting behind the examine objects, which is why a sweep over the puzzle,
// key and pickup families never touches them, and why they travel on into
// rich2 and rich3.
static const int kChloroform[] = { 94 /*wet cloth*/, 97 /*cloth*/, 98 /*bottle*/, 0 };

// Keys, puzzles and pickups never travel between levels - at every level
// start the whole family is reset (cleanupInventory). Only weapons, ammo,
// medipacks and flares are kept. Whatever a level needs from another one it
// therefore always receives via GIVEOBJ, regardless of order - "give" is
// not a condition here but the list of grants. Exceptions from the reset
// take care of themselves: what a level's own block grants (the Iris in
// rich2 via the vanilla script, the grants below) is never taken away.
static const LevelChain kLevelChains[] =
{
    { "joby3",   "joby2",   203, INV_KEY(8), chainCmdKey(8), true, "Swipe Card / Key 8 (joby2 -> joby3)" },
    { "Andrea3", "Andrea2", 240, INV_CROWBAR, 0,              true, "Crowbar (Andrea2 -> Andrea3)" },

    // Two entries that must NOT appear here, even though a lock with no
    // matching item in the level might look like an import - the level
    // matrix cannot decide that by itself:
    //
    // joby5 / Key 7: the cutscene joby9 grants the silver key inside the
    //   level (deltapak.cpp: joby9_end -> AddDisplayPickup(KEY_ITEM7), the
    //   admiral hands it over). Cutscenes are the second item source - as
    //   is PICKUP_ITEM1 in Andrea3 (also deltapak.cpp).
    //
    // Andy2 / Puzzle 2 (catapult): Andy2 does have Puzzle Hole 2 in room 9,
    // and its trigger_flags 1058 (>1024) mean, according to
    // PuzzleHoleCollision, "start cutscene 34 on solving" - which per the
    // cutsel table belongs to LVL5_LABYRINTH. Still, the lock is
    // repurposed: it runs in the background, Lara does not use a catapult
    // there. A catapult in the starting inventory would just be a
    // pointless item.
    //
    // A lock without a matching item in the level proves nothing by
    // itself; check whether a cutscene, enemy drop or script grants it
    // before adding a chain.
};

constexpr size_t kLevelChainCount = sizeof(kLevelChains) / sizeof(kLevelChains[0]);

// Combo halves: when the finished item is taken away, its halves go with it
// (otherwise half a catapult fork from andy1 stays in the inventory forever).
constexpr int INV_PUZZLE_COMBO(int n, int h) { return 37 + 2 * (n - 1) + (h - 1); }
constexpr int INV_KEY_COMBO(int n, int h)    { return 61 + 2 * (n - 1) + (h - 1); }
constexpr int INV_PICKUP_COMBO(int n, int h) { return 81 + 2 * (n - 1) + (h - 1); }

// The crossbow family ("Grappling Gun"): weapon plus all ammo types.
static const int kCrossbowFamily[] = { INV_CROSSBOW, 96, 5, 6, 15, 16, 0 };

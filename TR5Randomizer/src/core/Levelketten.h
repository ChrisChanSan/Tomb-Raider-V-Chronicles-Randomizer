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
// Levelketten und Inventar-Aufraeumen (Stufe 2/3 des Plans in Level Infos\
// Levelreihenfolge-Plan.md).
//
// DIE EINSICHT HINTER ALLEM (Namensaufloesung der Objektbefehle, 2026-08-22):
// die Inventar-Slots sind GENERISCH. PUZZLE_ITEM1 heisst in Andrea1 "Garden
// Key", in joby4 "Chaff flares", in andy1 "Heart", in rich3 "Helipad access
// key" - jedes Level benennt denselben Slot um. Ein mitgeschleppter Garden
// Key WIRD in joby4 zu Chaff flares. Darum:
//
//   1. Ketten (kLevelketten): was ein Level aus einem anderen wirklich
//      braucht. Liegt der Spender hinter dem Nehmer (oder nimmt ein Level
//      dazwischen das Item weg), reicht das Skript es per GIVEOBJ nach
//      (geben = true) - samt Namensbefehl und Modell. geben = false heisst:
//      nur nicht wegraeumen, solange es unterwegs ist (Story-Items, deren
//      Fehlen wohl nichts sperrt - Spieltest entscheidet).
//   2. Aufraeumen (kAnbieter + kAufraeumen): beim Start jedes Levels wird
//      per TAKEOBJ alles aus der Schluesselfamilie weggenommen, was ein
//      FRUEHERES Level anbieten konnte und das weder dieses noch ein
//      spaeteres Level per Kette importiert. Plus die "Grappling Gun" -
//      das ist der CROSSBOW-Slot (95 + Munition), der nur in rich3 Modell
//      und Anims hat (Hard-Crash beim Ausruesten anderswo, vom Nutzer
//      in rich2 gesehen). Rosen (PICKUP_ITEM4) bleiben unangetastet.
//
// Quellen: Schluesselmatrix (ItemPruef) und die Objektbefehle je Block.
// Kuratiert wird hier, nicht in der Oberflaeche.
//----------------------------------------------------------------------------
struct Levelkette
{
    const char* nehmer;
    const char* spender;
    int         slot;       // Modellslot (objects.h), 0 = kein Modell kopieren
    int         inv;        // Inventarnummer (invobj_types)
    uint8_t     nameCmd;    // Namensbefehl der SCRIPT.DAT, 0 = keiner
    bool        geben;      // true: per GIVEOBJ nachreichen; false: nur nicht wegraeumen
    const char* name;
};

// Opcodes der Namensbefehle (enum gf_commands, gameflow.h): CMD_KEY1 = 0x92,
// CMD_PUZZLE1 = 0x9E, CMD_PICKUP1 = 0xAA.
constexpr uint8_t kettenCmdKey(int n)    { return static_cast<uint8_t>(0x92 + (n - 1)); }
constexpr uint8_t kettenCmdPuzzle(int n) { return static_cast<uint8_t>(0x9E + (n - 1)); }
constexpr uint8_t kettenCmdPickup(int n) { return static_cast<uint8_t>(0xAA + (n - 1)); }

// Inventarnummern (invobj_types, newinv2.h)
constexpr int INV_PUZZLE(int n) { return 29 + (n - 1); }
constexpr int INV_KEY(int n)    { return 53 + (n - 1); }
constexpr int INV_PICKUP(int n) { return 77 + (n - 1); }
constexpr int INV_CROWBAR       = 90;
constexpr int INV_CROSSBOW      = 95;   // "Grappling Gun" in rich3

static const Levelkette kLevelketten[] =
{
    // --- echte Importe (geben) ---
    { "joby3",   "joby2",   203, INV_KEY(8),    kettenCmdKey(8),    true,  "Swipe Card / Key 8 (joby2 -> joby3)" },
    { "joby5",   "joby2",   202, INV_KEY(7),    kettenCmdKey(7),    true,  "Silver key / Key 7 (joby2 -> joby5)" },
    { "Andy2",   "andy1",   173, INV_PUZZLE(2), kettenCmdPuzzle(2), true,  "Catapult / Puzzle 2 (andy1 -> Andy2)" },
    { "Andrea3", "Andrea2", 240, INV_CROWBAR,   0,                  true,  "Brechstange (Andrea2 -> Andrea3)" },
    // --- Story-Items, die unterwegs sind: nicht wegraeumen, nicht geben ---
    { "rich3",   "rich2",   0,   INV_PUZZLE(2), 0,                  false, "Iris artifact (rich2 -> rich3)" },
    { "andy3",   "Andy2",   0,   INV_PICKUP(1), 0,                  false, "Bestiary (Andy2 -> andy3)" },
    { "joby5",   "joby4",   0,   INV_PICKUP(1), 0,                  false, "Spear of Destiny (joby4 -> joby5)" },
    { "Andy2",   "andy1",   0,   INV_PUZZLE(3), 0,                  false, "Iron clapper / Puzzle 3 (andy1 -> Andy2)" },
    { "joby5",   "joby2",   0,   INV_KEY(8),    0,                  false, "Swipe Card / Key 8 (joby2 -> joby5)" },
};

constexpr size_t kLevelkettenZahl = sizeof(kLevelketten) / sizeof(kLevelketten[0]);

// Was ein Level der Schluesselfamilie ins Inventar bringen kann (Items im
// Level; Skript-Gaben und Ketten-Gaben kommen zur Laufzeit dazu).
// Aus der Schluesselmatrix, Kombi-Haelften auf das fertige Item abgebildet.
struct Anbieter
{
    const char* level;
    int         inv[8];     // 0 = Ende
};

static const Anbieter kAnbieter[] =
{
    { "Andrea1", { INV_PICKUP(2), INV_KEY(1), INV_PUZZLE(1), 0 } },
    { "Andrea2", { INV_CROWBAR, INV_PUZZLE(1), INV_PUZZLE(3), INV_PUZZLE(4), INV_PUZZLE(5), 0 } },
    { "Andrea3", { INV_PICKUP(1), INV_KEY(1), INV_KEY(2), INV_PUZZLE(4), 0 } },
    { "joby2",   { INV_KEY(7), INV_KEY(8), INV_PUZZLE(1), 0 } },
    { "joby3",   { INV_KEY(6), INV_KEY(7), INV_PUZZLE(1), INV_PUZZLE(3), INV_PUZZLE(4), 0 } },
    { "joby4",   { INV_PUZZLE(1), INV_PICKUP(1), 0 } },
    { "joby5",   { INV_KEY(6), INV_KEY(8), INV_PUZZLE(1), INV_PUZZLE(2), 0 } },
    { "andy1",   { INV_PUZZLE(1), INV_PUZZLE(2), INV_PUZZLE(3), 0 } },
    { "Andy2",   { INV_PICKUP(1), 0 } },
    { "andy3",   { INV_PUZZLE(1), INV_PUZZLE(2), INV_PUZZLE(3), 0 } },
    { "rich1",   { INV_PICKUP(2), INV_KEY(7), INV_KEY(8), INV_PUZZLE(3), INV_PUZZLE(4), INV_PICKUP(1), 0 } },
    { "rich2",   { INV_PICKUP(2), INV_PUZZLE(3), INV_PICKUP(1), 0 } },
    { "rich3",   { INV_PUZZLE(1), INV_CROSSBOW, 0 } },
};

constexpr size_t kAnbieterZahl = sizeof(kAnbieter) / sizeof(kAnbieter[0]);

// Kombi-Haelften: wird das fertige Item weggenommen, die Haelften gleich mit
// (sonst bleibt eine halbe Katapult-Gabel aus andy1 ewig im Inventar).
constexpr int INV_PUZZLE_COMBO(int n, int h) { return 37 + 2 * (n - 1) + (h - 1); }
constexpr int INV_KEY_COMBO(int n, int h)    { return 61 + 2 * (n - 1) + (h - 1); }
constexpr int INV_PICKUP_COMBO(int n, int h) { return 81 + 2 * (n - 1) + (h - 1); }

// Die Crossbow-Familie ("Grappling Gun"): Waffe + alle Munitionen.
static const int kCrossbowFamilie[] = { INV_CROSSBOW, 96, 5, 6, 15, 16, 0 };

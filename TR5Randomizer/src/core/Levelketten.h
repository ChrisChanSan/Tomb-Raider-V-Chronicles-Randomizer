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
// Levelketten (Stufe 2 des Plans in Level Infos\Levelreihenfolge-Plan.md):
// welches Level braucht ein Item, das vanilla aus einem FRUEHEREN Level im
// Inventar mitkommt? Liegt der Spender nach einer Permutation hinter dem
// Nehmer, bekommt der Nehmer das Item per CMD_GIVEOBJ beim Start - und
// IMMER sein Modell (Slot-Kopie vom Spender) sowie den Namensbefehl
// (CMD_KEYn/PUZZLEn) im Skriptblock, sonst steht ein modell-/namenloses
// Item im Inventar (vom Nutzer bei Cheat-Items gesehen).
//
// Quelle: Schluesselmatrix aller Level (ItemPruef, 2026-08-22). Die
// Zeilen mit "PRUEFEN" stammen aus Schlosszaehlungen, die Flipmap-Dubletten
// enthalten koennen - der Spieltest entscheidet, sie bleiben bis dahin
// auskommentiert. Kuratiert wird hier, nicht in der Oberflaeche.
//
// Felder: Nehmer, Spender (Dateinamen ohne Endung), Modellslot (objects.h),
// Inventarnummer (invobj_types), Namensbefehl der SCRIPT.DAT (0 = keiner),
// Klartext fuer die Anzeige.
//----------------------------------------------------------------------------
struct Levelkette
{
    const char* nehmer;
    const char* spender;
    int         slot;
    int         inv;
    uint8_t     nameCmd;
    const char* name;
};

// Opcodes der Namensbefehle (enum gf_commands, gameflow.h): CMD_KEY1 = 0x92,
// CMD_PUZZLE1 = 0x9E, CMD_PICKUP1 = 0xAA.
constexpr uint8_t kettenCmdKey(int n)    { return static_cast<uint8_t>(0x92 + (n - 1)); }
constexpr uint8_t kettenCmdPuzzle(int n) { return static_cast<uint8_t>(0x9E + (n - 1)); }

static const Levelkette kLevelketten[] =
{
    // joby3: KEY_HOLE8 x2, kein KEY_ITEM8 - der Schluessel kam vanilla aus
    // joby2 im Inventar (darum brauchte der Secret-Spot dort nur das Modell).
    { "joby3", "joby2", 203, 60, kettenCmdKey(8),    "Key Item 8 (joby2 -> joby3)" },
    // joby5: KEY_HOLE7 ohne KEY_ITEM7 - Spender ist joby2 (zwei Schluessel 7).
    { "joby5", "joby2", 202, 59, kettenCmdKey(7),    "Key Item 7 (joby2 -> joby5)" },
    // Andy2: PUZZLE_HOLE1/2 ohne Items - andy1 traegt PUZZLE2 (als Haelften)
    // und PUZZLE3 ueber sein eigenes Schloss hinaus.
    { "Andy2", "andy1", 173, 30, kettenCmdPuzzle(2), "Puzzle Item 2 (andy1 -> Andy2)" },
    // PRUEFEN: { "Andy2", "andy1", 172, 29, kettenCmdPuzzle(1), "Puzzle Item 1 (andy1 -> Andy2)" },
    // PRUEFEN: { "joby5", "joby2", 203, 60, kettenCmdKey(8),    "Key Item 8 (joby2 -> joby5)" },
    // PRUEFEN: { "Andrea3", "Andrea2", 240, 90, 0,              "Brechstange (Andrea2 -> Andrea3)" },
};

constexpr size_t kLevelkettenZahl = sizeof(kLevelketten) / sizeof(kLevelketten[0]);

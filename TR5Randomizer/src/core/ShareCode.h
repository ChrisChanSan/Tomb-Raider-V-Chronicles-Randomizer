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
#include "Config.h"

#include <cstdint>
#include <string>

//----------------------------------------------------------------------------
// The share code: one line of text that carries the seed text and every
// setting a run depends on, so a second player - or a bug report - gets the
// same game by pasting it.
//
// It is a TRANSPORT of settings and nothing else: no field of it enters a
// draw. What a run rolls still comes from the seed text and the switches,
// exactly as if they had been set by hand.
//
// What travels is every Config field except the ones that belong to one
// machine: the folders (sourceDir, tr3Dir, tr3GoldDir), the window size
// and the UI language.
//
// The text:
//
//   TR5-A0R-XXXXX-XXXXX-...-XX-CC
//       |||  payload       check
//       ||build id (two digits)
//       |format letter
//
//   Digits are Crockford's base 32: 0-9 and A-Z without I, L, O, U. The
//   reader ignores '-', blanks and case and takes O as 0, I and L as 1.
//   The payload is a byte string, written as 5-bit digits from the most
//   significant bit on; the bits after the last byte are zero. The groups
//   of five are for the eye only.
//
//   The check is CRC-10 (polynomial 0x233, start 0, no reflection, no
//   final xor) over the format letter as an ASCII byte, the build id as
//   one byte and the payload bytes, written as two digits. A burst of up
//   to ten bits is always caught, so is every single mistyped digit and
//   every swap of two neighbours.
//
//   The build id is RANDO_BUILD_ID: a code of another build is refused as
//   a whole, because the same switches may draw another game there.
//
// Payload of format A, in this order:
//
//   byte  flags 1: bit 0 equipment, 1 key items, 2 extra enemies,
//         3 enemy swap, 4 outfits, 5 TR3 outfits, 6 secret rewards,
//         7 background music
//   byte  flags 2: bit 0 music triggers, 1 level sections, 2 weather,
//         3 weapon damage rolled, 4 enemy health rolled, 5 enemy damage
//         rolled; bits 6 and 7 zero
//   byte  extra enemies per level, 1..20
//   byte  enemy pool, 0..2
//   byte  item amount, 0..2
//   byte  secrets kind, 0..3
//   byte  level order, 0..2
//   byte  weapon damage in percent, 0..200
//   byte  enemy health in percent, 50..200
//   byte  enemy damage in percent, 50..200
//   text  the seed text, at most 63 bytes (the GUI's field)
//   list  the deselected enemy kinds: a varint count, then each type id
//         as the varint difference to the one before it (the first to 0),
//         ascending, every id above 0
//   text  the secret list's author
//   list  the deselected outfits: a varint count, then each id as a text,
//         in ascending byte order
//
//   A text is a varint length and that many UTF-8 bytes. A varint is
//   seven bits per byte, low bits first, the top bit set on every byte
//   but the last. Every byte must be used: a payload that ends early or
//   carries bytes after the last list is refused.
//
// The two sets hold the DESELECTION, as Config does: a kind or an outfit
// added in a later build is not in an old code and so stays on.
//----------------------------------------------------------------------------
namespace ShareCode
{
    // The code of the settings in c, for this build.
    std::string make(const Config& c);

    // The same for another build id - the self-test's way to a code this
    // build must refuse. make(c) is makeForBuild(c, RANDO_BUILD_ID).
    std::string makeForBuild(const Config& c, int buildId);

    // Reads a code into the travelling fields of c and leaves the others
    // alone. On any refusal c is not touched at all and err says why.
    bool apply(const std::string& code, Config& c, std::string& err);

    // True if the travelling fields of a and b are the same.
    bool sameSettings(const Config& a, const Config& b);

    // The value a run of the GUI draws from: the seed text, mixed with the
    // switches that change the game. The CLI needs the same value to repeat
    // a GUI run from a code, so the one formula lives here.
    uint64_t effectiveSeed(const Config& c);
}

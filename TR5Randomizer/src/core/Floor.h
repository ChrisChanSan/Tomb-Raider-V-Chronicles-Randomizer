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
// Floor slope of a sector - shared knowledge between ApplyPatch (reads the
// floordata), the randomizer (places items) and FloorCheck (verifies
// finished levels).
//
// Everything here is ported VERBATIM from the engine, nothing invented:
//  - GetHeight, control.cpp:1013-1035 (TILT) and 1078-1160 (splits):
//    the floor height at a point within the sector.
//  - CheckNoColFloorTriangle, control.cpp:2960-3009: which triangle of a
//    NOCOLF sector has no floor collision.
//  - The walkability limit: |slope| > 2 sets height_type to BIG_SLOPE or
//    DIAGONAL (control.cpp:1019-1022 and 1148-1149), and that is exactly
//    what Lara's standing, walking and sliding paths check (lara.cpp:1428,
//    collide.cpp:160ff). Up to slope 2 Lara stands and walks normally -
//    such slopes are fully valid item spots, Core itself places pickups
//    there (with a hand-set y on the true floor height).
//
// Why this is needed: a placed item is drawn by the engine at exactly its y.
// On a slope, however, the sector's base value (floor byte << 8) is the
// HIGHEST corner - an item in the middle of the sector would hang up to half
// a sector in the air (seen in game, #86). Whoever wants to place on slopes
// must compute the y with the same formula the engine uses to find the floor.
//----------------------------------------------------------------------------
#include <cstdint>

// The slope entry of a sector, raw: first word (function + hadj fields) and
// the extra word (slopes or corner nibbles). Sectors without an entry are
// flat and need no SlopeInfo.
struct SlopeInfo
{
    uint16_t typeWord = 0;
    uint16_t dataWord = 0;
};

// Marker for "floordata chain unreadable": floorPosAt then returns
// walkable = false and the sector stays off-limits for items.
const uint16_t kSlopeUnreadable = 0xFFFF;

struct FloorPos
{
    int  y = 0;             // floor height at the queried point
    bool walkable = true;   // false: slide (>2) or collision-free triangle
};

// Floor position at a point within the sector. localX/localZ are the position
// INSIDE the sector (0..1023) - room origins are sector-aligned, so this is
// exactly the engine's (x & 1023) and the world coordinate is not needed.
// baseY is the sector's base value (floor byte << 8; in the zone export the
// sector's floorY).
inline FloorPos floorPosAt(const SlopeInfo& si, int baseY, int localX, int localZ)
{
    FloorPos out;
    out.y = baseY;

    if (si.typeWord == kSlopeUnreadable) { out.walkable = false; return out; }

    const int fn = si.typeWord & 0x1F;
    const int dx = localX & 1023;
    const int dz = localZ & 1023;

    int xoff = 0, yoff = 0;

    if (fn == 2)                                        // TILT_TYPE
    {
        // control.cpp:1014-1015: signed bytes of the data word.
        xoff = static_cast<int16_t>(si.dataWord) >> 8;
        yoff = static_cast<int8_t>(si.dataWord & 0xFF);
    }
    else if (fn == 7 || fn == 8 || (fn >= 11 && fn <= 14))   // SPLIT1/2, NOCOLF*
    {
        // control.cpp:1084-1143. Family 1 (SPLIT1/NOCOLF1*) splits along one
        // diagonal, family 2 along the other; each triangle has its own
        // slopes (from the corner nibbles) and its own height adjustment
        // (hadj, 5 signed bits in the type word).
        const int t0 = si.dataWord & 0xF;
        const int t1 = (si.dataWord >> 4) & 0xF;
        const int t2 = (si.dataWord >> 8) & 0xF;
        const int t3 = (si.dataWord >> 12) & 0xF;

        const bool family1 = (fn == 7 || fn == 11 || fn == 12);
        const bool firstHalf = family1 ? (dx > 1024 - dz) : (dx > dz);

        int hadj;
        if (firstHalf)
        {
            hadj = (si.typeWord >> 5) & 0x1F;
            if (hadj & 0x10) hadj |= ~0x1F;
            if (family1) { xoff = t3 - t0; yoff = t3 - t2; }
            else          { xoff = t3 - t0; yoff = t0 - t1; }
        }
        else
        {
            hadj = (si.typeWord >> 10) & 0x1F;
            if (hadj & 0x10) hadj |= ~0x1F;
            if (family1) { xoff = t2 - t1; yoff = t0 - t1; }
            else          { xoff = t2 - t1; yoff = t3 - t2; }
        }
        out.y += hadj << 8;

        // CheckNoColFloorTriangle (control.cpp:2976-3002): if the point lies
        // in the collision-free triangle, there is no floor there.
        if ((fn == 11 && dx <= 1024 - dz) ||    // NOCOLF1T
            (fn == 12 && dx >  1024 - dz) ||    // NOCOLF1B
            (fn == 13 && dx <= dz)         ||   // NOCOLF2T
            (fn == 14 && dx >  dz))             // NOCOLF2B
            out.walkable = false;
    }
    else
        return out;                              // no floor entry: flat

    // control.cpp:1024-1032 and 1151-1159, taken over character for character.
    if (xoff < 0) out.y -= (xoff * dz) >> 2;
    else          out.y += (xoff * ((-1 - dz) & 1023)) >> 2;
    if (yoff < 0) out.y -= (yoff * dx) >> 2;
    else          out.y += (yoff * ((-1 - dx) & 1023)) >> 2;

    // BIG_SLOPE/DIAGONAL: Lara slides here, she cannot pick anything up.
    if (xoff < -2 || xoff > 2 || yoff < -2 || yoff > 2)
        out.walkable = false;

    return out;
}

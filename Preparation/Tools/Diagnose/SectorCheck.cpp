// SectorCheck - shows one sector in plain text: the raw 8 FLOOR_INFO bytes
// and the decoded floordata chain.
//
// Built for Make_Portal: before portals get WRITTEN, the working neighbor
// sectors must be READABLE as templates - and the result checked afterwards
// against exactly this display. No other tool shows the sector level.
//
// FLOOR_INFO (global/types.h), 8 bytes per sector:
//   +0 u16  index      pointer into the floordata (0 = no chain)
//   +2 u16  fx:4 | box:11 | stopper:1
//   +4 u8   pit_room   room below the floor (255 = none)
//   +5 s8   floor      floor height in steps of 256
//   +6 u8   sky_room   room above the ceiling (255 = none)
//   +7 s8   ceiling    ceiling height in steps of 256
//
// Usage: SectorCheck <level.trc> <room> [sx sz]
//        without sx/sz all sectors of the room.
#include "core/Tr5Level.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

static const char* fnName(int fn)
{
    switch (fn)
    {
    case 0:  return "FLOOR";
    case 1:  return "DOOR";          // portal: the next word is the target room
    case 2:  return "TILT";
    case 3:  return "ROOF";
    case 4:  return "TRIGGER";
    case 5:  return "LAVA";
    case 6:  return "CLIMB";
    case 7:  return "SPLIT1";
    case 8:  return "SPLIT2";
    case 9:  return "SPLIT3";
    case 10: return "SPLIT4";
    case 11: return "NOCOLF1T";
    case 12: return "NOCOLF1B";
    case 13: return "NOCOLF2T";
    case 14: return "NOCOLF2B";
    case 15: return "NOCOLC1T";
    case 16: return "NOCOLC1B";
    case 17: return "NOCOLC2T";
    case 18: return "NOCOLC2B";
    case 19: return "MONKEY";
    case 20: return "TRIGTRIGGER";
    default: return "?";
    }
}

static void showSector(const Tr5Level& lvl, const Tr5Room& r, int sx, int sz)
{
    const std::vector<uint8_t>& b = lvl.debugBytes();
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    if (at + 8 > b.size()) { std::printf("  (%d,%d): outside the file\n", sx, sz); return; }

    uint16_t index, w2;
    std::memcpy(&index, &b[at], 2);
    std::memcpy(&w2, &b[at + 2], 2);
    const unsigned pit = b[at + 4];
    const int      flr = static_cast<int8_t>(b[at + 5]);
    const unsigned sky = b[at + 6];
    const int      cei = static_cast<int8_t>(b[at + 7]);

    const int wx = r.originX + sx * 1024;
    const int wz = r.originZ + sz * 1024;

    std::printf("sector (%d,%d)  world X %d..%d, Z %d..%d\n", sx, sz, wx, wx + 1024, wz, wz + 1024);
    std::printf("  raw: %02X %02X  %02X %02X  %02X %02X %02X %02X\n",
                b[at], b[at+1], b[at+2], b[at+3], b[at+4], b[at+5], b[at+6], b[at+7]);
    std::printf("  index=%u  fx=%u box=%u stopper=%u\n", index, w2 & 0xF, (w2 >> 4) & 0x7FF, w2 >> 15);
    std::printf("  pit_room=%s  floor=%d (y %d)  sky_room=%s  ceiling=%d (y %d)%s\n",
                pit == 255 ? "-" : std::to_string(pit).c_str(), flr, flr * 256,
                sky == 255 ? "-" : std::to_string(sky).c_str(), cei, cei * 256,
                (flr == -127 || flr == 127) ? "   [WALL]" : "");

    if (!index) { std::printf("  floordata: none\n\n"); return; }

    std::vector<Tr5FloorEntry> chain;
    if (!lvl.readChain(index, chain)) { std::printf("  floordata: chain not readable!\n\n"); return; }

    for (const Tr5FloorEntry& e : chain)
    {
        std::printf("  floordata: %-12s", fnName(e.function));
        if (e.function == 1 && e.words.size() >= 2)
            std::printf(" -> room %u", e.words[1]);
        else if (e.function == 6 && !e.words.empty())
        {
            const unsigned b6 = (e.words[0] >> 8) & 0xF;
            std::printf(" directions:%s%s%s%s", (b6&1)?" 0":"", (b6&2)?" 90":"", (b6&4)?" 180":"", (b6&8)?" 270":"");
        }
        else if ((e.function == 2 || e.function == 3) && e.words.size() >= 2)
        {
            // ATTENTION, Core's naming is twisted (control.cpp):
            //   xoff = word >> 8   (HIGH byte) and tilts along Z
            //   yoff = word & 0xFF (LOW byte) and tilts along X
            // What stands here is therefore what the tilt really ACTS on.
            const int alongZ = static_cast<int8_t>(e.words[1] >> 8);
            const int alongX = static_cast<int8_t>(e.words[1] & 0xFF);
            std::printf(" tilt X:%d Z:%d (%d resp. %d units per sector)",
                        alongX, alongZ, alongX * 256, alongZ * 256);
        }
        else if (e.function == 4)
            std::printf(" kind=%d", e.kind);
        std::printf("   [");
        for (size_t i = 0; i < e.words.size(); ++i)
            std::printf("%s%04X", i ? " " : "", e.words[i]);
        std::printf("]\n");
    }
    std::printf("\n");
}

// The room's 32-byte portal entries: target room, normal, window polygon.
// They lie right behind the sector field (u16 count, then 32 bytes each);
// the vertices are ROOM-LOCAL. Template for Make_Portal - and the
// cross-check afterwards.
static void showPortals(const Tr5Level& lvl, const Tr5Room& r)
{
    const std::vector<uint8_t>& b = lvl.debugBytes();
    const size_t listAt = r.sectorsOffset +
        static_cast<size_t>(r.numXSectors) * r.numZSectors * 8;
    uint16_t count;
    std::memcpy(&count, &b[listAt], 2);
    std::printf("%u portal entries:\n", count);

    for (unsigned i = 0; i < count; ++i)
    {
        const size_t at = listAt + 2 + static_cast<size_t>(i) * 32;
        auto s16 = [&](size_t o) { int16_t v; std::memcpy(&v, &b[at + o], 2); return static_cast<int>(v); };
        std::printf("  [%2u] -> room %-3d  normal (%d, %d, %d)\n",
                    i, s16(0), s16(2), s16(4), s16(6));
        for (int v = 0; v < 4; ++v)
            std::printf("        V%d local (%6d, %6d, %6d)  world (%d, %d, %d)\n", v,
                        s16(8 + v * 6), s16(10 + v * 6), s16(12 + v * 6),
                        r.originX + s16(8 + v * 6), s16(10 + v * 6), r.originZ + s16(12 + v * 6));
    }
    std::printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: SectorCheck <level.trc> <room> [sx sz]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error)) { std::printf("load: %s\n", error.c_str()); return 1; }

    const int room = std::atoi(argv[2]);
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (room < 0 || room >= static_cast<int>(rooms.size())) { std::printf("room %d does not exist\n", room); return 1; }
    const Tr5Room& r = rooms[static_cast<size_t>(room)];
    if (!r.sectorsOffset) { std::printf("room %d: sector field not resolved\n", room); return 1; }

    std::printf("%s room %d: origin (%d, %d), yBottom/minfloor %d, %dx%d sectors, portal list: ",
                argv[1], room, r.originX, r.originZ, r.yBottom, r.numXSectors, r.numZSectors);
    for (size_t i = 0; i < r.portalRooms.size(); ++i)
        std::printf("%s%d", i ? " " : "", r.portalRooms[i]);
    if (r.portalRooms.empty()) std::printf("(empty)");
    std::printf("\n\n");

    if (argc >= 4 && std::strcmp(argv[3], "-portals") == 0)
        showPortals(lvl, r);
    else if (argc >= 5)
        showSector(lvl, r, std::atoi(argv[3]), std::atoi(argv[4]));
    else
        for (int sx = 0; sx < r.numXSectors; ++sx)
            for (int sz = 0; sz < r.numZSectors; ++sz)
                showSector(lvl, r, sx, sz);
    return 0;
}

// RoomCheck - shows flags and flipmap partners of rooms in a .trc.
//
// SFXLOG names obj=51 in rooms 57/59 of Andrea1 with a "water only" step,
// although no water is reachable there. The question to the file: do these
// rooms (or their flip variants) carry the water bit 0x1?
//
// Usage: RoomCheck <level.trc> [room ...]    without rooms: all with the water bit
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

static void line(const Tr5Level& lvl, int r)
{
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (r < 0 || r >= (int)rooms.size()) { std::printf("room %d: out of range (0..%zu)\n", r, rooms.size() - 1); return; }
    const Tr5Room& rm = rooms[(size_t)r];
    // World position too, and the portal list. Two rooms can only share a
    // portal where they actually touch, and without the origins that cannot
    // be judged - a wrong room number in a secret list looks exactly like a
    // missing view window until you see the coordinates.
    std::printf("room %3d: flags=0x%04X%s  altRoom=%d  %dx%d sectors  "
                "x %d..%d  z %d..%d  yBottom %d\n",
                r, rm.flags, (rm.flags & 1) ? " WATER" : "", rm.altRoom,
                rm.numXSectors, rm.numZSectors,
                rm.originX, rm.originX + rm.numXSectors * 1024,
                rm.originZ, rm.originZ + rm.numZSectors * 1024,
                rm.yBottom);
    {
        // The engine does NOT find the portal list where we do: it follows
        // the stored offset door (header +4), and an ODD value is its mark
        // for "this room has none" (aFixUpRoom, alexstuff.cpp). A room that gained
        // its first portal keeps that mark unless it is written, and then
        // the engine sees no neighbour at all.
        unsigned d = 0, fl = 0; std::string herr;
        if (lvl.headerField(r, 4, d, herr) && lvl.headerField(r, 8, fl, herr))
            std::printf("          door 0x%X%s   floor 0x%X\n",
                        d, (d & 1) ? "  <-- ODD: engine sees NO portals" : "", fl);
    }
    if (!rm.portalRooms.empty())
    {
        std::printf("          view windows to:");
        for (size_t k = 0; k < rm.portalRooms.size(); ++k)
            std::printf(" %d", rm.portalRooms[k]);
        std::printf("\n");

        // The records themselves, because a wrong one does not fail - it
        // draws a hole. Only by holding one against a hand-built portal can
        // a generated one be judged.
        // The vertical links too. A stacked pair is joined through these
        // sector bytes; a floordata door is the HORIZONTAL kind and sends the
        // height query round in a circle when both rooms overlap.
        std::vector<Tr5Level::SectorInfo> secs;
        std::string serr;
        if (lvl.sectorInfo(r, secs, serr))
            for (size_t k = 0; k < secs.size(); ++k)

                    std::printf("          sector (x%d,z%d) below %3d above %3d  "
                                "floor %7d ceil %7d  fd %5d box %5d  door %3d\n",
                                secs[k].sx, secs[k].sz, secs[k].roomBelow, secs[k].roomAbove,
                                secs[k].floor * 256, secs[k].ceiling * 256,
                                secs[k].fdIndex, secs[k].box, secs[k].doorTo);

        std::vector<Tr5Level::PortalRecord> recs;
        std::string perr;
        if (lvl.portalRecords(r, recs, perr))
            for (size_t k = 0; k < recs.size(); ++k)
            {
                std::printf("          -> room %3d  normal %2d,%2d,%2d  corners",
                            recs[k].toRoom, recs[k].nx, recs[k].ny, recs[k].nz);
                for (int q = 0; q < 4; ++q)
                    std::printf("  (%d,%d,%d)", recs[k].v[q][0], recs[k].v[q][1], recs[k].v[q][2]);
                std::printf("\n");
            }
    }
    if (rm.altRoom >= 0 && rm.altRoom < (int)rooms.size())
    {
        const Tr5Room& alt = rooms[(size_t)rm.altRoom];
        std::printf("   flip partner %3d: flags=0x%04X%s\n",
                    rm.altRoom, alt.flags, (alt.flags & 1) ? " WATER" : "");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: RoomCheck <level.trc> [room ...]\n"); return 1; }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error)) { std::printf("not loadable: %s\n", error.c_str()); return 1; }

    if (argc > 2)
    {
        for (int i = 2; i < argc; ++i) line(lvl, std::atoi(argv[i]));
        return 0;
    }

    std::printf("%s: %d rooms, all with the water bit:\n", argv[1], lvl.roomCount());
    for (int r = 0; r < lvl.roomCount(); ++r)
        if (lvl.rooms()[(size_t)r].flags & 1) line(lvl, r);
    return 0;
}

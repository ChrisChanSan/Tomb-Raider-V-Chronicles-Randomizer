// RaumPruef - shows flags and flipmap partners of rooms in a .trc.
//
// Occasion (#111 follow-up, the water-step phantom): SFXLOG names obj=51
// in rooms 57/59 of Andrea1 with a "water only" step, and the user is sure
// no water is reachable there. The question to the file: do these rooms
// (or their flip variants) carry the water bit 0x1?
//
// Usage: RaumPruef <level.trc> [room ...]    without rooms: all with the water bit
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
    std::printf("room %3d: flags=0x%04X%s  altRoom=%d  %dx%d sectors\n",
                r, rm.flags, (rm.flags & 1) ? " WATER" : "", rm.altRoom,
                rm.numXSectors, rm.numZSectors);
    if (rm.altRoom >= 0 && rm.altRoom < (int)rooms.size())
    {
        const Tr5Room& alt = rooms[(size_t)rm.altRoom];
        std::printf("   flip partner %3d: flags=0x%04X%s\n",
                    rm.altRoom, alt.flags, (alt.flags & 1) ? " WATER" : "");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: RaumPruef <level.trc> [room ...]\n"); return 1; }

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

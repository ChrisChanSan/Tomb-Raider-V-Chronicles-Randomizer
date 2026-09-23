/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */

// HdmDump - the HD markers of a finished level: the header block of the
// .trc and the .hdm sidecar beside it, in plain text.
//
// Written because both halves of the marker are invisible otherwise. data[3]
// sits in 32 bytes the engine reads before it loads a single room, and no
// existing tool shows that block at all; the sidecar is text, but reading it
// by eye says nothing about whether it agrees with the file it belongs to.
// This tool holds the two against each other in one line each.
//
// It doubles as the reference reader for the engine side: the parse loop in
// readHdm() below is the whole grammar, about fifty lines, and the engine's
// own reader has to answer the same way on the same file.
//
// Two sidecar versions exist. Version 1 can only name a whole room ("this
// room's shape changed, draw it classic"); version 2 adds the `cut` line, a
// box in world units round one opening the run made, and the engine drops
// the HD triangles inside that box out of every room's art and keeps the
// rooms. Both are read here.
//
// What the header block means (specific\file.cpp, LoadLevel):
//   data[0]  LaraDrawType - 1   (0 NORMAL, 1 YOUNG, 2 BUNHEAD, 3 CATSUIT,
//                                4 DIVESUIT, 5 PONYTAIL)
//   data[1]  WeatherType        (0 dry, 1 rain, 2 snow)
//   data[2]  foreign speech heads, 1 = skip the cutscene head swap
//   data[3]  HD outfit marker, HD_LARA_OUTFIT_* (0 none, 5 ANTARC,
//            6 NEVADA, 7 COAST, 8 CATSUIT)
//   data[4..15]  free - 0 in all 17 shipped files, measured.
//
// Usage: HdmDump <level.trc> [more levels...]
//        HdmDump -raw <level.hdm>      just parse a sidecar
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

static const char* outfitName(int hd)
{
    switch (hd)
    {
    case 0: return "none (the body is whatever data[0] says)";
    case 5: return "TR3_ANTARC  (wardrobe tr3_antarctica)";
    case 6: return "TR3_NEVADA  (wardrobe tr3_nevada)";
    case 7: return "TR3_COAST   (wardrobe tr3_jungle)";
    case 8: return "TR3_CATSUIT (wardrobe tr3_london)";
    default: return "NOT A TOMB RAIDER III OUTFIT - the engine refuses it";
    }
}

//----------------------------------------------------------------------------
// The sidecar reader. This is the whole format.
//----------------------------------------------------------------------------
struct HdMarkers
{
    int version = 0;
    int outfit = 0;
    std::vector<int> rooms;
    // Version 2: one opening the run made, as a box in world units with
    // ascending corners. Y grows downward, so mins[1] is the ceiling of the
    // opening and maxs[1] its floor.
    struct Cut { int mins[3], maxs[3]; };
    std::vector<Cut> cuts;
    struct Donor { int slot, fromSlot; std::string level; };
    std::vector<Donor> donors;
    std::vector<std::string> unknown;      // lines a later version may add
};

static bool readHdm(const std::string& path, HdMarkers& out, std::string& error)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "no sidecar file"; return false; }

    char line[512];
    while (std::fgets(line, sizeof(line), f))
    {
        // Tolerate \r: the file is written with plain \n, but a user may
        // have saved it out of an editor.
        size_t n = std::strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n) continue;

        int a = 0, b = 0;
        char word[64] = { 0 };

        if (std::sscanf(line, "hdm %d", &a) == 1) { out.version = a; continue; }
        if (std::sscanf(line, "outfit %d", &a) == 1) { out.outfit = a; continue; }
        if (std::sscanf(line, "room %d", &a) == 1) { out.rooms.push_back(a); continue; }
        {
            int box[6] = { 0, 0, 0, 0, 0, 0 };
            if (std::sscanf(line, "cut %d %d %d %d %d %d",
                            &box[0], &box[1], &box[2], &box[3], &box[4], &box[5]) == 6)
            {
                HdMarkers::Cut c;
                // Sorted, like the engine's reader: a box the wrong way
                // round would match no triangle and would do it silently.
                for (int k = 0; k < 3; ++k)
                {
                    c.mins[k] = box[k] < box[k + 3] ? box[k] : box[k + 3];
                    c.maxs[k] = box[k] < box[k + 3] ? box[k + 3] : box[k];
                }
                out.cuts.push_back(c);
                continue;
            }
        }
        if (std::sscanf(line, "donor %d %63s %d", &a, word, &b) == 3)
        {
            HdMarkers::Donor d;
            d.slot = a;
            d.level = word;
            d.fromSlot = b;
            out.donors.push_back(d);
            continue;
        }
        out.unknown.push_back(line);
    }
    std::fclose(f);

    // Version 2 only adds the `cut` line and changes the meaning of nothing
    // that was there before, so a version 1 file is a version 2 file with no
    // cut in it and both are read the same way. The engine's own reader says
    // the same in the same words - specific\hd\hd_markers.cpp, and these two
    // have to answer alike on one file.
    if (out.version != 1 && out.version != 2)
    {
        error = "version " + std::to_string(out.version) +
                " is neither the 1 nor the 2 this reader knows";
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------

static std::string sidecarPath(const std::string& levelPath)
{
    std::string p = levelPath;
    const size_t slash = p.find_last_of("/\\");
    const size_t dot = p.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        p.erase(dot);
    return p + ".hdm";
}

static void report(const HdMarkers& m)
{
    std::printf("  sidecar     version %d, %zu rooms, %zu cut boxes, %zu donor slots\n",
                m.version, m.rooms.size(), m.cuts.size(), m.donors.size());
    if (m.outfit) std::printf("  outfit line %d\n", m.outfit);

    if (!m.rooms.empty())
    {
        std::printf("  rooms      ");
        for (size_t i = 0; i < m.rooms.size(); ++i)
            std::printf("%s%d", i ? ", " : " ", m.rooms[i]);
        std::printf("   (drawn CLASSIC - nothing of their HD art is kept)\n");
    }

    // One line per opening, with the size the box really covers. The spans
    // are what a reader checks the box against. Exactly one of the three is
    // 2 * 512 = 1024: that is the opening's normal, and the raw box is flat
    // there - a window's four corners share their x or their z, @Clear only
    // accepts such faces, and a vertical link writes a zero-height plane.
    // The other two are the opening's own size less 2 * 128, the inward
    // step that keeps the passage's own floor and ceiling out of the box.
    for (size_t i = 0; i < m.cuts.size(); ++i)
    {
        const HdMarkers::Cut& c = m.cuts[i];
        std::printf("  cut %-3zu     x %d..%d  y %d..%d  z %d..%d"
                    "   (%d x %d x %d, y grows downward)\n",
                    i, c.mins[0], c.maxs[0], c.mins[1], c.maxs[1],
                    c.mins[2], c.maxs[2],
                    c.maxs[0] - c.mins[0], c.maxs[1] - c.mins[1],
                    c.maxs[2] - c.mins[2]);
    }
    for (size_t i = 0; i < m.donors.size(); ++i)
        std::printf("  donor       slot %d <- %s slot %d\n",
                    m.donors[i].slot, m.donors[i].level.c_str(), m.donors[i].fromSlot);
    for (size_t i = 0; i < m.unknown.size(); ++i)
        std::printf("  UNKNOWN     %s\n", m.unknown[i].c_str());
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Usage: HdmDump <level.trc> [more levels...]\n");
        std::printf("       HdmDump -raw <level.hdm>\n");
        std::printf("\nShows a level's HD markers: data[3] of the level\n"
                    "header and the .hdm sidecar beside the file.\n");
        return 2;
    }

    if (std::strcmp(argv[1], "-raw") == 0)
    {
        if (argc < 3) { std::printf("-raw needs a .hdm file\n"); return 2; }
        HdMarkers m;
        std::string err;
        if (!readHdm(argv[2], m, err)) { std::printf("%s: %s\n", argv[2], err.c_str()); return 1; }
        std::printf("%s\n", argv[2]);
        report(m);
        return 0;
    }

    int bad = 0;
    for (int i = 1; i < argc; ++i)
    {
        Tr5Level lvl;
        std::string err;
        std::printf("%s\n", argv[i]);
        if (!lvl.load(toW(argv[i]), err))
        {
            std::printf("  NOT READABLE: %s\n", err.c_str());
            ++bad;
            continue;
        }

        std::printf("  data[0]     %d  LaraDrawType %d\n", lvl.laraType(), lvl.laraType() + 1);
        std::printf("  data[1]     %d  weather (0 dry, 1 rain, 2 snow)\n", lvl.weatherType());
        std::printf("  data[2]     %d  foreign speech heads\n", lvl.foreignTalkHeads());
        std::printf("  data[3]     %d  %s\n", lvl.hdOutfit(), outfitName(lvl.hdOutfit()));

        const std::string side = sidecarPath(argv[i]);
        HdMarkers m;
        std::string sErr;
        if (!readHdm(side, m, sErr))
        {
            std::printf("  sidecar     %s (the engine reads that as \"no markers\")\n", sErr.c_str());
            continue;
        }
        report(m);

        // The one cross-check the two halves allow: the sidecar repeats
        // data[3], and a disagreement would mean one of the two was written
        // by an older run.
        if (m.outfit != lvl.hdOutfit())
        {
            std::printf("  MISMATCH    data[3] is %d, the sidecar says %d\n",
                        lvl.hdOutfit(), m.outfit);
            ++bad;
        }
    }
    return bad ? 1 : 0;
}

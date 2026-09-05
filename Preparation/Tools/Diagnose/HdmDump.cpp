// HdmDump - the HD markers of a finished level (#123, stage S7): the header
// block of the .trc and the .hdm sidecar beside it, in plain text.
//
// Written because both halves of the marker are invisible otherwise. data[3]
// sits in 32 bytes the engine reads before it loads a single room, and no
// existing tool shows that block at all; the sidecar is text, but reading it
// by eye says nothing about whether it agrees with the file it belongs to.
// This tool holds the two against each other in one line each.
//
// It doubles as the REFERENCE READER for the engine side (stage S7-2): the
// parse loop in readHdm() below is the whole grammar, about forty lines, and
// the engine's own reader has to answer the same way on the same file.
//
// What the header block means (specific\file.cpp, LoadLevel):
//   data[0]  LaraDrawType - 1   (0 NORMAL, 1 YOUNG, 2 BUNHEAD, 3 CATSUIT,
//                                4 DIVESUIT, 5 PONYTAIL)
//   data[1]  WeatherType        (0 dry, 1 rain, 2 snow)            #144
//   data[2]  foreign speech heads, 1 = skip the cutscene head swap
//   data[3]  HD outfit marker, HD_LARA_OUTFIT_* (0 none, 5 ANTARC,
//            6 NEVADA, 7 COAST, 8 CATSUIT)                         #123
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

    if (out.version != 1)
    {
        error = "version " + std::to_string(out.version) + " is not the 1 this reader knows";
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
    std::printf("  sidecar     version %d, %zu rooms, %zu donor slots\n",
                m.version, m.rooms.size(), m.donors.size());
    if (m.outfit) std::printf("  outfit line %d\n", m.outfit);

    if (!m.rooms.empty())
    {
        std::printf("  rooms      ");
        for (size_t i = 0; i < m.rooms.size(); ++i)
            std::printf("%s%d", i ? ", " : " ", m.rooms[i]);
        std::printf("\n");
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
        std::printf("\nShows the HD markers of stage S7: data[3] of the level\n"
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

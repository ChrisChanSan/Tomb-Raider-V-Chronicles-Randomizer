/*
 * Tr3Probe - writes the package that the RANDOMIZER's own converter
 * (Tr3Source::fetchMoveable) produces, so it can be held against the one
 * from the TR3Fetch tool.
 *
 * The two are twins by design: the tool is what we develop and measure
 * with, the library is what ships. Twins drift, and they drift silently -
 * this project has paid for that once already with the zone exports. A
 * byte comparison is the only thing that proves they still agree.
 *
 *     Tr3Probe <tr3level.TR2> <objectId> <out.bin> [-shiny n] [-recolor r,g,b=r,g,b]
 *
 * This file is part of the TR5 Randomizer. GNU GPL v3.
 */

#include "core/Tr3Source.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    while (*s) w.push_back((wchar_t)(unsigned char)*s++);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: Tr3Probe <tr3level.TR2> <objectId> <out.bin> "
                    "[-shiny n] [-recolor r,g,b=r,g,b]\n");
        return 2;
    }

    Tr3Source::Options opt;
    for (int a = 4; a < argc; ++a)
    {
        if (std::strcmp(argv[a], "-shiny") == 0)
        {
            int strength = 128;
            if (a + 1 < argc && argv[a + 1][0] != '-') strength = std::atoi(argv[++a]);
            opt.shinyWord = 2 | ((strength >> 1) & 0x7C);
        }
        else if (std::strcmp(argv[a], "-recolor") == 0 && a + 1 < argc)
        {
            Tr3Source::Recolor rc;
            if (std::sscanf(argv[++a], "%d,%d,%d=%d,%d,%d",
                            &rc.sr, &rc.sg, &rc.sb, &rc.dr, &rc.dg, &rc.db) == 6)
                opt.recolors.push_back(rc);
            else
                std::printf("  -recolor not readable: %s\n", argv[a]);
        }
    }

    std::vector<uint8_t> package;
    std::string error;
    if (!Tr3Source::fetchMoveable(toW(argv[1]), std::atoi(argv[2]), package, error, &opt))
    {
        std::printf("FETCH FAILED: %s\n", error.c_str());
        return 1;
    }

    FILE* f = std::fopen(argv[3], "wb");
    if (!f) { std::printf("target not writable: %s\n", argv[3]); return 2; }
    std::fwrite(package.data(), 1, package.size(), f);
    std::fclose(f);
    std::printf("  written: %s, %zu bytes\n", argv[3], package.size());
    return 0;
}

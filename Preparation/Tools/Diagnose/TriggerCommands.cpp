// TriggerCommands - counts per level which trigger COMMANDS stand in the
// floordata (TO_OBJECT, TO_CAMERA, ..., TO_BODYBAG, ...), and lists, for a
// chosen command type, every occurrence with room and sector.
//
// Occasion (#121, fifth measurement): TO_BODYBAG calls ResetGuards() -
// makes ALL active thinkers invisible and teleports them based on fields
// only vanilla guards carry. If andy3 carries such commands and Andrea1
// does not, the imported soldiers' blinking is explained.
//
// Usage: TriggerCommands <level.trc> [...]           count per level
//        TriggerCommands -show <type> <level.trc>    occurrences of one type
//
// Command word: bits 0-9 value, bits 10-13 command type, bit 15 chain end.
// The first word after the setup word of SWITCH/KEY/PICKUP is a reference
// word, not a command - skipped just like in the engine.
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

static const char* kNames[16] = {
    "OBJECT", "CAMERA", "SINK", "FLIPMAP", "FLIPON", "FLIPOFF", "TARGET",
    "FINISH", "CD", "FLIPEFFECT", "SECRET", "BODYBAG", "FLYBY", "CUTSCENE",
    "?14", "?15"
};

int main(int argc, char** argv)
{
    int showType = -1;
    int start = 1;
    if (argc >= 4 && std::strcmp(argv[1], "-show") == 0)
    {
        showType = std::atoi(argv[2]);
        start = 3;
    }
    if (argc <= start)
    {
        std::printf("usage: TriggerCommands <level.trc> [...]\n"
                    "       TriggerCommands -show <type> <level.trc>\n");
        return 2;
    }

    for (int a = start; a < argc; ++a)
    {
        Tr5Level l;
        std::string e;
        if (!l.load(toW(argv[a]), e)) { std::printf("%s: not loadable\n", argv[a]); continue; }

        const char* n = argv[a];
        for (const char* q = argv[a]; *q; ++q) if (*q == '\\' || *q == '/') n = q + 1;

        long counter[16] = {};

        for (const Tr5Room& r : l.rooms())
        {
            for (int sx = 0; sx < r.numXSectors; ++sx)
                for (int sz = 0; sz < r.numZSectors; ++sz)
                {
                    const int idx = l.sectorIndex(r.index, sx, sz);
                    if (idx <= 0) continue;
                    std::vector<Tr5FloorEntry> chain;
                    if (!l.readChain(idx, chain)) continue;

                    for (const Tr5FloorEntry& fe : chain)
                    {
                        if (fe.function != 4 /* FN_TRIGGER */) continue;
                        // words[0] = function word, [1] = setup, then commands.
                        size_t i = 2;
                        // SWITCH(2)/KEY(3)/PICKUP(4) insert a reference word.
                        if (fe.kind == 2 || fe.kind == 3 || fe.kind == 4) i = 3;
                        for (; i < fe.words.size(); ++i)
                        {
                            const int typ = (fe.words[i] >> 10) & 0xF;
                            ++counter[typ];
                            if (typ == showType)
                                std::printf("  %s room %3d sector %d,%d  value %d  trigger kind %d\n",
                                            n, r.index, sx, sz, fe.words[i] & 0x3FF, fe.kind);
                            // FLYBY consumes an extra word.
                            if (typ == 12) ++i;
                        }
                    }
                }
        }

        std::printf("%-14s", n);
        for (int t = 0; t < 14; ++t)
            if (counter[t]) std::printf("  %s=%ld", kNames[t], counter[t]);
        std::printf("\n");
    }
    return 0;
}

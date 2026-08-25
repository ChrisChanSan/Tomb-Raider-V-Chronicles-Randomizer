// MissMod - measures where a TR5 model sits relative to the item origin.
//
// Background: the Crane Guy (slot 55) stands 512 units off the sector
// center in game although the item lies exactly centered. Core only built
// him into joby2 as ANIMATING4 at the crane console - so the offset sits
// in the model itself, in the frame offsets and the frame box.
//
// Usage: MissMod <level.trc> [slot ...]
//
// Output per slot: the first frame's box and its center. A cleanly
// centered model has a center near 0/0 in x and z.

#include "core/Tr5Level.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static int16_t w16(const std::vector<uint8_t>& b, size_t at)
{
    int16_t v; std::memcpy(&v, &b[at], 2); return v;
}
static uint16_t u16(const std::vector<uint8_t>& b, size_t at)
{
    uint16_t v; std::memcpy(&v, &b[at], 2); return v;
}
static uint32_t u32(const std::vector<uint8_t>& b, size_t at)
{
    uint32_t v; std::memcpy(&v, &b[at], 4); return v;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: MissMod <level.trc> [slot ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string error;
    std::string path(argv[1]);
    std::wstring wpath(path.begin(), path.end());
    if (!lvl.load(wpath, error))
    {
        std::printf("load failed: %s\n", error.c_str());
        return 2;
    }

    size_t objAt = 0, frAt = 0;
    int32_t objN = 0, frN = 0;
    if (!lvl.debugModelBlocks(objAt, objN, frAt, frN))
    {
        std::printf("no object table found.\n");
        return 3;
    }

    const std::vector<uint8_t>& b = lvl.debugBytes();
    std::printf("%s: %d moveables, frame slice %d words, %d samples\n\n",
                argv[1], objN, frN, lvl.soundSampleCount());

    // --- Check the sound chain: MissMod <level.trc> -ton <mapId ...> -------
    //
    // Walks the same way the engine walks: SoundMap[id] -> SAMPLE_INFO ->
    // its number and variant count. Needed because with the Compy a
    // foreign sound cut in, and from the inserter's mapping table alone it
    // could not be seen what a place REALLY points at in the end.
    if (argc >= 3 && std::strcmp(argv[2], "-ton") == 0)
    {
        size_t lutAt = 0, infosAt = 0;
        int32_t nInfos = 0, nSamples = 0;
        if (!lvl.debugSoundBlocks(lutAt, infosAt, nInfos, nSamples))
        {
            std::printf("no readable sound block.\n");
            return 4;
        }
        std::printf("SAMPLE_INFOs %d, samples %d\n\n", nInfos, nSamples);
        std::printf("map   info   number  vol  rad  variants\n");
        for (int i = 3; i < argc; ++i)
        {
            const int map = std::atoi(argv[i]);
            if (map < 0 || map >= 450) { std::printf("%3d   outside 0..449\n", map); continue; }
            const int16_t idx = w16(b, lutAt + (size_t)map * 2);
            if (idx < 0 || idx >= nInfos) { std::printf("%3d   %5d   <- empty or out of range\n", map, idx); continue; }

            const size_t at = infosAt + (size_t)idx * 8;
            const int16_t number = w16(b, at);
            const uint8_t vol = b[at + 2];
            const int8_t  rad = (int8_t)b[at + 3];
            const uint16_t fl = u16(b, at + 6);
            std::printf("%3d   %5d   %6d  %3u  %3d  %d%s\n", map, idx, number, vol, rad,
                        (fl >> 2) & 0xF, (number + ((fl >> 2) & 0xF) > nSamples) ? "   RUNS PAST THE END" : "");
        }
        return 0;
    }

    std::vector<int> wanted;
    for (int i = 2; i < argc; ++i) wanted.push_back(std::atoi(argv[i]));

    std::printf("slot  meshes  minX  maxX  minY  maxY  minZ  maxZ | centerX centerZ | offX offY offZ\n");

    for (int i = 0; i < objN; ++i)
    {
        // A TR5 moveable is 20 bytes: the 18 from TR4 plus one fill word.
        size_t at   = objAt + (size_t)i * 20;
        uint32_t id = u32(b, at);
        if (!wanted.empty())
        {
            bool hit = false;
            for (int g : wanted) if ((int)id == g) hit = true;
            if (!hit) continue;
        }

        uint16_t meshes = u16(b, at + 4);
        uint32_t frOff  = u32(b, at + 12);      // byte offset into the frame slice

        size_t f = frAt + frOff;
        if (f + 18 > b.size()) { std::printf("%4u  frame pointer out of range\n", id); continue; }

        int16_t minX = w16(b, f + 0),  maxX = w16(b, f + 2);
        int16_t minY = w16(b, f + 4),  maxY = w16(b, f + 6);
        int16_t minZ = w16(b, f + 8),  maxZ = w16(b, f + 10);
        int16_t offX = w16(b, f + 12), offY = w16(b, f + 14), offZ = w16(b, f + 16);

        std::printf("%4u  %5u  %5d %5d %5d %5d %5d %5d | %7d %7d | %4d %4d %4d\n",
                    id, meshes, minX, maxX, minY, maxY, minZ, maxZ,
                    (minX + maxX) / 2, (minZ + maxZ) / 2, offX, offY, offZ);
    }
    return 0;
}

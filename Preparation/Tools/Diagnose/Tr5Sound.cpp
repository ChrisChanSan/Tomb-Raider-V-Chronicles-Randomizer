// Tr5Sound - reads a TR5 level's sound chain: soundmap entry, SAMPLE_INFO
// and the wave headers of the samples belonging to it.
//
// Occasions (#111 run-up, user finds 2026-08-18):
//  - Maze Monster runs in andy1 with a metallic screech, stacked
//    (SFXLOG: sfx=250 map=84 sample=97 of 1 variants "once").
//  - Guard (460) and Crane Guy (55) die with the wrong family's scream
//    (Rich instead of Joby).
//  - Mafia2 (51) always gives the same death variant in the rando.
//
// The engine plays: lut[sfx] -> SAMPLE_INFO -> wave number..number+n-1,
// variants = (flags >> 2) & 0xF, play mode = flags & 3
// (0 = stacks, 1 = only if silent, 2 = restart, 3 = loop).
// Exactly this view is what the tool reproduces, plus an FNV hash per
// wave, so a sample can be recognized across file borders.
//
// Usage: Tr5Sound <level.trc>              all occupied entries, compact
//        Tr5Sound <level.trc> <sfx> ...    details incl. wave headers
//        Tr5Sound <level.trc> -samples     every sample: sizes, fmt, hash
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

struct SamplePos { size_t dataAt; uint32_t uncomp; uint32_t comp; };

static uint32_t fnv(const uint8_t* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// The stored sample block is a complete WAV file. Read the header without
// trusting it - exactly that mix-up (ADPCM read as PCM) would be a
// candidate for the screech sound.
static void waveHeader(const std::vector<uint8_t>& b, const SamplePos& s, char* out, size_t outN)
{
    if (s.comp < 24 || s.dataAt + s.comp > b.size())
    {
        snprintf(out, outN, "too short for a WAV header");
        return;
    }

    const uint8_t* w = b.data() + s.dataAt;
    char riff[5] = {}, wave[5] = {};
    std::memcpy(riff, w, 4);
    std::memcpy(wave, w + 8, 4);
    uint16_t tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    std::memcpy(&tag, w + 20, 2);
    std::memcpy(&channels, w + 22, 2);
    std::memcpy(&rate, w + 24, 4);
    std::memcpy(&bits, w + 34, 2);

    uint32_t dataLen = 0;
    for (size_t q = 12; q + 8 <= s.comp; )
    {
        uint32_t cl = 0;
        std::memcpy(&cl, w + q + 4, 4);
        if (!std::memcmp(w + q, "data", 4)) { dataLen = cl; break; }
        q += 8 + cl + (cl & 1);
    }

    snprintf(out, outN, "%s/%s fmt=%u (%s) %uch %uHz %ubit data=%u",
             riff, wave, tag, tag == 1 ? "PCM" : tag == 2 ? "MS-ADPCM" : "unknown",
             channels, rate, bits, dataLen);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: Tr5Sound <level.trc> [sfx ... | -samples]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error))
    {
        std::printf("not loadable: %s\n", error.c_str());
        return 1;
    }

    size_t lutAt = 0, infosAt = 0, numSamplesAt = 0;
    int32_t nInfos = 0, nSamples = 0;
    if (!lvl.debugSoundBlocks(lutAt, infosAt, nInfos, nSamples))
    {
        std::printf("sound blocks not found\n");
        return 1;
    }
    lvl.debugSampleArray(numSamplesAt, nSamples);

    const std::vector<uint8_t>& b = lvl.debugBytes();

    // Walk the physical sample chain behind the main block.
    std::vector<SamplePos> positions;
    {
        size_t p = numSamplesAt + 4;
        for (int32_t i = 0; i < nSamples; ++i)
        {
            if (p + 8 > b.size()) { std::printf("sample chain breaks off at %d\n", i); break; }
            SamplePos s;
            std::memcpy(&s.uncomp, b.data() + p, 4);
            std::memcpy(&s.comp, b.data() + p + 4, 4);
            s.dataAt = p + 8;
            if (s.dataAt + s.comp > b.size()) { std::printf("sample %d runs out of the file\n", i); break; }
            positions.push_back(s);
            p = s.dataAt + s.comp;
        }
    }

    std::printf("%s: %d infos, %d samples (chain carries %zu)\n",
                argv[1], nInfos, nSamples, positions.size());

    auto lut = [&](int sfx) -> int16_t
    {
        if (sfx < 0 || sfx >= 450) return -1;
        int16_t v = -1;
        std::memcpy(&v, b.data() + lutAt + (size_t)sfx * 2, 2);
        return v;
    };

    struct Info { int16_t number; uint8_t volume; int8_t radius; uint8_t chance; int8_t pitch; uint16_t flags; };
    auto info = [&](int idx) -> Info
    {
        Info f = {};
        if (idx >= 0 && idx < nInfos)
        {
            const uint8_t* p = b.data() + infosAt + (size_t)idx * 8;
            std::memcpy(&f.number, p, 2);
            f.volume = p[2]; f.radius = (int8_t)p[3]; f.chance = p[4]; f.pitch = (int8_t)p[5];
            std::memcpy(&f.flags, p + 6, 2);
        }
        return f;
    };

    static const char* MODE[4] = { "stacks", "only if silent", "restart", "loop" };

    if (argc >= 3 && !std::strcmp(argv[2], "-samples"))
    {
        std::printf("%7s %8s %8s %10s  %s\n", "sample", "uncomp", "comp", "hash", "wave header");
        for (size_t i = 0; i < positions.size(); ++i)
        {
            char head[160];
            waveHeader(b, positions[i], head, sizeof(head));
            std::printf("%7zu %8u %8u  %08X  %s\n",
                        i, positions[i].uncomp, positions[i].comp,
                        fnv(b.data() + positions[i].dataAt, positions[i].comp), head);
        }
        return 0;
    }

    if (argc >= 3)
    {
        for (int a = 2; a < argc; ++a)
        {
            int sfx = std::atoi(argv[a]);
            int16_t m = lut(sfx);
            std::printf("\nsfx %d -> map %d", sfx, (int)m);
            if (m < 0) { std::printf(" (not occupied)\n"); continue; }
            Info f = info(m);
            int variants = (f.flags >> 2) & 0xF;
            std::printf("\n  number=%d vol=%u radius=%d chance=%u pitch=%d flags=0x%04X"
                        " -> %d variant(s), mode %d (%s)\n",
                        (int)f.number, f.volume, (int)f.radius, f.chance, (int)f.pitch,
                        f.flags, variants, f.flags & 3, MODE[f.flags & 3]);
            if (variants < 1) variants = 1;
            for (int v = 0; v < variants; ++v)
            {
                int s = f.number + v;
                if (s < 0 || (size_t)s >= positions.size())
                {
                    std::printf("  wave %d: OUTSIDE the chain (0..%zu)!\n", s, positions.size() ? positions.size() - 1 : 0);
                    continue;
                }
                char head[160];
                waveHeader(b, positions[(size_t)s], head, sizeof(head));
                std::printf("  wave %d: uncomp=%u comp=%u hash=%08X  %s\n",
                            s, positions[(size_t)s].uncomp, positions[(size_t)s].comp,
                            fnv(b.data() + positions[(size_t)s].dataAt, positions[(size_t)s].comp), head);
            }
        }
        return 0;
    }

    std::printf("%4s %4s %7s %5s %4s %4s %4s %6s  %s\n",
                "sfx", "map", "number", "var", "vol", "rad", "rnd", "mode", "");
    for (int sfx = 0; sfx < 450; ++sfx)
    {
        int16_t m = lut(sfx);
        if (m < 0) continue;
        Info f = info(m);
        // rad in sectors as in the file; the engine turns it into
        // (radius + 1) << 10 units as the hard hearing limit (sound.cpp:203).
        std::printf("%4d %4d %7d %5d %4u %4d %4u %6d  %s\n",
                    sfx, (int)m, (int)f.number, (f.flags >> 2) & 0xF,
                    f.volume, (int)f.radius, f.chance, f.flags & 3,
                    MODE[f.flags & 3]);
    }
    return 0;
}

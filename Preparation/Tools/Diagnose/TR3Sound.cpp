//----------------------------------------------------------------------------
// TR3Sound - resolves a TR3 sound ID down to the wave in MAIN.SFX.
//
// Usage: TR3Sound <level.TR2> <MAIN.SFX> <soundId> [more ids...]
//        TR3Sound <level.TR2> <MAIN.SFX> -package <target.bin> <id> [ids...]
//
// An ID may be written as "88=250": the sound is reachable in the target
// under 88 (that is how the anim commands call it), but the data comes
// from sound 250. Needed for the T-Rex: his commands name 88 ("T-rex
// footstep"), but the sound that actually plays is 250 (two variants,
// muffled).
//
// In package mode a container for the inserter is written:
//   "T35S", version 2, count
//   per sound: origId, volume, chance, chars, variants,
//              then per variant wavLen + wavLen bytes
// The WAVs travel byte-wise unchanged - they are pure PCM in exactly the
// format TR5 expects (mono, 22050 Hz, 16 bit), and the engine has had a
// direct path for that since the XAudio2 port.
//
// The way from the ID to the sound goes through THREE tables, and the
// distance between the two ends is NOT constant:
//
//   SoundMap[id]            -> number of a SoundDetails (or -1)
//   SoundDetails[n].sample  -> number in SampleIndices
//   SampleIndices[s]        -> BYTE offset into MAIN.SFX
//
// A SoundDetails can carry several variants; they lie as CONSECUTIVE
// samples behind the first. That is why every multiply occupied ID shifts
// everything behind it - visible in the TRsound tool as sound 0084 ->
// wave 87, but sound 0087 -> wave 89.
//
// The disputed field sizes at the file end are NOT guessed but tried out:
// only the right combination makes the walk end exactly at the file end.
// The same procedure as with the room header in TR3Peek.
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    struct Reader
    {
        std::vector<uint8_t> b;
        size_t at = 0;
        bool   broken = false;

        bool load(const char* p)
        {
            FILE* f = std::fopen(p, "rb");
            if (!f) return false;
            std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            b.resize(static_cast<size_t>(n));
            const size_t g = std::fread(b.data(), 1, b.size(), f);
            std::fclose(f);
            return g == b.size();
        }
        bool room(size_t n) { if (at + n > b.size()) { broken = true; return false; } return true; }
        uint16_t u16() { if (!room(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
        uint32_t u32() { if (!room(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
        void skip(size_t n) { if (room(n)) at += n; }
    };

    uint16_t g16(const std::vector<uint8_t>& b, size_t p) { uint16_t v; std::memcpy(&v, &b[p], 2); return v; }
    uint32_t g32(const std::vector<uint8_t>& b, size_t p) { uint32_t v; std::memcpy(&v, &b[p], 4); return v; }
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: TR3Sound <level.TR2> <MAIN.SFX> <soundId> [ids...]\n"); return 2; }

    Reader r;
    if (!r.load(argv[1])) { std::printf("level not readable\n"); return 2; }

    Reader sfx;
    if (!sfx.load(argv[2])) { std::printf("MAIN.SFX not readable\n"); return 2; }

    // --- Wave directory of MAIN.SFX: offset and length of every RIFF ------
    std::vector<uint32_t> waveAt, waveLen;
    for (size_t p = 0; p + 8 <= sfx.b.size(); )
    {
        if (sfx.b[p] == 'R' && sfx.b[p+1] == 'I' && sfx.b[p+2] == 'F' && sfx.b[p+3] == 'F')
        {
            const uint32_t len = g32(sfx.b, p + 4) + 8;
            waveAt.push_back(static_cast<uint32_t>(p));
            waveLen.push_back(len);
            p += len;
            if (len & 1) ++p;
        }
        else ++p;
    }
    std::printf("MAIN.SFX: %zu bytes, %zu waves\n", sfx.b.size(), waveAt.size());

    if ((r.u32() >> 24) != 0xFF) { std::printf("not a TR3 level\n"); return 2; }
    r.skip(768); r.skip(1024);
    const uint32_t nTex = r.u32();
    r.skip(size_t(nTex) * 65536); r.skip(size_t(nTex) * 131072);
    r.u32();

    // Room block (field sizes as determined in TR3Peek: ambient 4, light 24)
    const uint16_t nRooms = r.u16();
    for (uint16_t i = 0; i < nRooms && !r.broken; ++i)
    {
        r.skip(16);
        r.skip(size_t(r.u32()) * 2);
        r.skip(size_t(r.u16()) * 32);
        const uint16_t nZ = r.u16(), nX = r.u16();
        r.skip(size_t(nZ) * nX * 8);
        r.skip(4);
        r.skip(size_t(r.u16()) * 24);
        r.skip(size_t(r.u16()) * 20);
        r.skip(2 + 2 + 3);
    }
    r.skip(size_t(r.u32()) * 2);                         // floordata
    r.skip(size_t(r.u32()) * 2);                         // mesh data
    r.skip(size_t(r.u32()) * 4);                         // mesh pointers
    r.skip(size_t(r.u32()) * 32);                        // anims
    r.skip(size_t(r.u32()) * 6);                         // state changes
    r.skip(size_t(r.u32()) * 8);                         // dispatches
    r.skip(size_t(r.u32()) * 2);                         // anim commands
    r.skip(size_t(r.u32()) * 4);                         // bones
    r.skip(size_t(r.u32()) * 2);                         // frames
    r.skip(size_t(r.u32()) * 18);                        // moveables
    if (r.broken) { std::printf("walk up to the moveables failed\n"); return 1; }

    const size_t afterMoveables = r.at;

    // --- Rest of the header: try out the disputed sizes -------------------
    // Four fields are documented differently depending on the source. Right
    // is the combination at which the last table ends EXACTLY at the file
    // end - a wrong assumption otherwise runs past it or leaves a rest.
    size_t soundMapAt = 0, detailsAt = 0, indicesAt = 0;
    int nDetails = 0, nIndices = 0, goodMap = 0, goodItem = 0, goodBox = 0, goodZone = 0;

    for (int mapN = 256; mapN <= 370 && !soundMapAt; mapN += 114)
    for (int itemSz = 22; itemSz <= 24 && !soundMapAt; itemSz += 2)
    for (int boxSz = 8; boxSz <= 20 && !soundMapAt; boxSz += 12)
    for (int zoneN = 10; zoneN <= 20 && !soundMapAt; zoneN += 10)
    {
        r.at = afterMoveables; r.broken = false;
        r.skip(size_t(r.u32()) * 32);                    // static meshes
        r.skip(size_t(r.u32()) * 16);                    // sprite textures
        r.skip(size_t(r.u32()) * 8);                     // sprite sequences
        r.skip(size_t(r.u32()) * 16);                    // cameras
        r.skip(size_t(r.u32()) * 16);                    // sound sources
        const uint32_t nBoxes = r.u32();
        r.skip(size_t(nBoxes) * boxSz);
        r.skip(size_t(r.u32()) * 2);                     // overlaps
        r.skip(size_t(nBoxes) * zoneN * 2);              // zones
        r.skip(size_t(r.u32()) * 2);                     // animated textures
        r.skip(size_t(r.u32()) * 20);                    // object textures
        r.skip(size_t(r.u32()) * itemSz);                // items
        r.skip(8192);                                    // light map
        r.skip(size_t(r.u16()) * 16);                    // cinematic frames
        r.skip(size_t(r.u16()));                         // demo data
        if (r.broken) continue;

        const size_t mapAt = r.at;
        r.skip(size_t(mapN) * 2);
        const size_t detAt = r.at + 4;
        const uint32_t nDet = r.u32();
        r.skip(size_t(nDet) * 8);
        const size_t idxAt = r.at + 4;
        const uint32_t nIdx = r.u32();
        r.skip(size_t(nIdx) * 4);

        // The proof: landed exactly at the file end, and the numbers are
        // plausible.
        if (r.broken || r.at != r.b.size()) continue;
        if (nDet == 0 || nDet > 5000 || nIdx == 0 || nIdx > 5000) continue;

        soundMapAt = mapAt; detailsAt = detAt; indicesAt = idxAt;
        nDetails = int(nDet); nIndices = int(nIdx);
        goodMap = mapN; goodItem = itemSz; goodBox = boxSz; goodZone = zoneN;
    }

    if (!soundMapAt) { std::printf("ERROR: no header variant ends at the file end\n"); return 1; }

    std::printf("level: SoundMap %d entries, %d SoundDetails, %d SampleIndices\n",
                goodMap, nDetails, nIndices);
    std::printf("       (item %d bytes, box %d bytes, zone %d words - self-determined)\n\n",
                goodItem, goodBox, goodZone);

    // --- Wave mode: bypass the level tables -------------------------------
    //
    // Usage: TR3Sound <lvl.TR2> <MAIN.SFX> -waves <target.bin> <id>:<wave>[,<wave>...]
    //
    // Needed for chunnel.TR2 (TR3 Gold): there, the way via
    // SoundMap/SoundDetails/SampleIndices demonstrably delivers wrong
    // waves - the self-calibrated header search finds a combination that
    // ends exactly at the file end, but not the right one. On CRASH.TR2 the
    // same search matches byte for byte.
    //
    // Instead of researching, the wave number LISTENED TO in the TRsound
    // tool is taken directly here. The ID stays necessary anyway: the anim
    // commands call the sound under it, and the rerouting in the inserter
    // goes by it.
    if (argc >= 6 && std::strcmp(argv[3], "-waves") == 0)
    {
        std::vector<uint8_t> out;
        out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('S');
        for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 2 : 0);
        for (int i = 0; i < 4; ++i) out.push_back(0);
        int inPackage = 0;

        auto p16 = [&](uint16_t v) { out.push_back(uint8_t(v & 0xFF)); out.push_back(uint8_t(v >> 8)); };
        auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t((v >> (8*i)) & 0xFF)); };

        std::printf("  ID   variants  waves\n");
        for (int a = 5; a < argc; ++a)
        {
            const char* s = argv[a];
            const int id = std::atoi(s);
            const char* dp = std::strchr(s, ':');
            if (!dp) { std::printf("  %s: expected <id>:<wave>[,<wave>]\n", s); return 2; }

            std::vector<int> waves;
            for (const char* q = dp + 1; *q; )
            {
                waves.push_back(std::atoi(q));
                while (*q && *q != ',') ++q;
                if (*q == ',') ++q;
            }

            for (int w : waves)
                if (w < 1 || size_t(w) > waveAt.size())
                { std::printf("  wave %d does not exist in MAIN.SFX\n", w); return 1; }

            p16(uint16_t(id)); p16(0); p16(0); p16(0);
            p16(uint16_t(waves.size()));
            std::printf("  %3d  %9zu ", id, waves.size());
            for (int w : waves)
            {
                const size_t k = size_t(w) - 1;              // TRsound counts from 1
                p32(waveLen[k]);
                out.insert(out.end(), sfx.b.begin() + waveAt[k], sfx.b.begin() + waveAt[k] + waveLen[k]);
                std::printf(" %d(%u B)", w, waveLen[k]);
            }
            std::printf("\n");
            ++inPackage;
        }

        out[8] = uint8_t(inPackage & 0xFF); out[9] = uint8_t((inPackage >> 8) & 0xFF);
        FILE* f = std::fopen(argv[4], "wb");
        if (!f) { std::printf("target not writable\n"); return 2; }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        std::printf("\n  package: %d sounds, %zu bytes -> %s\n", inPackage, out.size(), argv[4]);
        return 0;
    }

    // Package mode? Then argv[3] is the switch and argv[4] the target.
    const bool package = (argc >= 6 && std::strcmp(argv[3], "-package") == 0);
    const int firstId = package ? 5 : 3;
    std::vector<uint8_t> out;
    int inPackage = 0;
    if (package)
    {
        out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('S');
        for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 2 : 0);   // version 2
        for (int i = 0; i < 4; ++i) out.push_back(0);                // room for the count
    }

    std::printf("  ID   details  sample  variants  wave  offset      length\n");
    for (int a = firstId; a < argc; ++a)
    {
        // "88=250": reachable under 88, data from 250 (see the header).
        int id = std::atoi(argv[a]);
        int source = id;
        if (const char* gl = std::strchr(argv[a], '='))
        {
            source = std::atoi(gl + 1);
            std::printf("  (sound %d takes its data from %d)\n", id, source);
        }
        const int lookup = source;
        if (lookup < 0 || lookup >= goodMap) { std::printf("  %3d  outside the SoundMap\n", id); continue; }

        const int16_t det = static_cast<int16_t>(g16(r.b, soundMapAt + size_t(lookup) * 2));
        if (det < 0 || det >= nDetails) { std::printf("  %3d  not occupied\n", id); continue; }

        const size_t d = detailsAt + size_t(det) * 8;
        const uint16_t sample = g16(r.b, d);
        const uint16_t chars = g16(r.b, d + 6);
        const int variants = (chars >> 2) & 0xF;

        const int varCount = variants > 0 ? variants : 1;
        if (sample + varCount > int(nIndices)) { std::printf("  %3d  sample index out of range\n", id); continue; }

        // SampleIndices carries WAVE NUMBERS, not byte offsets - proven
        // against the comparison numbers of the TRsound tool (sound 84 ->
        // entry 86 -> there wave 87, i.e. 0-based against 1-based).
        const uint32_t nr = g32(r.b, indicesAt + size_t(sample) * 4);
        if (nr >= waveAt.size()) { std::printf("  %3d  wave number outside MAIN.SFX\n", id); continue; }

        std::printf("  %3d  %7d  %6u  %8d  %4u  %-10u  %u\n",
                    id, det, sample, variants, nr + 1, waveAt[nr], waveLen[nr]);

        if (!package) continue;

        // Take volume, random share and the marker word from the
        // SoundDetails along - so the sound sounds in the target like it
        // does in TR3.
        const uint16_t volume = g16(r.b, d + 2);
        const uint16_t chance = g16(r.b, d + 4);
        auto p16 = [&](uint16_t v) { out.push_back(uint8_t(v & 0xFF)); out.push_back(uint8_t(v >> 8)); };
        auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t((v >> (8*i)) & 0xFF)); };
        p16(uint16_t(id)); p16(volume); p16(chance); p16(chars);
        p16(uint16_t(varCount));

        // Take all variants along: they lie as CONSECUTIVE samples behind
        // the first (the engine rolls sample = number + random(N)). For
        // the T-Rex's walking sound there are two - exactly the spot where
        // a single sample sounds wrong.
        for (int v = 0; v < varCount; ++v)
        {
            const uint32_t vnr = g32(r.b, indicesAt + size_t(sample + v) * 4);
            if (vnr >= waveAt.size()) { std::printf("  variant %d lies out of range\n", v); return 1; }
            p32(waveLen[vnr]);
            out.insert(out.end(), sfx.b.begin() + waveAt[vnr],
                                  sfx.b.begin() + waveAt[vnr] + waveLen[vnr]);
            if (v) std::printf("        variant %d: wave %u, %u bytes\n", v + 1, vnr + 1, waveLen[vnr]);
        }
        ++inPackage;
    }

    if (package)
    {
        out[8]  = uint8_t(inPackage & 0xFF);
        out[9]  = uint8_t((inPackage >> 8) & 0xFF);
        FILE* f = std::fopen(argv[4], "wb");
        if (!f) { std::printf("target not writable: %s\n", argv[4]); return 2; }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        std::printf("\n  package: %d sounds, %zu bytes -> %s\n", inPackage, out.size(), argv[4]);
    }
    return 0;
}

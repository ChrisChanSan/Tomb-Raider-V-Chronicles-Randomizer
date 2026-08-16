//----------------------------------------------------------------------------
// TR3Ton - loest eine TR3-Sound-ID bis zum Wave in MAIN.SFX auf (#93).
//
// Aufruf: TR3Ton <level.TR2> <MAIN.SFX> <soundId> [weitere ids...]
//
// Der Weg von der ID zum Klang geht ueber DREI Tabellen, und der Abstand
// zwischen beiden Enden ist NICHT konstant:
//
//   SoundMap[id]            -> Nummer eines SoundDetails (oder -1)
//   SoundDetails[n].sample  -> Nummer in SampleIndices
//   SampleIndices[s]        -> BYTE-Offset in MAIN.SFX
//
// Ein SoundDetails kann mehrere Varianten fuehren; die liegen als
// aufeinanderfolgende SAMPLES hinter dem ersten. Deshalb verschiebt jede
// mehrfach belegte ID alles dahinter - im Werkzeug TRsound sichtbar als
// Sound 0084 -> Wave 87, aber Sound 0087 -> Wave 89.
//
// Die strittigen Feldgroessen im Dateiende werden NICHT geraten, sondern
// durchprobiert: nur die richtige Kombination laesst den Durchlauf exakt am
// Dateiende enden. Dasselbe Verfahren wie beim Raumkopf in TR3Peek.
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    struct Leser
    {
        std::vector<uint8_t> b;
        size_t at = 0;
        bool   kaputt = false;

        bool laden(const char* p)
        {
            FILE* f = std::fopen(p, "rb");
            if (!f) return false;
            std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            b.resize(static_cast<size_t>(n));
            const size_t g = std::fread(b.data(), 1, b.size(), f);
            std::fclose(f);
            return g == b.size();
        }
        bool platz(size_t n) { if (at + n > b.size()) { kaputt = true; return false; } return true; }
        uint16_t u16() { if (!platz(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
        uint32_t u32() { if (!platz(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
        void spring(size_t n) { if (platz(n)) at += n; }
    };

    uint16_t g16(const std::vector<uint8_t>& b, size_t p) { uint16_t v; std::memcpy(&v, &b[p], 2); return v; }
    uint32_t g32(const std::vector<uint8_t>& b, size_t p) { uint32_t v; std::memcpy(&v, &b[p], 4); return v; }
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("Aufruf: TR3Ton <level.TR2> <MAIN.SFX> <soundId> [ids...]\n"); return 2; }

    Leser r;
    if (!r.laden(argv[1])) { std::printf("Level nicht lesbar\n"); return 2; }

    Leser sfx;
    if (!sfx.laden(argv[2])) { std::printf("MAIN.SFX nicht lesbar\n"); return 2; }

    // --- Wave-Verzeichnis von MAIN.SFX: Offset und Laenge jedes RIFF ------
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
    std::printf("MAIN.SFX: %zu Bytes, %zu Waves\n", sfx.b.size(), waveAt.size());

    if ((r.u32() >> 24) != 0xFF) { std::printf("Kein TR3-Level\n"); return 2; }
    r.spring(768); r.spring(1024);
    const uint32_t nTex = r.u32();
    r.spring(size_t(nTex) * 65536); r.spring(size_t(nTex) * 131072);
    r.u32();

    // Raumblock (Feldgroessen wie in TR3Peek ermittelt: Ambient 4, Licht 24)
    const uint16_t nRooms = r.u16();
    for (uint16_t i = 0; i < nRooms && !r.kaputt; ++i)
    {
        r.spring(16);
        r.spring(size_t(r.u32()) * 2);
        r.spring(size_t(r.u16()) * 32);
        const uint16_t nZ = r.u16(), nX = r.u16();
        r.spring(size_t(nZ) * nX * 8);
        r.spring(4);
        r.spring(size_t(r.u16()) * 24);
        r.spring(size_t(r.u16()) * 20);
        r.spring(2 + 2 + 3);
    }
    r.spring(size_t(r.u32()) * 2);                       // Floordata
    r.spring(size_t(r.u32()) * 2);                       // Meshdaten
    r.spring(size_t(r.u32()) * 4);                       // Meshzeiger
    r.spring(size_t(r.u32()) * 32);                      // Anims
    r.spring(size_t(r.u32()) * 6);                       // StateChanges
    r.spring(size_t(r.u32()) * 8);                       // Dispatches
    r.spring(size_t(r.u32()) * 2);                       // AnimCommands
    r.spring(size_t(r.u32()) * 4);                       // Bones
    r.spring(size_t(r.u32()) * 2);                       // Frames
    r.spring(size_t(r.u32()) * 18);                      // Moveables
    if (r.kaputt) { std::printf("Durchlauf bis zu den Moveables gescheitert\n"); return 1; }

    const size_t nachMoveables = r.at;

    // --- Rest des Kopfes: strittige Groessen durchprobieren ---------------
    // Vier Felder sind je nach Quelle unterschiedlich dokumentiert. Richtig
    // ist die Kombination, bei der die letzte Tabelle GENAU am Dateiende
    // endet - eine falsche Annahme laeuft sonst darueber hinaus oder laesst
    // Rest stehen.
    size_t soundMapAt = 0, detailsAt = 0, indicesAt = 0;
    int nDetails = 0, nIndices = 0, gutMap = 0, gutItem = 0, gutBox = 0, gutZone = 0;

    for (int mapN = 256; mapN <= 370 && !soundMapAt; mapN += 114)
    for (int itemSz = 22; itemSz <= 24 && !soundMapAt; itemSz += 2)
    for (int boxSz = 8; boxSz <= 20 && !soundMapAt; boxSz += 12)
    for (int zoneN = 10; zoneN <= 20 && !soundMapAt; zoneN += 10)
    {
        r.at = nachMoveables; r.kaputt = false;
        r.spring(size_t(r.u32()) * 32);                  // StaticMeshes
        r.spring(size_t(r.u32()) * 16);                  // SpriteTextures
        r.spring(size_t(r.u32()) * 8);                   // SpriteSequences
        r.spring(size_t(r.u32()) * 16);                  // Cameras
        r.spring(size_t(r.u32()) * 16);                  // SoundSources
        const uint32_t nBoxes = r.u32();
        r.spring(size_t(nBoxes) * boxSz);
        r.spring(size_t(r.u32()) * 2);                   // Overlaps
        r.spring(size_t(nBoxes) * zoneN * 2);            // Zones
        r.spring(size_t(r.u32()) * 2);                   // AnimatedTextures
        r.spring(size_t(r.u32()) * 20);                  // ObjectTextures
        r.spring(size_t(r.u32()) * itemSz);              // Items
        r.spring(8192);                                  // LightMap
        r.spring(size_t(r.u16()) * 16);                  // CinematicFrames
        r.spring(size_t(r.u16()));                       // DemoData
        if (r.kaputt) continue;

        const size_t mapAt = r.at;
        r.spring(size_t(mapN) * 2);
        const size_t detAt = r.at + 4;
        const uint32_t nDet = r.u32();
        r.spring(size_t(nDet) * 8);
        const size_t idxAt = r.at + 4;
        const uint32_t nIdx = r.u32();
        r.spring(size_t(nIdx) * 4);

        // Der Beweis: exakt am Dateiende gelandet, und die Zahlen plausibel.
        if (r.kaputt || r.at != r.b.size()) continue;
        if (nDet == 0 || nDet > 5000 || nIdx == 0 || nIdx > 5000) continue;

        soundMapAt = mapAt; detailsAt = detAt; indicesAt = idxAt;
        nDetails = int(nDet); nIndices = int(nIdx);
        gutMap = mapN; gutItem = itemSz; gutBox = boxSz; gutZone = zoneN;
    }

    if (!soundMapAt) { std::printf("FEHLER: keine Kopfvariante endet am Dateiende\n"); return 1; }

    std::printf("Level: SoundMap %d Eintraege, %d SoundDetails, %d SampleIndices\n",
                gutMap, nDetails, nIndices);
    std::printf("       (Item %d Byte, Box %d Byte, Zone %d Worte - selbst ermittelt)\n\n",
                gutItem, gutBox, gutZone);

    std::printf("  ID   Details  Sample  Varianten  Wave  Offset      Laenge\n");
    for (int a = 3; a < argc; ++a)
    {
        const int id = std::atoi(argv[a]);
        if (id < 0 || id >= gutMap) { std::printf("  %3d  ausserhalb der SoundMap\n", id); continue; }

        const int16_t det = static_cast<int16_t>(g16(r.b, soundMapAt + size_t(id) * 2));
        if (det < 0 || det >= nDetails) { std::printf("  %3d  nicht belegt\n", id); continue; }

        const size_t d = detailsAt + size_t(det) * 8;
        const uint16_t sample = g16(r.b, d);
        const uint16_t chars = g16(r.b, d + 6);
        const int varianten = (chars >> 2) & 0xF;

        if (sample >= nIndices) { std::printf("  %3d  Sampleindex ausserhalb\n", id); continue; }

        // SampleIndices fuehrt WAVE-NUMMERN, keine Byte-Offsets - an den
        // Vergleichszahlen des Werkzeugs TRsound nachgewiesen (Sound 84 ->
        // Eintrag 86 -> dort Wave 87, also 0-basiert gegen 1-basiert).
        const uint32_t nr = g32(r.b, indicesAt + size_t(sample) * 4);
        if (nr >= waveAt.size()) { std::printf("  %3d  Wave-Nummer ausserhalb von MAIN.SFX\n", id); continue; }

        std::printf("  %3d  %7d  %6u  %9d  %4u  %-10u  %u\n",
                    id, det, sample, varianten, nr + 1, waveAt[nr], waveLen[nr]);
    }
    return 0;
}

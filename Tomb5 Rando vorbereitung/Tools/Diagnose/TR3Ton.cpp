//----------------------------------------------------------------------------
// TR3Ton - loest eine TR3-Sound-ID bis zum Wave in MAIN.SFX auf (#93).
//
// Aufruf: TR3Ton <level.TR2> <MAIN.SFX> <soundId> [weitere ids...]
//         TR3Ton <level.TR2> <MAIN.SFX> -package <ziel.bin> <id> [ids...]
//
// Eine ID darf als "88=250" geschrieben werden: der Klang ist im Ziel unter
// 88 erreichbar (so rufen ihn die Anim-Kommandos), die Daten kommen aber von
// Sound 250. Gebraucht fuer den T-Rex: seine Kommandos nennen 88
// ("T-rex footstep"), der im Spiel gehoerte Laufklang ist aber 250
// (zweivariantig, dumpf) - vom Nutzer am Original gegengehoert.
//
// Im Paketmodus wird ein Container fuer den Einsetzer geschrieben:
//   "T35S", version 2, anzahl
//   je Klang: origId, volume, chance, chars, varianten,
//             dann je Variante wavLen + wavLen Bytes
// Die WAVs wandern byteweise unveraendert - sie sind reines PCM in genau
// dem Format, das TR5 erwartet (mono, 22050 Hz, 16 Bit), und die Engine
// hat dafuer seit der XAudio2-Portierung einen direkten Weg.
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

    // --- Wave-Modus: die Leveltabellen umgehen ----------------------------
    //
    // Aufruf: TR3Ton <lvl.TR2> <MAIN.SFX> -waves <ziel.bin> <id>:<wave>[,<wave>...]
    //
    // Gebraucht fuer chunnel.TR2 (TR3 Gold): dort liefert der Weg ueber
    // SoundMap/SoundDetails/SampleIndices nachweislich falsche Waves - meine
    // Selbstkalibrierung findet zwar eine Kombination, die exakt am Dateiende
    // endet, aber nicht die richtige. Bei CRASH.TR2 stimmte sie noch aufs Byte
    // mit dem Werkzeug des Nutzers ueberein.
    //
    // Statt zu forschen wird hier die im Werkzeug TRsound ABGEHOERTE
    // Wave-Nummer direkt genommen. Die ID bleibt trotzdem noetig: unter ihr
    // rufen die Anim-Kommandos den Klang, und danach richtet sich das
    // Umbiegen im Einsetzer.
    if (argc >= 6 && std::strcmp(argv[3], "-waves") == 0)
    {
        std::vector<uint8_t> out;
        out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('S');
        for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 2 : 0);
        for (int i = 0; i < 4; ++i) out.push_back(0);
        int imPaket = 0;

        auto p16 = [&](uint16_t v) { out.push_back(uint8_t(v & 0xFF)); out.push_back(uint8_t(v >> 8)); };
        auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t((v >> (8*i)) & 0xFF)); };

        std::printf("  ID   Varianten  Waves\n");
        for (int a = 5; a < argc; ++a)
        {
            const char* s = argv[a];
            const int id = std::atoi(s);
            const char* dp = std::strchr(s, ':');
            if (!dp) { std::printf("  %s: erwartet <id>:<wave>[,<wave>]\n", s); return 2; }

            std::vector<int> waves;
            for (const char* q = dp + 1; *q; )
            {
                waves.push_back(std::atoi(q));
                while (*q && *q != ',') ++q;
                if (*q == ',') ++q;
            }

            for (int w : waves)
                if (w < 1 || size_t(w) > waveAt.size())
                { std::printf("  Wave %d gibt es in MAIN.SFX nicht\n", w); return 1; }

            p16(uint16_t(id)); p16(0); p16(0); p16(0);
            p16(uint16_t(waves.size()));
            std::printf("  %3d  %9zu ", id, waves.size());
            for (int w : waves)
            {
                const size_t k = size_t(w) - 1;              // TRsound zaehlt ab 1
                p32(waveLen[k]);
                out.insert(out.end(), sfx.b.begin() + waveAt[k], sfx.b.begin() + waveAt[k] + waveLen[k]);
                std::printf(" %d(%u B)", w, waveLen[k]);
            }
            std::printf("\n");
            ++imPaket;
        }

        out[8] = uint8_t(imPaket & 0xFF); out[9] = uint8_t((imPaket >> 8) & 0xFF);
        FILE* f = std::fopen(argv[4], "wb");
        if (!f) { std::printf("Ziel nicht schreibbar\n"); return 2; }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        std::printf("\n  Paket: %d Klaenge, %zu Byte -> %s\n", imPaket, out.size(), argv[4]);
        return 0;
    }

    // Paketmodus? Dann ist argv[3] der Schalter und argv[4] das Ziel.
    const bool package = (argc >= 6 && std::strcmp(argv[3], "-package") == 0);
    const int erstesId = package ? 5 : 3;
    std::vector<uint8_t> out;
    int imPaket = 0;
    if (package)
    {
        out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('S');
        for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 2 : 0);   // Version 2
        for (int i = 0; i < 4; ++i) out.push_back(0);                // Platz fuer die Anzahl
    }

    std::printf("  ID   Details  Sample  Varianten  Wave  Offset      Laenge\n");
    for (int a = erstesId; a < argc; ++a)
    {
        // "88=250": unter 88 erreichbar, Daten von 250 (siehe Kopf).
        int id = std::atoi(argv[a]);
        int source = id;
        if (const char* gl = std::strchr(argv[a], '='))
        {
            source = std::atoi(gl + 1);
            std::printf("  (Klang %d nimmt die Daten von %d)\n", id, source);
        }
        const int nachschlag = source;
        if (nachschlag < 0 || nachschlag >= gutMap) { std::printf("  %3d  ausserhalb der SoundMap\n", id); continue; }

        const int16_t det = static_cast<int16_t>(g16(r.b, soundMapAt + size_t(nachschlag) * 2));
        if (det < 0 || det >= nDetails) { std::printf("  %3d  nicht belegt\n", id); continue; }

        const size_t d = detailsAt + size_t(det) * 8;
        const uint16_t sample = g16(r.b, d);
        const uint16_t chars = g16(r.b, d + 6);
        const int varianten = (chars >> 2) & 0xF;

        const int varZahl = varianten > 0 ? varianten : 1;
        if (sample + varZahl > int(nIndices)) { std::printf("  %3d  Sampleindex ausserhalb\n", id); continue; }

        // SampleIndices fuehrt WAVE-NUMMERN, keine Byte-Offsets - an den
        // Vergleichszahlen des Werkzeugs TRsound nachgewiesen (Sound 84 ->
        // Eintrag 86 -> dort Wave 87, also 0-basiert gegen 1-basiert).
        const uint32_t nr = g32(r.b, indicesAt + size_t(sample) * 4);
        if (nr >= waveAt.size()) { std::printf("  %3d  Wave-Nummer ausserhalb von MAIN.SFX\n", id); continue; }

        std::printf("  %3d  %7d  %6u  %9d  %4u  %-10u  %u\n",
                    id, det, sample, varianten, nr + 1, waveAt[nr], waveLen[nr]);

        if (!package) continue;

        // Lautstaerke, Zufallsanteil und Kennungswort aus den SoundDetails
        // mitnehmen - damit der Klang im Ziel so klingt wie in TR3.
        const uint16_t volume = g16(r.b, d + 2);
        const uint16_t chance = g16(r.b, d + 4);
        auto p16 = [&](uint16_t v) { out.push_back(uint8_t(v & 0xFF)); out.push_back(uint8_t(v >> 8)); };
        auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t((v >> (8*i)) & 0xFF)); };
        p16(uint16_t(id)); p16(volume); p16(chance); p16(chars);
        p16(uint16_t(varZahl));

        // Alle Varianten mitnehmen: sie liegen als AUFEINANDERFOLGENDE
        // Samples hinter dem ersten (die Engine wuerfelt
        // sample = number + Zufall(N)). Beim Laufklang des T-Rex sind es
        // zwei - genau die Stelle, an der ein Einzelsample falsch sounds.
        for (int v = 0; v < varZahl; ++v)
        {
            const uint32_t vnr = g32(r.b, indicesAt + size_t(sample + v) * 4);
            if (vnr >= waveAt.size()) { std::printf("  Variante %d liegt ausserhalb\n", v); return 1; }
            p32(waveLen[vnr]);
            out.insert(out.end(), sfx.b.begin() + waveAt[vnr],
                                  sfx.b.begin() + waveAt[vnr] + waveLen[vnr]);
            if (v) std::printf("        Variante %d: Wave %u, %u Byte\n", v + 1, vnr + 1, waveLen[vnr]);
        }
        ++imPaket;
    }

    if (package)
    {
        out[8]  = uint8_t(imPaket & 0xFF);
        out[9]  = uint8_t((imPaket >> 8) & 0xFF);
        FILE* f = std::fopen(argv[4], "wb");
        if (!f) { std::printf("Ziel nicht schreibbar: %s\n", argv[4]); return 2; }
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        std::printf("\n  Paket: %d Klaenge, %zu Byte -> %s\n", imPaket, out.size(), argv[4]);
    }
    return 0;
}

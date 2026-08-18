// Tr5Ton - liest die Klangkette eines TR5-Levels: Soundmap-Eintrag,
// SAMPLE_INFO und die Wave-Koepfe der zugehoerigen Samples.
//
// Anlaesse (#111-Vorfeld, Nutzerfunde 2026-08-18):
//  - Maze Monster laeuft in andy1 mit metallischem Kreischen, gestapelt
//    (SFXLOG: sfx=250 map=84 sample=97 von 1 Varianten "einmal").
//  - Wachmann (460) und Crane Guy (55) sterben mit dem Schrei der falschen
//    Familie (Rich statt Joby).
//  - Mafia2 (51) gibt im Rando immer dieselbe Sterbevariante ab.
//
// Die Engine spielt: lut[sfx] -> SAMPLE_INFO -> Wave number..number+n-1,
// Varianten = (flags >> 2) & 0xF, Abspielmodus = flags & 3
// (0 = stapelt, 1 = nur wenn still, 2 = neu starten, 3 = Schleife).
// Genau diese Sicht gibt das Werkzeug wieder, dazu ein FNV-Hash je Wave,
// damit sich ein Sample ueber Dateigrenzen wiedererkennen laesst.
//
// Aufruf: Tr5Ton <level.trc>              alle belegten Eintraege kompakt
//         Tr5Ton <level.trc> <sfx> ...    Details samt Wave-Koepfen
//         Tr5Ton <level.trc> -samples     jedes Sample: Groessen, fmt, Hash
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring zuW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

struct SampleLage { size_t datenAt; uint32_t uncomp; uint32_t comp; };

static uint32_t fnv(const uint8_t* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// Der gespeicherte Sample-Block ist eine komplette WAV-Datei. Kopf lesen,
// ohne ihm zu trauen - genau die Verwechslung (ADPCM als PCM gelesen)
// waere ein Kandidat fuer den Kreisch-Klang.
static void waveKopf(const std::vector<uint8_t>& b, const SampleLage& s, char* aus, size_t ausN)
{
    if (s.comp < 24 || s.datenAt + s.comp > b.size())
    {
        snprintf(aus, ausN, "zu kurz fuer einen WAV-Kopf");
        return;
    }

    const uint8_t* w = b.data() + s.datenAt;
    char riff[5] = {}, wave[5] = {};
    std::memcpy(riff, w, 4);
    std::memcpy(wave, w + 8, 4);
    uint16_t tag = 0, kanaele = 0, bits = 0;
    uint32_t rate = 0;
    std::memcpy(&tag, w + 20, 2);
    std::memcpy(&kanaele, w + 22, 2);
    std::memcpy(&rate, w + 24, 4);
    std::memcpy(&bits, w + 34, 2);

    uint32_t datenLen = 0;
    for (size_t q = 12; q + 8 <= s.comp; )
    {
        uint32_t cl = 0;
        std::memcpy(&cl, w + q + 4, 4);
        if (!std::memcmp(w + q, "data", 4)) { datenLen = cl; break; }
        q += 8 + cl + (cl & 1);
    }

    snprintf(aus, ausN, "%s/%s fmt=%u (%s) %uch %uHz %ubit data=%u",
             riff, wave, tag, tag == 1 ? "PCM" : tag == 2 ? "MS-ADPCM" : "unbekannt",
             kanaele, rate, bits, datenLen);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Aufruf: Tr5Ton <level.trc> [sfx ... | -samples]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string fehler;
    if (!lvl.load(zuW(argv[1]), fehler))
    {
        std::printf("Nicht ladbar: %s\n", fehler.c_str());
        return 1;
    }

    size_t lutAt = 0, infosAt = 0, numSamplesAt = 0;
    int32_t nInfos = 0, nSamples = 0;
    if (!lvl.debugKlangBloecke(lutAt, infosAt, nInfos, nSamples))
    {
        std::printf("Klangbloecke nicht gefunden\n");
        return 1;
    }
    lvl.debugSampleArray(numSamplesAt, nSamples);

    const std::vector<uint8_t>& b = lvl.debugBytes();

    // Physische Samplekette hinter dem Hauptblock ablaufen.
    std::vector<SampleLage> lagen;
    {
        size_t p = numSamplesAt + 4;
        for (int32_t i = 0; i < nSamples; ++i)
        {
            if (p + 8 > b.size()) { std::printf("Samplekette reisst bei %d ab\n", i); break; }
            SampleLage s;
            std::memcpy(&s.uncomp, b.data() + p, 4);
            std::memcpy(&s.comp, b.data() + p + 4, 4);
            s.datenAt = p + 8;
            if (s.datenAt + s.comp > b.size()) { std::printf("Sample %d laeuft aus der Datei\n", i); break; }
            lagen.push_back(s);
            p = s.datenAt + s.comp;
        }
    }

    std::printf("%s: %d Infos, %d Samples (Kette traegt %zu)\n",
                argv[1], nInfos, nSamples, lagen.size());

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

    static const char* MODUS[4] = { "stapelt", "nur wenn still", "neu starten", "Schleife" };

    if (argc >= 3 && !std::strcmp(argv[2], "-samples"))
    {
        std::printf("%7s %8s %8s %10s  %s\n", "Sample", "uncomp", "comp", "Hash", "Wave-Kopf");
        for (size_t i = 0; i < lagen.size(); ++i)
        {
            char kopf[160];
            waveKopf(b, lagen[i], kopf, sizeof(kopf));
            std::printf("%7zu %8u %8u  %08X  %s\n",
                        i, lagen[i].uncomp, lagen[i].comp,
                        fnv(b.data() + lagen[i].datenAt, lagen[i].comp), kopf);
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
            if (m < 0) { std::printf(" (nicht belegt)\n"); continue; }
            Info f = info(m);
            int varianten = (f.flags >> 2) & 0xF;
            std::printf("\n  number=%d vol=%u radius=%d chance=%u pitch=%d flags=0x%04X"
                        " -> %d Variante(n), Modus %d (%s)\n",
                        (int)f.number, f.volume, (int)f.radius, f.chance, (int)f.pitch,
                        f.flags, varianten, f.flags & 3, MODUS[f.flags & 3]);
            if (varianten < 1) varianten = 1;
            for (int v = 0; v < varianten; ++v)
            {
                int s = f.number + v;
                if (s < 0 || (size_t)s >= lagen.size())
                {
                    std::printf("  Wave %d: AUSSERHALB der Kette (0..%zu)!\n", s, lagen.size() ? lagen.size() - 1 : 0);
                    continue;
                }
                char kopf[160];
                waveKopf(b, lagen[(size_t)s], kopf, sizeof(kopf));
                std::printf("  Wave %d: uncomp=%u comp=%u Hash=%08X  %s\n",
                            s, lagen[(size_t)s].uncomp, lagen[(size_t)s].comp,
                            fnv(b.data() + lagen[(size_t)s].datenAt, lagen[(size_t)s].comp), kopf);
            }
        }
        return 0;
    }

    std::printf("%4s %4s %7s %5s %4s %4s %4s %6s  %s\n",
                "sfx", "map", "number", "Var", "vol", "rad", "rnd", "Modus", "");
    for (int sfx = 0; sfx < 450; ++sfx)
    {
        int16_t m = lut(sfx);
        if (m < 0) continue;
        Info f = info(m);
        // rad in Sektoren wie in der Datei; die Engine rechnet daraus
        // (radius + 1) << 10 Units als harte Hoergrenze (sound.cpp:203).
        std::printf("%4d %4d %7d %5d %4u %4d %4u %6d  %s\n",
                    sfx, (int)m, (int)f.number, (f.flags >> 2) & 0xF,
                    f.volume, (int)f.radius, f.chance, f.flags & 3,
                    MODUS[f.flags & 3]);
    }
    return 0;
}

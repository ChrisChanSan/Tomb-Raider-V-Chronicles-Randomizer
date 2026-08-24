// TriggerBefehle - zaehlt je Level, welche Trigger-BEFEHLE in der Floordata
// stehen (TO_OBJECT, TO_CAMERA, ..., TO_BODYBAG, ...), und listet fuer einen
// gewaehlten Befehlstyp jede Fundstelle mit Raum und Sektor.
//
// Anlass (#121, fuenfte Messung): TO_BODYBAG ruft ResetGuards() - macht ALLE
// aktiven Denker unsichtbar und teleportiert sie anhand von Feldern, die nur
// Vanilla-Wachen tragen. Traegt andy3 solche Befehle und Andrea1 nicht, ist
// das Blinken der importierten Soldaten erklaert.
//
// Aufruf: TriggerBefehle <level.trc> [...]           Zaehlung je Level
//         TriggerBefehle -zeige <typ> <level.trc>    Fundstellen eines Typs
//
// Befehlswort: Bits 0-9 Wert, Bits 10-13 Befehlstyp, Bit 15 Kettenende.
// Das erste Wort nach dem Setup-Wort von SWITCH/KEY/PICKUP ist ein
// Referenzwort, kein Befehl - wird uebersprungen wie in der Engine.
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::wstring weit(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

static const char* kNamen[16] = {
    "OBJECT", "CAMERA", "SINK", "FLIPMAP", "FLIPON", "FLIPOFF", "TARGET",
    "FINISH", "CD", "FLIPEFFECT", "SECRET", "BODYBAG", "FLYBY", "CUTSCENE",
    "?14", "?15"
};

int main(int argc, char** argv)
{
    int zeigeTyp = -1;
    int start = 1;
    if (argc >= 4 && std::strcmp(argv[1], "-zeige") == 0)
    {
        zeigeTyp = std::atoi(argv[2]);
        start = 3;
    }
    if (argc <= start)
    {
        std::printf("Aufruf: TriggerBefehle <level.trc> [...]\n"
                    "        TriggerBefehle -zeige <typ> <level.trc>\n");
        return 2;
    }

    for (int a = start; a < argc; ++a)
    {
        Tr5Level l;
        std::string e;
        if (!l.load(weit(argv[a]), e)) { std::printf("%s: nicht ladbar\n", argv[a]); continue; }

        const char* n = argv[a];
        for (const char* q = argv[a]; *q; ++q) if (*q == '\\' || *q == '/') n = q + 1;

        long zaehler[16] = {};

        for (const Tr5Room& r : l.rooms())
        {
            for (int sx = 0; sx < r.numXSectors; ++sx)
                for (int sz = 0; sz < r.numZSectors; ++sz)
                {
                    const int idx = l.sectorIndex(r.index, sx, sz);
                    if (idx <= 0) continue;
                    std::vector<Tr5FloorEntry> kette;
                    if (!l.readChain(idx, kette)) continue;

                    for (const Tr5FloorEntry& fe : kette)
                    {
                        if (fe.function != 4 /* FN_TRIGGER */) continue;
                        // words[0] = Funktionswort, [1] = Setup, dann Befehle.
                        size_t i = 2;
                        // SWITCH(2)/KEY(3)/PICKUP(4) schieben ein Referenzwort vor.
                        if (fe.art == 2 || fe.art == 3 || fe.art == 4) i = 3;
                        for (; i < fe.words.size(); ++i)
                        {
                            const int typ = (fe.words[i] >> 10) & 0xF;
                            ++zaehler[typ];
                            if (typ == zeigeTyp)
                                std::printf("  %s Raum %3d Sektor %d,%d  Wert %d  Triggerart %d\n",
                                            n, r.index, sx, sz, fe.words[i] & 0x3FF, fe.art);
                            // FLYBY verbraucht ein Zusatzwort.
                            if (typ == 12) ++i;
                        }
                    }
                }
        }

        std::printf("%-14s", n);
        for (int t = 0; t < 14; ++t)
            if (zaehler[t]) std::printf("  %s=%ld", kNamen[t], zaehler[t]);
        std::printf("\n");
    }
    return 0;
}

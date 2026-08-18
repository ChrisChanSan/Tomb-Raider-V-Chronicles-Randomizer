// ItemPruef - listet Items eines Levels mit OCB, Flags, Raum und Winkel.
//
// Gebraucht fuer Fragen, die MissMod nicht beantwortet: MissMod misst
// MODELLE, dieses Werkzeug die ITEMS. Anlaesse:
//  - Roman God (#84): traegt ein gesetztes Exemplar die OCB, die zu seinem
//    Modell gehoert? (trigger_flags 1 = Schwert/Fernkampf, 0 = Hammer)
//  - Larson/Antitrigger (#110): steht Bit 0x80 (IFL_ANTITRIGGER_ONESHOT)
//    wirklich an dem Item, das es tragen soll?
//
// Aufruf: ItemPruef <level.trc> [typeId ...]
// Ohne typeId werden alle Items gelistet.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring zuW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Aufruf: ItemPruef <level.trc> [typeId ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string fehler;
    if (!lvl.load(zuW(argv[1]), fehler))
    {
        std::printf("Nicht ladbar: %s\n", fehler.c_str());
        return 1;
    }

    std::vector<int> gesucht;
    for (int i = 2; i < argc; ++i) gesucht.push_back(std::atoi(argv[i]));

    std::printf("%s: %d Items\n", argv[1], lvl.itemCount());
    // Position und ihre Lage im Sektor dazu (#61): Sektormitte liegt bei
    // x.5 * 1024. "inSekt" zeigt den Versatz zur Mitte in Units - 0 heisst
    // genau mittig, +-512 waere die Sektorkante.
    std::printf("%5s %5s %5s %6s %8s %7s %6s  %9s %7s %9s  %11s\n",
                "Item", "Typ", "Raum", "OCB", "Flags", "Winkel", "Grad",
                "X", "Y", "Z", "dX/dZ z.Mitte");

    int gezeigt = 0;
    for (int i = 0; i < lvl.itemCount(); ++i)
    {
        Tr5Item it;
        if (!lvl.getItem(i, it)) continue;

        if (!gesucht.empty())
        {
            bool drin = false;
            for (int g : gesucht) if (it.objectNumber == g) drin = true;
            if (!drin) continue;
        }

        // Die OCB steckt in den unteren 6 Bit von trigger_flags; darueber
        // stehen andere Angaben, deshalb beides ausgeben.
        // Versatz zur Sektormitte: die Mitte liegt bei (n + 0.5) * 1024,
        // also ist der Rest von (koordinate mod 1024) minus 512 der Abstand.
        const int dx = ((it.x % 1024) + 1024) % 1024 - 512;
        const int dz = ((it.z % 1024) + 1024) % 1024 - 512;
        std::printf("%5d %5d %5d %6d   0x%04X %7d %6d  %9d %7d %9d  %5d /%5d\n",
                    i, (int)it.objectNumber, (int)it.roomNumber,
                    (int)(it.triggerFlags & 0x3F),
                    (unsigned)(uint16_t)it.flags, (int)it.yRot,
                    (int)((long long)it.yRot * 360 / 65536),
                    it.x, it.y, it.z, dx, dz);
        ++gezeigt;
    }
    std::printf("%d Items ausgegeben\n", gezeigt);
    return 0;
}

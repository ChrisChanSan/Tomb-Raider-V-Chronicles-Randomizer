//----------------------------------------------------------------------------
// TexRec - zeigt rohe TR5-Texinfos (PHDTEXTURESTRUCT, 40 Byte).
//
// Aufruf: TexRec <level.trc> [erster] [anzahl]
//
// Gebraucht fuer #90: um aus TR3-Objekttexturen gueltige TR5-Eintraege zu
// bauen, muss man wissen, welche Values drawtype und flag dort ueberhaupt
// annehmen - besonders, wie sich Vierecke von Dreiecken unterscheiden.
// Ablesen statt raten.
//
// Layout aus global/types.h und specific/file.cpp:1089ff:
//   +0 drawtype, +2 tpage (Bit 15 wandert beim Laden ins flag),
//   +4 flag, dann u1,v1,u2,v2,u3,v3,u4,v4 als 8.8-Festkomma.
//----------------------------------------------------------------------------
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <string>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Aufruf: TexRec <level.trc> [erster] [anzahl]\n"); return 2; }
    const int erster = (argc > 2) ? std::atoi(argv[2]) : 0;
    const int anzahl = (argc > 3) ? std::atoi(argv[3]) : 12;

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("Laden: %s\n", err.c_str()); return 2; }

    size_t at = 0; int32_t n = 0;
    if (!lvl.debugTexinfoBlock(at, n)) { std::printf("Kein Texinfo-Block\n"); return 1; }
    std::printf("%s: %d Texinfos\n\n", argv[1], n);
    // Verteilung ueber ALLE Eintraege: welche Values kommen ueberhaupt vor?
    {
        int mitBit15 = 0, ohneBit15 = 0, dreiecke = 0;
        std::vector<int> drawArten, flagArten;
        for (int i = 0; i < n; ++i)
        {
            const size_t o = at + static_cast<size_t>(i) * 40;
            uint16_t dw, tp, fl, u3, v3, u4, v4;
            std::memcpy(&dw, lvl.debugBytes().data() + o, 2);
            std::memcpy(&tp, lvl.debugBytes().data() + o + 2, 2);
            std::memcpy(&fl, lvl.debugBytes().data() + o + 4, 2);
            std::memcpy(&u3, lvl.debugBytes().data() + o + 14, 2);
            std::memcpy(&v3, lvl.debugBytes().data() + o + 16, 2);
            std::memcpy(&u4, lvl.debugBytes().data() + o + 18, 2);
            std::memcpy(&v4, lvl.debugBytes().data() + o + 20, 2);
            (tp & 0x8000) ? ++mitBit15 : ++ohneBit15;
            if (u4 == u3 && v4 == v3) ++dreiecke;
            bool da = false; for (int x : drawArten) if (x == dw) { da = true; break; }
            if (!da) drawArten.push_back(dw);
            da = false; for (int x : flagArten) if (x == fl) { da = true; break; }
            if (!da) flagArten.push_back(fl);
        }
        std::printf("  tpage mit Bit15: %d, ohne: %d\n", mitBit15, ohneBit15);
        std::printf("  vermutliche Dreiecke (u4/v4 == u3/v3): %d\n", dreiecke);
        std::printf("  drawtype-Values:");
        for (int x : drawArten) std::printf(" %d", x);
        std::printf("\n  flag-Values:");
        for (int x : flagArten) std::printf(" %04X", x);
        std::printf("\n\n");
    }

    std::printf("  Nr   draw  tpage  flag    u1/v1      u2/v2      u3/v3      u4/v4\n");

    const std::vector<uint8_t>& b = lvl.debugBytes();
    for (int i = erster; i < erster + anzahl && i < n; ++i)
    {
        const size_t o = at + static_cast<size_t>(i) * 40;
        uint16_t w[11];
        for (int k = 0; k < 11; ++k) std::memcpy(&w[k], b.data() + o + k * 2, 2);

        std::printf("  %4d  %4u  %5u  %04X  %3u/%-3u    %3u/%-3u    %3u/%-3u    %3u/%-3u\n",
                    i, w[0], w[1] & 0x7FFF, w[2],
                    w[3] >> 8, w[4] >> 8, w[5] >> 8, w[6] >> 8,
                    w[7] >> 8, w[8] >> 8, w[9] >> 8, w[10] >> 8);
    }
    return 0;
}

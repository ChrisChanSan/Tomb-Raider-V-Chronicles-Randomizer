// SektorPruef - zeigt einen Sektor im Klartext: die rohen 8 FLOOR_INFO-Bytes
// und die dekodierte Floordata-Kette.
//
// Gebaut fuer #53/Make_Portal: bevor Portale GESCHRIEBEN werden, muessen die
// funktionierenden Nachbarsektoren als Vorlage LESBAR sein - und das Ergebnis
// hinterher gegen genau diese Anzeige geprueft werden. Kein anderes Werkzeug
// zeigt die Sektorebene.
//
// FLOOR_INFO (global/types.h:633), 8 Byte je Sektor:
//   +0 u16  index      Zeiger in die Floordata (0 = keine Kette)
//   +2 u16  fx:4 | box:11 | stopper:1
//   +4 u8   pit_room   Raum unter dem Boden (255 = keiner)
//   +5 s8   floor      Bodenhoehe in 256er-Schritten
//   +6 u8   sky_room   Raum ueber der Decke (255 = keiner)
//   +7 s8   ceiling    Deckenhoehe in 256er-Schritten
//
// Aufruf: SektorPruef <level.trc> <raum> [sx sz]
//         ohne sx/sz alle Sektoren des Raums.
#include "core/Tr5Level.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::wstring zuW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

static const char* fnName(int fn)
{
    switch (fn)
    {
    case 0:  return "FLOOR";
    case 1:  return "DOOR";          // Portal: naechstes Wort ist der Zielraum
    case 2:  return "TILT";
    case 3:  return "ROOF";
    case 4:  return "TRIGGER";
    case 5:  return "LAVA";
    case 6:  return "CLIMB";
    case 7:  return "SPLIT1";
    case 8:  return "SPLIT2";
    case 9:  return "SPLIT3";
    case 10: return "SPLIT4";
    case 11: return "NOCOLF1T";
    case 12: return "NOCOLF1B";
    case 13: return "NOCOLF2T";
    case 14: return "NOCOLF2B";
    case 15: return "NOCOLC1T";
    case 16: return "NOCOLC1B";
    case 17: return "NOCOLC2T";
    case 18: return "NOCOLC2B";
    case 19: return "MONKEY";
    case 20: return "TRIGTRIGGER";
    default: return "?";
    }
}

static void zeigeSektor(const Tr5Level& lvl, const Tr5Room& r, int sx, int sz)
{
    const std::vector<uint8_t>& b = lvl.debugBytes();
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    if (at + 8 > b.size()) { std::printf("  (%d,%d): ausserhalb der Datei\n", sx, sz); return; }

    uint16_t index, w2;
    std::memcpy(&index, &b[at], 2);
    std::memcpy(&w2, &b[at + 2], 2);
    const unsigned pit = b[at + 4];
    const int      flr = static_cast<int8_t>(b[at + 5]);
    const unsigned sky = b[at + 6];
    const int      cei = static_cast<int8_t>(b[at + 7]);

    const int wx = r.originX + sx * 1024;
    const int wz = r.originZ + sz * 1024;

    std::printf("Sektor (%d,%d)  Welt X %d..%d, Z %d..%d\n", sx, sz, wx, wx + 1024, wz, wz + 1024);
    std::printf("  roh: %02X %02X  %02X %02X  %02X %02X %02X %02X\n",
                b[at], b[at+1], b[at+2], b[at+3], b[at+4], b[at+5], b[at+6], b[at+7]);
    std::printf("  index=%u  fx=%u box=%u stopper=%u\n", index, w2 & 0xF, (w2 >> 4) & 0x7FF, w2 >> 15);
    std::printf("  pit_room=%s  floor=%d (y %d)  sky_room=%s  ceiling=%d (y %d)%s\n",
                pit == 255 ? "-" : std::to_string(pit).c_str(), flr, flr * 256,
                sky == 255 ? "-" : std::to_string(sky).c_str(), cei, cei * 256,
                (flr == -127 || flr == 127) ? "   [WAND]" : "");

    if (!index) { std::printf("  Floordata: keine\n\n"); return; }

    std::vector<Tr5FloorEntry> kette;
    if (!lvl.readChain(index, kette)) { std::printf("  Floordata: Kette nicht lesbar!\n\n"); return; }

    for (const Tr5FloorEntry& e : kette)
    {
        std::printf("  Floordata: %-12s", fnName(e.funktion));
        if (e.funktion == 1 && e.woerter.size() >= 2)
            std::printf(" -> Raum %u", e.woerter[1]);
        else if ((e.funktion == 2 || e.funktion == 3) && e.woerter.size() >= 2)
            std::printf(" x=%d z=%d", static_cast<int8_t>(e.woerter[1] & 0xFF),
                        static_cast<int8_t>(e.woerter[1] >> 8));
        else if (e.funktion == 4)
            std::printf(" art=%d", e.art);
        std::printf("   [");
        for (size_t i = 0; i < e.woerter.size(); ++i)
            std::printf("%s%04X", i ? " " : "", e.woerter[i]);
        std::printf("]\n");
    }
    std::printf("\n");
}

// Die 32-Byte-Portaleintraege des Raums: Zielraum, Normale, Fensterpolygon.
// Liegen direkt hinter dem Sektorfeld (u16 Anzahl, dann je 32 Byte); die
// Vertices sind RAUMLOKAL. Vorlage fuer Make_Portal - und Gegenprobe danach.
static void zeigePortale(const Tr5Level& lvl, const Tr5Room& r)
{
    const std::vector<uint8_t>& b = lvl.debugBytes();
    const size_t listeAt = r.sectorsOffset +
        static_cast<size_t>(r.numXSectors) * r.numZSectors * 8;
    uint16_t anzahl;
    std::memcpy(&anzahl, &b[listeAt], 2);
    std::printf("%u Portaleintraege:\n", anzahl);

    for (unsigned i = 0; i < anzahl; ++i)
    {
        const size_t at = listeAt + 2 + static_cast<size_t>(i) * 32;
        auto s16 = [&](size_t o) { int16_t v; std::memcpy(&v, &b[at + o], 2); return static_cast<int>(v); };
        std::printf("  [%2u] -> Raum %-3d  Normale (%d, %d, %d)\n",
                    i, s16(0), s16(2), s16(4), s16(6));
        for (int v = 0; v < 4; ++v)
            std::printf("        V%d lokal (%6d, %6d, %6d)  Welt (%d, %d, %d)\n", v,
                        s16(8 + v * 6), s16(10 + v * 6), s16(12 + v * 6),
                        r.originX + s16(8 + v * 6), s16(10 + v * 6), r.originZ + s16(12 + v * 6));
    }
    std::printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("Aufruf: SektorPruef <level.trc> <raum> [sx sz]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string fehler;
    if (!lvl.load(zuW(argv[1]), fehler)) { std::printf("Laden: %s\n", fehler.c_str()); return 1; }

    const int raum = std::atoi(argv[2]);
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (raum < 0 || raum >= static_cast<int>(rooms.size())) { std::printf("Raum %d gibt es nicht\n", raum); return 1; }
    const Tr5Room& r = rooms[static_cast<size_t>(raum)];
    if (!r.sectorsOffset) { std::printf("Raum %d: Sektorfeld nicht aufgegangen\n", raum); return 1; }

    std::printf("%s Raum %d: origin (%d, %d), %dx%d Sektoren, Portalliste: ",
                argv[1], raum, r.originX, r.originZ, r.numXSectors, r.numZSectors);
    for (size_t i = 0; i < r.portalRooms.size(); ++i)
        std::printf("%s%d", i ? " " : "", r.portalRooms[i]);
    if (r.portalRooms.empty()) std::printf("(leer)");
    std::printf("\n\n");

    if (argc >= 4 && std::strcmp(argv[3], "-portale") == 0)
        zeigePortale(lvl, r);
    else if (argc >= 5)
        zeigeSektor(lvl, r, std::atoi(argv[3]), std::atoi(argv[4]));
    else
        for (int sx = 0; sx < r.numXSectors; ++sx)
            for (int sz = 0; sz < r.numZSectors; ++sz)
                zeigeSektor(lvl, r, sx, sz);
    return 0;
}

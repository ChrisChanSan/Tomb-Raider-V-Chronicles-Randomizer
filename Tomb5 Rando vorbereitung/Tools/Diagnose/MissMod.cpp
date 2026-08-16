// MissMod - misst, wo ein TR5-Modell relativ zum Itemursprung sitzt.
//
// Hintergrund: der Crane Guy (Slot 55) steht im Spiel 512 Units neben der
// Sektormitte, obwohl das Item exakt mittig liegt. Core hat ihn in joby2 nur
// als ANIMATING4 an der Krankonsole verbaut - der Versatz steckt also im
// Modell selbst, in den Frame-Offsets und der Frame-Box.
//
// Aufruf: MissMod <level.trc> [slot ...]
//
// Ausgabe je Slot: die Box des ersten Frames und ihr Mittelpunkt. Ein sauber
// zentriertes Modell hat einen Mittelpunkt nahe 0/0 in x und z.

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
        std::printf("Aufruf: MissMod <level.trc> [slot ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string fehler;
    std::string pfad(argv[1]);
    std::wstring wpfad(pfad.begin(), pfad.end());
    if (!lvl.load(wpfad, fehler))
    {
        std::printf("Laden fehlgeschlagen: %s\n", fehler.c_str());
        return 2;
    }

    size_t objAt = 0, frAt = 0;
    int32_t objN = 0, frN = 0;
    if (!lvl.debugModellBloecke(objAt, objN, frAt, frN))
    {
        std::printf("Keine Objekttabelle gefunden.\n");
        return 3;
    }

    const std::vector<uint8_t>& b = lvl.debugBytes();
    std::printf("%s: %d Moveables, Frame-Scheibe %d Woerter\n\n",
                argv[1], objN, frN);

    std::vector<int> gesucht;
    for (int i = 2; i < argc; ++i) gesucht.push_back(std::atoi(argv[i]));

    std::printf("Slot  Meshes  minX  maxX  minY  maxY  minZ  maxZ |  MitteX  MitteZ | OffX OffY OffZ\n");

    for (int i = 0; i < objN; ++i)
    {
        // TR5-Moveable ist 20 Byte: die 18 aus TR4 plus ein Fuellwort.
        size_t at   = objAt + (size_t)i * 20;
        uint32_t id = u32(b, at);
        if (!gesucht.empty())
        {
            bool drin = false;
            for (int g : gesucht) if ((int)id == g) drin = true;
            if (!drin) continue;
        }

        uint16_t meshes = u16(b, at + 4);
        uint32_t frOff  = u32(b, at + 12);      // Byteversatz in die Frame-Scheibe

        size_t f = frAt + frOff;
        if (f + 18 > b.size()) { std::printf("%4u  Frame-Zeiger ausserhalb\n", id); continue; }

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

//----------------------------------------------------------------------------
// TR3Setz - setzt ein mit TR3Hol geholtes Moveable in ein TR5-Level.
//
// Aufruf: TR3Setz <ziel.trc> <paket.bin> <zielSlot> <ausgabe.trc> [texinfo]
//
// texinfo (Vorgabe 0): auf diesen vorhandenen Texinfo zeigen danach ALLE
// Faces. Es wird keine Texturseite angefasst - siehe Tr5Tr3Import.cpp.
//----------------------------------------------------------------------------
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 5 || argc > 6)
    {
        std::printf("Aufruf: TR3Setz <ziel.trc> <paket.bin> <zielSlot> <ausgabe.trc> [texinfo]\n");
        return 2;
    }
    const int slot = std::atoi(argv[3]);
    const int tex  = (argc == 6) ? std::atoi(argv[5]) : 0;

    std::vector<uint8_t> paket;
    {
        FILE* f = std::fopen(argv[2], "rb");
        if (!f) { std::printf("Paket nicht lesbar: %s\n", argv[2]); return 2; }
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        paket.resize(static_cast<size_t>(n));
        const size_t g = std::fread(paket.data(), 1, paket.size(), f);
        std::fclose(f);
        if (g != paket.size()) { std::printf("Paket unvollstaendig gelesen\n"); return 2; }
    }

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("Level laden: %s\n", err.c_str()); return 2; }

    Tr5Level::MoveableInfo vorher;
    const bool hatte = lvl.moveableInfo(slot, vorher);
    std::printf("Ziel %s: %d Items, Slot %d %s\n", argv[1], lvl.itemCount(), slot,
                hatte ? "vorhanden (wird ersetzt)" : "frei (wird angehaengt)");

    if (!lvl.addMoveableFromTr3(paket, slot, tex, err))
    { std::printf("EINBAU GESCHEITERT: %s\n", err.c_str()); return 1; }

    Tr5Level::MoveableInfo mi;
    if (!lvl.moveableInfo(slot, mi)) { std::printf("Slot %d nach dem Einbau nicht auffindbar\n", slot); return 1; }
    std::printf("  Slot %d: %d Meshes, meshIdx %d, bone %d, frameOffset %u, anim %d\n",
                slot, mi.nmeshes, mi.meshIndex, mi.boneIndex,
                static_cast<unsigned>(mi.frameOffset), mi.animIndex);

    // Gegenprobe: laufen die eingebauten Meshes durch den Ableser der Engine?
    int schlecht = 0;
    for (int i = 0; i < mi.nmeshes; ++i)
    {
        int nv, nn, q, t, bytes;
        if (!lvl.debugMeshWalk(mi.meshIndex + i, nv, nn, q, t, bytes)) ++schlecht;
    }
    std::printf("  Meshpruefung: %d von %d nicht lesbar\n", schlecht, mi.nmeshes);

    if (!lvl.save(toW(argv[4]), err)) { std::printf("Speichern: %s\n", err.c_str()); return 1; }
    std::printf("  Geschrieben: %s\n", argv[4]);
    return schlecht ? 1 : 0;
}

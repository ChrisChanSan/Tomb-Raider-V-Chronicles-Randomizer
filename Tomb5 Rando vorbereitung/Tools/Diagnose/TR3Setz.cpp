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

// Schreibt die SFX-Nummern der Anim-Kommandos im Modellpaket um.
//
// Das Kommandowort 5 (PlaySound) fuehrt zwei Folgeworte: Frame und dann die
// SFX-Nummer, deren obere Bits (12-15) Ausfuehrungsbedingungen tragen (nur
// an Land, nur im Wasser) und stehen bleiben MUESSEN.
//
// Ohne diesen Schritt ruft der Gegner weiter seine TR3-Nummern auf, die im
// Ziel andere oder gar keine Klaenge sind. Dasselbe Vergessen bei der
// Sprungtabelle war der Absturz in #88 - deshalb hier ausdruecklich.
static int biegeSfxUm(std::vector<uint8_t>& paket, const std::vector<std::pair<int,int>>& zuordnung)
{
    auto g32 = [&](size_t p) { uint32_t v; std::memcpy(&v, &paket[p], 4); return v; };
    auto g16 = [&](size_t p) { uint16_t v; std::memcpy(&v, &paket[p], 2); return v; };

    // Kopf des Modellpakets ueberspringen, um an den Kommandoblock zu kommen.
    const uint32_t nMeshes = g32(12), nAnims = g32(16), nFrameW = g32(20);
    const uint32_t nBoneW = g32(24), nSC = g32(28), nAD = g32(32), nCmd = g32(36);
    size_t p = 48;
    for (uint32_t i = 0; i < nMeshes; ++i) { const uint32_t l = g32(p); p += 4 + l; }
    p += size_t(nBoneW) * 4 + size_t(nFrameW) * 2 + size_t(nAnims) * 40
       + size_t(nSC) * 6 + size_t(nAD) * 8;
    const size_t cmdAt = p;
    if (cmdAt + size_t(nCmd) * 2 > paket.size()) return -1;

    int getauscht = 0;
    for (uint32_t i = 0; i < nCmd; )
    {
        const uint16_t art = g16(cmdAt + size_t(i) * 2);
        ++i;
        int folge = 0;
        switch (art)
        {
        case 1: folge = 3; break;    // SetPosition
        case 2: folge = 2; break;    // JumpVelocity
        case 5: folge = 2; break;    // PlaySound: Frame + SFX
        case 6: folge = 2; break;    // Flipeffect
        default: folge = 0; break;   // EmptyHands, KillItem, Unbekanntes
        }

        if (art == 5 && i + 1 < nCmd)
        {
            const size_t at = cmdAt + size_t(i + 1) * 2;
            const uint16_t w = g16(at);
            const int nr = w & 0x0FFF;
            for (const auto& z : zuordnung)
                if (z.first == nr)
                {
                    const uint16_t neu = static_cast<uint16_t>((w & 0xF000) | (z.second & 0x0FFF));
                    std::memcpy(&paket[at], &neu, 2);
                    ++getauscht;
                    break;
                }
        }
        i += folge;
    }
    return getauscht;
}

int main(int argc, char** argv)
{
    if (argc < 5 || argc > 7)
    {
        std::printf("Aufruf: TR3Setz <ziel.trc> <paket.bin> <zielSlot> <ausgabe.trc> [texinfo] [ton.bin]\n");
        return 2;
    }
    const int slot = std::atoi(argv[3]);
    const int tex  = (argc >= 6) ? std::atoi(argv[5]) : 0;
    const char* tonDatei = (argc == 7) ? argv[6] : 0;

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

    // Sounds ZUERST: erst danach steht fest, auf welche Plaetze die
    // SFX-Nummern der Anim-Kommandos zeigen muessen - und die Kommandos
    // stecken im Modellpaket, das gleich eingebaut wird.
    if (tonDatei)
    {
        std::vector<uint8_t> ton;
        FILE* f = std::fopen(tonDatei, "rb");
        if (!f) { std::printf("Tonpaket nicht lesbar: %s\n", tonDatei); return 2; }
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        ton.resize(static_cast<size_t>(n));
        const size_t g = std::fread(ton.data(), 1, ton.size(), f);
        std::fclose(f);
        if (g != ton.size()) { std::printf("Tonpaket unvollstaendig\n"); return 2; }

        const int vorherSamples = lvl.soundSampleCount();
        std::vector<std::pair<int,int>> zuordnung;

        // 446..449: in allen 16 Leveln frei. Die Soundmap ist ein festes Feld
        // von 450 Plaetzen, ein hoher Platz kostet also kein Byte.
        if (!lvl.addSoundsFromTr3(ton, 446, zuordnung, err))
        { std::printf("SOUNDS GESCHEITERT: %s\n", err.c_str()); return 1; }

        std::printf("  Sounds: %d -> %d Samples\n", vorherSamples, lvl.soundSampleCount());
        for (const auto& z : zuordnung)
            std::printf("    TR3-Sound %3d  ->  Map %3d\n", z.first, z.second);

        const int n2 = biegeSfxUm(paket, zuordnung);
        if (n2 < 0) { std::printf("Kommandoblock im Modellpaket nicht auffindbar\n"); return 1; }
        std::printf("  Anim-Kommandos: %d SFX-Nummern umgebogen\n", n2);
    }

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

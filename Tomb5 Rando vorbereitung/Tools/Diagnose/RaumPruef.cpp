// RaumPruef - zeigt Flaggen und Flipmap-Partner von Raeumen einer .trc.
//
// Anlass (#111-Nachlese, Wasserschritt-Phantom): SFXLOG nennt obj=51 in
// Raum 57/59 von Andrea1 mit einem "nur Wasser"-Schritt, der Nutzer ist
// sicher, dass dort kein Wasser erreichbar ist. Die Frage an die Datei:
// tragen diese Raeume (oder ihre Flip-Varianten) das Wasserbit 0x1?
//
// Aufruf: RaumPruef <level.trc> [raum ...]    ohne Raeume: alle mit Wasserbit
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

static void zeile(const Tr5Level& lvl, int r)
{
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (r < 0 || r >= (int)rooms.size()) { std::printf("Raum %d: ausserhalb (0..%zu)\n", r, rooms.size() - 1); return; }
    const Tr5Room& rm = rooms[(size_t)r];
    std::printf("Raum %3d: flags=0x%04X%s  altRoom=%d  %dx%d Sektoren\n",
                r, rm.flags, (rm.flags & 1) ? " WASSER" : "", rm.altRoom,
                rm.numXSectors, rm.numZSectors);
    if (rm.altRoom >= 0 && rm.altRoom < (int)rooms.size())
    {
        const Tr5Room& alt = rooms[(size_t)rm.altRoom];
        std::printf("   Flip-Partner %3d: flags=0x%04X%s\n",
                    rm.altRoom, alt.flags, (alt.flags & 1) ? " WASSER" : "");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Aufruf: RaumPruef <level.trc> [raum ...]\n"); return 1; }

    Tr5Level lvl;
    std::string fehler;
    if (!lvl.load(zuW(argv[1]), fehler)) { std::printf("Nicht ladbar: %s\n", fehler.c_str()); return 1; }

    if (argc > 2)
    {
        for (int i = 2; i < argc; ++i) zeile(lvl, std::atoi(argv[i]));
        return 0;
    }

    std::printf("%s: %d Raeume, alle mit Wasserbit:\n", argv[1], lvl.roomCount());
    for (int r = 0; r < lvl.roomCount(); ++r)
        if (lvl.rooms()[(size_t)r].flags & 1) zeile(lvl, r);
    return 0;
}

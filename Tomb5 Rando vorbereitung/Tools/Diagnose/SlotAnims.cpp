// SlotAnims - misst die Anim-Scheibe eines Objektslots.
//
// Warum es das braucht: die Steuerung eines Gegners greift oft mit
// anim_index + N quer durch die GLOBALE Anim-Tabelle, waehrend die
// Objekttabelle ihm nur bis zum naechsten Objekt "gehoert". Ist N groesser
// als die Scheibe, schneidet die Heuristik des Modelltransfers zu frueh ab -
// im Ziel liest die Engine dann hinter dem Tabellenende (beim Imp als
// Todes-Flugschleife im Spiel gesehen, bei der Hydra als Abbruch
// "Frame-Zeiger ausserhalb der Frame-Scheibe").
//
// Aufruf: SlotAnims <slot> <level.trc> [level.trc ...]
//
// Ausgabe je Level: anim_index des Slots, der naechsthoehere anim_index im
// Level und die Differenz = die Scheibe, die die Heuristik nehmen wuerde.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include "core/Tr5Level.h"

static std::wstring zuW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("Aufruf: SlotAnims <slot> <level.trc> [level.trc ...]\n");
        return 1;
    }

    const int slot = std::atoi(argv[1]);
    std::printf("Slot %d\n", slot);
    std::printf("%-14s %8s %8s %9s %8s  %s\n",
                "Level", "Meshes", "anim@", "naechster", "Scheibe", "Bemerkung");

    for (int i = 2; i < argc; ++i)
    {
        std::string path(argv[i]);
        const size_t schnitt = path.find_last_of("\\/");
        std::string name = (schnitt == std::string::npos) ? path : path.substr(schnitt + 1);

        Tr5Level lvl;
        std::string error;
        if (!lvl.load(zuW(argv[i]), error))
        {
            std::printf("%-14s  nicht ladbar: %s\n", name.c_str(), error.c_str());
            continue;
        }

        Tr5Level::MoveableInfo mv;
        if (!lvl.moveableInfo(slot, mv))
        {
            std::printf("%-14s  -- kein Modell --\n", name.c_str());
            continue;
        }

        // Der naechsthoehere anim_index ueber alle Slots: genau die Grenze,
        // die die Transfer-Heuristik zieht.
        int naechster = -1;
        for (int s : lvl.moveableSlots())
        {
            Tr5Level::MoveableInfo o;
            if (!lvl.moveableInfo(s, o)) continue;
            if (o.animIndex <= mv.animIndex) continue;
            if (naechster < 0 || o.animIndex < naechster) naechster = o.animIndex;
        }

        if (mv.animIndex < 0)
        {
            std::printf("%-14s %8d %8s %9s %8s  Slot ohne eigene Animationen\n",
                        name.c_str(), mv.nmeshes, "-", "-", "-");
            continue;
        }

        if (naechster < 0)
        {
            std::printf("%-14s %8d %8d %9s %8s  letzter Slot - Scheibe reicht bis Tabellenende\n",
                        name.c_str(), mv.nmeshes, mv.animIndex, "(keiner)", "?");
            continue;
        }

        std::printf("%-14s %8d %8d %9d %8d\n",
                    name.c_str(), mv.nmeshes, mv.animIndex, naechster,
                    naechster - mv.animIndex);
    }
    return 0;
}

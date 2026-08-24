//----------------------------------------------------------------------------
// PruefBoden - findet Pickups, die falsch zum Boden liegen.
//
// Regel (siehe core/Floor.h, woertlich aus der Engine portiert): ein Pickup
// ist in Ordnung, wenn sein Punkt belaufbar ist (keine Rutsche mit Neigung
// ueber 2, kein kollisionsfreies Dreieck) UND sein y hoechstens 128 Einheiten
// von der echten Bodenhoehe an seiner Position abweicht (Toleranz fuer
// Typversaetze wie das HK-Sichtbarkeits-y von -100).
//
// Aufruf 1: PruefBoden <level.trc>
//   Prueft ALLE Pickup-Items - auf Vanilla-Leveln ist das die Eichung: Cores
//   handgesetzte Schraegen-Pickups muessen die Regel bestehen, sonst stimmt
//   die portierte Mathematik nicht. (Ausnahme: Items auf Sockeln/Bruecken
//   liegen bewusst hoch - solche Treffer von Hand gegenlesen.)
//
// Aufruf 2: PruefBoden <source.trc> <randomisiert.trc>
//   Flaggt nur Items, die der Randomizer BEWEGT oder HINZUGEFUEGT hat.
//   Items werden nie entfernt, Indizes sind stabil: gleicher Index + gleiche
//   Position = unangetastet.
//
// Rueckgabe 0 = keine Verstoesse, 1 = Verstoesse, 2 = Bedienfehler.
//----------------------------------------------------------------------------
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static bool istPickup(int t)
{
    return (t >= 172 && t <= 241) || (t >= 334 && t <= 355);
}

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(s[0])));
    return w;
}

// Prueft ein Item; liefert true bei Verstoss und beschreibt ihn.
static bool verstoss(const Tr5Level& lvl, int index, const Tr5Item& it, std::string& warum)
{
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (it.roomNumber < 0 || it.roomNumber >= static_cast<int>(rooms.size())) return false;
    const Tr5Room& r = rooms[static_cast<size_t>(it.roomNumber)];
    const int sx = (it.x - r.originX) / 1024;
    const int sz = (it.z - r.originZ) / 1024;
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return false;

    SlopeInfo si;
    int baseY = 0;
    const int art = lvl.floorSlope(it.roomNumber, sx, sz, si, baseY);
    if (art < 0) { warum = "Floordata unlesbar"; return true; }

    FloorPos lage;
    lage.y = baseY;
    if (art == 1)
        lage = floorPosAt(si, baseY, it.x & 1023, it.z & 1023);

    char buf[128];
    if (!lage.walkable)
    {
        std::snprintf(buf, sizeof(buf), "unbegehbar (Rutsche/NoCol), Boden %d", lage.y);
        warum = buf;
        return true;
    }
    const int diff = it.y - lage.y;
    if (diff > 128 || diff < -128)
    {
        std::snprintf(buf, sizeof(buf), "y %d, Boden %d, Abstand %d", it.y, lage.y, diff);
        warum = buf;
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::printf("Aufruf: PruefBoden <level.trc> [randomisiert.trc]\n");
        return 2;
    }

    Tr5Level a;
    std::string err;
    if (!a.load(toW(argv[1]), err)) { std::printf("Laden %s: %s\n", argv[1], err.c_str()); return 2; }

    int verstoesse = 0;
    std::string warum;

    if (argc == 2)
    {
        for (int i = 0; i < a.itemCount(); ++i)
        {
            Tr5Item it;
            if (!a.getItem(i, it) || !istPickup(it.objectNumber)) continue;
            if (!verstoss(a, i, it, warum)) continue;
            ++verstoesse;
            std::printf("  Item %3d: Typ %3d Raum %3d  %s\n",
                        i, it.objectNumber, it.roomNumber, warum.c_str());
        }
        std::printf("%d auffaellige Pickups (inkl. Vanilla - Sockel von Hand gegenlesen).\n", verstoesse);
        return verstoesse ? 1 : 0;
    }

    Tr5Level b;
    if (!b.load(toW(argv[2]), err)) { std::printf("Laden %s: %s\n", argv[2], err.c_str()); return 2; }

    for (int i = 0; i < b.itemCount(); ++i)
    {
        Tr5Item it;
        if (!b.getItem(i, it) || !istPickup(it.objectNumber)) continue;

        // Unangetastet? Dann ist es Cores Platzierung (oder eine fruehere
        // bewusste), und die zaehlt nicht als Verstoss dieses Laufs.
        if (i < a.itemCount())
        {
            Tr5Item alt;
            if (a.getItem(i, alt) &&
                alt.objectNumber == it.objectNumber &&
                alt.roomNumber == it.roomNumber &&
                alt.x == it.x && alt.y == it.y && alt.z == it.z)
                continue;
        }

        if (!verstoss(b, i, it, warum)) continue;
        ++verstoesse;
        std::printf("  VERSTOSS Item %3d: Typ %3d Raum %3d  %s\n",
                    i, it.objectNumber, it.roomNumber, warum.c_str());
    }
    std::printf("%d bewegte/neue Pickups falsch zum Boden.\n", verstoesse);
    return verstoesse ? 1 : 0;
}

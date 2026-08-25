//----------------------------------------------------------------------------
// PruefBoden - finds pickups that lie wrong relative to the floor.
//
// Rule (see core/Floor.h, ported literally from the engine): a pickup is
// fine when its point is walkable (no slide with a tilt above 2, no
// collision-free triangle) AND its y deviates at most 128 units from the
// real floor height at its position (tolerance for type offsets like the
// HK visibility y of -100).
//
// Call 1: PruefBoden <level.trc>
//   Checks ALL pickup items - on vanilla levels this is the calibration:
//   Core's hand-placed slope pickups must pass the rule, or the ported
//   math is wrong. (Exception: items on plinths/bridges deliberately lie
//   high - cross-read such hits by hand.)
//
// Call 2: PruefBoden <source.trc> <randomized.trc>
//   Flags only items the randomizer MOVED or ADDED. Items are never
//   removed, indices are stable: same index + same position = untouched.
//
// Return 0 = no violations, 1 = violations, 2 = usage error.
//----------------------------------------------------------------------------
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static bool isPickup(int t)
{
    return (t >= 172 && t <= 241) || (t >= 334 && t <= 355);
}

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(s[0])));
    return w;
}

// Checks one item; returns true on a violation and describes it.
static bool violation(const Tr5Level& lvl, int index, const Tr5Item& it, std::string& why)
{
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (it.roomNumber < 0 || it.roomNumber >= static_cast<int>(rooms.size())) return false;
    const Tr5Room& r = rooms[static_cast<size_t>(it.roomNumber)];
    const int sx = (it.x - r.originX) / 1024;
    const int sz = (it.z - r.originZ) / 1024;
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return false;

    SlopeInfo si;
    int baseY = 0;
    const int kind = lvl.floorSlope(it.roomNumber, sx, sz, si, baseY);
    if (kind < 0) { why = "floordata unreadable"; return true; }

    FloorPos pos;
    pos.y = baseY;
    if (kind == 1)
        pos = floorPosAt(si, baseY, it.x & 1023, it.z & 1023);

    char buf[128];
    if (!pos.walkable)
    {
        std::snprintf(buf, sizeof(buf), "not walkable (slide/NoCol), floor %d", pos.y);
        why = buf;
        return true;
    }
    const int diff = it.y - pos.y;
    if (diff > 128 || diff < -128)
    {
        std::snprintf(buf, sizeof(buf), "y %d, floor %d, distance %d", it.y, pos.y, diff);
        why = buf;
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::printf("usage: PruefBoden <level.trc> [randomized.trc]\n");
        return 2;
    }

    Tr5Level a;
    std::string err;
    if (!a.load(toW(argv[1]), err)) { std::printf("load %s: %s\n", argv[1], err.c_str()); return 2; }

    int violations = 0;
    std::string why;

    if (argc == 2)
    {
        for (int i = 0; i < a.itemCount(); ++i)
        {
            Tr5Item it;
            if (!a.getItem(i, it) || !isPickup(it.objectNumber)) continue;
            if (!violation(a, i, it, why)) continue;
            ++violations;
            std::printf("  item %3d: type %3d room %3d  %s\n",
                        i, it.objectNumber, it.roomNumber, why.c_str());
        }
        std::printf("%d conspicuous pickups (incl. vanilla - cross-read plinths by hand).\n", violations);
        return violations ? 1 : 0;
    }

    Tr5Level b;
    if (!b.load(toW(argv[2]), err)) { std::printf("load %s: %s\n", argv[2], err.c_str()); return 2; }

    for (int i = 0; i < b.itemCount(); ++i)
    {
        Tr5Item it;
        if (!b.getItem(i, it) || !isPickup(it.objectNumber)) continue;

        // Untouched? Then it is Core's placement (or an earlier deliberate
        // one), and that does not count as this run's violation.
        if (i < a.itemCount())
        {
            Tr5Item old;
            if (a.getItem(i, old) &&
                old.objectNumber == it.objectNumber &&
                old.roomNumber == it.roomNumber &&
                old.x == it.x && old.y == it.y && old.z == it.z)
                continue;
        }

        if (!violation(b, i, it, why)) continue;
        ++violations;
        std::printf("  VIOLATION item %3d: type %3d room %3d  %s\n",
                    i, it.objectNumber, it.roomNumber, why.c_str());
    }
    std::printf("%d moved/new pickups wrong relative to the floor.\n", violations);
    return violations ? 1 : 0;
}

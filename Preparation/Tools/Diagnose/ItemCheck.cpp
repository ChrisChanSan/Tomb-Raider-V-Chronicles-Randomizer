// ItemCheck - lists a level's items with OCB, flags, room and angle.
//
// Needed for questions ModelOffset does not answer: ModelOffset measures
// models, this tool the items. Occasions:
//  - Roman god: does a placed instance carry the OCB that belongs to
//    its model? (trigger_flags 1 = sword/ranged, 0 = hammer)
//  - Larson/antitrigger: does bit 0x80 (IFL_ANTITRIGGER_ONESHOT)
//    really stand on the item that is supposed to carry it?
//
// Usage: ItemCheck <level.trc> [typeId ...]
// Without a typeId all items are listed.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: ItemCheck <level.trc> [typeId ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error))
    {
        std::printf("not loadable: %s\n", error.c_str());
        return 1;
    }

    std::vector<int> wanted;
    for (int i = 2; i < argc; ++i) wanted.push_back(std::atoi(argv[i]));

    std::printf("%s: %d Items\n", argv[1], lvl.itemCount());
    // Position plus its place within the sector: the sector center
    // lies at x.5 * 1024. "dX/dZ" shows the offset to the center in units -
    // 0 means dead center, +-512 would be the sector edge.
    std::printf("%5s %5s %5s %6s %6s %8s %7s %6s  %9s %7s %9s  %11s\n",
                "item", "type", "room", "OCB", "raw", "flags", "angle", "deg",
                "X", "Y", "Z", "dX/dZ center");

    int shown = 0;
    for (int i = 0; i < lvl.itemCount(); ++i)
    {
        Tr5Item it;
        if (!lvl.getItem(i, it)) continue;

        if (!wanted.empty())
        {
            bool hit = false;
            for (int g : wanted) if (it.objectNumber == g) hit = true;
            if (!hit) continue;
        }

        // The OCB sits in the lower 6 bits of trigger_flags - but the upper
        // bits are not garnish, they carry meanings of their own, and
        // whoever looks only at the 6 bits gets misled: Andy2's Puzzle Hole 2
        // has trigger_flags 1058. Masked, that looks like OCB 34; in truth
        // >1024 in PuzzleHoleCollision (pickup.cpp) means "PuzzleType
        // 2" and on solving starts the cutscene trigger_flags - 1024, here
        // 34. Both the raw and the masked value are printed below.
        // Offset to the sector center: the center lies at (n + 0.5) * 1024,
        // so the remainder of (coordinate mod 1024) minus 512 is the
        // distance.
        const int dx = ((it.x % 1024) + 1024) % 1024 - 512;
        const int dz = ((it.z % 1024) + 1024) % 1024 - 512;
        std::printf("%5d %5d %5d %6d %6d   0x%04X %7d %6d  %9d %7d %9d  %5d /%5d\n",
                    i, (int)it.objectNumber, (int)it.roomNumber,
                    (int)(it.triggerFlags & 0x3F), (int)(uint16_t)it.triggerFlags,
                    (unsigned)(uint16_t)it.flags, (int)it.yRot,
                    (int)((long long)it.yRot * 360 / 65536),
                    it.x, it.y, it.z, dx, dz);
        ++shown;
    }
    std::printf("%d items printed\n", shown);
    return 0;
}

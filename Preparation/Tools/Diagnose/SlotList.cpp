// SlotList - lists which moveable slots a level carries.
//
// The randomizer scatters a consumable only where the level can draw it -
// presentSlots comes from Tr5Level::moveableSlots. A pickup without a model
// would be an invisible stumbling block.
//
// Usage: SlotList <level.trc> [slot ...]
//        without slots: how many there are, and the highest
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
    if (argc < 2) { std::printf("usage: SlotList <level.trc> [slot ...]\n"); return 1; }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error))
    { std::printf("not loadable: %s\n", error.c_str()); return 1; }

    const std::vector<int> slots = lvl.moveableSlots();

    if (argc > 2)
    {
        for (int i = 2; i < argc; ++i)
        {
            const int want = std::atoi(argv[i]);
            bool have = false;
            for (size_t k = 0; k < slots.size(); ++k)
                if (slots[k] == want) { have = true; break; }
            std::printf("  slot %4d: %s\n", want, have ? "present" : "MISSING");
        }
        return 0;
    }

    int hi = -1;
    for (size_t k = 0; k < slots.size(); ++k)
        if (slots[k] > hi) hi = slots[k];
    std::printf("%zu slots, highest %d\n", slots.size(), hi);
    return 0;
}

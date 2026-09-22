// SlotAnims - measures an object slot's anim slice.
//
// Why this is needed: an enemy's control code often reaches with
// anim_index + N across the GLOBAL anim table, while the object table only
// "owns" it up to the next object. If N is larger than the slice, the
// model transfer's heuristic cuts off too early - in the target the engine
// then reads past the table end: an animation loop (the Imp's death flight)
// or an out-of-range frame-pointer abort (the Hydra).
//
// Usage: SlotAnims <slot> <level.trc> [level.trc ...]
//
// Output per level: the slot's anim_index, the next higher anim_index in
// the level, and the difference = the slice the heuristic would take.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: SlotAnims <slot> <level.trc> [level.trc ...]\n");
        return 1;
    }

    const int slot = std::atoi(argv[1]);
    std::printf("slot %d\n", slot);
    std::printf("%-14s %8s %8s %9s %8s  %s\n",
                "level", "meshes", "anim@", "next", "slice", "note");

    for (int i = 2; i < argc; ++i)
    {
        std::string path(argv[i]);
        const size_t cut = path.find_last_of("\\/");
        std::string name = (cut == std::string::npos) ? path : path.substr(cut + 1);

        Tr5Level lvl;
        std::string error;
        if (!lvl.load(toW(argv[i]), error))
        {
            std::printf("%-14s  not loadable: %s\n", name.c_str(), error.c_str());
            continue;
        }

        Tr5Level::MoveableInfo mv;
        if (!lvl.moveableInfo(slot, mv))
        {
            std::printf("%-14s  -- no model --\n", name.c_str());
            continue;
        }

        // The next higher anim_index over all slots: exactly the boundary
        // the transfer heuristic draws.
        int next = -1;
        for (int s : lvl.moveableSlots())
        {
            Tr5Level::MoveableInfo o;
            if (!lvl.moveableInfo(s, o)) continue;
            if (o.animIndex <= mv.animIndex) continue;
            if (next < 0 || o.animIndex < next) next = o.animIndex;
        }

        if (mv.animIndex < 0)
        {
            std::printf("%-14s %8d %8s %9s %8s  slot without its own animations\n",
                        name.c_str(), mv.nmeshes, "-", "-", "-");
            continue;
        }

        if (next < 0)
        {
            std::printf("%-14s %8d %8d %9s %8s  last slot - the slice runs to the table end\n",
                        name.c_str(), mv.nmeshes, mv.animIndex, "(none)", "?");
            continue;
        }

        std::printf("%-14s %8d %8d %9d %8d\n",
                    name.c_str(), mv.nmeshes, mv.animIndex, next,
                    next - mv.animIndex);
    }
    return 0;
}

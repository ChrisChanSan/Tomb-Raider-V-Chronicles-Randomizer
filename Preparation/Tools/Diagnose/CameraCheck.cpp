// CameraCheck - lists the fixed cameras of a .trc with their flags.
//
// Occasion (2026-08-29): first person now pushes a trigger's Camera command
// aside, but the sniper/game camera is exempt - flags & 3. The user found two
// triggers in Andrea1 that look identical in trview (a Camera command plus a
// Lara comment) and behave differently: trigger 31 -> camera 4 stays in first
// person, trigger 11 -> camera 1 does not. From the trigger side that cannot
// be told apart; the difference has to be in the camera table.
//
// Usage: CameraCheck <level.trc> [index ...]     without indices: all
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

// What the engine makes of the low bits. camera.cpp reads flags & 3 into
// SniperCamActive and flags & 2 as "aim at Lara's torso"; control.cpp ORs
// IFL_INVISIBLE (0x100) in from the trigger at runtime, so a 0x100 in the
// FILE is a level-editor setting, not that.
static const char* meaning(int flags)
{
    if (flags & 2) return "sniper camera - aims at Lara's torso, draws the overlay, first person stays OUT";
    if (flags & 1) return "game camera - locks look key and inventory, first person stays OUT";
    return "plain cinematic camera - first person pushes it aside";
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Usage: CameraCheck <level.trc> [index ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string err;

    if (!lvl.load(toW(argv[1]), err))
    {
        std::printf("%s: %s\n", argv[1], err.c_str());
        return 1;
    }

    std::vector<Tr5Level::CameraRecord> cams;
    if (!lvl.cameraRecords(cams, err))
    {
        std::printf("%s: %s\n", argv[1], err.c_str());
        return 1;
    }

    std::printf("%s: %zu fixed cameras\n\n", argv[1], cams.size());
    std::printf("  idx  room        x        y        z   flags  \n");

    std::vector<int> want;
    for (int i = 2; i < argc; ++i) want.push_back(std::atoi(argv[i]));

    for (size_t i = 0; i < cams.size(); ++i)
    {
        if (!want.empty())
        {
            bool asked = false;
            for (int w : want) if (w == (int)i) asked = true;
            if (!asked) continue;
        }

        const Tr5Level::CameraRecord& c = cams[i];
        std::printf("  %3zu  %4d %8d %8d %8d  0x%04X  %s\n",
                    i, c.room, c.x, c.y, c.z, c.flags, meaning(c.flags));
    }

    return 0;
}

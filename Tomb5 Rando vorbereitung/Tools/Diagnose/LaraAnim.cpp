// LaraAnim - which of Lara's animations a level REALLY carries.
//
// Why this exists: Lara's anim table has the same length in every TR5
// level, but the FRAME DATA is level-specific. Where a level never needs
// an animation, Core leaves the entry in place with its frame pointer
// pointing at foreign data - what plays then is nonsense (a frozen pose,
// the wrong movement, the wrong length). Noticed at ANIM_KEYCARD (437) in
// joby3 and at the switch in andy3.
//
// The probe is a space calculation, not a guess:
//   keyframes = (frame_end - frame_base) / rate + 1
//   needed    = keyframes * FrameSize * 2 bytes
//   available = next larger frame pointer of ANY anim - own one
// If the needed does not fit into the available, this animation's data
// cannot lie there.
//
// Several anims may share ONE pointer when they have the same frame_base -
// then they are variants of the same movement block (andy3: 468/469/470
// all start on frame 8190). Counted is therefore the space for the
// LONGEST anim of the group.
//
// Usage: LaraAnim <level.trc> [level.trc ...]        overview per level
//        LaraAnim -all <level.trc> [...]             every anim individually
//        LaraAnim -anim <no> <level.trc> [...]       one anim in comparison
#include "core/Tr5Level.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

// The animations the engine picks for object interactions. Without real
// data Lara does not only look wrong - the length is wrong too, and she
// can leave the sector before the trigger takes hold.
struct AnimName { int nr; const char* name; };
static const AnimName kImportant[] = {
    {  63, "SWITCHOFF" },     {  64, "SWITCHON" },      { 131, "USEKEY" },
    { 134, "USEPUZZLE" },     { 135, "PICKUP" },        { 195, "GENERATORSW_OFF" },
    { 196, "GENERATORSW_ON" },{ 197, "ONEHANDPUSHSW" }, { 204, "PICKUPF" },
    { 291, "DUCKPICKUP" },    { 312, "DUCKPICKUPF" },   { 313, "PUSHDOOR" },
    { 314, "PULLDOOR" },      { 315, "KICKDOOR" },      { 316, "BLOCKSWITCH" },
    { 317, "LIFTTRAP" },      { 318, "PULLTRAP" },      { 319, "TURNSWITCHC" },
    { 320, "TURNSWITCHA" },   { 322, "COGSWITCHS" },    { 324, "RAILSWITCH" },
    { 325, "HIDDENPICKUP" },  { 403, "CROWBARDOOR" },   { 412, "TWODOOR" },
    { 413, "SMALLSWITCH" },   { 414, "LEAPSWITCH" },    { 415, "WATERSWITCH" },
    { 416, "WATERDOOR" },     { 419, "CROWBAR" },       { 420, "CROWSWITCH" },
    { 424, "PLINTHHI" },      { 425, "PLINTHLO" },      { 437, "KEYCARD" },
    { 460, "CROWDOVE" },      { 470, "AIRLOCKSW" },     { 471, "JOBYCROW" },
};

struct AnimInfo
{
    uint32_t frameOfs = 0;
    int base = 0, end = 0, rate = 0, size = 0;
    long long needed = 0, available = 0;
    bool fits = false;
};

// Measure all of a level's anims. Returns false when Lara is missing.
static bool measure(const Tr5Level& lvl, std::vector<AnimInfo>& out, int& base)
{
    Tr5Level::MoveableInfo mv;
    if (!lvl.moveableInfo(0, mv) || mv.animIndex < 0) return false;
    base = mv.animIndex;

    size_t animAt = 0; int32_t nAnims = 0;
    if (!lvl.debugAnimBlock(animAt, nAnims)) return false;
    const std::vector<uint8_t>& b = lvl.debugBytes();
    auto r16 = [&](size_t at) { uint16_t v; std::memcpy(&v, b.data() + at, 2); return v; };
    auto r32 = [&](size_t at) { uint32_t v; std::memcpy(&v, b.data() + at, 4); return v; };

    // Collect all frame pointers to find the next larger one.
    std::vector<uint32_t> pointers;
    for (int32_t i = 0; i < nAnims; ++i) pointers.push_back(r32(animAt + static_cast<size_t>(i) * 40));

    size_t objAt = 0, frAt = 0; int32_t objN = 0, frN = 0;
    lvl.debugModelBlocks(objAt, objN, frAt, frN);
    const uint32_t frameEndTotal = static_cast<uint32_t>(frN) * 2;

    out.clear();
    for (int32_t i = 0; i < nAnims; ++i)
    {
        const size_t o = animAt + static_cast<size_t>(i) * 40;
        AnimInfo a;
        a.frameOfs = r32(o);
        a.rate     = b[o + 4];
        a.size     = b[o + 5];
        a.base     = r16(o + 24);
        a.end      = r16(o + 26);

        const int span = a.end - a.base;
        const int keyframes = (a.rate > 0 && span >= 0) ? (span / a.rate + 1) : 1;
        a.needed = static_cast<long long>(keyframes) * a.size * 2;

        uint32_t next = frameEndTotal;
        for (uint32_t z : pointers) if (z > a.frameOfs && z < next) next = z;
        a.available = static_cast<long long>(next) - a.frameOfs;
        // DECISIVE: FrameSize 0 means the anim has no frame data of its
        // own at all. The engine computes frame_ptr[n * 0] and shows the
        // same picture forever - exactly the frozen pose that stood for 22
        // seconds at the keycard in joby3. Core's stub marker.
        a.fits = (a.size > 0) && (a.needed <= a.available);
        out.push_back(a);
    }
    return true;
}

int main(int argc, char** argv)
{
    bool all = false; int single = -1; int first = 1;
    while (first < argc && argv[first][0] == '-')
    {
        if (!std::strcmp(argv[first], "-all")) { all = true; ++first; }
        else if (!std::strcmp(argv[first], "-anim") && first + 1 < argc)
        { single = std::atoi(argv[first + 1]); first += 2; }
        else break;
    }
    if (first >= argc)
    {
        std::printf("usage: LaraAnim [-all] [-anim <no>] <level.trc> [level.trc ...]\n");
        return 1;
    }

    for (int f = first; f < argc; ++f)
    {
        Tr5Level lvl; std::string error;
        if (!lvl.load(toW(argv[f]), error)) { std::printf("%s: %s\n", argv[f], error.c_str()); continue; }

        std::vector<AnimInfo> a; int base = 0;
        if (!measure(lvl, a, base)) { std::printf("%s: no Lara\n", argv[f]); continue; }

        const char* shortName = std::strrchr(argv[f], '\\');
        if (!shortName) shortName = std::strrchr(argv[f], '/');
        shortName = shortName ? shortName + 1 : argv[f];

        if (single >= 0)
        {
            const size_t i = static_cast<size_t>(base + single);
            if (i >= a.size()) { std::printf("%-16s anim %d out of range\n", shortName, single); continue; }
            std::printf("%-16s anim %3d: ofs %-8u frames %5d..%-5d rate %-2d size %-3d  "
                        "needs %5lld / has %6lld  %s\n",
                        shortName, single, a[i].frameOfs, a[i].base, a[i].end, a[i].rate, a[i].size,
                        a[i].needed, a[i].available, a[i].size == 0 ? "STUB" : (a[i].fits ? "OK" : "TOO TIGHT"));
            continue;
        }

        std::printf("\n=== %s  (%zu anims from %d) ===\n", shortName, a.size(), base);
        int missing = 0;
        for (const AnimName& w : kImportant)
        {
            const size_t i = static_cast<size_t>(base + w.nr);
            if (i >= a.size())
            {
                std::printf("  %-16s (%3d) MISSING - table too short\n", w.name, w.nr);
                ++missing;
                continue;
            }
            if (a[i].size == 0)
            {
                std::printf("  %-16s (%3d) STUB - FrameSize 0, no frame data of its own\n", w.name, w.nr);
                ++missing;
            }
            else if (!a[i].fits)
            {
                std::printf("  %-16s (%3d) TOO TIGHT - needs %lld bytes, has %lld\n",
                            w.name, w.nr, a[i].needed, a[i].available);
                ++missing;
            }
            else if (all)
            {
                std::printf("  %-16s (%3d) ok  (%lld/%lld bytes)\n",
                            w.name, w.nr, a[i].needed, a[i].available);
            }
        }
        if (!missing) std::printf("  all %zu interaction anims carry real data\n",
                                  sizeof(kImportant) / sizeof(kImportant[0]));
    }
    return 0;
}

// FixCheck - applies LevelFix to a level and saves the result, so the added
// triggers can be measured with SectorCheck instead of believed.
//
// Usage: FixCheck <in.trc> <levelName> <out.trc>
#include <cstdio>
#include <string>
#include <vector>
#include "core/Tr5Level.h"
#include "core/LevelFix.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("Usage: FixCheck <in.trc> <levelName> <out.trc>\n"); return 1; }

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("load: %s\n", err.c_str()); return 1; }

    std::vector<std::string> warn;
    const int n = LevelFix::apply(lvl, argv[2], warn);
    std::printf("LevelFix applied %d fix(es) to %s\n", n, argv[2]);
    for (const std::string& w : warn) std::printf("  WARN %s\n", w.c_str());

    if (!lvl.save(toW(argv[3]), err)) { std::printf("save: %s\n", err.c_str()); return 1; }
    std::printf("written: %s\n", argv[3]);
    return 0;
}

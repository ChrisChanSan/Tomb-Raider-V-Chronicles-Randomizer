// One-off cross-check: what does Tr5Level::laraAnimUsable say?
#include "core/Tr5Level.h"
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: AnimProbe <level.trc> <animNo> [...]\n"); return 1; }
    std::wstring p; for (const char* s = argv[1]; *s; ++s) p += (wchar_t)(unsigned char)*s;
    Tr5Level lvl; std::string err;
    if (!lvl.load(p, err)) { std::printf("load: %s\n", err.c_str()); return 1; }
    for (int i = 2; i < argc; ++i)
    {
        const int n = std::atoi(argv[i]);
        std::printf("  anim %-4d usable: %s\n", n, lvl.laraAnimUsable(n) ? "YES" : "NO");
    }
    return 0;
}

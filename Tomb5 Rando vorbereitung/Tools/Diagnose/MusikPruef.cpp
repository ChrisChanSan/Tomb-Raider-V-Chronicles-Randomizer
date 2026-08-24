// MusikPruef <level.trc> [...] - welche TO_CD-Tracks loesen die Trigger
// eines Levels aus? Je Level eine Zeile, am Ende die Vereinigungsmenge.
#include "core/Tr5Level.h"
#include <cstdio>
#include <set>
#include <string>

static std::wstring toW(const char* s) { std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Aufruf: MusikPruef <level.trc> [...]\n"); return 2; }
    std::set<int> alle;
    for (int i = 1; i < argc; ++i)
    {
        Tr5Level lvl; std::string err;
        if (!lvl.load(toW(argv[i]), err)) { std::printf("%s: %s\n", argv[i], err.c_str()); continue; }
        std::set<int> t;
        lvl.musicTriggers(nullptr, &t);
        std::printf("%-60s", argv[i]);
        for (int x : t) { std::printf(" %d", x); alle.insert(x); }
        std::printf("\n");
    }
    std::printf("\nVEREINIGUNG (%zu):", alle.size());
    for (int x : alle) std::printf(" %d", x);
    std::printf("\n");
    return 0;
}

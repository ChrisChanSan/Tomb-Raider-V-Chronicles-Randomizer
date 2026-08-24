// Einmalige Gegenprobe: was sagt Tr5Level::laraAnimUsable?
#include "core/Tr5Level.h"
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("Aufruf: AnimProbe <level.trc> <animNr> [...]\n"); return 1; }
    std::wstring p; for (const char* s = argv[1]; *s; ++s) p += (wchar_t)(unsigned char)*s;
    Tr5Level lvl; std::string err;
    if (!lvl.load(p, err)) { std::printf("Laden: %s\n", err.c_str()); return 1; }
    for (int i = 2; i < argc; ++i)
    {
        const int n = std::atoi(argv[i]);
        std::printf("  Anim %-4d brauchbar: %s\n", n, lvl.laraAnimUsable(n) ? "JA" : "NEIN");
    }
    return 0;
}

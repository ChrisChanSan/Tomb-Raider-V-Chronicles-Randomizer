// Gegenprobe fuer ScriptDat.
//   ScriptProbe <SCRIPT.DAT>                                  - was steht drin
//   ScriptProbe <ein.dat> <aus.dat> <von> <nach> <keyNr>       - KEY kopieren
#include "core/ScriptDat.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static std::wstring w(const char* s) { std::wstring r; for (; *s; ++s) r += (wchar_t)(unsigned char)*s; return r; }

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Aufruf: ScriptProbe <SCRIPT.DAT> [aus.dat von nach keyNr]\n"); return 1; }

    ScriptDat sd; std::string err;
    if (!sd.load(w(argv[1]), err)) { std::printf("Fehler: %s\n", err.c_str()); return 1; }

    if (argc >= 6)
    {
        const int nr = std::atoi(argv[5]);
        bool ch = false;
        if (!sd.copyObjectCommand(argv[3], argv[4], ScriptDat::cmdKey(nr), ch, err))
        { std::printf("Fehler: %s\n", err.c_str()); return 1; }
        std::printf("KEY%d von %s nach %s: %s\n", nr, argv[3], argv[4], ch ? "eingefuegt" : "war schon da");

        // Zweiter Aufruf muss folgenlos bleiben - Wiederholbarkeit.
        bool ch2 = false;
        if (!sd.copyObjectCommand(argv[3], argv[4], ScriptDat::cmdKey(nr), ch2, err))
        { std::printf("Fehler beim zweiten Lauf: %s\n", err.c_str()); return 1; }
        std::printf("zweiter Aufruf: %s\n", ch2 ? "NOCHMAL eingefuegt - FALSCH" : "folgenlos, richtig");

        if (!sd.save(w(argv[2]), err)) { std::printf("Fehler: %s\n", err.c_str()); return 1; }
        std::printf("geschrieben: %s\n\n", argv[2]);
    }

    std::printf("ScriptDat fand %zu Level:\n", sd.levels().size());
    for (const auto& e : sd.levels())
        std::printf("   %-16s flags=0x%04X%s\n", e.fileName.c_str(), e.flags,
                    (e.flags & 0x0001) ? "  YOUNGLARA" : "");
    return 0;
}

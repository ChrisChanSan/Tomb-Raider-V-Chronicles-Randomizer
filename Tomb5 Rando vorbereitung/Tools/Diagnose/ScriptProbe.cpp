// Cross-check for ScriptDat.
//   ScriptProbe <SCRIPT.DAT>                                  - what is inside
//   ScriptProbe <in.dat> <out.dat> <from> <to> <keyNo>        - copy a KEY
#include "core/ScriptDat.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static std::wstring w(const char* s) { std::wstring r; for (; *s; ++s) r += (wchar_t)(unsigned char)*s; return r; }

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: ScriptProbe <SCRIPT.DAT> [out.dat from to keyNo]\n"); return 1; }

    ScriptDat sd; std::string err;
    if (!sd.load(w(argv[1]), err)) { std::printf("error: %s\n", err.c_str()); return 1; }

    if (argc >= 6)
    {
        const int nr = std::atoi(argv[5]);
        bool ch = false;
        if (!sd.copyObjectCommand(argv[3], argv[4], ScriptDat::cmdKey(nr), ch, err))
        { std::printf("error: %s\n", err.c_str()); return 1; }
        std::printf("KEY%d from %s to %s: %s\n", nr, argv[3], argv[4], ch ? "inserted" : "was already there");

        // A second call must have no effect - repeatability.
        bool ch2 = false;
        if (!sd.copyObjectCommand(argv[3], argv[4], ScriptDat::cmdKey(nr), ch2, err))
        { std::printf("error on the second run: %s\n", err.c_str()); return 1; }
        std::printf("second call: %s\n", ch2 ? "inserted AGAIN - WRONG" : "no effect, correct");

        if (!sd.save(w(argv[2]), err)) { std::printf("error: %s\n", err.c_str()); return 1; }
        std::printf("written: %s\n\n", argv[2]);
    }

    std::printf("ScriptDat found %zu levels:\n", sd.levels().size());
    for (const auto& e : sd.levels())
        std::printf("   %-16s flags=0x%04X%s\n", e.fileName.c_str(), e.flags,
                    (e.flags & 0x0001) ? "  YOUNGLARA" : "");
    return 0;
}

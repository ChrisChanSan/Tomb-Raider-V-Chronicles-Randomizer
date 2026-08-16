// Gegenprobe: liefert die Bibliotheksfassung Byte fuer Byte dasselbe Paket
// wie das Werkzeug TR3Hol? Ohne diesen Nachweis waere der Port ein Umbau ins
// Blaue - die Formatbruecke ist die empfindlichste Stelle des Projekts.
#include "core/Tr3Quelle.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 4) { std::printf("Aufruf: TqPruef <level.TR2> <objektId> <werkzeug.bin>\n"); return 2; }

    std::string p(argv[1]);
    std::wstring wp(p.begin(), p.end());

    std::vector<uint8_t> neu;
    std::string fehler;
    if (!Tr3Quelle::holeMoveable(wp, std::atoi(argv[2]), neu, fehler))
    { std::printf("GESCHEITERT: %s\n", fehler.c_str()); return 1; }

    std::vector<uint8_t> alt;
    {
        FILE* f = std::fopen(argv[3], "rb");
        if (!f) { std::printf("Vergleichsdatei nicht lesbar\n"); return 2; }
        std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        alt.resize((size_t)n);
        std::fread(alt.data(), 1, alt.size(), f);
        std::fclose(f);
    }

    if (alt.size() != neu.size())
    { std::printf("UNTERSCHIED: Werkzeug %zu Byte, Bibliothek %zu Byte\n", alt.size(), neu.size()); return 1; }
    for (size_t i = 0; i < alt.size(); ++i)
        if (alt[i] != neu[i]) { std::printf("UNTERSCHIED ab Byte %zu\n", i); return 1; }

    std::printf("GLEICH: %zu Byte, Byte fuer Byte identisch\n", neu.size());
    return 0;
}

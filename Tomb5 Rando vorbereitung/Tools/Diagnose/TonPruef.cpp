// Gegenprobe fuer Klangentnahme und SFX-Umbiegen (#92).
#include "core/Tr3Source.h"
#include <cstdio>
#include <string>
#include <vector>

static bool lies(const char* p, std::vector<uint8_t>& b)
{
    FILE* f = std::fopen(p, "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    b.resize((size_t)n); std::fread(b.data(), 1, b.size(), f); std::fclose(f); return true;
}

int main(int argc, char** argv)
{
    // TonPruef <MAIN.SFX> <werkzeug_ton.bin> <id:wave[,wave]> ...
    if (argc < 4) { std::printf("Aufruf: TonPruef <MAIN.SFX> <werkzeug.bin> <id:wave,...> ...\n"); return 2; }
    std::string p(argv[1]); std::wstring wp(p.begin(), p.end());

    std::vector<Tr3Source::SoundRequest> w;
    for (int a = 3; a < argc; ++a)
    {
        Tr3Source::SoundRequest k;
        k.tr3Id = std::atoi(argv[a]);
        const char* d = std::strchr(argv[a], ':');
        if (!d) { std::printf("erwartet <id>:<wave>\n"); return 2; }
        for (const char* q = d + 1; *q; )
        { k.waves.push_back(std::atoi(q)); while (*q && *q != ',') ++q; if (*q == ',') ++q; }
        w.push_back(k);
    }

    std::vector<uint8_t> neu; std::string error;
    if (!Tr3Source::fetchSounds(wp, w, neu, error))
    { std::printf("GESCHEITERT: %s\n", error.c_str()); return 1; }

    std::vector<uint8_t> alt;
    if (!lies(argv[2], alt)) { std::printf("Vergleichsdatei nicht lesbar\n"); return 2; }

    if (alt.size() != neu.size())
    { std::printf("UNTERSCHIED: Werkzeug %zu, Bibliothek %zu Byte\n", alt.size(), neu.size()); return 1; }
    for (size_t i = 0; i < alt.size(); ++i)
        if (alt[i] != neu[i]) { std::printf("UNTERSCHIED ab Byte %zu\n", i); return 1; }
    std::printf("GLEICH: %zu Byte\n", neu.size());
    return 0;
}

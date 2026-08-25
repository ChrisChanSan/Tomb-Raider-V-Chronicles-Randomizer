// Cross-check for sound extraction and SFX rerouting (#92).
#include "core/Tr3Source.h"
#include <cstdio>
#include <string>
#include <vector>

static bool readAll(const char* p, std::vector<uint8_t>& b)
{
    FILE* f = std::fopen(p, "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    b.resize((size_t)n); std::fread(b.data(), 1, b.size(), f); std::fclose(f); return true;
}

int main(int argc, char** argv)
{
    // TR3SoundCheck <MAIN.SFX> <tool_sound.bin> <id:wave[,wave]> ...
    if (argc < 4) { std::printf("usage: TR3SoundCheck <MAIN.SFX> <tool.bin> <id:wave,...> ...\n"); return 2; }
    std::string p(argv[1]); std::wstring wp(p.begin(), p.end());

    std::vector<Tr3Source::SoundRequest> w;
    for (int a = 3; a < argc; ++a)
    {
        Tr3Source::SoundRequest k;
        k.tr3Id = std::atoi(argv[a]);
        const char* d = std::strchr(argv[a], ':');
        if (!d) { std::printf("expected <id>:<wave>\n"); return 2; }
        for (const char* q = d + 1; *q; )
        { k.waves.push_back(std::atoi(q)); while (*q && *q != ',') ++q; if (*q == ',') ++q; }
        w.push_back(k);
    }

    std::vector<uint8_t> fresh; std::string error;
    if (!Tr3Source::fetchSounds(wp, w, fresh, error))
    { std::printf("FAILED: %s\n", error.c_str()); return 1; }

    std::vector<uint8_t> old;
    if (!readAll(argv[2], old)) { std::printf("comparison file not readable\n"); return 2; }

    if (old.size() != fresh.size())
    { std::printf("DIFFERENT: tool %zu, library %zu bytes\n", old.size(), fresh.size()); return 1; }
    for (size_t i = 0; i < old.size(); ++i)
        if (old[i] != fresh[i]) { std::printf("DIFFERENT from byte %zu\n", i); return 1; }
    std::printf("EQUAL: %zu bytes\n", fresh.size());
    return 0;
}

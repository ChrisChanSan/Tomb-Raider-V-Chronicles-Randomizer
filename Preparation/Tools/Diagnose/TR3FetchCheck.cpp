// Cross-check: does the library version deliver, byte for byte, the same
// package as the TR3Fetch tool? Without this proof the port would be a
// rebuild into the blue - the format bridge is the project's most
// sensitive spot.
#include "core/Tr3Source.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 4) { std::printf("usage: TR3FetchCheck <level.TR2> <objectId> <tool.bin>\n"); return 2; }

    std::string p(argv[1]);
    std::wstring wp(p.begin(), p.end());

    std::vector<uint8_t> fresh;
    std::string error;
    if (!Tr3Source::fetchMoveable(wp, std::atoi(argv[2]), fresh, error))
    { std::printf("FAILED: %s\n", error.c_str()); return 1; }

    std::vector<uint8_t> old;
    {
        FILE* f = std::fopen(argv[3], "rb");
        if (!f) { std::printf("comparison file not readable\n"); return 2; }
        std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        old.resize((size_t)n);
        std::fread(old.data(), 1, old.size(), f);
        std::fclose(f);
    }

    if (old.size() != fresh.size())
    { std::printf("DIFFERENT: tool %zu bytes, library %zu bytes\n", old.size(), fresh.size()); return 1; }
    for (size_t i = 0; i < old.size(); ++i)
        if (old[i] != fresh[i]) { std::printf("DIFFERENT from byte %zu\n", i); return 1; }

    std::printf("EQUAL: %zu bytes, byte-for-byte identical\n", fresh.size());
    return 0;
}

// Cross-check of the dino table (#92): does it produce, for all four, the
// same packages as the tools? One call, four guests, no argument fiddling.
#include "core/Tr3Source.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>

static bool readAll(const std::string& p, std::vector<uint8_t>& b)
{
    FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    b.resize((size_t)n); std::fread(b.data(), 1, b.size(), f); std::fclose(f); return true;
}
static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// The three header words per sound (volume, chance, chars) are SKIPPED by
// the inserter - Tr5Tr3Import.cpp:117 jumps over them with "p += 8" and
// rebuilds the SAMPLE_INFOs. The older T-Rex package comes from the tool's
// table mode and there still carries values from the source level; the
// library writes zeros like the wave mode. For the comparison they are
// therefore zeroed on both sides - everything else MUST match.
static void zeroHeaderWords(std::vector<uint8_t>& b)
{
    size_t q = 12;
    while (q + 10 <= b.size())
    {
        for (size_t k = q + 2; k < q + 8; ++k) b[k] = 0;
        uint16_t var; std::memcpy(&var, &b[q + 8], 2);
        if (var < 1 || var > 15) break;
        q += 10;
        for (uint16_t v = 0; v < var; ++v)
        {
            if (q + 4 > b.size()) return;
            uint32_t len; std::memcpy(&len, &b[q], 4);
            q += 4 + len;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 4) { std::printf("usage: DinoCheck <tr3Dir> <tr3GoldDir> <packageFolder>\n"); return 2; }
    const std::wstring tr3 = W(argv[1]), gold = W(argv[2]);
    const std::string dir(argv[3]);

    // File names of the comparison packages built with the tools. They are
    // outside file names the author chose, so "geier" (vulture) and the
    // "_ton" suffix (sound) stay as they are - renaming them here would
    // only stop the tool from finding the packages on disk.
    const char* mod[4] = { "trex.bin", "raptor_mod.bin", "compy_mod.bin", "geier.bin" };
    // "_ton" is the suffix the sound packages were exported under - an
    // outside file name, so it stays as it is.
    const char* snd[4] = { "trex_ton.bin", "raptor_ton.bin", "compy_ton.bin", "geier_ton.bin" };

    int i = 0, error = 0;
    for (const Tr3Source::DinoSource& d : Tr3Source::dinos())
    {
        std::string f;
        std::vector<uint8_t> m, t, vm, vt;

        if (!Tr3Source::fetchMoveable(Tr3Source::levelPath(d, tr3, gold), d.object, m, f))
        { std::printf("%-8s model FAILED: %s\n", d.name, f.c_str()); ++error; ++i; continue; }
        if (!Tr3Source::fetchSounds(Tr3Source::sfxPath(d, tr3, gold), d.sounds, t, f))
        { std::printf("%-8s sounds FAILED: %s\n", d.name, f.c_str()); ++error; ++i; continue; }

        const bool okM = readAll(dir + "\\" + mod[i], vm) && vm == m;
        bool okT = readAll(dir + "\\" + snd[i], vt);
        if (okT) { zeroHeaderWords(vt); zeroHeaderWords(t); okT = (vt == t); }
        std::printf("%-8s model %7zu bytes %-10s sounds %7zu bytes %s\n",
                    d.name, m.size(), okM ? "identical" : "DEVIATION",
                    t.size(), okT ? "identical" : "DEVIATION");
        if (!okM || !okT) ++error;
        ++i;
    }
    std::printf("\n%s\n", error ? "AT LEAST ONE DEVIATION" : "All four match the tool packages.");
    return error ? 1 : 0;
}

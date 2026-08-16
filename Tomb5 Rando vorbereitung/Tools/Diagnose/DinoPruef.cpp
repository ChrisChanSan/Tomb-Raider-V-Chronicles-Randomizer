// Gegenprobe der Dino-Tabelle (#92): erzeugt sie fuer alle vier dieselben
// Pakete wie die Werkzeuge? Ein Aufruf, vier Gaeste, kein Argumentbasteln.
#include "core/Tr3Quelle.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>

static bool lies(const std::string& p, std::vector<uint8_t>& b)
{
    FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    b.resize((size_t)n); std::fread(b.data(), 1, b.size(), f); std::fclose(f); return true;
}
static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Die drei Kopfwoerter je Klang (Volume, Chance, Chars) werden vom Einsetzer
// UEBERLESEN - Tr5Tr3Import.cpp:117 springt mit "p += 8" darueber hinweg und
// baut die SAMPLE_INFOs neu auf. Das aeltere T-Rex-Paket stammt aus dem
// Tabellenmodus des Werkzeugs und traegt dort noch Werte aus dem Quelllevel;
// die Bibliothek schreibt wie der Wellenmodus Nullen. Fuer den Vergleich
// werden sie deshalb auf beiden Seiten genullt - alles andere MUSS stimmen.
static void nulleKopfwoerter(std::vector<uint8_t>& b)
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
    if (argc != 4) { std::printf("Aufruf: DinoPruef <tr3Dir> <tr3GoldDir> <paketOrdner>\n"); return 2; }
    const std::wstring tr3 = W(argv[1]), gold = W(argv[2]);
    const std::string ord(argv[3]);

    // Dateinamen der mit den Werkzeugen gebauten Vergleichspakete
    const char* mod[4] = { "trex.bin", "raptor_mod.bin", "compy_mod.bin", "geier.bin" };
    const char* ton[4] = { "trex_ton.bin", "raptor_ton.bin", "compy_ton.bin", "geier_ton.bin" };

    int i = 0, fehler = 0;
    for (const Tr3Quelle::DinoQuelle& d : Tr3Quelle::dinos())
    {
        std::string f;
        std::vector<uint8_t> m, t, vm, vt;

        if (!Tr3Quelle::holeMoveable(Tr3Quelle::levelPfad(d, tr3, gold), d.objekt, m, f))
        { std::printf("%-8s Modell GESCHEITERT: %s\n", d.name, f.c_str()); ++fehler; ++i; continue; }
        if (!Tr3Quelle::holeKlaenge(Tr3Quelle::sfxPfad(d, tr3, gold), d.klaenge, t, f))
        { std::printf("%-8s Klaenge GESCHEITERT: %s\n", d.name, f.c_str()); ++fehler; ++i; continue; }

        const bool okM = lies(ord + "\\" + mod[i], vm) && vm == m;
        bool okT = lies(ord + "\\" + ton[i], vt);
        if (okT) { nulleKopfwoerter(vt); nulleKopfwoerter(t); okT = (vt == t); }
        std::printf("%-8s Modell %7zu Byte %-10s Klaenge %7zu Byte %s\n",
                    d.name, m.size(), okM ? "identisch" : "ABWEICHUNG",
                    t.size(), okT ? "identisch" : "ABWEICHUNG");
        if (!okM || !okT) ++fehler;
        ++i;
    }
    std::printf("\n%s\n", fehler ? "MINDESTENS EINE ABWEICHUNG" : "Alle vier stimmen mit den Werkzeugpaketen ueberein.");
    return fehler ? 1 : 0;
}

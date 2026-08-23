// LevelDiff <a.trc> <b.trc> - zwei TR5-Leveldateien Block fuer Block
// vergleichen. Gebaut fuer die Frage "wie weit weicht ein umgebautes Level vom
// Original ab" - etwa bei einem HD-Texturport.
//
// Der TR5-Kopf ist:
//     long  version
//     short rtPages, otPages, btPages       Texturseiten je Art
//     3 x { long size, long compSize, Nutzdaten }   32bit / 16bit / misc
//     short data[16]                        LaraDrawType, WeatherType, ...
//     long  mainSize, long mainComp, Hauptblock (unkomprimiert)
//     dahinter: die Sound-Samples, die die Engine direkt aus der Datei liest
//
// Der Hauptblock traegt ALLES, was kein Bild ist: Raeume, Floordata, Items,
// Trigger, Meshes, Animationen, Klanginfos, Texinfos. Ist er byteweise gleich,
// wurden ausschliesslich Bilder getauscht.
//
// Bauen (64 Bit, im Ordner Bittenichtbitdefender):
//   set R=..\..\Tomb5 Native win 11 fix\TR5Randomizer\src
//   set Z=..\..\Tomb5 Native win 11 fix\TOMB5 Code\TOMB5\tomb5\libs\zlib
//   cl /nologo /EHsc /O2 /std:c++17 /w /I "%R%" /I "%Z%" LevelDiff.cpp
//       "%R%\core\Tr5Level.cpp" "%R%\core\DataFolder.cpp" "%Z%\*.c"
//       /Fe:LevelDiff.exe
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct Block
{
    int32_t size = 0;
    int32_t comp = 0;
    size_t  at = 0;         // Beginn der Nutzdaten
    size_t  len = 0;        // tatsaechlich belegte Bytes
};

struct Kopf
{
    bool     ok = false;
    int32_t  version = 0;
    int16_t  rt = 0, ot = 0, bt = 0;
    bool     komprimiert = false;
    Block    tex[3];
    int16_t  data16[16] = {};
    Block    main;
    size_t   restAt = 0, restLen = 0;
};

bool lies(const std::wstring& pfad, std::vector<uint8_t>& raus)
{
    FILE* f = _wfopen(pfad.c_str(), L"rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    raus.resize(n > 0 ? static_cast<size_t>(n) : 0);
    if (!raus.empty()) std::fread(raus.data(), 1, raus.size(), f);
    std::fclose(f);
    return true;
}

int32_t i32(const std::vector<uint8_t>& b, size_t p)
{ int32_t v = 0; std::memcpy(&v, b.data() + p, 4); return v; }

int16_t i16(const std::vector<uint8_t>& b, size_t p)
{ int16_t v = 0; std::memcpy(&v, b.data() + p, 2); return v; }

// Dieselbe Zwei-Wege-Deutung wie Tr5Level::readHeader: ob die Texturbloecke
// komprimiert sind, steht nirgends als Flag - erkannt wird es daran, dass der
// Hauptblock danach schluessig liegt (size == compSize).
Kopf leseKopf(const std::vector<uint8_t>& b)
{
    Kopf k;
    if (b.size() < 16) return k;

    k.version = i32(b, 0);
    k.rt = i16(b, 4);
    k.ot = i16(b, 6);
    k.bt = i16(b, 8);

    for (int modus = 0; modus < 2; ++modus)
    {
        const bool komp = (modus == 0);
        size_t p = 10;
        bool gut = true;
        Block tex[3];

        for (int i = 0; i < 3 && gut; ++i)
        {
            if (p + 8 > b.size()) { gut = false; break; }
            tex[i].size = i32(b, p);
            tex[i].comp = i32(b, p + 4);
            p += 8;
            if (tex[i].size < 0 || tex[i].comp < 0) { gut = false; break; }
            tex[i].at = p;
            tex[i].len = static_cast<size_t>(komp ? tex[i].comp : tex[i].size);
            if (p + tex[i].len > b.size()) { gut = false; break; }
            p += tex[i].len;
        }
        if (!gut) continue;

        if (p + 32 + 8 > b.size()) continue;
        size_t d16 = p;
        p += 32;

        const int32_t mainSize = i32(b, p);
        const int32_t mainComp = i32(b, p + 4);
        p += 8;
        if (mainSize <= 0 || mainSize != mainComp) continue;
        if (p + static_cast<size_t>(mainSize) > b.size()) continue;

        k.ok = true;
        k.komprimiert = komp;
        for (int i = 0; i < 3; ++i) k.tex[i] = tex[i];
        for (int i = 0; i < 16; ++i) k.data16[i] = i16(b, d16 + i * 2);
        k.main.size = mainSize;
        k.main.comp = mainComp;
        k.main.at = p;
        k.main.len = static_cast<size_t>(mainSize);
        k.restAt = p + k.main.len;
        k.restLen = b.size() - k.restAt;
        return k;
    }

    return k;
}

// Byteweiser Vergleich zweier Bereiche. Liefert die Zahl abweichender Bytes
// und die erste Fundstelle (relativ zum Bereichsanfang).
struct Abgleich
{
    bool     gleicheLaenge = false;
    long long verschieden = 0;
    long long ersteStelle = -1;
};

Abgleich vergleiche(const std::vector<uint8_t>& a, size_t aAt, size_t aLen,
                    const std::vector<uint8_t>& b, size_t bAt, size_t bLen)
{
    Abgleich r;
    r.gleicheLaenge = (aLen == bLen);
    const size_t n = aLen < bLen ? aLen : bLen;

    for (size_t i = 0; i < n; ++i)
    {
        if (a[aAt + i] != b[bAt + i])
        {
            if (r.ersteStelle < 0) r.ersteStelle = static_cast<long long>(i);
            r.verschieden++;
        }
    }

    if (aLen != bLen) r.verschieden += static_cast<long long>(aLen > bLen ? aLen - bLen : bLen - aLen);
    return r;
}

void zeigeBlock(const char* name, const Block& x, const Block& y)
{
    std::printf("  %-22s %10d / %-10d %10d / %-10d %s\n", name,
                x.size, x.comp, y.size, y.comp,
                (x.size == y.size && x.comp == y.comp) ? "" : "  ABWEICHUNG");
}

const char* jaNein(bool b) { return b ? "ja" : "NEIN"; }

std::wstring weit(const char* s)
{ std::wstring w; while (*s) w += static_cast<wchar_t>(static_cast<unsigned char>(*s++)); return w; }

}   // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("Aufruf: LevelDiff <a.trc> <b.trc>\n");
        return 2;
    }

    const std::wstring pA = weit(argv[1]);
    const std::wstring pB = weit(argv[2]);

    std::vector<uint8_t> a, b;
    if (!lies(pA, a)) { std::printf("Nicht lesbar: %s\n", argv[1]); return 2; }
    if (!lies(pB, b)) { std::printf("Nicht lesbar: %s\n", argv[2]); return 2; }

    std::printf("A = %s  (%zu Bytes)\n", argv[1], a.size());
    std::printf("B = %s  (%zu Bytes)\n", argv[2], b.size());
    std::printf("Groessenunterschied: %+lld Bytes\n\n",
                static_cast<long long>(b.size()) - static_cast<long long>(a.size()));

    const Kopf kA = leseKopf(a);
    const Kopf kB = leseKopf(b);

    if (!kA.ok || !kB.ok)
    {
        std::printf("Kopfteil nicht deutbar (A %s, B %s)\n", jaNein(kA.ok), jaNein(kB.ok));
        return 1;
    }

    std::printf("=== KOPF ===\n");
    std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
    std::printf("  %-22s %-23d %-23d %s\n", "Version", kA.version, kB.version,
                kA.version == kB.version ? "" : "  ABWEICHUNG");
    std::printf("  %-22s %-23s %-23s %s\n", "Texturen komprimiert",
                jaNein(kA.komprimiert), jaNein(kB.komprimiert),
                kA.komprimiert == kB.komprimiert ? "" : "  ABWEICHUNG");
    std::printf("  %-22s %-23d %-23d %s\n", "Raumtexturseiten", kA.rt, kB.rt,
                kA.rt == kB.rt ? "" : "  ABWEICHUNG");
    std::printf("  %-22s %-23d %-23d %s\n", "Objekttexturseiten", kA.ot, kB.ot,
                kA.ot == kB.ot ? "" : "  ABWEICHUNG");
    std::printf("  %-22s %-23d %-23d %s\n", "Bumpmap-Seiten", kA.bt, kB.bt,
                kA.bt == kB.bt ? "" : "  ABWEICHUNG");

    std::printf("\n=== BLOECKE (size / compSize) ===\n");
    std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
    zeigeBlock("Texturen 32 Bit", kA.tex[0], kB.tex[0]);
    zeigeBlock("Texturen 16 Bit", kA.tex[1], kB.tex[1]);
    zeigeBlock("Texturen Misc", kA.tex[2], kB.tex[2]);
    zeigeBlock("HAUPTBLOCK", kA.main, kB.main);
    std::printf("  %-22s %10zu %-11s %10zu %-11s %s\n", "Sound-Samples danach",
                kA.restLen, "", kB.restLen, "",
                kA.restLen == kB.restLen ? "" : "  ABWEICHUNG");

    std::printf("\n=== LEVELSCHALTER (short data[16]) ===\n");
    bool d16Gleich = true;
    for (int i = 0; i < 16; ++i)
    {
        if (kA.data16[i] == kB.data16[i]) continue;
        d16Gleich = false;
        const char* name = (i == 0) ? "LaraDrawType"
                         : (i == 1) ? "WeatherType"
                         : (i == 2) ? "FremdeSprechkoepfe" : "";
        std::printf("  data16[%2d] %-20s %6d -> %6d\n", i, name, kA.data16[i], kB.data16[i]);
    }
    if (d16Gleich) std::printf("  alle 16 identisch\n");

    std::printf("\n=== BYTEVERGLEICH ===\n");
    {
        const Abgleich m = vergleiche(a, kA.main.at, kA.main.len, b, kB.main.at, kB.main.len);
        std::printf("  Hauptblock:      %s",
                    (m.gleicheLaenge && m.verschieden == 0) ? "BYTEGLEICH" : "abweichend");
        if (m.verschieden)
            std::printf("  (%lld Bytes verschieden, erste Stelle +%lld)", m.verschieden, m.ersteStelle);
        std::printf("\n");

        const Abgleich s = vergleiche(a, kA.restAt, kA.restLen, b, kB.restAt, kB.restLen);
        std::printf("  Sound-Samples:   %s",
                    (s.gleicheLaenge && s.verschieden == 0) ? "BYTEGLEICH" : "abweichend");
        if (s.verschieden)
            std::printf("  (%lld Bytes verschieden, erste Stelle +%lld)", s.verschieden, s.ersteStelle);
        std::printf("\n");

        for (int i = 0; i < 3; ++i)
        {
            const Abgleich t = vergleiche(a, kA.tex[i].at, kA.tex[i].len,
                                          b, kB.tex[i].at, kB.tex[i].len);
            const char* nm = i == 0 ? "Texturen 32Bit" : i == 1 ? "Texturen 16Bit" : "Texturen Misc";
            std::printf("  %-16s %s", nm,
                        (t.gleicheLaenge && t.verschieden == 0) ? "BYTEGLEICH" : "abweichend");
            if (t.verschieden)
                std::printf("  (%lld Bytes verschieden, erste Stelle +%lld)", t.verschieden, t.ersteStelle);
            std::printf("\n");
        }
    }

    std::printf("\n=== INHALT (ueber den Levelleser) ===\n");
    Tr5Level la, lb;
    std::string eA, eB;
    const bool okA = la.load(pA, eA);
    const bool okB = lb.load(pB, eB);

    if (!okA) std::printf("  A nicht ladbar: %s\n", eA.c_str());
    if (!okB) std::printf("  B nicht ladbar: %s\n", eB.c_str());

    if (okA && okB)
    {
        auto zeile = [](const char* name, long long x, long long y)
        {
            std::printf("  %-22s %-23lld %-23lld %s\n", name, x, y,
                        x == y ? "" : "  ABWEICHUNG");
        };
        std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
        zeile("Raeume", la.roomCount(), lb.roomCount());
        zeile("Items", la.itemCount(), lb.itemCount());
        zeile("Floordata-Woerter", la.floorDataCount(), lb.floorDataCount());
        zeile("Klangproben", la.soundSampleCount(), lb.soundSampleCount());
        zeile("Moveable-Slots", (long long)la.moveableSlots().size(),
                                (long long)lb.moveableSlots().size());
        zeile("Texinfos", la.debugTexinfoCount(), lb.debugTexinfoCount());
        zeile("Raum-Leseabweichungen", la.roomParseMismatches(), lb.roomParseMismatches());

        // Wo liegen die bekannten Unterbloecke? Damit laesst sich einordnen,
        // ob eine Byteabweichung im Hauptblock vor oder hinter den Texturdaten
        // beginnt - alles hinter einer Einfuegestelle verschiebt sich ohnehin.
        std::printf("\n=== LAGE DER UNTERBLOECKE (Abstand vom Hauptblockanfang) ===\n");
        {
            auto rel = [](size_t abs, size_t basis) -> long long
            { return static_cast<long long>(abs) - static_cast<long long>(basis); };

            size_t tA = 0, tB = 0, aA = 0, aB = 0;
            int32_t nn = 0;
            la.debugTexinfoBlock(tA, nn);
            lb.debugTexinfoBlock(tB, nn);
            la.debugAnimBlock(aA, nn);
            lb.debugAnimBlock(aB, nn);

            size_t lutA = 0, infA = 0, lutB = 0, infB = 0;
            int32_t n1 = 0, n2 = 0;
            la.debugKlangBloecke(lutA, infA, n1, n2);
            lb.debugKlangBloecke(lutB, infB, n1, n2);

            std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
            std::printf("  %-22s %-23lld %-23lld\n", "Animationen",
                        rel(aA, kA.main.at), rel(aB, kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "Texinfos",
                        rel(tA, kA.main.at), rel(tB, kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "Items",
                        rel(la.itemCountOffset(), kA.main.at), rel(lb.itemCountOffset(), kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "Klang-Zuordnung",
                        rel(lutA, kA.main.at), rel(lutB, kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "Klang-Infos",
                        rel(infA, kA.main.at), rel(infB, kB.main.at));

            // Abschnittsweiser Vergleich zwischen den bekannten Grenzen. Eine
            // Einfuegung verschiebt alles dahinter, ein reiner Bytevergleich
            // ueber den ganzen Hauptblock zeigt deshalb Abweichung, wo nur
            // verschoben wurde. Je Abschnitt mit den EIGENEN Grenzen verglichen
            // faellt die Verschiebung heraus und es bleibt die echte Aenderung.
            std::printf("\n=== ABSCHNITTE, JE MIT EIGENEN GRENZEN VERGLICHEN ===\n");

            struct Marke { const char* name; size_t a, b; };
            const Marke marken[] = {
                { "Raeume+Floordata+Meshes", kA.main.at, kB.main.at },
                { "Animationen bis Texinfo", aA, aB },
                { "Texinfos bis Items",      tA, tB },
                { "Items bis Klangtabelle",  la.itemCountOffset(), lb.itemCountOffset() },
                { "Klangtabelle bis Infos",  lutA, lutB },
                { "Klang-Infos bis Ende",    infA, infB },
            };
            const size_t endeA = kA.main.at + kA.main.len;
            const size_t endeB = kB.main.at + kB.main.len;

            for (int i = 0; i < 6; ++i)
            {
                const size_t a0 = marken[i].a;
                const size_t b0 = marken[i].b;
                const size_t a1 = (i < 5) ? marken[i + 1].a : endeA;
                const size_t b1 = (i < 5) ? marken[i + 1].b : endeB;

                if (a0 > a1 || b0 > b1 || a1 > a.size() || b1 > b.size())
                { std::printf("  %-26s (Grenzen unbrauchbar)\n", marken[i].name); continue; }

                const Abgleich v = vergleiche(a, a0, a1 - a0, b, b0, b1 - b0);
                std::printf("  %-26s %8zu -> %8zu Bytes  %s", marken[i].name,
                            a1 - a0, b1 - b0,
                            (v.gleicheLaenge && v.verschieden == 0) ? "BYTEGLEICH" : "abweichend");
                if (v.verschieden)
                    std::printf("  (%lld verschieden, erste Stelle +%lld)",
                                v.verschieden, v.ersteStelle);
                std::printf("\n");
            }
        }

        // Der eigentliche Beweis: stimmen die SPIELINHALTE ueberein, auch wenn
        // sich durch eingefuegte Texturdaten alles dahinter verschoben hat?
        std::printf("\n=== SPIELINHALTE ===\n");
        {
            int itemsAnders = 0;
            const int n = la.itemCount() < lb.itemCount() ? la.itemCount() : lb.itemCount();
            for (int i = 0; i < n; ++i)
            {
                Tr5Item x, y;
                if (!la.getItem(i, x) || !lb.getItem(i, y)) { itemsAnders++; continue; }
                if (x.objectNumber != y.objectNumber || x.roomNumber != y.roomNumber ||
                    x.x != y.x || x.y != y.y || x.z != y.z || x.yRot != y.yRot ||
                    x.shade != y.shade || x.triggerFlags != y.triggerFlags || x.flags != y.flags)
                {
                    if (itemsAnders < 12)
                        std::printf("  Item %3d: Slot %d/%d Raum %d/%d Pos %d,%d,%d / %d,%d,%d "
                                    "Winkel %d/%d Schatten %d/%d OCB %d/%d Flags %d/%d\n",
                                    i, x.objectNumber, y.objectNumber, x.roomNumber, y.roomNumber,
                                    x.x, x.y, x.z, y.x, y.y, y.z, x.yRot, y.yRot,
                                    x.shade, y.shade, x.triggerFlags, y.triggerFlags,
                                    x.flags, y.flags);
                    itemsAnders++;
                }
            }
            std::printf("  Items abweichend:      %d von %d\n", itemsAnders, n);

            // Floordata sektorweise ueber die Ketten vergleichen - das ist die
            // Ebene, auf der Trigger und Begehbarkeit wirklich stehen.
            int sektoren = 0, sektorenAnders = 0;
            {
                const std::vector<Tr5Room>& ra2 = la.rooms();
                const std::vector<Tr5Room>& rb2 = lb.rooms();
                const size_t rn2 = ra2.size() < rb2.size() ? ra2.size() : rb2.size();

                for (size_t r = 0; r < rn2; ++r)
                {
                    const int nx = ra2[r].numXSectors < rb2[r].numXSectors
                                 ? ra2[r].numXSectors : rb2[r].numXSectors;
                    const int nz = ra2[r].numZSectors < rb2[r].numZSectors
                                 ? ra2[r].numZSectors : rb2[r].numZSectors;

                    for (int sx = 0; sx < nx; ++sx)
                        for (int sz = 0; sz < nz; ++sz)
                        {
                            sektoren++;
                            const int ia = la.sectorIndex(static_cast<int>(r), sx, sz);
                            const int ib = lb.sectorIndex(static_cast<int>(r), sx, sz);

                            std::vector<Tr5FloorEntry> ka, kb;
                            const bool oa = ia > 0 && la.readChain(ia, ka);
                            const bool ob = ib > 0 && lb.readChain(ib, kb);

                            if ((ia > 0) != (ib > 0) || oa != ob || ka.size() != kb.size())
                            { sektorenAnders++; continue; }

                            for (size_t e = 0; e < ka.size(); ++e)
                            {
                                if (ka[e].funktion != kb[e].funktion ||
                                    ka[e].art != kb[e].art ||
                                    ka[e].woerter != kb[e].woerter)
                                { sektorenAnders++; break; }
                            }
                        }
                }
            }
            std::printf("  Floordata-Sektoren:    %d abweichend von %d\n",
                        sektorenAnders, sektoren);

            int raeumeAnders = 0;
            const std::vector<Tr5Room>& ra = la.rooms();
            const std::vector<Tr5Room>& rb = lb.rooms();
            const size_t rn = ra.size() < rb.size() ? ra.size() : rb.size();
            for (size_t i = 0; i < rn; ++i)
            {
                if (ra[i].originX != rb[i].originX || ra[i].originZ != rb[i].originZ ||
                    ra[i].yBottom != rb[i].yBottom ||
                    ra[i].numXSectors != rb[i].numXSectors ||
                    ra[i].numZSectors != rb[i].numZSectors ||
                    ra[i].altRoom != rb[i].altRoom ||
                    ra[i].portalRooms != rb[i].portalRooms)
                {
                    if (raeumeAnders < 8)
                        std::printf("  Raum %3zu weicht ab (Ursprung %d,%d / %d,%d, "
                                    "Sektoren %dx%d / %dx%d, Flip %d/%d)\n", i,
                                    ra[i].originX, ra[i].originZ, rb[i].originX, rb[i].originZ,
                                    ra[i].numXSectors, ra[i].numZSectors,
                                    rb[i].numXSectors, rb[i].numZSectors,
                                    ra[i].altRoom, rb[i].altRoom);
                    raeumeAnders++;
                }
            }
            std::printf("  Raeume abweichend:     %d von %zu\n", raeumeAnders, rn);

            // Raumweiser Bytevergleich, jeder Raum mit seinen EIGENEN Grenzen.
            // Damit faellt die Verschiebung durch vorherige Raeume heraus und
            // es bleibt, was sich im Raum selbst geaendert hat - Geometrie,
            // Beleuchtung und die Texturverweise seiner Flaechen.
            std::printf("\n=== RAEUME EINZELN (eigene Grenzen) ===\n");
            {
                int anders = 0, groesser = 0, kleiner = 0;
                long long summeDelta = 0;
                std::string erste;

                for (size_t i = 0; i < rn; ++i)
                {
                    const size_t a0 = ra[i].headerOffset;
                    const size_t b0 = rb[i].headerOffset;
                    const size_t a1 = (i + 1 < ra.size()) ? ra[i + 1].headerOffset : 0;
                    const size_t b1 = (i + 1 < rb.size()) ? rb[i + 1].headerOffset : 0;
                    if (!a0 || !b0 || !a1 || !b1 || a1 <= a0 || b1 <= b0) continue;

                    const size_t la_ = a1 - a0, lb_ = b1 - b0;
                    const Abgleich v = vergleiche(a, a0, la_, b, b0, lb_);
                    if (v.gleicheLaenge && v.verschieden == 0) continue;

                    anders++;
                    const long long d = static_cast<long long>(lb_) - static_cast<long long>(la_);
                    summeDelta += d;
                    if (d > 0) groesser++; else if (d < 0) kleiner++;

                    if (anders <= 10)
                    {
                        char zeile[160];
                        std::snprintf(zeile, sizeof(zeile),
                                      "  Raum %3zu: %6zu -> %6zu Bytes (%+lld), %lld Bytes anders "
                                      "ab +%lld\n", i, la_, lb_, d, v.verschieden, v.ersteStelle);
                        erste += zeile;
                    }
                }

                std::printf("%s", erste.c_str());
                if (anders > 10) std::printf("  ... und %d weitere\n", anders - 10);
                std::printf("  %d von %zu Raeumen veraendert (%d groesser, %d kleiner, "
                            "zusammen %+lld Bytes)\n", anders, rn, groesser, kleiner, summeDelta);
            }
        }

        // Meshes: Zahl, Blockgroesse und ein Vergleich Mesh fuer Mesh mit
        // eigenen Grenzen. Ein Mesh traegt seine Texturverweise in den
        // Flaechen - wer Texturen neu anordnet, aendert genau hier etwas,
        // ohne die Geometrie anzufassen.
        std::printf("\n=== MESHES ===\n");
        {
            size_t mdA = 0, mdB = 0;
            int32_t mnA = 0, mnB = 0;
            la.debugMeshDataBlock(mdA, mnA);
            lb.debugMeshDataBlock(mdB, mnB);

            std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
            std::printf("  %-22s %-23d %-23d %s\n", "Meshzeiger",
                        la.debugMeshPtrCount(), lb.debugMeshPtrCount(),
                        la.debugMeshPtrCount() == lb.debugMeshPtrCount() ? "" : "  ABWEICHUNG");
            std::printf("  %-22s %-23d %-23d %s\n", "Meshdaten (Woerter)", mnA, mnB,
                        mnA == mnB ? "" : "  ABWEICHUNG");

            const int mn = la.debugMeshPtrCount() < lb.debugMeshPtrCount()
                         ? la.debugMeshPtrCount() : lb.debugMeshPtrCount();
            int anders = 0, gleicheLaenge = 0;
            long long delta = 0;
            std::string liste;

            for (int i = 0; i < mn; ++i)
            {
                int va = 0, na = 0, q4a = 0, q3a = 0, ba = 0;
                int vb = 0, nb = 0, q4b = 0, q3b = 0, bb = 0;
                const bool wa = la.debugMeshWalk(i, va, na, q4a, q3a, ba);
                const bool wb = lb.debugMeshWalk(i, vb, nb, q4b, q3b, bb);
                if (!wa || !wb) { anders++; continue; }

                size_t aa = 0, ab = 0;
                la.debugMeshAt(i, aa);
                lb.debugMeshAt(i, ab);

                const Abgleich v = vergleiche(a, aa, static_cast<size_t>(ba),
                                              b, ab, static_cast<size_t>(bb));
                if (v.gleicheLaenge && v.verschieden == 0) continue;

                anders++;
                delta += bb - ba;
                if (ba == bb) gleicheLaenge++;

                if (anders <= 10)
                {
                    char z[200];
                    std::snprintf(z, sizeof(z),
                                  "  Mesh %4d: %5d -> %5d Bytes, Ecken %d/%d Normalen %d/%d "
                                  "Vierecke %d/%d Dreiecke %d/%d\n",
                                  i, ba, bb, va, vb, na, nb, q4a, q4b, q3a, q3b);
                    liste += z;
                }
            }

            std::printf("%s", liste.c_str());
            if (anders > 10) std::printf("  ... und %d weitere\n", anders - 10);
            std::printf("  %d von %d Meshes veraendert, davon %d bei GLEICHER Groesse "
                        "(zusammen %+lld Bytes)\n", anders, mn, gleicheLaenge, delta);

            // Zu welchem Objekt gehoeren die geaenderten Meshes? Ein Moveable
            // belegt meshIndex .. meshIndex+nmeshes-1 der Zeigerliste.
            if (anders > 0 && anders <= 10)
            {
                for (int i = 0; i < mn; ++i)
                {
                    int va = 0, na = 0, q4a = 0, q3a = 0, ba = 0;
                    int vb = 0, nb = 0, q4b = 0, q3b = 0, bb = 0;
                    if (!la.debugMeshWalk(i, va, na, q4a, q3a, ba)) continue;
                    if (!lb.debugMeshWalk(i, vb, nb, q4b, q3b, bb)) continue;
                    size_t aa = 0, ab = 0;
                    la.debugMeshAt(i, aa);
                    lb.debugMeshAt(i, ab);
                    const Abgleich v = vergleiche(a, aa, (size_t)ba, b, ab, (size_t)bb);
                    if (v.gleicheLaenge && v.verschieden == 0) continue;

                    std::string wer;
                    for (int s : la.moveableSlots())
                    {
                        Tr5Level::MoveableInfo mi;
                        if (!la.moveableInfo(s, mi)) continue;
                        if (i >= mi.meshIndex && i < mi.meshIndex + mi.nmeshes)
                        {
                            wer = "Slot " + std::to_string(s) + ", Teil " +
                                  std::to_string(i - mi.meshIndex);
                            break;
                        }
                    }
                    std::printf("  Mesh %4d gehoert zu: %s\n", i,
                                wer.empty() ? "keinem Moveable (Statik oder ungenutzt)" : wer.c_str());
                }
            }

            // Wohin sind die fehlenden Bytes? Wenn jedes einzelne Mesh gleich
            // gross blieb, der Block aber schrumpfte, lag im Original Ballast:
            // Daten, auf die kein Zeiger zeigt.
            auto belegt = [](const Tr5Level& L, long long& summe, int& verschiedene,
                             long long& hoechstesEnde)
            {
                std::vector<size_t> gesehen;
                summe = 0; hoechstesEnde = 0;
                size_t basis = 0; int32_t woerter = 0;
                L.debugMeshDataBlock(basis, woerter);

                for (int i = 0; i < L.debugMeshPtrCount(); ++i)
                {
                    size_t at = 0;
                    int v = 0, n = 0, q4 = 0, q3 = 0, by = 0;
                    if (!L.debugMeshAt(i, at) || !L.debugMeshWalk(i, v, n, q4, q3, by)) continue;

                    bool neu = true;
                    for (size_t g : gesehen) if (g == at) { neu = false; break; }
                    if (!neu) continue;

                    gesehen.push_back(at);
                    summe += by;
                    const long long ende = (long long)(at - basis) + by;
                    if (ende > hoechstesEnde) hoechstesEnde = ende;
                }
                verschiedene = (int)gesehen.size();
            };

            long long sA = 0, sB = 0, eA2 = 0, eB2 = 0;
            int dA = 0, dB = 0;
            belegt(la, sA, dA, eA2);
            belegt(lb, sB, dB, eB2);

            std::printf("\n  %-26s %-16s %-16s\n", "", "A", "B");
            std::printf("  %-26s %-16d %-16d\n", "verschiedene Meshes", dA, dB);
            std::printf("  %-26s %-16lld %-16lld\n", "davon belegte Bytes", sA, sB);
            std::printf("  %-26s %-16lld %-16lld\n", "hoechstes Meshende", eA2, eB2);
            std::printf("  %-26s %-16lld %-16lld\n", "Blockgroesse in Bytes",
                        (long long)mnA * 2, (long long)mnB * 2);
            std::printf("  %-26s %-16lld %-16lld\n", "davon von keinem Zeiger belegt",
                        (long long)mnA * 2 - sA, (long long)mnB * 2 - sB);
        }

        // Welche Objektplaetze sind dazugekommen oder verschwunden?
        const std::vector<int> sa = la.moveableSlots();
        const std::vector<int> sb = lb.moveableSlots();
        auto drin = [](const std::vector<int>& v, int s)
        { for (int x : v) if (x == s) return true; return false; };

        std::string nurA, nurB;
        for (int s : sa) if (!drin(sb, s)) { nurA += " " + std::to_string(s); }
        for (int s : sb) if (!drin(sa, s)) { nurB += " " + std::to_string(s); }
        if (!nurA.empty()) std::printf("  nur in A:%s\n", nurA.c_str());
        if (!nurB.empty()) std::printf("  nur in B:%s\n", nurB.c_str());
    }

    return 0;
}

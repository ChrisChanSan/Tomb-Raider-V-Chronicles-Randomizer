// LevelDiff <a.trc> <b.trc> - compare two TR5 level files block by block.
// Built for the question "how far does a rebuilt level deviate from the
// original" - e.g. with an HD texture port.
//
// The TR5 header is:
//     long  version
//     short rtPages, otPages, btPages       texture pages per kind
//     3 x { long size, long compSize, payload }   32bit / 16bit / misc
//     short data[16]                        LaraDrawType, WeatherType, ...
//     long  mainSize, long mainComp, main block (uncompressed)
//     behind it: the sound samples the engine reads straight from the file
//
// The main block carries EVERYTHING that is not an image: rooms,
// floordata, items, triggers, meshes, animations, sound infos, texinfos.
// If it is byte-equal, only images were exchanged.
//
// Build (64 bit), from the .bat next to this file:
//   set R=..\..\..\TR5Randomizer\src
//   set Z=..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib
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
    size_t  at = 0;         // start of the payload
    size_t  len = 0;        // bytes actually occupied
};

struct Header
{
    bool     ok = false;
    int32_t  version = 0;
    int16_t  rt = 0, ot = 0, bt = 0;
    bool     compressed = false;
    Block    tex[3];
    int16_t  data16[16] = {};
    Block    main;
    size_t   restAt = 0, restLen = 0;
};

bool readAll(const std::wstring& path, std::vector<uint8_t>& out)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    if (!out.empty()) std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

int32_t i32(const std::vector<uint8_t>& b, size_t p)
{ int32_t v = 0; std::memcpy(&v, b.data() + p, 4); return v; }

int16_t i16(const std::vector<uint8_t>& b, size_t p)
{ int16_t v = 0; std::memcpy(&v, b.data() + p, 2); return v; }

// The same two-way interpretation as Tr5Level::readHeader: whether the
// texture blocks are compressed stands nowhere as a flag - it is
// recognized by the main block lying consistently behind them
// (size == compSize).
Header readHeader(const std::vector<uint8_t>& b)
{
    Header k;
    if (b.size() < 16) return k;

    k.version = i32(b, 0);
    k.rt = i16(b, 4);
    k.ot = i16(b, 6);
    k.bt = i16(b, 8);

    for (int mode = 0; mode < 2; ++mode)
    {
        const bool comp = (mode == 0);
        size_t p = 10;
        bool good = true;
        Block tex[3];

        for (int i = 0; i < 3 && good; ++i)
        {
            if (p + 8 > b.size()) { good = false; break; }
            tex[i].size = i32(b, p);
            tex[i].comp = i32(b, p + 4);
            p += 8;
            if (tex[i].size < 0 || tex[i].comp < 0) { good = false; break; }
            tex[i].at = p;
            tex[i].len = static_cast<size_t>(comp ? tex[i].comp : tex[i].size);
            if (p + tex[i].len > b.size()) { good = false; break; }
            p += tex[i].len;
        }
        if (!good) continue;

        if (p + 32 + 8 > b.size()) continue;
        size_t d16 = p;
        p += 32;

        const int32_t mainSize = i32(b, p);
        const int32_t mainComp = i32(b, p + 4);
        p += 8;
        if (mainSize <= 0 || mainSize != mainComp) continue;
        if (p + static_cast<size_t>(mainSize) > b.size()) continue;

        k.ok = true;
        k.compressed = comp;
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

// Byte-wise comparison of two ranges. Delivers the count of deviating
// bytes and the first hit (relative to the range start).
struct Match
{
    bool     sameLength = false;
    long long different = 0;
    long long firstAt = -1;
};

Match compare(const std::vector<uint8_t>& a, size_t aAt, size_t aLen,
              const std::vector<uint8_t>& b, size_t bAt, size_t bLen)
{
    Match r;
    r.sameLength = (aLen == bLen);
    const size_t n = aLen < bLen ? aLen : bLen;

    for (size_t i = 0; i < n; ++i)
    {
        if (a[aAt + i] != b[bAt + i])
        {
            if (r.firstAt < 0) r.firstAt = static_cast<long long>(i);
            r.different++;
        }
    }

    if (aLen != bLen) r.different += static_cast<long long>(aLen > bLen ? aLen - bLen : bLen - aLen);
    return r;
}

void showBlock(const char* name, const Block& x, const Block& y)
{
    std::printf("  %-22s %10d / %-10d %10d / %-10d %s\n", name,
                x.size, x.comp, y.size, y.comp,
                (x.size == y.size && x.comp == y.comp) ? "" : "  DEVIATION");
}

const char* yesNo(bool b) { return b ? "yes" : "NO"; }

std::wstring toW(const char* s)
{ std::wstring w; while (*s) w += static_cast<wchar_t>(static_cast<unsigned char>(*s++)); return w; }

}   // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: LevelDiff <a.trc> <b.trc>\n");
        return 2;
    }

    const std::wstring pA = toW(argv[1]);
    const std::wstring pB = toW(argv[2]);

    std::vector<uint8_t> a, b;
    if (!readAll(pA, a)) { std::printf("not readable: %s\n", argv[1]); return 2; }
    if (!readAll(pB, b)) { std::printf("not readable: %s\n", argv[2]); return 2; }

    std::printf("A = %s  (%zu bytes)\n", argv[1], a.size());
    std::printf("B = %s  (%zu bytes)\n", argv[2], b.size());
    std::printf("size difference: %+lld bytes\n\n",
                static_cast<long long>(b.size()) - static_cast<long long>(a.size()));

    const Header kA = readHeader(a);
    const Header kB = readHeader(b);

    if (!kA.ok || !kB.ok)
    {
        std::printf("header not interpretable (A %s, B %s)\n", yesNo(kA.ok), yesNo(kB.ok));
        return 1;
    }

    std::printf("=== HEADER ===\n");
    std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
    std::printf("  %-22s %-23d %-23d %s\n", "version", kA.version, kB.version,
                kA.version == kB.version ? "" : "  DEVIATION");
    std::printf("  %-22s %-23s %-23s %s\n", "textures compressed",
                yesNo(kA.compressed), yesNo(kB.compressed),
                kA.compressed == kB.compressed ? "" : "  DEVIATION");
    std::printf("  %-22s %-23d %-23d %s\n", "room texture pages", kA.rt, kB.rt,
                kA.rt == kB.rt ? "" : "  DEVIATION");
    std::printf("  %-22s %-23d %-23d %s\n", "object texture pages", kA.ot, kB.ot,
                kA.ot == kB.ot ? "" : "  DEVIATION");
    std::printf("  %-22s %-23d %-23d %s\n", "bumpmap pages", kA.bt, kB.bt,
                kA.bt == kB.bt ? "" : "  DEVIATION");

    std::printf("\n=== BLOCKS (size / compSize) ===\n");
    std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
    showBlock("textures 32 bit", kA.tex[0], kB.tex[0]);
    showBlock("textures 16 bit", kA.tex[1], kB.tex[1]);
    showBlock("textures misc", kA.tex[2], kB.tex[2]);
    showBlock("MAIN BLOCK", kA.main, kB.main);
    std::printf("  %-22s %10zu %-11s %10zu %-11s %s\n", "sound samples after",
                kA.restLen, "", kB.restLen, "",
                kA.restLen == kB.restLen ? "" : "  DEVIATION");

    std::printf("\n=== LEVEL SWITCHES (short data[16]) ===\n");
    bool d16Equal = true;
    for (int i = 0; i < 16; ++i)
    {
        if (kA.data16[i] == kB.data16[i]) continue;
        d16Equal = false;
        const char* name = (i == 0) ? "LaraDrawType"
                         : (i == 1) ? "WeatherType"
                         : (i == 2) ? "ForeignSpeechHeads" : "";
        std::printf("  data16[%2d] %-20s %6d -> %6d\n", i, name, kA.data16[i], kB.data16[i]);
    }
    if (d16Equal) std::printf("  all 16 identical\n");

    std::printf("\n=== BYTE COMPARISON ===\n");
    {
        const Match m = compare(a, kA.main.at, kA.main.len, b, kB.main.at, kB.main.len);
        std::printf("  main block:      %s",
                    (m.sameLength && m.different == 0) ? "BYTE-EQUAL" : "deviating");
        if (m.different)
            std::printf("  (%lld bytes different, first at +%lld)", m.different, m.firstAt);
        std::printf("\n");

        const Match s = compare(a, kA.restAt, kA.restLen, b, kB.restAt, kB.restLen);
        std::printf("  sound samples:   %s",
                    (s.sameLength && s.different == 0) ? "BYTE-EQUAL" : "deviating");
        if (s.different)
            std::printf("  (%lld bytes different, first at +%lld)", s.different, s.firstAt);
        std::printf("\n");

        for (int i = 0; i < 3; ++i)
        {
            const Match t = compare(a, kA.tex[i].at, kA.tex[i].len,
                                    b, kB.tex[i].at, kB.tex[i].len);
            const char* nm = i == 0 ? "textures 32bit" : i == 1 ? "textures 16bit" : "textures misc";
            std::printf("  %-16s %s", nm,
                        (t.sameLength && t.different == 0) ? "BYTE-EQUAL" : "deviating");
            if (t.different)
                std::printf("  (%lld bytes different, first at +%lld)", t.different, t.firstAt);
            std::printf("\n");
        }
    }

    std::printf("\n=== CONTENT (via the level reader) ===\n");
    Tr5Level la, lb;
    std::string eA, eB;
    const bool okA = la.load(pA, eA);
    const bool okB = lb.load(pB, eB);

    if (!okA) std::printf("  A not loadable: %s\n", eA.c_str());
    if (!okB) std::printf("  B not loadable: %s\n", eB.c_str());

    if (okA && okB)
    {
        auto line = [](const char* name, long long x, long long y)
        {
            std::printf("  %-22s %-23lld %-23lld %s\n", name, x, y,
                        x == y ? "" : "  DEVIATION");
        };
        std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
        line("rooms", la.roomCount(), lb.roomCount());
        line("items", la.itemCount(), lb.itemCount());
        line("floordata words", la.floorDataCount(), lb.floorDataCount());
        line("sound samples", la.soundSampleCount(), lb.soundSampleCount());
        line("moveable slots", (long long)la.moveableSlots().size(),
                                (long long)lb.moveableSlots().size());
        line("texinfos", la.debugTexinfoCount(), lb.debugTexinfoCount());
        line("room parse mismatches", la.roomParseMismatches(), lb.roomParseMismatches());

        // Where do the known sub-blocks lie? This tells whether a byte
        // deviation in the main block begins before or behind the texture
        // data - everything behind an insertion point shifts anyway.
        std::printf("\n=== POSITION OF THE SUB-BLOCKS (distance from the main block start) ===\n");
        {
            auto rel = [](size_t abs, size_t base) -> long long
            { return static_cast<long long>(abs) - static_cast<long long>(base); };

            size_t tA = 0, tB = 0, aA = 0, aB = 0;
            int32_t nn = 0;
            la.debugTexinfoBlock(tA, nn);
            lb.debugTexinfoBlock(tB, nn);
            la.debugAnimBlock(aA, nn);
            lb.debugAnimBlock(aB, nn);

            size_t lutA = 0, infA = 0, lutB = 0, infB = 0;
            int32_t n1 = 0, n2 = 0;
            la.debugSoundBlocks(lutA, infA, n1, n2);
            lb.debugSoundBlocks(lutB, infB, n1, n2);

            std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
            std::printf("  %-22s %-23lld %-23lld\n", "animations",
                        rel(aA, kA.main.at), rel(aB, kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "texinfos",
                        rel(tA, kA.main.at), rel(tB, kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "items",
                        rel(la.itemCountOffset(), kA.main.at), rel(lb.itemCountOffset(), kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "sound map",
                        rel(lutA, kA.main.at), rel(lutB, kB.main.at));
            std::printf("  %-22s %-23lld %-23lld\n", "sound infos",
                        rel(infA, kA.main.at), rel(infB, kB.main.at));

            // Section-wise comparison between the known boundaries. An
            // insertion shifts everything behind it, so a plain byte
            // comparison over the whole main block shows deviation where
            // only a shift occurred. Compared per section with its OWN
            // boundaries, the shift drops out and the real change remains.
            std::printf("\n=== SECTIONS, EACH COMPARED WITH ITS OWN BOUNDARIES ===\n");

            struct Mark { const char* name; size_t a, b; };
            const Mark marks[] = {
                { "rooms+floordata+meshes", kA.main.at, kB.main.at },
                { "animations to texinfo",  aA, aB },
                { "texinfos to items",      tA, tB },
                { "items to sound map",     la.itemCountOffset(), lb.itemCountOffset() },
                { "sound map to infos",     lutA, lutB },
                { "sound infos to end",     infA, infB },
            };
            const size_t endA = kA.main.at + kA.main.len;
            const size_t endB = kB.main.at + kB.main.len;

            for (int i = 0; i < 6; ++i)
            {
                const size_t a0 = marks[i].a;
                const size_t b0 = marks[i].b;
                const size_t a1 = (i < 5) ? marks[i + 1].a : endA;
                const size_t b1 = (i < 5) ? marks[i + 1].b : endB;

                if (a0 > a1 || b0 > b1 || a1 > a.size() || b1 > b.size())
                { std::printf("  %-26s (boundaries unusable)\n", marks[i].name); continue; }

                const Match v = compare(a, a0, a1 - a0, b, b0, b1 - b0);
                std::printf("  %-26s %8zu -> %8zu bytes  %s", marks[i].name,
                            a1 - a0, b1 - b0,
                            (v.sameLength && v.different == 0) ? "BYTE-EQUAL" : "deviating");
                if (v.different)
                    std::printf("  (%lld different, first at +%lld)",
                                v.different, v.firstAt);
                std::printf("\n");
            }
        }

        // The actual proof: do the GAME CONTENTS agree, even when inserted
        // texture data shifted everything behind it?
        std::printf("\n=== GAME CONTENTS ===\n");
        {
            int itemsDiffer = 0;
            const int n = la.itemCount() < lb.itemCount() ? la.itemCount() : lb.itemCount();
            for (int i = 0; i < n; ++i)
            {
                Tr5Item x, y;
                if (!la.getItem(i, x) || !lb.getItem(i, y)) { itemsDiffer++; continue; }
                if (x.objectNumber != y.objectNumber || x.roomNumber != y.roomNumber ||
                    x.x != y.x || x.y != y.y || x.z != y.z || x.yRot != y.yRot ||
                    x.shade != y.shade || x.triggerFlags != y.triggerFlags || x.flags != y.flags)
                {
                    if (itemsDiffer < 12)
                        std::printf("  item %3d: slot %d/%d room %d/%d pos %d,%d,%d / %d,%d,%d "
                                    "angle %d/%d shade %d/%d OCB %d/%d flags %d/%d\n",
                                    i, x.objectNumber, y.objectNumber, x.roomNumber, y.roomNumber,
                                    x.x, x.y, x.z, y.x, y.y, y.z, x.yRot, y.yRot,
                                    x.shade, y.shade, x.triggerFlags, y.triggerFlags,
                                    x.flags, y.flags);
                    itemsDiffer++;
                }
            }
            std::printf("  items deviating:       %d of %d\n", itemsDiffer, n);

            // Compare the floordata sector-wise via the chains - that is
            // the level on which triggers and walkability really stand.
            int sectors = 0, sectorsDiffer = 0;
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
                            sectors++;
                            const int ia = la.sectorIndex(static_cast<int>(r), sx, sz);
                            const int ib = lb.sectorIndex(static_cast<int>(r), sx, sz);

                            std::vector<Tr5FloorEntry> ka, kb;
                            const bool oa = ia > 0 && la.readChain(ia, ka);
                            const bool ob = ib > 0 && lb.readChain(ib, kb);

                            if ((ia > 0) != (ib > 0) || oa != ob || ka.size() != kb.size())
                            { sectorsDiffer++; continue; }

                            for (size_t e = 0; e < ka.size(); ++e)
                            {
                                if (ka[e].function != kb[e].function ||
                                    ka[e].kind != kb[e].kind ||
                                    ka[e].words != kb[e].words)
                                { sectorsDiffer++; break; }
                            }
                        }
                }
            }
            std::printf("  floordata sectors:     %d deviating of %d\n",
                        sectorsDiffer, sectors);

            int roomsDiffer = 0;
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
                    if (roomsDiffer < 8)
                        std::printf("  room %3zu deviates (origin %d,%d / %d,%d, "
                                    "sectors %dx%d / %dx%d, flip %d/%d)\n", i,
                                    ra[i].originX, ra[i].originZ, rb[i].originX, rb[i].originZ,
                                    ra[i].numXSectors, ra[i].numZSectors,
                                    rb[i].numXSectors, rb[i].numZSectors,
                                    ra[i].altRoom, rb[i].altRoom);
                    roomsDiffer++;
                }
            }
            std::printf("  rooms deviating:       %d of %zu\n", roomsDiffer, rn);

            // Room-wise byte comparison, every room with its OWN
            // boundaries. The shift through preceding rooms drops out and
            // what remains is what changed in the room itself - geometry,
            // lighting and its faces' texture references.
            std::printf("\n=== ROOMS INDIVIDUALLY (own boundaries) ===\n");
            {
                int differ = 0, larger = 0, smaller = 0;
                long long sumDelta = 0;
                std::string first;

                for (size_t i = 0; i < rn; ++i)
                {
                    const size_t a0 = ra[i].headerOffset;
                    const size_t b0 = rb[i].headerOffset;
                    const size_t a1 = (i + 1 < ra.size()) ? ra[i + 1].headerOffset : 0;
                    const size_t b1 = (i + 1 < rb.size()) ? rb[i + 1].headerOffset : 0;
                    if (!a0 || !b0 || !a1 || !b1 || a1 <= a0 || b1 <= b0) continue;

                    const size_t la_ = a1 - a0, lb_ = b1 - b0;
                    const Match v = compare(a, a0, la_, b, b0, lb_);
                    if (v.sameLength && v.different == 0) continue;

                    differ++;
                    const long long d = static_cast<long long>(lb_) - static_cast<long long>(la_);
                    sumDelta += d;
                    if (d > 0) larger++; else if (d < 0) smaller++;

                    if (differ <= 10)
                    {
                        char line2[160];
                        std::snprintf(line2, sizeof(line2),
                                      "  room %3zu: %6zu -> %6zu bytes (%+lld), %lld bytes different "
                                      "from +%lld\n", i, la_, lb_, d, v.different, v.firstAt);
                        first += line2;
                    }
                }

                std::printf("%s", first.c_str());
                if (differ > 10) std::printf("  ... and %d more\n", differ - 10);
                std::printf("  %d of %zu rooms changed (%d larger, %d smaller, "
                            "together %+lld bytes)\n", differ, rn, larger, smaller, sumDelta);
            }
        }

        // Meshes: count, block size and a mesh-by-mesh comparison with own
        // boundaries. A mesh carries its texture references in the faces -
        // whoever rearranges textures changes something exactly here
        // without touching the geometry.
        std::printf("\n=== MESHES ===\n");
        {
            size_t mdA = 0, mdB = 0;
            int32_t mnA = 0, mnB = 0;
            la.debugMeshDataBlock(mdA, mnA);
            lb.debugMeshDataBlock(mdB, mnB);

            std::printf("  %-22s %-23s %-23s\n", "", "A", "B");
            std::printf("  %-22s %-23d %-23d %s\n", "mesh pointers",
                        la.debugMeshPtrCount(), lb.debugMeshPtrCount(),
                        la.debugMeshPtrCount() == lb.debugMeshPtrCount() ? "" : "  DEVIATION");
            std::printf("  %-22s %-23d %-23d %s\n", "mesh data (words)", mnA, mnB,
                        mnA == mnB ? "" : "  DEVIATION");

            const int mn = la.debugMeshPtrCount() < lb.debugMeshPtrCount()
                         ? la.debugMeshPtrCount() : lb.debugMeshPtrCount();
            int differ = 0, sameLength = 0;
            long long delta = 0;
            std::string list;

            for (int i = 0; i < mn; ++i)
            {
                int va = 0, na = 0, q4a = 0, q3a = 0, ba = 0;
                int vb = 0, nb = 0, q4b = 0, q3b = 0, bb = 0;
                const bool wa = la.debugMeshWalk(i, va, na, q4a, q3a, ba);
                const bool wb = lb.debugMeshWalk(i, vb, nb, q4b, q3b, bb);
                if (!wa || !wb) { differ++; continue; }

                size_t aa = 0, ab = 0;
                la.debugMeshAt(i, aa);
                lb.debugMeshAt(i, ab);

                const Match v = compare(a, aa, static_cast<size_t>(ba),
                                        b, ab, static_cast<size_t>(bb));
                if (v.sameLength && v.different == 0) continue;

                differ++;
                delta += bb - ba;
                if (ba == bb) sameLength++;

                if (differ <= 10)
                {
                    char z[200];
                    std::snprintf(z, sizeof(z),
                                  "  mesh %4d: %5d -> %5d bytes, verts %d/%d normals %d/%d "
                                  "quads %d/%d triangles %d/%d\n",
                                  i, ba, bb, va, vb, na, nb, q4a, q4b, q3a, q3b);
                    list += z;
                }
            }

            std::printf("%s", list.c_str());
            if (differ > 10) std::printf("  ... and %d more\n", differ - 10);
            std::printf("  %d of %d meshes changed, %d of them at the SAME size "
                        "(together %+lld bytes)\n", differ, mn, sameLength, delta);

            // Which object do the changed meshes belong to? A moveable
            // occupies meshIndex .. meshIndex+nmeshes-1 of the pointer list.
            if (differ > 0 && differ <= 10)
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
                    const Match v = compare(a, aa, (size_t)ba, b, ab, (size_t)bb);
                    if (v.sameLength && v.different == 0) continue;

                    std::string who;
                    for (int s : la.moveableSlots())
                    {
                        Tr5Level::MoveableInfo mi;
                        if (!la.moveableInfo(s, mi)) continue;
                        if (i >= mi.meshIndex && i < mi.meshIndex + mi.nmeshes)
                        {
                            who = "slot " + std::to_string(s) + ", part " +
                                  std::to_string(i - mi.meshIndex);
                            break;
                        }
                    }
                    std::printf("  mesh %4d belongs to: %s\n", i,
                                who.empty() ? "no moveable (static or unused)" : who.c_str());
                }
            }

            // Where did the missing bytes go? If every single mesh stayed
            // the same size but the block shrank, the original carried
            // ballast: data no pointer points at.
            auto occupied = [](const Tr5Level& L, long long& sum, int& distinct,
                             long long& highestEnd)
            {
                std::vector<size_t> seen;
                sum = 0; highestEnd = 0;
                size_t base = 0; int32_t words = 0;
                L.debugMeshDataBlock(base, words);

                for (int i = 0; i < L.debugMeshPtrCount(); ++i)
                {
                    size_t at = 0;
                    int v = 0, n = 0, q4 = 0, q3 = 0, by = 0;
                    if (!L.debugMeshAt(i, at) || !L.debugMeshWalk(i, v, n, q4, q3, by)) continue;

                    bool fresh = true;
                    for (size_t g : seen) if (g == at) { fresh = false; break; }
                    if (!fresh) continue;

                    seen.push_back(at);
                    sum += by;
                    const long long end = (long long)(at - base) + by;
                    if (end > highestEnd) highestEnd = end;
                }
                distinct = (int)seen.size();
            };

            long long sA = 0, sB = 0, eA2 = 0, eB2 = 0;
            int dA = 0, dB = 0;
            occupied(la, sA, dA, eA2);
            occupied(lb, sB, dB, eB2);

            std::printf("\n  %-26s %-16s %-16s\n", "", "A", "B");
            std::printf("  %-26s %-16d %-16d\n", "distinct meshes", dA, dB);
            std::printf("  %-26s %-16lld %-16lld\n", "occupied bytes of those", sA, sB);
            std::printf("  %-26s %-16lld %-16lld\n", "highest mesh end", eA2, eB2);
            std::printf("  %-26s %-16lld %-16lld\n", "block size in bytes",
                        (long long)mnA * 2, (long long)mnB * 2);
            std::printf("  %-26s %-16lld %-16lld\n", "of which no pointer occupies",
                        (long long)mnA * 2 - sA, (long long)mnB * 2 - sB);
        }

        // Which object slots came in or disappeared?
        const std::vector<int> sa = la.moveableSlots();
        const std::vector<int> sb = lb.moveableSlots();
        auto contains = [](const std::vector<int>& v, int s)
        { for (int x : v) if (x == s) return true; return false; };

        std::string onlyA, onlyB;
        for (int s : sa) if (!contains(sb, s)) { onlyA += " " + std::to_string(s); }
        for (int s : sb) if (!contains(sa, s)) { onlyB += " " + std::to_string(s); }
        if (!onlyA.empty()) std::printf("  only in A:%s\n", onlyA.c_str());
        if (!onlyB.empty()) std::printf("  only in B:%s\n", onlyB.c_str());
    }

    return 0;
}

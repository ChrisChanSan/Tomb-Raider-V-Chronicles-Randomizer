// FixCheck - applies LevelFix to a level and saves the result, so the added
// triggers can be measured with SectorCheck instead of believed.
//
// Usage: FixCheck <in.trc> <levelName> <out.trc>
//
// For a TriggerOneShot entry it also checks the result itself:
//   - FOUND: every trigger the entry reaches, with its sector, its item and
//     its setup word before and after;
//   - SET: the saved file read back, room by room - every trigger of the
//     entry's kind in its room that names an item of its slot carries the
//     one-shot bit;
// For a TriggerTimer entry likewise:
//   - FOUND: the trigger on the entry's sector, with its chain and its setup
//     word before and after;
//   - SET: the saved file read back - the first trigger on that sector is
//     of the entry's kind and carries the new timer byte.
// And once per level for both kinds:
//   - BYTES: the saved file against the source, byte by byte - the only
//     bytes that differ are the one-shot entries' high bytes and the timer
//     entries' low bytes of those setup words. Skipped when an entry of
//     another kind for the same level changes the file as well.
// A failed check ends with exit code 2.
#include <cstdio>
#include <string>
#include <vector>
#include "core/Tr5Level.h"
#include "core/LevelFix.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

static bool sameName(const std::string& a, const char* b)
{
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = char(x + 32);
        if (y >= 'A' && y <= 'Z') y = char(y + 32);
        if (x != y) return false;
    }
    return i == a.size() && b[i] == 0;
}

static bool readFile(const char* path, std::vector<unsigned char>& out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const size_t got =
        out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

// Entries of the kinds that rebuild chains or portal lists for this level -
// any of them changes the file beyond the setup words, so the byte
// comparison cannot hold then.
static int otherEntries(const std::string& name)
{
    using namespace LevelFix;
    int n = 0;
    for (const auto& k : kTriggerCopies) n += sameName(name, k.level);
    for (const auto& k : kTriggerMoves) n += sameName(name, k.level);
    for (const auto& k : kTriggerObjectRemoves) n += sameName(name, k.level);
    for (const auto& k : kTriggerObjectReplaces) n += sameName(name, k.level);
    for (const auto& k : kTriggerRewrites) n += sameName(name, k.level);
    for (const auto& k : kSightWindows) n += sameName(name, k.level);
    return n;
}

// The SET check on the saved file, walked here and not through
// setTriggerOneShot: each trigger of this kind in this room that names an
// item of this slot, and how many of them lack the bit.
static void walkRoom(const Tr5Level& lvl, int room, int kind, int slot,
                     int& named, int& missing)
{
    named = missing = 0;
    const Tr5Room& r = lvl.rooms()[(size_t)room];
    std::vector<Tr5FloorEntry> chain;

    for (int sx = 0; sx < r.numXSectors; ++sx)
        for (int sz = 0; sz < r.numZSectors; ++sz)
        {
            const int start = lvl.sectorIndex(room, sx, sz);
            if (start <= 0 || !lvl.readChain(start, chain)) continue;

            for (const Tr5FloorEntry& e : chain)
            {
                if (e.function != 4 || e.kind != kind) continue;

                bool hit = false;
                for (size_t k = Tr5Floor::hasRefWord(e.kind) ? 3 : 2;
                     k < e.words.size(); ++k)
                {
                    const int act = Tr5Floor::cmdType(e.words[k]);
                    if (act == 1 || act == 12) { ++k; continue; }
                    Tr5Item it;
                    if (act == 0 && lvl.getItem(e.words[k] & 0x3FF, it) &&
                        it.objectNumber == slot)
                        hit = true;
                }
                if (!hit) continue;

                ++named;
                const bool set = (e.words[1] & 0x0100) != 0;
                if (!set) ++missing;
                std::printf("  SET    room %d (%d,%d) setup %04X one-shot %s\n",
                            room, sx, sz, e.words[1], set ? "yes" : "NO");
            }
        }
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("Usage: FixCheck <in.trc> <levelName> <out.trc>\n"); return 1; }

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("load: %s\n", err.c_str()); return 1; }

    std::vector<std::string> warn;
    const int n = LevelFix::apply(lvl, argv[2], warn);
    std::printf("LevelFix applied %d fix(es) to %s\n", n, argv[2]);
    for (const std::string& w : warn) std::printf("  WARN %s\n", w.c_str());

    if (!lvl.save(toW(argv[3]), err)) { std::printf("save: %s\n", err.c_str()); return 1; }
    std::printf("written: %s\n", argv[3]);

    bool ok = warn.empty();
    const std::string name = argv[2];

    // Every byte the in-place entries of this level may change, with its
    // value in the source and in the result.
    struct Expect { size_t at; unsigned char from, to; };
    std::vector<Expect> expect;
    bool inPlace = false;

    for (const LevelFix::TriggerOneShot& k : LevelFix::kTriggerOneShots)
    {
        if (!sameName(name, k.level)) continue;
        inPlace = true;

        // The FOUND lines: the same search on a fresh copy of the source,
        // for the sectors and the file positions of the words.
        Tr5Level src;
        std::vector<Tr5OneShotHit> hits;
        if (!src.load(toW(argv[1]), err) ||
            !src.setTriggerOneShot(k.room, k.kind, k.slot, hits, err))
        {
            std::printf("  FAIL   one-shot room %d: %s\n", k.room, err.c_str());
            ok = false;
            continue;
        }
        for (const Tr5OneShotHit& h : hits)
            std::printf("  FOUND  room %d (%d,%d) item %d setup at %zu: "
                        "%04X -> %04X\n", k.room, h.sx, h.sz, h.item,
                        h.fileAt, h.before, h.after);

        // The SET line: the saved file, read back.
        Tr5Level out;
        int named = 0, missing = 0;
        if (!out.load(toW(argv[3]), err))
        {
            std::printf("  FAIL   reload: %s\n", err.c_str());
            ok = false;
            continue;
        }
        walkRoom(out, k.room, k.kind, k.slot, named, missing);
        if (named != (int)hits.size() || missing)
        {
            std::printf("  FAIL   %d trigger(s) in the saved file, %d without "
                        "the bit\n", named, missing);
            ok = false;
        }

        // The bit sits in the setup word's high byte.
        for (const Tr5OneShotHit& h : hits)
            if ((h.before >> 8) != (h.after >> 8))
                expect.push_back({ h.fileAt + 1,
                                   (unsigned char)(h.before >> 8),
                                   (unsigned char)(h.after >> 8) });
    }

    for (const LevelFix::TriggerTimer& k : LevelFix::kTriggerTimers)
    {
        if (!sameName(name, k.level)) continue;
        inPlace = true;

        // The FOUND line: the same change on a fresh copy of the source,
        // for the chain and the file position of the word.
        Tr5Level src;
        Tr5TimerHit hit;
        if (!src.load(toW(argv[1]), err) ||
            !src.setTriggerTimer(k.room, k.sx, k.sz, k.kind, k.fromTimer,
                                 k.toTimer, hit, err))
        {
            std::printf("  FAIL   timer room %d (%d,%d): %s\n", k.room, k.sx,
                        k.sz, err.c_str());
            ok = false;
            continue;
        }
        std::printf("  FOUND  room %d (%d,%d) kind %d chain %d setup at %zu: "
                    "%04X -> %04X\n", k.room, k.sx, k.sz, k.kind, hit.chain,
                    hit.fileAt, hit.before, hit.after);

        // The SET line: the saved file, read back.
        Tr5Level out;
        std::vector<Tr5FloorEntry> chain;
        if (!out.load(toW(argv[3]), err))
        {
            std::printf("  FAIL   reload: %s\n", err.c_str());
            ok = false;
            continue;
        }
        const int start = out.sectorIndex(k.room, k.sx, k.sz);
        const Tr5FloorEntry* trig = nullptr;
        if (start > 0 && out.readChain(start, chain))
            for (const Tr5FloorEntry& e : chain)
                if (e.function == 4) { trig = &e; break; }
        if (!trig)
        {
            std::printf("  FAIL   no trigger on room %d (%d,%d) in the saved "
                        "file\n", k.room, k.sx, k.sz);
            ok = false;
            continue;
        }
        const bool set = trig->kind == k.kind &&
                         (trig->words[1] & 0xFF) == (k.toTimer & 0xFF);
        std::printf("  SET    room %d (%d,%d) kind %d setup %04X timer %d "
                    "%s\n", k.room, k.sx, k.sz, trig->kind, trig->words[1],
                    trig->words[1] & 0xFF, set ? "yes" : "NO");
        if (!set) ok = false;

        // The timer is the setup word's low byte.
        if ((hit.before & 0xFF) != (hit.after & 0xFF))
            expect.push_back({ hit.fileAt,
                               (unsigned char)(hit.before & 0xFF),
                               (unsigned char)(hit.after & 0xFF) });
    }

    // The BYTES line: source against result, once for all in-place entries.
    if (inPlace && otherEntries(name))
        std::printf("  BYTES  skipped - %d other entr(ies) of this level "
                    "change the file\n", otherEntries(name));
    else if (inPlace)
    {
        std::vector<unsigned char> a, b;
        if (!readFile(argv[1], a) || !readFile(argv[3], b) ||
            a.size() != b.size())
        {
            std::printf("  FAIL   source and result differ in size\n");
            ok = false;
        }
        else
        {
            size_t diff = 0, expected = 0, other = 0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (a[i] == b[i]) continue;
                ++diff;
                bool mine = false;
                for (const Expect& x : expect)
                    if (i == x.at && a[i] == x.from && b[i] == x.to)
                        mine = true;
                if (mine) ++expected;
                else
                {
                    ++other;
                    std::printf("  BYTES  unexpected difference at %zu: "
                                "%02X -> %02X\n", i, a[i], b[i]);
                }
            }
            std::printf("  BYTES  %zu of %zu differ: %zu setup word(s), %zu "
                        "other\n", diff, a.size(), expected, other);
            if (other || expected != expect.size()) ok = false;
        }
    }

    std::printf("%s\n", ok ? "HOLDS" : "FAILS");
    return ok ? 0 : 2;
}

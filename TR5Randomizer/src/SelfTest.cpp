/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
//----------------------------------------------------------------------------
// Self-test of the core. Runs without a GUI and without level files, but
// checks against real zone exports if a path is passed.
//
// Usage:  TR5RandoSelfTest.exe [<path to a zone JSON>]
//
// Purpose: secure the two properties everything else builds on - that a seed
// is reproducible and that our own exports are read without errors. Both
// would otherwise only show once a user reports a broken level.
//----------------------------------------------------------------------------
#include "core/DataFolder.h"
#include "core/Json.h"
#include "core/Rng.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

static int g_failed = 0;

static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "ok " : "FAIL", what);
    if (!ok) ++g_failed;
}

static void testRng()
{
    std::printf("RNG\n");

    // The same seed must yield the same sequence.
    Rng a(12345), b(12345);
    bool same = true;
    for (int i = 0; i < 1000; ++i) if (a.next() != b.next()) { same = false; break; }
    check(same, "same seed -> same sequence");

    Rng c(12345), d(12346);
    bool diff = false;
    for (int i = 0; i < 8; ++i) if (c.next() != d.next()) { diff = true; break; }
    check(diff, "different seed -> different sequence");

    // Shuffling must be reproducible - this is the point where std::shuffle
    // would have failed.
    std::vector<int> v1, v2;
    for (int i = 0; i < 200; ++i) { v1.push_back(i); v2.push_back(i); }
    Rng e(999), f(999);
    e.shuffle(v1);
    f.shuffle(v2);
    check(v1 == v2, "same seed -> same shuffle");

    bool moved = false;
    for (int i = 0; i < 200; ++i) if (v1[i] != i) { moved = true; break; }
    check(moved, "shuffle actually changes the order");

    // Shuffling must lose or duplicate nothing.
    std::vector<bool> seen(200, false);
    bool perm = true;
    for (int x : v1) { if (x < 0 || x >= 200 || seen[x]) { perm = false; break; } seen[x] = true; }
    check(perm, "shuffle is a true permutation");

    // below() must stay in range and hit all values.
    Rng g(7);
    std::vector<int> hits(6, 0);
    for (int i = 0; i < 60000; ++i) hits[static_cast<size_t>(g.below(6))]++;
    bool inRange = true, allHit = true;
    for (int h : hits) { if (h == 0) allHit = false; }
    // Rough uniformity check: no value may deviate by more than 20 percent
    // from the expectation. Reliably catches a broken modulo.
    for (int h : hits) { if (h < 8000 || h > 12000) inRange = false; }
    check(allHit, "below(6) hits all six values");
    check(inRange, "below(6) is sufficiently uniform");
    check(Rng(1).below(0) == 0, "below(0) yields 0 instead of crashing");

    // Seed text round trip.
    bool roundTrip = true;
    const uint64_t probes[] = { 0ull, 1ull, 42ull, 123456789ull, 0xFFFFFFFFFFFFFFFFull };
    for (uint64_t p : probes)
    {
        std::string s = SeedText::format(p);
        if (SeedText::parse(s) != p) { roundTrip = false; break; }
    }
    check(roundTrip, "seed text converts back losslessly");

    check(SeedText::parse("Weihnachten") == SeedText::parse("Weihnachten"),
          "free text yields the same seed reproducibly");

    std::string s42 = SeedText::format(42);
    check(SeedText::parse(s42) == SeedText::parse(s42 + "  "),
          "whitespace in the seed text does not interfere");
}

static void testJson()
{
    std::printf("JSON\n");

    JsonValue v;
    std::string err;

    check(Json::parseString("{\"a\":1,\"b\":[1,2,3],\"c\":\"x\",\"d\":true,\"e\":null}", v, err),
          "simple object is read");
    check(v["a"].asInt() == 1, "number read");
    check(v["b"].size() == 3 && v["b"][2].asInt() == 3, "array read");
    check(v["c"].asString() == "x", "string read");
    check(v["d"].asBool(), "boolean read");
    check(v["e"].isNull(), "null read");
    check(v["doesnotexist"].isNull(), "missing key yields null instead of an error");
    check(v["b"][99].isNull(), "access outside the array yields null");

    check(Json::parseString("\xEF\xBB\xBF{\"a\":1}", v, err), "BOM is skipped");

    check(!Json::parseString("{\"a\":1", v, err), "incomplete object is rejected");
    check(!err.empty(), "error message is set");
    check(!Json::parseString("{\"a\":1} trailing", v, err), "trailing content is rejected");

    // Negative numbers and floating point - level coordinates are regularly
    // negative.
    check(Json::parseString("{\"y\":-11982,\"f\":1.5e2}", v, err), "negative and exponential numbers");
    check(v["y"].asInt() == -11982, "negative number correct");
    check(static_cast<int>(v["f"].asDouble()) == 150, "exponential notation correct");

    // Write and read back.
    JsonValue o = JsonValue::makeObject();
    JsonValue arr = JsonValue::makeArray();
    JsonValue e1 = JsonValue::makeObject();
    e1.set("typeId", JsonValue::makeInt(174));
    e1.set("room", JsonValue::makeInt(166));
    e1.set("x", JsonValue::makeInt(-90624));
    arr.push(e1);
    o.set("itemAdds", arr);

    std::string text = o.dump();
    check(text.find("\"typeId\":174") != std::string::npos, "integer is written without decimals");
    check(text.find("-90624") != std::string::npos, "negative coordinate is written");

    JsonValue back;
    check(Json::parseString(text, back, err), "self-written JSON is readable again");
    check(back["itemAdds"][0]["room"].asInt() == 166, "value equal after round trip");
    check(o.dump() == back.dump(), "output is stable (byte-identical)");
}

static void testAgainstRealExport(const char* path)
{
    std::printf("Real zone export: %s\n", path);

    std::wstring wpath;
    for (const char* p = path; *p; ++p) wpath.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));

    JsonValue z;
    std::string err;
    if (!Json::parseFile(wpath, z, err))
    {
        std::printf("  [FAIL] cannot be read: %s\n", err.c_str());
        ++g_failed;
        return;
    }
    check(true, "file read");

    const JsonValue& sectors = z["sectors"];
    const JsonValue& cons = z["keyConstraints"];
    std::printf("  level=%s  sectors=%u  constraints=%u  maxZone=%lld\n",
                z["level"].asString("?").c_str(),
                static_cast<unsigned>(sectors.size()),
                static_cast<unsigned>(cons.size()),
                static_cast<long long>(z["maxZone"].asInt(-1)));

    check(sectors.size() > 0, "sectors present");

    // Every sector must carry the fields placement accesses.
    bool fieldsOk = true;
    for (size_t i = 0; i < sectors.size() && fieldsOk; ++i)
    {
        const JsonValue& s = sectors[i];
        if (!s.has("room") || !s.has("sectorX") || !s.has("sectorZ") ||
            !s.has("zone") || !s.has("floorY"))
            fieldsOk = false;
    }
    check(fieldsOk, "every sector has room/sectorX/sectorZ/zone/floorY");

    for (size_t i = 0; i < cons.size(); ++i)
    {
        const JsonValue& c = cons[i];
        std::printf("    typeId %lld  maxZone %lld  allowRooms %u  excludeRooms %u\n",
                    static_cast<long long>(c["typeId"].asInt()),
                    static_cast<long long>(c["maxZone"].asInt()),
                    static_cast<unsigned>(c["allowRooms"].size()),
                    static_cast<unsigned>(c["excludeRooms"].size()));
    }
}

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    return w;
}

static void testDataFolder(const char* csv, const char* dataDir)
{
    std::printf("Data folder\n");

    DataFolder df;
    std::string err;
    check(df.loadChecksums(toW(csv), err), "checksum list read");
    if (!err.empty()) { std::printf("       %s\n", err.c_str()); return; }
    std::printf("  %u known levels\n", static_cast<unsigned>(df.knownLevelCount()));
    check(df.knownLevelCount() == 16, "16 levels in the list");

    if (!dataDir) return;

    auto st = df.inspect(toW(dataDir));
    int vanilla = 0, modified = 0, missing = 0;
    for (const auto& s : st)
    {
        if (s.state == LevelState::Vanilla)  ++vanilla;
        if (s.state == LevelState::Modified) { ++modified; std::printf("       deviating: %s\n", s.name.c_str()); }
        if (s.state == LevelState::Missing)  { ++missing;  std::printf("       missing:   %s\n", s.name.c_str()); }
    }
    std::printf("  vanilla=%d  deviating=%d  missing=%d\n", vanilla, modified, missing);

    std::string reason;
    bool usable = DataFolder::isUsableSource(st, reason);
    check(usable, "folder is usable as a source");
    if (!usable) std::printf("       %s\n", reason.c_str());

    // A made-up file must be recognized as "missing", not as vanilla.
    auto stBad = df.inspect(toW("C:\\doesnotexist_randompath_12345"));
    bool allMissing = true;
    for (const auto& s : stBad) if (s.state != LevelState::Missing) allMissing = false;
    check(allMissing, "non-existing folder yields 'missing' throughout");
    std::string r2;
    check(!DataFolder::isUsableSource(stBad, r2), "empty folder is rejected as a source");
}

static void testCopyAndRestore(const char* csv, const char* dataDir, const char* targetDir)
{
    std::printf("Copy and restore\n");

    DataFolder df;
    std::string err;
    if (!df.loadChecksums(toW(csv), err)) { std::printf("  [FAIL] %s\n", err.c_str()); ++g_failed; return; }

    int copied = 0;
    bool ok = df.copyLevels(toW(dataDir), toW(targetDir), err, &copied);
    check(ok, "levels copied into the target folder");
    if (!ok) { std::printf("       %s\n", err.c_str()); return; }
    std::printf("  %d files copied\n", copied);

    // The copy must be byte-identical - otherwise even a seed's source
    // material would already differ from what was checked.
    auto st = df.inspect(toW(targetDir));
    std::string reason;
    check(DataFolder::isUsableSource(st, reason), "copy is byte-identical with vanilla");
    if (!reason.empty()) std::printf("       %s\n", reason.c_str());

    // Deliberately corrupt one file; afterwards the check must trip and the
    // restore must repair it. That is exactly the flow "randomized level ->
    // restore to default".
    const std::wstring victim = PathUtil::join(toW(targetDir), L"del.trc");
    FILE* f = nullptr;
    if (_wfopen_s(&f, victim.c_str(), L"r+b") == 0 && f)
    {
        std::fseek(f, 64, SEEK_SET);
        unsigned char junk = 0xAB;
        std::fwrite(&junk, 1, 1, f);
        std::fclose(f);

        auto st2 = df.inspect(toW(targetDir));
        std::string r2;
        check(!DataFolder::isUsableSource(st2, r2), "changed file is detected");

        int again = 0;
        check(df.copyLevels(toW(dataDir), toW(targetDir), err, &again), "restore executed");
        auto st3 = df.inspect(toW(targetDir));
        std::string r3;
        check(DataFolder::isUsableSource(st3, r3), "vanilla again after the restore");
    }
    else
    {
        std::printf("  [FAIL] test file not writable\n");
        ++g_failed;
    }
}

int main(int argc, char** argv)
{
    std::printf("TR5Randomizer - core self-test\n\n");
    testRng();
    std::printf("\n");
    testJson();
    if (argc > 1)
    {
        std::printf("\n");
        testAgainstRealExport(argv[1]);
    }
    if (argc > 2)
    {
        std::printf("\n");
        testDataFolder(argv[2], argc > 3 ? argv[3] : nullptr);
    }
    if (argc > 4)
    {
        std::printf("\n");
        testCopyAndRestore(argv[2], argv[3], argv[4]);
    }
    std::printf("\n%s (%d errors)\n", g_failed ? "FAILED" : "All green", g_failed);
    return g_failed ? 1 : 0;
}

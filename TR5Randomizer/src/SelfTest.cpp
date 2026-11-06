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
// Purpose: secure the properties everything else builds on - that a seed
// is reproducible, that our own exports are read without errors, and that
// the level's texture blocks are unpacked and packed right. Each would
// otherwise only show once a player reports a broken level.
//----------------------------------------------------------------------------
#include "core/BuildId.h"
#include "core/DataFile.h"
#include "core/DataFolder.h"
#include "core/Inflate.h"
#include "core/Json.h"
#include "core/Rng.h"
#include "core/ShareCode.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
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

    check(SeedText::parse("Christmas") == SeedText::parse("Christmas"),
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

//----------------------------------------------------------------------------
// The deflate reader and the stored writer (core/Inflate.h).
//
// The three streams below were written once by zlib 1.2.12 (compress2,
// the copy the engine carried until #308) and pasted here, so the
// reader is checked against another implementation's output, not against
// its own writer: level 0 gives a stored block, level 9 on the short text a
// fixed-Huffman block, level 9 on the longer text a dynamic-Huffman block.
//----------------------------------------------------------------------------
namespace
{
    const char kShortText[] = "TR5 TR5 TR5 Randomizer TR5 Randomizer TR5 TR5 Randomizer";

    const uint8_t kStoredZ[] = {
        0x78, 0x01, 0x01, 0x38, 0x00, 0xC7, 0xFF, 0x54, 0x52, 0x35, 0x20, 0x54,
        0x52, 0x35, 0x20, 0x54, 0x52, 0x35, 0x20, 0x52, 0x61, 0x6E, 0x64, 0x6F,
        0x6D, 0x69, 0x7A, 0x65, 0x72, 0x20, 0x54, 0x52, 0x35, 0x20, 0x52, 0x61,
        0x6E, 0x64, 0x6F, 0x6D, 0x69, 0x7A, 0x65, 0x72, 0x20, 0x54, 0x52, 0x35,
        0x20, 0x54, 0x52, 0x35, 0x20, 0x52, 0x61, 0x6E, 0x64, 0x6F, 0x6D, 0x69,
        0x7A, 0x65, 0x72, 0xF3, 0xFB, 0x12, 0x74,
    };
    const uint8_t kFixedZ[] = {
        0x78, 0xDA, 0x0B, 0x09, 0x32, 0x55, 0x08, 0x81, 0xE2, 0xA0, 0xC4, 0xBC,
        0x94, 0xFC, 0xDC, 0xCC, 0xAA, 0xD4, 0x22, 0x6C, 0x5C, 0x54, 0x21, 0x00,
        0xF3, 0xFB, 0x12, 0x74,
    };
    // 120 words, 645 bytes: see longText().
    const uint8_t kDynamicZ[] = {
        0x78, 0xDA, 0x6D, 0x52, 0xC9, 0x0A, 0xC2, 0x30, 0x10, 0xFD, 0x95, 0xFC,
        0x8B, 0xA7, 0x42, 0x4F, 0x12, 0xBC, 0x0F, 0x34, 0xA0, 0x12, 0x2B, 0x44,
        0xF1, 0xFB, 0x65, 0xB6, 0xCC, 0x4B, 0x9B, 0xC3, 0x4C, 0x97, 0xB7, 0xCC,
        0x92, 0xE4, 0x46, 0x4F, 0xDA, 0xD3, 0xED, 0xB2, 0xA4, 0xAC, 0xAF, 0x2B,
        0x35, 0x4A, 0xD7, 0xF7, 0xAB, 0xA4, 0xA5, 0x3D, 0x3E, 0xFA, 0xC9, 0x70,
        0x2D, 0xBF, 0x52, 0x2D, 0x0B, 0x22, 0x1C, 0x46, 0x84, 0x12, 0x64, 0xA5,
        0x30, 0x00, 0xAE, 0xF6, 0x30, 0xDF, 0x52, 0x69, 0xDF, 0x40, 0xF7, 0xBD,
        0xC7, 0x5F, 0x95, 0xF7, 0xB2, 0x82, 0xF7, 0x2A, 0x92, 0xD0, 0x4B, 0x12,
        0xCB, 0x39, 0x0C, 0x70, 0x27, 0x28, 0xDF, 0x5D, 0x3C, 0x98, 0x0E, 0xDD,
        0xFA, 0x58, 0x2A, 0x8C, 0xF1, 0x62, 0x96, 0x58, 0x4B, 0x8E, 0x95, 0x75,
        0xDB, 0x3E, 0x85, 0x81, 0xAA, 0xF3, 0xBE, 0x4E, 0xDA, 0x23, 0x7D, 0x50,
        0x09, 0x7B, 0x18, 0x67, 0xEC, 0xE6, 0x3C, 0xF7, 0xBC, 0xBA, 0xF6, 0x85,
        0x4B, 0x85, 0xC3, 0x9B, 0x01, 0xBE, 0x17, 0x0F, 0x34, 0x80, 0x5D, 0x0D,
        0x7A, 0xBC, 0x34, 0x02, 0xCC, 0x0E, 0xFA, 0x78, 0x08, 0xB1, 0x0F, 0x75,
        0xFC, 0x03, 0xD8, 0xAA, 0xDA, 0xCA,
    };

    // The dynamic stream's text: 120 words drawn from eight by the
    // generator x = x * 1103515245 + 12345 (32 bit, from 7), word
    // (x >> 16) % 8, each followed by its space.
    std::vector<uint8_t> longText()
    {
        static const char* const words[] = { "the ", "level ", "Lara ", "Rome ",
                                             "Trajan ", "Iris ", "VCI ", "Ireland " };
        std::vector<uint8_t> t;
        uint32_t x = 7;
        for (int i = 0; i < 120; ++i)
        {
            x = x * 1103515245u + 12345u;
            const char* w = words[(x >> 16) % 8];
            t.insert(t.end(), w, w + std::strlen(w));
        }
        return t;
    }

    bool reads(const uint8_t* s, size_t n, const std::vector<uint8_t>& want)
    {
        std::vector<uint8_t> out;
        std::string err;
        return Inflate::zlibExact(s, n, want.size(), out, err) && out == want;
    }

    bool refused(std::vector<uint8_t> s, size_t room)
    {
        std::vector<uint8_t> out(room + 1);
        size_t got = 0;
        return Inflate::zlib(s.data(), s.size(), out.data(), room, got) != nullptr;
    }
}

static void testInflate()
{
    std::printf("Deflate reader and stored writer\n");

    const uint8_t wiki[] = { 'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a' };
    check(Inflate::adler32(wiki, 9) == 0x11E60398u, "Adler-32 of \"Wikipedia\" is 11E60398");
    check(Inflate::adler32(nullptr, 0) == 1u, "Adler-32 of nothing is 1");
    const uint8_t digits[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    check(Inflate::crc32(digits, 9) == 0xCBF43926u, "CRC-32 of \"123456789\" is CBF43926");

    const std::vector<uint8_t> shortText(kShortText, kShortText + std::strlen(kShortText));
    const std::vector<uint8_t> text = longText();
    check(text.size() == 645, "dynamic vector's text rebuilt (645 bytes)");
    check(reads(kStoredZ, sizeof(kStoredZ), shortText), "zlib's stored stream read");
    check(reads(kFixedZ, sizeof(kFixedZ), shortText), "zlib's fixed-Huffman stream read");
    check(reads(kDynamicZ, sizeof(kDynamicZ), text), "zlib's dynamic-Huffman stream read");

    std::vector<uint8_t> mine;
    Inflate::stored(shortText.data(), shortText.size(), mine);
    check(mine == std::vector<uint8_t>(kStoredZ, kStoredZ + sizeof(kStoredZ)),
          "stored writer gives zlib's own stored bytes");

    // Three blocks and a short last one: 65535 + 65535 + 1.
    std::vector<uint8_t> raw(131071);
    uint32_t x = 1;
    for (uint8_t& b : raw) { x = x * 1103515245u + 12345u; b = static_cast<uint8_t>(x >> 24); }
    std::vector<uint8_t> packed;
    Inflate::stored(raw.data(), raw.size(), packed);
    check(packed.size() == Inflate::storedSize(raw.size()) && packed.size() == 131092,
          "stored stream of 131071 bytes is 131092 bytes (3 blocks)");
    check(reads(packed.data(), packed.size(), raw), "stored round trip, 131071 bytes");

    std::vector<uint8_t> dyn(kDynamicZ, kDynamicZ + sizeof(kDynamicZ));
    std::vector<uint8_t> bad = dyn;
    bad.back() ^= 1;
    check(refused(bad, text.size()), "a wrong Adler-32 is refused");
    check(refused(std::vector<uint8_t>(dyn.begin(), dyn.end() - 5), text.size()),
          "a truncated stream is refused");
    bad = dyn;
    bad[1] ^= 1;
    check(refused(bad, text.size()), "a header failing its check bits is refused");
    check(refused(dyn, text.size() - 1), "more output than the room is refused");
}

//----------------------------------------------------------------------------
// The data files are files only (core/DataFile.h): nothing comes from inside
// the EXE, and a missing one is named with its folder.
//----------------------------------------------------------------------------
static void testDataRule(const char* zonePath)
{
    std::printf("Data files\n");

    const std::wstring dir = L"C:\\doesnotexist_randompath_12345";
    std::string text, err;
    const bool got = DataFile::read(dir, "zones_rich2.json", text, err);
    check(!got && text.empty(), "zones_rich2.json in a missing folder is not found");
    check(err.find("zones_rich2.json") != std::string::npos &&
          err.find("doesnotexist_randompath_12345") != std::string::npos,
          "the message names the file and the folder");
    check(!DataFile::read(dir, "VanillaLevelChecksums.csv", text, err),
          "VanillaLevelChecksums.csv in a missing folder is not found");

    if (zonePath)
    {
        std::wstring w;
        for (const char* p = zonePath; *p; ++p) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
        check(DataFile::read(w, text, err) && !text.empty(), "a zone export on disk is read");
    }
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

// The share code (ShareCode.h): every travelling field comes back as it
// went in, and a code that is not exactly what make() wrote is refused.
static void testShareCode()
{
    std::printf("Share code\n");

    Config def;
    check(def.seedText.empty(), "the default Config has no seed text");
    std::string err;
    {
        Config back;
        back.optKeyItems = false;   // something apply must overwrite
        const bool ok = ShareCode::apply(ShareCode::make(def), back, err);
        check(ok && ShareCode::sameSettings(back, def), "round trip: default Config");
    }

    // Every switch on, every number at the top of its range.
    Config all;
    all.seedText = "MN6T3HSDR2QPF";
    all.optOtherPickups = all.optKeyItems = all.optEnemies = true;
    all.optEnemySwap = all.optOutfits = all.optOutfitsTr3 = true;
    all.secretRewards = all.optMusicSwitch = all.optMusicTrigger = true;
    all.optLevelSections = all.optWeather = true;
    all.balWeaponsRandom = all.balEnemyHealthRandom = all.balEnemyDamageRandom = true;
    all.enemyCount = 20; all.enemyPool = 2; all.itemAmount = 2;
    all.secrets = 3; all.optLevelOrder = 2;
    all.balWeapons = 200; all.balEnemyHealth = 200; all.balEnemyDamage = 200;
    all.secretAuthor = "ChrisChanSan";
    {
        Config back;
        const bool ok = ShareCode::apply(ShareCode::make(all), back, err);
        check(ok && ShareCode::sameSettings(back, all), "round trip: every switch on");
    }

    // Sets with several entries, an empty author, the sliders at the
    // bottom of their range.
    Config sets;
    sets.seedText = "Christmas";
    sets.enemiesDisabled = { 1, 47, 49, 127, 128, 460, 471, 473, 16000 };
    sets.outfitsDisabled = { "Lara_Andrea", "Lara_Joby", "tr3_catsuit" };
    sets.balWeapons = 0; sets.balEnemyHealth = 50; sets.balEnemyDamage = 50;
    sets.enemyCount = 1;
    {
        Config back;
        back.secretAuthor = "someone";
        const bool ok = ShareCode::apply(ShareCode::make(sets), back, err);
        check(ok && ShareCode::sameSettings(back, sets) && back.secretAuthor.empty(),
              "round trip: sets with several entries, empty author");
    }

    // 40 characters with spaces and umlauts (UTF-8, 44 bytes).
    Config text;
    text.seedText = "Gr\xC3\xBC\xC3\x9F" "e aus K\xC3\xB6ln und M\xC3\xBCnchen, Tag 12 von 3";
    check(text.seedText.size() == 44, "the umlaut text is 44 bytes");
    {
        Config back;
        const bool ok = ShareCode::apply(ShareCode::make(text), back, err);
        check(ok && back.seedText == text.seedText &&
              ShareCode::sameSettings(back, text),
              "round trip: seed text with spaces and umlauts");
    }

    // The fields that belong to one machine stay as they were.
    {
        Config mine;
        mine.sourceDir = L"D:\\Games\\TRC";
        mine.tr3Dir = L"D:\\Games\\TR3";
        mine.winW = 1234; mine.winH = 777; mine.germanUi = true;
        const bool ok = ShareCode::apply(ShareCode::make(all), mine, err);
        check(ok && mine.sourceDir == L"D:\\Games\\TRC" && mine.tr3Dir == L"D:\\Games\\TR3" &&
              mine.winW == 1234 && mine.winH == 777 && mine.germanUi,
              "folders, window and language are not carried");
    }

    const std::string code = ShareCode::make(all);

    // Lower case, blanks and missing dashes are read the same.
    {
        std::string lower;
        for (char ch : code)
            if (ch != '-')
                lower.push_back((ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch);
        Config back;
        const bool ok = ShareCode::apply("  " + lower + "\r\n", back, err);
        check(ok && ShareCode::sameSettings(back, all), "lower case without dashes accepted");
    }

    // A wrong check digit.
    {
        std::string bad = code;
        char& last = bad[bad.size() - 1];
        last = (last == 'Z') ? 'Y' : 'Z';
        Config back = def;
        const bool ok = ShareCode::apply(bad, back, err);
        check(!ok && ShareCode::sameSettings(back, def) &&
              err.find("does not check out") != std::string::npos,
              "wrong check digit refused, nothing applied");
    }

    // Every digit of the code, replaced by every other digit: refused.
    {
        int tried = 0, passed = 0;
        const char* digits = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        for (size_t i = 4; i < code.size(); ++i)
        {
            if (code[i] == '-') continue;
            for (const char* d = digits; *d; ++d)
            {
                if (*d == code[i]) continue;
                std::string bad = code;
                bad[i] = *d;
                Config back;
                ++tried;
                if (ShareCode::apply(bad, back, err)) ++passed;
            }
        }
        std::printf("  %d codes with one digit changed\n", tried);
        check(tried > 1000 && passed == 0, "every single changed digit refused");
    }

    // Two neighbouring digits swapped.
    {
        int tried = 0, passed = 0;
        std::string flat;
        for (char ch : code) if (ch != '-') flat.push_back(ch);
        for (size_t i = 3; i + 1 < flat.size(); ++i)
        {
            if (flat[i] == flat[i + 1]) continue;
            std::string bad = flat;
            std::swap(bad[i], bad[i + 1]);
            Config back;
            ++tried;
            if (ShareCode::apply(bad, back, err)) ++passed;
        }
        check(tried > 20 && passed == 0, "every swap of two neighbours refused");
    }

    // A code of another build: refused whole, with both build numbers.
    {
        const std::string other = ShareCode::makeForBuild(all, RANDO_BUILD_ID + 1);
        Config back = def;
        const bool ok = ShareCode::apply(other, back, err);
        char want[64];
        std::snprintf(want, sizeof(want), "from build 0.%d; this is 0.%d",
                      RANDO_BUILD_ID + 1, RANDO_BUILD_ID);
        check(!ok && ShareCode::sameSettings(back, def) &&
              err.find(want) != std::string::npos,
              "code of another build refused");
        std::printf("  %s\n", err.c_str());
    }

    // Truncated: the last group, or the last digit, missing.
    {
        const std::string cut1 = code.substr(0, code.rfind('-'));
        const std::string cut2 = code.substr(0, code.size() - 1);
        Config back;
        check(!ShareCode::apply(cut1, back, err) && !ShareCode::apply(cut2, back, err) &&
              !ShareCode::apply("TR5-A", back, err) && !ShareCode::apply("", back, err),
              "truncated code refused");
    }

    // Not a code at all.
    {
        Config back;
        check(!ShareCode::apply("MN6T3HSDR2QPF", back, err) &&
              !ShareCode::apply(code + "U", back, err),
              "seed text or a foreign character refused");
    }

    // The effective value is the GUI's formula, pinned: a change to it
    // changes every game, so it must show here.
    {
        Config pin;
        pin.seedText = "Christmas";
        pin.optEnemies = true; pin.enemyPool = 1; pin.optEnemySwap = true;
        pin.optOutfits = true; pin.itemAmount = 2; pin.optWeather = true;
        pin.outfitsDisabled = { "Lara_Joby" };
        const uint64_t v = ShareCode::effectiveSeed(pin);
        std::printf("  effective value of the pinned settings: 0x%016llX\n",
                    static_cast<unsigned long long>(v));
        check(v == 0xB2AA237E267BD99Dull, "effective value unchanged");
        check(ShareCode::effectiveSeed(def) != SeedText::parse(def.seedText),
              "the switches enter the effective value");
    }

    std::printf("  default Config: %s\n", ShareCode::make(def).c_str());
    std::printf("  every switch on: %s\n", code.c_str());
}

int main(int argc, char** argv)
{
    std::printf("TR5Randomizer - core self-test\n\n");
    testRng();
    std::printf("\n");
    testJson();
    std::printf("\n");
    testInflate();
    std::printf("\n");
    testShareCode();
    std::printf("\n");
    testDataRule(argc > 1 ? argv[1] : nullptr);
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

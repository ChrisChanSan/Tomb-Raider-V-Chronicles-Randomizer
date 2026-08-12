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
// Selbsttest des Kerns. Laeuft ohne GUI und ohne Leveldateien, prueft aber
// gegen echte Zonen-Exporte, wenn ein Pfad uebergeben wird.
//
// Aufruf:  TR5RandoSelfTest.exe [<Pfad zu einem Zonen-JSON>]
//
// Zweck: die beiden Eigenschaften absichern, auf denen alles andere aufbaut -
// dass ein Seed reproduzierbar ist und dass unsere eigenen Exporte fehlerfrei
// gelesen werden. Beides faellt sonst erst auf, wenn ein Nutzer ein kaputtes
// Level meldet.
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
    std::printf("  [%s] %s\n", ok ? "ok " : "FEHL", what);
    if (!ok) ++g_failed;
}

static void testRng()
{
    std::printf("RNG\n");

    // Derselbe Seed muss dieselbe Folge liefern.
    Rng a(12345), b(12345);
    bool same = true;
    for (int i = 0; i < 1000; ++i) if (a.next() != b.next()) { same = false; break; }
    check(same, "gleicher Seed -> gleiche Folge");

    Rng c(12345), d(12346);
    bool diff = false;
    for (int i = 0; i < 8; ++i) if (c.next() != d.next()) { diff = true; break; }
    check(diff, "anderer Seed -> andere Folge");

    // Mischen muss reproduzierbar sein - das ist der Punkt, an dem std::shuffle
    // versagt haette.
    std::vector<int> v1, v2;
    for (int i = 0; i < 200; ++i) { v1.push_back(i); v2.push_back(i); }
    Rng e(999), f(999);
    e.shuffle(v1);
    f.shuffle(v2);
    check(v1 == v2, "gleicher Seed -> gleiche Mischung");

    bool moved = false;
    for (int i = 0; i < 200; ++i) if (v1[i] != i) { moved = true; break; }
    check(moved, "Mischung veraendert die Reihenfolge tatsaechlich");

    // Mischen darf nichts verlieren oder doppeln.
    std::vector<bool> seen(200, false);
    bool perm = true;
    for (int x : v1) { if (x < 0 || x >= 200 || seen[x]) { perm = false; break; } seen[x] = true; }
    check(perm, "Mischung ist eine echte Permutation");

    // below() muss im Bereich bleiben und alle Werte treffen.
    Rng g(7);
    std::vector<int> hits(6, 0);
    for (int i = 0; i < 60000; ++i) hits[static_cast<size_t>(g.below(6))]++;
    bool inRange = true, allHit = true;
    for (int h : hits) { if (h == 0) allHit = false; }
    // Grobe Gleichverteilungspruefung: kein Wert darf um mehr als 20 Prozent
    // vom Erwartungswert abweichen. Faengt einen kaputten Modulo zuverlaessig.
    for (int h : hits) { if (h < 8000 || h > 12000) inRange = false; }
    check(allHit, "below(6) trifft alle sechs Werte");
    check(inRange, "below(6) ist ausreichend gleichverteilt");
    check(Rng(1).below(0) == 0, "below(0) liefert 0 statt zu stuerzen");

    // Seed-Text hin und zurueck.
    bool roundTrip = true;
    const uint64_t probes[] = { 0ull, 1ull, 42ull, 123456789ull, 0xFFFFFFFFFFFFFFFFull };
    for (uint64_t p : probes)
    {
        std::string s = SeedText::format(p);
        if (SeedText::parse(s) != p) { roundTrip = false; break; }
    }
    check(roundTrip, "Seed-Text laesst sich verlustfrei zurueckwandeln");

    check(SeedText::parse("Weihnachten") == SeedText::parse("Weihnachten"),
          "freier Text ergibt reproduzierbar denselben Seed");

    std::string s42 = SeedText::format(42);
    check(SeedText::parse(s42) == SeedText::parse(s42 + "  "),
          "Leerraum im Seed-Text stoert nicht");
}

static void testJson()
{
    std::printf("JSON\n");

    JsonValue v;
    std::string err;

    check(Json::parseString("{\"a\":1,\"b\":[1,2,3],\"c\":\"x\",\"d\":true,\"e\":null}", v, err),
          "einfaches Objekt wird gelesen");
    check(v["a"].asInt() == 1, "Zahl gelesen");
    check(v["b"].size() == 3 && v["b"][2].asInt() == 3, "Array gelesen");
    check(v["c"].asString() == "x", "Zeichenkette gelesen");
    check(v["d"].asBool(), "Wahrheitswert gelesen");
    check(v["e"].isNull(), "null gelesen");
    check(v["gibtsnicht"].isNull(), "fehlender Schluessel liefert null statt Fehler");
    check(v["b"][99].isNull(), "Zugriff ausserhalb des Arrays liefert null");

    check(Json::parseString("\xEF\xBB\xBF{\"a\":1}", v, err), "BOM wird uebersprungen");

    check(!Json::parseString("{\"a\":1", v, err), "unvollstaendiges Objekt wird abgelehnt");
    check(!err.empty(), "Fehlermeldung ist gesetzt");
    check(!Json::parseString("{\"a\":1} nachlauf", v, err), "Nachlaufender Inhalt wird abgelehnt");

    // Negative Zahlen und Gleitkomma - Levelkoordinaten sind regelmaessig negativ.
    check(Json::parseString("{\"y\":-11982,\"f\":1.5e2}", v, err), "negative und Exponentialzahlen");
    check(v["y"].asInt() == -11982, "negative Zahl korrekt");
    check(static_cast<int>(v["f"].asDouble()) == 150, "Exponentialschreibweise korrekt");

    // Schreiben und wieder lesen.
    JsonValue o = JsonValue::makeObject();
    JsonValue arr = JsonValue::makeArray();
    JsonValue e1 = JsonValue::makeObject();
    e1.set("typeId", JsonValue::makeInt(174));
    e1.set("room", JsonValue::makeInt(166));
    e1.set("x", JsonValue::makeInt(-90624));
    arr.push(e1);
    o.set("itemAdds", arr);

    std::string text = o.dump();
    check(text.find("\"typeId\":174") != std::string::npos, "Ganzzahl wird ohne Nachkomma geschrieben");
    check(text.find("-90624") != std::string::npos, "negative Koordinate wird geschrieben");

    JsonValue back;
    check(Json::parseString(text, back, err), "Selbst geschriebenes JSON ist wieder lesbar");
    check(back["itemAdds"][0]["room"].asInt() == 166, "Wert nach Hin- und Rueckwandlung gleich");
    check(o.dump() == back.dump(), "Ausgabe ist stabil (bytegleich)");
}

static void testAgainstRealExport(const char* path)
{
    std::printf("Echter Zonen-Export: %s\n", path);

    std::wstring wpath;
    for (const char* p = path; *p; ++p) wpath.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));

    JsonValue z;
    std::string err;
    if (!Json::parseFile(wpath, z, err))
    {
        std::printf("  [FEHL] laesst sich nicht lesen: %s\n", err.c_str());
        ++g_failed;
        return;
    }
    check(true, "Datei gelesen");

    const JsonValue& sectors = z["sectors"];
    const JsonValue& cons = z["keyConstraints"];
    std::printf("  Level=%s  Sektoren=%u  Constraints=%u  maxZone=%lld\n",
                z["level"].asString("?").c_str(),
                static_cast<unsigned>(sectors.size()),
                static_cast<unsigned>(cons.size()),
                static_cast<long long>(z["maxZone"].asInt(-1)));

    check(sectors.size() > 0, "Sektoren vorhanden");

    // Jeder Sektor muss die Felder tragen, auf die die Platzierung zugreift.
    bool fieldsOk = true;
    for (size_t i = 0; i < sectors.size() && fieldsOk; ++i)
    {
        const JsonValue& s = sectors[i];
        if (!s.has("room") || !s.has("sectorX") || !s.has("sectorZ") ||
            !s.has("zone") || !s.has("floorY"))
            fieldsOk = false;
    }
    check(fieldsOk, "jeder Sektor hat room/sectorX/sectorZ/zone/floorY");

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
    std::printf("Datenordner\n");

    DataFolder df;
    std::string err;
    check(df.loadChecksums(toW(csv), err), "Pruefsummenliste gelesen");
    if (!err.empty()) { std::printf("       %s\n", err.c_str()); return; }
    std::printf("  %u bekannte Level\n", static_cast<unsigned>(df.knownLevelCount()));
    check(df.knownLevelCount() == 16, "16 Level in der Liste");

    if (!dataDir) return;

    auto st = df.inspect(toW(dataDir));
    int vanilla = 0, modified = 0, missing = 0;
    for (const auto& s : st)
    {
        if (s.state == LevelState::Vanilla)  ++vanilla;
        if (s.state == LevelState::Modified) { ++modified; std::printf("       abweichend: %s\n", s.name.c_str()); }
        if (s.state == LevelState::Missing)  { ++missing;  std::printf("       fehlt:      %s\n", s.name.c_str()); }
    }
    std::printf("  vanilla=%d  abweichend=%d  fehlt=%d\n", vanilla, modified, missing);

    std::string reason;
    bool usable = DataFolder::isUsableSource(st, reason);
    check(usable, "Ordner ist als Quelle brauchbar");
    if (!usable) std::printf("       %s\n", reason.c_str());

    // Eine erfundene Datei muss als "fehlt" erkannt werden, nicht als vanilla.
    auto stBad = df.inspect(toW("C:\\gibtesnicht_zufallspfad_12345"));
    bool allMissing = true;
    for (const auto& s : stBad) if (s.state != LevelState::Missing) allMissing = false;
    check(allMissing, "nicht existierender Ordner liefert durchgehend 'fehlt'");
    std::string r2;
    check(!DataFolder::isUsableSource(stBad, r2), "leerer Ordner wird als Quelle abgelehnt");
}

static void testCopyAndRestore(const char* csv, const char* dataDir, const char* targetDir)
{
    std::printf("Kopieren und Zuruecksetzen\n");

    DataFolder df;
    std::string err;
    if (!df.loadChecksums(toW(csv), err)) { std::printf("  [FEHL] %s\n", err.c_str()); ++g_failed; return; }

    int copied = 0;
    bool ok = df.copyLevels(toW(dataDir), toW(targetDir), err, &copied);
    check(ok, "Level in den Zielordner kopiert");
    if (!ok) { std::printf("       %s\n", err.c_str()); return; }
    std::printf("  %d Dateien kopiert\n", copied);

    // Die Kopie muss byteidentisch sein - sonst waere schon das Ausgangsmaterial
    // eines Seeds ein anderes als das geprueffte.
    auto st = df.inspect(toW(targetDir));
    std::string reason;
    check(DataFolder::isUsableSource(st, reason), "Kopie ist byteidentisch mit Vanilla");
    if (!reason.empty()) std::printf("       %s\n", reason.c_str());

    // Eine Datei absichtlich verfaelschen, danach muss die Pruefung anschlagen
    // und das Zuruecksetzen sie wiederherstellen. Das ist genau der Ablauf
    // "randomisiertes Level -> Restore to default".
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
        check(!DataFolder::isUsableSource(st2, r2), "veraenderte Datei wird erkannt");

        int again = 0;
        check(df.copyLevels(toW(dataDir), toW(targetDir), err, &again), "Zuruecksetzen ausgefuehrt");
        auto st3 = df.inspect(toW(targetDir));
        std::string r3;
        check(DataFolder::isUsableSource(st3, r3), "nach dem Zuruecksetzen wieder Vanilla");
    }
    else
    {
        std::printf("  [FEHL] Testdatei nicht beschreibbar\n");
        ++g_failed;
    }
}

int main(int argc, char** argv)
{
    std::printf("TR5Randomizer - Selbsttest des Kerns\n\n");
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
    std::printf("\n%s (%d Fehler)\n", g_failed ? "FEHLGESCHLAGEN" : "Alles gruen", g_failed);
    return g_failed ? 1 : 0;
}

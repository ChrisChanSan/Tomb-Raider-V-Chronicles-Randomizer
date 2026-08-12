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
// Test der Platzierungslogik gegen echte Leveldaten.
//
// Aufruf: TestRando.exe <zonen.json> <levelinfo.json> [seed]
//
// Geprueft wird nicht "es kommt irgendein Patch heraus", sondern genau die
// Eigenschaften, deren Verletzung ein unspielbares Level ergaebe.
//----------------------------------------------------------------------------
#include "core/Json.h"
#include "core/Randomizer.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

static int g_failed = 0;

static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "ok " : "FEHL", what);
    if (!ok) ++g_failed;
}

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("Aufruf: TestRando.exe <zonen.json> <levelinfo.json> [seed]\n");
        return 2;
    }
    const uint64_t seed = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 4711ull;

    JsonValue zone, info;
    std::string err;
    if (!Json::parseFile(toW(argv[1]), zone, err)) { std::printf("Zonen-Export: %s\n", err.c_str()); return 1; }
    if (!Json::parseFile(toW(argv[2]), info, err)) { std::printf("Level-Info: %s\n", err.c_str()); return 1; }

    std::printf("Level %s, Seed %llu\n\n", zone["level"].asString("?").c_str(),
                static_cast<unsigned long long>(seed));

    // --- Ortsgebundene Items ---
    std::printf("Ortsgebundene Items (aus Pickup-Triggern abgeleitet)\n");
    std::vector<std::string> why;
    std::set<int> pinned = Rando::findTriggerPinnedItems(info, &why);
    for (const std::string& w : why) std::printf("  %s\n", w.c_str());

    RandoInput in;
    if (!Rando::buildInput(zone, info, in, err)) { std::printf("buildInput: %s\n", err.c_str()); return 1; }
    std::printf("  Sektoren=%u  Constraints=%u  FixedItems=%u  Injektionen=%u\n\n",
                static_cast<unsigned>(in.sectors.size()),
                static_cast<unsigned>(in.constraints.size()),
                static_cast<unsigned>(in.fixedItems.size()),
                static_cast<unsigned>(in.injections.size()));

    RandoOptions opt;
    JsonValue patch;
    RandoReport rep;
    std::printf("Platzierung\n");
    bool ok = Rando::run(in, opt, seed, patch, rep);
    if (!ok) std::printf("  Fehler: %s\n", rep.error.c_str());
    check(ok, "Lauf erfolgreich");
    if (!ok) return 1;

    std::printf("  Key-Items bewegt=%d  sonstige=%d  ortsfest=%d  injiziert=%d\n",
                rep.movedKeyItems, rep.movedOtherItems, rep.pinnedItems, rep.injected);
    for (const std::string& n : rep.notes) std::printf("  Hinweis: %s\n", n.c_str());

    // --- Regel 1: niemals Items entfernen ---
    check(!patch.has("itemRemoves"), "Patch enthaelt kein itemRemoves");

    const JsonValue& edits = patch["itemEdits"];

    // --- Regel 2: kein ortsgebundenes Item wurde angefasst ---
    std::set<int> edited;
    for (size_t i = 0; i < edits.size(); ++i) edited.insert(static_cast<int>(edits[i]["index"].asInt()));
    bool pinnedUntouched = true;
    for (int p : pinned) if (edited.count(p)) { pinnedUntouched = false; std::printf("       Item %d wurde bewegt!\n", p); }
    check(pinnedUntouched, "kein triggergebundenes Item wurde bewegt");

    bool fixedUntouched = true;
    for (int f : in.fixedItems) if (edited.count(f)) { fixedUntouched = false; std::printf("       FIXED Item %d wurde bewegt!\n", f); }
    check(fixedUntouched, "kein handkuratiertes FIXED_ITEM wurde bewegt");

    // --- Regel 3: OCB wird auf 0 gesetzt ---
    // Ausgenommen sind Typen aus KEEP_OCB: dort beschreibt die OCB nicht die
    // alte Stelle, sondern die Aufnahmeart, und ohne sie ist das Objekt am
    // neuen Platz gar nicht aufnehmbar. Geprueft wird dann, dass die OCB
    // wirklich die des Originals ist und nicht irgendeine.
    std::map<int, const LevelItem*> origOf;
    for (const LevelItem& it : in.items) origOf[it.index] = &it;
    bool ocbZero = true;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx = static_cast<int>(edits[i]["index"].asInt());
        const int ocb = static_cast<int>(edits[i]["ocb"].asInt());
        std::map<int, const LevelItem*>::const_iterator oi = origOf.find(idx);
        const bool behalten = oi != origOf.end() && in.keepOcbTypes.count(oi->second->typeId) != 0;
        const int erwartet = behalten ? oi->second->ocb : 0;
        if (ocb != erwartet)
        {
            ocbZero = false;
            std::printf("       Item %d hat OCB %d, erwartet %d\n", idx, ocb, erwartet);
        }
    }
    check(ocbZero, "jedes bewegte Item bekommt OCB 0 (ausser KEEP_OCB-Typen)");

    // --- Nur Pickups werden bewegt ---
    std::map<int, int> typeOf;
    for (const LevelItem& it : in.items) typeOf[it.index] = it.typeId;
    bool onlyPickups = true;
    for (int e : edited) if (!isPickupType(typeOf[e])) { onlyPickups = false; std::printf("       Item %d typeId %d ist kein Pickup!\n", e, typeOf[e]); }
    check(onlyPickups, "ausschliesslich Pickups wurden bewegt");

    // --- Keine zwei Items auf demselben Sektor ---
    std::set<long long> occupied;
    bool noStack = true;
    auto keyOf = [](int room, int x, int z) { return (static_cast<long long>(room) << 40) | (static_cast<long long>(x) << 20) | z; };
    for (size_t i = 0; i < edits.size(); ++i)
    {
        long long k = keyOf(static_cast<int>(edits[i]["room"].asInt()),
                            static_cast<int>(edits[i]["x"].asInt()),
                            static_cast<int>(edits[i]["z"].asInt()));
        if (!occupied.insert(k).second) noStack = false;
    }
    check(noStack, "keine zwei Items auf derselben Position");

    // --- Zwangsbedingungen der Key-Items eingehalten ---
    std::map<long long, const ZoneSector*> secAt;
    std::map<int, std::pair<int,int>> ro;
    for (const RoomOrigin& r : in.roomOrigins) ro[r.room] = { r.originX, r.originZ };
    for (const ZoneSector& s : in.sectors)
        secAt[keyOf(s.room, ro[s.room].first + s.sectorX * 1024 + 512, ro[s.room].second + s.sectorZ * 1024 + 512)] = &s;

    std::map<int, const KeyConstraint*> con;
    for (const KeyConstraint& k : in.constraints) con[k.typeId] = &k;

    bool constraintsHeld = true;
    bool allOnValidSector = true;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx = static_cast<int>(edits[i]["index"].asInt());
        const int room = static_cast<int>(edits[i]["room"].asInt());
        // Der Platzierungsversatz des Typs (TYPE_OFFSET) wird herausgerechnet,
        // bevor der Sektor gesucht wird - sonst schluege die Suche fehl, weil
        // das Item absichtlich NICHT auf der Sektormitte sitzt. Die Pruefung
        // bleibt dadurch genauso streng: sie verlangt exakt Mitte plus Versatz.
        PlacementOffset po;
        std::map<int, PlacementOffset>::const_iterator poi = in.typeOffset.find(typeOf[idx]);
        if (poi != in.typeOffset.end()) po = poi->second;

        long long k = keyOf(room, static_cast<int>(edits[i]["x"].asInt()) - po.dx,
                                  static_cast<int>(edits[i]["z"].asInt()) - po.dz);
        auto si = secAt.find(k);
        if (si == secAt.end()) { allOnValidSector = false; continue; }

        // Y muss exakt die Bodenhoehe des Sektors sein, sonst schwebt das Item.
        if (edits[i]["y"].asInt() != si->second->floorY + po.dy) allOnValidSector = false;

        auto ci = con.find(typeOf[idx]);
        if (ci == con.end()) continue;
        const KeyConstraint* c = ci->second;
        if (si->second->zone > c->maxZone) { constraintsHeld = false; std::printf("       Item %d in Zone %d > max %d\n", idx, si->second->zone, c->maxZone); }
        if (!c->allowRooms.empty() && !std::binary_search(c->allowRooms.begin(), c->allowRooms.end(), room))
        { constraintsHeld = false; std::printf("       Item %d in Raum %d nicht in allowRooms\n", idx, room); }
        if (std::binary_search(c->excludeRooms.begin(), c->excludeRooms.end(), room))
        { constraintsHeld = false; std::printf("       Item %d in gesperrtem Raum %d\n", idx, room); }

        const long long sk = secKey(si->second->room, si->second->sectorX, si->second->sectorZ);
        if (!c->islandSectors.empty() && !c->islandSectors.count(sk))
        { constraintsHeld = false; std::printf("       Item %d liegt ausserhalb seiner Inselgruppe\n", idx); }
        if (c->bannedSectors.count(sk))
        { constraintsHeld = false; std::printf("       Item %d liegt auf einem gesperrten Sektor\n", idx); }
        if (c->forbiddenSectors.count(sk))
        { constraintsHeld = false; std::printf("       Item %d liegt hinter seiner eigenen Tuer\n", idx); }
        if (c->hasMinFloorY && si->second->floorY < c->minFloorY &&
            (c->minFloorYScope.empty() || c->minFloorYScope.count(sk)))
        { constraintsHeld = false; std::printf("       Item %d liegt zu hoch (y=%d < %d)\n", idx, si->second->floorY, c->minFloorY); }
    }
    check(allOnValidSector, "jedes Item sitzt auf einem gueltigen Sektor, exakt auf Bodenhoehe plus Typversatz");
    check(constraintsHeld, "alle Key-Item-Zwangsbedingungen eingehalten");

    // --- Waffen bleiben aus den gesperrten Raeumen heraus ---
    bool weaponsClear = true;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx  = static_cast<int>(edits[i]["index"].asInt());
        const int room = static_cast<int>(edits[i]["room"].asInt());
        if (in.weaponTypes.count(typeOf[idx]) && in.noWeaponRooms.count(room))
        { weaponsClear = false; std::printf("       Waffe %d (Typ %d) in Raum %d\n", idx, typeOf[idx], room); }
    }
    check(weaponsClear, "keine Waffe in einem waffenfreien Raum");

    // --- Pflichtvorkommen in der Inselgruppe ---
    //
    // Das ist die einzige Bedingung, die sich nicht je Item pruefen laesst: sie
    // gilt fuer alle Instanzen eines Typs zusammen. Betrachtet werden deshalb
    // die bewegten Instanzen an ihrem neuen Platz UND die uebrigen dort, wo sie
    // in Vanilla liegen - ein ortsfestes Item auf der Insel erfuellt die
    // Forderung genauso.
    std::map<int, long long> placedAt;    // Item -> Sektorschluessel nach dem Lauf
    for (const LevelItem& it : in.items) placedAt[it.index] = secKey(it.room, it.sectorX, it.sectorZ);
    for (size_t i = 0; i < edits.size(); ++i)
    {
        long long k = keyOf(static_cast<int>(edits[i]["room"].asInt()),
                            static_cast<int>(edits[i]["x"].asInt()),
                            static_cast<int>(edits[i]["z"].asInt()));
        auto si = secAt.find(k);
        if (si != secAt.end())
            placedAt[static_cast<int>(edits[i]["index"].asInt())] =
                secKey(si->second->room, si->second->sectorX, si->second->sectorZ);
    }
    bool requiredHeld = true;
    for (const KeyConstraint& c : in.constraints)
    {
        if (c.requireInIsland.empty()) continue;
        int inside = 0, total = 0;
        for (const LevelItem& it : in.items)
        {
            if (it.typeId != c.typeId) continue;
            ++total;
            if (c.requireInIsland.count(placedAt[it.index])) ++inside;
        }
        std::printf("  Typ %d: %d von %d Instanzen in der Inselgruppe\n", c.typeId, inside, total);
        if (inside == 0)
        { requiredHeld = false; std::printf("       Typ %d hat KEINE Instanz in der Inselgruppe!\n", c.typeId); }
    }
    check(requiredHeld, "jeder Pflichttyp liegt mindestens einmal in seiner Inselgruppe");

    // --- Getragene Items brauchen OCB 0 ---
    //
    // Nachgerechnet wird derselbe Kasten wie in GetCarriedItems. Was der Lauf
    // bewegt hat, steht in edits; alles andere liegt noch auf seinem
    // Vanilla-Platz. Zaehlt werden beide Richtungen: getragene Items insgesamt
    // und die davon, die nach dem Ablegen unaufnehmbar waeren.
    std::map<int, LevelItem> after;
    for (const LevelItem& it : in.items) after[it.index] = it;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        LevelItem& f = after[static_cast<int>(edits[i]["index"].asInt())];
        f.room = static_cast<int>(edits[i]["room"].asInt());
        f.x    = static_cast<int>(edits[i]["x"].asInt());
        f.y    = static_cast<int>(edits[i]["y"].asInt());
        f.z    = static_cast<int>(edits[i]["z"].asInt());
        f.ocb  = static_cast<int>(edits[i]["ocb"].asInt());
    }
    int carried = 0, carriedBad = 0;
    for (const LevelItem& b : in.items)
    {
        if (!isCarrierType(b.typeId)) continue;
        for (const auto& kv : after)
        {
            const LevelItem& p = kv.second;
            if (p.index == b.index || !isPickupType(p.typeId)) continue;
            if (p.room != b.room || !isWithinCarryBox(b.x, b.y, b.z, p.x, p.y, p.z)) continue;
            ++carried;
            if ((p.ocb & 0x3F) != 0)
            {
                ++carriedBad;
                std::printf("       Item %d (Typ %d) wird von %d getragen, OCB %d\n",
                            p.index, p.typeId, b.index, p.ocb & 0x3F);
            }
        }
    }
    std::printf("  %d Pickups landen bei einem Traeger, davon %d mit OCB != 0\n", carried, carriedBad);
    check(carriedBad == 0, "jedes getragene Pickup hat OCB 0");

    // --- Reproduzierbarkeit ---
    JsonValue p2; RandoReport r2;
    Rando::run(in, opt, seed, p2, r2);
    check(p2.dump() == patch.dump(), "gleicher Seed -> bytegleicher Patch");

    // Ein anderer Seed muss ein anderes Ergebnis liefern - aber nur, wenn es
    // ueberhaupt etwas zu bewegen gibt. richcut2 ist eine reine Cutscene ohne
    // ein einziges Pickup; dort ist ein identisches (leeres) Ergebnis richtig
    // und kein Zeichen fuer einen kaputten Zufallsgenerator.
    if (rep.movedKeyItems + rep.movedOtherItems > 1)
    {
        JsonValue p3; RandoReport r3;
        Rando::run(in, opt, seed + 1, p3, r3);
        check(p3.dump() != patch.dump(), "anderer Seed -> anderes Ergebnis");
    }
    else
    {
        std::printf("  [--- ] Seed-Variation nicht pruefbar - Level hat %d bewegliche Items\n",
                    rep.movedKeyItems + rep.movedOtherItems);
    }

    std::printf("\n%s (%d Fehler)\n", g_failed ? "FEHLGESCHLAGEN" : "Alles gruen", g_failed);
    return g_failed ? 1 : 0;
}

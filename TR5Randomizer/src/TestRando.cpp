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
// Test of the placement logic against real level data.
//
// Usage: TestRando.exe <zones.json> <levelinfo.json> [seed]
//
// What is checked is not "some patch comes out" but exactly the properties
// whose violation would yield an unplayable level.
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
    std::printf("  [%s] %s\n", ok ? "ok " : "FAIL", what);
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
        std::printf("Usage: TestRando.exe <zones.json> <levelinfo.json> [seed]\n");
        return 2;
    }
    const uint64_t seed = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 4711ull;

    JsonValue zone, info;
    std::string err;
    if (!Json::parseFile(toW(argv[1]), zone, err)) { std::printf("zone export: %s\n", err.c_str()); return 1; }
    if (!Json::parseFile(toW(argv[2]), info, err)) { std::printf("level info: %s\n", err.c_str()); return 1; }

    std::printf("Level %s, seed %llu\n\n", zone["level"].asString("?").c_str(),
                static_cast<unsigned long long>(seed));

    // --- Location-bound items ---
    std::printf("Location-bound items (derived from pickup triggers)\n");
    std::vector<std::string> why;
    std::set<int> pinned = Rando::findTriggerPinnedItems(info, &why);
    for (const std::string& w : why) std::printf("  %s\n", w.c_str());

    RandoInput in;
    if (!Rando::buildInput(zone, info, in, err)) { std::printf("buildInput: %s\n", err.c_str()); return 1; }
    std::printf("  sectors=%u  constraints=%u  fixedItems=%u  injections=%u\n\n",
                static_cast<unsigned>(in.sectors.size()),
                static_cast<unsigned>(in.constraints.size()),
                static_cast<unsigned>(in.fixedItems.size()),
                static_cast<unsigned>(in.injections.size()));

    RandoOptions opt;
    JsonValue patch;
    RandoReport rep;
    std::printf("Placement\n");
    bool ok = Rando::run(in, opt, seed, patch, rep);
    if (!ok) std::printf("  error: %s\n", rep.error.c_str());
    check(ok, "run successful");
    if (!ok) return 1;

    std::printf("  key items moved=%d  other=%d  pinned=%d  injected=%d\n",
                rep.movedKeyItems, rep.movedOtherItems, rep.pinnedItems, rep.injected);
    for (const std::string& n : rep.notes) std::printf("  note: %s\n", n.c_str());

    // --- Rule 1: never remove items ---
    check(!patch.has("itemRemoves"), "patch contains no itemRemoves");

    const JsonValue& edits = patch["itemEdits"];

    // --- Rule 2: no location-bound item was touched ---
    std::set<int> edited;
    for (size_t i = 0; i < edits.size(); ++i) edited.insert(static_cast<int>(edits[i]["index"].asInt()));
    bool pinnedUntouched = true;
    for (int p : pinned) if (edited.count(p)) { pinnedUntouched = false; std::printf("       item %d was moved!\n", p); }
    check(pinnedUntouched, "no trigger-bound item was moved");

    bool fixedUntouched = true;
    for (int f : in.fixedItems) if (edited.count(f)) { fixedUntouched = false; std::printf("       FIXED item %d was moved!\n", f); }
    check(fixedUntouched, "no hand-curated FIXED_ITEM was moved");

    // --- Rule 3: OCB is set to 0 ---
    // Types from KEEP_OCB are the exception: there the OCB does not describe
    // the old place but the pickup manner, and without it the object cannot
    // be picked up at its new place at all. What is checked then is that the
    // OCB really is the original's and not just any.
    std::map<int, const LevelItem*> origOf;
    for (const LevelItem& it : in.items) origOf[it.index] = &it;
    bool ocbZero = true;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx = static_cast<int>(edits[i]["index"].asInt());
        const int ocb = static_cast<int>(edits[i]["ocb"].asInt());
        std::map<int, const LevelItem*>::const_iterator oi = origOf.find(idx);
        const bool kept = oi != origOf.end() && in.keepOcbTypes.count(oi->second->typeId) != 0;
        const int expected = kept ? oi->second->ocb : 0;
        if (ocb != expected)
        {
            ocbZero = false;
            std::printf("       item %d has OCB %d, expected %d\n", idx, ocb, expected);
        }
    }
    check(ocbZero, "every moved item gets OCB 0 (except KEEP_OCB types)");

    // --- Only pickups are moved ---
    std::map<int, int> typeOf;
    for (const LevelItem& it : in.items) typeOf[it.index] = it.typeId;
    bool onlyPickups = true;
    for (int e : edited) if (!isPickupType(typeOf[e])) { onlyPickups = false; std::printf("       item %d typeId %d is no pickup!\n", e, typeOf[e]); }
    check(onlyPickups, "exclusively pickups were moved");

    // --- No two items on the same sector ---
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
    check(noStack, "no two items on the same position");

    // --- Key item constraints held ---
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
        // The type's placement offset (TYPE_OFFSET) is subtracted before the
        // sector is looked up - otherwise the lookup would fail because the
        // item deliberately does NOT sit on the sector center. The check
        // stays just as strict: it demands exactly center plus offset.
        PlacementOffset po;
        std::map<int, PlacementOffset>::const_iterator poi = in.typeOffset.find(typeOf[idx]);
        if (poi != in.typeOffset.end()) po = poi->second;

        long long k = keyOf(room, static_cast<int>(edits[i]["x"].asInt()) - po.dx,
                                  static_cast<int>(edits[i]["z"].asInt()) - po.dz);
        auto si = secAt.find(k);
        if (si == secAt.end()) { allOnValidSector = false; continue; }

        // Y must be exactly the sector's floor height, or the item floats.
        if (edits[i]["y"].asInt() != si->second->floorY + po.dy) allOnValidSector = false;

        auto ci = con.find(typeOf[idx]);
        if (ci == con.end()) continue;
        const KeyConstraint* c = ci->second;
        if (si->second->zone > c->maxZone) { constraintsHeld = false; std::printf("       item %d in zone %d > max %d\n", idx, si->second->zone, c->maxZone); }
        if (!c->allowRooms.empty() && !std::binary_search(c->allowRooms.begin(), c->allowRooms.end(), room))
        { constraintsHeld = false; std::printf("       item %d in room %d not in allowRooms\n", idx, room); }
        if (std::binary_search(c->excludeRooms.begin(), c->excludeRooms.end(), room))
        { constraintsHeld = false; std::printf("       item %d in barred room %d\n", idx, room); }

        const long long sk = secKey(si->second->room, si->second->sectorX, si->second->sectorZ);
        if (!c->islandSectors.empty() && !c->islandSectors.count(sk))
        { constraintsHeld = false; std::printf("       item %d lies outside its island group\n", idx); }
        if (c->bannedSectors.count(sk))
        { constraintsHeld = false; std::printf("       item %d lies on a barred sector\n", idx); }
        if (c->forbiddenSectors.count(sk))
        { constraintsHeld = false; std::printf("       item %d lies behind its own door\n", idx); }
        if (c->hasMinFloorY && si->second->floorY < c->minFloorY &&
            (c->minFloorYScope.empty() || c->minFloorYScope.count(sk)))
        { constraintsHeld = false; std::printf("       item %d lies too high (y=%d < %d)\n", idx, si->second->floorY, c->minFloorY); }
    }
    check(allOnValidSector, "every item sits on a valid sector, exactly at floor height plus type offset");
    check(constraintsHeld, "all key item constraints held");

    // --- Weapons stay out of the barred rooms ---
    bool weaponsClear = true;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx  = static_cast<int>(edits[i]["index"].asInt());
        const int room = static_cast<int>(edits[i]["room"].asInt());
        if (in.weaponTypes.count(typeOf[idx]) && in.noWeaponRooms.count(room))
        { weaponsClear = false; std::printf("       weapon %d (type %d) in room %d\n", idx, typeOf[idx], room); }
    }
    check(weaponsClear, "no weapon in a weapon-free room");

    // --- Mandatory presence in the island group ---
    //
    // This is the only condition that cannot be checked per item: it applies
    // to all instances of a type together. Considered are therefore the
    // moved instances at their new place AND the rest where they lie in
    // vanilla - a pinned item on the island satisfies the demand just as
    // well.
    std::map<int, long long> placedAt;    // item -> sector key after the run
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
        std::printf("  type %d: %d of %d instances in the island group\n", c.typeId, inside, total);
        if (inside == 0)
        { requiredHeld = false; std::printf("       type %d has NO instance in the island group!\n", c.typeId); }
    }
    check(requiredHeld, "every mandatory type lies at least once in its island group");

    // --- Carried items need OCB 0 ---
    //
    // Recomputes the same box as GetCarriedItems. What the run moved stands
    // in edits; everything else still lies on its vanilla place. Both
    // directions are counted: carried items in total and those among them
    // that would be unpickable after being dropped.
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
                std::printf("       item %d (type %d) is carried by %d, OCB %d\n",
                            p.index, p.typeId, b.index, p.ocb & 0x3F);
            }
        }
    }
    std::printf("  %d pickups land at a carrier, %d of them with OCB != 0\n", carried, carriedBad);
    check(carriedBad == 0, "every carried pickup has OCB 0");

    // --- Reproducibility ---
    JsonValue p2; RandoReport r2;
    Rando::run(in, opt, seed, p2, r2);
    check(p2.dump() == patch.dump(), "same seed -> byte-identical patch");

    // A different seed must yield a different result - but only if there is
    // anything to move at all. richcut2 is a pure cutscene without a single
    // pickup; there an identical (empty) result is correct and no sign of a
    // broken random generator.
    if (rep.movedKeyItems + rep.movedOtherItems > 1)
    {
        JsonValue p3; RandoReport r3;
        Rando::run(in, opt, seed + 1, p3, r3);
        check(p3.dump() != patch.dump(), "different seed -> different result");
    }
    else
    {
        std::printf("  [--- ] seed variation not testable - level has %d movable items\n",
                    rep.movedKeyItems + rep.movedOtherItems);
    }

    std::printf("\n%s (%d errors)\n", g_failed ? "FAILED" : "All green", g_failed);
    return g_failed ? 1 : 0;
}

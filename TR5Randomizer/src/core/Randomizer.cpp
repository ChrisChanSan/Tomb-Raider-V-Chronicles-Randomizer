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
#include "Randomizer.h"
#include "LevelChains.h"
#include "Tr3Source.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
    std::string fmt(const char* f, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, f);
        std::vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        return std::string(buf);
    }

    // Read the zone export's sector sets. Each entry there is a string
    // "room,x,z" - the PowerShell side uses the same spelling as hashset
    // keys, and both tools must mean exactly the same sectors. If the field
    // is missing, a null value arrives here and the set stays empty, which
    // everywhere means "no restriction".
    void readSectorSet(const JsonValue& v, std::set<long long>& out)
    {
        for (size_t i = 0; i < v.size(); ++i)
        {
            const std::string s = v[i].asString();
            // Split by hand instead of sscanf: room and sector indices are
            // never negative, so this suffices and the CRT warning goes away.
            int part[3] = { 0, 0, 0 };
            int n = 0;
            bool ok = !s.empty();
            for (size_t c = 0; c < s.size() && ok; ++c)
            {
                if (s[c] == ',')                       { ok = (++n <= 2); }
                else if (s[c] >= '0' && s[c] <= '9')   { part[n] = part[n] * 10 + (s[c] - '0'); }
                else                                    { ok = false; }
            }
            if (ok && n == 2) out.insert(secKey(part[0], part[1], part[2]));
        }
    }
}

std::set<int> Rando::findTriggerPinnedItems(const JsonValue& levelInfo,
                                            std::vector<std::string>* explain)
{
    std::set<int> pinned;

    // Which item the pickup trigger means sits in its reference word
    // ("refItem"), not in the command list - the commands are without
    // exception its effect (doors, enemies, flipmaps).
    //
    // A detour used to stand here: the meant item was recognized by an
    // Object command pointing at an item on the trigger sector. That gave
    // the right answer for the wrong reason - the exporter did not read the
    // reference word back then and pushed it into the list as the first
    // Object command. Since the exporter carries it as refItem, the detour
    // is empty, and the same loop would no longer pin ANY item without this
    // change. The map still stands: it is needed to look up the type of the
    // objects woken by command (swarm critters).
    std::map<int, const JsonValue*> byIndex;
    const JsonValue& items = levelInfo["items"];
    for (size_t i = 0; i < items.size(); ++i)
        byIndex[static_cast<int>(items[i]["index"].asInt())] = &items[i];

    const JsonValue& trigs = levelInfo["triggers"];
    for (size_t t = 0; t < trigs.size(); ++t)
    {
        const JsonValue& tr = trigs[t];
        if (tr["triggerType"].asString() != "Pickup") continue;

        // Which item is meant? Two export states must be served, and a
        // cut-off date would be dangerous here: with an older file lying
        // around, NO trigger would have pinned an item anymore, and pinned
        // items would have slipped into the pool without a sound.
        //
        // Newer export: the reference word is there as "refItem".
        // Older export: the word was emitted as the first Object command.
        //   It then points at an item lying on the trigger sector - and
        //   that is exactly how it can be recognized.
        const int trRoom = static_cast<int>(tr["room"].asInt());
        const int trSx = static_cast<int>(tr["sectorX"].asInt());
        const int trSz = static_cast<int>(tr["sectorZ"].asInt());

        const JsonValue& ref = tr["refItem"];
        int refItem = ref.isNull() ? -1 : static_cast<int>(ref.asInt());

        std::vector<std::string> effects;
        bool onlySoundtrack = true;

        const JsonValue& cmds = tr["commands"];
        for (size_t c = 0; c < cmds.size(); ++c)
        {
            const std::string type = cmds[c]["type"].asString();
            const int idx = static_cast<int>(cmds[c]["index"].asInt());

            bool harmless = false;

            if (type == "Object")
            {
                auto it = byIndex.find(idx);
                if (it != byIndex.end())
                {
                    const JsonValue& target = *it->second;

                    if (refItem < 0 &&
                        static_cast<int>(target["room"].asInt()) == trRoom &&
                        static_cast<int>(target["sectorX"].asInt()) == trSx &&
                        static_cast<int>(target["sectorZ"].asInt()) == trSz)
                    {
                        // That is the older export's reference word, not a
                        // command. Do not count it as an effect.
                        refItem = idx;
                        continue;
                    }
                    // Swarm critters count like music: not pinning.
                    harmless = isDespawningSwarm(static_cast<int>(target["typeId"].asInt()));
                }
            }

            effects.push_back(type);
            if (type != "PlaySoundtrack" && !harmless) onlySoundtrack = false;
        }

        if (refItem < 0) continue;                  // no interpretable pickup trigger
        const std::vector<int> onSector(1, refItem);

        // A trigger whose only further effect is music does not pin the
        // item - a deliberately accepted sacrifice, so e.g. Andrea2's
        // crowbar stays randomizable.
        if (effects.empty() || onlySoundtrack)
        {
            if (explain)
                for (int i : onSector)
                    explain->push_back(fmt("item %d stays free - pickup trigger only fires music or swarm critters", i));
            continue;
        }

        for (int i : onSector)
        {
            pinned.insert(i);
            if (explain)
            {
                std::string e;
                for (size_t k = 0; k < effects.size(); ++k) { if (k) e += "+"; e += effects[k]; }
                explain->push_back(fmt("item %d pinned - pickup trigger fires: %s", i, e.c_str()));
            }
        }
    }
    return pinned;
}

bool Rando::buildInput(const JsonValue& zoneExport,
                       const JsonValue& levelInfo,
                       RandoInput& out,
                       std::string& error)
{
    out = RandoInput();
    out.level = zoneExport["level"].asString();
    out.maxZone = static_cast<int>(zoneExport["maxZone"].asInt());

    const JsonValue& secs = zoneExport["sectors"];
    if (secs.size() == 0) { error = "zone export contains no sectors"; return false; }
    out.sectors.reserve(secs.size());
    for (size_t i = 0; i < secs.size(); ++i)
    {
        ZoneSector s;
        s.room = static_cast<int>(secs[i]["room"].asInt());
        s.sectorX = static_cast<int>(secs[i]["sectorX"].asInt());
        s.sectorZ = static_cast<int>(secs[i]["sectorZ"].asInt());
        s.zone = static_cast<int>(secs[i]["zone"].asInt());
        s.floorY = static_cast<int>(secs[i]["floorY"].asInt());
        s.clearance = static_cast<int>(secs[i]["clearance"].asInt(0));

        // Wall edges. Older exports do not know the field - then the list
        // stays empty, and wall pickups find no place instead of landing
        // in a wrong one.
        const JsonValue& wa = secs[i]["wallAngles"];
        for (size_t w = 0; w < wa.size(); ++w)
            s.wallAngles.push_back(static_cast<int>(wa[w].asInt()));

        // Approach sectors are listed as "room,x,z", in preference order.
        const JsonValue& ap = secs[i]["approach"];
        for (size_t a = 0; a < ap.size(); ++a)
        {
            int r = 0, x = 0, z = 0;
            if (std::sscanf(ap[a].asString().c_str(), "%d,%d,%d", &r, &x, &z) == 3)
                s.approach.push_back(secKey(r, x, z));
        }

        out.sectors.push_back(s);
    }

    const JsonValue& cons = zoneExport["keyConstraints"];
    for (size_t i = 0; i < cons.size(); ++i)
    {
        KeyConstraint k;
        k.typeId = static_cast<int>(cons[i]["typeId"].asInt());
        k.maxZone = static_cast<int>(cons[i]["maxZone"].asInt());
        const JsonValue& a = cons[i]["allowRooms"];
        for (size_t j = 0; j < a.size(); ++j) k.allowRooms.push_back(static_cast<int>(a[j].asInt()));
        const JsonValue& e = cons[i]["excludeRooms"];
        for (size_t j = 0; j < e.size(); ++j) k.excludeRooms.push_back(static_cast<int>(e[j].asInt()));
        std::sort(k.allowRooms.begin(), k.allowRooms.end());
        std::sort(k.excludeRooms.begin(), k.excludeRooms.end());

        // The fields below are all optional: a level without an island group
        // exports them as null, and then the sets stay empty.
        const JsonValue& mf = cons[i]["minFloorY"];
        if (!mf.isNull()) { k.hasMinFloorY = true; k.minFloorY = static_cast<int>(mf.asInt()); }
        readSectorSet(cons[i]["minFloorYIsland"],  k.minFloorYScope);
        readSectorSet(cons[i]["islandSectors"],    k.islandSectors);
        readSectorSet(cons[i]["bannedSectors"],    k.bannedSectors);
        readSectorSet(cons[i]["forbiddenSectors"], k.forbiddenSectors);
        readSectorSet(cons[i]["requireInIsland"],  k.requireInIsland);

        out.constraints.push_back(k);
    }
    std::sort(out.constraints.begin(), out.constraints.end(),
              [](const KeyConstraint& a, const KeyConstraint& b) { return a.typeId < b.typeId; });

    const JsonValue& fx = zoneExport["fixedItems"];
    for (size_t i = 0; i < fx.size(); ++i) out.fixedItems.push_back(static_cast<int>(fx[i]["item"].asInt()));
    std::sort(out.fixedItems.begin(), out.fixedItems.end());

    const JsonValue& et = zoneExport["enemyTypes"];
    for (size_t i = 0; i < et.size(); ++i)
        out.enemyTypes.push_back(static_cast<int>(et[i]["typeId"].asInt()));
    std::sort(out.enemyTypes.begin(), out.enemyTypes.end());

    const JsonValue& es = zoneExport["enemySounds"];
    for (size_t i = 0; i < es.size(); ++i)
    {
        EnemySound s;
        s.typeId = static_cast<int>(es[i]["typeId"].asInt());
        s.map = static_cast<int>(es[i]["map"].asInt(-1));
        s.fromLevel = es[i]["fromLevel"].asString();
        s.fromMap = static_cast<int>(es[i]["fromMap"].asInt(-1));
        if (s.map >= 0 && s.fromMap >= 0 && !s.fromLevel.empty())
            out.enemySounds.push_back(s);
    }

    const JsonValue& nw = zoneExport["noWeaponRooms"];
    for (size_t i = 0; i < nw.size(); ++i) out.noWeaponRooms.insert(static_cast<int>(nw[i].asInt()));
    const JsonValue& wt = zoneExport["weaponTypes"];
    for (size_t i = 0; i < wt.size(); ++i) out.weaponTypes.insert(static_cast<int>(wt[i].asInt()));

    const JsonValue& tyo = zoneExport["typeOffsets"];
    for (size_t i = 0; i < tyo.size(); ++i)
    {
        PlacementOffset po;
        po.dx = static_cast<int>(tyo[i]["dx"].asInt());
        po.dy = static_cast<int>(tyo[i]["dy"].asInt());
        po.dz = static_cast<int>(tyo[i]["dz"].asInt());
        out.typeOffset[static_cast<int>(tyo[i]["typeId"].asInt())] = po;
    }

    const JsonValue& ko = zoneExport["keepOcbTypes"];
    for (size_t i = 0; i < ko.size(); ++i) out.keepOcbTypes.insert(static_cast<int>(ko[i].asInt()));

    // Sectors behind a deadstop, as "room,x,z" like the approach sectors.
    // If the field is missing (older export), the set stays empty and
    // nothing changes.
    const JsonValue& fp = zoneExport["flagPresets"];
    for (size_t i = 0; i < fp.size(); ++i)
        out.flagPresets[static_cast<int>(fp[i]["item"].asInt())] = static_cast<int>(fp[i]["flags"].asInt());

    const JsonValue& kb = zoneExport["keyBarredSectors"];
    for (size_t i = 0; i < kb.size(); ++i)
    {
        int r = 0, x = 0, z = 0;
        if (std::sscanf(kb[i].asString().c_str(), "%d,%d,%d", &r, &x, &z) == 3)
            out.keyBarredSectors.insert(secKey(r, x, z));
    }

    const JsonValue& kbe = zoneExport["keyBarredExemptTypes"];
    for (size_t i = 0; i < kbe.size(); ++i)
        out.keyBarredExemptTypes.insert(static_cast<int>(kbe[i].asInt()));

    const JsonValue& inj = zoneExport["requiredInjections"];
    for (size_t i = 0; i < inj.size(); ++i)
    {
        Injection j;
        j.typeId = static_cast<int>(inj[i]["typeId"].asInt());
        j.room = static_cast<int>(inj[i]["room"].asInt());
        const JsonValue& rl = inj[i]["rooms"];
        for (size_t r = 0; r < rl.size(); ++r)
            j.rooms.push_back(static_cast<int>(rl[r].asInt()));
        const JsonValue& sx = inj[i]["sectorX"];
        const JsonValue& sz = inj[i]["sectorZ"];
        if (!sx.isNull() && !sz.isNull())
        {
            j.hasSector = true;
            j.sectorX = static_cast<int>(sx.asInt());
            j.sectorZ = static_cast<int>(sz.asInt());
        }
        const JsonValue& yo = inj[i]["yOffset"];
        if (!yo.isNull()) j.yOffset = static_cast<int>(yo.asInt());
        j.reason = inj[i]["reason"].asString();
        out.injections.push_back(j);
    }

    const JsonValue& rooms = levelInfo["rooms"];
    for (size_t i = 0; i < rooms.size(); ++i)
    {
        RoomOrigin r;
        r.room = static_cast<int>(rooms[i]["index"].asInt());
        r.originX = static_cast<int>(rooms[i]["originX"].asInt());
        r.originZ = static_cast<int>(rooms[i]["originZ"].asInt());
        r.numX = static_cast<int>(rooms[i]["numXSectors"].asInt());
        r.numZ = static_cast<int>(rooms[i]["numZSectors"].asInt());
        out.roomOrigins.push_back(r);
        if (rooms[i]["waterRoom"].asBool(false))
            out.waterRooms.insert(r.room);
    }

    // Per-room type bans (see RandoInput::typeNoRooms) and curated
    // antitrigger immunities.
    {
        const JsonValue& tnr = zoneExport["typeNoRooms"];
        for (size_t i = 0; i < tnr.size(); ++i)
        {
            const int type = static_cast<int>(tnr[i]["typeId"].asInt(-1));
            if (type < 0) continue;
            const JsonValue& rl = tnr[i]["rooms"];
            for (size_t r = 0; r < rl.size(); ++r)
                out.typeNoRooms[type].insert(static_cast<int>(rl[r].asInt(-1)));
        }
        // Per-sector type bans. Same purpose as typeNoRooms, only
        // finer - and like there outside the constraint branch, so it also
        // hits types without a pickup: andy3's chalk and crowbar have no
        // constraint, the constraints' bannedSectors never reach them.
        const JsonValue& tns = zoneExport["typeNoSectors"];
        for (size_t i = 0; i < tns.size(); ++i)
        {
            const int type = static_cast<int>(tns[i]["typeId"].asInt(-1));
            if (type < 0) continue;
            const JsonValue& sl = tns[i]["sectors"];
            for (size_t q = 0; q < sl.size(); ++q)
            {
                int r = 0, x = 0, z = 0;
                if (std::sscanf(sl[q].asString().c_str(), "%d,%d,%d", &r, &x, &z) == 3)
                    out.typeNoSectors[type].insert(secKey(r, x, z));
            }
        }

        const JsonValue& ai = zoneExport["antitriggerImmune"];
        for (size_t i = 0; i < ai.size(); ++i)
            out.antitriggerImmuneItems.insert(static_cast<int>(ai[i].asInt(-1)));

    }

    // Collect the antitrigger targets - see RandoInput::antitriggered.
    {
        const JsonValue& trigs = levelInfo["triggers"];
        for (size_t t = 0; t < trigs.size(); ++t)
        {
            const std::string kind = trigs[t]["triggerType"].asString();
            if (kind != "AntiTrigger" && kind != "HeavyAntiTrigger") continue;
            const JsonValue& cmds = trigs[t]["commands"];
            for (size_t c = 0; c < cmds.size(); ++c)
                if (cmds[c]["type"].asString() == "Object")
                    out.antitriggered.insert(static_cast<int>(cmds[c]["index"].asInt(-1)));
        }
    }
    if (out.roomOrigins.empty()) { error = "level info contains no rooms"; return false; }

    const JsonValue& items = levelInfo["items"];
    for (size_t i = 0; i < items.size(); ++i)
    {
        LevelItem it;
        it.index = static_cast<int>(items[i]["index"].asInt());
        it.typeId = static_cast<int>(items[i]["typeId"].asInt());
        it.room = static_cast<int>(items[i]["room"].asInt());
        it.x = static_cast<int>(items[i]["x"].asInt());
        it.y = static_cast<int>(items[i]["y"].asInt());
        it.z = static_cast<int>(items[i]["z"].asInt());
        it.sectorX = static_cast<int>(items[i]["sectorX"].asInt());
        it.sectorZ = static_cast<int>(items[i]["sectorZ"].asInt());
        it.ocb = static_cast<int>(items[i]["ocb"].asInt());
        out.items.push_back(it);
    }
    std::sort(out.items.begin(), out.items.end(),
              [](const LevelItem& a, const LevelItem& b) { return a.index < b.index; });

    out.triggerPinned = findTriggerPinnedItems(levelInfo);

    // --- Coupled triggers ---
    //
    // Only the item index comes from the curation. Where the trigger lies
    // today is read from the level data here and not maintained by hand: a
    // written-down coordinate would be exactly the statement that silently
    // goes wrong at the next level rebuild.
    const JsonValue& ct = zoneExport["coupledTriggers"];
    for (size_t i = 0; i < ct.size(); ++i)
    {
        const int wanted = static_cast<int>(ct[i]["item"].asInt(-1));
        if (wanted < 0) continue;

        const JsonValue& trigs = levelInfo["triggers"];
        for (size_t t = 0; t < trigs.size(); ++t)
        {
            const JsonValue& tr = trigs[t];
            if (tr["triggerType"].asString() != "Pickup") continue;

            // The same two-way reading as in findTriggerPinnedItems: the
            // newer export carries the reference word as refItem, the older
            // as the first Object command onto an item of its own sector.
            int ref = tr["refItem"].isNull() ? -1 : static_cast<int>(tr["refItem"].asInt());
            if (ref < 0)
            {
                const int trRoom = static_cast<int>(tr["room"].asInt());
                const int trSx = static_cast<int>(tr["sectorX"].asInt());
                const int trSz = static_cast<int>(tr["sectorZ"].asInt());
                const JsonValue& cmds = tr["commands"];
                for (size_t c = 0; c < cmds.size() && ref < 0; ++c)
                {
                    if (cmds[c]["type"].asString() != "Object") continue;
                    const int idx = static_cast<int>(cmds[c]["index"].asInt());
                    for (const LevelItem& li : out.items)
                        if (li.index == idx && li.room == trRoom &&
                            li.sectorX == trSx && li.sectorZ == trSz)
                        { ref = idx; break; }
                }
            }
            if (ref != wanted) continue;

            CoupledTrigger c;
            c.itemIndex = wanted;
            c.room = static_cast<int>(tr["room"].asInt());
            c.sectorX = static_cast<int>(tr["sectorX"].asInt());
            c.sectorZ = static_cast<int>(tr["sectorZ"].asInt());
            out.coupledTriggers.push_back(c);
            break;
        }
    }

    return true;
}

bool Rando::floorAt(const RandoInput& in, const ZoneSector& s,
                    int localX, int localZ, int& yOut)
{
    yOut = s.floorY;
    std::map<long long, SlopeInfo>::const_iterator f =
        in.slopes.find(secKey(s.room, s.sectorX, s.sectorZ));
    if (f == in.slopes.end()) return true;       // no entry: flat

    const FloorPos pos = floorPosAt(f->second, s.floorY, localX, localZ);
    yOut = pos.y;
    return pos.walkable;
}

bool Rando::sectorAllowedFor(const RandoInput& in,
                             const KeyConstraint* k,
                             const ZoneSector& s,
                             int typeId,
                             bool mustBeOnIsland,
                             int itemOcb)
{
    // Wall grabs: OCB 2 (crowbar) and OCB 9 (joby crow) demand a wall and
    // standing height. Both are engine rules, not optics:
    //  - PickUpCollision (pickup.cpp) explicitly checks !lara.IsDucked, so
    //    in the crawl the grab is impossible. Lara's standing collision
    //    height is 762 (game\lara.cpp), 768 is the next step above it.
    //  - The wall itself the engine NEVER checks - it is optics and
    //    reachability. Without a wall edge there would be no sensible
    //    angle, though, and the model (mesh at ~+470 in model Z) would
    //    stand free in the room instead of inside a wall.
    if (itemOcb == 2 || itemOcb == 9)
    {
        if (s.wallAngles.empty()) return false;
        if (s.clearance < 768) return false;

        // And only on a completely flat sector. The wall grab ends with a
        // step backwards, and that step does not come off over a sloped
        // field - Lara hangs at its edge. This is the one difference to an
        // ordinary pickup: walkable slopes up to slope 2 are fully valid
        // spots for those, since the game itself places pickups there,
        // because they are only picked up, not stepped away from.
        //
        // Flat means: the same floor height at all four corners, each of
        // them walkable. A sector without a slope entry is flat by
        // definition (Floor.h) and floorAt says so in one call, so the
        // common case costs a single map lookup.
        //
        // The rule hangs off the OCB, not off a level: the animation is the
        // same everywhere. Wall grabs exist in four levels.
        {
            int flatY = 0;
            if (!floorAt(in, s, 0, 0, flatY)) return false;

            for (int cx = 0; cx <= 1; ++cx)
                for (int cz = 0; cz <= 1; ++cz)
                {
                    int cornerY = 0;
                    if (!floorAt(in, s, cx * 1023, cz * 1023, cornerY)) return false;
                    if (cornerY != flatY) return false;
                }
        }

        // And never under water: PickUpCollision only runs at water_status
        // LW_ABOVE_WATER or LW_WADE (pickup.cpp). Applies only to these
        // grabs - ordinary pickups and puzzle pieces may very well lie in
        // water; only this special kind, with its wall-grab animation,
        // must not.
        if (in.waterRooms.count(s.room)) return false;

        // No wall grab within a carrier's reach: GetCarriedItems (setup.cpp)
        // attaches every pickup in the box |dx|<512, |dy|<256, |dz|<512 to
        // the nearest enemy or searchable at level start. A carried wall
        // grab is lost after the carrier's death: DropBaddyPickups puts it
        // on open floor, where its pickup style (wall + angle) can never be
        // satisfied again. The box is widened to sector size because the
        // final wall position is only rolled at write time - every point of
        // the sector must be safe. The reverse direction (new enemy next to
        // a finished wall grab) bars the enemy placement.
        const RoomOrigin* wro = nullptr;
        for (const RoomOrigin& r : in.roomOrigins)
            if (r.room == s.room) { wro = &r; break; }
        if (wro)
        {
            const int cx = wro->originX + s.sectorX * 1024 + 512;
            const int cz = wro->originZ + s.sectorZ * 1024 + 512;
            for (const LevelItem& b : in.items)
            {
                if (!isCarrierType(b.typeId)) continue;
                if (std::abs(b.x - cx) < 1024 && std::abs(b.z - cz) < 1024 &&
                    std::abs(b.y - s.floorY) < 512)
                    return false;
            }
        }
    }

    // No item on a fire's sector: without this, a pickup can land inside a
    // flame. The flame family 120..125 - Flame, the three flame emitters,
    // the gas cooker (COOKER_FLAME) and Burning Roots - burns in place; a
    // pickup there is a trap, not a find. Steam emitters (365) stay
    // deliberately allowed: the vertical ones are harmless scenery, and a
    // blanket ban would bar half the corridors in the ship levels.
    for (const LevelItem& fb : in.items)
    {
        if (fb.typeId < 120 || fb.typeId > 125) continue;
        if (fb.room == s.room && fb.sectorX == s.sectorX && fb.sectorZ == s.sectorZ)
            return false;
    }


    // Behind a deadstop lies nothing that could be a key. Applies to every
    // type with a constraint and additionally to the wall grabs, which have
    // no constraint.
    // Exempt types pass by here: the island is meant for other types, and
    // the exempt one needs the opposite. Its own bound stands elsewhere
    // (ISLAND_GROUP / bannedSectors).
    if ((k != nullptr || itemOcb == 2 || itemOcb == 9) &&
        !in.keyBarredExemptTypes.count(typeId) &&
        in.keyBarredSectors.count(secKey(s.room, s.sectorX, s.sectorZ)))
        return false;

    // The weapon ban hangs off the LEVEL, not the type constraint: a
    // shotgun is no key and would otherwise not be checked at all.
    if (in.weaponTypes.count(typeId) && in.noWeaponRooms.count(s.room)) return false;

    // No item on slides (slope > 2 - Lara cannot pick anything up there) or
    // above the collision-free triangle of a NOCOLF sector. Walkable slopes
    // are allowed - the emitters compute their y onto the true floor height
    // with floorAt. What matters is the LANDING POINT (sector center plus
    // type offset), because with splits each triangle has its own slopes.
    // Sits BEFORE the constraint branch like typeNoRooms and thus applies
    // on every placement path - keys, pickups, injections, scattering and
    // abundance.
    {
        std::map<int, PlacementOffset>::const_iterator o = in.typeOffset.find(typeId);
        const PlacementOffset po = (o == in.typeOffset.end()) ? PlacementOffset() : o->second;
        int y = 0;
        if (!floorAt(in, s, 512 + po.dx, 512 + po.dz, y)) return false;
    }

    // Bans of individual types for individual rooms. Sits deliberately
    // BEFORE the constraint branch and thus applies on every placement
    // path, including scattering and abundance, which arrive here without a
    // constraint.
    {
        std::map<int, std::set<int>>::const_iterator f = in.typeNoRooms.find(typeId);
        if (f != in.typeNoRooms.end() && f->second.count(s.room)) return false;

        std::map<int, std::set<long long>>::const_iterator g = in.typeNoSectors.find(typeId);
        if (g != in.typeNoSectors.end() &&
            g->second.count(secKey(s.room, s.sectorX, s.sectorZ))) return false;
    }

    if (!k) return true;

    const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
    if (s.zone > k->maxZone) return false;
    if (!k->allowRooms.empty() &&
        !std::binary_search(k->allowRooms.begin(), k->allowRooms.end(), s.room)) return false;
    if (std::binary_search(k->excludeRooms.begin(), k->excludeRooms.end(), s.room)) return false;
    if (!k->islandSectors.empty() && !k->islandSectors.count(sk)) return false;
    if (k->bannedSectors.count(sk)) return false;
    if (k->forbiddenSectors.count(sk)) return false;
    // Smaller y lies higher: a sector below the bound is too high. It
    // applies only within its scope, and everywhere without one.
    if (k->hasMinFloorY && s.floorY < k->minFloorY &&
        (k->minFloorYScope.empty() || k->minFloorYScope.count(sk))) return false;
    if (mustBeOnIsland && !k->requireInIsland.count(sk)) return false;
    return true;
}

bool Rando::buildMarkerPatch(const RandoInput& in,
                             int fillType,
                             JsonValue& patchOut,
                             std::vector<MarkerCount>& perType,
                             int& fillSectors)
{
    perType.clear();
    fillSectors = 0;
    patchOut = JsonValue::makeObject();
    JsonValue adds = JsonValue::makeArray();

    std::map<int, RoomOrigin> origin;
    for (const RoomOrigin& r : in.roomOrigins) origin[r.room] = r;

    auto offFor = [&](int typeId)
    {
        std::map<int, PlacementOffset>::const_iterator f = in.typeOffset.find(typeId);
        return f == in.typeOffset.end() ? PlacementOffset() : f->second;
    };

    auto marker = [&](int typeId, const ZoneSector& s)
    {
        std::map<int, RoomOrigin>::const_iterator ro = origin.find(s.room);
        if (ro == origin.end()) return;
        const PlacementOffset po = offFor(typeId);
        int yFloor = 0;
        Rando::floorAt(in, s, 512 + po.dx, 512 + po.dz, yFloor);
        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro->second.originX) + s.sectorX * 1024 + 512 + po.dx));
        a.set("y", JsonValue::makeInt(yFloor + po.dy));
        a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro->second.originZ) + s.sectorZ * 1024 + 512 + po.dz));
        a.set("ocb", JsonValue::makeInt(0));
        adds.push(a);
    };

    // Layer 1: the fill type onto EVERY valid sector. Not only onto those
    // no constraint covers - otherwise the marker would be missing wherever
    // a key item may ALSO lie, and those sectors would look as if they were
    // barred for ordinary pickups.
    if (fillType > 0)
    {
        for (const ZoneSector& s : in.sectors) { marker(fillType, s); ++fillSectors; }
    }

    // Layer 2: per constraint type its own, narrower area. Checked with the
    // same function the placement uses.
    //
    // If a type has a requireInIsland, the mandatory area is drawn, i.e.
    // the usual area and the island. Formally a second instance of the same
    // type could also lie outside - requireInIsland only says "one of them
    // there". The narrower set is drawn anyway: it is the one that was
    // curated and needs checking, and it is exactly what the PowerShell
    // tool draws too, against which the curation is compared. Two tools
    // drawing the same level differently would be worse than a formal
    // fuzziness.
    for (const KeyConstraint& k : in.constraints)
    {
        const bool mandatoryArea = k.islandSectors.empty() && !k.requireInIsland.empty();
        MarkerCount mc;
        mc.typeId = k.typeId;
        std::set<int> rooms;
        for (const ZoneSector& s : in.sectors)
        {
            if (!sectorAllowedFor(in, &k, s, k.typeId, mandatoryArea)) continue;
            marker(k.typeId, s);
            ++mc.sectors;
            rooms.insert(s.room);
        }
        mc.rooms = static_cast<int>(rooms.size());
        perType.push_back(mc);
    }

    // Layer 3: types without a constraint that still have their own
    // placement rules - the wall grabs (OCB 2 and 9).
    //
    // andy3's chalk (172) and crowbar (174) are the case: tools without a
    // pickup, so ZoneAnalysis derives no zone border for them and they stand
    // in no constraint. Layer 2 therefore never drew them - in the debug
    // level they were missing entirely, although they very much have their
    // own rules (wall edge, standing height 768, no water) and the deadstop
    // hits them via the OCB branch as well. Drawing them here shows that no
    // break was built for these types either.
    //
    // Their OCB comes from the vanilla items - it is what triggers the rule
    // in the first place; checked with the same function as the real
    // placement, only without a constraint and with the OCB as an argument.
    {
        // PINNED items do not count: a type whose occurrences all stay put
        // travels nowhere and must not get an area in the debug level
        // either. The same arithmetic as in run() further down - hand
        // curation plus trigger pinning, minus the coupled triggers and the
        // preset code bits.
        //
        // Two cases from the game require it: Andrea3's typeId 220 hangs on
        // the LevelEnd trigger and is held via triggerPinned - it stands in
        // neither VANILLA_TYPES nor fixedItems, so a type list would not
        // have found it. joby3's crowbar (172), by contrast, stands in
        // both.
        std::set<int> nailedDown(in.triggerPinned);
        for (int i : in.fixedItems) nailedDown.insert(i);

        for (std::map<int, int>::const_iterator f = in.flagPresets.begin(); f != in.flagPresets.end(); ++f)
            if (std::find(in.fixedItems.begin(), in.fixedItems.end(), f->first) == in.fixedItems.end())
                nailedDown.erase(f->first);

        for (const CoupledTrigger& c : in.coupledTriggers)
            if (std::find(in.fixedItems.begin(), in.fixedItems.end(), c.itemIndex) == in.fixedItems.end())
                nailedDown.erase(c.itemIndex);

        std::map<int, int> wallTypes;      // typeId -> OCB
        for (const LevelItem& it : in.items)
        {
            if (nailedDown.count(it.index)) continue;
            // Only pickable items. OCB 2 and 9 are no marker for wall
            // grabs but ordinary values that doors, enemies and scenery
            // carry too - without this check the debug marker fills half
            // the level, well past items, keys and puzzles.
            if (!isPickupType(it.typeId)) continue;
            if (it.ocb != 2 && it.ocb != 9) continue;

            bool hasConstraint = false;
            for (const KeyConstraint& k : in.constraints)
                if (k.typeId == it.typeId) { hasConstraint = true; break; }

            if (!hasConstraint) wallTypes[it.typeId] = it.ocb;
        }

        for (std::map<int, int>::const_iterator w = wallTypes.begin(); w != wallTypes.end(); ++w)
        {
            MarkerCount mc;
            mc.typeId = w->first;
            std::set<int> rooms;

            for (const ZoneSector& s : in.sectors)
            {
                if (!sectorAllowedFor(in, nullptr, s, w->first, false, w->second)) continue;
                marker(w->first, s);
                ++mc.sectors;
                rooms.insert(s.room);
            }

            mc.rooms = static_cast<int>(rooms.size());
            perType.push_back(mc);
        }
    }

    patchOut.set("itemAdds", adds);
    return true;
}

bool Rando::run(const RandoInput& in,
                const RandoOptions& opt,
                uint64_t seed,
                JsonValue& patchOut,
                RandoReport& report)
{
    report = RandoReport();

    // The level name in lower case - several blocks below decide on it
    // (weapon scattering, outfit, weapon rules).
    std::string lowerName;
    for (char c : in.level) lowerName.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c);

    // Every level gets its own dice stream. A single global seed feeding
    // every level run the same draw sequence would, since the enemy pools
    // are nearly identical, give all levels nearly the same enemy types in
    // the same order, as if one set had been picked and applied to all of
    // them equally. FNV-1a over the level name decouples the streams;
    // everything stays deterministic: the same seed still produces the
    // same game, just a different one per level.
    uint64_t levelSeed = seed;
    for (char c : lowerName)
    {
        levelSeed ^= static_cast<unsigned char>(c);
        levelSeed *= 0x100000001B3ull;
    }
    Rng rng(levelSeed);

    std::map<int, RoomOrigin> origin;
    for (const RoomOrigin& r : in.roomOrigins) origin[r.room] = r;

    std::map<int, const KeyConstraint*> conByType;
    for (const KeyConstraint& k : in.constraints) conByType[k.typeId] = &k;

    std::set<int> pinned(in.triggerPinned);
    for (int i : in.fixedItems) pinned.insert(i);

    // The coupled move lifts exactly the pinning that rule 2 imposes: the
    // item may travel because its trigger comes along. The hand curation
    // (fixedItems) stays untouched by that - whoever stands there stays
    // put, whatever might work for its trigger.
    // Preset code bits lift the pinning too: the condition the trigger
    // would have satisfied is thereby already satisfied.
    for (std::map<int, int>::const_iterator f = in.flagPresets.begin(); f != in.flagPresets.end(); ++f)
        if (std::find(in.fixedItems.begin(), in.fixedItems.end(), f->first) == in.fixedItems.end())
            pinned.erase(f->first);

    std::map<int, const CoupledTrigger*> coupled;
    for (const CoupledTrigger& c : in.coupledTriggers)
    {
        if (std::find(in.fixedItems.begin(), in.fixedItems.end(), c.itemIndex) != in.fixedItems.end())
            continue;
        coupled[c.itemIndex] = &c;
        pinned.erase(c.itemIndex);
    }

    // OCB 5 and 10: such pickups cannot be picked up in vanilla at all.
    // InitialisePickup (game/init.cpp) sets an OCB-5 item to ITEM_INVISIBLE
    // and returns; PickUpCollision (game/pickup.cpp) bails out immediately
    // at ocb 5 and 10. They are only unlocked by something else. Moving
    // them and, as with every moved item, setting the OCB to 0 would leave
    // them lying there visible and pickable from then on. That is no
    // longer a change of place but a change of behavior, so in doubt they
    // are barred instead of unlocked.
    //
    // Exactly this bar is lifted by a flagPresets entry, and only by it:
    // there the curation looked up what would have unlocked the item and
    // relieved it of the condition (Andrea2 item 62 - the HeavyTrigger
    // under it hangs on the Guardian's death). With preset code bits and a
    // zeroed OCB it lies visible from the start, which is otherwise
    // exactly what is being prevented here - hence it is a hand decision
    // and no derived rule.
    for (const LevelItem& it : in.items)
    {
        const int ocb = it.ocb & 0x3F;
        if ((ocb == 5 || ocb == 10) && !in.flagPresets.count(it.index))
            pinned.insert(it.index);
    }

    // Free sectors. Sorted order is mandatory: the random pick accesses by
    // index, and a different starting order would be a different result for
    // the same seed.
    std::vector<ZoneSector> pool = in.sectors;
    std::sort(pool.begin(), pool.end(), [](const ZoneSector& a, const ZoneSector& b) {
        if (a.room != b.room) return a.room < b.room;
        if (a.sectorX != b.sectorX) return a.sectorX < b.sectorX;
        return a.sectorZ < b.sectorZ;
    });
    std::set<long long> used;

    // --- Bar the sectors of enemies that can vanish with cargo ---
    //
    // GetCarriedItems (setup.cpp) attaches every pickup within 512 units
    // to the nearest enemy at level start: RemoveDrawnItem,
    // room_number = NO_ROOM - the item is then nothing but its cargo and
    // only drops out again at its death.
    //
    // For all other enemies this is harmless since the antitrigger can no
    // longer delete them and the two own death paths lay their cargo down.
    // One case remains: Larson and Pierre, who keep their OCB - with it
    // they find Core's own AI_AMBUSH marker and flee the level at 40 hit
    // points. Exactly three specimens in the whole game are affected
    // (Andrea1 34 and 35, Andrea2 76 - the scripted miniboss, which as a
    // hand-curated item is never swapped anyway).
    //
    // No pickup goes onto their sector, therefore. Three barred tiles out
    // of several hundred cost nothing; a key item leaving the level with a
    // fleeing Larson costs the whole playthrough.
    for (const LevelItem& it : in.items)
    {
        if ((it.typeId != 45 && it.typeId != 47) || it.ocb == 0) continue;
        used.insert(secKey(it.room, it.sectorX, it.sectorZ));
        report.notes.push_back(fmt("sector %d,%d,%d barred: item %d (typeId %d, OCB %d) can "
                                   "flee and would take cargo along.",
                                   it.room, it.sectorX, it.sectorZ, it.index, it.typeId, it.ocb));
    }

    // Which items are candidates at all?
    std::vector<LevelItem> movableKey, movableOther;
    for (const LevelItem& it : in.items)
    {
        if (!isPickupType(it.typeId)) continue;
        if (pinned.count(it.index)) { ++report.pinnedItems; continue; }
        if (conByType.count(it.typeId)) movableKey.push_back(it);
        else                            movableOther.push_back(it);
    }

    // RequireTypes: at least ONE instance of these types must lie inside
    // the island group. joby2 forces the rule: Key Item8 unlocks the
    // flipmap via the crane that opens the rest of the level. If no
    // instance lies before it, Lara never reaches the crane.
    //
    // As a sector filter this cannot be expressed. A filter answers "may
    // THIS instance go here?", but the demand concerns all instances of a
    // type together - four individually admissible places can still be
    // jointly wrong. Hence one instance is chosen and its candidate set
    // restricted to the island.
    //
    // If a pinned item already satisfies the demand, nothing is to be done
    // - it stays where it is. Otherwise the choice falls on the movable
    // instance with the smallest index, so it does not depend on the seed.
    std::map<int, int> forceIsland;      // typeId -> item that must go onto the island
    if (opt.shuffleKeyItems)
    {
        for (const KeyConstraint& k : in.constraints)
        {
            if (k.requireInIsland.empty()) continue;

            bool satisfied = false;
            int  firstMovable = -1;
            for (const LevelItem& it : in.items)      // sorted by index
            {
                if (it.typeId != k.typeId || !isPickupType(it.typeId)) continue;
                if (pinned.count(it.index))
                {
                    if (k.requireInIsland.count(secKey(it.room, it.sectorX, it.sectorZ))) satisfied = true;
                    continue;
                }
                if (firstMovable < 0) firstMovable = it.index;
            }
            if (satisfied) continue;
            if (firstMovable < 0)
            {
                report.ok = false;
                report.error = fmt("type %d must lie in the island group at least once, but has "
                                   "neither a pinned occurrence there nor a movable instance.", k.typeId);
                return false;
            }
            forceIsland[k.typeId] = firstMovable;
            report.notes.push_back(fmt("type %d: item %d is forced into the island group (mandatory occurrence).",
                                       k.typeId, firstMovable));
        }
    }

    JsonValue edits = JsonValue::makeArray();
    JsonValue adds = JsonValue::makeArray();

    // Each item's final state after the run. Moved ones get their new
    // position, all others keep the vanilla one - the carrier check at the
    // very bottom needs both together.
    std::map<int, LevelItem> finalOf;
    for (const LevelItem& it : in.items) finalOf[it.index] = it;

    // Some objects do not carry their mesh origin where the model assumes
    // it: they then stick in the floor or stand at the sector edge instead
    // of the middle. Applies to EVERY placement of that type - moved or
    // injected - hence central here and not at the single entry.
    auto offFor = [&](int typeId)
    {
        std::map<int, PlacementOffset>::const_iterator f = in.typeOffset.find(typeId);
        return f == in.typeOffset.end() ? PlacementOffset() : f->second;
    };

    auto placeAt = [&](const LevelItem& it, const ZoneSector& s)
    {
        const RoomOrigin& ro = origin[s.room];
        const PlacementOffset po = offFor(it.typeId);
        const int px = ro.originX + s.sectorX * 1024 + 512 + po.dx;
        // The true floor height at the landing point instead of the sector
        // base value: on a walkable slope the item would hang in the air
        // otherwise.
        int pyFloor = 0;
        Rando::floorAt(in, s, 512 + po.dx, 512 + po.dz, pyFloor);
        const int py = pyFloor + po.dy;
        const int pz = ro.originZ + s.sectorZ * 1024 + 512 + po.dz;

        // Rule 3: pedestal and hiding OCBs do not survive a move. Types
        // from KEEP_OCB are the exception, where the OCB does not describe
        // the old spot but the pickup style - without it the object cannot
        // be picked up at the new place at all.
        const int newOcb = in.keepOcbTypes.count(it.typeId) ? it.ocb : 0;

        LevelItem& fin = finalOf[it.index];
        fin.room = s.room;
        fin.x = px;
        fin.y = py;
        fin.z = pz;
        fin.ocb = newOcb;
        // Carry the sector along, not only the world coordinate: the
        // coupled trigger move needs it, and computing it back from x/z
        // afterwards would be a second source of the same truth.
        fin.sectorX = s.sectorX;
        fin.sectorZ = s.sectorZ;

        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(it.index));
        e.set("room", JsonValue::makeInt(s.room));
        e.set("x", JsonValue::makeInt(px));
        e.set("y", JsonValue::makeInt(py));
        e.set("z", JsonValue::makeInt(pz));
        e.set("ocb", JsonValue::makeInt(newOcb));

        // Write the preset code bits along, or the item would again depend
        // on its old trigger at the new place.
        {
            std::map<int, int>::const_iterator fp = in.flagPresets.find(it.index);
            if (fp != in.flagPresets.end()) e.set("flags", JsonValue::makeInt(fp->second));
        }

        // Wall grabs need the angle towards the wall. Otherwise a
        // moved item keeps its old orientation - which is right for
        // ordinary pickups (the engine turns them onto Lara when picking
        // up anyway), but not for OCB 2 and 9: there PickUpCollision
        // checks the real angle (+-30 degrees), and the model should stick
        // in the wall. If the sector has several wall edges, chance
        // decides.
        if ((newOcb == 2 || newOcb == 9) && !s.wallAngles.empty())
        {
            const size_t w = static_cast<size_t>(rng.below(s.wallAngles.size()));
            e.set("angle", JsonValue::makeInt(s.wallAngles[w]));
        }

        edits.push(e);
        used.insert(secKey(s.room, s.sectorX, s.sectorZ));
    };

    // An item's candidate sectors under its constraints.
    auto candidatesFor = [&](const LevelItem& it, std::vector<const ZoneSector*>& out)
    {
        out.clear();
        auto ci = conByType.find(it.typeId);
        const KeyConstraint* k = (ci == conByType.end()) ? nullptr : ci->second;

        auto fi = forceIsland.find(it.typeId);
        const bool mustBeOnIsland = (fi != forceIsland.end() && fi->second == it.index);
        // If the trigger travels along, only a sector that can accept it is
        // a candidate. Otherwise the placement picks a spot where the patch
        // later aborts - and an aborted patch mid-run is worse than a
        // narrower choice.
        const bool needsTriggerRoom = coupled.count(it.index) != 0;

        for (const ZoneSector& s : pool)
        {
            const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
            if (used.count(sk)) continue;
            if (needsTriggerRoom && !in.triggerSafeSectors.count(sk)) continue;
            if (!Rando::sectorAllowedFor(in, k, s, it.typeId, mustBeOnIsland, it.ocb)) continue;
            out.push_back(&s);
        }
    };

    // A minimum distance between the important finds - keys, puzzle pieces
    // and weapons.
    //
    // Drawing uniformly from all allowed sectors without looking at the
    // ones already placed leaves finds clustered too often. Measured over
    // 20 Andrea2 seeds, 6 of 200 pairs landed closer than three sectors and
    // 2 in the same room - about chance expectation, but it stands out in
    // play, because a close pair sticks in memory and the well-spread
    // pairs do not.
    //
    // No hard ban, but stages: first try 5 sectors of distance, then 3,
    // then 2, finally none. A tight zone thus stays placeable - a run
    // failing on a taste rule would be worse than two finds lying closer
    // together.
    std::vector<std::pair<int, int>> placedXZ;   // world coordinates of the previous ones

    auto pickWithSpacing = [&](const std::vector<const ZoneSector*>& c) -> const ZoneSector*
    {
        static const int stages[] = { 5120, 3072, 2048, 0 };
        for (int st : stages)
        {
            if (st == 0 || placedXZ.empty())
                return c[static_cast<size_t>(rng.below(c.size()))];

            std::vector<const ZoneSector*> far;
            for (const ZoneSector* s : c)
            {
                const RoomOrigin& ro = origin[s->room];
                const int sx = ro.originX + s->sectorX * 1024 + 512;
                const int sz = ro.originZ + s->sectorZ * 1024 + 512;
                bool ok = true;
                for (size_t i = 0; i < placedXZ.size() && ok; ++i)
                {
                    const long long dx = sx - placedXZ[i].first;
                    const long long dz = sz - placedXZ[i].second;
                    if (dx * dx + dz * dz < static_cast<long long>(st) * st) ok = false;
                }
                if (ok) far.push_back(s);
            }
            if (!far.empty()) return far[static_cast<size_t>(rng.below(far.size()))];
        }
        return c[static_cast<size_t>(rng.below(c.size()))];
    };

    auto noteSpot = [&](const ZoneSector& s)
    {
        const RoomOrigin& ro = origin[s.room];
        placedXZ.push_back({ ro.originX + s.sectorX * 1024 + 512,
                              ro.originZ + s.sectorZ * 1024 + 512 });
    };

    // The key items first, and among them the most constrained first.
    // Otherwise a free type occupies exactly the last sector a narrower
    // type would still have needed, and the placement fails on something
    // easily avoidable.
    if (opt.shuffleKeyItems)
    {
        std::vector<std::pair<size_t, size_t>> order;  // (candidate count, position)
        for (size_t i = 0; i < movableKey.size(); ++i)
        {
            std::vector<const ZoneSector*> c;
            candidatesFor(movableKey[i], c);
            order.push_back({ c.size(), i });
        }
        std::sort(order.begin(), order.end(), [&](const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) {
            if (a.first != b.first) return a.first < b.first;
            return movableKey[a.second].index < movableKey[b.second].index;   // stable tie
        });

        for (const auto& o : order)
        {
            const LevelItem& it = movableKey[o.second];
            std::vector<const ZoneSector*> c;
            candidatesFor(it, c);
            if (c.empty())
            {
                report.ok = false;
                report.error = fmt("no valid place for item %d (typeId %d) - constraints too tight.",
                                   it.index, it.typeId);
                return false;
            }
            const ZoneSector* w = pickWithSpacing(c);
            placeAt(it, *w);
            noteSpot(*w);
            ++report.movedKeyItems;
        }
    }

    if (opt.shuffleOtherPickups)
    {
        for (const LevelItem& it : movableOther)
        {
            std::vector<const ZoneSector*> c;
            candidatesFor(it, c);
            if (c.empty())
            {
                // No hard error: ordinary pickups are not progress-relevant,
                // an unmoved item simply stays where it was.
                report.notes.push_back(fmt("item %d (typeId %d) stays in place - no free sectors left.",
                                           it.index, it.typeId));
                continue;
            }
            // Weapons count towards the spread, ordinary ammo and medipacks
            // do not - those may lie close together, they are no find one
            // searches for.
            if (in.weaponTypes.count(it.typeId))
            {
                const ZoneSector* w = pickWithSpacing(c);
                placeAt(it, *w);
                noteSpot(*w);
            }
            else
                placeAt(it, *c[static_cast<size_t>(rng.below(c.size()))]);
            ++report.movedOtherItems;
        }
    }

    // Mandatory injections. These append to the item list and therefore
    // shift no existing indices - unlike a removal.
    for (const Injection& j : in.injections)
    {
        // Room selection: either the list (seed searches within) or the one room.
        const bool fromList = !j.rooms.empty();
        std::set<int> allowedRooms(j.rooms.begin(), j.rooms.end());

        std::vector<const ZoneSector*> c;
        for (const ZoneSector& s : pool)
        {
            if (fromList ? !allowedRooms.count(s.room) : (s.room != j.room)) continue;
            // With a fixed sector only it counts, and whether it is already
            // occupied does not matter: several injections may stack there.
            if (j.hasSector)
            {
                if (s.sectorX == j.sectorX && s.sectorZ == j.sectorZ) c.push_back(&s);
                continue;
            }
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            // Type bans apply here too: a mandatory injection must not go
            // into a room the curation barred for exactly this type.
            if (!Rando::sectorAllowedFor(in, nullptr, s, j.typeId, false)) continue;
            c.push_back(&s);
        }
        if (c.empty())
        {
            report.ok = false;
            report.error = j.hasSector
                ? fmt("mandatory injection typeId %d: sector %d,%d,%d is no valid spot.",
                      j.typeId, j.room, j.sectorX, j.sectorZ)
                : fromList
                    ? fmt("mandatory injection typeId %d: no free valid sector in the %d "
                          "named rooms.", j.typeId, static_cast<int>(j.rooms.size()))
                    : fmt("mandatory injection typeId %d: no free valid sector in room %d.",
                          j.typeId, j.room);
            return false;
        }
        const ZoneSector& s = j.hasSector ? *c[0] : *c[static_cast<size_t>(rng.below(c.size()))];
        const RoomOrigin& ro = origin[s.room];

        const PlacementOffset po = offFor(j.typeId);
        int yFloor = 0;
        Rando::floorAt(in, s, 512 + po.dx, 512 + po.dz, yFloor);
        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(j.typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512 + po.dx));
        a.set("y", JsonValue::makeInt(yFloor + po.dy + j.yOffset));
        a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s.sectorZ * 1024 + 512 + po.dz));
        a.set("ocb", JsonValue::makeInt(0));
        adds.push(a);
        // Only automatically chosen sectors are occupied. A hand-set one
        // stays free, or only a single item could lie there.
        if (!j.hasSector) used.insert(secKey(s.room, s.sectorX, s.sectorZ));
        ++report.injected;
        report.notes.push_back(j.hasSector
            ? fmt("injected: typeId %d onto %d,%d,%d (%s)", j.typeId, s.room, s.sectorX, s.sectorZ, j.reason.c_str())
            : fmt("injected: typeId %d into room %d (%s)", j.typeId, j.room, j.reason.c_str()));
    }

    // --- Scatter weapons ---
    //
    // The seed determines which of the weapons made usable by the import a
    // level additionally receives as a pickup, and how much ammo with it.
    // Only weapons the level does not lay out as an item anyway - existing
    // ones are mixed normally, and more ammo for existing weapons is the
    // business of item abundance.
    //
    // Pistols (334) are missing on purpose: Lara starts with them
    // everywhere as soon as the model slot is there - and rich2 must not
    // have them at all (slot 1 there carries Core's scripted second HK, see
    // the weapon rules).
    //
    // The sector choice runs via sectorAllowedFor without a constraint: so
    // exactly one rule applies, the per-room weapon ban (noWeaponRooms,
    // rich1's Scientist). Budget: MAX_ITEMS is 512; the run stops at 500
    // here, and the extra enemies placed later are already
    // subtracted.
    // A free sector admissible for this type. Without a constraint, so
    // exactly one rule applies: the per-room weapon ban (noWeaponRooms,
    // rich1's Scientist). Used by the weapon scattering AND the item
    // abundance, hence it stands before both.
    auto freeSpot = [&](int typeId) -> const ZoneSector* {
        std::vector<const ZoneSector*> c;
        for (const ZoneSector& s : pool)
        {
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            if (!Rando::sectorAllowedFor(in, nullptr, s, typeId, false)) continue;
            c.push_back(&s);
        }
        return c.empty() ? nullptr : c[static_cast<size_t>(rng.below(c.size()))];
    };

    if (opt.allWeapons && lowerName != "joby4")
    {
        struct WScatter { int weapon; int ammo[2]; };
        static const WScatter kScatter[] = {
            { 336, { 337,  -1 } },     // Uzi + Uzi ammo
            { 338, { 339, 340 } },     // shotgun + normal/wideshot
            { 345, { 346,  -1 } },     // HK + HK ammo
            { 347, { 348,  -1 } },     // Deagle + Deagle ammo
        };

        std::set<int> alreadyThere;
        for (const LevelItem& it : in.items) alreadyThere.insert(it.typeId);

        int budget = 500 - static_cast<int>(in.items.size())
                         - static_cast<int>(adds.size())
                         - (opt.addEnemies ? opt.enemiesPerLevel : 0);

        auto put = [&](int typeId, const ZoneSector& s)
        {
            const RoomOrigin& ro = origin[s.room];
            const PlacementOffset po = offFor(typeId);
            int yFloor = 0;
            Rando::floorAt(in, s, 512 + po.dx, 512 + po.dz, yFloor);
            JsonValue a = JsonValue::makeObject();
            a.set("typeId", JsonValue::makeInt(typeId));
            a.set("room", JsonValue::makeInt(s.room));
            a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512 + po.dx));
            a.set("y", JsonValue::makeInt(yFloor + po.dy));
            a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s.sectorZ * 1024 + 512 + po.dz));
            a.set("ocb", JsonValue::makeInt(0));
            adds.push(a);
            used.insert(secKey(s.room, s.sectorX, s.sectorZ));
            ++report.weaponsScattered;
            --budget;
        };

        for (const WScatter& w : kScatter)
        {
            if (alreadyThere.count(w.weapon)) continue;    // the level lays it out itself
            if (rng.below(2) == 0) continue;               // coin toss per weapon

            if (budget < 2) { report.notes.push_back("weapon scattering: item budget exhausted."); break; }
            const ZoneSector* sw = freeSpot(w.weapon);
            if (!sw) { report.notes.push_back(fmt("weapon scattering: no place for typeId %d.", w.weapon)); continue; }
            put(w.weapon, *sw);

            const int packs = 1 + static_cast<int>(rng.below(3));   // 1..3
            int laid = 0;
            for (int n = 0; n < packs && budget > 0; ++n)
            {
                const int ammo = (w.ammo[1] < 0) ? w.ammo[0]
                              : w.ammo[rng.below(2)];
                const ZoneSector* sm = freeSpot(ammo);
                if (!sm) break;
                put(ammo, *sm);
                ++laid;
            }
            report.notes.push_back(fmt("weapon scattering: typeId %d + %d ammo packs.", w.weapon, laid));
        }
    }

    // --- Item abundance: scatter extra equipment ---
    //
    // Some levels are decidedly sparse: Andy2 has all of ten pickups across
    // 254 rooms, three of them secrets - with 348 valid spots after the
    // curation. extraPickups lays out up to N further consumables.
    //
    // Only what also has a MODEL in the level is scattered - otherwise
    // there would be nothing to draw. Those are two sources: what the level
    // lays out anyway, and what the weapon import brings in.
    //
    // The weapon import is the more important case. Ammo lies in vanilla in
    // only 3 to 5 of 15 levels (measured: Uzi 5, shotgun 3, HK 3, Deagle 4)
    // - since every level receives the full weapon set, over ten levels
    // would stand there with weapons and no matching ammo. Hence the set's
    // ammo kinds get a fixed base weight, independent of the vanilla stock.
    //
    // Pistol ammo (335) drops out: pistols have unlimited shots, and not a
    // single piece of it lies in any of the 15 levels. For rich2, pistols
    // and their ammo are barred entirely - its slot 1 carries Core's
    // scripted second HK, and the mere presence of the pistol item would
    // give Lara starting pistols (InitialiseLara, setup.cpp).
    //
    // The weapons themselves only enter the pot if the level lays them out
    // nowhere yet: the scattering above tosses a coin per weapon, and with
    // bad luck the weapon is imported but nowhere to be found. This way it
    // is after all - and there are no duplicates.
    //
    // Everything below typeId 334 stays out: keys, puzzle pieces, quest
    // items and above all the secrets (PICKUP_ITEM4 = 223) - an extra
    // secret would falsify the level count. Also out: one-offs like
    // lasersight/binoculars/silencer and the crossbow bolt (projectile, no
    // pickup).
    //
    // "Fewer than vanilla" deliberately does not exist: removing items
    // would shift all subsequent indices and thereby break every
    // "Object N" reference of the triggers - the ground rule of the whole
    // tool.
    //
    // joby4 stays out as everywhere else: Lara is in the diving suit there
    // and carries no weapons. The ammo slots do lie around in the file, but
    // supplies for weapons that do not exist in the level would be mere
    // decoration.
    if (opt.extraPickups > 0 && lowerName == "joby4")
        report.notes.push_back("item abundance: joby4 stays vanilla (diving suit).");
    else if (opt.extraPickups > 0)
    {
        // All weapons and ammo kinds TR5 knows as pickups go into the pot,
        // whatever the level can carry. For every weapon its ammo - a
        // weapon without supplies is dead weight after two fights.
        struct ScatterWeapon { int weapon; int ammo[2]; };
        static const ScatterWeapon kWeaponGoods[] = {
            { 336, { 337,  -1 } },     // Uzi
            { 338, { 339, 340 } },     // shotgun normal / wideshot
            { 341, { 342, 343 } },     // crossbow normal / explosive
            { 345, { 346,  -1 } },     // HK
            { 347, { 348,  -1 } },     // Deagle
        };
        // Consumables without an associated weapon.
        // 355 and not 354. The two look alike in the catalogue - "Flare
        // Item" and "Flare Inv Item" - but in the level they are different
        // things: 354 is the single flare that fires the animation off by
        // itself, 355 are the flares one picks up. Vanilla Andrea1 carries
        // one 354 and several 355, which says the same. Scattering 354
        // hands the player a flare that lights itself.
        static const int kMisc[] = { 349, 350, 355 };   // big medi, small medi, flares

        // What lies in the level - vanilla stock plus what weapon
        // scattering and mandatory injections just added?
        std::map<int, int> countOf;
        for (const LevelItem& it : in.items) ++countOf[it.typeId];
        for (size_t i = 0; i < adds.size(); ++i)
            ++countOf[static_cast<int>(adds[i]["typeId"].asInt())];

        // Can the level draw this type? Either the model slot already
        // stands in the file, or the weapon import brings it along (slots
        // from kWeaponRules further below: Andrea3 334-340, joby2 347/348,
        // rich1 345/346 - the crossbow is NOT included, it stays reserved
        // for the levels that carry it themselves).
        const bool weaponSetHere = opt.allWeapons && lowerName != "joby4";
        // The flare goes into the rich levels further down in this same
        // run (slots 7, 354, 355 from Andrea1). presentSlots is the state
        // before the patch, so without this check the scatter would refuse
        // to lay a flare it is about to make drawable, leaving the slots
        // present but no flares among them.
        const bool flareComesHere =
            (lowerName == "rich1" || lowerName == "rich2" || lowerName == "rich3");

        auto drawable = [&](int type) -> bool {
            if (in.presentSlots.count(type)) return true;
            if (type == 355 && flareComesHere) return true;
            if (!weaponSetHere) return false;
            switch (type)
            {
            case 334: case 335: case 336: case 337:
            case 338: case 339: case 340:
            case 345: case 346: case 347: case 348: return true;
            default: return false;
            }
        };

        // Lots per type: one lot per existing piece keeps the level's
        // character, the base weight provides supplies.
        std::map<int, int> lots;
        auto offer = [&](int type, int baseWeight)
        {
            if (!drawable(type)) return;
            std::map<int, int>::const_iterator f = countOf.find(type);
            lots[type] += baseWeight + (f == countOf.end() ? 0 : f->second);
        };

        for (const ScatterWeapon& w : kWeaponGoods)
        {
            for (int m = 0; m < 2; ++m)
                if (w.ammo[m] > 0) offer(w.ammo[m], 4);
            // The weapon itself only if it lies nowhere yet: the scattering
            // above tosses a coin per weapon, and with bad luck it would be
            // imported but nowhere to find. This way there are no
            // duplicates.
            if (!countOf.count(w.weapon)) offer(w.weapon, 2);
        }
        // A level that carries flares in vanilla brings its own count into
        // the lots on top of the base weight. The rich levels carry none, so
        // on the base weight alone they would draw far more rarely than
        // everywhere else - with twenty extra pickups, often not at all.
        // Flares are needed there for the glitched secrets, so the weight
        // stands in for the stock the level never had.
        for (int t : kMisc)
            offer(t, (t == 355 && flareComesHere) ? 8 : 2);

        // rich2: neither pistols nor their ammo, ever (see above).
        if (lowerName == "rich2") { lots.erase(334); lots.erase(335); }

        std::vector<int> pot;                 // draw pot, one entry per lot
        for (std::map<int, int>::const_iterator it = lots.begin(); it != lots.end(); ++it)
            for (int n = 0; n < it->second; ++n) pot.push_back(it->first);

        if (pot.empty())
            report.notes.push_back("item abundance: the level carries no scatterable "
                                   "consumables - nothing added.");
        else
        {
            const int typesBefore = static_cast<int>(std::set<int>(pot.begin(), pot.end()).size());
            int budget = 500 - static_cast<int>(in.items.size())
                             - static_cast<int>(adds.size())
                             - (opt.addEnemies ? opt.enemiesPerLevel : 0);
            int howMany = opt.extraPickups;
            if (howMany > budget) howMany = budget;

            for (int n = 0; n < howMany && !pot.empty(); ++n)
            {
                const int type = pot[static_cast<size_t>(rng.below(pot.size()))];
                const ZoneSector* s = freeSpot(type);
                if (!s)
                {
                    // There is nowhere left an allowed free sector for this
                    // type - in rich2, say, almost the whole level is a
                    // weapon-free zone, no ammo fits there. The type flies
                    // out of the pot, the rest keep rolling; an abort here
                    // would have cost the medipacks too because of ONE
                    // barred type. It cannot become free again, sectors are
                    // only ever occupied over the course, never freed.
                    pot.erase(std::remove(pot.begin(), pot.end(), type), pot.end());
                    --n;
                    continue;
                }
                const RoomOrigin& ro = origin[s->room];
                const PlacementOffset po = offFor(type);
                int yFloor = 0;
                Rando::floorAt(in, *s, 512 + po.dx, 512 + po.dz, yFloor);
                JsonValue a = JsonValue::makeObject();
                a.set("typeId", JsonValue::makeInt(type));
                a.set("room", JsonValue::makeInt(s->room));
                a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s->sectorX * 1024 + 512 + po.dx));
                a.set("y", JsonValue::makeInt(yFloor + po.dy));
                a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s->sectorZ * 1024 + 512 + po.dz));
                a.set("ocb", JsonValue::makeInt(0));
                adds.push(a);
                used.insert(secKey(s->room, s->sectorX, s->sectorZ));
                ++report.extraPickups;

                // A weapon exactly once: it sits in the pot so it can be
                // found at all - a second specimen would only be ammo with
                // an unnecessary model in front.
                bool isWeapon = false;
                for (const ScatterWeapon& w : kWeaponGoods) if (w.weapon == type) isWeapon = true;
                if (isWeapon)
                    pot.erase(std::remove(pot.begin(), pot.end(), type), pot.end());
            }
            report.notes.push_back(fmt("item abundance: %d of %d extra pickups scattered "
                                       "(%d scatterable types in the level).",
                                       report.extraPickups, opt.extraPickups, typesBefore));
        }
    }

    // Display names: see Rando::enemyKinds() at the end of the file - the
    // list there must grow with this table.
    //
    // --- Cross enemy mix: extend the pool with foreign types ---
    //
    // The global pool table lists, per type, the donor level that carries
    // it with vanilla instances - only there do model and samples lie
    // together. The sound entries are the known exceptions whose samples
    // even the donor lacks (Larson's death sounds, Pierre's never-shipped
    // shot), matched by hand from the wider soundboard. The Hydra (63) and
    // the chef (87) are in the pool for the reasons noted at their rows.
    // Scientist and Guardian stand nowhere in the pool for known reasons.
    //
    // Per level at most THREE foreign types, chosen by the seed: every type
    // costs texture pages and samples in the target, and the level should
    // keep its face. Only what is really used is imported later - and with
    // replace, so that even an existing but never-played slot (joby5's
    // guard without samples) comes completely from the donor.
    // "companions": slots the type must bring along. Mafia2 carries his
    // upper body as a swap half from MESHSWAP2 = slot 406 (BaddyObjects in
    // setup.cpp: meshes 7/10/13, meshswap_meshbits 0x2400 already from
    // Initialise) - without the slot the torso is invisible. Nobody else
    // uses MESHSWAP2, the import is conflict-free.
    //
    // MESHSWAP1, by contrast, is a SHARED slot with level-dependent content
    // (SWAT family: meshes 10/13; Imp: 10; TwoGun: 10<-2; gladiator: 0-15;
    // Roman god: 0-16; Hitman: 0-19) - it cannot simply be replaced without
    // breaking the target's native users. The consequences show at the Imp
    // head and the statues' damage meshes; the solution (own fallback slots
    // in the engine or mesh-exact insertion) remains open and is not part
    // of this table.
    // "swapFrom"/"swapDst": the type's swap halves. MESHSWAP1 (405) is
    // filled per level with the content of its own enemies - a foreign
    // enemy needs the set of a level that really carries him, and that
    // lands under the fallback slot swapDst at import. Every enemy has his
    // own fallback slot (soldier family 462-467 beyond the file boundary,
    // loners 401-404): sharing one slot per donor would let native users of
    // the same slot draw foreign hands (joby2 item 69). The engine prefers
    // it (MeshSwapSource in setup.cpp, mapping as
    // MESHSWAP_CROSS_* in objects.h - both sides must agree). swapFrom can
    // differ from the model donor: rich1 (model for guard and TwoGun) has
    // NO MESHSWAP1 itself - the guard hands come from joby3, TwoGun's
    // source mesh 2 from rich3.
    struct CrossSound  { int map; const char* fromLevel; int fromMap; };
    // animCount (0 = heuristic): explicit anim slice length for types whose
    // control reaches past the object boundary into the global anim table -
    // so far only the Imp (see its table entry).
    //
    // baseSlot (0 = none): the soldier family's anim base type. SWAT+ and
    // Mafia carry only a one-frame stub in the files - the engine fetches
    // their anims at runtime from the base type (setup.cpp: anim_index is
    // bent onto SWAT's set, else guard 460, else guard 37). Whoever imports
    // such a type must bring the base type along, or he runs without a
    // valid set. Import with replace=false: if the target already has the
    // base type (native or as its own cross import), it stays. The stubs'
    // base is the guard (460): he always carries the clean joby3 set, while
    // slot 37 may carry a different model per level (rich!) - measurement
    // data at row 37.
    // "tr3" (0 = from TR5 itself): the foreign-source tag from
    // Tr3Source::dinos(). If set, "from" is empty - the model does not come
    // from a TR5 level but at runtime from the player's own Tomb Raider 3
    // (or The Lost Artifact) installation. Everything else stands in the
    // dino table, so there is
    // only one truth.
    // "sourceSlot" (0 = same as typeId): the slot under which the model
    // lies at the donor. Needed for the engine slots beyond 460: the joby
    // guard lies under 37 in every donor but lives under 460 in the target
    // - only that way can both guard models exist side by side. Such
    // levels run only on this engine; trview copes with them as well.
    struct CrossEnemy { int typeId; const char* from; CrossSound sounds[2];
                         int companions[2]; const char* swapFrom; int swapDst;
                         int animCount; int baseSlot; const char* tr3;
                         int sourceSlot; };
    static const CrossEnemy kCross[] = {
        { 33, "rich3",   {},                                    {-1,-1},  "rich3",   462, 0, 0 },  // SWAT
        // Sound 88 (BIO_BREATHE_OUT): the SWAT+'s gas mask breathing in
        // dark rooms (SasControl, sas.cpp, hard-wired - it stands in no
        // anim command and therefore does not travel by itself). Without
        // the entry he breathes mutely in foreign levels.
        { 35, "rich3",   { {88,"rich3",88} },                   {-1,-1},  "rich3",   463, 0, 33 }, // Advanced SWAT (anims from SWAT)
        // Slot 37 carries the RICH model (blue vest, rich1). Core's sets
        // carry different DEAD HEIRS per level: anims that share their
        // frame pointer with a neighbor anim but read it with their own
        // (wrong) stride. With the native owner it never shows, because he
        // does not play those states - the stub family does.
        //
        // Using joby3 as the family's anim base instead would make guard
        // (37) and guard (460) twins, and in levels with a native rich
        // guard the import would overwrite the native model. Hence the
        // family hangs on the guard 460 instead (the stubs' baseSlot,
        // alias chain in setup.cpp), and 37 stays free to be rich.
        //
        // Why the rich model is safe on its own (AnimMap measurement):
        //   rich1 guard sick: +19..23, +24..30, +34/35, +41..45, +50..56 -
        //     the SPECIAL POSES (states 26-31) and state 35, Mafia's shot
        //     pose. The guard HIMSELF plays none of it: his own shot anim
        //     (+2) is healthy, and the special poses need a pose OCB - the
        //     rando always sets the wanted OCB on swap and extra enemies
        //     (kSpawnOcb, guard = 0). The set only became sick as a FAMILY
        //     BASE: model gone from keyframe 1, shadow stays, damage
        //     arrives - the blinking soldier from andy3 (three measurement
        //     runs).
        //   joby3 guard: combat block and special poses clean; dead rest
        //     only +23..28/+30..32/+34f/+57f. Field proof: the guard (460)
        //     carries exactly this set and is flawless in all poses.
        { 37, "rich1",   {},                                    {-1,-1},  "joby3",   465, 0, 0 },  // Guard (Blue Guard, rich model)
        // The second guard (engine slot 460 = BLUE_GUARD2): the joby model
        // from joby3 - there the guard is the family's anim anchor and
        // carries the full own set (Mafia fetches his anims from him).
        // joby2's guard cannot be copied (frame pointers, see Crane Guy).
        // No base type: he brings everything along and keeps his set even
        // next to SWAT/guard. He is the stub family's anim base (baseSlot
        // 460 at Mafia and Crane Guy), exactly because his set is always
        // the clean one from joby3. Swap halves (fire hands 10/13) like the
        // guard from joby3.
        { 460, "joby3",  {},                                    {-1,-1},  "joby3",   467, 0, 0, nullptr, 37 }, // Guard (Joby)
        // Laser shot (193) and impact (167): hard-wired in twogun.cpp, in
        // no anim command - without the entries the laser fires mutely in
        // foreign levels. Both samples are byte-identical in rich1 and
        // rich3; the Hydra (from Andrea2) also sets the 167, likewise
        // identical content.
        { 39, "rich1",   { {167,"rich3",167}, {193,"rich3",193} }, {-1,-1}, "rich3",  464, 0, 0 },  // Two Gun
        { 41, "Andrea1", {},                                    {-1,-1},  nullptr,   -1 },  // Dog
        { 45, "Andrea1", { {96,"rich1",85}, {98,"rich1",143} }, {-1,-1},  nullptr,   -1 },  // Larson
        { 47, "Andrea3", { {90,"joby5",45}, {91,"joby5",123} }, {-1,-1},  nullptr,   -1 },  // Pierre
        { 49, "joby3",   {},                                    {-1,-1},  "joby3",   466, 0, 460 }, // Soldier MG (Mafia, anims from the guard)
        { 51, "joby3",   {},                                    {406,-1}, nullptr,   -1 },  // Soldier Pistols (Mafia2) + MESHSWAP2
        // Crane Guy: the only TR5 enemy with a complete model that Core
        // never placed as an item - the same situation as Pierre. Model
        // only in joby2 (15 meshes, own anims), the control is SasControl,
        // he fires via state 3 like the Blue Guard (sas.cpp names him there
        // explicitly).
        //
        // baseSlot 460: he fetches his anim set at runtime from SWAT, else
        // from the guard - stub family like Mafia and SWAT+. The base slot
        // is fetched via his own kCross row, i.e. from joby3. That matters:
        // the guard from joby2 cannot be copied ("frame pointer outside the
        // frame slice").
        //
        // Swap halves 405 -> 470: he was the last enemy still hanging raw
        // on MESHSWAP1 (setup.cpp, meshes 10 and 13 - the
        // fire hands of the soldier family). The case was dormant and
        // therefore never noticed: the halves show only at OCB 3 (fall
        // through the window), OCB 4 (rappel) and SasControl state 37 (the
        // keyhole), never on a hit or a death - and the pool always places
        // him with OCB 0. Where the target lacks a 405 entirely (Andrea1,
        // Andy2, joby2, joby4, rich1) he would have drawn slot 0, i.e.
        // Lara's meshes.
        //
        // swapFrom is joby3, NOT his model donor joby2: joby2 has no
        // MESHSWAP1 at all. joby3's 405 is the family's fire-hand set, the
        // same source the guard (37 -> 465) and the joby guard (460 -> 467)
        // already draw from - each into his own target slot, so nothing is
        // shared. If the target level IS joby3, the copy still runs
        // properly: ApplyPatch loads every donor separately from the backup
        // (the "source" cache in ApplyPatch.cpp), so the level's native 405
        // is read out of the untouched file and written to the free slot
        // 470 - the native one stays where it is.
        //
        // One gap stays open, deliberately: in joby2 his model is native,
        // so the emission takes the modelAlreadyHere branch and copies
        // nothing - not even the halves. There he keeps the old picture.
        // It costs nothing as long as the pool sets OCB 0, and the guard
        // (465 in rich1) stands the same way.
        { 55, "joby2",   {},                                    {-1,-1},  "joby3",   470, 0, 460 }, // Crane Guy
        { 57, "Andrea3", {},                                    {-1,-1},  nullptr,   -1 },  // Lion
        { 59, "Andrea3", {},                                    {-1,-1},  "Andrea3", 401 }, // Gladiator
        // The Roman god, two versions, two slots.
        //
        // In vanilla they share slot 61 and thereby exclude each other:
        // Andrea2 carries the sword there (item 108, OCB 1 - ranged, green
        // lightning), Andrea3 the hammer (item 79, OCB 0 - melee, blue).
        // The item's OCB picks the weapon, the slot the look - if they do
        // not match, he wields the blade like a hammer.
        //
        // With the own slot 461 for the hammer, BOTH now fit into the same
        // level. The spawn OCB for it stands in kSpawnOcb.
        //
        // Donors: 61 comes from Andrea2 (sword), 461 from Andrea3 (hammer,
        // there under slot 61 - hence sourceSlot). Both levels carry 61 in
        // their curated list and therefore never import it themselves;
        // Andrea3 additionally skips 461 as its own donor.
        //
        // Companion BUBBLES (367): the SWORD version's projectile is, as
        // with the Hydra, no sprite but a mesh from this slot
        // (TriggerRomanGodMissile in romangod.cpp). Without it something
        // wrong flies in foreign levels.
        { 61,  "Andrea2", {},                                   {367,-1}, "Andrea2", 403 }, // Roman god (sword)
        // Death sound 204 (SGOD_SWD_DIE1_Q): the hammer version's death
        // anim +16 calls it, but only Andrea2 fills the number - at home in
        // Andrea3 the statue died mutely (a Core hole). With the entry it
        // squeals everywhere like its sword brother.
        { 461, "Andrea3", { {204,"Andrea2",204} },              {-1,-1},  "Andrea3", 404, 0, 0, nullptr, 61 }, // Roman god (hammer)
        // Hydra: long deliberately out ("three items on one position, the
        // OCB picks the chain link"). Taken in once checked - the OCB is
        // harmless, everything else holds:
        //  - InitialiseHydra uses trigger_flags ONLY for the start frame
        //    (30 * OCB, so the three heads do not twitch in sync) and an
        //    offset of +-384 in z. With OCB 0 both drop away, and the pool
        //    only ever sets OCB 0.
        //  - Killable despite objects[].undead: HydraControl (hydra.cpp)
        //    computes its damage itself - in flame state 2 at range, in
        //    the attack states up close, threefold with the shotgun. 30
        //    hit points.
        //  - It cleans up after itself: death anim +15 carries cmd 4
        //    (KillItem), before that it blasts itself apart node by node.
        //  - Its control only reaches anim_index+15, so no access beyond
        //    the object boundary (the Imp problem vanishes), and it needs
        //    neither MESHSWAP nor companion slots.
        //  - Its four anim sounds (406-409) travel automatically, phase D
        //    of the transfer fetches them from the anim commands. The only
        //    hard-wired sound is SFX_SMASH_ROCK on shattering - it stands
        //    in no anim command and therefore needs this entry. Its number
        //    is 167, not 168, after a count in sound.h's enum was off by
        //    one and the log showed "Non present sample:167". Andrea2 has
        //    it itself, so the donor is the same as for the model.
        // The three vanilla specimens in Andrea2 (items 91/92/93) are
        // protected via fixedItems and stay untouched.
        //  - animCount 17: the object table grants it less than its control
        //    needs (anim_index+15 is the death anim) - the same pattern as
        //    the Imp. Without the explicit length the transfer aborts with
        //    "frame pointer outside the frame slice".
        //  - Companion slot BUBBLES (367): its fire projectile is no
        //    sprite but the 17th mesh of this slot (hydra.cpp,
        //    objects[BUBBLES].mesh_index + 16). If the target carries other
        //    meshes there, something wrong flies through the air, as seen
        //    in Andrea1. Andrea2 carries in 367 exactly the projectile core
        //    with flame wings. Replacing is safe: the BUBBLES in water have
        //    nothing to do with this slot, they run via their own particle
        //    system (Bubbles[40], CreateBubble in tomb4fx.cpp). Andrea2
        //    itself has water too and still the projectile core in 367 -
        //    the best proof of that.
        { 63, "Andrea2", { {167,"Andrea2",167} },               {367,-1}, nullptr,   -1, 17 },  // Hydra
        // The VCI cyborg - Core's HITMAN, the armoured android from rich3.
        // The nimblest human in the game: the only enemy with can_monkey
        // (lot.cpp), vault and drop of four clicks, long jump, five-shot
        // bursts, sparks instead of blood on a hit.
        //
        // Why his own engine slot 469 and not the vanilla 67:
        // objects[HITMAN].undead is set, so no weapon of Lara's ever
        // reaches his hit points - in vanilla he dies only through rich3's
        // level geometry (the gas rooms with room flag 0x80, which exist in
        // no other level, and a short circuit in water). One-to-one he
        // would therefore be immortal in twelve of thirteen levels. Slot
        // 469 gives him a mortal control of his own; the two vanilla
        // specimens in rich3 (items 183 and 200, each carrying a puzzle
        // half) keep slot 67, keep their immortality and stay exactly the
        // puzzle Core built. They stand in fixedItems anyway.
        //
        // Donor rich3, sourceSlot 67: only rich3 carries the model at all
        // (20 meshes, 72 anims in one piece from 577). Through the
        // sourceSlot the "nobody donates to himself" rule keeps him out of
        // rich3 - which is exactly right here, since his slot brother
        // already stands there.
        //
        // Swap halves 405 -> 468: setup.cpp copies TWENTY meshes raw out of
        // MESHSWAP1 for him, the largest such case in the project (SWAT 2,
        // Imp 1, gladiator 16, Roman god 17) - his damage skin. In foreign
        // levels he would pull their 405 (the soldier family's fire hands)
        // or slot 0 (Lara). Hence his own fallback slot, like the ten
        // enemies already converted. rich3's 405 has 20 meshes and no anims
        // of its own, so onlyMeshes is the right mode.
        //
        // No CrossSound entries: all six of his samples (95/99/119 feet,
        // 206/207 fire, 247 short circuit) hang on anim commands and travel
        // by themselves with phase D of the transfer - measured on the test
        // import. No companions either: his MIP 68 is never drawn for
        // enemies. No anim base type and no animCount - he brings his own
        // complete set.
        //
        // Price: he is the most expensive guest in the table. His textures
        // add SIX object texture pages and 211 texinfos, about 1.08 MB per
        // target level (measured on the test import into Andrea1 and
        // joby5).
        { 469, "rich3",  {},                                    {-1,-1},  "rich3",   468, 0, 0, nullptr, 67 }, // Cyborg
        { 77, "Andy2",   {},                                    {-1,-1},  nullptr,   -1 },  // Maze Monster
        // Huskie (85): the dog from joby2 - an enemy of its OWN, not the
        // dog (41). Own control (HuskieControl), own Initialise, 24 instead
        // of 18 hit points, 26 instead of 21 meshes. The engine knows him
        // completely, he just was never in the pool.
        //
        // Measured with SlotAnims across all 13 levels: slot 85 carries a
        // model ONLY in joby2 (anim_index 579, slice of 25 anims). His
        // control reaches to anim_index+22 (death anims 20/21/22) - the
        // slice suffices, so no animCount needed (unlike Imp/Hydra). No
        // MESHSWAP, no companion, no base type; the sounds all hang on anim
        // commands and travel by themselves (huskie.cpp calls SoundEffect
        // nowhere hard-wired).
        //
        // OCB: InitialiseHuskie starts at trigger_flags != 0 on anim+1
        // instead of +8 and removes ITEM_INVISIBLE. The pool always sets
        // OCB 0, and items with OCB != 0 are never swapped anyway.
        //
        // joby2's own three huskies (items 4, 86, 153) do not stand in its
        // curated list - they stay vanilla, joby2 only gives away.
        { 85, "joby2",   {},                                    {-1,-1},  nullptr,   -1 },  // Huskie
        // The chef: a pure melee fighter (knife), model only in joby3. His
        // set carries only +0..+11 real anims (the rest are dead heirs his
        // states 1-6 never touch); his death is a 1-frame stub, so in the
        // engine he borrows the soldier death +16 - skeleton bone-identical
        // with the family (BoneCheck: 0 of 14 deviating). baseSlot 460: the
        // guard always travels along as the death donor (chef.cpp). No swap
        // halves, no meshswap; his anim sounds (89/187/190) travel via the
        // commands. His spawn OCB is 1 (hunt instead of kitchen work, see
        // kSpawnOcb). The native item 186 in joby3 stays vanilla with OCB 0
        // per curation - stealth cutscene fallback AND kitchen work both
        // hang on it.
        //
        // animCount 12: the slice is limited to the 12 REAL anims - the
        // first shrinking use (the Imp stretches it). His dead heirs
        // +12..+55 all point into the NEXT slot's frame region, on which
        // the copier's frame pointer checker fails ("outside the frame
        // slice" - measured on the CLI run). AnimMap confirms: all chains
        // of +0..+11 stay within the 12-slice (0 wrong ones), more his
        // control never plays.
        { 87, "joby3",   {},                                    {-1,-1},  nullptr,   -1, 12, 460 }, // Chef
        // Imp: donor andy3, not andy1. andy1's Imp anim set is gutted: the
        // death animation +45 (ImpControl sets it fixed at hit_points<=0)
        // is a one-frame dummy there without a KillItem command - the Imp
        // can never die (state flicker 9<->0, no fade, garbage pose). Core
        // never noticed, because young Lara in andy1 has no weapons. andy3
        // carries the full set (+45 = frames 45..66 with KillItem ->
        // after_death -> fade); anims 0-12, geometry and the MESHSWAP1 swap
        // set are identical in both (measured with the anim and model
        // tools). animCount 46: the object table grants
        // the Imp only 18 anims, +19..+45 lie physically in the ranges of
        // scenery slots - without the explicit length the death anim was
        // missing from the import and the engine read past the table end
        // (the observed death flight loop).
        { 89, "andy3",   {},                                    {-1,-1},  "andy3",   402, 46 }, // Imp

        // --- Guests from Tomb Raider 3 -----------------------------------
        //
        // No donor level: "from" stays empty, the model comes at runtime
        // from the installed Tomb Raider III files. All details - file,
        // object, sounds, volumes - stand in Tr3Source::dinos().
        //
        // The four target slots carry a model in NONE of the 16 TR5 levels
        // (measured with ModelOffset against all of them), so they take nothing
        // from any native enemy. Neither base type nor companions nor swap
        // halves - they bring everything themselves.
        { 31, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3"     }, // T-Rex
        { 43, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3gold" }, // Vulture
        { 53, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3"     }, // Compy
        { 75, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3"     }, // Raptor
    };

    // Forbidden types (see RandoInput::forbiddenTypes) fly out of the pool
    // BEFORE anything chooses - neither swap nor extra enemies nor cross
    // may touch them.
    std::vector<int> enemyPool;
    for (int t : in.enemyTypes)
        if (!in.forbiddenTypes.count(t) && !opt.disabledEnemies.count(t))
            enemyPool.push_back(t);

    // The emergency brake for "everything deselected" deliberately stands
    // behind the cross pool: in front of it, it would refill an
    // intentionally narrow selection before the cross pool gets a chance to
    // satisfy it.
    std::map<int, const CrossEnemy*> crossChosen;
    std::set<int> crossUsed;

    // Does this row's model already lie natively in the level? Exactly when
    // this level is the donor and the model lives there under the same
    // number (no sourceSlot). Then there is nothing to copy - the type is
    // only placed. Guests from TR3 have no donor level.
    auto modelAlreadyHere = [&lowerName](const CrossEnemy& c) -> bool {
        if (!c.from || c.sourceSlot > 0) return false;
        std::string k;
        for (const char* p = c.from; *p; ++p)
            k.push_back((*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p);
        return k == lowerName;
    };
    if ((opt.crossEnemies || opt.allsanity) && (opt.swapEnemies || opt.addEnemies) && lowerName != "joby4")
    {
        std::set<int> own(in.enemyTypes.begin(), in.enemyTypes.end());
        std::vector<const CrossEnemy*> candidates;
        for (const CrossEnemy& c : kCross)
        {
            if (own.count(c.typeId)) continue;            // already in the level pool
            if (in.forbiddenTypes.count(c.typeId)) continue;   // the level cannot take him
            if (opt.disabledEnemies.count(c.typeId)) continue; // deselected in options

            // Guests from Tomb Raider 3: they need the installed game
            // files. If those are missing, they drop out silently - a run
            // must never fail because someone does not own the games.
            if (c.tr3)
            {
                const bool gold = (std::strcmp(c.tr3, "tr3gold") == 0);
                // The vulture needs BOTH: his model sits in Gold, his
                // sounds in TR3's MAIN.SFX (see Tr3Source::sfxPath).
                if (opt.tr3Dir.empty()) continue;
                if (gold && opt.tr3GoldDir.empty()) continue;
                candidates.push_back(&c);
                continue;
            }

            // "Nobody donates to himself" - that is a reason to skip the
            // import, but no reason to exclude the type. Whoever's model
            // already lies in the level can be placed without any copy -
            // the brake belongs at the emission, not at the selection.
            //
            // The occasion is the chef in joby3: the slot is there, Core's
            // curation only uses it for a single item. The same silently
            // held for the crane guy in joby2, Pierre in Andrea3 and the
            // Hydra in Andrea2 - all types whose model lies in the donor
            // level but who do not stand in its curated list there.
            //
            // One exception remains: if the row carries a sourceSlot, the
            // model lives at the donor under a DIFFERENT number (the guard
            // 460 lies there on 37). In the donor level itself that would
            // be a clone of himself - two equal models on two slots. That
            // stays out.
            std::string fromLower;
            for (const char* p = c.from; *p; ++p)
                fromLower.push_back((*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p);
            if (fromLower == lowerName && c.sourceSlot > 0) continue;   // would clone himself
            candidates.push_back(&c);
        }

        // Allsanity takes the whole pool, cross mix at most three.
        const int maxForeign = opt.allsanity ? static_cast<int>(candidates.size()) : 3;
        for (int n = 0; n < maxForeign && !candidates.empty(); ++n)
        {
            const size_t pick = static_cast<size_t>(rng.below(candidates.size()));
            const CrossEnemy* c = candidates[pick];
            candidates.erase(candidates.begin() + static_cast<ptrdiff_t>(pick));
            enemyPool.push_back(c->typeId);
            crossChosen[c->typeId] = c;
        }
        std::sort(enemyPool.begin(), enemyPool.end());
        if (opt.allsanity)
            report.notes.push_back(fmt("allsanity: enemy pool extended to %d types.",
                                       static_cast<int>(enemyPool.size())));
    }

    // Emergency brake for "everything deselected".
    //
    // It stands down here and not before the cross pool: whoever ticks
    // only the dinos in the "Enemy types" window thereby deselects all
    // native types. Placed earlier, the pool would already be empty at
    // that point, the brake would refill it with the complete vanilla list
    // (thus ignoring the deselection), and the dinos would only come on
    // top - half the level staying vanilla although "dinos only" was
    // chosen.
    //
    // Now what really lies in the pot at the end counts. A selection
    // satisfiable through cross types passes unchanged. The brake only
    // engages in a true emergency - say, someone picks exclusively dinos
    // but the TR3 paths are missing and they silently drop out. Then a
    // level with vanilla enemies is still better than one in which nothing
    // happens at all.
    if (enemyPool.empty())
    {
        for (int t : in.enemyTypes)
            if (!in.forbiddenTypes.count(t)) enemyPool.push_back(t);
        if (!enemyPool.empty())
            report.notes.push_back("enemy types: the selection left not a single type for "
                                   "this level (not even a foreign one) - ignored for this "
                                   "level, or it would have no enemies at all.");
    }

    // --- Swap existing enemies ---
    //
    // Only the KIND changes, the item index stays. That is the whole trick:
    // triggers address their enemies via "Object N", i.e. via the index. If
    // it stays, every enemy still wakes up exactly where and when Core
    // intended - the level's progress cannot shift. A removal or addition
    // at this point would be something entirely different.
    //
    // Swapping happens only among the level's curated types. Scientist,
    // Guardian and the scripted cases are thereby out by themselves - they
    // do not stand in there. The chef (87) is a normal cross type; his one
    // native item (joby3, 186) still stays vanilla because the curation
    // nails it down (stealth cutscene). With the cross enemy mix,
    // individual roles additionally go to the chosen foreign types.
    std::set<int> immuneSet;         // items already made immune during the swap
    // Space needs: some enemies need an arena, not a closet.
    //
    // The Hydra is the occasion. She stands firmly rooted, turns a full 360
    // degrees and fights at range with her flame projectile - in a tight
    // chamber Lara can neither dodge nor use the range.
    //
    // What is measured is NOT the room but the free space AROUND THE SECTOR
    // - a window of 7x7 tiles with the enemy standing in its middle. Two
    // wrong paths lie behind that, both discarded:
    //
    //  - The single room is the wrong measure. A TR5 level's room layout
    //    follows the editor's construction kit, not what Lara perceives as
    //    a hall: her own arena in Andrea2 consists of SEVEN rooms (71, 72,
    //    116, 117, 121, 140, 194), room 116 alone would be only 4x8
    //    walkable. Any room-level threshold would have excluded her own
    //    battleground.
    //  - The contiguous area is too generous. It merges everything
    //    connected at the same height - in Andrea1 that was 619 of 961
    //    sectors, corridors included. The Hydra would have been allowed to
    //    stand in the hallway just because the hallway docks onto a hall.
    //
    // The window measures exactly what counts: how open it is where she
    // stands. The yardstick behind the threshold is vanilla - measured at
    // all three battlegrounds in Andrea2 (items 91/92/93): in the 3x3
    // window 9 of 9 tiles free, in the 7x7 exactly 40 of 49.
    //
    // The threshold rules out two simpler alternatives, both measured and
    // discarded:
    //  - 7x7 with 40 of 49 (matching the hall) leaves only 255 sectors
    //    game-wide, and five levels would never see the Hydra.
    //  - 3x3 with all nine tiles free (no neighbouring tile built up) still
    //    throws away 93 percent of the placement spots, measured over
    //    twenty seeds - 0.65 Hydras per full run, none of them through the
    //    swap, and in the Andy levels practically none at all.
    //  - 3x3 with 5 of 9 is what stands here. At that threshold almost
    //    every level lands between 30 and 70 percent usable sectors -
    //    Andrea1 60.5, rich1 40.4, joby3 32.4, Andy2 30.0 - and a full run
    //    is expected to carry about 4.7 Hydras. The nine-tile arena idea is
    //    given up as only a rough guideline; what the measure still keeps
    //    out is the closet - a place whose neighbourhood is more wall than
    //    floor.
    //
    // The zone export is a PLACEMENT pool in this, not a walkability
    // raster: it only carries sectors on which something may really stand.
    // Gapless blocks are rare in it - all the more important that yardstick
    // and measure come from the same source.
    //
    // The table applies to EVERY path by which a type lands somewhere, i.e.
    // to swap and extra enemies alike - including the home pool: in Andrea2
    // the Hydra stands in the level's own pool and could otherwise end up
    // in a closet via the swap. The three vanilla Hydras stand in
    // fixedItems and stay untouched anyway.
    struct SpaceNeed { int typeId; int window; int free; };
    static const SpaceNeed kSpaceNeeds[] = {
        { 63, 3, 5 },    // Hydra: 3x3 window, at least 5 of its 9 tiles free
    };

    // Draw weight: how many tickets a kind holds in the lottery.
    //
    // Every roll below picks uniformly among the kinds that fit at that
    // spot. That is fair per spot but not per run: a kind barred almost
    // everywhere by the space rule above only ever enters the few lists it
    // qualifies for, while a kind with no rule at all is in every single
    // one. Measured over ten seeds and the twelve campaign levels, the
    // Hydra came out at 4.2 placements per run (spread 1 to 9, Andrea2's
    // three vanilla heads not counted) against 32 to 64 Compsognathi -
    // close to what the 5-of-9 threshold predicted (about 4.7), so the
    // space rule works as designed; she is simply rare because she is only
    // ever a candidate on open ground.
    //
    // Doubling her ticket count: measured over the same ten seeds
    // afterwards, 9.1 per run (spread 6 to 14), and no other kind moved by
    // more than about 8 percent - the tickets come out of the same pot, so
    // the loss spreads over everyone. This changes no place: a kind only
    // wins more often where it already qualified, and where it is not in
    // the list nothing about the list, the roll or the seed changes -
    // proved by a run with the Hydra deselected coming out byte for byte
    // as before, over ten seeds and twelve levels.
    struct DrawWeight { int typeId; int weight; };
    static const DrawWeight kDrawWeights[] = {
        { 63, 2 },       // Hydra: two tickets instead of one
    };
    auto drawWeightOf = [](int typeId) -> int
    {
        for (const DrawWeight& w : kDrawWeights)
            if (w.typeId == typeId) return w.weight;
        return 1;
    };

    // Pack enemies: types that do not work alone.
    //
    // The Compsognathus is the occasion. His control is built entirely on
    // the group: `bits = 512 * (item_number & 7) - 1792` (compy.cpp)
    // distributes the animals by their item number onto eight angles of a
    // circle around the target, and the decision to attack hangs on two
    // global counters (compy_scared_timer, compys_attack_lara) all compys
    // share. A single compy therefore only circles Lara and almost never
    // dares approach.
    //
    // Four consecutive item numbers yield four different angles - exactly
    // what our appending delivers by itself.
    //
    // A pack is placed as ONE extra-enemy slot: all animals on the same
    // sector, woken by ONE trigger with several Object commands. Several
    // items on one position are common in TR5 - Andrea2 carries the three
    // Hydra links the same way.
    struct Herd { int typeId; int count; };
    static const Herd kHerds[] = {
        { 53, 4 },      // Compsognathus
    };

    // Spawn OCB: some enemies read their OCB as BEHAVIOR, not as a place
    // statement - for them "always 0" is wrong.
    //
    // The Roman god is the occasion. His control, RomangodControl in
    // romangod.cpp, reads trigger_flags as the weapon kind:
    //   trigger_flags == 1 -> ranged, throws a projectile
    //   trigger_flags == 0 -> melee with the hammer
    // even the color of his lightning hangs on it.
    //
    // And the versions differ per level: Andrea2 carries him with the sword
    // (item 108, OCB 1 - green, shoots), Andrea3 with the hammer (item 79,
    // OCB 0 - blue, strikes). With the default OCB 0, a Roman god placed in
    // Andrea2 would get the hammer behavior on the sword model: holding the
    // blade like a hammer, with blue lightning instead of green - only the
    // vanilla item 108 was right.
    //
    // The rule follows the model, not the level as such: where the own
    // sword model stands, OCB 1 must go. All other levels get the hammer
    // model imported from Andrea3 and stay at 0.
    // Since both versions have their own slot, the OCB follows the slot
    // instead of the level - with one exception: Andrea3 carries the
    // hammer in its own slot 61, not the sword. The level-specific row
    // therefore stands before the general one, first hit wins.
    struct SpawnOcb { int typeId; const char* level; int ocb; };  // level 0 = everywhere
    static const SpawnOcb kSpawnOcb[] = {
        {  61, "andrea3", 0 },  // Andrea3's own slot 61 IS the hammer
        {  61, nullptr,   1 },  // otherwise sword version: ranged
        { 461, nullptr,   0 },  // hammer version: melee
        // The chef: state 1 is his vegetable chopping, and in vanilla he
        // only leaves it when Lara comes within one and a half sectors
        // (chef.cpp). For an enemy placed anywhere that is wrong - he
        // should seek like anyone else. OCB 1 switches chef.cpp to the
        // hunt; the vanilla chef in joby3 (item 186, OCB 0) stays untouched
        // at his work.
        {  87, nullptr,   1 },  // chef: hunt instead of kitchen work
    };
    auto spawnOcbFor = [&lowerName](int typeId) -> int {
        for (const SpawnOcb& s : kSpawnOcb)
            if (s.typeId == typeId && (!s.level || lowerName == s.level)) return s.ocb;
        return 0;
    };
    auto herdSize = [](int typeId) -> int {
        for (const Herd& h : kHerds)
            if (h.typeId == typeId) return h.count;
        return 1;
    };

    // World raster of all walkable sectors, keyed by the tile in sector
    // steps. Deliberately in.sectors and not the continuously filtered
    // pool: what is measured is how open the surroundings ARE, not where
    // something may still go right now. Exactly this source also underlay
    // the vanilla measurement the 40 came from - measure and yardstick must
    // come from the same source, or one compares apples with oranges.
    //
    // Per tile the floor heights are collected, because rooms stacked above
    // each other share the same x/z. Without the height comparison, the
    // balcony above her counted as free space next to her.
    std::map<long long, std::vector<int>> freeGrid;
    for (const ZoneSector& s : in.sectors)
    {
        std::map<int, RoomOrigin>::const_iterator o = origin.find(s.room);
        if (o == origin.end()) continue;
        const long long gk =
            (static_cast<long long>((o->second.originX + s.sectorX * 1024) / 1024) << 24)
          |  static_cast<long long>((o->second.originZ + s.sectorZ * 1024) / 1024);
        freeGrid[gk].push_back(s.floorY);
    }

    auto enoughSpace = [&origin, &freeGrid]
                      (int typeId, int room, int sectorX, int sectorZ, int y) -> bool
    {
        const SpaceNeed* b = 0;
        for (const SpaceNeed& r : kSpaceNeeds)
            if (r.typeId == typeId) { b = &r; break; }
        if (!b) return true;                        // no special measure, everything allowed

        std::map<int, RoomOrigin>::const_iterator o = origin.find(room);
        if (o == origin.end()) return false;        // unknown room: do not risk it
        const int gx = (o->second.originX + sectorX * 1024) / 1024;
        const int gz = (o->second.originZ + sectorZ * 1024) / 1024;
        const int r  = b->window / 2;

        int free = 0;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
            {
                const long long nk = (static_cast<long long>(gx + dx) << 24)
                                   |  static_cast<long long>(gz + dz);
                std::map<long long, std::vector<int>>::const_iterator f = freeGrid.find(nk);
                if (f == freeGrid.end()) continue;
                for (int h : f->second)
                {
                    int dy = h - y;
                    if (dy < 0) dy = -dy;
                    if (dy <= 512) { ++free; break; }   // one story counts once
                }
            }
        return free >= b->free;
    };

    // --- Headroom: does the kind fit under the ceiling here? -------------
    //
    // Without this rule a soldier statue, 1546 units tall, can end up in
    // rich1 room 12, where floor and ceiling are 1024 apart - the head
    // sticks through the ceiling. The rule is the plain comparison, without
    // a safety margin, because a 1024 room has to stay open to an 809 unit
    // human.
    //
    // Neither number is typed anywhere. The height is the model's rest pose
    // out of the file that carries it (Tr5Level::frameBounds), the clearance
    // comes from the floordata of the real level (Tr5Level::sectorCeilingLow)
    // - both filled by markTriggerSafeSectors, the cross enemies' models read
    // from their donor files through in.modelHeight. Whoever cannot be
    // measured PASSES: a rule without a number must not silently bar a kind.
    //
    // Calibration: over the 15 vanilla levels the same measure calls 0 of
    // Core's own 101 enemies too tall (HeightCheck) - the yardstick agrees
    // with the people who built the game.
    std::map<int, int> heightCache;
    auto heightOf = [&](int typeId) -> int
    {
        std::map<int, int>::const_iterator c = heightCache.find(typeId);
        if (c != heightCache.end()) return c->second;

        int h = 0;
        std::map<int, int>::const_iterator t = in.typeHeight.find(typeId);
        if (t != in.typeHeight.end()) h = t->second;
        else if (in.modelHeight)
        {
            // Not in the level yet - so it is a cross enemy, and its model
            // lies in the donor this very table names. sourceSlot, where it
            // is set, is the number the model has AT THE DONOR (the joby
            // guard lies there under 37 and lives here under 460).
            for (const CrossEnemy& k : kCross)
            {
                if (k.typeId != typeId) continue;
                int v = 0;
                if (in.modelHeight(k.from, k.tr3, k.sourceSlot > 0 ? k.sourceSlot : k.typeId, v))
                    h = v;
                break;
            }
        }
        heightCache[typeId] = h;
        return h;
    };

    auto fitsHeight = [&](int typeId, int room, int sectorX, int sectorZ, int y) -> bool
    {
        const int h = heightOf(typeId);
        if (h <= 0) return true;                     // no measure: no rule
        std::map<long long, long>::const_iterator c =
            in.sectorCeiling.find(secKey(room, sectorX, sectorZ));
        if (c == in.sectorCeiling.end()) return true;   // no ceiling known: likewise
        return (static_cast<long>(y) - c->second) >= h;
    };

    // Only for the report: -1 means the sector's ceiling is unknown.
    auto clearanceAt = [&](int room, int sectorX, int sectorZ, int y) -> long
    {
        std::map<long long, long>::const_iterator c =
            in.sectorCeiling.find(secKey(room, sectorX, sectorZ));
        if (c == in.sectorCeiling.end()) return -1;
        return static_cast<long>(y) - c->second;
    };

    // How often the height rule struck, so the spoiler can say it (see the
    // notes at the end of both enemy passes).
    int heightRejectedSwaps = 0;      // swap places with no fitting kind left
    int heightRejectedSpots = 0;      // extra-enemy spots the same happened to
    // The same two numbers for the space rule (kSpaceNeeds), which sifts
    // the swap candidates before the roll just as the height rule does.
    int spaceRejectedSwaps = 0;       // swap places no kind finds room on
    int spaceNetSwaps = 0;            // safety net AFTER the roll - must stay 0

    if (opt.swapEnemies && (in.enemyTypes.size() > 1 || !crossChosen.empty()))
    {
        std::set<int> allowed(in.enemyTypes.begin(), in.enemyTypes.end());
        for (int t : in.forbiddenTypes) allowed.erase(t);

        std::vector<int> indices;                 // item indices of the swappable
        std::vector<int> kinds;                   // their types, same order
        for (const LevelItem& it : in.items)
        {
            if (!allowed.count(it.typeId)) continue;
            // Hand curation beats everything: what stands there stays as it is.
            if (std::find(in.fixedItems.begin(), in.fixedItems.end(), it.index) != in.fixedItems.end())
                continue;
            // Antitrigger targets may join the swap: the swapped enemy gets
            // the antitrigger immunity below (see there).
            // OCB != 0 usually means: a type-specific special spawn. The
            // OCB drives Initialise/Control of the original type - a
            // foreign type does not understand it and may end up in the
            // geometry that special spawn was placed for.
            //
            // Larson and Pierre (45/47) are the reasoned exception: with
            // them the OCB describes no special environment, only three
            // harmless things (InitialisePierre and PierreControl in
            // pierre.cpp) - the tag of their flee marker (item_flags[3]),
            // an offset of 384 units within the same sector and the start
            // in guard mode. None of it sits in the geometry. A foreign
            // type may thus move in, as long as the OCB drops to 0 in the
            // process - otherwise he reads it as his own.
            //
            // Three items in the whole game are affected: Andrea1 34
            // (OCB 1) and 35 (OCB 2), both with their own AI_AMBUSH marker
            // in room 89 resp. 169, plus Andrea2 76 - the scripted
            // miniboss, protected via fixedItems and already sorted out
            // above.
            //
            // The soldier family is exempt in the same way: the OCB there
            // only picks the spawn pose (InitialiseSas: climb in through
            // the window, rappel from the ledge, get up from sitting) -
            // that does not sit in the geometry either, the new type simply
            // stands there normally afterwards.
            //
            // The Imp is exempt as well: its climb/throw OCBs (1/2/3/11/12,
            // InitialiseImp in imp.cpp) pick a spawn behaviour, and a
            // swapped-in type simply spawns normally. The soldiers once seen
            // in the wall at andy3's cliff imps (items 2/3) came from a
            // different bug.
            //
            // What does NOT run via this list but via fixedItems (a type
            // rule cannot protect a single item):
            //   rich3 53 and 54 - hang on the rope and stay for now
            //   joby2 69        - waits for its own slot
            const int ot = it.typeId;
            const bool ocbOnlyPose =
                   ot == 45 || ot == 47            // Larson/Pierre: marker tag
                || ot == 33 || ot == 35            // SWAT, SWAT+
                || ot == 37 || ot == 460           // guard, guard (Joby)
                || ot == 39                        // Two Gun
                || ot == 49 || ot == 51            // Mafia, Mafia2
                || ot == 55                        // Crane Guy
                || ot == 89;                       // Imp
            if (it.ocb != 0 && !ocbOnlyPose) continue;
            indices.push_back(it.index);
            kinds.push_back(it.typeId);
        }

        if (indices.size() > 1 || (!indices.empty() && !crossChosen.empty()))
        {
            // Fisher-Yates over the types. The RNG is the same as
            // everywhere, so the seed determines the result completely.
            for (size_t i = kinds.size() - 1; i > 0; --i)
            {
                const size_t j = static_cast<size_t>(rng.below(i + 1));
                std::swap(kinds[i], kinds[j]);
            }

            // Allsanity: every role rolls freely from the full pool -
            // maximum variety, the level may lose its face. Cross mix, by
            // contrast, stays restrained: about every fourth role goes to
            // one of the up to three chosen foreign types, the level feels
            // mixed through, not exchanged.
            // Pack enemies do not take part in the swap. A swap only
            // changes an existing item's kind - companions could only be
            // fitted by drilling open Core's existing trigger chain, and a
            // single compy does not work (see kHerds). They only enter the
            // level as extra enemies, where the trigger belongs to this
            // tool. Filtered already at the draw: otherwise rolls would
            // land on a type repeatedly that is rejected afterwards, and
            // the role would stay vanilla.
            std::vector<int> swapPool;
            for (int t : enemyPool)
                if (herdSize(t) == 1) swapPool.push_back(t);

            // Where role i stands, and which kinds fit under that ceiling
            // and find enough free space around it (kSpaceNeeds). Both
            // filters run before the roll, so a place gets a swap wherever
            // any kind at all fits, instead of rolling blindly and falling
            // back to vanilla on a miss - asking the space rule only after
            // the roll let a Hydra rolled onto a tight place cost the place
            // its swap altogether, 72 places in twenty seeds.
            auto placeOf = [&](size_t i) -> const LevelItem*
            {
                std::map<int, LevelItem>::const_iterator f = finalOf.find(indices[i]);
                return (f == finalOf.end()) ? nullptr : &f->second;
            };
            // A kind carrying a draw weight stands in the list as many times
            // as it has tickets (kDrawWeights); the roll below is a plain
            // rng.below(size) over it. Everything else keeps its single
            // entry and its order, so for a pool without a weighted kind the
            // list - and with it the whole run - is bit for bit as before.
            // The list is only ever drawn from, never counted: the type
            // counts in the report come from enemyPool, which stays
            // unweighted.
            auto fittingAt = [&](const std::vector<int>& pool, const LevelItem* p) -> std::vector<int>
            {
                std::vector<int> v;
                for (int t : pool)
                {
                    if (p && !(fitsHeight(t, p->room, p->sectorX, p->sectorZ, p->y)
                            && enoughSpace(t, p->room, p->sectorX, p->sectorZ, p->y))) continue;
                    for (int n = drawWeightOf(t); n > 0; --n) v.push_back(t);
                }
                return v;
            };

            if (opt.allsanity && !swapPool.empty())
            {
                for (size_t i = 0; i < kinds.size(); ++i)
                {
                    const std::vector<int> here = fittingAt(swapPool, placeOf(i));
                    if (here.empty()) continue;   // the pass below says so and counts it
                    kinds[i] = here[static_cast<size_t>(rng.below(here.size()))];
                }
            }
            else if (!crossChosen.empty())
            {
                std::vector<int> crossSwap;
                for (std::map<int, const CrossEnemy*>::const_iterator w = crossChosen.begin();
                     w != crossChosen.end(); ++w)
                    if (herdSize(w->first) == 1) crossSwap.push_back(w->first);

                for (size_t i = 0; i < kinds.size() && !crossSwap.empty(); ++i)
                {
                    if (rng.below(4) != 0) continue;
                    const std::vector<int> here = fittingAt(crossSwap, placeOf(i));
                    if (here.empty()) continue;   // no foreign kind fits: the own one stays
                    kinds[i] = here[static_cast<size_t>(rng.below(here.size()))];
                }
            }

            for (size_t i = 0; i < indices.size(); ++i)
            {
                std::map<int, LevelItem>::iterator f = finalOf.find(indices[i]);
                if (f == finalOf.end() || f->second.typeId == kinds[i]) continue;

                // Headroom and free space (kSpaceNeeds). Where the draw
                // above already came out of a filtered list this never fires;
                // it catches the plain permutation, which deals out the level's
                // own kinds without ever asking about a place. A kind that does
                // fit is then drawn instead of giving the place up entirely.
                // Only if not a single pool kind fits does the vanilla enemy
                // stay: he has stood there since 2000 and fits by measurement
                // (0 of Core's 101 enemies fail the height rule).
                if (!fitsHeight(kinds[i], f->second.room, f->second.sectorX,
                                f->second.sectorZ, f->second.y)
                 || !enoughSpace(kinds[i], f->second.room, f->second.sectorX,
                                 f->second.sectorZ, f->second.y))
                {
                    const std::vector<int> here = fittingAt(swapPool, &f->second);
                    if (here.empty())
                    {
                        // Which of the two rules emptied the list? The height
                        // rule alone answers that: if some kind would fit under
                        // this ceiling, only the space rule can have taken the
                        // rest away. Without the distinction the report cannot
                        // say what either rule actually costs.
                        bool anyFitsHeight = false;
                        for (int t : swapPool)
                            if (fitsHeight(t, f->second.room, f->second.sectorX,
                                           f->second.sectorZ, f->second.y))
                            { anyFitsHeight = true; break; }
                        if (anyFitsHeight)
                        {
                            ++spaceRejectedSwaps;
                            report.notes.push_back(fmt("enemy item %d: no kind of the pool finds enough free space in room %d - stays typeId %d.",
                                                       indices[i], f->second.room, f->second.typeId));
                        }
                        else
                        {
                            ++heightRejectedSwaps;
                            report.notes.push_back(fmt("enemy item %d: no kind of the pool fits under the ceiling in room %d (clearance %d) - stays typeId %d.",
                                                       indices[i], f->second.room,
                                                       static_cast<int>(clearanceAt(f->second.room, f->second.sectorX,
                                                                                    f->second.sectorZ, f->second.y)),
                                                       f->second.typeId));
                        }
                        continue;
                    }
                    kinds[i] = here[static_cast<size_t>(rng.below(here.size()))];
                    if (f->second.typeId == kinds[i]) continue;   // drew its own kind
                }

                // Safety net for the space rule (see kSpaceNeeds). Every kind
                // that reaches this line came either out of a list already
                // sifted by enoughSpace or through the repair above, so this
                // must never strike any more - the counter below is the proof.
                // If it ever did, the place would keep its old type: one enemy
                // fewer in the mix is better than a Hydra stuck in her chamber.
                if (!enoughSpace(kinds[i], f->second.room,
                                f->second.sectorX, f->second.sectorZ, f->second.y))
                {
                    ++spaceNetSwaps;
                    report.notes.push_back(fmt("safety net: enemy item %d: typeId %d needs more free space than it has in room %d - stays typeId %d.",
                                               indices[i], kinds[i], f->second.room, f->second.typeId));
                    continue;
                }

                // If this enemy stands at an operable fixture, he keeps his
                // vanilla type.
                //
                // The bar for EXTRA enemies alone does not suffice: here no
                // place changes, but a kind. Whoever stands next to a
                // keyhole and becomes a T-Rex blocks the operation just the
                // same - it just does not show while rolling, because the
                // place was occupied in vanilla.
                //
                // Core put the enemy there and evidently found it
                // tolerable; simply leaving him is therefore the safe
                // answer and needs no table of which type is "big".
                {
                    bool atOperable = false;
                    for (const auto& kv : finalOf)
                    {
                        if (!isOperableFixture(kv.second.typeId)) continue;
                        if (std::abs(kv.second.x - f->second.x) > 1536) continue;
                        if (std::abs(kv.second.z - f->second.z) > 1536) continue;
                        if (std::abs(kv.second.y - f->second.y) > 512) continue;
                        atOperable = true;
                        break;
                    }
                    if (atOperable)
                    {
                        report.notes.push_back(fmt("enemy item %d stands at an operable fixture - stays typeId %d instead of %d.",
                                                   indices[i], f->second.typeId, kinds[i]));
                        continue;
                    }
                }

                JsonValue e = JsonValue::makeObject();
                e.set("index", JsonValue::makeInt(indices[i]));
                e.set("typeId", JsonValue::makeInt(kinds[i]));

                // The OCB belonged to the old type (only Larson/Pierre even
                // arrive here with one, see above). The new type would read
                // it as his own - hence away with it. For types whose OCB
                // picks a BEHAVIOR, the right value stands in kSpawnOcb
                // (Roman god: weapon kind).
                const int wantedOcb = spawnOcbFor(kinds[i]);
                if (f->second.ocb != wantedOcb)
                {
                    e.set("ocb", JsonValue::makeInt(wantedOcb));
                    report.notes.push_back(fmt("item %d: OCB %d -> %d on the type change to typeId %d.",
                                               indices[i], f->second.ocb, wantedOcb, kinds[i]));
                    f->second.ocb = wantedOcb;
                }

                // Antitrigger immunity if needed - reasoning at the
                // straggler pass further down.
                if (in.antitriggered.count(indices[i]))
                {
                    e.set("antitriggerImmune", JsonValue::makeBool(true));
                    immuneSet.insert(indices[i]);
                }

                edits.push(e);

                report.notes.push_back(fmt("enemy item %d: typeId %d -> %d",
                                           indices[i], f->second.typeId, kinds[i]));
                f->second.typeId = kinds[i];
                if (crossChosen.count(kinds[i])) crossUsed.insert(kinds[i]);
                ++report.enemiesSwapped;
            }

            // How many of Core's enemy places the height rule left at
            // vanilla because nothing in the pool fits under their ceiling.
            report.notes.push_back(fmt("headroom: %d of %d swap places kept their vanilla kind - no pool kind fits under their ceiling.",
                                       heightRejectedSwaps, static_cast<int>(indices.size())));
            // The same for the space rule, plus the proof that its safety
            // net after the roll is dead wood: that number must be 0.
            report.notes.push_back(fmt("space: %d of %d swap places kept their vanilla kind - no pool kind finds enough free space there; safety net after the roll struck %d times.",
                                       spaceRejectedSwaps, static_cast<int>(indices.size()),
                                       spaceNetSwaps));
        }
        else
            report.notes.push_back(fmt("enemy swap: only %d swappable enemies in the level, nothing to mix.",
                                       static_cast<int>(indices.size())));
    }

    // --- Antitrigger immunity for EVERY affected enemy ---
    //
    // The item loss is the real reason, and it does not hang on the type
    // swap: GetCarriedItems (setup.cpp) attaches every pickup within
    // 512 units to the nearest enemy at level start - RemoveDrawnItem,
    // room_number = NO_ROOM, the item is then nothing but his cargo. If he
    // dies normally, CreatureDie releases it via DropBaddyPickups
    // (box.cpp). The antitrigger, by contrast, calls KillItem directly
    // (control.cpp) - enemy and cargo are gone.
    //
    // In vanilla that has no consequences, because Core lays nothing next
    // to such enemies. As soon as the items travel, a key item can lie
    // there, and then the level is unsolvable after one step onto the
    // antitrigger sector - without any message.
    //
    // Hence every affected enemy gets the immunity bit, the unswapped one
    // too. Effect like "taking the pointer out of the antitrigger", only at
    // the item instead of the floordata: the antitrigger still lists him
    // but skips him (_TestTriggers, control.cpp), and all other pointers of
    // the same trigger keep working unchanged.
    //
    // Only if items travel at all: with pure enemy randomization there is
    // nothing to lose, and a vanilla run should stay vanilla.
    if (opt.shuffleKeyItems || opt.shuffleOtherPickups)
    {
        std::set<int> enemyKindSet(in.enemyTypes.begin(), in.enemyTypes.end());
        for (std::map<int, const CrossEnemy*>::const_iterator c = crossChosen.begin();
             c != crossChosen.end(); ++c)
            enemyKindSet.insert(c->first);

        int backfilled = 0;
        for (int idx : in.antitriggered)
        {
            if (immuneSet.count(idx)) continue;
            std::map<int, LevelItem>::const_iterator f = finalOf.find(idx);
            // Enemies only: an antitrigger on a door or a flipmap must keep
            // firing, or the level stands still.
            if (f == finalOf.end() || !enemyKindSet.count(f->second.typeId)) continue;

            JsonValue e = JsonValue::makeObject();
            e.set("index", JsonValue::makeInt(idx));
            e.set("antitriggerImmune", JsonValue::makeBool(true));
            edits.push(e);
            immuneSet.insert(idx);
            ++backfilled;
        }
        if (!immuneSet.empty())
            report.notes.push_back(fmt("antitrigger: %d enemies made immune (%d of them unswapped) - "
                                       "protects carried items from vanishing.",
                                       static_cast<int>(immuneSet.size()), backfilled));
    }

    // Curated antitrigger immunities: they apply ALWAYS, regardless of
    // whether items travel - they describe level logic, not a side effect
    // of the randomization. rich2's door 171 is the case (see the curation
    // table).
    for (int idx : in.antitriggerImmuneItems)
    {
        if (immuneSet.count(idx)) continue;
        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(idx));
        e.set("antitriggerImmune", JsonValue::makeBool(true));
        edits.push(e);
        immuneSet.insert(idx);
        report.notes.push_back(fmt("item %d: curated antitrigger immunity set.", idx));
    }

    // --- Extra enemies ---
    //
    // Two things must fit together, and both are user requirements from
    // observations in game:
    //
    //  1. The trigger must NEVER lie on the enemy sector. Otherwise the
    //     enemy spawns into Lara.
    //  2. It must still be crossed reliably, or the enemy never wakes up.
    //
    // Both are satisfied by the approach sector: it lies about three steps
    // before the enemy on Lara's first-access path. It is computed in the
    // zone export where the first-access tree lives - only the finished
    // answer arrives here.
    //
    // The trigger gets OneShot. That is no formality: _TestTriggers
    // (control.cpp) passes the bit on to the item as IFL_INVISIBLE, and
    // Larson cannot be killed at all without it (PierreCanFlee, pierre.cpp).
    JsonValue trigAdds = JsonValue::makeArray();
    if (opt.addEnemies && opt.enemiesPerLevel > 0 && !enemyPool.empty())
    {
        // The engine holds items[] as a fixed field of 512 entries
        // (MAX_ITEMS, global/types.h; Tr5Level checks
        // it when appending). Capped instead of hoped - a rejected patch
        // mid-run would be worse than one enemy fewer.
        const int slotsFree = 512 - static_cast<int>(in.items.size()) - static_cast<int>(adds.size());

        // Where may an enemy go? Onto a free sector whose approach is also
        // free and tolerates a trigger.
        //
        // Carry ALL suitable approach sectors along, not only the best:
        // two different enemy tiles can have the same approach sector.
        // Whoever settles on one here no longer notices at the second
        // enemy that its trigger is already taken - and sets two triggers
        // on one tile. That then blows up while patching, and not as "one
        // enemy fewer" but as a lost level.
        struct Spot { const ZoneSector* s; std::vector<long long> trigger; };
        std::vector<Spot> spots;
        for (const ZoneSector& s : pool)
        {
            const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
            if (used.count(sk)) continue;
            if (!origin.count(s.room)) continue;
            // No enemy into a room with the water flag.
            if (in.waterRooms.count(s.room)) continue;

            // No new enemy where he would swallow a pickup.
            //
            // GetCarriedItems (setup.cpp) hands every pickup inside an
            // intelligent enemy's box to that enemy at level start:
            // RemoveDrawnItem, room_number = NO_ROOM, and it drops out
            // again only when he dies. Extra enemies stand behind a OneShot
            // trigger and are invisible until it is crossed, so such a
            // pickup is gone from the first frame - on a key item that is
            // the playthrough, and a fleeing Larson or Pierre carries it out
            // of the level for good (the same reason their own sectors are
            // barred for items at the top of this function).
            //
            // A rule narrowed to OCB 2 and 9 alone is too narrow: `used`
            // covers everything this run sets down, but not what stays. A
            // pinned or simply unmoved pickup never enters the set, and the
            // enemy can then land exactly on it - measured over 39 runs, 3
            // hits, every one at distance 0/0/0 and every one a
            // PICKUP_ITEM4 that had not moved.
            //
            // Distances as the engine computes them, 512/256, but WITHOUT
            // its room test: two rooms can occupy the same place (flip
            // twins), and one enemy standing elsewhere is cheaper than one
            // key item lost. Wall grabs keep the wider box, because they sit
            // at the wall edge up to 512 off the sector center while the
            // enemy stands on it. It bars the SPOT and not the enemy type:
            // which type goes here is decided further down, and a
            // non-carrier lying inside another model would be wrong anyway.
            {
                bool pickupNear = false;
                const RoomOrigin& roW = origin[s.room];
                const int cx = roW.originX + s.sectorX * 1024 + 512;
                const int cz = roW.originZ + s.sectorZ * 1024 + 512;
                for (const auto& kv : finalOf)
                {
                    const LevelItem& p2 = kv.second;
                    if (!isPickupType(p2.typeId)) continue;

                    const bool wallGrab = (p2.ocb & 0x3F) == 2 || (p2.ocb & 0x3F) == 9;
                    const int reachXZ = wallGrab ? 576 : 512;
                    const int reachY  = wallGrab ? 320 : 256;

                    if (std::abs(p2.x - cx) < reachXZ && std::abs(p2.z - cz) < reachXZ &&
                        std::abs(p2.y - s.floorY) < reachY) { pickupNear = true; break; }
                }
                if (pickupNear) continue;
            }

            // No new enemy at an operable fixture.
            //
            // The case in joby5: the lock for door 177 and the valve for
            // 127 sit directly at their door. If a big enemy stands there -
            // statue, T-Rex, TwoGun - his hitbox pushes Lara away before
            // the operating animation can start. The door can then never be
            // opened, and the level is lost.
            //
            // Barred for EVERY enemy type, not only big ones. Where exactly
            // the line between "pushes away" and "does not disturb" lies
            // hangs on radius, state and approach angle and would be a
            // table full of guesses - a softlock is more expensive than an
            // enemy standing elsewhere. Measured, the rule costs 0 to 18
            // percent of the enemy spots depending on the level (rich1 is
            // the outlier with 65 of 361); plenty remains everywhere for
            // placing twenty enemies.
            //
            // 1536 units is no round thumb value: operable fixtures stand
            // at the wall edge, up to 512 from the sector center. Only
            // 512 + 1024 safely covers the object's sector AND the
            // neighboring tiles Lara stands on to operate.
            {
                bool operableNear = false;
                const RoomOrigin& roB = origin[s.room];
                const int bx = roB.originX + s.sectorX * 1024 + 512;
                const int bz = roB.originZ + s.sectorZ * 1024 + 512;
                for (const auto& kv : finalOf)
                {
                    if (!isOperableFixture(kv.second.typeId)) continue;
                    if (std::abs(kv.second.x - bx) > 1536) continue;
                    if (std::abs(kv.second.z - bz) > 1536) continue;
                    if (std::abs(kv.second.y - s.floorY) > 512) continue;
                    operableNear = true;
                    break;
                }
                if (operableNear) continue;
            }

            Spot p;
            p.s = &s;
            for (long long a : s.approach)
            {
                if (a == sk || used.count(a)) continue;
                if (!in.triggerSafeSectors.count(a)) continue;
                p.trigger.push_back(a);
            }
            if (!p.trigger.empty()) spots.push_back(p);
        }

        int howMany = opt.enemiesPerLevel;
        if (howMany > slotsFree) howMany = slotsFree;
        if (howMany > static_cast<int>(spots.size())) howMany = static_cast<int>(spots.size());

        const int wanted = opt.enemiesPerLevel;
        const int spotsBefore = static_cast<int>(spots.size());

        int placed = 0;
        while (placed < howMany && !spots.empty())
        {
            const size_t pick = static_cast<size_t>(rng.below(spots.size()));
            const Spot p = spots[pick];
            spots.erase(spots.begin() + static_cast<ptrdiff_t>(pick));

            // Only here does the trigger get decided: up to the selection,
            // other enemies may have occupied sectors.
            long long trigger = -1;
            for (long long a : p.trigger)
                if (!used.count(a)) { trigger = a; break; }
            if (trigger < 0) continue;          // all approach tiles taken

            // The enemy gets the true floor height - the engine would put
            // him on the floor anyway, but he should not drop in half a
            // sector deep. Walkability is no condition for him, hence only
            // the height.
            //
            // It is computed before the choice, because the headroom rule
            // needs the y the enemy will really stand at.
            int gyFloor = 0;
            Rando::floorAt(in, *p.s, 512, 512, gyFloor);

            // First sift the types that find enough space here (see
            // kSpaceNeeds) and fit under the ceiling, then roll among them.
            // The other way round - roll and discard on mismatch - would
            // steal the Hydra a spot every time that another type could
            // have used.
            std::vector<int> fitting;
            int wouldFitButTooTall = 0;
            for (int t : enemyPool)
            {
                if (!enoughSpace(t, p.s->room, p.s->sectorX, p.s->sectorZ, p.s->floorY)) continue;
                // A pack must fit completely into the item budget - rather
                // no compys at all than three without the fourth.
                if (herdSize(t) > howMany - placed) continue;
                if (!fitsHeight(t, p.s->room, p.s->sectorX, p.s->sectorZ, gyFloor))
                { ++wouldFitButTooTall; continue; }
                // Weighted kinds get several tickets in the same list
                // (kDrawWeights); the counter above stays a count of KINDS,
                // because the report asks how many kinds a ceiling barred.
                for (int n = drawWeightOf(t); n > 0; --n) fitting.push_back(t);
            }
            if (fitting.empty())
            {
                // Distinguish the two reasons, or the report cannot say what
                // the new rule actually cost: only where a kind was barred
                // for its HEIGHT alone is this spot on the height rule's bill.
                if (wouldFitButTooTall > 0)
                {
                    ++heightRejectedSpots;
                    report.notes.push_back(fmt("enemy spot %d,%d,%d dropped: clearance %d is too low for every kind of the pool.",
                                               p.s->room, p.s->sectorX, p.s->sectorZ,
                                               static_cast<int>(clearanceAt(p.s->room, p.s->sectorX,
                                                                            p.s->sectorZ, gyFloor))));
                }
                continue;                       // too tight here for the whole pool
            }

            const int typeId = fitting[static_cast<size_t>(rng.below(fitting.size()))];
            const int herd = herdSize(typeId);
            placed += herd;
            if (crossChosen.count(typeId)) crossUsed.insert(typeId);
            const RoomOrigin& ro = origin[p.s->room];

            // The index the new item gets: itemAdds appends at the back, in
            // exactly this order.
            const int newIndex = static_cast<int>(in.items.size()) + static_cast<int>(adds.size());

            // A pack stands on ONE sector - like Andrea2 its three Hydra
            // links. The animals distribute themselves at runtime: their
            // formation hangs on the item number, and we assign those
            // consecutively here. One trigger wakes them all.
            JsonValue cmdList = JsonValue::makeArray();
            for (int k = 0; k < herd; ++k)
            {
                JsonValue a = JsonValue::makeObject();
                a.set("typeId", JsonValue::makeInt(typeId));
                a.set("room", JsonValue::makeInt(p.s->room));
                a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + p.s->sectorX * 1024 + 512));
                a.set("y", JsonValue::makeInt(gyFloor));
                a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + p.s->sectorZ * 1024 + 512));
                // OCB 0 as the default: with Larson an OCB != 0 sets
                // ai_bits = GUARD, and then he looks for AI objects that do
                // not stand at a new place (InitialisePierre, pierre.cpp).
                // Types whose OCB picks a behavior instead of an
                // environment are the exception - they stand with their
                // value in kSpawnOcb (Roman god: weapon kind, see there).
                a.set("ocb", JsonValue::makeInt(spawnOcbFor(typeId)));
                a.set("angle", JsonValue::makeInt(static_cast<int64_t>(rng.below(4)) * 16384 - 32768));
                adds.push(a);

                JsonValue cmd = JsonValue::makeObject();
                cmd.set("type", JsonValue::makeInt(0));          // TO_OBJECT
                cmd.set("param", JsonValue::makeInt(newIndex + k));
                cmdList.push(cmd);
            }

            const int tr = static_cast<int>(trigger >> 32);
            const int tx = static_cast<int>((trigger >> 16) & 0xFFFF);
            const int tz = static_cast<int>(trigger & 0xFFFF);

            JsonValue t = JsonValue::makeObject();
            t.set("room", JsonValue::makeInt(tr));
            t.set("sectorX", JsonValue::makeInt(tx));
            t.set("sectorZ", JsonValue::makeInt(tz));
            t.set("commands", cmdList);
            // The waker also belongs into the flipmap partner of this room.
            // The trigger sector always comes out of the zone export, and
            // that lists base rooms only - so as soon as the level flips
            // that group, the base room reads the alternate's floordata and
            // the waker is gone. In joby5 the intro cutscene switches group
            // 0 on before Lara can move (PLAYCUT 32 -> joby8, whose
            // joby8_init and joby8_control in deltapak.cpp fire the
            // FLIPMAP(0) heavy trigger in room 40), and nothing in the
            // level ever switches it back: without the copy
            // nine of twenty-two enemies never wake up. The mirror itself,
            // with its wall and height test, sits in ApplyPatch.
            t.set("mirrorFlip", JsonValue::makeInt(1));
            trigAdds.push(t);

            used.insert(secKey(p.s->room, p.s->sectorX, p.s->sectorZ));
            used.insert(trigger);
            report.enemiesAdded += herd;
            if (herd > 1)
                report.notes.push_back(fmt("pack: %d x typeId %d as items %d-%d on %d,%d,%d - one trigger on %d,%d,%d",
                                           herd, typeId, newIndex, newIndex + herd - 1,
                                           p.s->room, p.s->sectorX, p.s->sectorZ, tr, tx, tz));
            else
                report.notes.push_back(fmt("enemy typeId %d as item %d on %d,%d,%d - trigger on %d,%d,%d",
                                           typeId, newIndex, p.s->room, p.s->sectorX, p.s->sectorZ, tr, tx, tz));
        }

        // The run places as many as the level yields - missing spots are no
        // error but a property of the level. It is reported anyway, or
        // someone wonders about the number.
        if (report.enemiesAdded < wanted)
            report.notes.push_back(fmt("only %d of %d extra enemies placed "
                                       "(%d spots to choose from, %d item slots free).",
                                       report.enemiesAdded, wanted, spotsBefore, slotsFree));

        report.notes.push_back(fmt("headroom: %d extra-enemy spots dropped - too low for every kind of the pool.",
                                   heightRejectedSpots));
    }

    // --- Who carries what in the end? ---
    //
    // GetCarriedItems (game/setup.cpp) runs over all items at level
    // start and gives every carrier the pickups lying close enough to him.
    // That is intended: a key at an enemy is a good find.
    //
    // But there is one condition. DropBaddyPickups (game/box.cpp) puts
    // the item on the enemy's sector center at death, and PickUpCollision
    // (game/pickup.cpp) fans out via the OCB into special cases that
    // align Lara at a prop via TestLaraPosition - at a prop that does not
    // stand at the drop site. A carried pickup with OCB != 0 lies there
    // forever unpickable afterwards, without a crash and without a message.
    //
    // Everything this run moves gets OCB 0 anyway per rule 3, and in
    // vanilla all 56 carried items have OCB 0 as well. So the rule is
    // satisfied - it is recomputed anyway, because both are promises from
    // outside and no property of this code. As soon as enemies themselves
    // are moved, a carrier can land on a pinned pickup, and then it shows
    // up here instead of in game.
    for (const LevelItem& b : in.items)
    {
        if (!isCarrierType(b.typeId)) continue;
        for (const auto& kv : finalOf)
        {
            const LevelItem& p = kv.second;
            if (p.index == b.index || !isPickupType(p.typeId)) continue;
            if (p.room != b.room) continue;
            if (!isWithinCarryBox(b.x, b.y, b.z, p.x, p.y, p.z)) continue;

            if ((p.ocb & 0x3F) != 0)
            {
                // No abort. Moved items always get OCB 0, so a nonzero
                // value can only mean that item AND carrier both lie
                // unchanged where vanilla put them. This run is innocent of
                // that, and letting a run fail on a vanilla constellation
                // would fail seeds that are fine.
                //
                // As soon as enemies themselves are moved, that changes:
                // then a carrier can land on a pinned pickup, and that
                // would very much be our doing. Here is the place where it
                // must then become an error.
                report.notes.push_back(fmt("item %d (typeId %d) lies unchanged at carrier %d and has OCB %d - "
                                           "dropped it would be unpickable (vanilla state, not changed).",
                                           p.index, p.typeId, b.index, p.ocb & 0x3F));
                continue;
            }
            report.notes.push_back(fmt("item %d (typeId %d) lies at item %d (typeId %d) and is carried.",
                                       p.index, p.typeId, b.index, b.typeId));
        }
    }

    // --- Coupled triggers ---
    //
    // Only here, because only now is it fixed where the items landed. If a
    // coupled item stayed put, there is nothing to move either - the
    // trigger then still lies on its vanilla sector and is right.
    JsonValue trigMoves = JsonValue::makeArray();
    for (const auto& kv : coupled)
    {
        const CoupledTrigger& c = *kv.second;
        std::map<int, LevelItem>::const_iterator f = finalOf.find(c.itemIndex);
        if (f == finalOf.end()) continue;
        const LevelItem& fin = f->second;

        if (fin.room == c.room && fin.sectorX == c.sectorX && fin.sectorZ == c.sectorZ)
        {
            report.notes.push_back(fmt("item %d stayed on its sector - the trigger stays too.", c.itemIndex));
            continue;
        }

        JsonValue m = JsonValue::makeObject();
        m.set("fromRoom", JsonValue::makeInt(c.room));
        m.set("fromSectorX", JsonValue::makeInt(c.sectorX));
        m.set("fromSectorZ", JsonValue::makeInt(c.sectorZ));
        m.set("toRoom", JsonValue::makeInt(fin.room));
        m.set("toSectorX", JsonValue::makeInt(fin.sectorX));
        m.set("toSectorZ", JsonValue::makeInt(fin.sectorZ));
        trigMoves.push(m);
        report.notes.push_back(fmt("item %d travels to %d,%d,%d - its pickup trigger moves along from %d,%d,%d.",
                                   c.itemIndex, fin.room, fin.sectorX, fin.sectorZ,
                                   c.room, c.sectorX, c.sectorZ));
    }

    // --- Request missing sounds ---
    //
    // Only for types that actually stand in the level after this run.
    // Fetching a sample for an enemy that does not even exist here would be
    // visible ballast in the file.
    JsonValue soundAdds = JsonValue::makeArray();
    if (!in.enemySounds.empty())
    {
        std::set<int> present;
        for (const auto& kv : finalOf) present.insert(kv.second.typeId);
        for (size_t i = 0; i < adds.size(); ++i)
            present.insert(static_cast<int>(adds[i]["typeId"].asInt()));

        for (const EnemySound& s : in.enemySounds)
        {
            if (!present.count(s.typeId)) continue;
            JsonValue a = JsonValue::makeObject();
            a.set("map", JsonValue::makeInt(s.map));
            a.set("fromLevel", JsonValue::makeString(s.fromLevel));
            a.set("fromMap", JsonValue::makeInt(s.fromMap));
            soundAdds.push(a);
            report.notes.push_back(fmt("sound for typeId %d: map %d from %s map %d",
                                       s.typeId, s.map, s.fromLevel.c_str(), s.fromMap));
        }
    }

    // --- Cross enemies: request models and special sounds ---
    //
    // Only for the types the run really used. The patch path fetches the
    // model with the samples of its animation commands from the donor
    // (replace, see the pool comment above); the entries here additionally
    // cover the known holes even the donor has. Duplicate sound requests
    // are harmless - the patch path skips existing ones.
    JsonValue moveables = JsonValue::makeArray();
    JsonValue laraAnims = JsonValue::makeArray();
    std::set<int> swapEmitted;

    // The soldier family's anim base types are emitted first, before the
    // types that need them. Standing at the back, the import aborts as
    // soon as the base type really has to be loaded instead of already
    // being there: "cannot walk the main block - mesh pointers". This
    // matters because the "Enemy types" window can deselect the base type
    // specifically while a dependent type is still chosen.
    //
    // Whoever is himself in the run is skipped here: he comes below with
    // replace=true and thereby more completely from the donor.
    std::set<int> baseEmitted;
    for (int t : crossUsed)
    {
        const CrossEnemy* c = crossChosen[t];
        if (c->baseSlot <= 0) continue;
        if (crossUsed.count(c->baseSlot)) continue;
        if (!baseEmitted.insert(c->baseSlot).second) continue;

        // The base type comes from his own row, not from the donor level of
        // the one requesting him: with the Crane Guy, fetching the guard
        // from joby2 fails ("frame pointer outside the frame slice"), while
        // from rich1 it works. With Mafia and SWAT+ this never matters,
        // because their donors happen to carry a usable base type.
        const char* baseFrom = c->from;
        int baseSource = c->baseSlot;
        for (const CrossEnemy& b : kCross)
            if (b.typeId == c->baseSlot)
            {
                baseFrom = b.from;
                // Engine slots beyond 460 lie at the donor under their
                // sourceSlot (the guard 460 under 37) - the same toSlot
                // mechanics as with the type's own row.
                if (b.sourceSlot > 0) baseSource = b.sourceSlot;
                break;
            }

        JsonValue g = JsonValue::makeObject();
        g.set("slot", JsonValue::makeInt(baseSource));
        if (baseSource != c->baseSlot)
            g.set("toSlot", JsonValue::makeInt(c->baseSlot));
        g.set("fromLevel", JsonValue::makeString(baseFrom));
        // replace=false leaves a native base type standing - the level
        // should keep its own model if it has one.
        g.set("replace", JsonValue::makeBool(false));
        moveables.push(g);
        report.notes.push_back(fmt("anim base type %d loaded from %s for typeId %d.",
                                   c->baseSlot, baseFrom, t));
    }

    for (int t : crossUsed)
    {
        const CrossEnemy* c = crossChosen[t];

        // Guest from Tomb Raider 3: no donor level, but an instruction to
        // the patch path to fetch him at runtime from the installed game
        // files. All details stand in Tr3Source::dinos() - the slot
        // suffices here, since the table is findable via it.
        if (c->tr3)
        {
            JsonValue e = JsonValue::makeObject();
            e.set("slot", JsonValue::makeInt(t));
            e.set("fromTr3", JsonValue::makeString(c->tr3));
            moveables.push(e);
            report.notes.push_back(fmt("guest from Tomb Raider 3: slot %d is fetched while patching.", t));
            continue;
        }

        // Native type: this level is the donor, so the model already
        // stands under the right number - with meshes, swap halves and
        // samples, because this is exactly where the other levels fetch
        // them from. Nothing is copied, placing happens all the same: the
        // chef in joby3 is the occasion, he always had his slot and still
        // stood in the level only a single time.
        //
        // The anim base type above stays deliberately untouched: he comes
        // with replace=false and drops out by himself if the level already
        // has him - and if not, he is genuinely needed (the chef borrows
        // his death animation from him, chef.cpp).
        if (modelAlreadyHere(*c))
        {
            report.notes.push_back(fmt("enemy typeId %d is native here - model already stands, no import.", t));
            ++report.crossTypesUsed;
            continue;
        }

        JsonValue e = JsonValue::makeObject();
        // Engine slots beyond 460: the model lies at the donor under
        // sourceSlot and travels via toSlot into the new slot.
        e.set("slot", JsonValue::makeInt(c->sourceSlot > 0 ? c->sourceSlot : t));
        if (c->sourceSlot > 0)
            e.set("toSlot", JsonValue::makeInt(t));
        e.set("fromLevel", JsonValue::makeString(c->from));
        e.set("replace", JsonValue::makeBool(true));
        if (c->animCount > 0) e.set("animCount", JsonValue::makeInt(c->animCount));
        moveables.push(e);

        // The type's mandatory entourage (see the table comment).
        for (int b : c->companions)
        {
            if (b < 0) continue;
            JsonValue g = JsonValue::makeObject();
            g.set("slot", JsonValue::makeInt(b));
            g.set("fromLevel", JsonValue::makeString(c->from));
            g.set("replace", JsonValue::makeBool(true));
            moveables.push(g);
        }

        // The family's anim base type already stands above in the list - he
        // must come before the type that needs him.

        // Swap halves: the swap donor's MESHSWAP1 (swapFrom, may differ
        // from the model donor) into the type's fallback slot. This is
        // unique per enemy; the set remains as protection against a
        // double emission, should a slot ever be shared again.
        if (c->swapDst >= 0 && c->swapFrom && !swapEmitted.count(c->swapDst))
        {
            swapEmitted.insert(c->swapDst);
            JsonValue g = JsonValue::makeObject();
            g.set("slot", JsonValue::makeInt(405));         // the swap donor's MESHSWAP1
            g.set("toSlot", JsonValue::makeInt(c->swapDst));
            g.set("fromLevel", JsonValue::makeString(c->swapFrom));
            g.set("replace", JsonValue::makeBool(true));
            // MESHSWAP1 carries an anim_index without animations of its own
            // (Core legacy) - only the meshes count.
            g.set("onlyMeshes", JsonValue::makeBool(true));
            moveables.push(g);
        }

        for (int k = 0; k < 2; ++k)
        {
            if (c->sounds[k].map == 0) continue;
            JsonValue a = JsonValue::makeObject();
            a.set("map", JsonValue::makeInt(c->sounds[k].map));
            a.set("fromLevel", JsonValue::makeString(c->sounds[k].fromLevel));
            a.set("fromMap", JsonValue::makeInt(c->sounds[k].fromMap));
            soundAdds.push(a);
        }

        ++report.crossTypesUsed;
        report.notes.push_back(fmt("cross enemy: typeId %d comes from %s.", t, c->from));
    }

    // --- andy1: heal its own Imp ---
    //
    // andy1's Imp slot carries the gutted anim set (death anim +45 is a
    // dummy without KillItem - see the kCross comment at the Imp). Vanilla
    // never noticed, because young Lara there has no weapons. The
    // randomizer removes the young flag and gives pistols - from then on
    // the Imp dies in andy1 too and needs the full set from andy3 (geometry
    // identical, only the animations are complete there). Unconditional,
    // not bound to options: every run makes andy1's Imps killable.
    if (lowerName == "andy1")
    {
        JsonValue e = JsonValue::makeObject();
        e.set("slot", JsonValue::makeInt(89));
        e.set("fromLevel", JsonValue::makeString("andy3"));
        e.set("replace", JsonValue::makeBool(true));
        e.set("animCount", JsonValue::makeInt(46));   // as in the kCross entry
        moveables.push(e);
        report.notes.push_back("Imp healed: full anim set (with death) from andy3.");
    }

    // --- Level chains (LevelChains.h): models of the grantable items ---
    //
    // Every level that CAN need an item from another level via a chain gets
    // its model slot from the donor - regardless of the order, because
    // whether SCRIPT.DAT grants the item via GIVEOBJ is only decided in the
    // script step, and the level step does not know the order (CLI!). One
    // slot too many costs nothing; an inventory item without a model shows
    // as a broken cheat item. Without "replace": existing slots stay.
    for (size_t ki = 0; ki < kLevelChainCount; ++ki)
    {
        const LevelChain& k = kLevelChains[ki];
        if (!k.slot) continue;                          // story items without a model copy
        std::string kn = k.taker;
        for (char& c : kn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (kn != lowerName) continue;

        JsonValue e = JsonValue::makeObject();
        e.set("slot", JsonValue::makeInt(k.slot));
        e.set("fromLevel", JsonValue::makeString(k.donor));
        moveables.push(e);
        report.notes.push_back(std::string("level chain: model for ") + k.name + " installed.");
    }

    // --- rich: the flare is missing entirely ---
    //
    // Every section gets a full set of flares scattered except the rich
    // levels. The cause is not the scatter but the file - consumables are
    // only laid down where the level can draw them (presentSlots, from
    // Tr5Level::moveableSlots), and measured with SlotList the three rich
    // levels carry none of the three slots a flare needs, while Andrea1
    // carries all three:
    //
    //     7   FLARE_ANIM   Lara's own flare animations and the mesh in her
    //                      hand (draw_flare_meshes and set_flare_arm in
    //                      laraflar.cpp read both from this object)
    //   354   Flare Item   the pickup lying in the level
    //   355   Flare Inv    the model shown in the inventory
    //
    // Installed, not rolled: what the scatter then makes of them is its own
    // business, and it now can. Needed for the glitched secrets, which use
    // flares.
    //
    // Without "replace", as everywhere here: should a slot already be there,
    // the level's own stays.
    if (lowerName == "rich1" || lowerName == "rich2" || lowerName == "rich3")
    {
        static const int kFlareSlots[] = { 7, 354, 355 };

        for (int slot : kFlareSlots)
        {
            JsonValue e = JsonValue::makeObject();
            e.set("slot", JsonValue::makeInt(slot));
            e.set("fromLevel", JsonValue::makeString("Andrea1"));
            moveables.push(e);
        }

        report.notes.push_back("Flare installed from Andrea1 (slots 7, 354, 355) - "
                               "the rich levels carry none of them.");
    }

    // --- joby3: Key Item 8 (203) must lie in the level ---
    //
    // A secret spot in joby3 needs the slot present so a secret patch can
    // later place an item of this type. It is expressly not rolled here -
    // only the model travels along. joby3 does not have the slot in
    // vanilla (checked with SlotAnims: joby2 = 1 mesh from anim 618, joby3
    // = no model), joby2 lies as the donor in the same section.
    //
    // Without "replace": if the slot is already there (say, because a
    // secret patch brought it itself), the existing one stays.
    //
    // For the day the item is really collected: a key item's name and
    // inventory pose stand not in the level but in the CMD_KEY8 entry of
    // joby3's level block in SCRIPT.DAT (DoGameflow, gameflow.cpp).
    // inventry_objects_list is never reset between levels - if joby3 lacks
    // the entry, the key carries the name and pose whichever other level
    // set last.
    if (lowerName == "joby3")
    {
        JsonValue e = JsonValue::makeObject();
        e.set("slot", JsonValue::makeInt(203));
        e.set("fromLevel", JsonValue::makeString("joby2"));
        moveables.push(e);
        report.notes.push_back("Key Item 8 (203) installed from joby2 - present only, not rolled (secret spot).");

        // And Lara's card-swipe animation with it (ANIM_KEYCARD 437): the
        // slot exists in every level, only joby2 has real image data - in
        // joby3 the extra anims are Core stubs (436-439 point at the same
        // frame offset), and the engine otherwise only plays the substitute
        // key anim at Key Hole 8, which does not match the card model. The
        // card sound (sfx 316) travels along in the same move.
        JsonValue la = JsonValue::makeObject();
        la.set("anim", JsonValue::makeInt(437));
        la.set("fromLevel", JsonValue::makeString("joby2"));
        laraAnims.push(la);
        report.notes.push_back("card-swipe anim (Lara 437) implanted from joby2.");
    }

    // --- No flee markers: Larson and Pierre shall fight and die ---
    //
    // Both flee from 40 hit points to an AI_AMBUSH marker with matching OCB
    // and despawn there (GetAITarget/FindAITargetObject, box.cpp;
    // PierreControl clamps the hit points at 40 until then). Setting such a
    // marker for them would be the wrong direction, for two reasons:
    //
    //  1. As pool enemies, the two should always fight and die. That is
    //     exactly what Larson's fade-out and Pierre's loop-proof death
    //     animation were built for.
    //  2. The despawn takes carried items along. GetCarriedItems
    //     (setup.cpp) attaches every pickup within 512 units to the
    //     nearest enemy - a key item next to a fleeing Larson would be gone
    //     after his flight.
    //
    // Without a marker, PierreCanFlee() does not engage, the 40-HP
    // clamp drops, and both die like any other enemy. Core's own markers in
    // Andrea1 and Andrea2 stay untouched - there the vanilla specimens keep
    // their original behavior with their OCB; a swapped enemy loses his OCB
    // (see the swap loop) and thus finds no marker anymore anyway.
    //
    // Tr5Level::addAiObject and the patch field "aiAdds" remain - the
    // mechanics are proven and stay ready for placing AI objects for
    // something else.
    JsonValue aiAdds = JsonValue::makeArray();

    // --- Roll the outfit ---
    //
    // The choice falls here so it hangs on the seed like everything else;
    // the actual transfer is done by the patch path, which knows the donor
    // levels. If the choice falls on the outfit the level wears anyway,
    // nothing goes into the patch - copying onto oneself would be mere
    // ballast.
    JsonValue outfit = JsonValue::makeObject();
    bool outfitSet = false;
    if (opt.randomOutfits)
    {

        // Own family: check andrea BEFORE andy, or "andy" never matches.
        int own = -1;
        if (lowerName.rfind("andrea", 0) == 0)     own = 0;
        else if (lowerName.rfind("andy", 0) == 0)  own = -1;   // young Lara, has none of the three
        else if (lowerName.rfind("joby", 0) == 0)  own = 1;
        else if (lowerName.rfind("rich", 0) == 0)  own = 2;


        static const char* kDonors[3] = { "Andrea1", "joby2", "rich2" };

        // Candidates, out of the SAME list the wardrobe window shows. A
        // vanilla outfit keeps its index, because the donor level and the
        // LaraType byte hang on it; a guest carries only its id, and
        // everything else about it stands in Tr3Source::outfits().
        //
        // Deselected outfits fall out here, and guests fall out entirely
        // while the TR3 switch is off - the wardrobe must not be able to
        // reach into an installation that option leaves disabled.
        struct Cand { int vanilla; std::string id; };
        std::vector<Cand> cand;
        {
            const std::vector<OutfitKind> all = Rando::outfitKinds();
            int vanillaIdx = 0;
            for (size_t k = 0; k < all.size(); ++k)
            {
                // The counter runs over EVERY vanilla entry, deselected ones
                // included - it indexes kDonors, and a hole would shift the
                // rest onto the wrong donor level.
                const bool guest = all[k].source[0] != 0;
                const int mine = guest ? -1 : vanillaIdx++;

                if (guest && !opt.tr3Outfits) continue;
                if (opt.outfitsOff.count(all[k].id)) continue;

                Cand c;
                c.vanilla = mine;
                c.id = all[k].id;
                cand.push_back(c);
            }
        }

        if (lowerName == "joby4")
            report.notes.push_back("outfit: joby4 keeps the diving suit (stays vanilla).");
        else if (cand.empty())
            report.notes.push_back("outfit: nothing chosen in the wardrobe - every level keeps its own.");
        else
        {
            const Cand picked = cand[static_cast<size_t>(rng.below(cand.size()))];

            // A guest outfit: only the skin travels, and even there the head
            // stays the level's own - so no LaraType byte, no holsters, no
            // speech heads. All of that keeps working untouched.
            if (picked.vanilla < 0)
            {
                // A guest outfit brings only the BODY; head, braid, joints,
                // holsters and speech heads come from Andrea1, always.
                //
                // Keeping the level's own head was the first design and it is
                // wrong in three of the four sections, as a full run with
                // nothing but TR3 outfits showed:
                //   andy - the level's own Lara is the YOUNG one, so an adult
                //          body wore a child's head
                //   joby - her cap and the camouflage joints of that outfit
                //          landed on a Tomb Raider III body
                //   rich - laraType 3 sends the engine down the CATSUIT draw
                //          path, which does not use this skin the same way,
                //          so the outfit never appeared at all
                //
                // Handing over just a head would not do: the engine swaps in
                // LARA_SCREAM and the speech heads at runtime, and in an andy
                // level those are young as well. So the whole entourage is
                // fetched first, by the vanilla path that already knows how,
                // and the guest body is laid over it. The patch path runs the
                // two in that order by itself.
                //
                // Andrea1 as the donor, and not merely because it is the
                // simplest: joby2's Lara wears a cap and her hair object is
                // built for it, so a braid on that head spikes through the
                // body. Every Tomb Raider III Lara has a ponytail, and
                // Andrea1's standard braid is the one that fits her.
                //
                // And laraType 5, LARA_PONYTAIL, NOT 0.
                //
                // Zero means LARA_NORMAL, and in a Russia level that is the
                // engine's word for "an untouched vanilla level, so she is
                // wearing the cap": hair.cpp and laraskin.cpp then weld the
                // braid to the CAP head's vertex numbers, which the Andrea1
                // head placed there does not have. The braid then comes away
                // from its four points and stands off the back of her head -
                // only in the Russia section, where the level's own head is
                // the cap head.
                //
                // LARA_PONYTAIL exists for exactly this and is drawn like
                // LARA_NORMAL; it says "standard head with a ponytail" so
                // the braid takes the standard table. It was already used
                // for the vanilla Andrea outfit and simply not carried over
                // to the guests.
                outfit.set("fromLevel", JsonValue::makeString("Andrea1"));
                outfit.set("laraType", JsonValue::makeInt(5));   // LARA_PONYTAIL
                outfit.set("holstersFrom", JsonValue::makeString("joby2"));
                outfit.set("tr3", JsonValue::makeString(picked.id));
                outfitSet = true;
                report.notes.push_back(fmt("outfit: %s gets the guest outfit %s (head from Andrea1).",
                                           in.level.c_str(), picked.id.c_str()));
            }
            else
            {
            const int choice = picked.vanilla;
            if (choice == own)
            {
                report.notes.push_back(fmt("outfit: %s keeps its own.", in.level.c_str()));
                // The own catsuit carries holsters too -
                // the rich levels do not have objects 13-16 in vanilla, and
                // only with them does the engine draw holsters there.
                if (own == 2)
                {
                    outfit.set("holstersFrom", JsonValue::makeString("joby2"));
                    outfitSet = true;
                }
            }
            else
            {
                outfit.set("fromLevel", JsonValue::makeString(kDonors[choice]));

                // Pass the LaraType byte explicitly - the engine keys the
                // braid special-handling (anchoring in hair.cpp, weld table
                // in laraskin.cpp) off the outfit, not the level:
                //   0 = LARA_NORMAL   (Andrea outfit, standard braid)
                //   2 = LARA_BUNHEAD  (Joby outfit: cap braid, also outside
                //                      the Russia levels)
                //   3 = LARA_CATSUIT  (rich2 outfit, its own draw path)
                //   5 = LARA_PONYTAIL (Andrea outfit IN a Russia level:
                //                      there LARA_NORMAL wrongly triggers
                //                      the Russia braid handling, so a
                //                      value that draws like NORMAL but is
                //                      not NORMAL)
                static const int kLaraTypeByte[3] = { 0, 2, 3 };
                int typeByte = kLaraTypeByte[choice];
                if (choice == 0 && lowerName.rfind("joby", 0) == 0)
                    typeByte = 5;
                outfit.set("laraType", JsonValue::makeInt(typeByte));

                // The catsuit donor rich2 brings no holsters - the missing
                // slots then come from joby2 (dark straps, fits the black
                // suit).
                if (choice == 2)
                    outfit.set("holstersFrom", JsonValue::makeString("joby2"));
                outfitSet = true;
                report.notes.push_back(fmt("outfit: %s gets the Lara from %s.",
                                           in.level.c_str(), kDonors[choice]));
            }
            }
        }
    }

    // --- Weapons everywhere ---
    //
    // Resolved as a finished copy list, so the donor rules stand in ONE
    // place (here) and the patch path only executes - the same pattern as
    // with the outfit's LaraType byte. The rules are those of the tool's
    // gold reference from the weapon work ("all 13 levels with the full
    // set"), with two reasoned deviations:
    //
    // 1. The holster slots 13, 14 and 15 are "only what is missing" and
    //    come from Andrea3. Slot 16, by contrast, ALWAYS comes from joby2
    //    and replaces what is there - as the gold reference had it from the
    //    start.
    //
    //    Why that is a trade-off and no matter of course: the four slots
    //    supply Lara's two THIGHS per holster state, and leg, holster and
    //    weapon are ONE mesh - they cannot be separated. Slot 16 from
    //    joby2 thus means: the Deagle sits properly in the holster, but
    //    Rome and catsuit Lara wear joby2's camo-trousers thighs meanwhile.
    //
    //    Andrea3 as the donor for 16 instead would have the reverse price:
    //    consistent thighs but the old revolver in the holster. The weapon
    //    wins, and the mismatched thighs are accepted, since the goal is to
    //    replace the revolver with the Desert Eagle everywhere: drawn (6),
    //    with laser (12), as a pickup (347) and in the inventory the Deagle
    //    was already everywhere else anyway.
    // 2. Andrea3 additionally supplies 352/456/457 (the binoculars
    //    inventory object and the overlays for binoculars and scope) -
    //    only the andy levels lack those, and without 457 the view through
    //    the scope shows Lara's pistol hand (mesh index 0).
    //
    // The patch path skips "only what is missing" entries when the slot is
    // there - which also resolves the rich2 exception (its own special HK
    // pose stays) by itself. Entries whose donor is the level itself
    // already drop out here.
    JsonValue weapons = JsonValue::makeArray();
    if (opt.allWeapons && lowerName != "joby4")
    {
        struct WRule { const char* from; int slot; bool replace; };
        static const WRule kWeaponRules[] = {
            // Andrea3: pistols/Uzi/shotgun/holsters/lasersight - only what is missing
            {"Andrea3",1,false},{"Andrea3",2,false},{"Andrea3",3,false},
            {"Andrea3",13,false},{"Andrea3",14,false},{"Andrea3",15,false},
            {"Andrea3",334,false},{"Andrea3",335,false},{"Andrea3",336,false},{"Andrea3",337,false},
            {"Andrea3",338,false},{"Andrea3",339,false},{"Andrea3",340,false},{"Andrea3",351,false},
            // entourage (only the andy levels lack it)
            {"Andrea3",352,false},{"Andrea3",456,false},{"Andrea3",457,false},
            // joby2: the Desert Eagle is the default - always replace
            {"joby2",6,true},{"joby2",12,true},{"joby2",16,true},{"joby2",347,true},{"joby2",348,true},
            // rich1: HK - "only what is missing" leaves rich2 its special pose
            {"rich1",5,false},{"rich1",345,false},{"rich1",346,false},
        };

        int entries = 0;
        for (const WRule& r : kWeaponRules)
        {
            std::string fromLower;
            for (const char* p = r.from; *p; ++p)
                fromLower.push_back((*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p);
            if (fromLower == lowerName) continue;     // nobody donates to himself

            // rich2 gets NO pistols. Its slot 1 (PISTOLS_ANIM) is filled by
            // Core with a second HK plus a scripted animation set
            // (mesh-identical to slot 5, own anims with KILLITEM and shot
            // sounds - sequence material, richcut2 carries the same slot).
            // The slot stays untouched, and with it the pistol item must
            // not enter the level either: InitialiseLara (setup.cpp) gives
            // Lara starting pistols as soon as PISTOLS_ITEM is loaded, and
            // drawn, her arms would run over the scripted HK anims.
            if (lowerName == "rich2" && (r.slot == 334 || r.slot == 335)) continue;

            JsonValue e = JsonValue::makeObject();
            e.set("slot", JsonValue::makeInt(r.slot));
            e.set("fromLevel", JsonValue::makeString(r.from));
            e.set("replace", JsonValue::makeBool(r.replace));
            weapons.push(e);
            ++entries;
        }
        if (entries > 0)
            report.notes.push_back(fmt("weapons: %d slot rules (Andrea3/joby2/rich1) in the patch.", entries));
    }
    else if (opt.allWeapons)
        report.notes.push_back("weapons: joby4 stays vanilla (diving suit).");

    // --- Weather -----------------------------------------------------------
    //
    // Deliberately the last draw of the run. Everything above has already
    // pulled its numbers, so the extra draw cannot shift a single other
    // result: for the same seed, on and off give the same items, enemies,
    // weapons and outfit, and differ in exactly the one weather byte.
    // Measured over ten seeds and fifteen levels, plus five seeds against a
    // build with the option off, byte for byte.
    //
    // (The GUI checkbox is still part of effectiveSeed, as every option is -
    // so ticking it in the window does change the game. That is the seed
    // changing, not the draw order.)
    //
    // Only levels that can show weather roll at all (weatherRooms, read out
    // of the .trc - see Tr5Level::weatherRooms). In the other seven playable
    // levels rain would fall where the engine refuses to draw it, and the
    // spoiler would promise something the player never sees. Those keep
    // their vanilla byte, which for joby3 means it keeps the snow Core set
    // there and the engine has never shown.
    //
    // The three non-playable files stay out by name, exactly as the GUI
    // keeps them out of a run altogether (Main.cpp, isPlayable). Only the
    // command line can reach them at all, and del of all things HAS a room
    // that would show weather - a cutscene is not the place to surprise
    // anyone with snow.
    const bool cutsceneFile = (lowerName == "del" || lowerName == "richcut2" || lowerName == "title");

    int weatherRoll = -1;
    if (opt.randomWeather && !cutsceneFile && in.weatherRooms > 0)
    {
        weatherRoll = static_cast<int>(rng.below(3));       // 0 dry, 1 rain, 2 snow
        static const char* kName[3] = { "dry", "rain", "snow" };
        const int was = (in.vanillaWeather >= 0 && in.vanillaWeather <= 2) ? in.vanillaWeather : 0;
        report.notes.push_back(fmt("weather: %s (vanilla %s), %d rooms can show it.",
                                   kName[weatherRoll], kName[was], in.weatherRooms));
    }
    else if (opt.randomWeather && cutsceneFile)
        report.notes.push_back("weather: cutscene file - stays vanilla.");
    else if (opt.randomWeather)
        report.notes.push_back("weather: level has no room that could show rain or snow - stays vanilla.");

    patchOut = JsonValue::makeObject();
    patchOut.set("itemEdits", edits);
    if (weatherRoll >= 0) patchOut.set("weather", JsonValue::makeInt(weatherRoll));
    if (moveables.size() > 0) patchOut.set("moveableAdds", moveables);
    if (laraAnims.size() > 0) patchOut.set("laraAnimAdds", laraAnims);
    if (weapons.size() > 0) patchOut.set("weaponAdds", weapons);
    if (aiAdds.size() > 0) patchOut.set("aiAdds", aiAdds);
    if (outfitSet) patchOut.set("outfit", outfit);
    if (soundAdds.size() > 0) patchOut.set("soundAdds", soundAdds);
    if (adds.size() > 0) patchOut.set("itemAdds", adds);
    if (trigMoves.size() > 0) patchOut.set("triggerMoves", trigMoves);
    if (trigAdds.size() > 0) patchOut.set("triggerAdds", trigAdds);
    // The item count from which the planning numbered new items. If the
    // source file carries more items than the level JSON (the weapon base
    // was created after the JSON export), the itemAdds land further back
    // than planned, and every trigger would wake the item two before its
    // enemy. ApplyPatch shifts all TO_OBJECT targets >= itemBase by the
    // true difference with this number.
    patchOut.set("itemBase", JsonValue::makeInt(static_cast<int64_t>(in.items.size())));
    // Deliberately NO itemRemoves - see rule 1 in the header.
    return true;
}

// The selectable enemy kinds with display names. Order and content follow
// the pool table kCross further up - whoever adds a row there adds one
// here. The names are English for the international release; the German UI
// translates them via the Language table (see gui\Language.cpp).
std::vector<OutfitKind> Rando::outfitKinds()
{
    std::vector<OutfitKind> v;

    // The three TR5 brings itself. The ids are what the config and the patch
    // speak; the names are the ones shown in the outfit picker.
    v.push_back({ "andrea", "Lara Andrea", "" });
    v.push_back({ "joby",   "Lara Joby",   "" });
    v.push_back({ "rich",   "Lara Rich (catsuit)", "" });

    // And every guest, straight from the one truth about them. Nothing to
    // keep in step by hand: an outfit added there appears here by itself.
    const std::vector<Tr3Source::OutfitSource> guests = Tr3Source::outfits();
    for (size_t k = 0; k < guests.size(); ++k)
        v.push_back({ guests[k].id, guests[k].name, guests[k].source });

    return v;
}

std::vector<EnemyKind> Rando::enemyKinds()
{
    return {
        { 33, "SWAT",              nullptr },
        { 35, "SWAT (heavy)",      nullptr },
        { 37, "Guard",             nullptr },
        // Engine slot 460 (BLUE_GUARD2): the guard's joby model. In
        // vanilla both models share slot 37 and exclude each other - the
        // new slot makes them possible at the same time. Runs only on this
        // engine.
        { 460, "Guard (Joby)",     nullptr },
        { 39, "Two Gun",           nullptr },
        { 41, "Dog",               nullptr },
        { 45, "Larson",            nullptr },
        { 47, "Pierre",            nullptr },
        { 49, "Mafia (MG)",        nullptr },
        { 51, "Mafia (pistols)",   nullptr },
        { 55, "Crane Guy",         nullptr },
        { 57, "Lion",              nullptr },
        { 59, "Gladiator",         nullptr },
        { 61, "Soldier statue (sword)", nullptr },
        // Engine slot 461 (ROMAN_GOD2): the hammer version from Andrea3.
        // In vanilla it shares slot 61 with the sword and excludes it;
        // with its own slot both can stand in the same level.
        { 461, "Soldier statue (hammer)", nullptr },
        { 63, "Hydra",             nullptr },
        // Engine slot 469 (CYBORG): the VCI cyborg from rich3. In
        // vanilla he sits on slot 67 and is immortal there by design (his
        // object flag undead, he dies only through rich3's own geometry) -
        // the new slot carries a mortal version, while the two puzzle
        // carriers in rich3 keep the old one.
        { 469, "Cyborg",           nullptr },
        { 77, "Maze monster",      nullptr },
        // The dog from joby2 - its own enemy type with its own control,
        // not to be confused with "Dog" (41) from Andrea1.
        { 85, "Huskie",            nullptr },
        // The chef from joby3 - melee fighter with a knife.
        { 87, "Chef",              nullptr },
        { 89, "Imp",               nullptr },

        // The guests from Tomb Raider 3. They sit on slots that carry a
        // model in NONE of the 16 TR5 levels - so they take nothing from
        // any native enemy (measured against all of them).
        { 31, "T-Rex (TR3)",       "tr3" },
        { 75, "Raptor (TR3)",      "tr3" },
        { 53, "Compy (TR3)",       "tr3" },
        { 43, "Vulture (TR3 LA)",  "tr3gold" },
    };
}

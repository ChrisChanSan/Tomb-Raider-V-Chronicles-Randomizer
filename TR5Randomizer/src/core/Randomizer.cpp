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

#include <algorithm>
#include <cstdarg>
#include <cstdio>

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

    // Sektormengen des Zonen-Exports einlesen. Dort steht je Eintrag ein String
    // "raum,x,z" - die PowerShell-Seite nutzt dieselbe Schreibweise als
    // Hashset-Schluessel, und beide Werkzeuge muessen exakt dieselben Sektoren
    // meinen. Fehlt das Feld, kommt hier ein Null-Wert an und die Menge bleibt
    // leer, was ueberall "keine Einschraenkung" bedeutet.
    void readSectorSet(const JsonValue& v, std::set<long long>& out)
    {
        for (size_t i = 0; i < v.size(); ++i)
        {
            const std::string s = v[i].asString();
            // Von Hand zerlegt statt per sscanf: Raum- und Sektorindizes sind nie
            // negativ, damit reicht das hier und die CRT-Warnung entfaellt.
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

    // Welches Item der Pickup-Trigger meint, steht in seinem Referenzwort
    // ("refItem"), nicht in der Befehlsliste - die Befehle sind ausnahmslos
    // seine Wirkung (Tueren, Gegner, Flipmaps).
    //
    // Frueher stand hier ein Umweg: das gemeinte Item wurde daran erkannt, dass
    // ein Object-Befehl auf ein Item auf dem Triggersektor zeigt. Das lieferte
    // die richtige Antwort, aber aus dem falschen Grund - der Exporter las das
    // Referenzwort damals gar nicht und schob es als ersten Object-Befehl in
    // die Liste. Seit der Exporter es als refItem fuehrt, ist der Umweg leer,
    // und dieselbe Schleife wuerde ohne diese Umstellung KEIN Item mehr
    // festhalten. Die Karte bleibt trotzdem stehen: sie wird gebraucht, um den
    // Typ der per Befehl geweckten Objekte nachzuschlagen (Schwarmgetier).
    std::map<int, const JsonValue*> byIndex;
    const JsonValue& items = levelInfo["items"];
    for (size_t i = 0; i < items.size(); ++i)
        byIndex[static_cast<int>(items[i]["index"].asInt())] = &items[i];

    const JsonValue& trigs = levelInfo["triggers"];
    for (size_t t = 0; t < trigs.size(); ++t)
    {
        const JsonValue& tr = trigs[t];
        if (tr["triggerType"].asString() != "Pickup") continue;

        // Welches Item ist gemeint? Zwei Exportstaende sind zu bedienen, und ein
        // Stichtag waere hier gefaehrlich: laege eine aeltere Datei daneben,
        // haette KEIN Trigger mehr ein Item festgehalten, und ortsgebundene
        // Items waeren klanglos in den Pool gerutscht.
        //
        // Neuer Export: das Referenzwort steht als "refItem" da.
        // Aelterer Export: das Wort wurde als erster Object-Befehl ausgegeben.
        //   Es zeigt dann auf ein Item, das auf dem Triggersektor liegt - und
        //   genau daran ist es erkennbar.
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
                        // Das ist das Referenzwort des aelteren Exports, kein
                        // Befehl. Nicht als Wirkung zaehlen.
                        refItem = idx;
                        continue;
                    }
                    // Schwarmgetier zaehlt wie Musik: nicht festhaltend.
                    harmless = isDespawningSwarm(static_cast<int>(target["typeId"].asInt()));
                }
            }

            effects.push_back(type);
            if (type != "PlaySoundtrack" && !harmless) onlySoundtrack = false;
        }

        if (refItem < 0) continue;                  // kein deutbarer Pickup-Trigger
        const std::vector<int> onSector(1, refItem);

        // Ein Trigger, dessen einzige weitere Wirkung Musik ist, haelt das Item
        // nicht fest - bewusst akzeptiertes Opfer, damit z.B. Andrea2s
        // Brechstange randomisierbar bleibt.
        if (effects.empty() || onlySoundtrack)
        {
            if (explain)
                for (int i : onSector)
                    explain->push_back(fmt("Item %d bleibt frei - Pickup-Trigger loest nur Musik bzw. Schwarmgetier aus", i));
            continue;
        }

        for (int i : onSector)
        {
            pinned.insert(i);
            if (explain)
            {
                std::string e;
                for (size_t k = 0; k < effects.size(); ++k) { if (k) e += "+"; e += effects[k]; }
                explain->push_back(fmt("Item %d ortsgebunden - Pickup-Trigger loest aus: %s", i, e.c_str()));
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
    if (secs.size() == 0) { error = "Zonen-Export enthaelt keine Sektoren"; return false; }
    out.sectors.reserve(secs.size());
    for (size_t i = 0; i < secs.size(); ++i)
    {
        ZoneSector s;
        s.room = static_cast<int>(secs[i]["room"].asInt());
        s.sectorX = static_cast<int>(secs[i]["sectorX"].asInt());
        s.sectorZ = static_cast<int>(secs[i]["sectorZ"].asInt());
        s.zone = static_cast<int>(secs[i]["zone"].asInt());
        s.floorY = static_cast<int>(secs[i]["floorY"].asInt());

        // Anmarschsektoren stehen als "raum,x,z" da, in Vorzugsreihenfolge.
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

        // Die Felder unten sind alle optional: ein Level ohne Inselgruppe
        // exportiert sie als null, und dann bleiben die Mengen leer.
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

    const JsonValue& inj = zoneExport["requiredInjections"];
    for (size_t i = 0; i < inj.size(); ++i)
    {
        Injection j;
        j.typeId = static_cast<int>(inj[i]["typeId"].asInt());
        j.room = static_cast<int>(inj[i]["room"].asInt());
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
        out.roomOrigins.push_back(r);
        if (rooms[i]["waterRoom"].asBool(false))
            out.waterRooms.insert(r.room);
    }

    // Antitrigger-Ziele einsammeln - siehe RandoInput::antitriggered.
    {
        const JsonValue& trigs = levelInfo["triggers"];
        for (size_t t = 0; t < trigs.size(); ++t)
        {
            const std::string art = trigs[t]["triggerType"].asString();
            if (art != "AntiTrigger" && art != "HeavyAntiTrigger") continue;
            const JsonValue& cmds = trigs[t]["commands"];
            for (size_t c = 0; c < cmds.size(); ++c)
                if (cmds[c]["type"].asString() == "Object")
                    out.antitriggered.insert(static_cast<int>(cmds[c]["index"].asInt(-1)));
        }
    }
    if (out.roomOrigins.empty()) { error = "Level-Info enthaelt keine Raeume"; return false; }

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

    // --- Gekoppelte Trigger ---
    //
    // Aus der Kuration kommt nur der Item-Index. Wo der Trigger heute liegt,
    // wird hier aus den Leveldaten gelesen und nicht von Hand gepflegt: eine
    // eingetragene Koordinate waere genau die Angabe, die beim naechsten
    // Levelumbau still falsch wird.
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

            // Dasselbe Zwei-Wege-Lesen wie in findTriggerPinnedItems: neuer
            // Export fuehrt das Referenzwort als refItem, aelterer als ersten
            // Object-Befehl auf ein Item des eigenen Sektors.
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

bool Rando::sectorAllowedFor(const RandoInput& in,
                             const KeyConstraint* k,
                             const ZoneSector& s,
                             int typeId,
                             bool mustBeOnIsland)
{
    // Das Waffenverbot haengt am LEVEL, nicht am Typ-Constraint: eine
    // Schrotflinte ist kein Schluessel und wuerde sonst gar nicht geprueft.
    if (in.weaponTypes.count(typeId) && in.noWeaponRooms.count(s.room)) return false;
    if (!k) return true;

    const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
    if (s.zone > k->maxZone) return false;
    if (!k->allowRooms.empty() &&
        !std::binary_search(k->allowRooms.begin(), k->allowRooms.end(), s.room)) return false;
    if (std::binary_search(k->excludeRooms.begin(), k->excludeRooms.end(), s.room)) return false;
    if (!k->islandSectors.empty() && !k->islandSectors.count(sk)) return false;
    if (k->bannedSectors.count(sk)) return false;
    if (k->forbiddenSectors.count(sk)) return false;
    // Kleineres y liegt hoeher: ein Sektor unterhalb der Schranke ist zu hoch.
    // Sie gilt nur in ihrem Geltungsbereich, und ohne Geltungsbereich ueberall.
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
        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro->second.originX) + s.sectorX * 1024 + 512 + po.dx));
        a.set("y", JsonValue::makeInt(s.floorY + po.dy));
        a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro->second.originZ) + s.sectorZ * 1024 + 512 + po.dz));
        a.set("ocb", JsonValue::makeInt(0));
        adds.push(a);
    };

    // Ebene 1: der Fuelltyp auf JEDEN gueltigen Sektor. Nicht nur auf die, die
    // kein Constraint abdeckt - sonst fehlte der Marker ueberall dort, wo AUCH
    // ein Key-Item liegen darf, und diese Sektoren saehen aus, als waeren sie
    // fuer gewoehnliche Pickups gesperrt.
    if (fillType > 0)
    {
        for (const ZoneSector& s : in.sectors) { marker(fillType, s); ++fillSectors; }
    }

    // Ebene 2: je Constraint-Typ sein eigener, engerer Bereich. Geprueft wird
    // mit derselben Funktion, die auch die Platzierung benutzt.
    //
    // Hat ein Typ ein requireInIsland, wird der PFLICHTBEREICH gezeichnet, also
    // der uebliche Bereich UND die Insel. Formal duerfte eine zweite Instanz
    // desselben Typs auch ausserhalb liegen - requireInIsland sagt nur "eine
    // davon dort". Gezeichnet wird trotzdem die engere Menge: das ist die, die
    // der Nutzer festgelegt hat und pruefen will, und genau so macht es auch
    // das PowerShell-Werkzeug, gegen das er seine Kuration bisher abgeglichen
    // hat. Zwei Werkzeuge, die dasselbe Level unterschiedlich zeichnen, waeren
    // schlimmer als eine formale Unschaerfe.
    for (const KeyConstraint& k : in.constraints)
    {
        const bool pflichtbereich = k.islandSectors.empty() && !k.requireInIsland.empty();
        MarkerCount mc;
        mc.typeId = k.typeId;
        std::set<int> rooms;
        for (const ZoneSector& s : in.sectors)
        {
            if (!sectorAllowedFor(in, &k, s, k.typeId, pflichtbereich)) continue;
            marker(k.typeId, s);
            ++mc.sectors;
            rooms.insert(s.room);
        }
        mc.rooms = static_cast<int>(rooms.size());
        perType.push_back(mc);
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

    // Levelname in Kleinbuchstaben - mehrere Bloecke unten entscheiden daran
    // (Waffenstreuung, Outfit, Waffenregeln).
    std::string klein;
    for (char c : in.level) klein.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c);

    // Jedes Level bekommt seinen eigenen Wuerfelstrom. Vorher speiste
    // DERSELBE globale Seed jeden Levellauf mit derselben Zugfolge - und weil
    // die Allsanity-Pools fast identisch sind, bekamen alle Level fast
    // dieselben Gegnertypen in derselben Reihenfolge ("als haette er nur ein
    // Set ausgewaehlt und das auf alle gleich gemacht", Nutzerbeobachtung im
    // Spiel). FNV-1a ueber den Levelnamen entkoppelt die Stroeme;
    // deterministisch bleibt alles: derselbe Seed erzeugt weiterhin dasselbe
    // Spiel, nur eben je Level ein anderes.
    uint64_t levelSeed = seed;
    for (char c : klein)
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

    // Der gekoppelte Zug hebt genau die Fesselung auf, die Regel 2 anlegt: das
    // Item darf wandern, weil sein Trigger mitkommt. Die Handkuration
    // (fixedItems) bleibt davon unberuehrt - wer dort steht, bleibt liegen,
    // egal was mit seinem Trigger ginge.
    std::map<int, const CoupledTrigger*> coupled;
    for (const CoupledTrigger& c : in.coupledTriggers)
    {
        if (std::find(in.fixedItems.begin(), in.fixedItems.end(), c.itemIndex) != in.fixedItems.end())
            continue;
        coupled[c.itemIndex] = &c;
        pinned.erase(c.itemIndex);
    }

    // OCB 5 und 10: solche Pickups sind in Vanilla gar nicht aufnehmbar.
    // InitialisePickup (game/init.cpp:470) setzt ein OCB-5-Item auf
    // ITEM_INVISIBLE und kehrt zurueck; PickUpCollision (game/pickup.cpp:737)
    // steigt bei ocb 5 und 10 sofort aus. Sie werden erst durch etwas anderes
    // freigeschaltet. Wuerden wir sie verschieben und dabei - wie bei jedem
    // bewegten Item noetig - OCB auf 0 setzen, laegen sie ab sofort sichtbar
    // und aufnehmbar da. Das ist kein Ortswechsel mehr, sondern eine
    // Verhaltensaenderung, und im Zweifel wird gesperrt statt geoeffnet.
    for (const LevelItem& it : in.items)
    {
        const int ocb = it.ocb & 0x3F;
        if (ocb == 5 || ocb == 10) pinned.insert(it.index);
    }

    // Freie Sektoren. Sortierte Reihenfolge ist Pflicht: die Zufallsauswahl
    // greift per Index zu, und eine andere Ausgangsreihenfolge waere ein
    // anderes Ergebnis fuer denselben Seed.
    std::vector<ZoneSector> pool = in.sectors;
    std::sort(pool.begin(), pool.end(), [](const ZoneSector& a, const ZoneSector& b) {
        if (a.room != b.room) return a.room < b.room;
        if (a.sectorX != b.sectorX) return a.sectorX < b.sectorX;
        return a.sectorZ < b.sectorZ;
    });
    std::set<long long> used;

    // Welche Items kommen ueberhaupt in Frage?
    std::vector<LevelItem> movableKey, movableOther;
    for (const LevelItem& it : in.items)
    {
        if (!isPickupType(it.typeId)) continue;
        if (pinned.count(it.index)) { ++report.pinnedItems; continue; }
        if (conByType.count(it.typeId)) movableKey.push_back(it);
        else                            movableOther.push_back(it);
    }

    // RequireTypes: von diesen Typen muss MINDESTENS EINE Instanz innerhalb der
    // Inselgruppe liegen. joby2 erzwingt die Regel: Key Item8 schaltet ueber den
    // Kran die Flipmap frei, die den Rest des Levels oeffnet. Liegt keine
    // Instanz davor, kommt Lara nie an den Kran.
    //
    // Als Sektorfilter ist das nicht ausdrueckbar. Ein Filter beantwortet "darf
    // DIESE Instanz hierhin?", die Forderung betrifft aber alle Instanzen eines
    // Typs zusammen - vier einzeln zulaessige Plaetze koennen gemeinsam trotzdem
    // falsch sein. Deshalb wird eine Instanz ausgewaehlt und ihr Kandidatensatz
    // auf die Insel eingeschraenkt.
    //
    // Erfuellt ein ortsfestes Item die Forderung bereits, ist nichts zu tun - es
    // bleibt ja liegen, wo es ist. Sonst faellt die Wahl auf die bewegliche
    // Instanz mit dem kleinsten Index, damit sie nicht vom Seed abhaengt.
    std::map<int, int> forceIsland;      // typeId -> Item, das auf die Insel muss
    if (opt.shuffleKeyItems)
    {
        for (const KeyConstraint& k : in.constraints)
        {
            if (k.requireInIsland.empty()) continue;

            bool satisfied = false;
            int  firstMovable = -1;
            for (const LevelItem& it : in.items)      // nach Index sortiert
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
                report.error = fmt("Typ %d muss mindestens einmal in der Inselgruppe liegen, hat dort aber "
                                   "weder ein ortsfestes Vorkommen noch eine bewegliche Instanz.", k.typeId);
                return false;
            }
            forceIsland[k.typeId] = firstMovable;
            report.notes.push_back(fmt("Typ %d: Item %d wird in die Inselgruppe gezwungen (Pflichtvorkommen).",
                                       k.typeId, firstMovable));
        }
    }

    JsonValue edits = JsonValue::makeArray();
    JsonValue adds = JsonValue::makeArray();

    // Endstand jedes Items nach dem Lauf. Bewegte bekommen ihre neue Position,
    // alle anderen behalten die aus Vanilla - beides zusammen braucht die
    // Traegerpruefung ganz unten.
    std::map<int, LevelItem> finalOf;
    for (const LevelItem& it : in.items) finalOf[it.index] = it;

    // Manche Objekte tragen ihren Mesh-Ursprung nicht dort, wo das Modell ihn
    // annimmt: sie stecken dann im Boden oder stehen am Sektorrand statt in der
    // Mitte. Gilt fuer JEDE Platzierung dieses Typs - verschoben wie injiziert -,
    // deshalb hier zentral und nicht am einzelnen Eintrag.
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
        const int py = s.floorY + po.dy;
        const int pz = ro.originZ + s.sectorZ * 1024 + 512 + po.dz;

        // Regel 3: Sockel- und Versteck-OCB ueberleben einen Ortswechsel nicht.
        // Ausnahme sind Typen aus KEEP_OCB, bei denen die OCB nicht die alte
        // Stelle beschreibt, sondern die Aufnahmeart - ohne sie ist das Objekt
        // am neuen Platz gar nicht aufnehmbar.
        const int neueOcb = in.keepOcbTypes.count(it.typeId) ? it.ocb : 0;

        LevelItem& fin = finalOf[it.index];
        fin.room = s.room;
        fin.x = px;
        fin.y = py;
        fin.z = pz;
        fin.ocb = neueOcb;
        // Sektor mitfuehren, nicht nur die Weltkoordinate: der gekoppelte
        // Triggerzug braucht ihn, und ihn hinterher aus x/z zurueckzurechnen
        // waere eine zweite Quelle derselben Wahrheit.
        fin.sectorX = s.sectorX;
        fin.sectorZ = s.sectorZ;

        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(it.index));
        e.set("room", JsonValue::makeInt(s.room));
        e.set("x", JsonValue::makeInt(px));
        e.set("y", JsonValue::makeInt(py));
        e.set("z", JsonValue::makeInt(pz));
        e.set("ocb", JsonValue::makeInt(neueOcb));
        edits.push(e);
        used.insert(secKey(s.room, s.sectorX, s.sectorZ));
    };

    // Kandidatensektoren eines Items unter seinen Zwangsbedingungen.
    auto candidatesFor = [&](const LevelItem& it, std::vector<const ZoneSector*>& out)
    {
        out.clear();
        auto ci = conByType.find(it.typeId);
        const KeyConstraint* k = (ci == conByType.end()) ? nullptr : ci->second;

        auto fi = forceIsland.find(it.typeId);
        const bool mustBeOnIsland = (fi != forceIsland.end() && fi->second == it.index);
        // Wandert der Trigger mit, kommt nur ein Sektor in Frage, der ihn auch
        // aufnehmen kann. Sonst waehlt die Platzierung eine Stelle, an der der
        // Patch spaeter abbricht - und ein abgebrochener Patch mitten im Lauf
        // ist schlechter als eine engere Auswahl.
        const bool needsTriggerRoom = coupled.count(it.index) != 0;

        for (const ZoneSector& s : pool)
        {
            const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
            if (used.count(sk)) continue;
            if (needsTriggerRoom && !in.triggerSafeSectors.count(sk)) continue;
            if (!Rando::sectorAllowedFor(in, k, s, it.typeId, mustBeOnIsland)) continue;
            out.push_back(&s);
        }
    };

    // Zuerst die Key-Items, und darunter die am staerksten eingeschraenkten
    // zuerst. Sonst belegt ein freier Typ genau den letzten Sektor, den ein
    // engerer Typ noch gebraucht haette, und die Platzierung scheitert an
    // etwas, das leicht vermeidbar gewesen waere.
    if (opt.shuffleKeyItems)
    {
        std::vector<std::pair<size_t, size_t>> order;  // (Anzahl Kandidaten, Position)
        for (size_t i = 0; i < movableKey.size(); ++i)
        {
            std::vector<const ZoneSector*> c;
            candidatesFor(movableKey[i], c);
            order.push_back({ c.size(), i });
        }
        std::sort(order.begin(), order.end(), [&](const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) {
            if (a.first != b.first) return a.first < b.first;
            return movableKey[a.second].index < movableKey[b.second].index;   // stabiler Gleichstand
        });

        for (const auto& o : order)
        {
            const LevelItem& it = movableKey[o.second];
            std::vector<const ZoneSector*> c;
            candidatesFor(it, c);
            if (c.empty())
            {
                report.ok = false;
                report.error = fmt("Kein gueltiger Platz fuer Item %d (typeId %d) - Zwangsbedingungen zu eng.",
                                   it.index, it.typeId);
                return false;
            }
            placeAt(it, *c[static_cast<size_t>(rng.below(c.size()))]);
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
                // Kein harter Fehler: gewoehnliche Pickups sind nicht
                // fortschrittsrelevant, ein nicht verschobenes Item bleibt
                // schlicht liegen, wo es war.
                report.notes.push_back(fmt("Item %d (typeId %d) bleibt an Ort und Stelle - keine freien Sektoren mehr.",
                                           it.index, it.typeId));
                continue;
            }
            placeAt(it, *c[static_cast<size_t>(rng.below(c.size()))]);
            ++report.movedOtherItems;
        }
    }

    // Pflicht-Injektionen. Diese haengen sich hinten an die Itemliste an und
    // verschieben deshalb keine bestehenden Indizes - anders als ein Entfernen.
    for (const Injection& j : in.injections)
    {
        std::vector<const ZoneSector*> c;
        for (const ZoneSector& s : pool)
        {
            if (s.room != j.room) continue;
            // Bei festem Sektor zaehlt nur er, und ob er schon belegt ist,
            // spielt keine Rolle: mehrere Injektionen duerfen sich dort stapeln.
            if (j.hasSector)
            {
                if (s.sectorX == j.sectorX && s.sectorZ == j.sectorZ) c.push_back(&s);
                continue;
            }
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            c.push_back(&s);
        }
        if (c.empty())
        {
            report.ok = false;
            report.error = j.hasSector
                ? fmt("Pflicht-Injektion typeId %d: Sektor %d,%d,%d ist keine gueltige Ablage.",
                      j.typeId, j.room, j.sectorX, j.sectorZ)
                : fmt("Pflicht-Injektion typeId %d: in Raum %d ist kein freier gueltiger Sektor.",
                      j.typeId, j.room);
            return false;
        }
        const ZoneSector& s = j.hasSector ? *c[0] : *c[static_cast<size_t>(rng.below(c.size()))];
        const RoomOrigin& ro = origin[s.room];

        const PlacementOffset po = offFor(j.typeId);
        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(j.typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512 + po.dx));
        a.set("y", JsonValue::makeInt(s.floorY + po.dy + j.yOffset));
        a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s.sectorZ * 1024 + 512 + po.dz));
        a.set("ocb", JsonValue::makeInt(0));
        adds.push(a);
        // Nur automatisch gewaehlte Sektoren werden belegt. Ein handgesetzter
        // bleibt frei, sonst koennte dort nur ein einziges Item liegen.
        if (!j.hasSector) used.insert(secKey(s.room, s.sectorX, s.sectorZ));
        ++report.injected;
        report.notes.push_back(j.hasSector
            ? fmt("Injiziert: typeId %d auf %d,%d,%d (%s)", j.typeId, s.room, s.sectorX, s.sectorZ, j.reason.c_str())
            : fmt("Injiziert: typeId %d in Raum %d (%s)", j.typeId, j.room, j.reason.c_str()));
    }

    // --- Waffen streuen ---
    //
    // Zufaellige Streuung (Nutzerentscheidung): der Seed bestimmt, WELCHE der
    // durch den Import nutzbaren Waffen ein Level zusaetzlich als Pickup
    // bekommt und wie viel Munition dazu. Nur Waffen, die das Level nicht
    // ohnehin als Item auslegt - vorhandene werden normal mitgemischt, und
    // mehr Munition fuer Bestandwaffen ist Sache der Item-Abundance (#36).
    //
    // Pistolen (334) fehlen mit Absicht: Lara startet ueberall mit ihnen,
    // sobald der Modellslot da ist - und rich2 darf sie gar nicht haben
    // (Slot 1 traegt dort Cores gescriptete Zweit-HK, siehe Waffenregeln).
    //
    // Die Sektorwahl laeuft ueber sectorAllowedFor ohne Constraint: damit
    // greift genau eine Regel, das Waffenverbot je Raum (noWeaponRooms,
    // rich1s Scientist). Budget: MAX_ITEMS ist seit der Verdopplung 512,
    // hier wird bei 500 aufgehoert, und die spaeter gesetzten Zusatzgegner
    // sind schon abgezogen.
    // Ein freier, fuer diesen Typ zulaessiger Sektor. Ohne Constraint, damit
    // genau eine Regel greift: das Waffenverbot je Raum (noWeaponRooms,
    // rich1s Scientist). Wird von der Waffenstreuung UND der Item-Abundance
    // genutzt, deshalb steht sie vor beiden.
    auto freieStelle = [&](int typeId) -> const ZoneSector* {
        std::vector<const ZoneSector*> c;
        for (const ZoneSector& s : pool)
        {
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            if (!Rando::sectorAllowedFor(in, nullptr, s, typeId, false)) continue;
            c.push_back(&s);
        }
        return c.empty() ? nullptr : c[static_cast<size_t>(rng.below(c.size()))];
    };

    if (opt.allWeapons && klein != "joby4")
    {
        struct WStreu { int waffe; int munition[2]; };
        static const WStreu kStreu[] = {
            { 336, { 337,  -1 } },     // Uzi + Uzi-Munition
            { 338, { 339, 340 } },     // Schrotflinte + Normal-/Wideshot
            { 345, { 346,  -1 } },     // HK + HK-Munition
            { 347, { 348,  -1 } },     // Deagle + Deagle-Munition
        };

        std::set<int> schonDa;
        for (const LevelItem& it : in.items) schonDa.insert(it.typeId);

        int budget = 500 - static_cast<int>(in.items.size())
                         - static_cast<int>(adds.size())
                         - (opt.addEnemies ? opt.enemiesPerLevel : 0);

        auto lege = [&](int typeId, const ZoneSector& s)
        {
            const RoomOrigin& ro = origin[s.room];
            const PlacementOffset po = offFor(typeId);
            JsonValue a = JsonValue::makeObject();
            a.set("typeId", JsonValue::makeInt(typeId));
            a.set("room", JsonValue::makeInt(s.room));
            a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512 + po.dx));
            a.set("y", JsonValue::makeInt(s.floorY + po.dy));
            a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s.sectorZ * 1024 + 512 + po.dz));
            a.set("ocb", JsonValue::makeInt(0));
            adds.push(a);
            used.insert(secKey(s.room, s.sectorX, s.sectorZ));
            ++report.weaponsScattered;
            --budget;
        };

        for (const WStreu& w : kStreu)
        {
            if (schonDa.count(w.waffe)) continue;          // legt das Level selbst aus
            if (rng.below(2) == 0) continue;               // Muenzwurf je Waffe

            if (budget < 2) { report.notes.push_back("Waffenstreuung: Item-Budget erschoepft."); break; }
            const ZoneSector* sw = freieStelle(w.waffe);
            if (!sw) { report.notes.push_back(fmt("Waffenstreuung: kein Platz fuer typeId %d.", w.waffe)); continue; }
            lege(w.waffe, *sw);

            const int packs = 1 + static_cast<int>(rng.below(3));   // 1..3
            int gelegt = 0;
            for (int n = 0; n < packs && budget > 0; ++n)
            {
                const int mun = (w.munition[1] < 0) ? w.munition[0]
                              : w.munition[rng.below(2)];
                const ZoneSector* sm = freieStelle(mun);
                if (!sm) break;
                lege(mun, *sm);
                ++gelegt;
            }
            report.notes.push_back(fmt("Waffenstreuung: typeId %d + %d Munitionspacks.", w.waffe, gelegt));
        }
    }

    // --- Item-Abundance: zusaetzliche Ausruestung streuen (#36) ---
    //
    // Manche Level sind ausgesprochen karg: Andy2 hat auf 254 Raeume ganze
    // zehn Pickups, davon drei Secrets - bei 348 gueltigen Ablageflaechen
    // nach der Kuration. extraPickups legt bis zu N weitere Verbrauchsgueter
    // aus.
    //
    // Gestreut wird nur, was im Level auch ein MODELL hat - sonst gaebe es
    // nichts zu zeichnen. Das sind zwei Quellen: was das Level ohnehin
    // auslegt, und was der Waffenimport hineinbringt.
    //
    // Der Waffenimport ist der wichtigere Fall. Munition liegt vanilla nur in
    // 3 bis 5 von 15 Leveln (gemessen: Uzi 5, Schrot 3, HK 3, Deagle 4) -
    // seit jedes Level das volle Waffenset bekommt, stuenden ueber zehn
    // Level mit Waffen ohne passende Munition da. Deshalb bekommen die
    // Munitionssorten des Sets ein festes Grundgewicht, unabhaengig vom
    // Vanilla-Bestand.
    //
    // Pistolenmunition (335) faellt raus: Pistolen haben unbegrenzt Schuss,
    // und in keinem der 15 Level liegt ein einziges Stueck davon. Fuer rich2
    // sind Pistolen samt Munition ganz gesperrt - sein Slot 1 traegt Cores
    // gescriptete Zweit-HK, und schon das blosse Vorhandensein des
    // Pistolen-Items gaebe Lara Startpistolen (setup.cpp:111).
    //
    // Die Waffen selbst kommen nur in den Topf, wenn das Level sie noch
    // nirgends auslegt: die Streuung oben wuerfelt je Waffe eine Muenze, und
    // bei Pech ist die Waffe zwar importiert, aber nirgends zu finden. So
    // ist sie es doch - Doppelstuecke gibt es dadurch keine.
    //
    // Alles unter typeId 334 bleibt aussen vor: Schluessel, Puzzleteile,
    // Quest-Gegenstaende und vor allem die Secrets (PICKUP_ITEM4 = 223) -
    // ein zusaetzliches Secret wuerde die Levelzaehlung verfaelschen. Ebenso
    // aussen vor: Einzelstuecke wie Lasersight/Fernglas/Schalldaempfer und
    // der Armbrustbolzen (Geschoss, kein Pickup).
    //
    // "Weniger als Vanilla" gibt es bewusst nicht: Items entfernen wuerde
    // alle nachfolgenden Indizes verschieben und damit jeden "Object N"-
    // Verweis der Trigger brechen - die Grundregel des ganzen Werkzeugs.
    //
    // joby4 bleibt aussen vor wie ueberall sonst: Lara steckt dort im
    // Taucheranzug und traegt keine Waffen. Die Munitionsslots liegen in der
    // Datei zwar herum, aber Nachschub fuer Waffen, die es im Level nicht
    // gibt, waere nur Deko.
    if (opt.extraPickups > 0 && klein == "joby4")
        report.notes.push_back("Item-Abundance: joby4 bleibt vanilla (Taucheranzug).");
    else if (opt.extraPickups > 0)
    {
        // Alle Waffen und Munitionssorten, die TR5 als Pickup kennt
        // (Nutzervorgabe: alles in den Topf, was das Level tragen kann).
        // Zu jeder Waffe ihre Munition - eine Waffe ohne Nachschub ist nach
        // zwei Kaempfen totes Gewicht.
        struct Streuwaffe { int waffe; int munition[2]; };
        static const Streuwaffe kWaffenGut[] = {
            { 336, { 337,  -1 } },     // Uzi
            { 338, { 339, 340 } },     // Schrotflinte normal / Wideshot
            { 341, { 342, 343 } },     // Armbrust normal / explosiv
            { 345, { 346,  -1 } },     // HK
            { 347, { 348,  -1 } },     // Deagle
        };
        // Verbrauchsgueter ohne zugehoerige Waffe.
        static const int kSonstiges[] = { 349, 350, 354 };   // Bigmedi, Smallmedi, Fackel

        // Was liegt im Level - Vanilla-Bestand plus das, was Waffenstreuung
        // und Pflicht-Injektionen eben erst dazugelegt haben?
        std::map<int, int> haeufig;
        for (const LevelItem& it : in.items) ++haeufig[it.typeId];
        for (size_t i = 0; i < adds.size(); ++i)
            ++haeufig[static_cast<int>(adds[i]["typeId"].asInt())];

        // Kann das Level diesen Typ zeichnen? Entweder der Modellslot steht
        // schon in der Datei, oder der Waffenimport bringt ihn mit (Slots aus
        // kWaffen weiter unten: Andrea3 334-340, joby2 347/348, rich1
        // 345/346 - die Armbrust ist NICHT dabei, sie bleibt den Leveln
        // vorbehalten, die sie selbst fuehren).
        const bool waffensetDa = opt.allWeapons && klein != "joby4";
        auto zeichenbar = [&](int typ) -> bool {
            if (in.vorhandeneSlots.count(typ)) return true;
            if (!waffensetDa) return false;
            switch (typ)
            {
            case 334: case 335: case 336: case 337:
            case 338: case 339: case 340:
            case 345: case 346: case 347: case 348: return true;
            default: return false;
            }
        };

        // Lose je Typ: ein Los je vorhandenem Stueck erhaelt den Charakter
        // des Levels, das Grundgewicht sorgt fuer Nachschub.
        std::map<int, int> lose;
        auto biete = [&](int typ, int grundgewicht)
        {
            if (!zeichenbar(typ)) return;
            std::map<int, int>::const_iterator f = haeufig.find(typ);
            lose[typ] += grundgewicht + (f == haeufig.end() ? 0 : f->second);
        };

        for (const Streuwaffe& w : kWaffenGut)
        {
            for (int m = 0; m < 2; ++m)
                if (w.munition[m] > 0) biete(w.munition[m], 4);
            // Die Waffe selbst nur, wenn sie noch nirgends liegt: die
            // Streuung oben wuerfelt je Waffe eine Muenze, und bei Pech waere
            // sie zwar importiert, aber nirgends zu finden. Doppelstuecke
            // gibt es so keine.
            if (!haeufig.count(w.waffe)) biete(w.waffe, 2);
        }
        for (int t : kSonstiges) biete(t, 2);

        // rich2: weder Pistolen noch ihre Munition, nie (siehe oben).
        if (klein == "rich2") { lose.erase(334); lose.erase(335); }

        std::vector<int> topf;                 // Ziehungstopf, ein Eintrag je Los
        for (std::map<int, int>::const_iterator it = lose.begin(); it != lose.end(); ++it)
            for (int n = 0; n < it->second; ++n) topf.push_back(it->first);

        if (topf.empty())
            report.notes.push_back("Item-Abundance: das Level fuehrt keine streubaren "
                                   "Verbrauchsgueter - nichts hinzugefuegt.");
        else
        {
            const int typenVorher = static_cast<int>(std::set<int>(topf.begin(), topf.end()).size());
            int budget = 500 - static_cast<int>(in.items.size())
                             - static_cast<int>(adds.size())
                             - (opt.addEnemies ? opt.enemiesPerLevel : 0);
            int wieViele = opt.extraPickups;
            if (wieViele > budget) wieViele = budget;

            for (int n = 0; n < wieViele && !topf.empty(); ++n)
            {
                const int typ = topf[static_cast<size_t>(rng.below(topf.size()))];
                const ZoneSector* s = freieStelle(typ);
                if (!s)
                {
                    // Fuer diesen Typ gibt es nirgends mehr einen erlaubten
                    // freien Sektor - in rich2 etwa ist fast das ganze Level
                    // waffenfreie Zone, dort kommt keine Munition unter. Der
                    // Typ fliegt aus dem Topf, die uebrigen wuerfeln weiter;
                    // ein Abbruch hier haette wegen EINES gesperrten Typs auch
                    // die Medipacks gekostet. Erneut frei werden kann er nicht,
                    // Sektoren werden im Verlauf nur belegt, nie frei.
                    topf.erase(std::remove(topf.begin(), topf.end(), typ), topf.end());
                    --n;
                    continue;
                }
                const RoomOrigin& ro = origin[s->room];
                const PlacementOffset po = offFor(typ);
                JsonValue a = JsonValue::makeObject();
                a.set("typeId", JsonValue::makeInt(typ));
                a.set("room", JsonValue::makeInt(s->room));
                a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s->sectorX * 1024 + 512 + po.dx));
                a.set("y", JsonValue::makeInt(s->floorY + po.dy));
                a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s->sectorZ * 1024 + 512 + po.dz));
                a.set("ocb", JsonValue::makeInt(0));
                adds.push(a);
                used.insert(secKey(s->room, s->sectorX, s->sectorZ));
                ++report.extraPickups;

                // Eine Waffe genau einmal: sie liegt im Topf, damit sie
                // ueberhaupt auffindbar ist - ein zweites Exemplar waere nur
                // Munition mit unnoetigem Modell davor.
                bool istWaffe = false;
                for (const Streuwaffe& w : kWaffenGut) if (w.waffe == typ) istWaffe = true;
                if (istWaffe)
                    topf.erase(std::remove(topf.begin(), topf.end(), typ), topf.end());
            }
            report.notes.push_back(fmt("Item-Abundance: %d von %d zusaetzlichen Pickups gestreut "
                                       "(%d streubare Typen im Level).",
                                       report.extraPickups, opt.extraPickups, typenVorher));
        }
    }

    // --- Cross-Gegnermix: den Pool um fremde Typen erweitern ---
    //
    // Die globale Pool-Tabelle ist die Nutzerliste "Gegner Pool.txt", je Typ
    // mit dem Spenderlevel, das ihn MIT Vanilla-Instanzen traegt - nur dort
    // liegen Modell UND Samples beisammen. Die Sound-Eintraege sind die
    // bekannten Ausnahmen, deren Samples selbst dem Spender fehlen (Larsons
    // Sterbeklaenge, Pierres nie ausgelieferter Schuss - beide vom Nutzer im
    // Soundboard ausgesucht und im Spiel geprueft).
    //
    // Hydra (63) fehlt mit Absicht: Nutzerentscheidung steht aus, und ihre
    // drei Items teilen sich eine Position, deren OCB das Kettenglied waehlt
    // (hydra.cpp:18) - das vertraegt keine einfache Platzierung. Chef,
    // Scientist und Guardian stehen aus bekannten Gruenden nirgends im Pool.
    //
    // Je Level hoechstens DREI fremde Typen, vom Seed gewaehlt: jeder Typ
    // kostet Texturseiten und Samples im Ziel, und das Level soll sein
    // Gesicht behalten. Importiert wird spaeter nur, was wirklich verwendet
    // wurde - und zwar mit replace, damit auch ein vorhandener, aber nie
    // bespielter Slot (joby5s Guard ohne Samples) vollstaendig vom Spender
    // kommt.
    // "begleiter": Slots, die der Typ zwingend mitbringen muss. Mafia2 traegt
    // seinen Oberkoerper als Tauschhaelfte aus MESHSWAP2 = Slot 406
    // (setup.cpp:1443: Meshes 7/10/13, meshswap_meshbits 0x2400 schon ab
    // Initialise) - ohne den Slot ist der Torso unsichtbar (im Spiel
    // gesehen). MESHSWAP2 nutzt sonst niemand, der Import ist konfliktfrei.
    //
    // MESHSWAP1 dagegen ist ein GETEILTER Slot mit levelabhaengigem Inhalt
    // (SWAT-Familie: Meshes 10/13; Imp: 10; TwoGun: 10<-2; Gladiator: 0-15;
    // Roman God: 0-16; Hitman: 0-19) - den kann man nicht einfach ersetzen,
    // ohne die einheimischen Nutzer des Ziels zu brechen. Die Folgen sieht
    // man bei Imp-Kopf und Statuen-Schadensmeshes; die Loesung (eigene
    // Ausweichslots in der Engine oder Mesh-genaues Einsetzen) ist als
    // Aufgabe vermerkt und NICHT Teil dieser Tabelle.
    // "swapVon"/"swapDst": Tauschhaelften des Typs. MESHSWAP1 (405) ist je
    // Level mit dem Inhalt seiner EIGENEN Gegner belegt - ein fremder Gegner
    // braucht den Satz eines Levels, das ihn WIRKLICH traegt, und der landet
    // beim Import unter dem Ausweichslot swapDst (399-402, in keinem Level
    // geladen; die Engine greift bevorzugt darauf zu, MeshSwapQuelle in
    // setup.cpp, Zuordnung als MESHSWAP_CROSS_* in objects.h - beide Seiten
    // muessen uebereinstimmen). swapVon kann vom Modellspender abweichen:
    // rich1 (Modell fuer Guard und TwoGun) hat selbst KEINEN MESHSWAP1 -
    // die Guard-Haende kommen aus joby3, TwoGuns Quellmesh 2 aus rich3.
    // Typen mit demselben swapDst teilen sich den Import (einmal emittiert).
    struct CrossSound  { int map; const char* vonLevel; int vonMap; };
    // animZahl (0 = Heuristik): explizite Anim-Scheibenlaenge fuer Typen,
    // deren Steuerung ueber die Objektgrenze hinaus in die globale
    // Anim-Tabelle greift - bisher nur der Imp (siehe Tabelleneintrag).
    //
    // basisSlot (0 = keiner): Anim-Basistyp der Soldatenfamilie. SWAT+ und
    // Mafia tragen in den Dateien nur einen Ein-Frame-Stummel - ihre Anims
    // holt die Engine zur Laufzeit vom Basistyp (setup.cpp:1052/1112/1145:
    // anim_index wird auf SWATs, sonst Guards Satz umgebogen). Wer so einen
    // Typ importiert, MUSS den Basistyp mitbringen, sonst laeuft er ohne
    // gueltigen Satz. Import mit replace=false: hat das Ziel den Basistyp
    // schon (nativ oder als eigener Cross-Import), bleibt der stehen.
    struct CrossGegner { int typeId; const char* von; CrossSound sounds[2];
                         int begleiter[2]; const char* swapVon; int swapDst;
                         int animZahl; int basisSlot; };
    static const CrossGegner kCross[] = {
        { 33, "rich3",   {},                                    {-1,-1},  "rich3",   399, 0, 0 },  // SWAT
        { 35, "rich3",   {},                                    {-1,-1},  "rich3",   399, 0, 33 }, // Advanced SWAT (Anims von SWAT)
        { 37, "rich1",   {},                                    {-1,-1},  "joby3",   400, 0, 0 },  // Guard (Blue Guard)
        { 39, "rich1",   {},                                    {-1,-1},  "rich3",   399, 0, 0 },  // Two Gun
        { 41, "Andrea1", {},                                    {-1,-1},  nullptr,   -1 },  // Dog
        { 45, "Andrea1", { {96,"rich1",85}, {98,"rich1",143} }, {-1,-1},  nullptr,   -1 },  // Larson
        { 47, "Andrea3", { {90,"joby5",45}, {91,"joby5",123} }, {-1,-1},  nullptr,   -1 },  // Pierre
        { 49, "joby3",   {},                                    {-1,-1},  "joby3",   400, 0, 37 }, // Soldier MG (Mafia, Anims von Guard)
        { 51, "joby3",   {},                                    {406,-1}, nullptr,   -1 },  // Soldier Pistols (Mafia2) + MESHSWAP2
        { 57, "Andrea3", {},                                    {-1,-1},  nullptr,   -1 },  // Lion
        { 59, "Andrea3", {},                                    {-1,-1},  "Andrea3", 401 }, // Gladiator
        { 61, "Andrea3", {},                                    {-1,-1},  "Andrea3", 401 }, // Soldier Statue (Hammer)
        { 77, "Andy2",   {},                                    {-1,-1},  nullptr,   -1 },  // Maze Monster
        // Imp: Spender andy3, NICHT andy1. andy1s Imp-Animsatz ist entkernt:
        // die Todesanimation +45 (ImpControl setzt sie bei hit_points<=0 fest
        // ein) ist dort ein Ein-Frame-Dummy ohne KillItem-Kommando - der Imp
        // kann nie sterben (Zustands-Flackern 9<->0, kein Fade, Muell-Pose;
        // im Spiel gesehen). Core ist das nie aufgefallen, weil die junge
        // Lara in andy1 keine Waffen hat. andy3 traegt den vollen Satz
        // (+45 = Frames 45..66 mit KillItem -> after_death -> Fade); Anims
        // 0-12, Geometrie und MESHSWAP1-Tauschsatz sind in beiden identisch
        // (AnimDump/SucheModell, 2026-08-15). animZahl 46: die Objekttabelle
        // gesteht dem Imp nur 18 Anims zu, +19..+45 liegen physisch in den
        // Bereichen fremder Kulissenslots - ohne explizite Laenge fehlte die
        // Todesanim im Import und die Engine las hinter dem Tabellenende
        // (die beobachtete Todes-Flugschleife).
        { 89, "andy3",   {},                                    {-1,-1},  "andy3",   402, 46 }, // Imp
    };

    // Verbotene Typen (siehe RandoInput::verboteneTypen) fliegen aus dem
    // Pool, BEVOR irgendetwas waehlt - weder Tausch noch Zusatzgegner noch
    // Cross duerfen sie anfassen.
    std::vector<int> gegnerPool;
    for (int t : in.enemyTypes)
        if (!in.verboteneTypen.count(t)) gegnerPool.push_back(t);
    std::map<int, const CrossGegner*> crossGewaehlt;
    std::set<int> crossBenutzt;
    if ((opt.crossEnemies || opt.allsanity) && (opt.swapEnemies || opt.addEnemies) && klein != "joby4")
    {
        std::set<int> eigene(in.enemyTypes.begin(), in.enemyTypes.end());
        std::vector<const CrossGegner*> kandidaten;
        for (const CrossGegner& c : kCross)
        {
            if (eigene.count(c.typeId)) continue;         // steht schon im Levelpool
            if (in.verboteneTypen.count(c.typeId)) continue;   // Level vertraegt ihn nicht
            std::string vonKlein;
            for (const char* p = c.von; *p; ++p)
                vonKlein.push_back((*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p);
            if (vonKlein == klein) continue;              // sich selbst spendet niemand
            kandidaten.push_back(&c);
        }

        // Allsanity nimmt den ganzen Pool, Cross-Mix hoechstens drei.
        const int maxFremd = opt.allsanity ? static_cast<int>(kandidaten.size()) : 3;
        for (int n = 0; n < maxFremd && !kandidaten.empty(); ++n)
        {
            const size_t pick = static_cast<size_t>(rng.below(kandidaten.size()));
            const CrossGegner* c = kandidaten[pick];
            kandidaten.erase(kandidaten.begin() + static_cast<ptrdiff_t>(pick));
            gegnerPool.push_back(c->typeId);
            crossGewaehlt[c->typeId] = c;
        }
        std::sort(gegnerPool.begin(), gegnerPool.end());
        if (opt.allsanity)
            report.notes.push_back(fmt("Allsanity: Gegnerpool auf %d Typen erweitert.",
                                       static_cast<int>(gegnerPool.size())));
    }

    // --- Vorhandene Gegner tauschen ---
    //
    // Nur die ART wechselt, der Item-Index bleibt. Das ist der ganze Trick:
    // Trigger sprechen ihre Gegner ueber "Object N" an, also ueber den Index.
    // Bleibt der, wacht jeder Gegner weiter genau dort und genau dann auf, wo
    // und wann Core es vorgesehen hat - der Fortschritt im Level kann sich
    // nicht verschieben. Ein Entfernen oder Hinzufuegen an dieser Stelle waere
    // etwas voellig anderes.
    //
    // Getauscht wird nur unter den kuratierten Typen des Levels. Damit sind
    // Chef, Scientist, Guardian und die gescripteten Faelle von selbst aussen
    // vor - sie stehen dort nicht drin. Mit Cross-Gegnermix gehen einzelne
    // Rollen zusaetzlich an die gewaehlten fremden Typen.
    if (opt.swapEnemies && (in.enemyTypes.size() > 1 || !crossGewaehlt.empty()))
    {
        std::set<int> erlaubt(in.enemyTypes.begin(), in.enemyTypes.end());
        for (int t : in.verboteneTypen) erlaubt.erase(t);

        std::vector<int> indizes;                 // Item-Indizes der Tauschbaren
        std::vector<int> arten;                   // ihre Typen, gleiche Reihenfolge
        for (const LevelItem& it : in.items)
        {
            if (!erlaubt.count(it.typeId)) continue;
            // Handkuration schlaegt alles: was dort steht, bleibt wie es ist.
            if (std::find(in.fixedItems.begin(), in.fixedItems.end(), it.index) != in.fixedItems.end())
                continue;
            // Antitrigger-Ziele duerfen mittauschen: der getauschte Gegner
            // bekommt unten die Antitrigger-Immunitaet mit (siehe dort).
            // OCB != 0 heisst: typspezifischer Spezial-Spawn. Die OCB steuert
            // Initialise/Control des URSPRUNGSTYPS (Imp 1/2/3/11/12:
            // Kletter-/Wurfzustaende auf Wandvorspruengen, imp.cpp:82;
            // Pierre: Positionsversatz + AMBUSH, pierre.cpp:30; u.a.) - ein
            // fremder Typ versteht sie nicht und steht dann in der Wand
            // (im Spiel gesehen: andy3 Items 2/3, Klippen-Imps OCB 3 wurden
            // SWAT/Guard - Grafikloch, Zielerfassung flackert). Solche
            // Items behalten ihren Typ.
            if (it.ocb != 0) continue;
            indizes.push_back(it.index);
            arten.push_back(it.typeId);
        }

        if (indizes.size() > 1 || (!indizes.empty() && !crossGewaehlt.empty()))
        {
            // Fisher-Yates ueber die Typen. Der RNG ist derselbe wie ueberall,
            // der Seed bestimmt das Ergebnis also vollstaendig.
            for (size_t i = arten.size() - 1; i > 0; --i)
            {
                const size_t j = static_cast<size_t>(rng.below(i + 1));
                std::swap(arten[i], arten[j]);
            }

            // Allsanity: jede Rolle wuerfelt frei aus dem VOLLEN Pool -
            // maximale Auswahl, das Level darf sein Gesicht verlieren
            // (Nutzerwunsch). Cross-Mix dagegen bleibt zurueckhaltend:
            // etwa jede vierte Rolle geht an einen der bis zu drei
            // gewaehlten fremden Typen, das Level wirkt durchmischt,
            // nicht ausgetauscht.
            if (opt.allsanity && !gegnerPool.empty())
            {
                for (size_t i = 0; i < arten.size(); ++i)
                    arten[i] = gegnerPool[static_cast<size_t>(rng.below(gegnerPool.size()))];
            }
            else if (!crossGewaehlt.empty())
            {
                for (size_t i = 0; i < arten.size(); ++i)
                {
                    if (rng.below(4) != 0) continue;
                    std::map<int, const CrossGegner*>::iterator w = crossGewaehlt.begin();
                    std::advance(w, static_cast<size_t>(rng.below(crossGewaehlt.size())));
                    arten[i] = w->first;
                }
            }

            for (size_t i = 0; i < indizes.size(); ++i)
            {
                std::map<int, LevelItem>::iterator f = finalOf.find(indizes[i]);
                if (f == finalOf.end() || f->second.typeId == arten[i]) continue;

                JsonValue e = JsonValue::makeObject();
                e.set("index", JsonValue::makeInt(indizes[i]));
                e.set("typeId", JsonValue::makeInt(arten[i]));

                // Zeigt ein Antitrigger auf dieses Item, wuerde er den neuen
                // Gegner beim Betreten seines Sektors ohne Sterben und ohne
                // Ausblenden aus dem Level loeschen (control.cpp:2422). In
                // Vanilla ist das harmlos, weil der Gegner dort zu diesem
                // Zeitpunkt erledigt sein soll - der getauschte kaempft aber
                // vielleicht noch. Er bekommt deshalb Cores eigene
                // Antitrigger-Immunitaet mit; Details in ApplyPatch.
                if (in.antitriggered.count(indizes[i]))
                {
                    e.set("antitriggerImmun", JsonValue::makeBool(true));
                    report.notes.push_back(fmt("Item %d ist Antitrigger-Ziel - Immunitaet gesetzt.",
                                               indizes[i]));
                }

                edits.push(e);

                report.notes.push_back(fmt("Gegner Item %d: typeId %d -> %d",
                                           indizes[i], f->second.typeId, arten[i]));
                f->second.typeId = arten[i];
                if (crossGewaehlt.count(arten[i])) crossBenutzt.insert(arten[i]);
                ++report.enemiesSwapped;
            }
        }
        else
            report.notes.push_back(fmt("Gegnertausch: nur %d tauschbare Gegner im Level, nichts zu mischen.",
                                       static_cast<int>(indizes.size())));
    }

    // --- Zusaetzliche Gegner ---
    //
    // Zwei Dinge muessen zusammenpassen, und beide sind Nutzervorgaben aus
    // Beobachtungen im Spiel:
    //
    //  1. Der Trigger darf NIE auf dem Gegnersektor liegen. Sonst spawnt der
    //     Gegner in Lara hinein.
    //  2. Er muss trotzdem sicher ueberquert werden, sonst wacht der Gegner nie
    //     auf.
    //
    // Beides erfuellt der Anmarschsektor: er liegt etwa drei Schritte vor dem
    // Gegner auf Laras Erstzugangsweg. Ausgerechnet wird er im Zonen-Export, wo
    // der Erstzugangsbaum liegt - hier kommt nur noch die fertige Antwort an.
    //
    // Der Trigger bekommt OneShot. Das ist keine Formalie: _TestTriggers
    // (control.cpp:2415) gibt das Bit als IFL_INVISIBLE ans Item weiter, und
    // Larson ist ohne es gar nicht toetbar (pierre.cpp:68).
    JsonValue trigAdds = JsonValue::makeArray();
    if (opt.addEnemies && opt.enemiesPerLevel > 0 && !gegnerPool.empty())
    {
        // Die Engine haelt items[] als festes Feld von 512 Eintraegen
        // (MAX_ITEMS, global/types.h - seit der Verdopplung; Tr5Level prueft
        // es beim Anhaengen). Gedeckelt statt gehofft - ein abgewiesener
        // Patch mitten im Lauf waere schlimmer als ein Gegner weniger.
        const int platzFrei = 512 - static_cast<int>(in.items.size()) - static_cast<int>(adds.size());

        // Wohin darf ein Gegner? Auf einen freien Sektor, dessen Anmarsch
        // ebenfalls frei ist und einen Trigger vertraegt.
        //
        // ALLE tauglichen Anmarschsektoren mitfuehren, nicht nur den besten:
        // zwei verschiedene Gegnerfelder koennen denselben Anmarschsektor
        // haben. Wer sich hier auf einen festlegt, merkt beim zweiten Gegner
        // nicht mehr, dass dessen Trigger schon vergeben ist - und setzt zwei
        // Trigger auf ein Feld. Beim Patchen fliegt das dann auf, und zwar
        // nicht als "ein Gegner weniger", sondern als verlorenes Level.
        struct Platz { const ZoneSector* s; std::vector<long long> trigger; };
        std::vector<Platz> plaetze;
        for (const ZoneSector& s : pool)
        {
            const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
            if (used.count(sk)) continue;
            if (!origin.count(s.room)) continue;
            // Nutzervorgabe: kein Gegner in einen Raum mit Wasser-Flag.
            if (in.waterRooms.count(s.room)) continue;

            Platz p;
            p.s = &s;
            for (long long a : s.approach)
            {
                if (a == sk || used.count(a)) continue;
                if (!in.triggerSafeSectors.count(a)) continue;
                p.trigger.push_back(a);
            }
            if (!p.trigger.empty()) plaetze.push_back(p);
        }

        int wieViele = opt.enemiesPerLevel;
        if (wieViele > platzFrei) wieViele = platzFrei;
        if (wieViele > static_cast<int>(plaetze.size())) wieViele = static_cast<int>(plaetze.size());

        const int wollte = opt.enemiesPerLevel;
        const int plaetzeVorher = static_cast<int>(plaetze.size());

        int gesetzt = 0;
        while (gesetzt < wieViele && !plaetze.empty())
        {
            const size_t pick = static_cast<size_t>(rng.below(plaetze.size()));
            const Platz p = plaetze[pick];
            plaetze.erase(plaetze.begin() + static_cast<ptrdiff_t>(pick));

            // Erst hier entscheidet sich der Trigger: bis zur Auswahl koennen
            // andere Gegner Sektoren belegt haben.
            long long trigger = -1;
            for (long long a : p.trigger)
                if (!used.count(a)) { trigger = a; break; }
            if (trigger < 0) continue;          // alle Anmarschfelder vergeben
            ++gesetzt;

            const int typeId = gegnerPool[static_cast<size_t>(rng.below(gegnerPool.size()))];
            if (crossGewaehlt.count(typeId)) crossBenutzt.insert(typeId);
            const RoomOrigin& ro = origin[p.s->room];

            // Der Index, den das neue Item bekommt: itemAdds haengt hinten an,
            // in genau dieser Reihenfolge.
            const int neuerIndex = static_cast<int>(in.items.size()) + static_cast<int>(adds.size());

            JsonValue a = JsonValue::makeObject();
            a.set("typeId", JsonValue::makeInt(typeId));
            a.set("room", JsonValue::makeInt(p.s->room));
            a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + p.s->sectorX * 1024 + 512));
            a.set("y", JsonValue::makeInt(p.s->floorY));
            a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + p.s->sectorZ * 1024 + 512));
            // OCB 0: bei Larson setzt eine OCB != 0 ai_bits = GUARD, und dann
            // sucht er AI-Objekte, die an einer neuen Stelle nicht stehen
            // (pierre.cpp:84). Fuer neu gesetzte Gegner ist 0 die einzige
            // Einstellung, die ueberall dasselbe bedeutet.
            a.set("ocb", JsonValue::makeInt(0));
            a.set("angle", JsonValue::makeInt(static_cast<int64_t>(rng.below(4)) * 16384 - 32768));
            adds.push(a);

            const int tr = static_cast<int>(trigger >> 32);
            const int tx = static_cast<int>((trigger >> 16) & 0xFFFF);
            const int tz = static_cast<int>(trigger & 0xFFFF);

            JsonValue cmd = JsonValue::makeObject();
            cmd.set("type", JsonValue::makeInt(0));          // TO_OBJECT
            cmd.set("param", JsonValue::makeInt(neuerIndex));
            JsonValue cmdList = JsonValue::makeArray();
            cmdList.push(cmd);

            JsonValue t = JsonValue::makeObject();
            t.set("room", JsonValue::makeInt(tr));
            t.set("sectorX", JsonValue::makeInt(tx));
            t.set("sectorZ", JsonValue::makeInt(tz));
            t.set("commands", cmdList);
            trigAdds.push(t);

            used.insert(secKey(p.s->room, p.s->sectorX, p.s->sectorZ));
            used.insert(trigger);
            ++report.enemiesAdded;
            report.notes.push_back(fmt("Gegner typeId %d als Item %d auf %d,%d,%d - Trigger auf %d,%d,%d",
                                       typeId, neuerIndex, p.s->room, p.s->sectorX, p.s->sectorZ, tr, tx, tz));
        }

        // Der Lauf setzt so viele, wie das Level hergibt - fehlende Plaetze
        // sind kein Fehler, sondern eine Eigenschaft des Levels. Gemeldet wird
        // es trotzdem, sonst wundert sich jemand ueber die Zahl.
        if (report.enemiesAdded < wollte)
            report.notes.push_back(fmt("Nur %d von %d zusaetzlichen Gegnern gesetzt "
                                       "(%d Plaetze zur Auswahl, %d Item-Slots frei).",
                                       report.enemiesAdded, wollte, plaetzeVorher, platzFrei));
    }

    // --- Was traegt am Ende wer? ---
    //
    // GetCarriedItems (game/setup.cpp:1925) laeuft beim Levelstart ueber alle
    // Items und gibt jedem Traeger die Pickups, die dicht genug bei ihm liegen.
    // Das ist gewollt und vom Nutzer ausdruecklich in Ordnung befunden: ein
    // Schluessel beim Gegner ist eine gute Fundstelle.
    //
    // Eine Bedingung gibt es aber. DropBaddyPickups (game/box.cpp:833) legt das
    // Item beim Tod auf die Sektormitte des Gegners, und PickUpCollision
    // (game/pickup.cpp:734) faechert ueber die OCB in Sonderfaelle auf, die Lara
    // per TestLaraPosition an einer Kulisse ausrichten - an einer Kulisse, die
    // am Fallort nicht steht. Ein getragenes Pickup mit OCB != 0 liegt danach
    // fuer immer unaufnehmbar herum, ohne Absturz und ohne Meldung.
    //
    // Alles, was dieser Lauf bewegt, bekommt nach Regel 3 ohnehin OCB 0, und in
    // Vanilla haben alle 56 getragenen Items ebenfalls OCB 0. Die Regel ist also
    // erfuellt - nachgerechnet wird sie trotzdem, denn beides ist eine Zusage
    // von aussen und keine Eigenschaft dieses Codes. Sobald Gegner selbst
    // bewegt werden, kann ein Traeger auf einem ortsfesten Pickup landen, und
    // dann faellt es hier auf statt im Spiel.
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
                // Kein Abbruch. Bewegte Items bekommen immer OCB 0, ein Wert
                // ungleich null kann also nur bedeuten, dass Item UND Traeger
                // beide unveraendert dort liegen, wo Vanilla sie hingestellt
                // hat. Daran ist dieser Lauf unschuldig, und einen Lauf an einer
                // Vanilla-Konstellation scheitern zu lassen hat sich in diesem
                // Projekt schon zweimal als falsch erwiesen.
                //
                // Sobald Gegner selbst verschoben werden, aendert sich das: dann
                // kann ein Traeger auf einem ortsfesten Pickup landen, und das
                // waere sehr wohl unser Werk. Hier ist die Stelle, an der es
                // dann zum Fehler werden muss.
                report.notes.push_back(fmt("Item %d (typeId %d) liegt unveraendert bei Traeger %d und hat OCB %d - "
                                           "abgelegt waere es nicht aufnehmbar (Vanilla-Zustand, nicht veraendert).",
                                           p.index, p.typeId, b.index, p.ocb & 0x3F));
                continue;
            }
            report.notes.push_back(fmt("Item %d (typeId %d) liegt bei Item %d (typeId %d) und wird getragen.",
                                       p.index, p.typeId, b.index, b.typeId));
        }
    }

    // --- Gekoppelte Trigger ---
    //
    // Erst hier, weil erst jetzt feststeht, wo die Items gelandet sind. Ist ein
    // gekoppeltes Item liegen geblieben, gibt es auch nichts zu verschieben -
    // der Trigger liegt dann noch auf seinem Vanilla-Sektor und ist richtig.
    JsonValue trigMoves = JsonValue::makeArray();
    for (const auto& kv : coupled)
    {
        const CoupledTrigger& c = *kv.second;
        std::map<int, LevelItem>::const_iterator f = finalOf.find(c.itemIndex);
        if (f == finalOf.end()) continue;
        const LevelItem& fin = f->second;

        if (fin.room == c.room && fin.sectorX == c.sectorX && fin.sectorZ == c.sectorZ)
        {
            report.notes.push_back(fmt("Item %d blieb auf seinem Sektor - Trigger bleibt ebenfalls.", c.itemIndex));
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
        report.notes.push_back(fmt("Item %d wandert nach %d,%d,%d - sein Pickup-Trigger zieht von %d,%d,%d mit.",
                                   c.itemIndex, fin.room, fin.sectorX, fin.sectorZ,
                                   c.room, c.sectorX, c.sectorZ));
    }

    // --- Fehlende Sounds anfordern ---
    //
    // Nur fuer Typen, die nach diesem Lauf tatsaechlich im Level stehen. Ein
    // Sample fuer einen Gegner zu holen, den es hier gar nicht gibt, waere
    // sichtbarer Ballast in der Datei.
    JsonValue soundAdds = JsonValue::makeArray();
    if (!in.enemySounds.empty())
    {
        std::set<int> vorhanden;
        for (const auto& kv : finalOf) vorhanden.insert(kv.second.typeId);
        for (size_t i = 0; i < adds.size(); ++i)
            vorhanden.insert(static_cast<int>(adds[i]["typeId"].asInt()));

        for (const EnemySound& s : in.enemySounds)
        {
            if (!vorhanden.count(s.typeId)) continue;
            JsonValue a = JsonValue::makeObject();
            a.set("map", JsonValue::makeInt(s.map));
            a.set("fromLevel", JsonValue::makeString(s.fromLevel));
            a.set("fromMap", JsonValue::makeInt(s.fromMap));
            soundAdds.push(a);
            report.notes.push_back(fmt("Sound fuer typeId %d: Map %d aus %s Map %d",
                                       s.typeId, s.map, s.fromLevel.c_str(), s.fromMap));
        }
    }

    // --- Cross-Gegner: Modelle und Sonder-Sounds anfordern ---
    //
    // Nur fuer die Typen, die der Lauf wirklich eingesetzt hat. Der Patchweg
    // holt das Modell samt der Samples seiner Animationskommandos vom
    // Spender (replace, siehe Pool-Kommentar oben); die Eintraege hier
    // decken zusaetzlich die bekannten Luecken ab, die selbst der Spender
    // hat. Doppelte Sound-Anforderungen sind unschaedlich - der Patchweg
    // ueberspringt Vorhandenes.
    JsonValue moveables = JsonValue::makeArray();
    std::set<int> swapEmittiert;
    for (int t : crossBenutzt)
    {
        const CrossGegner* c = crossGewaehlt[t];
        JsonValue e = JsonValue::makeObject();
        e.set("slot", JsonValue::makeInt(t));
        e.set("fromLevel", JsonValue::makeString(c->von));
        e.set("replace", JsonValue::makeBool(true));
        if (c->animZahl > 0) e.set("animZahl", JsonValue::makeInt(c->animZahl));
        moveables.push(e);

        // Pflicht-Gefolge des Typs (siehe Tabellenkommentar).
        for (int b : c->begleiter)
        {
            if (b < 0) continue;
            JsonValue g = JsonValue::makeObject();
            g.set("slot", JsonValue::makeInt(b));
            g.set("fromLevel", JsonValue::makeString(c->von));
            g.set("replace", JsonValue::makeBool(true));
            moveables.push(g);
        }

        // Anim-Basistyp der Familie (siehe Tabellenkommentar): SWAT+/Mafia
        // laufen zur Laufzeit auf dem Satz von SWAT bzw. Guard. replace=false
        // laesst einen nativen oder selbst importierten Basistyp stehen.
        if (c->basisSlot > 0)
        {
            JsonValue g = JsonValue::makeObject();
            g.set("slot", JsonValue::makeInt(c->basisSlot));
            g.set("fromLevel", JsonValue::makeString(c->von));
            g.set("replace", JsonValue::makeBool(false));
            moveables.push(g);
        }

        // Tauschhaelften: die MESHSWAP1 des Tauschspenders (swapVon, kann
        // vom Modellspender abweichen) in den Ausweichslot des Typs. Typen
        // mit demselben swapDst teilen sich den Import - einmal reicht.
        if (c->swapDst >= 0 && c->swapVon && !swapEmittiert.count(c->swapDst))
        {
            swapEmittiert.insert(c->swapDst);
            JsonValue g = JsonValue::makeObject();
            g.set("slot", JsonValue::makeInt(405));         // MESHSWAP1 des Tauschspenders
            g.set("toSlot", JsonValue::makeInt(c->swapDst));
            g.set("fromLevel", JsonValue::makeString(c->swapVon));
            g.set("replace", JsonValue::makeBool(true));
            // MESHSWAP1 traegt einen anim_index ohne eigene Animationen
            // (Core-Altlast) - nur die Meshes zaehlen.
            g.set("onlyMeshes", JsonValue::makeBool(true));
            moveables.push(g);
        }

        for (int k = 0; k < 2; ++k)
        {
            if (c->sounds[k].map == 0) continue;
            JsonValue a = JsonValue::makeObject();
            a.set("map", JsonValue::makeInt(c->sounds[k].map));
            a.set("fromLevel", JsonValue::makeString(c->sounds[k].vonLevel));
            a.set("fromMap", JsonValue::makeInt(c->sounds[k].vonMap));
            soundAdds.push(a);
        }

        ++report.crossTypesUsed;
        report.notes.push_back(fmt("Cross-Gegner: typeId %d kommt aus %s.", t, c->von));
    }

    // --- andy1: eigenen Imp heilen ---
    //
    // andy1s Imp-Slot traegt den entkernten Animsatz (Todesanim +45 ist ein
    // Dummy ohne KillItem - siehe kCross-Kommentar beim Imp). Vanilla fiel
    // das nie auf, weil die junge Lara dort keine Waffen hat. Der Randomizer
    // nimmt das Jung-Flag raus und gibt Pistolen - ab da stirbt der Imp auch
    // in andy1 und braucht den vollen Satz aus andy3 (Geometrie identisch,
    // nur die Animationen sind dort vollstaendig). Unbedingt, nicht an
    // Optionen gebunden: jeder Lauf macht andy1s Imps toetbar.
    if (klein == "andy1")
    {
        JsonValue e = JsonValue::makeObject();
        e.set("slot", JsonValue::makeInt(89));
        e.set("fromLevel", JsonValue::makeString("andy3"));
        e.set("replace", JsonValue::makeBool(true));
        e.set("animZahl", JsonValue::makeInt(46));   // wie im kCross-Eintrag
        moveables.push(e);
        report.notes.push_back("Imp geheilt: voller Animsatz (mit Tod) aus andy3.");
    }

    // --- Fluchtmarker fuer Larson/Pierre (45/47) ---
    //
    // Beide fliehen ab 40 LP zu einem AI_AMBUSH-Marker mit passender OCB in
    // ihrer Zone und despawnen dort (GetAITarget/FindAITargetObject,
    // box.cpp) - Core garantiert den Marker in jedem Vanilla-Level, das ihn
    // braucht.
    //
    // Der Marker darf NIE auf dem Spawn-Sektor des Gegners liegen:
    // GetAIPickups (setup.cpp:2024) macht beim Levelstart jedes AI-Objekt
    // im Umkreis von 512 Units im selben Raum zur Start-Verhaltensanweisung
    // des Gegners (ai_bits |= AMBUSH - er rennt dann nur noch und zielt
    // nie) und VERBRAUCHT den Marker danach (room_number = NO_ROOM), die
    // Fluchtsuche findet also auch nichts mehr. Im Spiel gesehen: joby2
    // Item 182 lief Lara stumm hinterher, DIAG zeigte ai_bits 2 ab Spawn.
    //
    // Deshalb EIN Marker je Level auf Laras Startposition: die Flucht
    // fuehrt sichtbar zurueck Richtung Leveleingang und endet ausser Sicht,
    // wie Core es in den Vanilla-Leveln inszeniert. Exemplare, deren Zone
    // den Start nicht erreicht, fliehen einfach nicht und sterben normal
    // (PierreKannFliehen laesst die 40-LP-Klammer dann aus). Steht ein
    // Gegner nah an Laras Start, wuerde ER den Marker fressen - dann lieber
    // gar keiner. Vanilla-Exemplare mit OCB != 0 bleiben aussen vor - fuer
    // die stehen Cores eigene Marker im Level.
    JsonValue aiAdds = JsonValue::makeArray();
    {
        bool braucht = false;
        for (const auto& kv : finalOf)
            if ((kv.second.typeId == 45 || kv.second.typeId == 47) && kv.second.ocb == 0)
                braucht = true;
        for (size_t i = 0; i < adds.size(); ++i)
        {
            const int t = static_cast<int>(adds[i]["typeId"].asInt());
            if (t == 45 || t == 47) braucht = true;
        }

        const LevelItem* lara = nullptr;
        for (const LevelItem& it : in.items)
            if (it.typeId == 0) { lara = &it; break; }

        if (braucht && lara)
        {
            // Gegnertypen, die den Marker fressen koennten (GetAIPickups
            // greift bei jedem intelligenten Item): kuratierte Levelgegner,
            // alle Pool-Fremdtypen und die beiden Fluechter selbst.
            std::set<int> intelligente(in.enemyTypes.begin(), in.enemyTypes.end());
            for (const CrossGegner& c : kCross) intelligente.insert(c.typeId);

            auto zuNah = [&](int room, int x, int z)
            { return room == lara->room && std::abs(x - lara->x) < 768 && std::abs(z - lara->z) < 768; };

            bool frei = true;
            for (const auto& kv : finalOf)
                if (intelligente.count(kv.second.typeId) && zuNah(kv.second.room, kv.second.x, kv.second.z))
                    frei = false;
            for (size_t i = 0; i < adds.size(); ++i)
                if (intelligente.count(static_cast<int>(adds[i]["typeId"].asInt())) &&
                    zuNah(static_cast<int>(adds[i]["room"].asInt()),
                          static_cast<int>(adds[i]["x"].asInt()),
                          static_cast<int>(adds[i]["z"].asInt())))
                    frei = false;

            if (frei)
            {
                JsonValue m = JsonValue::makeObject();
                m.set("slot", JsonValue::makeInt(379));   // AI_AMBUSH (objects.h:270)
                m.set("room", JsonValue::makeInt(lara->room));
                m.set("x", JsonValue::makeInt(lara->x));
                m.set("y", JsonValue::makeInt(lara->y));
                m.set("z", JsonValue::makeInt(lara->z));
                m.set("ocb", JsonValue::makeInt(0));
                aiAdds.push(m);
                report.notes.push_back("Fluchtmarker: AI_AMBUSH fuer Larson/Pierre an Laras Start gesetzt.");
            }
            else
                report.notes.push_back("Fluchtmarker entfaellt: Gegner zu nah an Laras Start "
                                       "(GetAIPickups wuerde ihn fressen) - Larson/Pierre sterben normal.");
        }
        else if (braucht)
            report.notes.push_back("Fluchtmarker entfaellt: Lara-Startitem nicht im Export - "
                                   "Larson/Pierre sterben normal.");
    }

    // --- Outfit wuerfeln ---
    //
    // Die Wahl faellt hier, damit sie am Seed haengt wie alles andere; die
    // eigentliche Uebertragung macht der Patchweg, der die Spender-Level
    // kennt. Faellt die Wahl auf das Outfit, das das Level ohnehin traegt,
    // steht nichts im Patch - ein Kopieren auf sich selbst waere nur Ballast.
    JsonValue outfit = JsonValue::makeObject();
    bool outfitGesetzt = false;
    if (opt.randomOutfits)
    {

        // Eigene Familie: andrea VOR andy pruefen, sonst faengt "andy" nie.
        int eigene = -1;
        if (klein.rfind("andrea", 0) == 0)     eigene = 0;
        else if (klein.rfind("andy", 0) == 0)  eigene = -1;   // Kind-Lara, hat keins der drei
        else if (klein.rfind("joby", 0) == 0)  eigene = 1;
        else if (klein.rfind("rich", 0) == 0)  eigene = 2;

        static const char* kSpender[3] = { "Andrea1", "joby2", "rich2" };

        if (klein == "joby4")
            report.notes.push_back("Outfit: joby4 behaelt den Taucheranzug (bleibt vanilla).");
        else
        {
            const int wahl = static_cast<int>(rng.below(3));
            if (wahl == eigene)
            {
                report.notes.push_back(fmt("Outfit: %s behaelt sein eigenes.", in.level.c_str()));
                // Auch die eigene Catsuit soll Holster tragen (Nutzerwunsch) -
                // die rich-Level haben die Objekte 13-16 vanilla nicht, und
                // erst mit ihnen zeichnet die Engine dort Holster.
                if (eigene == 2)
                {
                    outfit.set("holstersFrom", JsonValue::makeString("joby2"));
                    outfitGesetzt = true;
                }
            }
            else
            {
                outfit.set("fromLevel", JsonValue::makeString(kSpender[wahl]));

                // LaraType-Byte ausdruecklich mitgeben - die Engine macht die
                // Zopf-Sonderbehandlung (Verankerung in hair.cpp, Schweiss-
                // tabelle in laraskin.cpp) am Outfit fest, nicht am Level:
                //   0 = LARA_NORMAL   (Andrea-Outfit, Standardzopf)
                //   2 = LARA_BUNHEAD  (Joby-Outfit: Muetzenzopf, auch
                //                      ausserhalb der Russland-Level)
                //   3 = LARA_CATSUIT  (rich2-Outfit, eigener Zeichenweg)
                //   5 = LARA_PONYTAIL (Andrea-Outfit IN einem Russland-Level:
                //                      dort loest LARA_NORMAL faelschlich die
                //                      Russland-Zopfbehandlung aus, also ein
                //                      Wert, der wie NORMAL zeichnet, aber
                //                      nicht NORMAL ist)
                static const int kLaraTypeByte[3] = { 0, 2, 3 };
                int typByte = kLaraTypeByte[wahl];
                if (wahl == 0 && klein.rfind("joby", 0) == 0)
                    typByte = 5;
                outfit.set("laraType", JsonValue::makeInt(typByte));

                // Der Catsuit-Spender rich2 bringt keine Holster mit - die
                // fehlenden Slots kommen dann aus joby2 (dunkle Riemen, passt
                // zum schwarzen Anzug).
                if (wahl == 2)
                    outfit.set("holstersFrom", JsonValue::makeString("joby2"));
                outfitGesetzt = true;
                report.notes.push_back(fmt("Outfit: %s bekommt die Lara aus %s.",
                                           in.level.c_str(), kSpender[wahl]));
            }
        }
    }

    // --- Waffen ueberall ---
    //
    // Aufgeloest als fertige Kopierliste, damit die Spenderregeln an EINER
    // Stelle stehen (hier) und der Patchweg nur noch ausfuehrt - dasselbe
    // Muster wie beim LaraType-Byte des Outfits. Die Regeln sind die der
    // Werkzeug-Goldreferenz (Import Export\Waffen Tests\Plan.md, "Alle 13
    // Level mit dem vollen Set"), mit zwei begruendeten Abweichungen:
    //
    // 1. Die Holsterslots 13-16 sind hier ALLE "nur was fehlt" und kommen
    //    einheitlich aus Andrea3 (die Goldreferenz nahm 16 immer aus joby2).
    //    Die Slots liefern die OBERSCHENKEL-Ansicht je Holsterzustand; bei
    //    zufaelligen Outfits bringt der Outfit-Spender seine eigenen mit,
    //    und gemischte Oberschenkel (drei von Andrea, einer von joby) waeren
    //    sichtbar falsch. Preis: die Deagle sieht IM Holster wie der alte
    //    Revolver aus - gezogen (6), mit Laser (12), als Pickup (347) und im
    //    Inventar ist sie ueberall die Deagle.
    // 2. Andrea3 liefert zusaetzlich 352/456/457 (Fernglas-Inventarobjekt
    //    und die Overlays fuer Fernglas und Zielfernrohr) - die fehlen nur
    //    den andy-Leveln, und ohne 457 zeigt der Blick durchs Scope Laras
    //    Pistolenhand (Levelabhaengige Eigenheiten.md, Mesh-Index 0).
    //
    // Der Patchweg ueberspringt "nur was fehlt"-Eintraege, wenn der Slot da
    // ist - dadurch loest sich auch die rich2-Ausnahme (eigene HK-Spezialpose
    // bleibt) von selbst. Eintraege, deren Spender das Level selbst ist,
    // entfallen hier schon.
    JsonValue waffen = JsonValue::makeArray();
    if (opt.allWeapons && klein != "joby4")
    {
        struct WRegel { const char* von; int slot; bool ersetzen; };
        static const WRegel kWaffen[] = {
            // Andrea3: Pistolen/Uzi/Schrotflinte/Holster/Lasersight - nur was fehlt
            {"Andrea3",1,false},{"Andrea3",2,false},{"Andrea3",3,false},
            {"Andrea3",13,false},{"Andrea3",14,false},{"Andrea3",15,false},{"Andrea3",16,false},
            {"Andrea3",334,false},{"Andrea3",335,false},{"Andrea3",336,false},{"Andrea3",337,false},
            {"Andrea3",338,false},{"Andrea3",339,false},{"Andrea3",340,false},{"Andrea3",351,false},
            // Gefolge (fehlt nur den andy-Leveln)
            {"Andrea3",352,false},{"Andrea3",456,false},{"Andrea3",457,false},
            // joby2: Desert Eagle ist Vorgabe - immer ersetzen
            {"joby2",6,true},{"joby2",12,true},{"joby2",347,true},{"joby2",348,true},
            // rich1: HK - "nur was fehlt" laesst rich2 seine Spezialpose
            {"rich1",5,false},{"rich1",345,false},{"rich1",346,false},
        };

        int eintraege = 0;
        for (const WRegel& r : kWaffen)
        {
            std::string vonKlein;
            for (const char* p = r.von; *p; ++p)
                vonKlein.push_back((*p >= 'A' && *p <= 'Z') ? static_cast<char>(*p + 32) : *p);
            if (vonKlein == klein) continue;          // sich selbst spendet niemand

            // rich2 bekommt KEINE Pistolen. Sein Slot 1 (PISTOLS_ANIM) ist
            // von Core mit einer zweiten HK samt gescriptetem Animationssatz
            // belegt (mesh-identisch mit Slot 5, eigene Anims mit KILLITEM
            // und Schusssounds - Sequenzmaterial, richcut2 traegt denselben
            // Slot). Der Slot bleibt unangetastet, und damit darf auch das
            // Pistolen-Item nicht ins Level: setup.cpp:111 gibt Lara
            // Startpistolen, sobald PISTOLS_ITEM geladen ist, und gezogen
            // liefen die Arme ueber die gescripteten HK-Anims.
            if (klein == "rich2" && (r.slot == 334 || r.slot == 335)) continue;

            JsonValue e = JsonValue::makeObject();
            e.set("slot", JsonValue::makeInt(r.slot));
            e.set("fromLevel", JsonValue::makeString(r.von));
            e.set("replace", JsonValue::makeBool(r.ersetzen));
            waffen.push(e);
            ++eintraege;
        }
        if (eintraege > 0)
            report.notes.push_back(fmt("Waffen: %d Slot-Regeln (Andrea3/joby2/rich1) im Patch.", eintraege));
    }
    else if (opt.allWeapons)
        report.notes.push_back("Waffen: joby4 bleibt vanilla (Taucheranzug).");

    patchOut = JsonValue::makeObject();
    patchOut.set("itemEdits", edits);
    if (moveables.size() > 0) patchOut.set("moveableAdds", moveables);
    if (waffen.size() > 0) patchOut.set("weaponAdds", waffen);
    if (aiAdds.size() > 0) patchOut.set("aiAdds", aiAdds);
    if (outfitGesetzt) patchOut.set("outfit", outfit);
    if (soundAdds.size() > 0) patchOut.set("soundAdds", soundAdds);
    if (adds.size() > 0) patchOut.set("itemAdds", adds);
    if (trigMoves.size() > 0) patchOut.set("triggerMoves", trigMoves);
    if (trigAdds.size() > 0) patchOut.set("triggerAdds", trigAdds);
    // Bewusst KEIN itemRemoves - siehe Regel 1 im Header.
    return true;
}

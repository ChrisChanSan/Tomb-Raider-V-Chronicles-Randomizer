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

    // Item-Index -> Sektor, um zu pruefen, ob ein per Object-Kommando genanntes
    // Item wirklich auf dem Triggersektor liegt. Nur dann ist es das Item, das
    // der Pickup-Trigger meint - die uebrigen Object-Kommandos sind seine
    // Wirkung (Tueren, Gegner, Flipmaps).
    std::map<int, const JsonValue*> byIndex;
    const JsonValue& items = levelInfo["items"];
    for (size_t i = 0; i < items.size(); ++i)
        byIndex[static_cast<int>(items[i]["index"].asInt())] = &items[i];

    const JsonValue& trigs = levelInfo["triggers"];
    for (size_t t = 0; t < trigs.size(); ++t)
    {
        const JsonValue& tr = trigs[t];
        if (tr["triggerType"].asString() != "Pickup") continue;

        const int trRoom = static_cast<int>(tr["room"].asInt());
        const int trSx = static_cast<int>(tr["sectorX"].asInt());
        const int trSz = static_cast<int>(tr["sectorZ"].asInt());

        std::vector<int> onSector;
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
                    const JsonValue& item = *it->second;
                    if (static_cast<int>(item["room"].asInt()) == trRoom &&
                        static_cast<int>(item["sectorX"].asInt()) == trSx &&
                        static_cast<int>(item["sectorZ"].asInt()) == trSz)
                    {
                        // Das ist das aufzunehmende Item selbst, keine Wirkung.
                        onSector.push_back(idx);
                        continue;
                    }
                    // Schwarmgetier zaehlt wie Musik: nicht festhaltend.
                    harmless = isDespawningSwarm(static_cast<int>(item["typeId"].asInt()));
                }
            }

            effects.push_back(type);
            if (type != "PlaySoundtrack" && !harmless) onlySoundtrack = false;
        }

        if (onSector.empty()) continue;

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
    return true;
}

bool Rando::run(const RandoInput& in,
                const RandoOptions& opt,
                uint64_t seed,
                JsonValue& patchOut,
                RandoReport& report)
{
    report = RandoReport();
    Rng rng(seed);

    std::map<int, RoomOrigin> origin;
    for (const RoomOrigin& r : in.roomOrigins) origin[r.room] = r;

    std::map<int, const KeyConstraint*> conByType;
    for (const KeyConstraint& k : in.constraints) conByType[k.typeId] = &k;

    std::set<int> pinned(in.triggerPinned);
    for (int i : in.fixedItems) pinned.insert(i);

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
        // Das Waffenverbot haengt am Level, nicht am Typ-Constraint: eine
        // Schrotflinte ist kein Schluessel und wuerde sonst gar nicht geprueft.
        const bool isWeapon = in.weaponTypes.count(it.typeId) != 0;

        for (const ZoneSector& s : pool)
        {
            const long long sk = secKey(s.room, s.sectorX, s.sectorZ);
            if (used.count(sk)) continue;
            if (isWeapon && in.noWeaponRooms.count(s.room)) continue;
            if (k)
            {
                if (s.zone > k->maxZone) continue;
                if (!k->allowRooms.empty() &&
                    !std::binary_search(k->allowRooms.begin(), k->allowRooms.end(), s.room)) continue;
                if (std::binary_search(k->excludeRooms.begin(), k->excludeRooms.end(), s.room)) continue;
                if (!k->islandSectors.empty() && !k->islandSectors.count(sk)) continue;
                if (k->bannedSectors.count(sk)) continue;
                if (k->forbiddenSectors.count(sk)) continue;
                // Kleineres y liegt hoeher: ein Sektor unterhalb der Schranke ist
                // zu hoch. Sie gilt nur in ihrem Geltungsbereich, und ohne
                // Geltungsbereich ueberall.
                if (k->hasMinFloorY && s.floorY < k->minFloorY &&
                    (k->minFloorYScope.empty() || k->minFloorYScope.count(sk))) continue;
                if (mustBeOnIsland && !k->requireInIsland.count(sk)) continue;
            }
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

    patchOut = JsonValue::makeObject();
    patchOut.set("itemEdits", edits);
    if (adds.size() > 0) patchOut.set("itemAdds", adds);
    // Bewusst KEIN itemRemoves - siehe Regel 1 im Header.
    return true;
}

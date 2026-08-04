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

    // Schluessel eines Sektors, um Doppelbelegung zu verhindern.
    inline long long secKey(int room, int sx, int sz)
    {
        return (static_cast<long long>(room) << 32) | (static_cast<long long>(sx) << 16) | static_cast<long long>(sz);
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
                }
            }
            effects.push_back(type);
            if (type != "PlaySoundtrack") onlySoundtrack = false;
        }

        if (onSector.empty()) continue;

        // Ein Trigger, dessen einzige weitere Wirkung Musik ist, haelt das Item
        // nicht fest - bewusst akzeptiertes Opfer, damit z.B. Andrea2s
        // Brechstange randomisierbar bleibt.
        if (effects.empty() || onlySoundtrack)
        {
            if (explain)
                for (int i : onSector)
                    explain->push_back(fmt("Item %d bleibt frei - Pickup-Trigger loest nur Musik aus", i));
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
        out.constraints.push_back(k);
    }
    std::sort(out.constraints.begin(), out.constraints.end(),
              [](const KeyConstraint& a, const KeyConstraint& b) { return a.typeId < b.typeId; });

    const JsonValue& fx = zoneExport["fixedItems"];
    for (size_t i = 0; i < fx.size(); ++i) out.fixedItems.push_back(static_cast<int>(fx[i]["item"].asInt()));
    std::sort(out.fixedItems.begin(), out.fixedItems.end());

    const JsonValue& inj = zoneExport["requiredInjections"];
    for (size_t i = 0; i < inj.size(); ++i)
    {
        Injection j;
        j.typeId = static_cast<int>(inj[i]["typeId"].asInt());
        j.room = static_cast<int>(inj[i]["room"].asInt());
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

    JsonValue edits = JsonValue::makeArray();
    JsonValue adds = JsonValue::makeArray();

    auto placeAt = [&](const LevelItem& it, const ZoneSector& s)
    {
        const RoomOrigin& ro = origin[s.room];
        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(it.index));
        e.set("room", JsonValue::makeInt(s.room));
        e.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512));
        e.set("y", JsonValue::makeInt(s.floorY));
        e.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s.sectorZ * 1024 + 512));
        // Regel 3: Sockel- und Versteck-OCB ueberleben einen Ortswechsel nicht.
        e.set("ocb", JsonValue::makeInt(0));
        edits.push(e);
        used.insert(secKey(s.room, s.sectorX, s.sectorZ));
    };

    // Kandidatensektoren eines Items unter seinen Zwangsbedingungen.
    auto candidatesFor = [&](const LevelItem& it, std::vector<const ZoneSector*>& out)
    {
        out.clear();
        auto ci = conByType.find(it.typeId);
        const KeyConstraint* k = (ci == conByType.end()) ? nullptr : ci->second;

        for (const ZoneSector& s : pool)
        {
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            if (k)
            {
                if (s.zone > k->maxZone) continue;
                if (!k->allowRooms.empty() &&
                    !std::binary_search(k->allowRooms.begin(), k->allowRooms.end(), s.room)) continue;
                if (std::binary_search(k->excludeRooms.begin(), k->excludeRooms.end(), s.room)) continue;
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
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            c.push_back(&s);
        }
        if (c.empty())
        {
            report.ok = false;
            report.error = fmt("Pflicht-Injektion typeId %d: in Raum %d ist kein freier gueltiger Sektor.",
                               j.typeId, j.room);
            return false;
        }
        const ZoneSector& s = *c[static_cast<size_t>(rng.below(c.size()))];
        const RoomOrigin& ro = origin[s.room];

        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(j.typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512));
        a.set("y", JsonValue::makeInt(s.floorY));
        a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + s.sectorZ * 1024 + 512));
        a.set("ocb", JsonValue::makeInt(0));
        adds.push(a);
        used.insert(secKey(s.room, s.sectorX, s.sectorZ));
        ++report.injected;
        report.notes.push_back(fmt("Injiziert: typeId %d in Raum %d (%s)", j.typeId, j.room, j.reason.c_str()));
    }

    patchOut = JsonValue::makeObject();
    patchOut.set("itemEdits", edits);
    if (adds.size() > 0) patchOut.set("itemAdds", adds);
    // Bewusst KEIN itemRemoves - siehe Regel 1 im Header.
    return true;
}

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
        s.clearance = static_cast<int>(secs[i]["clearance"].asInt(0));

        // Wandkanten (#61). Aeltere Exporte kennen das Feld nicht - dann
        // bleibt die Liste leer, und Wand-Pickups finden keinen Platz statt
        // an einer falschen Stelle zu landen.
        const JsonValue& wa = secs[i]["wallAngles"];
        for (size_t w = 0; w < wa.size(); ++w)
            s.wallAngles.push_back(static_cast<int>(wa[w].asInt()));

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

    // Sektoren hinter einem Deadstop (#61), als "raum,x,z" wie die
    // Anmarschsektoren. Fehlt das Feld (aelterer Export), bleibt die Menge
    // leer und nichts aendert sich.
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

    // Typsperren je Raum (siehe RandoInput::typeNoRooms) und kuratierte
    // Antitrigger-Immunitaeten.
    {
        const JsonValue& tnr = zoneExport["typeNoRooms"];
        for (size_t i = 0; i < tnr.size(); ++i)
        {
            const int typ = static_cast<int>(tnr[i]["typeId"].asInt(-1));
            if (typ < 0) continue;
            const JsonValue& rl = tnr[i]["rooms"];
            for (size_t r = 0; r < rl.size(); ++r)
                out.typeNoRooms[typ].insert(static_cast<int>(rl[r].asInt(-1)));
        }
        const JsonValue& ai = zoneExport["antitriggerImmun"];
        for (size_t i = 0; i < ai.size(); ++i)
            out.antitriggerImmunItems.insert(static_cast<int>(ai[i].asInt(-1)));

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

bool Rando::bodenAn(const RandoInput& in, const ZoneSector& s,
                    int lokalX, int lokalZ, int& yOut)
{
    yOut = s.floorY;
    std::map<long long, SchraegeInfo>::const_iterator f =
        in.schraegen.find(secKey(s.room, s.sectorX, s.sectorZ));
    if (f == in.schraegen.end()) return true;       // kein Eintrag: eben

    const BodenLage lage = bodenLageAn(f->second, s.floorY, lokalX, lokalZ);
    yOut = lage.y;
    return lage.begehbar;
}

bool Rando::sectorAllowedFor(const RandoInput& in,
                             const KeyConstraint* k,
                             const ZoneSector& s,
                             int typeId,
                             bool mustBeOnIsland,
                             int itemOcb)
{
    // Wandgriffe (#61): OCB 2 (Brechstange) und OCB 9 (Joby-Crow) verlangen
    // eine Wand und Stehhoehe. Beides sind Engine-Regeln, keine Optik:
    //  - PickUpCollision:803 prueft ausdruecklich !lara.IsDucked, im
    //    Kriechgang ist der Griff also unmoeglich. Laras Kollisionshoehe im
    //    Stehen ist 762 (game\lara.cpp), 768 ist die naechste Stufe darueber.
    //  - Die Wand selbst prueft die Engine NIE - sie ist Optik und
    //    Erreichbarkeit. Ohne Wandkante gaebe es aber keinen sinnvollen
    //    Winkel, und das Modell (Mesh bei ~+470 in Modell-Z) stuende frei im
    //    Raum statt in einer Wand.
    if (itemOcb == 2 || itemOcb == 9)
    {
        if (s.wallAngles.empty()) return false;
        if (s.clearance < 768) return false;

        // Und niemals unter Wasser: PickUpCollision laeuft ueberhaupt nur bei
        // water_status LW_ABOVE_WATER oder LW_WADE (pickup.cpp:766). Gilt NUR
        // fuer diese Griffe - gewoehnliche Pickups und Puzzleteile duerfen
        // sehr wohl im Wasser liegen (Nutzervorgabe 2026-08-19: "Nur diese
        // spezielle Art mit Anim nicht").
        if (in.waterRooms.count(s.room)) return false;
    }

    // Hinter einem Deadstop liegt nichts, was ein Schluessel sein koennte.
    // Gilt fuer JEDEN Typ mit Constraint und zusaetzlich fuer die Wandgriffe,
    // die keine Constraint haben (#61).
    if ((k != nullptr || itemOcb == 2 || itemOcb == 9) &&
        in.keyBarredSectors.count(secKey(s.room, s.sectorX, s.sectorZ)))
        return false;

    // Das Waffenverbot haengt am LEVEL, nicht am Typ-Constraint: eine
    // Schrotflinte ist kein Schluessel und wuerde sonst gar nicht geprueft.
    if (in.weaponTypes.count(typeId) && in.noWeaponRooms.count(s.room)) return false;

    // Kein Item auf Rutschen (Neigung > 2 - Lara kann dort nichts aufheben)
    // oder ueber dem kollisionsfreien Dreieck eines NOCOLF-Sektors. Belaufbare
    // Schraegen sind dagegen erlaubt - ihr y rechnen die Ausgabestellen mit
    // bodenAn auf die echte Bodenhoehe. Entscheidend ist der LANDEPUNKT
    // (Sektormitte plus Typversatz), denn bei Splits gelten je Dreieck eigene
    // Neigungen. Steht wie typeNoRooms VOR dem Constraint-Zweig und gilt
    // damit auf jedem Platzierungsweg - Schluessel, Pickups, Injektionen,
    // Streuung und Abundance.
    {
        std::map<int, PlacementOffset>::const_iterator o = in.typeOffset.find(typeId);
        const PlacementOffset po = (o == in.typeOffset.end()) ? PlacementOffset() : o->second;
        int y = 0;
        if (!bodenAn(in, s, 512 + po.dx, 512 + po.dz, y)) return false;
    }

    // Sperre einzelner Typen fuer einzelne Raeume. Steht bewusst VOR dem
    // Constraint-Zweig und gilt damit auf jedem Platzierungsweg, auch fuer
    // Streuung und Abundance, die ohne Constraint hierherkommen.
    {
        std::map<int, std::set<int>>::const_iterator f = in.typeNoRooms.find(typeId);
        if (f != in.typeNoRooms.end() && f->second.count(s.room)) return false;
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
        int yBoden = 0;
        Rando::bodenAn(in, s, 512 + po.dx, 512 + po.dz, yBoden);
        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro->second.originX) + s.sectorX * 1024 + 512 + po.dx));
        a.set("y", JsonValue::makeInt(yBoden + po.dy));
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
    // Vorgesetzte Codebits heben die Fesselung ebenfalls auf: die Bedingung,
    // die der Trigger erfuellt haette, ist damit schon erfuellt (#61).
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

    // OCB 5 und 10: solche Pickups sind in Vanilla gar nicht aufnehmbar.
    // InitialisePickup (game/init.cpp:470) setzt ein OCB-5-Item auf
    // ITEM_INVISIBLE und kehrt zurueck; PickUpCollision (game/pickup.cpp:737)
    // steigt bei ocb 5 und 10 sofort aus. Sie werden erst durch etwas anderes
    // freigeschaltet. Wuerden wir sie verschieben und dabei - wie bei jedem
    // bewegten Item noetig - OCB auf 0 setzen, laegen sie ab sofort sichtbar
    // und aufnehmbar da. Das ist kein Ortswechsel mehr, sondern eine
    // Verhaltensaenderung, und im Zweifel wird gesperrt statt geoeffnet.
    //
    // Genau diese Sperre hebt ein flagPresets-Eintrag auf, und nur der: dort
    // hat die Kuration nachgesehen, WAS das Item freigeschaltet haette, und
    // ihm die Bedingung abgenommen (Andrea2 Item 62 - der HeavyTrigger unter
    // ihm haengt am Tod des Guardians, siehe #61). Mit vorgesetzten Codebits
    // und genullter OCB liegt es von Anfang an sichtbar da, was hier sonst
    // gerade verhindert werden soll - deshalb ist es eine Handentscheidung
    // und keine abgeleitete Regel.
    for (const LevelItem& it : in.items)
    {
        const int ocb = it.ocb & 0x3F;
        if ((ocb == 5 || ocb == 10) && !in.flagPresets.count(it.index))
            pinned.insert(it.index);
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

    // --- Sektoren von Gegnern sperren, die mit Fracht verschwinden koennen ---
    //
    // GetCarriedItems (setup.cpp:1962) haengt beim Levelstart jedes Pickup
    // im Umkreis von 512 Einheiten an den naechsten Gegner: RemoveDrawnItem,
    // room_number = NO_ROOM - das Item ist danach nur noch dessen Fracht und
    // faellt erst bei seinem Tod wieder heraus.
    //
    // Fuer alle anderen Gegner ist das harmlos, seit der Antitrigger sie
    // nicht mehr loeschen kann und die beiden eigenen Todeswege ihre Fracht
    // ablegen. Ein Fall bleibt: Larson und Pierre, die ihre OCB behalten -
    // sie finden damit Cores eigenen AI_AMBUSH-Marker und fliehen bei 40
    // Lebenspunkten aus dem Level. Betroffen sind genau drei Exemplare im
    // ganzen Spiel (Andrea1 34 und 35, Andrea2 76 - der gescriptete
    // Miniboss, der als handkuriertes Item ohnehin nie getauscht wird).
    //
    // Auf ihren Sektor kommt deshalb kein Pickup. Drei gesperrte Flaechen
    // von mehreren hundert kosten nichts; ein Schluesselitem, das mit einem
    // fliehenden Larson das Level verlaesst, kostet den ganzen Durchgang.
    for (const LevelItem& it : in.items)
    {
        if ((it.typeId != 45 && it.typeId != 47) || it.ocb == 0) continue;
        used.insert(secKey(it.room, it.sectorX, it.sectorZ));
        report.notes.push_back(fmt("Sektor %d,%d,%d gesperrt: Item %d (typeId %d, OCB %d) kann "
                                   "fliehen und wuerde Fracht mitnehmen.",
                                   it.room, it.sectorX, it.sectorZ, it.index, it.typeId, it.ocb));
    }

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
        // Echte Bodenhoehe am Landepunkt statt Sektor-Grundwert: auf einer
        // belaufbaren Schraege laege das Item sonst in der Luft (#86/#87).
        int pyBoden = 0;
        Rando::bodenAn(in, s, 512 + po.dx, 512 + po.dz, pyBoden);
        const int py = pyBoden + po.dy;
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

        // Vorgesetzte Codebits mitschreiben, sonst waere das Item am neuen Ort
        // wieder von seinem alten Trigger abhaengig (#61).
        {
            std::map<int, int>::const_iterator fp = in.flagPresets.find(it.index);
            if (fp != in.flagPresets.end()) e.set("flags", JsonValue::makeInt(fp->second));
        }

        // Wandgriffe brauchen den Winkel ZUR Wand (#61). Sonst behaelt ein
        // verschobenes Item seine alte Ausrichtung - was fuer gewoehnliche
        // Pickups richtig ist (die Engine dreht sie beim Aufheben ohnehin auf
        // Lara), fuer OCB 2 und 9 aber nicht: dort prueft PickUpCollision den
        // echten Winkel (+-30 Grad), und das Modell soll in der Wand stecken.
        // Hat der Sektor mehrere Wandkanten, entscheidet der Zufall.
        if ((neueOcb == 2 || neueOcb == 9) && !s.wallAngles.empty())
        {
            const size_t w = static_cast<size_t>(rng.below(s.wallAngles.size()));
            e.set("angle", JsonValue::makeInt(s.wallAngles[w]));
        }

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
            if (!Rando::sectorAllowedFor(in, k, s, it.typeId, mustBeOnIsland, it.ocb)) continue;
            out.push_back(&s);
        }
    };

    // Mindestabstand zwischen den WICHTIGEN Fundstuecken - Schluessel,
    // Puzzleteile und Waffen (Nutzerwunsch 2026-08-19: "Bisschen mehr Spread
    // bei den Keys, Puzzels und den Waffen waere cool").
    //
    // Vorher zog die Auswahl gleichverteilt aus allen erlaubten Sektoren, ohne
    // die schon gesetzten anzusehen. Gemessen ueber 20 Andrea2-Seeds landeten
    // dabei 6 von 200 Paaren naeher als drei Sektoren und 2 im selben Raum -
    // das ist ungefaehr Zufallserwartung, faellt beim Spielen aber auf, weil
    // ein Paar nebeneinander im Gedaechtnis bleibt und die gut verteilten
    // Paare nicht.
    //
    // Kein hartes Verbot, sondern Stufen: erst 5 Sektoren Abstand versuchen,
    // dann 3, dann 2, zuletzt ohne Bedingung. So bleibt eine enge Zone
    // platzierbar - ein Lauf, der an einer Geschmacksregel scheitert, waere
    // schlechter als zwei Fundstuecke, die naeher beieinander liegen.
    std::vector<std::pair<int, int>> gesetztXZ;   // Weltkoordinaten der bisherigen

    auto waehleMitAbstand = [&](const std::vector<const ZoneSector*>& c) -> const ZoneSector*
    {
        static const int stufen[] = { 5120, 3072, 2048, 0 };
        for (int st : stufen)
        {
            if (st == 0 || gesetztXZ.empty())
                return c[static_cast<size_t>(rng.below(c.size()))];

            std::vector<const ZoneSector*> weit;
            for (const ZoneSector* s : c)
            {
                const RoomOrigin& ro = origin[s->room];
                const int sx = ro.originX + s->sectorX * 1024 + 512;
                const int sz = ro.originZ + s->sectorZ * 1024 + 512;
                bool ok = true;
                for (size_t i = 0; i < gesetztXZ.size() && ok; ++i)
                {
                    const long long dx = sx - gesetztXZ[i].first;
                    const long long dz = sz - gesetztXZ[i].second;
                    if (dx * dx + dz * dz < static_cast<long long>(st) * st) ok = false;
                }
                if (ok) weit.push_back(s);
            }
            if (!weit.empty()) return weit[static_cast<size_t>(rng.below(weit.size()))];
        }
        return c[static_cast<size_t>(rng.below(c.size()))];
    };

    auto merkePlatz = [&](const ZoneSector& s)
    {
        const RoomOrigin& ro = origin[s.room];
        gesetztXZ.push_back({ ro.originX + s.sectorX * 1024 + 512,
                              ro.originZ + s.sectorZ * 1024 + 512 });
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
            const ZoneSector* w = waehleMitAbstand(c);
            placeAt(it, *w);
            merkePlatz(*w);
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
            // Waffen zaehlen zum Spread, gewoehnliche Munition und Medipacks
            // nicht - die duerfen dicht liegen, sie sind kein Fund, den man
            // sucht.
            if (in.weaponTypes.count(it.typeId))
            {
                const ZoneSector* w = waehleMitAbstand(c);
                placeAt(it, *w);
                merkePlatz(*w);
            }
            else
                placeAt(it, *c[static_cast<size_t>(rng.below(c.size()))]);
            ++report.movedOtherItems;
        }
    }

    // Pflicht-Injektionen. Diese haengen sich hinten an die Itemliste an und
    // verschieben deshalb keine bestehenden Indizes - anders als ein Entfernen.
    for (const Injection& j : in.injections)
    {
        // Raumauswahl: entweder die Liste (Seed sucht darin) oder der eine Raum.
        const bool ausListe = !j.rooms.empty();
        std::set<int> erlaubteRaeume(j.rooms.begin(), j.rooms.end());

        std::vector<const ZoneSector*> c;
        for (const ZoneSector& s : pool)
        {
            if (ausListe ? !erlaubteRaeume.count(s.room) : (s.room != j.room)) continue;
            // Bei festem Sektor zaehlt nur er, und ob er schon belegt ist,
            // spielt keine Rolle: mehrere Injektionen duerfen sich dort stapeln.
            if (j.hasSector)
            {
                if (s.sectorX == j.sectorX && s.sectorZ == j.sectorZ) c.push_back(&s);
                continue;
            }
            if (used.count(secKey(s.room, s.sectorX, s.sectorZ))) continue;
            // Typsperren gelten auch hier: eine Pflicht-Injektion darf nicht in
            // einen Raum, den die Kuration fuer genau diesen Typ gesperrt hat.
            if (!Rando::sectorAllowedFor(in, nullptr, s, j.typeId, false)) continue;
            c.push_back(&s);
        }
        if (c.empty())
        {
            report.ok = false;
            report.error = j.hasSector
                ? fmt("Pflicht-Injektion typeId %d: Sektor %d,%d,%d ist keine gueltige Ablage.",
                      j.typeId, j.room, j.sectorX, j.sectorZ)
                : ausListe
                    ? fmt("Pflicht-Injektion typeId %d: in den %d genannten Raeumen ist kein freier "
                          "gueltiger Sektor.", j.typeId, static_cast<int>(j.rooms.size()))
                    : fmt("Pflicht-Injektion typeId %d: in Raum %d ist kein freier gueltiger Sektor.",
                          j.typeId, j.room);
            return false;
        }
        const ZoneSector& s = j.hasSector ? *c[0] : *c[static_cast<size_t>(rng.below(c.size()))];
        const RoomOrigin& ro = origin[s.room];

        const PlacementOffset po = offFor(j.typeId);
        int yBoden = 0;
        Rando::bodenAn(in, s, 512 + po.dx, 512 + po.dz, yBoden);
        JsonValue a = JsonValue::makeObject();
        a.set("typeId", JsonValue::makeInt(j.typeId));
        a.set("room", JsonValue::makeInt(s.room));
        a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512 + po.dx));
        a.set("y", JsonValue::makeInt(yBoden + po.dy + j.yOffset));
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
            int yBoden = 0;
            Rando::bodenAn(in, s, 512 + po.dx, 512 + po.dz, yBoden);
            JsonValue a = JsonValue::makeObject();
            a.set("typeId", JsonValue::makeInt(typeId));
            a.set("room", JsonValue::makeInt(s.room));
            a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s.sectorX * 1024 + 512 + po.dx));
            a.set("y", JsonValue::makeInt(yBoden + po.dy));
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
                int yBoden = 0;
                Rando::bodenAn(in, *s, 512 + po.dx, 512 + po.dz, yBoden);
                JsonValue a = JsonValue::makeObject();
                a.set("typeId", JsonValue::makeInt(typ));
                a.set("room", JsonValue::makeInt(s->room));
                a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + s->sectorX * 1024 + 512 + po.dx));
                a.set("y", JsonValue::makeInt(yBoden + po.dy));
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

    // Anzeigenamen siehe Rando::gegnerarten() am Dateiende - die Liste dort
    // muss mit dieser Tabelle mitwachsen.
    //
    // --- Cross-Gegnermix: den Pool um fremde Typen erweitern ---
    //
    // Die globale Pool-Tabelle ist die Nutzerliste "Gegner Pool.txt", je Typ
    // mit dem Spenderlevel, das ihn MIT Vanilla-Instanzen traegt - nur dort
    // liegen Modell UND Samples beisammen. Die Sound-Eintraege sind die
    // bekannten Ausnahmen, deren Samples selbst dem Spender fehlen (Larsons
    // Sterbeklaenge, Pierres nie ausgelieferter Schuss - beide vom Nutzer im
    // Soundboard ausgesucht und im Spiel geprueft).
    //
    // Hydra (63) ist seit dem 2026-08-15 dabei - Begruendung an ihrer Zeile.
    // Chef, Scientist und Guardian stehen aus bekannten Gruenden nirgends im
    // Pool.
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
    // beim Import unter dem Ausweichslot swapDst. Seit #111 hat JEDER Gegner
    // seinen eigenen (Soldatenfamilie 462-467 jenseits der Dateigrenze,
    // Einzelgaenger 401-404) - vorher teilten sich die Familien je Spender
    // einen Slot, und einheimische Nutzer desselben Slots zogen fremde
    // Haende (joby2 Item 69). Die Engine greift bevorzugt darauf zu
    // (MeshSwapQuelle in setup.cpp, Zuordnung als MESHSWAP_CROSS_* in
    // objects.h - beide Seiten muessen uebereinstimmen). swapVon kann vom
    // Modellspender abweichen: rich1 (Modell fuer Guard und TwoGun) hat
    // selbst KEINEN MESHSWAP1 - die Guard-Haende kommen aus joby3, TwoGuns
    // Quellmesh 2 aus rich3.
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
    // "tr3" (0 = aus TR5 selbst): Kennung der Fremdquelle aus
    // Tr3Quelle::dinos(). Ist sie gesetzt, ist "von" leer - das Modell kommt
    // nicht aus einem TR5-Level, sondern zur Laufzeit aus der Installation des
    // Nutzers. Alles Weitere steht in der Dino-Tabelle, damit es nur EINE
    // Wahrheit gibt.
    // "quellSlot" (0 = wie typeId): der Slot, unter dem das Modell BEIM
    // SPENDER liegt. Gebraucht fuer die neuen Engine-Slots jenseits 460
    // (#105): der Joby-Wachmann liegt in jedem Spender unter 37, wohnt im
    // Ziel aber unter 460 - nur so koennen beide Guard-Modelle nebeneinander
    // existieren. Solche Level laufen NUR auf unserer Engine (die wird per
    // #31 ohnehin mitgeliefert); trview kommt mit ihnen klar, am Quelltext
    // geprueft 2026-08-17.
    struct CrossGegner { int typeId; const char* von; CrossSound sounds[2];
                         int begleiter[2]; const char* swapVon; int swapDst;
                         int animZahl; int basisSlot; const char* tr3;
                         int quellSlot; };
    static const CrossGegner kCross[] = {
        { 33, "rich3",   {},                                    {-1,-1},  "rich3",   462, 0, 0 },  // SWAT
        // Klang 88 (BIO_BREATHE_OUT): das Gasmasken-Atmen des SWAT+ in
        // dunklen Raeumen (sas.cpp:155, fest verdrahtet - steht in keinem
        // Anim-Kommando und reist deshalb nicht von selbst). Ohne den
        // Eintrag atmet er in fremden Leveln stumm (#111 Teil 3).
        { 35, "rich3",   { {88,"rich3",88} },                   {-1,-1},  "rich3",   463, 0, 33 }, // Advanced SWAT (Anims von SWAT)
        { 37, "rich1",   {},                                    {-1,-1},  "joby3",   465, 0, 0 },  // Guard (Blue Guard)
        // Der zweite Wachmann (#83/#105, Engine-Slot 460 = BLUE_GUARD2):
        // das Joby-Modell aus joby3 - dort ist der Guard der Anim-Anker der
        // Familie und traegt den vollen eigenen Satz (Mafia holt sich seine
        // Anims von ihm). joby2s Guard laesst sich NICHT kopieren
        // (Frame-Zeiger, siehe Crane Guy). Kein Basistyp: er bringt alles
        // mit; steht zusaetzlich SWAT/Guard im Ziel, biegt setup.cpp ihn
        // zur Laufzeit auf deren Satz um wie jedes Familienmitglied.
        // Tauschhaelften (Feuerhaende 10/13) wie der Guard aus joby3.
        { 460, "joby3",  {},                                    {-1,-1},  "joby3",   467, 0, 0, nullptr, 37 }, // Wachmann (Joby)
        // Laserschuss (193) und -einschlag (167): fest verdrahtet in
        // twogun.cpp:616f, in keinem Anim-Kommando - ohne die Eintraege
        // feuert der Laser in fremden Leveln stumm (#111 Teil 3). Beide
        // Samples sind in rich1 und rich3 byte-identisch; die 167 setzt
        // auch die Hydra (aus Andrea2), ebenfalls identischer Inhalt.
        { 39, "rich1",   { {167,"rich3",167}, {193,"rich3",193} }, {-1,-1}, "rich3",  464, 0, 0 },  // Two Gun
        { 41, "Andrea1", {},                                    {-1,-1},  nullptr,   -1 },  // Dog
        { 45, "Andrea1", { {96,"rich1",85}, {98,"rich1",143} }, {-1,-1},  nullptr,   -1 },  // Larson
        { 47, "Andrea3", { {90,"joby5",45}, {91,"joby5",123} }, {-1,-1},  nullptr,   -1 },  // Pierre
        { 49, "joby3",   {},                                    {-1,-1},  "joby3",   466, 0, 37 }, // Soldier MG (Mafia, Anims von Guard)
        { 51, "joby3",   {},                                    {406,-1}, nullptr,   -1 },  // Soldier Pistols (Mafia2) + MESHSWAP2
        // Crane Guy: der einzige TR5-Gegner mit vollstaendigem Modell, den
        // Core NIE als Item platziert hat - dieselbe Lage wie bei Pierre.
        // Modell nur in joby2 (15 Meshes, eigene Anims), Steuerung ist
        // SasControl, er feuert ueber Zustand 3 wie der Blue Guard
        // (sas.cpp:344 nennt ihn dort ausdruecklich). Im Testlevel bestaetigt.
        //
        // basisSlot 37: er holt seinen Anim-Satz zur Laufzeit von SWAT, sonst
        // Guard (setup.cpp:1211-1214) - Stummel-Familie wie Mafia und SWAT+.
        // Der Basisslot wird ueber SEINE eigene kCross-Zeile geholt, also aus
        // rich1. Das ist wichtig: der Guard aus joby2 laesst sich NICHT
        // kopieren ("Frame-Zeiger ausserhalb der Frame-Scheibe"), der aus
        // rich1 schon.
        { 55, "joby2",   {},                                    {-1,-1},  nullptr,   -1, 0, 37 }, // Crane Guy
        { 57, "Andrea3", {},                                    {-1,-1},  nullptr,   -1 },  // Lion
        { 59, "Andrea3", {},                                    {-1,-1},  "Andrea3", 401 }, // Gladiator
        // Der Roman God, ZWEI Fassungen, zwei Slots (#84, Nutzeridee).
        //
        // Vanilla teilen sie sich Slot 61 und schliessen sich damit aus:
        // Andrea2 fuehrt dort das SCHWERT (Item 108, OCB 1 - Fernkampf,
        // gruener Blitz), Andrea3 den HAMMER (Item 79, OCB 0 - Nahkampf,
        // blau). Die Waffe waehlt die OCB des ITEMS, das Aussehen der Slot -
        // passen die nicht zusammen, fuehrt er die Klinge wie einen Hammer
        // (vom Nutzer im Spiel fotografiert).
        //
        // Mit dem eigenen Slot 461 fuer den Hammer passen jetzt BEIDE in
        // dasselbe Level. Die Spawn-OCB dazu steht in kSpawnOcb.
        //
        // Spender: 61 kommt aus Andrea2 (Schwert), 461 aus Andrea3 (Hammer,
        // dort unter Slot 61 - daher quellSlot). Beide Level fuehren 61 in
        // ihrer kuratierten Liste und importieren es deshalb nie selbst;
        // Andrea3 ueberspringt 461 zusaetzlich als eigener Spender.
        //
        // Begleiter BUBBLES (367): das Geschoss der SCHWERT-Fassung ist wie
        // bei der Hydra kein Sprite, sondern ein Mesh aus diesem Slot
        // (romangod.cpp:119 und 228). Ohne ihn fliegt in fremden Leveln
        // etwas Falsches.
        { 61,  "Andrea2", {},                                   {367,-1}, "Andrea2", 403 }, // Roman God (Schwert)
        // Sterbelaut 204 (SGOD_SWD_DIE1_Q): die Sterbeanim +16 der
        // Hammer-Fassung ruft ihn, aber nur Andrea2 belegt die Nummer -
        // zu Hause in Andrea3 starb die Statue stumm (Core-Loch). Mit dem
        // Eintrag quiekt sie ueberall wie ihr Schwert-Bruder
        // (Nutzerentscheidung 2026-08-19: "dann sind beide komplett").
        { 461, "Andrea3", { {204,"Andrea2",204} },              {-1,-1},  "Andrea3", 404, 0, 0, nullptr, 61 }, // Roman God (Hammer)
        // Hydra: lange bewusst draussen ("drei Items auf einer Position, die
        // OCB waehlt das Kettenglied"). Nach der Durchsicht am 2026-08-15
        // aufgenommen - die OCB ist harmlos, alles andere traegt:
        //  - InitialiseHydra nutzt trigger_flags NUR fuer den Startframe
        //    (30 * OCB, damit die drei Koepfe nicht synchron zucken) und
        //    einen Versatz von +-384 in z. Mit OCB 0 faellt beides weg, und
        //    der Pool setzt ohnehin nur OCB 0.
        //  - Toetbar trotz objects[].undead: HydraControl rechnet ihren
        //    Schaden SELBST aus (hydra.cpp:154 und 175) - im Flammenzustand 2
        //    auf Distanz, in den Angriffszustaenden aus der Naehe, mit der
        //    Schrotflinte dreifach. 30 Trefferpunkte.
        //  - Sie raeumt hinter sich auf: Todesanim +15 traegt Kmd 4
        //    (KillItem), davor sprengt sie sich Knoten fuer Knoten.
        //  - Ihre Steuerung greift nur auf anim_index+15, also kein Zugriff
        //    jenseits der Objektgrenze (das Imp-Problem entfaellt), und sie
        //    braucht weder MESHSWAP noch Begleitslots.
        //  - Ihre vier Anim-Sounds (406-409) wandern automatisch mit, die
        //    holt Phase D des Transfers aus den Anim-Befehlen. Der einzige
        //    fest verdrahtete Klang ist SFX_SMASH_ROCK beim Zerbersten - der
        //    steht in KEINEM Anim-Befehl und braucht deshalb diesen Eintrag.
        //    Seine Nummer ist 167, nicht 168: im Spiel nachgewiesen ueber
        //    "Non present sample:167" im Log, nachdem eine Zaehlung im Enum
        //    von sound.h um eins danebenlag. Andrea2 hat ihn selbst, der
        //    Spender ist also derselbe wie fuers Modell.
        // Die drei Vanilla-Exemplare in Andrea2 (Items 91/92/93) sind ueber
        // fixedItems geschuetzt und bleiben unberuehrt.
        //  - animZahl 17: die Objekttabelle gesteht ihr weniger zu, als ihre
        //    Steuerung braucht (anim_index+15 ist die Todesanim) - dasselbe
        //    Muster wie beim Imp. Ohne die explizite Laenge bricht der
        //    Transfer mit "Frame-Zeiger ausserhalb der Frame-Scheibe" ab.
        //  - Begleitslot BUBBLES (367): ihr Feuergeschoss ist KEIN Sprite,
        //    sondern das 17. Mesh dieses Slots (hydra.cpp:280,
        //    objects[BUBBLES].mesh_index + 16). Fuehrt das Ziel dort andere
        //    Meshes, fliegt etwas Falsches durch die Luft - im Spiel gesehen
        //    in Andrea1. Andrea2 traegt in 367 genau den Geschosskern samt
        //    Flammenfluegeln (vom Nutzer im Objektbaum nachgesehen).
        //    Das Ersetzen ist gefahrlos: die LUFTBLASEN im Wasser haben mit
        //    diesem Slot nichts zu tun, sie laufen ueber ein eigenes
        //    Partikelsystem (Bubbles[40], CreateBubble in tomb4fx.cpp:1438).
        //    Auch Andrea2 selbst hat Wasser und trotzdem den Geschosskern
        //    in 367 - der beste Beleg dafuer.
        { 63, "Andrea2", { {167,"Andrea2",167} },               {367,-1}, nullptr,   -1, 17 },  // Hydra
        { 77, "Andy2",   {},                                    {-1,-1},  nullptr,   -1 },  // Maze Monster
        // Huskie (85): der Hund aus joby2 - ein EIGENER Gegner, nicht der
        // Hund (41). Eigene Steuerung (HuskieControl), eigenes Initialise,
        // 24 statt 18 Trefferpunkte, 26 statt 21 Meshes. Der Motor kennt ihn
        // vollstaendig, es war nur nie jemand im Pool.
        //
        // Mit SlotAnims ueber alle 13 Level gemessen: Slot 85 traegt NUR in
        // joby2 ein Modell (anim_index 579, Scheibe 25 Anims). Seine
        // Steuerung greift bis anim_index+22 (DeathAnims 20/21/22) - die
        // Scheibe reicht, also kein animZahl noetig (anders als Imp/Hydra).
        // Kein MESHSWAP, kein Begleiter, kein Basistyp; die Klaenge haengen
        // alle an Anim-Kommandos und wandern von selbst mit (huskie.cpp
        // ruft SoundEffect nirgends fest verdrahtet auf).
        //
        // OCB: InitialiseHuskie startet bei trigger_flags != 0 auf anim+1
        // statt +8 und nimmt ITEM_INVISIBLE weg. Der Pool setzt immer OCB 0,
        // und Items mit OCB != 0 werden ohnehin nie getauscht.
        //
        // joby2s eigene drei Huskies (Items 4, 86, 153) stehen nicht in
        // seiner kuratierten Liste - sie bleiben vanilla, joby2 gibt nur ab.
        { 85, "joby2",   {},                                    {-1,-1},  nullptr,   -1 },  // Huskie
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

        // --- Gaeste aus Tomb Raider 3 (#92) ---------------------------------
        //
        // Kein Spenderlevel: "von" bleibt leer, das Modell kommt zur Laufzeit
        // aus der Installation des Nutzers. Alles Naehere - Datei, Objekt,
        // Klaenge, Lautstaerken - steht in Tr3Quelle::dinos().
        //
        // Die vier Zielslots tragen in KEINEM der 16 TR5-Level ein Modell (mit
        // MissMod an allen nachgemessen), sie nehmen also keinem einheimischen
        // Gegner etwas weg. Weder Basistyp noch Begleiter noch Tauschhaelften -
        // sie bringen alles selbst mit.
        { 31, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3"     }, // T-Rex
        { 43, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3gold" }, // Geier
        { 53, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3"     }, // Compy
        { 75, nullptr,   {},                                    {-1,-1},  nullptr,   -1, 0, 0, "tr3"     }, // Raptor
    };

    // Verbotene Typen (siehe RandoInput::verboteneTypen) fliegen aus dem
    // Pool, BEVOR irgendetwas waehlt - weder Tausch noch Zusatzgegner noch
    // Cross duerfen sie anfassen.
    std::vector<int> gegnerPool;
    for (int t : in.enemyTypes)
        if (!in.verboteneTypen.count(t) && !opt.gesperrteGegner.count(t))
            gegnerPool.push_back(t);

    // Die Notbremse fuer "alles abgewaehlt" steht ABSICHTLICH erst hinter dem
    // Cross-Pool (#109) - sie stand hier und war damit der Grund, dass eine
    // gezielte Auswahl nicht durchkam.
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
            if (opt.gesperrteGegner.count(c.typeId)) continue; // vom Nutzer abgewaehlt

            // Gaeste aus Tomb Raider 3 (#92): sie brauchen die Installation
            // des Nutzers. Fehlt sie, fallen sie STILL heraus - ein Lauf darf
            // nie daran scheitern, dass jemand die Spiele nicht besitzt.
            if (c.tr3)
            {
                const bool gold = (std::strcmp(c.tr3, "tr3gold") == 0);
                // Der Geier braucht BEIDE: sein Modell steckt in Gold, seine
                // Klaenge in der MAIN.SFX von TR3 (siehe Tr3Quelle::sfxPfad).
                if (opt.tr3Dir.empty()) continue;
                if (gold && opt.tr3GoldDir.empty()) continue;
                kandidaten.push_back(&c);
                continue;
            }

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

    // Notbremse fuer "der Nutzer hat ALLES abgewaehlt" (#109).
    //
    // Sie steht hier unten und nicht mehr vor dem Cross-Pool - und genau das
    // war der Fehler, den der Nutzer gemeldet hat: Wer im Fenster
    // "Gegnerarten" nur die Dinos anhakt, waehlt damit alle einheimischen
    // Arten ab. Oben war der Pool dann leer, die Bremse fuellte ihn mit der
    // KOMPLETTEN Vanilla-Liste wieder auf (die Abwahl also ignoriert), und
    // die Dinos kamen nur obendrauf. Ergebnis: der halbe Level blieb vanilla,
    // obwohl "nur Dinos" gewaehlt war.
    //
    // Jetzt zaehlt, was am Ende WIRKLICH im Topf liegt. Eine Auswahl, die
    // durch Cross-Typen erfuellbar ist, kommt unveraendert durch. Die Bremse
    // greift nur noch im echten Notfall - etwa wenn jemand ausschliesslich
    // Dinos waehlt, aber die TR3-Pfade fehlen und sie deshalb still
    // herausfallen. Dann ist ein Level mit Vanilla-Gegnern immer noch besser
    // als eines, in dem gar nichts mehr passiert.
    if (gegnerPool.empty())
    {
        for (int t : in.enemyTypes)
            if (!in.verboteneTypen.count(t)) gegnerPool.push_back(t);
        if (!gegnerPool.empty())
            report.notes.push_back("Gegnerarten: die Auswahl liess fuer dieses Level keinen "
                                   "einzigen Typ uebrig (auch keinen fremden) - fuer dieses "
                                   "Level ignoriert, sonst haette es gar keine Gegner.");
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
    std::set<int> immunGesetzt;      // Items, die schon beim Tausch immun wurden
    // Platzbedarf: manche Gegner brauchen eine Arena, kein Kaemmerchen.
    //
    // Die Hydra ist der Anlass. Sie steht fest verwurzelt, dreht sich um volle
    // 360 Grad und kaempft auf Distanz mit ihrem Flammengeschoss - in einer
    // engen Kammer kann Lara weder ausweichen noch die Reichweite nutzen.
    //
    // Gemessen wird NICHT der Raum, sondern der freie Platz UM DEN SEKTOR
    // herum - ein Fenster von 7x7 Feldern, in dessen Mitte der Gegner steht.
    // Zwei Irrwege liegen dahinter, beide verworfen:
    //
    //  - Der einzelne Raum ist das falsche Mass. Die Raumaufteilung eines
    //    TR5-Levels folgt dem Baukasten des Editors, nicht dem, was Lara als
    //    Halle wahrnimmt: ihre eigene Arena in Andrea2 besteht aus SIEBEN
    //    Raeumen (71, 72, 116, 117, 121, 140, 194), Raum 116 allein waere nur
    //    4x8 begehbar. Jede Schwelle auf Raumebene haette ihren eigenen
    //    Kampfplatz ausgeschlossen.
    //  - Die zusammenhaengende Flaeche ist zu grosszuegig. Sie verschmilzt
    //    alles, was auf gleicher Hoehe verbunden ist - in Andrea1 waren das
    //    619 von 961 Sektoren, Korridore eingeschlossen. Die Hydra haette im
    //    Gang stehen duerfen, nur weil der Gang an eine Halle andockt.
    //
    // Das Fenster misst genau das, was zaehlt: wie offen es dort ist, wo sie
    // steht. Die Schwelle stammt nicht aus dem Bauch, sondern aus Vanilla -
    // an allen drei Kampfplaetzen in Andrea2 (Items 91/92/93) gemessen:
    // im 3x3-Fenster 9 von 9 Feldern frei, im 7x7 genau 40 von 49.
    //
    // Gewaehlt ist 3x3 mit allen neun Feldern (Nutzerentscheidung): kein
    // einziges Nachbarfeld darf verbaut sein. Das ist scharf genug, um
    // Kammern, Nischen und Gaenge auszuschliessen, und laesst ihr trotzdem
    // Kampfplaetze in fast jedem Level. Ein 7x7-Fenster mit 40 Feldern waere
    // die Halle gewesen - das Mass hielten spielweit nur 255 Sektoren, fuenf
    // Level haetten die Hydra nie zu sehen bekommen.
    //
    // Der Zonen-Export ist dabei ein PLATZIERUNGS-Pool, kein
    // Begehbarkeitsraster: er fuehrt nur Sektoren, auf denen wirklich etwas
    // stehen darf. Lueckenlose Bloecke sind darin selten - umso wichtiger,
    // dass Messlatte und Mass aus derselben Quelle stammen.
    //
    // Die Tabelle gilt fuer JEDEN Weg, auf dem ein Typ irgendwo landet, also
    // fuer Tausch und Zusatzgegner gleichermassen - auch fuer den heimischen
    // Pool: in Andrea2 steht die Hydra im eigenen Levelpool und koennte sonst
    // ueber den Tausch in eine Abstellkammer geraten. Die drei Vanilla-Hydras
    // stehen in fixedItems und bleiben ohnehin unberuehrt.
    struct Raumbedarf { int typeId; int fenster; int frei; };
    static const Raumbedarf kRaumbedarf[] = {
        { 63, 3, 9 },    // Hydra: 3x3-Fenster, alle 9 Felder frei
    };

    // Rudelgegner: Typen, die allein nicht funktionieren (#98).
    //
    // Der Compsognathus ist der Anlass. Seine Steuerung ist ganz auf die
    // Gruppe gebaut: `bits = 512 * (item_number & 7) - 1792` (compy.cpp)
    // verteilt die Tiere anhand ihrer ITEM-NUMMER auf acht Winkel eines
    // Kreises um das Ziel, und der Angriffsentschluss haengt an zwei
    // GLOBALEN Zaehlern (compy_scared_timer, compys_attack_lara), die alle
    // Compys gemeinsam fuehren. Ein einzelner Compy umkreist Lara deshalb
    // nur und traut sich fast nie heran - im Spiel als "die KI spielt
    // verrueckt" gesehen (Nutzerbefund 2026-08-17).
    //
    // Vier aufeinanderfolgende Item-Nummern ergeben vier verschiedene
    // Winkel - genau das liefert unser Anhaengen von selbst.
    //
    // Ein Rudel wird als EIN Zusatzgegner-Platz gesetzt: alle Tiere auf
    // denselben Sektor, geweckt von EINEM Trigger mit mehreren
    // Object-Kommandos. Mehrere Items auf einer Position sind in TR5
    // ueblich - Andrea2 fuehrt die drei Hydra-Glieder genauso.
    struct Herde { int typeId; int anzahl; };
    static const Herde kHerden[] = {
        { 53, 4 },      // Compsognathus
    };

    // Spawn-OCB: manche Gegner lesen ihre OCB als VERHALTEN, nicht als
    // Ortsangabe - fuer die ist "immer 0" falsch.
    //
    // Der Roman God ist der Anlass (vom Nutzer im Spiel fotografiert). Seine
    // Steuerung liest trigger_flags als WAFFENART:
    //   trigger_flags == 1 -> Fernkampf, wirft ein Geschoss  (romangod.cpp:415)
    //   trigger_flags == 0 -> Nahkampf mit dem Hammer        (romangod.cpp:417)
    // sogar die Farbe seiner Blitze haengt daran (romangod.cpp:516).
    //
    // Und die Fassungen unterscheiden sich je Level: Andrea2 fuehrt ihn mit
    // SCHWERT (Item 108, OCB 1 - gruen, schiesst), Andrea3 mit HAMMER
    // (Item 79, OCB 0 - blau, schlaegt). Mit unserer Vorgabe OCB 0 bekam ein
    // in Andrea2 gesetzter Roman God das Hammer-Verhalten zum Schwert-Modell:
    // er hielt die Klinge wie einen Hammer und der Blitz war blau statt gruen.
    // Genau das hat der Nutzer gesehen - nur das Vanilla-Item 108 stimmte.
    //
    // Die Regel folgt dem MODELL, nicht dem Level an sich: wo das eigene
    // Schwert-Modell steht, muss OCB 1 hin. Alle anderen Level bekommen das
    // Hammer-Modell aus Andrea3 importiert und bleiben bei 0.
    // Seit beide Fassungen einen eigenen Slot haben (Nutzeridee), folgt die
    // OCB dem SLOT statt dem Level - mit einer Ausnahme: Andrea3 fuehrt in
    // seinem eigenen Slot 61 den Hammer, nicht das Schwert. Die
    // levelgenaue Zeile steht deshalb VOR der allgemeinen, erste Treffer
    // gewinnt.
    struct SpawnOcb { int typeId; const char* level; int ocb; };  // level 0 = ueberall
    static const SpawnOcb kSpawnOcb[] = {
        {  61, "andrea3", 0 },  // Andrea3s eigener Slot 61 IST der Hammer
        {  61, nullptr,   1 },  // sonst Schwert-Fassung: Fernkampf
        { 461, nullptr,   0 },  // Hammer-Fassung: Nahkampf
    };
    auto spawnOcbFuer = [&klein](int typeId) -> int {
        for (const SpawnOcb& s : kSpawnOcb)
            if (s.typeId == typeId && (!s.level || klein == s.level)) return s.ocb;
        return 0;
    };
    auto herdenGroesse = [](int typeId) -> int {
        for (const Herde& h : kHerden)
            if (h.typeId == typeId) return h.anzahl;
        return 1;
    };

    // Weltraster aller begehbaren Sektoren, Schluessel ist das Feld in
    // Sektorschritten. Bewusst in.sectors und nicht der laufend gefilterte
    // Pool: gemessen werden soll, wie offen die Umgebung IST, nicht wo gerade
    // noch etwas hindarf. Genau diese Quelle lag auch der Vanilla-Messung
    // zugrunde, aus der die 40 stammen - Mass und Messlatte muessen aus
    // derselben Quelle kommen, sonst vergleicht man Aepfel mit Birnen.
    //
    // Je Feld werden die Bodenhoehen gesammelt, weil uebereinander liegende
    // Raeume dieselben x/z teilen. Ohne Hoehenvergleich zaehlte der Balkon
    // ueber ihr als freier Platz neben ihr.
    std::map<long long, std::vector<int>> freiRaster;
    for (const ZoneSector& s : in.sectors)
    {
        std::map<int, RoomOrigin>::const_iterator o = origin.find(s.room);
        if (o == origin.end()) continue;
        const long long gk =
            (static_cast<long long>((o->second.originX + s.sectorX * 1024) / 1024) << 24)
          |  static_cast<long long>((o->second.originZ + s.sectorZ * 1024) / 1024);
        freiRaster[gk].push_back(s.floorY);
    }

    auto genugPlatz = [&origin, &freiRaster]
                      (int typeId, int room, int sectorX, int sectorZ, int y) -> bool
    {
        const Raumbedarf* b = 0;
        for (const Raumbedarf& r : kRaumbedarf)
            if (r.typeId == typeId) { b = &r; break; }
        if (!b) return true;                        // kein Sondermass, alles erlaubt

        std::map<int, RoomOrigin>::const_iterator o = origin.find(room);
        if (o == origin.end()) return false;        // Raum unbekannt: nicht riskieren
        const int gx = (o->second.originX + sectorX * 1024) / 1024;
        const int gz = (o->second.originZ + sectorZ * 1024) / 1024;
        const int r  = b->fenster / 2;

        int frei = 0;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
            {
                const long long nk = (static_cast<long long>(gx + dx) << 24)
                                   |  static_cast<long long>(gz + dz);
                std::map<long long, std::vector<int>>::const_iterator f = freiRaster.find(nk);
                if (f == freiRaster.end()) continue;
                for (int h : f->second)
                {
                    int dy = h - y;
                    if (dy < 0) dy = -dy;
                    if (dy <= 512) { ++frei; break; }   // ein Stockwerk zaehlt einmal
                }
            }
        return frei >= b->frei;
    };

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
            // OCB != 0 heisst in der Regel: typspezifischer Spezial-Spawn.
            // Die OCB steuert Initialise/Control des URSPRUNGSTYPS (Imp
            // 1/2/3/11/12: Kletter-/Wurfzustaende auf Wandvorspruengen,
            // imp.cpp:82) - ein fremder Typ versteht sie nicht und steht dann
            // in der Wand (im Spiel gesehen: andy3 Items 2/3, Klippen-Imps
            // OCB 3 wurden SWAT/Guard - Grafikloch, Zielerfassung flackert).
            //
            // Larson und Pierre (45/47) sind die begruendete Ausnahme: bei
            // ihnen beschreibt die OCB KEINE Sonderumgebung, sondern nur drei
            // harmlose Dinge (pierre.cpp:70-83 und 124-135) - die Kennung
            // ihres Fluchtmarkers (item_flags[3]), einen Versatz von 384
            // Einheiten innerhalb desselben Sektors und den Start im
            // Wachmodus. Nichts davon steckt in der Geometrie. Ein fremder
            // Typ darf hier also einziehen, solange die OCB dabei auf 0
            // faellt - sonst deutet er sie als seine eigene.
            //
            // Betroffen sind im ganzen Spiel drei Items: Andrea1 34 (OCB 1)
            // und 35 (OCB 2), beide mit eigenem AI_AMBUSH-Marker in Raum 89
            // bzw. 169, sowie Andrea2 76 - der gescriptete Miniboss, der
            // ueber fixedItems geschuetzt ist und oben schon aussortiert
            // wurde.
            // NACHTRAG 2026-08-17 (#109), Nutzerdurchsicht Item fuer Item:
            // Die Sperre war zu breit. Bei der Soldatenfamilie waehlt die OCB
            // nur die SPAWN-POSE (InitialiseSas: durchs Fenster einsteigen,
            // vom Sims abseilen, aus dem Sitzen aufstehen) - das steckt nicht
            // in der Geometrie, der neue Typ steht danach einfach normal da.
            //
            // Und der Imp ist NICHT der Sonderfall, fuer den ich ihn gehalten
            // habe: der Nutzer hat den alten Befund als Fehlalarm eingeordnet
            // ("die sind da faelschlich gelandet, das war ein false Flag
            // wegen einem anderen Bug"). Der oben beschriebene Grafikfehler
            // in andy3 kam von woanders. Zu andy1 ausdruecklich: "das wird
            // eher lustig, wenn auf einmal die alle spawnen."
            //
            // Was NICHT ueber diese Liste laeuft, sondern ueber fixedItems
            // (eine Typregel kann kein einzelnes Item schuetzen):
            //   rich3 53 und 54 - haengen am Rope und bleiben vorerst
            //   joby2 69        - wartet auf die Slot-Entkopplung (#111)
            const int ot = it.typeId;
            const bool ocbNurPose =
                   ot == 45 || ot == 47            // Larson/Pierre: Markerkennung
                || ot == 33 || ot == 35            // SWAT, SWAT+
                || ot == 37 || ot == 460           // Wachmann, Wachmann (Joby)
                || ot == 39                        // Two Gun
                || ot == 49 || ot == 51            // Mafia, Mafia2
                || ot == 55                        // Crane Guy
                || ot == 89;                       // Imp
            if (it.ocb != 0 && !ocbNurPose) continue;
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
            // Rudelgegner nehmen am Tausch NICHT teil (#98). Ein Tausch
            // aendert nur die Art eines vorhandenen Items - Begleiter liessen
            // sich nur unterbringen, indem man Cores bestehende Triggerkette
            // aufbohrt, und ein einzelner Compy funktioniert nicht (siehe
            // kHerden). Sie kommen ausschliesslich als Zusatzgegner ins Level,
            // dort gehoert uns der Trigger. Gefiltert wird schon bei der
            // Ziehung: sonst faenden Wuerfe reihenweise auf einen Typ, der
            // hinterher doch abgelehnt wird, und die Rolle bliebe vanilla.
            std::vector<int> tauschPool;
            for (int t : gegnerPool)
                if (herdenGroesse(t) == 1) tauschPool.push_back(t);

            if (opt.allsanity && !tauschPool.empty())
            {
                for (size_t i = 0; i < arten.size(); ++i)
                    arten[i] = tauschPool[static_cast<size_t>(rng.below(tauschPool.size()))];
            }
            else if (!crossGewaehlt.empty())
            {
                std::vector<int> crossTausch;
                for (std::map<int, const CrossGegner*>::const_iterator w = crossGewaehlt.begin();
                     w != crossGewaehlt.end(); ++w)
                    if (herdenGroesse(w->first) == 1) crossTausch.push_back(w->first);

                for (size_t i = 0; i < arten.size() && !crossTausch.empty(); ++i)
                {
                    if (rng.below(4) != 0) continue;
                    arten[i] = crossTausch[static_cast<size_t>(rng.below(crossTausch.size()))];
                }
            }

            for (size_t i = 0; i < indizes.size(); ++i)
            {
                std::map<int, LevelItem>::iterator f = finalOf.find(indizes[i]);
                if (f == finalOf.end() || f->second.typeId == arten[i]) continue;

                // Grosse Gegner brauchen freien Platz um sich (siehe
                // kRaumbedarf). Passt der ausgewuerfelte Typ hier nicht, bleibt
                // das Item auf seinem alten Typ stehen - ein Gegner weniger im
                // Mix ist besser als eine Hydra, die in ihrer Kammer festsitzt.
                if (!genugPlatz(arten[i], f->second.room,
                                f->second.sectorX, f->second.sectorZ, f->second.y))
                {
                    report.notes.push_back(fmt("Gegner Item %d: typeId %d braucht mehr freien Platz, als es in Raum %d hat - bleibt typeId %d.",
                                               indizes[i], arten[i], f->second.room, f->second.typeId));
                    continue;
                }

                JsonValue e = JsonValue::makeObject();
                e.set("index", JsonValue::makeInt(indizes[i]));
                e.set("typeId", JsonValue::makeInt(arten[i]));

                // Die OCB gehoerte dem alten Typ (nur Larson/Pierre kommen
                // ueberhaupt mit einer hierher, siehe oben). Der neue Typ
                // wuerde sie als seine eigene lesen - deshalb weg damit.
                // Fuer Typen, deren OCB ein VERHALTEN waehlt, steht der
                // richtige Wert in kSpawnOcb (Roman God: Waffenart).
                const int wunschOcb = spawnOcbFuer(arten[i]);
                if (f->second.ocb != wunschOcb)
                {
                    e.set("ocb", JsonValue::makeInt(wunschOcb));
                    report.notes.push_back(fmt("Item %d: OCB %d -> %d beim Typwechsel auf typeId %d.",
                                               indizes[i], f->second.ocb, wunschOcb, arten[i]));
                    f->second.ocb = wunschOcb;
                }

                // Antitrigger-Immunitaet, falls noetig - Begruendung beim
                // Nachzuegler-Durchgang weiter unten.
                if (in.antitriggered.count(indizes[i]))
                {
                    e.set("antitriggerImmun", JsonValue::makeBool(true));
                    immunGesetzt.insert(indizes[i]);
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

    // --- Antitrigger-Immunitaet fuer JEDEN betroffenen Gegner ---
    //
    // Der Item-Verlust ist der eigentliche Grund, und er haengt NICHT am
    // Typentausch: GetCarriedItems (setup.cpp:1962) haengt beim Levelstart
    // jedes Pickup im Umkreis von 512 Einheiten an den naechsten Gegner -
    // RemoveDrawnItem, room_number = NO_ROOM, das Item ist danach nur noch
    // dessen Fracht. Stirbt er normal, gibt CreatureDie sie ueber
    // DropBaddyPickups wieder frei (box.cpp:880). Der Antitrigger dagegen
    // ruft direkt KillItem (control.cpp:2422) - Gegner UND Fracht sind weg.
    //
    // In Vanilla ist das folgenlos, weil Core neben solche Gegner nichts
    // legt. Sobald die Items wandern, kann dort aber ein Schluesselitem
    // liegen, und dann ist das Level nach einem Schritt auf den
    // Antitrigger-Sektor unloesbar - ohne jede Meldung.
    //
    // Deshalb bekommt jeder betroffene Gegner das Immunitaetsbit, auch der
    // ungetauschte. Wirkung wie "den Zeiger aus dem Antitrigger nehmen", nur
    // am Item statt an der Floordata: der Antitrigger fuehrt ihn weiter auf,
    // ueberspringt ihn aber (control.cpp:2378), und alle anderen Zeiger
    // desselben Triggers arbeiten unveraendert weiter.
    //
    // Nur wenn Items ueberhaupt wandern: bei reiner Gegner-Randomisierung
    // gibt es nichts zu verlieren, und ein Vanilla-Lauf soll vanilla bleiben.
    if (opt.shuffleKeyItems || opt.shuffleOtherPickups)
    {
        std::set<int> gegnerarten(in.enemyTypes.begin(), in.enemyTypes.end());
        for (std::map<int, const CrossGegner*>::const_iterator c = crossGewaehlt.begin();
             c != crossGewaehlt.end(); ++c)
            gegnerarten.insert(c->first);

        int nachgezogen = 0;
        for (int idx : in.antitriggered)
        {
            if (immunGesetzt.count(idx)) continue;
            std::map<int, LevelItem>::const_iterator f = finalOf.find(idx);
            // Nur Gegner: ein Antitrigger auf eine Tuer oder eine Flipmap
            // muss weiter feuern, sonst steht das Level still.
            if (f == finalOf.end() || !gegnerarten.count(f->second.typeId)) continue;

            JsonValue e = JsonValue::makeObject();
            e.set("index", JsonValue::makeInt(idx));
            e.set("antitriggerImmun", JsonValue::makeBool(true));
            edits.push(e);
            immunGesetzt.insert(idx);
            ++nachgezogen;
        }
        if (!immunGesetzt.empty())
            report.notes.push_back(fmt("Antitrigger: %d Gegner immun gesetzt (%d davon ungetauscht) - "
                                       "schuetzt getragene Items vor dem Verschwinden.",
                                       static_cast<int>(immunGesetzt.size()), nachgezogen));
    }

    // Kuratierte Antitrigger-Immunitaeten: gelten IMMER, unabhaengig davon, ob
    // Items wandern - sie beschreiben Levellogik, nicht eine Nebenwirkung der
    // Randomisierung. rich2s Tuer 171 ist der Fall (siehe Kurationstabelle).
    for (int idx : in.antitriggerImmunItems)
    {
        if (immunGesetzt.count(idx)) continue;
        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(idx));
        e.set("antitriggerImmun", JsonValue::makeBool(true));
        edits.push(e);
        immunGesetzt.insert(idx);
        report.notes.push_back(fmt("Item %d: kuratierte Antitrigger-Immunitaet gesetzt.", idx));
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

            // Erst die Typen aussieben, die hier genug Platz finden (siehe
            // kRaumbedarf), dann daraus wuerfeln. Andersherum - wuerfeln und
            // bei Nichtpassen verwerfen - wuerde der Hydra jedes Mal einen
            // Platz stehlen, den ein anderer Typ haette nutzen koennen.
            std::vector<int> passend;
            for (int t : gegnerPool)
            {
                if (!genugPlatz(t, p.s->room, p.s->sectorX, p.s->sectorZ, p.s->floorY)) continue;
                // Ein Rudel muss KOMPLETT ins Item-Budget passen (#98) -
                // lieber gar keine Compys als drei ohne den vierten.
                if (herdenGroesse(t) > wieViele - gesetzt) continue;
                passend.push_back(t);
            }
            if (passend.empty()) continue;      // hier ist es fuer den ganzen Pool zu eng

            const int typeId = passend[static_cast<size_t>(rng.below(passend.size()))];
            const int herde = herdenGroesse(typeId);
            gesetzt += herde;
            if (crossGewaehlt.count(typeId)) crossBenutzt.insert(typeId);
            const RoomOrigin& ro = origin[p.s->room];

            // Der Index, den das neue Item bekommt: itemAdds haengt hinten an,
            // in genau dieser Reihenfolge.
            const int neuerIndex = static_cast<int>(in.items.size()) + static_cast<int>(adds.size());

            // Auch der Gegner bekommt die echte Bodenhoehe - er wuerde zwar
            // von der Engine auf den Boden gesetzt, soll aber nicht einen
            // halben Sektor tief einfallen. Belaufbarkeit ist fuer ihn keine
            // Bedingung, deshalb nur die Hoehe.
            int gyBoden = 0;
            Rando::bodenAn(in, *p.s, 512, 512, gyBoden);

            // Ein Rudel steht auf EINEM Sektor - wie Andrea2 seine drei
            // Hydra-Glieder. Die Tiere verteilen sich zur Laufzeit selbst:
            // ihre Formation haengt an der Item-Nummer, und die vergeben wir
            // hier fortlaufend. Ein Trigger weckt alle.
            JsonValue cmdList = JsonValue::makeArray();
            for (int k = 0; k < herde; ++k)
            {
                JsonValue a = JsonValue::makeObject();
                a.set("typeId", JsonValue::makeInt(typeId));
                a.set("room", JsonValue::makeInt(p.s->room));
                a.set("x", JsonValue::makeInt(static_cast<int64_t>(ro.originX) + p.s->sectorX * 1024 + 512));
                a.set("y", JsonValue::makeInt(gyBoden));
                a.set("z", JsonValue::makeInt(static_cast<int64_t>(ro.originZ) + p.s->sectorZ * 1024 + 512));
                // OCB 0 als Vorgabe: bei Larson setzt eine OCB != 0
                // ai_bits = GUARD, und dann sucht er AI-Objekte, die an
                // einer neuen Stelle nicht stehen (pierre.cpp:84).
                // Ausnahme sind Typen, deren OCB ein VERHALTEN waehlt statt
                // einer Umgebung - die stehen mit ihrem Wert in kSpawnOcb
                // (Roman God: Waffenart, siehe dort).
                a.set("ocb", JsonValue::makeInt(spawnOcbFuer(typeId)));
                a.set("angle", JsonValue::makeInt(static_cast<int64_t>(rng.below(4)) * 16384 - 32768));
                adds.push(a);

                JsonValue cmd = JsonValue::makeObject();
                cmd.set("type", JsonValue::makeInt(0));          // TO_OBJECT
                cmd.set("param", JsonValue::makeInt(neuerIndex + k));
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
            trigAdds.push(t);

            used.insert(secKey(p.s->room, p.s->sectorX, p.s->sectorZ));
            used.insert(trigger);
            report.enemiesAdded += herde;
            if (herde > 1)
                report.notes.push_back(fmt("Rudel: %d x typeId %d als Items %d-%d auf %d,%d,%d - ein Trigger auf %d,%d,%d",
                                           herde, typeId, neuerIndex, neuerIndex + herde - 1,
                                           p.s->room, p.s->sectorX, p.s->sectorZ, tr, tx, tz));
            else
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

    // ZUERST die Anim-Basistypen der Soldatenfamilie, VOR den Typen, die sie
    // brauchen (#96). Standen sie hinten, brach der Import ab, sobald der
    // Basistyp wirklich nachgeladen werden musste statt schon dazusein:
    // "Hauptblock laesst sich nicht durchlaufen - Mesh-Zeiger". Aufgefallen
    // ist es erst mit dem Fenster "Gegnerarten", weil man den Basistyp jetzt
    // gezielt abwaehlen kann - vorher zog der Seed ihn fast immer selbst mit,
    // und dann stand er schon da.
    //
    // Wer selbst im Lauf steckt, wird hier uebersprungen: er kommt unten mit
    // replace=true und damit vollstaendiger vom Spender.
    std::set<int> basisEmittiert;
    for (int t : crossBenutzt)
    {
        const CrossGegner* c = crossGewaehlt[t];
        if (c->basisSlot <= 0) continue;
        if (crossBenutzt.count(c->basisSlot)) continue;
        if (!basisEmittiert.insert(c->basisSlot).second) continue;

        // Der Basistyp kommt aus SEINER EIGENEN Zeile, nicht aus dem
        // Spenderlevel dessen, der ihn anfordert. Vorher stand hier c->von -
        // beim Crane Guy holte das den Guard aus joby2, und der laesst sich
        // nicht kopieren ("Frame-Zeiger ausserhalb der Frame-Scheibe"); aus
        // rich1 geht er. Bei Mafia und SWAT+ fiel es nie auf, weil deren
        // Spender zufaellig einen brauchbaren Basistyp fuehren.
        const char* basisVon = c->von;
        for (const CrossGegner& b : kCross)
            if (b.typeId == c->basisSlot) { basisVon = b.von; break; }

        JsonValue g = JsonValue::makeObject();
        g.set("slot", JsonValue::makeInt(c->basisSlot));
        g.set("fromLevel", JsonValue::makeString(basisVon));
        // replace=false laesst einen nativen Basistyp stehen - das Level soll
        // sein eigenes Modell behalten, wenn es eins hat.
        g.set("replace", JsonValue::makeBool(false));
        moveables.push(g);
        report.notes.push_back(fmt("Anim-Basistyp %d aus %s fuer typeId %d nachgeladen.",
                                   c->basisSlot, basisVon, t));
    }

    for (int t : crossBenutzt)
    {
        const CrossGegner* c = crossGewaehlt[t];

        // Gast aus Tomb Raider 3 (#92): kein Spenderlevel, sondern eine
        // Anweisung an den Patchweg, ihn zur Laufzeit aus der Installation
        // des Nutzers zu holen. Alles Naehere steht in Tr3Quelle::dinos() -
        // hier reicht der Slot, denn die Tabelle ist ueber ihn auffindbar.
        if (c->tr3)
        {
            JsonValue e = JsonValue::makeObject();
            e.set("slot", JsonValue::makeInt(t));
            e.set("ausTr3", JsonValue::makeString(c->tr3));
            moveables.push(e);
            report.notes.push_back(fmt("Gast aus Tomb Raider 3: Slot %d wird beim Patchen geholt.", t));
            continue;
        }

        JsonValue e = JsonValue::makeObject();
        // Engine-Slots jenseits 460 (#105): das Modell liegt beim Spender
        // unter quellSlot und wandert per toSlot in den neuen Slot.
        e.set("slot", JsonValue::makeInt(c->quellSlot > 0 ? c->quellSlot : t));
        if (c->quellSlot > 0)
            e.set("toSlot", JsonValue::makeInt(t));
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

        // Der Anim-Basistyp der Familie steht schon oben in der Liste - er
        // MUSS vor dem Typ kommen, der ihn braucht (#96).

        // Tauschhaelften: die MESHSWAP1 des Tauschspenders (swapVon, kann
        // vom Modellspender abweichen) in den Ausweichslot des Typs. Seit
        // #111 ist der je Gegner eindeutig; die Menge bleibt als Schutz
        // gegen doppelte Emission, falls ein Slot je wieder geteilt wird.
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

    // --- Keine Fluchtmarker: Larson und Pierre sollen kaempfen und sterben ---
    //
    // Beide fliehen ab 40 Lebenspunkten zu einem AI_AMBUSH-Marker mit
    // passender OCB und despawnen dort (GetAITarget/FindAITargetObject,
    // box.cpp; PierreControl klemmt die Lebenspunkte solange bei 40 fest).
    // Der Randomizer hat solche Marker eine Zeit lang selbst gesetzt, damit
    // die Flucht ihr Ziel findet - das war die falsche Richtung:
    //
    //  1. Nutzervorgabe: als Pool-Gegner sollen die beiden IMMER kaempfen
    //     und sterben. Genau dafuer sind Larsons Ausblenden (#65) und
    //     Pierres schleifenfeste Todesanimation ueberhaupt gebaut worden.
    //  2. Der Despawn nimmt getragene Items mit. GetCarriedItems
    //     (setup.cpp:1962) haengt jedes Pickup im Umkreis von 512 Einheiten
    //     an den naechsten Gegner - ein Schluesselitem neben einem
    //     fliehenden Larson waere nach seiner Flucht fort.
    //
    // Ohne Marker greift PierreKannFliehen() nicht, die 40-LP-Klammer
    // entfaellt, und beide sterben wie jeder andere Gegner. Cores eigene
    // Marker in Andrea1 und Andrea2 bleiben unangetastet - dort behalten
    // die Vanilla-Exemplare mit ihrer OCB ihr Originalverhalten; ein
    // getauschter Gegner verliert seine OCB (siehe Tauschschleife) und
    // findet damit ohnehin keinen Marker mehr.
    //
    // Tr5Level::addAiObject und das Patchfeld "aiAdds" bleiben bestehen -
    // die Mechanik ist geprueft und wird gebraucht, sobald wir AI-Objekte
    // fuer etwas anderes setzen wollen.
    JsonValue aiAdds = JsonValue::makeArray();

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
    // Die Itemzahl, von der aus die Planung neue Items nummeriert hat (#107).
    // Traegt die Quelldatei MEHR Items als das Level-JSON (die Waffen-Basis
    // entstand NACH dem JSON-Export), landen die itemAdds weiter hinten als
    // geplant - und jeder Trigger weckte das Item ZWEI VOR seinem Gegner
    // (in Andrea1 gesehen: JSON 151, Datei 153, alle Wecker um 2 verrutscht,
    // die letzten beiden Gegner ohne Trigger). ApplyPatch verschiebt mit
    // dieser Zahl alle TO_OBJECT-Ziele >= itemBase um die echte Differenz.
    patchOut.set("itemBase", JsonValue::makeInt(static_cast<int64_t>(in.items.size())));
    // Bewusst KEIN itemRemoves - siehe Regel 1 im Header.
    return true;
}

// Die waehlbaren Gegnerarten mit Anzeigenamen (#95). Reihenfolge und Inhalt
// folgen der Pool-Tabelle kCross weiter oben - wer dort eine Zeile ergaenzt,
// ergaenzt sie hier. Die Namen sind die aus der Nutzerliste "Gegner Pool.txt",
// nicht die Enum-Bezeichner der Engine.
std::vector<Gegnerart> Rando::gegnerarten()
{
    return {
        { 33, "SWAT",              nullptr },
        { 35, "SWAT (schwer)",     nullptr },
        { 37, "Wachmann",          nullptr },
        // Engine-Slot 460 (BLUE_GUARD2, #105): das Joby-Modell des Wachmanns.
        // Vanilla teilen sich beide Modelle Slot 37 und schliessen sich aus -
        // der neue Slot macht sie gleichzeitig moeglich. Nur auf unserer
        // Engine lauffaehig (wird per #31 mitgeliefert).
        { 460, "Wachmann (Joby)",  nullptr },
        { 39, "Two Gun",           nullptr },
        { 41, "Hund",              nullptr },
        { 45, "Larson",            nullptr },
        { 47, "Pierre",            nullptr },
        { 49, "Mafia (MP)",        nullptr },
        { 51, "Mafia (Pistolen)",  nullptr },
        { 55, "Crane Guy",         nullptr },
        { 57, "Loewe",             nullptr },
        { 59, "Gladiator",         nullptr },
        { 61, "Soldatenstatue (Schwert)", nullptr },
        // Engine-Slot 461 (ROMAN_GOD2, #84): die Hammer-Fassung aus Andrea3.
        // Vanilla teilt sie sich Slot 61 mit dem Schwert und schliesst es aus;
        // mit dem eigenen Slot koennen beide im selben Level stehen.
        { 461, "Soldatenstatue (Hammer)", nullptr },
        { 63, "Hydra",             nullptr },
        { 77, "Labyrinth-Monster", nullptr },
        // Der Hund aus joby2 - eigener Gegnertyp mit eigener Steuerung,
        // nicht zu verwechseln mit "Hund" (41) aus Andrea1.
        { 85, "Huskie",            nullptr },
        { 89, "Imp",               nullptr },

        // Die Gaeste aus Tomb Raider 3. Sie sitzen auf Slots, die in KEINEM
        // der 16 TR5-Level ein Modell tragen - deshalb nehmen sie keinem
        // einheimischen Gegner etwas weg (an allen nachgemessen).
        { 31, "T-Rex (TR3)",       "tr3" },
        { 75, "Raptor (TR3)",      "tr3" },
        { 53, "Compy (TR3)",       "tr3" },
        { 43, "Geier (TR3 LA)",    "tr3gold" },
    };
}

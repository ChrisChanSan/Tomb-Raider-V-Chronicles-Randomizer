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
#include "SecretListe.h"

#include "Json.h"
#include "Tr5Level.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

//----------------------------------------------------------------------------
// C++-Fassung des Listenlesers aus SecretPlan.ps1. Die Grammatik ist dieselbe;
// die Kommentare zu den Fallen stehen dort und im Listenkopf und werden hier
// nicht wiederholt. Zwei Abweichungen mit Absicht:
//
//   1. Positionen kommen aus der GELADENEN Leveldatei (Raumurspruenge,
//      Itemzahl, LikeItem-Bezuege), nicht aus dem Level-Info-JSON. Der Lauf
//      arbeitet damit gegen den Stand NACH dem Rando-Patch - Neu<k> zeigt
//      exakt auf die Items, die dieser Patch anlegen wird.
//   2. Der Winkel wird hier von Grad in die rohe TR-Einheit umgerechnet
//      (65536 = volle Drehung) - die Falle, die in SecretPlan.ps1 schon
//      einmal zuschlug (@Angle90 als 90 roh = halbe Gradminute).
//----------------------------------------------------------------------------

namespace
{
    // Eine Extra-Option, alle vier Formen in einem Topf.
    struct Option
    {
        enum Art { ITEM, TRIGGER, ERSETZ, PORTAL } art = ITEM;
        int typeId = -1, index = -1;
        int room = -1, sx = -1, sz = -1;
        int roomB = -1, sxB = -1, szB = -1;          // nur PORTAL
        bool hatOcb = false, hatAngle = false, hatHoehe = false;
        bool hatXOff = false, hatZOff = false;
        int ocb = 0, angleGrad = 0, hoehe = 0, xOff = 0, zOff = 0;
        int wieItem = -1;                            // @LikeItem, -1 = keins
        std::string triggerArt;                      // leer = kein Trigger
        std::vector<std::string> ziele;              // Zahl oder "Neu<k>"
    };

    struct Stelle { int room = 0, sx = 0, sz = 0; };

    struct Zeile
    {
        std::string level;
        int anzahl = 0, typ = 0;
        std::vector<int> ids;
        std::vector<Stelle> stellen;
        std::vector<Option> extras;
    };

    std::string klein(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    int gradInRoh(int grad)
    {
        int roh = static_cast<int>(std::lround(grad * 65536.0 / 360.0)) % 65536;
        if (roh > 32767)  roh -= 65536;
        if (roh < -32768) roh += 65536;
        return roh;
    }

    // Gruppen auf Klammertiefe 0 trennen; ';' bevorzugt, sonst '+'.
    std::vector<std::string> gruppenTrennen(const std::string& text)
    {
        const char trenner = (text.find(';') != std::string::npos) ? ';' : '+';
        std::vector<std::string> teile;
        std::string akt;
        int tiefe = 0;
        for (char c : text)
        {
            if (c == '(') ++tiefe;
            else if (c == ')') --tiefe;
            if (c == trenner && tiefe == 0) { teile.push_back(akt); akt.clear(); }
            else akt += c;
        }
        teile.push_back(akt);
        std::vector<std::string> raus;
        for (std::string& t : teile)
        {
            const size_t a = t.find_first_not_of(" \t");
            const size_t b = t.find_last_not_of(" \t");
            if (a != std::string::npos) raus.push_back(t.substr(a, b - a + 1));
        }
        return raus;
    }

    bool liesOption(const std::string& roh, Option& e, std::string& unbekannt)
    {
        static const std::regex reErsetz(R"(replace\s+Item_Level_ID\(\s*(\d+)\s*\)\s*with\s*->\s*Type_?ID\(\s*(\d+)\s*\))");
        static const std::regex reItem(R"(add\s+Type_ID\(\s*(\d+)\s*\))");
        static const std::regex reRoom(R"(@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");
        static const std::regex reTrigForm(R"(add\s+Trigger_Type\()");
        static const std::regex rePortal(R"(Make_Portal\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)\s*->\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");
        static const std::regex reOcb(R"(@\s*OCB\(\s*(\d+)\s*\))");
        static const std::regex reAngle(R"(@\s*Angle\s*\(?\s*(-?\d+))");
        static const std::regex reHoehe(R"(@\s*Height\(\s*(-?\d+)\s*\))");
        static const std::regex reXOff(R"(@\s*XOff\(\s*(-?\d+)\s*\))");
        static const std::regex reZOff(R"(@\s*ZOff\(\s*(-?\d+)\s*\))");
        static const std::regex reLike(R"(@\s*LikeItem\(\s*(\d+)\s*\))");
        static const std::regex reTrig(R"(Trigger_Type\(\s*(\w+)\s*\)[\s\S]*?Item_Level_ID\(\s*([^)]*)\))");

        std::smatch m;
        if (std::regex_search(roh, m, rePortal))
        {
            e.art = Option::PORTAL;
            e.room = std::stoi(m[1]); e.sx = std::stoi(m[2]); e.sz = std::stoi(m[3]);
            e.roomB = std::stoi(m[4]); e.sxB = std::stoi(m[5]); e.szB = std::stoi(m[6]);
        }
        else if (std::regex_search(roh, m, reErsetz))
        {
            e.art = Option::ERSETZ;
            e.index = std::stoi(m[1]);
            e.typeId = std::stoi(m[2]);
        }
        else if (std::regex_search(roh, m, reItem))
        {
            e.art = Option::ITEM;
            e.typeId = std::stoi(m[1]);
            std::smatch r;
            if (std::regex_search(roh, r, reRoom))
            { e.room = std::stoi(r[1]); e.sx = std::stoi(r[2]); e.sz = std::stoi(r[3]); }
            else if (!std::regex_search(roh, r, reLike))
                return false;                        // weder Raum noch Bezugsitem
        }
        else if (std::regex_search(roh, m, reTrigForm))
        {
            e.art = Option::TRIGGER;
            std::smatch r;
            if (!std::regex_search(roh, r, reRoom)) return false;
            e.room = std::stoi(r[1]); e.sx = std::stoi(r[2]); e.sz = std::stoi(r[3]);
        }
        else return false;

        if (std::regex_search(roh, m, reOcb))   { e.hatOcb = true;   e.ocb = std::stoi(m[1]); }
        if (std::regex_search(roh, m, reAngle)) { e.hatAngle = true; e.angleGrad = std::stoi(m[1]); }
        if (std::regex_search(roh, m, reHoehe)) { e.hatHoehe = true; e.hoehe = std::stoi(m[1]); }
        if (std::regex_search(roh, m, reXOff))  { e.hatXOff = true;  e.xOff = std::stoi(m[1]); }
        if (std::regex_search(roh, m, reZOff))  { e.hatZOff = true;  e.zOff = std::stoi(m[1]); }
        if (std::regex_search(roh, m, reLike))    e.wieItem = std::stoi(m[1]);
        if (e.art != Option::ERSETZ && e.art != Option::PORTAL &&
            std::regex_search(roh, m, reTrig))
        {
            e.triggerArt = m[1];
            std::string liste = m[2];
            size_t a = 0;
            while (a <= liste.size())
            {
                size_t b = liste.find(',', a);
                if (b == std::string::npos) b = liste.size();
                std::string t = liste.substr(a, b - a);
                const size_t x = t.find_first_not_of(" \t");
                const size_t y = t.find_last_not_of(" \t");
                if (x != std::string::npos) e.ziele.push_back(t.substr(x, y - x + 1));
                a = b + 1;
            }
        }
        if (e.art == Option::TRIGGER && e.ziele.empty()) return false;

        // Strenge statt Nachsicht: alles Erkannte austragen, was uebrig
        // bleibt, ist ein Tippfehler und wird gemeldet.
        std::string rest = roh;
        auto tilge = [&rest](const std::regex& re) { rest = std::regex_replace(rest, re, ""); };
        tilge(rePortal);
        static const std::regex reW1(R"(replace\s+)"), reW2(R"(with\s*->)"), reW3(R"(add\s+)");
        static const std::regex reW4(R"(Type_?ID\(\s*\d+\s*\))");
        static const std::regex reW5(R"(Trigger_Type\(\s*\w+\s*\))");
        static const std::regex reW6(R"(to\s*->\s*\w+)");
        static const std::regex reW7(R"(Item_Level_ID\([^)]*\))");
        static const std::regex reW8(R"([()+;\s])");
        tilge(reW1); tilge(reW2); tilge(reW3); tilge(reW4);
        tilge(reRoom); tilge(reOcb);
        static const std::regex reAngleVoll(R"(@\s*Angle\s*\(?\s*-?\d+\s*\)?)");
        tilge(reAngleVoll);
        tilge(reHoehe); tilge(reXOff); tilge(reZOff); tilge(reLike);
        tilge(reW5); tilge(reW6); tilge(reW7); tilge(reW8);
        unbekannt = rest;
        return true;
    }

    bool liesListe(const std::wstring& pfad, std::vector<Zeile>& raus,
                   std::vector<std::string>& warnungen, std::string& error)
    {
        const std::filesystem::path p(pfad);
        std::ifstream f(p);
        if (!f) { error = "Secret-Liste nicht lesbar"; return false; }

        static const std::regex reZeile(R"(^\s*(\w+)\.trc\s+(\d+)\s+(\d+)\s+\(([^)]*)\)\s*(.*)$)");
        static const std::regex reStelle(R"(Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");

        std::string z;
        while (std::getline(f, z))
        {
            if (!z.empty() && z.back() == '\r') z.pop_back();
            const size_t h = z.find_first_not_of(" \t");
            if (h == std::string::npos || z[h] == '#') continue;

            std::smatch m;
            if (!std::regex_match(z, m, reZeile)) continue;

            Zeile row;
            row.level = m[1];
            row.anzahl = std::stoi(m[2]);
            row.typ = std::stoi(m[3]);
            {
                std::string ids = m[4];
                size_t a = 0;
                while (a <= ids.size())
                {
                    size_t b = ids.find(',', a);
                    if (b == std::string::npos) b = ids.size();
                    const std::string t = ids.substr(a, b - a);
                    if (t.find_first_not_of(" \t") != std::string::npos)
                        row.ids.push_back(std::atoi(t.c_str()));
                    a = b + 1;
                }
            }
            const std::string rest = m[5];

            // Grenze zwischen Stellen und Extras: Room(x,z)-Klammern
            // ausblenden, die erste uebrige Klammer beginnt die Extras -
            // egal mit welchem Verb (die "(add"-Falle aus SecretPlan.ps1).
            std::string maske = rest;
            for (std::sregex_iterator it(rest.begin(), rest.end(), reStelle), e; it != e; ++it)
                for (size_t i = 0; i < it->length(); ++i)
                    maske[static_cast<size_t>(it->position()) + i] = ' ';
            const size_t pExtra = maske.find('(');
            const std::string vorn = (pExtra != std::string::npos) ? rest.substr(0, pExtra) : rest;

            for (std::sregex_iterator it(vorn.begin(), vorn.end(), reStelle), e; it != e; ++it)
            {
                Stelle s;
                s.room = std::stoi((*it)[1]); s.sx = std::stoi((*it)[2]); s.sz = std::stoi((*it)[3]);
                row.stellen.push_back(s);
            }

            if (static_cast<int>(row.ids.size()) != row.anzahl)
                warnungen.push_back(row.level + ": Anzahl " + std::to_string(row.anzahl) +
                                    ", aber " + std::to_string(row.ids.size()) + " Level_ID");
            if (!row.stellen.empty() && row.stellen.size() != row.ids.size())
                warnungen.push_back(row.level + ": " + std::to_string(row.ids.size()) +
                                    " Secrets, aber " + std::to_string(row.stellen.size()) + " neue Stellen");

            if (pExtra != std::string::npos)
                for (const std::string& g : gruppenTrennen(rest.substr(pExtra)))
                {
                    Option o;
                    std::string unbekannt;
                    if (!liesOption(g, o, unbekannt))
                    { warnungen.push_back(row.level + ": Option nicht lesbar -> " + g); continue; }
                    if (!unbekannt.empty())
                        warnungen.push_back(row.level + ": nicht verstandener Teil in einer Option -> '" + unbekannt + "'");
                    row.extras.push_back(o);
                }

            raus.push_back(row);
        }
        if (raus.empty()) { error = "Secret-Liste enthaelt keine einzige Levelzeile"; return false; }
        return true;
    }
}

std::vector<SecretQuelle> secretQuellenSuchen(const std::wstring& ordner)
{
    std::vector<SecretQuelle> raus;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(ordner, ec))
    {
        if (!e.is_regular_file()) continue;
        const std::filesystem::path p = e.path();
        if (klein(p.extension().string()) != ".txt") continue;
        const std::string name = p.stem().string();
        if (name.empty() || name[0] == '_') continue;
        const size_t u = name.find_last_of('_');
        if (u == std::string::npos) continue;
        const std::string kat = name.substr(u + 1);
        if (klein(kat) != "glitchless" && klein(kat) != "glitched") continue;
        SecretQuelle q;
        q.autor = name.substr(0, u);
        q.kategorie = (klein(kat) == "glitchless") ? "Glitchless" : "Glitched";
        q.pfad = p.wstring();
        raus.push_back(q);
    }
    return raus;
}

bool secretPatchBauen(const std::wstring& listenPfad, const std::string& levelName,
                      const Tr5Level& lvl, JsonValue& patchOut,
                      std::vector<std::string>& warnungen, std::string& error)
{
    std::vector<Zeile> zeilen;
    if (!liesListe(listenPfad, zeilen, warnungen, error)) return false;

    const Zeile* row = nullptr;
    for (const Zeile& z : zeilen)
        if (klein(z.level) == klein(levelName)) { row = &z; break; }
    if (!row)
    {
        // Kein Eintrag heisst: dieses Level bleibt vanilla. Das ist der
        // Normalfall fuer del und richcut2 (Bonuslevel ohne Secrets) und
        // erlaubt, dass eine Autorenliste nur einen Teil der Level abdeckt.
        warnungen.push_back(levelName + ": kein Eintrag in der Secret-Liste - bleibt vanilla.");
        patchOut = JsonValue::makeObject();
        return true;
    }

    JsonValue edits = JsonValue::makeArray();
    JsonValue adds = JsonValue::makeArray();
    JsonValue trigger = JsonValue::makeArray();
    JsonValue portale = JsonValue::makeArray();

    const std::vector<Tr5Room>& rooms = lvl.rooms();
    auto raumOk = [&](int r) { return r >= 0 && r < static_cast<int>(rooms.size()); };

    // Gegenprobe an der echten Datei: sind die genannten Indizes wirklich
    // Secrets? Ein Tippfehler verschoebe sonst still ein fremdes Item.
    for (int id : row->ids)
    {
        Tr5Item it;
        if (!lvl.getItem(id, it) || it.objectNumber != row->typ)
            warnungen.push_back(levelName + ": Item " + std::to_string(id) +
                                " ist in der Datei kein Typ " + std::to_string(row->typ));
    }

    // Die Secrets an ihre neuen Stellen.
    for (size_t i = 0; i < row->stellen.size() && i < row->ids.size(); ++i)
    {
        const Stelle& s = row->stellen[i];
        if (!raumOk(s.room))
        { error = levelName + ": Raum " + std::to_string(s.room) + " gibt es nicht"; return false; }
        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(row->ids[i]));
        e.set("room", JsonValue::makeInt(s.room));
        e.set("x", JsonValue::makeInt(rooms[static_cast<size_t>(s.room)].originX + s.sx * 1024 + 512));
        e.set("z", JsonValue::makeInt(rooms[static_cast<size_t>(s.room)].originZ + s.sz * 1024 + 512));
        e.set("y", JsonValue::makeInt(0));
        e.set("yAuto", JsonValue::makeBool(true));
        edits.push(e);
    }

    // Extras. Neue Items haengen ab der ECHTEN Itemzahl hinten an.
    const int itemBasis = lvl.itemCount();
    int addZahl = 0;
    for (const Option& o : row->extras) if (o.art == Option::ITEM) ++addZahl;

    auto zielBefehle = [&](const std::vector<std::string>& ziele) -> JsonValue
    {
        JsonValue c = JsonValue::makeArray();
        for (const std::string& z : ziele)
        {
            int idx = -1;
            if (!z.empty() && std::isdigit(static_cast<unsigned char>(z[0])))
                idx = std::atoi(z.c_str());
            else if (z.rfind("Neu", 0) == 0)
            {
                const int k = std::atoi(z.c_str() + 3);
                if (k >= 1 && k <= addZahl) idx = itemBasis + k - 1;
                else warnungen.push_back(levelName + ": " + z + " gibt es nicht - die Zeile legt " +
                                         std::to_string(addZahl) + " Items an.");
            }
            else warnungen.push_back(levelName + ": Triggerziel '" + z + "' nicht verstanden - uebersprungen.");
            if (idx >= 0)
            {
                JsonValue cmd = JsonValue::makeObject();
                cmd.set("type", JsonValue::makeInt(0));
                cmd.set("param", JsonValue::makeInt(idx));
                c.push(cmd);
            }
        }
        return c;
    };
    auto triggerArtNr = [&](const std::string& art) -> int
    {
        const std::string a = klein(art);
        if (a == "key")     return 3;
        if (a == "switch")  return 2;
        if (a == "pickup")  return 4;
        if (a == "trigger") return 0;
        warnungen.push_back(levelName + ": unbekannte Triggerart '" + art + "' - als einfacher Trigger behandelt.");
        return 0;
    };

    int n = 0;
    for (const Option& o : row->extras)
    {
        if (o.art == Option::PORTAL)
        {
            JsonValue p = JsonValue::makeObject();
            p.set("roomA", JsonValue::makeInt(o.room));
            p.set("sxA", JsonValue::makeInt(o.sx));
            p.set("szA", JsonValue::makeInt(o.sz));
            p.set("roomB", JsonValue::makeInt(o.roomB));
            p.set("sxB", JsonValue::makeInt(o.sxB));
            p.set("szB", JsonValue::makeInt(o.szB));
            portale.push(p);
            continue;
        }

        if (o.art == Option::ERSETZ)
        {
            JsonValue e = JsonValue::makeObject();
            e.set("index", JsonValue::makeInt(o.index));
            e.set("typeId", JsonValue::makeInt(o.typeId));
            if (o.hatAngle) e.set("angle", JsonValue::makeInt(gradInRoh(o.angleGrad)));
            if (o.hatOcb)   e.set("ocb", JsonValue::makeInt(o.ocb));
            if (o.hatHoehe) { e.set("yOff", JsonValue::makeInt(-o.hoehe)); e.set("yAuto", JsonValue::makeBool(true)); }
            if (o.hatXOff)  e.set("xOff", JsonValue::makeInt(o.xOff));
            if (o.hatZOff)  e.set("zOff", JsonValue::makeInt(o.zOff));
            edits.push(e);
            continue;
        }

        if (o.art == Option::TRIGGER)
        {
            JsonValue t = JsonValue::makeObject();
            t.set("room", JsonValue::makeInt(o.room));
            t.set("sectorX", JsonValue::makeInt(o.sx));
            t.set("sectorZ", JsonValue::makeInt(o.sz));
            t.set("art", JsonValue::makeInt(triggerArtNr(o.triggerArt)));
            t.set("timer", JsonValue::makeInt(0));
            t.set("oneShot", JsonValue::makeInt(1));
            t.set("mask", JsonValue::makeInt(31));
            t.set("commands", zielBefehle(o.ziele));
            trigger.push(t);
            continue;
        }

        // Item anhaengen.
        JsonValue e = JsonValue::makeObject();
        e.set("typeId", JsonValue::makeInt(o.typeId));
        if (o.wieItem >= 0)
        {
            Tr5Item bez;
            if (!lvl.getItem(o.wieItem, bez))
            { error = levelName + ": LikeItem(" + std::to_string(o.wieItem) + ") gibt es nicht"; return false; }
            e.set("room", JsonValue::makeInt(bez.roomNumber));
            e.set("x", JsonValue::makeInt(bez.x));
            e.set("z", JsonValue::makeInt(bez.z));
            e.set("y", JsonValue::makeInt(bez.y));
            e.set("yAuto", JsonValue::makeBool(false));
        }
        else
        {
            if (!raumOk(o.room))
            { error = levelName + ": Raum " + std::to_string(o.room) + " gibt es nicht"; return false; }
            e.set("room", JsonValue::makeInt(o.room));
            e.set("x", JsonValue::makeInt(rooms[static_cast<size_t>(o.room)].originX + o.sx * 1024 + 512));
            e.set("z", JsonValue::makeInt(rooms[static_cast<size_t>(o.room)].originZ + o.sz * 1024 + 512));
            e.set("y", JsonValue::makeInt(0));
            e.set("yAuto", JsonValue::makeBool(true));
        }
        if (o.hatAngle) e.set("angle", JsonValue::makeInt(gradInRoh(o.angleGrad)));
        if (o.hatOcb)   e.set("ocb", JsonValue::makeInt(o.ocb));
        if (o.hatHoehe) e.set("yOff", JsonValue::makeInt(-o.hoehe));
        if (o.hatXOff)  e.set("xOff", JsonValue::makeInt(o.xOff));
        if (o.hatZOff)  e.set("zOff", JsonValue::makeInt(o.zOff));
        adds.push(e);
        const int meinIndex = itemBasis + n;
        ++n;

        if (!o.triggerArt.empty() && o.room < 0)
        {
            // Der Trigger klebt am Sektor - bei @LikeItem ohne @Room ist
            // keiner benannt. Bisher braucht das keine Zeile; wer es doch
            // will, gibt @Room zusaetzlich an.
            warnungen.push_back(levelName + ": Trigger auf einem LikeItem-Eintrag ohne @Room - uebersprungen.");
        }
        else if (!o.triggerArt.empty())
        {
            JsonValue t = JsonValue::makeObject();
            t.set("room", JsonValue::makeInt(o.room));
            t.set("sectorX", JsonValue::makeInt(o.sx));
            t.set("sectorZ", JsonValue::makeInt(o.sz));
            t.set("art", JsonValue::makeInt(triggerArtNr(o.triggerArt)));
            t.set("timer", JsonValue::makeInt(0));
            t.set("oneShot", JsonValue::makeInt(1));
            t.set("mask", JsonValue::makeInt(31));
            t.set("refItem", JsonValue::makeInt(meinIndex));
            t.set("commands", zielBefehle(o.ziele));
            trigger.push(t);
        }
    }

    patchOut = JsonValue::makeObject();
    if (edits.size() > 0)   patchOut.set("itemEdits", edits);
    if (adds.size() > 0)    patchOut.set("itemAdds", adds);
    if (trigger.size() > 0) patchOut.set("triggerAdds", trigger);
    if (portale.size() > 0) patchOut.set("portalAdds", portale);
    return true;
}

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
#include "SecretList.h"

#include "Json.h"
#include "Tr5Level.h"
#include "Floor.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

//----------------------------------------------------------------------------
// C++ version of the list reader from SecretPlan.ps1. The grammar is the
// same; the comments about the pitfalls live there and in the list header
// and are not repeated here. Two deliberate deviations:
//
//   1. Positions come from the LOADED level file (room origins, item count,
//      LikeItem references), not from the level info JSON. The run thereby
//      works against the state AFTER the rando patch - New<k> points at
//      exactly the items this patch will create.
//   2. The angle is converted here from degrees to the raw TR unit
//      (65536 = full turn) - the trap that already snapped once in
//      SecretPlan.ps1 (@Angle90 as raw 90 = half an arc minute).
//----------------------------------------------------------------------------

namespace
{
    // One extra option, all forms in one pot.
    struct Option
    {
        enum Kind { ITEM, TRIGGER, REPLACE, PORTAL, CLIMB, WATER, MOVEROOM } kind = ITEM;
        long moveX = 0, moveY = 0, moveZ = 0;         // MOVEROOM only
        bool waterOn = false;                        // WATER only
        int typeId = -1, index = -1;
        int room = -1, sx = -1, sz = -1;
        int roomB = -1, sxB = -1, szB = -1;          // PORTAL only
        // 0 = wall (Make_Portal), 1 = A opens at its CEILING into B,
        // 2 = A opens at its FLOOR into B. Told apart by NAME rather than
        // worked out from the geometry - the guess got andy1 wrong.
        int portalKind = 0;
        // #136: the wall is a drawn face and stays put unless it is dealt
        // with. @Clear takes it away, @LikeWall gives it another texture.
        bool portalClear = false;
        int  likeWallRoom = -1, likeWallSx = -1, likeWallSz = -1;
        bool hasOcb = false, hasAngle = false, hasHeight = false;
        bool hasXOff = false, hasZOff = false;
        int ocb = 0, angleDeg = 0, height = 0, xOff = 0, zOff = 0;
        int likeItem = -1;                           // @LikeItem, -1 = none
        std::string triggerKind;                     // empty = no trigger
        std::vector<std::string> targets;            // number or "New<k>"
    };

    struct Spot { int room = 0, sx = 0, sz = 0; };

    struct Row
    {
        std::string level;
        int count = 0, type = 0;
        std::vector<int> ids;
        std::vector<Spot> spots;
        std::vector<Option> extras;
    };

    std::string lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    int degToRaw(int deg)
    {
        int raw = static_cast<int>(std::lround(deg * 65536.0 / 360.0)) % 65536;
        if (raw > 32767)  raw -= 65536;
        if (raw < -32768) raw += 65536;
        return raw;
    }

    // Split groups at bracket depth 0; ';' preferred, otherwise '+'.
    std::vector<std::string> splitGroups(const std::string& text)
    {
        const char sep = (text.find(';') != std::string::npos) ? ';' : '+';
        std::vector<std::string> parts;
        std::string cur;
        int depth = 0;
        for (char c : text)
        {
            if (c == '(') ++depth;
            else if (c == ')') --depth;
            if (c == sep && depth == 0) { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        parts.push_back(cur);
        std::vector<std::string> out;
        for (std::string& t : parts)
        {
            const size_t a = t.find_first_not_of(" \t");
            const size_t b = t.find_last_not_of(" \t");
            if (a != std::string::npos) out.push_back(t.substr(a, b - a + 1));
        }
        return out;
    }

    bool readOption(const std::string& raw, Option& e, std::string& unknown)
    {
        static const std::regex reReplace(R"(replace\s+Item_Level_ID\(\s*(\d+)\s*\)\s*with\s*->\s*Type_?ID\(\s*(\d+)\s*\))");
        static const std::regex reItem(R"(add\s+Type_ID\(\s*(\d+)\s*\))");
        static const std::regex reRoom(R"(@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");
        static const std::regex reTrigForm(R"(add\s+Trigger_Type\()");
        // The two vertical forms come first: they are their own words now,
        // because guessing wall-or-stacked from the geometry is what broke
        // andy1. "Celing" is tolerated too - the author writes it that way.
        static const std::regex reMoveRoom(R"(Move_Room(\d+)\s*\(([^)]*)\))", std::regex::icase);
        static const std::regex reAxis(R"(([xyz])\s*=\s*([+-]?\d+))", std::regex::icase);
        static const std::regex reClear(R"(@\s*Clear)", std::regex::icase);
        static const std::regex reLikeWall(R"(@\s*LikeWall\s*\(\s*Room(\d+)\s*\(\s*x(\d+)\s*,\s*z(\d+)\s*\)\s*\))", std::regex::icase);
        static const std::regex reCeilPortal(R"(Make_Ce[il]+ing_?Portal\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)\s*->\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))", std::regex::icase);
        static const std::regex reFloorPortal(R"(Make_Floor_?Portal\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)\s*->\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))", std::regex::icase);
        static const std::regex rePortal(R"(Make_Portal\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)\s*->\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");
        static const std::regex reClimb(R"(Make_Climb\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");
        static const std::regex reWater(R"(Make_Room_(No)?[Ww]ater\s*\(\s*(\d+)\s*\))");
        static const std::regex reOcb(R"(@\s*OCB\(\s*(\d+)\s*\))");
        static const std::regex reAngle(R"(@\s*Angle\s*\(?\s*(-?\d+))");
        static const std::regex reHeight(R"(@\s*Height\(\s*(-?\d+)\s*\))");
        static const std::regex reXOff(R"(@\s*XOff\(\s*(-?\d+)\s*\))");
        static const std::regex reZOff(R"(@\s*ZOff\(\s*(-?\d+)\s*\))");
        static const std::regex reLike(R"(@\s*LikeItem\(\s*(\d+)\s*\))");
        static const std::regex reTrig(R"(Trigger_Type\(\s*(\w+)\s*\)[\s\S]*?Item_Level_ID\(\s*([^)]*)\))");

        std::smatch m;
        if (std::regex_search(raw, m, reWater))
        {
            e.kind = Option::WATER;
            e.waterOn = !m[1].matched;               // leading "No" = turn off
            e.room = std::stoi(m[2]);
        }
        else if (std::regex_search(raw, m, reMoveRoom))
        {
            e.kind = Option::MOVEROOM;
            e.room = std::stoi(m[1]);
            const std::string body = m[2];
            for (std::sregex_iterator it(body.begin(), body.end(), reAxis), z; it != z; ++it)
            {
                const long v = std::stol((*it)[2]);
                switch (std::tolower((*it)[1].str()[0]))
                {
                    case 'x': e.moveX = v; break;
                    // PLUS RAISES. Y grows downward in the file, so the
                    // sign is turned round here: the list is written the way
                    // a level editor works, the patch carries file values.
                    case 'y': e.moveY = -v; break;
                    default:  e.moveZ = v; break;
                }
            }
        }
        else if (std::regex_search(raw, m, reClimb))
        {
            e.kind = Option::CLIMB;
            e.room = std::stoi(m[1]); e.sx = std::stoi(m[2]); e.sz = std::stoi(m[3]);
        }
        else if (std::regex_search(raw, m, reCeilPortal) ||
                 std::regex_search(raw, m, reFloorPortal) ||
                 std::regex_search(raw, m, rePortal))
        {
            e.kind = Option::PORTAL;
            e.portalKind = std::regex_search(raw, reCeilPortal) ? 1
                         : std::regex_search(raw, reFloorPortal) ? 2 : 0;
            e.room = std::stoi(m[1]); e.sx = std::stoi(m[2]); e.sz = std::stoi(m[3]);
            e.roomB = std::stoi(m[4]); e.sxB = std::stoi(m[5]); e.szB = std::stoi(m[6]);

            std::smatch w;
            e.portalClear = std::regex_search(raw, w, reClear);
            if (std::regex_search(raw, w, reLikeWall))
            {
                e.likeWallRoom = std::stoi(w[1]);
                e.likeWallSx = std::stoi(w[2]);
                e.likeWallSz = std::stoi(w[3]);
            }
        }
        else if (std::regex_search(raw, m, reReplace))
        {
            e.kind = Option::REPLACE;
            e.index = std::stoi(m[1]);
            e.typeId = std::stoi(m[2]);
            // Optionally with a move: @Room relocates the item to the sector
            // center (floor computed, @Height on top). Without @Room the
            // position stays - that used to be the ONLY case, and a @Room
            // written anyway was silently swallowed (noticed by the user:
            // the joby5 trapdoor was meant to move into the lower room and
            // nothing happened).
            std::smatch r;
            if (std::regex_search(raw, r, reRoom))
            { e.room = std::stoi(r[1]); e.sx = std::stoi(r[2]); e.sz = std::stoi(r[3]); }
        }
        else if (std::regex_search(raw, m, reItem))
        {
            e.kind = Option::ITEM;
            e.typeId = std::stoi(m[1]);
            std::smatch r;
            if (std::regex_search(raw, r, reRoom))
            { e.room = std::stoi(r[1]); e.sx = std::stoi(r[2]); e.sz = std::stoi(r[3]); }
            else if (!std::regex_search(raw, r, reLike))
                return false;                        // neither room nor reference item
        }
        else if (std::regex_search(raw, m, reTrigForm))
        {
            e.kind = Option::TRIGGER;
            std::smatch r;
            if (!std::regex_search(raw, r, reRoom)) return false;
            e.room = std::stoi(r[1]); e.sx = std::stoi(r[2]); e.sz = std::stoi(r[3]);
        }
        else return false;

        if (std::regex_search(raw, m, reOcb))   { e.hasOcb = true;   e.ocb = std::stoi(m[1]); }
        if (std::regex_search(raw, m, reAngle)) { e.hasAngle = true; e.angleDeg = std::stoi(m[1]); }
        if (std::regex_search(raw, m, reHeight)) { e.hasHeight = true; e.height = std::stoi(m[1]); }
        if (std::regex_search(raw, m, reXOff))  { e.hasXOff = true;  e.xOff = std::stoi(m[1]); }
        if (std::regex_search(raw, m, reZOff))  { e.hasZOff = true;  e.zOff = std::stoi(m[1]); }
        if (std::regex_search(raw, m, reLike))    e.likeItem = std::stoi(m[1]);
        if (e.kind != Option::REPLACE && e.kind != Option::PORTAL && e.kind != Option::CLIMB &&
            e.kind != Option::WATER && e.kind != Option::MOVEROOM &&
            std::regex_search(raw, m, reTrig))
        {
            e.triggerKind = m[1];
            std::string list = m[2];
            size_t a = 0;
            while (a <= list.size())
            {
                size_t b = list.find(',', a);
                if (b == std::string::npos) b = list.size();
                std::string t = list.substr(a, b - a);
                const size_t x = t.find_first_not_of(" \t");
                const size_t y = t.find_last_not_of(" \t");
                if (x != std::string::npos) e.targets.push_back(t.substr(x, y - x + 1));
                a = b + 1;
            }
        }
        if (e.kind == Option::TRIGGER && e.targets.empty()) return false;

        // Strictness over leniency: strike out everything recognized; what
        // remains is a typo and gets reported.
        std::string rest = raw;
        auto strike = [&rest](const std::regex& re) { rest = std::regex_replace(rest, re, ""); };
        strike(reMoveRoom);
        strike(reLikeWall);
        strike(reClear);
        strike(reCeilPortal);
        strike(reFloorPortal);
        strike(rePortal);
        strike(reClimb);
        strike(reWater);
        static const std::regex reWClimb(R"(Make_Climb)");
        strike(reWClimb);
        static const std::regex reW1(R"(replace\s+)"), reW2(R"(with\s*->)"), reW3(R"(add\s+)");
        static const std::regex reW4(R"(Type_?ID\(\s*\d+\s*\))");
        static const std::regex reW5(R"(Trigger_Type\(\s*\w+\s*\))");
        static const std::regex reW6(R"(to\s*->\s*\w+)");
        static const std::regex reW7(R"(Item_Level_ID\([^)]*\))");
        static const std::regex reW8(R"([()+;\s])");
        strike(reW1); strike(reW2); strike(reW3); strike(reW4);
        strike(reRoom); strike(reOcb);
        static const std::regex reAngleFull(R"(@\s*Angle\s*\(?\s*-?\d+\s*\)?)");
        strike(reAngleFull);
        strike(reHeight); strike(reXOff); strike(reZOff); strike(reLike);
        strike(reW5); strike(reW6); strike(reW7); strike(reW8);
        unknown = rest;
        return true;
    }

    bool readList(const std::wstring& path, std::vector<Row>& out,
                   std::vector<std::string>& warnings, std::string& error)
    {
        const std::filesystem::path p(path);
        std::ifstream f(p);
        if (!f) { error = "secret list not readable"; return false; }

        static const std::regex reRow(R"(^\s*(\w+)\.trc\s+(\d+)\s+(\d+)\s+\(([^)]*)\)\s*(.*)$)");
        static const std::regex reSpot(R"(Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\))");

        std::string z;
        while (std::getline(f, z))
        {
            if (!z.empty() && z.back() == '\r') z.pop_back();
            const size_t h = z.find_first_not_of(" \t");
            if (h == std::string::npos || z[h] == '#') continue;

            std::smatch m;
            if (!std::regex_match(z, m, reRow)) continue;

            Row row;
            row.level = m[1];
            row.count = std::stoi(m[2]);
            row.type = std::stoi(m[3]);
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

            // Boundary between spots and extras: mask out the Room(x,z)
            // brackets, the first remaining bracket starts the extras -
            // regardless of the verb (the "(add" trap from SecretPlan.ps1).
            std::string mask = rest;
            for (std::sregex_iterator it(rest.begin(), rest.end(), reSpot), e; it != e; ++it)
                for (size_t i = 0; i < it->length(); ++i)
                    mask[static_cast<size_t>(it->position()) + i] = ' ';
            const size_t pExtra = mask.find('(');
            const std::string front = (pExtra != std::string::npos) ? rest.substr(0, pExtra) : rest;

            for (std::sregex_iterator it(front.begin(), front.end(), reSpot), e; it != e; ++it)
            {
                Spot s;
                s.room = std::stoi((*it)[1]); s.sx = std::stoi((*it)[2]); s.sz = std::stoi((*it)[3]);
                row.spots.push_back(s);
            }

            if (static_cast<int>(row.ids.size()) != row.count)
                warnings.push_back(row.level + ": count " + std::to_string(row.count) +
                                    ", but " + std::to_string(row.ids.size()) + " Level_ID");
            if (!row.spots.empty() && row.spots.size() != row.ids.size())
                warnings.push_back(row.level + ": " + std::to_string(row.ids.size()) +
                                    " secrets, but " + std::to_string(row.spots.size()) + " new spots");

            if (pExtra != std::string::npos)
                for (const std::string& g : splitGroups(rest.substr(pExtra)))
                {
                    Option o;
                    std::string unknown;
                    if (!readOption(g, o, unknown))
                    { warnings.push_back(row.level + ": option not readable -> " + g); continue; }
                    if (!unknown.empty())
                        warnings.push_back(row.level + ": part of an option not understood -> '" + unknown + "'");
                    row.extras.push_back(o);
                }

            out.push_back(row);
        }
        if (out.empty()) { error = "secret list contains not a single level row"; return false; }
        return true;
    }
}

std::vector<SecretSource> findSecretSources(const std::wstring& folder)
{
    std::vector<SecretSource> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec))
    {
        if (!e.is_regular_file()) continue;
        const std::filesystem::path p = e.path();
        if (lower(p.extension().string()) != ".txt") continue;
        const std::string name = p.stem().string();
        if (name.empty() || name[0] == '_') continue;
        const size_t u = name.find_last_of('_');
        if (u == std::string::npos) continue;
        const std::string cat = name.substr(u + 1);
        if (lower(cat) != "glitchless" && lower(cat) != "glitched") continue;
        SecretSource q;
        q.author = name.substr(0, u);
        q.category = (lower(cat) == "glitchless") ? "Glitchless" : "Glitched";
        q.path = p.wstring();
        out.push_back(q);
    }
    return out;
}

bool buildSecretPatch(const std::wstring& listPath, const std::string& levelName,
                      const Tr5Level& lvl, JsonValue& patchOut,
                      int& secretsMoved,
                      std::vector<std::string>& warnings, std::string& error)
{
    secretsMoved = 0;
    std::vector<Row> rows;
    if (!readList(listPath, rows, warnings, error)) return false;

    const Row* row = nullptr;
    for (const Row& z : rows)
        if (lower(z.level) == lower(levelName)) { row = &z; break; }
    if (!row)
    {
        // No entry means: this level stays vanilla. That is the normal case
        // for del and richcut2 (bonus levels without secrets) and allows an
        // author list to cover only part of the levels.
        warnings.push_back(levelName + ": no entry in the secret list - stays vanilla.");
        patchOut = JsonValue::makeObject();
        return true;
    }

    JsonValue edits = JsonValue::makeArray();
    JsonValue adds = JsonValue::makeArray();
    JsonValue trigger = JsonValue::makeArray();
    JsonValue portals = JsonValue::makeArray();
    JsonValue moves = JsonValue::makeArray();
    JsonValue climbs = JsonValue::makeArray();
    JsonValue water = JsonValue::makeArray();

    const std::vector<Tr5Room>& rooms = lvl.rooms();
    auto roomOk = [&](int r) { return r >= 0 && r < static_cast<int>(rooms.size()); };

    // Cross-check against the real file: are the given indices really
    // secrets? A typo would otherwise silently move a foreign item.
    for (int id : row->ids)
    {
        Tr5Item it;
        if (!lvl.getItem(id, it) || it.objectNumber != row->type)
            warnings.push_back(levelName + ": item " + std::to_string(id) +
                                " is not of type " + std::to_string(row->type) + " in the file");
    }

    // The secrets to their new spots.
    for (size_t i = 0; i < row->spots.size() && i < row->ids.size(); ++i)
    {
        const Spot& s = row->spots[i];
        if (!roomOk(s.room))
        { error = levelName + ": room " + std::to_string(s.room) + " does not exist"; return false; }
        JsonValue e = JsonValue::makeObject();
        e.set("index", JsonValue::makeInt(row->ids[i]));
        e.set("room", JsonValue::makeInt(s.room));
        e.set("x", JsonValue::makeInt(rooms[static_cast<size_t>(s.room)].originX + s.sx * 1024 + 512));
        e.set("z", JsonValue::makeInt(rooms[static_cast<size_t>(s.room)].originZ + s.sz * 1024 + 512));
        e.set("y", JsonValue::makeInt(0));
        e.set("yAuto", JsonValue::makeBool(true));
        // Rule 3 of the randomizer applies here too: a pedestal or hiding
        // OCB describes the OLD spot and does not survive the move.
        // Andrea3's secret 75 carried OCB 4 (pedestal, vanilla room 81); at
        // the new spot PickUpCollision looks for a pedestal via FindPlinth
        // that is not there - the secret lay in plain sight and was forever
        // unpickable (found by the user, first blamed on the push block).
        e.set("ocb", JsonValue::makeInt(0));
        edits.push(e);
        ++secretsMoved;
    }

    // Extras. New items append starting at the REAL item count.
    const int itemBase = lvl.itemCount();
    int addCount = 0;
    for (const Option& o : row->extras) if (o.kind == Option::ITEM) ++addCount;

    auto targetCommands = [&](const std::vector<std::string>& targets) -> JsonValue
    {
        JsonValue c = JsonValue::makeArray();
        for (const std::string& z : targets)
        {
            int idx = -1;
            if (!z.empty() && std::isdigit(static_cast<unsigned char>(z[0])))
                idx = std::atoi(z.c_str());
            // "New<k>" is the keyword; "Neu<k>" was its German spelling before the
            // project went English and is still accepted, because lists written
            // against the old name are out there and both are three letters long,
            // so the number behind starts at the same offset either way.
            else if (z.rfind("New", 0) == 0 || z.rfind("Neu", 0) == 0)
            {
                const int k = std::atoi(z.c_str() + 3);
                if (k >= 1 && k <= addCount) idx = itemBase + k - 1;
                else warnings.push_back(levelName + ": " + z + " does not exist - the row creates " +
                                         std::to_string(addCount) + " items.");
            }
            else warnings.push_back(levelName + ": trigger target '" + z + "' not understood - skipped.");
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
    auto triggerKindNo = [&](const std::string& kind) -> int
    {
        const std::string a = lower(kind);
        if (a == "key")     return 3;
        if (a == "switch")  return 2;
        if (a == "pickup")  return 4;
        if (a == "trigger") return 0;
        warnings.push_back(levelName + ": unknown trigger kind '" + kind + "' - treated as a plain trigger.");
        return 0;
    };

    int n = 0;
    for (const Option& o : row->extras)
    {
        if (o.kind == Option::WATER)
        {
            JsonValue w = JsonValue::makeObject();
            w.set("room", JsonValue::makeInt(o.room));
            w.set("on", JsonValue::makeBool(o.waterOn));
            water.push(w);
            continue;
        }

        if (o.kind == Option::CLIMB)
        {
            if (!o.hasAngle)
            { error = levelName + ": Make_Climb needs @Angle (Lara's view onto the wall)"; return false; }
            // Engine convention control.cpp:2189: quad = (yRot+0x2000)/0x4000,
            // bit = 1 << (quad+8). In degrees: 0->0x100, 90->0x200, 180->0x400,
            // 270->0x800. Odd angles round to the nearest quarter.
            const int deg = ((o.angleDeg % 360) + 360) % 360;
            const int quad = ((deg + 45) / 90) & 3;
            JsonValue c = JsonValue::makeObject();
            c.set("room", JsonValue::makeInt(o.room));
            c.set("sectorX", JsonValue::makeInt(o.sx));
            c.set("sectorZ", JsonValue::makeInt(o.sz));
            c.set("dirBits", JsonValue::makeInt(1 << (quad + 8)));
            climbs.push(c);
            continue;
        }

        if (o.kind == Option::MOVEROOM)
        {
            JsonValue mv = JsonValue::makeObject();
            mv.set("room", JsonValue::makeInt(o.room));
            mv.set("dx", JsonValue::makeInt(static_cast<int>(o.moveX)));
            mv.set("dy", JsonValue::makeInt(static_cast<int>(o.moveY)));
            mv.set("dz", JsonValue::makeInt(static_cast<int>(o.moveZ)));
            moves.push(mv);
            continue;
        }

        if (o.kind == Option::PORTAL)
        {
            JsonValue p = JsonValue::makeObject();
            p.set("roomA", JsonValue::makeInt(o.room));
            p.set("sxA", JsonValue::makeInt(o.sx));
            p.set("szA", JsonValue::makeInt(o.sz));
            p.set("roomB", JsonValue::makeInt(o.roomB));
            p.set("sxB", JsonValue::makeInt(o.sxB));
            p.set("szB", JsonValue::makeInt(o.szB));
            p.set("kind", JsonValue::makeInt(o.portalKind));
            p.set("clear", JsonValue::makeInt(o.portalClear ? 1 : 0));
            p.set("likeWallRoom", JsonValue::makeInt(o.likeWallRoom));
            p.set("likeWallSx", JsonValue::makeInt(o.likeWallSx));
            p.set("likeWallSz", JsonValue::makeInt(o.likeWallSz));
            portals.push(p);
            continue;
        }

        if (o.kind == Option::REPLACE)
        {
            JsonValue e = JsonValue::makeObject();
            e.set("index", JsonValue::makeInt(o.index));
            e.set("typeId", JsonValue::makeInt(o.typeId));
            // With @Room the item moves: sector center, floor computed.
            // Needed for the joby5 trapdoor (#53): a trapdoor MUST stand on
            // the portal plane of its sector (CloseTrapDoor seals nothing
            // otherwise), and the ceiling trapdoor belongs in the LOWER
            // room at its ceiling for that - so a room change plus @Height.
            // A @Room here used to be silently swallowed; the user expected
            // the move and nothing happened.
            if (o.room >= 0)
            {
                if (!roomOk(o.room))
                { error = levelName + ": room " + std::to_string(o.room) + " does not exist"; return false; }
                e.set("room", JsonValue::makeInt(o.room));
                e.set("x", JsonValue::makeInt(rooms[static_cast<size_t>(o.room)].originX + o.sx * 1024 + 512));
                e.set("z", JsonValue::makeInt(rooms[static_cast<size_t>(o.room)].originZ + o.sz * 1024 + 512));
                e.set("y", JsonValue::makeInt(0));
                e.set("yAuto", JsonValue::makeBool(true));
            }
            if (o.hasAngle) e.set("angle", JsonValue::makeInt(degToRaw(o.angleDeg)));
            if (o.hasOcb)   e.set("ocb", JsonValue::makeInt(o.ocb));
            if (o.hasHeight) { e.set("yOff", JsonValue::makeInt(-o.height)); e.set("yAuto", JsonValue::makeBool(true)); }
            if (o.hasXOff)  e.set("xOff", JsonValue::makeInt(o.xOff));
            if (o.hasZOff)  e.set("zOff", JsonValue::makeInt(o.zOff));
            edits.push(e);
            continue;
        }

        if (o.kind == Option::TRIGGER)
        {
            JsonValue t = JsonValue::makeObject();
            t.set("room", JsonValue::makeInt(o.room));
            t.set("sectorX", JsonValue::makeInt(o.sx));
            t.set("sectorZ", JsonValue::makeInt(o.sz));
            t.set("kind", JsonValue::makeInt(triggerKindNo(o.triggerKind)));
            t.set("timer", JsonValue::makeInt(0));
            t.set("oneShot", JsonValue::makeInt(1));
            t.set("mask", JsonValue::makeInt(31));
            t.set("commands", targetCommands(o.targets));
            trigger.push(t);
            continue;
        }

        // Switches and keyholes need FLAT ground.
        //
        // Their position window has width zero in Y (SwitchBounds and
        // KeyHoleBounds: bounds[2] == bounds[3] == 0), so TestLaraPosition
        // demands Lara's y_pos EXACTLY equal to the item height
        // (collide.cpp:1455). On a slope Lara takes on arbitrarily many
        // heights depending on where she stands and never hits the one -
        // the object is then unusable, without a crash and without a
        // message. Cross-checked across all 13 levels: all 72 vanilla
        // switches stand on flat ground, not a single exception. Found at
        // the andy3 switch the user could not operate (room 202 sector 1,1
        // carries TILT).
        if ((o.typeId >= 258 && o.typeId <= 274) && o.room >= 0)
        {
            SlopeInfo si;
            int baseY = 0;
            if (lvl.floorSlope(o.room, o.sx, o.sz, si, baseY) > 0)
                warnings.push_back(levelName + ": type " + std::to_string(o.typeId) +
                                    " on room " + std::to_string(o.room) + "(" + std::to_string(o.sx) +
                                    "," + std::to_string(o.sz) + ") stands on a SLOPE - switches and "
                                    "keyholes cannot be operated there (the Y window is zero wide).");
        }

        // Append the item.
        JsonValue e = JsonValue::makeObject();
        e.set("typeId", JsonValue::makeInt(o.typeId));
        if (o.likeItem >= 0)
        {
            Tr5Item ref;
            if (!lvl.getItem(o.likeItem, ref))
            { error = levelName + ": LikeItem(" + std::to_string(o.likeItem) + ") does not exist"; return false; }
            e.set("room", JsonValue::makeInt(ref.roomNumber));
            e.set("x", JsonValue::makeInt(ref.x));
            e.set("z", JsonValue::makeInt(ref.z));
            e.set("y", JsonValue::makeInt(ref.y));
            e.set("yAuto", JsonValue::makeBool(false));
        }
        else
        {
            if (!roomOk(o.room))
            { error = levelName + ": room " + std::to_string(o.room) + " does not exist"; return false; }
            e.set("room", JsonValue::makeInt(o.room));
            e.set("x", JsonValue::makeInt(rooms[static_cast<size_t>(o.room)].originX + o.sx * 1024 + 512));
            e.set("z", JsonValue::makeInt(rooms[static_cast<size_t>(o.room)].originZ + o.sz * 1024 + 512));
            e.set("y", JsonValue::makeInt(0));
            e.set("yAuto", JsonValue::makeBool(true));
        }
        if (o.hasAngle) e.set("angle", JsonValue::makeInt(degToRaw(o.angleDeg)));
        if (o.hasOcb)   e.set("ocb", JsonValue::makeInt(o.ocb));
        if (o.hasHeight) e.set("yOff", JsonValue::makeInt(-o.height));
        if (o.hasXOff)  e.set("xOff", JsonValue::makeInt(o.xOff));
        if (o.hasZOff)  e.set("zOff", JsonValue::makeInt(o.zOff));
        adds.push(e);
        const int myIndex = itemBase + n;
        ++n;

        if (!o.triggerKind.empty() && o.room < 0)
        {
            // The trigger sticks to the sector - with @LikeItem and no
            // @Room none is named. So far no row needs that; whoever wants
            // it anyway adds @Room as well.
            warnings.push_back(levelName + ": trigger on a LikeItem entry without @Room - skipped.");
        }
        else if (!o.triggerKind.empty())
        {
            JsonValue t = JsonValue::makeObject();
            t.set("room", JsonValue::makeInt(o.room));
            t.set("sectorX", JsonValue::makeInt(o.sx));
            t.set("sectorZ", JsonValue::makeInt(o.sz));
            t.set("kind", JsonValue::makeInt(triggerKindNo(o.triggerKind)));
            t.set("timer", JsonValue::makeInt(0));
            t.set("oneShot", JsonValue::makeInt(1));
            t.set("mask", JsonValue::makeInt(31));
            t.set("refItem", JsonValue::makeInt(myIndex));
            t.set("commands", targetCommands(o.targets));
            trigger.push(t);
        }
    }

    patchOut = JsonValue::makeObject();
    if (edits.size() > 0)   patchOut.set("itemEdits", edits);
    if (adds.size() > 0)    patchOut.set("itemAdds", adds);
    if (trigger.size() > 0) patchOut.set("triggerAdds", trigger);
    if (moves.size() > 0)   patchOut.set("roomMoves", moves);
    if (portals.size() > 0) patchOut.set("portalAdds", portals);
    if (climbs.size() > 0)  patchOut.set("climbAdds", climbs);
    if (water.size() > 0)   patchOut.set("roomWater", water);
    return true;
}

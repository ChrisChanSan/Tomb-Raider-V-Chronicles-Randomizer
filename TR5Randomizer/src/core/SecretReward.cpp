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
#include "SecretReward.h"
#include "Rng.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

namespace
{
    struct Entry { const char* name; int object; int lots; };

    // The pool, and what each is worth in the draw.
    //
    // A weapon gets ONE lot, ammunition and medipacks three. Without that a
    // reward of six comes out as six weapons and the supplies vanish behind
    // them - and by the fourth level she has everything anyway, so a weapon
    // is only ever a bonus while the ammunition is the substance.
    //
    // The list matches the engine's kRewardNames exactly (game/secretreward.
    // cpp), which is the whole permission there: pistols and their ammo, the
    // crossbow family, sights and flares were struck off by the user, each
    // for its own reason.
    const Entry kPool[] = {
        { "Uzi",              336, 1 },
        { "Shotgun",          338, 1 },
        { "HK",               345, 1 },
        { "Revolver",         347, 1 },
        { "Uzi_Ammo",         337, 3 },
        { "Shotgun_Ammo",     339, 3 },
        { "Shotgun_Wideshot", 340, 3 },
        { "HK_Ammo",          346, 3 },
        { "Revolver_Ammo",    348, 3 },
        { "Big_Medipack",     349, 3 },
        { "Small_Medipack",   350, 3 },
    };

    bool isWeapon(int object)
    {
        return object == 336 || object == 338 || object == 345 || object == 347;
    }

    // The fourteen levels that are really played, in the engine's own order.
    const char* kLevels[] = {
        "Streets_of_Rome", "Trajan_Markets", "Colosseum", "Base", "Submarine",
        "Deepsea_Dive", "Sinking_Submarine", "Gallows_Tree", "Labyrinth",
        "Old_Mill", "Thirteenth_Floor", "Escape_with_the_Iris",
        "Security_Breach", "Red_Alert"
    };
    const int kLevelCount = static_cast<int>(sizeof(kLevels) / sizeof(kLevels[0]));

    std::string rollOne(Rng& rng)
    {
        // Four to eight entries, as the user asked.
        const int want = 4 + static_cast<int>(rng.below(5));

        std::vector<int> pot;
        for (const Entry& e : kPool)
            for (int n = 0; n < e.lots; ++n)
                pot.push_back(e.object);

        // Order kept so the same seed always writes the same line; a map
        // would sort by object number and lose which came first, which is
        // harmless but makes two runs harder to compare.
        std::vector<int> order;
        std::map<int, int> count;

        for (int n = 0; n < want && !pot.empty(); ++n)
        {
            const int object = pot[static_cast<size_t>(rng.below(pot.size()))];

            // A weapon only once. Two Uzis in one find are not a find, they
            // are a mistake - and the second would vanish into the
            // inventory without a trace. Ammunition may repeat and is then
            // counted up.
            if (isWeapon(object))
            {
                if (count.count(object)) { --n; continue; }
                for (size_t i = 0; i < pot.size(); )
                    if (pot[i] == object) pot.erase(pot.begin() + static_cast<ptrdiff_t>(i));
                    else ++i;
            }

            if (!count.count(object)) order.push_back(object);
            count[object] += isWeapon(object) ? 1 : (1 + static_cast<int>(rng.below(2)));
        }

        std::string out;
        for (size_t i = 0; i < order.size(); ++i)
        {
            const char* name = "?";
            for (const Entry& e : kPool)
                if (e.object == order[i]) { name = e.name; break; }

            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s%s x %d",
                          i ? ", " : "", name, count[order[i]]);
            out += buf;
        }

        return out.empty() ? std::string("Small_Medipack x 1") : out;
    }

    bool lineIsLevel(const std::string& line, std::string& name)
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) return false;

        name.clear();
        for (size_t i = 0; i < eq; ++i)
        {
            const char c = line[i];
            if (c == ' ' || c == '\t') continue;
            if (c == ';' || c == '#') return false;
            name.push_back(c);
        }
        return !name.empty();
    }
}

bool writeSecretRewards(const std::wstring& gameDir, unsigned long long seed,
                        std::string& summary, std::string& error)
{
    std::wstring path = gameDir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path.push_back(L'\\');
    path += L"SecretRewards.txt";

    // A seed of its own, so that changing the reward does not shift every
    // other roll of the run.
    Rng rng(seed ^ 0x5245574152447373ull);          // "REWARDss"

    std::vector<std::string> rolled;
    for (int i = 0; i < kLevelCount; ++i)
        rolled.push_back(rollOne(rng));

    // Whatever stands there already is kept - the header, the player's
    // comments, and any line we do not own. Only the fourteen level lines
    // are replaced, and a missing one is appended. The engine completes the
    // file by itself if we leave it half-finished.
    std::vector<std::string> out;
    std::vector<bool> written(static_cast<size_t>(kLevelCount), false);

    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            std::string name;
            bool replaced = false;

            if (lineIsLevel(line, name))
                for (int i = 0; i < kLevelCount; ++i)
                {
                    // The file's own spelling is the reference; nothing is
                    // matched loosely here, because a line we fail to
                    // recognise would be duplicated below.
                    if (name != kLevels[i]) continue;
                    char buf[512];
                    std::snprintf(buf, sizeof(buf), "%-22s = %s", kLevels[i], rolled[static_cast<size_t>(i)].c_str());
                    out.push_back(buf);
                    written[static_cast<size_t>(i)] = true;
                    replaced = true;
                    break;
                }

            if (!replaced) out.push_back(line);
        }
    }

    for (int i = 0; i < kLevelCount; ++i)
    {
        if (written[static_cast<size_t>(i)]) continue;
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%-22s = %s", kLevels[i], rolled[static_cast<size_t>(i)].c_str());
        out.push_back(buf);
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        error = "SecretRewards.txt could not be written";
        return false;
    }

    for (const std::string& l : out) f << l << "\r\n";

    std::ostringstream s;
    s << "Secret rewards rolled for all " << kLevelCount << " levels";
    summary = s.str();
    return true;
}

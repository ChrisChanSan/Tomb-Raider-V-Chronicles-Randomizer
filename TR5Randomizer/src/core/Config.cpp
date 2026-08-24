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
#include "Config.h"
#include "DataFolder.h"

#include <cstdio>
#include <cstdlib>

std::wstring Config::path()
{
    return PathUtil::join(PathUtil::exeDir(), L"TR5Randomizer.cfg");
}

void Config::load()
{
    *this = Config();   // everything to defaults; missing keys stay that way

    FILE* f = nullptr;
    if (_wfopen_s(&f, path().c_str(), L"rb") != 0 || !f) return;   // first start

    std::string text;
    char buf[1024];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, got);
    std::fclose(f);

    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "sourceDir") sourceDir = PathUtil::widen(val);
        else if (key == "tr3Dir")     tr3Dir = PathUtil::widen(val);
        else if (key == "tr3GoldDir") tr3GoldDir = PathUtil::widen(val);
        else if (key == "seed")  seedText = val;
        else if (key == "winW")            winW = std::atoi(val.c_str());
        else if (key == "winH")            winH = std::atoi(val.c_str());
        else if (key == "language")         germanUi = (val == "de");
        else if (key == "equipment")     optOtherPickups = (val == "1");
        else if (key == "keyItems")        optKeyItems = (val == "1");
        else if (key == "extraEnemies")    optEnemies = (val == "1");
        else if (key == "enemyCount")    enemyCount = std::atoi(val.c_str());
        else if (key == "enemySwap")       optEnemySwap = (val == "1");
        else if (key == "enemyPool")      enemyPool = std::atoi(val.c_str());
        else if (key == "enemiesDisabled")
        {
            // Comma list of deselected typeIds. Empty pieces are skipped so
            // a trailing comma does not arrive as type 0 (Lara!).
            enemiesDisabled.clear();
            size_t a = 0;
            while (a <= val.size())
            {
                size_t e = val.find(',', a);
                if (e == std::string::npos) e = val.size();
                std::string piece = val.substr(a, e - a);
                if (!piece.empty())
                {
                    int t = std::atoi(piece.c_str());
                    if (t > 0) enemiesDisabled.insert(t);
                }
                a = e + 1;
            }
        }
        else if (key == "outfits")         optOutfits = (val == "1");
        else if (key == "itemAmount")       itemAmount = std::atoi(val.c_str());
        else if (key == "secrets")         secrets = std::atoi(val.c_str());
        else if (key == "secretAuthor")     secretAuthor = val;
        else if (key == "musicSwitch")     optMusicSwitch = (val == "1");
        else if (key == "musicTrigger")    optMusicTrigger = (val == "1");
        else if (key == "levelSections") optLevelSections = (val == "1");
        else if (key == "levelOrder")      optLevelOrder = std::atoi(val.c_str());
        else if (key == "balWeapons")       balWeapons = std::atoi(val.c_str());
        else if (key == "balEnemyHealth")  balEnemyHealth = std::atoi(val.c_str());
        else if (key == "balEnemyDamage") balEnemyDamage = std::atoi(val.c_str());
        else if (key == "balWeaponsRandom")  balWeaponsRandom = (val == "1");
        else if (key == "balEnemyHealthRandom")   balEnemyHealthRandom = (val == "1");
        else if (key == "balEnemyDamageRandom") balEnemyDamageRandom = (val == "1");
    }

    if (optLevelOrder < 0 || optLevelOrder > 2) optLevelOrder = 0;

    // Limits of the UI sliders. Whoever turns the values in Balance.txt up
    // further by hand keeps them there - this is only about the slider
    // position, which would otherwise sit outside its track.
    if (balWeapons < 0)          balWeapons = 0;
    if (balWeapons > 200)        balWeapons = 200;
    if (balEnemyHealth < 50)    balEnemyHealth = 50;
    if (balEnemyHealth > 200)   balEnemyHealth = 200;
    if (balEnemyDamage < 50)  balEnemyDamage = 50;
    if (balEnemyDamage > 200) balEnemyDamage = 200;

    if (enemyCount < 1)  enemyCount = 1;
    if (enemyCount > 20) enemyCount = 20;
    if (itemAmount < 0 || itemAmount > 2) itemAmount = 0;
    if (secrets < 0 || secrets > 3) secrets = 0;
    if (enemyPool < 0 || enemyPool > 2) enemyPool = 0;
}

bool Config::save() const
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path().c_str(), L"wb") != 0 || !f) return false;

    // UTF-8 without BOM, so paths with umlauts come back unharmed.
    //
    // The keys here MUST match the ones load() reads - the #129 rename
    // once broke exactly this round trip (load read the new English keys,
    // save still wrote the old German ones, and a restart silently reset
    // half the settings).
    const std::string dir = PathUtil::narrow(sourceDir);
    std::fprintf(f, "sourceDir=%s\n", dir.c_str());
    std::fprintf(f, "tr3Dir=%s\n", PathUtil::narrow(tr3Dir).c_str());
    std::fprintf(f, "tr3GoldDir=%s\n", PathUtil::narrow(tr3GoldDir).c_str());
    std::fprintf(f, "seed=%s\n", seedText.c_str());
    std::fprintf(f, "winW=%d\n", winW);
    std::fprintf(f, "winH=%d\n", winH);
    std::fprintf(f, "language=%s\n", germanUi ? "de" : "en");
    std::fprintf(f, "equipment=%d\n", optOtherPickups ? 1 : 0);
    std::fprintf(f, "keyItems=%d\n", optKeyItems ? 1 : 0);
    std::fprintf(f, "extraEnemies=%d\n", optEnemies ? 1 : 0);
    std::fprintf(f, "enemyCount=%d\n", enemyCount);
    std::fprintf(f, "enemySwap=%d\n", optEnemySwap ? 1 : 0);
    std::fprintf(f, "enemyPool=%d\n", enemyPool);
    std::fprintf(f, "enemiesDisabled=");
    {
        bool first = true;
        for (int t : enemiesDisabled) { std::fprintf(f, "%s%d", first ? "" : ",", t); first = false; }
    }
    std::fprintf(f, "\n");
    std::fprintf(f, "outfits=%d\n", optOutfits ? 1 : 0);
    std::fprintf(f, "itemAmount=%d\n", itemAmount);
    std::fprintf(f, "secrets=%d\n", secrets);
    // Was read but never written once - the author choice used to be lost
    // on every program start.
    std::fprintf(f, "secretAuthor=%s\n", secretAuthor.c_str());
    std::fprintf(f, "musicSwitch=%d\n", optMusicSwitch ? 1 : 0);
    std::fprintf(f, "musicTrigger=%d\n", optMusicTrigger ? 1 : 0);
    std::fprintf(f, "levelSections=%d\n", optLevelSections ? 1 : 0);
    std::fprintf(f, "levelOrder=%d\n", optLevelOrder);
    std::fprintf(f, "balWeapons=%d\n", balWeapons);
    std::fprintf(f, "balEnemyHealth=%d\n", balEnemyHealth);
    std::fprintf(f, "balEnemyDamage=%d\n", balEnemyDamage);
    std::fprintf(f, "balWeaponsRandom=%d\n", balWeaponsRandom ? 1 : 0);
    std::fprintf(f, "balEnemyHealthRandom=%d\n", balEnemyHealthRandom ? 1 : 0);
    std::fprintf(f, "balEnemyDamageRandom=%d\n", balEnemyDamageRandom ? 1 : 0);
    std::fclose(f);
    return true;
}

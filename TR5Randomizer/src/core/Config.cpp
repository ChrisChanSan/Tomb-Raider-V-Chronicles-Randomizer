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
    *this = Config();   // alles auf Vorgabe; fehlende Schluessel bleiben so

    FILE* f = nullptr;
    if (_wfopen_s(&f, path().c_str(), L"rb") != 0 || !f) return;   // erster Start

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
        else if (key == "seed")  seedText = val;
        else if (key == "winW")            winW = std::atoi(val.c_str());
        else if (key == "winH")            winH = std::atoi(val.c_str());
        else if (key == "ausruestung")     optOtherPickups = (val == "1");
        else if (key == "keyItems")        optKeyItems = (val == "1");
        else if (key == "gegnerZusatz")    optEnemies = (val == "1");
        else if (key == "gegnerAnzahl")    enemyCount = std::atoi(val.c_str());
        else if (key == "gegnerMix")       optEnemySwap = (val == "1");
        else if (key == "gegnerPool")      enemyPool = std::atoi(val.c_str());
        else if (key == "outfits")         optOutfits = (val == "1");
        else if (key == "secrets")         secrets = std::atoi(val.c_str());
    }

    if (enemyCount < 1)  enemyCount = 1;
    if (enemyCount > 20) enemyCount = 20;
    if (secrets < 0 || secrets > 3) secrets = 0;
    if (enemyPool < 0 || enemyPool > 2) enemyPool = 0;
}

bool Config::save() const
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path().c_str(), L"wb") != 0 || !f) return false;

    // UTF-8 ohne BOM, damit Pfade mit Umlauten unbeschadet zurueckkommen.
    const std::string dir = PathUtil::narrow(sourceDir);
    std::fprintf(f, "sourceDir=%s\n", dir.c_str());
    std::fprintf(f, "seed=%s\n", seedText.c_str());
    std::fprintf(f, "winW=%d\n", winW);
    std::fprintf(f, "winH=%d\n", winH);
    std::fprintf(f, "ausruestung=%d\n", optOtherPickups ? 1 : 0);
    std::fprintf(f, "keyItems=%d\n", optKeyItems ? 1 : 0);
    std::fprintf(f, "gegnerZusatz=%d\n", optEnemies ? 1 : 0);
    std::fprintf(f, "gegnerAnzahl=%d\n", enemyCount);
    std::fprintf(f, "gegnerMix=%d\n", optEnemySwap ? 1 : 0);
    std::fprintf(f, "gegnerPool=%d\n", enemyPool);
    std::fprintf(f, "outfits=%d\n", optOutfits ? 1 : 0);
    std::fprintf(f, "secrets=%d\n", secrets);
    std::fclose(f);
    return true;
}

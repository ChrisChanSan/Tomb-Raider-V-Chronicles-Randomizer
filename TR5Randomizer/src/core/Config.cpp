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
        else if (key == "tr3Dir")     tr3Dir = PathUtil::widen(val);
        else if (key == "tr3GoldDir") tr3GoldDir = PathUtil::widen(val);
        else if (key == "seed")  seedText = val;
        else if (key == "winW")            winW = std::atoi(val.c_str());
        else if (key == "winH")            winH = std::atoi(val.c_str());
        else if (key == "ausruestung")     optOtherPickups = (val == "1");
        else if (key == "keyItems")        optKeyItems = (val == "1");
        else if (key == "gegnerZusatz")    optEnemies = (val == "1");
        else if (key == "gegnerAnzahl")    enemyCount = std::atoi(val.c_str());
        else if (key == "gegnerMix")       optEnemySwap = (val == "1");
        else if (key == "gegnerPool")      enemyPool = std::atoi(val.c_str());
        else if (key == "gegnerAus")
        {
            // Kommaliste abgewaehlter typeIds. Leere Felder werden
            // uebersprungen, damit ein Endkomma nicht als Typ 0 (Lara!)
            // ankommt.
            gegnerAus.clear();
            size_t a = 0;
            while (a <= val.size())
            {
                size_t e = val.find(',', a);
                if (e == std::string::npos) e = val.size();
                std::string stueck = val.substr(a, e - a);
                if (!stueck.empty())
                {
                    int t = std::atoi(stueck.c_str());
                    if (t > 0) gegnerAus.insert(t);
                }
                a = e + 1;
            }
        }
        else if (key == "outfits")         optOutfits = (val == "1");
        else if (key == "itemMenge")       itemMenge = std::atoi(val.c_str());
        else if (key == "secrets")         secrets = std::atoi(val.c_str());
        else if (key == "secretAutor")     secretAutor = val;
        else if (key == "musikSwitch")     optMusikSwitch = (val == "1");
        else if (key == "musikTrigger")    optMusikTrigger = (val == "1");
        else if (key == "levelAbschnitte") optLevelAbschnitte = (val == "1");
        else if (key == "levelFolge")      optLevelFolge = std::atoi(val.c_str());
        else if (key == "balWaffen")       balWaffen = std::atoi(val.c_str());
        else if (key == "balGegnerLeben")  balGegnerLeben = std::atoi(val.c_str());
        else if (key == "balGegnerSchaden") balGegnerSchaden = std::atoi(val.c_str());
        else if (key == "balWaffenZufall")  balWaffenZufall = (val == "1");
        else if (key == "balGegnerLebenZufall")   balGegnerLebenZufall = (val == "1");
        else if (key == "balGegnerSchadenZufall") balGegnerSchadenZufall = (val == "1");
    }

    if (optLevelFolge < 0 || optLevelFolge > 2) optLevelFolge = 0;

    // Grenzen der Regler in der Oberflaeche. Wer die Werte in Balance.txt
    // von Hand weiter aufdreht, behaelt sie dort - hier geht es nur um die
    // Reglerstellung, die sonst ausserhalb ihrer Bahn laege.
    if (balWaffen < 0)          balWaffen = 0;
    if (balWaffen > 200)        balWaffen = 200;
    if (balGegnerLeben < 50)    balGegnerLeben = 50;
    if (balGegnerLeben > 200)   balGegnerLeben = 200;
    if (balGegnerSchaden < 50)  balGegnerSchaden = 50;
    if (balGegnerSchaden > 200) balGegnerSchaden = 200;

    if (enemyCount < 1)  enemyCount = 1;
    if (enemyCount > 20) enemyCount = 20;
    if (itemMenge < 0 || itemMenge > 2) itemMenge = 0;
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
    std::fprintf(f, "tr3Dir=%s\n", PathUtil::narrow(tr3Dir).c_str());
    std::fprintf(f, "tr3GoldDir=%s\n", PathUtil::narrow(tr3GoldDir).c_str());
    std::fprintf(f, "seed=%s\n", seedText.c_str());
    std::fprintf(f, "winW=%d\n", winW);
    std::fprintf(f, "winH=%d\n", winH);
    std::fprintf(f, "ausruestung=%d\n", optOtherPickups ? 1 : 0);
    std::fprintf(f, "keyItems=%d\n", optKeyItems ? 1 : 0);
    std::fprintf(f, "gegnerZusatz=%d\n", optEnemies ? 1 : 0);
    std::fprintf(f, "gegnerAnzahl=%d\n", enemyCount);
    std::fprintf(f, "gegnerMix=%d\n", optEnemySwap ? 1 : 0);
    std::fprintf(f, "gegnerPool=%d\n", enemyPool);
    std::fprintf(f, "gegnerAus=");
    {
        bool erstes = true;
        for (int t : gegnerAus) { std::fprintf(f, "%s%d", erstes ? "" : ",", t); erstes = false; }
    }
    std::fprintf(f, "\n");
    std::fprintf(f, "outfits=%d\n", optOutfits ? 1 : 0);
    std::fprintf(f, "itemMenge=%d\n", itemMenge);
    std::fprintf(f, "secrets=%d\n", secrets);
    // Wurde gelesen, aber nie geschrieben - die Autorenwahl ging bei jedem
    // Programmstart verloren.
    std::fprintf(f, "secretAutor=%s\n", secretAutor.c_str());
    std::fprintf(f, "musikSwitch=%d\n", optMusikSwitch ? 1 : 0);
    std::fprintf(f, "musikTrigger=%d\n", optMusikTrigger ? 1 : 0);
    std::fprintf(f, "levelAbschnitte=%d\n", optLevelAbschnitte ? 1 : 0);
    std::fprintf(f, "levelFolge=%d\n", optLevelFolge);
    std::fprintf(f, "balWaffen=%d\n", balWaffen);
    std::fprintf(f, "balGegnerLeben=%d\n", balGegnerLeben);
    std::fprintf(f, "balGegnerSchaden=%d\n", balGegnerSchaden);
    std::fprintf(f, "balWaffenZufall=%d\n", balWaffenZufall ? 1 : 0);
    std::fprintf(f, "balGegnerLebenZufall=%d\n", balGegnerLebenZufall ? 1 : 0);
    std::fprintf(f, "balGegnerSchadenZufall=%d\n", balGegnerSchadenZufall ? 1 : 0);
    std::fclose(f);
    return true;
}

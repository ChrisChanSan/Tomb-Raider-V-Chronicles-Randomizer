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
#include "Levelfix.h"

#include <cstdio>
#include <cstring>

namespace
{
    // Levelnamen ohne Ruecksicht auf Gross- und Kleinschreibung vergleichen -
    // die Dateien heissen mal "joby5", mal "Andrea1", und der Aufrufer reicht
    // durch, was im Ordner steht.
    bool gleicherName(const std::string& a, const char* b)
    {
        size_t i = 0;
        for (; i < a.size() && b[i]; ++i)
        {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = char(x + 32);
            if (y >= 'A' && y <= 'Z') y = char(y + 32);
            if (x != y) return false;
        }
        return i == a.size() && b[i] == 0;
    }
}

namespace Levelfix
{

int anwenden(Tr5Level& level, const std::string& levelName,
             std::vector<std::string>& warnungen)
{
    int uebernommen = 0;

    for (const Triggerkopie& k : kTriggerkopien)
    {
        if (!gleicherName(levelName, k.level)) continue;

        std::string fehler;
        if (level.kopiereTrigger(k.vonRaum, k.vonSx, k.vonSz,
                                 k.nachRaum, k.nachSx, k.nachSz, fehler))
        {
            ++uebernommen;
            continue;
        }

        // Nicht abbrechen: ohne den Fix ist das Level so kaputt wie vanilla,
        // mit einem Abbruch waere der ganze Lauf hin. Aber laut sagen.
        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "Levelfix nicht angewandt (%s -> Raum %d Sektor %d,%d): %s",
                      k.grund, k.nachRaum, k.nachSx, k.nachSz, fehler.c_str());
        warnungen.push_back(buf);
    }

    return uebernommen;
}

}

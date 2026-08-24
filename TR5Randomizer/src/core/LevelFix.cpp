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
#include "LevelFix.h"

#include <cstdio>
#include <cstring>

namespace
{
    // Compare level names ignoring case - the files are called "joby5" here
    // and "Andrea1" there, and the caller passes through whatever the
    // folder says.
    bool sameName(const std::string& a, const char* b)
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

namespace LevelFix
{

int apply(Tr5Level& level, const std::string& levelName,
             std::vector<std::string>& warnings)
{
    int applied = 0;

    for (const TriggerCopy& k : kTriggerCopies)
    {
        if (!sameName(levelName, k.level)) continue;

        std::string error;
        if (level.copyTrigger(k.fromRoom, k.fromSx, k.fromSz,
                                 k.toRoom, k.toSx, k.toSz, error))
        {
            ++applied;
            continue;
        }

        // Do not abort: without the fix the level is as broken as vanilla,
        // and an abort would kill the whole run. But say it out loud.
        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d sector %d,%d): %s",
                      k.reason, k.toRoom, k.toSx, k.toSz, error.c_str());
        warnings.push_back(buf);
    }

    return applied;
}

}

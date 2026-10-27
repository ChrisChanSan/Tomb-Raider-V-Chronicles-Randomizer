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
             std::vector<std::string>& warnings,
             std::vector<int>* freedItems)
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

    for (const TriggerMove& k : kTriggerMoves)
    {
        if (!sameName(levelName, k.level)) continue;

        std::string error;
        if (level.moveTrigger(k.fromRoom, k.fromSx, k.fromSz,
                              k.toRoom, k.toSx, k.toSz, error))
        {
            ++applied;
            continue;
        }

        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d sector %d,%d): %s",
                      k.reason, k.toRoom, k.toSx, k.toSz, error.c_str());
        warnings.push_back(buf);
    }

    for (const TriggerRewrite& k : kTriggerRewrites)
    {
        if (!sameName(levelName, k.level)) continue;

        std::vector<Tr5TriggerCmd> cmds;
        Tr5TriggerCmd c;
        c.type = k.cmdType;
        c.param = k.cmdParam;
        cmds.push_back(c);

        std::string error;
        if (level.setTrigger(k.room, k.sx, k.sz, k.kind, 0, k.oneShot,
                             k.mask, cmds, error))
        {
            ++applied;
            if (freedItems && k.freesItem >= 0) freedItems->push_back(k.freesItem);
            continue;
        }

        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d sector %d,%d): %s",
                      k.reason, k.room, k.sx, k.sz, error.c_str());
        warnings.push_back(buf);
    }

    for (const TriggerObjectRemove& k : kTriggerObjectRemoves)
    {
        if (!sameName(levelName, k.level)) continue;

        std::string error;
        if (level.removeTriggerObject(k.room, k.sx, k.sz, k.item, error))
        {
            ++applied;
            continue;
        }

        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d sector %d,%d): %s",
                      k.reason, k.room, k.sx, k.sz, error.c_str());
        warnings.push_back(buf);
    }

    for (const SightWindow& k : kSightWindows)
    {
        if (!sameName(levelName, k.level)) continue;

        std::string error;
        if (level.addSightWindow(k.room, k.sx, k.sz, k.sx2, k.sz2, k.toRoom,
                                 error))
        {
            ++applied;
            continue;
        }

        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d sector %d,%d): %s",
                      k.reason, k.room, k.sx, k.sz, error.c_str());
        warnings.push_back(buf);
    }

    for (const TriggerOneShot& k : kTriggerOneShots)
    {
        if (!sameName(levelName, k.level)) continue;

        std::string error;
        std::vector<Tr5OneShotHit> hits;
        if (level.setTriggerOneShot(k.room, k.kind, k.slot, hits, error))
        {
            ++applied;
            continue;
        }

        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d): %s",
                      k.reason, k.room, error.c_str());
        warnings.push_back(buf);
    }

    for (const TriggerTimer& k : kTriggerTimers)
    {
        if (!sameName(levelName, k.level)) continue;

        std::string error;
        Tr5TimerHit hit;
        if (level.setTriggerTimer(k.room, k.sx, k.sz, k.kind, k.fromTimer,
                                  k.toTimer, hit, error))
        {
            ++applied;
            continue;
        }

        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "LevelFix not applied (%s -> room %d sector %d,%d): %s",
                      k.reason, k.room, k.sx, k.sz, error.c_str());
        warnings.push_back(buf);
    }

    return applied;
}

}

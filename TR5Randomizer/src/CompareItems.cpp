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
// Compares the item lists of both readers: our own Tr5Level against the old
// exporter's level-info JSON. Compared position by position - type, room and
// coordinates.
//
// Occasion: in rich1 the old exporter reported item 27 in room 15, our
// reader and trview show room 45. Since the whole curation runs over item
// indices, it must be settled whether that is an isolated case or a pattern.
//
// Usage: CompareItems.exe <data folder> <level-info folder>
#include "core/Json.h"
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <vector>

static std::wstring toW(const std::string& s)
{
    std::wstring w;
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("Usage: CompareItems.exe <data> <LevelInfos>\n"); return 2; }
    const std::string dataDir = argv[1], infoDir = argv[2];

    const char* names[] = {
        "Andrea1","Andrea2","Andrea3","andy1","Andy2","andy3","del",
        "joby2","joby3","joby4","joby5","rich1","rich2","rich3","richcut2","Title"
    };

    int levelsWithDiff = 0;
    for (const char* n : names)
    {
        Tr5Level lvl;
        std::string err;
        if (!lvl.load(toW(dataDir + "\\" + n + ".trc"), err))
        {
            std::printf("%-10s level not readable: %s\n", n, err.c_str());
            continue;
        }

        JsonValue info;
        if (!Json::parseFile(toW(infoDir + "\\" + std::string(n) + ".json"), info, err))
        {
            std::printf("%-10s JSON not readable: %s\n", n, err.c_str());
            continue;
        }

        const JsonValue& items = info["items"];
        const int nJson = static_cast<int>(items.size());
        const int nOurs = lvl.itemCount();

        int diffType = 0, diffRoom = 0, diffPos = 0, firstDiff = -1;
        const int count = (nJson < nOurs) ? nJson : nOurs;

        for (int i = 0; i < count; ++i)
        {
            Tr5Item a{};
            lvl.getItem(i, a);
            const JsonValue& b = items[i];

            const bool tDiff = a.objectNumber != static_cast<int>(b["typeId"].asInt());
            const bool rDiff = a.roomNumber != static_cast<int>(b["room"].asInt());
            const bool pDiff = a.x != static_cast<int>(b["x"].asInt())
                            || a.y != static_cast<int>(b["y"].asInt())
                            || a.z != static_cast<int>(b["z"].asInt());

            if (tDiff) ++diffType;
            if (rDiff) ++diffRoom;
            if (pDiff) ++diffPos;
            if ((tDiff || rDiff || pDiff) && firstDiff < 0) firstDiff = i;
        }

        const bool any = (diffType || diffRoom || diffPos || nJson != nOurs);
        if (any) ++levelsWithDiff;

        std::printf("%-10s items %3d/%-3d  type %3d  room %3d  pos %3d   %s\n",
                    n, nOurs, nJson, diffType, diffRoom, diffPos,
                    any ? "DEVIATION" : "identical");

        // On the first deviating item, show both versions - that usually
        // tells right away whether it is an offset or something else.
        if (firstDiff >= 0)
        {
            Tr5Item a{};
            lvl.getItem(firstDiff, a);
            const JsonValue& b = items[firstDiff];
            std::printf("           from index %d:  ours T%d R%d (%d,%d,%d)  |  JSON T%d R%d (%d,%d,%d)\n",
                        firstDiff,
                        a.objectNumber, a.roomNumber, a.x, a.y, a.z,
                        (int)b["typeId"].asInt(), (int)b["room"].asInt(),
                        (int)b["x"].asInt(), (int)b["y"].asInt(), (int)b["z"].asInt());
        }
    }

    std::printf("\nlevels with deviations: %d of 16\n", levelsWithDiff);
    return levelsWithDiff ? 1 : 0;
}

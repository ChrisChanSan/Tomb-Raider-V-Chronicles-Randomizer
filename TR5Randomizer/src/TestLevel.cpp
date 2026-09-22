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
// Checks our own TR5 reader against the real vanilla levels.
// Usage: TestLevel.exe <data folder with the .trc files>
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Usage: TestLevel.exe <data folder>\n"); return 2; }

    std::wstring dir;
    for (const char* p = argv[1]; *p; ++p) dir.push_back((wchar_t)(unsigned char)*p);
    if (!dir.empty() && dir.back() != L'\\') dir.push_back(L'\\');

    const char* names[] = {
        "Andrea1","Andrea2","Andrea3","andy1","Andy2","andy3","del",
        "joby2","joby3","joby4","joby5","rich1","rich2","rich3","richcut2","Title"
    };

    int ok = 0, fail = 0;
    for (const char* n : names)
    {
        std::wstring path = dir;
        for (const char* p = n; *p; ++p) path.push_back((wchar_t)(unsigned char)*p);
        path += L".trc";

        Tr5Level lvl;
        std::string err;
        if (!lvl.load(path, err))
        {
            std::printf("  [FAIL] %-10s %s\n", n, err.c_str());
            ++fail;
            continue;
        }

        // The main block should make up practically the whole remaining
        // file - nothing big comes after it; a strong deviation means the
        // header was misread.
        // Lists items whose trigger flags carry bits outside the lower six.
        // The old exporter masked with 0x3F, so 0x80 (regenerating pickup)
        // and 0x100 (mesh invisible) were never visible; this reader
        // delivers the field unmasked.
        int withHigh = 0;
        std::string detail;
        for (int i = 0; i < lvl.itemCount(); ++i)
        {
            Tr5Item it{};
            if (!lvl.getItem(i, it)) continue;
            const uint16_t raw = static_cast<uint16_t>(it.triggerFlags);
            if ((raw & ~0x3Fu) == 0) continue;
            ++withHigh;
            if (withHigh <= 6)
            {
                char buf[80];
                std::snprintf(buf, sizeof(buf), " %d(T%d,0x%04X)", i, it.objectNumber, raw);
                detail += buf;
            }
        }
        // Candidates for the stash room: no portal, no alternate room, and
        // not visible from anywhere through a portal.
        std::vector<int> seenFrom(lvl.rooms().size(), 0);
        for (const Tr5Room& r : lvl.rooms())
            for (int t : r.portalRooms)
                if (t >= 0 && t < (int)seenFrom.size()) seenFrom[t]++;

        std::string cand;
        int nCand = 0;
        for (const Tr5Room& r : lvl.rooms())
        {
            if (!r.portalRooms.empty()) continue;
            if (r.isAlternate()) continue;
            if (seenFrom[r.index]) continue;
            ++nCand;
            if (nCand <= 6) { char b[16]; std::snprintf(b, sizeof(b), " %d", r.index); cand += b; }
        }

        std::printf("  [%s] %-10s rooms %3d  high OCB %3d  portal errors %d  secluded rooms: %d%s\n",
                    lvl.roomParseMismatches() ? "FAIL" : "ok ",
                    n, lvl.roomCount(), withHigh, lvl.roomParseMismatches(), nCand, cand.c_str());
        if (lvl.roomParseMismatches()) ++fail; else ++ok;
    }

    std::printf("\n%d read, %d failed\n", ok, fail);
    return fail ? 1 : 0;
}

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
// Prueft den eigenen TR5-Leser gegen die echten Vanilla-Level.
// Aufruf: TestLevel.exe <data-Ordner mit den .trc>
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Aufruf: TestLevel.exe <data-Ordner>\n"); return 2; }

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
            std::printf("  [FEHL] %-10s %s\n", n, err.c_str());
            ++fail;
            continue;
        }

        // Der Hauptblock sollte praktisch die gesamte Restdatei ausmachen -
        // dahinter kommt nichts Grosses mehr. Weicht das stark ab, haben wir
        // den Kopfteil falsch gedeutet.
        // R8: welche Items tragen Bits AUSSERHALB der unteren sechs?
        // Der alte Export maskierte mit 0x3F, dadurch waren 0x80
        // (regenerierendes Pickup) und 0x100 (Mesh unsichtbar) nicht sichtbar.
        // Unser Leser liefert das Feld vollstaendig - die Frage laesst sich
        // also einfach beantworten, statt sie zu vermuten.
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
        // Kandidaten fuer den Ablageraum: kein Portal, kein Alternate-Raum,
        // und von nirgendwo aus ueber ein Portal einsehbar.
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

        std::printf("  [%s] %-10s Raeume %3d  OCB-hoch %3d  Portalfehler %d  abgeschiedene Raeume: %d%s\n",
                    lvl.roomParseMismatches() ? "FEHL" : "ok ",
                    n, lvl.roomCount(), withHigh, lvl.roomParseMismatches(), nCand, cand.c_str());
        if (lvl.roomParseMismatches()) ++fail; else ++ok;
    }

    std::printf("\n%d gelesen, %d fehlgeschlagen\n", ok, fail);
    return fail ? 1 : 0;
}

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
//----------------------------------------------------------------------------
// Durchstich: aus einem Seed eine fertige randomisierte Leveldatei.
//
// Aufruf:
//   Randomize.exe <zonen.json> <levelinfo.json> <quelle.trc> <ziel.trc> [seed]
//
// Spaeter treibt die Oberflaeche dieselben Schritte an; dieses Programm ist
// der Nachweis, dass die Kette vollstaendig ist und laesst sich zum Pruefen
// einzelner Level behalten.
//----------------------------------------------------------------------------
#include "core/ApplyPatch.h"
#include "core/Json.h"
#include "core/Randomizer.h"
#include "core/Rng.h"
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("Aufruf: Randomize.exe <zonen.json> <levelinfo.json> <quelle.trc> <ziel.trc> [seed]\n");
        return 2;
    }

    const std::string seedText = (argc > 5) ? argv[5] : "Weihnachten";
    const uint64_t seed = SeedText::parse(seedText);

    std::string err;

    JsonValue zone, info;
    if (!Json::parseFile(toW(argv[1]), zone, err)) { std::printf("Zonen-Export: %s\n", err.c_str()); return 1; }
    if (!Json::parseFile(toW(argv[2]), info, err)) { std::printf("Level-Info: %s\n", err.c_str()); return 1; }

    RandoInput in;
    if (!Rando::buildInput(zone, info, in, err)) { std::printf("Eingabe: %s\n", err.c_str()); return 1; }

    JsonValue patch;
    RandoReport rep;
    if (!Rando::run(in, RandoOptions(), seed, patch, rep))
    {
        std::printf("Platzierung fehlgeschlagen: %s\n", rep.error.c_str());
        return 1;
    }

    Tr5Level lvl;
    if (!lvl.load(toW(argv[3]), err)) { std::printf("Level laden: %s\n", err.c_str()); return 1; }
    const int itemsBefore = lvl.itemCount();

    PatchReport pr;
    if (!Patch::apply(lvl, patch, pr, err)) { std::printf("Patch: %s\n", err.c_str()); return 1; }
    for (const std::string& w : pr.warnings) std::printf("  Hinweis: %s\n", w.c_str());

    if (!lvl.save(toW(argv[4]), err)) { std::printf("Speichern: %s\n", err.c_str()); return 1; }

    std::printf("Level %s\n", zone["level"].asString("?").c_str());
    std::printf("  Seed        %s  (0x%016llX)\n", seedText.c_str(), (unsigned long long)seed);
    std::printf("  Platzierung Key %d, sonstige %d, ortsfest %d, injiziert %d\n",
                rep.movedKeyItems, rep.movedOtherItems, rep.pinnedItems, rep.injected);
    std::printf("  Patch       %d geaendert, %d hinzugefuegt\n", pr.edited, pr.added);
    std::printf("  Items       %d -> %d\n", itemsBefore, lvl.itemCount());

    // Gegenprobe: das Ergebnis neu einlesen und pruefen, dass die Aenderungen
    // wirklich in der Datei stehen und die Struktur weiter aufgeht. Ohne
    // diesen Schritt wuesste man nur, dass das Schreiben nicht abgestuerzt ist.
    Tr5Level check;
    if (!check.load(toW(argv[4]), err))
    {
        std::printf("  [FEHL] Ergebnis ist nicht mehr lesbar: %s\n", err.c_str());
        return 1;
    }
    if (check.itemCount() != lvl.itemCount())
    {
        std::printf("  [FEHL] Item-Anzahl nach dem Neuladen: %d statt %d\n", check.itemCount(), lvl.itemCount());
        return 1;
    }

    const JsonValue& edits = patch["itemEdits"];
    int verified = 0, wrong = 0;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx = static_cast<int>(edits[i]["index"].asInt());
        Tr5Item it{};
        if (!check.getItem(idx, it)) { ++wrong; continue; }
        if (it.roomNumber != static_cast<int16_t>(edits[i]["room"].asInt()) ||
            it.x != static_cast<int32_t>(edits[i]["x"].asInt()) ||
            it.y != static_cast<int32_t>(edits[i]["y"].asInt()) ||
            it.z != static_cast<int32_t>(edits[i]["z"].asInt()) ||
            (it.triggerFlags & 0x3F) != 0)
        {
            ++wrong;
            continue;
        }
        ++verified;
    }
    std::printf("  Gegenprobe  %d Positionen bestaetigt, %d abweichend\n", verified, wrong);

    return wrong ? 1 : 0;
}

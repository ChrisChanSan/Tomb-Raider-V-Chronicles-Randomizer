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
#pragma once
#include "Tr5Level.h"

#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Levelfehler von Core, die bei JEDEM Lauf repariert werden.
//
// Abgrenzung zur uebrigen Kuration: die Zonen- und Range-Tabellen in
// ZoneAnalyse.ps1 sagen, WO der Zufall etwas hinlegen darf - sie arbeiten mit
// dem Level, wie es ist. Hier steht das Gegenteil: Stellen, an denen das Level
// SELBST kaputt ist, unabhaengig vom Wuerfeln. Ein solcher Fehler laesst sich
// nicht umkurieren, er muss weg.
//
// Bewusst eine kurze, handgepflegte Liste und kein Regelwerk. Jeder Eintrag
// steht fuer einen Fall, den jemand im Spiel gesehen hat, und traegt seine
// Begruendung mit.
//----------------------------------------------------------------------------
namespace LevelFix
{
    // Ein Trigger, der auf einem zweiten Weg fehlt. Der Zielsektor bekommt
    // eine gleichlautende Kopie, die Quelle bleibt unberuehrt.
    struct TriggerCopy
    {
        const char* level;                  // Dateiname ohne Endung
        int fromRoom, fromSx, fromSz;
        int toRoom, toSx, toSz;
        const char* reason;
    };

    //------------------------------------------------------------------------
    // joby5: Der Bodentrigger, der die Electrical Cables 91 und 136 scharf
    // schaltet, liegt in Raum 138 auf Sektor (1,1) - und nur dort. Der Jump
    // Switch 190 in Raum 165 schaltet den Strom wieder ab; sein Antitrigger
    // wirkt aber nur auf Kabel, die BEREITS scharf sind.
    //
    // Ein Tester ist 2026-08-22 auf dem anderen Weg zum Schalter gekommen,
    // ohne Raum 138 zu betreten. Damit lief der Antitrigger VOR dem Trigger:
    // erst schaltete er nichts ab, danach machte der Bodentrigger die Kabel
    // scharf - und sie blieben es fuer den Rest des Levels. Das Wasser in
    // 126, 127, 128, 158 und 183 ist dann dauerhaft toedlich (10 Trefferpunkte
    // je Bild, game/joby.cpp ControlElectricalCables).
    //
    // Die Kopie auf Raum 122 Sektor (1,1) deckt diesen zweiten Weg ab: dort
    // kommt Lara in jedem Fall vorbei, bevor sie den Schalter erreicht.
    //
    // Warum das mehr ist als Bequemlichkeit: die Kuration erlaubt in genau
    // diesen Wasserraeumen ausdruecklich PUZZLE-Items (Nutzerentscheidung,
    // NO_KEY_ROOMS in ZoneAnalyse.ps1: "Die Puzzels koennen wir lassen, nur
    // Keys sind wichtig") - mit der Begruendung, dass sie erst am Ende
    // gebraucht werden, wenn der Strom laengst aus ist. Genau diese Annahme
    // bricht der Testerfall. Schluessel sind ohnehin gesperrt; die
    // Puzzle-Erlaubnis wird durch die Kopie wieder tragfaehig, statt sie
    // zuruecknehmen zu muessen.
    //
    // An der Datei nachgemessen (SektorPruef): Raum 138 hat genau EINEN
    // Trigger, auf (1,1), mit den Objektbefehlen 91 und 136. Raum 122 ist
    // 7x3 Sektoren gross, (1,1) ist begehbarer Boden ohne eigene Kette -
    // die Kopie legt sich also auf einen freien Sektor.
    //------------------------------------------------------------------------
    static const TriggerCopy kTriggerCopies[] =
    {
        { "joby5", 138, 1, 1, 122, 1, 1,
          "Kabeltrigger (91/136) auch auf dem zweiten Weg zum Jump Switch 190" },
    };

    // Alle Eintraege fuer dieses Level apply. Fehlgeschlagene Eintraege
    // landen als Warnung in "warnings"; der Lauf geht weiter, denn ein
    // nicht angewandter Fix macht das Level nicht schlechter als vorher.
    // Rueckgabe: Zahl der uebernommenen Fixes.
    int apply(Tr5Level& level, const std::string& levelName,
                 std::vector<std::string>& warnings);
}

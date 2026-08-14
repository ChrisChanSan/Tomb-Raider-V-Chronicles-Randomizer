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
#include <string>

//----------------------------------------------------------------------------
// Einstellungen, die zwischen zwei Starts erhalten bleiben.
//
// Bewusst eine Textdatei neben der EXE und keine Registry: das Werkzeug soll
// tragbar sein - Ordner kopieren, laeuft. Ausserdem kann der Nutzer
// hineinsehen und notfalls von Hand korrigieren, was bei einem Pfadproblem
// deutlich freundlicher ist als ein Registrierungseditor.
//----------------------------------------------------------------------------
struct Config
{
    std::wstring sourceDir;   // data-Ordner der eigenen Chronicles-Installation
    std::string  seedText;    // zuletzt benutzter Seed, als Text

    // Fenstergroesse beim letzten Beenden bzw. nach dem letzten Ziehen.
    // 0 heisst "noch nie gespeichert" - dann gilt die Vorgabegroesse.
    int winW = 0, winH = 0;

    // Die Wuerfel-Optionen aus Abschnitt 3, damit der Nutzer seine Auswahl
    // beim naechsten Start wiederfindet. Vorgaben identisch mit denen der
    // Oberflaeche - fehlt ein Schluessel in der Datei, aendert sich nichts.
    bool optOtherPickups = true;
    bool optKeyItems     = true;
    bool optEnemies      = false;
    int  enemyCount      = 6;
    bool optEnemySwap    = false;
    int  enemyPool       = 0;     // 0 Levelpool, 1 Cross-Mix, 2 Allsanity
    bool optOutfits      = false;
    int  secrets         = 0;     // Platzhalter (#53): 0 Aus .. 3 Custom

    // Liest bzw. schreibt <exe-Ordner>\TR5Randomizer.cfg. Fehlt die Datei,
    // bleibt alles auf Vorgabe - das ist kein Fehler, sondern der erste Start.
    void load();
    bool save() const;

    static std::wstring path();
};

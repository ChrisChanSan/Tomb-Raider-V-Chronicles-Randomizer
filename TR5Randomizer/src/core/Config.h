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

    // Liest bzw. schreibt <exe-Ordner>\TR5Randomizer.cfg. Fehlt die Datei,
    // bleibt alles leer - das ist kein Fehler, sondern der erste Start.
    void load();
    bool save() const;

    static std::wstring path();
};

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
// Balance.txt - die Einstellungsdatei der Engine, die neben der PCtomb5.exe
// liegt (Aufgabe #108).
//
// Der Randomizer fasst darin AUSSCHLIESSLICH die drei Prozentzeilen an, die
// seine Regler bedienen. Alles andere - der Schaden je Waffe, die
// Lebenspunkte je Gegner, der Schaden je Angriff, die Levelgefahren - bleibt
// unberuehrt, damit die Handarbeit des Spielers eine Runde Wuerfeln
// ueberlebt. Genau dafuer sind die Prozentzeilen in der Engine da: sie wirken
// zusaetzlich auf die Einzelwerte.
//
// Fehlt die Datei, wird eine kurze mit nur diesen drei Zeilen geschrieben.
// Das Spiel traegt beim naechsten Start alles Uebrige nach - das kann es seit
// bcb3286 von allein.
//----------------------------------------------------------------------------
namespace BalanceDatei
{
    // Alle drei Werte in Prozent. 100 = unveraendert.
    struct Werte
    {
        int waffenSchaden = 100;
        int gegnerLeben   = 100;
        int gegnerSchaden = 100;
    };

    // Die drei Zeilen in <spielOrdner>\Balance.txt setzen. Vorhandene Zeilen
    // werden an Ort und Stelle ersetzt, fehlende hinten angehaengt, der Rest
    // der Datei bleibt Zeichen fuer Zeichen stehen.
    //
    // Rueckgabe false nur bei einem echten Schreibfehler; fehlerText traegt
    // dann den Grund.
    bool schreiben(const std::wstring& spielOrdner, const Werte& w,
                   std::string* fehlerText = nullptr);

    // Was gerade in der Datei steht. Fehlt sie oder eine Zeile darin, gilt
    // 100 - dasselbe, was die Engine annimmt.
    Werte lesen(const std::wstring& spielOrdner);
}

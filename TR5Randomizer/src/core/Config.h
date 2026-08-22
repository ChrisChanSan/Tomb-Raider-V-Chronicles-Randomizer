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
#include <set>
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

    // Fremdinstallationen fuer die Gegner aus Tomb Raider 3 (#102).
    //
    // Wir liefern KEINE Spieldaten mit - wer die Dinosaurier haben will, muss
    // die Spiele besitzen. Aus ihnen holt der Lauf zur Laufzeit Modell,
    // Texturen und Klaenge; ohne Pfad bleiben die Arten in der Oberflaeche
    // ausgegraut.
    //
    //   tr3Dir     data-Ordner von Tomb Raider 3 - CRASH.TR2 traegt T-Rex,
    //              Raptor und Compsognathus, MAIN.SFX die Klaenge dazu.
    //   tr3GoldDir data-Ordner von The Lost Artifact - chunnel.TR2 traegt
    //              den Geier.
    std::wstring tr3Dir;
    std::wstring tr3GoldDir;

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
    // Abgewaehlte Gegnerarten (#95). Gespeichert wird die ABWAHL, damit eine
    // spaeter hinzukommende Art nicht durch alte Dateien stumm ausfaellt.
    std::set<int> gegnerAus;
    bool optOutfits      = false;
    int  itemMenge       = 0;     // 0 Vanilla, 1 mehr, 2 viel mehr (#36)
    int  secrets         = 0;     // Secrets (#53): 0 Vanilla, 1 Pool, 2/3 Liste
    std::string secretAutor;      // gewaehlter Listenautor (Art 2/3)
    bool optMusikSwitch  = false; // Levelmusik in der SCRIPT.DAT wuerfeln
    bool optMusikTrigger = false; // TO_CD-Tracks der Floordata wuerfeln
    bool optLevelAbschnitte = false;  // die vier Story-Abschnitte permutieren
    int  optLevelFolge   = 0;     // 0 aus, 1 innerhalb der Abschnitte, 2 frei ueber alles

    // Die drei Regler fuer Balance.txt (#108), in Prozent. Gespeichert wird
    // hier nur die Reglerstellung; die Wahrheit steht in Balance.txt neben
    // der PCtomb5.exe und wird beim Start von dort gelesen.
    int  balWaffen        = 100;
    int  balGegnerLeben   = 100;
    int  balGegnerSchaden = 100;

    // Liest bzw. schreibt <exe-Ordner>\TR5Randomizer.cfg. Fehlt die Datei,
    // bleibt alles auf Vorgabe - das ist kein Fehler, sondern der erste Start.
    void load();
    bool save() const;

    static std::wstring path();
};

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
// Entnahme aus einem Tomb-Raider-3-Level (#92).
//
// WARUM DAS HIER LIEGT UND NICHT IN EINEM WERKZEUG
//
// Dieses Projekt liefert keine Spieldaten mit. Wer die Dinosaurier haben
// will, muss Tomb Raider 3 besitzen - und dann muss der Lauf sie SICH SELBST
// aus seiner Installation holen, auf seinem Rechner, waehrend er laeuft.
// Solange die Entnahme nur in einem Standalone-Werkzeug steckt, muessten wir
// fertige Pakete verteilen, und die enthalten fremde Spieldaten. Deshalb ist
// das Verlegen dieser Funktionen in den Kern keine Aufraeumarbeit, sondern
// die Bedingung, unter der die Dinosaurier ueberhaupt ausgeliefert werden
// duerfen. Siehe [[project-lizenz-auslieferung]].
//
// Die Gegenrichtung - Einsetzen in ein TR5-Level - liegt bereits im Kern:
// Tr5Level::addMoveableFromTr3, addSoundsFromTr3, addTexturesFromTr3.
//
// Die Werkzeuge TR3Hol und TR3Ton rufen nach dem Port nur noch diese
// Funktionen auf. Es darf keinen zweiten Ableser geben: die Formatbruecke
// (Einachs-Drehungen 10 Bit -> 12 Bit, Sprungtabelle mit GLOBALEN
// Anim-Indizes, Faces mit TR5-Effektwort, farbige Faces auf eine echte
// Textur) ist an genau einer Stelle richtig, und die soll es bleiben.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Tr3Quelle
{
    // Holt Modell, Knochen, Animationen, Frames und Texturen eines Objekts
    // aus einem TR3-Level und legt sie im Paketformat ab (Version 2, dasselbe,
    // das Tr5Level::addMoveableFromTr3 und addTexturesFromTr3 erwarten).
    //
    // Alle Formatumrechnungen passieren hier - das Paket ist danach
    // TR5-tauglich. Geprueft wird dabei: jedes Mesh muss aufs Byte genau
    // enden, kein Frame-Zeiger und kein Sprungziel darf die eigene Scheibe
    // verlassen. Schlaegt eine Pruefung fehl, wird nichts geliefert.
    bool holeMoveable(const std::wstring& levelPfad,
                      int objektId,
                      std::vector<uint8_t>& paket,
                      std::string& fehler);

    // Ein Klang je Eintrag, mit ausdruecklich angegebenen Wave-Nummern aus
    // MAIN.SFX. Mehrere Wellen sind Varianten desselben Klangs.
    //
    // Die Wave-Nummern werden ABSICHTLICH von aussen gereicht statt aus den
    // Leveltabellen aufgeloest: bei den Gold-Leveln liefert der Weg ueber
    // SoundMap/SoundDetails/SampleIndices nachweislich falsche Wellen, und die
    // Selbstkalibrierung merkt es nicht. Die Nummern stammen aus dem Werkzeug
    // TRsound und sind vom Nutzer abgehoert.
    struct KlangWunsch { int tr3Id; std::vector<int> wellen; };

    // Schneidet die gewuenschten Wellen aus MAIN.SFX und legt sie im
    // Klangpaketformat ab (dasselbe, das addSoundsFromTr3 erwartet).
    //
    // ACHTUNG: The Lost Artifact bringt eine EIGENE MAIN.SFX mit, die sich von
    // der aus Tomb Raider 3 unterscheidet (9820544 gegen 11646918 Byte). Die
    // abgehoerten Wave-Nummern des Geiers beziehen sich auf die von TR3 -
    // welche Datei gemeint ist, entscheidet der Aufrufer, nicht diese
    // Funktion.
    bool holeKlaenge(const std::wstring& sfxPfad,
                     const std::vector<KlangWunsch>& wuensche,
                     std::vector<uint8_t>& paket,
                     std::string& fehler);

    // Schreibt die SFX-Nummern der Anim-Kommandos im Modellpaket um.
    //
    // "zuordnung" bildet TR3-Nummer auf Zielplatz ab. Nummern, die gerufen
    // werden und in keiner der beiden Listen stehen, landen in "offen" - der
    // Aufrufer MUSS daraufhin abbrechen. Eine stehen gebliebene TR3-Nummer
    // trifft im Ziel irgendeinen Vanilla-Klang; beim Compsognathus wurde aus
    // 243 SFX_GENERIC_HEAVY_FEET, und das zu finden hat Stunden gekostet.
    //
    // "stumm" nennt Nummern, die bewusst nicht mitkommen: sie wandern auf
    // einen leeren Platz der Soundmap, wo die Engine aussteigt statt etwas
    // Fremdes zu spielen.
    bool biegeSfxUm(std::vector<uint8_t>& paket,
                    const std::vector<std::pair<int,int>>& zuordnung,
                    const std::vector<int>& stumm,
                    int stummPlatz,
                    std::vector<int>& offen,
                    std::string& fehler);
}

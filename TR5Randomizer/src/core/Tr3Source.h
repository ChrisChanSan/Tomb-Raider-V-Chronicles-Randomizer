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
// Funktionen auf. Es darf keinen zweiten Ableser give: die Formatbruecke
// (Einachs-Drehungen 10 Bit -> 12 Bit, Sprungtabelle mit GLOBALEN
// Anim-Indizes, Faces mit TR5-Effektwort, farbige Faces auf eine echte
// Textur) ist an genau einer Stelle richtig, und die soll es bleiben.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Tr3Source
{
    // Holt Modell, Knochen, Animationen, Frames und Texturen eines Objekts
    // aus einem TR3-Level und legt sie im Paketformat ab (Version 2, dasselbe,
    // das Tr5Level::addMoveableFromTr3 und addTexturesFromTr3 erwarten).
    //
    // Alle Formatumrechnungen passieren hier - das Paket ist danach
    // TR5-tauglich. Geprueft wird dabei: jedes Mesh muss aufs Byte genau
    // enden, kein Frame-Zeiger und kein Sprungziel darf die eigene Scheibe
    // verlassen. Schlaegt eine Pruefung fehl, wird nichts geliefert.
    bool fetchMoveable(const std::wstring& levelPath,
                      int objektId,
                      std::vector<uint8_t>& package,
                      std::string& error);

    // Ein Klang je Eintrag, mit ausdruecklich angegebenen Wave-Nummern aus
    // MAIN.SFX. Mehrere Wellen sind Varianten desselben Klangs.
    //
    // Die Wave-Nummern werden ABSICHTLICH von aussen gereicht statt aus den
    // Leveltabellen aufgeloest: bei den Gold-Leveln liefert der Weg ueber
    // SoundMap/SoundDetails/SampleIndices nachweislich falsche Wellen, und die
    // Selbstkalibrierung merkt es nicht. Die Nummern stammen aus dem Werkzeug
    // TRsound und sind vom Nutzer abgehoert.
    struct SoundRequest { int tr3Id; std::vector<int> waves; };

    // Schneidet die gewuenschten Wellen aus MAIN.SFX und legt sie im
    // Klangpaketformat ab (dasselbe, das addSoundsFromTr3 erwartet).
    //
    // ACHTUNG: The Lost Artifact bringt eine EIGENE MAIN.SFX mit, die sich von
    // der aus Tomb Raider 3 unterscheidet (9820544 gegen 11646918 Byte). Die
    // abgehoerten Wave-Nummern des Geiers beziehen sich auf die von TR3 -
    // welche Datei gemeint ist, entscheidet der Aufrufer, nicht diese
    // Funktion.
    bool fetchSounds(const std::wstring& sfxPath,
                     const std::vector<SoundRequest>& requests,
                     std::vector<uint8_t>& package,
                     std::string& error);

    // Ein Gast aus Tomb Raider 3, vollstaendig beschrieben (#92).
    //
    // Das ist die EINZIGE Wahrheit ueber die vier Dinosaurier - Randomizer und
    // Patchweg read beide hier. Frueher standen dieselben Angaben verstreut
    // in Aufrufzeilen der Werkzeuge, und genau daran ist mir beim Compy die
    // halbe Klangzuordnung durchgerutscht.
    struct DinoSource
    {
        int         slot;        // Zielslot in TR5 - in KEINEM Level belegt
        const char* name;
        const char* source;      // "tr3" oder "tr3gold": welche Installation
        const char* file;       // Levelname darin
        int         object;      // Objektnummer in dieser Datei
        int         soundBase;  // erster Zielplatz in der Soundmap
        std::vector<SoundRequest> sounds;
        std::vector<int>         muted;   // bewusst nicht mitgenommene Nummern
        std::vector<int>         volumes;    // Prozent je Klang, in Paketreihenfolge
    };

    // Die vier Gaeste. Klangplaetze sind kollisionsfrei verteilt und im Level
    // "Jurassic Raider" gemeinsam erprobt: Geier 420-423, Compy 430-434,
    // Raptor 438-444, T-Rex 445-449, Stummes auf 411.
    std::vector<DinoSource> dinos();

    // Welche MAIN.SFX gilt fuer diesen Gast? ACHTUNG, nicht offensichtlich:
    // der Geier steckt zwar in The Lost Artifact, seine Klaenge liegen aber in
    // der MAIN.SFX von Tomb Raider 3. Gold bringt eine eigene mit, die sich
    // unterscheidet (9820544 gegen 11646918 Byte) - die abgehoerten
    // Wave-Nummern beziehen sich auf die von TR3.
    std::wstring sfxPath(const DinoSource& d,
                         const std::wstring& tr3Dir, const std::wstring& tr3GoldDir);

    // Und wo liegt sein Level?
    std::wstring levelPath(const DinoSource& d,
                           const std::wstring& tr3Dir, const std::wstring& tr3GoldDir);

    // Holt Modell und Klaenge eines Gastes - mit ZWISCHENLAGER (#103).
    //
    // Die Entnahme kostet je Aufruf rund elf Sekunden: sie liest den TR3-Level
    // (3,3 MB) und MAIN.SFX (11,6 MB) neu ein. Bei dreizehn Leveln wird
    // derselbe T-Rex also dreizehnmal entnommen. Einmal reicht.
    //
    // Liegt "cacheDir" vor, werden die fertigen Pakete dort abgelegt und beim
    // naechsten Mal wiederverwendet. Das ist lizenzrechtlich unbedenklich: die
    // Dateien entstehen auf dem Rechner dessen, der die Spiele besitzt, und
    // bleiben dort. AUSGELIEFERT WIRD DIESER ORDNER NIE - er traegt eine
    // Warnung als Textdatei, damit das auch beim Packen auffaellt.
    //
    // Erkannt wird ein veralteter Eintrag an Groesse und Aenderungszeit der
    // Quelldatei. Wer sein Spiel neu installiert oder patcht, bekommt
    // automatisch eine frische Entnahme statt eines stillen Fehlers.
    bool fetchCached(const DinoSource& d,
                      const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                      const std::wstring& cacheDir,
                      std::vector<uint8_t>& modell, std::vector<uint8_t>& sounds,
                      std::string& error);

    // Biegt die Texturworte der Faces auf die neuen Texinfos um. Muss NACH
    // Tr5Level::addTexturesFromTr3 laufen (das liefert die Zuordnung) und VOR
    // addMoveableFromTr3. Rueckgabe: Anzahl umgebogener Worte, -1 bei Fehler.
    int remapTextures(std::vector<uint8_t>& package, const std::map<int,int>& mapping);

    // Schreibt die SFX-Nummern der Anim-Kommandos im Modellpaket um.
    //
    // "mapping" bildet TR3-Nummer auf Zielplatz ab. Nummern, die gerufen
    // werden und in keiner der beiden Listen stehen, landen in "unresolved" - der
    // Aufrufer MUSS daraufhin abbrechen. Eine stehen gebliebene TR3-Nummer
    // trifft im Ziel irgendeinen Vanilla-Klang; beim Compsognathus wurde aus
    // 243 SFX_GENERIC_HEAVY_FEET, und das zu finden hat Stunden gekostet.
    //
    // "muted" nennt Nummern, die bewusst nicht mitkommen: sie wandern auf
    // einen leeren Platz der Soundmap, wo die Engine aussteigt statt etwas
    // Fremdes zu spielen.
    bool remapSfx(std::vector<uint8_t>& package,
                    const std::vector<std::pair<int,int>>& mapping,
                    const std::vector<int>& muted,
                    int muteSlot,
                    std::vector<int>& unresolved,
                    std::string& error);
}

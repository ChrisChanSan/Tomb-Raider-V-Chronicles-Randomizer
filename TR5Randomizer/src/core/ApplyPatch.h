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
#include "Json.h"
#include "Randomizer.h"
#include "Tr5Level.h"

#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Traegt einen Patch (itemEdits / itemAdds / triggerMoves) in ein geladenes
// Level ein.
//
// Das Patch-Format ist dasselbe, das die Platzierungslogik erzeugt:
//   { "itemEdits":    [ {index, room?, x?, y?, z?, ocb?}, ... ],
//     "itemAdds":     [ {typeId, room, x, y, z, ocb?}, ... ],
//     "triggerMoves": [ {fromRoom, fromSectorX, fromSectorZ,
//                        toRoom,   toSectorX,   toSectorZ}, ... ],
//     "triggerAdds":  [ {room, sectorX, sectorZ, art?, timer?, oneShot?, mask?,
//                        commands: [ {type, param}, ... ]}, ... ],
//     "soundAdds":    [ {map, fromLevel, fromMap}, ... ] }
//
// Bei den Edits sind alle Felder ausser index optional - was fehlt, bleibt
// unveraendert. Das ist der Grund, warum ein verschobenes Item seinen Winkel
// und seine Beleuchtung behaelt: wir schreiben nur, was wir wirklich meinen.
//
// triggerMoves nimmt den Triggereintrag des Quellsektors heraus und haengt ihn
// an die Kette des Zielsektors - ein Vorgang, nicht zwei. Gedacht ist es fuer
// den gekoppelten Zug: wandert ein Item, dessen Pickup-Trigger noch etwas
// anderes ausloest, wandert der Trigger mit, damit die Wirkung erhalten bleibt.
//
// triggerAdds setzt NEUE Trigger. Das ist der Weg, dem Level Gegner
// hinzuzufuegen: erst das Item per itemAdds anhaengen, dann einen Trigger
// darauf setzen. Vorgaben mit Absicht: art 0 (TRIGGER), Maske 31 und oneShot
// true sind die Standardwerte, weil genau das ein "wecke diesen Gegner"-Trigger
// ist. oneShot ist dabei kein Feinschliff - siehe Tr5Level::addTrigger.
//
// Weder itemRemoves noch triggerRemoves gibt es, und beides soll es nicht
// geben - siehe Tr5Level::addItem und die Begruendung in apply().
//----------------------------------------------------------------------------
// soundAdds uebernimmt Sound-Samples aus einem anderen Level. Gebraucht, sobald
// ein Gegner in ein Level soll, das ihn vanilla nicht enthaelt: ein Level fuehrt
// nur die Samples mit, die es selbst braucht, und ohne sie steht der Gegner
// stumm da. Der Quellpfad kommt vom Aufrufer, weil nur der weiss, wo die
// unberuehrten Level liegen - siehe Patch::apply's levelSource.
// weaponAdds kopiert Waffen-Modellslots aus anderen Leveln:
//   "weaponAdds": [ {slot, fromLevel, replace}, ... ]
// replace false heisst "nur was fehlt" - hat das Level den Slot schon, bleibt
// er unangetastet (so behaelt rich2 seine HK-Spezialpose). Die Liste kommt
// fertig aufgeloest aus dem Randomizer; die Spenderregeln stehen dort.
// Ausgefuehrt wird NACH dem Outfit, damit die Holsterslots des
// Outfit-Spenders nicht wieder ueberschrieben werden.
struct PatchReport
{
    int edited = 0;
    int added = 0;
    int triggersMoved = 0;
    int triggersAdded = 0;
    int soundsCopied = 0;
    int weaponsCopied = 0;               // uebernommene Waffenslots
    int moveablesCopied = 0;             // uebernommene Cross-Gegner-Slots
    std::string outfitFrom;              // leer = Outfit unveraendert
    std::vector<std::string> warnings;
};

// Woher Quell-Level fuer soundAdds kommen. Bekommt den Namen aus "fromLevel"
// und liefert den vollen Pfad; leerer Rueckgabewert heisst "gibt es nicht".
typedef std::wstring (*LevelSourceFn)(const std::string& name, void* user);

namespace Patch
{
    // levelSource wird nur fuer soundAdds gebraucht. Ohne sie werden
    // vorhandene soundAdds als Warnung gemeldet und uebersprungen - der Patch
    // scheitert daran nicht, denn ein stummer Gegner ist kein kaputtes Level.
    bool apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error,
               LevelSourceFn levelSource = nullptr, void* levelSourceUser = nullptr);

    // Traegt in die Eingabe ein, welche Sektoren einen mitwandernden Trigger
    // aufnehmen koennen. Muss VOR Rando::run laufen, sonst waehlt die
    // Platzierung eine Stelle, an der apply() spaeter abbricht.
    //
    // Liegt hier und nicht in Randomizer.cpp, weil die Antwort nur in der
    // Floordata des echten Levels steht - und Randomizer.cpp bewusst nichts
    // von Tr5Level weiss (der Selbsttest baut ohne). Liegt auch nicht in
    // Tr5Level, weil das nichts von RandoInput wissen soll. Dies ist die
    // Stelle, die ohnehin beide Seiten kennt.
    //
    // Ohne gekoppelte Items und ohne Gegnertypen tut sie nichts.
    void markTriggerSafeSectors(const Tr5Level& level, RandoInput& in);
}

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
#include <vector>

class Tr5Level;
class JsonValue;

//----------------------------------------------------------------------------
// Secret-Listen (#53): handgeschriebene Verstecke je Autor und Kategorie.
//
// Eine Datei = ein Dropdown-Eintrag, Dateiname Autor_Kategorie.txt im
// Ordner data\secrets. Die Syntax ist die der Kurationslisten (drei Formen
// plus Make_Portal, dokumentiert im Kopf von ChrisChanSan_Glitchless.txt);
// dieselbe Grammatik liest auch SecretPlan.ps1 - wer die Syntax aendert,
// aendert BEIDE Leser.
//
// Der Weg in den Lauf ist ein ZWEITER Patch-Durchgang: erst laeuft der
// normale Rando-Patch, dann wird aus der Liste gegen den ECHTEN Stand des
// Levels ein Secret-Patch gebaut (Positionen aus den Raumurspruengen der
// geladenen Datei, Itembasis aus ihrer tatsaechlichen Itemzahl) und mit
// derselben Patch::apply-Maschinerie angewandt. Ohne "itemBase" im Patch
// verschiebt apply nichts - die Neu<k>-Verweise stimmen damit exakt.
//----------------------------------------------------------------------------

struct SecretQuelle
{
    std::string  autor;
    std::string  kategorie;      // "Glitchless" oder "Glitched"
    std::wstring pfad;
};

// Alle Autor_Kategorie.txt eines Ordners. Dateien mit fuehrendem '_'
// (Referenzlisten) und unpassendem Namen werden uebersprungen.
std::vector<SecretQuelle> secretQuellenSuchen(const std::wstring& ordner);

// Baut aus der Zeile des Levels einen Patch (itemEdits/itemAdds/triggerAdds/
// portalAdds). Eine Zeile ohne neue Stellen und ohne Extras laesst den Patch
// leer - das Level bleibt vanilla, genau wie in der Vorlage vereinbart.
// Nicht verstandene Teile werden gemeldet, nie stillschweigend verschluckt.
//
// secretsVerschoben zaehlt NUR die umgezogenen Secrets selbst. Der Patch
// traegt daneben noch Beiwerk (Schluessel, Schalter, Umtypungen), und wer
// stattdessen edited+added des Reports anzeigt, meldet dem Nutzer "6 Secrets"
// in einem Level mit dreien - genau so in der Oberflaeche passiert.
bool secretPatchBauen(const std::wstring& listenPfad, const std::string& levelName,
                      const Tr5Level& lvl, JsonValue& patchOut,
                      int& secretsVerschoben,
                      std::vector<std::string>& warnungen, std::string& error);

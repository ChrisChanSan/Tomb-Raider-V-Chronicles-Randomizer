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
#include "Tr5Level.h"

#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Traegt einen Patch (itemEdits / itemAdds) in ein geladenes Level ein.
//
// Das Patch-Format ist dasselbe, das die Platzierungslogik erzeugt:
//   { "itemEdits": [ {index, room?, x?, y?, z?, ocb?}, ... ],
//     "itemAdds":  [ {typeId, room, x, y, z, ocb?}, ... ] }
//
// Bei den Edits sind alle Felder ausser index optional - was fehlt, bleibt
// unveraendert. Das ist der Grund, warum ein verschobenes Item seinen Winkel
// und seine Beleuchtung behaelt: wir schreiben nur, was wir wirklich meinen.
//
// Ein itemRemoves gibt es nicht und soll es nicht geben, siehe Tr5Level::addItem.
//----------------------------------------------------------------------------
struct PatchReport
{
    int edited = 0;
    int added = 0;
    std::vector<std::string> warnings;
};

namespace Patch
{
    bool apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error);
}

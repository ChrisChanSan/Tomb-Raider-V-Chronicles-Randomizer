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
// Secret lists (#53): hand-written hiding spots per author and category.
//
// One file = one dropdown entry, file name Author_Category.txt in the folder
// data\secrets. The syntax is that of the curation lists (three forms plus
// Make_Portal, documented at the top of ChrisChanSan_Glitchless.txt); the
// same grammar is also read by SecretPlan.ps1 - whoever changes the syntax
// changes BOTH readers.
//
// The way into the run is a SECOND patch pass: first the normal rando patch
// runs, then a secret patch is built from the list against the ACTUAL state
// of the level (positions from the room origins of the loaded file, item
// base from its real item count) and applied with the same Patch::apply
// machinery. Without "itemBase" in the patch, apply moves nothing - the
// New<k> references thus match exactly.
//----------------------------------------------------------------------------

struct SecretSource
{
    std::string  author;
    std::string  category;      // "Glitchless" or "Glitched"
    std::wstring path;
};

// All Author_Category.txt files of a folder. Files with a leading '_'
// (reference lists) and files with unsuitable names are skipped.
std::vector<SecretSource> findSecretSources(const std::wstring& folder);

// Builds a patch (itemEdits/itemAdds/triggerAdds/portalAdds) from the
// level's line. A line without new spots and without extras leaves the patch
// empty - the level stays vanilla, exactly as agreed in the template.
// Parts that are not understood are reported, never silently swallowed.
//
// secretsMoved counts ONLY the relocated secrets themselves. The patch also
// carries extras (keys, switches, retypings), and whoever displays the
// report's edited+added instead tells the user "6 secrets" in a level that
// has three - which is exactly what happened in the UI once.
bool buildSecretPatch(const std::wstring& listPath, const std::string& levelName,
                      const Tr5Level& lvl, JsonValue& patchOut,
                      int& secretsMoved,
                      std::vector<std::string>& warnings, std::string& error);

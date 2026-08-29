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
// SecretRewards.txt - what finding every secret in a level awards.
//
// The engine reads the file beside its executable (game/secretreward.cpp);
// this writes it. Keyed by LEVEL POSITION, not by file name, and that is the
// point: the level order is itself rolled, so a reward belongs to "the fourth
// level you play", not to a particular .trc. One run therefore writes all
// fourteen lines at once, out of the seed alone - which also makes it
// idempotent, since the command line runs once per level and each run would
// otherwise fight the last.
//----------------------------------------------------------------------------

// Writes the file into gameDir (the folder holding the executable). An
// existing file keeps its comments and any line this does not own.
// Returns false and fills error only when the file cannot be written.
bool writeSecretRewards(const std::wstring& gameDir, unsigned long long seed,
                        std::string& summary, std::string& error);

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

//----------------------------------------------------------------------------
// Shared identifier of randomizer, engine and curation data.
//
// A seed alone does not reproduce a level: the result depends on the
// placement logic, the bundled zone/curation data, and the engine that
// plays it. RANDO_BUILD_ID ties these three together, so a bug report for
// a given seed can be traced to the exact state that produced it.
//
// It is bumped on every change that can alter the outcome of a seed -
// including the curation tables in ZoneAnalysis.ps1, since a new ban list
// moves items. Pure UI changes do not count.
//
// The engine does not read or check this constant; nothing enforces that
// the two halves actually match. The id is still bumped and written into
// spoiler.txt, which keeps a bug report traceable.
//----------------------------------------------------------------------------
#define RANDO_BUILD_ID      19
#define RANDO_BUILD_STRING  "0.19"

// The published version, and a different thing from the build id above.
// That one counts revisions of the curation data and decides whether two
// runs can produce the same levels; this one is what a player calls the
// program they downloaded. Both belong in a bug report, so both are shown.
// The engine carries the same string in its version resource (tomb5.rc);
// the two source trees build separately, so they are kept in step by hand.
#define RANDO_VERSION_STRING "Beta_1.0"

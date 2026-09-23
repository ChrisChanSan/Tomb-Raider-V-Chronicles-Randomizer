//----------------------------------------------------------------------------
// TR5 Randomizer
// Copyright (C) 2026 ChrisChanSan
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See the LICENSE file at the root of this source tree.
//----------------------------------------------------------------------------

#pragma once

#include "Json.h"
#include "Tr5Level.h"

#include <string>

//----------------------------------------------------------------------------
// The level info - rooms, items and triggers - built from the .trc the
// randomizer opens anyway. Verified against all 16 vanilla levels: 2716
// rooms and 2094 items reproduce exactly, and of 2102 triggers the only
// differences are faults of the level_<name>.json export of TombItemEdit2026
// (see the refItem note in LevelInfo.cpp), whose shape the consuming code
// expects.
//
// "sectors" is not built here: it feeds ZoneAnalysis.ps1 at build time,
// and its validForItems field is a curated judgement (a hand-written
// exclusion list and a reachability call), so it stays as data.
//
// The .trc describes whatever that file currently is. On an already
// randomized level every typeId read here would be the swapped one and the
// run would silently decide on wrong data, so the GUI restores from Backup
// and checks the vanilla checksum before every run, and the CLI refuses a
// non-vanilla source (Randomize.cpp).
//----------------------------------------------------------------------------
namespace Rando
{
    // Builds { "rooms": [...], "items": [...], "triggers": [...] } from an
    // already loaded level. Field names and value conventions are exactly
    // those of that export, so buildInput() and findTriggerPinnedItems()
    // read it unchanged.
    bool levelInfoFromLevel(const Tr5Level& lvl, JsonValue& out, std::string& error);
}

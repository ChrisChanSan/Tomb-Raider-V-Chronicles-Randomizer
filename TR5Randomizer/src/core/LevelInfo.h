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
// The level info, built from the level file instead of read from disk.
//
// The randomizer used to read a "level_<name>.json" per level, produced by
// TombItemEdit2026, a separate program not shipped with it. Measuring what
// the randomizer actually took out of those files gave a short answer:
// three arrays - rooms, items, triggers - which together are 1.2% of the
// bytes. The other 98.8% (sectors, boxes, statics) is read by no line of
// this program.
//
// And those three are in the level file, which the randomizer opens
// anyway. Verified against all 16 vanilla levels: 2716 rooms and 2094
// items reproduce exactly, and of 2102 triggers the only differences are
// places where the export was wrong - see the refItem note below.
//
// So the file is not a source of knowledge, it is a copy of one. This
// builds the same structure the consuming code already expects, so
// nothing downstream needs a new shape.
//
// It does not reproduce "sectors". That array feeds ZoneAnalysis.ps1 at
// build time, and one of its fields - validForItems - is not a
// measurement but a curated judgement (a hand-written exclusion list and
// a reachability call). Recomputing a curated judgement means inventing
// it again, so that one stays as data.
//
// The old file described, by construction, the untouched level. Read from
// the .trc and it describes whatever that file currently is. Pointing the
// CLI at an already randomized level would make every typeId read here the
// swapped one, and the run would still succeed, silently deciding on wrong
// data. The GUI restores from Backup and checks the vanilla checksum
// before every run; the CLI must refuse a non-vanilla source, see
// Randomize.cpp.
//----------------------------------------------------------------------------
namespace Rando
{
    // Builds { "rooms": [...], "items": [...], "triggers": [...] } from an
    // already loaded level. Field names and value conventions are exactly
    // those of the old export, so buildInput() and findTriggerPinnedItems()
    // read it unchanged.
    bool levelInfoFromLevel(const Tr5Level& lvl, JsonValue& out, std::string& error);
}

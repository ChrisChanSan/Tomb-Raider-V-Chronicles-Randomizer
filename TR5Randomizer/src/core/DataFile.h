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

//----------------------------------------------------------------------------
// The single reader for the precomputed data: the zone exports
// (zones_*.json) and the two checksum lists (VanillaLevelChecksums.csv,
// Tr3Checksums.csv). They are files and nothing else - the package's data\
// folder beside the program is their one source, and the EXE carries no
// copy of them. A missing file is an error that names the file and the
// folder it was looked for in; the command line prints it, the GUI shows it
// in its status line or beside the level it stopped.
//
// One reader serves the command line, the GUI and the checksum check, so
// the rule and the message stand in one place.
//----------------------------------------------------------------------------
namespace DataFile
{
    // Reads the whole file at that path.
    bool read(const std::wstring& path, std::string& out, std::string& error);

    // Same, spelled as folder plus plain name.
    bool read(const std::wstring& dir, const std::string& name,
              std::string& out, std::string& error);

    // One line per read since program start: "zones_rich2.json  external
    // C:\...\data\zones_rich2.json", or "zones_rich2.json  MISSING  <folder>".
    // The middle word reads "external" as it did when the EXE still carried
    // a copy, so a spoiler.txt of any build reads the same. The command line
    // prints the lines as they happen, the GUI collects them into
    // spoiler.txt - a bug report is worth little without knowing which
    // curation the run actually used.
    const std::vector<std::string>& log();
}

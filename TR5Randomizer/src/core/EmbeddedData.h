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
#include <cstddef>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// The precomputed data, baked into the program.
//
// WHY. Until now the fifteen zone exports and the checksum list only existed
// as loose files in data\ beside the EXE. They are a build product, the
// folder is gitignored, and no repository covered them - so a copying
// accident that took data\ along left a program that could not randomize
// anything and said so only in a "file not found". The data now travels
// inside the EXE: whatever else goes missing, the tool still works.
//
// THE RULE: A FILE ON DISK WINS, THE BAKED COPY IS THE FLOOR.
// Exactly the pattern the engine already uses for the HQ music and the HQ
// sounds. If data\zones_rich2.json lies beside the program it is read; if it
// does not, the baked one is unpacked. Both halves matter:
//   - curation stays fast. A freshly exported zones_rich2.json dropped into
//     data\ takes effect on the next run, with no rebuild.
//   - a freshly unpacked package runs with no attachments at all.
// Every read says in the log which of the two it was - "external" with the
// path, or "embedded". Without that line the two cases are indistinguishable
// from the outside, and a stale file in data\ would silently outrank a
// corrected baked one.
//
// WHAT IS IN, WHAT IS NOT. Baked: zones_*.json, VanillaLevelChecksums.csv,
// Tr3Checksums.csv. NOT baked: data\secrets\*.txt. Those are hand-written
// lists the user edits and adds to; making them a build product would turn
// an open format into a closed one.
//
// THE GENERATOR STEP. EmbeddedData.cpp is GENERATED and versioned - that is
// the whole point, the repository is what carries the data now. It is made
// by
//
//     powershell -File "Preparation\Tools\EmbedData.ps1"
//
// which reads TR5Randomizer\build\data and rewrites
// TR5Randomizer\src\core\EmbeddedData.cpp. Run it after every re-curation,
// i.e. whenever the zone exports were regenerated; then rebuild and commit
// the generated file. Building itself never calls the generator: GPLv3
// asks that the shipped binary be buildable from the shipped source, and
// b1.bat/b2.bat therefore need nothing but a compiler.
//----------------------------------------------------------------------------

namespace EmbeddedData
{
    struct File
    {
        const char*          name;    // plain file name, e.g. "zones_rich2.json"
        const unsigned char* data;    // zlib stream (RFC 1950), read by uncompress()
        size_t               packed;  // bytes of that stream
        size_t               raw;     // bytes after unpacking
    };

    // Defined in the generated EmbeddedData.cpp, nowhere else.
    extern const File   g_files[];
    extern const size_t g_fileCount;

    // Unpacks one baked file. The name is compared without regard to case,
    // because the level names are not written consistently either
    // ("zones_Andy2.json" next to "zones_andy1.json").
    bool read(const std::string& name, std::string& out, std::string& error);
    bool has(const std::string& name);

    // All baked names, in the order the generator wrote them.
    std::vector<std::string> names();
}

//----------------------------------------------------------------------------
// The one reader everything goes through. Three separate ways of finding
// these files existed before - the command line took a path, the GUI built
// one from its payload folder, and the checksum list had its own two-place
// search. Each would have needed its own fallback; one reader needs one.
//----------------------------------------------------------------------------
namespace DataFile
{
    enum class Source { External, Embedded };
    const char* sourceName(Source s);

    // The file at that path wins; if it is not there, the baked copy of its
    // FILE NAME is taken. Fails only when it is neither.
    bool read(const std::wstring& path, std::string& out, Source& src, std::string& error);

    // Same rule, spelled as folder plus plain name.
    bool read(const std::wstring& dir, const std::string& name,
              std::string& out, Source& src, std::string& error);

    // One line per read since program start: "zones_rich2.json  external
    // C:\...\data\zones_rich2.json" or "zones_rich2.json  embedded". The
    // command line prints them as they happen, the GUI collects them into
    // spoiler.txt - a bug report is worth little without knowing which
    // curation the run actually used.
    const std::vector<std::string>& log();
}

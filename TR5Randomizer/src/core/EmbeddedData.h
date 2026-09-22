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
// Precomputed data (zone exports, checksum lists) baked into the executable
// as a fallback, the same pattern the engine uses for the HQ music and HQ
// sound packs: a file on disk always wins, the baked copy is used only when
// data\ is missing or does not contain it, so the tool keeps working even
// when the data folder was not carried along with the EXE. Every read logs
// which source was used - "external" with the path, or "embedded" - since
// the two cases are otherwise indistinguishable from the outside, and a
// stale file in data\ would silently outrank a corrected baked one.
//
// Baked: zones_*.json, VanillaLevelChecksums.csv, Tr3Checksums.csv.
// Not baked: data\secrets\*.txt, a hand-edited list that grows over time;
// making it a build product would turn an open format into a closed one.
//
// EmbeddedData.cpp is generated and versioned. Regenerate it after any
// re-curation with
//
//     powershell -File "Preparation\Tools\EmbedData.ps1"
//
// which reads TR5Randomizer\build\data and rewrites EmbeddedData.cpp, then
// rebuild and commit the generated file. The build itself never calls the
// generator, so the shipped binary stays buildable from the shipped source
// with nothing but a compiler, as GPLv3 requires.
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
// The single reader for these files: the command line, the GUI, and the
// checksum list each used to look them up their own way, and each would
// otherwise need its own external/embedded fallback.
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

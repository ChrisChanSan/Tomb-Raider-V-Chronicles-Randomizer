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
// as a fallback, like the engine's HQ music and sound packs: a file in
// data\ always wins, the baked copy is used only when it is missing. Every
// read logs its source ("external" with the path, or "embedded"), because a
// stale file in data\ would otherwise silently outrank a corrected baked one.
//
// Baked: zones_*.json, VanillaLevelChecksums.csv, Tr3Checksums.csv. Not
// baked: data\secrets\*.txt, a hand-edited list in an open format.
//
// EmbeddedData.cpp is generated and versioned. After any re-curation run
//     powershell -File "Preparation\Tools\EmbedData.ps1"
// (it reads TR5Randomizer\build\data), then rebuild and commit the file. The
// build never calls the generator, so the shipped binary stays buildable
// from the shipped source with nothing but a compiler, as GPLv3 requires.
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

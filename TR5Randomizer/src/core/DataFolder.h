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
#include <cstdint>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Management of the game data.
//
// The randomizer does NOT write into the Steam folder. It asks once for its
// path, checks the levels against the bundled checksum list
// (VanillaLevelChecksums.csv) and copies them into its own game folder next
// to the engine, because:
//  - Steam's "verify files" silently restores changed files;
//  - a game update would collide with the changes;
//  - a tool that writes into a purchased folder rightly makes players nervous;
//  - "Restore to default" is then just copying anew from the original.
//
// The checksum check makes sure only unmodified vanilla levels serve as the
// source: randomizing a modified level again gives results no seed can
// reproduce.
//----------------------------------------------------------------------------

struct LevelFile
{
    std::string  name;        // "Andrea1.trc"
    uint64_t     size = 0;
    std::string  sha256;      // uppercase, as in the CSV
};

enum class LevelState
{
    Missing,        // file not present in the source folder
    Vanilla,        // size and checksum match the list
    Modified,       // present but differs - not usable as a source
    Unknown         // no entry in the checksum list (e.g. a foreign level)
};

struct LevelStatus
{
    std::string name;
    LevelState  state = LevelState::Missing;
    uint64_t    actualSize = 0;
    std::string actualSha256;
};

class DataFolder
{
public:
    // Loads the bundled checksum list. Without it nothing can be checked,
    // hence a hard error instead of silently carrying on.
    //
    // Two entry points since the list is baked into the EXE as well
    // (EmbeddedData.h): callers that follow the "a file in data\ wins,
    // the baked copy is the floor" rule already hold the text and pass it
    // in; the path variant stays for tools that are handed one explicit
    // file and should read exactly that one.
    bool loadChecksums(const std::wstring& csvPath, std::string& error);
    bool loadChecksumsText(const std::string& csvText, std::string& error);

    size_t knownLevelCount() const { return m_known.size(); }

    // Checks every file of the list in the given source folder. The folder
    // is the "data" subfolder of the Steam installation.
    std::vector<LevelStatus> inspect(const std::wstring& sourceDataDir) const;

    // Is the folder usable as a source? Requires that ALL known levels are
    // present and unmodified. Deliberately strict: a single deviating level
    // is enough to make seeds incomparable.
    static bool isUsableSource(const std::vector<LevelStatus>& status, std::string& reason);

    // Copies all known levels from the source into the target folder,
    // creating the target if needed. This doubles as "restore to default" -
    // both are the same operation, which is why there is only one function.
    // Each level's HD marker sidecar (<stem>.hdm) in the target goes with
    // it: it described the randomized level the copy replaces.
    bool copyLevels(const std::wstring& sourceDataDir,
                    const std::wstring& targetDataDir,
                    std::string& error,
                    int* copiedCount = nullptr) const;

    // Is this ONE file the vanilla level of that name? Same comparison as
    // inspect(), for a caller that has a single path rather than a folder -
    // the command-line tool, which needs it since it reads rooms, items and
    // triggers out of the level file itself and must not be handed an
    // already randomized one.
    //
    // A name the list does not carry comes back as Unknown, not Modified: a
    // hand-built test level is not wrong, it is simply not one of Core's.
    LevelState checkOne(const std::string& name, const std::string& sha256Upper,
                        uint64_t size) const;

    const std::vector<LevelFile>& knownLevels() const { return m_known; }

private:
    std::vector<LevelFile> m_known;
};

//----------------------------------------------------------------------------
// The edition of the Tomb Raider III files a run reads the guests and the
// TR3 outfits from.
//
// Tr3Checksums.csv lists every edition this project knows, file by file,
// with size and SHA-256 and an Edition column: the original release and the
// I-III Remastered files. Four of the five Remastered levels have the
// original's size (retouched texels only), so only the hash tells them
// apart. A file of no known edition is never refused - the import reads it
// all the same - but the run says so: a WARNING in the GUI's result rows
// and in spoiler.txt, a line on the command line.
//----------------------------------------------------------------------------
namespace Tr3Edition
{
    struct Verdict
    {
        bool present = false;              // at least one file was checked
        std::string edition;               // "original", "Remastered",
                                           // both joined by " + "; or ""
        std::vector<std::string> unknown;  // names of files no edition has
    };

    // Checks those of `files` that exist against the list's rows of `game`
    // ("tr3" or "tr3gold"): name (any case), size and SHA-256. Each file's
    // hash is kept in <cacheDir>\Tr3Hashes.txt under its path, size and
    // write time - the same stamp the package cache trusts - so a file is
    // hashed once; an empty cacheDir hashes every time. A list without the
    // Edition column (an older data\Tr3Checksums.csv) counts as original.
    Verdict check(const std::string& csvText, const char* game,
                  const std::vector<std::wstring>& files,
                  const std::wstring& cacheDir);

    // One word for a log line: the edition, "unknown edition" or "absent".
    std::string describe(const Verdict& v);

    // The warning, word for word the same in every place it is shown.
    const char* warning();
}

namespace FileHash
{
    // SHA-256 via Windows' own CNG interface (bcrypt). Deliberately no
    // hand-written crypto: the system variant is vetted, costs no extra
    // dependency and is considerably faster.
    bool sha256File(const std::wstring& path, std::string& hexUpper, uint64_t& size, std::string& error);
}

namespace PathUtil
{
    std::wstring join(const std::wstring& a, const std::wstring& b);
    bool         fileExists(const std::wstring& path);
    bool         dirExists(const std::wstring& path);
    bool         ensureDir(const std::wstring& path);          // creates intermediate levels too
    std::wstring exeDir();                                     // folder of the running EXE
    std::wstring widen(const std::string& s);                  // UTF-8 -> UTF-16
    std::string  narrow(const std::wstring& s);                // UTF-16 -> UTF-8
}

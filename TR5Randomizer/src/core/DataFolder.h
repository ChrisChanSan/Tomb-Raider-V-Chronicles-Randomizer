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
// Fundamental decision (user, 2026-08-05): the randomizer does NOT write
// into the Steam folder. It asks once for its path, checks the levels
// against the bundled checksum list, and copies them into its own game
// folder next to the engine. Reasons:
//  - Steam's "verify files" silently restores changed files; a randomized
//    playthrough would be vanilla again without warning.
//  - A game update collides with our changes.
//  - A tool that writes into a purchased folder rightly makes users
//    nervous.
//  - "Restore to default" becomes trivial: the Steam folder IS the
//    original, restoring means copying anew.
//
// The checksum list (VanillaLevelChecksums.csv) already exists from the
// preparation phase and gets its true purpose here: making sure that only
// genuinely unmodified vanilla levels serve as the source. Randomizing an
// already randomized or otherwise modified level a second time produces
// results nobody can trace anymore - and preventing exactly that is what
// the seed feature is for.
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
    bool loadChecksums(const std::wstring& csvPath, std::string& error);

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
    bool copyLevels(const std::wstring& sourceDataDir,
                    const std::wstring& targetDataDir,
                    std::string& error,
                    int* copiedCount = nullptr) const;

    const std::vector<LevelFile>& knownLevels() const { return m_known; }

private:
    std::vector<LevelFile> m_known;
};

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

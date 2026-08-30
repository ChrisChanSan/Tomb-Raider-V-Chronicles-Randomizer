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
#include "DataFolder.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cctype>

#pragma comment(lib, "bcrypt.lib")

//----------------------------------------------------------------------------
// Path and text utilities
//----------------------------------------------------------------------------
namespace PathUtil
{
    std::wstring join(const std::wstring& a, const std::wstring& b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        wchar_t last = a[a.size() - 1];
        if (last == L'\\' || last == L'/') return a + b;
        return a + L"\\" + b;
    }

    bool fileExists(const std::wstring& path)
    {
        DWORD a = GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool dirExists(const std::wstring& path)
    {
        DWORD a = GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool ensureDir(const std::wstring& path)
    {
        if (path.empty()) return false;
        if (dirExists(path)) return true;

        // Create intermediate levels. CreateDirectoryW only ever creates one
        // level, and a user may well give a path whose parent folder does
        // not exist yet.
        size_t pos = path.find_last_of(L"\\/");
        if (pos != std::wstring::npos && pos > 2)
        {
            std::wstring parent = path.substr(0, pos);
            if (!parent.empty() && !dirExists(parent))
            {
                if (!ensureDir(parent)) return false;
            }
        }
        if (CreateDirectoryW(path.c_str(), nullptr)) return true;
        return GetLastError() == ERROR_ALREADY_EXISTS;
    }

    std::wstring exeDir()
    {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
            if (n == 0) return std::wstring();
            if (n < buf.size()) { buf.resize(n); break; }
            buf.resize(buf.size() * 2);
        }
        size_t pos = buf.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? std::wstring() : buf.substr(0, pos);
    }

    std::wstring widen(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
        return out;
    }

    std::string narrow(const std::wstring& s)
    {
        if (s.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
        return out;
    }
}

//----------------------------------------------------------------------------
// SHA-256 via Windows CNG
//----------------------------------------------------------------------------
bool FileHash::sha256File(const std::wstring& path, std::string& hexUpper, uint64_t& size, std::string& error)
{
    hexUpper.clear();
    size = 0;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = "cannot open file"; return false; }

    LARGE_INTEGER li{};
    if (GetFileSizeEx(h, &li)) size = static_cast<uint64_t>(li.QuadPart);

    BCRYPT_ALG_HANDLE  alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> hashObj, digest;
    bool ok = false;

    do
    {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        { error = "SHA256 not available"; break; }

        DWORD objLen = 0, digLen = 0, cb = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0) < 0) { error = "BCryptGetProperty failed"; break; }
        if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digLen), sizeof(digLen), &cb, 0) < 0) { error = "BCryptGetProperty failed"; break; }

        hashObj.resize(objLen);
        digest.resize(digLen);

        if (BCryptCreateHash(alg, &hash, hashObj.data(), objLen, nullptr, 0, 0) < 0)
        { error = "BCryptCreateHash failed"; break; }

        std::vector<unsigned char> buf(1 << 20);   // 1 MB - levels are 3-6 MB
        for (;;)
        {
            DWORD got = 0;
            if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr))
            { error = "read error"; break; }
            if (got == 0) { ok = true; break; }
            if (BCryptHashData(hash, buf.data(), got, 0) < 0) { error = "BCryptHashData failed"; break; }
        }
        if (!ok) break;
        ok = false;

        if (BCryptFinishHash(hash, digest.data(), digLen, 0) < 0)
        { error = "BCryptFinishHash failed"; break; }

        static const char* HEX = "0123456789ABCDEF";
        hexUpper.reserve(static_cast<size_t>(digLen) * 2);
        for (DWORD i = 0; i < digLen; ++i)
        {
            hexUpper.push_back(HEX[(digest[i] >> 4) & 0x0F]);
            hexUpper.push_back(HEX[digest[i] & 0x0F]);
        }
        ok = true;

    } while (false);

    if (hash) BCryptDestroyHash(hash);
    if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    return ok;
}

//----------------------------------------------------------------------------
// Checksum list
//----------------------------------------------------------------------------
namespace
{
    std::string trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
        return s.substr(a, b - a);
    }

    std::string upper(const std::string& s)
    {
        std::string o = s;
        for (char& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return o;
    }
}

bool DataFolder::loadChecksums(const std::wstring& csvPath, std::string& error)
{
    m_known.clear();

    FILE* f = nullptr;
    if (_wfopen_s(&f, csvPath.c_str(), L"rb") != 0 || !f)
    {
        error = "checksum list not found: " + PathUtil::narrow(csvPath);
        return false;
    }

    std::string text;
    {
        char buf[8192];
        size_t got;
        while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, got);
    }
    std::fclose(f);

    size_t pos = 0;
    bool firstLine = true;
    while (pos <= text.size())
    {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos));
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        if (line.empty()) continue;
        if (firstLine) { firstLine = false; if (line.rfind("FileName", 0) == 0) continue; }

        size_t c1 = line.find(',');
        if (c1 == std::string::npos) continue;
        size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;

        LevelFile lf;
        lf.name = trim(line.substr(0, c1));
        lf.size = std::strtoull(trim(line.substr(c1 + 1, c2 - c1 - 1)).c_str(), nullptr, 10);
        lf.sha256 = upper(trim(line.substr(c2 + 1)));
        if (!lf.name.empty() && lf.sha256.size() == 64) m_known.push_back(lf);
    }

    if (m_known.empty())
    {
        error = "checksum list contains no usable entries";
        return false;
    }
    return true;
}

std::vector<LevelStatus> DataFolder::inspect(const std::wstring& sourceDataDir) const
{
    std::vector<LevelStatus> out;
    out.reserve(m_known.size());

    for (const LevelFile& lf : m_known)
    {
        LevelStatus st;
        st.name = lf.name;

        const std::wstring path = PathUtil::join(sourceDataDir, PathUtil::widen(lf.name));
        if (!PathUtil::fileExists(path)) { st.state = LevelState::Missing; out.push_back(st); continue; }

        // Size first - on a mismatch that saves hashing 16 times several
        // megabytes.
        std::string err;
        uint64_t sz = 0;
        std::string hash;
        if (!FileHash::sha256File(path, hash, sz, err))
        {
            st.state = LevelState::Modified;
            out.push_back(st);
            continue;
        }
        st.actualSize = sz;
        st.actualSha256 = hash;
        st.state = (sz == lf.size && hash == lf.sha256) ? LevelState::Vanilla : LevelState::Modified;
        out.push_back(st);
    }
    return out;
}

LevelState DataFolder::checkOne(const std::string& name, const std::string& sha256Upper,
                                uint64_t size) const
{
    // Names are compared without case: the CSV says "Andrea1.trc", a folder
    // may hold "ANDREA1.TRC", and Windows considers those the same file.
    auto lower = [](std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };
    const std::string want = lower(name);

    for (const LevelFile& lf : m_known)
    {
        if (lower(lf.name) != want) continue;
        return (size == lf.size && sha256Upper == lf.sha256)
                   ? LevelState::Vanilla : LevelState::Modified;
    }
    return LevelState::Unknown;
}

bool DataFolder::isUsableSource(const std::vector<LevelStatus>& status, std::string& reason)
{
    // An empty list is NOT a success. Without this check the function
    // reports "all fine" when nothing was checked at all - which showed up
    // in the UI as a green message above an empty table.
    if (status.empty())
    {
        reason = "Not a single level was checked - the bundled checksum "
                 "list is missing or empty.";
        return false;
    }

    int missing = 0, modified = 0;
    std::string firstBad;

    for (const LevelStatus& s : status)
    {
        if (s.state == LevelState::Missing)  { ++missing;  if (firstBad.empty()) firstBad = s.name; }
        if (s.state == LevelState::Modified) { ++modified; if (firstBad.empty()) firstBad = s.name; }
    }

    if (missing == 0 && modified == 0) { reason.clear(); return true; }

    char buf[512];
    if (missing && modified)
        std::snprintf(buf, sizeof(buf),
            "%d levels are missing, %d differ from vanilla (first: %s). "
            "Only an unmodified installation works as a source - otherwise a seed cannot be reproduced.",
            missing, modified, firstBad.c_str());
    else if (missing)
        std::snprintf(buf, sizeof(buf),
            "%d levels are missing (first: %s). Does the path really point at the installation's data folder?",
            missing, firstBad.c_str());
    else
        std::snprintf(buf, sizeof(buf),
            "%d levels differ from vanilla (first: %s). Most likely this folder was modified before - "
            "please verify the files via Steam and try again.",
            modified, firstBad.c_str());

    reason = buf;
    return false;
}

bool DataFolder::copyLevels(const std::wstring& sourceDataDir,
                            const std::wstring& targetDataDir,
                            std::string& error,
                            int* copiedCount) const
{
    if (copiedCount) *copiedCount = 0;

    if (!PathUtil::dirExists(sourceDataDir))
    {
        error = "source folder does not exist";
        return false;
    }
    if (!PathUtil::ensureDir(targetDataDir))
    {
        error = "cannot create target folder";
        return false;
    }

    for (const LevelFile& lf : m_known)
    {
        const std::wstring src = PathUtil::join(sourceDataDir, PathUtil::widen(lf.name));
        const std::wstring dst = PathUtil::join(targetDataDir, PathUtil::widen(lf.name));

        if (!PathUtil::fileExists(src))
        {
            error = "source file missing: " + lf.name;
            return false;
        }
        // FALSE = overwrite an existing target file. That is exactly what
        // restoring wants: the randomized version gets replaced.
        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE))
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "copy failed at %s (Windows error %lu)",
                          lf.name.c_str(), static_cast<unsigned long>(GetLastError()));
            error = buf;
            return false;
        }
        // Remove the read-only attribute of copied files - Steam installs
        // occasionally have it, and the next randomize wants to overwrite
        // the file.
        DWORD attr = GetFileAttributesW(dst.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesW(dst.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);

        if (copiedCount) ++(*copiedCount);
    }
    return true;
}

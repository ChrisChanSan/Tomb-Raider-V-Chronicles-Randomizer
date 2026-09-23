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
#include "EmbeddedData.h"
#include "DataFolder.h"     // PathUtil

#include <zlib.h>

#include <cstdio>

//----------------------------------------------------------------------------
// The unpacking half. The byte tables live in the generated EmbeddedData.cpp;
// this file holds everything that was written by hand, so a regeneration can
// never overwrite logic.
//
// The streams are in zlib format on purpose, not raw deflate: the randomizer
// already links zlib for the .TRC files, and uncompress() reads that format
// straight off, header and checksum included. One call, no framing of our
// own - and the Adler-32 at the end of the stream means a truncated or
// mangled table is caught here instead of surfacing as a JSON parse error
// three functions later.
//----------------------------------------------------------------------------
namespace
{
    std::string lower(std::string s)
    {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    // The file name out of a full path. The command line may hand over any
    // path; the baked copies are keyed by name alone.
    std::string fileNameOf(const std::wstring& path)
    {
        const size_t pos = path.find_last_of(L"\\/");
        return PathUtil::narrow(pos == std::wstring::npos ? path : path.substr(pos + 1));
    }

    bool readWholeFile(const std::wstring& path, std::string& out, std::string& error)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) { error = "cannot open file"; return false; }

        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); error = "cannot read file size"; return false; }

        out.resize(static_cast<size_t>(n));
        const size_t got = (n > 0) ? std::fread(&out[0], 1, static_cast<size_t>(n), f) : 0;
        std::fclose(f);
        if (got != static_cast<size_t>(n)) { error = "file not fully readable"; return false; }
        return true;
    }

    const EmbeddedData::File* find(const std::string& name)
    {
        const std::string want = lower(name);
        for (size_t i = 0; i < EmbeddedData::g_fileCount; ++i)
            if (lower(EmbeddedData::g_files[i].name) == want) return &EmbeddedData::g_files[i];
        return nullptr;
    }

    std::vector<std::string> g_log;
}

bool EmbeddedData::has(const std::string& name)
{
    return find(name) != nullptr;
}

std::vector<std::string> EmbeddedData::names()
{
    std::vector<std::string> out;
    out.reserve(g_fileCount);
    for (size_t i = 0; i < g_fileCount; ++i) out.push_back(g_files[i].name);
    return out;
}

bool EmbeddedData::read(const std::string& name, std::string& out, std::string& error)
{
    const File* f = find(name);
    if (!f) { error = "no built-in copy of " + name; return false; }

    out.resize(f->raw);
    uLongf n = static_cast<uLongf>(f->raw);
    const int rc = uncompress(reinterpret_cast<Bytef*>(f->raw ? &out[0] : nullptr), &n,
                              f->data, static_cast<uLong>(f->packed));
    if (rc != Z_OK || n != f->raw)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "built-in copy of %s is damaged (zlib %d, %lu of %llu bytes)",
                      name.c_str(), rc, static_cast<unsigned long>(n),
                      static_cast<unsigned long long>(f->raw));
        error = buf;
        out.clear();
        return false;
    }
    return true;
}

const char* DataFile::sourceName(Source s)
{
    return s == Source::External ? "external" : "embedded";
}

const std::vector<std::string>& DataFile::log()
{
    return g_log;
}

bool DataFile::read(const std::wstring& path, std::string& out, Source& src, std::string& error)
{
    const std::string name = fileNameOf(path);

    if (PathUtil::fileExists(path))
    {
        if (!readWholeFile(path, out, error))
        {
            error = name + ": " + error + " (" + PathUtil::narrow(path) + ")";
            return false;
        }
        src = Source::External;
        g_log.push_back(name + "  external  " + PathUtil::narrow(path));
        return true;
    }

    if (!EmbeddedData::read(name, out, error)) return false;

    src = Source::Embedded;
    g_log.push_back(name + "  embedded");
    return true;
}

bool DataFile::read(const std::wstring& dir, const std::string& name,
                    std::string& out, Source& src, std::string& error)
{
    return read(PathUtil::join(dir, PathUtil::widen(name)), out, src, error);
}

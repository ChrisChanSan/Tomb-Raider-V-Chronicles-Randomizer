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
#include "DataFile.h"
#include "DataFolder.h"     // PathUtil

#include <cstdio>

namespace
{
    // The file name and the folder out of a full path, for the log line and
    // the message.
    void split(const std::wstring& path, std::string& name, std::string& dir)
    {
        const size_t pos = path.find_last_of(L"\\/");
        name = PathUtil::narrow(pos == std::wstring::npos ? path : path.substr(pos + 1));
        dir = pos == std::wstring::npos ? std::string(".") : PathUtil::narrow(path.substr(0, pos));
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

    std::vector<std::string> g_log;
}

const std::vector<std::string>& DataFile::log()
{
    return g_log;
}

bool DataFile::read(const std::wstring& path, std::string& out, std::string& error)
{
    std::string name, dir;
    split(path, name, dir);

    if (!PathUtil::fileExists(path))
    {
        error = name + " not found in " + dir + ". The randomizer reads it from "
                "there and has no copy of its own: the data folder of the "
                "package belongs beside the program.";
        g_log.push_back(name + "  MISSING  " + dir);
        out.clear();
        return false;
    }
    if (!readWholeFile(path, out, error))
    {
        error = name + ": " + error + " (" + PathUtil::narrow(path) + ")";
        g_log.push_back(name + "  UNREADABLE  " + PathUtil::narrow(path));
        out.clear();
        return false;
    }
    g_log.push_back(name + "  external  " + PathUtil::narrow(path));
    return true;
}

bool DataFile::read(const std::wstring& dir, const std::string& name,
                    std::string& out, std::string& error)
{
    return read(PathUtil::join(dir, PathUtil::widen(name)), out, error);
}

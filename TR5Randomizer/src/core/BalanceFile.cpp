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
#include "BalanceFile.h"
#include "DataFolder.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    struct Line
    {
        const char* name;
        int BalanceFile::Values::* field;
        const char* note;
    };

    // Same order as in the engine's template, so a freshly written file
    // looks exactly like one the game created.
    const Line kLines[] =
    {
        { "WeaponDamage", &BalanceFile::Values::weaponDamage,
          "  on all of Lara's weapons. 0 makes them harmless" },
        { "EnemyHealth",  &BalanceFile::Values::enemyHealth,
          "  50 = half as tough, 200 = twice as tough" },
        { "EnemyDamage",  &BalanceFile::Values::enemyDamage,
          "  0 makes all enemies harmless" },
    };

    std::wstring filePath(const std::wstring& gameDir)
    {
        return PathUtil::join(gameDir, L"Balance.txt");
    }

    bool readRaw(const std::wstring& path, std::string& content)
    {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) return false;

        char buffer[4096];
        size_t n;
        content.clear();
        while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            content.append(buffer, n);

        std::fclose(f);
        return true;
    }

    // Does this line refer to this setting? Whitespace before the = is
    // allowed, case does not matter - the engine reads it the same way.
    bool lineMatches(const std::string& line, const char* name)
    {
        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos) return false;
        if (line[i] == ';' || line[i] == '#') return false;

        const size_t len = std::strlen(name);
        if (line.size() - i < len) return false;

        for (size_t k = 0; k < len; k++)
        {
            char a = line[i + k], b = name[k];
            if (a >= 'A' && a <= 'Z') a = char(a + 32);
            if (b >= 'A' && b <= 'Z') b = char(b + 32);
            if (a != b) return false;
        }

        size_t j = line.find_first_not_of(" \t", i + len);
        return j != std::string::npos && line[j] == '=';
    }

    std::string buildLine(const Line& z, int value)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%-22s = %-6d ; (original 100)%s",
                      z.name, value, z.note);
        return buf;
    }
}

namespace BalanceFile
{

bool write(const std::wstring& gameDir, const Values& w, std::string* errorText)
{
    if (gameDir.empty())
    {
        if (errorText) *errorText = "no game folder selected.";
        return false;
    }

    const std::wstring path = filePath(gameDir);

    std::string old;
    const bool hadFile = readRaw(path, old);

    // Split into lines, remembering each line's ending. The engine's file is
    // CRLF; whoever saves it in another editor may leave LF behind - both
    // are preserved, so our writing leaves no traces.
    std::vector<std::string> lines;
    std::vector<std::string> endings;

    for (size_t i = 0; i < old.size();)
    {
        size_t e = i;
        while (e < old.size() && old[e] != '\r' && old[e] != '\n') e++;

        lines.push_back(old.substr(i, e - i));

        std::string ending;
        if (e < old.size() && old[e] == '\r') { ending += '\r'; e++; }
        if (e < old.size() && old[e] == '\n') { ending += '\n'; e++; }
        endings.push_back(ending);

        i = e;
    }

    const char* newline = "\r\n";
    for (size_t i = 0; i < endings.size(); i++)
        if (endings[i] == "\n") { newline = "\n"; break; }

    for (const Line& z : kLines)
    {
        const std::string fresh = buildLine(z, w.*(z.field));

        bool replaced = false;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (!lineMatches(lines[i], z.name)) continue;
            lines[i] = fresh;
            replaced = true;
            break;      // the engine also takes the first match
        }

        if (replaced) continue;

        // A missing line goes to the end. On the next start the game sorts
        // the file back into its usual shape anyway.
        if (!lines.empty() && endings.back().empty())
            endings.back() = newline;

        lines.push_back(fresh);
        endings.push_back(newline);
    }

    // A brand-new file gets a header, so it is clear where it came from and
    // that the game fills in the rest.
    std::string out;
    if (!hadFile)
    {
        out += "; Balance.txt - created by the TR5 Randomizer.";
        out += newline;
        out += "; On the next start the game fills in all further settings";
        out += newline;
        out += "; here: damage per weapon, hit points per enemy, damage per";
        out += newline;
        out += "; attack, level hazards.";
        out += newline;
        out += newline;
    }

    for (size_t i = 0; i < lines.size(); i++)
    {
        out += lines[i];
        out += endings[i];
    }

    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f)
    {
        if (errorText) *errorText = "Balance.txt cannot be written.";
        return false;
    }

    const bool ok = out.empty() || std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);

    if (!ok && errorText) *errorText = "Balance.txt could not be written completely.";
    return ok;
}

Values read(const std::wstring& gameDir)
{
    Values w;
    if (gameDir.empty()) return w;

    std::string content;
    if (!readRaw(filePath(gameDir), content)) return w;

    for (size_t i = 0; i < content.size();)
    {
        size_t e = i;
        while (e < content.size() && content[e] != '\r' && content[e] != '\n') e++;
        const std::string line = content.substr(i, e - i);
        while (e < content.size() && (content[e] == '\r' || content[e] == '\n')) e++;
        i = e;

        for (const Line& z : kLines)
        {
            if (!lineMatches(line, z.name)) continue;

            const size_t g = line.find('=');
            if (g == std::string::npos) break;

            w.*(z.field) = std::atoi(line.c_str() + g + 1);
            break;
        }
    }

    return w;
}

}

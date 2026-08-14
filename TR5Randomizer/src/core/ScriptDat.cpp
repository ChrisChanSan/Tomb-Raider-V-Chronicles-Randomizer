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
#include "ScriptDat.h"

#include <cstdio>
#include <cstring>

namespace
{
#pragma pack(push, 1)
    struct GAMEFLOW
    {
        uint32_t bits;
        uint32_t InputTimeout;
        uint8_t  SecurityTag, nLevels, nFileNames, Pad;
        uint16_t FileNameLen, ScriptLen;
    };
#pragma pack(pop)

    bool gleichOhneEndung(const char* a, const std::string& b)
    {
        // Die Namen im Skript tragen ihren Pfad: "DATA\ANDY1". Verglichen wird
        // nur der letzte Teil, ohne Endung und ohne Ruecksicht auf Gross- und
        // Kleinschreibung - "DATA\ANDY1" ist dasselbe Level wie "andy1.TRC".
        for (const char* s = a; *s; ++s)
            if (*s == '\\' || *s == '/') a = s + 1;

        size_t i = 0;
        for (; i < b.size(); ++i)
        {
            const char x = a[i];
            if (x == 0) return false;
            const char lx = (x >= 'A' && x <= 'Z') ? static_cast<char>(x + 32) : x;
            const char c = b[i];
            const char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
            if (lx != lc) return false;
        }
        return a[i] == 0 || a[i] == '.';
    }
}

bool ScriptDat::load(const std::wstring& path, std::string& error)
{
    m_bytes.clear();
    m_levels.clear();
    m_flagsAt.clear();

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) { error = "SCRIPT.DAT nicht lesbar"; return false; }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); error = "SCRIPT.DAT ist leer"; return false; }
    m_bytes.resize(static_cast<size_t>(n));
    const bool ok = std::fread(m_bytes.data(), 1, m_bytes.size(), f) == m_bytes.size();
    std::fclose(f);
    if (!ok) { error = "SCRIPT.DAT unvollstaendig gelesen"; return false; }

    return zerlege(error);
}

bool ScriptDat::zerlege(std::string& error)
{
    if (m_bytes.size() < sizeof(GAMEFLOW) + 40) { error = "SCRIPT.DAT zu klein"; return false; }

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));

    size_t p = sizeof(GAMEFLOW) + 40;                 // Kopf + "[PCExtensions]"
    const size_t nameOfsAt = p;
    p += 2ull * gf.nFileNames;
    const size_t nameWadAt = p;
    p += gf.FileNameLen;
    const size_t scriptOfsAt = p;
    p += 2ull * gf.nLevels;
    const size_t scriptWad = p;

    if (scriptWad + gf.ScriptLen > m_bytes.size()) { error = "SCRIPT.DAT: Kopf passt nicht zur Datei"; return false; }

    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, m_bytes.data() + o, 2); return v; };

    for (int i = 0; i < gf.nLevels; ++i)
    {
        size_t q = scriptWad + u16(scriptOfsAt + 2ull * i);

        while (q + 1 < m_bytes.size())
        {
            const uint8_t cmd = m_bytes[q];
            if (cmd == 0x83) break;                   // CMD_ENDSEQ

            if (cmd == 0x81)                          // CMD_LEVEL
            {
                if (q + 5 >= m_bytes.size()) break;
                const size_t fl = q + 2;              // Opcode + Nutzbyte[0]
                const uint8_t nameIdx = m_bytes[q + 4];

                Eintrag e;
                e.flags = static_cast<uint16_t>(m_bytes[fl] | (m_bytes[fl + 1] << 8));
                if (nameIdx < gf.nFileNames)
                {
                    const size_t o = nameWadAt + u16(nameOfsAt + 2ull * nameIdx);
                    if (o < m_bytes.size())
                        e.fileName = reinterpret_cast<const char*>(m_bytes.data() + o);
                }
                m_levels.push_back(e);
                m_flagsAt.push_back(fl);
                break;
            }

            // Nutzbytelaenge je Befehl, 1:1 aus LoadGameflow. Opcodes aus
            // enum gf_commands (CMD_FMV = 0x80):
            //   80 FMV   81 LEVEL  82 TITLE  83 ENDSEQ  84 PLAYCUT
            //   85 CUT1  86 CUT2   87 CUT3   88 CUT4    89 LAYER1
            //   8A LAYER2 8B UVROT 8C LEGEND 8D LENSFLARE
            //   8E MIRROR 8F FOG   90 ANIMATINGMIP      91 RESETHUB
            switch (cmd)
            {
            case 0x80: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88:
            case 0x8B: case 0x8C: case 0x90: case 0x91: q += 2; break;
            case 0x82: case 0x89: case 0x8A:                    q += 5; break;
            case 0x8D:                                          q += 10; break;
            case 0x8E:                                          q += 6; break;
            case 0x8F:                                          q += 4; break;
            default:                                            q += 3; break;
            }
        }
    }

    if (m_levels.empty()) { error = "SCRIPT.DAT: kein einziger CMD_LEVEL gefunden"; return false; }
    return true;
}

bool ScriptDat::clearLevelFlags(const std::string& fileName, uint16_t mask, bool& changed)
{
    changed = false;
    for (size_t i = 0; i < m_levels.size(); ++i)
    {
        if (!gleichOhneEndung(m_levels[i].fileName.c_str(), fileName)) continue;

        const uint16_t neu = static_cast<uint16_t>(m_levels[i].flags & ~mask);
        if (neu != m_levels[i].flags)
        {
            m_bytes[m_flagsAt[i]] = static_cast<uint8_t>(neu & 0xFF);
            m_bytes[m_flagsAt[i] + 1] = static_cast<uint8_t>(neu >> 8);
            m_levels[i].flags = neu;
            changed = true;
        }
        return true;
    }
    return false;
}

bool ScriptDat::save(const std::wstring& path, std::string& error) const
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) { error = "SCRIPT.DAT nicht schreibbar"; return false; }
    const bool ok = std::fwrite(m_bytes.data(), 1, m_bytes.size(), f) == m_bytes.size();
    std::fclose(f);
    if (!ok) { error = "SCRIPT.DAT unvollstaendig geschrieben"; return false; }
    return true;
}

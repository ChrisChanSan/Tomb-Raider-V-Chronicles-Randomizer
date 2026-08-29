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
#include "LevelChains.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <random>

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

    // Payload length per command, 1:1 from DoGameflow (game/gameflow.cpp:160ff).
    // Opcodes from enum gf_commands (game/gameflow.h), CMD_FMV = 0x80.
    //
    // The object commands (0x92..0xD8: key/puzzle/pickup/examine and the
    // combos) carry 14 payload bytes - name index, yoff, size and three
    // rotations plus flags. Exactly those were missing here before: the old
    // parser gave them, like everything unknown, 2 payload bytes. That it
    // still reached its goal was luck - an object command is 15 bytes, and
    // 15 is divisible by 3, so the too-short stride happened to find the
    // beat again after five hops. It would have torn as soon as one of the
    // jumped-into payload bytes (offset 3, 6, 9 or 12 in the command)
    // landed on an opcode with a different length, say 0x80 from a rotation
    // of 0x8000.
    int payload(uint8_t cmd)
    {
        switch (cmd)
        {
        case 0x80: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88:
        case 0x8B: case 0x8C: case 0x90: case 0x91: return 1;
        case 0x81:                                  return 5;   // LEVEL
        case 0x82: case 0x89: case 0x8A:            return 4;   // TITLE, LAYER1/2
        case 0x83:                                  return 0;   // ENDSEQ
        case 0x8D:                                  return 9;   // LENSFLARE
        case 0x8E:                                  return 5;   // MIRROR
        case 0x8F:                                  return 3;   // FOG
        case 0xD9: case 0xDA:                       return 2;   // GIVEOBJ/TAKEOBJ
        default:
            if (cmd >= ScriptDat::CMD_OBJ_FIRST && cmd <= ScriptDat::CMD_OBJ_LAST)
                return ScriptDat::OBJ_PAYLOAD_BYTES;
            return -1;                                          // unknown: stop
        }
    }

    bool sameWithoutExt(const char* a, const std::string& b)
    {
        // The names in the script carry their path: "DATA\ANDY1". Only the
        // last part is compared, without extension and ignoring case -
        // "DATA\ANDY1" is the same level as "andy1.TRC".
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
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) { error = "SCRIPT.DAT not readable"; return false; }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); error = "SCRIPT.DAT is empty"; return false; }
    m_bytes.resize(static_cast<size_t>(n));
    const bool ok = std::fread(m_bytes.data(), 1, m_bytes.size(), f) == m_bytes.size();
    std::fclose(f);
    if (!ok) { error = "SCRIPT.DAT read incompletely"; return false; }

    return parse(error);
}

bool ScriptDat::parse(std::string& error)
{
    m_levels.clear();
    m_flagsAt.clear();
    m_blockStart.clear();
    m_blockEnd.clear();
    m_ofsIndex.clear();

    if (m_bytes.size() < sizeof(GAMEFLOW) + 40) { error = "SCRIPT.DAT too small"; return false; }

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));

    size_t p = sizeof(GAMEFLOW) + 40;                 // header + "[PCExtensions]"
    const size_t nameOfsAt = p;
    p += 2ull * gf.nFileNames;
    const size_t nameWadAt = p;
    p += gf.FileNameLen;
    const size_t scriptOfsAt = p;
    p += 2ull * gf.nLevels;
    const size_t scriptWad = p;

    if (scriptWad + gf.ScriptLen > m_bytes.size()) { error = "SCRIPT.DAT: header does not match the file"; return false; }

    m_scriptOfsAt = scriptOfsAt;
    m_scriptWad   = scriptWad;
    m_nLevels     = gf.nLevels;

    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, m_bytes.data() + o, 2); return v; };

    for (int i = 0; i < gf.nLevels; ++i)
    {
        const size_t start = scriptWad + u16(scriptOfsAt + 2ull * i);

        // The block end is the next larger offset, not stubbornly the next
        // entry: the ordering of the offset table is guaranteed nowhere.
        size_t end = scriptWad + gf.ScriptLen;
        for (int j = 0; j < gf.nLevels; ++j)
        {
            const size_t o = scriptWad + u16(scriptOfsAt + 2ull * j);
            if (o > start && o < end) end = o;
        }
        if (end > m_bytes.size()) end = m_bytes.size();

        for (size_t q = start; q < end; )
        {
            const uint8_t cmd = m_bytes[q];
            const int len = payload(cmd);
            if (len < 0) break;                       // unknown: this is the end

            if (cmd == 0x81)                          // CMD_LEVEL
            {
                if (q + 5 >= m_bytes.size()) break;
                const size_t fl = q + 2;              // opcode + payload[0]
                const uint8_t nameIdx = m_bytes[q + 4];

                Entry e;
                e.flags = static_cast<uint16_t>(m_bytes[fl] | (m_bytes[fl + 1] << 8));
                if (nameIdx < gf.nFileNames)
                {
                    const size_t o = nameWadAt + u16(nameOfsAt + 2ull * nameIdx);
                    if (o < m_bytes.size())
                        e.fileName = reinterpret_cast<const char*>(m_bytes.data() + o);
                }
                m_levels.push_back(e);
                m_flagsAt.push_back(fl);
                m_blockStart.push_back(start);
                m_blockEnd.push_back(end);
                m_ofsIndex.push_back(i);
                break;
            }

            q += 1 + len;
        }
    }

    if (m_levels.empty()) { error = "SCRIPT.DAT: not a single CMD_LEVEL found"; return false; }
    return true;
}

// Looks for an object command in a level's block. Returns the opcode's
// position or npos.
size_t ScriptDat::findCommand(size_t idx, uint8_t cmd) const
{
    for (size_t q = m_blockStart[idx]; q < m_blockEnd[idx]; )
    {
        const int len = payload(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == cmd) return q;
        q += 1 + len;
    }
    return std::string::npos;
}

bool ScriptDat::findLevel(const std::string& name, size_t& idx) const
{
    for (size_t i = 0; i < m_levels.size(); ++i)
        if (sameWithoutExt(m_levels[i].fileName.c_str(), name)) { idx = i; return true; }
    return false;
}

bool ScriptDat::copyObjectCommand(const std::string& fromLevel, const std::string& toLevel,
                                  uint8_t cmd, bool& changed, std::string& error)
{
    changed = false;

    if (cmd < CMD_OBJ_FIRST || cmd > CMD_OBJ_LAST)
    { error = "not an object command: " + std::to_string((int)cmd); return false; }

    size_t iFrom = 0, iTo = 0;
    if (!findLevel(fromLevel, iFrom)) { error = "level not in the script: " + fromLevel;  return false; }
    if (!findLevel(toLevel, iTo)) { error = "level not in the script: " + toLevel; return false; }

    // If the target already has the command, everything stays as is. The
    // call is thereby repeatable - just like clearLevelFlags, and for the
    // same reason: the randomizer runs over the same installation more than
    // once.
    if (findCommand(iTo, cmd) != std::string::npos) return true;

    const size_t source = findCommand(iFrom, cmd);
    if (source == std::string::npos)
    { error = "level " + fromLevel + " does not have the command at all"; return false; }
    if (source + OBJ_COMMAND_BYTES > m_bytes.size())
    { error = "command in " + fromLevel + " sticks out of the file"; return false; }

    std::vector<uint8_t> command(m_bytes.begin() + source,
                                m_bytes.begin() + source + OBJ_COMMAND_BYTES);

    // At the front of the block: in vanilla the object commands all stand
    // before CMD_LEVEL, and they must - DoGameflow works ONE loop, and
    // CMD_LEVEL descends into the level there. Whatever stood behind it
    // would only be read after the level and arrive too late for the
    // naming.
    const size_t insertAt = m_blockStart[iTo];

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));

    if (gf.ScriptLen + OBJ_COMMAND_BYTES > 0xFFFF)
    { error = "SCRIPT.DAT: script wad would overflow"; return false; }

    // Check first, then write: leave no half-rebuilt script behind.
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > insertAt && o + OBJ_COMMAND_BYTES > 0xFFFF)
        { error = "SCRIPT.DAT: level offset would overflow"; return false; }
    }

    m_bytes.insert(m_bytes.begin() + insertAt, command.begin(), command.end());

    // Every block BEHIND the insertion point moves back by the command
    // length. The target block itself starts exactly at the insertion point
    // and stays where it is - hence strictly greater.
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > insertAt)
        {
            o = static_cast<uint16_t>(o + OBJ_COMMAND_BYTES);
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * i, &o, 2);
        }
    }

    gf.ScriptLen = static_cast<uint16_t>(gf.ScriptLen + OBJ_COMMAND_BYTES);
    std::memcpy(m_bytes.data() + offsetof(GAMEFLOW, ScriptLen), &gf.ScriptLen, 2);

    if (!parse(error)) return false;                // re-read the block bounds
    changed = true;
    return true;
}

bool ScriptDat::clearLevelFlags(const std::string& fileName, uint16_t mask, bool& changed)
{
    changed = false;
    for (size_t i = 0; i < m_levels.size(); ++i)
    {
        if (!sameWithoutExt(m_levels[i].fileName.c_str(), fileName)) continue;

        const uint16_t nw = static_cast<uint16_t>(m_levels[i].flags & ~mask);
        if (nw != m_levels[i].flags)
        {
            m_bytes[m_flagsAt[i]] = static_cast<uint8_t>(nw & 0xFF);
            m_bytes[m_flagsAt[i] + 1] = static_cast<uint8_t>(nw >> 8);
            m_levels[i].flags = nw;
            changed = true;
        }
        return true;
    }
    return false;
}



//----------------------------------------------------------------------------
// Put a CMD_TAKEOBJ into a level's script block (#53).
//
// The command is 3 bytes: opcode 0xDA and two payload bytes, the first of
// which carries the INVENTORY object number (enum invobj_types,
// newinv2.h:121 - 0 = Uzi, 1 = pistols, 19 = pistol ammo). At level start,
// setup.cpp:160 walks the list and calls NailInvItem: whatever is listed,
// Lara no longer has on entering.
//
// Inserted right BEFORE CMD_LEVEL, where the existing TAKEOBJ already stand
// - everything before CMD_LEVEL is read before DoLevel descends into the
// level.
//
// Repeatable: if the command for this object is already there, nothing
// happens.
//----------------------------------------------------------------------------
bool ScriptDat::addInvCommand(uint8_t op, const std::string& levelName, int invObject,
                              bool& changed, std::string& error, bool force)
{
    changed = false;
    if (invObject < 0 || invObject > 255)
    { error = "inventory number outside 0..255"; return false; }

    size_t idx = 0;
    if (!findLevel(levelName, idx)) { error = "level not in the script: " + levelName; return false; }

    // Already there? And where does CMD_LEVEL start?
    size_t beforeLevel = std::string::npos;
    for (size_t q = m_blockStart[idx]; q < m_blockEnd[idx]; )
    {
        const int len = payload(m_bytes[q]);
        if (len < 0) break;
        if (!force && m_bytes[q] == op && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObject))
            return true;                          // already takes it away
        if (m_bytes[q] == 0x81) { beforeLevel = q; break; }
        q += 1 + len;
    }
    if (beforeLevel == std::string::npos)
    { error = "no CMD_LEVEL in the block of " + levelName; return false; }

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));
    if (gf.ScriptLen + TAKEOBJ_BYTES > 0xFFFF)
    { error = "SCRIPT.DAT: script wad would overflow"; return false; }
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > beforeLevel && o + TAKEOBJ_BYTES > 0xFFFF)
        { error = "SCRIPT.DAT: level offset would overflow"; return false; }
    }

    const uint8_t command[TAKEOBJ_BYTES] = { op, static_cast<uint8_t>(invObject), 0 };
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(beforeLevel), command, command + TAKEOBJ_BYTES);

    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > beforeLevel)
        {
            o = static_cast<uint16_t>(o + TAKEOBJ_BYTES);
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * i, &o, 2);
        }
    }
    gf.ScriptLen = static_cast<uint16_t>(gf.ScriptLen + TAKEOBJ_BYTES);
    std::memcpy(m_bytes.data() + offsetof(GAMEFLOW, ScriptLen), &gf.ScriptLen, 2);

    if (!parse(error)) return false;
    changed = true;
    return true;
}
bool ScriptDat::shuffleMusic(uint64_t seed, int& swapped, std::string& error)
{
    swapped = 0;

    // The audio byte sits at a fixed spot behind the flags: the CMD_LEVEL
    // payload is [0]=name string [1][2]=flags [3]=file index [4]=audio, and
    // m_flagsAt points at [1]. The engine takes it over 1:1 as
    // CurrentAtmosphere (DoLevel, gameflow.cpp).
    std::vector<size_t> spots;

    for (size_t i = 0; i < m_levels.size(); ++i)
    {
        if (m_levels[i].flags & 0x8000) continue;                       // GF_NOLEVEL: unplayable placeholders
        if (m_levels[i].fileName.find("RICHCUT") != std::string::npos)  // cutscene levels: the music belongs to the scene
            continue;

        const size_t audioAt = m_flagsAt[i] + 3;
        if (audioAt >= m_bytes.size())
            continue;

        // Shuffle only true atmospheres - the same set the engine treats as
        // a running backdrop in TriggerNormalCDTrack. That also keeps the
        // dev entries GIBBY/TOM out, which sit in the script WITHOUT the
        // NOLEVEL flag and (like DEL) carry the cutscene audio
        // 110/xa14_joby6 - via the flags alone the pot was contaminated:
        // Andrea1 drew the cutscene as background music (heard by the
        // user).
        const uint8_t t = m_bytes[audioAt];
        const bool atmo = t == 117 || t == 118 || t == 121 || (t >= 123 && t <= 130);
        if (!atmo)
            continue;

        spots.push_back(audioAt);
    }

    if (spots.size() < 2)
    {
        error = "too few playable levels for the music swap";
        return false;
    }

    std::vector<uint8_t> values;
    for (size_t s : spots)
        values.push_back(m_bytes[s]);

    std::mt19937_64 rng(seed ^ 0x414D4249454E54ull);    // "AMBIENT" - its own stream next to the item/enemy rolls
    std::shuffle(values.begin(), values.end(), rng);

    for (size_t k = 0; k < spots.size(); ++k)
    {
        if (m_bytes[spots[k]] != values[k]) ++swapped;
        m_bytes[spots[k]] = values[k];
    }

    return true;
}

bool ScriptDat::shuffleSections(uint64_t seed, std::string& orderText, std::string& error)
{
    orderText.clear();

    // Section per level via the file name; everything else (title, GIBBY,
    // DEL, TOM) stays on its slot.
    const auto sectionOf = [](const std::string& file) -> int
    {
        const size_t p = file.find_last_of('\\');
        const std::string k = (p == std::string::npos) ? file : file.substr(p + 1);
        if (k.rfind("ANDREA", 0) == 0) return 0;
        if (k.rfind("JOBY", 0) == 0)   return 1;
        if (k.rfind("ANDY", 0) == 0)   return 2;
        if (k.rfind("RICH", 0) == 0)   return 3;
        return -1;
    };

    // Slots per section in their current order.
    std::vector<int> slots[4];
    for (size_t k = 0; k < m_levels.size(); ++k)
    {
        const int a = sectionOf(m_levels[k].fileName);
        if (a >= 0) slots[a].push_back(m_ofsIndex[k]);
    }
    for (int a = 0; a < 4; ++a)
        if (slots[a].empty()) { error = "a story section is missing from the script"; return false; }

    // All affected slots in ascending order - the permuted sections are
    // laid onto them one after another.
    std::vector<int> targets;
    for (int a = 0; a < 4; ++a) targets.insert(targets.end(), slots[a].begin(), slots[a].end());
    std::sort(targets.begin(), targets.end());

    int order[4] = { 0, 1, 2, 3 };
    std::mt19937_64 rng(seed ^ 0x414253434E495454ull);   // "ABSCNITT" - its own stream
    // A roll should also change something: the identity (1 in 24) is
    // rerolled, otherwise the player sees vanilla and believes the checkbox
    // does nothing.
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        std::shuffle(order, order + 4, rng);
        if (order[0] != 0 || order[1] != 1 || order[2] != 2 || order[3] != 3) break;
    }

    // Collect the old offsets, then redistribute.
    std::vector<uint16_t> old(m_nLevels);
    for (int i = 0; i < m_nLevels; ++i)
        std::memcpy(&old[i], m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);

    static const char* names[4] = { "Rome", "Russia", "Ireland", "VCI" };
    size_t z = 0;
    for (int n = 0; n < 4; ++n)
    {
        const int a = order[n];
        for (int s : slots[a])
        {
            const uint16_t o = old[s];
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * targets[z++], &o, 2);
        }
        if (!orderText.empty()) orderText += " -> ";
        orderText += names[a];
    }

    return parse(error);                                // fresh caches, the blocks now lie differently
}

bool ScriptDat::shuffleLevels(uint64_t seed, bool freeMode, std::string& orderText, std::string& error)
{
    orderText.clear();

    const auto shortName = [](const std::string& file) -> std::string
    {
        const size_t p = file.find_last_of('\\');
        return (p == std::string::npos) ? file : file.substr(p + 1);
    };
    const auto sectionOf = [&](const std::string& file) -> int
    {
        const std::string k = shortName(file);
        if (k.rfind("ANDREA", 0) == 0) return 0;
        if (k.rfind("JOBY", 0) == 0)   return 1;
        if (k.rfind("ANDY", 0) == 0)   return 2;
        if (k.rfind("RICH", 0) == 0)   return 3;
        return -1;
    };

    // Units per section: one level = one unit, RICH2+RICHCUT2 = one unit
    // (the cutscene belongs behind rich2, wherever rich2 lands).
    std::vector<std::vector<int>> units[4];
    for (size_t k = 0; k < m_levels.size(); ++k)
    {
        const int a = sectionOf(m_levels[k].fileName);
        if (a < 0) continue;
        const std::string kn = shortName(m_levels[k].fileName);
        if (kn == "RICHCUT2" && !units[a].empty())
            units[a].back().push_back(m_ofsIndex[k]);            // glue to rich2
        else
            units[a].push_back(std::vector<int>{ m_ofsIndex[k] });
    }

    std::vector<uint16_t> old(m_nLevels);
    for (int i = 0; i < m_nLevels; ++i)
        std::memcpy(&old[i], m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);

    std::mt19937_64 rng(seed ^ 0x4C45564C464F4C47ull);   // "LEVLFOLG"

    // Stage 3 (free): all units into ONE pot, spread over all campaign
    // slots - the sections dissolve. Implemented by pot 0 receiving
    // everything and pots 1..3 staying empty.
    if (freeMode)
    {
        for (int a = 1; a < 4; ++a)
        {
            units[0].insert(units[0].end(), units[a].begin(), units[a].end());
            units[a].clear();
        }
    }

    for (int a = 0; a < 4; ++a)
    {
        if (units[a].empty())
        {
            if (freeMode && a > 0) continue;
            error = "a story section is missing from the script";
            return false;
        }

        // The section's target slots in ascending order, then distribute
        // the permuted units onto them.
        std::vector<int> targets;
        for (const auto& e : units[a]) targets.insert(targets.end(), e.begin(), e.end());
        std::sort(targets.begin(), targets.end());

        // Here too: no identity as long as there is something to swap (with
        // three levels every sixth permutation would be vanilla - the user
        // had the impression the checkbox did not engage).
        {
            const std::vector<std::vector<int>> before = units[a];
            for (int attempt = 0; attempt < 16; ++attempt)
            {
                std::shuffle(units[a].begin(), units[a].end(), rng);
                if (units[a].size() < 2 || units[a] != before) break;
            }
        }

        size_t z = 0;
        for (const auto& e : units[a])
        {
            for (int s : e)
            {
                const uint16_t o = old[s];
                std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * targets[z++], &o, 2);
            }
        }
    }

    if (!parse(error)) return false;

    // The new sequence as text (only the campaign levels, in slot order).
    std::vector<std::pair<int, std::string>> order;
    for (size_t k = 0; k < m_levels.size(); ++k)
        if (sectionOf(m_levels[k].fileName) >= 0)
            order.emplace_back(m_ofsIndex[k], shortName(m_levels[k].fileName));
    std::sort(order.begin(), order.end());
    for (const auto& f : order)
    {
        if (!orderText.empty()) orderText += " ";
        orderText += f.second;
    }
    return true;
}

int ScriptDat::slotOfLevel(const std::string& levelName) const
{
    size_t idx = 0;
    if (!findLevel(levelName, idx)) return -1;
    return m_ofsIndex[idx];
}

bool ScriptDat::resolveChains(std::vector<std::string>& notes, std::string& error)
{
    for (size_t i = 0; i < kLevelChainCount; ++i)
    {
        const LevelChain& k = kLevelChains[i];
        if (!k.give) continue;
        const int sn = slotOfLevel(k.taker);
        const int ss = slotOfLevel(k.donor);
        if (sn < 0 || ss < 0) continue;                      // level not in the script: nothing to do

        // Since the per-level-start inventory reset (LevelChains.h) no key
        // travels between levels anymore - the grant is therefore
        // UNCONDITIONAL, wherever the donor lies. The old case distinction
        // (donor before / a level in between takes it away) is thereby
        // obsolete.
        bool ch = false;
        if (!addGiveObject(k.taker, k.inv, ch, error))
        { error = std::string(k.name) + ": " + error; return false; }

        if (k.nameCmd)
        {
            // Name and inventory pose come from the donor's name command;
            // copyObjectCommand is repeatable and leaves existing ones be.
            bool nch = false;
            std::string nErr;
            if (!copyObjectCommand(k.donor, k.taker, k.nameCmd, nch, nErr))
                notes.push_back(std::string(k.name) + ": name command not copied (" + nErr + ")");
        }

        notes.push_back(std::string(k.name) + ": granted via GIVEOBJ");
    }
    return true;
}

void ScriptDat::randomizerChanges(Changes& e)
{
    e = Changes();

    // 1. Strip the Ireland levels of the young-Lara bit.
    //
    // GF_YOUNGLARA disables the weapon system at eight places in the engine
    // - Lara travels there as a child and can carry nothing. User
    // requirement: the andy levels must always have weapons, or Lara faces
    // a TwoGun empty-handed.
    static const char* kIreland[] = { "andy1", "andy2", "andy3" };
    for (const char* lv : kIreland)
    {
        bool ch = false;
        if (!clearLevelFlags(lv, GF_YOUNGLARA, ch)) ++e.levelsMissing;
        else if (ch) ++e.flagsChanged;
    }

    // If the script does not know the Ireland levels, it is not one of
    // Chronicles. Then nothing else is touched here - and the caller does
    // not save.
    if (e.levelsMissing > 0) return;

    // 2. joby3 receives the KEY8 entry from joby2.
    //
    // The randomizer puts the model of Key Item 8 into joby3 (see
    // Randomizer.cpp). Name and inventory pose, however, live in the
    // script, and joby3 has no entry for them. Vanilla never notices,
    // because joby2 runs right before it and sets KEY8 there -
    // inventry_objects_list is never reset between levels. But whoever
    // saves and loads in joby3 sees the factory value: the key would be
    // called "Load".
    //
    // If it fails, it stays a note: the level is playable, the key is just
    // named wrongly. An abort would be the harsher punishment.
    bool keyCh = false;
    if (copyObjectCommand("joby2", "joby3", cmdKey(8), keyCh, e.keyError))
    {
        e.keyCopied = keyCh;
        e.keyError.clear();
    }

    // 3. rich2 takes the pistols away from Lara.
    //
    // rich2's slot 1 (PISTOLS_ANIM) is no pistol at all: Core put a second
    // HK with a scripted animation set there (mesh-identical to slot 5, its
    // own anims with KillItem and shot sounds - richcut2 carries the same
    // slot). The randomizer therefore keeps PISTOLS_ITEM out of rich2, but
    // that is not enough: Lara BRINGS the pistols from the previous level,
    // and drawn, her arms run over the scripted HK anims. Seen by the user
    // in game ("the pistols are bugged in rich2, Lara herself and the
    // inventory item").
    //
    // Vanilla already takes three things from rich2 (inventory objects 78,
    // 59, 60) and grants two (7, 30) - the pistols just are not among them.
    // That is exactly where they belong. Ammo along with them, or an entry
    // without a weapon stays in the inventory.
    for (int obj : { INV_PISTOLS, INV_PISTOLS_AMMO })
    {
        bool ch = false;
        std::string tErr;
        if (!addTakeObject("rich2", obj, ch, tErr))
            e.pistolError = tErr;
        else if (ch)
            ++e.pistolsRemoved;
    }

    // 4. joby3 NO longer takes Lara's weapons away.
    //
    // Vanilla disarms Lara in this level (Uzi, pistols, revolver,
    // lasersight): she is brought aboard as a prisoner, and the only enemy
    // up to the armory is the chef - who used to be immortal, because
    // ChefControl never queried his hit points. Neither fits anymore: the
    // randomizer places enemies everywhere, and the chef now has his death
    // handling (chef.cpp). So away with the four take-aways.
    static const int kJoby3Weapons[] = { 0 /*Uzi*/, INV_PISTOLS, 3 /*revolver*/, 20 /*lasersight*/ };

    for (int obj : kJoby3Weapons)
    {
        bool ch = false;
        std::string wErr;
        if (!removeInvCommand(0xDA, "joby3", obj, ch, wErr))
            e.weaponError = wErr;
        else if (ch)
            ++e.joby3Weapons;
    }

    // 5. Andrea1 NO longer takes the Uzi away.
    //
    // Core's own block hands Lara the Uzi and takes it away again two
    // commands later, so the grant is dead on arrival. In vanilla that is
    // invisible: Andrea1 is the first level, there is nothing to carry in,
    // and nobody misses a weapon that was never there. With a rolled level
    // order Andrea1 can come sixth, and the take-away then eats an Uzi the
    // player earned three levels ago. Seen by the user in a full playthrough
    // ("Andrea1: Uzi is still removed at the start").
    //
    // Only the TAKE goes. The GIVE stays: an Uzi at the start of Streets of
    // Rome is Core's intent, and the weapon scatter is built on top of it.
    {
        bool ch = false;
        std::string uErr;
        if (!removeInvCommand(0xDA, "Andrea1", 0 /*Uzi*/, ch, uErr))
            e.weaponError = uErr;
        else if (ch)
            ++e.andrea1Uzi;
    }
}

bool ScriptDat::save(const std::wstring& path, std::string& error) const
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) { error = "SCRIPT.DAT not writable"; return false; }
    const bool ok = std::fwrite(m_bytes.data(), 1, m_bytes.size(), f) == m_bytes.size();
    std::fclose(f);
    if (!ok) { error = "SCRIPT.DAT written incompletely"; return false; }
    return true;
}

bool ScriptDat::addTakeObject(const std::string& levelName, int invObject, bool& changed, std::string& error)
{
    return addInvCommand(0xDA, levelName, invObject, changed, error);      // CMD_TAKEOBJ
}

bool ScriptDat::addGiveObject(const std::string& levelName, int invObject, bool& changed, std::string& error)
{
    return addInvCommand(0xD9, levelName, invObject, changed, error);      // CMD_GIVEOBJ
}

bool ScriptDat::takesObject(size_t idx, int invObject) const
{
    for (size_t q = m_blockStart[idx]; q < m_blockEnd[idx]; )
    {
        const int len = payload(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == 0xDA && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObject))
            return true;
        if (m_bytes[q] == 0x81) break;                      // CMD_LEVEL: block head over
        q += 1 + len;
    }
    return false;
}

bool ScriptDat::givesObject(size_t idx, int invObject) const
{
    for (size_t q = m_blockStart[idx]; q < m_blockEnd[idx]; )
    {
        const int len = payload(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == 0xD9 && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObject))
            return true;
        if (m_bytes[q] == 0x81) break;
        q += 1 + len;
    }
    return false;
}

bool ScriptDat::cleanupInventory(std::vector<std::string>& notes, std::string& error)
{
    // See LevelChains.h: the key family is generically named, a "Garden
    // Key" carried along BECOMES something else in the next level. So: at
    // the start of every level, take away everything an EARLIER level could
    // have offered and that neither this nor a later level imports via a
    // chain. The crossbow family ("Grappling Gun") everywhere except in
    // rich3.
    const auto shortU = [](const std::string& file) -> std::string
    {
        const size_t p = file.find_last_of('\\');
        std::string k = (p == std::string::npos) ? file : file.substr(p + 1);
        for (char& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return k;
    };
    const auto equals = [](const std::string& a, const char* b)
    {
        std::string bb = b;
        for (char& c : bb) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return a == bb;
    };

    // Levels in slot order
    std::vector<std::pair<int, size_t>> order;      // (slot, idx)
    for (size_t k = 0; k < m_levels.size(); ++k)
        order.emplace_back(m_ofsIndex[k], k);
    std::sort(order.begin(), order.end());

    // Clean up only campaign levels (title/GIBBY/DEL/TOM stay).
    const auto isCampaign = [&](const std::string& k)
    {
        return k.rfind("ANDREA", 0) == 0 || k.rfind("JOBY", 0) == 0 ||
               k.rfind("ANDY", 0) == 0 || k.rfind("RICH", 0) == 0;
    };

    for (const auto& f : order)
    {
        const size_t idx  = f.second;
        const std::string name = shortU(m_levels[idx].fileName);
        if (!isCampaign(name)) continue;

        // Full reset of the level-bound families at EVERY level start (user
        // decision): puzzles 1-8 with halves, keys 1-8 with halves, pickups
        // 1-4 with halves, crowbar, burning torch, the three examine objects
        // and the chloroform set - and the crossbow family everywhere except
        // rich3. Whatever a level's own block grants (the vanilla Iris in
        // rich2, the chain grants) is not touched - a TAKE behind the GIVE
        // would cancel the grant (setup.cpp applies grants first, then
        // take-aways).
        //
        // Pickup 4 - the secret rose - used to be exempt here, and the
        // examine objects, the torch and the chloroform were never in the
        // list at all. All of them travelled: the rose sat in the ring of a
        // fresh level before a single secret had been found there, and
        // rich1's chloroform went on into rich2 and rich3. Both seen by the
        // user in a full playthrough.
        //
        // Taking the rose does NOT touch the secret count. The engine keeps
        // that in savegame.Level.Secrets and resets it itself at every level
        // start (gameflow.cpp); the "n of 3" line reads that counter, not
        // the item (newinv2.cpp). Only the ring entry goes.
        std::vector<int> takeList, regive;
        const auto take = [&](int inv)
        {
            // If the block grants the item itself (the vanilla Iris in
            // rich2, Andrea1's pickup 1), it is still taken away - and
            // granted anew afterwards. The engine applies grants behind
            // take-aways in a third phase (gameflow.cpp/setup.cpp), so the
            // amount comes out to exactly one. Without this, a carried-in
            // copy survived, and a counted puzzle item stood twice in the
            // inventory ("2 x catapult", seen by the user).
            if (givesObject(idx, inv) && !givesObjectAfterTake(idx, inv)) regive.push_back(inv);
            if (std::find(takeList.begin(), takeList.end(), inv) == takeList.end()) takeList.push_back(inv);
        };
        for (int n = 1; n <= 8; ++n)
        {
            take(INV_PUZZLE(n)); take(INV_PUZZLE_COMBO(n, 1)); take(INV_PUZZLE_COMBO(n, 2));
            take(INV_KEY(n));    take(INV_KEY_COMBO(n, 1));    take(INV_KEY_COMBO(n, 2));
        }
        for (int n = 1; n <= 4; ++n)
        {
            take(INV_PICKUP(n)); take(INV_PICKUP_COMBO(n, 1)); take(INV_PICKUP_COMBO(n, 2));
        }
        take(INV_CROWBAR);
        take(INV_BURNING_TORCH);
        for (int n = 1; n <= 3; ++n) take(INV_EXAMINE(n));
        for (const int* c = kChloroform; *c; ++c) take(*c);
        if (!equals(name, "RICH3"))
            for (const int* c = kCrossbowFamily; *c; ++c) take(*c);

        for (int inv : takeList)
        {
            bool ch = false;
            if (!addTakeObject(name, inv, ch, error))     // short name - findLevel compares without the path
            { error = name + ": " + error; return false; }
        }
        if (!takeList.empty())
        {
            std::string z = name + ": cleans up " + std::to_string(takeList.size()) + " inventory slots (";
            for (size_t n = 0; n < takeList.size() && n < 6; ++n) z += (n ? "," : "") + std::to_string(takeList[n]);
            if (takeList.size() > 6) z += ",...";
            notes.push_back(z + ")");
        }

        // ... and re-grant the block's own grants afterwards (force: the
        // old command stands further up and is canceled by the take-away).
        for (int inv : regive)
        {
            bool ch = false;
            if (!addInvCommand(0xD9, name, inv, ch, error, true))
            { error = name + ": " + error; return false; }
        }
    }
    return true;
}

// Is there a GIVEOBJ for this object BEHIND a TAKEOBJ in the block? That is
// exactly what the cleaner creates - the query makes it repeatable.
bool ScriptDat::givesObjectAfterTake(size_t idx, int invObject) const
{
    bool takeSeen = false;

    for (size_t q = m_blockStart[idx]; q < m_blockEnd[idx]; )
    {
        const int len = payload(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == 0xDA) takeSeen = true;
        if (takeSeen && m_bytes[q] == 0xD9 && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObject))
            return true;
        if (m_bytes[q] == 0x81) break;
        q += 1 + len;
    }
    return false;
}

bool ScriptDat::removeInvCommand(uint8_t op, const std::string& levelName, int invObject,
                                bool& changed, std::string& error)
{
    changed = false;

    size_t idx = 0;
    if (!findLevel(levelName, idx)) { error = "level not in the script: " + levelName; return false; }

    size_t spot = std::string::npos;
    for (size_t q = m_blockStart[idx]; q < m_blockEnd[idx]; )
    {
        const int len = payload(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == op && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObject))
        { spot = q; break; }
        if (m_bytes[q] == 0x81) break;
        q += 1 + len;
    }
    if (spot == std::string::npos) return true;            // not there: nothing to do (repeatable)

    m_bytes.erase(m_bytes.begin() + static_cast<ptrdiff_t>(spot),
                  m_bytes.begin() + static_cast<ptrdiff_t>(spot + TAKEOBJ_BYTES));

    // Exactly the reverse of addInvCommand: every block BEHIND the deletion
    // point moves forward by the command length.
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > spot)
        {
            o = static_cast<uint16_t>(o - TAKEOBJ_BYTES);
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * i, &o, 2);
        }
    }

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));
    gf.ScriptLen = static_cast<uint16_t>(gf.ScriptLen - TAKEOBJ_BYTES);
    std::memcpy(m_bytes.data() + offsetof(GAMEFLOW, ScriptLen), &gf.ScriptLen, 2);

    if (!parse(error)) return false;
    changed = true;
    return true;
}

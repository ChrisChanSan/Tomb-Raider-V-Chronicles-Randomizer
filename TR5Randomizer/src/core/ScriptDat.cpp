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
#include <cstddef>

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

    // Nutzbytelaenge je Befehl, 1:1 aus DoGameflow (game/gameflow.cpp:160ff).
    // Opcodes aus enum gf_commands (game/gameflow.h), CMD_FMV = 0x80.
    //
    // Die Objektbefehle (0x92..0xD8: Key/Puzzle/Pickup/Examine und die Combos)
    // tragen 14 Nutzbytes - Namensindex, yoff, Groesse und drei Drehungen plus
    // Flags. Genau die fehlten hier frueher: der alte Parser gab ihnen wie
    // allem Unbekannten 2 Nutzbytes. Dass er trotzdem ans Ziel kam, war Glueck
    // - ein Objektbefehl ist 15 Byte lang, und 15 ist durch 3 teilbar, also
    // fand der zu kurze Schritt nach fuenf Spruengen zufaellig wieder den
    // Takt. Er haette gerissen, sobald eines der angesprungenen Nutzbytes
    // (Versatz 3, 6, 9 oder 12 im Befehl) auf einem Opcode mit anderer Laenge
    // gelandet waere, etwa 0x80 aus einer Drehung von 0x8000.
    int nutzlast(uint8_t cmd)
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
            if (cmd >= ScriptDat::CMD_OBJ_ERSTER && cmd <= ScriptDat::CMD_OBJ_LETZTER)
                return ScriptDat::OBJEKT_NUTZBYTES;
            return -1;                                          // unbekannt: anhalten
        }
    }

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
    m_levels.clear();
    m_flagsAt.clear();
    m_blockAn.clear();
    m_blockBis.clear();
    m_ofsIndex.clear();

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

    m_scriptOfsAt = scriptOfsAt;
    m_scriptWad   = scriptWad;
    m_nLevels     = gf.nLevels;

    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, m_bytes.data() + o, 2); return v; };

    for (int i = 0; i < gf.nLevels; ++i)
    {
        const size_t start = scriptWad + u16(scriptOfsAt + 2ull * i);

        // Blockende ist der naechstgroessere Offset, nicht stur der naechste
        // Eintrag: die Reihenfolge der Offsettabelle ist nirgends zugesichert.
        size_t ende = scriptWad + gf.ScriptLen;
        for (int j = 0; j < gf.nLevels; ++j)
        {
            const size_t o = scriptWad + u16(scriptOfsAt + 2ull * j);
            if (o > start && o < ende) ende = o;
        }
        if (ende > m_bytes.size()) ende = m_bytes.size();

        for (size_t q = start; q < ende; )
        {
            const uint8_t cmd = m_bytes[q];
            const int len = nutzlast(cmd);
            if (len < 0) break;                       // unbekannt: hier ist Schluss

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
                m_blockAn.push_back(start);
                m_blockBis.push_back(ende);
                m_ofsIndex.push_back(i);
                break;
            }

            q += 1 + len;
        }
    }

    if (m_levels.empty()) { error = "SCRIPT.DAT: kein einziger CMD_LEVEL gefunden"; return false; }
    return true;
}

// Sucht einen Objektbefehl im Block eines Levels. Liefert die Stelle des
// Opcodes oder npos.
size_t ScriptDat::findeBefehl(size_t idx, uint8_t cmd) const
{
    for (size_t q = m_blockAn[idx]; q < m_blockBis[idx]; )
    {
        const int len = nutzlast(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == cmd) return q;
        q += 1 + len;
    }
    return std::string::npos;
}

bool ScriptDat::findeLevel(const std::string& name, size_t& idx) const
{
    for (size_t i = 0; i < m_levels.size(); ++i)
        if (gleichOhneEndung(m_levels[i].fileName.c_str(), name)) { idx = i; return true; }
    return false;
}

bool ScriptDat::copyObjectCommand(const std::string& vonLevel, const std::string& nachLevel,
                                  uint8_t cmd, bool& changed, std::string& error)
{
    changed = false;

    if (cmd < CMD_OBJ_ERSTER || cmd > CMD_OBJ_LETZTER)
    { error = "kein Objektbefehl: " + std::to_string((int)cmd); return false; }

    size_t iVon = 0, iNach = 0;
    if (!findeLevel(vonLevel, iVon))   { error = "Level nicht im Skript: " + vonLevel;  return false; }
    if (!findeLevel(nachLevel, iNach)) { error = "Level nicht im Skript: " + nachLevel; return false; }

    // Hat das Ziel den Befehl schon, bleibt alles wie es ist. Der Aufruf ist
    // damit wiederholbar - genau wie clearLevelFlags, und aus demselben Grund:
    // der Randomizer laeuft mehrfach ueber dieselbe Installation.
    if (findeBefehl(iNach, cmd) != std::string::npos) return true;

    const size_t quelle = findeBefehl(iVon, cmd);
    if (quelle == std::string::npos)
    { error = "Level " + vonLevel + " hat den Befehl gar nicht"; return false; }
    if (quelle + OBJEKT_BEFEHL_BYTES > m_bytes.size())
    { error = "Befehl in " + vonLevel + " ragt aus der Datei"; return false; }

    std::vector<uint8_t> befehl(m_bytes.begin() + quelle,
                                m_bytes.begin() + quelle + OBJEKT_BEFEHL_BYTES);

    // Vorne in den Block: die Objektbefehle stehen vanilla alle vor CMD_LEVEL,
    // und das muessen sie auch - DoGameflow arbeitet EINE Schleife ab, und
    // CMD_LEVEL steigt dort ins Level ab. Was dahinter stuende, wuerde erst
    // nach dem Level gelesen und kaeme fuer die Namensvergabe zu spaet.
    const size_t einfuegeAn = m_blockAn[iNach];

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));

    if (gf.ScriptLen + OBJEKT_BEFEHL_BYTES > 0xFFFF)
    { error = "SCRIPT.DAT: Skript-Wad wuerde ueberlaufen"; return false; }

    // Erst pruefen, dann schreiben: kein halb umgebautes Skript hinterlassen.
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > einfuegeAn && o + OBJEKT_BEFEHL_BYTES > 0xFFFF)
        { error = "SCRIPT.DAT: Leveloffset wuerde ueberlaufen"; return false; }
    }

    m_bytes.insert(m_bytes.begin() + einfuegeAn, befehl.begin(), befehl.end());

    // Jeder Block HINTER der Einfuegestelle rueckt um die Befehlslaenge nach.
    // Der Zielblock selbst faengt genau an der Einfuegestelle an und bleibt,
    // wo er ist - deshalb strikt groesser.
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > einfuegeAn)
        {
            o = static_cast<uint16_t>(o + OBJEKT_BEFEHL_BYTES);
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * i, &o, 2);
        }
    }

    gf.ScriptLen = static_cast<uint16_t>(gf.ScriptLen + OBJEKT_BEFEHL_BYTES);
    std::memcpy(m_bytes.data() + offsetof(GAMEFLOW, ScriptLen), &gf.ScriptLen, 2);

    if (!zerlege(error)) return false;                // Blockgrenzen neu einlesen
    changed = true;
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


void ScriptDat::randomizerEingriffe(Eingriffe& e)
{
    e = Eingriffe();

    // 1. Den Ireland-Leveln das Kind-Lara-Bit nehmen.
    //
    // GF_YOUNGLARA schaltet an acht Stellen im Motor das Waffensystem ab -
    // Lara ist dort als Kind unterwegs und kann nichts tragen. Nutzervorgabe:
    // die Andy-Level muessen immer Waffen haben, sonst steht Lara einem Two
    // Gun mit leeren Haenden gegenueber.
    static const char* kIreland[] = { "andy1", "andy2", "andy3" };
    for (const char* lv : kIreland)
    {
        bool ch = false;
        if (!clearLevelFlags(lv, GF_YOUNGLARA, ch)) ++e.levelFehlend;
        else if (ch) ++e.flagsGeaendert;
    }

    // Kennt das Skript die Ireland-Level nicht, ist es keines von Chronicles.
    // Dann wird hier gar nichts mehr angefasst - und der Aufrufer speichert
    // nicht.
    if (e.levelFehlend > 0) return;

    // 2. joby3 bekommt den KEY8-Eintrag aus joby2.
    //
    // Der Randomizer legt joby3 das Modell von Key Item 8 hinein (siehe
    // Randomizer.cpp). Name und Inventarpose stehen aber im Skript, und joby3
    // hat dafuer keinen Eintrag. Vanilla faellt das nie auf, weil joby2 direkt
    // davor laeuft und KEY8 dort setzt - inventry_objects_list wird zwischen
    // Leveln nie zurueckgesetzt. Wer aber in joby3 speichert und laedt,
    // bekommt den Werkswert zu sehen: der Schluessel hiesse "Load".
    //
    // Scheitert es, bleibt es bei der Notiz: das Level ist spielbar, der
    // Schluessel heisst halt falsch. Ein Abbruch waere die haertere Strafe.
    bool keyCh = false;
    if (copyObjectCommand("joby2", "joby3", cmdKey(8), keyCh, e.keyFehler))
    {
        e.keyKopiert = keyCh;
        e.keyFehler.clear();
    }
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

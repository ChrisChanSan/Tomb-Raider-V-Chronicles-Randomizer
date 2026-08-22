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
#include "Levelketten.h"

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



//----------------------------------------------------------------------------
// Ein CMD_TAKEOBJ in den Skriptblock eines Levels setzen (#53).
//
// Der Befehl ist 3 Byte: Opcode 0xDA und zwei Nutzbytes, davon traegt das
// erste die INVENTAR-Objektnummer (enum invobj_types, newinv2.h:121 -
// 0 = Uzi, 1 = Pistolen, 19 = Pistolenmunition). Beim Levelstart laeuft
// setup.cpp:160 die Liste durch und ruft NailInvItem: was drinsteht, hat
// Lara beim Betreten nicht mehr.
//
// Eingefuegt wird direkt VOR CMD_LEVEL, dort wo die vorhandenen TAKEOBJ
// schon stehen - alles vor CMD_LEVEL wird gelesen, bevor DoLevel ins Level
// absteigt.
//
// Wiederholbar: steht der Befehl fuer dieses Objekt schon da, passiert
// nichts.
//----------------------------------------------------------------------------
bool ScriptDat::addInvBefehl(uint8_t op, const std::string& levelName, int invObjekt,
                              bool& changed, std::string& error, bool force)
{
    changed = false;
    if (invObjekt < 0 || invObjekt > 255)
    { error = "Inventarnummer ausserhalb 0..255"; return false; }

    size_t idx = 0;
    if (!findeLevel(levelName, idx)) { error = "Level nicht im Skript: " + levelName; return false; }

    // Schon da? Und wo faengt CMD_LEVEL an?
    size_t vorLevel = std::string::npos;
    for (size_t q = m_blockAn[idx]; q < m_blockBis[idx]; )
    {
        const int len = nutzlast(m_bytes[q]);
        if (len < 0) break;
        if (!force && m_bytes[q] == op && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObjekt))
            return true;                          // nimmt es schon weg
        if (m_bytes[q] == 0x81) { vorLevel = q; break; }
        q += 1 + len;
    }
    if (vorLevel == std::string::npos)
    { error = "kein CMD_LEVEL im Block von " + levelName; return false; }

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));
    if (gf.ScriptLen + TAKEOBJ_BYTES > 0xFFFF)
    { error = "SCRIPT.DAT: Skript-Wad wuerde ueberlaufen"; return false; }
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > vorLevel && o + TAKEOBJ_BYTES > 0xFFFF)
        { error = "SCRIPT.DAT: Leveloffset wuerde ueberlaufen"; return false; }
    }

    const uint8_t befehl[TAKEOBJ_BYTES] = { op, static_cast<uint8_t>(invObjekt), 0 };
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(vorLevel), befehl, befehl + TAKEOBJ_BYTES);

    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > vorLevel)
        {
            o = static_cast<uint16_t>(o + TAKEOBJ_BYTES);
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * i, &o, 2);
        }
    }
    gf.ScriptLen = static_cast<uint16_t>(gf.ScriptLen + TAKEOBJ_BYTES);
    std::memcpy(m_bytes.data() + offsetof(GAMEFLOW, ScriptLen), &gf.ScriptLen, 2);

    if (!zerlege(error)) return false;
    changed = true;
    return true;
}
bool ScriptDat::musikWuerfeln(uint64_t seed, int& getauscht, std::string& error)
{
    getauscht = 0;

    // Das Audio-Byte liegt fix hinter den Flags: CMD_LEVEL-Nutzlast ist
    // [0]=Name-String [1][2]=Flags [3]=Dateiindex [4]=Audio, und m_flagsAt
    // zeigt auf [1]. Die Engine uebernimmt es 1:1 als CurrentAtmosphere
    // (DoLevel, gameflow.cpp).
    std::vector<size_t> stellen;

    for (size_t i = 0; i < m_levels.size(); ++i)
    {
        if (m_levels[i].flags & 0x8000) continue;                       // GF_NOLEVEL: unspielbare Platzhalter
        if (m_levels[i].fileName.find("RICHCUT") != std::string::npos)  // Cutscene-Level: Musik gehoert zur Szene
            continue;

        const size_t audioAt = m_flagsAt[i] + 3;
        if (audioAt >= m_bytes.size())
            continue;

        // Nur echte Atmosphaeren mischen - dieselbe Menge, die die Engine in
        // TriggerNormalCDTrack als Dauerkulisse behandelt. Das haelt zugleich
        // die Dev-Eintraege GIBBY/TOM heraus, die OHNE NOLEVEL-Flag im Skript
        // stehen und (wie DEL) das Cutscene-Audio 110/xa14_joby6 tragen -
        // ueber die Flags allein war der Topf verseucht: Andrea1 zog die
        // Cutscene als Hintergrundmusik (vom Nutzer gehoert).
        const uint8_t t = m_bytes[audioAt];
        const bool atmo = t == 117 || t == 118 || t == 121 || (t >= 123 && t <= 130);
        if (!atmo)
            continue;

        stellen.push_back(audioAt);
    }

    if (stellen.size() < 2)
    {
        error = "zu wenige spielbare Level fuer den Musiktausch";
        return false;
    }

    std::vector<uint8_t> werte;
    for (size_t s : stellen)
        werte.push_back(m_bytes[s]);

    std::mt19937_64 rng(seed ^ 0x414D4249454E54ull);    // "AMBIENT" - eigener Strom neben Item-/Gegnerwuerfeln
    std::shuffle(werte.begin(), werte.end(), rng);

    for (size_t k = 0; k < stellen.size(); ++k)
    {
        if (m_bytes[stellen[k]] != werte[k]) ++getauscht;
        m_bytes[stellen[k]] = werte[k];
    }

    return true;
}

bool ScriptDat::abschnitteWuerfeln(uint64_t seed, std::string& reihenfolge, std::string& error)
{
    reihenfolge.clear();

    // Abschnitt je Level ueber den Dateinamen; alles andere (Titel, GIBBY,
    // DEL, TOM) bleibt auf seinem Slot.
    const auto abschnitt = [](const std::string& datei) -> int
    {
        const size_t p = datei.find_last_of('\\');
        const std::string k = (p == std::string::npos) ? datei : datei.substr(p + 1);
        if (k.rfind("ANDREA", 0) == 0) return 0;
        if (k.rfind("JOBY", 0) == 0)   return 1;
        if (k.rfind("ANDY", 0) == 0)   return 2;
        if (k.rfind("RICH", 0) == 0)   return 3;
        return -1;
    };

    // Slots je Abschnitt in ihrer heutigen Reihenfolge.
    std::vector<int> slots[4];
    for (size_t k = 0; k < m_levels.size(); ++k)
    {
        const int a = abschnitt(m_levels[k].fileName);
        if (a >= 0) slots[a].push_back(m_ofsIndex[k]);
    }
    for (int a = 0; a < 4; ++a)
        if (slots[a].empty()) { error = "ein Story-Abschnitt fehlt im Skript"; return false; }

    // Alle betroffenen Slots in aufsteigender Folge - dorthin werden die
    // permutierten Abschnitte der Reihe nach gelegt.
    std::vector<int> ziele;
    for (int a = 0; a < 4; ++a) ziele.insert(ziele.end(), slots[a].begin(), slots[a].end());
    std::sort(ziele.begin(), ziele.end());

    int folge[4] = { 0, 1, 2, 3 };
    std::mt19937_64 rng(seed ^ 0x414253434E495454ull);   // "ABSCNITT" - eigener Strom
    // Ein Wuerfeln soll auch etwas aendern: die Identitaet (1 von 24) wird
    // neu gewuerfelt, sonst sieht der Spieler Vanilla und glaubt, der Haken
    // tue nichts.
    for (int versuch = 0; versuch < 16; ++versuch)
    {
        std::shuffle(folge, folge + 4, rng);
        if (folge[0] != 0 || folge[1] != 1 || folge[2] != 2 || folge[3] != 3) break;
    }

    // Alte Offsets einsammeln, dann neu verteilen.
    std::vector<uint16_t> alt(m_nLevels);
    for (int i = 0; i < m_nLevels; ++i)
        std::memcpy(&alt[i], m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);

    static const char* namen[4] = { "Rom", "Russland", "Irland", "VCI" };
    size_t z = 0;
    for (int n = 0; n < 4; ++n)
    {
        const int a = folge[n];
        for (int s : slots[a])
        {
            const uint16_t o = alt[s];
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * ziele[z++], &o, 2);
        }
        if (!reihenfolge.empty()) reihenfolge += " -> ";
        reihenfolge += namen[a];
    }

    return zerlege(error);                                // Caches neu, Bloecke liegen jetzt anders
}

bool ScriptDat::levelWuerfeln(uint64_t seed, bool frei, std::string& reihenfolge, std::string& error)
{
    reihenfolge.clear();

    const auto kurzname = [](const std::string& datei) -> std::string
    {
        const size_t p = datei.find_last_of('\\');
        return (p == std::string::npos) ? datei : datei.substr(p + 1);
    };
    const auto abschnitt = [&](const std::string& datei) -> int
    {
        const std::string k = kurzname(datei);
        if (k.rfind("ANDREA", 0) == 0) return 0;
        if (k.rfind("JOBY", 0) == 0)   return 1;
        if (k.rfind("ANDY", 0) == 0)   return 2;
        if (k.rfind("RICH", 0) == 0)   return 3;
        return -1;
    };

    // Einheiten je Abschnitt: ein Level = eine Einheit, RICH2+RICHCUT2 = eine
    // Einheit (die Cutscene gehoert hinter rich2, egal wo rich2 landet).
    std::vector<std::vector<int>> einheiten[4];
    for (size_t k = 0; k < m_levels.size(); ++k)
    {
        const int a = abschnitt(m_levels[k].fileName);
        if (a < 0) continue;
        const std::string kn = kurzname(m_levels[k].fileName);
        if (kn == "RICHCUT2" && !einheiten[a].empty())
            einheiten[a].back().push_back(m_ofsIndex[k]);        // an rich2 kleben
        else
            einheiten[a].push_back(std::vector<int>{ m_ofsIndex[k] });
    }

    std::vector<uint16_t> alt(m_nLevels);
    for (int i = 0; i < m_nLevels; ++i)
        std::memcpy(&alt[i], m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);

    std::mt19937_64 rng(seed ^ 0x4C45564C464F4C47ull);   // "LEVLFOLG"

    // Stufe 3 (frei): alle Einheiten in EINEN Topf, ueber alle Kampagnen-
    // Slots verteilt - die Abschnitte loesen sich auf. Umgesetzt, indem
    // Topf 0 alles bekommt und die Toepfe 1..3 leer bleiben.
    if (frei)
    {
        for (int a = 1; a < 4; ++a)
        {
            einheiten[0].insert(einheiten[0].end(), einheiten[a].begin(), einheiten[a].end());
            einheiten[a].clear();
        }
    }

    for (int a = 0; a < 4; ++a)
    {
        if (einheiten[a].empty())
        {
            if (frei && a > 0) continue;
            error = "ein Story-Abschnitt fehlt im Skript";
            return false;
        }

        // Zielslots des Abschnitts in aufsteigender Folge, dann die Einheiten
        // permutiert darauf verteilen.
        std::vector<int> ziele;
        for (const auto& e : einheiten[a]) ziele.insert(ziele.end(), e.begin(), e.end());
        std::sort(ziele.begin(), ziele.end());

        // Auch hier: keine Identitaet, solange es etwas zu tauschen gibt (bei
        // drei Leveln waere jede sechste Permutation Vanilla - der Nutzer
        // hatte den Eindruck, der Haken greife nicht).
        {
            const std::vector<std::vector<int>> vorher = einheiten[a];
            for (int versuch = 0; versuch < 16; ++versuch)
            {
                std::shuffle(einheiten[a].begin(), einheiten[a].end(), rng);
                if (einheiten[a].size() < 2 || einheiten[a] != vorher) break;
            }
        }

        size_t z = 0;
        for (const auto& e : einheiten[a])
        {
            for (int s : e)
            {
                const uint16_t o = alt[s];
                std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * ziele[z++], &o, 2);
            }
        }
    }

    if (!zerlege(error)) return false;

    // Neue Abfolge als Text (nur die Kampagnen-Level, in Slotreihenfolge).
    std::vector<std::pair<int, std::string>> folge;
    for (size_t k = 0; k < m_levels.size(); ++k)
        if (abschnitt(m_levels[k].fileName) >= 0)
            folge.emplace_back(m_ofsIndex[k], kurzname(m_levels[k].fileName));
    std::sort(folge.begin(), folge.end());
    for (const auto& f : folge)
    {
        if (!reihenfolge.empty()) reihenfolge += " ";
        reihenfolge += f.second;
    }
    return true;
}

int ScriptDat::slotVonLevel(const std::string& levelName) const
{
    size_t idx = 0;
    if (!findeLevel(levelName, idx)) return -1;
    return m_ofsIndex[idx];
}

bool ScriptDat::kettenAufloesen(std::vector<std::string>& notizen, std::string& error)
{
    for (size_t i = 0; i < kLevelkettenZahl; ++i)
    {
        const Levelkette& k = kLevelketten[i];
        if (!k.geben) continue;
        const int sn = slotVonLevel(k.nehmer);
        const int ss = slotVonLevel(k.spender);
        if (sn < 0 || ss < 0) continue;                      // Level nicht im Skript: nichts zu tun

        // Seit dem Inventar-Reset je Levelstart (Levelketten.h) wandert kein
        // Schluessel mehr zwischen Leveln - die Gabe ist darum UNBEDINGT,
        // egal wo der Spender liegt. Die alte Fallunterscheidung (Spender
        // davor / ein Level dazwischen nimmt es weg) ist damit hinfaellig.
        bool ch = false;
        if (!addGiveObject(k.nehmer, k.inv, ch, error))
        { error = std::string(k.name) + ": " + error; return false; }

        if (k.nameCmd)
        {
            // Name und Inventarpose kommen aus dem Namensbefehl des Spenders;
            // copyObjectCommand ist wiederholbar und laesst vorhandene stehen.
            bool nch = false;
            std::string nErr;
            if (!copyObjectCommand(k.spender, k.nehmer, k.nameCmd, nch, nErr))
                notizen.push_back(std::string(k.name) + ": Namensbefehl nicht kopiert (" + nErr + ")");
        }

        notizen.push_back(std::string(k.name) + ": per GIVEOBJ gegeben");
    }
    return true;
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

    // 3. rich2 nimmt Lara die Pistolen ab.
    //
    // rich2s Slot 1 (PISTOLS_ANIM) ist gar keine Pistole: Core hat dort eine
    // zweite HK mit gescriptetem Animationssatz liegen (mesh-identisch mit
    // Slot 5, eigene Anims mit KillItem und Schusssounds - richcut2 traegt
    // denselben Slot). Der Randomizer haelt PISTOLS_ITEM deshalb aus rich2
    // heraus, aber das reicht nicht: Lara BRINGT die Pistolen aus dem
    // Vorlevel mit, und gezogen laufen ihre Arme ueber die gescripteten
    // HK-Anims. Vom Nutzer im Spiel gesehen ("die Pistolen sind in rich2
    // verbugt, Lara an sich und das Inventar-Item").
    //
    // Vanilla nimmt rich2 schon drei Dinge weg (Inventarobjekte 78, 59, 60)
    // und gibt zwei (7, 30) - die Pistolen stehen nur nicht dabei. Genau da
    // gehoeren sie hin. Munition gleich mit, sonst bleibt ein Eintrag ohne
    // Waffe im Inventar stehen.
    for (int obj : { INV_PISTOLEN, INV_PISTOLEN_MUNI })
    {
        bool ch = false;
        std::string tErr;
        if (!addTakeObject("rich2", obj, ch, tErr))
            e.pistolenFehler = tErr;
        else if (ch)
            ++e.pistolenWeg;
    }

    // 4. joby3 nimmt Lara NICHT mehr die Waffen ab.
    //
    // Vanilla entwaffnet das Level Lara (Uzi, Pistolen, Revolver,
    // Lasersight): sie wird gefangen an Bord gebracht, und der einzige
    // Gegner bis zur Waffenkammer ist der Koch - der war unsterblich, weil
    // ChefControl seine Lebenspunkte nie abfragte. Beides passt nicht mehr:
    // der Randomizer stellt ueberall Gegner hin, und der Koch hat jetzt
    // seine Todesbehandlung (chef.cpp). Also weg mit den vier Wegnahmen.
    static const int kJoby3Waffen[] = { 0 /*Uzi*/, INV_PISTOLEN, 3 /*Revolver*/, 20 /*Lasersight*/ };

    for (int obj : kJoby3Waffen)
    {
        bool ch = false;
        std::string wErr;
        if (!removeInvBefehl(0xDA, "joby3", obj, ch, wErr))
            e.waffenFehler = wErr;
        else if (ch)
            ++e.joby3Waffen;
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

bool ScriptDat::addTakeObject(const std::string& levelName, int invObjekt, bool& changed, std::string& error)
{
    return addInvBefehl(0xDA, levelName, invObjekt, changed, error);      // CMD_TAKEOBJ
}

bool ScriptDat::addGiveObject(const std::string& levelName, int invObjekt, bool& changed, std::string& error)
{
    return addInvBefehl(0xD9, levelName, invObjekt, changed, error);      // CMD_GIVEOBJ
}

bool ScriptDat::nimmtObjekt(size_t idx, int invObjekt) const
{
    for (size_t q = m_blockAn[idx]; q < m_blockBis[idx]; )
    {
        const int len = nutzlast(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == 0xDA && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObjekt))
            return true;
        if (m_bytes[q] == 0x81) break;                      // CMD_LEVEL: Blockkopf zu Ende
        q += 1 + len;
    }
    return false;
}

bool ScriptDat::gibtObjekt(size_t idx, int invObjekt) const
{
    for (size_t q = m_blockAn[idx]; q < m_blockBis[idx]; )
    {
        const int len = nutzlast(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == 0xD9 && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObjekt))
            return true;
        if (m_bytes[q] == 0x81) break;
        q += 1 + len;
    }
    return false;
}

bool ScriptDat::inventarAufraeumen(std::vector<std::string>& notizen, std::string& error)
{
    // Siehe Levelketten.h: die Schluesselfamilie ist generisch benannt, ein
    // mitgeschleppter "Garden Key" WIRD im naechsten Level zu etwas anderem.
    // Also: beim Start jedes Levels alles wegnehmen, was ein FRUEHERES Level
    // anbieten konnte und das weder dieses noch ein spaeteres Level per
    // Kette importiert. Die Crossbow-Familie ("Grappling Gun") ueberall
    // ausser in rich3.
    const auto kurz = [](const std::string& datei) -> std::string
    {
        const size_t p = datei.find_last_of('\\');
        std::string k = (p == std::string::npos) ? datei : datei.substr(p + 1);
        for (char& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return k;
    };
    const auto gleich = [](const std::string& a, const char* b)
    {
        std::string bb = b;
        for (char& c : bb) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return a == bb;
    };

    // Level in Slotreihenfolge
    std::vector<std::pair<int, size_t>> folge;      // (slot, idx)
    for (size_t k = 0; k < m_levels.size(); ++k)
        folge.emplace_back(m_ofsIndex[k], k);
    std::sort(folge.begin(), folge.end());

    // Nur Kampagnen-Level aufraeumen (Titel/GIBBY/DEL/TOM bleiben).
    const auto istKampagne = [&](const std::string& k)
    {
        return k.rfind("ANDREA", 0) == 0 || k.rfind("JOBY", 0) == 0 ||
               k.rfind("ANDY", 0) == 0 || k.rfind("RICH", 0) == 0;
    };

    for (const auto& f : folge)
    {
        const size_t idx  = f.second;
        const std::string name = kurz(m_levels[idx].fileName);
        if (!istKampagne(name)) continue;

        // Kompletter Reset der Schluesselfamilie bei JEDEM Levelstart
        // (Entscheidung des Nutzers): Puzzles 1-8 samt Haelften, Keys 1-8
        // samt Haelften, Pickups 1-3 samt Haelften, Brechstange - und die
        // Crossbow-Familie ueberall ausser rich3. Rosen (PICKUP_ITEM4)
        // bleiben. Was der eigene Block gibt (Vanilla-Iris in rich2, die
        // Ketten-Gaben), wird nicht angefasst - ein TAKE hinter dem GIVE
        // wuerde die Gabe aufheben (setup.cpp wendet erst Gaben, dann
        // Wegnahmen an).
        std::vector<int> nehmen, wiedergeben;
        const auto nimm = [&](int inv)
        {
            // Gibt der Block das Item selbst (Vanilla-Iris in rich2, Andrea1s
            // Pickup 1), wird es trotzdem weggenommen - und danach neu
            // gegeben. Die Engine wendet Gaben hinter Wegnahmen in einer
            // dritten Phase an (gameflow.cpp/setup.cpp), darum stimmt die
            // Menge am Ende auf genau eins. Ohne das ueberlebte eine
            // mitgeschleppte Kopie, und ein gezaehltes Puzzle-Item stand
            // doppelt im Inventar ("2 x Katapult", vom Nutzer gesehen).
            if (gibtObjekt(idx, inv) && !gibtObjektNachWegnahme(idx, inv)) wiedergeben.push_back(inv);
            if (std::find(nehmen.begin(), nehmen.end(), inv) == nehmen.end()) nehmen.push_back(inv);
        };
        for (int n = 1; n <= 8; ++n)
        {
            nimm(INV_PUZZLE(n)); nimm(INV_PUZZLE_COMBO(n, 1)); nimm(INV_PUZZLE_COMBO(n, 2));
            nimm(INV_KEY(n));    nimm(INV_KEY_COMBO(n, 1));    nimm(INV_KEY_COMBO(n, 2));
        }
        for (int n = 1; n <= 3; ++n)
        {
            nimm(INV_PICKUP(n)); nimm(INV_PICKUP_COMBO(n, 1)); nimm(INV_PICKUP_COMBO(n, 2));
        }
        nimm(INV_CROWBAR);
        if (!gleich(name, "RICH3"))
            for (const int* c = kCrossbowFamilie; *c; ++c) nimm(*c);

        for (int inv : nehmen)
        {
            bool ch = false;
            if (!addTakeObject(name, inv, ch, error))     // Kurzname - findeLevel vergleicht ohne Pfad
            { error = name + ": " + error; return false; }
        }
        if (!nehmen.empty())
        {
            std::string z = name + ": raeumt " + std::to_string(nehmen.size()) + " Inventarplaetze auf (";
            for (size_t n = 0; n < nehmen.size() && n < 6; ++n) z += (n ? "," : "") + std::to_string(nehmen[n]);
            if (nehmen.size() > 6) z += ",...";
            notizen.push_back(z + ")");
        }

        // ... und die Gaben des Blocks danach neu setzen (force: der alte
        // Befehl steht weiter vorne und wird von der Wegnahme aufgehoben).
        for (int inv : wiedergeben)
        {
            bool ch = false;
            if (!addInvBefehl(0xD9, name, inv, ch, error, true))
            { error = name + ": " + error; return false; }
        }
    }
    return true;
}

// Steht im Block ein GIVEOBJ fuer dieses Objekt HINTER einem TAKEOBJ? Genau
// das legt der Aufraeumer an - die Abfrage macht ihn wiederholbar.
bool ScriptDat::gibtObjektNachWegnahme(size_t idx, int invObjekt) const
{
    bool wegnahmeGesehen = false;

    for (size_t q = m_blockAn[idx]; q < m_blockBis[idx]; )
    {
        const int len = nutzlast(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == 0xDA) wegnahmeGesehen = true;
        if (wegnahmeGesehen && m_bytes[q] == 0xD9 && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObjekt))
            return true;
        if (m_bytes[q] == 0x81) break;
        q += 1 + len;
    }
    return false;
}

bool ScriptDat::removeInvBefehl(uint8_t op, const std::string& levelName, int invObjekt,
                                bool& changed, std::string& error)
{
    changed = false;

    size_t idx = 0;
    if (!findeLevel(levelName, idx)) { error = "Level nicht im Skript: " + levelName; return false; }

    size_t stelle = std::string::npos;
    for (size_t q = m_blockAn[idx]; q < m_blockBis[idx]; )
    {
        const int len = nutzlast(m_bytes[q]);
        if (len < 0) break;
        if (m_bytes[q] == op && q + 1 < m_bytes.size() &&
            m_bytes[q + 1] == static_cast<uint8_t>(invObjekt))
        { stelle = q; break; }
        if (m_bytes[q] == 0x81) break;
        q += 1 + len;
    }
    if (stelle == std::string::npos) return true;          // nicht da: nichts zu tun (wiederholbar)

    m_bytes.erase(m_bytes.begin() + static_cast<ptrdiff_t>(stelle),
                  m_bytes.begin() + static_cast<ptrdiff_t>(stelle + TAKEOBJ_BYTES));

    // Genau die Gegenrichtung zu addInvBefehl: jeder Block HINTER der
    // Loeschstelle rueckt um die Befehlslaenge vor.
    for (int i = 0; i < m_nLevels; ++i)
    {
        uint16_t o;
        std::memcpy(&o, m_bytes.data() + m_scriptOfsAt + 2ull * i, 2);
        if (m_scriptWad + o > stelle)
        {
            o = static_cast<uint16_t>(o - TAKEOBJ_BYTES);
            std::memcpy(m_bytes.data() + m_scriptOfsAt + 2ull * i, &o, 2);
        }
    }

    GAMEFLOW gf;
    std::memcpy(&gf, m_bytes.data(), sizeof(gf));
    gf.ScriptLen = static_cast<uint16_t>(gf.ScriptLen - TAKEOBJ_BYTES);
    std::memcpy(m_bytes.data() + offsetof(GAMEFLOW, ScriptLen), &gf.ScriptLen, 2);

    if (!zerlege(error)) return false;
    changed = true;
    return true;
}

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
// SCRIPT.DAT: die Levelflags lesen und einzelne Bits loeschen.
//
// Gebraucht wird genau ein Bit: GF_YOUNGLARA. Es steht in den drei
// Ireland-Leveln (andy1, Andy2, andy3) und schaltet dort das ganze
// Waffensystem ab - Lara ist als Kind unterwegs und kann nichts tragen. An
// acht Stellen im Motor haengt das Verhalten daran, unter anderem setup.cpp:111,
// larafire.cpp:221, drawlara.cpp:227/429 und laraskin.cpp:424.
//
// Fuer den Randomizer ist das untragbar: der Nutzer hat festgelegt, dass die
// Andy-Level immer Waffen haben muessen ("Wenn wir cross Level Gegner haben
// waere ohne Waffen gegen ein Two Gun schon echt bloede"). Also wird das Bit
// beim Randomisieren geloescht.
//
// AUFBAU (aus LoadGameflow, game/gameflow.cpp:956, und struct GAMEFLOW,
// global/types.h:573):
//   GAMEFLOW   16 Byte
//   char       40 Byte  "[PCExtensions]"
//   ushort     nFileNames Dateinamen-Offsets
//   char       FileNameLen Dateinamen-Wad
//   ushort     nLevels    Skript-Offsets
//   uchar      ScriptLen  Skript-Wad
//
// Im Skript-Wad steht je Level eine Befehlsfolge. CMD_LEVEL (0x81) traegt fuenf
// Nutzbytes: [0] Titel-String, [1..2] gfLevelFlags (16 Bit LE), [3] Dateiname,
// [4] Argument. Siehe gameflow.cpp:155 - "gfLevelFlags = gf[1] | (gf[2] << 8)".
//
// Level werden ueber ihren DATEINAMEN gesucht, nicht ueber die Position in der
// Liste. Die Reihenfolge im Skript ist Spielreihenfolge und kann sich aendern;
// der Dateiname ist das, was auch der Randomizer benutzt.
//----------------------------------------------------------------------------
class ScriptDat
{
public:
    static constexpr uint16_t GF_YOUNGLARA = 0x0001;

    bool load(const std::wstring& path, std::string& error);
    bool save(const std::wstring& path, std::string& error) const;

    // Loescht Bits in den Flags des Levels mit diesem Dateinamen (ohne
    // Endung, Gross-/Kleinschreibung egal). Liefert false, wenn es das Level
    // im Skript nicht gibt. Schon geloeschte Bits noch einmal zu loeschen ist
    // ausdruecklich in Ordnung - der Aufruf ist wiederholbar, und genau das
    // braucht er: der Randomizer laeuft mehrfach ueber dieselbe Installation.
    bool clearLevelFlags(const std::string& fileName, uint16_t mask, bool& changed);

    // --- Objektbefehle: Name und Inventarpose eines Key-/Puzzle-/Pickup-Items
    //
    // Diese Angaben stehen NICHT im Level, sondern im Skriptblock des Levels.
    // Je Objekt ein Befehl mit 14 Nutzbytes: Namensindex, yoff, Groesse, drei
    // Drehungen, Flags. gameflow.cpp:371 schreibt sie nach
    // inventry_objects_list - und diese Tabelle wird zwischen Leveln NIE
    // zurueckgesetzt. Fehlt einem Level der Befehl, traegt das Item Namen und
    // Pose des zuletzt gesetzten Levels; bei frischem Start den Werkswert
    // TXT_Load (newinv2.cpp:325ff, alle acht Key-Items tragen denselben
    // Platzhalter, vanilla immer vom Skript ueberschrieben).
    //
    // Genau das trifft uns, sobald ein Modell in ein fremdes Level wandert:
    // joby3 bekommt Key Item 8, hat aber kein CMD_KEY8. Vanilla faellt das
    // nie auf, weil joby2 direkt davor laeuft und den Eintrag setzt - nach
    // einem Speichern und Laden in joby3 waere der Schluessel "Load".
    static constexpr uint8_t CMD_OBJ_ERSTER  = 0x92;   // CMD_KEY1  = 146
    static constexpr uint8_t CMD_OBJ_LETZTER = 0xD8;   // CMD_PICKUPCOMBO4_2 = 216
    static constexpr int     OBJEKT_NUTZBYTES    = 14;
    static constexpr int     OBJEKT_BEFEHL_BYTES = 1 + OBJEKT_NUTZBYTES;

    static constexpr uint8_t cmdKey(int n)    { return static_cast<uint8_t>(0x92 + (n - 1)); }  // KEY1..12
    static constexpr uint8_t cmdPuzzle(int n) { return static_cast<uint8_t>(0x9E + (n - 1)); }  // PUZZLE1..12
    static constexpr uint8_t cmdPickup(int n) { return static_cast<uint8_t>(0xAA + (n - 1)); }  // PICKUP1..4

    // Kopiert einen Objektbefehl samt Pose von einem Level in ein anderes.
    // Hat das Ziel ihn schon, passiert nichts (changed bleibt false) - der
    // Aufruf ist wiederholbar, genau wie clearLevelFlags.
    //
    // Der Einschub verlaengert die Datei: das Skript-Wad waechst um 15 Byte,
    // alle dahinterliegenden Leveloffsets und ScriptLen werden nachgezogen.
    // Unkritisch, weil LoadGameflow (gameflow.cpp:980) rein sequentiell durch
    // die Datei laeuft und die Texte in eigenen Sprachdateien liegen - es gibt
    // keine absoluten Offsets, die zeigen koennten.
    bool copyObjectCommand(const std::string& vonLevel, const std::string& nachLevel,
                           uint8_t cmd, bool& changed, std::string& error);

    // Was steht heute drin? Fuer Anzeige und Selbsttest.
    struct Eintrag
    {
        std::string fileName;
        uint16_t    flags = 0;
    };
    const std::vector<Eintrag>& levels() const { return m_levels; }

private:
    std::vector<uint8_t> m_bytes;
    std::vector<Eintrag> m_levels;
    std::vector<size_t>  m_flagsAt;    // wo die Flags jedes Eintrags stehen
    std::vector<size_t>  m_blockAn;    // Blockanfang je Level
    std::vector<size_t>  m_blockBis;   // Blockende (ausschliesslich)
    std::vector<int>     m_ofsIndex;   // Platz in der Offsettabelle
    size_t               m_scriptOfsAt = 0;
    size_t               m_scriptWad   = 0;
    int                  m_nLevels     = 0;

    bool   findeLevel(const std::string& name, size_t& idx) const;
    size_t findeBefehl(size_t idx, uint8_t cmd) const;

    bool zerlege(std::string& error);
};

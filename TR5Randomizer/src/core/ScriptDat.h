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
// SCRIPT.DAT: die Levelflags read und einzelne Bits loeschen.
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
    static constexpr uint8_t CMD_OBJ_FIRST  = 0x92;   // CMD_KEY1  = 146
    static constexpr uint8_t CMD_OBJ_LAST = 0xD8;   // CMD_PICKUPCOMBO4_2 = 216
    static constexpr int     OBJ_PAYLOAD_BYTES    = 14;
    static constexpr int     OBJ_COMMAND_BYTES = 1 + OBJ_PAYLOAD_BYTES;

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

    // Inventarnummern (enum invobj_types, newinv2.h:121), soweit gebraucht.
    static constexpr int INV_PISTOLS      = 1;
    static constexpr int INV_PISTOLS_AMMO = 19;
    static constexpr int TAKEOBJ_BYTES     = 3;   // Opcode + 2 Nutzbytes

    // Setzt ein CMD_TAKEOBJ in den Block eines Levels: Lara hat das
    // Inventarobjekt beim Betreten nicht mehr (setup.cpp:160, NailInvItem).
    // Wiederholbar - steht es schon da, passiert nichts.
    bool addTakeObject(const std::string& levelName, int invObject,
                       bool& changed, std::string& error);

    // Das Gegenstueck: CMD_GIVEOBJ - Lara hat das Inventarobjekt beim
    // Betreten (setup.cpp:122, DEL_picked_up_object). Wiederholbar.
    bool addGiveObject(const std::string& levelName, int invObject,
                       bool& changed, std::string& error);

    // Nimmt einen GIVEOBJ/TAKEOBJ-Befehl wieder heraus (die Gegenrichtung zu
    // addInvCommand, inklusive Offsets und ScriptLen). Wiederholbar: ist er
    // nicht da, passiert nichts.
    bool removeInvCommand(uint8_t op, const std::string& levelName, int invObject,
                         bool& changed, std::string& error);

    // Stufe 2: Reihenfolge INNERHALB der vier Abschnitte wuerfeln (RICHCUT2
    // bleibt an RICH2 geklebt). Wie shuffleSections nur die Offsettabelle;
    // nicht wiederholbar. orderText: die neue Abfolge als Text.
    // frei = Stufe 3: alle Kampagnen-Level ueber alle Abschnitte hinweg
    // permutieren (RICH2+RICHCUT2 bleiben eine Einheit). Die Videos haengen
    // in der Engine an der Level-Identitaet, das Finale am letzten Slot -
    // beides traegt auch die freie Folge.
    bool shuffleLevels(uint64_t seed, bool frei, std::string& orderText, std::string& error);

    // Slot (Platz in der Offsettabelle) eines Levels, -1 wenn unbekannt.
    int  slotOfLevel(const std::string& levelName) const;

    // LevelChains (LevelChains.h) gegen die JETZIGE Reihenfolge aufloesen:
    // liegt der Spender hinter dem Nehmer, bekommt der Nehmer das Item per
    // GIVEOBJ und den Namensbefehl des Spenders. In Vanilla-Reihenfolge
    // passiert nichts. Jede Massnahme landet als Klartext in notes.
    bool resolveChains(std::vector<std::string>& notes, std::string& error);

    // Inventar-Garbage-Collector (LevelChains.h, Kopfkommentar): beim Start
    // jedes Kampagnen-Levels wird per TAKEOBJ alles aus der Schluesselfamilie
    // weggenommen, was ein frueheres Level anbieten konnte und das weder
    // dieses noch ein spaeteres Level importiert; die Crossbow-Familie
    // ("Grappling Gun") ueberall ausser rich3. Laeuft NACH resolveChains,
    // damit Ketten-Gaben nicht gleich wieder weggenommen werden.
    bool cleanupInventory(std::vector<std::string>& notes, std::string& error);

    // --- Alle Changes des Randomizers an EINER Stelle -------------------
    //
    // Oberflaeche und CLI treiben dieselben Schritte an. Standen sie doppelt
    // im Code, waere der naechste Eingriff garantiert nur an einer der beiden
    // Stellen gelandet.
    //
    // Beide Changes sind wiederholbar: ein schon geloeschtes Bit noch einmal
    // zu loeschen aendert nichts, und ein schon vorhandener Objektbefehl wird
    // nicht ein zweites Mal eingefuegt. Derselbe Lauf darf also mehrfach ueber
    // dieselbe Datei gehen, ohne sie aufzublaehen.
    struct Changes
    {
        int         flagsChanged = 0;   // Ireland-Level, denen das Kind-Bit genommen wurde
        int         levelsMissing   = 0;   // Ireland-Level, die das Skript gar nicht kennt
        bool        keyCopied     = false;
        int         pistolsRemoved    = 0;    // rich2: Pistolen + Munition entzogen
        std::string keyError;            // leer = hat geklappt
        int         joby3Weapons    = 0;   // entfernte Waffen-Wegnahmen in joby3
        std::string weaponError;
        std::string pistolError;
    };

    // Bei levelsMissing > 0 ist das Skript keines von Chronicles - dann bleibt
    // alles Weitere aus, und der Aufrufer soll NICHT speichern.
    void randomizerChanges(Changes& e);

    // Hintergrundmusik wuerfeln: das Audio-Byte im CMD_LEVEL-Eintrag
    // (CurrentAtmosphere beim Levelstart) wird unter den spielbaren Leveln
    // permutiert. Aussen vor bleiben GF_NOLEVEL-Eintraege und die
    // Cutscene-Level (RICHCUT) - deren Musik gehoert zur Szene. NICHT
    // wiederholungsfest: jeder Lauf wuerfelt neu, der Aufrufer muss von der
    // unveraenderten Datei ausgehen (die Oberflaeche liest ohnehin aus der
    // Sicherung).
    bool shuffleMusic(uint64_t seed, int& getauscht, std::string& error);

    // Levelabschnitte wuerfeln (Stufe 1 des Plans in Level Infos\
    // Levelreihenfolge-Plan.md): die vier Story-Abschnitte Rom (ANDREA*),
    // Russland (JOBY*), Irland (ANDY*) und VCI (RICH*, inkl. RICHCUT2) werden
    // als unteilbare Bloecke permutiert. Geaendert wird NUR die Offsettabelle
    // der Levelbloecke - die Bloecke selbst (FMVs, Cutscenes, Flags, Musik,
    // Inventarbefehle) wandern unangetastet mit. Innerhalb eines Abschnitts
    // bleibt die Reihenfolge, damit alle Schluesselketten gueltig bleiben.
    // BRAUCHT die Engine mit gfLevelId (Identitaet aus dem Dateinamen) -
    // eine aeltere EXE hielte Slot 1 weiter fuer Rom. Nicht wiederholbar.
    // orderText bekommt die neue Abfolge als Text fuer die Anzeige.
    bool shuffleSections(uint64_t seed, std::string& orderText, std::string& error);

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
    std::vector<size_t>  m_blockStart;    // Blockanfang je Level
    std::vector<size_t>  m_blockEnd;   // Blockende (ausschliesslich)
    std::vector<int>     m_ofsIndex;   // Platz in der Offsettabelle
    size_t               m_scriptOfsAt = 0;
    size_t               m_scriptWad   = 0;
    int                  m_nLevels     = 0;

    bool   findLevel(const std::string& name, size_t& idx) const;
    // GIVEOBJ/TAKEOBJ gemeinsam. force: auch dann einfuegen, wenn derselbe
    // Befehl schon im Block steht - gebraucht vom Inventar-Reset, der eine
    // Gabe hinter seinen Wegnahmen wiederholen muss.
    bool   addInvCommand(uint8_t op, const std::string& levelName, int invObject,
                        bool& changed, std::string& error, bool force = false);
    bool   takesObject(size_t idx, int invObject) const;     // traegt der Block ein TAKEOBJ dafuer?
    bool   givesObject(size_t idx, int invObject) const;      // ... bzw. ein GIVEOBJ?
    bool   givesObjectAfterTake(size_t idx, int invObject) const;
    size_t findCommand(size_t idx, uint8_t cmd) const;

    bool parse(std::string& error);
};

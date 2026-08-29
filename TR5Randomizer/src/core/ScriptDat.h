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
// SCRIPT.DAT: read the level flags and clear individual bits.
//
// Exactly one bit is needed: GF_YOUNGLARA. It is set in the three Ireland
// levels (andy1, Andy2, andy3) and disables the entire weapon system there -
// Lara travels as a child and can carry nothing. Eight places in the engine
// hang off this behavior, among them setup.cpp:111, larafire.cpp:221,
// drawlara.cpp:227/429 and laraskin.cpp:424.
//
// For the randomizer that is unacceptable: the user decreed that the andy
// levels must always have weapons ("with cross-level enemies, facing a
// TwoGun without weapons would be really nasty"). So the bit is cleared
// when randomizing.
//
// LAYOUT (from LoadGameflow, game/gameflow.cpp:956, and struct GAMEFLOW,
// global/types.h:573):
//   GAMEFLOW   16 bytes
//   char       40 bytes  "[PCExtensions]"
//   ushort     nFileNames file name offsets
//   char       FileNameLen file name wad
//   ushort     nLevels    script offsets
//   uchar      ScriptLen  script wad
//
// The script wad holds one command sequence per level. CMD_LEVEL (0x81)
// carries five payload bytes: [0] title string, [1..2] gfLevelFlags (16 bit
// LE), [3] file name, [4] argument. See gameflow.cpp:155 -
// "gfLevelFlags = gf[1] | (gf[2] << 8)".
//
// Levels are looked up by their FILE NAME, not by their position in the
// list. The order in the script is play order and can change; the file name
// is what the randomizer uses as well.
//----------------------------------------------------------------------------
class ScriptDat
{
public:
    static constexpr uint16_t GF_YOUNGLARA = 0x0001;

    bool load(const std::wstring& path, std::string& error);
    bool save(const std::wstring& path, std::string& error) const;

    // Clears bits in the flags of the level with this file name (without
    // extension, case-insensitive). Returns false if the script does not
    // know the level. Clearing an already cleared bit again is explicitly
    // fine - the call is repeatable, and it needs to be: the randomizer
    // runs over the same installation more than once.
    bool clearLevelFlags(const std::string& fileName, uint16_t mask, bool& changed);

    // --- Object commands: name and inventory pose of a key/puzzle/pickup item
    //
    // This information is NOT in the level but in the level's script block.
    // One command with 14 payload bytes per object: name index, yoff, size,
    // three rotations, flags. gameflow.cpp:371 writes them into
    // inventry_objects_list - and that table is NEVER reset between levels.
    // If a level lacks the command, the item carries the name and pose of
    // whichever level set them last; on a fresh start the factory value
    // TXT_Load (newinv2.cpp:325ff - all eight key items carry the same
    // placeholder, in vanilla always overwritten by the script).
    //
    // That is exactly what hits us once a model travels into a foreign
    // level: joby3 receives Key Item 8 but has no CMD_KEY8. Vanilla never
    // notices, because joby2 runs right before it and sets the entry - after
    // a save and load in joby3 the key would read "Load".
    static constexpr uint8_t CMD_OBJ_FIRST  = 0x92;   // CMD_KEY1  = 146
    static constexpr uint8_t CMD_OBJ_LAST = 0xD8;   // CMD_PICKUPCOMBO4_2 = 216
    static constexpr int     OBJ_PAYLOAD_BYTES    = 14;
    static constexpr int     OBJ_COMMAND_BYTES = 1 + OBJ_PAYLOAD_BYTES;

    static constexpr uint8_t cmdKey(int n)    { return static_cast<uint8_t>(0x92 + (n - 1)); }  // KEY1..12
    static constexpr uint8_t cmdPuzzle(int n) { return static_cast<uint8_t>(0x9E + (n - 1)); }  // PUZZLE1..12
    static constexpr uint8_t cmdPickup(int n) { return static_cast<uint8_t>(0xAA + (n - 1)); }  // PICKUP1..4

    // Copies an object command including its pose from one level to another.
    // If the target already has it, nothing happens (changed stays false) -
    // the call is repeatable, just like clearLevelFlags.
    //
    // The insertion lengthens the file: the script wad grows by 15 bytes,
    // and all level offsets behind it plus ScriptLen are adjusted. Harmless,
    // because LoadGameflow (gameflow.cpp:980) walks the file strictly
    // sequentially and the texts live in separate language files - there are
    // no absolute offsets that could dangle.
    bool copyObjectCommand(const std::string& fromLevel, const std::string& toLevel,
                           uint8_t cmd, bool& changed, std::string& error);

    // Inventory numbers (enum invobj_types, newinv2.h:121), as needed.
    static constexpr int INV_PISTOLS      = 1;
    static constexpr int INV_PISTOLS_AMMO = 19;
    static constexpr int TAKEOBJ_BYTES     = 3;   // opcode + 2 payload bytes

    // Inserts a CMD_TAKEOBJ into a level's block: Lara no longer has the
    // inventory object on entering (setup.cpp:160, NailInvItem).
    // Repeatable - if it is already there, nothing happens.
    bool addTakeObject(const std::string& levelName, int invObject,
                       bool& changed, std::string& error);

    // The counterpart: CMD_GIVEOBJ - Lara has the inventory object on
    // entering (setup.cpp:122, DEL_picked_up_object). Repeatable.
    bool addGiveObject(const std::string& levelName, int invObject,
                       bool& changed, std::string& error);

    // Takes a GIVEOBJ/TAKEOBJ command back out (the reverse of
    // addInvCommand, offsets and ScriptLen included). Repeatable: if it is
    // not there, nothing happens.
    bool removeInvCommand(uint8_t op, const std::string& levelName, int invObject,
                         bool& changed, std::string& error);

    // Stage 2: shuffle the order WITHIN the four sections (RICHCUT2 stays
    // glued to RICH2). Like shuffleSections, only the offset table; not
    // repeatable. orderText: the new sequence as text.
    // free = stage 3: permute all campaign levels across all sections
    // (RICH2+RICHCUT2 remain one unit). The videos hang off the level
    // identity in the engine, the finale off the last slot - the free order
    // carries both as well.
    bool shuffleLevels(uint64_t seed, bool freeMode, std::string& orderText, std::string& error);

    // Slot (position in the offset table) of a level, -1 if unknown.
    int  slotOfLevel(const std::string& levelName) const;

    // Resolve the level chains (LevelChains.h) against the CURRENT order:
    // if the donor comes after the taker, the taker receives the item via
    // GIVEOBJ plus the donor's name command. In vanilla order nothing
    // happens. Every action lands as plain text in notes.
    bool resolveChains(std::vector<std::string>& notes, std::string& error);

    // Inventory garbage collector (LevelChains.h, header comment): at the
    // start of every campaign level, TAKEOBJ removes everything from the key
    // family that an earlier level could have offered and that neither this
    // nor a later level imports; the crossbow family ("Grappling Gun")
    // everywhere except rich3. Runs AFTER resolveChains, so chain grants are
    // not immediately taken away again.
    bool cleanupInventory(std::vector<std::string>& notes, std::string& error);

    // --- All the randomizer's changes in ONE place ----------------------
    //
    // UI and CLI drive the same steps. If they existed twice in the code,
    // the next change would be guaranteed to land in only one of the two
    // places.
    //
    // Both changes are repeatable: clearing an already cleared bit changes
    // nothing, and an object command that is already present is not
    // inserted a second time. The same run may therefore pass over the same
    // file several times without bloating it.
    struct Changes
    {
        int         flagsChanged = 0;   // Ireland levels stripped of the child bit
        int         levelsMissing   = 0;   // Ireland levels the script does not know
        bool        keyCopied     = false;
        int         pistolsRemoved    = 0;    // rich2: pistols + ammo removed
        std::string keyError;            // empty = it worked
        int         joby3Weapons    = 0;   // removed weapon take-aways in joby3
        int         andrea1Uzi      = 0;   // Andrea1: Core's own Uzi take-away removed
        std::string weaponError;
        std::string pistolError;
    };

    // With levelsMissing > 0 the script is not one of Chronicles - then
    // everything else is skipped, and the caller must NOT save.
    void randomizerChanges(Changes& e);

    // Shuffle the level music: the audio byte in the CMD_LEVEL entry
    // (CurrentAtmosphere at level start) is permuted among the playable
    // levels. GF_NOLEVEL entries and the cutscene levels (RICHCUT) stay out
    // - their music belongs to the scene. NOT repeatable: every run rolls
    // anew, the caller must start from the unmodified file (the UI reads
    // from the backup anyway).
    bool shuffleMusic(uint64_t seed, int& swapped, std::string& error);

    // Shuffle the level sections (stage 1 of the plan in Level Infos\
    // Level-Order-Plan.md): the four story sections Rome (ANDREA*),
    // Russia (JOBY*), Ireland (ANDY*) and VCI (RICH*, incl. RICHCUT2) are
    // permuted as indivisible blocks. ONLY the offset table of the level
    // blocks changes - the blocks themselves (FMVs, cutscenes, flags,
    // music, inventory commands) travel along untouched. Within a section
    // the order stays, so all key chains remain valid. REQUIRES the engine
    // with gfLevelId (identity from the file name) - an older EXE would
    // still take slot 1 for Rome. Not repeatable. orderText receives the
    // new sequence as text for display.
    bool shuffleSections(uint64_t seed, std::string& orderText, std::string& error);

    // What is in there today? For display and self-test.
    struct Entry
    {
        std::string fileName;
        uint16_t    flags = 0;
    };
    const std::vector<Entry>& levels() const { return m_levels; }

private:
    std::vector<uint8_t> m_bytes;
    std::vector<Entry> m_levels;
    std::vector<size_t>  m_flagsAt;    // where each entry's flags live
    std::vector<size_t>  m_blockStart;    // block start per level
    std::vector<size_t>  m_blockEnd;   // block end (exclusive)
    std::vector<int>     m_ofsIndex;   // position in the offset table
    size_t               m_scriptOfsAt = 0;
    size_t               m_scriptWad   = 0;
    int                  m_nLevels     = 0;

    bool   findLevel(const std::string& name, size_t& idx) const;
    // GIVEOBJ/TAKEOBJ combined. force: insert even if the same command is
    // already in the block - needed by the inventory reset, which must
    // repeat a grant after its take-aways.
    bool   addInvCommand(uint8_t op, const std::string& levelName, int invObject,
                        bool& changed, std::string& error, bool force = false);
    bool   takesObject(size_t idx, int invObject) const;     // does the block carry a TAKEOBJ for it?
    bool   givesObject(size_t idx, int invObject) const;      // ... or a GIVEOBJ?
    bool   givesObjectAfterTake(size_t idx, int invObject) const;
    size_t findCommand(size_t idx, uint8_t cmd) const;

    bool parse(std::string& error);
};

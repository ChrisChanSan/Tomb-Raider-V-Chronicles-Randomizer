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
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Floor.h"

//----------------------------------------------------------------------------
// Our own reader and writer for TR5 level files (.trc).
//
// Why our own code and not the existing one from TombItemEdit2026: its TR5
// parser (TRFile.cpp) comes from TRViewer by Popov Inc. and carries no
// license statement. Our own additions there build on top of it and cannot
// be extracted. For a tool that is meant to be redistributed that is not
// acceptable - hence built from scratch here.
//
// Format source: LoadLevel() and LoadTextures() in specific/file.cpp of the
// engine, which ships with this package in source anyway and is under the
// same license (GPLv3). The engine is the authoritative description - it
// has read these files correctly since 2000.
//
// File layout, derived from LoadLevel/LoadTextures:
//
//   int32  version
//   int16  RTPages, OTPages, BTPages        number of texture pages
//   -- texture block 1 (32 bit) --
//   int32  size, compressedSize
//   byte[] payload                          compressedSize resp. size bytes
//   -- texture block 2 (16 bit) --
//   int32  size, compressedSize
//   byte[] payload
//   int16  data[16]                         32 bytes, incl. LaraDrawType/Weather
//   -- main block --
//   int32  size, compressedSize
//   byte[] size bytes                       UNCOMPRESSED, verbatim
//
// The main block is the interesting part: it holds, in order, rooms,
// objects, sprites, cameras, sounds, boxes, animated textures and texture
// infos, then an int32 with the item count and after that the items at 24
// bytes each. The fact that it sits uncompressed and verbatim in the file
// is the reason items can be changed in place at all without rewriting the
// whole file - compressedSize is read for this block but never used.
//----------------------------------------------------------------------------

struct Tr5Item
{
    int16_t objectNumber = 0;
    int16_t roomNumber = 0;
    int32_t x = 0, y = 0, z = 0;
    int16_t yRot = 0;
    int16_t shade = 0;
    int16_t triggerFlags = 0;   // OCB in the lower 6 bits, see Randomizer.h
    int16_t flags = 0;
};

//----------------------------------------------------------------------------
// An AI object (AIOBJECT, global/types.h:2295, 24 bytes in the block right
// behind the items). Markers like AI_AMBUSH (slot 379): Larson/Pierre flee
// from 40 HP to an AIAmbush with matching OCB in their zone and despawn
// there (GetAITarget/FindAITargetObject, box.cpp). boxNumber may be 0 - the
// engine computes it itself at runtime (box.cpp:1640).
//----------------------------------------------------------------------------
struct Tr5AiObject
{
    int16_t objectNumber = 0;
    int16_t roomNumber = 0;
    int32_t x = 0, y = 0, z = 0;
    int16_t ocb = 0;            // trigger_flags: must match the enemy's OCB
    int16_t flags = 0;
    int16_t yRot = 0;
    int16_t boxNumber = 0;
};

//----------------------------------------------------------------------------
// A room, as far as we need it. The room blob is no longer skipped in one
// piece but broken down - portals, neighbors and flags are the basis for
// finding a genuinely secluded room.
//----------------------------------------------------------------------------
struct Tr5Room
{
    int   index = 0;
    int   originX = 0, originZ = 0, yBottom = 0;
    int   numXSectors = 0, numZSectors = 0;
    int   altRoom = -1;                 // flipmap partner, -1 = none
    unsigned flags = 0;                 // bit 0x1 = under water
    std::vector<int> portalRooms;       // target rooms of the portal list

    // Absolute file position of the sector field (numXSectors * numZSectors
    // entries of 8 bytes each). 0 if the room did not parse. The first word
    // of each entry is FLOOR_INFO.index, the pointer into the floordata.
    size_t sectorsOffset = 0;

    // Absolute file position of the room header (208 bytes). Needed to
    // write the flags at +78 - the water bit sits there.
    size_t headerOffset = 0;

    bool isAlternate() const { return altRoom >= 0; }
};

//----------------------------------------------------------------------------
// One entry of a floordata chain, with all its words.
//
// Splitting into entries is not convenience but necessity: words[0] carries
// in bit 15 the "last entry of the chain" marker. Whoever copies a chain
// word by word and takes one entry out leaves this bit on the wrong word -
// the engine then reads past the end of the chain. It is therefore
// reassigned when reassembling.
//----------------------------------------------------------------------------
struct Tr5FloorEntry
{
    int function = 0;                   // global/types.h, enum floor_types
    int art = 0;                        // only for function == 4 (trigger)
    std::vector<uint16_t> words;
};

//----------------------------------------------------------------------------
// A trigger command. type is the 4-bit command kind (enum trigobj_types,
// global/types.h:435): 0 = TO_OBJECT, 1 = TO_CAMERA, 3 = TO_FLIPMAP,
// 8 = TO_CD, 13 = TO_CUTSCENE and so on.
//----------------------------------------------------------------------------
struct Tr5TriggerCmd
{
    int type = 0;
    int param = 0;
};

class Tr5Level
{
public:
    // Reads the file completely into memory and determines the location of
    // the main block. On failure the reason is in error.
    bool load(const std::wstring& path, std::string& error);

    // Writes the - possibly modified - file. Deliberately to its own target
    // path: the source file stays untouched until the caller wants to
    // replace it.
    bool save(const std::wstring& path, std::string& error) const;

    int32_t version() const { return m_version; }
    size_t  fileSize() const { return m_bytes.size(); }

    // Absolute position and size of the main block in the file.
    size_t  mainBlockOffset() const { return m_mainOffset; }
    size_t  mainBlockSize() const { return m_mainSize; }

    // --- Items ---------------------------------------------------------
    // Absolute position of the int32 with the item count; right behind it
    // lie the items at 24 bytes each.
    size_t  itemCountOffset() const { return m_itemCountOffset; }
    int32_t itemCount() const { return m_itemCount; }
    int     roomCount() const { return m_roomCount; }

    const std::vector<Tr5Room>& rooms() const { return m_rooms; }

    // Counts in how many rooms the portal list did NOT parse. Must be 0 -
    // see the arithmetic in parseRoom().
    int     roomParseMismatches() const { return m_roomMismatch; }

    // Reads an item. index must be < itemCount().
    bool    getItem(int index, Tr5Item& out) const;

    // Writes an item back to the same place. Changes only the 24 bytes of
    // this record - file size and all other offsets stay untouched, which
    // is why changing is safe and removing is not.
    bool    setItem(int index, const Tr5Item& in);

    // Appends an AI object to the AI block (right behind the items). For
    // the flee marker of Larson/Pierre: ONE AI_AMBUSH per level at Lara's
    // start position - the 40-HP flight leads towards the level entrance
    // and despawns out of sight. NEVER put it on an enemy's spawn sector:
    // GetAIPickups (setup.cpp:2024) turns AI objects within 512 units into
    // the enemy's start behavior order (permanent AMBUSH, it never aims)
    // and consumes them afterwards (room_number = NO_ROOM).
    bool    addAiObject(const Tr5AiObject& in, std::string& error);

    // Appends an item at the end. That is the only way to add items:
    // appending shifts no existing indices, so the triggers' "Object N"
    // references stay valid. There is deliberately NO removal here - it
    // would shift all subsequent indices.
    bool    addItem(const Tr5Item& in, std::string& error);

    // The engine holds items[] as a fixed array of this size (MAX_ITEMS in
    // global/types.h). It loads no more items - rich2 already sits close
    // underneath with 248, which is why the limit is checked here rather
    // than hoped for. 512 since the engine doubling (256 before;
    // global/types.h of the engine is the authoritative place, both must
    // agree).
    static constexpr int MAX_ITEMS = 512;

    // Lifts exactly that limit - solely for the debug levels that put a
    // marker on every valid sector. Those come out with several thousand
    // items and are therefore NOT playable; the engine aborts loading with
    // a message. They are meant for viewing in trview and accordingly land
    // in their own folder, never in data\.
    //
    // Deliberately an explicit switch instead of a loosened limit: whoever
    // sets it must know that the result is no longer a level.
    void    allowMoreThanMaxItems(bool on) { m_allowOverLimit = on; }

    // --- Floordata -----------------------------------------------------
    //
    // Changes follow exactly one pattern: append the new chain to the end
    // of the block and repoint the sector's FLOOR_INFO.index. Never insert
    // or delete in the middle - that would shift EVERY subsequent index,
    // and silently so, because a wrong index simply reads a different
    // chain. The old chain stays behind as dead ballast; no sector points
    // at it afterwards.
    int32_t floorDataCount() const { return m_floorCount; }

    // Reads a sector's FLOOR_INFO.index. -1 if the sector does not exist.
    // 0 means: this sector has no floordata at all.
    int     sectorIndex(int room, int sx, int sz) const;

    // Splits the chain starting at index into its entries. For index == 0
    // the result is empty and the return value still true. false means: the
    // chain runs out of the block, hands off.
    bool    readChain(int index, std::vector<Tr5FloorEntry>& out) const;

    // Music triggers: visits every TO_CD action word (type 8) of all sector
    // chains exactly once (shared chains are deduplicated via the chain
    // start - otherwise a track would go through the mapping TWICE and land
    // somewhere other than rolled). found collects the track numbers;
    // mapping patches them IN PLACE - the word keeps type, end bit and
    // position, only the track number (bits 0..9) changes, which is why NO
    // chain rebuilding is needed here. Returns the number of patched resp.
    // found words.
    long    musicTriggers(const std::map<int, int>* mapping, std::set<int>* found);

    // A sector's slope entry (TILT, SPLIT1/2, NOCOLF), raw, for floorPosAt
    // (Floor.h) - the ported GetHeight math lives there, turning entry +
    // point into the true floor height and walkability. baseY is the
    // sector's base value (floor byte << 8), on a slope the HIGHEST corner.
    // Returns: -1 chain unreadable (hands off), 0 flat (no entry, baseY
    // filled anyway), 1 entry is in si.
    int     floorSlope(int room, int sx, int sz, SlopeInfo& si, int& baseY) const;

    // Installs a moveable fetched from a TR3 level (package from TR3Hol,
    // magic "T35H"). Implemented in Tr5Tr3Import.cpp - deliberately next to
    // the proven TR5->TR5 path and not inside it.
    //
    // texinfoIndex: EVERY face points at this already existing texinfo of
    // the target afterwards. That way no texture page is inserted and no
    // tile number shifts; the enemy is wrongly textured, but the page
    // management stays untouched. Real textures are a separate step via
    // Tr5Transfer.cpp.
    bool    addMoveableFromTr3(const std::vector<uint8_t>& package, int targetSlot,
                               int texinfoIndex, std::string& error);

    // Puts the sounds of a TR3 package ("T35S" from TR3Ton) onto the
    // soundmap slots baseId, baseId+1, ... and returns the mapping original
    // TR3 id -> new slot. With it the SFX numbers in the anim commands are
    // rewritten afterwards.
    //
    // Why high slots instead of the TR3 numbers: the original ids are
    // partly taken in TR5 (89 in EVERY level, 84/87 in some). Slots
    // 440..449 are free everywhere, and the soundmap is a fixed field of
    // 450 entries - a high slot costs not a single byte.
    //
    // volumePercent (optional, per sound in package order): stretches the
    // volume proportionally. The base value comes from an existing
    // SAMPLE_INFO of the target so the order of magnitude is right - TR3
    // stores volume as a whole word, TR5 as one byte, a direct takeover
    // would be a guess. 100 or absent means unchanged.
    bool    addSoundsFromTr3(const std::vector<uint8_t>& package, int baseId,
                             std::vector<std::pair<int,int>>& mapping,
                             std::string& error,
                             const std::vector<int>* volumePercent = nullptr);

    // Inserts the textures of a TR3 model package (version 2): append
    // pages, create texinfos. Returns TR3 texture number -> new texinfo
    // index; with it the caller redirects the faces' texture words.
    //
    // Implemented in Tr5Transfer.cpp - the proven helpers for page
    // management live there. That is the most dangerous spot of the tool:
    // insertion happens at the same place as in the TR5 transfer (rt+ot),
    // then texinfos and sprites are adjusted. Never append at the end on
    // your own - that shifts the boundary between object and bump pages.
    //
    // On success, error carries a short summary (new pages, new infos).
    bool    addTexturesFromTr3(const std::vector<uint8_t>& package,
                               std::map<int,int>& mapping, std::string& error);

    // Takes the trigger entry out of the source sector and appends it to
    // the target sector's chain. Both chains are rebuilt at the end, both
    // sectors repointed. The trigger keeps its reference word and its
    // commands word for word - it addresses its item via the index, not via
    // the place, and therefore stays valid.
    //
    // Aborts if the source does not have exactly one trigger or the target
    // is unsuitable per canAppendTrigger().
    bool    moveTrigger(int fromRoom, int fromSx, int fromSz,
                        int toRoom, int toSx, int toSz, std::string& error);

    // Like moveTrigger, but the source keeps its trigger: the target sector
    // receives a SECOND, identical copy. Needed for level defects where a
    // trigger lies on only one of several routes - see LevelFix.h.
    bool    copyTrigger(int fromRoom, int fromSx, int fromSz,
                           int toRoom, int toSx, int toSz, std::string& error);

    // Puts a NEW trigger onto a sector. That is the way to add enemies to
    // the level: no existing entry is read or rebuilt, only a chain is
    // appended at the end.
    //
    // art is the trigger kind (0 = TRIGGER, 1 = PAD, 5 = HEAVY, ...), mask
    // the five code bits (31 = all, the usual value). oneShot sets bit 8 of
    // the setup word - and that is more than "only once": _TestTriggers
    // (control.cpp:2415) then sets IFL_INVISIBLE on the woken item, which
    // changes the behavior of some enemies. Larson, for instance, flees at
    // 40 hit points without this bit and cannot be killed at all
    // (pierre.cpp:68).
    bool    addTrigger(int room, int sx, int sz, int art, int timer, bool oneShot,
                       int mask, const std::vector<Tr5TriggerCmd>& cmds,
                       std::string& error, int refItem = -1);

    // May a trigger be appended to this chain? Returns the obstacle or
    // nullptr. See the derivation in the implementation.
    static const char* canAppendTrigger(const std::vector<Tr5FloorEntry>& chain);


    // Turns a wall sector into a portal to targetRoom (ONE side - the
    // caller does the other side with the roles swapped).
    //
    // Precondition, found with SektorPruef on andy3: the 32-byte VIEWPORT
    // in the room's portal list must already exist. This function writes
    // only the sector side: floor/ceiling from the overlapped sector of the
    // target room (the portal columns of two rooms lie on top of each
    // other), box word as with all portal sectors (fx=6, box=2047), and a
    // DOOR entry goes onto the floordata chain - existing slopes (TILT)
    // stay, vanilla pattern [TILT][SPLIT][DOOR].
    //
    // Repeatable: if the chain already points at targetRoom, nothing
    // happens.
    bool    makePortal(int room, int sx, int sz, int targetRoom, std::string& error);

    // Makes a sector climbable: CLIMB entry with direction bits (0x100=0
    // degrees, 0x200=90, 0x400=180, 0x800=270 - Lara's view onto the
    // wall). ORs into existing entries; chain ordering see the body.
    bool    makeClimb(int room, int sx, int sz, int dirBits, std::string& error);

    // A room's water bit (ROOM_UNDERWATER, bit 0 of the room flags at +78).
    // A real intervention: 44 engine sites hang off it, not just visuals.
    bool    setRoomWater(int room, bool on, std::string& error);
    // --- Sound ---------------------------------------------------------
    //
    // A level only carries the samples it needs itself. If an enemy does
    // not appear in it in vanilla, its noises are missing - it then stands
    // mute in the level. Pierre is the case where that was noticed.
    int  soundInfoIndex(int map) const;    // -1 = this level does not know the sound
    int  soundSampleCount() const { return m_nSamples; }

    // Takes the sample behind srcMap over from src and stores it here under
    // dstMap. Random variants ("pick one of N") come along.
    // Does nothing and reports success if dstMap already resolves here.
    bool copySoundFrom(const Tr5Level& src, int srcMap, int dstMap, std::string& error);

    // --- Model transfer ------------------------------------------------
    //
    // Takes a complete object slot over from another level: meshes, mesh
    // pointers, bones, animations including state changes, ranges, commands
    // and frames, the referenced texture infos, the corresponding texture
    // pages (32 and 16 bit in sync) and the OBJECT_INFO entry. If the slot
    // exists in the target, its entry is replaced; the old data stays
    // behind as dead ballast - the same philosophy as everywhere: nothing
    // is removed, nothing shifts foreign indices.
    //
    // Slot 0 (LARA) is refused: it carries the level's animations, a
    // foreign slot 0 takes the level's own movements away (experienced in
    // game: missing switch and crawl animations).
    //
    // Sounds do NOT come along automatically - for that there is
    // copySoundFrom and the knowledge table in Tr5Transfer.cpp (hard-wired
    // weapon SFX).
    //
    // dstSlot (< 0 = same as slotId): the slot under which the copy is
    // registered in the TARGET. Needed for the fallback slots of the cross
    // enemy mix: the donor's MESHSWAP1 (405) lands there under 399-402, so
    // the target's own MESHSWAP1 stays untouched.
    //
    // meshesOnly: copy ONLY meshes, pointers and textures - no animations,
    // frames or bones. For mesh stores like MESHSWAP1: its object entry
    // does carry an anim_index but no animations of its own (Core legacy;
    // the generic slicing would run out of its blocks with it). The entry
    // in the target gets anim 0xFFFF, bone 0, frame 0 - at a store slot
    // only mesh_index is ever read anyway.
    //
    // animCount (<= 0 = heuristic "up to the next object"): explicit length
    // of the anim slice. Needed for the Imp (89): its control reaches with
    // anim_index+45 ACROSS the global anim table - the death animation
    // physically lies in the ranges of foreign scenery slots, the heuristic
    // cuts off after 18 anims and the engine would read past the table end
    // in the target (seen in game: death flight loop). The frame slice then
    // grows along past the highest frame pointer of the extended slice;
    // scenery anims copied along are harmless ballast per the principle
    // "remove nothing, shift nothing".
    bool copyMoveableFrom(const Tr5Level& src, int slotId, std::string& error,
                          int dstSlot = -1, bool meshesOnly = false,
                          int animCount = -1);

    // Lets one anim adopt the data and motion fields of another anim of the
    // SAME level: frame pointer, rate, frame_size, speeds and the command
    // reference. State, frame range, next anim and changes stay. Meant for
    // dead heritage anims like SWAT's state-3 fire anim (+2): it shares its
    // frame pointer with the target anim (+3) but reads its 34-word frames
    // with stride 36 - from keyframe 1 on, garbage appears (model
    // disappears, shadow smears). Core never noticed, because SWAT itself
    // fires via state 35; only Guard/CRANE_GUY fire via state 3, and no
    // vanilla level lets both sets play together. Requires identical frame
    // ranges in both anims (family sets are template-identical).
    bool adoptAnimData(int dstSlot, int dstOffset, int srcSlot,
                             int srcOffset, std::string& error);

    // Implants a single Lara anim including frame data, commands and sounds
    // from a DONOR level (ANIM_KEYCARD 437, #53). The anim slot exists
    // everywhere, the image data is level-specific - reasoning and limits
    // at the body in Tr5Transfer.cpp.
    bool copyLaraAnimFrom(const Tr5Level& src, int animNr, std::string& error);

    // Does Lara's anim <animNr> carry real image data here? FrameSize zero
    // is Core's stub marker; additionally the slot is recomputed.
    bool    laraAnimUsable(int animNr) const;

    // Which anim does Lara's <animNr> continue into (next_animation)? For
    // the chain repair - movements consist of several links, and Core stubs
    // them as a whole. -1 = does not exist.
    int     laraAnimNext(int animNr) const;

    // The object slot as it stands in the file. anim < 0 means: none.
    struct MoveableInfo
    {
        int slot = -1;
        int nmeshes = 0, meshIndex = 0, boneIndex = 0, animIndex = -1;
        uint32_t frameOffset = 0;
    };
    bool moveableInfo(int slotId, MoveableInfo& out) const;
    std::vector<int> moveableSlots() const;

    // For diagnostic tools only: raw values from the blocks. Outside the
    // block, 0 comes back - the tools compare donor against copy, where an
    // outlier stands out as a difference.
    int32_t debugReadBoneLong(long long i) const;
    int16_t debugReadFrameShort(long long i) const;
    // Diagnosis of the physical sample chain: where the num_samples field
    // behind the main block lies and how many samples it counts, plus the
    // raw bytes.
    bool debugSampleArray(size_t& numSamplesAt, int32_t& n) const
    { if (!m_soundOk) return false; numSamplesAt = m_numSamplesAt; n = m_nSamples; return true; }
    const std::vector<uint8_t>& debugBytes() const { return m_bytes; }
    int32_t debugTexinfoCount() const { return m_texinfos.count; }
    bool debugTexinfoBlock(size_t& dataAt, int32_t& n) const { dataAt = m_texinfos.dataAt; n = m_texinfos.count; return m_texinfos.count > 0; }
    // Anim and command block for diagnostic tools (AnimDump): location and
    // count, record format see SZ_ANIM in Tr5Transfer.cpp (40 bytes).
    bool debugAnimBlock(size_t& dataAt, int32_t& n) const { dataAt = m_anims.dataAt; n = m_anims.count; return m_anims.count > 0; }
    // Sound chain for measuring tools: soundmap, SAMPLE_INFO field, sample
    // count.
    bool debugSoundBlocks(size_t& lutAt, size_t& infosAt, int32_t& nInfos, int32_t& nSamples) const
    { if (!m_soundOk) return false;
      lutAt = m_lutAt; infosAt = m_infosAt; nInfos = m_nInfos; nSamples = m_nSamples; return true; }

    // Model blocks for measuring tools (object table and frame slice).
    bool debugModelBlocks(size_t& objAt, int32_t& objN, size_t& frAt, int32_t& frN) const
    { objAt = m_objects.dataAt; objN = m_objects.count;
      frAt = m_frames.dataAt;  frN  = m_frames.count;  return objN > 0; }
    bool debugCommandBlock(size_t& dataAt, int32_t& n) const { dataAt = m_commands.dataAt; n = m_commands.count; return m_commands.count > 0; }
    bool debugChangeBlock(size_t& dataAt, int32_t& n) const { dataAt = m_changes.dataAt; n = m_changes.count; return m_changes.count > 0; }
    bool debugRangeBlock(size_t& dataAt, int32_t& n) const { dataAt = m_ranges.dataAt; n = m_ranges.count; return m_ranges.count > 0; }
    // Byte position of the SAMPLE_INFO of a soundmap id (8 bytes: number
    // s16, volume u8, radius s8, randomness s8, pitch s8, flags s16).
    bool debugSoundInfoAt(int map, size_t& at) const
    { const int i = soundInfoIndex(map); if (i < 0) return false; at = m_infosAt + static_cast<size_t>(i) * 8; return true; }
    // Location and extent of the two mesh blocks - for raw comparisons of
    // two versions of the same level (LevelDiff).
    int32_t debugMeshPtrCount() const { return m_meshPtrs.count; }
    bool debugMeshDataBlock(size_t& dataAt, int32_t& n) const
    { dataAt = m_meshData.dataAt; n = m_meshData.count; return m_meshData.count > 0; }
    // Byte position of a mesh via its pointer index - for raw comparisons
    // (walk metrics do not distinguish identical geometry with different
    // texturing).
    bool debugMeshAt(int ptrIndex, size_t& at) const
    {
        if (ptrIndex < 0 || ptrIndex >= m_meshPtrs.count) return false;
        uint32_t ofs;
        std::memcpy(&ofs, m_bytes.data() + m_meshPtrs.dataAt + static_cast<size_t>(ptrIndex) * 4, 4);
        at = m_meshData.dataAt + ofs;
        return true;
    }
    // Header of a mesh (x, y, z, r, flags, nVerts word) via its pointer
    // index. false if the pointer does not exist.
    bool    debugMeshHeader(int ptrIndex, int16_t out[6]) const;
    // Walks a mesh with the correct normals rule (0 = normals, negative
    // only = prelight) and returns its metrics. Defined in Tr5Transfer.cpp,
    // where the walker lives.
    bool    debugMeshWalk(int ptrIndex, int& nVerts, int& nNorms,
                          int& ngt4, int& ngt3, int& bytes) const;

    // The outfit byte (LaraType) in the level header, behind the texture
    // blocks.
    int  laraType() const;
    bool setLaraType(int value);
    int  foreignTalkHeads() const;
    bool setForeignTalkHeads(int value);

    // --- Texture workshop (#104) ---------------------------------------
    //
    // The level's two texture blocks are unpacked ONCE at the first page
    // import and kept open; every further import only appends. Packing and
    // putting them back into the file happens ONCE at closing - save()
    // calls it itself, the caller need not do anything.
    //
    // Previously, unpacking, packing, file reassembly and reparse ran per
    // imported type: under allsanity ten times per level over 30+ MB, and
    // that is exactly where the measured 5-10 seconds of the enemy mix
    // lived (LOADING a level costs only 15 ms). Important for all
    // intermediate states: as long as the workshop is open, the texture
    // blocks in m_bytes carry the OLD state - whoever reads them must close
    // first. The main block behind them does not shift until closing, all
    // block offsets stay valid.
    bool texWorkshopClose(std::string& error);

private:
    bool                 m_texOpen = false;
    bool                 m_texDirty = false;
    std::vector<uint8_t> m_tex32, m_tex16;

    bool texWorkshopOpen(std::string& error);
    bool                 m_allowOverLimit = false;
    std::vector<uint8_t> m_bytes;
    int32_t              m_version = 0;
    int16_t              m_rtPages = 0, m_otPages = 0, m_btPages = 0;
    size_t               m_mainOffset = 0;   // start of the main block payload
    size_t               m_mainSize = 0;
    size_t               m_itemCountOffset = 0;
    int32_t              m_itemCount = 0;
    size_t               m_floorCountOffset = 0;  // the int32 with numFloorData
    size_t               m_floorAt = 0;           // first short of the floordata
    int32_t              m_floorCount = 0;

    // --- Sound block ---------------------------------------------------
    // Everything except the sample data itself lies IN the main block,
    // behind the items. Whoever inserts something before it - items or
    // floordata - must drag these offsets along; that is exactly what
    // shiftSound() is for.
    size_t               m_lutAt = 0;             // short sample_lut[450]
    size_t               m_numInfosAt = 0;
    size_t               m_infosAt = 0;
    int32_t              m_nInfos = 0;
    size_t               m_numSamplesInMainAt = 0;  // num_samples, copy in the main block
    size_t               m_numSamplesAt = 0;        // num_samples, copy behind it
    int32_t              m_nSamples = 0;
    bool                 m_soundOk = false;

    bool findSound();                     // after findItems, may fail
    void shiftSound(size_t bytes, size_t fromOffset);

    // --- Block map -----------------------------------------------------
    // Location of the blocks the model transfer must touch. countAt is the
    // position of the int32 with the count, dataAt the first data byte.
    // Written in findItems and invalid after EVERY insertion until
    // reparse() has run - which is why the transfer works with a splice
    // list and inserts everything in one stroke.
    struct Block
    {
        size_t  countAt = 0;
        size_t  dataAt = 0;
        int32_t count = 0;
    };
    Block m_meshData, m_meshPtrs, m_anims, m_changes, m_ranges, m_commands,
          m_bones, m_frames, m_objects, m_sprites, m_texinfos;

    // Re-reads header and main block after bytes were inserted.
    // copyMoveableFrom is a member and defined in Tr5Transfer.cpp - the
    // split keeps Tr5Level.cpp readable, nothing more.
    bool reparse(std::string& error);
    int                  m_roomCount = 0;
    std::vector<Tr5Room> m_rooms;
    int                  m_roomMismatch = 0;

    // Appends a chain to the end of the block and returns its index. Empty
    // chain -> 0 ("no floordata"). Drags numFloorData and the doubly kept
    // main block size along.
    bool appendChain(const std::vector<Tr5FloorEntry>& chain, uint16_t& outIndex,
                     std::string& error);
    void setSectorIndex(const Tr5Room& r, int sx, int sz, uint16_t index);

    // Breaks down a room blob. offset/size point at the blob in the main
    // block, index is the running room number.
    void parseRoom(size_t offset, size_t size, int index);

    bool readHeader(std::string& error);
    bool findItems(std::string& error);   // walks the main block
};

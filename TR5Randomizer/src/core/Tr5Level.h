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
// An AI object (AIOBJECT, global/types.h, 24 bytes in the block right
// behind the items). Markers like AI_AMBUSH (slot 379): Larson/Pierre flee
// from 40 HP to an AIAmbush with matching OCB in their zone and despawn
// there (GetAITarget/FindAITargetObject, box.cpp). boxNumber may be 0 - the
// engine computes it itself at runtime (box.cpp).
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
    int kind = 0;                        // only for function == 4 (trigger)
    std::vector<uint16_t> words;
};

//----------------------------------------------------------------------------
// A trigger command. type is the 4-bit command kind (enum trigobj_types,
// global/types.h): 0 = TO_OBJECT, 1 = TO_CAMERA, 3 = TO_FLIPMAP,
// 8 = TO_CD, 13 = TO_CUTSCENE and so on.
//----------------------------------------------------------------------------
struct Tr5TriggerCmd
{
    int type = 0;
    int param = 0;
};

//----------------------------------------------------------------------------
// The three rules for walking a floordata chain, kept as free functions
// rather than duplicated by every reader - a copied bit layout is a
// second chance to get it wrong. Each one names the engine line it was
// taken from; see Tr5Level.cpp.
//----------------------------------------------------------------------------
namespace Tr5Floor
{
    // SWITCH (2), KEY (3) and PICKUP (4) push a reference word in front of
    // the command list. It is NOT a command.
    bool hasRefWord(int kind);

    // Extra words of a non-trigger entry - the cases where GetHeight does a
    // "data++".
    int extraWords(int fn);

    // Command kind: 4 bits, not 5. Bit 15 is the end bit.
    int cmdType(uint16_t c);
}

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
    // GetAIPickups (setup.cpp) turns AI objects within 512 units into
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

    // Corner-exact floor / ceiling height of a sector (world y, grows
    // downward), from the floordata the way the engine reads it. cx and cz
    // are the corner within the sector and must be 0 or 1024.
    //
    // Why not floorPosAt (Floor.h): that one answers "where is the floor
    // under this point", which is what an item needs. Room geometry needs
    // the CORNER, and the engine's own formula cannot be asked for it - see
    // the substitution derived above cornerFloor() in Tr5Level.cpp.
    //
    // Returns false for a wall (height byte -127), an invalid sector or
    // corner, and an unreadable chain - the caller then has no height, not a
    // wrong one.
    bool    cornerFloor(int room, int sx, int sz, int cx, int cz, long& y) const;
    bool    cornerCeiling(int room, int sx, int sz, int cx, int cz, long& y) const;

    // Installs a moveable fetched from a TR3 level (package from TR3Fetch,
    // magic "T35H"). Implemented in Tr5Tr3Import.cpp - deliberately next to
    // the proven TR5->TR5 path and not inside it.
    //
    // texinfoIndex: EVERY face points at this already existing texinfo of
    // the target afterwards. That way no texture page is inserted and no
    // tile number shifts; the enemy is wrongly textured, but the page
    // management stays untouched. Real textures are a separate step via
    // Tr5Transfer.cpp.
    //
    // keepMeshes: indices that are NOT taken from the package - those mesh
    // pointers keep pointing at the mesh the target slot already uses. For a
    // Lara outfit that is the head, LM_HEAD = 14; see the comment at the
    // pointer table in Tr5Tr3Import.cpp for why it has to stay TR5's.
    // Counts the colours one mesh of a slot actually wears, read off its own
    // texture pages, and hands back the most frequent ones - brightest share
    // first. Used to find out what skin tone a target's Lara has, so an
    // imported flat colour can be set to it instead of guessed.
    struct ToneCount { int r, g, b, count; };

    // Makes one mesh of a slot draw nothing, by setting its quad and its
    // triangle count to zero. The face bytes stay where they are and are
    // simply never reached again - meshes are addressed by pointer, so
    // nothing behind them moves. Vertices are kept on purpose: Lara's seam
    // sewing reads them, and a joint that draws nothing should still not
    // change how its neighbours are stitched.
    bool    clearMeshFaces(int targetSlot, int meshNo, std::string& error);

    // Paints one mesh of a slot in a single flat colour, by pointing all of
    // its faces at a texture entry whose area already IS that colour. No page
    // and no entry is added - the flat colour page an import brings along
    // carries solid blocks, and this looks for the closest of them. Fails
    // rather than settling for a patterned entry.
    bool    paintMeshFlat(int targetSlot, int meshNo, int r, int g, int b,
                          int& usedTexinfo, int& gotR, int& gotG, int& gotB,
                          std::string& error);
    bool    meshTones(int targetSlot, int meshNo, std::vector<ToneCount>& out,
                      int& pixels, std::string& error);

    bool    addMoveableFromTr3(const std::vector<uint8_t>& package, int targetSlot,
                               int texinfoIndex, std::string& error,
                               const std::vector<int>* keepMeshes = 0);

    // Puts the sounds of a TR3 package ("T35S" from TR3Sound) onto the
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
    // kind is the trigger kind (0 = TRIGGER, 1 = PAD, 5 = HEAVY, ...), mask
    // the five code bits (31 = all, the usual value). oneShot sets bit 8 of
    // the setup word - and that is more than "only once": _TestTriggers
    // (control.cpp) then sets IFL_INVISIBLE on the woken item, which
    // changes the behavior of some enemies. Larson, for instance, flees at
    // 40 hit points without this bit and cannot be killed at all
    // (PierreControl, pierre.cpp).
    bool    addTrigger(int room, int sx, int sz, int kind, int timer, bool oneShot,
                       int mask, const std::vector<Tr5TriggerCmd>& cmds,
                       std::string& error, int refItem = -1);

    // Takes ONE object command out of the trigger on this sector - the
    // rest of it stays exactly as it was.
    //
    // Needed where Core points a trigger at an item that must not be
    // touched, while the same trigger does other things that have to
    // stay. Removing the whole trigger would take those with it.
    //
    // The chain is written anew at the end of the floordata and the
    // sector points at the copy, exactly as moveTrigger and copyTrigger
    // do; the old words stay behind unreferenced.
    bool    removeTriggerObject(int room, int sx, int sz, int item,
                                std::string& error)
    { return editTriggerObject(room, sx, sz, item, -1, 0, error); }

    // Same surgery, but the command is REPLACED instead of dropped: the
    // list keeps its length and the trigger keeps working.
    //
    // Needed where the object command is the trigger's only one - taking
    // it out would leave a trigger with nothing to do, which the engine
    // would still walk over. A void flipeffect (effect_routines index 44,
    // game/effects.cpp) is the quietest filler there is: it runs and does
    // nothing, where a music command would be audible.
    bool    replaceTriggerObject(int room, int sx, int sz, int item,
                                 int newType, int newParam, std::string& error)
    { return editTriggerObject(room, sx, sz, item, newType, newParam, error); }

    // Rewrites the trigger on this sector COMPLETELY: kind, timer,
    // one-shot, mask and command list. Everything else in the chain (a
    // tilt, a portal) stays where it is.
    //
    // Changing the kind is the point of it. A PICKUP, KEY or SWITCH
    // trigger carries a reference word naming the item it hangs on;
    // a plain TRIGGER has none, so switching the kind takes that pointer
    // out - which is the only way to free an item the randomizer pins
    // because of it (Randomizer.cpp, findTriggerPinnedItems).
    //
    // At least one command is required, and that is an engine rule, not
    // taste: _TestTriggers reads the commands in a do-while
    // (control.cpp), so it always reads one word. A trigger without one
    // would send it into the next floordata entry.
    bool    setTrigger(int room, int sx, int sz, int kind, int timer,
                       bool oneShot, int mask,
                       const std::vector<Tr5TriggerCmd>& cmds,
                       std::string& error, int refItem = -1);

    // One command word of a trigger, dropped (newType < 0) or replaced.
    // Both public wrappers above lead here - one walk, one truth.
    bool    editTriggerObject(int room, int sx, int sz, int item,
                              int newType, int newParam, std::string& error);

    // May a trigger be appended to this chain? Returns the obstacle or
    // nullptr. See the derivation in the implementation.
    static const char* canAppendTrigger(const std::vector<Tr5FloorEntry>& chain);


    // Turns a wall sector into a portal to targetRoom (ONE side - the
    // caller does the other side with the roles swapped).
    //
    // Precondition, found with SectorCheck on andy3: the 32-byte VIEWPORT
    // in the room's portal list must already exist. This function writes
    // only the sector side: floor/ceiling from the overlapped sector of the
    // target room (the portal columns of two rooms lie on top of each
    // other), box word as with all portal sectors (fx=6, box=2047), and a
    // DOOR entry goes onto the floordata chain - existing slopes (TILT)
    // stay, vanilla pattern [TILT][SPLIT][DOOR].
    //
    // Repeatable: if the chain already points at targetRoom, nothing
    // happens.
    // Both sectors are NAMED by the caller rather than derived from world
    // position: deriving the far one lands on the neighbour's INTERIOR
    // sector rather than its border ring, and an interior sector is read as
    // "stacked" - Andrea3's room 74 got a window facing the floor from
    // exactly that mistake.
    bool    makePortal(int room, int sx, int sz,
                       int targetRoom, int tsx, int tsz, std::string& error);

    // The VERTICAL kind - two rooms stacked on each other. Nothing like the
    // horizontal one: no floordata, no view window, no box. One byte of the
    // sector, and that is the whole connection.
    bool    makeVerticalPortal(int room, int sx, int sz, int targetRoom,
                               bool ceiling, std::string& error);

    // Creates the 32-byte view window between two rooms where none exists,
    // in BOTH directions. makePortal calls this by itself, so a secret list
    // needs no new spelling for it.
    //
    // The winding of the four corners is not invented: it is copied from a
    // portal the room already has on the same wall, and only the corner
    // positions are moved. Guessing it wrong does not fail loudly, it draws
    // a black hole.
    bool    addViewWindow(int roomA, int sxA, int szA,
                          int roomB, int sxB, int szB, std::string& error);

    // A view window along a run of border sectors, in BOTH directions, and
    // nothing else: no floordata, no box word, no HD cut.
    //
    // For an opening the level's own geometry leaves open - neither room
    // draws a face there - while no window describes it, so the portal walk
    // never reaches the room behind it and the screen shows the clear
    // colour. Such an opening lies in the air above a passage: Lara cannot
    // pass it, and the floordata already runs through the stacked rooms'
    // own links, so the sectors stay as they are. And since no face is
    // opened, the Remaster's art there is right as it stands; a cut would
    // only take away what stands in the opening.
    //
    // (sx,sz)..(sx2,sz2) is the run on roomA's border ring, one sector
    // deep, and each room gets ONE record over it, the way Core writes the
    // run of a wall. The height is wallWindow's over the run, the plane and
    // the one-unit inset are addViewWindow's, and the corner order is
    // copied from the level's first wall window that faces the same way.
    //
    // Repeatable: where both records already stand exactly so, nothing is
    // written. Refused where either room has another window to the other
    // on that plane.
    bool    addSightWindow(int roomA, int sx, int sz, int sx2, int sz2,
                           int roomB, std::string& error);

    // What a wall window has to describe, worked out from the
    // floordata instead of from the two height bytes.
    //
    // (sx,sz) is the sector of roomA that hands over to roomB - the very one
    // addViewWindow puts its window on. The opening at each of the two
    // corners of the shared edge is
    //     [ max(ceiling A, ceiling B) .. min(floor A, floor B) ]
    // and the window is the BOUNDING BOX over both corners, so a sloped
    // opening is enclosed rather than cut. yCeil comes out above yFloor (y
    // grows downward).
    //
    // Measured over the 2938 one-sector wall windows of the 13 campaign
    // levels (WinCheck): every single one of them is a rectangle, 0
    // trapezoids, and of the 2722 whose sector pair can be located again,
    // 2622 (96.3 %) are exactly this box against 1715 (63.0 %) for the height
    // bytes. Of the 100 that differ, 99 have Core's window LARGER - the
    // harmless direction, since the window only clips (SetRoomBounds,
    // draw.cpp). The one the other way round is rich2 room 73 (x3,z0),
    // where room 73's own floor drops to +256 at the shared edge and this box
    // comes out 1280 units taller than Core's; a larger window clips less,
    // never more.
    //
    // false where there is no wall opening to describe - a vertical pair, a
    // sector off the border ring, an unreadable chain, or two cross sections
    // that do not overlap at all.
    bool    wallWindow(int roomA, int sx, int sz, int roomB,
                       long& yCeil, long& yFloor) const;

    // The raw view windows of one room: target, normal, then four corners.
    // For holding a generated portal against a hand-built one - the only way
    // to see whether a record is right, since a wrong one does not fail, it
    // draws a hole.
    struct PortalRecord { int toRoom; int nx, ny, nz; int v[4][3]; };

    // One sector as the engine reads it. roomBelow/roomAbove are the VERTICAL
    // links - a stacked pair of rooms is joined through these, not through a
    // floordata door.
    struct SectorInfo { int sx, sz, fdIndex, box, roomBelow, floor, roomAbove, ceiling;
                       int doorTo; };   // floordata function 1, else -1

    // One drawn face of a room. The wall you look at IS one of these - the
    // portal underneath does not hide it, which is why a new opening still
    // shows the old wall. Corners are WORLD coordinates.
    struct FaceInfo
    {
        int  roomlet;
        bool quad;             // false = triangle, three corners used
        int  texInfo;          // texword & 0x3FFF
        bool doubleSided;      // texword bit 15
        int  effects;          // the word behind the texword
        int  index[4];         // vertex indices, roomlet-relative
        long v[4][3];          // world x, y, z, rounded
        size_t texWordAt;      // where the texword sits in the file
        size_t indexAt;        // where the four indices sit
    };
    bool    roomFaces(int room, std::vector<FaceInfo>& out, std::string& error) const;

    // One texture rule for buildRoomGeometry. -1 in a field means "does
    // not apply", so a rule with sx, sz and band all -1 is the room-wide
    // default and every later rule refines what an earlier one set. band is
    // counted from the floor upward, which is how a person looks at a wall;
    // the data runs the other way and is turned round on the way in.
    //
    // Each field names ONE texinfo, and a texinfo carries the shape it was cut
    // for - which is why quads and triangles have fields of their own. Corner-
    // exact geometry produces both (a split sector becomes two triangles, and
    // so does a wall piece that runs out at one corner), and a triangle
    // wearing a quad's texinfo maps wrongly.
    //
    // A triangle takes, in this order: the rule's own *Tri field, else the
    // commonest TRIANGLE texinfo of the like= room, else the quad number -
    // and that last case is said out loud instead of mapping wrongly in
    // silence.
    struct RoomTexRule
    {
        int sx = -1, sz = -1, band = -1;
        int floorTex = -1, ceilTex = -1, wallTex = -1;      // quads
        int floorTri = -1, ceilTri = -1, wallTri = -1;      // triangles
    };

    // Give a room that has no drawn surface one, built from its own floor
    // data. likeRoom < 0 means the textures come from the rules alone.
    // Idempotent: a room that already carries faces is left untouched.
    bool    buildRoomGeometry(int room, int likeRoom,
                              const std::vector<RoomTexRule>& rules,
                              std::string& error);

    // A raw 4-byte field of the 208-byte room header, for when the engine
    // reads a structure through a stored offset and our own parser does not.
    bool    headerField(int room, int offset, unsigned& value, std::string& error) const;

    // Shift a whole room. dx/dz in whole sectors, dy in whole height steps -
    // the caller has already rounded, this one refuses anything else.
    bool    moveRoom(int room, long dx, long dy, long dz, std::string& error);

    // A window belongs to a SECTOR - two rooms can be joined at several.
    bool    windowAt(int room, int sx, int sz, int targetRoom) const;

    bool    clearWallFaces(int room, int sx, int sz, int& cleared, std::string& error);
    bool    copyWallTexture(int fromRoom, int fsx, int fsz,
                            int toRoom, int tsx, int tsz, int& changed, std::string& error);
    bool    sectorInfo(int room, std::vector<SectorInfo>& out, std::string& error) const;
    bool    portalRecords(int room, std::vector<PortalRecord>& out, std::string& error) const;

    // The level's fixed cameras (OBJECT_VECTOR), in the order a
    // trigger's Camera command indexes them.
    //
    // flags is the interesting part: bits 0 and 1 make the engine treat
    // the camera as a GAME camera rather than a cinematic one - it sets
    // SniperCamActive, which locks the look key and the inventory
    // (control.cpp) and, with bit 1, aims at Lara's torso and draws the
    // sniper overlay (camera.cpp). Whether first person may push a
    // camera aside hangs on exactly that.
    struct CameraRecord { int x, y, z, room, flags; };
    bool    cameraRecords(std::vector<CameraRecord>& out, std::string& error) const;

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
    // from a DONOR level (ANIM_KEYCARD 437). The anim slot exists
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

    // The bounding box of the model's REST POSE, i.e. of anim 0 / frame 0.
    //
    // A TR5 frame begins with six int16 - minX, maxX, minY, maxY, minZ,
    // maxZ - and the moveable's frameOffset points at exactly that first
    // frame (byte offset into the frame block). out receives the six values
    // in that order; maxY - minY is the model's height in world units, and
    // y grows downward, so a taller model has the larger difference.
    //
    // Returns false if the slot is missing or its frame pointer does not
    // lie inside the frame block. A one-frame STUB (the soldier family,
    // whose real anims the engine fetches from a base type at runtime)
    // reads as an all-zero box - present, but no measure; the caller has to
    // decide what to do with a height of 0.
    bool frameBounds(int slotId, int16_t out[6]) const;

    // The sector's raw ceiling base: the ceiling byte (offset +7 of the
    // eight-byte FLOOR_INFO) times 256, i.e. the height before any slope.
    // The fallback for cornerCeiling, which answers false at a wall corner
    // and at an unreadable chain - there the flat value is still the best
    // knowledge there is. false means the sector itself does not exist or
    // the byte is NO_HEIGHT (-127, wall): then there is no height at all.
    bool sectorCeilingBase(int room, int sx, int sz, long& y) const;

    // The LOWEST ceiling of a sector - what an object standing there has
    // over its head. Largest of the four corner values, because y grows
    // downward.
    //
    // Two things separate it from calling cornerCeiling four times:
    //
    //  1. It follows the CEILING PORTALS first. GetCeiling walks up through
    //     sky_room (offset +6 of the FLOOR_INFO, 255 = none) before it even
    //     looks at a ceiling byte - because a
    //     sector whose ceiling is a portal carries the room BOUNDARY there,
    //     not a ceiling. Reading that boundary as a ceiling calls ten of
    //     Core's own vanilla enemies too tall for their spot (measured with
    //     HeightCheck over all 15 levels), among them the guard in joby3
    //     room 8, who has stood there since 2000.
    //  2. Where a corner has no value of its own (wall corner, unreadable
    //     chain), the sector's flat ceiling base stands in - a corner
    //     without an answer must not silently drop out of the maximum.
    //
    // false means there is no ceiling to be had at all: the sector is not
    // there, or its byte is NO_HEIGHT.
    bool sectorCeilingLow(int room, int sx, int sz, long& y) const;

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
    /* The ANIMATED TEXTURE ranges, for the diagnostic tools: where the block
     * lies and how many SHORTS it holds. The layout is the engine's own
     * `aranges` (specific\output.cpp, S_AnimateTextures): one short with
     * the number of ranges, then per range one short with the
     * number of FRAMES MINUS ONE followed by that many plus one texinfo
     * numbers. Reading it here rather than in the tool keeps the one walk of
     * the main block the one place that knows where anything is. */
    bool debugAnimTexBlock(size_t& dataAt, int32_t& shorts) const
    { dataAt = m_animtex.dataAt; shorts = m_animtex.count; return m_animtex.count > 0; }
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

    // data[3] of the same header block: which Tomb Raider III outfit the
    // randomizer put on this level. 0 means "none" - the
    // body is whatever data[0] says.
    //
    // It exists because data[0] cannot say it. The wardrobe writes
    // LARA_PONYTAIL (5) both for the Rome body in a Russia level and for
    // EVERY guest outfit, and nothing else in the file separates the two -
    // so the engine's HD path reads a LARA_PONYTAIL level as "classic" and
    // would dress a rolled Nevada Lara in Rome clothes.
    //
    // The numbers are the engine's own HD_LARA_OUTFIT_* (hd_lara.h), so the
    // byte goes into HDLaraOutfitFor unchanged and no side has to translate:
    //   0 none        5 TR3_ANTARC   6 TR3_NEVADA   7 TR3_COAST
    //                 8 TR3_CATSUIT
    // The table that maps a wardrobe id onto one of them is
    // Patch::hdOutfitId - one place, both sides documented from it.
    //
    // Measured free before it was used: all 17 shipped .trc files carry 0 in
    // data[3] through data[15].
    int  hdOutfit() const;
    bool setHdOutfit(int value);

    // The weather byte in the same header block: 0 dry, 1 rain,
    // 2 snow. Read by the engine in specific\file.cpp (data[1]) and by
    // nothing else than DoWeather (specific\specificfx.cpp).
    int  weatherType() const;
    bool setWeatherType(int value);

    // How many rooms of this level could show rain or snow at all.
    // The engine's own criterion, not a guess - see the definition in
    // Tr5Level.cpp for the two conditions and where they come from.
    // 0 means: a weather byte would be written into the void.
    int  weatherRooms() const;

    // --- HD markers ------------------------------------------------------
    //
    // The engine can draw a level with the Remaster's own geometry, and
    // that geometry describes the VANILLA level. Wherever this randomizer
    // changed a room's shape, the HD art is a lie - it still shows the wall
    // we just opened - and wherever a model came out of another level file,
    // the Remaster's name table for THIS level resolves the slot to the
    // wrong thing (ANIMATING10 means something different in every level).
    //
    // Both are recorded HERE, in the level object, and not in the caller:
    // this is the only place every one of those changes has to pass
    // through, so nothing can be forgotten by whoever calls it. The two
    // lists survive the second apply() pass for the secret list by
    // themselves, because the level object does.
    //
    // What counts as touched geometry, and why only these:
    //   clearWallFaces   takes drawn faces away
    //   addViewWindow    puts a 32-byte window into the room list
    //   makePortal       opens a wall (and calls addViewWindow)
    //   makeVerticalPortal   the stacked kind
    //   buildRoomGeometry    builds faces for a room that had none
    //   moveRoom with dy   moves every vertex of the room
    // NOT counted: moveRoom with dx/dz only (the vertices are room-local
    // and ride along with the origin), room flags (water, climb), items,
    // triggers and floordata - none of them is geometry.
    //
    // Two kinds of mark, and the difference is how much of the room's HD
    // art the change makes wrong:
    //
    //   THE WHOLE ROOM (hdMarkRoom) - moveRoom with dy and
    //   buildRoomGeometry. A room that was lifted, or that had no faces at
    //   all and now has some, shares nothing with the art the .TRG holds
    //   for it. There is nothing to keep, and the room is drawn classic.
    //
    //   ONE OPENING (hdMarkCut) - makePortal, addViewWindow,
    //   makeVerticalPortal and clearWallFaces. Those change ONE sector's
    //   worth of wall, floor or ceiling; the rest of the room is exactly
    //   what the Remaster drew. Marking the whole room for it turned four
    //   of ANDY3's 211 rooms classic in the middle of an HD level, with HD
    //   floors running into classic ones and the void showing in the seam.
    //   So the edit records the BOX its opening lives in instead, and the
    //   engine drops the HD triangles inside that box and keeps the room.
    struct HdDonor
    {
        int         slot;       // the slot in THIS level
        int         fromSlot;   // the slot it was taken from in the donor
        std::string level;      // donor level stem, or "TR3" for a guest
    };

    // An axis-aligned box in WORLD units, ascending corners, that contains
    // one opening this run made. Derived from the geometry the edit itself
    // wrote - the view window's own four corners, the sector square at the
    // height it opened, the faces it degenerated - never from a guess about
    // where a wall might be.
    //
    // Y GROWS DOWNWARD in this format, so lo[1] is the opening's CEILING
    // and hi[1] its FLOOR. The box grows along ONE axis and shrinks along
    // the other two:
    //   - HD_CUT_OUT_NORMAL (512, half a sector) OUTWARD along the
    //     opening's own normal, both ways. The Remaster's walls are built
    //     thick and their faces do not sit on the classic wall plane; half
    //     a sector reaches the art on both sides of it.
    //   - HD_CUT_IN_PLANE (128) INWARD along the other two axes.
    //
    // The inward step matters because a cut room KEEPS its HD art and an
    // HD room never draws its classic roomlets, so whatever the box takes
    // out is replaced by NOTHING - it is void, not a repair. The box may
    // therefore only ever hold art that stands INSIDE the opening. A view
    // window runs from its sector's ceiling to its floor by construction, so
    // an outward step of even one unit reaches the floor and the ceiling of
    // the passage itself, and those are flat quads lying exactly in the
    // box's boundary plane: measured 400,950 pixels of ANDY3's room 143 -
    // 43.5 % of the frame - turning into clear colour for that one reason,
    // and 41,687 more around the village gate outside the doorway. One step
    // INWARD and the same quads miss the box, while every wall triangle
    // that crosses the opening still meets it, because the engine's test is
    // a real triangle/box intersection and not a corner-inside test.
    //
    // The price is honest and small: a triangle that lives wholly inside
    // the outer 128 units of the opening survives as a rim. Measured on
    // ANDY3's two doorways, no such triangle exists.
    //
    // The inward step is clamped to half the extent, so a narrow opening
    // collapses to a plane through its own middle rather than inverting.
    struct HdCut
    {
        int lo[3];
        int hi[3];

        bool operator<(const HdCut& o) const
        {
            for (int i = 0; i < 3; ++i)
            {
                if (lo[i] != o.lo[i]) return lo[i] < o.lo[i];
                if (hi[i] != o.hi[i]) return hi[i] < o.hi[i];
            }
            return false;
        }
    };

    static const int HD_CUT_OUT_NORMAL = 512;
    static const int HD_CUT_IN_PLANE   = 128;

    void hdMarkRoom(int room);
    void hdMarkDonor(int slot, int fromSlot, const std::string& level);
    // A sound id this run imported from another game.
    //
    // The Tomb Raider III guests bring their own recordings and those go
    // into free slots of the TR5 soundmap - the vulture on 420..423, the
    // Compy on 430..434, the raptor on 438..444, the T-Rex on 445..449
    // (core\Tr3Source.cpp, dinos()). "Free" means free IN A TR5 LEVEL. It
    // does not mean free in the REMASTER'S BANK, and four of the vulture's
    // four are RICH1's VCI scientist voice lines there.
    //
    // The engine's HQ bridge pairs a level sample with a bank sample BY
    // SOUND ID, and a plausibility rule on the length is all that stands
    // between it and a wrong pairing. Measured on the randomized ANDREA1 of
    // seed 380: the vulture's id 422 is a 604 ms screech and the bank's id 422
    // is a 624 ms voice line - well inside the fifth the rule allows - so the
    // bird died saying something in English. The other three (322, 425 and
    // 406 ms against 624, 624 and 1037) are refused by length and the bug
    // looked like a one-off.
    //
    // A length cannot decide this and a name can. The run knows exactly
    // which ids it wrote; the engine cannot know. So the run says so, in
    // one line of the sidecar, and the engine's bridge passes those ids
    // over. Ids the run MOVED between TR5 levels are not imports and are
    // not listed: those really are the remaster's own sounds.
    void hdMarkSoundImport(int soundId);
    const std::set<int>& hdSoundImports() const { return m_hdSoundImports; }
    const std::set<int>& hdTouchedRooms() const { return m_hdRooms; }
    const std::vector<HdDonor>& hdDonors() const { return m_hdDonors; }
    // A std::set, so the same seed writes the same lines in the same order
    // and a second edit over the same opening costs no second line.
    const std::set<HdCut>& hdCuts() const { return m_hdCuts; }

    // --- Texture workshop -------------------------------------------------
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
    // What the run changed about the geometry and where foreign
    // models came from. Cleared by load(), so a reused object never carries
    // the previous level's marks.
    std::set<int>        m_hdRooms;
    std::vector<HdDonor> m_hdDonors;
    std::set<HdCut>      m_hdCuts;
    // A std::set again, so the same seed writes the same line.
    std::set<int>        m_hdSoundImports;

    // Record the box around one opening. Both are
    // private: an opening is recorded by the edit that made it, out of the
    // bytes that edit wrote, and never by a caller guessing at coordinates.
    //
    // hdMarkCutBox takes the raw world box of the opening and the axis its
    // normal runs along (0 x, 1 y, 2 z, -1 = pad every axis in the plane
    // way), and applies the padding.
    void hdMarkCutBox(long x0, long y0, long z0, long x1, long y1, long z1,
                      int normalAxis);
    // ... and this one reads the view window that now stands at that sector
    // and records its four corners. Used by both ways into makePortal - the
    // window it just built and the window that was already there - so the
    // box comes out of one derivation whichever way the run got here.
    void hdMarkCutFromWindow(int room, int sx, int sz, int targetRoom);

    // Appends one 32-byte view window to a room's portal list and moves
    // everything the insertion moves (see the body), then reparses. The
    // one splice behind addViewWindow and addSightWindow.
    bool appendPortalRecord(int room, const std::vector<uint8_t>& rec,
                            std::string& error);

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
          m_bones, m_frames, m_objects, m_sprites, m_texinfos, m_cameras,
          m_animtex;

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

    // Shared front end of cornerFloor/cornerCeiling: checks room, sector and
    // corner, hands out the file position of the 8-byte sector entry and the
    // sector's floordata chain (empty if the sector has none). false means
    // hands off - see there.
    bool cornerLookup(int room, int sx, int sz, int cx, int cz,
                      size_t& sectorAt, std::vector<Tr5FloorEntry>& chain) const;

    // The cross section of one sector at a corner named in WORLD x/z. The
    // neighbour of a sector may live in another room with another origin,
    // which is why the corner is not given room-locally.
    bool crossWorld(int room, int sx, int sz, long wx, long wz,
                    long& fy, long& cy) const;

    // Which wall of `room` a border sector sits on. ax/az is the step
    // OUTWARD (-1 low side, +1 high side); false means the sector is not on
    // the border ring at all, and then the two rooms meet vertically. A
    // corner sector belongs to two walls, and there the neighbour's own
    // position decides.
    bool portalWallAxis(int room, int other, int sx, int sz,
                        int& ax, int& az) const;

    // Breaks down a room blob. offset/size point at the blob in the main
    // block, index is the running room number.
    void parseRoom(size_t offset, size_t size, int index);

    bool readHeader(std::string& error);
    bool findItems(std::string& error);   // walks the main block
};

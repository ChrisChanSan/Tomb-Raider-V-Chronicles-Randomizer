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
#include "Tr5Level.h"

#include <cstdio>
#include <algorithm>
#include <cstring>

namespace
{
    // All multi-byte values in the TR format are little endian, just like
    // x86 - so a memcpy suffices. Still via helper functions, so every read
    // site gets the range check: a damaged size field in the header must
    // not run us past the end of the file.
    bool readI32(const std::vector<uint8_t>& b, size_t& pos, int32_t& out)
    {
        if (pos + 4 > b.size()) return false;
        std::memcpy(&out, b.data() + pos, 4);
        pos += 4;
        return true;
    }

    bool readI16(const std::vector<uint8_t>& b, size_t& pos, int16_t& out)
    {
        if (pos + 2 > b.size()) return false;
        std::memcpy(&out, b.data() + pos, 2);
        pos += 2;
        return true;
    }

    bool skip(const std::vector<uint8_t>& b, size_t& pos, int64_t n)
    {
        if (n < 0) return false;
        if (pos + static_cast<size_t>(n) > b.size()) return false;
        pos += static_cast<size_t>(n);
        return true;
    }
}

bool Tr5Level::load(const std::wstring& path, std::string& error)
{
    m_bytes.clear();
    m_mainOffset = m_mainSize = 0;
    // A fresh file has had nothing done to it yet. Without this a
    // second load() into the same object would inherit the first level's
    // touched rooms - and the GUI does reuse level objects.
    m_hdRooms.clear();
    m_hdDonors.clear();
    m_hdScales.clear();
    m_hdSoundImports.clear();
    m_hdCuts.clear();
    m_hdAddedItems.clear();

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
    {
        error = "cannot open level file";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); error = "level file is empty"; return false; }

    m_bytes.resize(static_cast<size_t>(n));
    size_t got = std::fread(m_bytes.data(), 1, m_bytes.size(), f);
    std::fclose(f);
    if (got != m_bytes.size()) { error = "level file not fully readable"; return false; }

    if (!readHeader(error)) return false;
    return findItems(error);
}

//----------------------------------------------------------------------------
// Walks the main block from the front until the item count is reached.
//
// There is no index and no marker where the items sit - every preceding
// section must be skipped exactly as the engine reads it. A single wrong
// byte shifts everything that follows, which is why the order below is
// taken 1:1 from LoadRooms/LoadObjects/LoadSprites/LoadCameras/
// LoadSoundEffects/LoadBoxes/LoadAnimatedTextures/LoadTextureInfos in
// specific/file.cpp, and the struct sizes come from a sizeof() test against
// the engine's headers, not from an estimate.
//----------------------------------------------------------------------------
namespace
{
    // Sizes, determined by the compiler against global/types.h.
    constexpr int SZ_ANIM = 40;   // ANIM_STRUCT
    constexpr int SZ_CHANGE = 6;    // CHANGE_STRUCT
    constexpr int SZ_RANGE = 8;    // RANGE_STRUCT
    constexpr int SZ_SPRITE = 16;   // PHDSPRITESTRUCT
    constexpr int SZ_OBJVEC = 16;   // OBJECT_VECTOR
    constexpr int SZ_SPOTCAM = 40;   // SPOTCAM
    constexpr int SZ_BOX = 8;    // BOX_INFO
    constexpr int SZ_TEXINFO = 40;   // PHDTEXTURESTRUCT

    // Bytes read per entry in the loops that read field by field.
    constexpr int SZ_OBJECTINFO = 20;   // slot4 + nmeshes2 + meshidx2 + bone4 + frame4 + anim2 + pad2
    constexpr int SZ_STATICINFO = 32;   // slot4 + mesh2 + 12 + 12 + flags2
    constexpr int SZ_SPRITESLOT = 8;    // slot4 + 2 shorts (both branches equally long)
}

bool Tr5Level::findItems(std::string& error)
{
    size_t p = m_mainOffset;
    const size_t end = m_mainOffset + m_mainSize;

    auto i32 = [&](int32_t& out) -> bool {
        if (p + 4 > end) return false;
        std::memcpy(&out, m_bytes.data() + p, 4);
        p += 4;
        return true;
    };
    // Skipping with a range check. count comes from the file and must never
    // be used unchecked as a length - a damaged or deliberately manipulated
    // number would otherwise point far past the end of the file.
    auto skipN = [&](int32_t count, int elemSize) -> bool {
        if (count < 0) return false;
        const uint64_t n = static_cast<uint64_t>(count) * static_cast<uint64_t>(elemSize);
        if (p + n > end) return false;
        p += static_cast<size_t>(n);
        return true;
    };

    int32_t n = 0;
    const char* where = "start";

#define STEP(what, expr) do { where = what; if (!(expr)) goto broken; } while (0)

    STEP("unused field", skipN(1, 4));
    STEP("room count", i32(n));
    m_roomCount = n;
    m_rooms.clear();
    m_roomMismatch = 0;
    for (int i = 0; i < m_roomCount; ++i)
    {
        int32_t roomSize = 0;
        STEP("room marker", skipN(1, 4));           // "XELA"
        STEP("room size", i32(roomSize));
        if (roomSize < 0 || p + static_cast<size_t>(roomSize) > end) { where = "room data"; goto broken; }
        parseRoom(p, static_cast<size_t>(roomSize), i);
        p += static_cast<size_t>(roomSize);
    }
    m_floorCountOffset = p;
    STEP("floordata", i32(n));
    m_floorCount = n;
    m_floorAt = p;
    STEP("floordata", skipN(n, 2));

    // Write the block map for the model transfer along the way. mark()
    // records where the count sits and where the data begins - AFTER i32
    // has read the count, p points exactly at the first data byte.
    auto mark = [&](Block& b, int32_t count) { b.countAt = p - 4; b.dataAt = p; b.count = count; };

    STEP("mesh data", i32(n));   mark(m_meshData, n);  STEP("mesh data", skipN(n, 2));
    STEP("mesh pointers", i32(n));  mark(m_meshPtrs, n);  STEP("mesh pointers", skipN(n, 4));
    STEP("animations", i32(n));  mark(m_anims, n);     STEP("animations", skipN(n, SZ_ANIM));
    STEP("state changes", i32(n)); mark(m_changes, n); STEP("state changes", skipN(n, SZ_CHANGE));
    STEP("ranges", i32(n));     mark(m_ranges, n);    STEP("ranges", skipN(n, SZ_RANGE));
    STEP("anim commands", i32(n)); mark(m_commands, n);  STEP("anim commands", skipN(n, 2));
    STEP("bones", i32(n));      mark(m_bones, n);     STEP("bones", skipN(n, 4));
    STEP("frames", i32(n));       mark(m_frames, n);    STEP("frames", skipN(n, 2));
    STEP("objects", i32(n));      mark(m_objects, n);   STEP("objects", skipN(n, SZ_OBJECTINFO));
    STEP("statics", i32(n));      STEP("statics", skipN(n, SZ_STATICINFO));

    STEP("SPR marker", skipN(1, 4));
    STEP("sprites", i32(n));      mark(m_sprites, n);   STEP("sprites", skipN(n, SZ_SPRITE));
    STEP("sprite slots", i32(n)); STEP("sprite slots", skipN(n, SZ_SPRITESLOT));

    STEP("cameras", i32(n));      mark(m_cameras, n);   STEP("cameras", skipN(n, SZ_OBJVEC));
    STEP("spotcams", i32(n));     STEP("spotcams", skipN(n, SZ_SPOTCAM));
    STEP("sound sources", i32(n)); STEP("sound sources", skipN(n, SZ_OBJVEC));

    {
        int32_t numBoxes = 0;
        STEP("boxes", i32(numBoxes));
        STEP("boxes", skipN(numBoxes, SZ_BOX));
        STEP("overlaps", i32(n)); STEP("overlaps", skipN(n, 2));
        // 2 passes of 4 + 1 zones each, all numBoxes shorts long.
        STEP("zones", skipN(numBoxes, 2 * 10));
    }

    STEP("animated textures", i32(n)); mark(m_animtex, n);
    STEP("animated textures", skipN(n, 2));
    // A single byte: nAnimUVRanges at the end of LoadAnimatedTextures. Easy
    // to miss, because it sits as a ReadChar between whole blocks - and
    // exactly this one byte shifted everything that follows by one position
    // on the first attempt.
    STEP("anim UV count", skipN(1, 1));
    STEP("TEX marker", skipN(1, 4));
    STEP("texture infos", i32(n)); mark(m_texinfos, n); STEP("texture infos", skipN(n, SZ_TEXINFO));

    // Arrived: the item count sits here.
    m_itemCountOffset = p;
    STEP("item count", i32(m_itemCount));
    STEP("item data", skipN(m_itemCount, 24));
    // The sound block follows. It may fail without the whole level counting
    // as unreadable - it is only needed when samples are to be taken over.
    findSound();
    return true;

#undef STEP

broken:
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "cannot walk the main block - failed at \"%s\" "
            "at offset %zu (n=%d, mainOffset=%zu, end=%zu, file=%zu). "
            "Does the file match the expected TR5 format?",
            where, p, n, m_mainOffset, m_mainOffset + m_mainSize, m_bytes.size());
        error = buf;
        m_itemCount = 0;
        m_itemCountOffset = 0;
        return false;
    }
}

//----------------------------------------------------------------------------
// Layout of a room blob, derived from the header offsets:
//
//   [0 .. 207]                 tr5_room_header, fixed 208 bytes
//   208 + start_sd             sector field, numX * numZ * 8 bytes
//   after                      uint16 portal count, then 32 bytes per portal
//   after                      uint16 statics count
//   208 + end_portal           statics field
//
// The portal list has no offset of its own in the header - it lies between
// sectors and statics. That yields an equation that MUST hold:
//
//   208 + end_portal == sector end + 2 + portal count * 32 + 2
//
// The last "+ 2" is the statics count, which end_portal already skips - it
// additionally sits in the header at +50. Without this one field the
// arithmetic came out exactly two bytes off, and in EVERY room; it only
// became visible when measured against a real blob. Confirmed on Andrea1
// with all 242 rooms.
//
// That is exactly what is checked here. If it does not hold, the assumption
// about the layout is wrong, and then the portals should rather stay empty
// than be wrong - an invented neighbor would mislead the search for a
// secluded room exactly where it has to be right.
//----------------------------------------------------------------------------
void Tr5Level::parseRoom(size_t offset, size_t size, int index)
{
    constexpr size_t HDR = 208;
    constexpr size_t OFS_START_SD = 8, OFS_END_PORTAL = 16;
    constexpr size_t OFS_X = 20, OFS_Z = 28, OFS_YBOTTOM = 32;
    constexpr size_t OFS_NUM_Z = 40, OFS_NUM_X = 42;
    constexpr size_t OFS_ALT = 76, OFS_FLAGS = 78;
    constexpr size_t SECTOR = 8, PORTAL = 32;

    Tr5Room r;
    r.index = index;
    if (size < HDR) { m_rooms.push_back(r); ++m_roomMismatch; return; }

    const uint8_t* b = m_bytes.data() + offset;
    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, b + o, 2); return v; };
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto i32r = [&](size_t o) { int32_t v; std::memcpy(&v, b + o, 4); return v; };

    r.originX = i32r(OFS_X);
    r.originZ = i32r(OFS_Z);
    r.yBottom = i32r(OFS_YBOTTOM);
    r.numZSectors = u16(OFS_NUM_Z);
    r.numXSectors = u16(OFS_NUM_X);
    const uint16_t alt = u16(OFS_ALT);
    r.altRoom = (alt == 0xFFFF) ? -1 : static_cast<int>(alt);
    r.flags = u16(OFS_FLAGS);

    const size_t sectorsAt = HDR + u32(OFS_START_SD);
    const size_t sectorsEnd = sectorsAt + static_cast<size_t>(r.numXSectors) * r.numZSectors * SECTOR;
    const size_t staticsAt = HDR + u32(OFS_END_PORTAL);

    if (sectorsEnd + 2 > size || staticsAt > size || staticsAt < sectorsEnd + 2)
    {
        m_rooms.push_back(r);
        ++m_roomMismatch;
        return;
    }

    const uint16_t numPortals = u16(sectorsEnd);
    if (sectorsEnd + 2 + static_cast<size_t>(numPortals) * PORTAL + 2 != staticsAt)
    {
        m_rooms.push_back(r);
        ++m_roomMismatch;
        return;
    }

    r.portalRooms.reserve(numPortals);
    for (uint16_t i = 0; i < numPortals; ++i)
        r.portalRooms.push_back(static_cast<int>(u16(sectorsEnd + 2 + static_cast<size_t>(i) * PORTAL)));

    // Only set here: up to this point the room can still be abandoned as
    // not parsed, and an offset onto an unclear room would be worse than
    // none.
    r.sectorsOffset = offset + sectorsAt;
    r.headerOffset = offset;

    m_rooms.push_back(r);
}

bool Tr5Level::getItem(int index, Tr5Item& out) const
{
    if (index < 0 || index >= m_itemCount) return false;
    const uint8_t* p = m_bytes.data() + m_itemCountOffset + 4 + static_cast<size_t>(index) * 24;
    std::memcpy(&out.objectNumber, p + 0, 2);
    std::memcpy(&out.roomNumber, p + 2, 2);
    std::memcpy(&out.x, p + 4, 4);
    std::memcpy(&out.y, p + 8, 4);
    std::memcpy(&out.z, p + 12, 4);
    std::memcpy(&out.yRot, p + 16, 2);
    std::memcpy(&out.shade, p + 18, 2);
    std::memcpy(&out.triggerFlags, p + 20, 2);
    std::memcpy(&out.flags, p + 22, 2);
    return true;
}

bool Tr5Level::addItem(const Tr5Item& in, std::string& error)
{
    if (m_itemCount >= MAX_ITEMS && !m_allowOverLimit)
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            "level already has %d items - the engine loads at most %d.",
            m_itemCount, MAX_ITEMS);
        error = buf;
        return false;
    }

    // Insert 24 bytes behind the last item. Everything after it in the main
    // block (AI infos, cutscene data) moves along; that is harmless because
    // everything there is read sequentially and nobody holds absolute
    // offsets.
    const size_t itemsEnd = m_itemCountOffset + 4 + static_cast<size_t>(m_itemCount) * 24;
    if (itemsEnd > m_bytes.size()) { error = "item area lies outside the file"; return false; }

    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(itemsEnd), 24, 0);
    // Behind the items lies the sound block - its offsets travel along.
    shiftSound(24, itemsEnd);

    ++m_itemCount;
    std::memcpy(m_bytes.data() + m_itemCountOffset, &m_itemCount, 4);

    // The main block has grown by 24 bytes. Its size sits twice in the
    // header before it - as size and as compressedSize, which are equal for
    // an uncompressed block. Both must move, or the engine reads the block
    // short and everything after tips over.
    m_mainSize += 24;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    const int index = m_itemCount - 1;

    if (!setItem(index, in)) return false;

    // The run's own item, for the sidecar's `item` lines. Nothing is ever
    // inserted before it or removed, so the index it gets here is the one
    // it has in the saved file.
    m_hdAddedItems.insert(index);
    return true;
}

bool Tr5Level::setItem(int index, const Tr5Item& in)
{
    if (index < 0 || index >= m_itemCount) return false;
    uint8_t* p = m_bytes.data() + m_itemCountOffset + 4 + static_cast<size_t>(index) * 24;
    std::memcpy(p + 0, &in.objectNumber, 2);
    std::memcpy(p + 2, &in.roomNumber, 2);
    std::memcpy(p + 4, &in.x, 4);
    std::memcpy(p + 8, &in.y, 4);
    std::memcpy(p + 12, &in.z, 4);
    std::memcpy(p + 16, &in.yRot, 2);
    std::memcpy(p + 18, &in.shade, 2);
    std::memcpy(p + 20, &in.triggerFlags, 2);
    std::memcpy(p + 22, &in.flags, 2);
    return true;
}

bool Tr5Level::addAiObject(const Tr5AiObject& in, std::string& error)
{
    // The AI block lies right behind the items: int32 count, then AIOBJECT
    // entries of 24 bytes each (LoadAIInfo, specific/file.cpp).
    // Insertion at the block end, the same mechanics as addItem: everything
    // behind it in the main block (cutscene data, sound) moves along.
    const size_t aiCountAt = m_itemCountOffset + 4 + static_cast<size_t>(m_itemCount) * 24;
    if (aiCountAt + 4 > m_bytes.size()) { error = "AI block lies outside the file"; return false; }

    int32_t nAi = 0;
    std::memcpy(&nAi, m_bytes.data() + aiCountAt, 4);
    if (nAi < 0 || aiCountAt + 4 + static_cast<size_t>(nAi) * 24 > m_bytes.size())
    { error = "AI block implausible"; return false; }

    const size_t insertAt = aiCountAt + 4 + static_cast<size_t>(nAi) * 24;
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(insertAt), 24, 0);
    shiftSound(24, insertAt);

    ++nAi;
    std::memcpy(m_bytes.data() + aiCountAt, &nAi, 4);

    uint8_t* p = m_bytes.data() + insertAt;
    std::memcpy(p + 0, &in.objectNumber, 2);
    std::memcpy(p + 2, &in.roomNumber, 2);
    std::memcpy(p + 4, &in.x, 4);
    std::memcpy(p + 8, &in.y, 4);
    std::memcpy(p + 12, &in.z, 4);
    std::memcpy(p + 16, &in.ocb, 2);
    std::memcpy(p + 18, &in.flags, 2);
    std::memcpy(p + 20, &in.yRot, 2);
    std::memcpy(p + 22, &in.boxNumber, 2);

    m_mainSize += 24;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    return true;
}

//----------------------------------------------------------------------------
// Floordata
//
// The format is fully derived in "Preparation\Trigger\Floordata and
// Triggers.md", with line references into the engine. Here only what is
// needed to read the code along.
//----------------------------------------------------------------------------
namespace
{
    // global/types.h, enum floor_types
    enum { FN_DOOR = 1, FN_TILT = 2, FN_ROOF = 3, FN_TRIGGER = 4,
           FN_LAVA = 5, FN_CLIMB = 6, FN_MONKEY = 19, FN_TRIGTRIGGER = 20 };

    // SWITCH (2), KEY (3) and PICKUP (4) push a reference word in front of
    // the command list: the item that must be operated resp. picked up. The
    // engine reads all three the same way - _TestTriggers in game/control.cpp,
    // each time "value = *(data++) & 0x3FF". The word is NOT a
    // command.
    // Defined in the Tr5Floor namespace (Tr5Level.h) rather than here, so
    // LevelInfo.cpp can walk a chain with exactly this knowledge instead of
    // its own copy. A second copy of a bit layout is a second chance to get
    // it wrong.
    using Tr5Floor::hasRefWord;
    using Tr5Floor::extraWords;
    using Tr5Floor::cmdType;
}

namespace Tr5Floor
{
    bool hasRefWord(int kind) { return kind == 2 || kind == 3 || kind == 4; }

    // Extra words of an entry that is not a trigger: exactly the cases
    // where GetHeight (control.cpp) does a "data++".
    int extraWords(int fn)
    {
        return (fn == FN_DOOR || fn == FN_TILT || fn == FN_ROOF || (fn >= 7 && fn <= 18)) ? 1 : 0;
    }

    // Command kind: 4 bits, not 5. Assigned twice independently - "switch
    // ((trigger & 0x3FFF) >> 10)" in _TestTriggers (control.cpp) and
    // "(trigger & 0x3C00) != (TO_OBJECT << 10)" in GetHeight. Bit
    // 15 is the end bit, bit 14 the engine never evaluates.
    int cmdType(uint16_t c) { return (c & 0x3C00) >> 10; }
}

int Tr5Level::sectorIndex(int room, int sx, int sz) const
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return -1;
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (r.sectorsOffset == 0) return -1;
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return -1;

    // The sector field is x-major: x * numZSectors + z.
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    if (at + 2 > m_bytes.size()) return -1;
    uint16_t v;
    std::memcpy(&v, m_bytes.data() + at, 2);
    return static_cast<int>(v);
}

void Tr5Level::setSectorIndex(const Tr5Room& r, int sx, int sz, uint16_t index)
{
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    std::memcpy(m_bytes.data() + at, &index, 2);
}

bool Tr5Level::readChain(int index, std::vector<Tr5FloorEntry>& out) const
{
    out.clear();
    if (index <= 0) return index == 0;              // 0 = no floordata

    auto word = [&](size_t i) {
        uint16_t v;
        std::memcpy(&v, m_bytes.data() + m_floorAt + i * 2, 2);
        return v;
    };
    const size_t limit = static_cast<size_t>(m_floorCount);
    size_t i = static_cast<size_t>(index);

    for (;;)
    {
        if (i >= limit) return false;

        Tr5FloorEntry e;
        const uint16_t w = word(i++);
        e.function = w & 0x1F;
        e.kind = (w >> 8) & 0x3F;
        e.words.push_back(w);
        const bool last = (w & 0x8000) != 0;

        if (e.function == FN_TRIGGER)
        {
            if (i >= limit) return false;
            e.words.push_back(word(i++));         // timer, one-shot, mask

            bool more = true;
            if (hasRefWord(e.kind))
            {
                if (i >= limit) return false;
                const uint16_t r = word(i++);
                e.words.push_back(r);
                more = (r & 0x8000) == 0;
            }
            while (more)
            {
                if (i >= limit) return false;
                const uint16_t c = word(i++);
                e.words.push_back(c);
                const int act = cmdType(c);
                if (act == 1 /*camera*/ || act == 12 /*flyby*/)
                {
                    if (i >= limit) return false;
                    e.words.push_back(word(i++));

                    // Known divergence from the engine, deliberately left in
                    // place. For a camera the engine overwrites its loop
                    // variable with this extra word - "case TO_CAMERA: trigger
                    // = *data++" in _TestTriggers - and the loop below it
                    // then tests THAT word's end bit.
                    // The command word is tested instead, three lines down.
                    //
                    // In the 16 vanilla levels the two agree everywhere the
                    // result is used: no caller of readChain reads camera or
                    // flyby commands, and the old level exports parsed it the
                    // same way, which is why switching to this reader produced
                    // byte-identical output. Aligning it with the engine is
                    // therefore a change with no measurable upside today and a
                    // real downside: readChain also feeds moveTrigger and
                    // copyTrigger, so a different chain length there rewrites
                    // floordata. If anything ever needs camera or flyby
                    // commands, fix it HERE and re-run the byte comparison.
                }
                if (c & 0x8000) break;
            }
        }
        else
        {
            for (int k = 0; k < extraWords(e.function); ++k)
            {
                if (i >= limit) return false;
                e.words.push_back(word(i++));
            }
        }

        out.push_back(e);
        if (last) break;
    }
    return true;
}

long Tr5Level::musicTriggers(const std::map<int, int>* mapping, std::set<int>* found)
{
    // The walk deliberately leans on readChain: it knows every word
    // distribution (the reference word of the switch/key/pickup kinds, the
    // extra words of camera and flyby, the extraWords of the other
    // functions). The file position of a word follows exactly from chain
    // start + summed entry lengths, because readChain reads strictly
    // linearly.
    std::set<int> chains;
    std::vector<Tr5FloorEntry> entries;
    long count = 0;

    for (size_t rm = 0; rm < m_rooms.size(); ++rm)
    {
        const Tr5Room& r = m_rooms[rm];

        for (int sx = 0; sx < r.numXSectors; ++sx)
        {
            for (int sz = 0; sz < r.numZSectors; ++sz)
            {
                const int start = sectorIndex(static_cast<int>(rm), sx, sz);
                if (start <= 0 || !chains.insert(start).second) continue;
                if (!readChain(start, entries)) continue;     // unreadable: hands off

                size_t pos = static_cast<size_t>(start);
                for (const Tr5FloorEntry& e : entries)
                {
                    if (e.function == FN_TRIGGER)
                    {
                        // The action words begin behind head + setup (+ reference word).
                        size_t k = hasRefWord(e.kind) ? 3 : 2;
                        for (; k < e.words.size(); ++k)
                        {
                            const uint16_t c = e.words[k];
                            const int act = cmdType(c);

                            if (act == 1 || act == 12)      // camera/flyby: extra word
                            {
                                ++k;
                                continue;
                            }

                            if (act != 8)                   // TO_CD
                                continue;

                            const int track = c & 0x3FF;

                            if (found)
                            {
                                found->insert(track);
                                ++count;
                            }

                            if (mapping)
                            {
                                const auto it = mapping->find(track);
                                if (it != mapping->end() && it->second != track)
                                {
                                    const uint16_t nw = static_cast<uint16_t>((c & 0xFC00) | (it->second & 0x3FF));
                                    std::memcpy(m_bytes.data() + m_floorAt + (pos + k) * 2, &nw, 2);
                                    ++count;
                                }
                            }
                        }
                    }

                    pos += e.words.size();
                }
            }
        }
    }

    return count;
}

int Tr5Level::floorSlope(int room, int sx, int sz, SlopeInfo& si, int& baseY) const
{
    si = SlopeInfo();
    baseY = 0;

    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return -1;
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return -1;

    // The sector's floor byte: signed byte at +5 of the 8-byte entry
    // (FLOOR_INFO in global/types.h: index, fx/box/stopper, pit_room,
    // floor, sky_room, ceiling). GetHeight starts with exactly this value
    // << 8.
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    baseY = static_cast<int8_t>(m_bytes[at + 5]) << 8;

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0) return -1;
    if (idx == 0) return 0;                         // no floordata = flat

    std::vector<Tr5FloorEntry> chain;
    if (!readChain(idx, chain)) return -1;          // unreadable: hands off

    for (const Tr5FloorEntry& e : chain)
    {
        const bool isFloor = (e.function == FN_TILT || e.function == 7 ||
                            e.function == 8 || (e.function >= 11 && e.function <= 14));
        if (!isFloor) continue;
        if (e.words.size() < 2) return -1;        // entry without a data word: broken
        si.typeWord = e.words[0];
        si.dataWord = e.words[1];
        return 1;
    }
    return 0;                                       // only portals/triggers/...: flat
}

namespace
{
    // Floor slopes: TILT, SPLIT1/2, NOCOLF1T/1B/2T/2B - exactly the set
    // GetCeiling skips over (control.cpp).
    bool isFloorSlope(int fn)
    {
        return fn == FN_TILT || fn == 7 || fn == 8 || (fn >= 11 && fn <= 14);
    }

    // The two families of a split: family 1 (SPLIT1/3, NOCOLF1*/NOCOLC1*)
    // splits along one diagonal, family 2 along the other.
    bool isFamily1(int fn)
    {
        return fn == 7 || fn == 11 || fn == 12 ||        // floor
               fn == 9 || fn == 15 || fn == 16;          // ceiling
    }

    // The height adjustment of one split triangle: 5 signed bits in the type
    // word, sign-extended the way the engine does it (GetHeight and GetCeiling
    // in control.cpp).
    int splitHadj(uint16_t typeWord, int shift)
    {
        int hadj = (typeWord >> shift) & 0x1F;
        if (hadj & 0x10) hadj |= ~0x1F;
        return hadj;
    }
}

bool Tr5Level::cornerLookup(int room, int sx, int sz, int cx, int cz,
                            size_t& sectorAt, std::vector<Tr5FloorEntry>& chain) const
{
    sectorAt = 0;
    chain.clear();

    if (cx != 0 && cx != 1024) return false;
    if (cz != 0 && cz != 1024) return false;
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return false;

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (r.sectorsOffset == 0) return false;
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return false;

    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    if (at + 8 > m_bytes.size()) return false;
    sectorAt = at;

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0) return false;
    if (idx == 0) return true;                      // no floordata: flat sector
    return readChain(idx, chain);                   // unreadable: hands off
}

//----------------------------------------------------------------------------
// Corner-exact floor and ceiling height - the one substitution in the
// engine's formula.
//
// GetHeight and GetCeiling (control.cpp)
// are linear in the offset within the sector, but they write that offset as
//     (v & 1023)   and   ((-1 - v) & 1023)
// and both of these hit 1023 where the sector edge lies at 1024: the engine
// can only ever be asked about a point INSIDE the sector, never about the
// corner. Evaluating the SAME linear function exactly on the edge means one
// substitution and nothing else:
//     (v & 1023)        ->  cv              with cv in {0, 1024}
//     ((-1 - v) & 1023) ->  1024 - cv
// Check on the substitution: every product is now a multiple of 1024, so
// the engine's ">> 2" is an exact division and every corner height is an
// exact multiple of 256. Measured over all 16 vanilla levels: it is.
//
// One thing the corner cannot inherit from the engine is the split's
// triangle test - "dx > 1024 - dz" would sit exactly on the diagonal at two
// of the four corners. The test therefore runs on a probe point one unit
// toward the sector centre (1 resp. 1023), which is the triangle the corner
// belongs to. It does not matter which one is picked: measured over all 16
// vanilla levels, both triangles of a split deliver the SAME height at their
// two shared corners (18156 of 18156), so the corner height is unique.
//----------------------------------------------------------------------------
bool Tr5Level::cornerFloor(int room, int sx, int sz, int cx, int cz, long& y) const
{
    y = 0;

    size_t at = 0;
    std::vector<Tr5FloorEntry> chain;
    if (!cornerLookup(room, sx, sz, cx, cz, at, chain)) return false;

    // The sector's floor byte, signed, at +5 of the 8-byte entry. GetHeight
    // starts with exactly this value << 8; -127 is NO_HEIGHT, the wall.
    const int floorByte = static_cast<int8_t>(m_bytes[at + 5]);
    if (floorByte == -127) return false;
    long h = static_cast<long>(floorByte) * 256;

    const int px = (cx == 0) ? 1 : 1023;            // probe for the triangle test
    const int pz = (cz == 0) ? 1 : 1023;

    // GetHeight walks the WHOLE chain and adds up every slope entry it meets
    // (the "height +=" inside its do-while). Ported as it stands, even
    // though no vanilla chain carries more than one floor slope.
    for (const Tr5FloorEntry& e : chain)
    {
        int xoff = 0, yoff = 0;

        if (e.function == FN_TILT)                              // as GetHeight
        {
            if (e.words.size() < 2) return false;               // entry without data word
            xoff = static_cast<int16_t>(e.words[1]) >> 8;
            yoff = static_cast<int8_t>(e.words[1] & 0xFF);
        }
        else if (e.function == 7 || e.function == 8 ||           // SPLIT1/2
                 (e.function >= 11 && e.function <= 14))         // NOCOLF*
        {                                                        // as GetHeight
            if (e.words.size() < 2) return false;
            const uint16_t d = e.words[1];
            const int t0 = d & 0xF, t1 = (d >> 4) & 0xF;
            const int t2 = (d >> 8) & 0xF, t3 = (d >> 12) & 0xF;
            const bool fam1 = isFamily1(e.function);
            const bool firstHalf = fam1 ? (px > 1024 - pz) : (px > pz);

            if (firstHalf)
            {
                h += splitHadj(e.words[0], 5) * 256;
                xoff = t3 - t0;
                yoff = fam1 ? (t3 - t2) : (t0 - t1);
            }
            else
            {
                h += splitHadj(e.words[0], 10) * 256;
                xoff = t2 - t1;
                yoff = fam1 ? (t0 - t1) : (t3 - t2);
            }
        }
        else
            continue;                                            // portal, trigger, climb, ...

        // GetHeight's slope formula (control.cpp), with the substitution above.
        if (xoff < 0) h -= (xoff * cz) >> 2;
        else          h += (xoff * (1024 - cz)) >> 2;
        if (yoff < 0) h -= (yoff * cx) >> 2;
        else          h += (yoff * (1024 - cx)) >> 2;
    }

    y = h;
    return true;
}

bool Tr5Level::cornerCeiling(int room, int sx, int sz, int cx, int cz, long& y) const
{
    y = 0;

    size_t at = 0;
    std::vector<Tr5FloorEntry> chain;
    if (!cornerLookup(room, sx, sz, cx, cz, at, chain)) return false;

    // The ceiling byte sits at +7 of the sector entry; -127 is the wall here
    // as well (GetCeiling:1354).
    const int ceilByte = static_cast<int8_t>(m_bytes[at + 7]);
    if (ceilByte == -127) return false;
    long h = static_cast<long>(ceilByte) * 256;

    // Here GetCeiling parts company with GetHeight: it does NOT walk the
    // chain. It reads the FIRST entry, and only if that is a floor slope does
    // it step over it to the second (the "ended"
    // flag). A ceiling slope any further back in the chain the engine never
    // sees - reproduced on purpose, because the drawn geometry follows the
    // engine, not the intention.
    const Tr5FloorEntry* e = nullptr;
    if (!chain.empty())
    {
        e = &chain[0];
        if (isFloorSlope(e->function))
        {
            const bool last = (e->words[0] & 0x8000) != 0;
            if (last || chain.size() < 2) e = nullptr;
            else                          e = &chain[1];
        }
    }

    const int px = (cx == 0) ? 1 : 1023;
    const int pz = (cz == 0) ? 1 : 1023;

    int h1 = 0, h2 = 0;
    if (e && e->function == FN_ROOF)                             // as GetCeiling
    {
        if (e->words.size() < 2) return false;
        h1 = static_cast<int16_t>(e->words[1]) >> 8;
        h2 = static_cast<int8_t>(e->words[1] & 0xFF);
    }
    else if (e && (e->function == 9 || e->function == 10 ||       // SPLIT3/4
                   (e->function >= 15 && e->function <= 18)))     // NOCOLC*
    {                                                            // as GetCeiling
        if (e->words.size() < 2) return false;
        const uint16_t d = e->words[1];
        // The ceiling nibbles go in NEGATED (GetCeiling).
        const int t0 = -(d & 0xF), t1 = -((d >> 4) & 0xF);
        const int t2 = -((d >> 8) & 0xF), t3 = -((d >> 12) & 0xF);
        const bool fam1 = isFamily1(e->function);
        const bool firstHalf = fam1 ? (px <= 1024 - pz) : (px <= pz);

        // Mind the shifts: the ceiling takes the hadj fields the other way
        // round from the floor - 10 for the first triangle, 5 for the second.
        if (firstHalf)
        {
            h += 256 * splitHadj(e->words[0], 10);
            h1 = t2 - t1;
            h2 = fam1 ? (t3 - t2) : (t0 - t1);
        }
        else
        {
            h += 256 * splitHadj(e->words[0], 5);
            h1 = t3 - t0;
            h2 = fam1 ? (t0 - t1) : (t3 - t2);
        }
    }
    // Anything else - portal, trigger, or no entry at all - leaves h1 and h2
    // at 0, which is what the engine's fall-through does.

    // As GetCeiling in control.cpp. The asymmetry is the engine's: for h1 the
    // NEGATIVE case takes the offset itself, for h2 the POSITIVE one does.
    if (h1 < 0) h += (h1 * cz) >> 2;
    else        h -= (h1 * (1024 - cz)) >> 2;
    if (h2 < 0) h += (h2 * (1024 - cx)) >> 2;
    else        h -= (h2 * cx) >> 2;

    y = h;
    return true;
}

const char* Tr5Level::canAppendTrigger(const std::vector<Tr5FloorEntry>& chain)
{
    // _TestTriggers receives a pointer from GetHeight and from there skips
    // EXACTLY four entry kinds - lava, climb, monkey, TrigTrigger
    // (control.cpp) - and then unconditionally reads a trigger
    // head. GetHeight sets the pointer onto the FIRST entry from
    // {trigger, climb, monkey, TrigTrigger}, lava even
    // unconditionally anew.
    //
    // If one of these five already sits in the chain, the pointer points
    // there - and everything between it and our appended trigger is
    // misread as a trigger head. Portals and slopes alone are harmless:
    // GetHeight eats them while walking without setting the pointer.
    for (const Tr5FloorEntry& e : chain)
    {
        if (e.function == FN_TRIGGER)     return "sector already has a trigger";
        if (e.function == FN_LAVA)        return "sector has a lava entry";
        if (e.function == FN_CLIMB)       return "sector has a climb entry";
        if (e.function == FN_MONKEY)      return "sector has a monkey entry";
        if (e.function == FN_TRIGTRIGGER) return "sector has a TrigTrigger entry";
    }
    return nullptr;
}

bool Tr5Level::appendChain(const std::vector<Tr5FloorEntry>& chain, uint16_t& outIndex,
                           std::string& error)
{
    if (chain.empty()) { outIndex = 0; return true; }   // sector without floordata

    std::vector<uint16_t> words;
    for (size_t n = 0; n < chain.size(); ++n)
    {
        std::vector<uint16_t> w = chain[n].words;
        if (w.empty()) { error = "empty floordata entry"; return false; }
        w[0] &= 0x7FFF;                              // end bit off for now
        if (n + 1 == chain.size()) w[0] |= 0x8000;   // and set only on the last
        words.insert(words.end(), w.begin(), w.end());
    }

    // FLOOR_INFO.index is a ushort (global/types.h). Whatever lies beyond
    // 65535 no sector can address anymore.
    const size_t newIndex = static_cast<size_t>(m_floorCount);
    if (newIndex + words.size() > 65535)
    {
        error = "floordata would exceed 65535 shorts - FLOOR_INFO.index is ushort";
        return false;
    }

    std::vector<uint8_t> raw;
    raw.reserve(words.size() * 2);
    for (uint16_t w : words) { raw.push_back(w & 0xFF); raw.push_back(w >> 8); }

    const size_t at = m_floorAt + static_cast<size_t>(m_floorCount) * 2;
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(at), raw.begin(), raw.end());

    m_floorCount += static_cast<int32_t>(words.size());
    std::memcpy(m_bytes.data() + m_floorCountOffset, &m_floorCount, 4);

    // The main block has grown; its size sits twice in front of it.
    m_mainSize += raw.size();
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    // And here is the pitfall: the items lie BEHIND the floordata. Whoever
    // grows the block without adjusting this offset writes into foreign
    // data on the next setItem/addItem. The rooms' sector fields lie before
    // it and stay where they are.
    m_itemCountOffset += raw.size();
    shiftSound(raw.size(), at);      // the sound block lies even further back

    // And the BLOCK MAP along with it. Mesh, anim, frame and object blocks
    // also lie behind the floordata inside the main block; without this
    // correction they point off after the first appended trigger - only the
    // anim check reads these blocks after a floordata append, and reports
    // Lara's animations as "missing" when the offset is wrong.
    auto adjustBlock = [&](Block& b) {
        if (b.countAt >= at) b.countAt += raw.size();
        if (b.dataAt  >= at) b.dataAt  += raw.size();
    };
    adjustBlock(m_meshData);  adjustBlock(m_meshPtrs);
    adjustBlock(m_anims);     adjustBlock(m_changes);
    adjustBlock(m_ranges);    adjustBlock(m_commands);
    adjustBlock(m_bones);     adjustBlock(m_frames);
    adjustBlock(m_objects);   adjustBlock(m_sprites);
    adjustBlock(m_cameras);
    adjustBlock(m_texinfos);

    outIndex = static_cast<uint16_t>(newIndex);
    return true;
}

bool Tr5Level::moveTrigger(int fromRoom, int fromSx, int fromSz,
                           int toRoom, int toSx, int toSz, std::string& error)
{
    char buf[256];

    if (fromRoom == toRoom && fromSx == toSx && fromSz == toSz)
    { error = "source and target are the same sector"; return false; }

    const int fromIdx = sectorIndex(fromRoom, fromSx, fromSz);
    const int toIdx = sectorIndex(toRoom, toSx, toSz);
    if (fromIdx < 0) { error = "source sector does not exist"; return false; }
    if (toIdx < 0) { error = "target sector does not exist"; return false; }

    std::vector<Tr5FloorEntry> from, to;
    if (!readChain(fromIdx, from)) { error = "source chain runs out of the block"; return false; }
    if (!readChain(toIdx, to)) { error = "target chain runs out of the block"; return false; }

    // Expect exactly one trigger entry. Two would be ambiguous, none a
    // usage error - rather abort both than guess.
    size_t found = 0, at = 0;
    for (size_t k = 0; k < from.size(); ++k)
        if (from[k].function == FN_TRIGGER) { ++found; at = k; }
    if (found != 1)
    {
        std::snprintf(buf, sizeof(buf),
            "source sector %d,%d,%d has %zu trigger entries, expected exactly one",
            fromRoom, fromSx, fromSz, found);
        error = buf;
        return false;
    }

    if (const char* why = canAppendTrigger(to))
    {
        std::snprintf(buf, sizeof(buf), "target sector %d,%d,%d unsuitable: %s",
                      toRoom, toSx, toSz, why);
        error = buf;
        return false;
    }

    const Tr5FloorEntry trig = from[at];
    from.erase(from.begin() + static_cast<ptrdiff_t>(at));
    to.push_back(trig);

    // Order with intent: first append both chains, then repoint both
    // sectors. Otherwise a sector would momentarily point at a chain that
    // is not yet fully written.
    uint16_t newFrom = 0, newTo = 0;
    if (!appendChain(from, newFrom, error)) return false;
    if (!appendChain(to, newTo, error)) return false;

    setSectorIndex(m_rooms[static_cast<size_t>(fromRoom)], fromSx, fromSz, newFrom);
    setSectorIndex(m_rooms[static_cast<size_t>(toRoom)], toSx, toSz, newTo);
    return true;
}

bool Tr5Level::copyTrigger(int fromRoom, int fromSx, int fromSz,
                              int toRoom, int toSx, int toSz, std::string& error)
{
    char buf[256];

    if (fromRoom == toRoom && fromSx == toSx && fromSz == toSz)
    { error = "source and target are the same sector"; return false; }

    const int fromIdx = sectorIndex(fromRoom, fromSx, fromSz);
    const int toIdx = sectorIndex(toRoom, toSx, toSz);
    if (fromIdx < 0) { error = "source sector does not exist"; return false; }
    if (toIdx < 0) { error = "target sector does not exist"; return false; }

    std::vector<Tr5FloorEntry> from, to;
    if (!readChain(fromIdx, from)) { error = "source chain runs out of the block"; return false; }
    if (!readChain(toIdx, to)) { error = "target chain runs out of the block"; return false; }

    // As with moveTrigger: exactly one trigger entry. Two would be
    // ambiguous, none a usage error.
    size_t found = 0, at = 0;
    for (size_t k = 0; k < from.size(); ++k)
        if (from[k].function == FN_TRIGGER) { ++found; at = k; }
    if (found != 1)
    {
        std::snprintf(buf, sizeof(buf),
            "source sector %d,%d,%d has %zu trigger entries, expected exactly one",
            fromRoom, fromSx, fromSz, found);
        error = buf;
        return false;
    }

    if (const char* why = canAppendTrigger(to))
    {
        std::snprintf(buf, sizeof(buf), "target sector %d,%d,%d unsuitable: %s",
                      toRoom, toSx, toSz, why);
        error = buf;
        return false;
    }

    // Only the target chain grows. The source stays word for word as it
    // was - that is the whole difference from moveTrigger.
    to.push_back(from[at]);

    uint16_t newTo = 0;
    if (!appendChain(to, newTo, error)) return false;

    setSectorIndex(m_rooms[static_cast<size_t>(toRoom)], toSx, toSz, newTo);
    return true;
}

//----------------------------------------------------------------------------
// Take a single object command out of a trigger.
//
// Occasion: rich1 room 71. Its one-shot trigger names a music track, the
// sword statue and two doors. Door 129 is REVERSE-ACTIVATED - its item
// flags carry all five code bits, so InitialiseItem (items.cpp) strips
// them, sets IFL_REVERSE and activates the item. TriggerActive then reads
// backwards: incomplete bits mean OPEN, and the trigger, by completing
// them, SHUTS the door behind Lara. Adding a second trigger cannot undo
// that - it would complete the bits as well, and the one-shot has by then
// hung IFL_INVISIBLE on the door, which makes the engine skip every plain
// trigger on it (control.cpp, top of the TO_OBJECT branch).
//
// So the pointer has to go, and only the pointer: the music and the statue
// belong to the scene.
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// Rewrite a whole trigger in place.
//
// Occasion: andy1's heart (item 8, type 172). Its PICKUP trigger opens an
// unneeded door and starts a cutscene that can teleport Lara into a
// softlock. On top of that the randomizer pins the heart where it lies,
// because the trigger does more than play music (Randomizer.cpp,
// findTriggerPinnedItems) - so the curated placement range for type 172
// never comes into play.
//
// Taking only the commands out would not be enough: as a PICKUP trigger it
// keeps its reference word, and that word IS the pointer at the heart. The
// clean fix is to make it a plain TRIGGER instead. Then there is no
// reference word at all, and findTriggerPinnedItems skips the entry
// outright.
//----------------------------------------------------------------------------
bool Tr5Level::setTrigger(int room, int sx, int sz, int kind, int timer,
                          bool oneShot, int mask,
                          const std::vector<Tr5TriggerCmd>& cmds,
                          std::string& error, int refItem)
{
    char buf[256];

    if (cmds.empty())
    {
        error = "a trigger needs at least one command - the engine's command loop "
                "is a do-while and would read past the entry";
        return false;
    }

    for (const Tr5TriggerCmd& c : cmds)
        if (c.type == 1 || c.type == 12)
        {
            error = "camera and flyby commands need an extra word - not provided for here";
            return false;
        }

    if (hasRefWord(kind) && refItem < 0)
    {
        error = "this trigger kind needs a reference word (refItem)";
        return false;
    }
    if (!hasRefWord(kind) && refItem >= 0)
    {
        error = "this trigger kind knows no reference word - refItem would be pointless";
        return false;
    }

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0)
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d does not exist", room, sx, sz);
        error = buf;
        return false;
    }

    std::vector<Tr5FloorEntry> chain;
    if (!readChain(idx, chain)) { error = "chain runs out of the block"; return false; }

    bool hit = false;

    for (Tr5FloorEntry& e : chain)
    {
        if (e.function != FN_TRIGGER) continue;

        e.kind = kind;
        e.words.clear();
        // Word 0: function in bits 0-4, kind in bits 8-13. Bit 15 is
        // assigned by the chain builder.
        e.words.push_back(static_cast<uint16_t>((kind << 8) | FN_TRIGGER));
        e.words.push_back(static_cast<uint16_t>((timer & 0xFF) |
                                                  (oneShot ? 0x100 : 0) |
                                                  ((mask & 0x1F) << 9)));
        if (hasRefWord(kind))
            e.words.push_back(static_cast<uint16_t>(refItem & 0x3FF));

        for (size_t i = 0; i < cmds.size(); ++i)
        {
            uint16_t w = static_cast<uint16_t>(((cmds[i].type & 0xF) << 10) | (cmds[i].param & 0x3FF));
            if (i + 1 == cmds.size()) w |= 0x8000;
            e.words.push_back(w);
        }

        hit = true;
    }

    if (!hit)
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d carries no trigger", room, sx, sz);
        error = buf;
        return false;
    }

    uint16_t fresh = 0;
    if (!appendChain(chain, fresh, error)) return false;
    setSectorIndex(m_rooms[static_cast<size_t>(room)], sx, sz, fresh);
    return true;
}

bool Tr5Level::editTriggerObject(int room, int sx, int sz, int item,
                                 int newType, int newParam, std::string& error)
{
    char buf[256];

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0)
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d does not exist", room, sx, sz);
        error = buf;
        return false;
    }

    std::vector<Tr5FloorEntry> chain;
    if (!readChain(idx, chain)) { error = "chain runs out of the block"; return false; }

    bool hit = false;

    for (Tr5FloorEntry& e : chain)
    {
        if (e.function != FN_TRIGGER) continue;

        // Words 0 and 1 are the head, then possibly the reference word of
        // a SWITCH/KEY/PICKUP trigger. Only what follows are commands.
        size_t at = 2;
        if (hasRefWord(e.kind)) ++at;
        if (at >= e.words.size()) continue;

        std::vector<uint16_t> keep(e.words.begin(), e.words.begin() + static_cast<ptrdiff_t>(at));
        bool dropped = false;

        while (at < e.words.size())
        {
            const uint16_t w = e.words[at];
            const int type = (w >> 10) & 0xF;
            const int param = w & 0x3FF;

            // Camera and flyby carry a second word that belongs to them.
            const size_t width = (type == 1 || type == 12) ? 2 : 1;
            const bool last = (w & 0x8000) != 0;

            if (type == 0 && param == item)
            {
                dropped = true;

                // A replacement keeps the end bit exactly where it was, so a
                // trigger whose ONLY command this is survives the surgery.
                if (newType >= 0)
                    keep.push_back(static_cast<uint16_t>(
                        (w & 0x8000) | ((newType & 0xF) << 10) | (newParam & 0x3FF)));
            }
            else
                for (size_t k = 0; k < width && at + k < e.words.size(); ++k)
                    keep.push_back(e.words[at + k]);

            at += width;
            if (last) break;
        }

        if (!dropped) continue;

        size_t headWords = hasRefWord(e.kind) ? 3u : 2u;
        if (keep.size() <= headWords)
        {
            error = "that was the trigger's only command - dropping it leaves the trigger pointless; pass a replacement instead";
            return false;
        }

        // The end bit sat on the word that is gone or moved - set it
        // afresh on the new last command and clear it everywhere else.
        for (size_t k = headWords; k < keep.size(); ++k) keep[k] &= 0x7FFF;
        keep.back() |= 0x8000;

        e.words = keep;
        hit = true;
    }

    if (!hit)
    {
        std::snprintf(buf, sizeof(buf),
                      "sector %d,%d,%d has no trigger command for item %d",
                      room, sx, sz, item);
        error = buf;
        return false;
    }

    uint16_t fresh = 0;
    if (!appendChain(chain, fresh, error)) return false;
    setSectorIndex(m_rooms[static_cast<size_t>(room)], sx, sz, fresh);
    return true;
}

bool Tr5Level::setTriggerOneShot(int room, int kind, int slot,
                                 std::vector<Tr5OneShotHit>& hits,
                                 std::string& error)
{
    hits.clear();
    char buf[256];

    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    {
        std::snprintf(buf, sizeof(buf), "room %d does not exist", room);
        error = buf;
        return false;
    }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];

    // The floordata index of each setup word to set, in step with hits.
    std::vector<size_t> words;
    std::set<int> chains;
    std::vector<Tr5FloorEntry> entries;

    for (int sx = 0; sx < r.numXSectors; ++sx)
    {
        for (int sz = 0; sz < r.numZSectors; ++sz)
        {
            const int start = sectorIndex(room, sx, sz);
            if (start <= 0 || !chains.insert(start).second) continue;
            if (!readChain(start, entries))
            {
                std::snprintf(buf, sizeof(buf),
                              "sector %d,%d,%d: chain runs out of the block",
                              room, sx, sz);
                error = buf;
                return false;
            }

            // readChain reads strictly linearly, so a word's index is the
            // chain start plus the lengths of the entries in front of it.
            size_t pos = static_cast<size_t>(start);
            for (const Tr5FloorEntry& e : entries)
            {
                const size_t head = pos;
                pos += e.words.size();
                if (e.function != FN_TRIGGER || e.kind != kind) continue;

                for (size_t k = hasRefWord(e.kind) ? 3 : 2;
                     k < e.words.size(); ++k)
                {
                    const uint16_t c = e.words[k];
                    const int act = cmdType(c);
                    if (act == 1 || act == 12) { ++k; continue; }
                    if (act != 0) continue;

                    Tr5Item it;
                    const int item = c & 0x3FF;
                    if (item >= m_itemCount || !getItem(item, it)) continue;
                    if (it.objectNumber != slot) continue;

                    Tr5OneShotHit h;
                    h.sx = sx;
                    h.sz = sz;
                    h.item = item;
                    h.fileAt = m_floorAt + (head + 1) * 2;
                    h.before = e.words[1];
                    h.after = static_cast<uint16_t>(e.words[1] | 0x0100);
                    hits.push_back(h);
                    words.push_back(head + 1);
                    break;          // one setup word per trigger
                }
            }
        }
    }

    if (hits.empty())
    {
        std::snprintf(buf, sizeof(buf),
                      "room %d has no trigger of kind %d naming slot %d",
                      room, kind, slot);
        error = buf;
        return false;
    }

    // A sector of another room whose chain covers one of these words would
    // read the bit as well. Nothing is written in that case.
    std::set<int> seen;
    for (size_t rm = 0; rm < m_rooms.size(); ++rm)
    {
        if (static_cast<int>(rm) == room) continue;
        const Tr5Room& o = m_rooms[rm];

        for (int sx = 0; sx < o.numXSectors; ++sx)
        {
            for (int sz = 0; sz < o.numZSectors; ++sz)
            {
                const int start = sectorIndex(static_cast<int>(rm), sx, sz);
                if (start <= 0 || !seen.insert(start).second) continue;
                if (!readChain(start, entries)) continue;

                size_t len = 0;
                for (const Tr5FloorEntry& e : entries) len += e.words.size();

                const size_t s = static_cast<size_t>(start);
                for (size_t w : words)
                {
                    if (w < s || w >= s + len) continue;
                    std::snprintf(buf, sizeof(buf),
                                  "the trigger's chain is shared with room "
                                  "%d sector %d,%d",
                                  static_cast<int>(rm), sx, sz);
                    error = buf;
                    hits.clear();
                    return false;
                }
            }
        }
    }

    for (size_t i = 0; i < words.size(); ++i)
        std::memcpy(m_bytes.data() + m_floorAt + words[i] * 2,
                    &hits[i].after, 2);
    return true;
}

bool Tr5Level::setTriggerTimer(int room, int sx, int sz, int kind,
                               int fromTimer, int toTimer, Tr5TimerHit& hit,
                               std::string& error)
{
    hit = Tr5TimerHit();
    char buf[256];

    const int start = sectorIndex(room, sx, sz);
    if (start < 0)
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d does not exist",
                      room, sx, sz);
        error = buf;
        return false;
    }

    std::vector<Tr5FloorEntry> entries;
    if (!readChain(start, entries))
    {
        error = "chain runs out of the block";
        return false;
    }

    // The first trigger of the chain is the one _TestTriggers reads. Its
    // setup word sits one behind its head word, and the head's index is
    // the chain start plus the entries in front of it.
    size_t pos = static_cast<size_t>(start);
    const Tr5FloorEntry* trig = nullptr;
    for (const Tr5FloorEntry& e : entries)
    {
        if (e.function == FN_TRIGGER) { trig = &e; break; }
        pos += e.words.size();
    }
    if (!trig)
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d carries no trigger",
                      room, sx, sz);
        error = buf;
        return false;
    }

    const uint16_t setup = trig->words[1];
    if (trig->kind != kind || (setup & 0xFF) != (fromTimer & 0xFF))
    {
        std::snprintf(buf, sizeof(buf),
                      "sector %d,%d,%d: trigger of kind %d with timer %d, "
                      "expected kind %d with timer %d",
                      room, sx, sz, trig->kind, setup & 0xFF, kind,
                      fromTimer & 0xFF);
        error = buf;
        return false;
    }
    const size_t word = pos + 1;

    // Any other sector whose chain covers the word would get the new timer
    // as well. Nothing is written in that case.
    for (size_t rm = 0; rm < m_rooms.size(); ++rm)
    {
        const Tr5Room& o = m_rooms[rm];
        for (int ox = 0; ox < o.numXSectors; ++ox)
        {
            for (int oz = 0; oz < o.numZSectors; ++oz)
            {
                if (static_cast<int>(rm) == room && ox == sx && oz == sz)
                    continue;
                const int s = sectorIndex(static_cast<int>(rm), ox, oz);
                if (s <= 0 || !readChain(s, entries)) continue;

                size_t len = 0;
                for (const Tr5FloorEntry& e : entries) len += e.words.size();
                const size_t from = static_cast<size_t>(s);
                if (word < from || word >= from + len) continue;

                std::snprintf(buf, sizeof(buf),
                              "the trigger's chain is shared with room "
                              "%d sector %d,%d",
                              static_cast<int>(rm), ox, oz);
                error = buf;
                return false;
            }
        }
    }

    hit.chain = start;
    hit.fileAt = m_floorAt + word * 2;
    hit.before = setup;
    hit.after = static_cast<uint16_t>((setup & 0xFF00) | (toTimer & 0xFF));
    std::memcpy(m_bytes.data() + hit.fileAt, &hit.after, 2);
    return true;
}

bool Tr5Level::addTrigger(int room, int sx, int sz, int kind, int timer, bool oneShot,
                          int mask, const std::vector<Tr5TriggerCmd>& cmds,
                          std::string& error, int refItem)
{
    char buf[256];

    if (cmds.empty()) { error = "a trigger without commands would be pointless"; return false; }

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0)
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d does not exist", room, sx, sz);
        error = buf;
        return false;
    }

    std::vector<Tr5FloorEntry> chain;
    if (!readChain(idx, chain)) { error = "chain runs out of the block"; return false; }

    if (const char* why = canAppendTrigger(chain))
    {
        std::snprintf(buf, sizeof(buf), "sector %d,%d,%d unsuitable: %s", room, sx, sz, why);
        error = buf;
        return false;
    }

    Tr5FloorEntry e;
    e.function = FN_TRIGGER;
    e.kind = kind;
    // Word 0: function in bits 0-4, kind in bits 8-13. Bit 15 is assigned
    // by the chain builder.
    e.words.push_back(static_cast<uint16_t>((kind << 8) | FN_TRIGGER));
    // Word 1: timer, one-shot, code bits.
    e.words.push_back(static_cast<uint16_t>((timer & 0xFF) |
                                              (oneShot ? 0x100 : 0) |
                                              ((mask & 0x1F) << 9)));

    // SWITCH, KEY and PICKUP push a reference word in front of the command
    // list: the item that must be operated resp. picked up. The engine
    // reads it three times the same way (_TestTriggers in control.cpp,
    // "value = *(data++) & 0x3FF") and does NOT treat it as a command.
    //
    // Such a trigger is not one placed freely but one bound to an existing
    // object: a secret list places a keyhole plus the key trigger that
    // connects it to a door. Whoever passes refItem thereby says explicitly
    // which object the trigger is bound to.
    if (hasRefWord(kind))
    {
        if (refItem < 0)
        {
            error = "this trigger kind needs a reference word (refItem) - cannot be created like this";
            return false;
        }
        e.words.push_back(static_cast<uint16_t>(refItem & 0x3FF));
    }
    else if (refItem >= 0)
    {
        error = "this trigger kind knows no reference word - refItem would be pointless";
        return false;
    }

    for (size_t i = 0; i < cmds.size(); ++i)
    {
        uint16_t c = static_cast<uint16_t>(((cmds[i].type & 0xF) << 10) | (cmds[i].param & 0x3FF));
        if (i + 1 == cmds.size()) c |= 0x8000;       // end bit on the last
        e.words.push_back(c);

        if (cmds[i].type == 1 || cmds[i].type == 12)
        {
            error = "camera and flyby commands need an extra word - not provided for here";
            return false;
        }
    }

    chain.push_back(e);

    uint16_t fresh = 0;
    if (!appendChain(chain, fresh, error)) return false;
    setSectorIndex(m_rooms[static_cast<size_t>(room)], sx, sz, fresh);
    return true;
}

//----------------------------------------------------------------------------
// Turn a wall sector into a portal (the andy3 double door).
//
// What a portal sector is was measured with SectorCheck on the working
// neighbors in andy3 (room 34 column x0, room 36 column x4):
//
//   - Floor/ceiling carry the values of the OVERLAPPED walkable sector of
//     the target room. The portal columns of two rooms lie on top of each
//     other: 34(0,z) covers the same world area as 36(3,z). pit_room and
//     sky_room come from there too.
//   - The box word is fx=6, box=2047 on all portal sectors (no box,
//     0x7FF6) - portals belong to no AI box.
//   - The chain ends with DOOR -> target room; slopes before it stay
//     (36(4,4) vanilla: [TILT][SPLIT4][DOOR]).
//
// The VIEW WINDOW (the room list's 32-byte portal entry) is built by
// addViewWindow when it is missing, rather than demanded as a precondition:
// in andy3, Core built both windows in completely and only sealed the
// sectors, but other secret lists ask for walls opened where Core never
// prepared a window at all.
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// Create the view window between two rooms.
//
// A room blob is laid out as
//
//   [0 .. 207]                 header, fixed
//   208 + start_sd             sectors, numX * numZ * 8
//   after                      uint16 portal count, then 32 bytes per portal
//   after                      uint16 statics count
//   208 + end_portal           statics
//
// and the rooms lie SEQUENTIALLY in the file behind their XELA marker and a
// size word - there is no table of room offsets to repair. So inserting one
// portal is four numbers: the count in front of the list, end_portal in the
// header, the room's own size word, and the main block size. Everything
// after simply shifts, which reparse sorts out.
//
// One window is written per direction. They are inserted one at a time with
// a reparse in between, because the first insertion moves the second room.
//----------------------------------------------------------------------------
bool Tr5Level::portalRecords(int room, std::vector<PortalRecord>& out, std::string& error) const
{
    out.clear();
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room does not exist"; return false; }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.sectorsOffset) { error = "room did not parse"; return false; }

    const size_t end = r.sectorsOffset +
        static_cast<size_t>(r.numXSectors) * r.numZSectors * 8;
    uint16_t n; std::memcpy(&n, m_bytes.data() + end, 2);

    for (uint16_t i = 0; i < n; ++i)
    {
        const size_t at = end + 2 + static_cast<size_t>(i) * 32;
        PortalRecord p;
        uint16_t to; std::memcpy(&to, m_bytes.data() + at, 2);
        p.toRoom = to;
        int16_t w;
        std::memcpy(&w, m_bytes.data() + at + 2, 2); p.nx = w;
        std::memcpy(&w, m_bytes.data() + at + 4, 2); p.ny = w;
        std::memcpy(&w, m_bytes.data() + at + 6, 2); p.nz = w;
        for (int k = 0; k < 4; ++k)
            for (int c = 0; c < 3; ++c)
            { std::memcpy(&w, m_bytes.data() + at + 8 + k * 6 + c * 2, 2); p.v[k][c] = w; }
        out.push_back(p);
    }
    return true;
}

/*
 * The fixed camera table. Sixteen bytes per record: three int32
 * coordinates, then the room as int16 and the flags as int16.
 *
 * First person pushes a trigger's Camera command aside, but not where the
 * camera carries flags & 3 -
 * that is the sniper/game camera, and its overlay is drawn inside
 * FixedCamera. From the outside two such triggers look identical, so the
 * flag has to be readable.
 */
bool Tr5Level::cameraRecords(std::vector<CameraRecord>& out, std::string& error) const
{
    out.clear();

    if (!m_cameras.dataAt)
    {
        error = "camera table not located - level not parsed?";
        return false;
    }

    for (int i = 0; i < m_cameras.count; ++i)
    {
        const size_t at = m_cameras.dataAt + static_cast<size_t>(i) * 16;
        if (at + 16 > m_bytes.size())
        {
            error = "camera table runs past the end of the file";
            return false;
        }

        int32_t x, y, z;
        int16_t room, flags;
        std::memcpy(&x, m_bytes.data() + at, 4);
        std::memcpy(&y, m_bytes.data() + at + 4, 4);
        std::memcpy(&z, m_bytes.data() + at + 8, 4);
        std::memcpy(&room, m_bytes.data() + at + 12, 2);
        std::memcpy(&flags, m_bytes.data() + at + 14, 2);

        CameraRecord c;
        c.x = x; c.y = y; c.z = z;
        c.room = room; c.flags = static_cast<uint16_t>(flags);
        out.push_back(c);
    }

    return true;
}

//----------------------------------------------------------------------------
// Every drawn face of a room, in world coordinates.
//
// The layout comes from the engine, not from guesswork. ROOM_INFO is the
// 208-byte room header, and three of its fields matter here:
//   168  nRoomlets
//   172  pRoomlets   an array of 56-byte ROOMLETs
//   176  pRmVtx      7 floats per vertex: x, y, z, nx, ny, nz, prelight
//   180  pRmFace     one stream of shorts, handed out to the roomlets in
//                    order (aFixUpRoom, alexstuff.cpp):
//                        pFac += 5 * nTri + 6 * nQuad
// and drawroom.cpp reads a face as
//   quad: v0 v1 v2 v3 texword effects     (6 shorts)
//   tri:  v0 v1 v2    texword effects     (5 shorts)
//   texword: bit 15 double sided, bits 0-13 the texinfo
//
// Vertex indices are relative to the ROOMLET's own block, not the room's.
bool Tr5Level::roomFaces(int room, std::vector<FaceInfo>& out, std::string& error) const
{
    out.clear();
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room does not exist"; return false; }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset) { error = "room did not parse"; return false; }

    const size_t hdr = r.headerOffset, body = hdr + 208;
    int32_t nRoomlets; std::memcpy(&nRoomlets, m_bytes.data() + hdr + 168, 4);
    uint32_t oRoomlets, oVtx, oFace;
    std::memcpy(&oRoomlets, m_bytes.data() + hdr + 172, 4);
    std::memcpy(&oVtx,      m_bytes.data() + hdr + 176, 4);
    std::memcpy(&oFace,     m_bytes.data() + hdr + 180, 4);

    if (nRoomlets <= 0 || nRoomlets > 4096) return true;      // nothing drawn
    if ((oRoomlets & 1) || (oVtx & 1) || (oFace & 1)) return true;

    size_t facAt = body + oFace;
    size_t vtxAt = body + oVtx;

    for (int i = 0; i < nRoomlets; ++i)
    {
        const size_t rl = body + oRoomlets + static_cast<size_t>(i) * 56;
        if (rl + 56 > m_bytes.size()) { error = "roomlet outside the file"; return false; }
        int16_t nVtx, nQuad, nTri;
        std::memcpy(&nVtx,  m_bytes.data() + rl,     2);
        std::memcpy(&nQuad, m_bytes.data() + rl + 6, 2);
        std::memcpy(&nTri,  m_bytes.data() + rl + 8, 2);

        auto corner = [&](int idx, long* xyz) -> bool
        {
            const size_t at = vtxAt + static_cast<size_t>(idx) * 28;
            if (idx < 0 || idx >= nVtx || at + 12 > m_bytes.size()) return false;
            float f[3];
            std::memcpy(f, m_bytes.data() + at, 12);
            xyz[0] = static_cast<long>(f[0]) + r.originX;
            xyz[1] = static_cast<long>(f[1]);
            xyz[2] = static_cast<long>(f[2]) + r.originZ;
            return true;
        };

        for (int q = 0; q < nQuad + nTri; ++q)
        {
            const bool quad = (q < nQuad);
            const int  n    = quad ? 4 : 3;
            const size_t at = facAt;
            if (at + static_cast<size_t>(n + 2) * 2 > m_bytes.size())
            { error = "face outside the file"; return false; }

            FaceInfo f;
            f.roomlet = i; f.quad = quad;
            f.indexAt = at;
            f.texWordAt = at + static_cast<size_t>(n) * 2;
            bool good = true;
            for (int k = 0; k < 4; ++k) { f.index[k] = 0; f.v[k][0] = f.v[k][1] = f.v[k][2] = 0; }
            for (int k = 0; k < n; ++k)
            {
                uint16_t w; std::memcpy(&w, m_bytes.data() + at + static_cast<size_t>(k) * 2, 2);
                f.index[k] = w;
                if (!corner(w, f.v[k])) good = false;
            }
            uint16_t tw, ef;
            std::memcpy(&tw, m_bytes.data() + f.texWordAt, 2);
            std::memcpy(&ef, m_bytes.data() + f.texWordAt + 2, 2);
            f.texInfo = tw & 0x3FFF;
            f.doubleSided = (tw & 0x8000) != 0;
            f.effects = ef;
            if (good) out.push_back(f);

            facAt += static_cast<size_t>(n + 2) * 2;
        }

        vtxAt += static_cast<size_t>(nVtx) * 28;
    }
    return true;
}

namespace {

// Does the face lie wholly on this sector's square of the world, and does it
// stand UPRIGHT? Floors and ceilings sit on the same square and must not be
// caught - only a wall gets in the way of looking through.
bool faceOnSectorWall(const Tr5Level::FaceInfo& f, long x0, long x1, long z0, long z1)
{
    const int n = f.quad ? 4 : 3;
    for (int k = 0; k < n; ++k)
        if (f.v[k][0] < x0 || f.v[k][0] > x1 || f.v[k][2] < z0 || f.v[k][2] > z1)
            return false;

    bool sameX = true, sameZ = true, sameY = true;
    for (int k = 1; k < n; ++k)
    {
        if (f.v[k][0] != f.v[0][0]) sameX = false;
        if (f.v[k][2] != f.v[0][2]) sameZ = false;
        if (f.v[k][1] != f.v[0][1]) sameY = false;
    }
    return (sameX || sameZ) && !sameY;
}

}  // namespace

//----------------------------------------------------------------------------
// Take the wall away from a sector (option "@Clear").
//
// A genuine portal carries NO face at all - measured: andy1's room 0 at its
// door to room 70 has none, and neither has either side of joby3's opening.
// No editor of the old days could put a texture on a portal, which is why
// they were always something special to work around. Andrea3's room 1 at
// (x6,z2) has exactly one quad, and that is the wall one still bumps into.
//
// The face is DEGENERATED rather than removed: all four corner indices are
// set to the first one, so the quad has no area and nothing is drawn. That
// keeps every count, every offset and the file length exactly as they are.
// Cutting the twelve bytes out would match a real portal to the byte, but it
// means shifting the face stream and nine header offsets with it - arithmetic
// that is easy to get wrong - and it looks no different.
bool Tr5Level::clearWallFaces(int room, int sx, int sz, int& cleared, std::string& error)
{
    cleared = 0;
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "sector outside room " + std::to_string(room); return false; }

    std::vector<FaceInfo> faces;
    if (!roomFaces(room, faces, error)) return false;

    const long x0 = r.originX + static_cast<long>(sx) * 1024, x1 = x0 + 1024;
    const long z0 = r.originZ + static_cast<long>(sz) * 1024, z1 = z0 + 1024;

    // The box over the faces this call really took away.
    long lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };

    for (size_t i = 0; i < faces.size(); ++i)
    {
        const FaceInfo& f = faces[i];
        if (!faceOnSectorWall(f, x0, x1, z0, z1)) continue;
        const int n = f.quad ? 4 : 3;
        const uint16_t first = static_cast<uint16_t>(f.index[0]);
        for (int k = 1; k < n; ++k)
            std::memcpy(m_bytes.data() + f.indexAt + static_cast<size_t>(k) * 2, &first, 2);
        for (int k = 0; k < n; ++k)
            for (int c = 0; c < 3; ++c)
            {
                if (!cleared && !k) { lo[c] = hi[c] = f.v[k][c]; continue; }
                if (f.v[k][c] < lo[c]) lo[c] = f.v[k][c];
                if (f.v[k][c] > hi[c]) hi[c] = f.v[k][c];
            }
        ++cleared;
    }

    // The room is marked here only when a face was actually found and
    // degenerated: a cut box has to be the box of something, and the only
    // thing this call knows about is the faces it changed.
    //
    // Nothing is lost by staying quiet when there were none. @Clear is
    // reachable from one place only (Patch::apply's portalAdds loop), and
    // that loop has just opened the very sector through makePortal or
    // makeVerticalPortal - both of which record the opening themselves, out
    // of geometry that is always there.
    //
    // The axis the faces are FLAT in is the wall's normal, and it gets the
    // half sector like any other opening's - faceOnSectorWall only ever
    // accepts a face whose corners share their x or their z, so there is
    // always such an axis and it is found rather than assumed. Measured
    // consequence, ANDREA3 with the glitched list: the box over the three
    // cleared walls then comes out to the same six numbers as the box over
    // the three view windows, and the sidecar carries three cut lines where
    // it carried six.
    if (cleared)
    {
        int flat = -1;
        for (int c = 0; c < 3; ++c)
            if (lo[c] == hi[c]) { flat = c; break; }

        hdMarkCutBox(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], flat);
    }
    return true;
}

//----------------------------------------------------------------------------
// Give a wall the texture of another one (option "@LikeWall").
//
// For the case where the opening should NOT be a hole but a pane of glass.
// A texinfo number says nothing to a human, so the source is a sector one
// can point at instead. Only the texture word is taken; the effects word of
// the target stays as it is.
//
// This can only retexture a face that EXISTS. Where a sector has no wall -
// which is what most portals look like - there is nothing to put a texture
// on, and that is reported rather than silently ignored.
bool Tr5Level::copyWallTexture(int fromRoom, int fsx, int fsz,
                               int toRoom, int tsx, int tsz, int& changed, std::string& error)
{
    changed = 0;
    if (fromRoom < 0 || fromRoom >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(fromRoom) + " does not exist"; return false; }
    if (toRoom < 0 || toRoom >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(toRoom) + " does not exist"; return false; }

    const Tr5Room& fr = m_rooms[static_cast<size_t>(fromRoom)];
    std::vector<FaceInfo> src;
    if (!roomFaces(fromRoom, src, error)) return false;
    const long fx0 = fr.originX + static_cast<long>(fsx) * 1024, fx1 = fx0 + 1024;
    const long fz0 = fr.originZ + static_cast<long>(fsz) * 1024, fz1 = fz0 + 1024;

    uint16_t texWord = 0;
    bool found = false;
    for (size_t i = 0; i < src.size() && !found; ++i)
        if (faceOnSectorWall(src[i], fx0, fx1, fz0, fz1))
        {
            std::memcpy(&texWord, m_bytes.data() + src[i].texWordAt, 2);
            found = true;
        }
    if (!found)
    {
        error = "room " + std::to_string(fromRoom) + " has no wall at (x" +
                std::to_string(fsx) + ",z" + std::to_string(fsz) + ") to take a texture from";
        return false;
    }

    const Tr5Room& tr = m_rooms[static_cast<size_t>(toRoom)];
    std::vector<FaceInfo> dst;
    if (!roomFaces(toRoom, dst, error)) return false;
    const long tx0 = tr.originX + static_cast<long>(tsx) * 1024, tx1 = tx0 + 1024;
    const long tz0 = tr.originZ + static_cast<long>(tsz) * 1024, tz1 = tz0 + 1024;

    for (size_t i = 0; i < dst.size(); ++i)
        if (faceOnSectorWall(dst[i], tx0, tx1, tz0, tz1))
        {
            std::memcpy(m_bytes.data() + dst[i].texWordAt, &texWord, 2);
            ++changed;
        }
    return true;
}

bool Tr5Level::headerField(int room, int offset, unsigned& value, std::string& error) const
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room does not exist"; return false; }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset || offset < 0 || offset > 204)
    { error = "no such field"; return false; }
    uint32_t v; std::memcpy(&v, m_bytes.data() + r.headerOffset + offset, 4);
    value = v;
    return true;
}

bool Tr5Level::sectorInfo(int room, std::vector<SectorInfo>& out, std::string& error) const
{
    out.clear();
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room does not exist"; return false; }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.sectorsOffset) { error = "room did not parse"; return false; }

    for (int x = 0; x < r.numXSectors; ++x)
        for (int z = 0; z < r.numZSectors; ++z)
        {
            const size_t at = r.sectorsOffset +
                (static_cast<size_t>(x) * r.numZSectors + z) * 8;
            SectorInfo s;
            s.sx = x; s.sz = z;
            uint16_t w;
            std::memcpy(&w, m_bytes.data() + at, 2);     s.fdIndex = w;
            std::memcpy(&w, m_bytes.data() + at + 2, 2); s.box = w;
            s.roomBelow = m_bytes[at + 4];
            s.floor     = static_cast<int8_t>(m_bytes[at + 5]);
            s.roomAbove = m_bytes[at + 6];
            s.ceiling   = static_cast<int8_t>(m_bytes[at + 7]);

            // The floordata door too. THAT is the horizontal handover, and
            // seeing it next to the vertical link bytes is the whole point:
            // one sector must never carry both.
            s.doorTo = -1;
            if (s.fdIndex)
            {
                std::vector<Tr5FloorEntry> chain;
                if (readChain(s.fdIndex, chain))
                    for (size_t q = 0; q < chain.size(); ++q)
                        if (chain[q].function == 1 && chain[q].words.size() >= 2)
                            s.doorTo = chain[q].words[1];
            }
            out.push_back(s);
        }
    return true;
}

// Which wall of `room` a border sector sits on. Portals sit on the border
// ring of a room, and that is what says which way the opening faces. A corner
// sector belongs to two walls; then the neighbour's own position decides.
bool Tr5Level::portalWallAxis(int room, int other, int sx, int sz,
                              int& ax, int& az) const
{
    ax = az = 0;
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return false;
    if (other < 0 || other >= static_cast<int>(m_rooms.size())) return false;

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    const Tr5Room& o = m_rooms[static_cast<size_t>(other)];

    const bool loX = (sx == 0), hiX = (sx == r.numXSectors - 1);
    const bool loZ = (sz == 0), hiZ = (sz == r.numZSectors - 1);
    if (!loX && !hiX && !loZ && !hiZ) return false;

    const int cxA = r.originX + r.numXSectors * 512;
    const int czA = r.originZ + r.numZSectors * 512;
    const int cxB = o.originX + o.numXSectors * 512;
    const int czB = o.originZ + o.numZSectors * 512;
    const bool alongX = abs(cxB - cxA) >= abs(czB - czA);

    if (alongX && (loX || hiX)) { ax = loX ? -1 : 1; return true; }
    if (!alongX && (loZ || hiZ)) { az = loZ ? -1 : 1; return true; }
    if (loX || hiX) { ax = loX ? -1 : 1; return true; }
    az = loZ ? -1 : 1;
    return true;
}

bool Tr5Level::crossWorld(int room, int sx, int sz, long wx, long wz,
                          long& fy, long& cy) const
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return false;
    const Tr5Room& t = m_rooms[static_cast<size_t>(room)];
    const long lx = wx - (static_cast<long>(t.originX) + static_cast<long>(sx) * 1024);
    const long lz = wz - (static_cast<long>(t.originZ) + static_cast<long>(sz) * 1024);
    if ((lx != 0 && lx != 1024) || (lz != 0 && lz != 1024)) return false;
    return cornerFloor(room, sx, sz, static_cast<int>(lx), static_cast<int>(lz), fy) &&
           cornerCeiling(room, sx, sz, static_cast<int>(lx), static_cast<int>(lz), cy);
}

//----------------------------------------------------------------------------
// The corner-exact opening of a wall portal - see the header for what it is
// measured against.
//
// The pairing is the same one buildRoomGeometry uses for its walls: a portal
// square is the OTHER room's space, so OUR cross section comes from the
// sector one step INTO room A, and the target's from room B's sector at the
// very place that portal square occupies. Those two sectors are neighbours in
// the world and share the edge the wall stands on.
//
// Union, not intersection. The window is a clipping rectangle, and taking the
// intersection of the two corners would cut away the part of a sloped opening
// that only one corner reaches - which is exactly the 24 % of Andrea3's new
// octagon that came out black.
//----------------------------------------------------------------------------
bool Tr5Level::wallWindow(int roomA, int sx, int sz, int roomB,
                          long& yCeil, long& yFloor) const
{
    yCeil = yFloor = 0;

    int ax = 0, az = 0;
    if (!portalWallAxis(roomA, roomB, sx, sz, ax, az)) return false;

    const Tr5Room& a = m_rooms[static_cast<size_t>(roomA)];
    const Tr5Room& b = m_rooms[static_cast<size_t>(roomB)];

    // Our own cross section: one step inward from the portal square.
    const int aix = sx - ax, aiz = sz - az;

    // The target's: its sector at the portal square's place in the world.
    const long wpx = static_cast<long>(a.originX) + static_cast<long>(sx) * 1024;
    const long wpz = static_cast<long>(a.originZ) + static_cast<long>(sz) * 1024;
    const long dbx = wpx - b.originX, dbz = wpz - b.originZ;
    if (dbx < 0 || dbz < 0 || (dbx % 1024) || (dbz % 1024)) return false;
    const int bix = static_cast<int>(dbx / 1024), biz = static_cast<int>(dbz / 1024);

    // The shared edge: the INNER edge of the portal square, which is where
    // the wall between the two rooms stands.
    const long edgeX = static_cast<long>(a.originX) +
                       static_cast<long>((ax < 0) ? sx + 1 : sx) * 1024;
    const long edgeZ = static_cast<long>(a.originZ) +
                       static_cast<long>((az < 0) ? sz + 1 : sz) * 1024;

    long top = 0, bot = 0;
    for (int i = 0; i < 2; ++i)
    {
        const long wx = ax ? edgeX
                           : (static_cast<long>(a.originX) + static_cast<long>(sx + i) * 1024);
        const long wz = ax ? (static_cast<long>(a.originZ) + static_cast<long>(sz + i) * 1024)
                           : edgeZ;

        long fA = 0, cA = 0, fB = 0, cB = 0;
        if (!crossWorld(roomA, aix, aiz, wx, wz, fA, cA)) return false;
        if (!crossWorld(roomB, bix, biz, wx, wz, fB, cB)) return false;

        const long o = (cA > cB) ? cA : cB;          // the lower of the two ceilings
        const long u = (fA < fB) ? fA : fB;          // the higher of the two floors
        if (i == 0) { top = o; bot = u; }
        else { if (o < top) top = o; if (u > bot) bot = u; }
    }

    if (top >= bot) return false;                    // no head room anywhere
    yCeil = top;
    yFloor = bot;
    return true;
}

bool Tr5Level::addViewWindow(int roomA, int sx, int sz,
                             int roomB, int nsxB, int nszB, std::string& error)
{
    constexpr size_t HDR = 208, SECTOR = 8, PORTAL = 32;

    // ONE plane for both windows of a horizontal portal.
    //
    // Taking each side's own value - the room below its ceiling, the room
    // above its floor - fails whenever they do not coincide: andy1 room
    // 227's ceiling and room 93's floor sit 256 units apart, so two windows
    // built that way describe different holes. The engine draws a void to
    // slide through, and trview refuses the level.
    //
    // Filled in below, before anything is inserted, because the second
    // insertion reparses and moves the rooms.
    long sharedY = 0;
    bool haveShared = false;

    // And one HEIGHT for both windows of a wall opening.
    //
    // Taking the floor and ceiling of the sector across from each side
    // fails too: those are two different columns of the world, so on
    // sloping ground they disagree. Built that way, Andrea3 came out with
    // one window spanning -5120..0 and its partner -3072..-768 - two
    // windows describing different holes. Core's own pairs always match
    // (joby3's 76 <-> 10 are both -2048..-1536), and a mismatch is what
    // makes trview refuse the level.
    //
    // Both windows therefore get ONE rectangle - the corner-exact one out
    // of the floordata (wallWindow), not the two height bytes. The bytes
    // are one number for a whole square, and the engine
    // CLIPS the neighbouring room to this rectangle (SetRoomBounds,
    // draw.cpp, unioned over all portals by GetRoomBounds).
    // Andrea3's new opening reaches down to -4096 while the bytes said
    // -3072, and a quarter of the octagon behind it was simply never drawn.
    // Where the floordata cannot answer - a shell room, an unreadable chain -
    // the old byte reading stands in, intersected as before.
    long sharedCeil = 0, sharedFloor = 0;
    bool haveSharedY = false;

    // One direction. Returns the bytes to splice and where they go.
    auto build = [&](int from, int to, int fx, int fz,
                     size_t& outAt, std::vector<uint8_t>& rec) -> bool
    {
        const Tr5Room& r = m_rooms[static_cast<size_t>(from)];
        const Tr5Room& o = m_rooms[static_cast<size_t>(to)];
        if (!r.headerOffset || !r.sectorsOffset)
        { error = "room " + std::to_string(from) + " did not parse"; return false; }

        // Wall or floor? A sector off the border ring cannot be a wall, and
        // rooms stacked in Y do not meet on one either - Andrea3's rooms 1
        // and 74 overlap by TWO columns, so no pair of sectors is on both
        // rooms' edges, and andy1's room 227 sits bodily inside room 93.
        //
        // Y grows DOWNWARD in this format, so the room with the smaller
        // yBottom is the upper one.
        int ax = 0, az = 0, ay = 0;
        if (!portalWallAxis(from, to, fx, fz, ax, az))
            ay = (o.yBottom < r.yBottom) ? -1 : 1;   // -1 = neighbour above

        const size_t sectorsEnd = r.sectorsOffset +
            static_cast<size_t>(r.numXSectors) * r.numZSectors * SECTOR;
        uint16_t nPortals; std::memcpy(&nPortals, m_bytes.data() + sectorsEnd, 2);

        // The winding is DERIVED, not copied off a look-alike.
        //
        // First attempt was to find a portal already facing the same way and
        // copy it. That works only where the room happens to have one -
        // Andrea3's room 1 has a single portal, on another wall, and the
        // build failed there for no good reason.
        //
        // The convention is the same throughout the format, so ANY portal in
        // ANY room answers it: take its corners in file order, cross the
        // first two edges, and see whether that points along its normal or
        // against it. That sign is what has to be reproduced.
        int sign = 0;
        for (size_t rm = 0; rm < m_rooms.size() && !sign; ++rm)
        {
            const Tr5Room& q = m_rooms[rm];
            if (!q.sectorsOffset || q.portalRooms.empty()) continue;
            const size_t qEnd = q.sectorsOffset +
                static_cast<size_t>(q.numXSectors) * q.numZSectors * SECTOR;
            const size_t at = qEnd + 2;
            int16_t nx, ny, nz, v[4][3];
            std::memcpy(&nx, m_bytes.data() + at + 2, 2);
            std::memcpy(&ny, m_bytes.data() + at + 4, 2);
            std::memcpy(&nz, m_bytes.data() + at + 6, 2);
            for (int k = 0; k < 4; ++k)
                for (int c = 0; c < 3; ++c)
                    std::memcpy(&v[k][c], m_bytes.data() + at + 8 + k * 6 + c * 2, 2);

            const long e1[3] = { v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2] };
            const long e2[3] = { v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2] };
            const long cx = e1[1] * e2[2] - e1[2] * e2[1];
            const long cy = e1[2] * e2[0] - e1[0] * e2[2];
            const long cz = e1[0] * e2[1] - e1[1] * e2[0];
            const long dot = cx * nx + cy * ny + cz * nz;
            if (dot) sign = (dot > 0) ? 1 : -1;
        }
        if (!sign)
        { error = "the level has no portal at all to read the corner order from"; return false; }

        rec.assign(PORTAL, 0);
        const uint16_t adj = static_cast<uint16_t>(to);
        std::memcpy(rec.data(), &adj, 2);

        // The normal points AWAY from the neighbour, into this room - that is
        // what makes the opening visible from in here.
        const int16_t nrm[3] = { static_cast<int16_t>(-ax), static_cast<int16_t>(-ay),
                                 static_cast<int16_t>(-az) };
        std::memcpy(rec.data() + 2, &nrm[0], 2);
        std::memcpy(rec.data() + 4, &nrm[1], 2);
        std::memcpy(rec.data() + 6, &nrm[2], 2);

        // The opening: one sector wide, from the ceiling to the floor of the
        // sector on the OTHER side - that is the hole one can actually walk
        // through.
        const int worldX = r.originX + fx * 1024;
        const int worldZ = r.originZ + fz * 1024;
        const int osx = (worldX - o.originX) / 1024;
        const int osz = (worldZ - o.originZ) / 1024;
        if (osx < 0 || osx >= o.numXSectors || osz < 0 || osz >= o.numZSectors)
        { error = "the rooms do not overlap at that sector"; return false; }

        const size_t across = o.sectorsOffset +
            (static_cast<size_t>(osx) * o.numZSectors + osz) * SECTOR;
        const int yFloor = haveSharedY ? static_cast<int>(sharedFloor)
            : static_cast<int>(static_cast<int8_t>(m_bytes[across + 5])) * 256;
        const int yCeil  = haveSharedY ? static_cast<int>(sharedCeil)
            : static_cast<int>(static_cast<int8_t>(m_bytes[across + 7])) * 256;

        // A horizontal opening lies in ONE plane: this room's own ceiling
        // when the neighbour is above, its floor when the neighbour is below.
        const size_t here = r.sectorsOffset +
            (static_cast<size_t>(fx) * r.numZSectors + fz) * SECTOR;
        const int yHere = haveShared
            ? static_cast<int>(sharedY)
            : ((ay < 0)
               ? static_cast<int>(static_cast<int8_t>(m_bytes[here + 7])) * 256
               : static_cast<int>(static_cast<int8_t>(m_bytes[here + 5])) * 256);

        // The four corners of the opening, laid out in one order and then
        // flipped if the level's convention runs the other way.
        const int nx0 = fx * 1024, nx1 = fx * 1024 + 1024;
        const int nz0 = fz * 1024, nz1 = fz * 1024 + 1024;
        // A portal does not sit ON its plane - it sits one unit in FRONT of
        // it, and only on the side whose normal points the positive way.
        // Measured off vanilla andy1, where all six directions occur:
        //
        //   227 -> 226  normal  1,0,0   x 1023   ( = 1024 - 1 )
        //   227 -> 230  normal -1,0,0   x 2048
        //   227 -> 114  normal  0,0,1   z 1023
        //   227 -> 139  normal  0,0,-1  z 4096
        //   222 -> 227  normal  0,1,0   y 7679   ( = 7680 - 1 )
        //   222 -> 119  normal  0,-1,0  y 9472
        //
        // So: where the normal component is +1, subtract one. Without it the
        // window lies exactly in the wall or ceiling it is meant to open, and
        // the two coincident planes are what made trview refuse the level and
        // the engine draw a void to slide through.
        //
        // The normal is (-ax,-ay,-az), so the +1 side is the one where the
        // axis value is negative.
        // The window sits on the sector's INNER edge - the one facing the
        // room's own interior - not on its outer edge. Three vanilla windows
        // say so, and all three agree:
        //
        //   Andrea3 room  1 -> 20  normal  1,0,0 at 1023   ring sector 0,
        //   joby3   room 76 -> 10  normal  1,0,0 at 1023   inner edge 1024,
        //   joby3   room 10 -> 76  normal -1,0,0 at 6144   ring sector 6,
        //                                                  inner edge 6144
        //
        // Taking the outer edge put Andrea3's room 1 window at 98304 while
        // room 74's sat at 97280 - two windows describing different holes,
        // and from inside room 74 the drawer never looked through at all.
        //
        // ax is -1 for the low-side ring sector and +1 for the high one, and
        // the normal is its opposite, pointing into the room. The one-unit
        // inset applies where that normal is positive.
        const int wallX = ax ? ((ax < 0) ? nx1 - 1 : nx0) : 0;
        const int wallZ = az ? ((az < 0) ? nz1 - 1 : nz0) : 0;
        const int flatY = yHere - ((ay < 0) ? 1 : 0);

        int corner[4][3];
        for (int k = 0; k < 4; ++k)
        {
            const bool farEnd = (k == 1 || k == 2);
            const bool low    = (k >= 2);
            if (ay)
            {
                // Flat in one plane: the four corners of the sector itself.
                corner[k][0] = farEnd ? nx1 : nx0;
                corner[k][2] = low    ? nz1 : nz0;
                corner[k][1] = flatY;
            }
            else if (ax)
            {
                corner[k][0] = wallX;
                corner[k][2] = farEnd ? nz1 : nz0;
                corner[k][1] = low ? yFloor : yCeil;
            }
            else
            {
                corner[k][2] = wallZ;
                corner[k][0] = farEnd ? nx1 : nx0;
                corner[k][1] = low ? yFloor : yCeil;
            }
        }

        // Does that order match the level's? If not, reverse it - reversing
        // a quad's order flips its facing, and that is the only degree of
        // freedom left once the positions are fixed.
        {
            const long e1[3] = { corner[1][0] - corner[0][0], corner[1][1] - corner[0][1],
                                 corner[1][2] - corner[0][2] };
            const long e2[3] = { corner[2][0] - corner[0][0], corner[2][1] - corner[0][1],
                                 corner[2][2] - corner[0][2] };
            const long cx = e1[1] * e2[2] - e1[2] * e2[1];
            const long cy = e1[2] * e2[0] - e1[0] * e2[2];
            const long cz = e1[0] * e2[1] - e1[1] * e2[0];
            const long dot = cx * (-ax) + cy * (-ay) + cz * (-az);
            const int mine = (dot > 0) ? 1 : -1;
            if (mine != sign)
            {
                for (int k = 0; k < 2; ++k)
                    for (int c = 0; c < 3; ++c)
                    { const int t = corner[k][c]; corner[k][c] = corner[3 - k][c]; corner[3 - k][c] = t; }
            }
        }

        for (int k = 0; k < 4; ++k)
            for (int c = 0; c < 3; ++c)
            {
                const int16_t val = static_cast<int16_t>(corner[k][c]);
                std::memcpy(rec.data() + 8 + k * 6 + c * 2, &val, 2);
            }

        outAt = sectorsEnd + 2 + static_cast<size_t>(nPortals) * PORTAL;
        return true;
    };

    // One insertion, then reparse - the second room has moved by then.
    auto insertOne = [&](int from, int to, int fx, int fz) -> bool
    {
        size_t at = 0;
        std::vector<uint8_t> rec;
        if (!build(from, to, fx, fz, at, rec)) return false;
        return appendPortalRecord(from, rec, error);
    };

    if (roomA < 0 || roomA >= static_cast<int>(m_rooms.size()) ||
        roomB < 0 || roomB >= static_cast<int>(m_rooms.size()))
    { error = "room does not exist"; return false; }

    // The counterpart's sector, worked out before anything moves.
    const Tr5Room& a0 = m_rooms[static_cast<size_t>(roomA)];
    const Tr5Room& b0 = m_rooms[static_cast<size_t>(roomB)];
    const int worldX = a0.originX + sx * 1024;
    const int worldZ = a0.originZ + sz * 1024;
    const int bsx = (worldX - b0.originX) / 1024;
    const int bsz = (worldZ - b0.originZ) / 1024;
    const int bsx2 = nsxB, bsz2 = nszB;

    // Horizontal? Then settle the plane now, from the LOWER room's ceiling -
    // that is the hole one climbs through. A wall sector has -127 in that
    // byte and is no height at all; where that happens, fall back to the
    // upper room's floor, and if that is a wall too, leave each side to work
    // it out for itself as before.
    {
        int ax = 0, az = 0;
        if (!portalWallAxis(roomA, roomB, sx, sz, ax, az))
        {
            const bool aLower = a0.yBottom > b0.yBottom;
            const Tr5Room& low  = aLower ? a0 : b0;
            const Tr5Room& high = aLower ? b0 : a0;
            const int lx = aLower ? sx  : bsx, lz = aLower ? sz  : bsz;
            const int hx = aLower ? bsx : sx,  hz = aLower ? bsz : sz;

            const size_t lowSec = low.sectorsOffset +
                (static_cast<size_t>(lx) * low.numZSectors + lz) * 8;
            const size_t highSec = high.sectorsOffset +
                (static_cast<size_t>(hx) * high.numZSectors + hz) * 8;

            const int lowCeil  = static_cast<int8_t>(m_bytes[lowSec + 7]);
            const int highFloor = static_cast<int8_t>(m_bytes[highSec + 5]);

            if (lowCeil != -127)       { sharedY = lowCeil * 256;   haveShared = true; }
            else if (highFloor != -127){ sharedY = highFloor * 256; haveShared = true; }
        }
    }

    // The opening, corner-exact, before anything is inserted - the insertion
    // reparses and every reference into m_rooms would dangle.
    {
        long wc = 0, wf = 0;
        if (wallWindow(roomA, sx, sz, roomB, wc, wf))
        { sharedCeil = wc; sharedFloor = wf; haveSharedY = true; }
    }

    // The height bytes as a stand-in, only where the floordata said nothing.
    if (!haveSharedY)
    {
        auto across = [&](const Tr5Room& here, int hx, int hz,
                          const Tr5Room& there, int& cy, int& fy) -> bool
        {
            const int wx = here.originX + hx * 1024;
            const int wz = here.originZ + hz * 1024;
            const int ox = (wx - there.originX) / 1024;
            const int oz = (wz - there.originZ) / 1024;
            if (ox < 0 || ox >= there.numXSectors || oz < 0 || oz >= there.numZSectors)
                return false;
            const size_t at = there.sectorsOffset +
                (static_cast<size_t>(ox) * there.numZSectors + oz) * SECTOR;
            const int f = static_cast<int8_t>(m_bytes[at + 5]);
            const int c = static_cast<int8_t>(m_bytes[at + 7]);
            if (f == -127 || c == -127) return false;      // a wall is no height
            fy = f * 256; cy = c * 256;
            return true;
        };

        int ca, fa, cb, fb;
        if (across(a0, sx, sz, b0, ca, fa) && across(b0, bsx2, bsz2, a0, cb, fb))
        {
            const int c = (ca > cb) ? ca : cb;             // the lower ceiling
            const int f = (fa < fb) ? fa : fb;             // the higher floor
            if (c < f) { sharedCeil = c; sharedFloor = f; haveSharedY = true; }
        }
    }

    if (!insertOne(roomA, roomB, sx, sz)) return false;
    if (!insertOne(roomB, roomA, nsxB, nszB)) return false;

    const int before = m_roomMismatch;
    (void)before;
    if (m_roomMismatch)
    { error = "after inserting the view window " + std::to_string(m_roomMismatch) +
              " rooms no longer parse - the layout arithmetic is off"; return false; }

    // Both rooms now carry a window the Remaster's geometry knows nothing
    // about - but only where the window
    // is. The box is read back out of the two records that were just
    // written, one per room, so the SAME code answers here and in
    // makePortal, and the two sides of one opening are two readings of the
    // two records the engine itself clips against.
    hdMarkCutFromWindow(roomA, sx, sz, roomB);
    hdMarkCutFromWindow(roomB, nsxB, nszB, roomA);
    return true;
}

//----------------------------------------------------------------------------
// One view window onto the end of a room's portal list. The record is
// complete as it comes in; what this does is everything the 32 inserted
// bytes move.
//----------------------------------------------------------------------------
bool Tr5Level::appendPortalRecord(int room, const std::vector<uint8_t>& rec,
                                  std::string& error)
{
    constexpr size_t HDR = 208, SECTOR = 8, PORTAL = 32;

    if (room < 0 || room >= static_cast<int>(m_rooms.size()) ||
        rec.size() != PORTAL)
    { error = "no such room, or not a 32-byte record"; return false; }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset || !r.sectorsOffset)
    { error = "room " + std::to_string(room) + " did not parse"; return false; }

    const size_t sectorsEnd = r.sectorsOffset +
        static_cast<size_t>(r.numXSectors) * r.numZSectors * SECTOR;
    const size_t hdr = r.headerOffset;
    uint16_t nOld; std::memcpy(&nOld, m_bytes.data() + sectorsEnd, 2);
    const size_t at = sectorsEnd + 2 + static_cast<size_t>(nOld) * PORTAL;

    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(at),
                   rec.begin(), rec.end());

    uint16_t nPortals; std::memcpy(&nPortals, m_bytes.data() + sectorsEnd, 2);
    ++nPortals;
    std::memcpy(m_bytes.data() + sectorsEnd, &nPortals, 2);

    // A room that had NO portals at all carries Core's mark for it in
    // its door field: an ODD value (Andrea3's room 74 has 0xFFFFFFFF).
    // The engine tests exactly that and throws the list away before it
    // ever looks at it - aFixUpRoom in alexstuff.cpp
    //
    //     if ((long)pR->door & 1) { Log("%X", pR->door); pR->door = 0; }
    //
    // (those bare hex lines in the log after LoadRooms are this, one per
    // portal-less room). So the window sat in the file, our own reader
    // found it because it works the position out for itself, and the
    // engine saw no neighbour: standing in room 74 one looked into
    // blackness with room 1 right there behind the opening.
    //
    // The first portal a room ever gets has to set the field. It points
    // at the count word, which is where the sectors end - and that is in
    // FRONT of the insertion, so it needs no relocating afterwards.
    {
        uint32_t doorOfs;
        std::memcpy(&doorOfs, m_bytes.data() + hdr + 4, 4);
        if (doorOfs & 1)
        {
            doorOfs = static_cast<uint32_t>(sectorsEnd - (hdr + HDR));
            std::memcpy(m_bytes.data() + hdr + 4, &doorOfs, 4);
        }
    }

    // EVERY offset in the header that points behind the portal list has
    // to move with it, not just end_portal.
    //
    // The 208-byte header IS the engine's ROOM_INFO, and nine of its
    // fields are stored as offsets that get relocated on load
    // (aFixUpRoom, alexstuff.cpp): door, floor, mesh, pclight, fogbulb,
    // pRoomlets, pRmVtx, pRmFace, pRmPrelight. door and floor point at
    // the portal list and the sectors, both in front of the insertion,
    // so they stay. The other seven come after and were left behind on
    // the first attempt - the level then reread cleanly here, because
    // this parser only checks the sector and portal arithmetic, and
    // still made trview fail at generating rooms and the game crash
    // after a slide through the void.
    //
    // An odd value is the engine's "none" marker (it tests door & 1), so
    // those are left alone.
    static const size_t RELOC[] = { 16, 144, 148, 172, 176, 180, 184 };
    const size_t insRel = at - (hdr + HDR);
    for (size_t f = 0; f < sizeof(RELOC) / sizeof(RELOC[0]); ++f)
    {
        uint32_t v; std::memcpy(&v, m_bytes.data() + hdr + RELOC[f], 4);
        if ((v & 1) || v < insRel) continue;
        v += PORTAL;
        std::memcpy(m_bytes.data() + hdr + RELOC[f], &v, 4);
    }

    // The room's own size word sits in front of the blob, behind XELA.
    int32_t roomSize; std::memcpy(&roomSize, m_bytes.data() + hdr - 4, 4);
    roomSize += static_cast<int32_t>(PORTAL);
    std::memcpy(m_bytes.data() + hdr - 4, &roomSize, 4);

    m_mainSize += PORTAL;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    return reparse(error);
}

//----------------------------------------------------------------------------
// A view window over an opening the geometry already leaves open - see the
// header for when that is, and why nothing but the two records is written.
//----------------------------------------------------------------------------
bool Tr5Level::addSightWindow(int roomA, int sx, int sz, int sx2, int sz2,
                              int roomB, std::string& error)
{
    constexpr size_t SECTOR = 8, PORTAL = 32;
    const int nRooms = static_cast<int>(m_rooms.size());
    if (roomA < 0 || roomA >= nRooms || roomB < 0 || roomB >= nRooms ||
        roomA == roomB)
    { error = "room does not exist"; return false; }

    // The wall: every sector of the run on the same side of room A's ring,
    // and the run along that wall, not across it.
    int ax = 0, az = 0;
    if (!portalWallAxis(roomA, roomB, sx, sz, ax, az))
    { error = "the run does not start on the border ring"; return false; }
    if (sx2 < sx || sz2 < sz || (ax && sx2 != sx) || (az && sz2 != sz))
    { error = "the run does not lie along one wall"; return false; }

    // The height: the corner-exact opening of every sector, unioned - the
    // window clips the room behind it, so it has to hold all of them.
    long top = 0, bot = 0;
    for (int x = sx; x <= sx2; ++x)
        for (int z = sz; z <= sz2; ++z)
        {
            int bx = 0, bz = 0;
            long c = 0, f = 0;
            if (!portalWallAxis(roomA, roomB, x, z, bx, bz) || bx != ax ||
                bz != az || !wallWindow(roomA, x, z, roomB, c, f))
            {
                error = "no opening between room " + std::to_string(roomA) +
                        " and room " + std::to_string(roomB) + " at sector " +
                        std::to_string(x) + "," + std::to_string(z);
                return false;
            }
            if (x == sx && z == sz) { top = c; bot = f; continue; }
            if (c < top) top = c;
            if (f > bot) bot = f;
        }

    // The plane in world units - the inner edge of the ring sector, where
    // the wall between the two rooms stands - and the run's extent along it.
    const Tr5Room& a = m_rooms[static_cast<size_t>(roomA)];
    const long plane = ax
        ? a.originX + static_cast<long>((ax < 0) ? sx + 1 : sx) * 1024
        : a.originZ + static_cast<long>((az < 0) ? sz + 1 : sz) * 1024;
    const long lo = ax ? a.originZ + static_cast<long>(sz) * 1024
                       : a.originX + static_cast<long>(sx) * 1024;
    const long hi = ax ? a.originZ + static_cast<long>(sz2 + 1) * 1024
                       : a.originX + static_cast<long>(sx2 + 1) * 1024;
    const int along = ax ? 2 : 0;           // the coordinate the run runs in
    const int across = ax ? 0 : 2;          // the one the plane fixes

    // One record per side. The normal points into the room that holds the
    // record, and the window sits one unit in front of the plane where that
    // normal is positive (addViewWindow measured it off vanilla andy1).
    auto build = [&](int from, int to, int sign, std::vector<uint8_t>& rec,
                     bool& present) -> bool
    {
        const Tr5Room& r = m_rooms[static_cast<size_t>(from)];
        const int n[3] = { ax ? sign : 0, 0, az ? sign : 0 };
        const long at = plane - ((sign > 0) ? 1 : 0);

        // Core's corner order for this facing, out of the first wall window
        // of the level that faces the same way: each of its corners is the
        // top or the bottom, the low or the high end of its run, and ours
        // are put down in that same order.
        int order[4][2] = {};               // [k][0] bottom, [k][1] high end
        bool found = false;
        for (int q = 0; q < nRooms && !found; ++q)
        {
            std::vector<PortalRecord> recs;
            std::string ignored;
            if (!portalRecords(q, recs, ignored)) continue;
            for (const PortalRecord& p : recs)
            {
                if (p.nx != n[0] || p.ny != 0 || p.nz != n[2]) continue;
                int y0 = p.v[0][1], y1 = p.v[0][1];
                int w0 = p.v[0][along], w1 = p.v[0][along];
                for (int k = 1; k < 4; ++k)
                {
                    y0 = std::min(y0, p.v[k][1]);
                    y1 = std::max(y1, p.v[k][1]);
                    w0 = std::min(w0, p.v[k][along]);
                    w1 = std::max(w1, p.v[k][along]);
                }
                if (y0 == y1 || w0 == w1) continue;
                bool rect = true;
                for (int k = 0; k < 4 && rect; ++k)
                {
                    const bool yb = p.v[k][1] == y1, wb = p.v[k][along] == w1;
                    if (!yb && p.v[k][1] != y0) rect = false;
                    if (!wb && p.v[k][along] != w0) rect = false;
                    order[k][0] = yb;
                    order[k][1] = wb;
                }
                if (rect) { found = true; break; }
            }
        }
        if (!found)
        {
            error = "the level has no wall window of that facing to copy";
            return false;
        }

        rec.assign(PORTAL, 0);
        const uint16_t adj = static_cast<uint16_t>(to);
        std::memcpy(rec.data(), &adj, 2);
        for (int c = 0; c < 3; ++c)
        {
            const int16_t w = static_cast<int16_t>(n[c]);
            std::memcpy(rec.data() + 2 + c * 2, &w, 2);
        }

        // Corners are stored room-local in x and z, absolute in y.
        int v[4][3];
        for (int k = 0; k < 4; ++k)
        {
            const long w = order[k][1] ? hi : lo;
            v[k][0] = static_cast<int>((ax ? at : w) - r.originX);
            v[k][1] = static_cast<int>(order[k][0] ? bot : top);
            v[k][2] = static_cast<int>((ax ? w : at) - r.originZ);
            for (int c = 0; c < 3; ++c)
            {
                const int16_t val = static_cast<int16_t>(v[k][c]);
                std::memcpy(rec.data() + 8 + k * 6 + c * 2, &val, 2);
            }
        }

        // Never a second window onto the same stretch of wall: one that
        // stands there exactly so counts as done, any other is refused.
        present = false;
        const size_t end = r.sectorsOffset +
            static_cast<size_t>(r.numXSectors) * r.numZSectors * SECTOR;
        std::vector<PortalRecord> mine;
        if (!portalRecords(from, mine, error)) return false;
        for (size_t i = 0; i < mine.size(); ++i)
        {
            const PortalRecord& p = mine[i];
            if (p.toRoom != to) continue;
            bool onPlane = true;
            for (int k = 0; k < 4; ++k)
                if (std::abs(p.v[k][across] - v[0][across]) > 1)
                    onPlane = false;
            if (!onPlane) continue;
            if (std::memcmp(m_bytes.data() + end + 2 + i * PORTAL, rec.data(),
                            PORTAL) == 0)
            { present = true; continue; }
            error = "room " + std::to_string(from) +
                    " already has a window to room " + std::to_string(to) +
                    " on that wall";
            return false;
        }
        return true;
    };

    // Both records first: the first insertion reparses and moves the rooms.
    std::vector<uint8_t> recA, recB;
    bool haveA = false, haveB = false;
    if (!build(roomA, roomB, -(ax + az), recA, haveA)) return false;
    if (!build(roomB, roomA, ax + az, recB, haveB)) return false;
    if (haveA && haveB) return true;
    if (haveA != haveB)
    { error = "only one of the two rooms has the window"; return false; }

    if (!appendPortalRecord(roomA, recA, error)) return false;
    if (!appendPortalRecord(roomB, recB, error)) return false;
    if (m_roomMismatch)
    {
        error = "after inserting the view window " +
                std::to_string(m_roomMismatch) +
                " rooms no longer parse - the layout arithmetic is off";
        return false;
    }

    // No hdMarkCut: no face of either room changes, so the Remaster's art
    // of the opening is the level's own and stays whole.
    return true;
}

//----------------------------------------------------------------------------
// A vertical opening: ceiling into the room above, floor into the room below.
//
// This is NOT makePortal with a different direction. The horizontal kind
// writes a floordata DOOR (function 1) and the height query follows it
// into the neighbour at the same x/z. Two
// rooms stacked on each other cover the SAME x/z, so a door in each sends the
// query 227 -> 93 -> 227 -> ... forever: the game froze the moment Lara came
// near it ("Game Thread appeared stuck, skipping suspend to avoid a deadlock").
//
// The vertical connection lives in the sector's own bytes instead:
//   byte 4  room below
//   byte 6  room above
// Measured against a hand-built level in FexInspect, where the same
// opening works. There, room 227's sector reads "below 222, above 93"
// and room 93's reads "below 227" - and NOTHING else differs from vanilla:
// same box, same floor and ceiling heights, floordata index still 0, and no
// view window in either room's portal list.
//
// Hence: one byte, and hands off everything else. In particular the OTHER
// link byte stays - overwriting room 227's "below 222" tore out a connection
// Core had put there.
//----------------------------------------------------------------------------
// Shift a whole room ("Move_Room").
//
// What has to move, and why each of them:
//
//   header 20 / 28   the room's origin. Every vertex and every portal corner
//                    of the room is measured from it in x and z, so those
//                    come along by themselves.
//   header 32 / 36   minfloor and maxceiling, the room's height range - the
//                    engine tests against them (IsRoomOutside, control.cpp).
//   sector bytes     floor (+5) and ceiling (+7), in steps of 256. A wall
//                    marker (-127) is no height and is left alone.
//   vertices         y only, and as a float. x and z are relative.
//   own portals      y only, same reason.
//   foreign portals  a window in ANOTHER room onto this one is written in
//                    THAT room's coordinates, and that room did not move -
//                    so its corners take the full shift in all three axes,
//                    or the two halves of the opening drift apart.
//
// Y is quantised to 256 because the sector bytes are, and collision follows
// them: moving the drawing by an odd amount would leave Lara walking on a
// floor that is no longer where it looks.
//
// Items keep their world position - a room move does not carry its contents.
bool Tr5Level::moveRoom(int room, long dx, long dy, long dz, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }
    if (dx % 1024 || dz % 1024)
    { error = "x and z must be whole sectors (1024)"; return false; }
    if (dy % 256)
    { error = "y must be a whole height step (256)"; return false; }
    if (!dx && !dy && !dz) return true;

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset || !r.sectorsOffset)
    { error = "room " + std::to_string(room) + " did not parse"; return false; }
    const size_t hdr = r.headerOffset;

    auto addLong = [&](size_t at, long d)
    {
        int32_t v; std::memcpy(&v, m_bytes.data() + at, 4);
        v += static_cast<int32_t>(d);
        std::memcpy(m_bytes.data() + at, &v, 4);
    };
    addLong(hdr + 20, dx);
    addLong(hdr + 28, dz);
    addLong(hdr + 32, dy);
    addLong(hdr + 36, dy);

    if (dy)
    {
        const int steps = static_cast<int>(dy / 256);
        for (int x = 0; x < r.numXSectors; ++x)
            for (int z = 0; z < r.numZSectors; ++z)
            {
                const size_t at = r.sectorsOffset +
                    (static_cast<size_t>(x) * r.numZSectors + z) * 8;
                for (int b = 5; b <= 7; b += 2)
                {
                    const int v = static_cast<int8_t>(m_bytes[at + b]);
                    if (v == -127) continue;                  // a wall, no height
                    const int n = v + steps;
                    if (n < -126 || n > 127)
                    { error = "the move would push a floor or ceiling out of range"; return false; }
                    m_bytes[at + b] = static_cast<uint8_t>(static_cast<int8_t>(n));
                }
            }

        // Every vertex of the room, y only.
        int32_t nRoomlets; std::memcpy(&nRoomlets, m_bytes.data() + hdr + 168, 4);
        uint32_t oRoomlets, oVtx;
        std::memcpy(&oRoomlets, m_bytes.data() + hdr + 172, 4);
        std::memcpy(&oVtx,      m_bytes.data() + hdr + 176, 4);
        if (nRoomlets > 0 && nRoomlets <= 4096 && !(oRoomlets & 1) && !(oVtx & 1))
        {
            const size_t body = hdr + 208;
            size_t vtxAt = body + oVtx;
            for (int i = 0; i < nRoomlets; ++i)
            {
                int16_t nVtx;
                std::memcpy(&nVtx, m_bytes.data() + body + oRoomlets + static_cast<size_t>(i) * 56, 2);
                for (int k = 0; k < nVtx; ++k)
                {
                    const size_t at = vtxAt + static_cast<size_t>(k) * 28 + 4;
                    float f; std::memcpy(&f, m_bytes.data() + at, 4);
                    f += static_cast<float>(dy);
                    std::memcpy(m_bytes.data() + at, &f, 4);
                }
                vtxAt += static_cast<size_t>(nVtx) * 28;
            }
        }
    }

    // The room's own windows: y only, x and z ride along with the origin.
    // Windows in other rooms onto this one: all three, they did not move.
    for (size_t q = 0; q < m_rooms.size(); ++q)
    {
        const Tr5Room& o = m_rooms[q];
        if (!o.sectorsOffset) continue;
        const bool self = (static_cast<int>(q) == room);
        if (!self)
        {
            bool touches = false;
            for (int p : o.portalRooms) if (p == room) { touches = true; break; }
            if (!touches) continue;
        }

        const size_t end = o.sectorsOffset +
            static_cast<size_t>(o.numXSectors) * o.numZSectors * 8;
        uint16_t n; std::memcpy(&n, m_bytes.data() + end, 2);
        for (uint16_t i = 0; i < n; ++i)
        {
            const size_t at = end + 2 + static_cast<size_t>(i) * 32;
            uint16_t to; std::memcpy(&to, m_bytes.data() + at, 2);
            if (!self && to != static_cast<uint16_t>(room)) continue;
            for (int k = 0; k < 4; ++k)
                for (int c = 0; c < 3; ++c)
                {
                    const long d = self ? ((c == 1) ? dy : 0)
                                        : ((c == 0) ? dx : (c == 1) ? dy : dz);
                    if (!d) continue;
                    const size_t w = at + 8 + static_cast<size_t>(k) * 6 + static_cast<size_t>(c) * 2;
                    int16_t v; std::memcpy(&v, m_bytes.data() + w, 2);
                    v = static_cast<int16_t>(v + d);
                    std::memcpy(m_bytes.data() + w, &v, 2);
                }
        }
    }

    // Only a VERTICAL move breaks the Remaster's geometry. A TRG's
    // vertices are room-local and ride along with the origin, so dx and dz
    // change nothing about them - dy is written into every vertex above and
    // is exactly what the HD art would not follow.
    if (dy) hdMarkRoom(room);

    // The origins changed, so the room table has to be read again.
    return reparse(error);
}

//----------------------------------------------------------------------------
// Is there already a view window at THIS sector, onto that room?
//
// A window belongs to the sector it opens, and a pair of rooms can be joined
// at several of them. The test is where the record lies: its corners have to
// sit on the sector's own square of the world. The one-unit inset of a
// window means a coordinate may fall a single unit outside, so the square is
// widened by one.
bool Tr5Level::windowAt(int room, int sx, int sz, int targetRoom) const
{
    std::vector<PortalRecord> recs;
    std::string err;
    if (!portalRecords(room, recs, err)) return false;
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return false;
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];

    const long x0 = r.originX + static_cast<long>(sx) * 1024 - 1, x1 = x0 + 1026;
    const long z0 = r.originZ + static_cast<long>(sz) * 1024 - 1, z1 = z0 + 1026;

    for (size_t i = 0; i < recs.size(); ++i)
    {
        if (recs[i].toRoom != targetRoom) continue;
        bool inside = true;
        for (int k = 0; k < 4 && inside; ++k)
        {
            const long wx = recs[i].v[k][0] + r.originX;
            const long wz = recs[i].v[k][2] + r.originZ;
            if (wx < x0 || wx > x1 || wz < z0 || wz > z1) inside = false;
        }
        if (inside) return true;
    }
    return false;
}

bool Tr5Level::makeVerticalPortal(int room, int sx, int sz, int targetRoom,
                                  bool ceiling, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }
    if (targetRoom < 0 || targetRoom > 254)
    { error = "target room " + std::to_string(targetRoom) +
              " cannot be reached through a sector byte (0..254)"; return false; }
    if (targetRoom >= static_cast<int>(m_rooms.size()))
    { error = "target room " + std::to_string(targetRoom) + " does not exist"; return false; }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.sectorsOffset) { error = "sector field did not parse"; return false; }
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "sector outside room " + std::to_string(room); return false; }

    const size_t at = r.sectorsOffset +
        (static_cast<size_t>(sx) * r.numZSectors + sz) * 8 + (ceiling ? 6 : 4);
    if (at >= m_bytes.size()) { error = "sector lies outside the file"; return false; }

    const int had = m_bytes[at];
    if (had == targetRoom) return true;              // repeatable
    if (had != 255)
    {
        error = "the sector already leads " + std::string(ceiling ? "up" : "down") +
                " into room " + std::to_string(had) +
                " - overwriting that would tear out a connection of Core's";
        return false;
    }

    m_bytes[at] = static_cast<uint8_t>(targetRoom);

    // A floor or ceiling opening can now be passed through, and the face
    // that used to close it is still drawn in the Remaster's geometry -
    // over THIS sector and nowhere else.
    //
    // The vertical kind writes no view window and no portal record, so there
    // is nothing to read the plane back out of. It comes from the sector's
    // own height byte, which is the plane the link was opened in: byte 7 is
    // the ceiling, byte 5 the floor, both in units of 256. A wall carries
    // -127 there and is no height at all; then the sector's other byte
    // stands in, and if that is a wall too the room's floor does. Each of
    // the three is a plane inside this sector's column, and half a sector of
    // padding above and below reaches the art of any of them - which is why
    // a fallback is honest here and would not be for the window above.
    const size_t sec = r.sectorsOffset +
        (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    const int wanted = static_cast<int8_t>(m_bytes[sec + (ceiling ? 7 : 5)]);
    const int other  = static_cast<int8_t>(m_bytes[sec + (ceiling ? 5 : 7)]);
    const long planeY = (wanted != -127) ? static_cast<long>(wanted) * 256
                      : (other != -127) ? static_cast<long>(other) * 256
                      : static_cast<long>(r.yBottom);

    const long wx = r.originX + static_cast<long>(sx) * 1024;
    const long wz = r.originZ + static_cast<long>(sz) * 1024;
    hdMarkCutBox(wx, planeY, wz, wx + 1024, planeY, wz + 1024, 1);
    return true;
}

bool Tr5Level::makePortal(int room, int sx, int sz,
                          int targetRoom, int tsx, int tsz, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }
    if (targetRoom < 0 || targetRoom >= static_cast<int>(m_rooms.size()))
    { error = "target room " + std::to_string(targetRoom) + " does not exist"; return false; }

    // The opening is marked once the window is proven to exist, not before:
    // an opening cannot be recorded before it exists. The mark sits on the
    // one path every successful call takes, whether the window had to be
    // built or was already in the file, and a failed call aborts the whole
    // run so nothing half-recorded is written out.

    // No view window yet? Then build one, in both directions. This
    // happens FIRST, before any reference into m_rooms is taken: creating a
    // window reparses the file, and every reference would dangle after it.
    {
        const Tr5Room& pre = m_rooms[static_cast<size_t>(room)];

        // Per SECTOR, not per room pair. Two rooms may be joined at several
        // sectors and each opening needs its own window - Core's joby3 has
        // two between rooms 76 and 10. Asking only "is there a window to
        // that room" meant the second and third sector of Andrea3's widened
        // opening got a passage but no window, so one looked at a wall that
        // was no longer there.
        bool have = windowAt(room, sx, sz, targetRoom);
        if (!have && !addViewWindow(room, sx, sz, targetRoom, tsx, tsz, error))
        {
            error = "no view window between room " + std::to_string(room) + " and " +
                    std::to_string(targetRoom) + ", and none could be built: " + error;
            return false;
        }
    }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    const Tr5Room& z = m_rooms[static_cast<size_t>(targetRoom)];
    if (!r.sectorsOffset || !z.sectorsOffset)
    { error = "sector field did not parse"; return false; }
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "sector outside the room"; return false; }

    // By now the window exists - either it always did, or it was just built.
    // Checked again rather than assumed: without it the opening is a black
    // hole in the picture, and that is worth one comparison.
    if (!windowAt(room, sx, sz, targetRoom))
    {
        error = "room " + std::to_string(room) + " still has no view window at (x" +
                std::to_string(sx) + ",z" + std::to_string(sz) + ") to " +
                std::to_string(targetRoom) + " after building one";
        return false;
    }

    // And the reason the check above is worth its comparison twice over:
    // the window is there, so the box of the opening
    // can be read out of it. Only this room's side - the caller opens the
    // other direction with its own call, and addViewWindow has already
    // recorded both sides where it built them. All three readings of one
    // opening give the same box and the set keeps one.
    hdMarkCutFromWindow(room, sx, sz, targetRoom);

    // Find the overlapped sector of the target room: same world area.
    const int worldX = r.originX + sx * 1024;
    const int worldZ = r.originZ + sz * 1024;
    const int osx = (worldX - z.originX) / 1024;
    const int osz = (worldZ - z.originZ) / 1024;
    if (osx < 0 || osx >= z.numXSectors || osz < 0 || osz >= z.numZSectors)
    { error = "target room does not overlap the sector"; return false; }

    const size_t ownAt    = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    const size_t acrossAt = z.sectorsOffset + (static_cast<size_t>(osx) * z.numZSectors + osz) * 8;
    if (ownAt + 8 > m_bytes.size() || acrossAt + 8 > m_bytes.size())
    { error = "sector lies outside the file"; return false; }

    // The overlapped sector must be walkable - a wall as template would
    // mean the portal leads into the massif.
    if (static_cast<int8_t>(m_bytes[acrossAt + 5]) == -127)
    { error = "the overlapped sector in the target room is itself a wall"; return false; }

    // Is this sector on the room's BORDER RING? That, and not the rooms'
    // heights, is what says whether the two lie side by side or on top of
    // each other.
    //
    // The first attempt compared yBottom and the world column, and called
    // Andrea3's rooms 1 and 74 stacked. They are not: room 74 is a tall
    // shaft STANDING NEXT to room 1, and a low yBottom only means it is
    // tall. Every room's border ring overlaps its horizontal neighbour's
    // interior - that overlap IS the normal arrangement for a wall portal,
    // so a shared world column proves nothing.
    //
    // A sector in the room's interior, on the other hand, has no horizontal
    // neighbour to reach: the only room it can touch there is one above or
    // below. A floordata door would then point at the same x/z in a room
    // covering the same ground, and the height query ping-pongs between the
    // two until the game locks up (andy1, "Game Thread appeared stuck").
    if (sx != 0 && sx != r.numXSectors - 1 &&
        sz != 0 && sz != r.numZSectors - 1)
    {
        error = "sector (x" + std::to_string(sx) + ",z" + std::to_string(sz) +
                ") lies inside room " + std::to_string(room) +
                ", not on its border ring, so room " + std::to_string(targetRoom) +
                " can only be above or below it - a Make_Portal would lock the game "
                "up. Use Make_Ceiling_Portal or Make_Floor_Portal.";
        return false;
    }

    // Read the existing chain; geometry (TILT/ROOF/SPLITs/NOCOLs) stays.
    uint16_t old;
    std::memcpy(&old, m_bytes.data() + ownAt, 2);
    std::vector<Tr5FloorEntry> chain;
    if (old && !readChain(old, chain))
    { error = "floordata chain not readable"; return false; }

    std::vector<Tr5FloorEntry> fresh;
    for (const Tr5FloorEntry& e : chain)
    {
        if (e.function == 1)                              // already a portal?
        {
            if (e.words.size() >= 2 && e.words[1] == static_cast<uint16_t>(targetRoom))
                return true;                              // exactly this one: done
            error = "sector already has a portal into another room";
            return false;
        }
        if (e.function == 2 || e.function == 3 ||
            (e.function >= 7 && e.function <= 18))
            fresh.push_back(e);
        // everything else (trigger, climb, ...) has no business on a portal
        // sector and drops away - vanilla never carries such things there.
    }

    Tr5FloorEntry door;
    door.function = 1;
    door.words.push_back(0x0001);
    door.words.push_back(static_cast<uint16_t>(targetRoom));
    fresh.push_back(door);

    uint16_t idx = 0;
    if (!appendChain(fresh, idx, error)) return false;
    setSectorIndex(r, sx, sz, idx);

    // Box word: fx=6, box=2047 (none), stopper=0 - like all portal sectors.
    const uint16_t boxWord = 0x7FF6;
    std::memcpy(m_bytes.data() + ownAt + 2, &boxWord, 2);

    // Floor and ceiling from the overlapped sector, so the step through is
    // level - but ONLY those two. Bytes 4 and 6 are the room-below and
    // room-above links, and copying them carried the neighbour's pit into
    // this sector: joby3's room 76 came out claiming a drop into room 4.
    // A hand-built wall portal (andy1 room 0, sector 45) keeps them at 255.
    m_bytes[ownAt + 5] = m_bytes[acrossAt + 5];
    m_bytes[ownAt + 7] = m_bytes[acrossAt + 7];
    return true;
}

//----------------------------------------------------------------------------
// Make a sector climbable (Make_Climb).
//
// A CLIMB entry is ONE word: function 6 in bits 0-4, the climbable wall
// sides in bits 8-11. The engine checks in _TestTriggers
// (control.cpp): quad = (yRot + 0x2000) / 0x4000, bit = 1 << (quad+8)
// - 0 degrees -> 0x100, 90 -> 0x200, 180 -> 0x400, 270 -> 0x800. The angle
// is Lara's VIEWING direction onto the wall, the same convention as with
// the wall grabs.
//
// The CHAIN ORDER is a rule, not taste: _TestTriggers reads from
// trigger_index in order [LAVA] -> [CLIMB] -> rest. A CLIMB behind the
// trigger would be misread as a trigger command, one before the LAVA would
// leave the LAVA unread. Hence the chain is reassembled: geometry, then
// LAVA, then CLIMB, then everything else in the old order.
//
// Repeatable: existing CLIMB bits are ORed, a second call with the same
// bits changes nothing.
//----------------------------------------------------------------------------
bool Tr5Level::makeClimb(int room, int sx, int sz, int dirBits, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.sectorsOffset) { error = "sector field did not parse"; return false; }
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "sector outside the room"; return false; }
    if ((dirBits & ~0x0F00) || !(dirBits & 0x0F00))
    { error = "climb bits must lie in 0x100..0x800"; return false; }

    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    uint16_t old;
    std::memcpy(&old, m_bytes.data() + at, 2);

    std::vector<Tr5FloorEntry> chain;
    if (old && !readChain(old, chain))
    { error = "floordata chain not readable"; return false; }

    std::vector<Tr5FloorEntry> geo, lava, rest;
    Tr5FloorEntry* climb = nullptr;
    Tr5FloorEntry climbOld;
    for (Tr5FloorEntry& e : chain)
    {
        if (e.function == 6)      { climbOld = e; climb = &climbOld; }
        else if (e.function == 5) lava.push_back(e);
        else if (e.function == 1 || e.function == 2 || e.function == 3 ||
                 (e.function >= 7 && e.function <= 18))
            geo.push_back(e);
        else
            rest.push_back(e);
    }

    if (climb)
    {
        if ((climb->words[0] & 0x0F00 & dirBits) == static_cast<uint16_t>(dirBits))
            return true;                              // bits already set
        climb->words[0] |= static_cast<uint16_t>(dirBits);
    }
    else
    {
        climbOld = Tr5FloorEntry();
        climbOld.function = 6;
        climbOld.words.push_back(static_cast<uint16_t>(0x0006 | dirBits));
        climb = &climbOld;
    }

    std::vector<Tr5FloorEntry> fresh;
    for (const Tr5FloorEntry& e : geo)  fresh.push_back(e);
    for (const Tr5FloorEntry& e : lava) fresh.push_back(e);
    fresh.push_back(*climb);
    for (const Tr5FloorEntry& e : rest) fresh.push_back(e);

    uint16_t idx = 0;
    if (!appendChain(fresh, idx, error)) return false;
    setSectorIndex(r, sx, sz, idx);
    return true;
}

//----------------------------------------------------------------------------
// Set or clear a room's water bit (Make_Room_Water / Make_Room_Nowater).
//
// ROOM_UNDERWATER is bit 0 of the room flags (global/types.h), in the
// room header at +78. 44 engine sites hang off it: Lara's swim state,
// camera, sound, AI paths, fire. Flipping it is therefore a real
// intervention and not optics - the room truly becomes a water room.
//
// Repeatable: if the bit already stands as desired, nothing happens.
//----------------------------------------------------------------------------
bool Tr5Level::setRoomWater(int room, bool on, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }

    Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset) { error = "room header did not parse"; return false; }

    constexpr size_t OFS_FLAGS = 78;
    const size_t at = r.headerOffset + OFS_FLAGS;
    if (at + 2 > m_bytes.size()) { error = "room header lies outside the file"; return false; }

    uint16_t f;
    std::memcpy(&f, m_bytes.data() + at, 2);
    const uint16_t nw = on ? static_cast<uint16_t>(f | 0x0001)
                           : static_cast<uint16_t>(f & ~0x0001);
    if (nw == f) return true;                     // already stands that way

    std::memcpy(m_bytes.data() + at, &nw, 2);
    r.flags = nw;
    return true;
}

//----------------------------------------------------------------------------
// Does Lara's animation <animNr> carry real image data in this level?
//
// Core's stub marker is a FrameSize of zero (the high byte of the
// "interpolation" field, ANIM_STRUCT in global/types.h). The engine
// computes the image address as frame_ptr[n * FrameSize] - at zero it stays
// on the same image forever, and what plays is a frozen pose.
//
// Second case: the size is right, but between its own frame pointer and the
// next larger one there is less space than the keyframes need. Then the
// data there belongs to another animation.
//
// Found via ANIM_KEYCARD in joby3 (22 seconds frozen) and the switch in
// andy3 (wrong, far too short movement). See LaraAnim.
//----------------------------------------------------------------------------
bool Tr5Level::laraAnimUsable(int animNr) const
{
    MoveableInfo mv;
    if (!moveableInfo(0, mv) || mv.animIndex < 0) return false;

    const long long a = static_cast<long long>(mv.animIndex) + animNr;
    if (a < 0 || a >= m_anims.count) return false;

    const size_t o = m_anims.dataAt + static_cast<size_t>(a) * 40;
    const uint8_t size = m_bytes[o + 4 + 1];             // high byte
    if (!size) return false;                             // stub, form 1

    const uint8_t rate = m_bytes[o + 4];
    uint16_t from, to;
    std::memcpy(&from, m_bytes.data() + o + 24, 2);
    std::memcpy(&to, m_bytes.data() + o + 26, 2);
    // Stub, form 2: an empty frame range. joby4 carries its AIRLOCKSW like
    // that - FrameSize 10, but frames 0..0. A donor with this form would be
    // a swap of stub for stub.
    if (to <= from) return false;
    const int keyframes = (rate > 0 && to >= from) ? ((to - from) / rate + 1) : 1;
    const long long needed = static_cast<long long>(keyframes) * size * 2;

    uint32_t own;
    std::memcpy(&own, m_bytes.data() + o, 4);
    uint32_t next = static_cast<uint32_t>(m_frames.count) * 2;
    for (int32_t i = 0; i < m_anims.count; ++i)
    {
        uint32_t z;
        std::memcpy(&z, m_bytes.data() + m_anims.dataAt + static_cast<size_t>(i) * 40, 4);
        if (z > own && z < next) next = z;
    }
    return needed <= static_cast<long long>(next) - own;
}

//----------------------------------------------------------------------------
// Which anim does Lara's anim <animNr> point at with next_animation?
//
// Needed to repair a movement as a CHAIN instead of link by link: the crawl
// roll runs 218 -> 219 -> 220 -> 222, and Core stubs such chains as a
// whole. Returns -1 if the anim does not exist or the successor lies
// outside Lara's set.
//----------------------------------------------------------------------------
int Tr5Level::laraAnimNext(int animNr) const
{
    MoveableInfo mv;
    if (!moveableInfo(0, mv) || mv.animIndex < 0) return -1;

    const long long a = static_cast<long long>(mv.animIndex) + animNr;
    if (a < 0 || a >= m_anims.count) return -1;

    uint16_t next;
    std::memcpy(&next, m_bytes.data() + m_anims.dataAt + static_cast<size_t>(a) * 40 + 28, 2);

    const long long rel = static_cast<long long>(next) - mv.animIndex;
    if (rel < 0 || static_cast<long long>(mv.animIndex) + rel >= m_anims.count) return -1;
    return static_cast<int>(rel);
}

//----------------------------------------------------------------------------
// Sound
//
// Layout, read off TOMB5's LoadSamples (specific/file.cpp):
//
//   In the main block, right behind the items:
//     long  nAIObjs;  AIOBJECT aiObjs[nAIObjs];    (24 bytes per entry)
//     short cinematic;
//     short sample_lut[450];      map -> index into sample_infos, -1 = missing
//     long  num_infos;
//     SAMPLE_INFO sample_infos[num_infos];         (8 bytes: short number,
//                                                   uchar volume, char radius,
//                                                   randomness, pitch, short flags)
//     long  num_samples;
//     ... then an index table the engine NEVER reads ...
//
//   Behind the main block, up to the end of the file:
//     long  num_samples;          the same once more
//     per sample: long uncompSize, long compSize, compSize bytes
//
// That num_samples appears twice is no misreading: LoadSamples first gets
// it with ReadLong() from the buffered main block and right after
// once more via fread() straight from the file, whereby the second
// value overwrites the first. Whoever adjusts only one of the two builds a
// file that looks different depending on the reader.
//----------------------------------------------------------------------------
namespace
{
    constexpr int SAMPLE_LUT_SIZE = 450;   // MAX_SAMPLES, global/types.h
    constexpr int SZ_SAMPLE_INFO = 8;
    constexpr int SZ_AIOBJECT = 24;

    // specific/dxsound.cpp writes DS_Buffers[num] without a range check;
    // the engine therefore aborts with a message from 257 samples on
    // (LoadSamples, file.cpp).
    constexpr int MAX_SAMPLES_PLAYABLE = 256;
}

bool Tr5Level::findSound()
{
    m_soundOk = false;
    if (m_itemCountOffset == 0) return false;

    size_t p = m_itemCountOffset + 4 + static_cast<size_t>(m_itemCount) * 24;
    if (p + 4 > m_bytes.size()) return false;

    int32_t nAI = 0;
    std::memcpy(&nAI, m_bytes.data() + p, 4); p += 4;
    if (nAI < 0) return false;
    p += static_cast<size_t>(nAI) * SZ_AIOBJECT;
    p += 2;                                             // cinematic

    if (p + SAMPLE_LUT_SIZE * 2 + 4 > m_bytes.size()) return false;
    m_lutAt = p;
    m_numInfosAt = p + SAMPLE_LUT_SIZE * 2;
    std::memcpy(&m_nInfos, m_bytes.data() + m_numInfosAt, 4);
    if (m_nInfos <= 0) return false;

    m_infosAt = m_numInfosAt + 4;
    m_numSamplesInMainAt = m_infosAt + static_cast<size_t>(m_nInfos) * SZ_SAMPLE_INFO;
    if (m_numSamplesInMainAt + 4 > m_bytes.size()) return false;

    m_numSamplesAt = m_mainOffset + m_mainSize;
    if (m_numSamplesAt + 4 > m_bytes.size()) return false;
    std::memcpy(&m_nSamples, m_bytes.data() + m_numSamplesAt, 4);

    // Both copies must say the same. If they do not, one of the offsets is
    // wrong - then rather touch nothing at all.
    int32_t inBlock = 0;
    std::memcpy(&inBlock, m_bytes.data() + m_numSamplesInMainAt, 4);
    if (inBlock != m_nSamples) return false;

    m_soundOk = true;
    return true;
}

void Tr5Level::shiftSound(size_t bytes, size_t fromOffset)
{
    if (!m_soundOk || bytes == 0) return;
    if (m_lutAt >= fromOffset)              m_lutAt += bytes;
    if (m_numInfosAt >= fromOffset)         m_numInfosAt += bytes;
    if (m_infosAt >= fromOffset)            m_infosAt += bytes;
    if (m_numSamplesInMainAt >= fromOffset) m_numSamplesInMainAt += bytes;
    if (m_numSamplesAt >= fromOffset)       m_numSamplesAt += bytes;
}

int Tr5Level::soundInfoIndex(int map) const
{
    if (!m_soundOk || map < 0 || map >= SAMPLE_LUT_SIZE) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_lutAt + static_cast<size_t>(map) * 2, 2);
    return (v < 0 || v >= m_nInfos) ? -1 : static_cast<int>(v);
}

bool Tr5Level::copySoundFrom(const Tr5Level& src, int srcMap, int dstMap, std::string& error)
{
    char buf[256];

    if (!m_soundOk) { error = "target level: sound block not parseable"; return false; }
    if (!src.m_soundOk) { error = "source level: sound block not parseable"; return false; }
    if (dstMap < 0 || dstMap >= SAMPLE_LUT_SIZE) { error = "target map outside 0..449"; return false; }

    if (soundInfoIndex(dstMap) >= 0) return true;      // already there, nothing to do

    const int first = src.soundInfoIndex(srcMap);
    if (first < 0)
    {
        std::snprintf(buf, sizeof(buf), "source level does not have map %d", srcMap);
        error = buf;
        return false;
    }

    // Random variants: N consecutive SAMPLES, N in bits 2-5 of the info's
    // flags - the engine rolls sample = number + random(N)
    // (SoundEffect, game/sound.cpp). So it is ONE info with N samples, NOT N infos:
    // the old reading here copied the NEIGHBOR infos' samples (= foreign
    // sounds) along as "variants" - in the target the engine then rolled
    // between the right sound and foreign ones, with the right one's flags
    // (heard in game: TwoGun randomly plays his shot instead of the
    // footsteps while walking, gladiator/Roman god a high-pitch scream on
    // being hit - all multi-variant sounds).
    int16_t flags = 0;
    std::memcpy(&flags, src.m_bytes.data() + src.m_infosAt + static_cast<size_t>(first) * SZ_SAMPLE_INFO + 6, 2);
    int variants = (static_cast<int>(static_cast<uint16_t>(flags)) >> 2) & 0xF;
    if (variants <= 0) variants = 1;

    int16_t number = 0;
    std::memcpy(&number, src.m_bytes.data() + src.m_infosAt + static_cast<size_t>(first) * SZ_SAMPLE_INFO, 2);
    if (number < 0 || number + variants > src.m_nSamples)
    { error = "info points at samples that do not exist"; return false; }

    if (m_nSamples + variants > MAX_SAMPLES_PLAYABLE)
    {
        std::snprintf(buf, sizeof(buf),
            "level would have %d samples - the engine loads at most %d",
            m_nSamples + variants, MAX_SAMPLES_PLAYABLE);
        error = buf;
        return false;
    }

    // Collect the source samples number..number+variants-1 in one pass
    // before anything is written.
    std::vector<std::vector<uint8_t>> infos;
    std::vector<std::pair<int32_t, std::vector<uint8_t>>> samples;
    {
        size_t q = src.m_numSamplesAt + 4;
        for (int32_t i = 0; i < src.m_nSamples && static_cast<int>(samples.size()) < variants; ++i)
        {
            if (q + 8 > src.m_bytes.size()) break;
            int32_t uc = 0, cs = 0;
            std::memcpy(&uc, src.m_bytes.data() + q, 4);
            std::memcpy(&cs, src.m_bytes.data() + q + 4, 4);
            q += 8;
            if (cs < 0 || q + static_cast<size_t>(cs) > src.m_bytes.size()) break;
            if (i >= number)
                samples.push_back({ uc, std::vector<uint8_t>(src.m_bytes.begin() + q,
                                                             src.m_bytes.begin() + q + cs) });
            q += static_cast<size_t>(cs);
        }
        if (static_cast<int>(samples.size()) != variants)
        { error = "cannot find the source's sample data"; return false; }
    }

    {
        const size_t o = src.m_infosAt + static_cast<size_t>(first) * SZ_SAMPLE_INFO;
        std::vector<uint8_t> info(src.m_bytes.begin() + o, src.m_bytes.begin() + o + SZ_SAMPLE_INFO);
        const int16_t newNumber = static_cast<int16_t>(m_nSamples);
        std::memcpy(info.data(), &newNumber, 2);        // point at the new slot
        infos.push_back(info);
    }

    // --- Insert the infos. They lie IN the main block, so it grows. -------
    const size_t insertAt = m_infosAt + static_cast<size_t>(m_nInfos) * SZ_SAMPLE_INFO;
    std::vector<uint8_t> raw;
    for (const std::vector<uint8_t>& i : infos) raw.insert(raw.end(), i.begin(), i.end());
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(insertAt), raw.begin(), raw.end());

    const int16_t newLut = static_cast<int16_t>(m_nInfos);
    std::memcpy(m_bytes.data() + m_lutAt + static_cast<size_t>(dstMap) * 2, &newLut, 2);

    m_nInfos += 1;      // ONE info; the variants are samples, not infos
    std::memcpy(m_bytes.data() + m_numInfosAt, &m_nInfos, 4);

    // Everything behind the insertion point has moved.
    shiftSound(raw.size(), insertAt);
    if (m_itemCountOffset >= insertAt) m_itemCountOffset += raw.size();

    m_mainSize += raw.size();
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    m_nSamples += variants;
    std::memcpy(m_bytes.data() + m_numSamplesInMainAt, &m_nSamples, 4);
    std::memcpy(m_bytes.data() + m_numSamplesAt, &m_nSamples, 4);

    // --- Sample data at the end of the file -------------------------------
    for (const auto& s : samples)
    {
        const int32_t uc = s.first, cs = static_cast<int32_t>(s.second.size());
        const uint8_t* pu = reinterpret_cast<const uint8_t*>(&uc);
        const uint8_t* pc = reinterpret_cast<const uint8_t*>(&cs);
        m_bytes.insert(m_bytes.end(), pu, pu + 4);
        m_bytes.insert(m_bytes.end(), pc, pc + 4);
        m_bytes.insert(m_bytes.end(), s.second.begin(), s.second.end());
    }
    return true;
}

bool Tr5Level::reparse(std::string& error)
{
    // After an insertion ALL remembered offsets are history. Instead of
    // adjusting each one - and forgetting one - the file is simply
    // reinterpreted. It sits in memory, that costs nothing.
    if (!readHeader(error)) return false;
    return findItems(error);
}

bool Tr5Level::moveableInfo(int slotId, MoveableInfo& out) const
{
    for (int32_t i = 0; i < m_objects.count; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * 20;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot != slotId) continue;

        int16_t v16; uint16_t u16v; int32_t v32; uint32_t u32v;
        std::memcpy(&v16, m_bytes.data() + o + 4, 2);  out.nmeshes = v16;
        std::memcpy(&v16, m_bytes.data() + o + 6, 2);  out.meshIndex = v16;
        std::memcpy(&v32, m_bytes.data() + o + 8, 4);  out.boneIndex = v32;
        std::memcpy(&u32v, m_bytes.data() + o + 12, 4); out.frameOffset = u32v;
        std::memcpy(&u16v, m_bytes.data() + o + 16, 2);
        out.animIndex = (u16v == 0xFFFF) ? -1 : static_cast<int>(u16v);
        out.slot = slotId;
        return true;
    }
    return false;
}

bool Tr5Level::frameBounds(int slotId, int16_t out[6]) const
{
    for (int k = 0; k < 6; ++k) out[k] = 0;

    MoveableInfo mi;
    if (!moveableInfo(slotId, mi)) return false;

    // frameOffset is a BYTE offset into the frame block, the block itself is
    // counted in words - hence the /2. Only an even offset can name a frame;
    // an odd one would mean the pointer is not a frame pointer at all.
    if (mi.frameOffset & 1u) return false;
    const long long w = static_cast<long long>(mi.frameOffset) / 2;
    if (w < 0 || w + 6 > m_frames.count) return false;

    for (int k = 0; k < 6; ++k) out[k] = debugReadFrameShort(w + k);
    return true;
}

bool Tr5Level::sectorCeilingBase(int room, int sx, int sz, long& y) const
{
    y = 0;
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return false;

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (r.sectorsOffset == 0) return false;
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return false;

    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    if (at + 8 > m_bytes.size()) return false;

    const int ceilByte = static_cast<int8_t>(m_bytes[at + 7]);
    if (ceilByte == -127) return false;                  // NO_HEIGHT: wall
    y = static_cast<long>(ceilByte) * 256;
    return true;
}

bool Tr5Level::sectorCeilingLow(int room, int sx, int sz, long& y) const
{
    y = 0;

    // Step 1: up through the ceiling portals, as GetCeiling does. The step
    // limit is a cycle brake, nothing more - no vanilla chain is longer than
    // a handful of rooms.
    //
    // One thing of the engine's is missing here on purpose: GetCeiling
    // breaks the walk when CheckNoColCeilingTriangle answers for the
    // STARTING sector (control.cpp). That case needs the exact point,
    // not the sector, and this function answers about the whole sector.
    // Where it applies, the walk goes one room too far and the answer comes
    // out generous - which is the harmless direction for the caller this was
    // written for: an enemy is then allowed somewhere the engine would have
    // called just as tight, never barred from somewhere it is roomy.
    for (int step = 0; step < 64; ++step)
    {
        if (room < 0 || room >= static_cast<int>(m_rooms.size())) return false;
        const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
        if (r.sectorsOffset == 0) return false;
        if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return false;

        const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
        if (at + 8 > m_bytes.size()) return false;

        const int sky = m_bytes[at + 6];
        if (sky == 255) break;                            // NO_ROOM: this is the ceiling
        if (sky < 0 || sky >= static_cast<int>(m_rooms.size())) break;

        // Same world column, different room - and the rooms have different
        // origins, so the sector indices have to be recomputed.
        const int wx = r.originX + sx * 1024;
        const int wz = r.originZ + sz * 1024;
        const Tr5Room& up = m_rooms[static_cast<size_t>(sky)];
        const int nx = (wx - up.originX) / 1024;
        const int nz = (wz - up.originZ) / 1024;
        if (nx < 0 || nx >= up.numXSectors || nz < 0 || nz >= up.numZSectors) break;

        room = sky; sx = nx; sz = nz;
    }

    // Step 2: the lowest of the four corners of the sector we ended on.
    bool any = false;
    long low = 0;
    for (int cx = 0; cx <= 1024; cx += 1024)
        for (int cz = 0; cz <= 1024; cz += 1024)
        {
            long c = 0;
            if (!cornerCeiling(room, sx, sz, cx, cz, c) &&
                !sectorCeilingBase(room, sx, sz, c)) continue;
            if (!any || c > low) { low = c; any = true; }
        }

    y = low;
    return any;
}

bool Tr5Level::adoptAnimData(int dstSlot, int dstOffset, int srcSlot,
                                   int srcOffset, std::string& error)
{
    MoveableInfo dst, src;
    if (!moveableInfo(dstSlot, dst) || dst.animIndex < 0)
    { error = "target slot missing or carries no anims"; return false; }
    if (!moveableInfo(srcSlot, src) || src.animIndex < 0)
    { error = "source slot missing or carries no anims"; return false; }

    const long long d = static_cast<long long>(dst.animIndex) + dstOffset;
    const long long s = static_cast<long long>(src.animIndex) + srcOffset;
    if (d < 0 || d >= m_anims.count || s < 0 || s >= m_anims.count)
    { error = "anim lies outside the table"; return false; }

    uint8_t* dr = m_bytes.data() + m_anims.dataAt + static_cast<size_t>(d) * 40;
    const uint8_t* sr = m_bytes.data() + m_anims.dataAt + static_cast<size_t>(s) * 40;

    // Secure the template equality of the family sets: both anims must
    // carry the same frame range, or keyframe counting and the commands'
    // frame numbers will not match.
    uint16_t dFrom, dTo, sFrom, sTo;
    std::memcpy(&dFrom, dr + 24, 2); std::memcpy(&dTo, dr + 26, 2);
    std::memcpy(&sFrom, sr + 24, 2); std::memcpy(&sTo, sr + 26, 2);
    if (dFrom != sFrom || dTo != sTo)
    { error = "the two anims' frame ranges differ"; return false; }

    std::memcpy(dr, sr, 6);           // frame pointer, rate, frame_size
    std::memcpy(dr + 8, sr + 8, 16);  // speed/acceleration
    std::memcpy(dr + 36, sr + 36, 4); // command count + offset (global block)
    return true;
}

std::vector<int> Tr5Level::moveableSlots() const
{
    std::vector<int> out;
    for (int32_t i = 0; i < m_objects.count; ++i)
    {
        int32_t slot;
        std::memcpy(&slot, m_bytes.data() + m_objects.dataAt + static_cast<size_t>(i) * 20, 4);
        out.push_back(slot);
    }
    return out;
}

int32_t Tr5Level::debugReadBoneLong(long long i) const
{
    if (i < 0 || i >= m_bones.count) return 0;
    int32_t v;
    std::memcpy(&v, m_bytes.data() + m_bones.dataAt + static_cast<size_t>(i) * 4, 4);
    return v;
}

int16_t Tr5Level::debugReadFrameShort(long long i) const
{
    if (i < 0 || i >= m_frames.count) return 0;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_frames.dataAt + static_cast<size_t>(i) * 2, 2);
    return v;
}

bool Tr5Level::debugMeshHeader(int ptrIndex, int16_t out[6]) const
{
    if (ptrIndex < 0 || ptrIndex >= m_meshPtrs.count) return false;
    uint32_t ofs;
    std::memcpy(&ofs, m_bytes.data() + m_meshPtrs.dataAt + static_cast<size_t>(ptrIndex) * 4, 4);
    if (ofs + 12 > static_cast<uint32_t>(m_meshData.count) * 2) return false;
    std::memcpy(out, m_bytes.data() + m_meshData.dataAt + ofs, 12);
    return true;
}

//----------------------------------------------------------------------------
// LaraType: the outfit byte. It lies in the 32 bytes between the texture
// blocks and the main block (LaraType u16, WeatherType u16, the rest
// padding) - exactly the area readHeader skips with "+ 32".
//----------------------------------------------------------------------------
int Tr5Level::laraType() const
{
    if (m_mainOffset < 40) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_mainOffset - 8 - 32, 2);
    return v;
}

bool Tr5Level::setLaraType(int value)
{
    if (m_mainOffset < 40) return false;
    const int16_t v = static_cast<int16_t>(value);
    std::memcpy(m_bytes.data() + m_mainOffset - 8 - 32, &v, 2);
    return true;
}

//----------------------------------------------------------------------------
// data[2] of the same 32 bytes: "speech heads do not match the outfit". In
// dialogue cutscenes the engine swaps Lara's head for LARA_SPEECH_HEAD1-4
// (deltapak.cpp, handle_lara_chatting) - they carry the level's vanilla
// outfit. If the outfit donor can supply no speech heads of its own (rich2:
// the VCI levels know no dialogue cutscenes), the engine skips the swap
// with the flag set. In vanilla this is 0 in all 16 files.
//----------------------------------------------------------------------------
int Tr5Level::foreignTalkHeads() const
{
    if (m_mainOffset < 40) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_mainOffset - 8 - 32 + 4, 2);
    return v;
}

bool Tr5Level::setForeignTalkHeads(int value)
{
    if (m_mainOffset < 40) return false;
    const int16_t v = static_cast<int16_t>(value);
    std::memcpy(m_bytes.data() + m_mainOffset - 8 - 32 + 4, &v, 2);
    return true;
}

//----------------------------------------------------------------------------
// data[3] of the same 32 bytes: the HD outfit marker.
//
// The engine's HD path needs to know which Tomb Raider III outfit the
// wardrobe rolled, and data[0] cannot tell it: LARA_PONYTAIL is written both
// for the Rome body in a Russia level and for every guest, and the file says
// nothing else about it (hd_lara.cpp explains the case at length).
//
// Where the value comes from, and why data[3] and not a fifth file: the
// block is "short data[16]" (specific\file.cpp) and the engine reads all 32
// bytes before it touches anything else, so a byte here is available before
// a single room is loaded. All 17 shipped .trc files carry 0 in data[3]
// through data[15] - measured, not assumed - so the slot beside the speech
// head flag was free.
//
// The value is the engine's own HD_LARA_OUTFIT_*, see Tr5Level.h.
//----------------------------------------------------------------------------
int Tr5Level::hdOutfit() const
{
    if (m_mainOffset < 40) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_mainOffset - 8 - 32 + 6, 2);
    return v;
}

bool Tr5Level::setHdOutfit(int value)
{
    if (m_mainOffset < 40) return false;
    const int16_t v = static_cast<int16_t>(value);
    std::memcpy(m_bytes.data() + m_mainOffset - 8 - 32 + 6, &v, 2);
    return true;
}

//----------------------------------------------------------------------------
// The three marker lists. All three are plain recording - the judgement
// about which of them the engine may use lives in Patch::writeHdMarkers,
// so that this class keeps
// saying only what happened.
//----------------------------------------------------------------------------
void Tr5Level::hdMarkRoom(int room)
{
    if (room >= 0) m_hdRooms.insert(room);
}

//----------------------------------------------------------------------------
// One opening, as a box.
//
// The raw box is whatever the edit itself wrote - a view window's four
// corners, a sector square at the height a link was opened, the corners of
// the faces that were degenerated. It arrives here in WORLD units, in any
// corner order, and leaves as an ascending box, grown along ONE axis and
// shrunk along the other two:
//
//   normalAxis    the axis the opening's normal runs along, 0 x, 1 y, 2 z.
//                 That axis is the one the raw box is FLAT in - a wall
//                 window's four corners all share their x or their z - and
//                 it is the one that needs half a sector of reach OUTWARD,
//                 because the Remaster builds a wall as a solid with
//                 thickness and its faces are not on the classic wall
//                 plane. The other two shrink INWARD by
//                 HD_CUT_IN_PLANE; see the long note in Tr5Level.h for why
//                 that step has to point inward, and what was measured
//                 when it pointed outward.
//   -1            the raw box is not a plane at all but a volume of art
//                 that the edit really took away (two walls of a corner
//                 sector degenerated in one call, say). Then there is no
//                 normal to grow along and no boundary plane of the opening
//                 to keep clear of, so the box is used as it stands.
//
// Nothing is clamped to the room or to the level. A box that reaches past
// the level's own extent costs nothing: the engine tests triangles against
// it, and there are no triangles out there.
//----------------------------------------------------------------------------
void Tr5Level::hdMarkCutBox(long x0, long y0, long z0, long x1, long y1, long z1,
                            int normalAxis)
{
    long lo[3] = { x0, y0, z0 };
    long hi[3] = { x1, y1, z1 };

    HdCut cut;
    for (int i = 0; i < 3; ++i)
    {
        if (lo[i] > hi[i]) { const long t = lo[i]; lo[i] = hi[i]; hi[i] = t; }

        if (normalAxis < 0)
        {
            cut.lo[i] = static_cast<int>(lo[i]);
            cut.hi[i] = static_cast<int>(hi[i]);
            continue;
        }

        if (i == normalAxis)
        {
            cut.lo[i] = static_cast<int>(lo[i] - HD_CUT_OUT_NORMAL);
            cut.hi[i] = static_cast<int>(hi[i] + HD_CUT_OUT_NORMAL);
            continue;
        }

        // Half the extent at most: an opening narrower than 256 units
        // collapses to a plane through its own middle, which still catches
        // everything that crosses it, instead of turning inside out.
        const long half = (hi[i] - lo[i]) / 2;
        const long in   = (half < HD_CUT_IN_PLANE) ? half : HD_CUT_IN_PLANE;
        cut.lo[i] = static_cast<int>(lo[i] + in);
        cut.hi[i] = static_cast<int>(hi[i] - in);
    }
    m_hdCuts.insert(cut);
}

//----------------------------------------------------------------------------
// The view window standing at that sector, as a cut box.
//
// ONE derivation for both ways into makePortal. A run reaches an opening
// either through addViewWindow, which has just written the four corners, or
// past it, because a window was already there - and a box worked out twice
// in two places is a box that can disagree with itself. So both call this,
// it reads the record out of the file, and the two answers are the same
// sixteen bytes by construction.
//
// The record's corners are ROOM LOCAL in x and z (the room's origin has to
// be added) and world in y; its normal names the axis the opening is flat
// in. Both are read rather than recomputed - that record IS the opening as
// far as the engine is concerned, since SetRoomBounds clips the neighbouring
// room to exactly these four corners.
//
// A sector with no matching window records nothing and says nothing: that
// cannot happen on a path that reaches here (makePortal checks for the
// window right before), and inventing a box out of the sector square would
// be the guess this whole function exists to avoid.
//----------------------------------------------------------------------------
void Tr5Level::hdMarkCutFromWindow(int room, int sx, int sz, int targetRoom)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return;
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];

    std::vector<PortalRecord> recs;
    std::string err;
    if (!portalRecords(room, recs, err)) return;

    // The same square windowAt() tests against, widened by the one unit a
    // window is inset by.
    const long x0 = r.originX + static_cast<long>(sx) * 1024 - 1, x1 = x0 + 1026;
    const long z0 = r.originZ + static_cast<long>(sz) * 1024 - 1, z1 = z0 + 1026;

    for (size_t i = 0; i < recs.size(); ++i)
    {
        const PortalRecord& p = recs[i];
        if (p.toRoom != targetRoom) continue;

        long lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
        bool inside = true;
        for (int k = 0; k < 4; ++k)
        {
            const long w[3] = { p.v[k][0] + r.originX, p.v[k][1],
                                p.v[k][2] + r.originZ };
            if (w[0] < x0 || w[0] > x1 || w[2] < z0 || w[2] > z1)
            { inside = false; break; }
            for (int c = 0; c < 3; ++c)
            {
                if (!k || w[c] < lo[c]) lo[c] = w[c];
                if (!k || w[c] > hi[c]) hi[c] = w[c];
            }
        }
        if (!inside) continue;

        // The normal is (+-1, 0, 0) or (0, +-1, 0) or (0, 0, +-1) - built
        // that way by addViewWindow and by every window Core ever wrote -
        // and the axis it runs along is the axis the four corners are flat
        // in.
        const int axis = p.nx ? 0 : (p.ny ? 1 : (p.nz ? 2 : -1));
        const int nrm  = p.nx ? p.nx : (p.ny ? p.ny : p.nz);

        // The one unit back: a window does not sit ON its plane. Where its
        // normal component is +1 it sits one unit in FRONT of it, and where
        // it is -1 it sits on it - measured off vanilla andy1 and written
        // down in addViewWindow's own comment, which is the rule this
        // randomizer builds its windows by.
        //
        // Undoing it is not cosmetic. The two sides of ONE opening are two
        // records with opposite normals, so without this they name two boxes
        // one unit apart, the set cannot see that they are the same opening,
        // and the sidecar carries two lines and the engine tests two boxes
        // for every doorway in the game. With it both sides land on the wall
        // plane itself and one opening costs one line - which is also what a
        // person reading the file expects to see.
        if (axis >= 0 && nrm > 0) { lo[axis] += 1; hi[axis] += 1; }

        hdMarkCutBox(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], axis);
        return;
    }
}

void Tr5Level::hdMarkSoundImport(int soundId)
{
    if (soundId < 0 || soundId >= SAMPLE_LUT_SIZE) return;

    m_hdSoundImports.insert(soundId);
}

void Tr5Level::hdMarkDonor(int slot, int fromSlot, const std::string& level)
{
    if (slot < 0 || level.empty()) return;

    // The same slot can be written twice in one run (a weapon slot filled
    // "only what is missing" and later replaced). Only the LAST write is in
    // the file, so only the last one is true - and remembering both would
    // put two contradictory lines in the sidecar.
    for (size_t i = 0; i < m_hdDonors.size(); ++i)
        if (m_hdDonors[i].slot == slot)
        {
            m_hdDonors[i].fromSlot = (fromSlot < 0) ? slot : fromSlot;
            m_hdDonors[i].level = level;
            return;
        }

    HdDonor d;
    d.slot = slot;
    d.fromSlot = (fromSlot < 0) ? slot : fromSlot;
    d.level = level;
    m_hdDonors.push_back(d);
}

void Tr5Level::hdMarkScale(int slot, int num, int den)
{
    if (slot < 0) return;

    // num 0 (or a nonsense den) is "this copy was not scaled": an earlier
    // mark for the slot would describe bytes that are no longer in the file.
    if (num <= 0 || den <= 0) { m_hdScales.erase(slot); return; }

    m_hdScales[slot] = std::make_pair(num, den);
}

bool Tr5Level::hdScaleOf(int slot, int& num, int& den) const
{
    const auto it = m_hdScales.find(slot);
    if (it == m_hdScales.end()) return false;

    num = it->second.first;
    den = it->second.second;
    return true;
}

//----------------------------------------------------------------------------
// data[1] of the same 32 bytes: the weather. 0 dry, 1 rain, 2 snow.
//
// LoadLevel reads the block as "short data[16]" and takes
// WeatherType = (char)data[1] (specific\file.cpp) - so the value sits two
// bytes behind LaraType. DoWeather (specific\specificfx.cpp) is the only
// reader in the whole engine: 1 calls DoRain, 2 DoSnow, anything else draws
// nothing. The script flag GF_WEATHER exists in gameflow.h and is read
// nowhere.
//----------------------------------------------------------------------------
int Tr5Level::weatherType() const
{
    if (m_mainOffset < 40) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_mainOffset - 8 - 30, 2);
    return v;
}

bool Tr5Level::setWeatherType(int value)
{
    if (m_mainOffset < 40) return false;
    const int16_t v = static_cast<int16_t>(value);
    std::memcpy(m_bytes.data() + m_mainOffset - 8 - 30, &v, 2);
    return true;
}

//----------------------------------------------------------------------------
// In how many rooms weather could be seen at all.
//
// Two conditions have to meet in ONE room, and both are the engine's, not
// an assumption:
//
//   ROOM_OUTSIDE (0x8) - DoRain only creates drops while the global
//     "outside" is set, and that is exactly r->flags & ROOM_OUTSIDE of the
//     camera's room resp. of any drawn room (game\draw.cpp).
//     DoSnow hangs off snow_outside, which is set when a portal
//     points into a ROOM_OUTSIDE room (draw.cpp).
//
//   ROOM_NOT_INSIDE (0x20) - every single drop asks IsRoomOutside for its
//     own position, and that returns "outside" ONLY for a room with
//     ROOM_UNDERWATER or ROOM_NOT_INSIDE (IsRoomOutside, game\control.cpp).
//     Both weather functions then throw away what landed in a water room,
//     so ROOM_NOT_INSIDE is what is left.
//
// Water rooms are dropped for that last reason, flipmap alternatives
// because BuildOutsideTable (game\setup.cpp) skips them when it builds
// the lookup IsRoomOutside walks.
//
// Measured over the 16 vanilla files: no level has ROOM_NOT_INSIDE rooms
// without also having such a room carry ROOM_OUTSIDE, so the strict "both
// in one room" and the loose "somewhere in the level" pick the same six
// playable levels (andy1, Andy2, andy3, joby2, rich2, rich3).
//----------------------------------------------------------------------------
int Tr5Level::weatherRooms() const
{
    constexpr unsigned ROOM_UNDERWATER = 0x1, ROOM_OUTSIDE = 0x8, ROOM_NOT_INSIDE = 0x20;

    std::set<int> flipTargets;
    for (const Tr5Room& r : m_rooms)
        if (r.altRoom >= 0) flipTargets.insert(r.altRoom);

    int n = 0;
    for (const Tr5Room& r : m_rooms)
    {
        if (!(r.flags & ROOM_OUTSIDE)) continue;
        if (!(r.flags & ROOM_NOT_INSIDE)) continue;
        if (r.flags & ROOM_UNDERWATER) continue;
        if (flipTargets.count(r.index)) continue;
        ++n;
    }
    return n;
}

bool Tr5Level::readHeader(std::string& error)
{
    size_t pos = 0;

    if (!readI32(m_bytes, pos, m_version)) { error = "file too short for the version header"; return false; }
    if (!readI16(m_bytes, pos, m_rtPages) ||
        !readI16(m_bytes, pos, m_otPages) ||
        !readI16(m_bytes, pos, m_btPages))
    {
        error = "file too short for the texture page counts";
        return false;
    }

    // THREE texture blocks, not two: 32 bit, 16 bit and a misc block (font
    // and sky). Recomputed on real files, Andrea1 for instance: 17 pages *
    // 4 * 0x10000 = 4456448, then 17 * 2 * 0x10000 = 2228224, then 3 * 4 *
    // 0x10000 = 786432. The engine reads one of them depending on the
    // display format and skips the others.
    //
    // Whether the payload is compressed is not stored as a flag in the file
    // - the engine decides that via a global variable. Instead of guessing,
    // both interpretations are tried; valid is the one after which the main
    // block lies there consistently. Its telltale is unambiguous: it is
    // uncompressed, and then it carries size == compressedSize.
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool compressed = (mode == 0);
        size_t p = pos;
        bool ok = true;

        for (int block = 0; block < 3 && ok; ++block)
        {
            int32_t size = 0, compSize = 0;
            if (!readI32(m_bytes, p, size) || !readI32(m_bytes, p, compSize)) { ok = false; break; }
            if (size < 0 || compSize < 0) { ok = false; break; }
            ok = skip(m_bytes, p, compressed ? compSize : size);
        }
        if (!ok) continue;

        // short data[16] - LaraDrawType, WeatherType and further level switches.
        if (!skip(m_bytes, p, 32)) continue;

        int32_t mainSize = 0, mainComp = 0;
        if (!readI32(m_bytes, p, mainSize) || !readI32(m_bytes, p, mainComp)) continue;
        if (mainSize <= 0 || mainSize != mainComp) continue;
        if (p + static_cast<size_t>(mainSize) > m_bytes.size()) continue;

        // Behind the main block follow the sound samples, which the engine
        // reads directly from the file - a remainder is therefore normal
        // and no sign of a misinterpretation.
        m_mainOffset = p;
        m_mainSize = static_cast<size_t>(mainSize);
        return true;
    }

    error = "header not interpretable - neither compressed nor uncompressed "
            "texture blocks yield a fitting main block. Is this really a "
            "TR5 level file?";
    return false;
}

bool Tr5Level::save(const std::wstring& path, std::string& error) const
{
    if (m_bytes.empty()) { error = "no level loaded"; return false; }

    // Texture workshop: as long as it is dirty and open, the texture
    // blocks in m_bytes carry the OLD state. Rather fail loudly than
    // silently write a level with stale pages (the TR3Insert lesson: nobody
    // reads a warning). Patch::apply closes at the end itself.
    if (m_texOpen && m_texDirty)
    {
        error = "texture workshop still open - call texWorkshopClose() before saving";
        return false;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
    {
        error = "cannot open the target file for writing";
        return false;
    }
    size_t put = std::fwrite(m_bytes.data(), 1, m_bytes.size(), f);
    std::fclose(f);
    if (put != m_bytes.size()) { error = "target file not fully written"; return false; }
    return true;
}

//----------------------------------------------------------------------------
// Give a room that has none a drawn surface ("Make_Room_Geometry").
//
// 89 rooms in Chronicles have no faces at all. Andrea3's room 74 is one of
// them: five by five sectors, four roomlets, and pRmVtx == pRmFace, which
// means the vertex block and the face stream are BOTH zero bytes long. The
// floor data is there, so Lara walks on it and trview draws the sectors, but
// nothing is drawn and you look into black. It only became visible once a
// secret opened a window into the room.
//
// Nothing here touches the floor data. Collision comes from the sectors, and
// the sectors already say what the room looks like. This reads that and lays a
// surface over it, so what you see is exactly what you walk on. A mistake here
// can look wrong; it cannot make a level unplayable.
//
// Not the two height BYTES of a sector, though - those are one number for a
// whole square, and a Core room is sloped and triangulated. Every height here
// is a CORNER height, read out of the floordata with the engine's own formula
// (cornerFloor / cornerCeiling). The rule for what becomes surface, measured
// over 13 levels against the faces Core actually drew (95.4 % of them exact):
//
//     wall(A) = cross section(A) MINUS cross section(B)
//     opening = cross section(A) INTERSECTED WITH cross section(B)
//
// where B is what stands beyond the edge - a sector of our own room, or, past
// a portal square, the sector of the room the portal hands over to. A portal
// square itself gets nothing at all: it is the other room's space.
//
// Measured over the 16 vanilla levels: 76 rooms have pRmVtx == pRmFace. 74 of
// them build (10740 faces, 58 triangles); the other two carry nRoomlets == 0
// and fail the precondition below, as they always did.
//
// Where things sit in a room body, measured across all 2958 rooms of the 17
// vanilla levels:
//
//     pRoomlets                   nRoomlets * 56
//     pRmFace == pRmPrelight      2 * (5*nTri + 6*nQuad)
//     [2 bytes of padding when the room's total nTri is odd]
//     pRmVtx                      28 * total nVtx
//     [padding to the next multiple of 16]
//
// Two things there are not what one would guess. The face stream comes first,
// ahead of the vertices. And pRmPrelight is not a block of its own: it equals
// pRmFace in every single room, the block is empty, and the engine assigns the
// pointer without ever reading it (two lines of drawroom.cpp are the only
// mentions in the whole tree). All the lighting sits in the seventh word of
// each vertex.
//
// Because all three offsets are equal in an empty room, this is an APPEND, not
// an insert, and exactly one header offset moves. The portal insertion's RELOC
// loop must NOT be reused here: it moves every offset at or behind the
// insertion point, and pRmVtx, pRmFace and pRmPrelight are the same number, so
// it would move all three and leave pRmFace pointing into the middle of the
// vertices. Three equal numbers cannot be told apart by a comparison - the
// difference between them is what they mean, not what they are - reusing
// the RELOC loop here would silently corrupt the room.
bool Tr5Level::buildRoomGeometry(int room, int likeRoom,
                                 const std::vector<RoomTexRule>& rules,
                                 std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset || !r.sectorsOffset)
    { error = "room " + std::to_string(room) + " did not parse"; return false; }

    // Marked at the top, before the idempotent way out below. A
    // room named by Make_Room_Geometry has no drawn surface in the vanilla
    // file - which is why the command exists at all - so the Remaster's
    // group for it is empty too and the engine falls back to classic there
    // by itself. The mark says it out loud instead of relying on that, and
    // it costs nothing: a room the player could not see before is not one
    // the HD art has anything to show for.
    hdMarkRoom(room);

    const size_t hdr = r.headerOffset;
    const size_t body = hdr + 208;

    int32_t nRoomlets = 0;
    uint32_t oRoomlets = 0, oVtx = 0, oFace = 0, oPre = 0;
    std::memcpy(&nRoomlets, m_bytes.data() + hdr + 168, 4);
    std::memcpy(&oRoomlets, m_bytes.data() + hdr + 172, 4);
    std::memcpy(&oVtx,      m_bytes.data() + hdr + 176, 4);
    std::memcpy(&oFace,     m_bytes.data() + hdr + 180, 4);
    std::memcpy(&oPre,      m_bytes.data() + hdr + 184, 4);

    // Idempotent on purpose. Patch::apply runs TWICE - once for the level
    // patch and once for the secret patch - and roomMoves and portalAdds only
    // ever come from the second one. A room that already carries geometry is
    // left exactly as it is, the way makePortal returns true for a portal that
    // is already open. Failing here instead would kill the whole run, and
    // precisely in the setting where the room becomes visible at all.
    if (oVtx != oFace) return true;

    // The preconditions that make an append legitimate. Loud rather than
    // clever: if a room is not shaped the way the measurement says, this stops
    // instead of writing something that reads back cleanly and crashes later.
    if (nRoomlets < 1 || nRoomlets > 4096)
    { error = "room " + std::to_string(room) + ": implausible roomlet count"; return false; }
    if (oPre != oFace)
    { error = "room " + std::to_string(room) + ": pRmPrelight is not pRmFace"; return false; }
    if (oFace != oRoomlets + 56u * static_cast<uint32_t>(nRoomlets))
    { error = "room " + std::to_string(room) + ": the roomlets do not end at pRmFace"; return false; }
    if ((oRoomlets | oVtx | oFace) & 1u)
    { error = "room " + std::to_string(room) + ": odd block offset"; return false; }

    for (int i = 0; i < nRoomlets; ++i)
    {
        const size_t at = body + oRoomlets + static_cast<size_t>(i) * 56;
        if (at + 56 > m_bytes.size())
        { error = "room " + std::to_string(room) + ": roomlet outside the file"; return false; }
        int16_t a = 0, b = 0, c = 0;
        std::memcpy(&a, m_bytes.data() + at + 0, 2);
        std::memcpy(&b, m_bytes.data() + at + 6, 2);
        std::memcpy(&c, m_bytes.data() + at + 8, 2);
        if (a || b || c)
        { error = "room " + std::to_string(room) + ": roomlet " + std::to_string(i) + " is not empty"; return false; }
    }

    int32_t roomSize = 0;
    std::memcpy(&roomSize, m_bytes.data() + hdr - 4, 4);
    const long tail = static_cast<long>(roomSize) - 208 - static_cast<long>(oVtx);
    if (tail < 0 || tail > 15)
    { error = "room " + std::to_string(room) + ": something stands behind the vertex block"; return false; }

    // --- textures ---------------------------------------------------------
    //
    // like=Room<n> takes them from a room that already has faces. Which kind a
    // face is comes from its winding: cross(v1-v0, v2-v0) points AWAY from the
    // room's inside (measured on all 33 quads of Andrea3 room 36), so a floor
    // gives +y - y grows downward - a ceiling -y, and anything flat in y is a
    // wall. The commonest texinfo of each kind wins; a room built from a
    // single texture, as the neighbouring cave is, answers all three with it.
    //
    // Quads and triangles are counted SEPARATELY, six numbers instead of
    // three. A texinfo carries the shape it was cut for - the cave next door
    // puts its 24 quads on 2283 and its 42 triangles on 2284..2289 - so a
    // triangle wearing a quad's texinfo maps wrongly. Corner-exact geometry
    // does produce triangles: a split sector becomes two, and so does a wall
    // piece that runs out at one of its corners.
    int texFloor = -1, texCeil = -1, texWall = -1;      // quads
    int triFloor = -1, triCeil = -1, triWall = -1;      // triangles

    if (likeRoom >= 0)
    {
        std::vector<FaceInfo> src;
        std::string fErr;
        if (!roomFaces(likeRoom, src, fErr))
        { error = "like=Room" + std::to_string(likeRoom) + ": " + fErr; return false; }

        std::map<int, int> cntQ[3], cntT[3];
        for (const FaceInfo& f : src)
        {
            const long ax = f.v[1][0] - f.v[0][0], az = f.v[1][2] - f.v[0][2];
            const long bx = f.v[2][0] - f.v[0][0], bz = f.v[2][2] - f.v[0][2];
            const long cy = az * bx - ax * bz;          // y of the cross product
            const int kind = (cy > 0) ? 0 : (cy < 0) ? 1 : 2;
            if (f.quad) cntQ[kind][f.texInfo]++;
            else        cntT[kind][f.texInfo]++;
        }

        int* outQ[3] = { &texFloor, &texCeil, &texWall };
        int* outT[3] = { &triFloor, &triCeil, &triWall };
        for (int k = 0; k < 3; ++k)
        {
            int best = -1, bestN = 0;
            for (const auto& e : cntQ[k])
                if (e.second > bestN) { bestN = e.second; best = e.first; }
            *outQ[k] = best;

            best = -1; bestN = 0;
            for (const auto& e : cntT[k])
                if (e.second > bestN) { bestN = e.second; best = e.first; }
            *outT[k] = best;
        }

        // A room with no wall faces of its own still has to answer for walls.
        if (texWall < 0)  texWall  = (texFloor >= 0) ? texFloor : texCeil;
        if (texFloor < 0) texFloor = texWall;
        if (texCeil < 0)  texCeil  = texWall;
        if (triWall < 0)  triWall  = (triFloor >= 0) ? triFloor : triCeil;
        if (triFloor < 0) triFloor = triWall;
        if (triCeil < 0)  triCeil  = triWall;
    }

    // Later rules refine earlier ones; a rule without sx/sz/band is the
    // room-wide default. This runs per face, so every single square can carry
    // its own texture without any further machinery.
    //
    // floor=/ceiling=/wall= name a QUAD's texinfo, floortri=/ceilingtri=/
    // walltri= a TRIANGLE's - separate fields, because a texinfo carries the
    // shape it was cut for and a triangle wearing a quad's number maps
    // wrongly. A triangle takes the rule's own field first, then the triangle
    // texinfo of the like= room, and only when neither exists does it fall
    // back to the quad number - which is said out loud rather than done in
    // silence.
    int triWarned = 0;
    auto pick = [&](int sx, int sz, int band, int kind, bool tri) -> int
    {
        int ruled = -1, ruledTri = -1;
        for (const RoomTexRule& q : rules)
        {
            if (q.sx >= 0 && q.sx != sx) continue;
            if (q.sz >= 0 && q.sz != sz) continue;
            if (q.band >= 0 && q.band != band) continue;
            const int v = (kind == 0) ? q.floorTex : (kind == 1) ? q.ceilTex : q.wallTex;
            if (v >= 0) ruled = v;
            const int t = (kind == 0) ? q.floorTri : (kind == 1) ? q.ceilTri : q.wallTri;
            if (t >= 0) ruledTri = t;
        }

        const int likeQ = (kind == 0) ? texFloor : (kind == 1) ? texCeil : texWall;
        if (!tri) return (ruled >= 0) ? ruled : likeQ;

        if (ruledTri >= 0) return ruledTri;

        const int likeT = (kind == 0) ? triFloor : (kind == 1) ? triCeil : triWall;
        if (likeT >= 0) return likeT;

        const int fallback = (ruled >= 0) ? ruled : likeQ;
        if (fallback >= 0 && triWarned < 8)
        {
            ++triWarned;
            std::printf("  room %d: a triangle takes the QUAD texinfo %d (kind %d) - "
                        "the like= room has no triangle of that kind and no rule names "
                        "one (floortri=/ceilingtri=/walltri=)\n", room, fallback, kind);
        }
        return fallback;
    };

    // --- build the surface --------------------------------------------------
    //
    // The rule, measured over 13 levels against what Core actually drew
    // (95.4 % of the faces exact):
    //
    //     wall(A) = cross section(A) MINUS cross section(B)
    //     opening = cross section(A) INTERSECTED WITH cross section(B)
    //
    // where a cross section is the corner-exact floor and ceiling of a sector,
    // read out of the floordata with the engine's own formula (cornerFloor /
    // cornerCeiling). The old build read the two height BYTES instead and cut
    // its openings to the 32-byte view windows; both are wrong. A room is
    // sloped and triangulated, and the opening follows from the floordata, not
    // from a window whose only job is clipping.
    struct Vtx { float x, y, z, nx, ny, nz; uint32_t light; };
    std::vector<Vtx> vtx;
    std::vector<uint16_t> quadFace, triFace;

    // Every face gets its own four corners rather than sharing them along a
    // plane. It costs a few kilobytes and buys two things: every normal is
    // exactly its own face's normal with nothing to average, and a vertex
    // index cannot stray into another face's corner.
    //
    // 0xFF181818 is what the neighbouring room carries on 50 of its 72
    // corners, so the seam at the shared wall matches. The bytes go into the
    // file as B, G, R, A.
    const uint32_t PRELIGHT = 0xFF181818u;

    // drop < 0 makes a quad; otherwise that corner is left out and the three
    // remaining ones become a triangle. Dropping one corner of a convex quad
    // keeps the cyclic order, so the winding - and with it which side is
    // visible - survives untouched.
    //
    // Corners are doubles rather than integers: a wall piece cut where two
    // height lines swap over lands between two sector corners, and the file
    // stores floats anyway.
    auto emit = [&](const double p[4][3], float nx, float ny, float nz, int tex, int drop)
    {
        const size_t base = vtx.size();
        int n = 0;
        for (int k = 0; k < 4; ++k)
        {
            if (k == drop) continue;
            vtx.push_back({ static_cast<float>(p[k][0]), static_cast<float>(p[k][1]),
                            static_cast<float>(p[k][2]), nx, ny, nz, PRELIGHT });
            ++n;
        }

        std::vector<uint16_t>& out = (drop < 0) ? quadFace : triFace;
        for (int k = 0; k < n; ++k)
            out.push_back(static_cast<uint16_t>(base + k));
        out.push_back(static_cast<uint16_t>(tex & 0x3FFF));    // bit 15 clear: one sided
        out.push_back(0);                                      // effects, always 0 in vanilla
    };

    const int nx = r.numXSectors, nz = r.numZSectors;

    auto sector = [&](int sx, int sz) -> const uint8_t*
    { return m_bytes.data() + r.sectorsOffset + (static_cast<size_t>(sx) * nz + sz) * 8; };
    auto solid = [&](int sx, int sz) -> bool
    {
        if (sx < 0 || sz < 0 || sx >= nx || sz >= nz) return true;
        return static_cast<int8_t>(sector(sx, sz)[5]) == -127;
    };

    // The floordata DOOR (function 1) of a sector: the room it hands over to,
    // or -1. That word is what makes a square a portal square, and a portal
    // square is the OTHER room's space - it gets no floor, no ceiling and no
    // wall of ours. Measured over 13 levels: of 280560 corners Core drew, not
    // one lies outside the core box, and 1276 portal squares carry not one
    // horizontal face.
    auto doorOf = [&](int rm, int ssx, int ssz) -> int
    {
        const int idx = sectorIndex(rm, ssx, ssz);
        if (idx <= 0) return -1;
        std::vector<Tr5FloorEntry> chain;
        if (!readChain(idx, chain)) return -1;
        for (const Tr5FloorEntry& e : chain)
            if (e.function == FN_DOOR && e.words.size() >= 2)
                return static_cast<int>(e.words[1]);
        return -1;
    };

    // The cross section of ONE sector at a corner named in WORLD x/z. Asked
    // for our own sector and for the neighbour's, and the neighbour may live
    // in another room with another origin - hence world coordinates rather
    // than sector-local ones. false means that sector has no cross section
    // there (wall, unreadable chain, corner not on it).
    auto crossAt = [&](int rm, int ssx, int ssz, long wx, long wz,
                       long& fy, long& cy) -> bool
    {
        if (rm < 0 || rm >= static_cast<int>(m_rooms.size())) return false;
        const Tr5Room& t = m_rooms[static_cast<size_t>(rm)];
        const long lx = wx - (static_cast<long>(t.originX) + static_cast<long>(ssx) * 1024);
        const long lz = wz - (static_cast<long>(t.originZ) + static_cast<long>(ssz) * 1024);
        if ((lx != 0 && lx != 1024) || (lz != 0 && lz != 1024)) return false;
        return cornerFloor(rm, ssx, ssz, static_cast<int>(lx), static_cast<int>(lz), fy) &&
               cornerCeiling(rm, ssx, ssz, static_cast<int>(lx), static_cast<int>(lz), cy);
    };

    // Which diagonal the engine cuts a split sector along. 1 = SPLIT1/3 and
    // their NOCOL twins, the diagonal dx + dz == 1024; 2 = SPLIT2/4, the
    // diagonal dx == dz; 0 = no split at all. Only ever asked where the four
    // corners disagree, and a sector without a split cannot disagree: a TILT
    // is one plane, and so is a sum of tilts.
    auto floorFamily = [&](int rm, int ssx, int ssz) -> int
    {
        const int idx = sectorIndex(rm, ssx, ssz);
        if (idx <= 0) return 0;
        std::vector<Tr5FloorEntry> chain;
        if (!readChain(idx, chain)) return 0;
        for (const Tr5FloorEntry& e : chain)
        {
            if (e.function == 7 || e.function == 11 || e.function == 12) return 1;
            if (e.function == 8 || e.function == 13 || e.function == 14) return 2;
        }
        return 0;
    };

    // The ceiling reads at most ONE entry - the first, or the second when the
    // first is a floor slope (GetCeiling, control.cpp). cornerCeiling
    // reproduces exactly that, so the diagonal has to be looked up the same
    // way, or the two triangles would not sit on the heights they were cut for.
    auto ceilFamily = [&](int rm, int ssx, int ssz) -> int
    {
        const int idx = sectorIndex(rm, ssx, ssz);
        if (idx <= 0) return 0;
        std::vector<Tr5FloorEntry> chain;
        if (!readChain(idx, chain) || chain.empty()) return 0;
        const Tr5FloorEntry* e = &chain[0];
        if (e->words.empty()) return 0;
        if (isFloorSlope(e->function))
        {
            const bool last = (e->words[0] & 0x8000) != 0;
            if (last || chain.size() < 2) return 0;
            e = &chain[1];
        }
        if (e->function == 9  || e->function == 15 || e->function == 16) return 1;
        if (e->function == 10 || e->function == 17 || e->function == 18) return 2;
        return 0;
    };

    int anomalies = 0;              // corners disagree, yet no split in the chain
    int kinks = 0;                  // sides cut where two height lines swap over

    for (int sx = 0; sx < nx; ++sx)
        for (int sz = 0; sz < nz; ++sz)
        {
            if (solid(sx, sz)) continue;
            if (doorOf(room, sx, sz) >= 0) continue;    // portal square: not ours

            const long X0 = static_cast<long>(sx) * 1024, X1 = X0 + 1024;
            const long Z0 = static_cast<long>(sz) * 1024, Z1 = Z0 + 1024;

            // Our own cross section: floor and ceiling at all four corners.
            long fC[2][2], cC[2][2];                    // [x corner][z corner]
            bool have = true;
            for (int ix = 0; ix < 2 && have; ++ix)
                for (int iz = 0; iz < 2 && have; ++iz)
                    have = cornerFloor(room, sx, sz, ix * 1024, iz * 1024, fC[ix][iz]) &&
                           cornerCeiling(room, sx, sz, ix * 1024, iz * 1024, cC[ix][iz]);
            if (!have) continue;                        // unreadable chain: hands off

            // No head room at ANY corner closes the sector - not the
            // byte-wise test "floor byte <= ceiling byte", which throws
            // away 7 core sectors in 3 rooms (rich3 214, Andrea2 102, rich3
            // 206) that are shut by the bytes and half open at the corners.
            bool anyOpen = false;
            for (int ix = 0; ix < 2; ++ix)
                for (int iz = 0; iz < 2; ++iz)
                    if (fC[ix][iz] > cC[ix][iz]) anyOpen = true;
            if (!anyOpen) continue;

            // The grid the wall texture repeats on. The sector's floor BYTE,
            // as before - a per-sector constant, so the bands of the four
            // sides line up with each other, and identical to the old
            // behaviour wherever the sector is flat.
            const long FBAND = static_cast<long>(static_cast<int8_t>(sector(sx, sz)[5])) * 256;

            // --- floor and ceiling ---------------------------------------
            //
            // Four corner heights, the winding unchanged. Four corners of a
            // split sector do not lie in one plane; then it becomes two
            // triangles along the engine's own diagonal, which is how Core
            // drew 1759 of 1768 vanilla split floors.
            {
                const double pf[4][3] = { { double(X1), double(fC[1][1]), double(Z1) },
                                          { double(X1), double(fC[1][0]), double(Z0) },
                                          { double(X0), double(fC[0][0]), double(Z0) },
                                          { double(X0), double(fC[0][1]), double(Z1) } };
                if (fC[0][0] + fC[1][1] == fC[0][1] + fC[1][0])
                    emit(pf, 0.0F, -1.0F, 0.0F, pick(sx, sz, -1, 0, false), -1);
                else
                {
                    int fam = floorFamily(room, sx, sz);
                    if (fam == 0) { ++anomalies; fam = 1; }
                    const int t = pick(sx, sz, -1, 0, true);
                    // Corner order here is (1,1) (1,0) (0,0) (0,1), so the
                    // family 1 diagonal (0,1024)-(1024,0) joins corners 3 and
                    // 1, the family 2 diagonal (0,0)-(1024,1024) corners 2
                    // and 0. Dropping the other two in turn gives the pair.
                    if (fam == 1) { emit(pf, 0.0F, -1.0F, 0.0F, t, 2); emit(pf, 0.0F, -1.0F, 0.0F, t, 0); }
                    else          { emit(pf, 0.0F, -1.0F, 0.0F, t, 1); emit(pf, 0.0F, -1.0F, 0.0F, t, 3); }
                }

                const double pc[4][3] = { { double(X0), double(cC[0][1]), double(Z1) },
                                          { double(X0), double(cC[0][0]), double(Z0) },
                                          { double(X1), double(cC[1][0]), double(Z0) },
                                          { double(X1), double(cC[1][1]), double(Z1) } };
                if (cC[0][0] + cC[1][1] == cC[0][1] + cC[1][0])
                    emit(pc, 0.0F, 1.0F, 0.0F, pick(sx, sz, -1, 1, false), -1);
                else
                {
                    int fam = ceilFamily(room, sx, sz);
                    if (fam == 0) { ++anomalies; fam = 1; }
                    const int t = pick(sx, sz, -1, 1, true);
                    // Corner order (0,1) (0,0) (1,0) (1,1): family 1 joins 0
                    // and 2, family 2 joins 1 and 3.
                    if (fam == 1) { emit(pc, 0.0F, 1.0F, 0.0F, t, 1); emit(pc, 0.0F, 1.0F, 0.0F, t, 3); }
                    else          { emit(pc, 0.0F, 1.0F, 0.0F, t, 0); emit(pc, 0.0F, 1.0F, 0.0F, t, 2); }
                }
            }

            // One vertical strip of one side, between the height line yt and
            // the height line yb, both given at the two ends of the strip.
            auto strip = [&](const double px[2], const double pz[2],
                             const double yt[2], const double yb[2],
                             float fnx, float fnz)
            {
                const double h0 = yb[0] - yt[0], h1 = yb[1] - yt[1];
                if (h0 <= 0.0 && h1 <= 0.0) return;

                const double p[4][3] = { { px[0], yt[0], pz[0] }, { px[1], yt[1], pz[1] },
                                         { px[1], yb[1], pz[1] }, { px[0], yb[0], pz[0] } };
                const double low = (yb[0] > yb[1]) ? yb[0] : yb[1];
                const int band = static_cast<int>((static_cast<double>(FBAND) - low) / 1024.0);

                // A strip that runs out at one of its two ends is a triangle -
                // drop the corner that has collapsed onto its neighbour.
                if (h0 <= 0.0 || h1 <= 0.0)
                {
                    emit(p, fnx, 0.0F, fnz, pick(sx, sz, band, 2, true), (h0 <= 0.0) ? 3 : 1);
                    return;
                }

                // Rectangular strips keep the 1024 band split, so a tall wall
                // repeats its texture instead of stretching it over the whole
                // height. A sloped strip stays whole: a band cut across a
                // slope would need its own corner heights and buys nothing.
                if (yt[0] == yt[1] && yb[0] == yb[1])
                {
                    std::vector<double> cuts;
                    cuts.push_back(yt[0]);
                    cuts.push_back(yb[0]);
                    for (double y = static_cast<double>(FBAND); y > yt[0]; y -= 1024.0)
                        if (y < yb[0]) cuts.push_back(y);
                    std::sort(cuts.begin(), cuts.end());
                    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

                    for (size_t k = 0; k + 1 < cuts.size(); ++k)
                    {
                        const double a = cuts[k], b = cuts[k + 1];
                        if (b - a < 2.0) continue;
                        const double q[4][3] = { { px[0], a, pz[0] }, { px[1], a, pz[1] },
                                                 { px[1], b, pz[1] }, { px[0], b, pz[0] } };
                        const int bb = static_cast<int>((static_cast<double>(FBAND) - b) / 1024.0);
                        emit(q, fnx, 0.0F, fnz, pick(sx, sz, bb, 2, false), -1);
                    }
                    return;
                }

                emit(p, fnx, 0.0F, fnz, pick(sx, sz, band, 2, false), -1);
            };

            // --- the four sides ------------------------------------------
            for (int d = 0; d < 4; ++d)
            {
                const int ox = (d == 0) ? -1 : (d == 1) ? 1 : 0;
                const int oz = (d == 2) ? -1 : (d == 3) ? 1 : 0;

                // The two corners of this edge, as [x corner][z corner]
                // indices, in the winding this side has always used - west and
                // east run the opposite way round, and a hard-coded corner
                // index would be right for one of them only.
                int ex[2], ez[2];
                if (d == 0)      { ex[0] = 0; ez[0] = 0; ex[1] = 0; ez[1] = 1; }   // west
                else if (d == 1) { ex[0] = 1; ez[0] = 1; ex[1] = 1; ez[1] = 0; }   // east
                else if (d == 2) { ex[0] = 1; ez[0] = 0; ex[1] = 0; ez[1] = 0; }   // north
                else             { ex[0] = 0; ez[0] = 1; ex[1] = 1; ez[1] = 1; }   // south

                const float fnx = (d == 0) ? 1.0F : (d == 1) ? -1.0F : 0.0F;
                const float fnz = (d == 2) ? 1.0F : (d == 3) ? -1.0F : 0.0F;

                double eX[2], eZ[2], cA[2], fA[2];
                long wCx[2], wCz[2];
                for (int i = 0; i < 2; ++i)
                {
                    eX[i] = static_cast<double>(ex[i] ? X1 : X0);
                    eZ[i] = static_cast<double>(ez[i] ? Z1 : Z0);
                    wCx[i] = r.originX + (ex[i] ? X1 : X0);
                    wCz[i] = r.originZ + (ez[i] ? Z1 : Z0);
                    cA[i] = static_cast<double>(cC[ex[i]][ez[i]]);
                    fA[i] = static_cast<double>(fC[ex[i]][ez[i]]);
                }

                // Whose cross section stands on the other side of that edge.
                // Three cases, in the order they occur: nothing at all (wall
                // or outside the room), an open sector of our own room, or a
                // portal square - and a portal square has no cross section of
                // its own, it is the window into room T, so T's sector at the
                // SAME place in the world answers instead.
                int nbRoom = -1, nbSx = 0, nbSz = 0;
                if (!solid(sx + ox, sz + oz))
                {
                    const int T = doorOf(room, sx + ox, sz + oz);
                    if (T < 0) { nbRoom = room; nbSx = sx + ox; nbSz = sz + oz; }
                    else if (T < static_cast<int>(m_rooms.size()))
                    {
                        const Tr5Room& t = m_rooms[static_cast<size_t>(T)];
                        const long dxw = r.originX + static_cast<long>(sx + ox) * 1024 - t.originX;
                        const long dzw = r.originZ + static_cast<long>(sz + oz) * 1024 - t.originZ;
                        if (dxw >= 0 && dzw >= 0 && !(dxw % 1024) && !(dzw % 1024) && t.sectorsOffset)
                        {
                            const int tsx = static_cast<int>(dxw / 1024);
                            const int tsz = static_cast<int>(dzw / 1024);
                            if (tsx < t.numXSectors && tsz < t.numZSectors &&
                                static_cast<int8_t>(m_bytes[t.sectorsOffset +
                                    (static_cast<size_t>(tsx) * t.numZSectors + tsz) * 8 + 5]) != -127 &&
                                doorOf(T, tsx, tsz) < 0)
                            { nbRoom = T; nbSx = tsx; nbSz = tsz; }
                        }
                    }
                    // Everything else - the target sector lies outside T, is a
                    // wall there, or is itself a portal - counts as no
                    // neighbour, which makes the side a full wall. Over 13
                    // levels that happened twice in all.
                }

                double cB[2], fB[2];
                bool haveNb = (nbRoom >= 0);
                for (int i = 0; i < 2 && haveNb; ++i)
                {
                    long fy = 0, cy = 0;
                    if (crossAt(nbRoom, nbSx, nbSz, wCx[i], wCz[i], fy, cy))
                    { fB[i] = static_cast<double>(fy); cB[i] = static_cast<double>(cy); }
                    else haveNb = false;
                }

                // Along the edge every height runs linearly from one corner to
                // the other, but max(cA,cB) and min(fA,fB) have a KINK wherever
                // two of the four lines swap over. A single quad would cut
                // straight across that kink, so the edge is split there first.
                std::vector<double> ts;
                ts.push_back(0.0);
                ts.push_back(1.0);
                if (haveNb)
                {
                    const double a[4] = { cA[0], fA[0], cB[0], fB[0] };
                    const double b[4] = { cA[1], fA[1], cB[1], fB[1] };
                    for (int i = 0; i < 4; ++i)
                        for (int j = i + 1; j < 4; ++j)
                        {
                            const double d0 = a[i] - a[j], d1 = b[i] - b[j];
                            if ((d0 > 0.0) == (d1 > 0.0)) continue;     // no swap
                            if (d0 == d1) continue;
                            const double t = d0 / (d0 - d1);
                            if (t > 1e-6 && t < 1.0 - 1e-6) { ts.push_back(t); ++kinks; }
                        }
                    std::sort(ts.begin(), ts.end());
                    ts.erase(std::unique(ts.begin(), ts.end()), ts.end());
                }

                for (size_t si = 0; si + 1 < ts.size(); ++si)
                {
                    double px[2], pz[2], cAv[2], fAv[2], top[2], bot[2];
                    for (int i = 0; i < 2; ++i)
                    {
                        const double t = ts[si + static_cast<size_t>(i)];
                        px[i] = eX[0] + (eX[1] - eX[0]) * t;
                        pz[i] = eZ[0] + (eZ[1] - eZ[0]) * t;
                        cAv[i] = cA[0] + (cA[1] - cA[0]) * t;
                        fAv[i] = fA[0] + (fA[1] - fA[0]) * t;

                        if (haveNb)
                        {
                            const double cBv = cB[0] + (cB[1] - cB[0]) * t;
                            const double fBv = fB[0] + (fB[1] - fB[0]) * t;
                            // open = [max(cA,cB) .. min(fA,fB)], clamped into
                            // our own cross section so no piece leaves it.
                            top[i] = std::min(std::max(cAv[i], cBv), fAv[i]);
                            bot[i] = std::max(std::min(fAv[i], fBv), cAv[i]);
                        }
                        else { top[i] = fAv[i]; bot[i] = fAv[i]; }
                    }

                    // An opening that is empty at BOTH ends of the strip is no
                    // opening: the wall runs through from ceiling to floor.
                    if (bot[0] <= top[0] && bot[1] <= top[1])
                    { top[0] = fAv[0]; top[1] = fAv[1]; bot[0] = fAv[0]; bot[1] = fAv[1]; }

                    strip(px, pz, cAv, top, fnx, fnz);      // above the opening
                    strip(px, pz, bot, fAv, fnx, fnz);      // below it
                }
            }
        }

    if (anomalies)
        std::printf("  room %d: %d sector(s) whose corners disagree without a split "
                    "entry in the chain - cut along the family 1 diagonal\n", room, anomalies);
    if (kinks)
        std::printf("  room %d: %d side(s) cut where two height lines swap over\n", room, kinks);

    if (vtx.empty())
    { error = "room " + std::to_string(room) + ": nothing to build, every sector is wall or portal"; return false; }
    if (vtx.size() > 32767)
    { error = "room " + std::to_string(room) + ": too many vertices for one roomlet"; return false; }

    // --- assemble and append ------------------------------------------------
    //
    // Order inside the payload is fixed here, so nothing has to work out
    // afterwards which of the three equal offsets meant what.
    std::vector<uint8_t> payload;
    payload.reserve((quadFace.size() + triFace.size()) * 2 + vtx.size() * 28 + 16);

    // Quads come first in a roomlet's stream, then triangles - measured on
    // every vanilla room, and the engine's own hand-out relies on it
    // (pFac += 5*nTri + 6*nQuad).
    for (uint16_t s : quadFace)
    {
        payload.push_back(static_cast<uint8_t>(s & 0xFF));
        payload.push_back(static_cast<uint8_t>(s >> 8));
    }
    for (uint16_t s : triFace)
    {
        payload.push_back(static_cast<uint8_t>(s & 0xFF));
        payload.push_back(static_cast<uint8_t>(s >> 8));
    }

    // Two bytes of padding when the room's total triangle count is odd - the
    // face stream is then 2 mod 4 long and pRmVtx has to land on a multiple of
    // four. True in 319 of the vanilla rooms, and in all of them the count is
    // odd, so this is the rule and not a coincidence.
    if ((triFace.size() / 5) & 1)
    {
        payload.push_back(0);
        payload.push_back(0);
    }

    const size_t faceBytes = payload.size();

    for (const Vtx& v : vtx)
    {
        const float f[6] = { v.x, v.y, v.z, v.nx, v.ny, v.nz };
        const uint8_t* p = reinterpret_cast<const uint8_t*>(f);
        payload.insert(payload.end(), p, p + 24);
        const uint8_t* l = reinterpret_cast<const uint8_t*>(&v.light);
        payload.insert(payload.end(), l, l + 4);
    }

    while ((static_cast<long>(payload.size()) + tail) % 16) payload.push_back(0);
    const size_t N = payload.size();

    m_bytes.insert(m_bytes.begin() + static_cast<long>(body + oVtx),
                   payload.begin(), payload.end());

    // Exactly one header offset moves: the vertices now start behind the face
    // stream. pRmFace and pRmPrelight keep pointing where they always did.
    const uint32_t newVtx = oVtx + static_cast<uint32_t>(faceBytes);
    std::memcpy(m_bytes.data() + hdr + 176, &newVtx, 4);

    // Roomlet 0 takes everything. The engine puts no condition on cell width -
    // InsertRoom (drawroom.cpp) checks nVerts, then each roomlet's bounding
    // box, then its nVtx, and nothing else - and with a single filled roomlet
    // the roomlet-relative indexing cannot go wrong at all.
    {
        const size_t at = body + oRoomlets;
        const int16_t nV = static_cast<int16_t>(vtx.size());
        const int16_t nQ = static_cast<int16_t>(quadFace.size() / 6);
        const int16_t nT = static_cast<int16_t>(triFace.size() / 5);
        const int16_t z16 = 0;
        std::memcpy(m_bytes.data() + at + 0, &nV, 2);
        std::memcpy(m_bytes.data() + at + 2, &z16, 2);
        std::memcpy(m_bytes.data() + at + 4, &z16, 2);
        std::memcpy(m_bytes.data() + at + 6, &nQ, 2);
        std::memcpy(m_bytes.data() + at + 8, &nT, 2);
        std::memcpy(m_bytes.data() + at + 10, &z16, 2);
        std::memcpy(m_bytes.data() + at + 12, &z16, 2);
        std::memcpy(m_bytes.data() + at + 14, &z16, 2);

        // The bounding box, from the corners we just wrote. Too large only
        // costs a frustum test; too small makes the roomlet vanish at the edge
        // of the screen and takes its muzzle flashes with it. moveRoom shifts
        // vertex y but leaves the box alone, which is harmless here because
        // this step runs after the move and measures the finished corners.
        float bb[6] = { vtx[0].x, vtx[0].y, vtx[0].z, vtx[0].x, vtx[0].y, vtx[0].z };
        for (const Vtx& v : vtx)
        {
            bb[0] = std::min(bb[0], v.x); bb[3] = std::max(bb[3], v.x);
            bb[1] = std::min(bb[1], v.y); bb[4] = std::max(bb[4], v.y);
            bb[2] = std::min(bb[2], v.z); bb[5] = std::max(bb[5], v.z);
        }
        std::memcpy(m_bytes.data() + at + 16, bb, 24);

        const uint32_t zero32 = 0;                      // the four runtime pointers
        for (int k = 0; k < 4; ++k)
            std::memcpy(m_bytes.data() + at + 40 + static_cast<size_t>(k) * 4, &zero32, 4);
    }

    // The engine never reads these three, but they agree with the sums in all
    // 2958 vanilla rooms. Leaving them behind would produce a room that every
    // later diagnostic reports as broken.
    {
        const int32_t gt3 = static_cast<int32_t>(triFace.size() / 5);
        const int32_t gt4 = static_cast<int32_t>(quadFace.size() / 6);
        const int32_t vdump = static_cast<int32_t>(vtx.size() * 28);
        std::memcpy(m_bytes.data() + hdr + 136, &gt3, 4);
        std::memcpy(m_bytes.data() + hdr + 140, &gt4, 4);
        std::memcpy(m_bytes.data() + hdr + 188, &vdump, 4);
    }

    // nVerts (header +80) is deliberately NOT touched. `if (r->nVerts)` in
    // InsertRoom (drawroom.cpp) is its only reader in the whole engine, and it is not
    // the sum of the roomlet counts anyway - only 1046 of 2958 rooms agree.

    roomSize += static_cast<int32_t>(N);
    std::memcpy(m_bytes.data() + hdr - 4, &roomSize, 4);

    // The main block carries its length twice, and readHeader throws the file
    // out when the two disagree.
    m_mainSize += N;
    {
        const int32_t s = static_cast<int32_t>(m_mainSize);
        std::memcpy(m_bytes.data() + m_mainOffset - 8, &s, 4);
        std::memcpy(m_bytes.data() + m_mainOffset - 4, &s, 4);
    }

    return reparse(error);
}

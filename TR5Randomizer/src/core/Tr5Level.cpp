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

    STEP("cameras", i32(n));      STEP("cameras", skipN(n, SZ_OBJVEC));
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

    STEP("animated textures", i32(n)); STEP("animated textures", skipN(n, 2));
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

    return setItem(m_itemCount - 1, in);
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
    // entries of 24 bytes each (LoadAIInfo, specific/file.cpp:1209).
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
// The format is fully derived in "Tomb5 Rando vorbereitung\Trigger\Floordata
// und Trigger.md", with line references into the engine. Here only what is
// needed to read the code along.
//----------------------------------------------------------------------------
namespace
{
    // global/types.h, enum floor_types
    enum { FN_DOOR = 1, FN_TILT = 2, FN_ROOF = 3, FN_TRIGGER = 4,
           FN_LAVA = 5, FN_CLIMB = 6, FN_MONKEY = 19, FN_TRIGTRIGGER = 20 };

    // SWITCH (2), KEY (3) and PICKUP (4) push a reference word in front of
    // the command list: the item that must be operated resp. picked up. The
    // engine reads all three the same way - game/control.cpp:2251, :2266,
    // :2274, each time "value = *(data++) & 0x3FF". The word is NOT a
    // command.
    bool hasRefWord(int kind) { return kind == 2 || kind == 3 || kind == 4; }

    // Extra words of an entry that is not a trigger: exactly the cases
    // where GetHeight (control.cpp:977ff) does a "data++".
    int extraWords(int fn)
    {
        return (fn == FN_DOOR || fn == FN_TILT || fn == FN_ROOF || (fn >= 7 && fn <= 18)) ? 1 : 0;
    }

    // Command kind: 4 bits, not 5. Assigned twice independently - "switch
    // ((trigger & 0x3FFF) >> 10)" in _TestTriggers (control.cpp:2350) and
    // "(trigger & 0x3C00) != (TO_OBJECT << 10)" in GetHeight (:1025). Bit
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

const char* Tr5Level::canAppendTrigger(const std::vector<Tr5FloorEntry>& chain)
{
    // _TestTriggers receives a pointer from GetHeight and from there skips
    // EXACTLY four entry kinds - lava, climb, monkey, TrigTrigger
    // (control.cpp:2151-2195) - and then unconditionally reads a trigger
    // head (:2197). GetHeight sets the pointer onto the FIRST entry from
    // {trigger, climb, monkey, TrigTrigger} (1014-1053), lava even
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
    // correction they point off after the first appended trigger. It went
    // unnoticed for a long time, because nobody read these blocks after a
    // floordata append - only the anim check did, and promptly reported
    // that Lara's animations were "missing" (#53).
    auto adjustBlock = [&](Block& b) {
        if (b.countAt >= at) b.countAt += raw.size();
        if (b.dataAt  >= at) b.dataAt  += raw.size();
    };
    adjustBlock(m_meshData);  adjustBlock(m_meshPtrs);
    adjustBlock(m_anims);     adjustBlock(m_changes);
    adjustBlock(m_ranges);    adjustBlock(m_commands);
    adjustBlock(m_bones);     adjustBlock(m_frames);
    adjustBlock(m_objects);   adjustBlock(m_sprites);
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
    // reads it three times the same way (control.cpp:2251/2266/2274,
    // "value = *(data++) & 0x3FF") and does NOT treat it as a command.
    //
    // A refusal used to stand here - such a trigger is not one you place
    // freely but one bound to an existing object. With the secret lists
    // (#53) that case now exists: the user places a keyhole plus the key
    // trigger that connects it to a door. Whoever passes refItem thereby
    // says explicitly which object the trigger is bound to.
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
// Turn a wall sector into a portal (#53, the andy3 double door).
//
// What a portal sector is was measured with SektorPruef on the working
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
// The VIEW WINDOW (the room list's 32-byte portal entry) is NOT created
// here - it must already exist. In andy3, Core built both windows in
// completely and only sealed the sectors; exactly that case is checked, and
// its absence is a hard error instead of a black hole in the picture.
//----------------------------------------------------------------------------
bool Tr5Level::makePortal(int room, int sx, int sz, int targetRoom, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "room " + std::to_string(room) + " does not exist"; return false; }
    if (targetRoom < 0 || targetRoom >= static_cast<int>(m_rooms.size()))
    { error = "target room " + std::to_string(targetRoom) + " does not exist"; return false; }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    const Tr5Room& z = m_rooms[static_cast<size_t>(targetRoom)];
    if (!r.sectorsOffset || !z.sectorsOffset)
    { error = "sector field did not parse"; return false; }
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "sector outside the room"; return false; }

    // View window precondition: the target room must be in the portal list.
    bool window = false;
    for (int p : r.portalRooms) if (p == targetRoom) { window = true; break; }
    if (!window)
    {
        error = "room " + std::to_string(room) + " has no view window to " +
                std::to_string(targetRoom) + " - the 32-byte portal is missing, without it "
                "the opening would be a black hole";
        return false;
    }

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

    // pit/floor/sky/ceiling from the overlapped sector.
    std::memcpy(m_bytes.data() + ownAt + 4, m_bytes.data() + acrossAt + 4, 4);
    return true;
}

//----------------------------------------------------------------------------
// Make a sector climbable (#53, Make_Climb).
//
// A CLIMB entry is ONE word: function 6 in bits 0-4, the climbable wall
// sides in bits 8-11. The engine checks in _TestTriggers
// (control.cpp:2189): quad = (yRot + 0x2000) / 0x4000, bit = 1 << (quad+8)
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
// Set or clear a room's water bit (#53, Make_Room_Water / Make_Room_Nowater).
//
// ROOM_UNDERWATER is bit 0 of the room flags (global/types.h:281), in the
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
// "interpolation" field, ANIM_STRUCT in global/types.h:645). The engine
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
// Layout, read off TOMB5's LoadSamples (specific/file.cpp:1232):
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
// it with ReadLong() from the buffered main block (:1252) and right after
// once more via fread() straight from the file (:1261), whereby the second
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
    // (file.cpp:1282).
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
    // (game/sound.cpp:280). So it is ONE info with N samples, NOT N infos:
    // the old reading here copied the NEIGHBOR infos' samples (= foreign
    // sounds) along as "variants" - in the target the engine then rolled
    // between the right sound and foreign ones, with the right one's flags
    // (heard in game, #76: TwoGun randomly plays his shot instead of the
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

    // Texture workshop (#104): as long as it is dirty and open, the texture
    // blocks in m_bytes carry the OLD state. Rather fail loudly than
    // silently write a level with stale pages (the TR3Setz lesson: nobody
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

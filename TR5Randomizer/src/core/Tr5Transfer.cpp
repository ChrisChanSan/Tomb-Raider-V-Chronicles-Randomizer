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

//----------------------------------------------------------------------------
// Model transfer between TR5 levels: Tr5Level::copyMoveableFrom.
//
// This is the key piece for three things at once - weapons in all levels,
// random outfits and the cross enemy mix. It used to live only in the
// workshop tool TombItemEdit2026 (vImportFromMVB / -copymvb), and that is
// never shipped: game data must not leave the house, so the transfer has to
// happen at runtime on the user's files.
//
// Every format statement here was read off the engine, not off documentation:
//
//   header            specific/file.cpp:1339 - version u32, RT/OT/BT u16 each
//   texture blocks    LoadTextures (:416) - 32-bit and 16-bit block, each
//                     u32 uncompressed + u32 compressed + zlib stream; the
//                     engine reads ONLY ONE of them depending on the screen
//                     format, so both must grow in sync
//   page order        room (RT), then object (OT), then bump (BT); index 0
//                     is reserved, usable 1..RT+OT+BT, limit 256 (:438)
//   bump              Textures[n].bumptpage = n + BT/2 (:593) - pure runtime
//                     arithmetic. Inserting new pages at the OT end is safe
//                     as long as all STORED tile numbers >= RT+OT rise by
//                     the insertion count
//   texinfo           40 bytes, tile as u16 at offset 2, 0-based in bits
//                     0-14, bit 15 is a flag (LoadTextureInfos :1093)
//   sprites           16 bytes, tile as u16 at offset 0, 0-based, the
//                     loader adds +1 (:903)
//   mesh              drawroom.cpp ProcessMeshData (:740): x,y,z,r,flags,
//                     nVerts word; vertices 3 shorts; nNorms short, then
//                     normals (3 per vertex) or prelight (1 per vertex);
//                     ngt4 + quads of 6 shorts {v0..v3, texinfo, attr};
//                     ngt3 + tris of 5 shorts. The engine masks the face
//                     texinfo with & 0x7FFF (output.cpp:1575)
//   mesh pointers     byte offsets into the mesh data block (file.cpp:775)
//   anims             40 bytes; frame_ptr is a BYTE OFFSET into the frame
//                     block (fixup :810); jump_anim, change_index and
//                     command_index are level-wide absolute; frame_base and
//                     frame_end are logical numbers and stay untouched
//   object entry      slot u32, nmeshes s16, mesh_index s16 (pointer index,
//                     the doubling only happens at load :835),
//                     bone_index s32, frame_offset u32, anim_index u16
//                     (0xFFFF = none), 2 bytes padding (:814)
//
// Three rules from the weapon work (#58), learned dearly there:
//   1. Never copy slot 0 (LARA) - it carries the level's animations.
//   2. Mesh offset 0 is NOT a null pointer; Lara's right hand lives there.
//      Harmless here, because offsets are never treated as truth values -
//      the tool's mistake is simply not rebuilt.
//   3. The tile numbers must be adjusted completely, or something points at
//      the wrong page (the pixelated gunsmoke).
//----------------------------------------------------------------------------
#include "Tr5Level.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include <zlib.h>

namespace
{
    constexpr size_t PAGE32 = 256 * 256 * 4;
    constexpr size_t PAGE16 = 256 * 256 * 2;
    constexpr int    MAX_PAGES = 256;        // Textures[], file.cpp:438
    constexpr int    SZ_OBJ = 20;
    constexpr int    SZ_ANIM = 40;
    constexpr int    SZ_CHANGE = 6;
    constexpr int    SZ_RANGE = 8;
    constexpr int    SZ_TEXINFO = 40;
    constexpr int    SZ_SPRITE = 16;

    uint16_t rd16(const std::vector<uint8_t>& b, size_t o) { uint16_t v; std::memcpy(&v, b.data() + o, 2); return v; }
    uint32_t rd32(const std::vector<uint8_t>& b, size_t o) { uint32_t v; std::memcpy(&v, b.data() + o, 4); return v; }
    int16_t  rs16(const std::vector<uint8_t>& b, size_t o) { int16_t v; std::memcpy(&v, b.data() + o, 2); return v; }
    void wr16(std::vector<uint8_t>& b, size_t o, uint16_t v) { std::memcpy(b.data() + o, &v, 2); }
    void wr32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }

    void push16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
    void push32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }

    //------------------------------------------------------------------------
    // An insertion plan: collect all insertions first, then apply them in ONE
    // pass. The reason is at the block map in Tr5Level.h: every single
    // insertion would invalidate all remembered offsets, and nine insertions
    // with a manual fixup of all offsets each are nine chances to forget one.
    //------------------------------------------------------------------------
    struct Splice { size_t at; std::vector<uint8_t> bytes; };

    void applySplices(std::vector<uint8_t>& file, std::vector<Splice>& splices)
    {
        std::sort(splices.begin(), splices.end(),
                  [](const Splice& a, const Splice& b) { return a.at < b.at; });
        std::vector<uint8_t> out;
        size_t extra = 0;
        for (const Splice& s : splices) extra += s.bytes.size();
        out.reserve(file.size() + extra);

        size_t p = 0;
        for (const Splice& s : splices)
        {
            out.insert(out.end(), file.begin() + p, file.begin() + s.at);
            out.insert(out.end(), s.bytes.begin(), s.bytes.end());
            p = s.at;
        }
        out.insert(out.end(), file.begin() + p, file.end());
        file.swap(out);
    }

    //------------------------------------------------------------------------
    // The three texture blocks at the start of the file. Each: u32
    // uncompressed, u32 compressed, then the zlib stream.
    //------------------------------------------------------------------------
    struct TexHeader
    {
        int rt = 0, ot = 0, bt = 0;
        size_t block32At = 0;                // position of the first u32
        size_t block16At = 0;
        size_t blockMiscAt = 0;
        size_t afterBlocksAt = 0;            // first position behind block 3
    };

    bool parseTexHeader(const std::vector<uint8_t>& b, TexHeader& h, std::string& err)
    {
        if (b.size() < 10 + 8) { err = "file too short for the texture header"; return false; }
        h.rt = rd16(b, 4); h.ot = rd16(b, 6); h.bt = rd16(b, 8);

        size_t p = 10;
        h.block32At = p; p += 8 + rd32(b, p + 4);
        if (p + 8 > b.size()) { err = "32-bit block does not match the file"; return false; }
        h.block16At = p; p += 8 + rd32(b, p + 4);
        if (p + 8 > b.size()) { err = "16-bit block does not match the file"; return false; }
        h.blockMiscAt = p; p += 8 + rd32(b, p + 4);
        if (p > b.size()) { err = "third texture block does not match the file"; return false; }
        h.afterBlocksAt = p;
        return true;
    }

    bool inflateBlock(const std::vector<uint8_t>& b, size_t at, std::vector<uint8_t>& out, std::string& err)
    {
        const uint32_t un = rd32(b, at), comp = rd32(b, at + 4);
        out.resize(un);
        uLongf dstLen = un;
        const int rc = uncompress(out.data(), &dstLen, b.data() + at + 8, comp);
        if (rc != Z_OK || dstLen != un) { err = "texture block cannot be unpacked"; return false; }
        return true;
    }

    void deflateBlock(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out)
    {
        uLongf bound = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> buffer(bound);
        // Z_BEST_SPEED instead of Z_BEST_COMPRESSION (#104): this call used
        // to run ONCE per imported type over the target's complete texture
        // block - under allsanity ten times per level on 30+ MB, and this is
        // exactly where the measured 5-10 seconds of the enemy mix lived
        // (loading a level costs only 15 ms by comparison). The engine does
        // not care about the level, the file gets a bit bigger, the time
        // drops many-fold.
        compress2(buffer.data(), &bound, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED);
        out.clear();
        push32(out, static_cast<uint32_t>(raw.size()));
        push32(out, static_cast<uint32_t>(bound));
        out.insert(out.end(), buffer.begin(), buffer.begin() + bound);
    }

    //------------------------------------------------------------------------
    // Length of a mesh in bytes, exactly the walker of ProcessMeshData.
    //------------------------------------------------------------------------
    bool meshSize(const std::vector<uint8_t>& b, size_t at, size_t limit, size_t& bytes, std::string& err)
    {
        size_t p = at;
        auto s16 = [&](int16_t& v) -> bool {
            if (p + 2 > limit) return false;
            std::memcpy(&v, b.data() + p, 2); p += 2; return true;
        };
        int16_t w;
        for (int i = 0; i < 5; ++i) if (!s16(w)) { err = "mesh header truncated"; return false; }
        int16_t vertsWord; if (!s16(vertsWord)) { err = "mesh header truncated"; return false; }
        const int nVerts = vertsWord & 0xFF;

        if (nVerts == 0)
        {
            // Empty mesh: ProcessMeshData skips (6*num)+1 shorts, num sits
            // in the high byte (drawroom.cpp:790/869).
            const int num = (vertsWord >> 8) & 0xFF;
            p += (static_cast<size_t>(num) * 6 + 1) * 2;
        }
        else
        {
            p += static_cast<size_t>(nVerts) * 6;             // vertices
            int16_t nNorms; if (!s16(nNorms)) { err = "mesh normals truncated"; return false; }
            // CAUTION, zero means NORMALS: ProcessMeshData sets
            // "if (!nNorms) nNorms = nVerts" BEFORE the branch
            // (drawroom.cpp:833) - only NEGATIVE values mean prelight.
            // Whoever reads 0 as prelight copies the mesh short, and the
            // faces behind it are garbage. That is what joby2's braid
            // looked like after copying: a spike through the body, while
            // Andrea1's hair without such meshes stayed intact.
            if (nNorms >= 0) p += static_cast<size_t>(nVerts) * 6;  // normals, 3 per vertex
            else             p += static_cast<size_t>(nVerts) * 2;  // prelight, 1 per vertex
        }

        int16_t ngt4; if (!s16(ngt4)) { err = "mesh quads truncated"; return false; }
        p += static_cast<size_t>(ngt4) * 12;
        int16_t ngt3; if (!s16(ngt3)) { err = "mesh tris truncated"; return false; }
        p += static_cast<size_t>(ngt3) * 10;

        if (p > limit) { err = "mesh runs out of the block"; return false; }
        bytes = p - at;
        return true;
    }

    // Calls fn(faceTexOffset) for every face texture word of a mesh.
    template <typename F>
    bool forEachFaceTex(const std::vector<uint8_t>& b, size_t at, size_t limit, F fn, std::string& err)
    {
        size_t p = at;
        const int16_t vertsWord = rs16(b, p + 10);
        const int nVerts = vertsWord & 0xFF;
        p += 12;

        if (nVerts == 0)
            p += ((static_cast<size_t>((vertsWord >> 8) & 0xFF)) * 6 + 1) * 2;
        else
        {
            p += static_cast<size_t>(nVerts) * 6;
            const int16_t nNorms = rs16(b, p); p += 2;
            // 0 means normals, see meshSize.
            p += static_cast<size_t>(nVerts) * (nNorms >= 0 ? 6 : 2);
        }

        const int16_t ngt4 = rs16(b, p); p += 2;
        for (int i = 0; i < ngt4; ++i, p += 12) fn(p + 8);    // word 4 of 6
        const int16_t ngt3 = rs16(b, p); p += 2;
        for (int i = 0; i < ngt3; ++i, p += 10) fn(p + 6);    // word 3 of 5
        if (p > limit) { err = "mesh faces run out of the block"; return false; }
        return true;
    }

    // Length of an anim command in shorts including opcode (game/control.cpp,
    // AnimateItem command evaluation; DumpAnim uses the same values).
    int commandLen(int16_t type)
    {
        switch (type)
        {
        case 1: return 4;      // SetPosition, 3 parameters
        case 2: return 3;      // JumpVelocity, 2
        case 3: return 1;      // EmptyHands
        case 4: return 1;      // KillItem
        case 5: return 3;      // PlaySound, frame + SFX
        case 6: return 3;      // Flipeffect, frame + effect
        default: return 1;     // unknown: swallow only the opcode
        }
    }
}

bool Tr5Level::debugMeshWalk(int ptrIndex, int& nVerts, int& nNorms,
                             int& ngt4, int& ngt3, int& bytes) const
{
    if (ptrIndex < 0 || ptrIndex >= m_meshPtrs.count) return false;
    uint32_t ofs;
    std::memcpy(&ofs, m_bytes.data() + m_meshPtrs.dataAt + static_cast<size_t>(ptrIndex) * 4, 4);

    const size_t at = m_meshData.dataAt + ofs;
    const size_t limit = m_meshData.dataAt + static_cast<size_t>(m_meshData.count) * 2;
    size_t sz = 0;
    std::string err;
    if (!meshSize(m_bytes, at, limit, sz, err)) return false;
    bytes = static_cast<int>(sz);

    const int16_t vertsWord = rs16(m_bytes, at + 10);
    nVerts = vertsWord & 0xFF;
    size_t p = at + 12;
    if (nVerts == 0) { nNorms = 0; ngt4 = 0; ngt3 = 0; return true; }
    p += static_cast<size_t>(nVerts) * 6;
    nNorms = rs16(m_bytes, p); p += 2;
    p += static_cast<size_t>(nVerts) * (nNorms >= 0 ? 6 : 2);
    ngt4 = rs16(m_bytes, p); p += 2 + static_cast<size_t>(ngt4) * 12;
    ngt3 = rs16(m_bytes, p);
    return true;
}

//----------------------------------------------------------------------------
// Insert the textures of a TR3 package (#90).
//
// Deliberately HERE and not in Tr5Tr3Import.cpp: the page management is the
// most dangerous spot of the whole tool (user requirement: "at the texture
// pages only append as before, shift nothing - otherwise the level
// collapses"). The proven helpers for it live in this file; rebuilding them
// a second time would be exactly the mistake the requirement is meant to
// prevent. Attached, not rebuilt - copyMoveableFrom stays untouched.
//
// Returned in "mapping": TR3 object texture number -> new texinfo index.
// With it the caller then redirects the faces' texture words.
//----------------------------------------------------------------------------
bool Tr5Level::addTexturesFromTr3(const std::vector<uint8_t>& package,
                                  std::map<int,int>& mapping, std::string& error)
{
    char buf[256];
    mapping.clear();

    // Fetch a fresh block map (#104) - third entrance of the same kind.
    // On the TR3 path the sounds run BEFORE the textures (the order is a
    // rule: sounds -> SFX -> textures -> faces -> model), and
    // addSoundsFromTr3 shifts the main block without updating the map.
    // Until the texture workshop, the rebuild in phase B healed that;
    // without it, phase C spliced the texinfos to an outdated position and
    // the main block was gone ("item count 2926"). Noticed only at seed 3
    // WITHOUT items20 - the #104 sweep always ran with it, proof that a
    // sweep only tests the parameters it drives.
    if (!reparse(error)) { error = "block map: " + error; return false; }

    if (package.size() < 48 || std::memcmp(package.data(), "T35H", 4) != 0)
    { error = "not a TR3 model package"; return false; }
    if (rd32(package, 4) < 2) { error = "package without a texture part (version < 2)"; return false; }

    // --- Fast-forward to the texture part (layout see TR3Fetch) --------------
    const uint32_t nMeshes = rd32(package, 12), nAnims = rd32(package, 16);
    const uint32_t nFrameW = rd32(package, 20), nBoneW = rd32(package, 24);
    const uint32_t nSC = rd32(package, 28), nAD = rd32(package, 32), nCmd = rd32(package, 36);
    size_t p = 48;
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) { error = "package ends in the mesh list"; return false; }
        p += 4 + rd32(package, p);
    }
    p += size_t(nBoneW) * 4 + size_t(nFrameW) * 2 + size_t(nAnims) * SZ_ANIM
       + size_t(nSC) * SZ_CHANGE + size_t(nAD) * SZ_RANGE + size_t(nCmd) * 2;
    if (p + 4 > package.size()) { error = "package ends before the texture part"; return false; }

    struct Tr3Tex { int orig; uint16_t drawtype; int tile; uint8_t uv[8]; };
    std::vector<Tr3Tex> texEntries;
    const uint32_t nTexEntries = rd32(package, p); p += 4;
    for (uint32_t i = 0; i < nTexEntries; ++i)
    {
        if (p + 14 > package.size()) { error = "texture part ends prematurely"; return false; }
        Tr3Tex t;
        t.orig = rd16(package, p);
        t.drawtype = rd16(package, p + 2);
        t.tile = rd16(package, p + 4);
        std::memcpy(t.uv, package.data() + p + 6, 8);
        p += 14;
        texEntries.push_back(t);
    }

    const uint32_t nTiles = rd32(package, p); p += 4;
    const size_t tileAt = p;
    if (p + size_t(nTiles) * PAGE16 > package.size())
    { error = "tile data runs out of the package"; return false; }

    // --- Open the target blocks --------------------------------------------
    TexHeader dstTex;
    if (!parseTexHeader(m_bytes, dstTex, error)) return false;

    // Target via the texture workshop (#104): unpacked once, every import
    // works on the open blocks, packing happens at closing.
    if (!texWorkshopOpen(error)) return false;
    std::vector<uint8_t>& dst32 = m_tex32;
    std::vector<uint8_t>& dst16 = m_tex16;

    if (1 + dstTex.rt + dstTex.ot + dstTex.bt + int(nTiles) > MAX_PAGES)
    {
        std::snprintf(buf, sizeof(buf),
            "target would have %d texture pages - the engine holds at most %d",
            1 + dstTex.rt + dstTex.ot + dstTex.bt + int(nTiles), MAX_PAGES);
        error = buf;
        return false;
    }

    // --- READ the channel order of the 32-bit pages OFF THE TARGET ---------
    //
    // TR3 delivers ARGB1555. How TR5 arranges its 32-bit pages is not
    // assumed but measured: the target carries every page in both versions,
    // so the mapping can be read off its own data. We look for the channel
    // that matches the red component of the 16-bit pixel.
    int posR = 2, posG = 1, posB = 0, posA = 3;      // default: BGRA
    if (dst16.size() >= PAGE16 && dst32.size() >= PAGE32)
    {
        long long best = -1;
        const int candidates[2][3] = { { 2, 1, 0 }, { 0, 1, 2 } };   // BGRA, RGBA
        for (int k = 0; k < 2; ++k)
        {
            long long diff = 0;
            for (size_t px = 0; px < 4096; ++px)
            {
                const uint16_t w = rd16(dst16, px * 2);
                const int r5 = (w >> 10) & 0x1F, g5 = (w >> 5) & 0x1F, b5 = w & 0x1F;
                const int r8 = (r5 << 3) | (r5 >> 2);
                const int g8 = (g5 << 3) | (g5 >> 2);
                const int b8 = (b5 << 3) | (b5 >> 2);
                diff += std::abs(int(dst32[px * 4 + candidates[k][0]]) - r8);
                diff += std::abs(int(dst32[px * 4 + candidates[k][1]]) - g8);
                diff += std::abs(int(dst32[px * 4 + candidates[k][2]]) - b8);
            }
            if (best < 0 || diff < best)
            { best = diff; posR = candidates[k][0]; posG = candidates[k][1]; posB = candidates[k][2]; }
        }
    }

    // --- Insert the pages: ONLY append, at the same place as always --------
    const int insertFrom = dstTex.rt + dstTex.ot;
    std::map<int,int> tileRemap;
    int newPages = 0;

    for (uint32_t k = 0; k < nTiles; ++k)
    {
        const uint8_t* srcPage16 = package.data() + tileAt + size_t(k) * PAGE16;

        std::vector<uint8_t> page32(PAGE32);
        for (size_t px = 0; px < 256 * 256; ++px)
        {
            uint16_t w; std::memcpy(&w, srcPage16 + px * 2, 2);
            const int r5 = (w >> 10) & 0x1F, g5 = (w >> 5) & 0x1F, b5 = w & 0x1F;
            uint8_t* z = page32.data() + px * 4;
            z[posR] = uint8_t((r5 << 3) | (r5 >> 2));
            z[posG] = uint8_t((g5 << 3) | (g5 >> 2));
            z[posB] = uint8_t((b5 << 3) | (b5 >> 2));
            z[posA] = (w & 0x8000) ? 255 : 0;        // bit 15 = visible
        }

        // As with the TR5 transfer: only check against the STABLE pages
        // before the insertion point, so a reuse does not slip through
        // later insertions.
        int found = -1;
        const int stable = insertFrom + newPages;
        for (int d = 0; d < stable; ++d)
            if (std::memcmp(dst32.data() + size_t(d) * PAGE32, page32.data(), PAGE32) == 0)
            { found = d; break; }

        if (found >= 0) { tileRemap[int(k)] = found; continue; }

        const int fresh = insertFrom + newPages;
        dst32.insert(dst32.begin() + size_t(fresh) * PAGE32, page32.begin(), page32.end());
        dst16.insert(dst16.begin() + size_t(fresh) * PAGE16, srcPage16, srcPage16 + PAGE16);
        tileRemap[int(k)] = fresh;
        ++newPages;
    }

    if (newPages > 0)
    {
        // Workshop (#104): only the page counter in the header moves along
        // immediately - packing and reassembly happen ONCE at closing. The
        // main block does not shift through this, the block offsets for the
        // adjustment below stay valid without a reparse.
        wr16(m_bytes, 6, static_cast<uint16_t>(dstTex.ot + newPages));
        m_texDirty = true;

        // Adjust all STORED tile numbers from the insertion point on -
        // texinfos (bit 15 is a flag and stays) and sprites.
        for (int32_t i = 0; i < m_texinfos.count; ++i)
        {
            const size_t o = m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO + 2;
            const uint16_t w = rd16(m_bytes, o);
            const int tile = w & 0x7FFF;
            if (tile >= insertFrom)
                wr16(m_bytes, o, static_cast<uint16_t>((w & 0x8000) | (tile + newPages)));
        }
        for (int32_t i = 0; i < m_sprites.count; ++i)
        {
            const size_t o = m_sprites.dataAt + static_cast<size_t>(i) * SZ_SPRITE;
            const uint16_t w = rd16(m_bytes, o);
            if (w >= insertFrom) wr16(m_bytes, o, static_cast<uint16_t>(w + newPages));
        }
    }

    // --- Create the texinfos ------------------------------------------------
    //
    // Layout from global/types.h and specific/file.cpp:1089ff, read off:
    //   +0 drawtype, +2 tpage, +4 flag, then u1,v1,u2,v2,u3,v3,u4,v4
    //   as 8.8 fixed point. TR3 stores the same four corners as pixels.
    //
    // tpage DELIBERATELY gets no bit 15: only then does AdjustUV run the
    // half-texel correction (file.cpp:214 checks flag & 0x8000, and this bit
    // comes from tpage). Without it, the orientation value in flag & 7 is
    // never read either - the only quantity one would otherwise have to
    // guess.
    std::vector<uint8_t> newInfoBytes;
    int newCount = 0;
    for (const Tr3Tex& t : texEntries)
    {
        std::map<int,int>::const_iterator kr = tileRemap.find(t.tile);
        if (kr == tileRemap.end()) { error = "texture refers to a tile that is not in the package"; return false; }

        uint8_t rec[SZ_TEXINFO];
        std::memset(rec, 0, SZ_TEXINFO);
        const uint16_t draw = t.drawtype;
        const uint16_t tp = static_cast<uint16_t>(kr->second);
        std::memcpy(rec + 0, &draw, 2);
        std::memcpy(rec + 2, &tp, 2);
        for (int v = 0; v < 4; ++v)
        {
            const uint16_t u = static_cast<uint16_t>(t.uv[v * 2]) << 8;
            const uint16_t vv = static_cast<uint16_t>(t.uv[v * 2 + 1]) << 8;
            std::memcpy(rec + 6 + v * 4, &u, 2);
            std::memcpy(rec + 8 + v * 4, &vv, 2);
        }

        // Does the entry already exist? Then reuse it.
        int found = -1;
        for (int32_t i = 0; i < m_texinfos.count && found < 0; ++i)
            if (std::memcmp(m_bytes.data() + m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO,
                            rec, SZ_TEXINFO) == 0)
                found = i;
        for (int k = 0; k < newCount && found < 0; ++k)
            if (std::memcmp(newInfoBytes.data() + static_cast<size_t>(k) * SZ_TEXINFO, rec, SZ_TEXINFO) == 0)
                found = m_texinfos.count + k;

        if (found >= 0) { mapping[t.orig] = found; continue; }
        mapping[t.orig] = m_texinfos.count + newCount;
        newInfoBytes.insert(newInfoBytes.end(), rec, rec + SZ_TEXINFO);
        ++newCount;
    }

    if (newCount > 0)
    {
        int32_t v; std::memcpy(&v, m_bytes.data() + m_texinfos.countAt, 4);
        v += newCount;
        std::memcpy(m_bytes.data() + m_texinfos.countAt, &v, 4);

        std::vector<Splice> splices;
        splices.push_back({ m_texinfos.dataAt + static_cast<size_t>(m_texinfos.count) * SZ_TEXINFO,
                            std::move(newInfoBytes) });
        const size_t added = splices[0].bytes.size();
        applySplices(m_bytes, splices);

        m_mainSize += added;
        const int32_t newSize = static_cast<int32_t>(m_mainSize);
        std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
        std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

        if (!reparse(error)) { error = "after the texinfos: " + error; return false; }
    }

    std::snprintf(buf, sizeof(buf), "%d new pages, %d new texinfos (channels R=%d G=%d B=%d)",
                  newPages, newCount, posR, posG, posB);
    error = buf;              // success message for the caller
    return true;
}

//----------------------------------------------------------------------------
// Texture workshop (#104) - see Tr5Level.h. Opening unpacks both target
// blocks once, closing packs them once and reassembles the file ONCE. In
// between, header and main block of m_bytes stay untouched, only the page
// counter in the header (word 6) moves along per import - the MAX_PAGES
// check and the insertion point read it.
//----------------------------------------------------------------------------
bool Tr5Level::texWorkshopOpen(std::string& error)
{
    if (m_texOpen) return true;

    TexHeader t;
    if (!parseTexHeader(m_bytes, t, error)) return false;
    if (!inflateBlock(m_bytes, t.block32At, m_tex32, error)) { error = "target 32: " + error; return false; }
    if (!inflateBlock(m_bytes, t.block16At, m_tex16, error)) { error = "target 16: " + error; return false; }

    m_texOpen = true;
    m_texDirty = false;
    return true;
}

bool Tr5Level::texWorkshopClose(std::string& error)
{
    if (!m_texOpen) return true;

    if (m_texDirty)
    {
        TexHeader t;
        if (!parseTexHeader(m_bytes, t, error)) return false;

        // Reassemble the file: header, both grown blocks, the rest -
        // exactly the assembly that used to run per import.
        std::vector<uint8_t> b32, b16;
        deflateBlock(m_tex32, b32);
        deflateBlock(m_tex16, b16);

        std::vector<uint8_t> fresh;
        fresh.reserve(m_bytes.size() + b32.size() + b16.size());
        fresh.insert(fresh.end(), m_bytes.begin(), m_bytes.begin() + 10);
        fresh.insert(fresh.end(), b32.begin(), b32.end());
        fresh.insert(fresh.end(), b16.begin(), b16.end());
        fresh.insert(fresh.end(), m_bytes.begin() + t.blockMiscAt, m_bytes.end());
        m_bytes.swap(fresh);

        if (!reparse(error)) { error = "after the page insertion: " + error; return false; }
    }

    m_texOpen = false;
    m_texDirty = false;
    m_tex32.clear(); m_tex32.shrink_to_fit();
    m_tex16.clear(); m_tex16.shrink_to_fit();
    return true;
}

bool Tr5Level::copyMoveableFrom(const Tr5Level& src, int slotId, std::string& error,
                                int dstSlot, bool meshesOnly, int animCount)
{
    char buf[256];

    // Fetch a fresh block map (#104). Whoever wrote into the main block
    // before us without updating the map - addTrigger appends floordata,
    // say - leaves outdated dataAt values behind, and the splices below
    // would land off. Chance used to heal this: phase B rebuilt the file
    // per import and called reparse BEFORE phase C used the map. With the
    // texture workshop that rebuild is gone - the map must be fetched
    // fresh at the entrance itself, not as a side effect.
    if (!reparse(error)) { error = "block map: " + error; return false; }

    // The slot under which the copy is registered in the target. Divergent
    // only for the fallback slots of the cross enemy mix
    // (donor MESHSWAP1 -> 399-403).
    const int targetSlot = (dstSlot < 0) ? slotId : dstSlot;

    if (slotId == 0 || targetSlot == 0)
    {
        // Rule 1 from the weapon work. Slot 0's mesh tree is identical in
        // all levels, the body comes from LARA_SKIN (8) - copying would
        // gain nothing and take the level's animations away.
        error = "slot 0 (LARA) is not copied - it carries the level's animations";
        return false;
    }

    MoveableInfo mv;
    if (!src.moveableInfo(slotId, mv))
    {
        std::snprintf(buf, sizeof(buf), "source level does not know slot %d", slotId);
        error = buf;
        return false;
    }
    if (mv.nmeshes <= 0 || mv.meshIndex < 0 ||
        mv.meshIndex + mv.nmeshes > src.m_meshPtrs.count)
    { error = "source slot has implausible mesh data"; return false; }

    //========================================================================
    // Phase A: collect everything from the source. Up to here NOTHING is
    // changed - if a check fails, the target stays untouched.
    //========================================================================

    // --- Delimit the slices: the end of my data is the start of the -------
    // --- next slot's data (the same principle as when reading the    -------
    // --- animations: a slot does not know its own count).            -------
    std::vector<MoveableInfo> all;
    for (int s : src.moveableSlots())
    {
        MoveableInfo m;
        if (src.moveableInfo(s, m)) all.push_back(m);
    }
    auto nextStart = [&](long long mine, long long blockEnd, auto value) -> long long {
        long long best = blockEnd;
        for (const MoveableInfo& m : all)
        {
            const long long w = value(m);
            if (w > mine && w < best) best = w;
        }
        return best;
    };

    // meshesOnly: deliberately leave the anim, frame and bone slices empty -
    // see the reasoning at the prototype in Tr5Level.h (MESHSWAP1 carries an
    // anim_index without animations of its own).
    const long long boneStart = meshesOnly ? 0 : mv.boneIndex;
    const long long boneEnd = meshesOnly ? 0 : nextStart(boneStart, src.m_bones.count,
                                        [](const MoveableInfo& m) { return static_cast<long long>(m.boneIndex); });

    const long long frameStart = meshesOnly ? 0 : mv.frameOffset;
    long long frameEnd = meshesOnly ? 0 : nextStart(frameStart, static_cast<long long>(src.m_frames.count) * 2,
                                         [](const MoveableInfo& m) { return static_cast<long long>(m.frameOffset); });

    long long animStart = -1, animEnd = -1;
    if (!meshesOnly && mv.animIndex >= 0)
    {
        animStart = mv.animIndex;
        animEnd = nextStart(animStart, src.m_anims.count,
                            [](const MoveableInfo& m) { return static_cast<long long>(m.animIndex); });
        if (animEnd > src.m_anims.count) { error = "anim slice implausible"; return false; }

        // Explicit slice length (see the prototype): some types' control
        // reaches past the object boundary into the global anim table (Imp:
        // anim_index+45 as the death animation). Then the frame slice must
        // also grow past the highest frame pointer of the extended anims -
        // the pointer check below demands that each of them lies inside the
        // slice.
        if (animCount > 0)
        {
            animEnd = animStart + animCount;
            if (animEnd > src.m_anims.count)
            { error = "anim slice (explicit length) runs out of the table"; return false; }

            long long maxFp = frameStart;
            for (long long a = animStart; a < animEnd; ++a)
            {
                const uint32_t fp = rd32(src.m_bytes, src.m_anims.dataAt + static_cast<size_t>(a) * SZ_ANIM);
                if (fp > maxFp) maxFp = fp;
            }
            frameEnd = nextStart(maxFp, static_cast<long long>(src.m_frames.count) * 2,
                                 [](const MoveableInfo& m) { return static_cast<long long>(m.frameOffset); });
        }
    }

    // --- Animations with their appendix ------------------------------------
    std::vector<uint8_t> animBytes, changeBytes, rangeBytes, cmdBytes, frameBytes, boneBytes;
    long long chgStart = 0, chgEnd = 0, rngStart = 0, rngEnd = 0, cmdStart = 0, cmdEnd = 0;
    std::set<int> usedSfx;

    if (animStart >= 0)
    {
        chgStart = src.m_changes.count; chgEnd = 0;
        cmdStart = src.m_commands.count; cmdEnd = 0;

        for (long long a = animStart; a < animEnd; ++a)
        {
            const size_t o = src.m_anims.dataAt + static_cast<size_t>(a) * SZ_ANIM;
            const uint16_t nChg = rd16(src.m_bytes, o + 32);
            const uint16_t iChg = rd16(src.m_bytes, o + 34);
            const uint16_t nCmd = rd16(src.m_bytes, o + 36);
            const uint16_t iCmd = rd16(src.m_bytes, o + 38);

            if (nChg) { chgStart = std::min<long long>(chgStart, iChg); chgEnd = std::max<long long>(chgEnd, iChg + nChg); }

            if (nCmd)
            {
                // Commands have variable length - the list must be walked
                // to know its end. PlaySound commands are collected for the
                // sample takeover along the way.
                size_t c = src.m_commands.dataAt + static_cast<size_t>(iCmd) * 2;
                long long run = iCmd;
                for (int k = 0; k < nCmd; ++k)
                {
                    const int16_t type = rs16(src.m_bytes, c);
                    if (type == 5)
                        usedSfx.insert(rs16(src.m_bytes, c + 4) & 0x3FFF);
                    const int len = commandLen(type);
                    c += static_cast<size_t>(len) * 2;
                    run += len;
                }
                cmdStart = std::min<long long>(cmdStart, iCmd);
                cmdEnd = std::max<long long>(cmdEnd, run);
            }
        }
        if (chgEnd <= chgStart) { chgStart = 0; chgEnd = 0; }
        if (cmdEnd <= cmdStart) { cmdStart = 0; cmdEnd = 0; }

        // The ranges hang off the state changes
        rngStart = src.m_ranges.count; rngEnd = 0;
        for (long long c = chgStart; c < chgEnd; ++c)
        {
            const size_t o = src.m_changes.dataAt + static_cast<size_t>(c) * SZ_CHANGE;
            const uint16_t nR = rd16(src.m_bytes, o + 2);
            const uint16_t iR = rd16(src.m_bytes, o + 4);
            if (nR) { rngStart = std::min<long long>(rngStart, iR); rngEnd = std::max<long long>(rngEnd, iR + nR); }
        }
        if (rngEnd <= rngStart) { rngStart = 0; rngEnd = 0; }

        if (chgEnd > src.m_changes.count || rngEnd > src.m_ranges.count || cmdEnd > src.m_commands.count)
        { error = "anim appendix runs out of its blocks"; return false; }

        // Every frame pointer must lie in the frame slice BEFORE anything
        // is changed - otherwise the rebase later continues with an
        // underflow and nobody notices.
        for (long long a = animStart; a < animEnd; ++a)
        {
            const uint32_t fp = rd32(src.m_bytes, src.m_anims.dataAt + static_cast<size_t>(a) * SZ_ANIM);
            if (fp < frameStart || fp >= static_cast<uint64_t>(frameEnd))
            { error = "frame pointer of an animation lies outside the frame slice"; return false; }
        }

        auto slice = [&](const Tr5Level::Block& b, long long from, long long to, int elem, std::vector<uint8_t>& out) {
            out.assign(src.m_bytes.begin() + b.dataAt + from * elem,
                        src.m_bytes.begin() + b.dataAt + to * elem);
        };
        slice(src.m_anims, animStart, animEnd, SZ_ANIM, animBytes);
        slice(src.m_changes, chgStart, chgEnd, SZ_CHANGE, changeBytes);
        slice(src.m_ranges, rngStart, rngEnd, SZ_RANGE, rangeBytes);
        slice(src.m_commands, cmdStart, cmdEnd, 2, cmdBytes);

        // Core's own voicing mistakes in donor sets, straightened only in
        // the COPY - the original stays byte-true, because at home the
        // mistake does not show (or belongs to the scenery). So far one
        // documented case, found via the extended SFXLOG (anim=886
        // type=0x4000 in a dry room, #113):
        //  - joby3, Mafia2 (51), anim +17 frame 589: the water step
        //    (sfx 17) is the ONLY one in the set marked "land only" - all
        //    its siblings are "water only". In dry target levels it
        //    audibly splashed in the middle of the street.
        struct CmdSoundFix { int slot; uint16_t oldW; uint16_t newW; };
        static const CmdSoundFix kCmdSoundFixes[] = {
            { 51, 17 | 0x4000, 17 | 0x8000 },
        };
        for (const CmdSoundFix& fx : kCmdSoundFixes)
        {
            if (fx.slot != slotId) continue;
            for (long long a = animStart; a < animEnd; ++a)
            {
                const size_t o = src.m_anims.dataAt + static_cast<size_t>(a) * SZ_ANIM;
                const uint16_t nCmd = rd16(src.m_bytes, o + 36);
                const uint16_t iCmd = rd16(src.m_bytes, o + 38);
                if (!nCmd || iCmd < cmdStart) continue;
                // Walk locally in the cut block; the loop counts COMMANDS,
                // not words (the old TR3Insert lesson).
                size_t c = (static_cast<size_t>(iCmd) - static_cast<size_t>(cmdStart)) * 2;
                for (int k = 0; k < nCmd && c + 2 <= cmdBytes.size(); ++k)
                {
                    const int16_t type = rs16(cmdBytes, c);
                    const int len = commandLen(type);
                    if (len <= 0) break;
                    if (type == 5 && c + 6 <= cmdBytes.size() && rd16(cmdBytes, c + 4) == fx.oldW)
                    {
                        cmdBytes[c + 4] = static_cast<uint8_t>(fx.newW & 0xFF);
                        cmdBytes[c + 5] = static_cast<uint8_t>(fx.newW >> 8);
                    }
                    c += static_cast<size_t>(len) * 2;
                }
            }
        }
    }

    if (frameEnd > frameStart)
        frameBytes.assign(src.m_bytes.begin() + src.m_frames.dataAt + frameStart,
                          src.m_bytes.begin() + src.m_frames.dataAt + frameEnd);

    // --- Straighten the model offset ----------------------------------------
    // Not every model sits on the item origin. Where Core installed a figure
    // only as decoration on a structure, the offset lives in the FRAMES: in
    // the root displacement and in the frame box. The Crane Guy (55) is only
    // an ANIMATING4 at the crane console in joby2 - measured with ModelOffset
    // (center of the frame box | root):
    //
    //   Guard 37      -1 /  29 |   -1 -478  24   <- this is what centered looks like
    //   Mafia 51      -4 /  40 |   -1 -478  24
    //   Crane 55    -539 / 471 | -510 -473 361   <- offset
    //   Anim4 422   -540 / 458 | -510 -473 361   <- the same figure as scenery
    //
    // As an enemy he thus stands about half a sector next to his item. The
    // MODEL is corrected, not the item: moving the item would fix optics
    // and collision, but the AI takes its box and zone assignment from
    // item->pos and would think in the neighboring sector.
    //
    // The amount is subtracted from EVERY frame, not only the first - that
    // way the root motion within the animation stays untouched.
    struct ModelOffset { int slot; int dx; int dz; };
    static const ModelOffset kOffsets[] = {
        { 55, 510, -361 },
    };
    for (const ModelOffset& v : kOffsets)
    {
        if (v.slot != mv.slot || frameBytes.empty() || animBytes.empty()) continue;

        // The frames are walked via the ANIMATIONS, not with a fixed stride
        // through the slice: frame_size sits per animation in the header
        // (byte 5, given in words) and may differ between a model's anims,
        // because single-axis rotations need only one word. A guessed
        // stride would eventually run into the rotation data and shred the
        // model.
        //
        // Several anims can point at the same frame - so collect all starts
        // first, then touch each exactly once.
        std::set<size_t> starts;
        for (size_t a = 0; a + SZ_ANIM <= animBytes.size(); a += SZ_ANIM)
        {
            const uint32_t fp  = rd32(animBytes, a);
            const size_t   fsz = static_cast<size_t>(animBytes[a + 5]) * 2;
            const uint16_t fb  = rd16(animBytes, a + 24);
            const uint16_t fe  = rd16(animBytes, a + 26);
            if (fsz < 18 || fe < fb || fp < frameStart) continue;

            size_t pos = static_cast<size_t>(fp - frameStart);
            for (int k = 0; k <= fe - fb; ++k, pos += fsz)
            {
                if (pos + 18 > frameBytes.size()) break;
                starts.insert(pos);
            }
        }

        int corrected = 0;
        for (size_t f : starts)
        {
            auto adj = [&](size_t word, int d) {
                int16_t w = static_cast<int16_t>(rd16(frameBytes, f + word * 2));
                w = static_cast<int16_t>(w + d);
                frameBytes[f + word * 2]     = static_cast<uint8_t>(w & 0xFF);
                frameBytes[f + word * 2 + 1] = static_cast<uint8_t>((w >> 8) & 0xFF);
            };
            adj(0, v.dx); adj(1, v.dx);     // minX, maxX
            adj(4, v.dz); adj(5, v.dz);     // minZ, maxZ
            adj(6, v.dx); adj(8, v.dz);     // root x and z (y stays)
            ++corrected;
        }
        (void)corrected;   // cross-check runs via ModelOffset on the finished level
        break;
    }

    if (boneEnd > boneStart)
        boneBytes.assign(src.m_bytes.begin() + src.m_bones.dataAt + boneStart * 4,
                         src.m_bytes.begin() + src.m_bones.dataAt + boneEnd * 4);

    // --- Meshes: slices via the pointer table -------------------------------
    // Several pointers can point at the same mesh (the engine deduplicates
    // at load, drawroom.cpp:764). The mapping source offset -> new mesh is
    // therefore kept, instead of copying every occurrence individually.
    const size_t srcMeshBytes = static_cast<size_t>(src.m_meshData.count) * 2;
    std::vector<uint32_t> srcPtrs;
    for (int i = 0; i < mv.nmeshes; ++i)
        srcPtrs.push_back(rd32(src.m_bytes, src.m_meshPtrs.dataAt + (static_cast<size_t>(mv.meshIndex) + i) * 4));

    std::map<uint32_t, std::pair<size_t, size_t>> meshRegion;   // source offset -> (start in meshPack, length)
    std::vector<uint8_t> meshPack;
    std::set<int> srcTexinfoIdx;

    for (uint32_t ofs : srcPtrs)
    {
        if (meshRegion.count(ofs)) continue;
        if (ofs >= srcMeshBytes) { error = "mesh pointer outside the mesh block"; return false; }

        size_t sz = 0;
        if (!meshSize(src.m_bytes, src.m_meshData.dataAt + ofs, src.m_meshData.dataAt + srcMeshBytes, sz, error))
            return false;

        std::string faceErr;
        if (!forEachFaceTex(src.m_bytes, src.m_meshData.dataAt + ofs, src.m_meshData.dataAt + ofs + sz,
                            [&](size_t texAt) { srcTexinfoIdx.insert(rd16(src.m_bytes, texAt) & 0x7FFF); },
                            faceErr))
        { error = faceErr; return false; }

        // 4-byte alignment as in the original - mesh pointers there are
        // consistently multiples of 4, and it costs nothing.
        while (meshPack.size() % 4) meshPack.push_back(0);
        meshRegion[ofs] = { meshPack.size(), sz };
        meshPack.insert(meshPack.end(),
                        src.m_bytes.begin() + src.m_meshData.dataAt + ofs,
                        src.m_bytes.begin() + src.m_meshData.dataAt + ofs + sz);
    }

    for (int t : srcTexinfoIdx)
        if (t >= src.m_texinfos.count) { error = "mesh refers to a texinfo that does not exist"; return false; }

    // --- Tiles of the used texinfos -----------------------------------------
    std::set<int> srcTiles;
    for (int t : srcTexinfoIdx)
        srcTiles.insert(rd16(src.m_bytes, src.m_texinfos.dataAt + static_cast<size_t>(t) * SZ_TEXINFO + 2) & 0x7FFF);

    TexHeader srcTex, dstTex;
    if (!parseTexHeader(src.m_bytes, srcTex, error)) { error = "source: " + error; return false; }
    if (!parseTexHeader(m_bytes, dstTex, error)) { error = "target: " + error; return false; }

    const int srcPages = srcTex.rt + srcTex.ot + srcTex.bt;
    for (int t : srcTiles)
        if (t >= srcPages) { error = "texinfo refers to a page that does not exist"; return false; }

    std::vector<uint8_t> src32, src16;
    if (!inflateBlock(src.m_bytes, srcTex.block32At, src32, error)) { error = "source: " + error; return false; }
    if (!inflateBlock(src.m_bytes, srcTex.block16At, src16, error)) { error = "source: " + error; return false; }

    // Target via the texture workshop (#104): unpacked once, every import
    // works on the open blocks, packing happens at closing.
    if (!texWorkshopOpen(error)) { error = "target: " + error; return false; }
    std::vector<uint8_t>& dst32 = m_tex32;
    std::vector<uint8_t>& dst16 = m_tex16;

    //========================================================================
    // Phase B: texture pages into the target. Inserted at the end of the
    // object area (before the bump pages); every page that already exists
    // with identical content is reused - when importing a whole weapon set
    // from the same donor, the slots share their pages, and without the
    // check each of them would sit in the file several times.
    //========================================================================
    const int insertFrom = dstTex.rt + dstTex.ot;      // 0-based tile number
    std::map<int, int> tileRemap;                      // source tile -> target tile
    int newPages = 0;

    for (int t : srcTiles)
    {
        const uint8_t* page32 = src32.data() + static_cast<size_t>(t) * PAGE32;

        // Only check against the pages BEFORE the insertion point (and the
        // freshly inserted ones): their numbers are stable. A hit in the
        // bump area behind would shift by one at the next insertion - then
        // rather one page twice than one page wrong.
        int found = -1;
        const int stable = insertFrom + newPages;
        for (int d = 0; d < stable; ++d)
            if (std::memcmp(dst32.data() + static_cast<size_t>(d) * PAGE32, page32, PAGE32) == 0)
            { found = d; break; }

        if (found >= 0) { tileRemap[t] = found; continue; }

        const int fresh = insertFrom + newPages;
        dst32.insert(dst32.begin() + static_cast<size_t>(fresh) * PAGE32, page32, page32 + PAGE32);
        const uint8_t* page16 = src16.data() + static_cast<size_t>(t) * PAGE16;
        dst16.insert(dst16.begin() + static_cast<size_t>(fresh) * PAGE16, page16, page16 + PAGE16);
        tileRemap[t] = fresh;
        ++newPages;
    }

    if (1 + dstTex.rt + dstTex.ot + dstTex.bt + newPages > MAX_PAGES)
    {
        std::snprintf(buf, sizeof(buf),
            "target would have %d texture pages - the engine holds at most %d (Textures[], file.cpp:438)",
            1 + dstTex.rt + dstTex.ot + dstTex.bt + newPages, MAX_PAGES);
        error = buf;
        return false;
    }

    if (newPages > 0)
    {
        // Workshop (#104): only the page counter in the header moves along
        // immediately - packing and reassembly happen ONCE at closing. The
        // main block does not shift through this, the block offsets for the
        // adjustment below stay valid without a reparse.
        wr16(m_bytes, 6, static_cast<uint16_t>(dstTex.ot + newPages));
        m_texDirty = true;

        // Adjust all STORED tile numbers from the insertion point on -
        // those are the texinfos (the rooms' bump pages) and the sprites.
        // Bit 15 carries a flag in the texinfos and stays.
        for (int32_t i = 0; i < m_texinfos.count; ++i)
        {
            const size_t o = m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO + 2;
            const uint16_t w = rd16(m_bytes, o);
            const int tile = w & 0x7FFF;
            if (tile >= insertFrom)
                wr16(m_bytes, o, static_cast<uint16_t>((w & 0x8000) | (tile + newPages)));
        }
        for (int32_t i = 0; i < m_sprites.count; ++i)
        {
            const size_t o = m_sprites.dataAt + static_cast<size_t>(i) * SZ_SPRITE;
            const uint16_t w = rd16(m_bytes, o);
            if (w >= insertFrom)
                wr16(m_bytes, o, static_cast<uint16_t>(w + newPages));
        }
    }

    //========================================================================
    // Phase C: create texinfos (with reuse), remap the faces, append all
    // blocks in one stroke.
    //========================================================================
    std::map<int, int> texinfoRemap;
    std::vector<uint8_t> newTexinfos;
    int texinfoNew = 0;

    for (int t : srcTexinfoIdx)
    {
        uint8_t rec[SZ_TEXINFO];
        std::memcpy(rec, src.m_bytes.data() + src.m_texinfos.dataAt + static_cast<size_t>(t) * SZ_TEXINFO, SZ_TEXINFO);
        uint16_t w; std::memcpy(&w, rec + 2, 2);
        const uint16_t newW = static_cast<uint16_t>((w & 0x8000) | tileRemap[w & 0x7FFF]);
        std::memcpy(rec + 2, &newW, 2);

        // Does the entry already exist? With a second slot of the same
        // donor, yes.
        int found = -1;
        for (int32_t i = 0; i < m_texinfos.count && found < 0; ++i)
            if (std::memcmp(m_bytes.data() + m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO, rec, SZ_TEXINFO) == 0)
                found = i;
        for (int k = 0; k < texinfoNew && found < 0; ++k)
            if (std::memcmp(newTexinfos.data() + static_cast<size_t>(k) * SZ_TEXINFO, rec, SZ_TEXINFO) == 0)
                found = m_texinfos.count + k;

        if (found >= 0) { texinfoRemap[t] = found; continue; }
        texinfoRemap[t] = m_texinfos.count + texinfoNew;
        newTexinfos.insert(newTexinfos.end(), rec, rec + SZ_TEXINFO);
        ++texinfoNew;
    }

    // Remap the copied meshes' faces (in meshPack, not in the source).
    for (const auto& kv : meshRegion)
    {
        std::string faceErr;
        // Lift meshPack into a vector wrapper so forEachFaceTex fits.
        const size_t start = kv.second.first, len = kv.second.second;
        if (!forEachFaceTex(meshPack, start, start + len,
                            [&](size_t texAt) {
                                uint16_t w; std::memcpy(&w, meshPack.data() + texAt, 2);
                                const uint16_t newW = static_cast<uint16_t>((w & 0x8000) | texinfoRemap[w & 0x7FFF]);
                                std::memcpy(meshPack.data() + texAt, &newW, 2);
                            }, faceErr))
        { error = faceErr; return false; }
    }

    // Rebase: the copied blocks receive the target's deltas.
    const int32_t dstMeshBytesOld = m_meshData.count * 2;
    const int32_t dstPtrOld = m_meshPtrs.count;
    const int32_t dstAnimOld = m_anims.count;
    const int32_t dstChgOld = m_changes.count;
    const int32_t dstRngOld = m_ranges.count;
    const int32_t dstCmdOld = m_commands.count;
    const int32_t dstBoneOld = m_bones.count;
    const int32_t dstFrameBytesOld = m_frames.count * 2;

    for (size_t o = 0; o + SZ_ANIM <= animBytes.size(); o += SZ_ANIM)
    {
        std::vector<uint8_t>& a = animBytes;
        uint32_t fp; std::memcpy(&fp, a.data() + o, 4);
        fp = static_cast<uint32_t>(dstFrameBytesOld + (fp - frameStart));
        std::memcpy(a.data() + o, &fp, 4);

        uint16_t v;
        std::memcpy(&v, a.data() + o + 28, 2); v = static_cast<uint16_t>(v - animStart + dstAnimOld); std::memcpy(a.data() + o + 28, &v, 2);
        std::memcpy(&v, a.data() + o + 32, 2);
        if (v) // only if there are state changes does the index carry meaning
        {
            uint16_t i; std::memcpy(&i, a.data() + o + 34, 2);
            i = static_cast<uint16_t>(i - chgStart + dstChgOld); std::memcpy(a.data() + o + 34, &i, 2);
        }
        std::memcpy(&v, a.data() + o + 36, 2);
        if (v)
        {
            uint16_t i; std::memcpy(&i, a.data() + o + 38, 2);
            i = static_cast<uint16_t>(i - cmdStart + dstCmdOld); std::memcpy(a.data() + o + 38, &i, 2);
        }
    }
    for (size_t o = 0; o + SZ_CHANGE <= changeBytes.size(); o += SZ_CHANGE)
    {
        uint16_t n; std::memcpy(&n, changeBytes.data() + o + 2, 2);
        if (!n) continue;
        uint16_t i; std::memcpy(&i, changeBytes.data() + o + 4, 2);
        i = static_cast<uint16_t>(i - rngStart + dstRngOld); std::memcpy(changeBytes.data() + o + 4, &i, 2);
    }
    for (size_t o = 0; o + SZ_RANGE <= rangeBytes.size(); o += SZ_RANGE)
    {
        uint16_t i; std::memcpy(&i, rangeBytes.data() + o + 4, 2);
        i = static_cast<uint16_t>(i - animStart + dstAnimOld); std::memcpy(rangeBytes.data() + o + 4, &i, 2);
    }

    std::vector<uint8_t> newPtrs;
    for (uint32_t ofs : srcPtrs)
        push32(newPtrs, static_cast<uint32_t>(dstMeshBytesOld + meshRegion[ofs].first));

    // The object entry. Replaced or appended - both via the same bytes.
    // In the meshesOnly case, bone/frame/anim deliberately point at nothing
    // - at a mesh store the engine only reads mesh_index.
    uint8_t objRec[SZ_OBJ];
    {
        const int32_t s32 = targetSlot;                   std::memcpy(objRec + 0, &s32, 4);
        const int16_t nm = static_cast<int16_t>(mv.nmeshes); std::memcpy(objRec + 4, &nm, 2);
        const int16_t mi = static_cast<int16_t>(dstPtrOld);  std::memcpy(objRec + 6, &mi, 2);
        const int32_t bi = meshesOnly ? 0 : dstBoneOld;    std::memcpy(objRec + 8, &bi, 4);
        const uint32_t fo = meshesOnly ? 0
                          : static_cast<uint32_t>(dstFrameBytesOld + (mv.frameOffset - frameStart));
        std::memcpy(objRec + 12, &fo, 4);
        const uint16_t ai = (meshesOnly || mv.animIndex < 0) ? 0xFFFF
                          : static_cast<uint16_t>(mv.animIndex - animStart + dstAnimOld);
        std::memcpy(objRec + 16, &ai, 2);
        objRec[18] = 0; objRec[19] = 0;
    }

    bool replaced = false;
    for (int32_t i = 0; i < m_objects.count && !replaced; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * SZ_OBJ;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot != targetSlot) continue;
        std::memcpy(m_bytes.data() + o, objRec, SZ_OBJ);
        replaced = true;
    }

    // Adjust the counters BEFORE inserting: the counter bytes travel with
    // the splice, their content is automatically in the right place after.
    auto bump = [&](const Block& b, int32_t delta) {
        int32_t v; std::memcpy(&v, m_bytes.data() + b.countAt, 4);
        v += delta;
        std::memcpy(m_bytes.data() + b.countAt, &v, 4);
    };
    bump(m_meshData, static_cast<int32_t>(meshPack.size() / 2));
    bump(m_meshPtrs, static_cast<int32_t>(srcPtrs.size()));
    bump(m_anims, static_cast<int32_t>(animBytes.size() / SZ_ANIM));
    bump(m_changes, static_cast<int32_t>(changeBytes.size() / SZ_CHANGE));
    bump(m_ranges, static_cast<int32_t>(rangeBytes.size() / SZ_RANGE));
    bump(m_commands, static_cast<int32_t>(cmdBytes.size() / 2));
    bump(m_bones, static_cast<int32_t>(boneBytes.size() / 4));
    bump(m_frames, static_cast<int32_t>(frameBytes.size() / 2));
    bump(m_texinfos, texinfoNew);
    if (!replaced) bump(m_objects, 1);

    std::vector<Splice> splices;
    auto append = [&](const Block& b, int elem, std::vector<uint8_t>&& bytes) {
        if (bytes.empty()) return;
        splices.push_back({ b.dataAt + static_cast<size_t>(b.count) * elem, std::move(bytes) });
    };
    append(m_meshData, 2, std::move(meshPack));
    append(m_meshPtrs, 4, std::move(newPtrs));
    append(m_anims, SZ_ANIM, std::move(animBytes));
    append(m_changes, SZ_CHANGE, std::move(changeBytes));
    append(m_ranges, SZ_RANGE, std::move(rangeBytes));
    append(m_commands, 2, std::move(cmdBytes));
    append(m_bones, 4, std::move(boneBytes));
    append(m_frames, 2, std::move(frameBytes));
    if (!replaced)
        splices.push_back({ m_objects.dataAt + static_cast<size_t>(m_objects.count) * SZ_OBJ,
                            std::vector<uint8_t>(objRec, objRec + SZ_OBJ) });
    append(m_texinfos, SZ_TEXINFO, std::move(newTexinfos));

    size_t inMainBlock = 0;
    for (const Splice& s : splices) inMainBlock += s.bytes.size();

    applySplices(m_bytes, splices);

    // Update the doubly kept main block size.
    m_mainSize += inMainBlock;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    if (!reparse(error)) { error = "after the append: " + error; return false; }

    //========================================================================
    // Phase D: sounds. The anim commands name their PlaySound numbers
    // themselves; the weapons' shot and stop sounds appear in NO anim data
    // at all but in hard-wired SoundEffect() calls of the engine. Without
    // this table the HK stayed mute back then - the bug, until it was found
    // (see Plan.md of the weapon work).
    //========================================================================
    struct WeaponSfx { const int* slots; int nSlots; const int* sfx; int nSfx; };
    static const int sPistols[] = { 1, 334, 335 },         fPistols[] = { 8 };
    static const int sUzi[]      = { 2, 336, 337 },        fUzi[]      = { 43, 44 };
    static const int sShotgun[]  = { 3, 338, 339, 340 },   fShotgun[]  = { 45 };
    static const int sGrapple[]  = { 4, 341, 342, 343, 344 }, fGrapple[] = { 235 };
    static const int sHK[]       = { 5, 345, 346 },        fHK[]       = { 68, 69, 14 };
    static const int sRevolver[] = { 6, 347, 348 },        fRevolver[] = { 121 };
    static const WeaponSfx kWeapons[] = {
        { sPistols, 3, fPistols, 1 }, { sUzi, 3, fUzi, 2 }, { sShotgun, 4, fShotgun, 1 },
        { sGrapple, 5, fGrapple, 1 },   { sHK, 3, fHK, 3 },   { sRevolver, 3, fRevolver, 1 },
    };
    for (const WeaponSfx& w : kWeapons)
        for (int i = 0; i < w.nSlots; ++i)
            if (w.slots[i] == slotId)
            {
                usedSfx.insert(6); usedSfx.insert(7); usedSfx.insert(105);
                for (int k = 0; k < w.nSfx; ++k) usedSfx.insert(w.sfx[k]);
            }

    for (int sfx : usedSfx)
    {
        if (soundInfoIndex(sfx) >= 0) continue;      // the target already has it
        if (src.soundInfoIndex(sfx) < 0) continue;   // not even the source has it
        std::string sErr;
        if (!copySoundFrom(src, sfx, sfx, sErr))
        {
            // A missing sound does not break the level - unlike everything
            // above. Hence soft here instead of hard.
            continue;
        }
    }

    return true;
}

//----------------------------------------------------------------------------
// Implant a single Lara animation from a donor level (#53).
//
// Occasion: ANIM_KEYCARD (437), the card-swipe move at Key Hole 8. The anim
// SLOT exists in every level - Lara's table is the same length everywhere -
// but the IMAGE DATA is level-specific: outside joby2 the extra anims 435+
// are Core stubs whose frame pointers all point at the same clump of data
// (joby3: 436-439 -> one offset; measured with AnimCmd/FrameOfs). Played
// back, that is a frozen pose.
//
// Transplanted: the frame data (to the block end), rate/frame_size/state/
// speeds of the anim record, the anim commands (to the end of the command
// block, PlaySound/FlipEffect frame numbers converted to the target's
// numbering) and the PlaySound samples the target lacks.
// NOT transplanted: frame_base/frame_end (the target numbering stays),
// next_anim/next_frame (they refer into the target's OWN level numbering)
// and the state changes (they remain the target's).
//
// Repeatability in the patch run's sense is unnecessary - the run always
// starts from the vanilla file. A second call would append the data again.
//----------------------------------------------------------------------------
bool Tr5Level::copyLaraAnimFrom(const Tr5Level& src, int animNr, std::string& error)
{
    MoveableInfo dl, sl;
    if (!moveableInfo(0, dl) || dl.animIndex < 0) { error = "target has no Lara"; return false; }
    if (!src.moveableInfo(0, sl) || sl.animIndex < 0) { error = "donor has no Lara"; return false; }

    const long long d = static_cast<long long>(dl.animIndex) + animNr;
    const long long s = static_cast<long long>(sl.animIndex) + animNr;
    if (d < 0 || d >= m_anims.count) { error = "anim lies outside the table in the target"; return false; }
    if (s < 0 || s >= src.m_anims.count) { error = "anim lies outside the table in the donor"; return false; }

    const size_t so = src.m_anims.dataAt + static_cast<size_t>(s) * 40;
    const uint32_t foSrc = rd32(src.m_bytes, so);

    // Length of the frame data: up to the next larger frame offset of ANY
    // animation of the donor - the offsets are global across all objects,
    // and the keyframes are packed, their length is stored nowhere
    // individually.
    uint32_t foEnd = static_cast<uint32_t>(src.m_frames.count) * 2;
    for (int32_t i = 0; i < src.m_anims.count; ++i)
    {
        const uint32_t fo = rd32(src.m_bytes, src.m_anims.dataAt + static_cast<size_t>(i) * 40);
        if (fo > foSrc && fo < foEnd) foEnd = fo;
    }
    if (foEnd <= foSrc) { error = "donor's frame data cannot be delimited"; return false; }

    std::vector<uint8_t> frameBytes(src.m_bytes.begin() + src.m_frames.dataAt + foSrc,
                                    src.m_bytes.begin() + src.m_frames.dataAt + foEnd);

    // Cut out the commands and convert the frame numbers.
    const uint16_t nCmd = rd16(src.m_bytes, so + 36);
    const uint16_t iCmd = rd16(src.m_bytes, so + 38);
    const uint16_t sBase = rd16(src.m_bytes, so + 24);
    const size_t dstRec0 = m_anims.dataAt + static_cast<size_t>(d) * 40;
    const uint16_t dBase = rd16(m_bytes, dstRec0 + 24);

    std::vector<uint8_t> cmdBytes;
    std::vector<int> sfxUsed;
    {
        size_t c = src.m_commands.dataAt + static_cast<size_t>(iCmd) * 2;
        for (int k = 0; k < nCmd; ++k)
        {
            int16_t type; std::memcpy(&type, src.m_bytes.data() + c, 2);
            const int len = commandLen(type);
            if (len <= 0) { error = "unknown anim command in the donor"; return false; }
            std::vector<uint8_t> w(src.m_bytes.begin() + c, src.m_bytes.begin() + c + static_cast<size_t>(len) * 2);
            if (type == 5 || type == 6)
            {
                uint16_t frame; std::memcpy(&frame, w.data() + 2, 2);
                frame = static_cast<uint16_t>(dBase + (frame - sBase));
                std::memcpy(w.data() + 2, &frame, 2);
            }
            if (type == 5)
            {
                uint16_t sfx; std::memcpy(&sfx, w.data() + 4, 2);
                sfxUsed.push_back(sfx & 0x3FFF);
            }
            cmdBytes.insert(cmdBytes.end(), w.begin(), w.end());
            c += static_cast<size_t>(len) * 2;
        }
    }

    // Record the append positions BEFORE the splice.
    const uint32_t newFrameOfs = static_cast<uint32_t>(m_frames.count) * 2;
    const uint16_t newCmdIndex = static_cast<uint16_t>(m_commands.count);

    auto bump = [&](const Block& b, int32_t delta) {
        int32_t v; std::memcpy(&v, m_bytes.data() + b.countAt, 4);
        v += delta;
        std::memcpy(m_bytes.data() + b.countAt, &v, 4);
    };
    bump(m_frames, static_cast<int32_t>(frameBytes.size() / 2));
    bump(m_commands, static_cast<int32_t>(cmdBytes.size() / 2));

    std::vector<Splice> splices;
    if (!cmdBytes.empty())
        splices.push_back({ m_commands.dataAt + static_cast<size_t>(m_commands.count) * 2, std::move(cmdBytes) });
    splices.push_back({ m_frames.dataAt + static_cast<size_t>(m_frames.count) * 2, std::move(frameBytes) });

    size_t inMainBlock = 0;
    for (const Splice& sp : splices) inMainBlock += sp.bytes.size();
    applySplices(m_bytes, splices);
    m_mainSize += inMainBlock;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);
    if (!reparse(error)) { error = "after the append: " + error; return false; }

    // Touch the target anim record AFTER the reparse - the block map is fresh.
    uint8_t* dr = m_bytes.data() + m_anims.dataAt + static_cast<size_t>(d) * 40;
    const uint8_t* sr = src.m_bytes.data() + so;
    std::memcpy(dr + 0, &newFrameOfs, 4);         // frame pointer onto the copy
    std::memcpy(dr + 4, sr + 4, 4);               // rate, frame_size, state
    std::memcpy(dr + 8, sr + 8, 16);              // speeds
    {
        const uint16_t sEnd = rd16(src.m_bytes, so + 26);
        const uint16_t dEnd = static_cast<uint16_t>(dBase + (sEnd - sBase));
        std::memcpy(dr + 26, &dEnd, 2);           // the source's length, the target's numbering
        std::memcpy(dr + 36, &nCmd, 2);
        std::memcpy(dr + 38, &newCmdIndex, 2);
    }

    // Backfill missing PlaySound samples - soft as in the model transfer:
    // a missing sound does not break the animation.
    for (int sfx : sfxUsed)
    {
        if (soundInfoIndex(sfx) >= 0) continue;
        if (src.soundInfoIndex(sfx) < 0) continue;
        std::string sErr;
        copySoundFrom(src, sfx, sfx, sErr);
    }
    return true;
}

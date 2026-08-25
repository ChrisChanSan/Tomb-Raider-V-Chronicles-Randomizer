/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 */
//----------------------------------------------------------------------------
// Extraction from a TR3 level - implementation of Tr3Source.h (#92).
//
// Taken over word for word from the TR3Fetch tool; only the frame changed
// (main -> library function), loading via a wide path, and the error exits,
// which now return a text instead of printing. The format bridge itself is
// UNTOUCHED - it is correct in exactly one place, and it should stay that
// way.
//
// The progress messages remain as printf: in the tool they are the familiar
// output, in the UI they run into the void.
//----------------------------------------------------------------------------
#include "Tr3Source.h"
#include <sys/stat.h>
#include <direct.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Reader
    {
        std::vector<uint8_t> b;
        size_t at = 0;
        bool   broken = false;

        bool load(const wchar_t* p)
        {
            FILE* f = nullptr;
            if (_wfopen_s(&f, p, L"rb") != 0) return false;
            if (!f) return false;
            std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            b.resize(static_cast<size_t>(n));
            const size_t g = std::fread(b.data(), 1, b.size(), f);
            std::fclose(f);
            return g == b.size();
        }
        bool fits(size_t n) { if (at + n > b.size()) { broken = true; return false; } return true; }
        uint16_t u16() { if (!fits(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
        uint32_t u32() { if (!fits(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
        void skip(size_t n) { if (fits(n)) at += n; }
    };

    int16_t  rs16(const std::vector<uint8_t>& b, size_t p) { int16_t v; std::memcpy(&v, &b[p], 2); return v; }
    uint16_t ru16(const std::vector<uint8_t>& b, size_t p) { uint16_t v; std::memcpy(&v, &b[p], 2); return v; }
    uint32_t ru32(const std::vector<uint8_t>& b, size_t p) { uint32_t v; std::memcpy(&v, &b[p], 4); return v; }

    void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v & 0xFF)); o.push_back(uint8_t(v >> 8)); }
    void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t((v >> (8*i)) & 0xFF)); }

    struct Moveable
    {
        uint32_t objectId; uint16_t numMeshes; uint16_t startingMesh;
        uint32_t meshTree; uint32_t frameOffset; uint16_t animIndex;
    };
}

bool Tr3Source::fetchMoveable(const std::wstring& levelPath, int objectId,
                             std::vector<uint8_t>& package, std::string& error)
{
    error.clear();
    package.clear();
    const int wanted = objectId;

    Reader r;
    if (!r.load(levelPath.c_str())) { error = "TR3 level not readable"; return false; }

    const uint32_t version = r.u32();
    if ((version >> 24) != 0xFF) { error = "file is not a TR3 level"; return false; }

    r.skip(768); r.skip(1024);
    const uint32_t nTex = r.u32();
    r.skip(size_t(nTex) * 65536);                // Textile8 (palette version)
    const size_t tex16At = r.at;                 // Textile16: ARGB1555, 131072 per tile
    r.skip(size_t(nTex) * 131072);
    r.u32();

    const size_t roomAt = r.at;
    const uint16_t nRooms = r.u16();

    // Try the room header variants instead of guessing (see TR3Peek).
    size_t afterRooms = 0;
    for (int amb = 2; amb <= 6 && !afterRooms; amb += 2)
    for (int light = 18; light <= 26 && !afterRooms; light += 2)
    {
        r.at = roomAt + 2; r.broken = false;
        for (uint16_t i = 0; i < nRooms && !r.broken; ++i)
        {
            r.skip(16);
            r.skip(size_t(r.u32()) * 2);
            r.skip(size_t(r.u16()) * 32);
            const uint16_t nZ = r.u16(), nX = r.u16();
            r.skip(size_t(nZ) * nX * 8);
            r.skip(size_t(amb));
            r.skip(size_t(r.u16()) * light);
            r.skip(size_t(r.u16()) * 20);
            r.skip(2 + 2 + 3);
        }
        if (r.broken) continue;
        const size_t mark = r.at;
        const uint32_t a = r.u32(); r.skip(size_t(a) * 2);
        const uint32_t b2 = r.u32(); r.skip(size_t(b2) * 2);
        const uint32_t c = r.u32(); r.skip(size_t(c) * 4);
        const uint32_t d = r.u32(); r.skip(size_t(d) * 32);
        if (!r.broken && a < 500000 && b2 < 2000000 && c < 20000 && d < 20000) afterRooms = mark;
        r.at = mark;
    }
    if (!afterRooms) { error = "room walk failed - unexpected room header"; return false; }
    r.at = afterRooms; r.broken = false;

    r.skip(size_t(r.u32()) * 2);                                    // floordata
    const uint32_t nMeshData = r.u32(); const size_t meshAt = r.at;
    r.skip(size_t(nMeshData) * 2);
    const uint32_t nPtr = r.u32(); const size_t ptrAt = r.at;
    r.skip(size_t(nPtr) * 4);
    const uint32_t nAnims = r.u32(); const size_t animAt = r.at;
    r.skip(size_t(nAnims) * 32);
    const uint32_t nSC = r.u32(); const size_t scAt = r.at;  r.skip(size_t(nSC) * 6);
    const uint32_t nAD = r.u32(); const size_t adAt = r.at;  r.skip(size_t(nAD) * 8);
    const uint32_t nCmd = r.u32(); const size_t cmdAt = r.at; r.skip(size_t(nCmd) * 2);
    const uint32_t nBone = r.u32(); const size_t boneAt = r.at; r.skip(size_t(nBone) * 4);
    const uint32_t nFrame = r.u32(); const size_t frameAt = r.at; r.skip(size_t(nFrame) * 2);
    const uint32_t nMv = r.u32();
    if (r.broken) { error = "file ends before the moveable table"; return false; }

    std::vector<Moveable> mv;
    for (uint32_t i = 0; i < nMv; ++i)
    {
        Moveable m;
        m.objectId = r.u32(); m.numMeshes = r.u16(); m.startingMesh = r.u16();
        m.meshTree = r.u32(); m.frameOffset = r.u32(); m.animIndex = r.u16();
        mv.push_back(m);
    }
    const Moveable* z = 0;
    for (const Moveable& m : mv) if (int(m.objectId) == wanted) { z = &m; break; }
    if (!z) { error = "object " + std::to_string(wanted) + " does not exist in this level"; return false; }

    uint32_t animEnd = nAnims;
    for (const Moveable& m : mv)
        if (m.animIndex > z->animIndex && m.animIndex < animEnd) animEnd = m.animIndex;
    const uint32_t animCount = animEnd - z->animIndex;

    std::printf("object %d: %u meshes, %u bones, anims %u..%u (%u)\n",
                wanted, z->numMeshes, z->numMeshes ? z->numMeshes - 1 : 0,
                z->animIndex, animEnd - 1, animCount);

    // --- On to the object textures (for the texture work, #90) -------------
    // The contested field sizes at the file end are again tried out instead
    // of guessed: the right combination is the one where the last table
    // ends exactly at the end of the file. If none is found, objTexAt stays
    // 0 and the report is simply shorter - the model travels regardless.
    uint32_t nObjTex = 0;
    size_t objTexAt = 0;
    {
        const size_t afterMv = r.at;
        for (int mapN = 256; mapN <= 370 && !objTexAt; mapN += 114)
        for (int itemSz = 22; itemSz <= 24 && !objTexAt; itemSz += 2)
        for (int boxSz = 8; boxSz <= 20 && !objTexAt; boxSz += 12)
        for (int zoneN = 10; zoneN <= 20 && !objTexAt; zoneN += 10)
        {
            r.at = afterMv; r.broken = false;
            r.skip(size_t(r.u32()) * 32);                // StaticMeshes
            r.skip(size_t(r.u32()) * 16);                // SpriteTextures
            r.skip(size_t(r.u32()) * 8);                 // SpriteSequences
            r.skip(size_t(r.u32()) * 16);                // Cameras
            r.skip(size_t(r.u32()) * 16);                // SoundSources
            const uint32_t nBoxes = r.u32();
            r.skip(size_t(nBoxes) * boxSz);
            r.skip(size_t(r.u32()) * 2);                 // Overlaps
            r.skip(size_t(nBoxes) * zoneN * 2);          // Zones
            r.skip(size_t(r.u32()) * 2);                 // AnimatedTextures
            const uint32_t nOT = r.u32();
            const size_t otAt = r.at;
            r.skip(size_t(nOT) * 20);
            r.skip(size_t(r.u32()) * itemSz);            // Items
            r.skip(8192);                                // LightMap
            r.skip(size_t(r.u16()) * 16);                // CinematicFrames
            r.skip(size_t(r.u16()));                     // DemoData
            r.skip(size_t(mapN) * 2);                    // SoundMap
            r.skip(size_t(r.u32()) * 8);                 // SoundDetails
            r.skip(size_t(r.u32()) * 4);                 // SampleIndices
            if (r.broken || r.at != r.b.size()) continue;
            if (!nOT || nOT > 20000) continue;
            nObjTex = nOT; objTexAt = otAt;
        }
        r.at = afterMv; r.broken = false;
    }

    // --- Rewrite the meshes -------------------------------------------------
    std::vector<std::vector<uint8_t>> meshes;
    int totalQuads = 0, totalTris = 0, totalColored = 0, maxVerts = 0;
    int firstTex = -1;                        // first texture the model itself uses
    struct ColorSpot { std::vector<uint8_t>* buffer; size_t at; };
    std::vector<ColorSpot> coloredSpots;
    for (uint16_t i = 0; i < z->numMeshes; ++i)
    {
        const uint32_t off = ru32(r.b, ptrAt + size_t(z->startingMesh + i) * 4);
        size_t p = meshAt + off;
        std::vector<uint8_t> o;

        for (int k = 0; k < 10; ++k) o.push_back(r.b[p + k]);       // centre + coll radius
        p += 10;
        const int16_t nVerts = rs16(r.b, p); p += 2;
        if (nVerts > 255) { error = "a mesh has more than 255 vertices - TR5 reads no more"; return false; }
        put16(o, uint16_t(nVerts));
        for (int v = 0; v < nVerts * 3; ++v) { put16(o, ru16(r.b, p)); p += 2; }
        const int16_t nNorms = rs16(r.b, p); p += 2;
        put16(o, uint16_t(nNorms));
        const int words = (nNorms >= 0) ? nVerts * 3 : nVerts;
        for (int v = 0; v < words; ++v) { put16(o, ru16(r.b, p)); p += 2; }
        if (nVerts > maxVerts) maxVerts = nVerts;

        // Textured quads: 4 vertex words + texture, plus a new effect word.
        const int16_t nQ = rs16(r.b, p); p += 2;
        std::vector<uint8_t> quads, tris;
        int qc = 0, tc = 0;
        for (int q = 0; q < nQ; ++q)
        {
            for (int w = 0; w < 5; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            put16(quads, 0);                                        // effect word
            ++qc;
        }
        const int16_t nT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nT; ++t)
        {
            for (int w = 0; w < 4; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            put16(tris, 0);
            ++tc;
        }
        // Colored faces: TR5 does not know them. They are taken over
        // textured so the geometry stays closed - but NOT with texture 0.
        // That is something arbitrary in the source level; with the T-Rex
        // it was a piece of Lara's leg, which then stuck under his jaw
        // (seen in game). Instead, the first texture the model itself uses
        // - that one is guaranteed to belong to it.
        const int16_t nCQ = rs16(r.b, p); p += 2;
        for (int q = 0; q < nCQ; ++q)
        {
            for (int w = 0; w < 4; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            p += 2;                                                 // discard color index
            coloredSpots.push_back({ &quads, quads.size() });
            put16(quads, 0); put16(quads, 0);
            ++qc; ++totalColored;
        }
        const int16_t nCT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nCT; ++t)
        {
            for (int w = 0; w < 3; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            p += 2;
            coloredSpots.push_back({ &tris, tris.size() });
            put16(tris, 0); put16(tris, 0);
            ++tc; ++totalColored;
        }

        // Remember this mesh's first real texture and set the colored spots
        // to it BEFORE the buffers go into the mesh.
        if (firstTex < 0)
        {
            if (nQ > 0)      firstTex = ru16(quads, 8);
            else if (nT > 0) firstTex = ru16(tris, 6);
        }
        for (const ColorSpot& fs : coloredSpots)
            if (firstTex >= 0)
            {
                const uint16_t v = uint16_t(firstTex);
                (*fs.buffer)[fs.at]     = uint8_t(v & 0xFF);
                (*fs.buffer)[fs.at + 1] = uint8_t(v >> 8);
            }
        coloredSpots.clear();

        put16(o, uint16_t(qc)); o.insert(o.end(), quads.begin(), quads.end());
        put16(o, uint16_t(tc)); o.insert(o.end(), tris.begin(), tris.end());
        totalQuads += qc; totalTris += tc;
        meshes.push_back(o);
    }

    // --- Which object textures does this model need? ------------------------
    //
    // The faces still carry the TR3 indices; only the installer redirects
    // them. For the texture work (#90) what counts is how many distinct
    // entries and above all how many distinct TILES lie behind them - every
    // tile becomes a 256x256 page in the target.
    std::vector<int> texIdx;      // used TR3 object textures, in discovery order
    std::vector<int> tiles;       // the tiles behind them, in discovery order
    for (const std::vector<uint8_t>& m : meshes)
    {
        size_t p = 10;
        const int16_t vw = rs16(m, p); p += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) p += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            p += size_t(nv) * 6;
            const int16_t nn = rs16(m, p); p += 2;
            p += size_t(nv) * (nn >= 0 ? 6 : 2);
        }
        auto note = [&](int t) {
            for (int x : texIdx) if (x == t) return;
            texIdx.push_back(t);
            if (t >= 0 && nObjTex && uint32_t(t) < nObjTex)
            {
                const int k = ru16(r.b, objTexAt + size_t(t) * 20 + 2) & 0x7FFF;
                for (int x : tiles) if (x == k) return;
                tiles.push_back(k);
            }
        };
        const int16_t nq = rs16(m, p); p += 2;
        for (int i = 0; i < nq; ++i, p += 12) note(ru16(m, p + 8) & 0x7FFF);
        const int16_t nt = rs16(m, p); p += 2;
        for (int i = 0; i < nt; ++i, p += 10) note(ru16(m, p + 6) & 0x7FFF);
    }
    std::printf("  object textures: %zu distinct on %zu tiles (",
                texIdx.size(), tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) std::printf("%s%d", i ? ", " : "", tiles[i]);
    std::printf(")\n");

    // --- Delimit the frame slice -------------------------------------------
    //
    // An animation's frame pointer is a byte offset into the level's GLOBAL
    // frame block. Whoever extracts only the object must cut the slice and
    // rebase the pointers to its start - otherwise they point somewhere in
    // the target. The end is the start of the next object, exactly as with
    // the anims.
    uint32_t frameEnd = nFrame * 2;
    for (const Moveable& m : mv)
        if (m.frameOffset > z->frameOffset && m.frameOffset < frameEnd) frameEnd = m.frameOffset;
    const uint32_t frameBytes = frameEnd - z->frameOffset;

    // --- Animations 32 -> 40 bytes, rebase frame pointers onto the slice ----
    std::vector<uint8_t> anims;
    int outside = 0;
    uint16_t animMin = 0xFFFF, animMax = 0;
    for (uint32_t i = 0; i < animCount; ++i)
    {
        const size_t a = animAt + size_t(z->animIndex + i) * 32;

        const uint32_t fo = ru32(r.b, a);
        if (fo < z->frameOffset || fo >= frameEnd) ++outside;
        put32(anims, fo - z->frameOffset);                          // rebased

        anims.push_back(r.b[a + 4]); anims.push_back(r.b[a + 5]);   // rate, size
        anims.push_back(r.b[a + 6]); anims.push_back(r.b[a + 7]);   // state
        for (int k = 8; k < 16; ++k) anims.push_back(r.b[a + k]);   // speed, accel
        for (int k = 0; k < 8; ++k) anims.push_back(0);             // lateral (TR4+)

        for (int k = 16; k < 20; ++k) anims.push_back(r.b[a + k]);  // FrameStart, FrameEnd
        const uint16_t nextAnim = ru16(r.b, a + 20);
        if (nextAnim < animMin) animMin = nextAnim;
        if (nextAnim > animMax) animMax = nextAnim;
        put16(anims, uint16_t(nextAnim - z->animIndex));            // rebase onto the slice
        for (int k = 22; k < 32; ++k) anims.push_back(r.b[a + k]);  // NextFrame, SC, cmd
    }

    // --- Output -------------------------------------------------------------
    std::vector<uint8_t> out;
    out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('H');
    put32(out, 2);                 // version 2: with texture part at the end
    put32(out, uint32_t(wanted));
    put32(out, z->numMeshes);
    put32(out, animCount);
    put32(out, frameBytes / 2);  // only this object's slice, in words
    put32(out, uint32_t(z->numMeshes ? (z->numMeshes - 1) * 4 : 0));
    put32(out, nSC); put32(out, nAD); put32(out, nCmd);
    put32(out, z->frameOffset);
    put32(out, z->animIndex);

    for (const std::vector<uint8_t>& m : meshes) { put32(out, uint32_t(m.size())); out.insert(out.end(), m.begin(), m.end()); }
    for (uint32_t i = 0; i < (z->numMeshes ? uint32_t(z->numMeshes - 1) * 4 : 0); ++i)
        put32(out, ru32(r.b, boneAt + (size_t(z->meshTree) + i) * 4));
    // --- Frames: rewrite single-axis rotations to TR5's width ---------------
    //
    // THE actual bridge between the games. Both pack a rotation into one or
    // two words, the top two bits pick the form. The two-word case is
    // IDENTICAL (phd_RotYXZpack computes (angles >> 10) & 0x3FF, then << 6,
    // in both games). The single-axis case is not:
    //
    //   TR3 (draw.cpp:183):  (word & 1023)  << 6     10 bits
    //   TR5 (draw.cpp:267):  (word & 0xFFF) << 4     12 bits
    //
    // A TR3 word taken over unchanged is thus read by TR5 with a quarter of
    // the angle - and because TR3 uses the overflow into negative, large
    // values flip entirely. Affected are exactly the SINGLE-AXIS JOINTS:
    // jaw, ankles, toes. Seen in game as twisted feet and an inverted maw
    // while the torso was fine - it rotates around all three axes and thus
    // goes through the identical two-word case.
    //
    // Lossless conversion: TR5 is meant to produce v << 6 but computes
    // << 4, so v << 2 belongs into the 12-bit field (v <= 1023, fits).
    {
        std::vector<uint8_t> frames(r.b.begin() + frameAt + z->frameOffset,
                                    r.b.begin() + frameAt + frameEnd);
        int converted = 0, outOfSlice = 0;
        std::vector<uint32_t> done;

        for (uint32_t a = 0; a < animCount; ++a)
        {
            const size_t ao = animAt + size_t(z->animIndex + a) * 32;
            const uint32_t fo = ru32(r.b, ao);
            const uint8_t rate = r.b[ao + 4];
            const uint8_t fsize = r.b[ao + 5];
            const uint16_t fs = ru16(r.b, ao + 16), fe = ru16(r.b, ao + 18);
            if (!rate || !fsize) continue;

            const int keyframes = (fe >= fs) ? int((fe - fs) / rate) + 1 : 1;
            for (int k = 0; k < keyframes; ++k)
            {
                const long long start = (long long)fo - z->frameOffset + (long long)k * fsize * 2;
                if (start < 0 || start + 18 > (long long)frames.size()) { ++outOfSlice; continue; }

                bool seen = false;
                for (uint32_t e : done) if (e == uint32_t(start)) { seen = true; break; }
                if (seen) continue;
                done.push_back(uint32_t(start));

                size_t p2 = size_t(start) + 18;          // bbox (6) + offset (3) words
                for (uint16_t m = 0; m < z->numMeshes; ++m)
                {
                    if (p2 + 2 > frames.size()) break;
                    const uint16_t w = ru16(frames, p2);
                    if (w & 0xC000)
                    {
                        const uint16_t v = w & 0x3FF;
                        const uint16_t nw = uint16_t((w & 0xC000) | ((v << 2) & 0xFFF));
                        frames[p2]     = uint8_t(nw & 0xFF);
                        frames[p2 + 1] = uint8_t(nw >> 8);
                        ++converted;
                        p2 += 2;
                    }
                    else p2 += 4;
                }
            }
        }
        std::printf("  rotations: %d single-axis words brought to TR5 width (%zu frames)\n",
                    converted, done.size());
        if (outOfSlice) std::printf("  CAUTION: %d frames lay outside the slice\n", outOfSlice);

        out.insert(out.end(), frames.begin(), frames.end());
    }
    out.insert(out.end(), anims.begin(), anims.end());
    out.insert(out.end(), r.b.begin() + scAt,  r.b.begin() + scAt  + size_t(nSC) * 6);

    // Jump table (AnimDispatch, 8 bytes: low, high, NextAnim, NextFrame).
    // Its NextAnim is a GLOBAL anim index of the source level - exactly as
    // with the anims themselves it must be rebased onto the slice, or it
    // points somewhere in the target. That was the crash on the first
    // attempt: the entries traveled unrebased, and as soon as the control
    // took a state change, the engine jumped into a foreign animation.
    // Entries outside the slice belong to other objects; they are never
    // read but get 0, so no wild value sits there.
    {
        std::vector<uint8_t> ad(r.b.begin() + adAt, r.b.begin() + adAt + size_t(nAD) * 8);
        int inside = 0, foreign = 0;
        for (size_t o = 0; o + 8 <= ad.size(); o += 8)
        {
            const uint16_t na = uint16_t(ad[o + 4] | (ad[o + 5] << 8));
            uint16_t nw = 0;
            if (na >= z->animIndex && na < animEnd) { nw = uint16_t(na - z->animIndex); ++inside; }
            else ++foreign;
            ad[o + 4] = uint8_t(nw & 0xFF);
            ad[o + 5] = uint8_t(nw >> 8);
        }
        std::printf("  jump table: %d entries rebased onto the slice, %d foreign ones zeroed\n", inside, foreign);
        out.insert(out.end(), ad.begin(), ad.end());
    }
    out.insert(out.end(), r.b.begin() + cmdAt, r.b.begin() + cmdAt + size_t(nCmd) * 2);

    // --- Texture part (package version 2) ----------------------------------
    //
    // Appended so old packages stay readable: whoever reads only up to the
    // command block gets exactly the old format.
    //
    //   uint32 textureCount
    //   per texture: origIndex(2), drawtype(2), tileInPackage(2),
    //                then 8 bytes u1,v1,u2,v2,u3,v3,u4,v4 as PIXELS
    //   uint32 tileCount
    //   per tile: 131072 bytes ARGB1555 (TR3's 16-bit version)
    //
    // The UVs stay PIXELS here. Only the installer shifts them 8 bits to
    // the left - TR5 stores them as 8.8 fixed point (file.cpp:1096).
    {
        put32(out, uint32_t(texIdx.size()));
        int written = 0;
        for (int t : texIdx)
        {
            if (t < 0 || uint32_t(t) >= nObjTex) { put16(out, uint16_t(t)); put16(out, 0); put16(out, 0);
                                                   for (int k = 0; k < 8; ++k) out.push_back(0); continue; }
            const size_t o = objTexAt + size_t(t) * 20;
            const uint16_t attribute = ru16(r.b, o);
            const int tile = ru16(r.b, o + 2) & 0x7FFF;

            int local = 0;
            for (size_t i = 0; i < tiles.size(); ++i) if (tiles[i] == tile) { local = int(i); break; }

            put16(out, uint16_t(t));
            put16(out, attribute);
            put16(out, uint16_t(local));
            for (int v = 0; v < 4; ++v)
            {
                out.push_back(r.b[o + 4 + v * 4 + 1]);   // Xpixel
                out.push_back(r.b[o + 4 + v * 4 + 3]);   // Ypixel
            }
            ++written;
        }

        put32(out, uint32_t(tiles.size()));
        for (int k : tiles)
        {
            if (k < 0 || uint32_t(k) >= nTex)
            { error = "a texture tile lies outside the texture pages"; return false; }
            const size_t at = tex16At + size_t(k) * 131072;
            out.insert(out.end(), r.b.begin() + at, r.b.begin() + at + 131072);
        }
        std::printf("  texture part: %d entries, %zu tiles of 131072 bytes\n",
                    written, tiles.size());
    }

    // A file used to be written here. As a library we return the package -
    // the caller decides whether it goes to disk (tool) or is processed
    // directly (randomizer run).
    package = out;

    // --- Cross-check: does the TR5 walker run through cleanly? --------------
    //
    // Rebuilt after the engine's ProcessMeshData (drawroom.cpp), identical
    // to meshSize() in Tr5Transfer.cpp. If the computed length matches the
    // written one to the byte, the layout is right - that is the real proof
    // of the format bridge, not the mere byte output.
    int bad = 0;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        const std::vector<uint8_t>& m = meshes[i];
        size_t p = 10;
        const int16_t vw = rs16(m, p); p += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) p += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            p += size_t(nv) * 6;
            const int16_t nn = rs16(m, p); p += 2;
            p += size_t(nv) * (nn >= 0 ? 6 : 2);
        }
        const int16_t q = rs16(m, p); p += 2; p += size_t(q) * 12;
        const int16_t t = rs16(m, p); p += 2; p += size_t(t) * 10;
        if (p != m.size())
        {
            std::printf("  MESH %zu DOES NOT FIT: walker ends at %zu, written %zu\n", i, p, m.size());
            ++bad;
        }
    }
    std::printf("  cross-check TR5 walker: %zu meshes checked, %d deviations\n",
                meshes.size(), bad);

    size_t meshBytes = 0; for (const std::vector<uint8_t>& m : meshes) meshBytes += m.size();
    std::printf("  meshes rewritten: %zu bytes, %d quads, %d tris (%d of them from colored faces)\n",
                meshBytes, totalQuads, totalTris, totalColored);
    std::printf("  largest mesh: %d vertices\n", maxVerts);
    std::printf("  anims: %u of 40 bytes, frame slice %u bytes (%u..%u), bones %u words\n",
                animCount, frameBytes, z->frameOffset, frameEnd,
                z->numMeshes ? (z->numMeshes - 1) * 4 : 0);
    std::printf("  frame pointers outside the slice: %d   jump targets %u..%u (slice %u..%u)\n",
                outside, animMin, animMax, z->animIndex, animEnd - 1);
    if (outside) std::printf("  CAUTION: frame pointers point out of the slice\n");
    if (animMin < z->animIndex || animMax >= animEnd)
        std::printf("  CAUTION: an anim jumps out of its own slice\n");
    std::printf("  package done: %zu bytes\n", package.size());
    return true;
}

//----------------------------------------------------------------------------
// Sound extraction from MAIN.SFX (#92, taken over from TR3Sound).
//----------------------------------------------------------------------------
namespace
{
    // MAIN.SFX is a chain of concatenated RIFF WAVs. There is no directory
    // - you walk through and remember start and length of every block. Odd
    // lengths are padded to even.
    bool waveDirectory(const std::vector<uint8_t>& b,
                           std::vector<uint32_t>& at, std::vector<uint32_t>& len)
    {
        for (size_t p = 0; p + 8 <= b.size(); )
        {
            if (b[p] == 'R' && b[p+1] == 'I' && b[p+2] == 'F' && b[p+3] == 'F')
            {
                const uint32_t n = ru32(b, p + 4) + 8;
                if (p + n > b.size()) break;
                at.push_back(static_cast<uint32_t>(p));
                len.push_back(n);
                p += n;
                if (n & 1) ++p;
            }
            else ++p;
        }
        return !at.empty();
    }
}

bool Tr3Source::fetchSounds(const std::wstring& sfxPath,
                            const std::vector<SoundRequest>& requests,
                            std::vector<uint8_t>& package,
                            std::string& error)
{
    error.clear();
    package.clear();

    Reader sfx;
    if (!sfx.load(sfxPath.c_str())) { error = "MAIN.SFX not readable"; return false; }

    std::vector<uint32_t> at, len;
    if (!waveDirectory(sfx.b, at, len))
    { error = "not a single RIFF in MAIN.SFX - wrong file?"; return false; }

    // Package header: magic, version 2, count (filled in at the end).
    std::vector<uint8_t> out;
    out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('S');
    for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 2 : 0);
    for (int i = 0; i < 4; ++i) out.push_back(0);

    for (const SoundRequest& k : requests)
    {
        if (k.waves.empty())
        { error = "sound " + std::to_string(k.tr3Id) + " without a wave list"; return false; }

        for (int w : k.waves)
            if (w < 1 || size_t(w) > at.size())
            {
                error = "wave " + std::to_string(w) + " does not exist in this MAIN.SFX ("
                       + std::to_string(at.size()) + " present)";
                return false;
            }

        // Header per sound: id, then three words the installer builds anew
        // (volume, chance, characteristics - a template would drag pitch
        // and random detune along, see Tr5Tr3Import.cpp).
        put16(out, uint16_t(k.tr3Id)); put16(out, 0); put16(out, 0); put16(out, 0);
        put16(out, uint16_t(k.waves.size()));

        for (int w : k.waves)
        {
            const size_t i = size_t(w) - 1;          // TRsound counts from 1
            put32(out, len[i]);
            out.insert(out.end(), sfx.b.begin() + at[i], sfx.b.begin() + at[i] + len[i]);
        }
    }

    out[8]  = uint8_t(requests.size() & 0xFF);
    out[9]  = uint8_t((requests.size() >> 8) & 0xFF);
    package = out;
    return true;
}

//----------------------------------------------------------------------------
// Rewrite the SFX numbers of the anim commands (#92, taken over from TR3Insert).
//
// TWO TRAPS LIVE IN HERE, both paid for dearly:
//
// 1. "count" in the anim header counts COMMANDS, not words. A command takes
//    1 to 4 words depending on its kind. Whoever runs the loop up to
//    "start + count" with the WORD index sees only the first commands and
//    notices nothing - the report about unresolved numbers can only report
//    what it has read. With the Compsognathus, the second occurrence of 243
//    stayed put that way, and 243 in TR5 is SFX_GENERIC_HEAVY_FEET.
//
// 2. Only touch the commands that THIS object's animations point at. The
//    package carries the source level's whole command block.
//----------------------------------------------------------------------------
bool Tr3Source::remapSfx(std::vector<uint8_t>& package,
                           const std::vector<std::pair<int,int>>& mapping,
                           const std::vector<int>& muted,
                           int muteSlot,
                           std::vector<int>& unresolved,
                           std::string& error)
{
    error.clear();
    unresolved.clear();

    if (package.size() < 48) { error = "model package too small"; return false; }

    const uint32_t nMeshes = ru32(package, 12), nAnims = ru32(package, 16);
    const uint32_t nFrameW = ru32(package, 20), nBoneW = ru32(package, 24);
    const uint32_t nSC = ru32(package, 28), nAD = ru32(package, 32), nCmd = ru32(package, 36);

    size_t p = 48;
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) { error = "model package breaks off in the meshes"; return false; }
        p += 4 + ru32(package, p);
    }
    p += size_t(nBoneW) * 4 + size_t(nFrameW) * 2 + size_t(nAnims) * 40
       + size_t(nSC) * 6 + size_t(nAD) * 8;

    const size_t cmdAt = p;
    if (cmdAt + size_t(nCmd) * 2 > package.size())
    { error = "command block not found in the model package"; return false; }
    const size_t animAt = cmdAt - size_t(nAD) * 8 - size_t(nSC) * 6 - size_t(nAnims) * 40;

    // Muting is just another mapping - onto an empty soundmap slot, where
    // the engine bails out instead of playing something foreign.
    std::vector<std::pair<int,int>> all = mapping;
    for (int s : muted) all.push_back(std::make_pair(s, muteSlot));

    for (uint32_t a = 0; a < nAnims; ++a)
    {
        const size_t ao = animAt + size_t(a) * 40;
        const uint16_t count = ru16(package, ao + 36);
        if (!count) continue;
        const uint16_t start = ru16(package, ao + 38);

        uint32_t i = start;
        for (uint16_t k = 0; k < count && i < nCmd; ++k)       // count COMMANDS
        {
            const uint16_t kind = ru16(package, cmdAt + size_t(i) * 2);
            ++i;
            int follow = 0;
            switch (kind)
            {
            case 1: follow = 3; break;   // SetPosition
            case 2: follow = 2; break;   // JumpVelocity
            case 5: follow = 2; break;   // PlaySound: frame + SFX
            case 6: follow = 2; break;   // Flipeffect
            default: follow = 0; break;
            }

            if (kind == 5 && i + 1 < nCmd)
            {
                const size_t spot = cmdAt + size_t(i + 1) * 2;
                const uint16_t word = ru16(package, spot);
                const int nr = word & 0x0FFF;    // bits 12-15 are conditions
                bool hit = false;
                for (const auto& z : all)
                    if (z.first == nr)
                    {
                        const uint16_t nw = uint16_t((word & 0xF000) | (z.second & 0x0FFF));
                        std::memcpy(&package[spot], &nw, 2);
                        hit = true;
                        break;
                    }
                if (!hit)
                {
                    bool seen = false;
                    for (int o : unresolved) if (o == nr) { seen = true; break; }
                    if (!seen) unresolved.push_back(nr);
                }
            }
            i += follow;
        }
    }
    return true;
}

//----------------------------------------------------------------------------
// The four guests from Tomb Raider 3 (#92).
//
// All numbers are MEASURED, not assumed:
//   - object numbers from the tomb3 enum, cross-checked (VULTURE is 29
//     there and matches the user's note)
//   - target slots checked with ModelOffset against all 16 TR5 levels: none
//     carries a model there
//   - wave numbers auditioned in the TRsound tool and recovered from the
//     finished sound packages by finding the wave data in MAIN.SFX again
//   - volumes tuned by the user in game
//----------------------------------------------------------------------------
std::vector<Tr3Source::DinoSource> Tr3Source::dinos()
{
    std::vector<DinoSource> d;

    {   // T-Rex on SAS. The footstep is the exception: the anim commands
        // call 88, but the sound heard in game is the dull two-variant one
        // of 250 - cross-checked by the user against the original.
        DinoSource t;
        t.slot = 31; t.name = "T-Rex"; t.source = "tr3";
        t.file = "CRASH.TR2"; t.object = 287; t.soundBase = 445;
        t.sounds = { { 84, {87} }, { 87, {89} }, { 88, {256,257} },
                      { 89, {91} }, { 255, {262} } };
        t.muted = { 143 };                 // TR3 "Falling", taken in TR5
        t.volumes  = { 100, 130, 150, 100, 140 };
        d.push_back(t);
    }
    {   // Raptor on REAPER (used to be a water jellyfish).
        DinoSource t;
        t.slot = 75; t.name = "Raptor"; t.source = "tr3";
        t.file = "CRASH.TR2"; t.object = 288; t.soundBase = 438;
        t.sounds = { { 250, {256,257} }, { 251, {258} }, { 252, {259} },
                      { 253, {260} }, { 254, {261} }, { 255, {262} }, { 256, {263} } };
        t.muted = { 143 };
        t.volumes  = { 100, 110, 110, 110, 130, 120, 120 };
        d.push_back(t);
    }
    {   // Compsognathus on SAILOR (used to be a sailor without a model).
        DinoSource t;
        t.slot = 53; t.name = "Compy"; t.source = "tr3";
        t.file = "CRASH.TR2"; t.object = 34; t.soundBase = 430;
        t.sounds = { { 239, {242} }, { 240, {243} }, { 241, {244} },
                      { 242, {245} }, { 243, {246,247} } };
        t.volumes  = { 100, 100, 100, 110, 70 };
        d.push_back(t);
    }
    {   // Vulture on CROW. Model from The Lost Artifact, sounds from TR3 -
        // see sfxPath().
        DinoSource t;
        t.slot = 43; t.name = "Geier"; t.source = "tr3gold";
        t.file = "chunnel.tr2"; t.object = 29; t.soundBase = 420;
        t.sounds = { { 182, {192} }, { 183, {193,194} }, { 184, {195} }, { 185, {196} } };
        t.muted = { 56 };                  // TR3: Lara's footstep, taken in TR5
        t.volumes  = { 100, 100, 100, 100 };
        d.push_back(t);
    }
    return d;
}

std::wstring Tr3Source::sfxPath(const DinoSource&,
                                const std::wstring& tr3Dir, const std::wstring&)
{
    // ALWAYS the MAIN.SFX of Tomb Raider 3 - for the vulture too. Gold
    // ships its own, but the auditioned wave numbers refer to the TR3 one.
    // The Gold parameter stays in the interface so the intent is visible
    // and nobody "cleans it up" later.
    return tr3Dir + L"\\MAIN.SFX";
}

std::wstring Tr3Source::levelPath(const DinoSource& d,
                                  const std::wstring& tr3Dir, const std::wstring& tr3GoldDir)
{
    const std::wstring& base = (std::strcmp(d.source, "tr3gold") == 0) ? tr3GoldDir : tr3Dir;
    std::wstring name;
    for (const char* p = d.file; *p; ++p) name.push_back(wchar_t((unsigned char)*p));
    return base + L"\\" + name;
}

//----------------------------------------------------------------------------
// Redirect the faces' texture words to the new texinfos (#92, from TR3Insert).
//
// The same walker as everywhere: mesh header 10 bytes, then the vertex
// word; 0 normals means NORMALS. Bit 15 of the texture word is a flag and
// stays - it decides whether AdjustUVs runs the half-texel correction.
//----------------------------------------------------------------------------
int Tr3Source::remapTextures(std::vector<uint8_t>& package, const std::map<int,int>& mapping)
{
    const uint32_t nMeshes = ru32(package, 12);
    size_t p = 48;
    int swapped = 0;

    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) return -1;
        const uint32_t len = ru32(package, p); p += 4;
        const size_t end = p + len;
        size_t q = p + 10;
        if (q + 2 > end) return -1;

        const int16_t vw = rs16(package, q); q += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) q += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            q += size_t(nv) * 6;
            const int16_t nn = rs16(package, q); q += 2;
            q += size_t(nv) * (nn >= 0 ? 6 : 2);
        }

        auto redirect = [&](size_t at) {
            const uint16_t w = ru16(package, at);
            std::map<int,int>::const_iterator f = mapping.find(w & 0x7FFF);
            if (f == mapping.end()) return;
            const uint16_t nw = uint16_t((w & 0x8000) | (f->second & 0x7FFF));
            std::memcpy(&package[at], &nw, 2);
            ++swapped;
        };

        const int16_t nq = rs16(package, q); q += 2;
        for (int k = 0; k < nq; ++k, q += 12) redirect(q + 8);
        const int16_t nt = rs16(package, q); q += 2;
        for (int k = 0; k < nt; ++k, q += 10) redirect(q + 6);

        p = end;
    }
    return swapped;
}

//----------------------------------------------------------------------------
// Cache for the extracted packages (#103).
//----------------------------------------------------------------------------
namespace
{
    bool readFile(const std::wstring& p, std::vector<uint8_t>& b)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return false;
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        if (n <= 0) { std::fclose(f); return false; }
        b.resize(static_cast<size_t>(n));
        const size_t g = std::fread(b.data(), 1, b.size(), f);
        std::fclose(f);
        return g == b.size();
    }

    bool writeFile(const std::wstring& p, const std::vector<uint8_t>& b)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, p.c_str(), L"wb") != 0 || !f) return false;
        const size_t g = std::fwrite(b.data(), 1, b.size(), f);
        std::fclose(f);
        return g == b.size();
    }

    // Source file fingerprint: size and modification time. The two together
    // are enough to recognize a reinstalled or patched version - and it
    // costs no pass over 11 MB like a checksum would.
    std::string sourceStamp(const std::wstring& path)
    {
        struct _stat64 s;
        if (_wstat64(path.c_str(), &s) != 0) return std::string();
        return std::to_string((long long)s.st_size) + "-" + std::to_string((long long)s.st_mtime);
    }

    std::wstring cacheName(const std::wstring& dir, const char* name, const char* part)
    {
        std::wstring w = dir + L"\\";
        for (const char* p = name; *p; ++p) w.push_back(wchar_t((unsigned char)*p));
        w += L"_";
        for (const char* p = part; *p; ++p) w.push_back(wchar_t((unsigned char)*p));
        return w + L".bin";
    }
}

bool Tr3Source::fetchCached(const DinoSource& d,
                             const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                             const std::wstring& cacheDir,
                             std::vector<uint8_t>& model, std::vector<uint8_t>& sounds,
                             std::string& error)
{
    const std::wstring lvlPath = levelPath(d, tr3Dir, tr3GoldDir);
    const std::wstring sfx     = sfxPath(d, tr3Dir, tr3GoldDir);

    // Expected stamp: both source files together. If one changes, the entry
    // is void.
    const std::string stamp = sourceStamp(lvlPath) + "|" + sourceStamp(sfx);

    const std::wstring mFile = cacheName(cacheDir, d.name, "modell");
    const std::wstring kFile = cacheName(cacheDir, d.name, "sounds");
    const std::wstring nFile = cacheName(cacheDir, d.name, "kennung");

    if (!cacheDir.empty() && !stamp.empty())
    {
        std::vector<uint8_t> old;
        if (readFile(nFile, old))
        {
            const std::string was(old.begin(), old.end());
            if (was == stamp && readFile(mFile, model) && readFile(kFile, sounds))
            {
                std::printf("  %s: from the cache\n", d.name);
                return true;
            }
        }
    }

    model.clear();
    sounds.clear();
    if (!fetchMoveable(lvlPath, d.object, model, error))
    { error = std::string(d.name) + ": " + error; return false; }
    if (!fetchSounds(sfx, d.sounds, sounds, error))
    { error = std::string(d.name) + " (sounds): " + error; return false; }

    // Storing is optional: if it fails (write-protected folder, full disk),
    // the run still works - it is just slow again next time. A cache must
    // never be the reason something does not work.
    if (!cacheDir.empty() && !stamp.empty())
    {
        // Create the folder if it is missing. This used to sit with the
        // caller - and the command line did not have it, so every store
        // silently failed and the cache stayed empty. A function that uses
        // a folder should be able to create it.
        _wmkdir(cacheDir.c_str());
        writeFile(mFile, model);
        writeFile(kFile, sounds);
        writeFile(nFile, std::vector<uint8_t>(stamp.begin(), stamp.end()));
    }
    return true;
}

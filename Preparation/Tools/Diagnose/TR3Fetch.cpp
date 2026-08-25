//----------------------------------------------------------------------------
// TR3Fetch - fetches a moveable from a TR3 level and recomputes it into the
// TR5 layout. Step 1 of the T-Rex port (#88): first prove the format
// bridge, then insert.
//
// Usage: TR3Fetch <tr3level.TR2> <objectId> <target.bin>
//
// What is the same and simply travels along:
//   - Mesh header: TR3 Centre(6) + CollRadius(4) = 10 bytes, TR5 reads
//     exactly 5 shorts before the vertex word. Same place, same meaning.
//   - Vertices and normals: 3 shorts each, unchanged.
//   - Bone tree: 4 int32 per bone, identical in TR1..TR5.
//   - Frames: TR2/TR3 and TR4/TR5 pack rotations alike (2 bits axis
//     choice, then one or two words). Recomputed on the T-Rex: frameSize
//     58 words = 6 bbox + 3 offset + 49 for 25 bones, i.e. 24 two-word
//     and one one-word rotation. Fits.
//
// What MUST be recomputed:
//   - Faces: TR3 carries 4 vertex words + 1 texture word (quad 8 bytes,
//     tri 6 bytes). TR5 appends an EFFECT WORD each (12 resp. 10 bytes).
//   - Colored faces: TR3 knows them, TR5 does not. They become textured
//     ones with texture 0 - visibly wrong but geometrically intact; the
//     T-Rex barely uses them anyway.
//   - Animations: TR3 32 bytes, TR5 40 bytes. The additional 8 bytes are
//     the lateral speeds (TR4+), they are set to 0.
//
// The output is a plain container the inserter reads:
//   header: "T35H", version, objectId, numMeshes, numAnims, numFrameWords,
//           numBoneWords, numStateChanges, numDispatches, numCmdWords
//   then: mesh blocks (uint32 length + bytes each), bone bytes, frame
//         bytes, anim bytes (40 per anim), state changes, dispatches,
//         commands
//----------------------------------------------------------------------------
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

        bool load(const char* p)
        {
            FILE* f = std::fopen(p, "rb");
            if (!f) return false;
            std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            b.resize(static_cast<size_t>(n));
            const size_t g = std::fread(b.data(), 1, b.size(), f);
            std::fclose(f);
            return g == b.size();
        }
        bool room(size_t n) { if (at + n > b.size()) { broken = true; return false; } return true; }
        uint16_t u16() { if (!room(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
        uint32_t u32() { if (!room(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
        void skip(size_t n) { if (room(n)) at += n; }
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

int main(int argc, char** argv)
{
    if (argc != 4) { std::printf("usage: TR3Fetch <tr3level.TR2> <objectId> <target.bin>\n"); return 2; }
    const int wanted = std::atoi(argv[2]);

    Reader r;
    if (!r.load(argv[1])) { std::printf("file not readable: %s\n", argv[1]); return 2; }

    const uint32_t version = r.u32();
    if ((version >> 24) != 0xFF) { std::printf("not a TR3 level (version 0x%08X)\n", version); return 2; }

    r.skip(768); r.skip(1024);
    const uint32_t nTex = r.u32();
    r.skip(size_t(nTex) * 65536);                // textile8 (palette version)
    const size_t tex16At = r.at;                 // textile16: ARGB1555, 131072 per tile
    r.skip(size_t(nTex) * 131072);
    r.u32();

    const size_t roomAt = r.at;
    const uint16_t nRooms = r.u16();

    // Try out the room-header variants instead of guessing (see TR3Peek).
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
    if (!afterRooms) { std::printf("room walk failed\n"); return 1; }
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
    if (r.broken) { std::printf("the file ends before the moveable table\n"); return 1; }

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
    if (!z) { std::printf("object %d not present\n", wanted); return 1; }

    uint32_t animEnd = nAnims;
    for (const Moveable& m : mv)
        if (m.animIndex > z->animIndex && m.animIndex < animEnd) animEnd = m.animIndex;
    const uint32_t animCount = animEnd - z->animIndex;

    std::printf("object %d: %u meshes, %u bones, anims %u..%u (%u)\n",
                wanted, z->numMeshes, z->numMeshes ? z->numMeshes - 1 : 0,
                z->animIndex, animEnd - 1, animCount);

    // --- Continue to the object textures (for the texture work, #90) -------
    // The disputed field sizes at the file end are again tried out instead
    // of guessed: right is the combination at which the last table ends
    // exactly at the file end. If none is found, objTexAt stays 0 and the
    // report is simply shorter - the model travels anyway.
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
            r.skip(size_t(r.u32()) * 32);                // static meshes
            r.skip(size_t(r.u32()) * 16);                // sprite textures
            r.skip(size_t(r.u32()) * 8);                 // sprite sequences
            r.skip(size_t(r.u32()) * 16);                // cameras
            r.skip(size_t(r.u32()) * 16);                // sound sources
            const uint32_t nBoxes = r.u32();
            r.skip(size_t(nBoxes) * boxSz);
            r.skip(size_t(r.u32()) * 2);                 // overlaps
            r.skip(size_t(nBoxes) * zoneN * 2);          // zones
            r.skip(size_t(r.u32()) * 2);                 // animated textures
            const uint32_t nOT = r.u32();
            const size_t otAt = r.at;
            r.skip(size_t(nOT) * 20);
            r.skip(size_t(r.u32()) * itemSz);            // items
            r.skip(8192);                                // light map
            r.skip(size_t(r.u16()) * 16);                // cinematic frames
            r.skip(size_t(r.u16()));                     // demo data
            r.skip(size_t(mapN) * 2);                    // sound map
            r.skip(size_t(r.u32()) * 8);                 // sound details
            r.skip(size_t(r.u32()) * 4);                 // sample indices
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

        for (int k = 0; k < 10; ++k) o.push_back(r.b[p + k]);       // Centre + CollRadius
        p += 10;
        const int16_t nVerts = rs16(r.b, p); p += 2;
        if (nVerts > 255) { std::printf("  mesh %u has %d vertices - TR5 only reads 255\n", i, nVerts); return 1; }
        put16(o, uint16_t(nVerts));
        for (int v = 0; v < nVerts * 3; ++v) { put16(o, ru16(r.b, p)); p += 2; }
        const int16_t nNorms = rs16(r.b, p); p += 2;
        put16(o, uint16_t(nNorms));
        const int words = (nNorms >= 0) ? nVerts * 3 : nVerts;
        for (int v = 0; v < words; ++v) { put16(o, ru16(r.b, p)); p += 2; }
        if (nVerts > maxVerts) maxVerts = nVerts;

        // Textured quads: 4 vertex words + texture, plus a new effect word.
        const int16_t nQ = rs16(r.b, p); p += 2;
        const int16_t nQfarbAt = 0; (void)nQfarbAt;
        std::vector<uint8_t> quads, tris;
        int qz = 0, tz = 0;
        for (int q = 0; q < nQ; ++q)
        {
            for (int w = 0; w < 5; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            put16(quads, 0);                                        // effect word
            ++qz;
        }
        const int16_t nT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nT; ++t)
        {
            for (int w = 0; w < 4; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            put16(tris, 0);
            ++tz;
        }
        // Colored faces: TR5 does not know them. They are taken over as
        // textured so the geometry stays closed - but NOT with texture 0.
        // That is something arbitrary in the source level; with the T-Rex
        // it was a piece of Lara's leg, which then stuck under his jaw
        // (seen in game). Instead the first texture the model itself uses
        // - that one is guaranteed to belong to it.
        const int16_t nCQ = rs16(r.b, p); p += 2;
        for (int q = 0; q < nCQ; ++q)
        {
            for (int w = 0; w < 4; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            p += 2;                                                 // discard the color index
            coloredSpots.push_back({ &quads, quads.size() });
            put16(quads, 0); put16(quads, 0);
            ++qz; ++totalColored;
        }
        const int16_t nCT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nCT; ++t)
        {
            for (int w = 0; w < 3; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            p += 2;
            coloredSpots.push_back({ &tris, tris.size() });
            put16(tris, 0); put16(tris, 0);
            ++tz; ++totalColored;
        }

        // Remember this mesh's first real texture and set the colored spots
        // to it BEFORE the buffers move into the mesh.
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

        put16(o, uint16_t(qz)); o.insert(o.end(), quads.begin(), quads.end());
        put16(o, uint16_t(tz)); o.insert(o.end(), tris.begin(), tris.end());
        totalQuads += qz; totalTris += tz;
        meshes.push_back(o);
    }

    // --- Which object textures does this model need? ------------------------
    //
    // The faces still carry the TR3 indices; only the inserter reroutes
    // them. For the texture work (#90) what counts is how many distinct
    // entries and above all how many distinct TILES stand behind them -
    // every tile becomes a 256x256 page in the target.
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
    // rebase the pointers to its start - otherwise they point anywhere in
    // the target. The end is the next object's start, just as with the
    // anims.
    uint32_t frameEnd = nFrame * 2;
    for (const Moveable& m : mv)
        if (m.frameOffset > z->frameOffset && m.frameOffset < frameEnd) frameEnd = m.frameOffset;
    const uint32_t frameBytes = frameEnd - z->frameOffset;

    // --- Animations 32 -> 40 bytes, rebase the frame pointers to the slice --
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

        for (int k = 16; k < 20; ++k) anims.push_back(r.b[a + k]);  // frameStart, frameEnd
        const uint16_t nextAnim = ru16(r.b, a + 20);
        if (nextAnim < animMin) animMin = nextAnim;
        if (nextAnim > animMax) animMax = nextAnim;
        put16(anims, uint16_t(nextAnim - z->animIndex));            // rebase to the slice
        for (int k = 22; k < 32; ++k) anims.push_back(r.b[a + k]);  // nextFrame, SC, cmd
    }

    // --- Output -------------------------------------------------------------
    std::vector<uint8_t> out;
    out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('H');
    put32(out, 2);                 // version 2: with the texture part at the end
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
    // --- Frames: rewrite one-axis rotations to TR5's width ------------------
    //
    // THE actual bridge between the games. Both pack a rotation into one or
    // two words, the top two bits choose the form. The two-word case is
    // IDENTICAL (phd_RotYXZpack computes (angles >> 10) & 0x3FF, then << 6
    // in both games). The one-axis case is not:
    //
    //   TR3 (draw.cpp:183):  (word & 1023)  << 6     10 bits
    //   TR5 (draw.cpp:267):  (word & 0xFFF) << 4     12 bits
    //
    // A TR3 word taken over unchanged is thus read by TR5 with a quarter of
    // the angle - and because TR3 uses the overflow into the negative,
    // large values flip entirely. Affected are exactly the ONE-AXIS JOINTS:
    // jaw, ankles, toes. Seen in game as twisted feet and an inside-out
    // mouth while the torso was right - it rotates around all three axes
    // and thereby runs through the identical two-word case.
    //
    // The conversion is lossless: TR5 shall yield v << 6 but computes
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
                        const uint16_t fresh = uint16_t((w & 0xC000) | ((v << 2) & 0xFFF));
                        frames[p2]     = uint8_t(fresh & 0xFF);
                        frames[p2 + 1] = uint8_t(fresh >> 8);
                        ++converted;
                        p2 += 2;
                    }
                    else p2 += 4;
                }
            }
        }
        std::printf("  rotations: %d one-axis words brought to TR5 width (%zu frames)\n",
                    converted, done.size());
        if (outOfSlice) std::printf("  ATTENTION: %d frames lay outside the slice\n", outOfSlice);

        out.insert(out.end(), frames.begin(), frames.end());
    }
    out.insert(out.end(), anims.begin(), anims.end());
    out.insert(out.end(), r.b.begin() + scAt,  r.b.begin() + scAt  + size_t(nSC) * 6);

    // Dispatch table (AnimDispatch, 8 bytes: low, high, nextAnim,
    // nextFrame). Its nextAnim is a GLOBAL anim index of the source level -
    // exactly like the anims themselves it must be rebased to the slice, or
    // it points anywhere in the target. That was the crash on the first
    // attempt: the entries traveled unrebased, and as soon as the control
    // code took a state change, the engine jumped into a foreign
    // animation. Entries outside the slice belong to other objects; they
    // are never read but get 0 so no wild value stands there.
    {
        std::vector<uint8_t> ad(r.b.begin() + adAt, r.b.begin() + adAt + size_t(nAD) * 8);
        int inside = 0, outsideAd = 0;
        for (size_t o = 0; o + 8 <= ad.size(); o += 8)
        {
            const uint16_t na = uint16_t(ad[o + 4] | (ad[o + 5] << 8));
            uint16_t fresh = 0;
            if (na >= z->animIndex && na < animEnd) { fresh = uint16_t(na - z->animIndex); ++inside; }
            else ++outsideAd;
            ad[o + 4] = uint8_t(fresh & 0xFF);
            ad[o + 5] = uint8_t(fresh >> 8);
        }
        std::printf("  dispatch table: %d entries rebased to the slice, %d foreign ones zeroed\n", inside, outsideAd);
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
    // The UVs stay PIXELS here. Only the inserter shifts them left by 8
    // bits - TR5 carries them as 8.8 fixed point (file.cpp:1096).
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
            { std::printf("  tile %d lies outside the texture pages\n", k); return 1; }
            const size_t at = tex16At + size_t(k) * 131072;
            out.insert(out.end(), r.b.begin() + at, r.b.begin() + at + 131072);
        }
        std::printf("  texture part: %d entries, %zu tiles of 131072 bytes each\n",
                    written, tiles.size());
    }

    FILE* f = std::fopen(argv[3], "wb");
    if (!f) { std::printf("target not writable: %s\n", argv[3]); return 2; }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);

    // --- Cross-check: does the TR5 walker run through cleanly? --------------
    //
    // Rebuilt after the engine's ProcessMeshData (drawroom.cpp), identical
    // to meshSize() in Tr5Transfer.cpp. If the computed length matches the
    // written one to the byte, the layout fits - that is the real proof of
    // the format bridge, not the mere byte output.
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
            std::printf("  MESH %zu DOES NOT FIT: the walker ends at %zu, written %zu\n", i, p, m.size());
            ++bad;
        }
    }
    std::printf("  cross-check TR5 walker: %zu meshes checked, %d deviations\n",
                meshes.size(), bad);

    size_t meshBytes = 0; for (const std::vector<uint8_t>& m : meshes) meshBytes += m.size();
    std::printf("  meshes rewritten: %zu bytes, %d quads, %d tris (%d of them from colored faces)\n",
                meshBytes, totalQuads, totalTris, totalColored);
    std::printf("  largest mesh: %d vertices\n", maxVerts);
    std::printf("  anims: %u of 40 bytes each, frame slice %u bytes (%u..%u), bones %u words\n",
                animCount, frameBytes, z->frameOffset, frameEnd,
                z->numMeshes ? (z->numMeshes - 1) * 4 : 0);
    std::printf("  frame pointers outside the slice: %d   jump targets %u..%u (slice %u..%u)\n",
                outside, animMin, animMax, z->animIndex, animEnd - 1);
    if (outside) std::printf("  ATTENTION: frame pointers point out of the slice\n");
    if (animMin < z->animIndex || animMax >= animEnd)
        std::printf("  ATTENTION: an anim jumps out of its own slice\n");
    std::printf("  written: %s (%zu bytes)\n", argv[3], out.size());
    return 0;
}

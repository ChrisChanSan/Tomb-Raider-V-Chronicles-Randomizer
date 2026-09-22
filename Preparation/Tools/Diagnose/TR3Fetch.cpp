//----------------------------------------------------------------------------
// TR3Fetch - fetches a moveable from a TR3 level and recomputes it into the
// TR5 layout: first prove the format bridge, then insert.
//
// Usage: TR3Fetch <tr3level.TR2> <objectId> <target.bin>
//          [-shiny]            give the meshes the TR4/TR5 environment map
//          [-addcolor]         keep the source colours and add, do not replace
//          [-recolor r,g,b=r,g,b]   swap one colour for another
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
    if (argc <  4) { std::printf("usage: TR3Fetch <tr3level.TR2> <objectId> <target.bin> [-recolor r,g,b=r,g,b]\n"); return 2; }
    const int wanted = std::atoi(argv[2]);

    // -shiny <strength>: the TR4/TR5 environment map, on every TEXTURED face
    // of the model. The effects word behind each face carries it - bit 1
    // switches it on, bits 2..6 are the strength, and the drawer multiplies
    // by (word & 0x7C) << 1 before laying the environment texture over the
    // face a second time (specific/output.cpp).
    //
    // Textured is exactly the right set: a TR1-3 model wears its outfit as
    // textures and its SKIN as flat colours, so the suit shines and the skin
    // stays matt without naming a single face.
    //
    // Measured on rich1, where TR5's own catsuit lives: word 0x0042, that is
    // strength 128. Same value here by default.
    int shinyWord = 0;
    for (int a = 4; a < argc; ++a)
        if (std::strcmp(argv[a], "-shiny") == 0)
        {
            int strength = 128;
            if (a + 1 < argc && argv[a + 1][0] != '-') strength = std::atoi(argv[++a]);
            shinyWord = 2 | ((strength >> 1) & 0x7C);
            std::printf("  shiny: effects word 0x%04X (strength %d) on every textured face\n",
                        shinyWord, (shinyWord & 0x7C) << 1);
        }

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

    // --- Continue to the object textures ------------------------------------
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

    // Colored faces, collected across ALL meshes and resolved at the end.
    //
    // TR5 has no colored faces, so they have to become textured ones. Giving
    // every one in the model the SAME substitute - the first texture the
    // model happened to use - is visibly wrong: on Lara's TR3 body 61 of 442
    // triangles would carry skin or trouser colour where something else
    // belongs.
    //
    // They can be placed far better than that. The face names a palette
    // entry, and the palette is right there in the file; the object textures
    // are pieces of tiles whose average colour can be measured. So each
    // colored face can take the texture that comes CLOSEST to the colour it
    // asked for. The model's own textures only - a piece of this model is
    // always more plausible than something borrowed.
    struct ColorSpot { size_t meshNo; size_t at; uint16_t colorIdx; };
    std::vector<ColorSpot> coloredSpots;
    std::vector<int> meshFirstTex;            // per mesh, as the fallback
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
            put16(quads, uint16_t(shinyWord));                      // effect word
            ++qz;
        }
        const int16_t nT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nT; ++t)
        {
            for (int w = 0; w < 4; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            put16(tris, uint16_t(shinyWord));
            ++tz;
        }
        // Colored faces: TR5 does not know them. They are taken over as
        // textured so the geometry stays closed - but NOT with texture 0.
        // That is something arbitrary in the source level; with the T-Rex
        // it was a piece of Lara's leg, which would then stick under his
        // jaw. Instead the first texture the model itself uses - that one
        // is guaranteed to belong to it.
        const int16_t nCQ = rs16(r.b, p); p += 2;
        std::vector<std::pair<size_t, uint16_t>> quadSpots, triSpots;

        for (int q = 0; q < nCQ; ++q)
        {
            for (int w = 0; w < 4; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            const uint16_t colIdx = ru16(r.b, p); p += 2;
            quadSpots.push_back({ quads.size(), colIdx });
            put16(quads, 0); put16(quads, 0);
            ++qz; ++totalColored;
        }
        const int16_t nCT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nCT; ++t)
        {
            for (int w = 0; w < 3; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            const uint16_t colIdx = ru16(r.b, p); p += 2;
            triSpots.push_back({ tris.size(), colIdx });
            put16(tris, 0); put16(tris, 0);
            ++tz; ++totalColored;
        }

        // This mesh's own first real texture - the fallback if no colour can
        // be matched, and better than the model's first: a piece of the same
        // body part is closer than a piece of the boot.
        int mineFirst = -1;
        if (nQ > 0)      mineFirst = ru16(quads, 8);
        else if (nT > 0) mineFirst = ru16(tris, 6);
        if (firstTex < 0) firstTex = mineFirst;
        meshFirstTex.push_back(mineFirst);

        put16(o, uint16_t(qz));
        const size_t qBase = o.size();
        o.insert(o.end(), quads.begin(), quads.end());
        put16(o, uint16_t(tz));
        const size_t tBase = o.size();
        o.insert(o.end(), tris.begin(), tris.end());

        for (const auto& sp : quadSpots) coloredSpots.push_back({ meshes.size(), qBase + sp.first, sp.second });
        for (const auto& sp : triSpots)  coloredSpots.push_back({ meshes.size(), tBase + sp.first, sp.second });
        totalQuads += qz; totalTris += tz;
        meshes.push_back(o);
    }

    // --- Which object textures does this model need? ------------------------
    //
    // The faces still carry the TR3 indices; only the inserter reroutes
    // them. What counts is how many distinct entries and above all how many
    // distinct TILES stand behind them -
    // every tile becomes a 256x256 page in the target.
    // -recolor r,g,b=r,g,b (repeatable): a flat colour of TR3's is written
    // to the page as a different one. Wanted for skin, where TR3 has one
    // flat value and TR5's own Lara has a painted, shaded texture: the two
    // can be brought to the same base tone without touching the model.
    // Source and target are given in full so nothing has to be guessed
    // about which of the flat colours is meant.
    struct Recolor { int sr, sg, sb, dr, dg, db; };
    std::vector<Recolor> recolors;

    // -addcolor r,g,b (repeatable): an extra solid block on the flat colour
    // page that NO face points at. It is there so the inserter has that
    // exact tone available later - it paints a seam by looking for an
    // evenly coloured entry, and can only find what the level holds. The
    // page has 256 slots and a model uses a handful.
    struct Rgb { int r, g, b; };
    std::vector<Rgb> extraColors;
    for (int a = 4; a < argc; ++a)
        if (std::strcmp(argv[a], "-addcolor") == 0 && a + 1 < argc)
        {
            Rgb c;
            if (std::sscanf(argv[++a], "%d,%d,%d", &c.r, &c.g, &c.b) == 3)
                extraColors.push_back(c);
            else
                std::printf("  -addcolor not readable: %s (expected r,g,b)\n", argv[a]);
        }
        else if (std::strcmp(argv[a], "-recolor") == 0 && a + 1 < argc)
        {
            Recolor rc;
            if (std::sscanf(argv[++a], "%d,%d,%d=%d,%d,%d",
                            &rc.sr, &rc.sg, &rc.sb, &rc.dr, &rc.dg, &rc.db) == 6)
                recolors.push_back(rc);
            else
                std::printf("  -recolor not readable: %s (expected r,g,b=r,g,b)\n", argv[a]);
        }

    std::vector<int> texIdx;      // used TR3 object textures, in discovery order
    std::vector<uint8_t> texIsTri;// 1 = the entry sits on triangles, 0 = on quads
    std::vector<int> tiles;       // the tiles behind them, in discovery order
    std::vector<int> mixedUse;    // entries that serve both - reported below
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
        auto note = [&](int t, uint8_t tri) {
            for (size_t x = 0; x < texIdx.size(); ++x) if (texIdx[x] == t)
            {
                if (texIsTri[x] != tri) mixedUse.push_back(t);
                return;
            }
            texIdx.push_back(t);
            texIsTri.push_back(tri);
            if (t >= 0 && nObjTex && uint32_t(t) < nObjTex)
            {
                const int k = ru16(r.b, objTexAt + size_t(t) * 20 + 2) & 0x7FFF;
                for (int x : tiles) if (x == k) return;
                tiles.push_back(k);
            }
        };
        const int16_t nq = rs16(m, p); p += 2;
        for (int i = 0; i < nq; ++i, p += 12) note(ru16(m, p + 8) & 0x7FFF, 0);
        const int16_t nt = rs16(m, p); p += 2;
        for (int i = 0; i < nt; ++i, p += 10) note(ru16(m, p + 6) & 0x7FFF, 1);
    }

    // --- Split the entries that serve both face kinds -----------------------
    //
    // TR5 keeps one corner order per texture entry, and the triangle table
    // and the quad table share no case. TR3 has no such field at all and
    // shares entries freely, so an entry used by both kinds cannot be
    // described in one record.
    //
    // Those get a second one: the quads keep the original, the triangles are
    // sent to an alias carrying the triangle order. Same trick as the flat
    // colours below and for the same reason - the inserter maps a face by
    // the number written in it, so a number we invent costs it nothing.
    // 20000 is above every real TR3 index and below the colours at 30000.
    const int TRI_ALIAS_BASE = 20000;
    std::vector<int> aliasOf;                 // per alias: the entry it copies
    size_t firstAlias = texIdx.size();
    if (!mixedUse.empty())
    {
        std::vector<int> shared;
        for (int t : mixedUse)
        {
            if (t < 0 || uint32_t(t) >= nObjTex) continue;
            bool had = false;
            for (int x : shared) if (x == t) { had = true; break; }
            if (!had) shared.push_back(t);
        }

        int moved = 0;
        for (size_t mi = 0; mi < meshes.size(); ++mi)
        {
            std::vector<uint8_t>& m = meshes[mi];
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
            const int16_t nq = rs16(m, p); p += 2;
            p += size_t(nq) * 12;                        // the quads keep theirs
            const int16_t nt = rs16(m, p); p += 2;
            for (int i = 0; i < nt; ++i, p += 10)
            {
                const uint16_t w = ru16(m, p + 6);
                const int t = w & 0x7FFF;
                for (size_t k = 0; k < shared.size(); ++k) if (shared[k] == t)
                {
                    const uint16_t nw = uint16_t((w & 0x8000) | uint16_t(TRI_ALIAS_BASE + k));
                    m[p + 6] = uint8_t(nw & 0xFF);
                    m[p + 7] = uint8_t(nw >> 8);
                    ++moved;
                    break;
                }
            }
        }

        firstAlias = texIdx.size();
        for (size_t k = 0; k < shared.size(); ++k)
        {
            texIdx.push_back(TRI_ALIAS_BASE + int(k));
            texIsTri.push_back(1);
            aliasOf.push_back(shared[k]);
        }
        for (int t : shared)
            for (size_t x = 0; x < firstAlias; ++x) if (texIdx[x] == t) texIsTri[x] = 0;

        std::printf("  shared entries: %zu split into a quad and a triangle copy, %d triangle faces moved\n",
                    shared.size(), moved);
    }
    // --- Place the colored faces on the closest texture ---------------------
    //
    // The palette sits right behind the file header (256 entries, 4 bytes
    // each), the 16 bit tiles at tex16At in ARGB1555. So both sides of the
    // comparison are measurable: what colour the face asked for, and what
    // colour each of the model's textures actually is.
    //
    // The palette's scale is not assumed but read off - TR1's is 6 bit, and
    // whether TR3 followed is not worth guessing when the maximum says it.
    // Flat colours for the colored faces: which palette entries are used, in
    // the order they appear. They become blocks on one extra tile, and the
    // faces point at them via synthetic texture entries numbered from here.
    const int FLAT_BASE = 30000;
    std::vector<int> flatColors;

    struct Avg { int r, g, b; bool ok; };
    std::vector<Avg> texAvgOut;

    if (!coloredSpots.empty() && nObjTex && !texIdx.empty())
    {
        int palMax = 0;
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 3; ++c)
                if (r.b[772 + size_t(i) * 4 + size_t(c)] > palMax) palMax = r.b[772 + size_t(i) * 4 + size_t(c)];
        const int palShift = (palMax > 63) ? 3 : 1;      // -> 0..31, like ARGB1555

        // Average colour of one object texture: its own pixel rectangle out of
        // the 16 bit tile, transparent pixels left out (bit 15 = opaque).
        std::vector<Avg> texAvg(texIdx.size(), { 0, 0, 0, false });

        for (size_t k = 0; k < texIdx.size(); ++k)
        {
            const int t = texIdx[k];
            if (t < 0 || uint32_t(t) >= nObjTex) continue;
            const size_t e = objTexAt + size_t(t) * 20;
            const int tile = ru16(r.b, e + 2) & 0x7FFF;
            int x0 = 255, y0 = 255, x1 = 0, y1 = 0;

            for (int v = 0; v < 4; ++v)
            {
                const int px = r.b[e + 4 + size_t(v) * 4 + 1];
                const int py = r.b[e + 4 + size_t(v) * 4 + 3];
                if (px < x0) x0 = px;
                if (px > x1) x1 = px;
                if (py < y0) y0 = py;
                if (py > y1) y1 = py;
            }

            const size_t tileAt = tex16At + size_t(tile) * 131072;
            if (tileAt + 131072 > r.b.size()) continue;

            long sr = 0, sg = 0, sb = 0, n = 0;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    const uint16_t px16 = ru16(r.b, tileAt + (size_t(y) * 256 + size_t(x)) * 2);
                    if (!(px16 & 0x8000)) continue;              // transparent
                    sr += (px16 >> 10) & 0x1F;
                    sg += (px16 >> 5) & 0x1F;
                    sb += px16 & 0x1F;
                    ++n;
                }

            if (n) texAvg[k] = { int(sr / n), int(sg / n), int(sb / n), true };
        }

        // Which colours does this model actually ask for? Printed once per
        // distinct entry - it says at a glance whether a colour exists among
        // the model's textures at all.
        {
            std::vector<int> seen;
            for (const ColorSpot& fs : coloredSpots)
            {
                const int ci = (fs.colorIdx >> 8) & 0xFF;
                bool had = false;
                for (int x : seen) if (x == ci) { had = true; break; }
                if (had) continue;
                seen.push_back(ci);
                const size_t e = 772 + size_t(ci) * 4;
                int cnt = 0;
                for (const ColorSpot& g : coloredSpots) if (((g.colorIdx >> 8) & 0xFF) == ci) ++cnt;
                std::printf("    colour %3d: rgb %3d,%3d,%3d  on %d faces\n",
                            ci, r.b[e], r.b[e + 1], r.b[e + 2], cnt);
            }
        }

        // Each colored face gets its OWN flat colour, on a page built for
        // exactly that - not the nearest existing texture.
        //
        // The nearest one only ever matches the hue; the face still inherits
        // that texture's IMAGE. On a big surface that passes, on Lara's
        // forearms - which are colored faces from end to end - it does not,
        // and the two of them came out differently because they landed on two
        // different skin textures. TR3 means a flat colour here, so a flat
        // colour is what it gets.
        //
        // One extra tile carries the colours as a 16 by 16 grid of blocks,
        // and one synthetic texture entry points at each. Their orig numbers
        // start at 30000, far above any real TR3 index - the inserter maps
        // entries by that number and needs to know nothing about this.
        for (const ColorSpot& fs : coloredSpots)
        {
            const int ci = (fs.colorIdx >> 8) & 0xFF;
            int slot = -1;
            for (size_t k = 0; k < flatColors.size(); ++k) if (flatColors[k] == ci) { slot = int(k); break; }
            if (slot < 0)
            {
                if (flatColors.size() >= 256) continue;      // one tile holds 256
                slot = int(flatColors.size());
                flatColors.push_back(ci);
            }
            const uint16_t v = uint16_t(FLAT_BASE + slot);
            meshes[fs.meshNo][fs.at]     = uint8_t(v & 0xFF);
            meshes[fs.meshNo][fs.at + 1] = uint8_t(v >> 8);
        }

        std::printf("  colored faces: %zu flat colours on their own page\n", flatColors.size());
        texAvgOut = texAvg;
    }

    // Average colour per MESH - which body part carries which colour.
    //
    // Answers "should this forearm be dark or bare skin" without a game run:
    // every face's texture is measured anyway for the colored substitution,
    // so averaging them per mesh costs nothing and says outright what the
    // model itself intends. Mesh order is Lara's: 0 hips, 1-3 left leg,
    // 4-6 right leg, 7 torso, 8-10 right arm, 11-13 left arm, 14 head.
    if (nObjTex && !texAvgOut.empty())
    {
        static const char* part[15] = { "hips", "L thigh", "L shin", "L foot",
                                        "R thigh", "R shin", "R foot", "torso",
                                        "R inarm", "R outarm", "R hand",
                                        "L inarm", "L outarm", "L hand", "head" };
        for (size_t mi = 0; mi < meshes.size(); ++mi)
        {
            const std::vector<uint8_t>& m = meshes[mi];
            size_t q = 10;
            const int16_t vw = rs16(m, q); q += 2;
            const int nv = vw & 0xFF;
            if (nv == 0) q += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
            else
            {
                q += size_t(nv) * 6;
                const int16_t nn = rs16(m, q); q += 2;
                q += size_t(nv) * (nn >= 0 ? 6 : 2);
            }
            long sr = 0, sg = 0, sb = 0, n = 0;
            auto take = [&](int t) {
                for (size_t k = 0; k < texIdx.size(); ++k)
                    if (texIdx[k] == t && texAvgOut[k].ok)
                    { sr += texAvgOut[k].r; sg += texAvgOut[k].g; sb += texAvgOut[k].b; ++n; return; }
            };
            const int16_t nq = rs16(m, q); q += 2;
            for (int i = 0; i < nq; ++i, q += 12) take(ru16(m, q + 8) & 0x7FFF);
            const int16_t nt = rs16(m, q); q += 2;
            for (int i = 0; i < nt; ++i, q += 10) take(ru16(m, q + 6) & 0x7FFF);
            if (n)
                std::printf("    mesh %2zu %-9s rgb %2ld,%2ld,%2ld of 31  (%ld faces)\n",
                            mi, mi < 15 ? part[mi] : "?", sr / n, sg / n, sb / n, n);
        }
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
    put32(out, 3);                 // version 3: texture part, with the corner order
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
    //   TR3 (draw.cpp:183):                  (word & 1023)  << 6   10 bits
    //   TR5 (draw.cpp, gar_RotYXZsuperpack): (word & 0xFFF) << 4   12 bits
    //
    // A TR3 word taken over unchanged is thus read by TR5 with a quarter of
    // the angle - and because TR3 uses the overflow into the negative,
    // large values flip entirely. Affected are exactly the ONE-AXIS JOINTS:
    // jaw, ankles, toes - producing twisted feet and an inside-out mouth,
    // while the torso is unaffected because it rotates around all three
    // axes and thereby runs through the identical two-word case.
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
    // it points anywhere in the target: left unrebased, as soon as the
    // control code takes a state change, the engine jumps into a foreign
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

    // --- Texture part (package version 3) ----------------------------------
    //
    // Appended so old packages stay readable: whoever reads only up to the
    // command block gets exactly the old format.
    //
    //   uint32 textureCount
    //   per texture: origIndex(2), drawtype(2), tileInPackage(2),
    //                then 8 bytes u1,v1,u2,v2,u3,v3,u4,v4 as PIXELS,
    //                then cornerOrder(2)  - version 3 and up
    //   uint32 tileCount
    //   per tile: 131072 bytes ARGB1555 (TR3's 16-bit version)
    //
    // The UVs stay PIXELS here. Only the inserter shifts them left by 8
    // bits - TR5 carries them as 8.8 fixed point (LoadTextureInfos,
    // specific/file.cpp).
    {
        // Why cornerOrder exists.
        //
        // TR5 pulls every UV half a texel towards the middle of its own
        // texture area while the level loads (AdjustUV, specific/file.cpp).
        // Which direction that is for each corner it does not work out - it
        // reads it out of the texinfo: bit 15 of tpage says triangle or quad,
        // the low three bits of flag say in which order the corners run.
        //
        // TR3 has no such field. It has something better: next to every UV
        // pixel stands a byte that says whether that corner sits at the low
        // or at the high edge of its area. That is exactly the question, so
        // nothing here has to be guessed.
        //
        // Leaving it at zero is not harmless. Zero means "corner 0 of a
        // quad", so every imported triangle had its third corner pushed
        // OUTWARD - half a texel into whatever texture lies next to it in
        // the page. That is the thin bright line along a seam.
        //
        // Two bits per corner: bit 0 = u moves plus, bit 1 = v moves plus.
        // AdjustUV's eight triangle cases are exactly the eight patterns a
        // right-angled triangle can have; of the four a quad can have it
        // knows two, and TR3 uses no others.
        static const uint8_t TRI_SIGNS[8][3] = {
            { 3, 2, 1 }, { 2, 0, 3 }, { 0, 1, 2 }, { 1, 3, 0 },
            { 2, 3, 0 }, { 3, 1, 2 }, { 1, 0, 3 }, { 0, 2, 1 } };
        static const uint8_t QUAD_SIGNS[2][4] = { { 3, 2, 0, 1 }, { 2, 3, 1, 0 } };

        int orderNoMatch = 0, orderDisagree = 0, orderTris = 0;
        auto cornerOrder = [&](size_t o, bool isTri) -> uint16_t
        {
            const int n = isTri ? 3 : 4;
            int px[4], py[4], cx[4], cy[4];
            for (int v = 0; v < 4; ++v)
            {
                cx[v] = r.b[o + 4 + v * 4 + 0];      // 1 = low edge, 255 = high edge
                px[v] = r.b[o + 4 + v * 4 + 1];
                cy[v] = r.b[o + 4 + v * 4 + 2];
                py[v] = r.b[o + 4 + v * 4 + 3];
            }
            int loU = px[0], hiU = px[0], loV = py[0], hiV = py[0];
            for (int v = 1; v < n; ++v)
            {
                if (px[v] < loU) loU = px[v];
                if (px[v] > hiU) hiU = px[v];
                if (py[v] < loV) loV = py[v];
                if (py[v] > hiV) hiV = py[v];
            }

            int want = 0;
            for (int v = 0; v < n; ++v)
            {
                const bool uPlus = (cx[v] < 128);
                const bool vPlus = (cy[v] < 128);
                // Cross-check where the corners themselves say it plainly. A
                // count above zero here would mean the byte does not mean what
                // this code assumes - so it is worth printing.
                if (loU != hiU && (px[v] == loU || px[v] == hiU) && uPlus != (px[v] == loU)) ++orderDisagree;
                if (loV != hiV && (py[v] == loV || py[v] == hiV) && vPlus != (py[v] == loV)) ++orderDisagree;
                want |= ((uPlus ? 1 : 0) | (vPlus ? 2 : 0)) << (v * 2);
            }

            int type = -1;
            if (isTri)
            {
                ++orderTris;
                for (int c = 0; c < 8 && type < 0; ++c)
                    if ((TRI_SIGNS[c][0] | (TRI_SIGNS[c][1] << 2) | (TRI_SIGNS[c][2] << 4)) == want) type = c;
            }
            else
                for (int c = 0; c < 2 && type < 0; ++c)
                    if ((QUAD_SIGNS[c][0] | (QUAD_SIGNS[c][1] << 2)
                       | (QUAD_SIGNS[c][2] << 4) | (QUAD_SIGNS[c][3] << 6)) == want) type = c;

            if (type < 0) { ++orderNoMatch; type = 0; }
            return uint16_t((isTri ? 0x8000 : 0) | type);
        };

        put32(out, uint32_t(texIdx.size() + flatColors.size() + extraColors.size()));
        int written = 0;
        for (size_t ti = 0; ti < texIdx.size(); ++ti)
        {
            const int t = texIdx[ti];
            // An alias keeps its own number but copies everything else from
            // the entry it stands in for - only the corner order differs.
            const int srcTex = (ti >= firstAlias) ? aliasOf[ti - firstAlias] : t;
            if (srcTex < 0 || uint32_t(srcTex) >= nObjTex) { put16(out, uint16_t(t)); put16(out, 0); put16(out, 0);
                                                   for (int k = 0; k < 8; ++k) out.push_back(0);
                                                   put16(out, 0); continue; }
            const size_t o = objTexAt + size_t(srcTex) * 20;
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
            put16(out, cornerOrder(o, texIsTri[ti] != 0));
            ++written;
        }

        // The flat colours as synthetic entries: same layout, orig numbers
        // from FLAT_BASE, and all of them on the extra tile appended below.
        // Each block is inset by three pixels so a neighbour cannot bleed in.
        for (size_t k = 0; k < flatColors.size(); ++k)
        {
            const int col = int(k) % 16, row = int(k) / 16;
            const uint8_t x0 = uint8_t(col * 16 + 3), x1 = uint8_t(col * 16 + 12);
            const uint8_t y0 = uint8_t(row * 16 + 3), y1 = uint8_t(row * 16 + 12);
            put16(out, uint16_t(FLAT_BASE + k));
            put16(out, 0);                                   // opaque
            put16(out, uint16_t(tiles.size()));               // the extra tile
            out.push_back(x0); out.push_back(y0);
            out.push_back(x1); out.push_back(y0);
            out.push_back(x1); out.push_back(y1);
            out.push_back(x0); out.push_back(y1);
            put16(out, 0);   // quad, top left corner first - and inside a
                             // solid block half a texel reaches nothing
            ++written;
        }

        // The extra colours sit behind them in the same grid.
        for (size_t k = 0; k < extraColors.size(); ++k)
        {
            const size_t n = flatColors.size() + k;
            const int col = int(n) % 16, row = int(n) / 16;
            const uint8_t x0 = uint8_t(col * 16 + 3), x1 = uint8_t(col * 16 + 12);
            const uint8_t y0 = uint8_t(row * 16 + 3), y1 = uint8_t(row * 16 + 12);
            put16(out, uint16_t(FLAT_BASE + n));
            put16(out, 0);
            put16(out, uint16_t(tiles.size()));
            out.push_back(x0); out.push_back(y0);
            out.push_back(x1); out.push_back(y0);
            out.push_back(x1); out.push_back(y1);
            out.push_back(x0); out.push_back(y1);
            put16(out, 0);
            ++written;
        }

        const bool haveFlatPage = !flatColors.empty() || !extraColors.empty();
        // A recolour hits the imported TILES as well, not only the flat
        // colour page.
        //
        // TR3 does not always keep skin as a flat colour: London does, the
        // jungle outfit carries it as a texture - its bare forearm measures
        // 100 percent one single tone. The 5 bit step that matters (TR3's 224
        // red lands on level 28 and returns as 231, TR5 sits on 27 and
        // returns as 222) is in the pixels there, where a flat colour
        // correction can never reach it.
        //
        // Safe to do wholesale: these pages were copied in for this model and
        // only its own texinfos point into them. Whatever else of the donor
        // level sits on the same page is never drawn.
        std::vector<uint16_t> swapFrom, swapTo;
        for (size_t q = 0; q < recolors.size(); ++q)
        {
            const int sr = recolors[q].sr, sg = recolors[q].sg, sb = recolors[q].sb;
            const int dr = recolors[q].dr, dg = recolors[q].dg, db = recolors[q].db;
            swapFrom.push_back(uint16_t(((sr >> 3) << 10) | ((sg >> 3) << 5) | (sb >> 3)));
            swapTo.push_back(  uint16_t(((dr >> 3) << 10) | ((dg >> 3) << 5) | (db >> 3)));
        }

        put32(out, uint32_t(tiles.size() + (haveFlatPage ? 1 : 0)));
        for (int k : tiles)
        {
            if (k < 0 || uint32_t(k) >= nTex)
            { std::printf("  tile %d lies outside the texture pages\n", k); return 1; }
            const size_t at = tex16At + size_t(k) * 131072;
            const size_t was = out.size();
            out.insert(out.end(), r.b.begin() + at, r.b.begin() + at + 131072);
            for (size_t px = 0; px < 65536 && !swapFrom.empty(); ++px)
            {
                uint16_t w; std::memcpy(&w, out.data() + was + px * 2, 2);
                for (size_t q = 0; q < swapFrom.size(); ++q)
                    if ((w & 0x7FFF) == swapFrom[q])
                    { w = uint16_t((w & 0x8000) | swapTo[q]);
                      std::memcpy(out.data() + was + px * 2, &w, 2); break; }
            }
        }
        if (haveFlatPage)
        {
            // One 256x256 ARGB1555 page, 16 by 16 blocks of 16 pixels. The
            // palette scale is read off its own maximum, not assumed.
            int palMax2 = 0;
            for (int q = 0; q < 256; ++q)
                for (int c = 0; c < 3; ++c)
                    if (r.b[772 + size_t(q) * 4 + size_t(c)] > palMax2) palMax2 = r.b[772 + size_t(q) * 4 + size_t(c)];
            const int sh = (palMax2 > 63) ? 3 : 1;

            std::vector<uint16_t> page(256 * 256, 0x8000);    // opaque black
            for (size_t n = 0; n < flatColors.size(); ++n)
            {
                const size_t e = 772 + size_t(flatColors[n]) * 4;
                int cr = r.b[e], cg = r.b[e + 1], cb = r.b[e + 2];
                for (size_t q = 0; q < recolors.size(); ++q)
                    if (recolors[q].sr == cr && recolors[q].sg == cg && recolors[q].sb == cb)
                    {
                        std::printf("    recolored %d,%d,%d to %d,%d,%d\n",
                                    cr, cg, cb, recolors[q].dr, recolors[q].dg, recolors[q].db);
                        cr = recolors[q].dr; cg = recolors[q].dg; cb = recolors[q].db;
                        break;
                    }
                const uint16_t v = uint16_t(0x8000
                                 | ((cr >> sh) << 10)
                                 | ((cg >> sh) << 5)
                                 |  (cb >> sh));
                const int col = int(n) % 16, row = int(n) / 16;
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        page[size_t(row * 16 + y) * 256 + size_t(col * 16 + x)] = v;
            }
            for (size_t k = 0; k < extraColors.size(); ++k)
            {
                const uint16_t v = uint16_t(0x8000
                                 | ((extraColors[k].r >> 3) << 10)
                                 | ((extraColors[k].g >> 3) << 5)
                                 |  (extraColors[k].b >> 3));
                const size_t n = flatColors.size() + k;
                const int col = int(n) % 16, row = int(n) / 16;
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        page[size_t(row * 16 + y) * 256 + size_t(col * 16 + x)] = v;
            }
            for (uint16_t v : page) put16(out, v);
            std::printf("    flat colour page added: %zu colours, %zu extra\n",
                        flatColors.size(), extraColors.size());
        }

        std::printf("  texture part: %d entries, %zu tiles of 131072 bytes each\n",
                    written, tiles.size() + (haveFlatPage ? 1 : 0));
        std::printf("  corner order: %d of %d entries are triangles, %d without a pattern, %d corners where the edge byte and the corner disagree\n",
                    orderTris, int(texIdx.size()), orderNoMatch, orderDisagree);
        if (!mixedUse.empty())
            std::printf("  %zu texture entries sit on triangles AND on quads - one order cannot fit both\n", mixedUse.size());
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

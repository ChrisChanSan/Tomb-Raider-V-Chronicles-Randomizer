//----------------------------------------------------------------------------
// TR3Peek - reads a TR3 level (.TR2 file format, uncompressed) far enough
// for the moveable table to become visible. Feasibility check for porting a
// TR3 moveable: first measure what is inside, then decide.
//
// Usage: TR3Peek <level.TR2> [objectId]
//
// Without objectId: header data and all moveables.
// With objectId:    this moveable in detail, incl. the anim slice (up to
//                   the next moveable in the anim table) and frame range.
//
// The TR3 layout is length-prefixed throughout, so it can be walked
// without format knowledge of the room geometry - the blocks only have to
// be skipped in the right order.
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct Reader
{
    std::vector<uint8_t> b;
    size_t at = 0;
    bool   broken = false;

    bool load(const char* path)
    {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        b.resize(static_cast<size_t>(n));
        const size_t got = std::fread(b.data(), 1, b.size(), f);
        std::fclose(f);
        return got == b.size();
    }

    bool room(size_t n) { if (at + n > b.size()) { broken = true; return false; } return true; }
    uint8_t  u8()  { if (!room(1)) return 0; return b[at++]; }
    uint16_t u16() { if (!room(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
    uint32_t u32() { if (!room(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
    int32_t  i32() { return static_cast<int32_t>(u32()); }
    void skip(size_t n) { if (room(n)) at += n; }
};

struct Moveable
{
    uint32_t objectId;
    uint16_t numMeshes;
    uint16_t startingMesh;
    uint32_t meshTree;
    uint32_t frameOffset;
    uint16_t animIndex;
};

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::printf("usage: TR3Peek <level.TR2> [objectId]\n");
        return 2;
    }
    const int wanted = (argc == 3) ? std::atoi(argv[2]) : -1;

    Reader r;
    if (!r.load(argv[1])) { std::printf("file cannot be read: %s\n", argv[1]); return 2; }

    const uint32_t version = r.u32();
    std::printf("file %s (%.1f MB), version 0x%08X", argv[1], r.b.size() / 1048576.0, version);
    // TR2 = 0x0000002D, TR3 = 0xFF080038 / 0xFF180038 / 0xFF180034 (high byte 0xFF)
    if (version == 0x0000002D) std::printf("  -> TR2\n");
    else if ((version >> 24) == 0xFF) std::printf("  -> TR3\n");
    else std::printf("  -> unknown\n");

    r.skip(768);                         // palette 8 bit
    r.skip(1024);                        // palette 16 bit
    const uint32_t nTextiles = r.u32();
    r.skip(static_cast<size_t>(nTextiles) * 65536);    // textile8
    r.skip(static_cast<size_t>(nTextiles) * 131072);   // textile16
    std::printf("  texture pages %u\n", nTextiles);

    r.u32();                             // unused
    const size_t roomBlockAt = r.at;
    const uint16_t nRooms = r.u16();

    // Two field sizes in the room header are documented differently
    // depending on the source: the number of ambient bytes and the size of
    // a room light. Instead of guessing, every combination is tried and
    // the one taken that yields a SENSIBLE remainder - recognizable by the
    // blocks behind it having plausible sizes and the moveable table
    // adding up. Wrong assumptions almost always run out of the file
    // immediately.
    int goodAmbient = -1, goodLight = -1;
    size_t afterRooms = 0;
    for (int amb = 2; amb <= 6 && goodAmbient < 0; amb += 2)
    {
        for (int light = 18; light <= 26 && goodAmbient < 0; light += 2)
        {
            r.at = roomBlockAt + 2;
            r.broken = false;
            for (uint16_t i = 0; i < nRooms && !r.broken; ++i)
            {
                r.skip(16);
                const uint32_t dataWords = r.u32();
                r.skip(static_cast<size_t>(dataWords) * 2);
                const uint16_t nPortals = r.u16();
                r.skip(static_cast<size_t>(nPortals) * 32);
                const uint16_t nZ = r.u16();
                const uint16_t nX = r.u16();
                r.skip(static_cast<size_t>(nZ) * nX * 8);
                r.skip(static_cast<size_t>(amb));
                const uint16_t nLights = r.u16();
                r.skip(static_cast<size_t>(nLights) * light);
                const uint16_t nStatics = r.u16();
                r.skip(static_cast<size_t>(nStatics) * 20);
                r.skip(2 + 2);           // AlternateRoom, Flags
                r.skip(3);               // WaterScheme, ReverbInfo, Filler
            }
            if (r.broken) continue;

            // Trial read: do the following block sizes fit into the file?
            const size_t mark = r.at;
            bool ok = true;
            const uint32_t pFloor = r.u32();   r.skip(static_cast<size_t>(pFloor) * 2);
            const uint32_t pMesh  = r.u32();   r.skip(static_cast<size_t>(pMesh) * 2);
            const uint32_t pPtr   = r.u32();   r.skip(static_cast<size_t>(pPtr) * 4);
            const uint32_t pAnim  = r.u32();   r.skip(static_cast<size_t>(pAnim) * 32);
            if (r.broken || pFloor > 500000 || pMesh > 2000000 || pPtr > 20000 || pAnim > 20000) ok = false;
            if (ok) { goodAmbient = amb; goodLight = light; afterRooms = mark; }
            r.at = mark;
        }
    }
    if (goodAmbient < 0) { std::printf("ERROR: no room-header variant fits (rooms %u)\n", nRooms); return 1; }
    r.at = afterRooms;
    r.broken = false;
    std::printf("  rooms %u  (ambient %d bytes, light %d bytes, position after %zu)\n",
                nRooms, goodAmbient, goodLight, r.at);

    const uint32_t nFloor = r.u32();  r.skip(static_cast<size_t>(nFloor) * 2);
    const uint32_t nMeshData = r.u32(); const size_t meshDataAt = r.at;
    r.skip(static_cast<size_t>(nMeshData) * 2);
    const uint32_t nMeshPtr = r.u32(); const size_t meshPtrAt = r.at;
    r.skip(static_cast<size_t>(nMeshPtr) * 4);
    const uint32_t nAnims = r.u32(); const size_t animsAt = r.at;
    r.skip(static_cast<size_t>(nAnims) * 32);            // TR1-3: 32 bytes per anim
    const uint32_t nStateChanges = r.u32(); r.skip(static_cast<size_t>(nStateChanges) * 6);
    const uint32_t nDispatches = r.u32();   r.skip(static_cast<size_t>(nDispatches) * 8);
    const uint32_t nAnimCmds = r.u32();     r.skip(static_cast<size_t>(nAnimCmds) * 2);
    const uint32_t nMeshTrees = r.u32();    const size_t meshTreeAt = r.at;
    r.skip(static_cast<size_t>(nMeshTrees) * 4);
    const uint32_t nFrames = r.u32();       const size_t framesAt = r.at;
    r.skip(static_cast<size_t>(nFrames) * 2);
    const uint32_t nMoveables = r.u32();

    if (r.broken) { std::printf("ERROR: the file ends before the moveable table\n"); return 1; }

    std::printf("  mesh data %u words, mesh pointers %u, anims %u, bones %u words, frames %u words, moveables %u\n",
                nMeshData, nMeshPtr, nAnims, nMeshTrees, nFrames, nMoveables);

    std::vector<Moveable> mv;
    mv.reserve(nMoveables);
    for (uint32_t i = 0; i < nMoveables && !r.broken; ++i)
    {
        Moveable m;
        m.objectId     = r.u32();
        m.numMeshes    = r.u16();
        m.startingMesh = r.u16();
        m.meshTree     = r.u32();
        m.frameOffset  = r.u32();
        m.animIndex    = r.u16();
        mv.push_back(m);
    }
    if (r.broken) { std::printf("ERROR: moveable table incomplete\n"); return 1; }

    if (wanted < 0)
    {
        std::printf("\n  id  meshes  mesh0  boneIdx   frameOfs  anim\n");
        for (const Moveable& m : mv)
            std::printf("  %4u %6u %6u %8u %10u %5u\n",
                        m.objectId, m.numMeshes, m.startingMesh, m.meshTree, m.frameOffset, m.animIndex);
        return 0;
    }

    const Moveable* z = 0;
    for (const Moveable& m : mv) if (static_cast<int>(m.objectId) == wanted) { z = &m; break; }
    if (!z) { std::printf("\nobject %d not in this level.\n", wanted); return 1; }

    // Anim slice: up to the next larger animIndex of another moveable.
    uint32_t nextAnim = nAnims;
    for (const Moveable& m : mv)
        if (m.animIndex > z->animIndex && m.animIndex < nextAnim) nextAnim = m.animIndex;

    std::printf("\n=== object %d ===\n", wanted);
    std::printf("  meshes        %u  (mesh pointers %u..%u)\n",
                z->numMeshes, z->startingMesh, z->startingMesh + z->numMeshes - 1);
    std::printf("  bone index    %u  (%u bones of 4 words each)\n",
                z->meshTree, z->numMeshes ? z->numMeshes - 1 : 0);
    std::printf("  frame offset  %u bytes\n", z->frameOffset);
    std::printf("  anims         %u..%u  = %u total\n",
                z->animIndex, nextAnim - 1, nextAnim - z->animIndex);

    // Measure the first anim: frame rate and frame size tell how the
    // frames lie in the block - that is the size the converter must later
    // recompute into the TR5 format.
    if (z->animIndex < nAnims)
    {
        const size_t a = animsAt + static_cast<size_t>(z->animIndex) * 32;
        uint32_t fo; uint8_t rate, size_; uint16_t state, fs, fe;
        std::memcpy(&fo,    &r.b[a + 0],  4);
        rate = r.b[a + 4];
        size_ = r.b[a + 5];
        std::memcpy(&state, &r.b[a + 6],  2);
        std::memcpy(&fs,    &r.b[a + 16], 2);
        std::memcpy(&fe,    &r.b[a + 18], 2);
        std::printf("  anim[%u]: frameOffset %u, rate %u, frameSize %u words, state %u, frames %u..%u\n",
                    z->animIndex, fo, rate, size_, state, fs, fe);
    }
    std::printf("\n  blocks: meshData@%zu  meshPtrs@%zu  anims@%zu  bones@%zu  frames@%zu\n",
                meshDataAt, meshPtrAt, animsAt, meshTreeAt, framesAt);
    return 0;
}

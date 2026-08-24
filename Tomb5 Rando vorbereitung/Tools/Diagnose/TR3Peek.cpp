//----------------------------------------------------------------------------
// TR3Peek - liest ein TR3-Level (.TR2-Dateiformat, unkomprimiert) so weit,
// dass die Moveable-Tabelle sichtbar wird. Machbarkeitstest fuer den
// T-Rex-Port (#88): erst messen, was drin steht, dann entscheiden.
//
// Aufruf: TR3Peek <level.TR2> [objektId]
//
// Ohne objektId: Kopfdaten und alle Moveables.
// Mit objektId:  dieses Moveable im Detail, samt Anim-Scheibe (bis zum
//                naechsten Moveable in der Anim-Tabelle) und Frame-Bereich.
//
// Der TR3-Aufbau ist durchgehend laengenpraefixiert, deshalb laesst er sich
// ohne Formatwissen ueber die Raumgeometrie durchlaufen - man muss nur die
// Bloecke in der richtigen Reihenfolge ueberspringen.
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct Leser
{
    std::vector<uint8_t> b;
    size_t at = 0;
    bool   kaputt = false;

    bool laden(const char* path)
    {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        b.resize(static_cast<size_t>(n));
        const size_t gelesen = std::fread(b.data(), 1, b.size(), f);
        std::fclose(f);
        return gelesen == b.size();
    }

    bool platz(size_t n) { if (at + n > b.size()) { kaputt = true; return false; } return true; }
    uint8_t  u8()  { if (!platz(1)) return 0; return b[at++]; }
    uint16_t u16() { if (!platz(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
    uint32_t u32() { if (!platz(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
    int32_t  i32() { return static_cast<int32_t>(u32()); }
    void spring(size_t n) { if (platz(n)) at += n; }
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
        std::printf("Aufruf: TR3Peek <level.TR2> [objektId]\n");
        return 2;
    }
    const int suche = (argc == 3) ? std::atoi(argv[2]) : -1;

    Leser r;
    if (!r.laden(argv[1])) { std::printf("Datei laesst sich nicht read: %s\n", argv[1]); return 2; }

    const uint32_t version = r.u32();
    std::printf("Datei %s (%.1f MB), Version 0x%08X", argv[1], r.b.size() / 1048576.0, version);
    // TR2 = 0x0000002D, TR3 = 0xFF080038 / 0xFF180038 / 0xFF180034 (hohes Byte 0xFF)
    if (version == 0x0000002D) std::printf("  -> TR2\n");
    else if ((version >> 24) == 0xFF) std::printf("  -> TR3\n");
    else std::printf("  -> unbekannt\n");

    r.spring(768);                       // Palette 8 Bit
    r.spring(1024);                      // Palette 16 Bit
    const uint32_t nTextiles = r.u32();
    r.spring(static_cast<size_t>(nTextiles) * 65536);    // Textile8
    r.spring(static_cast<size_t>(nTextiles) * 131072);   // Textile16
    std::printf("  Texturseiten %u\n", nTextiles);

    r.u32();                             // unbenutzt
    const size_t raumBlockAt = r.at;
    const uint16_t nRooms = r.u16();

    // Zwei Feldgroessen im Raumkopf sind je nach Quelle unterschiedlich
    // dokumentiert: die Zahl der Ambient-Bytes und die Groesse eines
    // Raumlichts. Statt zu raten, wird jede Kombination durchprobiert und die
    // genommen, die einen SINNVOLLEN Rest ergibt - erkennbar daran, dass die
    // Bloecke dahinter plausible Groessen haben und die Moveable-Tabelle
    // aufgeht. Falsche Annahmen laufen fast immer sofort aus der Datei.
    int gutAmbient = -1, gutLicht = -1;
    size_t nachRaeumen = 0;
    for (int amb = 2; amb <= 6 && gutAmbient < 0; amb += 2)
    {
        for (int licht = 18; licht <= 26 && gutAmbient < 0; licht += 2)
        {
            r.at = raumBlockAt + 2;
            r.kaputt = false;
            for (uint16_t i = 0; i < nRooms && !r.kaputt; ++i)
            {
                r.spring(16);
                const uint32_t datenWorte = r.u32();
                r.spring(static_cast<size_t>(datenWorte) * 2);
                const uint16_t nPortale = r.u16();
                r.spring(static_cast<size_t>(nPortale) * 32);
                const uint16_t nZ = r.u16();
                const uint16_t nX = r.u16();
                r.spring(static_cast<size_t>(nZ) * nX * 8);
                r.spring(static_cast<size_t>(amb));
                const uint16_t nLights = r.u16();
                r.spring(static_cast<size_t>(nLights) * licht);
                const uint16_t nStatics = r.u16();
                r.spring(static_cast<size_t>(nStatics) * 20);
                r.spring(2 + 2);         // AlternateRoom, Flags
                r.spring(3);             // WaterScheme, ReverbInfo, Filler
            }
            if (r.kaputt) continue;

            // Probelesen: passen die folgenden Blockgroessen in die Datei?
            const size_t merk = r.at;
            bool ok = true;
            const uint32_t pFloor = r.u32();   r.spring(static_cast<size_t>(pFloor) * 2);
            const uint32_t pMesh  = r.u32();   r.spring(static_cast<size_t>(pMesh) * 2);
            const uint32_t pPtr   = r.u32();   r.spring(static_cast<size_t>(pPtr) * 4);
            const uint32_t pAnim  = r.u32();   r.spring(static_cast<size_t>(pAnim) * 32);
            if (r.kaputt || pFloor > 500000 || pMesh > 2000000 || pPtr > 20000 || pAnim > 20000) ok = false;
            if (ok) { gutAmbient = amb; gutLicht = licht; nachRaeumen = merk; }
            r.at = merk;
        }
    }
    if (gutAmbient < 0) { std::printf("FEHLER: keine Raumkopf-Variante passt (Raeume %u)\n", nRooms); return 1; }
    r.at = nachRaeumen;
    r.kaputt = false;
    std::printf("  Raeume %u  (Ambient %d Byte, Licht %d Byte, Position danach %zu)\n",
                nRooms, gutAmbient, gutLicht, r.at);

    const uint32_t nFloor = r.u32();  r.spring(static_cast<size_t>(nFloor) * 2);
    const uint32_t nMeshData = r.u32(); const size_t meshDataAt = r.at;
    r.spring(static_cast<size_t>(nMeshData) * 2);
    const uint32_t nMeshPtr = r.u32(); const size_t meshPtrAt = r.at;
    r.spring(static_cast<size_t>(nMeshPtr) * 4);
    const uint32_t nAnims = r.u32(); const size_t animsAt = r.at;
    r.spring(static_cast<size_t>(nAnims) * 32);          // TR1-3: 32 Byte je Anim
    const uint32_t nStateChanges = r.u32(); r.spring(static_cast<size_t>(nStateChanges) * 6);
    const uint32_t nDispatches = r.u32();   r.spring(static_cast<size_t>(nDispatches) * 8);
    const uint32_t nAnimCmds = r.u32();     r.spring(static_cast<size_t>(nAnimCmds) * 2);
    const uint32_t nMeshTrees = r.u32();    const size_t meshTreeAt = r.at;
    r.spring(static_cast<size_t>(nMeshTrees) * 4);
    const uint32_t nFrames = r.u32();       const size_t framesAt = r.at;
    r.spring(static_cast<size_t>(nFrames) * 2);
    const uint32_t nMoveables = r.u32();

    if (r.kaputt) { std::printf("FEHLER: Datei endet vor der Moveable-Tabelle\n"); return 1; }

    std::printf("  Meshdaten %u Worte, Meshzeiger %u, Anims %u, Bones %u Worte, Frames %u Worte, Moveables %u\n",
                nMeshData, nMeshPtr, nAnims, nMeshTrees, nFrames, nMoveables);

    std::vector<Moveable> mv;
    mv.reserve(nMoveables);
    for (uint32_t i = 0; i < nMoveables && !r.kaputt; ++i)
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
    if (r.kaputt) { std::printf("FEHLER: Moveable-Tabelle unvollstaendig\n"); return 1; }

    if (suche < 0)
    {
        std::printf("\n  Id  Meshes  Mesh0  BoneIdx   FrameOfs  Anim\n");
        for (const Moveable& m : mv)
            std::printf("  %4u %6u %6u %8u %10u %5u\n",
                        m.objectId, m.numMeshes, m.startingMesh, m.meshTree, m.frameOffset, m.animIndex);
        return 0;
    }

    const Moveable* z = 0;
    for (const Moveable& m : mv) if (static_cast<int>(m.objectId) == suche) { z = &m; break; }
    if (!z) { std::printf("\nObjekt %d nicht in diesem Level.\n", suche); return 1; }

    // Anim-Scheibe: bis zum naechstgroesseren animIndex eines anderen Moveables.
    uint32_t naechsterAnim = nAnims;
    for (const Moveable& m : mv)
        if (m.animIndex > z->animIndex && m.animIndex < naechsterAnim) naechsterAnim = m.animIndex;

    std::printf("\n=== Objekt %d ===\n", suche);
    std::printf("  Meshes        %u  (Mesh-Zeiger %u..%u)\n",
                z->numMeshes, z->startingMesh, z->startingMesh + z->numMeshes - 1);
    std::printf("  Bone-Index    %u  (%u Knochen a 4 Worte)\n",
                z->meshTree, z->numMeshes ? z->numMeshes - 1 : 0);
    std::printf("  Frame-Offset  %u Byte\n", z->frameOffset);
    std::printf("  Anims         %u..%u  = %u Stueck\n",
                z->animIndex, naechsterAnim - 1, naechsterAnim - z->animIndex);

    // Erste Anim ausmessen: Frame-Rate und Frame-Groesse sagen, wie die
    // Frames im Block liegen - das ist die Groesse, die der Konverter
    // spaeter auf das TR5-Format umrechnen muss.
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
        std::printf("  Anim[%u]: frameOffset %u, rate %u, frameSize %u Worte, state %u, Frames %u..%u\n",
                    z->animIndex, fo, rate, size_, state, fs, fe);
    }
    std::printf("\n  Bloecke: MeshDaten@%zu  MeshZeiger@%zu  Anims@%zu  Bones@%zu  Frames@%zu\n",
                meshDataAt, meshPtrAt, animsAt, meshTreeAt, framesAt);
    return 0;
}

//----------------------------------------------------------------------------
// TR3Hol - holt ein Moveable aus einem TR3-Level und rechnet es auf das
// TR5-Layout um. Schritt 1 des T-Rex-Ports (#88): erst die Formatbruecke
// beweisen, dann einsetzen.
//
// Aufruf: TR3Hol <tr3level.TR2> <objektId> <ziel.bin>
//
// Was gleich ist und einfach mitwandert:
//   - Mesh-Kopf: TR3 Centre(6) + CollRadius(4) = 10 Byte, TR5 liest genau
//     5 shorts vor dem Vertex-Wort. Gleiche Stelle, gleiche Bedeutung.
//   - Vertices und Normalen: je 3 shorts, unveraendert.
//   - Knochenbaum: 4 int32 je Knochen, in TR1..TR5 identisch.
//   - Frames: TR2/TR3 und TR4/TR5 packen Rotationen gleich (2 Bit Achswahl,
//     danach ein oder zwei Worte). Nachgerechnet am T-Rex: frameSize 58
//     Worte = 6 BBox + 3 Versatz + 49 fuer 25 Knochen, also 24 zweiwortige
//     und eine einwortige Rotation. Passt.
//
// Was umgerechnet werden MUSS:
//   - Faces: TR3 traegt 4 Vertexworte + 1 Texturwort (Quad 8 Byte,
//     Tri 6 Byte). TR5 haengt je ein EFFEKTWORT an (12 bzw. 10 Byte).
//   - Farbige Faces: TR3 kennt sie, TR5 nicht. Sie werden zu texturierten
//     mit Textur 0 - sichtbar falsch, aber geometrisch heil; der T-Rex
//     nutzt sie ohnehin kaum.
//   - Animationen: TR3 32 Byte, TR5 40 Byte. Die zusaetzlichen 8 Byte sind
//     die seitlichen Geschwindigkeiten (TR4+), sie werden auf 0 gesetzt.
//
// Die Ausgabe ist ein schlichter Container, den der Einsetzer liest:
//   Kopf: "T35H", version, objektId, numMeshes, numAnims, numFrameWorte,
//         numBoneWorte, numStateChanges, numDispatches, numCmdWorte
//   dann: Meshbloecke (je uint32 Laenge + Bytes), Bonebytes, Framebytes,
//         Animbytes (40 je Anim), StateChanges, Dispatches, Commands
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Leser
    {
        std::vector<uint8_t> b;
        size_t at = 0;
        bool   kaputt = false;

        bool laden(const char* p)
        {
            FILE* f = std::fopen(p, "rb");
            if (!f) return false;
            std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            b.resize(static_cast<size_t>(n));
            const size_t g = std::fread(b.data(), 1, b.size(), f);
            std::fclose(f);
            return g == b.size();
        }
        bool platz(size_t n) { if (at + n > b.size()) { kaputt = true; return false; } return true; }
        uint16_t u16() { if (!platz(2)) return 0; uint16_t v; std::memcpy(&v, &b[at], 2); at += 2; return v; }
        uint32_t u32() { if (!platz(4)) return 0; uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
        void spring(size_t n) { if (platz(n)) at += n; }
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
    if (argc != 4) { std::printf("Aufruf: TR3Hol <tr3level.TR2> <objektId> <ziel.bin>\n"); return 2; }
    const int suche = std::atoi(argv[2]);

    Leser r;
    if (!r.laden(argv[1])) { std::printf("Datei nicht lesbar: %s\n", argv[1]); return 2; }

    const uint32_t version = r.u32();
    if ((version >> 24) != 0xFF) { std::printf("Kein TR3-Level (Version 0x%08X)\n", version); return 2; }

    r.spring(768); r.spring(1024);
    const uint32_t nTex = r.u32();
    r.spring(size_t(nTex) * 65536);              // Textile8 (Palettenfassung)
    const size_t tex16At = r.at;                 // Textile16: ARGB1555, 131072 je Kachel
    r.spring(size_t(nTex) * 131072);
    r.u32();

    const size_t raumAt = r.at;
    const uint16_t nRooms = r.u16();

    // Raumkopf-Varianten durchprobieren statt raten (siehe TR3Peek).
    size_t nachRaeumen = 0;
    for (int amb = 2; amb <= 6 && !nachRaeumen; amb += 2)
    for (int licht = 18; licht <= 26 && !nachRaeumen; licht += 2)
    {
        r.at = raumAt + 2; r.kaputt = false;
        for (uint16_t i = 0; i < nRooms && !r.kaputt; ++i)
        {
            r.spring(16);
            r.spring(size_t(r.u32()) * 2);
            r.spring(size_t(r.u16()) * 32);
            const uint16_t nZ = r.u16(), nX = r.u16();
            r.spring(size_t(nZ) * nX * 8);
            r.spring(size_t(amb));
            r.spring(size_t(r.u16()) * licht);
            r.spring(size_t(r.u16()) * 20);
            r.spring(2 + 2 + 3);
        }
        if (r.kaputt) continue;
        const size_t merk = r.at;
        const uint32_t a = r.u32(); r.spring(size_t(a) * 2);
        const uint32_t b2 = r.u32(); r.spring(size_t(b2) * 2);
        const uint32_t c = r.u32(); r.spring(size_t(c) * 4);
        const uint32_t d = r.u32(); r.spring(size_t(d) * 32);
        if (!r.kaputt && a < 500000 && b2 < 2000000 && c < 20000 && d < 20000) nachRaeumen = merk;
        r.at = merk;
    }
    if (!nachRaeumen) { std::printf("Raumdurchlauf gescheitert\n"); return 1; }
    r.at = nachRaeumen; r.kaputt = false;

    r.spring(size_t(r.u32()) * 2);                                  // Floordata
    const uint32_t nMeshData = r.u32(); const size_t meshAt = r.at;
    r.spring(size_t(nMeshData) * 2);
    const uint32_t nPtr = r.u32(); const size_t ptrAt = r.at;
    r.spring(size_t(nPtr) * 4);
    const uint32_t nAnims = r.u32(); const size_t animAt = r.at;
    r.spring(size_t(nAnims) * 32);
    const uint32_t nSC = r.u32(); const size_t scAt = r.at;  r.spring(size_t(nSC) * 6);
    const uint32_t nAD = r.u32(); const size_t adAt = r.at;  r.spring(size_t(nAD) * 8);
    const uint32_t nCmd = r.u32(); const size_t cmdAt = r.at; r.spring(size_t(nCmd) * 2);
    const uint32_t nBone = r.u32(); const size_t boneAt = r.at; r.spring(size_t(nBone) * 4);
    const uint32_t nFrame = r.u32(); const size_t frameAt = r.at; r.spring(size_t(nFrame) * 2);
    const uint32_t nMv = r.u32();
    if (r.kaputt) { std::printf("Datei endet vor der Moveable-Tabelle\n"); return 1; }

    std::vector<Moveable> mv;
    for (uint32_t i = 0; i < nMv; ++i)
    {
        Moveable m;
        m.objectId = r.u32(); m.numMeshes = r.u16(); m.startingMesh = r.u16();
        m.meshTree = r.u32(); m.frameOffset = r.u32(); m.animIndex = r.u16();
        mv.push_back(m);
    }
    const Moveable* z = 0;
    for (const Moveable& m : mv) if (int(m.objectId) == suche) { z = &m; break; }
    if (!z) { std::printf("Objekt %d nicht vorhanden\n", suche); return 1; }

    uint32_t animEnde = nAnims;
    for (const Moveable& m : mv)
        if (m.animIndex > z->animIndex && m.animIndex < animEnde) animEnde = m.animIndex;
    const uint32_t animCount = animEnde - z->animIndex;

    std::printf("Objekt %d: %u Meshes, %u Knochen, Anims %u..%u (%u)\n",
                suche, z->numMeshes, z->numMeshes ? z->numMeshes - 1 : 0,
                z->animIndex, animEnde - 1, animCount);

    // --- Weiter bis zu den Objekttexturen (fuer die Texturarbeit, #90) -----
    // Die strittigen Feldgroessen im Dateiende werden wieder durchprobiert
    // statt geraten: richtig ist die Kombination, bei der die letzte Tabelle
    // exakt am Dateiende endet. Findet sich keine, bleibt objTexAt 0 und der
    // Bericht faellt eben knapper aus - das Modell wandert trotzdem.
    uint32_t nObjTex = 0;
    size_t objTexAt = 0;
    {
        const size_t nachMv = r.at;
        for (int mapN = 256; mapN <= 370 && !objTexAt; mapN += 114)
        for (int itemSz = 22; itemSz <= 24 && !objTexAt; itemSz += 2)
        for (int boxSz = 8; boxSz <= 20 && !objTexAt; boxSz += 12)
        for (int zoneN = 10; zoneN <= 20 && !objTexAt; zoneN += 10)
        {
            r.at = nachMv; r.kaputt = false;
            r.spring(size_t(r.u32()) * 32);              // StaticMeshes
            r.spring(size_t(r.u32()) * 16);              // SpriteTextures
            r.spring(size_t(r.u32()) * 8);               // SpriteSequences
            r.spring(size_t(r.u32()) * 16);              // Cameras
            r.spring(size_t(r.u32()) * 16);              // SoundSources
            const uint32_t nBoxes = r.u32();
            r.spring(size_t(nBoxes) * boxSz);
            r.spring(size_t(r.u32()) * 2);               // Overlaps
            r.spring(size_t(nBoxes) * zoneN * 2);        // Zones
            r.spring(size_t(r.u32()) * 2);               // AnimatedTextures
            const uint32_t nOT = r.u32();
            const size_t otAt = r.at;
            r.spring(size_t(nOT) * 20);
            r.spring(size_t(r.u32()) * itemSz);          // Items
            r.spring(8192);                              // LightMap
            r.spring(size_t(r.u16()) * 16);              // CinematicFrames
            r.spring(size_t(r.u16()));                   // DemoData
            r.spring(size_t(mapN) * 2);                  // SoundMap
            r.spring(size_t(r.u32()) * 8);               // SoundDetails
            r.spring(size_t(r.u32()) * 4);               // SampleIndices
            if (r.kaputt || r.at != r.b.size()) continue;
            if (!nOT || nOT > 20000) continue;
            nObjTex = nOT; objTexAt = otAt;
        }
        r.at = nachMv; r.kaputt = false;
    }

    // --- Meshes umschreiben -------------------------------------------------
    std::vector<std::vector<uint8_t>> meshes;
    int gesQuads = 0, gesTris = 0, gesFarbig = 0, maxVerts = 0;
    int erstesTex = -1;                       // erste Textur, die das Modell selbst nutzt
    struct FarbStelle { std::vector<uint8_t>* puffer; size_t at; };
    std::vector<FarbStelle> farbigeStellen;
    for (uint16_t i = 0; i < z->numMeshes; ++i)
    {
        const uint32_t off = ru32(r.b, ptrAt + size_t(z->startingMesh + i) * 4);
        size_t p = meshAt + off;
        std::vector<uint8_t> o;

        for (int k = 0; k < 10; ++k) o.push_back(r.b[p + k]);       // Centre + CollRadius
        p += 10;
        const int16_t nVerts = rs16(r.b, p); p += 2;
        if (nVerts > 255) { std::printf("  Mesh %u hat %d Vertices - TR5 liest nur 255\n", i, nVerts); return 1; }
        put16(o, uint16_t(nVerts));
        for (int v = 0; v < nVerts * 3; ++v) { put16(o, ru16(r.b, p)); p += 2; }
        const int16_t nNorms = rs16(r.b, p); p += 2;
        put16(o, uint16_t(nNorms));
        const int worte = (nNorms >= 0) ? nVerts * 3 : nVerts;
        for (int v = 0; v < worte; ++v) { put16(o, ru16(r.b, p)); p += 2; }
        if (nVerts > maxVerts) maxVerts = nVerts;

        // Texturierte Quads: 4 Vertexworte + Textur, dazu neu ein Effektwort.
        const int16_t nQ = rs16(r.b, p); p += 2;
        const int16_t nQfarbAt = 0; (void)nQfarbAt;
        std::vector<uint8_t> quads, tris;
        int qz = 0, tz = 0;
        for (int q = 0; q < nQ; ++q)
        {
            for (int w = 0; w < 5; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            put16(quads, 0);                                        // Effektwort
            ++qz;
        }
        const int16_t nT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nT; ++t)
        {
            for (int w = 0; w < 4; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            put16(tris, 0);
            ++tz;
        }
        // Farbige Faces: TR5 kennt sie nicht. Sie werden texturiert
        // uebernommen, damit die Geometrie geschlossen bleibt - aber NICHT
        // mit Textur 0. Die ist im Quelllevel irgendetwas Beliebiges; beim
        // T-Rex war es ein Stueck von Laras Bein, das dann unter seinem Maul
        // klebte (im Spiel gesehen). Stattdessen die erste Textur, die das
        // Modell selbst benutzt - die gehoert garantiert zu ihm.
        const int16_t nCQ = rs16(r.b, p); p += 2;
        for (int q = 0; q < nCQ; ++q)
        {
            for (int w = 0; w < 4; ++w) { put16(quads, ru16(r.b, p)); p += 2; }
            p += 2;                                                 // Farbindex verwerfen
            farbigeStellen.push_back({ &quads, quads.size() });
            put16(quads, 0); put16(quads, 0);
            ++qz; ++gesFarbig;
        }
        const int16_t nCT = rs16(r.b, p); p += 2;
        for (int t = 0; t < nCT; ++t)
        {
            for (int w = 0; w < 3; ++w) { put16(tris, ru16(r.b, p)); p += 2; }
            p += 2;
            farbigeStellen.push_back({ &tris, tris.size() });
            put16(tris, 0); put16(tris, 0);
            ++tz; ++gesFarbig;
        }

        // Erste echte Textur dieses Meshes merken und die farbigen Stellen
        // darauf setzen, BEVOR die Puffer ins Mesh wandern.
        if (erstesTex < 0)
        {
            if (nQ > 0)      erstesTex = ru16(quads, 8);
            else if (nT > 0) erstesTex = ru16(tris, 6);
        }
        for (const FarbStelle& fs : farbigeStellen)
            if (erstesTex >= 0)
            {
                const uint16_t v = uint16_t(erstesTex);
                (*fs.puffer)[fs.at]     = uint8_t(v & 0xFF);
                (*fs.puffer)[fs.at + 1] = uint8_t(v >> 8);
            }
        farbigeStellen.clear();

        put16(o, uint16_t(qz)); o.insert(o.end(), quads.begin(), quads.end());
        put16(o, uint16_t(tz)); o.insert(o.end(), tris.begin(), tris.end());
        gesQuads += qz; gesTris += tz;
        meshes.push_back(o);
    }

    // --- Welche Objekttexturen braucht dieses Modell? -----------------------
    //
    // Die Faces tragen noch die TR3-Indizes; erst der Einsetzer biegt sie um.
    // Fuer die Texturarbeit (#90) zaehlt, wie viele verschiedene Eintraege und
    // vor allem wie viele verschiedene KACHELN dahinter stehen - jede Kachel
    // wird im Ziel eine 256x256-Seite.
    std::vector<int> texIdx;      // gebrauchte TR3-Objekttexturen, in Fundreihenfolge
    std::vector<int> kacheln;     // die Kacheln dahinter, in Fundreihenfolge
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
        auto merke = [&](int t) {
            for (int x : texIdx) if (x == t) return;
            texIdx.push_back(t);
            if (t >= 0 && nObjTex && uint32_t(t) < nObjTex)
            {
                const int k = ru16(r.b, objTexAt + size_t(t) * 20 + 2) & 0x7FFF;
                for (int x : kacheln) if (x == k) return;
                kacheln.push_back(k);
            }
        };
        const int16_t nq = rs16(m, p); p += 2;
        for (int i = 0; i < nq; ++i, p += 12) merke(ru16(m, p + 8) & 0x7FFF);
        const int16_t nt = rs16(m, p); p += 2;
        for (int i = 0; i < nt; ++i, p += 10) merke(ru16(m, p + 6) & 0x7FFF);
    }
    std::printf("  Objekttexturen: %zu verschiedene auf %zu Kacheln (",
                texIdx.size(), kacheln.size());
    for (size_t i = 0; i < kacheln.size(); ++i) std::printf("%s%d", i ? ", " : "", kacheln[i]);
    std::printf(")\n");

    // --- Frame-Scheibe abgrenzen -------------------------------------------
    //
    // Der Frame-Zeiger einer Animation ist ein Byte-Offset in den GLOBALEN
    // Frameblock des Levels. Wer nur das Objekt herausnimmt, muss die Scheibe
    // schneiden und die Zeiger auf ihren Anfang beziehen - sonst zeigen sie im
    // Ziel irgendwohin. Das Ende ist der Anfang des naechsten Objekts, genau
    // wie bei den Anims.
    uint32_t frameEnde = nFrame * 2;
    for (const Moveable& m : mv)
        if (m.frameOffset > z->frameOffset && m.frameOffset < frameEnde) frameEnde = m.frameOffset;
    const uint32_t frameBytes = frameEnde - z->frameOffset;

    // --- Animationen 32 -> 40 Byte, Frame-Zeiger auf die Scheibe beziehen ---
    std::vector<uint8_t> anims;
    int ausserhalb = 0;
    uint16_t animMin = 0xFFFF, animMax = 0;
    for (uint32_t i = 0; i < animCount; ++i)
    {
        const size_t a = animAt + size_t(z->animIndex + i) * 32;

        const uint32_t fo = ru32(r.b, a);
        if (fo < z->frameOffset || fo >= frameEnde) ++ausserhalb;
        put32(anims, fo - z->frameOffset);                          // rebasiert

        anims.push_back(r.b[a + 4]); anims.push_back(r.b[a + 5]);   // Rate, Size
        anims.push_back(r.b[a + 6]); anims.push_back(r.b[a + 7]);   // State
        for (int k = 8; k < 16; ++k) anims.push_back(r.b[a + k]);   // Speed, Accel
        for (int k = 0; k < 8; ++k) anims.push_back(0);             // Lateral (TR4+)

        for (int k = 16; k < 20; ++k) anims.push_back(r.b[a + k]);  // FrameStart, FrameEnd
        const uint16_t nextAnim = ru16(r.b, a + 20);
        if (nextAnim < animMin) animMin = nextAnim;
        if (nextAnim > animMax) animMax = nextAnim;
        put16(anims, uint16_t(nextAnim - z->animIndex));            // auf die Scheibe beziehen
        for (int k = 22; k < 32; ++k) anims.push_back(r.b[a + k]);  // NextFrame, SC, Cmd
    }

    // --- Ausgabe ------------------------------------------------------------
    std::vector<uint8_t> out;
    out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('H');
    put32(out, 2);                 // Version 2: mit Texturteil am Ende
    put32(out, uint32_t(suche));
    put32(out, z->numMeshes);
    put32(out, animCount);
    put32(out, frameBytes / 2);  // nur die Scheibe dieses Objekts, in Worten
    put32(out, uint32_t(z->numMeshes ? (z->numMeshes - 1) * 4 : 0));
    put32(out, nSC); put32(out, nAD); put32(out, nCmd);
    put32(out, z->frameOffset);
    put32(out, z->animIndex);

    for (const std::vector<uint8_t>& m : meshes) { put32(out, uint32_t(m.size())); out.insert(out.end(), m.begin(), m.end()); }
    for (uint32_t i = 0; i < (z->numMeshes ? uint32_t(z->numMeshes - 1) * 4 : 0); ++i)
        put32(out, ru32(r.b, boneAt + (size_t(z->meshTree) + i) * 4));
    // --- Frames: Einachs-Drehungen auf TR5s Breite umschreiben -------------
    //
    // DIE eigentliche Bruecke zwischen den Spielen. Beide packen eine Drehung
    // in ein oder zwei Worte, die oberen zwei Bits waehlen die Form. Der
    // Zweiwort-Fall ist IDENTISCH (phd_RotYXZpack rechnet in beiden Spielen
    // (angles >> 10) & 0x3FF, dann << 6). Der Einachs-Fall aber nicht:
    //
    //   TR3 (draw.cpp:183):  (wort & 1023)  << 6     10 Bit
    //   TR5 (draw.cpp:267):  (wort & 0xFFF) << 4     12 Bit
    //
    // Ein unveraendert uebernommenes TR3-Wort liest TR5 also mit einem
    // Viertel des Winkels - und weil TR3 den Ueberlauf ins Negative nutzt,
    // kippen grosse Values ganz. Betroffen sind genau die EINACHSGELENKE:
    // Kiefer, Knoechel, Zehen. Im Spiel gesehen als verdrehte Fuesse und ein
    // umgestuelptes Maul, waehrend der Rumpf stimmte - der dreht um alle drei
    // Achsen und laeuft damit durch den identischen Zweiwort-Fall.
    //
    // Umrechnung verlustfrei: TR5 soll v << 6 ergeben, rechnet aber << 4,
    // also gehoert v << 2 ins 12-Bit-Feld (v <= 1023, passt).
    {
        std::vector<uint8_t> frames(r.b.begin() + frameAt + z->frameOffset,
                                    r.b.begin() + frameAt + frameEnde);
        int umgerechnet = 0, ausserhalb = 0;
        std::vector<uint32_t> erledigt;

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
                if (start < 0 || start + 18 > (long long)frames.size()) { ++ausserhalb; continue; }

                bool schon = false;
                for (uint32_t e : erledigt) if (e == uint32_t(start)) { schon = true; break; }
                if (schon) continue;
                erledigt.push_back(uint32_t(start));

                size_t p2 = size_t(start) + 18;          // BBox (6) + Versatz (3) Worte
                for (uint16_t m = 0; m < z->numMeshes; ++m)
                {
                    if (p2 + 2 > frames.size()) break;
                    const uint16_t w = ru16(frames, p2);
                    if (w & 0xC000)
                    {
                        const uint16_t v = w & 0x3FF;
                        const uint16_t neu = uint16_t((w & 0xC000) | ((v << 2) & 0xFFF));
                        frames[p2]     = uint8_t(neu & 0xFF);
                        frames[p2 + 1] = uint8_t(neu >> 8);
                        ++umgerechnet;
                        p2 += 2;
                    }
                    else p2 += 4;
                }
            }
        }
        std::printf("  Drehungen: %d Einachs-Worte auf TR5-Breite gebracht (%zu Frames)\n",
                    umgerechnet, erledigt.size());
        if (ausserhalb) std::printf("  ACHTUNG: %d Frames lagen ausserhalb der Scheibe\n", ausserhalb);

        out.insert(out.end(), frames.begin(), frames.end());
    }
    out.insert(out.end(), anims.begin(), anims.end());
    out.insert(out.end(), r.b.begin() + scAt,  r.b.begin() + scAt  + size_t(nSC) * 6);

    // Sprungtabelle (AnimDispatch, 8 Byte: Low, High, NextAnim, NextFrame).
    // Ihr NextAnim ist ein GLOBALER Anim-Index des Quelllevels - genau wie
    // bei den Anims selbst muss er auf die Scheibe bezogen werden, sonst
    // zeigt er im Ziel irgendwohin. Das war der Absturz beim ersten Versuch:
    // die Eintraege wanderten ungerebased mit, und sobald die Steuerung einen
    // Zustandswechsel nahm, sprang die Engine in eine fremde Animation.
    // Eintraege ausserhalb der Scheibe gehoeren zu anderen Objekten; sie
    // werden nie gelesen, bekommen aber 0, damit kein wilder Wert dasteht.
    {
        std::vector<uint8_t> ad(r.b.begin() + adAt, r.b.begin() + adAt + size_t(nAD) * 8);
        int drin = 0, drauss = 0;
        for (size_t o = 0; o + 8 <= ad.size(); o += 8)
        {
            const uint16_t na = uint16_t(ad[o + 4] | (ad[o + 5] << 8));
            uint16_t neu = 0;
            if (na >= z->animIndex && na < animEnde) { neu = uint16_t(na - z->animIndex); ++drin; }
            else ++drauss;
            ad[o + 4] = uint8_t(neu & 0xFF);
            ad[o + 5] = uint8_t(neu >> 8);
        }
        std::printf("  Sprungtabelle: %d Eintraege auf die Scheibe bezogen, %d fremde genullt\n", drin, drauss);
        out.insert(out.end(), ad.begin(), ad.end());
    }
    out.insert(out.end(), r.b.begin() + cmdAt, r.b.begin() + cmdAt + size_t(nCmd) * 2);

    // --- Texturteil (Paketversion 2) --------------------------------------
    //
    // Angehaengt, damit alte Pakete weiter lesbar bleiben: wer nur bis zum
    // Kommandoblock liest, bekommt genau das alte Format.
    //
    //   uint32 anzahlTexturen
    //   je Textur: origIndex(2), drawtype(2), kachelImPaket(2),
    //              dann 8 Byte u1,v1,u2,v2,u3,v3,u4,v4 als PIXEL
    //   uint32 anzahlKacheln
    //   je Kachel: 131072 Byte ARGB1555 (TR3s 16-Bit-Fassung)
    //
    // Die UVs bleiben hier PIXEL. Erst der Einsetzer schiebt sie um 8 Bit
    // nach links - TR5 fuehrt sie als 8.8-Festkomma (file.cpp:1096).
    {
        put32(out, uint32_t(texIdx.size()));
        int geschrieben = 0;
        for (int t : texIdx)
        {
            if (t < 0 || uint32_t(t) >= nObjTex) { put16(out, uint16_t(t)); put16(out, 0); put16(out, 0);
                                                   for (int k = 0; k < 8; ++k) out.push_back(0); continue; }
            const size_t o = objTexAt + size_t(t) * 20;
            const uint16_t attribut = ru16(r.b, o);
            const int kachel = ru16(r.b, o + 2) & 0x7FFF;

            int lokal = 0;
            for (size_t i = 0; i < kacheln.size(); ++i) if (kacheln[i] == kachel) { lokal = int(i); break; }

            put16(out, uint16_t(t));
            put16(out, attribut);
            put16(out, uint16_t(lokal));
            for (int v = 0; v < 4; ++v)
            {
                out.push_back(r.b[o + 4 + v * 4 + 1]);   // Xpixel
                out.push_back(r.b[o + 4 + v * 4 + 3]);   // Ypixel
            }
            ++geschrieben;
        }

        put32(out, uint32_t(kacheln.size()));
        for (int k : kacheln)
        {
            if (k < 0 || uint32_t(k) >= nTex)
            { std::printf("  Kachel %d liegt ausserhalb der Texturseiten\n", k); return 1; }
            const size_t at = tex16At + size_t(k) * 131072;
            out.insert(out.end(), r.b.begin() + at, r.b.begin() + at + 131072);
        }
        std::printf("  Texturteil: %d Eintraege, %zu Kacheln a 131072 Byte\n",
                    geschrieben, kacheln.size());
    }

    FILE* f = std::fopen(argv[3], "wb");
    if (!f) { std::printf("Ziel nicht schreibbar: %s\n", argv[3]); return 2; }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);

    // --- Gegenprobe: laeuft der TR5-Ableser sauber durch? -------------------
    //
    // Nachgebaut nach ProcessMeshData der Engine (drawroom.cpp), identisch zu
    // meshSize() in Tr5Transfer.cpp. Wenn die gerechnete Laenge auf das Byte
    // mit der geschriebenen uebereinstimmt, passt das Layout - das ist der
    // eigentliche Beweis der Formatbruecke, nicht die blosse Byteausgabe.
    int schlecht = 0;
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
            std::printf("  MESH %zu PASST NICHT: Ableser endet bei %zu, geschrieben %zu\n", i, p, m.size());
            ++schlecht;
        }
    }
    std::printf("  Gegenprobe TR5-Ableser: %zu Meshes geprueft, %d Abweichungen\n",
                meshes.size(), schlecht);

    size_t meshBytes = 0; for (const std::vector<uint8_t>& m : meshes) meshBytes += m.size();
    std::printf("  Meshes umgeschrieben: %zu Byte, %d Quads, %d Tris (davon %d aus farbigen Faces)\n",
                meshBytes, gesQuads, gesTris, gesFarbig);
    std::printf("  Groesstes Mesh: %d Vertices\n", maxVerts);
    std::printf("  Anims: %u a 40 Byte, Frame-Scheibe %u Byte (%u..%u), Knochen %u Worte\n",
                animCount, frameBytes, z->frameOffset, frameEnde,
                z->numMeshes ? (z->numMeshes - 1) * 4 : 0);
    std::printf("  Frame-Zeiger ausserhalb der Scheibe: %d   Sprungziele %u..%u (Scheibe %u..%u)\n",
                ausserhalb, animMin, animMax, z->animIndex, animEnde - 1);
    if (ausserhalb) std::printf("  ACHTUNG: Frame-Zeiger zeigen aus der Scheibe heraus\n");
    if (animMin < z->animIndex || animMax >= animEnde)
        std::printf("  ACHTUNG: eine Anim springt aus der eigenen Scheibe heraus\n");
    std::printf("  Geschrieben: %s (%zu Byte)\n", argv[3], out.size());
    return 0;
}

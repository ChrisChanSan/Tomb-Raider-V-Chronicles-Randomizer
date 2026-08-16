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
// Entnahme aus einem TR3-Level - Umsetzung zu Tr3Quelle.h (#92).
//
// Wortgleich aus dem Werkzeug TR3Hol uebernommen; geaendert wurden nur der
// Rahmen (main -> Bibliotheksfunktion), das Laden ueber einen breiten Pfad
// und die Fehlerausgaenge, die jetzt einen Text liefern statt zu drucken.
// Die Formatbruecke selbst ist UNANGETASTET - sie ist an genau einer Stelle
// richtig, und das soll sie bleiben.
//
// Die Fortschrittsmeldungen bleiben als printf stehen: im Werkzeug sind sie
// die gewohnte Ausgabe, in der Oberflaeche laufen sie ins Leere.
//----------------------------------------------------------------------------
#include "Tr3Quelle.h"
#include <sys/stat.h>
#include <direct.h>
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

        bool laden(const wchar_t* p)
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

bool Tr3Quelle::holeMoveable(const std::wstring& levelPfad, int objektId,
                             std::vector<uint8_t>& paket, std::string& fehler)
{
    fehler.clear();
    paket.clear();
    const int suche = objektId;

    Leser r;
    if (!r.laden(levelPfad.c_str())) { fehler = "TR3-Level nicht lesbar"; return false; }

    const uint32_t version = r.u32();
    if ((version >> 24) != 0xFF) { fehler = "Datei ist kein TR3-Level"; return false; }

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
    if (!nachRaeumen) { fehler = "Raumdurchlauf gescheitert - unerwarteter Raumkopf"; return false; }
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
    if (r.kaputt) { fehler = "Datei endet vor der Moveable-Tabelle"; return false; }

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
    if (!z) { fehler = "Objekt " + std::to_string(suche) + " ist in diesem Level nicht vorhanden"; return false; }

    uint32_t animEnde = nAnims;
    for (const Moveable& m : mv)
        if (m.animIndex > z->animIndex && m.animIndex < animEnde) animEnde = m.animIndex;
    const uint32_t animZahl = animEnde - z->animIndex;

    std::printf("Objekt %d: %u Meshes, %u Knochen, Anims %u..%u (%u)\n",
                suche, z->numMeshes, z->numMeshes ? z->numMeshes - 1 : 0,
                z->animIndex, animEnde - 1, animZahl);

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
        if (nVerts > 255) { fehler = "Ein Mesh hat mehr als 255 Vertices - mehr liest TR5 nicht"; return false; }
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
    for (uint32_t i = 0; i < animZahl; ++i)
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
    put32(out, animZahl);
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
    // kippen grosse Werte ganz. Betroffen sind genau die EINACHSGELENKE:
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

        for (uint32_t a = 0; a < animZahl; ++a)
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
            { fehler = "Eine Texturkachel liegt ausserhalb der Texturseiten"; return false; }
            const size_t at = tex16At + size_t(k) * 131072;
            out.insert(out.end(), r.b.begin() + at, r.b.begin() + at + 131072);
        }
        std::printf("  Texturteil: %d Eintraege, %zu Kacheln a 131072 Byte\n",
                    geschrieben, kacheln.size());
    }

    // Frueher wurde hier eine Datei geschrieben. Als Bibliothek geben wir das
    // Paket zurueck - der Aufrufer entscheidet, ob es auf die Platte geht
    // (Werkzeug) oder direkt weiterverarbeitet wird (Randomizer-Lauf).
    paket = out;

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
                animZahl, frameBytes, z->frameOffset, frameEnde,
                z->numMeshes ? (z->numMeshes - 1) * 4 : 0);
    std::printf("  Frame-Zeiger ausserhalb der Scheibe: %d   Sprungziele %u..%u (Scheibe %u..%u)\n",
                ausserhalb, animMin, animMax, z->animIndex, animEnde - 1);
    if (ausserhalb) std::printf("  ACHTUNG: Frame-Zeiger zeigen aus der Scheibe heraus\n");
    if (animMin < z->animIndex || animMax >= animEnde)
        std::printf("  ACHTUNG: eine Anim springt aus der eigenen Scheibe heraus\n");
    std::printf("  Paket fertig: %zu Byte\n", paket.size());
    return true;
}

//----------------------------------------------------------------------------
// Klangentnahme aus MAIN.SFX (#92, aus TR3Ton uebernommen).
//----------------------------------------------------------------------------
namespace
{
    // MAIN.SFX ist eine Kette aneinandergehaengter RIFF-WAVs. Ein Verzeichnis
    // gibt es nicht - man laeuft durch und merkt sich Anfang und Laenge jedes
    // Blocks. Ungerade Laengen sind auf gerade aufgefuellt.
    bool wellenVerzeichnis(const std::vector<uint8_t>& b,
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

bool Tr3Quelle::holeKlaenge(const std::wstring& sfxPfad,
                            const std::vector<KlangWunsch>& wuensche,
                            std::vector<uint8_t>& paket,
                            std::string& fehler)
{
    fehler.clear();
    paket.clear();

    Leser sfx;
    if (!sfx.laden(sfxPfad.c_str())) { fehler = "MAIN.SFX nicht lesbar"; return false; }

    std::vector<uint32_t> at, len;
    if (!wellenVerzeichnis(sfx.b, at, len))
    { fehler = "In MAIN.SFX steht kein einziges RIFF - falsche Datei?"; return false; }

    // Paketkopf: Kennung, Version 2, Anzahl (wird am Ende nachgetragen).
    std::vector<uint8_t> out;
    out.push_back('T'); out.push_back('3'); out.push_back('5'); out.push_back('S');
    for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 2 : 0);
    for (int i = 0; i < 4; ++i) out.push_back(0);

    for (const KlangWunsch& k : wuensche)
    {
        if (k.wellen.empty())
        { fehler = "Klang " + std::to_string(k.tr3Id) + " ohne Wellenangabe"; return false; }

        for (int w : k.wellen)
            if (w < 1 || size_t(w) > at.size())
            {
                fehler = "Wave " + std::to_string(w) + " gibt es in dieser MAIN.SFX nicht ("
                       + std::to_string(at.size()) + " vorhanden)";
                return false;
            }

        // Kopf je Klang: ID, dann drei Woerter, die der Einsetzer neu bildet
        // (Lautstaerke, Chance, Merkmale - eine Vorlage wuerde Tonhoehe und
        // Zufallsverstimmung mitschleppen, siehe Tr5Tr3Import.cpp).
        put16(out, uint16_t(k.tr3Id)); put16(out, 0); put16(out, 0); put16(out, 0);
        put16(out, uint16_t(k.wellen.size()));

        for (int w : k.wellen)
        {
            const size_t i = size_t(w) - 1;          // TRsound zaehlt ab 1
            put32(out, len[i]);
            out.insert(out.end(), sfx.b.begin() + at[i], sfx.b.begin() + at[i] + len[i]);
        }
    }

    out[8]  = uint8_t(wuensche.size() & 0xFF);
    out[9]  = uint8_t((wuensche.size() >> 8) & 0xFF);
    paket = out;
    return true;
}

//----------------------------------------------------------------------------
// SFX-Nummern der Anim-Kommandos umbiegen (#92, aus TR3Setz uebernommen).
//
// ZWEI FALLEN STECKEN HIER DRIN, beide teuer bezahlt:
//
// 1. "Anzahl" im Anim-Kopf zaehlt KOMMANDOS, nicht Worte. Ein Kommando belegt
//    je nach Art 1 bis 4 Worte. Wer die Schleife bis "Start + Anzahl" mit dem
//    WORTindex laufen laesst, sieht nur die ersten Kommandos und merkt nichts
//    davon - die Meldung ueber offene Nummern kann nur melden, was sie gelesen
//    hat. Beim Compsognathus blieb so die zweite Fundstelle von 243 stehen,
//    und 243 ist in TR5 SFX_GENERIC_HEAVY_FEET.
//
// 2. Nur die Kommandos anfassen, auf die die Animationen DIESES Objekts
//    zeigen. Das Paket fuehrt den ganzen Kommandoblock des Quelllevels mit.
//----------------------------------------------------------------------------
bool Tr3Quelle::biegeSfxUm(std::vector<uint8_t>& paket,
                           const std::vector<std::pair<int,int>>& zuordnung,
                           const std::vector<int>& stumm,
                           int stummPlatz,
                           std::vector<int>& offen,
                           std::string& fehler)
{
    fehler.clear();
    offen.clear();

    if (paket.size() < 48) { fehler = "Modellpaket zu klein"; return false; }

    const uint32_t nMeshes = ru32(paket, 12), nAnims = ru32(paket, 16);
    const uint32_t nFrameW = ru32(paket, 20), nBoneW = ru32(paket, 24);
    const uint32_t nSC = ru32(paket, 28), nAD = ru32(paket, 32), nCmd = ru32(paket, 36);

    size_t p = 48;
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > paket.size()) { fehler = "Modellpaket bricht in den Meshes ab"; return false; }
        p += 4 + ru32(paket, p);
    }
    p += size_t(nBoneW) * 4 + size_t(nFrameW) * 2 + size_t(nAnims) * 40
       + size_t(nSC) * 6 + size_t(nAD) * 8;

    const size_t cmdAt = p;
    if (cmdAt + size_t(nCmd) * 2 > paket.size())
    { fehler = "Kommandoblock im Modellpaket nicht auffindbar"; return false; }
    const size_t animAt = cmdAt - size_t(nAD) * 8 - size_t(nSC) * 6 - size_t(nAnims) * 40;

    // Stummschaltung ist nur eine weitere Zuordnung - auf einen leeren Platz
    // der Soundmap, wo die Engine aussteigt statt etwas Fremdes zu spielen.
    std::vector<std::pair<int,int>> alle = zuordnung;
    for (int s : stumm) alle.push_back(std::make_pair(s, stummPlatz));

    for (uint32_t a = 0; a < nAnims; ++a)
    {
        const size_t ao = animAt + size_t(a) * 40;
        const uint16_t anzahl = ru16(paket, ao + 36);
        if (!anzahl) continue;
        const uint16_t start = ru16(paket, ao + 38);

        uint32_t i = start;
        for (uint16_t k = 0; k < anzahl && i < nCmd; ++k)      // KOMMANDOS zaehlen
        {
            const uint16_t art = ru16(paket, cmdAt + size_t(i) * 2);
            ++i;
            int folge = 0;
            switch (art)
            {
            case 1: folge = 3; break;    // SetPosition
            case 2: folge = 2; break;    // JumpVelocity
            case 5: folge = 2; break;    // PlaySound: Frame + SFX
            case 6: folge = 2; break;    // Flipeffect
            default: folge = 0; break;
            }

            if (art == 5 && i + 1 < nCmd)
            {
                const size_t stelle = cmdAt + size_t(i + 1) * 2;
                const uint16_t wort = ru16(paket, stelle);
                const int nr = wort & 0x0FFF;    // Bits 12-15 sind Bedingungen
                bool getroffen = false;
                for (const auto& z : alle)
                    if (z.first == nr)
                    {
                        const uint16_t neu = uint16_t((wort & 0xF000) | (z.second & 0x0FFF));
                        std::memcpy(&paket[stelle], &neu, 2);
                        getroffen = true;
                        break;
                    }
                if (!getroffen)
                {
                    bool schon = false;
                    for (int o : offen) if (o == nr) { schon = true; break; }
                    if (!schon) offen.push_back(nr);
                }
            }
            i += folge;
        }
    }
    return true;
}

//----------------------------------------------------------------------------
// Die vier Gaeste aus Tomb Raider 3 (#92).
//
// Alle Zahlen sind GEMESSEN, nicht angenommen:
//   - Objektnummern aus der tomb3-Enum, gegengeprueft (VULTURE steht dort auf
//     29 und passt zur Notiz des Nutzers)
//   - Zielslots mit MissMod an allen 16 TR5-Leveln geprueft: keiner traegt
//     dort ein Modell
//   - Wave-Nummern im Werkzeug TRsound abgehoert und aus den fertigen
//     Klangpaketen zurueckgewonnen, indem die Wellendaten in MAIN.SFX
//     wiedergefunden wurden
//   - Lautstaerken vom Nutzer im Spiel eingestellt
//----------------------------------------------------------------------------
std::vector<Tr3Quelle::DinoQuelle> Tr3Quelle::dinos()
{
    std::vector<DinoQuelle> d;

    {   // T-Rex auf SAS. Der Laufklang ist die Ausnahme: die Anim-Kommandos
        // rufen 88, der im Spiel gehoerte Klang ist aber der dumpfe,
        // zweivariantige von 250 - vom Nutzer am Original gegengehoert.
        DinoQuelle t;
        t.slot = 31; t.name = "T-Rex"; t.quelle = "tr3";
        t.datei = "CRASH.TR2"; t.objekt = 287; t.klangBasis = 445;
        t.klaenge = { { 84, {87} }, { 87, {89} }, { 88, {256,257} },
                      { 89, {91} }, { 255, {262} } };
        t.stumm = { 143 };                 // TR3 "Falling", in TR5 belegt
        t.laut  = { 100, 130, 150, 100, 140 };
        d.push_back(t);
    }
    {   // Raptor auf REAPER (war eine Wasserqualle).
        DinoQuelle t;
        t.slot = 75; t.name = "Raptor"; t.quelle = "tr3";
        t.datei = "CRASH.TR2"; t.objekt = 288; t.klangBasis = 438;
        t.klaenge = { { 250, {256,257} }, { 251, {258} }, { 252, {259} },
                      { 253, {260} }, { 254, {261} }, { 255, {262} }, { 256, {263} } };
        t.stumm = { 143 };
        t.laut  = { 100, 110, 110, 110, 130, 120, 120 };
        d.push_back(t);
    }
    {   // Compsognathus auf SAILOR (war ein Matrose ohne Modell).
        DinoQuelle t;
        t.slot = 53; t.name = "Compy"; t.quelle = "tr3";
        t.datei = "CRASH.TR2"; t.objekt = 34; t.klangBasis = 430;
        t.klaenge = { { 239, {242} }, { 240, {243} }, { 241, {244} },
                      { 242, {245} }, { 243, {246,247} } };
        t.laut  = { 100, 100, 100, 110, 70 };
        d.push_back(t);
    }
    {   // Geier auf CROW. Modell aus The Lost Artifact, Klaenge aus TR3 -
        // siehe sfxPfad().
        DinoQuelle t;
        t.slot = 43; t.name = "Geier"; t.quelle = "tr3gold";
        t.datei = "chunnel.tr2"; t.objekt = 29; t.klangBasis = 420;
        t.klaenge = { { 182, {192} }, { 183, {193,194} }, { 184, {195} }, { 185, {196} } };
        t.stumm = { 56 };                  // TR3: Laras Schritt, in TR5 belegt
        t.laut  = { 100, 100, 100, 100 };
        d.push_back(t);
    }
    return d;
}

std::wstring Tr3Quelle::sfxPfad(const DinoQuelle&,
                                const std::wstring& tr3Dir, const std::wstring&)
{
    // IMMER die MAIN.SFX von Tomb Raider 3 - auch fuer den Geier. Gold bringt
    // eine eigene mit, aber die abgehoerten Wave-Nummern beziehen sich auf
    // die von TR3. Der Parameter fuer Gold bleibt in der Schnittstelle, damit
    // die Absicht sichtbar ist und niemand spaeter "aufraeumt".
    return tr3Dir + L"\\MAIN.SFX";
}

std::wstring Tr3Quelle::levelPfad(const DinoQuelle& d,
                                  const std::wstring& tr3Dir, const std::wstring& tr3GoldDir)
{
    const std::wstring& basis = (std::strcmp(d.quelle, "tr3gold") == 0) ? tr3GoldDir : tr3Dir;
    std::wstring name;
    for (const char* p = d.datei; *p; ++p) name.push_back(wchar_t((unsigned char)*p));
    return basis + L"\\" + name;
}

//----------------------------------------------------------------------------
// Texturworte der Faces auf die neuen Texinfos umbiegen (#92, aus TR3Setz).
//
// Gleicher Ableser wie ueberall: Meshkopf 10 Byte, dann das Vertexwort;
// 0 Normalen heisst NORMALEN. Bit 15 des Texturworts ist ein Flag und bleibt
// stehen - es entscheidet, ob AdjustUVs die Halbtexel-Korrektur laufen laesst.
//----------------------------------------------------------------------------
int Tr3Quelle::biegeTexUm(std::vector<uint8_t>& paket, const std::map<int,int>& zuordnung)
{
    const uint32_t nMeshes = ru32(paket, 12);
    size_t p = 48;
    int getauscht = 0;

    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > paket.size()) return -1;
        const uint32_t len = ru32(paket, p); p += 4;
        const size_t ende = p + len;
        size_t q = p + 10;
        if (q + 2 > ende) return -1;

        const int16_t vw = rs16(paket, q); q += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) q += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            q += size_t(nv) * 6;
            const int16_t nn = rs16(paket, q); q += 2;
            q += size_t(nv) * (nn >= 0 ? 6 : 2);
        }

        auto biege = [&](size_t at) {
            const uint16_t w = ru16(paket, at);
            std::map<int,int>::const_iterator f = zuordnung.find(w & 0x7FFF);
            if (f == zuordnung.end()) return;
            const uint16_t neu = uint16_t((w & 0x8000) | (f->second & 0x7FFF));
            std::memcpy(&paket[at], &neu, 2);
            ++getauscht;
        };

        const int16_t nq = rs16(paket, q); q += 2;
        for (int k = 0; k < nq; ++k, q += 12) biege(q + 8);
        const int16_t nt = rs16(paket, q); q += 2;
        for (int k = 0; k < nt; ++k, q += 10) biege(q + 6);

        p = ende;
    }
    return getauscht;
}

//----------------------------------------------------------------------------
// Zwischenlager fuer die entnommenen Pakete (#103).
//----------------------------------------------------------------------------
namespace
{
    bool dateiLesen(const std::wstring& p, std::vector<uint8_t>& b)
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

    bool dateiSchreiben(const std::wstring& p, const std::vector<uint8_t>& b)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, p.c_str(), L"wb") != 0 || !f) return false;
        const size_t g = std::fwrite(b.data(), 1, b.size(), f);
        std::fclose(f);
        return g == b.size();
    }

    // Kennzeichen der Quelldatei: Groesse und Aenderungszeit. Beides zusammen
    // reicht, um eine neu installierte oder gepatchte Fassung zu erkennen -
    // und es kostet keinen Durchlauf durch 11 MB wie eine Pruefsumme.
    std::string quellKennung(const std::wstring& pfad)
    {
        struct _stat64 s;
        if (_wstat64(pfad.c_str(), &s) != 0) return std::string();
        return std::to_string((long long)s.st_size) + "-" + std::to_string((long long)s.st_mtime);
    }

    std::wstring lagerName(const std::wstring& dir, const char* name, const char* teil)
    {
        std::wstring w = dir + L"\\";
        for (const char* p = name; *p; ++p) w.push_back(wchar_t((unsigned char)*p));
        w += L"_";
        for (const char* p = teil; *p; ++p) w.push_back(wchar_t((unsigned char)*p));
        return w + L".bin";
    }
}

bool Tr3Quelle::holeMitLager(const DinoQuelle& d,
                             const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                             const std::wstring& lagerDir,
                             std::vector<uint8_t>& modell, std::vector<uint8_t>& klaenge,
                             std::string& fehler)
{
    const std::wstring lvlPfad = levelPfad(d, tr3Dir, tr3GoldDir);
    const std::wstring sfx     = sfxPfad(d, tr3Dir, tr3GoldDir);

    // Erwartete Kennung: beide Quelldateien zusammen. Aendert sich eine, ist
    // der Eintrag hinfaellig.
    const std::string kennung = quellKennung(lvlPfad) + "|" + quellKennung(sfx);

    const std::wstring mDatei = lagerName(lagerDir, d.name, "modell");
    const std::wstring kDatei = lagerName(lagerDir, d.name, "klaenge");
    const std::wstring nDatei = lagerName(lagerDir, d.name, "kennung");

    if (!lagerDir.empty() && !kennung.empty())
    {
        std::vector<uint8_t> alt;
        if (dateiLesen(nDatei, alt))
        {
            const std::string war(alt.begin(), alt.end());
            if (war == kennung && dateiLesen(mDatei, modell) && dateiLesen(kDatei, klaenge))
            {
                std::printf("  %s: aus dem Zwischenlager\n", d.name);
                return true;
            }
        }
    }

    modell.clear();
    klaenge.clear();
    if (!holeMoveable(lvlPfad, d.objekt, modell, fehler))
    { fehler = std::string(d.name) + ": " + fehler; return false; }
    if (!holeKlaenge(sfx, d.klaenge, klaenge, fehler))
    { fehler = std::string(d.name) + " (Klaenge): " + fehler; return false; }

    // Ablegen ist Kuer: schlaegt es fehl (schreibgeschuetzter Ordner, volle
    // Platte), laeuft der Durchgang trotzdem - er ist eben beim naechsten Mal
    // wieder langsam. Ein Zwischenlager darf nie der Grund sein, dass etwas
    // nicht geht.
    if (!lagerDir.empty() && !kennung.empty())
    {
        // Ordner anlegen, falls er fehlt. Frueher stand das beim Aufrufer -
        // und die Kommandozeile hatte es nicht, weshalb jedes Ablegen still
        // scheiterte und das Lager leer blieb. Eine Funktion, die einen
        // Ordner benutzt, soll ihn auch anlegen koennen.
        _wmkdir(lagerDir.c_str());
        dateiSchreiben(mDatei, modell);
        dateiSchreiben(kDatei, klaenge);
        dateiSchreiben(nDatei, std::vector<uint8_t>(kennung.begin(), kennung.end()));
    }
    return true;
}

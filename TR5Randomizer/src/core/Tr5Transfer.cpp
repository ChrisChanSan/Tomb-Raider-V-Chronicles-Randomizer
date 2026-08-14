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
// Modelltransfer zwischen TR5-Leveln: Tr5Level::copyMoveableFrom.
//
// Das ist das Schluesselstueck fuer drei Dinge zugleich - Waffen in allen
// Leveln, zufaellige Outfits und den Cross-Gegnermix. Es lag bisher nur im
// Werkstattwerkzeug TombItemEdit2026 (vImportFromMVB / -copymvb), und das wird
// nie mitgeliefert: Spieldaten duerfen das Haus nicht verlassen, also muss der
// Transfer zur Laufzeit auf den Dateien des Nutzers passieren.
//
// Jede Formatangabe hier ist am Motor abgelesen, nicht an Dokumentation:
//
//   Kopf              specific/file.cpp:1339 - version u32, RT/OT/BT je u16
//   Texturbloecke     LoadTextures (:416) - 32-Bit- und 16-Bit-Block, je
//                     u32 unkomprimiert + u32 komprimiert + zlib-Strom;
//                     die Engine liest je nach Bildschirmformat NUR EINEN
//                     davon, beide muessen also synchron wachsen
//   Seitenordnung     Raum (RT), dann Objekt (OT), dann Bump (BT); Index 0
//                     ist reserviert, nutzbar 1..RT+OT+BT, Grenze 256 (:438)
//   Bump              Textures[n].bumptpage = n + BT/2 (:593) - reine
//                     Laufzeitarithmetik. Neue Seiten am OT-Ende einfuegen
//                     ist sicher, solange alle GESPEICHERTEN Kachelnummern
//                     >= RT+OT um die Einfuegezahl steigen
//   Texinfo           40 Byte, Kachel als u16 bei Offset 2, 0-basiert in den
//                     Bits 0-14, Bit 15 ist Flag (LoadTextureInfos :1093)
//   Sprites           16 Byte, Kachel als u16 bei Offset 0, 0-basiert,
//                     Lader addiert +1 (:903)
//   Mesh              drawroom.cpp ProcessMeshData (:740): x,y,z,r,flags,
//                     nVerts-Wort; Vertices 3 Shorts; nNorms-Short, dann
//                     Normalen (3 je Vertex) oder Prelight (1 je Vertex);
//                     ngt4 + Quads zu 6 Shorts {v0..v3, texinfo, attr};
//                     ngt3 + Tris zu 5 Shorts. Face-Texinfo maskiert die
//                     Engine mit & 0x7FFF (output.cpp:1575)
//   Meshzeiger        Byteoffsets in den Mesh-Datenblock (file.cpp:775)
//   Anims             40 Byte; frame_ptr ist ein BYTEOFFSET in den
//                     Frame-Block (Fixup :810); jump_anim, change_index und
//                     command_index sind levelweit absolut; frame_base und
//                     frame_end sind logische Nummern und bleiben unberuehrt
//   Objekteintrag     slot u32, nmeshes s16, mesh_index s16 (Zeigerindex,
//                     die Verdopplung passiert erst beim Laden :835),
//                     bone_index s32, frame_offset u32, anim_index u16
//                     (0xFFFF = keiner), 2 Byte Polster (:814)
//
// Drei Regeln aus der Waffenarbeit (#58), dort teuer gelernt:
//   1. Slot 0 (LARA) niemals kopieren - er traegt die Animationen des Levels.
//   2. Mesh-Offset 0 ist KEIN Nullzeiger; dort liegt Laras rechte Hand.
//      Hier folgenlos, weil Offsets nie als Wahrheitswert gedeutet werden -
//      der Fehler des Werkzeugs wird gar nicht erst nachgebaut.
//   3. Die Kachelnummern muessen vollstaendig nachgezogen werden, sonst
//      zeigt irgendetwas auf die falsche Seite (der verpixelte Gunsmoke).
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
    // Ein Einfuegeplan: alle Einfuegungen erst sammeln, dann in EINEM Durchlauf
    // anwenden. Der Grund steht in Tr5Level.h bei der Blockkarte: jede einzelne
    // Einfuegung wuerde alle gemerkten Offsets entwerten, und neun Einfuegungen
    // mit je einer Handkorrektur aller Offsets sind neun Gelegenheiten, eine zu
    // vergessen.
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
    // Die drei Texturbloecke am Dateianfang. Jeder: u32 unkomprimiert,
    // u32 komprimiert, dann der zlib-Strom.
    //------------------------------------------------------------------------
    struct TexHeader
    {
        int rt = 0, ot = 0, bt = 0;
        size_t block32At = 0;                // Position des ersten u32
        size_t block16At = 0;
        size_t blockMiscAt = 0;
        size_t afterBlocksAt = 0;            // erste Position hinter Block 3
    };

    bool parseTexHeader(const std::vector<uint8_t>& b, TexHeader& h, std::string& err)
    {
        if (b.size() < 10 + 8) { err = "Datei zu kurz fuer den Texturkopf"; return false; }
        h.rt = rd16(b, 4); h.ot = rd16(b, 6); h.bt = rd16(b, 8);

        size_t p = 10;
        h.block32At = p; p += 8 + rd32(b, p + 4);
        if (p + 8 > b.size()) { err = "32-Bit-Block passt nicht zur Datei"; return false; }
        h.block16At = p; p += 8 + rd32(b, p + 4);
        if (p + 8 > b.size()) { err = "16-Bit-Block passt nicht zur Datei"; return false; }
        h.blockMiscAt = p; p += 8 + rd32(b, p + 4);
        if (p > b.size()) { err = "dritter Texturblock passt nicht zur Datei"; return false; }
        h.afterBlocksAt = p;
        return true;
    }

    bool inflateBlock(const std::vector<uint8_t>& b, size_t at, std::vector<uint8_t>& raus, std::string& err)
    {
        const uint32_t un = rd32(b, at), comp = rd32(b, at + 4);
        raus.resize(un);
        uLongf dstLen = un;
        const int rc = uncompress(raus.data(), &dstLen, b.data() + at + 8, comp);
        if (rc != Z_OK || dstLen != un) { err = "Texturblock laesst sich nicht entpacken"; return false; }
        return true;
    }

    void deflateBlock(const std::vector<uint8_t>& roh, std::vector<uint8_t>& raus)
    {
        uLongf grenze = compressBound(static_cast<uLong>(roh.size()));
        std::vector<uint8_t> puffer(grenze);
        compress2(puffer.data(), &grenze, roh.data(), static_cast<uLong>(roh.size()), Z_BEST_COMPRESSION);
        raus.clear();
        push32(raus, static_cast<uint32_t>(roh.size()));
        push32(raus, static_cast<uint32_t>(grenze));
        raus.insert(raus.end(), puffer.begin(), puffer.begin() + grenze);
    }

    //------------------------------------------------------------------------
    // Laenge eines Meshes in Bytes, exakt der Ableser von ProcessMeshData.
    //------------------------------------------------------------------------
    bool meshSize(const std::vector<uint8_t>& b, size_t at, size_t limit, size_t& bytes, std::string& err)
    {
        size_t p = at;
        auto s16 = [&](int16_t& v) -> bool {
            if (p + 2 > limit) return false;
            std::memcpy(&v, b.data() + p, 2); p += 2; return true;
        };
        int16_t w;
        for (int i = 0; i < 5; ++i) if (!s16(w)) { err = "Mesh-Kopf abgeschnitten"; return false; }
        int16_t vertsWord; if (!s16(vertsWord)) { err = "Mesh-Kopf abgeschnitten"; return false; }
        const int nVerts = vertsWord & 0xFF;

        if (nVerts == 0)
        {
            // Leeres Mesh: ProcessMeshData ueberspringt (6*num)+1 Shorts,
            // num steht im Hochbyte (drawroom.cpp:790/869).
            const int num = (vertsWord >> 8) & 0xFF;
            p += (static_cast<size_t>(num) * 6 + 1) * 2;
        }
        else
        {
            p += static_cast<size_t>(nVerts) * 6;             // Vertices
            int16_t nNorms; if (!s16(nNorms)) { err = "Mesh-Normalen abgeschnitten"; return false; }
            // ACHTUNG, null heisst NORMALEN: ProcessMeshData setzt
            // "if (!nNorms) nNorms = nVerts" VOR der Verzweigung
            // (drawroom.cpp:833) - nur NEGATIVE Werte bedeuten Prelight.
            // Wer 0 als Prelight deutet, kopiert das Mesh verkuerzt, und die
            // Faces dahinter sind Muell. So sah joby2s Zopf nach dem Kopieren
            // aus: ein Spiess durch den Koerper, waehrend Andrea1s Haar ohne
            // solche Meshes heil blieb.
            if (nNorms >= 0) p += static_cast<size_t>(nVerts) * 6;  // Normalen, 3 je Vertex
            else             p += static_cast<size_t>(nVerts) * 2;  // Prelight, 1 je Vertex
        }

        int16_t ngt4; if (!s16(ngt4)) { err = "Mesh-Quads abgeschnitten"; return false; }
        p += static_cast<size_t>(ngt4) * 12;
        int16_t ngt3; if (!s16(ngt3)) { err = "Mesh-Tris abgeschnitten"; return false; }
        p += static_cast<size_t>(ngt3) * 10;

        if (p > limit) { err = "Mesh laeuft aus dem Block heraus"; return false; }
        bytes = p - at;
        return true;
    }

    // Ruft fn(faceTexOffset) fuer jedes Face-Texturwort eines Meshes.
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
            // 0 heisst Normalen, siehe meshSize.
            p += static_cast<size_t>(nVerts) * (nNorms >= 0 ? 6 : 2);
        }

        const int16_t ngt4 = rs16(b, p); p += 2;
        for (int i = 0; i < ngt4; ++i, p += 12) fn(p + 8);    // Wort 4 von 6
        const int16_t ngt3 = rs16(b, p); p += 2;
        for (int i = 0; i < ngt3; ++i, p += 10) fn(p + 6);    // Wort 3 von 5
        if (p > limit) { err = "Mesh-Faces laufen aus dem Block heraus"; return false; }
        return true;
    }

    // Laenge eines Anim-Befehls in Shorts inkl. Opcode (game/control.cpp,
    // AnimateItem-Befehlsauswertung; dieselben Werte nutzt DumpAnim).
    int commandLen(int16_t typ)
    {
        switch (typ)
        {
        case 1: return 4;      // SetPosition, 3 Parameter
        case 2: return 3;      // JumpVelocity, 2
        case 3: return 1;      // EmptyHands
        case 4: return 1;      // KillItem
        case 5: return 3;      // PlaySound, Frame + SFX
        case 6: return 3;      // Flipeffect, Frame + Effekt
        default: return 1;     // unbekannt: nur den Opcode schlucken
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

bool Tr5Level::copyMoveableFrom(const Tr5Level& src, int slotId, std::string& error,
                                int dstSlot, bool nurMeshes)
{
    char buf[256];

    // Unter welchem Slot die Kopie im Ziel steht. Abweichend nur fuer die
    // Ausweichslots des Cross-Gegnermix (Spender-MESHSWAP1 -> 399-403).
    const int zielSlot = (dstSlot < 0) ? slotId : dstSlot;

    if (slotId == 0 || zielSlot == 0)
    {
        // Regel 1 aus der Waffenarbeit. Der Meshtree von Slot 0 ist in allen
        // Leveln identisch, der Koerper kommt aus LARA_SKIN (8) - kopieren
        // braechte nichts und naehme dem Level seine Animationen.
        error = "Slot 0 (LARA) wird nicht kopiert - er traegt die Animationen des Levels";
        return false;
    }

    MoveableInfo mv;
    if (!src.moveableInfo(slotId, mv))
    {
        std::snprintf(buf, sizeof(buf), "Quell-Level kennt Slot %d nicht", slotId);
        error = buf;
        return false;
    }
    if (mv.nmeshes <= 0 || mv.meshIndex < 0 ||
        mv.meshIndex + mv.nmeshes > src.m_meshPtrs.count)
    { error = "Quellslot hat unplausible Meshangaben"; return false; }

    //========================================================================
    // Phase A: alles aus der Quelle einsammeln. Bis hierher wird NICHTS
    // veraendert - scheitert eine Pruefung, bleibt das Ziel unberuehrt.
    //========================================================================

    // --- Scheiben abgrenzen: das Ende meiner Daten ist der Anfang der ------
    // --- Daten des naechsten Slots (dasselbe Prinzip wie beim Auslesen  ------
    // --- der Animationen: ein Slot kennt seine eigene Anzahl nicht).    ------
    std::vector<MoveableInfo> alle;
    for (int s : src.moveableSlots())
    {
        MoveableInfo m;
        if (src.moveableInfo(s, m)) alle.push_back(m);
    }
    auto nextStart = [&](long long mine, long long blockEnd, auto wert) -> long long {
        long long best = blockEnd;
        for (const MoveableInfo& m : alle)
        {
            const long long w = wert(m);
            if (w > mine && w < best) best = w;
        }
        return best;
    };

    // nurMeshes: Anim-, Frame- und Bone-Scheiben bewusst leer lassen -
    // siehe die Begruendung am Prototyp in Tr5Level.h (MESHSWAP1 traegt
    // einen anim_index ohne eigene Animationen).
    const long long boneStart = nurMeshes ? 0 : mv.boneIndex;
    const long long boneEnd = nurMeshes ? 0 : nextStart(boneStart, src.m_bones.count,
                                        [](const MoveableInfo& m) { return static_cast<long long>(m.boneIndex); });

    const long long frameStart = nurMeshes ? 0 : mv.frameOffset;
    const long long frameEnd = nurMeshes ? 0 : nextStart(frameStart, static_cast<long long>(src.m_frames.count) * 2,
                                         [](const MoveableInfo& m) { return static_cast<long long>(m.frameOffset); });

    long long animStart = -1, animEnd = -1;
    if (!nurMeshes && mv.animIndex >= 0)
    {
        animStart = mv.animIndex;
        animEnd = nextStart(animStart, src.m_anims.count,
                            [](const MoveableInfo& m) { return static_cast<long long>(m.animIndex); });
        if (animEnd > src.m_anims.count) { error = "Anim-Scheibe unplausibel"; return false; }
    }

    // --- Animationen samt Anhang -------------------------------------------
    std::vector<uint8_t> animBytes, changeBytes, rangeBytes, cmdBytes, frameBytes, boneBytes;
    long long chgStart = 0, chgEnd = 0, rngStart = 0, rngEnd = 0, cmdStart = 0, cmdEnd = 0;
    std::set<int> gebrauchteSfx;

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
                // Befehle sind variabel lang - die Liste muss abgelaufen
                // werden, um ihr Ende zu kennen. PlaySound-Befehle werden
                // dabei gleich fuer die Sample-Uebernahme eingesammelt.
                size_t c = src.m_commands.dataAt + static_cast<size_t>(iCmd) * 2;
                long long lauf = iCmd;
                for (int k = 0; k < nCmd; ++k)
                {
                    const int16_t typ = rs16(src.m_bytes, c);
                    if (typ == 5)
                        gebrauchteSfx.insert(rs16(src.m_bytes, c + 4) & 0x3FFF);
                    const int len = commandLen(typ);
                    c += static_cast<size_t>(len) * 2;
                    lauf += len;
                }
                cmdStart = std::min<long long>(cmdStart, iCmd);
                cmdEnd = std::max<long long>(cmdEnd, lauf);
            }
        }
        if (chgEnd <= chgStart) { chgStart = 0; chgEnd = 0; }
        if (cmdEnd <= cmdStart) { cmdStart = 0; cmdEnd = 0; }

        // Bereiche haengen an den Zustandswechseln
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
        { error = "Anim-Anhang laeuft aus seinen Bloecken heraus"; return false; }

        // Jeder Frame-Zeiger muss in der Frame-Scheibe liegen, BEVOR irgend
        // etwas veraendert wird - sonst rechnet der Rebase spaeter mit einem
        // Unterlauf weiter und niemand merkt es.
        for (long long a = animStart; a < animEnd; ++a)
        {
            const uint32_t fp = rd32(src.m_bytes, src.m_anims.dataAt + static_cast<size_t>(a) * SZ_ANIM);
            if (fp < frameStart || fp >= static_cast<uint64_t>(frameEnd))
            { error = "Frame-Zeiger einer Animation liegt ausserhalb der Frame-Scheibe"; return false; }
        }

        auto scheibe = [&](const Tr5Level::Block& b, long long von, long long bis, int elem, std::vector<uint8_t>& raus) {
            raus.assign(src.m_bytes.begin() + b.dataAt + von * elem,
                        src.m_bytes.begin() + b.dataAt + bis * elem);
        };
        scheibe(src.m_anims, animStart, animEnd, SZ_ANIM, animBytes);
        scheibe(src.m_changes, chgStart, chgEnd, SZ_CHANGE, changeBytes);
        scheibe(src.m_ranges, rngStart, rngEnd, SZ_RANGE, rangeBytes);
        scheibe(src.m_commands, cmdStart, cmdEnd, 2, cmdBytes);
    }

    if (frameEnd > frameStart)
        frameBytes.assign(src.m_bytes.begin() + src.m_frames.dataAt + frameStart,
                          src.m_bytes.begin() + src.m_frames.dataAt + frameEnd);
    if (boneEnd > boneStart)
        boneBytes.assign(src.m_bytes.begin() + src.m_bones.dataAt + boneStart * 4,
                         src.m_bytes.begin() + src.m_bones.dataAt + boneEnd * 4);

    // --- Meshes: Scheiben ueber die Zeigertabelle --------------------------
    // Mehrere Zeiger koennen auf dasselbe Mesh zeigen (die Engine dedupliziert
    // beim Laden, drawroom.cpp:764). Die Zuordnung Quelloffset -> neues Mesh
    // bleibt deshalb erhalten, statt jedes Vorkommen einzeln zu kopieren.
    const size_t srcMeshBytes = static_cast<size_t>(src.m_meshData.count) * 2;
    std::vector<uint32_t> srcPtrs;
    for (int i = 0; i < mv.nmeshes; ++i)
        srcPtrs.push_back(rd32(src.m_bytes, src.m_meshPtrs.dataAt + (static_cast<size_t>(mv.meshIndex) + i) * 4));

    std::map<uint32_t, std::pair<size_t, size_t>> meshRegion;   // Quelloffset -> (Start in meshPack, Laenge)
    std::vector<uint8_t> meshPack;
    std::set<int> srcTexinfoIdx;

    for (uint32_t ofs : srcPtrs)
    {
        if (meshRegion.count(ofs)) continue;
        if (ofs >= srcMeshBytes) { error = "Meshzeiger ausserhalb des Mesh-Blocks"; return false; }

        size_t sz = 0;
        if (!meshSize(src.m_bytes, src.m_meshData.dataAt + ofs, src.m_meshData.dataAt + srcMeshBytes, sz, error))
            return false;

        std::string faceErr;
        if (!forEachFaceTex(src.m_bytes, src.m_meshData.dataAt + ofs, src.m_meshData.dataAt + ofs + sz,
                            [&](size_t texAt) { srcTexinfoIdx.insert(rd16(src.m_bytes, texAt) & 0x7FFF); },
                            faceErr))
        { error = faceErr; return false; }

        // 4er-Ausrichtung wie im Original - Meshzeiger sind dort durchweg
        // vielfache von 4, und Kosten hat es keine.
        while (meshPack.size() % 4) meshPack.push_back(0);
        meshRegion[ofs] = { meshPack.size(), sz };
        meshPack.insert(meshPack.end(),
                        src.m_bytes.begin() + src.m_meshData.dataAt + ofs,
                        src.m_bytes.begin() + src.m_meshData.dataAt + ofs + sz);
    }

    for (int t : srcTexinfoIdx)
        if (t >= src.m_texinfos.count) { error = "Mesh verweist auf ein Texinfo, das es nicht gibt"; return false; }

    // --- Kacheln der gebrauchten Texinfos ----------------------------------
    std::set<int> srcTiles;
    for (int t : srcTexinfoIdx)
        srcTiles.insert(rd16(src.m_bytes, src.m_texinfos.dataAt + static_cast<size_t>(t) * SZ_TEXINFO + 2) & 0x7FFF);

    TexHeader srcTex, dstTex;
    if (!parseTexHeader(src.m_bytes, srcTex, error)) { error = "Quelle: " + error; return false; }
    if (!parseTexHeader(m_bytes, dstTex, error)) { error = "Ziel: " + error; return false; }

    const int srcPages = srcTex.rt + srcTex.ot + srcTex.bt;
    for (int t : srcTiles)
        if (t >= srcPages) { error = "Texinfo verweist auf eine Seite, die es nicht gibt"; return false; }

    std::vector<uint8_t> src32, src16, dst32, dst16;
    if (!inflateBlock(src.m_bytes, srcTex.block32At, src32, error)) { error = "Quelle: " + error; return false; }
    if (!inflateBlock(src.m_bytes, srcTex.block16At, src16, error)) { error = "Quelle: " + error; return false; }
    if (!inflateBlock(m_bytes, dstTex.block32At, dst32, error)) { error = "Ziel: " + error; return false; }
    if (!inflateBlock(m_bytes, dstTex.block16At, dst16, error)) { error = "Ziel: " + error; return false; }

    //========================================================================
    // Phase B: Texturseiten ins Ziel. Eingefuegt wird am Ende des
    // Objektbereichs (vor den Bump-Seiten); jede Seite, die inhaltsgleich
    // schon da ist, wird wiederverwendet - beim Import eines ganzen
    // Waffensets aus demselben Spender teilen sich die Slots ihre Seiten,
    // und ohne die Pruefung laege jede davon mehrfach in der Datei.
    //========================================================================
    const int einfuegeAb = dstTex.rt + dstTex.ot;      // 0-basierte Kachelnummer
    std::map<int, int> tileRemap;                      // Quellkachel -> Zielkachel
    int neueSeiten = 0;

    for (int t : srcTiles)
    {
        const uint8_t* seite32 = src32.data() + static_cast<size_t>(t) * PAGE32;

        // Nur gegen die Seiten VOR der Einfuegestelle (und die schon neu
        // eingefuegten) pruefen: deren Nummern sind stabil. Ein Treffer im
        // Bump-Bereich dahinter wuerde beim naechsten Einfuegen um eins
        // verrutschen - dann lieber eine Seite doppelt als eine falsche.
        int gefunden = -1;
        const int stabil = einfuegeAb + neueSeiten;
        for (int d = 0; d < stabil; ++d)
            if (std::memcmp(dst32.data() + static_cast<size_t>(d) * PAGE32, seite32, PAGE32) == 0)
            { gefunden = d; break; }

        if (gefunden >= 0) { tileRemap[t] = gefunden; continue; }

        const int neu = einfuegeAb + neueSeiten;
        dst32.insert(dst32.begin() + static_cast<size_t>(neu) * PAGE32, seite32, seite32 + PAGE32);
        const uint8_t* seite16 = src16.data() + static_cast<size_t>(t) * PAGE16;
        dst16.insert(dst16.begin() + static_cast<size_t>(neu) * PAGE16, seite16, seite16 + PAGE16);
        tileRemap[t] = neu;
        ++neueSeiten;
    }

    if (1 + dstTex.rt + dstTex.ot + dstTex.bt + neueSeiten > MAX_PAGES)
    {
        std::snprintf(buf, sizeof(buf),
            "Ziel haette %d Texturseiten - die Engine haelt hoechstens %d (Textures[], file.cpp:438)",
            1 + dstTex.rt + dstTex.ot + dstTex.bt + neueSeiten, MAX_PAGES);
        error = buf;
        return false;
    }

    if (neueSeiten > 0)
    {
        // Datei neu zusammensetzen: Kopf, beide gewachsenen Bloecke, Rest.
        std::vector<uint8_t> b32, b16;
        deflateBlock(dst32, b32);
        deflateBlock(dst16, b16);

        std::vector<uint8_t> neu;
        neu.reserve(m_bytes.size() + b32.size() + b16.size());
        neu.insert(neu.end(), m_bytes.begin(), m_bytes.begin() + 10);
        neu.insert(neu.end(), b32.begin(), b32.end());
        neu.insert(neu.end(), b16.begin(), b16.end());
        neu.insert(neu.end(), m_bytes.begin() + dstTex.blockMiscAt, m_bytes.end());
        m_bytes.swap(neu);

        wr16(m_bytes, 6, static_cast<uint16_t>(dstTex.ot + neueSeiten));

        if (!reparse(error)) { error = "nach dem Seiteneinfuegen: " + error; return false; }

        // Alle GESPEICHERTEN Kachelnummern ab der Einfuegestelle nachziehen -
        // das sind die Texinfos (Bump-Seiten der Raeume) und die Sprites.
        // Bit 15 traegt bei den Texinfos ein Flag und bleibt stehen.
        for (int32_t i = 0; i < m_texinfos.count; ++i)
        {
            const size_t o = m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO + 2;
            const uint16_t w = rd16(m_bytes, o);
            const int tile = w & 0x7FFF;
            if (tile >= einfuegeAb)
                wr16(m_bytes, o, static_cast<uint16_t>((w & 0x8000) | (tile + neueSeiten)));
        }
        for (int32_t i = 0; i < m_sprites.count; ++i)
        {
            const size_t o = m_sprites.dataAt + static_cast<size_t>(i) * SZ_SPRITE;
            const uint16_t w = rd16(m_bytes, o);
            if (w >= einfuegeAb)
                wr16(m_bytes, o, static_cast<uint16_t>(w + neueSeiten));
        }
    }

    //========================================================================
    // Phase C: Texinfos anlegen (mit Wiederverwendung), Faces ummappen,
    // alle Bloecke in einem Schlag anhaengen.
    //========================================================================
    std::map<int, int> texinfoRemap;
    std::vector<uint8_t> neueTexinfos;
    int texinfoNeu = 0;

    for (int t : srcTexinfoIdx)
    {
        uint8_t rec[SZ_TEXINFO];
        std::memcpy(rec, src.m_bytes.data() + src.m_texinfos.dataAt + static_cast<size_t>(t) * SZ_TEXINFO, SZ_TEXINFO);
        uint16_t w; std::memcpy(&w, rec + 2, 2);
        const uint16_t neuW = static_cast<uint16_t>((w & 0x8000) | tileRemap[w & 0x7FFF]);
        std::memcpy(rec + 2, &neuW, 2);

        // Gibt es den Eintrag schon? Beim zweiten Slot desselben Spenders ja.
        int gefunden = -1;
        for (int32_t i = 0; i < m_texinfos.count && gefunden < 0; ++i)
            if (std::memcmp(m_bytes.data() + m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO, rec, SZ_TEXINFO) == 0)
                gefunden = i;
        for (int k = 0; k < texinfoNeu && gefunden < 0; ++k)
            if (std::memcmp(neueTexinfos.data() + static_cast<size_t>(k) * SZ_TEXINFO, rec, SZ_TEXINFO) == 0)
                gefunden = m_texinfos.count + k;

        if (gefunden >= 0) { texinfoRemap[t] = gefunden; continue; }
        texinfoRemap[t] = m_texinfos.count + texinfoNeu;
        neueTexinfos.insert(neueTexinfos.end(), rec, rec + SZ_TEXINFO);
        ++texinfoNeu;
    }

    // Faces der kopierten Meshes ummappen (im meshPack, nicht in der Quelle).
    for (const auto& kv : meshRegion)
    {
        std::string faceErr;
        // meshPack in einen vector-Wrapper heben, damit forEachFaceTex passt.
        const size_t start = kv.second.first, len = kv.second.second;
        if (!forEachFaceTex(meshPack, start, start + len,
                            [&](size_t texAt) {
                                uint16_t w; std::memcpy(&w, meshPack.data() + texAt, 2);
                                const uint16_t neuW = static_cast<uint16_t>((w & 0x8000) | texinfoRemap[w & 0x7FFF]);
                                std::memcpy(meshPack.data() + texAt, &neuW, 2);
                            }, faceErr))
        { error = faceErr; return false; }
    }

    // Rebasen: die kopierten Bloecke bekommen die Deltas des Ziels.
    const int32_t dstMeshBytesAlt = m_meshData.count * 2;
    const int32_t dstPtrAlt = m_meshPtrs.count;
    const int32_t dstAnimAlt = m_anims.count;
    const int32_t dstChgAlt = m_changes.count;
    const int32_t dstRngAlt = m_ranges.count;
    const int32_t dstCmdAlt = m_commands.count;
    const int32_t dstBoneAlt = m_bones.count;
    const int32_t dstFrameBytesAlt = m_frames.count * 2;

    for (size_t o = 0; o + SZ_ANIM <= animBytes.size(); o += SZ_ANIM)
    {
        std::vector<uint8_t>& a = animBytes;
        uint32_t fp; std::memcpy(&fp, a.data() + o, 4);
        fp = static_cast<uint32_t>(dstFrameBytesAlt + (fp - frameStart));
        std::memcpy(a.data() + o, &fp, 4);

        uint16_t v;
        std::memcpy(&v, a.data() + o + 28, 2); v = static_cast<uint16_t>(v - animStart + dstAnimAlt); std::memcpy(a.data() + o + 28, &v, 2);
        std::memcpy(&v, a.data() + o + 32, 2);
        if (v) // nur wenn es Zustandswechsel gibt, traegt der Index Bedeutung
        {
            uint16_t i; std::memcpy(&i, a.data() + o + 34, 2);
            i = static_cast<uint16_t>(i - chgStart + dstChgAlt); std::memcpy(a.data() + o + 34, &i, 2);
        }
        std::memcpy(&v, a.data() + o + 36, 2);
        if (v)
        {
            uint16_t i; std::memcpy(&i, a.data() + o + 38, 2);
            i = static_cast<uint16_t>(i - cmdStart + dstCmdAlt); std::memcpy(a.data() + o + 38, &i, 2);
        }
    }
    for (size_t o = 0; o + SZ_CHANGE <= changeBytes.size(); o += SZ_CHANGE)
    {
        uint16_t n; std::memcpy(&n, changeBytes.data() + o + 2, 2);
        if (!n) continue;
        uint16_t i; std::memcpy(&i, changeBytes.data() + o + 4, 2);
        i = static_cast<uint16_t>(i - rngStart + dstRngAlt); std::memcpy(changeBytes.data() + o + 4, &i, 2);
    }
    for (size_t o = 0; o + SZ_RANGE <= rangeBytes.size(); o += SZ_RANGE)
    {
        uint16_t i; std::memcpy(&i, rangeBytes.data() + o + 4, 2);
        i = static_cast<uint16_t>(i - animStart + dstAnimAlt); std::memcpy(rangeBytes.data() + o + 4, &i, 2);
    }

    std::vector<uint8_t> neuePtrs;
    for (uint32_t ofs : srcPtrs)
        push32(neuePtrs, static_cast<uint32_t>(dstMeshBytesAlt + meshRegion[ofs].first));

    // Der Objekteintrag. Ersetzt oder angehaengt - beides ueber dieselben Bytes.
    // Im nurMeshes-Fall zeigen Bone/Frame/Anim bewusst auf nichts - an einem
    // Mesh-Lager liest die Engine nur mesh_index.
    uint8_t objRec[SZ_OBJ];
    {
        const int32_t s32 = zielSlot;                     std::memcpy(objRec + 0, &s32, 4);
        const int16_t nm = static_cast<int16_t>(mv.nmeshes); std::memcpy(objRec + 4, &nm, 2);
        const int16_t mi = static_cast<int16_t>(dstPtrAlt);  std::memcpy(objRec + 6, &mi, 2);
        const int32_t bi = nurMeshes ? 0 : dstBoneAlt;    std::memcpy(objRec + 8, &bi, 4);
        const uint32_t fo = nurMeshes ? 0
                          : static_cast<uint32_t>(dstFrameBytesAlt + (mv.frameOffset - frameStart));
        std::memcpy(objRec + 12, &fo, 4);
        const uint16_t ai = (nurMeshes || mv.animIndex < 0) ? 0xFFFF
                          : static_cast<uint16_t>(mv.animIndex - animStart + dstAnimAlt);
        std::memcpy(objRec + 16, &ai, 2);
        objRec[18] = 0; objRec[19] = 0;
    }

    bool ersetzt = false;
    for (int32_t i = 0; i < m_objects.count && !ersetzt; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * SZ_OBJ;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot != zielSlot) continue;
        std::memcpy(m_bytes.data() + o, objRec, SZ_OBJ);
        ersetzt = true;
    }

    // Zaehler VOR dem Einfuegen anpassen: die Zaehlerbytes wandern beim
    // Splice mit, ihr Inhalt ist danach automatisch am richtigen Ort.
    auto zaehler = [&](const Block& b, int32_t delta) {
        int32_t v; std::memcpy(&v, m_bytes.data() + b.countAt, 4);
        v += delta;
        std::memcpy(m_bytes.data() + b.countAt, &v, 4);
    };
    zaehler(m_meshData, static_cast<int32_t>(meshPack.size() / 2));
    zaehler(m_meshPtrs, static_cast<int32_t>(srcPtrs.size()));
    zaehler(m_anims, static_cast<int32_t>(animBytes.size() / SZ_ANIM));
    zaehler(m_changes, static_cast<int32_t>(changeBytes.size() / SZ_CHANGE));
    zaehler(m_ranges, static_cast<int32_t>(rangeBytes.size() / SZ_RANGE));
    zaehler(m_commands, static_cast<int32_t>(cmdBytes.size() / 2));
    zaehler(m_bones, static_cast<int32_t>(boneBytes.size() / 4));
    zaehler(m_frames, static_cast<int32_t>(frameBytes.size() / 2));
    zaehler(m_texinfos, texinfoNeu);
    if (!ersetzt) zaehler(m_objects, 1);

    std::vector<Splice> splices;
    auto anhaengen = [&](const Block& b, int elem, std::vector<uint8_t>&& bytes) {
        if (bytes.empty()) return;
        splices.push_back({ b.dataAt + static_cast<size_t>(b.count) * elem, std::move(bytes) });
    };
    anhaengen(m_meshData, 2, std::move(meshPack));
    anhaengen(m_meshPtrs, 4, std::move(neuePtrs));
    anhaengen(m_anims, SZ_ANIM, std::move(animBytes));
    anhaengen(m_changes, SZ_CHANGE, std::move(changeBytes));
    anhaengen(m_ranges, SZ_RANGE, std::move(rangeBytes));
    anhaengen(m_commands, 2, std::move(cmdBytes));
    anhaengen(m_bones, 4, std::move(boneBytes));
    anhaengen(m_frames, 2, std::move(frameBytes));
    if (!ersetzt)
        splices.push_back({ m_objects.dataAt + static_cast<size_t>(m_objects.count) * SZ_OBJ,
                            std::vector<uint8_t>(objRec, objRec + SZ_OBJ) });
    anhaengen(m_texinfos, SZ_TEXINFO, std::move(neueTexinfos));

    size_t imHauptblock = 0;
    for (const Splice& s : splices) imHauptblock += s.bytes.size();

    applySplices(m_bytes, splices);

    // Die doppelt gefuehrte Hauptblockgroesse nachziehen.
    m_mainSize += imHauptblock;
    const int32_t neueGroesse = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &neueGroesse, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &neueGroesse, 4);

    if (!reparse(error)) { error = "nach dem Anhaengen: " + error; return false; }

    //========================================================================
    // Phase D: Sounds. Die Anim-Befehle nennen ihre PlaySound-Nummern selbst;
    // die Schuss- und Stopsounds der Waffen stehen bei KEINER in den
    // Anim-Daten, sondern in fest verdrahteten SoundEffect()-Aufrufen der
    // Engine. Ohne diese Tabelle blieb die HK damals stumm - der Fehler, bis
    // er gefunden war (siehe Plan.md der Waffenarbeit).
    //========================================================================
    struct WaffenSfx { const int* slots; int nSlots; const int* sfx; int nSfx; };
    static const int sPistolen[] = { 1, 334, 335 },        fPistolen[] = { 8 };
    static const int sUzi[]      = { 2, 336, 337 },        fUzi[]      = { 43, 44 };
    static const int sShotgun[]  = { 3, 338, 339, 340 },   fShotgun[]  = { 45 };
    static const int sGrapple[]  = { 4, 341, 342, 343, 344 }, fGrapple[] = { 235 };
    static const int sHK[]       = { 5, 345, 346 },        fHK[]       = { 68, 69, 14 };
    static const int sRevolver[] = { 6, 347, 348 },        fRevolver[] = { 121 };
    static const WaffenSfx kWaffen[] = {
        { sPistolen, 3, fPistolen, 1 }, { sUzi, 3, fUzi, 2 }, { sShotgun, 4, fShotgun, 1 },
        { sGrapple, 5, fGrapple, 1 },   { sHK, 3, fHK, 3 },   { sRevolver, 3, fRevolver, 1 },
    };
    for (const WaffenSfx& w : kWaffen)
        for (int i = 0; i < w.nSlots; ++i)
            if (w.slots[i] == slotId)
            {
                gebrauchteSfx.insert(6); gebrauchteSfx.insert(7); gebrauchteSfx.insert(105);
                for (int k = 0; k < w.nSfx; ++k) gebrauchteSfx.insert(w.sfx[k]);
            }

    for (int sfx : gebrauchteSfx)
    {
        if (soundInfoIndex(sfx) >= 0) continue;      // hat das Ziel schon
        if (src.soundInfoIndex(sfx) < 0) continue;   // hat nicht mal die Quelle
        std::string sErr;
        if (!copySoundFrom(src, sfx, sfx, sErr))
        {
            // Ein fehlender Klang macht das Level nicht kaputt - anders als
            // alles oben. Deshalb hier weich statt hart.
            continue;
        }
    }

    return true;
}

//----------------------------------------------------------------------------
// Einbau eines TR3-Moveables in ein TR5-Level (#88).
//
// BEWUSST eine eigene Datei: Tr5Transfer.cpp traegt den erprobten
// TR5->TR5-Weg, an dem der ganze Gegnermix haengt. Der Fremdimport hat
// andere Vorbedingungen (die Scheiben sind schon geschnitten, die Zeiger
// schon auf den Scheibenanfang bezogen) und wird deshalb daneben gelegt,
// statt den bestehenden Ablauf umzubauen.
//
// Das Paket kommt aus TR3Hol: dort wurden Faces, Anims und Frames bereits
// auf das TR5-Layout gebracht. Hier passiert nur noch das, was das ZIEL
// betrifft - alle Indizes um die Zaehlerstaende des Ziels erhoehen und
// alles in einem Rutsch anhaengen.
//
// TEXTUREN: Dieser Weg fasst die Texturseiten NICHT an. Jedes Face zeigt
// danach auf denselben, im Ziel bereits vorhandenen Texinfo. Der Gegner
// sieht falsch aus, aber es wird keine Seite eingefuegt und keine Nummer
// verschoben - genau die Stelle, an der ein Level sonst zerfaellt
// (Nutzervorgabe). Echte Texturen spaeter ueber Tr5Transfer.cpp.
//----------------------------------------------------------------------------
#include "Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace
{
    const int SZ_ANIM = 40, SZ_CHANGE = 6, SZ_RANGE = 8, SZ_OBJ = 20;

    // Dieselben Values wie im Namensraum von Tr5Level.cpp (dort Zeile 794ff).
    // Bewusst abgelesen und nicht geraten; wenn sie sich dort je aendern,
    // faellt es hier auf, weil der Sound-Block nicht mehr aufgeht.
    constexpr int SAMPLE_LUT_SIZE = 450;        // MAX_SAMPLES, global/types.h
    constexpr int SZ_SAMPLE_INFO = 8;
    constexpr int MAX_SAMPLES_PLAYABLE = 256;   // DS_Buffers[], siehe #94

    uint16_t g16(const std::vector<uint8_t>& b, size_t p) { uint16_t v; std::memcpy(&v, &b[p], 2); return v; }
    uint32_t g32(const std::vector<uint8_t>& b, size_t p) { uint32_t v; std::memcpy(&v, &b[p], 4); return v; }
    void     s16(std::vector<uint8_t>& b, size_t p, uint16_t v) { std::memcpy(&b[p], &v, 2); }
    void     s32(std::vector<uint8_t>& b, size_t p, uint32_t v) { std::memcpy(&b[p], &v, 4); }

    // Jedes Texturwort eines Meshes anfassen. Gleicher Ableser wie
    // meshSize/forEachFaceTex in Tr5Transfer.cpp - Kopf 10 Byte, dann das
    // Vertexwort; 0 Normalen heisst NORMALEN, nur negativ ist Prelight.
    template <typename F>
    bool faceTexte(std::vector<uint8_t>& b, size_t at, size_t ende, F fn)
    {
        size_t p = at + 10;
        if (p + 2 > ende) return false;
        const int16_t vw = static_cast<int16_t>(g16(b, p)); p += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) p += (static_cast<size_t>((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            p += static_cast<size_t>(nv) * 6;
            if (p + 2 > ende) return false;
            const int16_t nn = static_cast<int16_t>(g16(b, p)); p += 2;
            p += static_cast<size_t>(nv) * (nn >= 0 ? 6 : 2);
        }
        if (p + 2 > ende) return false;
        const int16_t nq = static_cast<int16_t>(g16(b, p)); p += 2;
        for (int i = 0; i < nq; ++i, p += 12) { if (p + 12 > ende) return false; fn(p + 8); }
        if (p + 2 > ende) return false;
        const int16_t nt = static_cast<int16_t>(g16(b, p)); p += 2;
        for (int i = 0; i < nt; ++i, p += 10) { if (p + 10 > ende) return false; fn(p + 6); }
        return p <= ende;
    }
}

bool Tr5Level::addSoundsFromTr3(const std::vector<uint8_t>& package, int basisId,
                               std::vector<std::pair<int,int>>& mapping,
                               std::string& error,
                               const std::vector<int>* lautProzent)
{
    char buf[256];
    mapping.clear();

    if (!m_soundOk) { error = "Ziel-Level: Sound-Block nicht deutbar"; return false; }
    if (package.size() < 12 || std::memcmp(package.data(), "T35S", 4) != 0)
    { error = "Kein TR3-Soundpaket (Kennung T35S fehlt)"; return false; }
    if (g32(package, 4) != 2) { error = "Unbekannte Soundpaketversion"; return false; }

    const uint32_t anzahl = g32(package, 8);
    if (basisId < 0 || basisId + int(anzahl) > SAMPLE_LUT_SIZE)
    { error = "Zielplaetze liegen ausserhalb der Soundmap"; return false; }

    // Das SAMPLE_INFO wird NEU gebaut, nicht von einem vorhandenen abgeschrieben.
    //
    // Der erste Versuch nahm das erste Info des Ziels als Vorlage, um bei der
    // Lautstaerke nicht raten zu muessen. Damit kamen aber auch Tonhoehe und
    // Flags mit: in Andrea1 ist das Laras Schritt (pitch 12, Flags 0x6010 mit
    // Zufalls-Tonhoehe). Der T-Rex stampfte dadurch bei jedem Schritt anders
    // und grundsaetzlich zu hoch - im Spiel gehoert, der Nutzer hat es am
    // Original gegengehoert.
    //
    // Deshalb feste, neutrale Values nach dem Muster, das Andrea1s eigene
    // Eintraege zeigen (vol 191, rad 13, pitch 0, Flags 0x0004 = eine
    // Variante, keine Verstimmung). Ein Klang, der so eingetragen ist, klingt
    // genau wie seine Datei.
    const uint8_t VOL = 191;      // gebraeuchlicher Wert in TR5-Leveln
    const int8_t  RAD = 13;       // Hoerweite in Sektoren, dito
    const uint16_t FLAGS = 0x0004;   // Bits 2-5 = 1 Variante, sonst nichts

    // --- Erst alles einsammeln, dann write ----------------------------
    struct Welle { int32_t uncomp; std::vector<uint8_t> wav; };
    struct Klang { int origId; std::vector<Welle> waves; };
    std::vector<Klang> sounds;
    int32_t samplesGesamt = 0;
    {
        size_t p = 12;
        for (uint32_t i = 0; i < anzahl; ++i)
        {
            if (p + 10 > package.size()) { error = "Soundpaket endet vorzeitig"; return false; }
            Klang k;
            k.origId = g16(package, p);
            p += 8;                                   // Volume, Chance, Chars: siehe oben
            const uint16_t varianten = g16(package, p); p += 2;
            if (varianten < 1 || varianten > 15) { error = "Unplausible Variantenzahl"; return false; }

            for (uint16_t v = 0; v < varianten; ++v)
            {
                if (p + 4 > package.size()) { error = "Soundpaket endet vor einer Variante"; return false; }
                const uint32_t len = g32(package, p); p += 4;
                if (p + len > package.size()) { error = "Wave laeuft aus dem Paket"; return false; }
                Welle w;
                w.wav.assign(package.begin() + p, package.begin() + p + len);
                p += len;

                // uncomp_size sauber setzen: Laenge des data-Blocks. Die
                // Engine rechnet ihn im PCM-Zweig zwar selbst aus, aber ein
                // stimmiger Wert erspart spaeteren Lesern das Raetselraten.
                w.uncomp = 0;
                for (size_t q = 12; q + 8 <= w.wav.size(); )
                {
                    const uint32_t cl = g32(w.wav, q + 4);
                    if (!std::memcmp(&w.wav[q], "data", 4)) { w.uncomp = int32_t(cl); break; }
                    q += 8 + cl + (cl & 1);
                }
                k.waves.push_back(std::move(w));
            }
            samplesGesamt += int32_t(k.waves.size());
            sounds.push_back(std::move(k));
        }
    }

    if (m_nInfos <= 0) { error = "Ziel-Level hat keinen Sound-Block"; return false; }

    if (m_nSamples + samplesGesamt > MAX_SAMPLES_PLAYABLE)
    {
        std::snprintf(buf, sizeof(buf),
            "Level haette %d Samples - die Engine laedt hoechstens %d",
            m_nSamples + samplesGesamt, MAX_SAMPLES_PLAYABLE);
        error = buf;
        return false;
    }

    for (size_t i = 0; i < sounds.size(); ++i)
    {
        const int ziel = basisId + int(i);
        if (soundInfoIndex(ziel) >= 0)
        {
            std::snprintf(buf, sizeof(buf), "Soundmap-Platz %d ist im Ziel schon belegt", ziel);
            error = buf;
            return false;
        }
    }

    // --- Infos einfuegen (liegen IM Hauptblock, der waechst) --------------
    std::vector<uint8_t> roh;
    int32_t laufSample = m_nSamples;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        // SAMPLE_INFO: short number, uchar volume, char radius,
        //              char randomness, char pitch, short flags
        //
        // Mehrere Varianten sind EIN Info mit N aufeinanderfolgenden
        // SAMPLES - die Engine wuerfelt sample = number + Zufall(N)
        // (sound.cpp:280). Die Zahl steht in Bit 2-5 der Flags. Wer
        // stattdessen N Infos anlegt, holt sich die Falle aus #76.
        const int varianten = static_cast<int>(sounds[i].waves.size());
        std::vector<uint8_t> info(SZ_SAMPLE_INFO, 0);
        const int16_t nummer = static_cast<int16_t>(laufSample);
        std::memcpy(info.data(), &nummer, 2);
        info[2] = VOL;
        info[3] = static_cast<uint8_t>(RAD);
        info[4] = 0;                       // keine Lautstaerkestreuung
        info[5] = 0;                       // keine Tonhoehenverschiebung
        const uint16_t flags = static_cast<uint16_t>((FLAGS & ~0x3C) | ((varianten & 0xF) << 2));
        std::memcpy(info.data() + 6, &flags, 2);
        laufSample += varianten;

        // Lautstaerke nachjustieren. Das Feld ist EIN Byte (0..255) - der Wert
        // kommt aus der Vorlage des Ziels, damit die Groessenordnung sicher
        // stimmt, und wird hier nur prozentual gedehnt. Beim T-Rex wollte der
        // Nutzer das Bruellen rund 30 Prozent lauter.
        if (lautProzent && i < lautProzent->size())
        {
            const int p = (*lautProzent)[i];
            if (p > 0 && p != 100)
            {
                int v = (static_cast<int>(info[2]) * p) / 100;
                if (v > 255) v = 255;
                if (v < 1) v = 1;
                info[2] = static_cast<uint8_t>(v);
            }
        }

        roh.insert(roh.end(), info.begin(), info.end());
    }

    const size_t einfuegenAn = m_infosAt + static_cast<size_t>(m_nInfos) * SZ_SAMPLE_INFO;
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(einfuegenAn), roh.begin(), roh.end());

    for (size_t i = 0; i < sounds.size(); ++i)
    {
        const int ziel = basisId + int(i);
        const int16_t lut = static_cast<int16_t>(m_nInfos + int32_t(i));
        std::memcpy(m_bytes.data() + m_lutAt + static_cast<size_t>(ziel) * 2, &lut, 2);
        mapping.push_back({ sounds[i].origId, ziel });
    }

    m_nInfos += int32_t(sounds.size());
    std::memcpy(m_bytes.data() + m_numInfosAt, &m_nInfos, 4);

    shiftSound(roh.size(), einfuegenAn);
    if (m_itemCountOffset >= einfuegenAn) m_itemCountOffset += roh.size();

    m_mainSize += roh.size();
    const int32_t neueGroesse = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &neueGroesse, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &neueGroesse, 4);

    m_nSamples += samplesGesamt;
    std::memcpy(m_bytes.data() + m_numSamplesInMainAt, &m_nSamples, 4);
    std::memcpy(m_bytes.data() + m_numSamplesAt, &m_nSamples, 4);

    // --- Sampledaten ans Dateiende ---------------------------------------
    // Reihenfolge zaehlt: die Varianten eines Klangs muessen unmittelbar
    // aufeinander folgen, sonst greift die Zufallsauswahl der Engine daneben.
    for (const Klang& k : sounds)
        for (const Welle& w : k.waves)
        {
            const int32_t uc = w.uncomp, cs = static_cast<int32_t>(w.wav.size());
            const uint8_t* pu = reinterpret_cast<const uint8_t*>(&uc);
            const uint8_t* pc = reinterpret_cast<const uint8_t*>(&cs);
            m_bytes.insert(m_bytes.end(), pu, pu + 4);
            m_bytes.insert(m_bytes.end(), pc, pc + 4);
            m_bytes.insert(m_bytes.end(), w.wav.begin(), w.wav.end());
        }
    return true;
}

bool Tr5Level::addMoveableFromTr3(const std::vector<uint8_t>& package, int zielSlot,
                                  int texinfoIndex, std::string& error)
{
    char buf[256];

    // Frische Blockkarte holen (#104) - gleiche Begruendung wie am Eingang
    // von copyMoveableFrom: Vorarbeiten wie addTrigger haben den Hauptblock
    // verschoben, ohne die Karte nachzuziehen.
    if (!reparse(error)) { error = "Blockkarte: " + error; return false; }

    if (package.size() < 48 || std::memcmp(package.data(), "T35H", 4) != 0)
    { error = "Kein TR3-Paket (Kennung T35H fehlt)"; return false; }
    if (zielSlot <= 0) { error = "Zielslot muss groesser 0 sein"; return false; }
    // texinfoIndex < 0 heisst: die Faces tragen schon gueltige Texinfos
    // (die Texturen wurden vorher eingesetzt) und werden NICHT angefasst.
    if (texinfoIndex >= m_texinfos.count)
    { error = "Texinfo-Index liegt nicht im Ziel"; return false; }

    size_t p = 4;
    const uint32_t version   = g32(package, p); p += 4;
    p += 4;                                              // objectId, hier ohne Belang
    const uint32_t nMeshes   = g32(package, p); p += 4;
    const uint32_t nAnims    = g32(package, p); p += 4;
    const uint32_t nFrameW   = g32(package, p); p += 4;
    const uint32_t nBoneW    = g32(package, p); p += 4;
    const uint32_t nSC       = g32(package, p); p += 4;
    const uint32_t nAD       = g32(package, p); p += 4;
    const uint32_t nCmd      = g32(package, p); p += 4;
    p += 8;                                              // Quell-Offsets, schon rebasiert
    // Version 2 haengt hinter dem Kommandoblock einen Texturteil an. Der wird
    // hier nicht gelesen - dafuer gibt es addTexturesFromTr3 -, stoert aber
    // auch nicht: alles davor steht unveraendert an derselben Stelle.
    if (version != 1 && version != 2) { error = "Unbekannte Paketversion"; return false; }

    // --- Meshes einsammeln und zu einem Block packen -----------------------
    std::vector<uint8_t> meshPack;
    std::vector<uint32_t> meshStart;
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) { error = "Paket endet in der Meshliste"; return false; }
        const uint32_t len = g32(package, p); p += 4;
        if (p + len > package.size()) { error = "Mesh laeuft aus dem Paket"; return false; }
        if (len & 1) { error = "Mesh hat ungerade Laenge"; return false; }
        meshStart.push_back(static_cast<uint32_t>(meshPack.size()));
        meshPack.insert(meshPack.end(), package.begin() + p, package.begin() + p + len);
        p += len;
    }

    // Alle Faces auf denselben vorhandenen Texinfo zeigen lassen. Bit 15 der
    // Texturworte traegt bei TR5 ein Flag; hier wird bewusst sauber genullt.
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        const size_t at = meshStart[i];
        const size_t ende = (i + 1 < nMeshes) ? meshStart[i + 1] : meshPack.size();
        // Auch bei texinfoIndex < 0 durchlaufen: der Ableser prueft dabei,
        // dass jedes Mesh als TR5-Mesh lesbar ist.
        if (!faceTexte(meshPack, at, ende,
                       [&](size_t texAt) {
                           if (texinfoIndex >= 0) s16(meshPack, texAt, static_cast<uint16_t>(texinfoIndex));
                       }))
        {
            std::snprintf(buf, sizeof(buf), "Mesh %u laesst sich nicht als TR5-Mesh read", i);
            error = buf;
            return false;
        }
    }

    auto hol = [&](size_t n) -> std::vector<uint8_t> {
        std::vector<uint8_t> v;
        if (p + n <= package.size()) { v.assign(package.begin() + p, package.begin() + p + n); p += n; }
        return v;
    };
    std::vector<uint8_t> boneBytes  = hol(static_cast<size_t>(nBoneW) * 4);
    std::vector<uint8_t> frameBytes = hol(static_cast<size_t>(nFrameW) * 2);
    std::vector<uint8_t> animBytes  = hol(static_cast<size_t>(nAnims) * SZ_ANIM);
    std::vector<uint8_t> chgBytes   = hol(static_cast<size_t>(nSC) * SZ_CHANGE);
    std::vector<uint8_t> rngBytes   = hol(static_cast<size_t>(nAD) * SZ_RANGE);
    std::vector<uint8_t> cmdBytes   = hol(static_cast<size_t>(nCmd) * 2);
    // Version 1 endet hier genau; Version 2 haengt den Texturteil an, der von
    // addTexturesFromTr3 gelesen wird und hier folgerichtig uebrig bleibt.
    if (version == 1 && p != package.size())
    { error = "Paketgroesse passt nicht zu den Kopfangaben"; return false; }
    if (p > package.size()) { error = "Paket kuerzer als seine Kopfangaben"; return false; }

    // --- Rebasen auf die Zaehlerstaende des Ziels --------------------------
    const int32_t dstMeshBytes = m_meshData.count * 2;
    const int32_t dstPtr       = m_meshPtrs.count;
    const int32_t dstAnim      = m_anims.count;
    const int32_t dstChg       = m_changes.count;
    const int32_t dstRng       = m_ranges.count;
    const int32_t dstCmd       = m_commands.count;
    const int32_t dstBone      = m_bones.count;
    const int32_t dstFrameByte = m_frames.count * 2;

    // Die Anims kommen scheibenrelativ aus TR3Hol: Frame-Zeiger ab 0,
    // Sprungziel ab 0. Die Anhaenge (Zustandswechsel, Bereiche, Befehle)
    // wurden als GANZE Bloecke mitgegeben, ihre Indizes sind also schon
    // 0-basiert - beide Faelle brauchen nur den Zielversatz.
    for (size_t o = 0; o + SZ_ANIM <= animBytes.size(); o += SZ_ANIM)
    {
        s32(animBytes, o, g32(animBytes, o) + static_cast<uint32_t>(dstFrameByte));
        s16(animBytes, o + 28, static_cast<uint16_t>(g16(animBytes, o + 28) + dstAnim));
        if (g16(animBytes, o + 32)) s16(animBytes, o + 34, static_cast<uint16_t>(g16(animBytes, o + 34) + dstChg));
        if (g16(animBytes, o + 36)) s16(animBytes, o + 38, static_cast<uint16_t>(g16(animBytes, o + 38) + dstCmd));
    }
    for (size_t o = 0; o + SZ_CHANGE <= chgBytes.size(); o += SZ_CHANGE)
        if (g16(chgBytes, o + 2)) s16(chgBytes, o + 4, static_cast<uint16_t>(g16(chgBytes, o + 4) + dstRng));
    for (size_t o = 0; o + SZ_RANGE <= rngBytes.size(); o += SZ_RANGE)
        s16(rngBytes, o + 4, static_cast<uint16_t>(g16(rngBytes, o + 4) + dstAnim));

    std::vector<uint8_t> ptrBytes;
    for (uint32_t st : meshStart)
    {
        const uint32_t v = static_cast<uint32_t>(dstMeshBytes) + st;
        ptrBytes.insert(ptrBytes.end(), reinterpret_cast<const uint8_t*>(&v),
                                        reinterpret_cast<const uint8_t*>(&v) + 4);
    }

    uint8_t objRec[SZ_OBJ];
    {
        const int32_t  s = zielSlot;                        std::memcpy(objRec + 0,  &s,  4);
        const int16_t nm = static_cast<int16_t>(nMeshes);   std::memcpy(objRec + 4,  &nm, 2);
        const int16_t mi = static_cast<int16_t>(dstPtr);    std::memcpy(objRec + 6,  &mi, 2);
        const int32_t bi = dstBone;                         std::memcpy(objRec + 8,  &bi, 4);
        const uint32_t fo = static_cast<uint32_t>(dstFrameByte); std::memcpy(objRec + 12, &fo, 4);
        const uint16_t ai = static_cast<uint16_t>(dstAnim); std::memcpy(objRec + 16, &ai, 2);
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

    // --- Zaehler hoch, dann alles in einem Rutsch einfuegen ----------------
    auto zaehler = [&](const Block& b, int32_t d) {
        int32_t v; std::memcpy(&v, m_bytes.data() + b.countAt, 4);
        v += d;
        std::memcpy(m_bytes.data() + b.countAt, &v, 4);
    };
    zaehler(m_meshData, static_cast<int32_t>(meshPack.size() / 2));
    zaehler(m_meshPtrs, static_cast<int32_t>(ptrBytes.size() / 4));
    zaehler(m_anims,    static_cast<int32_t>(animBytes.size() / SZ_ANIM));
    zaehler(m_changes,  static_cast<int32_t>(chgBytes.size() / SZ_CHANGE));
    zaehler(m_ranges,   static_cast<int32_t>(rngBytes.size() / SZ_RANGE));
    zaehler(m_commands, static_cast<int32_t>(cmdBytes.size() / 2));
    zaehler(m_bones,    static_cast<int32_t>(boneBytes.size() / 4));
    zaehler(m_frames,   static_cast<int32_t>(frameBytes.size() / 2));
    if (!ersetzt) zaehler(m_objects, 1);

    // Einfuegepunkte sammeln und von HINTEN nach vorne einsetzen, damit die
    // vorderen Positionen gueltig bleiben.
    struct Einfuegung { size_t at; std::vector<uint8_t> bytes; };
    std::vector<Einfuegung> e;
    auto anhaengen = [&](const Block& b, int elem, std::vector<uint8_t>& bytes) {
        if (!bytes.empty())
            e.push_back({ b.dataAt + static_cast<size_t>(b.count) * elem, std::move(bytes) });
    };
    anhaengen(m_meshData, 2, meshPack);
    anhaengen(m_meshPtrs, 4, ptrBytes);
    anhaengen(m_anims, SZ_ANIM, animBytes);
    anhaengen(m_changes, SZ_CHANGE, chgBytes);
    anhaengen(m_ranges, SZ_RANGE, rngBytes);
    anhaengen(m_commands, 2, cmdBytes);
    anhaengen(m_bones, 4, boneBytes);
    anhaengen(m_frames, 2, frameBytes);
    if (!ersetzt)
    {
        std::vector<uint8_t> ob(objRec, objRec + SZ_OBJ);
        e.push_back({ m_objects.dataAt + static_cast<size_t>(m_objects.count) * SZ_OBJ, std::move(ob) });
    }

    size_t dazu = 0;
    for (const Einfuegung& x : e) dazu += x.bytes.size();

    std::stable_sort(e.begin(), e.end(),
                     [](const Einfuegung& a, const Einfuegung& b) { return a.at > b.at; });
    for (const Einfuegung& x : e)
    {
        if (x.at > m_bytes.size()) { error = "Einfuegepunkt liegt hinter dem Dateiende"; return false; }
        m_bytes.insert(m_bytes.begin() + x.at, x.bytes.begin(), x.bytes.end());
    }

    // Die doppelt gefuehrte Hauptblockgroesse nachziehen - beide Stellen.
    m_mainSize += dazu;
    const int32_t neueGroesse = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &neueGroesse, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &neueGroesse, 4);

    if (!reparse(error)) { error = "nach dem Einbau: " + error; return false; }
    return true;
}

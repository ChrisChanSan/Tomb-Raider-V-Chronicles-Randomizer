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
// Installing a TR3 moveable into a TR5 level.
//
// DELIBERATELY its own file: Tr5Transfer.cpp carries the proven TR5->TR5
// path the whole enemy mix hangs on. The foreign import has different
// preconditions (the slices are already cut, the pointers already rebased
// to the slice start) and is therefore laid down next to it instead of
// rebuilding the existing flow.
//
// The package comes from TR3Fetch: faces, anims and frames were already
// brought into the TR5 layout there. Only what concerns the TARGET happens
// here - raise all indices by the target's counters and append everything
// in one stroke.
//
// TEXTURES: this path does NOT touch the texture pages. Every face points
// at the same texinfo that already exists in the target afterwards. The
// enemy looks wrong, but no page is inserted and no number shifts - exactly
// the spot where a level otherwise falls apart. Real textures come later
// via Tr5Transfer.cpp.
//----------------------------------------------------------------------------
#include "Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <algorithm>

namespace
{
    const int SZ_ANIM = 40, SZ_CHANGE = 6, SZ_RANGE = 8, SZ_OBJ = 20;

    // The same values as in the anonymous namespace of Tr5Level.cpp.
    // Deliberately read off, not guessed; if they ever change there, it
    // shows up here because the sound block no longer parses.
    constexpr int SAMPLE_LUT_SIZE = 450;        // MAX_SAMPLES, global/types.h
    constexpr int SZ_SAMPLE_INFO = 8;
    constexpr int MAX_SAMPLES_PLAYABLE = 256;   // DS_Buffers[]

    uint16_t g16(const std::vector<uint8_t>& b, size_t p) { uint16_t v; std::memcpy(&v, &b[p], 2); return v; }
    uint32_t g32(const std::vector<uint8_t>& b, size_t p) { uint32_t v; std::memcpy(&v, &b[p], 4); return v; }
    void     s16(std::vector<uint8_t>& b, size_t p, uint16_t v) { std::memcpy(&b[p], &v, 2); }
    void     s32(std::vector<uint8_t>& b, size_t p, uint32_t v) { std::memcpy(&b[p], &v, 4); }

    // Touch every texture word of a mesh. Same walker as
    // meshSize/forEachFaceTex in Tr5Transfer.cpp - header 10 bytes, then the
    // vertex word; 0 normals means NORMALS, only negative is prelight.
    template <typename F>
    bool faceTextures(std::vector<uint8_t>& b, size_t at, size_t end, F fn)
    {
        size_t p = at + 10;
        if (p + 2 > end) return false;
        const int16_t vw = static_cast<int16_t>(g16(b, p)); p += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) p += (static_cast<size_t>((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            p += static_cast<size_t>(nv) * 6;
            if (p + 2 > end) return false;
            const int16_t nn = static_cast<int16_t>(g16(b, p)); p += 2;
            p += static_cast<size_t>(nv) * (nn >= 0 ? 6 : 2);
        }
        if (p + 2 > end) return false;
        const int16_t nq = static_cast<int16_t>(g16(b, p)); p += 2;
        for (int i = 0; i < nq; ++i, p += 12) { if (p + 12 > end) return false; fn(p + 8); }
        if (p + 2 > end) return false;
        const int16_t nt = static_cast<int16_t>(g16(b, p)); p += 2;
        for (int i = 0; i < nt; ++i, p += 10) { if (p + 10 > end) return false; fn(p + 6); }
        return p <= end;
    }
}

bool Tr5Level::addSoundsFromTr3(const std::vector<uint8_t>& package, int baseId,
                               std::vector<std::pair<int,int>>& mapping,
                               std::string& error,
                               const std::vector<int>* volumePercent)
{
    char buf[256];
    mapping.clear();

    if (!m_soundOk) { error = "target level: sound block not parseable"; return false; }
    if (package.size() < 12 || std::memcmp(package.data(), "T35S", 4) != 0)
    { error = "not a TR3 sound package (magic T35S missing)"; return false; }
    if (g32(package, 4) != 2) { error = "unknown sound package version"; return false; }

    const uint32_t count = g32(package, 8);
    if (baseId < 0 || baseId + int(count) > SAMPLE_LUT_SIZE)
    { error = "target slots lie outside the soundmap"; return false; }

    // The SAMPLE_INFO is built FRESH, not copied from an existing one.
    //
    // The first attempt took the target's first info as a template, to
    // avoid guessing the volume. But that also brought pitch and flags
    // along: in Andrea1 that is Lara's footstep (pitch 12, flags 0x6010
    // with random pitch). The T-Rex thus stomped differently on every step
    // and fundamentally too high.
    //
    // Hence fixed, neutral values following the pattern Andrea1's own
    // entries show (vol 191, rad 13, pitch 0, flags 0x0004 = one variant,
    // no detuning). A sound registered like this sounds exactly like its
    // file.
    const uint8_t VOL = 191;      // common value in TR5 levels
    const int8_t  RAD = 13;       // hearing range in sectors, likewise
    const uint16_t FLAGS = 0x0004;   // bits 2-5 = 1 variant, nothing else

    // --- Collect everything first, then write -----------------------------
    struct Wave { int32_t uncomp; std::vector<uint8_t> wav; };
    struct Sound { int origId; std::vector<Wave> waves; };
    std::vector<Sound> sounds;
    int32_t samplesTotal = 0;
    {
        size_t p = 12;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (p + 10 > package.size()) { error = "sound package ends prematurely"; return false; }
            Sound k;
            k.origId = g16(package, p);
            p += 8;                                   // volume, chance, chars: see above
            const uint16_t variants = g16(package, p); p += 2;
            if (variants < 1 || variants > 15) { error = "implausible variant count"; return false; }

            for (uint16_t v = 0; v < variants; ++v)
            {
                if (p + 4 > package.size()) { error = "sound package ends before a variant"; return false; }
                const uint32_t len = g32(package, p); p += 4;
                if (p + len > package.size()) { error = "wave runs out of the package"; return false; }
                Wave w;
                w.wav.assign(package.begin() + p, package.begin() + p + len);
                p += len;

                // Set uncomp_size properly: length of the data chunk. The
                // engine computes it itself in the PCM branch, but a
                // consistent value spares later readers the guesswork.
                w.uncomp = 0;
                for (size_t q = 12; q + 8 <= w.wav.size(); )
                {
                    const uint32_t cl = g32(w.wav, q + 4);
                    if (!std::memcmp(&w.wav[q], "data", 4)) { w.uncomp = int32_t(cl); break; }
                    q += 8 + cl + (cl & 1);
                }
                k.waves.push_back(std::move(w));
            }
            samplesTotal += int32_t(k.waves.size());
            sounds.push_back(std::move(k));
        }
    }

    if (m_nInfos <= 0) { error = "target level has no sound block"; return false; }

    if (m_nSamples + samplesTotal > MAX_SAMPLES_PLAYABLE)
    {
        std::snprintf(buf, sizeof(buf),
            "level would have %d samples - the engine loads at most %d",
            m_nSamples + samplesTotal, MAX_SAMPLES_PLAYABLE);
        error = buf;
        return false;
    }

    for (size_t i = 0; i < sounds.size(); ++i)
    {
        const int target = baseId + int(i);
        if (soundInfoIndex(target) >= 0)
        {
            std::snprintf(buf, sizeof(buf), "soundmap slot %d is already taken in the target", target);
            error = buf;
            return false;
        }
    }

    // --- Insert the infos (they live IN the main block, which grows) ------
    std::vector<uint8_t> raw;
    int32_t runningSample = m_nSamples;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        // SAMPLE_INFO: short number, uchar volume, char radius,
        //              char randomness, char pitch, short flags
        //
        // Several variants are ONE info with N consecutive SAMPLES - the
        // engine rolls sample = number + random(N) (sound.cpp). The
        // count sits in bits 2-5 of the flags. Creating N separate infos
        // instead makes the engine's random pick land wrong.
        const int variants = static_cast<int>(sounds[i].waves.size());
        std::vector<uint8_t> info(SZ_SAMPLE_INFO, 0);
        const int16_t number = static_cast<int16_t>(runningSample);
        std::memcpy(info.data(), &number, 2);
        info[2] = VOL;
        info[3] = static_cast<uint8_t>(RAD);
        info[4] = 0;                       // no volume randomness
        info[5] = 0;                       // no pitch shift
        const uint16_t flags = static_cast<uint16_t>((FLAGS & ~0x3C) | ((variants & 0xF) << 2));
        std::memcpy(info.data() + 6, &flags, 2);
        runningSample += variants;

        // Adjust the volume. The field is ONE byte (0..255) - the value
        // comes from the target's template so the order of magnitude is
        // certainly right, and is only stretched proportionally here, e.g.
        // the T-Rex roar comes out about 30 percent louder.
        if (volumePercent && i < volumePercent->size())
        {
            const int p = (*volumePercent)[i];
            if (p > 0 && p != 100)
            {
                int v = (static_cast<int>(info[2]) * p) / 100;
                if (v > 255) v = 255;
                if (v < 1) v = 1;
                info[2] = static_cast<uint8_t>(v);
            }
        }

        raw.insert(raw.end(), info.begin(), info.end());
    }

    const size_t insertAt = m_infosAt + static_cast<size_t>(m_nInfos) * SZ_SAMPLE_INFO;
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(insertAt), raw.begin(), raw.end());

    for (size_t i = 0; i < sounds.size(); ++i)
    {
        const int target = baseId + int(i);
        const int16_t lut = static_cast<int16_t>(m_nInfos + int32_t(i));
        std::memcpy(m_bytes.data() + m_lutAt + static_cast<size_t>(target) * 2, &lut, 2);
        mapping.push_back({ sounds[i].origId, target });
    }

    m_nInfos += int32_t(sounds.size());
    std::memcpy(m_bytes.data() + m_numInfosAt, &m_nInfos, 4);

    shiftSound(raw.size(), insertAt);
    if (m_itemCountOffset >= insertAt) m_itemCountOffset += raw.size();

    m_mainSize += raw.size();
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    m_nSamples += samplesTotal;
    std::memcpy(m_bytes.data() + m_numSamplesInMainAt, &m_nSamples, 4);
    std::memcpy(m_bytes.data() + m_numSamplesAt, &m_nSamples, 4);

    // --- Sample data at the end of the file -------------------------------
    // Order matters: the variants of one sound must follow each other
    // directly, or the engine's random pick grabs the wrong one.
    for (const Sound& k : sounds)
        for (const Wave& w : k.waves)
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

bool Tr5Level::paintMeshFlat(int targetSlot, int meshNo, int r, int g, int b,
                             int& usedTexinfo, int& gotR, int& gotG, int& gotB,
                             std::string& error)
{
    if (!texWorkshopOpen(error)) return false;
    const size_t PAGE = 256 * 256 * 2;
    const size_t nPages = m_tex16.size() / PAGE;
    const size_t SZ_TEXINFO = 40;

    // Look for the entry that comes closest to the wanted colour AND is
    // even. Evenness is the point: a patterned entry would drag its
    // pattern onto the seam. Sixteen samples across the area are enough to
    // tell a solid block from a picture.
    int best = -1;
    long bestDist = 0;
    for (int32_t i = 0; i < m_texinfos.count; ++i)
    {
        const size_t t = m_texinfos.dataAt + static_cast<size_t>(i) * SZ_TEXINFO;
        const uint16_t tp = g16(m_bytes, t + 2);
        const int page = tp & 0x7FFF;
        if (page < 0 || static_cast<size_t>(page) >= nPages) continue;
        const int corners = (tp & 0x8000) ? 3 : 4;

        int loX = 255, hiX = 0, loY = 255, hiY = 0;
        for (int v = 0; v < corners; ++v)
        {
            const int x = (g16(m_bytes, t + 6 + v * 4) >> 8) & 0xFF;
            const int y = (g16(m_bytes, t + 8 + v * 4) >> 8) & 0xFF;
            if (x < loX) loX = x;   if (x > hiX) hiX = x;
            if (y < loY) loY = y;   if (y > hiY) hiY = y;
        }
        if (hiX <= loX || hiY <= loY) continue;

        long sr = 0, sg = 0, sb = 0;
        int n = 0, spread = 0;
        int fr = -1, fg = 0, fb = 0;
        for (int yy = 0; yy < 4; ++yy)
            for (int xx = 0; xx < 4; ++xx)
            {
                const int x = loX + (hiX - loX) * xx / 3;
                const int y = loY + (hiY - loY) * yy / 3;
                uint16_t w;
                std::memcpy(&w, m_tex16.data() + static_cast<size_t>(page) * PAGE
                                               + (static_cast<size_t>(y) * 256 + x) * 2, 2);
                const int r5 = (w >> 10) & 0x1F, g5 = (w >> 5) & 0x1F, b5 = w & 0x1F;
                const int r8 = (r5 << 3) | (r5 >> 2);
                const int g8 = (g5 << 3) | (g5 >> 2);
                const int b8 = (b5 << 3) | (b5 >> 2);
                if (fr < 0) { fr = r8; fg = g8; fb = b8; }
                const int d = abs(r8 - fr) + abs(g8 - fg) + abs(b8 - fb);
                if (d > spread) spread = d;
                sr += r8; sg += g8; sb += b8; ++n;
            }
        if (!n || spread > 12) continue;                 // not an even block

        const long dist = labs(sr / n - r) + labs(sg / n - g) + labs(sb / n - b);
        if (best < 0 || dist < bestDist)
        {
            best = i; bestDist = dist;
            gotR = int(sr / n); gotG = int(sg / n); gotB = int(sb / n);
        }
    }

    if (best < 0) { error = "the level holds no evenly coloured texture entry"; return false; }
    usedTexinfo = best;

    int32_t found = -1;
    for (int32_t i = 0; i < m_objects.count && found < 0; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * SZ_OBJ;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot == targetSlot) found = i;
    }
    if (found < 0) { error = "slot not in the level"; return false; }

    const size_t o = m_objects.dataAt + static_cast<size_t>(found) * SZ_OBJ;
    int16_t nMesh = 0, meshIdx = 0;
    std::memcpy(&nMesh,   m_bytes.data() + o + 4, 2);
    std::memcpy(&meshIdx, m_bytes.data() + o + 6, 2);
    if (meshNo < 0 || meshNo >= nMesh) { error = "mesh number outside the slot"; return false; }

    const size_t pAt = m_meshPtrs.dataAt + (static_cast<size_t>(meshIdx) + static_cast<size_t>(meshNo)) * 4;
    if (pAt + 4 > m_bytes.size()) { error = "mesh pointer outside the file"; return false; }
    uint32_t off; std::memcpy(&off, m_bytes.data() + pAt, 4);

    const size_t at  = m_meshData.dataAt + off;
    const size_t end = m_meshData.dataAt + static_cast<size_t>(m_meshData.count) * 2;
    if (!faceTextures(m_bytes, at, end, [&](size_t texAt)
    {
        s16(m_bytes, texAt, static_cast<uint16_t>(best));
    }))
    { error = "mesh cannot be read"; return false; }

    return true;
}

bool Tr5Level::clearMeshFaces(int targetSlot, int meshNo, std::string& error)
{
    int32_t found = -1;
    for (int32_t i = 0; i < m_objects.count && found < 0; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * SZ_OBJ;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot == targetSlot) found = i;
    }
    if (found < 0) { error = "slot not in the level"; return false; }

    const size_t o = m_objects.dataAt + static_cast<size_t>(found) * SZ_OBJ;
    int16_t nMesh = 0, meshIdx = 0;
    std::memcpy(&nMesh,   m_bytes.data() + o + 4, 2);
    std::memcpy(&meshIdx, m_bytes.data() + o + 6, 2);
    if (meshNo < 0 || meshNo >= nMesh) { error = "mesh number outside the slot"; return false; }

    const size_t pAt = m_meshPtrs.dataAt + (static_cast<size_t>(meshIdx) + static_cast<size_t>(meshNo)) * 4;
    if (pAt + 4 > m_bytes.size()) { error = "mesh pointer outside the file"; return false; }
    uint32_t off; std::memcpy(&off, m_bytes.data() + pAt, 4);

    const size_t at  = m_meshData.dataAt + off;
    const size_t end = m_meshData.dataAt + static_cast<size_t>(m_meshData.count) * 2;

    // Walk the header exactly as the reader does, to land on the quad count.
    size_t p = at + 10;
    if (p + 2 > end) { error = "mesh ends before its vertex count"; return false; }
    const int16_t vw = static_cast<int16_t>(g16(m_bytes, p)); p += 2;
    const int nv = vw & 0xFF;
    if (nv == 0) p += (static_cast<size_t>((vw >> 8) & 0xFF) * 6 + 1) * 2;
    else
    {
        p += static_cast<size_t>(nv) * 6;
        if (p + 2 > end) { error = "mesh ends before its normal count"; return false; }
        const int16_t nn = static_cast<int16_t>(g16(m_bytes, p)); p += 2;
        p += static_cast<size_t>(nv) * (nn >= 0 ? 6 : 2);
    }
    if (p + 4 > end) { error = "mesh ends before its face counts"; return false; }

    // Both counts are written where the QUAD count stands and right behind
    // it. The reader takes the triangle count from there once the quads are
    // gone, so zeroing only the first would leave it reading face data as a
    // count.
    s16(m_bytes, p,     0);
    s16(m_bytes, p + 2, 0);
    return true;
}

bool Tr5Level::meshTones(int targetSlot, int meshNo, std::vector<ToneCount>& out,
                         int& pixels, std::string& error)
{
    out.clear();
    pixels = 0;

    int32_t found = -1;
    for (int32_t i = 0; i < m_objects.count && found < 0; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * SZ_OBJ;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot == targetSlot) found = i;
    }
    if (found < 0) { error = "slot not in the level"; return false; }

    const size_t o = m_objects.dataAt + static_cast<size_t>(found) * SZ_OBJ;
    int16_t nMesh = 0, meshIdx = 0;
    std::memcpy(&nMesh,   m_bytes.data() + o + 4, 2);
    std::memcpy(&meshIdx, m_bytes.data() + o + 6, 2);
    if (meshNo < 0 || meshNo >= nMesh) { error = "mesh number outside the slot"; return false; }

    const size_t pAt = m_meshPtrs.dataAt + (static_cast<size_t>(meshIdx) + static_cast<size_t>(meshNo)) * 4;
    if (pAt + 4 > m_bytes.size()) { error = "mesh pointer outside the file"; return false; }
    uint32_t off; std::memcpy(&off, m_bytes.data() + pAt, 4);

    const size_t meshAt  = m_meshData.dataAt + off;
    const size_t meshEnd = m_meshData.dataAt + static_cast<size_t>(m_meshData.count) * 2;
    if (meshAt >= meshEnd) { error = "mesh lies outside the mesh block"; return false; }

    // drawtype(2), tpage(2), flag(2), then 4 corners of u,v as 8.8 fixed
    // point - and TR5 appends 18 more bytes after those, so the struct is
    // 40 bytes, not the 22 the readable part accounts for. Same value as
    // in Tr5Transfer.cpp, which keeps its constant to itself.
    const size_t SZ_TEXINFO = 40;

    // The 16 bit pages, not the 32 bit ones: ARGB1555 has no channel order
    // to work out, which is one guess fewer.
    if (!texWorkshopOpen(error)) return false;
    const size_t PAGE = 256 * 256 * 2;
    const size_t nPages = m_tex16.size() / PAGE;

    // Grouped to 3 bits per channel, but the group's TRUE mean is reported.
    // At five bits a shaded face falls apart into hundreds of neighbouring
    // shades and the black background of the pages wins with four percent -
    // measured. Grouping puts the skin back together without inventing a
    // colour that is not in the texture.
    struct Bucket { long long r, g, b; int count; };
    std::map<uint32_t, Bucket> hist;
    int faceCount = 0, loTex = 0x7FFFFFFF, hiTex = -1;

    // The effects word sits two bytes behind the texture word, on quads and
    // on triangles alike (12 and 10 byte records, both ending tex, effects).
    // Bit 0 draws additively, bit 1 switches the environment map on, bits
    // 2..6 are its strength - specific/output.cpp. Counted here so the
    // shine of an existing model can be read off instead of guessed.
    std::map<int, int> fxHist;
    // drawtype at +0: 0 draws opaque, 1 and 2 go through the sorted list
    // and blend. An imported face that carries 2 by accident looks washed
    // out in game while a viewer still shows it solid.
    std::map<int, int> drawHist;
    if (!faceTextures(m_bytes, meshAt, meshEnd, [&](size_t texAt)
    {
        const int idx = g16(m_bytes, texAt) & 0x7FFF;
        if (idx < 0 || idx >= m_texinfos.count) return;
        ++faceCount;
        if (idx < loTex) loTex = idx;
        if (idx > hiTex) hiTex = idx;
        fxHist[g16(m_bytes, texAt + 2)]++;
        drawHist[g16(m_bytes, m_texinfos.dataAt + size_t(idx) * SZ_TEXINFO)]++;
        const size_t t = m_texinfos.dataAt + static_cast<size_t>(idx) * SZ_TEXINFO;
        const uint16_t tp = g16(m_bytes, t + 2);
        const int page = tp & 0x7FFF;
        if (page < 0 || static_cast<size_t>(page) >= nPages) return;

        // Bit 15 marks a TRIANGLE, and then the fourth corner is unused and
        // usually zero. Taking it along stretches the rectangle into the
        // empty edge of the page - which is why the first measurement came
        // back mostly black.
        const int corners = (tp & 0x8000) ? 3 : 4;
        int loX = 255, hiX = 0, loY = 255, hiY = 0;
        for (int v = 0; v < corners; ++v)
        {
            const int x = (g16(m_bytes, t + 6 + v * 4) >> 8) & 0xFF;
            const int y = (g16(m_bytes, t + 8 + v * 4) >> 8) & 0xFF;
            if (x < loX) loX = x;   if (x > hiX) hiX = x;
            if (y < loY) loY = y;   if (y > hiY) hiY = y;
        }
        for (int y = loY; y <= hiY; ++y)
            for (int x = loX; x <= hiX; ++x)
            {
                uint16_t w;
                std::memcpy(&w, m_tex16.data() + static_cast<size_t>(page) * PAGE
                                               + (static_cast<size_t>(y) * 256 + x) * 2, 2);
                if (!(w & 0x8000)) continue;                 // fully transparent
                const int r5 = (w >> 10) & 0x1F, g5 = (w >> 5) & 0x1F, b5 = w & 0x1F;
                const int r8 = (r5 << 3) | (r5 >> 2);
                const int g8 = (g5 << 3) | (g5 >> 2);
                const int b8 = (b5 << 3) | (b5 >> 2);
                const uint32_t key = (uint32_t(r8 >> 5) << 6) | (uint32_t(g8 >> 5) << 3) | uint32_t(b8 >> 5);
                Bucket& z = hist[key];
                z.r += r8; z.g += g8; z.b += b8; z.count++;
                ++pixels;
            }
    }))
    { error = "mesh cannot be read"; return false; }

    std::printf("    (mesh has %d textured faces, texinfos %d..%d, %d pages)\n",
                faceCount, loTex, hiTex, int(nPages));
    for (std::map<int, int>::const_iterator it = drawHist.begin(); it != drawHist.end(); ++it)
        std::printf("    drawtype %d on %d faces%s\n", it->first, it->second,
                    it->first ? "  <- blended, not opaque" : "");
    for (std::map<int, int>::const_iterator it = fxHist.begin(); it != fxHist.end(); ++it)
        std::printf("    effects word 0x%04X on %d faces: %s%s strength %d\n",
                    it->first, it->second,
                    (it->first & 1) ? "additive " : "",
                    (it->first & 2) ? "envmap" : "plain",
                    (it->first & 0x7C) << 1);

    for (std::map<uint32_t, Bucket>::const_iterator it = hist.begin(); it != hist.end(); ++it)
    {
        ToneCount tc;
        tc.count = it->second.count;
        tc.r = int(it->second.r / tc.count);
        tc.g = int(it->second.g / tc.count);
        tc.b = int(it->second.b / tc.count);
        out.push_back(tc);
    }
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = i + 1; j < out.size(); ++j)
            if (out[j].count > out[i].count) { ToneCount t = out[i]; out[i] = out[j]; out[j] = t; }
    if (out.size() > 12) out.resize(12);
    return true;
}

bool Tr5Level::addMoveableFromTr3(const std::vector<uint8_t>& package, int targetSlot,
                                  int texinfoIndex, std::string& error,
                                  const std::vector<int>* keepMeshes)
{
    char buf[256];

    // Fetch a fresh block map - same reasoning as at the entry of
    // copyMoveableFrom: preparatory work like addTrigger has shifted the
    // main block without updating the map.
    if (!reparse(error)) { error = "block map: " + error; return false; }

    if (package.size() < 48 || std::memcmp(package.data(), "T35H", 4) != 0)
    { error = "not a TR3 package (magic T35H missing)"; return false; }
    if (targetSlot <= 0) { error = "target slot must be greater than 0"; return false; }
    // texinfoIndex < 0 means: the faces already carry valid texinfos (the
    // textures were inserted beforehand) and are NOT touched.
    if (texinfoIndex >= m_texinfos.count)
    { error = "texinfo index does not exist in the target"; return false; }

    size_t p = 4;
    const uint32_t version   = g32(package, p); p += 4;
    p += 4;                                              // objectId, irrelevant here
    const uint32_t nMeshes   = g32(package, p); p += 4;
    const uint32_t nAnims    = g32(package, p); p += 4;
    const uint32_t nFrameW   = g32(package, p); p += 4;
    const uint32_t nBoneW    = g32(package, p); p += 4;
    const uint32_t nSC       = g32(package, p); p += 4;
    const uint32_t nAD       = g32(package, p); p += 4;
    const uint32_t nCmd      = g32(package, p); p += 4;
    p += 8;                                              // source offsets, already rebased
    // Version 2 appends a texture part behind the command block. It is not
    // read here - that is what addTexturesFromTr3 is for - but it does not
    // hurt either: everything before it sits unchanged at the same place.
    // Version 3 only added a field to the texture part at the very end -
    // the moveable part in front of it is unchanged, so it reads the same.
    if (version < 1 || version > 3) { error = "unknown package version"; return false; }

    // --- Collect the meshes and pack them into one block -------------------
    std::vector<uint8_t> meshPack;
    std::vector<uint32_t> meshStart;
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) { error = "package ends in the mesh list"; return false; }
        const uint32_t len = g32(package, p); p += 4;
        if (p + len > package.size()) { error = "mesh runs out of the package"; return false; }
        if (len & 1) { error = "mesh has odd length"; return false; }
        meshStart.push_back(static_cast<uint32_t>(meshPack.size()));
        meshPack.insert(meshPack.end(), package.begin() + p, package.begin() + p + len);
        p += len;
    }

    // Point all faces at the same existing texinfo. Bit 15 of the texture
    // words carries a flag in TR5; deliberately zeroed cleanly here.
    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        const size_t at = meshStart[i];
        const size_t end = (i + 1 < nMeshes) ? meshStart[i + 1] : meshPack.size();
        // Walk even with texinfoIndex < 0: the walker verifies along the
        // way that every mesh is readable as a TR5 mesh.
        if (!faceTextures(meshPack, at, end,
                       [&](size_t texAt) {
                           if (texinfoIndex >= 0) s16(meshPack, texAt, static_cast<uint16_t>(texinfoIndex));
                       }))
        {
            std::snprintf(buf, sizeof(buf), "mesh %u cannot be read as a TR5 mesh", i);
            error = buf;
            return false;
        }
    }

    auto take = [&](size_t n) -> std::vector<uint8_t> {
        std::vector<uint8_t> v;
        if (p + n <= package.size()) { v.assign(package.begin() + p, package.begin() + p + n); p += n; }
        return v;
    };
    std::vector<uint8_t> boneBytes  = take(static_cast<size_t>(nBoneW) * 4);
    std::vector<uint8_t> frameBytes = take(static_cast<size_t>(nFrameW) * 2);
    std::vector<uint8_t> animBytes  = take(static_cast<size_t>(nAnims) * SZ_ANIM);
    std::vector<uint8_t> chgBytes   = take(static_cast<size_t>(nSC) * SZ_CHANGE);
    std::vector<uint8_t> rngBytes   = take(static_cast<size_t>(nAD) * SZ_RANGE);
    std::vector<uint8_t> cmdBytes   = take(static_cast<size_t>(nCmd) * 2);
    // Version 1 ends exactly here; version 2 appends the texture part that
    // addTexturesFromTr3 reads, which consequently remains here.
    if (version == 1 && p != package.size())
    { error = "package size does not match its header"; return false; }
    if (p > package.size()) { error = "package shorter than its header claims"; return false; }

    // --- Rebase onto the target's counters ---------------------------------
    const int32_t dstMeshBytes = m_meshData.count * 2;
    const int32_t dstPtr       = m_meshPtrs.count;
    const int32_t dstAnim      = m_anims.count;
    const int32_t dstChg       = m_changes.count;
    const int32_t dstRng       = m_ranges.count;
    const int32_t dstCmd       = m_commands.count;
    const int32_t dstBone      = m_bones.count;
    const int32_t dstFrameByte = m_frames.count * 2;

    // The anims come slice-relative from TR3Fetch: frame pointers from 0, jump
    // targets from 0. The appendices (state changes, ranges, commands) were
    // handed over as WHOLE blocks, so their indices are already 0-based -
    // both cases only need the target offset.
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

    // --- Meshes that stay the target's own ---------------------------------
    //
    // Their pointer is left pointing at the mesh the slot already uses, so
    // the imported one is appended but never referenced. Leaving it in place
    // is deliberate: every offset above was computed over the whole pack, and
    // cutting one out would move all the others.
    //
    // For a Lara outfit this is the head (LM_HEAD = 14), and not for looks.
    // TR5 writes lara.mesh_ptrs[LM_HEAD] at RUNTIME - the scream head in
    // larafire.cpp, the speech heads out of the cutscene pak - so a
    // foreign head pops out the moment Lara screams. And the ponytail takes
    // its anchor point and its collision radius straight from the bounding
    // sphere of whatever mesh sits there (hair.cpp), so a foreign head
    // hangs the braid in the wrong place the rest of the time.
    std::vector<uint32_t> keptPtr(nMeshes, 0);
    std::vector<char>     keptHas(nMeshes, 0);
    if (keepMeshes && !keepMeshes->empty())
    {
        int32_t found = -1;
        for (int32_t i = 0; i < m_objects.count && found < 0; ++i)
        {
            const size_t o = m_objects.dataAt + static_cast<size_t>(i) * SZ_OBJ;
            int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
            if (slot == targetSlot) found = i;
        }
        if (found < 0)
        { error = "nothing to keep: the target has no such slot yet"; return false; }

        const size_t o = m_objects.dataAt + static_cast<size_t>(found) * SZ_OBJ;
        int16_t oldN = 0, oldIdx = 0;
        std::memcpy(&oldN,   m_bytes.data() + o + 4, 2);
        std::memcpy(&oldIdx, m_bytes.data() + o + 6, 2);

        for (size_t j = 0; j < keepMeshes->size(); ++j)
        {
            const int k = (*keepMeshes)[j];
            if (k < 0 || static_cast<uint32_t>(k) >= nMeshes)
            { error = "mesh to keep lies outside the package"; return false; }
            if (k >= oldN)
            { error = "mesh to keep lies outside the slot already in the level"; return false; }
            const size_t at = m_meshPtrs.dataAt + (static_cast<size_t>(oldIdx) + static_cast<size_t>(k)) * 4;
            if (at + 4 > m_bytes.size())
            { error = "mesh pointer to keep lies outside the file"; return false; }
            std::memcpy(&keptPtr[k], m_bytes.data() + at, 4);
            keptHas[k] = 1;
        }
    }

    std::vector<uint8_t> ptrBytes;
    for (size_t k = 0; k < meshStart.size(); ++k)
    {
        const uint32_t v = keptHas[k] ? keptPtr[k]
                                      : static_cast<uint32_t>(dstMeshBytes) + meshStart[k];
        ptrBytes.insert(ptrBytes.end(), reinterpret_cast<const uint8_t*>(&v),
                                        reinterpret_cast<const uint8_t*>(&v) + 4);
    }

    uint8_t objRec[SZ_OBJ];
    {
        const int32_t  s = targetSlot;                      std::memcpy(objRec + 0,  &s,  4);
        const int16_t nm = static_cast<int16_t>(nMeshes);   std::memcpy(objRec + 4,  &nm, 2);
        const int16_t mi = static_cast<int16_t>(dstPtr);    std::memcpy(objRec + 6,  &mi, 2);
        const int32_t bi = dstBone;                         std::memcpy(objRec + 8,  &bi, 4);
        const uint32_t fo = static_cast<uint32_t>(dstFrameByte); std::memcpy(objRec + 12, &fo, 4);
        const uint16_t ai = static_cast<uint16_t>(dstAnim); std::memcpy(objRec + 16, &ai, 2);
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

    // --- Bump the counters, then insert everything in one stroke -----------
    auto bump = [&](const Block& b, int32_t d) {
        int32_t v; std::memcpy(&v, m_bytes.data() + b.countAt, 4);
        v += d;
        std::memcpy(m_bytes.data() + b.countAt, &v, 4);
    };
    bump(m_meshData, static_cast<int32_t>(meshPack.size() / 2));
    bump(m_meshPtrs, static_cast<int32_t>(ptrBytes.size() / 4));
    bump(m_anims,    static_cast<int32_t>(animBytes.size() / SZ_ANIM));
    bump(m_changes,  static_cast<int32_t>(chgBytes.size() / SZ_CHANGE));
    bump(m_ranges,   static_cast<int32_t>(rngBytes.size() / SZ_RANGE));
    bump(m_commands, static_cast<int32_t>(cmdBytes.size() / 2));
    bump(m_bones,    static_cast<int32_t>(boneBytes.size() / 4));
    bump(m_frames,   static_cast<int32_t>(frameBytes.size() / 2));
    if (!replaced) bump(m_objects, 1);

    // Collect the insertion points and insert from BACK to front, so the
    // front positions stay valid.
    struct Insertion { size_t at; std::vector<uint8_t> bytes; };
    std::vector<Insertion> e;
    auto append = [&](const Block& b, int elem, std::vector<uint8_t>& bytes) {
        if (!bytes.empty())
            e.push_back({ b.dataAt + static_cast<size_t>(b.count) * elem, std::move(bytes) });
    };
    append(m_meshData, 2, meshPack);
    append(m_meshPtrs, 4, ptrBytes);
    append(m_anims, SZ_ANIM, animBytes);
    append(m_changes, SZ_CHANGE, chgBytes);
    append(m_ranges, SZ_RANGE, rngBytes);
    append(m_commands, 2, cmdBytes);
    append(m_bones, 4, boneBytes);
    append(m_frames, 2, frameBytes);
    if (!replaced)
    {
        std::vector<uint8_t> ob(objRec, objRec + SZ_OBJ);
        e.push_back({ m_objects.dataAt + static_cast<size_t>(m_objects.count) * SZ_OBJ, std::move(ob) });
    }

    size_t added = 0;
    for (const Insertion& x : e) added += x.bytes.size();

    std::stable_sort(e.begin(), e.end(),
                     [](const Insertion& a, const Insertion& b) { return a.at > b.at; });
    for (const Insertion& x : e)
    {
        if (x.at > m_bytes.size()) { error = "insertion point lies past the end of the file"; return false; }
        m_bytes.insert(m_bytes.begin() + x.at, x.bytes.begin(), x.bytes.end());
    }

    // Update the doubly kept main block size - both places.
    m_mainSize += added;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    if (!reparse(error)) { error = "after the install: " + error; return false; }
    return true;
}

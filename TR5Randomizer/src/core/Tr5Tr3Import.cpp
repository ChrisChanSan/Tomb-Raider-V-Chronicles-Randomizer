//----------------------------------------------------------------------------
// Installing a TR3 moveable into a TR5 level (#88).
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
// the spot where a level otherwise falls apart (user requirement). Real
// textures come later via Tr5Transfer.cpp.
//----------------------------------------------------------------------------
#include "Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace
{
    const int SZ_ANIM = 40, SZ_CHANGE = 6, SZ_RANGE = 8, SZ_OBJ = 20;

    // The same values as in the namespace of Tr5Level.cpp (line 794ff
    // there). Deliberately read off, not guessed; if they ever change
    // there, it shows up here because the sound block no longer parses.
    constexpr int SAMPLE_LUT_SIZE = 450;        // MAX_SAMPLES, global/types.h
    constexpr int SZ_SAMPLE_INFO = 8;
    constexpr int MAX_SAMPLES_PLAYABLE = 256;   // DS_Buffers[], see #94

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
    // and fundamentally too high - heard in game, the user cross-checked
    // against the original.
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
        // engine rolls sample = number + random(N) (sound.cpp:280). The
        // count sits in bits 2-5 of the flags. Whoever creates N infos
        // instead walks into the trap from #76.
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
        // certainly right, and is only stretched proportionally here. For
        // the T-Rex the user wanted the roar about 30 percent louder.
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

bool Tr5Level::addMoveableFromTr3(const std::vector<uint8_t>& package, int targetSlot,
                                  int texinfoIndex, std::string& error)
{
    char buf[256];

    // Fetch a fresh block map (#104) - same reasoning as at the entry of
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
    if (version != 1 && version != 2) { error = "unknown package version"; return false; }

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

    std::vector<uint8_t> ptrBytes;
    for (uint32_t st : meshStart)
    {
        const uint32_t v = static_cast<uint32_t>(dstMeshBytes) + st;
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

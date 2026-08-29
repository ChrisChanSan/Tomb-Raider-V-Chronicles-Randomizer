//----------------------------------------------------------------------------
// TR3Insert - inserts a moveable fetched with TR3Fetch into a TR5 level.
//
// Usage: TR3Insert <target.trc> <package.bin> <targetSlot> <output.trc>
//          [texinfo] [sound.bin]
//          [-keepmesh n] [-dropjoint n] [-jointcolor n=r,g,b] [-tone n]
//          [-muted] [-muteslot] [-base] [-volumes]
//        (sound.bin is a sixth POSITIONAL argument; all eight switches and
//         that argument were added after this header was first written)
//
// texinfo (default 0): ALL faces point at this existing texinfo afterwards.
// No texture page is touched - see Tr5Tr3Import.cpp.
//----------------------------------------------------------------------------
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

// Rewrites the SFX numbers of the anim commands in the model package.
//
// Command word 5 (PlaySound) carries two follow-up words: the frame and
// then the SFX number, whose upper bits (12-15) carry execution conditions
// (land only, water only) and MUST stay in place.
//
// Without this step the enemy keeps calling its TR3 numbers, which in the
// target are different sounds or none at all. The same omission at the
// dispatch table was the crash in #88 - hence explicit here.
static int remapSfx(std::vector<uint8_t>& package, const std::vector<std::pair<int,int>>& mapping,
                      std::vector<int>& unresolved, std::vector<std::pair<int,int>>& used,
                      std::vector<std::pair<int,int>>& kinds)
{
    auto g32 = [&](size_t p) { uint32_t v; std::memcpy(&v, &package[p], 4); return v; };
    auto g16 = [&](size_t p) { uint16_t v; std::memcpy(&v, &package[p], 2); return v; };

    // Skip the model package's header to get to the command block.
    const uint32_t nMeshes = g32(12), nAnims = g32(16), nFrameW = g32(20);
    const uint32_t nBoneW = g32(24), nSC = g32(28), nAD = g32(32), nCmd = g32(36);
    size_t p = 48;
    for (uint32_t i = 0; i < nMeshes; ++i) { const uint32_t l = g32(p); p += 4 + l; }
    p += size_t(nBoneW) * 4 + size_t(nFrameW) * 2 + size_t(nAnims) * 40
       + size_t(nSC) * 6 + size_t(nAD) * 8;
    const size_t cmdAt = p;
    if (cmdAt + size_t(nCmd) * 2 > package.size()) return -1;

    // Only touch the commands that THIS object's animations point at. The
    // package carries the whole command block of the source level along
    // (the anims reference into it with global indices) - whoever bluntly
    // walks over everything also sees the sounds of raptors, doors and
    // Lara and reports dozens of "missing" mappings nobody cares about.
    // Per anim the count is at +36 and the start index at +38.
    const size_t animAt = cmdAt - size_t(nAD) * 8 - size_t(nSC) * 6 - size_t(nAnims) * 40;

    int swapped = 0;
    for (uint32_t a = 0; a < nAnims; ++a)
    {
        const size_t ao = animAt + size_t(a) * 40;
        const uint16_t count = g16(ao + 36);
        if (!count) continue;
        const uint16_t start = g16(ao + 38);

    // THE COUNT COUNTS COMMANDS, NOT WORDS.
    //
    // "i < start + count" stood here with i as a WORD index - and because
    // a command occupies 1 to 4 words depending on its kind, the loop
    // broke off far too early. It saw only the first few commands of an
    // animation, the rest stayed untouched AND unnoticed: the "without a
    // mapping" report can only report what it read at all.
    //
    // It surfaced with the Compsognathus. His walking animation calls the
    // step several times; the first occurrence was rerouted, the later
    // ones kept their TR3 number 243 - and 243 in TR5 is
    // SFX_GENERIC_HEAVY_FEET, a hefty heavy step. Exactly that is what the
    // user heard while all measurements on model, mapping, soundmap and
    // wave data stayed clean. It only became visible through the sound log
    // in the engine.
    uint32_t i = start;
    for (uint16_t k = 0; k < count && i < nCmd; ++k)
    {
        const uint16_t kind = g16(cmdAt + size_t(i) * 2);
        ++i;
        int follow = 0;
        switch (kind)
        {
        case 1: follow = 3; break;   // SetPosition
        case 2: follow = 2; break;   // JumpVelocity
        case 5: follow = 2; break;   // PlaySound: frame + SFX
        case 6: follow = 2; break;   // Flipeffect
        default: follow = 0; break;  // EmptyHands, KillItem, unknown
        }

        // Count the command kinds along. A flipeffect (6) carries a number
        // that has DIFFERENT effects in TR3 and TR5 and in TR5 can very
        // well trigger a sound - it is NOT rerouted here. Whoever looks
        // only at the PlaySounds misses that.
        kinds.push_back(std::make_pair((int)kind, (int)a));

        if (kind == 5 && i + 1 < nCmd)
        {
            const size_t at = cmdAt + size_t(i + 1) * 2;
            const uint16_t w = g16(at);
            const int nr = w & 0x0FFF;
            bool mapped = false;
            for (const auto& z : mapping)
                if (z.first == nr)
                {
                    const uint16_t fresh = static_cast<uint16_t>((w & 0xF000) | (z.second & 0x0FFF));
                    std::memcpy(&package[at], &fresh, 2);
                    ++swapped;
                    mapped = true;
                    // Count WHICH ID is called how often and in which
                    // animation. A mere total does not say whether the
                    // sought sound occurs at all - with the Compy I
                    // wrongly took the walking sound as covered because
                    // the sum matched.
                    used.push_back(std::make_pair(nr, (int)a));
                    break;
                }

            // REPORT unmapped numbers. Otherwise they silently keep their
            // TR3 number and hit some foreign sound in the target -
            // exactly the sort of silent error that has cost this project
            // time several times already.
            if (!mapped)
            {
                bool alreadyReported = false;
                for (int o : unresolved) if (o == nr) { alreadyReported = true; break; }
                if (!alreadyReported) unresolved.push_back(nr);
            }
        }
        i += follow;
    }
    }
    return swapped;
}

static uint32_t packageVersion(const std::vector<uint8_t>& p)
{
    if (p.size() < 8) return 0;
    uint32_t v; std::memcpy(&v, p.data() + 4, 4); return v;
}

// Rewrites the faces' texture words onto the new texinfos. The same walker
// as everywhere (header 10 bytes, then the vertex word; 0 normals means
// NORMALS). Bit 15 of the texture word is a flag and stays in place.
static int remapTextures(std::vector<uint8_t>& package, const std::map<int,int>& mapping)
{
    auto g32 = [&](size_t q) { uint32_t v; std::memcpy(&v, &package[q], 4); return v; };
    auto g16 = [&](size_t q) { uint16_t v; std::memcpy(&v, &package[q], 2); return v; };

    const uint32_t nMeshes = g32(12);
    size_t p = 48;
    int swapped = 0;

    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) return -1;
        const uint32_t len = g32(p); p += 4;
        const size_t end = p + len;
        size_t q = p + 10;
        if (q + 2 > end) return -1;
        const int16_t vw = static_cast<int16_t>(g16(q)); q += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) q += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            q += size_t(nv) * 6;
            const int16_t nn = static_cast<int16_t>(g16(q)); q += 2;
            q += size_t(nv) * (nn >= 0 ? 6 : 2);
        }

        auto reroute = [&](size_t at) {
            const uint16_t w = g16(at);
            std::map<int,int>::const_iterator f = mapping.find(w & 0x7FFF);
            if (f == mapping.end()) return;
            const uint16_t fresh = static_cast<uint16_t>((w & 0x8000) | (f->second & 0x7FFF));
            std::memcpy(&package[at], &fresh, 2);
            ++swapped;
        };

        const int16_t nq = static_cast<int16_t>(g16(q)); q += 2;
        for (int k = 0; k < nq; ++k, q += 12) reroute(q + 8);
        const int16_t nt = static_cast<int16_t>(g16(q)); q += 2;
        for (int k = 0; k < nt; ++k, q += 10) reroute(q + 6);

        p = end;
    }
    return swapped;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("usage: TR3Insert <target.trc> <package.bin> <targetSlot> <output.trc> [texinfo] [sound.bin] [-keepmesh n]\n");
        return 2;
    }
    const int slot = std::atoi(argv[3]);

    // -keepmesh <n>, repeatable: that mesh is NOT taken from the package,
    // the slot keeps the one it already has. Read here and not down in the
    // sound block, so it also works without a sound package.
    std::vector<int> keepMeshes;
    std::vector<int> dropJoints;
    struct JointColor { int joint, r, g, b; };
    std::vector<JointColor> jointColors;
    int toneMesh = -1;
    for (int a = 5; a < argc; ++a)
    {
        if (std::strcmp(argv[a], "-keepmesh") == 0 && a + 1 < argc)
            keepMeshes.push_back(std::atoi(argv[++a]));
        // -tone <mesh>: only report which colours that mesh of the TARGET
        // wears, before anything is replaced. For matching an imported flat
        // colour to the skin the target's own Lara already has.
        else if (std::strcmp(argv[a], "-tone") == 0 && a + 1 < argc)
            toneMesh = std::atoi(argv[++a]);
        // -dropjoint <n>, repeatable: seam n of Lara's joint shell draws
        // nothing any more. Seam n sits in mesh n+1 of LARA_SKIN_JOINTS
        // (laraskin.cpp: jointMeshNum = i + 1). Wanted for an imported
        // outfit, where a seam TR5's joints were not built for can look
        // worse than the gap it was meant to cover.
        else if (std::strcmp(argv[a], "-dropjoint") == 0 && a + 1 < argc)
            dropJoints.push_back(std::atoi(argv[++a]));
        // -jointcolor <n>=r,g,b, repeatable: seam n is painted in one flat
        // colour instead of TR5's own texture. For an imported outfit, where
        // the seams otherwise wear the colours of a body that is no longer
        // there.
        else if (std::strcmp(argv[a], "-jointcolor") == 0 && a + 1 < argc)
        {
            JointColor jc;
            if (std::sscanf(argv[++a], "%d=%d,%d,%d", &jc.joint, &jc.r, &jc.g, &jc.b) == 4)
                jointColors.push_back(jc);
            else
                std::printf("  -jointcolor not readable: %s (expected n=r,g,b)\n", argv[a]);
        }
    }

    // Positional arguments 5 and 6, switches may follow afterwards (-base,
    // -volumes). A word starting with '-' is a switch, not a value -
    // otherwise argc would count along and the sound path would silently
    // fall back to NULL.
    // The FIRST word starting with '-' ends the positional part: everything
    // behind it belongs to a switch, its value words included. Without that
    // the value of a switch is taken for the next positional argument - the
    // sound path swallowed the 14 of "-keepmesh 14".
    int firstSwitch = argc;
    for (int a = 5; a < argc; ++a) if (argv[a][0] == '-') { firstSwitch = a; break; }
    auto positional = [&](int i) -> const char* {
        return (i < firstSwitch && argc > i && argv[i][0] != '-') ? argv[i] : 0; };
    const char* texArg    = positional(5);
    const int   tex       = texArg ? std::atoi(texArg) : 0;
    const char* soundFile = positional(6);

    std::vector<uint8_t> package;
    {
        FILE* f = std::fopen(argv[2], "rb");
        if (!f) { std::printf("package not readable: %s\n", argv[2]); return 2; }
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        package.resize(static_cast<size_t>(n));
        const size_t g = std::fread(package.data(), 1, package.size(), f);
        std::fclose(f);
        if (g != package.size()) { std::printf("package read incompletely\n"); return 2; }
    }

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("level load: %s\n", err.c_str()); return 2; }

    Tr5Level::MoveableInfo before;
    const bool had = lvl.moveableInfo(slot, before);
    std::printf("target %s: %d items, slot %d %s\n", argv[1], lvl.itemCount(), slot,
                had ? "present (will be replaced)" : "free (will be appended)");

    if (toneMesh >= 0)
    {
        std::vector<Tr5Level::ToneCount> tones;
        int px = 0;
        if (!lvl.meshTones(slot, toneMesh, tones, px, err))
            std::printf("  tone of mesh %d: %s\n", toneMesh, err.c_str());
        else
        {
            std::printf("  colours of slot %d mesh %d, %d pixels:\n", slot, toneMesh, px);
            for (size_t k = 0; k < tones.size(); ++k)
                std::printf("    rgb %3d,%3d,%3d  %6d px  %4.1f%%\n",
                            tones[k].r, tones[k].g, tones[k].b, tones[k].count,
                            px ? 100.0 * tones[k].count / px : 0.0);
        }
    }

    // Sounds FIRST: only afterwards is it fixed which places the SFX
    // numbers of the anim commands must point at - and the commands sit in
    // the model package that is about to be built in.
    if (soundFile)
    {
        std::vector<uint8_t> sound;
        FILE* f = std::fopen(soundFile, "rb");
        if (!f) { std::printf("sound package not readable: %s\n", soundFile); return 2; }
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        sound.resize(static_cast<size_t>(n));
        const size_t g = std::fread(sound.data(), 1, sound.size(), f);
        std::fclose(f);
        if (g != sound.size()) { std::printf("sound package incomplete\n"); return 2; }

        const int beforeSamples = lvl.soundSampleCount();
        std::vector<std::pair<int,int>> mapping;

        // 446..449: free in all 16 levels. The soundmap is a fixed field of
        // 450 places, so a high place costs no byte.
        // Volume per sound in package order (84 attack, 87 roar, 88 foot
        // stomp, 89 sniffing, 255 raptor death scream). The roar shall be
        // about 30 percent louder - heard in game, the user's wish.
        // All listened to in game and set by the user. ATTENTION: the base
        // value is 191 and the field one byte - everything above 133
        // percent lands at the cap 255. Roar and death lie there, so their
        // percentages are the wish, not the result.
        std::vector<int> volumes = { 100, 130, 150, 100, 140 };

        // -muted <id,...>: deliberately reroute these numbers onto an
        // EMPTY place of the soundmap instead of leaving them in place.
        //
        // 411 is unoccupied in all 16 TR5 levels - measured with ModelOffset
        // -ton on every single one, not assumed. The first candidate 405
        // was occupied in rich3; free are 411 to 419. The place lies
        // outside the ranges we assign for imports (420-449).
        std::vector<int> muted;
        int muteSlot = 411;

        // 445..449: five places at the upper end, free in all levels.
        int base = 445;

        // Since the raptor (#97), the base place and the volumes are
        // adjustable - before, the T-Rex values were fixed in the tool.
        // Two enemies do not share the top edge of the soundmap by
        // themselves:
        //   -base N          first target place
        //   -volumes a,b,c,..  percent per sound in package order
        for (int a = 5; a < argc; ++a)
        {
            if (std::strcmp(argv[a], "-muted") == 0 && a + 1 < argc)
            {
                const char* p = argv[++a];
                while (*p)
                {
                    muted.push_back(std::atoi(p));
                    while (*p && *p != ',') ++p;
                    if (*p == ',') ++p;
                }
            }
            else if (std::strcmp(argv[a], "-muteslot") == 0 && a + 1 < argc)
                muteSlot = std::atoi(argv[++a]);
            else if (std::strcmp(argv[a], "-base") == 0 && a + 1 < argc)
                base = std::atoi(argv[++a]);
            else if (std::strcmp(argv[a], "-volumes") == 0 && a + 1 < argc)
            {
                volumes.clear();
                const char* p = argv[++a];
                while (*p)
                {
                    volumes.push_back(std::atoi(p));
                    while (*p && *p != ',') ++p;
                    if (*p == ',') ++p;
                }
            }
        }
        std::printf("  target places from %d, %d volume entries\n",
                    base, static_cast<int>(volumes.size()));

        if (!lvl.addSoundsFromTr3(sound, base, mapping, err, &volumes))
        { std::printf("SOUNDS FAILED: %s\n", err.c_str()); return 1; }

        std::printf("  sounds: %d -> %d samples\n", beforeSamples, lvl.soundSampleCount());
        for (const auto& z : mapping)
            std::printf("    TR3 sound %3d  ->  map %3d\n", z.first, z.second);

        std::vector<int> unresolved;
        std::vector<std::pair<int,int>> used;
        std::vector<std::pair<int,int>> kinds;
        const int n2 = remapSfx(package, mapping, unresolved, used, kinds);

        // Show per ID how often and from which animation it is called. A
        // mere total does not suffice: with the Compy the sum matched, and
        // it was still open whether the walking sound occurs at all.
        for (const auto& z : mapping)
        {
            std::string where;
            int times = 0;
            for (const auto& g : used)
                if (g.first == z.first)
                {
                    ++times;
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%s%d", where.empty() ? "" : ",", g.second);
                    where += buf;
                }
            std::printf("    ID %3d -> map %3d : %dx%s%s\n", z.first, z.second, times,
                        times ? ", anim " : " - IS NEVER CALLED", where.c_str());
        }

        // List all command kinds. PlaySound is not the only one that
        // becomes audible in the target: a flipeffect carries a number
        // with different effects in TR3 and TR5, and is NOT rerouted.
        {
            const char* name[8] = { "?", "SetPosition", "JumpVelocity", "EmptyHands",
                                    "KillItem", "PlaySound", "Flipeffect", "?" };
            for (int k = 1; k <= 6; ++k)
            {
                std::string where;
                int times = 0;
                for (const auto& s : kinds)
                    if (s.first == k)
                    {
                        ++times;
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%s%d", where.empty() ? "" : ",", s.second);
                        where += buf;
                    }
                if (times)
                    std::printf("    command %d (%s): %dx, anim %s\n", k, name[k], times, where.c_str());
            }
        }
        if (n2 < 0) { std::printf("command block in the model package not findable\n"); return 1; }
        std::printf("  anim commands: %d SFX numbers rerouted\n", n2);
        // ABORT HARD, do not just warn.
        //
        // A TR3 number left in place hits some vanilla sound in the
        // target. That is no cosmetic flaw: with the Compsognathus, 243
        // stayed and became SFX_GENERIC_HEAVY_FEET - a hefty heavy step in
        // the middle of the right sounds. The hunt for it cost hours,
        // because all measurements on the files were clean and only a
        // WARNING stood in the build log, which one reads past.
        //
        // Where the target places lie does not help against that: they lie
        // high anyway (420-449) and cannot collide with TR3 numbers (up to
        // about 256). The protection must take hold at the source - every
        // called number MUST have a mapping.
        //
        // Whoever deliberately does not want to take a sound along
        // reroutes it onto an own silent place. Simply leaving it is no
        // longer an option.
        // Deliberately muted numbers: they are rerouted onto an EMPTY
        // place of the soundmap. The engine bails out there (sound.cpp:
        // lut == -1 -> "Non present sample") and plays nothing. That costs
        // no sample and is guaranteed to hit nothing foreign.
        //
        // Needed for sounds we do not want to take from the source game:
        // TR3 143 "Falling" with the T-Rex and the raptor, TR3 56 (Lara's
        // step) with the vulture. Leaving them is no option - 143 and 56
        // are occupied in TR5 (SFX_GENERIC_BODY_FALL, SFX_LARA_GRABFEET).
        if (!unresolved.empty() && !muted.empty())
        {
            std::vector<std::pair<int,int>> toMute;
            for (int o : unresolved)
                for (int s : muted)
                    if (o == s) toMute.push_back(std::make_pair(o, muteSlot));

            if (!toMute.empty())
            {
                std::vector<int> rest;
                std::vector<std::pair<int,int>> ignore;
                remapSfx(package, toMute, rest, ignore, ignore);
                std::printf("  muted onto empty place %d:", muteSlot);
                for (const auto& z : toMute) std::printf(" %d", z.first);
                std::printf("\n");

                std::vector<int> remaining;
                for (int o : unresolved)
                {
                    bool gone = false;
                    for (const auto& z : toMute) if (z.first == o) gone = true;
                    if (!gone) remaining.push_back(o);
                }
                unresolved = remaining;
            }
        }

        if (!unresolved.empty())
        {
            std::printf("\nABORT: %zu SFX numbers WITHOUT a mapping. They would keep\n"
                        "their TR3 number and hit a foreign sound in the target:\n   ",
                        unresolved.size());
            for (int o : unresolved) std::printf("%d ", o);
            std::printf("\n\nTake them into the sound package (TR3Sound -waves) or reroute\n"
                        "them deliberately onto a silent place. NOTHING was written.\n");
            return 1;
        }
    }

    // Textures BEFORE the model: only afterwards is it fixed which texinfos
    // the faces must point at - and the faces sit in the model package.
    // The same order as with the sounds.
    int texBase = tex;
    if (packageVersion(package) >= 2)
    {
        std::map<int,int> texMap;
        if (!lvl.addTexturesFromTr3(package, texMap, err))
        { std::printf("TEXTURES FAILED: %s\n", err.c_str()); return 1; }
        std::printf("  textures: %s\n", err.c_str());

        const int n = remapTextures(package, texMap);
        if (n < 0) { std::printf("meshes in the package not readable\n"); return 1; }
        std::printf("  faces: %d texture words rerouted\n", n);
        texBase = -1;                  // faces are right now, do not overwrite
    }

    for (size_t k = 0; k < keepMeshes.size(); ++k)
        std::printf("  mesh %d stays the target's own\n", keepMeshes[k]);
    if (!lvl.addMoveableFromTr3(package, slot, texBase, err, &keepMeshes))
    { std::printf("INSERT FAILED: %s\n", err.c_str()); return 1; }

    for (size_t k = 0; k < dropJoints.size(); ++k)
    {
        // 9 is LARA_SKIN_JOINTS (game/objects.h).
        if (!lvl.clearMeshFaces(9, dropJoints[k] + 1, err))
            std::printf("  seam %d not dropped: %s\n", dropJoints[k], err.c_str());
        else
            std::printf("  seam %d dropped (joint mesh %d draws nothing)\n",
                        dropJoints[k], dropJoints[k] + 1);
    }

    for (size_t k = 0; k < jointColors.size(); ++k)
    {
        int used = 0, gr = 0, gg = 0, gb = 0;
        if (!lvl.paintMeshFlat(9, jointColors[k].joint + 1,
                               jointColors[k].r, jointColors[k].g, jointColors[k].b,
                               used, gr, gg, gb, err))
            std::printf("  seam %d not painted: %s\n", jointColors[k].joint, err.c_str());
        else
            std::printf("  seam %d painted rgb %d,%d,%d (wanted %d,%d,%d, texinfo %d)\n",
                        jointColors[k].joint, gr, gg, gb,
                        jointColors[k].r, jointColors[k].g, jointColors[k].b, used);
    }

    Tr5Level::MoveableInfo mi;
    if (!lvl.moveableInfo(slot, mi)) { std::printf("slot %d not findable after the insert\n", slot); return 1; }
    std::printf("  slot %d: %d meshes, meshIdx %d, bone %d, frameOffset %u, anim %d\n",
                slot, mi.nmeshes, mi.meshIndex, mi.boneIndex,
                static_cast<unsigned>(mi.frameOffset), mi.animIndex);

    // Cross-check: do the built-in meshes run through the engine's walker?
    int bad = 0;
    for (int i = 0; i < mi.nmeshes; ++i)
    {
        int nv, nn, q, t, bytes;
        if (!lvl.debugMeshWalk(mi.meshIndex + i, nv, nn, q, t, bytes)) ++bad;
    }
    std::printf("  mesh check: %d of %d not readable\n", bad, mi.nmeshes);

    // Pack the texture pages back down before saving. Adding pages opens the
    // texture workshop (#104), and save refuses while it is open. Closing is a
    // no-op when nothing was added, so it can stand unconditionally.
    if (!lvl.texWorkshopClose(err)) { std::printf("texture workshop: %s\n", err.c_str()); return 1; }

    if (!lvl.save(toW(argv[4]), err)) { std::printf("save: %s\n", err.c_str()); return 1; }
    std::printf("  written: %s\n", argv[4]);
    return bad ? 1 : 0;
}

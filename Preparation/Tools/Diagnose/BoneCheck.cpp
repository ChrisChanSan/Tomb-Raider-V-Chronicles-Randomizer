/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */

// BoneCheck - show a moveable slot's bone tree, or compare two; or follow
// a mesh's sphere through an animation (-reach).
//
// A family stub (meshes from donor A) runs at runtime on the base type's
// anim set (from donor B). The frames carry rotations per mesh, but the
// STACK COMMANDS (push/pop in bone[0] bits 0/1) come from the stub's bone
// tree. If the two donors' trees are not built alike, the matrix stack
// drifts apart while drawing - meshes anywhere, body invisible, bounds
// still valid.
//
// Usage: BoneCheck <level.trc> <slot> [<level2.trc> <slot2>]
//   One pair:  show the tree (flags x y z per bone).
//   Two pairs: both trees side by side, deviations marked.
//
// Bone stream: per mesh after the first, 4 longs (flags, x, y, z);
// flags bit0 = pop, bit1 = push, bits 2..4 = rotation channels from
// item->data.
//
// Usage: BoneCheck <level.trc> <slot> -reach <mesh|all> <anim>
//                  [<first> <last>] [-meshes <level.trc> <slot>]
//   Where a mesh's sphere goes during an animation - the reach of a strike
//   (#291). The arithmetic is GetSpheres' (game\sphere.cpp), the spheres
//   TestCollision sets the touch bits from: the item at the origin facing
//   +z, the frame's root offset, its rotations (gar_RotYXZsuperpack), the
//   bones' push/pop and offsets, then the mesh's sphere centre. The joints
//   a control turns (CreatureJoint, item->data) stand at zero, so this is
//   the animation's own strike, aimed straight ahead. The frame is the one
//   GetBestFrame takes (draw.cpp): the nearer key, the later past half.
//     <mesh>   the mesh whose sphere is followed; all = every mesh
//     <anim>   the animation within the slot's set, 0 = its first
//     <first> <last>  frames within the animation, 0 = its first frame;
//              every frame when left out
//     -meshes  the spheres out of another record: a composite's mesh
//              donor (joby2 439 for the brawler, whose bones and frames
//              are the chef's)
//   The animation's record comes first (state, frames, rate, speed,
//   successor, the state changes with their frame ranges), then per frame
//   the centre x (+ = right), y (down), z (forward), the radius, the front
//   z + r (how far ahead of the pivot the sphere reaches), the centre's
//   angle off the facing in degrees and in the engine's units (65536 a
//   turn; + = right, the sign of info.angle), the band y - r .. y + r and
//   the frame box's z range (TestBoundsCollide tests Lara against that
//   box). Last the frame of the largest front.
//
// Build (64 bit): bbone.bat next to this file.
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

static int16_t r16(const std::vector<uint8_t>& b, size_t at)
{ int16_t v; std::memcpy(&v, &b[at], 2); return v; }
static uint16_t u16(const std::vector<uint8_t>& b, size_t at)
{ uint16_t v; std::memcpy(&v, &b[at], 2); return v; }
static int32_t s32(const std::vector<uint8_t>& b, size_t at)
{ int32_t v; std::memcpy(&v, &b[at], 4); return v; }

// A 3x4 matrix, the engine's phd_mxptr without the fixed point.
struct Mx { double m[3][4]; };

static const double kPi = 3.14159265358979323846;

static Mx ident()
{
    Mx a; std::memset(&a, 0, sizeof a);
    a.m[0][0] = a.m[1][1] = a.m[2][2] = 1;
    return a;
}

// phd_RotX, phd_RotY, phd_RotZ, phd_TranslateRel (specific\3dmath.cpp).
static void rotX(Mx& a, int ang)
{
    if (!ang) return;
    const double t = ang * 2 * kPi / 65536.0, s = std::sin(t), c = std::cos(t);
    for (int r = 0; r < 3; ++r)
    {
        const double m1 = c * a.m[r][1] + s * a.m[r][2];
        const double m2 = c * a.m[r][2] - s * a.m[r][1];
        a.m[r][1] = m1; a.m[r][2] = m2;
    }
}

static void rotY(Mx& a, int ang)
{
    if (!ang) return;
    const double t = ang * 2 * kPi / 65536.0, s = std::sin(t), c = std::cos(t);
    for (int r = 0; r < 3; ++r)
    {
        const double m0 = c * a.m[r][0] - s * a.m[r][2];
        const double m2 = c * a.m[r][2] + s * a.m[r][0];
        a.m[r][0] = m0; a.m[r][2] = m2;
    }
}

static void rotZ(Mx& a, int ang)
{
    if (!ang) return;
    const double t = ang * 2 * kPi / 65536.0, s = std::sin(t), c = std::cos(t);
    for (int r = 0; r < 3; ++r)
    {
        const double m0 = c * a.m[r][0] + s * a.m[r][1];
        const double m1 = c * a.m[r][1] - s * a.m[r][0];
        a.m[r][0] = m0; a.m[r][1] = m1;
    }
}

static void trans(Mx& a, double x, double y, double z)
{
    for (int r = 0; r < 3; ++r)
        a.m[r][3] += x * a.m[r][0] + y * a.m[r][1] + z * a.m[r][2];
}

// gar_RotYXZsuperpack (game\draw.cpp): one word for a single axis, two
// for all three (Y, X, Z, ten bits each).
static void superpack(Mx& a, const std::vector<uint8_t>& b, size_t& at)
{
    const uint16_t w = u16(b, at);
    switch (w >> 14)
    {
    case 0:
    {
        const uint32_t packed = (uint32_t(w) << 16) | u16(b, at + 2);
        rotY(a, short(((packed >> 10) & 0x3FF) << 6));
        rotX(a, short(((packed >> 20) & 0x3FF) << 6));
        rotZ(a, short((packed & 0x3FF) << 6));
        at += 4;
        break;
    }
    case 1: rotX(a, short((w & 0xFFF) << 4)); at += 2; break;
    case 2: rotY(a, short((w & 0xFFF) << 4)); at += 2; break;
    default: rotZ(a, short((w & 0xFFF) << 4)); at += 2; break;
    }
}

// The matrix of every mesh for the frame at byte fat, as GetSpheres
// builds them with WorldSpace 1 and the item's rotation zero.
static std::vector<Mx> poseFrame(const Tr5Level& l, const Tr5Level::MoveableInfo& mi, size_t fat)
{
    const std::vector<uint8_t>& b = l.debugBytes();
    std::vector<Mx> out, stack;
    Mx m = ident();
    trans(m, r16(b, fat + 12), r16(b, fat + 14), r16(b, fat + 16));
    size_t rot = fat + 18;
    for (int i = 0; i < mi.nmeshes; ++i)
    {
        if (i > 0)
        {
            const long long bi = mi.boneIndex + (long long)(i - 1) * 4;
            const int32_t fl = l.debugReadBoneLong(bi);
            if ((fl & 1) && !stack.empty()) { m = stack.back(); stack.pop_back(); }
            if (fl & 2) stack.push_back(m);
            trans(m, l.debugReadBoneLong(bi + 1), l.debugReadBoneLong(bi + 2), l.debugReadBoneLong(bi + 3));
        }
        superpack(m, b, rot);
        out.push_back(m);
    }
    return out;
}

static int reach(int argc, char** argv)
{
    // argv: exe level slot -reach mesh anim [first last] [-meshes level slot]
    if (argc < 6)
    {
        std::printf("usage: BoneCheck <level.trc> <slot> -reach <mesh|all> <anim> [<first> <last>]"
                    " [-meshes <level.trc> <slot>]\n");
        return 2;
    }

    int rest = argc;
    const char* meshLevel = nullptr;
    const char* meshSlot = nullptr;
    if (argc >= 9 && std::strcmp(argv[argc - 3], "-meshes") == 0)
    {
        meshLevel = argv[argc - 2];
        meshSlot = argv[argc - 1];
        rest = argc - 3;
    }
    if (rest != 6 && rest != 8)
    {
        std::printf("reach: give the animation alone or with <first> <last>\n");
        return 2;
    }

    Tr5Level a, m;
    std::string e;
    if (!a.load(toW(argv[1]), e)) { std::printf("not loadable: %s\n", e.c_str()); return 1; }
    Tr5Level::MoveableInfo ma, mm;
    if (!a.moveableInfo(std::atoi(argv[2]), ma)) { std::printf("slot %s missing\n", argv[2]); return 1; }

    const Tr5Level* ml = &a;
    mm = ma;
    if (meshLevel)
    {
        if (!m.load(toW(meshLevel), e)) { std::printf("mesh level not loadable: %s\n", e.c_str()); return 1; }
        if (!m.moveableInfo(std::atoi(meshSlot), mm)) { std::printf("slot %s missing in the mesh level\n", meshSlot); return 1; }
        if (mm.nmeshes != ma.nmeshes)
        {
            std::printf("mesh counts differ: %d against %d\n", mm.nmeshes, ma.nmeshes);
            return 1;
        }
        ml = &m;
    }

    const bool all = std::strcmp(argv[4], "all") == 0;
    const int mesh = all ? -1 : std::atoi(argv[4]);
    if (!all && (mesh < 0 || mesh >= ma.nmeshes))
    {
        std::printf("mesh %d outside 0..%d\n", mesh, ma.nmeshes - 1);
        return 1;
    }

    const std::vector<uint8_t>& b = a.debugBytes();
    size_t animAt = 0; int32_t nAnims = 0;
    size_t objAt = 0, frAt = 0; int32_t objN = 0, frN = 0;
    size_t chAt = 0, rgAt = 0; int32_t nCh = 0, nRg = 0;
    a.debugAnimBlock(animAt, nAnims);
    a.debugModelBlocks(objAt, objN, frAt, frN);
    a.debugChangeBlock(chAt, nCh);
    a.debugRangeBlock(rgAt, nRg);

    const int animRel = std::atoi(argv[5]);
    const int anim = ma.animIndex + animRel;
    if (ma.animIndex < 0 || anim < 0 || anim >= nAnims)
    {
        std::printf("animation %d outside the table (%d anims)\n", anim, nAnims);
        return 1;
    }

    // The 40-byte record (Tr5Level.cpp, ANIM_STRUCT).
    const size_t ao = animAt + (size_t)anim * 40;
    const uint32_t fptr = (uint32_t)s32(b, ao);
    const int rate = b[ao + 4] ? b[ao + 4] : 1;
    const int fsize = b[ao + 5];
    const int state = u16(b, ao + 6);
    const double speed = s32(b, ao + 8) / 65536.0, accel = s32(b, ao + 12) / 65536.0;
    const double side = s32(b, ao + 16) / 65536.0;
    const int fbase = u16(b, ao + 24), fend = u16(b, ao + 26);
    const int next = u16(b, ao + 28), nextFrame = u16(b, ao + 30);
    const int nChanges = u16(b, ao + 32), chIndex = u16(b, ao + 34);

    std::printf("%s slot %s: anim %d (+%d), state %d, frames %d..%d (%d), rate %d, size %d\n",
                argv[1], argv[2], anim, animRel, state, fbase, fend, fend - fbase + 1, rate, fsize);
    std::printf("  speed %.2f accel %.4f side %.2f, then anim %d (+%d) frame %d\n",
                speed, accel, side, next, next - ma.animIndex, nextFrame);
    for (int c = 0; c < nChanges && chIndex + c < nCh; ++c)
    {
        const size_t co = chAt + (size_t)(chIndex + c) * 6;
        const int goal = u16(b, co), nr = u16(b, co + 2), ri = u16(b, co + 4);
        std::printf("  change to state %d:", goal);
        for (int r = 0; r < nr && ri + r < nRg; ++r)
        {
            const size_t ro = rgAt + (size_t)(ri + r) * 8;
            const int lo = r16(b, ro), hi = r16(b, ro + 2), to = r16(b, ro + 4), tf = r16(b, ro + 6);
            std::printf(" frames %d..%d -> anim +%d frame %d;", lo, hi, to - ma.animIndex, tf);
        }
        std::printf("\n");
    }

    int first = 0, last = fend - fbase;
    if (rest == 8)
    {
        first = std::atoi(argv[6]);
        last = std::atoi(argv[7]);
    }
    if (first < 0 || last > fend - fbase || first > last)
    {
        std::printf("frames %d..%d outside 0..%d\n", first, last, fend - fbase);
        return 1;
    }

    // The sphere(s): x, y, z, r from the mesh header.
    std::vector<int16_t> sph((size_t)ma.nmeshes * 4);
    for (int i = 0; i < ma.nmeshes; ++i)
    {
        int16_t h[6];
        if (!ml->debugMeshHeader(mm.meshIndex + i, h)) { std::printf("mesh %d unreadable\n", i); return 1; }
        for (int k = 0; k < 4; ++k) sph[(size_t)i * 4 + k] = h[k];
    }
    if (!all)
        std::printf("mesh %d: sphere centre %d %d %d, radius %d (from %s slot %s)\n", mesh,
                    sph[mesh * 4], sph[mesh * 4 + 1], sph[mesh * 4 + 2], sph[mesh * 4 + 3],
                    meshLevel ? meshLevel : argv[1], meshLevel ? meshSlot : argv[2]);

    if (!all)
        std::printf("frame  rel  key |   x     y     z    r | front  angle  units |  band y     | box z\n");

    double best = -1e9; int bestFrame = -1;
    double bx = 0, by = 0, bz = 0;
    for (int f = first; f <= last; ++f)
    {
        // GetFrames + GetBestFrame: key f / rate, the next one past half.
        // GetFrames shortens the rate of the last pair against frame_end,
        // a number counted from the slot's first frame, with the frame
        // counted from the animation's - the engine's comparison as it
        // stands.
        int key = f / rate;
        int frac = f % rate, span = rate;
        if (frac)
        {
            const int num = rate * (f / rate + 1);
            if (num > fend) span = rate + fend - num;
            if (frac > (span >> 1)) ++key;
        }
        const size_t fat = frAt + fptr + (size_t)key * fsize * 2;
        if (fat + 18 > b.size()) { std::printf("frame %d runs past the file\n", f); return 1; }
        const std::vector<Mx> mx = poseFrame(a, ma, fat);
        const int boxZ0 = r16(b, fat + 8), boxZ1 = r16(b, fat + 10);

        if (all)
        {
            std::printf("frame %d (rel %d, key %d), box x %d..%d y %d..%d z %d..%d\n", fbase + f, f, key,
                        r16(b, fat), r16(b, fat + 2), r16(b, fat + 4), r16(b, fat + 6), boxZ0, boxZ1);
            for (int i = 0; i < ma.nmeshes; ++i)
            {
                double p[3];
                for (int r = 0; r < 3; ++r)
                    p[r] = mx[i].m[r][0] * sph[i * 4] + mx[i].m[r][1] * sph[i * 4 + 1] +
                           mx[i].m[r][2] * sph[i * 4 + 2] + mx[i].m[r][3];
                std::printf("  mesh %2d: %6.0f %6.0f %6.0f  r %3d\n", i, p[0], p[1], p[2], sph[i * 4 + 3]);
            }
            continue;
        }

        double p[3];
        for (int r = 0; r < 3; ++r)
            p[r] = mx[mesh].m[r][0] * sph[mesh * 4] + mx[mesh].m[r][1] * sph[mesh * 4 + 1] +
                   mx[mesh].m[r][2] * sph[mesh * 4 + 2] + mx[mesh].m[r][3];
        const int rad = sph[mesh * 4 + 3];
        const double front = p[2] + rad;
        const double angle = std::atan2(p[0], p[2]) * 180.0 / kPi;
        const long units = std::lround(angle * 65536.0 / 360.0);
        std::printf("%5d %4d %4d | %5.0f %5.0f %5.0f %4d | %5.0f %6.1f %6ld | %5.0f..%5.0f | %d..%d\n",
                    fbase + f, f, key, p[0], p[1], p[2], rad, front, angle, units,
                    p[1] - rad, p[1] + rad, boxZ0, boxZ1);
        if (front > best)
        {
            best = front; bestFrame = f;
            bx = p[0]; by = p[1]; bz = p[2];
        }
    }

    if (!all && bestFrame >= 0)
        std::printf("largest front %.0f at frame %d (rel %d): centre %.0f %.0f %.0f, angle %.1f (%ld)\n",
                    best, fbase + bestFrame, bestFrame, bx, by, bz, std::atan2(bx, bz) * 180.0 / kPi,
                    std::lround(std::atan2(bx, bz) * 32768.0 / kPi));
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 4 && std::strcmp(argv[3], "-reach") == 0)
        return reach(argc, argv);

    if (argc != 3 && argc != 5)
    {
        std::printf("usage: BoneCheck <level.trc> <slot> [<level2.trc> <slot2>]\n"
                    "       BoneCheck <level.trc> <slot> -reach <mesh|all> <anim> [<first> <last>]"
                    " [-meshes <level.trc> <slot>]\n");
        return 2;
    }

    Tr5Level a, b;
    std::string e;
    if (!a.load(toW(argv[1]), e)) { std::printf("A not loadable: %s\n", e.c_str()); return 1; }
    Tr5Level::MoveableInfo ma, mb;
    if (!a.moveableInfo(std::atoi(argv[2]), ma)) { std::printf("slot %s missing in A\n", argv[2]); return 1; }

    const bool two = (argc == 5);
    if (two)
    {
        if (!b.load(toW(argv[3]), e)) { std::printf("B not loadable: %s\n", e.c_str()); return 1; }
        if (!b.moveableInfo(std::atoi(argv[4]), mb)) { std::printf("slot %s missing in B\n", argv[4]); return 1; }
        if (ma.nmeshes != mb.nmeshes)
            std::printf("MESH COUNT DEVIATES: %d vs %d\n", ma.nmeshes, mb.nmeshes);
    }

    std::printf("A: %s slot %s, %d meshes, boneIndex %d\n", argv[1], argv[2], ma.nmeshes, ma.boneIndex);
    if (two)
        std::printf("B: %s slot %s, %d meshes, boneIndex %d\n", argv[3], argv[4], mb.nmeshes, mb.boneIndex);

    int differing = 0;
    const int n = two ? (ma.nmeshes < mb.nmeshes ? ma.nmeshes : mb.nmeshes) : ma.nmeshes;
    for (int i = 0; i < n - 1; ++i)
    {
        long fA = 0, xA = 0, yA = 0, zA = 0;
        fA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 0);
        xA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 1);
        yA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 2);
        zA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 3);

        if (!two)
        {
            std::printf("  bone %2d: flags=%02lX  %6ld %6ld %6ld\n", i, fA, xA, yA, zA);
            continue;
        }

        long fB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 0);
        long xB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 1);
        long yB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 2);
        long zB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 3);

        // Decisive are push/pop (bits 0/1) - they drive the stack.
        const bool stackDiffers = (fA & 3) != (fB & 3);
        const bool restDiffers = fA != fB || xA != xB || yA != yB || zA != zB;
        if (stackDiffers || restDiffers) ++differing;

        std::printf("  bone %2d: A flags=%02lX %6ld %6ld %6ld | B flags=%02lX %6ld %6ld %6ld%s\n",
                    i, fA, xA, yA, zA, fB, xB, yB, zB,
                    stackDiffers ? "  <-- STACK!" : (restDiffers ? "  <-- differs" : ""));
    }

    if (two)
        std::printf("%d of %d bones deviating\n", differing, n - 1);
    return 0;
}

// BoneCheck - show a moveable slot's bone tree, or compare two.
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
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <string>

static std::wstring toW(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 5)
    {
        std::printf("usage: BoneCheck <level.trc> <slot> [<level2.trc> <slot2>]\n");
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

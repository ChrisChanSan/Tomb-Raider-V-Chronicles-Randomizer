// AnimMap - two modes around a .trc's animation table.
//
// Occasion (#121 follow-up): an imported enemy blinks - some animations
// draw, others deliver zeros as bounds and drop out of drawing AND hit
// testing. The question to the file: WHICH anims are broken, and do their
// frame pointers still agree with the donor level?
//
//   AnimMap <level.trc>
//       All moveable slots sorted by anim_index, with mesh count and the
//       slice that follows from it. Shows overlaps and gaps in the
//       appended area - they arise when two imports collide.
//
//   AnimMap <a.trc> <slotA> <b.trc> <slotB> <count>
//       Compares the first <count> anims of both slices entry by entry:
//       state, frame window, frame offset and the first six shorts of the
//       frame (the bounding box from which S_GetObjectBounds builds the
//       clip window). A box of all zeros means: the pointer runs into the
//       void, the enemy is invisible for this anim.
//
// Anim entry in the main block, 40 bytes (verified at Tr5Level.cpp:1190/1213):
//   +0  u32 frameOffset (BYTE offset into the frames block)
//   +4  u8  frameRate   +5 u8 frameSize   +6 u16 state
//   +24 s16 frameStart  +26 s16 frameEnd  +28 s16 nextAnim
//
// Build (64 bit): bam.bat next to this file. See Preparation/Tools/README.md.
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

struct AnimRow
{
    uint32_t frameOffset = 0;
    uint8_t  frameRate = 0, frameSize = 0;
    uint16_t state = 0;
    int16_t  frameStart = 0, frameEnd = 0, nextAnim = 0;
};

static bool readAnim(const Tr5Level& l, int index, AnimRow& z)
{
    size_t at = 0; int32_t n = 0;
    if (!l.debugAnimBlock(at, n) || index < 0 || index >= n) return false;
    const std::vector<uint8_t>& b = l.debugBytes();
    const uint8_t* e = b.data() + at + (size_t)index * 40;
    std::memcpy(&z.frameOffset, e + 0, 4);
    z.frameRate = e[4]; z.frameSize = e[5];
    std::memcpy(&z.state, e + 6, 2);
    std::memcpy(&z.frameStart, e + 24, 2);
    std::memcpy(&z.frameEnd, e + 26, 2);
    std::memcpy(&z.nextAnim, e + 28, 2);
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 6)
    {
        std::printf("usage: AnimMap <level.trc>\n"
                    "       AnimMap <a.trc> <slotA> <b.trc> <slotB> <count>\n");
        return 2;
    }

    if (argc == 2)
    {
        Tr5Level l; std::string e;
        if (!l.load(toW(argv[1]), e)) { std::printf("not loadable: %s\n", e.c_str()); return 1; }

        size_t at = 0; int32_t nAnims = 0;
        l.debugAnimBlock(at, nAnims);

        struct Entry { int slot, animIndex, nmeshes; };
        std::vector<Entry> list;
        for (int s : l.moveableSlots())
        {
            Tr5Level::MoveableInfo mi;
            if (!l.moveableInfo(s, mi)) continue;
            list.push_back({ s, mi.animIndex, mi.nmeshes });
        }
        std::sort(list.begin(), list.end(),
                  [](const Entry& a, const Entry& b) { return a.animIndex < b.animIndex; });

        std::printf("%s: %d anims total\n", argv[1], nAnims);
        std::printf("%5s %9s %7s %7s\n", "slot", "animIdx", "meshes", "slice");
        for (size_t i = 0; i < list.size(); ++i)
        {
            const int end = (i + 1 < list.size()) ? list[i + 1].animIndex : nAnims;
            const int width = end - list[i].animIndex;
            std::printf("%5d %9d %7d %7d%s\n", list[i].slot, list[i].animIndex,
                        list[i].nmeshes, width, width < 0 ? "  <-- OVERLAP" : "");
        }
        return 0;
    }

    Tr5Level a, b; std::string e;
    if (!a.load(toW(argv[1]), e)) { std::printf("A not loadable: %s\n", e.c_str()); return 1; }
    if (!b.load(toW(argv[3]), e)) { std::printf("B not loadable: %s\n", e.c_str()); return 1; }

    Tr5Level::MoveableInfo ma, mb;
    if (!a.moveableInfo(std::atoi(argv[2]), ma)) { std::printf("slot %s missing in A\n", argv[2]); return 1; }
    if (!b.moveableInfo(std::atoi(argv[4]), mb)) { std::printf("slot %s missing in B\n", argv[4]); return 1; }

    const int n = std::atoi(argv[5]);
    std::printf("A=%s slot %s from %d   B=%s slot %s from %d   %d anims\n",
                argv[1], argv[2], ma.animIndex, argv[3], argv[4], mb.animIndex, n);
    std::printf("%4s %4s | %4s %6s %10s  box(A)              | %4s %6s %10s  box(B)\n",
                "no", "", "st", "frames", "frameOfs", "st", "frames", "frameOfs");

    int broken = 0;
    for (int i = 0; i < n; ++i)
    {
        AnimRow za, zb;
        if (!readAnim(a, ma.animIndex + i, za) || !readAnim(b, mb.animIndex + i, zb))
        { std::printf("%4d  table too short\n", i); break; }

        // The first six shorts of the frame are the bounding box.
        int16_t boxA[6] = {}, boxB[6] = {};
        for (int k = 0; k < 6; ++k)
        {
            boxA[k] = a.debugReadFrameShort((long long)(za.frameOffset / 2) + k);
            boxB[k] = b.debugReadFrameShort((long long)(zb.frameOffset / 2) + k);
        }

        bool zeroA = true, equal = true;
        for (int k = 0; k < 6; ++k)
        {
            if (boxA[k]) zeroA = false;
            if (boxA[k] != boxB[k]) equal = false;
        }

        // Compare the LAST keyframe too, not only the first. The engine
        // interpolates between keyframes (frameRate); if the copied frame
        // stream does not reach the last keyframe, the first keyframe is
        // intact (the comparison above stays silent), but every
        // interpolated picture behind it reads garbage - the enemy is then
        // visible only on the keyframes themselves. Exactly the blink
        // rhythm from andy3.
        bool endEqual = true;
        {
            const int rateA = za.frameRate ? za.frameRate : 1;
            const int rateB = zb.frameRate ? zb.frameRate : 1;
            const int kfA = (za.frameEnd - za.frameStart) / rateA;
            const int kfB = (zb.frameEnd - zb.frameStart) / rateB;
            for (int k = 0; k < 6; ++k)
            {
                const int16_t ea = a.debugReadFrameShort((long long)(za.frameOffset / 2) + (long long)kfA * za.frameSize + k);
                const int16_t eb = b.debugReadFrameShort((long long)(zb.frameOffset / 2) + (long long)kfB * zb.frameSize + k);
                if (ea != eb) { endEqual = false; break; }
            }
        }

        const bool conspicuous = (zeroA && !equal) || !endEqual ||
                                za.state != zb.state ||
                                za.frameRate != zb.frameRate ||
                                za.frameSize != zb.frameSize ||
                                (za.frameEnd - za.frameStart) != (zb.frameEnd - zb.frameStart);
        if (conspicuous) ++broken;

        // Print rate and frameSize along: the step-width disease ("dead
        // inheritance", see adoptAnimData in Tr5Level.h) can ONLY be
        // recognized by two anims reading the same frameOffset with
        // different frameSize.
        std::printf("%4d %-4s | %4u %3d..%-3d r%-2u s%-3u %10u  %6d %6d %6d | %4u %3d..%-3d r%-2u s%-3u %10u  %6d %6d %6d\n",
                    i, conspicuous ? "!!" : "", za.state, za.frameStart, za.frameEnd,
                    za.frameRate, za.frameSize,
                    za.frameOffset, boxA[0], boxA[2], boxA[4],
                    zb.state, zb.frameStart, zb.frameEnd,
                    zb.frameRate, zb.frameSize,
                    zb.frameOffset, boxB[0], boxB[2], boxB[4]);
    }
    std::printf("%d of %d conspicuous\n", broken, n);

    // --- Check the linkages --------------------------------------------
    //
    // nextAnim and the state changes' jump targets are ABSOLUTE indices
    // into the animation table. On import each must be shifted by the
    // difference of the slice starts. A forgotten or doubly shifted entry
    // does NOT show in the frame comparison above - the anim itself is
    // intact - but at runtime sends the enemy into a foreign anim with a
    // foreign mesh count: bounds zero, invisible.
    {
        const int delta = ma.animIndex - mb.animIndex;
        std::printf("\nlinkages (expected shift %+d):\n", delta);

        size_t chA = 0, chB = 0, rgA = 0, rgB = 0;
        int32_t nChA = 0, nChB = 0, nRgA = 0, nRgB = 0;
        a.debugChangeBlock(chA, nChA); b.debugChangeBlock(chB, nChB);
        a.debugRangeBlock(rgA, nRgA);  b.debugRangeBlock(rgB, nRgB);
        const std::vector<uint8_t>& ba = a.debugBytes();
        const std::vector<uint8_t>& bb = b.debugBytes();

        int wrong = 0;
        for (int i = 0; i < n; ++i)
        {
            AnimRow za, zb;
            if (!readAnim(a, ma.animIndex + i, za) || !readAnim(b, mb.animIndex + i, zb)) break;

            if (za.nextAnim != zb.nextAnim + delta)
            {
                std::printf("  anim %3d: nextAnim %d, donor %d -> expected %d  <-- WRONG\n",
                            i, za.nextAnim, zb.nextAnim, zb.nextAnim + delta);
                ++wrong;
            }

            // Change entry: u16 state, u16 rangeCount, u16 rangeIndex (6 B).
            // Range entry: s16 start, s16 end, s16 linkAnim, s16 linkFrame.
            uint16_t ncA = 0, ciA = 0, ncB = 0, ciB = 0;
            {
                size_t at = 0; int32_t cnt = 0; a.debugAnimBlock(at, cnt);
                std::memcpy(&ncA, ba.data() + at + (size_t)(ma.animIndex + i) * 40 + 32, 2);
                std::memcpy(&ciA, ba.data() + at + (size_t)(ma.animIndex + i) * 40 + 34, 2);
                b.debugAnimBlock(at, cnt);
                std::memcpy(&ncB, bb.data() + at + (size_t)(mb.animIndex + i) * 40 + 32, 2);
                std::memcpy(&ciB, bb.data() + at + (size_t)(mb.animIndex + i) * 40 + 34, 2);
            }
            if (ncA != ncB) { std::printf("  anim %3d: %u changes, donor %u\n", i, ncA, ncB); ++wrong; continue; }

            for (int c = 0; c < ncA && ciA + c < nChA && ciB + c < nChB; ++c)
            {
                uint16_t rcA = 0, riA = 0, rcB = 0, riB = 0;
                std::memcpy(&rcA, ba.data() + chA + (size_t)(ciA + c) * 6 + 2, 2);
                std::memcpy(&riA, ba.data() + chA + (size_t)(ciA + c) * 6 + 4, 2);
                std::memcpy(&rcB, bb.data() + chB + (size_t)(ciB + c) * 6 + 2, 2);
                std::memcpy(&riB, bb.data() + chB + (size_t)(ciB + c) * 6 + 4, 2);

                for (int r = 0; r < rcA && r < rcB && riA + r < nRgA && riB + r < nRgB; ++r)
                {
                    int16_t laA = 0, laB = 0;
                    std::memcpy(&laA, ba.data() + rgA + (size_t)(riA + r) * 8 + 4, 2);
                    std::memcpy(&laB, bb.data() + rgB + (size_t)(riB + r) * 8 + 4, 2);
                    if (laA != laB + delta)
                    {
                        std::printf("  anim %3d change %d range %d: target %d, donor %d -> expected %d  <-- WRONG\n",
                                    i, c, r, laA, laB, laB + delta);
                        ++wrong;
                    }
                }
            }
        }
        std::printf("%d wrong linkages\n", wrong);
    }
    return 0;
}

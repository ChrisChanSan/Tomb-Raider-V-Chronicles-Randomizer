// AnimKmd - lists an object slot's anim commands, above all the sound
// commands (type 5). Needed for #111 part 2: which death scream does the
// soldier family really call, and do the SFX numbers differ between the
// donors (rich3 SWAT vs joby3 Guard)?
//
// The command loop counts COMMANDS, not words - one command occupies 1 to
// 4 words (type 1: 3 operands, type 2: 2, type 3/4: none, type 5/6: 2).
// Exactly this counting is what TR3Setz once failed at (the Compy case).
//
// Usage: AnimKmd <level.trc> <slot> [slot ...]
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: AnimKmd <level.trc> <slot> [slot ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error))
    {
        std::printf("not loadable: %s\n", error.c_str());
        return 1;
    }

    size_t animAt = 0, cmdAt = 0;
    int32_t nAnims = 0, nCmd = 0;
    if (!lvl.debugAnimBlock(animAt, nAnims) || !lvl.debugCommandBlock(cmdAt, nCmd))
    {
        std::printf("anim or command block not found\n");
        return 1;
    }
    const std::vector<uint8_t>& b = lvl.debugBytes();

    auto r16 = [&](size_t at) -> uint16_t
    {
        uint16_t v = 0;
        std::memcpy(&v, b.data() + at, 2);
        return v;
    };

    for (int arg = 2; arg < argc; ++arg)
    {
        const int slot = std::atoi(argv[arg]);
        Tr5Level::MoveableInfo mv;
        if (!lvl.moveableInfo(slot, mv) || mv.animIndex < 0)
        {
            std::printf("\nslot %d: no model or no anims of its own\n", slot);
            continue;
        }

        int end = nAnims;
        for (int s : lvl.moveableSlots())
        {
            Tr5Level::MoveableInfo o;
            if (!lvl.moveableInfo(s, o)) continue;
            if (o.animIndex > mv.animIndex && o.animIndex < end) end = o.animIndex;
        }

        std::printf("\nslot %d: anims %d..%d (slice %d)\n",
                    slot, mv.animIndex, end - 1, end - mv.animIndex);

        // Frame range per animation. Decides the question "does the object
        // move at all when triggered": an anim with frame_base ==
        // frame_end stands still, then every visible movement comes from
        // outside (cutscene) and a trigger does nothing. The anim struct
        // is 40 bytes, frame_base at +24, frame_end at +26,
        // next_animation +28.
        for (int a = mv.animIndex; a < end; ++a)
        {
            const size_t o = animAt + (size_t)a * 40;
            const uint16_t fb = r16(o + 24), fe = r16(o + 26), na = r16(o + 28);
            const uint16_t nCh = r16(o + 32), iCh = r16(o + 34);
            uint32_t fo; std::memcpy(&fo, &b[o], 4);
            const uint8_t  iv = b[o + 4];
            const uint16_t st = r16(o + 6);     // current_anim_state
            std::printf("  anim +%-3d (abs %4d): state %-3u frameOfs %-7u frames %u..%u (%d total) rate %u -> continues with anim %u  changes:%u@%u%s\n",
                        a - mv.animIndex, a, st, fo, fb, fe, (int)fe - (int)fb + 1, iv, na, nCh, iCh,
                        (fe == fb) ? "   [STANDS STILL]" : "");
        }

        for (int a = mv.animIndex; a < end; ++a)
        {
            const size_t o = animAt + (size_t)a * 40;
            const uint16_t cnt = r16(o + 36);
            const uint16_t idx = r16(o + 38);
            if (!cnt) continue;

            size_t p = cmdAt + (size_t)idx * 2;
            for (uint16_t k = 0; k < cnt; ++k)
            {
                if (p + 2 > cmdAt + (size_t)nCmd * 2) { std::printf("  anim +%d: commands run out of the block\n", a - mv.animIndex); break; }
                const uint16_t typ = r16(p); p += 2;
                int operands = 0;
                switch (typ)
                {
                case 1: operands = 3; break;
                case 2: operands = 2; break;
                case 3: case 4: operands = 0; break;
                case 5: case 6: operands = 2; break;
                default:
                    std::printf("  anim +%d: unknown command type %u - rest skipped\n", a - mv.animIndex, typ);
                    k = cnt;
                    continue;
                }

                if (typ == 5 && operands == 2)
                {
                    const uint16_t frame = r16(p);
                    const uint16_t id = r16(p + 2);
                    std::printf("  anim +%-3d (abs %4d): PlaySound frame %-5u sfx %-4u%s\n",
                                a - mv.animIndex, a, frame, id & 0x3FFF,
                                (id & 0x4000) ? " [land only]" : (id & 0x8000) ? " [water only]" : "");
                }
                else if (typ == 4)
                    std::printf("  anim +%-3d (abs %4d): KillItem\n", a - mv.animIndex, a);
                else if (typ == 6)
                    std::printf("  anim +%-3d (abs %4d): FlipEffect frame %-5u no %u\n",
                                a - mv.animIndex, a, r16(p), r16(p + 2) & 0x3FFF);

                p += (size_t)operands * 2;
            }
        }
    }
    return 0;
}

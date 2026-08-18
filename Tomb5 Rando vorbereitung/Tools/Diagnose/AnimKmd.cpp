// AnimKmd - listet die Anim-Kommandos eines Objektslots, vor allem die
// Klangbefehle (Typ 5). Gebraucht fuer #111 Teil 2: welchen Sterbeschrei
// ruft die Soldatenfamilie wirklich, und unterscheiden sich die SFX-Nummern
// zwischen den Spendern (rich3-SWAT vs. joby3-Guard)?
//
// Die Kommandoschleife zaehlt KOMMANDOS, nicht Worte - ein Kommando belegt
// 1 bis 4 Worte (Typ 1: 3 Operanden, Typ 2: 2, Typ 3/4: keine, Typ 5/6: 2).
// Genau an dieser Zaehlung ist TR3Setz einmal gescheitert (Compy-Fall).
//
// Aufruf: AnimKmd <level.trc> <slot> [slot ...]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring zuW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("Aufruf: AnimKmd <level.trc> <slot> [slot ...]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string fehler;
    if (!lvl.load(zuW(argv[1]), fehler))
    {
        std::printf("Nicht ladbar: %s\n", fehler.c_str());
        return 1;
    }

    size_t animAt = 0, cmdAt = 0;
    int32_t nAnims = 0, nCmd = 0;
    if (!lvl.debugAnimBlock(animAt, nAnims) || !lvl.debugCommandBlock(cmdAt, nCmd))
    {
        std::printf("Anim- oder Kommandoblock nicht gefunden\n");
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
            std::printf("\nSlot %d: kein Modell oder keine eigenen Anims\n", slot);
            continue;
        }

        int ende = nAnims;
        for (int s : lvl.moveableSlots())
        {
            Tr5Level::MoveableInfo o;
            if (!lvl.moveableInfo(s, o)) continue;
            if (o.animIndex > mv.animIndex && o.animIndex < ende) ende = o.animIndex;
        }

        std::printf("\nSlot %d: Anims %d..%d (Scheibe %d)\n",
                    slot, mv.animIndex, ende - 1, ende - mv.animIndex);

        for (int a = mv.animIndex; a < ende; ++a)
        {
            const size_t o = animAt + (size_t)a * 40;
            const uint16_t anz = r16(o + 36);
            const uint16_t idx = r16(o + 38);
            if (!anz) continue;

            size_t p = cmdAt + (size_t)idx * 2;
            for (uint16_t k = 0; k < anz; ++k)
            {
                if (p + 2 > cmdAt + (size_t)nCmd * 2) { std::printf("  Anim +%d: Kommandos laufen aus dem Block\n", a - mv.animIndex); break; }
                const uint16_t typ = r16(p); p += 2;
                int operanden = 0;
                switch (typ)
                {
                case 1: operanden = 3; break;
                case 2: operanden = 2; break;
                case 3: case 4: operanden = 0; break;
                case 5: case 6: operanden = 2; break;
                default:
                    std::printf("  Anim +%d: unbekannter Kommandotyp %u - Rest uebersprungen\n", a - mv.animIndex, typ);
                    k = anz;
                    continue;
                }

                if (typ == 5 && operanden == 2)
                {
                    const uint16_t frame = r16(p);
                    const uint16_t id = r16(p + 2);
                    std::printf("  Anim +%-3d (abs %4d): PlaySound Frame %-5u sfx %-4u%s\n",
                                a - mv.animIndex, a, frame, id & 0x3FFF,
                                (id & 0x4000) ? " [nur Land]" : (id & 0x8000) ? " [nur Wasser]" : "");
                }
                else if (typ == 4)
                    std::printf("  Anim +%-3d (abs %4d): KillItem\n", a - mv.animIndex, a);
                else if (typ == 6)
                    std::printf("  Anim +%-3d (abs %4d): FlipEffect Frame %-5u Nr %u\n",
                                a - mv.animIndex, a, r16(p), r16(p + 2) & 0x3FFF);

                p += (size_t)operanden * 2;
            }
        }
    }
    return 0;
}

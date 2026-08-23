// BonePruef - Knochenbaum eines Moveable-Slots anzeigen oder zwei vergleichen.
//
// Anlass (#121, dritte Runde): ein Familien-Stummel (Meshes aus Spender A)
// laeuft zur Laufzeit auf dem Anim-Satz des Basistyps (aus Spender B). Die
// Frames tragen Rotationen je Mesh, aber die STAPELBEFEHLE (Push/Pop in
// bone[0] Bit 0/1) kommen aus dem Knochenbaum des Stummels. Sind die Baeume
// der beiden Spender nicht baugleich, laeuft der Matrix-Stapel beim Zeichnen
// auseinander - Meshes irgendwo, Koerper unsichtbar, Bounds trotzdem gueltig.
//
// Aufruf: BonePruef <level.trc> <slot> [<level2.trc> <slot2>]
//   Ein Paar:  Baum anzeigen (flags x y z je Knochen).
//   Zwei Paare: beide Baeume nebeneinander, Abweichungen markiert.
//
// Knochenstrom: je Mesh nach dem ersten 4 Longs (flags, x, y, z);
// flags Bit0 = Pop, Bit1 = Push, Bits 2..4 = Drehkanaele aus item->data.
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <string>

static std::wstring weit(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 5)
    {
        std::printf("Aufruf: BonePruef <level.trc> <slot> [<level2.trc> <slot2>]\n");
        return 2;
    }

    Tr5Level a, b;
    std::string e;
    if (!a.load(weit(argv[1]), e)) { std::printf("A nicht ladbar: %s\n", e.c_str()); return 1; }
    Tr5Level::MoveableInfo ma, mb;
    if (!a.moveableInfo(std::atoi(argv[2]), ma)) { std::printf("Slot %s fehlt in A\n", argv[2]); return 1; }

    const bool zwei = (argc == 5);
    if (zwei)
    {
        if (!b.load(weit(argv[3]), e)) { std::printf("B nicht ladbar: %s\n", e.c_str()); return 1; }
        if (!b.moveableInfo(std::atoi(argv[4]), mb)) { std::printf("Slot %s fehlt in B\n", argv[4]); return 1; }
        if (ma.nmeshes != mb.nmeshes)
            std::printf("MESHZAHL WEICHT AB: %d gegen %d\n", ma.nmeshes, mb.nmeshes);
    }

    std::printf("A: %s Slot %s, %d Meshes, boneIndex %d\n", argv[1], argv[2], ma.nmeshes, ma.boneIndex);
    if (zwei)
        std::printf("B: %s Slot %s, %d Meshes, boneIndex %d\n", argv[3], argv[4], mb.nmeshes, mb.boneIndex);

    int anders = 0;
    const int n = zwei ? (ma.nmeshes < mb.nmeshes ? ma.nmeshes : mb.nmeshes) : ma.nmeshes;
    for (int i = 0; i < n - 1; ++i)
    {
        long fA = 0, xA = 0, yA = 0, zA = 0;
        fA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 0);
        xA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 1);
        yA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 2);
        zA = a.debugReadBoneLong(ma.boneIndex + (long long)i * 4 + 3);

        if (!zwei)
        {
            std::printf("  Knochen %2d: flags=%02lX  %6ld %6ld %6ld\n", i, fA, xA, yA, zA);
            continue;
        }

        long fB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 0);
        long xB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 1);
        long yB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 2);
        long zB = b.debugReadBoneLong(mb.boneIndex + (long long)i * 4 + 3);

        // Entscheidend sind Push/Pop (Bit 0/1) - sie steuern den Stapel.
        const bool stapelAnders = (fA & 3) != (fB & 3);
        const bool restAnders = fA != fB || xA != xB || yA != yB || zA != zB;
        if (stapelAnders || restAnders) ++anders;

        std::printf("  Knochen %2d: A flags=%02lX %6ld %6ld %6ld | B flags=%02lX %6ld %6ld %6ld%s\n",
                    i, fA, xA, yA, zA, fB, xB, yB, zB,
                    stapelAnders ? "  <-- STAPEL!" : (restAnders ? "  <-- anders" : ""));
    }

    if (zwei)
        std::printf("%d von %d Knochen abweichend\n", anders, n - 1);
    return 0;
}

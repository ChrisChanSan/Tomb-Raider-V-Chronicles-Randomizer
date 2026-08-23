// AnimMap - zwei Betriebsarten rund um die Animationstabelle einer .trc.
//
// Anlass (#121-Nachlese): ein importierter Gegner blinkt - manche Animationen
// zeichnen, andere liefern Nullen als Bounds und fallen aus Zeichnung UND
// Trefferpruefung. Die Frage an die Datei: WELCHE Anims sind kaputt, und
// stimmen ihre Frame-Zeiger noch mit dem Spenderlevel ueberein?
//
//   AnimMap <level.trc>
//       Alle Moveable-Slots nach anim_index sortiert, mit Meshzahl und der
//       daraus folgenden Scheibe. Zeigt Ueberlappungen und Luecken im
//       angehaengten Bereich - die entstehen, wenn zwei Importe kollidieren.
//
//   AnimMap <a.trc> <slotA> <b.trc> <slotB> <anzahl>
//       Vergleicht die ersten <anzahl> Anims beider Scheiben Eintrag fuer
//       Eintrag: Zustand, Frame-Fenster, Frame-Offset und die ersten sechs
//       Shorts des Frames (die Bounding-Box, aus der S_GetObjectBounds das
//       Clip-Fenster baut). Eine Box aus lauter Nullen heisst: der Zeiger
//       laeuft ins Leere, der Gegner ist fuer diese Anim unsichtbar.
//
// Anim-Eintrag im Hauptblock, 40 Bytes (an Tr5Level.cpp:1190/1213 verifiziert):
//   +0  u32 frameOffset (BYTE-Offset in den Frames-Block)
//   +4  u8  frameRate   +5 u8 frameSize   +6 u16 state
//   +24 s16 frameStart  +26 s16 frameEnd  +28 s16 nextAnim
//
// Bauen (64 Bit, im Ordner Bittenichtbitdefender): wie SlotAnims, siehe
// reference-werkzeugkasten.
#include "core/Tr5Level.h"
#include "core/DataFolder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::wstring weit(const char* s)
{ std::wstring w; while (*s) w += (wchar_t)(unsigned char)*s++; return w; }

struct AnimZeile
{
    uint32_t frameOffset = 0;
    uint8_t  frameRate = 0, frameSize = 0;
    uint16_t state = 0;
    int16_t  frameStart = 0, frameEnd = 0, nextAnim = 0;
};

static bool liesAnim(const Tr5Level& l, int index, AnimZeile& z)
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
        std::printf("Aufruf: AnimMap <level.trc>\n"
                    "        AnimMap <a.trc> <slotA> <b.trc> <slotB> <anzahl>\n");
        return 2;
    }

    if (argc == 2)
    {
        Tr5Level l; std::string e;
        if (!l.load(weit(argv[1]), e)) { std::printf("nicht ladbar: %s\n", e.c_str()); return 1; }

        size_t at = 0; int32_t nAnims = 0;
        l.debugAnimBlock(at, nAnims);

        struct Eintrag { int slot, animIndex, nmeshes; };
        std::vector<Eintrag> liste;
        for (int s : l.moveableSlots())
        {
            Tr5Level::MoveableInfo mi;
            if (!l.moveableInfo(s, mi)) continue;
            liste.push_back({ s, mi.animIndex, mi.nmeshes });
        }
        std::sort(liste.begin(), liste.end(),
                  [](const Eintrag& a, const Eintrag& b) { return a.animIndex < b.animIndex; });

        std::printf("%s: %d Anims gesamt\n", argv[1], nAnims);
        std::printf("%5s %9s %7s %7s\n", "Slot", "animIdx", "Meshes", "Scheibe");
        for (size_t i = 0; i < liste.size(); ++i)
        {
            const int ende = (i + 1 < liste.size()) ? liste[i + 1].animIndex : nAnims;
            const int breite = ende - liste[i].animIndex;
            std::printf("%5d %9d %7d %7d%s\n", liste[i].slot, liste[i].animIndex,
                        liste[i].nmeshes, breite, breite < 0 ? "  <-- UEBERLAPPUNG" : "");
        }
        return 0;
    }

    Tr5Level a, b; std::string e;
    if (!a.load(weit(argv[1]), e)) { std::printf("A nicht ladbar: %s\n", e.c_str()); return 1; }
    if (!b.load(weit(argv[3]), e)) { std::printf("B nicht ladbar: %s\n", e.c_str()); return 1; }

    Tr5Level::MoveableInfo ma, mb;
    if (!a.moveableInfo(std::atoi(argv[2]), ma)) { std::printf("Slot %s fehlt in A\n", argv[2]); return 1; }
    if (!b.moveableInfo(std::atoi(argv[4]), mb)) { std::printf("Slot %s fehlt in B\n", argv[4]); return 1; }

    const int n = std::atoi(argv[5]);
    std::printf("A=%s Slot %s ab %d   B=%s Slot %s ab %d   %d Anims\n",
                argv[1], argv[2], ma.animIndex, argv[3], argv[4], mb.animIndex, n);
    std::printf("%4s %4s | %4s %6s %10s  Box(A)              | %4s %6s %10s  Box(B)\n",
                "Nr", "", "Zst", "Frames", "frameOfs", "Zst", "Frames", "frameOfs");

    int kaputt = 0;
    for (int i = 0; i < n; ++i)
    {
        AnimZeile za, zb;
        if (!liesAnim(a, ma.animIndex + i, za) || !liesAnim(b, mb.animIndex + i, zb))
        { std::printf("%4d  Tabelle zu kurz\n", i); break; }

        // Die ersten sechs Shorts des Frames sind die Bounding-Box.
        int16_t boxA[6] = {}, boxB[6] = {};
        for (int k = 0; k < 6; ++k)
        {
            boxA[k] = a.debugReadFrameShort((long long)(za.frameOffset / 2) + k);
            boxB[k] = b.debugReadFrameShort((long long)(zb.frameOffset / 2) + k);
        }

        bool nullA = true, gleich = true;
        for (int k = 0; k < 6; ++k)
        {
            if (boxA[k]) nullA = false;
            if (boxA[k] != boxB[k]) gleich = false;
        }

        // Auch den LETZTEN Keyframe vergleichen, nicht nur den ersten. Die
        // Engine interpoliert zwischen Keyframes (frameRate); reicht der
        // kopierte Frame-Strom nicht bis zum letzten Keyframe, ist der erste
        // Keyframe intakt (der Vergleich oben schweigt), aber jedes
        // interpolierte Bild dahinter liest Muell - der Gegner ist dann nur
        // auf den Keyframes selbst sichtbar. Genau der Blink-Takt aus andy3.
        bool endeGleich = true;
        {
            const int rateA = za.frameRate ? za.frameRate : 1;
            const int rateB = zb.frameRate ? zb.frameRate : 1;
            const int kfA = (za.frameEnd - za.frameStart) / rateA;
            const int kfB = (zb.frameEnd - zb.frameStart) / rateB;
            for (int k = 0; k < 6; ++k)
            {
                const int16_t ea = a.debugReadFrameShort((long long)(za.frameOffset / 2) + (long long)kfA * za.frameSize + k);
                const int16_t eb = b.debugReadFrameShort((long long)(zb.frameOffset / 2) + (long long)kfB * zb.frameSize + k);
                if (ea != eb) { endeGleich = false; break; }
            }
        }

        const bool auffaellig = (nullA && !gleich) || !endeGleich ||
                                za.state != zb.state ||
                                za.frameRate != zb.frameRate ||
                                za.frameSize != zb.frameSize ||
                                (za.frameEnd - za.frameStart) != (zb.frameEnd - zb.frameStart);
        if (auffaellig) ++kaputt;

        std::printf("%4d %-4s | %4u %3d..%-3d %10u  %6d %6d %6d | %4u %3d..%-3d %10u  %6d %6d %6d\n",
                    i, auffaellig ? "!!" : "", za.state, za.frameStart, za.frameEnd,
                    za.frameOffset, boxA[0], boxA[2], boxA[4],
                    zb.state, zb.frameStart, zb.frameEnd,
                    zb.frameOffset, boxB[0], boxB[2], boxB[4]);
    }
    std::printf("%d von %d auffaellig\n", kaputt, n);

    // --- Verkettungen pruefen ------------------------------------------
    //
    // nextAnim und die Sprungziele der State-Changes sind ABSOLUTE Indizes
    // in die Animationstabelle. Beim Import muss jeder um die Differenz der
    // Scheibenanfaenge verschoben werden. Ein vergessener oder doppelt
    // verschobener Eintrag faellt im Frame-Vergleich oben NICHT auf - die
    // Anim selbst ist ja intakt -, schickt den Gegner aber zur Laufzeit in
    // eine fremde Anim mit fremder Meshzahl: Bounds Null, unsichtbar.
    {
        const int delta = ma.animIndex - mb.animIndex;
        std::printf("\nVerkettungen (Soll-Verschiebung %+d):\n", delta);

        size_t chA = 0, chB = 0, rgA = 0, rgB = 0;
        int32_t nChA = 0, nChB = 0, nRgA = 0, nRgB = 0;
        a.debugChangeBlock(chA, nChA); b.debugChangeBlock(chB, nChB);
        a.debugRangeBlock(rgA, nRgA);  b.debugRangeBlock(rgB, nRgB);
        const std::vector<uint8_t>& ba = a.debugBytes();
        const std::vector<uint8_t>& bb = b.debugBytes();

        int falsch = 0;
        for (int i = 0; i < n; ++i)
        {
            AnimZeile za, zb;
            if (!liesAnim(a, ma.animIndex + i, za) || !liesAnim(b, mb.animIndex + i, zb)) break;

            if (za.nextAnim != zb.nextAnim + delta)
            {
                std::printf("  Anim %3d: nextAnim %d, Spender %d -> erwartet %d  <-- FALSCH\n",
                            i, za.nextAnim, zb.nextAnim, zb.nextAnim + delta);
                ++falsch;
            }

            // Change-Eintrag: u16 state, u16 rangeCount, u16 rangeIndex (6 B).
            // Range-Eintrag: s16 start, s16 end, s16 linkAnim, s16 linkFrame.
            uint16_t ncA = 0, ciA = 0, ncB = 0, ciB = 0;
            {
                size_t at = 0; int32_t cnt = 0; a.debugAnimBlock(at, cnt);
                std::memcpy(&ncA, ba.data() + at + (size_t)(ma.animIndex + i) * 40 + 32, 2);
                std::memcpy(&ciA, ba.data() + at + (size_t)(ma.animIndex + i) * 40 + 34, 2);
                b.debugAnimBlock(at, cnt);
                std::memcpy(&ncB, bb.data() + at + (size_t)(mb.animIndex + i) * 40 + 32, 2);
                std::memcpy(&ciB, bb.data() + at + (size_t)(mb.animIndex + i) * 40 + 34, 2);
            }
            if (ncA != ncB) { std::printf("  Anim %3d: %u Changes, Spender %u\n", i, ncA, ncB); ++falsch; continue; }

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
                        std::printf("  Anim %3d Change %d Range %d: Ziel %d, Spender %d -> erwartet %d  <-- FALSCH\n",
                                    i, c, r, laA, laB, laB + delta);
                        ++falsch;
                    }
                }
            }
        }
        std::printf("%d falsche Verkettungen\n", falsch);
    }
    return 0;
}

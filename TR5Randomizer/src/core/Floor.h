#pragma once
//----------------------------------------------------------------------------
// Bodenneigung eines Sektors - geteiltes Wissen zwischen ApplyPatch (liest
// die Floordata), dem Randomizer (platziert Items) und PruefBoden (prueft
// fertige Level).
//
// Alles hier ist WOERTLICH aus der Engine portiert, nichts erfunden:
//  - GetHeight, control.cpp:1013-1035 (TILT) und 1078-1160 (Splits):
//    die Bodenhoehe an einem Punkt im Sektor.
//  - CheckNoColFloorTriangle, control.cpp:2960-3009: welches Dreieck eines
//    NOCOLF-Sektors keine Bodenkollision hat.
//  - Die Belaufbarkeitsgrenze: |Neigung| > 2 setzt height_type auf
//    BIG_SLOPE bzw. DIAGONAL (control.cpp:1019-1022 und 1148-1149), und
//    genau darauf pruefen Laras Steh-, Lauf- und Rutschwege (lara.cpp:1428,
//    collide.cpp:160ff). Bis Neigung 2 steht und geht Lara normal - solche
//    Schraegen sind vollwertige Itemplaetze, Core selbst legt dort Pickups
//    ab (mit handgesetztem y auf der echten Bodenhoehe).
//
// Warum das noetig ist: ein platziertes Item wird von der Engine genau auf
// seinem y gezeichnet. Der Sektor-Grundwert (floor-Byte << 8) ist auf einer
// Schraege aber die HOECHSTE Ecke - ein Item in der Sektormitte hinge damit
// bis zu einen halben Sektor in der Luft (im Spiel gesehen, #86). Wer auf
// Schraegen platzieren will, muss das y mit derselben Formel ausrechnen,
// mit der die Engine den Boden findet.
//----------------------------------------------------------------------------
#include <cstdint>

// Der Neigungseintrag eines Sektors, roh: erstes Wort (Funktion + hadj-
// Felder) und das Zusatzwort (Neigungen bzw. Ecknibbles). Sektoren ohne
// Eintrag sind eben und brauchen keine SlopeInfo.
struct SlopeInfo
{
    uint16_t typeWord = 0;
    uint16_t dataWord = 0;
};

// Marke fuer "Floordata-Kette unlesbar": floorPosAt liefert dann
// walkable = false, der Sektor bleibt fuer Items tabu.
const uint16_t kSlopeUnreadable = 0xFFFF;

struct FloorPos
{
    int  y = 0;             // Bodenhoehe an der angefragten Stelle
    bool walkable = true;   // false: Rutsche (>2) oder kollisionsfreies Dreieck
};

// Bodenlage an einem Punkt im Sektor. localX/localZ sind die Lage INNERHALB
// des Sektors (0..1023) - Raumurspruenge sind sektorausgerichtet, deshalb ist
// das genau das (x & 1023) der Engine und die Weltkoordinate wird nicht
// gebraucht. baseY ist der Sektor-Grundwert (floor-Byte << 8; im Zonen-Export
// das floorY des Sektors).
inline FloorPos floorPosAt(const SlopeInfo& si, int baseY, int localX, int localZ)
{
    FloorPos out;
    out.y = baseY;

    if (si.typeWord == kSlopeUnreadable) { out.walkable = false; return out; }

    const int fn = si.typeWord & 0x1F;
    const int dx = localX & 1023;
    const int dz = localZ & 1023;

    int xoff = 0, yoff = 0;

    if (fn == 2)                                        // TILT_TYPE
    {
        // control.cpp:1014-1015: Vorzeichenbytes des Datenworts.
        xoff = static_cast<int16_t>(si.dataWord) >> 8;
        yoff = static_cast<int8_t>(si.dataWord & 0xFF);
    }
    else if (fn == 7 || fn == 8 || (fn >= 11 && fn <= 14))   // SPLIT1/2, NOCOLF*
    {
        // control.cpp:1084-1143. Familie 1 (SPLIT1/NOCOLF1*) teilt entlang
        // der einen Diagonale, Familie 2 entlang der anderen; je Dreieck
        // gelten eigene Neigungen (aus den Ecknibbles) und ein eigener
        // Hoehenversatz (hadj, 5 Bit mit Vorzeichen im Typwort).
        const int t0 = si.dataWord & 0xF;
        const int t1 = (si.dataWord >> 4) & 0xF;
        const int t2 = (si.dataWord >> 8) & 0xF;
        const int t3 = (si.dataWord >> 12) & 0xF;

        const bool family1 = (fn == 7 || fn == 11 || fn == 12);
        const bool firstHalf = family1 ? (dx > 1024 - dz) : (dx > dz);

        int hadj;
        if (firstHalf)
        {
            hadj = (si.typeWord >> 5) & 0x1F;
            if (hadj & 0x10) hadj |= ~0x1F;
            if (family1) { xoff = t3 - t0; yoff = t3 - t2; }
            else          { xoff = t3 - t0; yoff = t0 - t1; }
        }
        else
        {
            hadj = (si.typeWord >> 10) & 0x1F;
            if (hadj & 0x10) hadj |= ~0x1F;
            if (family1) { xoff = t2 - t1; yoff = t0 - t1; }
            else          { xoff = t2 - t1; yoff = t3 - t2; }
        }
        out.y += hadj << 8;

        // CheckNoColFloorTriangle (control.cpp:2976-3002): liegt der Punkt
        // im kollisionsfreien Dreieck, gibt es dort keinen Boden.
        if ((fn == 11 && dx <= 1024 - dz) ||    // NOCOLF1T
            (fn == 12 && dx >  1024 - dz) ||    // NOCOLF1B
            (fn == 13 && dx <= dz)         ||   // NOCOLF2T
            (fn == 14 && dx >  dz))             // NOCOLF2B
            out.walkable = false;
    }
    else
        return out;                              // kein Bodeneintrag: eben

    // control.cpp:1024-1032 bzw. 1151-1159, zeichengleich uebernommen.
    if (xoff < 0) out.y -= (xoff * dz) >> 2;
    else          out.y += (xoff * ((-1 - dz) & 1023)) >> 2;
    if (yoff < 0) out.y -= (yoff * dx) >> 2;
    else          out.y += (yoff * ((-1 - dx) & 1023)) >> 2;

    // BIG_SLOPE/DIAGONAL: Lara rutscht, dort kann sie nichts aufheben.
    if (xoff < -2 || xoff > 2 || yoff < -2 || yoff > 2)
        out.walkable = false;

    return out;
}

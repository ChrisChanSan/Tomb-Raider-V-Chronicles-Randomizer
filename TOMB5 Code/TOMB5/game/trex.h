#pragma once
#include "../global/types.h"

//----------------------------------------------------------------------------
// T-Rex aus Tomb Raider 3 (#88). Portiert aus der tomb3-Dekompilation
// (game/trex.cpp), die wie TOMB5 unter GPLv3 steht.
//
// Die Zustandsnummern sind die des TR3-Modells und muessen es bleiben - sie
// stehen so in den Animationen, die der Randomizer aus CRASH.TR2 holt.
//----------------------------------------------------------------------------
enum TREX_STATES
{
    DINO_EMPTY1,
    DINO_STOP,
    DINO_WALK,
    DINO_RUN,
    DINO_EMPTY2,
    DINO_DEATH,
    DINO_ROAR,
    DINO_ATTACK2,
    DINO_KILL,
    DINO_LONGROARSTART,
    DINO_LONGROARMID,
    DINO_LONGROAREND,
    DINO_SNIFFSTART,
    DINO_SNIFFMID,
    DINO_SNIFFEND
};

void DinoControl(short item_number);

// setup.cpp bindet box.h nicht ein, braucht aber den Standard-Initialisierer
// fuer den T-Rex-Slot - deshalb hier mitgereicht.
void InitialiseCreature(short item_number);

#pragma once
#include "../global/types.h"

//----------------------------------------------------------------------------
// T-Rex from Tomb Raider 3 (#88). Ported from the tomb3 decompilation
// (game/trex.cpp), which like TOMB5 stands under GPLv3.
//
// The state numbers are the TR3 model's and must stay so - they are baked
// into the animations the randomizer fetches from CRASH.TR2.
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

// setup.cpp does not include box.h but needs the standard initializer for
// the T-Rex slot - hence passed along here.
void InitialiseCreature(short item_number);

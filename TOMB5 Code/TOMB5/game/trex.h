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
#pragma once
#include "../global/types.h"

//----------------------------------------------------------------------------
// T-Rex from Tomb Raider 3. Ported from the tomb3 decompilation
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

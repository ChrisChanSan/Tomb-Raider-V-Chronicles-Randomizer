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
/*
 * Compsognathus from Tomb Raider 3, ported to TOMB5 in 2026 (#99).
 *
 * Taken from tomb3 (game/compy.h, GPLv3) - the same license as TOMB5 and
 * this project. The code is deliberately taken over directly instead of
 * rewritten: rewriting would obscure its origin.
 *
 * CarcassControl from the same source file stays out - the carcass is its
 * own TR3 object that we do not bring along. See compy.cpp.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 */
#pragma once
#include "../global/types.h"

void InitialiseCompy(short item_number);
void CompyControl(short item_number);

enum COMPY_STATES
{
	COMPY_STOP,
	COMPY_RUN,
	COMPY_JUMP,
	COMPY_ATTACK,
	COMPY_DEATH
};

extern long compy_scared_timer;
extern long compys_attack_lara;

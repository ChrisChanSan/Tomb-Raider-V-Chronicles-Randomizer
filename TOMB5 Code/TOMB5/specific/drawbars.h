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

void S_DrawHealthBar(long pos);
void S_DrawAirBar(long pos);
void S_DrawDashBar(long pos);
void S_DrawEnemyBar(long pos);
void S_InitLoadBar(long max);
void S_UpdateLoadBar();
void S_FinishLoadBar();
long S_DrawLoadBar();
void S_LoadBar();
void DoSlider(long x, long y, long width, long height, long pos, long c1, long c2, long c3);

extern long loadbar_on;

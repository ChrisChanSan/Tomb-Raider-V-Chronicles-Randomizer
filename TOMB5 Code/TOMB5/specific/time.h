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

long Sync();

/* How far past the last tick Sync() gave away the clock stands, as a
 * fraction of a 1/60 s tick. Reads the remainder without consuming it, is
 * not clamped below one, and is the only sub-tick reading in the engine. */
double TIME_SubTick();

void TIME_Reset();
bool TIME_Init();

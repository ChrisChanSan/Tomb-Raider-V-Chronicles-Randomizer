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

void GetLaraDeadlyBounds();
void InitialiseLaraAnims(ITEM_INFO* item);
void InitialiseLaraLoad(short item_num);
void LaraCheat(ITEM_INFO* item, COLL_INFO* coll);
void LaraCheatEnde(ITEM_INFO* item);
void LaraInitialiseMeshes();
void LaraCheatGetStuff();
void AnimateLara(ITEM_INFO* item);
void LaraCheatyBits();
void LaraControl(short item_number);

extern COLL_INFO mycoll;

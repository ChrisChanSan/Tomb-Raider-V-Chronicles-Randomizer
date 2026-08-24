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

void SaveGamePath(char* buffer, long slot_num);
void EnsureSaveGameFolder();
long S_SaveGame(long slot_num);
long S_LoadGame(long slot_num);
bool GameInitialise();
unsigned int __stdcall GameMain(void* ptr);
void GameClose();

extern WATERTAB WaterTable[22][64];
extern THREAD MainThread;
extern float vert_wibble_table[32];
extern short* clipflags;
extern long SaveCounter;

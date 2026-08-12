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

void WriteSG(void* pointer, long size);
void ReadSG(void* pointer, long size);
void SaveLaraData();
void RestoreLaraData(long FullSave);
void SaveLevelData(long FullSave);
void RestoreLevelData(long FullSave);
void sgSaveGame();
void sgRestoreGame();

extern SAVEGAME_INFO savegame;
extern tomb5_save_info tomb5_save;
extern ulong tomb5_save_size;

// Actual byte count the last ReadFile(..., &savegame, sizeof(SAVEGAME_INFO), ...)
// call in specific/gamemain.cpp's S_LoadGame() returned - set there, checked in
// sgRestoreGame() to detect an old-format save (written before buffer[] was
// resized) before ever applying it. Same technique this codebase already uses
// for tomb5_save_size/tomb5_save_info, applied to the main savegame struct too.
extern ulong savegame_bytes_read;

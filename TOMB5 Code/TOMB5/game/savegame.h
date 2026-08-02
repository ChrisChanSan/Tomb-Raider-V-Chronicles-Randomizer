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

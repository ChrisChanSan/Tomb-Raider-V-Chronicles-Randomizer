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
#include "../tomb5/pch.h"
#include "gamemain.h"
#include "function_stubs.h"
#include "audio.h"
#include "file.h"
#include "function_table.h"
#include "3dmath.h"
#include "../game/text.h"
#include "time.h"
#include "winmain.h"
#include "../game/sound.h"
#include "../game/gameflow.h"
#include "dxshell.h"
#include "specificfx.h"
#include "../game/savegame.h"
#include "../game/balance.h"
#include "../game/secretreward.h"
#include "input.h"			// ApplyKeyScheme - the one-time keyboard seat
#include "../tomb5/tomb5.h"
#include "zrange_math.h"		// ZFAR_UNITS - the far plane

WATERTAB WaterTable[22][64];
THREAD MainThread;
float vert_wibble_table[32];
short* clipflags;
long SaveCounter;

static ushort GetRandom(WATERTAB* wt, long lp)
{
	long loop;
	ushort ret;

	do
	{
		ret = rand() & 0xFC;
		
		for (loop = 0; loop < lp; loop++)
		{
			if (wt[loop].random == ret)
				break;
		}

	} while (loop != lp);

	return ret;
}

static void init_water_table()
{
	float fSin;
	short sSin;
	static short water_shimmer[4] = { 31, 63, 95, 127 };
	static short water_choppy[4] = { 16, 53, 90, 127 };
	static uchar water_abs[4] = { 4, 8, 12, 16 };

	srand(121197);

	for (int i = 0; i < 64; i++)
	{
		sSin = rcossin_tbl[i << 7];
		WaterTable[0][i].shimmer = (63 * sSin) >> 15;
		WaterTable[0][i].choppy = (16 * sSin) >> 12;
		WaterTable[0][i].random = (uchar)GetRandom(WaterTable[0], i);
		WaterTable[0][i].abs = 0;

		WaterTable[1][i].shimmer = (32 * sSin) >> 15;
		WaterTable[1][i].choppy = 0;
		WaterTable[1][i].random = (uchar)GetRandom(WaterTable[1], i);
		WaterTable[1][i].abs = -3;

		WaterTable[2][i].shimmer = (64 * sSin) >> 15;
		WaterTable[2][i].choppy = 0;
		WaterTable[2][i].random = (uchar)GetRandom(WaterTable[2], i);
		WaterTable[2][i].abs = 0;

		WaterTable[3][i].shimmer = (96 * sSin) >> 15;
		WaterTable[3][i].choppy = 0;
		WaterTable[3][i].random = (uchar)GetRandom(WaterTable[3], i);
		WaterTable[3][i].abs = 4;

		WaterTable[4][i].shimmer = (127 * sSin) >> 15;
		WaterTable[4][i].choppy = 0;
		WaterTable[4][i].random = (uchar)GetRandom(WaterTable[4], i);
		WaterTable[4][i].abs = 8;

		for (int j = 0, k = 5; j < 4; j++, k += 4)
		{
			for (int m = 0; m < 4; m++)
			{
				WaterTable[k + m][i].shimmer = -((sSin * water_shimmer[m]) >> 15);
				WaterTable[k + m][i].choppy = sSin * water_choppy[j] >> 12;
				WaterTable[k + m][i].random = (uchar)GetRandom(WaterTable[k + m], i);
				WaterTable[k + m][i].abs = water_abs[m];
			}
		}
	}

	for (int i = 0; i < 32; i++)
	{
		fSin = sin(i * 0.19634955F);
		vert_wibble_table[i] = fSin + fSin;
	}
}

// The savegames lie in the subfolder savegame\ next to the EXE instead of
// loose in the game folder. SaveGamePath builds the path,
// EnsureSaveGameFolder creates the folder at startup and moves any
// savegame.N still lying in the game folder over (only when the slot in
// the folder is free - MoveFile does not overwrite).
void SaveGamePath(char* buffer, long slot_num)
{
	wsprintf(buffer, "savegame\\savegame.%d", slot_num);
}

void EnsureSaveGameFolder()
{
	char oldPath[32], newPath[32];

	CreateDirectory("savegame", 0);

	for (int i = 0; i < 15; i++)
	{
		wsprintf(oldPath, "savegame.%d", i);

		if (GetFileAttributes(oldPath) == INVALID_FILE_ATTRIBUTES)
			continue;

		SaveGamePath(newPath, i);

		if (MoveFile(oldPath, newPath))
			Log("Moved %s into savegame folder", oldPath);
	}
}

long S_SaveGame(long slot_num)
{
	HANDLE file;
	ulong bytes;
	long days, hours, minutes, seconds;
	char buffer[80], counter[16];

	for (int i = 0; i < 20; i++)
		buffer[i] = '\0';

	SaveGamePath(buffer, slot_num);
	file = CreateFile(buffer, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

	if (file != INVALID_HANDLE_VALUE)
	{
		for (int i = 0; i < 20; i++)
			buffer[i] = '\0';

		wsprintf(buffer, "%s", SCRIPT_TEXT(gfLevelNames[gfCurrentLevel]));
		WriteFile(file, buffer, 75, &bytes, 0);
		WriteFile(file, &SaveCounter, sizeof(long), &bytes, 0);
		days = savegame.Game.Timer / 30 / 86400;
		hours = savegame.Game.Timer / 30 % 86400 / 3600;
		minutes = savegame.Game.Timer / 30 / 60 % 60;
		seconds = savegame.Game.Timer / 30 % 60;
		WriteFile(file, &days, 2, &bytes, 0);
		WriteFile(file, &hours, 2, &bytes, 0);
		WriteFile(file, &minutes, 2, &bytes, 0);
		WriteFile(file, &seconds, 2, &bytes, 0);
		WriteFile(file, &savegame, sizeof(SAVEGAME_INFO), &bytes, 0);
		WriteFile(file, &tomb5_save, sizeof(tomb5_save_info), &bytes, 0);
		CloseHandle(file);
		wsprintf(counter, "%d", SaveCounter);
		SaveCounter++;
		return 1;
	}

	return 0;
}

long S_LoadGame(long slot_num)
{
	HANDLE file;
	ulong bytes;
	long value;
	char buffer[80];

	SaveGamePath(buffer, slot_num);
	file = CreateFile(buffer, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (file != INVALID_HANDLE_VALUE)
	{
		ReadFile(file, buffer, 75, &bytes, 0);
		ReadFile(file, &value, sizeof(long), &bytes, 0);
		ReadFile(file, &value, sizeof(long), &bytes, 0);
		ReadFile(file, &value, sizeof(long), &bytes, 0);
		ReadFile(file, &savegame, sizeof(SAVEGAME_INFO), &bytes, 0);
		savegame_bytes_read = bytes; // see sgRestoreGame() - detects an old-format (pre-resize) save

		// Computed immediately, while the bytes are still exactly those
		// from the file. sgRestoreGame() only runs after the fade and the
		// level reload - by then ControlPhase (control.cpp), which keeps
		// incrementing savegame.Level.Timer while gfGameMode is 0, may have
		// run behind the menu. A load started in that window (for example
		// during Lara's death animation) then finds a checksum that no
		// longer matches a fully and correctly read file, and aborts with
		// "incompatible or corrupted savegame".
		{
			const short stored = savegame.Checksum;
			savegame.Checksum = 0;

			char* p = (char*)&savegame;
			char sum = 0;

			for (int i = 0; i < sizeof(SAVEGAME_INFO); i++)
			{
				sum += *p;
				p++;
			}

			savegame.Checksum = stored;
			savegame_checksum_ok = (stored == (short)-sum);
		}
		ReadFile(file, &tomb5_save, sizeof(tomb5_save_info), &tomb5_save_size, 0);
		CloseHandle(file);
		return 1;
	}

	return 0;
}

bool GameInitialise()
{
	Log(__FUNCTION__);
	EnsureSaveGameFolder();
	BalanceLoad();
	// Beside it, and for the same reason: the reward for a level's secrets
	// is the randomizer's to roll and the player's to set.
	SecretRewardLoad();
	init_game_malloc();
	clipflags = (short*)malloc(0x8000);
	init_water_table();
	aInitFX();
	return 1;
}

unsigned int __stdcall GameMain(void* ptr)
{
	Log(__FUNCTION__);

	if (GameInitialise())
	{
		InitialiseFunctionTable();
		HWInitialise();
		InitWindow(0, 0, App.dx.dwRenderWidth, App.dx.dwRenderHeight,
			ZNEAR_UNITS, ZFAR_UNITS, 80, App.dx.dwRenderWidth,
			App.dx.dwRenderHeight);
		InitFont();
		TIME_Init();
		App.SetupComplete = 1;
		S_CDStop();
		ClearSurfaces();

		if (!App.SoundDisabled)
			SOUND_Init();

		RPC_Init();
		init_tomb5_stuff();
		/* The first point in startup where the keyboard scheme can be
		 * seated. LoadSettings() (winmain.cpp) reads both key sets from the
		 * registry early in WinMain, but init_tomb5_stuff() just above is
		 * what reads tomb5.control_modern to tell them apart, so the seat
		 * cannot happen any earlier. If the scheme already active is the
		 * one selected, ApplyKeyScheme (input.cpp) returns without
		 * touching anything. */
		ApplyKeyScheme();
		/* Same reasoning for the pad: tomb5.action_split lives in the same
		 * registry key, so this is the first point the two pad sets can be
		 * told apart. If the setting already active is the one selected,
		 * the call returns without touching anything. */
		ApplyJoySplit();
		DoGameflow();
		GameClose();
		S_CDStop();
		RPC_close();

		PostMessage(App.hWnd, WM_CLOSE, 0, 0);
		MainThread.active = 0;
		_endthreadex(1);
	}

	return 1;
}

void GameClose()
{
	Log(__FUNCTION__);
	ACMClose();
	FreeLevel();
	free(clipflags);

	if (ADPCMBuffer)
		free(ADPCMBuffer);

	if (logF)
		fclose(logF);

	free(malloc_buffer);
	free(gfScriptFile);
	free(gfLanguageFile);
}

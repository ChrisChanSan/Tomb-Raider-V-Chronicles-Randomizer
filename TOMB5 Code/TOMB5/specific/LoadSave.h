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

void CheckKeyConflicts();
void DisplayStatsUCunt();
long DoLoadSave(long LoadSave);
long GetCampaignCheatValue();
void DoOptions();
void CreateMonoScreen();
void RGBM_Mono(uchar* r, uchar* g, uchar* b);
void ConvertSurfaceToTextures(LPDIRECT3DSURFACE9 surface);
long CaptureLoadingScreen();
void FreeMonoScreen();

// The picture's texture onto a new device (specific/devicemove.h).
struct DeviceMoveCount;
long MonoScreenToDevice(LPDIRECT3DDEVICE9 device, DeviceMoveCount* count);
void S_DrawTile(long x, long y, long w, long h, LPDIRECT3DTEXTURE9 t, long tU, long tV, long tW, long tH, long c0, long c1, long c2, long c3, long filter);
void S_DisplayMonoScreen();
long S_LoadSave(long load_or_save, long mono, long inv_active);
void LoadScreen(long screen, long pathNum);

// Is the HD path usable at all? Checks the BC7 decoder's own tables once.
long LoadScreenHDReady();

// Optional replacement for LoadScreen(): the picture out of screens\HD named
// after the level, decoded from BC7. Returns 0 when there is none, and the
// caller then falls back to LoadScreen().
long LoadScreenHD(const char* name);

// The same for the title screen, which is composed out of two files:
// screens\HD\TITLE plus the wordmark cut out of the language's LOGO_xx.
long LoadScreenHDLogo();
void ReleaseScreen();
void DrawLoadingScreen();
// The loading picture, for -log: 0 none, 1 classic, 2 HD, 3 read back.
long LoadingPictureKind();
long GetSaveLoadFiles();
long S_DisplayPauseMenu(long reset);
long S_PauseMenu();
void DoFrontEndOneShotStuff();
long FindSFCursor(long in, long selection);

// The Special Features page, one frame of it. It stays in LoadSave.cpp
// because it reads three file-statics there; the menu frame
// (game\menu_frame.cpp) runs it as a page of the tree. 1 = the player left it.
long SpecialFeaturesPageRun();
void CalculateNumSpecialFeatures();
void SpecialFeaturesDisplayScreens(long num);
void DoSpecialFeaturesServer();

extern long sfx_frequencies[3];
extern long SoundQuality;
extern long MusicVolume;
extern long SFXVolume;
extern long ControlMethod;
extern char MonoScreenOn;

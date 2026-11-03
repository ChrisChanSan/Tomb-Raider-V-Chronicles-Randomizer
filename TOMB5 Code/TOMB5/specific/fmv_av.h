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

/* The films of the 64-bit build, through FFmpeg's Bink decoders.
 *
 * The game's binkw32.dll is 32-bit code. The 64-bit build plays the same
 * movie\fmv*.bik through three FFmpeg DLLs beside the EXE (avutil, avcodec,
 * avformat - vendor\ffmpeg-min, nothing in them but Bink and the file
 * protocol), loaded at run time; no import library is linked. fmv.cpp
 * routes its public functions here under _WIN64, so the callers in the game
 * keep their names; the 32-bit build compiles none of this. */

#ifdef _WIN64
bool FmvAvLoad();			// LoadBinkStuff: the three DLLs, every function
void FmvAvFree();			// FreeBinkStuff
void FmvAvDeviceLost();		// FmvDeviceLost: the texture goes with its device
long FmvAvPlay(long num);	// PlayFmv
#endif

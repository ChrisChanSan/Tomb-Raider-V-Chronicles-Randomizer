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

void S_CDPlay(long track, long mode);
void S_CDStop();
void S_CDFade(long n);
void S_StartSyncedAudio(long track);
void ACMSetVolume();
void ACMEmulateCDPlay(long track, long mode);
bool ACMInit();
void ACMClose();

extern HACMDRIVER hACMDriver;
extern uchar* ADPCMBuffer;
extern bool acm_ready;
extern long XATrack;

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

float DSVolumeToLinear(long volume);
bool DXChangeOutputFormat(long nSamplesPerSec, bool force);
void DSChangeVolume(long num, long volume);
void DSAdjustPitch(long num, long pitch);
void DSAdjustPan(long num, long pan);
bool DXSetOutputFormat();
bool DXDSCreate();
bool InitSampleDecompress();
bool FreeSampleDecompress();
bool DXCreateSampleADPCM(char* data, long comp_size, long uncomp_size, long num);
void DXStopSample(long num);
bool DSIsChannelPlaying(long num);
long DSGetFreeChannel();
long DXStartSample(long num, long volume, long pitch, long pan, ulong flags);
long CalcVolume(long volume);
void S_SoundStopAllSamples();
void S_SoundStopSample(long num);
long S_SoundPlaySample(long num, ushort volume, long pitch, short pan);
long S_SoundPlaySampleLooped(long num, ushort volume, long pitch, short pan);
void DXFreeSounds();
long S_SoundSampleIsPlaying(long num);
void S_SoundSetPanAndVolume(long num, short pan, ushort volume);
void S_SoundSetPitch(long num, long pitch);
bool DXCreateSample(long num, LPWAVEFORMATEX format, LPVOID data, ulong bytes);

extern char* samples_buffer;
extern IXAudio2* g_XAudio2;
extern IXAudio2MasteringVoice* g_MasteringVoice;

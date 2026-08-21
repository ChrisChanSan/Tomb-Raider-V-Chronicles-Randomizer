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
#include "dxsound.h"
#include "function_stubs.h"
#include "dxshell.h"
#include "winmain.h"
#include "LoadSave.h"
#include "../game/sound.h"
#include "audio.h"

char* samples_buffer;

IXAudio2* g_XAudio2;
IXAudio2MasteringVoice* g_MasteringVoice;

// SFX are decoded once into plain PCM (unchanged ACM decode step) and kept in memory here;
// XAudio2 voices SubmitBuffer directly against this shared data instead of the old
// DuplicateSoundBuffer-per-play DirectSound pattern.
struct SFX_BUFFER
{
	BYTE* pcm;
	ulong bytes;
	ulong frequency;
};

// A small pool of reusable source voices (one per concurrently playable channel), instead of
// creating/destroying a DirectSound buffer instance per play.
struct SFX_VOICE
{
	IXAudio2SourceVoice* voice;
	long playing;
};

// Frueher fest 256. Die Soundmap des Levelformats fasst MAX_SAMPLES (450)
// Eintraege, und sample_lut wird in LoadSamples() auch mit MAX_SAMPLES
// angelegt - DS_Buffers war also enger als der Rest des eigenen Soundsystems.
// Cores Level bleiben weit darunter, aber jeder Fremdimport bringt Klaenge
// mit, und die TR3-Gegner haben den Vorrat spuerbar angeknabbert (#94).
//
// Kosten: reine Verwaltung, 450 * 12 Byte statt 256 * 12 - die Klangdaten
// selbst haengen als malloc daran und wachsen nur mit dem, was ein Level
// wirklich mitbringt.
static SFX_BUFFER DS_Buffers[MAX_SAMPLES];
static SFX_VOICE DS_Samples[32];

#pragma warning(push)
#pragma warning(disable : 4838)
#pragma warning(disable : 4309)
static char source_pcm_format[50] =
{
	2, 0, 1, 0, 34, 86, 0, 0, 147, 43, 0, 0, 0, 2, 4, 0, 32, 0, 244, 3, 7, 0, 0, 1, 0, 0, 0, 2, 0, 255, 0, 0,
	0, 0, 192, 0, 64, 0, 240, 0, 0, 0, 204, 1, 48, 255, 136, 1, 24, 255
};
#pragma warning(pop)

static HACMSTREAM d_hACMStream;
static MMRESULT mmresult;
static WAVEFORMATEX pcm_format;
static ACMSTREAMHEADER ACMStreamHeader;
static char* decompressed_samples_buffer;

// DirectSound's SetVolume/pan use hundredths-of-a-decibel (logarithmic, 0 = full, -10000 =
// silence) - CalcVolume() already produces exactly that, unchanged. XAudio2's SetVolume is a
// linear amplitude multiplier, so convert at the API boundary rather than touching the
// existing volume-curve math. Shared with the music path in audio.cpp (#100).
float DSVolumeToLinear(long volume)
{
	if (volume <= -10000)
		return 0.0F;

	if (volume >= 0)
		return 1.0F;

	return powf(10.0F, (float)volume / 2000.0F);
}

static IXAudio2SourceVoice* CreateSfxVoice(ulong hz)
{
	IXAudio2SourceVoice* voice;
	WAVEFORMATEX wfx;

	memset(&wfx, 0, sizeof(wfx));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 1;
	wfx.nSamplesPerSec = hz;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	if (FAILED(g_XAudio2->CreateSourceVoice(&voice, &wfx, 0, 8.0F, 0, 0, 0)))
		return 0;

	return voice;
}

bool DXChangeOutputFormat(long nSamplesPerSec, bool force)
{
	static long lastSPC;

	if (!force && lastSPC == nSamplesPerSec)
		return 1;

	lastSPC = nSamplesPerSec;
	S_SoundStopAllSamples();
	return 1;
}

void DSChangeVolume(long num, long volume)
{
	if (DS_Samples[num].voice)
		DS_Samples[num].voice->SetVolume(DSVolumeToLinear(volume));
}

void DSAdjustPitch(long num, long pitch)
{
	ulong frequency;

	if (DS_Samples[num].voice)
	{
		frequency = ulong((float)pitch / 65536.0F * 22050.0F);

		if (frequency < 100)
			frequency = 100;
		else if (frequency > 100000)
			frequency = 100000;

		DS_Samples[num].voice->SetFrequencyRatio((float)frequency / (float)pcm_format.nSamplesPerSec);
	}
}

void DSAdjustPan(long num, long pan)
{
	float matrix[2];
	float norm, angle;

	if (DS_Samples[num].voice)
	{
		norm = (float)pan / 32768.0F;

		if (norm < -1.0F)
			norm = -1.0F;
		else if (norm > 1.0F)
			norm = 1.0F;

		// equal-power pan law
		angle = (norm + 1.0F) * 0.25F * 3.14159265F;
		matrix[0] = cosf(angle);
		matrix[1] = sinf(angle);
		DS_Samples[num].voice->SetOutputMatrix(0, 1, 2, matrix);
	}
}

bool DXSetOutputFormat()
{
	return 1;
}

bool DXDSCreate()
{
	Log(__FUNCTION__);

	// No IDirectSound object anymore: music and SFX run on XAudio2, and Bink FMV
	// audio goes through BinkOpenWaveOut (fmv.cpp) since the WaveOut switch.
	if (FAILED(XAudio2Create(&g_XAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
	{
		Log("XAudio2Create Failed");
		return 0;
	}

	if (FAILED(g_XAudio2->CreateMasteringVoice(&g_MasteringVoice)))
	{
		Log("CreateMasteringVoice Failed");
		return 0;
	}

	sound_active = 1;
	return 1;
}

bool InitSampleDecompress()
{
	pcm_format.wFormatTag = WAVE_FORMAT_PCM;
	pcm_format.cbSize = 0;
	pcm_format.nChannels = 1;
	pcm_format.nAvgBytesPerSec = 44100;
	pcm_format.nSamplesPerSec = 22050;
	pcm_format.nBlockAlign = 2;
	pcm_format.wBitsPerSample = 16;
	mmresult = acmStreamOpen(&d_hACMStream, hACMDriver, (LPWAVEFORMATEX)source_pcm_format, &pcm_format, 0, 0, 0, 0);

	if (mmresult != DS_OK)
		Log("Stream Open %d", mmresult);

	decompressed_samples_buffer = (char*)malloc(0x40000);
	samples_buffer = (char*)malloc(0x4005A);
	memset(&ACMStreamHeader, 0, sizeof(ACMSTREAMHEADER));
	ACMStreamHeader.pbSrc = (uchar*)(samples_buffer + 90);
	ACMStreamHeader.cbStruct = sizeof(ACMSTREAMHEADER);
	ACMStreamHeader.cbSrcLength = 0x40000;
	ACMStreamHeader.cbDstLength = 0x40000;
	ACMStreamHeader.pbDst = (uchar*)decompressed_samples_buffer;
	mmresult = acmStreamPrepareHeader(d_hACMStream, &ACMStreamHeader, 0);

	if (mmresult != DS_OK)
		Log("Prepare Stream %d", mmresult);

	return 1;
}

bool FreeSampleDecompress()
{
	ACMStreamHeader.cbSrcLength = 0x40000;
	mmresult = acmStreamUnprepareHeader(d_hACMStream, &ACMStreamHeader, 0);

	if (mmresult != DS_OK)
		Log("UnPrepare Stream %d", mmresult);

	mmresult = acmStreamClose(d_hACMStream, 0);

	if (mmresult != DS_OK)
		Log("Stream Close %d", mmresult);

	free(decompressed_samples_buffer);
	free(samples_buffer);
	return 1;
}

bool DXCreateSampleADPCM(char* data, long comp_size, long uncomp_size, long num)
{
	LPWAVEFORMATEX format;
	ulong dekodiert;

	Log(__FUNCTION__);

	if (!g_XAudio2)
		return 0;

	// Siehe DXCreateSample: eigene Schranke, damit der Schreibzugriff unten
	// nicht am Aufrufer haengt (#94).
	if (num < 0 || num >= MAX_SAMPLES)
	{
		Log("DXCreateSampleADPCM: sample %d outside DS_Buffers[%d]", num, MAX_SAMPLES);
		return 0;
	}

	format = (LPWAVEFORMATEX)(data + 20);

	if (format->nSamplesPerSec != 22050)
		Log("Incorrect SamplesPerSec");

	ACMStreamHeader.cbSrcLength = comp_size - (ushort)format->cbSize - 58;
	mmresult = acmStreamConvert(d_hACMStream, &ACMStreamHeader, ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_START);

	if (mmresult != DS_OK)
		Log("Stream Convert %d", mmresult);

	if (DS_Buffers[num].pcm)
	{
		free(DS_Buffers[num].pcm);
		DS_Buffers[num].pcm = 0;
	}

	// Nur uebernehmen, was die Umsetzung wirklich geliefert hat: sie arbeitet
	// blockweise und bleibt gern unter uncomp_size - 32. Der alte Code (schon
	// bei Core so) kopierte trotzdem die volle Laenge - das Ende war dann
	// Altbestand des zuvor dekodierten Samples und knackte hoerbar.
	dekodiert = ACMStreamHeader.cbDstLengthUsed;
	DS_Buffers[num].bytes = uncomp_size - 32;
	DS_Buffers[num].pcm = (BYTE*)malloc(DS_Buffers[num].bytes);

	if (dekodiert > DS_Buffers[num].bytes)
		dekodiert = DS_Buffers[num].bytes;

	memcpy(DS_Buffers[num].pcm, decompressed_samples_buffer, dekodiert);

	if (dekodiert < DS_Buffers[num].bytes)
	{
		memset(DS_Buffers[num].pcm + dekodiert, 0, DS_Buffers[num].bytes - dekodiert);
		Log("Sample %d: %lu Bytes Rest genullt", num, DS_Buffers[num].bytes - dekodiert);
	}

	DS_Buffers[num].frequency = pcm_format.nSamplesPerSec;
	return 1;
}

void DXStopSample(long num)
{
	if (num >= 0 && DS_Samples[num].voice)
	{
		DS_Samples[num].voice->Stop(0);
		DS_Samples[num].voice->FlushSourceBuffers();
		DS_Samples[num].playing = 0;
	}
}

bool DSIsChannelPlaying(long num)
{
	XAUDIO2_VOICE_STATE state;

	if (DS_Samples[num].voice)
	{
		DS_Samples[num].voice->GetState(&state);

		if (state.BuffersQueued > 0)
			return 1;

		DXStopSample(num);
	}

	return 0;
}

long DSGetFreeChannel()
{
	for (int i = 0; i < 32; i++)
	{
		if (!DSIsChannelPlaying(i))
			return i;
	}

	return -1;
}

long DXStartSample(long num, long volume, long pitch, long pan, ulong flags)
{
	XAUDIO2_BUFFER buf;
	long channel;

	channel = DSGetFreeChannel();

	if (channel < 0)
		return -1;

	if (!DS_Buffers[num].pcm)
		return -1;

	if (!DS_Samples[channel].voice)
	{
		DS_Samples[channel].voice = CreateSfxVoice(pcm_format.nSamplesPerSec);

		if (!DS_Samples[channel].voice)
			return -1;
	}

	DS_Samples[channel].voice->Stop(0);
	DS_Samples[channel].voice->FlushSourceBuffers();

	memset(&buf, 0, sizeof(buf));
	buf.AudioBytes = DS_Buffers[num].bytes;
	buf.pAudioData = DS_Buffers[num].pcm;
	buf.Flags = XAUDIO2_END_OF_STREAM;
	buf.LoopCount = (flags & DSBPLAY_LOOPING) ? XAUDIO2_LOOP_INFINITE : 0;

	if (FAILED(DS_Samples[channel].voice->SubmitSourceBuffer(&buf)))
		return -1;

	DS_Samples[channel].playing = num;
	DS_Samples[channel].voice->SetVolume(DSVolumeToLinear(volume));
	DSAdjustPitch(channel, pitch);
	DSAdjustPan(channel, pan);
	DS_Samples[channel].voice->Start(0);
	return channel;
}

long CalcVolume(long volume)
{
	long result;

	result = 8000 - long(float(0x7FFF - volume) * 0.30518511F);

	if (result > 0)
		result = 0;
	else if (result < -10000)
		result = -10000;

	result -= (100 - SFXVolume) * 50;

	if (result > 0)
		result = 0;

	if (result < -10000)
		result = -10000;

	return result;
}

void S_SoundStopAllSamples()
{
	for (int i = 0; i < 32; i++)
		DXStopSample(i);
}

void S_SoundStopSample(long num)
{
	DXStopSample(num);
}

long S_SoundPlaySample(long num, ushort volume, long pitch, short pan)
{
	return DXStartSample(num, CalcVolume(volume), pitch, pan, 0);
}

long S_SoundPlaySampleLooped(long num, ushort volume, long pitch, short pan)
{
	return DXStartSample(num, CalcVolume(volume), pitch, pan, DSBPLAY_LOOPING);
}

void DXFreeSounds()
{
	S_SoundStopAllSamples();

	for (int i = 0; i < MAX_SAMPLES; i++)
	{
		if (DS_Buffers[i].pcm)
		{
			free(DS_Buffers[i].pcm);
			DS_Buffers[i].pcm = 0;
		}
	}
}

long S_SoundSampleIsPlaying(long num)
{
	if (sound_active && DSIsChannelPlaying(num))
		return 1;

	return 0;
}

void S_SoundSetPanAndVolume(long num, short pan, ushort volume)
{
	if (sound_active)
	{
		DSChangeVolume(num, CalcVolume(volume));
		DSAdjustPan(num, pan);
	}
}

void S_SoundSetPitch(long num, long pitch)
{
	if (sound_active)
		DSAdjustPitch(num, pitch);
}

bool DXCreateSample(long num, LPWAVEFORMATEX format, LPVOID data, ulong bytes)
{
	Log(__FUNCTION__);

	if (!g_XAudio2)
		return 0;

	// Eigene Schranke, statt sich auf den Aufrufer zu verlassen: LoadSamples()
	// prueft die Gesamtzahl, aber diese Funktion schrieb bisher blind in
	// DS_Buffers[num] (#94).
	if (num < 0 || num >= MAX_SAMPLES)
	{
		Log("DXCreateSample: sample %d outside DS_Buffers[%d]", num, MAX_SAMPLES);
		return 0;
	}

	if (DS_Buffers[num].pcm)
	{
		free(DS_Buffers[num].pcm);
		DS_Buffers[num].pcm = 0;
	}

	DS_Buffers[num].bytes = bytes;
	DS_Buffers[num].pcm = (BYTE*)malloc(bytes);
	memcpy(DS_Buffers[num].pcm, data, bytes);
	DS_Buffers[num].frequency = format->nSamplesPerSec;
	return 1;
}

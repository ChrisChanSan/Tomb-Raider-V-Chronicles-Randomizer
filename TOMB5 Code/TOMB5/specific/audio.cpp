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
#include "audio.h"
#include "file.h"
#include "function_stubs.h"
#include "dxsound.h"
#include "../game/control.h"
#include "../game/gameflow.h"	// Gameflow->Language: picks HQ DE or HQ EN
#include "LoadSave.h"

// HQ music: if remaster versions of the tracks lie under
// audio\HQ DE\<nr>.OGG or audio\HQ EN\<nr>.OGG (number = engine track
// number, verified by runtime comparison - the HQ sets carry exactly the
// 52 language tracks), the engine plays them instead of the original WAVs:
// the German ones for German, else the English; if the file is missing,
// the original WAV runs as always. The files are 44.1 kHz stereo - exactly
// the MusicVoice's PCM format, so the same queue suffices.
// Decoder: stb_vorbis (public domain, Sean Barrett et al.). NEVER ship
// them - the OGGs are the user's data, the engine only checks whether
// they are there.
#pragma warning(push, 0)
#define STB_VORBIS_NO_PUSHDATA_API
#include "../tomb5/libs/stb/stb_vorbis.c"
#pragma warning(pop)

const char* TrackFileNames[136] =
{
	"xa1_TL_10B.wav",
	"xa1_Z_10.wav",
	"xa1_TL_05.wav",
	"xa1_TL_08.wav",
	"xa1_TL_11.wav",
	"xa1_ANDYPEW.wav",
	"xa1_SECRET.wav",
	"xa1_TL_02.wav",
	"xa2_HMMM05.wav",
	"xa2_TL_01.wav",
	"xa2_ATTACK04.wav",
	"xa2_UWATER2B.wav",
	"xa2_SPOOKY2A.wav",
	"xa2_TL_10A.wav",
	"xa2_HMMM02.wav",
	"xa2_TOMS01.wav",
	"xa3_Attack03.wav",
	"xa3_Attack02.wav",
	"xa3_Hmmm01.wav",
	"xa3_Stealth1.wav",
	"xa3_Stealth2.wav",
	"xa3_Attack01.wav",
	"xa3_TL_06.wav",
	"xa3_TL_03.wav",
	"xa4_hmmm06.wav",
	"xa4_mil01.wav",
	"xa4_Z_03.wav",
	"xa4_hit01.wav",
	"xa4_spooky05.wav",
	"xa4_drama01.wav",
	"xa4_stealth4.wav",
	"xa4_mil05.wav",
	"xa5_HMMM04.wav",
	"xa5_MIL06.wav",
	"xa5_SPOOKY02.wav",
	"xa5_TL_12.wav",
	"xa5_MIL02A.wav",
	"xa5_HMMM03.wav",
	"xa5_MIL02.wav",
	"xa5_TL_04.wav",
	"xa6_Mil04.wav",
	"xa6_Solo01.wav",
	"xa6_Z12.wav",
	"xa6_Stealth3.wav",
	"xa6_AuthSolo.wav",
	"xa6_Spooky03.wav",
	"xa6_Z13.wav",
	"xa6_Z_04anim.wav",
	"xa7_z_06a.wav",
	"xa7_andyoooh.wav",
	"xa7_andyooer.wav",
	"xa7_tl_07.wav",
	"xa7_z_02.wav",
	"xa7_evibes01.wav",
	"xa7_z_06.wav",
	"xa7_authtr.wav",
	"xa8_mil03.wav",
	"xa8_fightsc.wav",
	"xa8_richcut3.wav",
	"xa8_z_13.wav",
	"xa8_z_08.wav",
	"xa8_uwater2a.wav",
	"xa8_jobyalrm.wav",
	"xa8_mil02b.wav",
	"xa9_swampy.wav",
	"xa9_evibes02.wav",
	"xa9_gods01.wav",
	"xa9_z_03.wav",
	"xa9_richcut4.wav",
	"xa9_title4.wav",
	"xa9_spooky01.wav",
	"xa9_chopin01.wav",
	"xa10_echoir01.wav",
	"xa10_title3.wav",
	"xa10_perc01.wav",
	"xa10_vc01.wav",
	"xa10_title2.wav",
	"xa10_z_09.wav",
	"xa10_spooky04.wav",
	"xa10_z_10.wav",
	"xa11_vc01atv.wav",
	"xa11_andy3.wav",
	"xa11_title1.wav",
	"xa11_flyby1.wav",
	"xa11_monk_2.wav",
	"xa11_andy4.wav",
	"xa11_flyby3.wav",
	"xa11_flyby2.wav",
	"xa12_moses01.wav",
	"xa12_andy4b.wav",
	"xa12_z_10.wav",
	"xa12_flyby4.wav",
	"xa12_richcut1.wav",
	"xa12_andy5.wav",
	"xa12_z_05.wav",
	"xa12_z_01.wav",
	"xa13_Joby3.wav",
	"xa13_Andy7.wav",
	"xa13_Andrea3B.wav",
	"xa13_cossack.wav",
	"xa13_Z_07.wav",
	"xa13_Andy6.wav",
	"xa13_Andrea3.wav",
	"xa13_Joby7.wav",
	"xa14_uwater1.wav",
	"xa14_joby1.wav",
	"xa14_andy10.wav",
	"xa14_richcut2.wav",
	"xa14_andrea1.wav",
	"xa14_andy8.wav",
	"xa14_joby6.wav",
	"xa14_ecredits.wav",
	"xa15_boss_01.wav",
	"xa15_joby2.wav",
	"xa15_joby4.wav",
	"xa15_joby5.wav",
	"xa15_joby9.wav",
	"xa15_a_andy.wav",
	"xa15_a_rome.wav",
	"xa15_andy2.wav",
	"xa16_Joby8.wav",
	"xa16_A_Sub_Amb.wav",
	"xa16_Joby10.wav",
	"xa16_A_Harbour_out.wav",
	"xa16_A_Andy_Out_Norm.wav",
	"xa16_A_Andy_Out_Spooky.wav",
	"xa16_A_Rome_Day.wav",
	"xa16_A_Underwater.wav",
	"xa17_A_Rome_Night.wav",
	"xa17_A_VC_Saga.wav",
	"xa17_A_Industry.wav",
	"xa17_Andrea2.wav",
	"xa17_Andy1.wav",
	"xa17_Andrea4.wav",
	"xa17_Andy9.wav",
	"xa17_Andy11.wav",
};

#pragma warning(push)
#pragma warning(disable : 4838)
#pragma warning(disable : 4309)
static char source_wav_format[50] =
{
	2, 0, 2, 0, 68, 172, 0, 0, 71, 173, 0, 0, 0, 8, 4, 0, 32, 0, 244, 7, 7, 0, 0, 1, 0, 0, 0,
	2, 0, 255, 0, 0, 0, 0, 192, 0, 64, 0, 240, 0, 0, 0, 204, 1, 48, 255, 136, 1, 24, 255
};
#pragma warning(pop)

HACMDRIVER hACMDriver;
uchar* ADPCMBuffer;
bool acm_ready;

long XATrack = -1;

// Music via XAudio2 instead of DirectSound (#100, A3 of the porting plan).
//
// The ACM decoding (MS-ADPCM -> PCM) and the file logic - looping via
// fseek to byte 90, EOF tail, return to the atmosphere - stay 1:1.
// Replaced is only the device layer: instead of a DirectSound ring buffer
// with position notifications (whose Lock failed with E_INVALIDARG on
// every track start under Windows 11 - the music was mute), the four
// stream headers now decode into their own PCM chunks, which go into a
// source voice's queue via SubmitSourceBuffer. The refill thread stays but
// is woken by the voice's OnBufferEnd.
#define MUSIC_CHUNKS		4

static FILE* audio_stream_fp;
static HACMSTREAM hACMStream;
static HACMDRIVERID hACMDriverID;
static HANDLE NotificationThreadHandle;
static CRITICAL_SECTION audio_cs;
static ACMSTREAMHEADER StreamHeaders[MUSIC_CHUNKS];
static HANDLE NotifyEventHandles[2];
static IXAudio2SourceVoice* MusicVoice;
static uchar* MusicChunks[MUSIC_CHUNKS];
static long audio_play_mode;
static long CurrentNotify;
static long eof_counter;

// HQ OGG stream (see the header comment). While it runs, the ACM path
// rests: a track plays either as OGG or as WAV, never both. Its own PCM
// chunks in ADPCM block size (~0.5 s), so the EOF tail (EOF_LEEWAY) ticks
// in the same rhythm as on the WAV path.
#define OGG_CHUNK_FRAMES	22050
static stb_vorbis* OggStream;
static short* OggChunks[MUSIC_CHUNKS];

#define ACM_BUFFER_SIZE		0x5800
#define EOF_LEEWAY			2								//WAS 8.. every 2 = about 1 extra second of playtime

// Only wakes the refill thread: nothing may block on the XAudio2 mixer
// thread, so the actual work does not happen here.
struct MusicVoiceCallback : public IXAudio2VoiceCallback
{
	void __stdcall OnVoiceProcessingPassStart(UINT32) {}
	void __stdcall OnVoiceProcessingPassEnd() {}
	void __stdcall OnStreamEnd() {}
	void __stdcall OnBufferStart(void*) {}
	void __stdcall OnBufferEnd(void*) { SetEvent(NotifyEventHandles[0]); }
	void __stdcall OnLoopEnd(void*) {}
	void __stdcall OnVoiceError(void*, HRESULT) {}
};

static MusicVoiceCallback MusicCallback;

void S_CDPlay(long track, long mode)
{
	if (acm_ready)
	{
		eof_counter = 0;
		IsAtmospherePlaying = track == CurrentAtmosphere;
		S_CDStop();
		ACMEmulateCDPlay(track, mode);
	}
}

void S_CDStop()
{
	if (!acm_ready)
		return;

	EnterCriticalSection(&audio_cs);

	if (audio_stream_fp || OggStream)
	{
		if (MusicVoice)
		{
			MusicVoice->Stop(0);
			MusicVoice->FlushSourceBuffers();
		}

		if (audio_stream_fp)
		{
			fclose(audio_stream_fp);
			audio_stream_fp = 0;
		}

		if (OggStream)
		{
			stb_vorbis_close(OggStream);
			OggStream = 0;
		}

		eof_counter = 0;
		XATrack = -1;
	}

	LeaveCriticalSection(&audio_cs);
}

void S_CDFade(long n)
{

}

void S_StartSyncedAudio(long track)
{
	S_CDStop();
	S_CDPlay(track, 2);
}

void ACMSetVolume()
{
	long volume;

	if (!MusicVolume)
		volume = -10000;
	else
		volume = -4000 * (100 - MusicVolume) / 100;

	// Same curve as in the DirectSound days (hundredths of a decibel),
	// translated into the linear XAudio2 factor only at the API boundary.
	if (MusicVoice)
		MusicVoice->SetVolume(DSVolumeToLinear(volume));
}

// Caller holds audio_cs.
static void OpenStreamFile(char* name)
{
	audio_stream_fp = FileOpen(name);

	if (!audio_stream_fp)
	{
		Log("%s - Not Found", name);
		return;
	}

	fseek(audio_stream_fp, 90, SEEK_SET);
}

// The OGG counterpart to MusicFeedChunk: decodes the next block from the
// stb_vorbis stream and appends it to the same voice queue. Caller holds
// audio_cs. At the track's end, SILENCE blocks are pushed in so
// OnBufferEnd keeps ticking - otherwise the EOF tail (return to the level
// atmosphere) would never get its turn.
static void OggFeedChunk()
{
	XAUDIO2_BUFFER buf;
	long frames;

	if (!MusicVoice || !OggStream || XATrack == -1)
		return;

	short* ziel = OggChunks[CurrentNotify];
	frames = stb_vorbis_get_samples_short_interleaved(OggStream, 2, ziel, OGG_CHUNK_FRAMES * 2);

	if (frames <= 0 && audio_play_mode == 1)
	{
		stb_vorbis_seek_start(OggStream);
		frames = stb_vorbis_get_samples_short_interleaved(OggStream, 2, ziel, OGG_CHUNK_FRAMES * 2);
	}

	if (frames <= 0)
	{
		eof_counter++;

		if (eof_counter > EOF_LEEWAY)
		{
			eof_counter = 0;

			if (audio_play_mode == 2)
			{
				S_CDStop();
				return;
			}

			if (CurrentAtmosphere && !IsAtmospherePlaying)
			{
				S_CDStop();
				S_CDPlay(CurrentAtmosphere, 1);
				return;
			}
		}

		memset(ziel, 0, OGG_CHUNK_FRAMES * 2 * sizeof(short));
		frames = OGG_CHUNK_FRAMES;
	}

	memset(&buf, 0, sizeof(buf));
	buf.pAudioData = (BYTE*)ziel;
	buf.AudioBytes = static_cast<UINT32>(frames) * 2 * sizeof(short);

	if (FAILED(MusicVoice->SubmitSourceBuffer(&buf)))
		return;

	CurrentNotify = (CurrentNotify + 1) & (MUSIC_CHUNKS - 1);
}

// Reads the next ADPCM block, decodes it into the next free PCM block and
// appends it to the voice queue. Caller holds audio_cs. At the track's end,
// the old EOF tail including the return to the level atmosphere runs here
// as well.
static void MusicFeedChunk(bool first)
{
	XAUDIO2_BUFFER buf;
	ACMSTREAMHEADER* header;

	if (OggStream)
	{
		OggFeedChunk();
		return;
	}

	if (!MusicVoice || !audio_stream_fp || XATrack == -1)
		return;

	memset(ADPCMBuffer, 0, ACM_BUFFER_SIZE);
	fread(ADPCMBuffer, 1, ACM_BUFFER_SIZE, audio_stream_fp);

	if (feof(audio_stream_fp))
	{
		if (audio_play_mode == 1)
			fseek(audio_stream_fp, 90, SEEK_SET);
		else
		{
			eof_counter++;

			if (eof_counter > EOF_LEEWAY)
			{
				eof_counter = 0;

				if (audio_play_mode == 2)
				{
					S_CDStop();
					return;
				}

				if (CurrentAtmosphere && !IsAtmospherePlaying)
				{
					S_CDStop();
					S_CDPlay(CurrentAtmosphere, 1);
					return;
				}
			}
		}
	}

	header = &StreamHeaders[CurrentNotify];

	if (acmStreamConvert(hACMStream, header,
		first ? ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_START : ACM_STREAMCONVERTF_BLOCKALIGN))
		return;

	if (!header->cbDstLengthUsed)
		return;

	memset(&buf, 0, sizeof(buf));
	buf.pAudioData = header->pbDst;
	buf.AudioBytes = header->cbDstLengthUsed;

	if (FAILED(MusicVoice->SubmitSourceBuffer(&buf)))
		return;

	CurrentNotify = (CurrentNotify + 1) & (MUSIC_CHUNKS - 1);
}

// Refills the queue back up to two blocks. A loop instead of a single
// block, because the auto-reset event merges two closely spaced
// OnBufferEnd calls into one wakeup - whoever then adds only one block
// slowly drains the queue.
static void MusicFeed()
{
	XAUDIO2_VOICE_STATE state;

	while (MusicVoice && (audio_stream_fp || OggStream) && XATrack != -1)
	{
		MusicVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

		if (state.BuffersQueued >= 2)
			break;

		MusicFeedChunk(false);
	}
}

static ulong __stdcall MusicStreamThread(void* arg)
{
	while (WaitForMultipleObjects(2, NotifyEventHandles, 0, INFINITE) == WAIT_OBJECT_0)
	{
		EnterCriticalSection(&audio_cs);
		MusicFeed();
		LeaveCriticalSection(&audio_cs);
	}

	return 0;
}

void ACMEmulateCDPlay(long track, long mode)
{
	XAUDIO2_VOICE_STATE state;
	char name[256];

	EnterCriticalSection(&audio_cs);

	// Look for an HQ version first: audio\HQ <language>\<nr>.OGG, chosen
	// via the language choice in the options menu; if the language's set
	// is missing, audio\HQ EN\<nr>.OGG steps in, and if that is missing
	// too, the original WAV runs. Italian has no set of its own (yet); the
	// JP set only becomes reachable once a real Japanese language file is
	// selectable (#116).
	// S_CDPlay called S_CDStop beforehand, OggStream is always 0 here.
	if (track >= 0 && track < 136)
	{
		const char* hq;
		int err = 0;

		switch (Gameflow->Language)
		{
		case GERMAN:  hq = "DE"; break;
		case FRENCH:  hq = "FR"; break;
		case ITALIAN: hq = "IT"; break;
		case SPANISH: hq = "ES"; break;
		case JAPAN:   hq = "JP"; break;
		default:      hq = 0;    break;	// Englisch, US, Niederlaendisch
		}

		if (hq)
		{
			wsprintf(name, "audio\\HQ %s\\%d.OGG", hq, track);
			OggStream = stb_vorbis_open_filename(name, &err, 0);
		}

		if (!OggStream)
		{
			wsprintf(name, "audio\\HQ EN\\%d.OGG", track);
			OggStream = stb_vorbis_open_filename(name, &err, 0);
		}
	}

	if (OggStream)
	{
		Log("Playing %s %s %d", name, mode ? "Looped" : "", track);
		XATrack = track;
		audio_play_mode = mode;

		if (MusicVoice)
		{
			for (int i = 0; i < 100; i++)
			{
				MusicVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

				if (!state.BuffersQueued)
					break;

				Sleep(1);
			}
		}

		CurrentNotify = 0;
		OggFeedChunk();
		OggFeedChunk();
		ACMSetVolume();

		if (MusicVoice)
			MusicVoice->Start(0);

		LeaveCriticalSection(&audio_cs);
		return;
	}

	wsprintf(name, "audio\\%s", TrackFileNames[track]);

	if (mode)
		Log("Playing %s %s %d", name, "Looped", track);
	else
		Log("Playing %s %s %d", name, "", track);

	XATrack = track;
	audio_play_mode = mode;
	OpenStreamFile(name);

	if (!audio_stream_fp)
	{
		LeaveCriticalSection(&audio_cs);
		return;
	}

	// After Stop+FlushSourceBuffers the mixer loop can hold the old blocks
	// for a blink longer - only write on once they are really out, or the
	// track change crackles.
	if (MusicVoice)
	{
		for (int i = 0; i < 100; i++)
		{
			MusicVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

			if (!state.BuffersQueued)
				break;

			Sleep(1);
		}
	}

	// Two blocks of lead like before (CurrentNotify used to start at 2);
	// the START conversion pass resets the ADPCM predictor state.
	CurrentNotify = 0;
	MusicFeedChunk(true);
	MusicFeedChunk(false);
	ACMSetVolume();

	if (MusicVoice)
		MusicVoice->Start(0);

	LeaveCriticalSection(&audio_cs);
}

static BOOL __stdcall ACMEnumCallBack(HACMDRIVERID hadid, DWORD_PTR dwInstance, DWORD fdwSupport)
{
	ACMDRIVERDETAILS driver;

	memset(&driver, 0, sizeof(driver));
	driver.cbStruct = sizeof(ACMDRIVERDETAILS);
	acmDriverDetails(hadid, &driver, 0);

	if (strcmp(driver.szShortName, "MS-ADPCM"))
		return 1;

	hACMDriverID = hadid;
	return 0;
}

bool ACMInit()
{
	static WAVEFORMATEX wav_format;
	static ulong StreamSize;
	ulong version, pMetric, ThreadId;

	version = acmGetVersion();
	InitializeCriticalSection(&audio_cs);
	acm_ready = 0;
	Log("ACM Version %u.%.02u", ((version >> 16) & 0xFFFF) >> 8, (version >> 16) & 0xFF);
	acmDriverEnum(ACMEnumCallBack, 0, 0);

	if (!hACMDriverID)
	{
		Log("*** Unable To Locate MS-ADPCM Driver ***");
		return 0;
	}

	if (acmDriverOpen(&hACMDriver, hACMDriverID, 0))
	{
		Log("*** Failed To Open Driver MS-ADPCM Driver ***");
		return 0;
	}

	ADPCMBuffer = (uchar*)malloc(ACM_BUFFER_SIZE);
	wav_format.wFormatTag = WAVE_FORMAT_PCM;
	acmMetrics(0, ACM_METRIC_MAX_SIZE_FORMAT, &pMetric);
	acmFormatSuggest(hACMDriver, (LPWAVEFORMATEX)&source_wav_format, &wav_format, pMetric, ACM_FORMATSUGGESTF_WFORMATTAG);

	acmStreamOpen(&hACMStream, hACMDriver, (LPWAVEFORMATEX)&source_wav_format, &wav_format, 0, 0, 0, 0);
	acmStreamSize(hACMStream, ACM_BUFFER_SIZE, &StreamSize, 0);

	// Four own PCM blocks at the upper bound reported by acmStreamSize -
	// the headers used to point into the locked DirectSound ring, spaced
	// by NotifySize.
	for (int i = 0; i < MUSIC_CHUNKS; i++)
	{
		MusicChunks[i] = (uchar*)malloc(StreamSize);
		memset(&StreamHeaders[i], 0, sizeof(ACMSTREAMHEADER));
		StreamHeaders[i].cbStruct = sizeof(ACMSTREAMHEADER);
		StreamHeaders[i].pbSrc = ADPCMBuffer;
		StreamHeaders[i].cbSrcLength = ACM_BUFFER_SIZE;
		StreamHeaders[i].pbDst = MusicChunks[i];
		StreamHeaders[i].cbDstLength = StreamSize;
		acmStreamPrepareHeader(hACMStream, &StreamHeaders[i], 0);
	}

	// Own PCM blocks for the HQ OGG path - stereo 16 bit, the same format
	// as wav_format, which is why both paths share the MusicVoice.
	for (int i = 0; i < MUSIC_CHUNKS; i++)
		OggChunks[i] = (short*)malloc(OGG_CHUNK_FRAMES * 2 * sizeof(short));

	if (!g_XAudio2 || FAILED(g_XAudio2->CreateSourceVoice(&MusicVoice, &wav_format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &MusicCallback, 0, 0)))
	{
		Log("*** Unable To Create Music Voice ***");
		return 0;
	}

	NotifyEventHandles[0] = CreateEvent(0, 0, 0, 0);
	NotifyEventHandles[1] = CreateEvent(0, 0, 0, 0);
	NotificationThreadHandle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MusicStreamThread, 0, 0, &ThreadId);

	if (!NotificationThreadHandle)
	{
		Log("Create Notification Thread failed");
		return 0;
	}

	acm_ready = 1;
	return 1;
}

void ACMClose()
{
	if (!acm_ready)
		return;

	S_CDStop();
	acm_ready = 0;

	// Catch up with the refill thread first, then close the events -
	// closing handles that are still being waited on is a gamble.
	SetEvent(NotifyEventHandles[1]);
	WaitForSingleObject(NotificationThreadHandle, 5000);
	CloseHandle(NotificationThreadHandle);
	NotificationThreadHandle = 0;
	CloseHandle(NotifyEventHandles[0]);
	CloseHandle(NotifyEventHandles[1]);
	NotifyEventHandles[0] = 0;
	NotifyEventHandles[1] = 0;

	if (MusicVoice)
	{
		MusicVoice->DestroyVoice();
		MusicVoice = 0;
	}

	for (int i = 0; i < MUSIC_CHUNKS; i++)
	{
		acmStreamUnprepareHeader(hACMStream, &StreamHeaders[i], 0);
		free(MusicChunks[i]);
		MusicChunks[i] = 0;
	}

	acmStreamClose(hACMStream, 0);
	acmDriverClose(hACMDriver, 0);
	DeleteCriticalSection(&audio_cs);
}

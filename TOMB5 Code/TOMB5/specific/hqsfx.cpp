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
#include "hqsfx.h"
#include "function_stubs.h"

// Optional high quality sounds.
//
// Tomb Raider Chronicles has no MAIN.SFX - that was Tomb Raider 4. From
// Chronicles on, Core moved the samples into the level files, so every .TRC
// carries its own bank of MS-ADPCM at 22050 Hz. The remaster went the other
// way and consolidated everything back into one global MAIN.SFX of plain PCM
// at 44100 Hz.
//
// Measured against the original samples, that file is not the old material
// resampled: run a 22050 sample up to 44100 and back down and almost nothing
// is left over, while the remaster's samples keep about twenty times as much
// residue, and their spectrum runs through 11025 Hz without the cliff a
// resampled signal always shows. Same recordings, same levels, but with the
// bandwidth the 2000 release threw away. Three of the 463 are resampled after
// all; they simply come out sounding like today.
//
// If the player drops that MAIN.SFX into audio\, this reads the samples from
// it instead of the ones inside the level. If it is not there, nothing here
// happens and the game plays Core's samples as it always has. The file is the
// player's own data and is NEVER shipped - exactly like the HQ music in
// audio.cpp.
//
// Layout of the file, all little endian (the header is the remaster's own
// invention - Tomb Raider 2 and 3, the only ones with a real MAIN.SFX, start
// at offset 0 with the first RIFF):
//
//   0x0000  1000 B  sample_lut[500], int16, -1 for "no sound"
//   0x03E8     4 B  number of SAMPLE_INFOs
//   0x03EC     8 B each  SAMPLE_INFO[], byte for byte our own struct
//           4000 B  500 x int64, a bit mask of the levels each sound is used
//                   in - not needed here, but it is what makes the file
//                   self-describing
//           then    the RIFFs, back to back, ending on the last byte
//
// The bridge between the two numbering schemes is the sound id. A level knows
// its samples as 0..135 or so, the bank knows them as 0..462, and neither
// order has anything to do with the other. But both sides index their LUT by
// the same sound id, so id -> SAMPLE_INFO -> sample number can be walked on
// both sides and the results paired. Core's SAMPLE_INFO values are identical
// in every level (checked across three of them: 72 shared ids, no
// difference), so the bridge does not shift from level to level.
//
// What is deliberately NOT done: the level's own tables stay in place and
// only the audio data is swapped. That keeps the sounds the randomizer
// imports from Tomb Raider 3 working - they have no counterpart here and
// simply keep their original samples - and it means MAX_SAMPLES and the
// on-disk LUT length are not touched at all.

#define HQ_LUT_ENTRIES		500
#define HQ_LEVEL_MASK_BYTES	4000
#define HQ_MAX_SAMPLES		4096	// sanity bound, the real file has 463

static FILE* hq_fp;
static bool hq_tried;
static bool hq_ready;

static short hq_lut[HQ_LUT_ENTRIES];
static SAMPLE_INFO* hq_infos;
static long hq_info_count;

static long* hq_offset;		// file position of each RIFF
static long* hq_size;		// its length including the 8 byte RIFF header
static long hq_count;

static char* hq_buffer;		// big enough for the largest RIFF in the file
static long hq_buffer_size;

static long* hq_map;		// level sample number -> bank sample number
static long hq_substituted;

static void HQSfxFail(const char* why)
{
	Log("HQ SFX: audio\\MAIN.SFX unusable - %s", why);
	HQSfxClose();
}

bool HQSfxOpen()
{
	long headerEnd, at, riffSize, fileSize;
	char tag[4];

	if (hq_tried)
		return hq_ready;

	hq_tried = true;

	hq_fp = fopen("audio\\MAIN.SFX", "rb");

	if (!hq_fp)
	{
		Log("HQ SFX: no audio\\MAIN.SFX, using the level's own samples");
		return false;
	}

	fseek(hq_fp, 0, SEEK_END);
	fileSize = ftell(hq_fp);
	fseek(hq_fp, 0, SEEK_SET);

	if (fileSize < (long)sizeof(hq_lut) + 4 + HQ_LEVEL_MASK_BYTES)
	{
		HQSfxFail("too small to hold a header");
		return false;
	}

	if (fread(hq_lut, sizeof(hq_lut), 1, hq_fp) != 1 ||
		fread(&hq_info_count, 4, 1, hq_fp) != 1)
	{
		HQSfxFail("header could not be read");
		return false;
	}

	if (hq_info_count <= 0 || hq_info_count > HQ_MAX_SAMPLES)
	{
		HQSfxFail("implausible number of sound infos");
		return false;
	}

	hq_infos = (SAMPLE_INFO*)malloc(hq_info_count * sizeof(SAMPLE_INFO));

	if (!hq_infos || fread(hq_infos, sizeof(SAMPLE_INFO), hq_info_count, hq_fp) != (size_t)hq_info_count)
	{
		HQSfxFail("sound infos could not be read");
		return false;
	}

	// Where the RIFFs have to start if the file really is built the way the
	// comment above says. Checking that the first one lands exactly there is
	// what turns a guess about the layout into a test of it - a header of a
	// different shape puts the first RIFF somewhere else and is rejected here
	// rather than producing garbled sound later.
	headerEnd = (long)sizeof(hq_lut) + 4 + hq_info_count * (long)sizeof(SAMPLE_INFO) + HQ_LEVEL_MASK_BYTES;

	if (headerEnd + 8 > fileSize)
	{
		HQSfxFail("header longer than the file");
		return false;
	}

	fseek(hq_fp, headerEnd, SEEK_SET);

	if (fread(tag, 4, 1, hq_fp) != 1 || memcmp(tag, "RIFF", 4))
	{
		HQSfxFail("no RIFF where the header ends - not the expected layout");
		return false;
	}

	hq_offset = (long*)malloc(HQ_MAX_SAMPLES * sizeof(long));
	hq_size = (long*)malloc(HQ_MAX_SAMPLES * sizeof(long));

	if (!hq_offset || !hq_size)
	{
		HQSfxFail("out of memory");
		return false;
	}

	// Walk the chain. It has no count of its own; the file simply ends after
	// the last one, and that it comes out exactly on the last byte is the
	// second half of the layout test.
	at = headerEnd;

	while (at + 8 <= fileSize && hq_count < HQ_MAX_SAMPLES)
	{
		fseek(hq_fp, at, SEEK_SET);

		if (fread(tag, 4, 1, hq_fp) != 1 || memcmp(tag, "RIFF", 4) ||
			fread(&riffSize, 4, 1, hq_fp) != 1)
			break;

		if (riffSize <= 0 || at + 8 + riffSize > fileSize)
			break;

		hq_offset[hq_count] = at;
		hq_size[hq_count] = riffSize + 8;

		if (riffSize + 8 > hq_buffer_size)
			hq_buffer_size = riffSize + 8;

		hq_count++;
		at += 8 + riffSize;

		if (at & 1)
			at++;
	}

	if (!hq_count || at != fileSize)
	{
		HQSfxFail("the chain of sounds does not add up to the file");
		return false;
	}

	hq_buffer = (char*)malloc(hq_buffer_size);

	if (!hq_buffer)
	{
		HQSfxFail("out of memory");
		return false;
	}

	hq_ready = true;
	Log("HQ SFX: audio\\MAIN.SFX accepted - %d sounds, %d infos, largest %d bytes",
		hq_count, hq_info_count, hq_buffer_size);
	return true;
}

void HQSfxClose()
{
	if (hq_fp)
	{
		fclose(hq_fp);
		hq_fp = 0;
	}

	free(hq_infos);
	free(hq_offset);
	free(hq_size);
	free(hq_buffer);
	hq_infos = 0;
	hq_offset = 0;
	hq_size = 0;
	hq_buffer = 0;
	hq_info_count = 0;
	hq_count = 0;
	hq_buffer_size = 0;
	hq_ready = false;
	HQSfxDropMap();
}

void HQSfxDropMap()
{
	free(hq_map);
	hq_map = 0;
	hq_substituted = 0;
}

void HQSfxBuildMap(const short* levelLut, long levelLutEntries,
	const SAMPLE_INFO* levelInfos, long levelInfoCount)
{
	long sfx, li, hi, v, n, lvar, hvar, paired;

	HQSfxDropMap();

	if (!hq_ready || !levelLut || !levelInfos || levelInfoCount <= 0)
		return;

	hq_map = (long*)malloc(MAX_SAMPLES * sizeof(long));

	if (!hq_map)
		return;

	for (v = 0; v < MAX_SAMPLES; v++)
		hq_map[v] = -1;

	if (levelLutEntries > HQ_LUT_ENTRIES)
		levelLutEntries = HQ_LUT_ENTRIES;

	paired = 0;

	for (sfx = 0; sfx < levelLutEntries; sfx++)
	{
		li = levelLut[sfx];
		hi = hq_lut[sfx];

		if (li < 0 || li >= levelInfoCount)
			continue;

		if (hi < 0 || hi >= hq_info_count)
			continue;

		// The variation count sits in bits 2..5 of flags, the same place
		// sound.cpp reads it from when it rolls which of a sound's takes to
		// play. Pair only as many as both sides actually have.
		lvar = (levelInfos[li].flags >> 2) & 0xF;
		hvar = (hq_infos[hi].flags >> 2) & 0xF;

		if (!lvar)
			lvar = 1;

		if (!hvar)
			hvar = 1;

		n = lvar < hvar ? lvar : hvar;

		for (v = 0; v < n; v++)
		{
			const long ls = levelInfos[li].number + v;
			const long hs = hq_infos[hi].number + v;

			if (ls < 0 || ls >= MAX_SAMPLES || hs < 0 || hs >= hq_count)
				continue;

			hq_map[ls] = hs;
			paired++;
		}
	}

	Log("HQ SFX: %d of this level's samples have a counterpart in the bank", paired);
}

char* HQSfxPayload(long levelSample, long pcmBytes22k, long* riffBytes)
{
	long g, at, chunk, dataLen;
	const WAVEFORMATEX* wf;

	if (riffBytes)
		*riffBytes = 0;

	if (!hq_ready || !hq_map || levelSample < 0 || levelSample >= MAX_SAMPLES)
		return 0;

	g = hq_map[levelSample];

	if (g < 0 || g >= hq_count)
		return 0;

	fseek(hq_fp, hq_offset[g], SEEK_SET);

	if (fread(hq_buffer, hq_size[g], 1, hq_fp) != 1)
	{
		Log("HQ SFX: sample %d could not be read, keeping the level's own", g);
		return 0;
	}

	if (hq_size[g] < 44 || memcmp(hq_buffer + 8, "WAVE", 4))
		return 0;

	wf = (const WAVEFORMATEX*)(hq_buffer + 20);

	if (wf->wFormatTag != WAVE_FORMAT_PCM || wf->nChannels != 1 || wf->wBitsPerSample != 16)
	{
		Log("HQ SFX: sample %d is not mono 16 bit PCM, keeping the level's own", g);
		return 0;
	}

	// Find the data block rather than assuming it follows fmt - fact and LIST
	// blocks can stand in between.
	at = 12;
	dataLen = 0;

	while (at + 8 <= hq_size[g])
	{
		chunk = *(const long*)(hq_buffer + at + 4);

		if (!memcmp(hq_buffer + at, "data", 4))
		{
			dataLen = chunk;
			break;
		}

		at += 8 + chunk + (chunk & 1);
	}

	if (dataLen <= 0)
		return 0;

	// Plausibility. The pairing runs over the sound map, and the randomizer
	// is free to move sounds around inside a level, so a mismatched pair is
	// not impossible. Both sides hold the same recording, so at twice the
	// rate the bank's sample must be about twice as long; anything further
	// off than a fifth is not the same sound and keeps Core's version.
	if (pcmBytes22k > 0)
	{
		const long expected = pcmBytes22k * 2;

		if (dataLen < expected - expected / 5 || dataLen > expected + expected / 5)
		{
			Log("HQ SFX: sample %d is %d bytes where %d were expected - keeping the level's own",
				g, dataLen, expected);
			return 0;
		}
	}

	hq_substituted++;

	if (riffBytes)
		*riffBytes = hq_size[g];

	return hq_buffer;
}

long HQSfxSubstituted()
{
	return hq_substituted;
}

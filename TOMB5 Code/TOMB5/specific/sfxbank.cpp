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
#include "sfxbank.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The header says what this is and why. What follows is only how.
//
// Engine-free on purpose: no pch, no types.h, no Log. The SAMPLE_INFO of the
// engine is read here as the eight bytes it is, so that this file can be
// linked by a diagnostic tool that has no engine behind it - the same reason
// hd_install.cpp is written the way it is.

namespace
{
	// The engine's SAMPLE_INFO, byte for byte (global\types.h):
	//   0  short number   the first sample of this sound
	//   2  uchar volume
	//   3  char  radius
	//   4  char  randomness
	//   5  char  pitch
	//   6  short flags    bits 2..5 are the variation count
	const int kInfoBytes = 8;
	const int kLevelMaskBytes = 4000;

	short read_short(const unsigned char* at)
	{
		return (short)((unsigned short)at[0] | ((unsigned short)at[1] << 8));
	}

	long read_long(const unsigned char* at)
	{
		return (long)((unsigned long)at[0] | ((unsigned long)at[1] << 8) |
			((unsigned long)at[2] << 16) | ((unsigned long)at[3] << 24));
	}
}

struct SfxBank
{
	std::FILE* file;
	short lut[SFX_LUT_ENTRIES];
	unsigned char* infos;		// kInfoBytes each
	int infoCount;
	long* offset;			// file position of each RIFF
	long* size;			// its length including the 8 byte header
	int count;
	char* buffer;			// the largest RIFF in the file fits
	long bufferBytes;
};

SfxBank* SfxBankOpen(const char* path, const char** why)
{
	static const char* trouble = "";
	unsigned char header[1004];
	char tag[4];
	long fileSize, headerEnd, at;

	if (!why)
		why = &trouble;

	*why = "";

	if (!path || !*path)
	{
		*why = "no path";
		return 0;
	}

	std::FILE* file = std::fopen(path, "rb");

	if (!file)
	{
		*why = "not there";
		return 0;
	}

	SfxBank* bank = (SfxBank*)std::calloc(1, sizeof(SfxBank));

	if (!bank)
	{
		std::fclose(file);
		*why = "out of memory";
		return 0;
	}

	bank->file = file;

	std::fseek(file, 0, SEEK_END);
	fileSize = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);

	if (fileSize < (long)sizeof(header) + kLevelMaskBytes)
	{
		*why = "too small to hold a header";
		SfxBankClose(bank);
		return 0;
	}

	if (std::fread(header, sizeof(header), 1, file) != 1)
	{
		*why = "header could not be read";
		SfxBankClose(bank);
		return 0;
	}

	for (int i = 0; i < SFX_LUT_ENTRIES; i++)
		bank->lut[i] = read_short(header + i * 2);

	bank->infoCount = (int)read_long(header + 1000);

	if (bank->infoCount <= 0 || bank->infoCount > SFX_MAX_SAMPLES)
	{
		*why = "implausible number of sound infos";
		SfxBankClose(bank);
		return 0;
	}

	bank->infos = (unsigned char*)std::malloc((size_t)bank->infoCount * kInfoBytes);

	if (!bank->infos ||
		std::fread(bank->infos, kInfoBytes, (size_t)bank->infoCount, file) !=
			(size_t)bank->infoCount)
	{
		*why = "sound infos could not be read";
		SfxBankClose(bank);
		return 0;
	}

	// Where the RIFFs have to start if the file really is built the way the
	// header says. Checking that the first one lands exactly there is what
	// turns a guess about the layout into a test of it - a header of a
	// different shape puts the first RIFF somewhere else and is rejected here
	// rather than producing garbled sound later.
	headerEnd = 1004 + (long)bank->infoCount * kInfoBytes + kLevelMaskBytes;

	if (headerEnd + 8 > fileSize)
	{
		*why = "header longer than the file";
		SfxBankClose(bank);
		return 0;
	}

	std::fseek(file, headerEnd, SEEK_SET);

	if (std::fread(tag, 4, 1, file) != 1 || std::memcmp(tag, "RIFF", 4))
	{
		*why = "no RIFF where the header ends - not the expected layout";
		SfxBankClose(bank);
		return 0;
	}

	bank->offset = (long*)std::malloc(SFX_MAX_SAMPLES * sizeof(long));
	bank->size = (long*)std::malloc(SFX_MAX_SAMPLES * sizeof(long));

	if (!bank->offset || !bank->size)
	{
		*why = "out of memory";
		SfxBankClose(bank);
		return 0;
	}

	// Walk the chain. It has no count of its own; the file simply ends after
	// the last one, and that it comes out exactly on the last byte is the
	// second half of the layout test.
	at = headerEnd;

	while (at + 8 <= fileSize && bank->count < SFX_MAX_SAMPLES)
	{
		unsigned char head[8];

		std::fseek(file, at, SEEK_SET);

		if (std::fread(head, 8, 1, file) != 1 || std::memcmp(head, "RIFF", 4))
			break;

		const long riffSize = read_long(head + 4);

		if (riffSize <= 0 || at + 8 + riffSize > fileSize)
			break;

		bank->offset[bank->count] = at;
		bank->size[bank->count] = riffSize + 8;

		if (riffSize + 8 > bank->bufferBytes)
			bank->bufferBytes = riffSize + 8;

		bank->count++;
		at += 8 + riffSize;

		if (at & 1)
			at++;
	}

	if (!bank->count || at != fileSize)
	{
		*why = "the chain of sounds does not add up to the file";
		SfxBankClose(bank);
		return 0;
	}

	bank->buffer = (char*)std::malloc((size_t)bank->bufferBytes);

	if (!bank->buffer)
	{
		*why = "out of memory";
		SfxBankClose(bank);
		return 0;
	}

	return bank;
}

void SfxBankClose(SfxBank* bank)
{
	if (!bank)
		return;

	if (bank->file)
		std::fclose(bank->file);

	std::free(bank->infos);
	std::free(bank->offset);
	std::free(bank->size);
	std::free(bank->buffer);
	std::free(bank);
}

int SfxBankSamples(const SfxBank* bank)
{
	return bank ? bank->count : 0;
}

int SfxBankInfos(const SfxBank* bank)
{
	return bank ? bank->infoCount : 0;
}

int SfxBankIdsUsed(const SfxBank* bank)
{
	int used = 0;

	if (!bank)
		return 0;

	for (int i = 0; i < SFX_LUT_ENTRIES; i++)
		if (bank->lut[i] >= 0)
			used++;

	return used;
}

long SfxBankLargest(const SfxBank* bank)
{
	return bank ? bank->bufferBytes : 0;
}

int SfxBankLut(const SfxBank* bank, int soundId)
{
	if (!bank || soundId < 0 || soundId >= SFX_LUT_ENTRIES)
		return -1;

	const int info = bank->lut[soundId];

	return info >= 0 && info < bank->infoCount ? info : -1;
}

int SfxBankInfoNumber(const SfxBank* bank, int info)
{
	if (!bank || info < 0 || info >= bank->infoCount)
		return -1;

	return read_short(bank->infos + (size_t)info * kInfoBytes);
}

int SfxBankInfoVariations(const SfxBank* bank, int info)
{
	if (!bank || info < 0 || info >= bank->infoCount)
		return 0;

	const int flags = read_short(bank->infos + (size_t)info * kInfoBytes + 6);
	const int takes = (flags >> 2) & 0xF;

	return takes ? takes : 1;
}

long SfxBankSampleBytes(const SfxBank* bank, int sample)
{
	if (!bank || sample < 0 || sample >= bank->count)
		return 0;

	return bank->size[sample];
}

const char* SfxBankSample(SfxBank* bank, int sample, long* bytes)
{
	if (bytes)
		*bytes = 0;

	if (!bank || sample < 0 || sample >= bank->count)
		return 0;

	std::fseek(bank->file, bank->offset[sample], SEEK_SET);

	if (std::fread(bank->buffer, (size_t)bank->size[sample], 1, bank->file) != 1)
		return 0;

	if (bytes)
		*bytes = bank->size[sample];

	return bank->buffer;
}

bool SfxRiffCheck(const char* riff, long bytes, long* dataBytes, long* byteRate,
	const char** why)
{
	static const char* trouble = "";

	if (!why)
		why = &trouble;

	*why = "";

	if (dataBytes)
		*dataBytes = 0;

	if (byteRate)
		*byteRate = 0;

	if (!riff || bytes < 44)
	{
		*why = "shorter than a RIFF header";
		return false;
	}

	if (std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4))
	{
		*why = "not a RIFF WAVE";
		return false;
	}

	/* The chunks, walked rather than assumed: fact and LIST blocks can stand
	 * between fmt and data, and a wav written by any editor may well have
	 * them. */
	long at = 12;
	const unsigned char* raw = (const unsigned char*)riff;
	bool haveFmt = false;
	long dataLen = 0;
	int tag = 0, channels = 0, bits = 0;
	long rate = 0;

	while (at + 8 <= bytes)
	{
		const long len = read_long(raw + at + 4);

		if (len < 0 || at + 8 + len > bytes)
		{
			*why = "a chunk runs past the end of the file";
			return false;
		}

		if (!std::memcmp(riff + at, "fmt ", 4) && len >= 16)
		{
			tag = read_short(raw + at + 8);
			channels = read_short(raw + at + 10);
			rate = read_long(raw + at + 12);
			bits = read_short(raw + at + 22);
			haveFmt = true;
		}
		else if (!std::memcmp(riff + at, "data", 4))
			dataLen = len;

		at += 8 + len + (len & 1);
	}

	if (!haveFmt)
	{
		*why = "no fmt chunk";
		return false;
	}

	if (dataLen <= 0)
	{
		*why = "no data chunk";
		return false;
	}

	if (tag != 1)
	{
		*why = "not uncompressed PCM";
		return false;
	}

	if (channels != 1 || bits != 16)
	{
		*why = "not mono 16 bit";
		return false;
	}

	if (rate <= 0)
	{
		*why = "no sample rate";
		return false;
	}

	if (dataBytes)
		*dataBytes = dataLen;

	if (byteRate)
		*byteRate = rate * channels * (bits / 8);

	return true;
}

char* SfxWavLoad(const char* path, long cap, long* bytes, const char** why)
{
	static const char* trouble = "";

	if (!why)
		why = &trouble;

	*why = "";

	if (bytes)
		*bytes = 0;

	std::FILE* file = std::fopen(path, "rb");

	if (!file)
	{
		*why = "could not be opened";
		return 0;
	}

	std::fseek(file, 0, SEEK_END);
	const long size = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);

	if (size < 44)
	{
		std::fclose(file);
		*why = "shorter than a RIFF header";
		return 0;
	}

	if (size > cap)
	{
		std::fclose(file);
		*why = "larger than this engine will hand to a voice";
		return 0;
	}

	char* buffer = (char*)std::malloc((size_t)size);

	if (!buffer)
	{
		std::fclose(file);
		*why = "out of memory";
		return 0;
	}

	const bool read = std::fread(buffer, (size_t)size, 1, file) == 1;

	std::fclose(file);

	if (!read)
	{
		std::free(buffer);
		*why = "could not be read";
		return 0;
	}

	if (bytes)
		*bytes = size;

	return buffer;
}

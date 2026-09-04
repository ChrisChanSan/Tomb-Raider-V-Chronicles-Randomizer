/*
 * DdsProbe - decode a BC7 .DDS with the engine's own decoder and write a PNG.
 *
 * Direct3D 9 cannot sample BC7, so the engine unpacks the remaster's HD
 * loading screens on the CPU (TOMB5 Code\TOMB5\specific\bc7.cpp). This tool
 * compiles that exact file and nothing else, so a picture that comes out
 * right here is a picture that comes out right in the game - without starting
 * the game.
 *
 * Usage:
 *   DdsProbe -selftest              table consistency check only
 *   DdsProbe <in.dds> <out.png>     decode, report, write
 *
 * The report lists which BC7 modes and how many distinct partitions the file
 * actually exercised. That matters: a partition table row that is never used
 * is never proven, so the counts say how much of the decoder the picture
 * really tested.
 *
 * Build: bdp.bat
 *
 * Copyright 2026 ChrisChanSan. GPLv3, see the LICENSE file at the root of
 * this source tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc7.h"
#include "zlib.h"

static unsigned char* ReadWhole(const char* path, unsigned long* size)
{
	FILE* f;
	unsigned char* buf;
	long n;

	f = fopen(path, "rb");

	if (!f)
		return 0;

	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = (unsigned char*)malloc(n);

	if (!buf || fread(buf, 1, n, f) != (size_t)n)
	{
		free(buf);
		fclose(f);
		return 0;
	}

	fclose(f);
	*size = (unsigned long)n;
	return buf;
}

static void PutBE(unsigned char* p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void PngChunk(FILE* f, const char* tag, const unsigned char* data, unsigned long len)
{
	unsigned char head[8];
	unsigned char tail[4];
	unsigned long crc;

	PutBE(head, len);
	memcpy(head + 4, tag, 4);
	fwrite(head, 1, 8, f);

	if (len)
		fwrite(data, 1, len, f);

	crc = crc32(0, (const Bytef*)tag, 4);

	if (len)
		crc = crc32(crc, (const Bytef*)data, len);

	PutBE(tail, crc);
	fwrite(tail, 1, 4, f);
}

// 24 bit RGB PNG, filter 0 on every scanline. Small and obviously correct
// beats clever here - this file only has to be readable, not small.
static bool WritePng(const char* path, const unsigned long* px, long w, long h)
{
	FILE* f;
	unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char ihdr[13];
	unsigned char* raw;
	unsigned char* zbuf;
	unsigned long rawLen, zLen;
	long x, y;
	unsigned char* q;

	rawLen = (unsigned long)h * (1 + (unsigned long)w * 3);
	raw = (unsigned char*)malloc(rawLen);

	if (!raw)
		return false;

	q = raw;

	for (y = 0; y < h; y++)
	{
		*q++ = 0;

		for (x = 0; x < w; x++)
		{
			unsigned long c = px[y * w + x];
			*q++ = (unsigned char)(c >> 16);
			*q++ = (unsigned char)(c >> 8);
			*q++ = (unsigned char)c;
		}
	}

	zLen = compressBound(rawLen);
	zbuf = (unsigned char*)malloc(zLen);

	if (!zbuf || compress2(zbuf, &zLen, raw, rawLen, 6) != Z_OK)
	{
		free(raw);
		free(zbuf);
		return false;
	}

	free(raw);
	f = fopen(path, "wb");

	if (!f)
	{
		free(zbuf);
		return false;
	}

	fwrite(sig, 1, 8, f);
	PutBE(ihdr, (unsigned long)w);
	PutBE(ihdr + 4, (unsigned long)h);
	ihdr[8] = 8;		// bit depth
	ihdr[9] = 2;		// truecolour
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;
	PngChunk(f, "IHDR", ihdr, 13);
	PngChunk(f, "IDAT", zbuf, zLen);
	PngChunk(f, "IEND", 0, 0);
	fclose(f);
	free(zbuf);
	return true;
}

// Which modes and partitions the file uses, read straight off the blocks.
static void Census(const unsigned char* p, unsigned long size, long w, long h)
{
	long modeCount[9];
	unsigned char part2Used[64], part3Used[64];
	long blocks, i, m, n2, n3;
	const unsigned char* b;
	static const int pbits[8] = { 4, 6, 6, 6, 0, 0, 0, 6 };
	static const int nsub[8] = { 3, 2, 3, 2, 1, 1, 1, 2 };

	memset(modeCount, 0, sizeof(modeCount));
	memset(part2Used, 0, sizeof(part2Used));
	memset(part3Used, 0, sizeof(part3Used));
	blocks = (w / 4) * (h / 4);
	b = p + 148;

	for (i = 0; i < blocks; i++, b += 16)
	{
		unsigned long word = (unsigned long)b[0] | ((unsigned long)b[1] << 8);

		for (m = 0; m < 8; m++)
			if (word & (1UL << m))
				break;

		modeCount[m]++;

		if (m < 8 && pbits[m])
		{
			int part = (int)((word >> (m + 1)) & ((1UL << pbits[m]) - 1));

			if (nsub[m] == 2)
				part2Used[part] = 1;
			else
				part3Used[part] = 1;
		}
	}

	(void)size;
	printf("  blocks           : %ld\n", blocks);
	printf("  mode use         :");

	for (m = 0; m < 9; m++)
		if (modeCount[m])
			printf(" %ld=%ld", m, modeCount[m]);

	printf("\n");
	n2 = 0;
	n3 = 0;

	for (i = 0; i < 64; i++)
	{
		n2 += part2Used[i];
		n3 += part3Used[i];
	}

	printf("  partitions used  : %ld of 64 two subset, %ld of 64 three subset\n", n2, n3);
}

int main(int argc, char** argv)
{
	unsigned char* file;
	unsigned long size;
	unsigned long* px;
	const char* why;
	long w, h;

	if (!BC7SelfTest(&why))
	{
		printf("SELF TEST FAILED: %s\n", why);
		return 2;
	}

	printf("self test        : tables consistent\n");

	if (argc == 2 && !strcmp(argv[1], "-selftest"))
		return 0;

	if (argc != 3)
	{
		printf("usage: DdsProbe <in.dds> <out.png>\n");
		printf("       DdsProbe -selftest\n");
		return 1;
	}

	file = ReadWhole(argv[1], &size);

	if (!file)
	{
		printf("cannot read %s\n", argv[1]);
		return 1;
	}

	printf("file             : %s (%lu bytes)\n", argv[1], size);
	px = BC7DecodeDDS(file, size, 4096, &w, &h, &why);

	if (!px)
	{
		printf("REFUSED: %s\n", why);
		free(file);
		return 1;
	}

	printf("  size             : %ld x %ld\n", w, h);
	Census(file, size, w, h);

	// Corner pixels, so a flipped or swizzled image shows up in the numbers
	// as well as in the picture.
	printf("  corners ARGB     : TL %08lX  TR %08lX  BL %08lX  BR %08lX\n",
		px[0], px[w - 1], px[(h - 1) * w], px[h * w - 1]);

	if (!WritePng(argv[2], px, w, h))
	{
		printf("cannot write %s\n", argv[2]);
		free(px);
		free(file);
		return 1;
	}

	printf("wrote            : %s\n", argv[2]);
	free(px);
	free(file);
	return 0;
}

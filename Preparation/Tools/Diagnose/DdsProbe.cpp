/*
 * DdsProbe - decode a BC7 .DDS with the engine's own decoder and write a PNG.
 *
 * Direct3D 9 cannot sample BC7, so the engine unpacks the remaster's HD
 * loading screens on the CPU (TOMB5 Code\TOMB5\specific\bc7.cpp). This tool
 * compiles that exact file and nothing else, so a picture that comes out
 * right here is a picture that comes out right in the game - without starting
 * the game.
 *
 * The second job is the title screen. The engine composes it out of two of
 * those files at load time - TITLE.DDS plus the wordmark cut out of the
 * language's LOGO_xx.DDS - and -logo runs that composition here, through the
 * very same routine (hdscreen.cpp, compiled with HDSCREEN_NO_ENGINE), so the
 * picture in the PNG is the picture the game will show.
 *
 * The third is the title MENU, which draws the same wordmark once the loading
 * screen is gone. That one is not composed but cut into a texture page of its
 * own, and -crop builds that page here - same routines, same size - so the
 * page can be looked at before the game ever runs.
 *
 * Usage:
 *   DdsProbe -selftest              table consistency check only
 *   DdsProbe <in.dds> <out.png>     decode, report, write
 *   DdsProbe -logo <title.dds> <logo.dds> <out.png>
 *                                   compose the title screen, report, write
 *   DdsProbe -alpha <in.dds> <out.png>
 *                                   the ALPHA channel as a grey picture, with
 *                                   a histogram - an item page's alpha is a
 *                                   gloss mask, not coverage (#123, fix round
 *                                   D, D2)
 *   DdsProbe -crop <logo.dds> <out.png>
 *                                   the title MENU's logo texture page, report,
 *                                   write (alpha over a checkerboard)
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
#define HDSCREEN_NO_ENGINE
#include "hdscreen.h"
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

// Decode one file and say what came out. Returns the pixels or 0.
static unsigned long* Decode(const char* path, long* w, long* h)
{
	unsigned char* file;
	unsigned long size;
	unsigned long* px;
	const char* why;

	file = ReadWhole(path, &size);

	if (!file)
	{
		printf("cannot read %s\n", path);
		return 0;
	}

	px = BC7DecodeDDS(file, size, 4096, w, h, &why);
	free(file);

	if (!px)
	{
		printf("REFUSED %s: %s\n", path, why);
		return 0;
	}

	printf("file             : %s (%lu bytes), %ld x %ld\n", path, size, *w, *h);
	return px;
}

// The bounding box at four alpha thresholds. The engine draws everything
// above 8 and lines the mark up on everything above 128, so these numbers say
// how much of the box is the soft halo around the lettering and how much is
// the lettering itself.
static void BoxCensus(const unsigned long* px, long w, long h)
{
	static const long thresh[4] = { 8, 64, 128, 224 };
	long t, x, y, x0, y0, x1, y1;

	for (t = 0; t < 4; t++)
	{
		x0 = w; y0 = h; x1 = -1; y1 = -1;

		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++)
			{
				if ((long)((px[y * w + x] >> 24) & 0xFF) <= thresh[t])
					continue;

				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				y1 = y;
			}

		if (x1 < 0)
			printf("  alpha > %-3ld      : nothing\n", thresh[t]);
		else
			printf("  alpha > %-3ld      : x %ld..%ld (%ld wide, centre %ld), y %ld..%ld (%ld high)\n",
				thresh[t], x0, x1, x1 - x0 + 1, (x0 + x1) / 2, y0, y1, y1 - y0 + 1);
	}
}

// Straight or premultiplied? In a premultiplied picture no channel can ever be
// above its own alpha. One that is proves the file is straight, and that the
// compositor has to premultiply before it interpolates.
static void AlphaKind(const unsigned long* px, long w, long h)
{
	long x, y, a, opaque, edge, notPre;

	opaque = 0;
	edge = 0;
	notPre = 0;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			unsigned long c = px[y * w + x];

			a = (long)((c >> 24) & 0xFF);

			if (a == 255)
			{
				opaque++;
				continue;
			}

			if (!a)
				continue;

			edge++;

			if ((long)((c >> 16) & 0xFF) > a || (long)((c >> 8) & 0xFF) > a || (long)(c & 0xFF) > a)
				notPre++;
		}

	printf("  alpha shape      : %ld opaque, %ld partial, of those %ld with a channel above alpha\n",
		opaque, edge, notPre);
	printf("  -> stored as     : %s\n", notPre ? "straight alpha (premultiply before blending)" : "premultiplied or too flat to tell");
}


/* -alpha: THE GLOSS MASK (#123, fix round D, D2).
 *
 * An ITEM page's alpha channel is not coverage. Fix round A2 measured that on
 * the room pages and TROne says the same about the item ones in so many words
 * (host\render\game_renderer.cpp:373: "an opaque-pass tile's alpha channel
 * holds a sparkle/specular MASK, not coverage"). This mode reads that channel
 * out: a histogram in sixteen buckets, the mean, and the PNG written is the
 * alpha as a grey picture, so a person can see whether the bright parts sit
 * where a highlight belongs.
 */
static int AlphaMask(const char* inPath, const char* outPath)
{
	long w = 0, h = 0;
	unsigned long* px = Decode(inPath, &w, &h);

	if (!px)
		return 2;

	const long texels = w * h;
	long bucket[16] = { 0 };
	double sum = 0.0;
	long below128 = 0, exactly255 = 0;

	for (long i = 0; i < texels; i++)
	{
		const long a = (long)((px[i] >> 24) & 0xFF);

		bucket[a >> 4]++;
		sum += (double)a;

		if (a < 128)
			below128++;

		if (a == 255)
			exactly255++;
	}

	printf("file             : %s, %ld x %ld\n", inPath, w, h);
	printf("  alpha mean       : %.1f of 255\n", sum / (double)texels);
	printf("  alpha < 128      : %ld texels (%.1f %%)\n", below128,
		100.0 * below128 / (double)texels);
	printf("  alpha == 255     : %ld texels (%.1f %%)\n", exactly255,
		100.0 * exactly255 / (double)texels);
	printf("  histogram, 16 buckets of 16:\n");

	for (int b = 0; b < 16; b++)
		printf("    %3d..%3d       : %8ld  %5.1f %%\n", b * 16, b * 16 + 15,
			bucket[b], 100.0 * bucket[b] / (double)texels);

	// The alpha as a grey picture.
	for (long i = 0; i < texels; i++)
	{
		const unsigned long a = (px[i] >> 24) & 0xFF;

		px[i] = 0xFF000000u | (a << 16) | (a << 8) | a;
	}

	const int ok = WritePng(outPath, px, w, h);

	free(px);
	return ok ? 0 : 2;
}

// -logo: title picture + wordmark -> PNG, through the engine's own routine.
static int ComposeLogo(const char* titlePath, const char* logoPath, const char* outPath)
{
	unsigned long* title;
	unsigned long* logo;
	unsigned long* blank;
	long tw, th, lw, lh, x0, y0, x1, y1;
	long leftAlign;
	const char* logoBase;

	// The engine decides by the logo's name; mirror that here so the PNG
	// shows what the game will show (US top left, the rest top centre).
	logoBase = strrchr(logoPath, '/');
	if (!logoBase) logoBase = strrchr(logoPath, 92);
	logoBase = logoBase ? logoBase + 1 : logoPath;
	leftAlign = _strnicmp(logoBase, "LOGO_US", 7) == 0;
	title = Decode(titlePath, &tw, &th);

	if (!title)
		return 1;

	logo = Decode(logoPath, &lw, &lh);

	if (!logo)
	{
		free(title);
		return 1;
	}

	printf("logo picture, as it lies in the file:\n");
	BoxCensus(logo, lw, lh);
	AlphaKind(logo, lw, lh);

	if (HDScreenAlphaBox(logo, lw, lh, &x0, &y0, &x1, &y1))
		printf("  engine cuts      : x %ld..%ld, y %ld..%ld (%ld x %ld)\n",
			x0, x1, y0, y1, x1 - x0 + 1, y1 - y0 + 1);
	else
		printf("  engine cuts      : nothing - the logo file is empty\n");

	// Where it lands: compose onto an empty frame of the base picture's size
	// and measure that, so the answer comes from the routine itself and not
	// from a second copy of its arithmetic. The alpha > 128 row is the one to
	// read - that is the lettering, the rest is halo.
	blank = (unsigned long*)calloc((size_t)tw * th, sizeof(unsigned long));

	if (blank)
	{
		if (HDScreenComposeLogo(blank, tw, th, logo, lw, lh, leftAlign))
		{
			printf("logo picture, where it lands on the title:\n");
			BoxCensus(blank, tw, th);
		}

		free(blank);
	}

	if (!HDScreenComposeLogo(title, tw, th, logo, lw, lh, leftAlign))
		printf("compose          : no wordmark found - the title picture is unchanged\n");
	else
		printf("compose          : wordmark blended over the title picture\n");

	free(logo);

	if (!WritePng(outPath, title, tw, th))
	{
		printf("cannot write %s\n", outPath);
		free(title);
		return 1;
	}

	printf("wrote            : %s\n", outPath);
	free(title);
	return 0;
}

// -crop: the texture page the title menu builds out of a logo file, written
// out as a PNG. Same two routines the engine calls (HDScreenLogoSize and
// HDScreenCutLogo), same page size, so what comes out here is texel for texel
// what LoadTitleLogoHD() uploads. Alpha is shown over a grey checkerboard,
// because the PNG this tool writes has no alpha channel of its own - and a
// checkerboard also shows up a dark seam around the lettering, which is the
// one thing the premultiplied resampling is there to prevent.
static int CropLogo(const char* logoPath, const char* outPath)
{
	unsigned long* logo;
	unsigned long* page;
	long lw, lh, w, h, tw, th, x, y, a, i, ch[3];

	logo = Decode(logoPath, &lw, &lh);

	if (!logo)
		return 1;

	printf("logo picture, as it lies in the file:\n");
	BoxCensus(logo, lw, lh);

	if (!HDScreenLogoSize(logo, lw, lh, HD_LOGO_PAGE_MAX_W, HD_LOGO_PAGE_MAX_H, &w, &h))
	{
		printf("the logo file draws nothing - the title menu keeps the pak logo\n");
		free(logo);
		return 1;
	}

	for (tw = 32; tw < w; tw <<= 1);
	for (th = 32; th < h; th <<= 1);

	printf("title menu page  : wordmark %ld x %ld in a %ld x %ld page (u %.6f, v %.6f)\n",
		w, h, tw, th, (double)w / (double)tw, (double)h / (double)th);

	page = (unsigned long*)calloc((size_t)tw * th, sizeof(unsigned long));

	if (!page)
	{
		printf("out of memory\n");
		free(logo);
		return 1;
	}

	if (!HDScreenCutLogo(logo, lw, lh, page, tw, w, h))
	{
		printf("the cut failed\n");
		free(page);
		free(logo);
		return 1;
	}

	free(logo);
	printf("the page, as the device will sample it:\n");
	BoxCensus(page, tw, th);

	// Straight alpha over the checkerboard, exactly what the device does with
	// SRCALPHA / INVSRCALPHA.
	for (y = 0; y < th; y++)
		for (x = 0; x < tw; x++)
		{
			unsigned long c = page[y * tw + x];
			long bg = ((x >> 4) + (y >> 4)) & 1 ? 96 : 160;

			a = (long)((c >> 24) & 0xFF);
			ch[0] = ((long)((c >> 16) & 0xFF) * a + bg * (255 - a)) / 255;
			ch[1] = ((long)((c >> 8) & 0xFF) * a + bg * (255 - a)) / 255;
			ch[2] = ((long)(c & 0xFF) * a + bg * (255 - a)) / 255;

			for (i = 0; i < 3; i++)
				if (ch[i] > 255)
					ch[i] = 255;

			page[y * tw + x] = ((unsigned long)ch[0] << 16) |
				((unsigned long)ch[1] << 8) | (unsigned long)ch[2];
		}

	if (!WritePng(outPath, page, tw, th))
	{
		printf("cannot write %s\n", outPath);
		free(page);
		return 1;
	}

	printf("wrote            : %s\n", outPath);
	free(page);
	return 0;
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

	if (argc == 5 && !strcmp(argv[1], "-logo"))
		return ComposeLogo(argv[2], argv[3], argv[4]);

	if (argc == 4 && !strcmp(argv[1], "-crop"))
		return CropLogo(argv[2], argv[3]);

	if (argc == 4 && !strcmp(argv[1], "-alpha"))
		return AlphaMask(argv[2], argv[3]);

	if (argc != 3)
	{
		printf("usage: DdsProbe <in.dds> <out.png>\n");
		printf("       DdsProbe -logo <title.dds> <logo.dds> <out.png>\n");
		printf("       DdsProbe -crop <logo.dds> <out.png>\n");
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

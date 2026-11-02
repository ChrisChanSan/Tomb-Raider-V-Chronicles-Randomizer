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

/*
 * FontProbe - where every classic glyph lives in the remaster's font atlas,
 * and what it looks like when it is drawn from there.
 *
 * The engine draws every line of text it has - title menu, inventory ring,
 * options, load and save lists, statistics, HUD counters, item names,
 * subtitles - out of ONE 256 x 256 page and ONE table of 106 rectangles
 * (game\text.cpp, DrawChar). The remaster ships the same glyph bitmaps at
 * eight times the size, in an atlas of its own that is typeset completely
 * differently, and it ships a 256-sized copy of that atlas beside it. So the
 * classic rectangle is the template and the small atlas is the haystack: mask
 * the ink, slide, score intersection over union, and a hit says where the same
 * letter sits in the big one.
 *
 * This tool compiles the ENGINE's own matcher (specific\hd\hd_fontmatch.cpp)
 * and the engine's own BC7 decoder (specific\bc7.cpp) and the engine's own
 * glyph table (game\chardef.inc) and accent table (game\accent.inc), so a
 * letter that is found here is the letter the game finds, and a picture that
 * comes out right here comes out right in the game - without starting it.
 * bc7.cpp already has that deal with DdsProbe.
 *
 * Usage:
 *   FontProbe -pages <file.trc> [more.trc ...]
 *       the md5 of the font page of each level file. The question behind it:
 *       does the match have to run per level, or once.
 *
 *   FontProbe -match <level.trc> <TEXdir> [<png dir>]
 *       matches the 106 rectangles against FONT0_OG.DDS and FONT1_OG.DDS,
 *       prints the table (glyph, score, position on the atlas) and, when a
 *       directory is given, writes font0_pairs.png and font1_pairs.png: all
 *       106 glyphs as "classic | HD" side by side, the HD half cut out of the
 *       2048 atlas at the matched place. A pair that did not reach the
 *       threshold is marked in red.
 *
 *   FontProbe -atlas <level.trc> <TEXdir>
 *       what the HD atlas actually IS: white ink with an alpha channel or
 *       pre-coloured, how far its ink reaches past the eightfold classic
 *       rectangle, and whether the big picture really is the small one times
 *       eight. The three questions the draw seam depends on.
 *
 *   FontProbe -sample <level.trc> <TEXdir> <out.png> <text>
 *       draws one line classic and the same line HD, at one screen size,
 *       through the same arithmetic DrawChar uses, one over the other in a
 *       single picture - and prints how far the two disagree. %NN in the text
 *       is accent row NN of game\accent.inc (%4 is the German a-umlaut), so
 *       "M%4rkte M%1hle" is a word with two of them.
 *
 * Nothing of the remaster is copied anywhere: the tool reads the installed
 * files where they lie, exactly as the engine does.
 *
 * Build: bfont.bat
 *
 * Copyright 2026 ChrisChanSan. GPLv3, see the LICENSE file at the root of
 * this source tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bc7.h"
#include "hd/hd_fontmatch.h"
#include "core/Inflate.h"

//----------------------------------------------------------------------------
// The glyph table, straight out of the engine.
//----------------------------------------------------------------------------
struct ProbeCharDef
{
	float u, v;
	short w, h, YOffset;
	char TopShade, BottomShade;
};

static const ProbeCharDef CharDef[106] =
{
#include "chardef.inc"
};

static const char AccentTable[46][2] =
{
#include "accent.inc"
};

#define GLYPHS	((int)(sizeof(CharDef) / sizeof(CharDef[0])))

//----------------------------------------------------------------------------
// Files.
//----------------------------------------------------------------------------
static unsigned char* ReadWhole(const char* path, unsigned long* size)
{
	FILE* f = fopen(path, "rb");
	unsigned char* buf;
	long n;

	if (size)
		*size = 0;

	if (!f)
		return 0;

	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (n <= 0)
	{
		fclose(f);
		return 0;
	}

	buf = (unsigned char*)malloc((size_t)n);

	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n)
	{
		free(buf);
		fclose(f);
		return 0;
	}

	fclose(f);

	if (size)
		*size = (unsigned long)n;

	return buf;
}

static unsigned long Rd32(const unsigned char* p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
		((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* The font page of a level file.
 *
 * A .TRC opens with an int32 version, three int16 page counts and then THREE
 * zlib blocks, each of them an int32 unpacked size, an int32 packed size and
 * the stream (specific\file.cpp, LoadTextures; core\Tr5Level.cpp says the same
 * in its own words). The third block is the one that matters here: three
 * 0x40000 pages, in the order shine, font, sky - the "//font" line of
 * LoadTextures reads the middle one. 256 x 256 texels of A8R8G8B8, which is
 * the same byte order the _OG atlas has.
 */
static unsigned long* LevelFontPage(const char* path, const char** why)
{
	unsigned long size = 0;
	unsigned char* file = ReadWhole(path, &size);
	unsigned long at = 10;
	unsigned char* raw;
	unsigned long un = 0, comp = 0;
	size_t got;
	unsigned long* out;

	*why = "";

	if (!file)
	{
		*why = "cannot be read";
		return 0;
	}

	for (int block = 0; block < 3; block++)
	{
		if (at + 8 > size)
		{
			free(file);
			*why = "too short for the three texture blocks";
			return 0;
		}

		un = Rd32(file + at);
		comp = Rd32(file + at + 4);

		if (block == 2)
			break;

		at += 8 + comp;
	}

	if (un < 3 * 0x40000UL || at + 8 + comp > size)
	{
		free(file);
		*why = "the third texture block does not fit the file";
		return 0;
	}

	raw = (unsigned char*)malloc(un);

	if (!raw)
	{
		free(file);
		*why = "no memory";
		return 0;
	}

	got = un;

	if (Inflate::zlib(file + at + 8, comp, raw, un, got) || got != un)
	{
		free(raw);
		free(file);
		*why = "the third texture block does not unpack";
		return 0;
	}

	free(file);
	out = (unsigned long*)malloc(0x40000);

	if (out)
		memcpy(out, raw + 0x40000, 0x40000);

	free(raw);

	if (!out)
		*why = "no memory";

	return out;
}

static char* Join(const char* dir, const char* name)
{
	static char buf[1024];
	size_t n;

	strncpy(buf, dir, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	n = strlen(buf);

	while (n && (buf[n - 1] == '\\' || buf[n - 1] == '/'))
		buf[--n] = 0;

	snprintf(buf + n, sizeof(buf) - n, "\\%s", name);
	return buf;
}

//----------------------------------------------------------------------------
// A 24 bit RGB PNG, filter 0 on every line - the same small writer DdsProbe
// carries, for the same reason: it only has to be readable.
//----------------------------------------------------------------------------
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

	crc = Inflate::crc32((const uint8_t*)tag, 4);

	if (len)
		crc = Inflate::crc32((const uint8_t*)data, len, crc);

	PutBE(tail, crc);
	fwrite(tail, 1, 4, f);
}

static bool WritePng(const char* path, const unsigned long* px, long w, long h)
{
	FILE* f;
	unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char ihdr[13];
	unsigned char* raw;
	unsigned char* zbuf;
	unsigned long rawLen, zLen;
	unsigned char* q;

	rawLen = (unsigned long)h * (1 + (unsigned long)w * 3);
	raw = (unsigned char*)malloc(rawLen);

	if (!raw)
		return false;

	q = raw;

	for (long y = 0; y < h; y++)
	{
		*q++ = 0;

		for (long x = 0; x < w; x++)
		{
			const unsigned long c = px[y * w + x];
			*q++ = (unsigned char)(c >> 16);
			*q++ = (unsigned char)(c >> 8);
			*q++ = (unsigned char)c;
		}
	}

	// Stored deflate blocks (Inflate.h): every PNG reader takes them, and
	// the file is the raw scanlines plus a few bytes per 64 KB.
	zLen = (unsigned long)Inflate::storedSize(rawLen);
	zbuf = (unsigned char*)malloc(zLen);

	if (!zbuf || Inflate::storedTo(raw, rawLen, zbuf) != zLen)
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
	ihdr[8] = 8;
	ihdr[9] = 2;
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

//----------------------------------------------------------------------------
// Shared loading.
//----------------------------------------------------------------------------
static unsigned long* g_page;			// the level's classic font page
static unsigned long* g_og[2];			// FONT0_OG, FONT1_OG
static unsigned long* g_hd;			// FONT0.DDS or FONT1.DDS, 2048
static long g_hdSide;

static bool LoadOg(const char* texDir, int which)
{
	char name[32];
	unsigned long size = 0;
	unsigned char* file;
	const char* why = "";
	bool ok;

	snprintf(name, sizeof(name), "FONT%d_OG.DDS", which);
	file = ReadWhole(Join(texDir, name), &size);

	if (!file)
	{
		printf("  FONT%d_OG.DDS : not there\n", which);
		return false;
	}

	g_og[which] = (unsigned long*)malloc(HD_FONT_SIDE * HD_FONT_SIDE * 4);
	ok = g_og[which] && HDFontOgDecode(file, size, g_og[which], &why);
	free(file);

	if (!ok)
	{
		printf("  FONT%d_OG.DDS : refused - %s\n", which, why);
		free(g_og[which]);
		g_og[which] = 0;
		return false;
	}

	return true;
}

static bool LoadHd(const char* texDir, int which)
{
	char name[32];
	unsigned long size = 0;
	unsigned char* file;
	const char* why = "";
	long w = 0, h = 0;

	snprintf(name, sizeof(name), "FONT%d.DDS", which);
	file = ReadWhole(Join(texDir, name), &size);

	if (!file)
		return false;

	free(g_hd);
	g_hd = BC7DecodeDDS(file, size, 4096, &w, &h, &why);
	free(file);

	if (!g_hd)
	{
		printf("  FONT%d.DDS : refused - %s\n", which, why);
		return false;
	}

	g_hdSide = w;

	if (w != h)
	{
		printf("  FONT%d.DDS : %ld x %ld, not square\n", which, w, h);
		return false;
	}

	return true;
}

static void BuildCells(HDFontCell* cells)
{
	for (int i = 0; i < GLYPHS; i++)
		cells[i] = HDFontCellOf(CharDef[i].u, CharDef[i].v, CharDef[i].w, CharDef[i].h);
}

// What a CharDef index is called in a report. Index 0 is '!', and the twelve
// beyond '~' are the PlayStation pad icons the PC build still carries.
static const char* GlyphName(int i)
{
	static char buf[16];
	const int c = i + '!';

	if (c <= '~')
		snprintf(buf, sizeof(buf), "'%c'", (char)c);
	else
		snprintf(buf, sizeof(buf), "#%d", i);

	return buf;
}

//----------------------------------------------------------------------------
// -pages
//----------------------------------------------------------------------------
static int ModePages(int argc, char** argv)
{
	char first[33] = { 0 };
	int same = 0, files = 0;

	for (int i = 2; i < argc; i++)
	{
		const char* why = "";
		unsigned long* page = LevelFontPage(argv[i], &why);
		char md5[33];

		if (!page)
		{
			printf("%-40s %s\n", argv[i], why);
			continue;
		}

		HDFontMd5(page, 0x40000, md5);
		free(page);
		files++;

		if (!first[0])
			strcpy(first, md5);

		if (!strcmp(first, md5))
			same++;

		printf("%-40s font page md5 %s\n", argv[i], md5);
	}

	printf("\n%d file(s), %d of them with the same font page\n", files, same);
	return 0;
}

//----------------------------------------------------------------------------
// -match
//----------------------------------------------------------------------------

// One glyph pair into the contact sheet: the classic rectangle magnified by
// SCALE, then the same rectangle out of the 2048 atlas shrunk by 8/SCALE, so
// that both halves are the same size on screen and the difference that is
// left is the difference in sharpness.
#define SHEET_SCALE	4
#define SHEET_COLS	6

static unsigned long Over(unsigned long src, unsigned long dst)
{
	const unsigned long a = src >> 24;

	if (!a)
		return dst;

	if (a == 255)
		return src | 0xFF000000UL;

	{
		const unsigned long sr = (src >> 16) & 255, sg = (src >> 8) & 255, sb = src & 255;
		const unsigned long dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
		const unsigned long r = (sr * a + dr * (255 - a)) / 255;
		const unsigned long g = (sg * a + dg * (255 - a)) / 255;
		const unsigned long b = (sb * a + db * (255 - a)) / 255;
		return 0xFF000000UL | (r << 16) | (g << 8) | b;
	}
}

static unsigned long Checker(long x, long y, bool bad)
{
	const unsigned long c = ((x >> 3) + (y >> 3)) & 1 ? 0xFF303030UL : 0xFF181818UL;
	return bad ? (c | 0xFF400000UL) : c;
}

static int ModeMatch(int argc, char** argv)
{
	const char* trc = argv[2];
	const char* texDir = argv[3];
	const char* pngDir = argc > 4 ? argv[4] : 0;
	const char* why = "";
	HDFontCell cells[106];
	HDFontHit hits[2][106];
	char md5[33];
	int found[2] = { 0, 0 };

	g_page = LevelFontPage(trc, &why);

	if (!g_page)
	{
		printf("%s: %s\n", trc, why);
		return 2;
	}

	HDFontMd5(g_page, 0x40000, md5);
	BuildCells(cells);
	printf("classic font page of %s, md5 %s\n", trc, md5);
	printf("threshold %.2f, ink at alpha >= %d\n\n", HD_FONT_ACCEPT, HD_FONT_INK);

	for (int which = 0; which < 2; which++)
	{
		if (!LoadOg(texDir, which))
			continue;

		found[which] = HDFontMatchAll(g_og[which], g_page, cells, GLYPHS,
			HD_FONT_ACCEPT, hits[which]);
		printf("FONT%d_OG.DDS : %d of %d glyphs matched\n", which, found[which], GLYPHS);
	}

	printf("\n%-5s %-6s %-14s  %-22s  %-22s\n", "idx", "glyph", "classic cell",
		"FONT0 hit", "FONT1 hit");

	for (int i = 0; i < GLYPHS; i++)
	{
		printf("%-5d %-6s %3d,%3d %2dx%-2d  ", i, GlyphName(i),
			cells[i].x, cells[i].y, cells[i].w, cells[i].h);

		for (int which = 0; which < 2; which++)
		{
			if (!g_og[which])
				printf("%-24s", "-");
			else
				printf("%3d,%3d  %.3f %-6s", hits[which][i].x, hits[which][i].y,
					hits[which][i].score, hits[which][i].ok ? "ok" : "BELOW");
		}

		printf("\n");
	}

	// The score histogram, so "88 of 106" can be read as more than one number.
	for (int which = 0; which < 2; which++)
	{
		int band[6] = { 0, 0, 0, 0, 0, 0 };

		if (!g_og[which])
			continue;

		for (int i = 0; i < GLYPHS; i++)
		{
			const float s = hits[which][i].score;
			int b = 0;

			if (s >= 0.999f) b = 5;
			else if (s >= 0.94f) b = 4;
			else if (s >= 0.80f) b = 3;
			else if (s >= 0.60f) b = 2;
			else if (s >= 0.40f) b = 1;

			band[b]++;
		}

		printf("\nFONT%d scores: 1.000 %d | 0.94..0.99 %d | 0.80.. %d | 0.60.. %d | 0.40.. %d | under %d\n",
			which, band[5], band[4], band[3], band[2], band[1], band[0]);
	}

	if (!pngDir)
		return 0;

	for (int which = 0; which < 2; which++)
	{
		char path[1024];
		long cellW = 0, cellH = 0, sheetW, sheetH;
		unsigned long* sheet;
		int rows;

		if (!g_og[which] || !LoadHd(texDir, which))
			continue;

		for (int i = 0; i < GLYPHS; i++)
		{
			if (cells[i].w > cellW) cellW = cells[i].w;
			if (cells[i].h > cellH) cellH = cells[i].h;
		}

		cellW = cellW * SHEET_SCALE + 6;
		cellH = cellH * SHEET_SCALE + 6;
		rows = (GLYPHS + SHEET_COLS - 1) / SHEET_COLS;
		sheetW = SHEET_COLS * (2 * cellW + 8);
		sheetH = rows * (cellH + 6);
		sheet = (unsigned long*)malloc((size_t)sheetW * sheetH * 4);

		if (!sheet)
			continue;

		for (long i = 0; i < sheetW * sheetH; i++)
			sheet[i] = 0xFF000000UL;

		for (int i = 0; i < GLYPHS; i++)
		{
			const long col = i % SHEET_COLS, row = i / SHEET_COLS;
			const long ox = col * (2 * cellW + 8) + 3;
			const long oy = row * (cellH + 6) + 3;
			const bool bad = !hits[which][i].ok;
			const long shrink = HD_FONT_SCALE / SHEET_SCALE;

			for (long y = 0; y < cells[i].h * SHEET_SCALE; y++)
			{
				for (long x = 0; x < cells[i].w * SHEET_SCALE; x++)
				{
					const long sx = cells[i].x + x / SHEET_SCALE;
					const long sy = cells[i].y + y / SHEET_SCALE;
					unsigned long src = g_page[sy * HD_FONT_SIDE + sx];
					sheet[(oy + y) * sheetW + ox + x] =
						Over(src, Checker(ox + x, oy + y, bad));

					// The HD half: a shrink x shrink box out of the 2048.
					{
						unsigned long r = 0, g = 0, b = 0, a = 0;
						const long hx = (hits[which][i].x + x / SHEET_SCALE) * HD_FONT_SCALE;
						const long hy = (hits[which][i].y + y / SHEET_SCALE) * HD_FONT_SCALE;
						const long fx = hx + (x % SHEET_SCALE) * shrink;
						const long fy = hy + (y % SHEET_SCALE) * shrink;

						for (long by = 0; by < shrink; by++)
						{
							for (long bx = 0; bx < shrink; bx++)
							{
								const long px = fx + bx, py = fy + by;
								unsigned long c;

								if (px < 0 || py < 0 || px >= g_hdSide || py >= g_hdSide)
									continue;

								c = g_hd[py * g_hdSide + px];
								a += c >> 24;
								r += (c >> 16) & 255;
								g += (c >> 8) & 255;
								b += c & 255;
							}
						}

						{
							const unsigned long n = (unsigned long)(shrink * shrink);
							const unsigned long c = ((a / n) << 24) | ((r / n) << 16) |
								((g / n) << 8) | (b / n);
							const long tx = ox + cellW + 8 + x;
							sheet[(oy + y) * sheetW + tx] =
								Over(c, Checker(tx, oy + y, bad));
						}
					}
				}
			}
		}

		snprintf(path, sizeof(path), "%s\\font%d_pairs.png", pngDir, which);

		if (WritePng(path, sheet, sheetW, sheetH))
			printf("\nwrote %s (%ld x %ld, %d columns of classic|HD, index runs left to right)\n",
				path, sheetW, sheetH, SHEET_COLS);

		free(sheet);
	}

	return 0;
}

//----------------------------------------------------------------------------
// -atlas
//----------------------------------------------------------------------------
static int ModeAtlas(int argc, char** argv)
{
	static const double marginOf[] = { 0.0, 0.5, 1.0, 1.5, 2.0, 3.0 };
#define MARGINS	((int)(sizeof(marginOf) / sizeof(marginOf[0])))
	const char* trc = argv[2];
	const char* texDir = argv[3];
	const char* why = "";
	HDFontCell cells[106];
	HDFontHit hits[106];
	long overLeft = 0, overRight = 0, overTop = 0, overBottom = 0;
	char overWho[4][16] = { "-", "-", "-", "-" };
	double marginOwn[MARGINS] = { 0 }, marginOwnAll[MARGINS] = { 0 };
	double marginLit[MARGINS] = { 0 }, marginLitAll[MARGINS] = { 0 };
	double marginForeign[MARGINS] = { 0 };
	double marginWorst[MARGINS] = { 1, 1, 1, 1, 1, 1 };
	char marginWho[MARGINS][16] = { "-", "-", "-", "-", "-", "-" };
	double marginWorstForeign[MARGINS] = { 0 };
	char marginForeignWho[MARGINS][16] = { "-", "-", "-", "-", "-", "-" };
	long shrinkTexels = 0, shrinkDiff = 0;
	long edgeTexels = 0, edgeBright = 0;
	double edgeLum = 0.0;
	long solid = 0;
	double solidLum = 0.0;
	int measured = 0;

	(void)argc;
	g_page = LevelFontPage(trc, &why);

	if (!g_page)
	{
		printf("%s: %s\n", trc, why);
		return 2;
	}

	BuildCells(cells);

	if (!LoadOg(texDir, 0) || !LoadHd(texDir, 0))
		return 2;

	printf("FONT0.DDS decoded: %ld x %ld, that is %d x the 256 atlas\n\n",
		g_hdSide, g_hdSide, (int)(g_hdSide / HD_FONT_SIDE));

	HDFontMatchAll(g_og[0], g_page, cells, GLYPHS, HD_FONT_ACCEPT, hits);

	for (int i = 0; i < GLYPHS; i++)
	{
		const long M = 24;		// three classic texels of margin to look in
		long x0, y0, x1, y1, ww, wh;
		long hdX0 = 1 << 20, hdY0 = 1 << 20, hdX1 = -1, hdY1 = -1;
		unsigned char* seen;
		long* stack;
		long top = 0;

		if (!hits[i].ok)
			continue;

		measured++;

		/* Is the big picture the small one times eight? The direct test:
		 * shrink the 2048 back down to 256 with an 8 x 8 box filter over the
		 * matched rectangle and compare the ink mask with the _OG's own. */
		for (long y = 0; y < cells[i].h; y++)
		{
			for (long x = 0; x < cells[i].w; x++)
			{
				unsigned long sum = 0;
				bool ogInk, hdInk;

				for (long by = 0; by < HD_FONT_SCALE; by++)
					for (long bx = 0; bx < HD_FONT_SCALE; bx++)
						sum += g_hd[((hits[i].y + y) * HD_FONT_SCALE + by) * g_hdSide +
							(hits[i].x + x) * HD_FONT_SCALE + bx] >> 24;

				hdInk = (sum / (HD_FONT_SCALE * HD_FONT_SCALE)) >= HD_FONT_INK;
				ogInk = (g_og[0][(hits[i].y + y) * HD_FONT_SIDE + hits[i].x + x] >> 24) >= HD_FONT_INK;
				shrinkTexels++;

				if (ogInk != hdInk)
					shrinkDiff++;
			}
		}

		/* The glyph's own ink on the 2048, and only its own. The atlas packs
		 * the letters two or three classic texels apart, so a window that
		 * takes every ink texel in it measures the neighbour as well. A flood
		 * fill from inside the matched rectangle cannot cross the transparent
		 * gap between two letters, and the rim and the drop shadow the
		 * remaster painted around the letter ARE part of the same blob -
		 * which is exactly what has to be measured. */
		x0 = hits[i].x * HD_FONT_SCALE - M;
		y0 = hits[i].y * HD_FONT_SCALE - M;
		x1 = (hits[i].x + cells[i].w) * HD_FONT_SCALE + M;
		y1 = (hits[i].y + cells[i].h) * HD_FONT_SCALE + M;

		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > g_hdSide) x1 = g_hdSide;
		if (y1 > g_hdSide) y1 = g_hdSide;

		ww = x1 - x0;
		wh = y1 - y0;
		seen = (unsigned char*)calloc((size_t)ww * wh, 1);
		stack = (long*)malloc((size_t)ww * wh * sizeof(long));

		if (!seen || !stack)
		{
			free(seen);
			free(stack);
			continue;
		}

		for (long y = hits[i].y * HD_FONT_SCALE; y < (hits[i].y + cells[i].h) * HD_FONT_SCALE; y++)
		{
			for (long x = hits[i].x * HD_FONT_SCALE; x < (hits[i].x + cells[i].w) * HD_FONT_SCALE; x++)
			{
				if (x < x0 || y < y0 || x >= x1 || y >= y1)
					continue;

				if ((g_hd[y * g_hdSide + x] >> 24) < HD_FONT_INK)
					continue;

				if (seen[(y - y0) * ww + (x - x0)])
					continue;

				seen[(y - y0) * ww + (x - x0)] = 1;
				stack[top++] = (y - y0) * ww + (x - x0);
			}
		}

		while (top > 0)
		{
			const long at = stack[--top];
			const long lx = at % ww, ly = at / ww;

			if (x0 + lx < hdX0) hdX0 = x0 + lx;
			if (y0 + ly < hdY0) hdY0 = y0 + ly;
			if (x0 + lx > hdX1) hdX1 = x0 + lx;
			if (y0 + ly > hdY1) hdY1 = y0 + ly;

			for (int dy = -1; dy <= 1; dy++)
			{
				for (int dx = -1; dx <= 1; dx++)
				{
					const long nx = lx + dx, ny = ly + dy;

					if (nx < 0 || ny < 0 || nx >= ww || ny >= wh)
						continue;

					if (seen[ny * ww + nx])
						continue;

					if ((g_hd[(y0 + ny) * g_hdSide + x0 + nx] >> 24) < HD_FONT_INK)
						continue;

					seen[ny * ww + nx] = 1;
					stack[top++] = ny * ww + nx;
				}
			}
		}

		free(stack);

		if (hdX1 < 0)
		{
			free(seen);
			continue;
		}

		// How far past the eightfold CELL does the glyph's own ink reach?
		{
			const long cellX0 = hits[i].x * HD_FONT_SCALE;
			const long cellY0 = hits[i].y * HD_FONT_SCALE;
			const long cellX1 = (hits[i].x + cells[i].w) * HD_FONT_SCALE - 1;
			const long cellY1 = (hits[i].y + cells[i].h) * HD_FONT_SCALE - 1;

			if (cellX0 - hdX0 > overLeft) { overLeft = cellX0 - hdX0; strcpy(overWho[0], GlyphName(i)); }
			if (hdX1 - cellX1 > overRight) { overRight = hdX1 - cellX1; strcpy(overWho[1], GlyphName(i)); }
			if (cellY0 - hdY0 > overTop) { overTop = cellY0 - hdY0; strcpy(overWho[2], GlyphName(i)); }
			if (hdY1 - cellY1 > overBottom) { overBottom = hdY1 - cellY1; strcpy(overWho[3], GlyphName(i)); }
		}

		/* And the number the margin is picked from. The classic UV samples
		 * the glyph rectangle inset by half a texel on each side, which at
		 * eight times the size is four HD texels; widening that window by m
		 * classic texels lets more of the atlas through. Three things are
		 * counted for each m: how much of the letter's OWN ink is inside the
		 * window, how much of its WHITE ink is (the shadow around it matters
		 * less than the letter itself), and how much FOREIGN ink comes in
		 * with it - the neighbouring letter, which is the price of a wide
		 * margin and the reason it cannot simply be made large. */
		{
			double own = 0.0, lit = 0.0;

			for (long y = y0; y < y1; y++)
			{
				for (long x = x0; x < x1; x++)
				{
					const unsigned long c = g_hd[y * g_hdSide + x];
					const double a = (double)(c >> 24);

					if (!seen[(y - y0) * ww + (x - x0)])
						continue;

					own += a;

					if ((((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3 >= 128)
						lit += a;
				}
			}

			for (int m = 0; m < MARGINS; m++)
			{
				const double pad = marginOf[m] * HD_FONT_SCALE;
				/* The window the engine really samples, drift and all. u is
				 * stored as n/255 and DrawChar reads it as n/256, so the
				 * classic window does not start at the left edge of the
				 * rectangle but up to a whole texel to the right of it - at
				 * x0 * 256/255. The HD path translates that window without
				 * correcting it, so the measurement has to carry the same
				 * drift or it measures a window nobody draws. */
				const double driftX = (double)cells[i].x / 255.0 * HD_FONT_SCALE;
				const double driftY = (double)cells[i].y / 255.0 * HD_FONT_SCALE;
				const double wx0 = hits[i].x * HD_FONT_SCALE + driftX + 4.0 - pad;
				const double wy0 = hits[i].y * HD_FONT_SCALE + driftY + 4.0 - pad;
				const double wx1 = (hits[i].x + cells[i].w) * HD_FONT_SCALE + driftX - 4.0 + pad;
				const double wy1 = (hits[i].y + cells[i].h) * HD_FONT_SCALE + driftY - 4.0 + pad;
				double inOwn = 0.0, inLit = 0.0, inForeign = 0.0;

				for (long y = y0; y < y1; y++)
				{
					for (long x = x0; x < x1; x++)
					{
						const unsigned long c = g_hd[y * g_hdSide + x];
						const double a = (double)(c >> 24);

						if ((double)x + 0.5 < wx0 || (double)x + 0.5 > wx1 ||
							(double)y + 0.5 < wy0 || (double)y + 0.5 > wy1)
							continue;

						if (seen[(y - y0) * ww + (x - x0)])
						{
							inOwn += a;

							if ((((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3 >= 128)
								inLit += a;
						}
						else
							inForeign += a;
					}
				}

				marginOwn[m] += inOwn;
				marginOwnAll[m] += own;
				marginLit[m] += inLit;
				marginLitAll[m] += lit;
				marginForeign[m] += inForeign;

				if (lit > 0.0 && inLit / lit < marginWorst[m])
				{
					marginWorst[m] = inLit / lit;
					strcpy(marginWho[m], GlyphName(i));
				}

				if (own > 0.0 && inForeign / own > marginWorstForeign[m])
				{
					marginWorstForeign[m] = inForeign / own;
					strcpy(marginForeignWho[m], GlyphName(i));
				}
			}
		}

		// White ink, or pre-coloured? Two numbers: the brightness of the
		// texels that are FULLY opaque and the brightness of the ones on the
		// antialiased rim.
		for (long y = y0; y < y1; y++)
		{
			for (long x = x0; x < x1; x++)
			{
				const unsigned long c = g_hd[y * g_hdSide + x];
				const unsigned long a = c >> 24;
				const double lum = (double)(((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3.0;

				if (!seen[(y - y0) * ww + (x - x0)])
					continue;

				if (a == 255)
				{
					solid++;
					solidLum += lum;
				}
				else if (a >= HD_FONT_INK)
				{
					edgeTexels++;
					edgeLum += lum;

					if (lum >= 192.0)
						edgeBright++;
				}
			}
		}

		free(seen);
	}

	printf("measured over the %d matched glyphs:\n", measured);
	printf("  the 2048 shrunk back to 256 against the _OG picture:\n");
	printf("    %ld of %ld texels of the matched rectangles differ (%.2f %%)\n",
		shrinkDiff, shrinkTexels,
		shrinkTexels ? 100.0 * (double)shrinkDiff / (double)shrinkTexels : 0.0);
	printf("  the glyph own ink reaching PAST the eightfold classic rectangle:\n");
	printf("    left %2ld %-5s  right %2ld %-5s  top %2ld %-5s  bottom %2ld %-5s  HD texels\n",
		overLeft, overWho[0], overRight, overWho[1], overTop, overWho[2],
		overBottom, overWho[3]);
	printf("  what the sampled window holds, by margin:\n");

	for (int m = 0; m < MARGINS; m++)
		printf("    %.1f texel(s) : own ink %6.2f %%  white ink %6.2f %%"
			"  (worst glyph %6.2f %% %-5s)  foreign ink %5.2f %% (worst %6.2f %% %-5s)\n",
			marginOf[m],
			marginOwnAll[m] > 0.0 ? 100.0 * marginOwn[m] / marginOwnAll[m] : 0.0,
			marginLitAll[m] > 0.0 ? 100.0 * marginLit[m] / marginLitAll[m] : 0.0,
			100.0 * marginWorst[m], marginWho[m],
			marginOwnAll[m] > 0.0 ? 100.0 * marginForeign[m] / marginOwnAll[m] : 0.0,
			100.0 * marginWorstForeign[m], marginForeignWho[m]);

	/* And what the engine actually gives each glyph. The table above is the
	 * trade-off a UNIFORM margin would make and the reason there is not one;
	 * this is the per-glyph answer HDFontMargins works out, which is what
	 * DrawChar uses. */
	{
		float margins[106];
		int full = 0, none = 0, part = 0;

		HDFontMargins(g_hd, g_hdSide, g_page, cells, hits, GLYPHS, HD_FONT_MARGIN, margins);
		printf("  what each glyph really gets (ceiling %.2f):\n", HD_FONT_MARGIN);

		for (int i = 0; i < GLYPHS; i++)
		{
			if (!hits[i].ok)
				continue;

			if (margins[i] >= HD_FONT_MARGIN)
				full++;
			else if (margins[i] <= 0.0f)
				none++;
			else
			{
				part++;
				printf("    %-5s only %.3f texel(s) - the atlas has no more room\n",
					GlyphName(i), margins[i]);
			}
		}

		printf("    %d glyph(s) get the whole ceiling, %d get part of it, %d get none\n",
			full, part, none);
	}

	printf("  colour of the ink:\n");
	printf("    fully opaque texels : %ld, mean brightness %.1f of 255\n",
		solid, solid ? solidLum / solid : 0.0);
	printf("    rim texels (alpha %d..254) : %ld, mean brightness %.1f, %ld of them >= 192\n",
		HD_FONT_INK, edgeTexels, edgeTexels ? edgeLum / edgeTexels : 0.0, edgeBright);
	return 0;
}

//----------------------------------------------------------------------------
// -sample
//
// The line is drawn twice through the very arithmetic of DrawChar, once out
// of the classic page and once out of the atlas, and both halves are put in
// one picture. What the numbers underneath say is how far the two disagree:
// the quad of a glyph, and the centre of gravity of its ink.
//----------------------------------------------------------------------------
#define SAMPLE_W	1920
#define SAMPLE_H	1080

struct SampleQuad
{
	long x1, y1, x2, y2;		// the classic quad, in screen pixels
	float u1, v1, u2, v2;		// what it samples, in units of the 256 page
	int index;
};

static long g_winxmax = SAMPLE_W - 1;
static long g_winymax = SAMPLE_H - 1;

// InitFont's own scaling, for the screen size above.
static void ScaledDef(int i, short* w, short* h, short* yoff)
{
	*h = (short)((float)CharDef[i].h * (float)(g_winymax / 240.0F));
	*w = (short)((float)CharDef[i].w * (float)(g_winxmax / 512.0F));
	*yoff = (short)((float)CharDef[i].YOffset * (float)(g_winymax / 240.0F));
}

// DrawChar's own UV arithmetic, in units of the 256 page.
static void ClassicUv(int i, short w, short h, float* u1, float* v1, float* u2, float* v2)
{
	*u1 = CharDef[i].u + (1.0F / 512.0F);
	*v1 = CharDef[i].v + (1.0F / 512.0F);
	*u2 = 512.0F / float(g_winxmax + 1) * (float)w * (1.0F / 256.0F) + CharDef[i].u - (1.0F / 512.0F);
	*v2 = 240.0F / float(g_winymax + 1) * (float)h * (1.0F / 256.0F) + CharDef[i].v - (1.0F / 512.0F);
}

// PrintString's walk, cut down to what a sample line needs: no flags, no
// colour codes, no line breaks. %NN is accent row NN.
static int LayoutLine(const char* text, long x, long y, SampleQuad* out, int max)
{
	int n = 0;

	while (*text && n < max)
	{
		int index = -1, accent = -1;
		short w, h, yoff;

		if (*text == '%' && text[1] >= '0' && text[1] <= '9')
		{
			int row = 0;
			text++;

			while (*text >= '0' && *text <= '9')
				row = row * 10 + (*text++ - '0');

			if (row < 0 || row > 45 || AccentTable[row][0] == ' ')
				continue;

			index = AccentTable[row][0] - '!';
			accent = AccentTable[row][1] == ' ' ? -1 : AccentTable[row][1] - '!';
		}
		else if (*text == ' ')
		{
			x += long(float(g_winxmax + 1) / 640.0F * 8.0F);
			text++;
			continue;
		}
		else
		{
			const unsigned char c = (unsigned char)*text++;

			if (c < '!' || c - '!' >= GLYPHS)
				continue;

			index = c - '!';
		}

		ScaledDef(index, &w, &h, &yoff);
		out[n].index = index;
		out[n].x1 = x;
		out[n].x2 = x + w;
		out[n].y1 = y + yoff;
		out[n].y2 = y + h + yoff;
		ClassicUv(index, w, h, &out[n].u1, &out[n].v1, &out[n].u2, &out[n].v2);
		n++;

		if (accent >= 0 && n < max)
		{
			short aw, ah, ayoff;
			const long ax = w / 2 + x - 3;
			const long ay = y + yoff;

			ScaledDef(accent, &aw, &ah, &ayoff);
			out[n].index = accent;
			out[n].x1 = ax;
			out[n].x2 = ax + aw;
			out[n].y1 = ay + ayoff;
			out[n].y2 = ay + ah + ayoff;
			ClassicUv(accent, aw, ah, &out[n].u1, &out[n].v1, &out[n].u2, &out[n].v2);
			n++;
		}

		x += w;
	}

	return n;
}

static unsigned long SampleBilinear(const unsigned long* px, long side, float u, float v)
{
	const float fx = u * (float)side - 0.5f;
	const float fy = v * (float)side - 0.5f;
	long x0 = (long)floorf(fx), y0 = (long)floorf(fy);
	const float ax = fx - (float)x0, ay = fy - (float)y0;
	unsigned long out = 0;

	for (int ch = 0; ch < 4; ch++)
	{
		float acc = 0.0f;

		for (int j = 0; j < 2; j++)
		{
			for (int i = 0; i < 2; i++)
			{
				long x = x0 + i, y = y0 + j;
				float wgt = (i ? ax : 1.0f - ax) * (j ? ay : 1.0f - ay);

				if (x < 0) x = 0;
				if (y < 0) y = 0;
				if (x >= side) x = side - 1;
				if (y >= side) y = side - 1;

				acc += wgt * (float)((px[y * side + x] >> (ch * 8)) & 255);
			}
		}

		{
			long c = (long)(acc + 0.5f);

			if (c < 0) c = 0;
			if (c > 255) c = 255;

			out |= ((unsigned long)c) << (ch * 8);
		}
	}

	return out;
}

// Draws one quad into a buffer and returns the ink's centre of gravity.
static void DrawQuad(unsigned long* buf, long bw, long bh,
	float x1, float y1, float x2, float y2,
	const unsigned long* tex, long side, float u1, float v1, float u2, float v2,
	double* cx, double* cy, double* mass)
{
	const long px0 = (long)floorf(x1), px1 = (long)ceilf(x2);
	const long py0 = (long)floorf(y1), py1 = (long)ceilf(y2);

	*cx = *cy = *mass = 0.0;

	for (long y = py0; y < py1; y++)
	{
		for (long x = px0; x < px1; x++)
		{
			const float sx = (float)x + 0.5f, sy = (float)y + 0.5f;
			float fu, fv;
			unsigned long c;
			double a;

			if (x < 0 || y < 0 || x >= bw || y >= bh)
				continue;

			if (sx < x1 || sx > x2 || sy < y1 || sy > y2)
				continue;

			fu = u1 + (u2 - u1) * (sx - x1) / (x2 - x1);
			fv = v1 + (v2 - v1) * (sy - y1) / (y2 - y1);
			c = SampleBilinear(tex, side, fu, fv);
			a = (double)(c >> 24) / 255.0;
			*cx += a * (double)x;
			*cy += a * (double)y;
			*mass += a;
			buf[y * bw + x] = Over(c, buf[y * bw + x]);
		}
	}

	if (*mass > 0.0)
	{
		*cx /= *mass;
		*cy /= *mass;
	}
}

static int ModeSample(int argc, char** argv)
{
	const char* trc = argv[2];
	const char* texDir = argv[3];
	const char* out = argv[4];
	const char* text = argc > 5 ? argv[5] : "LOAD GAME";
	// An optional sixth argument overrides the margin, so that widening the
	// window can be looked at without rebuilding.
	const float margin = argc > 6 ? (float)atof(argv[6]) : HD_FONT_MARGIN;
	const char* why = "";
	HDFontCell cells[106];
	HDFontHit hits[106];
	SampleQuad quads[256];
	int n;
	long bw, bh;
	unsigned long* buf;
	float margins[106];
	double worstCentre = 0.0, worstSlope = 0.0;
	long lineRight;

	g_page = LevelFontPage(trc, &why);

	if (!g_page)
	{
		printf("%s: %s\n", trc, why);
		return 2;
	}

	BuildCells(cells);

	if (!LoadOg(texDir, 0) || !LoadHd(texDir, 0))
		return 2;

	HDFontMatchAll(g_og[0], g_page, cells, GLYPHS, HD_FONT_ACCEPT, hits);

	/* The per-glyph margins, out of the engine's own routine - so the picture
	 * this tool draws is the picture the game draws, and not one with a margin
	 * somebody typed in. The optional sixth argument only raises or lowers the
	 * CEILING; how much of it a glyph really gets is still decided by how much
	 * room it has on the atlas. */
	HDFontMargins(g_hd, g_hdSide, g_page, cells, hits, GLYPHS, margin, margins);

	n = LayoutLine(text, 40, 120, quads, 256);

	if (!n)
	{
		printf("nothing to draw\n");
		return 2;
	}

	lineRight = 0;

	for (int i = 0; i < n; i++)
	{
		if (quads[i].x2 > lineRight)
			lineRight = quads[i].x2;
	}

	bw = lineRight + 60;
	bh = 340;
	buf = (unsigned long*)malloc((size_t)bw * bh * 4);

	if (!buf)
		return 2;

	for (long i = 0; i < bw * bh; i++)
		buf[i] = 0xFF101820UL;

	printf("line: \"%s\" at %d x %d, %d quad(s)\n", text, SAMPLE_W, SAMPLE_H, n);
	printf("%-5s %-6s %-22s %-9s  %s\n", "idx", "glyph", "classic quad", "hd", "centre difference");

	for (int i = 0; i < n; i++)
	{
		const SampleQuad& q = quads[i];
		const bool hd = hits[q.index].ok;
		double ax, ay, am, bx, by, bm;
		double grew;			// how much the quad grew, for the report

		// Classic, top line.
		DrawQuad(buf, bw, bh, (float)q.x1, (float)(q.y1 - 60), (float)q.x2, (float)(q.y2 - 60),
			g_page, HD_FONT_SIDE, q.u1, q.v1, q.u2, q.v2, &ax, &ay, &am);

		if (!hd)
		{
			// The HD line draws the classic glyph for a letter with no hit -
			// that is exactly what the engine does.
			DrawQuad(buf, bw, bh, (float)q.x1, (float)(q.y1 + 60), (float)q.x2, (float)(q.y2 + 60),
				g_page, HD_FONT_SIDE, q.u1, q.v1, q.u2, q.v2, &bx, &by, &bm);
			printf("%-5d %-6s %4ld,%4ld %4ld,%4ld  %-9s  (classic)\n",
				q.index, GlyphName(q.index), q.x1, q.y1, q.x2, q.y2, "-");
			continue;
		}

		/* The relocation, and it is the whole seam: the atlas is the classic
		 * page times eight, so in NORMALISED coordinates the same glyph sits
		 * at the same place plus a whole number of classic texels. The UV of
		 * the classic quad is therefore not recomputed but SHIFTED, which is
		 * why nothing can move or change size. */
		{
			const float du = (float)(hits[q.index].x - cells[q.index].x) / (float)HD_FONT_SIDE;
			const float dv = (float)(hits[q.index].y - cells[q.index].y) / (float)HD_FONT_SIDE;

			// The margin, on the picture and on the screen in the same ratio.
			const float mu = margins[q.index] / (float)HD_FONT_SIDE;
			const float mx = (float)(q.x2 - q.x1) * mu / (q.u2 - q.u1);
			const float my = (float)(q.y2 - q.y1) * mu / (q.v2 - q.v1);

			grew = mx;
			DrawQuad(buf, bw, bh,
				(float)q.x1 - mx, (float)(q.y1 + 60) - my,
				(float)q.x2 + mx, (float)(q.y2 + 60) + my,
				g_hd, g_hdSide,
				q.u1 + du - mu, q.v1 + dv - mu, q.u2 + du + mu, q.v2 + dv + mu,
				&bx, &by, &bm);
		}

		{
			// The HD line is drawn 120 px under the classic one, so that is
			// the distance the two centres are SUPPOSED to have.
			const double dcx = bx - ax;
			const double dcy = by - ay - 120.0;
			const double d = sqrt(dcx * dcx + dcy * dcy);

			if (d > worstCentre)
				worstCentre = d;

			printf("%-5d %-6s %4ld,%4ld %4ld,%4ld  +/-%5.2f px  %.3f px\n",
				q.index, GlyphName(q.index), q.x1, q.y1, q.x2, q.y2, grew, d);
		}

		// And the mapping itself: how many classic texels one screen pixel
		// walks, classic against HD. They must be the same number.
		{
			const double slopeA = (double)(q.u2 - q.u1) / (double)(q.x2 - q.x1);
			const float mu = margins[q.index] / (float)HD_FONT_SIDE;
			const float mx = (float)(q.x2 - q.x1) * mu / (q.u2 - q.u1);
			const double slopeB = (double)((q.u2 + mu) - (q.u1 - mu)) /
				(double)((q.x2 + mx) - (q.x1 - mx));
			const double rel = fabs(slopeA - slopeB) / slopeA;

			if (rel > worstSlope)
				worstSlope = rel;
		}
	}

	printf("\nworst distance between the classic and the HD centre of gravity : %.3f px\n", worstCentre);
	printf("worst relative difference in texels per screen pixel             : %.3g\n", worstSlope);
	printf("classic line at the top, HD line 120 px below it.\n");

	if (WritePng(out, buf, bw, bh))
		printf("wrote %s (%ld x %ld)\n", out, bw, bh);

	free(buf);
	return 0;
}

//----------------------------------------------------------------------------
int main(int argc, char** argv)
{
	const char* why = "";

	if (!BC7SelfTest(&why))
	{
		printf("BC7 self test failed: %s\n", why);
		return 2;
	}

	if (argc >= 3 && !strcmp(argv[1], "-pages"))
		return ModePages(argc, argv);

	if (argc >= 4 && !strcmp(argv[1], "-match"))
		return ModeMatch(argc, argv);

	if (argc >= 4 && !strcmp(argv[1], "-atlas"))
		return ModeAtlas(argc, argv);

	if (argc >= 5 && !strcmp(argv[1], "-sample"))
		return ModeSample(argc, argv);

	printf("usage: FontProbe -pages  <file.trc> [more.trc ...]\n");
	printf("       FontProbe -match  <level.trc> <TEXdir> [<png dir>]\n");
	printf("       FontProbe -atlas  <level.trc> <TEXdir>\n");
	printf("       FontProbe -sample <level.trc> <TEXdir> <out.png> <text>\n");
	printf("\n<TEXdir> is <remaster>\\5\\TEX, the folder with FONT0.DDS and\n");
	printf("FONT0_OG.DDS in it. Nothing is written there and nothing is copied.\n");
	return 2;
}

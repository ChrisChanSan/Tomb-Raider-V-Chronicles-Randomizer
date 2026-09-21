/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The template match is TROne's idea (Copyright 2026 Todesengel, GPLv3,
 * host\render\game_renderer.cpp, the block marked "hdtext") - see
 * hd_fontmatch.h, which says why the citation carries no line numbers, and
 * THIRD-PARTY.md.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_fontmatch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// No engine header, on purpose - see hd_fontmatch.h.

namespace
{
	unsigned long DwordAt(const unsigned char* p)
	{
		return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
			((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
	}

	// The ink mask of a whole 256 x 256 picture, one byte per texel.
	void InkMask(const unsigned long* px, unsigned char* out)
	{
		for (long i = 0; i < HD_FONT_SIDE * HD_FONT_SIDE; i++)
			out[i] = (px[i] >> 24) >= HD_FONT_INK ? 1 : 0;
	}

	/* THE PREFILTER. A summed area table over the atlas ink, so that the ink
	 * count of any window is three additions instead of w * h reads. TROne
	 * measured that a window whose ink count is nowhere near the template's
	 * cannot reach 0.94 anyway and prefilters on 0.85 .. 1.20 of it; the
	 * same window here. Without it the full comparison would run 65 536
	 * times per glyph instead of a few dozen. */
	struct Sat
	{
		// (SIDE + 1) squared, so that the row and column of zeroes above and
		// left of the picture need no special case.
		long v[(HD_FONT_SIDE + 1) * (HD_FONT_SIDE + 1)];

		void build(const unsigned char* mask)
		{
			const long stride = HD_FONT_SIDE + 1;
			memset(v, 0, sizeof(v));

			for (long y = 0; y < HD_FONT_SIDE; y++)
				for (long x = 0; x < HD_FONT_SIDE; x++)
					v[(y + 1) * stride + x + 1] = mask[y * HD_FONT_SIDE + x] +
						v[y * stride + x + 1] + v[(y + 1) * stride + x] -
						v[y * stride + x];
		}

		long window(long x, long y, long w, long h) const
		{
			const long stride = HD_FONT_SIDE + 1;
			return v[(y + h) * stride + x + w] - v[y * stride + x + w] -
				v[(y + h) * stride + x] + v[y * stride + x];
		}
	};
}

/* MD5, written from RFC 1321's own description of the algorithm. It names a
 * texture page in a log line and in the diagnostic tool's output and is
 * never used for anything that has to be secure; what it has to be is the
 * SAME number on both sides, and small enough to sit in this file without an
 * extra dependency. */
namespace
{
	struct Md5
	{
		unsigned long a, b, c, d;
		unsigned long lengthLow, lengthHigh;
		unsigned char block[64];
		unsigned long fill;
	};

	unsigned long Md5Rotate(unsigned long x, int n)
	{
		return (x << n) | (x >> (32 - n));
	}

	void Md5Round(Md5& md, const unsigned char* p)
	{
		static const unsigned long K[64] =
		{
			0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
			0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
			0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
			0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
			0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
			0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
			0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
			0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
			0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
			0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
			0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
			0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
			0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
			0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
			0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
			0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
		};
		static const int S[64] =
		{
			7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
			5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
			4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
			6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
		};
		unsigned long m[16];
		unsigned long a = md.a, b = md.b, c = md.c, d = md.d;

		for (int i = 0; i < 16; i++)
			m[i] = DwordAt(p + i * 4);

		for (int i = 0; i < 64; i++)
		{
			unsigned long f;
			int g;

			if (i < 16) { f = (b & c) | (~b & d); g = i; }
			else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
			else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
			else { f = c ^ (b | ~d); g = (7 * i) & 15; }

			f = (f + a + K[i] + m[g]) & 0xFFFFFFFFUL;
			a = d;
			d = c;
			c = b;
			b = (b + Md5Rotate(f, S[i])) & 0xFFFFFFFFUL;
		}

		md.a = (md.a + a) & 0xFFFFFFFFUL;
		md.b = (md.b + b) & 0xFFFFFFFFUL;
		md.c = (md.c + c) & 0xFFFFFFFFUL;
		md.d = (md.d + d) & 0xFFFFFFFFUL;
	}

	void Md5Hex(unsigned long word, char* out)
	{
		static const char* digits = "0123456789abcdef";

		for (int i = 0; i < 4; i++)
		{
			const unsigned char byte = (unsigned char)(word >> (i * 8));
			out[i * 2] = digits[byte >> 4];
			out[i * 2 + 1] = digits[byte & 15];
		}
	}
}

void HDFontMd5(const void* data, unsigned long size, char* out33)
{
	Md5 md;
	const unsigned char* p = (const unsigned char*)data;
	unsigned char tail[128];
	unsigned long left = size;
	unsigned long bits;
	unsigned long pad;

	if (!out33)
		return;

	out33[0] = 0;

	if (!p && size)
		return;

	md.a = 0x67452301;
	md.b = 0xefcdab89;
	md.c = 0x98badcfe;
	md.d = 0x10325476;

	while (left >= 64)
	{
		Md5Round(md, p);
		p += 64;
		left -= 64;
	}

	memset(tail, 0, sizeof(tail));
	memcpy(tail, p, left);
	tail[left] = 0x80;

	// 56 bytes of message plus the eight byte length fit in one block; more
	// than that needs a second one.
	pad = (left + 1 <= 56) ? 64 : 128;
	bits = size << 3;
	tail[pad - 8] = (unsigned char)bits;
	tail[pad - 7] = (unsigned char)(bits >> 8);
	tail[pad - 6] = (unsigned char)(bits >> 16);
	tail[pad - 5] = (unsigned char)(bits >> 24);
	// The top word of the bit length: sizes over 512 MB do not occur here,
	// but the field has to be right anyway.
	tail[pad - 4] = (unsigned char)(size >> 29);

	Md5Round(md, tail);

	if (pad == 128)
		Md5Round(md, tail + 64);

	Md5Hex(md.a, out33);
	Md5Hex(md.b, out33 + 8);
	Md5Hex(md.c, out33 + 16);
	Md5Hex(md.d, out33 + 24);
	out33[32] = 0;
}

HDFontCell HDFontCellOf(float u, float v, short w, short h)
{
	HDFontCell cell;

	/* n/255 AND NOT n/256 - measured over all 106 rows of game\chardef.inc,
	 * the largest distance from u * 255 to a whole number is 0.000133, so
	 * this rounding is exact and not a guess. Read as n/256 the rectangles
	 * would sit up to one texel too far left and up, and the templates would
	 * carry a slice of the neighbouring glyph: the 106 cells cover 8 669 ink
	 * texels, which is EVERY ink texel of the page, and they only do that
	 * when they sit where the artist put them. */
	cell.x = (short)floor((double)u * 255.0 + 0.5);
	cell.y = (short)floor((double)v * 255.0 + 0.5);
	cell.w = w;
	cell.h = h;
	return cell;
}

bool HDFontOgDecode(const void* file, unsigned long size, unsigned long* out,
	const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	const unsigned char* p = (const unsigned char*)file;

	if (!why)
		why = &dummy;

	*why = nothing;

	if (!p || !out)
	{
		*why = "nothing to decode";
		return false;
	}

	// 4 bytes "DDS ", then a 124 byte header, then the pixels. FONT0_OG.DDS
	// is 262 272 bytes = 128 + 256 * 256 * 4, measured on the installed
	// bundle.
	if (size < 128 + (unsigned long)HD_FONT_SIDE * HD_FONT_SIDE * 4)
	{
		*why = "shorter than a 256x256 DDS";
		return false;
	}

	if (DwordAt(p) != 0x20534444UL || DwordAt(p + 4) != 124)
	{
		*why = "not a DDS";
		return false;
	}

	if (DwordAt(p + 16) != HD_FONT_SIDE || DwordAt(p + 12) != HD_FONT_SIDE)
	{
		*why = "not 256 x 256";
		return false;
	}

	/* DDPF_ALPHAPIXELS | DDPF_RGB = 0x41, 32 bits a texel, and the four
	 * channel masks of B8G8R8A8 - which is the byte order B, G, R, A and
	 * therefore, read as a little endian dword, exactly the 0xAARRGGBB this
	 * engine uses everywhere. Measured on FONT0_OG.DDS and FONT1_OG.DDS:
	 * pfflags 0x41, 32 bits, R 00FF0000 G 0000FF00 B 000000FF A FF000000,
	 * pitch 0x400, one mip level. Nothing else is accepted, because nothing
	 * else is what the remaster ships and a wrong channel order would show
	 * up as a picture, not as an error. */
	if (DwordAt(p + 80) != 0x41 || DwordAt(p + 88) != 32 ||
		DwordAt(p + 92) != 0x00FF0000UL || DwordAt(p + 96) != 0x0000FF00UL ||
		DwordAt(p + 100) != 0x000000FFUL || DwordAt(p + 104) != 0xFF000000UL)
	{
		*why = "not an uncompressed B8G8R8A8 DDS";
		return false;
	}

	memcpy(out, p + 128, (size_t)HD_FONT_SIDE * HD_FONT_SIDE * 4);
	return true;
}

int HDFontMatchAll(const unsigned long* atlas, const unsigned long* page,
	const HDFontCell* cells, int count, float accept, HDFontHit* hits)
{
	unsigned char* atlasInk;
	unsigned char* pageInk;
	Sat* sat;
	int found = 0;

	if (!atlas || !page || !cells || !hits || count <= 0)
		return 0;

	// Three big buffers, and the summed area table alone is 264 KB - too
	// much for the stack of the load thread.
	atlasInk = (unsigned char*)malloc(HD_FONT_SIDE * HD_FONT_SIDE);
	pageInk = (unsigned char*)malloc(HD_FONT_SIDE * HD_FONT_SIDE);
	sat = (Sat*)malloc(sizeof(Sat));

	if (!atlasInk || !pageInk || !sat)
	{
		free(atlasInk);
		free(pageInk);
		free(sat);
		return 0;
	}

	InkMask(atlas, atlasInk);
	InkMask(page, pageInk);
	sat->build(atlasInk);

	for (int i = 0; i < count; i++)
	{
		const HDFontCell& cell = cells[i];
		long best = -1, bestX = 0, bestY = 0;
		long ink = 0;
		float bestScore = 0.0f;

		hits[i].x = 0;
		hits[i].y = 0;
		hits[i].score = 0.0f;
		hits[i].ok = false;

		if (cell.w <= 0 || cell.h <= 0 ||
			cell.x < 0 || cell.y < 0 ||
			cell.x + cell.w > HD_FONT_SIDE || cell.y + cell.h > HD_FONT_SIDE)
			continue;

		for (long y = 0; y < cell.h; y++)
			for (long x = 0; x < cell.w; x++)
				ink += pageInk[(cell.y + y) * HD_FONT_SIDE + cell.x + x];

		if (!ink)
			continue;

		{
			// TROne's prefilter window, kept in whole texels so that a
			// small glyph is not squeezed to nothing by the rounding.
			const long low = (long)(ink * 85 / 100);
			const long high = (long)(ink * 120 / 100) + 1;

			for (long ay = 0; ay + cell.h <= HD_FONT_SIDE && bestScore < 0.999f; ay++)
			{
				for (long ax = 0; ax + cell.w <= HD_FONT_SIDE; ax++)
				{
					long here = sat->window(ax, ay, cell.w, cell.h);
					long inter = 0;
					float score;

					if (here < low || here > high)
						continue;

					for (long y = 0; y < cell.h; y++)
					{
						const unsigned char* a = &atlasInk[(ay + y) * HD_FONT_SIDE + ax];
						const unsigned char* t = &pageInk[(cell.y + y) * HD_FONT_SIDE + cell.x];

						for (long x = 0; x < cell.w; x++)
							inter += (a[x] & t[x]);
					}

					// union = |template| + |window| - |intersection|
					score = (float)inter / (float)(ink + here - inter);

					if (score > bestScore)
					{
						bestScore = score;
						bestX = ax;
						bestY = ay;
						best = inter;

						if (bestScore >= 0.999f)
							break;		// nothing can beat a whole glyph
					}
				}
			}
		}

		if (best < 0)
			continue;

		hits[i].x = (short)bestX;
		hits[i].y = (short)bestY;
		hits[i].score = bestScore;
		hits[i].ok = bestScore >= accept;

		if (hits[i].ok)
			found++;
	}

	free(atlasInk);
	free(pageInk);
	free(sat);
	return found;
}

void HDFontMargins(const unsigned long* hd, long side, const unsigned long* page,
	const HDFontCell* cells, const HDFontHit* hits, int count, float maxMargin,
	float* out)
{
	const long steps = (long)(maxMargin * HD_FONT_SCALE + 0.5f);

	if (!out || count <= 0)
		return;

	for (int i = 0; i < count; i++)
		out[i] = 0.0f;

	if (!hd || side < HD_FONT_SIDE * HD_FONT_SCALE || !page || !cells || !hits ||
		steps <= 0)
		return;

	for (int i = 0; i < count; i++)
	{
		/* WHAT IS NOT THIS LETTER, on the picture that is actually drawn.
		 *
		 * Not the ink but the WHITE ink: alpha alone runs the letter and its
		 * drop shadow together, and a flood fill over that walks from one
		 * mark straight into the next one - measured, it does exactly that
		 * between the two ticks of the double quote, and the margin then
		 * comes back "there is nothing else here" for the very glyphs that
		 * need the most care. The white body of a letter is separated from
		 * the white body of the next one by the dark shadow between them, so
		 * a fill over white alone keeps them apart.
		 *
		 * Shadows may therefore overlap, and that is right: two shadows over
		 * each other are a shadow, two letters over each other are a fault.
		 *
		 * It cannot be measured on the _OG picture either, tempting as that
		 * is (there, ink outside the matched rectangle is somebody else's by
		 * construction). The 2048 is a NEW drawing and its letters are bigger
		 * than the small ones times eight - measured, up to three classic
		 * texels bigger - so the room the small picture has is not the room
		 * the big one has. */
		const long pad = steps + HD_FONT_SCALE;
		const long rx0 = (long)hits[i].x * HD_FONT_SCALE;
		const long ry0 = (long)hits[i].y * HD_FONT_SCALE;
		const long rx1 = (long)(hits[i].x + cells[i].w) * HD_FONT_SCALE;
		const long ry1 = (long)(hits[i].y + cells[i].h) * HD_FONT_SCALE;
		long x0 = rx0 - pad, y0 = ry0 - pad, x1 = rx1 + pad, y1 = ry1 + pad;
		long ww, wh;
		unsigned char* seen;
		long* stack;
		long* sat;
		long top = 0;
		double driftX, driftY;

		if (!hits[i].ok || cells[i].w <= 0 || cells[i].h <= 0)
			continue;

		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > side) x1 = side;
		if (y1 > side) y1 = side;

		ww = x1 - x0;
		wh = y1 - y0;

		if (ww <= 0 || wh <= 0)
			continue;

		seen = (unsigned char*)calloc((size_t)ww * wh, 1);
		stack = (long*)malloc((size_t)ww * wh * sizeof(long));
		sat = (long*)malloc((size_t)(ww + 1) * (wh + 1) * sizeof(long));

		if (!seen || !stack || !sat)
		{
			free(seen);
			free(stack);
			free(sat);
			continue;
		}

		/* THE SEEDS, and they are NOT simply "white inside the rectangle".
		 * The big picture's letters are larger than the small picture's times
		 * eight, so the neighbour can reach a texel or two INTO the matched
		 * rectangle - measured, the second tick of the double quote does
		 * exactly that - and seeding the whole rectangle would then flood the
		 * neighbour as well and call it part of this letter.
		 *
		 * So the seeds come from the CLASSIC page: a texel of the glyph's own
		 * rectangle that carries ink there is this letter beyond doubt - that
		 * is what the match established - and the eight by eight block under
		 * it on the atlas is this letter's white. */
		for (long dy = 0; dy < cells[i].h; dy++)
		{
			for (long dx = 0; dx < cells[i].w; dx++)
			{
				const long cxp = cells[i].x + dx, cyp = cells[i].y + dy;

				if (cxp < 0 || cyp < 0 || cxp >= HD_FONT_SIDE || cyp >= HD_FONT_SIDE)
					continue;

				if ((page[cyp * HD_FONT_SIDE + cxp] >> 24) < HD_FONT_INK)
					continue;

				for (long by = 0; by < HD_FONT_SCALE; by++)
				{
					for (long bx = 0; bx < HD_FONT_SCALE; bx++)
					{
						const long x = rx0 + dx * HD_FONT_SCALE + bx;
						const long y = ry0 + dy * HD_FONT_SCALE + by;

						if (x < x0 || y < y0 || x >= x1 || y >= y1)
							continue;

						if (!HDFontIsWhite(hd[y * side + x]))
							continue;

						if (seen[(y - y0) * ww + (x - x0)])
							continue;

						seen[(y - y0) * ww + (x - x0)] = 1;
						stack[top++] = (y - y0) * ww + (x - x0);
					}
				}
			}
		}

		while (top > 0)
		{
			const long at = stack[--top];
			const long lx = at % ww, ly = at / ww;

			for (int dy = -1; dy <= 1; dy++)
			{
				for (int dx = -1; dx <= 1; dx++)
				{
					const long nx = lx + dx, ny = ly + dy;

					if (nx < 0 || ny < 0 || nx >= ww || ny >= wh)
						continue;

					if (seen[ny * ww + nx])
						continue;

					if (!HDFontIsWhite(hd[(y0 + ny) * side + x0 + nx]))
						continue;

					seen[ny * ww + nx] = 1;
					stack[top++] = ny * ww + nx;
				}
			}
		}

		// Everything else that is white belongs to another letter, and a
		// summed area table over it answers "is any of it in this window" in
		// three additions instead of a scan per margin.
		{
			const long stride = ww + 1;

			for (long x = 0; x <= ww; x++)
				sat[x] = 0;

			for (long y = 0; y < wh; y++)
			{
				sat[(y + 1) * stride] = 0;

				for (long x = 0; x < ww; x++)
				{
					const long foreign = (!seen[y * ww + x] &&
						HDFontIsWhite(hd[(y0 + y) * side + x0 + x])) ? 1 : 0;

					sat[(y + 1) * stride + x + 1] = foreign +
						sat[y * stride + x + 1] + sat[(y + 1) * stride + x] -
						sat[y * stride + x];
				}
			}

			// The drift of the classic u: stored as n/255 and read as n/256,
			// so the window sits up to a whole classic texel to the right of
			// the rectangle. That is the window the engine samples, so that
			// is the window this asks about.
			driftX = (double)cells[i].x / 255.0 * HD_FONT_SCALE;
			driftY = (double)cells[i].y / 255.0 * HD_FONT_SCALE;

			for (long m = steps; m >= 0; m--)
			{
				long wx0 = (long)floor((double)rx0 + driftX + 4.0 - (double)m);
				long wy0 = (long)floor((double)ry0 + driftY + 4.0 - (double)m);
				long wx1 = (long)ceil((double)rx1 + driftX - 4.0 + (double)m);
				long wy1 = (long)ceil((double)ry1 + driftY - 4.0 + (double)m);
				long a, b, c, d;

				if (wx0 < x0) wx0 = x0;
				if (wy0 < y0) wy0 = y0;
				if (wx1 > x1) wx1 = x1;
				if (wy1 > y1) wy1 = y1;

				if (wx1 <= wx0 || wy1 <= wy0)
					continue;

				a = sat[(wy1 - y0) * stride + (wx1 - x0)];
				b = sat[(wy0 - y0) * stride + (wx1 - x0)];
				c = sat[(wy1 - y0) * stride + (wx0 - x0)];
				d = sat[(wy0 - y0) * stride + (wx0 - x0)];

				if (a - b - c + d == 0)
				{
					out[i] = (float)m / (float)HD_FONT_SCALE;
					break;
				}
			}
		}

		free(seen);
		free(stack);
		free(sat);
	}
}

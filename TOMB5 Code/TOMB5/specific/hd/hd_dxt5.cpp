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
#include "hd_dxt5.h"

#include <cmath>
#include <cstring>

namespace
{
	inline int clamp255(int v)
	{
		return v < 0 ? 0 : (v > 255 ? 255 : v);
	}

	inline unsigned short to565(int r, int g, int b)
	{
		return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	inline void from565(unsigned short c, int* r, int* g, int* b)
	{
		// The expansion the hardware does: the top bits are repeated into the
		// bottom ones, so 0b11111 becomes 255 and not 248. Encoding against
		// any other expansion measures a picture the card will never show.
		const int r5 = (c >> 11) & 31;
		const int g6 = (c >> 5) & 63;
		const int b5 = c & 31;

		*r = (r5 << 3) | (r5 >> 2);
		*g = (g6 << 2) | (g6 >> 4);
		*b = (b5 << 3) | (b5 >> 2);
	}

	/* ONE 4x4 COLOUR BLOCK, range fit.
	 *
	 * The bounding box of the sixteen colours, pulled in by a sixteenth of
	 * its own span at each end, is the line the four palette entries sit on.
	 * The inset is the one refinement that is worth its four lines: without
	 * it the two extreme texels own the endpoints and the fourteen in between
	 * are quantised against a line that is too long, which is exactly the
	 * banding this format is blamed for.
	 *
	 * The index of each texel is then the nearest of the four palette
	 * entries, measured in the colour the CARD will produce (from565 above),
	 * not in the 8 bit colour we started from. */
	void encode_colour(const unsigned long* block, unsigned char* out)
	{
		int lo[3] = { 255, 255, 255 };
		int hi[3] = { 0, 0, 0 };
		int i, c;

		for (i = 0; i < 16; i++)
		{
			const unsigned long p = block[i];
			const int rgb[3] =
			{
				(int)((p >> 16) & 0xFF),
				(int)((p >> 8) & 0xFF),
				(int)(p & 0xFF)
			};

			for (c = 0; c < 3; c++)
			{
				if (rgb[c] < lo[c]) lo[c] = rgb[c];
				if (rgb[c] > hi[c]) hi[c] = rgb[c];
			}
		}

		for (c = 0; c < 3; c++)
		{
			const int inset = (hi[c] - lo[c]) >> 4;

			lo[c] = clamp255(lo[c] + inset);
			hi[c] = clamp255(hi[c] - inset);

			if (lo[c] > hi[c])
			{
				const int mid = (lo[c] + hi[c]) >> 1;
				lo[c] = hi[c] = mid;
			}
		}

		unsigned short c0 = to565(hi[0], hi[1], hi[2]);
		unsigned short c1 = to565(lo[0], lo[1], lo[2]);

		/* BC3's colour block is always read as the four colour mode, whatever
		 * the order of the two endpoints - the three colour mode with its
		 * transparent fourth entry belongs to BC1 alone. Keeping c0 above c1
		 * anyway costs nothing and means the block reads the same under every
		 * description of the format there is. */
		bool swapped = false;

		if (c0 < c1)
		{
			const unsigned short t = c0;
			c0 = c1;
			c1 = t;
			swapped = true;
		}

		int pal[4][3];

		from565(c0, &pal[0][0], &pal[0][1], &pal[0][2]);
		from565(c1, &pal[1][0], &pal[1][1], &pal[1][2]);

		for (c = 0; c < 3; c++)
		{
			pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
			pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
		}

		out[0] = (unsigned char)(c0 & 0xFF);
		out[1] = (unsigned char)(c0 >> 8);
		out[2] = (unsigned char)(c1 & 0xFF);
		out[3] = (unsigned char)(c1 >> 8);
		out[4] = out[5] = out[6] = out[7] = 0;

		if (c0 == c1)
			return;		// one colour, every index 0 - nothing left to write

		(void)swapped;

		for (i = 0; i < 16; i++)
		{
			const unsigned long p = block[i];
			const int r = (int)((p >> 16) & 0xFF);
			const int g = (int)((p >> 8) & 0xFF);
			const int b = (int)(p & 0xFF);
			int best = 0;
			long bestErr = -1;
			int k;

			for (k = 0; k < 4; k++)
			{
				const long dr = r - pal[k][0];
				const long dg = g - pal[k][1];
				const long db = b - pal[k][2];

				/* The classic 2:4:1 weighting of the three channels - the eye
				 * sees green error four times as loudly as blue error, and an
				 * unweighted distance spends the palette on blue. */
				const long err = 2 * dr * dr + 4 * dg * dg + db * db;

				if (bestErr < 0 || err < bestErr)
				{
					bestErr = err;
					best = k;
				}
			}

			out[4 + (i >> 2)] |= (unsigned char)(best << ((i & 3) * 2));
		}
	}

	/* ONE 4x4 ALWAYS-EIGHT-VALUE ALPHA BLOCK. a0 above a1 selects the eight
	 * value interpolation; the six value mode with its hard 0 and 255 buys
	 * nothing on a page whose alpha is a gloss mask or a cut-out edge. */
	void encode_alpha(const unsigned long* block, unsigned char* out)
	{
		int lo = 255, hi = 0;
		int i, k;

		for (i = 0; i < 16; i++)
		{
			const int a = (int)((block[i] >> 24) & 0xFF);

			if (a < lo) lo = a;
			if (a > hi) hi = a;
		}

		out[0] = (unsigned char)hi;
		out[1] = (unsigned char)lo;
		out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = 0;

		if (hi == lo)
			return;

		int pal[8];

		pal[0] = hi;
		pal[1] = lo;

		for (k = 2; k < 8; k++)
			pal[k] = ((8 - k) * hi + (k - 1) * lo) / 7;

		// The forty-eight index bits, three per texel, written low bit first
		// across the six bytes - the order the format stores them in.
		unsigned long long bits = 0;

		for (i = 0; i < 16; i++)
		{
			const int a = (int)((block[i] >> 24) & 0xFF);
			int best = 0;
			int bestErr = -1;

			for (k = 0; k < 8; k++)
			{
				const int d = a - pal[k];
				const int err = d * d;

				if (bestErr < 0 || err < bestErr)
				{
					bestErr = err;
					best = k;
				}
			}

			bits |= (unsigned long long)best << (i * 3);
		}

		for (i = 0; i < 6; i++)
			out[2 + i] = (unsigned char)((bits >> (i * 8)) & 0xFF);
	}
}

unsigned long DXT5Size(long width, long height)
{
	if (width <= 0 || height <= 0)
		return 0;

	return (unsigned long)((width + 3) / 4) * (unsigned long)((height + 3) / 4) * 16UL;
}

void DXT5Encode(const unsigned long* pixels, long width, long height,
	unsigned char* out)
{
	if (!pixels || !out || width <= 0 || height <= 0)
		return;

	for (long by = 0; by < height; by += 4)
		for (long bx = 0; bx < width; bx += 4)
		{
			unsigned long block[16];

			for (int i = 0; i < 16; i++)
			{
				long x = bx + (i & 3);
				long y = by + (i >> 2);

				if (x >= width) x = width - 1;
				if (y >= height) y = height - 1;

				block[i] = pixels[y * width + x];
			}

			encode_alpha(block, out);
			encode_colour(block, out + 8);
			out += 16;
		}
}

void DXT5Decode(const unsigned char* blocks, long width, long height,
	unsigned long* pixels)
{
	if (!blocks || !pixels || width <= 0 || height <= 0)
		return;

	for (long by = 0; by < height; by += 4)
		for (long bx = 0; bx < width; bx += 4)
		{
			const int a0 = blocks[0];
			const int a1 = blocks[1];
			int alpha[8];
			int k;

			alpha[0] = a0;
			alpha[1] = a1;

			if (a0 > a1)
				for (k = 2; k < 8; k++)
					alpha[k] = ((8 - k) * a0 + (k - 1) * a1) / 7;
			else
			{
				for (k = 2; k < 6; k++)
					alpha[k] = ((6 - k) * a0 + (k - 1) * a1) / 5;

				alpha[6] = 0;
				alpha[7] = 255;
			}

			unsigned long long abits = 0;

			for (k = 0; k < 6; k++)
				abits |= (unsigned long long)blocks[2 + k] << (k * 8);

			const unsigned short c0 = (unsigned short)(blocks[8] | (blocks[9] << 8));
			const unsigned short c1 = (unsigned short)(blocks[10] | (blocks[11] << 8));
			int pal[4][3];

			from565(c0, &pal[0][0], &pal[0][1], &pal[0][2]);
			from565(c1, &pal[1][0], &pal[1][1], &pal[1][2]);

			for (int c = 0; c < 3; c++)
			{
				pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
				pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
			}

			const unsigned long cbits = (unsigned long)blocks[12] |
				((unsigned long)blocks[13] << 8) |
				((unsigned long)blocks[14] << 16) |
				((unsigned long)blocks[15] << 24);

			for (int i = 0; i < 16; i++)
			{
				const long x = bx + (i & 3);
				const long y = by + (i >> 2);

				if (x >= width || y >= height)
					continue;

				const int ai = (int)((abits >> (i * 3)) & 7);
				const int ci = (int)((cbits >> (i * 2)) & 3);

				pixels[y * width + x] =
					((unsigned long)alpha[ai] << 24) |
					((unsigned long)pal[ci][0] << 16) |
					((unsigned long)pal[ci][1] << 8) |
					(unsigned long)pal[ci][2];
			}

			blocks += 16;
		}
}

double DXT5Psnr(const unsigned long* a, const unsigned long* b, long count)
{
	if (!a || !b || count <= 0)
		return 0.0;

	double sum = 0.0;

	for (long i = 0; i < count; i++)
	{
		const unsigned long p = a[i];
		const unsigned long q = b[i];

		for (int shift = 0; shift < 32; shift += 8)
		{
			const double d = (double)(int)((p >> shift) & 0xFF) -
				(double)(int)((q >> shift) & 0xFF);
			sum += d * d;
		}
	}

	const double mse = sum / ((double)count * 4.0);

	if (mse <= 0.0)
		return 999.0;

	return 10.0 * std::log10(255.0 * 255.0 / mse);
}

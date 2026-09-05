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
#include <stdlib.h>
#include <string.h>
#include "bc7.h"

// BC7 / BPTC unorm, decode side only.
//
// A block is 16 bytes and covers 4x4 pixels. Its bits are read from the least
// significant bit of byte 0 upwards. The first field is the mode, written in
// unary: as many zeroes as the mode number, then a one. A block whose first
// eight bits are all zero is invalid and decodes to transparent black.
//
// After the mode come, in this order and only where the mode has them:
// partition, rotation, index selection, then all red endpoint values, all
// green, all blue, all alpha, then the P bits, then the primary indices and
// the secondary indices. Endpoints are stored per subset pair, so a two
// subset mode has four of each colour component.
//
// Two things are easy to get wrong and are worth naming. First, one index per
// subset is stored one bit short: its top bit is known to be zero, because the
// encoder is free to order the two endpoints of a subset and always picks the
// order that makes the anchor's index small. The anchor of subset 0 is always
// pixel 0; for the other subsets it is looked up per partition. Second, modes
// 4 and 5 keep two independent index arrays, one for colour and one for
// alpha, and in mode 4 a single bit says which of the two is which.
//
// The partition and anchor tables below are the format's own. They are not
// taken from anyone's implementation, and they are checked against each other
// at run time by BC7SelfTest(): every partition must start in subset 0, must
// really use all of its subsets, and the anchor listed for subset n must be a
// pixel that this partition actually assigns to subset n. Those constraints
// are tight enough that a mistyped row does not survive them. See
// Preparation\Tools\Diagnose\DdsProbe.cpp for the second, empirical check.

struct BC7MODE
{
	int ns;		// subsets
	int pb;		// partition bits
	int rb;		// rotation bits
	int isb;	// index selection bit
	int cb;		// colour bits per endpoint component
	int ab;		// alpha bits per endpoint component, 0 = opaque
	int epb;	// one P bit per endpoint
	int spb;	// one P bit per subset, shared by its two endpoints
	int ib;		// primary index bits
	int ib2;	// secondary index bits, 0 = none
};

static const BC7MODE bc7_mode[8] =
{
	{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
	{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
	{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
	{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
	{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
	{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
	{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
	{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 }
};

static const unsigned char bc7_part2[64][16] =
{
	{0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1}, {0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1},
	{0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1}, {0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
	{0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1}, {0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1},
	{0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1},
	{0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1}, {0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1},
	{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1},
	{0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1},
	{0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1}, {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1},
	{0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1}, {0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0}, {0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0},
	{0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0}, {0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0},
	{0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0}, {0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1},
	{0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0}, {0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0},
	{0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0}, {0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0},
	{0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0}, {0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0},
	{0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0}, {0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0},
	{0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1}, {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
	{0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0}, {0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0},
	{0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0}, {0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0},
	{0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1}, {0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1},
	{0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0}, {0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0},
	{0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0}, {0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0},
	{0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0}, {0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1},
	{0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1}, {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
	{0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0}, {0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0},
	{0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0}, {0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0},
	{0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1}, {0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1},
	{0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0}, {0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0},
	{0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1}, {0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1},
	{0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1}, {0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1},
	{0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1}, {0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0},
	{0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0}, {0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1}
};

static const unsigned char bc7_part3[64][16] =
{
	{0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2}, {0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1},
	{0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1}, {0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1},
	{0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2}, {0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2},
	{0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1}, {0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1},
	{0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2}, {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2},
	{0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2}, {0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2},
	{0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2}, {0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2},
	{0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2}, {0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0},
	{0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2}, {0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0},
	{0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2}, {0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1},
	{0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2}, {0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1},
	{0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2}, {0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0},
	{0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0}, {0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2},
	{0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0}, {0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1},
	{0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2}, {0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2},
	{0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1}, {0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1},
	{0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2}, {0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1},
	{0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2}, {0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0},
	{0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0}, {0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0},
	{0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0}, {0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1},
	{0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1}, {0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2},
	{0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1}, {0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2},
	{0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1}, {0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1},
	{0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1}, {0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1},
	{0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2}, {0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1},
	{0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2}, {0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2},
	{0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2}, {0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2},
	{0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2}, {0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2},
	{0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2}, {0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2},
	{0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2}, {0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2},
	{0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1}, {0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2},
	{0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2}, {0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0}
};

// Anchor of subset 1 in a two subset partition.
static const unsigned char bc7_anchor2[64] =
{
	15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15,
	15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2,
	15,15, 6, 8, 2, 8,15,15,  2, 8, 2, 2, 2,15,15, 6,
	 6, 2, 6, 8,15,15, 2, 2, 15,15,15,15,15, 2, 2,15
};

// Anchors of subsets 1 and 2 in a three subset partition.
static const unsigned char bc7_anchor3_1[64] =
{
	 3, 3,15,15, 8, 3,15,15,  8, 8, 6, 6, 6, 5, 3, 3,
	 3, 3, 8,15, 3, 3, 6,10,  5, 8, 8, 6, 8, 5,15,15,
	 8,15, 3, 5, 6,10, 8,15, 15, 3,15, 5,15,15,15,15,
	 3,15, 5, 5, 5, 8, 5,10,  5,10, 8,13,15,12, 3, 3
};

static const unsigned char bc7_anchor3_2[64] =
{
	15, 8, 8, 3,15,15, 3, 8, 15,15,15,15,15,15,15, 8,
	15, 8,15, 3,15, 8,15, 8,  3,15, 6,10,15,15,10, 8,
	15, 3,15,10,10, 8, 9,10,  6,15, 8,15, 3, 6, 6, 8,
	15, 3,15,15,15,15,15,15, 15,15,15,15, 3,15,15, 8
};

static const unsigned char bc7_weight2[4] = { 0, 21, 43, 64 };
static const unsigned char bc7_weight3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const unsigned char bc7_weight4[16] =
{
	0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64
};

static const unsigned char* bc7_weights(int bits)
{
	if (bits == 2)
		return bc7_weight2;

	if (bits == 3)
		return bc7_weight3;

	return bc7_weight4;
}

struct BC7BITS
{
	const unsigned char* p;
	int at;
};

static unsigned long BC7Read(BC7BITS* s, int n)
{
	unsigned long v;
	int i;

	v = 0;

	for (i = 0; i < n; i++, s->at++)
		v |= (unsigned long)((s->p[s->at >> 3] >> (s->at & 7)) & 1) << i;

	return v;
}

// n bits at the top, the missing low bits filled from the top again - the
// format's own rule, and the reason a 5 bit 31 becomes 255 and not 248.
static unsigned char BC7Expand(unsigned long v, int n)
{
	if (n >= 8)
		return (unsigned char)v;

	return (unsigned char)((v << (8 - n)) | (v >> (2 * n - 8)));
}

static unsigned char BC7Lerp(unsigned char e0, unsigned char e1, unsigned char w)
{
	return (unsigned char)((((64 - w) * e0) + (w * e1) + 32) >> 6);
}

void BC7DecodeBlock(const unsigned char* src, unsigned long* dst)
{
	BC7BITS bits;
	const BC7MODE* m;
	const unsigned char* part;
	const unsigned char* cw;
	const unsigned char* aw;
	unsigned char ep[6][4];		// up to 3 subsets x 2 endpoints, RGBA
	unsigned char pbit[6];
	unsigned char idx1[16], idx2[16];
	unsigned char anchor[3];
	int mode, partition, rotation, idxmode, eps, cbits, abits;
	int i, s, c, n, ci, ai, cbit, abit;
	unsigned char t;

	bits.p = src;
	bits.at = 0;

	for (mode = 0; mode < 8; mode++)
		if (BC7Read(&bits, 1))
			break;

	if (mode == 8)		// no set bit in the first eight - an invalid block
	{
		for (i = 0; i < 16; i++)
			dst[i] = 0;

		return;
	}

	m = &bc7_mode[mode];
	eps = 2 * m->ns;
	partition = m->pb ? (int)BC7Read(&bits, m->pb) : 0;
	rotation = m->rb ? (int)BC7Read(&bits, m->rb) : 0;
	idxmode = m->isb ? (int)BC7Read(&bits, m->isb) : 0;

	for (c = 0; c < 3; c++)
		for (i = 0; i < eps; i++)
			ep[i][c] = (unsigned char)BC7Read(&bits, m->cb);

	for (i = 0; i < eps; i++)
		ep[i][3] = m->ab ? (unsigned char)BC7Read(&bits, m->ab) : 0;

	for (i = 0; i < eps; i++)
		pbit[i] = 0;

	if (m->epb)
		for (i = 0; i < eps; i++)
			pbit[i] = (unsigned char)BC7Read(&bits, 1);
	else if (m->spb)
		for (s = 0; s < m->ns; s++)
		{
			t = (unsigned char)BC7Read(&bits, 1);
			pbit[2 * s] = t;
			pbit[2 * s + 1] = t;
		}

	cbits = m->cb + ((m->epb || m->spb) ? 1 : 0);
	abits = m->ab ? m->ab + ((m->epb || m->spb) ? 1 : 0) : 0;

	for (i = 0; i < eps; i++)
	{
		for (c = 0; c < 3; c++)
		{
			unsigned long v = ep[i][c];

			if (m->epb || m->spb)
				v = (v << 1) | pbit[i];

			ep[i][c] = BC7Expand(v, cbits);
		}

		if (m->ab)
		{
			unsigned long v = ep[i][3];

			if (m->epb || m->spb)
				v = (v << 1) | pbit[i];

			ep[i][3] = BC7Expand(v, abits);
		}
		else
			ep[i][3] = 255;
	}

	// Which pixels carry a short index. Subset 0 always anchors on pixel 0.
	anchor[0] = 0;
	anchor[1] = 0;
	anchor[2] = 0;
	part = 0;

	if (m->ns == 2)
	{
		part = bc7_part2[partition];
		anchor[1] = bc7_anchor2[partition];
	}
	else if (m->ns == 3)
	{
		part = bc7_part3[partition];
		anchor[1] = bc7_anchor3_1[partition];
		anchor[2] = bc7_anchor3_2[partition];
	}

	for (i = 0; i < 16; i++)
	{
		n = m->ib;

		for (s = 1; s < m->ns; s++)
			if (i == anchor[s])
				n--;

		if (!i)
			n--;

		idx1[i] = (unsigned char)BC7Read(&bits, n);
	}

	if (m->ib2)
		for (i = 0; i < 16; i++)
		{
			n = m->ib2;

			for (s = 1; s < m->ns; s++)
				if (i == anchor[s])
					n--;

			if (!i)
				n--;

			idx2[i] = (unsigned char)BC7Read(&bits, n);
		}

	// Modes 4 and 5 split colour and alpha across the two index arrays; in
	// mode 4 the index selection bit says which array drives which.
	if (!m->ib2)
	{
		cbit = m->ib;
		abit = m->ib;
	}
	else if (!idxmode)
	{
		cbit = m->ib;
		abit = m->ib2;
	}
	else
	{
		cbit = m->ib2;
		abit = m->ib;
	}

	cw = bc7_weights(cbit);
	aw = bc7_weights(abit);

	for (i = 0; i < 16; i++)
	{
		unsigned char px[4];

		s = part ? part[i] : 0;

		if (!m->ib2)
		{
			ci = idx1[i];
			ai = idx1[i];
		}
		else if (!idxmode)
		{
			ci = idx1[i];
			ai = idx2[i];
		}
		else
		{
			ci = idx2[i];
			ai = idx1[i];
		}

		for (c = 0; c < 3; c++)
			px[c] = BC7Lerp(ep[2 * s][c], ep[2 * s + 1][c], cw[ci]);

		px[3] = m->ab ? BC7Lerp(ep[2 * s][3], ep[2 * s + 1][3], aw[ai]) : 255;

		if (rotation)		// rotation n swaps alpha with component n-1
		{
			t = px[3];
			px[3] = px[rotation - 1];
			px[rotation - 1] = t;
		}

		dst[i] = ((unsigned long)px[3] << 24) | ((unsigned long)px[0] << 16) |
			((unsigned long)px[1] << 8) | (unsigned long)px[2];
	}
}

static unsigned long BC7DwordAt(const unsigned char* p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
		((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

#define DDS_MAGIC	0x20534444UL	// "DDS "
#define DDS_FOURCC	0x30315844UL	// "DX10"
#define DXGI_BC7_UNORM		98
#define DXGI_BC7_UNORM_SRGB	99

unsigned long* BC7DecodeDDS(const void* file, unsigned long size, long maxSide,
	long* width, long* height, const char** why)
{
	return BC7DecodeDDSMip(file, size, maxSide, 0, width, height, why);
}

long BC7DDSMipCount(const void* file, unsigned long size)
{
	const unsigned char* p;
	unsigned long levels;

	if (size < 148)
		return 0;

	p = (const unsigned char*)file;

	if (BC7DwordAt(p) != DDS_MAGIC || BC7DwordAt(p + 4) != 124)
		return 0;

	// DDSD_MIPMAPCOUNT is bit 17 of the header flags at +8. A file that does
	// not set it carries the one picture, whatever the field at +28 says -
	// which is 0 in a good many DDS writers' output.
	levels = (BC7DwordAt(p + 8) & 0x20000UL) ? BC7DwordAt(p + 28) : 1;

	if (!levels)
		levels = 1;

	return (long)levels;
}

// The mip chain of a BC7 DDS is stored one level after the other, biggest
// first, each level ceil(w/4) x ceil(h/4) blocks of 16 bytes. The remaster's
// pages are 512x512 with six levels (512, 256, 128, 64, 32, 16), which is
// why the sixteenth of a page at the end still divides evenly into blocks.
unsigned long* BC7DecodeDDSMip(const void* file, unsigned long size, long maxSide,
	long mip, long* width, long* height, const char** why)
{
	const unsigned char* p;
	unsigned long* out;
	unsigned long block[16];
	unsigned long skip;
	long w, h, bx, by, x, y, i, level, levels, mw, mh;

	*why = "";
	*width = 0;
	*height = 0;
	p = (const unsigned char*)file;

	if (size < 148)
	{
		*why = "shorter than a DX10 DDS header";
		return 0;
	}

	if (BC7DwordAt(p) != DDS_MAGIC || BC7DwordAt(p + 4) != 124)
	{
		*why = "not a DDS";
		return 0;
	}

	if (BC7DwordAt(p + 80) != 4 || BC7DwordAt(p + 84) != DDS_FOURCC)
	{
		*why = "no DX10 extension header";
		return 0;
	}

	if (BC7DwordAt(p + 128) != DXGI_BC7_UNORM && BC7DwordAt(p + 128) != DXGI_BC7_UNORM_SRGB)
	{
		*why = "not BC7";
		return 0;
	}

	if (BC7DwordAt(p + 132) != 3 || BC7DwordAt(p + 140) > 1)
	{
		*why = "not a plain 2D texture";
		return 0;
	}

	h = (long)BC7DwordAt(p + 12);
	w = (long)BC7DwordAt(p + 16);

	if (w <= 0 || h <= 0 || (w & 7) || (h & 7))
	{
		*why = "size is not a multiple of 8";
		return 0;
	}

	if (w > maxSide || h > maxSide)
	{
		*why = "larger than the engine's picture texture";
		return 0;
	}

	if (size - 148 < (unsigned long)w * (unsigned long)h)	// BC7 is one byte per pixel
	{
		*why = "truncated - less pixel data than the size promises";
		return 0;
	}

	levels = BC7DDSMipCount(file, size);

	if (mip < 0 || mip >= levels)
	{
		*why = "the file does not carry that mip level";
		return 0;
	}

	// Walk past the levels in front of the wanted one. Their sizes are
	// counted in BLOCKS, not pixels: a 2x2 level is still one whole block.
	skip = 0;
	mw = w;
	mh = h;

	for (level = 0; level < mip; level++)
	{
		skip += (unsigned long)((mw + 3) / 4) * (unsigned long)((mh + 3) / 4) * 16UL;
		mw = mw > 1 ? mw / 2 : 1;
		mh = mh > 1 ? mh / 2 : 1;
	}

	// Only whole 4x4 blocks are decoded, so a level below 4 pixels on a side
	// is refused rather than half written. The remaster stops at 16.
	if (mw < 4 || mh < 4 || (mw & 3) || (mh & 3))
	{
		*why = "mip level is not a whole number of 4x4 blocks";
		return 0;
	}

	if (size - 148 < skip + (unsigned long)mw * (unsigned long)mh)
	{
		*why = "truncated - the mip chain ends before that level";
		return 0;
	}

	w = mw;
	h = mh;

	out = (unsigned long*)malloc((size_t)w * (size_t)h * sizeof(unsigned long));

	if (!out)
	{
		*why = "out of memory";
		return 0;
	}

	p += 148 + skip;

	for (by = 0; by < h; by += 4)
		for (bx = 0; bx < w; bx += 4)
		{
			BC7DecodeBlock(p, block);
			p += 16;

			for (i = 0; i < 16; i++)
			{
				x = bx + (i & 3);
				y = by + (i >> 2);
				out[y * w + x] = block[i];
			}
		}

	*width = w;
	*height = h;
	return out;
}

bool BC7SelfTest(const char** why)
{
	int p, i, s, seen;

	*why = "";

	for (p = 0; p < 64; p++)
	{
		seen = 0;

		for (i = 0; i < 16; i++)
		{
			if (bc7_part2[p][i] > 1)
			{
				*why = "two subset partition holds a subset above 1";
				return false;
			}

			seen |= 1 << bc7_part2[p][i];
		}

		if (bc7_part2[p][0] || seen != 3)
		{
			*why = "two subset partition does not start in subset 0 or misses a subset";
			return false;
		}

		if (bc7_anchor2[p] > 15 || bc7_part2[p][bc7_anchor2[p]] != 1)
		{
			*why = "two subset anchor does not sit in subset 1";
			return false;
		}
	}

	for (p = 0; p < 64; p++)
	{
		seen = 0;

		for (i = 0; i < 16; i++)
		{
			if (bc7_part3[p][i] > 2)
			{
				*why = "three subset partition holds a subset above 2";
				return false;
			}

			seen |= 1 << bc7_part3[p][i];
		}

		if (bc7_part3[p][0] || seen != 7)
		{
			*why = "three subset partition does not start in subset 0 or misses a subset";
			return false;
		}

		if (bc7_anchor3_1[p] > 15 || bc7_part3[p][bc7_anchor3_1[p]] != 1 ||
			bc7_anchor3_2[p] > 15 || bc7_part3[p][bc7_anchor3_2[p]] != 2 ||
			bc7_anchor3_1[p] == bc7_anchor3_2[p])
		{
			*why = "three subset anchors do not sit in their own subsets";
			return false;
		}
	}

	for (s = 0; s < 3; s++)
	{
		const unsigned char* w = bc7_weights(s + 2);
		int n = 1 << (s + 2);

		if (w[0] || w[n - 1] != 64)
		{
			*why = "an index weight table does not run from 0 to 64";
			return false;
		}

		for (i = 1; i < n; i++)
			if (w[i] <= w[i - 1])
			{
				*why = "an index weight table is not monotonic";
				return false;
			}
	}

	return true;
}

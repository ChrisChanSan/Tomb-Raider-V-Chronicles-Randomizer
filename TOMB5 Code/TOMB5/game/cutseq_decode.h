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
#pragma once

/* The cutscene track decoder of DATA\CUTSEQ.BIN, and nothing else.
 *
 * GetTrackWord, DecodeTrack, DecodeAnim and InitPackNodes (game\deltapak.cpp)
 * forward to the templates below, which hold their bodies unchanged. The
 * templates take the engine's RTDECODE, PACKNODE and NODELOADHEADER as
 * parameters, so this header includes no engine header:
 * Preparation\Tools\Diagnose\CutseqProbe.cpp decodes every cutscene of a
 * cutseq.bin with this file itself, not a copy, and instantiates it with the
 * same structs from global\types.h.
 *
 * A track is a stream of deltas: each call adds the next word to a node's
 * running value and advances that node's own decoder state, so frame k + 1
 * cannot be read without decoding frame k first. */

/* One signed word of `packmethod` bits at word index `off` of a bit stream.
 * The read takes four bytes at once, which is what the four bytes of padding
 * InitPackNodes leaves after each track are for. */
inline short CutseqTrackWord(long off, char* packed, long packmethod)
{
	unsigned long data;
	long index;

	index = off * packmethod;
	data = *(unsigned long*)&packed[index >> 3];
	data >>= index & 7;
	data &= (1 << packmethod) - 1;

	if (data & (1 << (packmethod - 1)))
		data |= ~((1 << packmethod) - 1);

	return (short)data;
}

/* The next delta of one track: a run of literal words (decodetype 1) or one
 * word repeated `counter` times (decodetype 2). */
template <class RT>
inline short CutseqDecodeTrack(char* packed, RT* code)
{
	short word, word2;

	if (!code->decodetype)
	{
		word = CutseqTrackWord(code->off, packed, code->packmethod);

		if (word & 0x20)
		{
			code->counter = word & 0xF;

			if (!code->counter)
				code->counter = 16;

			code->decodetype = 1;
			code->off++;
			code->length--;
		}
		else
		{
			code->decodetype = 2;

			if (word & 0x10)
			{
				word2 = CutseqTrackWord(code->off + 1, packed, code->packmethod);
				code->counter = (word & 7) << 5;
				code->counter |= word2 & 0x1F;
				code->counter &= 0xFF;
				code->data = CutseqTrackWord(code->off + 2, packed, code->packmethod);
				code->off += 3;
				code->length -= 3;
			}
			else
			{
				code->data = CutseqTrackWord(code->off + 1, packed, code->packmethod);
				code->counter = word & 0x7;
				code->off += 2;
				code->length -= 2;
			}
		}
	}

	if (code->decodetype == 2)
	{
		code->counter--;

		if (!code->counter)
			code->decodetype = 0;

		return code->data;
	}

	word = CutseqTrackWord(code->off, packed, code->packmethod);
	code->off++;
	code->length--;
	code->counter--;

	if (!code->counter)
		code->decodetype = 0;

	return word;
}

/* Frame 0 puts every node on its key and resets its decoder; any other frame
 * adds one delta per axis. Node 0 is a position and is not masked; the
 * others are angles masked with `limit` (0x3FF for an actor, ten bits a
 * turn; 0xFFFF for the camera's two positions). */
template <class NODE>
inline void CutseqDecodeAnim(NODE* nodes, long nNodes, long frame, long limit)
{
	if (!frame)
	{
		for (int i = 0; i < nNodes; i++)
		{
			nodes[i].xrot_run = nodes[i].xkey;
			nodes[i].yrot_run = nodes[i].ykey;
			nodes[i].zrot_run = nodes[i].zkey;
			nodes[i].decode_x.length = nodes[i].xlength;
			nodes[i].decode_y.length = nodes[i].ylength;
			nodes[i].decode_z.length = nodes[i].zlength;
			nodes[i].decode_x.off = 0;
			nodes[i].decode_y.off = 0;
			nodes[i].decode_z.off = 0;
			nodes[i].decode_x.counter = 0;
			nodes[i].decode_y.counter = 0;
			nodes[i].decode_z.counter = 0;
			nodes[i].decode_x.data = 0;
			nodes[i].decode_y.data = 0;
			nodes[i].decode_z.data = 0;
			nodes[i].decode_x.decodetype = 0;
			nodes[i].decode_y.decodetype = 0;
			nodes[i].decode_z.decodetype = 0;
		}
	}
	else
	{
		nodes[0].xrot_run += CutseqDecodeTrack(nodes[0].xpacked, &nodes[0].decode_x);
		nodes[0].yrot_run += CutseqDecodeTrack(nodes[0].ypacked, &nodes[0].decode_y);
		nodes[0].zrot_run += CutseqDecodeTrack(nodes[0].zpacked, &nodes[0].decode_z);

		for (int i = 1; i < nNodes; i++)
		{
			nodes[i].xrot_run += CutseqDecodeTrack(nodes[i].xpacked, &nodes[i].decode_x);
			nodes[i].yrot_run += CutseqDecodeTrack(nodes[i].ypacked, &nodes[i].decode_y);
			nodes[i].zrot_run += CutseqDecodeTrack(nodes[i].zpacked, &nodes[i].decode_z);
			nodes[i].xrot_run &= limit;
			nodes[i].yrot_run &= limit;
			nodes[i].zrot_run &= limit;
		}
	}
}

/* Points each node's three tracks into `packed`, behind the table of
 * `nNodes` load headers that starts it; each track is padded by four bytes. */
template <class LNODE, class NODE>
inline void CutseqInitPackNodes(LNODE* lnode, NODE* pnode, char* packed, long nNodes)
{
	long offset, xoff, yoff, zoff;

	offset = nNodes * sizeof(LNODE);

	for (int i = 0; i < nNodes; i++)
	{
		pnode[i].xkey = lnode[i].xkey;
		pnode[i].ykey = lnode[i].ykey;
		pnode[i].zkey = lnode[i].zkey;
		pnode[i].decode_x.packmethod = (lnode[i].packmethod >> 10) & 0xF;
		pnode[i].decode_y.packmethod = (lnode[i].packmethod >> 5) & 0xF;
		pnode[i].decode_z.packmethod = lnode[i].packmethod & 0xF;
		pnode[i].xlength = lnode[i].xlength;
		pnode[i].ylength = lnode[i].ylength;
		pnode[i].zlength = lnode[i].zlength;
		xoff = ((lnode[i].xlength * pnode[i].decode_x.packmethod) >> 3) + 4;
		yoff = ((lnode[i].ylength * pnode[i].decode_y.packmethod) >> 3) + 4;
		zoff = ((lnode[i].zlength * pnode[i].decode_z.packmethod) >> 3) + 4;
		pnode[i].xpacked = &packed[offset];
		pnode[i].ypacked = &packed[offset + xoff];
		pnode[i].zpacked = &packed[offset + xoff + yoff];

		offset += xoff + yoff + zoff;
	}
}

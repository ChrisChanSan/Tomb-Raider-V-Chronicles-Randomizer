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
#include <stddef.h>

/* GetCollidedObjects' two lists (game\collide.cpp) in tsv_buffer
 * (game\tomb4fx.cpp), the scratch buffer they share with the debris and the
 * effects' vertices. They are pointer lists, each closed by a 0: the items
 * at one place, the statics behind them. The callers gave the places in
 * bytes - 0 and 1024 for Lara, her flare, the torch and the pushable block,
 * 0x2000 and 0x3000 for the grenade and the burning floor - which on a
 * 32-bit build are 256 pointers to the items before the statics begin. On a
 * 64-bit build the same bytes are 128 pointers, and the 129th item would be
 * written over the first static.
 *
 * So the places are counted in pointers here, the same bytes on a 32-bit
 * build, twice them on a 64-bit one, and the buffer is as many pointers
 * long on either: 4096. GetCollidedObjects keeps each list inside the
 * smallest room it is given - 255 items and 1023 statics, each with its 0 -
 * where it had no limit. The fifteen levels hold at most 33 other items in
 * reach of one and 60 statics in a room with its neighbours, so the 32-bit
 * build never came near either. */

#define TSV_BUFFER_SLOTS	4096
#define TSV_BUFFER_SIZE		(TSV_BUFFER_SLOTS * sizeof(void*))

// The two pairs of places, in pointers.
#define COLLIDE_ITEMS_AT	0
#define COLLIDE_STATICS_AT	256
#define COLLIDE_ITEMS_AT_B	2048
#define COLLIDE_STATICS_AT_B	3072

// The most each list holds before its closing 0.
#define COLLIDE_ITEMS_MAX	(COLLIDE_STATICS_AT - COLLIDE_ITEMS_AT - 1)
#define COLLIDE_STATICS_MAX	(TSV_BUFFER_SLOTS - COLLIDE_STATICS_AT_B - 1)

// A place in pointers as the byte it starts at.
inline size_t CollideListByte(size_t slot)
{
	return slot * sizeof(void*);
}

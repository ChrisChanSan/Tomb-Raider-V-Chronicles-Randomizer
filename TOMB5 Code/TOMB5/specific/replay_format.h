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
#include <stdint.h>
#include <string.h>

/* The file of -record and -replay (specific\replay.cpp), as bytes: written
 * and read here, with no engine header, so that ModernTest runs these very
 * lines. Every number goes in little-endian byte by byte, never as a struct,
 * so a 32-bit and a 64-bit build write the same bytes for the same values.
 *
 * The file: a header of REPLAY_HEADER_SIZE bytes - "TR5R", the version
 * byte, the recording build's pointer width in bits, the Corpses switch
 * (REPLAY_CORPSES_AT), a zero byte and the build's stamp, zero padded - then
 * the records, each a type byte and its fields:
 *
 *   'L' a level start: level, control seed, draw seed, the tick counter,
 *       the frame counter, the frame-rate mode (six int32)
 *   'I' one call of the input function: the tick (uint16), the keys that
 *       are not 0 as a count (uint16) and index/value byte pairs, the
 *       mouse's two counts and buttons (int32), a flag byte (bit 0 the
 *       cursor captured, bit 1 a pad read, bit 2 the pad on XInput), then
 *       the pad's x, y, buttons and the two sticks' four raw axes (int32),
 *       and a check of the game's state the call found (int32: the logic
 *       generator's seed and Lara's place mixed), which -replay compares
 *   'P' a present whose tick count the logic takes: the tick (uint16) and
 *       the count (int32)
 *   'S' a save or a load: the tick (uint16), 's' or 'l', the slot (int32)
 *   'A' a drawn frame's place between two ticks as the clock gave it
 *       (TIME_SubTick, a double): the tick (uint16) and the double's eight
 *       bytes. The display-bound frame rate blends a frame by it, and the
 *       next tick reads Lara's joints as that frame left them.
 *
 * Version 2 added 'A' and the input record's check; a file of version 1
 * is refused. Version 3 added the Corpses switch at REPLAY_CORPSES_AT
 * (tomb5.corpses, 1 on, 0 off): it decides when a dead body's KillItem
 * runs, which moves the room lists and, for a spawned item, the free list,
 * so a recording plays back with the setting it was made with. A file of
 * version 2 is read as before, as corpses on, the only setting there was.
 */

#define REPLAY_VERSION		3
#define REPLAY_VERSION_OLDEST	2	// still read: corpses on
#define REPLAY_HEADER_SIZE	48
#define REPLAY_CORPSES_AT	6	// was a zero byte up to version 2
#define REPLAY_STAMP_SIZE	40
#define REPLAY_KEYS		256
#define REPLAY_RECORD_MAX	(1 + 2 + 2 + 2 * REPLAY_KEYS + 12 + 1 + 32)

#define REPLAY_LEVEL	'L'
#define REPLAY_INPUT	'I'
#define REPLAY_FRAMES	'P'
#define REPLAY_SLOT	'S'
#define REPLAY_SUBTICK	'A'

#define REPLAY_CAPTURED	1
#define REPLAY_PAD_READ	2
#define REPLAY_PAD_XINPUT	4

struct ReplayRecord
{
	uint8_t type;
	uint16_t tick;

	// 'L'
	int32_t level;
	int32_t ctrl_seed;
	int32_t draw_seed;
	int32_t counter;
	int32_t frame_counter;
	int32_t fps_mode;

	// 'I'
	uint8_t keys[REPLAY_KEYS];
	int32_t mouse_dx;
	int32_t mouse_dy;
	int32_t mouse_buttons;
	uint8_t flags;
	int32_t pad_x;
	int32_t pad_y;
	int32_t pad_fire;
	int32_t raw_lx;
	int32_t raw_ly;
	int32_t raw_rx;
	int32_t raw_ry;
	int32_t check;

	// 'P'
	int32_t frames;

	// 'S'
	uint8_t slot_kind;
	int32_t slot;

	// 'A'
	double subtick;
};

inline uint8_t* ReplayPut16(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	return p + 2;
}

inline uint8_t* ReplayPut32(uint8_t* p, int32_t v)
{
	const uint32_t u = (uint32_t)v;

	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
	return p + 4;
}

inline uint16_t ReplayGet16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

inline int32_t ReplayGet32(const uint8_t* p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/* The header into `out` (REPLAY_HEADER_SIZE bytes); `stamp` is cut to
 * REPLAY_STAMP_SIZE - 1 characters; `corpses` is the Corpses switch. */
inline size_t ReplayHeaderWrite(uint8_t* out, int pointer_bits,
	const char* stamp, int corpses)
{
	memset(out, 0, REPLAY_HEADER_SIZE);
	memcpy(out, "TR5R", 4);
	out[4] = REPLAY_VERSION;
	out[5] = (uint8_t)pointer_bits;
	out[REPLAY_CORPSES_AT] = corpses ? 1 : 0;

	if (stamp)
	{
		size_t n = strlen(stamp);

		if (n > REPLAY_STAMP_SIZE - 1)
			n = REPLAY_STAMP_SIZE - 1;

		memcpy(out + 8, stamp, n);
	}

	return REPLAY_HEADER_SIZE;
}

/* 1 for a header this build reads (versions REPLAY_VERSION_OLDEST to
 * REPLAY_VERSION), 0 for a short file or another magic, -1 for another
 * version. The version, the width, the Corpses switch (1 for version 2) and
 * the stamp (with its terminating zero, REPLAY_STAMP_SIZE bytes) are given
 * back either way. */
inline int ReplayHeaderRead(const uint8_t* in, size_t size, int* version,
	int* pointer_bits, int* corpses, char* stamp)
{
	*version = 0;
	*pointer_bits = 0;
	*corpses = 1;
	stamp[0] = 0;

	if (size < REPLAY_HEADER_SIZE || memcmp(in, "TR5R", 4))
		return 0;

	*version = in[4];
	*pointer_bits = in[5];

	if (in[4] >= 3)
		*corpses = in[REPLAY_CORPSES_AT] ? 1 : 0;

	memcpy(stamp, in + 8, REPLAY_STAMP_SIZE - 1);
	stamp[REPLAY_STAMP_SIZE - 1] = 0;
	return in[4] >= REPLAY_VERSION_OLDEST && in[4] <= REPLAY_VERSION ? 1 :
		-1;
}

/* One record into `out` (at least REPLAY_RECORD_MAX bytes); its length, or
 * 0 for a type this version does not know. */
inline size_t ReplayEncode(const ReplayRecord* r, uint8_t* out)
{
	uint8_t* p = out;

	*p++ = r->type;

	switch (r->type)
	{
	case REPLAY_LEVEL:
		p = ReplayPut32(p, r->level);
		p = ReplayPut32(p, r->ctrl_seed);
		p = ReplayPut32(p, r->draw_seed);
		p = ReplayPut32(p, r->counter);
		p = ReplayPut32(p, r->frame_counter);
		p = ReplayPut32(p, r->fps_mode);
		break;

	case REPLAY_INPUT:
	{
		uint8_t* count = p + 2;
		uint32_t n = 0;

		p = ReplayPut16(p, r->tick);
		p += 2;

		for (int i = 0; i < REPLAY_KEYS; i++)
		{
			if (!r->keys[i])
				continue;

			*p++ = (uint8_t)i;
			*p++ = r->keys[i];
			n++;
		}

		ReplayPut16(count, n);
		p = ReplayPut32(p, r->mouse_dx);
		p = ReplayPut32(p, r->mouse_dy);
		p = ReplayPut32(p, r->mouse_buttons);
		*p++ = r->flags;
		p = ReplayPut32(p, r->pad_x);
		p = ReplayPut32(p, r->pad_y);
		p = ReplayPut32(p, r->pad_fire);
		p = ReplayPut32(p, r->raw_lx);
		p = ReplayPut32(p, r->raw_ly);
		p = ReplayPut32(p, r->raw_rx);
		p = ReplayPut32(p, r->raw_ry);
		p = ReplayPut32(p, r->check);
		break;
	}

	case REPLAY_FRAMES:
		p = ReplayPut16(p, r->tick);
		p = ReplayPut32(p, r->frames);
		break;

	case REPLAY_SLOT:
		p = ReplayPut16(p, r->tick);
		*p++ = r->slot_kind;
		p = ReplayPut32(p, r->slot);
		break;

	case REPLAY_SUBTICK:
	{
		uint64_t bits;

		memcpy(&bits, &r->subtick, sizeof(bits));
		p = ReplayPut16(p, r->tick);
		p = ReplayPut32(p, (int32_t)(uint32_t)bits);
		p = ReplayPut32(p, (int32_t)(uint32_t)(bits >> 32));
		break;
	}

	default:
		return 0;
	}

	return (size_t)(p - out);
}

/* One record out of `in` (`size` bytes left in the file) into `r`, every
 * field it does not carry zeroed; its length, or 0 for a cut record, an
 * unknown type or a key count past 256. */
inline size_t ReplayDecode(const uint8_t* in, size_t size, ReplayRecord* r)
{
	const uint8_t* p = in;
	size_t need;

	memset(r, 0, sizeof(*r));

	if (!size)
		return 0;

	r->type = *p++;

	switch (r->type)
	{
	case REPLAY_LEVEL:
		if (size < 25)
			return 0;

		r->level = ReplayGet32(p);
		r->ctrl_seed = ReplayGet32(p + 4);
		r->draw_seed = ReplayGet32(p + 8);
		r->counter = ReplayGet32(p + 12);
		r->frame_counter = ReplayGet32(p + 16);
		r->fps_mode = ReplayGet32(p + 20);
		return 25;

	case REPLAY_INPUT:
	{
		if (size < 5)
			return 0;

		const uint32_t n = ReplayGet16(p + 2);

		need = 5 + 2 * (size_t)n + 45;

		if (n > REPLAY_KEYS || size < need)
			return 0;

		r->tick = ReplayGet16(p);
		p += 4;

		for (uint32_t i = 0; i < n; i++, p += 2)
			r->keys[p[0]] = p[1];

		r->mouse_dx = ReplayGet32(p);
		r->mouse_dy = ReplayGet32(p + 4);
		r->mouse_buttons = ReplayGet32(p + 8);
		r->flags = p[12];
		r->pad_x = ReplayGet32(p + 13);
		r->pad_y = ReplayGet32(p + 17);
		r->pad_fire = ReplayGet32(p + 21);
		r->raw_lx = ReplayGet32(p + 25);
		r->raw_ly = ReplayGet32(p + 29);
		r->raw_rx = ReplayGet32(p + 33);
		r->raw_ry = ReplayGet32(p + 37);
		r->check = ReplayGet32(p + 41);
		return need;
	}

	case REPLAY_FRAMES:
		if (size < 7)
			return 0;

		r->tick = ReplayGet16(p);
		r->frames = ReplayGet32(p + 2);
		return 7;

	case REPLAY_SLOT:
		if (size < 8)
			return 0;

		r->tick = ReplayGet16(p);
		r->slot_kind = p[2];
		r->slot = ReplayGet32(p + 3);
		return 8;

	case REPLAY_SUBTICK:
	{
		if (size < 11)
			return 0;

		const uint64_t bits = (uint64_t)(uint32_t)ReplayGet32(p + 2) |
			((uint64_t)(uint32_t)ReplayGet32(p + 6) << 32);

		r->tick = ReplayGet16(p);
		memcpy(&r->subtick, &bits, sizeof(bits));
		return 11;
	}
	}

	return 0;
}

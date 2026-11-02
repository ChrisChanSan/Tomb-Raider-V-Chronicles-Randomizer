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

/* ModernTest's checks of the level file's and the save's layouts - the half
 * that needs the engine's own structs. ModernTest.cpp includes no engine
 * header (its rules are plain numbers), so this file does it for them: it
 * is compiled beside ModernTest.cpp by bmt.bat with the engine's
 * global\types.h and global\disk_layout.h, so every static_assert of the
 * latter holds here too, and it runs the converters of disk_layout.h on
 * records whose numbers are known. ModernTest hands in its ok().
 *
 * The suite is a 32-bit program, where a runtime struct and its file form
 * are the same bytes; so beside the field checks, each conversion is set
 * against what the old raw read and fix-up gave. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <mmeapi.h>
#include <d3d9.h>
#include <dinput.h>
#include <dsound.h>
#include "../../../TOMB5 Code/TOMB5/global/disk_layout.h"
#include "../../../TOMB5 Code/TOMB5/specific/atan_math.h"

typedef void (*LayoutOk)(bool cond, const char* what, long got, long want);

/* Whether byte `i` lies in one of `n` four-byte fields at `at`. */
static bool in_fields(size_t i, const size_t* at, int n)
{
	for (int k = 0; k < n; k++)
	{
		if (i >= at[k] && i < at[k] + 4)
			return true;
	}

	return false;
}

/* A room header with known offsets in, the runtime room out. */
static void room_header(LayoutOk ok)
{
	static unsigned char block[0x1000];
	ROOM_INFO_DISK d;
	ROOM_INFO r, old;
	char* base = (char*)block + sizeof(ROOM_INFO_DISK);
	const size_t used[9] = { offsetof(ROOM_INFO_DISK, door),
		offsetof(ROOM_INFO_DISK, floor), offsetof(ROOM_INFO_DISK, mesh),
		offsetof(ROOM_INFO_DISK, pclight), offsetof(ROOM_INFO_DISK, fogbulb),
		offsetof(ROOM_INFO_DISK, pRoomlets), offsetof(ROOM_INFO_DISK, pRmVtx),
		offsetof(ROOM_INFO_DISK, pRmFace),
		offsetof(ROOM_INFO_DISK, pRmPrelight) };
	const size_t unread[9] = { offsetof(ROOM_INFO_DISK, data),
		offsetof(ROOM_INFO_DISK, light), offsetof(ROOM_INFO_DISK, SourceVB),
		offsetof(ROOM_INFO_DISK, FaceData),
		offsetof(ROOM_INFO_DISK, vnormals),
		offsetof(ROOM_INFO_DISK, fnormals),
		offsetof(ROOM_INFO_DISK, prelight),
		offsetof(ROOM_INFO_DISK, prelightwater),
		offsetof(ROOM_INFO_DISK, verts) };
	int bad;

	// Every byte a number of its own; the offsets the loader uses and junk
	// where the file leaves a pointer to the run time.
	for (size_t i = 0; i < sizeof(d); i++)
		((unsigned char*)&d)[i] = (unsigned char)(i * 7 + 3);

	d.door = 0x100;
	d.floor = 0x200;
	d.mesh = 0x300;
	d.pclight = 0x400;
	d.fogbulb = 0x500;
	d.pRoomlets = 0x600;
	d.pRmVtx = 0x700;
	d.pRmFace = 0x800;
	d.pRmPrelight = 0x900;
	d.nRoomlets = 2;
	memcpy(block, &d, sizeof(d));
	memset(&r, 0xCD, sizeof(r));
	RoomFromDisk(&r, &d, base);

	ok((char*)r.door - base == 0x100 && (char*)r.floor - base == 0x200 &&
		(char*)r.mesh - base == 0x300 && (char*)r.pclight - base == 0x400 &&
		(char*)r.fogbulb - base == 0x500,
		"x64 layout: a room's door, floor, mesh, lights and fog from the "
		"header's end", (long)((char*)r.floor - base), 0x200);
	ok((char*)r.pRoomlets - base == 0x600 && (char*)r.pRmVtx - base == 0x700 &&
		(char*)r.pRmFace - base == 0x800 &&
		(char*)r.pRmPrelight - base == 0x900,
		"x64 layout: ...its roomlets and their three streams",
		(long)((char*)r.pRoomlets - base), 0x600);
	ok(!r.data && !r.light && !r.SourceVB && !r.FaceData && !r.vnormals &&
		!r.fnormals && !r.prelight && !r.prelightwater && !r.verts,
		"x64 layout: the pointers the file leaves to the run time are null",
		0, 0);
	ok(r.x == d.x && r.minfloor == d.minfloor && r.x_size == d.x_size &&
		r.ambient == d.ambient && r.FlipNumber == d.FlipNumber &&
		r.test_bottom == d.test_bottom && r.flags == d.flags &&
		r.nShoreVerts == d.nShoreVerts && r.posz == d.posz &&
		r.watercalc == d.watercalc && r.gt4cnt == d.gt4cnt &&
		r.nFogBulbs == d.nFogBulbs && r.cy1 == d.cy1 &&
		r.nRoomlets == 2 && r.vDumpSz == d.vDumpSz && r.fBottom == d.fBottom,
		"x64 layout: every number of the header by name", r.nRoomlets, 2);

	// Where the two forms are one (this 32-bit suite), every byte that is
	// no pointer lies where it lay.
	bad = 0;

	for (size_t i = 0; i < sizeof(d) && sizeof(r) == sizeof(d); i++)
	{
		if (!in_fields(i, used, 9) && !in_fields(i, unread, 9) &&
			((unsigned char*)&r)[i] != ((unsigned char*)&d)[i])
			bad++;
	}

	ok(sizeof(r) == sizeof(d) && bad == 0,
		"x64 layout: on 32 bits the header's numbers are its bytes", bad, 0);

	// The old loader: the header copied raw, its offsets fixed up by the
	// address of the header's end. The conversion gives the same room but
	// for the pointers no one reads.
	bad = 0;

	if (sizeof(old) == sizeof(d))
	{
		memcpy(&old, block, sizeof(old));
		old.fogbulb = (FOGBULB_INFO*)((char*)old.fogbulb + (uintptr_t)base);
		old.pclight = (PCLIGHT_INFO*)((char*)old.pclight + (uintptr_t)base);
		old.floor = (FLOOR_INFO*)((char*)old.floor + (uintptr_t)base);
		old.door = (short*)((char*)old.door + (uintptr_t)base);
		old.mesh = (MESH_INFO*)((char*)old.mesh + (uintptr_t)base);
		old.pRoomlets = (ROOMLET*)((char*)old.pRoomlets + (uintptr_t)base);
		old.pRmFace = (short*)((char*)old.pRmFace + (uintptr_t)base);
		old.pRmPrelight = (long*)((char*)old.pRmPrelight + (uintptr_t)base);
		old.pRmVtx = (float*)((char*)old.pRmVtx + (uintptr_t)base);

		for (size_t i = 0; i < sizeof(old); i++)
		{
			if (!in_fields(i, unread, 9) &&
				((unsigned char*)&r)[i] != ((unsigned char*)&old)[i])
				bad++;
		}
	}
	else
		bad = -1;

	ok(bad == 0, "x64 layout: the room the old raw read gave, byte for byte",
		bad, 0);

	// An offset above 2 GB is the 32-bit sum's wrap: base less the rest.
	d.door = 0xFFFFFFFEu;
	RoomFromDisk(&r, &d, base);
	ok((char*)r.door == base - 2,
		"x64 layout: an offset counts as an int32_t, as the 32-bit sum wrapped",
		(long)((char*)r.door - base), -2);
}

/* Two roomlets out of the file's form: in place, and into an array of
 * their own as a 64-bit build has them. */
static void roomlets(LayoutOk ok)
{
	static ROOMLET_DISK disk[2];
	static ROOMLET own[2];
	static short fac[64];
	static long pre[16];
	static float vtx[64];

	for (int pass = 0; pass < 2; pass++)
	{
		memset(disk, 0, sizeof(disk));
		disk[0].nVtx = 3;
		disk[0].nTri = 1;
		disk[0].nQuad = 2;
		disk[0].nWVtx = 11;
		disk[0].bBox[5] = 640.5f;
		disk[0].pVtx = 0xDEADBEEF;
		disk[1].nVtx = 4;
		disk[1].nTri = 2;
		disk[1].nSVtx = 9;
		disk[1].bBox[0] = -128.0f;

		ROOMLET* out = pass ? own : (ROOMLET*)disk;

		RoomletsFromDisk(out, disk, 2, fac, pre, vtx);
		ok(out[0].pFac == fac && out[0].pPrelight == pre &&
			out[0].pSVtx == vtx && out[1].pFac == fac + 5 + 12 &&
			out[1].pPrelight == pre + 3 && out[1].pSVtx == vtx + 21,
			pass ? "x64 layout: roomlets into their own array, streams in step"
			: "x64 layout: roomlets where they lie, streams in step",
			(long)(out[1].pFac - fac), 17);
		ok(out[0].nVtx == 3 && out[0].nWVtx == 11 &&
			out[0].bBox[5] == 640.5f && out[1].nSVtx == 9 &&
			out[1].nTri == 2 && out[1].bBox[0] == -128.0f &&
			!out[0].pVtx && !out[1].pVtx,
			pass ? "x64 layout: ...their counts and boxes kept, pVtx null"
			: "x64 layout: ...counts and boxes kept in place, pVtx null",
			out[1].nSVtx, 9);
	}
}

/* Anims out of the file's 40-byte form, waiting where file.cpp lets them. */
static void anims(LayoutOk ok)
{
	static ANIM_STRUCT block[3];
	static short frames[256];
	ANIM_STRUCT_DISK one, three[3];
	ANIM_STRUCT_DISK* slot;
	ANIM_STRUCT a;

	memset(&one, 0, sizeof(one));
	one.frame_ptr = 0x40;
	one.interpolation = 0x0103;
	one.current_anim_state = 7;
	one.velocity = 0x12345678;
	one.Xacceleration = -65536;
	one.frame_base = 100;
	one.frame_end = 140;
	one.jump_anim_num = 12;
	one.command_index = 33;
	AnimFromDisk(&a, &one, frames);
	ok((char*)a.frame_ptr - (char*)frames == 0x40 &&
		a.interpolation == 0x0103 && a.current_anim_state == 7 && a.velocity == 0x12345678 &&
		a.Xacceleration == -65536 && a.frame_base == 100 &&
		a.frame_end == 140 && a.jump_anim_num == 12 && a.command_index == 33,
		"x64 layout: an anim's frames from the frames block, its numbers kept",
		(long)((char*)a.frame_ptr - (char*)frames), 0x40);

	// Three anims read to the slot file.cpp gives them, converted forwards.
	for (int i = 0; i < 3; i++)
	{
		three[i] = one;
		three[i].frame_ptr = 0x10 * (i + 1);
		three[i].frame_base = (short)(10 * i);
	}

	slot = AnimDiskSlot(block, 3);
	memcpy(slot, three, sizeof(three));
	AnimsFromDisk(block, slot, 3, frames);
	ok(block[0].frame_ptr == frames + 8 && block[1].frame_ptr == frames + 16 &&
		block[2].frame_ptr == frames + 24 && block[2].frame_base == 20 &&
		block[1].command_index == 33,
		"x64 layout: three anims from the end of their block, in order",
		block[2].frame_base, 20);
	ok((char*)slot - (char*)block ==
		3 * (long)(sizeof(ANIM_STRUCT) - sizeof(ANIM_STRUCT_DISK)),
		"x64 layout: ...the slot is the block's end, behind the anims' growth",
		(long)((char*)slot - (char*)block), 0);
}

/* Lara's bytes between her pointers, into the save's layout and back. */
static void lara_runs(LayoutOk ok)
{
	static LARA_INFO l, back;
	static LARA_INFO_SAVE s;
	const size_t ptrs[] = { offsetof(LARA_INFO_SAVE, spaz_effect),
		offsetof(LARA_INFO_SAVE, target),
		offsetof(LARA_INFO_SAVE, left_arm.frame_base),
		offsetof(LARA_INFO_SAVE, right_arm.frame_base),
		offsetof(LARA_INFO_SAVE, creature),
		offsetof(LARA_INFO_SAVE, GeneralPtr) };
	int bad = 0, kept = 0, copied = 0;

	for (size_t i = 0; i < sizeof(l); i++)
		((unsigned char*)&l)[i] = (unsigned char)(i * 13 + 1);

	memset(&s, 0, sizeof(s));
	LaraCopyRuns(&l, &s, 1);

	for (size_t i = 0; i < sizeof(s); i++)
	{
		const bool ptr = in_fields(i, ptrs, 6) ||
			(i >= offsetof(LARA_INFO_SAVE, mesh_ptrs) &&
			i < offsetof(LARA_INFO_SAVE, mesh_ptrs) + 60);

		if (ptr)
			kept += ((unsigned char*)&s)[i] == 0;
		else
		{
			copied++;

			if (sizeof(l) == sizeof(s) &&
				((unsigned char*)&s)[i] != ((unsigned char*)&l)[i])
				bad++;
		}
	}

	ok(kept == 84 && copied == 265 && bad == 0,
		"x64 layout: Lara's 265 bytes between her 21 pointers go into the "
		"save, the pointers' 84 left to the save code", copied, 265);

	memset(&back, 0xEE, sizeof(back));
	LaraCopyRuns(&back, &s, 0);
	ok(back.item_number == l.item_number && back.air == l.air &&
		back.IsMoving == l.IsMoving && back.BurnBlue == l.BurnBlue &&
		back.mesh_effects == l.mesh_effects &&
		back.left_arm.flash_gun == l.left_arm.flash_gun &&
		back.right_arm.frame_number == l.right_arm.frame_number &&
		back.holster == l.holster && back.CornerX == l.CornerX &&
		back.RopePtr == l.RopePtr && back.RopeOffset == l.RopeOffset &&
		back.ChaffTimer == l.ChaffTimer &&
		!memcmp(back.keyitems, l.keyitems, sizeof(l.keyitems)),
		"x64 layout: ...and back, field by field", back.air, l.air);
	ok((uintptr_t)back.GeneralPtr == (uintptr_t)0xEEEEEEEEu &&
		(uintptr_t)back.mesh_ptrs[14] == (uintptr_t)0xEEEEEEEEu &&
		(uintptr_t)back.left_arm.frame_base == (uintptr_t)0xEEEEEEEEu,
		"x64 layout: ...the runtime pointers untouched by the copy", 0, 0);
}

/* The pendulum's rope as its distance from RopeList. */
static void pendulum(LayoutOk ok)
{
	static ROPE_STRUCT ropes[8];
	PENDULUM p, back;
	PENDULUM_SAVE s;

	memset(&p, 0, sizeof(p));
	p.Position.y = -2048;
	p.Velocity.x = 65536;
	p.node = 5;
	p.Rope = &ropes[3];
	PendulumToSave(&s, &p, ropes);
	PendulumFromSave(&back, &s, ropes);
	ok(s.Rope == 3 * sizeof(ROPE_STRUCT) && s.node == 5 &&
		back.Rope == &ropes[3] && back.Position.y == -2048 &&
		back.Velocity.x == 65536,
		"x64 layout: the pendulum's rope by its distance, there and back",
		(long)s.Rope, 3 * 1460);
}

/* phd_atan as it read before its arithmetic moved to atan_math.h, word for
 * word: the reference AtanMath must equal for every input but LONG_MIN. */
static long OldAtan(long x, long y)
{
	long octant, n, result;

	result = 0;
	octant = 0;

	if (x || y)
	{
		if (x < 0)
		{
			octant += 4;
			x = -x;
		}

		if (y < 0)
		{
			octant += 2;
			y = -y;
		}

		if (y > x)
		{
			octant++;
			n = x;
			x = y;
			y = n;
		}

		while ((short)y != y)
		{
			x >>= 1;
			y >>= 1;
		}

		result = phdtan2[octant] + phdtantab[(y << 11) / x];

		if (result < 0)
			result = -result;
	}

	return result;
}

/* phd_atan: the same angle for every input it could take, and LONG_MIN -
 * the one that divided by zero - clamped to the angle of -LONG_MAX. */
static void atan_clamp(LayoutOk ok)
{
	static const long edge[] = { 0, 1, -1, 2, -2, 7, -7, 255, -255, 32767,
		-32767, 32768, -32768, 65535, -65535, 1000000, -1000000, 1 << 30,
		-(1 << 30), LONG_MAX, -LONG_MAX };
	const int n = (int)(sizeof(edge) / sizeof(edge[0]));
	unsigned long seed = 0x303B;
	long bad = 0, tried = 0;

	for (int i = 0; i < n; i++)
	{
		for (int j = 0; j < n; j++, tried++)
			bad += AtanMath(edge[i], edge[j]) != OldAtan(edge[i], edge[j]);
	}

	for (int k = 0; k < 200000; k++, tried++)
	{
		seed = seed * 1103515245UL + 12345UL;
		long x = (long)(seed >> 1) >> (seed & 15);
		seed = seed * 1103515245UL + 12345UL;
		long y = (long)(seed >> 1) >> (seed & 15);

		if (seed & 0x10000)
			x = -x;

		if (seed & 0x20000)
			y = -y;

		bad += AtanMath(x, y) != OldAtan(x, y);
	}

	ok(bad == 0, "x64 layout: phd_atan's arithmetic as before, 200441 pairs",
		bad, 0);

	long x = LONG_MIN, y = 4096;
	const bool c1 = AtanClamp(&x, &y);
	const long a1 = AtanMath(x, y);

	ok(c1 && x == -LONG_MAX && y == 4096 && a1 == OldAtan(-LONG_MAX, 4096),
		"x64 layout: phd_atan clamps an x of LONG_MIN, one unit in",
		a1, OldAtan(-LONG_MAX, 4096));
	x = 12;
	y = LONG_MIN;

	const bool c2 = AtanClamp(&x, &y);
	const long a2 = AtanMath(x, y);

	ok(c2 && y == -LONG_MAX && x == 12 && a2 >= 0 && a2 < 65536,
		"x64 layout: ...and a y of LONG_MIN, an angle in the turn", a2, 0);
	x = LONG_MIN;
	y = LONG_MIN;
	ok(AtanClamp(&x, &y) && AtanMath(x, y) == OldAtan(-LONG_MAX, -LONG_MAX),
		"x64 layout: ...and both", AtanMath(x, y),
		OldAtan(-LONG_MAX, -LONG_MAX));
	x = -LONG_MAX;
	y = 0;
	ok(!AtanClamp(&x, &y) && x == -LONG_MAX && y == 0,
		"x64 layout: every other input passes unclamped", 0, 0);
}

void layout_checks(LayoutOk ok)
{
	ok(sizeof(SAVEGAME_INFO) == 29397 && sizeof(LARA_INFO_SAVE) == 349 &&
		sizeof(PENDULUM_SAVE) == 32,
		"x64 layout: the save's image, Lara and the pendulum at their sizes",
		(long)sizeof(SAVEGAME_INFO), 29397);
	ok(sizeof(ROOM_INFO_DISK) == 208 && sizeof(ROOMLET_DISK) == 56 &&
		sizeof(ANIM_STRUCT_DISK) == 40,
		"x64 layout: the file's room header, roomlet and anim",
		(long)sizeof(ROOM_INFO_DISK), 208);
	room_header(ok);
	roomlets(ok);
	anims(ok);
	lara_runs(ok);
	pendulum(ok);
	atan_clamp(ok);
}

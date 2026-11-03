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

/* -high: no allocation of the 64-bit build below 4 GB.
 *
 * A pointer cut to 32 bits and widened again keeps its low half. On a 64-bit
 * build that is the right address only while the memory it names lies below
 * 4 GB, and where the heap, the stacks and the game_malloc pool land moves
 * with every start - so such a cut can play right in most runs and write
 * somewhere else in the next. With -high the start reserves every free
 * region below 4 GB (MEM_RESERVE, PAGE_NOACCESS: address space taken, no
 * memory spent), before the pool and any level is allocated, and the pool is
 * taken from the top of the address space (MEM_TOP_DOWN). Whatever is
 * allocated after the walk lands above 4 GB, and a cut pointer then names a
 * reserved page and faults at its first use.
 *
 * What it cannot move: everything allocated before WinMain reads the switch
 * - the executable and its DLLs (above 4 GB on a 64-bit Windows anyway), the
 * CRT's heap as far as it was already committed, the first thread's stack,
 * the sanitizer's own regions in the Asan build. A heap that already holds a
 * segment below 4 GB may hand out blocks from it; the HIGHLOG line names the
 * bytes the walk found held below 4 GB. Allocations of the system's own
 * libraries afterwards (Direct3D 9, XAudio2, DirectInput) go above 4 GB like
 * any other.
 *
 * Engine-free, like hd\hd_memory.cpp: ModernTest compiles it to test the
 * span arithmetic and the switch's word match. */
#include <stddef.h>
#include <stdint.h>

// The line the walk reserves below: 4 GB.
#define HIGH_LINE	0x100000000ULL

// What the walk did, for the HIGHLOG lines.
struct HighReserve
{
	long regions;			// free regions reserved below 4 GB
	unsigned long long bytes;	// their bytes
	long failed;			// reservations VirtualAlloc refused
	unsigned long long held;	// bytes below 4 GB others held already
	unsigned long long left;	// free bytes below 4 GB after the walk
};

/* The part of a free region [base, base + size) that can be reserved below
 * `line`: from base rounded up to `granularity` (a reservation starts on
 * the 64 KB allocation granularity, and address 0 means "anywhere" to
 * VirtualAlloc, so nothing starts below one granule) to the region's end or
 * `line`, whichever is lower, rounded down to `page`. False when no whole
 * page is left. */
bool HighReserveSpan(uint64_t base, uint64_t size, uint64_t line,
	uint64_t granularity, uint64_t page, uint64_t* outBase, uint64_t* outSize);

/* A switch `word` (any case) as a whole word behind '-' or '/': the sign
 * at the line's start or after a blank, the word ended by a blank or the
 * line's end - not inside a longer word or a path. */
bool SwitchWordGiven(const char* cmd, const char* word);

/* -high, by SwitchWordGiven. */
bool HighSwitchGiven(const char* cmd);

/* Reserves every free region below 4 GB and says what it did. False on a
 * 32-bit build, which has nothing above 4 GB to move to. Once per run. */
bool HighReserveBelow4G(HighReserve* out);

// The walk's result when it ran in this run, else 0.
const HighReserve* HighReserveDone();

/* The pool's memory under -high: committed, read-write, from the top of the
 * address space. 0 when VirtualAlloc refuses. */
void* HighPoolTake(size_t size);
void HighPoolRelease(void* pool);

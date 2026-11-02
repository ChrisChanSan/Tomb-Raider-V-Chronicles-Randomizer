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

/* The game_malloc pool under AddressSanitizer (the Asan|x64 configuration,
 * Preparation\Tools\Diagnose\basan.bat).
 *
 * The pool is one block of the C heap, so the sanitizer sees every byte of
 * it as allocated and says nothing about a write inside it. The Asan build
 * tells it more: the whole pool is poisoned at the start and again when
 * FreeLevel hands it back, game_malloc unpoisons each block as it hands it
 * out, and POOL_REDZONE poisoned bytes stay behind every block. Then
 *
 * - a write or read through a pointer kept from the level before, into
 *   pool memory the new level has not been given yet (the joby3 load crash
 *   of 2026-08-25: ShutThatDoor writing through baddie_slots while the
 *   next level loaded), and
 * - a block's overrun into the next one (mesh_vtxbuf did so on the first
 *   64-bit builds, sized 4 bytes an entry for 8)
 *
 * stop the game with a report at the line. A stale pointer whose address
 * the new level has already been handed again is NOT caught: the block
 * there is the new level's and unpoisoned. That class is the table of the
 * pointers FreeLevel clears (specific\file.cpp).
 *
 * The red zones cost room: the Asan build's pool is MALLOC_SIZE plus
 * POOL_SPARE, and game_malloc refuses a block when the blocks alone would
 * pass MALLOC_SIZE, where a release build refuses it. On the other builds
 * both are 0 and the functions empty: their pool is block for block what
 * it was. */
#include <stddef.h>

#ifdef __SANITIZE_ADDRESS__
extern "C"
{
	void __asan_poison_memory_region(void const volatile* addr, size_t size);
	void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
}

// Behind every block; a multiple of 8, so every block starts on 8 bytes.
#define POOL_REDZONE	32
// Room for the red zones: 262,144 blocks of them. A level takes a few
// thousand.
#define POOL_SPARE		(8 * 1024 * 1024)

inline void PoolPoison(const void* p, size_t n)
{
	__asan_poison_memory_region(p, n);
}

inline void PoolUnpoison(const void* p, size_t n)
{
	__asan_unpoison_memory_region(p, n);
}

/* The sanitizer's report into the engine's log and the crash seam: every
 * line of it as ASANLOG through `writer` (the log, with -log), then a CRASH
 * line and a dump as the crash seam writes them (crashseam.h), with the
 * report's kind, address and file. The report itself goes to the file
 * ASAN_OPTIONS=log_path names, or, without one, to asan_report.<pid> in the
 * working directory. Once, after CrashSeamInstall. */
void AsanSeamInstall(void (*writer)(const char* line));
#else
#define POOL_REDZONE	0
#define POOL_SPARE		0

inline void PoolPoison(const void*, size_t)
{
}

inline void PoolUnpoison(const void*, size_t)
{
}

inline void AsanSeamInstall(void (*)(const char*))
{
}
#endif

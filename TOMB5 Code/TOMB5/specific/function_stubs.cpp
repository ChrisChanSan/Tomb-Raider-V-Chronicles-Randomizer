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
#include "../tomb5/pch.h"
#include "function_stubs.h"
#include "pool_guard.h"		// the pool under AddressSanitizer
#include "high_pool.h"		// -high: the pool from the top

// Keeps the logging code compiled in; gLogEnabled below controls whether
// Log() actually does anything at runtime.
#define DO_LOG

// Off until the command line says otherwise. WinCheckLogFlag in
// winmain.cpp reads the flag before any other startup step, so no early
// log line is lost.
bool gLogEnabled = 0;

FILE* logF = 0;

char* malloc_buffer;
char* malloc_ptr;
long malloc_size;
long malloc_free;

long nPolyType;

static long malloc_used;		// this level's blocks, without red zones
static long malloc_blocks;		// and how many
static bool malloc_high;		// the pool is -high's (VirtualAlloc)

static long rand_1 = 0xD371F947;
static long rand_2 = 0xD371F947;

void init_game_malloc()
{
	const HighReserve* high = HighReserveDone();

	// MALLOC_SIZE, and in the Asan build the room for the red zones on top
	// (pool_guard.h); POOL_SPARE is 0 on every other build.
	malloc_size = MALLOC_SIZE + POOL_SPARE;
	malloc_buffer = 0;
	malloc_high = false;

	/* -high (high_pool.h): the pool from the top of the address space. Should
	 * VirtualAlloc refuse it, the heap's, as without the switch. */
	if (high)
	{
		malloc_buffer = (char*)HighPoolTake((size_t)malloc_size);
		malloc_high = malloc_buffer != 0;
	}

	if (!malloc_buffer)
		malloc_buffer = (char*)malloc(malloc_size);

	malloc_ptr = malloc_buffer;
	malloc_free = malloc_size;
	malloc_used = 0;
	malloc_blocks = 0;
	PoolPoison(malloc_buffer, (size_t)malloc_size);

	// Where the pool lies in this run: it moves at every start (bottom-up
	// address randomisation), which is what game\save_pointer_rule.h has
	// to survive - two logs' POOL lines show by how much.
	Log("POOL base=0x%08lX size=%ld", (unsigned long)(uintptr_t)malloc_buffer,
		(long)MALLOC_SIZE);

	if (high)
		Log("HIGHLOG reserved=%ld regions below 4 GB, pool base=0x%016llX%s",
			high->regions, (unsigned long long)(uintptr_t)malloc_buffer,
			malloc_high ? " top-down" : " heap: VirtualAlloc refused");

#ifdef __SANITIZE_ADDRESS__
	Log("ASANLOG pool base=0x%016llX span=%ld redzone=%d",
		(unsigned long long)(uintptr_t)malloc_buffer, malloc_size,
		POOL_REDZONE);
#endif
}

void* game_malloc(long size)
{
	char* ptr;

	// Blocks start on 4 bytes, as they always did; on a 64-bit build on 8,
	// the width of the pointers many of them hold. The 32-bit pool stays
	// exactly as it was, block for block.
#ifdef _WIN64
	size = (size + 7) & -8;
#else
	size = (size + 3) & -4;
#endif

	// The blocks alone against MALLOC_SIZE, as on every build; the Asan
	// build's red zones come out of POOL_SPARE.
	if (size > MALLOC_SIZE - malloc_used || size + POOL_REDZONE > malloc_free)
	{
		Log("OUT OF MEMORY");
		return 0;
	}

	ptr = malloc_ptr;
	malloc_free -= size + POOL_REDZONE;
	malloc_used += size;
	malloc_blocks++;
	malloc_ptr += size + POOL_REDZONE;
	PoolUnpoison(ptr, (size_t)size);	// its red zone stays poisoned
	memset(ptr, 0, size);
	return ptr;
}

/* Every block handed back at once (FreeLevel). Every pointer into the pool
 * is stale from here; specific\file.cpp's FreeLevel clears the ones that
 * are read before the next level gives them a new block. */
void reset_game_malloc()
{
#ifdef __SANITIZE_ADDRESS__
	Log("ASANLOG pool freed used=%ld blocks=%ld span=%ld", malloc_used,
		malloc_blocks, (long)(malloc_ptr - malloc_buffer));
#endif
	malloc_ptr = malloc_buffer;
	malloc_free = malloc_size;
	malloc_used = 0;
	malloc_blocks = 0;
	PoolPoison(malloc_buffer, (size_t)malloc_size);
}

void free_game_malloc()
{
	PoolUnpoison(malloc_buffer, (size_t)malloc_size);

	if (malloc_high)
		HighPoolRelease(malloc_buffer);
	else
		free(malloc_buffer);

	malloc_buffer = 0;
	malloc_ptr = 0;
	malloc_free = 0;
	malloc_high = false;
}

long GetRandomControl()
{
	rand_1 = 0x41C64E6D * rand_1 + 12345;
	return (rand_1 >> 10) & 0x7FFF;
}

void SeedRandomControl(long seed)
{
	rand_1 = seed;
}

// Read-only, and used only by interpolate.cpp and -record's level header;
// see function_stubs.h for why.
long GetRandomControlSeed()
{
	return rand_1;
}

long GetRandomDraw()
{
	rand_2 = 0x41C64E6D * rand_2 + 12345;
	return (rand_2 >> 10) & 0x7FFF;
}

void SeedRandomDraw(long seed)
{
	rand_2 = seed;
}

long GetRandomDrawSeed()
{
	return rand_2;
}

void Log(const char* s, ...)
{
#ifdef DO_LOG
	va_list list;
	char buf[4096];

	// Checked before anything else in the function. Without -log this costs
	// only a call and a branch per line: no file is opened, nothing is
	// formatted, no log.txt appears next to the executable. The seams that
	// call Log() (sound, clipping, slots) sit in per-frame code, so the
	// check has to stay this cheap.
	if (!gLogEnabled)
		return;

	if (!logF)
		logF = fopen("log.txt", "w+");

	va_start(list, s);
	vsprintf(buf, s, list);
	strcat(buf, "\n");
	va_end(list);

	// Milliseconds since the first log entry, not a clock time: this shows
	// the gap between two lines, which localizes a startup delay, where a
	// plain ordering of lines would not. Costs one GetTickCount call per
	// line.
	{
		static DWORD firstTick = 0;
		DWORD now = GetTickCount();
		if (!firstTick) firstTick = now;

		char stamp[24];
		sprintf(stamp, "[%7lu] ", (unsigned long)(now - firstTick));
		fwrite(stamp, strlen(stamp), 1, logF);
	}

	fwrite(buf, strlen(buf), 1, logF);
	fflush(logF);
#endif
}

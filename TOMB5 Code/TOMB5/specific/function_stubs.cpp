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

static long malloc_used;

static long rand_1 = 0xD371F947;
static long rand_2 = 0xD371F947;

void init_game_malloc()
{
	malloc_buffer = (char*)malloc(MALLOC_SIZE);
	malloc_size = MALLOC_SIZE;
	malloc_ptr = malloc_buffer;
	malloc_free = MALLOC_SIZE;
	malloc_used = 0;

	// Where the pool lies in this run: it moves at every start (bottom-up
	// address randomisation), which is what game\save_pointer_rule.h has
	// to survive - two logs' POOL lines show by how much.
	Log("POOL base=0x%08lX size=%ld", (unsigned long)(uintptr_t)malloc_buffer,
		(long)MALLOC_SIZE);
}

void* game_malloc(long size)
{
	char* ptr;

	size = (size + 3) & -4;

	if (size > malloc_free)
	{
		Log("OUT OF MEMORY");
		return 0;
	}

	ptr = malloc_ptr;
	malloc_free -= size;
	malloc_used += size;
	malloc_ptr += size;
	memset(ptr, 0, size);
	return ptr;
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

// Read-only, and used only by interpolate.cpp; see function_stubs.h for why.
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

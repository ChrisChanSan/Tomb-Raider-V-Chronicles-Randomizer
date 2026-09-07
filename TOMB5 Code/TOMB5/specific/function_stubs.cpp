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

// Log() is COMPILED IN, but silent unless the game is started with -log
// (#125). Upstream compiled it out entirely, which meant a player who hit
// something could never produce a log - the only way was a special build.
// One executable that can be asked for a log is worth more than a second
// executable nobody has.
#define DO_LOG

// The gate. Off until the command line says otherwise; see
// WinCheckLogFlag in winmain.cpp, which reads the flag before ANY other
// startup step so no early line is lost.
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

// #148, order 3 - the note is in function_stubs.h. Read only, and only by
// game\interpolate.cpp.
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

	// THE FIRST THING IN THE FUNCTION, before the format buffer is even
	// touched. Without -log this costs a call and a branch per line and
	// nothing else: no file is opened, nothing is formatted, no log.txt
	// appears next to the executable. That matters because the seams this
	// gate exists for (sound, clipping, slots) sit in per-frame code -
	// with the old unconditional version a long session wrote a file that
	// grew without end and flushed on every single line.
	if (!gLogEnabled)
		return;

	if (!logF)
		logF = fopen("log.txt", "w+");

	va_start(list, s);
	vsprintf(buf, s, list);
	strcat(buf, "\n");
	va_end(list);

	// Milliseconds since the first log entry. Without this a startup delay
	// cannot be localized - you see the order but not WHERE the time is
	// lost. Deliberately relative and not a clock time: what is sought are
	// gaps between two lines, not the moment. Costs one GetTickCount call
	// per line.
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

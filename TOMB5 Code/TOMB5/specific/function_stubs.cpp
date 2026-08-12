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

// TEMPORARY: enable Log()'s output (normally compiled out) to diagnose the
// SFX-missing-on-headset issue via log.txt - remove once done.
#define DO_LOG

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

	if (!logF)
		logF = fopen("log.txt", "w+");

	va_start(list, s);
	vsprintf(buf, s, list);
	strcat(buf, "\n");
	va_end(list);

	// Millisekunden seit dem ersten Logeintrag. Ohne diese Angabe laesst sich
	// eine Startverzoegerung nicht lokalisieren - man sieht die Reihenfolge,
	// aber nicht, WO die Zeit verloren geht. Bewusst relativ und nicht als
	// Uhrzeit: gesucht sind Abstaende zwischen zwei Zeilen, nicht der
	// Zeitpunkt. Kostet einen GetTickCount-Aufruf pro Zeile.
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

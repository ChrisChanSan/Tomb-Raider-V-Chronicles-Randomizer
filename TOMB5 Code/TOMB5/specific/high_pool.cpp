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
#include "high_pool.h"

// NOT the engine's pch.h: ModernTest compiles this file beside itself
// (high_pool.h says why).
#include <windows.h>
#include <ctype.h>
#include <string.h>

static HighReserve g_done;
static bool g_ran = false;

bool HighReserveSpan(uint64_t base, uint64_t size, uint64_t line,
	uint64_t granularity, uint64_t page, uint64_t* outBase, uint64_t* outSize)
{
	uint64_t from, to;

	*outBase = 0;
	*outSize = 0;

	if (!size || !granularity || !page || base >= line)
		return false;

	from = (base + granularity - 1) / granularity * granularity;

	if (from < granularity)
		from = granularity;

	to = base + size;

	if (to < base || to > line)		// wrapped, or past the line
		to = line;

	to = to / page * page;

	if (from >= to)
		return false;

	*outBase = from;
	*outSize = to - from;
	return true;
}

/* Where the first whole switch `word` ends in `cmd` - the index just past
 * its last letter - or 0 when it is not there. 0 is never an end: the word
 * has a letter and a sign before it. */
static size_t SwitchWordEnd(const char* cmd, const char* word)
{
	size_t len, n, i, k;

	if (!cmd || !word)
		return 0;

	len = strlen(cmd);
	n = strlen(word);

	if (!n)
		return 0;

	for (i = 1; i + n <= len; i++)
	{
		// Through unsigned char, as WinCheckLogFlag does: a path with
		// umlauts hands toupper a negative value otherwise.
		for (k = 0; k < n; k++)
			if (toupper((unsigned char)cmd[i + k]) !=
				toupper((unsigned char)word[k]))
				break;

		if (k < n)
			continue;

		if (cmd[i - 1] != '-' && cmd[i - 1] != '/')
			continue;

		if (i > 1 && cmd[i - 2] != ' ')
			continue;

		if (i + n < len && cmd[i + n] != ' ')
			continue;

		return i + n;
	}

	return 0;
}

bool SwitchWordGiven(const char* cmd, const char* word)
{
	return SwitchWordEnd(cmd, word) != 0;
}

bool HighSwitchGiven(const char* cmd)
{
	return SwitchWordGiven(cmd, "high");
}

/* One tick number of -rngtrace from cmd[*at]: blanks, then 1..5 digits up
 * to 65535 (GlobalCounter is an unsigned short), ended by a blank or the
 * line's end. *at moves past it. */
static bool SwitchTickNumber(const char* cmd, size_t* at, long* out)
{
	size_t i = *at;
	long v = 0;
	int digits = 0;

	while (cmd[i] == ' ')
		i++;

	while (cmd[i] >= '0' && cmd[i] <= '9')
	{
		v = v * 10 + (cmd[i] - '0');
		i++;

		if (++digits > 5 || v > 65535)
			return false;
	}

	if (!digits || (cmd[i] && cmd[i] != ' '))
		return false;

	*at = i;
	*out = v;
	return true;
}

int RngTraceSwitch(const char* cmd, long* from, long* to)
{
	size_t at = SwitchWordEnd(cmd, "rngtrace");
	long a, b;

	if (!at)
		return 0;

	if (!SwitchTickNumber(cmd, &at, &a) || !SwitchTickNumber(cmd, &at, &b) ||
		a > b)
		return -1;

	*from = a;
	*to = b;
	return 1;
}

#ifdef _WIN64
// The free and the held bytes below the line, by one walk.
static void HighCount(uint64_t* freeBytes, uint64_t* heldBytes)
{
	MEMORY_BASIC_INFORMATION mbi;
	uint64_t at = 0;

	*freeBytes = 0;
	*heldBytes = 0;

	while (at < HIGH_LINE)
	{
		if (!VirtualQuery((const void*)(uintptr_t)at, &mbi, sizeof(mbi)))
			break;

		const uint64_t base = (uint64_t)(uintptr_t)mbi.BaseAddress;
		uint64_t end = base + mbi.RegionSize;

		if (!mbi.RegionSize || end <= at)
			break;

		const uint64_t below = (end > HIGH_LINE ? HIGH_LINE : end) - base;

		if (mbi.State == MEM_FREE)
			*freeBytes += below;
		else
			*heldBytes += below;

		at = end;
	}
}
#endif

bool HighReserveBelow4G(HighReserve* out)
{
	memset(out, 0, sizeof(*out));

#ifndef _WIN64
	return false;
#else
	SYSTEM_INFO info;
	MEMORY_BASIC_INFORMATION mbi;
	uint64_t at = 0, freeBytes;

	if (g_ran)
	{
		*out = g_done;
		return true;
	}

	GetSystemInfo(&info);
	HighCount(&freeBytes, &out->held);

	/* The walk of hd\hd_memory.cpp, stopped at 4 GB: one query per region.
	 * A region reserved here is not free any more when the walk queries the
	 * address again, so every free region is met once. */
	while (at < HIGH_LINE)
	{
		if (!VirtualQuery((const void*)(uintptr_t)at, &mbi, sizeof(mbi)))
			break;

		const uint64_t base = (uint64_t)(uintptr_t)mbi.BaseAddress;
		const uint64_t end = base + mbi.RegionSize;
		uint64_t from, size;

		if (!mbi.RegionSize || end <= at)
			break;

		if (mbi.State == MEM_FREE && HighReserveSpan(base, mbi.RegionSize,
			HIGH_LINE, info.dwAllocationGranularity, info.dwPageSize, &from,
			&size))
		{
			void* got = VirtualAlloc((void*)(uintptr_t)from, (SIZE_T)size,
				MEM_RESERVE, PAGE_NOACCESS);

			if (got == (void*)(uintptr_t)from)
			{
				out->regions++;
				out->bytes += size;
			}
			else
			{
				if (got)
					VirtualFree(got, 0, MEM_RELEASE);

				out->failed++;
			}
		}

		at = end;
	}

	HighCount(&out->left, &freeBytes);
	g_done = *out;
	g_ran = true;
	return true;
#endif
}

const HighReserve* HighReserveDone()
{
	return g_ran ? &g_done : 0;
}

void* HighPoolTake(size_t size)
{
	return VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN,
		PAGE_READWRITE);
}

void HighPoolRelease(void* pool)
{
	if (pool)
		VirtualFree(pool, 0, MEM_RELEASE);
}

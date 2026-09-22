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
#include "hd_memory.h"

// NOT the engine's pch.h, and no engine header at all: this file is compiled
// into Preparation\Tools\Diagnose\PoolProbe.exe as well, so that the tool
// which measures the address space and the engine which spends it run the
// same walk and the same rule. Two implementations of one rule are two
// implementations that will one day disagree.
#include <windows.h>

void HDMemoryMeasure(HDMemoryRoom* out)
{
	if (!out)
		return;

	out->known = 0;
	out->availVirtual = 0;
	out->largestFree = 0;
	out->freeRegions = 0;
	out->regions = 0;
	out->walkMs = 0;

	MEMORYSTATUSEX status;

	// dwLength FIRST, or the call refuses - it is how the structure says
	// which version of itself it is.
	status.dwLength = sizeof(status);

	if (!GlobalMemoryStatusEx(&status))
		return;		// known stays 0 and the rule falls back - hd_memory.h

	out->availVirtual = (double)status.ullAvailVirtual;
	out->known = 1;

	/* The walk, and what it is for. ullAvailVirtual above is a SUM, and an
	 * allocation is not made out of a sum: one page of the cache needs one
	 * free block of its whole size. So every region of this process's user
	 * address space is stepped through once and the biggest free one is
	 * kept.
	 *
	 * From 0 to lpMaximumApplicationAddress, which on this 32 bit process
	 * with LARGEADDRESSAWARE is just under 4 GiB. The step is the region
	 * size VirtualQuery reports, so a run of identical pages is one query
	 * and not a query per page; a region of 0 or one that does not move
	 * forward ends the walk rather than spinning, and the last region of a
	 * LARGEADDRESSAWARE process ends exactly at the top of the address
	 * space, where the pointer wraps to 0.
	 *
	 * What it costs is measured here rather than assumed, because it is a
	 * system call per region and the level build prints it. */
	SYSTEM_INFO info;

	GetSystemInfo(&info);

	const DWORD began = GetTickCount();
	const char* end = (const char*)info.lpMaximumApplicationAddress;
	const char* at = 0;
	MEMORY_BASIC_INFORMATION mbi;

	while (at < end)
	{
		if (!VirtualQuery(at, &mbi, sizeof(mbi)))
			break;

		out->regions++;

		if (mbi.State == MEM_FREE)
		{
			out->freeRegions++;

			if ((double)mbi.RegionSize > out->largestFree)
				out->largestFree = (double)mbi.RegionSize;
		}

		const char* next = (const char*)mbi.BaseAddress + mbi.RegionSize;

		if (mbi.RegionSize == 0 || next <= at)
			break;

		at = next;
	}

	out->walkMs = (int)(GetTickCount() - began);
}

double HDRedressPageBlock(int dxt5, int side)
{
	if (side <= 0)
		return 0;

	const double texel = dxt5 ? 1.0 : 4.0;

	/* The mip chain is the level itself and a quarter of it again and again,
	 * which comes to four thirds of the largest level - and a DXT5 chain
	 * stops at 4x4 blocks rather than at one texel, so four thirds is the
	 * ceiling for both and the ceiling is what a cap wants. */
	return (double)side * (double)side * texel * 4.0 / 3.0;
}

const char* HDRedressRefusalText(int refused)
{
	switch (refused)
	{
	case HD_REDRESS_OK:		return "nothing";
	case HD_REDRESS_NO_CARD:	return "the card";
	case HD_REDRESS_NO_ADDRESS:	return "the address space";
	case HD_REDRESS_NO_BLOCK:	return "fragmentation";
	}

	return "?";
}

void HDRedressDecide(const HDRedressAsk* ask, HDRedressVerdict* out)
{
	if (!out)
		return;

	out->allowed = 0;
	out->refused = HD_REDRESS_NO_CARD;
	out->spent = 0;
	out->cardRoom = 0;
	out->addrRoom = 0;
	out->fits = 0;

	if (!ask)
		return;

	out->spent = ask->cacheBytes - ask->bytes0;
	out->cardRoom = ask->budgetBytes + ask->allowanceBytes - ask->cacheBytes;

	/* The old rule, which is the FLOOR of the card side and not the ceiling: a
	 * level that has not yet spent the allowance is allowed on the card's
	 * account whatever the budget says. Nobody is stopped earlier than the
	 * old rule alone would stop them. */
	const bool old_rule = out->spent < ask->allowanceBytes;

	if (!ask->room.known)
	{
		/* Nothing measured, nothing loosened. The measure failed, so only the
		 * old rule applies: a rule may not spend an answer it did not get.
		 * The log says which line this was. */
		out->allowed = old_rule ? 1 : 0;
		out->refused = old_rule ? HD_REDRESS_OK : HD_REDRESS_NO_CARD;

		const double left = ask->allowanceBytes - out->spent;

		out->fits = (ask->worstBytes > 0 && left > 0)
			? (int)(left / ask->worstBytes) : 0;
		return;
	}

	out->addrRoom = ask->room.availVirtual - ask->reserveBytes;

	const bool card_ok = out->cardRoom >= ask->worstBytes || old_rule;
	const bool addr_ok = out->addrRoom >= ask->worstBytes;
	const bool block_ok = ask->room.largestFree >= ask->pageBytes;

	out->allowed = (card_ok && addr_ok && block_ok) ? 1 : 0;

	/* Which wall, in the order in which they are walls. The address space is
	 * the hard one - past it nothing can be allocated at all; fragmentation
	 * is the one that stops a single big page while the sum still looks
	 * healthy; the card is the original budget check. */
	out->refused = out->allowed ? HD_REDRESS_OK
		: !addr_ok ? HD_REDRESS_NO_ADDRESS
		: !block_ok ? HD_REDRESS_NO_BLOCK
		: HD_REDRESS_NO_CARD;

	/* How many more fit, for the log: the smaller of the two rooms divided by
	 * one worst change. The card side counts the allowance it has not spent
	 * yet when that is the larger of the two, because that is what the old
	 * rule lets it spend. */
	double card_left = out->cardRoom;
	const double allowance_left = ask->allowanceBytes - out->spent;

	if (allowance_left > card_left)
		card_left = allowance_left;

	double left = card_left < out->addrRoom ? card_left : out->addrRoom;

	if (!block_ok)
		left = 0;

	out->fits = (ask->worstBytes > 0 && left > 0)
		? (int)(left / ask->worstBytes) : 0;
}

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

/* What this process has left, and the rule that spends it.
 *
 * The HD page cache never gives a page back inside a level, so every outfit
 * the player puts on in one level stays in it (see HDPageCacheAppend, hd_pagecache.h).
 * A flat 64 MiB cap on that growth is the slack AutoBudgetBytes already held
 * back on the card - but a real budget of 3878 MiB, a cache of 465 to 517
 * MiB and six changes of 4 to 13 MiB each can still hit "spent 52 of 64 MiB"
 * and stop the player there, with the card three gigabytes from being full.
 * That number alone is not the wall that matters.
 *
 * The wall that matters is not the card. This engine is a 32 BIT process
 * with LARGEADDRESSAWARE set (specific\alexstuff.cpp and the .vcxproj): four
 * gigabytes of user address space, whatever the machine has. A card can hold
 * 16 GiB and still answer only 4068 MiB to GetAvailableTextureMem, because
 * that is the largest number the 32 bit call can say, on a machine with far
 * more RAM than that besides. Nothing in the engine measured the address
 * space before this rule existed, and nothing may spend what it has not
 * measured.
 *
 * Two numbers say it and they are not the same number:
 *
 *   availVirtual  the free user address space ADDED UP - what
 *                 GlobalMemoryStatusEx puts in ullAvailVirtual. One call.
 *   largestFree   the biggest single free block in it, which one walk with
 *                 VirtualQuery finds. An allocation needs ONE block of its
 *                 size: a process with 900 MiB free in ten thousand crumbs
 *                 cannot make an 85 MiB texture, and the sum would have said
 *                 it could.
 *
 * Neither is asked per frame. The walk is thousands of system calls and the
 * F4 window asks whether another change is allowed on every drawn frame, so
 * the answer is measured when something has CHANGED it - at the end of a
 * level build, after a re-dress, and once when the window opens - and stored
 * (hd_level.cpp, HDLevelRedressAllowed).
 */

struct HDMemoryRoom
{
	int known;		// 0 = GlobalMemoryStatusEx refused - see HDRedressDecide
	double availVirtual;	// bytes of free user address space, all of it
	double largestFree;	// the biggest single free block of it
	int freeRegions;	// free blocks the walk counted - the fragmentation
	int regions;		// regions it stepped through, free or not
	int walkMs;		// what the walk cost, for the log
};

/* Measures both, now. Costs one GlobalMemoryStatusEx and one VirtualQuery per
 * region of the address space; measured with PoolProbe, and the walk's own
 * cost is printed in the log of every level build.
 * A null pointer is ignored; a failed GlobalMemoryStatusEx leaves known at 0
 * and the walk is not made at all, because a caller that cannot have the sum
 * has no use for the largest block either. */
void HDMemoryMeasure(HDMemoryRoom* out);

/* The allowance: the 64 MiB of slack AutoBudgetBytes holds back on the card,
 * which is what a level is allowed to spend on outfit changes on the card's
 * account alone. It is the FLOOR of the card side of the rule: whatever the
 * address space says, a level that has not yet spent this much is never
 * refused for the card's sake. */
#define HD_REDRESS_ALLOWANCE		(64.0 * 1048576.0)

/* The dearest single change, measured with ModelProbe -pagelist over ANDREA1
 * and a wardrobe of 55 outfits: one outfit's OWN pages - the ones the rest of
 * the wardrobe has not got - come to at most 20.00 MiB, and the dearest
 * change measured between two of them added 13.33 MiB. The rule asks whether
 * THIS much room is left, not whether the chosen outfit fits, and the reason
 * is in hd_level.h. */
#define HD_REDRESS_WORST		(20.0 * 1048576.0)

/* What is not ours to spend. A level load, a Bink film, the save screenshot
 * and the driver's own transient allocations all come out of the same address
 * space, and a re-dress that left them nothing would trade a change of outfit
 * for a crash in the next corridor. This is a FIRST SETTING: every level
 * build prints it in the log beside what was actually free, for comparison. */
#define HD_REDRESS_ADDRESS_RESERVE	(512.0 * 1048576.0)

/* The biggest single allocation one page costs: one texture of `side` by
 * `side` with its whole mip chain, four bytes a texel or one when dxt5 is set.
 * The widest pages of the remaster are 4096 across (eleven of 5\TEX, eight
 * of 4\TEX, all of them 4096x2048: TrgProbe -ddssizes); a square of that
 * side, the bound the caller passes, is 85.33 MiB in A8R8G8B8 and 21.33 MiB
 * in BC3 - the two numbers the fragmentation condition is asked against, and
 * twice what such a page really takes. Measured against the real thing with
 * PoolProbe -pool. */
double HDRedressPageBlock(int dxt5, int side);

// Which of the three conditions refused, for the log and for the grid.
#define HD_REDRESS_OK		0
#define HD_REDRESS_NO_CARD	1	// the card's budget, with the old rule
#define HD_REDRESS_NO_ADDRESS	2	// the address space, added up
#define HD_REDRESS_NO_BLOCK	3	// fragmentation: no block big enough

const char* HDRedressRefusalText(int refused);

/* Everything the rule reads, so that it reads NOTHING else: no globals, no
 * device, no clock. That is what lets PoolProbe -grid put any set of numbers
 * through the very function the engine runs. */
struct HDRedressAsk
{
	double cacheBytes;	// HDPageCacheBytes() now
	double bytes0;		// what this level's own build left in the cache
	double budgetBytes;	// the page cache's budget for this level
	double allowanceBytes;	// HD_REDRESS_ALLOWANCE
	double worstBytes;	// HD_REDRESS_WORST
	double reserveBytes;	// HD_REDRESS_ADDRESS_RESERVE
	double pageBytes;	// the biggest single allocation one page costs
	HDMemoryRoom room;	// what HDMemoryMeasure said
};

struct HDRedressVerdict
{
	int allowed;
	int refused;		// HD_REDRESS_*, HD_REDRESS_OK when allowed
	double spent;		// what this level's changes have added
	double cardRoom;	// budget + allowance - cache
	double addrRoom;	// availVirtual - reserve, 0 when nothing is known
	int fits;		// how many more changes of worstBytes there is room for
};

/* The rule. Pure: the same inputs give the same verdict on any machine, and
 * the arithmetic is in doubles because four gigabytes do not fit a signed
 * 32 bit int.
 *
 *   spent      cacheBytes - bytes0
 *   cardRoom   budgetBytes + allowance - cacheBytes. The allowance is added
 *              because it IS the slack the budget holds back: the 64 MiB of
 *              the allowance and the 64 MiB of AutoBudgetBytes are the same
 *              bytes, and spending them once is not spending them twice.
 *   addrRoom   availVirtual - reserve
 *
 *   allowed = (cardRoom >= worst || spent < allowance)   the card, with the
 *                                                        old rule as its floor
 *          && addrRoom >= worst                          the address space
 *          && largestFree >= pageBytes                   one block for one page
 *
 * WITH NOTHING MEASURED it is exactly the old rule - spent < allowance and
 * nothing else. A rule may not be loosened on an answer it did not get. */
void HDRedressDecide(const HDRedressAsk* ask, HDRedressVerdict* out);

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

/* WHAT THIS PROCESS HAS LEFT, AND THE RULE THAT SPENDS IT (#164).
 *
 * The HD page cache never gives a page back inside a level, so every outfit
 * the player puts on in one level stays in it (hd_pagecache.cpp, the A2 note).
 * Round M3 (#153) capped that growth at a flat 64 MiB, which was the slack
 * AutoBudgetBytes already held back on the card. The user's log of 2026-09-15
 * shows what that costs him: a budget of 3878 MiB, a cache of 465 to 517 MiB
 * and six changes of 4 to 13 MiB each - and the menu stopping him at
 * "spent 52 of 64 MiB", with the card three gigabytes from being full.
 *
 * SO THE QUESTION IS WHICH WALL IS REALLY IN FRONT OF HIM, and it is not the
 * card. This engine is a 32 BIT process with LARGEADDRESSAWARE set (specific\
 * alexstuff.cpp and the .vcxproj): four gigabytes of user address space,
 * whatever the machine has. His card holds 16 GiB and answers 4068 MiB to
 * GetAvailableTextureMem because that is the largest number the 32 bit call
 * can say; his machine has 96 GiB of RAM. Nothing in the engine measured the
 * address space until this round, and nothing may spend what it has not
 * measured.
 *
 * TWO NUMBERS SAY IT AND THEY ARE NOT THE SAME NUMBER:
 *
 *   availVirtual  the free user address space ADDED UP - what
 *                 GlobalMemoryStatusEx puts in ullAvailVirtual. One call.
 *   largestFree   the biggest single free block in it, which one walk with
 *                 VirtualQuery finds. An allocation needs ONE block of its
 *                 size: a process with 900 MiB free in ten thousand crumbs
 *                 cannot make an 85 MiB texture, and the sum would have said
 *                 it could.
 *
 * NEITHER IS ASKED PER FRAME. The walk is thousands of system calls and the
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
 * region of the address space; measured on the user's machine with PoolProbe,
 * and the number is in MODIFICATIONS.md and in the log of every level build.
 * A null pointer is ignored; a failed GlobalMemoryStatusEx leaves known at 0
 * and the walk is not made at all, because a caller that cannot have the sum
 * has no use for the largest block either. */
void HDMemoryMeasure(HDMemoryRoom* out);

/* THE ALLOWANCE (#153, round M3; #164). The 64 MiB of slack AutoBudgetBytes
 * holds back on the card, which is what a level was allowed to spend on
 * outfit changes before this round. It is still the FLOOR of the card side of
 * the rule: whatever the address space says, a level that has not yet spent
 * this much is never refused for the card's sake, so no player is stopped
 * earlier than he was in round M3. */
#define HD_REDRESS_ALLOWANCE		(64.0 * 1048576.0)

/* THE DEAREST SINGLE CHANGE, measured (#153, round M3, with ModelProbe
 * -pagelist over ANDREA1 and the user's own wardrobe of 55 outfits): one
 * outfit's OWN pages - the ones the rest of the wardrobe has not got - come
 * to at most 20.00 MiB, and the dearest change measured between two of them
 * added 13.33 MiB. The rule asks whether THIS much room is left, not whether
 * the chosen outfit fits, and the reason is in hd_level.h. */
#define HD_REDRESS_WORST		(20.0 * 1048576.0)

/* WHAT IS NOT OURS TO SPEND. A level load, a Bink film, the save screenshot
 * and the driver's own transient allocations all come out of the same address
 * space, and a re-dress that left them nothing would trade a change of outfit
 * for a crash in the next corridor. This is a FIRST SETTING and is meant to
 * be read against the user's next log, where every level build prints it
 * beside what was actually free. */
#define HD_REDRESS_ADDRESS_RESERVE	(512.0 * 1048576.0)

/* THE BIGGEST SINGLE ALLOCATION ONE PAGE COSTS: one texture of `side` by
 * `side` with its whole mip chain, four bytes a texel or one when dxt5 is set.
 * Eleven pages of the remaster are 4096x4096 (hd_render.cpp, where the
 * device's own cap enters the policy), which is 85.33 MiB in A8R8G8B8 and
 * 21.33 MiB in BC3 - the two numbers the fragmentation condition is asked
 * against. Measured against the real thing with PoolProbe -pool; what the
 * driver puts on top of it is in MODIFICATIONS.md. */
double HDRedressPageBlock(int dxt5, int side);

// Which of the three conditions refused, for the log and for the grid.
#define HD_REDRESS_OK		0
#define HD_REDRESS_NO_CARD	1	// the card's budget, with the old rule
#define HD_REDRESS_NO_ADDRESS	2	// the address space, added up
#define HD_REDRESS_NO_BLOCK	3	// fragmentation: no block big enough

const char* HDRedressRefusalText(int refused);

/* Everything the rule reads, so that it reads NOTHING else: no globals, no
 * device, no clock. That is what lets PoolProbe -grid put the user's own
 * numbers through the very function the engine runs. */
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

/* THE RULE. Pure: the same inputs give the same verdict on any machine, and
 * the arithmetic is in doubles because four gigabytes do not fit a signed
 * 32 bit int.
 *
 *   spent      cacheBytes - bytes0, as in round M3
 *   cardRoom   budgetBytes + allowance - cacheBytes. The allowance is added
 *              because it IS the slack the budget holds back: the 64 MiB of
 *              round M3 and the 64 MiB of AutoBudgetBytes are the same bytes,
 *              and spending them once is not spending them twice.
 *   addrRoom   availVirtual - reserve
 *
 *   allowed = (cardRoom >= worst || spent < allowance)   the card, with the
 *                                                        old rule as its floor
 *          && addrRoom >= worst                          the address space
 *          && largestFree >= pageBytes                   one block for one page
 *
 * WITH NOTHING MEASURED it is exactly the rule of round M3 - spent < allowance
 * and nothing else. A rule may not be loosened on an answer it did not get. */
void HDRedressDecide(const HDRedressAsk* ask, HDRedressVerdict* out);

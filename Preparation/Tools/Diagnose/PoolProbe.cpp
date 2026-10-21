/*
 * PoolProbe - what one page of the HD cache really costs a 32 bit process,
 * and the rule that spends it.
 *
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
 *
 * The engine is x86 with LARGEADDRESSAWARE: four gigabytes of address space,
 * whatever the card holds. A card with 16 GiB of memory and a machine with
 * 96 GiB of RAM can still stop the HD texture cache at "spent 52 of 64 MiB"
 * with three gigabytes of texture budget unused, once the cache's own cap
 * is raised onto the address space instead of the card. So the first
 * question is whether the address space is really the wall, and a wall
 * nobody has measured is a guess. This tool measures it.
 *
 *   -pool   makes a Direct3D 9 device and then real cache pages - DXT5 and
 *           A8R8G8B8, 4096 and 2048 a side, whole mip chains - in
 *           D3DPOOL_MANAGED and in D3DPOOL_DEFAULT, and prints what each one
 *           takes out of the address space: the sum (GlobalMemoryStatusEx),
 *           the largest free block (a VirtualQuery walk), the process's own
 *           commit, and what the card says it has left. It also prints the
 *           blocks one page reserves, which is what the fragmentation
 *           condition of the rule is asked against, and it releases
 *           everything again and says how much came back.
 *
 *   -grid   the rule itself, on a grid of representative numbers - no
 *           device, no data, no game. It calls HDRedressDecide, the very
 *           function the engine calls, so the table this prints is not a
 *           description of the rule but the rule's own answers.
 *
 *   -walk   the VirtualQuery walk on its own, ten times, because the level
 *           build pays for it and a cost nobody timed is another guess.
 *
 * It links the engine's own specific\hd\hd_memory.cpp - no copy of it here -
 * so the walk this tool times and the rule this tool tabulates are the walk
 * and the rule the engine runs.
 *
 * It touches no game data at all: no level, no .TRM, no .DDS, no registry.
 * A window that is never shown, a device, textures of the right shape filled
 * with nothing, and the process's own memory counters.
 *
 * Usage
 *
 *   PoolProbe -pool [-count N] [-side N] [-only managed|default|systemmem]
 *   PoolProbe -grid
 *   PoolProbe -walk [-count N]
 *
 *     -count N   how many pages per row (-pool, default 8), or how many walks
 *                (-walk, default 10)
 *     -side N    only this side (4096 or 2048)
 *     -only      only this pool
 *
 * Build: bpool.bat beside this file. x86, like the engine that has to live
 * with these numbers - the answer is a different one at 64 bit.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>

#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_memory.h"

static const double kMiB = 1048576.0;

/* ----------------------------------------------------------- the census -- */

/* The walk in hd_memory.cpp answers the two numbers the RULE needs. This one
 * answers a different question, and only this tool asks it: WHICH regions
 * appeared when one page was made. A page that takes 85 MiB out of the sum in
 * one reserve and a page that takes it in a hundred are the same number and
 * not the same fact - the second one fragments the address space and the
 * first one does not. */
struct Region
{
	size_t base;
	size_t size;
	DWORD state;
	DWORD type;
};

static void census(std::vector<Region>& out)
{
	SYSTEM_INFO info;

	GetSystemInfo(&info);
	out.clear();

	const char* end = (const char*)info.lpMaximumApplicationAddress;
	const char* at = 0;
	MEMORY_BASIC_INFORMATION mbi;

	while (at < end)
	{
		if (!VirtualQuery(at, &mbi, sizeof(mbi)))
			break;

		Region region;

		region.base = (size_t)mbi.BaseAddress;
		region.size = (size_t)mbi.RegionSize;
		region.state = mbi.State;
		region.type = mbi.Type;
		out.push_back(region);

		const char* next = (const char*)mbi.BaseAddress + mbi.RegionSize;

		if (mbi.RegionSize == 0 || next <= at)
			break;

		at = next;
	}
}

// The regions that are in `now` and were free (or not there at all) in `was`.
static void newly_taken(const std::vector<Region>& was,
	const std::vector<Region>& now, std::vector<Region>& out)
{
	out.clear();

	for (size_t i = 0; i < now.size(); i++)
	{
		if (now[i].state == MEM_FREE)
			continue;

		bool taken_before = false;

		for (size_t j = 0; j < was.size(); j++)
		{
			if (was[j].state == MEM_FREE)
				continue;

			const size_t a0 = was[j].base;
			const size_t a1 = was[j].base + was[j].size;

			if (now[i].base >= a0 && now[i].base < a1)
			{
				taken_before = true;
				break;
			}
		}

		if (!taken_before)
			out.push_back(now[i]);
	}
}

static double taken_bytes(const std::vector<Region>& regions)
{
	double sum = 0;

	for (size_t i = 0; i < regions.size(); i++)
		sum += (double)regions[i].size;

	return sum;
}

static double commit_bytes()
{
	PROCESS_MEMORY_COUNTERS_EX counters;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);

	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return 0;

	return (double)counters.PrivateUsage;
}

/* --------------------------------------------------------------- d3d ----- */

static HWND g_window = 0;
static IDirect3D9* g_d3d = 0;
static IDirect3DDevice9* g_device = 0;

static bool make_device()
{
	WNDCLASSA cls;
	D3DPRESENT_PARAMETERS pp;

	memset(&cls, 0, sizeof(cls));
	cls.lpfnWndProc = DefWindowProcA;
	cls.hInstance = GetModuleHandleA(0);
	cls.lpszClassName = "PoolProbeWindow";
	RegisterClassA(&cls);

	// Never shown: WS_POPUP without WS_VISIBLE, one pixel off screen. The
	// same window ModelProbe makes, for the same reason.
	g_window = CreateWindowA("PoolProbeWindow", "PoolProbe", WS_POPUP,
		-32, -32, 16, 16, 0, 0, cls.hInstance, 0);

	if (!g_window)
	{
		printf("PoolProbe: CreateWindow failed\n");
		return false;
	}

	g_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!g_d3d)
	{
		printf("PoolProbe: Direct3DCreate9 failed\n");
		return false;
	}

	memset(&pp, 0, sizeof(pp));
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferWidth = 16;
	pp.BackBufferHeight = 16;
	pp.hDeviceWindow = g_window;
	pp.EnableAutoDepthStencil = FALSE;

	if (FAILED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp,
		&g_device)))
	{
		printf("PoolProbe: CreateDevice(HAL, hardware vertex processing) failed\n");
		return false;
	}

	return true;
}

static void drop_device()
{
	if (g_device) { g_device->Release(); g_device = 0; }
	if (g_d3d) { g_d3d->Release(); g_d3d = 0; }
	if (g_window) { DestroyWindow(g_window); g_window = 0; }
}

/* --------------------------------------------------------------- pool ---- */

struct Row
{
	const char* pool;
	const char* format;
	int side;
	int wanted;
	int made;
	double firstBlock;	// what the FIRST page took out of the sum
	double firstRegions;	// how many regions that one page reserved
	double biggestRegion;	// the biggest single one of them
	double perPage;		// the sum's drop divided by the pages made
	double commitPerPage;
	double largestFree0;
	double largestFree1;
	double cardBefore;	// what the card said it had
	double cardAfter;
	double cameBack;	// of the sum, after releasing everything
	int walkMs;
};

static void say_row(const Row& row)
{
	printf("%-8s %-9s %4d  %3d/%-3d  %8.2f  %8.2f %5.0f  %8.2f  %8.2f  "
		"%8.2f %8.2f  %8.2f  %8.2f  %4d\n",
		row.pool, row.format, row.side, row.made, row.wanted,
		row.firstBlock / kMiB, row.biggestRegion / kMiB, row.firstRegions,
		row.perPage / kMiB, row.commitPerPage / kMiB,
		row.largestFree0 / kMiB, row.largestFree1 / kMiB,
		(row.cardBefore - row.cardAfter) / kMiB, row.cameBack / kMiB,
		row.walkMs);
}

static void one_pool(D3DPOOL pool, const char* poolName, D3DFORMAT format,
	const char* formatName, int side, int count)
{
	std::vector<IDirect3DTexture9*> made;
	std::vector<Region> before, after, fresh;
	HDMemoryRoom room0, room1, room2;
	Row row;

	memset(&row, 0, sizeof(row));
	row.pool = poolName;
	row.format = formatName;
	row.side = side;
	row.wanted = count;

	HDMemoryMeasure(&room0);
	census(before);

	const double commit0 = commit_bytes();

	row.cardBefore = (double)g_device->GetAvailableTextureMem();
	row.largestFree0 = room0.largestFree;
	row.walkMs = room0.walkMs;

	/* The first page on its own, because the two questions are different: what
	 * ONE page costs is the fragmentation condition's number, what the NEXT
	 * seven cost is the price the rule spends. */
	for (int i = 0; i < count; i++)
	{
		IDirect3DTexture9* texture = 0;

		// levels = 0 is the whole mip chain, which is what the cache asks
		// for (hd_pagecache.cpp hands in the chain it decoded).
		if (FAILED(g_device->CreateTexture((UINT)side, (UINT)side, 0, 0,
			format, pool, &texture, 0)) || !texture)
		{
			printf("  %s %s %d: CreateTexture refused at page %d of %d\n",
				poolName, formatName, side, i + 1, count);
			break;
		}

		made.push_back(texture);

		if (i == 0)
		{
			HDMemoryMeasure(&room1);
			census(after);
			newly_taken(before, after, fresh);

			row.firstBlock = room0.availVirtual - room1.availVirtual;
			row.firstRegions = (double)fresh.size();
			row.biggestRegion = 0;

			for (size_t r = 0; r < fresh.size(); r++)
				if ((double)fresh[r].size > row.biggestRegion)
					row.biggestRegion = (double)fresh[r].size;

			// What the census says the one page reserved, against what the
			// sum lost. They agree when nothing else in the process moved.
			const double census_bytes = taken_bytes(fresh);

			if (fresh.size() && census_bytes > 0)
				row.firstBlock = census_bytes;
		}
	}

	row.made = (int)made.size();

	HDMemoryMeasure(&room2);

	row.cardAfter = (double)g_device->GetAvailableTextureMem();
	row.largestFree1 = room2.largestFree;
	row.perPage = row.made
		? (room0.availVirtual - room2.availVirtual) / (double)row.made : 0;
	row.commitPerPage = row.made
		? (commit_bytes() - commit0) / (double)row.made : 0;

	for (size_t i = 0; i < made.size(); i++)
		made[i]->Release();

	made.clear();

	// A managed texture is not necessarily gone the moment it is released -
	// this says whether the address space comes back at once.
	HDMemoryRoom room3;

	HDMemoryMeasure(&room3);
	row.cameBack = room3.availVirtual - room2.availVirtual;

	say_row(row);
}

static void pool_run(int count, int onlySide, const char* onlyPool)
{
	if (!make_device())
		return;

	D3DCAPS9 caps;

	memset(&caps, 0, sizeof(caps));
	g_device->GetDeviceCaps(&caps);

	HDMemoryRoom room;

	HDMemoryMeasure(&room);

	printf("PoolProbe -pool: %d pages a row, the card says %.0f MiB, its "
		"largest texture is %ux%u\n", count,
		(double)g_device->GetAvailableTextureMem() / kMiB,
		caps.MaxTextureWidth, caps.MaxTextureHeight);
	printf("the process: avail_virtual %.0f MiB, largest free block %.0f MiB, "
		"%d regions (%d free), the walk took %d ms\n\n",
		room.availVirtual / kMiB, room.largestFree / kMiB, room.regions,
		room.freeRegions, room.walkMs);

	printf("pool     format    side  made     1st page  biggest  reg  "
		"per page  commit/pg  free blk0  free blk1  card/page  came back  "
		"walk\n");
	printf("                               "
		"      MiB      MiB           MiB        MiB        MiB        MiB"
		"        MiB        MiB    ms\n");

	const int sides[2] = { 4096, 2048 };

	for (int s = 0; s < 2; s++)
	{
		if (onlySide && sides[s] != onlySide)
			continue;

		for (int f = 0; f < 2; f++)
		{
			const D3DFORMAT format = f ? D3DFMT_A8R8G8B8 : D3DFMT_DXT5;
			const char* formatName = f ? "a8r8g8b8" : "dxt5";

			if (!onlyPool || _stricmp(onlyPool, "default") == 0)
				one_pool(D3DPOOL_DEFAULT, "default", format, formatName,
					sides[s], count);

			if (!onlyPool || _stricmp(onlyPool, "managed") == 0)
				one_pool(D3DPOOL_MANAGED, "managed", format, formatName,
					sides[s], count);

			/* And the staging texture's own pool. The engine's pages are
			 * D3DPOOL_DEFAULT and cannot be locked, so every mip goes
			 * through one reused D3DPOOL_SYSTEMMEM texture of the page's
			 * whole size (hd_pagecache.cpp, ensure_staging). That single
			 * texture is the biggest allocation a page load makes in the
			 * address space, which is what the rule's fragmentation
			 * condition is really about. */
			if (!onlyPool || _stricmp(onlyPool, "systemmem") == 0)
				one_pool(D3DPOOL_SYSTEMMEM, "systemmem", format, formatName,
					sides[s], count);
		}
	}

	printf("\nwhat one page of the cache's own shape should be, by the rule's "
		"own arithmetic:\n");
	printf("  a8r8g8b8 4096 = %.2f MiB, dxt5 4096 = %.2f MiB, "
		"a8r8g8b8 2048 = %.2f MiB, dxt5 2048 = %.2f MiB\n",
		HDRedressPageBlock(0, 4096) / kMiB, HDRedressPageBlock(1, 4096) / kMiB,
		HDRedressPageBlock(0, 2048) / kMiB, HDRedressPageBlock(1, 2048) / kMiB);

	drop_device();
}

/* --------------------------------------------------------------- walk ---- */

static void walk_run(int count)
{
	printf("PoolProbe -walk: %d walks of the whole user address space\n\n",
		count);

	int worst = 0;
	int sum = 0;

	for (int i = 0; i < count; i++)
	{
		HDMemoryRoom room;

		HDMemoryMeasure(&room);
		sum += room.walkMs;

		if (room.walkMs > worst)
			worst = room.walkMs;

		printf("  %2d: %d regions (%d free), avail_virtual %.0f MiB, "
			"largest free block %.0f MiB, %d ms\n", i + 1, room.regions,
			room.freeRegions, room.availVirtual / kMiB,
			room.largestFree / kMiB, room.walkMs);
	}

	printf("\n  mean %.1f ms, worst %d ms\n",
		count ? (double)sum / (double)count : 0.0, worst);

	/* And the same walk on a clock that can see it. GetTickCount steps in
	 * 15.6 ms, so the walk_ms the engine prints is 0 and 0 is not a
	 * measurement - it only says "under a tick". A hundred walks between two
	 * QueryPerformanceCounter reads say what it really costs, which is what
	 * the level build is paying twice. */
	LARGE_INTEGER frequency, began, ended;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&began);

	for (int i = 0; i < 100; i++)
	{
		HDMemoryRoom room;

		HDMemoryMeasure(&room);
	}

	QueryPerformanceCounter(&ended);

	const double us = frequency.QuadPart
		? (double)(ended.QuadPart - began.QuadPart) * 1000000.0
			/ (double)frequency.QuadPart / 100.0 : 0.0;

	printf("  on a finer clock: %.0f microseconds a walk over 100 of them\n",
		us);
}

/* --------------------------------------------------------------- grid ---- */

static void ask_of(HDRedressAsk& ask, double cacheMiB, double spentMiB,
	double budgetMiB, double availMiB, double largestMiB, int known)
{
	memset(&ask, 0, sizeof(ask));
	ask.cacheBytes = cacheMiB * kMiB;
	ask.bytes0 = (cacheMiB - spentMiB) * kMiB;
	ask.budgetBytes = budgetMiB * kMiB;
	ask.allowanceBytes = HD_REDRESS_ALLOWANCE;
	ask.worstBytes = HD_REDRESS_WORST;
	ask.reserveBytes = HD_REDRESS_ADDRESS_RESERVE;
	ask.pageBytes = HDRedressPageBlock(0, 4096);
	ask.room.known = known;
	ask.room.availVirtual = availMiB * kMiB;
	ask.room.largestFree = largestMiB * kMiB;
}

// The previous rule, kept here for comparison against the current one:
// HDPageCacheBytes() - g_redress_bytes0 < HD_REDRESS_ALLOWANCE
static bool old_rule(double spentMiB)
{
	return spentMiB * kMiB < HD_REDRESS_ALLOWANCE;
}

static void grid_run()
{
	HDRedressAsk ask;
	HDRedressVerdict verdict;

	printf("PoolProbe -grid: HDRedressDecide, the engine's own function.\n");
	printf("the constants: allowance %.0f MiB, worst %.0f MiB, reserve %.0f "
		"MiB, one page %.2f MiB (a8r8g8b8 4096)\n\n",
		HD_REDRESS_ALLOWANCE / kMiB, HD_REDRESS_WORST / kMiB,
		HD_REDRESS_ADDRESS_RESERVE / kMiB, HDRedressPageBlock(0, 4096) / kMiB);

	/* (i) A measured level, with room on both sides: the two rules agree
	 * wherever the old one said yes, and the new one goes on saying yes where
	 * the old one had stopped. Budget 3878 MiB and a cache of 465 MiB are
	 * measured values from a real run. */
	printf("(i) the measured run's numbers - budget 3878 MiB, "
		"cache 465 + spent, avail_virtual 3000 MiB, "
		"largest free block 1000 MiB\n\n");
	printf("  spent  card room  addr room   old   new   refused         fits\n");

	static const double spents[] = { 0, 10, 20, 40, 52, 60, 63, 64, 70, 100, 200 };

	for (int i = 0; i < (int)(sizeof(spents) / sizeof(spents[0])); i++)
	{
		ask_of(ask, 465.0 + spents[i], spents[i], 3878.0, 3000.0, 1000.0, 1);
		HDRedressDecide(&ask, &verdict);

		printf("  %5.0f  %9.0f  %9.0f   %-3s   %-3s   %-14s  %4d\n",
			spents[i], verdict.cardRoom / kMiB, verdict.addrRoom / kMiB,
			old_rule(spents[i]) ? "yes" : "no",
			verdict.allowed ? "yes" : "no",
			HDRedressRefusalText(verdict.refused), verdict.fits);
	}

	/* (ii) The card side on its own. A budget that is nearly spent is what
	 * the old rule was really about, and the new rule may not be stricter
	 * there: with the address space fine, every row the old rule allowed is
	 * allowed. */
	printf("\n(ii) a small budget - cache 465 MiB, avail_virtual 3000 MiB, "
		"largest free block 1000 MiB\n\n");
	printf("  budget  spent  card room   old   new   refused\n");

	/* 455 and 400 are the rows that matter most: there the card room is
	 * smaller than one worst change, and the old rule still says yes - so
	 * the new rule has to say yes too, or a player would be stopped earlier
	 * than the old rule ever stopped them. */
	static const double budgets[] = { 3878, 600, 500, 480, 470, 455, 400 };

	for (int b = 0; b < (int)(sizeof(budgets) / sizeof(budgets[0])); b++)
	{
		for (int s = 0; s < 2; s++)
		{
			const double spent = s ? 70.0 : 40.0;

			ask_of(ask, 465.0 + spent, spent, budgets[b], 3000.0, 1000.0, 1);
			HDRedressDecide(&ask, &verdict);

			printf("  %6.0f  %5.0f  %9.0f   %-3s   %-3s   %s\n", budgets[b],
				spent, verdict.cardRoom / kMiB,
				old_rule(spent) ? "yes" : "no",
				verdict.allowed ? "yes" : "no",
				HDRedressRefusalText(verdict.refused));
		}
	}

	/* (iii) The address space, which is the wall this rule is written for.
	 * The reserve is 512 MiB, so 532 MiB free is the last row that has room
	 * for one worst change and 530 is the first that has not. */
	printf("\n(iii) the address space - budget 3878 MiB, cache 465 MiB, "
		"spent 0, largest free block 1000 MiB\n\n");
	printf("  avail_virtual  addr room   old   new   refused         fits\n");

	static const double avails[] = { 3000, 1000, 600, 533, 532, 531, 530, 300 };

	for (int i = 0; i < (int)(sizeof(avails) / sizeof(avails[0])); i++)
	{
		ask_of(ask, 465.0, 0.0, 3878.0, avails[i], 1000.0, 1);
		HDRedressDecide(&ask, &verdict);

		printf("  %13.0f  %9.0f   %-3s   %-3s   %-14s  %4d\n", avails[i],
			verdict.addrRoom / kMiB, old_rule(0.0) ? "yes" : "no",
			verdict.allowed ? "yes" : "no",
			HDRedressRefusalText(verdict.refused), verdict.fits);
	}

	/* (iv) Fragmentation: the sum can be healthy while no single block can
	 * hold one 85.33 MiB page. */
	printf("\n(iv) fragmentation - budget 3878 MiB, cache 465 MiB, spent 0, "
		"avail_virtual 3000 MiB\n\n");
	printf("  largest free  one page   old   new   refused         fits\n");

	static const double blocks[] = { 1000, 86, 85, 64, 8 };

	for (int i = 0; i < (int)(sizeof(blocks) / sizeof(blocks[0])); i++)
	{
		ask_of(ask, 465.0, 0.0, 3878.0, 3000.0, blocks[i], 1);
		HDRedressDecide(&ask, &verdict);

		printf("  %12.0f  %8.2f   %-3s   %-3s   %-14s  %4d\n", blocks[i],
			HDRedressPageBlock(0, 4096) / kMiB, old_rule(0.0) ? "yes" : "no",
			verdict.allowed ? "yes" : "no",
			HDRedressRefusalText(verdict.refused), verdict.fits);
	}

	/* (v) And with nothing measured, which is what a failed
	 * GlobalMemoryStatusEx leaves: the old rule and nothing else. */
	printf("\n(v) the measure failed (known=0) - the flat allowance and "
		"nothing else\n\n");
	printf("  spent   old   new   refused         fits\n");

	for (int i = 0; i < (int)(sizeof(spents) / sizeof(spents[0])); i++)
	{
		ask_of(ask, 465.0 + spents[i], spents[i], 3878.0, 3000.0, 1000.0, 0);
		HDRedressDecide(&ask, &verdict);

		printf("  %5.0f   %-3s   %-3s   %-14s  %4d\n", spents[i],
			old_rule(spents[i]) ? "yes" : "no",
			verdict.allowed ? "yes" : "no",
			HDRedressRefusalText(verdict.refused), verdict.fits);
	}

	/* (vi) How many changes a level has room for: 3000 MiB free, 512 of them
	 * reserved, 20 MiB a change. */
	ask_of(ask, 465.0, 0.0, 3878.0, 3000.0, 1000.0, 1);
	HDRedressDecide(&ask, &verdict);

	printf("\n(vi) with 3000 MiB of address space free and the measured "
		"run's budget, a level has room for %d changes of %.0f MiB\n",
		verdict.fits,
		HD_REDRESS_WORST / kMiB);
	printf("     (the old rule gave that run %d, and a measured play log was "
		"stopped at the sixth)\n",
		(int)(HD_REDRESS_ALLOWANCE / HD_REDRESS_WORST));
}

/* --------------------------------------------------------------- main ---- */

static void usage()
{
	printf("PoolProbe - what a page costs a 32 bit process, and the "
		"rule that spends it\n\n"
		"  PoolProbe -pool [-count N] [-side N] [-only managed|default|systemmem]\n"
		"  PoolProbe -grid\n"
		"  PoolProbe -walk [-count N]\n");
}

int main(int argc, char** argv)
{
	const char* what = 0;
	const char* onlyPool = 0;
	int count = 0;
	int side = 0;

	for (int i = 1; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-pool") || !_stricmp(argv[i], "-grid")
			|| !_stricmp(argv[i], "-walk"))
			what = argv[i];
		else if (!_stricmp(argv[i], "-count") && i + 1 < argc)
			count = atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-side") && i + 1 < argc)
			side = atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-only") && i + 1 < argc)
			onlyPool = argv[++i];
		else
		{
			printf("PoolProbe: what is %s?\n\n", argv[i]);
			usage();
			return 1;
		}
	}

	if (!what)
	{
		usage();
		return 1;
	}

	if (!_stricmp(what, "-pool"))
		pool_run(count > 0 ? count : 8, side, onlyPool);
	else if (!_stricmp(what, "-walk"))
		walk_run(count > 0 ? count : 10);
	else
		grid_run();

	return 0;
}

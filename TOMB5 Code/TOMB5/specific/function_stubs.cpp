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
#ifdef TOMB5_TEST_BUILD
#include <intrin.h>			// _ReturnAddress - -rngtrace
#endif

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

/* One finished line, `n` bytes with its newline, into log.txt behind the
 * time stamp: Log()'s writer, and -rngtrace's for lines longer than Log()'s
 * buffer. The caller has tested gLogEnabled.
 *
 * The stamp is milliseconds since the first log entry, not a clock time:
 * this shows the gap between two lines, which localizes a startup delay,
 * where a plain ordering of lines would not. Costs one GetTickCount call
 * per line. */
static void LogText(const char* text, size_t n)
{
	static DWORD firstTick = 0;
	char stamp[24];
	DWORD now = GetTickCount();

	if (!logF)
		logF = fopen("log.txt", "w+");

	if (!logF)
		return;

	if (!firstTick)
		firstTick = now;

	sprintf(stamp, "[%7lu] ", (unsigned long)(now - firstTick));
	fwrite(stamp, strlen(stamp), 1, logF);
	fwrite(text, n, 1, logF);
	fflush(logF);
}

#ifdef TOMB5_TEST_BUILD

/* -rngtrace <from> <to> (winmain.cpp reads it, high_pool.h parses it): who
 * draws from the logic generator, by return address. A span runs from one
 * input call (ReplayInputSeam, specific\replay.cpp) to the next and is
 * named by the tick that ends it - GlobalCounter at its closing input call,
 * the tick STATELOG and REPLAYLOG drift name there. A span is recorded when
 * that tick lies in [from, to]: the tick before's body after its input,
 * the frames drawn after it, and this tick's start up to its input.
 *
 * Each GetRandomControl call keeps its return address relative to the
 * module's base, so two runs of one build compare whatever the loader's
 * placement. The first RNGTRACE_CALLS calls of a span are kept, the rest
 * counted. At the closing input call, before STATELOG, the span becomes
 * one RNGTRACE tick= line, then one RNGTRACE section= line per mark
 * (RngTraceMark) with the calls made before it and the generator's state.
 *
 * Without the switch the generator pays one test of a static bool per
 * call: a load and a branch that never goes, beside a multiply. */
#define RNGTRACE_CALLS	65536
#define RNGTRACE_MARKS	256
#define RNGTRACE_LINE	(RNGTRACE_CALLS * 9 + 256)

struct RNGTRACE_MARK
{
	const char* name;
	long at;		// calls of the span before the mark
	long seed;		// the generator's state at the mark
};

static bool rt_armed = false;	// the span being recorded; the one test
static long rt_from = -1;		// the window, -1 without the switch
static long rt_to = -1;
static uintptr_t rt_base;		// GetModuleHandle(0)
static uint32_t* rt_calls;		// RNGTRACE_CALLS return addresses
static char* rt_line;			// RNGTRACE_LINE bytes
static long rt_count;			// calls kept
static long rt_dropped;			// calls past RNGTRACE_CALLS
static long rt_seed_in;			// the state at the span's opening input
static RNGTRACE_MARK rt_marks[RNGTRACE_MARKS];
static long rt_nmarks;
static long rt_marks_dropped;

// Bounded: past RNGTRACE_CALLS a call is only counted.
static __declspec(noinline) void RngTraceNote(void* ret)
{
	if (rt_count < RNGTRACE_CALLS)
		rt_calls[rt_count++] = (uint32_t)((uintptr_t)ret - rt_base);
	else
		rt_dropped++;
}

bool RngTraceArm(long from, long to)
{
	rt_calls = (uint32_t*)malloc(RNGTRACE_CALLS * sizeof(uint32_t));
	rt_line = (char*)malloc(RNGTRACE_LINE);

	if (!rt_calls || !rt_line)
	{
		free(rt_calls);
		free(rt_line);
		rt_calls = 0;
		rt_line = 0;
		return false;
	}

	rt_base = (uintptr_t)GetModuleHandle(0);
	rt_from = from;
	rt_to = to;
	return true;
}

void RngTraceMark(const char* name)
{
	RNGTRACE_MARK* m;

	if (!rt_armed)
		return;

	if (rt_nmarks >= RNGTRACE_MARKS)
	{
		rt_marks_dropped++;
		return;
	}

	m = &rt_marks[rt_nmarks++];
	m->name = name;
	m->at = rt_count + rt_dropped;
	m->seed = rand_1;
}

// The closed span as its lines.
static void RngTraceWrite(long tick)
{
	size_t n;

	n = (size_t)sprintf(rt_line, "RNGTRACE tick=%ld calls=%ld "
		"seed_in=0x%08lX seed_out=0x%08lX", tick, rt_count + rt_dropped,
		(unsigned long)(uint32_t)rt_seed_in,
		(unsigned long)(uint32_t)rand_1);

	if (rt_dropped)
		n += (size_t)sprintf(rt_line + n, " overflow=%ld", rt_dropped);

	n += (size_t)sprintf(rt_line + n, " addrs=");

	for (long i = 0; i < rt_count; i++)
		n += (size_t)sprintf(rt_line + n, i ? ",%x" : "%x",
			(unsigned int)rt_calls[i]);

	rt_line[n++] = '\n';
	LogText(rt_line, n);

	for (long i = 0; i < rt_nmarks; i++)
		Log("RNGTRACE section=%s tick=%ld at=%ld seed=0x%08lX",
			rt_marks[i].name, tick, rt_marks[i].at,
			(unsigned long)(uint32_t)rt_marks[i].seed);

	if (rt_marks_dropped)
		Log("RNGTRACE sections tick=%ld dropped=%ld", tick,
			rt_marks_dropped);
}

void RngTraceInputSeam(long tick, long fmv)
{
	if (rt_from < 0 || fmv || !gLogEnabled)
		return;

	if (rt_armed)
		RngTraceWrite(tick);

	// The span that opens here closes at the input call of tick + 1.
	rt_armed = tick + 1 >= rt_from && tick + 1 <= rt_to;

	if (!rt_armed)
		return;

	rt_count = 0;
	rt_dropped = 0;
	rt_nmarks = 0;
	rt_marks_dropped = 0;
	rt_seed_in = rand_1;
}

#endif	// TOMB5_TEST_BUILD

/* Not inlined in a test build: -rngtrace's return address has to be the
 * caller's (the Release builds link with whole-program optimisation). A
 * shipping build has no -rngtrace and inlines it as before. */
TOMB5_NOINLINE_IN_TEST long GetRandomControl()
{
#ifdef TOMB5_TEST_BUILD
	if (rt_armed)
		RngTraceNote(_ReturnAddress());
#endif

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

	va_start(list, s);
	vsprintf(buf, s, list);
	strcat(buf, "\n");
	va_end(list);

	LogText(buf, strlen(buf));
#endif
}

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
#include "profile.h"
#include "output.h"		// gPresentMinTicks - the budget a late frame is late against
#include "../game/control.h"	// GlobalCounter - the logic tick the line reports

namespace
{
	/* The names, in the order of the enum. They are what the log line is
	 * read with, so they are short and they never change once a report has
	 * quoted them. */
	const char* kName[PROF_COUNT] =
	{
		"logic",
		"gather",
		"rooms",
		"sky",
		"hdroom",
		"hdobj",
		"hdlara",
		"output",
		"hddraw",
		"hdlight",
		"hdobjdraw",
		"buckets",
		"hdglow",
		"sort",
		"sortdraw",
		"spin",
		"flip",
		"clear",
		"begin_wait",
		"begin"
	};

	/* The phases the per-second line and the frame lines list, in order.
	 * CLEAR, BEGIN_WAIT and BEGIN lie outside the work and are printed only
	 * at the end of the pause and late lines (#322), so the other lines
	 * read as they always did. */
	const int kListed = PROF_CLEAR;

	__int64 g_freq = 0;		// 0 = not read yet, -1 = no clock on this machine

	__int64 g_start[PROF_COUNT];	// when the phase last began, 0 = not open
	double g_frame[PROF_COUNT];	// this frame, milliseconds
	double g_sum[PROF_COUNT];	// this second, added up
	double g_max[PROF_COUNT];	// this second, the worst single frame

	double g_frame_sum = 0;		// the frame interval, this second
	double g_frame_max = 0;
	double g_work_sum = 0;		// Begin -> End of one frame, this second
	double g_work_max = 0;

	__int64 g_qpc_frame = 0;	// the previous ProfileFrameEnd, 0 = none
	__int64 g_qpc_begin = 0;	// this frame's ProfileFrameBegin
	__int64 g_qpc_second = 0;	// when the second being filled started
	__int64 g_qpc_level = 0;	// when the level started

	int g_sec_frames = 0;
	int g_sec_intervals = 0;	// frames whose interval was a real one
	int g_sec_late = 0;
	int g_sec_pauses = 0;		// intervals the main loop spent elsewhere
	unsigned short g_sec_counter = 0;	// GlobalCounter when the second began

	int g_presents = 0;		// presents since the last ProfileFrameEnd
	long g_sort_max = 0;		// the frame's longest sort list

	// The fog bulbs' pass (ProfileNoteFog): this frame, and this second.
	double g_fog_cpu = 0, g_fog_gpu = -1;
	double g_fog_cpu_sum = 0, g_fog_cpu_max = 0;
	double g_fog_gpu_sum = 0, g_fog_gpu_max = 0;
	int g_fog_gpu_n = 0;

	/* The park's hand-over (ProfileNoteParked, #322): the milliseconds the
	 * game thread was parked, added up since the process began. Written by
	 * the window thread while the game thread is suspended, read by the
	 * game thread; Interlocked on both sides. */
	volatile LONG g_parked_total = 0;
	LONG g_parked_seen = 0;		// the total at the last ProfileFrameEnd
	LONG g_park_at[PROF_COUNT];	// the total when LOGIC/BEGIN_WAIT opened
	double g_park_in[PROF_COUNT];	// parked inside LOGIC/BEGIN_WAIT, ms
	bool g_in_work = false;		// between ProfileFrameBegin and End

	int g_level_late = 0;		// the level's totals, for the summary
	int g_level_pauses = 0;

	int g_level_second = 0;		// PROFLOG seconds flushed for this level
	int g_level_frames = 0;
	bool g_first_frame = false;	// the next ProfileFrameEnd is a level's first

	char g_level[64] = { 0 };

	bool ClockReady()
	{
		if (!g_freq)
		{
			LARGE_INTEGER f;

			if (!QueryPerformanceFrequency(&f) || !f.QuadPart)
			{
				g_freq = -1;
				return false;
			}

			g_freq = f.QuadPart;
		}

		return g_freq > 0;
	}

	double Ms(__int64 ticks)
	{
		return (double)ticks * 1000.0 / (double)g_freq;
	}

	LONG ParkedNow()
	{
		return InterlockedCompareExchange(&g_parked_total, 0, 0);
	}

	// The phases a park can fall into: the spin it suspends the thread in,
	// and ControlPhase, whose modal loops have spins of their own.
	bool ParkPhase(int phase)
	{
		return phase == PROF_LOGIC || phase == PROF_BEGIN_WAIT;
	}

	// A phase that belongs to the interval outside the work, not to it.
	bool OutsidePhase(int phase)
	{
		return phase == PROF_LOGIC || phase >= PROF_CLEAR;
	}

	/* The end of the pause and late lines (#322): the interval outside the
	 * work, split. Each phase is taken net of the park inside it, so the
	 * six are disjoint; `rest` is what none of them covers, and it is left
	 * signed - a few milliseconds below zero is the tick clock's grain of
	 * `parked`, anything more is a stamp counted twice. */
	void OutsideLine(char* out, size_t size, double frame_ms,
		double work_ms, double parked)
	{
		double logic = g_frame[PROF_LOGIC] - g_park_in[PROF_LOGIC];
		double wait = g_frame[PROF_BEGIN_WAIT] - g_park_in[PROF_BEGIN_WAIT];

		if (logic < 0)
			logic = 0;

		if (wait < 0)
			wait = 0;

		_snprintf_s(out, size, _TRUNCATE, "logic=%.2f clear=%.2f "
			"begin_wait=%.2f begin=%.2f parked=%.2f rest=%.2f", logic,
			g_frame[PROF_CLEAR], wait, g_frame[PROF_BEGIN], parked,
			frame_ms - work_ms - logic - g_frame[PROF_CLEAR] - wait -
			g_frame[PROF_BEGIN] - parked);
	}

	/* Everything classic the output phase did: the buckets, the sort list,
	 * the binocular mask, the pickup display and the fade. HD_DRAW and
	 * HD_GLOW are the only two phases that run inside S_OutputPolyList and
	 * nowhere else, so they are the only two that may be taken off it -
	 * BUCKETS, SORT and SORTDRAW are whole-frame totals that also cover
	 * the sky phase, and subtracting them would make this read zero in
	 * every outdoor level.
	 * HD_OBJDRAW and HD_LIGHT sit inside HD_DRAW and are not subtracted
	 * twice. Clamped because the three clocks are read at different
	 * moments and a rounding of a few microseconds must not print a
	 * negative. */
	double OutRest(const double* p)
	{
		double rest = p[PROF_OUTPUT] - p[PROF_HD_DRAW] - p[PROF_HD_GLOW];

		return rest > 0 ? rest : 0;
	}

	// "name=%.1f " for every phase, into one buffer. Log() takes a format, so
	// the line is built first and handed over as a single string.
	void OneFrameLine(char* out, size_t size, const double* p)
	{
		int at = 0;

		for (int i = 0; i < kListed && at >= 0 && (size_t)at < size; i++)
			at += _snprintf_s(out + at, size - at, _TRUNCATE, "%s=%.2f ",
				kName[i], p[i]);

		if (at >= 0 && (size_t)at < size)
			_snprintf_s(out + at, size - at, _TRUNCATE,
				"outrest=%.2f sortn=%d fog=%.2f fog_gpu=%.2f", OutRest(p),
				(int)g_sort_max, g_fog_cpu, g_fog_gpu);
	}

	/* The slot this frame asked for, in milliseconds. gPresentMinTicks is
	 * how many 1/60 s ticks the present that just ran waited for, so this
	 * is the frame's own budget and not the mode's: one tick for a game
	 * frame at fps_mode 1, two for every modal loop in the build whatever
	 * the mode says. DrawPhaseGame deliberately puts the variable back to
	 * two only after ProfileFrameEnd, so this reads the policy of the
	 * frame being judged.
	 *
	 * The definition lives in specific/output.cpp (PresentSlotMs) rather
	 * than here, because a third case exists - the display-bound frame,
	 * whose slot is the panel's period rather than a whole number of
	 * ticks - and FPSLOG and PROFLOG must not be able to disagree about
	 * it. */
	double BudgetMs()
	{
		return PresentSlotMs();
	}

	// "name=mean/max " for every phase.
	void OneSecondLine(char* out, size_t size, int frames)
	{
		const double n = frames > 0 ? (double)frames : 1.0;
		int at = 0;

		for (int i = 0; i < kListed && at >= 0 && (size_t)at < size; i++)
			at += _snprintf_s(out + at, size - at, _TRUNCATE,
				"%s=%.2f/%.2f ", kName[i], g_sum[i] / n, g_max[i]);

		if (at >= 0 && (size_t)at < size)
			_snprintf_s(out + at, size - at, _TRUNCATE,
				"outrest=%.2f fog=%.2f/%.2f fog_gpu=%.2f/%.2f",
				OutRest(g_sum) / n, g_fog_cpu_sum / n, g_fog_cpu_max,
				g_fog_gpu_n ? g_fog_gpu_sum / g_fog_gpu_n : 0.0,
				g_fog_gpu_max);
	}

	void NewSecond()
	{
		for (int i = 0; i < PROF_COUNT; i++)
		{
			g_sum[i] = 0;
			g_max[i] = 0;
		}

		g_frame_sum = 0;
		g_frame_max = 0;
		g_work_sum = 0;
		g_work_max = 0;
		g_fog_cpu_sum = 0;
		g_fog_cpu_max = 0;
		g_fog_gpu_sum = 0;
		g_fog_gpu_max = 0;
		g_fog_gpu_n = 0;
		g_sec_frames = 0;
		g_sec_intervals = 0;
		g_sec_late = 0;
		g_sec_pauses = 0;
		g_sec_counter = GlobalCounter;
	}

	// at_s is the WHOLE SECONDS since ProfileLevel, handed in so that this
	// line and a late frame's line count from the same place.
	void FlushSecond(int at_s)
	{
		char buf[768];

		if (g_sec_frames <= 0)
			return;

		OneSecondLine(buf, sizeof(buf), g_sec_frames);

		/* logic_ticks is the proof of the clock split. GlobalCounter steps
		 * once per logic tick (game/control.cpp) and nowhere else, so its
		 * difference over one wall-clock second is the simulation's own
		 * rate. At fps_mode 1 this line must read `frames=60
		 * logic_ticks=30`: sixty presents, thirty ticks, and the game
		 * running at exactly the speed it always did. */
		Log("PROFLOG level=%s at=%ds frames=%d logic_ticks=%d fps=%d "
			"budget=%.1f late=%d pauses=%d frame=%.2f/%.2f work=%.2f/%.2f %s",
			g_level[0] ? g_level : "?", at_s, g_sec_frames,
			(int)(unsigned short)(GlobalCounter - g_sec_counter),
			(int)TargetPresentHz(), BudgetMs(), g_sec_late, g_sec_pauses,
			g_frame_sum / (g_sec_intervals > 0 ? g_sec_intervals : 1),
			g_frame_max,
			g_work_sum / g_sec_frames, g_work_max, buf);

		g_level_second++;
		g_level_late += g_sec_late;
		g_level_pauses += g_sec_pauses;
	}
}

void ProfileBeginPhase(int phase)
{
	LARGE_INTEGER now;

	if (phase < 0 || phase >= PROF_COUNT || !ClockReady())
		return;

	/* CLEAR, BEGIN_WAIT and BEGIN count only outside the work and outside
	 * ControlPhase: a modal loop's scene inside the logic is its logic's,
	 * and the six fields of the pause line stay disjoint (#322). */
	if (phase >= PROF_CLEAR && (g_in_work || g_start[PROF_LOGIC]))
		return;

	if (ParkPhase(phase))
		g_park_at[phase] = ParkedNow();

	QueryPerformanceCounter(&now);
	g_start[phase] = now.QuadPart;
}

void ProfileEndPhase(int phase)
{
	LARGE_INTEGER now;

	if (phase < 0 || phase >= PROF_COUNT || !ClockReady() || !g_start[phase])
		return;

	QueryPerformanceCounter(&now);
	g_frame[phase] += Ms(now.QuadPart - g_start[phase]);
	g_start[phase] = 0;

	if (ParkPhase(phase))
		g_park_in[phase] += (double)(unsigned long)(ParkedNow() -
			g_park_at[phase]);
}

void ProfileLevel(const char* fileName)
{
	LARGE_INTEGER now;
	char* dot;

	if (!gLogEnabled || !ClockReady())
		return;

	// The level that is over: its last, part-filled second, and one summary.
	if (g_level[0] && g_level_frames)
		Log("PROFLOG level=%s summary seconds=%d frames=%d late=%d pauses=%d",
			g_level, g_level_second, g_level_frames, g_level_late,
			g_level_pauses);

	g_level[0] = 0;

	if (fileName)
	{
		strncpy_s(g_level, sizeof(g_level), fileName, _TRUNCATE);
		dot = strrchr(g_level, '.');

		if (dot)
			*dot = 0;
	}

	QueryPerformanceCounter(&now);
	g_qpc_level = now.QuadPart;
	g_qpc_second = now.QuadPart;

	/* Not the previous present: the load has just eaten several seconds and
	 * an interval measured across it would say nothing. The first frame of
	 * a level therefore has no interval, only its own work - which is the
	 * number that shows the level's warm-up cost, and why it gets a line
	 * of its own. */
	g_qpc_frame = 0;
	g_level_second = 0;
	g_level_frames = 0;
	g_level_late = 0;
	g_level_pauses = 0;
	g_first_frame = true;
	g_presents = 0;
	g_sort_max = 0;

	for (int i = 0; i < PROF_COUNT; i++)
	{
		g_start[i] = 0;
		g_frame[i] = 0;
		g_park_in[i] = 0;
	}

	// A park during the load is no frame's: the next interval starts here.
	g_parked_seen = ParkedNow();
	g_in_work = false;
	NewSecond();
}

/* Marks one present. Every present in the engine that goes through
 * S_DumpScreen or S_DumpScreenFrame lands here; ProfileFrameEnd reads the
 * count and expects exactly one - its own. More than that means a modal
 * loop drew frames in between and the interval it is about to measure is
 * not a game frame's. */
void ProfileNotePresent()
{
	g_presents++;
}

void ProfileNoteSort(long count)
{
	if (count > g_sort_max)
		g_sort_max = count;
}

// The window thread, with the game thread suspended (profile.h).
void ProfileNoteParkedMs(unsigned long ms)
{
	InterlockedExchangeAdd(&g_parked_total, (LONG)ms);
}

void ProfileNoteFog(double cpuMs, double gpuMs)
{
	g_fog_cpu += cpuMs;

	if (gpuMs >= 0)
		g_fog_gpu = gpuMs;
}

void ProfileFrameBegin()
{
	LARGE_INTEGER now;

	if (!gLogEnabled || !ClockReady())
		return;

	/* PROF_LOGIC is not cleared here. ControlPhase runs between the present
	 * and this call, so its milliseconds are already in the accumulator
	 * and belong to the frame that is starting - see the note in
	 * profile.h. ProfileFrameEnd clears it once it has reported it;
	 * clearing it in neither place would let `logic` grow without bound.
	 * CLEAR, BEGIN_WAIT and BEGIN are kept the same way and for the same
	 * reason: they ran outside the work, before this call (#322).
	 * Everything else is this frame's own and starts at nothing. */
	for (int i = 0; i < PROF_COUNT; i++)
	{
		g_start[i] = 0;

		if (!OutsidePhase(i))
			g_frame[i] = 0;
	}

	g_in_work = true;

	g_sort_max = 0;
	g_fog_cpu = 0;
	g_fog_gpu = -1;
	QueryPerformanceCounter(&now);
	g_qpc_begin = now.QuadPart;
}

void ProfileFrameEnd()
{
	LARGE_INTEGER now;
	char buf[832];
	char outside[160];
	double frame_ms, work_ms, missed, parked;
	bool paused;
	int at_s;
	LONG parked_now;

	if (!gLogEnabled || !ClockReady())
		return;

	QueryPerformanceCounter(&now);
	g_in_work = false;

	// The park that ended since the last frame end: this interval's.
	parked_now = ParkedNow();
	parked = (double)(unsigned long)(parked_now - g_parked_seen);
	g_parked_seen = parked_now;

	// Whole seconds since ProfileLevel, off the clock rather than off the
	// flushed-second counter: a menu or a cutscene draws no game frame, no
	// PROFLOG second is flushed, and the counter would then say a level was
	// four seconds old when it had been running for forty.
	at_s = g_qpc_level ? (int)((now.QuadPart - g_qpc_level) / g_freq) : 0;

	work_ms = g_qpc_begin ? Ms(now.QuadPart - g_qpc_begin) : 0;
	frame_ms = g_qpc_frame ? Ms(now.QuadPart - g_qpc_frame) : 0;
	g_qpc_frame = now.QuadPart;
	g_level_frames++;

	/* The budget follows the present, not the option. A frame is late when
	 * its interval ran over the slot its own present asked for plus half
	 * a 60 Hz tick - 25.0 ms after a one-tick present, 41.7 ms after a
	 * two-tick one. Taking the slot from tomb5.fps_mode directly would
	 * measure the inventory, the pause menu and the title - all of which
	 * keep their two ticks at any setting - against 16.7 ms, and report a
	 * healthy menu second as thirty late frames.
	 *
	 * And an interval is not late when the main loop was somewhere else.
	 * g_presents is this frame's own present and nothing else in the
	 * normal case; anything above one means a modal loop drew between
	 * this frame and the last. The second half of the rule catches what
	 * has no present at all - an alt-tab, a window drag, a breakpoint - by
	 * the one thing those always look like: an interval a full second
	 * longer than the work in it. */
	missed = BudgetMs() + 1000.0 / 120.0;
	paused = g_presents > 1 || frame_ms - work_ms > 1000.0;
	OutsideLine(outside, sizeof(outside), frame_ms, work_ms, parked);

	if (g_first_frame)
	{
		/* The first frame of a level, always written and never counted as
		 * late: there is no interval behind it, and it is the frame that
		 * shows the level's warm-up cost. */
		g_first_frame = false;
		OneFrameLine(buf, sizeof(buf), g_frame);
		Log("PROFLOG level=%s first_frame fps=%d work=%.2f %s",
			g_level[0] ? g_level : "?", (int)TargetPresentHz(), work_ms, buf);
	}
	else if (paused)
	{
		/* One short line, and no phases: a frame that waited out an inventory
		 * visit or an alt-tab has a meaningless interval, and printing its
		 * seventeen phases beside it only invites the interval to be blamed on
		 * one of them. */
		g_sec_pauses++;
		Log("PROFLOG level=%s pause at=%ds frame=%.2f work=%.2f presents=%d "
			"%s", g_level[0] ? g_level : "?", at_s, frame_ms, work_ms,
			g_presents, outside);
	}
	else if (frame_ms > missed)
	{
		g_sec_late++;
		OneFrameLine(buf, sizeof(buf), g_frame);
		/* The late line lists `logic` among its phases already, so it gains
		 * the split from `clear=` on: the same numbers, one key once. */
		Log("PROFLOG level=%s late at=%ds budget=%.1f frame=%.2f work=%.2f %s "
			"%s", g_level[0] ? g_level : "?", at_s, missed, frame_ms,
			work_ms, buf, strstr(outside, "clear="));
	}

	for (int i = 0; i < PROF_COUNT; i++)
	{
		g_sum[i] += g_frame[i];

		if (g_frame[i] > g_max[i])
			g_max[i] = g_frame[i];
	}

	/* The logic accumulator is emptied here and nowhere else. It has just
	 * been reported and just been added to the second; the ControlPhase
	 * that runs next fills it again for the next frame. Without this line
	 * it is never cleared at all and every PROFLOG second reads a running
	 * total. */
	g_frame[PROF_LOGIC] = 0;

	// The other outside phases, and the parks inside two of them, likewise.
	for (int i = 0; i < PROF_COUNT; i++)
	{
		if (OutsidePhase(i))
			g_frame[i] = 0;

		g_park_in[i] = 0;
	}

	g_work_sum += work_ms;

	if (work_ms > g_work_max)
		g_work_max = work_ms;

	g_fog_cpu_sum += g_fog_cpu;

	if (g_fog_cpu > g_fog_cpu_max)
		g_fog_cpu_max = g_fog_cpu;

	if (g_fog_gpu >= 0)
	{
		g_fog_gpu_sum += g_fog_gpu;
		g_fog_gpu_n++;

		if (g_fog_gpu > g_fog_gpu_max)
			g_fog_gpu_max = g_fog_gpu;
	}

	/* Only a real interval goes into the frame clock: not the first frame of
	 * a level, which has none, and not one that waited out a menu - a single
	 * five-second pause would otherwise own both the mean and the maximum of
	 * its second and say nothing about the frame rate. */
	if (frame_ms > 0 && !paused)
	{
		g_frame_sum += frame_ms;
		g_sec_intervals++;

		if (frame_ms > g_frame_max)
			g_frame_max = frame_ms;
	}

	g_sec_frames++;
	g_presents = 0;

	if (now.QuadPart - g_qpc_second < g_freq)
		return;

	FlushSecond(at_s);

	/* One second on, unless more than two went by - a menu, a load or an FMV
	 * draws no game frame at all, and a catch-up loop would write a run of
	 * empty lines about seconds nobody played. Same rule as the frame clock's.
	 */
	if (now.QuadPart - g_qpc_second < 2 * g_freq)
		g_qpc_second += g_freq;
	else
		g_qpc_second = now.QuadPart;

	NewSecond();
}

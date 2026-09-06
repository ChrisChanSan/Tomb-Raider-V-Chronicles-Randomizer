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
	/* THE NAMES, in the order of the enum. They are what the log line is read
	 * with, so they are short and they never change once a report has quoted
	 * them. */
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
		"flip"
	};

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

	/* EVERYTHING CLASSIC THE OUTPUT PHASE DID: the buckets, the sort list,
	 * the binocular mask, the pickup display and the fade. HD_DRAW and
	 * HD_GLOW are the only two phases that run inside S_OutputPolyList and
	 * NOWHERE else, so they are the only two that may be taken off it -
	 * BUCKETS, SORT and SORTDRAW are whole-frame totals that also cover the
	 * sky phase, and subtracting them would make this read zero in every
	 * outdoor level.
	 * HD_OBJDRAW and HD_LIGHT sit inside HD_DRAW and are not subtracted
	 * twice. Clamped because the three clocks are read at different moments
	 * and a rounding of a few microseconds must not print a negative. */
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

		for (int i = 0; i < PROF_COUNT && at >= 0 && (size_t)at < size; i++)
			at += _snprintf_s(out + at, size - at, _TRUNCATE, "%s=%.2f ",
				kName[i], p[i]);

		if (at >= 0 && (size_t)at < size)
			_snprintf_s(out + at, size - at, _TRUNCATE, "outrest=%.2f sortn=%d",
				OutRest(p), (int)g_sort_max);
	}

	/* THE SLOT THIS FRAME ASKED FOR, in milliseconds. gPresentMinTicks is how
	 * many 1/60 s ticks the present that just ran waited for, so this is the
	 * frame's OWN budget and not the mode's: one tick for a game frame at
	 * fps_mode 1, two for every modal loop in the build whatever the mode
	 * says. DrawPhaseGame deliberately puts the variable back to two only
	 * AFTER ProfileFrameEnd, so this reads the policy of the frame being
	 * judged (#148, order 1b). */
	double BudgetMs()
	{
		const long ticks = gPresentMinTicks > 0 ? gPresentMinTicks : 1;

		return (double)ticks * 1000.0 / 60.0;
	}

	// "name=mean/max " for every phase.
	void OneSecondLine(char* out, size_t size, int frames)
	{
		const double n = frames > 0 ? (double)frames : 1.0;
		int at = 0;

		for (int i = 0; i < PROF_COUNT && at >= 0 && (size_t)at < size; i++)
			at += _snprintf_s(out + at, size - at, _TRUNCATE,
				"%s=%.2f/%.2f ", kName[i], g_sum[i] / n, g_max[i]);

		if (at >= 0 && (size_t)at < size)
			_snprintf_s(out + at, size - at, _TRUNCATE, "outrest=%.2f",
				OutRest(g_sum) / n);
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

		/* logic_ticks IS THE PROOF OF THE CLOCK SPLIT (#148, order 1).
		 * GlobalCounter steps once per logic tick (game\control.cpp) and
		 * nowhere else, so its difference over one wall-clock second is the
		 * simulation's own rate. At fps_mode 1 this line must read
		 * `frames=60 logic_ticks=30`: sixty presents, thirty ticks, and the
		 * game running at exactly the speed it always did. */
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

	/* NOT the previous present: the load has just eaten several seconds and
	 * an interval measured across it would say nothing. The first frame of a
	 * level therefore has no interval, only its own work - which is exactly
	 * the number the warm-up question is about, and why it gets a line of its
	 * own. */
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
	}

	NewSecond();
}

/* ONE PRESENT. Every present in the engine that goes through S_DumpScreen or
 * S_DumpScreenFrame lands here; ProfileFrameEnd reads the count and expects
 * exactly one - its own. More than that means a modal loop drew frames in
 * between and the interval it is about to measure is not a game frame's. */
void ProfileNotePresent()
{
	g_presents++;
}

void ProfileNoteSort(long count)
{
	if (count > g_sort_max)
		g_sort_max = count;
}

void ProfileFrameBegin()
{
	LARGE_INTEGER now;

	if (!gLogEnabled || !ClockReady())
		return;

	/* PROF_LOGIC IS NOT CLEARED HERE. ControlPhase runs between the present
	 * and this call, so its milliseconds are already in the accumulator and
	 * belong to the frame that is starting - see the note in profile.h.
	 * ProfileFrameEnd clears it once it has reported it; clearing it in
	 * neither place is what made `logic` grow without bound in order 1.
	 * Everything else is this frame's own and starts at nothing. */
	for (int i = 0; i < PROF_COUNT; i++)
	{
		g_start[i] = 0;

		if (i != PROF_LOGIC)
			g_frame[i] = 0;
	}

	g_sort_max = 0;
	QueryPerformanceCounter(&now);
	g_qpc_begin = now.QuadPart;
}

void ProfileFrameEnd()
{
	LARGE_INTEGER now;
	char buf[832];
	double frame_ms, work_ms, missed;
	bool paused;
	int at_s;

	if (!gLogEnabled || !ClockReady())
		return;

	QueryPerformanceCounter(&now);

	// Whole seconds since ProfileLevel, off the clock rather than off the
	// flushed-second counter: a menu or a cutscene draws no game frame, no
	// PROFLOG second is flushed, and the counter would then say a level was
	// four seconds old when it had been running for forty.
	at_s = g_qpc_level ? (int)((now.QuadPart - g_qpc_level) / g_freq) : 0;

	work_ms = g_qpc_begin ? Ms(now.QuadPart - g_qpc_begin) : 0;
	frame_ms = g_qpc_frame ? Ms(now.QuadPart - g_qpc_frame) : 0;
	g_qpc_frame = now.QuadPart;
	g_level_frames++;

	/* THE BUDGET FOLLOWS THE PRESENT, not the option (#148, order 1b). A
	 * frame is late when its interval ran over the slot its OWN present asked
	 * for plus half a 60 Hz tick - 25.0 ms after a one-tick present, 41.7 ms
	 * after a two-tick one. Order 1 took the slot from tomb5.fps_mode, and so
	 * measured the inventory, the pause menu and the title - all of which
	 * keep their two ticks at any setting - against 16.7 ms, and reported a
	 * healthy menu second as thirty late frames.
	 *
	 * AND AN INTERVAL IS NOT LATE WHEN THE MAIN LOOP WAS SOMEWHERE ELSE.
	 * g_presents is this frame's own present and nothing else in the normal
	 * case; anything above one means a modal loop drew between this frame and
	 * the last. The second half of the rule catches what has no present at
	 * all - an alt-tab, a window drag, a breakpoint - by the one thing those
	 * always look like: an interval a full second longer than the work in
	 * it. */
	missed = BudgetMs() + 1000.0 / 120.0;
	paused = g_presents > 1 || frame_ms - work_ms > 1000.0;

	if (g_first_frame)
	{
		/* THE FIRST FRAME OF A LEVEL, always written and never counted as
		 * late: there is no interval behind it, and it is the frame the
		 * warm-up question is about. */
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
		Log("PROFLOG level=%s pause at=%ds frame=%.2f work=%.2f presents=%d",
			g_level[0] ? g_level : "?", at_s, frame_ms, work_ms, g_presents);
	}
	else if (frame_ms > missed)
	{
		g_sec_late++;
		OneFrameLine(buf, sizeof(buf), g_frame);
		Log("PROFLOG level=%s late at=%ds budget=%.1f frame=%.2f work=%.2f %s",
			g_level[0] ? g_level : "?", at_s, missed, frame_ms, work_ms, buf);
	}

	for (int i = 0; i < PROF_COUNT; i++)
	{
		g_sum[i] += g_frame[i];

		if (g_frame[i] > g_max[i])
			g_max[i] = g_frame[i];
	}

	/* THE LOGIC ACCUMULATOR IS EMPTIED HERE and nowhere else. It has just
	 * been reported and just been added to the second; the ControlPhase that
	 * runs next fills it again for the next frame. Without this line it is
	 * never cleared at all and every PROFLOG second reads a running total
	 * (#148, order 1b). */
	g_frame[PROF_LOGIC] = 0;

	g_work_sum += work_ms;

	if (work_ms > g_work_max)
		g_work_max = work_ms;

	/* Only a REAL interval goes into the frame clock: not the first frame of
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

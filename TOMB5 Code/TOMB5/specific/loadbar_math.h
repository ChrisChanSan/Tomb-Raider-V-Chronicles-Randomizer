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

/* The loading screen's rules, and nothing else: the Loadbar style row's
 * state, the brake's position, the fade of the third state and the
 * level's name on the screen.
 *
 * No engine header on purpose: Preparation\Tools\Diagnose\ModernTest.cpp
 * includes this file and runs the rules as the engine runs them
 * (tomb5\tomb5.cpp reads the row with them, specific\drawbars.cpp draws
 * with them, specific\file.cpp and specific\hd\hd_level.cpp feed the
 * work, specific\output.cpp names the coming level over the TR4 view). */

#include <math.h>	// floor - the brake's clock in whole ticks

// The Loadbar style row: the TR5 bar, the TR4 bar, or no loading screen.
enum
{
	LOADBAR_TR5 = 0,
	LOADBAR_TR4 = 1,
	LOADBAR_OFF = 2
};

/* The row's state out of the registry. `found` and `value` are the DWORD
 * loadbar as REG_ReadLong answers; `tr4_bar` is the older bool tr4LB, the
 * row's whole state before the third one existed. Without the DWORD, or
 * with a value no state has, the bool decides, so that no player's
 * loading screen changes by the update: 1 the TR4 bar, 0 the TR5 one. */
inline long LoadBarModeOf(bool found, unsigned long value, bool tr4_bar)
{
	if (found && value <= LOADBAR_OFF)
		return (long)value;

	return tr4_bar ? LOADBAR_TR4 : LOADBAR_TR5;
}

/* Whether a load builds the HD data, as the keep decides it (hd_level.cpp,
 * HDLevelKeyEarly): the build gate open (hd_build and an installation),
 * the level's art there (its .TRG) and the data in memory not kept for
 * it. Such a load - another level, the title, the first load, a key
 * refused - takes seconds; one that keeps them or builds none takes a
 * tenth of one (the owner's -log runs: 34 to 94 ms against 0.96 to 5 s). */
inline long LoadBuildsHD(long gate, long art, long kept)
{
	return gate && art && !kept;
}

/* The state a load's screen is drawn in. The row off leaves the screen
 * out only for a short load; a load that builds the HD data shows it as
 * the TR5 state does - the picture, the bar, the brake - so that seconds
 * of black are never taken for a hang. */
inline long LoadScreenModeOf(long mode, long builds)
{
	return mode == LOADBAR_OFF && builds ? LOADBAR_TR5 : mode;
}

/* The work of an HD load in two parts: the loading thread (its S_LoadBar
 * calls, pages + 20) and the main thread's work after it - the HD build
 * and the steps that follow it (font, cutscenes, warmup, profile). The
 * thread's share is its median part of the two, counted in the LOADLOG
 * lines of 18 fresh loads of a -log run on the target machine (thread /
 * (thread + main) 0.038 to 0.161, median 0.094), so that the bar keeps
 * one pace across both. A load with no HD build to follow gives the
 * thread the whole bar. */
const float kLoadBarThreadShare = 0.10f;

/* The points at which the main thread reports its work: the HD build's
 * own steps - the full build's 16 laps (hd_level.cpp, HDLevelBuildHD's
 * lap(), in its LoadPhase order: marks, boxes, gloss, intr, air, pairs,
 * cuts, stand, places, begin, tables, statics, sheets, surfaces, frames,
 * objects) or the resident build's 5 steps (sheets, surfaces, frames,
 * objects, life) - and after them three of specific\file.cpp's: the
 * build's end, the font and the cutscenes. */
const int kLoadBarBuildLaps = 16;
const int kLoadBarResidentSteps = 5;
const int kLoadBarAfterSteps = 3;

/* The main thread's part done at each point, the median over the same
 * -log run: 18 fresh loads for the laps, the 16 saves loaded inside their
 * level for the resident steps. The room build (begin) is about three
 * quarters of a fresh load's, the objects a fifth; the warmup, the last
 * step, takes what is left to 1. */
const float kLoadBarLapShare[kLoadBarBuildLaps + kLoadBarAfterSteps] =
{
	0.005f, 0.005f, 0.005f, 0.005f, 0.006f, 0.007f, 0.007f, 0.007f,
	0.007f, 0.730f, 0.732f, 0.735f, 0.742f, 0.742f, 0.742f, 0.932f,
	0.987f, 0.989f, 0.989f
};

const float kLoadBarResidentShare[kLoadBarResidentSteps +
	kLoadBarAfterSteps] =
{
	0.186f, 0.189f, 0.189f, 0.573f, 0.621f, 0.968f, 0.968f, 0.968f
};

/* The main thread's part done after `done` of its points, the build
 * having `total` steps of its own: the tables for the full and the
 * resident build, an even share of total + kLoadBarAfterSteps for any
 * other count. Past the last point it stays there: 1 comes with the work's
 * end (S_FinishLoadBar). */
inline float LoadBarBuildShare(int done, int total)
{
	if (done <= 0 || total <= 0)
		return 0.0f;

	const int last = total + kLoadBarAfterSteps;
	const int at = done < last ? done : last;

	if (total == kLoadBarBuildLaps)
		return kLoadBarLapShare[at - 1];

	if (total == kLoadBarResidentSteps)
		return kLoadBarResidentShare[at - 1];

	return (float)at / (float)(last + 1);
}

/* The 1/60 s tick of Sync() (specific\time.cpp), in ms: the brake's clock
 * counts whole ticks, so that every frame moves the bar by the ticks it
 * stands for and no frame drawn a little late moves it by more. */
const double kLoadBarTickMs = 1000.0 / 60.0;

/* `ms` on the tick grid: the whole ticks it holds, in ms. Rounded to the
 * nearest tick, since both ends of the span sit on Sync()'s boundaries and
 * the span is a whole number of ticks up to the clock's rounding. */
inline double LoadBarTicksMs(double ms)
{
	if (ms <= 0.0)
		return 0.0;

	return floor(ms / kLoadBarTickMs + 0.5) * kLoadBarTickMs;
}

// A share in 0..1.
inline float LoadBarClamp01(float v)
{
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* The work done, 0..1: the thread's part (its calls as 0..1) and the HD
 * build's (0..1), weighed by the thread's share. */
inline float LoadBarWork(float thread, float build, float threadShare)
{
	const float share = LoadBarClamp01(threadShare);

	return LoadBarClamp01(share * LoadBarClamp01(thread) +
		(1.0f - share) * LoadBarClamp01(build));
}

/* The brake: the bar's position, 0..100, `elapsed` ms after the loading
 * screen's first frame with `work` of the work done, the screen to stand
 * at least `minMs`: 100 * min(work, elapsed / minMs). Never ahead of the
 * work, never ahead of the clock; both only grow, so the position never
 * goes back. It reaches 100 when both are done: at minMs on a load
 * shorter than that, at the work's end on a longer one. minMs 0 is no
 * brake: the bar is the work. */
inline float LoadBarPosition(float work, double elapsed, double minMs)
{
	float time = 1.0f;

	if (minMs > 0.0)
		time = elapsed <= 0.0 ? 0.0f :
			elapsed >= minMs ? 1.0f : (float)(elapsed / minMs);

	const float done = LoadBarClamp01(work);

	return 100.0f * (done < time ? done : time);
}

// The brake's end: the work done and the screen up for minMs.
inline bool LoadBarDone(float work, double elapsed, double minMs)
{
	return work >= 1.0f && elapsed >= minMs;
}

/* The third state's fade: how dark the black over the last frame is,
 * 0..255, `elapsed` ms after the fade began, over `fadeMs`. 0 ms is a
 * cut: black at once. */
inline long LoadFadeAlpha(double elapsed, double fadeMs)
{
	if (fadeMs <= 0.0 || elapsed >= fadeMs)
		return 255;

	if (elapsed <= 0.0)
		return 0;

	return (long)(255.0 * elapsed / fadeMs);
}

/* The level's name on a loading screen (the Loading text row). `slot` is
 * the gameflow slot the load reads: 0 is the title's (DoGameflow), which
 * is no level and shows no name, -1 no load at all. A screen not shown
 * (the row off, a short load) draws nothing, the name neither. `baked`:
 * the picture under the frame already carries the name (the TR4 view,
 * read back with it), so that it is never drawn twice. */
inline long LoadNameDrawn(long row, long shown, long slot, long baked)
{
	return row && shown && slot > 0 && !baked;
}

/* The slot DoGameflow (game\gameflow.cpp) loads next, out of what it reads
 * when DoTitle or DoLevel has returned: RenderLoadPic draws the view at
 * their end, before gfCurrentLevel is set. A save (status 2) is its own
 * level; after the title, 3 is the level chosen and anything else stays
 * where it is; after a level, 1 is the title (slot 1 when there is none),
 * 3 the next level, the title past the last one, on a demo disc or with
 * two slots, and the title after the last campaign slot (DoLevel turns
 * that 3 into 1 for the credits); 4 ends the game. */
inline long LoadNextSlot(long fromTitle, long status, long current,
	long lastSlot, long saveLevel, long complete, long titleOn,
	long titleOnly, long levels)
{
	if (status == 4)
		return -1;

	if (status == 2)
		return saveLevel;

	if (fromTitle)
		return status == 3 ? complete : current;

	if (status == 1 || (status == 3 && current == lastSlot))
		return titleOn ? 0 : 1;

	if (status == 3)
		return titleOnly || complete > levels ? 0 : complete;

	return current;
}

/* The name's font style: the one the title's level list draws a level's
 * name in (gameflow.cpp, an entry not selected) and the statistics too. */
const long kLoadNameStyle = 2;

/* The name's margin from the top edge to its highest ink, in
 * GetFixedScale units: the bar's own margin from the bottom edge
 * (GetFixedScale(20), drawbars.cpp), so that name and bar frame the
 * picture alike. */
const long kLoadNameMargin = 20;

/* How far the font reaches above a baseline at the current screen size,
 * in pixels: the highest glyph of `def` (CharDef, scaled by InitFont) and
 * the highest accent PrintString composes (AccentTable: the accent drawn
 * at the base letter's y offset). */
template <class Def>
inline long LoadNameReach(const Def* def, long glyphs,
	const char (*accent)[2], long rows)
{
	long reach = 0;

	for (long i = 0; i < glyphs; i++)
		if (-def[i].YOffset > reach)
			reach = -def[i].YOffset;

	for (long i = 0; i < rows; i++)
	{
		const long a = accent[i][0] - '!';
		const long b = accent[i][1] - '!';

		if (a < 0 || b < 0 || a >= glyphs || b >= glyphs)
			continue;

		if (-(def[a].YOffset + def[b].YOffset) > reach)
			reach = -(def[a].YOffset + def[b].YOffset);
	}

	return reach;
}

/* The name's baseline: its highest ink `margin` pixels under the top
 * edge. A name is one line (no level name of the language files breaks),
 * so it grows no further down than the font's depth under the baseline. */
inline long LoadNameY(long margin, long reach)
{
	return margin + reach;
}

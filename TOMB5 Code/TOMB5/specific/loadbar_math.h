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
 * state, the brake's position and the fade of the third state.
 *
 * No engine header on purpose: Preparation\Tools\Diagnose\ModernTest.cpp
 * includes this file and runs the rules as the engine runs them
 * (tomb5\tomb5.cpp reads the row with them, specific\drawbars.cpp draws
 * with them, specific\file.cpp and specific\hd\hd_level.cpp feed the
 * work). */

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

/* The work of a fresh HD load in two parts: the loading thread (its
 * S_LoadBar calls, pages + 20) and the HD build on the main thread
 * after it. The thread's share is its median part of the two, counted
 * in the LOADLOG lines of 18 fresh loads of a -log run on the target
 * machine (thread / (thread + hd_build) 0.038 to 0.164, median 0.096),
 * so that the bar keeps one pace across both. A load with no HD build
 * to follow gives the thread the whole bar. */
const float kLoadBarThreadShare = 0.10f;

/* The HD build's laps (hd_level.cpp, HDLevelBuildHD's lap(), in its
 * LoadPhase order: marks, boxes, gloss, intr, air, pairs, cuts, stand,
 * places, begin, tables, statics, sheets, surfaces, frames, objects): the
 * part of the build done at each lap's end, the median over the same 18
 * fresh loads. The room build (begin) is about three quarters, the
 * objects a fifth; what follows the last lap is the rest to 1. */
const int kLoadBarBuildLaps = 16;

const float kLoadBarLapShare[kLoadBarBuildLaps] =
{
	0.005f, 0.005f, 0.005f, 0.006f, 0.006f, 0.007f, 0.007f, 0.007f,
	0.007f, 0.741f, 0.742f, 0.745f, 0.752f, 0.752f, 0.752f, 0.950f
};

/* The HD build's part done after `done` of `total` steps: the lap table
 * for the full build's laps, an even share for any other count (the
 * resident build's five steps). */
inline float LoadBarBuildShare(int done, int total)
{
	if (done <= 0 || total <= 0)
		return 0.0f;

	if (done >= total)
		return total == kLoadBarBuildLaps ? kLoadBarLapShare[total - 1] :
			1.0f;

	if (total == kLoadBarBuildLaps)
		return kLoadBarLapShare[done - 1];

	return (float)done / (float)total;
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

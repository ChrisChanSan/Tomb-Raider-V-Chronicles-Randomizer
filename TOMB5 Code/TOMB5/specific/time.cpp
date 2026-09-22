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
#include "time.h"
#include "function_stubs.h"

static __int64 frequency;
static __int64 counter;

long Sync()
{
	__int64 PerformanceCount, f;
	long n;

	QueryPerformanceCounter((LARGE_INTEGER*)&PerformanceCount);
	f = (PerformanceCount - counter) / frequency;
	counter += frequency * f;
	n = (long)f;
	return n;
}

/* Time since Sync() last consumed a whole 1/60 s tick, as a fraction of one
 * tick, read without consuming it. Sync() hands out whole ticks and leaves
 * the remainder standing on the last tick boundary in `counter`, so
 * ControlPhase and S_AnimateTextures keep seeing a fixed thirty hertz
 * simulation; this fraction lets a refresh rate not locked to 60 Hz blend
 * between ticks (see InterpTickFraction in game/interpolate.cpp), since at a
 * higher rate most presents fall inside one tick and Sync() alone answers 0
 * for them.
 *
 * Grows monotonically between Sync() calls; its sum with the tick count is
 * continuous across a Sync() call, which keeps a blend built from the two
 * free of steps and drift. Not clamped below one, since more than a tick can
 * pass before the next Sync() - the caller clamps, since only it knows what
 * it may not extrapolate past. Zero before TIME_Init, and zero rather than
 * negative if the clock is ever read behind the boundary (a QPC that jumps
 * backwards on a broken driver would otherwise hand out a negative blend
 * factor). */
double TIME_SubTick()
{
	__int64 PerformanceCount, d;

	if (frequency <= 0)
		return 0.0;

	QueryPerformanceCounter((LARGE_INTEGER*)&PerformanceCount);
	d = PerformanceCount - counter;

	if (d <= 0)
		return 0.0;

	return (double)d / (double)frequency;
}

void TIME_Reset()
{
	QueryPerformanceCounter((LARGE_INTEGER*)&counter);
}

bool TIME_Init()
{
	__int64 pfq;

	Log(__FUNCTION__);

	if (!QueryPerformanceFrequency((LARGE_INTEGER*)&pfq))
		return 0;

	frequency = pfq / 60;
	TIME_Reset();
	return 1;
}

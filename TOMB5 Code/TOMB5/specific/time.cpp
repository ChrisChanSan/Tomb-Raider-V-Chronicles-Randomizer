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

/* WHAT IS LEFT OVER OF A TICK (#148, order 6).
 *
 * Sync() above hands out WHOLE 1/60 s ticks and keeps the remainder in
 * `counter`, which is left standing on the last tick boundary it gave away.
 * That is the whole contract of this file and it does not change: ControlPhase
 * and S_AnimateTextures are fed whole ticks and the simulation stays a thirty
 * hertz simulation.
 *
 * This reads the remainder WITHOUT consuming it: how far past that last
 * boundary the clock stands right now, as a fraction of a tick. Nothing else
 * in the engine needs it - a frame rate bound to the display's refresh does,
 * because at 144 Hz two presents out of three fall inside one tick and Sync()
 * answers 0 for both of them. See InterpTickFraction (game\interpolate.cpp)
 * for what is built on top of it.
 *
 * PROPERTIES IT IS RELIED ON FOR:
 *
 *   monotone   between two Sync() calls it only ever grows - it is one
 *              subtraction from a monotone counter.
 *   continuous when Sync() consumes k ticks, this drops by exactly k and the
 *              caller's tick count rises by exactly k, so their SUM is
 *              continuous across the call. That is what makes the blend
 *              factor built from the pair free of steps and free of drift.
 *   unbounded  it is NOT clamped to below one. Sync() is called once per
 *              frame at the END of the frame, so at the top of the next
 *              frame more than a whole tick may well have passed - and on a
 *              hitch, many. The caller clamps, because only the caller knows
 *              what it may not extrapolate past.
 *
 * Zero before TIME_Init, and zero rather than negative if the clock is ever
 * read behind the boundary (it cannot be, but a QPC that jumps backwards on a
 * broken driver would otherwise hand out a negative blend factor). */
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

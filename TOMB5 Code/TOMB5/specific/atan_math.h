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
#include <limits.h>
#include "../global/math_tbls.h"

/* phd_atan's arithmetic, in a header of its own so that ModernTest runs the
 * engine's very lines (3dmath.cpp calls it, nothing else changes).
 *
 * The one input it cannot take is LONG_MIN. Its negation is LONG_MIN again,
 * the swap makes it the smaller side, and the shift loop runs until that
 * side is -1, by which time the other side is shifted to 0 - the divisor of
 * phdtantab's index. Every other pair keeps x >= y > 0 through the loop.
 * AtanClamp turns LONG_MIN into -LONG_MAX, one unit off, before the
 * arithmetic, which gives the angle of the next value and goes on. */

/* Whether `x` or `y` is LONG_MIN; each that is becomes -LONG_MAX. */
inline bool AtanClamp(long* x, long* y)
{
	if (*x != LONG_MIN && *y != LONG_MIN)
		return false;

	if (*x == LONG_MIN)
		*x = -LONG_MAX;

	if (*y == LONG_MIN)
		*y = -LONG_MAX;

	return true;
}

/* The angle of (x, y) in the engine's 65536 to the turn: phd_atan's body
 * as it always was. */
inline long AtanMath(long x, long y)
{
	long octant, n, result;

	result = 0;
	octant = 0;

	if (x || y)
	{
		if (x < 0)
		{
			octant += 4;
			x = -x;
		}

		if (y < 0)
		{
			octant += 2;
			y = -y;
		}

		if (y > x)
		{
			octant++;
			n = x;
			x = y;
			y = n;
		}

		while ((short)y != y)
		{
			x >>= 1;
			y >>= 1;
		}

		result = phdtan2[octant] + phdtantab[(y << 11) / x];

		if (result < 0)
			result = -result;
	}

	return result;
}

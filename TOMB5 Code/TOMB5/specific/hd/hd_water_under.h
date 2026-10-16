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

#include <math.h>

/* The water seen from below, the arithmetic without a device
 * (hd_render.cpp, "under the water"): the eye's depth under the surface the
 * extinction measures its path against, and the window of the sky a flat
 * surface lets through. ModernTest checks both (water_under).
 *
 * HDWaterUnderDepth: `fallback` is 1 for a water room no sheet lies over
 * (its body is the level's largest), `deep` 1 in a level that takes the
 * deep look, `eyeY` and `surface` world y (down), `none` the depth that
 * puts every point in the water (kUnderNoSurface). A fallback room has no
 * surface over it - except under the deep look, whose water is one sea:
 * there the body's surface counts on either side of the eye, its distance
 * from the eye the depth. Under it that is the eye's depth; over it the
 * same distance keeps the far surface in sight, which the whole way
 * through the water would take: in JOBY4 an eye at y 8,000 in room 119,
 * 12 squares under the surface art at y -4,253, saw none of it, at 4
 * squares (room 116) and at 20 (room 79) it showed (RoomProbe
 * -underwater, the frame with and without room 19). Anywhere else the
 * depth is the eye's under the body's surface. */
inline float HDWaterUnderDepth(int fallback, int deep, float eyeY,
	float surface, float none)
{
	if (fallback && deep)
		return eyeY > surface ? eyeY - surface : surface - eyeY;

	return fallback ? none : eyeY - surface;
}

/* The cosine of the half angle of the window a flat surface opens over the
 * eye (Snell's window): rays from the air bend toward the normal by the
 * ratio of the two refractive indices, so all the sky above the surface
 * comes in within asin(1 / n) of straight up and outside it the surface
 * turns the water's own light back. n = 1.333 for water: 48.6 degrees. */
inline float HDWaterSnellCos(float n)
{
	const float s = n > 1.0f ? 1.0f / n : 1.0f;

	return sqrtf(1.0f - s * s);
}

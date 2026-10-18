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

/* A sloped face's plane (hd_render.cpp, marble_build): the normal of the
 * face `p` (nine floats, the group's own x and z, the world's y) turned up
 * (y negative) and of length 1, and its distance in world space, the
 * group's origin (ox, oz) added. False for a face of no area. */
inline bool HDMarbleTiltPlane(const float* p, float ox, float oz,
	float* out)
{
	const double ux = (double)p[3] - p[0], uy = (double)p[4] - p[1];
	const double uz = (double)p[5] - p[2], vx = (double)p[6] - p[0];
	const double vy = (double)p[7] - p[1], vz = (double)p[8] - p[2];
	double n[3] = { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx };
	const double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

	if (len <= 0.0)
		return false;

	const double s = n[1] > 0.0 ? -len : len;

	for (int k = 0; k < 3; k++)
	{
		n[k] /= s;
		out[k] = (float)n[k];
	}

	out[3] = (float)(n[0] * ((double)p[0] + ox) + n[1] * p[1] +
		n[2] * ((double)p[2] + oz));
	return true;
}

/* Whether two sloped planes (HDMarbleTiltPlane's four floats) are one: the
 * normals' parts within `same` and the distances within `join` units. */
inline bool HDMarbleTiltSame(const float* a, const float* b, float same,
	float join)
{
	return fabsf(a[0] - b[0]) <= same && fabsf(a[1] - b[1]) <= same &&
		fabsf(a[2] - b[2]) <= same && fabsf(a[3] - b[3]) <= join;
}

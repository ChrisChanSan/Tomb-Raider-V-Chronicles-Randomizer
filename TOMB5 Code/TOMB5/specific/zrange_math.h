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

/* The view distance and the depth curve, as numbers and nothing else.
 *
 * No engine header on purpose: Preparation\Tools\Diagnose\ModernTest.cpp
 * and RoomProbe.cpp include this file and take the numbers the engine
 * takes. specific\gamemain.cpp and specific\winmain.cpp hand ZFAR_UNITS to
 * InitWindow; specific\3dmath.cpp (SetupZRange) builds the depth curve on
 * ZFAR_SHIPPED; specificfx.cpp (DoRain) and drawroom.cpp
 * (aBuildFogBulbList) keep ZFAR_SHIPPED as their own scale.
 *
 * The far plane is a cut, not a picture of its own: an object
 * (S_GetObjectBounds), a static the art does not draw, a sprite or an
 * effect whose depth lies past it is not drawn, while the rooms' faces are
 * drawn at any distance, classic and HD alike. Forty-one sectors: the
 * deepest thing the portal walk lists from inside a room, drawn by the
 * rule at any plane, stands 40,806 units ahead (JOBY2, a static of room
 * 3 seen from room 129; RoomProbe -farsurvey over every square of the
 * fifteen levels), the corridor's double door 34,340 (the same level,
 * from room 121, 33.5 sectors), and an eye
 * elsewhere in the sampled square stands at most a square farther from
 * it: 40,806 + 1,024 = 41,830, under 41 x 1,024 = 41,984. The numbers are
 * in MODIFICATIONS.md, "The far plane". */
#define ZFAR_SECTORS	41
#define ZFAR_UNITS	(ZFAR_SECTORS * 1024)

/* The near plane, unchanged: InitWindow's 20 units. */
#define ZNEAR_UNITS	20

/* The far plane the game shipped with, twenty sectors. It stays the scale
 * of three things that are looks and not cuts:
 *
 *   the depth curve   sz = a - b / z, 0.005 at the near plane and 0.995
 *                     here. Built on this number, every depth nearer than
 *                     it is the same float as before, and the HD path's
 *                     projection, the water's depth reading and the sky's
 *                     slice (0.996 and up) keep their meaning. Past it the
 *                     curve rises on towards a = 0.99597, under the slice
 *                     at any distance.
 *   the rain's tint   DoRain's drops brighten from 8 to 16 over it.
 *   the fog bulbs     aBuildFogBulbList keeps the bulbs nearer than it.
 */
#define ZFAR_SHIPPED	20480

/* The depth curve's two constants as SetupZRange takes them, f_a and f_b
 * (f_b already negated), in single precision as the engine has them. */
inline void ZDepthCurve(float mznear, float* a, float* b)
{
	const float scale = (float)ZFAR_SHIPPED;
	const float fb = scale * mznear * 0.99F / (mznear - scale);

	*a = 0.005F - fb / mznear;
	*b = -fb;
}

/* The depth the curve writes at view depth z. */
inline double ZDepthAt(double z)
{
	float a, b;

	ZDepthCurve((float)ZNEAR_UNITS, &a, &b);
	return (double)a - (double)b / z;
}

/* The units one step of a depth buffer of `bits` bits spans at view depth
 * z: the curve's slope is b / z^2, so a step of 2^-bits is z^2 / (b 2^bits)
 * units. 24 bits at 30,000 units: about 2.7. */
inline double ZDepthStep(double z, int bits)
{
	float a, b;

	ZDepthCurve((float)ZNEAR_UNITS, &a, &b);
	return z * z / ((double)b * (double)(1L << bits));
}

/* DoRain's tint of a drop at view depth mz, as the engine computes it with
 * `scale` 8 for the drop's tail and 16 for its head. */
inline long ZRainTint(float mz, float scale)
{
	const float range = (float)ZFAR_SHIPPED;

	return long((1.0F - (range - mz) * (1.0F / range)) * scale + scale);
}

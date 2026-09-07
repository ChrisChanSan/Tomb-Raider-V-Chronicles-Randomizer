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
#include <stdlib.h>	// labs

/* THE ARITHMETIC OF THE TIME INTERPOLATION (#148, order 2), AND NOTHING ELSE.
 *
 * This header includes no engine header at all, on purpose: it is the one
 * piece of order 2 that can be checked without a game, a level or a device,
 * and `Preparation\Tools\Diagnose\InterpTest.cpp` includes THIS FILE - not a
 * copy of it - and asserts on the functions below. So there is no twin to
 * keep in step: a change here is a change the test sees.
 *
 * game\interpolate.cpp is the rest of the order - the snapshot, the snap
 * rules, the three call sites and the log. */

/* THE SHORT WAY ROUND.
 *
 * A TR angle is a signed short and a full turn is exactly its range, so the
 * difference of two of them TAKEN AS A SHORT is already the shorter of the
 * two ways round. The wrap does it; no comparison against half a turn is
 * needed anywhere.
 *
 * 32000 to -32000 is the case that proves it. As an int the difference is
 * -64000, which would drag the body three-quarters of a turn BACKWARDS
 * THROUGH ZERO - an enemy spinning the wrong way for half a tick, on every
 * tween frame, whenever it faces roughly south. As a short the difference is
 * +1536, and 32000 + 768 is 32768, which wraps to -32768: exactly halfway,
 * over the top, the way the body really turned.
 *
 * Exactly half a turn (a difference of 32768) has no shorter way round and
 * comes out as -32768, i.e. it turns anticlockwise. That is arbitrary and it
 * is deterministic, which is all it has to be - a body that turns 180 degrees
 * in one thirtieth of a second is a snap in the animation, not a rotation. */
inline short InterpLerpAngle(short a, short b, float alpha)
{
	const short d = (short)(b - a);

	return (short)(a + (short)((float)d * alpha));
}

/* Positions are integers in this engine and stay integers: the truncation is
 * at most one unit, a TR sector is 1024 of them, and handing a float position
 * to a matrix stack that works in longs would only move the rounding
 * somewhere less visible. */
inline long InterpLerpLong(long a, long b, float alpha)
{
	return a + (long)((float)(b - a) * alpha);
}

/* THE DISTANCE THE SNAP RULES MEASURE: the sum of the three absolute
 * components, not a real distance. Three subtractions and no square root, it
 * is never SMALLER than the true distance (so it can only ever snap sooner,
 * never later), and a threshold that is a round number in a game of
 * 1024-unit sectors is worth more than one that is exact. */
inline long InterpManhattan(long dx, long dy, long dz)
{
	return labs(dx) + labs(dy) + labs(dz);
}

/* THE SNAP THRESHOLDS.
 *
 * INTERP_ITEM_SNAP - nothing in TR5 crosses half a sector under its own power
 * in one thirtieth of a second. Lara's terminal fall is about 130 units a
 * tick, a vehicle under 100, a bullet is not an item. What DOES cross it is a
 * teleport: the TranslateItem animation command (game\control.cpp), a flipmap
 * move, an enemy being placed by the randomizer's own spawn code, an item
 * being taken away and given back. Those must not be drawn as a streak
 * halfway across the room, so they are drawn where they really are.
 *
 * INTERP_CAM_SNAP is looser because a camera legitimately travels faster than
 * anything it films - MoveCamera closes a fraction of its distance to the
 * ideal every tick and a flyby's spline is faster still. It is not there for
 * speed at all; it is there for the CUTS: a fixed camera taking over, a spot
 * cam jumping to its next node, the binoculars going up. */
#define INTERP_ITEM_SNAP	512L
#define INTERP_CAM_SNAP		1024L

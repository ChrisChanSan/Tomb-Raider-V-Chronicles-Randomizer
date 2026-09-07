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

/* ==========================================================================
 * ORDER 4 - THE POSE.
 *
 * Order 2 moved where a thing STANDS. This is where its limbs stand, and it
 * is a different problem, because the engine already interpolates poses - it
 * just interpolates them in KEY FRAMES rather than in TIME.
 *
 * An animation stores every second, fourth, fifth ... frame and the engine
 * blends between two stored ones (game\draw.cpp, GetFrames). The blend
 * factor was never a number: it was three hard-wired branches, worth a
 * quarter, a half and three quarters, chosen by a pair of integers. So one
 * animation frame is drawn as
 *
 *     pose(f) = lerp(K[k], K[k+1], alpha_vanilla(f mod rate, rate))
 *
 * and the whole of order 4 is: on the in-between frame, draw the MIDPOINT OF
 * THE TWO POSES THE GAME REALLY DRAWS AT THIRTY, not a newly computed
 * "correct" one.
 *
 * THAT DISTINCTION IS THE WHOLE DESIGN AND IT IS NOT A DETAIL. Vanilla's
 * factor is coarse and, at rate 5, plainly wrong - frames 1..4 of a five
 * frame interval are drawn at 0.25, 0.75, 0.75, 0.75, so the pose stands
 * still for three frames and then jumps. Computing frac/rate instead would
 * be "better" and would CHANGE THE GAME: measured over all sixteen level
 * files, 3187 animations carry rate 5, 3350 rate 2, 5399 rate 4 and 35 rate
 * 3, 6, 9 or 10 - a third of every animation in TR5 would move differently
 * from the day it shipped. The user's rule for this whole order is "vanilla,
 * only smoother", so what goes between two drawn poses is their midpoint and
 * nothing else.
 *
 * BECAUSE BOTH TICK POSES LIE ON THE SAME KEY FRAME PAIR, their midpoint
 * does too, and the whole thing is one number:
 *
 *   same interval   pose(f-1) = lerp(K, K', ap), pose(f) = lerp(K, K', ac)
 *                   -> midpoint = lerp(K, K', (ap + ac) / 2)
 *   crossing one    f mod rate == 0, so pose(f) IS K' exactly
 *                   -> midpoint = lerp(K, K', (ap + 1) / 2)
 *
 * which is why order 4 needs no third matrix stack and no second blend: it
 * hands the existing pair a different factor. And it is why animations of
 * rate 1 - 3619 of them, every weapon-aim animation among them - which never
 * interpolated at all in vanilla, interpolate now: their two ticks are two
 * adjacent stored frames and the midpoint is the pair blended at a half. */

/* VANILLA'S OWN KEY FRAME FACTOR, as the three branches of game\draw.cpp
 * choose it - InterpolateMatrix, aInterpolateMatrix and the two arm matrix
 * functions all classify the pair (frac, rate) exactly this way, and this is
 * the single place that classification now lives.
 *
 * frac == 0 means the item sits ON a stored frame and nothing is blended. */
inline float InterpKeyAlpha(long frac, long rate)
{
	if (!frac)
		return 0.0F;

	if (rate == 2 || (frac == 2 && rate == 4))
		return 0.5F;

	if (frac == 1)
		return 0.25F;

	return 0.75F;
}

/* THE LAST INTERVAL OF AN ANIMATION IS SHORTER THAN THE OTHERS, and GetFrames
 * says so by shortening the rate it reports:
 *
 *     num = rate * (key + 1);
 *     if (num > anim->frame_end) rate += anim->frame_end - num;
 *
 * transcribed as it stands, comparison included. It compares a frame counted
 * from the animation's own start against a frame_end counted from the level's
 * - Core's own mismatch, which fires on the animations whose frame_base is
 * small and never on the rest. It is vanilla's arithmetic and it decides
 * vanilla's factor, so it is reproduced and not repaired. */
inline long InterpKeyRate(long rate, long key, long frameEnd)
{
	const long num = rate * (key + 1);

	return num > frameEnd ? rate + frameEnd - num : rate;
}

/* THE MIDPOINT OF THE TWO POSES, as a key frame index and a factor.
 *
 * `t` is the time alpha of the drawn frame - a half for the frame that falls
 * between two logic ticks. Returns false when there is no pose to blend, and
 * every one of those cases is rule K6 of the plan:
 *
 *   the frame did not step by exactly one   an animation that jumped, a loop
 *                                           that wrapped to its own start, a
 *                                           machine standing still, a frame
 *                                           number written by hand. There is
 *                                           no neighbouring pose to blend
 *                                           with, so the current one is drawn
 *                                           and the picture is what thirty
 *                                           hertz shows for that one frame.
 *   the animation has no rate at all        nothing to walk.
 *   the current frame is the animation's    the previous pose belongs to
 *   first                                   another animation.
 *
 * The animation NUMBER changing is also K6 and is tested by the caller, which
 * is the only one that knows it (game\draw.cpp, GetFramesAt). */
inline bool InterpPoseAt(long prevFrame, long curFrame, long frameBase, long rate,
	long frameEnd, float t, long* key, float* blend)
{
	if (rate < 1)
		return false;

	if (curFrame != prevFrame + 1)
		return false;

	const long f = curFrame - frameBase;
	const long p = prevFrame - frameBase;

	if (p < 0 || f < 1)
		return false;

	const long kp = p / rate;
	const long kc = f / rate;
	const float ap = InterpKeyAlpha(p % rate, InterpKeyRate(rate, kp, frameEnd));
	const float ac = (kc == kp) ? InterpKeyAlpha(f % rate, InterpKeyRate(rate, kc, frameEnd)) : 1.0F;

	*key = kp;
	*blend = ap + (ac - ap) * t;

	return true;
}

/* ---------------------------------------------------------- the blend itself
 *
 * ONE FACTOR IN PLACE OF FOUR HARD-WIRED BRANCHES, and it has to come out bit
 * for bit where the branches did, or the thirty hertz picture is not the one
 * that shipped (plan, K11 and proof 6).
 *
 * The float form keeps vanilla's three EXPRESSIONS rather than one algebraic
 * rearrangement of them - `(a + b) * 0.5` and `a + (b - a) * 0.25` and `b -
 * (b - a) * 0.25` round differently in their last bit, and a matrix element
 * that differs in its last bit is a frame that is not byte-identical. The
 * split at a half is not a special case bolted on: it is the same rule the
 * integer form needs anyway, which is to work from whichever end is nearer. */
inline float InterpBlendFloat(float a, float b, float alpha)
{
	if (alpha == 0.5F)
		return (a + b) * 0.5F;

	if (alpha > 0.5F)
		return b - (b - a) * (1.0F - alpha);

	return a + (b - a) * alpha;
}

/* THE INTEGER STACK ROUNDS BY SHIFTING, which is a floor and not a
 * truncation, and it matters: `(-3) >> 2` is -1 where `(long)(-3 * 0.25f)` is
 * 0. Vanilla's three branches are `a + (d >> 1)`, `a + (d >> 2)` and `b - (d
 * >> 2)`; in EIGHTHS those are the same expression twice,
 *
 *     alpha8 <= 4 :  a + ((d * alpha8) >> 3)
 *     alpha8 >  4 :  b - ((d * (8 - alpha8)) >> 3)
 *
 * because `(d * 4) >> 3` IS `d >> 1` and `(d * 2) >> 3` IS `d >> 2`, for
 * every value including the negative ones. So the integer form is exact, has
 * no float in it at all, and needs no floor call.
 *
 * EIGHTHS ARE ENOUGH, not a compromise. Vanilla's factors are quarters; the
 * midpoint of two of them is an eighth; and nothing in this build produces
 * anything else. A future variable time alpha (plan, order 6) would be
 * rounded here to the nearest eighth of a key frame interval - about one
 * two-hundred-and-fortieth of a second of animation - and that is written
 * down here so that it is a decision and not a surprise. */
inline long InterpEighths(float alpha)
{
	long e = (long)(alpha * 8.0F + 0.5F);

	if (e < 0)
		e = 0;

	if (e > 8)
		e = 8;

	return e;
}

inline long InterpBlendLong(long a, long b, long alpha8)
{
	const long d = b - a;

	return alpha8 <= 4 ? a + ((d * alpha8) >> 3) : b - ((d * (8 - alpha8)) >> 3);
}

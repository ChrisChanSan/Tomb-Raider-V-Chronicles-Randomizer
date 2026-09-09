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
#include <math.h>	// sqrt - the camera aim rule of fix round R

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
 * in one thirtieth of a second is a snap in the animation, not a rotation.
 * Fix round R made that literal: InterpHalfTurn below names the case, and
 * ApplyNode (game\interpolate.cpp) does not blend a rotation that is one. */
inline short InterpLerpAngle(short a, short b, float alpha)
{
	const short d = (short)(b - a);

	return (short)(a + (short)((float)d * alpha));
}

/* A HALF TURN IN ONE TICK IS A SNAP (#148, fix round R).
 *
 * turn180_effect (game\effects.cpp) is the one thing in this engine that
 * turns an item by exactly 32768 between two ticks: `y_rot -= 0x8000`, and
 * x_rot negated in the same breath. Seven of Lara's animations fire it from
 * an animation command (AnimCmd over ANDREA1: 48, 147, 205, 207, 210, 212 and
 * the level's own 451 - the roll, the roll in water, the twist of the jump
 * back), most of them on their last frame, and the animation's own root bone
 * turns the other half so that the picture at thirty presents a second is
 * continuous.
 *
 * Blended apart, the two halves do not cancel. The root took the arbitrary
 * anticlockwise half of InterpLerpAngle above, while the pose was refused on
 * that same tick - by rule K6 where the animation changes at the flip, by
 * InterpPoseFar's ninety degrees where it does not - and so was already the
 * new one: Lara stood sideways for one tween frame. The play test saw it
 * with HD off as well, which is what named the blend. TROne has no such rule
 * because it blends the finished node matrices, in which the two halves have
 * cancelled before the blend; nothing of that was taken here.
 *
 * So a node whose y difference is exactly half a turn is not blended in
 * rotation at all: the whole rotation keeps the live value and only the
 * position blends. Nothing turns half a turn in a thirtieth of a second
 * under its own power; the rule can only ever meet a snap. */
inline bool InterpHalfTurn(short a, short b)
{
	return (short)(b - a) == (short)-32768;
}

/* THE NINTH STEP OF A SCREEN FADE (#148, fix round W).
 *
 * SetFade (specific\specificfx.cpp) walks FadeVal from `start` to `end` in
 * eight steps of (end - start) >> 3, and DoScreenFade calls the ninth call
 * the end: it reads the value BEFORE it steps, and on the ninth call it puts
 * `end` over whatever it read. For 255 -> 0 the step is -32 and eight of
 * them land on -1 - which is alpha 255 once shifted into the colour. At
 * thirty presents a second that never shows, because the ninth call is the
 * one that reads it and it overrides. Every in-between frame between the
 * eighth and the ninth tick read it as it stood: a whole tick of black in
 * the middle of every level's fade-in - the play test's "flickers black when
 * I load", the video's two black frames at sixty.
 *
 * Clamped to the end once past it. The ninth tick drew `end` anyway, so no
 * thirty hertz picture changes; the in-between frames draw `end` too. For a
 * walk that does not overshoot (0 -> 255 lands on 248) nothing changes at
 * all. */
inline long InterpFadeClamp(long val, long step, long end)
{
	if (step < 0 && val < end)
		return end;
	if (step > 0 && val > end)
		return end;
	return val;
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
 * ORDER 6 - THE BLEND FACTOR WHEN THE PANEL SETS THE PACE.
 *
 * At fps_mode 1 the factor needs no clock: the frames land on the ticks, so
 * `framecount < 0 ? 0.5 : 1` is exact and cannot drift (InterpAlphaNow). Bind
 * the rate to a 144 Hz display instead and the frames land wherever they
 * land - two out of three inside one 1/60 s tick - so the factor has to come
 * from the clock. This is that arithmetic, and it is the whole of it.
 *
 * WHAT THE TWO NUMBERS ARE.
 *
 *   framecount  ControlPhase's accumulator (game\control.h), in 1/60 s
 *               ticks, AFTER the last ControlPhase and therefore as the top
 *               of DrawPhaseGame finds it. `for (framecount += nframes;
 *               framecount > 0; framecount -= 2)` leaves it at -1 or 0, and
 *               nothing else ever writes it.
 *   subTick     how far past the last tick boundary Sync() gave away the
 *               clock stands (specific\time.cpp, TIME_SubTick). It is NOT
 *               capped at 1: Sync() runs at the END of a frame, so at the
 *               top of the next one a whole tick has usually gone by.
 *
 * WHERE THE +1 COMES FROM, because it is the one term that is not obvious.
 * Count time in ticks. Let S be the whole ticks Sync() has handed out and k
 * the logic steps run, so framecount = S - 2k and the clock stands at
 * S + subTick. The loop spends two ticks per step and runs a step as soon as
 * the accumulator is positive, so step k happens at S = 2k-1: the state after
 * it is the newest there is, and it stays the newest until S = 2k+1. For the
 * frames drawn in that whole window to be an INTERPOLATION between the two
 * states rather than a guess past the newer one, the newer state has to be
 * read as the world at S = 2k+1. Then
 *
 *     alpha = (now - (2k-1)) / 2 = (S + subTick - 2k + 1) / 2
 *           = (framecount + subTick + 1) / 2
 *
 * and it walks 0 -> 1 across every logic step, at any refresh rate, with no
 * accumulator of its own and nothing to drift.
 *
 * AND IT IS THE SAME NUMBER AT SIXTY. Frames one tick apart arrive with
 * subTick at 1 (the previous frame's Sync consumed the boundary a whole tick
 * ago), so the pair (-1, 1) gives 0.5 and (0, 1) gives 1.0 - the parity rule
 * of order 2, to the bit, out of the clock. That is not a coincidence to be
 * grateful for, it is the check that the derivation is right, and InterpTest
 * asserts it.
 *
 * THE CLAMP IS FOR HITCHES ONLY. A frame that took longer than the logic had
 * states for would ask for a factor past 1, i.e. for a position the game has
 * not computed. It is given the newest state instead - the same thing thirty
 * hertz shows on a slow frame - and ControlPhase catches the ticks up
 * afterwards, so no game time is lost. */
inline float InterpRefreshAlpha(long framecount, double subTick)
{
	double a = ((double)framecount + subTick + 1.0) * 0.5;

	if (a < 0.0)
		a = 0.0;

	if (a > 1.0)
		a = 1.0;

	return (float)a;
}

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


/* ==========================================================================
 * ORDER 4 - THE PAIR A MATRIX BLEND CANNOT SURVIVE (#148; #123 fix round S, S2)
 * ==========================================================================
 *
 * THE PLAY TEST. A dive roll in the third person at sixty hertz, three
 * captures: tucked, ONE frame with a leg fully extended the other way round,
 * tucked again. The first reading was a packed ten bit angle interpolated the
 * long way round - 1000 to 20 giving 510 instead of 1022.
 *
 * THAT READING IS WRONG, and it is worth writing down why, because the right
 * answer is in the same place. Order 4 does not interpolate ANGLES anywhere:
 * GetFramesAt hands the key frame PAIR and a factor to InitInterpolateAlpha,
 * and the blend is vanilla's own InterpolateMatrix, which lerps the two 3x4
 * MATRICES element by element (game\draw.cpp). An element-wise lerp of two
 * rotations always takes the SHORT way - at a half it is the short-way
 * bisector, scaled - so the wrap cannot happen. Measured as well, ModelProbe
 * -poserisk over all 319 Lara animations of ANDREA1: not one adjacent key
 * frame pair has a per-axis packed difference over 512 after the wrap, and a
 * wrap cannot produce one.
 *
 * WHAT AN ELEMENT-WISE LERP DOES INSTEAD IS SHRINK. Write the two node frames
 * as A and B with a relative rotation of D degrees about some axis. At a half,
 *
 *     (A + B) / 2  =  A * (I + R(D)) / 2
 *
 * which is the identity on the axis and cos(D/2) times a rotation of D/2 in
 * the plane across it. At D = 60 that is a 13 % shrink of the bone's own
 * frame; at 90 it is 29 %; at 180 the plane collapses to NOTHING and the
 * frame is singular - the bone has no length in that plane at all and
 * whatever hangs off it lands somewhere that is on neither of the two poses.
 * A leg pointing the wrong way is what that looks like.
 *
 * SO THE QUESTION IS WHICH PAIRS ORDER 4 BLENDS THAT VANILLA NEVER DID, and
 * the answer is one word: rate. At thirty hertz vanilla blends key frames k
 * and k+1 at a quarter, a half or three quarters - and only when the
 * animation's rate is MORE THAN ONE, because with rate 1 every frame is a key
 * frame and `frac` is always zero. Order 4 blends the rate 1 animations too:
 * their two ticks are two adjacent stored frames, a whole game frame apart,
 * and the midpoint is that pair at a half. For every other rate the tween
 * frame lands INSIDE the same segment the two tick frames are blended on, so
 * it is no worse than its own neighbours and refusing it would be a jump.
 *
 * MEASURED, ModelProbe -poserisk, ANDREA1: 319 Lara animations, 23 of them
 * rate 1. Twenty-two of those twenty-three turn at most 52.7 degrees between
 * two adjacent frames at every one of the fifteen nodes - a shrink of ten per
 * cent, which is what sixty hertz is supposed to smooth. The twenty-third is
 * animation 205, whose HIPS - node 0, the root of the whole body - turn
 * 179.9 degrees between key 0 and key 1. That one frame is the picture.
 *
 * SO: a rate 1 pair whose turn is too big is not blended at all, and the
 * frame is drawn exactly as thirty hertz draws it. That is rule K6 of the
 * plan with one more entry, it cannot reach fps_mode 0 (nothing here runs
 * unless InterpPoseTime asked for a tween frame), and it cannot change a
 * picture vanilla draws, because vanilla does not draw this pair blended
 * either.
 *
 * THE LIMIT IS NINETY DEGREES, which is the last turn whose midpoint keeps
 * more than seventy per cent of the bone's frame. In ANDREA1 it refuses
 * animation 205 and nothing else. */
#define INTERP_POSE_TURN_COS	0.0F	/* cos 90 degrees: trace = 1 + 2*cos */

/* ONE NODE'S PACKED ROTATION, and the cursor stepped past it (game\draw.cpp,
 * gar_RotYXZsuperpack). Two words when the top two bits are zero and three
 * TEN bit angles follow; one word otherwise, naming a single axis with TWELVE
 * bits. The angles come back in the engine's own 16 bit space. */
inline void InterpUnpackRot(const short*& p, short* xyz)
{
	const unsigned short w = (unsigned short)*p;

	xyz[0] = xyz[1] = xyz[2] = 0;

	if ((w & 0xC000) == 0)
	{
		const unsigned short w2 = (unsigned short)p[1];
		const unsigned long packed = ((unsigned long)w << 16) | (unsigned long)w2;

		xyz[0] = (short)((short)((packed >> 20) & 0x3FF) << 6);
		xyz[1] = (short)((short)((packed >> 10) & 0x3FF) << 6);
		xyz[2] = (short)((short)(packed & 0x3FF) << 6);
		p += 2;
		return;
	}

	xyz[(w >> 14) == 1 ? 0 : (w >> 14) == 2 ? 1 : 2] =
		(short)((short)(w & 0xFFF) << 4);
	p++;
}

/* The node's frame out of its three packed angles, row major 3x3, in
 * phd_RotYXZ's own order: Y, then X, then Z. The order has to match the
 * engine's, because the test below is about the frames the engine really
 * builds; the SCALE does not, and there is none here. */
inline void InterpPoseRot(const short* xyz, float* m)
{
	const float k = 3.14159265358979323846F / 32768.0F;
	const float sy = sinf((float)xyz[1] * k), cy = cosf((float)xyz[1] * k);
	const float sx = sinf((float)xyz[0] * k), cx = cosf((float)xyz[0] * k);
	const float sz = sinf((float)xyz[2] * k), cz = cosf((float)xyz[2] * k);

	/* Ry then Rx then Rz, composed as Rz * Rx * Ry. */
	m[0] = cz * cy + sz * sx * sy;
	m[1] = sz * cx;
	m[2] = cz * -sy + sz * sx * cy;

	m[3] = -sz * cy + cz * sx * sy;
	m[4] = cz * cx;
	m[5] = sz * sy + cz * sx * cy;

	m[6] = cx * sy;
	m[7] = -sx;
	m[8] = cx * cy;
}

/* cos of the angle between two node frames: trace(A^T B) = 1 + 2 cos. */
inline float InterpPoseTurnCos(const float* a, const float* b)
{
	float t = 0.0F;

	for (int i = 0; i < 3; i++)
		for (int k = 0; k < 3; k++)
			t += a[i * 3 + k] * b[i * 3 + k];

	return (t - 1.0F) * 0.5F;
}

/* TRUE when the two frames are too far apart at any node for an element-wise
 * matrix blend - the whole rule above in one call, so that game\draw.cpp and
 * InterpTest ask exactly the same question. `nodes` is the model's own mesh
 * count; the two pointers are the frames themselves, and the nine shorts of
 * bounding box and offset are stepped over here. */
inline bool InterpPoseFar(const short* frameA, const short* frameB, int nodes,
	float cosLimit)
{
	const short* a = frameA + 9;
	const short* b = frameB + 9;

	for (int node = 0; node < nodes; node++)
	{
		short ra[3], rb[3];
		float ma[9], mb[9];

		InterpUnpackRot(a, ra);
		InterpUnpackRot(b, rb);

		if (ra[0] == rb[0] && ra[1] == rb[1] && ra[2] == rb[2])
			continue;

		InterpPoseRot(ra, ma);
		InterpPoseRot(rb, mb);

		if (InterpPoseTurnCos(ma, mb) < cosLimit)
			return true;
	}

	return false;
}

/* ==========================================================================
 * ORDER 5 - THE PARTICLES, AND THE ONE LINE OF ARITHMETIC THEY NEED.
 *
 * Orders 2 and 4 blend between a REMEMBERED previous state and the live one.
 * A particle needs no such memory, because it carries the thing the snapshot
 * exists to reconstruct: its own velocity. Every particle simulation in this
 * engine has the same last two statements -
 *
 *     sptr->Yvel += sptr->Gravity;       // whatever changes the velocity
 *     sptr->x += sptr->Xvel >> 5;        // ... then the step it takes
 *
 * - the velocity is settled FIRST and the position moves by it SECOND, so the
 * stored velocity is exactly the step the last tick took and
 *
 *     previous position = current position - step
 *
 * is not an estimate. It is the same number the snapshot would have held, for
 * a thousand sparks, two thousand raindrops and every drop of blood, at a
 * cost of zero bytes and with nothing to restore afterwards.
 *
 * THE TIME AXIS IS THE SAME ONE, AND THAT IS THE WHOLE POINT. Order 2 draws a
 * body at `prev + (cur - prev) * alpha`, i.e. the world at (tick - 1) + alpha.
 * If a particle were drawn at `cur + vel * alpha` - the obvious reading of
 * "move it along by the fraction" - it would show the world at tick + alpha,
 * one whole logic step AHEAD of everything around it: sparks running out in
 * front of the gun that made them, rain falling through a floor that has not
 * arrived yet. So the particle is walked BACKWARDS from the state the game
 * holds, never forwards, and the identity below is exact rather than close:
 *
 *     InterpTrailLong(cur, step, alpha) == InterpLerpLong(cur - step, cur, alpha)
 *
 * It is written as that lerp and not as `cur - step * (1 - alpha)`, which is
 * the same line of algebra and NOT the same integer: a C cast truncates
 * towards zero, so the two forms disagree by one unit on odd differences and
 * a particle would sit one unit away from where the item machinery would have
 * put it. InterpTest asserts the identity over a simulated particle at both
 * frame rates rather than trusting the rearrangement.
 *
 * At fps_mode 0 nothing calls this - InterpParticleAlpha returns 1 and every
 * draw site keeps its own vanilla expression - and at alpha 1 it would return
 * `cur` unchanged anyway. */
inline long InterpTrailLong(long cur, long step, float alpha)
{
	return InterpLerpLong(cur - step, cur, alpha);
}

/* ==========================================================================
 * FIX ROUND R - WHERE A CAMERA TARGET IS A PLACE AND WHERE IT IS A DIRECTION.
 *
 * Order 2 declares a camera CUT when the eye or the target moved further than
 * INTERP_CAM_SNAP in one logic tick. For every camera the engine had when
 * that was written the target is a PLACE a couple of sectors away - Lara's
 * torso for the chase camera, a fixed camera's own look-at point - and a jump
 * of a whole sector really is a cut.
 *
 * THE FIRST PERSON CAMERA BREAKS THAT, and the play test of 2026-09-07 found
 * it: FirstPersonCamera (game\camera.cpp) builds its target as
 *
 *     camera.target = camera.pos + direction * 0x5100
 *
 * - twenty thousand seven hundred and thirty-six units down the line of
 * sight, because what it wants to say is WHICH WAY she is looking and the
 * engine's camera has no field for that. On such a target a rotation of
 * 2.8 degrees moves the point a whole sector, and Lara's idle animation turns
 * her head by more than that between two ticks. So the cut fired ten to
 * seventeen times a second while she STOOD STILL (INTERPLOG camcut=10..17),
 * and on every one of those frames the camera was drawn live while the world
 * around it was drawn half a step back: a shudder at every fourth frame, the
 * "four models dragged along behind her", and room seams that walk with her
 * breathing.
 *
 * THE RULE. Beyond INTERP_CAM_FAR_TARGET the target is not a place, it is a
 * direction, so it is measured as one: both targets are pulled back onto a
 * sphere of INTERP_CAM_DIR_RADIUS about THEIR OWN eye and the threshold is
 * applied to what is left, which is the change of DIRECTION alone. The eye's
 * own travel is not measured twice - it has its own test, with the same
 * threshold, right beside this one.
 *
 * WHAT THAT IS WORTH IN DEGREES. At a radius of 1024 a Manhattan sum of 1024
 * is a turn of between 57 and 41 degrees, depending on the plane the turn
 * happens in. In one thirtieth of a second that is 1200 to 1700 degrees a
 * second - nothing a body, a head or a look control in this game can do -
 * while a real cut, which is a new view and not a turn, is 60 degrees or more
 * and is caught. A ninety degree cut measures 1448 and a half turn 2048.
 *
 * AND THE CHASE CAMERA IS NOT TOUCHED. Its target sits on Lara, a sector and
 * a half from the eye, which is under INTERP_CAM_FAR_TARGET - the branch is
 * not taken and the test is the absolute one it always was, to the bit. */
#define INTERP_CAM_FAR_TARGET	4096L
#define INTERP_CAM_DIR_RADIUS	1024L

/* The target as this rule measures it, RELATIVE TO ITS OWN EYE: the offset
 * itself while the target is a place, and the offset scaled onto the sphere
 * while it is a direction.
 *
 * IT IS NO LONGER THE RULE, and the sentence that stood here said the
 * opposite (#123, fix round S, S3(a); this note corrected in fix round U,
 * U5). It claimed that asking ONE camera at a time was what kept the two
 * sides of the comparison from being reduced differently - and asking one
 * camera at a time is exactly what put a cliff on the shell, because a
 * target at 4095 was then a place and the same target at 4097 a direction.
 * InterpCamAimCut below chooses the branch once for the PAIR and scales both
 * sides by the same k; nothing in the ENGINE calls this any more.
 *
 * IT IS STILL CALLED, AND DELETING IT WOULD BREAK A TEST (corrected in U7):
 * Preparation\Tools\Diagnose\InterpTest.cpp measures the old rule against the
 * new one, and it includes THIS header rather than a copy of it, which is the
 * whole point of the tool. It stays for that, because it is the readable
 * statement of what an aim offset IS, and because InterpCamAimCut's arithmetic
 * is only recognisable as this one beside it. */
inline void InterpCamAim(long px, long py, long pz, long tx, long ty, long tz,
	long* ax, long* ay, long* az)
{
	const double dx = (double)(tx - px);
	const double dy = (double)(ty - py);
	const double dz = (double)(tz - pz);
	const double len = sqrt(dx * dx + dy * dy + dz * dz);

	if (len <= (double)INTERP_CAM_FAR_TARGET)
	{
		*ax = tx - px;
		*ay = ty - py;
		*az = tz - pz;
		return;
	}

	const double k = (double)INTERP_CAM_DIR_RADIUS / len;

	*ax = (long)(dx * k);
	*ay = (long)(dy * k);
	*az = (long)(dz * k);
}

/* Did the VIEW DIRECTION cut between these two cameras? The whole of the rule
 * above in one call, so that game\interpolate.cpp and InterpTest ask exactly
 * the same question.
 *
 * THE BRANCH IS CHOSEN ONCE FOR THE PAIR AND BOTH SIDES TAKE THE SAME SCALE
 * (#123, fix round S, S3(a)). Fix round R asked each camera on its own whether
 * its target was far, which put a seam right on the shell: a target at 4095
 * was compared as a PLACE, 4095 units from its eye, and the same target one
 * unit further out as a DIRECTION, 1024 units from its eye - a difference of
 * three thousand units out of nothing at all, on every frame a target hovered
 * there. It could not reach the two cameras the engine really has (a first
 * person target sits at 20736 and a chase target at about 1500), but it was a
 * cliff in the middle of the rule and a rule with a cliff in it is a bug
 * waiting for the third camera.
 *
 * So: FAR if EITHER target is beyond INTERP_CAM_FAR_TARGET, and then both
 * offsets are scaled by the same k - INTERP_CAM_DIR_RADIUS over the LONGER of
 * the two lengths. One k and not one each, because two different k's would
 * hide a target that really did jump along its own ray. Under the shell
 * nothing is scaled and the comparison is the absolute one order 2 shipped,
 * to the bit. */
inline bool InterpCamAimCut(long ppx, long ppy, long ppz, long ptx, long pty, long ptz,
	long px, long py, long pz, long tx, long ty, long tz, long snap)
{
	const double adx = (double)(ptx - ppx);
	const double ady = (double)(pty - ppy);
	const double adz = (double)(ptz - ppz);
	const double bdx = (double)(tx - px);
	const double bdy = (double)(ty - py);
	const double bdz = (double)(tz - pz);
	const double alen = sqrt(adx * adx + ady * ady + adz * adz);
	const double blen = sqrt(bdx * bdx + bdy * bdy + bdz * bdz);
	const double longer = alen > blen ? alen : blen;

	long a[3] = { ptx - ppx, pty - ppy, ptz - ppz };
	long b[3] = { tx - px, ty - py, tz - pz };

	if (longer > (double)INTERP_CAM_FAR_TARGET)
	{
		const double k = (double)INTERP_CAM_DIR_RADIUS / longer;

		a[0] = (long)(adx * k);
		a[1] = (long)(ady * k);
		a[2] = (long)(adz * k);
		b[0] = (long)(bdx * k);
		b[1] = (long)(bdy * k);
		b[2] = (long)(bdz * k);
	}

	return InterpManhattan(b[0] - a[0], b[1] - a[1], b[2] - a[2]) > snap;
}

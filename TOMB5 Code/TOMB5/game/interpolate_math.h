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

/* THE HAND-OVER THAT IS NOT A CUT (#148, fix round X).
 *
 * camera_type by value - game\global\types.h names them CHASE_CAMERA 0,
 * FIXED_CAMERA 1, LOOK_CAMERA 2, COMBAT_CAMERA 3, CINEMATIC_CAMERA 4,
 * HEAVY_CAMERA 5, and interpolate.cpp static_asserts the three below against
 * the enum, because this file is built without the engine for InterpTest.
 *
 * Chase, look and combat are the three cameras that hand the view over to
 * each other SMOOTHLY: the engine moves the eye towards the new camera's
 * place at camera.speed, the target stays Lara's, and nothing jumps.
 * Drawing a weapon is one of those hand-overs (chase -> combat,
 * game\larafire.cpp), and so is looking around. Fix round R's cut rule took
 * every change of camera.type or camera.old_type for a cut, drew that tick
 * live, and blended the next one half a tick back: the play test's picture
 * that "jumps irregularly back and forth" at every draw in third person,
 * 45 cuts in one log all of the type kind, not one of the eye kind. First
 * person was smooth because its camera never changes type.
 *
 * So a change of type is a cut only when it LEAVES that family - a fixed
 * camera, a heavy one, a cinematic - and a hand-over inside it is not. The
 * eye and aim tests beside this still catch a real jump whatever the type
 * says, and camera.fixed_camera changing is a cut on its own. */
#define INTERP_CAM_CHASE	0
#define INTERP_CAM_LOOK		2
#define INTERP_CAM_COMBAT	3

inline bool InterpCamSmoothType(int type)
{
	return type == INTERP_CAM_CHASE || type == INTERP_CAM_LOOK || type == INTERP_CAM_COMBAT;
}

/* True when the change from `prevType` to `type`, or from `prevOld` to
 * `oldType`, is a cut. An unchanged pair never is. */
inline bool InterpCamTypeCut(int prevType, int type, int prevOld, int oldType)
{
	if (prevType != type && !(InterpCamSmoothType(prevType) && InterpCamSmoothType(type)))
		return true;
	if (prevOld != oldType && !(InterpCamSmoothType(prevOld) && InterpCamSmoothType(oldType)))
		return true;
	return false;
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
 * THE INVENTORY RING'S OWN CADENCE (#154).
 *
 * The ring is a modal loop with a present of its own and never reaches
 * ControlPhase, so it keeps an accumulator of its own - of exactly
 * ControlPhase's shape, in the same 1/60 s ticks, spending two of them on a
 * logic step. The three functions below are the whole of that arithmetic and
 * they live here, with the rest of the arithmetic, so that InterpTest can run
 * them without a game. game\inventory_interp.cpp is the rest of the round -
 * the snapshot, the wiring and the log.
 * ======================================================================== */

/* SPEND THE TICKS THE LAST PRESENT RETURNED AND SAY WHETHER THIS FRAME
 * CARRIES THE RING'S LOGIC STEP.
 *
 * `for (framecount += nframes; framecount > 0; framecount -= 2)`
 * (game\control.cpp) with the body run AT MOST ONCE, because the ring's body
 * draws as it steps and a second pass in one frame would draw twice into one
 * poly list. A frame slower than a logic step therefore steps the ring once,
 * which is what thirty hertz does with a long frame too.
 *
 * THE CLAMP IS THE ONE LINE CONTROLPHASE HAS NOT GOT. ControlPhase pays a
 * hitch off by running several ticks in one call; this loop cannot, so an
 * unclamped accumulator would keep it stepping on EVERY frame until the debt
 * was gone - the ring running at the frame rate, which is the one thing this
 * round exists to stop. One step's worth of credit is kept and the rest of a
 * hitch is dropped. */
inline bool InterpRingStep(long* acc, long ticks)
{
	*acc += ticks;

	if (*acc > 2)
		*acc = 2;

	if (*acc > 0)
	{
		*acc -= 2;
		return true;
	}

	return false;
}

/* WHAT A RING FRAME IS DRAWN AT.
 *
 * At fps_mode 2 it is the clock, through InterpRefreshAlpha above and with
 * the ring's accumulator in place of ControlPhase's - the derivation is the
 * same one and the phase is the same, because the last Sync() was inside the
 * previous present in both loops.
 *
 * At fps_mode 1 the frames land on the ticks and that formula returns exactly
 * a half on the frame whose step ran (acc -1, subTick 1) and exactly one on
 * the frame between (acc 0, subTick 1), so the parity is used directly and no
 * clock is read. InterpTest asserts that the two agree.
 *
 * AND THE HALF IS ON THE STEP FRAME, which reads backwards until the order of
 * the loop is written out. In the game the DRAW comes before the tick, so the
 * drawn states are T1, mid(T1,T2), T2 - the live frame first. In the ring the
 * STEP comes before the draw, and the same arrangement would draw C1,
 * mid(C0,C1), C2, mid(C1,C2): a picture walking backwards on every second
 * frame. Even, monotone half steps are the other way round - mid(C0,C1), C1,
 * mid(C1,C2), C2 - and that is what this returns. */
inline float InterpRingAlpha(long mode, long acc, double subTick, bool step)
{
	// fps_mode 0 is the ring as it was: every frame carries its step and is
	// drawn live. InvInterpBeginFrame returns before it ever gets here, and
	// the line is in anyway so that the rule is whole where it is written.
	if (mode == 0)
		return 1.0F;

	if (mode == 2)
		return InterpRefreshAlpha(acc, subTick);

	return step ? 0.5F : 1.0F;
}

/* THE SLIDE, AND THE ONE JUMP IN IT.
 *
 * objlistmovement walks to 65536 in eight steps of 8192 and is then set
 * straight back to 0 with curobjinlist moved on by one. That is a reset and
 * not a motion: blended, the whole row would slide halfway back for one
 * frame. A pair that straddles it draws the newer of the two - the value the
 * step frame itself shows - so the picture stands for the two frames the jump
 * takes and moves again on the next pair.
 *
 * `older` and `newer` are the two previous generations and not the live
 * field, because this is the one field vanilla steps AFTER it has drawn with
 * it (game\inventory_interp.h). */
inline long InterpRingSlide(long older, long newer, bool jumped, float alpha)
{
	if (jumped)
		return newer;

	return InterpLerpLong(older, newer, alpha);
}

/* AND THE SLOT THE SLIDE BELONGS TO.
 *
 * curobjinlist is discrete and is never blended - but it is HALF OF A PAIR
 * with the slide, and the two have to be drawn from the same generation. The
 * reset makes that literal: the step that puts objlistmovement back to 0 moves
 * curobjinlist in the same breath, so a frame drawing the NEW slot with the
 * OLD offset puts the whole row seven eighths of a slot to the side for one
 * frame. That is a bug this round had and the arithmetic below is its fix.
 *
 * `count` is numobjectsinlist as it stands now: a slot from before a list was
 * rebuilt can be past the end of the new one, and then the live slot is the
 * only safe answer. */
inline long InterpRingSlot(long snapCur, long liveCur, long count)
{
	if (snapCur < 0 || snapCur >= count)
		return liveCur;

	return snapCur;
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
 *                                           and the LIMBS are what thirty
 *                                           hertz shows for that one frame.
 *                                           The ROOT under them is not - order
 *                                           2 has already blended it - and
 *                                           #157 is what makes the two agree
 *                                           again; see InterpRootKey below.
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
 * ORDER 4'S ROOT - WHAT K6 REFUSES AND WHAT IT MUST NOT (#157)
 * ==========================================================================
 *
 * THE PLAY TEST. JOBY5, a ladder, sixty frames a second: three consecutive
 * captures of the same climb show Lara in the same pose family from the same
 * camera, and the MIDDLE one has her half a rung lower than the two either
 * side of it. The same ladder at thirty is faultless.
 *
 * WHERE THE HALF RUNG COMES FROM. Every stored frame carries a bounding box
 * and then the ROOT OFFSET - frm[6], frm[7], frm[8] - which the draw applies
 * after the item's own rotation (game\draw.cpp, DrawAnimatingItem;
 * game\delstuff.cpp for Lara). A climb carries its whole vertical travel in
 * those offsets, and the engine puts item->pos where the offsets had taken
 * her at the moment the animation wraps - the TranslateItem animation command
 * of game\laramisc.cpp, AnimateLara. So at thirty hertz the DRAWN root, pos
 * plus the rotated offset, is the same number on both sides of the wrap.
 *
 * Sixty hertz breaks that agreement on exactly one frame, and it takes both
 * halves of the interpolation to do it:
 *
 *   order 2 has blended item->pos HALF WAY across the wrap - it is a move of
 *   at most 512 units, which is not a teleport and must not snap;
 *   rule K6 refuses the POSE - the frame stepped back to the animation's own
 *   start, so there is no neighbouring pose to blend with - and the draw
 *   falls back to the current tick's frame, whose offset is the FIRST
 *   frame's, near nothing, rather than the last frame's.
 *
 * Half the step is then missing from the picture: drawn = Y + D/2 where both
 * neighbours drew Y + D. At sixty that is one frame; on a 165 Hz panel the
 * same tween state is presented four or five times in a row, which is what
 * the player photographed.
 *
 * THE RULE, AND IT IS ONE SENTENCE: K6 refuses the POSE, never the ROOT. On a
 * refused tween frame the offset is blended in TIME between what the previous
 * tick drew and what this one draws, and the two halves then add up to the
 * whole step - pos contributes D/2 and the offset the other D/2 - so the
 * in-between frame lands exactly where both of its neighbours are (NARROWED
 * where nothing else moved with it - see InterpRootNarrow below, #157b). Where the
 * two ticks' offsets are equal, which is every animation that does not carry
 * its own travel and every item standing still, the blend comes out at that
 * same offset - and InterpRootNow then says so and hands the draw NOTHING, so
 * the line that has always stood at the draw site runs and the picture is the
 * one thirty hertz shows, to the bit.
 *
 * THE PAIR THE CURRENT TICK DRAWS is vanilla's own: frm[0] alone when the
 * frame sits on a stored one, and otherwise the key pair at InterpKeyAlpha's
 * factor, which is the `keyAlpha` handed in - GetFramesNow has already chosen
 * it and it is not classified a second time here. It is taken in INTEGERS,
 * where the draw takes it in floats through two matrix stacks: the two differ
 * by at most one unit, against the 256 this is repairing, and an integer
 * offset is the one thing both call sites can be handed.
 *
 * THAT ONE UNIT REACHES ONLY THE FRAMES THIS RULE REPAIRS, which is worth
 * saying because a refused tween frame is not rare - a door at rest, a
 * machine standing still and every other item whose frame did not step is
 * refused on every tween frame there is. InterpRootNow hands the draw a
 * blended offset only where that offset is NOT the one the frame carries;
 * everywhere else it returns false and vanilla's own float translate runs
 * untouched. So the integer form is what a repaired ladder frame is drawn
 * with, and nothing else in the game changes by even that unit.
 *
 * frm1 IS NOT READ AT frac ZERO. A frame that sits on a stored one has no
 * second key - `frm[1] = frm[0] + size` is then one frame size past it, which
 * for the last animation in a level is past the frame block altogether, and
 * vanilla never dereferences it either. */
inline void InterpRootKey(const short* frm0, const short* frm1, long frac, float keyAlpha,
	long out[3])
{
	const long alpha8 = InterpEighths(keyAlpha);

	for (int i = 0; i < 3; i++)
		out[i] = frac
			? InterpBlendLong(frm0[6 + i], frm1[6 + i], alpha8)
			: (long)frm0[6 + i];
}

/* And the two ticks' offsets at the time `t` the frame stands at - at that
 * alpha itself, through InterpLerpLong, and NOT rounded to an eighth.
 *
 * THE OFFSET HAS TO CANCEL AGAINST ORDER 2'S POSITION, and that is the whole
 * arithmetic of this rule. The drawn root is pos plus the offset; order 2 has
 * already blended pos with InterpLerpLong at this frame's alpha
 * (game\interpolate.cpp, ApplyNode); and only if the offset is blended by the
 * same primitive at the same alpha do the two halves add up to the lerp of
 * the two ticks' drawn roots. Rounding a real alpha to the nearest eighth
 * leaves up to a sixteenth of the offset's step behind - and the ladder's
 * step is 504 units, so 31 of them.
 *
 * MEASURED (fix round, scratchpad alphacheck.cpp, which links this header):
 * sweeping alpha in 1/256 steps over the wrap of animation 161, the worst
 * distance between the drawn root and the point the two ticks put it at is
 * 504 units as the picture stands today, 31 with the eighths, and 1 - the two
 * integer truncations - at the alpha itself. At fps_mode 1 alpha is a half,
 * where all three agree to the unit; fps_mode 2 is display-bound and its
 * alpha is whatever the panel's clock makes it, which is the case this
 * exactness is for.
 *
 * The KEY pair blend above keeps its eighths and loses nothing by it:
 * InterpKeyAlpha's factors are quarters, and a quarter IS two eighths. */
inline void InterpRootLerp(const long prev[3], const long cur[3], float t, long out[3])
{
	for (int i = 0; i < 3; i++)
		out[i] = InterpLerpLong(prev[i], cur[i], t);
}

/* AND THE HALF TURN TURNS THE PREVIOUS OFFSET WITH HER (#169, round 161a3).
 *
 * The two offsets InterpRootLerp blends are in the ITEM'S OWN frame, and on a
 * tick that turned half a turn those two frames are half a turn apart. The
 * draw is at the NEW one - ApplyNode (game\interpolate.cpp) keeps the live
 * rotation on exactly this tick - so the previous tick's offset is a point of
 * the OLD frame handed to the new one, and the straight blend of the two drags
 * her root through her own pivot.
 *
 * MEASURED, the underwater roll (the user's log of 2026-09-20 and RootProbe
 * over ANDREA1): animation 203's last frame 5330 carries (42,122,68), the
 * frame the change lands on - 205's frame 5423, where FlipEffect 0 fires -
 * carries (-45,122,-67), and the drawn offset walked (32,122,53), (17,122,29),
 * (1,122,4), (-15,122,-20) through the middle. Up to 140 units beside her for
 * a drawn frame or two, which is what the play test filmed.
 *
 * WHY THE WHOLE TRANSFORM IS TWO SIGNS. turn180_effect (game\effects.cpp) is
 * `y_rot -= 0x8000` AND `x_rot = -x_rot` in the same breath, so the offset has
 * to be carried by Rcur^-1 * Rprev with the engine's own Y*X*Z order. Written
 * out with the y turned by half and the x negated, that product is
 *
 *     Rz(-z) * Rx(x) * Ry(180) * Rx(x) * Rz(z)
 *
 * and Rx(a) * Ry(180) * Rx(a) is diag(-1, 1, -1) for EVERY a - the x_rot
 * negation cancels itself, which is why the effect negates it at all. With the
 * lean z equal at both ticks the two Rz cancel as well and what is left is
 * Ry(180): negate x and z, keep y. The level file says the same thing without
 * any of this arithmetic - animation 205's own key 0, the frame the change
 * skips over, carries (42,122,68), the pre-flip offset, against key 1's
 * (-45,122,-67): the authors wrote the two as the same world point, three
 * units apart, and the flip is the whole of the difference.
 *
 * THE LEAN IS THE ONE TERM LEFT, and it is written down rather than built: a
 * z_rot of z leaves Rz(-z) * Ry(180) * Rz(z), which turns the x/y pair by 2z
 * on top of the flip. Nothing here can repair that, because the ANIMATION
 * itself was authored without it - at thirty hertz the same 2z sits between
 * the two ticks' pictures. Blending the turned offset reproduces both ends of
 * vanilla's own step; inventing a lean correction would move the picture away
 * from the one thirty hertz draws.
 *
 * THE IDEA IS TODESENGEL'S, re-derived and not taken: TROne (GPLv3,
 * github.com/todesengel1931) interpolates whole node MATRICES and blends the
 * previous frame expressed in the CURRENT one, so a translation is never split
 * from the rotation it belongs to and a flip cannot come apart. This engine
 * blends Euler angles and a separate root offset, so the "express it in the
 * current frame" step has to be done by hand - and for half a turn it is two
 * signs. Nothing of that source is in this file. */
inline void InterpRootFlip(const long in[3], long out[3])
{
	out[0] = -in[0];
	out[1] = in[1];
	out[2] = -in[2];
}

/* AND IT BLENDS ONLY WHAT A POSITION STEP PAYS FOR (#157, round 157b).
 *
 * THE PLAY TEST, 2026-09-20, 165 Hz panel, fps_mode 2: *bei dem schnellen
 * Wechsel von kriechen zum aufstehen ist der gleiche Interpolationsbug wie an
 * der Leiter, 1 Frame steht sie hoeher als ihre Wurzel.* His own -log says it
 * in numbers - the crawl ends, the crouch begins, and item->pos does not move:
 *
 *   INTERPLOG root lara anim 274->222 frame 6655->5653
 *     offset_prev=(0,-204,-37) offset_cur=(0,-143,-37)
 *     drawn=(0,-197,-37) then (0,-188,..) (0,-176,..) (0,-164,..) pos_y=-1024
 *
 * Four drawn frames of the CROUCH pose - a pose built for a root 143 above the
 * floor - carried 54, 45, 33 and 21 units too high, and then settling into it.
 *
 * WHY THE LADDER IS RIGHT AND THIS IS WRONG is one sentence either way. The
 * climb carries its travel in the offsets and the engine moves item->pos by
 * the same amount at the wrap (TranslateItem, game\laramisc.cpp AnimateLara):
 * offset step +504, position step -512, and the two halves cancel - order 2
 * blends the one, this rule blends the other, and the drawn root is continuous.
 * Crawl to crouch has the offset step and NO position step. Nothing cancels, so
 * the blend simply drags the new pose away from the offset it was built for. At
 * thirty hertz vanilla shows pose A at A's offset and then pose B at B's; a
 * refused in-between frame draws pose B, and pose B belongs at B's offset
 * unless a position step says otherwise.
 *
 * SO ROUND 157'S SENTENCE WAS ONE CLAUSE SHORT: K6 refuses the POSE, never the
 * ROOT - AS FAR AS A POSITION STEP PAYS FOR IT. Per axis, with d = cur - prev
 * (prev already turned by InterpRootFlip) and `step` this tick's own position
 * step in the same frame, the paid part is min(|d|, |step|) where the two point
 * AGAINST each other and nothing at all where they do not, and the blend starts
 * from cur minus that part instead of from prev.
 *
 * MEASURED (RootProbe, against the user's own two cases). The ladder wrap
 * 161->161: d.y +504 against a step of -512, so the whole step is paid, the
 * start offset IS prev, InterpRootLerp sees today's operands and the drawn y is
 * today's TO THE UNIT. Crawl to crouch 274->222: step 0, nothing is paid, the
 * start offset is cur, the blend comes out at cur and InterpRootNow's own
 * "blended == cur" exit then hands the draw nothing - the picture of thirty
 * hertz, which is the one the pose was authored for. The rule can only ever
 * blend LESS than it does today, never more and never in another direction.
 *
 * AND OVER THE WHOLE OF LARA (RootProbe -sweep, ANDREA1, her 474 animations):
 * 433 transitions carry an offset step of 8 units or more. Every one of the 31
 * that the file pays for - a TranslateItem at a wrap - keeps its paid axis, and
 * in NOT ONE of the 433 does the narrowed draw leave the segment from the
 * frame's own offset to today's draw, further out or the other way. That last
 * figure is the invariant in the form that can be violated, and the sweep
 * counts it itself; the shape it reads more naturally in - "blends where
 * today's does not" - is dead there, because a row only enters the sweep when
 * an axis moves 8 units or more and today's half-blend then always lands off
 * cur, whatever this function does.
 * The other 402 are state changes and wraps without a command, where the file
 * knows of no step at all; what really moves her there is the state routine
 * (lara_col_reach, TestLaraVault, SnapLaraToEdgeOfBlock, LaraDoClimbLeftRight,
 * SetCornerAnim and their kin), and the rule measures that step because it
 * measures item->pos and not the animation table.
 *
 * THE FRAME THE TWO STEPS ARE COMPARED IN IS y_rot ALONE, and it is enough
 * because nothing that pays a step does so with a pitch. TranslateItem
 * (game\control.cpp) turns its three shorts by phd_sin/phd_cos of y_rot and
 * adds y straight, and so does every state routine that moves her by hand -
 * the vault, the ledge grab, the pushblock all write y_pos directly and their
 * x/z through phd_sin of y_rot. The y_rot frame IS the frame a position step is
 * authored in; running it through the full Y*X*Z would put the step into a
 * frame nothing that produces it ever used.
 *
 * BE HONEST ABOUT THE OTHER HALF (fix round of 157b, a reviewer's finding):
 * the quantity the step is weighed AGAINST, d, is applied by the draw through
 * the FULL phd_RotYXZ (game\delstuff.cpp turns by y_rot, x_rot, z_rot and then
 * translates by the offset), so where x_rot or z_rot is not zero the two do
 * live in different frames and this y_rot-only inverse IS an approximation
 * there. What that can cost is bounded by the shape of the rule itself, and
 * not by the accuracy of the inverse: the kept part is clamped to |d| on its
 * own axis whatever `step` says, so the start offset always lies on the
 * segment between cur and prev. A component the inverse understates keeps
 * less, one it overstates keeps at most the whole step - which is today's
 * picture. Never more than today, never in another direction: the one
 * guarantee the round rests on holds either way.
 * And it is not reached by anything measured: of the 31 transitions in ANDREA1
 * whose offset step the file pays for, not one is an underwater state (the
 * states seen are 0, 1, 2, 10, 19, 25, 36, 37, 54, 56, 57, 61, 87, 88, 89 and
 * 117 - no AS_TREAD 13, no AS_SWIM 17, no AS_GLIDE 18, the three that carry a
 * pitch; measured by RootProbe -sweep). She swims with an x_rot, but her step
 * (LaraUnderWater, game\laraswim.cpp) is authored in the y_rot frame too.
 *
 * THIS IS NOT LARA'S RULE, and the whole of the measurement above is hers
 * (second fix round of 157b, a reviewer's finding). InterpRootNow is asked
 * once per drawn item in DrawAnimatingItem (game\draw.cpp), so every animating
 * moveable in the level - every enemy, every door, every machine - is narrowed
 * by exactly this arithmetic, and the `-log` seam prints Lara's line alone, so
 * nobody would go looking for theirs. It is safe for them for the reason it is
 * safe for her and for no reason of hers in particular: the kept part is
 * clamped to |d| per axis, so the rule blends at most what it blends today and
 * never in another direction, whatever moved the item. What a sweep of Lara
 * cannot say is how OFTEN it changes their picture; the answer for them is the
 * same shape as for her - a wrap with a TranslateItem keeps its paid axis, and
 * an item whose creature routine walks it by hand is measured through
 * item->pos like she is.
 *
 * `step` IS NEGATED ONLY WHERE IT CANNOT HURT. The loop below writes -step[i],
 * and the ternary in front of it is what makes that safe: the branch is only
 * reached when step[i] and d have opposite signs and |step[i]| < |d|, and d is
 * a difference of two frame offsets, which are shorts - so LONG_MIN never
 * reaches the negation. (This paragraph said "NOTHING HERE NEGATES step" until
 * audit round A1, which is literally false of the code.) The magnitudes
 * themselves are compared by carrying the sign to the other side, so that part
 * of the arithmetic holds over the whole range of the type. */
inline void InterpRootNarrow(const long prev[3], const long cur[3], const long step[3],
	long out[3])
{
	for (int i = 0; i < 3; i++)
	{
		const long d = cur[i] - prev[i];

		if (d > 0 && step[i] < 0)
			out[i] = cur[i] - (step[i] <= -d ? d : -step[i]);
		else if (d < 0 && step[i] > 0)
			out[i] = cur[i] + (step[i] >= -d ? -d : step[i]);
		else
			out[i] = cur[i];
	}
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

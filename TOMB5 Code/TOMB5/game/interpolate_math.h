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
#include <math.h>	// sqrt - the camera aim rule

/* The arithmetic of the time interpolation, and nothing else.
 *
 * This header includes no engine header on purpose:
 * `Preparation\Tools\Diagnose\InterpTest.cpp` includes this file itself, not
 * a copy, and asserts on the functions below without a game, a level or a
 * device. game\interpolate.cpp holds the snapshot, the snap rules, the call
 * sites and the log. */

/* Angle blend the short way round. A TR angle is a signed short and a full
 * turn is its range, so the difference taken as a short is already the
 * shorter way: 32000 to -32000 is +1536 (as an int, -64000 would spin the
 * body backwards through zero). Exactly half a turn comes out as -32768,
 * anticlockwise; that case is a snap, not a rotation, and ApplyNode
 * (interpolate.cpp) does not blend it (InterpHalfTurn). */
inline short InterpLerpAngle(short a, short b, float alpha)
{
	const short d = (short)(b - a);

	return (short)(a + (short)((float)d * alpha));
}

/* A half turn in one tick is a snap.
 *
 * turn180_effect (game\effects.cpp) turns an item by exactly 32768 between two
 * ticks (`y_rot -= 0x8000`, x_rot negated). Seven of Lara's animations fire
 * it (AnimCmd over ANDREA1: 48, 147, 205, 207, 210, 212 and the level's own
 * 451 - the rolls, the tightrope turn and the jumps' twists). In 147, 205
 * and 451 the animation's root bone turns the other half, so the 30 Hz
 * picture is continuous; 48, 207, 210 and 212 fire on a last frame made for
 * the old yaw, which the draw turns back (INTERP_TURN_EARLY,
 * InterpKeyFrameTurn). Blended, the root
 * would take InterpLerpAngle's arbitrary half while the pose is already the
 * new one (refused by the neighbour rule or InterpPoseFar), and Lara would
 * stand sideways for one frame. So a node whose y difference is exactly half
 * a turn keeps its live rotation and only its position blends. TROne, read
 * for comparison, blends finished node matrices, where the two halves
 * cancel before the blend, and needs no such rule; nothing of its code is
 * used here. */
inline bool InterpHalfTurn(short a, short b)
{
	return (short)(b - a) == (short)-32768;
}

/* Clamps a screen fade value that overshot its end.
 *
 * SetFade (specific\specificfx.cpp) walks FadeVal from `start` to `end` in
 * eight steps of (end - start) >> 3; DoScreenFade reads the value before it
 * steps and puts `end` over it on the ninth call. For 255 -> 0 the step is
 * -32 and eight steps land on -1, which is alpha 255 in the colour: at 30 Hz
 * the ninth call overrides it, but a tween frame between the eighth and ninth
 * tick would draw a black frame in every level's fade-in. The ninth tick draws
 * `end` anyway, so no 30 Hz picture changes; a walk that does not overshoot
 * (0 -> 255 lands on 248) is untouched. */
inline long InterpFadeClamp(long val, long step, long end)
{
	if (step < 0 && val < end)
		return end;
	if (step > 0 && val > end)
		return end;
	return val;
}

/* Camera type changes that are hand-overs, not cuts.
 *
 * camera_type values from game\global\types.h (CHASE_CAMERA 0, FIXED_CAMERA
 * 1, LOOK_CAMERA 2, COMBAT_CAMERA 3, CINEMATIC_CAMERA 4, HEAVY_CAMERA 5);
 * interpolate.cpp static_asserts the three below against the enum, because
 * this file is built without the engine for InterpTest.
 *
 * Chase, look and combat hand the view over smoothly: the eye moves towards
 * the new place at camera.speed and the target stays Lara's (drawing a weapon
 * is chase -> combat, game\larafire.cpp). Treated as cuts, every such change
 * would draw one tick live and the next half a tick back, a visible jerk. A
 * type change is a cut only when it leaves that family; the eye and aim tests
 * still catch a real jump, and a change of camera.fixed_camera is a cut on
 * its own. */
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

/* Whether a level camera sequence holds the view: bUseSpotCam, or
 * bTrackCamInit, which game\spotcam.cpp's CalculateSpotCams sets on every tick
 * of a sequence whose first camera tracks Lara (SP_TRACKCAMERA). Under such a
 * sequence the upstream line at the top of ControlPhase (game\control.cpp)
 * clears bUseSpotCam on every call, the calls that run no tick included, and
 * the tick's flyby trigger sets it again; read alone it is 0 at the start of
 * every tick and between ticks and 1 after one, so every tick of the sequence
 * would read as a flyby beginning. The pair is 1 from the sequence's first
 * tick to the tick that runs CalculateCamera again, which clears
 * bTrackCamInit. Outside a tracking sequence bTrackCamInit is 0 and the
 * answer is bUseSpotCam's. */
inline bool InterpLevelCamHolds(long useSpotCam, long trackCamInit)
{
	return useSpotCam != 0 || trackCamInit != 0;
}

/* The camera cut test's flyby term: the view passed between a level camera
 * sequence and the game's own camera since the snapshot. `prevHolds` is
 * InterpLevelCamHolds at the start of the tick, `holds` its value now. */
inline bool InterpSpotCut(bool prevHolds, bool holds)
{
	return prevHolds != holds;
}

/* Whether the drawn-eye rule may move this frame's eye (game\interpolate.cpp,
 * ModernCameraDrawnEye): never while a level camera sequence holds the view at
 * either end of the blend, and never in a cutscene. */
inline bool InterpEyeRuleRuns(bool prevHolds, bool holds, bool cutseq)
{
	return !holds && !prevHolds && !cutseq;
}

/* Positions are integers in this engine and stay integers: the truncation is
 * at most one unit, a TR sector is 1024 of them, and handing a float position
 * to a matrix stack that works in longs would only move the rounding
 * somewhere less visible. */
inline long InterpLerpLong(long a, long b, float alpha)
{
	return a + (long)((float)(b - a) * alpha);
}

/* The distance the snap rules measure: the sum of the absolute components.
 * No square root, and never smaller than the true distance, so it can only
 * snap sooner, never later. */
inline long InterpManhattan(long dx, long dy, long dz)
{
	return labs(dx) + labs(dy) + labs(dz);
}

/* Snap thresholds.
 *
 * INTERP_ITEM_SNAP: nothing in TR5 crosses half a sector in one tick under
 * its own power (Lara's terminal fall is about 130 units a tick, a vehicle
 * under 100). What does is a teleport - TranslateItem (game\control.cpp), a
 * flipmap move, a randomizer spawn, an item taken and given back - and it is
 * drawn where it is, not as a streak across the room.
 *
 * INTERP_CAM_SNAP is looser because a camera legitimately travels faster than
 * what it films (MoveCamera, flyby splines); it catches cuts: a fixed camera
 * taking over, a spot cam jumping nodes, the binoculars going up. */
#define INTERP_ITEM_SNAP	512L
#define INTERP_CAM_SNAP		1024L

/* INTERP_CUTSEQ_SNAP is the cut rule of a cutscene camera, whose eye and
 * target come from the tracks of DATA\CUTSEQ.BIN rather than from a camera
 * routine: a cut in a track is a jump of the eye or the target between two
 * frames. Over the 44 cutscenes (CutseqProbe, 77,187 steps) the cuts jump 294
 * units and more, 371 of them in 33 cutscenes, while the fastest drive moves
 * 222 a frame and the eye's 99th percentile is 78; 35 of the cuts step no
 * more than 1024 on both the eye and the target, so a 1024 Manhattan rule on
 * the two would blend them as motion. */
#define INTERP_CUTSEQ_SNAP	256L

/* ==========================================================================
 * The blend factor when the display sets the pace (fps_mode 2).
 *
 * At fps_mode 1 frames land on the ticks and `framecount < 0 ? 0.5 : 1` is
 * exact (InterpAlphaNow). Bound to e.g. a 144 Hz display, frames land anywhere
 * inside a tick, so the factor comes from the clock:
 *
 *   framecount  ControlPhase's accumulator (game\control.h) in 1/60 s ticks,
 *               as the top of DrawPhaseGame finds it: -1 or 0.
 *   subTick     how far past the last tick boundary handed out by Sync() the
 *               clock stands (TIME_SubTick, specific\time.cpp). Not capped at
 *               1: Sync() runs at the end of a frame.
 *
 * With S the whole ticks handed out and k the logic steps run, framecount =
 * S - 2k. Step k runs at S = 2k-1 and its state stays newest until S = 2k+1;
 * reading it as the world at S = 2k+1 makes every frame in that window an
 * interpolation rather than an extrapolation:
 *
 *     alpha = (S + subTick - 2k + 1) / 2 = (framecount + subTick + 1) / 2
 *
 * It walks 0 -> 1 across every step at any refresh rate. At 60 Hz subTick is
 * 1, so (-1, 1) gives 0.5 and (0, 1) gives 1.0, the fps_mode 1 parity rule
 * exactly; InterpTest asserts it. The clamp covers hitches: a factor past 1
 * would ask for a state not yet computed, so the newest state is drawn and
 * ControlPhase catches up afterwards. */
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
 * The inventory ring's own cadence.
 *
 * The ring is a modal loop with its own present and never reaches
 * ControlPhase, so it keeps an accumulator of the same shape (1/60 s ticks,
 * two per logic step). The arithmetic lives here so InterpTest can run it;
 * game\inventory_interp.cpp holds the snapshot, the wiring and the log.
 * ======================================================================== */

/* Spends the ticks the last present returned and says whether this frame
 * carries the ring's logic step: ControlPhase's loop with the body run at
 * most once, because the ring draws as it steps and a second pass would draw
 * twice into one poly list. The credit is clamped to one step, because this
 * loop cannot pay off a hitch in one call; unclamped, it would step on every
 * frame until the debt was gone and the ring would run at the frame rate. */
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

/* The blend factor of a ring frame. At fps_mode 2 it is the clock through
 * InterpRefreshAlpha with the ring's accumulator (same phase: the last Sync()
 * was inside the previous present in both loops). At fps_mode 1 that formula
 * gives exactly 0.5 on the step frame (acc -1, subTick 1) and 1 on the frame
 * between (acc 0, subTick 1), so the parity is used directly; InterpTest
 * asserts the two agree.
 *
 * The half is on the step frame because in the ring the step comes before
 * the draw: the game's arrangement would draw C1, mid(C0,C1), C2, mid(C1,C2),
 * walking backwards every second frame. This yields the monotone mid(C0,C1),
 * C1, mid(C1,C2), C2. */
inline float InterpRingAlpha(long mode, long acc, double subTick, bool step)
{
	// fps_mode 0: every frame carries its step and is drawn live.
	// InvInterpBeginFrame returns before reaching this; the line keeps the
	// rule complete.
	if (mode == 0)
		return 1.0F;

	if (mode == 2)
		return InterpRefreshAlpha(acc, subTick);

	return step ? 0.5F : 1.0F;
}

/* The ring's slide offset. objlistmovement walks to 65536 in eight steps of
 * 8192 and is then reset to 0 with curobjinlist moved on by one; blended
 * across that reset the row would slide halfway back for one frame, so a
 * straddling pair draws the newer value. `older` and `newer` are the two
 * previous generations, not the live field, because vanilla steps this field
 * after drawing with it (game\inventory_interp.h). */
inline long InterpRingSlide(long older, long newer, bool jumped, float alpha)
{
	if (jumped)
		return newer;

	return InterpLerpLong(older, newer, alpha);
}

/* The slot the slide belongs to. curobjinlist is never blended, but it must
 * come from the same generation as the slide: the reset moves both at once,
 * and the new slot with the old offset would put the row seven eighths of a
 * slot to the side for one frame. `count` is the current numobjectsinlist; a
 * snapshot slot past the end of a rebuilt list falls back to the live one. */
inline long InterpRingSlot(long snapCur, long liveCur, long count)
{
	if (snapCur < 0 || snapCur >= count)
		return liveCur;

	return snapCur;
}

/* ==========================================================================
 * The pose.
 *
 * An animation stores every rate-th frame and the engine blends two stored
 * key frames (game\draw.cpp, GetFrames) with a factor of a quarter, a half or
 * three quarters chosen by three hard-wired branches:
 *
 *     pose(f) = lerp(K[k], K[k+1], alpha_vanilla(f mod rate, rate))
 *
 * The tween frame draws the midpoint of the two poses the game really draws
 * at 30 Hz, not a newly computed "correct" one. Vanilla's factor is coarse
 * (rate 5 draws frames 1..4 at 0.25, 0.75, 0.75, 0.75), but frac/rate would
 * change how a third of all TR5 animations move: over all sixteen level
 * files 3187 animations carry rate 5, 3350 rate 2, 5399 rate 4 and 35 rate 3,
 * 6, 9 or 10.
 *
 * Both tick poses lie on the same key frame pair, so the midpoint is one
 * factor on that pair:
 *
 *   same interval   pose(f-1) = lerp(K, K', ap), pose(f) = lerp(K, K', ac)
 *                   -> midpoint = lerp(K, K', (ap + ac) / 2)
 *   crossing one    f mod rate == 0, so pose(f) IS K' exactly
 *                   -> midpoint = lerp(K, K', (ap + 1) / 2)
 *
 * No third matrix stack is needed. Rate 1 animations (3619, including every
 * weapon-aim animation), never blended by vanilla, now blend their two
 * adjacent stored frames at a half. */

/* Vanilla's key frame factor, exactly as InterpolateMatrix,
 * aInterpolateMatrix and the two arm matrix functions (draw.cpp) classify
 * (frac, rate). frac == 0: the item sits on a stored frame, nothing blends. */
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

/* The shortened rate of an animation's last interval, transcribed from
 * GetFrames:
 *
 *     num = rate * (key + 1);
 *     if (num > anim->frame_end) rate += anim->frame_end - num;
 *
 * It compares a frame counted from the animation's start against a frame_end
 * counted from the level's, so it only fires where frame_base is small. That
 * mismatch is vanilla's and decides vanilla's factor, so it is reproduced,
 * not repaired. */
inline long InterpKeyRate(long rate, long key, long frameEnd)
{
	const long num = rate * (key + 1);

	return num > frameEnd ? rate + frameEnd - num : rate;
}

/* The midpoint of the two tick poses, as a key frame index and a factor.
 *
 * `t` is the drawn frame's time alpha. Returns false (the neighbour rule of
 * interpolate.h) when:
 *
 *   the frame did not step by exactly one   a jump, a loop wrapping to its
 *                                           start, an item standing still, a
 *                                           frame set by hand: the current
 *                                           pose is drawn; the root offset is
 *                                           still blended (InterpRootKey).
 *   the animation has no rate               nothing to walk.
 *   the current frame is the animation's    the previous pose belongs to
 *   first                                   another animation.
 *
 * A change of animation number also breaks the neighbour rule and is tested
 * by the caller
 * (draw.cpp, GetFramesAt). */
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

/* The pose between two ticks of an animation played backwards, as a key
 * frame index and a factor.
 *
 * The ledge hang's lowering (game\lara.cpp, lara_as_ledgehang) plays the
 * pull-up - animation 97, 74 frames at rate 4 - three frames down a tick, the
 * last step cut short to land on the first frame. InterpPoseAt refuses every
 * one of those frames; GetFramesAt (draw.cpp) then asks this rule, for Lara
 * and only while LaraLedgeReversing. Returns false, and the current pose is
 * drawn, when:
 *
 *   the frame did not step down         a forward or standing frame is
 *                                       InterpPoseAt's ground.
 *   the animation has no rate           nothing to walk.
 *   a frame lies outside the animation  the current one below frame_base or
 *                                       the previous one beyond frame_end:
 *                                       the previous pose belongs to another
 *                                       animation.
 *   the frames lie more than one key    kp > kc + 1: the path between them
 *   interval apart                      runs over a key frame neither tick
 *                                       drew from.
 *
 * Both tick poses are placed in key units exactly as vanilla draws them,
 * InterpKeyAlpha over InterpKeyRate's rate:
 *
 *     pp = kp + alpha(p mod rate),   pc = kc + alpha(f mod rate)
 *
 * and the drawn frame stands at m = pp + (pc - pp) * t on the animation's own
 * path, walking down from pp to pc: the previous tick's pose as t nears 0,
 * the current tick's as t nears 1, exactly their midpoint at t = 0.5, with no
 * cut at either end. It is the forward rule's idea - one factor on the key
 * interval the pose lies in - with the interval chosen by m: the pair is
 * kc + 1 when m reaches it, else kc, and the factor is what is left over.
 *
 * The pair can be the upper one because a step of three frames at rate four
 * passes over a key on two ticks of every four, and the midpoint can still
 * lie above that key: 70 down to 67 walks from 17.5 to 16.75 and the midpoint,
 * 17.125, is the pair (17, 18) at 0.125. The forward rule's clamp form - the
 * pair kc, and the previous pose taken as 1.0 on it - is exact only for a
 * step of one, where a pose that crossed a key sits on the key itself. Here
 * it would start the walk at kc + 1 instead of pp and draw the tween up to a
 * quarter of an interval too far along (16.875 on that tick, 17.75 instead
 * of 17.875 on the one before), a stutter on two ticks of every four.
 *
 * The pair index never exceeds kp. For t > 0, all GetFramesNow asks for, it
 * is kp only when the previous frame lies past that key, and then the
 * previous tick drew from kp and kp + 1 itself; otherwise the pair is kc and
 * kc + 1, which the current tick drew from or which is kp. Both key frames of
 * the pair are always frames one of the two ticks read. */
inline bool InterpPoseBack(long prevFrame, long curFrame, long frameBase, long rate,
	long frameEnd, float t, long* key, float* blend)
{
	if (rate < 1)
		return false;

	if (prevFrame - curFrame < 1)
		return false;

	const long f = curFrame - frameBase;
	const long p = prevFrame - frameBase;

	if (f < 0 || p > frameEnd - frameBase)
		return false;

	const long kc = f / rate;
	const long kp = p / rate;

	if (kp > kc + 1)
		return false;

	const float pc = (float)kc + InterpKeyAlpha(f % rate, InterpKeyRate(rate, kc, frameEnd));
	const float pp = (float)kp + InterpKeyAlpha(p % rate, InterpKeyRate(rate, kp, frameEnd));
	const float m = pp + (pc - pp) * t;

	*key = (m >= (float)(kc + 1)) ? kc + 1 : kc;
	*blend = m - (float)*key;

	return true;
}

/* ---------------------------------------------------------- the blend itself
 *
 * One factor in place of vanilla's hard-wired branches, bit-exact with them
 * so the 30 Hz picture is unchanged. The float form keeps vanilla's three
 * expressions (`(a + b) * 0.5`, `a + (b - a) * 0.25`, `b - (b - a) * 0.25`),
 * which round differently in the last bit than one rearranged formula; like
 * the integer form it works from whichever end is nearer. */
inline float InterpBlendFloat(float a, float b, float alpha)
{
	if (alpha == 0.5F)
		return (a + b) * 0.5F;

	if (alpha > 0.5F)
		return b - (b - a) * (1.0F - alpha);

	return a + (b - a) * alpha;
}

/* The integer stack rounds by shifting, a floor rather than a truncation
 * (`(-3) >> 2` is -1, `(long)(-3 * 0.25f)` is 0). Vanilla's branches `a + (d
 * >> 1)`, `a + (d >> 2)` and `b - (d >> 2)` are, in eighths,
 *
 *     alpha8 <= 4 :  a + ((d * alpha8) >> 3)
 *     alpha8 >  4 :  b - ((d * (8 - alpha8)) >> 3)
 *
 * since `(d * 4) >> 3` is `d >> 1` and `(d * 2) >> 3` is `d >> 2` for every
 * value including negatives: exact, with no float. Vanilla's factors are
 * quarters and their midpoints eighths. A variable time alpha (fps_mode 2)
 * is rounded here to the nearest eighth of a key frame interval, about
 * 1/240 s of animation. */
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
 * The root offset under a refused pose.
 * ==========================================================================
 *
 * Every stored frame carries a bounding box and then the root offset
 * frm[6..8], applied after the item's rotation (draw.cpp DrawAnimatingItem,
 * delstuff.cpp for Lara). A climb carries its vertical travel in those
 * offsets and TranslateItem (laramisc.cpp, AnimateLara) moves item->pos by
 * the same amount at the wrap, so at 30 Hz the drawn root is continuous. On
 * the tween frame item->pos is blended half way (at most 512 units, not a
 * snap) while the neighbour rule refuses the pose and the draw uses the
 * current frame's offset: drawn = Y + D/2 where both neighbours drew Y + D. On a 165 Hz
 * display that tween state is shown four or five times in a row.
 *
 * So on a refused tween frame the offset is blended in time as well, and the
 * two halves add up to the whole step (narrowed by InterpRootNarrow). Where
 * the two ticks' offsets are equal the blend equals the offset and
 * InterpRootNow hands the draw nothing, so vanilla's line runs.
 *
 * The current tick's offset is vanilla's pair: frm[0] alone on a stored
 * frame, else the key pair at `keyAlpha` (already chosen by GetFramesNow). It
 * is computed in integers, within one unit of the draw's float path; that
 * unit reaches only the frames InterpRootNow actually changes. frm1 is not
 * read at frac zero: `frm[0] + size` may then lie past the level's frame
 * block, and vanilla never reads it either. */
inline void InterpRootKey(const short* frm0, const short* frm1, long frac, float keyAlpha,
	long out[3])
{
	const long alpha8 = InterpEighths(keyAlpha);

	for (int i = 0; i < 3; i++)
		out[i] = frac
			? InterpBlendLong(frm0[6 + i], frm1[6 + i], alpha8)
			: (long)frm0[6 + i];
}

/* The two ticks' offsets blended at the frame's time `t` itself, through
 * InterpLerpLong and not rounded to an eighth. The drawn root is pos plus the
 * offset, and ApplyNode (interpolate.cpp) blends pos with InterpLerpLong at
 * this alpha; only the same primitive at the same alpha makes the two halves
 * sum to the lerp of the drawn roots. Sweeping alpha in 1/256 steps over the
 * wrap of animation 161, the worst error is 504 units without this blend, 31
 * with eighths and 1 (two truncations) at the alpha itself. At fps_mode 1
 * (alpha 0.5) all agree; the exactness matters for fps_mode 2. The key pair
 * blend above keeps eighths without loss: its factors are quarters. */
inline void InterpRootLerp(const long prev[3], const long cur[3], float t, long out[3])
{
	for (int i = 0; i < 3; i++)
		out[i] = InterpLerpLong(prev[i], cur[i], t);
}

/* Turns the previous tick's root offset through a half turn.
 *
 * The offsets InterpRootLerp blends are in the item's own frame; on a tick
 * where turn180_effect fired, the two frames are half a turn apart and the
 * draw uses the new one (ApplyNode keeps the live rotation), so a straight
 * blend drags the root through the pivot. Underwater roll (RootProbe over
 * ANDREA1): animation 203's last frame 5330 carries (42,122,68), 205's frame
 * 5423 where FlipEffect 0 fires carries (-45,122,-67), and the drawn offset
 * walked (32,122,53) ... (-15,122,-20): up to 140 units off.
 *
 * turn180_effect is `y_rot -= 0x8000` and `x_rot = -x_rot`, so the offset is
 * carried by Rcur^-1 * Rprev in the engine's Y*X*Z order:
 *
 *     Rz(-z) * Rx(x) * Ry(180) * Rx(x) * Rz(z)
 *
 * Rx(a) * Ry(180) * Rx(a) is diag(-1, 1, -1) for every a, and with the lean z
 * equal at both ticks the Rz cancel: negate x and z, keep y. Animation 205's
 * key 0 (42,122,68) against key 1 (-45,122,-67) confirms it. A lean z would
 * add a turn of 2z in x/y that the animation itself carries at 30 Hz too, so
 * it is not corrected.
 *
 * TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931 -
 * was read for comparison, and the approach follows it: TROne blends whole
 * node matrices with the previous frame expressed in the current one. This
 * engine blends Euler angles and a separate offset, so that step is done
 * by hand; for half a turn it is two signs. No TROne source is in this
 * file. */
inline void InterpRootFlip(const long in[3], long out[3])
{
	out[0] = -in[0];
	out[1] = in[1];
	out[2] = -in[2];
}

/* Narrows the root blend to what a position step pays for.
 *
 * At a ladder wrap the offset step (+504) and the position step (-512) cancel:
 * ApplyNode blends one half, the root blend the other, and the drawn root is
 * continuous. Crawl to crouch (274->222) has an offset step, (0,-204,-37) to
 * (0,-143,-37), and no position step, so a full blend would draw the crouch
 * pose 54, 45, 33 and 21 units too high over four frames at 165 Hz. A refused
 * tween frame draws the new pose, which belongs at its own offset unless a
 * position step says otherwise.
 *
 * Per axis, with d = cur - prev (prev already turned by InterpRootFlip) and
 * `step` this tick's position step in the same frame, the paid part is
 * min(|d|, |step|) where the two point against each other and zero otherwise;
 * the blend starts from cur minus that part. The ladder wrap keeps its whole
 * step (drawn y unchanged); crawl to crouch keeps none and draws cur. The rule
 * can only blend less, never more or in another direction. RootProbe -sweep
 * over Lara's 474 animations in ANDREA1: 433 transitions carry an offset step
 * of 8 units or more; the 31 paid by a TranslateItem at a wrap keep their paid
 * axis, and in none of the 433 does the narrowed draw leave the segment from
 * the frame's own offset to the full blend. The other 402 are state changes
 * whose real motion comes from the state routine (lara_col_reach,
 * TestLaraVault, SnapLaraToEdgeOfBlock, LaraDoClimbLeftRight, SetCornerAnim),
 * which this sees because it measures item->pos.
 *
 * The steps are compared in the y_rot frame alone: TranslateItem
 * (control.cpp) and the state routines that move her by hand turn x/z by
 * y_rot and add y straight. The draw applies d through the full phd_RotYXZ,
 * so with x_rot or z_rot non-zero this is an approximation, bounded by the
 * clamp to |d|: the start offset always lies between cur and prev. None of
 * the 31 paid transitions is a pitched state (states 0, 1, 2, 10, 19, 25, 36,
 * 37, 54, 56, 57, 61, 87, 88, 89, 117; no AS_TREAD 13, AS_SWIM 17 or AS_GLIDE
 * 18), and LaraUnderWater (laraswim.cpp) also steps in the y_rot frame.
 *
 * InterpRootNow runs for every drawn item in DrawAnimatingItem (draw.cpp),
 * enemies, doors and machines included; the same clamp keeps it safe for
 * them, though the -log line prints Lara only.
 *
 * `step[i]` is negated only on the branch where it and d have opposite signs
 * and |step[i]| < |d|; d is a difference of two shorts, so LONG_MIN never
 * reaches the negation. The magnitudes are compared by carrying the sign to
 * the other side, which holds over the whole range of the type. */
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
 * The pair a matrix blend cannot survive.
 * ==========================================================================
 *
 * The pose blend is vanilla's InterpolateMatrix (draw.cpp): an element-wise
 * lerp of two 3x4 matrices, which always takes the short way, so no angle
 * wraps. It shrinks instead: for node frames A and B a relative rotation of D
 * apart,
 *
 *     (A + B) / 2  =  A * (I + R(D)) / 2
 *
 * is the identity on the axis and cos(D/2) times a rotation of D/2 across it:
 * 13 % shrink at 60 degrees, 29 % at 90, and at 180 the frame is singular and
 * whatever hangs off the bone lands on neither pose (a leg pointing the wrong
 * way).
 *
 * Vanilla blends the key frames of animations of rate > 1 this way, and the
 * tween frame of such an animation lies in the same segment as its
 * neighbours. Rate 1 pairs are new: two adjacent stored frames at a half.
 * ModelProbe -poserisk over ANDREA1 (319 Lara animations, 23 at rate 1): 22
 * turn at most 52.7 degrees at any node; the hips (node 0) of animation 205
 * turn 179.9 degrees between key 0 and key 1.
 *
 * A rate 1 pair turning more than 90 degrees at any node (the last turn whose
 * midpoint keeps over seventy per cent of the frame) is not blended; in
 * ANDREA1 that is animation 205 alone. Vanilla never draws such a pair
 * blended either, and nothing of this rule runs at fps_mode 0.
 *
 * Vanilla's own key frame blend meets the same arithmetic at rate > 1 where
 * a node of Lara's turns more than 90 degrees in the drawn frame between two
 * key frames, and there it runs at every fps_mode: InterpKeyFarDrawn below. */
#define INTERP_POSE_TURN_COS	0.0F	/* cos 90 degrees: trace = 1 + 2*cos */

/* One node's packed rotation, with the cursor stepped past it (draw.cpp,
 * gar_RotYXZsuperpack): two words holding three 10-bit angles when the top
 * two bits are zero, otherwise one word naming a single axis with 12 bits.
 * The angles come back in the engine's 16-bit space. */
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

/* The node's rotation from its three angles, row major 3x3, in phd_RotYXZ's
 * order (Y, then X, then Z) so it matches the frames the engine builds. */
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

/* True when the two frames are too far apart at any node for an element-wise
 * matrix blend; shared by draw.cpp and InterpTest. `nodes` is the model's
 * mesh count; the nine shorts of bounding box and offset are skipped here. */
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

/* True when the hips (node 0) of two key frames of Lara's turn more than 90
 * degrees between them: the test for where turn180_effect's half turn lies
 * (InterpTurnKind) and for the way round the bridge takes the hips
 * (InterpBridgeLong), both below.
 *
 * The tightrope turn (animation 451) swings the hips back by 176.9 degrees
 * between key 25 and key 26 in ANDREA1 and rich2 (rate 2) and by 171.3
 * degrees between key 12 and key 13 in ANDREA2 and ANDREA3 (rate 4), while
 * turn180_effect turns the item by half a turn on the second key's frame: the
 * animation carries the other half turn itself. Over the thirteen campaign
 * levels' playable Lara animations the hips turn this far across 32 blended
 * key pairs - animations 209 and 213 key 0 to 1 in every level, 451 in
 * ANDREA1, ANDREA2, ANDREA3 and rich2, 219 key 0 to 1 and 152 key 3 to 4 in
 * rich3 - with the nearest turns on either side of the limit at 85.97 and
 * 94.92 degrees. The same threshold and arithmetic as the rate 1 rule, over
 * one node.
 *
 * Which key pairs are drawn from a built key frame is decided over every
 * node, in the drawn frame (InterpKeyFarDrawn below): the hips' own turn is
 * node 0's drawn turn, so every pair this fires on is among them. */
inline bool InterpKeyFar(const short* frame0, const short* frame1)
{
	return InterpPoseFar(frame0, frame1, 1, INTERP_POSE_TURN_COS);
}

/* ==========================================================================
 * The drawn turn of a key pair.
 * ==========================================================================
 *
 * Vanilla's key frame blend (delstuff.cpp Rich_CalcLaraMatrices_Interpolated
 * for Lara, draw.cpp DrawAnimatingItem for the rest, both through
 * InitInterpolateAlpha and InterpolateMatrix) keeps two matrix stacks, one per
 * key frame, multiplies each node's rotation onto both and averages the two
 * stacks element by element at every node. What is averaged is a node's
 * drawn rotation - its own rotation multiplied onto its parents' - so a node
 * whose drawn rotation turns D between the two keys is drawn cos(D/2) of
 * itself across the turn at the midpoint, whatever its own rotation does, and
 * its skin joins that frame to its parent's. In ANDREA1 the standing jump
 * forward (animation 76, key 0 to 1) turns the upper arm (node 11) 143.7
 * degrees on its own and the forearm (node 12, 0.4 degrees on its own)
 * 170.1 in the drawn frame: at the one in-between frame the forearm is 0.09
 * of itself across and the arm is drawn flat. A node may turn far on its own
 * and little in the drawn frame, where its parent's turn takes part of it
 * back (53 key 0 to 1: node 9 147.6 on its own, no node over 83.6 drawn, 0.75
 * kept), and little on its own and far in the drawn frame, where its
 * parents' turns add up (10 key 0 to 1: no node over 56.7 on its own, node
 * 13 94.2 drawn).
 *
 * So a key pair of Lara's is far where the drawn rotation of any node turns
 * more than 90 degrees between its keys (INTERP_KEY_DRAWN_COS), and it is
 * drawn from a key frame built at the weight in hand (InterpKeyFrameFar),
 * or held on its first key where turn180_effect fires on its second
 * (InterpKeyHold), unless it is a seam in the data (InterpKeySeamAt below),
 * which vanilla's blend draws. Over the thirteen campaign levels' playable
 * Lara animations 1204 of the 35364 key pairs vanilla blends are far, 72 to
 * 134 per level, 14 of them seams (the start of the handstand from a ledge in
 * every level, and rich3's 152 key 4 to 5). Far and no seam are the
 * take-offs of the standing jumps (76 forward 170.1 degrees at node 12, 91 up
 * 133.8 at node 11, 74 back 153.8, 78 left 147.9, 80 right 131.7), the first
 * pair of the back jump's twist (212, 137.0 at node 10), and the 32 pairs
 * whose hips turn that far (InterpKeyFar). The own turn
 * would mark 617: it misses 719 whose drawn turn passes 90 and takes 132
 * whose drawn turn stays under it. The nearest drawn turns on either side of
 * the limit are 89.93 and 90.04 degrees, and the engine's float walk and a
 * double-precision one agree on every pair of the thirteen levels.
 *
 * The answer is kept per animation, one bit per key pair
 * (game\interpolate.cpp InterpKeyFarOf, filled by InterpKeyFarFill on the
 * animation's first draw), so a draw pays a table lookup. The longest
 * playable animation of Lara's has 199 key pairs (a rate 1 animation, 471 in
 * ANDREA3; 100 at rates above 1), inside the table's INTERP_KEY_FAR_PAIRS. */
#define INTERP_KEY_DRAWN_COS	0.0F	/* cos 90 degrees: the midpoint keeps cos 45 = 0.71 */
#define INTERP_KEY_FAR_WORDS	8	/* 32-bit words of key pairs per animation */
#define INTERP_KEY_FAR_PAIRS	(32 * INTERP_KEY_FAR_WORDS)

/* The 3x3 row major product a * b, written to `out`. */
inline void InterpRotMul(const float* a, const float* b, float* out)
{
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}

/* The drawn rotation of every node of frame `f`, nine floats per node in
 * `out`, in InterpPoseRot's layout: the node's own rotation multiplied onto
 * its parent's drawn rotation. The parent is found as the draw finds it
 * (DrawAnimatingItem, and the fixed walk of Rich_CalcLaraMatrices for Lara's
 * fifteen nodes): `bone` is the object's bone table, four longs per node from
 * node 1 on, whose first long pops the matrix stack before the node (bit 0)
 * and pushes it (bit 1); the offsets turn nothing and are not read. The
 * draw's stack multiplies each rotation on the right, and InterpPoseRot's
 * layout is its transpose, so here a node's own rotation stands on the left.
 * Node 0's drawn rotation is its own, bit for bit. Only the key frames are
 * read: the torso's look and aim rotation (lara.torso_*_rot), which the draw
 * puts on the stack after node 7, is not part of it. At most 32 nodes. */
inline void InterpDrawnRots(const short* f, int nodes, const long* bone, float* out)
{
	float stack[32][9];
	float cur[9] = { 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F };
	int depth = 0;
	const short* p = f + 9;

	for (int node = 0; node < nodes && node < 32; node++)
	{
		if (node > 0)
		{
			const long flags = bone[(node - 1) * 4];

			if ((flags & 1) && depth > 0)
			{
				depth--;

				for (int i = 0; i < 9; i++)
					cur[i] = stack[depth][i];
			}

			if ((flags & 2) && depth < 32)
			{
				for (int i = 0; i < 9; i++)
					stack[depth][i] = cur[i];

				depth++;
			}
		}

		short r[3];
		float m[9];

		InterpUnpackRot(p, r);
		InterpPoseRot(r, m);
		InterpRotMul(m, cur, out + node * 9);

		for (int i = 0; i < 9; i++)
			cur[i] = out[node * 9 + i];
	}
}

/* The cosine of the largest turn between two frames' drawn rotations `a`
 * and `b` (InterpDrawnRots) over `nodes` nodes; `node`, where not null,
 * receives the node that turns furthest (the first of equals). */
inline float InterpDrawnTurnCos(const float* a, const float* b, int nodes, int* node)
{
	float worst = 2.0F;
	int at = 0;

	for (int n = 0; n < nodes && n < 32; n++)
	{
		const float c = InterpPoseTurnCos(a + n * 9, b + n * 9);

		if (c < worst)
		{
			worst = c;
			at = n;
		}
	}

	if (node)
		*node = at;

	return worst < 2.0F ? worst : 1.0F;
}

/* True when the key frames `f0` and `f1` lie too far apart for vanilla's key
 * frame blend: the drawn rotation of some node turns more than 90 degrees
 * between them (INTERP_KEY_DRAWN_COS). `node` and `turnCos`, where not null,
 * receive the node that turns furthest and the cosine of its turn. */
inline bool InterpKeyFarDrawn(const short* f0, const short* f1, int nodes, const long* bone,
	int* node, float* turnCos)
{
	float a[32 * 9], b[32 * 9];

	InterpDrawnRots(f0, nodes, bone, a);
	InterpDrawnRots(f1, nodes, bone, b);

	const float c = InterpDrawnTurnCos(a, b, nodes, node);

	if (turnCos)
		*turnCos = c;

	return c < INTERP_KEY_DRAWN_COS;
}

/* ==========================================================================
 * A seam in the data.
 * ==========================================================================
 *
 * A far pair is drawn from a key frame built at the weight in hand, every
 * node turned along its own turn, so every in-between frame is a rotation of
 * each part. That presumes the data carries a motion between the two keys.
 * Where a limb turns within ten degrees of half a turn both on its own and
 * in the drawn frame, the pair does not say which way round it goes: its own
 * two ways round differ by less than twenty degrees, its drawn two ways as
 * well, and the two need not agree, so any path through the in-between
 * frames swings the limb half a turn through a side the keys never show.
 * Such a pair is a seam in the data, and it is left to vanilla's blend,
 * which chooses no side: it averages the two keys' matrices, position
 * included, so each joint walks the straight line between its two key
 * places and the limb shortens across the turn instead of swinging.
 *
 * Over the thirteen campaign levels' playable Lara animations one pair is
 * such a seam, in every level: the start of the handstand from a ledge (159,
 * key 1 to 2, rate 4, state AS_GYMNAST). Her hands lie on the ledge in both
 * keys (the wrists 6.5 and 7.3 units apart between them) while her shoulders
 * rise 189 units past them and the upper arms flip from above and in front
 * of the shoulders to behind them: the right upper arm (node 8) turns 173.8
 * degrees on its own and 177.9 in the drawn frame. The built frame swings
 * both arms out to the sides and carries the wrists up to 80.7 and 73.2
 * units off the line between their two key places, into the wall; the other
 * way round of node 8's own turn carries the right wrist behind her back,
 * and a turn along the drawn rotations lifts it above the ledge. Vanilla's
 * blend keeps both wrists on the ledge and draws the arms shortened on the
 * three in-between frames. Besides it only rich3's 152 key 4 to 5 is one,
 * whose frame data run past their frame size. Every other far pair whose
 * hips turn less than 90 degrees stays at or below 143.7 at every limb by
 * the smaller of the two turns, the standing jump forward's upper arm (76
 * key 0 to 1: 143.7 on its own, 170.0 drawn); the upper arm of the standing
 * jump up turns 179.9 on its own but 133.8 drawn (91 key 0 to 1,
 * InterpKeyFrameFar).
 *
 * A pair whose hips turn more than 90 degrees (InterpKeyFar) keeps the hips'
 * rules - the hold, the early turn, the bridge - and is never a seam: node 0's
 * own turn is its drawn turn, and the limbs of those 32 pairs stay at or
 * below 159.1 degrees (rich3's 152 key 3 to 4, node 1). */
#define INTERP_KEY_SEAM_COS	-0.98481F	/* cos 170 degrees */

/* The node that makes the key pair `f0` to `f1` a seam, or -1: the first
 * node from 1 on whose own rotation and whose drawn rotation both turn more
 * than 170 degrees between the two keys (INTERP_KEY_SEAM_COS), in a pair
 * whose hips do not turn more than 90 (InterpKeyFar). `a` and `b` are the
 * two keys' drawn rotations (InterpDrawnRots) over `nodes` nodes. `ownCos`
 * and `drawnCos`, where not null, receive that node's two cosines. */
inline int InterpKeySeamAt(const short* f0, const short* f1, const float* a, const float* b,
	int nodes, float* ownCos, float* drawnCos)
{
	if (InterpKeyFar(f0, f1))
		return -1;

	const short* p = f0 + 9;
	const short* q = f1 + 9;

	for (int node = 0; node < nodes && node < 32; node++)
	{
		short ra[3], rb[3];

		InterpUnpackRot(p, ra);
		InterpUnpackRot(q, rb);

		if (node == 0)
			continue;

		const float d = InterpPoseTurnCos(a + node * 9, b + node * 9);

		if (d >= INTERP_KEY_SEAM_COS)
			continue;

		float ma[9], mb[9];

		InterpPoseRot(ra, ma);
		InterpPoseRot(rb, mb);

		const float o = InterpPoseTurnCos(ma, mb);

		if (o < INTERP_KEY_SEAM_COS)
		{
			if (ownCos)
				*ownCos = o;

			if (drawnCos)
				*drawnCos = d;

			return node;
		}
	}

	return -1;
}

/* InterpKeySeamAt for the key frames `f0` and `f1` alone, their drawn
 * rotations built here from Lara's bone table `bone` (InterpDrawnRots). */
inline int InterpKeySeam(const short* f0, const short* f1, int nodes, const long* bone,
	float* ownCos, float* drawnCos)
{
	float a[32 * 9], b[32 * 9];

	InterpDrawnRots(f0, nodes, bone, a);
	InterpDrawnRots(f1, nodes, bone, b);

	return InterpKeySeamAt(f0, f1, a, b, nodes, ownCos, drawnCos);
}

/* One animation's far key pairs (InterpKeyFarDrawn) that are no seam
 * (InterpKeySeamAt), one bit per pair in `bits` (INTERP_KEY_FAR_WORDS words,
 * cleared here): `frames` is the animation's block of key frames, `size` its
 * frame size in shorts and `pairs` its key pairs, so keys 0 to `pairs` are
 * read. Each key frame's drawn rotations are built once and serve both pairs
 * it belongs to. A seam, and every pair from INTERP_KEY_FAR_PAIRS on, is
 * left unmarked and drawn as vanilla draws it. */
inline void InterpKeyFarFill(const short* frames, long size, long pairs, int nodes,
	const long* bone, unsigned long* bits)
{
	float rots[2][32 * 9];

	for (int w = 0; w < INTERP_KEY_FAR_WORDS; w++)
		bits[w] = 0;

	if (!frames || !bone || size < 9 || nodes < 1 || pairs < 1)
		return;

	if (pairs > INTERP_KEY_FAR_PAIRS)
		pairs = INTERP_KEY_FAR_PAIRS;

	InterpDrawnRots(frames, nodes, bone, rots[0]);

	for (long k = 0; k < pairs; k++)
	{
		const float* a = rots[k & 1];
		float* b = rots[(k + 1) & 1];

		InterpDrawnRots(frames + size * (k + 1), nodes, bone, b);

		if (InterpDrawnTurnCos(a, b, nodes, 0) < INTERP_KEY_DRAWN_COS &&
			InterpKeySeamAt(frames + size * k, frames + size * (k + 1), a, b, nodes, 0, 0) < 0)
			bits[k >> 5] |= 1UL << (k & 31);
	}
}

/* Whether pair `pair` is marked in `bits` (InterpKeyFarFill); false for a
 * pair outside the table. */
inline bool InterpKeyFarBit(const unsigned long* bits, long pair)
{
	if (!bits || pair < 0 || pair >= INTERP_KEY_FAR_PAIRS)
		return false;

	return ((bits[pair >> 5] >> (pair & 31)) & 1UL) != 0;
}

/* ==========================================================================
 * Key frames built rather than blended.
 * ==========================================================================
 *
 * A far key pair (InterpKeyFarDrawn) that is no seam (InterpKeySeamAt) and
 * is not held on its first key is drawn from a key frame made for the weight
 * in hand, and the frame an early turn180_effect fires on (INTERP_TURN_EARLY)
 * from copies turned by half a turn (draw.cpp LaraPoseFrames). Both are
 * frames in the level's own format, so every draw path reads them as it
 * reads a stored frame.
 *
 * A node's packed rotation (InterpUnpackRot above; draw.cpp
 * gar_RotYXZsuperpack and specific\3dmath.cpp phd_RotYXZpack read it) comes
 * in two forms:
 *
 *   three axes   two words, the top two bits of the first zero; the 32-bit
 *                pair holds x in bits 20-29, y in bits 10-19 and z in bits
 *                0-9, each in 1024ths of a turn (a step of 64 in the
 *                engine's 16-bit angle);
 *   one axis     one word whose top two bits name the axis (1 x, 2 y, 3 z)
 *                and whose low twelve bits hold the angle in 4096ths of a
 *                turn (a step of 16). */
#define INTERP_KEYFRAME_MAX	(9 + 2 * 32)	/* box, offset and 32 nodes of two words */

/* The form of the packed rotation at `p`: -1 for three axes, else the one
 * axis (0 x, 1 y, 2 z). */
inline int InterpRotAxis(const short* p)
{
	const unsigned short w = (unsigned short)*p;

	if ((w & 0xC000) == 0)
		return -1;

	return (int)(w >> 14) - 1;
}

/* Packs three angles in the form `axis` names (InterpRotAxis) and returns
 * the words written. Each angle is rounded to the nearest step of its form;
 * the one-axis form writes xyz[axis] and assumes the other two are zero. A
 * rotation read by InterpUnpackRot and packed in the form it was read in
 * comes back word for word. */
inline int InterpPackRot(const short* xyz, int axis, short* out)
{
	if (axis >= 0)
	{
		const unsigned long a = (((unsigned long)(unsigned short)xyz[axis] + 8) >> 4) & 0xFFF;

		out[0] = (short)(unsigned short)(((unsigned long)(axis + 1) << 14) | a);
		return 1;
	}

	const unsigned long x = (((unsigned long)(unsigned short)xyz[0] + 32) >> 6) & 0x3FF;
	const unsigned long y = (((unsigned long)(unsigned short)xyz[1] + 32) >> 6) & 0x3FF;
	const unsigned long z = (((unsigned long)(unsigned short)xyz[2] + 32) >> 6) & 0x3FF;
	const unsigned long packed = (x << 20) | (y << 10) | z;

	out[0] = (short)(unsigned short)(packed >> 16);
	out[1] = (short)(unsigned short)(packed & 0xFFFF);
	return 2;
}

/* A node's rotation matrix (InterpPoseRot's layout) as a unit quaternion
 * (w, x, y, z), and back. The two are exact inverses of each other, so
 * whatever convention the layout stands for, a blend between two
 * quaternions comes back as the blend between the two matrices. */
inline void InterpRotToQuat(const float* m, float* q)
{
	const float t = m[0] + m[4] + m[8];

	if (t > 0.0F)
	{
		const float s = sqrtf(t + 1.0F) * 2.0F;

		q[0] = 0.25F * s;
		q[1] = (m[7] - m[5]) / s;
		q[2] = (m[2] - m[6]) / s;
		q[3] = (m[3] - m[1]) / s;
	}
	else if (m[0] > m[4] && m[0] > m[8])
	{
		const float s = sqrtf(1.0F + m[0] - m[4] - m[8]) * 2.0F;

		q[0] = (m[7] - m[5]) / s;
		q[1] = 0.25F * s;
		q[2] = (m[1] + m[3]) / s;
		q[3] = (m[2] + m[6]) / s;
	}
	else if (m[4] > m[8])
	{
		const float s = sqrtf(1.0F + m[4] - m[0] - m[8]) * 2.0F;

		q[0] = (m[2] - m[6]) / s;
		q[1] = (m[1] + m[3]) / s;
		q[2] = 0.25F * s;
		q[3] = (m[5] + m[7]) / s;
	}
	else
	{
		const float s = sqrtf(1.0F + m[8] - m[0] - m[4]) * 2.0F;

		q[0] = (m[3] - m[1]) / s;
		q[1] = (m[2] + m[6]) / s;
		q[2] = (m[5] + m[7]) / s;
		q[3] = 0.25F * s;
	}
}

inline void InterpQuatToRot(const float* q, float* m)
{
	const float w = q[0], x = q[1], y = q[2], z = q[3];

	m[0] = 1.0F - 2.0F * (y * y + z * z);
	m[1] = 2.0F * (x * y - z * w);
	m[2] = 2.0F * (x * z + y * w);
	m[3] = 2.0F * (x * y + z * w);
	m[4] = 1.0F - 2.0F * (x * x + z * z);
	m[5] = 2.0F * (y * z - x * w);
	m[6] = 2.0F * (x * z - y * w);
	m[7] = 2.0F * (y * z + x * w);
	m[8] = 1.0F - 2.0F * (x * x + y * y);
}

/* The three angles of a rotation matrix in InterpPoseRot's layout, which
 * stores m[7] = -sin x, m[6] / m[8] = tan y and m[1] / m[4] = tan z. x comes
 * back in [-90, 90] degrees; where cos x vanishes only y + z is defined and z
 * is taken as zero. In the engine's 16-bit angle, rounded. */
inline void InterpRotToAngles(const float* m, short* xyz)
{
	const float k = 32768.0F / 3.14159265358979323846F;
	float s = -m[7];

	if (s > 1.0F)
		s = 1.0F;
	else if (s < -1.0F)
		s = -1.0F;

	const float x = asinf(s);
	float y, z;

	if (fabsf(m[6]) + fabsf(m[8]) > 1e-5F)
	{
		y = atan2f(m[6], m[8]);
		z = atan2f(m[1], m[4]);
	}
	else
	{
		y = atan2f(-m[2], m[0]);
		z = 0.0F;
	}

	xyz[0] = (short)(long)floorf(x * k + 0.5F);
	xyz[1] = (short)(long)floorf(y * k + 0.5F);
	xyz[2] = (short)(long)floorf(z * k + 0.5F);
}

/* Two nodes' rotations blended at `alpha` along the shortest turn between
 * them (a spherical blend of their quaternions, the shorter of the two ways
 * round), as three angles. Every result is a rotation, where an element-wise
 * blend of the two matrices is not; and it is the shortest turn, where a
 * blend of the three angles one by one is not: a node's two keys may be
 * stored on the two sides of x = 90 degrees, (x, y, z) and its twin (180 - x,
 * y + 180, z + 180), and there the angles swing up to half a turn each while
 * the node turns a few degrees (animation 209 key 0 to 1 in ANDREA1: node 9
 * turns 31.6 degrees with y and z moving 157 each, and blended angle by angle
 * node 12's frame strays 191 degrees off the way between its keys).
 *
 * `longArc` takes the other way round instead, the turn's complement to a
 * whole turn in the opposite sense: the bridge after an early half turn asks
 * for it at the hips where the shorter way would run against her own motion
 * (InterpBridgeLong). Two rotations that do not differ have no other way
 * round and are blended as without it. */
inline void InterpSlerpRot(const short* a, const short* b, float alpha, short* out,
	bool longArc = false)
{
	float ma[9], mb[9], m[9], qa[4], qb[4], q[4];

	InterpPoseRot(a, ma);
	InterpPoseRot(b, mb);
	InterpRotToQuat(ma, qa);
	InterpRotToQuat(mb, qb);

	float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];

	if (d < 0.0F)
	{
		d = -d;

		for (int i = 0; i < 4; i++)
			qb[i] = -qb[i];
	}

	if (longArc && d < 0.9995F)
	{
		d = -d;

		for (int i = 0; i < 4; i++)
			qb[i] = -qb[i];
	}

	float wa = 1.0F - alpha, wb = alpha;

	if (d < 0.9995F)
	{
		const float th = acosf(d);
		const float sn = sinf(th);

		wa = sinf((1.0F - alpha) * th) / sn;
		wb = sinf(alpha * th) / sn;
	}

	float len = 0.0F;

	for (int i = 0; i < 4; i++)
	{
		q[i] = wa * qa[i] + wb * qb[i];
		len += q[i] * q[i];
	}

	len = sqrtf(len);

	for (int i = 0; i < 4; i++)
		q[i] /= len;

	InterpQuatToRot(q, m);
	InterpRotToAngles(m, out);
}

/* One node's packed rotation between `a` and `b` at `alpha`, written to
 * `out`; returns the words written and moves `a` and `b` past the node.
 *
 * A node equal in both is copied word for word. A node both store in the
 * one-axis form on the same axis blends that angle the short way round
 * (InterpLerpAngle) and stays in that form; any other node is blended by
 * InterpSlerpRot and packed in the three-axis form, and so is a node asked
 * for the longer way round (`longArc`), which the one-axis blend has no form
 * for. */
inline int InterpKeyNodeLerp(const short*& a, const short*& b, float alpha, bool longArc,
	short* out)
{
	const int axisA = InterpRotAxis(a);
	const int axisB = InterpRotAxis(b);
	const int wordsA = axisA < 0 ? 2 : 1;
	const int wordsB = axisB < 0 ? 2 : 1;
	int n = 0;

	if (wordsA == wordsB && a[0] == b[0] && (wordsA == 1 || a[1] == b[1]))
	{
		for (int w = 0; w < wordsA; w++)
			out[n++] = a[w];
	}
	else
	{
		const short* pa = a;
		const short* pb = b;
		short ra[3], rb[3], r[3] = { 0, 0, 0 };

		InterpUnpackRot(pa, ra);
		InterpUnpackRot(pb, rb);

		if (axisA >= 0 && axisA == axisB && !longArc)
		{
			r[axisA] = InterpLerpAngle(ra[axisA], rb[axisA], alpha);
			n += InterpPackRot(r, axisA, out + n);
		}
		else
		{
			InterpSlerpRot(ra, rb, alpha, r, longArc);
			n += InterpPackRot(r, -1, out + n);
		}
	}

	a += wordsA;
	b += wordsB;

	return n;
}

/* The key frame between `f0` and `f1` at `alpha`, written to `out` (at
 * least 9 + 2 * nodes shorts); returns the shorts written.
 *
 * The bounding box is the nearer key's (f0 up to a half), the root offset the
 * two keys' offsets blended as InterpRootKey blends them, so the root rule
 * and the drawn frame agree to the unit. Every node takes the shorter way
 * round (InterpKeyNodeLerp). */
inline int InterpKeyFrameLerp(const short* f0, const short* f1, int nodes, float alpha,
	short* out)
{
	const short* box = alpha > 0.5F ? f1 : f0;
	const long alpha8 = InterpEighths(alpha);
	int n = 9;

	for (int i = 0; i < 6; i++)
		out[i] = box[i];

	for (int i = 6; i < 9; i++)
		out[i] = (short)InterpBlendLong(f0[i], f1[i], alpha8);

	const short* a = f0 + 9;
	const short* b = f1 + 9;

	for (int node = 0; node < nodes; node++)
		n += InterpKeyNodeLerp(a, b, alpha, false, out + n);

	return n;
}

/* Where a node's own rotation turns more than 179 degrees between two keys,
 * the shorter way round and the longer one differ by less than two degrees,
 * which three angles each rounded to 0.35 degrees cannot tell apart: the way
 * InterpKeyFrameLerp takes is decided by the last bits of the data. */
#define INTERP_KEY_HALF_COS	-0.99985F	/* cos 179 degrees */

/* Two rotations in InterpPoseRot's layout blended at `alpha` along the
 * shortest turn between them (InterpSlerpRot's arithmetic on matrices),
 * written to `out`. */
inline void InterpSlerpMat(const float* a, const float* b, float alpha, float* out)
{
	float qa[4], qb[4], q[4];

	InterpRotToQuat(a, qa);
	InterpRotToQuat(b, qb);

	float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];

	if (d < 0.0F)
	{
		d = -d;

		for (int i = 0; i < 4; i++)
			qb[i] = -qb[i];
	}

	float wa = 1.0F - alpha, wb = alpha;

	if (d < 0.9995F)
	{
		const float th = acosf(d);
		const float sn = sinf(th);

		wa = sinf((1.0F - alpha) * th) / sn;
		wb = sinf(alpha * th) / sn;
	}

	float len = 0.0F;

	for (int i = 0; i < 4; i++)
	{
		q[i] = wa * qa[i] + wb * qb[i];
		len += q[i] * q[i];
	}

	len = sqrtf(len);

	for (int i = 0; i < 4; i++)
		q[i] /= len;

	InterpQuatToRot(q, out);
}

/* The key frame drawn at `alpha` across a far key pair of Lara's
 * (InterpKeyFarDrawn), written to `out` (at least 9 + 2 * nodes shorts);
 * returns the shorts written. `bone` is her bone table (InterpDrawnRots).
 *
 * It is InterpKeyFrameLerp's frame, word for word, unless a node's own
 * rotation turns within a degree of half a turn (INTERP_KEY_HALF_COS). Such
 * a node takes the way round whose drawn rotation, at the middle of the pair
 * and under its parent as built, lies nearer the middle of the shortest turn
 * between the two keys' drawn rotations - the turn vanilla's blend averages
 * across - and it keeps that way at every weight, so the tween frames across
 * the pair follow one path. In the standing jump up (91, key 0 to 1, in every
 * campaign level) the upper arm (node 11) turns 179.85 degrees on its own and
 * 133.8 in the drawn frame; the shorter way of its own turn puts the arm 180
 * degrees off the middle of that drawn turn on the in-between frame, the
 * longer way about 21 degrees off it. Over the thirteen campaign levels this
 * decides that node alone; every other far pair built is InterpKeyFrameLerp's
 * (rich3's 152 key 4 to 5, whose frame data run past their frame size and
 * whose node 1 turns 179.8 degrees on its own, is a seam, InterpKeySeamAt,
 * and is not built). */
inline int InterpKeyFrameFar(const short* f0, const short* f1, int nodes, const long* bone,
	float alpha, short* out)
{
	bool any = false;

	{
		const short* a = f0 + 9;
		const short* b = f1 + 9;

		for (int node = 0; node < nodes && node < 32; node++)
		{
			short ra[3], rb[3];
			float ma[9], mb[9];

			InterpUnpackRot(a, ra);
			InterpUnpackRot(b, rb);
			InterpPoseRot(ra, ma);
			InterpPoseRot(rb, mb);

			if (InterpPoseTurnCos(ma, mb) < INTERP_KEY_HALF_COS)
				any = true;
		}
	}

	if (!any || !bone || nodes > 32)
		return InterpKeyFrameLerp(f0, f1, nodes, alpha, out);

	static const float ident[9] = { 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F };
	float da[32 * 9], db[32 * 9], mid[32 * 9];
	bool longArc[32];
	int stack[32];
	int depth = 0, cur = -1;

	InterpDrawnRots(f0, nodes, bone, da);
	InterpDrawnRots(f1, nodes, bone, db);

	/* The ways round, decided at the middle, root first: each node's drawn
	 * rotation there under its parent's as built. */
	{
		const short* a = f0 + 9;
		const short* b = f1 + 9;

		for (int node = 0; node < nodes; node++)
		{
			if (node > 0)
			{
				const long flags = bone[(node - 1) * 4];

				if ((flags & 1) && depth > 0)
					cur = stack[--depth];

				if ((flags & 2) && depth < 32)
					stack[depth++] = cur;
			}

			const float* parent = cur >= 0 ? mid + cur * 9 : ident;
			short ra[3], rb[3], r[3];
			float ma[9], mb[9], m[9];

			InterpUnpackRot(a, ra);
			InterpUnpackRot(b, rb);
			InterpPoseRot(ra, ma);
			InterpPoseRot(rb, mb);
			InterpSlerpRot(ra, rb, 0.5F, r);
			InterpPoseRot(r, m);
			InterpRotMul(m, parent, mid + node * 9);
			longArc[node] = false;

			if (InterpPoseTurnCos(ma, mb) < INTERP_KEY_HALF_COS)
			{
				float g[9], other[9];

				InterpSlerpRot(ra, rb, 0.5F, r, true);
				InterpPoseRot(r, m);
				InterpRotMul(m, parent, other);
				InterpSlerpMat(da + node * 9, db + node * 9, 0.5F, g);

				if (InterpPoseTurnCos(other, g) > InterpPoseTurnCos(mid + node * 9, g))
				{
					longArc[node] = true;

					for (int i = 0; i < 9; i++)
						mid[node * 9 + i] = other[i];
				}
			}

			cur = node;
		}
	}

	const short* box = alpha > 0.5F ? f1 : f0;
	const long alpha8 = InterpEighths(alpha);
	int n = 9;

	for (int i = 0; i < 6; i++)
		out[i] = box[i];

	for (int i = 6; i < 9; i++)
		out[i] = (short)InterpBlendLong(f0[i], f1[i], alpha8);

	const short* a = f0 + 9;
	const short* b = f1 + 9;

	for (int node = 0; node < nodes; node++)
		n += InterpKeyNodeLerp(a, b, alpha, longArc[node], out + n);

	return n;
}

/* The key frame `f` turned by half a turn about the item's vertical, written
 * to `out` (at least 9 + 2 * nodes shorts); returns the shorts written.
 *
 * Drawn under an item turned by turn180_effect, the copy lands where `f`
 * lands under the item before the turn: the draw applies the item's rotation,
 * then the root offset, then node 0's rotation, so the offset turns as
 * InterpRootFlip turns it and node 0 gains half a turn in y (y is the outer
 * angle of phd_RotYXZ's order, so the half turn is added to it exactly). The
 * box's x and z extents turn with it. The effect also negates x_rot; with the
 * item's z_rot zero that is the same half turn seen from the other side, so
 * the copy is exact, and a lean z_rot would leave a turn of 2 z that the 30 Hz
 * picture carries as well. Node 0 keeps its form where it is three-axis or
 * one-axis in y; a one-axis x or z becomes three-axis, rounded to the step of
 * 64. Every other node is copied word for word. */
inline int InterpKeyFrameTurn(const short* f, int nodes, short* out)
{
	const long offset[3] = { f[6], f[7], f[8] };
	long turned[3];

	out[0] = (short)-f[1];
	out[1] = (short)-f[0];
	out[2] = f[2];
	out[3] = f[3];
	out[4] = (short)-f[5];
	out[5] = (short)-f[4];

	InterpRootFlip(offset, turned);

	for (int i = 0; i < 3; i++)
		out[6 + i] = (short)turned[i];

	const short* p = f + 9;
	int n = 9;

	for (int node = 0; node < nodes; node++)
	{
		const int axis = InterpRotAxis(p);
		const int words = axis < 0 ? 2 : 1;

		if (node == 0)
		{
			const short* q = p;
			short r[3];

			InterpUnpackRot(q, r);
			r[1] = (short)(r[1] + (short)-32768);
			n += InterpPackRot(r, axis == 1 ? 1 : -1, out + n);
		}
		else
		{
			for (int w = 0; w < words; w++)
				out[n++] = p[w];
		}

		p += words;
	}

	return n;
}

/* Where turn180_effect fires in an animation, and what that means for the
 * frames around it.
 *
 * The effect turns the item by half a turn on the tick whose frame_number
 * reaches `flip` (laramisc.cpp AnimateLara), and that tick draws frame `flip`
 * under the turned item. The pair of key frames around it, (k, k + 1) with k
 * = (flip - 1 - frameBase) / rate, says for which yaw the frame was made:
 *
 *   INTERP_TURN_INSIDE  the hips turn more than 90 degrees inside the pair
 *                       (InterpKeyFar): the animation carries the other half
 *                       turn itself and the frame belongs to the new yaw.
 *                       Measured on the tightrope turn 451 (key 26 in ANDREA1
 *                       and rich2, key 13 in ANDREA2 and ANDREA3) and the
 *                       underwater roll 205 (key 1): the flip falls on the
 *                       pair's second key frame in every case.
 *   INTERP_TURN_EARLY   the pair turns less: the frame belongs to the old yaw
 *                       and the turned item draws it half a turn off. The
 *                       effect sits on the animation's last frame and the
 *                       animation that follows is made for the new yaw.
 *                       Measured in ANDREA1, the hips' turn from the frame
 *                       before when the frame is drawn under the old yaw and
 *                       as drawn under the new one: 48, the end of the roll
 *                       47 (state AS_ROLL, into the stand 11; 0.9 / 179.3);
 *                       207, the twist out of a
 *                       running jump's start (5.3 / 176.8); 210, the twist
 *                       out of the forward jump (11.0 / 177.0); 212, the
 *                       twist out of the back jump (9.7 / 171.9). All four
 *                       fire on their last frame in all thirteen campaign
 *                       levels. The next animation's first frame follows the
 *                       turned frame by 5.9 (11), 13.4 (209), 19.4 (213) - and
 *                       179.2 (211): 211 begins half a turn about her
 *                       lateral axis away from 210's end, a seam in the
 *                       level's data that only the frames between the two
 *                       ticks bridge (InterpKeyFrameBridge).
 *   INTERP_TURN_NONE    no pair before it: the effect fires on the
 *                       animation's first frame (the roll 147), whose change
 *                       turns the root instead (InterpRootFlip).
 *
 * `keys` is the animation's frame block and `size` its frame size in shorts.
 * `pair` receives k, or -1. */
#define INTERP_TURN_NONE	0
#define INTERP_TURN_EARLY	1
#define INTERP_TURN_INSIDE	2

inline int InterpTurnKind(const short* keys, long size, long frameBase, long rate, long flip,
	long* pair)
{
	*pair = -1;

	if (!keys || size < 9 || rate < 1 || flip <= frameBase)
		return INTERP_TURN_NONE;

	const long k = (flip - 1 - frameBase) / rate;

	*pair = k;

	return InterpKeyFar(keys + size * k, keys + size * (k + 1))
		? INTERP_TURN_INSIDE : INTERP_TURN_EARLY;
}

/* True when the far key pair `pair` is held on its first key frame: the
 * half turn is INSIDE it and the flip falls on its second key, so every frame
 * between the two keys is drawn under the old yaw and only the first key
 * belongs to it. Every other far pair is built by InterpKeyFrameFar, save a
 * seam (InterpKeySeamAt), which vanilla's blend draws. INSIDE
 * asks the hips (InterpKeyFar), so a pair far only by another node is never
 * held: over the thirteen campaign levels the one such pair with
 * turn180_effect on its second key, rich3's 210 key 1 to 2 (the head 96.9
 * degrees in the drawn frame, the hips 23.3), is an early turn and is built. */
inline bool InterpKeyHold(int kind, long turnPair, long flip, long frameBase, long rate,
	long pair)
{
	return kind == INTERP_TURN_INSIDE && pair == turnPair &&
		flip == frameBase + (pair + 1) * rate;
}

/* ==========================================================================
 * The tween across the animation change after an early half turn.
 * ==========================================================================
 *
 * An early turn180_effect (INTERP_TURN_EARLY) fires on the last frame of its
 * animation, so the next tick stands on another animation, and the pose rule
 * refuses a tween across an animation change (draw.cpp GetFramesAt): every
 * frame drawn between the two ticks would wear the new tick's pose. After
 * 210, the twist out of the forward jump, that pose is 211's first key, which
 * the level's data pitches half a turn about her lateral axis away from 210's
 * last frame: the body would turn 179 degrees on one drawn frame and then
 * stand still until the next tick.
 *
 * Those frames are drawn from a key frame built between the two poses
 * instead (InterpKeyFrameBridge): from the pose the flip tick drew (its key
 * frames turned by half a turn, InterpKeyFrameTurn, and built at the tick's
 * factor where the frame lies between two keys) to the pose of the current
 * tick. Every node turns on a spherical path, so each frame between is a
 * rotation, and the step is spread over the frames the display draws between
 * the two ticks: one at sixty hertz, five or six at 165. It is a motion the
 * animation does not carry; the draw invents it. */

/* True on a drawn frame whose pose is bridged: a frame between two ticks
 * (0 < t < 1), the tick before stood on the frame an early turn180_effect
 * fires on (`kind` and `flip` are InterpTurnOf's answer for `prevAnim`), and
 * the animation has changed since. */
inline bool InterpBridgeFires(int kind, long flip, long prevAnim, long prevFrame, long anim,
	float t)
{
	return t > 0.0F && t < 1.0F && kind == INTERP_TURN_EARLY && prevFrame == flip &&
		anim != prevAnim;
}

/* The turn of the hips (node 0) from frame `a` to frame `b` as the item sees
 * it: `v` receives the vector part of the turn's unit quaternion, taken the
 * shorter way round, so its direction is the axis and the sense of the turn
 * and its length the sine of half the angle; returns the scalar part, the
 * cosine of half the angle. InterpPoseRot's layout is the transpose of the
 * matrix the draw's stack builds, so the turn the stack sees, B A^T, is
 * conj(qb) * qa in its quaternions. */
inline float InterpHipsTurn(const short* a, const short* b, float* v)
{
	const short* pa = a + 9;
	const short* pb = b + 9;
	short ra[3], rb[3];
	float ma[9], mb[9], qa[4], qb[4];

	InterpUnpackRot(pa, ra);
	InterpUnpackRot(pb, rb);
	InterpPoseRot(ra, ma);
	InterpPoseRot(rb, mb);
	InterpRotToQuat(ma, qa);
	InterpRotToQuat(mb, qb);

	const float w = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
	const float s = w < 0.0F ? -1.0F : 1.0F;

	v[0] = s * (qb[0] * qa[1] - qa[0] * qb[1] - (qb[2] * qa[3] - qb[3] * qa[2]));
	v[1] = s * (qb[0] * qa[2] - qa[0] * qb[2] - (qb[3] * qa[1] - qb[1] * qa[3]));
	v[2] = s * (qb[0] * qa[3] - qa[0] * qb[3] - (qb[1] * qa[2] - qb[2] * qa[1]));

	return s * w;
}

/* Whether the bridge from `from` to `to` takes the longer way round at the
 * hips.
 *
 * Across nearly half a turn the shorter way is decided by the last few
 * degrees of the data and may run against the motion the animation shows. So
 * where the hips turn more than 90 degrees between the two (InterpKeyFar)
 * the shorter way is checked against the hips' own turn over the key pair
 * that led into `from` (`before0` to `before1`) and over the pair that leads
 * out of `to` (`after0` to `after1`, null where `to` is its animation's last
 * key): only where it turns against both is the longer way taken. A side
 * whose hips do not turn does not count against it. Every other node keeps
 * the shorter way: it turns relative to the hips, where the two poses lie
 * close (from 210's last frame to 211's first within 1.4 degrees in
 * ANDREA1). */
inline bool InterpBridgeLong(const short* from, const short* to, const short* before0,
	const short* before1, const short* after0, const short* after1)
{
	if (!InterpKeyFar(from, to))
		return false;

	float arc[3], m[3];

	InterpHipsTurn(from, to, arc);
	InterpHipsTurn(before0, before1, m);

	if (arc[0] * m[0] + arc[1] * m[1] + arc[2] * m[2] >= 0.0F)
		return false;

	if (!after0 || !after1)
		return false;

	InterpHipsTurn(after0, after1, m);

	return arc[0] * m[0] + arc[1] * m[1] + arc[2] * m[2] < 0.0F;
}

/* The key frame drawn at weight `t` between `from` and `to`, written to `out`
 * (at least 9 + 2 * nodes shorts); returns the shorts written.
 *
 * The box and the root offset are `to`'s own: the box is not drawn, and the
 * root offset of a frame between two ticks is the root rule's to blend
 * (InterpRootNow reads it from the drawn pair as it reads `to` on a refused
 * frame), so the place under the pose comes out exactly as it does without
 * the bridge. Every node turns by InterpKeyNodeLerp, the hips the longer way
 * round where `longHips` (InterpBridgeLong). At t 0 the frame is `from` and
 * at t 1 `to`, to the step of the packed form. */
inline int InterpKeyFrameBridge(const short* from, const short* to, int nodes, float t,
	bool longHips, short* out)
{
	for (int i = 0; i < 9; i++)
		out[i] = to[i];

	const short* a = from + 9;
	const short* b = to + 9;
	int n = 9;

	for (int node = 0; node < nodes; node++)
		n += InterpKeyNodeLerp(a, b, t, node == 0 && longHips, out + n);

	return n;
}

/* ==========================================================================
 * Particles.
 *
 * Every particle simulation in this engine settles the velocity first and
 * moves by it second:
 *
 *     sptr->Yvel += sptr->Gravity;       // whatever changes the velocity
 *     sptr->x += sptr->Xvel >> 5;        // ... then the step it takes
 *
 * so the stored velocity is exactly the last tick's step and the previous
 * position is `cur - step`, with no snapshot. The particle is walked
 * backwards from the current state, not forwards (`cur + vel * alpha` would
 * show the world one logic step ahead of the bodies around it):
 *
 *     InterpTrailLong(cur, step, alpha) == InterpLerpLong(cur - step, cur, alpha)
 *
 * It is written as that lerp rather than `cur - step * (1 - alpha)`, which
 * truncates differently by one unit on odd differences; InterpTest asserts
 * the identity at both frame rates. Not called at fps_mode 0; at alpha 1 it
 * returns `cur`. */
inline long InterpTrailLong(long cur, long step, float alpha)
{
	return InterpLerpLong(cur - step, cur, alpha);
}

/* ==========================================================================
 * Where a camera target is a place and where it is a direction.
 *
 * A camera cut is declared when the eye or the target moves more than
 * INTERP_CAM_SNAP in one tick. For the chase and fixed cameras the target is
 * a place a couple of sectors away. FirstPersonCamera (camera.cpp) sets
 *
 *     camera.target = camera.pos + direction * 0x5100
 *
 * 20736 units down the line of sight, to express a direction; there a turn of
 * 2.8 degrees moves the target a whole sector, and Lara's idle head motion
 * would fire 10 to 17 false cuts a second, each drawing the camera live
 * against a half-step-back world.
 *
 * Beyond INTERP_CAM_FAR_TARGET both targets are pulled onto a sphere of
 * INTERP_CAM_DIR_RADIUS about their own eye and the threshold is applied to
 * the change of direction alone (the eye's travel has its own test). At
 * radius 1024 a Manhattan sum of 1024 is a turn of 41 to 57 degrees in one
 * tick (1200 to 1700 degrees a second); a 90 degree cut measures 1448, a half
 * turn 2048. The chase camera's target, about a sector and a half away, stays
 * under the shell and keeps the absolute test. */
#define INTERP_CAM_FAR_TARGET	4096L
#define INTERP_CAM_DIR_RADIUS	1024L

/* One camera's target relative to its own eye: the offset itself while the
 * target is a place, scaled onto the sphere while it is a direction.
 *
 * The engine does not call this; InterpCamAimCut chooses the branch once for
 * the pair, because a per-camera choice puts a cliff on the shell (4095 a
 * place, 4097 a direction). InterpTest (Preparation\Tools\Diagnose) compares
 * the two rules and needs it, and it states what an aim offset is. */
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

/* True when the view direction cut between the two cameras; shared by
 * interpolate.cpp and InterpTest.
 *
 * The branch is chosen once for the pair: far if either target is beyond
 * INTERP_CAM_FAR_TARGET, and then both offsets are scaled by the same k,
 * INTERP_CAM_DIR_RADIUS over the longer length. Choosing per camera would
 * compare a target at 4095 as a place and one at 4097 as a direction, a jump
 * of three thousand units from nothing; two different k's would hide a
 * target that really jumped along its own ray. Under the shell nothing is
 * scaled and the test is absolute. */
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

/* ==========================================================================
 * The cutscene nodes between two ticks.
 *
 * A cutscene actor is a table of packed nodes decoded once per tick
 * (DecodeAnim, game\deltapak.cpp): node 0 holds the root position, drawn
 * at three times its value (updateAnimFrame, game\deltapak.cpp), and every
 * other node three angles of ten bits (& 0x3FF, 1024 to a turn). The draw
 * reads nothing but these values, so a frame between two ticks is drawn from
 * the two tables blended (game\cutseq_interp.cpp); the arithmetic is here so
 * that InterpTest runs it without a game.
 *
 * INTERP_CUTSEQ_ROOT_SNAP: a root whose drawn position steps further than
 * this in one tick is a jump in the track, and that actor is drawn at the
 * tick's values for the frame. Over the file 119 root steps are above it, 44
 * of them without a camera cut. No value parts jumps from motion cleanly: a
 * falling object in cutscene 38 moves 1029 to 1032 a frame and is drawn at
 * the tick's values, and 17 lone jumps under it with no camera cut are
 * blended as a slide for one tick. INTERP_CUTSEQ_ACTORS is the engine's own
 * ceiling (actor_pnodes[10]); INTERP_CUTSEQ_NODES the ceiling of one actor's
 * table the snapshot holds, above the largest in the file (30). */
#define INTERP_CUTSEQ_ROOT_SNAP	1024L
#define INTERP_CUTSEQ_ACTORS	10
#define INTERP_CUTSEQ_NODES	40

/* The short way round for a ten-bit angle: the difference is folded into
 * -512 .. 511 and the result masked back to 0 .. 1023. Exactly half a turn
 * comes out as -512 and has no shorter way; InterpHalfTurn10 refuses it. */
inline short InterpLerpAngle10(short a, short b, float alpha)
{
	const long d = ((((long)b - (long)a) + 512) & 0x3FF) - 512;

	return (short)(((long)a + (long)((float)d * alpha)) & 0x3FF);
}

inline bool InterpHalfTurn10(short a, short b)
{
	return (((long)b - (long)a) & 0x3FF) == 512;
}

/* The cutscene camera's cut rule: the eye or the target stepped further than
 * INTERP_CUTSEQ_SNAP between the two ticks. Both steps are handed back for
 * the log. */
inline bool InterpCutseqCameraCut(const long prevEye[3], const long eye[3],
	const long prevTarget[3], const long target[3], long* eyeStep, long* targetStep)
{
	*eyeStep = InterpManhattan(eye[0] - prevEye[0], eye[1] - prevEye[1], eye[2] - prevEye[2]);
	*targetStep = InterpManhattan(target[0] - prevTarget[0], target[1] - prevTarget[1],
		target[2] - prevTarget[2]);

	return *eyeStep > INTERP_CUTSEQ_SNAP || *targetStep > INTERP_CUTSEQ_SNAP;
}

/* How far an actor's drawn root moved between two node tables of x, y, z
 * triples: node 0, at three times its value. */
inline long InterpCutseqRootStep(const short* prev, const short* cur)
{
	return InterpManhattan(3 * ((long)cur[0] - (long)prev[0]), 3 * ((long)cur[1] - (long)prev[1]),
		3 * ((long)cur[2] - (long)prev[2]));
}

/* One actor's table between two ticks. `prev` and `cur` hold `nodes` x, y, z
 * triples; `out` receives the blend at `alpha`. Returns false with `out`
 * untouched when the root stepped further than INTERP_CUTSEQ_ROOT_SNAP: the
 * actor is drawn at the tick's values. Otherwise the root is blended as a
 * position and every other node as three ten-bit angles, except a node with
 * an axis exactly half a turn from its previous value, which keeps the
 * tick's three values: a half turn has no shorter way, and its other axes
 * usually turn with it (an Euler flip), so blending them alone would draw a
 * rotation neither tick holds. `rootStep` receives the root's step,
 * `halfTurns` the nodes kept for a half turn, `halfNode` the first of them
 * or -1. */
inline bool InterpCutseqBlend(const short* prev, const short* cur, int nodes, float alpha,
	short* out, long* rootStep, int* halfTurns, int* halfNode)
{
	*rootStep = InterpCutseqRootStep(prev, cur);
	*halfTurns = 0;
	*halfNode = -1;

	if (*rootStep > INTERP_CUTSEQ_ROOT_SNAP)
		return false;

	for (int k = 0; k < 3; k++)
		out[k] = (short)InterpLerpLong(prev[k], cur[k], alpha);

	for (int i = 1; i < nodes; i++)
	{
		const short* p = prev + 3 * i;
		const short* c = cur + 3 * i;
		short* o = out + 3 * i;

		if (InterpHalfTurn10(p[0], c[0]) || InterpHalfTurn10(p[1], c[1]) ||
			InterpHalfTurn10(p[2], c[2]))
		{
			o[0] = c[0];
			o[1] = c[1];
			o[2] = c[2];

			if (*halfNode < 0)
				*halfNode = i;

			(*halfTurns)++;
			continue;
		}

		for (int k = 0; k < 3; k++)
			o[k] = InterpLerpAngle10(p[k], c[k], alpha);
	}

	return true;
}

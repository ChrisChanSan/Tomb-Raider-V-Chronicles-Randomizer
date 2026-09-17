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
#include "modern_camera.h"
#include "modern_move.h"	// ModernWrap and MODERN_STICK_FULL - see below

/* The arithmetic of the modern camera (#161, round 161a2, step 1). The header
 * says what the orbit is, whose model it is and where every number came from;
 * this file is the three rules and nothing else. No engine header, no global,
 * no side effect outside the state the caller hands in.
 *
 * WHY game\modern_move.h IS THE ONE INCLUDE. It is pure too - that is its
 * whole point - so it costs this file nothing, and it already owns the two
 * facts this one would otherwise have had to copy: ModernWrap, the fold of an
 * angle into the shorter way round (written out rather than cast, because a
 * cast of a value over 32767 to short is implementation-defined before C++20
 * and two different build scripts compile these files), and
 * MODERN_STICK_FULL, the hardware fact that specific\dxshell.cpp scales the
 * axes down by 256. Two copies of a wrap is two wraps to keep in step. */

/* An angle's magnitude has to be taken as a long: |delta| where delta is
 * -32768 does not fit in a short, and -32768 is exactly the half turn an
 * offset can hold after the stick has gone once round her. */
static long ModernCameraAbs(long v)
{
	return (v < 0) ? -v : v;
}

/* Is this axis out of its rest position at all? Asked separately from the
 * rate below, because "the player is steering" has to be true for an axis
 * whose rate has rounded down to nothing - one unit of stick is 8 units of
 * yaw, so that cannot happen at the shipping numbers, but a cfg with a slow
 * rate would otherwise let the return run under the player's thumb. */
static long ModernCameraAxisOut(const MODERN_CAMERA_CFG* cfg, long axis)
{
	return (axis > cfg->dead || axis < -cfg->dead) ? 1 : 0;
}

/* One axis of the stick as a rate of turn: the axis IS the rate, scaled so
 * that full deflection is the whole constant. The clamp is not cosmetic - a
 * diagonal reads about 181 in our units (the two axes are not scaled as a
 * vector, see MODERN_STICK_FULL) and an unclamped axis would hand the orbit
 * 1.4 times the rate the header promises for a push it cannot tell from any
 * other. */
static long ModernCameraRate(const MODERN_CAMERA_CFG* cfg, long axis, long rate)
{
	long a = axis;

	if (!cfg->stick_full)
		return 0;		// a cfg with no scale moves nothing, rather than divide by it

	if (a > cfg->stick_full)
		a = cfg->stick_full;
	else if (a < -cfg->stick_full)
		a = -cfg->stick_full;

	if (!ModernCameraAxisOut(cfg, a))
		return 0;

	return rate * a / cfg->stick_full;
}

/* One tick of a fall towards `home`, the short way round, by a fraction of
 * what is left. Returns the signed step it took, which is what MODERNLOG
 * prints as cam_ret.
 *
 * THE PRODUCT IS THE ONE THING TO WATCH. What is left is at most 32768 and
 * the rate is a fraction of 65536, so mag * rate reaches 2147450880 at the
 * widest rate a caller can ask for - eleven counts under what a signed long
 * holds. That is the reason for the guard below rather than a clamp to
 * MODERN_CAMERA_RETURN_ONE itself: at the full fraction the answer is "all of
 * it" and no multiply is needed, and every fraction under it fits. */
static long ModernCameraFall(long* value, long home, long rate, long min_step)
{
	const long delta = ModernWrap(home - *value);
	const long mag = ModernCameraAbs(delta);
	long step;

	if (!mag || rate <= 0)
		return 0;

	if (rate >= MODERN_CAMERA_RETURN_ONE)
		step = mag;
	else
		step = mag * rate / MODERN_CAMERA_RETURN_ONE;

	if (step < min_step)
		step = min_step;

	if (step > mag)
		step = mag;

	if (delta < 0)
		step = -step;

	*value = ModernWrap(*value + step);

	return step;
}

void ModernCameraDefaults(MODERN_CAMERA_CFG* cfg)
{
	if (!cfg)
		return;

	cfg->dead = MODERN_CAMERA_DEAD;
	cfg->stick_full = MODERN_STICK_FULL;	// the left stick's own scale, one fact
	cfg->yaw_rate = MODERN_CAMERA_YAW_RATE;
	cfg->pitch_rate = MODERN_CAMERA_PITCH_RATE;
	cfg->pitch_min = MODERN_CAMERA_PITCH_MIN;
	cfg->pitch_max = MODERN_CAMERA_PITCH_MAX;
	cfg->pitch_home = MODERN_CAMERA_PITCH_HOME;
	cfg->invert_pitch = MODERN_CAMERA_INVERT_PITCH;
	cfg->return_rate = MODERN_CAMERA_RETURN;
	cfg->return_min = MODERN_CAMERA_RETURN_MIN;
	cfg->pitch_return = MODERN_CAMERA_PITCH_RETURN;
	cfg->wall_factor = MODERN_CAMERA_WALL_FACTOR;
	cfg->wall_dead = MODERN_CAMERA_WALL_DEAD;
	cfg->follow_wall = MODERN_CAMERA_FOLLOW_WALL;
	cfg->hide_dist = MODERN_CAMERA_HIDE_DIST;
}

/* BEHIND HER BACK, AND AT THE HEIGHT THE GAME HAS ALWAYS USED.
 *
 * The yaw goes to zero because zero IS behind her - the offset is measured
 * from her heading, which is what makes it survive her turning. The pitch does
 * not go to zero, and that is not an oversight: the pitch is the elevation
 * itself, so zero would be dead level, ten degrees below where ChaseCamera
 * puts an untouched camera, and it would land on the very value that function
 * reads as "nobody has set this" (camera.cpp:1006). MODERN_CAMERA_PITCH_HOME
 * is that resting elevation, and a cfg that moves pitch_home somewhere else
 * has to seat the state itself - the same division of labour as A1's
 * ModernResetState, which is a state function and takes no cfg either. */
void ModernCameraReset(MODERN_CAMERA_STATE* state)
{
	if (!state)
		return;

	state->offset_yaw = 0;
	state->offset_pitch = MODERN_CAMERA_PITCH_HOME;
	state->wall_push = 0;
	state->wall = 0;
	state->ret = 0;
	state->steering = 0;
}

/* THE WHOLE ORBIT, IN THE ORDER THE THREE RULES HAPPEN AND FOR A REASON.
 *
 * `wall_push_yaw` is the displacement geometry forced on the camera: the yaw
 * it really ended up at minus the yaw the orbit asked for, zero when nothing
 * was in the way. It is LAST tick's news - step 2 hands in what ChaseCamera
 * resolved after the previous update - which is why it is answered first,
 * before this tick's stick can be laid on top of it.
 *
 * Taking it INTO the offset is what observation 3's "mit dem Winkel der Wand"
 * means, and it is stable rather than a feedback loop: once the offset names
 * the angle the camera is really at, the next tick asks for an angle nothing
 * blocks, the displacement comes back as zero and the orbit is quiet. While
 * she RUNS along the wall it does not go quiet - her heading moves under the
 * offset every tick, so the absolute angle is blocked again every tick - and
 * that is exactly the case the user described, and the reason the fast return
 * needs no memory of its own to last as long as the wall does.
 *
 * `lara_moving` is the caller's word for "she is going somewhere", and the
 * return is the only thing that reads it: standing, the offset is kept (2);
 * moving, it falls back behind her (3). The return also stands still while
 * the player is STEERING, which is the one thing neither sentence covers - he
 * turned the camera and THEN ran - and the honest reading of both: a camera
 * that pulls against the thumb holding it is a camera fighting the player for
 * nine per cent of his own input. */
void ModernCameraUpdate(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state,
	long rs_x, long rs_y, long lara_moving, long wall_push_yaw,
	short* out_yaw_offset, short* out_pitch)
{
	long yaw;
	long pitch;
	long push;
	long rate;

	if (!cfg || !state)
		return;

	yaw = state->offset_yaw;
	pitch = state->offset_pitch;
	state->wall = 0;
	state->ret = 0;

	/* 1. THE WALL. */
	push = ModernWrap(wall_push_yaw);

	if (ModernCameraAbs(push) > cfg->wall_dead)
	{
		state->wall = 1;
		state->wall_push = (short)push;

		if (cfg->follow_wall)
			yaw = ModernWrap(yaw + push);
	}

	/* 2. THE STICK. The yaw wraps - it is 360 degrees either way, observation
	 * 1 - and the pitch clamps, because over the pole the camera would hang
	 * upside down behind her and the classic camera would take the angle back
	 * off us anyway (MODERN_CAMERA_PITCH_MIN). */
	state->steering = (ModernCameraAxisOut(cfg, rs_x) || ModernCameraAxisOut(cfg, rs_y)) ? 1 : 0;

	yaw = ModernWrap(yaw + ModernCameraRate(cfg, rs_x, cfg->yaw_rate));

	if (cfg->invert_pitch)
		pitch += ModernCameraRate(cfg, rs_y, cfg->pitch_rate);
	else
		pitch -= ModernCameraRate(cfg, rs_y, cfg->pitch_rate);

	/* 3. THE RETURN. */
	if (lara_moving && !state->steering)
	{
		rate = cfg->return_rate;

		if (state->wall)
			rate *= cfg->wall_factor;

		state->ret = ModernCameraFall(&yaw, 0, rate, cfg->return_min);

		if (cfg->pitch_return)
			ModernCameraFall(&pitch, cfg->pitch_home, rate, cfg->return_min);
	}

	/* The clamp last, and on the long, so that nothing can wrap on its way
	 * into a short: the return only ever moves the pitch towards a home that
	 * is inside the limits, but the stick above can walk it straight at
	 * them. */
	if (pitch > cfg->pitch_max)
		pitch = cfg->pitch_max;
	else if (pitch < cfg->pitch_min)
		pitch = cfg->pitch_min;

	state->offset_yaw = ModernWrap(yaw);
	state->offset_pitch = (short)pitch;

	if (out_yaw_offset)
		*out_yaw_offset = state->offset_yaw;

	if (out_pitch)
		*out_pitch = state->offset_pitch;
}

/* IS SHE TOO CLOSE TO DRAW? Observation 4, and one line, because the whole
 * difficulty of it is in step 2: WHICH distance is measured, once per frame,
 * and every path that draws her (game\draw.cpp, specific\hd\hd_laradraw.cpp,
 * the braid, the shadow) asking the same question. What belongs here is only
 * the comparison, so that MODERN_CAMERA_HIDE_DIST stays a number a test can
 * hold to its origin. A hard answer and not a fade: the user measured the
 * Remaster cutting her within a twelfth of a second. */
long ModernCameraHidesLara(const MODERN_CAMERA_CFG* cfg, long distance)
{
	if (!cfg)
		return 0;

	return (distance < cfg->hide_dist) ? 1 : 0;
}

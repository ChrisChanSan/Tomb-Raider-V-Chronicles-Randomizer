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

/* THE SPEED SLIDER, AS ONE MULTIPLY (round 161a2b, item 5). "Der rechte Stick
 * muss noch schneller werden, der ist viel zu langsam. Am besten ein Slider wie
 * die SFX und Musik." The ROW is specific\LoadSave.cpp's and the stored value
 * is tomb5's (step 5); what lives here is the whole of what the number MEANS,
 * so that the options page, the registry and the test all ask one function.
 *
 * Linear between the two ends, in the orbit's own units, and the division is by
 * the slider's own top so that the arithmetic comes out exact at the default:
 * 364 + 40 * 1820 / 100 = 1092, the rate round 161a2 shipped. The pitch is half
 * the yaw, as it has been since the orbit was written.
 *
 * THE CLAMP IS NOT DECORATION. The value arrives from the registry, and a
 * registry key a user can edit is a key that can hold 5000; an unclamped
 * multiply would hand the orbit a rate that trips the 60 fps cut detection on
 * its own steps, which is the one thing the header promises it cannot do. */
void ModernCameraSpeedRates(long speed, long* yaw_rate, long* pitch_rate)
{
	long yaw;

	if (speed < 0)
		speed = 0;
	else if (speed > MODERN_CAMERA_SPEED_FULL)
		speed = MODERN_CAMERA_SPEED_FULL;

	yaw = MODERN_CAMERA_YAW_RATE_MIN +
		speed * (MODERN_CAMERA_YAW_RATE_MAX - MODERN_CAMERA_YAW_RATE_MIN) /
		MODERN_CAMERA_SPEED_FULL;

	if (yaw_rate)
		*yaw_rate = yaw;

	if (pitch_rate)
		*pitch_rate = yaw / 2;
}

void ModernCameraDefaults(MODERN_CAMERA_CFG* cfg)
{
	if (!cfg)
		return;

	cfg->dead = MODERN_CAMERA_DEAD;
	cfg->stick_full = MODERN_STICK_FULL;	// the left stick's own scale, one fact
	ModernCameraSpeedRates(MODERN_CAMERA_SPEED_DEFAULT, &cfg->yaw_rate, &cfg->pitch_rate);
	cfg->pitch_min = MODERN_CAMERA_PITCH_MIN;
	cfg->pitch_max = MODERN_CAMERA_PITCH_MAX;
	cfg->pitch_home = MODERN_CAMERA_PITCH_HOME;
	cfg->invert_pitch = MODERN_CAMERA_INVERT_PITCH;
	cfg->return_rate = MODERN_CAMERA_RETURN;
	cfg->return_min = MODERN_CAMERA_RETURN_MIN;
	cfg->pitch_return = MODERN_CAMERA_PITCH_RETURN;
	cfg->wall_factor = MODERN_CAMERA_WALL_FACTOR;
	cfg->hide_dist = MODERN_CAMERA_HIDE_DIST;
}

/* BEHIND HER BACK, AND AT THE HEIGHT THE GAME HAS ALWAYS USED.
 *
 * The OFFSET goes to zero because zero is behind her, and the world yaw goes
 * with it unseated: this function has no Lara to ask, so "behind her" is a
 * promise the first update keeps, on the heading it is handed (round 161a2b,
 * item 1). The pitch does not go to zero, and that is not an oversight: the
 * pitch is the elevation itself, so zero would be dead level, ten degrees
 * below where ChaseCamera puts an untouched camera, and it would land on the
 * very value that function reads as "nobody has set this" (camera.cpp:1006).
 * MODERN_CAMERA_PITCH_HOME is that resting elevation, and a cfg that moves
 * pitch_home somewhere else has to seat the state itself - the same division
 * of labour as A1's ModernResetState, which is a state function and takes no
 * cfg either. */
void ModernCameraReset(MODERN_CAMERA_STATE* state)
{
	if (!state)
		return;

	state->world_yaw = 0;
	state->offset_yaw = 0;
	state->offset_pitch = MODERN_CAMERA_PITCH_HOME;
	state->seated = 0;
	state->wall = 0;
	state->ret = 0;
	state->steering = 0;
}

/* THE WHOLE ORBIT, IN THE ORDER THE THREE RULES HAPPEN AND FOR A REASON.
 *
 * `lara_heading` is her y_rot, and it is the only thing that turns this file's
 * world yaw into the offset the engine's slot wants. It is handed in on every
 * tick rather than remembered, because a state that keeps its own copy of her
 * heading is a state that can quietly disagree with her.
 *
 * HER TURNING MOVES NOTHING HERE, and that is item 1 of round 161a2b: the
 * world yaw is not touched by the heading at all, so a quarter turn under a
 * resting stick leaves the camera standing exactly where it stood, and only
 * the DERIVED offset changes - by minus her turn, which is precisely what
 * keeps the view still while she spins. "Die Kamera richtet sich auch nicht
 * selbstaendig wieder hinter Lara."
 *
 * `wall_clipped` is the engine's word for "geometry took the eye off the ideal
 * this tick". It is a FLAG and no longer an angle (item 3): the camera keeps
 * the angle the player dialled and loses DISTANCE instead, so there is nothing
 * left to absorb - only the return to speed up while it lasts, which is
 * observation 3's "dann geht sie mit dem Winkel der Wand schneller zum
 * Ursprung hinter Lara".
 *
 * `lara_moving` is the caller's word for "she is going somewhere", and the
 * return is the only thing that reads it: standing, the camera is kept (2);
 * moving, it falls back behind her (3). The return also stands still while
 * the player is STEERING, which is the one thing neither sentence covers - he
 * turned the camera and THEN ran - and the honest reading of both: a camera
 * that pulls against the thumb holding it is a camera fighting the player for
 * nine per cent of his own input. */
void ModernCameraUpdate(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state,
	long rs_x, long rs_y, long lara_heading, long lara_moving, long wall_clipped,
	short* out_yaw_offset, short* out_pitch)
{
	long heading;
	long world;
	long pitch;
	long rate;

	if (!cfg || !state)
		return;

	heading = ModernWrap(lara_heading);

	/* THE SEATING, and it happens here rather than in the reset because the
	 * reset has no Lara to ask: whatever offset the state is holding becomes
	 * an offset from THIS heading, once. After that the world yaw is the state
	 * and the offset is only ever an output of it. */
	if (!state->seated)
	{
		state->world_yaw = ModernWrap(heading + state->offset_yaw);
		state->seated = 1;
	}

	world = state->world_yaw;
	pitch = state->offset_pitch;
	state->wall = wall_clipped ? 1 : 0;
	state->ret = 0;

	/* 1. THE STICK. The yaw wraps - it is 360 degrees either way, observation
	 * 1 - and the pitch clamps, because over the cap the camera sits on her
	 * own axis, where nothing measured from the eye has a direction left
	 * (MODERN_CAMERA_PITCH_MIN). */
	state->steering = (ModernCameraAxisOut(cfg, rs_x) || ModernCameraAxisOut(cfg, rs_y)) ? 1 : 0;

	world = ModernWrap(world + ModernCameraRate(cfg, rs_x, cfg->yaw_rate));

	if (cfg->invert_pitch)
		pitch += ModernCameraRate(cfg, rs_y, cfg->pitch_rate);
	else
		pitch -= ModernCameraRate(cfg, rs_y, cfg->pitch_rate);

	/* 2. THE RETURN, whose home is HER HEADING now and no longer an offset of
	 * zero: the same fall, written where the state really lives. */
	if (lara_moving && !state->steering)
	{
		rate = cfg->return_rate;

		if (state->wall)
			rate *= cfg->wall_factor;

		state->ret = ModernCameraFall(&world, heading, rate, cfg->return_min);

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

	/* And the derivation, once, at the end: the offset the engine's slot wants
	 * is this tick's world yaw measured against this tick's heading. */
	state->world_yaw = ModernWrap(world);
	state->offset_yaw = ModernWrap((long)state->world_yaw - heading);
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

/* WHAT ATTITUDE IS THE ORBIT POINTING AT (round 161a2 step 4, the water)?
 *
 * The user's observation 7 calls the right stick a 3D pointer "der Lara dann
 * folgt", so the water needs the orbit's pitch as a BODY pitch, and the two
 * are not the same number: the chase camera has always sat pitch_home above
 * her own axis. ChaseCamera's own line is the proof and the arithmetic -
 * "camera.target_elevation = -1820 + item->pos.x_rot" (game\camera.cpp), so
 * the elevation that belongs to an attitude is home + x_rot, and the attitude
 * that belongs to an elevation is elevation - home. Inverting the engine's own
 * line rather than picking a number means a camera left alone points exactly
 * at level swimming, which is where TR5 has pointed it since 1999.
 *
 * It is NOT clamped here. The orbit's own limits are the chase camera's
 * (+-15470) and the water's are LaraUnderWater's (the same +-15470), but they
 * are offset from one another by home, so the top of the range really is out
 * of her reach by 1820 units and the water's own move is where that is
 * answered - with its own constants, in the file the play test would tune. */
short ModernCameraSwimPitch(const MODERN_CAMERA_CFG* cfg, long pitch)
{
	MODERN_CAMERA_CFG spare_cfg;

	if (!cfg)
	{
		ModernCameraDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	return (short)(pitch - cfg->pitch_home);
}

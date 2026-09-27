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
#include "interpolate_math.h"	// InterpLerpLong - see ModernCameraChordClamp

/* The arithmetic of the modern camera. The header says what the orbit is and
 * where every number comes from; this file is the three rules. No engine
 * header, no global, no side effect outside the state the caller hands in.
 *
 * game\modern_move.h is the one include: it is pure too, and it already owns
 * two facts this file needs - ModernWrap, the fold of an angle into the
 * shorter way round (written out rather than cast, because a cast of a value
 * over 32767 to short is implementation-defined before C++20 and two build
 * scripts compile these files), and MODERN_STICK_FULL, the fact that
 * specific\dxshell.cpp scales the axes down by 256.
 *
 * game\interpolate_math.h is the other, pure as well: the drawn eye's chord
 * search builds its points with the interpolator's own InterpLerpLong, so a
 * point it tests is the point the frame draws. */

/* An angle's magnitude has to be taken as a long: |delta| where delta is
 * -32768 does not fit in a short, and -32768 is exactly the half turn an
 * offset can hold after the stick has gone once round her. */
static long ModernCameraAbs(long v)
{
	return (v < 0) ? -v : v;
}

/* Whether this axis is out of its rest position at all. Asked separately from
 * the rate below, because "the player is steering" has to be true for an axis
 * whose rate has rounded down to nothing - one unit of stick is 8 units of
 * yaw at the default numbers, but a cfg with a slow rate would otherwise let
 * the return run under the player's thumb. */
static long ModernCameraAxisOut(const MODERN_CAMERA_CFG* cfg, long axis)
{
	return (axis > cfg->dead || axis < -cfg->dead) ? 1 : 0;
}

/* One axis of the stick as a rate of turn: the axis IS the rate, scaled so
 * that full deflection is the whole constant. The clamp is not cosmetic - a
 * diagonal reads about 181 in stick units (the two axes are not scaled as a
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
 * what is left. Returns the signed step it took, which MODERNLOG prints as
 * cam_ret.
 *
 * What is left is at most 32768 and the rate is a fraction of 65536, so
 * mag * rate stays under what a signed long holds for every rate below
 * MODERN_CAMERA_RETURN_ONE. At the full fraction or above the answer is "all
 * of it" and no multiply is needed, hence the guard. */
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

/* The camera speed slider, as one multiply. The options row lives in
 * specific\LoadSave.cpp and the stored value in tomb5; this function is the
 * whole of what the number means, so that the options page, the registry and
 * the test all ask one place.
 *
 * Linear between the two ends, in the orbit's units, divided by the slider's
 * top so the arithmetic is exact at the default: 364 + 40 * 1820 / 100 =
 * 1092. The pitch is half the yaw.
 *
 * The clamp matters: the value arrives from the registry, which a player can
 * edit to hold 5000, and an unclamped rate would trip the 60 fps cut
 * detection on the orbit's own steps. */
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

/* The mouse's gain for a sensitivity of 0..100, in MODERN_CAMERA_MOUSE_ONE-ths
 * of a unit per count: 32 * 4^((s - 50) / 50) units, which is
 * 16384 * 2^((s - 50) / 25) in this fixed point (the header says why those
 * numbers). The power is split into whole doublings and a remainder of 0..24
 * twenty-fifths: the doublings are shifts, and the remainder is the table
 * below, round(4096 * 2^(r / 25)). So the gain is exact at 0, 25, 50, 75 and
 * 100 - 4096, 8192, 16384, 32768 and 65536, that is 8, 16, 32, 64 and 128
 * units a count - and doubles exactly every 25 steps, because the lowest end
 * is the table itself and every other end a left shift of it.
 *
 * A value out of range - the registry is edited by hand - is taken to the
 * nearer end, like the camera speed. */
long ModernCameraMouseGain(long sensitivity)
{
	static const long pow25[25] =
	{
		4096, 4211, 4330, 4451, 4576, 4705, 4837, 4973, 5113, 5257, 5405, 5557, 5713,
		5873, 6039, 6208, 6383, 6562, 6747, 6937, 7132, 7332, 7538, 7750, 7968
	};
	long s, k, r, g;

	s = sensitivity;

	if (s < 0)
		s = 0;
	else if (s > MODERN_CAMERA_MOUSE_SENS_FULL)
		s = MODERN_CAMERA_MOUSE_SENS_FULL;

	/* s - 50 = 25 * k + r with r in 0..24, so k is -2..2: a floor division,
	 * written on the shifted value so that no negative is divided. */
	k = s / 25 - 2;
	r = s % 25;

	/* The table is a quarter of the middle gain, 4096 of 16384, times
	 * 2^(r / 25); k + 2 is 0..4, so the gain is the table shifted left by that
	 * many doublings, with nothing shifted out. */
	static_assert(MODERN_CAMERA_MOUSE_GAIN * MODERN_CAMERA_MOUSE_ONE == 4096 << 2,
		"the table's 4096 is a quarter of the middle gain");

	g = pow25[r];

	return g << (k + 2);
}

/* One tick of mouse counts as the orbit's two steps. dx is to the right and dy
 * towards the player, as Raw Input reports them. The yaw follows dx the way
 * the stick's x axis does. The pitch follows dy the way the stick's y axis
 * does: the stick reads forward-negative and ModernCameraUpdate subtracts it,
 * so a mouse pushed away from the player, dy negative, looks up as a stick
 * pushed forward does; `invert` flips the pitch alone. The stick's own
 * invert_pitch is a different setting and does not reach this.
 *
 * Counts times gain is taken whole and divided once, truncating toward zero,
 * so a count to the left is exactly the negative of a count to the right. */
void ModernCameraMouseSteps(long dx, long dy, long sensitivity, long invert, long* yaw, long* pitch)
{
	const long gain = ModernCameraMouseGain(sensitivity);
	long y, p;

	if (dx > MODERN_CAMERA_MOUSE_COUNTS_MAX)
		dx = MODERN_CAMERA_MOUSE_COUNTS_MAX;
	else if (dx < -MODERN_CAMERA_MOUSE_COUNTS_MAX)
		dx = -MODERN_CAMERA_MOUSE_COUNTS_MAX;

	if (dy > MODERN_CAMERA_MOUSE_COUNTS_MAX)
		dy = MODERN_CAMERA_MOUSE_COUNTS_MAX;
	else if (dy < -MODERN_CAMERA_MOUSE_COUNTS_MAX)
		dy = -MODERN_CAMERA_MOUSE_COUNTS_MAX;

	y = dx * gain / MODERN_CAMERA_MOUSE_ONE;
	p = dy * gain / MODERN_CAMERA_MOUSE_ONE;

	if (!invert)
		p = -p;

	if (y > MODERN_CAMERA_MOUSE_STEP_MAX)
		y = MODERN_CAMERA_MOUSE_STEP_MAX;
	else if (y < -MODERN_CAMERA_MOUSE_STEP_MAX)
		y = -MODERN_CAMERA_MOUSE_STEP_MAX;

	if (p > MODERN_CAMERA_MOUSE_STEP_MAX)
		p = MODERN_CAMERA_MOUSE_STEP_MAX;
	else if (p < -MODERN_CAMERA_MOUSE_STEP_MAX)
		p = -MODERN_CAMERA_MOUSE_STEP_MAX;

	if (yaw)
		*yaw = y;

	if (pitch)
		*pitch = p;
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
	cfg->carry_flip = MODERN_CAMERA_CARRY_FLIP;
	cfg->carry_ticks = MODERN_CAMERA_CARRY_TICKS;
}

/* How much of a swing has been paid after `tick` of cfg->carry_ticks, by the
 * eased profile 3u^2 - 2u^3: nothing at 0, the whole at the last tick, and
 * steepest in the middle. It is the cumulative figure and not a step, because
 * a sum of rounded steps does not add up to the total - the caller takes the
 * difference of two of these and the rounding cancels.
 *
 * The profile is computed in 4096ths first and the total scaled by it second,
 * so the fraction is at most one whole (4096) and a swing at most a full turn.
 *
 * The profile needs 64 bits: the numerator 3 * n^3 * 4096 passes 2^31 at
 * n = 56, and a 32-bit product would give a negative fraction and a swing
 * running backwards. ModernTest's camera_carry() sweeps carry_ticks to 120. */
long ModernCameraCarryStep(const MODERN_CAMERA_CFG* cfg, long total, long tick)
{
	MODERN_CAMERA_CFG spare_cfg;
	long n, u;

	if (!cfg)
	{
		ModernCameraDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	n = cfg->carry_ticks;

	if (n <= 0 || tick <= 0)
		return 0;

	if (tick >= n)
		return total;

	u = (long)(((__int64)tick * tick * (3 * n - 2 * tick) * MODERN_CAMERA_CARRY_ONE) /
		((__int64)n * n * n));

	return total * u / MODERN_CAMERA_CARRY_ONE;
}

/* Her heading flipped, and the camera owes her a swing. The engine calls this
 * with the heading change of the tick whenever an animation made it - a roll,
 * a jump twist, an underwater roll - and never for a turn the stick asked for.
 * Under the threshold nothing happens, so an over-eager caller costs only the
 * comparison.
 *
 * The swing is the flip itself, which puts her offset back where it was: the
 * offset is world yaw minus her heading, so a heading that moved by f needs a
 * world yaw that moves by f. Both directions end on the same world angle; the
 * swing goes to the side the camera already leans to, the shorter way for the
 * picture. A camera dead behind her (offset 0) has no lean and takes the
 * right, a fixed side.
 *
 * A second flip while one is still running (two twists in a row) adds what is
 * left to the new swing and restarts the profile; queueing it would leave the
 * camera arriving behind her a second after she landed. */
void ModernCameraCarry(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state, long flip)
{
	MODERN_CAMERA_CFG spare_cfg;
	long s, want;

	if (!state)
		return;

	if (!cfg)
	{
		ModernCameraDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	if (cfg->carry_ticks <= 0)
		return;

	s = ModernWrap(flip);

	if (ModernCameraAbs(s) < cfg->carry_flip)
		return;

	if (state->offset_yaw >= 0)
		want = (s >= 0) ? s : s + 65536;
	else
		want = (s <= 0) ? s : s - 65536;

	state->carry_total = state->carry + want;
	state->carry = state->carry_total;
	state->carry_tick = 0;
}

/* One coordinate of the eye, pulled `step` of `steps` along the ray back
 * toward her.
 *
 * step == steps is the eye where it stands, step == 0 is the target itself,
 * and everything between is the straight line joining them. It is a function
 * rather than three lines in game\camera.cpp because the yaw the player sees
 * is only preserved while x and z move by the same fraction, so the fraction
 * belongs in one place where Preparation\Tools\Diagnose\ModernTest can
 * measure what its truncation costs (camera_pull). Moving the two coordinates
 * by different amounts - as CameraCollisionBounds does when it snaps wx and wz
 * onto the sector grid one at a time - turns the view.
 *
 * The multiplication comes first so that the fraction is taken of the whole
 * distance; (eye - target) is at most a few thousand units and steps a
 * handful, so the product is nowhere near a long's limit. The division
 * truncates toward zero, under a unit either way. */
long ModernCameraPullAxis(long target, long eye, long step, long steps)
{
	if (steps <= 0 || step >= steps)
		return eye;

	if (step <= 0)
		return target;

	return target + (eye - target) * step / steps;
}

/* One point of the chord, as the interpolator blends it. */
static void ModernChordPoint(const long from[3], const long to[3], float alpha, long out[3])
{
	for (int i = 0; i < 3; i++)
		out[i] = InterpLerpLong(from[i], to[i], alpha);
}

static long ModernChordTest(const long from[3], const long to[3], float alpha,
	MODERN_CHORD_OPEN open, void* ctx, long* tests)
{
	long p[3];

	ModernChordPoint(from, to, alpha, p);
	(*tests)++;
	return open(ctx, p[0], p[1], p[2]);
}

/* The chord search (see the header). Outward first, a step at a time on both
 * sides at once, so the first open sample is the nearest at the scan's
 * resolution whichever side it is on; then each side that found one is
 * halved between its last closed sample and its open one, keeping the open
 * end, so the answer is always a point the test passed. A scan that reached
 * an end tests the end itself: a tick eye is a point of the chord too.
 *
 * Nearest in the blend factor is nearest along the chord, because the chord
 * is straight. Of two answers at the same distance the one toward `to`, the
 * newer tick eye, is taken. */
long ModernCameraChordClamp(const long from[3], const long to[3], float alpha,
	MODERN_CHORD_OPEN open, void* ctx, float* out_alpha, long out[3], long* tests)
{
	const float step = 1.0F / (float)MODERN_CAMERA_CHORD_SCAN;
	float lo_in = alpha, hi_in = alpha, lo_ok = 0.0F, hi_ok = 0.0F, pick;
	long lo_found = 0, hi_found = 0;
	long lo_done = (alpha <= 0.0F) ? 1 : 0;
	long hi_done = (alpha >= 1.0F) ? 1 : 0;

	*tests = 0;

	if (ModernChordTest(from, to, alpha, open, ctx, tests))
	{
		*out_alpha = alpha;
		ModernChordPoint(from, to, alpha, out);
		return 1;
	}

	for (long k = 1; k <= MODERN_CAMERA_CHORD_SCAN; k++)
	{
		if (!lo_done)
		{
			float a = alpha - (float)k * step;

			if (a <= 0.0F)
			{
				a = 0.0F;
				lo_done = 1;
			}

			if (ModernChordTest(from, to, a, open, ctx, tests))
			{
				lo_ok = a;
				lo_found = 1;
			}
			else
				lo_in = a;
		}

		if (!hi_done)
		{
			float a = alpha + (float)k * step;

			if (a >= 1.0F)
			{
				a = 1.0F;
				hi_done = 1;
			}

			if (ModernChordTest(from, to, a, open, ctx, tests))
			{
				hi_ok = a;
				hi_found = 1;
			}
			else
				hi_in = a;
		}

		if (lo_found || hi_found || (lo_done && hi_done))
			break;
	}

	if (!lo_found && !hi_found)
		return 0;

	for (long i = 0; lo_found && i < MODERN_CAMERA_CHORD_BISECT; i++)
	{
		const float mid = (lo_ok + lo_in) * 0.5F;

		if (ModernChordTest(from, to, mid, open, ctx, tests))
			lo_ok = mid;
		else
			lo_in = mid;
	}

	for (long i = 0; hi_found && i < MODERN_CAMERA_CHORD_BISECT; i++)
	{
		const float mid = (hi_ok + hi_in) * 0.5F;

		if (ModernChordTest(from, to, mid, open, ctx, tests))
			hi_ok = mid;
		else
			hi_in = mid;
	}

	if (!lo_found)
		pick = hi_ok;
	else if (!hi_found)
		pick = lo_ok;
	else
		pick = (alpha - lo_ok < hi_ok - alpha) ? lo_ok : hi_ok;

	*out_alpha = pick;
	ModernChordPoint(from, to, pick, out);
	return 1;
}

/* The two rooms a chord point is asked from, for the search above. */
struct MODERN_CHORD_ROOMS
{
	MODERN_EYE_OPEN open;
	void* ctx;
	short cur_room;
	short prev_room;	// -1 when there is none, or when it is cur_room
};

static long ModernChordOpenFromRooms(void* ctx, long x, long y, long z)
{
	const MODERN_CHORD_ROOMS* rooms = (const MODERN_CHORD_ROOMS*)ctx;
	short at;

	if (rooms->open(rooms->ctx, x, y, z, rooms->cur_room, &at))
		return 1;

	return (rooms->prev_room >= 0 && rooms->open(rooms->ctx, x, y, z, rooms->prev_room, &at)) ? 1 : 0;
}

/* The order of the header. The tick's own room is asked first because it is
 * the room the walk starts in today, so a frame whose eye opens there is
 * drawn exactly as before. The previous tick's room comes next because the
 * blended eye starts beside the previous tick eye: on the first frames after
 * a tick the new room is applied to an eye that has not reached it (ANDREA1,
 * room 40's east door: the logic room is already 225 while the eye still
 * stands in open space of room 40, and GetFloor from 225 reads 225's corner
 * wall sector there). Only an eye that neither room holds is moved. */
void ModernCameraDrawnEye(const long prev[3], short prev_room, const long cur[3], short cur_room,
	float alpha, MODERN_EYE_OPEN open, void* ctx, MODERN_EYE_FIX* out)
{
	MODERN_CHORD_ROOMS rooms;
	long p[3], q[3], chord_tests;
	float got;
	short at, at_prev;

	ModernChordPoint(prev, cur, alpha, p);

	out->fix = MODERN_EYE_FIX_NONE;
	out->alpha = alpha;
	out->x = p[0];
	out->y = p[1];
	out->z = p[2];
	out->tests = 1;

	at = cur_room;

	if (open(ctx, p[0], p[1], p[2], cur_room, &at))
	{
		out->room = at;
		return;
	}

	// The room the walk starts in whenever nothing better is found: the one
	// GetFloor reached from the tick's room, as on every frame before.
	out->room = at;

	rooms.open = open;
	rooms.ctx = ctx;
	rooms.cur_room = cur_room;
	rooms.prev_room = (prev_room >= 0 && prev_room != cur_room) ? prev_room : (short)-1;

	if (rooms.prev_room >= 0)
	{
		out->tests++;
		at_prev = rooms.prev_room;

		if (open(ctx, p[0], p[1], p[2], rooms.prev_room, &at_prev))
		{
			out->fix = MODERN_EYE_FIX_PREVROOM;
			out->room = at_prev;
			return;
		}
	}

	if (!ModernCameraChordClamp(prev, cur, alpha, ModernChordOpenFromRooms, &rooms, &got, q,
		&chord_tests))
	{
		out->tests += chord_tests;
		out->fix = MODERN_EYE_FIX_NOSKY;
		return;
	}

	out->tests += chord_tests;

	// The room of the point found, asked in the same order as the search did.
	out->tests++;
	at = cur_room;

	if (!open(ctx, q[0], q[1], q[2], cur_room, &at) && rooms.prev_room >= 0)
	{
		out->tests++;
		at = rooms.prev_room;
		open(ctx, q[0], q[1], q[2], rooms.prev_room, &at);
	}

	out->fix = ((q[0] == prev[0] && q[1] == prev[1] && q[2] == prev[2]) ||
		(q[0] == cur[0] && q[1] == cur[1] && q[2] == cur[2])) ?
		MODERN_EYE_FIX_TICK : MODERN_EYE_FIX_CHORD;
	out->alpha = got;
	out->x = q[0];
	out->y = q[1];
	out->z = q[2];
	out->room = at;
}

/* Behind her back, at the game's resting height.
 *
 * The offset goes to zero because zero is behind her, and the world yaw is
 * left unseated: this function has no Lara to ask, so the first update seats
 * it on the heading it is handed. The pitch is the elevation itself, so zero
 * would be dead level, ten degrees below where ChaseCamera puts an untouched
 * camera, and the value ChaseCamera reads as "unset".
 * MODERN_CAMERA_PITCH_HOME is that resting elevation; a cfg that moves
 * pitch_home elsewhere seats the state itself, as with ModernResetState,
 * which takes no cfg either. */
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
	state->carry = 0;
	state->carry_total = 0;
	state->carry_tick = 0;
	state->stick_step = 0;
	state->mouse_yaw = 0;
	state->mouse_pitch = 0;
}

/* One tick of the orbit: the stick, the swing, the return, in that order.
 *
 * `lara_heading` is her y_rot, the only thing that turns the world yaw into
 * the offset the engine's slot wants. It is handed in on every tick rather
 * than remembered, so the state cannot disagree with her.
 *
 * Her turning moves nothing here: the heading never touches the world yaw, so
 * a quarter turn under a resting stick leaves the camera where it stood, and
 * only the derived offset changes - by minus her turn, which keeps the view
 * still while she spins.
 *
 * `wall_clipped` means geometry took the eye off the ideal this tick. The
 * camera keeps the dialled angle and loses distance instead, so the flag only
 * speeds up the return while it lasts.
 *
 * `lara_moving` is read by the return alone: standing, the camera is kept;
 * moving, it falls back behind her. The return also stands still while the
 * player steers, so the camera never pulls against the thumb holding it. */
void ModernCameraUpdate(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state,
	long rs_x, long rs_y, long lara_heading, long lara_moving, long wall_clipped,
	short* out_yaw_offset, short* out_pitch)
{
	long heading;
	long world;
	long pitch;
	long rate;
	long swinging = 0;

	if (!cfg || !state)
		return;

	heading = ModernWrap(lara_heading);

	/* The seating happens here because the reset has no Lara to ask: whatever
	 * offset the state holds becomes an offset from this heading, once. After
	 * that the world yaw is the state and the offset only an output of it. */
	if (!state->seated)
	{
		state->world_yaw = ModernWrap(heading + state->offset_yaw);
		state->seated = 1;
	}

	world = state->world_yaw;
	pitch = state->offset_pitch;
	state->wall = wall_clipped ? 1 : 0;
	state->ret = 0;

	/* 1. The stick. The yaw wraps - 360 degrees either way - and the pitch
	 * clamps, because over the cap the camera sits on her own axis, where
	 * nothing measured from the eye has a direction left
	 * (MODERN_CAMERA_PITCH_MIN).
	 *
	 * The mouse is the same rule with a second input: its steps are added
	 * beside the stick's rate terms, and a nonzero step is steering exactly as
	 * a stick out of its rest is, so it stands the return still and cancels a
	 * swing below. */
	state->steering = (ModernCameraAxisOut(cfg, rs_x) || ModernCameraAxisOut(cfg, rs_y) ||
		state->mouse_yaw || state->mouse_pitch) ? 1 : 0;

	/* The player's own yaw step is kept here, where it is a term and not yet a
	 * difference of two angles, before 1b and 2 below, so the swing and the
	 * return cannot get into it: game\modern_state.cpp adds exactly this to
	 * her held course, so that only a camera the player turns takes her course
	 * with it - by the stick or by the mouse alike. game\camera.cpp's
	 * modern_cam_lead_yaw is a different quantity - the world yaw's whole step
	 * less the return, swing included - which is right for leading the eye and
	 * wrong here. */
	state->stick_step = ModernWrap(ModernCameraRate(cfg, rs_x, cfg->yaw_rate) + state->mouse_yaw);

	world = ModernWrap(world + state->stick_step);

	if (cfg->invert_pitch)
		pitch += ModernCameraRate(cfg, rs_y, cfg->pitch_rate);
	else
		pitch -= ModernCameraRate(cfg, rs_y, cfg->pitch_rate);

	/* The mouse's pitch step arrives signed by its own invert, so it is only
	 * added. Both steps are consumed: the next update without a fresh
	 * hand-over sees nothing. */
	pitch += state->mouse_pitch;
	state->mouse_yaw = 0;
	state->mouse_pitch = 0;

	/* 1b. The swing an animation about-turn owes, paid out here because
	 * ModernCameraCarry only seats it - the world yaw is this function's to
	 * move.
	 *
	 * The stick cancels it the moment it is pushed: the right stick is the
	 * player's camera, and a swing paying out under a thumb that has taken the
	 * camera back would fight it. What is left is dropped, not remembered; the
	 * offset the player dials now is the one wanted.
	 *
	 * The return stands still while the swing runs, as it does while the
	 * player steers: two rules moving the same angle at once would end the
	 * swing somewhere other than promised. It resumes on the tick after the
	 * last. */
	if (state->steering)
	{
		state->carry = 0;
		state->carry_total = 0;
		state->carry_tick = 0;
	}
	else if (state->carry_total && cfg->carry_ticks > 0)
	{
		const long paid = ModernCameraCarryStep(cfg, state->carry_total, state->carry_tick);

		swinging = 1;

		state->carry_tick++;
		world = ModernWrap(world +
			(ModernCameraCarryStep(cfg, state->carry_total, state->carry_tick) - paid));
		state->carry = state->carry_total -
			ModernCameraCarryStep(cfg, state->carry_total, state->carry_tick);

		if (state->carry_tick >= cfg->carry_ticks)
		{
			state->carry = 0;
			state->carry_total = 0;
			state->carry_tick = 0;
		}
	}

	/* 2. The return: the world yaw falls towards her heading. */
	if (lara_moving && !state->steering && !swinging)
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

/* Whether she is too close to draw. Only the comparison lives here, so that
 * MODERN_CAMERA_HIDE_DIST stays a number a test can hold to its origin; which
 * distance is measured, once per frame, and every path that draws her
 * (game\draw.cpp, specific\hd\hd_laradraw.cpp, the braid, the shadow) asking
 * the same question, is the engine's side. A hard cut and not a fade: the
 * Remaster's camera cuts her within a twelfth of a second. */
long ModernCameraHidesLara(const MODERN_CAMERA_CFG* cfg, long distance)
{
	if (!cfg)
		return 0;

	return (distance < cfg->hide_dist) ? 1 : 0;
}

/* The swimming attitude the orbit points at. Under water the right stick is a
 * 3D pointer Lara follows, so the water needs the orbit's pitch as a body
 * pitch, and the two differ: the chase camera sits pitch_home above her own
 * axis. ChaseCamera's own line gives the arithmetic - "camera.target_elevation
 * = -1820 + item->pos.x_rot" (game\camera.cpp) - so the attitude that belongs
 * to an elevation is elevation - home, and a camera left alone points at
 * level swimming.
 *
 * Not clamped here. The orbit's limits are the chase camera's and the water's
 * are LaraUnderWater's (both +-15470), but offset from one another by home, so
 * the top of the range is out of her reach by 1820 units; the water's own
 * move answers that with its own constants. */
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

/* Whether the tick that wrote `stamp` ran immediately before `now`.
 *
 * game\camera.cpp keeps two answers that are only valid while the orbit ticks
 * every tick - whether mgLOS clipped the eye, and the world angle the player
 * has dialled - and the orbit stands still through a flyby, a cutscene, first
 * person, the look key and the four rich2 sniper cameras. An answer carried
 * across such a break describes a camera that is no longer there, so each is
 * stamped with GlobalCounter when written and checked here when read.
 *
 * Exactly one, not at most one: game\control.cpp steps GlobalCounter once per
 * logic tick, and S_UpdateInput and CalculateCamera both run once inside that
 * tick body, so consecutive ticks are exactly one apart. Zero would mean the
 * same tick asking twice, which no caller does; accepting it would hide a
 * doubled call.
 *
 * The subtraction is done in the counter's own unsigned width, so a gap is
 * only mistaken for a neighbour if it is exactly 65,537 ticks long - 36
 * minutes of one unbroken sniper camera. It lives in this pure file so a test
 * can reach the edge and the two call sites cannot disagree. */
long ModernCameraTickFollows(unsigned short now, unsigned short stamp)
{
	return ((unsigned short)(now - stamp) == 1) ? 1 : 0;
}

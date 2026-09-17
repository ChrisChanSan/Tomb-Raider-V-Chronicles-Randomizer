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
#include "modern_move.h"
#include <math.h>	// atan2, floor, sqrt - the stick's direction and its length

/* The arithmetic of the modern control (#161, round 161a, step 1). The
 * header says what this is and where the design comes from; this file is the
 * decision tree and nothing else. No engine header, no global, no side
 * effect outside the two structs the caller hands in. */

/* THE ARC TANGENT, AND WHY IT IS NOT phd_atan.
 *
 * The engine's own is phd_atan (specific\3dmath.cpp), and the CAMERA HEADING
 * this file works against is built with it: game\modern_state.cpp computes
 * phd_atan(camera.target.z - camera.pos.z, camera.target.x - camera.pos.x)
 * and hands the result in. So the number that has to agree with the engine's
 * own angles is the engine's own.
 *
 * It cannot be called from HERE. specific\3dmath.h pulls global\types.h
 * behind it and that needs windows.h and d3d9.h - including it would stop
 * this file compiling outside the engine, and with it ModernTest, which is
 * the whole point of keeping the translation in a file of its own.
 *
 * So the stick's own direction is taken with the C library's atan2, scaled to
 * the same 16-bit units. Measured over all 131768 integer stick pairs in
 * -181..181 (full deflection on a diagonal is 181), the largest disagreement
 * between this function and (short)phd_atan(z, x) is 5 of 65536 - 0.03
 * degrees, and that is the tan table's own resolution, not an error. The
 * smallest threshold anything below compares against is steer_step, 384 units
 * or 2.1 degrees, so the difference cannot reach a decision. */
short ModernAngle(long x, long z)
{
	if (!x && !z)
		return 0;

	return ModernWrap((long)floor(atan2((double)x, (double)z) * 32768.0 / 3.14159265358979323846 + 0.5));
}

/* Fold an angle into a signed short, i.e. into the shorter of the two ways
 * round. Written out rather than left to a cast: a cast of a value above
 * 32767 to short is implementation-defined before C++20, and this file is
 * compiled by two different build scripts. */
short ModernWrap(long angle)
{
	const unsigned long a = (unsigned long)angle & 0xFFFFul;

	return (a >= 0x8000ul) ? (short)((long)a - 0x10000L) : (short)a;
}

/* An angle's magnitude has to be taken as a long. |delta| where delta is
 * -32768 does not fit in a short, and that is exactly half a turn - the case
 * the back sector compares against. */
static long ModernAbs(long v)
{
	return (v < 0) ? -v : v;
}

/* How far the stick is out, 0..stick_full. The clamp is TROne's and changes
 * no decision - a diagonal reaches 181 in our units and every threshold below
 * is under 128 - but it keeps `mag` a number the log can be read against. */
static long ModernMagnitude(const MODERN_CFG* cfg, long x, long y)
{
	const double m = sqrt((double)(x * x + y * y));

	return (m > (double)cfg->stick_full) ? cfg->stick_full : (long)m;
}

/* A world course, in LARA'S OWN frame, as one direction bit. Four sectors:
 * forward, back, and the two sides. `sector` is the boundary to the front and
 * to the back; `step_bits` picks the side step instead of the turn, which is
 * what aiming wants. */
static long ModernRelativeBits(short rel, long sector, long step_bits)
{
	const long a = ModernAbs(rel);

	if (a <= sector)
		return MODERN_IN_FORWARD;

	if (a >= 0x8000L - sector)
		return MODERN_IN_BACK;

	if (rel > 0)
		return step_bits ? MODERN_IN_RSTEP : MODERN_IN_RIGHT;

	return step_bits ? MODERN_IN_LSTEP : MODERN_IN_LEFT;
}

/* The stick came back to rest, or this tick is not ours: forget the held
 * course so that the next push seats a fresh one. The blocked counter is NOT
 * cleared here - it is the caller's, it is measured from Lara's position and
 * it has its own rule in game\modern_state.cpp. */
static void ModernRelease(MODERN_STATE* state)
{
	state->have_ref = 0;
	state->spin = 0;
	state->aligned = 0;
	state->aiming = 0;
}

void ModernDefaults(MODERN_CFG* cfg)
{
	if (!cfg)
		return;

	cfg->dead = MODERN_DEAD;
	cfg->walk_below = MODERN_WALK_BELOW;
	cfg->aim_window = MODERN_AIM_WINDOW;
	cfg->aligned_factor = MODERN_ALIGNED_FACTOR;
	cfg->spin_enter = MODERN_SPIN_ENTER;
	cfg->spin_exit = MODERN_SPIN_EXIT;
	cfg->stand_spin = MODERN_STAND_SPIN;
	cfg->steer_step = MODERN_STEER_STEP;
	cfg->recenter = MODERN_RECENTER;
	cfg->aim_sector = MODERN_AIM_SECTOR;
	cfg->block_frames = MODERN_BLOCK_FRAMES;
	cfg->block_eps = MODERN_BLOCK_EPS;
	cfg->turn_rate = MODERN_TURN_RATE;
	cfg->direct_turn = MODERN_DIRECT_TURN;
	cfg->snap_delta = MODERN_SNAP_DELTA;
	cfg->snap_forward = MODERN_SNAP_FORWARD;
	cfg->stick_full = MODERN_STICK_FULL;
	cfg->hold_course = MODERN_HOLD_COURSE;
	cfg->back_turns = MODERN_BACK_TURNS;
	cfg->aim_strafe = MODERN_AIM_STRAFE;
	cfg->jump_dir = MODERN_JUMP_DIR;
	cfg->block_turn = MODERN_BLOCK_TURN;
}

void ModernResetState(MODERN_STATE* state)
{
	if (!state)
		return;

	state->have_ref = 0;
	state->ref = 0;
	state->course = 0;
	state->last_off = 0;
	state->spin = 0;
	state->aligned = 0;
	state->aiming = 0;
	state->blocked_count = 0;
	state->last_mode = MODERN_MODE_OFF;
}

/* THE STATES THAT STAY CLASSIC - ROUND 161a STEP 3 WRITES THIS TABLE.
 *
 * It belongs in this file and it is declared in the header because the tree
 * below asks it first, but its content is a measurement of its own: an
 * explicit switch over 0..138, every classic entry carrying the routine name
 * from game\lara.cpp's lara_control_routines[] as its reason, the water
 * states marked classic with "modern in A2 (camera pitch)", and the harmless
 * ones named so that nobody removes them later. Design sections 1.7 and 6.1.
 *
 * Until then it answers "modern" for every state - which is the classic case
 * switched off, not a wrong answer waiting to be found in the game: nothing
 * in the engine calls ModernTranslate at all until step 4 puts the seam in,
 * and step 3 comes before step 4. */
long ModernStateIsClassic(short anim_state)
{
	(void)anim_state;

	return 0;
}

/* Standing still, or turning on the spot. Here a turn is ONLY reachable
 * without IN_FORWARD (game\lara.cpp - lara_as_stop makes AS_TURN_L/AS_TURN_R
 * out of IN_LEFT/IN_RIGHT and a simultaneous IN_FORWARD overwrites the goal
 * with run or walk; lara_as_turn_r does the same), so the spin threshold is
 * the lower stand_spin in these six states and the running one everywhere
 * else.
 *   2 AS_STOP   6 AS_TURN_R   7 AS_TURN_L
 *  12 AS_SPLAT (she walked into a wall)   14 AS_LAND   20 AS_FASTTURN */
long ModernStateIsGroundIdle(short anim_state)
{
	return (anim_state == 2 || anim_state == 6 || anim_state == 7 ||
		anim_state == 12 || anim_state == 14 || anim_state == 20) ? 1 : 0;
}

/* Can she jump from here, and does a direction bit therefore mean "which
 * jump" instead of "which way"? 2 AS_STOP, 14 AS_LAND, and above all 15
 * AS_COMPRESS - the crouch before the jump, where lara_as_compress reads the
 * direction bits to pick the forward jump, the back flip or a side flip. */
long ModernStateIsStanding(short anim_state)
{
	return (anim_state == 2 || anim_state == 14 || anim_state == 15) ? 1 : 0;
}

/* THE WEAPON HALF (combat mode), design section 1.5. A1 only ADDS it -
 * nothing calls it yet. Workflow B wires it at the second seam of
 * specific\input.cpp and puts tomb5.combat_hold on its first argument.
 *
 * Two truths come in. `ready` is a weapon out and shootable (gun_status ==
 * LG_READY, no torch), `holstered` is the hands empty (LG_NO_ARMS). BOTH
 * false means the draw or holster animation is running, or the hands are busy
 * on a ladder or in the water - and then no IN_DRAW goes out in either mode,
 * so an impossible request cannot queue up and fire a second later.
 *
 * Toggle hands the bits back untouched, and that is not a shortcut: our draw
 * key already IS the toggle, because game\larafire.cpp turns any IN_DRAW at
 * LG_READY into LG_UNDRAW_GUNS. The original behaviour is the Toggle
 * behaviour, bit for bit, which is why Toggle is the default.
 *
 * Hold asks for the one transition that is missing: draw while the key is
 * down and the guns are away, holster when the key comes up and they are
 * out. A held flag alone would draw and holster without end for the same
 * reason - hence the two conditions and not one. */
long ModernWeaponBits(long combat_hold, long ready, long holstered, long held, long bits)
{
	if (!combat_hold)
		return bits;

	bits &= ~(long)MODERN_IN_DRAW;

	if ((held && holstered) || (!held && ready))
		bits |= MODERN_IN_DRAW;

	return bits;
}

/* THE TRANSLATION (design sections 1.1 to 1.3).
 *
 * stick_x is right-positive and stick_y is FORWARD-NEGATIVE - that is how
 * specific\dxshell.cpp hands the axes over ("joystick_read_y = -ly / 256")
 * and how specific\input.cpp reads them for the classic bits. So the stick's
 * own angle is taken from (stick_x, -stick_y).
 *
 * THE HELD COURSE is the heart of it, and it is the part that is not
 * obvious. The chase camera hangs rigidly behind Lara, so its heading IS her
 * heading; take it as the reference of every tick and she turns, the camera
 * follows, the target walks on, and she circles forever. TROne measured 600
 * frames of exactly that. So the state carries a WORLD course instead, and
 * the camera is only consulted when the player changes his mind:
 *   stick pushed from rest		course = camera + stick angle. This is the
 *								camera-relative part - the same stick means a
 *								different world heading under a different
 *								camera.
 *   stick held, angle moved
 *   less than steer_step		nothing. A thumb is never still.
 *   stick held, angle moved
 *   at least steer_step but
 *   under recenter				the course turns by exactly that much. The
 *								camera is NOT read, which is what makes a
 *								camera cut leave her path alone.
 *   stick angle moved at
 *   least recenter				a new direction was meant: seat the course on
 *								the camera of THIS tick.
 *   stick back at rest			forget it all; the next push starts fresh.
 *
 * The order of the tree below is the order of the design and it matters: the
 * classic states get out before anything is remembered, the dead zone before
 * an angle is taken from noise, aiming before the course is used (aiming does
 * not hold a course at all - the game turns her onto her target itself and a
 * held course would be a feedback loop), the jump before the turn logic
 * (there the bits pick a jump, not a way), and the run case last. */
void ModernTranslate(const MODERN_CFG* cfg, MODERN_STATE* state, long stick_x, long stick_y,
	short camera_yaw, short lara_yaw, const MODERN_SITUATION* sit, MODERN_RESULT* out)
{
	MODERN_CFG spare_cfg;
	MODERN_STATE spare_state;
	MODERN_SITUATION no_sit;
	short offset;
	short target;
	short delta;
	long adelta;
	long window;
	long enter;
	long leave;
	long bits;

	if (!out)
		return;

	out->bits = 0;
	out->active = 0;
	out->target = lara_yaw;
	out->delta = 0;
	out->mag = 0;
	out->reference = camera_yaw;
	out->mode = MODERN_MODE_OFF;
	out->blocked = 0;
	out->turning = 0;

	/* All three may be left out. A caller that hands in nothing gets the
	 * defaults, a scratch state and plain running on land - which is what the
	 * tests mean when they say "no situation". */
	if (!cfg)
	{
		ModernDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	if (!state)
	{
		ModernResetState(&spare_state);
		state = &spare_state;
	}

	if (!sit)
	{
		no_sit.anim_state = 0;
		no_sit.classic = 0;
		no_sit.drawn = 0;
		no_sit.action = 0;
		no_sit.jump = 0;
		no_sit.blocked = 0;
		sit = &no_sit;
	}

	out->mag = ModernMagnitude(cfg, stick_x, stick_y);

	/* 0. Swimming, hanging, the ladder, the monkey swing, a block, a vehicle,
	 * the object states: there a direction bit does not mean "where do I go",
	 * so the classic input stays and we report that we did nothing. The seam
	 * leaves the base bits standing when active is 0. */
	if (sit->classic || ModernStateIsClassic(sit->anim_state))
	{
		ModernRelease(state);
		state->last_mode = out->mode;
		return;
	}

	/* 1. The dead zone. The stick at rest is asked for by name and not left
	 * to the threshold: ours starts at 0 because the hardware zone is already
	 * applied one step earlier, and "mag < 0" is never true - without this
	 * line a centred stick would read as a push straight forward. */
	if ((!stick_x && !stick_y) || out->mag < cfg->dead)
	{
		ModernRelease(state);
		state->last_mode = out->mode;
		return;
	}

	/* 2. The stick's own direction, and the held world course. */
	offset = ModernAngle(stick_x, -stick_y);

	if (!state->have_ref)
	{
		state->ref = camera_yaw;
		state->course = ModernWrap((long)camera_yaw + offset);
		state->last_off = offset;
		state->have_ref = 1;
	}
	else if (!cfg->hold_course)
	{
		/* The course switched off: the camera of every tick is the
		 * reference. This is the version that circles, and it is here so
		 * that a play test can show the difference in one setting rather
		 * than in an argument. */
		state->ref = camera_yaw;
		state->course = ModernWrap((long)camera_yaw + offset);
		state->last_off = offset;
	}
	else
	{
		const short d_off = ModernWrap((long)offset - state->last_off);
		const long a_off = ModernAbs(d_off);

		if (a_off >= cfg->recenter)
		{
			state->ref = camera_yaw;
			state->course = ModernWrap((long)camera_yaw + offset);
			state->last_off = offset;
		}
		else if (a_off >= cfg->steer_step)
		{
			state->course = ModernWrap((long)state->course + d_off);
			state->last_off = offset;
		}
	}

	target = state->course;
	out->reference = state->ref;

	/* 3. Aiming: a weapon ready AND the action key down. Letting go gives the
	 * turn back at once, so a shot in passing does not lock the movement for
	 * longer than the shot. */
	state->aiming = (cfg->aim_strafe && sit->drawn && sit->action) ? 1 : 0;

	if (state->aiming)
	{
		/* No held course here. While she aims, the game turns her onto her
		 * target itself; a course of ours would fight it. So the direction is
		 * read fresh against the camera every tick and comes out as a side
		 * step - the stick pushes her AROUND the target. */
		target = ModernWrap((long)camera_yaw + offset);
		state->course = target;
		state->last_off = offset;
		state->ref = camera_yaw;

		delta = ModernWrap((long)target - lara_yaw);
		bits = ModernRelativeBits(delta, cfg->aim_sector, 1);

		if (cfg->walk_below > 0 && out->mag < cfg->walk_below)
			bits |= MODERN_IN_WALK;

		state->spin = 0;
		out->bits = bits;
		out->active = 1;
		out->target = target;
		out->delta = delta;
		out->reference = state->ref;
		out->mode = MODERN_MODE_AIM;
		state->last_mode = out->mode;
		return;
	}

	delta = ModernWrap((long)target - lara_yaw);
	adelta = ModernAbs(delta);

	/* 4. The directional jump out of a stand. In AS_COMPRESS the direction
	 * bits choose the jump - forward, the back flip, a side flip - and
	 * nothing turns at all. The jump key may already be let go by then:
	 * AS_COMPRESS lasts several ticks and a tap is only in the first of them,
	 * so the state alone is enough to keep this branch. Without that, the
	 * ticks after the tap would fall into the run case, set IN_FORWARD, and
	 * turn every back flip into a forward jump. */
	if (cfg->jump_dir && (sit->jump || sit->anim_state == 15) &&
		ModernStateIsStanding(sit->anim_state))
	{
		bits = ModernRelativeBits(delta, cfg->aim_sector, 0);

		state->spin = 0;
		out->bits = bits;
		out->active = 1;
		out->target = target;
		out->delta = delta;
		out->mode = MODERN_MODE_JUMP;
		state->last_mode = out->mode;
		return;
	}

	/* 5. Back on the stick as a step backwards instead of an about-turn -
	 * only when back_turns is switched off. */
	if (!cfg->back_turns && adelta >= 0x8000L - cfg->aim_sector)
	{
		bits = MODERN_IN_BACK;

		if (cfg->walk_below > 0 && out->mag < cfg->walk_below)
			bits |= MODERN_IN_WALK;

		state->spin = 0;
		out->bits = bits;
		out->active = 1;
		out->target = target;
		out->delta = delta;
		out->mode = MODERN_MODE_BACK;
		state->last_mode = out->mode;
		return;
	}

	/* 6. The normal case: turn, and run while turning. */
	enter = cfg->spin_enter;
	leave = cfg->spin_exit;

	if (cfg->stand_spin > 0 && ModernStateIsGroundIdle(sit->anim_state))
	{
		enter = cfg->stand_spin;

		if (leave >= enter)
			leave = enter / 2;
	}

	if (state->spin)
	{
		if (adelta <= leave)
			state->spin = 0;
	}
	else if (adelta >= enter)
	{
		state->spin = 1;
	}

	bits = 0;
	window = state->aligned ? cfg->aim_window * cfg->aligned_factor : cfg->aim_window;

	if (adelta > window)
	{
		bits |= (delta > 0) ? MODERN_IN_RIGHT : MODERN_IN_LEFT;
		state->aligned = 0;
		out->turning = 1;
	}
	else
	{
		state->aligned = 1;
	}

	/* She runs unless she is turning on the spot - otherwise a half turn
	 * would carry her a long way the wrong side first - and unless forward is
	 * going nowhere. The second half is the wall case: with something in the
	 * way, IN_FORWARD keeps the classic automaton in AS_STOP or AS_SPLAT and
	 * the turn never happens, so a blocked tick sends the turn bit ALONE and
	 * she turns in front of the wall exactly as she does in Tank. Once she is
	 * aligned the rule lapses - pressing against a wall is classic behaviour
	 * and not a control that has stopped answering. */
	if (cfg->block_turn && sit->blocked && out->turning)
		out->blocked = 1;

	if (!state->spin && !out->blocked)
		bits |= MODERN_IN_FORWARD;

	if (cfg->walk_below > 0 && out->mag < cfg->walk_below)
		bits |= MODERN_IN_WALK;

	out->bits = bits;
	out->active = 1;
	out->target = target;
	out->delta = delta;
	out->mode = MODERN_MODE_RUN;
	state->last_mode = out->mode;
}

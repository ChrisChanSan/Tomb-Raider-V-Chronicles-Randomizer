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

/* ModernTest checks the arithmetic of the modern control scheme without
 * running the game. The scheme is a pure function: stick direction plus a
 * remembered world heading in, the classic direction bits out. It is
 * compiled into this tool rather than copied into it - bmt.bat hands the
 * engine's own game\modern_move.cpp and game\modern_camera.cpp to the
 * compiler beside this file, exactly as InterpTest is built against
 * game\interpolate_math.h - so there is no twin to keep in step and a
 * change to the tree is a change this test sees. Build with bmt.bat, run
 * with no arguments.
 *
 * The design is TROne's, re-derived in our own voice (see
 * game\modern_move.h). The expectations below are the one part taken
 * across rather than re-invented: the 28 `modern.*` and 4 `combatmode.*`
 * cases of TROne's tests\test_controller.cpp, ported to our units and
 * named above each block with the TROne test they come from.
 *   TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931
 *
 * Units differ: TROne's stick is +-32767, ours arrives from
 * specific\dxshell.cpp as about +-128 (XInput /256) or +-125 (DirectInput).
 * Every stick number below goes through S() - TROne's own value, scaled -
 * so the port can be read against their file line by line. Angles,
 * thresholds and state numbers are the same units in both projects.
 *
 * Some of TROne's cases run through their glue - the per-tick state
 * machine, the blocked counter, the direct y_rot write - or through their
 * orbit camera, which are not all built here yet. Where such a case still
 * says something about the pure function, that half is ported and the
 * comment says which half; the rest are listed by name at the end of the
 * run under "not ours yet", so nothing goes missing silently.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../../../TOMB5 Code/TOMB5/game/modern_move.h"
#include "../../../TOMB5 Code/TOMB5/game/modern_camera.h"
/* The 60 fps cut detection's own thresholds and its own rule, so that
 * camera_camcut() measures the orbit against the tree rather than against a
 * number copied out of it. It costs nothing: this header includes stdlib.h
 * and math.h and no engine at all, which is why InterpTest is built against
 * it the same way. */
#include "../../../TOMB5 Code/TOMB5/game/interpolate_math.h"

static int g_fail = 0;
static int g_checks = 0;

static void ok(bool cond, const char* what, long got, long want)
{
	g_checks++;

	if (!cond)
	{
		g_fail++;
		printf("  FAIL %-62s got %ld, want %ld\n", what, got, want);
	}
}

/* TROne's stick value in ours. Their full deflection is 32767, ours is
 * MODERN_STICK_FULL - so 30000 ("push it over") becomes 117 and 16000 (half,
 * the walk threshold) becomes 62, which is MODERN_WALK_BELOW itself. The
 * original number stays in the comment of every call site. */
static long S(long trone)
{
	return trone * (long)MODERN_STICK_FULL / 32767L;
}

/* Angle magnitudes are taken as longs on purpose: |-32768| does not fit in a
 * short, and -32768 is exactly the half turn every back case lands on. */
static long absl(long v)
{
	return (v < 0) ? -v : v;
}

static long roundl2(double v)
{
	return (v < 0.0) ? -(long)floor(-v + 0.5) : (long)floor(v + 0.5);
}

/* One tick of the translation, with a config and a state the caller owns. */
static void tick(const MODERN_CFG* cfg, MODERN_STATE* state, long x, long y,
	short camera_yaw, short lara_yaw, const MODERN_SITUATION* sit, MODERN_RESULT* out)
{
	ModernTranslate(cfg, state, x, y, camera_yaw, lara_yaw, sit, out);
}

/* The stick back at rest. TROne write this as "a value inside the dead zone";
 * ours is the rest position itself, because MODERN_DEAD is 0 - the zone is
 * applied one step earlier, by ModernStickDeadZone at the pad seam, and
 * deadzone() below is where it is checked. */
static void release(const MODERN_CFG* cfg, MODERN_STATE* state)
{
	MODERN_RESULT out;

	ModernTranslate(cfg, state, 0, 0, 0, 0, 0, &out);
}

/* ------------------------------------------------------------- the angles -- */

static void angles()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_RESULT out;

	printf("angles - the convention every other number rests on\n");

	/* TROne: modern.angles_match_the_classic_convention. x = sin(course),
	 * z = cos(course): +Z is 0 and +X is 0x4000, which is the convention of
	 * item->pos.y_rot and of the engine's own phd_atan(z, x). */
	ok(ModernAngle(0, 1000) == 0, "angle(0, 1000) is +Z", ModernAngle(0, 1000), 0);
	ok(ModernAngle(1000, 0) == 0x4000, "angle(1000, 0) is +X",
		ModernAngle(1000, 0), 0x4000);
	ok(ModernAngle(-1000, 0) == -0x4000, "angle(-1000, 0) is -X",
		ModernAngle(-1000, 0), -0x4000);
	ok(absl(ModernAngle(0, -1000)) == 0x8000, "angle(0, -1000) is half a turn",
		absl(ModernAngle(0, -1000)), 0x8000);

	/* TROne: modern.diagonal_is_45_degrees. A diagonal has to be exactly 45
	 * degrees or a synthetic stick, such as WASD keys would make, sends Lara
	 * slightly askew on every diagonal. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	tick(&cfg, &state, S(32000), -S(32000), 0, 0, 0, &out);	/* TROne 32000/-32000 */
	ok(out.target == 0x2000, "forward + right is exactly 45 degrees", out.target, 0x2000);

	release(&cfg, &state);
	tick(&cfg, &state, -S(32000), -S(32000), 0, 0, 0, &out);
	ok(out.target == -0x2000, "forward + left is exactly -45 degrees", out.target, -0x2000);

	release(&cfg, &state);
	tick(&cfg, &state, S(32000), S(32000), 0, 0, 0, &out);
	ok(out.target == 0x6000, "back + right is exactly 135 degrees", out.target, 0x6000);

	/* Ours, not TROne's: the fold. A cast of anything above 32767 to short is
	 * implementation-defined before C++20 and this file and the engine are
	 * built by two different scripts, so the fold is written out - and here it
	 * is asserted at both edges and past a whole turn. */
	ok(ModernWrap(0x8000) == -32768, "wrap(0x8000) is the half turn", ModernWrap(0x8000), -32768);
	ok(ModernWrap(-0x8000) == -32768, "wrap(-0x8000) is the same half turn",
		ModernWrap(-0x8000), -32768);
	ok(ModernWrap(0x10000) == 0, "wrap(a whole turn) is zero", ModernWrap(0x10000), 0);
	ok(ModernWrap(0x14000) == 0x4000, "wrap(a whole turn + 0x4000) is 0x4000",
		ModernWrap(0x14000), 0x4000);
	ok(ModernAngle(0, 0) == 0, "a stick at rest has no angle", ModernAngle(0, 0), 0);

	/* TROne's other half of that test is trone_modern_camera_yaw, which falls
	 * back to Lara's own heading when the camera is degenerate (target sits on
	 * position) so that the frame behaves like Tank. Ours is computed with
	 * the engine's own phd_atan and handed in, so the equivalent statement
	 * here is what the translation does with it: a camera heading equal to
	 * Lara's makes a forward push mean "straight ahead", which is the classic
	 * reading of the stick. */
	release(&cfg, &state);
	tick(&cfg, &state, 0, -S(30000), 0x2000, 0x2000, 0, &out);
	ok(out.target == 0x2000, "a camera on Lara's own heading is Tank's forward",
		out.target, 0x2000);
	ok(out.bits == MODERN_IN_FORWARD, "...and forward alone comes out",
		out.bits, MODERN_IN_FORWARD);
}

/* ------------------------------------------------------- the frozen values -- */

static void bit_values()
{
	printf("bits and state numbers - the values that must never move\n");

	/* TROne: modern.shared_bit_values. The five bits their translation writes,
	 * frozen. Ours are mirrored by value from global\types.h's input_buttons
	 * (IN_FORWARD 0x1, IN_BACK 0x2, IN_LEFT 0x4, IN_RIGHT 0x8, IN_JUMP 0x10,
	 * IN_DRAW 0x20, IN_ACTION 0x40, IN_WALK 0x80, IN_LSTEP 0x400, IN_RSTEP
	 * 0x800) - this file cannot include that header, so game\modern_state.cpp
	 * carries the static_assert that says they still agree. This is the
	 * other half of that guard: the values as the translation knows them. */
	ok(MODERN_IN_FORWARD == 0x1, "IN_FORWARD", MODERN_IN_FORWARD, 0x1);
	ok(MODERN_IN_BACK == 0x2, "IN_BACK", MODERN_IN_BACK, 0x2);
	ok(MODERN_IN_LEFT == 0x4, "IN_LEFT", MODERN_IN_LEFT, 0x4);
	ok(MODERN_IN_RIGHT == 0x8, "IN_RIGHT", MODERN_IN_RIGHT, 0x8);
	ok(MODERN_IN_WALK == 0x80, "IN_WALK", MODERN_IN_WALK, 0x80);

	/* TROne: modern.shared_state_numbers, second half - the two bits the
	 * aiming strafe and the side step need, and the draw bit the weapon half
	 * hands back. TROne also freeze their JUMP (0x10) and ACTION (0x40)
	 * mirrors; ours are deliberately NOT mirrored, because the translation
	 * never writes them - it only ever READS "jump is down" and "action is
	 * down" out of the situation struct. */
	ok(MODERN_IN_LSTEP == 0x400, "IN_LSTEP", MODERN_IN_LSTEP, 0x400);
	ok(MODERN_IN_RSTEP == 0x800, "IN_RSTEP", MODERN_IN_RSTEP, 0x800);
	ok(MODERN_IN_DRAW == 0x20, "IN_DRAW", MODERN_IN_DRAW, 0x20);

	/* TROne: modern.classic_situations_report_inactive..., last assertion. The
	 * mask is what the seam clears before it writes a result, so it has
	 * to hold every bit the translation can produce and nothing else. IN_DRAW
	 * is deliberately outside it: the weapon half hands the bits back rather
	 * than replacing them. */
	ok(MODERN_MOVE_MASK == 0xC8F, "the move mask is exactly the seven movement bits",
		MODERN_MOVE_MASK, 0xC8F);
	ok((MODERN_MOVE_MASK & MODERN_IN_DRAW) == 0, "...and the draw bit is not in it",
		MODERN_MOVE_MASK & MODERN_IN_DRAW, 0);

	/* TROne: modern.shared_state_numbers, first half. 2 = AS_STOP,
	 * 15 = AS_COMPRESS (the crouch where the direction bits pick the jump),
	 * 1 = AS_RUN. */
	ok(ModernStateIsStanding(2) == 1, "AS_STOP stands", ModernStateIsStanding(2), 1);
	ok(ModernStateIsStanding(15) == 1, "AS_COMPRESS stands", ModernStateIsStanding(15), 1);
	ok(ModernStateIsStanding(14) == 1, "AS_LAND stands", ModernStateIsStanding(14), 1);
	ok(ModernStateIsStanding(1) == 0, "AS_RUN does not", ModernStateIsStanding(1), 0);

	/* The constants of the scheme, as ModernDefaults hands them out. MODERN_TURN_RATE,
	 * MODERN_WALK_BELOW and MODERN_DEAD are the tuning values; everything
	 * else is frozen here so that a change to one of them is a change somebody
	 * had to mean. */
	{
		MODERN_CFG cfg;

		ModernDefaults(&cfg);
		ok(cfg.dead == 0, "dead zone starts at nothing (the hardware one is applied)",
			cfg.dead, 0);
		/* Moved off TROne's 62 (16000 of 32767) to 0: the stick must never be
		 * able to set IN_WALK on its own, only the pad's own walk key can.
		 * walk_threshold() below still proves the TROne mechanism works,
		 * under an explicit cfg that asks for it by name. */
		ok(cfg.walk_below == 0, "walk below is 0: the stick alone never walks her",
			cfg.walk_below, 0);
		ok(cfg.aim_window == 0x0400, "aligned window 2.8 degrees", cfg.aim_window, 0x0400);
		ok(cfg.aligned_factor == 2, "and twice that once aligned", cfg.aligned_factor, 2);
		ok(cfg.spin_enter == 0x5000, "spin above 112.5 degrees", cfg.spin_enter, 0x5000);
		ok(cfg.spin_exit == 0x2800, "run again below 56.25", cfg.spin_exit, 0x2800);
		ok(cfg.stand_spin == 0x3000, "standing, spin above 67.5", cfg.stand_spin, 0x3000);
		ok(cfg.steer_step == 0x0180, "the thumb's noise floor", cfg.steer_step, 0x0180);
		/* recenter now equals MODERN_STEER_STEP itself: any stick change
		 * above the noise floor re-seats the course. held_course() pins
		 * TROne's own 0x1000 explicitly for the checks ported from their
		 * file, and adds its own four at the shipped default. */
		ok(cfg.recenter == cfg.steer_step, "recenter now equals the noise floor itself",
			cfg.recenter, cfg.steer_step);
		ok(cfg.aim_sector == 0x2000, "the quadrant boundary", cfg.aim_sector, 0x2000);
		ok(cfg.block_frames == 6, "six ticks make a block", cfg.block_frames, 6);
		ok(cfg.block_eps == 4, "four world units still count as not moving",
			cfg.block_eps, 4);
		/* 3277 was TROne's own 18 degrees a tick, a lid the thumb runs past.
		 * 5461 is 30 degrees a tick, measured from Remaster footage: a spin
		 * on the spot in 0.42 s and a running half turn in six ticks. */
		ok(cfg.turn_rate == 5461, "the direct turn's rate, the Remaster's own 30 deg/tick",
			cfg.turn_rate, 5461);
		ok(cfg.direct_turn == 1, "the direct turn's own gate is on", cfg.direct_turn, 1);
		/* MODERN_SNAP_DELTA is gone with the one-tick about-turn. There is no
		 * successor field: a reversal is turned at the rate like everything
		 * else now, which reversal() below measures tick by tick. */
		ok(cfg.snap_forward == 0x2000, "forward returns at 45 degrees left",
			cfg.snap_forward, 0x2000);
		/* Standing it is twice as wide: behind the narrow one a standing pose
		 * spins on the spot for 232 ticks (measured). */
		ok(cfg.snap_forward_stand == 0x4000, "...and at 90 while she turns on the spot",
			cfg.snap_forward_stand, 0x4000);
		/* A standing turn that is getting nowhere - the thumb circling at
		 * exactly her rate, so the error never falls - is handed IN_FORWARD
		 * after this many ticks whatever the gate says. spin_sweep() measures
		 * both halves. */
		ok(cfg.stand_forward_ticks == 8, "...and a standing turn that arrives nowhere"
			" runs after eight ticks", cfg.stand_forward_ticks, 8);
		/* The forward jump's own rate, four times the classic's 182. */
		ok(cfg.air_turn_rate == 728, "the air turn is 4 degrees a tick", cfg.air_turn_rate, 728);
		ok(cfg.stick_full == 128, "full deflection in our units", cfg.stick_full, 128);
		/* aim_strafe ships OFF: firing does not put her into the strafing
		 * walk on its own. The rule is still built and still tested, behind
		 * an explicit cfg; this line is what it ships as. */
		ok(cfg.hold_course == 1 && cfg.back_turns == 1 && cfg.aim_strafe == 0 &&
			cfg.jump_dir == 1 && cfg.block_turn == 1, "four rules on, the strafe off",
			cfg.hold_course + cfg.back_turns + cfg.aim_strafe + cfg.jump_dir +
			cfg.block_turn, 4);

		/* Three turn-rate candidates, all named so swapping one back in is a
		 * one-word edit. The default is the Remaster-measured 5461; 3277 is
		 * TROne's 18 degrees. */
		ok(MODERN_TURN_RATE_VANILLA == 1456, "vanilla's fastest turn, for the play test",
			MODERN_TURN_RATE_VANILLA, 1456);
		ok(MODERN_TURN_RATE_MIDDLE == 2184, "and the one in between",
			MODERN_TURN_RATE_MIDDLE, 2184);
		ok(MODERN_TURN_RATE_TRONE == 3277, "and TROne's own 18 degrees, which we shipped once",
			MODERN_TURN_RATE_TRONE, 3277);
	}

	/* ModernResetState is what the seam calls on a level start, on a death and
	 * when the scheme is switched. Every field, because a forgotten one is a
	 * course held across a level change. */
	{
		MODERN_STATE state;

		state.have_ref = 1;
		state.ref = 1234;
		state.course = 4321;
		state.last_off = 999;
		state.spin = 1;
		state.aligned = 1;
		state.aiming = 1;
		state.blocked_count = 5;
		state.last_mode = MODERN_MODE_RUN;
		state.last_course = 777;
		state.have_course = 1;
		state.last_turn = -1;		/* the tie-break's own memory */
		ModernResetState(&state);
		ok(!state.have_ref && !state.ref && !state.course && !state.last_off &&
			!state.spin && !state.aligned && !state.aiming && !state.blocked_count &&
			state.last_mode == MODERN_MODE_OFF, "the reset really clears every field",
			state.have_ref + state.ref + state.course + state.last_off + state.spin +
			state.aligned + state.aiming + state.blocked_count + state.last_mode, 0);
		ok(!state.last_course && !state.have_course && !state.last_turn,
			"...the course memory and the turn's own side with them",
			state.last_course + state.have_course + state.last_turn, 0);
	}
}

/* --------------------------------------------------- the stick's own zone -- */

/* The per-axis zone the square zone replaces, kept so that the new one can be
 * measured against it rather than only asserted about: each component
 * judged on its own, which is the whole defect. */
static void old_axis_zone(long zone, long in_x, long in_y, long* out_x, long* out_y)
{
	*out_x = (in_x > -zone && in_x < zone) ? 0 : in_x;
	*out_y = (in_y > -zone && in_y < zone) ? 0 : in_y;
}

/* A pair's direction in degrees, 0..360. */
static double dirdeg(long x, long y)
{
	const double a = atan2((double)y, (double)x) * 180.0 / 3.14159265358979323846;

	return (a < 0.0) ? a + 360.0 : a;
}

/* How many of a full sweep land EXACTLY on one of the four cardinals - the
 * measurement that says whether a zone snaps directions onto axes. A round
 * stick answers 4, one per cardinal; a per-axis zone answers far more. */
static long cardinal_hits(long zone, long axis_zone, long radius, long old_zone)
{
	long deg, x, y, ox, oy, hits = 0;

	for (deg = 0; deg < 360; deg++)
	{
		const double r = (double)deg * 3.14159265358979323846 / 180.0;

		x = roundl2(cos(r) * (double)radius);
		y = roundl2(sin(r) * (double)radius);

		if (old_zone)
			old_axis_zone(zone, x, y, &ox, &oy);
		else
			ModernStickDeadZone(zone, axis_zone, x, y, &ox, &oy);

		if ((ox || oy) && (!ox || !oy))
			hits++;
	}

	return hits;
}

/* The worst direction error of a full sweep at one radius, in degrees. */
static double worst_dir(long zone, long axis_zone, long radius, long* smallest_mag)
{
	double worst = 0.0;
	long deg;

	*smallest_mag = 1000;

	for (deg = 0; deg < 360; deg++)
	{
		const double r = (double)deg * 3.14159265358979323846 / 180.0;
		const long x = roundl2(cos(r) * (double)radius);
		const long y = roundl2(sin(r) * (double)radius);
		long ox, oy, m;
		double err;

		m = ModernStickDeadZone(zone, axis_zone, x, y, &ox, &oy);

		if (!ox && !oy)
			continue;

		if (m < *smallest_mag)
			*smallest_mag = m;

		err = dirdeg(ox, oy) - dirdeg(x, y);

		while (err > 180.0)
			err -= 360.0;

		while (err < -180.0)
			err += 360.0;

		if (err < 0.0)
			err = -err;

		if (err > worst)
			worst = err;
	}

	return worst;
}

static void deadzone()
{
	const long zones[3] = { MODERN_DEAD_LEFT, MODERN_DEAD_RIGHT, MODERN_DEAD_RIGHT_DINPUT };
	const char* names[3] = { "left 31", "right 34", "right DI 24" };
	long z, deg, mag, x, y, ox, oy, m, prev, bad, smallest;
	double worst;

	printf("the radial dead zone - the direction the thumb holds is the direction that comes out\n");

	/* 1. Nothing inside the zone: a resting stick that turns the camera is the
	 * one failure this exists to prevent, so the whole disc is swept, not the
	 * two axes. */
	for (z = 0; z < 3; z++)
	{
		bad = 0;

		for (deg = 0; deg < 360; deg++)
		{
			const double r = (double)deg * 3.14159265358979323846 / 180.0;

			for (mag = 0; mag <= zones[z]; mag++)
			{
				x = roundl2(cos(r) * (double)mag);
				y = roundl2(sin(r) * (double)mag);

				if (x * x + y * y > zones[z] * zones[z])
					continue;	// the integer grid rounded this one OUT of the zone

				m = ModernStickDeadZone(zones[z], 0, x, y, &ox, &oy);

				if (ox || oy || m)
					bad++;
			}
		}

		ok(bad == 0, "inside the zone nothing comes out at all", bad, 0);
		printf("  %s: %ld of the disc's points moved anything\n", names[z], bad);
	}

	/* 2. Full at the rim, in every direction - a round stick, not a square one.
	 * A degree of the sweep is itself two rounded integers, so a point asked
	 * for at radius 128 can land at 127.5 and answer 127: the promise is
	 * "within one of full in every direction", and the exact 128 is asserted
	 * where the input really is at or past the rim. */
	for (z = 0; z < 3; z++)
	{
		long worst_m = MODERN_STICK_FULL;

		bad = 0;

		for (deg = 0; deg < 360; deg++)
		{
			const double r = (double)deg * 3.14159265358979323846 / 180.0;

			x = roundl2(cos(r) * (double)MODERN_STICK_FULL);
			y = roundl2(sin(r) * (double)MODERN_STICK_FULL);
			m = ModernStickDeadZone(zones[z], 0, x, y, &ox, &oy);

			if (m < worst_m)
				worst_m = m;

			if (m < MODERN_STICK_FULL - 1)
				bad++;

			if (absl((long)roundl2(sqrt((double)(ox * ox + oy * oy))) - MODERN_STICK_FULL) > 1)
				bad++;
		}

		ok(bad == 0, "at the stick's own rim the length is full in every direction", bad, 0);
		printf("  %s: shortest of the 360 rim pushes %ld of %d\n",
			names[z], worst_m, (int)MODERN_STICK_FULL);

		m = ModernStickDeadZone(zones[z], 0, MODERN_STICK_FULL, 0, &ox, &oy);
		ok(m == MODERN_STICK_FULL && ox == MODERN_STICK_FULL && !oy,
			"a push right at the rim is exactly full", m, MODERN_STICK_FULL);
		m = ModernStickDeadZone(zones[z], 0, 0, -MODERN_STICK_FULL, &ox, &oy);
		ok(m == MODERN_STICK_FULL && !ox && oy == -MODERN_STICK_FULL,
			"...in the other axis too, and forward is still negative", oy, -MODERN_STICK_FULL);
	}

	/* 3. And the diagonal is not 181: the two axes are gated as a square, so a
	 * corner push reads 181 raw; an unclamped rate would be 1.4 times the one
	 * the header promises for a push the player cannot tell from any other. */
	m = ModernStickDeadZone(MODERN_DEAD_RIGHT, 0, MODERN_STICK_FULL, MODERN_STICK_FULL, &ox, &oy);
	ok(m == MODERN_STICK_FULL, "a full diagonal is clamped to full and not to 181", m, MODERN_STICK_FULL);
	ok(ox == oy, "...and the clamp keeps the diagonal diagonal", ox, oy);
	printf("  corner push (128,128) -> (%ld,%ld), length %ld\n", ox, oy, m);

	/* 4. Monotonic along a ray: the length never falls while the thumb pushes
	 * further, so there is no place where pushing harder does less. */
	for (z = 0; z < 3; z++)
	{
		bad = 0;

		for (deg = 0; deg < 360; deg += 3)
		{
			const double r = (double)deg * 3.14159265358979323846 / 180.0;

			prev = -1;

			for (mag = 0; mag <= MODERN_STICK_FULL; mag++)
			{
				x = roundl2(cos(r) * (double)mag);
				y = roundl2(sin(r) * (double)mag);
				m = ModernStickDeadZone(zones[z], 0, x, y, &ox, &oy);

				if (m < prev)
					bad++;

				prev = m;
			}
		}

		ok(bad == 0, "pushing further never gives less", bad, 0);
	}

	/* 5. It eases in: crossing the rim is not a step - which is what the old
	 * per-axis zone could not do on the DirectInput right stick, where the
	 * first live reading was 62 of 128, half the travel at once. */
	m = ModernStickDeadZone(MODERN_DEAD_RIGHT_DINPUT, 0, MODERN_DEAD_RIGHT_DINPUT + 1, 0, &ox, &oy);
	ok(m <= 3, "one unit past the DirectInput zone the camera barely moves", m, 0);
	printf("  DirectInput right: raw %ld -> %ld (the per-axis zone's first reading was 62)\n",
		(long)MODERN_DEAD_RIGHT_DINPUT + 1, m);

	m = ModernStickDeadZone(MODERN_DEAD_RIGHT, 0, MODERN_DEAD_RIGHT + 1, 0, &ox, &oy);
	ok(m <= 3, "and the same one unit past the XInput zone", m, 0);

	/* 6. The defect itself, in measured units. A stick nine degrees off the
	 * axis: the old zone throws the small component away and hands on a
	 * cardinal, the new one hands on the direction held. */
	x = roundl2(cos(81.0 * 3.14159265358979323846 / 180.0) * 120.0);	// 19
	y = roundl2(sin(81.0 * 3.14159265358979323846 / 180.0) * 120.0);	// 119
	old_axis_zone(MODERN_DEAD_RIGHT_DINPUT, x, y, &ox, &oy);
	ok(ox == 0, "the old per-axis zone snapped a 9-degree-off push onto the axis", ox, 0);
	printf("  raw (%ld,%ld) = %.1f deg -> old (%ld,%ld) = %.1f deg\n",
		x, y, dirdeg(x, y), ox, oy, dirdeg(ox, oy));

	m = ModernStickDeadZone(MODERN_DEAD_RIGHT_DINPUT, 0, x, y, &ox, &oy);
	ok(absl((long)roundl2(dirdeg(ox, oy) - dirdeg(x, y))) <= 1,
		"the radial zone keeps it", (long)roundl2(dirdeg(ox, oy)), (long)roundl2(dirdeg(x, y)));
	printf("                          -> new (%ld,%ld) = %.1f deg, length %ld\n",
		ox, oy, dirdeg(ox, oy), m);

	/* 7. And the histogram that found it: 855 pushed ticks piled up on the
	 * four cardinals (68/41/48/43) and were all but empty beside them
	 * (1/2/6/4). Sweeping 360 even directions through each zone counts the
	 * same thing: how many of them come out with one axis thrown away. */
	printf("  a 360-direction sweep, how many come out ON an axis (4 is the honest answer):\n");

	for (z = 0; z < 3; z++)
	{
		const long old_hits = cardinal_hits(zones[z], 0, 120, 1);
		const long new_hits = cardinal_hits(zones[z], 0, 120, 0);

		printf("    %-12s old %3ld   new %3ld\n", names[z], old_hits, new_hits);
		ok(new_hits == 4, "the radial zone snaps nothing onto an axis", new_hits, 4);
		ok(old_hits > 4, "...where the per-axis zone snapped a whole band of them", old_hits, 5);
	}

	/* 8. The direction survives, to within a degree wherever the integer grid
	 * can hold a degree at all. Two integers of +-128 cannot do better than
	 * atan(0.5/m) at length m, so the promise is asserted where m allows it
	 * and printed everywhere - a number nobody can read is a number nobody can
	 * check. */
	printf("  worst direction error of a 360-direction sweep:\n");

	for (z = 0; z < 3; z++)
	{
		const long radii[3] = { MODERN_STICK_FULL, 96, 64 };
		long k;

		for (k = 0; k < 3; k++)
		{
			worst = worst_dir(zones[z], 0, radii[k], &smallest);
			printf("    %-12s radius %3ld: %.2f deg (shortest output %ld)\n",
				names[z], radii[k], worst, smallest);

			if (smallest >= 29)
				ok(worst <= 1.0, "the direction survives the zone to within a degree",
					(long)roundl2(worst * 100.0), 100);
			else
				ok(worst <= atan(0.5 / (double)smallest) * 180.0 / 3.14159265358979323846 + 0.01,
					"the direction survives to the integer grid's own limit",
					(long)roundl2(worst * 100.0),
					(long)roundl2(atan(0.5 / (double)smallest) * 18000.0 / 3.14159265358979323846));
		}
	}

	/* 9. Odd: the stick pushed the other way reads the same distance the other
	 * way, so no direction is quietly favoured by the sign of its components. */
	bad = 0;

	for (deg = 0; deg < 360; deg++)
	{
		const double r = (double)deg * 3.14159265358979323846 / 180.0;
		long bx, by;

		x = roundl2(cos(r) * 100.0);
		y = roundl2(sin(r) * 100.0);

		ModernStickDeadZone(MODERN_DEAD_LEFT, 0, x, y, &ox, &oy);
		ModernStickDeadZone(MODERN_DEAD_LEFT, 0, -x, -y, &bx, &by);

		if (bx != -ox || by != -oy)
			bad++;
	}

	ok(bad == 0, "the zone is odd: the other way round reads the same", bad, 0);

	/* 10. The two ends a registry key could one day hand it: a zone of nothing
	 * passes the stick through; a zone at or past the rim is a dead stick and
	 * not a divide by nothing. */
	m = ModernStickDeadZone(0, 0, 100, 0, &ox, &oy);
	ok(m == 100 && ox == 100 && oy == 0, "a zone of nothing changes nothing", ox, 100);
	m = ModernStickDeadZone(-5, 0, 100, 0, &ox, &oy);
	ok(m == 100 && ox == 100, "and a negative one is the same as nothing", ox, 100);
	m = ModernStickDeadZone(MODERN_STICK_FULL, 0, 127, 0, &ox, &oy);
	ok(m == 0 && !ox && !oy, "a zone at the rim is a dead stick, not a crash", m, 0);
	m = ModernStickDeadZone(5000, 0, 127, 0, &ox, &oy);
	ok(m == 0 && !ox && !oy, "and so is one past it", m, 0);
	m = ModernStickDeadZone(MODERN_DEAD_LEFT, 0, 0, 0, &ox, &oy);
	ok(m == 0 && !ox && !oy, "a stick at dead centre is dead centre", m, 0);

	/* 11. Nothing leaves the domain the whole tree carries axes in. */
	bad = 0;

	for (x = -MODERN_STICK_FULL; x <= MODERN_STICK_FULL; x += 1)
		for (y = -MODERN_STICK_FULL; y <= MODERN_STICK_FULL; y += 7)
		{
			m = ModernStickDeadZone(MODERN_DEAD_RIGHT, 0, x, y, &ox, &oy);

			if (absl(ox) > MODERN_STICK_FULL || absl(oy) > MODERN_STICK_FULL ||
				m < 0 || m > MODERN_STICK_FULL)
				bad++;
		}

	ok(bad == 0, "no push of any kind leaves +-128", bad, 0);

	/* 12. And the square the hardware held: a per-axis zone of half-width A
	 * zeroes the pair over the whole square of that half-width, while a
	 * radial zone of radius A only covers the disc inside it, so every rest
	 * position in the square's corners - out to A * 1.41 - stopped being
	 * dead. The four pairs below are the four the seam actually uses
	 * (specific\input.cpp), zone and square together. */
	{
		const long zone2[4] = { MODERN_DEAD_LEFT, MODERN_DEAD_RIGHT,
			MODERN_DEAD_LEFT_DINPUT, MODERN_DEAD_RIGHT_DINPUT };
		const long axis2[4] = { MODERN_DEAD_AXIS_LEFT, MODERN_DEAD_AXIS_RIGHT,
			MODERN_DEAD_AXIS_LEFT_DINPUT, MODERN_DEAD_AXIS_RIGHT_DINPUT };
		const char* name2[4] = { "left XInput", "right XInput",
			"left DInput", "right DInput" };
		long k;

		/* The DirectInput zones were originally sized for a stick resting at
		 * (450,-450) of 1000 - (56,-56) at the seam, radius 79 - the widest
		 * rest the hardware's own 500 could be hiding, with no measurement of
		 * where the stick really rests. Measured over eight sessions of
		 * MODERNLOG's pad line: lx 0..2, ly -9..10, rx 2..5, ry -6..2, all of
		 * 128. So the worst rest this pad has ever shown sits inside (10,10),
		 * and that is what the zones are sized for now.
		 *
		 * Both figures are checked here, though only the measured one is
		 * asserted: the measured rest is dead under every pair the seam
		 * uses, which is the promise. The originally feared rest is printed
		 * instead, because at 24 and 20 it is alive - safe only because the
		 * rest position is now a measurement and not a worst case. */
		{
			const long rest[3][2] = { { 10, 10 }, { -10, -10 }, { 10, -10 } };
			long k2, r2;

			for (k2 = 0; k2 < 4; k2++)
				for (r2 = 0; r2 < 3; r2++)
				{
					m = ModernStickDeadZone(zone2[k2], axis2[k2],
						rest[r2][0], rest[r2][1], &ox, &oy);
					ok(m == 0 && !ox && !oy,
						"the pad's own measured rest is dead on every path", m, 0);
				}
		}

		m = ModernStickDeadZone(MODERN_DEAD_RIGHT_DINPUT, MODERN_DEAD_AXIS_RIGHT_DINPUT,
			56, -56, &ox, &oy);
		printf("  the rest first feared, (450,-450) of 1000 = (56,-56), radius 79:\n");
		printf("    through the new 24/20 -> length %ld of %d; the pad's measured rest lies"
			" inside (10,10)\n", m, (int)MODERN_STICK_FULL);

		/* Every point the hardware zeroed is still zero - the whole claim, swept
		 * over the domain rather than argued, for each of the four pairs. */
		for (k = 0; k < 4; k++)
		{
			long leaked = 0;
			long lost = 0;

			for (x = -MODERN_STICK_FULL; x <= MODERN_STICK_FULL; x++)
				for (y = -MODERN_STICK_FULL; y <= MODERN_STICK_FULL; y++)
				{
					long px, py;

					old_axis_zone(axis2[k], x, y, &px, &py);
					ModernStickDeadZone(zone2[k], axis2[k], x, y, &ox, &oy);

					if (!px && !py && (ox || oy))
						leaked++;
					else if ((px || py) && !ox && !oy)
						lost++;
				}

			/* `lost` is the OTHER side of the exchange and is printed rather
			 * than asserted away: it is the thin band between the square and
			 * the radial rim, where a push the per-axis zone let through as a
			 * cardinal is now judged too short. It is a push of under 34 of
			 * 128 in one component and nothing in the other. */
			ok(leaked == 0, "nothing the per-axis zone zeroed comes alive", leaked, 0);
			printf("    %-13s dead->live %ld, live->dead %ld (the radial rim's own band)\n",
				name2[k], leaked, lost);
		}

		/* And the snap is still cured, which is what the square must not cost:
		 * with both gates in place a 360-direction sweep still lands on a
		 * cardinal four times and nowhere else, and the direction still
		 * survives to the integer grid's own limit. */
		for (k = 0; k < 4; k++)
		{
			const long hits = cardinal_hits(zone2[k], axis2[k], 120, 0);

			worst = worst_dir(zone2[k], axis2[k], 120, &smallest);
			ok(hits == 4, "with the square too, nothing is snapped onto an axis", hits, 4);
			ok(worst <= 1.0, "...and the direction still survives to within a degree",
				(long)roundl2(worst * 100.0), 100);
			printf("    %-13s cardinal hits %ld, worst direction error %.2f deg\n",
				name2[k], hits, worst);
		}
	}

	/* 7. And the player's own per cent on top of all eight: a slider,
	 * toggleable the way the Remaster's is, over the tuned zone widths.
	 *
	 * The row is worth nothing if its default is not the tuned engine
	 * itself, so that is the first thing checked here: 50 has to give back
	 * every one of the eight tuned widths unchanged, not something near them. */
	{
		const long base8[8] = { MODERN_DEAD_LEFT, MODERN_DEAD_RIGHT,
			MODERN_DEAD_LEFT_DINPUT, MODERN_DEAD_RIGHT_DINPUT,
			MODERN_DEAD_AXIS_LEFT, MODERN_DEAD_AXIS_RIGHT,
			MODERN_DEAD_AXIS_LEFT_DINPUT, MODERN_DEAD_AXIS_RIGHT_DINPUT };
		const char* name8[8] = { "MODERN_DEAD_LEFT", "MODERN_DEAD_RIGHT",
			"MODERN_DEAD_LEFT_DINPUT", "MODERN_DEAD_RIGHT_DINPUT",
			"MODERN_DEAD_AXIS_LEFT", "MODERN_DEAD_AXIS_RIGHT",
			"MODERN_DEAD_AXIS_LEFT_DINPUT", "MODERN_DEAD_AXIS_RIGHT_DINPUT" };
		long b, u;

		printf("  the dead zone slider - 50 is the tuned engine, 0 is none, 100 is twice\n");

		for (b = 0; b < 8; b++)
		{
			ok(ModernDeadZoneUser(base8[b], 50) == base8[b],
				"the slider's middle is the tuned constant itself",
				ModernDeadZoneUser(base8[b], 50), base8[b]);
			ok(ModernDeadZoneUser(base8[b], 0) == 0,
				"...and its bottom end is no zone at all",
				ModernDeadZoneUser(base8[b], 0), 0);
			ok(ModernDeadZoneUser(base8[b], 100) == 2 * base8[b],
				"...and its top end is twice the zone",
				ModernDeadZoneUser(base8[b], 100), 2 * base8[b]);
		}

		printf("    %-30s %s\n", "constant", "0 / 25 / 50 / 75 / 100");

		for (b = 0; b < 8; b++)
			printf("    %-30s %ld / %ld / %ld / %ld / %ld\n", name8[b],
				ModernDeadZoneUser(base8[b], 0), ModernDeadZoneUser(base8[b], 25),
				ModernDeadZoneUser(base8[b], 50), ModernDeadZoneUser(base8[b], 75),
				ModernDeadZoneUser(base8[b], 100));

		/* MONOTONE over the whole bar, for every base: a slider that gives back
		 * a NARROWER zone one notch further right is a slider nobody can tune
		 * by feel, and integer rounding is exactly the thing that could do it. */
		for (b = 0; b < 8; b++)
		{
			long rising = 1;

			prev = ModernDeadZoneUser(base8[b], 0);

			for (u = 1; u <= 100; u++)
			{
				const long now = ModernDeadZoneUser(base8[b], u);

				if (now < prev)
					rising = 0;

				prev = now;
			}

			ok(rising == 1, "the bar never gives back a narrower zone", rising, 1);
		}

		/* And the +25 is really there. The three endpoints above cannot see
		 * it: at 0 and at 50 the remainder is 0, and at 100 the half-divisor
		 * truncates away again, so dropping the +25 from (base*user+25)/50
		 * passed every case in this group. The function's own comment
		 * promises round-to-nearest, so the promise is checked here rather
		 * than left to a reader's trust.
		 *
		 * The reference is computed the other way round - quotient plus
		 * remainder, no half-divisor in the numerator - so it is a second
		 * opinion and not the same expression twice. `rounded` counts the
		 * settings where the two roads would differ, and is asserted to be
		 * non-zero: a sweep that never meets a remainder of 25 or more proves
		 * nothing about rounding. */
		{
			long rounded = 0;

			for (b = 0; b < 8; b++)
			{
				long exact = 1;

				for (u = 0; u <= 100; u++)
				{
					const long whole = base8[b] * u;
					const long expect = whole / 50 + ((whole % 50) >= 25 ? 1 : 0);

					if (whole % 50 >= 25)
						rounded++;

					if (ModernDeadZoneUser(base8[b], u) != expect)
						exact = 0;
				}

				ok(exact == 1, "every notch of the bar rounds to the nearest unit",
					exact, 1);
			}

			ok(rounded > 0, "and the sweep really met the settings that round up",
				rounded, 1);

			/* The one the function's own comment names, kept as a case of its
			 * own so a failure says which number was meant: 31 at 40 per cent
			 * is 24.8, which answers 25 and not 24. */
			ok(ModernDeadZoneUser(MODERN_DEAD_LEFT, 40) == 25,
				"the comment's own example: 31 at 40 per cent is 25, not 24",
				ModernDeadZoneUser(MODERN_DEAD_LEFT, 40), 25);
		}

		/* The arguments are guarded, not trusted - the value comes from the
		 * registry and a hand can edit it. Out of range is pinned to the ends,
		 * and a base of 0 or less is no width to scale. */
		ok(ModernDeadZoneUser(MODERN_DEAD_LEFT, -5) == 0,
			"a negative setting reads as the bottom end",
			ModernDeadZoneUser(MODERN_DEAD_LEFT, -5), 0);
		ok(ModernDeadZoneUser(MODERN_DEAD_LEFT, 500) == 2 * MODERN_DEAD_LEFT,
			"and one past the top reads as the top",
			ModernDeadZoneUser(MODERN_DEAD_LEFT, 500), 2 * MODERN_DEAD_LEFT);
		ok(ModernDeadZoneUser(0, 100) == 0, "no base is no zone at any setting",
			ModernDeadZoneUser(0, 100), 0);
		ok(ModernDeadZoneUser(-3, 100) == 0, "...and neither is a negative one",
			ModernDeadZoneUser(-3, 100), 0);

		/* And the pad still rests silent at the default: two measured rest
		 * positions, on both DirectInput sticks, through the scaled pair the
		 * seam really passes at 50 (specific\input.cpp) - the same promise the
		 * measured rest above makes, asked once more on the way the slider now
		 * takes. The XInput pair is asked beside them because the same two
		 * readings arrive there when a DualSense runs through a wrapper. */
		{
			const long rest2[2][2] = { { 5, -6 }, { 2, 10 } };
			long r3, k3;

			for (k3 = 0; k3 < 4; k3++)
				for (r3 = 0; r3 < 2; r3++)
				{
					const long zb[4] = { MODERN_DEAD_LEFT_DINPUT, MODERN_DEAD_RIGHT_DINPUT,
						MODERN_DEAD_LEFT, MODERN_DEAD_RIGHT };
					const long ab[4] = { MODERN_DEAD_AXIS_LEFT_DINPUT, MODERN_DEAD_AXIS_RIGHT_DINPUT,
						MODERN_DEAD_AXIS_LEFT, MODERN_DEAD_AXIS_RIGHT };

					m = ModernStickDeadZone(ModernDeadZoneUser(zb[k3], 50),
						ModernDeadZoneUser(ab[k3], 50),
						rest2[r3][0], rest2[r3][1], &ox, &oy);
					ok(m == 0 && !ox && !oy,
						"a resting pad is still silent at the slider's default", m, 0);
				}
		}
	}
}

/* ------------------------------------------------- the stick becomes bits -- */

static void stick_to_tank()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_RESULT out;

	printf("stick to tank bits - the whole point of the scheme\n");

	/* TROne: modern.stick_direction_becomes_tank_input. */
	ModernDefaults(&cfg);
	ModernResetState(&state);

	/* Their first case is a stick inside the dead zone, 1000/-1000 of 32767.
	 * Ours is 0 (MODERN_DEAD): XInput has already thrown away its own 24 per
	 * cent before ReadJoystick sees the axes, and a second dead zone would
	 * only eat travel. So the case is run twice - once at rest, which is the
	 * only silent input at our default, and once with their threshold scaled
	 * in, kept in case the dead zone is ever widened. */
	tick(&cfg, &state, 0, 0, 0, 0, 0, &out);
	ok(out.active == 0, "at rest: nothing", out.active, 0);
	ok(out.bits == 0, "at rest: no bits", out.bits, 0);

	cfg.dead = S(8000);						/* TROne's own 8000 of 32767 = 31 of 128 */
	tick(&cfg, &state, S(1000), -S(1000), 0, 0, 0, &out);	/* TROne 1000/-1000 */
	ok(out.active == 0, "inside a raised dead zone: nothing", out.active, 0);
	ok(out.bits == 0, "inside a raised dead zone: no bits", out.bits, 0);
	cfg.dead = 0;

	/* Stick forward, camera on +Z, Lara on +Z: forward alone, no turn. */
	tick(&cfg, &state, 0, -S(30000), 0, 0, 0, &out);	/* TROne 0/-30000 */
	ok(out.active == 1, "forward: the translation is responsible", out.active, 1);
	ok(out.bits == MODERN_IN_FORWARD, "forward: IN_FORWARD alone", out.bits,
		MODERN_IN_FORWARD);
	ok(out.target == 0, "forward: the world course is +Z", out.target, 0);

	/* Stick right: the target is 90 degrees round, so she turns - and keeps
	 * running, because 0x4000 of error is still under spin_enter. */
	tick(&cfg, &state, S(30000), 0, 0, 0, 0, &out);
	ok(out.target == 0x4000, "right: the course is +X", out.target, 0x4000);
	ok(out.delta == 0x4000, "right: a quarter turn of error", out.delta, 0x4000);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "right: the turn bit",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "right: and she runs while turning",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);

	/* The camera turned 90 degrees: the SAME stick forward now means +X. That
	 * is the camera-relative half of the scheme, and it is seated at the push,
	 * so the stick is let go first. */
	release(&cfg, &state);
	tick(&cfg, &state, 0, -S(30000), 0x4000, 0, 0, &out);
	ok(out.reference == 0x4000, "a new push seats on the camera of that tick",
		out.reference, 0x4000);
	ok(out.target == 0x4000, "...so forward under a turned camera means +X",
		out.target, 0x4000);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "...and she turns towards it",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);

	/* Stick left. */
	release(&cfg, &state);
	tick(&cfg, &state, -S(30000), 0, 0, 0, 0, &out);
	ok(out.target == -0x4000, "left: the course is -X", out.target, -0x4000);
	ok((out.bits & MODERN_IN_LEFT) != 0, "left: the turn bit",
		out.bits & MODERN_IN_LEFT, MODERN_IN_LEFT);
}

/* ------------------------------------------------------- the held course -- */

static void held_course()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_RESULT out;

	printf("the held course - what stops her circling forever\n");

	/* TROne: modern.reference_is_latched_while_the_stick_is_held. The chase
	 * camera hangs behind Lara, so its heading IS her heading; read it fresh
	 * every tick and she turns, it follows, the target walks on and she
	 * circles. TROne measured 600 frames of exactly that. The reference is
	 * therefore latched at the push and the error has to SHRINK. */
	ModernDefaults(&cfg);
	/* The shipped default has moved off TROne's 0x1000 (bit_values() checks
	 * the shipped number, MODERN_RECENTER == 0x0180 == steer_step now, own
	 * story in game\modern_move.h). Every check in this function is
	 * otherwise a TROne-ported statement about the held course itself,
	 * unaffected by which number recenter has - but it is pinned explicitly
	 * anyway so a future default change cannot silently make it mean less
	 * than TROne measured: the old three-band behaviour stays provable under
	 * an explicit cfg even though it is not the default. The new default's own
	 * tests are at the end of this function. */
	cfg.recenter = 0x1000;
	ModernResetState(&state);

	tick(&cfg, &state, S(30000), 0, 0, 0, 0, &out);
	ok(out.reference == 0, "seated on the camera of the push", out.reference, 0);
	ok(out.target == 0x4000, "and the world course is +X", out.target, 0x4000);

	tick(&cfg, &state, S(30000), 0, 0x1000, 0x1000, 0, &out);
	ok(out.reference == 0, "the camera follows her - the reference does not move",
		out.reference, 0);
	ok(out.target == 0x4000, "...nor does the course", out.target, 0x4000);
	ok(out.delta == 0x3000, "...and the error shrinks", out.delta, 0x3000);

	tick(&cfg, &state, S(30000), 0, 0x3000, 0x3000, 0, &out);
	ok(out.target == 0x4000, "still the same course", out.target, 0x4000);
	ok(out.delta == 0x1000, "...and still shrinking", out.delta, 0x1000);

	tick(&cfg, &state, S(30000), 0, 0x4000, 0x4000, 0, &out);
	ok(out.delta == 0, "she arrives", out.delta, 0);
	ok(out.bits == MODERN_IN_FORWARD, "...and only runs from here on", out.bits,
		MODERN_IN_FORWARD);

	/* Let go and push again: a fresh reference from the camera of NOW, so the
	 * same stick direction means a different world course. */
	release(&cfg, &state);
	tick(&cfg, &state, S(30000), 0, 0x4000, 0x4000, 0, &out);
	ok(out.reference == 0x4000, "the new push takes the new camera", out.reference, 0x4000);
	ok(out.target == -32768, "...so right now means half a turn round",
		out.target, -32768);

	/* TROne: modern.a_camera_cut_does_not_bend_the_course. This is the rule a
	 * fixed room camera breaks in every other implementation. */
	ModernResetState(&state);
	tick(&cfg, &state, 0, -S(30000), 0, 0, 0, &out);
	ok(out.target == 0, "seated: the course is +Z", out.target, 0);

	tick(&cfg, &state, 0, -S(30000), 0x4000, 0, 0, &out);
	ok(out.target == 0, "the camera cuts 90 degrees - the course does not move",
		out.target, 0);
	tick(&cfg, &state, 0, -S(30000), -0x4000, 0, 0, &out);
	ok(out.target == 0, "...and cuts back the other way - still no movement",
		out.target, 0);

	/* A small stick movement is fine steering: the course turns by exactly
	 * that much and the (still turned) camera stays out of it. */
	tick(&cfg, &state, S(6000), -S(30000), -0x4000, 0, 0, &out);	/* TROne 6000/-30000 */
	ok(out.target > 0 && out.target < cfg.recenter,
		"a small stick change steers the course itself", out.target, cfg.recenter / 2);

	/* Ours: the noise floor itself, which is the third of the four rules and
	 * the only one TROne's own cases never put a number on - a mutation of the
	 * tree that deletes it passed every other check in this file, which is how
	 * this block came to be written. A thumb is never still: below steer_step
	 * (0x0180, 2.1 degrees) the course must not move by a unit, and one stick
	 * unit further it must move by exactly the angle the stick did.
	 * At full deflection one unit of our x axis is about 82 angle units, so
	 * x = 4 is under the floor and x = 5 is over it. */
	ModernResetState(&state);
	tick(&cfg, &state, 0, -127, 0, 0, 0, &out);
	ok(out.target == 0, "seated straight ahead", out.target, 0);
	ok(ModernAngle(4, 127) < cfg.steer_step, "4 of 128 sideways is under the noise floor",
		ModernAngle(4, 127), cfg.steer_step);
	tick(&cfg, &state, 4, -127, 0, 0, 0, &out);
	ok(out.target == 0, "...so a shaky thumb does not walk the course", out.target, 0);

	ok(ModernAngle(5, 127) >= cfg.steer_step, "5 of 128 sideways is over it",
		ModernAngle(5, 127), cfg.steer_step);
	tick(&cfg, &state, 5, -127, 0, 0, 0, &out);
	ok(out.target == ModernAngle(5, 127), "...and real steering moves the course exactly",
		out.target, ModernAngle(5, 127));

	/* Only a DELIBERATE change re-seats on the camera of now. */
	ModernResetState(&state);
	tick(&cfg, &state, 0, -S(30000), 0, 0, 0, &out);
	tick(&cfg, &state, S(6000), -S(30000), -0x4000, 0, 0, &out);
	tick(&cfg, &state, S(30000), 0, -0x4000, 0, 0, &out);
	ok(out.reference == -0x4000, "a big stick change re-seats on the camera",
		out.reference, -0x4000);
	ok(out.target == 0, "...and the same stick means a new world course",
		out.target, 0);

	/* TROne: modern.held_back_keeps_its_course_when_the_chase_camera_turns.
	 * Theirs runs through their per-tick state machine and the direct turn.
	 * The half that is arithmetic is this: pull back, let the camera swing a
	 * degree a tick for twenty ticks, and the course must not move by a unit. */
	ModernResetState(&state);
	tick(&cfg, &state, 0, S(30000), 0, 0, 0, &out);
	{
		const short seated = out.target;
		long moved = 0;
		int t;

		ok(seated == -32768, "pull back: the course is half a turn round", seated, -32768);

		for (t = 1; t < 20; t++)
		{
			tick(&cfg, &state, 0, S(30000), (short)(-t * 32), 0, 0, &out);

			if (out.target != seated)
				moved++;
		}

		ok(moved == 0, "19 ticks of a swinging chase camera move the course not at all",
			moved, 0);
	}

	/* A measured episode (see game\modern_move.h): ticks 470-508 (t=53.0s),
	 * the stick turning slowly from right (125/0) to back-right (85/105)
	 * over 39 ticks - 51 degrees of stick rotation, every single per-tick
	 * step small enough to stay under either the old 0x1000 or the
	 * candidate 0x2000 - while the chase camera swung 120 degrees behind
	 * her. The course only ever fine-steered: it followed the stick's 51
	 * degrees and finished 50 degrees short of the intended heading,
	 * because a stick read against the camera visible at that instant does
	 * not match a course seeded once and nudged since. The fix:
	 * MODERN_RECENTER == MODERN_STEER_STEP, so any stick change above the
	 * noise floor re-seats. Four checks below match the shape of the
	 * measurement and its two guarantees. */

	/* (a) At the shipped defaults: a stick that keeps moving - 3 degrees a
	 * tick, comfortably over the 2.1-degree noise floor, the shape of the
	 * measured episode above - re-seats on every tick while a camera swings
	 * under her, tracking it exactly as closely as the Remaster's own
	 * every-tick read does for as long as the thumb keeps moving. */
	{
		short cam = 0;
		int t;

		ModernDefaults(&cfg);
		ModernResetState(&state);
		tick(&cfg, &state, 0, -127, cam, 0, 0, &out);	/* seat straight ahead */

		for (t = 1; t <= 20; t++)
		{
			const double rad = 6.283185307179586 * (3.0 * (double)t) / 360.0;
			const long sx = roundl2(127.0 * sin(rad));
			const long sy = -roundl2(127.0 * cos(rad));
			const short offset = ModernAngle(sx, -sy);
			short want_course;

			cam = ModernWrap((long)cam - roundl2(65536.0 * 5.0 / 360.0));	/* ~5 deg/tick */
			want_course = ModernWrap((long)cam + offset);

			tick(&cfg, &state, sx, sy, cam, 0, 0, &out);

			ok(out.reference == cam, "(a) every moving tick re-seats: ref is this tick's camera",
				out.reference, cam);
			ok(state.course == want_course,
				"(a) ...and the course is that camera plus the stick's own offset",
				state.course, want_course);
		}
	}

	/* (b) At the shipped defaults: a stick held exactly still while the
	 * camera swings 90 degrees over ten ticks - ref and course must not
	 * move by a unit. TROne's own circling finding demands this, and (a)
	 * does not touch it: a re-seat only fires on a stick change, and there
	 * is none here - the held-course guarantee and the camera-cut
	 * expectation, in the one check. */
	{
		short cam = 0;
		short seated_ref;
		short seated_course;
		int t;

		ModernDefaults(&cfg);
		ModernResetState(&state);
		tick(&cfg, &state, 100, -80, cam, 0, 0, &out);
		seated_ref = state.ref;
		seated_course = state.course;

		for (t = 1; t <= 10; t++)
		{
			cam = ModernWrap((long)cam + roundl2(65536.0 * 90.0 / 360.0 / 10.0));

			tick(&cfg, &state, 100, -80, cam, 0, 0, &out);

			ok(state.ref == seated_ref, "(b) a still stick: the reference never moves",
				state.ref, seated_ref);
			ok(state.course == seated_course,
				"(b) ...nor does the course, however far the camera swings",
				state.course, seated_course);
		}
	}

	/* (c) At the shipped defaults: one degree of stick movement - under the
	 * 2.1-degree noise floor - does not re-seat. recenter's new value does
	 * not touch steer_step's own threshold, only what now happens above it;
	 * the fine-steering band between them is empty, not the floor itself. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	tick(&cfg, &state, 0, -127, 0, 0, 0, &out);
	{
		const double rad = 6.283185307179586 / 360.0;
		const long sx = roundl2(127.0 * sin(rad));
		const long sy = -roundl2(127.0 * cos(rad));
		const short before = state.course;

		tick(&cfg, &state, sx, sy, 0, 0, 0, &out);
		ok(state.course == before, "(c) one degree of stick movement does not re-seat",
			state.course, before);
	}

	/* (d) is the pin at the top of this function: an explicit cfg with
	 * TROne's own 0x1000 keeps every ported expectation above provable,
	 * the old three-band behaviour and all - named here so the four checks
	 * this comment promised are easy to find. */
}

/* ------------------------------------------------- turning on the spot ----- */

static void spin_hysteresis()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;

	printf("the spin - and why it takes two thresholds\n");

	/* TROne: modern.big_turns_spin_on_the_spot_with_hysteresis. */
	ModernDefaults(&cfg);
	ModernResetState(&state);

	tick(&cfg, &state, 0, S(30000), 0, 0, 0, &out);		/* TROne 0/30000 = back */
	ok(state.spin == 1, "half a turn: she turns on the spot", state.spin, 1);
	ok((out.bits & MODERN_IN_FORWARD) == 0, "...and does NOT run",
		out.bits & MODERN_IN_FORWARD, 0);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) != 0, "...but does turn",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), MODERN_IN_LEFT);

	tick(&cfg, &state, 0, S(30000), 0, 0x3000, 0, &out);
	ok(state.spin == 1, "part way round, the spin holds", state.spin, 1);
	ok((out.bits & MODERN_IN_FORWARD) == 0, "...still no running",
		out.bits & MODERN_IN_FORWARD, 0);

	tick(&cfg, &state, 0, S(30000), 0, 0x6000, 0, &out);
	ok(state.spin == 0, "near enough: the spin ends", state.spin, 0);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...and she runs",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);

	tick(&cfg, &state, 0, S(30000), 0, -32768, 0, &out);
	ok(out.bits == MODERN_IN_FORWARD, "aligned: forward alone", out.bits, MODERN_IN_FORWARD);

	state.spin = 1;
	release(&cfg, &state);
	ok(state.spin == 0, "letting go clears the spin", state.spin, 0);

	/* TROne: modern.standing_spins_earlier_than_running. In the run, forward
	 * plus a turn bit is a curve and that is wanted. STANDING it cannot be:
	 * game\lara.cpp's lara_as_stop turns IN_LEFT/IN_RIGHT into AS_TURN_L/R and
	 * a simultaneous IN_FORWARD overwrites the goal with run or walk - so with
	 * something in the way she would stop answering altogether. */
	sit.anim_state = 1;					/* AS_RUN */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, -0x0800, 0, &sit, &out);
	ok(out.delta == 0x3800, "78.75 degrees of error", out.delta, 0x3800);
	ok(state.spin == 0, "running, that is a curve", state.spin, 0);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...so she keeps running",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);

	state.spin = 0;
	state.have_ref = 0;
	sit.anim_state = 2;					/* AS_STOP */
	tick(&cfg, &state, S(30000), 0, -0x0800, 0, &sit, &out);
	ok(out.delta == 0x3800, "the same error, standing", out.delta, 0x3800);
	ok(state.spin == 1, "...is a turn on the spot", state.spin, 1);
	ok((out.bits & MODERN_IN_FORWARD) == 0, "...with no forward at all",
		out.bits & MODERN_IN_FORWARD, 0);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "...and the turn bit alone",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);

	/* The wall bump and the turn states count as standing too - that is where
	 * the old version hung. */
	ok(ModernStateIsGroundIdle(2) == 1, "AS_STOP is ground idle", ModernStateIsGroundIdle(2), 1);
	ok(ModernStateIsGroundIdle(6) == 1, "AS_TURN_R", ModernStateIsGroundIdle(6), 1);
	ok(ModernStateIsGroundIdle(7) == 1, "AS_TURN_L", ModernStateIsGroundIdle(7), 1);
	ok(ModernStateIsGroundIdle(12) == 1, "AS_SPLAT", ModernStateIsGroundIdle(12), 1);
	ok(ModernStateIsGroundIdle(14) == 1, "AS_LAND", ModernStateIsGroundIdle(14), 1);
	ok(ModernStateIsGroundIdle(20) == 1, "AS_FASTTURN", ModernStateIsGroundIdle(20), 1);
	ok(ModernStateIsGroundIdle(1) == 0, "AS_RUN is not", ModernStateIsGroundIdle(1), 0);
	ok(ModernStateIsGroundIdle(0) == 0, "AS_WALK is not", ModernStateIsGroundIdle(0), 0);

	/* stand_spin = 0 switches the lower threshold off again. */
	cfg.stand_spin = 0;
	state.spin = 0;
	state.have_ref = 0;
	tick(&cfg, &state, S(30000), 0, -0x0800, 0, &sit, &out);
	ok(state.spin == 0, "stand_spin 0 puts the running threshold back", state.spin, 0);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...and she runs while turning",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);
	cfg.stand_spin = MODERN_STAND_SPIN;

	/* TROne: modern.ground_reversal_turns_without_a_step_in_the_wrong_direction.
	 * Theirs measures the direct turn over twelve ticks through the glue (step
	 * 4). The part that is arithmetic: an about-turn from a stand sends NEITHER
	 * forward NOR back, so she cannot take a step the wrong way while the turn
	 * runs. */
	ModernResetState(&state);
	sit.anim_state = 2;
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok((out.bits & (MODERN_IN_FORWARD | MODERN_IN_BACK)) == 0,
		"an about-turn from a stand takes no step at all",
		out.bits & (MODERN_IN_FORWARD | MODERN_IN_BACK), 0);
	ok(out.bits == MODERN_IN_LEFT, "...it is the turn bit and nothing else",
		out.bits, MODERN_IN_LEFT);
}

/* ------------------------------------------------------------- the walk ---- */

static void walk_threshold()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_RESULT out;

	printf("walk below half a stick\n");

	/* TROne: modern.walk_below_threshold. Their cfg.walk_below is set to 16000
	 * of 32767; ours is 62 of 128, the same half deflection. The shipped
	 * default is 0 (checked in bit_values()), so this whole block sets
	 * walk_below back to 62 by hand to prove the mechanism itself still
	 * works with an explicit cfg. It is simply not what a player gets
	 * without asking any more. */
	ModernDefaults(&cfg);
	cfg.walk_below = 62;		/* explicit: the shipped default is 0 */
	ModernResetState(&state);

	tick(&cfg, &state, 0, -S(12000), 0, 0, 0, &out);	/* TROne 0/-12000 */
	ok((out.bits & MODERN_IN_WALK) != 0, "a third of a stick: she walks",
		out.bits & MODERN_IN_WALK, MODERN_IN_WALK);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...forwards", out.bits & MODERN_IN_FORWARD,
		MODERN_IN_FORWARD);
	ok(out.mag == S(12000), "...and the log reads the deflection back", out.mag, S(12000));

	tick(&cfg, &state, 0, -S(30000), 0, 0, 0, &out);	/* TROne 0/-30000 */
	ok((out.bits & MODERN_IN_WALK) == 0, "pushed over: she runs", out.bits & MODERN_IN_WALK, 0);

	/* Ours, at the threshold itself, because "below" is a word and 62 is a
	 * number: 61 walks, 62 does not. */
	release(&cfg, &state);
	tick(&cfg, &state, 0, -61, 0, 0, 0, &out);
	ok((out.bits & MODERN_IN_WALK) != 0, "61 of 128 walks", out.bits & MODERN_IN_WALK,
		MODERN_IN_WALK);
	release(&cfg, &state);
	tick(&cfg, &state, 0, -62, 0, 0, 0, &out);
	ok((out.bits & MODERN_IN_WALK) == 0, "62 of 128 runs", out.bits & MODERN_IN_WALK, 0);

	/* And the clamp: a full diagonal is 181 units long in our axes, which the
	 * magnitude clamps to 128 so that the log can be read against a number
	 * that means something. */
	release(&cfg, &state);
	tick(&cfg, &state, 128, -128, 0, 0, 0, &out);
	ok(out.mag == 128, "a full diagonal reads as full deflection, not 181", out.mag, 128);

	/* At the shipped defaults. A magnitude threshold cannot tell "still
	 * travelling" from "meant to walk" on a real stick: of 32 pushes from
	 * rest, 19 read 20-60 of 128 on their very first tick and 25 of the
	 * same 32 were already >= 100 on the second. So the stick alone must
	 * never set IN_WALK - not a smaller number, zero. This sweeps every
	 * direction and every magnitude the stick can produce and asserts the
	 * bit never once appears. */
	{
		long bad = 0;
		long cases = 0;
		long a, m;

		ModernDefaults(&cfg);		/* walk_below back to the shipped 0 */

		for (a = 0; a < 64; a++)
		{
			const double rad = 6.283185307179586 * (double)a / 64.0;

			for (m = 1; m <= 181; m++)
			{
				const long sx = roundl2((double)m * sin(rad));
				const long sy = -roundl2((double)m * cos(rad));

				if (!sx && !sy)
					continue;

				ModernResetState(&state);
				tick(&cfg, &state, sx, sy, 0, 0, 0, &out);
				cases++;

				if (out.bits & MODERN_IN_WALK)
					bad++;
			}
		}

		printf("  %ld stick magnitude/direction pairs at the shipped defaults\n", cases);
		ok(bad == 0, "at the defaults the stick can never set IN_WALK", bad, 0);
	}
}

/* ----------------------------------------------------------- the aiming ---- */

static void aiming()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;

	printf("aiming - she pushes around the target, she does not turn\n");

	/* TROne: modern.aiming_strafes_instead_of_turning. Weapon ready AND the
	 * action key down: the game turns her onto her target itself, so the stick
	 * becomes a side step and no turn bit goes out at all. Letting the action
	 * go gives the turn back in the same tick.
	 *
	 * Every case in this group sets cfg.aim_strafe explicitly, because the
	 * rule ships OFF - she runs while she fires - so TROne's expectations
	 * below are the expectations of the rule and not of the default any
	 * more. What the default does instead is firing_run() further down; the
	 * capability itself is unchanged. */
	ModernDefaults(&cfg);
	cfg.aim_strafe = 1;					/* explicit: the shipped default is 0 */
	ModernResetState(&state);
	sit.anim_state = 0;
	sit.classic = 0;
	sit.drawn = 1;
	sit.action = 1;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_AIM, "the aim mode", out.mode, MODERN_MODE_AIM);
	ok((out.bits & MODERN_IN_RSTEP) != 0, "stick right: a step to the right",
		out.bits & MODERN_IN_RSTEP, MODERN_IN_RSTEP);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) == 0, "...and NO turn bit",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), 0);

	tick(&cfg, &state, -S(30000), 0, 0, 0, &sit, &out);
	ok((out.bits & MODERN_IN_LSTEP) != 0, "stick left: a step to the left",
		out.bits & MODERN_IN_LSTEP, MODERN_IN_LSTEP);

	tick(&cfg, &state, 0, -S(30000), 0, 0, &sit, &out);
	ok((out.bits & ~(long)MODERN_IN_WALK) == MODERN_IN_FORWARD, "stick forward: forward",
		out.bits & ~(long)MODERN_IN_WALK, MODERN_IN_FORWARD);
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok((out.bits & ~(long)MODERN_IN_WALK) == MODERN_IN_BACK, "stick back: back",
		out.bits & ~(long)MODERN_IN_WALK, MODERN_IN_BACK);

	sit.action = 0;
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "action let go: turning and running again",
		out.mode, MODERN_MODE_RUN);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "...and the turn bit is back",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);

	/* Ours: the drawn weapon ALONE is not aiming. Walking about with the
	 * pistols out has to steer exactly as it does with empty hands, or the
	 * scheme changes under the player every time he draws. */
	sit.drawn = 1;
	sit.action = 0;
	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "a drawn gun on its own does not strafe",
		out.mode, MODERN_MODE_RUN);

	/* ...and the action alone, with empty hands, is a grab or a lever and not
	 * an aim. */
	sit.drawn = 0;
	sit.action = 1;
	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "the action key on its own does not strafe either",
		out.mode, MODERN_MODE_RUN);

	/* aim_strafe = 0 takes the whole rule out, and that is the shipped
	 * setting, so this case is asserted twice on purpose: once with the
	 * field set by hand, and once from ModernDefaults with nothing touched
	 * at all. The second is the one that would catch the default drifting
	 * back. */
	cfg.aim_strafe = 0;
	sit.drawn = 1;
	sit.action = 1;
	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "aim_strafe 0 switches the strafe off",
		out.mode, MODERN_MODE_RUN);

	ModernDefaults(&cfg);
	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "...and that IS the default now", out.mode,
		MODERN_MODE_RUN);
}

/* ------------------------------------------------------------ the jumps ---- */

static void jumping()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;

	printf("the directional jump - where a bit means which jump, not which way\n");

	/* TROne: modern.standing_jump_picks_a_direction. In AS_COMPRESS the
	 * classic automaton reads the direction bits as "which jump": forward, the
	 * back flip, a side flip. Exactly one bit, and nothing turns.
	 * Armed: a weapon has to be in her hand for AS_COMPRESS to read a flip
	 * at all, see rule 4's own comment in modern_move.cpp - so every
	 * TROne-ported case below sets sit.drawn = 1 to stay inside that case;
	 * jumping()'s own two-case split for the unarmed answer is further
	 * down, beside TROne's side-flip expectation.
	 * aim_strafe is set by hand: one case in this group - "firing without
	 * the jump key aims again" - is about the aim coming back after the
	 * flip, which has no meaning unless the rule is on. The flips
	 * themselves do not care either way, which the same case proves by
	 * still coming out as flips with it on. */
	ModernDefaults(&cfg);
	cfg.aim_strafe = 1;					/* explicit: the shipped default is 0 */
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	sit.classic = 0;
	sit.drawn = 1;
	sit.action = 0;
	sit.jump = 1;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_JUMP, "the jump mode", out.mode, MODERN_MODE_JUMP);
	ok(out.bits == MODERN_IN_BACK, "stick back: the back flip, not an about-turn",
		out.bits, MODERN_IN_BACK);

	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.bits == MODERN_IN_RIGHT, "stick right: the side flip", out.bits, MODERN_IN_RIGHT);

	tick(&cfg, &state, 0, -S(30000), 0, 0, &sit, &out);
	ok(out.bits == MODERN_IN_FORWARD, "stick forward: the forward jump", out.bits,
		MODERN_IN_FORWARD);

	sit.anim_state = 1;					/* AS_RUN */
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "in the run it is a running jump and the course decides",
		out.mode, MODERN_MODE_RUN);

	/* TROne: modern.back_and_jump_from_a_stand_is_a_backflip. The same case
	 * from a clean state, because it is the case that matters most:
	 * back plus jump must NOT become "turn round and jump forward". */
	ModernResetState(&state);
	sit.anim_state = 2;
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok(out.bits == MODERN_IN_BACK, "back plus jump from a stand is the back flip",
		out.bits, MODERN_IN_BACK);
	ok(out.turning == 0, "...and nothing is turning", out.turning, 0);

	/* TROne: modern.compress_keeps_the_jump_direction_after_the_key_is_released.
	 * AS_COMPRESS lasts several ticks and a tap of the jump key is only in the
	 * first of them. Without the state in the branch's condition the rest of
	 * them would fall into the run case, set IN_FORWARD, and turn every back
	 * flip into a forward jump. */
	ModernResetState(&state);
	sit.anim_state = 15;				/* AS_COMPRESS */
	sit.jump = 0;						/* the key is already let go */
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_JUMP, "AS_COMPRESS keeps the jump branch without the key",
		out.mode, MODERN_MODE_JUMP);
	ok(out.bits == MODERN_IN_BACK, "...so the direction survives the tap", out.bits,
		MODERN_IN_BACK);

	/* TROne: modern.standing_jump_is_a_side_flip_with_and_without_a_drawn_gun.
	 *
	 * TROne's own line that disarms aiming on a jump is
	 * trone_modern_glue.h:473, outside the profile block that forces
	 * aim_strafe off for the Remaster, and their own test builds a plain
	 * modern_test_glue() that never sets remaster_profile - so their
	 * expectation is unconditional. Measured against a Remaster recording
	 * of jumps with and without weapons: armed, the side flip stands as
	 * TROne's; unarmed, it is rule 6 (the run branch) instead - turn onto
	 * the course, IN_FORWARD, and a forward jump rather than a flip. So
	 * TROne's one case is now two of ours. */
	ModernResetState(&state);
	sit.anim_state = 2;
	sit.jump = 1;
	sit.action = 0;
	sit.drawn = 1;
	state.have_ref = 0;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_JUMP, "armed: still TROne's side flip", out.mode,
		MODERN_MODE_JUMP);
	ok(out.bits == MODERN_IN_RIGHT, "...exactly one direction bit and nothing else",
		out.bits, MODERN_IN_RIGHT);

	/* Unarmed: a moderate offset so the one tick can show both halves of the
	 * rule at once - turning AND the forward jump together -
	 * rather than only the first few ticks of a wide turn, where stand_spin
	 * (0x3000) would still suppress
	 * IN_FORWARD on its own merits. That hysteresis is not what this test is
	 * about; it only has to stay clear of it to show rule 6's ordinary
	 * answer. */
	ModernResetState(&state);
	sit.drawn = 0;
	state.have_ref = 0;
	tick(&cfg, &state, 20, -120, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "unarmed: rule 6 answers, not a flip", out.mode,
		MODERN_MODE_RUN);
	ok(out.bits == (MODERN_IN_FORWARD | MODERN_IN_RIGHT),
		"...she turns toward the stick and jumps forward, not sideways",
		out.bits, MODERN_IN_FORWARD | MODERN_IN_RIGHT);
	ok(out.target == state.course, "...on the held course, the way rule 6 always steers",
		out.target, state.course);

	/* The case the first pass got wrong: gun out AND the action held, i.e. she
	 * is firing, and she jumps anyway. The flip has to stay a flip - IN_RSTEP
	 * is not a direction to game\lara.cpp's lara_as_compress, so a side STEP
	 * here is a jump straight up. Both the side flip and the back flip. */
	sit.drawn = 1;
	sit.action = 1;
	state.have_ref = 0;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_JUMP, "firing + jump: the flip wins over the aim",
		out.mode, MODERN_MODE_JUMP);
	ok(out.bits == MODERN_IN_RIGHT, "...so the bit is the side FLIP, not the side step",
		out.bits, MODERN_IN_RIGHT);

	state.have_ref = 0;
	tick(&cfg, &state, 0, S(32000), 0, 0, &sit, &out);
	ok(out.bits == MODERN_IN_BACK, "...and the back flip survives firing too", out.bits,
		MODERN_IN_BACK);

	/* And the aim comes straight back the moment the jump key is not the
	 * question any more - the disarm is one tick's rule, not a mode. */
	sit.jump = 0;
	sit.anim_state = 2;
	state.have_ref = 0;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_AIM, "firing without the jump key aims again", out.mode,
		MODERN_MODE_AIM);
	ok(out.bits == MODERN_IN_RSTEP, "...and that IS the side step", out.bits,
		MODERN_IN_RSTEP);
	sit.jump = 1;

	/* jump_dir = 0 takes the directional jump out altogether. */
	cfg.jump_dir = 0;
	sit.drawn = 0;
	sit.action = 0;
	ModernResetState(&state);
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "jump_dir 0 puts the run branch back", out.mode,
		MODERN_MODE_RUN);
}

/* ------------------------------------------------- the states we leave be -- */

static void classic_states()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	int i;

	printf("the states that stay classic\n");

	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	/* TROne: modern.classic_states_keep_the_original_input, second half, and
	 * modern.classic_situations_report_inactive_so_the_host_can_fill_in, last
	 * half. Standing, running, the running jump, the crouch, the crawl: these
	 * get the translation. */
	{
		const short modern[] = { 0, 1, 2, 3, 15, 71, 80, 81 };

		for (i = 0; i < (int)(sizeof(modern) / sizeof(modern[0])); i++)
		{
			sit.anim_state = modern[i];
			state.have_ref = 0;
			tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
			ok(out.active == 1, "a state on foot is ours", out.active, 1);
		}
	}

	/* TROne: modern.classic_states_keep_the_original_input, first half, and
	 * modern.water_and_inventory_leave_pose_and_original_input_alone. The
	 * caller's own veto - the water, a vehicle, the inventory - is answered
	 * with active 0 and no bits at all, so that the seam leaves the
	 * classic bits of specific\input.cpp exactly where they are. */
	sit.anim_state = 2;
	sit.classic = 1;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.active == 0, "the caller's veto: not responsible", out.active, 0);
	ok(out.bits == 0, "...and not one bit written", out.bits, 0);
	ok(out.mode == MODERN_MODE_OFF, "...mode off", out.mode, MODERN_MODE_OFF);

	/* The veto also forgets the held course, so that surfacing from the water
	 * and pushing the stick seats a fresh one instead of running off on the
	 * heading she had before she dived. */
	sit.classic = 0;
	state.have_ref = 0;
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(state.have_ref == 1, "a push on land seats a course", state.have_ref, 1);
	sit.classic = 1;
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(state.have_ref == 0, "the veto forgets it again", state.have_ref, 0);

	/* The state table itself: an explicit switch in game\modern_move.cpp,
	 * every classic entry named after the
	 * routine it runs in game\lara.cpp. Checked here by name against the
	 * TROne expectations this file was waiting on, and then as a full sweep
	 * against a reference table of our own, so "all 139 states classified"
	 * is a swept fact and not a sample. */

	/* TROne: modern.shared_state_numbers. Standing (2/15) is already
	 * ModernStateIsStanding's own job; this is the classic/modern
	 * half. Note on 25/26/27: TROne's own list and this
	 * very expectation both call them classic - the back flip and the two
	 * side flips read a direction bit as "end the flip", not "where to go"
	 * (game\lara.cpp's AS_TWIST reads IN_RIGHT, its mirror reads IN_LEFT,
	 * both also read IN_FORWARD|IN_ROLL). The switch measures the routines
	 * rather than typing "25-29 are modern" in as a test, and what it
	 * measured is what is asserted below. */
	{
		/* One number has moved sides, deliberately: 17 AS_SWIM. TROne's
		 * expectation puts it with the classic states, because their engine
		 * has no camera pitch to swim by; ours now does, so the swim is
		 * translated like anything else and 17 is asserted on the modern
		 * side here. A divergence from a ported expectation, named rather
		 * than quietly dropped; the rest of that expectation stands
		 * unchanged. */
		const short is_classic[] = { 10, 25, 26, 27, 36, 57, 76 };
		const short is_modern[] = { 2, 3, 17, 81 };

		for (i = 0; i < (int)(sizeof(is_classic) / sizeof(is_classic[0])); i++)
			ok(ModernStateIsClassic(is_classic[i]) == 1,
				"shared_state_numbers: classic 10/25/26/27/36/57/76",
				ModernStateIsClassic(is_classic[i]), 1);

		for (i = 0; i < (int)(sizeof(is_modern) / sizeof(is_modern[0])); i++)
			ok(ModernStateIsClassic(is_modern[i]) == 0,
				"shared_state_numbers: not 2/3/81, and not 17 now that the swim is translated",
				ModernStateIsClassic(is_modern[i]), 0);
	}

	/* TROne: modern.classic_situations_report_inactive_so_the_host_can_fill_in,
	 * the two state numbers this file owed until now: 57 the ladder, 95 the
	 * turn switch - both must come back active = 0 by their OWN anim_state,
	 * not only through the caller's sit.classic veto tested above. */
	{
		const short states[] = { 57, 95 };

		for (i = 0; i < (int)(sizeof(states) / sizeof(states[0])); i++)
		{
			sit.anim_state = states[i];
			sit.classic = 0;
			state.have_ref = 0;
			tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
			ok(out.active == 0, "classic_situations_report_inactive: state 57/95",
				out.active, 0);
			ok(out.bits == 0, "...and not one bit written (57/95)", out.bits, 0);
		}
	}

	/* TROne: modern.water_and_inventory_leave_pose_and_original_input_alone,
	 * the state half (the caller's-veto half is above, under "the veto also
	 * forgets the held course"). The inventory half is what still holds
	 * unchanged - it arrives as the caller's veto, tested above. The water
	 * half has since turned over: 17 AS_SWIM now asks for the translation by
	 * its own anim_state, and the pose it leaves alone is the one the seam
	 * still does not touch (the automaton's animation - the scheme writes
	 * angles, never a frame). */
	sit.anim_state = 17;
	sit.classic = 0;
	state.have_ref = 0;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.active == 1, "AS_SWIM(17) is the translation's now",
		out.active, 1);
	ok(out.mode == MODERN_MODE_RUN, "...and it is the ordinary run branch",
		out.mode, MODERN_MODE_RUN);

	/* The water group. The eight states the
	 * player really steers in the water are translated like anything on foot
	 * - the same held course, the same run branch - and what makes them the
	 * water is ModernWaterMove afterwards, not a different translation. */
	{
		const short steered[] = { 13, 17, 18, 33, 34, 47, 48, 49 };

		for (i = 0; i < (int)(sizeof(steered) / sizeof(steered[0])); i++)
		{
			sit.anim_state = steered[i];
			sit.classic = 0;
			state.have_ref = 0;
			tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
			ok(out.active == 1, "a water state the player steers is ours",
				out.active, 1);
			ok(out.mode == MODERN_MODE_RUN, "...the ordinary run branch",
				out.mode, MODERN_MODE_RUN);
			ok(ModernWaterTurnRate(&cfg, steered[i]) > 0,
				"...and the water's table knows its rate",
				ModernWaterTurnRate(&cfg, steered[i]), 1);
		}
	}

	/* And the four that look like water and are not steered like it: the two
	 * dives and the climb-out are the handover, the roll is a fixed maneuver,
	 * the death is nobody's. 118 (DOZY's flight) is with the rest of TR5's own
	 * states in the full sweep below. */
	{
		const short handover[] = { 35, 44, 52, 53, 55, 66 };

		for (i = 0; i < (int)(sizeof(handover) / sizeof(handover[0])); i++)
		{
			sit.anim_state = handover[i];
			sit.classic = 0;
			state.have_ref = 0;
			tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
			ok(out.active == 0, "a water handover stays classic", out.active, 0);
			ok(out.mode == MODERN_MODE_OFF, "...mode off, not just inactive",
				out.mode, MODERN_MODE_OFF);
			ok(ModernWaterTurnRate(&cfg, handover[i]) == 0,
				"...and the water's table does not claim it",
				ModernWaterTurnRate(&cfg, handover[i]), 0);
		}
	}

	/* Harmless but listed: Busy states where the input
	 * mask already strips everything but the four direction bits, so this is
	 * measured to be inconsequential either way and named so nobody removes
	 * it later thinking it was missed. 98 shares AS_PICKUP's own routine. */
	{
		const short busy[] = { 39, 40, 41, 42, 43, 50, 51, 67, 70, 98 };

		for (i = 0; i < (int)(sizeof(busy) / sizeof(busy[0])); i++)
			ok(ModernStateIsClassic(busy[i]) == 1, "harmless-but-listed Busy state",
				ModernStateIsClassic(busy[i]), 1);
	}

	/* All 139, against a reference of our own. Not a sample: every index
	 * 0..138 against a frozen array written separately from the same state list
	 * as the switch in modern_move.cpp, so a slip in either one
	 * shows up as a mismatch here rather than as the same mistake made twice
	 * and agreeing with itself.
	 *
	 * The water group takes eight states: 13/17/18 under the water and
	 * 33/34/47/48/49 on it. 35, 44, 52, 53, 55 and 66 stay classic (see the
	 * two groups above), so the count is 87 and not 95. */
	{
		const short classic[] =
		{
			10, 25, 26, 27, 30, 31, 35, 36, 37, 38, 39, 40,
			41, 42, 43, 44, 50, 51, 52, 53, 55, 56, 57, 58, 59, 60,
			61, 66, 67, 70, 75, 76, 77, 78, 79, 82, 83, 87, 88, 89, 90, 91, 92,
			93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 107, 108, 109,
			110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122,
			123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
			136, 137, 138
		};
		unsigned char want[139];

		for (i = 0; i < 139; i++)
			want[i] = 0;

		for (i = 0; i < (int)(sizeof(classic) / sizeof(classic[0])); i++)
			want[classic[i]] = 1;

		for (i = 0; i <= 138; i++)
			ok(ModernStateIsClassic((short)i) == want[i],
				"all 139 states classified: matches the reference table",
				ModernStateIsClassic((short)i), want[i]);

		ok((int)(sizeof(classic) / sizeof(classic[0])) == 87,
			"...87 of 139 are classic, 52 are modern",
			(int)(sizeof(classic) / sizeof(classic[0])), 87);
	}
}

/* ------------------------------------------------------------- the water -- */

/* A result as ModernTranslate would have left it, so that ModernWaterMove can
 * be driven at an exact error without a camera and a stick behind it. Every
 * field is filled, because the water reads three of them and rewrites a
 * fourth. */
static void water_out(MODERN_RESULT* out, long delta, long bits)
{
	out->bits = bits;
	out->active = 1;
	out->target = 0;
	out->delta = (short)delta;
	out->mag = MODERN_STICK_FULL;
	out->reference = 0;
	out->mode = MODERN_MODE_RUN;
	out->blocked = 0;
	out->turning = 0;
}

/* The water. TROne have no expectation here at all - their water is the
 * classic input - so every number below is our own measurement of
 * game\laraswim.cpp and game\larasurf.cpp, and the point of the group is
 * that the modern swimmer moves at TR5's own speeds and only the scheme
 * changes: the control under water is exactly the same as on land, with no
 * difference. */
static void water_move()
{
	MODERN_CFG cfg;
	MODERN_CAMERA_CFG ccfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	long yaw, xrot, stroke;
	long rem;
	int ticks;
	int under;
	int surf;
	int i;

	printf("the water: the flat circle, the 3D pointer and the stroke\n");

	ModernDefaults(&cfg);
	ModernCameraDefaults(&ccfg);
	ModernResetState(&state);
	sit.anim_state = 17;
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	/* The table. Two of the three rates are still the engine's own - 728 is
	 * what lara_as_surftread and lara_as_surfswim write into y_rot themselves
	 * and 364 is what the three side and back strokes write - and the third
	 * is not any more. 1092 was LaraUnderWater's ceiling on lara.turn_rate,
	 * which is an accumulator the modern path never uses: it writes y_rot
	 * itself and zeroes turn_rate. Mirroring that lid cost 319 of 452
	 * measured water ticks pinned at exactly 6 degrees while pushed at a
	 * median of 44 degrees off course. 2730 is 15 degrees a tick, half the
	 * land's new 30. The surface keeps its two, because there the numbers
	 * are rates and not lids (the states write y_rot directly, with no
	 * accumulator and no clamp). */
	{
		const short swim[] = { 13, 17, 18 };
		const short surface[] = { 33, 34 };
		const short side[] = { 47, 48, 49 };
		const short dry[] = { 0, 1, 2, 15, 35, 44, 52, 53, 55, 66, 118 };

		for (i = 0; i < (int)(sizeof(swim) / sizeof(swim[0])); i++)
		{
			ok(ModernWaterTurnRate(&cfg, swim[i]) == 2730,
				"under water she turns at the modern 2730, 15 degrees a tick",
				ModernWaterTurnRate(&cfg, swim[i]), 2730);
			ok(ModernStateIsSwim(swim[i]) == 1, "...and counts as swimming",
				ModernStateIsSwim(swim[i]), 1);
		}

		for (i = 0; i < (int)(sizeof(surface) / sizeof(surface[0])); i++)
		{
			ok(ModernWaterTurnRate(&cfg, surface[i]) == 728,
				"treading and swimming on the surface turn at 728",
				ModernWaterTurnRate(&cfg, surface[i]), 728);
			ok(ModernStateIsSwim(surface[i]) == 0, "...and are not swimming",
				ModernStateIsSwim(surface[i]), 0);
		}

		for (i = 0; i < (int)(sizeof(side) / sizeof(side[0])); i++)
			ok(ModernWaterTurnRate(&cfg, side[i]) == 364,
				"the back and side strokes turn at 364",
				ModernWaterTurnRate(&cfg, side[i]), 364);

		for (i = 0; i < (int)(sizeof(dry) / sizeof(dry[0])); i++)
		{
			ok(ModernWaterTurnRate(&cfg, dry[i]) == 0,
				"and nothing else is the water's",
				ModernWaterTurnRate(&cfg, dry[i]), 0);
			ok(ModernStateIsSwim(dry[i]) == 0, "...nor swimming",
				ModernStateIsSwim(dry[i]), 0);
		}
	}

	/* A state the water does not own is not touched. The seam branches on the
	 * same table, so this can only ever be reached by a caller that has not,
	 * and then the honest answer is "not mine". */
	sit.anim_state = 1;		// AS_RUN
	water_out(&out, 0x4000, MODERN_IN_FORWARD);
	ok(ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke) == 0,
		"a land state is not the water's", 0, 0);
	ok(out.bits == MODERN_IN_FORWARD, "...and its bits stand", out.bits, MODERN_IN_FORWARD);
	ok(yaw == 0, "...no turn", yaw, 0);

	/* The stick at rest is not a course. She keeps heading, attitude and
	 * speed, and the classic bits of specific\input.cpp stand - the same
	 * answer the translation gives on land when it is not responsible. */
	sit.anim_state = 17;
	water_out(&out, 0x4000, MODERN_IN_BACK);
	out.active = 0;
	ok(ModernWaterMove(&cfg, &sit, &out, 8000, 3000, &yaw, &xrot, &stroke) == 0,
		"the stick at rest: not the water's tick either", 0, 0);
	ok(xrot == 3000, "...her attitude is left where it was", xrot, 3000);
	ok(stroke == 0, "...and no stroke is held", stroke, 0);
	ok(out.bits == MODERN_IN_BACK, "...and not one bit written", out.bits, MODERN_IN_BACK);

	/* The turn. Clamped to the state's rate, exact under it, and the sign is
	 * the error's - the same ModernStepToward the land's turn uses. */
	water_out(&out, 0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == 2730, "90 degrees to go: one tick is the swim rate", yaw, 2730);

	water_out(&out, -0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == -2730, "...and the other way round", yaw, -2730);

	water_out(&out, 500, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == 500, "under the rate the whole error is taken", yaw, 500);
	ok(out.turning == 0, "...and nothing is left to turn", out.turning, 0);

	water_out(&out, 0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(out.turning == 1, "with 90 degrees to go she is still turning",
		out.turning, 1);

	/* No instant about-turn in the water, which is the one rule of the land's
	 * turn that is deliberately not here: past 90 degrees the land takes the
	 * whole error in one tick, and under water a one-tick half turn is the
	 * AS_WATERROLL animation's job and looks wrong without it. */
	water_out(&out, 0x7FFF, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == 2730, "half a turn away: still only one tick's worth", yaw, 2730);

	/* The bits. Under water IN_FORWARD is SwimTurn's PITCH (x_rot -= 364), so
	 * it may never go out down there however well she is aligned; on the
	 * surface it really is forward and it is also the stroke. IN_WALK is
	 * untouched in both, exactly as on land. */
	sit.anim_state = 17;
	water_out(&out, 0, MODERN_IN_FORWARD | MODERN_IN_LEFT | MODERN_IN_WALK);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(out.bits == MODERN_IN_WALK, "under water: no direction bit survives, walk does",
		out.bits, MODERN_IN_WALK);

	sit.anim_state = 33;
	water_out(&out, 0, MODERN_IN_BACK | MODERN_IN_RSTEP | MODERN_IN_WALK);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(out.bits == (MODERN_IN_FORWARD | MODERN_IN_WALK),
		"on the surface: forward is forward again",
		out.bits, MODERN_IN_FORWARD | MODERN_IN_WALK);

	water_out(&out, 0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(out.bits == 0, "...but not while she is still turning onto the course",
		out.bits, 0);
	ok(yaw == 728, "...and the surface rate is 728", yaw, 728);

	water_out(&out, 0x2000 + 728, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(out.bits == MODERN_IN_FORWARD, "...it comes back at the last 45 degrees",
		out.bits, MODERN_IN_FORWARD);

	/* The stroke: the left stick moves Lara in the direction held, so the
	 * stroke is held for as long as the stick is out. Under water only - on
	 * the surface the stroke IS the forward bit, and IN_JUMP there means
	 * "dive" (ten ticks of it, larasurf.cpp). */
	sit.anim_state = 18;	// AS_GLIDE
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(stroke == 1, "under water the stroke is held while the stick is out",
		stroke, 1);

	sit.anim_state = 33;
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(stroke == 0, "on the surface it is not - IN_JUMP there is the dive",
		stroke, 0);

	cfg.water_auto_stroke = 0;
	sit.anim_state = 18;
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(stroke == 0, "switched off, the jump key is the stroke again", stroke, 0);
	ok(yaw == 0, "...and nothing else about the tick changes", yaw, 0);
	ModernDefaults(&cfg);

	/* THE 3D POINTER. The orbit's elevation, turned into a body attitude by
	 * the camera's own ModernCameraSwimPitch, and walked towards at the modern
	 * pitch rate - SwimTurn's own 364, doubled to 728 (364 is the rate of a
	 * held bit, and ours is a pointer the body chases). The conversion is
	 * the inverse of the engine's own line: ChaseCamera shows elevation =
	 * pitch_home + x_rot, so the attitude that belongs to an elevation is
	 * elevation - pitch_home, and a camera nobody has touched points at
	 * level swimming. */
	ok(ModernCameraSwimPitch(&ccfg, ccfg.pitch_home) == 0,
		"a camera left alone points at level swimming",
		ModernCameraSwimPitch(&ccfg, ccfg.pitch_home), 0);

	for (i = -15000; i <= 15000; i += 1000)
		ok(ModernCameraSwimPitch(&ccfg, ccfg.pitch_home + i) == i,
			"the conversion inverts ChaseCamera's own elevation line",
			ModernCameraSwimPitch(&ccfg, ccfg.pitch_home + i), i);

	sit.anim_state = 17;
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, ModernCameraSwimPitch(&ccfg, ccfg.pitch_home + 4000),
		4000, &yaw, &xrot, &stroke);
	ok(xrot == 4000, "a camera sitting where the classic would put it asks for nothing",
		xrot, 4000);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 5000, 0, &yaw, &xrot, &stroke);
	ok(xrot == 728, "one tick towards the pointer is the modern pitch rate", xrot, 728);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, -5000, 0, &yaw, &xrot, &stroke);
	ok(xrot == -728, "...and the same nose down", xrot, -728);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 100, 0, &yaw, &xrot, &stroke);
	ok(xrot == 100, "under the rate she arrives in one tick", xrot, 100);

	/* The limits are LaraUnderWater's own (+-15470), and the target is clamped
	 * BEFORE the step, so the clamp is a place she can sit rather than a wall
	 * the step stops at. It bites at the top by exactly pitch_home: the orbit
	 * may ask for 17290 and her body stops at 15470. */
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, ModernCameraSwimPitch(&ccfg, MODERN_CAMERA_PITCH_MAX),
		15470, &yaw, &xrot, &stroke);
	ok(xrot == 15470, "the camera at its own top: her body stops at the clamp",
		xrot, 15470);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 20000, 15470, &yaw, &xrot, &stroke);
	ok(xrot == 15470, "...and nothing beyond it moves her further", xrot, 15470);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, -20000, -15470, &yaw, &xrot, &stroke);
	ok(xrot == -15470, "...nor the other way", xrot, -15470);

	/* The surface is flat: the course on it stays flat, so
	 * nothing there writes x_rot at all, whatever the orbit is pointing at,
	 * and the engine keeps her level anyway. */
	sit.anim_state = 33;
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, -12000, 4000, &yaw, &xrot, &stroke);
	ok(xrot == 4000, "the surface does not follow the pointer", xrot, 4000);

	/* Two things the translation itself had to be told about the water.
	 * A flare is held at LG_READY, so sit.drawn can be 1 down there and the
	 * aim branch would have strafed her around nothing; and with back_turns
	 * switched off the back branch would have sent IN_BACK, which under water
	 * is "pitch her nose up". Both are refused by the state, and the land
	 * cases beside them prove the refusal is the water's and not a change of
	 * the rule.
	 *
	 * aim_strafe is forced on for this pair: with the shipped default the
	 * land case would answer RUN as well and the contrast would prove
	 * nothing; with the rule on, the water still refuses it, which is the
	 * claim - the water's refusal is the state's and not the switch's. */
	sit.anim_state = 17;
	sit.drawn = 1;
	sit.action = 1;
	cfg.aim_strafe = 1;					/* explicit: the shipped default is 0 */
	state.have_ref = 0;
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "a flare and the action key: still the run branch",
		out.mode, MODERN_MODE_RUN);

	sit.anim_state = 2;		// AS_STOP, the same situation on land
	state.have_ref = 0;
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_AIM, "...and on land it is still aiming",
		out.mode, MODERN_MODE_AIM);

	sit.drawn = 0;
	sit.action = 0;
	cfg.aim_strafe = 0;					/* back to the shipped default */
	cfg.back_turns = 0;
	sit.anim_state = 17;
	state.have_ref = 0;
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);	// stick pulled back
	ok(out.mode == MODERN_MODE_RUN, "back on the stick under water is not a step back",
		out.mode, MODERN_MODE_RUN);

	sit.anim_state = 2;
	state.have_ref = 0;
	tick(&cfg, &state, 0, S(30000), 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_BACK, "...and on land, with the switch off, it is",
		out.mode, MODERN_MODE_BACK);
	ModernDefaults(&cfg);

	/* What it costs, measured by running it. A half turn under water and the
	 * same on the surface, tick by tick through the function itself rather
	 * than by dividing two numbers. */
	sit.anim_state = 17;
	rem = 0x7FFF;
	ticks = 0;

	while (rem > 0 && ticks < 1000)
	{
		water_out(&out, rem, 0);
		ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
		rem -= absl(yaw);
		ticks++;
	}

	under = ticks;
	/* 32767/2730 is 12.005 - twelve whole rates and a thirteenth tick for
	 * the seven units that are left. It was 31 at the old rate of 1092. */
	ok(under == 13, "a half turn under water takes 13 ticks", under, 13);

	sit.anim_state = 33;
	rem = 0x7FFF;
	ticks = 0;

	while (rem > 0 && ticks < 1000)
	{
		water_out(&out, rem, 0);
		ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
		rem -= absl(yaw);
		ticks++;
	}

	surf = ticks;
	ok(surf == 46, "and on the surface 46", surf, 46);

	printf("  measured: half a turn costs %d ticks under water (%.2f s) and %d on"
		" the surface (%.2f s)\n", under, under / 30.0, surf, surf / 30.0);

	sit.anim_state = 17;
	xrot = 0;
	ticks = 0;

	while (xrot < 15470 && ticks < 1000)
	{
		const long from = xrot;

		water_out(&out, 0, 0);
		ModernWaterMove(&cfg, &sit, &out, 20000, from, &yaw, &xrot, &stroke);
		ticks++;
	}

	ok(ticks == 22, "level to straight up is 22 ticks", ticks, 22);
	printf("  measured: level to the pitch limit is %d ticks (%.2f s) at %ld a tick\n",
		ticks, ticks / 30.0, cfg.swim_pitch_rate);
}

/* ------------------------------------------------------ back on the stick -- */

static void back_input()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_RESULT out;

	printf("back on the stick turns her round\n");

	/* TROne: modern.back_input_turns_around_unless_switched_off. */
	ModernDefaults(&cfg);
	ModernResetState(&state);

	tick(&cfg, &state, 0, S(30000), 0, 0, 0, &out);
	ok(out.mode == MODERN_MODE_RUN, "back is the run branch", out.mode, MODERN_MODE_RUN);
	ok((out.bits & MODERN_IN_BACK) == 0, "...no step backwards", out.bits & MODERN_IN_BACK, 0);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) != 0, "...she turns round instead",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), MODERN_IN_LEFT);

	cfg.back_turns = 0;
	state.have_ref = 0;
	tick(&cfg, &state, 0, S(30000), 0, 0, 0, &out);
	ok(out.mode == MODERN_MODE_BACK, "back_turns 0: the classic step backwards",
		out.mode, MODERN_MODE_BACK);
	ok((out.bits & ~(long)MODERN_IN_WALK) == MODERN_IN_BACK, "...and IN_BACK is the bit",
		out.bits & ~(long)MODERN_IN_WALK, MODERN_IN_BACK);

	/* TROne: modern.remaster_back_input_distinguishes_running_from_armed_hopback
	 * rides on this switch - their Remaster profile sets back_turns from
	 * "is a weapon held". Ours is a constant (MODERN_BACK_TURNS), because this
	 * engine has no pointer aiming to hop away from; the switch is here for a
	 * profile that aims with a pointer, which this engine does not have. The
	 * sector the rule fires in is asserted either side, because it is the one
	 * place a half turn and a step apart differ by a single unit. */
	state.have_ref = 0;
	tick(&cfg, &state, 0, S(30000), 0, (short)0x1FFF, 0, &out);
	ok(out.mode == MODERN_MODE_BACK, "45 degrees off straight back is still back",
		out.mode, MODERN_MODE_BACK);
	state.have_ref = 0;
	tick(&cfg, &state, 0, S(30000), 0, (short)0x2100, 0, &out);
	ok(out.mode == MODERN_MODE_RUN, "past the quadrant it is a turn again", out.mode,
		MODERN_MODE_RUN);
}

/* ---------------------------------------------------------- small angles --- */

static void chatter()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_RESULT out;

	printf("small angles do not chatter\n");

	/* TROne: modern.small_angles_do_not_chatter. Without the hysteresis the
	 * turn bit flickers on the window's edge at tick rate, which is a visible
	 * twitch while she runs in a straight line. */
	ModernDefaults(&cfg);
	ModernResetState(&state);

	tick(&cfg, &state, 0, -S(30000), 0, 0, 0, &out);
	ok(state.aligned == 1, "straight ahead: aligned", state.aligned, 1);

	tick(&cfg, &state, 0, -S(30000), 0, -0x0500, 0, &out);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) == 0,
		"just over the window, once aligned: no turn",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), 0);

	tick(&cfg, &state, 0, -S(30000), 0, -0x0C00, 0, &out);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "over twice the window: she turns again",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);
	ok(state.aligned == 0, "...and is no longer aligned", state.aligned, 0);

	/* Ours: the window itself, from a state that is NOT aligned - the edge is
	 * "greater than", so an error exactly on it counts as aligned. */
	ModernResetState(&state);
	tick(&cfg, &state, 0, -S(30000), 0, (short)-0x0400, 0, &out);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) == 0, "an error of exactly the window",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), 0);
	ModernResetState(&state);
	tick(&cfg, &state, 0, -S(30000), 0, (short)-0x0401, 0, &out);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "...and one unit past it",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);
}

/* --------------------------------------------------------- the wall case --- */

static void blocked()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;

	printf("blocked - the turn goes out alone or she stops answering\n");

	/* TROne: modern.a_blocked_step_turns_instead_of_pushing_the_wall. With
	 * something in front of her, IN_FORWARD keeps the classic automaton in
	 * AS_STOP/AS_SPLAT and the turn never happens - TROne measured 210 frozen
	 * frames. Our game\lara.cpp has the same lines (lara_as_stop overwrites
	 * the turn goal with run or walk when IN_FORWARD is set), so this is not
	 * optional for us either.
	 *
	 * Stick right (0x4000) under a camera on -0x3800 gives the world course
	 * 0x0800; Lara faces 0, so the error is 0x0800 - over the aligned window,
	 * far under the spin threshold. That is exactly the band where forward AND
	 * a turn could go out together. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	tick(&cfg, &state, S(30000), 0, (short)-0x3800, 0, &sit, &out);
	ok(out.delta == 0x0800, "the band: 11 degrees of error", out.delta, 0x0800);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "free: she runs", out.bits & MODERN_IN_FORWARD,
		MODERN_IN_FORWARD);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "free: and turns", out.bits & MODERN_IN_RIGHT,
		MODERN_IN_RIGHT);
	ok(out.blocked == 0, "free: nothing is blocked", out.blocked, 0);

	sit.blocked = 1;
	tick(&cfg, &state, S(30000), 0, (short)-0x3800, 0, &sit, &out);
	ok(out.blocked == 1, "blocked: the rule fires", out.blocked, 1);
	ok(out.bits == MODERN_IN_RIGHT, "blocked: the turn bit ALONE, so AS_TURN_R is reachable",
		out.bits, MODERN_IN_RIGHT);

	tick(&cfg, &state, S(30000), 0, (short)-0x3800, (short)0x0800, &sit, &out);
	ok(out.delta == 0, "aligned against the wall", out.delta, 0);
	ok(out.blocked == 0, "...the rule lapses", out.blocked, 0);
	ok((out.bits & MODERN_IN_FORWARD) != 0,
		"...and pressing into the wall is classic behaviour again",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);

	cfg.block_turn = 0;
	state.aligned = 0;
	tick(&cfg, &state, S(30000), 0, (short)-0x3800, 0, &sit, &out);
	ok(out.blocked == 0, "block_turn 0 restores the behaviour before the repair",
		out.blocked, 0);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...forward goes out again",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "...together with the turn",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);
}

/* ------------------------------------------------------- the direct turn --- */

/* One tick exactly as the seam takes it (game\modern_state.cpp): translate,
 * then offer the result to the direct turn. Returns the direct turn's own
 * answer - 1 when it took charge of the turning - and writes the yaw change
 * this tick into *yaw. Nothing here re-implements the seam; these are the two
 * calls it makes, in its order. */
static long tick_turn(const MODERN_CFG* cfg, MODERN_STATE* state, long x, long y,
	short camera_yaw, short lara_yaw, const MODERN_SITUATION* sit, MODERN_RESULT* out, long* yaw)
{
	ModernTranslate(cfg, state, x, y, camera_yaw, lara_yaw, sit, out);

	return ModernDirectTurn(cfg, state, sit, out, yaw);
}

static void direct_turn()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	const short allowed[] = { 0, 1, 2, 6, 7, 12, 14, 15, 20, 73 };
	const short airborne[] = { 3, 9, 10, 17, 25, 26, 27, 36, 57, 76, 81 };
	long yaw;
	long total;
	long count;
	int i;
	int t;

	printf("the direct turn - the one place the engine writes her heading\n");

	/* TROne: modern.direct_ground_turn_is_bounded_and_never_changes_airborne_
	 * poses. Their four numbers, still driven with THEIR rate (3277) although
	 * ours is 5461 now: the expectation is about the bounding and the state
	 * list, not about the number, and pinning it here keeps it readable as
	 * theirs. What our own rate does is measured in reversal() below. */
	ok(ModernGroundTurn(2, 0x4000, 3277) == 3277, "standing, 90 degrees: the rate and no more",
		ModernGroundTurn(2, 0x4000, 3277), 3277);
	ok(ModernGroundTurn(1, -0x4000, 3277) == -3277, "running, the other way: the rate again",
		ModernGroundTurn(1, -0x4000, 3277), -3277);
	ok(ModernGroundTurn(1, 17, 3277) == 17, "under the rate the whole error is taken",
		ModernGroundTurn(1, 17, 3277), 17);
	ok(ModernGroundTurn(1, 0x4000, 0) == 0, "rate 0 is the feature switched off",
		ModernGroundTurn(1, 0x4000, 0), 0);

	/* Ours: a rate over half a turn would make "clamp the error to the rate"
	 * mean nothing, so the rate is clamped as well. Nothing sets it that
	 * high - this is the guard, asked for by name. */
	ok(ModernGroundTurn(1, 0x7000, 0x7000) == 0x4000, "a rate over half a turn is clamped too",
		ModernGroundTurn(1, 0x7000, 0x7000), 0x4000);

	/* Their twelve states less one: airborne, hanging, climbing, swimming, in
	 * an interaction pose. There y_rot is the animation's and nothing outside
	 * may touch it. 15 AS_COMPRESS, their twelfth, is in
	 * `allowed` below, because ours can reach it with mode RUN (unarmed)
	 * where TROne's design never could. */
	for (i = 0; i < (int)(sizeof(airborne) / sizeof(airborne[0])); i++)
		ok(ModernGroundTurn(airborne[i], 0x4000, 3277) == 0,
			"no write in an airborne, climbing or object pose",
			ModernGroundTurn(airborne[i], 0x4000, 3277), 0);

	/* Their ten ticks for a half turn. 32768 / 3277 is 10.0009, so the
	 * tenth tick lands exactly on it rather than overshooting. */
	yaw = 0;

	for (t = 0; t < 10; t++)
		yaw = ModernWrap(yaw + ModernGroundTurn(2, 0x8000L - yaw, 3277));

	ok(ModernWrap(yaw) == -32768, "ten ticks for 180 degrees at 3277", ModernWrap(yaw), -32768);

	/* Ours, because the state list is ours and not theirs (TR5 runs to 138
	 * where TROne stop at 104): TROne's nine ground states allow the write,
	 * plus 15 AS_COMPRESS (see ModernStateAllowsDirectTurn's own comment in
	 * modern_move.cpp for why) - ten in all, and the ten are the ten. */
	count = 0;

	for (i = 0; i < 139; i++)
		if (ModernStateAllowsDirectTurn((short)i))
			count++;

	ok(count == 10, "ten of the 139 states allow the write", count, 10);

	for (i = 0; i < (int)(sizeof(allowed) / sizeof(allowed[0])); i++)
		ok(ModernStateAllowsDirectTurn(allowed[i]) == 1, "...and these are the ten",
			ModernStateAllowsDirectTurn(allowed[i]), 1);

	/* AS_COMPRESS keeps the write too, but only when she is unarmed and rule
	 * 6 is answering - never during an armed flip, because ModernDirectTurn's
	 * own mode check just below already refuses anything that is not
	 * MODERN_MODE_RUN. Both halves through the seam's own two calls
	 * (tick_turn), not just the state-list membership asserted above, so the
	 * interaction with rule 4's sit->drawn gate is proven end to end. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 15;			/* AS_COMPRESS */
	sit.classic = 0;
	sit.drawn = 0;					/* unarmed: rule 4 steps aside, rule 6 answers */
	sit.action = 0;
	sit.jump = 1;
	sit.blocked = 0;
	sit.cam_player_step = 0;
	ok(tick_turn(&cfg, &state, 20, -120, 0, 0, &sit, &out, &yaw) == 1,
		"AS_COMPRESS, unarmed: the direct turn takes charge too", 1, 1);
	ok(out.mode == MODERN_MODE_RUN, "...because unarmed it is rule 6 answering",
		out.mode, MODERN_MODE_RUN);

	ModernResetState(&state);
	sit.drawn = 1;					/* armed: rule 4 owns the tick instead */
	ok(tick_turn(&cfg, &state, 20, -120, 0, 0, &sit, &out, &yaw) == 0,
		"AS_COMPRESS, armed: still a flip, so no direct turn", 0, 0);
	ok(out.mode == MODERN_MODE_JUMP, "...the jump mode owns the heading instead",
		out.mode, MODERN_MODE_JUMP);

	/* The two other named rate candidates also turn a half circle in a
	 * sensible number of ticks, measured rather than reasoned about.
	 * 32768/1456 = 22.5, so 22 whole rates and a 23rd tick for the remaining
	 * 736; 32768/2184 = 15.003, so 15 whole rates and a 16th tick for the
	 * remaining 8. Ten ticks, sixteen ticks, twenty-three ticks: at 30 Hz
	 * that is a third of a second, half a second and three quarters. */
	yaw = 0;
	total = 0;

	while (ModernWrap(yaw) != -32768 && total < 200)
	{
		yaw = ModernWrap(yaw + ModernGroundTurn(2, 0x8000L - yaw, MODERN_TURN_RATE_VANILLA));
		total++;
	}

	ok(total == 23, "at 1456 (vanilla's fastest) a half turn costs 23 ticks", total, 23);

	yaw = 0;
	total = 0;

	while (ModernWrap(yaw) != -32768 && total < 200)
	{
		yaw = ModernWrap(yaw + ModernGroundTurn(2, 0x8000L - yaw, MODERN_TURN_RATE_MIDDLE));
		total++;
	}

	ok(total == 16, "at 2184 it costs 16", total, 16);

	/* TROne: modern.the_instant_reversal_stays_in_the_run_and_leaves_the_
	 * stand. Full stick back, camera and Lara both on 0, so the error is
	 * exactly half a turn.
	 *
	 * TROne take the whole of it in one tick while she is running and refuse
	 * it while standing, because a one-tick reversal out of a stand is spent
	 * before the player has BACK and JUMP down together and the back flip
	 * becomes unreachable. Our engine instead turns every reversal at the
	 * bounded rate, running or standing: a one-tick snap measured as 36
	 * one-tick steps over 30 degrees in one session, 24 of them over 90 - a
	 * turn too abrupt for the interpolation to keep up with.
	 *
	 * So the snap is gone and the three cases below are the same three, read
	 * the other way: running, a flick, and a flick through the stick's
	 * centre all turn at the rate, and the state list and the course memory
	 * they were written to prove are proven by exactly the same ticks. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 1;					/* AS_RUN - she is really running */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	ok(tick_turn(&cfg, &state, 0, -S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"running: the direct turn takes charge", 1, 1);
	ok(yaw == 0, "...with nothing to do while the stick points where she runs", yaw, 0);

	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"the stick thrown from forward to back: still the direct turn", 1, 1);
	ok(absl(out.course_step) == 0x8000, "...the course itself jumped a half turn",
		absl(out.course_step), 0x8000);
	ok(absl(yaw) == MODERN_TURN_RATE,
		"...and even a flick is turned at the rate now - no one-tick about-turn",
		absl(yaw), MODERN_TURN_RATE);

	/* The course memory is still worth its field, although nothing decides
	 * on it any more: it is MODERNLOG's cstep=, which tells a flick of the
	 * thumb from error that piled up. So it is still asserted - from a fresh
	 * state there is nothing to have jumped from. */
	ModernResetState(&state);
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"a first push with no course behind it: the turn is still ours", 1, 1);
	ok(out.course_step == 0, "...and the course had nothing to jump from",
		out.course_step, 0);
	ok(absl(yaw) == MODERN_TURN_RATE, "...bounded, as every tick is now",
		absl(yaw), MODERN_TURN_RATE);

	/* And a real flick goes through the centre of the stick, which is why the
	 * course memory outlives a release (MODERN_STATE::last_course). Forward,
	 * one tick of nothing at all as the thumb crosses over, then fully back:
	 * the flick is still SEEN as a flick, and is now turned like anything
	 * else. */
	ModernResetState(&state);
	tick_turn(&cfg, &state, 0, -S(32000), 0, 0, &sit, &out, &yaw);
	tick_turn(&cfg, &state, 0, 0, 0, 0, &sit, &out, &yaw);
	ok(out.mode == MODERN_MODE_OFF, "the stick crosses the centre: one dead tick",
		out.mode, MODERN_MODE_OFF);
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"...and the push out the other side is the direct turn again", 1, 1);
	ok(absl(out.course_step) == 0x8000, "...the flick survived the dead tick",
		absl(out.course_step), 0x8000);
	ok(absl(yaw) == MODERN_TURN_RATE, "...and is turned at the rate", absl(yaw),
		MODERN_TURN_RATE);

	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"standing: it takes charge as well", 1, 1);
	ok(absl(yaw) == MODERN_TURN_RATE, "...at the same one rate, standing or running",
		absl(yaw), MODERN_TURN_RATE);
	ok((out.bits & MODERN_IN_FORWARD) == 0, "...and no step forward while she turns",
		out.bits & MODERN_IN_FORWARD, 0);

	/* TROne: modern.ground_reversal_turns_without_a_step_in_the_wrong_
	 * direction, all twelve ticks. A check above already asserted the first tick's
	 * "no step"; this is the rest of it - she arrives, and only then does
	 * forward come back. (At 5461 she is round after seven of the twelve and
	 * the last five turn nothing, which is what "and stays there" is worth;
	 * the loop keeps TROne's own count so that the expectation stays theirs.)
	 *
	 * The mask is MODERN_DIR_MASK and not MODERN_MOVE_MASK on purpose: ours
	 * carries IN_WALK and TROne's does not, and the direct turn deliberately
	 * leaves walking alone (the stick's deflection still decides it). */
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP for every one of the twelve */
	yaw = 0;
	total = 0;

	for (t = 0; t < 12; t++)
	{
		long step = 0;

		tick_turn(&cfg, &state, 0, S(32000), 0, (short)total, &sit, &out, &step);
		total = ModernWrap(total + step);

		if (!t)
		{
			ok(step == -MODERN_TURN_RATE, "the first tick turns by one rate",
				step, -MODERN_TURN_RATE);
			ok((out.bits & MODERN_DIR_MASK) == 0, "...and sends no direction bit at all",
				out.bits & MODERN_DIR_MASK, 0);
		}
	}

	ok(total == -32768, "after twelve ticks she is round", total, -32768);
	ok((out.bits & MODERN_DIR_MASK) == MODERN_IN_FORWARD, "...and forward comes back",
		out.bits & MODERN_DIR_MASK, MODERN_IN_FORWARD);

	/* TROne: modern.held_back_keeps_its_course_when_the_chase_camera_turns -
	 * the turn half (held_course() above asserted the course half over a
	 * swinging camera). Tick 0 is the stand and turns by one rate; from tick 1 she is running,
	 * the camera has cut by 180 degrees, and the held course does not care.
	 *
	 * The course is held through the cut, so it does not jump, and a snap
	 * needs a jump - so the reversal is walked at the bounded rate over ten
	 * ticks instead of being taken whole on tick 1. What has not changed is
	 * the claim TROne's expectation is about: the camera cut never reaches
	 * the course, so she turns the short way to the same heading and stays
	 * there. IN_FORWARD is set on every running tick of it now, where before
	 * it would have been withheld for the ticks with more than 45 degrees
	 * left. Seven ticks at the current rate (it was ten at 3277): 5461 six
	 * times is 32766, and the seventh is clamped to the two units that are
	 * left. */
	ModernResetState(&state);
	yaw = 0;

	for (t = 0; t < 20; t++)
	{
		long step = 0;
		const short cam = (short)(t ? -32768 : 0);

		sit.anim_state = (short)(t ? 1 : 2);
		tick_turn(&cfg, &state, 0, S(32000), cam, (short)yaw, &sit, &out, &step);
		yaw = ModernWrap(yaw + step);

		if (!t)
			continue;

		ok(out.course_step == 0, "held back through a camera cut: the course does not move",
			out.course_step, 0);
		ok(absl(step) <= MODERN_TURN_RATE, "...so nothing is ever taken in one tick",
			absl(step), MODERN_TURN_RATE);
		ok((out.bits & MODERN_DIR_MASK) == MODERN_IN_FORWARD,
			"...running forward on the held course", out.bits & MODERN_DIR_MASK,
			MODERN_IN_FORWARD);

		if (t >= 6)
			ok(yaw == -32768, "...and by the seventh tick she is round, and stays round",
				yaw, -32768);
	}

	/* Ours: the four cases that say the write cannot happen where it must
	 * not. The second gate first: MODERN_DIRECT_TURN off leaves the turn bit
	 * in the word and the automaton in charge, which isolates whether a
	 * fault sits in the direct turn or in the translation itself. */
	ModernDefaults(&cfg);
	cfg.direct_turn = 0;
	ModernResetState(&state);
	sit.anim_state = 1;
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 0,
		"direct_turn 0: no write", 0, 0);
	ok(yaw == 0, "...and no angle", yaw, 0);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) != 0, "...the turn bit stands instead",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), MODERN_IN_LEFT);

	/* A weapon up and the action held do not make the direct turn stand
	 * aside: measured against game\larafire.cpp, game\lara1gun.cpp and
	 * game\lara2gun.cpp, which write lara.left_arm.y_rot, lara.right_arm.y_rot,
	 * lara.torso_y_rot and lara.head_y_rot and never lara_item->pos.y_rot.
	 * Nothing was turning her but the turn bits, which the direct turn
	 * replaces armed or not - so firing steers like empty hands. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.drawn = 1;
	sit.action = 1;
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"weapon up and action held: we turn her, the guns only aim", 1, 1);
	ok(out.mode == MODERN_MODE_RUN, "...in the running mode, not the strafe",
		out.mode, MODERN_MODE_RUN);

	/* A classic state, and the standing jump. Neither is mode RUN, and the
	 * jump is the case that would cost the back flip if it were. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.drawn = 0;
	sit.action = 0;
	sit.classic = 1;
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 0,
		"a classic tick never writes her heading", 0, 0);

	ModernResetState(&state);
	sit.classic = 0;
	sit.anim_state = 2;
	sit.drawn = 1;			/* armed: a flip needs it */
	sit.jump = 1;
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 0,
		"the back flip is not turned into an about-turn", 0, 0);
	ok(out.bits == MODERN_IN_BACK, "...the back bit goes out untouched", out.bits,
		MODERN_IN_BACK);

	/* The spin has to be put down when the direct turn picks the turning up,
	 * and the reason only shows a tick later: the flag is the automaton's way
	 * of turning on the spot, and while the direct turn is in charge it
	 * changes nothing at all - the bits are rewritten from `remaining`
	 * anyway. It bites when the direct turn STOPS, in the air or under a
	 * weapon, and a spin left standing keeps IN_FORWARD off for as long as
	 * the error stays over spin_exit. So: one tick standing with 0x3800 of
	 * error, then the same course with her in a forward jump, where the write
	 * is not allowed. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP - stand_spin 0x3000, so the spin is set */
	sit.jump = 0;
	tick_turn(&cfg, &state, 0, -S(32000), (short)0x3800, 0, &sit, &out, &yaw);
	ok(state.spin == 0, "the direct turn puts the spin down", state.spin, 0);

	sit.anim_state = 3;					/* AS_FORWARDJUMP - modern, but no write in the air */
	ok(tick_turn(&cfg, &state, 0, -S(32000), (short)0x3800, (short)MODERN_TURN_RATE,
		&sit, &out, &yaw) == 0, "...and in the air the automaton has it back", 0, 0);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...running on, not stuck in a spin nobody asked for",
		out.bits & MODERN_IN_FORWARD, MODERN_IN_FORWARD);

	/* And walking survives the rewrite: the direct turn takes the direction
	 * bits, never IN_WALK. Half a stick, a half turn of error, running.
	 * cfg.walk_below is set explicitly, since the shipped default of 0 would
	 * make this tautological - the point here is that IN_WALK, when the
	 * capability is asked for, is not among the bits the direct turn
	 * clears, not that IN_WALK never appears at all. */
	ModernResetState(&state);
	cfg.walk_below = 62;		/* explicit: the shipped default is 0 */
	sit.jump = 0;
	sit.anim_state = 1;
	tick_turn(&cfg, &state, 0, -S(12000), 0, 0, &sit, &out, &yaw);	/* seat a course first */
	tick_turn(&cfg, &state, 0, S(12000), 0, 0, &sit, &out, &yaw);
	ok((out.bits & MODERN_IN_WALK) != 0, "a gentle stick still walks while the turn is ours",
		out.bits & MODERN_IN_WALK, MODERN_IN_WALK);
	/* The reversal still happens, at the rate now and not in one tick. What
	 * this block is about - IN_WALK surviving the rewrite of the bits - is
	 * the same either way. */
	ok(absl(yaw) == MODERN_TURN_RATE, "...and the reversal still happens, at the rate",
		absl(yaw), MODERN_TURN_RATE);

	/* The level boundary: what the reset at the level start is worth.
	 *
	 * The held course is a world heading and a level change throws that
	 * world away. game\modern_state.cpp's tick guard cannot notice: no tick
	 * runs at all between two levels, so the boundary reaches it as an
	 * ordinary step of one. The reset is therefore hung at the level start
	 * itself, in game\gameflow.cpp beside InterpInvalidate("level start").
	 * Nothing in this tool can reach that call - it links the arithmetic and
	 * no engine - so what is asserted here is what the call buys, measured
	 * from the other end: anybody who deletes the call has to delete these
	 * checks with it.
	 *
	 * Level A: the camera on 0x2000, the stick held forward, three ticks
	 * seat the course. Level B: she stands on -0x4000 with the chase camera
	 * behind her, and the player never let go of the stick. The camera is
	 * then held still on purpose - the course is held, so nothing the
	 * camera does can re-seat it.
	 *
	 * The rate is pinned at 3277 here, the way the ten-ticks-for-a-half-turn
	 * check above pins it: two of these numbers are a count of ticks, and a
	 * later change to MODERN_TURN_RATE must not make this block fail for a
	 * reason that has nothing to do with the boundary. */
	ModernDefaults(&cfg);
	cfg.turn_rate = 3277;
	ModernResetState(&state);
	sit.anim_state = 1;					/* AS_RUN - she was running in level A */
	sit.jump = 0;

	{
		MODERN_STATE carried;
		const short lara = -0x4000;
		short heading;
		long forward_at = 0;
		long arrived_at = 0;
		int tk;

		for (tk = 0; tk < 3; tk++)
			tick_turn(&cfg, &state, 0, -S(32000), (short)0x2000, (short)0x2000, &sit, &out, &yaw);

		ok(state.course == 0x2000, "level A seats a world course", state.course, 0x2000);

		/* A level start is a standing pose, and standing keeps the bounded
		 * rate (the snap is only for a real run), so this is the slow,
		 * visible version of the cost. */
		carried = state;
		sit.anim_state = 2;				/* AS_STOP */
		tick_turn(&cfg, &carried, 0, -S(32000), lara, lara, &sit, &out, &yaw);
		ok(out.target == 0x2000, "the next level's first tick answers the OLD course",
			out.target, 0x2000);
		ok(out.delta == 24576, "...135 degrees of error out of nowhere", out.delta, 24576);
		ok(out.bits == 0, "...she does not run at all", out.bits, 0);
		ok(yaw == 3277, "...and turns away from the camera at the full rate", yaw, 3277);

		/* Her heading follows the yaw the direct turn really returned, the way
		 * game\modern_state.cpp writes it - not the rate times the tick
		 * number, because the last tick of a turn is clamped to whatever error
		 * is left and a rate times a count would overshoot it. */
		heading = ModernWrap((long)lara + yaw);

		for (tk = 2; tk <= 12; tk++)
		{
			tick_turn(&cfg, &carried, 0, -S(32000), lara, heading, &sit, &out, &yaw);
			heading = ModernWrap((long)heading + yaw);

			if (!forward_at && (out.bits & MODERN_IN_FORWARD))
				forward_at = tk;

			if (!arrived_at && out.delta == 0)
				arrived_at = tk - 1;
		}

		/* Standing, IN_FORWARD is withheld only over 90 degrees and not over
		 * 45, so the same 135 degrees of carried error start her running on
		 * the third tick rather than the fifth. The turn itself is unchanged
		 * - eight ticks at the pinned 3277 - which is what the boundary
		 * check is about. */
		ok(forward_at == 3, "...no forward for three ticks of it", forward_at, 3);
		ok(arrived_at == 8, "...and eight ticks to finish the turn nobody asked for",
			arrived_at, 8);

		/* The same tick after the reset: the stick means what the player's
		 * camera says it means, and she runs. */
		ModernResetState(&carried);
		tick_turn(&cfg, &carried, 0, -S(32000), lara, lara, &sit, &out, &yaw);
		ok(out.target == lara, "after the reset the same stick answers THIS level's camera",
			out.target, lara);
		ok(out.delta == 0, "...with no error to work off", out.delta, 0);
		ok(out.bits == MODERN_IN_FORWARD, "...so she simply runs", out.bits, MODERN_IN_FORWARD);
		ok(yaw == 0, "...and nothing writes her heading", yaw, 0);

		/* And if the boundary catches her running instead of standing, the
		 * defect is the same one worn the other way round, but quieter: the
		 * carried course does not jump across the boundary, it is simply
		 * carried, so the snap declines and she turns away at the bounded
		 * rate rather than whipping 135 degrees in one tick. The wrong
		 * heading is still wrong, which is what the reset in
		 * game\gameflow.cpp is for; only the violence of it is gone. */
		carried = state;
		sit.anim_state = 1;				/* AS_RUN */
		tick_turn(&cfg, &carried, 0, -S(32000), lara, lara, &sit, &out, &yaw);
		ok(out.delta == 24576, "caught running, the carried course is still 135 degrees out",
			out.delta, 24576);
		ok(out.course_step == 0, "...but it did not jump, so no whip", out.course_step, 0);
		ok(yaw == 3277, "...she turns away at the bounded rate instead", yaw, 3277);
	}
}

/* ------------------------------------------------- the spin on the spot ---- */

/* The rule replaced, written in the pieces the header still exports so that
 * the sweep below can be run against both and the difference is a number
 * rather than an argument. Two lines are the whole of it: the instant
 * about-turn fired on the error alone, and IN_FORWARD was withheld until
 * snap_forward of the turn was left, whatever Lara was doing. Nothing in the
 * engine calls this any more; it stays because a claim about a removed rule
 * is only honest while the rule can still be run against the new one - the
 * removed rule produced a stumble animation with a spin that had no
 * animation of its own.
 *
 * The gates ModernDirectTurn asks before these two lines (mode RUN, the state
 * list, the two switches) are left out because the sweep never fails one of
 * them; every tick of it is an active, running, unarmed MODE RUN tick. */
static long old_direct_turn(const MODERN_CFG* cfg, const MODERN_RESULT* out, short anim_state,
	long* forward)
{
	/* MODERN_SNAP_DELTA's own value, and this is now the only place it
	 * exists: the constant was deleted along with the rule it gated. It
	 * stays here because a claim about a removed rule is only honest while
	 * the rule can still be run against the new one. */
	const long old_snap_delta = 0x4000;
	long yaw = ModernGroundTurn(anim_state, out->delta, cfg->turn_rate);

	if (absl(out->delta) > old_snap_delta && !ModernStateIsStanding(anim_state))
		yaw = out->delta;

	*forward = (absl((long)out->delta - yaw) <= cfg->snap_forward) ? 1 : 0;

	return yaw;
}

/* What one run of a circling stick left behind. */
struct SPIN_RUN
{
	long no_forward;	/* ticks that sent no IN_FORWARD */
	long drops;			/* episodes of that - the counted stumbles */
	long jerks;			/* ticks that turned by more than the rate */
	long not_run;		/* ticks whose mode was not MODERN_MODE_RUN */
	long worst_error;	/* the biggest |delta| the run reached */
	long worst_cstep;	/* the biggest |course_step| the run reached */
};

/* One run of a stick circling at a fixed rate, from a run, with the automaton
 * modelled exactly as far as measured and no further: IN_FORWARD keeps her
 * in AS_RUN, and a tick without it drops her to AS_STOP for that tick
 * (game\lara.cpp, lara_as_run - a run with no forward goal is a stop). The
 * camera is held still, matching a real one at rest.
 *
 * The model is a uniform circle and a real thumb is not, which matters for
 * reading the `rule` runs rather than the shipped one: a stick that turns
 * faster than Lara can, without ever slowing, gives each rule its own worst
 * case, where a real thumb would keep handing them chances to recover. The
 * current shipped numbers do not depend on the model at all - they are
 * zeroes.
 *
 * `rule` picks which of the three to run:
 *   0  what ships - both parts of the replacement together
 *   1  the previous shipped rule - the error-only snap, forward withheld
 *   2  the first part without the second, which is why they ship together:
 *      keep her running and the error-only snap starts firing, because she
 *      never stutters into the standing state where it is disabled */
static void spin_run(long step_units, long ticks, int rule, long rate, SPIN_RUN* r)
{
	const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short heading = 0;
	short anim = 1;						/* AS_RUN - she is already running */
	long last_forward = 1;
	long t;

	ModernDefaults(&cfg);

	if (rate > 0)
		cfg.turn_rate = rate;			/* 0 = whatever ships */

	ModernResetState(&state);
	sit.anim_state = anim;
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	r->no_forward = 0;
	r->drops = 0;
	r->jerks = 0;
	r->not_run = 0;
	r->worst_error = 0;
	r->worst_cstep = 0;

	for (t = 0; t < ticks; t++)
	{
		const double ang = (double)(t * step_units) * turn;
		const long x = roundl2(sin(ang) * 120.0);	/* 120 of 128: a stick pushed over */
		const long z = roundl2(cos(ang) * 120.0);
		long yaw = 0;
		long forward = 0;

		sit.anim_state = anim;
		ModernTranslate(&cfg, &state, x, -z, 0, heading, &sit, &out);

		if (rule)
		{
			yaw = old_direct_turn(&cfg, &out, anim, &forward);

			if (rule == 2 && ModernStateIsRunning(anim))
				forward = 1;				/* the first part, without the second beside it */
		}
		else
		{
			ModernDirectTurn(&cfg, &state, &sit, &out, &yaw);
			forward = (out.bits & MODERN_IN_FORWARD) ? 1 : 0;
		}

		if (out.mode != MODERN_MODE_RUN)
			r->not_run++;

		if (!forward)
		{
			r->no_forward++;

			if (last_forward)
				r->drops++;
		}

		if (absl(yaw) > cfg.turn_rate)
			r->jerks++;

		if (absl(out.delta) > r->worst_error)
			r->worst_error = absl(out.delta);

		if (absl(out.course_step) > r->worst_cstep)
			r->worst_cstep = absl(out.course_step);

		heading = (short)ModernWrap((long)heading + yaw);
		anim = (short)(forward ? 1 : 2);
		last_forward = forward;
	}
}

static void circling()
{
	MODERN_CFG cfg;
	SPIN_RUN now;
	SPIN_RUN was;
	SPIN_RUN half;

	printf("the spin on the spot - a circling stick must not stutter\n");

	ModernDefaults(&cfg);

	/* A measured circling speed: about 21 degrees a tick, which is 3822 of
	 * our units. Five revolutions of it, 86 ticks, out of a run. reversal()
	 * and spin_sweep() below cover the rest of this sweep.
	 *
	 * The two old rules are run at the rate they shipped with
	 * (MODERN_TURN_RATE_TRONE, 3277), which has since moved to 5461
	 * underneath them. They have to be: the old rule is a rule and a rate
	 * together - the stutter it produced came from the stick outrunning
	 * 3277 - so driving it with the new rate would measure neither. */
	spin_run(3822, 86, 0, 0, &now);
	spin_run(3822, 86, 1, MODERN_TURN_RATE_TRONE, &was);
	spin_run(3822, 86, 2, MODERN_TURN_RATE_TRONE, &half);

	printf("  a stick circling at 21 deg/tick, five revolutions of 86 ticks, from a run:\n");
	printf("    the old rule at 3277:        %ld ticks with no forward in %ld stumbles,"
		" %ld one-tick whips\n", was.no_forward, was.drops, was.jerks);
	printf("    first part alone at 3277:    %ld / %ld / %ld  <- the whips the stutter hid\n",
		half.no_forward, half.drops, half.jerks);
	printf("    ours at 5461:                %ld / %ld / %ld, worst error %ld,"
		" worst course step %ld\n", now.no_forward, now.drops, now.jerks,
		now.worst_error, now.worst_cstep);

	ok(now.no_forward == 0, "circling: IN_FORWARD on every single tick", now.no_forward, 0);
	ok(now.drops == 0, "...so not one stumble into AS_STOP", now.drops, 0);
	ok(now.jerks == 0, "...and no tick turns more than the rate", now.jerks, 0);
	ok(now.not_run == 0, "...she never leaves MODE RUN", now.not_run, 0);

	/* The course's own step is what MODERNLOG's cstep= prints, and a circle
	 * never comes near a flick threshold. It is not exactly
	 * the stick's 3822 because a stick is a pair of whole numbers: 120 units of
	 * deflection quantise the angle by up to half a degree a tick. */
	ok(absl(now.worst_cstep - 3822) <= 128, "...the course steps with the stick and no further",
		now.worst_cstep, 3822);
	ok(now.worst_cstep < 0x4000, "...a long way under what a flick used to need",
		now.worst_cstep, 0x4000);

	/* What the two halves are worth separately, which is the answer to why
	 * this ships as one change. The old rule stuttered: the withheld
	 * forward put her in AS_STOP, and AS_STOP is a standing state, where the
	 * error-only snap is disabled - so the stutter was hiding the whip. Keep
	 * her running but leave the snap on the error and the whips come
	 * straight out: a thumb that keeps letting her recover between them can
	 * meet both defects in the same session without seeing either clearly. */
	ok(was.no_forward > 0, "the old rule dropped forward on this same stick",
		was.no_forward, 1);
	ok(was.jerks == 0, "...and never whipped here, because the stutter disabled the snap",
		was.jerks, 0);
	ok(half.no_forward == 0, "the first part alone keeps her running", half.no_forward, 0);
	ok(half.jerks > 0, "...and then the error-only snap whips her, which is why the second part ships with it",
		half.jerks, 1);

	/* What removing the error-only snap cost, and what raising the turn rate
	 * pays it with. The snap was also a ceiling on how far she could fall
	 * behind: it fired at 90 degrees and put the error back to nought.
	 * Without it she simply lagged, by the difference between the thumb and
	 * her rate - 3902 against 3277, so 625 a tick - and after 53 ticks of
	 * uninterrupted circling the lag reached half a turn and she started
	 * closing it the other way round. The cure is the turn rate itself,
	 * measured against a thumb that kept breaking out of the circle (twice
	 * into a wall, four windows of it) and raised to 5461. This is the
	 * shipped rate, on the measured circle. */
	ok(now.worst_error <= 3822 + 128, "the shipped rate leaves no lag on his own circle",
		now.worst_error, 3822);

	spin_run(3822, 86, 1, MODERN_TURN_RATE_TRONE, &was);
	printf("    the same circle at the old 3277: worst error %ld; at 5461: %ld\n",
		was.worst_error, now.worst_error);

	/* A circle the old rate could keep up with changes nothing either way -
	 * 15 degrees a tick, inside 3277's own 18. That is the honest limit of
	 * the claim: the change only ever bit where the thumb outran her. */
	spin_run(2730, 86, 0, 0, &now);
	spin_run(2730, 86, 1, MODERN_TURN_RATE_TRONE, &was);

	ok(now.no_forward == 0 && was.no_forward == 0,
		"a circle inside her turn rate: neither rule ever drops forward",
		now.no_forward + was.no_forward, 0);
	ok(now.jerks == 0 && was.jerks == 0, "...and neither one whips",
		now.jerks + was.jerks, 0);
	ok(now.worst_error < cfg.snap_forward, "...the error never even reaches 45 degrees",
		now.worst_error, cfg.snap_forward);
}

/* --------------------------------------------- the reversal, tick by tick -- */

/* One reversal, driven through the seam's own two calls. She stands on
 * `heading`, the camera sits on her back, the stick is thrown fully back and
 * held - so the course is seated exactly half a turn from her and never
 * moves again (the camera does not follow her, and a stick that is not
 * moving does not re-seat). What comes back is how the turn was really
 * taken.
 *
 * `run` is the state she is in: 1 AS_RUN or 2 AS_STOP. The automaton is not
 * modelled beyond that, because a reversal is over in seven ticks and she is in
 * one state for all of them. */
struct REVERSAL
{
	long ticks;			/* until the error is gone */
	long worst_step;	/* the biggest single turn of it */
	long flips;			/* ticks that turned against the tick before */
	long overshoot;	/* ticks that went past the course and had to come back */
	long forward_at;	/* the first tick with IN_FORWARD, 0 if never */
	long left_at_six;	/* what was still to turn after six ticks */
	long worst_hold;	/* the highest MODERN_STATE::stand_hold of the run */
};

static void reversal_run(short heading, short anim, long rate, REVERSAL* r)
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	long last_sign = 0;
	long prev_left = 0x8000;
	long yaw;
	long t;

	ModernDefaults(&cfg);

	if (rate > 0)
		cfg.turn_rate = rate;

	ModernResetState(&state);
	sit.anim_state = anim;
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	r->ticks = 0;
	r->worst_step = 0;
	r->flips = 0;
	r->overshoot = 0;
	r->forward_at = 0;
	r->left_at_six = 0;
	r->worst_hold = 0;

	for (t = 1; t <= 40; t++)
	{
		long left;

		yaw = 0;
		tick_turn(&cfg, &state, 0, S(32000), heading, heading, &sit, &out, &yaw);

		if (absl(yaw) > r->worst_step)
			r->worst_step = absl(yaw);

		if (yaw && last_sign && ((yaw > 0) != (last_sign > 0)))
			r->flips++;

		if (yaw)
			last_sign = yaw;

		if (!r->forward_at && (out.bits & MODERN_IN_FORWARD))
			r->forward_at = t;

		/* The standing stall's counter, watched from the outside. A held
		 * stick is the case the 90 degree gate exists for - pushing away from
		 * a ledge - so the tick-counted way out must never open here. This is
		 * the number that proves it did not. */
		if (state.stand_hold > r->worst_hold)
			r->worst_hold = state.stand_hold;

		heading = (short)ModernWrap((long)heading + yaw);
		left = absl(ModernWrap((long)state.course - heading));

		/* An overshoot shows as the error GROWING on a tick that turned: the
		 * short way round never gets longer while she is walking down it. */
		if (yaw && left > prev_left)
			r->overshoot++;

		prev_left = left;

		if (t == 6)
			r->left_at_six = left;

		if (!r->ticks && !left)
			r->ticks = t;
	}
}

static void reversal()
{
	MODERN_CFG cfg;
	MODERN_RESULT out;
	REVERSAL r;
	REVERSAL worst;
	long h;
	long yaw;

	printf("the reversal - a half turn is a turn now, not a jump\n");

	ModernDefaults(&cfg);

	/* 1. From every heading, standing and running: settled in at most seven
	 * ticks, no step above the rate, and monotone - never a tick that turns
	 * back against the one before, never an overshoot she has to come back
	 * from. 64 headings is every 5.6 degrees of the circle, which is finer
	 * than the stick's own quantisation. */
	worst.ticks = 0;
	worst.worst_step = 0;
	worst.flips = 0;
	worst.overshoot = 0;
	worst.worst_hold = 0;

	for (h = -32768; h < 32768; h += 1024)
	{
		int s;

		for (s = 0; s < 2; s++)
		{
			reversal_run((short)h, (short)(s ? 1 : 2), 0, &r);

			if (r.ticks > worst.ticks)
				worst.ticks = r.ticks;

			if (r.worst_step > worst.worst_step)
				worst.worst_step = r.worst_step;

			worst.flips += r.flips;
			worst.overshoot += r.overshoot;

			if (r.worst_hold > worst.worst_hold)
				worst.worst_hold = r.worst_hold;
		}
	}

	printf("  a half turn from each of 64 headings, standing and running:\n");
	printf("    worst %ld ticks, biggest step %ld, %ld direction flips, %ld overshoots\n",
		worst.ticks, worst.worst_step, worst.flips, worst.overshoot);

	ok(worst.ticks <= 7, "a reversal settles in at most seven ticks", worst.ticks, 7);
	ok(worst.worst_step <= MODERN_TURN_RATE, "...with no step above the rate",
		worst.worst_step, MODERN_TURN_RATE);
	ok(worst.flips == 0, "...monotone: never a tick against the one before", worst.flips, 0);
	ok(worst.overshoot == 0, "...and never past the course", worst.overshoot, 0);

	/* And the ledge is still safe. The tick-counted way out of a standing
	 * turn (MODERN_STAND_FORWARD_TICKS) must answer a moving thumb only; a
	 * stick thrown a half turn and held is exactly the case the 90 degree
	 * gate protects, and over all 128 runs above the counter never gets past
	 * three - she is inside the gate on tick four and the rule is never
	 * reached at all. */
	ok(worst.worst_hold < MODERN_STAND_FORWARD_TICKS,
		"...and a held stick never reaches the tick-counted way out",
		worst.worst_hold, MODERN_STAND_FORWARD_TICKS - 1);
	printf("    the standing stall's counter over all of them: %ld of %d\n",
		worst.worst_hold, (int)MODERN_STAND_FORWARD_TICKS);

	/* 2. Six ticks and a remainder: 32768 / 5461 = 6.0004, so six whole
	 * rates leave two units and a seventh tick takes them. Two units is a
	 * fiftieth of a degree - the half turn is six ticks to anything an eye
	 * can see, and the seventh is written down rather than rounded away. */
	reversal_run(0, 2, 0, &r);
	ok(r.left_at_six == 2, "after six ticks two units of the half turn are left",
		r.left_at_six, 2);
	ok(r.ticks == 7, "...and the seventh tick takes them", r.ticks, 7);
	printf("  180 degrees at %d: six ticks (0.20 s) leave %ld units, gone on the seventh\n",
		(int)MODERN_TURN_RATE, r.left_at_six);

	/* 3. What it was, run against what it is. The old rule took the whole
	 * half turn in one tick while she was running - measured as 24 steps
	 * over 90 degrees in one session, a turn too abrupt for the
	 * interpolation to keep up with. The model of it is old_direct_turn
	 * above, driven at the rate it shipped with. */
	{
		MODERN_STATE state;
		MODERN_SITUATION sit;
		long forward = 0;
		long old_yaw;

		ModernDefaults(&cfg);
		cfg.turn_rate = MODERN_TURN_RATE_TRONE;
		ModernResetState(&state);
		sit.anim_state = 1;
		sit.classic = 0;
		sit.drawn = 0;
		sit.action = 0;
		sit.jump = 0;
		sit.blocked = 0;
		sit.cam_player_step = 0;

		tick(&cfg, &state, 0, -S(32000), 0, 0, &sit, &out);
		tick(&cfg, &state, 0, S(32000), 0, 0, &sit, &out);
		old_yaw = old_direct_turn(&cfg, &out, 1, &forward);
		ok(absl(old_yaw) == 0x8000, "the old direct turn took the same half turn in ONE tick",
			absl(old_yaw), 0x8000);
		printf("  the rule this replaces: one tick of %ld units; ours: %d of %d\n",
			absl(old_yaw), (int)r.ticks, (int)MODERN_TURN_RATE);
	}

	/* 4. The tie at exactly half a turn, which is the one thing the deletion
	 * needed building rather than removing. Both sides are the same angle and
	 * ModernWrap can only answer one of them, so without a decision every exact
	 * reversal would go left by accident - and a reversal that met 180 again on
	 * its second tick could change its mind and stand still. The bias is the
	 * turn's own last direction. */
	ModernDefaults(&cfg);
	ok(ModernGroundTurnBiased(2, -0x8000L, cfg.turn_rate, 0) == -cfg.turn_rate,
		"a tie with nothing behind it goes to the fixed side",
		ModernGroundTurnBiased(2, -0x8000L, cfg.turn_rate, 0), -cfg.turn_rate);
	ok(ModernGroundTurnBiased(2, -0x8000L, cfg.turn_rate, 1) == cfg.turn_rate,
		"...and a turn already going right keeps going right",
		ModernGroundTurnBiased(2, -0x8000L, cfg.turn_rate, 1), cfg.turn_rate);
	ok(ModernGroundTurnBiased(2, -0x8000L, cfg.turn_rate, -1) == -cfg.turn_rate,
		"...one already going left, left", ModernGroundTurnBiased(2, -0x8000L, cfg.turn_rate, -1),
		-cfg.turn_rate);
	ok(ModernGroundTurn(2, -0x8000L, cfg.turn_rate) == -cfg.turn_rate,
		"and the unbiased door TROne's own expectations come through is unchanged",
		ModernGroundTurn(2, -0x8000L, cfg.turn_rate), -cfg.turn_rate);

	/* Every other angle is decided by the angle and not by the bias, which is
	 * what makes this a tie-break and not a thumb on the scale. */
	{
		long bad = 0;

		for (h = -32767; h < 32768; h += 97)
		{
			const long a = ModernGroundTurnBiased(2, h, cfg.turn_rate, 1);
			const long b = ModernGroundTurnBiased(2, h, cfg.turn_rate, -1);

			if (a != b)
				bad++;
		}

		ok(bad == 0, "nothing but the exact half turn reads the bias at all", bad, 0);
	}

	/* 5. And the tie through the seam, where the bias comes from the state: a
	 * first tick that turns her right leaves last_turn at 1, so a second tick
	 * that lands on exactly 180 carries on right instead of reversing. */
	{
		MODERN_STATE state;
		MODERN_SITUATION sit;

		ModernResetState(&state);
		sit.anim_state = 2;
		sit.classic = 0;
		sit.drawn = 0;
		sit.action = 0;
		sit.jump = 0;
		sit.blocked = 0;
		sit.cam_player_step = 0;

		tick_turn(&cfg, &state, S(32000), 0, 0, 0, &sit, &out, &yaw);
		ok(yaw > 0, "a stick to the right turns her right", (yaw > 0) ? 1 : 0, 1);
		ok(state.last_turn == 1, "...and the state remembers which way", state.last_turn, 1);

		/* Now exactly half a turn of error, from a stick thrown to the back: the
		 * camera stands on her new heading, so course = heading + 180. */
		tick_turn(&cfg, &state, 0, S(32000), (short)yaw, (short)yaw, &sit, &out, &yaw);
		ok(out.delta == -32768, "the next tick is the exact tie", out.delta, -32768);
		ok(yaw > 0, "...and she carries on the way she was going", (yaw > 0) ? 1 : 0, 1);
	}
}

/* ------------------------------------------- a circling stick, 10 to 35 ----- */

/* What one circle of the stick leaves behind, from a stand and with the
 * standing automaton modelled exactly as far as measured: no IN_FORWARD
 * and she stays in AS_STOP, IN_FORWARD and she is running. `start` is where
 * on the circle the thumb begins, because that is the error she starts with
 * - the course is seated on the first push. */
struct CIRCLE
{
	long worst_error;	/* the biggest |delta| after the first tick */
	long flips;			/* ticks that turned against the tick before */
	long forward_at;	/* the first tick with IN_FORWARD, 0 if never in the run */
	long spun;			/* ticks in AS_STOP with a turn and no forward */
};

/* `stand_ticks` is the tick-counted way out of a standing turn: -1 leaves the
 * shipped default alone, 0 switches it off - which is what the runs of the OLD
 * rule below pass, because that rule never had one. */
static void circle_run(long step_units, long start_units, long ticks, long rate,
	long stand_gate, long stand_ticks, CIRCLE* c)
{
	const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short heading = 0;
	short anim = 2;						/* AS_STOP - she is standing when he starts */
	long last_sign = 0;
	long t;

	ModernDefaults(&cfg);

	if (rate > 0)
		cfg.turn_rate = rate;

	if (stand_gate > 0)
		cfg.snap_forward_stand = stand_gate;

	if (stand_ticks >= 0)
		cfg.stand_forward_ticks = stand_ticks;

	ModernResetState(&state);
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	c->worst_error = 0;
	c->flips = 0;
	c->forward_at = 0;
	c->spun = 0;

	for (t = 0; t < ticks; t++)
	{
		const double ang = (double)(start_units + t * step_units) * turn;
		const long x = roundl2(sin(ang) * 120.0);
		const long z = roundl2(cos(ang) * 120.0);
		long yaw = 0;
		long forward;

		sit.anim_state = anim;
		tick_turn(&cfg, &state, x, -z, 0, heading, &sit, &out, &yaw);
		forward = (out.bits & MODERN_IN_FORWARD) ? 1 : 0;

		if (t && absl(out.delta) > c->worst_error)
			c->worst_error = absl(out.delta);

		if (yaw && last_sign && ((yaw > 0) != (last_sign > 0)))
			c->flips++;

		if (yaw)
			last_sign = yaw;

		if (!c->forward_at && forward)
			c->forward_at = t + 1;

		if (anim == 2 && yaw && !forward)
			c->spun++;

		heading = (short)ModernWrap((long)heading + yaw);
		anim = (short)(forward ? 1 : 2);
	}
}

static void spin_sweep()
{
	const long speeds[6] = { 1820, 2730, 3641, 4551, 5461, 6371 };	/* 10..35 deg/tick */
	const char* names[6] = { "10", "15", "20", "25", "30", "35" };
	CIRCLE c;
	long worst_new = 0;
	long worst_old = 0;
	long never_new = 0;
	long never_old = 0;
	long spun_new = 0;
	long spun_old = 0;
	int i;

	printf("a circling stick at 10 to 35 deg/tick\n");

	/* 1. A 300-tick sweep of an even circle from the direction she is
	 * already facing, which is a thumb that starts where she stands. The
	 * error stays bounded and the turn never changes its mind. */
	printf("  300 ticks, the circle starting where she faces:\n");

	for (i = 0; i < 6; i++)
	{
		circle_run(speeds[i], 0, 300, 0, 0, -1, &c);
		printf("    %s deg/tick: worst error %5ld (%3ld deg), %ld direction flips,"
			" forward on tick %ld\n", names[i], c.worst_error,
			c.worst_error * 360 / 65536, c.flips, c.forward_at);

		if (i <= 4)
		{
			ok(c.worst_error < 0x4000, "the error stays under 90 degrees up to 30 deg/tick",
				c.worst_error, 0x4000);
			ok(c.flips == 0, "...and the turn never flips direction", c.flips, 0);
		}
	}

	/* And the one that does not hold, measured rather than hidden: at 35
	 * degrees a tick the thumb is faster than she can turn, so the lag grows by
	 * the difference every tick, crosses half a turn and the shortest way round
	 * changes side. That is the arithmetic of "turn toward the stick at a fixed
	 * rate" and not a rule that can be added; the cure would be a rate over
	 * 6371, and 5461 is what the Remaster's own footage was counted at. It is
	 * the same shape as the defect measured at 3277 - only the speed it
	 * starts at has moved from 18 to 30 degrees a tick. */
	circle_run(6371, 0, 300, 0, 0, -1, &c);
	ok(c.flips > 0, "at 35 deg/tick the thumb outruns her and the side does change",
		c.flips, 1);
	printf("    - and that is the honest limit: past 30 deg/tick she lags by the"
		" difference\n");

	/* 2. A sweep over every start angle: how long a standing Lara spins
	 * before the run begins. Measured as 232 ticks of a full circle with no
	 * real animation of the turn, at 3277 with the 45 degree gate. Both the
	 * old and new rate are run here. */
	printf("  from a stand, 36 start angles x six speeds, 300 ticks each:\n");

	for (i = 0; i < 6; i++)
	{
		long start;
		long new_worst = 0;
		long old_worst = 0;
		long new_never = 0;
		long old_never = 0;

		for (start = 0; start < 65536; start += 1820)
		{
			circle_run(speeds[i], start, 300, 0, 0, -1, &c);

			if (!c.forward_at)
				new_never++;
			else
			{
				if (c.forward_at > new_worst)
					new_worst = c.forward_at;

				/* Only the starts that DO reach the run are counted here: a
				 * locked start spins for all 300 ticks of the sweep and would
				 * bury the number this line is about. The lock is counted on
				 * its own, below. */
				spun_new += c.spun;
			}

			circle_run(speeds[i], start, 300, MODERN_TURN_RATE_TRONE, 0x2000, 0, &c);

			if (!c.forward_at)
				old_never++;
			else
			{
				if (c.forward_at > old_worst)
					old_worst = c.forward_at;

				spun_old += c.spun;
			}
		}

		printf("    %s deg/tick: running by tick %3ld, %ld of 36 starts never"
			"   (3277 and 45 deg: %3ld, %ld never)\n",
			names[i], new_worst, new_never, old_worst, old_never);

		/* This holds at every speed. Without the tick-counted way out, five
		 * of these 36 starts at 30 deg/tick never reach the run at all; it
		 * closes the gap at 35 as well, where
		 * she can never catch the thumb by turning. */
		ok(new_never == 0, "every start reaches the run, at every speed the thumb was"
			" measured at", new_never, 0);
		ok(new_worst <= MODERN_STAND_FORWARD_TICKS,
			"...within eight ticks", new_worst,
			MODERN_STAND_FORWARD_TICKS);

		if (new_worst > worst_new)
			worst_new = new_worst;

		if (old_worst > worst_old)
			worst_old = old_worst;

		never_new += new_never;
		never_old += old_never;
	}

	printf("  standing ticks spun with no animation, over the starts that reach the run"
		" (216 of 216 now, 211 before this pass): %ld now, %ld then\n",
		spun_new, spun_old);
	printf("  (a play log counted 232 such ticks in one session, 140 of them stepping"
		" 18 deg with no bit at all)\n");
	ok(spun_new * 4 < spun_old, "the standing spin is a quarter of what it was",
		spun_new, spun_old / 4);

	/* The lock itself is arithmetic and stays: a stick circling at exactly
	 * her turn rate moves the course by as much as she turns, so whatever
	 * error the first push seated is the error she keeps - that is what
	 * "turn toward the course at a fixed rate" means and no rule can take it
	 * away. What can be taken away is the consequence - without the way out she stands in
	 * AS_STOP turning with no animation for as long as the thumb keeps it up,
	 * and a slower rate has the same lock twelve degrees a tick lower down,
	 * inside the measured 15 to 30 degree range.
	 *
	 * So the error still holds and she runs it out: the tick-counted way out
	 * hands IN_FORWARD over on the eighth shut tick, the automaton leaves
	 * AS_STOP, and a thumb that keeps circling now steers a run instead of
	 * winding a statue. The run below is the same 135 degrees off that used
	 * to never reach the run. */
	circle_run(MODERN_TURN_RATE, 0x6000, 300, 0, 0, -1, &c);
	ok(c.forward_at > 0 && c.forward_at <= MODERN_STAND_FORWARD_TICKS,
		"a stick at exactly her rate still gets her running, on the eighth tick",
		c.forward_at, MODERN_STAND_FORWARD_TICKS);
	ok(c.worst_error > 0x4000, "...while the error it seated is indeed still there",
		c.worst_error, 0x4000);
	printf("  the lock: a circle at exactly %d units a tick, started 135 degrees off,"
		" holds %ld deg and runs on tick %ld\n", (int)MODERN_TURN_RATE,
		c.worst_error * 360 / 65536, c.forward_at);

	circle_run(MODERN_TURN_RATE, 0x6000, 300, 0, 0, 0, &c);
	ok(c.forward_at == 0, "...and with the way out switched off she stands there for ever",
		c.forward_at, 0);
	printf("            with stand_forward_ticks 0 (the rule this pass added, switched"
		" off): %ld standing ticks of 300\n", c.spun);

	circle_run(MODERN_TURN_RATE_TRONE, 0x6000, 300, MODERN_TURN_RATE_TRONE, 0x2000, 0, &c);
	ok(c.forward_at == 0, "...and the old rate had the same lock at 18 deg/tick",
		c.forward_at, 0);
	printf("            the same at the old 3277 - the lock moved UP 12 deg/tick,"
		" out of his measured range\n");
	printf("  starts that never reach the run: %ld of 216 now, %ld then"
		" (the six speeds are not the old lock's own)\n", never_new, never_old);
}

/* ------------------------------------------------- she steers in the air --- */

static void air_turn()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	long yaw;
	long total;
	long ticks;
	long i;

	printf("the air turn - a wider radius round a ledge\n");

	/* 1. One state, and it is the one the classic itself steers. 3
	 * AS_FORWARDJUMP is the only routine in game\lara.cpp that reads IN_LEFT or
	 * IN_RIGHT off the ground (lara_as_forwardjump, turn_rate capped at
	 * 546 against LaraAboveWater's decay of 364 = 182 a tick, 1.0 degree).
	 * 11 AS_REACH, 28 AS_UPJUMP, 9 AS_FASTFALL, 52 AS_SWANDIVE, 53 AS_FASTDIVE
	 * and 10 AS_FALLBACK read no direction bit at all, and the reach is the
	 * grab - where her nose points is which ledge lara_col_reach tests. */
	total = 0;

	for (i = 0; i < 139; i++)
		if (ModernStateAllowsAirTurn((short)i))
			total++;

	ok(total == 1, "one of the 139 states steers in the air", total, 1);
	ok(ModernStateAllowsAirTurn(3) == 1, "...and it is the forward jump",
		ModernStateAllowsAirTurn(3), 1);

	{
		const short airborne[6] = { 9, 10, 11, 28, 52, 53 };

		for (i = 0; i < 6; i++)
			ok(ModernStateAllowsAirTurn(airborne[i]) == 0,
				"the reach, the dives and the falls keep their own heading",
				ModernStateAllowsAirTurn(airborne[i]), 0);
	}

	/* 2. Ninety degrees off, and she arrives without overshooting. 0x4000 /
	 * 728 = 22.5, so twenty-two whole rates and a twenty-third tick clamped to
	 * the 368 units that are left. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 3;					/* AS_FORWARDJUMP */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	{
		short heading = 0;
		long worst = 0;
		long over = 0;

		ticks = 0;

		for (i = 0; i < 40; i++)
		{
			yaw = 0;
			ModernTranslate(&cfg, &state, S(32000), 0, 0, heading, &sit, &out);
			ModernAirTurn(&cfg, &state, &sit, &out, &yaw);

			if (absl(yaw) > worst)
				worst = absl(yaw);

			if (absl(yaw) > absl(out.delta))
				over++;

			heading = (short)ModernWrap((long)heading + yaw);

			if (!ticks && out.delta == 0)
				ticks = i;
		}

		ok(worst == 728, "the air turn steps at its own rate and no faster", worst, 728);
		ok(over == 0, "...and never past the course", over, 0);
		ok(ticks == 23, "90 degrees off, she is on the course after 23 ticks", ticks, 23);
		ok(heading == 0x4000, "...on the course exactly, not past it", heading, 0x4000);
		printf("  90 degrees off: %ld ticks of %ld units, ending on %d\n",
			ticks, worst, heading);
	}

	/* 3. What a whole jump is worth: a wider radius around a ledge. A
	 * forward jump lasts about 40 ticks. The classic manages 182 a tick and
	 * measured jumps turn 1 to 36 degrees in total; ours turns four times
	 * that. The stick is thrown to the back and held, which is the furthest
	 * a course can ever be from her: 32768 / 728 is 45 ticks, so she is
	 * still turning when she lands and 160 degrees is the whole of what a
	 * jump can buy - measured rather than argued. A held stick nearer than
	 * that arrives at the course and stops there, because the course is
	 * held and does not run away from her. */
	ModernResetState(&state);
	{
		short heading = 0;

		for (i = 0; i < 40; i++)
		{
			yaw = 0;
			ModernTranslate(&cfg, &state, 0, S(32000), 0, heading, &sit, &out);
			ModernAirTurn(&cfg, &state, &sit, &out, &yaw);
			heading = (short)ModernWrap((long)heading + yaw);
		}

		total = absl(ModernWrap(heading));
		ok(total == 40 * 728, "40 ticks of jump turn 160 degrees at most", total, 40 * 728);
		printf("  a 40-tick jump: %ld units = %ld degrees, where the classic's own 182"
			" makes %ld\n", total, total * 360 / 65536, 40L * 182 * 360 / 65536);
	}

	/* 4. The bits: the two turn bits go, because the air turn is turning her
	 * and a bit would turn her twice - and nothing else does, because in the
	 * air the rest of the word is not a direction: IN_BACK is the twist,
	 * IN_WALK the swan dive, IN_ACTION the grab. */
	ModernResetState(&state);
	ModernTranslate(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	out.bits = MODERN_IN_LEFT | MODERN_IN_RIGHT | MODERN_IN_FORWARD | MODERN_IN_WALK |
		MODERN_IN_BACK;
	ok(ModernAirTurn(&cfg, &state, &sit, &out, &yaw) == 1, "in the jump the air turn takes charge",
		1, 1);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) == 0, "...the turn bits go",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), 0);
	ok((out.bits & (MODERN_IN_FORWARD | MODERN_IN_WALK | MODERN_IN_BACK)) ==
		(MODERN_IN_FORWARD | MODERN_IN_WALK | MODERN_IN_BACK),
		"...and the twist, the swan dive and the forward bit stand",
		out.bits & (MODERN_IN_FORWARD | MODERN_IN_WALK | MODERN_IN_BACK),
		MODERN_IN_FORWARD | MODERN_IN_WALK | MODERN_IN_BACK);

	/* 5. The gates: rate 0 is the feature switched off and the classic 1 degree
	 * a tick back in charge; a state that is not the forward jump, a classic
	 * tick and a stick at rest are all "not mine". */
	ModernResetState(&state);
	ModernTranslate(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	cfg.air_turn_rate = 0;
	ok(ModernAirTurn(&cfg, &state, &sit, &out, &yaw) == 0, "air_turn_rate 0: no write", 0, 0);
	ok(yaw == 0, "...and no angle", yaw, 0);
	ok((out.bits & MODERN_IN_RIGHT) != 0, "...the classic turn bit stands instead",
		out.bits & MODERN_IN_RIGHT, MODERN_IN_RIGHT);

	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 11;				/* AS_REACH */
	ModernTranslate(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(ModernAirTurn(&cfg, &state, &sit, &out, &yaw) == 0, "the reach is not steered", 0, 0);

	sit.anim_state = 3;
	sit.classic = 1;
	ModernTranslate(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(ModernAirTurn(&cfg, &state, &sit, &out, &yaw) == 0, "a classic tick is not steered", 0, 0);
	sit.classic = 0;

	ModernResetState(&state);
	ModernTranslate(&cfg, &state, 0, 0, 0, 0, &sit, &out);
	ok(ModernAirTurn(&cfg, &state, &sit, &out, &yaw) == 0, "and a stick at rest turns nothing",
		0, 0);

	/* 6. And the ground turn still refuses the air, which is what keeps the
	 * two from ever running in the same tick: the state lists do not overlap. */
	ok(ModernStateAllowsDirectTurn(3) == 0, "the ground turn has never touched the jump",
		ModernStateAllowsDirectTurn(3), 0);
	ok(ModernStateAllowsAirTurn(1) == 0, "...and the air turn does not touch the run",
		ModernStateAllowsAirTurn(1), 0);
}

/* ------------------------------------------- the three arms of one turn ----- */

/* What the engine's own either/or rests on:
 * game\modern_state.cpp turns her in three places and they are one chain of
 * else-ifs: the water's move, the ground's direct turn, the air turn. That is
 * only honest while no state can answer two of them - otherwise the ORDER of
 * the arms would silently be a rule, and reordering them
 * would change her turning without touching a rate. So the exclusivity is
 * asserted here over every state number the engine can put in, rather than
 * argued from three lists nobody has laid side by side. */
static void turn_chain()
{
	MODERN_CFG cfg;
	long water = 0;
	long ground = 0;
	long airs = 0;
	long clash = 0;
	long i;

	printf("\nthe three arms of the turn - one state, one answer\n");

	ModernDefaults(&cfg);

	for (i = 0; i < 139; i++)
	{
		const long w = ModernWaterTurnRate(&cfg, (short)i) ? 1 : 0;
		const long g = ModernStateAllowsDirectTurn((short)i) ? 1 : 0;
		const long a = ModernStateAllowsAirTurn((short)i) ? 1 : 0;

		water += w;
		ground += g;
		airs += a;

		if (w + g + a > 1)
			clash++;
	}

	ok(clash == 0, "no state answers two arms of the chain", clash, 0);
	ok(airs == 1, "the air arm is the forward jump and nothing else", airs, 1);
	printf("  of 139 states: water %ld, ground %ld, air %ld, overlapping %ld\n",
		water, ground, airs, clash);

	/* And the one the chain is built on: game\modern_state.cpp asks the water
	 * first, by rate, so a swim state must never be steerable on the ground
	 * either - which is the same claim from the other side, and cheap to make
	 * again by name. */
	ok(ModernWaterTurnRate(&cfg, 3) == 0, "the forward jump is not the water's", 0, 0);
	ok(ModernStateAllowsDirectTurn(3) == 0, "...nor the ground's", 0, 0);
	ok(ModernStateAllowsAirTurn(3) == 1, "...it is the air's", 1, 1);
}

/* ------------------------------------------------- the look key as Tank ----- */

/* The seam's own tail, modelled. The engine side of it is two statements in
 * specific\input.cpp and one of them clears the very bit the other one is
 * gated on - the sort of thing that reads correctly and flickers at 30 Hz in
 * the hand. So the tail is modelled here, in the order the file has it, and
 * driven through the cases that matter.
 *
 * The bit's value does not matter to the model, only that it is one bit; the
 * engine's own IN_LOOK lives behind global\types.h, which this harness does
 * not open (the same reason game\modern_move.h mirrors the movement bits).
 *
 * And what a model cannot do: bmt.bat compiles this file beside
 * game\modern_move.cpp and game\modern_camera.cpp and nothing else, so
 * specific\input.cpp is NOT under test here: an edit to the seam's own gate
 * would leave every check below passing. The model is read against the file
 * whenever either changes, and that reading is the test. The same holds for
 * camera_pull()'s three mirrors of game\camera.cpp further down, which say so
 * in their own header. */
enum { LOOK_BIT = 1 };

typedef struct LOOK_SEAM
{
	long latch;		/* modern_look_tank, specific\input.cpp */
	long look_cnt;		/* LookCnt, the six-tick IN_TARGET swallow */
	long word;		/* `input`, the word the engine was handed last tick */
} LOOK_SEAM;

/* One pass of S_UpdateInput's tail. Returns the word the ENGINE is handed;
 * `translated` says whether the modern translation ran on this tick and
 * `targeted` whether the release turned into IN_TARGET. */
static long look_tick(LOOK_SEAM* s, long look_key, long gun_ready, long seam_on,
	long* translated, long* targeted)
{
	long word = look_key ? LOOK_BIT : 0;

	/* 1. the seam, gated on BOTH halves of the previous tick: the word the
	 * engine was handed - which game\control.cpp can put the bit into on its
	 * own, for the binoculars - and the latch beside it. */
	*translated = (seam_on && !(s->word & LOOK_BIT) && !s->latch) ? 1 : 0;
	*targeted = 0;

	/* 2. the weapon's own swallow, unchanged from the engine's own logic */
	if (gun_ready)
	{
		if (word & LOOK_BIT)
		{
			if (s->look_cnt >= 6)
				s->look_cnt = 100;
			else
			{
				word &= ~(long)LOOK_BIT;
				s->look_cnt++;
			}
		}
		else
		{
			if (s->look_cnt && s->look_cnt != 100)
				*targeted = 1;

			s->look_cnt = 0;
		}
	}

	/* 3. the latch, and then the strip */
	s->latch = (word & LOOK_BIT) ? 1 : 0;

	if (seam_on && !gun_ready)
		word &= ~(long)LOOK_BIT;

	s->word = word;
	return word;
}

static void look_tank()
{
	LOOK_SEAM s;
	long word, translated, targeted;
	long seen = 0;
	long spun = 0;
	long flips = 0;
	long last = -1;
	long i;

	printf("\nthe look key steers her as at Tank\n");

	/* 1. Thirty ticks of a held key, no weapon. The engine must never see the
	 * bit - that is what keeps LookLeftRight (game\lara.cpp) from eating
	 * IN_LEFT / IN_RIGHT and leaves them to lara_as_stop, which is where
	 * AS_TURN_L and its animation come from. And the scheme must change hands
	 * exactly once, on the second tick: the first tick is the one the latch
	 * has not been written on yet. */
	s.latch = 0;
	s.look_cnt = 0;
	s.word = 0;

	for (i = 0; i < 30; i++)
	{
		word = look_tick(&s, 1, 0, 1, &translated, &targeted);

		if (word & LOOK_BIT)
			seen++;

		if (translated)
			spun++;

		if (last >= 0 && translated != last)
			flips++;

		last = translated;
	}

	ok(seen == 0, "the engine never sees the look key in Modern", seen, 0);
	ok(spun == 1, "the translation runs on the first tick and then stands down", spun, 1);
	ok(flips == 1, "...and the scheme changes hands once, not every tick", flips, 1);

	/* 2. And it comes back one tick after the key, just as it went down one
	 * tick after it. Both ends are one tick behind, so the scheme is back on
	 * the second tick after the last held one - and that is not new: before
	 * this the gate read `input & IN_LOOK`, which is the previous tick's word
	 * and exactly as old as the latch that stands in for it now. 2b below
	 * counts both ends against the hold. */
	word = look_tick(&s, 0, 0, 1, &translated, &targeted);
	ok(translated == 0, "the first tick the key reads up is still Tank's", translated, 0);
	word = look_tick(&s, 0, 0, 1, &translated, &targeted);
	ok(translated == 1, "the next one is the modern scheme again", translated, 1);

	/* 2b. Ten ticks held, ten ticks bypassed - the symmetry itself, because a
	 * bypass that outlasted the hold would be a scheme the player cannot let
	 * go of. */
	{
		LOOK_SEAM h;
		long bypass = 0;
		long resumed = 0;

		h.latch = 0;
		h.look_cnt = 0;
		h.word = 0;

		for (i = 0; i < 10; i++)
		{
			look_tick(&h, 1, 0, 1, &translated, &targeted);

			if (!translated)
				bypass++;
		}

		for (i = 0; i < 5; i++)
		{
			look_tick(&h, 0, 0, 1, &translated, &targeted);

			if (!translated)
				bypass++;
			else if (!resumed)
				resumed = i + 1;
		}

		ok(bypass == 10, "a hold of ten ticks is bypassed for exactly ten", bypass, 10);
		ok(resumed == 2, "...and the modern scheme is back on the second tick after"
			" the key reads up", resumed, 2);
		printf("  the look key's own latency: one tick down, one tick up,"
			" %ld bypassed ticks for ten held\n", bypass);
	}

	/* 3. A weapon ready is the classic look, unchanged. Six ticks of swallow
	 * with the translation still running, then the bit stands - which is what
	 * opens the binoculars and the lasersight in game\control.cpp. */
	s.latch = 0;
	s.look_cnt = 0;
	s.word = 0;
	seen = 0;
	spun = 0;

	for (i = 0; i < 10; i++)
	{
		word = look_tick(&s, 1, 1, 1, &translated, &targeted);

		if (word & LOOK_BIT)
			seen++;

		if (translated)
			spun++;
	}

	ok(seen == 4, "with a weapon ready the look key reaches the engine from tick 7",
		seen, 4);
	ok(spun == 7, "...and the translation runs until it does", spun, 7);

	/* 4. And a tap with a weapon ready is still IN_TARGET. Three ticks held
	 * is under the six, so the bit never leaves the seam and the release
	 * fires the target instead - the swallow stays untouched. */
	s.latch = 0;
	s.look_cnt = 0;
	s.word = 0;
	seen = 0;

	for (i = 0; i < 3; i++)
	{
		word = look_tick(&s, 1, 1, 1, &translated, &targeted);

		if (word & LOOK_BIT)
			seen++;
	}

	word = look_tick(&s, 0, 1, 1, &translated, &targeted);
	ok(seen == 0, "a tap with a weapon ready never reaches the engine as a look", seen, 0);
	ok(targeted == 1, "...it is IN_TARGET on the release, as it always was", targeted, 1);

	/* 5. Where the seam is not reachable at all - a cutscene, a spotcam, the
	 * inventory, first person, the title - the key is the engine's as before.
	 * One term of the seam's condition stands for all eight of them. */
	s.latch = 0;
	s.look_cnt = 0;
	s.word = 0;
	word = look_tick(&s, 1, 0, 0, &translated, &targeted);
	ok((word & LOOK_BIT) != 0, "outside the seam the look key is the engine's",
		word & LOOK_BIT, LOOK_BIT);

	/* 6. And the binoculars are why the old term stayed. game\control.cpp
	 * holds IN_LOOK in the word for the whole binocular and lasersight
	 * session, long after the player has let go - so the word can carry the
	 * bit when nothing in this file put it there. The translation must stay
	 * down for as long as it does, which is what the first of the gate's two
	 * terms says. */
	s.latch = 0;
	s.look_cnt = 0;
	s.word = 0;
	spun = 0;

	for (i = 0; i < 8; i++)
	{
		word = look_tick(&s, 0, 1, 1, &translated, &targeted);
		s.word |= LOOK_BIT;		/* game\control.cpp, the session holding it on */

		if (translated)
			spun++;
	}

	ok(spun == 1, "the binocular session keeps the translation down, key or no key",
		spun, 1);

	/* 7. And the left stick's own zone on this path: the left radial zone of
	 * 31 sat over the axis gate of 18 and killed real pushes of 18..31 - the
	 * seam passed XInput's 31 on both paths before this. Every push between
	 * the gate and the old radius, in eight directions, and how many of them
	 * live now. */
	{
		const long dir[8][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 },
			{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
		long alive_old = 0;
		long alive_new = 0;
		long d, m, ox, oy;

		for (d = 0; d < 8; d++)
			for (m = 19; m <= 31; m++)
			{
				const long px = dir[d][0] * m;
				const long py = dir[d][1] * m;

				if (ModernStickDeadZone(MODERN_DEAD_LEFT, MODERN_DEAD_AXIS_LEFT_DINPUT,
					px, py, &ox, &oy))
					alive_old++;

				if (ModernStickDeadZone(MODERN_DEAD_LEFT_DINPUT, MODERN_DEAD_AXIS_LEFT_DINPUT,
					px, py, &ox, &oy))
					alive_new++;
			}

		ok(alive_new > alive_old, "the DirectInput left zone hands back the pushes 31 killed",
			alive_new, alive_old + 1);
		printf("  pushes of 19..31 in eight directions: %ld live under the old 31,"
			" %ld under the new 24\n", alive_old, alive_new);
	}
}

/* ------------------------------------ the look key's Tank, read by sector ----- */

/* A stick at a given heading, in the convention the whole scheme uses: 0 is
 * straight forward, +90 is right, -90 is left, and stick_y is forward-negative
 * because that is how specific\dxshell.cpp hands the axis over. Measured
 * thumb angles are quoted in degrees, so the tests are written in degrees
 * and converted here once. */
static void stick_at(double deg, long mag, long* x, long* y)
{
	const double rad = deg * 3.14159265358979323846 / 180.0;

	*x = roundl2(sin(rad) * (double)mag);
	*y = -roundl2(cos(rad) * (double)mag);
}

static void tank_sector()
{
	/* Four measured thumb positions: the medians of thumb pushes left and
	 * right at full deflection, ten to sixteen degrees below the horizontal.
	 * Every one of them comes out of the classic assembly as a turn
	 * and a step together. */
	const double thumb[4] = { -100.0, -106.0, 112.0, 116.0 };
	const double turn_deg = (double)MODERN_TANK_TURN_SECTOR * 180.0 / 32768.0;
	long x, y, ox, oy, sector;
	int i;

	printf("\nthe look key's Tank reads the stick by sector\n");

	/* 1. The measured thumb. One axis out, the turn's, at the stick's own
	 * magnitude and not its component - the component of a push 16 degrees
	 * low is already smaller than the push, and the classic threshold of 8
	 * must not be the thing that decides. */
	for (i = 0; i < 4; i++)
	{
		stick_at(thumb[i], 120, &x, &y);
		sector = ModernTankSector(x, y, MODERN_TANK_SECTOR_NONE, &ox, &oy);

		ok(sector == MODERN_TANK_SECTOR_TURN, "his measured thumb is a pure turn",
			sector, MODERN_TANK_SECTOR_TURN);
		ok(oy == 0, "...and no step bit goes out with it", oy, 0);
		ok(absl(ox) == 120, "...at the stick's own magnitude, not its component",
			absl(ox), 120);
		ok((ox < 0) == (thumb[i] < 0.0), "...and on the side the thumb was on",
			ox, thumb[i] < 0.0 ? -120 : 120);
	}

	printf("  his four measured angles (%.0f, %.0f, %+.0f, %+.0f deg) all turn;"
		" the sector is +-%.0f deg of the horizontal\n",
		thumb[0], thumb[1], thumb[2], thumb[3], turn_deg);

	/* 2. And the vertical is the step, over the whole +-25 degrees - the
	 * widest a forward or back push was ever measured missing the vertical
	 * by. The magnitude asked for is the quantised pair's own and not a flat
	 * 120: at 9 degrees the integer stick (19, -119) is 120.5 units long and
	 * rounds to 121, which is the axes being whole numbers and not this
	 * function doing anything. */
	for (i = -25; i <= 25; i++)
	{
		long mag;

		stick_at((double)i, 120, &x, &y);
		mag = roundl2(sqrt((double)(x * x + y * y)));
		sector = ModernTankSector(x, y, MODERN_TANK_SECTOR_NONE, &ox, &oy);
		ok(sector == MODERN_TANK_SECTOR_STEP && ox == 0 && oy == -mag,
			"25 degrees either side of forward is a pure step", sector * 1000 + oy,
			MODERN_TANK_SECTOR_STEP * 1000 - mag);

		stick_at(180.0 - (double)i, 120, &x, &y);
		mag = roundl2(sqrt((double)(x * x + y * y)));
		sector = ModernTankSector(x, y, MODERN_TANK_SECTOR_NONE, &ox, &oy);
		ok(sector == MODERN_TANK_SECTOR_STEP && ox == 0 && oy == mag,
			"...and so is 25 degrees either side of back", sector * 1000 + oy,
			MODERN_TANK_SECTOR_STEP * 1000 + mag);
	}

	/* 3. The boundary, and the hysteresis on it, both ways. A thumb sitting
	 * exactly on the plain boundary is read as whichever sector it was already
	 * in - that is the whole of the rule, and without it the turn and the step
	 * alternate at 30 Hz in the hand. The widths are held as numbers too: the
	 * sector really is MODERN_TANK_SECTOR_HYST wider one way and narrower the
	 * other, measured by walking the angle a degree at a time. */
	{
		const double hyst_deg = (double)MODERN_TANK_SECTOR_HYST * 180.0 / 32768.0;
		double held_turn = 0.0;
		double held_step = 0.0;
		double held_none = 0.0;
		double d;

		/* Where each memory gives way, walked from the horizontal up a
		 * quarter of a degree at a time. It is walked rather than asked at
		 * one angle because the stick is a pair of whole numbers: at 128
		 * units the nearest push to any given angle is up to half a degree
		 * off it, so "exactly on the boundary" is not a stick position that
		 * exists. What can be held exactly is the order of the three and the
		 * width of the band, which is the rule. */
		for (d = 0.0; d <= 90.0; d += 0.25)
		{
			stick_at(-(90.0 - d), 128, &x, &y);

			if (ModernTankSector(x, y, MODERN_TANK_SECTOR_TURN, &ox, &oy)
				== MODERN_TANK_SECTOR_TURN)
				held_turn = d;

			if (ModernTankSector(x, y, MODERN_TANK_SECTOR_STEP, &ox, &oy)
				== MODERN_TANK_SECTOR_TURN)
				held_step = d;

			if (ModernTankSector(x, y, MODERN_TANK_SECTOR_NONE, &ox, &oy)
				== MODERN_TANK_SECTOR_TURN)
				held_none = d;
		}

		/* The constants themselves, held as numbers, answered by measuring
		 * rather than by tightening a window. Every assertion below derives
		 * its expectation from MODERN_TANK_SECTOR_HYST, which is right for
		 * the arithmetic and blind to the value: mutate the hysteresis to 0
		 * and the walk and its windows move together, so a purely derived
		 * test cannot fail - tried it, 0 failed. So the two chosen numbers
		 * are asserted here, once, as the one place a mutation of either has
		 * to stop, and the windows below go on testing what they are for,
		 * which is that the code honours whatever they say. Both read back
		 * as `sector=` in MODERNLOG's tank line. */
		ok(MODERN_TANK_TURN_SECTOR == 9102, "the turn sector is 50 degrees",
			MODERN_TANK_TURN_SECTOR, 9102);
		ok(MODERN_TANK_SECTOR_HYST == 1456, "...and the hysteresis on it its own 8",
			MODERN_TANK_SECTOR_HYST, 1456);

		/* Both edges lie on the right side of the plain boundary, asked
		 * strictly, with no hysteresis term in it, so that these two hold the
		 * direction of the rule however the constant is later tuned - and each
		 * sits within a degree of where the constant puts it. A degree is the
		 * walk's own quarter plus the quantised stick's half; anything wider
		 * stops being a test of the arithmetic at all. */
		ok(held_turn > turn_deg
			&& held_turn > turn_deg + hyst_deg - 1.0 && held_turn < turn_deg + hyst_deg + 1.0,
			"a turning thumb keeps the turn a hysteresis further out",
			(long)(held_turn * 10.0), (long)((turn_deg + hyst_deg) * 10.0));
		ok(held_step < turn_deg
			&& held_step > turn_deg - hyst_deg - 1.0 && held_step < turn_deg - hyst_deg + 1.0,
			"...and a stepping one gives the step up a hysteresis sooner",
			(long)(held_step * 10.0), (long)((turn_deg - hyst_deg) * 10.0));
		ok(held_none > turn_deg - 1.0 && held_none < turn_deg + 1.0,
			"...and a fresh push takes the plain boundary, biased neither way",
			(long)(held_none * 10.0), (long)(turn_deg * 10.0));
		ok(held_turn - held_step > 2.0 * hyst_deg - 1.0
			&& held_turn - held_step < 2.0 * hyst_deg + 1.0,
			"the band is two hysteresis widths wide, one either side",
			(long)((held_turn - held_step) * 10.0), (long)(2.0 * hyst_deg * 10.0));
		printf("  the boundary walks from %.2f deg (was stepping) through %.2f"
			" (a fresh push) to %.2f (was turning), a band of %.2f deg round %.0f\n",
			held_step, held_none, held_turn, held_turn - held_step, turn_deg);
	}

	/* 4. The stick at rest, and it is the radial dead zone one step earlier
	 * that makes it that pair - so the case is driven through both functions,
	 * the way the engine drives them, rather than asserted on a hand-written
	 * 0/0. A push under the zone is 0/0 out of the zone and must stay 0/0 and
	 * no sector here; this function may not invent a direction for it. */
	{
		long zx, zy;

		for (i = 0; i <= MODERN_DEAD_LEFT_DINPUT; i++)
		{
			ModernStickDeadZone(MODERN_DEAD_LEFT_DINPUT, MODERN_DEAD_AXIS_LEFT_DINPUT,
				-i, 0, &zx, &zy);
			sector = ModernTankSector(zx, zy, MODERN_TANK_SECTOR_TURN, &ox, &oy);
			ok(sector == MODERN_TANK_SECTOR_NONE && !ox && !oy,
				"a magnitude under the zone is no sector and no bits", sector,
				MODERN_TANK_SECTOR_NONE);
		}
	}

	/* 5. And what this costs the classic assembly, counted rather than claimed:
	 * the same sweep of angles through the two thresholds of 8, with the sector
	 * and without it. `both` is the defect - a turn bit and a step bit in the
	 * same word, where lara_as_stop lets the step overwrite the turn goal. */
	{
		long both_old = 0;
		long both_new = 0;
		long deg;

		for (deg = -179; deg <= 180; deg++)
		{
			long turn_bit, step_bit;

			stick_at((double)deg, 120, &x, &y);

			turn_bit = (x < -8 || x > 8) ? 1 : 0;
			step_bit = (y > 8 || y < -8) ? 1 : 0;

			if (turn_bit && step_bit)
				both_old++;

			ModernTankSector(x, y, MODERN_TANK_SECTOR_NONE, &ox, &oy);
			turn_bit = (ox < -8 || ox > 8) ? 1 : 0;
			step_bit = (oy > 8 || oy < -8) ? 1 : 0;

			if (turn_bit && step_bit)
				both_new++;
		}

		ok(both_new == 0, "no direction of the stick sends a turn and a step together",
			both_new, 0);
		ok(both_old > 300, "...where every angle off the two axes used to", both_old, 301);
		printf("  360 whole-degree pushes: %ld sent a turn AND a step before,"
			" %ld do now\n", both_old, both_new);
	}

	/* 6. And what a model cannot do, as look_tank() above says: bmt.bat does
	 * not compile specific\input.cpp, so where this function is called - on
	 * the bypass ticks and on no others, under tomb5.control_modern - is read
	 * against that file and not checked here. What is checked is the
	 * arithmetic, which is the whole of why it lives in game\modern_move.cpp. */
}

/* -------------------------- the camera he turns takes her course with it ----- */

/* What 120 ticks of steering with the camera leave behind. `impulses` is the
 * measure this group exists for: a tick on which her heading did not move at
 * all, followed by one that moves five degrees or more. That is what the
 * held course produced while the player turned the view - the course
 * ignored the camera until the left thumb happened to shift, then the
 * re-latch seated on the camera of that tick and paid the whole accumulated
 * angle at the turn rate, in one frame; under water it read as a stutter
 * sideways in her animation. */
struct STEER
{
	long impulses;		/* still tick, then 5 degrees or more at once */
	long biggest;		/* the biggest single heading step of the run */
	long off_rate;		/* steady ticks whose step was NOT the camera's own */
	long course_moved;	/* ticks on which the held course moved at all */
};

/* `cam_step` is what the right stick gives the orbit per tick; `wobble` is how
 * often the left thumb shifts by three degrees (0 = held perfectly still), which
 * is what makes the old re-latch fire; `player` says the step is the thumb's
 * own (carried into the course) rather than the return's or a carried
 * swing's (it must change nothing). `water` drives ModernWaterMove instead of
 * ModernDirectTurn, because the two are the translation's two turners and this
 * is one function above both of them. */
static void steer_run(long cam_step, long wobble, short anim, long water, long player,
	STEER* s)
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short heading = 0;
	short cam = 0;
	long last_yaw = 1;			/* the first tick has no predecessor to be still */
	long off = 0;				/* the left thumb's own angle, in units */
	long t;

	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = anim;
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	s->impulses = 0;
	s->biggest = 0;
	s->off_rate = 0;
	s->course_moved = 0;

	for (t = 0; t < 120; t++)
	{
		const double rad = (double)off * 3.14159265358979323846 / 32768.0;
		const long x = roundl2(sin(rad) * 120.0);
		const long y = -roundl2(cos(rad) * 120.0);
		long yaw = 0;
		long xrot = 0;
		long stroke = 0;

		/* The orbit's own tick, as game\camera.cpp would have left it: the
		 * world angle has moved by the step, and the situation carries that
		 * step only when it was the player's thumb that did it. */
		cam = (short)ModernWrap((long)cam + cam_step);
		sit.cam_player_step = player ? (short)cam_step : (short)0;

		ModernTranslate(&cfg, &state, x, y, cam, heading, &sit, &out);

		if (water)
			ModernWaterMove(&cfg, &sit, &out, 0, xrot, &yaw, &xrot, &stroke);
		else
			ModernDirectTurn(&cfg, &state, &sit, &out, &yaw);

		if (absl(yaw) > s->biggest)
			s->biggest = absl(yaw);

		if (out.course_step)
			s->course_moved++;

		/* The impulse this test watches for: a still tick and then five
		 * degrees at once. */
		if (!last_yaw && absl(yaw) >= 910)
			s->impulses++;

		/* And the other side of the same claim, on the ticks that are neither
		 * the first nor a thumb shift: the step SHOULD be the camera's own. */
		if (t >= 10 && (!wobble || (t % wobble)) && absl(yaw) != absl(cam_step))
			s->off_rate++;

		last_yaw = yaw;
		heading = (short)ModernWrap((long)heading + yaw);

		if (wobble && !(t % wobble))
			off += 546;		/* three degrees, over cfg.recenter's 384 */
	}
}

static void cam_steer()
{
	/* 3.6 and 6.5 degrees a tick: median camera speeds measured while
	 * steering with the view, in the water and on foot. */
	const long rates[2] = { 655, 1183 };
	const char* names[2] = { "3.6", "6.5" };
	const short anims[2] = { 17, 1 };		/* AS_SWIM and AS_RUN */
	const char* where[2] = { "water", "land " };
	STEER s;
	int r, w;

	printf("\nthe camera he turns takes her course with it\n");

	for (w = 0; w < 2; w++)
		for (r = 0; r < 2; r++)
		{
			/* 1. The left stick held perfectly still. Her heading follows the
			 * view every single tick, by exactly the camera's own step - both
			 * rates are well inside both turn rates, so nothing is being
			 * clipped and "follows" means follows. Before this fix such a run
			 * turned her onto the course the first push seated and then stood
			 * there for 120 ticks while the view swung round her. */
			steer_run(rates[r], 0, anims[w], w == 0, 1, &s);
			ok(s.off_rate == 0, "steering with the camera, her heading follows it every tick",
				s.off_rate, 0);
			ok(s.biggest == rates[r], "...by the camera's own step and no more",
				s.biggest, rates[r]);

			/* 2. And with the left thumb shifting every eight ticks, which is
			 * what makes a re-latch rule fire and pay the whole
			 * accumulated angle in one frame - measured as 34 such impulses in
			 * the water and 57 on foot. Here there are none. */
			steer_run(rates[r], 8, anims[w], w == 0, 1, &s);
			ok(s.impulses == 0, "...and a shifting thumb no longer makes her jump",
				s.impulses, 0);
			printf("  %s at %s deg/tick: %ld impulses, biggest step %ld"
				" (%.1f deg), course moved on %ld of 120 ticks\n",
				where[w], names[r], s.impulses, s.biggest,
				(double)s.biggest * 180.0 / 32768.0, s.course_moved);

			/* 3. And the protection the held course exists for is untouched: the
			 * same camera movement, this time the return falling home or an
			 * animation's carried swing paying out, with no thumb behind it.
			 * The course does not move at all - which is the rule that stops her
			 * circling, and the reason cam_player_step is the stick's own term
			 * and not the orbit's whole step. */
			steer_run(rates[r], 0, anims[w], w == 0, 0, &s);
			ok(s.course_moved == 0, "a camera moving by ITSELF still moves no course",
				s.course_moved, 0);
			ok(s.impulses == 0, "...and no jump comes of it either", s.impulses, 0);
		}

	/* 4. And the whole point, measured directly: the same shifting thumb
	 * with the carry switched off at the situation, which is the code as it
	 * stood before this fix. The impulses come back - so this group can
	 * fail, which is what makes the zeroes above worth anything. */
	steer_run(1183, 8, 1, 0, 0, &s);
	ok(s.impulses > 0, "without the carry the same run stutters, as a play log counted it",
		s.impulses, 1);
	printf("  the counter-proof: the land run at 6.5 deg/tick with the step NOT"
		" handed over shows %ld impulses, biggest %ld (%.1f deg)\n",
		s.impulses, s.biggest, (double)s.biggest * 180.0 / 32768.0);
}

/* -------------------------------------------- the camera follows a flip ----- */

static void camera_carry()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE state;
	short off, pitch;
	long t;

	printf("the camera follows an animation about-turn\n");

	ModernCameraDefaults(&cfg);
	ok(cfg.carry_flip == 27307, "150 degrees in one tick is an animation", cfg.carry_flip,
		27307);
	ok(cfg.carry_ticks == 20, "...and the swing takes 20 ticks, 0.67 s", cfg.carry_ticks, 20);

	/* 1. The swing is the flip, and it puts the offset back. She is running
	 * with the camera on her back; she rolls, which turns her 180 degrees in
	 * one tick. Without this fix the camera is left standing in front of her
	 * face - measured as cam_off going from about 0 to about 180 and staying
	 * there (under 30 degrees of the slow return in 40 ticks), which is what
	 * makes her jump the wrong way afterwards. */
	ModernCameraReset(&state);
	ModernCameraUpdate(&cfg, &state, 0, 0, 0x2000, 1, 0, &off, &pitch);
	ok(off == 0, "seated behind her back", off, 0);

	ModernCameraCarry(&cfg, &state, 0x8000);		/* the roll: +180 degrees */
	ok(absl(state.carry) == 32768, "the roll owes the camera a half turn",
		absl(state.carry), 32768);

	{
		long worst = 0;
		long first = 0;
		long middle = 0;
		long last = 0;
		long wrong = 0;
		long sum = 0;
		short before = state.world_yaw;

		for (t = 0; t < 20; t++)
		{
			const short was = state.world_yaw;
			long step;

			ModernCameraUpdate(&cfg, &state, 0, 0, (short)ModernWrap(0x2000 + 0x8000), 1, 0, &off, &pitch);
			step = ModernWrap((long)state.world_yaw - was);
			sum += step;

			if (step < 0)
				wrong++;

			if (absl(step) > worst)
				worst = absl(step);

			if (!t)
				first = step;

			if (t == 9)
				middle = step;

			if (t == 19)
				last = step;
		}

		ok(off == 0, "after the swing her offset is what it was before the flip", off, 0);
		ok(state.carry == 0, "...and nothing is left pending", state.carry, 0);
		ok(sum == 32768, "...the swing added up to the flip itself", sum, 32768);
		ok(wrong == 0, "...every tick of it went the same way", wrong, 0);
		ok(first < middle && last < middle, "...eased: it starts and ends slower than it runs",
			middle, first);
		printf("  the swing: first tick %ld, middle %ld, last %ld, biggest %ld units\n",
			first, middle, last, worst);

		/* And it is not a cut. The eye rides a circle of camera.target_distance
		 * around her, so the biggest one-tick step of the swing has to stay
		 * under the interpolator's own threshold or the picture jumps instead
		 * of swinging (game\interpolate_math.h, INTERP_CAM_SNAP). */
		{
			const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
			const double radius = 1536.0;
			const long chord = InterpManhattan(
				roundl2(radius * sin((double)worst * turn)),
				0,
				roundl2(radius * (1.0 - cos((double)worst * turn))));

			ok(chord < INTERP_CAM_SNAP, "the swing's fastest tick is not a camera cut",
				chord, INTERP_CAM_SNAP);
			printf("  its fastest tick moves the eye %ld of the %ld the interpolator"
				" calls a cut\n", chord, (long)INTERP_CAM_SNAP);
		}

		(void)before;
	}

	/* 2. The side is the one the camera already leans to. Both ways round end
	 * on the same world angle, so the choice is free and it goes the way the
	 * picture is already looking - a camera hanging to her right swings right.
	 * Dead behind her (offset 0) there is no lean and it takes the right, a
	 * fixed side rather than an accident of the wrap. */
	ModernCameraReset(&state);
	state.offset_yaw = 0x1000;						/* leaning right */
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 0, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, -0x8000);		/* a flip the other way */
	ok(state.carry > 0, "leaning right, the swing goes right whatever the flip's sign",
		(state.carry > 0) ? 1 : 0, 1);

	ModernCameraReset(&state);
	state.offset_yaw = -0x1000;						/* leaning left */
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 0, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, 0x8000);
	ok(state.carry < 0, "...and leaning left, left", (state.carry < 0) ? 1 : 0, 1);

	ModernCameraReset(&state);
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 0, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, -0x8000);
	ok(state.carry > 0, "dead behind her, the tie goes to the fixed side",
		(state.carry > 0) ? 1 : 0, 1);

	/* 3. Under the threshold nothing happens at all. The modern turn's own
	 * biggest step is 30 degrees and the classic automaton's is 8, so 150 in
	 * one tick can only be an animation - but the engine half is what decides
	 * to call this, and a caller that calls it too often costs one comparison
	 * and no swing. */
	ModernCameraReset(&state);
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 0, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, 27306);
	ok(state.carry == 0, "just under 150 degrees is a turn, not a flip", state.carry, 0);
	ModernCameraCarry(&cfg, &state, MODERN_TURN_RATE);
	ok(state.carry == 0, "...and the modern turn's own fastest tick is nowhere near",
		state.carry, 0);
	ModernCameraCarry(&cfg, &state, 27307);
	ok(state.carry != 0, "at 150 exactly it is a flip", (state.carry != 0) ? 1 : 0, 1);

	/* 4. The stick cancels what is left, the moment it is pushed: the right
	 * stick is the player's camera, and a swing still paying out under his
	 * thumb is the camera fighting him. */
	ModernCameraReset(&state);
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 1, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, 0x8000);

	for (t = 0; t < 5; t++)
		ModernCameraUpdate(&cfg, &state, 0, 0, (short)ModernWrap(0x8000), 1, 0, &off, &pitch);

	ok(state.carry != 0, "five ticks in, the swing is still running",
		(state.carry != 0) ? 1 : 0, 1);

	{
		const short held = state.world_yaw;

		ModernCameraUpdate(&cfg, &state, MODERN_STICK_FULL, 0, (short)ModernWrap(0x8000),
			1, 0, &off, &pitch);
		ok(state.carry == 0, "the stick cancels what is left of it", state.carry, 0);
		ok(state.carry_total == 0, "...all of it", state.carry_total, 0);
		ok(ModernWrap((long)state.world_yaw - held) == cfg.yaw_rate,
			"...and that tick is the stick's own step and nothing else",
			ModernWrap((long)state.world_yaw - held), cfg.yaw_rate);
	}

	/* 5. The return stands still while the swing runs, for the same reason it
	 * stands still while he steers: two hands on the same angle would leave the
	 * swing somewhere other than where it promised. It is the fast one anyway -
	 * 0.67 s against the return's four-second half life. */
	ModernCameraReset(&state);
	state.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 1, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, 0x8000);
	ModernCameraUpdate(&cfg, &state, 0, 0, (short)ModernWrap(0x8000), 1, 0, &off, &pitch);
	ok(state.ret == 0, "no return step while the swing is paying out", state.ret, 0);

	for (t = 0; t < 25; t++)
		ModernCameraUpdate(&cfg, &state, 0, 0, (short)ModernWrap(0x8000), 1, 0, &off, &pitch);

	ok(state.ret != 0, "...and it is running again once the swing is done",
		(state.ret != 0) ? 1 : 0, 1);

	/* 6. A second flip while the first is running - two twists in a row.
	 * What is left rides along with the new swing rather than queueing
	 * behind it, so the camera is behind her 0.67 s after the last twist and
	 * not after the first.
	 *
	 * The arithmetic of it is worth reading, because it is not "twice as
	 * much to swing": two twists put her heading back where it started, so the
	 * camera owes nothing at the end - what is pending after the second flip is
	 * the unwinding of the little the first one had already paid. The sum is
	 * what makes that come out right on its own, without a case for it. */
	ModernCameraReset(&state);
	ModernCameraUpdate(&cfg, &state, 0, 0, 0, 1, 0, &off, &pitch);
	ModernCameraCarry(&cfg, &state, 0x8000);

	for (t = 0; t < 5; t++)
		ModernCameraUpdate(&cfg, &state, 0, 0, (short)ModernWrap(0x8000), 1, 0, &off, &pitch);

	{
		const long left = state.carry;

		ModernCameraCarry(&cfg, &state, 0x8000);
		ok(state.carry_tick == 0, "a second twist starts the profile again", state.carry_tick, 0);
		ok(absl(state.carry) == 32768 - absl(left),
			"...and what was left of the first rides along with it, sign and all",
			absl(state.carry), 32768 - absl(left));
		printf("  a second twist five ticks in: %ld was pending, %ld is now\n",
			left, state.carry);

		for (t = 0; t < 20; t++)
			ModernCameraUpdate(&cfg, &state, 0, 0, 0, 1, 0, &off, &pitch);

		ok(off == 0, "...and after it she has her camera behind her again", off, 0);
	}

	/* 7. carry_ticks 0 is the feature switched off, which isolates whether a
	 * fault sits in the swing or in the flip detection. */
	{
		MODERN_CAMERA_CFG off_cfg = cfg;

		off_cfg.carry_ticks = 0;
		ModernCameraReset(&state);
		ModernCameraUpdate(&off_cfg, &state, 0, 0, 0, 1, 0, &off, &pitch);
		ModernCameraCarry(&off_cfg, &state, 0x8000);
		ok(state.carry == 0, "carry_ticks 0: the camera stays where it was", state.carry, 0);
	}

	/* 8. And the profile holds at any carry_ticks a setting could ever
	 * offer. Its numerator was a plain long and 3 * n^3 * 4096 passes 2^31
	 * at n = 56, so a slow swing a later menu might offer would have come
	 * back negative and run backwards. It is 64-bit now; this sweep is the
	 * proof, and what it sweeps is the engine's own function and not a
	 * model of it. */
	{
		MODERN_CAMERA_CFG wide = cfg;
		long bad_back = 0;
		long bad_range = 0;
		long bad_end = 0;
		long n;

		for (n = 1; n <= 120; n++)
		{
			long prev = 0;
			long k;

			wide.carry_ticks = n;

			for (k = 0; k <= n; k++)
			{
				const long paid = ModernCameraCarryStep(&wide, 0x8000, k);

				if (paid < prev)
					bad_back++;

				if (paid < 0 || paid > 0x8000)
					bad_range++;

				prev = paid;
			}

			if (prev != 0x8000)
				bad_end++;
		}

		ok(bad_back == 0, "the swing never runs backwards, at any carry_ticks up to 120",
			bad_back, 0);
		ok(bad_range == 0, "...nor past the swing it was given", bad_range, 0);
		ok(bad_end == 0, "...and it always arrives on the last tick", bad_end, 0);
	}
}

/* ------------------------------------------------------ running and firing -- */

static void firing_run()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	long yaw;

	printf("running while she fires - the strafe is off by default\n");

	/* The case in one tick: a weapon ready, the action key held, and she is
	 * running. With the strafe on she came out of here in MODE AIM with a
	 * side step; the default now answers MODE RUN, a turn of her own, and
	 * IN_FORWARD. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 1;					/* AS_RUN */
	sit.classic = 0;
	sit.drawn = 1;
	sit.action = 1;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	tick_turn(&cfg, &state, S(30000), 0, 0, 0, &sit, &out, &yaw);
	ok(out.mode == MODERN_MODE_RUN, "firing: the running mode", out.mode, MODERN_MODE_RUN);
	ok((out.bits & MODERN_IN_FORWARD) != 0, "...she runs", out.bits & MODERN_IN_FORWARD,
		MODERN_IN_FORWARD);
	ok((out.bits & (MODERN_IN_LSTEP | MODERN_IN_RSTEP)) == 0, "...and does not side-step",
		out.bits & (MODERN_IN_LSTEP | MODERN_IN_RSTEP), 0);
	ok(yaw == MODERN_TURN_RATE, "...the direct turn turns her while she fires", yaw,
		MODERN_TURN_RATE);
	ok((out.bits & MODERN_IN_WALK) == 0, "...and nothing puts the walk key down",
		out.bits & MODERN_IN_WALK, 0);

	/* The same tick, empty-handed, is the same tick. That is the whole of item
	 * 8: firing changes nothing about how she moves. */
	{
		MODERN_STATE bare;
		MODERN_RESULT unarmed;
		long bare_yaw;

		ModernResetState(&bare);
		sit.drawn = 0;
		sit.action = 0;
		tick_turn(&cfg, &bare, S(30000), 0, 0, 0, &sit, &unarmed, &bare_yaw);
		ok(unarmed.bits == out.bits && unarmed.mode == out.mode && bare_yaw == yaw,
			"empty hands answer exactly the same", unarmed.bits, out.bits);
	}

	/* And the rule is still there, one cfg field away, with TROne's own
	 * answer - which is what makes this a default and not a deletion. */
	cfg.aim_strafe = 1;
	ModernResetState(&state);
	sit.drawn = 1;
	sit.action = 1;
	tick_turn(&cfg, &state, S(30000), 0, 0, 0, &sit, &out, &yaw);
	ok(out.mode == MODERN_MODE_AIM, "aim_strafe 1: the strafe is back", out.mode,
		MODERN_MODE_AIM);
	ok((out.bits & MODERN_IN_RSTEP) != 0, "...the side step with it",
		out.bits & MODERN_IN_RSTEP, MODERN_IN_RSTEP);
	ok(yaw == 0, "...and the direct turn stands aside, because MODE AIM is not MODE RUN",
		yaw, 0);

	/* The flips with a weapon drawn are a different rule (jump_dir) and the
	 * aim strafe does not touch them - asserted here at the shipped default,
	 * where jumping() above asserts them with the strafe forced on. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	sit.jump = 1;
	tick(&cfg, &state, 0, S(32000), 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_JUMP, "armed and jumping: still the flip", out.mode,
		MODERN_MODE_JUMP);
	ok(out.bits == MODERN_IN_BACK, "...the back flip, at the shipped default", out.bits,
		MODERN_IN_BACK);
}

/* ---------------------------------------------------------- the weapons ---- */

static void weapon_sequences();	/* the press-hold-release scripts, below */

static void weapons()
{
	long bits;
	int frame;

	printf("the weapon half - Toggle is the original, bit for bit\n");

	/* ModernWeaponBits(combat_hold, ready, holstered, held, bits). TROne's is
	 * trone_modern_weapon_bits(glue, bits, ready, holstered) with the mode and
	 * the key inside the glue struct; ours takes both, because a pure function
	 * cannot ask a struct which mode it is in. specific\input.cpp calls it at
	 * the Combat mode Hold seam - inside if (tomb5.combat_hold), with 1 as its
	 * first argument - for keyboard and pad alike. */

	/* TROne: combatmode.toggle_leaves_the_tank_bits_exactly_as_the_game_wrote_them.
	 * This is the byte-identity case and the reason Toggle is our default: our
	 * draw key already IS a toggle, because game\larafire.cpp turns any
	 * IN_DRAW at LG_READY into LG_UNDRAW_GUNS. So Toggle must hand the bits
	 * back without touching one of them, held key or not. */
	for (frame = 0; frame < 4; frame++)
	{
		const long ready = (frame > 1) ? 1 : 0;
		const long holstered = (frame <= 1) ? 1 : 0;

		ok(ModernWeaponBits(0, ready, holstered, 1, MODERN_IN_DRAW) == MODERN_IN_DRAW,
			"Toggle: a pressed draw key comes back exactly as it went in",
			ModernWeaponBits(0, ready, holstered, 1, MODERN_IN_DRAW), MODERN_IN_DRAW);
		ok(ModernWeaponBits(0, 1, 0, 0, 0) == 0, "Toggle: and no key is still no key",
			ModernWeaponBits(0, 1, 0, 0, 0), 0);
	}

	/* Ours, and stronger than TROne's: EVERY combination. Toggle is the
	 * original, so there must be no input at all on which it differs - that is
	 * what "byte-identical at the defaults" means for this function. */
	{
		long differs = 0;
		long r, h, k, b;

		for (r = 0; r < 2; r++)
			for (h = 0; h < 2; h++)
				for (k = 0; k < 2; k++)
					for (b = 0; b < 4; b++)
					{
						const long in = (b & 1 ? MODERN_IN_DRAW : 0) |
							(b & 2 ? (MODERN_IN_FORWARD | MODERN_IN_WALK) : 0);

						if (ModernWeaponBits(0, r, h, k, in) != in)
							differs++;
					}

		ok(differs == 0, "Toggle differs on none of the 32 combinations", differs, 0);
	}

	/* TROne: combatmode.hold_in_the_tank_profile_draws_while_held_and_holsters_on_release.
	 * Hold asks for the one transition the original has no key for: draw while
	 * the key is down and the hands are empty, holster when it comes up and
	 * the guns are out. A held flag alone would draw and holster without end. */
	ok((ModernWeaponBits(1, 0, 1, 1, MODERN_IN_DRAW) & MODERN_IN_DRAW) != 0,
		"Hold: key down, guns away - draw",
		ModernWeaponBits(1, 0, 1, 1, MODERN_IN_DRAW) & MODERN_IN_DRAW, MODERN_IN_DRAW);
	ok(ModernWeaponBits(1, 0, 0, 1, MODERN_IN_DRAW) == 0,
		"Hold: key down, the draw animation running - nothing",
		ModernWeaponBits(1, 0, 0, 1, MODERN_IN_DRAW), 0);
	ok(ModernWeaponBits(1, 1, 0, 1, MODERN_IN_DRAW) == 0,
		"Hold: key down, guns out - nothing (or it would holster at once)",
		ModernWeaponBits(1, 1, 0, 1, MODERN_IN_DRAW), 0);
	ok((ModernWeaponBits(1, 1, 0, 0, 0) & MODERN_IN_DRAW) != 0,
		"Hold: key up, guns out - holster",
		ModernWeaponBits(1, 1, 0, 0, 0) & MODERN_IN_DRAW, MODERN_IN_DRAW);
	ok(ModernWeaponBits(1, 0, 1, 0, 0) == 0, "Hold: key up, guns away - quiet",
		ModernWeaponBits(1, 0, 1, 0, 0), 0);
	ok(ModernWeaponBits(1, 0, 0, 0, 0) == 0, "Hold: hands busy, key up - quiet",
		ModernWeaponBits(1, 0, 0, 0, 0), 0);

	bits = ModernWeaponBits(1, 0, 1, 1, MODERN_IN_FORWARD | MODERN_IN_WALK);
	ok((bits & (MODERN_IN_FORWARD | MODERN_IN_WALK)) ==
		(MODERN_IN_FORWARD | MODERN_IN_WALK), "Hold: every other bit is left alone",
		bits & (MODERN_IN_FORWARD | MODERN_IN_WALK),
		MODERN_IN_FORWARD | MODERN_IN_WALK);

	/* TROne: modern.held_equip_draws_once_and_interaction_does_not_fire, the
	 * half that is the weapon bits. (The interact half is the Action split's
	 * IN_INTERACT and the shoot half IN_ACTION; neither is in this function.) */
	ok((ModernWeaponBits(1, 0, 1, 1, 0) & MODERN_IN_DRAW) != 0,
		"Hold, held: the draw goes out", ModernWeaponBits(1, 0, 1, 1, 0) & MODERN_IN_DRAW,
		MODERN_IN_DRAW);
	ok(ModernWeaponBits(1, 0, 1, 0, 0) == 0, "Hold, let go with empty hands: nothing",
		ModernWeaponBits(1, 0, 1, 0, 0), 0);
	ok(ModernWeaponBits(1, 1, 0, 1, 0) == 0, "Hold, held with the guns already out: nothing",
		ModernWeaponBits(1, 1, 0, 1, 0), 0);

	/* TROne: combatmode.hold_and_toggle_never_hang_on_an_impossible_request.
	 * The torch and the torch hand report neither ready nor holstered. Eight
	 * ticks of a held key must not queue up a draw that fires a second later. */
	{
		long stuck = 0;

		for (frame = 0; frame < 8; frame++)
			if (ModernWeaponBits(1, 0, 0, 1, 0) != 0)
				stuck++;

		ok(stuck == 0, "Hold: an impossible request never queues up", stuck, 0);

		stuck = 0;

		for (frame = 0; frame < 8; frame++)
			if (ModernWeaponBits(0, 0, 0, 1, 0) != 0)
				stuck++;

		ok(stuck == 0, "Toggle: it cannot even produce one", stuck, 0);
	}

	/* TROne: combatmode.toggle_pulses_once_per_press_in_the_modern_profile.
	 * That is the POINTER profile - one frame of IN_DRAW per press edge - and
	 * it needs an edge memory this function deliberately does not have: in our
	 * Tank profile Toggle IS the original and needs none. A pointer profile
	 * would own the edge, together with the pointer keys it belongs to; this
	 * engine has none. What is ours here is
	 * the other side of the same coin, asserted above: Toggle never invents a
	 * bit and never swallows one. */

	weapon_sequences();
}

/* ------------------------------------------------- Sprint and Duck mode ---- */

/* One tick of ModernModeLatch, as specific\input.cpp calls it: the raw key's
 * own bit is already in the word, the function hands back what leaves the
 * assembly. `bit` is the mode's bit and nothing else is ever in the word here
 * except the one companion bit the "everything else is left alone" checks
 * carry. */
static long latch_tick(long toggle, int down, int drop, long* state, long bit, long extra)
{
	const long in = (down ? bit : 0) | extra;

	return ModernModeLatch(toggle, down, drop, state, bit, in);
}

static void mode_latch()
{
	long state;
	long bits;
	int i;

	printf("the two mode latches - Hold is the original, bit for bit\n");

	/* Hold is the default, and this is the whole of "byte-identical at the
	 * defaults" for this function: every combination of key, drop and word
	 * comes back exactly as it went in, and the state is left at 0 so that a
	 * trip through Toggle and back cannot resurrect a latch. */
	{
		long differs = 0;
		long stuck = 0;
		int down, drop, b;

		for (down = 0; down < 2; down++)
			for (drop = 0; drop < 2; drop++)
				for (b = 0; b < 4; b++)
				{
					const long in = (b & 1 ? MODERN_IN_SPRINT : 0) |
						(b & 2 ? (MODERN_IN_FORWARD | MODERN_IN_WALK) : 0);

					state = MODERN_LATCH_ON | MODERN_LATCH_KEY;	// a latch from before the row was switched off

					if (ModernModeLatch(0, down, drop, &state, MODERN_IN_SPRINT, in) != in)
						differs++;

					if (state != 0)
						stuck++;
				}

		ok(differs == 0, "Hold differs on none of the 16 combinations", differs, 0);
		ok(stuck == 0, "Hold clears the memory on every one of them", stuck, 0);
	}

	/* The press latches and the next press releases. Four ticks: press, hold,
	 * release, press again - the shortest script that says "toggle". */
	state = 0;
	bits = latch_tick(1, 1, 0, &state, MODERN_IN_DUCK, 0);
	ok((bits & MODERN_IN_DUCK) != 0, "Toggle: the press latches the bit on",
		bits & MODERN_IN_DUCK, MODERN_IN_DUCK);
	bits = latch_tick(1, 1, 0, &state, MODERN_IN_DUCK, 0);
	ok((bits & MODERN_IN_DUCK) != 0, "Toggle: holding it down changes nothing",
		bits & MODERN_IN_DUCK, MODERN_IN_DUCK);
	bits = latch_tick(1, 0, 0, &state, MODERN_IN_DUCK, 0);
	ok((bits & MODERN_IN_DUCK) != 0, "Toggle: and the bit survives the release",
		bits & MODERN_IN_DUCK, MODERN_IN_DUCK);
	bits = latch_tick(1, 1, 0, &state, MODERN_IN_DUCK, 0);
	ok((bits & MODERN_IN_DUCK) == 0, "Toggle: the second press lets it go",
		bits & MODERN_IN_DUCK, 0);
	bits = latch_tick(1, 1, 0, &state, MODERN_IN_DUCK, 0);
	ok((bits & MODERN_IN_DUCK) == 0,
		"Toggle: and the key still down afterwards does not latch it again",
		bits & MODERN_IN_DUCK, 0);

	/* A latched bit stands for as long as nobody asks it to go. Thirty ticks
	 * with the key up - a second of play - and it is still there. */
	{
		int lost = 0;

		state = 0;
		latch_tick(1, 1, 0, &state, MODERN_IN_SPRINT, 0);

		for (i = 0; i < 30; i++)
			if ((latch_tick(1, 0, 0, &state, MODERN_IN_SPRINT, 0) & MODERN_IN_SPRINT) == 0)
				lost++;

		ok(lost == 0, "Toggle: a latched bit holds for thirty ticks", lost, 0);
	}

	/* The drop. It clears the latch, and a drop tick is a Hold tick bit for
	 * bit: whatever the key says goes through untouched. That is what lets the
	 * binocular zoom keep reading IN_SPRINT and IN_DUCK while the sight is up
	 * (game\camera.cpp), which is the one reader that needs a held key. */
	state = 0;
	latch_tick(1, 1, 0, &state, MODERN_IN_SPRINT, 0);
	bits = latch_tick(1, 0, 1, &state, MODERN_IN_SPRINT, 0);
	ok((bits & MODERN_IN_SPRINT) == 0, "a drop lets the latch go",
		bits & MODERN_IN_SPRINT, 0);
	ok((state & MODERN_LATCH_ON) == 0, "and takes it out of the memory as well",
		state & MODERN_LATCH_ON, 0);
	bits = latch_tick(1, 1, 1, &state, MODERN_IN_SPRINT, 0);
	ok((bits & MODERN_IN_SPRINT) != 0, "a drop tick hands the raw key through",
		bits & MODERN_IN_SPRINT, MODERN_IN_SPRINT);
	ok((state & MODERN_LATCH_ON) == 0, "and a press on a drop tick latches nothing",
		state & MODERN_LATCH_ON, 0);

	/* The key held across a drop - the level change, and the menu. The key
	 * memory is tracked on drop ticks too, so the tick the drop ends is not a
	 * fresh press and nothing latches by itself. */
	{
		int latched = 0;

		state = MODERN_LATCH_ON | MODERN_LATCH_KEY;	// ducked, key down, and a level starts
		ModernModeLatchDrop(&state);
		ok((state & MODERN_LATCH_ON) == 0, "the outside drop clears the latch",
			state & MODERN_LATCH_ON, 0);
		ok((state & MODERN_LATCH_KEY) != 0, "and leaves the key memory alone",
			state & MODERN_LATCH_KEY, MODERN_LATCH_KEY);

		for (i = 0; i < 8; i++)
			if ((latch_tick(1, 1, 0, &state, MODERN_IN_DUCK, 0) & MODERN_IN_DUCK) != 0)
				latched++;

		ok(latched == 0, "a key held across the boundary latches on none of eight ticks",
			latched, 0);

		bits = latch_tick(1, 0, 0, &state, MODERN_IN_DUCK, 0);
		ok((bits & MODERN_IN_DUCK) == 0, "the release is still no press",
			bits & MODERN_IN_DUCK, 0);
		bits = latch_tick(1, 1, 0, &state, MODERN_IN_DUCK, 0);
		ok((bits & MODERN_IN_DUCK) != 0, "and the next real press latches again",
			bits & MODERN_IN_DUCK, MODERN_IN_DUCK);
	}

	/* Every other bit is left alone, latched, unlatched and dropped. */
	{
		const long extra = MODERN_IN_FORWARD | MODERN_IN_WALK;
		int hurt = 0;

		state = 0;

		for (i = 0; i < 6; i++)
		{
			bits = latch_tick(1, i & 1, (i == 4), &state, MODERN_IN_SPRINT, extra);

			if ((bits & extra) != extra)
				hurt++;
		}

		ok(hurt == 0, "the other bits come through all six ticks untouched", hurt, 0);
	}

	/* And the two modes do not share a thing. Each carries its own state and
	 * its own bit, which is how one row can say Toggle while the other says
	 * Hold - the case the options page makes possible and the seam has to
	 * honour. */
	{
		long sprint = 0;
		long duck = 0;

		bits = MODERN_IN_SPRINT | MODERN_IN_DUCK;
		bits = ModernModeLatch(1, 1, 0, &sprint, MODERN_IN_SPRINT, bits);
		bits = ModernModeLatch(0, 1, 0, &duck, MODERN_IN_DUCK, bits);
		ok((bits & MODERN_IN_SPRINT) != 0 && (bits & MODERN_IN_DUCK) != 0,
			"sprint Toggle beside duck Hold: both bits out on the press tick",
			bits & (MODERN_IN_SPRINT | MODERN_IN_DUCK),
			MODERN_IN_SPRINT | MODERN_IN_DUCK);

		bits = 0;
		bits = ModernModeLatch(1, 0, 0, &sprint, MODERN_IN_SPRINT, bits);
		bits = ModernModeLatch(0, 0, 0, &duck, MODERN_IN_DUCK, bits);
		ok((bits & MODERN_IN_SPRINT) != 0 && (bits & MODERN_IN_DUCK) == 0,
			"and on release only the latched one stays",
			bits & (MODERN_IN_SPRINT | MODERN_IN_DUCK), MODERN_IN_SPRINT);
		ok(duck == 0, "the Hold mode keeps no memory at all", duck, 0);
	}

	/* The bits themselves, like bit_values() does for the other nine: the
	 * mirrors in game\modern_move.h are what specific\input.cpp passes in, and
	 * global\types.h is checked against them by the static_assert in
	 * game\modern_state.cpp - which this tool cannot compile, so the numbers are
	 * written out here as well. */
	ok(MODERN_IN_DUCK == 0x20000000, "MODERN_IN_DUCK is IN_DUCK's own bit",
		MODERN_IN_DUCK, 0x20000000);
	ok(MODERN_IN_SPRINT == 0x40000000, "MODERN_IN_SPRINT is IN_SPRINT's own bit",
		MODERN_IN_SPRINT, 0x40000000);
	ok((MODERN_MOVE_MASK & (MODERN_IN_DUCK | MODERN_IN_SPRINT)) == 0,
		"and neither is in MODERN_MOVE_MASK - the movement seam never writes them",
		MODERN_MOVE_MASK & (MODERN_IN_DUCK | MODERN_IN_SPRINT), 0);
}

/* ------------------------------------------------------- Grab mode (5) ---- */

/* The two family tables, each against a reference of its own, the way
 * classic_states() does it for ModernStateIsClassic: every index 0..138, so a
 * slip in the switch shows up as a mismatch rather than as the same mistake
 * made twice. The reference lists are written out by hand from the
 * routines. */
static void grab_family()
{
	int i;

	printf("Grab mode - the hold-on family, and the duck-and-crawl family\n");

	{
		const short grab[] =
		{
			10, 30, 31, 56, 57, 58, 59, 60, 61, 70, 75, 76, 77, 78, 79,
			82, 83, 90, 91, 99, 100, 101, 102, 103, 107, 108, 109, 110,
			111, 112, 113, 114, 115, 128
		};
		unsigned char want[139];

		for (i = 0; i < 139; i++)
			want[i] = 0;

		for (i = 0; i < (int)(sizeof(grab) / sizeof(grab[0])); i++)
			want[grab[i]] = 1;

		for (i = 0; i <= 138; i++)
			ok(ModernStateIsGrab((short)i) == want[i],
				"all 139 states: the hold-on family matches the reference",
				ModernStateIsGrab((short)i), want[i]);

		ok((int)(sizeof(grab) / sizeof(grab[0])) == 34,
			"...34 of 139 states hold on", (int)(sizeof(grab) / sizeof(grab[0])), 34);
	}

	/* The four borderline states, named one by one so that a change
	 * to one of them has to change a check with a sentence on it. */
	ok(ModernStateIsGrab(11) == 0, "AS_REACH is a catch, not a hold", ModernStateIsGrab(11), 0);
	ok(ModernStateIsGrab(28) == 0, "AS_UPJUMP likewise", ModernStateIsGrab(28), 0);
	ok(ModernStateIsGrab(80) == 0, "AS_ALL4S is where the crawl-to-hang STARTS",
		ModernStateIsGrab(80), 0);
	ok(ModernStateIsGrab(36) == 0, "and a pushblock is not in it at all",
		ModernStateIsGrab(36), 0);

	{
		const short duck[] = { 39, 67, 71, 72, 80, 81, 84, 85, 86, 98, 105, 106 };
		unsigned char want[139];

		for (i = 0; i < 139; i++)
			want[i] = 0;

		for (i = 0; i < (int)(sizeof(duck) / sizeof(duck[0])); i++)
			want[duck[i]] = 1;

		for (i = 0; i <= 138; i++)
			ok(ModernStateIsDuck((short)i) == want[i],
				"all 139 states: the duck-and-crawl family matches the reference",
				ModernStateIsDuck((short)i), want[i]);

		ok((int)(sizeof(duck) / sizeof(duck[0])) == 12,
			"...12 of 139 states are the crouch and the crawl",
			(int)(sizeof(duck) / sizeof(duck[0])), 12);
	}

	// AS_ALL4S has to be in the duck family: ANIM_DUCK_TO_ALL4S runs in
	// AS_ALL4S and sets LG_HANDS_BUSY, so leaving it out of the family drops
	// the latch exactly where a real grab sequence needs it.
	ok(ModernStateIsDuck(80) == 1, "AS_ALL4S is in it - that is the 161b2c bug",
		ModernStateIsDuck(80), 1);
	ok(ModernStateIsDuck(88) == 0, "AS_CRAWL2HANG is NOT - she is leaving the floor",
		ModernStateIsDuck(88), 0);
	ok(ModernStateIsDuck(89) == 0, "and AS_CONTROLLED is a dozen other objects' state",
		ModernStateIsDuck(89), 0);

	// The two families never overlap: no state both holds on and crawls, or one
	// press would have two meanings.
	{
		int both = 0;

		for (i = 0; i <= 138; i++)
			if (ModernStateIsGrab((short)i) && ModernStateIsDuck((short)i))
				both++;

		ok(both == 0, "no state is in both families", both, 0);
	}
}

/* One tick of ModernGrabLatch as specific\input.cpp calls it under "Separate",
 * where the let-go mask is IN_ACTION alone because IN_INTERACT is a different
 * physical key. This is the shape the checks below were originally written
 * against, and it stays that shape so they still say what they said. */
static long grab_tick(long toggle, int down, int family, int drop, long* state, long extra)
{
	const long in = (down ? MODERN_IN_ACTION : 0) | extra;

	return ModernGrabLatch(toggle, down, family, drop, state, MODERN_IN_ACTION,
		MODERN_IN_ACTION, in);
}

/* And one tick under "One key". The one action key raises IN_SELECT |
 * IN_INTERACT | IN_ACTION in a single statement (specific\input.cpp's
 * action_bits), so the whole word goes down and up together and the let-go
 * mask is IN_ACTION_ANY. IN_SELECT is deliberately not in the mask: it
 * confirms in menus, and it is what the third check below watches survive. */
#define GRAB_ONEKEY_WORD	(MODERN_IN_ACTION | MODERN_IN_INTERACT | MODERN_IN_SELECT)
#define GRAB_ONEKEY_RELEASE	(MODERN_IN_ACTION | MODERN_IN_INTERACT)

static long grab_tick_onekey(long toggle, int down, int family, int drop, long* state, long extra)
{
	const long in = (down ? GRAB_ONEKEY_WORD : 0) | extra;

	return ModernGrabLatch(toggle, down, family, drop, state, MODERN_IN_ACTION,
		GRAB_ONEKEY_RELEASE, in);
}

static void grab_latch()
{
	long state;
	long bits;
	int i;

	printf("Grab mode - Hold is the original, Toggle hangs on the state\n");

	/* Hold is the default, over every combination of key, family, drop and
	 * word: the word comes back exactly as it went in and the memory is left at
	 * 0, so a trip through Toggle and back cannot resurrect anything.
	 *
	 * Fix 3 put the let-go mask in as a parameter of its own and the sweep runs
	 * over BOTH of the values specific\input.cpp can hand in - the sixty-four
	 * combinations are the thirty-two of before, once per Action setting. Hold
	 * returns before either mask is read, so neither may make a difference, and
	 * this is the check that says so. */
	{
		long differs = 0;
		long stuck = 0;
		int down, family, drop, b, rel;

		for (down = 0; down < 2; down++)
			for (family = 0; family < 2; family++)
				for (drop = 0; drop < 2; drop++)
					for (b = 0; b < 4; b++)
						for (rel = 0; rel < 2; rel++)
						{
							const long release = rel ? GRAB_ONEKEY_RELEASE :
								MODERN_IN_ACTION;
							const long in = (b & 1 ? GRAB_ONEKEY_WORD : 0) |
								(b & 2 ? (MODERN_IN_FORWARD | MODERN_IN_WALK) : 0);

							state = MODERN_GRAB_RELEASE | MODERN_LATCH_KEY;

							if (ModernGrabLatch(0, down, family, drop, &state,
								MODERN_IN_ACTION, release, in) != in)
								differs++;

							if (state != 0)
								stuck++;
						}

		ok(differs == 0, "Hold differs on none of the 64 combinations", differs, 0);
		ok(stuck == 0, "Hold clears the memory on every one of them", stuck, 0);
	}

	/* A full sequence: she jumps with the key held, catches the ledge,
	 * hangs; the key goes up and she is still hanging; the next press lets go
	 * and the bit stays clear while that press lasts. */
	state = 0;
	bits = grab_tick(1, 1, 0, 0, &state, 0);	// in the air, key down: the catch
	ok((bits & MODERN_IN_ACTION) != 0, "outside the family the key is the key",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
	bits = grab_tick(1, 1, 1, 0, &state, 0);	// she is hanging now, key still down
	ok((bits & MODERN_IN_ACTION) != 0, "the hang keeps the bit while the key is down",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
	bits = grab_tick(1, 0, 1, 0, &state, 0);	// the key goes up
	ok((bits & MODERN_IN_ACTION) != 0, "and she hangs on with the key released",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);

	{
		int lost = 0;

		for (i = 0; i < 30; i++)
			if ((grab_tick(1, 0, 1, 0, &state, 0) & MODERN_IN_ACTION) == 0)
				lost++;

		ok(lost == 0, "thirty ticks of hanging with no key at all", lost, 0);
	}

	bits = grab_tick(1, 1, 1, 0, &state, 0);	// the press that lets go
	ok((bits & MODERN_IN_ACTION) == 0, "a fresh press inside the family lets go",
		bits & MODERN_IN_ACTION, 0);
	ok((state & MODERN_GRAB_RELEASE) != 0, "and is remembered as the let-go press",
		state & MODERN_GRAB_RELEASE, MODERN_GRAB_RELEASE);
	bits = grab_tick(1, 1, 1, 0, &state, 0);	// still down, still hanging for a tick
	ok((bits & MODERN_IN_ACTION) == 0, "while that press lasts the bit stays clear",
		bits & MODERN_IN_ACTION, 0);
	bits = grab_tick(1, 1, 0, 0, &state, 0);	// falling now, key STILL down
	ok((bits & MODERN_IN_ACTION) == 0, "no instant re-grab on the way down",
		bits & MODERN_IN_ACTION, 0);
	bits = grab_tick(1, 0, 0, 0, &state, 0);	// the press ends
	ok((state & MODERN_GRAB_RELEASE) == 0, "the release ends with the press",
		state & MODERN_GRAB_RELEASE, 0);
	bits = grab_tick(1, 1, 0, 0, &state, 0);	// and the next press is a catch again
	ok((bits & MODERN_IN_ACTION) != 0, "and the next press catches the lower ledge",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);

	/* The savegame restored in a hang: no key was ever pressed, the state says
	 * she is hanging, and the bit is there. */
	state = 0;
	bits = grab_tick(1, 0, 1, 0, &state, 0);
	ok((bits & MODERN_IN_ACTION) != 0, "a restore in a hang needs no key",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);

	/* A press that began outside the family is not a let-go. She jumps for the
	 * ledge with the key down and holds it through the whole hang - the case
	 * that matters most. */
	{
		int dropped = 0;

		state = 0;
		grab_tick(1, 1, 0, 0, &state, 0);	// the press, in the air

		for (i = 0; i < 20; i++)
			if ((grab_tick(1, 1, 1, 0, &state, 0) & MODERN_IN_ACTION) == 0)
				dropped++;

		ok(dropped == 0, "the key held from the jump through the hang never lets go",
			dropped, 0);

		bits = grab_tick(1, 0, 1, 0, &state, 0);
		ok((bits & MODERN_IN_ACTION) != 0, "and releasing it later changes nothing",
			bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
	}

	/* The drop. It is a Hold tick bit for bit, it clears a standing release,
	 * and a press on one lets go of nothing. */
	state = 0;
	bits = grab_tick(1, 0, 1, 1, &state, 0);
	ok((bits & MODERN_IN_ACTION) == 0, "a drop tick hands the raw key through (up)",
		bits & MODERN_IN_ACTION, 0);
	bits = grab_tick(1, 1, 1, 1, &state, 0);
	ok((bits & MODERN_IN_ACTION) != 0, "a drop tick hands the raw key through (down)",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
	ok((state & MODERN_GRAB_RELEASE) == 0, "and a press on a drop tick is no let-go",
		state & MODERN_GRAB_RELEASE, 0);

	/* The key held across a drop - out of a menu, into a level. The key memory
	 * is tracked on drop ticks too, so the tick the drop ends is not a fresh
	 * press and she does not let go by herself. */
	{
		int dropped = 0;

		state = MODERN_GRAB_RELEASE | MODERN_LATCH_KEY;
		ModernModeLatchDrop(&state);
		ok((state & MODERN_GRAB_RELEASE) == 0, "the outside drop clears the release",
			state & MODERN_GRAB_RELEASE, 0);
		ok((state & MODERN_LATCH_KEY) != 0, "and leaves the key memory alone",
			state & MODERN_LATCH_KEY, MODERN_LATCH_KEY);

		for (i = 0; i < 8; i++)
			if ((grab_tick(1, 1, 1, 0, &state, 0) & MODERN_IN_ACTION) == 0)
				dropped++;

		ok(dropped == 0, "a key held across the boundary lets go on none of eight ticks",
			dropped, 0);

		grab_tick(1, 0, 1, 0, &state, 0);
		bits = grab_tick(1, 1, 1, 0, &state, 0);
		ok((bits & MODERN_IN_ACTION) == 0, "and the next real press does let go",
			bits & MODERN_IN_ACTION, 0);
	}

	/* "One key", which is the default action setting, and the let-go press
	 * there: on a rope or the parallel bars a press that left IN_INTERACT
	 * standing was inert with no other exit, trapping her until a reload.
	 *
	 * The one action key carries IN_SELECT | IN_INTERACT | IN_ACTION. Thirteen
	 * family states test IN_ACTION_ANY, so a let-go press that leaves
	 * IN_INTERACT standing is no let-go at all. IN_SELECT is the control: it
	 * must come through untouched, because the latch has no business in a
	 * menu. */
	state = 0;
	bits = grab_tick_onekey(1, 0, 1, 0, &state, 0);	// restored on the rope, no key
	ok((bits & MODERN_IN_ACTION) != 0, "One key: the rope is held with no key at all",
		bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
	ok(bits == MODERN_IN_ACTION, "and the delivery is IN_ACTION alone - nothing is operated",
		bits, MODERN_IN_ACTION);
	bits = grab_tick_onekey(1, 1, 1, 0, &state, 0);	// the press that lets go
	ok((bits & MODERN_IN_ACTION) == 0, "One key: the let-go press clears IN_ACTION",
		bits & MODERN_IN_ACTION, 0);
	ok((bits & MODERN_IN_INTERACT) == 0, "and IN_INTERACT with it - this is the fix",
		bits & MODERN_IN_INTERACT, 0);
	ok((bits & MODERN_IN_SELECT) != 0, "and leaves IN_SELECT, which is the menus'",
		bits & MODERN_IN_SELECT, MODERN_IN_SELECT);
	bits = grab_tick_onekey(1, 1, 0, 0, &state, 0);	// still held, off the rope now
	ok((bits & (MODERN_IN_ACTION | MODERN_IN_INTERACT)) == 0,
		"the held press picks nothing up on the way down either",
		bits & (MODERN_IN_ACTION | MODERN_IN_INTERACT), 0);
	ok((bits & MODERN_IN_SELECT) != 0, "IN_SELECT still through on that tick",
		bits & MODERN_IN_SELECT, MODERN_IN_SELECT);
	grab_tick_onekey(1, 0, 0, 0, &state, 0);	// the key goes up
	bits = grab_tick_onekey(1, 1, 0, 0, &state, 0);	// and the next press is whole again
	ok(bits == GRAB_ONEKEY_WORD, "and the next press is the whole action word again",
		bits, GRAB_ONEKEY_WORD);

	/* "Separate" is untouched by all of that, and this is the check that says
	 * so: there IN_INTERACT comes from the OTHER key, so a player holding it
	 * to push at a door while he lets go of a ledge keeps it through the
	 * let-go press. grab_tick is the "Separate" call, mask IN_ACTION alone. */
	{
		const long other = MODERN_IN_INTERACT;

		state = 0;
		bits = grab_tick(1, 0, 1, 0, &state, other);
		ok((bits & MODERN_IN_INTERACT) != 0, "Separate: the other key's bit is there while she hangs",
			bits & MODERN_IN_INTERACT, MODERN_IN_INTERACT);
		bits = grab_tick(1, 1, 1, 0, &state, other);
		ok((bits & MODERN_IN_ACTION) == 0, "Separate: the let-go press still lets go",
			bits & MODERN_IN_ACTION, 0);
		ok((bits & MODERN_IN_INTERACT) != 0, "and does NOT take the other key's bit with it",
			bits & MODERN_IN_INTERACT, MODERN_IN_INTERACT);
		bits = grab_tick(1, 1, 0, 0, &state, other);
		ok((bits & MODERN_IN_INTERACT) != 0, "nor on the ticks the release still stands",
			bits & MODERN_IN_INTERACT, MODERN_IN_INTERACT);
	}

	/* Every other bit is left alone, in the family and out of it - which is
	 * what keeps IN_SELECT the Action button's own under "One key", and what
	 * keeps a pushblock being pushed by a held key: the latch delivers
	 * IN_ACTION and nothing else, and IN_INTERACT is only ever taken away, and
	 * only for the rest of one let-go press. */
	{
		const long extra = MODERN_IN_FORWARD | MODERN_IN_WALK;
		int hurt = 0;

		state = 0;

		for (i = 0; i < 8; i++)
		{
			bits = grab_tick(1, i & 1, (i & 2) != 0, (i == 6), &state, extra);

			if ((bits & extra) != extra)
				hurt++;
		}

		ok(hurt == 0, "the other bits come through all eight ticks untouched", hurt, 0);
	}

	// The bit itself, like bit_values() does for the other nine.
	ok(MODERN_IN_ACTION == 0x40, "MODERN_IN_ACTION is IN_ACTION's own bit",
		MODERN_IN_ACTION, 0x40);
	ok((MODERN_MOVE_MASK & MODERN_IN_ACTION) == 0,
		"and it is not in MODERN_MOVE_MASK - the movement seam never writes it",
		MODERN_MOVE_MASK & MODERN_IN_ACTION, 0);
	ok(MODERN_GRAB_RELEASE == 0x4, "MODERN_GRAB_RELEASE is its own bit beside the two",
		MODERN_GRAB_RELEASE, 0x4);
	// The two bits fix 3 mirrored, by value, the same way as the nine above.
	ok(MODERN_IN_INTERACT == 0x4000, "MODERN_IN_INTERACT is IN_INTERACT's own bit",
		MODERN_IN_INTERACT, 0x4000);
	ok(MODERN_IN_SELECT == 0x100000, "MODERN_IN_SELECT is IN_SELECT's own bit",
		MODERN_IN_SELECT, 0x100000);
	ok(GRAB_ONEKEY_RELEASE == (MODERN_IN_ACTION | MODERN_IN_INTERACT),
		"and IN_ACTION_ANY is the OR of the two - the let-go mask under One key",
		GRAB_ONEKEY_RELEASE, MODERN_IN_ACTION | MODERN_IN_INTERACT);
}

/* Lara's own gun cycle, as a model. The truth table above says what the
 * function answers to one tick; the sequences below say what a player gets,
 * and for that the answer has to be fed back into the state that produced
 * it. This is that feedback, and it is the engine's own chain and nothing
 * invented: game\larafire.cpp asks about IN_DRAW at LG_NO_ARMS and at
 * LG_READY and nowhere else, so while LG_DRAW_GUNS or LG_UNDRAW_GUNS runs
 * the chain has no branch and the animation simply plays out
 * (game\lara2gun.cpp ends the draw at LG_READY and the holster at
 * LG_NO_ARMS). The animation is the debounce.
 *
 * The four states are numbered in the order she walks them. GUN_BUSY is
 * LG_HANDS_BUSY - a ladder, the water, a vehicle - and answers neither truth,
 * which is exactly why it is here: a script that runs through it must come out
 * the other side with nothing queued. */
enum { GUN_AWAY = 0, GUN_DRAWING = 1, GUN_OUT = 2, GUN_HOLSTERING = 3, GUN_BUSY = 4 };

#define GUN_ANIM_TICKS 8	/* any length > 1 makes the point; the real draw is ~10 frames */

typedef struct
{
	int draws;		/* transitions into the draw animation */
	int holsters;		/* transitions into the holster animation */
	int requests;		/* ticks on which IN_DRAW went out at all */
	int state;		/* where she ended up */
} GUN_RUN;

/* One tick: ask ModernWeaponBits with the state's two truths, then let the
 * engine's chain answer. `held` is the key, and `bits` carries the key's own
 * IN_DRAW exactly as specific\input.cpp hands it over at the seam. */
static void gun_tick(int mode, int held, int* state, int* timer, GUN_RUN* run)
{
	const long ready = (*state == GUN_OUT) ? 1 : 0;
	const long holstered = (*state == GUN_AWAY) ? 1 : 0;
	const long in = held ? MODERN_IN_DRAW : 0;
	const long draw = ModernWeaponBits(mode, ready, holstered, (long)held, in) & MODERN_IN_DRAW;

	if (draw)
		run->requests++;

	if (*state == GUN_AWAY && draw)
	{
		*state = GUN_DRAWING;
		*timer = GUN_ANIM_TICKS;
		run->draws++;
	}
	else if (*state == GUN_OUT && draw)
	{
		*state = GUN_HOLSTERING;
		*timer = GUN_ANIM_TICKS;
		run->holsters++;
	}

	if (*state == GUN_DRAWING || *state == GUN_HOLSTERING)
		if (--*timer <= 0)
			*state = (*state == GUN_DRAWING) ? GUN_OUT : GUN_AWAY;
}

/* A script is a string of ticks: '.' the key up, '#' the key down. */
static GUN_RUN gun_script(int mode, int start, const char* script)
{
	GUN_RUN run;
	int state = start;
	int timer = 0;
	const char* c;

	run.draws = 0;
	run.holsters = 0;
	run.requests = 0;

	for (c = script; *c; c++)
		gun_tick(mode, (*c == '#') ? 1 : 0, &state, &timer, &run);

	run.state = state;
	return run;
}

static void weapon_sequences()
{
	GUN_RUN run;

	printf("the weapon half - press, hold, release, fed back through her own state\n");

	/* The gun cycle's rule: a press-hold-release script over the three gun
	 * states gives exactly one draw and one holster. Thirty ticks held is
	 * nearly four draw animations long, so anything that repeated its request
	 * would show up here at once. */
	run = gun_script(1, GUN_AWAY, ".....##############################..............................");
	ok(run.draws == 1, "Hold: one long press draws exactly once", run.draws, 1);
	ok(run.holsters == 1, "Hold: and the release holsters exactly once", run.holsters, 1);
	ok(run.state == GUN_AWAY, "Hold: and she ends with empty hands", run.state, GUN_AWAY);
	ok(run.requests == 2, "Hold: two ticks of the whole script carried the bit",
		run.requests, 2);

	/* The measurement the seam rests on, asserted rather than asserted about:
	 * the SAME script in Toggle really does cycle, because Key(9) is a level
	 * read and larafire turns every tick of it into the next transition. Base
	 * does this today; a player never sees it because a tap is shorter than
	 * the animation. If this ever came out 1 the premise of Hold would be
	 * wrong, so it is worth a check of its own. */
	run = gun_script(0, GUN_AWAY, ".....##############################..............................");
	ok(run.draws >= 2, "Toggle: a held key cycles - the original, measured", run.draws >= 2, 1);
	ok(run.state == GUN_AWAY, "Toggle: the cycle ends where the key leaves it",
		run.state, GUN_AWAY);

	/* CASE: the key is released DURING the draw animation. Nothing may be
	 * queued - the holster is asked for on the first tick she is really
	 * LG_READY, not on the tick the key came up. */
	run = gun_script(1, GUN_AWAY, "##....................");
	ok(run.draws == 1, "Hold: a short press still draws once", run.draws, 1);
	ok(run.holsters == 1, "Hold: and holsters once, after the draw has finished",
		run.holsters, 1);
	ok(run.state == GUN_AWAY, "Hold: and nothing is left over", run.state, GUN_AWAY);

	/* CASE: the hands are busy for the whole script - a ladder, the water, a
	 * vehicle. Neither truth is true, so not one bit goes out, and no request
	 * waits for the moment she is free. */
	run = gun_script(1, GUN_BUSY, "..####..####..####..");
	ok(run.requests == 0, "Hold: busy hands answer nothing at all", run.requests, 0);
	ok(run.state == GUN_BUSY, "Hold: and the state is not touched", run.state, GUN_BUSY);

	/* CASE: she comes out of the water with the key ALREADY down. The tick she
	 * turns LG_NO_ARMS is the tick the draw goes out - one request, not one
	 * per tick of the swim. */
	{
		GUN_RUN busy = gun_script(1, GUN_BUSY, "##########");
		int state = GUN_AWAY;
		int timer = 0;
		int i;

		ok(busy.requests == 0, "Hold: ten ticks of busy, held, queue nothing",
			busy.requests, 0);

		run.draws = 0;
		run.holsters = 0;
		run.requests = 0;

		for (i = 0; i < 30; i++)
			gun_tick(1, 1, &state, &timer, &run);

		ok(run.draws == 1, "Hold: and the first free tick draws, once", run.draws, 1);
		ok(state == GUN_OUT, "Hold: she is out of the water with the guns out",
			state, GUN_OUT);
	}

	/* CASE: a savegame is loaded with the key down and the guns already out.
	 * Held and ready agree, so the load is silent - she does not holster the
	 * weapons the save gave her. */
	run = gun_script(1, GUN_OUT, "####################");
	ok(run.requests == 0, "Hold: a load with the key down and the guns out is silent",
		run.requests, 0);
	ok(run.state == GUN_OUT, "Hold: and they stay out", run.state, GUN_OUT);

	/* ... and the same load with the key UP asks once, which is the agreement
	 * the other way round and the whole point of Hold. */
	run = gun_script(1, GUN_OUT, "....................");
	ok(run.holsters == 1, "Hold: a load with the key up holsters, once", run.holsters, 1);
	ok(run.requests == 1, "Hold: exactly one tick carried it", run.requests, 1);
	ok(run.state == GUN_AWAY, "Hold: and she settles with empty hands", run.state, GUN_AWAY);

	/* And the script that would catch a latch: press, release, press again.
	 * Two presses, two draws, two holsters - a function that remembered
	 * anything between them would come out odd. */
	run = gun_script(1, GUN_AWAY, "############............############............");
	ok(run.draws == 2, "Hold: two presses, two draws", run.draws, 2);
	ok(run.holsters == 2, "Hold: two releases, two holsters", run.holsters, 2);
	ok(run.state == GUN_AWAY, "Hold: and she ends where she started", run.state, GUN_AWAY);
}

/* --------------------------------------------------------- our own sweeps -- */

static void sweeps()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	long bad;
	int a, c, l, i;

	printf("our own sweeps - the four a play test cannot reach\n");

	ModernDefaults(&cfg);

	/* (a) THE WALL INVARIANT, everywhere at once. The blocked case above is
	 * one stick angle at one camera heading; this is the statement itself over
	 * 64 stick directions, 32 camera headings and 4 headings of Lara's, in the
	 * two states that can hold her still: while she is blocked, IN_FORWARD and
	 * a turn bit are NEVER in the same result. If they were, the classic
	 * automaton would take the run goal and she would stop answering. */
	bad = 0;
	{
		long cases = 0;

		sit.classic = 0;
		sit.drawn = 0;
		sit.action = 0;
		sit.jump = 0;
		sit.blocked = 1;
		sit.cam_player_step = 0;

		for (i = 0; i < 2; i++)
		{
			sit.anim_state = i ? 1 : 2;		/* AS_RUN and AS_STOP */

			for (a = 0; a < 64; a++)
			{
				const double rad = 6.283185307179586 * (double)a / 64.0;
				const long sx = roundl2(127.0 * sin(rad));
				const long sy = -roundl2(127.0 * cos(rad));

				for (c = 0; c < 32; c++)
					for (l = 0; l < 4; l++)
					{
						ModernResetState(&state);
						tick(&cfg, &state, sx, sy, (short)(c * 2048), (short)(l * 16384),
							&sit, &out);
						cases++;

						if ((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) &&
							(out.bits & MODERN_IN_FORWARD))
							bad++;

						/* and the rule's own bookkeeping: blocked is reported
						 * exactly when a turn was due. */
						if (out.blocked != (out.turning ? 1 : 0))
							bad++;
					}
			}
		}

		printf("  (a) %ld blocked cases swept\n", cases);
	}
	ok(bad == 0, "(a) blocked: forward and a turn bit never go out together", bad, 0);

	/* The counter-proof, or the sweep above is a tautology: the SAME sweep
	 * with blocked = 0 must produce the pair very often, because running while
	 * turning is the normal case and the whole feel of the scheme. */
	bad = 0;
	{
		long together = 0;

		sit.blocked = 0;
		sit.cam_player_step = 0;
		sit.anim_state = 1;

		for (a = 0; a < 64; a++)
		{
			const double rad = 6.283185307179586 * (double)a / 64.0;
			const long sx = roundl2(127.0 * sin(rad));
			const long sy = -roundl2(127.0 * cos(rad));

			for (c = 0; c < 32; c++)
			{
				ModernResetState(&state);
				tick(&cfg, &state, sx, sy, (short)(c * 2048), 0, &sit, &out);

				if ((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) &&
					(out.bits & MODERN_IN_FORWARD))
					together++;
			}
		}

		ok(together > 0, "(a) unblocked, the same sweep runs and turns at once",
			together > 0 ? 1 : 0, 1);
		printf("  (a) %ld of 2048 unblocked cases run while turning\n", together);
	}

	/* (b) THE DEAD ZONE. At our default there is exactly one silent input, and
	 * it has to be silent in every field - and it has to FORGET the course, or
	 * letting go and pushing again would keep the old heading. Then the same
	 * with TROne's own threshold scaled in, over every stick pair inside it. */
	sit.anim_state = 2;
	sit.blocked = 0;
	sit.cam_player_step = 0;
	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, 0x1234, 0, &sit, &out);
	ok(state.have_ref == 1, "(b) a push seats the course", state.have_ref, 1);

	tick(&cfg, &state, 0, 0, 0x1234, 0, &sit, &out);
	ok(out.active == 0 && out.bits == 0 && out.mode == MODERN_MODE_OFF &&
		out.turning == 0 && out.blocked == 0,
		"(b) at rest: active 0, no bits, mode off", out.active + out.bits + out.mode, 0);
	ok(state.have_ref == 0 && state.spin == 0 && state.aligned == 0 && state.aiming == 0,
		"(b) ...and the held course is forgotten",
		state.have_ref + state.spin + state.aligned + state.aiming, 0);

	bad = 0;
	cfg.dead = S(8000);						/* TROne's 8000 of 32767 */
	{
		long x, y;
		long cases = 0;

		for (x = -40; x <= 40; x++)
			for (y = -40; y <= 40; y++)
			{
				if ((long)sqrt((double)(x * x + y * y)) >= cfg.dead)
					continue;

				ModernResetState(&state);
				state.have_ref = 1;
				state.course = 0x1234;
				tick(&cfg, &state, x, y, 0x2000, 0, &sit, &out);
				cases++;

				if (out.active || out.bits || out.mode != MODERN_MODE_OFF ||
					state.have_ref)
					bad++;
			}

		printf("  (b) %ld stick pairs inside a raised dead zone\n", cases);
	}
	ok(bad == 0, "(b) every pair inside the dead zone is silent and forgets", bad, 0);
	cfg.dead = MODERN_DEAD;

	/* (c) THE CAMERA CUT. Half a turn, mid-hold, from every stick direction
	 * and every camera heading: the course is what the player asked for and a
	 * cut is the room's idea, not his. Five ticks after the cut, because a
	 * fixed camera stays fixed. */
	bad = 0;
	{
		long cases = 0;

		for (a = 0; a < 64; a++)
		{
			const double rad = 6.283185307179586 * (double)a / 64.0;
			const long sx = roundl2(127.0 * sin(rad));
			const long sy = -roundl2(127.0 * cos(rad));

			for (c = 0; c < 32; c++)
			{
				const short cam = (short)(c * 2048);
				short seated;
				int t;

				ModernResetState(&state);
				tick(&cfg, &state, sx, sy, cam, 0, &sit, &out);
				seated = out.target;

				for (t = 0; t < 5; t++)
				{
					tick(&cfg, &state, sx, sy, ModernWrap((long)cam + 0x8000), 0, &sit, &out);
					cases++;

					if (out.target != seated || state.course != seated)
						bad++;
				}
			}
		}

		printf("  (c) %ld ticks after a half-turn camera cut\n", cases);
	}
	ok(bad == 0, "(c) a camera cut of 180 degrees never bends the held course", bad, 0);

	/* (d) The D-pad, and what the medipacks on it cost.
	 *
	 * specific\dxshell.cpp does not OR the D-pad into the stick's two axes, it
	 * ASSIGNS: a direction press REPLACES whatever the stick was saying with
	 * +-20 of 128, a quarter of the way. Under Tank that is right and is
	 * Core's own idea - a direction bit has no size, so a quarter push and a
	 * full one mean the same thing. Under Modern the size IS the speed (20 is
	 * under TROne's walk_below, 62) and the direction IS a course.
	 *
	 * The D-pad now carries the medipacks instead, and the movement seam
	 * reads the left stick's own two axes (joystick_raw_lx/ly) ahead of the
	 * overwritten pair, so the cross costs the movement nothing at all: a
	 * medipack press does not touch her course or speed, even while
	 * running.
	 *
	 * Nothing linked here can run any of that - it is engine code, in two
	 * files this test cannot see. So the block does what a pure test honestly
	 * can: it measures the defect the fix removes, on the translation itself,
	 * with dxshell's own overwrite and input.cpp's own former condition
	 * transcribed and named as transcriptions; and it holds the two statements
	 * about the cross that survive the change.
	 *
	 * cfg.walk_below is set explicitly, the same way walk_threshold() does, so
	 * the counter-proof (what a +-20 reading does to the translation) keeps
	 * proving what it always proved rather than turning into a tautology at
	 * the shipped default of 0. */
	{
		static const long dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		static const long dz[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		const long dpad = 20;		/* specific\dxshell.cpp, both joystick paths */
		long silent = 0;
		long walked = 0;
		long killed = 0;
		long veered = 0;
		long worst = 0;
		long sx, sy;

		cfg.walk_below = 62;	/* explicit: the shipped default is 0 */
		sit.anim_state = 2;

		/* The size of the defect, over the whole square of stick positions
		 * with D-Pad Up held. dxshell assigns joystick_read_y = -20 and leaves
		 * x alone; the old seam then erased the pair when both axes were in
		 * {0, +20, -20} and not both zero. What the player asked for is the
		 * stick itself, which is what the seam hands over now. */
		for (sx = -MODERN_STICK_FULL; sx <= MODERN_STICK_FULL; sx++)
		{
			for (sy = -MODERN_STICK_FULL; sy <= MODERN_STICK_FULL; sy++)
			{
				MODERN_RESULT mine;		/* the stick, which is what she asked for */
				MODERN_RESULT then;		/* what dxshell's assignment leaves of it */
				long ox = sx;			/* dxshell's own assignment, transcribed */
				long oy = -dpad;

				if (!((ox != 0 && ox != dpad && ox != -dpad) ||
					(oy != 0 && oy != dpad && oy != -dpad)) && (ox || oy))
				{
					ox = 0;				/* the old seam's condition, transcribed */
					oy = 0;
				}

				ModernResetState(&state);
				tick(&cfg, &state, sx, sy, 0x1000, 0, &sit, &mine);

				ModernResetState(&state);
				tick(&cfg, &state, ox, oy, 0x1000, 0, &sit, &then);

				if (!mine.active)
					continue;			/* she was not moving; nothing to lose */

				if (!then.active)
				{
					killed++;			/* a tap on the medipack stopped her */
					continue;
				}

				if (then.target != mine.target)
				{
					const long off = absl(ModernWrap((long)then.target - (long)mine.target));

					veered++;

					if (off > worst)
						worst = off;
				}
			}
		}

		/* 770 is not a guess: the overwrite leaves y at -20, so the old
		 * condition fired at exactly the three columns x = -20, 0, +20 (257
		 * rows each) minus the one position where the stick was at rest
		 * anyway. Every one of them is a tick of running turned into a tick of
		 * standing still. */
		ok(killed == 770, "(d) the overwrite plus the old erasure stopped her dead at 770 stick positions",
			killed, 770);

		/* And a quarter circle is a floor, not the number: running backwards
		 * and tapping the medipack turned her a half circle round. */
		ok(worst > 0x4000, "(d) ...and turned the course by more than a quarter circle at the rest",
			worst, 0x4000);

		printf("  measured: one D-Pad Up press cost her %ld of the %ld stick positions outright\n",
			killed, (long)(2 * MODERN_STICK_FULL + 1) * (2 * MODERN_STICK_FULL + 1));
		printf("  measured: and bent the course at %ld more, by up to %.1f degrees\n",
			veered, (double)worst * 360.0 / 65536.0);

		for (i = 0; i < 8; i++)
		{
			MODERN_RESULT gone;
			MODERN_RESULT raw;

			/* What the translation is handed in Modern when the cross is the
			 * only thing pressed: the stick, which is at rest. */
			ModernResetState(&state);
			tick(&cfg, &state, 0, 0, 0x1000, 0, &sit, &gone);

			ModernResetState(&state);
			tick(&cfg, &state, dx[i] * dpad, -dz[i] * dpad, 0x1000, 0, &sit, &raw);

			if (!gone.active && !gone.bits && gone.mode == MODERN_MODE_OFF)
				silent++;

			/* And what a +-20 reading itself still is, so the two halves above
			 * cannot quietly become the same statement: a course and a walk.
			 * The DirectInput path can really deliver it - its smallest
			 * non-zero reading is 18 - which is the other reason the fix is
			 * "keep the stick" rather than "erase this value". */
			if (raw.active && (raw.bits & MODERN_IN_WALK) && raw.mag < cfg.walk_below)
				walked++;
		}

		ok(silent == 8, "(d) ...while a D-pad press alone is still no course, no bits and no mode",
			silent, 8);
		ok(walked == 8, "(d) ...and a genuine +-20 of the stick is a course and a walk, not nothing",
			walked, 8);
	}
}

/* ----------------------------------------------------- the modern camera --- */

/* The orbit: game\modern_camera.cpp, compiled beside this file exactly the
 * way the translation is, for exactly the same reason - a feel number
 * nobody can re-measure is a number nobody can change.
 *
 * None of the expectations below comes from TROne. Their orbit holds a world
 * angle and never returns behind the player at all; ours holds an offset from
 * Lara's own heading and lets it fall back. The numbers here come from two
 * places and the comments say which: the design's four governing sentences,
 * quoted in full in game\modern_camera.h, and the classic camera's own
 * limits, measured out of game\camera.cpp before a line of the orbit was
 * written. */

static void camera_orbit()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE st;
	short yaw;
	short pitch;
	long lp;
	long ticks;

	printf("the orbit - the stick moves it, her turning does not, running brings it back\n");

	ModernCameraDefaults(&cfg);

	/* The constants, held to the origins the header claims for them. A test
	 * that only checks behaviour lets a number drift away from its own
	 * reason; these are the reasons. */
	ok(cfg.dead == 0, "the right stick's dead zone is the left one's: the hardware's",
		cfg.dead, 0);
	ok(cfg.stick_full == MODERN_STICK_FULL, "one scale for both sticks",
		cfg.stick_full, MODERN_STICK_FULL);
	ok(60 * cfg.yaw_rate >= 65000 && 60 * cfg.yaw_rate <= 66100,
		"the default speed takes the camera once round in about 2 s", 60 * cfg.yaw_rate, 65536);
	ok(60 * cfg.pitch_rate >= 32400 && 60 * cfg.pitch_rate <= 33100,
		"...and half a turn of pitch in the same 2 s", 60 * cfg.pitch_rate, 32768);
	ok(cfg.pitch_max == 15470,
		"below her the clamp is ChaseCamera's own (camera.cpp:1012)", cfg.pitch_max, 15470);
	ok(cfg.pitch_min == -10923,
		"above her the sphere's top cap is cut at 60 degrees", cfg.pitch_min, -10923);
	ok(cfg.pitch_home == -1820,
		"the resting elevation is ChaseCamera's own (camera.cpp:1006)", cfg.pitch_home, -1820);
	ok(cfg.hide_dist == 20 + 102,
		"hiding starts at the near plane (20) plus her own radius (102)", cfg.hide_dist, 122);

	/* 1. The stick moves the camera by the rate, as if the camera were a
	 * sphere around her - the axis is the rate, so half a push is half the
	 * speed and a diagonal cannot buy more than a full one. Her heading is
	 * 0 in this block, where the world yaw and the offset are the same number.
	 */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)cfg.yaw_rate, "full right: one rate's worth of yaw", yaw, cfg.yaw_rate);
	ok(st.steering == 1, "...and the orbit knows the player is steering", st.steering, 1);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, -128, 0, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)-cfg.yaw_rate, "full left: the other way, the same rate", yaw, -cfg.yaw_rate);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 64, 0, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)(cfg.yaw_rate / 2), "half deflection is half the rate",
		yaw, cfg.yaw_rate / 2);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 181, 0, 0, 0, 0, &yaw, &pitch);	/* a diagonal reads 181 */
	ok(yaw == (short)cfg.yaw_rate, "a diagonal cannot buy more than full deflection",
		yaw, cfg.yaw_rate);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 0, &yaw, &pitch);
	ok(yaw == 0, "a stick at rest moves nothing", yaw, 0);
	ok(st.steering == 0, "...and is not steering", st.steering, 0);

	/* 360 degrees on the sphere: the yaw wraps, it does not clamp. */
	ModernCameraReset(&st);

	for (lp = 0; lp < 60; lp++)
		ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, 0, &yaw, &pitch);

	ok(absl(yaw) <= 32, "60 ticks of full right is one whole turn, back behind her", yaw, 0);

	/* 2. Her turning does not move the camera: it does not swing itself back
	 * behind her on its own. The camera stays where it is in the world. The
	 * state is the world yaw; the offset the engine's slot wants is derived
	 * from it, so a turn of hers changes the offset by exactly minus the
	 * turn and the view stands still.
	 *
	 * The seating first, because a pure function has no Lara to ask: a reset
	 * says "behind her" and the first tick ties that to the heading it is
	 * handed. */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 0, 0x2000, 0, 0, &yaw, &pitch);
	ok(st.world_yaw == 0x2000, "the first tick seats the world yaw on her heading",
		st.world_yaw, 0x2000);
	ok(yaw == 0, "...which is behind her back", yaw, 0);
	ok(st.seated == 1, "...once, and it says so", st.seated, 1);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 128, 0, 0x2000, 0, 0, &yaw, &pitch);
	ok(st.world_yaw == (short)(0x2000 + cfg.yaw_rate), "the stick moves the WORLD yaw",
		st.world_yaw, 0x2000 + cfg.yaw_rate);
	ok(yaw == (short)cfg.yaw_rate, "...and the offset follows out of it", yaw, cfg.yaw_rate);

	/* A quarter turn of hers, standing, at 1024 units a tick - a little over
	 * her own fast turn rate of 910. */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 0, &yaw, &pitch);

	for (lp = 1; lp <= 16; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, lp * 1024, 0, 0, &yaw, &pitch);

	ok(st.world_yaw == 0, "a quarter turn of Lara leaves the camera exactly where it stood",
		st.world_yaw, 0);
	ok(yaw == (short)-16384, "...and the offset takes all of it, which is what keeps the view still",
		yaw, -16384);

	/* And the whole of his spin video: ten seconds of her spinning on the spot
	 * at her own 3277 a tick, and the camera does not move by one unit. */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 0, &yaw, &pitch);

	for (lp = 1; lp <= 300; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, ModernWrap(lp * 3277), 0, 0, &yaw, &pitch);

	ok(st.world_yaw == 0, "ten seconds of spinning on the spot: the camera stands still",
		st.world_yaw, 0);

	/* 3. Standing, the camera is kept exactly where it was moved to on the
	 * sphere. */
	ModernCameraReset(&st);

	for (lp = 0; lp < 15; lp++)
		ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, 0, &yaw, &pitch);

	ok(yaw == (short)(15 * cfg.yaw_rate), "a quarter turn to the right",
		yaw, 15 * cfg.yaw_rate);

	for (lp = 0; lp < 300; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 0, &yaw, &pitch);

	ok(yaw == (short)(15 * cfg.yaw_rate), "ten seconds of standing: exactly where he left it",
		yaw, 15 * cfg.yaw_rate);
	ok(st.ret == 0, "...and the return never ran", st.ret, 0);

	/* 4. Running, it falls back smoothly and gently: 377 parts of 65536 of
	 * what is left, every tick. The home is her heading now, and the
	 * seating is what lets a test still start from an offset. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);
	ok(st.ret == -94, "running: a 90 degree offset falls by 377/65536 of itself", st.ret, -94);
	ok(yaw == 0x4000 - 94, "...towards her back, the short way round", yaw, 0x4000 - 94);

	ModernCameraReset(&st);
	st.offset_yaw = (short)-0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);
	ok(st.ret == 94, "an offset to the left falls the same way, mirrored", st.ret, 94);

	/* The same, with her facing somewhere else: the world yaw falls towards her
	 * heading and not towards the world's own zero. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0x1000, 1, 0, &yaw, &pitch);
	ok(st.world_yaw == (short)(0x5000 - 94), "the return's home is her heading",
		st.world_yaw, 0x5000 - 94);
	ok(yaw == 0x4000 - 94, "...so the offset falls by the same step", yaw, 0x4000 - 94);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;

	for (ticks = 0; ticks < 1000 && absl(st.offset_yaw) > 0x2000; ticks++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);

	printf("  measured: 90 degrees of offset halve in %ld ticks of running (%.2f s)\n",
		ticks, (double)ticks / 30.0);
	ok(ticks >= 115 && ticks <= 128, "the documented half life, about 4 s of running",
		ticks, 120);

	for (lp = 0; lp < 3000 && st.offset_yaw; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);

	printf("  measured: and reach her back %ld ticks later (%.2f s)\n", lp, (double)lp / 30.0);
	ok(st.offset_yaw == 0, "it really arrives behind her - that is what the floor of the step is for",
		st.offset_yaw, 0);

	/* The return stands still while the player is steering: he turned the
	 * camera and then ran, and a camera that pulls against the thumb
	 * holding it would be a camera fighting the player. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 1, 0, 0, 1, 0, &yaw, &pitch);
	ok(st.ret == 0, "one unit of stick freezes the return", st.ret, 0);
	ok(yaw == 0x4000 + 8, "...and only the stick moves the camera then", yaw, 0x4000 + 8);

	/* The reset, and what "zeroed" means for each of the angles. */
	ModernCameraReset(&st);
	st.world_yaw = 0x1234;
	st.offset_yaw = 0x1234;
	st.offset_pitch = 4321;
	st.seated = 1;
	st.wall = 1;
	st.ret = 5;
	st.steering = 1;
	ModernCameraReset(&st);
	ok(st.offset_yaw == 0 && st.world_yaw == 0, "the reset puts her camera behind her back",
		st.offset_yaw, 0);
	ok(st.seated == 0, "...unseated, so the next tick ties it to wherever she is looking",
		st.seated, 0);
	ok(st.offset_pitch == -1820,
		"...at the classic resting elevation, not at dead level", st.offset_pitch, -1820);
	ok(!st.wall && !st.ret && !st.steering, "...and forgets the wall",
		st.wall + st.ret + st.steering, 0);
}

/* The speed slider's mapping: a right-stick speed felt far too slow next to
 * the left one, calling for a slider like the SFX and music ones. The row is
 * specific\LoadSave.cpp's and arrives from a menu; what can be checked
 * without a menu is the whole of what the number means, and that is one
 * function.
 *
 * The three named points: 0 is 60 deg/s, 100 is 360, and 40 - the default -
 * is 180, the shipped rate, so a player who never touches the row keeps the
 * camera he already has. */
static void camera_speed()
{
	MODERN_CAMERA_CFG cfg;
	long yaw;
	long pitch;
	long prev;
	long speed;
	long jumps = 0;

	printf("the camera speed slider - what 0, 40 and 100 mean in degrees a second\n");

	ModernCameraDefaults(&cfg);

	ModernCameraSpeedRates(0, &yaw, &pitch);
	ok(yaw == 364, "speed 0 is 364 units a tick", yaw, 364);
	ok(pitch == 182, "...and the pitch is half of it", pitch, 182);
	printf("  measured: speed 0 = %.1f deg/s of yaw\n", (double)yaw * 30.0 * 360.0 / 65536.0);
	ok(yaw * 30 * 360 >= 59 * 65536 && yaw * 30 * 360 <= 61 * 65536,
		"...which is 60 deg/s", yaw * 30 * 360 / 65536, 60);

	ModernCameraSpeedRates(MODERN_CAMERA_SPEED_DEFAULT, &yaw, &pitch);
	ok(yaw == 1092, "the default 40 is exactly the former fixed rate of 1092", yaw, 1092);
	ok(pitch == 546, "...and its own 546 of pitch", pitch, 546);
	printf("  measured: speed 40 = %.1f deg/s of yaw\n", (double)yaw * 30.0 * 360.0 / 65536.0);
	ok(yaw * 30 * 360 >= 179 * 65536 && yaw * 30 * 360 <= 181 * 65536,
		"...which is 180 deg/s", yaw * 30 * 360 / 65536, 180);
	ok(cfg.yaw_rate == yaw && cfg.pitch_rate == pitch,
		"and ModernCameraDefaults seats the default through this one function",
		cfg.yaw_rate, yaw);

	ModernCameraSpeedRates(100, &yaw, &pitch);
	ok(yaw == 2184, "speed 100 is 2184 units a tick", yaw, 2184);
	ok(pitch == 1092, "...and half of that in pitch", pitch, 1092);
	printf("  measured: speed 100 = %.1f deg/s of yaw\n", (double)yaw * 30.0 * 360.0 / 65536.0);
	ok(yaw * 30 * 360 >= 359 * 65536 && yaw * 30 * 360 <= 361 * 65536,
		"...which is 360 deg/s, a full turn in one second",
		yaw * 30 * 360 / 65536, 360);
	ok(yaw * 30 * 360 / 65536 > 125,
		"and past the Remaster's own 125 deg/s, so the slider has room over it",
		yaw * 30 * 360 / 65536, 125);

	/* The clamp. The value comes out of the registry, and a key a user can edit
	 * is a key that can hold anything. */
	ModernCameraSpeedRates(-1, &yaw, &pitch);
	ok(yaw == 364, "under the bottom is the bottom", yaw, 364);
	ModernCameraSpeedRates(5000, &yaw, &pitch);
	ok(yaw == 2184, "and over the top is the top", yaw, 2184);

	/* Monotonic and linear: every step of the slider is worth the same 18 or 19
	 * units, so the row moves the camera evenly under the thumb. */
	ModernCameraSpeedRates(0, &prev, &pitch);

	for (speed = 1; speed <= 100; speed++)
	{
		ModernCameraSpeedRates(speed, &yaw, &pitch);

		if (yaw - prev < 18 || yaw - prev > 19)
			jumps++;

		ok(pitch == yaw / 2, "the pitch is half the yaw at every setting", pitch, yaw / 2);
		prev = yaw;
	}

	ok(jumps == 0, "every step of the slider is worth 18 or 19 units of rate", jumps, 0);
}

static void camera_pitch()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE st;
	short yaw;
	short pitch;
	long lp;

	printf("the pitch - which way is up, and the limits the camera already had\n");

	ModernCameraDefaults(&cfg);

	/* Which way is up is not taste here: the classic look camera answered it
	 * once and for all. LookUpDown (game\lara.cpp) turns IN_FORWARD into
	 * head_x_rot -= 364, and a negative x_rot is looking up; looking up from an
	 * orbit means the camera sits BELOW her, which is a positive elevation. */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, 0, &yaw, &pitch);
	ok(pitch == (short)(cfg.pitch_home + cfg.pitch_rate),
		"stick forward looks up, the way the look camera does",
		pitch, cfg.pitch_home + cfg.pitch_rate);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, 0, &yaw, &pitch);
	ok(pitch == (short)(cfg.pitch_home - cfg.pitch_rate), "pulling back looks down",
		pitch, cfg.pitch_home - cfg.pitch_rate);

	cfg.invert_pitch = 1;
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, 0, &yaw, &pitch);
	ok(pitch == (short)(cfg.pitch_home - cfg.pitch_rate),
		"the switch turns that axis round and nothing else",
		pitch, cfg.pitch_home - cfg.pitch_rate);
	cfg.invert_pitch = 0;

	/* The two ends are not the same: below her the camera still runs to
	 * ChaseCamera's own 84.98 degrees; above her the sphere's top cap is cut
	 * at 60, because up there the eye ends up on her own axis and everything
	 * measured from it loses its direction - directly overhead the classic
	 * camera is buggy, changing direction abruptly. */
	ModernCameraReset(&st);

	for (lp = 0; lp < 200; lp++)
		ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, 0, &yaw, &pitch);

	ok(pitch == (short)cfg.pitch_max, "200 ticks of stick forward: it stops 84.98 degrees below her",
		pitch, cfg.pitch_max);

	ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, 0, &yaw, &pitch);
	ok(pitch == (short)cfg.pitch_max, "...and stays there instead of wrapping over the pole",
		pitch, cfg.pitch_max);

	ModernCameraReset(&st);

	for (lp = 0; lp < 200; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, 0, &yaw, &pitch);

	ok(pitch == (short)cfg.pitch_min, "200 ticks of pulling back: it stops at the new cap",
		pitch, cfg.pitch_min);

	ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, 0, &yaw, &pitch);
	ok(pitch == (short)cfg.pitch_min, "...and no wrap there either", pitch, cfg.pitch_min);

	/* And how long the cap takes to reach from the resting elevation, because
	 * that is what the player feels: 9103 units at 546 a tick. The horizontal
	 * radius the cap leaves is the number it was chosen for. */
	ModernCameraReset(&st);

	for (lp = 0; lp < 200 && st.offset_pitch != (short)cfg.pitch_min; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, 0, &yaw, &pitch);

	printf("  measured: the cap is %ld ticks of full stick from the resting elevation (%.2f s),\n",
		lp, (double)lp / 30.0);
	printf("            and leaves the ideal eye a horizontal radius of %.0f units\n",
		1536.0 * cos((double)cfg.pitch_min * 2.0 * 3.14159265358979323846 / 65536.0));
	ok(lp >= 16 && lp <= 18, "about half a second of stick, not a wall he runs into",
		lp, 17);

	/* The height does NOT come back on its own. He described the yaw
	 * returning and said nothing about the height, and observation 2 says what
	 * he did not ask for stays where he put it. The capability exists and is
	 * proved here under an explicit cfg; it is simply not what ships. */
	ModernCameraReset(&st);
	st.offset_pitch = (short)cfg.pitch_max;

	for (lp = 0; lp < 300; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);

	ok(pitch == (short)cfg.pitch_max, "running does not bring the height back",
		pitch, cfg.pitch_max);

	cfg.pitch_return = 1;
	ModernCameraReset(&st);
	st.offset_pitch = (short)cfg.pitch_max;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);
	ok(pitch == 15371, "pitch_return 1: it falls towards the resting elevation at the same rate",
		pitch, 15371);

	for (lp = 0; lp < 3000 && st.offset_pitch != (short)cfg.pitch_home; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);

	ok(st.offset_pitch == (short)cfg.pitch_home, "...and arrives at it, not near it",
		st.offset_pitch, cfg.pitch_home);
}

static void camera_wall()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE st;
	short yaw;
	short pitch;
	long free_ret;
	long wall_ret;
	long free_left;
	long wall_left;
	long lp;

	printf("the wall - it no longer moves the angle, it only hurries the return home\n");

	ModernCameraDefaults(&cfg);

	/* What a wall does is now one thing. A previous mechanism took the angle
	 * geometry forced on the camera into the offset; what that really wrote
	 * into the state was ChaseCamera's quadrant snap, so the next tick asked
	 * for a cardinal plus one stick step, was blocked again, and the stick
	 * was swallowed - measured as 290 of 855 fully pushed ticks that did not
	 * advance or ran against the thumb, the camera effectively unmovable in
	 * that state. That mechanism is gone; `wall` is a flag the engine sets
	 * when the eye was clipped, and the only thing it does is hurry the
	 * return. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 1, &yaw, &pitch);
	ok(st.wall == 1, "the engine says the eye was clipped: the orbit believes it", st.wall, 1);
	ok(yaw == 0x2000, "...and the angle the player dialled does not move", yaw, 0x2000);

	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 0, &yaw, &pitch);
	ok(st.wall == 0, "no wall, no flag", st.wall, 0);
	ok(yaw == 0x2000, "...and nothing moves either way", yaw, 0x2000);

	/* Standing at a wall, for two seconds: nothing at all happens, where the
	 * previous mechanism walked the camera about by every push it was handed. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;

	for (lp = 0; lp < 60; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 1, &yaw, &pitch);

	ok(yaw == 0x2000, "two seconds of standing at a wall: exactly where he left it",
		yaw, 0x2000);

	/* And the stick reaches the camera while the wall lasts, which is the other
	 * half of the same complaint. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;

	for (lp = 0; lp < 15; lp++)
		ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, 1, &yaw, &pitch);

	ok(yaw == (short)(0x2000 + 15 * cfg.yaw_rate),
		"a quarter turn of stick at a wall arrives in full, tick for tick",
		yaw, 0x2000 + 15 * cfg.yaw_rate);

	/* The rate, isolated: the two runs start from the same offset and differ in
	 * one thing only, whether the eye was clipped. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);
	free_ret = st.ret;

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 1, &yaw, &pitch);
	wall_ret = st.ret;

	ok(wall_ret == -754, "at a wall the same offset falls eight times as far in one tick",
		wall_ret, -754);
	ok(absl(wall_ret) > absl(free_ret) * 7,
		"...eight being the factor, not two numbers that happen to differ",
		absl(wall_ret), absl(free_ret) * 7);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;

	for (lp = 0; lp < 30; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 0, &yaw, &pitch);

	free_left = absl(st.offset_yaw);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;

	for (lp = 0; lp < 30; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 1, &yaw, &pitch);

	wall_left = absl(st.offset_yaw);
	printf("  measured: one second of running from 90 degrees leaves %ld units in the open, "
		"%ld at a wall\n", free_left, wall_left);
	ok(wall_left * 2 < free_left,
		"a second at a wall is most of the way home; a second in the open is a nudge",
		wall_left, free_left);

	/* And the fast return is still only a return: the thumb on the stick stops
	 * it at a wall exactly as it stops it in the open. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 1, 0, 0, 1, 1, &yaw, &pitch);
	ok(st.ret == 0, "one unit of stick freezes it at a wall too", st.ret, 0);
	ok(st.wall == 1, "...and the wall is still seen while it does", st.wall, 1);
}

static void camera_hide()
{
	MODERN_CAMERA_CFG cfg;

	printf("too close to draw - the hard cut of observation 4\n");

	ModernCameraDefaults(&cfg);

	ok(ModernCameraHidesLara(&cfg, 0) == 1, "the eye inside her: she is not drawn",
		ModernCameraHidesLara(&cfg, 0), 1);
	ok(ModernCameraHidesLara(&cfg, cfg.hide_dist - 1) == 1, "one unit under the edge: hidden",
		ModernCameraHidesLara(&cfg, cfg.hide_dist - 1), 1);
	ok(ModernCameraHidesLara(&cfg, cfg.hide_dist) == 0,
		"at the edge: drawn - a hard answer, not a fade",
		ModernCameraHidesLara(&cfg, cfg.hide_dist), 0);
	ok(ModernCameraHidesLara(&cfg, 1536) == 0,
		"at the chase camera's own distance: drawn", ModernCameraHidesLara(&cfg, 1536), 0);
	ok(ModernCameraHidesLara(&cfg, 384) == 0,
		"at the camera collision's own push: drawn", ModernCameraHidesLara(&cfg, 384), 0);
}

/* The seam's own arithmetic: everything the engine side does that is not a
 * call into the engine, checked here so that the identities the seam rests
 * on are numbers and not paragraphs.
 *
 * The seam itself is game\camera.cpp and cannot be linked into this tool - it
 * needs a camera, a Lara and a level. What it computes, though, is angle
 * arithmetic, and angle arithmetic goes wrong quietly:
 *
 *   camera.actual_angle = lara_item->pos.y_rot + camera.target_angle
 *                                          (UpdateCameraElevation)
 *   camera.target_angle = world_yaw - lara_item->pos.y_rot
 *                                          (ModernCameraUpdate's derivation)
 *
 * The first adds two shorts into an int and assigns it back to a short; the
 * second is the orbit's own ModernWrap. The claim under test is that the two
 * are inverses of one another at every heading - because if they are not, the
 * camera points somewhere else entirely for exactly the headings nobody tries
 * by hand, and this is the compiler the engine is built with, so the answer
 * here is the answer there. */
static void camera_seam()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE st;
	short yaw;
	short pitch;
	long heading;
	long offset;
	long world;
	long absolute;

	printf("the seam - the angle lines game\\camera.cpp adds around the orbit\n");

	ModernCameraDefaults(&cfg);

	/* 1. The absolute angle is the offset's own wrap. The engine writes the
	 * offset into camera.target_angle and lets UpdateCameraElevation add her
	 * heading to it in a short. */
	for (heading = -32768; heading < 32768; heading += 997)
	{
		for (offset = -32768; offset < 32768; offset += 4093)
		{
			absolute = (short)(heading + offset);
			ok(absolute == ModernWrap(heading + offset),
				"her heading plus the offset is the orbit's own wrap",
				absolute, ModernWrap(heading + offset));
		}
	}

	/* 2. And the engine gets the world angle back: the orbit keeps a world
	 * yaw and hands over world - heading; the engine adds
	 * her heading back in a short. Standing, with the stick at rest, so that
	 * nothing but the derivation is in the answer. */
	for (heading = -32768; heading < 32768; heading += 997)
	{
		for (world = -32768; world < 32768; world += 4093)
		{
			ModernCameraReset(&st);
			st.world_yaw = (short)world;
			st.seated = 1;

			ModernCameraUpdate(&cfg, &st, 0, 0, heading, 0, 0, &yaw, &pitch);

			absolute = (short)(heading + yaw);
			ok(absolute == ModernWrap(world),
				"the engine adds her heading back and arrives at the world angle",
				absolute, ModernWrap(world));
			ok(st.world_yaw == ModernWrap(world),
				"...and the orbit kept the world angle through her heading",
				st.world_yaw, ModernWrap(world));
		}
	}

	/* 3. The seating is the same identity at a level start, where the engine
	 * hands the orbit a heading it has never seen: a fresh state seats itself
	 * behind her, whatever direction that is. */
	for (heading = -32768; heading < 32768; heading += 2731)
	{
		ModernCameraReset(&st);
		ModernCameraUpdate(&cfg, &st, 0, 0, heading, 0, 0, &yaw, &pitch);
		absolute = (short)(heading + yaw);
		ok(absolute == ModernWrap(heading), "a fresh orbit seats itself behind her back",
			absolute, ModernWrap(heading));
	}

	/* 4. And geometry cannot move it, which is what makes it safe for her
	 * movement to be steered by it. Seating her course on
	 * the measured vector eye -> target would let a wall that pulls the
	 * eye in turn that vector into anything at all - measured as 57 jumps
	 * of more than 45 degrees in one tick, her course jumping with them. It
	 * reads the world yaw under the orbit now. The whole point of it is the
	 * two ticks below: a clipped eye is an input to this function and it
	 * leaves the angle exactly where the player put it, standing or running. */
	ModernCameraReset(&st);
	st.world_yaw = 0x2000;
	st.seated = 1;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, 1, &yaw, &pitch);
	ok(st.world_yaw == 0x2000, "a clipped eye does not move the angle her course is seated on",
		st.world_yaw, 0x2000);

	ModernCameraReset(&st);
	st.world_yaw = 0x2000;
	st.seated = 1;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 1, 1, &yaw, &pitch);
	ok(st.world_yaw == (short)(0x2000 + st.ret),
		"...and while she runs it moves by the return and by nothing else",
		st.world_yaw, 0x2000 + st.ret);
	ok(st.ret == -377, "the clipped eye's only effect being to hurry that return",
		st.ret, -377);
}

/* ------------------------------------------ the stick reaches the eye ------ */

/* MoveCamera is a first order lag.
 *
 * The right stick speed felt far too slow next to the left one. Half of that
 * is the rate, which the speed slider answers; the other half is that the
 * rate the orbit turns at is not the rate the picture turns at. Measured:
 * the orbit was already moving 1066 units a tick and the eye-to-target yaw
 * needed about 25 ticks to reach it, 47 per cent at tick 10 and 81 at tick
 * 20. No rate cures that - a lag costs the same fraction of a fast push as
 * of a slow one.
 *
 * What is modelled here is the engine's own four lines and nothing else:
 *
 *     target_elevation  = the orbit's pitch                      ModernCameraOrbit
 *     actual_elevation += (target - actual) >> 3                 UpdateCameraElevation
 *     ideal.x = target.x - 1536 * cos(actual_elevation) * sin(angle)   ChaseCamera
 *     ideal.y = target.y + 1536 * sin(actual_elevation)
 *     ideal.z = target.z - 1536 * cos(actual_elevation) * cos(angle)
 *     pos    += (ideal - pos) / speed                            MoveCamera
 *
 * with her standing still at the origin, the eye in longs because the engine's
 * is, and the division left to C because the engine's truncates the same way.
 * `speed` is 10 behind her (CalculateCamera's own reset) and 8 with a weapon
 * out.
 *
 * `lead` is the change: game\camera.cpp turns the eye about her by this tick's
 * stick step before MoveCamera sees it, and adds the same step to
 * actual_elevation before the >> 3 sees it (ModernCameraLeadEye and
 * ModernCameraOrbit). Both spellings are in here, so the test states the size of
 * the defect as well as the size of the cure.
 *
 * `radius` is camera.target_distance, which is 1536 behind her and 1024 or 2048
 * in the states that ask for it (game\lara.cpp), and `start` is how far out the
 * eye begins as a multiple of it - 1 is a settled camera, more is one arriving
 * from a cut or a released fixed camera. Both of ModernCameraLeadEye's bounds
 * are modelled with them: the outer one that refuses to turn an eye eight
 * sectors away at all, and the scale that keeps the eye's own step down to
 * the ideal's wherever it is.
 *
 * This is a model and not the function. Everything else in this file compiles
 * the engine's own source beside itself; game\camera.cpp cannot be linked
 * without a level, so the body above is a hand-built twin of
 * ModernCameraLeadEye and every number this group publishes about the lead -
 * 99.9 per cent on the first tick, the 407/609/812 sweep - is measured on
 * the twin. One piece of it is shared for real: the outer bound is
 * MODERN_CAMERA_LEAD_MAX out of game\modern_camera.h, so that much cannot
 * drift without breaking this build. The rest - the target_distance scale,
 * the pitch-then-yaw order, the truncating divisions - is kept in step by
 * this paragraph and by the one at the other end, in ModernCameraLeadEye's
 * own header. Change one, change the other. */
struct LEAD_RUN
{
	double first_yaw;	// the eye's yaw step on the FIRST tick, in angle units
	double first_pitch;	// and its elevation step
	double last_yaw;	// the yaw step once the lag has settled
	long to_90;		// ticks until the yaw step is nine tenths of the orbit's
	long worst_eye;		// the largest step the eye itself took, Manhattan
	long worst_lead;	// and the largest the LEAD alone took, before MoveCamera
};

static void lead_run(long speed, long yaw_rate, long pitch_rate, long lead,
	long ticks, double radius, double start, LEAD_RUN* r)
{
	const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
	// The outer bound is the tree's own, not a copy of it: it lives in
	// game\modern_camera.h, so deleting or renaming it breaks this build
	// instead of leaving this model measuring a number the engine gave up.
	const long lead_max = MODERN_CAMERA_LEAD_MAX;
	const long lim = (long)radius;
	MODERN_CAMERA_CFG cfg;
	long ang = 0;
	long tgt_elev = MODERN_CAMERA_PITCH_HOME;
	long act_elev = MODERN_CAMERA_PITCH_HOME;
	long px, py, pz;
	long lp;
	double prev_yaw, prev_pitch;

	ModernCameraDefaults(&cfg);

	r->first_yaw = 0.0;
	r->first_pitch = 0.0;
	r->last_yaw = 0.0;
	r->to_90 = 0;
	r->worst_eye = 0;
	r->worst_lead = 0;

	/* A settled camera stands on its own ideal, which is where a push starts;
	 * `start` past 1 is the eye that is still walking in from somewhere else. */
	px = roundl2(-start * radius * cos((double)act_elev * turn) * sin((double)ang * turn));
	py = roundl2(start * radius * sin((double)act_elev * turn));
	pz = roundl2(-start * radius * cos((double)act_elev * turn) * cos((double)ang * turn));
	prev_yaw = atan2((double)-px, (double)-pz) / turn;
	prev_pitch = atan2((double)py, sqrt((double)px * px + (double)pz * pz)) / turn;

	for (lp = 0; lp < ticks; lp++)
	{
		long step_pitch = pitch_rate;
		long ix, iy, iz, ox, oy, oz, moved;
		double now_yaw, now_pitch, dy;

		/* one tick of the orbit at full deflection, with its own clamp */
		if (tgt_elev + step_pitch > cfg.pitch_max)
			step_pitch = cfg.pitch_max - tgt_elev;

		ang += yaw_rate;
		tgt_elev += step_pitch;

		if (lead)
			act_elev += step_pitch;

		act_elev += (tgt_elev - act_elev) >> 3;

		ix = roundl2(-radius * cos((double)act_elev * turn) * sin((double)ang * turn));
		iy = roundl2(radius * sin((double)act_elev * turn));
		iz = roundl2(-radius * cos((double)act_elev * turn) * cos((double)ang * turn));

		ox = px;
		oy = py;
		oz = pz;

		/* ModernCameraLeadEye, and both of its bounds. The outer one refuses
		 * an eye that is not orbiting her at all; the scale keeps the eye's
		 * own arc down to the one it would have made at the orbit's own
		 * radius, which is the radius every promise in this file was
		 * measured at. `lead` 2 is the same rotation without that scale -
		 * the earlier, unscaled form - so the run can state the size of the
		 * defect as well as the size of the cure. */
		if (lead && absl(px) <= lead_max && absl(py) <= lead_max && absl(pz) <= lead_max)
		{
			const double h = sqrt((double)px * px + (double)pz * pz);
			const long lx = px, ly = py, lz = pz;
			long eye_pitch = step_pitch;
			long eye_yaw = yaw_rate;
			long moved_lead;

			if (lead == 1 && lim > 0)
			{
				/* r, lim and the truncating division are camera.cpp's own. */
				const long rr = (long)sqrt(h * h + (double)py * py);

				if (rr > lim)
				{
					eye_pitch = eye_pitch * lim / rr;
					eye_yaw = eye_yaw * lim / rr;
				}
			}

			if (eye_pitch && h > 0.0)
			{
				const double c = cos((double)eye_pitch * turn);
				const double s = sin((double)eye_pitch * turn);
				const double nh = h * c - (double)py * s;

				py = roundl2((double)py * c + h * s);
				px = roundl2((double)px * (nh < 0.0 ? 0.0 : nh) / h);
				pz = roundl2((double)pz * (nh < 0.0 ? 0.0 : nh) / h);
			}

			if (eye_yaw)
			{
				const double c = cos((double)eye_yaw * turn);
				const double s = sin((double)eye_yaw * turn);
				const long nx = roundl2((double)px * c + (double)pz * s);

				pz = roundl2((double)pz * c - (double)px * s);
				px = nx;
			}

			moved_lead = InterpManhattan(px - lx, py - ly, pz - lz);

			if (moved_lead > r->worst_lead)
				r->worst_lead = moved_lead;
		}

		px += (ix - px) / speed;
		py += (iy - py) / speed;
		pz += (iz - pz) / speed;

		moved = InterpManhattan(px - ox, py - oy, pz - oz);

		if (moved > r->worst_eye)
			r->worst_eye = moved;

		now_yaw = atan2((double)-px, (double)-pz) / turn;
		now_pitch = atan2((double)py, sqrt((double)px * px + (double)pz * pz)) / turn;
		dy = now_yaw - prev_yaw;

		while (dy < -32768.0)
			dy += 65536.0;

		while (dy > 32768.0)
			dy -= 65536.0;

		if (!lp)
		{
			r->first_yaw = dy;
			r->first_pitch = now_pitch - prev_pitch;
		}

		if (!r->to_90 && yaw_rate && dy >= 0.9 * (double)yaw_rate)
			r->to_90 = lp + 1;

		r->last_yaw = dy;
		prev_yaw = now_yaw;
		prev_pitch = now_pitch;
	}
}

/* The stamp that says the orbit is still the camera.
 *
 * game\camera.cpp keeps two answers that are only worth anything while the orbit
 * really ticks every tick - whether mgLOS clipped the eye, and what world angle
 * the player has dialled - and the orbit does not tick every tick. The one that
 * cost something was the second: ModernCameraActive() reads the option, Lara,
 * the title, the spotcam, the cutscene and first person, but not which camera
 * the engine is running, so through a rich2 sniper camera - the one level camera
 * Modern deliberately keeps - it stays true while FixedCamera has the view and
 * the orbit stands still. game\modern_state.cpp would go on building her course
 * against the world angle the orbit was frozen at, which is a camera that
 * is not on screen.
 *
 * Both answers go through ModernCameraTickFollows, which is the whole of the
 * rule and is checked here: the engine side is two stamps and two calls, and
 * this is the arithmetic under them - including the wrap, which the engine
 * reaches once every thirty-six minutes and a test reaches in a line. */
static void camera_stamp()
{
	long lp;
	long live;

	printf("the tick stamp - the orbit's answers expire when the orbit stops\n");

	ok(ModernCameraTickFollows(1, 0) == 1, "the tick after: one apart is a live answer",
		ModernCameraTickFollows(1, 0), 1);
	ok(ModernCameraTickFollows(1000, 999) == 1, "and anywhere in the counter's range",
		ModernCameraTickFollows(1000, 999), 1);
	ok(ModernCameraTickFollows(1000, 1000) == 0, "the SAME tick asking twice is not one apart",
		ModernCameraTickFollows(1000, 1000), 0);
	ok(ModernCameraTickFollows(1000, 998) == 0, "two ticks is a gap: a sniper camera took one",
		ModernCameraTickFollows(1000, 998), 0);
	ok(ModernCameraTickFollows(1000, 900) == 0, "and a hundred is the flyby that took a hundred",
		ModernCameraTickFollows(1000, 900), 0);
	ok(ModernCameraTickFollows(1000, 1001) == 0, "a stamp from the FUTURE answers nothing",
		ModernCameraTickFollows(1000, 1001), 0);

	/* The wrap, which is why the subtraction is done in the counter's own width.
	 * GlobalCounter is a ushort and the level it counts runs for hours. */
	ok(ModernCameraTickFollows(0, 65535) == 1, "over the wrap 65535 -> 0 is still one apart",
		ModernCameraTickFollows(0, 65535), 1);
	ok(ModernCameraTickFollows(0, 65534) == 0, "and 65534 -> 0 is still a gap",
		ModernCameraTickFollows(0, 65534), 0);
	ok(ModernCameraTickFollows(0, 0) == 0, "a reset leaves both at 0, which answers nothing",
		ModernCameraTickFollows(0, 0), 0);

	/* Exactly one stamp in the whole counter is live for any given tick, which is
	 * the property the engine leans on: the flag cannot survive a break of any
	 * length, so nothing it guards has to be cleared on the way out of one. */
	live = 0;

	for (lp = 0; lp < 65536; lp++)
		live += ModernCameraTickFollows(4321, (unsigned short)lp);

	ok(live == 1, "of all 65536 possible stamps, exactly one is a live answer at any tick",
		live, 1);

	/* And walked as the engine walks it: the orbit stamps every tick it runs, so
	 * the seam's reference is live on every one of them; the moment a camera
	 * takes over, the very next tick declines and every tick after it declines
	 * too - the stale angle can never come back without the orbit coming back. */
	live = 0;

	for (lp = 1; lp <= 300; lp++)
		live += ModernCameraTickFollows((unsigned short)lp, (unsigned short)(lp - 1));

	ok(live == 300, "300 ticks of an orbit that runs: the reference is live on all 300",
		live, 300);

	live = 0;

	for (lp = 1; lp <= 300; lp++)
		live += ModernCameraTickFollows((unsigned short)(300 + lp), 300);

	ok(live == 1, "300 ticks of a sniper camera: live on the first only, then never again",
		live, 1);
}

static void camera_lead()
{
	MODERN_CAMERA_CFG cfg;
	const double radius = 1536.0;		// camera.target_distance, camera.cpp
	LEAD_RUN slow, fast;
	long yaw_max, pitch_max;
	long speed;

	printf("the lead - the stick's own step reaches the eye in the tick it was made\n");

	ModernCameraDefaults(&cfg);
	ModernCameraSpeedRates(MODERN_CAMERA_SPEED_FULL, &yaw_max, &pitch_max);

	/* The yaw, at both dampings and at both ends of the slider. The first run of
	 * each pair is what shipped, and it is here to be printed rather than
	 * asserted: a test that only proves the cure cannot say how large the defect
	 * was. */
	for (speed = 10; speed >= 8; speed -= 2)
	{
		lead_run(speed, cfg.yaw_rate, 0, 0, 60, radius, 1.0, &slow);
		lead_run(speed, cfg.yaw_rate, 0, 1, 60, radius, 1.0, &fast);

		printf("  measured: speed %ld, the default rate %ld - the eye moved %.0f units on the\n",
			speed, cfg.yaw_rate, slow.first_yaw);
		printf("            first tick (%.1f%% of the orbit's step, %ld ticks to nine tenths);"
			" it moves %.0f now (%.1f%%)\n",
			100.0 * slow.first_yaw / (double)cfg.yaw_rate, slow.to_90, fast.first_yaw,
			100.0 * fast.first_yaw / (double)cfg.yaw_rate);

		ok(fast.first_yaw >= 0.9 * (double)cfg.yaw_rate,
			"the first tick of a push is at least nine tenths of the orbit's step",
			roundl2(fast.first_yaw), roundl2(0.9 * (double)cfg.yaw_rate));
		ok(fast.to_90 == 1, "...which is to say it arrives on the first tick and not the 18th",
			fast.to_90, 1);
		ok(slow.first_yaw < 0.2 * (double)cfg.yaw_rate,
			"and the damping really did hold nine tenths of it back before",
			roundl2(slow.first_yaw), roundl2(0.2 * (double)cfg.yaw_rate));

		/* The lag is not removed, only led past: what it settles at is what it
		 * always settled at, the orbit's own rate. */
		ok(absl(roundl2(fast.last_yaw) - cfg.yaw_rate) < cfg.yaw_rate / 32,
			"a held push still settles at exactly the orbit's rate",
			roundl2(fast.last_yaw), cfg.yaw_rate);

		lead_run(speed, yaw_max, 0, 1, 60, radius, 1.0, &fast);
		ok(fast.first_yaw >= 0.9 * (double)yaw_max,
			"at the top of the speed slider too", roundl2(fast.first_yaw),
			roundl2(0.9 * (double)yaw_max));
	}

	/* The pitch has two dampings in series, the elevation's own >> 3 and then
	 * MoveCamera's, so it was the worse of the two: an eighth of a tenth. Both
	 * are led past - the elevation in ModernCameraOrbit, the eye in
	 * ModernCameraLeadEye - and the check is the same shape as the yaw's. */
	lead_run(10, 0, cfg.pitch_rate, 0, 60, radius, 1.0, &slow);
	lead_run(10, 0, cfg.pitch_rate, 1, 60, radius, 1.0, &fast);

	printf("  measured: the pitch moved %.1f units on the first tick of a push"
		" (%.1f%% of %ld); it moves %.0f now (%.1f%%)\n",
		slow.first_pitch, 100.0 * slow.first_pitch / (double)cfg.pitch_rate,
		cfg.pitch_rate, fast.first_pitch,
		100.0 * fast.first_pitch / (double)cfg.pitch_rate);

	ok(fast.first_pitch >= 0.9 * (double)cfg.pitch_rate,
		"the pitch step arrives on the first tick as well",
		roundl2(fast.first_pitch), roundl2(0.9 * (double)cfg.pitch_rate));
	ok(slow.first_pitch < 0.1 * (double)cfg.pitch_rate,
		"...where an eighth of a tenth of it used to",
		roundl2(slow.first_pitch), roundl2(0.1 * (double)cfg.pitch_rate));
}

/* The 60 fps cut detection must not fire on the orbit's own steps, which is
 * the reason this group includes game\interpolate_math.h rather than
 * quoting a threshold out of it. Both sides of the comparison are
 * therefore the tree's: the orbit's rates come from ModernCameraDefaults and
 * the rule comes from InterpCamAimCut and INTERP_CAM_SNAP.
 *
 * What is being measured: the chase camera's ideal eye sits at
 *
 *     x = target.x - 1536 * cos(elevation) * sin(angle)
 *     y = target.y + 1536 * sin(elevation)
 *     z = target.z - 1536 * cos(elevation) * cos(angle)
 *
 * (ChaseCamera; 1536 is camera.target_distance). One tick of a fully
 * deflected right stick moves `angle` by yaw_rate and `elevation` by
 * pitch_rate, so the worst case is the largest step that pair can make
 * anywhere on the sphere, and it is swept rather than argued.
 *
 * The real eye moves this much and not less; MoveCamera's damping does
 * not make it smaller: `pos += (ideal - pos) /
 * speed` is a first order lag, and a lag filter fed a constant velocity
 * settles at that same velocity. It is smaller for the first few ticks of
 * a push and equal for as long as the push lasts. The margin below is
 * therefore the whole margin. */
static long camcut_worst(long yaw_rate, long pitch_rate, long pitch_min, long pitch_max,
	double radius, long* cuts)
{
	const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
	long worst = 0;
	long elev;
	long ang;

	*cuts = 0;

	for (elev = pitch_min; elev <= pitch_max; elev += 128)
	{
		long elev2 = elev + pitch_rate;

		if (elev2 > pitch_max)
			elev2 = pitch_max;

		for (ang = 0; ang < 65536; ang += 128)
		{
			const long ang2 = ang + yaw_rate;
			const double r1 = radius * cos((double)elev * turn);
			const double r2 = radius * cos((double)elev2 * turn);
			const long x1 = roundl2(-r1 * sin((double)ang * turn));
			const long z1 = roundl2(-r1 * cos((double)ang * turn));
			const long y1 = roundl2(radius * sin((double)elev * turn));
			const long x2 = roundl2(-r2 * sin((double)ang2 * turn));
			const long z2 = roundl2(-r2 * cos((double)ang2 * turn));
			const long y2 = roundl2(radius * sin((double)elev2 * turn));
			const long eye = InterpManhattan(x2 - x1, y2 - y1, z2 - z1);

			if (eye > worst)
				worst = eye;

			/* And the view DIRECTION, by the interpolator's own rule. The
			 * target is Lara and the orbit does not move her, so it stands at
			 * the origin on both sides: what is left is the eye's own travel
			 * measured as an aim, and at a radius of 1536 - under
			 * INTERP_CAM_FAR_TARGET - that rule is the absolute one. */
			if (InterpCamAimCut(x1, y1, z1, 0, 0, 0, x2, y2, z2, 0, 0, 0, INTERP_CAM_SNAP))
				(*cuts)++;
		}
	}

	return worst;
}

static void camera_camcut()
{
	MODERN_CAMERA_CFG cfg;
	const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
	const double radius = 1536.0;		// camera.target_distance, camera.cpp
	long worst_eye;
	long cuts = 0;
	long yaw_rate;
	long pitch_rate;

	printf("the camera cut - the orbit's step against the interpolator's own threshold\n");

	ModernCameraDefaults(&cfg);

	worst_eye = camcut_worst(cfg.yaw_rate, cfg.pitch_rate, cfg.pitch_min, cfg.pitch_max,
		radius, &cuts);

	printf("  measured: at the DEFAULT speed the worst tick of a fully pushed right stick\n");
	printf("            moves the ideal eye %ld units (Manhattan) against INTERP_CAM_SNAP %ld"
		" - a margin of %.1f\n",
		worst_eye, (long)INTERP_CAM_SNAP, (double)INTERP_CAM_SNAP / (double)worst_eye);

	ok(worst_eye < INTERP_CAM_SNAP,
		"the orbit's own step never trips the eye test", worst_eye, INTERP_CAM_SNAP);
	ok(worst_eye * 2 < INTERP_CAM_SNAP,
		"...and not by a hair: twice the step is still under it", worst_eye * 2,
		INTERP_CAM_SNAP);
	ok(cuts == 0, "nor the view direction test, anywhere on the sphere", cuts, 0);

	/* And at the top of the slider, which is the whole reason this sweep is
	 * run twice: the speed row can ask for twice the step the orbit shipped
	 * with, and a promise that held at 1092 units a tick is not a promise
	 * about 2184. */
	ModernCameraSpeedRates(MODERN_CAMERA_SPEED_FULL, &yaw_rate, &pitch_rate);
	worst_eye = camcut_worst(yaw_rate, pitch_rate, cfg.pitch_min, cfg.pitch_max,
		radius, &cuts);

	printf("  measured: at speed 100 (%ld units a tick, %ld of pitch) it moves %ld"
		" - a margin of %.1f\n",
		yaw_rate, pitch_rate, worst_eye, (double)INTERP_CAM_SNAP / (double)worst_eye);

	ok(worst_eye < INTERP_CAM_SNAP,
		"the fastest the slider can be asked for does not trip it either",
		worst_eye, INTERP_CAM_SNAP);
	ok(cuts == 0, "nor the view direction test at that speed", cuts, 0);

	/* And the eye itself, now that it is led. The sweep above is the ideal's
	 * step, and while MoveCamera damped everything that was the honest upper
	 * bound for the real eye as well. The lead changes what has to be
	 * proved: the eye is turned about her by the stick's own step before the
	 * damping sees it, so on the first tick of a push it moves by the whole
	 * step rather than a tenth of it. The step is the same rotation through
	 * the same angles - what differs is the radius it is applied at, and the
	 * eye's is at most camera.target_distance, which is the radius the sweep
	 * above already uses. The run below measures it instead of arguing it,
	 * at the slider's maximum and at both dampings. */
	{
		LEAD_RUN led;
		long led_worst = 0;
		long speed;

		for (speed = 10; speed >= 8; speed -= 2)
		{
			lead_run(speed, yaw_rate, pitch_rate, 1, 400, radius, 1.0, &led);

			if (led.worst_eye > led_worst)
				led_worst = led.worst_eye;
		}

		printf("  measured: the LED eye's worst tick at speed 100 is %ld"
			" - a margin of %.1f\n", led_worst, (double)INTERP_CAM_SNAP / (double)led_worst);

		ok(led_worst < INTERP_CAM_SNAP,
			"the eye the stick leads does not trip the cut detection either",
			led_worst, INTERP_CAM_SNAP);
		ok(led_worst <= worst_eye + 64,
			"...and it is the ideal's own step, at the eye's own radius",
			led_worst, worst_eye + 64);
	}

	/* And the eye that is still walking in: everything above is measured
	 * with the eye at the orbit's own radius, which is where it lives once
	 * it has settled. It does not start there: after a flyby, a cutscene, a
	 * room change or a released fixed camera it walks in from thousands of
	 * units away with camera.speed 10, and a player already holding the
	 * right stick has those ticks led. A rotation moves the eye along a
	 * chord that grows with the radius, so the lead step at four sectors out
	 * is four times the one this file measured - and the promise that the
	 * orbit never makes the interpolator cut was never measured there at
	 * all.
	 *
	 * So both spellings are swept, out to the distance MODERN_CAMERA_LEAD_MAX
	 * stops the lead at, and at each of the three distances the engine can set
	 * (camera.target_distance is 1536 behind her, 1024 in the puzzle states and
	 * 2048 on the rope - game\lara.cpp). `lead` 2 is the earlier, unscaled
	 * form and 1 is the scaled one; the defect gets a number and the cure
	 * gets a bound. */
	{
		const double dists[3] = { 1024.0, 1536.0, 2048.0 };
		long worst_broken = 0;
		double broken_from = 0.0;
		long d;

		printf("  measured: the eye that is still walking in, at each target_distance\n");
		printf("            (the ideal's own worst step at that distance is the bound the\n");
		printf("             lead promises to stay inside; the threshold is %ld)\n",
			(long)INTERP_CAM_SNAP);

		for (d = 0; d < 3; d++)
		{
			long dist_fixed = 0;
			long dist_broken = 0;
			long dist_cuts = 0;
			long ideal;
			long speed;
			double start;

			/* The ideal's own step at THIS distance - the promise's right hand
			 * side, swept the same way the two above it are. */
			ideal = camcut_worst(yaw_rate, pitch_rate, cfg.pitch_min, cfg.pitch_max,
				dists[d], &dist_cuts);

			for (speed = 10; speed >= 8; speed -= 2)
				for (start = 1.0; start <= 8.0; start += 0.25)
				{
					LEAD_RUN fixed_run, broken_run;

					lead_run(speed, yaw_rate, pitch_rate, 1, 120, dists[d], start, &fixed_run);
					lead_run(speed, yaw_rate, pitch_rate, 2, 120, dists[d], start, &broken_run);

					if (fixed_run.worst_lead > dist_fixed)
						dist_fixed = fixed_run.worst_lead;

					if (broken_run.worst_lead > dist_broken)
						dist_broken = broken_run.worst_lead;

					/* Where the unscaled lead first passes the threshold - the
					 * number that matters most about this defect. */
					if (broken_run.worst_lead > INTERP_CAM_SNAP &&
						(broken_from == 0.0 || start * dists[d] < broken_from))
						broken_from = start * dists[d];
				}

			printf("    distance %4ld: ideal %4ld, led %4ld (margin %.1f), unscaled %5ld\n",
				(long)dists[d], ideal, dist_fixed,
				(double)INTERP_CAM_SNAP / (double)dist_fixed, dist_broken);

			ok(dist_fixed < INTERP_CAM_SNAP,
				"the led eye never outruns the interpolator, however far out it starts",
				dist_fixed, INTERP_CAM_SNAP);
			ok(dist_fixed <= ideal + 64,
				"...because the scale holds it to the ideal's own step at that distance",
				dist_fixed, ideal + 64);
			ok(ideal < INTERP_CAM_SNAP,
				"and the ideal itself is under the threshold at that distance too",
				ideal, INTERP_CAM_SNAP);
			ok(dist_cuts == 0, "no aim cut at that distance either", dist_cuts, 0);

			if (dist_broken > worst_broken)
				worst_broken = dist_broken;
		}

		printf("    the defect: unscaled, the lead alone reaches %ld - %.1f times the"
			" threshold -\n", worst_broken, (double)worst_broken / (double)INTERP_CAM_SNAP);
		printf("            and it first passes it with the eye about %ld units out\n",
			(long)broken_from);

		ok(worst_broken > INTERP_CAM_SNAP,
			"and the defect was real: unscaled, the lead alone trips the threshold",
			worst_broken, INTERP_CAM_SNAP);
	}

	/* The one thing that cuts at a wall is vanilla's, and it is worth
	 * a number so that nobody hunts for it in the orbit: the five candidate
	 * positions of ChaseCamera are the wanted angle and the four world
	 * cardinals, so the ideal can jump a whole quadrant in one tick. That is
	 * the classic camera's behaviour and it is there at Tank today. An
	 * earlier attempt tried to absorb it into the offset and made it worse -
	 * the stick was swallowed (camera_wall above) - so this file stops
	 * producing it in Modern: the candidate loop keeps only the angle the
	 * player asked for, clipped by mgLOS, which shrinks the distance rather
	 * than jumping the angle. The jump below is therefore Tank's alone,
	 * and it is measured here so that the thing Modern does not do has a
	 * size. */
	{
		const long x1 = roundl2(-radius * sin(0.0));
		const long z1 = roundl2(-radius * cos(0.0));
		const long x2 = roundl2(-radius * sin(16384.0 * turn));
		const long z2 = roundl2(-radius * cos(16384.0 * turn));
		const long jump = InterpManhattan(x2 - x1, 0, z2 - z1);

		printf("  measured: the classic quadrant snap moves it %ld - vanilla's own cut,\n", jump);
		printf("            which the candidate loop stops producing in Modern\n");
		ok(jump > INTERP_CAM_SNAP, "which is why it is a cut and ours is not",
			jump, INTERP_CAM_SNAP);
	}
}

/* --------------------------------------- the eye stays on the ray ---

   A flat world of sectors, because that is all this rule is about. The harness is
   2D on purpose: the rule under test moves x and z, and the y is left to the
   floor and the ceiling, which game\camera.cpp's ModernCameraRayBounds keeps
   verbatim out of CameraCollisionBounds. Eight by eight sectors of 1024, '#'
   solid and '.' open, row 0 is gz 0 and column 0 is gx 0.

   Two maps. The corridor is one sector wide, so a dialled angle across it is
   clipped on most of the circle; the niche is a room with a two-by-two
   pillar beside her, which is the shape that gave the classic snap two axes
   to fire at once.

   And the three models below are mirrors, for the same reason lead_run() is one:
   game\camera.cpp cannot be linked without a level. h_mglos mirrors mgLOS
   (the >> 3, the eight steps, the step back at the end), h_old mirrors the
   four horizontal branches of CameraCollisionBounds and h_new mirrors the
   loop of ModernCameraRayBounds with its y half removed. The arithmetic
   under test is not mirrored: h_new calls the engine's own
   ModernCameraPullAxis, which is why that function sits in
   game\modern_camera.cpp rather than in the camera. Change one, change the
   other.

   And the limit of that: bmt.bat compiles this file beside
   game\modern_move.cpp and game\modern_camera.cpp only, so game\camera.cpp
   is not linked and cannot be under test. A later edit to
   ModernCameraRayBounds's probe order would leave these checks passing on the
   mirror. What they do hold is the rule - that a pulled eye keeps the orbit's
   own yaw where the sector snap threw it away - and the one piece of arithmetic
   the engine really runs. */

static const char* const g_corridor[8] = {
	"########",
	"###.####",
	"###.####",
	"###.####",
	"###.####",
	"###.####",
	"###.####",
	"########"
};

static const char* const g_niche[8] = {
	"########",
	"#......#",
	"#......#",
	"#..##..#",
	"#..##..#",
	"#......#",
	"#......#",
	"########"
};

static bool h_solid(long x, long z, const char* const* map)
{
	const long gx = x >> 10;
	const long gz = z >> 10;

	if (x < 0 || z < 0 || gx > 7 || gz > 7)
		return true;

	return map[gz][gx] == '#';
}

/* Returns how many of the eight steps were still in a room, so the caller can
   tell a clipped ray from a clear one. */
static long h_mglos(long tx, long tz, long ex, long ez, const char* const* map,
	long* ox, long* oz)
{
	const long dx = (ex - tx) >> 3;
	const long dz = (ez - tz) >> 3;
	long x = tx;
	long z = tz;
	long lp;

	for (lp = 0; lp < 8; lp++)
	{
		if (h_solid(x, z, map))
			break;

		x += dx;
		z += dz;
	}

	/* mgLOS steps back whenever lp is not 0 - and lp is 8 on a CLEAR line as
	   well, so even an unclipped eye comes home at seven eighths of what was
	   asked for. Vanilla's own arithmetic, modelled rather than corrected. */
	if (lp)
	{
		x -= dx;
		z -= dz;
	}

	*ox = x;
	*oz = z;
	return lp;
}

static void h_old(long wx, long wz, const char* const* map, long* ox, long* oz)
{
	const long push = (long)MODERN_CAMERA_PULL_PUSH;

	if (h_solid(wx - push, wz, map))
		wx = (wx & ~1023) + push;

	if (h_solid(wx, wz - push, map))
		wz = (wz & ~1023) + push;

	if (h_solid(wx + push, wz, map))
		wx = (wx | 1023) - push;

	if (h_solid(wx, wz + push, map))
		wz = (wz | 1023) - push;

	*ox = wx;
	*oz = wz;
}

static void h_new(long tx, long tz, long bx, long bz, long wantx, long wantz,
	const char* const* map, long* ox, long* oz)
{
	const long push = (long)MODERN_CAMERA_PULL_PUSH;
	const long steps = (long)MODERN_CAMERA_PULL_STEPS;
	long step, wx, wz;

	/* mgLOS's degenerate answer, and the floor the engine puts under it: a ray
	   whose FIRST eighth is already in the wall comes back as the target
	   itself, and an eye on her own axis has no angle at all. The dialled
	   candidate gives the direction and MODERN_CAMERA_HIDE_DIST the distance. */
	if (bx == tx && bz == tz)
	{
		const long dx = wantx - tx;
		const long dz = wantz - tz;
		const long d = (long)floor(sqrt((double)dx * dx + (double)dz * dz));

		bx = (d > (long)MODERN_CAMERA_HIDE_DIST)
			? tx + dx * (long)MODERN_CAMERA_HIDE_DIST / d : wantx;
		bz = (d > (long)MODERN_CAMERA_HIDE_DIST)
			? tz + dz * (long)MODERN_CAMERA_HIDE_DIST / d : wantz;
	}

	for (step = steps; step > 0; step--)
	{
		wx = ModernCameraPullAxis(tx, bx, step, steps);
		wz = ModernCameraPullAxis(tz, bz, step, steps);

		if (wx == tx && wz == tz)
			continue;

		if (h_solid(wx - push, wz, map) || h_solid(wx + push, wz, map) ||
			h_solid(wx, wz - push, map) || h_solid(wx, wz + push, map))
			continue;

		if (h_solid(wx, wz, map))
			continue;

		*ox = wx;
		*oz = wz;
		return;
	}

	*ox = bx;	// nothing on the ray cleared: the ray wins, as the engine's does
	*oz = bz;
}

/* The dialled angle read back out of a resolved eye, in the engine's own
   convention: the candidates are built as target - d * sin(a) in x and
   target - d * cos(a) in z (camera.cpp), so the angle is the atan2 of the
   negated pair - exactly the reading lead_run() already uses. */
static double h_yaw(long tx, long tz, long ex, long ez)
{
	return atan2((double)-(ex - tx), (double)-(ez - tz)) * 32768.0 / 3.14159265358979323846;
}

static double h_wrap(double units)
{
	while (units >= 32768.0)
		units -= 65536.0;

	while (units < -32768.0)
		units += 65536.0;

	return units;
}

struct PULL_RUN
{
	double worst_new;	// biggest |dialled - resolved| in units, under the Modern rule
	double worst_old;	// and the same for the classic sector snap
	long over_60_new;	// how many of the swept angles are 60 degrees out
	long over_60_old;
	long clipped;		// how many rays mgLOS had to cut short
	long pulled;		// how many of those the pull-in moved further in
	long outward;		// how many ended FURTHER out than was dialled - must be 0
	long degenerate;	// resolved eyes sitting on her own axis - must be 0
	long onaxis;		// rays mgLOS handed back as the target itself
	long stood;		// clipped rays where nothing on the ray cleared
};

static void pull_run(const char* const* map, long tx, long tz, long radius, PULL_RUN* r)
{
	const double turn = 32768.0 / 3.14159265358979323846;
	const double sixty = 60.0 * 65536.0 / 360.0;
	long a;

	r->worst_new = 0.0;
	r->worst_old = 0.0;
	r->over_60_new = 0;
	r->over_60_old = 0;
	r->clipped = 0;
	r->pulled = 0;
	r->outward = 0;
	r->degenerate = 0;
	r->onaxis = 0;
	r->stood = 0;

	for (a = -32768; a < 32768; a += 256)
	{
		const double rad = (double)a / turn;
		const long ex = tx - (long)floor((double)radius * sin(rad) + 0.5);
		const long ez = tz - (long)floor((double)radius * cos(rad) + 0.5);
		long bx, bz, nx, nz, ox, oz, cut;
		double en, eo;

		cut = h_mglos(tx, tz, ex, ez, map, &bx, &bz);

		if (bx == tx && bz == tz)
			r->onaxis++;

		if (cut < 8)
			r->clipped++;

		h_new(tx, tz, bx, bz, ex, ez, map, &nx, &nz);
		h_old(bx, bz, map, &ox, &oz);

		if (nx != bx || nz != bz)
			r->pulled++;
		else if (cut < 8)
			r->stood++;

		if (nx == tx && nz == tz)
		{
			r->degenerate++;
			continue;	// no angle to measure, and the check below is the one that fires
		}

		if ((nx - tx) * (nx - tx) + (nz - tz) * (nz - tz) >
			(ex - tx) * (ex - tx) + (ez - tz) * (ez - tz))
			r->outward++;

		en = fabs(h_wrap(h_yaw(tx, tz, nx, nz) - (double)a));
		eo = fabs(h_wrap(h_yaw(tx, tz, ox, oz) - (double)a));

		if (en > r->worst_new)
			r->worst_new = en;

		if (eo > r->worst_old)
			r->worst_old = eo;

		if (en >= sixty)
			r->over_60_new++;

		if (eo >= sixty)
			r->over_60_old++;
	}
}

static void camera_pull()
{
	PULL_RUN c, n;
	long i;

	printf("\nthe ray - the eye comes in along it and never round her\n");

	/* 1. The arithmetic itself: the ends are exact and the fraction is taken of
	   the whole distance, so the same step on two axes keeps the ratio. */
	ok(ModernCameraPullAxis(1000, 3000, 8, 8) == 3000,
		"pull: the full step is the eye itself",
		ModernCameraPullAxis(1000, 3000, 8, 8), 3000);
	ok(ModernCameraPullAxis(1000, 3000, 0, 8) == 1000,
		"pull: step 0 is the target itself",
		ModernCameraPullAxis(1000, 3000, 0, 8), 1000);
	ok(ModernCameraPullAxis(1000, 3000, 4, 8) == 2000,
		"pull: half way is half way",
		ModernCameraPullAxis(1000, 3000, 4, 8), 2000);
	ok(ModernCameraPullAxis(1000, 3000, 9, 8) == 3000,
		"pull: a step past the end cannot push the eye out",
		ModernCameraPullAxis(1000, 3000, 9, 8), 3000);
	ok(ModernCameraPullAxis(1000, 3000, 4, 0) == 3000,
		"pull: no steps at all leaves the eye alone",
		ModernCameraPullAxis(1000, 3000, 4, 0), 3000);
	ok(ModernCameraPullAxis(3000, 1000, 4, 8) == 2000,
		"pull: and backwards, for an eye west of her",
		ModernCameraPullAxis(3000, 1000, 4, 8), 2000);

	/* 2. The truncation costs less than a unit on an axis, which is the whole
	   of why the yaw survives: the division is the last thing done, on a
	   product of the whole difference, so nothing accumulates. */
	{
		long worst = 0;

		for (i = -4096; i <= 4096; i += 7)
		{
			long step;

			for (step = 1; step <= (long)MODERN_CAMERA_PULL_STEPS; step++)
			{
				const long got = ModernCameraPullAxis(0, i, step, MODERN_CAMERA_PULL_STEPS);
				const double want = (double)i * (double)step / (double)MODERN_CAMERA_PULL_STEPS;
				const long err = absl(got - roundl2(want));

				if (err > worst)
					worst = err;
			}
		}

		printf("  measured: the truncation is at most %ld unit(s) on an axis\n", worst);
		ok(worst <= 1, "pull: the fraction truncates by under a unit", worst, 1);
	}

	/* 3. The corridor and the niche. The dialled angle is what her movement is
	   measured against (the orbit's world yaw) and the resolved eye is what the
	   player sees; the rule says they are the same angle. */
	/* She is off the centre line in both, because that is where the player runs
	   and it is the only place the defect is worth its measured size: 228 units
	   from the corridor wall and 210 from the pillar, both well outside her own
	   collision radius of 102. Down the middle of the corridor the sector snap
	   costs 37 degrees; hugging the wall it costs 180, the shape it was measured
	   at. */
	pull_run(g_corridor, 3300, 3584, 1536, &c);
	pull_run(g_niche, 2950, 3400, 1536, &n);

	printf("  corridor (one sector wide, she hugs the wall, 256 angles): %ld clipped, %ld pulled in,\n",
		c.clipped, c.pulled);
	printf("           %ld stood, %ld came back on her axis and were floored\n",
		c.stood, c.onaxis);
	printf("           worst view error   ours %6.2f deg    the sector snap %6.2f deg\n",
		c.worst_new * 360.0 / 65536.0, c.worst_old * 360.0 / 65536.0);
	printf("           60 deg or worse    ours %ld              the sector snap %ld\n",
		c.over_60_new, c.over_60_old);
	printf("  niche (a 2x2 pillar she hugs): %ld clipped, %ld pulled in,\n",
		n.clipped, n.pulled);
	printf("           %ld stood, %ld came back on her axis and were floored\n",
		n.stood, n.onaxis);
	printf("           worst view error   ours %6.2f deg    the sector snap %6.2f deg\n",
		n.worst_new * 360.0 / 65536.0, n.worst_old * 360.0 / 65536.0);
	printf("           60 deg or worse    ours %ld              the sector snap %ld\n",
		n.over_60_new, n.over_60_old);

	/* Two degrees is the mgLOS model's own >> 3 and not ours: the eighth it
	   steps in truncates the direction by a few units of POSITION before
	   anything here is asked a question. */
	ok(c.worst_new <= 2 * 65536 / 360,
		"ray: the corridor's picture shows the angle she runs at",
		roundl2(c.worst_new * 360.0 / 65536.0), 2);
	ok(n.worst_new <= 2 * 65536 / 360,
		"ray: and the niche's",
		roundl2(n.worst_new * 360.0 / 65536.0), 2);
	ok(c.over_60_new == 0 && n.over_60_new == 0,
		"ray: no angle at all is 60 degrees out any more",
		c.over_60_new + n.over_60_new, 0);
	ok(c.outward == 0 && n.outward == 0,
		"ray: a wall may only bring the eye CLOSER, never past what was dialled",
		c.outward + n.outward, 0);
	ok(c.degenerate == 0 && n.degenerate == 0,
		"ray: and no eye came back sitting on her own axis",
		c.degenerate + n.degenerate, 0);
	ok(c.onaxis + n.onaxis > 0,
		"ray: mgLOS really does hand back the target, so the floor is not theory",
		c.onaxis + n.onaxis, 1);

	/* And the defect this replaces, pinned rather than described. Measured
	   in play - 481 of 930 pushed ticks 60 degrees out, all of them clipped,
	   median 124 between 200 and 400 units; these two maps reproduce the shape
	   of it out of nothing but the geometry, and what the shape is is a picture
	   that does not show the angle she is running at. */
	ok(c.over_60_old > 0 && n.over_60_old > 0,
		"ray: the sector snap DID throw the picture 60 degrees out, on both maps",
		c.over_60_old + n.over_60_old, 1);
	ok(c.worst_old > c.worst_new && n.worst_old > n.worst_new,
		"ray: and it was worse than ours everywhere it fired",
		roundl2((c.worst_old - c.worst_new) * 360.0 / 65536.0), 1);
}

/* -------------------------------- what the engine hands the carry -------- */

/* The engine half is a subtraction, and this is it.
   game\modern_state.cpp publishes her heading as the modern input left it, and
   game\camera.cpp's ModernCameraCarryFromAnim subtracts that from her heading
   as the animation has left it a few hundred lines later in the SAME tick. What
   is left over is the animation's own turn - so a stick turn of any size, on
   its own, can never seat a swing, and an animation flip seats the whole of
   itself whatever the stick did in the same tick. */
static void camera_flip()
{
	MODERN_CAMERA_CFG cfg;
	long heading, turn;

	printf("\nthe flip - what game\\camera.cpp hands ModernCameraCarry\n");

	ModernCameraDefaults(&cfg);

	for (heading = -32768; heading < 32768; heading += 1009)
	{
		for (turn = -(long)MODERN_TURN_RATE; turn <= (long)MODERN_TURN_RATE;
			turn += (long)MODERN_TURN_RATE)
		{
			/* What game\modern_state.cpp publishes: her heading after the
			   modern turn of this tick, read back off the item. */
			const short published = ModernWrap(heading + turn);
			MODERN_CAMERA_STATE st;
			short after;

			/* a. The stick alone. The animation left her heading where the
			   input put it, so the subtraction is nothing at all - whatever
			   rate the turn uses. */
			after = published;
			ModernCameraReset(&st);
			ModernCameraCarry(&cfg, &st, ModernWrap((long)after - published));
			ok(st.carry == 0 && st.carry_total == 0,
				"flip: a turn by the stick seats no swing at all",
				st.carry_total, 0);

			/* b. And the same tick with an animation about-turn on top of it -
			   the roll, the jump twist, the underwater roll. The subtraction
			   sees the half turn and nothing of the stick. */
			after = ModernWrap((long)published + 32768);
			ok(absl(ModernWrap((long)after - published)) == 32768,
				"flip: the animation's half turn survives the subtraction",
				absl(ModernWrap((long)after - published)), 32768);

			ModernCameraReset(&st);
			ModernCameraCarry(&cfg, &st, ModernWrap((long)after - published));
			ok(absl(st.carry_total) == 32768,
				"flip: and the whole half turn is what the camera owes",
				absl(st.carry_total), 32768);
		}
	}

	/* c. The swing in ticks, through the engine's own two functions: seated by
	   the subtraction above, paid out by the eased profile, arriving whole. */
	{
		MODERN_CAMERA_STATE st;
		long t, paid, last, biggest, against;

		ModernCameraReset(&st);
		ModernCameraCarry(&cfg, &st, ModernWrap(32768));

		last = 0;
		biggest = 0;
		against = 0;

		for (t = 1; t <= cfg.carry_ticks; t++)
		{
			paid = ModernCameraCarryStep(&cfg, st.carry_total, t);

			if (absl(paid - last) > biggest)
				biggest = absl(paid - last);

			if ((paid - last) * ((st.carry_total >= 0) ? 1 : -1) < 0)
				against++;

			last = paid;
		}

		printf("  measured: %ld units over %ld ticks, biggest tick %ld, %ld against the grain\n",
			absl(last), cfg.carry_ticks, biggest, against);
		ok(absl(last) == 32768, "flip: the swing arrives whole", absl(last), 32768);
		ok(against == 0, "flip: and never runs backwards on the way", against, 0);
		ok(ModernCameraCarryStep(&cfg, st.carry_total, cfg.carry_ticks - 1) != st.carry_total,
			"flip: and it really takes all of them",
			ModernCameraCarryStep(&cfg, st.carry_total, cfg.carry_ticks - 1),
			st.carry_total);
	}
}

/* ---------------------------------------------------------- the ledge hang --- */

/* A turn-phase situation that goes on: the option on, the key held, hands
 * free, unhurt, nothing refused. Each check below changes one field of it. */
static void ledge_turn_sit(MODERN_LEDGE_SITUATION* s, short start, long tick)
{
	s->option = 1;
	s->phase = MODERN_LEDGE_TURN;
	s->tick = tick;
	s->frame = 0;
	s->frame_base = 0;
	s->frame_end = 0;
	s->step = MODERN_LEDGE_FRAME_STEP;
	s->y_rot = start;
	s->target = ModernWrap((long)start + MODERN_LEDGE_HALF_TURN);
	s->side = MODERN_LEDGE_TURN_SIDE;
	s->cardinal_off = 0;
	s->refusals = 0;
	s->key = 1;
	s->jump = 0;
	s->hands_free = 1;
	s->hit_points = 1000;
	s->entry_hit_points = 1000;
	s->control_taken = 0;
}

/* The same for the lowering, on the pull-up of one level. */
static void ledge_lower_sit(MODERN_LEDGE_SITUATION* s, long base, long end, long frame, long tick)
{
	ledge_turn_sit(s, 0, 0);
	s->phase = MODERN_LEDGE_LOWER;
	s->tick = tick;
	s->frame = frame;
	s->frame_base = base;
	s->frame_end = end;
}

static void ledge_hang()
{
	const short cardinals[4] = { 0, 0x4000, -0x8000, -0x4000 };
	MODERN_LEDGE_SITUATION s;
	MODERN_LEDGE_RESULT r;
	long i, t, sum;

	printf("\nthe ledge hang - ModernLedgeDecide\n");

	/* The numbers by name. The standing line and the hang position lie 200
	 * units apart along the heading, which is animation 102's translate; the
	 * catch's window is the engine's 0x18E2. */
	ok(MODERN_LEDGE_STAND_IN + MODERN_LEDGE_HANG_OUT + 1 == 200,
		"ledge: standing line and hang position are the pull-up's 200 apart",
		MODERN_LEDGE_STAND_IN + MODERN_LEDGE_HANG_OUT + 1, 200);
	ok(MODERN_LEDGE_SNAP == 0x18E2, "ledge: the catch's own angle window", MODERN_LEDGE_SNAP, 0x18E2);
	ok(MODERN_LEDGE_HALF_TURN == 0x8000, "ledge: the turn is half a turn", MODERN_LEDGE_HALF_TURN, 0x8000);

	/* The turn is six ticks, and six are the direct turn's own rate: every
	 * step is the half turn's sixth in whole units, 5461 or 5462, within a
	 * unit of MODERN_TURN_RATE - the two cannot drift apart unnoticed. */
	ok(MODERN_LEDGE_TURN_TICKS == 6, "ledge: the half turn takes six ticks", MODERN_LEDGE_TURN_TICKS, 6);
	ok(MODERN_LEDGE_TURN_SIDE == 1, "ledge: she turns to the right", MODERN_LEDGE_TURN_SIDE, 1);
	sum = 0;

	for (t = 0; t < MODERN_LEDGE_TURN_TICKS; t++)
	{
		const long step = ModernLedgeTurnStep(t);

		sum += step;
		ok(step == MODERN_LEDGE_HALF_TURN / MODERN_LEDGE_TURN_TICKS ||
			step == MODERN_LEDGE_HALF_TURN / MODERN_LEDGE_TURN_TICKS + 1,
			"ledge: every tick of the turn is the half turn's share in whole units", step,
			MODERN_LEDGE_HALF_TURN / MODERN_LEDGE_TURN_TICKS);
		ok(absl(step - MODERN_TURN_RATE) <= 1, "ledge: ...within a unit of the direct turn's rate",
			step, MODERN_TURN_RATE);
	}

	ok(sum == MODERN_LEDGE_HALF_TURN, "ledge: the steps add up to the half turn", sum, MODERN_LEDGE_HALF_TURN);
	ok(ModernLedgeTurnStep(-1) == 0 && ModernLedgeTurnStep(MODERN_LEDGE_TURN_TICKS) == 0,
		"ledge: no step outside the turn", ModernLedgeTurnStep(MODERN_LEDGE_TURN_TICKS), 0);

	/* The turn ends on the opposite cardinal, to the unit, from all four
	 * cardinals and from every entry offset inside the window, turning either
	 * way: the first step takes the offset out, and every step has the
	 * side's sign. */
	for (i = 0; i < 8; i++)
	{
		const long side = (i < 4) ? 1 : -1;
		long off;

		for (off = -MODERN_LEDGE_SNAP; off <= MODERN_LEDGE_SNAP; off += 97)
		{
			const long o = (off > MODERN_LEDGE_SNAP - 97) ? MODERN_LEDGE_SNAP : off;
			short y = ModernWrap((long)cardinals[i & 3] + o);
			long ended = 0, ticks = 0, against = 0;

			for (t = 0; t < MODERN_LEDGE_TURN_TICKS + 2 && !ended; t++)
			{
				ledge_turn_sit(&s, cardinals[i & 3], t);
				s.y_rot = y;
				s.side = side;
				s.cardinal_off = (t == 0) ? (short)o : 0;
				ModernLedgeDecide(&s, &r);

				if (r.verdict != MODERN_LEDGE_CONTINUE)
					break;

				if (t > 0 && r.yaw_step * side <= 0)
					against++;

				y = ModernWrap((long)y + r.yaw_step);
				ticks++;

				if (r.phase == MODERN_LEDGE_LOWER)
					ended = 1;
			}

			ok(ended == 1, "ledge: the turn hands on to the lowering", ended, 1);
			ok(ticks == MODERN_LEDGE_TURN_TICKS, "ledge: after exactly TURN_TICKS ticks",
				ticks, MODERN_LEDGE_TURN_TICKS);
			ok(y == ModernWrap((long)cardinals[i & 3] + MODERN_LEDGE_HALF_TURN),
				"ledge: and she faces the opposite cardinal to the unit",
				y, ModernWrap((long)cardinals[i & 3] + MODERN_LEDGE_HALF_TURN));
			ok(against == 0, "ledge: ...every step after the first the side's way", against, 0);
		}
	}

	/* The window: 0x18E2 is taken, 0x18E3 is a bad entry, either side. */
	ledge_turn_sit(&s, 0x4000, 0);
	s.cardinal_off = MODERN_LEDGE_SNAP;
	s.y_rot = 0x4000 + MODERN_LEDGE_SNAP;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_CONTINUE, "ledge: 0x18E2 off the cardinal is taken",
		r.verdict, MODERN_LEDGE_CONTINUE);
	s.cardinal_off = -MODERN_LEDGE_SNAP;
	s.y_rot = 0x4000 - MODERN_LEDGE_SNAP;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_CONTINUE, "ledge: -0x18E2 likewise",
		r.verdict, MODERN_LEDGE_CONTINUE);
	s.cardinal_off = MODERN_LEDGE_SNAP + 1;
	s.y_rot = 0x4000 + MODERN_LEDGE_SNAP + 1;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: 0x18E3 is refused with a stand",
		r.verdict, MODERN_LEDGE_STAND);
	ok(r.yaw_step == 0 && r.phase == MODERN_LEDGE_NONE, "ledge: ...no turn, phase cleared", r.yaw_step, 0);
	s.cardinal_off = -(MODERN_LEDGE_SNAP + 1);
	s.y_rot = 0x4000 - MODERN_LEDGE_SNAP - 1;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: -0x18E3 likewise",
		r.verdict, MODERN_LEDGE_STAND);

	/* Other bad entries: a target that is not a cardinal, or not the one
	 * opposite, a tick past the turn, a phase nobody knows. */
	ledge_turn_sit(&s, 0, 0);
	s.target = 0x4000;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: a target a quarter round is a bad entry",
		r.verdict, MODERN_LEDGE_STAND);
	s.target = -0x7FFF;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: a target off the cardinals likewise",
		r.verdict, MODERN_LEDGE_STAND);
	ledge_turn_sit(&s, 0, MODERN_LEDGE_TURN_TICKS);
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: a tick past the turn likewise",
		r.verdict, MODERN_LEDGE_STAND);
	ledge_turn_sit(&s, 0, 0);
	s.phase = 3;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: a phase it does not know ends in a stand",
		r.verdict, MODERN_LEDGE_STAND);
	s.phase = 255;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: ...the byte's top value too",
		r.verdict, MODERN_LEDGE_STAND);

	/* Every single refusal bit stops the turn, on its first tick and on every
	 * later one. */
	for (i = 0; i < 32; i++)
	{
		for (t = 0; t < MODERN_LEDGE_TURN_TICKS; t++)
		{
			ledge_turn_sit(&s, -0x4000, t);
			s.refusals = (long)(1ul << i);
			ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_STAND, "ledge: each refusal bit alone ends the turn",
				r.verdict, MODERN_LEDGE_STAND);
		}
	}

	/* The aborts: each one alone ends the turn in a stand on every tick, and
	 * none of them stands her up in the lowering - she goes on, or is handed
	 * to the hang. */
	for (i = 0; i < 7; i++)
	{
		for (t = 0; t < MODERN_LEDGE_TURN_TICKS + 30; t++)
		{
			const long lower = (t >= MODERN_LEDGE_TURN_TICKS);

			if (lower)
				ledge_lower_sit(&s, 1388, 1461, 1461 - (t - MODERN_LEDGE_TURN_TICKS), t - MODERN_LEDGE_TURN_TICKS);
			else
				ledge_turn_sit(&s, 0, t);

			switch (i)
			{
			case 0: s.key = 0; break;
			case 1: s.jump = 1; break;
			case 2: s.hands_free = 0; break;
			case 3: s.hit_points = 999; break;
			case 4: s.hit_points = 0; break;
			case 5: s.control_taken = 1; break;
			case 6: s.refusals = 1; break;
			}

			ModernLedgeDecide(&s, &r);

			if (!lower)
				ok(r.verdict == MODERN_LEDGE_STAND && r.phase == MODERN_LEDGE_NONE && r.yaw_step == 0,
					"ledge: key up, jump, weapons, damage, death, cutscene, refusal: a stand in the turn",
					r.verdict, MODERN_LEDGE_STAND);
			else if (i == 4 || i == 5)
				ok(r.verdict == MODERN_LEDGE_HANG && r.phase == MODERN_LEDGE_NONE,
					"ledge: death or a cutscene in the lowering hand over at once",
					r.verdict, MODERN_LEDGE_HANG);
			else
				ok(r.verdict != MODERN_LEDGE_STAND,
					"ledge: nothing stands her up in the lowering", r.verdict, MODERN_LEDGE_CONTINUE);
		}
	}

	/* The lowering on the real pull-up: animation 97 is frames 1388..1461 in
	 * rich1 and 1516..1589 in the Andrea and andy levels - 74 frames in every
	 * campaign level. At steps 1 to 4 it takes ceil(73 / step) steps after the
	 * unstepped first tick, lands on the first frame exactly, never below it,
	 * and hands over on the tick after. */
	{
		const long bases[2] = { 1388, 1516 };
		const long want[5] = { 0, 73, 37, 25, 19 };
		long b, step;

		for (b = 0; b < 2; b++)
		{
			const long base = bases[b], end = bases[b] + 73;

			for (step = 1; step <= 4; step++)
			{
				long frame = end, steps = 0, below = 0, verdict = 0;

				for (t = 0; t < 200; t++)
				{
					ledge_lower_sit(&s, base, end, frame, t);
					s.step = step;
					verdict = ModernLedgeDecide(&s, &r);

					if (verdict != MODERN_LEDGE_CONTINUE)
						break;

					if (t == 0 && r.frame_step != 0)
						below++;

					if (r.frame_step)
						steps++;

					frame -= r.frame_step;

					if (frame < base)
						below++;
				}

				ok(steps == want[step], "ledge: the lowering's step count at steps 1..4", steps, want[step]);
				ok(frame == base, "ledge: ...lands on frame_base exactly", frame, base);
				ok(below == 0, "ledge: ...never below it, and the first tick is not stepped", below, 0);
				ok(verdict == MODERN_LEDGE_HANG, "ledge: ...then hands over to the hang", verdict, MODERN_LEDGE_HANG);
				ok(t == want[step] + 1, "ledge: ...on the tick after it landed", t, want[step] + 1);
			}
		}

		ok(want[MODERN_LEDGE_FRAME_STEP] == 25, "ledge: at the shipped step the lowering is 25 steps",
			want[MODERN_LEDGE_FRAME_STEP], 25);
	}

	/* A lowering frame outside the pull-up hands over at once. */
	ledge_lower_sit(&s, 1388, 1461, 1462, 5);
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_HANG, "ledge: a frame past the end hands over",
		r.verdict, MODERN_LEDGE_HANG);
	ledge_lower_sit(&s, 1388, 1461, 1300, 5);
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_HANG, "ledge: a frame before the start likewise",
		r.verdict, MODERN_LEDGE_HANG);

	/* The option off: nothing is reported, whatever the phase and the input -
	 * no verdict, no phase, no step. */
	{
		long phase, combo, quiet = 0, total = 0;

		for (phase = -1; phase <= 4; phase++)
		{
			for (combo = 0; combo < 256; combo++)
			{
				for (t = -1; t <= MODERN_LEDGE_TURN_TICKS + 1; t++)
				{
					ledge_lower_sit(&s, 1388, 1461, 1388 + (combo % 80), t);
					s.option = 0;
					s.phase = (phase == 4) ? 255 : phase;
					s.y_rot = (short)(combo * 257);
					s.target = (short)(combo * 4096);
					s.cardinal_off = (short)((combo % 16) * 1000 - 8000);
					s.key = combo & 1;
					s.jump = (combo >> 1) & 1;
					s.hands_free = (combo >> 2) & 1;
					s.hit_points = (combo & 8) ? 0 : 1000;
					s.entry_hit_points = (combo & 16) ? 1001 : 1000;
					s.control_taken = (combo >> 5) & 1;
					s.refusals = (combo & 64) ? 0x7FF : 0;
					s.step = (combo & 128) ? 4 : 1;
					r.phase = 9;
					r.yaw_step = 9;
					r.frame_step = 9;
					r.verdict = 9;
					total++;

					if (ModernLedgeDecide(&s, &r) == MODERN_LEDGE_QUIET && r.verdict == MODERN_LEDGE_QUIET &&
						r.phase == MODERN_LEDGE_NONE && r.yaw_step == 0 && r.frame_step == 0)
						quiet++;
				}
			}
		}

		printf("  measured: %ld of %ld option-off situations report nothing\n", quiet, total);
		ok(quiet == total, "ledge: with the option off it reports nothing at all", quiet, total);
	}

	/* And with the option on, the phase none is not the move either. */
	ledge_turn_sit(&s, 0, 0);
	s.phase = MODERN_LEDGE_NONE;
	ok(ModernLedgeDecide(&s, &r) == MODERN_LEDGE_QUIET, "ledge: phase none reports nothing",
		r.verdict, MODERN_LEDGE_QUIET);

	/* The state is classic for the translation - the stick must not steer the
	 * turn - and in no other family. */
	ok(ModernStateIsClassic(139) == 1, "ledge: AS_LEDGEHANG stays classic", ModernStateIsClassic(139), 1);
	ok(ModernStateIsGrab(139) == 0, "ledge: ...not in the hold-on family", ModernStateIsGrab(139), 0);
	ok(ModernStateIsDuck(139) == 0, "ledge: ...not a crouch", ModernStateIsDuck(139), 0);
	ok(ModernStateIsSwim(139) == 0, "ledge: ...not a swim", ModernStateIsSwim(139), 0);
	ok(ModernStateAllowsDirectTurn(139) == 0, "ledge: ...no direct turn", ModernStateAllowsDirectTurn(139), 0);
	ok(ModernStateAllowsAirTurn(139) == 0, "ledge: ...no air turn", ModernStateAllowsAirTurn(139), 0);
}

/* The ledge hang's trigger: the quad she hangs with, the distance to the
 * edge per cardinal, the step onto the standing line, and the fire rule of
 * the direct stop - the arithmetic lara.cpp's LaraLedgeTrigger applies. The
 * engine's own formulas (LaraLedgeSpots' standing line, LaraLedgeDist) are
 * written out here as the reference. */
static long ledge_line_ref(long quad, long c)
{
	switch (quad)
	{
	case 0:
	case 1:
		return (c & ~1023L) + MODERN_LEDGE_STAND_IN;

	default:
		return (c | 1023L) - MODERN_LEDGE_STAND_IN;
	}
}

static long ledge_dist_ref(long quad, long c)
{
	return (quad < 2) ? c - (c & ~1023L) : (c | 1023L) - c;
}

static void ledge_trigger()
{
	const short cardinals[4] = { 0, 0x4000, -0x8000, -0x4000 };
	const long want_quad[4] = { 2, 3, 0, 1 };
	const long bases[3] = { 0, 5 * 1024, 37 * 1024 };
	const long speeds[5] = { 0, 15, 23, 38, 47 };
	long q, b, o, i, d, sp, shift, fired, bad, total;

	printf("\nthe ledge hang - the trigger\n");

	ok(MODERN_LEDGE_NEAR == 512, "ledge trigger: LEDGELOG's reach is half a sector", MODERN_LEDGE_NEAR, 512);

	/* The quad she hangs with is two round from the approach, and it is the
	 * quad of the approach turned half round. */
	for (i = 0; i < 4; i++)
	{
		const short hang = (short)(cardinals[i] + 0x8000);

		ok(ModernLedgeHangQuad(cardinals[i]) == want_quad[i], "ledge trigger: hang quad of each approach",
			ModernLedgeHangQuad(cardinals[i]), want_quad[i]);
		ok(ModernLedgeHangQuad(cardinals[i]) == (long)((unsigned short)(hang + 0x2000) / 0x4000),
			"ledge trigger: ...the quad of the heading turned half round", ModernLedgeHangQuad(cardinals[i]),
			(long)((unsigned short)(hang + 0x2000) / 0x4000));
	}

	/* The distance, every unit of a sector in three sectors, per quad, against
	 * the engine's formula; the other coordinate does not count. */
	bad = 0;
	total = 0;

	for (q = 0; q < 4; q++)
		for (b = 0; b < 3; b++)
			for (o = 0; o < 1024; o++)
			{
				const long c = bases[b] + o;
				const long x = (q & 1) ? c : 777 + o;
				const long z = (q & 1) ? 777 + o : c;
				const long want = ledge_dist_ref(q, c);

				total++;

				if (ModernLedgeEdgeDist(q, x, z) != want || want != ((q < 2) ? o : 1023 - o))
					bad++;
			}

	ok(bad == 0, "ledge trigger: edge distance per quad = the engine's, 0 on the edge unit", bad, 0);
	ok(total == 4 * 3 * 1024, "ledge trigger: ...over every unit of three sectors", total, 4 * 3 * 1024);
	ok(ModernLedgeEdgeDist(4, 100, 100) == -1, "ledge trigger: a quad outside the four has no distance",
		ModernLedgeEdgeDist(4, 100, 100), -1);

	/* The fire rule and the step, per quad, for every unit of a sector and
	 * each speed: it fires exactly from 15 past the line to one step short of
	 * it; a point short of the line lands on the engine's standing line, one
	 * past it is not moved; the move is toward the drop only and at most one
	 * step; the other coordinate is kept. */
	bad = 0;
	total = 0;

	for (q = 0; q < 4; q++)
		for (i = 0; i < 5; i++)
			for (o = 0; o < 1024; o++)
			{
				const long c = bases[1] + o;
				long x = (q & 1) ? c : 3000;
				long z = (q & 1) ? 3000 : c;
				const long step = (speeds[i] > MODERN_LEDGE_RESIDUAL) ? speeds[i] : MODERN_LEDGE_RESIDUAL;
				long moved, other, after;

				sp = speeds[i];
				d = ModernLedgeEdgeDist(q, x, z);
				fired = ModernLedgeFire(d, sp, &shift);
				total++;

				if (fired != (d >= MODERN_LEDGE_STAND_IN - MODERN_LEDGE_RESIDUAL && d <= MODERN_LEDGE_STAND_IN + step))
				{
					bad++;
					continue;
				}

				if (!fired)
				{
					if (shift != 0)
						bad++;

					continue;
				}

				if (shift < 0 || shift > step)
					bad++;

				ModernLedgeStep(q, shift, &x, &z);
				moved = (q & 1) ? x : z;
				other = (q & 1) ? z : x;
				after = ModernLedgeEdgeDist(q, x, z);

				if (other != 3000 || d - after != shift)
					bad++;

				if (d >= MODERN_LEDGE_STAND_IN && (moved != ledge_line_ref(q, c) || after != MODERN_LEDGE_STAND_IN))
					bad++;

				if (d < MODERN_LEDGE_STAND_IN && moved != c)
					bad++;
			}

	ok(bad == 0, "ledge trigger: fire window, step onto the line, never backwards", bad, 0);
	ok(total == 4 * 5 * 1024, "ledge trigger: ...every unit of a sector, five speeds, four quads", total,
		4 * 5 * 1024);

	/* The run at 47 a tick, the table: arriving at 0..146 units from the edge
	 * after the tick's movement. */
	for (d = 0; d <= 146; d++)
	{
		fired = ModernLedgeFire(d, 47, &shift);

		if (d < 84)
			ok(!fired, "ledge trigger: run past the line by more than the residual does not start", d, -1);
		else
		{
			ok(fired, "ledge trigger: run inside the window starts", d, 1);
			ok(shift == ((d > MODERN_LEDGE_STAND_IN) ? d - MODERN_LEDGE_STAND_IN : 0),
				"ledge trigger: run shift is what is left to the line", shift,
				(d > MODERN_LEDGE_STAND_IN) ? d - MODERN_LEDGE_STAND_IN : 0);
		}
	}

	ok(!ModernLedgeFire(147, 47, &shift), "ledge trigger: a run one step and a unit short waits a tick", 147, 0);

	/* A steady approach never passes the window: from every start far enough
	 * out, stepping `speed` a tick, the first tick that fires lies between the
	 * line and one step short of it. 38 is a run at the edge of the catch's
	 * window (47 cos 34.6 degrees). */
	for (i = 1; i < 5; i++)
	{
		sp = speeds[i];
		bad = 0;

		for (o = MODERN_LEDGE_STAND_IN + sp + 1; o <= 1023; o++)
		{
			d = o;
			fired = 0;

			while (d >= 0 && !(fired = ModernLedgeFire(d, sp, &shift)))
				d -= sp;

			if (!fired || d < MODERN_LEDGE_STAND_IN || shift != d - MODERN_LEDGE_STAND_IN || shift > sp)
				bad++;
		}

		ok(bad == 0, "ledge trigger: a steady approach starts between the line and one step short", bad, 0);
	}

	/* The stand and the walk's own edge stop, 101 units out: 2 forward. */
	ok(ModernLedgeFire(101, 0, &shift) && shift == 2, "ledge trigger: from the walk's own stop, 2 forward",
		shift, 2);
	ok(ModernLedgeFire(MODERN_LEDGE_STAND_IN + MODERN_LEDGE_RESIDUAL, 0, &shift) && shift == MODERN_LEDGE_RESIDUAL,
		"ledge trigger: a stand a walk step short of the line", shift, MODERN_LEDGE_RESIDUAL);
	ok(!ModernLedgeFire(MODERN_LEDGE_STAND_IN + MODERN_LEDGE_RESIDUAL + 1, 0, &shift),
		"ledge trigger: ...and not a unit farther", shift, 0);
	ok(ModernLedgeFire(MODERN_LEDGE_STAND_IN - MODERN_LEDGE_RESIDUAL, 0, &shift) && shift == 0,
		"ledge trigger: a stand a walk step past the line starts where she is", shift, 0);
	ok(!ModernLedgeFire(MODERN_LEDGE_STAND_IN - MODERN_LEDGE_RESIDUAL - 1, 0, &shift),
		"ledge trigger: ...and not a unit past that", shift, 0);
	ok(!ModernLedgeFire(-1, 47, &shift) && !ModernLedgeFire(1024, 47, &shift),
		"ledge trigger: a distance outside the sector never starts", shift, 0);
}

/* ------------------------------------------------------ what is not ours --- */

static void deferred()
{
	printf("\nnot ours yet - TROne expectations still owed, by name\n");
	printf("(ptr: needs a pointer profile, which this engine does not have;\n");
	printf(" seam: the missing half is in specific\\input.cpp, which cannot be linked here):\n");
	printf("  ptr     modern.remaster_back_input_distinguishes_running_from_armed_hopback\n");
	printf("  none    modern.angles_match_the_classic_convention (trone_modern_camera_yaw)\n");
	printf("          - the degenerate camera falls back to Lara's yaw in game\\modern_state.cpp,\n");
	printf("            which needs the engine's own camera and cannot be linked here\n");
	printf("  ptr     combatmode.toggle_pulses_once_per_press_in_the_modern_profile (the edge)\n");
	printf("  seam    modern.held_equip_draws_once_and_interaction_does_not_fire\n");
	printf("          - the draw half is checked above (Hold); the interaction half is the\n");
	printf("            Action split's IN_INTERACT, a key of its own in specific\\input.cpp\n");
	printf("\none came OFF that list:\n");
	printf("  modern.orbit_stays_in_world_space_and_accepts_mouse_while_running - both halves\n");
	printf("  are ours now. The stick moves the camera while she runs, and what it moves is a\n");
	printf("  WORLD angle her own turning does not touch. Both are checked above, as `the\n");
	printf("  orbit' and as `the seam'. What still differs from TROne is the return: theirs\n");
	printf("  never comes back behind the player, ours falls home at MODERN_CAMERA_RETURN\n");
	printf("  while she moves and the thumb is off the stick.\n");
}

int main()
{
	printf("ModernTest - the arithmetic of the modern control\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "modern_move.cpp\n");
	printf("                  and TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "modern_camera.cpp\n");
	printf("expectations ported from TROne (GPLv3) by Todesengel1931\n\n");

	angles();
	bit_values();
	deadzone();
	stick_to_tank();
	held_course();
	spin_hysteresis();
	walk_threshold();
	aiming();
	jumping();
	classic_states();
	water_move();
	back_input();
	chatter();
	blocked();
	direct_turn();
	circling();
	reversal();
	spin_sweep();
	air_turn();
	turn_chain();
	look_tank();
	tank_sector();
	cam_steer();
	firing_run();
	weapons();
	mode_latch();
	grab_family();
	grab_latch();
	sweeps();
	camera_orbit();
	camera_speed();
	camera_pitch();
	camera_wall();
	camera_hide();
	camera_seam();
	camera_stamp();
	camera_lead();
	camera_camcut();
	camera_carry();
	camera_pull();
	camera_flip();
	ledge_hang();
	ledge_trigger();
	deferred();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

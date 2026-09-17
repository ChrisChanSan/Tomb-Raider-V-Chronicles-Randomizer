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

/* ModernTest - the arithmetic of the modern control, checked without a game
 * (#161, round 161a, step 2). Build with bmt.bat, run with no arguments.
 *
 * WHAT IT CHECKS, and why it exists at all. The modern control is a pure
 * function: stick direction plus a remembered world heading in, the 1996
 * direction bits out. Everything a play test can see - Lara turning the wrong
 * way, spinning in circles in front of a fixed camera, refusing to turn at a
 * wall - is one of about a dozen decisions in that function, and every one of
 * them is a threshold that a play test can only reach by accident. So the
 * function is COMPILED INTO THIS TOOL, not copied into it: bmt.bat hands the
 * engine's own game\modern_move.cpp to the compiler beside this file, exactly
 * as InterpTest is built against game\interpolate_math.h. There is no twin to
 * keep in step, and a change to the tree is a change this test sees.
 *
 * WHERE THE EXPECTATIONS COME FROM. The design is TROne's, re-derived in our
 * own voice (see game\modern_move.h). The EXPECTATIONS below are the one part
 * taken across rather than re-invented: the 28 `modern.*` and 4 `combatmode.*`
 * cases of TROne's tests\test_controller.cpp, ported to our units and named in
 * a comment above each block with the TROne test they come from. They are
 * worth having verbatim because they are somebody else's five games' worth of
 * play testing written down as numbers.
 *   TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931
 *
 * OUR UNITS ARE NOT THEIRS. TROne's stick is +-32767; ours arrives from
 * specific\dxshell.cpp as about +-128 (XInput /256) or +-125 (DirectInput).
 * Every stick number below therefore goes through S() - TROne's own value,
 * scaled - and the original stands beside it, so the port can be read against
 * their file line by line. Angles, thresholds and state numbers are the same
 * units in both projects and are taken across unchanged.
 *
 * WHAT IS NOT HERE, AND WHO OWES IT. Several of TROne's cases run through
 * their GLUE - the per-tick state machine, the blocked counter, the direct
 * y_rot write - or through their orbit camera, and ours are not built yet.
 * Where such a case still says something about the pure function, that half is
 * ported here and the comment says which half it is; where it does not, the
 * case is listed by name at the end of the run under "not ours yet" with the
 * step that owes it: the state table is round 161a step 3, the state machine
 * and the direct turn are step 4, the camera is workflow A2, the pointer
 * profile's draw edge is workflow B. Nothing goes missing between steps that
 * way - the list is printed on every run, not buried in a comment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../../../TOMB5 Code/TOMB5/game/modern_move.h"

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
 * ours is the rest position itself, because our dead zone starts at 0 (the
 * hardware one is already applied - see MODERN_DEAD). */
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

	/* TROne: modern.diagonal_is_45_degrees. A diagonal has to be EXACTLY 45
	 * degrees or a synthetic stick - the D-pad below, and workflow B's WASD -
	 * sends Lara slightly askew on every diagonal. */
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
	 * position) so that the frame behaves like Tank. Ours is computed by step 4
	 * with the engine's own phd_atan and handed in, so the equivalent statement
	 * here is what the translation does with it: a camera heading equal to
	 * Lara's makes a forward push mean "straight ahead", which IS the classic
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
	 * 0x800) - this file cannot include that header, so step 4's
	 * modern_state.cpp carries the static_assert that says they still agree.
	 * This is the other half of that guard: the values as the translation
	 * knows them. */
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
	 * mask is what step 4's seam clears before it writes a result, so it has
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

	/* The constants of the scheme, as ModernDefaults hands them out. The play
	 * test changes MODERN_TURN_RATE and possibly MODERN_WALK_BELOW and
	 * MODERN_DEAD (design section 3, the three "measure" rows); everything
	 * else is frozen here so that a change to one of them is a change somebody
	 * had to mean. */
	{
		MODERN_CFG cfg;

		ModernDefaults(&cfg);
		ok(cfg.dead == 0, "dead zone starts at nothing (the hardware one is applied)",
			cfg.dead, 0);
		ok(cfg.walk_below == 62, "walk below 62 of 128 (TROne 16000 of 32767)",
			cfg.walk_below, 62);
		ok(cfg.aim_window == 0x0400, "aligned window 2.8 degrees", cfg.aim_window, 0x0400);
		ok(cfg.aligned_factor == 2, "and twice that once aligned", cfg.aligned_factor, 2);
		ok(cfg.spin_enter == 0x5000, "spin above 112.5 degrees", cfg.spin_enter, 0x5000);
		ok(cfg.spin_exit == 0x2800, "run again below 56.25", cfg.spin_exit, 0x2800);
		ok(cfg.stand_spin == 0x3000, "standing, spin above 67.5", cfg.stand_spin, 0x3000);
		ok(cfg.steer_step == 0x0180, "the thumb's noise floor", cfg.steer_step, 0x0180);
		ok(cfg.recenter == 0x1000, "re-seat at 22.5 degrees of stick", cfg.recenter, 0x1000);
		ok(cfg.aim_sector == 0x2000, "the quadrant boundary", cfg.aim_sector, 0x2000);
		ok(cfg.block_frames == 6, "six ticks make a block", cfg.block_frames, 6);
		ok(cfg.block_eps == 4, "four world units still count as not moving",
			cfg.block_eps, 4);
		ok(cfg.turn_rate == 3277, "the direct turn's rate (TROne parity)", cfg.turn_rate, 3277);
		ok(cfg.direct_turn == 1, "the direct turn's own gate is on", cfg.direct_turn, 1);
		ok(cfg.snap_delta == 0x4000, "past 90 degrees the turn is taken at once",
			cfg.snap_delta, 0x4000);
		ok(cfg.snap_forward == 0x2000, "forward returns at 45 degrees left",
			cfg.snap_forward, 0x2000);
		ok(cfg.stick_full == 128, "full deflection in our units", cfg.stick_full, 128);
		ok(cfg.hold_course == 1 && cfg.back_turns == 1 && cfg.aim_strafe == 1 &&
			cfg.jump_dir == 1 && cfg.block_turn == 1, "all five rules on",
			cfg.hold_course + cfg.back_turns + cfg.aim_strafe + cfg.jump_dir +
			cfg.block_turn, 5);

		/* The two candidates the play test will be offered beside 3277. They
		 * are named constants and not comments so that swapping one in is a
		 * one-word edit (design section 3.1, row 16). */
		ok(MODERN_TURN_RATE_VANILLA == 1456, "vanilla's fastest turn, for the play test",
			MODERN_TURN_RATE_VANILLA, 1456);
		ok(MODERN_TURN_RATE_MIDDLE == 2184, "and the one in between",
			MODERN_TURN_RATE_MIDDLE, 2184);
	}

	/* ModernResetState is what step 4 calls on a level start, on a death and
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
		ModernResetState(&state);
		ok(!state.have_ref && !state.ref && !state.course && !state.last_off &&
			!state.spin && !state.aligned && !state.aiming && !state.blocked_count &&
			state.last_mode == MODERN_MODE_OFF, "the reset really clears every field",
			state.have_ref + state.ref + state.course + state.last_off + state.spin +
			state.aligned + state.aiming + state.blocked_count + state.last_mode, 0);
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
	 * OURS IS 0 (MODERN_DEAD): XInput has already thrown away its own 24 per
	 * cent before ReadJoystick sees the axes, and a second dead zone would
	 * only eat travel. So the case is run twice - once at rest, which is the
	 * only silent input at our default, and once with their threshold scaled
	 * in, which is what the user gets if the measurement plan says raise it. */
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

	/* Ours: THE NOISE FLOOR ITSELF, which is the third of the four rules and
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
	 * Theirs runs through the glue and the direct turn (step 4). The half that
	 * is arithmetic is this: pull back, let the camera swing a degree a tick
	 * for twenty ticks, and the course must not move by a unit. */
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
	 * of 32767; ours is 62 of 128, which is the same half deflection - and it
	 * is the default, so nothing is set here. */
	ModernDefaults(&cfg);
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
	 * go gives the turn back in the same tick. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 0;
	sit.classic = 0;
	sit.drawn = 1;
	sit.action = 1;
	sit.jump = 0;
	sit.blocked = 0;

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

	/* aim_strafe = 0 takes the whole rule out, which is how a play test can
	 * bisect a complaint about it without a rebuild of the scheme. */
	cfg.aim_strafe = 0;
	sit.drawn = 1;
	sit.action = 1;
	ModernResetState(&state);
	tick(&cfg, &state, S(30000), 0, 0, 0, &sit, &out);
	ok(out.mode == MODERN_MODE_RUN, "aim_strafe 0 switches the strafe off",
		out.mode, MODERN_MODE_RUN);
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
	 * back flip, a side flip. Exactly ONE bit, and nothing turns. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 1;
	sit.blocked = 0;

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
	 * from a clean state, because it is the one the whole repair round was
	 * about: back plus jump must NOT become "turn round and jump forward". */
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
	 * The round's first pass read theirs as passing on the Remaster profile
	 * forcing aim_strafe off, and froze OUR answer (a side STEP while firing)
	 * as a difference. That reading was wrong and the review caught it: the
	 * line that disarms aiming on a jump is trone_modern_glue.h:473, OUTSIDE
	 * the profile block above it, and their own test builds a plain
	 * modern_test_glue() that never sets remaster_profile. So the expectation
	 * is theirs unconditionally, and it is ours now too - the translation
	 * itself stops aiming on the tick she jumps (game\modern_move.cpp, rule 3).
	 * Asserted here for all four combinations of gun and trigger. */
	ModernResetState(&state);
	sit.anim_state = 2;
	sit.jump = 1;
	sit.action = 0;
	{
		int drawn;

		for (drawn = 0; drawn < 2; drawn++)
		{
			sit.drawn = drawn;
			state.have_ref = 0;
			tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
			ok(out.bits == MODERN_IN_RIGHT, "the side flip works with and without a gun out",
				out.bits, MODERN_IN_RIGHT);
		}
	}

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
	 * with active 0 and no bits at all, so that step 4's seam leaves the
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

	/* THE STATE TABLE ITSELF, ROUND 161a STEP 3: an explicit switch in
	 * game\modern_move.cpp, every classic entry named after design sections
	 * 1.7 and 6.1 and the routine it runs in game\lara.cpp:40-181. Checked
	 * here by name against the TROne expectations this file was waiting on,
	 * and then as a full sweep against a reference table of our own, so "all
	 * 139 states classified" is a swept fact and not a sample. */

	/* TROne: modern.shared_state_numbers. Standing (2/15) is already
	 * ModernStateIsStanding's job from step 1; this is the classic/modern
	 * half. NOTE ON 25/26/27: TROne's own list (design section 1.7) and this
	 * very expectation both call them classic - the back flip and the two
	 * side flips read a direction bit as "end the flip", not "where to go"
	 * (game\lara.cpp:765 AS_TWIST on IN_RIGHT, :781 the mirror on IN_LEFT,
	 * :799 IN_FORWARD|IN_ROLL). This round's own build brief says "25-29 are
	 * modern" in its step 3 paragraph; the switch measures the routines
	 * instead of typing that sentence in as a test, and what it measured is
	 * what is asserted below. Reported in the step 3 record. */
	{
		const short is_classic[] = { 10, 17, 25, 26, 27, 36, 57, 76 };
		const short is_modern[] = { 2, 3, 81 };

		for (i = 0; i < (int)(sizeof(is_classic) / sizeof(is_classic[0])); i++)
			ok(ModernStateIsClassic(is_classic[i]) == 1,
				"shared_state_numbers: classic 10/17/25/26/27/36/57/76",
				ModernStateIsClassic(is_classic[i]), 1);

		for (i = 0; i < (int)(sizeof(is_modern) / sizeof(is_modern[0])); i++)
			ok(ModernStateIsClassic(is_modern[i]) == 0,
				"shared_state_numbers: not 2/3/81",
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
	 * forgets the held course"): 17 AS_SWIM asks for classic by its own
	 * anim_state, with no help from sit.classic. */
	sit.anim_state = 17;
	sit.classic = 0;
	state.have_ref = 0;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.active == 0, "water_and_inventory: AS_SWIM(17) is classic by itself",
		out.active, 0);

	/* The water group in full - design section 6's decision and the brief's
	 * exact list. A2's job, not A1's, so every one comes back inactive; the
	 * mode is never AIM/JUMP/BACK/RUN because the state veto fires first. */
	{
		const short water[] = { 13, 17, 18, 33, 34, 35, 44, 47, 48, 49, 52, 53, 55 };

		for (i = 0; i < (int)(sizeof(water) / sizeof(water[0])); i++)
		{
			sit.anim_state = water[i];
			sit.classic = 0;
			state.have_ref = 0;
			tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
			ok(out.active == 0, "water state is classic (modern in A2)", out.active, 0);
			ok(out.mode == MODERN_MODE_OFF, "...mode off, not just inactive",
				out.mode, MODERN_MODE_OFF);
		}
	}

	/* Design section 6.1's "harmless but listed": Busy states where the input
	 * mask already strips everything but the four direction bits, so this is
	 * measured to be inconsequential either way and named so nobody removes
	 * it later thinking it was missed. 98 shares AS_PICKUP's own routine. */
	{
		const short busy[] = { 39, 40, 41, 42, 43, 50, 51, 67, 70, 98 };

		for (i = 0; i < (int)(sizeof(busy) / sizeof(busy[0])); i++)
			ok(ModernStateIsClassic(busy[i]) == 1, "harmless-but-listed Busy state",
				ModernStateIsClassic(busy[i]), 1);
	}

	/* ALL 139, AGAINST A REFERENCE OF OUR OWN. Not a sample: every index
	 * 0..138 against a frozen array built from the same two design sections
	 * the switch in modern_move.cpp was built from, so a slip in either one
	 * shows up as a mismatch here rather than as the same mistake made twice
	 * and agreeing with itself. */
	{
		const short classic[] =
		{
			10, 13, 17, 18, 25, 26, 27, 30, 31, 33, 34, 35, 36, 37, 38, 39, 40,
			41, 42, 43, 44, 47, 48, 49, 50, 51, 52, 53, 55, 56, 57, 58, 59, 60,
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

		ok((int)(sizeof(classic) / sizeof(classic[0])) == 95,
			"...95 of 139 are classic, 44 are modern",
			(int)(sizeof(classic) / sizeof(classic[0])), 95);
	}
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
	 * "is a weapon held". Ours is a constant (MODERN_BACK_TURNS), because A1
	 * has no pointer aiming to hop away from; the switch is here and workflow
	 * B decides whether to drive it. The sector the rule fires in is asserted
	 * either side, because it is the one place a half turn and a step apart
	 * differ by a single unit. */
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
	 * a turn used to go out together. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;

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
	const short allowed[] = { 0, 1, 2, 6, 7, 12, 14, 20, 73 };
	const short airborne[] = { 3, 9, 10, 15, 17, 25, 26, 27, 36, 57, 76, 81 };
	long yaw;
	long total;
	long count;
	int i;
	int t;

	printf("the direct turn - the one place the engine writes her heading\n");

	/* TROne: modern.direct_ground_turn_is_bounded_and_never_changes_airborne_
	 * poses. Their four numbers, unchanged: our angles are their angles and
	 * the rate is the same 3277. */
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

	/* Their twelve states: airborne, hanging, climbing, swimming, in an
	 * interaction pose. There y_rot is the animation's and nothing outside
	 * may touch it. */
	for (i = 0; i < (int)(sizeof(airborne) / sizeof(airborne[0])); i++)
		ok(ModernGroundTurn(airborne[i], 0x4000, 3277) == 0,
			"no write in an airborne, climbing or object pose",
			ModernGroundTurn(airborne[i], 0x4000, 3277), 0);

	/* Their ten ticks for a half turn - the number the play test will judge
	 * the rate by. 32768 / 3277 is 10.0009, so the tenth tick lands exactly
	 * on it rather than overshooting. */
	yaw = 0;

	for (t = 0; t < 10; t++)
		yaw = ModernWrap(yaw + ModernGroundTurn(2, 0x8000L - yaw, 3277));

	ok(ModernWrap(yaw) == -32768, "ten ticks for 180 degrees at 3277", ModernWrap(yaw), -32768);

	/* Ours, because the state list is ours and not theirs (TR5 runs to 138
	 * where TROne stop at 104): exactly nine states allow the write, and the
	 * nine are the nine. */
	count = 0;

	for (i = 0; i < 139; i++)
		if (ModernStateAllowsDirectTurn((short)i))
			count++;

	ok(count == 9, "nine of the 139 states allow the write", count, 9);

	for (i = 0; i < (int)(sizeof(allowed) / sizeof(allowed[0])); i++)
		ok(ModernStateAllowsDirectTurn(allowed[i]) == 1, "...and these are the nine",
			ModernStateAllowsDirectTurn(allowed[i]), 1);

	/* And the two candidate rates the play test may pick instead still turn
	 * a half circle in a sensible number of ticks - this IS the number the
	 * user is choosing between, so it is written down rather than reasoned
	 * about. 32768/1456 = 22.5, so 22 whole rates and a 23rd tick for the
	 * remaining 736; 32768/2184 = 15.003, so 15 whole rates and a 16th tick
	 * for the remaining 8. Ten ticks, sixteen ticks, twenty-three ticks: at
	 * 30 Hz that is a third of a second, half a second and three quarters. */
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
	 * Running, the whole of it is taken in one tick - that is the Remaster's
	 * about-turn. Standing, it is NOT, and TROne measured why: a one-tick
	 * reversal out of a stand is spent before the player has BACK and JUMP
	 * down together, so the back flip becomes unreachable. Their third case
	 * switches the stand back to the snap with turn_snap_stand; we did not
	 * build that switch, because the case it restores is the bug. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 1;					/* AS_RUN - she is really running */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;

	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"running: the direct turn takes charge", 1, 1);
	ok(absl(yaw) == 0x8000, "...and the whole half turn in one tick", absl(yaw), 0x8000);

	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 1,
		"standing: it takes charge as well", 1, 1);
	ok(absl(yaw) == MODERN_TURN_RATE, "...but only one rate, so BACK and JUMP still meet",
		absl(yaw), MODERN_TURN_RATE);
	ok((out.bits & MODERN_IN_FORWARD) == 0, "...and no step forward while she turns",
		out.bits & MODERN_IN_FORWARD, 0);

	/* TROne: modern.ground_reversal_turns_without_a_step_in_the_wrong_
	 * direction, all twelve ticks. Step 2 already asserted the first tick's
	 * "no step"; this is the rest of it - she arrives, and only then does
	 * forward come back.
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
	 * the turn half (step 2 asserted the course half over a swinging camera).
	 * Tick 0 is the stand and turns by one rate; from tick 1 she is running,
	 * the camera has CUT by 180 degrees, and the held course does not care:
	 * the reversal finishes in that one tick and stays finished for the rest
	 * of the twenty. */
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

		ok(yaw == -32768, "held back through a camera cut: she is round and stays round",
			yaw, -32768);
		ok((out.bits & MODERN_DIR_MASK) == MODERN_IN_FORWARD,
			"...running forward on the held course", out.bits & MODERN_DIR_MASK,
			MODERN_IN_FORWARD);
	}

	/* OURS, the four cases that say the write cannot happen where it must
	 * not. The second gate first: MODERN_DIRECT_TURN off leaves the turn bit
	 * in the word and the automaton in charge, which is how a play test can
	 * ask "is Modern wrong, or is only the direct turn wrong?". */
	ModernDefaults(&cfg);
	cfg.direct_turn = 0;
	ModernResetState(&state);
	sit.anim_state = 1;
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 0,
		"direct_turn 0: no write", 0, 0);
	ok(yaw == 0, "...and no angle", yaw, 0);
	ok((out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) != 0, "...the turn bit stands instead",
		out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT), MODERN_IN_LEFT);

	/* Aiming: with aim_strafe switched off the mode is RUN and the game is
	 * already turning her onto her target - two hands on one heading. */
	ModernDefaults(&cfg);
	cfg.aim_strafe = 0;
	ModernResetState(&state);
	sit.drawn = 1;
	sit.action = 1;
	ok(tick_turn(&cfg, &state, 0, S(32000), 0, 0, &sit, &out, &yaw) == 0,
		"weapon up and action held: the game turns her, not us", 0, 0);

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
	 * bits, never IN_WALK. Half a stick, a half turn of error, running. */
	ModernResetState(&state);
	sit.jump = 0;
	sit.anim_state = 1;
	tick_turn(&cfg, &state, 0, S(12000), 0, 0, &sit, &out, &yaw);
	ok((out.bits & MODERN_IN_WALK) != 0, "a gentle stick still walks while the turn is ours",
		out.bits & MODERN_IN_WALK, MODERN_IN_WALK);
	ok(absl(yaw) == 0x8000, "...and the reversal still happens", absl(yaw), 0x8000);

	/* THE LEVEL BOUNDARY - what the reset at the level start is worth (#161,
	 * the one high finding of the second review pass).
	 *
	 * The held course is a WORLD heading and a level change throws that world
	 * away. game\modern_state.cpp's tick guard cannot notice: no tick runs at
	 * all between two levels, so the boundary reaches it as an ordinary step
	 * of one. The reset is therefore hung at the level start itself, in
	 * game\gameflow.cpp beside InterpInvalidate("level start"). Nothing in
	 * this tool can reach that call - it links the arithmetic and no engine -
	 * so what is asserted here is what the call BUYS, which is the same thing
	 * measured from the other end: anybody who deletes the call has to delete
	 * these checks with it.
	 *
	 * Level A: the camera on 0x2000, the stick held forward, three ticks seat
	 * the course. Level B: she stands on -0x4000 with the chase camera behind
	 * her, and the player never let go of the stick. The camera is then held
	 * still on purpose - the course is HELD, so nothing the camera does can
	 * re-seat it, which is exactly the finding.
	 *
	 * The rate is pinned at 3277 here, the way the ten-ticks-for-a-half-turn
	 * check above pins it: two of these numbers are a count of ticks, and the
	 * play test is expected to change MODERN_TURN_RATE, which must not make
	 * this block fail for a reason that has nothing to do with the boundary. */
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

		ok(forward_at == 5, "...no forward for five ticks of it", forward_at, 5);
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
		 * snap past 90 degrees takes the whole wrong turn in one tick - the
		 * same defect, worn the other way round. */
		carried = state;
		sit.anim_state = 1;				/* AS_RUN */
		tick_turn(&cfg, &carried, 0, -S(32000), lara, lara, &sit, &out, &yaw);
		ok(yaw == 24576, "caught running, the snap whips all 135 degrees in one tick",
			yaw, 24576);
	}
}

/* ---------------------------------------------------------- the weapons ---- */

static void weapons()
{
	long bits;
	int frame;

	printf("the weapon half - Toggle is the original, bit for bit\n");

	/* ModernWeaponBits(combat_hold, ready, holstered, held, bits). TROne's is
	 * trone_modern_weapon_bits(glue, bits, ready, holstered) with the mode and
	 * the key inside the glue struct; ours takes both, because a pure function
	 * cannot ask a struct which mode it is in. Nothing calls it yet - workflow
	 * B wires it at the second seam of specific\input.cpp and puts
	 * tomb5.combat_hold on the first argument. */

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
	 * half that is the weapon bits. (The interact and shoot halves are
	 * workflow B's IN_ACTION2 and are not in this function at all.) */
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
	 * Tank profile Toggle IS the original and needs none. Workflow B owns the
	 * edge, together with the pointer keys it belongs to. What is ours here is
	 * the other side of the same coin, asserted above: Toggle never invents a
	 * bit and never swallows one. */
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

	/* (d) THE D-PAD. specific\dxshell.cpp ORs the D-pad into the very same
	 * axes as the stick, but at +-20 of 128 - a quarter of the way. Under Tank
	 * that is only a direction bit and the size does not matter; under Modern
	 * the size IS the speed, and 20 is under walk_below (62), so a D-pad-only
	 * push would make Lara WALK. Decision 3 of the brief therefore has step 4
	 * replace the +-20 with +-MODERN_STICK_FULL before the translation runs.
	 * Both halves are asserted: that the synthesis gives the same answer as a
	 * full stick push, and that the raw +-20 does NOT - because the second is
	 * the whole reason for the first. */
	{
		static const long dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		static const long dz[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		long same = 0;
		long walked = 0;

		sit.anim_state = 2;

		for (i = 0; i < 8; i++)
		{
			MODERN_RESULT syn;
			MODERN_RESULT full;
			MODERN_RESULT raw;
			const long fx = dx[i] * MODERN_STICK_FULL;
			const long fy = -dz[i] * MODERN_STICK_FULL;	/* forward is -y in our axes */

			ModernResetState(&state);
			tick(&cfg, &state, fx, fy, 0x1000, 0, &sit, &syn);

			ModernResetState(&state);
			tick(&cfg, &state, dx[i] * 127, -dz[i] * 127, 0x1000, 0, &sit, &full);

			ModernResetState(&state);
			tick(&cfg, &state, dx[i] * 20, -dz[i] * 20, 0x1000, 0, &sit, &raw);

			if (syn.bits == full.bits && syn.target == full.target &&
				syn.mode == full.mode && syn.active == full.active)
				same++;

			if ((raw.bits & MODERN_IN_WALK) && !(syn.bits & MODERN_IN_WALK) &&
				(raw.bits & ~(long)MODERN_IN_WALK) == syn.bits)
				walked++;
		}

		ok(same == 8, "(d) the synthesised D-pad reads exactly like a full stick push",
			same, 8);
		ok(walked == 8, "(d) ...and the raw +-20 of dxshell would make her walk instead",
			walked, 8);
	}
}

/* ------------------------------------------------------ what is not ours --- */

static void deferred()
{
	printf("\nnot ours yet - TROne expectations a later step owes, by name:\n");
	printf("  B       modern.remaster_back_input_distinguishes_running_from_armed_hopback\n");
	printf("  none    modern.angles_match_the_classic_convention (trone_modern_camera_yaw)\n");
	printf("          - the degenerate camera falls back to Lara's yaw in game\\modern_state.cpp,\n");
	printf("            which needs the engine's own camera and cannot be linked here\n");
	printf("  A2      modern.orbit_stays_in_world_space_and_accepts_mouse_while_running\n");
	printf("  B       combatmode.toggle_pulses_once_per_press_in_the_modern_profile (the edge)\n");
	printf("  B       modern.held_equip_draws_once_and_interaction_does_not_fire (IN_ACTION2)\n");
}

int main()
{
	printf("ModernTest - the arithmetic of the modern control (#161, round 161a)\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "modern_move.cpp\n");
	printf("expectations ported from TROne (GPLv3) by Todesengel1931\n\n");

	angles();
	bit_values();
	stick_to_tank();
	held_course();
	spin_hysteresis();
	walk_threshold();
	aiming();
	jumping();
	classic_states();
	back_input();
	chatter();
	blocked();
	direct_turn();
	weapons();
	sweeps();
	deferred();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

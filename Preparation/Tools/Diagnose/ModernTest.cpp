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
 * (#161, round 161a step 2; the camera's orbit added in round 161a2 step 1, its
 * seam's own angle arithmetic in step 2 and the water's turn and pitch table in
 * step 4 - step 3, the pad table, has no pure function to check, and the two
 * review passes of the round retargeted the D-pad block it retired, the second
 * one to measure the defect its own first answer left behind).
 * Build with bmt.bat, run with no arguments.
 *
 * WHAT IT CHECKS, and why it exists at all. The modern control is a pure
 * function: stick direction plus a remembered world heading in, the 1996
 * direction bits out. Everything a play test can see - Lara turning the wrong
 * way, spinning in circles in front of a fixed camera, refusing to turn at a
 * wall - is one of about a dozen decisions in that function, and every one of
 * them is a threshold that a play test can only reach by accident. So the
 * function is COMPILED INTO THIS TOOL, not copied into it: bmt.bat hands the
 * engine's own game\modern_move.cpp and game\modern_camera.cpp to the compiler
 * beside this file, exactly as InterpTest is built against
 * game\interpolate_math.h. There is no twin to keep in step, and a change to
 * the tree is a change this test sees.
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
 * and the direct turn are step 4, the camera's orbit is round 161a2 step 1 and
 * is checked here now - with the two angle lines its engine seam adds around
 * it, step 2, which is as much of game\camera.cpp as can be linked without a
 * level - the pointer
 * profile's draw edge is workflow B. Nothing goes missing between steps that
 * way - the list is printed on every run, not buried in a comment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../../../TOMB5 Code/TOMB5/game/modern_move.h"
#include "../../../TOMB5 Code/TOMB5/game/modern_camera.h"
/* The 60 fps cut detection's own thresholds and its own rule, so that
 * camera_camcut() measures the orbit against the tree rather than against a
 * number copied out of it (#161, round 161a2, step 2). It costs nothing: this
 * header includes stdlib.h and math.h and no engine at all, which is why
 * InterpTest is built against it the same way. */
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
	 * degrees or a synthetic stick - workflow B's WASD, and the D-pad until
	 * round 161a2 step 3 retired it (see the block at the end of sweeps) -
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
		/* Round 161a-fix (#161 item 1): the first play test's log moved this
		 * off TROne's 62 (16000 of 32767) to 0 - the stick must never be able
		 * to set IN_WALK, only the pad's own walk key can (MODIFICATIONS.md).
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
		/* Round 161a-fix (#161 item 2), SECOND MEASUREMENT (MODIFICATIONS.md):
		 * moved off TROne's 0x1000 - briefly to 0x2000, then superseded by a
		 * second measurement of the same log that a wider window still could
		 * not answer - to MODERN_STEER_STEP itself: any stick change above
		 * the noise floor now re-seats. held_course() pins 0x1000 explicitly
		 * for the TROne-derived checks that came with that number, and adds
		 * its own four at the shipped default. */
		ok(cfg.recenter == cfg.steer_step, "recenter now equals the noise floor itself",
			cfg.recenter, cfg.steer_step);
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
	/* Round 161a-fix (#161 item 2) moved the shipped default off TROne's
	 * 0x1000 (bit_values() checks the shipped number, MODERN_RECENTER == 0x0180
	 * == steer_step now, own story in game\modern_move.h). Every check in
	 * THIS function is otherwise a TROne-ported statement about the held
	 * course itself, unaffected by which number recenter has - but it is
	 * pinned explicitly anyway so a future default change cannot silently
	 * make it mean less than TROne measured. THIS pin is also requirement (d)
	 * of the second measurement's own test list: the old three-band
	 * behaviour, provable under an explicit cfg even though it no longer
	 * ships. The new default's own tests are at the end of this function. */
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

	/* ROUND 161a-fix (#161 item 2), SECOND MEASUREMENT OF THE SAME LOG,
	 * SUPERSEDING THE FIRST (game\modern_move.h has the full story). Ticks
	 * 470-508 (t=53.0s) of the play test: the stick turning slowly from right
	 * (125/0) to back-right (85/105) over 39 ticks - 51 degrees of stick
	 * rotation, every single per-tick step small enough to stay under EITHER
	 * the old 0x1000 or the first answer's 0x2000 - while the chase camera
	 * swung 120 degrees behind her. The course only ever fine-steered: it
	 * followed the stick's 51 degrees and finished 50 degrees short of what
	 * the player meant, because he read the stick against the camera he
	 * could see at that instant - "solche Bewegungen schräg nach hinten sind
	 * bei uns unmöglich" - not against a course seeded once and nudged since.
	 * The fix: MODERN_RECENTER == MODERN_STEER_STEP, so ANY stick change
	 * above the noise floor re-seats. Four checks, matching the shape of the
	 * measurement and its two guarantees. */

	/* (a) AT THE SHIPPED DEFAULTS: a stick that keeps moving - 3 degrees a
	 * tick, comfortably over the 2.1-degree noise floor, the shape of the
	 * measured episode above - re-seats on EVERY tick while a camera swings
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

	/* (b) AT THE SHIPPED DEFAULTS: a stick held EXACTLY still while the
	 * camera swings 90 degrees over ten ticks - ref and course must not move
	 * by a unit. This is the property the FIRST measurement (and TROne's own
	 * circling finding) demands, and (a) does not touch it: a re-seat only
	 * fires on a stick CHANGE, and there is none here - the held-course
	 * guarantee and the camera-cut expectation, in the one check. */
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

	/* (c) AT THE SHIPPED DEFAULTS: one degree of stick movement - under the
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
	 * TROne's own 0x1000 keeps every ported expectation above provable, the
	 * old three-band behaviour and all, exactly as it was before this round -
	 * named here so the four checks this comment promised are easy to find. */
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
	 * of 32767; ours is 62 of 128, the same half deflection - CAPABILITY ONLY
	 * after round 161a-fix (#161 item 1): the shipped default is 0 (checked
	 * in bit_values()), so this whole block sets walk_below back to 62 by
	 * hand to prove the mechanism itself still works, exactly as a play test
	 * could with an explicit cfg. It is simply not what a player gets without
	 * asking any more. */
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

	/* ROUND 161a-fix (#161 item 1), AT THE SHIPPED DEFAULTS. The play test's
	 * log measured why a magnitude threshold cannot tell "still travelling"
	 * from "meant to walk" on a real stick at all (MODIFICATIONS.md: of 32
	 * pushes from rest, 19 read 20-60 of 128 on their very first tick and 25
	 * of the same 32 were already >= 100 on the second), so the user's
	 * decision was that the stick must never set IN_WALK - not a smaller
	 * number, zero. This sweeps every direction and every magnitude the
	 * stick can produce and asserts the bit never once appears. */
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
	 * back flip, a side flip. Exactly ONE bit, and nothing turns.
	 * ARMED (round 161a-fix, #161 item 3): this whole mechanism is now the
	 * user's Kampfmodus case - a weapon has to be in her hand for AS_COMPRESS
	 * to read a flip at all, see rule 4's own comment in modern_move.cpp - so
	 * every TROne-ported case below sets sit.drawn = 1 to stay inside that
	 * case; jumping()'s own two-case split for the unarmed answer is further
	 * down, where TROne's side-flip expectation used to be the only one. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 2;					/* AS_STOP */
	sit.classic = 0;
	sit.drawn = 1;
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
	 * was theirs unconditionally - UNTIL the first play test measured it
	 * against a Remaster recording of jumps with and without weapons and the
	 * user overruled the "without" half of it (round 161a-fix, #161 item 3,
	 * MODIFICATIONS.md): "Wenn Lara die Waffen in der Hand hat (Kampfmodus)
	 * ist das jetzige Verhalten richtig. Wenn sie nicht im Kampfmodus ist,
	 * macht sie egal wie ich sie drehe den normalen Sprung aus dem Stand in
	 * die angegebene Richtung." Armed, TROne's side flip stands unchanged;
	 * unarmed, it is rule 6 (the run branch) instead - turn onto the course,
	 * IN_FORWARD, and a forward jump rather than a flip. So TROne's one case
	 * is now two of ours. */
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
	 * user's sentence at once - turning AND the forward jump together -
	 * rather than only the first few ticks of a wide turn, where stand_spin
	 * (0x3000, design row 10, unchanged by this round) would still suppress
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
		/* AND ONE NUMBER HAS MOVED SIDES, DELIBERATELY: 17 AS_SWIM. TROne's
		 * expectation puts it with the classic states and round 161a agreed,
		 * because A1 had no camera pitch to swim by. Round 161a2 step 4 built
		 * one, so the swim is translated now like anything else and 17 is
		 * asserted on the modern side here. A divergence from a ported
		 * expectation, named rather than quietly dropped; the rest of that
		 * expectation stands unchanged. */
		const short is_classic[] = { 10, 25, 26, 27, 36, 57, 76 };
		const short is_modern[] = { 2, 3, 17, 81 };

		for (i = 0; i < (int)(sizeof(is_classic) / sizeof(is_classic[0])); i++)
			ok(ModernStateIsClassic(is_classic[i]) == 1,
				"shared_state_numbers: classic 10/25/26/27/36/57/76",
				ModernStateIsClassic(is_classic[i]), 1);

		for (i = 0; i < (int)(sizeof(is_modern) / sizeof(is_modern[0])); i++)
			ok(ModernStateIsClassic(is_modern[i]) == 0,
				"shared_state_numbers: not 2/3/81, and not 17 since step 4",
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
	 * forgets the held course"). The INVENTORY half is what still holds
	 * unchanged - it arrives as the caller's veto, tested above. The WATER
	 * half is the one round 161a2 step 4 turned over: 17 AS_SWIM now asks for
	 * the translation by its own anim_state, and the pose it leaves alone is
	 * the one the seam still does not touch (the automaton's animation - the
	 * scheme writes angles, never a frame). */
	sit.anim_state = 17;
	sit.classic = 0;
	state.have_ref = 0;
	tick(&cfg, &state, S(32000), 0, 0, 0, &sit, &out);
	ok(out.active == 1, "step 4: AS_SWIM(17) is the translation's now",
		out.active, 1);
	ok(out.mode == MODERN_MODE_RUN, "...and it is the ordinary run branch",
		out.mode, MODERN_MODE_RUN);

	/* THE WATER GROUP, SPLIT THE WAY STEP 4 SPLIT IT. The eight states the
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
	 * and agreeing with itself.
	 *
	 * ROUND 161a2 STEP 4 TOOK EIGHT OUT OF IT: 13/17/18 under the water and
	 * 33/34/47/48/49 on it. 35, 44, 52, 53, 55 and 66 stayed (see the two
	 * groups above), so the count moves from 95 to 87. */
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

/* THE WATER (round 161a2 step 4). TROne have no expectation here at all -
 * their water is the classic input - so every number below is OUR measurement
 * of game\laraswim.cpp and game\larasurf.cpp, and the point of the group is
 * that the modern swimmer moves at TR5's own speeds and only the SCHEME
 * changes. The user's rule: "Die Steuerung unter Wasser ist genau gleich wie
 * ausserhalb, da ist kein Unterschied." */
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

	/* THE TABLE, against the three rates measured out of the engine: 1092 is
	 * LaraUnderWater's own ceiling on lara.turn_rate, 728 is what
	 * lara_as_surftread and lara_as_surfswim write into y_rot themselves, 364
	 * is what the three side and back strokes write. */
	{
		const short swim[] = { 13, 17, 18 };
		const short surface[] = { 33, 34 };
		const short side[] = { 47, 48, 49 };
		const short dry[] = { 0, 1, 2, 15, 35, 44, 52, 53, 55, 66, 118 };

		for (i = 0; i < (int)(sizeof(swim) / sizeof(swim[0])); i++)
		{
			ok(ModernWaterTurnRate(&cfg, swim[i]) == 1092,
				"under water she turns at LaraUnderWater's own 1092",
				ModernWaterTurnRate(&cfg, swim[i]), 1092);
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

	/* A STATE THE WATER DOES NOT OWN IS NOT TOUCHED. The seam branches on the
	 * same table, so this can only ever be reached by a caller that has not,
	 * and then the honest answer is "not mine". */
	sit.anim_state = 1;		// AS_RUN
	water_out(&out, 0x4000, MODERN_IN_FORWARD);
	ok(ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke) == 0,
		"a land state is not the water's", 0, 0);
	ok(out.bits == MODERN_IN_FORWARD, "...and its bits stand", out.bits, MODERN_IN_FORWARD);
	ok(yaw == 0, "...no turn", yaw, 0);

	/* THE STICK AT REST IS NOT A COURSE. She keeps heading, attitude and
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

	/* THE TURN. Clamped to the state's rate, exact under it, and the sign is
	 * the error's - the same ModernStepToward the land's turn uses. */
	water_out(&out, 0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == 1092, "90 degrees to go: one tick is the swim rate", yaw, 1092);

	water_out(&out, -0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == -1092, "...and the other way round", yaw, -1092);

	water_out(&out, 500, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == 500, "under the rate the whole error is taken", yaw, 500);
	ok(out.turning == 0, "...and nothing is left to turn", out.turning, 0);

	water_out(&out, 0x4000, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(out.turning == 1, "with 90 degrees to go she is still turning",
		out.turning, 1);

	/* NO INSTANT ABOUT-TURN IN THE WATER, which is the one rule of the land's
	 * turn that is deliberately not here: past 90 degrees the land takes the
	 * whole error in one tick, and under water a one-tick half turn is the
	 * AS_WATERROLL animation's job and looks wrong without it. */
	water_out(&out, 0x7FFF, 0);
	ModernWaterMove(&cfg, &sit, &out, 0, 0, &yaw, &xrot, &stroke);
	ok(yaw == 1092, "half a turn away: still only one tick's worth", yaw, 1092);

	/* THE BITS. Under water IN_FORWARD is SwimTurn's PITCH (x_rot -= 364), so
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

	/* THE STROKE, the user's own decision 7: "der linke Stick BEWEGT Lara in
	 * die Richtung", so the stroke is held for as long as the stick is out.
	 * Under water only - on the surface the stroke IS the forward bit, and
	 * IN_JUMP there means "dive" (ten ticks of it, larasurf.cpp). */
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
	 * the camera's own ModernCameraSwimPitch, and walked towards at SwimTurn's
	 * own 364 a tick. The conversion is the INVERSE OF THE ENGINE'S OWN LINE:
	 * ChaseCamera shows elevation = pitch_home + x_rot, so the attitude that
	 * belongs to an elevation is elevation - pitch_home, and a camera nobody
	 * has touched points at level swimming. */
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
	ok(xrot == 364, "one tick towards the pointer is SwimTurn's own 364", xrot, 364);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, -5000, 0, &yaw, &xrot, &stroke);
	ok(xrot == -364, "...and the same nose down", xrot, -364);

	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, 100, 0, &yaw, &xrot, &stroke);
	ok(xrot == 100, "under the rate she arrives in one tick", xrot, 100);

	/* THE LIMITS ARE LaraUnderWater's OWN (+-15470), and the target is clamped
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

	/* THE SURFACE IS FLAT. "Auf der Oberflaeche ist der Kurs flach" - so
	 * nothing there writes x_rot at all, whatever the orbit is pointing at,
	 * and the engine keeps her level anyway. */
	sit.anim_state = 33;
	water_out(&out, 0, 0);
	ModernWaterMove(&cfg, &sit, &out, -12000, 4000, &yaw, &xrot, &stroke);
	ok(xrot == 4000, "the surface does not follow the pointer", xrot, 4000);

	/* THE TWO THINGS THE TRANSLATION ITSELF HAD TO BE TOLD ABOUT THE WATER.
	 * A flare is held at LG_READY, so sit.drawn can be 1 down there and the
	 * aim branch would have strafed her around nothing; and with back_turns
	 * switched off the back branch would have sent IN_BACK, which under water
	 * is "pitch her nose up". Both are refused by the state, and the land
	 * cases beside them prove the refusal is the water's and not a change of
	 * the rule. */
	sit.anim_state = 17;
	sit.drawn = 1;
	sit.action = 1;
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

	/* WHAT IT COSTS, MEASURED BY RUNNING IT. A half turn under water and the
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
	ok(under == 31, "a half turn under water takes 31 ticks", under, 31);

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

	ok(ticks == 43, "level to straight up is 43 ticks", ticks, 43);
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
	const short allowed[] = { 0, 1, 2, 6, 7, 12, 14, 15, 20, 73 };
	const short airborne[] = { 3, 9, 10, 17, 25, 26, 27, 36, 57, 76, 81 };
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

	/* Their twelve states less one: airborne, hanging, climbing, swimming, in
	 * an interaction pose. There y_rot is the animation's and nothing outside
	 * may touch it. 15 AS_COMPRESS used to be the twelfth - round 161a-fix
	 * (#161 item 3) moves it to `allowed` below, because ours can now reach
	 * it with mode RUN (unarmed) where TROne's design never could. */
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
	 * where TROne stop at 104): TROne's nine ground states allow the write,
	 * PLUS 15 AS_COMPRESS since round 161a-fix (#161 item 3, see
	 * ModernStateAllowsDirectTurn's own comment in modern_move.cpp for why) -
	 * ten in all, and the ten are the ten. */
	count = 0;

	for (i = 0; i < 139; i++)
		if (ModernStateAllowsDirectTurn((short)i))
			count++;

	ok(count == 10, "ten of the 139 states allow the write", count, 10);

	for (i = 0; i < (int)(sizeof(allowed) / sizeof(allowed[0])); i++)
		ok(ModernStateAllowsDirectTurn(allowed[i]) == 1, "...and these are the ten",
			ModernStateAllowsDirectTurn(allowed[i]), 1);

	/* ROUND 161a-fix (#161 item 3): AS_COMPRESS keeps the write too, but only
	 * when she is unarmed and rule 6 is answering - never during an armed
	 * flip, because ModernDirectTurn's own mode check just below already
	 * refuses anything that is not MODERN_MODE_RUN. Both halves through the
	 * seam's own two calls (tick_turn), not just the state-list membership
	 * asserted above, so the interaction with rule 4's new sit->drawn gate is
	 * proven end to end. */
	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 15;			/* AS_COMPRESS */
	sit.classic = 0;
	sit.drawn = 0;					/* unarmed: rule 4 steps aside, rule 6 answers */
	sit.action = 0;
	sit.jump = 1;
	sit.blocked = 0;
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
	sit.drawn = 1;			/* armed (round 161a-fix, #161 item 3): a flip needs it now */
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
	 * EXPLICIT cfg.walk_below (round 161a-fix, #161 item 1): the shipped
	 * default is 0 now, which would make this tautological - the point here
	 * is that IN_WALK, WHEN THE CAPABILITY IS ASKED FOR, is not among the
	 * bits the direct turn clears, not that IN_WALK never appears at all. */
	ModernResetState(&state);
	cfg.walk_below = 62;		/* explicit: the shipped default is 0 */
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

	/* (d) THE D-PAD, AND WHAT THE MEDIPACKS ON IT COST UNTIL THE SECOND FIX
	 * ROUND OF 161a2.
	 *
	 * specific\dxshell.cpp does not OR the D-pad into the stick's two axes, it
	 * ASSIGNS: a direction press REPLACES whatever the stick was saying with
	 * +-20 of 128, a quarter of the way. Under Tank that is right and is
	 * Core's own idea - a direction bit has no size, so a quarter push and a
	 * full one mean the same thing. Under Modern the size IS the speed (20 is
	 * under TROne's walk_below, 62) and the direction IS a course.
	 *
	 * Round 161a answered that by BOOSTING the signature to a full push
	 * (ModernDPadToStick) and this block asserted the boost. Step 3 retired
	 * the boost, freed the cross for bindings (the user's decision 8: "das
	 * D-Pad wird dann in der modernen Steuerung fuer die Binds frei") and put
	 * the medipacks on it - and ERASED the +-20 signature in
	 * specific\input.cpp. That was the wrong file, and this block asserted
	 * that too. By the time the pair reaches input.cpp the stick's own reading
	 * is already overwritten, so the erasure could only ever turn "she walks
	 * at a quarter push" into "she stops dead", and only on the readings that
	 * matched the signature exactly; the rest came through as a course that
	 * was not hers. With a medipack on the cross, that is something a player
	 * does WHILE RUNNING. The second fix round keeps the left stick's own two
	 * axes beside the overwritten pair (joystick_read_lx/ly) and the movement
	 * seam reads those, so the cross costs the movement nothing at all.
	 *
	 * NOTHING LINKED HERE CAN RUN ANY OF THAT - it is engine code, in two
	 * files this test cannot see. So the block does what a pure test honestly
	 * can: it MEASURES the defect the fix removes, on the translation itself,
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

		/* THE SIZE OF THE DEFECT, over the whole square of stick positions
		 * with D-Pad Up held. dxshell assigns joystick_read_y = -20 and leaves
		 * x alone; the old seam then erased the pair when both axes were in
		 * {0, +20, -20} and not both zero. What the player asked for is the
		 * stick itself, which is what the seam hands over now. */
		for (sx = -MODERN_STICK_FULL; sx <= MODERN_STICK_FULL; sx++)
		{
			for (sy = -MODERN_STICK_FULL; sy <= MODERN_STICK_FULL; sy++)
			{
				MODERN_RESULT mine;		/* the stick, which is what she asked for */
				MODERN_RESULT then;		/* what the press used to leave of it */
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

/* THE ORBIT (#161, round 161a2, step 1): game\modern_camera.cpp, compiled
 * beside this file exactly the way the translation is, for exactly the same
 * reason - a feel number nobody can re-measure is a number nobody can change.
 *
 * NONE of the expectations below comes from TROne. Their orbit holds a WORLD
 * angle and never returns behind the player at all; ours holds an offset from
 * LARA'S OWN heading and lets it fall back. The numbers here come from two
 * places and the comments say which: the user's four sentences, quoted in full
 * in game\modern_camera.h, and the classic camera's own limits, measured out
 * of game\camera.cpp before a line of the orbit was written. */

static void camera_orbit()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE st;
	short yaw;
	short pitch;
	long lp;
	long ticks;

	printf("the orbit - the stick moves it, standing keeps it, running brings it back\n");

	ModernCameraDefaults(&cfg);

	/* The constants, held to the origins the header claims for them. A test
	 * that only checks behaviour lets a number drift away from its own
	 * reason; these six are the reasons. */
	ok(cfg.dead == 0, "the right stick's dead zone is the left one's: the hardware's",
		cfg.dead, 0);
	ok(cfg.stick_full == MODERN_STICK_FULL, "one scale for both sticks",
		cfg.stick_full, MODERN_STICK_FULL);
	ok(60 * cfg.yaw_rate >= 65000 && 60 * cfg.yaw_rate <= 66100,
		"full deflection takes the camera once round in about 2 s", 60 * cfg.yaw_rate, 65536);
	ok(60 * cfg.pitch_rate >= 32400 && 60 * cfg.pitch_rate <= 33100,
		"...and half a turn of pitch in the same 2 s", 60 * cfg.pitch_rate, 32768);
	ok(cfg.pitch_max == 15470 && cfg.pitch_min == -15470,
		"the pitch clamp is ChaseCamera's own (camera.cpp:1012)", cfg.pitch_max, 15470);
	ok(cfg.pitch_home == -1820,
		"the resting elevation is ChaseCamera's own (camera.cpp:1006)", cfg.pitch_home, -1820);
	ok(cfg.hide_dist == 20 + 102,
		"hiding starts at the near plane (20) plus her own radius (102)", cfg.hide_dist, 122);

	/* 1. THE STICK MOVES THE OFFSET BY THE RATE. "Es ist, als waere die Kamera
	 * eine Kugel um Lara herum" - the axis IS the rate, so half a push is half
	 * the speed and a diagonal cannot buy more than a full one. */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)cfg.yaw_rate, "full right: one rate's worth of yaw", yaw, cfg.yaw_rate);
	ok(st.steering == 1, "...and the orbit knows the player is steering", st.steering, 1);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, -128, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)-cfg.yaw_rate, "full left: the other way, the same rate", yaw, -cfg.yaw_rate);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 64, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)(cfg.yaw_rate / 2), "half deflection is half the rate",
		yaw, cfg.yaw_rate / 2);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 181, 0, 0, 0, &yaw, &pitch);	/* a diagonal reads 181 */
	ok(yaw == (short)cfg.yaw_rate, "a diagonal cannot buy more than full deflection",
		yaw, cfg.yaw_rate);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, &yaw, &pitch);
	ok(yaw == 0, "a stick at rest moves nothing", yaw, 0);
	ok(st.steering == 0, "...and is not steering", st.steering, 0);

	/* 360 degrees on the sphere: the yaw wraps, it does not clamp. */
	ModernCameraReset(&st);

	for (lp = 0; lp < 60; lp++)
		ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, &yaw, &pitch);

	ok(absl(yaw) <= 32, "60 ticks of full right is one whole turn, back behind her", yaw, 0);

	/* 2. STANDING, THE OFFSET IS KEPT. "sie behaelt immer ihre Position, an der
	 * ich sie auf der Kugel verschoben habe". */
	ModernCameraReset(&st);

	for (lp = 0; lp < 15; lp++)
		ModernCameraUpdate(&cfg, &st, 128, 0, 0, 0, &yaw, &pitch);

	ok(yaw == (short)(15 * cfg.yaw_rate), "a quarter turn to the right",
		yaw, 15 * cfg.yaw_rate);

	for (lp = 0; lp < 300; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, &yaw, &pitch);

	ok(yaw == (short)(15 * cfg.yaw_rate), "ten seconds of standing: exactly where he left it",
		yaw, 15 * cfg.yaw_rate);
	ok(st.ret == 0, "...and the return never ran", st.ret, 0);

	/* 3. RUNNING, IT FALLS BACK - "ganz leicht und super smooth", 377 parts of
	 * 65536 of what is left, every tick. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);
	ok(st.ret == -94, "running: a 90 degree offset falls by 377/65536 of itself", st.ret, -94);
	ok(yaw == 0x4000 - 94, "...towards zero, the short way round", yaw, 0x4000 - 94);

	ModernCameraReset(&st);
	st.offset_yaw = (short)-0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);
	ok(st.ret == 94, "an offset to the left falls the same way, mirrored", st.ret, 94);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;

	for (ticks = 0; ticks < 1000 && absl(st.offset_yaw) > 0x2000; ticks++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);

	printf("  measured: 90 degrees of offset halve in %ld ticks of running (%.2f s)\n",
		ticks, (double)ticks / 30.0);
	ok(ticks >= 115 && ticks <= 128, "the documented half life, about 4 s of running",
		ticks, 120);

	for (lp = 0; lp < 3000 && st.offset_yaw; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);

	printf("  measured: and reach her back %ld ticks later (%.2f s)\n", lp, (double)lp / 30.0);
	ok(st.offset_yaw == 0, "it really arrives behind her - that is what the floor of the step is for",
		st.offset_yaw, 0);

	/* The return stands still while the player is steering: he turned the
	 * camera and THEN ran, which is the case the user described; a camera that
	 * pulls against the thumb holding it is a camera fighting the player. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 1, 0, 1, 0, &yaw, &pitch);
	ok(st.ret == 0, "one unit of stick freezes the return", st.ret, 0);
	ok(yaw == 0x4000 + 8, "...and only the stick moves the camera then", yaw, 0x4000 + 8);

	/* The reset, and what "zeroed" means for each of the two angles. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x1234;
	st.offset_pitch = 4321;
	st.wall_push = 99;
	st.wall = 1;
	st.ret = 5;
	st.steering = 1;
	ModernCameraReset(&st);
	ok(st.offset_yaw == 0, "the reset puts her camera behind her back", st.offset_yaw, 0);
	ok(st.offset_pitch == -1820,
		"...at the classic resting elevation, not at dead level", st.offset_pitch, -1820);
	ok(!st.wall_push && !st.wall && !st.ret && !st.steering, "...and forgets the wall",
		st.wall_push + st.wall + st.ret + st.steering, 0);
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

	/* WHICH WAY IS UP is not taste here: the classic look camera answered it
	 * once and for all. LookUpDown (game\lara.cpp) turns IN_FORWARD into
	 * head_x_rot -= 364, and a negative x_rot is looking up; looking up from an
	 * orbit means the camera sits BELOW her, which is a positive elevation. */
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, &yaw, &pitch);
	ok(pitch == (short)(cfg.pitch_home + cfg.pitch_rate),
		"stick forward looks up, the way the look camera does",
		pitch, cfg.pitch_home + cfg.pitch_rate);

	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, &yaw, &pitch);
	ok(pitch == (short)(cfg.pitch_home - cfg.pitch_rate), "pulling back looks down",
		pitch, cfg.pitch_home - cfg.pitch_rate);

	cfg.invert_pitch = 1;
	ModernCameraReset(&st);
	ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, &yaw, &pitch);
	ok(pitch == (short)(cfg.pitch_home - cfg.pitch_rate),
		"the switch turns that axis round and nothing else",
		pitch, cfg.pitch_home - cfg.pitch_rate);
	cfg.invert_pitch = 0;

	/* The clamp, both ends, and the point of it: never over the pole. */
	ModernCameraReset(&st);

	for (lp = 0; lp < 200; lp++)
		ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, &yaw, &pitch);

	ok(pitch == (short)cfg.pitch_max, "200 ticks up: it stops at the camera's own 84.98 degrees",
		pitch, cfg.pitch_max);

	ModernCameraUpdate(&cfg, &st, 0, -128, 0, 0, &yaw, &pitch);
	ok(pitch == (short)cfg.pitch_max, "...and stays there instead of wrapping over the pole",
		pitch, cfg.pitch_max);

	ModernCameraReset(&st);

	for (lp = 0; lp < 200; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, &yaw, &pitch);

	ok(pitch == (short)cfg.pitch_min, "200 ticks down: the same, under her",
		pitch, cfg.pitch_min);

	ModernCameraUpdate(&cfg, &st, 0, 128, 0, 0, &yaw, &pitch);
	ok(pitch == (short)cfg.pitch_min, "...and no wrap there either", pitch, cfg.pitch_min);

	/* The height does NOT come back on its own. He described the yaw
	 * returning and said nothing about the height, and observation 2 says what
	 * he did not ask for stays where he put it. The capability exists and is
	 * proved here under an explicit cfg; it is simply not what ships. */
	ModernCameraReset(&st);
	st.offset_pitch = (short)cfg.pitch_max;

	for (lp = 0; lp < 300; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);

	ok(pitch == (short)cfg.pitch_max, "running does not bring the height back",
		pitch, cfg.pitch_max);

	cfg.pitch_return = 1;
	ModernCameraReset(&st);
	st.offset_pitch = (short)cfg.pitch_max;
	ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);
	ok(pitch == 15371, "pitch_return 1: it falls towards the resting elevation at the same rate",
		pitch, 15371);

	for (lp = 0; lp < 3000 && st.offset_pitch != (short)cfg.pitch_home; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);

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

	printf("the wall - the offset takes its angle, and falls home faster while it lasts\n");

	ModernCameraDefaults(&cfg);

	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0x0100, &yaw, &pitch);
	ok(st.wall == 0, "a displacement at the noise floor is arithmetic, not a wall", st.wall, 0);
	ok(yaw == 0x2000, "...and moves nothing", yaw, 0x2000);

	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0x1000, &yaw, &pitch);
	ok(st.wall == 1, "a real displacement is a wall", st.wall, 1);
	ok(yaw == 0x3000, "...and the offset takes the wall's angle", yaw, 0x3000);
	ok(st.wall_push == 0x1000, "...which the log can read back afterwards", st.wall_push, 0x1000);

	/* Standing, a wall is the ONE thing that may move the camera - the
	 * exception written into observation 2 itself. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, -0x2000, &yaw, &pitch);
	ok(yaw == 0, "standing at a wall: moved by exactly the push, and by nothing else", yaw, 0);

	/* THE RATE, ISOLATED. follow_wall off, so that the two runs below start
	 * from the same offset and differ in one thing only: whether a wall is
	 * pushing. With it on, the push would move the offset first and the two
	 * steps would no longer be comparable. */
	cfg.follow_wall = 0;

	ModernCameraReset(&st);
	st.offset_yaw = 0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0x1000, &yaw, &pitch);
	ok(yaw == 0x2000, "follow_wall 0: the offset ignores the displacement", yaw, 0x2000);
	ok(st.wall == 1, "...but the wall is still seen", st.wall, 1);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);
	free_ret = st.ret;

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0x1000, &yaw, &pitch);
	wall_ret = st.ret;

	ok(wall_ret == -754, "at a wall the same offset falls eight times as far in one tick",
		wall_ret, -754);
	ok(absl(wall_ret) > absl(free_ret) * 7,
		"...eight being the factor, not two numbers that happen to differ",
		absl(wall_ret), absl(free_ret) * 7);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;

	for (lp = 0; lp < 30; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0, &yaw, &pitch);

	free_left = absl(st.offset_yaw);

	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;

	for (lp = 0; lp < 30; lp++)
		ModernCameraUpdate(&cfg, &st, 0, 0, 1, 0x1000, &yaw, &pitch);

	wall_left = absl(st.offset_yaw);
	printf("  measured: one second of running from 90 degrees leaves %ld units in the open, "
		"%ld at a wall\n", free_left, wall_left);
	ok(wall_left * 2 < free_left,
		"a second at a wall is most of the way home; a second in the open is a nudge",
		wall_left, free_left);

	cfg.follow_wall = 1;

	/* And the two together, which is what the user actually sees: the wall
	 * takes the camera to its own angle AND she comes home from there fast. */
	ModernCameraReset(&st);
	st.offset_yaw = 0x4000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 1, -0x1000, &yaw, &pitch);
	ok(yaw == 0x3000 - 565, "wall and run together: the wall's angle first, then the fast fall",
		yaw, 0x3000 - 565);
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

/* THE SEAM'S OWN ARITHMETIC (#161, round 161a2, step 2). Everything the engine
 * side does that is not a call into the engine, checked here so that the two
 * identities the seam RESTS on are numbers and not paragraphs.
 *
 * The seam itself is game\camera.cpp and cannot be linked into this tool - it
 * needs a camera, a Lara and a level. What it computes, though, is two lines
 * of angle arithmetic, and both of them can go wrong quietly:
 *
 *   camera.actual_angle = lara_item->pos.y_rot + camera.target_angle
 *                                          (UpdateCameraElevation, :855)
 *   modern_cam_push     = phd_atan(dz, dx) - camera.actual_angle
 *                                          (ModernCameraNoteWall)
 *
 * The first adds two shorts into an int and assigns it back to a short; the
 * second subtracts a short from phd_atan's return, which is 0..65535 and not
 * a short at all. Both are handed to the orbit, which folds them with
 * ModernWrap - and THAT is the claim under test. */
static void camera_seam()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_STATE st;
	short yaw;
	short pitch;
	long heading;
	long offset;
	long resolved;
	long push;
	long absolute;

	printf("the seam - the two angle lines game\\camera.cpp adds around the orbit\n");

	ModernCameraDefaults(&cfg);

	/* 1. THE ABSOLUTE ANGLE IS THE OFFSET'S OWN WRAP. The engine writes the
	 * offset into camera.target_angle and lets UpdateCameraElevation add her
	 * heading to it in a short. If that truncation were not ModernWrap, an
	 * offset behind a heading near half a turn would point the camera
	 * somewhere else entirely - and this is the compiler the engine is built
	 * with, so the answer here is the answer there. */
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

	/* 2. THE WALL ABSORPTION REALLY NAMES WHERE THE CAMERA IS, which is the
	 * whole reason the displacement can be taken into the offset without a
	 * feedback loop: after it, the absolute angle IS the angle geometry left
	 * the camera at, so the next tick asks for an angle nothing blocks and the
	 * displacement comes back as zero.
	 *
	 * `resolved` is phd_atan's own range, 0..65535, and `push` is the raw
	 * difference the seam stores - up to 98303 or down to -32768, which is
	 * exactly the value no cast to short may touch on the way. Standing, so
	 * that nothing but the wall moves the offset. */
	for (heading = -32768; heading < 32768; heading += 4093)
	{
		for (resolved = 0; resolved < 65536; resolved += 2731)
		{
			ModernCameraReset(&st);
			st.offset_yaw = (short)0x2000;
			offset = st.offset_yaw;
			push = resolved - (long)(short)(heading + offset);

			ModernCameraUpdate(&cfg, &st, 0, 0, 0, push, &yaw, &pitch);

			absolute = (short)(heading + yaw);

			/* Under the wall's noise floor the orbit is right to ignore it
			 * and the camera stays where the stick left it - the one case
			 * this identity does not claim, and 1.4 degrees of it. */
			if (absl(ModernWrap(push)) <= cfg.wall_dead)
				ok(yaw == (short)offset, "a displacement at the noise floor moves nothing",
					yaw, offset);
			else
				ok(absolute == ModernWrap(resolved),
					"after the wall the offset names the angle the camera is really at",
					absolute, ModernWrap(resolved));
		}
	}

	/* 3. AND THEN THE WALL IS QUIET. The second tick of the same wall: the
	 * seam hands in 0 because the angle it asked for is the angle it got, and
	 * the offset stands still (standing - running is where the fast return
	 * takes over, which camera_wall above measures). */
	ModernCameraReset(&st);
	st.offset_yaw = (short)0x2000;
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0x1000, &yaw, &pitch);
	ok(yaw == (short)0x3000, "the wall's tick: the offset takes its angle", yaw, 0x3000);
	ModernCameraUpdate(&cfg, &st, 0, 0, 0, 0, &yaw, &pitch);
	ok(yaw == (short)0x3000, "the next tick: nothing is in the way any more", yaw, 0x3000);
	ok(st.wall == 0, "...and the orbit says so", st.wall, 0);
}

/* THE 60 FPS CUT DETECTION MUST NOT FIRE ON THE ORBIT'S OWN STEPS - the
 * round's own rule, and the reason this group includes game\interpolate_math.h
 * rather than quoting a threshold out of it. Both sides of the comparison are
 * therefore the tree's: the orbit's rates come from ModernCameraDefaults and
 * the rule comes from InterpCamAimCut and INTERP_CAM_SNAP.
 *
 * WHAT IS BEING MEASURED. The chase camera's ideal eye sits at
 *
 *     x = target.x - 1536 * cos(elevation) * sin(angle)
 *     y = target.y + 1536 * sin(elevation)
 *     z = target.z - 1536 * cos(elevation) * cos(angle)
 *
 * (ChaseCamera, camera.cpp:1017-1052; 1536 is camera.target_distance,
 * camera.cpp:258). One tick of a FULLY deflected right stick moves `angle` by
 * yaw_rate and `elevation` by pitch_rate, so the worst case is the largest
 * step that pair can make anywhere on the sphere, and it is swept rather than
 * argued.
 *
 * THE REAL EYE MOVES THIS MUCH AND NOT LESS, and that corrects step 1's own
 * note, which said MoveCamera's damping makes it smaller: `pos += (ideal -
 * pos) / speed` is a first order lag, and a lag filter fed a constant
 * VELOCITY settles at that same velocity. It is smaller for the first few
 * ticks of a push and equal for as long as the push lasts. The margin below
 * is therefore the whole margin. */
static void camera_camcut()
{
	MODERN_CAMERA_CFG cfg;
	const double turn = 2.0 * 3.14159265358979323846 / 65536.0;
	const double radius = 1536.0;		// camera.target_distance, camera.cpp:258
	long worst_eye = 0;
	long worst_aim = 0;
	long cuts = 0;
	long elev;
	long ang;

	printf("the camera cut - the orbit's step against the interpolator's own threshold\n");

	ModernCameraDefaults(&cfg);

	for (elev = cfg.pitch_min; elev <= cfg.pitch_max; elev += 128)
	{
		long elev2 = elev + cfg.pitch_rate;

		if (elev2 > cfg.pitch_max)
			elev2 = cfg.pitch_max;

		for (ang = 0; ang < 65536; ang += 128)
		{
			const long ang2 = ang + cfg.yaw_rate;
			const double r1 = radius * cos((double)elev * turn);
			const double r2 = radius * cos((double)elev2 * turn);
			const long x1 = roundl2(-r1 * sin((double)ang * turn));
			const long z1 = roundl2(-r1 * cos((double)ang * turn));
			const long y1 = roundl2(radius * sin((double)elev * turn));
			const long x2 = roundl2(-r2 * sin((double)ang2 * turn));
			const long z2 = roundl2(-r2 * cos((double)ang2 * turn));
			const long y2 = roundl2(radius * sin((double)elev2 * turn));
			const long eye = InterpManhattan(x2 - x1, y2 - y1, z2 - z1);

			if (eye > worst_eye)
				worst_eye = eye;

			/* And the view DIRECTION, by the interpolator's own rule. The
			 * target is Lara and the orbit does not move her, so it stands at
			 * the origin on both sides: what is left is the eye's own travel
			 * measured as an aim, and at a radius of 1536 - under
			 * INTERP_CAM_FAR_TARGET - that rule is the absolute one. */
			if (InterpCamAimCut(x1, y1, z1, 0, 0, 0, x2, y2, z2, 0, 0, 0, INTERP_CAM_SNAP))
				cuts++;

			if (eye > worst_aim)
				worst_aim = eye;
		}
	}

	printf("  measured: the worst tick of a fully pushed right stick moves the ideal eye\n");
	printf("            %ld units (Manhattan) against INTERP_CAM_SNAP %ld - a margin of %.1f\n",
		worst_eye, (long)INTERP_CAM_SNAP, (double)INTERP_CAM_SNAP / (double)worst_eye);

	ok(worst_eye < INTERP_CAM_SNAP,
		"the orbit's own step never trips the eye test", worst_eye, INTERP_CAM_SNAP);
	ok(worst_eye * 2 < INTERP_CAM_SNAP,
		"...and not by a hair: twice the step is still under it", worst_eye * 2,
		INTERP_CAM_SNAP);
	ok(cuts == 0, "nor the view direction test, anywhere on the sphere", cuts, 0);

	/* THE ONE THING THAT STILL CUTS AT A WALL IS VANILLA'S, and it is worth a
	 * number so that nobody hunts for it in the orbit: the five candidate
	 * positions of ChaseCamera are the wanted angle and the four world
	 * cardinals, so the ideal can jump a whole quadrant in one tick. That is
	 * the classic camera's behaviour, it is there at Tank today, and the orbit
	 * does not add to it - it ABSORBS it, which is what camera_seam's second
	 * identity is about. */
	{
		const long x1 = roundl2(-radius * sin(0.0));
		const long z1 = roundl2(-radius * cos(0.0));
		const long x2 = roundl2(-radius * sin(16384.0 * turn));
		const long z2 = roundl2(-radius * cos(16384.0 * turn));
		const long jump = InterpManhattan(x2 - x1, 0, z2 - z1);

		printf("  measured: the classic quadrant snap moves it %ld - vanilla's own cut,\n", jump);
		printf("            and the orbit takes that angle instead of fighting it\n");
		ok(jump > INTERP_CAM_SNAP, "which is why it is a cut and ours is not",
			jump, INTERP_CAM_SNAP);
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
	printf("  none    modern.orbit_stays_in_world_space_and_accepts_mouse_while_running\n");
	printf("          - the half that IS ours is checked above (the orbit): the stick moves the\n");
	printf("            camera while she runs. The world-space half is not our model and will\n");
	printf("            not become it - the user's camera holds an offset from HER heading and\n");
	printf("            lets it fall back behind her (game\\modern_camera.h, observation 3),\n");
	printf("            where TROne's holds a world angle that never returns. The engine side\n");
	printf("            landed in round 161a2 step 2 (game\\camera.cpp); what can be linked of\n");
	printf("            it without a level is checked above as `the seam'.\n");
	printf("  B       combatmode.toggle_pulses_once_per_press_in_the_modern_profile (the edge)\n");
	printf("  B       modern.held_equip_draws_once_and_interaction_does_not_fire (IN_ACTION2)\n");
}

int main()
{
	printf("ModernTest - the arithmetic of the modern control (#161, round 161a)\n");
	printf("built against TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "modern_move.cpp\n");
	printf("                  and TOMB5 Code" "\\" "TOMB5" "\\" "game" "\\" "modern_camera.cpp\n");
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
	water_move();
	back_input();
	chatter();
	blocked();
	direct_turn();
	weapons();
	sweeps();
	camera_orbit();
	camera_pitch();
	camera_wall();
	camera_hide();
	camera_seam();
	camera_camcut();
	deferred();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

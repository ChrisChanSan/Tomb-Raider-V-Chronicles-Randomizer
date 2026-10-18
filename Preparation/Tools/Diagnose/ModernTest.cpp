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
// The HD/SD switch's press rule, pure (hd_switch below).
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_switch.h"
/* The 60 fps cut detection's own thresholds and its own rule, so that
 * camera_camcut() measures the orbit against the tree rather than against a
 * number copied out of it. It costs nothing: this header includes stdlib.h
 * and math.h and no engine at all, which is why InterpTest is built against
 * it the same way. */
#include "../../../TOMB5 Code/TOMB5/game/interpolate_math.h"
/* The HD room renderer's page class and the mirror's arithmetic, both
 * headers of pure functions that include no engine header - the same
 * arrangement as the one above. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_pageclass.h"
#include "../../../TOMB5 Code/TOMB5/game/mirror_math.h"
/* Where a flyby makes its heavy-trigger test - pure as well (spotcam_end
 * below). */
#include "../../../TOMB5 Code/TOMB5/game/spotcam_math.h"
/* Which opaque triangles of an HD object are cut out - pure as well
 * (model_cut below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_model_cut.h"
/* The sheen's arithmetic, its page table and its light field - pure again:
 * which surfaces shine, the override file's reader, the bound, and the
 * field's grid, texels, build and sample (gloss_* below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_gloss_pages.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_gloss_field.h"
/* Which of the engine's item lights give an HD object nothing - pure as well
 * (objlight_dark below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_objlight_dark.h"
/* The soft motion of the chest: the region finder, the mask, the spring, its
 * clock and the draw constants - pure again (softbody_* below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_softbody.h"
/* The projected shadow: the key light, its following in time, the box and
 * the map mapping, and the gate - pure again (shadow_* below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_shadow.h"
/* A classic room and another group's art in one place: which art stands on
 * a face, whether it covers the room, and which of the two a frame draws -
 * pure again (stand_* below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_cover.h"
// A broken static's pieces in HD: the face rule and the blend.
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_debris_rules.h"
// The visibility path's tables (hd_vis.cpp is linked by bmt.bat).
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_vis.h"
#include "../../../TOMB5 Code/TOMB5/vendor/trforge/src/trf_remaster/Trg.h"
/* The water's life: the drift, the bank, the spawn, the flight and the
 * bob - pure again (life_* below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_life_rules.h"
// The level load's pool and the build's FNV-1a 64 step, header-only - the
// group load_pool.
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_pagecache.h"
/* The water's cards: a dive splash, a fish tank's spray and the wet film
 * on its floor as functions of their age alone (splash_* below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_splash_math.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_marble_math.h"
// The water seen from below: the eye's depth, Snell's window (water_under).
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_water_under.h"
// The sky band's closed poles (sky_cap).
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_sky_cap.h"
// The springs: the table, the file's key, the hashed clock (spring_*).
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_spring.h"
/* The level's HD key: which load of a level keeps the HD data of the one
 * before (level_key below). */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_levelkey.h"
// The loading screen's rules: the row's state and the fade (load_mode).
#include "../../../TOMB5 Code/TOMB5/specific/loadbar_math.h"
// The far plane and the depth curve: the numbers (far_plane).
#include "../../../TOMB5 Code/TOMB5/specific/zrange_math.h"
#include <string.h>
#include <algorithm>
#include <chrono>
#include <new>
#include <vector>

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
		 * 5461 is 30 degrees a tick, the top of the thumb's measured 15 to
		 * 30: a running half turn in six ticks. */
		ok(cfg.turn_rate == 5461, "the direct turn's rate, 30 deg/tick",
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
		 * one-word edit. The default is 5461, the measured thumb's top; 3277 is
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

	/* 7. And the player's own per cent on top of all eight: a slider the
	 * player can switch off, over the tuned zone widths.
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
	 * under her, tracking it exactly as closely as an every-tick read of
	 * the camera would for as long as the thumb keeps moving. */
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
	sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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
	 * TROne's own line that disarms aiming on a jump is in
	 * trone_modern_glue.h, trone_modern_glue_situation (TROne-main, copy of
	 * 2026-10-05), outside a profile block of TROne's that
	 * forces aim_strafe off, and their own test builds a plain
	 * modern_test_glue() that never sets that profile - so their
	 * expectation is unconditional. This project's choice: armed, the side
	 * flip stands as TROne's; unarmed, it is rule 6 (the run branch) instead
	 * - turn onto the course, IN_FORWARD, and a forward jump rather than a
	 * flip. So TROne's one case is now two of ours. */
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
	sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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

	/* TROne: its test of the back input that tells running from the armed
	 * hop back (tests/test_controller.cpp) rides on this switch - a profile
	 * of TROne's sets back_turns from "is a weapon held". Ours is a constant
	 * (MODERN_BACK_TURNS), because this engine has no pointer aiming to hop
	 * away from; the switch is here for a profile that aims with a pointer,
	 * which this engine does not have. The
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
	sit.cam_cut = 0;

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
	sit.cam_cut = 0;
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
	sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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
		sit.cam_cut = 0;

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
		sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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
	 * 6371, and 5461 is the top of the thumb's measured 15 to 30. It is
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
	sit.cam_cut = 0;

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

/* ------------------------------------------ the four keys as the stick ----- */

/* Two translations from a fresh state each, one tick: whether they agree on
 * everything the seam and the direct turn read. */
static long same_result(const MODERN_RESULT* a, const MODERN_RESULT* b)
{
	return (a->bits == b->bits && a->active == b->active && a->target == b->target &&
		a->delta == b->delta && a->mode == b->mode) ? 1 : 0;
}

static void fresh_tick(long x, long y, short camera_yaw, short lara_yaw, MODERN_RESULT* out)
{
	MODERN_CFG cfg;
	MODERN_STATE state;

	ModernDefaults(&cfg);
	ModernResetState(&state);
	ModernTranslate(&cfg, &state, x, y, camera_yaw, lara_yaw, 0, out);
}

/* specific\input.cpp's guard chain round the key stick, modelled in the order
 * the file has it, for the same reason and with the same limit as look_tick()
 * above: bmt.bat does not compile that file, so this model is read against it
 * whenever either changes. Returns whether ModernKeyStick is consulted, and
 * the pair the translation is handed when it is.
 *
 *   move_x/move_y   joy_x/joy_y; under Modern outside a menu 0/0, and the
 *                   pad's zoned stick only on the pad page (`pad_x/pad_y`
 *                   stand for that zoned reading)
 *   modern_seam     control_modern && in play (first person, spotcam,
 *                   cutscene, bDisableLaraControl, inventory, title all no)
 *   modern_tank     modern_seam && the look key
 *   the call        modern_seam && !modern_tank
 *
 * `in_play` stands for all of the seam's game-state terms at once. The menu
 * term, which also decides the zeroing of move_x/move_y, is one of them; the
 * other terms leave that zeroing alone, which the model does not show and
 * does not need to, because the call is not reached when any of them says
 * no. */
static long key_seam_model(long control_modern, long pad_page, long in_play, long look,
	long joy_x, long joy_y, long pad_x, long pad_y, long keys, long* x, long* y)
{
	long move_x = joy_x;
	long move_y = joy_y;
	long seam, tank;

	*x = 0;
	*y = 0;

	if (control_modern && in_play)
	{
		move_x = 0;
		move_y = 0;

		if (pad_page)
		{
			move_x = pad_x;
			move_y = pad_y;
		}
	}

	seam = (control_modern && in_play) ? 1 : 0;
	tank = (seam && look) ? 1 : 0;

	if (!(seam && !tank))
		return 0;

	ModernKeyStick(move_x, move_y, keys & 1, keys & 2, keys & 4, keys & 8, x, y);
	return 1;
}

static void key_stick()
{
	const long full = (long)MODERN_STICK_FULL;
	MODERN_RESULT a, b;
	long x, y, sx, sy, feed, bad, k, c, l;

	printf("\nthe four movement keys as the modern stick\n");

	/* 1. The sign. A stick pushed straight forward reads y negative, in the
	 * scheme's own helper and at the pad's dead zone alike; the forward key
	 * must read the same, or W would run her towards the camera. */
	stick_at(0.0, full, &sx, &sy);
	feed = ModernKeyStick(0, 0, 1, 0, 0, 0, &x, &y);
	ok(feed == MODERN_FEED_KEYS, "the forward key alone feeds the seam from the keys",
		feed, MODERN_FEED_KEYS);
	ok(x == 0 && y == -full, "...as 0/-MODERN_STICK_FULL, full deflection forward", y, -full);
	ok(y == sy && x == sx, "...the very pair of a stick pushed straight forward", y, sy);
	ModernStickDeadZone(MODERN_DEAD_LEFT, MODERN_DEAD_AXIS_LEFT, 0, -full, &sx, &sy);
	ok(y == sy && x == sx, "...and of the pad's rim after its dead zone", y, sy);

	/* 2. Forward key against the pad at full forward, through the whole
	 * translation: the bits, the course and the mode. Both the rim itself
	 * and the XInput reading of 32767 (-127, one short of the rim). */
	fresh_tick(0, -full, 0, 0, &a);
	fresh_tick(sx, sy, 0, 0, &b);
	ok(same_result(&a, &b) && a.mag == b.mag,
		"the forward key gives the bits the pad gives at full forward", a.bits, b.bits);
	ok(a.active && (a.bits & MODERN_IN_FORWARD) && !(a.bits & MODERN_IN_WALK),
		"...forward and running: no key is half a deflection", a.bits, MODERN_IN_FORWARD);
	ModernStickDeadZone(MODERN_DEAD_LEFT, MODERN_DEAD_AXIS_LEFT, 0, -127, &sx, &sy);
	fresh_tick(sx, sy, 0, 0, &b);
	ok(a.bits == b.bits && a.target == b.target && a.mode == b.mode,
		"...and the bits of the pad's real full push, which reads 127", b.bits, a.bits);

	/* 3. Every key and every pair of neighbours against the stick at the same
	 * heading, under 16 cameras and 8 headings of hers: the keys are the stick,
	 * nothing of their own. Left is -90, right +90, back 180. */
	{
		static const long combo[8] = { 1, 1 | 8, 8, 2 | 8, 2, 2 | 4, 4, 1 | 4 };
		static const double deg[8] = { 0.0, 45.0, 90.0, 135.0, 180.0, -135.0, -90.0, -45.0 };

		bad = 0;

		for (k = 0; k < 8; k++)
		{
			ModernKeyStick(0, 0, combo[k] & 1, combo[k] & 2, combo[k] & 4, combo[k] & 8, &x, &y);
			stick_at(deg[k], full, &sx, &sy);

			for (c = 0; c < 16; c++)
			{
				for (l = 0; l < 8; l++)
				{
					fresh_tick(x, y, (short)(c * 4096), (short)(l * 8192), &a);
					fresh_tick(sx, sy, (short)(c * 4096), (short)(l * 8192), &b);

					if (!same_result(&a, &b) || a.mag != full)
						bad++;
				}
			}
		}

		ok(bad == 0, "W/S/A/D and their diagonals translate as the stick at that heading",
			bad, 0);
		printf("  8 key sets x 16 cameras x 8 headings of hers, %ld differ\n", bad);
	}

	/* 4. The diagonal. Two full axes are 181 long; the magnitude is clamped to
	 * full, and the course is exactly 45 degrees off the camera - 0x2000, not
	 * one unit either side - for all four. */
	feed = ModernKeyStick(0, 0, 1, 0, 0, 1, &x, &y);
	ok(feed == MODERN_FEED_KEYS && x == full && y == -full,
		"forward and right together are both axes at full", x, full);
	fresh_tick(x, y, 0, 0, &a);
	ok(a.mag == full, "...the diagonal reads MODERN_STICK_FULL, not 181", a.mag, full);
	ok(a.target == 0x2000, "...and her course is exactly 45 degrees right of the camera",
		a.target, 0x2000);
	fresh_tick(x, y, 0x3000, 0x3000, &a);
	ok(a.target == 0x5000, "...under any camera: 45 degrees right of 0x3000 is 0x5000",
		a.target, 0x5000);
	{
		static const long diag[4] = { 1 | 8, 2 | 8, 2 | 4, 1 | 4 };
		static const long want[4] = { 0x2000, 0x6000, -0x6000, -0x2000 };

		bad = 0;

		for (k = 0; k < 4; k++)
		{
			ModernKeyStick(0, 0, diag[k] & 1, diag[k] & 2, diag[k] & 4, diag[k] & 8, &x, &y);
			fresh_tick(x, y, 0, 0, &a);

			if (a.target != want[k] || a.mag != full)
				bad++;
		}

		ok(bad == 0, "...and all four diagonals lie on the exact 45 degree lines", bad, 0);
	}

	/* 5. The pad wins. Any pad pair off centre - a nudge of one unit, a
	 * partial push, full lock - is handed on unchanged whatever keys are
	 * held, and never summed with them. */
	{
		static const long pad[6][2] = { { 0, -1 }, { 1, 0 }, { 50, -30 }, { -full, 0 },
			{ 0, full }, { -90, 90 } };
		long p;

		bad = 0;

		for (p = 0; p < 6; p++)
		{
			for (k = 0; k < 16; k++)
			{
				feed = ModernKeyStick(pad[p][0], pad[p][1], k & 1, k & 2, k & 4, k & 8, &x, &y);

				if (feed != MODERN_FEED_PAD || x != pad[p][0] || y != pad[p][1])
					bad++;
			}
		}

		ok(bad == 0, "a pad off centre is handed on unchanged under every set of keys",
			bad, 0);
	}

	/* 6. Both at rest: nothing, and the scheme steps aside exactly as it does
	 * for a resting stick - the classic bits the keys did not write stand. */
	feed = ModernKeyStick(0, 0, 0, 0, 0, 0, &x, &y);
	ok(feed == MODERN_FEED_NONE && !x && !y, "pad at rest and no key: nothing is fed",
		feed, MODERN_FEED_NONE);
	fresh_tick(x, y, 0, 0, &a);
	ok(!a.active && !a.bits, "...and the translation steps aside", a.active, 0);

	/* 7. Opposite keys cancel on their axis, and only there. */
	feed = ModernKeyStick(0, 0, 1, 1, 0, 0, &x, &y);
	ok(feed == MODERN_FEED_NONE && !x && !y, "forward and back cancel to nothing", y, 0);
	feed = ModernKeyStick(0, 0, 0, 0, 1, 1, &x, &y);
	ok(feed == MODERN_FEED_NONE && !x && !y, "left and right cancel to nothing", x, 0);
	feed = ModernKeyStick(0, 0, 1, 1, 1, 1, &x, &y);
	ok(feed == MODERN_FEED_NONE && !x && !y, "all four cancel to nothing", feed,
		MODERN_FEED_NONE);
	feed = ModernKeyStick(0, 0, 1, 1, 0, 1, &x, &y);
	ok(feed == MODERN_FEED_KEYS && x == full && !y,
		"forward and back with right leave a pure right", x, full);

	/* 8. All sixteen sets: each axis is 0 or full, and the keys feed exactly
	 * when the vector is not zero. */
	bad = 0;

	for (k = 0; k < 16; k++)
	{
		feed = ModernKeyStick(0, 0, k & 1, k & 2, k & 4, k & 8, &x, &y);

		if ((x != 0 && x != full && x != -full) || (y != 0 && y != full && y != -full))
			bad++;

		if ((feed == MODERN_FEED_KEYS) != (x != 0 || y != 0))
			bad++;

		if (feed == MODERN_FEED_PAD)
			bad++;
	}

	ok(bad == 0, "every set of keys is a full-deflection axis pair or nothing", bad, 0);
	y = 12345;
	ok(ModernKeyStick(0, 0, 1, 0, 0, 0, 0, &y) == MODERN_FEED_NONE && y == 12345,
		"a missing out pointer is refused, not written", y, 12345);

	/* 9. The guard chain, modelled (see key_seam_model). Tank never consults
	 * the key stick, in or out of play, with or without the look key; nor
	 * does Modern while the look key steers her or outside play. On the
	 * keyboard page a d-pad press in joy_x/joy_y (+-20) is no movement: the
	 * keys alone are fed. */
	{
		long cm, pp, ip, lk, used, calls = 0;

		bad = 0;

		for (cm = 0; cm < 2; cm++)
			for (pp = 0; pp < 2; pp++)
				for (ip = 0; ip < 2; ip++)
					for (lk = 0; lk < 2; lk++)
						for (k = 0; k < 16; k++)
						{
							used = key_seam_model(cm, pp, ip, lk, 20, -20, 0, 0, k, &x, &y);

							if (used)
								calls++;

							if (used != (cm && ip && !lk))
								bad++;
						}

		ok(bad == 0, "the key stick is consulted only under Modern, in play, without look",
			bad, 0);
		ok(calls == 2 * 16, "...never at Tank: every consultation is a Modern one", calls,
			2 * 16);

		key_seam_model(1, 0, 1, 0, 20, -20, 0, 0, 1, &x, &y);
		ok(x == 0 && y == -full, "on the keyboard page a d-pad press does not steer: W alone",
			y, -full);
		key_seam_model(1, 0, 1, 0, 20, -20, 0, 0, 0, &x, &y);
		ok(x == 0 && y == 0, "...and with no key the keyboard page feeds nothing", x, 0);
		key_seam_model(1, 1, 1, 0, 0, 0, 40, -60, 1, &x, &y);
		ok(x == 40 && y == -60, "on the pad page the stick off centre beats W", y, -60);
	}
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
	sit.cam_cut = 0;

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
		sit.cam_cut = 0;

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
	sit.cam_cut = 0;

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
 * classic_states() does it for ModernStateIsClassic: every index 0..139, so a
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
			111, 112, 113, 114, 115, 128, 139
		};
		unsigned char want[140];

		for (i = 0; i < 140; i++)
			want[i] = 0;

		for (i = 0; i < (int)(sizeof(grab) / sizeof(grab[0])); i++)
			want[grab[i]] = 1;

		for (i = 0; i <= 139; i++)
			ok(ModernStateIsGrab((short)i) == want[i],
				"all 140 states: the hold-on family matches the reference",
				ModernStateIsGrab((short)i), want[i]);

		ok((int)(sizeof(grab) / sizeof(grab[0])) == 35,
			"...35 of 140 states hold on", (int)(sizeof(grab) / sizeof(grab[0])), 35);
	}

	/* The borderline states, named one by one so that a change
	 * to one of them has to change a check with a sentence on it. */
	ok(ModernStateIsGrab(11) == 0, "AS_REACH is a catch, not a hold", ModernStateIsGrab(11), 0);
	ok(ModernStateIsGrab(28) == 0, "AS_UPJUMP likewise", ModernStateIsGrab(28), 0);
	ok(ModernStateIsGrab(80) == 0, "AS_ALL4S is where the crawl-to-hang STARTS",
		ModernStateIsGrab(80), 0);
	ok(ModernStateIsGrab(36) == 0, "and a pushblock is not in it at all",
		ModernStateIsGrab(36), 0);
	ok(ModernStateIsGrab(139) == 1, "AS_LEDGEHANG is in it - the ledge move is a grab from the fire on",
		ModernStateIsGrab(139), 1);

	{
		const short duck[] = { 39, 67, 71, 72, 80, 81, 84, 85, 86, 98, 105, 106 };
		unsigned char want[140];

		for (i = 0; i < 140; i++)
			want[i] = 0;

		for (i = 0; i < (int)(sizeof(duck) / sizeof(duck[0])); i++)
			want[duck[i]] = 1;

		for (i = 0; i <= 139; i++)
			ok(ModernStateIsDuck((short)i) == want[i],
				"all 140 states: the duck-and-crawl family matches the reference",
				ModernStateIsDuck((short)i), want[i]);

		ok((int)(sizeof(duck) / sizeof(duck[0])) == 12,
			"...12 of 140 states are the crouch and the crawl",
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

		for (i = 0; i <= 139; i++)
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

	/* The ledge hang's turn and lowering, AS_LEDGEHANG. The move fires from a
	 * run with the key held; the run is outside the family, so there the key
	 * is the key, and the key memory it sets makes the move's first tick no
	 * fresh press. From the fire on the bit is delivered whatever the key
	 * does; a fresh press in the move lets go, and the next key-up delivers
	 * again. The families are asked by state, as specific\input.cpp asks. */
	{
		const int run = (int)ModernStateIsGrab(1);		// AS_RUN
		const int ledge = (int)ModernStateIsGrab(139);	// AS_LEDGEHANG

		state = 0;
		bits = grab_tick(1, 1, run, 0, &state, 0);	// the run, key down: the fire
		ok((bits & MODERN_IN_ACTION) != 0, "ledge: in the run the key is the key",
			bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
		bits = grab_tick(1, 1, ledge, 0, &state, 0);	// the turn's first tick, key still down
		ok((state & MODERN_GRAB_RELEASE) == 0, "ledge: the key held into the move is no let-go",
			state & MODERN_GRAB_RELEASE, 0);
		ok((bits & MODERN_IN_ACTION) != 0, "ledge: ...and the bit is delivered",
			bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
		bits = grab_tick(1, 0, ledge, 0, &state, 0);	// the key goes up in the move
		ok((bits & MODERN_IN_ACTION) != 0, "ledge: the key up in the move still delivers the bit",
			bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
		bits = grab_tick(1, 1, ledge, 0, &state, 0);	// a fresh press in the move
		ok((bits & MODERN_IN_ACTION) == 0, "ledge: a fresh press in the move strips the bit",
			bits & MODERN_IN_ACTION, 0);
		ok((state & MODERN_GRAB_RELEASE) != 0, "ledge: ...and is the let-go press",
			state & MODERN_GRAB_RELEASE, MODERN_GRAB_RELEASE);
		bits = grab_tick(1, 0, ledge, 0, &state, 0);	// the key up again
		ok((state & MODERN_GRAB_RELEASE) == 0, "ledge: the key up again clears the let-go",
			state & MODERN_GRAB_RELEASE, 0);
		ok((bits & MODERN_IN_ACTION) != 0, "ledge: ...and the bit is delivered again",
			bits & MODERN_IN_ACTION, MODERN_IN_ACTION);

		// The same fire under "One key", where the key raises the whole word.
		state = 0;
		grab_tick_onekey(1, 1, run, 0, &state, 0);
		bits = grab_tick_onekey(1, 1, ledge, 0, &state, 0);
		ok((bits & MODERN_IN_ACTION) != 0, "ledge, One key: the key held into the move is delivered",
			bits & MODERN_IN_ACTION, MODERN_IN_ACTION);
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
		sit.cam_cut = 0;

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
		sit.cam_cut = 0;
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
	sit.cam_cut = 0;
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

/* ------------------------------------------------ the cut and its latch --- */

/* The stick at full push in the direction `off` (0 straight ahead, 0x4000
 * right), in the pad's own axes: x right-positive, y forward-negative. */
static void stick_at(long off, long* x, long* y)
{
	const double rad = (double)off * 3.14159265358979323846 / 32768.0;

	*x = roundl2(127.0 * sin(rad));
	*y = -roundl2(127.0 * cos(rad));
}

/* A thumb that is never still: the direction walks 60 units a tick out to 600
 * either side of `base` and back, a triangle of 40 ticks. 60 is the median
 * stick step of the held ticks in the rich2 MODERNLOG through the sniper
 * camera (61 over 2199 ticks); 600 carries the direction past cfg.recenter's
 * 384 twice a period, so the held course is re-seated over and over. */
static long thumb(long base, long t)
{
	const long p = t % 40;

	if (p < 10)
		return base + p * 60;

	if (p < 30)
		return base + (20 - p) * 60;

	return base + (p - 40) * 60;
}

/* One hold through camera cuts, run twice side by side: `cut` sees the
 * reference jump to cam1 on tick `at1` and to cam2 on tick `at2`, with
 * cam_cut set on those two ticks; `still` keeps cam0 all along. Both get the
 * same thumb. Returns the ticks from `at1` on where the two courses differ,
 * split at `at2`; `biggest` is the cut run's largest course step after the
 * first cut, `moved` the ticks it moved at all, `latched` the ticks the latch
 * stood from the first cut on, `early` the ticks it stood before it. */
static void cut_twin(const MODERN_CFG* cfg, long base, short cam0, short cam1, short cam2,
	long at1, long at2, long ticks, MODERN_STATE* cut, long* differ1, long* differ2,
	long* biggest, long* moved, long* latched, long* early)
{
	MODERN_STATE still;
	MODERN_SITUATION sit;
	MODERN_RESULT oc, os;
	long t;

	sit.anim_state = 1;					/* AS_RUN */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;

	ModernResetState(cut);
	ModernResetState(&still);
	*differ1 = *differ2 = *biggest = *moved = *latched = *early = 0;

	for (t = 0; t < ticks; t++)
	{
		const short cam = (t >= at2) ? cam2 : (t >= at1) ? cam1 : cam0;
		long x, y;

		stick_at(thumb(base, t), &x, &y);

		sit.cam_cut = (t == at1 || t == at2) ? 1 : 0;
		tick(cfg, cut, x, y, cam, 0, &sit, &oc);
		sit.cam_cut = 0;
		tick(cfg, &still, x, y, cam0, 0, &sit, &os);

		if (t < at1)
		{
			*early += cut->cut_latch;
			continue;
		}

		if (cut->course != still.course)
		{
			if (t < at2)
				(*differ1)++;
			else
				(*differ2)++;
		}

		if (absl(oc.course_step) > *biggest)
			*biggest = absl(oc.course_step);

		if (oc.course_step)
			(*moved)++;

		*latched += cut->cut_latch;
	}
}

static void camera_cut_latch()
{
	MODERN_CFG cfg;
	MODERN_CFG off_cfg;
	MODERN_STATE state;
	MODERN_STATE still;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	MODERN_RESULT os;
	long differ1, differ2, biggest, moved, latched, early;
	long x, y, t, bad;
	int a, c;

	/* The numbers of the rich2 MODERNLOG through the sniper camera: the chase
	 * camera's reference before the cut at 14891 ms was -16736, the cut into
	 * the sniper at 21516 ms jumped the course by 29198, and "right" is the
	 * direction the thumb held. cam2 is a third camera, 12000 further on. */
	const short cam0 = -16736;
	const short cam1 = ModernWrap(-16736L + 29198L);
	const short cam2 = ModernWrap((long)cam1 + 12000L);
	const long base = 0x4000;

	printf("\nthe camera cut keeps the held course until the stick is let go or re-aimed\n");

	ModernDefaults(&cfg);
	off_cfg = cfg;
	off_cfg.cut_latch = 0;

	ok(cfg.cut_latch == 1, "the cut latch ships on (MODERN_CUT_LATCH)", cfg.cut_latch, 1);

	/* (1) The log's case. A held, wandering thumb; the reference jumps by
	 * 29198 on tick 20 and by 12000 more on tick 170. Against a twin whose
	 * camera never moves, the course is the same on every tick: the thumb
	 * still re-seats it, the cuts never do. */
	cut_twin(&cfg, base, cam0, cam1, cam2, 20, 170, 320, &state, &differ1, &differ2,
		&biggest, &moved, &latched, &early);
	ok(early == 0, "(1) no latch while the camera stays", early, 0);
	ok(latched == 300, "(1) the cut latches, and the latch stands for all 300 held ticks",
		latched, 300);
	ok(differ1 == 0, "(1) a cut of 29198 with the stick held: the course is the uncut twin's",
		differ1, 0);
	ok(differ2 == 0, "(1) a second cut while the latch stands changes nothing", differ2, 0);
	ok(moved > 0, "(1) ...while the wandering thumb still re-seats the course", moved > 0 ? 1 : 0, 1);
	ok(biggest < 1024, "(1) ...and no tick moves it more than a re-seat of the thumb", biggest, 1023);
	printf("  (1) 300 latched ticks, %ld re-seats by the thumb, largest step %ld\n", moved, biggest);

	/* The same hold with the latch switched off in this copy of the config:
	 * the first re-seat after the cut takes the new camera - the turn the log
	 * shows. */
	{
		MODERN_STATE off_state;

		cut_twin(&off_cfg, base, cam0, cam1, cam2, 20, 170, 320, &off_state, &differ1,
			&differ2, &biggest, &moved, &latched, &early);
		ok(differ1 > 0 && biggest >= 16384,
			"(1) counter-proof: without the latch the cut turns her at the next re-seat",
			biggest, 16384);
		ok(latched == 0, "(1) ...and the switched-off latch never stands", latched, 0);
		printf("  (1) without the latch: %ld of 150 ticks off the twin, largest step %ld\n",
			differ1, biggest);
	}

	sit.anim_state = 1;
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;
	sit.cam_cut = 0;

	/* Rolled far while the latch stands - half a turn from where the stick
	 * pointed at the first cut, the second cut having left that direction
	 * where it was: a re-aim past MODERN_CUT_REAIM, so the hold ends and the
	 * new direction is read against the view of the tick, not against the
	 * chase camera the course was seated on. */
	stick_at(-0x4000, &x, &y);
	tick(&cfg, &state, x, y, cam2, 0, &sit, &out);
	ok(out.reference == cam2, "(1) rolled far under the latch: the reference is the view of the tick",
		out.reference, cam2);
	ok(state.course == ModernWrap((long)cam2 + ModernAngle(x, -y)),
		"(1) ...and the new direction is taken against it", state.course,
		ModernWrap((long)cam2 + ModernAngle(x, -y)));
	ok(state.cut_latch == 0 && out.latch_drop == 1,
		"(1) ...because the re-aim ended the hold on that tick", out.latch_drop, 1);

	/* Let go and push again: the latch goes with the course, and the push
	 * seats on the camera of that tick. */
	tick(&cfg, &state, 0, 0, cam1, 0, &sit, &out);
	ok(state.have_ref == 0 && state.cut_latch == 0,
		"(1) letting go drops the course and the latch", state.have_ref + state.cut_latch, 0);

	stick_at(base, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	ok(out.reference == cam1, "(1) the next push seats on the camera of its tick",
		out.reference, cam1);
	ok(state.course == ModernWrap((long)cam1 + ModernAngle(x, -y)),
		"(1) ...so right is the new view's right", state.course,
		ModernWrap((long)cam1 + ModernAngle(x, -y)));
	ok(state.cut_latch == 0, "(1) ...with no latch", state.cut_latch, 0);

	/* The cut back with the stick held, against a twin seated on the same
	 * camera that never cuts. */
	ModernResetState(&still);
	tick(&cfg, &still, x, y, cam1, 0, &sit, &os);
	bad = 0;

	for (t = 1; t < 300; t++)
	{
		stick_at(thumb(base, t), &x, &y);
		sit.cam_cut = (t == 10) ? 1 : 0;
		tick(&cfg, &state, x, y, (t >= 10) ? cam0 : cam1, 0, &sit, &out);
		sit.cam_cut = 0;
		tick(&cfg, &still, x, y, cam1, 0, &sit, &os);

		if (state.course != still.course || (t >= 10 && !state.cut_latch))
			bad++;
	}

	ok(bad == 0, "(1) the cut back with the stick held: no turn, the latch stands", bad, 0);

	/* (2) The mirror of (c), with the thumb moving. (c) holds the stick
	 * dead still, so it never re-seats and cannot see the re-seat taking the
	 * new camera. Here the thumb walks 100 units a tick after a half-turn
	 * cut, past recenter on the fourth tick, from every stick direction and
	 * every camera heading; the twin's camera never cuts. */
	{
		long steady_off = 0;
		long drift_on = 0;
		long drift_off = 0;
		long cases = 0;
		int pass;

		for (pass = 0; pass < 3; pass++)
		{
			const MODERN_CFG* use = (pass == 1) ? &cfg : &off_cfg;
			const long drift = pass ? 100 : 0;	/* pass 0: (c)'s steady stick, no latch */

			for (a = 0; a < 64; a++)
				for (c = 0; c < 32; c++)
				{
					const short cam = (short)(c * 2048);
					const long start = (long)a * 1024;

					ModernResetState(&state);
					ModernResetState(&still);

					for (t = 0; t < 9; t++)
					{
						stick_at(start + (t ? (t - 1) * drift : 0), &x, &y);
						sit.cam_cut = (t == 1) ? 1 : 0;
						tick(use, &state, x, y, t ? ModernWrap((long)cam + 0x8000) : cam, 0,
							&sit, &out);
						sit.cam_cut = 0;
						tick(use, &still, x, y, cam, 0, &sit, &os);

						if (!t)
							continue;

						if (pass == 1)
							cases++;

						if (state.course != still.course)
						{
							if (pass == 0)
								steady_off++;
							else if (pass == 1)
								drift_on++;
							else
								drift_off++;
						}
					}
				}
		}

		printf("  (2) %ld ticks after a half-turn cut with the thumb moving\n", cases);
		ok(steady_off == 0, "(2) a steady stick holds through the cut even without the latch",
			steady_off, 0);
		ok(drift_on == 0, "(2) a moving thumb through the cut: the latch keeps the twin's course",
			drift_on, 0);
		ok(drift_off > 0, "(2) counter-proof: without the latch the moving thumb turns her",
			drift_off > 0 ? 1 : 0, 1);
		printf("  (2) without the latch: %ld of %ld ticks off the twin\n", drift_off, cases);
	}

	/* (3) The player's own camera turn under the latch moves reference and
	 * course together, 500 units a tick, as it does without a cut. */
	{
		short ref0;
		long turned = 0;

		sit.cam_cut = 0;
		sit.cam_player_step = 0;
		stick_at(base, &x, &y);
		ModernResetState(&state);
		tick(&cfg, &state, x, y, cam0, 0, &sit, &out);
		sit.cam_cut = 1;
		tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
		sit.cam_cut = 0;
		ok(state.cut_latch == 1, "(3) latched by the cut", state.cut_latch, 1);
		ref0 = state.ref;

		for (t = 1; t <= 10; t++)
		{
			sit.cam_player_step = 500;
			tick(&cfg, &state, x, y, ModernWrap((long)cam1 + t * 500), 0, &sit, &out);

			if (out.course_step == 500 && out.reference == ModernWrap((long)ref0 + t * 500) &&
				state.cut_latch)
				turned++;
		}

		sit.cam_player_step = 0;
		ok(turned == 10, "(3) the player's camera turn under the latch takes the course along",
			turned, 10);

		/* And a re-seat after it reads the stick against the turned reference. */
		stick_at(base + 0x2000, &x, &y);
		tick(&cfg, &state, x, y, ModernWrap((long)cam1 + 5000), 0, &sit, &out);
		ok(state.course == ModernWrap((long)ref0 + 5000 + ModernAngle(x, -y)),
			"(3) ...and a re-seat after it is against the turned reference", state.course,
			ModernWrap((long)ref0 + 5000 + ModernAngle(x, -y)));
	}

	/* Where the latch has nothing to do. hold_course off reads the camera of
	 * every tick, so a cut sets no latch there; aiming reads the camera fresh
	 * and drops it. */
	{
		MODERN_CFG free_cfg = cfg;

		free_cfg.hold_course = 0;
		sit.cam_cut = 0;
		stick_at(base, &x, &y);
		ModernResetState(&state);
		tick(&free_cfg, &state, x, y, cam0, 0, &sit, &out);
		sit.cam_cut = 1;
		tick(&free_cfg, &state, x, y, cam1, 0, &sit, &out);
		sit.cam_cut = 0;
		ok(state.cut_latch == 0 && out.reference == cam1,
			"hold_course off: a cut sets no latch, the camera of the tick leads",
			state.cut_latch, 0);

		free_cfg = cfg;
		free_cfg.aim_strafe = 1;
		ModernResetState(&state);
		tick(&free_cfg, &state, x, y, cam0, 0, &sit, &out);
		sit.cam_cut = 1;
		tick(&free_cfg, &state, x, y, cam1, 0, &sit, &out);
		sit.cam_cut = 0;
		sit.drawn = 1;
		sit.action = 1;
		tick(&free_cfg, &state, x, y, cam1, 0, &sit, &out);
		ok(out.mode == MODERN_MODE_AIM && state.cut_latch == 0 && out.reference == cam1,
			"aiming reads the camera of the tick and drops the latch", state.cut_latch, 0);
		sit.drawn = 0;
		sit.action = 0;
	}

	/* A push that starts on the tick of a cut has no course to keep: it seats
	 * on the new camera and nothing latches. */
	sit.cam_cut = 1;
	stick_at(base, &x, &y);
	ModernResetState(&state);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	sit.cam_cut = 0;
	ok(out.reference == cam1 && state.cut_latch == 0,
		"a push on the cut's own tick seats on the new camera, unlatched", state.cut_latch, 0);
}

/* A situation of plain running on land with no cut. */
static void reaim_sit(MODERN_SITUATION* sit)
{
	sit->anim_state = 1;				/* AS_RUN */
	sit->classic = 0;
	sit->drawn = 0;
	sit->action = 0;
	sit->jump = 0;
	sit->blocked = 0;
	sit->cam_player_step = 0;
	sit->cam_cut = 0;
}

/* A latch armed the way the seam arms it: a push on `cam0` seats the course,
 * and the next tick, the stick unchanged, carries the cut to `cam1`. */
static void reaim_arm(const MODERN_CFG* cfg, MODERN_STATE* state, long x, long y, short cam0,
	short cam1, MODERN_RESULT* out)
{
	MODERN_SITUATION sit;

	reaim_sit(&sit);
	ModernResetState(state);
	tick(cfg, state, x, y, cam0, 0, &sit, out);
	sit.cam_cut = 1;
	tick(cfg, state, x, y, cam1, 0, &sit, out);
}

/* Two stick positions whose directions (ModernAngle, as the translation takes
 * them) lie exactly `gap` apart, both pushed well out (radius 100 or more).
 * Searched, because a direction is what an integer stick gives and not any
 * number one likes; returns 0 when no pair exists. */
static int reaim_pair(long gap, long* ax, long* ay, long* bx, long* by)
{
	static short angle_x[65536], angle_y[65536];
	static unsigned char have[65536];
	static int filled = 0;
	long sx, sy;
	long a;

	if (!filled)
	{
		memset(have, 0, sizeof(have));

		for (sx = -127; sx <= 127; sx++)
			for (sy = -127; sy <= 127; sy++)
			{
				if (sx * sx + sy * sy < 100 * 100)
					continue;

				a = (unsigned short)ModernAngle(sx, -sy);

				if (!have[a])
				{
					have[a] = 1;
					angle_x[a] = (short)sx;
					angle_y[a] = (short)sy;
				}
			}

		filled = 1;
	}

	for (a = 0; a < 65536; a++)
	{
		const long b = (a + gap) & 0xFFFF;

		if (have[a] && have[b])
		{
			*ax = angle_x[a];
			*ay = angle_y[a];
			*bx = angle_x[b];
			*by = angle_y[b];
			return 1;
		}
	}

	return 0;
}

/* The re-aim that ends the cut latch (MODERN_CUT_REAIM): a stick held through
 * a cut and then turned more than 45 degrees from where it pointed at the cut
 * is read in the view on screen from that tick on. */
static void camera_cut_reaim()
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_STATE fresh;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	MODERN_RESULT of;
	const short cam0 = -16736;
	const short cam1 = ModernWrap(-16736L + 29198L);
	const short cam2 = ModernWrap((long)cam1 + 12000L);
	long x, y, ax, ay, bx, by, t, bad, drops;
	short off_a;

	printf("\nthe cut latch also ends when the stick is re-aimed by more than 45 degrees\n");

	ModernDefaults(&cfg);
	reaim_sit(&sit);

	ok(MODERN_CUT_REAIM == 8192, "the re-aim bound is 45 degrees (MODERN_CUT_REAIM)",
		MODERN_CUT_REAIM, 8192);

	/* (vii) The remembered direction is the translation's own `offset` of the
	 * arming tick, and the arming tick itself drops nothing. */
	stick_at(0x1234, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	ok(state.cut_latch == 1 && state.cut_off == ModernAngle(x, -y),
		"(vii) the latch remembers the stick's direction of its arming tick", state.cut_off,
		ModernAngle(x, -y));
	ok(out.latch_drop == 0 && out.reference == cam0,
		"(vii) ...and the arming tick keeps the course's reference", out.latch_drop, 0);

	/* (iv) The drop seats exactly what a release and a fresh push in the same
	 * direction would seat, on the same tick's camera - and she does not stop:
	 * the tick is active and sends bits, where a release sends none. */
	off_a = ModernAngle(x, -y);
	stick_at((long)off_a + 0x4000, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	ModernResetState(&fresh);
	tick(&cfg, &fresh, x, y, cam1, 0, &sit, &of);
	ok(out.latch_drop == 1 && state.cut_latch == 0,
		"a quarter turn under the latch ends the hold on that tick", out.latch_drop, 1);
	ok(state.have_ref == fresh.have_ref && state.ref == fresh.ref &&
		state.course == fresh.course && state.last_off == fresh.last_off &&
		state.cut_latch == fresh.cut_latch && state.cut_off == fresh.cut_off,
		"...course, reference and latch as a fresh push of that stick leaves them",
		state.course, fresh.course);
	ok(out.reference == cam1 && out.active == 1 && out.bits != 0,
		"...seated on the camera of the tick, and she keeps moving", out.reference, cam1);

	/* (i) A slow roll, 200 units a tick, every step far under recenter: the
	 * hold ends on the first tick its sum passes the bound from the arming
	 * direction, and not before. */
	stick_at(0, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	off_a = state.cut_off;
	bad = 0;
	drops = 0;

	for (t = 1; t <= 60; t++)
	{
		long dev;

		stick_at(t * 200, &x, &y);
		dev = absl(ModernWrap((long)ModernAngle(x, -y) - off_a));
		tick(&cfg, &state, x, y, cam1, 0, &sit, &out);

		if (drops)
		{
			if (out.latch_drop)
				bad++;

			continue;
		}

		if ((dev > 8192) != (out.latch_drop == 1))
			bad++;

		if (out.latch_drop)
		{
			drops++;

			if (out.reference != cam1)
				bad++;
		}
		else if (out.reference != cam0 || !state.cut_latch)
			bad++;
	}

	ok(bad == 0 && drops == 1, "(i) a slow roll ends the hold when its sum passes 8192, not before",
		bad, 0);

	/* ...and a roll that turns back inside the bound keeps it, out to 8000
	 * and home again. */
	stick_at(0, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	bad = 0;

	for (t = 0; t <= 80; t++)
	{
		const long r = (t <= 40) ? t * 200 : (80 - t) * 200;

		stick_at(r, &x, &y);
		tick(&cfg, &state, x, y, cam1, 0, &sit, &out);

		if (out.latch_drop || !state.cut_latch || out.reference != cam0)
			bad++;
	}

	ok(bad == 0, "(i) a roll out to 8000 and back inside the bound keeps the hold", bad, 0);

	/* (ii) A cut while the latch stands leaves the remembered direction: a
	 * stick rolled 6000, cut again, and rolled 3000 more ends the hold - 9000
	 * from the first cut, though only 3000 from the second. */
	stick_at(0, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	off_a = state.cut_off;
	stick_at(6000, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	sit.cam_cut = 1;
	tick(&cfg, &state, x, y, cam2, 0, &sit, &out);
	sit.cam_cut = 0;
	ok(state.cut_latch == 1 && state.cut_off == off_a && out.reference == cam0,
		"(ii) a second cut under the latch keeps the remembered direction", state.cut_off, off_a);
	stick_at(9000, &x, &y);
	tick(&cfg, &state, x, y, cam2, 0, &sit, &out);
	ok(out.latch_drop == 1 && out.reference == cam2,
		"(ii) ...so 9000 from the first cut ends the hold", out.latch_drop, 1);

	/* (iii) A cut after the drop, with the stick out, arms the latch again
	 * with that tick's direction. */
	sit.cam_cut = 1;
	tick(&cfg, &state, x, y, cam0, 0, &sit, &out);
	sit.cam_cut = 0;
	ok(state.cut_latch == 1 && state.cut_off == ModernAngle(x, -y) && out.reference == cam2,
		"(iii) a cut after the drop arms the latch again with the stick of its tick",
		state.cut_off, ModernAngle(x, -y));

	/* (iv) A tick that carries a cut and a roll past the bound together: the
	 * cut is taken first (the latch already stands and keeps its direction),
	 * then the roll ends the hold, so the course is seated on that tick's new
	 * camera. */
	stick_at(0, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	stick_at(0x4000, &x, &y);
	sit.cam_cut = 1;
	tick(&cfg, &state, x, y, cam2, 0, &sit, &out);
	sit.cam_cut = 0;
	ok(out.latch_drop == 1 && state.cut_latch == 0 && out.reference == cam2 &&
		state.course == ModernWrap((long)cam2 + ModernAngle(x, -y)),
		"(iv) a cut and a roll past the bound on one tick: seated on the new camera",
		out.reference, cam2);

	/* ...and a push with no course yet, on a tick that carries a cut, has
	 * nothing to latch and nothing to drop. */
	ModernResetState(&state);
	sit.cam_cut = 1;
	tick(&cfg, &state, x, y, cam2, 0, &sit, &out);
	sit.cam_cut = 0;
	ok(out.latch_drop == 0 && state.cut_latch == 0 && out.reference == cam2,
		"(iv) a push on the cut's own tick neither latches nor drops", out.latch_drop, 0);

	/* (v) The bound: exactly 8192 keeps the hold, 8193 ends it, on either
	 * side; the pairs are integer sticks whose directions lie exactly that far
	 * apart. */
	{
		const long gaps[4] = { 8192, -8192, 8193, -8193 };
		int g;

		for (g = 0; g < 4; g++)
		{
			const int found = reaim_pair(gaps[g], &ax, &ay, &bx, &by);
			const long want = (absl(gaps[g]) > 8192) ? 1 : 0;

			ok(found == 1, "(v) a stick pair exactly that far apart exists", found, 1);

			if (!found)
				continue;

			reaim_arm(&cfg, &state, ax, ay, cam0, cam1, &out);
			tick(&cfg, &state, bx, by, cam1, 0, &sit, &out);
			ok(ModernWrap((long)ModernAngle(bx, -by) - ModernAngle(ax, -ay)) == gaps[g] &&
				out.latch_drop == want && state.cut_latch == 1 - want,
				want ? "(v) 8193 from the arming direction ends the hold" :
				"(v) exactly 8192 from the arming direction keeps it", out.latch_drop, want);
		}
	}

	/* ...and across the wrap the short way: armed just short of +32768,
	 * re-aimed just past -32768 is a small step and keeps the hold; a quarter
	 * turn on across the wrap ends it. */
	reaim_arm(&cfg, &state, 1, 127, cam0, cam1, &out);
	tick(&cfg, &state, -1, 127, cam1, 0, &sit, &out);
	ok(state.cut_off == ModernAngle(1, -127) && ModernAngle(-1, -127) < 0 &&
		out.latch_drop == 0 && state.cut_latch == 1,
		"(v) across +32767/-32768 the step is the short one and keeps the hold",
		out.latch_drop, 0);
	stick_at(-0x8000 + 0x2400, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	ok(out.latch_drop == 1, "(v) ...and a turn past the bound across the wrap ends it",
		out.latch_drop, 1);

	/* (vi) The movement keys: the direction is the one the translation takes
	 * from the stick ModernKeyStick builds. Forward held through the cut, then
	 * right added - 45 degrees exactly - keeps the hold; right alone, a
	 * quarter turn, ends it. */
	ModernKeyStick(0, 0, 1, 0, 0, 0, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	ModernKeyStick(0, 0, 1, 0, 0, 1, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	ok(absl(ModernWrap((long)ModernAngle(x, -y) - state.cut_off)) == 8192 &&
		out.latch_drop == 0 && state.cut_latch == 1 && out.reference == cam0,
		"(vi) keys: forward, then forward and right (45 degrees) keeps the hold",
		out.latch_drop, 0);
	ModernKeyStick(0, 0, 0, 0, 0, 1, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	ok(out.latch_drop == 1 && out.reference == cam1,
		"(vi) ...and right alone, a quarter turn, ends it", out.latch_drop, 1);

	/* The re-seat under a standing latch: a flick of recenter or more that
	 * stays inside the bound reads against the held reference, as before. */
	stick_at(0, &x, &y);
	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	stick_at(5000, &x, &y);
	tick(&cfg, &state, x, y, cam1, 0, &sit, &out);
	ok(out.latch_drop == 0 && out.reference == cam0 &&
		state.course == ModernWrap((long)cam0 + ModernAngle(x, -y)),
		"a flick of 5000 under the latch re-seats against the held reference", state.course,
		ModernWrap((long)cam0 + ModernAngle(x, -y)));

	/* (viii) The remembered direction goes wherever the latch goes: the
	 * release, aiming and ModernResetState. */
	tick(&cfg, &state, 0, 0, cam1, 0, &sit, &out);
	ok(state.cut_latch == 0 && state.cut_off == 0,
		"(viii) letting go clears the latch and its direction", state.cut_off, 0);

	{
		MODERN_CFG aim_cfg = cfg;

		aim_cfg.aim_strafe = 1;
		stick_at(0x2000, &x, &y);
		reaim_arm(&aim_cfg, &state, x, y, cam0, cam1, &out);
		sit.drawn = 1;
		sit.action = 1;
		tick(&aim_cfg, &state, x, y, cam1, 0, &sit, &out);
		sit.drawn = 0;
		sit.action = 0;
		ok(out.mode == MODERN_MODE_AIM && state.cut_latch == 0 && state.cut_off == 0,
			"(viii) aiming clears the latch and its direction", state.cut_off, 0);
	}

	reaim_arm(&cfg, &state, x, y, cam0, cam1, &out);
	ModernResetState(&state);
	ok(state.cut_latch == 0 && state.cut_off == 0,
		"(viii) ModernResetState clears both", state.cut_off, 0);

	/* (viii) With the latch switched off, or the held course off, a cut and
	 * a quarter turn after it never report a drop, and the course is what it
	 * is without the rule: the camera of the tick plus the stick. */
	{
		MODERN_CFG no_latch = cfg;
		MODERN_CFG no_hold = cfg;
		int pass;

		no_latch.cut_latch = 0;
		no_hold.hold_course = 0;
		bad = 0;

		for (pass = 0; pass < 2; pass++)
		{
			const MODERN_CFG* use = pass ? &no_hold : &no_latch;

			stick_at(0, &x, &y);
			reaim_arm(use, &state, x, y, cam0, cam1, &out);

			if (state.cut_latch || out.latch_drop)
				bad++;

			for (t = 1; t <= 50; t++)
			{
				stick_at(t * 2000, &x, &y);
				tick(use, &state, x, y, cam1, 0, &sit, &out);

				if (out.latch_drop || state.cut_latch ||
					state.course != ModernWrap((long)cam1 + ModernAngle(x, -y)))
					bad++;
			}
		}

		ok(bad == 0, "(viii) cut_latch 0 or hold_course 0: no latch, no drop, the course as before",
			bad, 0);
	}
}

/* ----------------------------------------------------- the modern camera --- */

/* The orbit: game\modern_camera.cpp, compiled beside this file exactly the
 * way the translation is, for exactly the same reason - a feel number
 * nobody can re-measure is a number nobody can change.
 *
 * None of the expectations below comes from TROne. Their orbit (TROne-main,
 * copy of 2026-09-13) held a world angle and never returned behind the
 * player at all; ours holds a world angle too and lets it fall back behind
 * her (game\modern_camera.h); since its round K32 TROne runs this orbit
 * itself (copy of 2026-10-06, commit 75c4c843). The numbers here come from
 * two
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
		"below her the clamp is ChaseCamera's own (camera.cpp:2007)", cfg.pitch_max, 15470);
	ok(cfg.pitch_min == -10923,
		"above her the sphere's top cap is cut at 60 degrees", cfg.pitch_min, -10923);
	ok(cfg.pitch_home == -1820,
		"the resting elevation is ChaseCamera's own (camera.cpp:2000)", cfg.pitch_home, -1820);
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
		"and past 125 deg/s, a circle in under three seconds, at the top",
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

/* The diagonal-wall push: the orbit's angle is not the view while it pins the eye.
 *
 * In The 13th Floor and Escape with the Iris, game\camera.cpp's MoveCamera runs
 * Core's CheckForRichesIllegalDiagonalWalls after the orbit has placed the eye,
 * and that push throws it across the dialled ray to the near side of a
 * 45-degree wall panel. game\camera.cpp notes the push per orbit pass and asks
 * ModernCameraOrbitIsView before it hands out the world yaw or the player's
 * step; a decline makes game\modern_state.cpp read the stick against the
 * measured view, and the source switch arms the cut latch, so a stick held
 * through the push keeps its course.
 *
 * The numbers are a RICH2 MODERNLOG tick at room 8's south-west panel: the
 * measured view (`cam=`) -26668, the orbit's world yaw (`cam_world=`) 9478,
 * `view_err=` 29390, the stick 19/125, the return turning the orbit by 1483
 * units a tick (`cam_ret=`). Of that log's six push episodes the longest
 * lasted 172 ticks. */
static void camera_push_view()
{
	MODERN_CFG cfg;
	MODERN_CFG off_cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	long differ1, differ2, biggest, moved, latched, early;
	long lp, live, at, flag, bad, x, y;
	unsigned short orbit_at;
	short off;

	const short view = -26668;
	const short orbit = 9478;
	const short orbit_after = ModernWrap(9478L - 8L * 1483L);
	const long base = 0x4000;

	printf("the diagonal-wall push: the stick is read in the view the push leaves\n");

	/* (i) The rule itself. */
	ok(ModernCameraOrbitIsView(1001, 1000, 0) == 1,
		"the orbit ran last tick and its eye stayed on the ray: its angle is the view",
		ModernCameraOrbitIsView(1001, 1000, 0), 1);
	ok(ModernCameraOrbitIsView(1001, 1000, 1) == 0,
		"the push moved that eye: the angle declines",
		ModernCameraOrbitIsView(1001, 1000, 1), 0);
	ok(ModernCameraOrbitIsView(1000, 998, 0) == 0,
		"an orbit that did not run last tick declines as before",
		ModernCameraOrbitIsView(1000, 998, 0), 0);
	ok(ModernCameraOrbitIsView(1000, 998, 1) == 0, "...pushed or not",
		ModernCameraOrbitIsView(1000, 998, 1), 0);
	ok(ModernCameraOrbitIsView(1000, 1000, 0) == 0, "the same tick asking twice declines as before",
		ModernCameraOrbitIsView(1000, 1000, 0), 0);
	ok(ModernCameraOrbitIsView(0, 65535, 0) == 1, "over the wrap 65535 -> 0 the stamp still holds",
		ModernCameraOrbitIsView(0, 65535, 0), 1);
	ok(ModernCameraOrbitIsView(0, 65535, 1) == 0, "...and a push over the wrap still declines",
		ModernCameraOrbitIsView(0, 65535, 1), 0);
	ok(ModernCameraOrbitIsView(0, 65534, 0) == 0, "65534 -> 0 is a gap as before",
		ModernCameraOrbitIsView(0, 65534, 0), 0);

	/* Never live where the stamp alone is not, and never live on a push. */
	live = 0;
	bad = 0;

	for (lp = 0; lp < 65536; lp++)
	{
		live += ModernCameraOrbitIsView(4321, (unsigned short)lp, 0);
		bad += ModernCameraOrbitIsView(4321, (unsigned short)lp, 1);

		if (ModernCameraOrbitIsView(4321, (unsigned short)lp, 0) !=
			ModernCameraTickFollows(4321, (unsigned short)lp))
			bad++;
	}

	ok(live == 1 && bad == 0,
		"over all 65536 stamps: unpushed it is the tick stamp, pushed it is never live",
		live * 100 + bad, 100);

	/* Walked as game\camera.cpp walks it: each orbit pass stamps the tick and
	 * clears the flag, MoveCamera of the same pass sets it while the push moves
	 * the eye (ticks 100..271, the log's longest episode), and the seam asks
	 * on the tick after. Declined exactly on the 172 ticks after a pushed pass:
	 * a push two ticks ago is answered by the clean pass in between. */
	live = 0;
	bad = 0;
	orbit_at = 0;
	flag = 0;

	for (lp = 1; lp <= 400; lp++)
	{
		const long answer = ModernCameraOrbitIsView((unsigned short)lp, orbit_at, flag);
		const long pushed_before = (lp - 1 >= 100 && lp - 1 <= 271) ? 1 : 0;

		if (lp > 1)
		{
			live += answer;

			if (answer != (pushed_before ? 0 : 1))
				bad++;
		}

		orbit_at = (unsigned short)lp;	/* the orbit pass of tick lp clears the flag, */
		flag = (lp >= 100 && lp <= 271) ? 1 : 0;	/* its MoveCamera sets it on a push */
	}

	ok(bad == 0 && live == 399 - 172,
		"172 pushed passes: the angle declines on the 172 ticks after them and on none else",
		live, 399 - 172);

	/* (ii) What the seam does with a decline, through the engine's
	 * ModernTranslate. A stick held through a push episode: the reference
	 * switches from the orbit to the measured view on tick 20, with cam_cut, and
	 * back on tick 192 to the orbit, which the return has turned on meanwhile.
	 * Against a twin whose reference never moves the course is the same on
	 * every held tick - the cut latch carries it. */
	ModernDefaults(&cfg);
	off_cfg = cfg;
	off_cfg.hold_course = 0;

	cut_twin(&cfg, base, orbit, view, orbit_after, 20, 192, 320, &state, &differ1, &differ2,
		&biggest, &moved, &latched, &early);
	ok(early == 0, "(ii) no latch before the push", early, 0);
	ok(latched == 300, "(ii) the push's source switch latches the held course for all 300 ticks",
		latched, 300);
	ok(differ1 == 0, "(ii) a stick held into the push: the course is the unpushed twin's",
		differ1, 0);
	ok(differ2 == 0, "(ii) ...and out of it again, onto the orbit's turned angle", differ2, 0);
	ok(biggest < 1024, "(ii) ...no tick moves it more than a re-seat of the thumb", biggest, 1023);

	sit.anim_state = 1;					/* AS_RUN */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;
	sit.cam_cut = 0;

	/* Let go while pinned, push again: the push seats on the measured view, so
	 * the course minus the picture is the stick's own angle - the log's tick,
	 * where the orbit's angle put it 29390 units off. */
	tick(&cfg, &state, 0, 0, orbit, 0, &sit, &out);
	ok(state.have_ref == 0 && state.cut_latch == 0, "(ii) letting go drops the course and the latch",
		state.have_ref + state.cut_latch, 0);

	sit.cam_cut = 1;
	tick(&cfg, &state, 0, 0, view, 0, &sit, &out);		/* the switch, with the stick at rest */
	sit.cam_cut = 0;
	ok(state.cut_latch == 0, "(ii) a switch with the stick at rest latches nothing", state.cut_latch, 0);

	x = 19;
	y = 125;
	off = ModernAngle(x, -y);
	tick(&cfg, &state, x, y, view, 0, &sit, &out);
	ok(out.reference == view, "(ii) the push in the pinned view seats on the measured view",
		out.reference, view);
	ok(state.course == ModernWrap((long)view + off),
		"(ii) ...so the course minus the picture is the stick's angle", state.course,
		ModernWrap((long)view + off));
	{
		const long old_err = absl(ModernWrap((long)ModernWrap((long)orbit + off) - state.course));

		ok(old_err == 29390, "(ii) ...where the orbit's angle would have put it 29390 off (view_err)",
			old_err, 29390);
	}

	/* The switch back to the orbit with that stick held: nothing turns. */
	at = state.course;
	bad = 0;

	for (lp = 0; lp < 60; lp++)
	{
		sit.cam_cut = (lp == 0) ? 1 : 0;
		tick(&cfg, &state, x, y, (short)ModernWrap((long)orbit_after - lp * 1483L), 0, &sit, &out);
		sit.cam_cut = 0;

		if (state.course != at || !state.cut_latch)
			bad++;
	}

	ok(bad == 0, "(ii) the switch back to the turning orbit with the stick held: no turn, latched",
		bad, 0);

	/* With the held course switched off the camera of every tick is the
	 * reference, so under the push the course follows the picture. */
	ModernResetState(&state);
	bad = 0;

	for (lp = 0; lp < 40; lp++)
	{
		const short pic = (short)ModernWrap((long)view + lp * 274L);	/* the pinned view's median turn */

		tick(&off_cfg, &state, x, y, pic, 0, &sit, &out);

		if (state.course != ModernWrap((long)pic + off))
			bad++;
	}

	ok(bad == 0, "(ii) hold_course off: under the push the course follows the picture", bad, 0);
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

	/* And the mouse, which has no top speed of its own: its step is bounded at
	 * MODERN_CAMERA_MOUSE_STEP_MAX each way, and the bound is what has to be
	 * swept. Unlike the stick it can ask for the whole bound on both axes at
	 * once - the pitch takes the yaw's gain - so both rates are the bound, at
	 * every target distance the engine sets, for the ideal, the led eye and
	 * the eye walking in. The step above the bound is swept as well, to show
	 * that the bound is the edge and not a guess with room to spare. */
	{
		const double dists[3] = { 1024.0, 1536.0, 2048.0 };
		const long m = MODERN_CAMERA_MOUSE_STEP_MAX;
		long worst_ideal = 0;
		long worst_walk = 0;
		long mouse_cuts = 0;
		long over;
		long d;
		LEAD_RUN led;

		for (d = 0; d < 3; d++)
		{
			long speed;
			double start;
			long ideal = camcut_worst(m, m, cfg.pitch_min, cfg.pitch_max, dists[d], &cuts);

			if (ideal > worst_ideal)
				worst_ideal = ideal;

			mouse_cuts += cuts;

			for (speed = 10; speed >= 8; speed -= 2)
				for (start = 1.0; start <= 8.0; start += 0.25)
				{
					lead_run(speed, m, m, 1, 120, dists[d], start, &led);

					if (led.worst_lead > worst_walk)
						worst_walk = led.worst_lead;
				}

			printf("  measured: the mouse's largest step, %ld on both axes, at distance %4ld"
				" moves the ideal eye %ld\n", m, (long)dists[d], ideal);
		}

		lead_run(10, m, m, 1, 400, radius, 1.0, &led);

		printf("            the led eye %ld, walking in %ld - a margin of %.2f\n",
			led.worst_eye, worst_walk, (double)INTERP_CAM_SNAP / (double)worst_ideal);

		ok(worst_ideal < INTERP_CAM_SNAP,
			"mouse: its largest step on both axes never trips the eye test, at any distance",
			worst_ideal, INTERP_CAM_SNAP);
		ok(mouse_cuts == 0, "mouse: nor the view direction test", mouse_cuts, 0);
		ok(led.worst_eye < INTERP_CAM_SNAP,
			"mouse: nor the eye the step leads", led.worst_eye, INTERP_CAM_SNAP);
		ok(worst_walk < INTERP_CAM_SNAP,
			"mouse: nor the led eye walking in from far out", worst_walk, INTERP_CAM_SNAP);

		over = camcut_worst(m + 128, m + 128, cfg.pitch_min, cfg.pitch_max, 2048.0, &cuts);

		printf("            128 units (0.7 degrees) more on both axes measures %ld at distance"
			" 2048, with %ld aim cuts\n", over, cuts);

		ok(over >= INTERP_CAM_SNAP || cuts > 0,
			"mouse: and a step 128 units larger already cuts at the rope's distance",
			over, INTERP_CAM_SNAP);
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

/* ------------------------------ the drawn eye walks the chord past the niche ---

   Between two ticks the interpolator draws the eye on the straight line joining
   the two tick eyes, and beside a pillar that line cuts the corner. These checks
   run the engine's own ModernCameraChordClamp and ModernCameraDrawnEye
   (game\modern_camera.cpp, linked by bmt.bat) on the niche above: its 2x2
   pillar is x and z 3072 to 5119, the same shape as the pillars of ANDREA1's
   rooms 3 and 11 where the black frames were measured. The map's test is the
   engine's in two dimensions, like h_new's: a point is open when its sector is
   '.'.

   The reference for "nearest" is brute force: every 1/1024 of the chord is
   asked, and the answer may differ from the best of those by at most one step,
   because the answer is finer than the grid. */

static long h_chord_open(void* ctx, long x, long y, long z)
{
	(void)y;
	return h_solid(x, z, (const char* const*)ctx) ? 0 : 1;
}

/* Two rooms over the niche map for the room order: room 0 holds x below 4096,
   room 1 the rest. Asked about a point in the other half, a room answers with
   a closed sector of its own - the shape of the measured case, where room 225's
   corner wall sector stands where the eye was still in room 40. */
static long h_eye_open(void* ctx, long x, long y, long z, short room, short* at)
{
	*at = room;

	if ((room == 0) != (x < 4096))
		return 0;

	return h_chord_open(ctx, x, y, z);
}

static bool h_on_chord(const long a[3], const long b[3], float alpha, const long p[3])
{
	for (int i = 0; i < 3; i++)
		if (p[i] != InterpLerpLong(a[i], b[i], alpha))
			return false;

	return alpha >= 0.0F && alpha <= 1.0F;
}

static void camera_chord()
{
	const void* niche = (const void*)g_niche;
	long bad_open = 0, bad_chord = 0, bad_near = 0, missing = 0, moved = 0, most_tests = 0;
	long sweeps = 0;

	printf("\nthe drawn eye - the nearest open point of the chord, past the niche's pillar\n");

	/* 1. Two open eyes either side of each of the pillar's four corners, the
	   chord between them through the block, at two depths, and every 64th of
	   the chord as the frame's blend factor. */
	{
		static const long corners[4][4] = {
			// corner x, corner z, and the direction out of the pillar there
			{ 3072, 3072, -1, -1 }, { 5120, 3072, 1, -1 },
			{ 3072, 5120, -1, 1 }, { 5120, 5120, 1, 1 }
		};
		static const long depths[2][2] = { { 150, 400 }, { 40, 900 } };

		for (int c = 0; c < 4; c++)
		{
			for (int d = 0; d < 2; d++)
			{
				const long cx = corners[c][0], cz = corners[c][1];
				const long ox = corners[c][2], oz = corners[c][3];
				const long near_ = depths[d][0], far_ = depths[d][1];
				// one eye beside the block's x face, the other beside its z face
				const long a[3] = { cx + ox * near_, 256, cz - oz * far_ };
				const long b[3] = { cx - ox * far_, 256, cz + oz * near_ };

				for (long k = 0; k <= 64; k++)
				{
					const float alpha = (float)k / 64.0F;
					float got = -1.0F, best = 2.0F;
					long p[3], tests = 0;

					sweeps++;

					if (!ModernCameraChordClamp(a, b, alpha, h_chord_open, (void*)niche, &got, p, &tests))
					{
						missing++;
						continue;
					}

					if (tests > most_tests)
						most_tests = tests;

					if (got != alpha)
						moved++;

					if (h_solid(p[0], p[2], g_niche))
						bad_open++;

					if (!h_on_chord(a, b, got, p))
						bad_chord++;

					for (long j = 0; j <= 1024; j++)
					{
						const float t = (float)j / 1024.0F;
						long q[3];

						for (int i = 0; i < 3; i++)
							q[i] = InterpLerpLong(a[i], b[i], t);

						if (!h_solid(q[0], q[2], g_niche) && fabs((double)t - alpha) < best)
							best = (float)fabs((double)t - alpha);
					}

					if (fabs(fabs((double)got - alpha) - best) > 1.0 / 1024.0 + 1.0e-6)
						bad_near++;
				}
			}
		}

		printf("  measured: %ld blends over 8 chords past the 4 corners, %ld moved off the corner,\n",
			sweeps, moved);
		printf("            at most %ld tests of the map for one frame\n", most_tests);
		ok(missing == 0, "chord: two open tick eyes always leave an open point to draw", missing, 0);
		ok(moved > 0, "chord: and the chords really cut the pillar, so the sweep is not idle",
			moved, 1);
		ok(bad_open == 0, "chord: every point it answers is open", bad_open, 0);
		ok(bad_chord == 0, "chord: every point it answers lies on the chord, at its own factor",
			bad_chord, 0);
		ok(bad_near == 0, "chord: and is the nearest open point, to a 1024th of the chord",
			bad_near, 0);
		ok(most_tests <= 1 + 2 * MODERN_CAMERA_CHORD_SCAN + 2 * MODERN_CAMERA_CHORD_BISECT,
			"chord: the search stays inside its own budget of tests", most_tests,
			1 + 2 * MODERN_CAMERA_CHORD_SCAN + 2 * MODERN_CAMERA_CHORD_BISECT);
	}

	/* 2. An open chord: the frame's own factor comes back, after one test. */
	{
		const long a[3] = { 1500, 256, 1500 };
		const long b[3] = { 2500, 256, 1700 };
		long wrong = 0, extra = 0;

		for (long k = 0; k <= 64; k++)
		{
			const float alpha = (float)k / 64.0F;
			float got = -1.0F;
			long p[3], tests = 0;

			if (!ModernCameraChordClamp(a, b, alpha, h_chord_open, (void*)niche, &got, p, &tests) ||
				got != alpha || !h_on_chord(a, b, alpha, p))
				wrong++;

			if (tests != 1)
				extra++;
		}

		ok(wrong == 0, "chord: an open chord answers the frame's own factor unchanged", wrong, 0);
		ok(extra == 0, "chord: and asks the map once", extra, 0);
	}

	/* 3. Both ends solid. A chord inside the block has no answer; one whose
	   ends are in rock but whose middle crosses the open floor answers the
	   nearest open point of that middle, because the rule is the nearest open
	   point of the chord and a tick eye is only one of them. */
	{
		const long a[3] = { 3300, 256, 3300 };
		const long b[3] = { 4800, 256, 4900 };
		const long c[3] = { 3500, 256, 3500 };
		const long d[3] = { 500, 256, 3500 };
		float got = -1.0F;
		long p[3], tests = 0, found;

		found = ModernCameraChordClamp(a, b, 0.5F, h_chord_open, (void*)niche, &got, p, &tests);
		ok(found == 0, "chord: a chord solid from end to end has no answer", found, 0);

		found = ModernCameraChordClamp(c, d, 0.1F, h_chord_open, (void*)niche, &got, p, &tests);
		ok(found == 1 && !h_solid(p[0], p[2], g_niche) && p[0] >= 3071 - 1 && p[0] <= 3071,
			"chord: solid ends, open middle: the pillar's west face, a unit from it", p[0], 3071);
	}

	/* 4. One end solid: the answer lies toward the open end, at the face. */
	{
		const long a[3] = { 3500, 256, 3300 };	// in the pillar
		const long b[3] = { 3500, 256, 2500 };	// north of it, open
		float got = -1.0F;
		long p[3], tests = 0, found;

		found = ModernCameraChordClamp(a, b, 0.1F, h_chord_open, (void*)niche, &got, p, &tests);
		ok(found == 1 && got > 0.1F, "chord: one end in the pillar: the answer moves toward the open end",
			roundl2(got * 1000.0), 101);
		ok(found == 1 && p[2] >= 3071 - 1 && p[2] <= 3071,
			"chord: and stops at the pillar's north face, within a unit", p[2], 3071);
	}

	/* 5. The order of ModernCameraDrawnEye, over the two rooms above. */
	{
		MODERN_EYE_FIX fix;

		// Open from the tick's own room: nothing changes.
		{
			const long a[3] = { 1500, 256, 1500 };
			const long b[3] = { 2500, 256, 1700 };

			ModernCameraDrawnEye(a, 0, b, 0, 0.5F, h_eye_open, (void*)niche, &fix);
			ok(fix.fix == MODERN_EYE_FIX_NONE && fix.room == 0 && fix.alpha == 0.5F &&
				fix.x == InterpLerpLong(1500, 2500, 0.5F) && fix.tests == 1,
				"drawn eye: open from the tick's room - no fix, one test", fix.fix,
				MODERN_EYE_FIX_NONE);
		}

		// The new tick's room is 1, the eye still stands in room 0's half.
		{
			const long a[3] = { 3000, 256, 1500 };
			const long b[3] = { 4500, 256, 1500 };

			ModernCameraDrawnEye(a, 0, b, 1, 0.25F, h_eye_open, (void*)niche, &fix);
			ok(fix.fix == MODERN_EYE_FIX_PREVROOM,
				"drawn eye: the logic room refuses it, the previous tick's room holds it",
				fix.fix, MODERN_EYE_FIX_PREVROOM);
			ok(fix.room == 0 && fix.x == InterpLerpLong(3000, 4500, 0.25F) && fix.alpha == 0.25F,
				"drawn eye: the walk starts there and the eye does not move", fix.room, 0);
		}

		// The corner cut, across the two rooms: a moved eye, in the room that holds it.
		{
			const long a[3] = { 3072 - 150, 256, 3072 + 400 };
			const long b[3] = { 3072 + 400, 256, 3072 - 150 };
			short at = -1;

			ModernCameraDrawnEye(a, 0, b, 0, 0.5F, h_eye_open, (void*)niche, &fix);
			ok(fix.fix == MODERN_EYE_FIX_CHORD && fix.alpha != 0.5F,
				"drawn eye: in the pillar - drawn at the nearest open point instead",
				fix.fix, MODERN_EYE_FIX_CHORD);
			ok(h_eye_open((void*)niche, fix.x, fix.y, fix.z, fix.room, &at) == 1,
				"drawn eye: and that point opens from the room the walk starts in",
				fix.room, 0);
		}

		// The nearest open point is a tick eye itself.
		{
			const long a[3] = { 3071, 256, 3500 };	// the last open unit west of the pillar
			const long b[3] = { 5200, 256, 3500 };

			ModernCameraDrawnEye(a, 0, b, 1, 0.1F, h_eye_open, (void*)niche, &fix);
			ok(fix.fix == MODERN_EYE_FIX_TICK && fix.x == a[0] && fix.y == a[1] && fix.z == a[2] &&
				fix.room == 0,
				"drawn eye: the nearest open point is the older tick eye - drawn as a tick",
				fix.fix, MODERN_EYE_FIX_TICK);
		}

		// Nothing on the chord opens: the eye stays and the caller withholds the sky.
		{
			const long a[3] = { 3300, 256, 3300 };
			const long b[3] = { 3800, 256, 3900 };

			ModernCameraDrawnEye(a, 0, b, 0, 0.5F, h_eye_open, (void*)niche, &fix);
			ok(fix.fix == MODERN_EYE_FIX_NOSKY && fix.x == InterpLerpLong(3300, 3800, 0.5F) &&
				fix.alpha == 0.5F && fix.room == 0,
				"drawn eye: nothing on the chord opens - no move, the sky is withheld",
				fix.fix, MODERN_EYE_FIX_NOSKY);
		}
	}
}

/* ------------------------------ the tick eye pushed off the niche's walls ---

   game\camera.cpp's MoveCamera ends the Modern orbit's tick with the engine's
   own ModernCameraWallClearance (game\modern_camera.cpp, linked by bmt.bat):
   the eye moves square off every closed axis face nearer than the near
   rectangle's reach, to exactly that reach. The niche map above holds both
   shapes the rule meets: the pillar's four faces and the outer wall, whose
   inside corners are concave. The map's test answers in two dimensions, like
   h_chord_open, and counts its calls; a context can refuse one point or name
   the room an open point other than the eye resolves to. */

struct H_WALL
{
	const char* const* map;
	long tests;
	long eye_x, eye_z;		// the eye handed in, answered in its own room
	long refuse_x, refuse_z;	// a point declared closed although the map opens it; -1 none
	short moved_room;		// the room any other open point resolves to
};

/* Calls not made at the eye's height from the eye's room: the rule hands
   every probe and the moved point the eye's y and room, and h_clear's eye
   stands at 256 in room 0, so this stays nought. */
static long g_wall_wrong_args = 0;

static long h_wall_open(void* ctx, long x, long y, long z, short room, short* at)
{
	H_WALL* w = (H_WALL*)ctx;

	if (y != 256 || room != 0)
		g_wall_wrong_args++;

	w->tests++;
	*at = room;

	if (x == w->refuse_x && z == w->refuse_z)
		return 0;

	if (h_solid(x, z, w->map))
		return 0;

	if (x != w->eye_x || z != w->eye_z)
		*at = w->moved_room;

	return 1;
}

/* One call of the rule on the niche map, from room 0; the moved point
   resolves to room 5, so a room of 5 in the answer is the moved point's. */
static long h_clear(long x, long z, long radius, long out[3], short* room, long* tests,
	long refuse_x = -1, long refuse_z = -1)
{
	H_WALL w;
	const long eye[3] = { x, 256, z };
	long moved;

	w.map = g_niche;
	w.tests = 0;
	w.eye_x = x;
	w.eye_z = z;
	w.refuse_x = refuse_x;
	w.refuse_z = refuse_z;
	w.moved_room = 5;
	moved = ModernCameraWallClearance(eye, 0, radius, h_wall_open, &w, out, room);
	*tests = w.tests;
	return moved;
}

/* The engine's focal length as AlterFOV (specific\3dmath.cpp) builds it at
   the game's 80 degrees: half of winh * 320 / 240, times the cosine over the
   sine of 40 degrees, read from the sine table as 4096ths times four
   (phd_sin, specific\3dmath.h), in whole numbers. */
static long h_persp(long winh)
{
	const double a = (double)((80 * 182 / 2) >> 4) * 2.0 * 3.14159265358979323846 / 4096.0;
	const long s = 4 * roundl2(sin(a) * 4096.0);
	const long c = 4 * roundl2(cos(a) * 4096.0);

	return (winh * 320 / 240 / 2) * c / s;
}

static void camera_wall_clear()
{
	const long p1080 = h_persp(1080), p1440 = h_persp(1440);
	const long r169 = ModernCameraNearRadius(20, 960, 540, 858);
	const long r219 = ModernCameraNearRadius(20, 1280, 540, 858);
	const long R = r169;
	long out[3], tests, moved;
	short room;

	printf("\nthe tick eye pushed square off a wall, to the near rectangle's reach\n");
	printf("  measured: focal length %ld at 1080 lines and %ld at 1440; radius %ld at 1920x1080,\n",
		p1080, p1440, r169);
	printf("            %ld at 2560x1080, %ld at 2560x1440\n", r219,
		ModernCameraNearRadius(20, 1280, 720, p1440));

	/* 1. The radius, from the projection. */
	ok(p1080 == 858, "wall clear: AlterFOV's focal length at 1080 lines is 858", p1080, 858);
	ok(p1440 == 1144, "wall clear: and at 1440 lines 1144", p1440, 1144);
	ok(r169 == 33, "wall clear: the near rectangle's reach at 16:9 rounds up to 33", r169, 33);
	ok(r219 == 39, "wall clear: and at 21:9 to 39", r219, 39);
	ok(ModernCameraNearRadius(20, 1280, 720, p1440) == 33,
		"wall clear: 2560x1440 is 16:9 again, 33", ModernCameraNearRadius(20, 1280, 720, p1440), 33);
	ok(ModernCameraNearRadius(20, 960, 540, 0) == 0 && ModernCameraNearRadius(0, 960, 540, 858) == 0,
		"wall clear: no focal length or no near plane, no radius", ModernCameraNearRadius(20, 960, 540, 0), 0);

	/* 2. Each face of the pillar and of the outer wall, the eye 0, 1 and
	   R - 1 units from it: moved to exactly R on that axis and nowhere else.
	   A west face can stand 0 units away; an east or a south face is at
	   least 1, since the eye's own sector ends 1023 units past its edge. */
	{
		// face coordinate, axis (0 x, 1 z), side the eye stands on (+1 past it, -1 before it), the other axis
		static const long faces[6][4] = {
			{ 5120, 0, 1, 3500 },	// the pillar's east face, eye east of it
			{ 3072, 0, -1, 3500 },	// the pillar's west face, eye west of it
			{ 5120, 1, 1, 3500 },	// the pillar's south face, eye south of it
			{ 3072, 1, -1, 3500 },	// the pillar's north face, eye north of it
			{ 1024, 0, 1, 2500 },	// the outer wall's west face
			{ 7168, 1, -1, 2500 }	// the outer wall's south face
		};
		long wrong_pos = 0, wrong_other = 0, wrong_moved = 0, wrong_room = 0, cases = 0, most = 0;

		for (int f = 0; f < 6; f++)
		{
			const long d_list[3] = { 0, 1, R - 1 };

			for (int k = 0; k < 3; k++)
			{
				const long face = faces[f][0], axis = faces[f][1], side = faces[f][2];
				const long d = d_list[k];
				long x, z;

				if (side < 0 && d == 0)
					continue;	// the eye would stand in the closed sector itself

				x = axis ? faces[f][3] : face + side * d;
				z = axis ? face + side * d : faces[f][3];
				moved = h_clear(x, z, R, out, &room, &tests);
				cases++;

				if (tests > most)
					most = tests;

				if ((axis ? out[2] : out[0]) != face + side * R)
					wrong_pos++;

				if ((axis ? out[0] : out[2]) != (axis ? x : z) || out[1] != 256)
					wrong_other++;

				if (moved != R - d)
					wrong_moved++;

				if (room != 5)
					wrong_room++;
			}
		}

		printf("  measured: %ld eyes beside six faces, at most %ld tests of the map for one\n",
			cases, most);
		ok(cases == 15, "wall clear: fifteen eyes, 0, 1 and R - 1 units from six faces", cases, 15);
		ok(wrong_pos == 0, "wall clear: each lands exactly R from the face it was near", wrong_pos, 0);
		ok(wrong_other == 0, "wall clear: and the other axis and the height do not move", wrong_other, 0);
		ok(wrong_moved == 0, "wall clear: the answer is the units moved, R - distance", wrong_moved, 0);
		ok(wrong_room == 0, "wall clear: the room is the one the moved point resolved to", wrong_room, 0);
		ok(most <= 3, "wall clear: the eye, the face within R and the moved point - three tests at most",
			most, 3);
	}

	/* 3. At R and R + 1 the eye stays. */
	{
		long wrong = 0;

		for (long d = R; d <= R + 1; d++)
		{
			moved = h_clear(5120 + d, 3500, R, out, &room, &tests);

			if (moved != 0 || out[0] != 5120 + d || out[2] != 3500 || room != 0)
				wrong++;

			moved = h_clear(3072 - d, 3500, R, out, &room, &tests);

			if (moved != 0 || out[0] != 3072 - d || out[2] != 3500 || room != 0)
				wrong++;
		}

		ok(wrong == 0, "wall clear: an eye R or R + 1 from a face stays where it is, answer 0", wrong, 0);
	}

	/* 4. A concave corner: the outer wall's north-west inside corner, the eye
	   within R of both faces - moved on both axes. */
	moved = h_clear(1024 + 5, 1024 + 10, R, out, &room, &tests);
	ok(moved == (R - 5) + (R - 10) && out[0] == 1024 + R && out[2] == 1024 + R && room == 5,
		"wall clear: in a concave corner both axes move, each to R", moved, (R - 5) + (R - 10));

	/* 5. Near an edge whose neighbour is open: nothing moves, and an eye with
	   no edge within R asks the map nothing. */
	moved = h_clear(2048 + 3, 1500, R, out, &room, &tests);
	ok(moved == 0 && out[0] == 2048 + 3 && out[2] == 1500 && room == 0,
		"wall clear: an open neighbour is no face - the eye stays", moved, 0);
	moved = h_clear(1500, 1500, R, out, &room, &tests);
	ok(moved == 0 && tests == 0, "wall clear: an eye with no edge within R asks the map nothing", tests, 0);

	/* 6. An eye that is itself closed is not moved, and the test stops there. */
	moved = h_clear(3072 + 5, 3500, R, out, &room, &tests);
	ok(moved == 0 && out[0] == 3072 + 5 && out[2] == 3500 && room == 0 && tests == 1,
		"wall clear: an eye in the pillar itself - untouched, one test", tests, 1);

	/* 7. A moved point the room refuses: -1 and no move. */
	moved = h_clear(5120 + 4, 3500, R, out, &room, &tests, 5120 + R, 3500);
	ok(moved == -1 && out[0] == 5120 + 4 && out[2] == 3500 && room == 0,
		"wall clear: the moved point opens in no room - answer -1, the eye stays", moved, -1);

	/* 8. A radius the rule does not take. */
	ok(h_clear(5120, 3500, 0, out, &room, &tests) == 0 && out[0] == 5120,
		"wall clear: radius 0 moves nothing", out[0], 5120);
	ok(h_clear(5120, 3500, 513, out, &room, &tests) == 0 && out[0] == 5120,
		"wall clear: nor a radius over half a sector", out[0], 5120);

	/* 9. Every open point of the map on a grid of 7 units, at R and at the
	   21:9 radius: afterwards no closed axis face of its sector is nearer
	   than the radius, no axis moved further than the radius, and an eye
	   that had nothing within the radius did not move. */
	{
		long eyes = 0, pushed = 0, too_near = 0, too_far = 0, needless = 0, refused = 0;

		for (int ri = 0; ri < 2; ri++)
		{
			const long rad = ri ? r219 : r169;

			for (long x = 1024; x < 7168; x += 7)
			{
				for (long z = 1024; z < 7168; z += 7)
				{
					long near_any, ox, oz;

					if (h_solid(x, z, g_niche))
						continue;

					eyes++;
					moved = h_clear(x, z, rad, out, &room, &tests);

					if (moved < 0)
					{
						refused++;
						continue;
					}

					if (moved > 0)
						pushed++;

					ox = out[0];
					oz = out[2];

					if ((h_solid((ox & ~1023) - 1, oz, g_niche) && (ox & 1023) < rad) ||
						(h_solid((ox | 1023) + 1, oz, g_niche) && 1024 - (ox & 1023) < rad) ||
						(h_solid(ox, (oz & ~1023) - 1, g_niche) && (oz & 1023) < rad) ||
						(h_solid(ox, (oz | 1023) + 1, g_niche) && 1024 - (oz & 1023) < rad))
						too_near++;

					if (labs(ox - x) > rad || labs(oz - z) > rad)
						too_far++;

					near_any = (h_solid((x & ~1023) - 1, z, g_niche) && (x & 1023) < rad) ||
						(h_solid((x | 1023) + 1, z, g_niche) && 1024 - (x & 1023) < rad) ||
						(h_solid(x, (z & ~1023) - 1, g_niche) && (z & 1023) < rad) ||
						(h_solid(x, (z | 1023) + 1, g_niche) && 1024 - (z & 1023) < rad);

					if (!near_any && (ox != x || oz != z))
						needless++;
				}
			}
		}

		printf("  measured: %ld open eyes on a 7-unit grid at radius %ld and %ld, %ld pushed\n",
			eyes, r169, r219, pushed);
		ok(pushed > 0, "wall clear: the sweep really meets faces, so it is not idle", pushed, 1);
		ok(refused == 0, "wall clear: no moved point on the map is refused", refused, 0);
		ok(too_near == 0, "wall clear: afterwards no closed face is nearer than the radius", too_near, 0);
		ok(too_far == 0, "wall clear: and no axis moved further than the radius", too_far, 0);
		ok(needless == 0, "wall clear: an eye with nothing within the radius never moves", needless, 0);
	}

	ok(g_wall_wrong_args == 0,
		"wall clear: every probe and the moved point are asked at the eye's height from the eye's room",
		g_wall_wrong_args, 0);
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
	 * turn - and in no other family but the hold-on one. */
	ok(ModernStateIsClassic(139) == 1, "ledge: AS_LEDGEHANG stays classic", ModernStateIsClassic(139), 1);
	ok(ModernStateIsGrab(139) == 1, "ledge: ...in the hold-on family, a transition into the hang",
		ModernStateIsGrab(139), 1);
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
	printf("  ptr     modern: the back input, running against the hop back\n");
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

/* ------------------------------------------ the mouse turns the same orbit ----- */

/* Her held course after one tick in which the orbit moved by `step`, handed on
 * the way game\modern_state.cpp hands it: the course is seated by ten ticks of
 * the left stick held forward, then the camera turns by the step and the
 * situation carries it as the player's own. */
static long mouse_course_step(short step)
{
	MODERN_CFG cfg;
	MODERN_STATE state;
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short cam = 0;
	short before;
	long t;

	ModernDefaults(&cfg);
	ModernResetState(&state);
	sit.anim_state = 1;		/* AS_RUN */
	sit.classic = 0;
	sit.drawn = 0;
	sit.action = 0;
	sit.jump = 0;
	sit.blocked = 0;
	sit.cam_player_step = 0;
	sit.cam_cut = 0;

	for (t = 0; t < 10; t++)
		ModernTranslate(&cfg, &state, 0, -120, cam, 0, &sit, &out);

	before = state.course;
	cam = (short)ModernWrap((long)cam + step);
	sit.cam_player_step = step;
	ModernTranslate(&cfg, &state, 0, -120, cam, 0, &sit, &out);

	return ModernWrap((long)state.course - before);
}

/* The mouse is a second input to the orbit, not a second orbit: its counts
 * become two steps (ModernCameraMouseSteps), the steps go into the state beside
 * the stick's axes, and everything after that is the stick's own rule. So the
 * checks below hold the gain to the numbers game\modern_camera.h states, and
 * then hold a mouse tick against a stick tick of the same size. */
static void camera_mouse()
{
	MODERN_CAMERA_CFG cfg;
	MODERN_CAMERA_CFG fast;
	MODERN_CAMERA_STATE a;
	MODERN_CAMERA_STATE b;
	long s, y, p, yi, pi, dx, dy, heading, t;
	long even = 0;

	printf("\nthe mouse - a second input to the same orbit\n");

	ModernCameraDefaults(&cfg);

	/* 1. The gain. */
	ok(ModernCameraMouseGain(MODERN_CAMERA_MOUSE_SENS_DEFAULT) == 32 * MODERN_CAMERA_MOUSE_ONE,
		"mouse: the middle of the slider is 32 units a count",
		ModernCameraMouseGain(MODERN_CAMERA_MOUSE_SENS_DEFAULT), 32 * MODERN_CAMERA_MOUSE_ONE);
	ok(2048 * ModernCameraMouseGain(50) / MODERN_CAMERA_MOUSE_ONE == 65536,
		"mouse: so 2048 counts turn the camera once round",
		2048 * ModernCameraMouseGain(50) / MODERN_CAMERA_MOUSE_ONE, 65536);
	ok(4 * ModernCameraMouseGain(0) == ModernCameraMouseGain(50),
		"mouse: the bottom of the slider is a quarter of the middle",
		4 * ModernCameraMouseGain(0), ModernCameraMouseGain(50));
	ok(ModernCameraMouseGain(100) == 4 * ModernCameraMouseGain(50),
		"mouse: the top of the slider is four times the middle",
		ModernCameraMouseGain(100), 4 * ModernCameraMouseGain(50));
	ok(ModernCameraMouseGain(-7) == ModernCameraMouseGain(0),
		"mouse: under the bottom is the bottom", ModernCameraMouseGain(-7), ModernCameraMouseGain(0));
	ok(ModernCameraMouseGain(900) == ModernCameraMouseGain(100),
		"mouse: over the top is the top", ModernCameraMouseGain(900), ModernCameraMouseGain(100));

	for (s = 0; s + 25 <= 100; s++)
		ok(ModernCameraMouseGain(s + 25) == 2 * ModernCameraMouseGain(s),
			"mouse: twenty-five steps of the slider double the gain, all along it",
			ModernCameraMouseGain(s + 25), 2 * ModernCameraMouseGain(s));

	for (s = 1; s <= 100; s++)
	{
		const long ratio = 10000 * ModernCameraMouseGain(s) / ModernCameraMouseGain(s - 1);

		ok(ModernCameraMouseGain(s) > ModernCameraMouseGain(s - 1),
			"mouse: every step of the slider raises the gain",
			ModernCameraMouseGain(s), ModernCameraMouseGain(s - 1));

		/* 2^(1/25) = 1.0281: each step is the same fraction of the last, which
		 * is what makes the slider feel even along its length. */
		if (ratio < 10270 || ratio > 10292)
			even++;
	}

	ok(even == 0, "mouse: every step of the slider is worth the same 2.8 per cent", even, 0);

	/* 2. The steps. */
	ModernCameraMouseSteps(50, 0, 50, 0, &y, &p);
	ok(y == 1600 && p == 0, "mouse: 50 counts to the right at 50 are 1600 units of yaw and no pitch", y, 1600);
	ModernCameraMouseSteps(-50, 0, 50, 0, &y, &p);
	ok(y == -1600, "mouse: to the left is exactly the other way", y, -1600);
	ModernCameraMouseSteps(0, -50, 50, 0, &y, &p);
	ok(p == 1600 && y == 0,
		"mouse: pushed away raises the elevation - it looks up, as the stick pushed forward does", p, 1600);
	ModernCameraMouseSteps(0, 50, 50, 0, &y, &p);
	ok(p == -1600, "mouse: pulled back looks down", p, -1600);
	ModernCameraMouseSteps(1, 0, 0, 0, &y, &p);
	ok(y == 8, "mouse: one count at the bottom of the slider is still 8 units - no count is lost", y, 8);
	ModernCameraMouseSteps(100000, -100000, 100, 0, &y, &p);
	ok(y == MODERN_CAMERA_MOUSE_STEP_MAX && p == MODERN_CAMERA_MOUSE_STEP_MAX,
		"mouse: raw counts handed straight in are still held to the step bound, both ways",
		y, MODERN_CAMERA_MOUSE_STEP_MAX);
	ModernCameraMouseSteps(-100000, 100000, 100, 0, &y, &p);
	ok(y == -MODERN_CAMERA_MOUSE_STEP_MAX && p == -MODERN_CAMERA_MOUSE_STEP_MAX,
		"mouse: and the other way round", y, -MODERN_CAMERA_MOUSE_STEP_MAX);

	/* 2b. The spend: a tick spends at most the counts of one bounded step and
	 * owes the rest. Swept over the whole slider, the largest spend turns the
	 * camera by the bound or just under it, and one count more would pass it -
	 * so the bound is reached and never exceeded. */
	for (s = 0; s <= 100; s++)
	{
		long owed = 0;
		long most = ModernCameraMouseSpend(1000000, s, &owed);
		long gain = ModernCameraMouseGain(s);

		ModernCameraMouseSteps(most, -most, s, 0, &y, &p);
		ok(y <= MODERN_CAMERA_MOUSE_STEP_MAX && p <= MODERN_CAMERA_MOUSE_STEP_MAX,
			"mouse: the largest spend never makes a step over the bound", y, MODERN_CAMERA_MOUSE_STEP_MAX);
		ok((most + 1) * gain / MODERN_CAMERA_MOUSE_ONE > MODERN_CAMERA_MOUSE_STEP_MAX,
			"mouse: and one count more would", (most + 1) * gain / MODERN_CAMERA_MOUSE_ONE,
			MODERN_CAMERA_MOUSE_STEP_MAX);
		ok(owed > 0 && owed * gain / MODERN_CAMERA_MOUSE_ONE <= MODERN_CAMERA_MOUSE_OWED_MAX,
			"mouse: a jolt leaves at most half a turn owed", owed * gain / MODERN_CAMERA_MOUSE_ONE,
			MODERN_CAMERA_MOUSE_OWED_MAX);
	}

	{
		long owed = 0;

		ok(ModernCameraMouseSpend(1000, 50, &owed) == 64 && owed == 936,
			"mouse: at 50 a tick spends 64 counts and owes the rest", owed, 936);
		ok(ModernCameraMouseSpend(0, 50, &owed) == 64 && owed == 872,
			"mouse: the next tick pays 64 more without a new count", owed, 872);
		ok(ModernCameraMouseSpend(-900, 50, &owed) == -28 && owed == 0,
			"mouse: a move back is set against what is owed", owed, 0);
		owed = 0;
		ok(ModernCameraMouseSpend(-5, 50, &owed) == -5 && owed == 0,
			"mouse: a small move is spent whole, nothing owed", owed, 0);
		owed = 0;
		ok(ModernCameraMouseSpend(100000, 50, &owed) == 64 && owed == 1024,
			"mouse: what is owed stops at half a turn, 1024 counts at 50", owed, 1024);
		owed = 0;
		ModernCameraMouseSpend(1, 100, &owed);
		ok(owed == 0, "mouse: one count at 100 is spent at once", owed, 0);
		owed = 0;
		y = ModernCameraMouseSpend(1000, 100, &owed);
		owed = 0;
		p = ModernCameraMouseSpend(1000, 0, &owed);
		ok(y == 16 && p == 256, "mouse: 16 counts a tick at the top of the slider, 256 at the bottom", y, 16);
	}

	/* No count is lost under the half-turn bound: a sweep of 2000 counts at 50,
	 * handed in as 100 a tick for 20 ticks - 3000 counts a second, about 9.5 cm
	 * a second at 800 counts per inch, faster than one tick's bound of 64 -
	 * turns the camera by exactly 2000 counts times the gain once the ticks
	 * after it have paid out, and no single tick's step passes the bound. */
	{
		long owed = 0;
		long total_counts = 0;
		long total_yaw = 0;
		long worst = 0;

		for (t = 0; t < 60; t++)
		{
			const long spent = ModernCameraMouseSpend(t < 20 ? 100 : 0, 50, &owed);

			ModernCameraMouseSteps(spent, 0, 50, 0, &y, &p);
			total_counts += spent;
			total_yaw += y;

			if (absl(y) > worst)
				worst = absl(y);
		}

		ok(total_counts == 2000 && owed == 0, "mouse: a brisk sweep is paid out whole", total_counts, 2000);
		ok(total_yaw == 2000 * 32, "mouse: and turns the camera as far as the hand went", total_yaw, 2000 * 32);
		ok(worst == MODERN_CAMERA_MOUSE_STEP_MAX, "mouse: never faster than the bound", worst,
			MODERN_CAMERA_MOUSE_STEP_MAX);
	}

	/* The invert, swept: it flips the pitch and touches nothing else. */
	for (s = 0; s <= 100; s += 25)
		for (dx = -300; dx <= 300; dx += 37)
			for (dy = -300; dy <= 300; dy += 41)
			{
				ModernCameraMouseSteps(dx, dy, s, 0, &y, &p);
				ModernCameraMouseSteps(dx, dy, s, 1, &yi, &pi);
				ok(pi == -p, "mouse: the invert flips the pitch", pi, -p);
				ok(yi == y, "mouse: and leaves the yaw alone", yi, y);
			}

	/* 3. A mouse tick against a stick tick of the same size. The stick is
	 * given a rate of exactly what 50 counts at 50 are, so the two inputs ask
	 * for the same step and everything after must agree. */
	fast = cfg;
	fast.yaw_rate = 1600;
	fast.pitch_rate = 1600;

	for (heading = -32768; heading < 32768; heading += 4099)
	{
		ModernCameraReset(&a);
		ModernCameraUpdate(&fast, &a, fast.stick_full, -fast.stick_full, heading, 0, 0, 0, 0);

		ModernCameraReset(&b);
		ModernCameraMouseSteps(50, -50, 50, 0, &y, &p);
		b.mouse_yaw = y;
		b.mouse_pitch = p;
		ModernCameraUpdate(&cfg, &b, 0, 0, heading, 0, 0, 0, 0);

		ok(b.world_yaw == a.world_yaw,
			"mouse: 50 counts at 50 turn the world yaw as far as the stick tick does",
			b.world_yaw, a.world_yaw);
		ok(b.world_yaw == ModernWrap(heading + 1600),
			"mouse: which is the counts times the gain", b.world_yaw, ModernWrap(heading + 1600));
		ok(b.offset_pitch == a.offset_pitch,
			"mouse: and raise the elevation as far as the stick pushed forward",
			b.offset_pitch, a.offset_pitch);
		ok(b.stick_step == a.stick_step && b.stick_step == 1600,
			"mouse: the step handed to her held course is the stick's", b.stick_step, a.stick_step);
	}

	ok(mouse_course_step(1600) == 1600,
		"mouse: her held course takes that step whole", mouse_course_step(1600), 1600);
	ok(mouse_course_step(-1600) == -1600,
		"mouse: and the other way", mouse_course_step(-1600), -1600);
	ok(mouse_course_step(0) == 0,
		"mouse: while a tick without a step leaves it where it is", mouse_course_step(0), 0);

	/* The stick and the mouse at once add up. */
	ModernCameraReset(&b);
	b.mouse_yaw = 320;
	ModernCameraUpdate(&cfg, &b, cfg.stick_full, 0, 0, 0, 0, 0, 0);
	ok(b.stick_step == cfg.yaw_rate + 320,
		"mouse: the stick and the mouse at once add their steps", b.stick_step, cfg.yaw_rate + 320);

	/* 4. A mouse step is steering: it cancels a swing and stands the return
	 * still, as the stick does. */
	ModernCameraReset(&b);
	ModernCameraCarry(&cfg, &b, 32768);
	ok(b.carry_total != 0, "mouse: a swing is owed before the mouse moves", b.carry_total, 32768);
	b.mouse_yaw = 32;
	ModernCameraUpdate(&cfg, &b, 0, 0, 0, 1, 0, 0, 0);
	ok(b.steering == 1, "mouse: a mouse step is steering", b.steering, 1);
	ok(b.carry == 0 && b.carry_total == 0, "mouse: it cancels the swing", b.carry_total, 0);
	ok(b.ret == 0, "mouse: and the return stands still while she runs", b.ret, 0);

	ModernCameraReset(&b);
	b.mouse_pitch = -32;
	ModernCameraUpdate(&cfg, &b, 0, 0, 0, 1, 0, 0, 0);
	ok(b.steering == 1, "mouse: a pitch step alone is steering too", b.steering, 1);

	/* The next tick has no hand-over: not steering, and the return runs. */
	ModernCameraReset(&b);
	b.mouse_yaw = 8192;
	ModernCameraUpdate(&cfg, &b, 0, 0, 0, 0, 0, 0, 0);
	ok(b.mouse_yaw == 0 && b.mouse_pitch == 0, "mouse: the update clears the hand-over", b.mouse_yaw, 0);
	a = b;
	ModernCameraUpdate(&cfg, &b, 0, 0, 0, 0, 0, 0, 0);
	ok(b.world_yaw == a.world_yaw, "mouse: a tick without a hand-over turns nothing", b.world_yaw, a.world_yaw);
	ModernCameraUpdate(&cfg, &b, 0, 0, 0, 1, 0, 0, 0);
	ok(b.steering == 0 && b.ret != 0, "mouse: and once she runs, the return falls home again", b.ret, 1);

	/* 5. The elevation clamp holds the mouse as it holds the stick. From the
	 * resting -1820 the largest step needs nine ticks to pass 15470, so twenty
	 * reach the clamp either way with room to spare. */
	ModernCameraReset(&b);

	for (t = 0; t < 20; t++)
	{
		b.mouse_pitch = MODERN_CAMERA_MOUSE_STEP_MAX;
		ModernCameraUpdate(&cfg, &b, 0, 0, 0, 0, 0, 0, 0);
	}

	ok(b.offset_pitch == cfg.pitch_max, "mouse: the elevation stops at ChaseCamera's clamp below her",
		b.offset_pitch, cfg.pitch_max);

	for (t = 0; t < 20; t++)
	{
		b.mouse_pitch = -MODERN_CAMERA_MOUSE_STEP_MAX;
		ModernCameraUpdate(&cfg, &b, 0, 0, 0, 0, 0, 0, 0);
	}

	ok(b.offset_pitch == cfg.pitch_min, "mouse: and at the 60 degree cap above her",
		b.offset_pitch, cfg.pitch_min);

	/* 6. The largest step wraps the short way: the bound from near the half
	 * turn lands on the other side, not a turn back. */
	ModernCameraReset(&b);
	b.offset_yaw = 32000;
	b.mouse_yaw = MODERN_CAMERA_MOUSE_STEP_MAX;
	ModernCameraUpdate(&cfg, &b, 0, 0, 0, 0, 0, 0, 0);
	ok(b.world_yaw == ModernWrap(32000 + MODERN_CAMERA_MOUSE_STEP_MAX),
		"mouse: the largest step past the half turn wraps round", b.world_yaw,
		ModernWrap(32000 + MODERN_CAMERA_MOUSE_STEP_MAX));

	/* 7. A reset forgets a hand-over as it forgets the rest. */
	b.mouse_yaw = 5;
	b.mouse_pitch = 5;
	ModernCameraReset(&b);
	ok(b.mouse_yaw == 0 && b.mouse_pitch == 0, "mouse: a reset clears the hand-over", b.mouse_yaw, 0);
}

/* The HD/SD switch (specific\hd\hd_switch.h): one flip per press, none for
 * a held key, none outside a level or under a menu, nothing without HD
 * data, and a state that holds across the loads of levels with and
 * without HD data - because nothing but a press or the row moves it.
 * `run` feeds a picture of the readings: 'k' down live, '.' up live, 'm'
 * down under a menu, ',' up under a menu. */
static int hd_switch_run(HDSwitchState* s, const char* run, int hd,
	int* flips, int* nothing)
{
	*flips = 0;
	*nothing = 0;

	for (const char* p = run; *p; p++)
	{
		const int down = *p == 'k' || *p == 'm';
		const int live = *p == 'k' || *p == '.';
		const int what = HDSwitchStep(s, down, live, hd);

		*flips += what == HD_SWITCH_FLIPPED;
		*nothing += what == HD_SWITCH_NOTHING;
	}

	return s->drawn;
}

static void hd_switch()
{
	HDSwitchState s = HD_SWITCH_START;
	int flips, nothing;

	ok(s.drawn == 1 && s.held == 0, "hd switch: the tools' start is HD",
		s.drawn, 1);

	// One press, one flip; a key held for a while is still one press.
	hd_switch_run(&s, ".kkkkkk.", 1, &flips, &nothing);
	ok(flips == 1 && s.drawn == 0, "hd switch: a held key flips once",
		flips, 1);

	// Two presses, two flips, and the draw is classic again.
	hd_switch_run(&s, "k.k.", 1, &flips, &nothing);
	ok(flips == 2 && s.drawn == 0, "hd switch: two presses, two flips",
		flips, 2);

	// Under a menu a press switches nothing, and a key still down when the
	// menu closes is no new press.
	hd_switch_run(&s, ",mmmkkk.", 1, &flips, &nothing);
	ok(flips == 0 && s.drawn == 0, "hd switch: no flip from under a menu",
		flips, 0);

	// Key and pad are one reading: down on either, then both, is one press.
	{
		HDSwitchState t = HD_SWITCH_START;
		int key = 0, pad = 0, n = 0;
		const int seq[6][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 },
			{ 0, 1 }, { 0, 0 } };

		for (int i = 0; i < 6; i++)
		{
			key = seq[i][0];
			pad = seq[i][1];
			n += HDSwitchStep(&t, key || pad, 1, 1) == HD_SWITCH_FLIPPED;
		}

		ok(n == 2 && t.drawn == 1, "hd switch: key and pad share one edge",
			n, 2);
	}

	// Without HD data a press flips nothing and says so, once per press.
	hd_switch_run(&s, "k.kk.", 0, &flips, &nothing);
	ok(flips == 0 && nothing == 2 && s.drawn == 0,
		"hd switch: no HD data, nothing to switch", nothing, 2);

	/* Across loads, from a fresh start: drawn classic in one level, a level
	 * without HD data in between (a press there changes nothing), and the
	 * next HD level is still drawn classic until a press. */
	s = HDSwitchState HD_SWITCH_START;
	hd_switch_run(&s, "k.", 1, &flips, &nothing);
	ok(s.drawn == 0, "hd switch: classic in the first level", s.drawn, 0);
	hd_switch_run(&s, "....", 1, &flips, &nothing);
	ok(s.drawn == 0, "hd switch: a load does not move it", s.drawn, 0);
	hd_switch_run(&s, "k.", 0, &flips, &nothing);
	ok(s.drawn == 0, "hd switch: a level without HD keeps it", s.drawn, 0);
	hd_switch_run(&s, "....", 1, &flips, &nothing);
	ok(s.drawn == 0, "hd switch: the next HD level is classic still",
		s.drawn, 0);
	hd_switch_run(&s, "k.", 1, &flips, &nothing);
	ok(s.drawn == 1, "hd switch: and a press brings HD back", s.drawn, 1);

	// A sweep: n presses leave the draw at HD exactly when n is even.
	for (int n = 0; n < 9; n++)
	{
		HDSwitchState t = HD_SWITCH_START;

		for (int i = 0; i < n; i++)
			hd_switch_run(&t, "kk.", 1, &flips, &nothing);

		ok(t.drawn == !(n & 1), "hd switch: n presses, n flips", t.drawn,
			!(n & 1));
	}
}

/* The HD textures row and the key as one state (hd_switch.h): the state is
 * the setting hd_enable, a press flips it by HDSwitchStep and a step of the
 * row by HDSwitchSet to the other value, each with `hd` the one data test,
 * HDSwitchHasData. The data are built at the load whenever the build gate
 * is open - hd_build and an installation found - and the level's .TRG has
 * something for it; the row (the draw) is no part of that. Then the world
 * the room gates draw: HDLevelActive is the rooms' build and the state. */
struct hd_option_world
{
	int build;		// tomb5.hd_build
	int install;	// an installation found
	int level;		// the level's .TRG has HD rooms for it
	HDSwitchState s;	// s.drawn is tomb5.hd_enable, the row
};

static int hd_option_data(const hd_option_world* w)
{
	return w->build && w->install && w->level;
}

static int hd_option_level_active(const hd_option_world* w)
{
	return hd_option_data(w) && w->s.drawn;
}

// One press of the key, live: down, then up.
static int hd_option_press(hd_option_world* w)
{
	const int what = HDSwitchStep(&w->s, 1, 1, hd_option_data(w));

	HDSwitchStep(&w->s, 0, 1, hd_option_data(w));
	return what;
}

// One step of the row: the state, which is the row's value, to the other.
static int hd_option_step(hd_option_world* w)
{
	return HDSwitchSet(&w->s, !w->s.drawn, hd_option_data(w));
}

static void hd_switch_option()
{
	// The setter alone: every state, both values, with and without data.
	for (int was = 0; was < 2; was++)
		for (int on = 0; on < 2; on++)
			for (int hd = 0; hd < 2; hd++)
				for (int held = 0; held < 2; held++)
				{
					HDSwitchState s = { was, held };
					const int what = HDSwitchSet(&s, on, hd);
					const int want = !hd ? HD_SWITCH_NOTHING :
						was != on ? HD_SWITCH_FLIPPED : HD_SWITCH_NONE;

					ok(s.drawn == on, "hd option: the draw takes the row's "
						"value", s.drawn, on);
					ok(what == want, "hd option: a flip only on a change, "
						"nothing without data", what, want);
					ok(s.held == held, "hd option: the key's held state is "
						"left alone", s.held, held);
				}

	/* The new default: the row off (hd_enable 0), the data built because
	 * an installation is found. The game starts classic; the key flips it
	 * HD at once, the row then reads on; the row steps it back. */
	{
		hd_option_world w = { 1, 1, 1, { 0, 0 } };

		ok(hd_option_level_active(&w) == 0, "hd option: row off, the "
			"game starts classic", hd_option_level_active(&w), 0);
		ok(hd_option_press(&w) == HD_SWITCH_FLIPPED && w.s.drawn == 1 &&
			hd_option_level_active(&w) == 1, "hd option: the key with the "
			"row off draws HD", w.s.drawn, 1);
		ok(hd_option_step(&w) == HD_SWITCH_FLIPPED && w.s.drawn == 0 &&
			hd_option_level_active(&w) == 0, "hd option: the row after the "
			"key steps it classic", w.s.drawn, 0);
		ok(hd_option_step(&w) == HD_SWITCH_FLIPPED && w.s.drawn == 1 &&
			hd_option_level_active(&w) == 1, "hd option: the row on draws "
			"HD", w.s.drawn, 1);
		ok(hd_option_press(&w) == HD_SWITCH_FLIPPED &&
			hd_option_level_active(&w) == 0, "hd option: the key with the "
			"row on flips classic", w.s.drawn, 0);
	}

	/* No hidden state: a level loaded with the row off has its data, so
	 * a press there switches, and the row shows it. The case of a press in
	 * a level loaded without HD data while the row said on cannot be made
	 * any more: the row is not asked by the build. */
	for (int drawn = 0; drawn < 2; drawn++)
	{
		hd_option_world w = { 1, 1, 1, { drawn, 0 } };

		hd_option_press(&w);
		ok(hd_option_level_active(&w) == w.s.drawn, "hd option: the world "
			"is the row after a press", hd_option_level_active(&w),
			w.s.drawn);
		ok(w.s.drawn == !drawn, "hd option: a level loaded with either "
			"value switches", w.s.drawn, !drawn);
	}

	// The build gate off: the key does nothing, the row moves the setting.
	{
		hd_option_world w = { 0, 1, 1, { 1, 0 } };

		ok(hd_option_press(&w) == HD_SWITCH_NOTHING && w.s.drawn == 1,
			"hd option: hd_build 0, the key does nothing", w.s.drawn, 1);
		ok(hd_option_step(&w) == HD_SWITCH_NOTHING && w.s.drawn == 0 &&
			hd_option_level_active(&w) == 0, "hd option: hd_build 0, the "
			"row moves the setting, nothing drawn HD", w.s.drawn, 0);
	}

	// No installation: the same.
	{
		hd_option_world w = { 1, 0, 1, { 1, 0 } };

		ok(hd_option_press(&w) == HD_SWITCH_NOTHING && w.s.drawn == 1,
			"hd option: no install, the key does nothing", w.s.drawn, 1);
		ok(hd_option_step(&w) == HD_SWITCH_NOTHING && w.s.drawn == 0,
			"hd option: no install, the row moves the setting", w.s.drawn,
			0);
		ok(hd_option_level_active(&w) == 0, "hd option: no install, no "
			"HD level", hd_option_level_active(&w), 0);
	}

	/* A level the .TRG has nothing for: nothing to switch there; the next
	 * level with HD data is drawn as the setting stands. */
	{
		hd_option_world w = { 1, 1, 0, { 0, 0 } };

		ok(hd_option_press(&w) == HD_SWITCH_NOTHING && w.s.drawn == 0,
			"hd option: a level without data, the key does nothing",
			w.s.drawn, 0);
		ok(hd_option_step(&w) == HD_SWITCH_NOTHING && w.s.drawn == 1,
			"hd option: a level without data, the row sets HD", w.s.drawn,
			1);
		w.level = 1;
		ok(hd_option_level_active(&w) == 1, "hd option: the next level with "
			"data is drawn HD", hd_option_level_active(&w), 1);
	}

	// A held key and the row's step are two edges, never one: the key
	// flips at its edge, the row's steps flip, the key still down is no
	// new press, and only the next press flips again.
	{
		hd_option_world w = { 1, 1, 1, HD_SWITCH_START };
		int n = 0;

		n += HDSwitchStep(&w.s, 1, 1, 1) == HD_SWITCH_FLIPPED;
		hd_option_step(&w);
		hd_option_step(&w);
		n += HDSwitchStep(&w.s, 1, 1, 1) == HD_SWITCH_FLIPPED;
		n += HDSwitchStep(&w.s, 1, 0, 1) == HD_SWITCH_FLIPPED;
		ok(n == 1 && w.s.drawn == 0, "hd option: a held key across the "
			"row's steps is one press", n, 1);
		HDSwitchStep(&w.s, 0, 1, 1);
		n += HDSwitchStep(&w.s, 1, 1, 1) == HD_SWITCH_FLIPPED;
		ok(n == 2 && w.s.drawn == 1, "hd option: the next press flips",
			n, 2);
	}

	/* The truth table, swept: hd_build, installation, the level's data and
	 * the row, then a step of the row and a press of the key. The row's
	 * step always moves the value; the world is HD exactly with the data
	 * and the value; the press flips exactly with the data, and the world
	 * follows it there. */
	for (int build = 0; build < 2; build++)
		for (int install = 0; install < 2; install++)
			for (int level = 0; level < 2; level++)
				for (int row = 0; row < 2; row++)
				{
					hd_option_world w = { build, install, level, { row, 0 } };
					const int data = build && install && level;

					ok(hd_option_level_active(&w) == (data && row), "hd "
						"option table: the start draws the row where there "
						"are data", hd_option_level_active(&w), data && row);

					const int set = hd_option_step(&w);

					ok(w.s.drawn == !row && set == (data ? HD_SWITCH_FLIPPED :
						HD_SWITCH_NOTHING), "hd option table: the row's step "
						"moves the value, a flip with data", w.s.drawn, !row);
					ok(hd_option_level_active(&w) == (data && !row), "hd "
						"option table: the world is the row where there are "
						"data", hd_option_level_active(&w), data && !row);

					const int what = hd_option_press(&w);

					ok((what == HD_SWITCH_FLIPPED) == data, "hd option "
						"table: the key flips exactly with data",
						what, data ? HD_SWITCH_FLIPPED : HD_SWITCH_NOTHING);
					ok(w.s.drawn == (data ? row : !row), "hd option table: "
						"the value after the key", w.s.drawn,
						data ? row : !row);
					ok(hd_option_level_active(&w) == (data && w.s.drawn),
						"hd option table: the world follows the key",
						hd_option_level_active(&w), data && w.s.drawn);
				}
}

/* Opens a file of the tree by its path relative to Diagnose\, the folder the
 * EXE is built in: first beside the EXE (_get_pgmptr, the EXE's full path),
 * so that a start from any working directory reads the same sources, then
 * relative to the working directory. 0 when neither opens. */
static FILE* tree_open(const char* rel)
{
	FILE* f = 0;
	char* exe = 0;
	char path[1024];

	if (!_get_pgmptr(&exe) && exe)
	{
		const char* cut = strrchr(exe, '\\');
		const size_t dir = cut ? (size_t)(cut - exe) + 1 : 0;

		if (dir && dir + strlen(rel) < sizeof(path))
		{
			memcpy(path, exe, dir);
			strcpy_s(path + dir, sizeof(path) - dir, rel);

			if (!fopen_s(&f, path, "rb") && f)
				return f;
		}
	}

	if (fopen_s(&f, rel, "rb") || !f)
		return 0;

	return f;
}

/* The switch's wiring, in the engine's own text. The gates that ask
 * HDDrawn() live in hd_level.cpp, hd_object.cpp, hd_laradraw.cpp and
 * hd_life.cpp, which need the game and are not linked here, so their lines
 * are counted where they stand: the central question with HDDrawn() in it,
 * every gate of the HD world asking it, and the readers that must not
 * switch - the build, the flips, the F4 changer's rebuild, the inventory
 * ring, the life built at the load - still asking what they asked. A gate
 * that a later change takes back shows here as a count. */
static char* hd_switch_text(const char* name)
{
	char path[256];

	snprintf(path, sizeof(path), "../../../TOMB5 Code/TOMB5/specific/hd/%s",
		name);

	FILE* f = tree_open(path);

	if (!f)
		return 0;

	fseek(f, 0, SEEK_END);

	const long size = ftell(f);
	char* text = (char*)malloc((size_t)size + 1);

	fseek(f, 0, SEEK_SET);

	if (text)
		text[fread(text, 1, (size_t)size, f)] = 0;

	fclose(f);
	return text;
}

static int hd_switch_count(const char* text, const char* what)
{
	int n = 0;

	for (const char* p = text ? strstr(text, what) : 0; p;
		p = strstr(p + 1, what))
		n++;

	return n;
}

static void hd_switch_wiring()
{
	char* level = hd_switch_text("hd_level.cpp");
	char* object = hd_switch_text("hd_object.cpp");
	char* lara = hd_switch_text("hd_laradraw.cpp");
	char* life = hd_switch_text("hd_life.cpp");
	int n;

	ok(level && object && lara && life, "hd switch wiring: the four files "
		"are read", !!level + !!object + !!lara + !!life, 4);

	n = hd_switch_count(level, "return g_level_active && HDDrawn();");
	ok(n == 1, "hd switch wiring: HDLevelActive asks HDDrawn", n, 1);

	// GroupOfRoom, HDRoomletDropped, HDRoomShadowHD, HDRoomArtListed,
	// HDRoomCarries, HDStaticDrawnClassic, StandHolderRoom.
	n = hd_switch_count(level,
		"if (!HDLevelActive() || roomIndex < 0 || roomIndex >= g_room_count)");
	ok(n == 7, "hd switch wiring: seven room gates ask HDLevelActive", n, 7);

	n = hd_switch_count(level, "if (!HDLevelActive() || group < 0 ||") +
		hd_switch_count(level, "if (!viewWorld || !HDLevelActive())");
	ok(n == 2, "hd switch wiring: HDCarriedRoom and HDFrameSky ask it", n,
		2);

	// The build, the F4 changer's rebuild (three), HDRoomWaterFaceCount
	// (read in the HD-room branch only) and HDFlipRoom.
	n = hd_switch_count(level, "!g_level_active");
	ok(n == 6, "hd switch wiring: six readers keep g_level_active", n, 6);

	// begin_decision, the items, and HDInvBegin, the inventory ring and
	// the pickup display: both ask the draw.
	n = hd_switch_count(object,
		"if (!g_active || !tomb5.hd_objects || !HDDrawn())");
	ok(n == 2, "hd switch wiring: the items' and the ring's gates ask "
		"HDDrawn", n, 2);

	n = hd_switch_count(object, "if (!g_active || !tomb5.hd_objects)\r") +
		hd_switch_count(object, "if (!g_active || !tomb5.hd_objects)\n");
	ok(n == 0, "hd switch wiring: no object gate without it", n, 0);

	// The models' release at the load asks what was built, not the draw.
	n = hd_switch_count(object,
		"if (!g_active && !(tomb5.hd_objects && HDLaraActive()))");
	ok(n == 1, "hd switch wiring: the release asks the build", n, 1);

	n = hd_switch_count(lara,
		"tomb5.hd_objects && HDLaraActive() && HDDrawn();");
	ok(n == 1, "hd switch wiring: Lara's HD draw asks HDDrawn", n, 1);

	n = hd_switch_count(life, "if (!life_on() || !HDDrawn())");
	ok(n == 1, "hd switch wiring: the life's frame asks HDDrawn", n, 1);

	n = hd_switch_count(life,
		"return tomb5.hd_water && HDRenderWaterSheetsBuilt();");
	ok(n == 1, "hd switch wiring: the life built at the load does not", n,
		1);

	free(level);
	free(object);
	free(lara);
	free(life);
}

/* A number #defined in an engine text, or -1: the token after the name. */
static long glass_define(const char* text, const char* name)
{
	const char* p = text ? strstr(text, name) : 0;

	return p ? strtol(p + strlen(name), 0, 10) : -1;
}

/* The body of an engine function, from the brace after `head` to its
 * closing brace, with every blank taken out - or an empty string. Lets a
 * test hold the engine's own expression and not only a copy of it. */
static void glass_body(const char* text, const char* head, char* out,
	size_t size)
{
	const char* p = text ? strstr(text, head) : 0;
	size_t n = 0;
	int depth = 0;

	out[0] = 0;
	p = p ? strchr(p, '{') : 0;

	for (; p && *p && n + 1 < size; p++)
	{
		if (*p == '{')
			depth++;
		else if (*p == '}' && --depth == 0)
			break;

		// A blank is anything at or under the space.
		if (depth > 0 && *p != '{' && (unsigned char)*p > ' ')
			out[n++] = *p;
	}

	out[n] = 0;
}

/* The split of a see-through draw call (hd_render.cpp, GlassSplits) and the
 * additive run's glass (AddGlass): their numbers and their bodies read out
 * of the engine's text, and their rules run over the census of the fifteen
 * levels. The split: every material 14 part of an alpha pass with param[1]
 * under 1, page by page (the page's smallest alpha and its share below the
 * alpha test's reference in per mille, measured as hd_pagecache.cpp
 * measures them), with what the classic draws at the parts' place (solid
 * faces, additive ones) and whether their triangles cover a clear texel;
 * only ANDREA2's window grid and its flip twin split, the one family where
 * the classic draws a solid frame with additive glass AND the texels paint
 * a frame. The additive glass: the 107 material 14 parts with an additive
 * run, page by page; a page without a clear texel is added whole. */
static void glass_split()
{
	struct Row
	{
		const char* what;
		int parts;
		float blend;
		int clear;
		int alphaMin;
		int permille;
		int classicSolid;
		int classicGlass;
		int clearCovered;
		int splits;
	};

	static const Row rows[] =
	{
		{ "ANDREA2 65/132 page 3007", 2, 0.1f, 0, 0, 856, 1, 1, 1, 1 },
		{ "JOBY2 52/78 page 16073", 2, 0.1f, 0, 77, 12, 0, 0, 0, 0 },
		{ "JOBY2 102 page 513", 1, 0.2333f, 0, 254, 0, 0, 1, 0, 0 },
		{ "JOBY3 page 10072", 27, 0.1f, 0, 120, 46, 1, 0, 0, 0 },
		{ "JOBY5 page 10072", 31, 0.1f, 0, 120, 46, 1, 1, 0, 0 },
		{ "RICH2 page 3309", 6, 0.8417f, 0, 165, 0, 1, 0, 0, 0 },
		{ "RICH3 46 page 10084", 5, 0.1f, 0, 0, 116, 1, 0, 0, 0 },
		{ "TITLE page 2150 at weight 1", 3, 1.0f, 0, 0, 186, 1, 0, 0, 0 },
		{ "ANDREA1 page 15074, additive run", 29, 0.0f, 0, 0, 328, 1, 1, 0,
			0 },
		{ "RICH2 125 page 16103, additive run", 1, 0.0f, 0, 100, 62, 0, 1, 0,
			0 },
		{ "a clear floor", 1, 1.0f, 1, 231, 0, 1, 0, 0, 0 }
	};

	/* The additive run's glass by page: its parts, the page's smallest
	 * alpha, and whether it is added whole. */
	struct Add
	{
		const char* what;
		int parts;
		int alphaMin;
		int whole;
	};

	static const Add adds[] =
	{
		{ "RICH2 6 page 1038", 1, 254, 1 },
		{ "RICH1/RICH3 page 3068", 26, 165, 1 },
		{ "RICH3 page 3309", 19, 165, 1 },
		{ "ANDREA1/2 page 11047", 19, 115, 1 },
		{ "ANDREA1/2 page 13066", 6, 81, 1 },
		{ "ANDREA1 157 page 16063", 1, 83, 1 },
		{ "ANDREA1 93 page 16070", 1, 75, 1 },
		{ "JOBY3/JOBY5/RICH2 bridge page 16103", 13, 100, 1 },
		{ "RICH2 page 15026", 9, 0, 0 },
		{ "ANDREA1 arched panes page 15074", 7, 0, 0 },
		{ "RICH2/RICH3 lamp glass page 15101", 5, 12, 0 }
	};

	char* render = hd_switch_text("hd_render.cpp");
	char* shader = hd_switch_text("hd_shader/hd_room.hlsl");

	ok(render && shader, "glass split: the two texts are read",
		!!render + !!shader, 2);

	const long below = glass_define(render, "#define HD_GLASS_CLEAR_BELOW");
	const long frame = glass_define(render, "#define HD_GLASS_FRAME_PERMILLE");

	ok(below == 16, "glass split: a clear texel is under 16 of 255", below,
		16);
	ok(frame >= 116 && frame < 856, "glass split: the share sits in the "
		"census's gap, 116 to 856 per mille", frame, 500);

	/* The rules below are the engine's own: its two bodies are held here
	 * blank for blank, so a change to either fails until this test follows
	 * it. */
	char body[512];

	glass_body(render, "inline bool GlassSplits(", body, sizeof(body));
	ok(!strcmp(body, "returnblend>0.0f&&blend<1.0f&&!clear&&"
		"HDPageCacheSlotAlphaMin(slot)<HD_GLASS_CLEAR_BELOW&&"
		"HDPageCacheSlotCutoutPermille(slot)>HD_GLASS_FRAME_PERMILLE;"),
		"glass split: GlassSplits is the rule tested here", 0, 0);

	glass_body(render, "inline bool AddGlass(", body, sizeof(body));
	ok(!strcmp(body, "returnmaterial==HD_TRG_MATERIAL_DECAL&&"
		"HDPageCacheSlotAlphaMin(slot)>=HD_GLASS_CLEAR_BELOW;"),
		"glass split: AddGlass is the rule tested here", 0, 0);

	int split = 0, parts = 0;

	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
	{
		const Row& r = rows[i];
		const int s = r.blend > 0.0f && r.blend < 1.0f && !r.clear &&
			r.alphaMin < below && r.permille > frame ? 1 : 0;
		char what[160];

		snprintf(what, sizeof(what), "glass split: %s", r.what);
		ok(s == r.splits, what, s, r.splits);

		/* And the texels' decision is the classic's where both speak: a
		 * see-through part splits exactly where the classic draws a solid
		 * frame with additive glass at its place and its triangles cover a
		 * clear texel. */
		if (r.blend > 0.0f && r.blend < 1.0f && !r.clear)
		{
			const int both = r.classicSolid && r.classicGlass &&
				r.clearCovered ? 1 : 0;

			snprintf(what, sizeof(what), "glass split: %s, the classic "
				"agrees", r.what);
			ok(s == both, what, s, both);
		}

		split += s * r.parts;
		parts += r.blend > 0.0f && r.blend < 1.0f && !r.clear ? r.parts : 0;
	}

	ok(parts == 74, "glass split: the census's see-through parts", parts, 74);
	ok(split == 2, "glass split: the parts that split", split, 2);

	int whole = 0, added = 0;

	for (size_t i = 0; i < sizeof(adds) / sizeof(adds[0]); i++)
	{
		const Add& a = adds[i];
		const int w = a.alphaMin >= below ? 1 : 0;
		char what[160];

		snprintf(what, sizeof(what), "glass added whole: %s", a.what);
		ok(w == a.whole, what, w, a.whole);
		whole += w * a.parts;
		added += a.parts;
	}

	ok(added == 107, "glass added whole: the census's additive glass",
		added, 107);
	ok(whole == 86, "glass added whole: the parts added whole", whole, 86);

	int n = hd_switch_count(render,
		"GlassSplits(range.blend, range.clear, range.pageSlot)");

	ok(n == 1, "glass split: draw_pass asks the rule", n, 1);

	n = hd_switch_count(render, "additive_pass && AddGlass(range.material,");
	ok(n == 1, "glass added whole: draw_pass asks the rule", n, 1);

	n = hd_switch_count(render, "additive_pass && !add_glass ?");
	ok(n == 1, "glass added whole: its weight is 1", n, 1);

	n = hd_switch_count(render, "pane_on ? D3DBLEND_ONE : D3DBLEND_SRCALPHA");
	ok(n == 1, "glass split: the glass half adds ONE / ONE", n, 1);

	n = hd_switch_count(shader,
		"clip(gPass.z * (texel.a < 16.0f / 255.0f ? 1.0f : -1.0f));");
	ok(n == 1, "glass split: the room shader keeps one half's texels", n, 1);

	n = hd_switch_count(shader,
		"lit *= lerp(1.0f, texel.a, gPass.y * step(0.0f, gPass.z));");
	ok(n == 1, "glass added whole: the alpha weighs no glass", n, 1);

	free(render);
	free(shader);
}

/* The build gate and the draw, in the engine's own text. HDEnabled() is
 * the build gate - hd_build and the installation - and every reader that
 * builds or loads at the load asks it; hd_enable, the HD textures row, is
 * the draw, and the interface asks it as the world does: the ring and the
 * pickup display (counted above), the HD font per character, the loading
 * screens and the F4 changer. No load-time reader asks the draw. */
static void hd_build_gate()
{
	char* level = hd_switch_text("hd_level.cpp");
	char* font = hd_switch_text("hd_font.cpp");
	char* feet = hd_switch_text("hd_feet.cpp");
	char* outfit = hd_switch_text("hd_outfitmenu.cpp");
	char* file = hd_switch_text("../file.cpp");
	char* opts = hd_switch_text("../../tomb5/tomb5.cpp");
	int n;

	ok(level && font && feet && outfit && file && opts, "hd build gate: "
		"the six files are read", !!level + !!font + !!feet + !!outfit +
		!!file + !!opts, 6);

	// HDEnabled asks hd_build and never the row.
	n = hd_switch_count(level, "if (!tomb5.hd_build)");
	ok(n == 1, "hd build gate: HDEnabled asks hd_build", n, 1);
	n = hd_switch_count(level, "if (!tomb5.hd_enable)") +
		hd_switch_count(level, "if (tomb5.hd_enable") +
		hd_switch_count(level, "&& tomb5.hd_enable") +
		hd_switch_count(level, "|| tomb5.hd_enable");
	ok(n == 0, "hd build gate: hd_level.cpp reads no row", n, 0);
	n = hd_switch_count(level, "if (tomb5.hd_build && !g_stem[0])");
	ok(n == 1, "hd build gate: the read-ahead's drop asks hd_build", n, 1);
	n = hd_switch_count(level, "return g_level_active;");
	ok(n == 1, "hd build gate: HDLevelBuilt is the rooms' build", n, 1);

	// The #203 key: valid only past the build gate.
	n = hd_switch_count(level,
		"if (!wadName || !*wadName || !HDEnabled())");
	ok(n == 1, "hd build gate: the level's key asks the build gate", n, 1);

	// The font: built behind the gate, drawn per character with the draw.
	n = hd_switch_count(font, "if (!page || !HDEnabled())") +
		hd_switch_count(font, "if (!HDEnabled() || !g_matched || g_atlas < 0)");
	ok(n == 2, "hd build gate: the font is built behind it", n, 2);
	n = hd_switch_count(font,
		"if (!g_tex || !g_matched || !HDDrawn() || index < 0");
	ok(n == 1, "hd build gate: a glyph asks the draw", n, 1);

	// The sounds stay as built: the feet and the spare sample infos.
	n = hd_switch_count(feet, "\tif (!HDEnabled())\r") +
		hd_switch_count(feet, "\tif (!HDEnabled())\n");
	ok(n == 1, "hd build gate: the barefoot set is built behind it", n, 1);
	n = hd_switch_count(file,
		"const long spare = HDEnabled() ? HD_FEET_SPARE_INFOS : 0;") +
		hd_switch_count(file,
		"if (!HDEnabled() || !sample_lut || !sample_infos)");
	ok(n == 2, "hd build gate: the samples ask the build gate", n, 2);

	// The loading screens and the F4 changer ask the draw.
	n = hd_switch_count(file, "if (!tomb5.hd_enable ||");
	ok(n == 1, "hd build gate: a loading screen asks the draw", n, 1);
	n = hd_switch_count(outfit, "if (!HDEnabled() || !HDDrawn())");
	ok(n == 1, "hd build gate: F4 asks the build and the draw", n, 1);

	// The setting: written first, read with 1 as its default, saved.
	n = hd_switch_count(opts, "sprintf(buf, \"hd_build\");");
	ok(n == 3, "hd build gate: hd_build written, read and saved", n, 3);
	n = hd_switch_count(opts, "REG_ReadLong(buf, tomb5.hd_build, 1);");
	ok(n == 1, "hd build gate: absent reads as 1", n, 1);

	// The switch's state is the setting: read by HDDrawn, written by the
	// one setter, saved by a flip; the data test is the one for all three.
	{
		char* sw = hd_switch_text("hd_switch.cpp");

		ok(sw != 0, "hd build gate: hd_switch.cpp is read", !!sw, 1);
		n = hd_switch_count(sw, "\treturn tomb5.hd_enable;");
		ok(n == 1, "hd build gate: HDDrawn is hd_enable", n, 1);
		n = hd_switch_count(sw, "tomb5.hd_enable =");
		ok(n == 1, "hd build gate: one writer of the draw", n, 1);
		n = hd_switch_count(sw, "tomb5.hd_enable = s.drawn != 0;");
		ok(n == 1, "hd build gate: the writer is the setter", n, 1);
		n = hd_switch_count(sw,
			"return HDLevelBuilt() || HDObjectActive() || HDLaraActive();");
		ok(n == 1, "hd build gate: one data test", n, 1);
		n = hd_switch_count(sw, "HDSwitchHasData() ? 1 : 0");
		ok(n == 2, "hd build gate: the key and the row ask it", n, 2);
		n = hd_switch_count(sw, "\t\tSaveDrawn();");
		ok(n == 1, "hd build gate: a flip of the key is saved", n, 1);
		free(sw);
	}

	free(level);
	free(font);
	free(feet);
	free(outfit);
	free(file);
	free(opts);
}

// Where `what` first stands in `text` from `from` on, or -1.
static long load_keep_at(const char* text, const char* from, const char* what)
{
	const char* start = text ? strstr(text, from) : 0;
	const char* p = start ? strstr(start, what) : 0;

	return p ? (long)(p - text) : -1;
}

/* A save load inside the level, in the engine's own text: the classic texture
 * pages kept with the HD data on round 2's key, the decoded loading picture
 * kept on its file, and the loading picture drawn under every bar frame.
 * LoadLevel, FreeLevel, LoadTextures and the screen code need the game and
 * a device, so their lines are counted where they stand; the bookkeeping of
 * a kept load against a read one is run outside the tree on the engine's own
 * functions (the record names it). */
static void load_keep()
{
	char* file = hd_switch_text("../file.cpp");
	char* tex = hd_switch_text("../texture.cpp");
	char* save = hd_switch_text("../LoadSave.cpp");
	char* screen = hd_switch_text("../hdscreen.cpp");
	char* bars = hd_switch_text("../drawbars.cpp");
	char* level = hd_switch_text("hd_level.cpp");
	long n, a, b;

	ok(file && tex && save && screen && bars && level, "load keep: the six "
		"files are read", !!file + !!tex + !!save + !!screen + !!bars +
		!!level, 6);

	// The decision: once, in LoadLevel, after the key and before FreeLevel,
	// by the key's own comparison.
	n = hd_switch_count(file, "g_pages_kept = TexturePagesKeep();");
	ok(n == 1, "load keep: the pages' decision is taken once", n, 1);
	a = load_keep_at(file, "HDLevelKeyNext((const char*)name);",
		"g_pages_kept = TexturePagesKeep();");
	b = load_keep_at(file, "HDLevelKeyNext((const char*)name);",
		"\tFreeLevel();");
	ok(a >= 0 && a < b, "load keep: after the key, before FreeLevel", a < b, 1);
	n = hd_switch_count(file, "!HDLevelKeepsNext()");
	ok(n == 1, "load keep: the decision asks the HD key", n, 1);
	n = hd_switch_count(level,
		"return !HDLevelKeepRefused(&g_key_built, &g_key_next, "
		"g_redress_count);");
	ok(n == 1, "load keep: HDLevelKeepsNext is HDLevelUnload's test", n, 1);

	// FreeLevel spares the kept pages only; the misc pages go.
	n = hd_switch_count(file, "FreeTexturesFrom(g_pages_kept);");
	ok(n == 1, "load keep: FreeLevel frees above the kept pages", n, 1);
	n = hd_switch_count(tex, "for (long i = first; i < nTextures; i++)");
	ok(n == 1, "load keep: FreeTexturesFrom starts at the first", n, 1);

	// LoadTextures: the kept walk returns at once without a decision, the
	// bar gets pages + 3 calls, the record is behind the build gate.
	n = hd_switch_count(file, "\tif (!g_pages_kept)\r\n\t\treturn 0;") +
		hd_switch_count(file, "\tif (!g_pages_kept)\n\t\treturn 0;");
	ok(n == 1, "load keep: no decision, no kept walk", n, 1);
	n = hd_switch_count(file, "for (long i = 0; i < g_pages_kept + 2; i++)");
	ok(n == 1, "load keep: the bar's calls of a read", n, 1);
	a = load_keep_at(file, "LoadTexturePages(RTPages, OTPages, BTPages, "
		"format,", "if (HDEnabled())");
	b = load_keep_at(file, "LoadTexturePages(RTPages, OTPages, BTPages, "
		"format,", "g_pages.end = nTextures;");
	ok(a >= 0 && a < b, "load keep: the record is behind the build gate",
		a < b, 1);
	n = hd_switch_count(file, "\"textures\", g_pages_kept);");
	ok(n == 1, "load keep: the textures step says kept", n, 1);

	// The picture: kept behind the build gate, keyed on size and time,
	// dropped before another read; the title's logo reads as before.
	n = hd_switch_count(save, "px = HDScreenReadKept(name, 2048, &w, &h);");
	ok(n == 1, "load keep: LoadScreenHD asks the picture's keep", n, 1);
	a = load_keep_at(save, "long LoadScreenHD(const char* name)",
		"if (HDEnabled())");
	b = load_keep_at(save, "long LoadScreenHD(const char* name)",
		"HDScreenReadKept(");
	ok(a >= 0 && a < b, "load keep: the picture's keep is behind the gate",
		a < b, 1);
	n = hd_switch_count(save,
		"title = HDScreenRead(\"TITLE\", 2048, &tw, &th);");
	ok(n == 1, "load keep: the title's logo reads as before", n, 1);
	n = hd_switch_count(screen, "g_screen_keep.size == size &&");
	ok(n == 1, "load keep: the picture's key holds the file's size", n, 1);
	a = load_keep_at(screen, "ulong* HDScreenReadKept(", "HDScreenKeepDrop();");
	b = load_keep_at(screen, "ulong* HDScreenReadKept(",
		"px = HDScreenRead(name, maxSide, width, height);");
	ok(a >= 0 && a < b, "load keep: another picture drops the kept one first",
		a < b, 1);

	// The frames: the picture once, then under every bar frame, released
	// after the bar's last frame.
	n = hd_switch_count(file, "for (int i = 0; i < 4; i++)");
	ok(n == 0, "load keep: no four loading frames", n, 0);
	n = hd_switch_count(file, "DrawLoadingScreen();");
	ok(n == 1, "load keep: the picture's one frame", n, 1);
	a = load_keep_at(bars, "long S_DrawLoadBar()", "DrawLoadingScreen();");
	b = load_keep_at(bars, "long S_DrawLoadBar()", "if (tomb5.tr4_loadbar)");
	ok(a >= 0 && a < b, "load keep: every bar frame draws the picture first",
		a < b, 1);
	a = load_keep_at(file, "S_LoadScreenEnd();", "S_LoadScreenEnd();");
	b = load_keep_at(file, "S_LoadScreenEnd();", "ReleaseScreen();");
	ok(a >= 0 && a < b, "load keep: the picture goes after the last frame",
		a < b, 1);

	// Stage 0 between the two threads: each loading-thread bind and each
	// bar frame's binds and draws under the one lock (texture.cpp).
	n = hd_switch_count(tex, "TextureStageLock();") +
		hd_switch_count(tex, "TextureStageUnlock();");
	ok(n == 4, "load keep: FreeTextures' two binds are locked", n, 4);
	n = hd_switch_count(file, "TextureStageLock();") +
		hd_switch_count(file, "TextureStageUnlock();");
	ok(n == 4, "load keep: LoadTextures' two binds are locked", n, 4);
	{
		const char* ot = "Textures[nTex].bump = 0;\r\n\t\tTextureStageLock";
		const char* fr = "long S_DrawLoadBar()";
		long s[5];

		if (load_keep_at(file, ot, "TextureStageLock();") < 0)
			ot = "Textures[nTex].bump = 0;\n\t\tTextureStageLock";

		s[0] = load_keep_at(file, ot, "TextureStageLock();");
		s[1] = load_keep_at(file, ot, "SetTexture(0, tSurf);");
		s[2] = load_keep_at(file, ot, "TextureStageUnlock();");
		n = s[0] >= 0 && s[0] < s[1] && s[1] < s[2];
		ok(n == 1, "load keep: the page loop binds under the lock", n, 1);

		s[0] = load_keep_at(bars, fr, "TextureStageLock();");
		s[1] = load_keep_at(bars, fr, "DrawLoadingScreen();");
		s[2] = load_keep_at(bars, fr, "DrawSortList();");
		s[3] = load_keep_at(bars, fr, "TextureStageUnlock();");
		s[4] = load_keep_at(bars, fr, "S_DumpScreenFrame();");
		n = s[0] >= 0 && s[0] < s[1] && s[1] < s[2] && s[2] < s[3] &&
			s[3] < s[4];
		ok(n == 1, "load keep: the bar frame draws under the lock, presents "
			"outside it", n, 1);
	}
	n = hd_switch_count(file, "if (!level_fp && g_pages_kept)");
	ok(n == 1, "load keep: a failed open frees the kept pages", n, 1);

	free(file);
	free(tex);
	free(save);
	free(screen);
	free(bars);
	free(level);
}

/* The font's glyph rows and accent rows as the engine has them (game\
 * chardef.inc, game\accent.inc, unscaled), for the place of the level's
 * name on the loading screen. */
struct LoadNameDef
{
	float u, v;
	short w, h, YOffset;
	char TopShade, BottomShade;
};

static const LoadNameDef load_name_rows[106] =
{
#include "../../../TOMB5 Code/TOMB5/game/chardef.inc"
};

static const char load_name_accent[46][2] =
{
#include "../../../TOMB5 Code/TOMB5/game/accent.inc"
};

/* The glyph rows at a screen `rows` lines high, as InitFont scales them. */
static void load_name_scaled(long rows, LoadNameDef* out)
{
	const short winymax = (short)(rows - 1);

	for (int i = 0; i < 106; i++)
	{
		out[i] = load_name_rows[i];
		out[i].h = short((float)load_name_rows[i].h *
			float(winymax / 240.0F));
		out[i].YOffset = short((float)load_name_rows[i].YOffset *
			float(winymax / 240.0F));
	}
}

// GetFixedScale (specific\output.cpp) at a screen size; MulDiv rounds.
static long load_name_fixed(long w, long h, long unit)
{
	const long x = w > 640 ? (w * unit + 320) / 640 : unit;
	const long y = h > 480 ? (h * unit + 240) / 480 : unit;

	return x < y ? x : y;
}

// How far the font reaches below a baseline, in pixels.
static long load_name_depth(const LoadNameDef* def)
{
	long depth = 0;

	for (int i = 0; i < 106; i++)
		if (def[i].YOffset + def[i].h > depth)
			depth = def[i].YOffset + def[i].h;

	return depth;
}

/* The level's name on the loading screen (loadbar_math.h and the engine's
 * text): when it is drawn - the row, the screen shown, a level and not the
 * title -, its place inside the screen at 1080, 1440 and 2160 rows, and the
 * frame that draws it: the load's first, read back, and no text in the
 * bar's frames. */
static void load_mode_name(const char* file, const char* bars,
	const char* flow)
{
	char* out = hd_switch_text("../output.cpp");
	char* text = hd_switch_text("../../game/text.cpp");
	long n, a, b, c, d;

	ok(out && text, "load name: output.cpp and text.cpp are read",
		!!out + !!text, 2);

	// The rule: the row, a screen shown, a level.
	{
		long bad = 0, drawn = 0;

		for (long row = 0; row <= 1; row++)
			for (long shown = 0; shown <= 1; shown++)
				for (long slot = -1; slot <= 1; slot++)	// none, title, level
				{
					const long want = row && shown && slot > 0;
					const long got = LoadNameDrawn(row, shown, slot);

					bad += got != want;
					drawn += got;
				}

		ok(bad == 0, "load name: the row, shown, a level", bad, 0);
		ok(drawn == 1, "load name: one case of twelve draws", drawn, 1);
	}
	ok(!LoadNameDrawn(1, 1, 0), "load name: the title shows none",
		LoadNameDrawn(1, 1, 0), 0);
	ok(!LoadNameDrawn(0, 1, 5), "load name: the row off shows none",
		LoadNameDrawn(0, 1, 5), 0);

	// The screen's states: TR5 and TR4 show it, off only for a build.
	n = LoadNameDrawn(1, LoadScreenModeOf(LOADBAR_TR5, 0) != LOADBAR_OFF,
		5) + LoadNameDrawn(1, LoadScreenModeOf(LOADBAR_TR4, 0) !=
		LOADBAR_OFF, 5);
	ok(n == 2, "load name: TR5 and TR4 carry the name", n, 2);
	n = LoadNameDrawn(1, LoadScreenModeOf(LOADBAR_OFF, 1) != LOADBAR_OFF,
		5);
	ok(n == 1, "load name: off, a load that builds carries it", n, 1);
	n = LoadNameDrawn(1, LoadScreenModeOf(LOADBAR_OFF, 0) != LOADBAR_OFF,
		5);
	ok(n == 0, "load name: off, a short load shows no text", n, 0);

	/* The slot: S_LoadLevelFile names gfCurrentLevel, which DoGameflow has
	 * set for this load, since only DoTitle and DoLevel call it. */
	n = hd_switch_count(flow, "S_LoadLevelFile(");
	ok(n == 2, "load name: only DoTitle and DoLevel load", n, 2);
	n = hd_switch_count(flow, "available_levels[lp - 1] ? 2 : 3, "
		"SCRIPT_TEXT(gfLevelNames[lp])");
	ok(n == 1 && kLoadNameStyle == 2, "load name: the level list's style",
		kLoadNameStyle, 2);
	n = hd_switch_count(bars, "y = phd_winheight - h - GetFixedScale(20);");
	ok(n == 2 && kLoadNameMargin == 20, "load name: the bar's own margin",
		kLoadNameMargin, 20);

	// The arithmetic it follows: InitFont's scale, DrawChar's accent.
	n = hd_switch_count(text, "yoff = short((float)copy[i].YOffset * "
		"float(phd_winymax / 240.0F));");
	ok(n == 1, "load name: InitFont scales the y offsets so", n, 1);
	n = hd_switch_count(text, "DrawChar(def->w / 2 + x2 - 3, y + "
		"def->YOffset, col, accent);");
	ok(n == 1, "load name: an accent sits on its letter's offset", n, 1);

	// The place at three sizes: the ink inside, the margin kept.
	{
		static const long size[3][5] =
		{
			// width, rows, margin, reach, baseline (m_place.py)
			{ 1920, 1080, 45, 66, 111 },
			{ 2560, 1440, 60, 88, 148 },
			{ 3840, 2160, 90, 133, 223 },
		};
		LoadNameDef def[106];

		for (int s = 0; s < 3; s++)
		{
			load_name_scaled(size[s][1], def);

			const long m = load_name_fixed(size[s][0], size[s][1],
				kLoadNameMargin);
			const long reach = LoadNameReach(def, 106, load_name_accent,
				46);
			const long y = LoadNameY(m, reach);
			const long depth = load_name_depth(def);

			ok(m == size[s][2], "load name: the margin at this size", m,
				size[s][2]);
			ok(reach == size[s][3], "load name: the font's reach up", reach,
				size[s][3]);
			ok(y == size[s][4], "load name: the baseline", y, size[s][4]);
			ok(y - reach >= 0 && y - reach == m, "load name: the top ink "
				"inside, at the margin", y - reach, m);
			ok(y + depth < size[s][1] / 4, "load name: the line in the "
				"top quarter", y + depth, size[s][1] / 4);

			/* The lettering's place: its baseline GetFixedScale(37) over
			 * the bottom edge. One line stays inside; PrintString steps a
			 * further line down by its ink's height and 2, a line of
			 * capitals ('A') by the least, and a third line's baseline is
			 * under the edge already. */
			const long old = size[s][1] - load_name_fixed(size[s][0],
				size[s][1], 37);
			const long step = def['A' - '!'].h + 2;

			ok(old + depth <= size[s][1], "load name: one line at the old "
				"place inside", old + depth, size[s][1]);
			ok(old + 2 * step > size[s][1], "load name: a third line at the "
				"old place below the edge", old + 2 * step, size[s][1]);
		}
	}

	// The lettering is gone.
	n = hd_switch_count(out, "TXT_LOADING2") + hd_switch_count(file,
		"TXT_LOADING2") + hd_switch_count(bars, "TXT_LOADING2");
	ok(n == 0, "load name: the script's lettering is drawn nowhere", n, 0);

	// The load's first frame: the name, the read back, then the present.
	a = load_keep_at(file, "if (shown)\r\n\t{", "DrawLoadingScreen();");
	if (a < 0)
		a = load_keep_at(file, "if (shown)\n\t{", "DrawLoadingScreen();");
	b = load_keep_at(file, "DrawLoadingScreen();\r\n\r\n\t\tif (named)",
		"S_LoadScreenName(gfCurrentLevel);");
	if (b < 0)
		b = load_keep_at(file, "DrawLoadingScreen();\n\n\t\tif (named)",
			"S_LoadScreenName(gfCurrentLevel);");
	c = load_keep_at(file, "S_LoadScreenName(gfCurrentLevel);",
		"S_LoadScreenKeepName()");
	d = load_keep_at(file, "S_LoadScreenName(gfCurrentLevel);",
		"S_DumpScreenFrame();");
	ok(a >= 0 && a < b && b < c && c < d, "load name: the first frame draws"
		" it and is read back before its present", a < b && b < c && c < d,
		1);
	n = hd_switch_count(file, "S_LoadScreenName(") + hd_switch_count(out,
		"S_LoadScreenName(");
	ok(n == 1, "load name: drawn in the first frame, nowhere else", n, 1);
	n = hd_switch_count(file, "if (named)\r\n\t\t\tS_LoadScreenKeepName();") +
		hd_switch_count(file, "if (named)\n\t\t\tS_LoadScreenKeepName();");
	ok(n == 1, "load name: the frame with it read back", n, 1);

	// The bar's frames draw the picture and no text of their own.
	n = hd_switch_count(bars, "PrintString(");
	ok(n == 1, "load name: one PrintString in drawbars.cpp", n, 1);
	a = load_keep_at(bars, "long S_DrawLoadBar()", "PrintString(");
	ok(a < 0, "load name: none in the bar's frames", a, -1);
	a = load_keep_at(bars, "long S_LoadScreenKeepName()", "_EndScene();");
	b = load_keep_at(bars, "long S_LoadScreenKeepName()",
		"made = CaptureLoadingScreen();");
	c = load_keep_at(bars, "long S_LoadScreenKeepName()", "_BeginScene();");
	ok(a >= 0 && a < b && b < c, "load name: read back between two scenes",
		a < b && b < c, 1);
	n = hd_switch_count(bars, "MonoScreenOn = was;");
	ok(n == 1, "load name: a failed read keeps the picture", n, 1);

	free(out);
	free(text);
}

/* The Loadbar style row's third state (specific\loadbar_math.h and the
 * engine's own text): the row's state out of the registry, the fade of the
 * load without a loading screen, and the paths that state takes - no
 * picture read, no level view rendered, no frame of the load's own but the
 * fade's, and nothing at all over a black screen - for a short load only:
 * one that builds the HD data shows the screen as TR5, on the keep's own
 * key, stamped once before the loading thread. */
static void load_mode()
{
	char* file = hd_switch_text("../file.cpp");
	char* bars = hd_switch_text("../drawbars.cpp");
	char* save = hd_switch_text("../LoadSave.cpp");
	char* fx = hd_switch_text("../specificfx.cpp");
	char* flow = hd_switch_text("../../game/gameflow.cpp");
	char* menu = hd_switch_text("../../game/menu_frame.cpp");
	char* opts = hd_switch_text("../../tomb5/tomb5.cpp");
	char* level = hd_switch_text("hd_level.cpp");
	long n, a, b;

	ok(file && bars && save && fx && flow && menu && opts && level,
		"load mode: the eight files are read", !!file + !!bars + !!save +
		!!fx + !!flow + !!menu + !!opts + !!level, 8);

	// The DWORD when it holds a state; the older bool otherwise.
	ok(LoadBarModeOf(true, 0, true) == LOADBAR_TR5,
		"load mode: a DWORD 0 is TR5", LoadBarModeOf(true, 0, true), 0);
	ok(LoadBarModeOf(true, 1, false) == LOADBAR_TR4,
		"load mode: a DWORD 1 is TR4", LoadBarModeOf(true, 1, false), 1);
	ok(LoadBarModeOf(true, 2, true) == LOADBAR_OFF,
		"load mode: a DWORD 2 is off", LoadBarModeOf(true, 2, true), 2);
	ok(LoadBarModeOf(true, 2, false) == LOADBAR_OFF,
		"load mode: off whatever the bool says", LoadBarModeOf(true, 2,
		false), 2);
	ok(LoadBarModeOf(false, 0, true) == LOADBAR_TR4,
		"load mode: no DWORD, tr4LB 1 sees TR4", LoadBarModeOf(false, 0,
		true), 1);
	ok(LoadBarModeOf(false, 2, false) == LOADBAR_TR5,
		"load mode: no DWORD, tr4LB 0 sees TR5", LoadBarModeOf(false, 2,
		false), 0);
	ok(LoadBarModeOf(true, 3, true) == LOADBAR_TR4,
		"load mode: a DWORD 3 falls back to tr4LB 1", LoadBarModeOf(true, 3,
		true), 1);
	ok(LoadBarModeOf(true, 0xFFFFFFFFul, false) == LOADBAR_TR5,
		"load mode: a DWORD -1 falls back to tr4LB 0", LoadBarModeOf(true,
		0xFFFFFFFFul, false), 0);

	// The fade: 0 at its start, 255 at its end and after, a cut at 0 ms.
	ok(LoadFadeAlpha(0.0, 150.0) == 0, "load mode: the fade starts clear",
		LoadFadeAlpha(0.0, 150.0), 0);
	ok(LoadFadeAlpha(75.0, 150.0) == 127, "load mode: half way is 127",
		LoadFadeAlpha(75.0, 150.0), 127);
	ok(LoadFadeAlpha(150.0, 150.0) == 255, "load mode: black at its end",
		LoadFadeAlpha(150.0, 150.0), 255);
	ok(LoadFadeAlpha(400.0, 150.0) == 255, "load mode: black after it",
		LoadFadeAlpha(400.0, 150.0), 255);
	ok(LoadFadeAlpha(0.0, 0.0) == 255, "load mode: 0 ms is a cut",
		LoadFadeAlpha(0.0, 0.0), 255);
	ok(LoadFadeAlpha(-3.0, 150.0) == 0, "load mode: before its start clear",
		LoadFadeAlpha(-3.0, 150.0), 0);
	{
		long last = -1, back = 0;

		for (int ms = 0; ms <= 200; ms++)
		{
			const long al = LoadFadeAlpha((double)ms, 150.0);

			back += al < last;
			last = al;
		}

		ok(back == 0, "load mode: the fade never lightens", back, 0);
	}

	// The registry: the DWORD read with the bool as its default, and the
	// bool following the state; the row's three states.
	n = hd_switch_count(opts, "LoadBarModeOf(false, 0, tomb5.tr4_loadbar));");
	ok(n == 1, "load mode: absent, the DWORD reads as tr4LB", n, 1);
	n = hd_switch_count(opts, "tomb5.tr4_loadbar = tomb5.loadbar == "
		"LOADBAR_TR4;");
	ok(n == 1, "load mode: tr4LB follows the state when read", n, 1);
	n = hd_switch_count(opts, "sprintf(buf, \"loadbar\");");
	ok(n == 3, "load mode: loadbar written, read and saved", n, 3);
	n = hd_switch_count(opts, "sprintf(buf, \"loadfade_ms\");");
	ok(n == 3, "load mode: loadfade_ms written, read and saved", n, 3);
	n = hd_switch_count(opts, "REG_ReadLong(buf, tomb5.loadfade_ms, 150);");
	ok(n == 1, "load mode: loadfade_ms absent reads as 150", n, 1);
	n = hd_switch_count(menu, "tomb5.tr4_loadbar = tomb5.loadbar == "
		"LOADBAR_TR4;");
	ok(n == 1, "load mode: the row keeps tr4LB with the state", n, 1);
	n = hd_switch_count(menu, "tomb5.loadbar == LOADBAR_OFF ? \"off\" :");
	ok(n == 1, "load mode: the row names off", n, 1);

	// Off: no picture read, no level view, no frame but the fade's.
	n = hd_switch_count(file, "if (shown)\r\n\t{\r\n\t\tif (!MonoScreenOn)") +
		hd_switch_count(file, "if (shown)\n\t{\n\t\tif (!MonoScreenOn)");
	ok(n == 1, "load mode: off reads no picture, a shown load its own", n,
		1);
	/* The dropped view's names, each spelt in two literals so that a search
	 * of the tree for them finds the record alone. */
	n = hd_switch_count(flow, "Render" "LoadPic") + hd_switch_count(file,
		"Render" "LoadPic") + hd_switch_count(bars, "Render" "LoadPic");
	ok(n == 0, "load mode: no load renders a level view", n, 0);
	n = hd_switch_count(opts, "tr4" "LS") + hd_switch_count(opts,
		"tr4_" "loadscreens") + hd_switch_count(menu, "tr4_" "loadscreens") +
		hd_switch_count(flow, "TR4 " "loadscreens") + hd_switch_count(file,
		"tr4_" "loadscreens");
	ok(n == 0, "load mode: the view's row and its key are gone", n, 0);
	a = load_keep_at(file, "LoadLogLevel(name);", "if (shown)");
	b = load_keep_at(file, "LoadLogLevel(name);", "DrawLoadingScreen();");
	ok(a >= 0 && a < b, "load mode: the load's first frame only when shown",
		a < b, 1);
	n = hd_switch_count(file, "loadbar_on && S_LoadScreenDraws())");
	ok(n == 1, "load mode: the thread's frames ask the state", n, 1);
	n = hd_switch_count(bars,
		"if (App.dx.Flags & DXF_HWR && loadbar_mode != LOADBAR_OFF)");
	ok(n == 1, "load mode: the last frames only when shown", n, 1);
	n = hd_switch_count(bars, "return loadbar_mode != LOADBAR_OFF || "
		"loadbar_fading;");
	ok(n == 1, "load mode: off draws only while the fade runs", n, 1);
	a = load_keep_at(file, "if (!shown && !black)", "fade = 1;");
	b = load_keep_at(file, "if (!shown && !black)", "CaptureLoadingScreen();");
	ok(a >= 0 && a < b, "load mode: the fade only over a frame not black",
		a < b, 1);
	a = load_keep_at(file, "load thread joined", "S_LoadScreenJoin();");
	b = load_keep_at(file, "load thread joined", "HDLevelBuild();");
	ok(a >= 0 && a < b, "load mode: a fade the thread outlasts ends at the "
		"join", a < b, 1);
	n = hd_switch_count(bars, "loadbar_fade_ms = 0;\r\n\tS_DrawLoadBar();") +
		hd_switch_count(bars, "loadbar_fade_ms = 0;\n\tS_DrawLoadBar();");
	ok(n == 1, "load mode: in one black frame", n, 1);
	n = hd_switch_count(bars,
		"if (alpha >= 255)\r\n\t\t\tloadbar_fading = 0;") +
		hd_switch_count(bars, "if (alpha >= 255)\n\t\t\tloadbar_fading = 0;");
	ok(n == 1, "load mode: the black frame ends the fade", n, 1);

	// Black: the fade ran to black, no movie after it, noted before the
	// level's fade-in resets the fade, at both of the load's entries.
	n = hd_switch_count(fx, "return DoFade == 2 && FadeEnd >= 255;");
	ok(n == 1, "load mode: black is a fade run to black", n, 1);
	n = hd_switch_count(flow, "LoadEntryNoteBlack();\r\n\tSetFade(255, 0);") +
		hd_switch_count(flow, "LoadEntryNoteBlack();\n\tSetFade(255, 0);");
	ok(n == 2, "load mode: noted before both fade-ins", n, 2);
	n = hd_switch_count(flow, "fmv_on_screen = 1;");
	ok(n == 3, "load mode: each movie before a load counts", n, 3);

	// The picture read back is released with the load's last frame.
	n = hd_switch_count(save, "if (MonoScreenHD || MonoScreenView)");
	ok(n == 1, "load mode: a screen read back is given back", n, 1);
	n = hd_switch_count(file, "S_LoadScreenEnd();\r\n\r\n\tif (MonoScreenOn "
		"== 2)\r\n\t\tReleaseScreen();") + hd_switch_count(file,
		"S_LoadScreenEnd();\n\n\tif (MonoScreenOn == 2)\n\t\tReleaseScreen();");
	ok(n == 1, "load mode: a picture read back goes after the load", n, 1);

	// A picture that could not be read: the frame on the screen read back
	// once, before the first frame, and drawn under every frame from then.
	a = load_keep_at(file, "if (shown && !MonoScreenOn)",
		"CaptureLoadingScreen();");
	b = load_keep_at(file, "if (shown && !MonoScreenOn)",
		"DrawLoadingScreen();");
	ok(a >= 0 && a < b, "load mode: no picture read, the screen read back "
		"before the first frame", a < b, 1);
	a = load_keep_at(file, "LoadScreen(chosen_screen + 2, 4);",
		"if (shown && !MonoScreenOn)");
	ok(a >= 0, "load mode: read back only after the picture's read", a >= 0,
		1);
	n = hd_switch_count(save, "d[x] = s[x] | 0xFF000000;");
	ok(n == 1, "load mode: the screen read back pixel for pixel", n, 1);

	// Off only for the short loads: a load builds the HD data with the
	// build gate open, the level's .TRG there and the data not kept.
	{
		long bad = 0;

		for (long g = 0; g <= 1; g++)
			for (long art = 0; art <= 1; art++)
				for (long kept = 0; kept <= 1; kept++)
					bad += LoadBuildsHD(g, art, kept) !=
						(g && art && !kept ? 1 : 0);

		ok(bad == 0, "load mode: a build needs the gate, the art and no "
			"keep", bad, 0);
	}
	n = LoadScreenModeOf(LOADBAR_OFF, LoadBuildsHD(1, 1, 1));
	ok(n == LOADBAR_OFF, "load mode: off and the data kept, no screen", n,
		LOADBAR_OFF);
	n = LoadScreenModeOf(LOADBAR_OFF, LoadBuildsHD(1, 1, 0));
	ok(n == LOADBAR_TR5, "load mode: off and a build, the screen as TR5", n,
		LOADBAR_TR5);
	n = LoadScreenModeOf(LOADBAR_OFF, LoadBuildsHD(0, 1, 0)) +
		LoadScreenModeOf(LOADBAR_OFF, LoadBuildsHD(0, 0, 0));
	ok(n == 2 * LOADBAR_OFF, "load mode: off with the build gate shut, no "
		"screen", n, 2 * LOADBAR_OFF);
	n = LoadScreenModeOf(LOADBAR_OFF, LoadBuildsHD(1, 0, 0));
	ok(n == LOADBAR_OFF, "load mode: off and no .TRG, no screen", n,
		LOADBAR_OFF);
	{
		long bad = 0;

		for (long builds = 0; builds <= 1; builds++)
		{
			bad += LoadScreenModeOf(LOADBAR_TR5, builds) != LOADBAR_TR5;
			bad += LoadScreenModeOf(LOADBAR_TR4, builds) != LOADBAR_TR4;
		}

		ok(bad == 0, "load mode: TR5 and TR4 as the row says", bad, 0);
	}

	// The decision in the engine: taken once, before the loading thread,
	// from the key HDLevelKeyEarly stamps; the screen's state is the one
	// the load draws in.
	a = load_keep_at(file, "long S_LoadLevelFile(long num)",
		"HDLevelKeyEarly(name, &next);");
	b = load_keep_at(file, "long S_LoadLevelFile(long num)",
		"LoadLevel, name, 0,");
	ok(a >= 0 && a < b, "load mode: the decision before the thread", a < b,
		1);
	n = hd_switch_count(file, "HDLevelKeyEarly(");
	ok(n == 1, "load mode: one early stamp", n, 1);
	n = hd_switch_count(file, "LoadScreenModeOf(row,\r\n\t\tLoadBuildsHD("
		"next.gate, next.art, next.kept));") + hd_switch_count(file,
		"LoadScreenModeOf(row,\n\t\tLoadBuildsHD(next.gate, next.art, "
		"next.kept));");
	ok(n == 1, "load mode: the state from the row and the keep", n, 1);
	n = hd_switch_count(file, "S_LoadScreenBegin(mode, fade,");
	ok(n == 1, "load mode: the screen begins in that state", n, 1);
	n = hd_switch_count(file, "LoadLogStepBar(&loadlog_at, shown);") +
		hd_switch_count(file, "wait=%.1f shown=%ld\",");
	ok(n == 2, "load mode: the bar line says shown", n, 2);

	// One key: LoadLevel's HDLevelKeyNext takes the early stamp for the
	// same file; both stamp with KeyStampOf, every comparison is
	// HDLevelKeepRefused on g_key_built.
	n = hd_switch_count(level, "KeyStampOf(wadName, ");
	ok(n == 2, "load mode: the early stamp and LoadLevel's, one stamp", n,
		2);
	n = hd_switch_count(level, "FileStampOf(");
	ok(n == 6, "load mode: the files stamped in one place", n, 6);
	n = hd_switch_count(level, "!strcmp(g_key_early_wad, wadName);");
	ok(n == 1, "load mode: the early key taken for the same file", n, 1);
	a = load_keep_at(level, "void HDLevelKeyNext(const char* wadName)",
		"k = g_key_early;");
	b = load_keep_at(level, "void HDLevelKeyNext(const char* wadName)",
		"memset(&g_key_early, 0, sizeof(g_key_early));");
	ok(a >= 0 && a < b, "load mode: taken once, then cleared", a < b, 1);
	n = hd_switch_count(level, "out->kept = !HDLevelKeepRefused("
		"&g_key_built, &g_key_early,");
	ok(n == 1, "load mode: the early answer is the keep's comparison", n, 1);
	n = hd_switch_count(level, "HDLevelKeepRefused(");
	ok(n == 4, "load mode: no other comparison", n, 4);
	n = hd_switch_count(level, "HDLevelKeyDiffers(");
	ok(n == 0, "load mode: no key compared outside the keep's rule", n, 0);

	// The loading text: the level's name.
	load_mode_name(file, bars, flow);

	free(level);
	free(file);
	free(bars);
	free(save);
	free(fx);
	free(flow);
	free(menu);
	free(opts);
}

/* The brake on a load's timeline (loadbar_math.h as drawbars.cpp runs
 * it), in ms from the loading screen's first frame, which sits on a tick.
 * While the loading thread runs, the loop draws a frame at every 1/60 s
 * tick and polls again after its Sleep(1); the join follows its last
 * turn. After the join the main thread's points - the join, aInit, the HD
 * build's hook calls, the build's end, the font, the cutscenes, the warmup
 * (specific\file.cpp, S_LoadScreenStep) - draw a frame only where a tick
 * has passed since the last one handed out. After the work, the tail
 * draws one frame a tick until the work is done and the screen has stood
 * minMs. A frame hands out the tick it is drawn in, and its time on the
 * bar is the last tick boundary it stands past. */
struct BrakeLoad
{
	const char* level;
	int resident;			// a save loaded inside its level
	double thread;			// the loading thread's end (logged: the join)
	double ainit;
	double build;			// the HD build's ms
	double font;
	double cutscenes;
	double warmup;
	double profile;
	int calls;			// the build's hook calls
	double at[16];			// each one's ms after the build's start
};

struct BrakeRun
{
	int frames;			// the bar's frames, the picture's first not counted
	int buildFrames;		// of them, those of the build's reports
	int back;			// steps backwards
	int ahead;			// frames ahead of the work or the clock
	int full;			// frames at 100
	int overTick;			// steps larger than one tick's share
	int overSeen;			// of them, those with no blind stretch before
	float maxJump;			// the largest step, percent
	double blind;			// the longest stretch without a frame point
	const char* blindAt;		// the point that ended it
	double join;
	double end;			// the work's end
	double last;			// the last frame
};

static BrakeRun brake_run(const BrakeLoad& L, double minMs, float share,
	bool logged)
{
	const double tick = kLoadBarTickMs;
	const float eps = 0.001f;
	BrakeRun r = {};
	double handed = 0.0;
	double point = 0.0;
	float prev = -1.0f;

	/* A frame at `t`, the point before it at `point`: a step over one
	 * tick's share is seen (overSeen) when no stretch of the work longer
	 * than a tick lies before it - a frame the rule should have kept to
	 * its tick. */
	const auto frame = [&](double t, float work)
	{
		const double on = LoadBarTicksMs(floor(t / tick + 1e-9) * tick);
		const float pos = LoadBarPosition(work, on, minMs);

		if (prev >= 0.0f)
		{
			const float jump = pos - prev;
			const bool over = minMs > 0.0 &&
				jump > (float)(100.0 * tick / minMs) + eps;

			r.back += jump < -eps;
			r.maxJump = std::max(r.maxJump, jump);
			r.overTick += over;
			r.overSeen += over && t - point <= tick;
		}

		r.ahead += pos > 100.0f * work + eps ||
			(minMs > 0.0 && pos > (float)(100.0 * t / minMs) + eps);
		r.full += pos >= 100.0f - eps;
		prev = pos;
		handed = floor(t / tick + 1e-9) * tick;
		r.frames++;
		r.last = t;
		point = t;
		return LoadBarDone(work, on, minMs);
	};

	/* A point of the main thread's work at `t`: a frame if a tick has
	 * passed since the last one handed out. */
	const auto reach = [&](double t, float work, const char* name, bool build)
	{
		if (t - point > r.blind)
		{
			r.blind = t - point;
			r.blindAt = name;
		}

		if (t >= handed + tick)
		{
			frame(t, work);
			r.buildFrames += build;
		}

		point = t;
	};

	// The loop: a frame at the next tick while the thread was running at
	// its poll; logged loads already hold the loop's last turn in the join.
	double poll = 1.0;

	while (logged ? handed + tick < L.thread : poll < L.thread)
	{
		const double t = std::max(handed + tick, poll);

		frame(t, share * (float)std::min(1.0, t / L.thread));
		poll = t + 1.0;
	}

	r.join = logged ? L.thread : std::max(L.thread, poll);

	// The engine's table for a build that reported nothing: the laps'.
	const int total = L.calls ? L.calls : kLoadBarBuildLaps;
	double t = r.join;
	float build = 0.0f;

	reach(t, LoadBarWork(1.0f, build, share), "join", false);
	t += L.ainit;
	reach(t, LoadBarWork(1.0f, build, share), "ainit", false);

	for (int i = 0; i < L.calls; i++)
	{
		build = LoadBarBuildShare(i + 1, total);
		reach(r.join + L.ainit + L.at[i], LoadBarWork(1.0f, build, share),
			i == 9 && total == kLoadBarBuildLaps ? "begin" :
			i == 15 ? "objects" : i == 4 && total == kLoadBarResidentSteps ?
			"life" : "step", true);
	}

	const double steps[3] = { L.build, L.font, L.cutscenes };
	const char* names[3] = { "build", "font", "cutscenes" };

	for (int i = 0; i < 3; i++)
	{
		t += steps[i];
		build = LoadBarBuildShare(total + 1 + i, total);
		reach(t, LoadBarWork(1.0f, build, share), names[i], i == 0);
	}

	t += L.warmup;
	reach(t, LoadBarWork(1.0f, build, share), "warmup", false);
	r.end = t + L.profile;

	while (!frame(std::max(handed + tick, r.end), 1.0f));

	return r;
}

static const BrakeLoad kBrakeLogged[] =
{
	// level, resident, join, ainit, build, font, cutscenes, warmup,
	// profile, hook calls (ms after the build's start)
	{ "TITLE", 0, 433.6, 0.0, 2593.7, 121.2, 2.3, 9.8, 0.0, 16,
		{ 13.6, 13.6, 13.6, 13.7, 14.6, 15.0, 15.0, 15.8,
		16.0, 2061.8, 2067.0, 2071.9, 2072.5, 2072.5, 2072.5, 2539.8 } },
	{ "ANDREA2", 0, 255.4, 0.0, 2667.4, 0.0, 0.0, 25.6, 0.0, 16,
		{ 8.0, 8.0, 8.0, 8.3, 8.9, 12.3, 12.3, 12.6,
		12.7, 2063.0, 2066.7, 2074.1, 2125.8, 2125.8, 2125.8, 2574.4 } },
	{ "ANDREA2", 1, 18.4, 0.0, 15.7, 0.0, 0.0, 0.4, 0.0, 5,
		{ 5.1, 5.1, 5.1, 9.4, 11.8 } },
	{ "ANDREA2", 1, 34.6, 0.0, 14.9, 0.0, 0.0, 0.4, 0.0, 5,
		{ 4.6, 4.6, 4.6, 8.8, 11.2 } },
	{ "ANDREA1", 0, 196.4, 0.0, 1370.1, 0.0, 0.0, 20.8, 0.0, 16,
		{ 7.4, 7.4, 7.4, 7.7, 8.2, 10.3, 10.3, 10.5,
		10.7, 953.8, 956.9, 960.0, 971.5, 971.5, 971.5, 1296.3 } },
	{ "ANDREA1", 1, 35.0, 0.0, 13.3, 0.0, 0.0, 0.4, 0.0, 5,
		{ 2.2, 2.2, 2.2, 6.8, 9.0 } },
	{ "ANDY2", 0, 167.6, 0.0, 1435.9, 0.0, 1.4, 27.8, 0.0, 16,
		{ 7.8, 7.8, 7.8, 8.1, 8.7, 9.9, 9.9, 10.1,
		10.5, 1022.4, 1025.3, 1031.6, 1045.0, 1045.0, 1045.0, 1370.4 } },
	{ "ANDY2", 1, 34.5, 0.0, 11.4, 0.0, 0.2, 0.4, 0.0, 5,
		{ 2.9, 2.9, 2.9, 7.1, 7.3 } },
	{ "ANDY2", 1, 19.0, 0.0, 10.8, 0.0, 0.2, 0.4, 0.0, 5,
		{ 2.4, 2.4, 2.4, 6.6, 6.8 } },
	{ "ANDY3", 0, 164.2, 0.0, 1513.9, 0.0, 0.0, 17.3, 0.0, 16,
		{ 13.3, 13.3, 13.3, 13.5, 14.4, 15.8, 15.8, 16.0,
		16.0, 1051.8, 1055.8, 1069.1, 1087.0, 1087.5, 1089.5, 1414.6 } },
	{ "ANDY3", 1, 18.4, 0.0, 16.8, 0.0, 0.0, 0.4, 0.0, 5,
		{ 7.7, 8.2, 8.4, 12.4, 12.8 } },
	{ "ANDY3", 1, 18.4, 0.0, 16.7, 0.0, 0.0, 0.4, 0.0, 5,
		{ 7.2, 7.7, 7.9, 12.2, 12.6 } },
	{ "JOBY5", 0, 214.6, 0.0, 1245.2, 0.0, 0.0, 23.9, 0.0, 16,
		{ 6.2, 6.2, 6.2, 6.4, 6.7, 7.9, 7.9, 8.0,
		8.2, 776.8, 778.6, 780.3, 790.0, 790.2, 790.2, 1173.9 } },
	{ "JOBY5", 1, 17.8, 0.0, 15.2, 0.0, 0.0, 0.4, 0.0, 5,
		{ 5.6, 5.9, 5.9, 10.9, 11.1 } },
	{ "RICH2", 0, 203.3, 0.0, 2355.9, 0.0, 0.0, 22.7, 0.0, 16,
		{ 9.7, 9.7, 9.7, 9.9, 10.5, 13.7, 13.7, 13.9,
		14.3, 1755.8, 1758.7, 1769.3, 1783.1, 1783.1, 1783.1, 2114.7 } },
	{ "RICH2", 1, 18.3, 0.0, 19.6, 0.0, 0.0, 0.6, 0.0, 5,
		{ 5.5, 5.5, 5.5, 12.6, 12.8 } },
	{ "RICHCUT2", 0, 35.4, 0.0, 893.4, 0.0, 1.4, 4.3, 0.0, 16,
		{ 37.0, 37.0, 37.0, 37.0, 37.1, 37.1, 37.1, 37.1,
		37.1, 712.8, 713.1, 713.6, 713.9, 713.9, 713.9, 861.4 } },
	{ "RICH1", 0, 150.2, 0.0, 1129.6, 0.0, 0.0, 19.7, 0.0, 16,
		{ 8.4, 8.4, 8.4, 8.6, 8.9, 9.9, 9.9, 10.0,
		10.2, 714.2, 717.4, 719.3, 719.7, 719.7, 719.7, 1065.6 } },
	{ "RICH1", 1, 35.8, 0.0, 9.5, 0.0, 0.0, 0.4, 0.0, 5,
		{ 0.4, 0.4, 0.4, 5.2, 5.2 } },
	{ "JOBY2", 0, 173.8, 0.0, 1688.7, 0.0, 0.0, 33.0, 0.0, 16,
		{ 12.5, 12.5, 12.5, 12.7, 13.9, 16.3, 16.3, 17.0,
		17.2, 1242.6, 1246.8, 1249.6, 1262.9, 1263.0, 1263.0, 1545.2 } },
	{ "JOBY2", 1, 18.2, 0.0, 58.1, 0.0, 0.0, 0.4, 0.0, 5,
		{ 3.1, 3.2, 3.2, 8.0, 53.6 } },
	{ "JOBY4", 0, 63.7, 0.0, 844.9, 0.0, 0.0, 6.6, 0.0, 16,
		{ 11.7, 11.7, 11.7, 11.8, 12.4, 13.0, 13.0, 13.1,
		13.1, 654.0, 656.8, 657.1, 668.7, 668.7, 668.7, 812.7 } },
	{ "JOBY4", 1, 18.3, 0.0, 8.5, 0.0, 0.0, 0.3, 0.0, 5,
		{ 1.4, 1.4, 1.4, 3.9, 4.1 } },
	{ "RICH3", 0, 170.7, 0.0, 1174.4, 0.0, 0.0, 22.2, 0.0, 16,
		{ 5.6, 5.6, 5.6, 5.8, 6.3, 8.1, 8.1, 8.3,
		8.5, 763.7, 769.0, 771.6, 772.2, 772.2, 772.2, 1102.4 } },
	{ "RICH3", 1, 18.3, 0.0, 9.8, 0.0, 0.0, 0.4, 0.0, 5,
		{ 0.5, 0.5, 0.5, 5.3, 5.3 } },
	{ "ANDY1", 0, 182.4, 0.0, 1352.5, 0.0, 0.0, 20.8, 0.0, 16,
		{ 6.4, 6.4, 6.4, 6.7, 7.3, 8.7, 8.7, 8.8,
		8.8, 900.0, 904.2, 932.2, 944.5, 944.6, 944.6, 1268.3 } },
	{ "ANDY1", 1, 18.5, 0.0, 11.4, 0.0, 0.0, 0.5, 0.0, 5,
		{ 1.9, 2.0, 2.0, 6.8, 7.0 } },
	{ "ANDREA3", 0, 147.4, 0.0, 1497.0, 0.0, 0.0, 16.6, 0.0, 16,
		{ 6.1, 6.1, 6.1, 6.2, 6.5, 7.2, 7.2, 7.3,
		7.3, 1167.0, 1170.1, 1171.6, 1172.0, 1172.0, 1172.0, 1438.4 } },
	{ "ANDREA3", 1, 18.6, 0.0, 9.4, 0.0, 0.0, 0.4, 0.0, 5,
		{ 0.4, 0.4, 0.4, 4.6, 4.6 } },
	{ "JOBY3", 0, 186.8, 0.0, 955.2, 0.0, 0.2, 18.9, 0.0, 16,
		{ 6.1, 6.1, 6.1, 6.3, 6.5, 7.4, 7.4, 7.5,
		7.6, 550.0, 552.1, 553.6, 554.0, 554.0, 554.0, 886.5 } },
	{ "JOBY3", 1, 18.5, 0.0, 10.6, 0.0, 0.3, 0.4, 0.0, 5,
		{ 0.4, 0.4, 0.4, 5.6, 5.6 } },
	{ "TITLE", 0, 107.9, 0.0, 2082.6, 0.0, 0.2, 6.2, 0.0, 16,
		{ 18.4, 18.4, 18.4, 18.5, 22.9, 23.5, 23.5, 23.8,
		24.0, 1867.9, 1873.1, 1878.0, 1878.5, 1878.5, 1878.5, 2028.4 } },
	{ "ANDREA2", 0, 151.0, 0.0, 2240.3, 0.0, 0.0, 23.7, 0.0, 16,
		{ 6.0, 6.0, 6.0, 6.3, 6.9, 9.2, 9.2, 9.4,
		9.4, 1866.6, 1870.2, 1877.5, 1883.9, 1883.9, 1883.9, 2147.0 } },
	{ "TITLE", 0, 118.6, 0.0, 2022.4, 0.0, 0.3, 7.4, 0.0, 16,
		{ 11.0, 11.0, 11.0, 11.1, 12.4, 13.0, 13.0, 13.3,
		13.5, 1804.5, 1809.7, 1814.7, 1815.3, 1815.3, 1815.3, 1968.3 } },
};

/* The brake (loadbar_math.h): its rule, three loads made up for it - a
 * save loaded inside its level, a fresh HD load, a load exactly as long as
 * the brake - and the 34 logged loads; then the engine's text: the frames
 * of the build that never wait, the tail, the lines that log it. */
static void load_brake()
{
	char* file = hd_switch_text("../file.cpp");
	char* bars = hd_switch_text("../drawbars.cpp");
	char* level = hd_switch_text("hd_level.cpp");
	char* opts = hd_switch_text("../../tomb5/tomb5.cpp");
	const double tick = 1000.0 / 60.0;
	long n, a, b;

	ok(file && bars && level && opts, "load brake: the four files are read",
		!!file + !!bars + !!level + !!opts, 4);

	// The rule: the smaller of the work and the clock, 100 when both are.
	ok(LoadBarPosition(1.0f, 0.0, 500.0) == 0.0f, "load brake: 0 at the "
		"first frame", (long)LoadBarPosition(1.0f, 0.0, 500.0), 0);
	ok(LoadBarPosition(1.0f, 250.0, 500.0) == 50.0f, "load brake: the work "
		"done, half the time", (long)LoadBarPosition(1.0f, 250.0, 500.0), 50);
	ok(fabs(LoadBarPosition(0.3f, 400.0, 500.0) - 30.0f) < 0.01f,
		"load brake: never ahead of the work",
		(long)(LoadBarPosition(0.3f, 400.0, 500.0) + 0.5f), 30);
	ok(LoadBarPosition(1.0f, 600.0, 500.0) == 100.0f, "load brake: full "
		"after the time", (long)LoadBarPosition(1.0f, 600.0, 500.0), 100);
	ok(LoadBarPosition(0.5f, 10.0, 0.0) == 50.0f, "load brake: 0 ms is the "
		"work", (long)LoadBarPosition(0.5f, 10.0, 0.0), 50);
	ok(LoadBarDone(1.0f, 500.0, 500.0), "load brake: done at the time",
		LoadBarDone(1.0f, 500.0, 500.0), 1);
	ok(!LoadBarDone(1.0f, 499.9, 500.0), "load brake: not before it",
		LoadBarDone(1.0f, 499.9, 500.0), 0);
	ok(!LoadBarDone(0.99f, 900.0, 500.0), "load brake: not before the work",
		LoadBarDone(0.99f, 900.0, 500.0), 0);
	ok(LoadBarWork(1.0f, 0.0f, 0.1f) == 0.1f, "load brake: the thread's "
		"share", (long)(LoadBarWork(1.0f, 0.0f, 0.1f) * 100), 10);
	ok(LoadBarWork(1.0f, 1.0f, 0.1f) == 1.0f, "load brake: the whole work",
		(long)(LoadBarWork(1.0f, 1.0f, 0.1f) * 100), 100);
	ok(LoadBarBuildShare(10, 16) == 0.730f, "load brake: the room build's "
		"lap", (long)(LoadBarBuildShare(10, 16) * 1000), 730);
	ok(LoadBarBuildShare(3, 5) == 0.189f, "load brake: a resident step",
		(long)(LoadBarBuildShare(3, 5) * 1000), 189);
	ok(LoadBarBuildShare(19, 16) == 0.989f && LoadBarBuildShare(30, 16) ==
		0.989f, "load brake: the cutscenes' point, and no further",
		(long)(LoadBarBuildShare(30, 16) * 1000), 989);
	ok(LoadBarBuildShare(2, 4) == 0.25f, "load brake: an even share of "
		"another count", (long)(LoadBarBuildShare(2, 4) * 100), 25);
	ok(LoadBarTicksMs(30.0 * kLoadBarTickMs - 0.002) == 30.0 *
		kLoadBarTickMs && LoadBarDone(1.0f, LoadBarTicksMs(499.998), 500.0),
		"load brake: the clock in whole ticks, 30 of them 500 ms",
		(long)LoadBarTicksMs(499.998), 500);
	ok(LoadBarTicksMs(5.0 * kLoadBarTickMs + 0.9) == 5.0 * kLoadBarTickMs,
		"load brake: a frame a little late keeps its tick",
		(long)(LoadBarTicksMs(5.0 * kLoadBarTickMs + 0.9) * 10), 833);
	{
		long back = 0;

		for (int i = 1; i < 16 + kLoadBarAfterSteps + 2; i++)
			back += LoadBarBuildShare(i + 1, 16) < LoadBarBuildShare(i, 16);

		for (int i = 1; i < 5 + kLoadBarAfterSteps + 2; i++)
			back += LoadBarBuildShare(i + 1, 5) < LoadBarBuildShare(i, 5);

		for (int w = 0; w <= 100; w++)
			for (int t = 0; t < 700; t += 7)
				back += LoadBarPosition(w / 100.0f, t + 7, 500.0) <
					LoadBarPosition(w / 100.0f, t, 500.0) ||
					(w && LoadBarPosition(w / 100.0f, t, 500.0) <
					LoadBarPosition((w - 1) / 100.0f, t, 500.0));

		ok(back == 0, "load brake: the laps and the rule never go back",
			back, 0);
	}

	// A save loaded inside its level, 52 ms of work: the clock leads.
	{
		const BrakeLoad save = { "save", 1, 35.0, 0.0, 15.0, 0.0, 0.0,
			2.0, 0.0, 5, { 3.0, 3.5, 4.0, 10.0, 14.0 } };
		const BrakeRun r = brake_run(save, 500.0, kLoadBarThreadShare,
			false);

		ok(r.back == 0 && r.ahead == 0, "load brake: 52 ms, monotonic and "
			"never ahead", r.back + r.ahead, 0);
		ok(r.full == 1, "load brake: 52 ms, one full frame, the last",
			r.full, 1);
		ok(r.last >= 500.0 - 1e-6 && r.last < 500.0 + tick,
			"load brake: 52 ms, full at the brake's 500", (long)r.last, 500);
		ok(r.frames >= 29 && r.frames <= 31, "load brake: 52 ms, a frame a "
			"tick", r.frames, 30);
		ok(r.maxJump <= 100.0f * (float)(tick / 500.0) + 0.001f,
			"load brake: 52 ms, no step over a tick's share",
			(long)(r.maxJump * 100), 333);
	}

	// A fresh HD load of 2.5 s: the work leads, the bar follows it.
	{
		BrakeLoad fresh = { "fresh", 0, 250.0, 0.0, 2200.0, 0.0, 0.0,
			50.0, 0.0, 16, {} };

		for (int i = 0; i < 16; i++)
			fresh.at[i] = 2200.0 * kLoadBarLapShare[i];

		const BrakeRun r = brake_run(fresh, 500.0, kLoadBarThreadShare,
			false);

		ok(r.back == 0 && r.ahead == 0, "load brake: 2.5 s, monotonic and "
			"never ahead of the work", r.back + r.ahead, 0);
		ok(r.full == 1, "load brake: 2.5 s, one full frame, the last",
			r.full, 1);
		ok(r.last >= r.end && r.last < r.end + tick, "load brake: 2.5 s, "
			"full at the work's end", (long)(r.last - r.end), 0);
		ok(r.buildFrames >= 2 && r.buildFrames <= 5, "load brake: 2.5 s, "
			"the build's laps draw a few frames", r.buildFrames, 4);
	}

	// A load exactly as long as the brake: full on the tick at 500.
	{
		const BrakeLoad even = { "even", 0, 433.4, 0.0, 0.0, 0.0, 0.0,
			65.6, 0.0, 0, {} };
		const BrakeRun r = brake_run(even, 500.0, 1.0f, false);

		ok(r.end >= 499.9 && r.end <= 500.1, "load brake: the made-up load "
			"ends at 500", (long)r.end, 500);
		ok(r.full == 1 && r.last >= 499.9 && r.last <= 500.1,
			"load brake: 500 ms, full on its last tick", (long)r.last, 500);
		ok(r.back == 0 && r.ahead == 0, "load brake: 500 ms, monotonic",
			r.back + r.ahead, 0);
	}

	// The 34 logged loads at 500 ms.
	{
		long bad = 0, late = 0, full = 0, saves = 0, fresh = 0;
		long saveFrames[2] = { 1000, 0 }, freshFrames[2] = { 1000, 0 };
		long saveOver = 0, freshOver = 0, buildFrames = 0, seen = 0;
		float saveJump = 0.0f, freshJump = 0.0f;
		const int count = sizeof(kBrakeLogged) / sizeof(kBrakeLogged[0]);

		for (int i = 0; i < count; i++)
		{
			const BrakeLoad& L = kBrakeLogged[i];
			const BrakeRun r = brake_run(L, 500.0, kLoadBarThreadShare,
				true);
			const double due = std::max(500.0, r.end);
			long* frames = L.resident ? saveFrames : freshFrames;

			bad += r.back + r.ahead;
			seen += r.overSeen;
			full += r.full != 1;
			late += r.last < due || r.last >= due + tick;
			frames[0] = std::min(frames[0], (long)r.frames);
			frames[1] = std::max(frames[1], (long)r.frames);

			if (L.resident)
			{
				saves++;
				saveOver += r.overTick;
				saveJump = std::max(saveJump, r.maxJump);
			}
			else
			{
				fresh++;
				freshOver += r.overTick;
				freshJump = std::max(freshJump, r.maxJump);
				buildFrames += r.buildFrames;
			}

			if (getenv("BRAKE_TABLE"))
				printf("    brake %-8s res=%d join=%.1f end=%.1f last=%.1f "
					"frames=%d build=%d over=%d jump=%.1f blind=%.1f %s\n",
					L.level, L.resident, r.join, r.end, r.last, r.frames,
					r.buildFrames, r.overTick, r.maxJump, r.blind,
					r.blindAt ? r.blindAt : "-");
		}

		ok(count == 34 && saves == 16 && fresh == 18, "load brake: 34 "
			"logged loads, 16 saves, 18 fresh", count, 34);
		ok(bad == 0, "load brake: logged, monotonic and never ahead", bad, 0);
		ok(full == 0, "load brake: logged, the full bar only last", full, 0);
		ok(late == 0, "load brake: logged, full at 500 or the work's end",
			late, 0);
		ok(seen == 0, "load brake: logged, a step over a tick's share only "
			"after a stretch of work with no frame point", seen, 0);
		ok(saveOver <= 1 && saveJump < 11.0f, "load brake: logged saves, "
			"one such step at most (JOBY2's 45 ms life)", saveOver, 1);
		printf("    brake, 16 saves: %ld to %ld frames, %ld steps over a "
			"tick's share, largest %.1f %%\n", saveFrames[0], saveFrames[1],
			saveOver, saveJump);
		printf("    brake, 18 fresh: %ld to %ld frames, %ld of the build's "
			"laps, %ld steps over a tick's share, largest %.1f %%\n",
			freshFrames[0], freshFrames[1], buildFrames, freshOver,
			freshJump);
	}

	// The engine: no frame of the build waits on the tick, the tail runs to
	// the brake's end, the lines that log it.
	a = load_keep_at(bars, "static void LoadScreenFrameDue(",
		"loadbar_lost || TIME_SubTick() < 1.0)");
	b = load_keep_at(bars, "static void LoadScreenFrameDue(",
		"S_DrawLoadBar();");
	ok(a >= 0 && a < b, "load brake: a frame after the join only past a "
		"tick", a < b, 1);
	a = load_keep_at(bars, "void S_LoadBarBuild(", "LoadScreenFrameDue(1);");
	b = load_keep_at(bars, "void S_LoadBarBuild(", "void S_LoadScreenStep(");
	ok(a >= 0 && a < b, "load brake: the build's reports draw through it",
		a < b, 1);
	n = hd_switch_count(file, "S_LoadScreenStep(LOADSTEP_");
	ok(n == 5, "load brake: aInit, the build, font, cutscenes, warmup", n,
		5);
	n = hd_switch_count(bars, "S_LoadScreenStep(LOADSTEP_JOIN);");
	ok(n == 1, "load brake: and the join", n, 1);
	n = hd_switch_count(file, "const long hd = tomb5.hd_build && "
		"HDInstallRoot()[0];");
	ok(n == 1, "load brake: the thread's share when a build follows", n, 1);
	n = hd_switch_count(bars, "const double elapsed = LoadBarTicksMs(");
	ok(n == 1, "load brake: the bar's time in whole ticks", n, 1);

	// Between the join and the tail no present resets a lost device.
	a = load_keep_at(bars, "static void LoadBarPresentHeld()",
		"Present(0, 0, 0, 0);");
	b = load_keep_at(bars, "static void LoadBarPresentHeld()",
		"loadbar_lost = 1;");
	ok(a >= 0 && a < b, "load brake: the held present notes a lost "
		"device", a < b, 1);
	n = hd_switch_count(bars, "DXShowFrame(");
	ok(n == 0, "load brake: and never resets it there", n, 0);
	n = hd_switch_count(bars,
		"if (loadbar_held)\r\n\t\tLoadBarPresentHeld();") +
		hd_switch_count(bars, "if (loadbar_held)\n\t\tLoadBarPresentHeld();");
	ok(n == 1, "load brake: a frame presents held after the join", n, 1);
	a = load_keep_at(bars, "void S_LoadScreenJoin()", "loadbar_held = ");
	b = load_keep_at(bars, "void S_LoadScreenEnd()", "loadbar_held = 0;");
	ok(a >= 0 && b > a && b < load_keep_at(bars, "void S_LoadScreenEnd()",
		"while (!S_DrawLoadBar());"), "load brake: held from the join, the "
		"tail presents as ever", b > a, 1);

	a = load_keep_at(bars, "long S_DrawLoadBar()",
		"while (TIME_SubTick() < 1.0);");
	b = load_keep_at(bars, "long S_DrawLoadBar()", "_BeginScene();");
	ok(a >= 0 && a < b, "load brake: the tick before the position", a < b,
		1);
	n = hd_switch_count(bars, "return LoadBarDone(work, elapsed, "
		"loadbar_min_ms);");
	ok(n == 1, "load brake: a frame says when the brake is done", n, 1);
	n = hd_switch_count(bars, "while (!S_DrawLoadBar());");
	ok(n == 1, "load brake: the tail draws to the brake's end", n, 1);
	n = hd_switch_count(level, "S_LoadBarBuild(phase + 1, LOAD_PHASES);");
	ok(n == 1, "load brake: every lap of the build reports", n, 1);
	n = hd_switch_count(level, "S_LoadBarBuild(");
	ok(n == 6, "load brake: the laps and the resident build's 5 steps", n,
		6);
	n = hd_switch_count(file, "LoadLogStepBar(&loadlog_at, shown);");
	ok(n == 1, "load brake: the bar step logs frames and wait", n, 1);
	n = hd_switch_count(file, "step=hd_frames ms=%.1f frames=%ld");
	ok(n == 1, "load brake: the build's frames on a line", n, 1);
	n = hd_switch_count(opts, "sprintf(buf, \"loadbar_min_ms\");");
	ok(n == 3, "load brake: loadbar_min_ms written, read and saved", n, 3);
	n = hd_switch_count(opts,
		"REG_ReadLong(buf, tomb5.loadbar_min_ms, 500);");
	ok(n == 1, "load brake: absent reads as 500", n, 1);

	free(file);
	free(bars);
	free(level);
	free(opts);
}

/* The page class of the HD room renderer (specific\hd\hd_pageclass.h): which
 * alpha-pass pages are drawn as a clear floor, out of the smallest and the
 * largest alpha of the page's top mip. The pairs are the measured ones where
 * a page is named. */
static void page_class()
{
	// RICH2's lobby floor, 0.917 in every texel.
	const int floor = (int)(0.917 * 255.0 + 0.5);

	ok(HDPageClassOf(floor, floor) == HD_PAGE_CLEAR,
		"page: 0.917 in every texel is clear", HDPageClassOf(floor, floor), HD_PAGE_CLEAR);
	ok(HDPageClassOf(232, 236) == HD_PAGE_CLEAR,
		"page: RICH2 page 1047 (232..236) is clear", HDPageClassOf(232, 236), HD_PAGE_CLEAR);
	ok(HDPageClassOf(231, 239) == HD_PAGE_CLEAR,
		"page: RICH2 page 1029 (231..239) is clear", HDPageClassOf(231, 239), HD_PAGE_CLEAR);
	ok(HDPageClassOf(231, 255) == HD_PAGE_CLEAR,
		"page: RICH2's inlaid floor 3314 (231..255) is clear", HDPageClassOf(231, 255), HD_PAGE_CLEAR);
	ok(HDPageClassOf(174, 255) == HD_PAGE_CLEAR,
		"page: TITLE page 2154 (174..255) is clear", HDPageClassOf(174, 255), HD_PAGE_CLEAR);
	ok(HDPageClassOf(233, 255) == HD_PAGE_CLEAR,
		"page: TITLE page 2149 (233..255) is clear", HDPageClassOf(233, 255), HD_PAGE_CLEAR);

	// A page that cuts somewhere is a cut-out, whatever the rest of it is.
	ok(HDPageClassOf(0, 255) == HD_PAGE_CUTOUT,
		"page: 0 and 1 is a cut-out", HDPageClassOf(0, 255), HD_PAGE_CUTOUT);
	ok(HDPageClassOf(77, 255) == HD_PAGE_CUTOUT,
		"page: 0.3 .. 1 is a cut-out", HDPageClassOf(77, 255), HD_PAGE_CUTOUT);
	ok(HDPageClassOf(0, 30) == HD_PAGE_CUTOUT,
		"page: a cloud sheet (0..30) is a cut-out", HDPageClassOf(0, 30), HD_PAGE_CUTOUT);

	// And a page at or near 255 everywhere is opaque.
	ok(HDPageClassOf(255, 255) == HD_PAGE_OPAQUE,
		"page: 1 in every texel is opaque", HDPageClassOf(255, 255), HD_PAGE_OPAQUE);
	ok(HDPageClassOf(254, 255) == HD_PAGE_OPAQUE,
		"page: JOBY3 page 14070 (254..255) is opaque", HDPageClassOf(254, 255), HD_PAGE_OPAQUE);

	// The two edges: the alpha test's reference and the clear top.
	ok(HDPageClassOf(HD_PAGE_TEST_REF, 255) == HD_PAGE_CLEAR,
		"page: a smallest alpha at the test's reference is clear",
		HDPageClassOf(HD_PAGE_TEST_REF, 255), HD_PAGE_CLEAR);
	ok(HDPageClassOf(HD_PAGE_TEST_REF - 1, 255) == HD_PAGE_CUTOUT,
		"page: one below the reference is a cut-out",
		HDPageClassOf(HD_PAGE_TEST_REF - 1, 255), HD_PAGE_CUTOUT);
	ok(HDPageClassOf(HD_PAGE_CLEAR_TOP, 255) == HD_PAGE_CLEAR,
		"page: a smallest alpha at the clear top is clear",
		HDPageClassOf(HD_PAGE_CLEAR_TOP, 255), HD_PAGE_CLEAR);
	ok(HDPageClassOf(HD_PAGE_CLEAR_TOP + 1, 255) == HD_PAGE_OPAQUE,
		"page: one above the clear top is opaque",
		HDPageClassOf(HD_PAGE_CLEAR_TOP + 1, 255), HD_PAGE_OPAQUE);
	ok(HD_PAGE_CLEAR_TOP > 233 && HD_PAGE_CLEAR_TOP < 254,
		"page: the clear top lies in the measured gap 233..254", HD_PAGE_CLEAR_TOP, 243);

	// Nothing measured - the smallest still above the largest - is opaque.
	ok(HDPageClassOf(255, 0) == HD_PAGE_OPAQUE,
		"page: a page nothing was measured on is opaque", HDPageClassOf(255, 0), HD_PAGE_OPAQUE);
}

/* The stand rule (specific\hd\hd_cover.h).
 *
 * stand_draw: HDStandDraw over every input. While the room is listed its
 * place comes from exactly one source - the classic roomlets, or the
 * carrier's art with its group, or the carrier's art in the room's window -
 * never two and never none; the art that lies on the faces whole is drawn
 * only with the carrier or while the room is not listed; art that only
 * grazes the faces is drawn with its group whenever that is listed, and
 * pushed back exactly when it reaches onto the faces of a listed room drawn
 * classic; nothing is drawn in a window of a room that is not listed. And the
 * four list states of ANDREA2's room 124 and its carrier, group 121, one by
 * one, and RICH2 16's grazing art.
 *
 * stand_cover: the threshold - every face of a room with faces - and the
 * coverage count of HDCoverFaces on a floor sector covered by its two
 * halves, by one half, and by art a reach too far off.
 *
 * stand_overlap: which art triangle stands on a face (HDCoverOverlaps) and
 * which lies on it whole (HDCoverTriOnFaces): a copy of the face, the same
 * within the reach and past it, turned away, double-sided, touching only an
 * edge, and reaching half off the face. */
static HDCoverFace stand_floor(double x0, double z0, double size, double y)
{
	// A floor sector seen from above, TR5's y down: (b - a) x (c - a) points
	// to +y, away from the side it is seen from, as the game's classic floors
	// are wound (hd_cover.h, HD_COVER_UP_SIGN).
	HDCoverFace f;

	f.corners = 4;
	f.doubleSided = 0;
	const double v[4][3] =
	{
		{ x0, y, z0 }, { x0, y, z0 + size }, { x0 + size, y, z0 + size }, { x0 + size, y, z0 }
	};

	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++)
			f.v[c][k] = v[c][k];

	return f;
}

static bool stand_tri(double ax, double az, double bx, double bz, double cx, double cz,
	double y, HDCoverTri* out)
{
	const float v[3][3] =
	{
		{ (float)ax, (float)y, (float)az }, { (float)bx, (float)y, (float)bz },
		{ (float)cx, (float)y, (float)cz }
	};
	const float origin[3] = { 0.0f, 0.0f, 0.0f };

	return HDCoverMakeTri(v, origin, 121, out);
}

static void stand_draw()
{
	int sources_bad = 0, both = 0, window_unlisted = 0, copy_kept = 0;
	int graze_lost = 0, classic_wrong = 0, unlisted_wrong = 0, behind_wrong = 0;

	for (int bits = 0; bits < 64; bits++)
	{
		const bool holder = (bits & 1) != 0, art = (bits & 2) != 0;
		const bool covered = (bits & 4) != 0, carrier = (bits & 8) != 0;
		const bool onFaces = (bits & 16) != 0, grazes = (bits & 32) != 0;

		// A range is one kind: on the faces whole, grazing them, or neither.
		if (onFaces && grazes)
			continue;

		const HDStandAnswer a = HDStandDraw(holder, art, covered, carrier, onFaces,
			grazes);

		/* Pushed back: drawn with its group, grazing the faces of a listed
		 * room that is drawn classic - and never otherwise. */
		if (a.behind != (a.withArt && holder && !covered && grazes))
			behind_wrong++;

		if (a.withArt && a.inHolder)
			both++;

		if (a.inHolder && (!holder || art))
			window_unlisted++;

		if (a.classic != (holder && !covered))
			classic_wrong++;

		// Not listed: nothing of its own, the art with its group.
		if (!holder && (a.classic || a.inHolder || a.withArt != art))
			unlisted_wrong++;

		// A copy of the faces drawn while the room is listed: only the carrier.
		if (holder && onFaces && (a.withArt || a.inHolder) && !(covered && carrier))
			copy_kept++;

		// Grazing art is never left out where its group is listed.
		if (!onFaces && art && !a.withArt)
			graze_lost++;

		/* The sources of a listed room's place: its roomlets, or the carrier
		 * range of a covered room (asked with carrier and onFaces set). */
		if (holder && carrier && onFaces)
		{
			const int n = (a.classic ? 1 : 0) + (covered ? ((a.withArt ? 1 : 0) +
				(a.inHolder ? 1 : 0)) : 0);

			if (n != 1)
				sources_bad++;
		}
	}

	ok(both == 0, "stand: never with the group and in the window at once", both, 0);
	ok(window_unlisted == 0, "stand: the window only for a listed room, art off the list",
		window_unlisted, 0);
	ok(classic_wrong == 0, "stand: classic exactly when listed and not covered",
		classic_wrong, 0);
	ok(unlisted_wrong == 0, "stand: a room off the list draws nothing of its own",
		unlisted_wrong, 0);
	ok(copy_kept == 0, "stand: a copy of the faces is left out but the carrier's",
		copy_kept, 0);
	ok(graze_lost == 0, "stand: grazing art is drawn wherever its group is", graze_lost, 0);
	ok(sources_bad == 0, "stand: a listed room's place has exactly one source",
		sources_bad, 0);
	ok(behind_wrong == 0, "stand: pushed back exactly where grazing art meets classic",
		behind_wrong, 0);

	// ANDREA2 124, covered, carrier 121: the four list states.
	HDStandAnswer a = HDStandDraw(true, true, true, true, true, false);
	ok(!a.classic && a.withArt && !a.inHolder, "stand: 124 and 121 listed - 121 draws it",
		a.withArt, 1);
	a = HDStandDraw(true, false, true, true, true, false);
	ok(!a.classic && !a.withArt && a.inHolder, "stand: 124 listed alone - in 124's window",
		a.inHolder, 1);
	a = HDStandDraw(false, true, true, true, true, false);
	ok(!a.classic && a.withArt && !a.inHolder, "stand: 121 listed alone - with 121",
		a.withArt, 1);
	a = HDStandDraw(false, false, true, true, true, false);
	ok(!a.classic && !a.withArt && !a.inHolder, "stand: neither listed - nothing", a.withArt, 0);
	// Group 127's second copy of 124's floor, 121 the carrier.
	a = HDStandDraw(true, true, true, false, true, false);
	ok(!a.withArt && !a.inHolder, "stand: 127's copy left out while 124 is listed",
		a.withArt, 0);
	// Group 117's palm trunk, grazing 124's faces: 124 is drawn as art.
	a = HDStandDraw(true, true, true, false, false, true);
	ok(a.withArt && !a.behind, "stand: 117's trunk drawn with 117, in place", a.behind, 0);
	// RICH2 16, not covered: classic, group 14's copy out, its grazing art in.
	a = HDStandDraw(true, true, false, false, true, false);
	ok(a.classic && !a.withArt, "stand: RICH2 16 classic, 14's copy out", a.classic, 1);
	a = HDStandDraw(true, true, false, false, false, true);
	ok(a.classic && a.withArt, "stand: RICH2 16, 14's grazing art kept", a.withArt, 1);
	ok(a.behind, "stand: RICH2 16, 14's grazing art pushed back", a.behind, 1);
	// 16 off the list: the same art in place.
	a = HDStandDraw(false, true, false, false, false, true);
	ok(a.withArt && !a.behind, "stand: RICH2 16 off the list - 14's art in place",
		a.behind, 0);
	// Art in 16's box on no face is not pushed.
	a = HDStandDraw(true, true, false, false, false, false);
	ok(a.withArt && !a.behind, "stand: RICH2 16, art on no face in place", a.behind, 0);
}

/* The visibility path's tables (hd_vis.h) on a level of three rooms built by
 * hand: A (0) and B (1) side by side with a portal between them, sharing the
 * wall ring at x 5120..6144, and C (2) standing inside A with no link. A's
 * art covers its west wall face and leaves its east one bare; B's art and
 * C's art each reach out of their own room into A's inside, eight small
 * triangles each. */
static void vis_tri(trf::remaster::Trg& trg, float x, float y, float z,
	const float a[3], const float b[3], const float c[3])
{
	const float* corner[3] = { a, b, c };

	for (int k = 0; k < 3; k++)
	{
		trf::remaster::TrgVertex v;

		v.x = (int16_t)(corner[k][0] - x);
		v.y = (int16_t)(corner[k][1] - y);
		v.z = (int16_t)(corner[k][2] - z);
		trg.indices.push_back((uint32_t)trg.vertices.size());
		trg.vertices.push_back(v);
	}
}

static void vis_tables()
{
	trf::remaster::Trg trg;
	std::vector<HDVisRoom> rooms(3);
	std::vector<HDCoverFace> faces(2);
	std::vector<HDVisPortal> portals(2);
	const long x[3] = { 0, 5120, 2048 }, z[3] = { 0, 0, 2048 };
	const int xs[3] = { 6, 6, 4 };

	for (int r = 0; r < 3; r++)
	{
		HDVisRoom& one = rooms[(size_t)r];

		one.x = x[r];
		one.y = 0;
		one.z = z[r];
		one.xs = xs[r];
		one.zs = xs[r];
		one.top = -4096;
		one.bottom = 0;
		one.flags = 0;
		one.twin = -1;
		one.firstFace = 0;
		one.faceCount = r == 0 ? 2 : 0;
		one.firstPortal = r < 2 ? r : 0;
		one.portalCount = r < 2 ? 1 : 0;
		one.firstLink = 0;
		one.linkCount = 0;
	}

	// A's west wall (normal -x, its outside) and east wall (+x).
	const double w[4][3] = { { 1024, 0, 3072 }, { 1024, 0, 2048 },
		{ 1024, -1024, 2048 }, { 1024, -1024, 3072 } };
	const double e[4][3] = { { 5120, 0, 2048 }, { 5120, 0, 3072 },
		{ 5120, -1024, 3072 }, { 5120, -1024, 2048 } };

	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++)
		{
			faces[0].v[c][k] = w[c][k];
			faces[1].v[c][k] = e[c][k];
		}

	faces[0].corners = faces[1].corners = 4;
	faces[0].doubleSided = faces[1].doubleSided = 0;

	// The portal on A's east wall beside the bare face, and B's back.
	const short pa[4][3] = { { 5120, 0, 3072 }, { 5120, 0, 4096 },
		{ 5120, -1024, 4096 }, { 5120, -1024, 3072 } };

	portals[0].to = 1;
	portals[1].to = 0;
	portals[0].n[0] = -1;
	portals[1].n[0] = 1;
	portals[0].n[1] = portals[0].n[2] = portals[1].n[1] = portals[1].n[2] = 0;

	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++)
		{
			portals[0].v[c][k] = pa[c][k];
			portals[1].v[c][k] = (short)(k == 0 ? 0 : pa[c][k]);
		}

	trg.groups.resize(3);

	for (int g = 0; g < 3; g++)
	{
		trf::remaster::TrgPart part;

		part.pass[0].start = (uint32_t)trg.indices.size();

		if (g == 0)
		{
			const float a[3] = { 1024, 0, 3072 }, b[3] = { 1024, 0, 2048 };
			const float c[3] = { 1024, -1024, 2048 };
			const float d[3] = { 1024, -1024, 3072 };

			vis_tri(trg, 0, 0, 0, a, b, c);
			vis_tri(trg, 0, 0, 0, a, c, d);
		}
		else
			for (int t = 0; t < 8; t++)
			{
				// Art at x 3000 (B's) or 1500 (C's), out of its own room.
				const float cx = g == 1 ? 3000.0f : 1500.0f;
				const float a[3] = { cx, -2000.0f, 3000.0f + t * 16.0f };
				const float b[3] = { cx + 8.0f, -2000.0f, 3000.0f + t * 16.0f };
				const float c[3] = { cx, -2008.0f, 3008.0f + t * 16.0f };

				vis_tri(trg, (float)x[g], 0, (float)z[g], a, b, c);
			}

		part.pass[0].count = (uint32_t)trg.indices.size() - part.pass[0].start;
		trg.groups[(size_t)g].parts.push_back(part);
	}

	HDVisTables t;
	const HDVisParams p = HDVisDefaults();

	HDVisBuild(trg, &rooms[0], 3, &faces[0], 2, &portals[0], 2, 0, 0, p, &t);

	ok(t.guestCount[0] == 1 && t.guests[(size_t)t.guestFirst[0]].group == 1,
		"vis: A's one guest is B, linked, its art out of B", t.guestCount[0],
		1);
	ok(t.guestsUnlinked == 1, "vis: C, unlinked, is no guest of A under no sky",
		t.guestsUnlinked, 1);
	ok(t.artRooms == 1 && t.wallFaces == 2 && t.bareFaces == 1,
		"vis: A's walls asked, the east face bare", t.bareFaces, 1);
	ok(t.openCount[0] == 1 && t.openings[(size_t)t.openFirst[0]].behind == 1,
		"vis: the opening lists B, the room behind", t.openCount[0], 1);
	ok(t.openingsUnlinked == 1, "vis: and not C, standing in A unlinked",
		t.openingsUnlinked, 1);
	ok(t.openings.size() == 1 && t.openings[0].n[0] < 0,
		"vis: the opening's normal points into A", t.openings[0].n[0], -1024);
	ok(t.wideCount[0] == 1 && t.wides[0].portal == 0 &&
		t.wides[0].v[0][2] == 2048,
		"vis: A's portal widened over the bare face", t.wides[0].v[0][2], 2048);
	ok(t.wideCount[1] == 1 && t.wideMirrored == 1 &&
		t.wides[(size_t)t.wideFirst[1]].v[0][0] == 0 &&
		t.wides[(size_t)t.wideFirst[1]].v[0][2] == 2048,
		"vis: B's record of the portal widened by A's bare face too",
		t.wideCount[1], 1);

	// Under the sky A takes C's art reaching out of C too.
	rooms[0].flags = 8;
	HDVisBuild(trg, &rooms[0], 3, &faces[0], 2, &portals[0], 2, 0, 0, p, &t);
	ok(t.guestCount[0] == 2, "vis: A under the sky has B and C as guests",
		t.guestCount[0], 2);

	// Art standing inside its own room's box is no guest, whatever the rule.
	HDVisParams own = p;

	own.overhang = 4096.0;
	HDVisBuild(trg, &rooms[0], 3, &faces[0], 2, &portals[0], 2, 0, 0, own, &t);
	ok(t.guestCount[0] == 0, "vis: art within its own room's box is no guest",
		t.guestCount[0], 0);

	// A face beside a portal widens it only at wideBare or more; the
	// openings keep their own share.
	HDVisParams narrow = p;

	rooms[0].flags = 0;
	narrow.wideBare = 1.01;
	HDVisBuild(trg, &rooms[0], 3, &faces[0], 2, &portals[0], 2, 0, 0, narrow,
		&t);
	ok(t.wideCount[0] == 0 && t.openCount[0] == 1,
		"vis: a face less bare than wideBare widens no portal",
		t.wideCount[0], 0);
	ok(p.edge * 2560.0 > 1.99 && p.edge * 2560.0 < 2.01,
		"vis: the window's edge is 2 pixels at 2,560", (int)(p.edge * 2560.0),
		2);

	// B the other room of C's flipmap pair: the opening behind A's bare
	// face names C, the pair's main room, once.
	rooms[2].twin = 1;
	HDVisBuild(trg, &rooms[0], 3, &faces[0], 2, &portals[0], 2, 0, 0, p, &t);
	ok(t.openCount[0] == 1 && t.openings[(size_t)t.openFirst[0]].behind == 2 &&
		t.openingsTwin == 1,
		"vis: an opening behind a flipmap twin names the main room",
		t.openCount[0] ? t.openings[(size_t)t.openFirst[0]].behind : -1, 2);
	rooms[2].twin = -1;
}

/* The plane of a claimed face (HDVisFacesShareAPlane) on the faces of
 * ANDREA1's brick block of room 93 standing in room 101, as the build hands
 * them over (room-local): its floor at y -3584, its roof at y -5376, its wall
 * at z 5120 in two faces and its wall at x 6144; and on room 101's facade at
 * x 5120, whose faces are one wall in one plane. */
static void vis_plane()
{
	const float floor606[4][3] = { { 5120, -3584, 5120 }, { 5120, -3584, 6144 },
		{ 6144, -3584, 6144 }, { 6144, -3584, 5120 } };
	const float roof607[4][3] = { { 5120, -5376, 6144 }, { 5120, -5376, 5120 },
		{ 6144, -5376, 5120 }, { 6144, -5376, 6144 } };
	const float wall608[4][3] = { { 6144, -4608, 5120 }, { 5120, -4608, 5120 },
		{ 5120, -3584, 5120 }, { 6144, -3584, 5120 } };
	const float wall609[4][3] = { { 6144, -5376, 5120 }, { 5120, -5376, 5120 },
		{ 5120, -4608, 5120 }, { 6144, -4608, 5120 } };
	const float wall611[4][3] = { { 6144, -4608, 6144 }, { 6144, -4608, 5120 },
		{ 6144, -3584, 5120 }, { 6144, -3584, 6144 } };
	const float facade696[4][3] = {
		{ 5120, -3584, 5120 }, { 5120, -3584, 4096 },
		{ 5120, -4608, 4096 }, { 5120, -4608, 5120 } };
	const float facade698[4][3] = {
		{ 5120, -3584, 6144 }, { 5120, -3584, 5120 },
		{ 5120, -4608, 5120 }, { 5120, -4608, 6144 } };
	const float sloped699[4][3] = {
		{ 5120, -5376, 5120 }, { 5120, -5632, 6144 },
		{ 5120, -4608, 6144 }, { 5120, -4608, 5120 } };
	const float tri[4][3] = { { 0, 0, 0 }, { 1024, 0, 0 }, { 0, 0, 1024 },
		{ 0, 0, 0 } };
	const float line[4][3] = { { 0, 0, 0 }, { 512, 0, 0 }, { 1024, 0, 0 },
		{ 0, 0, 1024 } };
	const double tol = HD_VIS_PLANE_TOLERANCE;

	ok(!HDVisFacesShareAPlane(floor606, 4, wall608, 4, tol),
		"plane: the block's floor drawn keeps its z wall out", 0, 0);
	ok(!HDVisFacesShareAPlane(floor606, 4, wall611, 4, tol),
		"plane: the block's floor drawn keeps its x wall out", 0, 0);
	ok(!HDVisFacesShareAPlane(floor606, 4, roof607, 4, tol),
		"plane: a parallel face 1,792 units off is another plane", 0, 0);
	ok(HDVisFacesShareAPlane(wall608, 4, wall609, 4, tol),
		"plane: the two faces of one wall share it", 1, 1);
	ok(HDVisFacesShareAPlane(wall609, 4, wall608, 4, tol),
		"plane: and either way round", 1, 1);
	ok(HDVisFacesShareAPlane(facade696, 4, facade698, 4, tol),
		"plane: the facade's faces at x 5120 are one wall", 1, 1);
	ok(HDVisFacesShareAPlane(facade698, 4, sloped699, 4, tol),
		"plane: a facade face with a sloped top edge is still that wall", 1,
		1);
	ok(HDVisFacesShareAPlane(floor606, 4, floor606, 4, tol),
		"plane: a face shares its own plane", 1, 1);
	ok(HDVisFacesShareAPlane(tri, 3, floor606, 4, tol) == false,
		"plane: a triangle at y 0 is not the floor at y -3584", 0, 0);
	ok(HDVisFacesShareAPlane(line, 4, tri, 3, tol),
		"plane: three corners on a line take the first, third and fourth", 1,
		1);
	ok(!HDVisFacesShareAPlane(line, 2, tri, 3, tol),
		"plane: a face of two corners has no plane", 0, 0);
	const float near1[4][3] = { { 6144, -4608, 5121 }, { 5120, -4608, 5121 },
		{ 5120, -3584, 5121 }, { 6144, -3584, 5121 } };
	const float near2[4][3] = { { 6144, -4608, 5122 }, { 5120, -4608, 5122 },
		{ 5120, -3584, 5122 }, { 6144, -3584, 5122 } };

	ok(HDVisFacesShareAPlane(wall608, 4, near1, 4, tol),
		"plane: one unit off is in the plane", 1, 1);
	ok(!HDVisFacesShareAPlane(wall608, 4, near2, 4, tol),
		"plane: two units off is not", 0, 0);
}

/* The wall of a claimed face (HDVisFaceUpright, HDVisFacesShareAnEdge,
 * HDVisFacesOneWall, HDVisWallDrawn): ANDREA3's wooden wall of room 8 in
 * room 115's air, faces 76 and 77 at z 3072 and face 78 at x 0 round the
 * corner, in the seed room's frame as RoomProbe -visfaces prints them; and
 * ANDREA1's brick block of room 93, whose drawn floor is no part of its
 * walls and roof. */
static void vis_wall()
{
	const float f76[4][3] = { { -1024, -1024, 3072 }, { -2048, -1024, 3072 },
		{ -2048, 0, 3072 }, { -1024, 0, 3072 } };
	const float f77[4][3] = { { 0, -1024, 3072 }, { -1024, -1024, 3072 },
		{ -1024, 0, 3072 }, { 0, 0, 3072 } };
	const float f78[4][3] = { { 0, -1024, 4096 }, { 0, -1024, 3072 },
		{ 0, 0, 3072 }, { 0, 0, 4096 } };
	const float floor606[4][3] = { { 5120, -3584, 5120 }, { 5120, -3584, 6144 },
		{ 6144, -3584, 6144 }, { 6144, -3584, 5120 } };
	const float roof607[4][3] = { { 5120, -5376, 6144 }, { 5120, -5376, 5120 },
		{ 6144, -5376, 5120 }, { 6144, -5376, 6144 } };
	const float wall608[4][3] = { { 6144, -4608, 5120 }, { 5120, -4608, 5120 },
		{ 5120, -3584, 5120 }, { 6144, -3584, 5120 } };
	const float wall609[4][3] = { { 6144, -5376, 5120 }, { 5120, -5376, 5120 },
		{ 5120, -4608, 5120 }, { 6144, -4608, 5120 } };
	const float wall611[4][3] = { { 6144, -4608, 6144 }, { 6144, -4608, 5120 },
		{ 6144, -3584, 5120 }, { 6144, -3584, 6144 } };
	// A slope: one corner raised, the plane leans off the upright.
	const float slope[4][3] = { { 0, 0, 0 }, { 1024, -256, 0 },
		{ 1024, -256, 1024 }, { 0, 0, 1024 } };
	// Touching f78 at the one corner (0, 0, 4096) only.
	const float touch[4][3] = { { 0, 0, 4096 }, { 1024, 0, 4096 },
		{ 1024, 1024, 4096 }, { 0, 1024, 4096 } };
	// Half as tall as f78's neighbour across the seam at x 0, z 3072.
	const float half[4][3] = { { 0, -512, 3072 }, { -1024, -512, 3072 },
		{ -1024, 0, 3072 }, { 0, 0, 3072 } };
	const double tol = HD_VIS_PLANE_TOLERANCE;

	ok(HDVisFaceUpright(f78, 4), "wall: face 78 stands upright", 1, 1);
	ok(HDVisFaceUpright(f76, 4), "wall: face 76 stands upright", 1, 1);
	ok(!HDVisFaceUpright(floor606, 4), "wall: a floor does not", 0, 0);
	ok(!HDVisFaceUpright(roof607, 4), "wall: a roof does not", 0, 0);
	ok(!HDVisFaceUpright(slope, 4), "wall: a slope does not", 0, 0);
	ok(!HDVisFaceUpright(f78, 2), "wall: two corners are no face", 0, 0);

	ok(HDVisFacesShareAnEdge(f77, 4, f78, 4, tol),
		"wall: 77 and 78 meet along the corner", 1, 1);
	ok(HDVisFacesShareAnEdge(f78, 4, f77, 4, tol),
		"wall: and either way round", 1, 1);
	ok(HDVisFacesShareAnEdge(f76, 4, f77, 4, tol),
		"wall: 76 and 77 meet along their seam", 1, 1);
	ok(!HDVisFacesShareAnEdge(f76, 4, f78, 4, tol),
		"wall: 76 and 78 do not touch", 0, 0);
	ok(!HDVisFacesShareAnEdge(f78, 4, touch, 4, tol),
		"wall: one corner in common is no edge", 0, 0);
	ok(HDVisFacesShareAnEdge(f78, 4, half, 4, tol),
		"wall: half the corner's height is an edge", 1, 1);

	ok(HDVisFacesOneWall(f78, 4, f77, 4, tol),
		"wall: 77 is one wall with 78 round the corner", 1, 1);
	ok(!HDVisFacesOneWall(f78, 4, f76, 4, tol),
		"wall: 76 is not, face to face", 0, 0);
	ok(HDVisFacesOneWall(f77, 4, f76, 4, tol),
		"wall: 76 is one wall with 77 in its plane", 1, 1);
	ok(!HDVisFacesOneWall(floor606, 4, wall608, 4, tol),
		"wall: the block's floor is not one wall with its side", 0, 0);
	ok(HDVisFacesOneWall(wall608, 4, wall611, 4, tol),
		"wall: the block's two sides are one wall round its corner", 1, 1);
	ok(!HDVisFacesOneWall(floor606, 4, roof607, 4, tol),
		"wall: nor its floor with its roof", 0, 0);

	HDVisWallFace andrea3[3] = { { f76, 4, false }, { f77, 4, false },
		{ f78, 4, true } };

	ok(HDVisWallDrawn(andrea3, 3, 0, tol),
		"wall: 78 drawn keeps 76 whole, through 77", 1, 1);
	ok(HDVisWallDrawn(andrea3, 3, 1, tol),
		"wall: 78 drawn keeps 77 whole", 1, 1);
	ok(HDVisWallDrawn(andrea3, 3, 2, tol),
		"wall: a drawn face keeps itself whole", 1, 1);
	andrea3[2].drawn = false;
	ok(!HDVisWallDrawn(andrea3, 3, 0, tol),
		"wall: none drawn keeps none whole", 0, 0);

	HDVisWallFace andrea1[5] = { { floor606, 4, true },
		{ roof607, 4, false }, { wall608, 4, false }, { wall609, 4, false },
		{ wall611, 4, false } };

	ok(!HDVisWallDrawn(andrea1, 5, 1, tol),
		"wall: the block's floor drawn cuts its roof", 0, 0);
	ok(!HDVisWallDrawn(andrea1, 5, 2, tol),
		"wall: and its wall at z 5120", 0, 0);
	ok(!HDVisWallDrawn(andrea1, 5, 4, tol),
		"wall: and its wall at x 6144", 0, 0);
	ok(HDVisWallDrawn(andrea1, 5, 0, tol),
		"wall: and keeps the floor itself", 1, 1);
	andrea1[2].drawn = true;
	ok(HDVisWallDrawn(andrea1, 5, 4, tol),
		"wall: a side drawn keeps the side round the corner", 1, 1);
	ok(HDVisWallDrawn(andrea1, 5, 3, tol),
		"wall: and the side's other face in its plane", 1, 1);
	ok(!HDVisWallDrawn(andrea1, 5, 1, tol),
		"wall: but not the roof", 0, 0);
	ok(!HDVisWallDrawn(andrea1, 5, 5, tol),
		"wall: a face past the list is not whole", 0, 0);
	ok(!HDVisWallDrawn(0, 0, 0, tol), "wall: no faces, nothing whole", 0, 0);
}

/* The classic room the visibility path leaves out (HDVisClassicRoomHidden):
 * ANDY3's room 88 - an empty group, 58 classic faces none of which any art
 * covers, room 35's trees its guest - and the rooms it keeps. */
static void vis_hidden_room()
{
	ok(HDVisClassicRoomHidden(true, false, 58, 2),
		"hidden: ANDY3 88, empty, uncovered, two guests", 1, 1);
	ok(!HDVisClassicRoomHidden(false, false, 12, 1),
		"hidden: a hollow group (JOBY2 19) keeps its faces", 0, 0);
	ok(!HDVisClassicRoomHidden(true, true, 16, 2),
		"hidden: covered in part (RICH2 18) keeps its faces", 0, 0);
	ok(!HDVisClassicRoomHidden(true, false, 31, 0),
		"hidden: no guest (ANDY3 26) keeps its faces", 0, 0);
	ok(!HDVisClassicRoomHidden(true, false, 0, 3),
		"hidden: no classic face, nothing to leave out", 0, 0);
	ok(HDVisClassicRoomHidden(true, false, 15, 1),
		"hidden: ANDY3 122, one guest of 14 triangles", 1, 1);
	ok(HDVisClassicRoomHidden(true, false, 9, 2),
		"hidden: ANDY1 212, two guests", 1, 1);
}

/* The static mesh the visibility path leaves to the art that lies on it
 * (HDVisStaticDrawnByArt): ANDY3's dormer of room 39, room 35's art on
 * 0.603 to 0.791 of its statics' samples within 32 units, and the shares
 * the rule keeps classic. */
static void vis_static_rule()
{
	ok(HDVisStaticDrawnByArt(0.603f, 0.5, true),
		"static: ANDY3 39 mesh 12, 0.603, room 35 drawn", 1, 1);
	ok(HDVisStaticDrawnByArt(0.791f, 0.5, true),
		"static: ANDY3 39 mesh 2, 0.791, room 35 drawn", 1, 1);
	ok(!HDVisStaticDrawnByArt(0.791f, 0.5, false),
		"static: the holder not drawn keeps it classic", 0, 0);
	ok(!HDVisStaticDrawnByArt(0.475f, 0.5, true),
		"static: JOBY5 141 mesh 7, 0.475, stays classic", 0, 0);
	ok(!HDVisStaticDrawnByArt(0.474f, 0.5, true),
		"static: JOBY3 137 meshes 4 and 5, 0.474, stay classic", 0, 0);
	ok(HDVisStaticDrawnByArt(0.5f, 0.5, true),
		"static: a half of its samples is enough", 1, 1);
	ok(!HDVisStaticDrawnByArt(0.0f, 0.0, true),
		"static: no art on it is never the art's", 0, 0);
	ok(HDVisStaticDrawnByArt(1.0f, 0.5, true),
		"static: TITLE 23, every sample, is the art's", 1, 1);
}

/* The classic room the visibility path leaves out because it stands inside
 * another (HDVisClassicRoomEnclosed): JOBY2's sniper rooms 19 and 21,
 * hollow groups inside room 146's box. */
static void vis_enclosed_rule()
{
	ok(HDVisClassicRoomEnclosed(false, false, 12, 146),
		"enclosed: JOBY2 21, hollow, uncovered, inside 146", 1, 1);
	ok(!HDVisClassicRoomEnclosed(true, false, 12, 146),
		"enclosed: a group with art in its box keeps its faces", 0, 0);
	ok(!HDVisClassicRoomEnclosed(false, true, 46, 121),
		"enclosed: covered faces stay, though a room encloses it", 0, 0);
	ok(!HDVisClassicRoomEnclosed(false, false, 31, -1),
		"enclosed: inside no room (ANDY3 26) keeps its faces", 0, 0);
	ok(!HDVisClassicRoomEnclosed(false, false, 0, 146),
		"enclosed: no classic face, nothing to leave out", 0, 0);
	ok(HDVisClassicRoomEnclosed(false, false, 12, 0),
		"enclosed: group 0 encloses as any other", 1, 1);
}

/* The height a triangle of an unlinked room reaches into an intrusion box
 * (HDVisHeightReaches): JOBY2's room 30 on room 60's east wall, room 60's
 * box from its ceiling -3,328 to its floor -1,280. */
static void vis_height_rule()
{
	const float top = -3328.0f, bottom = -1280.0f, m = 32.0f;

	ok(HDVisHeightReaches(-3413.0f, -3242.0f, top, bottom, m),
		"height: room 30's triangle reaches 86 under the ceiling", 1, 1);
	ok(!HDVisHeightReaches(-3413.0f, -3300.0f, top, bottom, m),
		"height: 28 under the ceiling is within the margin", 0, 0);
	ok(!HDVisHeightReaches(-3413.0f, -3296.0f, top, bottom, m),
		"height: 32 under the ceiling is the margin itself", 0, 0);
	ok(HDVisHeightReaches(-1400.0f, -1100.0f, top, bottom, m),
		"height: a foot from under the floor reaching up", 1, 1);
	ok(!HDVisHeightReaches(-1300.0f, -1100.0f, top, bottom, m),
		"height: 20 over the floor is within the margin", 0, 0);
	ok(!HDVisHeightReaches(-4000.0f, -3500.0f, top, bottom, m),
		"height: wholly over the ceiling", 0, 0);
	ok(HDVisHeightReaches(-3413.0f, -3072.0f, top, bottom, m),
		"height: down to room 30's floor, 256 into the box", 1, 1);
}

/* A triangle in one side plane of an intrusion box (HDVisOnBoxSide): room
 * 60's box of JOBY2 in room 30's frame, x 6,144..7,168, z -1,024..3,072. */
static void vis_box_side()
{
	const float mins[3] = { 6144.0f, -3328.0f, -1024.0f };
	const float maxs[3] = { 7168.0f, -1280.0f, 3072.0f };
	const float onX[3] = { 7168.0f, 7168.0f, 7168.0f };
	const float zA[3] = { 2389.0f, 2389.0f, 2730.0f };

	ok(HDVisOnBoxSide(onX, zA, mins, maxs),
		"side: room 30's triangle on room 60's east wall", 1, 1);

	const float nearX[3] = { 7168.0f, 7509.0f, 7168.0f };
	const float zB[3] = { 2048.0f, 2048.0f, 2048.0f };

	ok(!HDVisOnBoxSide(nearX, zA, mins, maxs),
		"side: reaching 341 into the wall is not on it", 0, 0);
	ok(HDVisOnBoxSide(nearX, zB, mins, maxs) == false,
		"side: z 2,048 is inside, no side there", 0, 0);

	const float offX[3] = { 7169.0f, 7167.5f, 7168.9f };

	ok(HDVisOnBoxSide(offX, zA, mins, maxs),
		"side: within a unit of the plane is on it", 1, 1);

	const float farX[3] = { 7170.5f, 7168.0f, 7168.0f };

	ok(!HDVisOnBoxSide(farX, zA, mins, maxs),
		"side: a corner 2.5 off the plane is not", 0, 0);

	const float midX[3] = { 6500.0f, 6600.0f, 6700.0f };
	const float onZ[3] = { -1024.0f, -1024.0f, -1023.5f };

	ok(HDVisOnBoxSide(midX, onZ, mins, maxs),
		"side: the north side, z -1,024, counts as well", 1, 1);
	ok(!HDVisOnBoxSide(midX, zA, mins, maxs),
		"side: in the middle of the box is on no side", 0, 0);
}

/* A cube of art in the frame of the room at `ox`, `oz`: x0..x0 + s,
 * y0 - s..y0 (y down) and z0..z0 + s, each face two triangles. */
static void vis_cube(trf::remaster::Trg& trg, float ox, float oz, float x0,
	float y0, float z0, float s)
{
	const float c[8][3] =
	{
		{ x0, y0, z0 }, { x0 + s, y0, z0 }, { x0 + s, y0, z0 + s },
		{ x0, y0, z0 + s }, { x0, y0 - s, z0 }, { x0 + s, y0 - s, z0 },
		{ x0 + s, y0 - s, z0 + s }, { x0, y0 - s, z0 + s }
	};
	const int q[6][4] = { { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 },
		{ 1, 5, 6, 2 }, { 2, 6, 7, 3 }, { 3, 7, 4, 0 } };

	for (int f = 0; f < 6; f++)
	{
		vis_tri(trg, ox, 0, oz, c[q[f][0]], c[q[f][1]], c[q[f][2]]);
		vis_tri(trg, ox, 0, oz, c[q[f][0]], c[q[f][2]], c[q[f][3]]);
	}
}

/* The enclosure table on a level built by hand: room A (0), a box of 6 x 6
 * sectors, with room C (1), a box of 3 x 3 inside A's and linked with it by
 * a portal, whose own group holds nothing in its box; A's art stands in C's
 * inside. */
static void vis_enclosed_tables()
{
	for (int variant = 0; variant < 5; variant++)
	{
		trf::remaster::Trg trg;
		std::vector<HDVisRoom> rooms(2);
		std::vector<HDVisPortal> portals(2);
		// 0: as above; 1: no portal; 2: C reaches out of A's box; 3: A's
		// art in C's inside is four triangles; 4: eight, a guest's least.
		const long x[2] = { 0, variant == 2 ? 5120 : 1024 };

		for (int r = 0; r < 2; r++)
		{
			HDVisRoom& one = rooms[(size_t)r];

			memset(&one, 0, sizeof(one));
			one.x = x[r];
			one.z = r ? 1024 : 0;
			one.xs = one.zs = r ? 3 : 6;
			one.top = -4096;
			one.bottom = 0;
			one.twin = -1;
			one.firstPortal = r;
			one.portalCount = variant == 1 ? 0 : 1;
		}

		memset(&portals[0], 0, sizeof(HDVisPortal) * 2);
		portals[0].to = 1;
		portals[1].to = 0;
		trg.groups.resize(2);

		for (int g = 0; g < 2; g++)
		{
			trf::remaster::TrgPart part;

			part.pass[0].start = (uint32_t)trg.indices.size();

			// A's art in the middle of C's inside, one sector square.
			const int tris = variant == 3 ? 4 : variant == 4 ? 8 : 12;

			for (int t = 0; g == 0 && t < tris; t++)
			{
				const float cx = (float)x[1] + 1100.0f + t * 32.0f;
				const float a[3] = { cx, -1000.0f, 2100.0f };
				const float b[3] = { cx + 16.0f, -1000.0f, 2100.0f };
				const float c[3] = { cx, -1016.0f, 2116.0f };

				vis_tri(trg, 0, 0, 0, a, b, c);
			}

			part.pass[0].count = (uint32_t)trg.indices.size() -
				part.pass[0].start;
			trg.groups[(size_t)g].parts.push_back(part);
		}

		HDVisTables t;

		HDVisBuild(trg, &rooms[0], 2, 0, 0, &portals[0], 2, 0, 0,
			HDVisDefaults(), &t);

		const int want = variant == 0 || variant == 4 ? 0 : -1;
		static const char* what[5] =
		{
			"enclosed: C inside A, linked, A's art in it",
			"enclosed: no link, no enclosing room",
			"enclosed: C past A's box, none",
			"enclosed: four triangles are fewer than a guest's eight",
			"enclosed: eight triangles are enough"
		};

		ok(t.enclosedBy.size() == 2 && t.enclosedBy[1] == want, what[variant],
			t.enclosedBy.size() == 2 ? t.enclosedBy[1] : -9, want);

		if (variant == 0)
		{
			ok(t.enclosedBy.size() == 2 && t.enclosedBy[0] == -1,
				"enclosed: A, whose inside holds no other art, by none",
				t.enclosedBy.size() == 2 ? t.enclosedBy[0] : -9, -1);
			ok(t.roomsEnclosed == 1 && t.enclosedTris.size() == 2 &&
				t.enclosedTris[1] == 12,
				"enclosed: one room, by A's twelve triangles",
				t.roomsEnclosed, 1);
		}
	}
}

/* The static covers on a level built by hand: room A (0), 6 x 6 sectors,
 * with room C (1), 3 x 3, inside its box, and a static of C, a cube of 256
 * units, whose faces A's art lies on within 32 units on all six sides. */
static void vis_static_covers()
{
	// A at the origin, C inside it; a cube static of C at x 2048..2304.
	std::vector<HDVisRoom> rooms(2);

	for (int r = 0; r < 2; r++)
	{
		HDVisRoom& one = rooms[(size_t)r];

		memset(&one, 0, sizeof(one));
		one.x = r ? 1024 : 0;
		one.z = r ? 1024 : 0;
		one.xs = one.zs = r ? 3 : 6;
		one.top = -4096;
		one.bottom = 0;
		one.twin = -1;
	}

	// The static's six faces, world units, wound as the cube's art.
	std::vector<HDCoverFace> faces;
	const float s0[3] = { 2048.0f, -512.0f, 2048.0f };
	const double s = 256.0;
	const double cube[8][3] =
	{
		{ s0[0], s0[1], s0[2] }, { s0[0] + s, s0[1], s0[2] },
		{ s0[0] + s, s0[1], s0[2] + s }, { s0[0], s0[1], s0[2] + s },
		{ s0[0], s0[1] - s, s0[2] }, { s0[0] + s, s0[1] - s, s0[2] },
		{ s0[0] + s, s0[1] - s, s0[2] + s }, { s0[0], s0[1] - s, s0[2] + s }
	};
	const int q[6][4] = { { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 },
		{ 1, 5, 6, 2 }, { 2, 6, 7, 3 }, { 3, 7, 4, 0 } };

	for (int f = 0; f < 6; f++)
	{
		HDCoverFace face;

		face.corners = 4;
		face.doubleSided = 0;

		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 3; k++)
				face.v[c][k] = cube[q[f][c]][k];

		faces.push_back(face);
	}

	HDVisStatic one;

	one.room = 1;
	one.mesh = 0;
	one.firstFace = 0;
	one.faceCount = 6;

	// Room-local box: x and z from C's origin.
	one.mins[0] = s0[0] - 1024.0f;
	one.maxs[0] = s0[0] + (float)s - 1024.0f;
	one.mins[1] = s0[1] - (float)s;
	one.maxs[1] = s0[1];
	one.mins[2] = s0[2] - 1024.0f;
	one.maxs[2] = s0[2] + (float)s - 1024.0f;

	for (int variant = 0; variant < 6; variant++)
	{
		trf::remaster::Trg trg;

		trg.groups.resize(2);

		for (int g = 0; g < 2; g++)
		{
			trf::remaster::TrgPart part;

			part.pass[0].start = (uint32_t)trg.indices.size();

			/* 0: A's art on the cube's faces, 24 units out; 1: 40 units
			 * out, past the reach; 2: C's own art on the faces, which
			 * holds its box's cells and answers it without asking; 3: A's
			 * art on the cube with A's box moved away from it; 4: as 0 with
			 * the static in a room past the level's groups; 5: as 0 with no
			 * faces handed in. */
			const float off = variant == 1 ? 40.0f : 24.0f;
			const bool here = variant == 2 ? g == 1 : g == 0;

			if (here)
				vis_cube(trg, g ? 1024.0f : 0.0f, g ? 1024.0f : 0.0f,
					s0[0] - off, s0[1] + off, s0[2] - off,
					(float)s + 2.0f * off);

			part.pass[0].count = (uint32_t)trg.indices.size() -
				part.pass[0].start;
			trg.groups[(size_t)g].parts.push_back(part);
		}

		std::vector<HDVisRoom> asked = rooms;

		if (variant == 3)
			asked[0].x = 8192;

		HDVisTables t;

		HDVisStatic which = one;

		if (variant == 4)
			which.room = 7;

		HDVisStaticCovers(trg, &asked[0], 2, &which, 1, &faces[0],
			variant == 5 ? 0 : (int)faces.size(), HDVisDefaults(), &t);

		const float share = t.staticShare.empty() ? -1.0f : t.staticShare[0];
		const int group = t.staticGroup.empty() ? -9 : t.staticGroup[0];

		if (variant == 0)
		{
			ok(group == 0 && share > 0.99f,
				"covers: A's art 24 units out lies on every sample",
				(long)(share * 1000.0f), 1000);
			ok(t.staticsHeld == 1, "covers: and the static is held",
				t.staticsHeld, 1);
		}
		else if (variant == 1)
			ok(group == -1 && share == 0.0f,
				"covers: 40 units out is past the reach of 32",
				(long)(share * 1000.0f), 0);
		else if (variant == 2)
			ok(group == -1 && t.staticsHeld == 0,
				"covers: the room's own art holding it is not asked",
				group, -1);
		else if (variant == 3)
			ok(group == -1,
				"covers: art of a room whose box misses the static", group,
				-1);
		else if (variant == 4)
			ok(group == -1 && t.staticsHeld == 0,
				"covers: a static of no group's room is not asked", group, -1);
		else
			ok(t.staticGroup.size() == 1 && group == -1 && share == 0.0f,
				"covers: no faces, one answer of none",
				(long)t.staticGroup.size(), 1);
	}
}

static void stand_cover()
{
	ok(HDCoverRoomCovered(46, 46), "stand: 46 of 46 faces is covered",
		HDCoverRoomCovered(46, 46), 1);
	ok(!HDCoverRoomCovered(46, 45), "stand: 45 of 46 is not", HDCoverRoomCovered(46, 45), 0);
	ok(!HDCoverRoomCovered(0, 0), "stand: a room with no face is not",
		HDCoverRoomCovered(0, 0), 0);
	ok(!HDCoverRoomCovered(32, 16), "stand: RICH2 16 (16 of 32) is not",
		HDCoverRoomCovered(32, 16), 0);

	const HDCoverFace face = stand_floor(0.0, 0.0, 1024.0, 0.0);
	HDCoverTri t0, t1, far0, far1;

	// The two halves of the face, as art 40 units above it (y down: -40).
	stand_tri(0, 0, 0, 1024, 1024, 1024, -40.0, &t0);
	stand_tri(0, 0, 1024, 1024, 1024, 0, -40.0, &t1);
	stand_tri(0, 0, 0, 1024, 1024, 1024, -200.0, &far0);
	stand_tri(0, 0, 1024, 1024, 1024, 0, -200.0, &far1);

	HDCoverSet whole, half, far;

	whole.tris.push_back(t0);
	whole.tris.push_back(t1);
	half.tris.push_back(t0);
	far.tris.push_back(far0);
	far.tris.push_back(far1);
	whole.build();
	half.build();
	far.build();

	const HDCoverCount cw = HDCoverFaces(&face, 1, whole, HD_COVER_REACH, HD_COVER_STEP, 1);
	const HDCoverCount ch = HDCoverFaces(&face, 1, half, HD_COVER_REACH, HD_COVER_STEP, 1);
	const HDCoverCount cf = HDCoverFaces(&face, 1, far, HD_COVER_REACH, HD_COVER_STEP, 1);

	ok(cw.faces == 1 && cw.facesCovered == 1 && cw.samples == cw.samplesCovered,
		"stand: both halves cover the sector", (long)cw.samplesCovered, (long)cw.samples);
	// Each half's longest edge is the diagonal, 1448 units: 23 cuts of 64.
	ok(cw.samples == 2 * 23 * 23, "stand: a sector is 2 x 23 x 23 samples at step 64",
		(long)cw.samples, 2 * 23 * 23);
	ok(ch.facesCovered == 0 && ch.samplesCovered > 0 && ch.samplesCovered < ch.samples,
		"stand: one half covers part of it", (long)ch.samplesCovered, 256);
	ok(cf.samplesCovered == 0, "stand: art 200 units off covers nothing",
		(long)cf.samplesCovered, 0);
}

static void stand_overlap()
{
	const HDCoverFace face = stand_floor(0.0, 0.0, 1024.0, 0.0);
	HDCoverFace two = face;
	HDCoverTri t;

	two.doubleSided = 1;

	stand_tri(0, 0, 0, 1024, 1024, 1024, -40.0, &t);
	ok(HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: a copy of a half stands on it",
		HDCoverOverlaps(t, face, HD_COVER_REACH), 1);
	ok(HDCoverTriOnFaces(t, &face, 1, HD_COVER_REACH), "stand: and lies on it whole",
		HDCoverTriOnFaces(t, &face, 1, HD_COVER_REACH), 1);

	stand_tri(0, 0, 0, 1024, 1024, 1024, -120.0, &t);
	ok(HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: 120 units off is within the reach",
		HDCoverOverlaps(t, face, HD_COVER_REACH), 1);

	stand_tri(0, 0, 0, 1024, 1024, 1024, -140.0, &t);
	ok(!HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: 140 units off is past it",
		HDCoverOverlaps(t, face, HD_COVER_REACH), 0);

	// Turned away: the winding reversed.
	stand_tri(0, 0, 1024, 1024, 0, 1024, -40.0, &t);
	ok(!HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: art turned away does not",
		HDCoverOverlaps(t, face, HD_COVER_REACH), 0);
	ok(HDCoverOverlaps(t, two, HD_COVER_REACH), "stand: unless the face is double-sided",
		HDCoverOverlaps(t, two, HD_COVER_REACH), 1);

	// Next door, sharing the face's edge x = 1024 and nothing else.
	stand_tri(1024, 0, 1024, 1024, 2048, 1024, -40.0, &t);
	ok(!HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: touching an edge is not standing on it",
		HDCoverOverlaps(t, face, HD_COVER_REACH), 0);

	// Half on the face, half off it: stands on it, not whole.
	stand_tri(512, 0, 512, 1024, 1536, 1024, -40.0, &t);
	ok(HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: reaching half off still stands on it",
		HDCoverOverlaps(t, face, HD_COVER_REACH), 1);
	ok(!HDCoverTriOnFaces(t, &face, 1, HD_COVER_REACH), "stand: but does not lie on it whole",
		HDCoverTriOnFaces(t, &face, 1, HD_COVER_REACH), 0);

	// A wall triangle beside the floor (facing +x) is not on the floor.
	{
		const float v[3][3] = { { 512, 0, 0 }, { 512, -512, 0 }, { 512, 0, 512 } };
		const float origin[3] = { 0, 0, 0 };

		HDCoverMakeTri(v, origin, 117, &t);
		ok(!HDCoverOverlaps(t, face, HD_COVER_REACH), "stand: an upright trunk is not on the floor",
			HDCoverOverlaps(t, face, HD_COVER_REACH), 0);
	}

	// The tri maker: corner in the world, edges local, no area refused.
	{
		const float v[3][3] = { { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2, 3 } };
		const float origin[3] = { 100, 0, 200 };

		ok(!HDCoverMakeTri(v, origin, 0, &t), "stand: a triangle of no area is refused", 1, 0);
	}
}

/* stand_part: a classic room's faces one by one (hd_cover.h). The room's
 * state from its faces and the carrier's (HDCoverRoomState, at least half
 * for a room drawn in part), the carrier among the groups
 * (HDCoverCarrierBetter), a face covered (HDCoverFaceCovered), and
 * HDStandDrawFaces over every input: a room drawn classic or wholly as art
 * answers exactly as HDStandDraw; a room drawn in part, while it is listed,
 * keeps its classic roomlets, gets every covered face from exactly one
 * source - the carrier's range with its group or in the room's window, never
 * both and never none - leaves out art that lies whole on kept faces alone
 * and every other group's copy, never leaves out a carrier triangle that
 * stands on a covered face, and pushes back exactly what reaches onto a kept
 * face; off the list it draws nothing of its own. And RICH2's rooms 16, 18
 * and 19 with group 14 and TITLE's rooms with group 71, measured with
 * TrgProbe -classicart -perface.
 *
 * stand_shadow: a face she can stand on (HDCoverFaceFloor) - a floor, a
 * ceiling, a wall, a double-sided ceiling, two slopes, a face of no area -
 * and whether a room counts as HD for her shadow (HDCoverRoomShadowHD) with
 * the gate's answer for ANDREA2 124, RICH2 15, 16, 18 and 19, ANDY3 26 (drawn
 * classic, no face covered), ANDREA3 115 (no classic face) and an HD room. */
static void stand_part()
{
	ok(HDCoverRoomState(32, 16) == HD_STAND_ROOM_PART, "part: RICH2 16, 16 of 32, in part",
		HDCoverRoomState(32, 16), HD_STAND_ROOM_PART);
	ok(HDCoverRoomState(16, 15) == HD_STAND_ROOM_PART, "part: RICH2 18, 15 of 16, in part",
		HDCoverRoomState(16, 15), HD_STAND_ROOM_PART);
	ok(HDCoverRoomState(15, 13) == HD_STAND_ROOM_PART, "part: RICH2 19, 13 of 15, in part",
		HDCoverRoomState(15, 13), HD_STAND_ROOM_PART);
	ok(HDCoverRoomState(46, 46) == HD_STAND_ROOM_COVERED, "part: ANDREA2 124, 46 of 46, covered",
		HDCoverRoomState(46, 46), HD_STAND_ROOM_COVERED);
	ok(HDCoverRoomState(32, 15) == HD_STAND_ROOM_CLASSIC, "part: 15 of 32 is under half, classic",
		HDCoverRoomState(32, 15), HD_STAND_ROOM_CLASSIC);
	ok(HDCoverRoomState(65, 10) == HD_STAND_ROOM_CLASSIC, "part: TITLE 70, 10 of 65, classic",
		HDCoverRoomState(65, 10), HD_STAND_ROOM_CLASSIC);
	ok(HDCoverRoomState(213, 7) == HD_STAND_ROOM_CLASSIC, "part: TITLE 103, 7 of 213, classic",
		HDCoverRoomState(213, 7), HD_STAND_ROOM_CLASSIC);
	ok(HDCoverRoomState(46, 0) == HD_STAND_ROOM_CLASSIC, "part: no face covered, classic",
		HDCoverRoomState(46, 0), HD_STAND_ROOM_CLASSIC);
	ok(HDCoverRoomState(0, 0) == HD_STAND_ROOM_CLASSIC, "part: no face at all, classic",
		HDCoverRoomState(0, 0), HD_STAND_ROOM_CLASSIC);

	ok(HDCoverFaceCovered(1.0), "part: every sample covered is a covered face", 1, 1);
	ok(!HDCoverFaceCovered(0.999), "part: one bare sample keeps the face", 0, 0);

	// ANDREA2 124: 121 covers 46 faces with 3322 triangles, 127 36 with 1732.
	int best = -1, bestFaces = 0, bestTris = 0;
	const int cand[3][3] = { { 117, 0, 16 }, { 121, 46, 3322 }, { 127, 36, 1732 } };

	for (int i = 0; i < 3; i++)
		if (HDCoverCarrierBetter(cand[i][1], cand[i][2], cand[i][0], bestFaces, bestTris, best))
		{
			best = cand[i][0];
			bestFaces = cand[i][1];
			bestTris = cand[i][2];
		}

	ok(best == 121, "part: ANDREA2 124's carrier is 121", best, 121);
	ok(HDCoverCarrierBetter(10, 5, 9, 9, 500, 3), "part: more faces beat more triangles", 1, 1);
	ok(HDCoverCarrierBetter(10, 6, 9, 10, 5, 3), "part: equal faces, more triangles win", 1, 1);
	ok(HDCoverCarrierBetter(10, 5, 3, 10, 5, 9), "part: all equal, the lower group wins", 1, 1);
	ok(!HDCoverCarrierBetter(10, 5, 9, 10, 5, 3), "part: all equal, the higher group loses", 0, 0);
	ok(!HDCoverCarrierBetter(0, 900, 1, -1, 0, -1), "part: a group that covers no face never carries",
		0, 0);

	int same_wrong = 0, both = 0, sources_bad = 0, copy_kept = 0, carrier_lost = 0;
	int behind_wrong = 0, unlisted_wrong = 0, classic_wrong = 0, window_wrong = 0;

	for (int bits = 0; bits < 16; bits++)
		for (int state = 0; state < 3; state++)
			for (int kind = 0; kind < 3; kind++)
				for (int faces = 0; faces < 4; faces++)
				{
					const bool holder = (bits & 1) != 0, art = (bits & 2) != 0;
					const bool carrier = (bits & 4) != 0;

					if ((bits & 8) || (kind == 2 && faces))
						continue;

					const HDStandAnswer a = HDStandDrawFaces(holder, art, state, carrier, kind,
						faces);

					if (state != HD_STAND_ROOM_PART)
					{
						const HDStandAnswer b = HDStandDraw(holder, art,
							state == HD_STAND_ROOM_COVERED, carrier, kind == 1, kind == 0);

						if (a.classic != b.classic || a.withArt != b.withArt ||
							a.inHolder != b.inHolder || a.behind != b.behind)
							same_wrong++;

						continue;
					}

					// A range of a room drawn in part.
					const bool onCovered = (faces & HD_STAND_FACES_COVERED) != 0;
					const bool onKept = (faces & HD_STAND_FACES_KEPT) != 0;
					const bool drawn = a.withArt || a.inHolder;

					if (a.withArt && a.inHolder)
						both++;

					if (a.classic != holder)
						classic_wrong++;

					if (a.inHolder && (!holder || art))
						window_wrong++;

					if (!holder && (a.classic || a.inHolder || a.behind || a.withArt != art))
						unlisted_wrong++;

					if (!holder)
						continue;

					// The carrier's art on a covered face, or its relief: one source.
					if (carrier && (kind == 2 || onCovered) && (drawn ? 1 : 0) != 1)
						sources_bad++;

					if (carrier && onCovered && !drawn)
						carrier_lost++;

					// Lying whole on the faces and not the carrier's on a covered one.
					if (kind == 1 && !(carrier && onCovered) && drawn)
						copy_kept++;

					const bool want_behind = drawn && kind != 2 &&
						(onKept || !(carrier && onCovered));

					if (a.behind != want_behind)
						behind_wrong++;
				}

	ok(same_wrong == 0, "part: classic and covered rooms answer as HDStandDraw", same_wrong, 0);
	ok(both == 0, "part: never with the group and in the window at once", both, 0);
	ok(classic_wrong == 0, "part: the kept faces drawn exactly while listed", classic_wrong, 0);
	ok(window_wrong == 0, "part: the window only for a listed room, art off the list",
		window_wrong, 0);
	ok(unlisted_wrong == 0, "part: a room off the list draws nothing of its own",
		unlisted_wrong, 0);
	ok(sources_bad == 0, "part: a covered face has exactly one source while listed",
		sources_bad, 0);
	ok(carrier_lost == 0, "part: a carrier triangle on a covered face is never left out",
		carrier_lost, 0);
	ok(copy_kept == 0, "part: art whole on kept faces and every other copy left out",
		copy_kept, 0);
	ok(behind_wrong == 0, "part: pushed back exactly what reaches onto a kept face",
		behind_wrong, 0);

	// RICH2 16 drawn in part, carrier 14, both listed and 14 off the list.
	const int P = HD_STAND_ROOM_PART;
	HDStandAnswer a = HDStandDrawFaces(true, true, P, true, 1, HD_STAND_FACES_COVERED);
	ok(a.classic && a.withArt && !a.behind, "part: RICH2 16, 14's walls with 14, in place",
		a.withArt, 1);
	a = HDStandDrawFaces(true, false, P, true, 1, HD_STAND_FACES_COVERED);
	ok(a.inHolder && !a.withArt && !a.behind, "part: RICH2 16, 14 off - its walls in 16's window",
		a.inHolder, 1);
	a = HDStandDrawFaces(true, true, P, true, 1, HD_STAND_FACES_BOTH);
	ok(a.withArt && a.behind, "part: RICH2 16, 14's art on a covered and a kept face pushed back",
		a.behind, 1);
	a = HDStandDrawFaces(true, false, P, true, 1, HD_STAND_FACES_BOTH);
	ok(a.inHolder && a.behind, "part: the same in 16's window, pushed back", a.behind, 1);
	a = HDStandDrawFaces(true, true, P, true, 1, HD_STAND_FACES_KEPT);
	ok(!a.withArt && !a.inHolder, "part: RICH2 19, 14's art whole on its kept wall left out",
		a.withArt, 0);
	a = HDStandDrawFaces(true, true, P, true, 0, HD_STAND_FACES_KEPT);
	ok(a.withArt && a.behind, "part: 14's art reaching onto a kept face, pushed back", a.behind, 1);
	a = HDStandDrawFaces(true, false, P, true, 0, HD_STAND_FACES_KEPT);
	ok(!a.withArt && !a.inHolder, "part: the same with 14 off - nothing", a.withArt, 0);
	a = HDStandDrawFaces(true, false, P, true, 2, 0);
	ok(a.inHolder && !a.behind, "part: 14's relief in 16's box, 14 off - in the window",
		a.inHolder, 1);
	a = HDStandDrawFaces(false, true, P, true, 1, HD_STAND_FACES_KEPT);
	ok(a.withArt && !a.behind && !a.classic, "part: 16 off the list - 14's art in place",
		a.withArt, 1);
	// TITLE 103: 7 of 213 faces is a room drawn classic, 71's copy left out.
	a = HDStandDrawFaces(true, true, HDCoverRoomState(213, 7), true, 1, 0);
	ok(a.classic && !a.withArt, "part: TITLE 103 classic, 71's art on its faces left out",
		a.withArt, 0);
}

static void stand_shadow()
{
	const HDCoverFace floor = stand_floor(0.0, 0.0, 1024.0, 0.0);
	HDCoverFace ceiling = floor, wall = floor, twoSided = floor, slope45 = floor;
	HDCoverFace slope70 = floor, none = floor;

	// The ceiling: the floor's corners the other way round.
	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++)
			ceiling.v[c][k] = floor.v[3 - c][k];

	twoSided = ceiling;
	twoSided.doubleSided = 1;

	// A wall in the x-y plane, and two slopes rising along z.
	const double w[4][3] = { { 0, 0, 0 }, { 0, -1024, 0 }, { 1024, -1024, 0 }, { 1024, 0, 0 } };
	const double s45[4][3] = { { 0, 0, 0 }, { 0, -1024, 1024 }, { 1024, -1024, 1024 },
		{ 1024, 0, 0 } };
	const double s70[4][3] = { { 0, 0, 0 }, { 0, -2814, 1024 }, { 1024, -2814, 1024 },
		{ 1024, 0, 0 } };

	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++)
		{
			wall.v[c][k] = w[c][k];
			slope45.v[c][k] = s45[c][k];
			slope70.v[c][k] = s70[c][k];
			none.v[c][k] = 5.0;
		}

	ok(HDCoverFaceUp(floor) > 0.999, "shadow: a floor points up", (long)(HDCoverFaceUp(floor) * 1000),
		1000);
	ok(HDCoverFaceFloor(floor), "shadow: a floor is a face she can stand on", 1, 1);
	ok(HDCoverFaceUp(ceiling) < -0.999 && !HDCoverFaceFloor(ceiling),
		"shadow: a ceiling points down and is none", (long)(HDCoverFaceUp(ceiling) * 1000), -1000);
	ok(!HDCoverFaceFloor(wall), "shadow: a wall is none", (long)(HDCoverFaceUp(wall) * 1000), 0);
	ok(HDCoverFaceFloor(twoSided), "shadow: a double-sided ceiling is a floor from above", 1, 1);
	ok(HDCoverFaceFloor(slope45), "shadow: a 45 degree slope is one",
		(long)(HDCoverFaceUp(slope45) * 1000), 707);
	ok(!HDCoverFaceFloor(slope70), "shadow: a 70 degree slope is none",
		(long)(HDCoverFaceUp(slope70) * 1000), 342);
	ok(!HDCoverFaceFloor(none) && HDCoverFaceUp(none) == 0.0, "shadow: a face of no area is none",
		0, 0);

	/* The rooms of the game, from TrgProbe -classicart -perface: the state
	 * and the kept floors; the gate at HEAD and as it stood before, when
	 * only a room drawn HD counted. */
	struct Room
	{
		const char* name;
		bool hd;
		int faces, covered, floorsKept;
		bool want;
	};
	const Room rooms[] =
	{
		{ "ANDREA2 124", false, 46, 46, 0, true },
		{ "RICH2 15", false, 15, 15, 0, true },
		{ "RICH2 16", false, 32, 16, 0, true },
		{ "RICH2 18", false, 16, 15, 1, false },
		{ "RICH2 19", false, 15, 13, 1, false },
		{ "ANDY3 26", false, 31, 0, 0, false },
		{ "ANDREA3 115", false, 0, 0, 0, true },
		{ "an HD room", true, 0, 0, 0, true },
	};

	for (const Room& r : rooms)
	{
		const bool hd = HDCoverRoomShadowHD(r.hd, HDCoverRoomState(r.faces, r.covered),
			r.floorsKept, r.faces);
		const int gate = HDShadowGate(HD_SHADOW_MODE_PROJECTED, true, true, hd, true);
		const int before = HDShadowGate(HD_SHADOW_MODE_PROJECTED, true, true, r.hd, true);
		char what[96];

		snprintf(what, sizeof(what), "shadow: %s %s (was %s)", r.name, HDShadowGateWord(gate),
			HDShadowGateWord(before));
		ok(hd == r.want, what, hd ? 1 : 0, r.want ? 1 : 0);
	}

	/* A room with no classic face and a sector she can stand on with no art
	 * in its box (TrgProbe -perface noface lines, bare=1: ANDREA3 71, RICH3
	 * 205) keeps the blob; the flag means nothing for a room with faces or
	 * one drawn HD. */
	ok(!HDCoverRoomShadowHD(false, HD_STAND_ROOM_CLASSIC, 0, 0, true),
		"shadow: a faceless room with a bare floor (ANDREA3 71) keeps the blob", 0, 0);
	ok(HDCoverRoomShadowHD(false, HD_STAND_ROOM_CLASSIC, 0, 0, false),
		"shadow: a faceless room with art in its box (ANDREA3 115) counts as HD", 1, 1);
	ok(HDCoverRoomShadowHD(true, HD_STAND_ROOM_CLASSIC, 0, 0, true),
		"shadow: a room drawn HD counts whatever its floor data", 1, 1);
	ok(HDCoverRoomShadowHD(false, HDCoverRoomState(46, 46), 0, 46, true) &&
		!HDCoverRoomShadowHD(false, HD_STAND_ROOM_CLASSIC, 0, 31, false),
		"shadow: the flag is not asked of a room with classic faces", 1, 1);
}

/* static_piece: the one source a frame takes a static from (hd_cover.h,
 * HDStaticDrawSource). Per answer and per list state - a holder listed or
 * not, the static's piece drawn or not - exactly one of the classic static,
 * the art and the piece: a static held by groups is the art while a holder
 * is listed, its piece while none is and the piece is drawn, and the classic
 * static otherwise; the piece is never drawn beside the art. With no piece the
 * answer is HDStaticDrawClassic's. And the three distances the piece is made
 * with, against what they were measured on (RoomProbe's build of ANDREA2). */
static void static_piece()
{
	for (int answer = HD_STATIC_GROUPS; answer <= HD_STATIC_CLASSIC; answer++)
		for (int listed = 0; listed < 2; listed++)
			for (int piece = 0; piece < 2; piece++)
			{
				const int source = HDStaticDrawSource(answer, listed != 0, piece != 0);
				const int want = answer == HD_STATIC_ART ? HD_STATIC_FROM_ART :
					answer != HD_STATIC_GROUPS ? HD_STATIC_FROM_CLASSIC :
					listed ? HD_STATIC_FROM_ART :
					piece ? HD_STATIC_FROM_PIECE : HD_STATIC_FROM_CLASSIC;
				char what[128];

				snprintf(what, sizeof(what), "piece: answer %d, a holder listed %d, its "
					"piece drawn %d - one source", answer, listed, piece);
				ok(source == want, what, source, want);
			}

	for (int answer = HD_STATIC_GROUPS; answer <= HD_STATIC_CLASSIC; answer++)
		for (int listed = 0; listed < 2; listed++)
		{
			char what[128];

			snprintf(what, sizeof(what), "piece: answer %d, a holder listed %d, no piece - "
				"as HDStaticDrawClassic", answer, listed);
			ok((HDStaticDrawSource(answer, listed != 0, false) == HD_STATIC_FROM_CLASSIC) ==
				HDStaticDrawClassic(answer, listed != 0), what, 1, 1);
		}

	// A palm trunk of ANDREA2 room 124: 124 listed, 117 and 127 not - its
	// piece; with 127 listed - the art, the piece not again.
	ok(HDStaticDrawSource(HD_STATIC_GROUPS, false, true) == HD_STATIC_FROM_PIECE,
		"piece: ANDREA2 124 trunk, no holder listed - its piece", 1, 1);
	ok(HDStaticDrawSource(HD_STATIC_GROUPS, true, true) == HD_STATIC_FROM_ART,
		"piece: ANDREA2 124 trunk, group 127 listed - the art alone", 1, 1);
	ok(HDStaticDrawSource(HD_STATIC_GROUPS, false, false) == HD_STATIC_FROM_CLASSIC,
		"piece: ANDY1 225's hut, no piece - the classic fallback", 1, 1);

	ok(HD_STATIC_PIECE_REACH > HD_STATIC_PIECE_OPEN && HD_STATIC_PIECE_OPEN > 530.0f,
		"piece: the reach past the open limit, the open limit past the leaf cards' 530",
		(int)HD_STATIC_PIECE_OPEN, (int)HD_STATIC_PIECE_OPEN);
	ok(HD_STATIC_PIECE_SLACK >= 32.0f && HD_STATIC_PIECE_SLACK < HD_STATIC_PIECE_OPEN,
		"piece: the slack covers the trunks' 32 units", (int)HD_STATIC_PIECE_SLACK,
		(int)HD_STATIC_PIECE_SLACK);
}

/* static_art: which art draws a static mesh's place (hd_cover.h,
 * HDStaticAnswer and the two stand range questions). The room's own art is
 * asked at the box as before; another source at the static's own vertices,
 * nine twentieths of them; the classic static is drawn while the room is
 * listed only where no source the frame draws holds it. Every stand range
 * kind and state: a range HDStaticStandSure keeps is drawn in every frame
 * that lists its holder, one HDStaticStandWithGroup keeps in every frame
 * that lists its group - checked against HDStandDrawFaces in all four list
 * states, never the other way round. The rows of the game, from RoomProbe
 * -statictable. */
static void static_art()
{
	unsigned char holds[HD_STATIC_MAX_GROUPS];
	const int none[1] = { 0 };

	// The own group, at the box: a twentieth, as the rule before measured it.
	ok(HDStaticAnswer(1, 20, 0, none, 0, 0, holds) == HD_STATIC_ART,
		"static: own art 1 of 20 cells holds it", 1, 1);
	ok(HDStaticAnswer(1, 21, 0, none, 0, 0, holds) == HD_STATIC_CLASSIC,
		"static: own art 1 of 21 cells does not", 1, 1);
	ok(HDStaticAnswer(0, 768, 0, none, 0, 50, holds) == HD_STATIC_CLASSIC,
		"static: nothing anywhere stays classic", 1, 1);

	// Another source, at the vertices: nine twentieths.
	ok(HDStaticOtherHolds(9, 20) && !HDStaticOtherHolds(8, 20),
		"static: another source needs nine twentieths of the vertices", 1, 1);
	ok(!HDStaticOtherHolds(0, 0), "static: a mesh of no vertices is held by nothing", 1, 1);

	// The carrier's sure art holds it for good, another group per frame.
	ok(HDStaticAnswer(0, 75, 37, none, 0, 39, holds) == HD_STATIC_ART,
		"static: RICH2 15 mesh 0, the carrier's art 37 of 39 vertices - art", 1, 1);
	ok(HDStaticAnswer(0, 75, 12, none, 0, 39, holds) == HD_STATIC_CLASSIC,
		"static: RICH2 18 mesh 0, 12 of 39 - classic", 1, 1);

	{
		const int found[3] = { 9, 6, 3 };
		const int answer = HDStaticAnswer(0, 36, 3, found, 3, 12, holds);

		ok(answer == HD_STATIC_GROUPS && holds[0] && holds[1] && !holds[2],
			"static: ANDREA2 124 trunk - groups 127 and 117 hold it, the carrier's "
			"floor (3 of 12) does not", holds[0] + holds[1] + holds[2], 2);
	}
	{
		const int found[1] = { 27 };

		ok(HDStaticAnswer(0, 768, 0, found, 1, 50, holds) == HD_STATIC_GROUPS && holds[0],
			"static: ANDREA2 119 crown - group 117, 27 of 50", 1, 1);
	}
	{
		const int found[1] = { 6 };

		ok(HDStaticAnswer(0, 10, 0, found, 1, 15, holds) == HD_STATIC_CLASSIC && !holds[0],
			"static: 6 of 15 (0.40, the highest share under the gap) stays classic", 1, 1);
	}

	// A flat static: a box with no extent along one axis.
	{
		const float pane[2][3] = { { 1281.0f, -24712.0f, 5120.0f }, { 1281.0f, -24043.0f, 6144.0f } };
		const float crown[2][3] = { { 1568.0f, -4735.0f, 6874.0f }, { 3552.0f, -2048.0f, 8486.0f } };
		const float floorSquare[2][3] = { { 0.0f, -256.0f, 0.0f }, { 1024.0f, -256.0f, 1024.0f } };

		ok(HDStaticFlat(pane[0], pane[1]), "static: RICH2 6's pane is flat", 1, 1);
		ok(HDStaticFlat(floorSquare[0], floorSquare[1]), "static: a square on a floor is flat",
			1, 1);
		ok(!HDStaticFlat(crown[0], crown[1]), "static: a palm crown is not", 1, 1);
	}

	/* The shatterable ones: a piece pairs with the static whose box it
	 * overlaps, in the piece's group frame. A box of room A at x 1000..1200
	 * is at 4072..4272 in a group whose room starts 3072 units further west. */
	{
		const float mins[3] = { 1000.0f, -512.0f, 2000.0f };
		const float maxs[3] = { 1200.0f, 0.0f, 2200.0f };
		const float own[2][3] = { { 1100.0f, -400.0f, 2100.0f }, { 1300.0f, -100.0f, 2300.0f } };
		const float west[2][3] = { { 4100.0f, -400.0f, 1100.0f }, { 4300.0f, -100.0f, 1300.0f } };
		const float beside[2][3] = { { 1201.0f, -400.0f, 2100.0f }, { 1400.0f, -100.0f, 2300.0f } };

		ok(HDStaticPiecePaired(mins, maxs, 0.0f, 0.0f, own[0], own[1]),
			"static: a piece of its own group in its box pairs", 1, 1);
		ok(HDStaticPiecePaired(mins, maxs, 3072.0f, -1024.0f, west[0], west[1]),
			"static: a piece of another group pairs in that group's frame", 1, 1);
		ok(!HDStaticPiecePaired(mins, maxs, 0.0f, 0.0f, west[0], west[1]),
			"static: the same piece read in the static's own frame does not", 1, 1);
		ok(!HDStaticPiecePaired(mins, maxs, 0.0f, 0.0f, beside[0], beside[1]),
			"static: a piece one unit beside the box does not", 1, 1);
	}

	/* The smash group: the pairs RoomProbe -smashcensus measured, room
	 * local, each case a level's own boxes. A pane's glass a unit behind its
	 * plane pairs; a row's neighbour touching or overlapping by two units at
	 * the shared edge does not; the smallest true overlap, 38, does. */
	{
		const float a1Pane[2][3] =
			{ { 1110.0f, -2729.0f, 968.0f }, { 1969.0f, -1792.0f, 968.0f } };
		const float a1Glass[2][3] =
			{ { 1109.0f, -2769.0f, 966.0f }, { 1963.0f, -1833.0f, 967.0f } };
		const float a1Frame[2][3] =
			{ { 1110.0f, -2770.0f, 952.0f }, { 1963.0f, -1833.0f, 980.0f } };
		const float j2Pane0[2][3] =
			{ { 1024.0f, -4096.0f, 1536.0f }, { 2048.0f, -3072.0f, 1536.0f } };
		const float j2Piece0[2][3] =
			{ { 1024.0f, -4096.0f, 1523.0f }, { 2048.0f, -3071.0f, 1548.0f } };
		const float j2Piece1[2][3] =
			{ { 2048.0f, -4096.0f, 1523.0f }, { 3072.0f, -3071.0f, 1548.0f } };
		const float r3Box0[2][3] =
			{ { 1025.0f, -2055.0f, 3069.0f }, { 2049.0f, -3.0f, 4093.0f } };
		const float r3Piece13[2][3] =
			{ { 1023.0f, -2055.0f, 3068.0f }, { 2048.0f, 0.0f, 4095.0f } };
		const float r3Piece14[2][3] =
			{ { 2047.0f, -2055.0f, 3068.0f }, { 3072.0f, 0.0f, 4095.0f } };
		const float r3Lamp[2][3] =
			{ { 4425.0f, -681.0f, 2982.0f }, { 4785.0f, -512.0f, 3249.0f } };
		const float r3Shade[2][3] =
			{ { 4719.0f, -614.0f, 3085.0f }, { 4776.0f, -576.0f, 3142.0f } };

		ok(HDStaticPiecePaired(a1Pane[0], a1Pane[1], 0.0f, 0.0f,
			a1Glass[0], a1Glass[1]),
			"smash: ANDREA1 198's glass a unit behind the pane pairs", 1, 1);
		ok(HDStaticPiecePaired(a1Pane[0], a1Pane[1], 0.0f, 0.0f,
			a1Frame[0], a1Frame[1]),
			"smash: ANDREA1 198's frame around the pane pairs", 1, 1);
		ok(HDStaticPiecePaired(j2Pane0[0], j2Pane0[1], 0.0f, 0.0f,
			j2Piece0[0], j2Piece0[1]),
			"smash: JOBY2 57's first pane takes its own piece", 1, 1);
		ok(!HDStaticPiecePaired(j2Pane0[0], j2Pane0[1], 0.0f, 0.0f,
			j2Piece1[0], j2Piece1[1]),
			"smash: JOBY2 57's first pane leaves the piece it touches", 1, 1);
		ok(HDStaticPiecePaired(r3Box0[0], r3Box0[1], 0.0f, 0.0f,
			r3Piece13[0], r3Piece13[1]),
			"smash: RICH3 120's first static takes its own piece", 1, 1);
		ok(!HDStaticPiecePaired(r3Box0[0], r3Box0[1], 0.0f, 0.0f,
			r3Piece14[0], r3Piece14[1]),
			"smash: RICH3 120's first static leaves the piece two units in",
			1, 1);
		ok(HDStaticPiecePaired(r3Lamp[0], r3Lamp[1], 0.0f, 0.0f,
			r3Shade[0], r3Shade[1]),
			"smash: RICH3 46's 38-unit overlap pairs", 1, 1);
	}

	/* The window group: the ten shatter slots the savegame keeps, a flipmap
	 * twin's art left to its own state, and a static read where its list
	 * stands after a flip. ANDREA2's window onto the hall is slot 59 in rooms
	 * 132 (the main) and 65 (its alternate, flip 8), each with its own grid
	 * at the same place, piece 4 in group 132 and piece 3 in group 65; the
	 * controller clears the static room 132 holds once the flip has run. */
	{
		ok(!HDStaticShatterSlot(49), "window: slot 49 is not a shatter slot",
			HDStaticShatterSlot(49), 0);
		ok(HDStaticShatterSlot(50), "window: slot 50 is the first", 0, 1);
		ok(HDStaticShatterSlot(57), "window: slot 57, the last a shot takes",
			0, 1);
		ok(HDStaticShatterSlot(59), "window: slot 59, ANDREA2's window", 0, 1);
		ok(!HDStaticShatterSlot(60), "window: slot 60 is past the ten", 1, 0);
		ok(HDStaticPieceInState(-1, 31),
			"window: a room with no twin takes any group's piece", 0, 1);
		ok(!HDStaticPieceInState(65, 65),
			"window: room 132's static leaves its twin's grid", 1, 0);
		ok(HDStaticPieceInState(65, 132),
			"window: room 132's static takes its own grid", 0, 1);
		ok(HDStaticPieceInState(65, 31),
			"window: and a neighbour's art beside it", 0, 1);

		short table[250];

		for (int r = 0; r < 250; r++)
			table[r] = (short)r;

		ok(HDStaticListRoom(table, 250, 65) == 65,
			"window: before a flip a list stands in its own room",
			HDStaticListRoom(table, 250, 65), 65);

		// FlipMap(8) and HDFlipRoom(132, 65).
		table[132] = 65;
		table[65] = 132;

		ok(HDStaticListRoom(table, 250, 65) == 132,
			"window: after the flip room 65's list stands at 132",
			HDStaticListRoom(table, 250, 65), 132);
		ok(HDStaticListRoom(table, 250, 132) == 65,
			"window: and room 132's at 65", HDStaticListRoom(table, 250, 132),
			65);
		ok(HDStaticListRoom(table, 250, 31) == 31,
			"window: a room outside the pair stays",
			HDStaticListRoom(table, 250, 31), 31);
		ok(HDStaticListRoom(table, 250, -1) == -1 &&
			HDStaticListRoom(table, 250, 300) == -1,
			"window: no room holds a list out of range", 0, -1);

		/* The whole walk, as BuildStaticTables pairs and UpdateDestructibles
		 * reads: Flags bit 0 per list (by the room it was built in), the
		 * pieces' hidden state per pair, last pair written wins. */
		const int statics[2] = { 65, 132 };
		const int twins[2] = { 132, 65 };
		const int pieces[2] = { 3, 4 };
		const int groupOf[2] = { 65, 132 };
		int flags[250];
		int pairs = 0;
		bool hidden[2] = { false, false };

		for (int r = 0; r < 250; r++)
			flags[r] = 1;

		// The shot after the flip: room 132's static, the list built at 65.
		flags[table[132]] = 0;

		for (int s = 0; s < 2; s++)
			for (int p = 0; p < 2; p++)
			{
				if (!HDStaticPieceInState(twins[s], groupOf[p]))
					continue;

				pairs++;
				hidden[p] = flags[table[HDStaticListRoom(table, 250,
					statics[s])]] == 0;
			}

		ok(pairs == 2, "window: one pair per state", pairs, 2);
		ok(hidden[0], "window: the broken state's grid goes with the break",
			hidden[0], 1);
		ok(!hidden[1], "window: the whole state's grid stays", hidden[1], 0);

		/* Read by the room's number instead, piece 3's pair would read the
		 * list standing at room 65 now - the whole state's - and keep it. */
		const bool byNumber = flags[table[65]] == 0;

		ok(!byNumber, "window: read by number the broken grid would stay",
			byNumber, 0);
	}

	// The frame: never both the static and its art, never neither.
	for (int answer = HD_STATIC_GROUPS; answer <= HD_STATIC_CLASSIC; answer++)
		for (int listed = 0; listed < 2; listed++)
		{
			const bool classic = HDStaticDrawClassic(answer, listed != 0);
			const bool art = answer == HD_STATIC_ART ||
				(answer == HD_STATIC_GROUPS && listed);
			char what[96];

			snprintf(what, sizeof(what), "static: answer %d, a holder listed %d - "
				"exactly one of static and art", answer, listed);
			ok(classic != art, what, classic ? 1 : 0, art ? 0 : 1);
		}

	// The two stand range questions, against the frame's own answer.
	int sure = 0, withGroup = 0;

	for (int state = HD_STAND_ROOM_CLASSIC; state <= HD_STAND_ROOM_COVERED; state++)
		for (int carrier = 0; carrier < 2; carrier++)
			for (int kind = 0; kind < 3; kind++)
				for (int faces = 0; faces < 4; faces++)
				{
					bool allHolder = true, allGroup = true;

					for (int artListed = 0; artListed < 2; artListed++)
					{
						const HDStandAnswer a = HDStandDrawFaces(true, artListed != 0,
							state, carrier != 0, kind, faces);

						allHolder = allHolder && (a.withArt || a.inHolder);
					}

					for (int holderListed = 0; holderListed < 2; holderListed++)
					{
						const HDStandAnswer a = HDStandDrawFaces(holderListed != 0, true,
							state, carrier != 0, kind, faces);

						allGroup = allGroup && (a.withArt || a.inHolder);
					}

					char what[128];

					snprintf(what, sizeof(what), "static: state %d carrier %d kind %d "
						"faces %d - sure %d, with its group %d", state, carrier, kind,
						faces, allHolder ? 1 : 0, allGroup ? 1 : 0);
					ok(HDStaticStandSure(state, carrier != 0, kind, faces) == allHolder &&
						HDStaticStandWithGroup(state, carrier != 0, kind, faces) == allGroup,
						what, 1, 1);

					sure += HDStaticStandSure(state, carrier != 0, kind, faces) ? 1 : 0;
					withGroup += HDStaticStandWithGroup(state, carrier != 0, kind, faces) ? 1 : 0;
				}

	// A covered room's carrier ranges are all sure; nothing of a room drawn
	// classic is.
	ok(HDStaticStandSure(HD_STAND_ROOM_COVERED, true, 1, 0) &&
		HDStaticStandSure(HD_STAND_ROOM_COVERED, true, 2, 0) &&
		!HDStaticStandSure(HD_STAND_ROOM_CLASSIC, true, 2, 0) &&
		!HDStaticStandSure(HD_STAND_ROOM_COVERED, false, 2, 0),
		"static: sure is the carrier's art of a room drawn as it", 1, 1);
	// In part: the carrier's art on covered faces and its relief, never its
	// art lying whole on kept faces.
	ok(HDStaticStandSure(HD_STAND_ROOM_PART, true, 1, HD_STAND_FACES_COVERED) &&
		HDStaticStandSure(HD_STAND_ROOM_PART, true, 2, 0) &&
		!HDStaticStandSure(HD_STAND_ROOM_PART, true, 1, HD_STAND_FACES_KEPT),
		"static: in part, sure is the carrier's art on covered faces and its relief", 1, 1);
	// Art lying whole on a classic room's faces is left out while it is listed.
	ok(!HDStaticStandWithGroup(HD_STAND_ROOM_CLASSIC, false, 1, 0) &&
		HDStaticStandWithGroup(HD_STAND_ROOM_CLASSIC, false, 0, 0) &&
		HDStaticStandWithGroup(HD_STAND_ROOM_CLASSIC, false, 2, 0),
		"static: with its group, all but art lying whole on listed faces", 1, 1);
	ok(sure > 0 && withGroup > sure, "static: the two sets are not empty", sure, withGroup);
}

/* The object path's cut-out rule (specific\hd\hd_model_cut.h): the material
 * of a .TRM part, and whether a page's texels below the reference are holes.
 * The (below, at 0) pairs are the cache's own sample of the pages named, as
 * ModelProbe -slot prints them. */
static void model_cut()
{
	// The materials: 6, 7 and 8 cut, the gloss materials and the rest not.
	ok(HDTrmMaterialCuts(6), "cut: material 6 is a cut-out material", HDTrmMaterialCuts(6), 1);
	ok(HDTrmMaterialCuts(7), "cut: material 7 is a cut-out material", HDTrmMaterialCuts(7), 1);
	ok(HDTrmMaterialCuts(8), "cut: material 8 is a cut-out material", HDTrmMaterialCuts(8), 1);
	ok(!HDTrmMaterialCuts(0), "cut: material 0 is not", HDTrmMaterialCuts(0), 0);
	ok(!HDTrmMaterialCuts(3), "cut: material 3 (gloss) is not", HDTrmMaterialCuts(3), 0);
	ok(!HDTrmMaterialCuts(14), "cut: material 14 is not", HDTrmMaterialCuts(14), 0);
	ok(!HDTrmMaterialCuts(18), "cut: material 18 is not", HDTrmMaterialCuts(18), 0);
	// Compared whole: a high bit makes it another material.
	ok(!HDTrmMaterialCuts(0x8006), "cut: 0x8006 is not material 6", HDTrmMaterialCuts(0x8006), 0);

	// The pages the rule cuts.
	ok(HDPageHolesAreCoverage(12694, 11300),
		"cut: page 15080 (fan mesh, 890 per mille at 0) is holes",
		HDPageHolesAreCoverage(12694, 11300), 1);
	ok(HDPageHolesAreCoverage(11672, 9038),
		"cut: page 11163 (fan and trapdoor mesh, 774) is holes",
		HDPageHolesAreCoverage(11672, 9038), 1);
	ok(HDPageHolesAreCoverage(232, 177),
		"cut: page 22009 (ANDY1 straw, 762) is holes", HDPageHolesAreCoverage(232, 177), 1);
	ok(HDPageHolesAreCoverage(1505, 490),
		"cut: page 12052 (RICH3 grating stairs, 325) is holes",
		HDPageHolesAreCoverage(1505, 490), 1);

	// The page it keeps whole: the pistol grips, at alpha 3 to 5.
	ok(!HDPageHolesAreCoverage(1517, 7),
		"cut: page 8516 (pistol grips, 4 per mille at 0) is a mask",
		HDPageHolesAreCoverage(1517, 7), 0);

	// The edges: the threshold itself, nothing below the reference, and a
	// count that cannot be.
	ok(HDPageHolesAreCoverage(1000, HD_MODEL_CUT_ZERO_PERMILLE),
		"cut: exactly the threshold is holes",
		HDPageHolesAreCoverage(1000, HD_MODEL_CUT_ZERO_PERMILLE), 1);
	ok(!HDPageHolesAreCoverage(1000, HD_MODEL_CUT_ZERO_PERMILLE - 1),
		"cut: one below the threshold is a mask",
		HDPageHolesAreCoverage(1000, HD_MODEL_CUT_ZERO_PERMILLE - 1), 0);
	ok(!HDPageHolesAreCoverage(0, 0), "cut: a page with nothing below is not cut",
		HDPageHolesAreCoverage(0, 0), 0);
	ok(!HDPageHolesAreCoverage(10, 11), "cut: more at 0 than below is refused",
		HDPageHolesAreCoverage(10, 11), 0);
	ok(HD_MODEL_CUT_ZERO_PERMILLE > 4 && HD_MODEL_CUT_ZERO_PERMILLE < 325,
		"cut: the threshold lies in the measured gap 4..325", HD_MODEL_CUT_ZERO_PERMILLE, 100);

	// The share the tools print.
	ok(HDPageZeroPermille(12694, 11300) == 890, "cut: page 15080's share is 890",
		HDPageZeroPermille(12694, 11300), 890);
	ok(HDPageZeroPermille(1517, 7) == 4, "cut: page 8516's share is 4",
		HDPageZeroPermille(1517, 7), 4);
	ok(HDPageZeroPermille(0, 0) == 0, "cut: nothing below reads 0", HDPageZeroPermille(0, 0), 0);
	ok(HDPageZeroPermille(10, 20) == 1000, "cut: the share stops at 1000",
		HDPageZeroPermille(10, 20), 1000);
}

/* A camera matrix in the engine's layout (aW2VMx): the rotation - a yaw
 * about y after a pitch about x, orthonormal - in the three rows and the
 * camera's world position in M03 / M13 / M23. */
static void mirror_camera(float* m, double yaw, double pitch, float cx, float cy, float cz)
{
	const double sy = sin(yaw), cyw = cos(yaw), sp = sin(pitch), cp = cos(pitch);
	// Rows of Rx(pitch) * Ry(yaw) - any rotation will do.
	const double r[3][3] =
	{
		{ cyw, 0.0, -sy },
		{ sp * sy, cp, sp * cyw },
		{ cp * sy, -sp, cp * cyw }
	};

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m[4 * i + j] = (float)r[i][j];

	m[3] = cx;
	m[7] = cy;
	m[11] = cz;
}

// A world point through a camera matrix: R (p - c).
static void mirror_view_point(const float* m, const float* p, float* v)
{
	for (int i = 0; i < 3; i++)
		v[i] = m[4 * i] * (p[0] - m[3]) + m[4 * i + 1] * (p[1] - m[7]) +
			m[4 * i + 2] * (p[2] - m[11]);
}

/* A model matrix built the engine's way through a camera and a stack base:
 * the base's rotation times the model's (a yaw about y) for the axes, and the
 * base's rotation times (p - camera) for the translation - what
 * phd_TranslateAbs and phd_RotY make of it. */
static void mirror_model(const float* camera, const float* base, double yaw, const float* p,
	float* out)
{
	const double s = sin(yaw), c = cos(yaw);
	const double o[3][3] = { { c, 0.0, s }, { 0.0, 1.0, 0.0 }, { -s, 0.0, c } };

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			out[4 * i + j] = (float)(base[4 * i] * o[0][j] + base[4 * i + 1] * o[1][j] +
				base[4 * i + 2] * o[2][j]);

		out[4 * i + 3] = base[4 * i] * (p[0] - camera[3]) +
			base[4 * i + 1] * (p[1] - camera[7]) + base[4 * i + 2] * (p[2] - camera[11]);
	}
}

static long mirror_err(const float* a, const float* b, int n)
{
	double worst = 0.0;

	for (int i = 0; i < n; i++)
		if (fabs((double)a[i] - (double)b[i]) > worst)
			worst = fabs((double)a[i] - (double)b[i]);

	return (long)(worst * 1000.0);	// in thousandths
}

/* The mirror's arithmetic (game\mirror_math.h): the view reflected in a
 * wall's plane, and Lara's matrices moved into that view. The planes and the
 * points are RICH2's washroom: room 134's glass at x = 33792, a door of the
 * cabin across the corridor at x = 35328. */
static void mirror_pose()
{
	const float plane = 33792.0f;
	float cam[12], twice[12];
	long lcam[12], ltwice[12];

	mirror_camera(cam, 1.1, 0.35, 34400.0f, -24900.0f, 77000.0f);

	// 1. Reflected twice is the view it was, to the bit.
	for (int i = 0; i < 12; i++)
		twice[i] = cam[i];

	MirrorView(twice, 0, plane, true);
	MirrorView(twice, 0, plane, true);
	ok(mirror_err(cam, twice, 12) == 0 && cam[3] == twice[3],
		"mirror: the view reflected twice is the view", mirror_err(cam, twice, 12), 0);

	for (int i = 0; i < 12; i++)
		lcam[i] = ltwice[i] = (long)(cam[i] * 16384.0f);

	lcam[3] = ltwice[3] = 34400;
	MirrorView(ltwice, 0, 33792L, true);
	MirrorView(ltwice, 0, 33792L, true);

	bool same = true;

	for (int i = 0; i < 12; i++)
		same = same && lcam[i] == ltwice[i];

	ok(same, "mirror: the fixed-point view reflected twice is the view", same ? 1 : 0, 1);

	/* 2. The reflected view shows a point where the real view shows its
	 * reflection: the door at x 35328 appears where a door at 32256 would. */
	float reflected[12];

	for (int i = 0; i < 12; i++)
		reflected[i] = cam[i];

	MirrorView(reflected, 0, plane, true);
	ok(reflected[3] == 2.0f * plane - 34400.0f,
		"mirror: the camera is reflected in the plane", (long)reflected[3], (long)(2.0f * plane - 34400.0f));

	const float door[3] = { 35328.0f, -24064.0f, 76288.0f };
	const float image[3] = { 2.0f * plane - door[0], door[1], door[2] };
	float v1[3], v2[3];

	mirror_view_point(reflected, door, v1);
	mirror_view_point(cam, image, v2);
	ok(mirror_err(v1, v2, 3) <= 10,
		"mirror: the reflected view shows the door where its image stands", mirror_err(v1, v2, 3), 0);

	// And the plane in the z direction the same way.
	float zref[12];
	const float zdoor[3] = { 34000.0f, -24064.0f, 76900.0f };
	const float zimage[3] = { zdoor[0], zdoor[1], 2.0f * 76800.0f - zdoor[2] };

	for (int i = 0; i < 12; i++)
		zref[i] = cam[i];

	MirrorView(zref, 2, 76800.0f, true);
	mirror_view_point(zref, zdoor, v1);
	mirror_view_point(cam, zimage, v2);
	ok(mirror_err(v1, v2, 3) <= 10,
		"mirror: a plane of constant z reflects the same way", mirror_err(v1, v2, 3), 0);

	/* 3. A mirrored pose faces the other way: an item turned to +x (y_rot
	 * 16384) is seen in the x-plane mirror as one turned to -x, and the
	 * directions across the plane keep theirs. */
	const float east[3] = { 1.0f, 0.0f, 0.0f };
	const float north[3] = { 0.0f, 0.0f, 1.0f };
	float d1[3], d2[3];

	for (int i = 0; i < 3; i++)
	{
		d1[i] = reflected[4 * i] * east[0] + reflected[4 * i + 1] * east[1] + reflected[4 * i + 2] * east[2];
		d2[i] = -(cam[4 * i] * east[0] + cam[4 * i + 1] * east[1] + cam[4 * i + 2] * east[2]);
	}

	ok(mirror_err(d1, d2, 3) == 0,
		"mirror: a pose facing +x faces -x in the glass", mirror_err(d1, d2, 3), 0);

	for (int i = 0; i < 3; i++)
	{
		d1[i] = reflected[4 * i] * north[0] + reflected[4 * i + 1] * north[1] + reflected[4 * i + 2] * north[2];
		d2[i] = cam[4 * i] * north[0] + cam[4 * i + 1] * north[1] + cam[4 * i + 2] * north[2];
	}

	ok(mirror_err(d1, d2, 3) == 0,
		"mirror: a pose facing along the glass keeps its facing", mirror_err(d1, d2, 3), 0);

	/* 4. Lara's matrices moved into the reflected view are the matrices the
	 * reflected view would have built - with the stack's base carrying a
	 * shake scale in its columns, as phd_GenerateW2V leaves it. */
	float base[12], rbase[12];

	for (int i = 0; i < 12; i++)
		base[i] = cam[i];

	for (int i = 0; i < 3; i++)
	{
		base[4 * i] *= 1.03f;
		base[4 * i + 1] *= 0.98f;
	}

	for (int i = 0; i < 12; i++)
		rbase[i] = base[i];

	MirrorView(rbase, 0, 0.0f, false);

	const float hips[3] = { 34450.0f, -24800.0f, 77100.0f };
	float built[12], want[12], moved[12], q[9], t[3];

	mirror_model(cam, base, 0.7, hips, built);
	mirror_model(reflected, rbase, 0.7, hips, want);
	MirrorViewSpaceSetup(cam, base, 0, plane, q, t);

	for (int i = 0; i < 12; i++)
		moved[i] = built[i];

	MirrorViewSpaceApply(q, t, moved);
	ok(mirror_err(moved, want, 12) <= 20,
		"mirror: Lara's matrix moved into the glass is the reflected view's",
		mirror_err(moved, want, 12), 0);

	// And moved twice it is where it started.
	MirrorViewSpaceApply(q, t, moved);
	ok(mirror_err(moved, built, 12) <= 20,
		"mirror: Lara's matrix moved twice is where it started", mirror_err(moved, built, 12), 0);

	/* 5. Which points the rule draws: RICH2 room 134's copy box as the engine
	 * measured it (x 31738..33785, y -25607..-24042, z 74758..78848), the
	 * copy on the smaller-x side of x 33792, and the draw's 256 of slack. */
	const long mins[3] = { 31738, -25607, 74758 };
	const long maxs[3] = { 33785, -24042, 78848 };
	const long p189[3] = { 35328, -24064, 76288 };	// the kick door of cabin 131
	const long plara[3] = { 34304, -24064, 77000 };	// Lara in the corridor
	const long p199[3] = { 32256, -24064, 77312 };	// room 136's door, behind the glass
	const long pfar[3] = { 38400, -20992, 74240 };	// room 140, far past the copy

	ok(MirrorLandsInCopy(p189, 0, 33792, -1, mins, maxs, 256),
		"mirror: the door across the corridor is drawn in the glass", 0, 1);
	ok(MirrorLandsInCopy(plara, 0, 33792, -1, mins, maxs, 256),
		"mirror: Lara in the corridor is drawn in the glass", 0, 1);
	ok(!MirrorLandsInCopy(p199, 0, 33792, -1, mins, maxs, 256),
		"mirror: nothing behind the glass is drawn in it", 1, 0);
	ok(!MirrorLandsInCopy(pfar, 0, 33792, -1, mins, maxs, 256),
		"mirror: a point whose image leaves the copy is not drawn", 1, 0);

	/* 6. The slack does not hold along the glass: RICH2 room 135's copy
	 * (x 28669..30716, z 74764..79872, plane x 30720) ends where corridor
	 * room 56 (z 73728..74752) goes on across the plane. Lara at the
	 * corridor's south edge would stand, reflected, in the corridor itself;
	 * room 136's door lies inside the copy and is drawn. */
	const long mins135[3] = { 28669, -25607, 74764 };
	const long maxs135[3] = { 30716, -24042, 79872 };
	const long pcorridor[3] = { 31232, -24064, 74600 };	// Lara in room 56
	const long p199b[3] = { 32256, -24064, 77312 };		// room 136's door

	ok(!MirrorLandsInCopy(pcorridor, 0, 30720, -1, mins135, maxs135, 256),
		"mirror: a point past the copy's end along the glass is not drawn", 1, 0);
	ok(MirrorLandsInCopy(p199b, 0, 30720, -1, mins135, maxs135, 256),
		"mirror: room 136's door is drawn in room 135's glass", 0, 1);
}

// A point through a camera matrix and onto the screen, as the classic path
// projects it (specific\polyinsert.cpp): x = cx + persp vx / vz, y likewise.
static void mirror_screen(const float* m, const float* p, double persp, double cx,
	double cy, double* sx, double* sy)
{
	float v[3];

	mirror_view_point(m, p, v);
	*sx = cx + persp * v[0] / v[2];
	*sy = cy + persp * v[1] / v[2];
}

// The three plane tests of a view-space point; bit i set where plane i keeps it.
static int mirror_kept(const float* planes, const float* v)
{
	int bits = 0;

	for (int i = 0; i < 3; i++)
		if (planes[4 * i] * v[0] + planes[4 * i + 1] * v[1] + planes[4 * i + 2] * v[2] +
			planes[4 * i + 3] >= 0.0f)
			bits |= 1 << i;

	return bits;
}

/* The reflection's bounds (game\mirror_math.h): the three clip planes the HD
 * draw cuts a reflected body with, and the copy's rectangle on the screen the
 * classic draw is cut to. The glass is RICH2 room 135's (plane x = 30720, the
 * copy at smaller x, box x 28669..30716, y -25607..-24042, z 74764..79872);
 * the still camera is the corridor view of room 56 at eye (31953, -25213,
 * 74166) on Lara at (30877, -24723, 74784), 2560 x 1440 at persp 1144. */
static void mirror_clip()
{
	const long P = 30720;
	const long mins[3] = { 28669, -25607, 74764 };
	const long maxs[3] = { 30716, -24042, 79872 };
	float cam[12], reflected[12], planes[12];

	// 1. On a reflected view each plane's sign is the image's world test,
	// for a grid on both sides of the glass and past both ends of the copy.
	mirror_camera(cam, -1.05, -0.37, 31953.0f, -25213.0f, 74166.0f);

	for (int i = 0; i < 12; i++)
		reflected[i] = cam[i];

	MirrorView(reflected, 0, (float)P, true);
	MirrorClipPlanes(reflected, 0, P, -1, mins, maxs, planes);

	int wrong = 0, points = 0;

	for (int x = 29000; x <= 32500; x += 250)
		for (int z = 73500; z <= 80500; z += 250)
			for (int y = -25500; y <= -24000; y += 750)
			{
				const float p[3] = { (float)x + 3.0f, (float)y, (float)z + 3.0f };
				const double image[3] = { 2.0 * P - p[0], p[1], p[2] };
				const int want = (image[0] <= P ? 1 : 0) | (image[2] >= mins[2] ? 2 : 0) |
					(image[2] <= maxs[2] ? 4 : 0);
				float v[3];

				mirror_view_point(reflected, p, v);

				if (mirror_kept(planes, v) != want)
					wrong++;

				points++;
			}

	ok(wrong == 0 && points > 1000,
		"mirror clip: each plane keeps what the image's world test keeps", wrong, 0);

	// The same for a plane of constant z with the copy at larger z.
	const long zmins[3] = { 31000, -25600, 77000 };
	const long zmaxs[3] = { 33000, -24000, 79000 };
	float zref[12], zplanes[12];

	for (int i = 0; i < 12; i++)
		zref[i] = cam[i];

	MirrorView(zref, 2, 76800.0f, true);
	MirrorClipPlanes(zref, 2, 76800, 1, zmins, zmaxs, zplanes);
	wrong = 0;

	for (int x = 30500; x <= 33500; x += 250)
		for (int z = 74500; z <= 79500; z += 250)
		{
			const float p[3] = { (float)x + 3.0f, -24500.0f, (float)z + 3.0f };
			const double image[3] = { p[0], p[1], 2.0 * 76800 - p[2] };
			const int want = (image[2] >= 76800 ? 1 : 0) | (image[0] >= zmins[0] ? 2 : 0) |
				(image[0] <= zmaxs[0] ? 4 : 0);
			float v[3];

			mirror_view_point(zref, p, v);

			if (mirror_kept(zplanes, v) != want)
				wrong++;
		}

	ok(wrong == 0, "mirror clip: a plane of constant z bounds the same way", wrong, 0);

	// 2. The planes that are off keep every point.
	float off[12];
	const float far_point[3] = { -1.0e5f, 3.0e4f, -2.0e5f };

	MirrorClipOff(off);
	ok(mirror_kept(off, far_point) == 7, "mirror clip: the planes that are off keep every point",
		mirror_kept(off, far_point), 7);

	// 3. The copy's rectangle. A level camera looking along +z at a box
	// straight ahead: the rectangle holds the eight projections and lies
	// inside the window.
	float level[12];
	const long screen[4] = { 0, 0, 2560, 1440 };
	const long bmins[3] = { -300, -200, 2000 };
	const long bmaxs[3] = { 500, 400, 3000 };
	long win[4];

	mirror_camera(level, 0.0, 0.0, 0.0f, 0.0f, 0.0f);
	MirrorBoxWindow(level, bmins, bmaxs, 1144.0f, 1280.0f, 720.0f, 20, screen, win);

	bool holds = win[0] >= screen[0] && win[1] >= screen[1] && win[2] <= screen[2] &&
		win[3] <= screen[3] && win[0] < win[2] && win[1] < win[3];

	for (int i = 0; i < 8; i++)
	{
		const float p[3] = { (float)(i & 1 ? bmaxs[0] : bmins[0]),
			(float)(i & 2 ? bmaxs[1] : bmins[1]), (float)(i & 4 ? bmaxs[2] : bmins[2]) };
		double sx, sy;

		mirror_screen(level, p, 1144.0, 1280.0, 720.0, &sx, &sy);
		holds = holds && sx >= win[0] && sx <= win[2] && sy >= win[1] && sy <= win[3];
	}

	ok(holds, "mirror clip: the rectangle holds the box's eight corners inside the window",
		holds ? 1 : 0, 1);

	// A point straight ahead lands on the centre.
	const long pt[3] = { 0, 0, 2500 };

	MirrorBoxWindow(level, pt, pt, 1144.0f, 1280.0f, 720.0f, 20, screen, win);
	ok(win[0] == 1280 && win[2] == 1280 && win[1] == 720 && win[3] == 720,
		"mirror clip: a point straight ahead lands on the centre", win[0] * 10000 + win[1],
		1280 * 10000 + 720);

	// A corner behind the camera gives the window itself.
	const long behind[3] = { -300, -200, -100 };

	MirrorBoxWindow(level, behind, bmaxs, 1144.0f, 1280.0f, 720.0f, 20, screen, win);
	ok(win[0] == 0 && win[1] == 0 && win[2] == 2560 && win[3] == 1440,
		"mirror clip: a corner behind the camera gives the window", win[2], 2560);

	// 4. The corridor view of room 56. The still camera's own matrix.
	const double dx = 30877.0 - 31953.0, dy = -24723.0 + 25213.0, dz = 74784.0 - 74166.0;
	float owner[12], oref[12], oplanes[12];

	mirror_camera(owner, atan2(dx, dz), atan2(-dy, sqrt(dx * dx + dz * dz)), 31953.0f,
		-25213.0f, 74166.0f);

	double sx, sy;
	const float target[3] = { 30877.0f, -24723.0f, 74784.0f };

	mirror_screen(owner, target, 1144.0, 1280.0, 720.0, &sx, &sy);
	ok(fabs(sx - 1280.0) < 0.5 && fabs(sy - 720.0) < 0.5,
		"mirror clip: the still camera looks at its target", (long)sx, 1280);

	for (int i = 0; i < 12; i++)
		oref[i] = owner[i];

	MirrorView(oref, 0, (float)P, true);
	MirrorClipPlanes(oref, 0, P, -1, mins, maxs, oplanes);

	/* A shoulder of the reflection in the corridor: its image (30600, -24500,
	 * 74700) is drawn at (30840, -24500, 74700). In front of the glass's plane
	 * and 64 units past the copy's end at z 74764: the end plane cuts it. */
	const float shoulder[3] = { 30840.0f, -24500.0f, 74700.0f };
	const float shoulderImage[3] = { 30600.0f, -24500.0f, 74700.0f };
	float v[3];

	mirror_view_point(oref, shoulder, v);
	ok(mirror_kept(oplanes, v) == 5,
		"mirror clip: the shoulder in the corridor fails the copy's end", mirror_kept(oplanes, v), 5);

	const double endDot = oplanes[4] * v[0] + oplanes[5] * v[1] + oplanes[6] * v[2] + oplanes[7];

	ok(fabs(endDot + 64.0) < 1.0, "mirror clip: the shoulder stands 64 units past the end",
		(long)floor(endDot + 0.5), -64);

	// A point inside the copy: image (30563, -24500, 75000), drawn at
	// (30877, -24500, 75000), passes all three and lies in the rectangle.
	const float inside[3] = { 30877.0f, -24500.0f, 75000.0f };
	const float insideImage[3] = { 30563.0f, -24500.0f, 75000.0f };

	mirror_view_point(oref, inside, v);
	ok(mirror_kept(oplanes, v) == 7, "mirror clip: a point inside the copy passes all three",
		mirror_kept(oplanes, v), 7);

	MirrorBoxWindow(owner, mins, maxs, 1144.0f, 1280.0f, 720.0f, 20, screen, win);
	mirror_screen(owner, insideImage, 1144.0, 1280.0, 720.0, &sx, &sy);
	ok(sx >= win[0] && sx <= win[2] && sy >= win[1] && sy <= win[3],
		"mirror clip: the point inside the copy lies in the rectangle", (long)sx, win[0]);

	/* The rectangle is a bound only: the copy's far corner (28669, -25607,
	 * 74764) reaches to x 820 on the screen, so over the whole screen it holds
	 * the shoulder (x 1129) too, and the planes are what cut it. The mirror
	 * room's portal window is what cuts it for a classic draw: room 135 is
	 * seen through doorway 177/135, whose west edge at the floor projects to
	 * x 1694, and the rectangle inside that window leaves the shoulder at
	 * least 40 pixels to the left. */
	double shx, shy;

	mirror_screen(owner, shoulderImage, 1144.0, 1280.0, 720.0, &shx, &shy);
	ok(win[0] == 820 && shx > win[0],
		"mirror clip: over the whole screen the rectangle holds the shoulder", win[0], 820);

	const float doorway[3] = { 30720.0f, -24064.0f, 75776.0f };
	double dwx, dwy;

	mirror_screen(owner, doorway, 1144.0, 1280.0, 720.0, &dwx, &dwy);

	const long portal[4] = { (long)floor(dwx - 1.0), 0, 2560, 1440 };

	MirrorBoxWindow(owner, mins, maxs, 1144.0f, 1280.0f, 720.0f, 20, portal, win);
	ok(shx <= win[0] - 40.0,
		"mirror clip: inside room 135's window the rectangle leaves the shoulder out",
		(long)shx, win[0]);
}

/* The sheen: which surfaces shine (specific\hd\hd_gloss_math.h and its page
 * table hd_gloss_pages.h). */

// One page named by every source at once, and each source taken away in turn.
static void gloss_order()
{
	HDGloss mat;
	const float param[4] = { 45.0f, 0.2f, 0.6f, 0.0f };

	HDGlossFromMaterial(0x8003u, param, &mat);

	HDGlossPage file = { 140, HD_GLOSS_MARBLE, 0.5f, 20.0f, 0.3f };
	HDGlossPage table = { 140, HD_GLOSS_ICE, -1.0f, -1.0f, -1.0f };
	HDGlossPage veto = { 140, HD_GLOSS_NONE, -1.0f, -1.0f, -1.0f };

	HDGloss g = HDGlossResolve(&mat, &file, false, &table, HD_GLOSS_METAL, false);
	ok(g.source == HD_GLOSS_FROM_MAT && g.strength == 0.6f && g.power == 45.0f,
		"gloss order: the material over the file, the table and the seed", g.source,
		HD_GLOSS_FROM_MAT);
	ok(g.cls == HD_GLOSS_MARBLE && g.f0 == 0.3f,
		"gloss order: a page's named class and F0 go onto the material's gloss", g.cls,
		HD_GLOSS_MARBLE);

	g = HDGlossResolve(0, &file, false, &table, HD_GLOSS_METAL, false);
	ok(g.source == HD_GLOSS_FROM_FILE && g.strength == 0.5f && g.power == 20.0f,
		"gloss order: the file over the table and the seed", g.source, HD_GLOSS_FROM_FILE);

	g = HDGlossResolve(0, 0, false, &table, HD_GLOSS_METAL, false);
	ok(g.source == HD_GLOSS_FROM_TABLE && g.cls == HD_GLOSS_ICE,
		"gloss order: the table over the seed", g.source, HD_GLOSS_FROM_TABLE);

	g = HDGlossResolve(0, 0, false, 0, HD_GLOSS_METAL, false);
	ok(g.source == HD_GLOSS_FROM_SEED && g.cls == HD_GLOSS_METAL && g.f0 == 0.5f,
		"gloss order: the seed alone", g.source, HD_GLOSS_FROM_SEED);

	g = HDGlossResolve(0, 0, false, 0, HD_GLOSS_NONE, false);
	ok(g.strength == 0.0f && g.source == HD_GLOSS_FROM_NONE,
		"gloss order: nothing names the page, nothing shines", (long)(g.strength * 1000), 0);

	g = HDGlossResolve(0, 0, true, &table, HD_GLOSS_METAL, false);
	ok(g.strength == 0.0f, "gloss order: the file's '-' takes the page from table and seed",
		(long)(g.strength * 1000), 0);

	g = HDGlossResolve(0, 0, false, &veto, HD_GLOSS_METAL, false);
	ok(g.strength == 0.0f, "gloss order: the table's '-' line takes the page from the seed",
		(long)(g.strength * 1000), 0);

	g = HDGlossResolve(0, &file, false, &veto, HD_GLOSS_METAL, false);
	ok(g.source == HD_GLOSS_FROM_FILE, "gloss order: the file names a page the table took out",
		g.source, HD_GLOSS_FROM_FILE);

	g = HDGlossResolve(&mat, 0, true, 0, HD_GLOSS_METAL, false);
	ok(g.source == HD_GLOSS_FROM_MAT && g.cls == HD_GLOSS_GLASS,
		"gloss order: no '-' takes a material's own gloss", g.source, HD_GLOSS_FROM_MAT);

	g = HDGlossResolve(0, &file, false, &table, HD_GLOSS_METAL, true);
	ok(g.strength == 0.0f, "gloss order: a barred page takes nothing but the material",
		(long)(g.strength * 1000), 0);

	g = HDGlossResolve(&mat, &file, false, &table, HD_GLOSS_METAL, true);
	ok(g.source == HD_GLOSS_FROM_MAT && g.cls == HD_GLOSS_GLASS,
		"gloss order: a barred page keeps its material, class and all", g.cls, HD_GLOSS_GLASS);

	// RICH2's lobby: its clear floors and its marble, and not the pictures
	// numbered between them (the washroom's mosaic 1054).
	const int lobby[] = { 1010, 1029, 1032, 1033, 1045, 1047, 1049, 1053, 1079, 1081 };
	int barred = 0;

	for (size_t i = 0; i < sizeof(lobby) / sizeof(lobby[0]); i++)
		barred += HDGlossPageBarred(lobby[i]) ? 1 : 0;

	ok(barred == 10 && !HDGlossPageBarred(1054) && !HDGlossPageBarred(1082) &&
		!HDGlossPageBarred(140), "gloss order: RICH2's lobby pages are barred, no other",
		barred, 10);
}

// hd_gloss.txt, line by line and whole.
static void gloss_parser()
{
	HDGlossPage e;
	bool remove = false;

	ok(HDGlossParseLine("140 metal", &e, &remove) == 1 && e.page == 140 &&
		e.cls == HD_GLOSS_METAL && e.strength < 0.0f && e.power < 0.0f && e.f0 < 0.0f &&
		!remove, "gloss parser: a page and a class, the rest the class's", e.page, 140);

	ok(HDGlossParseLine("  141\tMarble 0.5 32 0.1   # the crate's front", &e, &remove) == 1 &&
		e.page == 141 && e.cls == HD_GLOSS_MARBLE && e.strength == 0.5f &&
		e.power == 32.0f && e.f0 == 0.1f, "gloss parser: strength, exponent, F0 and a comment",
		(long)e.power, 32);

	ok(HDGlossParseLine("142 ice - 16", &e, &remove) == 1 && e.strength < 0.0f &&
		e.power == 16.0f && e.f0 < 0.0f, "gloss parser: '-' keeps one number's default",
		(long)e.power, 16);

	ok(HDGlossParseLine("143 custom 0.4 20 0.3", &e, &remove) == 1 &&
		e.cls == HD_GLOSS_CUSTOM, "gloss parser: a custom page with its three numbers",
		e.cls, HD_GLOSS_CUSTOM);

	ok(HDGlossParseLine("-141", &e, &remove) == 1 && remove && e.page == 141,
		"gloss parser: '-' takes a page out", remove ? 1 : 0, 1);

	ok(HDGlossParseLine("# only a comment", &e, &remove) == 0 &&
		HDGlossParseLine("", &e, &remove) == 0 && HDGlossParseLine(" \t ", &e, &remove) == 0,
		"gloss parser: blank and comment lines are no entry", 0, 0);

	const char* bad[] =
	{
		"abc metal", "140 bogus", "140", "140 metal 2", "140 metal 0", "140 metal 0.5 300",
		"140 metal 0.5 32 1.5", "140 custom 0.5 20", "0 metal", "70000 metal",
		"-141 metal", "140 metal x", "140 metal 0.5 32 0.1 9", "14a metal", "-"
	};
	int rejected = 0;

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		rejected += HDGlossParseLine(bad[i], &e, &remove) < 0 ? 1 : 0;

	ok(rejected == (int)(sizeof(bad) / sizeof(bad[0])),
		"gloss parser: every malformed line is refused", rejected,
		(long)(sizeof(bad) / sizeof(bad[0])));

	// The whole file: CR LF, LF and CR alone, a later line winning, bad lines
	// counted and the rest kept.
	HDGlossFile file;

	HDGlossParseText("# my pages\r\n140 metal\r\n141 marble 0.6\n-150\r"
		"151 wrong\n140 ice\n\n152 glass", &file);
	bool removes = false;
	const HDGlossPage* p140 = file.find(140, &removes);
	const HDGlossPage* p150 = file.find(150, &removes);

	ok(file.present && file.lines == 8 && file.bad == 1,
		"gloss parser: the file's lines and its one bad line", file.lines * 100 + file.bad, 801);
	ok(p140 && p140->cls == HD_GLOSS_ICE, "gloss parser: a later line for a page wins",
		p140 ? p140->cls : -1, HD_GLOSS_ICE);
	ok(!p150 && removes, "gloss parser: the file's '-' line reads as a removal",
		removes ? 1 : 0, 1);
	ok(file.find(152, &removes) && !removes && !file.find(151, &removes),
		"gloss parser: the last line without an end is read, the bad one is not", 1, 1);

	HDGlossFile bom;

	HDGlossParseText("\xEF\xBB\xBF" "140 metal\r\n141 ice", &bom);
	ok(bom.bad == 0 && bom.find(140, &removes) && bom.find(141, &removes),
		"gloss parser: a UTF-8 byte order mark before the first line is skipped",
		bom.bad, 0);

	HDGlossFile none;

	HDGlossParseText(0, &none);
	ok(!none.present && none.entries.empty(), "gloss parser: no text is no file", 0, 0);
}

// The class defaults, the footstep materials and the base material.
static void gloss_class()
{
	HDGloss g = {};

	ok(HDGlossClassDefaults(HD_GLOSS_METAL, &g) && g.f0 == 0.5f && g.tint == 1.0f &&
		g.strength > 0.0f && g.power >= 1.0f, "gloss class: metal reflects face on, in its colour",
		(long)(g.f0 * 100), 50);

	int dielectric = 0;
	const int others[3] = { HD_GLOSS_MARBLE, HD_GLOSS_ICE, HD_GLOSS_GLASS };

	for (int i = 0; i < 3; i++)
		dielectric += HDGlossClassDefaults(others[i], &g) && g.f0 < 0.5f && g.tint == 0.0f &&
			g.strength > 0.0f ? 1 : 0;

	ok(dielectric == 3, "gloss class: marble, ice and glass reflect less, untinted",
		dielectric, 3);
	ok(!HDGlossClassDefaults(HD_GLOSS_NONE, &g) && g.strength == 0.0f,
		"gloss class: no class, no gloss", 0, 0);

	ok(HDGlossClassOfFx(8) == HD_GLOSS_METAL && HDGlossClassOfFx(13) == HD_GLOSS_METAL &&
		HDGlossClassOfFx(9) == HD_GLOSS_MARBLE && HDGlossClassOfFx(4) == HD_GLOSS_ICE &&
		HDGlossClassOfFx(6) == HD_GLOSS_NONE && HDGlossClassOfFx(5) == HD_GLOSS_NONE,
		"gloss class: footstep metal, old metal, marble and ice, nothing else", 1, 1);

	ok(HDGlossBaseMaterial(0x8003u) == 3u && HDGlossBaseMaterial(0x8403u) == 3u &&
		HDGlossBaseMaterial(17u) == 17u, "gloss class: 0x8003 is material 3",
		(long)HDGlossBaseMaterial(0x8003u), 3);

	const float rich3[4] = { 45.0f, 0.2f, 0.6f, 0.0f };
	const float joby3[4] = { 36.0f, 0.1f, 0.0f, 0.0f };
	const float matt[4] = { 2.0f, 0.0f, 0.0f, 0.0f };
	const float odd[4] = { 44.5f, 0.0f, 0.6f, 0.0f };

	ok(HDGlossFromMaterial(0x8203u, rich3, &g) && g.strength == 0.6f && g.power == 45.0f &&
		g.cls == HD_GLOSS_GLASS && g.source == HD_GLOSS_FROM_MAT,
		"gloss class: RICH3's destructible panes carry material 3's gloss", (long)g.power, 45);
	ok(HDGlossFromMaterial(17u, joby3, &g) && g.strength == 0.1f && g.cls == HD_GLOSS_METAL,
		"gloss class: material 17 is metal, its strength in param[1]", g.cls, HD_GLOSS_METAL);
	ok(!HDGlossFromMaterial(3u, matt, &g) && !HDGlossFromMaterial(3u, odd, &g) &&
		!HDGlossFromMaterial(0u, rich3, &g) && !HDGlossFromMaterial(14u, rich3, &g),
		"gloss class: no strength, an odd exponent or another material names none", 0, 0);

	// The table: sorted, one line per page, every class a real one or a
	// removal, and none of RICH2's lobby pages in it but as a line of the
	// mirror column alone.
	int bad_lines = 0;

	for (int i = 0; i < HDGlossTableSize(); i++)
	{
		const HDGlossPage& t = kHDGlossPages[i];

		if ((i && kHDGlossPages[i - 1].page >= t.page) ||
			(HDGlossPageBarred(t.page) && t.cls != HD_GLOSS_ASIS) ||
			t.cls < HD_GLOSS_ASIS || t.cls > HD_GLOSS_CUSTOM ||
			(t.mirror != 0 && t.mirror != 1) || HDGlossTableFind(t.page) != &t)
			bad_lines++;
	}

	ok(bad_lines == 0 && HDGlossTableSize() > 0 && HDGlossTableFind(141) &&
		HDGlossTableFind(141)->cls == HD_GLOSS_METAL && !HDGlossTableFind(140),
		"gloss class: the page table is sorted, unique and keeps out of the lobby", bad_lines, 0);
}

// The seed.
static void gloss_seed()
{
	int fx[16] = { 0 };

	// The crate's page 140 in ANDREA1: 162 on metal, 128 on stone.
	fx[8] = 162;
	fx[6] = 128;
	ok(HDGlossSeedClass(fx, 2490) == HD_GLOSS_METAL,
		"gloss seed: the crate's page is metal", HDGlossSeedClass(fx, 2490), HD_GLOSS_METAL);

	// The crate's front, 141: 18 on metal, 88 on stone.
	fx[8] = 18;
	fx[6] = 88;
	ok(HDGlossSeedClass(fx, 846) == HD_GLOSS_NONE,
		"gloss seed: a page mostly on stone stays matt", HDGlossSeedClass(fx, 846), 0);

	// RICH2's 3008: 194 on metal of 226,746 triangles.
	fx[8] = 194;
	fx[6] = 104;
	ok(HDGlossSeedClass(fx, 226746) == HD_GLOSS_NONE,
		"gloss seed: a page the level wears everywhere is not a metal page",
		HDGlossSeedClass(fx, 226746), 0);

	fx[8] = 0;
	fx[6] = 0;
	fx[9] = 288;
	ok(HDGlossSeedClass(fx, 288) == HD_GLOSS_MARBLE, "gloss seed: marble",
		HDGlossSeedClass(fx, 288), HD_GLOSS_MARBLE);

	fx[9] = 0;
	fx[4] = 1512;
	ok(HDGlossSeedClass(fx, 3375) == HD_GLOSS_ICE, "gloss seed: ice",
		HDGlossSeedClass(fx, 3375), HD_GLOSS_ICE);

	fx[4] = 1;
	ok(HDGlossSeedClass(fx, 1) == HD_GLOSS_NONE, "gloss seed: one stray triangle names nothing",
		HDGlossSeedClass(fx, 1), 0);

	// A room of 2 x 2 squares at the origin: (1, 0) is metal with its floor
	// at -768, the crate's top; (0, 0) a wall.
	const HDRoomGrid grid = { 0, 2, 2, 36864.0f, 28672.0f };
	const HDSectorFloor squares[4] =
	{
		{ -32512, 0, 1 }, { 256, 6, 0 }, { -768, 8, 0 }, { 256, 6, 0 }
	};
	const float a[3] = { 1100, -768, 100 }, b[3] = { 1900, -768, 100 }, c[3] = { 1100, -768, 900 };
	const float up_a[3] = { 1100, -1200, 100 }, up_b[3] = { 1900, -1200, 100 },
		up_c[3] = { 1100, -1200, 900 };
	int votes[16] = { 0 };

	HDGlossSeedVote(a, b, c, -1.0f, grid, squares, 4, votes);
	HDGlossSeedVote(a, b, c, 1.0f, grid, squares, 4, votes);		// a ceiling
	HDGlossSeedVote(up_a, up_b, up_c, -1.0f, grid, squares, 4, votes);	// 432 above
	ok(votes[8] == 1 && votes[6] == 0, "gloss seed: a floor triangle on the metal square votes",
		votes[8], 1);

	const float wa[3] = { 100, -500, 100 }, wb[3] = { 100, 0, 100 }, wc[3] = { 100, -500, 900 };

	ok(!HDGlossFacesUp(wa, wb, wc, -0.1f) && HDGlossFacesUp(a, b, c, -1.0f) &&
		!HDGlossFacesUp(a, b, c, 1.0f), "gloss seed: floors vote, walls and ceilings do not", 1, 1);
	ok(HDGlossSectorAt(grid, 1500.0f, 500.0f) == 2 && HDGlossSectorAt(grid, -1.0f, 0.0f) == -1 &&
		HDGlossSectorAt(grid, 2048.0f, 0.0f) == -1, "gloss seed: the square under a point",
		HDGlossSectorAt(grid, 1500.0f, 500.0f), 2);
}

/* The light field (specific\hd\hd_gloss_field.h): the grid, the texels, a
 * lamp's falloff, the build, the sample and the sight test. */
static void gloss_field_grid()
{
	// One room box and one lamp in its middle.
	const float box[6] = { 1024.0f, -4096.0f, 1024.0f, 9216.0f, 0.0f, 5120.0f };
	const HDGlossLamp lamp = { { 5120.0f, -2048.0f, 3072.0f }, { 20.0f, 20.0f, 20.0f } };
	HDGlossFieldGrid grid;

	ok(HDGlossFieldLayout(box, 1, &lamp, 1, false, 0, &grid) &&
		grid.origin[0] == 0.0f && grid.origin[1] == -5120.0f && grid.origin[2] == 0.0f &&
		grid.dims[0] == 11 && grid.dims[1] == 7 && grid.dims[2] == 7,
		"gloss field grid: the rooms' box on whole cells, one cell more each side",
		grid.dims[0] * 100 + grid.dims[1] * 10 + grid.dims[2], 1177);

	HDGlossFieldGrid two;

	ok(HDGlossFieldLayout(box, 1, &lamp, 1, true, 0, &two) && two.dims[0] == 16 &&
		two.dims[1] == 8 && two.dims[2] == 8 && two.origin[0] == grid.origin[0],
		"gloss field grid: a device that asks for powers of two gets them, same corner",
		two.dims[0], 16);

	ok(!HDGlossFieldLayout(box, 1, &lamp, 1, false, 8, &two) && two.dims[0] == 0,
		"gloss field grid: a side past the device's largest is no field", two.dims[0], 0);

	const HDGlossLamp far = { { 50000.0f, -2048.0f, 3072.0f }, { 20.0f, 20.0f, 20.0f } };

	ok(!HDGlossFieldLayout(box, 1, &far, 1, false, 0, &two),
		"gloss field grid: a lamp that reaches no room is no field", 0, 0);

	// The cell of a world position, and the order of the cells.
	int cell[3];
	const float p[3] = { 5120.0f, -2048.0f, 3072.0f };
	const float out[3] = { -1.0f, -2048.0f, 3072.0f };

	ok(HDGlossFieldCellOf(grid, p, cell) && cell[0] == 5 && cell[1] == 3 && cell[2] == 3 &&
		!HDGlossFieldCellOf(grid, out, cell),
		"gloss field grid: the cell of a world position, none outside", cell[0], 5);
	ok(HDGlossFieldIndex(grid, 1, 0, 0) == 1 && HDGlossFieldIndex(grid, 0, 1, 0) == 11 &&
		HDGlossFieldIndex(grid, 0, 0, 1) == 77,
		"gloss field grid: x fastest, then y, then z - a volume texture's rows and slices",
		(long)HDGlossFieldIndex(grid, 0, 0, 1), 77);
}

static void gloss_field_texels()
{
	ok(HDGlossFieldTexel(1.0f, 0.0f, 0.0f, 0.0f) == 0x00FF0000u &&
		HDGlossFieldTexel(0.0f, 0.0f, 1.0f, 1.0f) == 0xFF0000FFu &&
		HDGlossFieldTexel(2.0f, -1.0f, 0.5f, 0.0f) == 0x00FF0080u,
		"gloss field texel: A8R8G8B8, clamped, rounded", 1, 1);

	// The direction's packing, round trip over a sweep of unit vectors.
	double worst = 0.0;
	int tested = 0;

	for (int a = 0; a < 24; a++)
		for (int b = -6; b <= 6; b++)
		{
			const double yaw = a * 3.14159265 / 12.0, pitch = b * 3.14159265 / 13.0;
			const float u[3] = { (float)(cos(pitch) * sin(yaw)), (float)sin(pitch),
				(float)(cos(pitch) * cos(yaw)) };
			float back[3], side = 0.0f;

			HDGlossFieldUnpackDir(HDGlossFieldPackDir(u, 0.6f), back, &side);

			for (int c = 0; c < 3; c++)
				worst = fabs(back[c] - u[c]) > worst ? fabs(back[c] - u[c]) : worst;

			worst = fabs(side - 0.6f) > worst ? fabs(side - 0.6f) : worst;
			tested++;
		}

	ok(tested == 312 && worst <= 1.0 / 255.0 + 1e-6,
		"gloss field texel: a direction and its one-sidedness come back within a byte's step",
		(long)(worst * 100000), 392);

	// A lamp's falloff: one at the lamp, nothing at its reach, smooth between.
	const float r2 = HD_GLOSS_LAMP_REACH * HD_GLOSS_LAMP_REACH;
	bool falls = true;

	for (int i = 1; i <= 64; i++)
		falls = falls && HDGlossFieldFalloff(r2 * i / 64.0f) < HDGlossFieldFalloff(r2 * (i - 1) / 64.0f);

	ok(HDGlossFieldFalloff(0.0f) == 1.0f && HDGlossFieldFalloff(r2) == 0.0f &&
		fabs(HDGlossFieldFalloff(r2 * 0.25f) - 0.5625f) < 1e-6 && falls,
		"gloss field falloff: (1 - d^2 / R^2)^2, falling all the way to the reach",
		(long)(HDGlossFieldFalloff(r2 * 0.25f) * 10000), 5625);

	ok(HDGlossFieldExposure(0.0f) == 0.0f && fabs(HDGlossFieldExposure(0.01f) - 0.01f) < 1e-4 &&
		HDGlossFieldExposure(50.0f) <= 1.0f && HDGlossFieldExposure(2.0f) > HDGlossFieldExposure(1.0f),
		"gloss field exposure: m where small, below one whatever the sum",
		(long)(HDGlossFieldExposure(1.0f) * 1000), 632);
}

static void gloss_field_build()
{
	const float box[6] = { 1024.0f, -4096.0f, 1024.0f, 9216.0f, 0.0f, 5120.0f };
	const HDGlossLamp lamp = { { 5120.0f, -2048.0f, 3072.0f }, { 20.0f, 10.0f, 0.0f } };
	HDGlossFieldGrid grid;
	HDGlossFieldStats stats;
	std::vector<unsigned int> dir, rgb;

	HDGlossFieldLayout(box, 1, &lamp, 1, false, 0, &grid);
	HDGlossFieldBuild(grid, &lamp, 1, dir, rgb, &stats);

	// The cell two to the lamp's -x side: the direction points +x at it.
	float u[3], side = 0.0f;
	const size_t k = HDGlossFieldIndex(grid, 3, 3, 3);

	HDGlossFieldUnpackDir(dir[k], u, &side);

	const float centre[3] = { 3584.0f, -1536.0f, 3584.0f };
	const float d[3] = { 5120.0f - centre[0], -2048.0f - centre[1], 3072.0f - centre[2] };
	const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	const float f = HDGlossFieldFalloff(len * len);
	const float m = 20.0f * HD_GLOSS_LAMP_NORM * f;
	const float red = (float)((rgb[k] >> 16) & 255u) / 255.0f;
	const float green = (float)((rgb[k] >> 8) & 255u) / 255.0f;

	ok(fabs(u[0] - d[0] / len) < 0.01f && fabs(u[1] - d[1] / len) < 0.01f &&
		fabs(u[2] - d[2] / len) < 0.01f && side > 0.99f,
		"gloss field build: one lamp - the cell points at it, wholly one-sided",
		(long)(side * 100), 100);
	ok(fabs(red - HDGlossFieldExposure(m)) < 0.005f && fabs(green - red * 0.5f) < 0.005f &&
		(rgb[k] & 255u) == 0u && fabs((float)(rgb[k] >> 24) / 255.0f - HDGlossFieldExposure(m)) < 0.005f,
		"gloss field build: its colour bounded by the exposure, the hue kept, the strength in alpha",
		(long)(red * 1000), (long)(HDGlossFieldExposure(m) * 1000));
	ok(stats.lamps == 1 && stats.lit > 0 && stats.lit <= (int)dir.size() &&
		stats.brightest > 0.0f, "gloss field build: the stats count the lamp and its cells",
		stats.lamps, 1);

	// Two equal lamps either side of a cell: no direction, a weak highlight.
	const HDGlossLamp pair[2] =
	{
		{ { 5632.0f - 2048.0f, -2560.0f, 3584.0f }, { 20.0f, 20.0f, 20.0f } },
		{ { 5632.0f + 2048.0f, -2560.0f, 3584.0f }, { 20.0f, 20.0f, 20.0f } }
	};

	HDGlossFieldLayout(box, 1, pair, 2, false, 0, &grid);
	HDGlossFieldBuild(grid, pair, 2, dir, rgb, &stats);

	// The centre of cell (5, 2, 3), halfway between the two.
	const float mid[3] = { 5632.0f, -2560.0f, 3584.0f };
	int c[3];

	HDGlossFieldCellOf(grid, mid, c);
	HDGlossFieldUnpackDir(dir[HDGlossFieldIndex(grid, c[0], c[1], c[2])], u, &side);

	const float both = (float)((rgb[HDGlossFieldIndex(grid, c[0], c[1], c[2])] >> 16) & 255u) / 255.0f;

	ok(side < 0.05f && both > 0.5f && stats.weak > 0,
		"gloss field build: two equal lamps either side cancel to no direction, bright but weak",
		(long)(side * 100), 0);

	// The sample: halfway between two cell centres is the mean of the two.
	float colour[3], dirs[3], one = 0.0f, length = 0.0f;
	const float half[3] = { mid[0] + 512.0f, mid[1], mid[2] };
	const size_t a = HDGlossFieldIndex(grid, c[0], c[1], c[2]);
	const size_t b = HDGlossFieldIndex(grid, c[0] + 1, c[1], c[2]);

	HDGlossFieldSample(grid, dir, rgb, half, colour, dirs, &one, &length);
	ok(fabs(colour[0] - 0.5f * ((float)((rgb[a] >> 16) & 255u) + (float)((rgb[b] >> 16) & 255u)) /
		255.0f) < 1e-4f && fabs(one - 0.5f * ((float)(dir[a] >> 24) + (float)(dir[b] >> 24)) /
		255.0f) < 1e-4f, "gloss field sample: halfway between two cells, the mean of the two",
		(long)(colour[0] * 1000), (long)(500.0f * ((float)((rgb[a] >> 16) & 255u) +
		(float)((rgb[b] >> 16) & 255u)) / 255.0f));

	// The sight test: a lamp of room 1, which room 0 does not see.
	const float rooms[12] =
	{
		1024.0f, -4096.0f, 1024.0f, 5120.0f, 0.0f, 5120.0f,
		5120.0f, -4096.0f, 1024.0f, 9216.0f, 0.0f, 5120.0f
	};
	const HDGlossLamp east = { { 7680.0f, -2048.0f, 3072.0f }, { 20.0f, 20.0f, 20.0f } };
	const int lampRoom[1] = { 1 };
	const unsigned char apart[4] = { 1, 0, 0, 1 };
	const HDGlossFieldSight sight = { rooms, 2, lampRoom, apart };
	std::vector<unsigned int> dirSight, rgbSight;
	HDGlossFieldStats statsSight;

	HDGlossFieldLayout(rooms, 2, &east, 1, false, 0, &grid);
	HDGlossFieldBuild(grid, &east, 1, dirSight, rgbSight, &statsSight, &sight);
	HDGlossFieldBuild(grid, &east, 1, dir, rgb, &stats);

	const float west[3] = { 3584.0f, -2560.0f, 3584.0f };
	const float own[3] = { 7680.0f, -2560.0f, 3584.0f };
	const float solid[3] = { 3584.0f, -4608.0f, 3584.0f };
	int cw[3], co[3], cs[3];

	HDGlossFieldCellOf(grid, west, cw);
	HDGlossFieldCellOf(grid, own, co);
	HDGlossFieldCellOf(grid, solid, cs);
	ok(rgbSight[HDGlossFieldIndex(grid, cw[0], cw[1], cw[2])] == 0u &&
		rgb[HDGlossFieldIndex(grid, cw[0], cw[1], cw[2])] != 0u &&
		rgbSight[HDGlossFieldIndex(grid, co[0], co[1], co[2])] ==
		rgb[HDGlossFieldIndex(grid, co[0], co[1], co[2])] &&
		rgbSight[HDGlossFieldIndex(grid, cs[0], cs[1], cs[2])] != 0u,
		"gloss field sight: a room that does not see the lamp's room takes nothing of it, "
		"its own room and a cell in no room do", statsSight.lit < stats.lit ? 1 : 0, 1);
}

// The bound: the sheen never above its named limit, whatever goes in.
static void gloss_field_bound()
{
	int over = 0, hue = 0, small = 0, white = 0, capped = 0, tested = 0;

	for (int i = 0; i <= 20; i++)
		for (int j = 0; j <= 20; j++)
			for (int t = 0; t < 4; t++)
			{
				const float tint[4][3] = { { 1, 1, 1 }, { 1, 0.5f, 0.2f }, { 0.1f, 0.3f, 1 }, { 0, 0, 1 } };
				const float s = 0.02f * (float)(i * i);		// up to 8 - far past white
				const float lit = 0.05f * (float)j;		// 0 to 1
				const float sheen[3] = { s * tint[t][0], s * tint[t][1], s * tint[t][2] };
				const float own[3] = { lit, lit * 0.8f, lit * 0.6f };
				float bounded[3], composed[3];

				HDGlossSheenBound(sheen, own, bounded);
				HDGlossSheenCompose(own, bounded, composed);

				const float limit = fminf(HD_GLOSS_BOUND * lit + HD_GLOSS_FLOOR, HD_GLOSS_CAP);
				const float top = fmaxf(bounded[0], fmaxf(bounded[1], bounded[2]));

				over += top > limit * (1.0f + 1e-5f) ? 1 : 0;

				for (int c = 0; c < 3; c++)
				{
					if (s > 0.0f && fabs(bounded[c] * sheen[t == 3 ? 2 : 0] - sheen[c] *
						bounded[t == 3 ? 2 : 0]) > 1e-5f)
						hue++;

					// Past white only where the surface is white already.
					if (lit < 1.0f - HD_GLOSS_FLOOR)
						white += composed[c] > 1.0f + 1e-6f ? 1 : 0;

					// And short of white by the cap: a quarter of the way stays.
					capped += composed[c] > own[c] + HD_GLOSS_CAP * (1.0f - own[c]) + 1e-6f ? 1 : 0;
				}

				// A sheen far under the limit passes almost whole.
				if (s > 0.0f && s * 50.0f < limit && fabs(top - s) > 0.02f * s)
					small++;

				tested++;
			}

	ok(over == 0 && tested == 1764, "gloss field bound: the sheen never passes HD_GLOSS_BOUND "
		"times the surface's own light plus HD_GLOSS_FLOOR, nor HD_GLOSS_CAP", over, 0);
	ok(hue == 0, "gloss field bound: the bound scales the sheen whole and keeps its hue", hue, 0);
	ok(small == 0, "gloss field bound: a sheen far under the bound passes within 2 per cent",
		small, 0);
	ok(white == 0, "gloss field bound: the screen never takes a channel past white", white, 0);
	ok(capped == 0, "gloss field bound: the screen closes no more than HD_GLOSS_CAP of the "
		"way to white", capped, 0);

	// A dark surface stays dark: nothing on black but HD_GLOSS_FLOOR.
	const float black[3] = { 0.0f, 0.0f, 0.0f };
	const float huge[3] = { 9.0f, 9.0f, 9.0f };
	float kept[3];

	HDGlossSheenBound(huge, black, kept);
	ok(kept[0] <= HD_GLOSS_FLOOR && kept[0] > 0.0f,
		"gloss field bound: a black surface takes no more than HD_GLOSS_FLOOR",
		(long)(kept[0] * 255.0f + 0.5f), (long)(HD_GLOSS_FLOOR * 255.0f + 0.5f));

	// And a bright one no more than the cap, whatever its own light allows.
	const float bright[3] = { 0.9f, 0.9f, 0.9f };

	HDGlossSheenBound(huge, bright, kept);
	ok(kept[0] <= HD_GLOSS_CAP && kept[0] > 0.9f * HD_GLOSS_CAP,
		"gloss field bound: a bright surface takes no more than HD_GLOSS_CAP",
		(long)(kept[0] * 100.0f + 0.5f), (long)(HD_GLOSS_CAP * 100.0f + 0.5f));
}

/* The faded engine light (specific\hd\hd_objlight_dark.h): which entries of
 * the engine's two item light lists fill_lights leaves out. The fade is
 * specific\lighting.cpp's own, statement for statement on the same floats:
 * the swap's fade-out of a previous light and the fade-in of a light that
 * comes in range (CreateLightList), the fade-out of a current light that is
 * out of range, and FadeLightList's step. Shadow lights fade an integer and
 * are not handed on, so they are not walked here. */
struct EngineLight
{
	float r, g, b, tr, tg, tb, rs, gs, bs;
	long fcnt;
	unsigned char Type, Active;
};

static EngineLight engine_light(float r, float g, float b, unsigned char type)
{
	EngineLight l;

	memset(&l, 0, sizeof(l));
	l.r = l.tr = r;
	l.g = l.tg = g;
	l.b = l.tb = b;
	l.Type = type;
	return l;
}

// CreateLightList: a previous light at a room change.
static void engine_swap_out(EngineLight& l)
{
	if (l.Active)
	{
		l.rs = l.r * -0.125F;
		l.gs = l.g * -0.125F;
		l.bs = l.b * -0.125F;
		l.tr = 0.0F;
		l.tg = 0.0F;
		l.tb = 0.0F;
		l.fcnt = 8;
	}
}

// CreateLightList: a current light that is in range.
static void engine_in_range(EngineLight& l)
{
	if (!l.Active)
	{
		l.rs = l.tr * 0.125F;
		l.gs = l.tg * 0.125F;
		l.bs = l.tb * 0.125F;
		l.r = 0;
		l.g = 0;
		l.b = 0;
		l.Active = 1;
		l.fcnt = 8;
	}
}

// CreateLightList: a current light that is out of range.
static void engine_out_of_range(EngineLight& l)
{
	if (l.Active && !l.fcnt)
	{
		l.rs = l.r * -0.125F;
		l.gs = l.g * -0.125F;
		l.bs = l.b * -0.125F;
		l.fcnt = 8;
	}
}

// FadeLightList, one light.
static void engine_fade(EngineLight& l)
{
	if (l.Active && l.fcnt)
	{
		l.r += l.rs;
		l.g += l.gs;
		l.b += l.bs;
		l.fcnt--;

		if (l.r <= 0 && l.g <= 0 && l.b <= 0)
			l.Active = 0;
	}
}

static bool engine_dark(const EngineLight& l)
{
	return HDObjLightIsDark(l.r, l.g, l.b) != 0;
}

/* fill_lights' choice over one list, without the conversion: an Active
 * sun, point or spot (types 0, 1 and 2) that is not dark. */
static int engine_handed(const EngineLight* list, int n)
{
	int handed = 0;

	for (int i = 0; i < n; i++)
		if (list[i].Active && list[i].Type <= 2 && !engine_dark(list[i]))
			handed++;

	return handed;
}

/* Fade a state out eight steps, by the swap or by the range test. `steps`
 * counts the steps before the last that came out dark, `left` whether the
 * light is still Active after the last and `leftLight` whether it is so
 * with a colour that is not dark. */
static void engine_fade_out(EngineLight l, bool swap, int& steps, int& left, int& leftLight)
{
	if (swap)
		engine_swap_out(l);
	else
		engine_out_of_range(l);

	for (int k = 1; k <= 8; k++)
	{
		engine_fade(l);

		if (k < 8 && l.Active && engine_dark(l))
			steps++;
	}

	if (l.Active)
	{
		left++;

		if (!engine_dark(l))
			leftLight++;
	}
}

static float float_of(unsigned int bits)
{
	float f;

	memcpy(&f, &bits, sizeof(f));
	return f;
}

static void objlight_dark()
{
	const float bound = HD_OBJLIGHT_DARK_BOUND;
	const float below = nextafterf(bound, 0.0f);
	const float above = nextafterf(bound, 1.0f);

	// The predicate at its edge.
	ok(HDObjLightIsDark(below, below, below),
		"objlight: just below the bound in all three channels is dark", 0, 1);
	ok(!HDObjLightIsDark(bound, bound, bound), "objlight: at the bound is light", 1, 0);
	ok(!HDObjLightIsDark(above, above, above), "objlight: above the bound is light", 1, 0);
	ok(!HDObjLightIsDark(above, below, below) && !HDObjLightIsDark(below, above, below) &&
		!HDObjLightIsDark(below, below, above),
		"objlight: one channel above with two below is light, in each channel", 1, 0);
	ok(HDObjLightIsDark(0.0f, 0.0f, 0.0f), "objlight: black is dark", 0, 1);
	ok(HDObjLightIsDark(-0.1f, -1e-9f, -0.5f), "objlight: negative remainders are dark", 0, 1);
	ok(HDObjLightIsDark(-1.49e-8f, 4.47e-8f, 0.0f),
		"objlight: a remainder of mixed sign is dark", 0, 1);
	ok(!HDObjLightIsDark(1.0f / 255.0f, 0.0f, 0.0f),
		"objlight: one unit of 0..255 in one channel is light", 1, 0);
	ok(!HDObjLightIsDark(NAN, 0.0f, 0.0f), "objlight: a NaN counts as light", 1, 0);

	/* The bound in the measured gap: above the largest remainder of a fade
	 * by a factor of a hundred, below the smallest step of a fade-out that
	 * begins one step into the fade-in of a light of 1/255. */
	ok(bound > 100.0f * 8.94e-8f && bound < (1.0f / 255.0f) / 64.0f,
		"objlight: the bound lies between 100 x 8.94e-8 and (1/255) / 64",
		(long)(bound * 1e9f), 10000);

	/* The fade over the byte grid, grey and red alone: the fade-in from 0,
	 * the fade-out from its end by the swap and by the range test, a swap
	 * at every step of the fade-in and at every step of a fade-out out of
	 * range, and the current list's second fade of a remainder. */
	int left = 0, leftLight = 0, steps = 0, inDark = 0, greyLeft = 0, refades = 0;
	int partLeft = 0, partLight = 0, partSteps = 0;

	for (int n = 1; n <= 255; n++)
		for (int shape = 0; shape < 2; shape++)
		{
			const float c = (float)n / 255.0f;
			EngineLight l = shape ? engine_light(c, 0.0f, 0.0f, 1) : engine_light(c, c, c, 0);
			EngineLight in[8];

			engine_in_range(l);

			for (int k = 0; k < 8; k++)
			{
				engine_fade(l);
				inDark += !l.Active || engine_dark(l) ? 1 : 0;
				in[k] = l;
			}

			// From the full colour, both ways out.
			const int before = left;

			engine_fade_out(in[7], true, steps, left, leftLight);
			greyLeft += shape == 0 && left > before ? 1 : 0;
			engine_fade_out(in[7], false, steps, left, leftLight);

			// A swap at every step of the fade-in.
			for (int k = 0; k < 7; k++)
			{
				EngineLight part = in[k];

				engine_fade_out(part, true, partSteps, partLeft, partLight);
			}

			// A swap at every step of a fade-out out of range, and the
			// remainder at its end faded again.
			EngineLight out = in[7];

			engine_out_of_range(out);

			for (int k = 1; k <= 8; k++)
			{
				engine_fade(out);

				if (!out.Active)
					break;

				int ignored = 0;

				engine_fade_out(out, true, k < 8 ? partSteps : ignored, partLeft, partLight);
			}

			int cycles = 0;

			while (out.Active && cycles < 16)
			{
				engine_out_of_range(out);

				for (int k = 0; k < 8; k++)
					engine_fade(out);

				cycles++;
			}

			refades += cycles > 1 ? 1 : 0;
		}

	ok(greyLeft == 139, "objlight: 139 of the 255 grey byte-grid colours stay Active after "
		"the swap's fade-out", greyLeft, 139);
	ok(left > 0 && leftLight == 0, "objlight: every light left Active by a fade-out from its "
		"full colour is dark", leftLight, 0);
	ok(partLeft > 0 && partLight == 0, "objlight: every light left Active by a fade-out from a "
		"part-way state is dark", partLight, 0);
	ok(inDark == 0, "objlight: no step of a fade-in from 1/255 up is dark", inDark, 0);
	ok(steps == 0, "objlight: no step of a fade-out from 1/255 up is dark but the last", steps, 0);
	ok(partSteps == 0, "objlight: nor of one that begins part-way, from 1/255 up", partSteps, 0);
	ok(refades == 0, "objlight: the current list's second fade clears every remainder",
		refades, 0);

	/* The lists. A list of nothing but remainders, a shadow light and a
	 * light that is not Active hands nothing on, and the eye is the lamp;
	 * one real light beside them hands one. The first entry is ANDREA2 room
	 * 9's sun (0.749969, 0.655856, 0.608799 in the level file) walked
	 * through its fade-in and the swap's fade-out: Active, 0 / 4.47e-8 /
	 * 1.49e-8. */
	EngineLight sun = engine_light(float_of(0x3f3ffe00u), float_of(0x3f27e629u),
		float_of(0x3f1bda3du), 0);

	engine_in_range(sun);

	for (int k = 0; k < 8; k++)
		engine_fade(sun);

	engine_swap_out(sun);

	for (int k = 0; k < 8; k++)
		engine_fade(sun);

	ok(sun.Active && sun.fcnt == 0 && engine_dark(sun),
		"objlight: ANDREA2 room 9's sun stays Active and dark after the swap's fade-out",
		sun.Active && engine_dark(sun) ? 1 : 0, 1);

	EngineLight list[5];

	list[0] = sun;
	list[1] = engine_light(2.0e-8f, -3.0e-8f, 1.0e-9f, 1);
	list[1].Active = 1;
	list[2] = engine_light(0.25f, 0.25f, 0.25f, 3);
	list[2].Active = 1;
	list[3] = engine_light(0.5f, 0.4f, 0.3f, 2);
	list[4] = engine_light(0.37f, 0.28f, 0.26f, 1);
	list[4].Active = 1;

	ok(engine_handed(list, 4) == 0,
		"objlight: a list of remainders, a shadow and an inactive light hands none on",
		engine_handed(list, 4), 0);
	ok(engine_handed(list, 5) == 1, "objlight: one real light beside them hands one on",
		engine_handed(list, 5), 1);
}

/* The soft motion of the chest (specific\hd\hd_softbody.h). */

static float sb_len(const float* v)
{
	return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static long sb_milli(double v)
{
	return (long)floor(v * 1000.0 + 0.5);
}

/* An anchor path: where both centres stand at time t. The right one rides 60
 * units to the side of the left. */
typedef void (*SbPath)(double t, float* a);

/* A run along a path at a frame rate: every frame samples the path, steps the
 * motion by 1 / fps and keeps the largest offset either side draws. With
 * `ticks` set, the path is sampled at the 30 game ticks a second and the
 * frames in between are blended linearly - which is what the game's pose is
 * at every frame rate above 30. */
static float sb_run(SbPath path, double fps, double seconds, int strength, float radius,
	bool ticks, int* resets)
{
	HDSoftSim s;

	HDSoftClear(&s);

	float peak = 0.0f;
	const int frames = (int)(seconds * fps + 0.5);

	for (int f = 0; f <= frames; f++)
	{
		const double t = f / fps;
		float a[2][3];

		if (ticks)
		{
			const double k = floor(t * 30.0 + 1e-9);
			const double w = t * 30.0 - k;
			float p0[3], p1[3];

			path(k / 30.0, p0);
			path((k + 1.0) / 30.0, p1);

			for (int i = 0; i < 3; i++)
				a[0][i] = (float)(p0[i] + (p1[i] - p0[i]) * w);
		}
		else
			path(t, a[0]);

		a[1][0] = a[0][0] + 60.0f;
		a[1][1] = a[0][1];
		a[1][2] = a[0][2];

		float out[2][3];

		HDSoftAdvance(&s, a, f ? (float)(1.0 / fps) : 0.0f, strength, radius, out);

		for (int k = 0; k < 2; k++)
			if (sb_len(out[k]) > peak)
				peak = sb_len(out[k]);
	}

	if (resets)
		*resets = s.resets;

	return peak;
}

/* A run on the spot that bobs at 2.2 Hz, a sprint from a standstill to 3000
 * units a second and back, and a fall that lands - the three things her torso
 * does most. y is down, as in the game. */
static void sb_path_play(double t, float* a)
{
	const double pi = 3.14159265358979323846;
	double x = 0.0, y = 0.0;

	if (t < 1.5)
		y = 6.0 * sin(2.0 * pi * 2.2 * t);

	if (t > 0.5)
		x = t < 1.2 ? 3000.0 * (t - 0.5) * (t - 0.5) / 1.4 : 1050.0;

	if (t > 1.6)
		y += t < 2.1 ? 2700.0 * (t - 1.6) * (t - 1.6) : 2700.0 * 0.25;

	a[0] = (float)x;
	a[1] = (float)y;
	a[2] = 0.0f;
}


static void sb_path_walk(double t, float* a)
{
	a[0] = (float)(2000.0 * t);
	a[1] = 0.0f;
	a[2] = (float)(-500.0 * t);
}

/* The spring against the closed form of a damped oscillator: an anchor that
 * starts moving at speed V leaves the point behind, and its travel is
 *
 *   x(t) = -V / wd * exp(-z w t) * sin(wd t)
 *
 * sampled at frames that are whole numbers of sub-steps, so nothing is carried. */
static void softbody_spring()
{
	const double pi = 3.14159265358979323846;
	const double w = 2.0 * pi * HD_SOFT_HZ;
	const double z = HD_SOFT_DAMPING;
	const double wd = w * sqrt(1.0 - z * z);
	const float V = 100.0f;
	const float dt = 8.0f * HD_SOFT_STEP;
	const float radius = 100.0f;
	HDSoftSim s;
	float a[2][3] = { { 0, 0, 0 }, { 60, 0, 0 } };
	float out[2][3];

	HDSoftClear(&s);
	HDSoftAdvance(&s, a, 0.0f, 2, radius, out);
	HDSoftAdvance(&s, a, dt, 2, radius, out);

	double worst = 0.0;
	double peak = 0.0;
	double afterRest = 0.0;
	int frames = 0, restAt = -1;

	for (int f = 1; f <= 360; f++)
	{
		a[0][2] = V * dt * (float)f;
		a[1][2] = a[0][2];

		const bool rest = HDSoftAdvance(&s, a, dt, 2, radius, out);
		const double t = (double)dt * f;
		const double want = -V / wd * exp(-z * w * t) * sin(wd * t);
		const double got = s.side[0].x[2];

		if (rest && restAt < 0)
			restAt = f;

		if (restAt < 0 && fabs(got - want) > worst)
			worst = fabs(got - want);

		if (restAt >= 0 && fabs(want) > afterRest)
			afterRest = fabs(want);

		if (fabs(want) > peak)
			peak = fabs(want);

		frames++;
	}

	ok(frames == 360 && worst < 1e-3 * peak,
		"softbody: the spring follows the damped oscillator's closed form to 0.1 per cent",
		sb_milli(worst / peak * 1000.0), 0);
	/* Until it snaps to rest, which it does only where the closed form itself
	 * no longer swings past REST_X. */
	ok(restAt > 0 && afterRest < HD_SOFT_REST_X,
		"softbody: the spring snaps to rest only once the true swing is under REST_X",
		sb_milli(afterRest), 0);
	ok(s.side[0].x[0] == 0.0f && s.side[0].x[1] == 0.0f,
		"softbody: a push along one axis moves the point along that axis alone",
		sb_milli(sb_len(s.side[0].x)), 0);

	/* The output: GAIN times the travel while it is small, never past BOUND
	 * times the radius however large. */
	const float small[3] = { 0.0f, 0.0f, 1.0f };
	const float huge[3] = { 1.0e6f, -1.0e6f, 3.0e5f };
	float o[3];

	HDSoftOutput(small, 2, 30.0f, o);
	ok(fabsf(o[2] - HD_SOFT_GAIN * HD_SOFT_NORMAL) < 1e-3f * HD_SOFT_GAIN * HD_SOFT_NORMAL,
		"softbody: a small travel is drawn at GAIN times the strength's scale",
		sb_milli(o[2]), sb_milli(HD_SOFT_GAIN * HD_SOFT_NORMAL));
	ok(HDSoftOutput(huge, 3, 30.0f, o) && sb_len(o) <= HD_SOFT_BOUND * HD_SOFT_STRONG * 30.0f &&
		sb_len(o) > 0.99f * HD_SOFT_BOUND * HD_SOFT_STRONG * 30.0f,
		"softbody: a huge travel is drawn just under the bound and reported as at it",
		sb_milli(sb_len(o)), sb_milli(HD_SOFT_BOUND * HD_SOFT_STRONG * 30.0f));
	ok(!HDSoftOutput(huge, 0, 30.0f, o) && sb_len(o) == 0.0f,
		"softbody: strength 0 draws nothing", sb_milli(sb_len(o)), 0);
	ok(HDSoftStrengthScale(1) == HD_SOFT_SUBTLE && HDSoftStrengthScale(2) == HD_SOFT_NORMAL &&
		HDSoftStrengthScale(3) == HD_SOFT_STRONG && HDSoftStrengthScale(4) == 0.0f &&
		HDSoftStrengthScale(-1) == 0.0f,
		"softbody: the strengths are subtle, normal, strong and nothing else", 0, 0);
}

/* R3: the same path at 30, 60 and 165 frames a second gives the same peak to
 * within 5 per cent - blended between ticks as the game draws it, and
 * sampled directly as a smooth path. */
static void softbody_rate()
{
	for (int blend = 1; blend >= 0; blend--)
	{
		const float p30 = sb_run(sb_path_play, 30.0, 3.0, 2, 30.0f, blend != 0, 0);
		const float p60 = sb_run(sb_path_play, 60.0, 3.0, 2, 30.0f, blend != 0, 0);
		const float p165 = sb_run(sb_path_play, 165.0, 3.0, 2, 30.0f, blend != 0, 0);
		const float lo = p30 < p60 ? (p30 < p165 ? p30 : p165) : (p60 < p165 ? p60 : p165);
		const float hi = p30 > p60 ? (p30 > p165 ? p30 : p165) : (p60 > p165 ? p60 : p165);

		printf("  softbody: peak at 30 / 60 / 165 fps, %s: %.4f / %.4f / %.4f units\n",
			blend ? "blended between ticks" : "smooth", p30, p60, p165);

		ok(lo > 0.0f && hi <= 1.05f * lo,
			blend ? "softbody: the peak is the same at 30, 60 and 165 fps (blended path)"
				: "softbody: the peak is the same at 30, 60 and 165 fps (smooth path)",
			sb_milli((hi / lo - 1.0f) * 100.0f), 0);
	}

	// And the run on the spot alone, which stays below the bound throughout.
	const float bob30 = sb_run([](double t, float* a) {
		a[0] = 0.0f; a[1] = (float)(6.0 * sin(2.0 * 3.14159265358979323846 * 2.2 * t)); a[2] = 0.0f; },
		30.0, 3.0, 2, 30.0f, true, 0);
	const float bob165 = sb_run([](double t, float* a) {
		a[0] = 0.0f; a[1] = (float)(6.0 * sin(2.0 * 3.14159265358979323846 * 2.2 * t)); a[2] = 0.0f; },
		165.0, 3.0, 2, 30.0f, true, 0);

	ok(bob30 > 0.0f && fabsf(bob165 - bob30) <= 0.05f * bob30,
		"softbody: a run on the spot peaks the same at 30 and 165 fps",
		sb_milli(bob165), sb_milli(bob30));
}

/* R4: bounded. Wild anchors, zero, negative, tiny and huge time steps, NaN
 * and infinity - the drawn offset stays finite and within the bound at every
 * strength, and so does the state. */
static void softbody_bound()
{
	unsigned int seed = 12345u;
	const auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed; };
	const float dts[] = { -0.1f, 0.0f, 1e-7f, 1.0f / 30.0f, 1.0f / 165.0f, HD_SOFT_GAP,
		0.3f, 5.0f };
	int beyond = 0, nonFinite = 0, frames = 0, stateBad = 0;

	for (int strength = 0; strength < HD_SOFT_LEVELS; strength++)
	{
		HDSoftSim s;
		float a[2][3] = { { 0, 0, 0 }, { 60, 0, 0 } };

		HDSoftClear(&s);

		for (int f = 0; f < 20000; f++)
		{
			const unsigned int r = next();
			float dt = dts[r % 8];

			if ((r >> 3) % 4 != 0)
				dt = 1.0f / 60.0f;

			/* Mostly within the teleport bound, so that the spring is driven
			 * hard rather than reset: a jump of up to 120 units a frame is
			 * 7200 units a second. */
			for (int k = 0; k < 2; k++)
				for (int i = 0; i < 3; i++)
					a[k][i] += (float)((int)(next() % 241) - 120);

			if ((r >> 8) % 997 == 0)
				a[0][1] = NAN;
			else if ((r >> 8) % 991 == 0)
				a[1][2] = INFINITY;
			else if ((r >> 8) % 983 == 0)
				a[0][0] += 1.0e7f;

			float out[2][3];

			HDSoftAdvance(&s, a, dt, strength, 30.0f, out);

			for (int k = 0; k < 2; k++)
			{
				const float m = sb_len(out[k]);

				if (!(m == m) || m > 1e30f)
					nonFinite++;
				else if (m > HD_SOFT_BOUND * HDSoftStrengthScale(strength) * 30.0f * 1.00001f)
					beyond++;

				if (!(sb_len(s.side[k].x) == sb_len(s.side[k].x)) ||
					!(sb_len(s.side[k].u) == sb_len(s.side[k].u)))
					stateBad++;

				for (int i = 0; i < 3; i++)
					if (!(a[k][i] == a[k][i]) || fabsf(a[k][i]) > 1e8f)
						a[k][i] = 0.0f;
			}

			frames++;
		}
	}

	ok(frames == 80000 && nonFinite == 0, "softbody: no drawn offset is ever NaN or infinite",
		nonFinite, 0);
	ok(beyond == 0, "softbody: no drawn offset ever passes the bound, at any strength",
		beyond, 0);
	ok(stateBad == 0, "softbody: the state stays a number under any input", stateBad, 0);

	/* A resonant shake at the spring's own frequency, as hard as the teleport
	 * bound allows, for ten seconds. */
	HDSoftSim s;
	float peak = 0.0f;

	HDSoftClear(&s);

	for (int f = 0; f <= 600; f++)
	{
		const double t = f / 60.0;
		const float y = (float)(300.0 * sin(2.0 * 3.14159265358979323846 * HD_SOFT_HZ * t));
		float a[2][3] = { { 0, y, 0 }, { 60, y, 0 } };
		float out[2][3];

		HDSoftAdvance(&s, a, f ? 1.0f / 60.0f : 0.0f, 3, 30.0f, out);
		peak = sb_len(out[0]) > peak ? sb_len(out[0]) : peak;
	}

	ok(peak <= HD_SOFT_BOUND * HD_SOFT_STRONG * 30.0f && s.clamped > 0 && s.resets == 0,
		"softbody: a resonant shake runs into the bound, is counted there and never passes it",
		sb_milli(peak), sb_milli(HD_SOFT_BOUND * HD_SOFT_STRONG * 30.0f));
}

/* R5: back to rest without a kick. An anchor that moves at a constant speed
 * drives nothing, so every one of these must leave the drawn offset exactly
 * zero from the reset on. */
static void softbody_reset()
{
	int resets = 0;
	const float walk = sb_run(sb_path_walk, 60.0, 2.0, 3, 30.0f, false, &resets);

	ok(walk == 0.0f && resets == 0,
		"softbody: starting while she walks at a steady 2060 units a second does not kick",
		sb_milli(walk), 0);

	HDSoftSim s;
	float out[2][3];
	float a[2][3] = { { 0, 0, 0 }, { 60, 0, 0 } };
	const float dt = 1.0f / 60.0f;
	int kicked = 0;

	HDSoftClear(&s);

	/* Excite it with a stop, then reset it while she keeps walking: a level
	 * start, a savegame, an outfit or an option change all come down to this
	 * call in the engine. */
	for (int f = 0; f < 240; f++)
	{
		const float v = f < 60 ? 2000.0f : 0.0f;

		a[0][0] += v * (f ? dt : 0.0f);
		a[1][0] = a[0][0] + 60.0f;

		if (f == 120)
			HDSoftReset(&s);

		HDSoftAdvance(&s, a, f ? dt : 0.0f, 2, 30.0f, out);

		if (f >= 120 && (sb_len(out[0]) != 0.0f || sb_len(out[1]) != 0.0f))
			kicked++;
	}

	ok(kicked == 0, "softbody: a reset leaves her at rest, with nothing carried over",
		kicked, 0);

	/* A teleport: the anchor jumps 50000 units between two frames while she
	 * walks - a cutscene's first frame. */
	HDSoftClear(&s);
	kicked = 0;

	for (int f = 0; f < 120; f++)
	{
		a[0][0] = 2000.0f * dt * f + (f >= 60 ? 50000.0f : 0.0f);
		a[1][0] = a[0][0] + 60.0f;
		HDSoftAdvance(&s, a, f ? dt : 0.0f, 2, 30.0f, out);

		if (sb_len(out[0]) != 0.0f || sb_len(out[1]) != 0.0f)
			kicked++;
	}

	ok(kicked == 0 && s.resets == 1,
		"softbody: a jump of the anchor resets once, without a kick", kicked + s.resets * 1000,
		1000);

	/* A gap: a frame half a second after the last - a hitch, a load. */
	HDSoftClear(&s);
	kicked = 0;

	for (int f = 0; f < 120; f++)
	{
		a[0][0] = 2000.0f * dt * f;
		a[1][0] = a[0][0] + 60.0f;
		HDSoftAdvance(&s, a, f == 60 ? 0.5f : (f ? dt : 0.0f), 2, 30.0f, out);

		if (sb_len(out[0]) != 0.0f || sb_len(out[1]) != 0.0f)
			kicked++;
	}

	ok(kicked == 0 && s.resets == 1, "softbody: a gap beyond GAP resets once, without a kick",
		kicked + s.resets * 1000, 1000);

	/* And a reset in the middle of a swing ends the swing on the spot. */
	HDSoftClear(&s);

	for (int f = 0; f < 30; f++)
	{
		a[0][1] = f < 15 ? 100.0f * dt * f : 100.0f * dt * 15;
		a[1][1] = a[0][1];
		HDSoftAdvance(&s, a, f ? dt : 0.0f, 2, 30.0f, out);
	}

	const float swinging = sb_len(out[0]);

	a[0][1] += 40000.0f;
	a[1][1] = a[0][1];
	HDSoftAdvance(&s, a, dt, 2, 30.0f, out);

	ok(swinging > 0.0f && sb_len(out[0]) == 0.0f && sb_len(out[1]) == 0.0f,
		"softbody: a jump in the middle of a swing ends it at once", sb_milli(sb_len(out[0])), 0);
}

/* R6: at rest is exactly at rest - offsets and state exactly zero, and the
 * draw constants of the offset exactly zero, which the shader adds as
 * nothing. R2: a frame that does not advance the game does not move the
 * state by a bit. */
static void softbody_rest()
{
	HDSoftSim s;
	float a[2][3] = { { 0, 0, 0 }, { 60, 0, 0 } };
	float out[2][3];
	const float dt = 1.0f / 60.0f;
	bool rest = false;
	int settled = -1;

	HDSoftClear(&s);

	for (int f = 0; f < 600; f++)
	{
		if (f < 20)
			a[0][1] = a[1][1] = 1500.0f * dt * f;

		rest = HDSoftAdvance(&s, a, f ? dt : 0.0f, 3, 30.0f, out);

		if (rest && settled < 0 && f > 20)
			settled = f;
	}

	bool zero = true;

	for (int k = 0; k < 2; k++)
		for (int i = 0; i < 3; i++)
			if (out[k][i] != 0.0f || s.side[k].x[i] != 0.0f || s.side[k].u[i] != 0.0f)
				zero = false;

	printf("  softbody: a landing at 1500 units a second comes to rest %d frames (60 fps) "
		"after it\n", settled - 20);

	ok(rest && zero && settled > 20 && settled < 20 + 600,
		"softbody: after a landing she comes to rest, and at rest everything is exactly zero",
		settled, 0);

	/* Paused: the same frame drawn again, and a frame that is not later. */
	HDSoftClear(&s);

	for (int f = 0; f < 40; f++)
	{
		a[0][1] = a[1][1] = f < 20 ? 1500.0f * dt * f : 1500.0f * dt * 20;
		HDSoftAdvance(&s, a, f ? dt : 0.0f, 2, 30.0f, out);
	}

	const HDSoftSim before = s;
	float first[2][3];

	memcpy(first, out, sizeof(first));

	for (int f = 0; f < 50; f++)
	{
		a[0][0] += 5.0f;	// the anchor moves while the time does not: still nothing
		HDSoftAdvance(&s, a, f % 2 ? 0.0f : -0.01f, 2, 30.0f, out);
	}

	ok(memcmp(&before, &s, sizeof(s)) == 0 && memcmp(first, out, sizeof(first)) == 0,
		"softbody: frames that do not advance the game leave the state and the offset alone",
		0, 0);

	/* The constants of an offset of zero carry exact zeros in the offset
	 * registers. */
	HDSoftRegion region = HDSoftRegion();
	const float zeros[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
	const float view[16] = { 0.6f, 0.8f, 0, 0, -0.8f, 0.6f, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1 };
	float c[16];

	region.ok = 1;
	region.radius = 30.0f;
	HDSoftConstants(&region, zeros, view, 7, c);

	ok(c[8] == 0.0f && c[9] == 0.0f && c[10] == 0.0f && c[12] == 0.0f && c[13] == 0.0f &&
		c[14] == 0.0f && c[11] == 7.0f,
		"softbody: at rest the offset registers are exactly zero", 0, 0);
}

/* R1 and R2, the clock: one step per drawn frame, never from a mirror's draw,
 * game time with half ticks at sixty and the counter's wrap, nothing while
 * the game stands still. */
static void softbody_clock()
{
	HDSoftClock c = { 0, 0, 0, 0.0f };
	float dt = 0.0f;
	int steps = 0;

	HDSoftClockFrame(&c);

	const bool mirrorStepped = HDSoftClockStep(&c, true, 100, 1.0f, &dt);	// the mirror first

	steps += HDSoftClockStep(&c, false, 100, 1.0f, &dt) ? 1 : 0;	// her own draw
	ok(!mirrorStepped && steps == 1 && dt == 0.0f,
		"softbody: a mirror's draw never steps; the first draw starts the clock",
		mirrorStepped ? 1 : 0, 0);

	steps += HDSoftClockStep(&c, false, 100, 1.0f, &dt) ? 1 : 0;	// drawn twice
	steps += HDSoftClockStep(&c, true, 100, 1.0f, &dt) ? 1 : 0;
	ok(steps == 1, "softbody: one step per frame, however often she is drawn", steps, 1);

	HDSoftClockFrame(&c);
	HDSoftClockStep(&c, false, 101, 0.5f, &dt);
	ok(fabsf(dt - 1.0f / 60.0f) < 1e-6f, "softbody: a frame half a tick on is 1/60 s",
		sb_milli(dt * 60.0f), 1000);

	HDSoftClockFrame(&c);
	HDSoftClockStep(&c, false, 101, 1.0f, &dt);
	ok(fabsf(dt - 1.0f / 60.0f) < 1e-6f, "softbody: and the tick after it 1/60 s more",
		sb_milli(dt * 60.0f), 1000);

	HDSoftClockFrame(&c);
	HDSoftClockStep(&c, false, 101, 1.0f, &dt);
	ok(dt == 0.0f, "softbody: the game standing still - the same tick again - is no time",
		sb_milli(dt), 0);

	HDSoftClockFrame(&c);
	HDSoftClockStep(&c, false, 101, 0.5f, &dt);
	ok(dt == 0.0f && c.tick == 101 && c.alpha == 1.0f,
		"softbody: a frame that is not later is no time and does not move the clock",
		sb_milli(dt), 0);

	c.tick = 65535;
	c.alpha = 1.0f;
	HDSoftClockFrame(&c);
	HDSoftClockStep(&c, false, 0, 1.0f, &dt);
	ok(fabsf(dt - 1.0f / 30.0f) < 1e-6f, "softbody: the tick counter's wrap is one tick",
		sb_milli(dt * 30.0f), 1000);

	HDSoftClockForget(&c);
	HDSoftClockFrame(&c);
	HDSoftClockStep(&c, false, 9000, 0.5f, &dt);
	ok(dt == 0.0f && c.tick == 9000, "softbody: a forgotten clock starts again at no time",
		sb_milli(dt), 0);
}

/* A synthetic body for the finder: hips at the origin, head 250 up (y is
 * down), thighs 20 to either side, feet pointing `toes` along z, and a torso
 * that is an elliptical tube from the waist to the neck with up to two bumps
 * on its front. `collar` leans the whole front forward above 120 up, that
 * many units for every 120 higher, the way a collar or a helmet's rim keeps
 * coming forward over the band. */
static void sb_body(std::vector<HDSoftVertex>& v, HDSoftSkeleton& sk, float left, float right,
	float toes, float ridge = 0.0f, float collar = 0.0f)
{
	const float sk0[7][3] =
	{
		{ 0, 0, 0 }, { 0, -250, 0 }, { -20, 10, 0 }, { 20, 10, 0 }, { 0, -70, 0 },
		{ -20, 400, 0 }, { 20, 400, 0 }
	};

	memcpy(&sk, sk0, sizeof(sk));
	v.clear();

	// The feet: under the ankles, reaching `toes` units along z.
	for (int side = 0; side < 2; side++)
		for (int i = 0; i < 20; i++)
		{
			HDSoftVertex p = { side ? 20.0f : -20.0f, 410.0f + (float)(i % 3) * 5.0f,
				toes * (float)i / 19.0f - toes * 0.2f, 0.0f, side + 1 };

			v.push_back(p);
		}

	// The torso, its front +z.
	for (float y = -40.0f; y >= -240.0f; y -= 4.0f)
		for (int k = 0; k < 90; k++)
		{
			const float ang = (float)k * 6.2831853f / 90.0f;
			const float x = 55.0f * cosf(ang);
			float z = 35.0f * sinf(ang);

			if (z > 0.0f)
			{
				const float dl = (x + 28.0f) * (x + 28.0f) + (y + 150.0f) * (y + 150.0f);
				const float dr = (x - 28.0f) * (x - 28.0f) + (y + 150.0f) * (y + 150.0f);

				z += left * expf(-dl / (2.0f * 13.0f * 13.0f)) +
					right * expf(-dr / (2.0f * 13.0f * 13.0f));

				/* A ridge on her right: the bump's own profile across, the
				 * same at every height - as far forward as a chest and
				 * standing out of nothing below it. */
				z += ridge * expf(-(x - 28.0f) * (x - 28.0f) / (2.0f * 13.0f * 13.0f));

				if (-y > 120.0f)
					z += collar * (-y - 120.0f) / 120.0f;
			}

			HDSoftVertex p = { x, y, z, 1.0f, 0 };

			v.push_back(p);
		}
}

static void softbody_region()
{
	std::vector<HDSoftVertex> v;
	HDSoftSkeleton sk;
	HDSoftRegion r;

	sb_body(v, sk, 16.0f, 16.0f, 40.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);

	printf("  softbody: synthetic chest: %s, apexes (%.1f, %.1f, %.1f) / (%.1f, %.1f, %.1f), "
		"radius %.1f, %d/%d inside\n", r.reason, r.apex[0][0], r.apex[0][1], r.apex[0][2],
		r.apex[1][0], r.apex[1][1], r.apex[1][2], r.radius, r.inside[0], r.inside[1]);

	ok(r.ok == 1, "softbody: two bumps on a torso are found", r.ok, 1);
	/* On the bumps - within their own width of 13 of the bump's middle; the
	 * tube's own front leans the peak a little towards the middle. */
	ok(fabsf(r.apex[0][0] + 28.0f) < 8.0f && fabsf(r.apex[1][0] - 28.0f) < 8.0f &&
		fabsf(r.apex[0][1] + 150.0f) < 4.0f && fabsf(r.apex[1][1] + 150.0f) < 4.0f,
		"softbody: the apexes stand on the bumps, the left one on her left",
		sb_milli(r.apex[0][0]), -28000);
	ok(fabsf(r.apex[0][0] + r.apex[1][0]) < 0.5f && fabsf(r.apex[0][1] - r.apex[1][1]) < 0.5f &&
		fabsf(r.apex[0][2] - r.apex[1][2]) < 0.5f,
		"softbody: the two apexes are mirror images", sb_milli(r.apex[0][0] + r.apex[1][0]), 0);
	ok(fabsf(r.radius - 0.5f * (r.apex[1][0] - r.apex[0][0])) < 1e-3f &&
		fabsf(r.centre[0][2] - (r.apex[0][2] - HD_SOFT_SETBACK * r.radius)) < 1e-3f,
		"softbody: the regions meet in the middle and the centres stand behind the apexes",
		sb_milli(r.radius), 0);
	ok(r.fwd[2] > 0.99f && r.up[1] < -0.99f && r.side[0] > 0.99f,
		"softbody: front, up and her right are read from the feet and the joints",
		sb_milli(r.fwd[2]), 1000);

	// The same body with its feet the other way round: its front is behind.
	sb_body(v, sk, 16.0f, 16.0f, -40.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(r.fwd[2] < -0.99f && strcmp(r.reason, "ok") != 0,
		"softbody: feet pointing back turn the front round, and the back has no chest",
		sb_milli(r.fwd[2]), -1000);

	sb_body(v, sk, 0.0f, 0.0f, 40.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "flat"), "softbody: a front with no chest is refused as flat",
		r.ok, 0);

	sb_body(v, sk, 16.0f, 0.0f, 40.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "asymmetric"),
		"softbody: a front with one bump is refused as asymmetric", r.ok, 0);

	// A chest on her left and a ridge just as far forward on her right: the
	// apexes are mirror images, and only the right one stands out of nothing.
	sb_body(v, sk, 16.0f, 0.0f, 40.0f, 16.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "asymmetric"),
		"softbody: a chest on one side and a ridge on the other is refused as asymmetric",
		r.ok, 0);

	sb_body(v, sk, 16.0f, 16.0f, 0.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "no_front"), "softbody: feet that point nowhere give no front",
		r.ok, 0);

	// No torso weights at all.
	sb_body(v, sk, 16.0f, 16.0f, 40.0f);

	for (size_t i = 0; i < v.size(); i++)
		v[i].torso = 0.4f;

	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "no_torso"),
		"softbody: a body whose torso weights stay under a half has no torso", r.ok, 0);

	/* A chest under a front that keeps leaning forward above the band: the
	 * band's frontmost vertex sits on its top edge, which is the band cutting
	 * through a collar and not a peak. */
	sb_body(v, sk, 16.0f, 16.0f, 40.0f, 0.0f, 60.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "band_edge"),
		"softbody: a front still coming forward at the band's top is refused at the band's edge",
		r.ok, 0);

	/* The region reaches back to the chest wall: the ordinary chest moves as
	 * a whole, a chest standing far further forward than the regions' radius
	 * would move at its tip alone. */
	sb_body(v, sk, 16.0f, 16.0f, 40.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(r.ok == 1 && r.wall >= HD_SOFT_WALL,
		"softbody: an ordinary chest's region reaches its wall", sb_milli(r.wall), 1000);

	sb_body(v, sk, 60.0f, 60.0f, 40.0f);
	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	printf("  softbody: deep synthetic chest: %s, radius %.1f, mask at the wall %.3f\n",
		r.reason, r.radius, r.wall);
	ok(!r.ok && !strcmp(r.reason, "deep") && r.wall < HD_SOFT_WALL,
		"softbody: a chest deeper than its region reaches is refused as deep",
		sb_milli(r.wall), 0);

	// Nothing of the torso at the collarbones: no wall to show the reach.
	sb_body(v, sk, 16.0f, 16.0f, 40.0f);

	for (size_t i = 0; i < v.size(); i++)
		if (v[i].leg == 0 && -v[i].y > 170.0f)
			v[i].torso = 0.4f;

	HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);
	ok(!r.ok && !strcmp(r.reason, "deep") && r.wall == 0.0f,
		"softbody: a chest with no torso at its collarbones is refused as deep",
		sb_milli(r.wall), 0);

	/* From the ordinary chest to the deep one: the mask at the wall only
	 * falls, and the refusal comes exactly where it falls under HD_SOFT_WALL. */
	bool rule = true, falls = true, both = false, none = true;
	float last = 2.0f;

	for (float bump = 16.0f; bump <= 60.0f; bump += 2.0f)
	{
		sb_body(v, sk, bump, bump, 40.0f);
		HDSoftFindRegion(&v[0], (int)v.size(), &sk, &r);

		const bool deep = !strcmp(r.reason, "deep");

		if (!r.ok && !deep)
		{
			none = false;
			continue;
		}

		rule = rule && deep == (r.wall < HD_SOFT_WALL);
		falls = falls && r.wall <= last + 1e-4f;
		last = r.wall;
		both = both || deep;
	}

	ok(rule && falls && both && none,
		"softbody: growing chests lose the wall step by step and are refused where the mask "
		"falls under the limit", (rule ? 1 : 0) + (falls ? 2 : 0) + (both ? 4 : 0) +
		(none ? 8 : 0), 15);
}

/* The mask against the pairs hd_softbody.h documents beside HDSoftMask, which
 * the shader carries line for line: 1 at the centre, 0 at the radius and
 * beyond, monotone between. */
static void softbody_mask()
{
	const float c[3] = { 10.0f, -20.0f, 30.0f };
	const float pairs[][2] = { { 0.0f, 1.0f }, { 0.25f, 0.84375f }, { 0.5f, 0.5f },
		{ 0.75f, 0.15625f }, { 1.0f, 0.0f }, { 1.5f, 0.0f }, { 7.0f, 0.0f } };
	int wrong = 0;

	for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
	{
		const float p[3] = { c[0], c[1] + pairs[i][0] * 40.0f, c[2] };

		if (fabsf(HDSoftMask(p, c, 1.0f / 40.0f) - pairs[i][1]) > 1e-6f)
			wrong++;
	}

	ok(wrong == 0, "softbody: the mask meets its documented pairs", wrong, 0);

	int rising = 0;
	float last = 2.0f;

	for (int i = 0; i <= 1000; i++)
	{
		const float p[3] = { c[0] + 0.05f * i * 0.6f, c[1], c[2] + 0.05f * i * 0.8f };
		const float m = HDSoftMask(p, c, 1.0f / 40.0f);

		if (m > last || m < 0.0f || m > 1.0f)
			rising++;

		last = m;
	}

	ok(rising == 0, "softbody: the mask falls monotonely from 1 to 0 in any direction",
		rising, 0);

	const float p[3] = { 500.0f, 0.0f, 0.0f };

	ok(HDSoftMask(p, c, 0.0f) == 1.0f,
		"softbody: an inverse radius of 0 masks everything in - why no region sends zeros",
		0, 0);
}

/* The constants of a draw: the layout, the offset turned with the torso's
 * own view matrix, a mirror's reflected draw giving a reflected offset, and
 * the camera changing the direction the offset is written in but neither its
 * size nor the motion behind it (R7). */
static void softbody_constants()
{
	HDSoftRegion region = HDSoftRegion();

	region.ok = 1;
	region.radius = 25.0f;
	region.centre[0][0] = -28.0f;
	region.centre[0][1] = -150.0f;
	region.centre[0][2] = 60.0f;
	region.centre[1][0] = 28.0f;
	region.centre[1][1] = -150.0f;
	region.centre[1][2] = 60.0f;

	const float off[2][3] = { { 1.0f, -2.0f, 0.5f }, { -1.0f, -2.0f, 0.5f } };
	const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1 };
	float c[16];

	HDSoftConstants(&region, off, identity, 7, c);
	ok(c[0] == -28.0f && c[2] == 60.0f && c[3] == 1.0f / 25.0f && c[4] == 28.0f &&
		c[7] == 1.0f / 25.0f && c[8] == 1.0f && c[9] == -2.0f && c[10] == 0.5f &&
		c[11] == 7.0f && c[12] == -1.0f && c[15] == 0.0f,
		"softbody: the four registers carry centre, 1 / radius, offset and the torso node",
		sb_milli(c[3] * 25.0f), 1000);

	// A mirror's draw: the reflection in the view's own z plane.
	const float mirror[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 5, 6, 7, 1 };

	HDSoftConstants(&region, off, mirror, 7, c);
	ok(c[8] == 1.0f && c[9] == -2.0f && c[10] == -0.5f,
		"softbody: a mirror's draw gets the offset reflected", sb_milli(c[10]), -500);

	// Two cameras: the same offset, the same length, the one turned into the other.
	const float yaw = 0.7f;
	const float cam[16] = { cosf(yaw), 0, sinf(yaw), 0, 0, 1, 0, 0, -sinf(yaw), 0, cosf(yaw), 0,
		0, 0, 0, 1 };
	float d[16];

	HDSoftConstants(&region, off, identity, 7, c);
	HDSoftConstants(&region, off, cam, 7, d);
	ok(fabsf(sb_len(&c[8]) - sb_len(&d[8])) < 1e-5f &&
		fabsf(d[8] - (cosf(yaw) * c[8] - sinf(yaw) * c[10])) < 1e-5f,
		"softbody: a camera turns the offset with it and does not change its size",
		sb_milli(sb_len(&d[8])), sb_milli(sb_len(&c[8])));

	HDSoftRegion bad = region;

	bad.ok = 0;
	HDSoftConstants(&bad, off, identity, 7, c);

	int nonZero = 0;

	for (int i = 0; i < 16; i++)
		nonZero += c[i] != 0.0f ? 1 : 0;

	ok(nonZero == 0, "softbody: a refused region sends sixteen zeros", nonZero, 0);

	/* The anchor and the torso's own axes: a joint turned a quarter round y
	 * carries a centre and takes a world offset back into its own frame, the
	 * front part weakened. */
	const float joint[12] = { 0, 0, 1, 10, 0, 1, 0, 20, -1, 0, 0, 30 };
	const float origin[3] = { 1000.0f, 2000.0f, 3000.0f };
	const float centre[3] = { 4.0f, -150.0f, 60.0f };
	const float bind[3] = { 0.0f, -70.0f, 0.0f };
	float anchor[3];

	HDSoftAnchor(joint, origin, centre, bind, anchor);
	ok(fabsf(anchor[0] - (1000.0f + 10.0f + 60.0f)) < 1e-3f &&
		fabsf(anchor[1] - (2000.0f + 20.0f - 80.0f)) < 1e-3f &&
		fabsf(anchor[2] - (3000.0f + 30.0f - 4.0f)) < 1e-3f,
		"softbody: the anchor is the centre carried by the torso joint into the world",
		sb_milli(anchor[0]), 1070000);

	const float fwd[3] = { 0.0f, 0.0f, 1.0f };
	const float world[3] = { 10.0f, 2.0f, -3.0f };
	float torso[3];

	HDSoftToTorso(joint, fwd, world, torso);
	ok(fabsf(torso[0] - 3.0f) < 1e-5f && fabsf(torso[1] - 2.0f) < 1e-5f &&
		fabsf(torso[2] - 10.0f * HD_SOFT_FRONTBACK) < 1e-5f,
		"softbody: a world offset comes into the torso's axes with its front part weakened",
		sb_milli(torso[2]), sb_milli(10.0f * HD_SOFT_FRONTBACK));
}

/* ------------------------------------------------- the projected shadow -- */

static long sh_milli(double f)
{
	return (long)floor(f * 1000.0 + 0.5);
}

static double sh_angle(const float* a, const float* b)
{
	double d = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
	const double la = sqrt((double)a[0] * a[0] + (double)a[1] * a[1] + (double)a[2] * a[2]);
	const double lb = sqrt((double)b[0] * b[0] + (double)b[1] * b[1] + (double)b[2] * b[2]);

	d /= la * lb;
	d = d > 1.0 ? 1.0 : (d < -1.0 ? -1.0 : d);
	return acos(d) * 180.0 / 3.14159265358979323846;
}

static bool sh_unit_finite(const float* d)
{
	for (int i = 0; i < 3; i++)
		if (!(d[i] == d[i]) || d[i] > 2.0f || d[i] < -2.0f)
			return false;

	return fabs(sqrt((double)d[0] * d[0] + (double)d[1] * d[1] + (double)d[2] * d[2]) - 1.0) < 1e-5;
}

static HDShadowLight sh_sun(float x, float y, float z, float c)
{
	HDShadowLight l;

	l.kind = HD_SHADOW_SUN;
	l.x = x; l.y = y; l.z = z;
	l.r = l.g = l.b = c;
	l.range = 0.0f;
	return l;
}

static HDShadowLight sh_lamp(int kind, float x, float y, float z, float c, float range)
{
	HDShadowLight l;

	l.kind = kind;
	l.x = x; l.y = y; l.z = z;
	l.r = l.g = l.b = c;
	l.range = range;
	return l;
}

/* The key light at the lights' full part, pull 1: one sun, one lamp, two
 * that cancel, none; continuity in P; the slant limit; nothing that is not a
 * number. */
static void shadow_key()
{
	const float P[3] = { 30000.0f, -2000.0f, 41000.0f };
	float fb[3];
	HDShadowKey k;

	HDShadowFallback(fb);
	ok(fabs(HDShadowSlant(fb) - HD_SHADOW_FALLBACK_SLANT) < 1e-3 && fb[1] > 0.0f,
		"shadow: the fixed slant stands FALLBACK_SLANT off straight down",
		sh_milli(HDShadowSlant(fb)), sh_milli(HD_SHADOW_FALLBACK_SLANT));

	/* The model shader's falloff, max(0, 1 - d / range), at the documented
	 * pairs. */
	ok(HDShadowFalloff(0.0f, 1000.0f) == 1.0f && HDShadowFalloff(250.0f, 1000.0f) == 0.75f &&
		HDShadowFalloff(500.0f, 1000.0f) == 0.5f && HDShadowFalloff(1000.0f, 1000.0f) == 0.0f &&
		HDShadowFalloff(1500.0f, 1000.0f) == 0.0f && HDShadowFalloff(10.0f, 0.0f) == 0.0f,
		"shadow: the falloff is the model shader's 1 - d / range, 0 past it",
		sh_milli(HDShadowFalloff(250.0f, 1000.0f)), 750);

	// One white sun: its own direction with the fixed slant's weight beside it.
	{
		const HDShadowLight sun = sh_sun(0.3f, 1.0f, 0.2f, 1.0f);
		float want[3] = { 0.3f, 1.0f, 0.2f };
		const float l = sqrtf(0.3f * 0.3f + 1.0f + 0.2f * 0.2f);

		for (int i = 0; i < 3; i++)
			want[i] = want[i] / l + HD_SHADOW_W_FALLBACK * fb[i];

		hd_shadow_unit(want);
		HDShadowKeyTarget(&sun, 1, P, &k, 1.0f);

		const float sunDir[3] = { 0.3f, 1.0f, 0.2f };

		ok(sh_angle(k.dir, want) < 1e-3 && sh_angle(k.dir, sunDir) < sh_angle(k.dir, fb),
			"shadow: one sun gives its own direction, plus the fixed slant's weight",
			sh_milli(sh_angle(k.dir, sunDir)), 0);
		// Short of DARK by the AGREE_EPS under the agreement alone.
		ok(fabs(k.strength - HD_SHADOW_DARK) < 1e-4 && k.lights == 1,
			"shadow: one bright sun gives the full darkness", sh_milli(k.strength),
			sh_milli(HD_SHADOW_DARK));
	}

	// One lamp above and to the left: the shadow is thrown away from it.
	{
		const HDShadowLight lamp = sh_lamp(HD_SHADOW_POINT, P[0] - 500.0f, P[1] - 800.0f,
			P[2], 1.0f, 4000.0f);
		const float away[3] = { 500.0f, 800.0f, 0.0f };

		HDShadowKeyTarget(&lamp, 1, P, &k, 1.0f);
		ok(k.dir[0] > 0.0f && sh_angle(k.dir, away) < 8.0,
			"shadow: a lamp throws the shadow away from itself",
			sh_milli(sh_angle(k.dir, away)), 0);
		ok(fabs(k.weight - (1.0f - sqrtf(500.0f * 500.0f + 800.0f * 800.0f) / 4000.0f)) < 1e-4,
			"shadow: a lamp weighs its luminance times its falloff at P",
			sh_milli(k.weight), sh_milli(1.0 - sqrt(890000.0) / 4000.0));

		// A spot weighs the same way, out to its own range.
		const HDShadowLight spot = sh_lamp(HD_SHADOW_SPOT, P[0] - 500.0f, P[1] - 800.0f,
			P[2], 1.0f, 4000.0f);
		HDShadowKey ks;

		HDShadowKeyTarget(&spot, 1, P, &ks, 1.0f);
		ok(sh_angle(ks.dir, k.dir) < 1e-4 && fabs(ks.weight - k.weight) < 1e-6,
			"shadow: a spot weighs and points like a point of the same range",
			sh_milli(sh_angle(ks.dir, k.dir)), 0);
	}

	// Two equal lamps on opposite sides cancel: the fixed slant is left.
	{
		const HDShadowLight two[2] =
		{
			sh_lamp(HD_SHADOW_POINT, P[0] - 600.0f, P[1], P[2], 0.8f, 3000.0f),
			sh_lamp(HD_SHADOW_POINT, P[0] + 600.0f, P[1], P[2], 0.8f, 3000.0f)
		};

		HDShadowKeyTarget(two, 2, P, &k, 1.0f);
		ok(sh_angle(k.dir, fb) < 1e-3,
			"shadow: two equal opposite lamps leave the fixed slant",
			sh_milli(sh_angle(k.dir, fb)), 0);
		ok(fabs(k.strength - HD_SHADOW_DARK * HD_SHADOW_LOW) < 1e-5 && k.agree < 1e-5,
			"shadow: lamps that cancel give LOW of the darkness",
			sh_milli(k.strength), sh_milli(HD_SHADOW_DARK * HD_SHADOW_LOW));
	}

	// No light at all: the fixed slant at LOW of the darkness.
	HDShadowKeyTarget(0, 0, P, &k, 1.0f);
	ok(sh_angle(k.dir, fb) < 1e-4 && fabs(k.strength - HD_SHADOW_DARK * HD_SHADOW_LOW) < 1e-6 &&
		k.lights == 0 && k.weight == 0.0f,
		"shadow: no light leaves the fixed slant at LOW of the darkness",
		sh_milli(k.strength), sh_milli(HD_SHADOW_DARK * HD_SHADOW_LOW));

	/* A dim lamp: the darkness follows the weight up to W_REF. A quarter of
	 * W_REF agrees fully and reaches a quarter of the way from LOW. */
	{
		const HDShadowLight dim = sh_sun(0.0f, 1.0f, 0.0f, 0.25f * HD_SHADOW_W_REF);

		HDShadowKeyTarget(&dim, 1, P, &k, 1.0f);

		const double want = HD_SHADOW_DARK * (HD_SHADOW_LOW + (1.0 - HD_SHADOW_LOW) *
			0.25 * (0.25 * HD_SHADOW_W_REF) / (0.25 * HD_SHADOW_W_REF + HD_SHADOW_AGREE_EPS));

		ok(fabs(k.strength - want) < 1e-5,
			"shadow: below W_REF the darkness falls towards LOW with the weight",
			sh_milli(k.strength), sh_milli(want));
	}

	/* A small move of P is a small turn, even straight under a lamp, and
	 * across a lamp's range edge. */
	{
		const HDShadowLight lamps[2] =
		{
			sh_lamp(HD_SHADOW_POINT, P[0], P[1] - 900.0f, P[2], 1.0f, 2500.0f),
			sh_lamp(HD_SHADOW_POINT, P[0] + 2600.0f, P[1] - 500.0f, P[2] + 300.0f, 0.7f, 2000.0f)
		};
		double worst = 0.0, worstS = 0.0;
		HDShadowKey prev;

		for (int step = 0; step <= 1600; step++)
		{
			const float at[3] = { P[0] - 800.0f + (float)step, P[1], P[2] + 0.25f * step };

			HDShadowKeyTarget(lamps, 2, at, &k, 1.0f);

			if (step)
			{
				const double a = sh_angle(k.dir, prev.dir);
				const double s = fabs((double)k.strength - prev.strength);

				worst = a > worst ? a : worst;
				worstS = s > worstS ? s : worstS;
			}

			prev = k;
		}

		ok(worst < 0.2 && worstS < 1e-3,
			"shadow: a step of one unit turns the direction by less than 0.2 degrees",
			sh_milli(worst), 0);
	}

	// The slant limit: a low sun, a sun from below, one from straight below.
	{
		const HDShadowLight low = sh_sun(1.0f, 0.05f, 0.0f, 1.0f);

		HDShadowKeyTarget(&low, 1, P, &k, 1.0f);
		ok(fabs(k.slant - HD_SHADOW_SLANT_MAX) < 1e-3 && k.dir[0] > 0.0f &&
			fabs(atan2(k.dir[2], k.dir[0]) - atan2(0.0f + HD_SHADOW_W_FALLBACK * fb[2],
				1.0f + HD_SHADOW_W_FALLBACK * fb[0])) < 1e-4,
			"shadow: a low sun is held to SLANT_MAX with its compass bearing",
			sh_milli(k.slant), sh_milli(HD_SHADOW_SLANT_MAX));

		const HDShadowLight below = sh_sun(0.2f, -1.0f, 0.0f, 1.0f);

		HDShadowKeyTarget(&below, 1, P, &k, 1.0f);
		ok(fabs(k.slant - HD_SHADOW_SLANT_MAX) < 1e-3 && k.dir[1] > 0.0f && k.dir[0] > 0.0f,
			"shadow: a light from below points down at the limit, bearing kept",
			sh_milli(k.slant), sh_milli(HD_SHADOW_SLANT_MAX));

		float up[3] = { 0.0f, -1.0f, 0.0f };

		ok(HDShadowLimit(up) && fabs(HDShadowSlant(up) - HD_SHADOW_SLANT_MAX) < 1e-3 &&
			up[0] > 0.0f && up[2] > 0.0f,
			"shadow: straight up takes the fixed slant's bearing at the limit",
			sh_milli(HDShadowSlant(up)), sh_milli(HD_SHADOW_SLANT_MAX));

		int over = 0;

		for (int i = 0; i < 2000; i++)
		{
			const float a = (float)i * 0.731f, b = (float)i * 1.917f;
			const HDShadowLight any = sh_sun(cosf(a) * sinf(b), cosf(b), sinf(a) * sinf(b), 1.0f);

			HDShadowKeyTarget(&any, 1, P, &k, 1.0f);

			if (k.slant > HD_SHADOW_SLANT_MAX + 1e-3f || !sh_unit_finite(k.dir))
				over++;
		}

		ok(over == 0, "shadow: no direction of 2000 leaves the slant limit", over, 0);
	}

	// Nothing that is not a number, whatever the lights.
	{
		const float nan = sqrtf(-1.0f);
		const HDShadowLight bad[5] =
		{
			sh_sun(0.0f, 0.0f, 0.0f, 1.0f),				// a sun of no direction
			sh_lamp(HD_SHADOW_POINT, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f),	// a range of zero
			sh_lamp(HD_SHADOW_POINT, P[0], P[1], P[2], 1.0f, 1000.0f),	// at P itself
			sh_lamp(HD_SHADOW_SPOT, P[0], P[1], P[2], nan, 1000.0f),	// a colour of NaN
			sh_lamp(7, P[0], P[1], P[2], 1.0f, 1000.0f)			// a kind it does not know
		};

		HDShadowKeyTarget(bad, 5, P, &k, 1.0f);
		ok(sh_unit_finite(k.dir) && k.strength == k.strength && k.weight == k.weight &&
			k.lights == 1 && sh_angle(k.dir, fb) < 1e-4,
			"shadow: no NaN from a zero vector, a zero range, a lamp at P itself",
			k.lights, 1);
	}
}

/* Whether two floats hold the same bits, and two key lights the same
 * direction, strength, agreement and slant. */
static bool sh_same(float a, float b)
{
	return memcmp(&a, &b, sizeof(float)) == 0;
}

static bool sh_same_key(const HDShadowKey& a, const HDShadowKey& b)
{
	return sh_same(a.dir[0], b.dir[0]) && sh_same(a.dir[1], b.dir[1]) &&
		sh_same(a.dir[2], b.dir[2]) && sh_same(a.strength, b.strength) &&
		sh_same(a.agree, b.agree) && sh_same(a.slant, b.slant);
}

/* The lights' part between 0 and 1, for a sun and for a lamp: a pull
 * between turns the direction along the arc from the fixed slant towards
 * the lights' own at pull 1, the further the larger the pull, and gives a
 * strength between DARK * LOW and the lights' own, rising with it - for a
 * sun at pull p the strength of a sun p as bright; a pull past either end
 * counts as that end. */
static void shadow_pull()
{
	const float P[3] = { 30000.0f, -2000.0f, 41000.0f };
	const HDShadowLight sun = sh_sun(0.5f, 1.0f, -0.3f, 1.0f);
	const HDShadowLight lamp = sh_lamp(HD_SHADOW_POINT, P[0] + 400.0f, P[1] - 900.0f,
		P[2] - 500.0f, 0.9f, 3000.0f);
	const HDShadowLight* sets[2] = { &sun, &lamp };
	float fb[3];

	HDShadowFallback(fb);

	for (int s = 0; s < 2; s++)
	{
		HDShadowKey zero, full, k, prev;

		HDShadowKeyTarget(sets[s], 1, P, &zero, 0.0f);
		HDShadowKeyTarget(sets[s], 1, P, &full, 1.0f);
		prev = zero;

		const double span = sh_angle(fb, full.dir);
		double worstArc = 0.0;
		int back = 0, outside = 0, fall = 0;

		for (int i = 0; i <= 20; i++)
		{
			HDShadowKeyTarget(sets[s], 1, P, &k, 0.05f * (float)i);

			const double a = sh_angle(k.dir, fb);
			const double arc = fabs(a + sh_angle(k.dir, full.dir) - span);

			worstArc = arc > worstArc ? arc : worstArc;

			if (k.strength < zero.strength || k.strength > full.strength)
				outside++;

			if (i && a < sh_angle(prev.dir, fb) - 1e-4)
				back++;

			if (i && k.strength < prev.strength - 1e-6f)
				fall++;

			prev = k;
		}

		ok(span > 10.0 && worstArc < 1e-3 && back == 0,
			s ? "shadow: a lamp's pull turns the direction along the arc from the fixed slant, the further the larger"
				: "shadow: a sun's pull turns the direction along the arc from the fixed slant, the further the larger",
			sh_milli(worstArc), 0);
		ok(outside == 0 && fall == 0 && full.strength > zero.strength,
			s ? "shadow: a lamp's pull gives a strength between DARK * LOW and its own, rising with it"
				: "shadow: a sun's pull gives a strength between DARK * LOW and its own, rising with it",
			outside + fall, 0);

		HDShadowKeyTarget(sets[s], 1, P, &k, 0.5f);
		ok(sh_angle(k.dir, fb) > 1.0 && sh_angle(k.dir, full.dir) > 1.0 &&
			k.strength > zero.strength && k.strength < full.strength,
			s ? "shadow: half a lamp's pull is neither the fixed light nor the lamp's"
				: "shadow: half a sun's pull is neither the fixed light nor the sun's",
			sh_milli(sh_angle(k.dir, fb)), 0);
	}

	/* A white sun at a pull p weighs p times its luminance, in the
	 * agreement and against W_REF alike: the strength is that of a sun p as
	 * bright at pull 1. */
	{
		const double lum = HDShadowLuminance(1.0f, 1.0f, 1.0f);
		double worst = 0.0;

		for (int i = 1; i <= 10; i++)
		{
			const float p = 0.1f * (float)i;
			const double w = (double)p * lum;
			const double agree = w / (w + HD_SHADOW_AGREE_EPS) *
				(w < HD_SHADOW_W_REF ? w / HD_SHADOW_W_REF : 1.0);
			const double want = HD_SHADOW_DARK * (HD_SHADOW_LOW + (1.0 - HD_SHADOW_LOW) * agree);
			HDShadowKey k;

			HDShadowKeyTarget(&sun, 1, P, &k, p);

			const double e = fabs((double)k.strength - want);

			worst = e > worst ? e : worst;
		}

		ok(worst < 1e-5, "shadow: a sun's strength at a pull p is that of a sun p as bright",
			sh_milli(worst * 1000.0), 0);
	}

	// Past the ends: above 1 is 1; below 0, minus infinity and NaN are 0.
	{
		HDShadowKey one, nil, k;
		int differ = 0;

		HDShadowKeyTarget(&lamp, 1, P, &one, 1.0f);
		HDShadowKeyTarget(&lamp, 1, P, &nil, 0.0f);
		HDShadowKeyTarget(&lamp, 1, P, &k, 2.0f);
		differ += sh_same_key(k, one) ? 0 : 1;
		HDShadowKeyTarget(&lamp, 1, P, &k, 1.0e30f);
		differ += sh_same_key(k, one) ? 0 : 1;
		HDShadowKeyTarget(&lamp, 1, P, &k, -0.5f);
		differ += sh_same_key(k, nil) ? 0 : 1;
		HDShadowKeyTarget(&lamp, 1, P, &k, -1.0e30f);
		differ += sh_same_key(k, nil) ? 0 : 1;
		HDShadowKeyTarget(&lamp, 1, P, &k, sqrtf(-1.0f));
		differ += sh_same_key(k, nil) ? 0 : 1;
		ok(differ == 0 && !sh_same_key(one, nil),
			"shadow: a pull above 1 counts as 1, one below 0 or not a number as 0", differ, 0);
	}
}

/* The fixed light. At the table's pull the key light gives the same bits
 * for no light, a lamp above her, at her feet, below her and at P itself, a
 * spot beside her, a sun from any of 26 sides, thirty lights at once and
 * lights that are not numbers, at five reference points from the origin to
 * NaN: the fixed slant as HDShadowFallback gives it, at DARK * LOW, with
 * `lights` and `weight` what the list holds at pull 1. Driven as shadow_step
 * drives it, through a walk whose lights change every four steps, the
 * following draws exactly that in every step, with no offset and the lag at
 * 0, at 30, 60 and 165 steps a second; the same walk at pull 1 turns. */
static void shadow_fixed()
{
	const float nan = sqrtf(-1.0f);
	const float big = 1.0e30f;
	const float Ps[5][3] =
	{
		{ 30000.0f, -2000.0f, 41000.0f }, { 0.0f, 0.0f, 0.0f },
		{ -70000.0f, 12000.0f, 65000.0f }, { big, -big, big }, { nan, nan, nan }
	};
	const float down[3] = { 0.0f, 1.0f, 0.0f };
	const float dark = HD_SHADOW_DARK * HD_SHADOW_LOW;
	float fb[3];

	HDShadowFallback(fb);

	ok(HD_SHADOW_PULL == 0.0f && HD_SHADOW_FALLBACK_SLANT == 30.0f && HD_SHADOW_LOW == 0.75f,
		"shadow: the table's light is fixed - pull 0, 30 degrees, 0.75 of DARK",
		sh_milli(HD_SHADOW_PULL), 0);
	ok(fabs(sh_angle(fb, down) - 30.0) < 1e-3 &&
		fabs(atan2((double)fb[2], (double)fb[0]) -
			atan2((double)HD_SHADOW_FALLBACK_Z, (double)HD_SHADOW_FALLBACK_X)) < 1e-6 &&
		fabs(sqrt((double)fb[0] * fb[0] + (double)fb[2] * fb[2]) / fb[1] - 0.57735) < 1e-4,
		"shadow: the fixed slant stands 30 degrees off straight down on its bearing, 0.58 of her height long",
		sh_milli(sh_angle(fb, down)), 30000);

	int dirs = 0, strengths = 0, counts = 0, pulled = 0, cases = 0;

	for (int p = 0; p < 5; p++)
	{
		const float* P = Ps[p];

		for (int c = 0; c < 35; c++)
		{
			HDShadowLight set[HD_SHADOW_MAX_LIGHTS];
			const HDShadowLight* lights = set;
			int n = 1;

			if (c == 0)
				n = 0;
			else if (c == 1)
			{
				lights = 0;
				n = 5;
			}
			else if (c == 2)
				set[0] = sh_lamp(HD_SHADOW_POINT, P[0] + 100.0f, P[1] - 800.0f, P[2], 1.0f, 3000.0f);
			else if (c == 3)
				set[0] = sh_lamp(HD_SHADOW_POINT, P[0] - 300.0f, P[1] + 900.0f, P[2] + 200.0f, 1.0f, 3000.0f);
			else if (c == 4)
				set[0] = sh_lamp(HD_SHADOW_POINT, P[0] + 200.0f, P[1] + 2500.0f, P[2] - 300.0f, 1.0f, 6000.0f);
			else if (c == 5)
				set[0] = sh_lamp(HD_SHADOW_POINT, P[0], P[1], P[2], 1.0f, 3000.0f);
			else if (c == 6)
				set[0] = sh_lamp(HD_SHADOW_SPOT, P[0] - 600.0f, P[1] - 100.0f, P[2] + 150.0f, 0.7f, 1500.0f);
			else if (c < 33)
			{
				// 26 sides: the six axes and twenty between them.
				const int i = c - 7;
				const float a = (float)i * 0.9f, b = (float)i * 0.37f;

				set[0] = i < 6 ? sh_sun(i == 0 ? 1.0f : (i == 1 ? -1.0f : 0.0f),
					i == 2 ? 1.0f : (i == 3 ? -1.0f : 0.0f),
					i == 4 ? 1.0f : (i == 5 ? -1.0f : 0.0f), 1.0f)
					: sh_sun(cosf(a) * sinf(b), cosf(b), sinf(a) * sinf(b), 0.8f);
			}
			else if (c == 33)
			{
				n = 30;

				for (int i = 0; i < 15; i++)
				{
					const float a = (float)i * 0.42f;

					set[i] = sh_lamp(i & 1 ? HD_SHADOW_SPOT : HD_SHADOW_POINT,
						P[0] + 900.0f * cosf(a), P[1] - 300.0f - 60.0f * i, P[2] + 900.0f * sinf(a),
						0.2f + 0.05f * i, 2500.0f);
					set[15 + i] = sh_sun(sinf(a), 0.3f + 0.1f * i, -cosf(a), 0.1f + 0.03f * i);
				}
			}
			else
			{
				n = 3;
				set[0] = sh_lamp(HD_SHADOW_SPOT, P[0], P[1], P[2], nan, 1000.0f);
				set[1] = sh_lamp(HD_SHADOW_POINT, nan, P[1], P[2], 1.0f, 1000.0f);
				set[2] = sh_sun(big, -big, nan, 1.0f);
			}

			HDShadowKey k, k0, k1;

			HDShadowKeyTarget(lights, n, P, &k);
			HDShadowKeyTarget(lights, n, P, &k0, 0.0f);
			HDShadowKeyTarget(lights, n, P, &k1, 1.0f);
			cases++;

			if (!sh_same(k.dir[0], fb[0]) || !sh_same(k.dir[1], fb[1]) ||
				!sh_same(k.dir[2], fb[2]) || !sh_same_key(k, k0))
				dirs++;

			if (!sh_same(k.strength, dark) || !sh_same(k.agree, 0.0f))
				strengths++;

			if (k.lights != k1.lights || !sh_same(k.weight, k1.weight))
				counts++;

			if (sh_angle(k1.dir, fb) > 5.0)
				pulled++;
		}
	}

	ok(dirs == 0, "shadow: at the table's pull every light set at every P gives the fixed slant, bit for bit",
		dirs, 0);
	ok(strengths == 0, "shadow: at the table's pull every light set at every P gives DARK * LOW, bit for bit",
		strengths, 0);
	ok(counts == 0, "shadow: lights and weight count what the list holds, before the pull", counts, 0);
	// The same sets at pull 1 turn the direction, so the checks above are not empty.
	ok(pulled >= 100 && cases == 175, "shadow: the same light sets at pull 1 turn the direction by over 5 degrees",
		pulled, 100);

	// The walk, with lights that change every four steps.
	const float* O = Ps[0];
	const HDShadowLight walkLamps[3] =
	{
		sh_lamp(HD_SHADOW_POINT, O[0] + 1500.0f, O[1] - 700.0f, O[2] + 200.0f, 1.0f, 3000.0f),
		sh_lamp(HD_SHADOW_SPOT, O[0] + 3500.0f, O[1] - 400.0f, O[2] - 600.0f, 0.8f, 2500.0f),
		sh_sun(-0.7f, 0.5f, 0.4f, 0.9f)
	};
	const HDShadowLight* walkSets[4] = { walkLamps, walkLamps + 1, 0, walkLamps };
	const int walkCounts[4] = { 1, 2, 0, 3 };
	const int rates[3] = { 30, 60, 165 };
	double turned = 0.0;

	for (int r = 0; r < 3; r++)
	{
		const float dt = 1.0f / (float)rates[r];
		HDShadowFollow f, g;
		int wrong = 0;

		memset(&f, 0, sizeof(f));
		memset(&g, 0, sizeof(g));
		HDShadowFollowReset(&f);
		HDShadowFollowReset(&g);

		for (int i = 0; i < rates[r] * 5; i++)
		{
			const float t = (float)i * dt;
			const float at[3] = { O[0] + 1000.0f * t, O[1], O[2] + 250.0f * t };
			const int s = (i / 4) % 4;
			const float step = i && i % 13 ? dt : 0.0f;
			HDShadowKey k, k1;

			HDShadowKeyTarget(walkSets[s], walkCounts[s], at, &k);
			HDShadowFollowLights(&f, walkSets[s], walkCounts[s]);

			const bool snap = HDShadowFollowStep(&f, &k, at, step);

			if (snap != (i == 0) || !sh_same(f.dir[0], fb[0]) || !sh_same(f.dir[1], fb[1]) ||
				!sh_same(f.dir[2], fb[2]) || !sh_same(f.strength, dark) ||
				f.offset[0] != 0.0f || f.offset[1] != 0.0f || f.offset[2] != 0.0f ||
				f.offsetStrength != 0.0f || !sh_same(f.lag, 0.0f))
				wrong++;

			HDShadowKeyTarget(walkSets[s], walkCounts[s], at, &k1, 1.0f);
			HDShadowFollowLights(&g, walkSets[s], walkCounts[s], 1.0f);
			HDShadowFollowStep(&g, &k1, at, step);
		}

		turned = g.lag > turned ? g.lag : turned;

		ok(wrong == 0 && f.snaps == 1,
			r == 0 ? "shadow: at 30 steps a second a walk through changing lights draws the fixed light, lag 0"
				: (r == 1 ? "shadow: at 60 steps a second a walk through changing lights draws the fixed light, lag 0"
					: "shadow: at 165 steps a second a walk through changing lights draws the fixed light, lag 0"),
			wrong, 0);
	}

	ok(turned > 5.0, "shadow: the same walk at pull 1 turns the drawn direction away from the target",
		sh_milli(turned), 5000);
}

/* The following the way shadow_step drives it, at the lights' full part,
 * pull 1, so that a change of the lights moves the target: the frame's
 * target out of the lights at P, the same lights weighed at the last step's
 * P, the step. True when the step snapped. */
static bool sh_follow(HDShadowFollow* f, const HDShadowLight* lights, int count, const float* P,
	float dt, HDShadowKey* target)
{
	HDShadowKeyTarget(lights, count, P, target, 1.0f);
	HDShadowFollowLights(f, lights, count, 1.0f);
	return HDShadowFollowStep(f, target, P, dt);
}

/* The closed form of a change of the lights from the target `from` to `to`
 * with `e` of the change still held back: to + (from - to) e, worked in
 * double, made unit and held to the slant limit; the strength likewise,
 * held to 0..DARK. */
static void sh_closed(const HDShadowKey& from, const HDShadowKey& to, double e, float* dir,
	float* strength)
{
	double raw[3], l = 0.0;

	for (int i = 0; i < 3; i++)
	{
		raw[i] = to.dir[i] + ((double)from.dir[i] - to.dir[i]) * e;
		l += raw[i] * raw[i];
	}

	l = sqrt(l);

	for (int i = 0; i < 3; i++)
		dir[i] = (float)(raw[i] / l);

	HDShadowLimit(dir);

	const double s = to.strength + ((double)from.strength - to.strength) * e;

	*strength = (float)(s > 0.0 ? (s < HD_SHADOW_DARK ? s : HD_SHADOW_DARK) : 0.0);
}

/* Whether a step left no offset and drew the target as it is. */
static bool sh_cleared(const HDShadowFollow& f, const HDShadowKey& k)
{
	return f.offset[0] == 0.0f && f.offset[1] == 0.0f && f.offset[2] == 0.0f &&
		f.offsetStrength == 0.0f && f.dir[0] == k.dir[0] && f.dir[1] == k.dir[1] &&
		f.dir[2] == k.dir[2] && f.strength == k.strength;
}

/* The following's ground: the first step snaps to the target; a step given
 * only a target, with no measurement of the lights, counts the whole change
 * of the target as the lights' and blends it along the closed form; the
 * decay of an offset. */
static void shadow_follow()
{
	const float P[3] = { 100.0f, 200.0f, 300.0f };
	HDShadowKey from, to;
	HDShadowFollow f;

	memset(&f, 0, sizeof(f));
	memset(&from, 0, sizeof(from));
	memset(&to, 0, sizeof(to));
	from.dir[1] = 1.0f;
	from.strength = 0.1f;
	to.dir[0] = sinf(0.6f);
	to.dir[1] = cosf(0.6f);
	to.strength = 0.4f;

	ok(HDShadowFollowStep(&f, &from, P, 0.0f) && f.strength == from.strength &&
		f.dir[0] == 0.0f && f.dir[1] == 1.0f && f.snaps == 1 && f.offsetStrength == 0.0f,
		"shadow: the first step snaps to the target", f.snaps, 1);

	// After n steps of dt, exp(-n dt / TAU) of the change is still held back.
	double worst = 0.0, worstS = 0.0;

	for (int n = 1; n <= 90; n++)
	{
		HDShadowFollowStep(&f, &to, P, 1.0f / 60.0f);

		float cd[3], cs;

		sh_closed(from, to, exp(-(double)n / 60.0 / HD_SHADOW_TAU), cd, &cs);

		const double a = sh_angle(f.dir, cd);
		const double e = fabs((double)f.strength - cs);

		worst = a > worst ? a : worst;
		worstS = e > worstS ? e : worstS;
	}

	ok(worst < 1e-3 && worstS < 2e-6 && f.snaps == 1,
		"shadow: given only a target, a change is blended along exp(-t / TAU)",
		sh_milli(worst), 0);

	ok(HDShadowDecay(0.0f) == 1.0f && HDShadowDecay(-1.0f) == 1.0f &&
		fabs(HDShadowDecay(HD_SHADOW_TAU) - exp(-1.0)) < 1e-6,
		"shadow: after one TAU 37 per cent of an offset is left, none decays without time",
		sh_milli(HDShadowDecay(HD_SHADOW_TAU)), sh_milli(exp(-1.0)));
}

/* Her movement is not delayed. With the lights fixed - one lamp; two lamps
 * and a sun - P walks past the lamp at 1000 units a second for four seconds
 * and then stands for two, at 30, 60 and 165 steps a second: in every step
 * the drawn direction and strength are that step's target, the log's lag
 * stays near 0, and once she stands every further step draws the same bits
 * as the one before. */
static void shadow_follow_move()
{
	const float O[3] = { 30000.0f, -2000.0f, 41000.0f };
	const HDShadowLight one[1] =
	{
		sh_lamp(HD_SHADOW_POINT, O[0], O[1] - 900.0f, O[2], 1.0f, 3000.0f)
	};
	const HDShadowLight three[3] =
	{
		sh_lamp(HD_SHADOW_POINT, O[0], O[1] - 900.0f, O[2], 0.9f, 3000.0f),
		sh_lamp(HD_SHADOW_SPOT, O[0] + 1200.0f, O[1] - 700.0f, O[2] - 400.0f, 0.6f, 2500.0f),
		sh_sun(0.4f, 1.0f, -0.3f, 0.3f)
	};
	const HDShadowLight* sets[2] = { one, three };
	const int counts[2] = { 1, 3 };
	const char* names[2] = { "one lamp", "two lamps and a sun" };
	const int rates[3] = { 30, 60, 165 };
	char what[192];

	for (int s = 0; s < 2; s++)
		for (int r = 0; r < 3; r++)
		{
			const int rate = rates[r];
			const float dt = 1.0f / (float)rate;
			HDShadowFollow f;
			HDShadowKey k, start, end;
			double worstA = 0.0, worstS = 0.0;
			float held[3] = { 0.0f, 0.0f, 0.0f };
			float heldS = 0.0f;
			bool same = true;

			memset(&f, 0, sizeof(f));
			memset(&start, 0, sizeof(start));
			memset(&end, 0, sizeof(end));

			for (int i = 0; i <= 6 * rate; i++)
			{
				const double t = (double)(i < 4 * rate ? i : 4 * rate) / rate;
				const float at[3] = { O[0] - 2000.0f + (float)(1000.0 * t), O[1],
					O[2] + (float)(150.0 * t) };

				sh_follow(&f, sets[s], counts[s], at, i ? dt : 0.0f, &k);

				if (i == 0)
					start = k;

				if (i <= 4 * rate)
				{
					const double a = sh_angle(f.dir, k.dir);
					const double e = fabs((double)f.strength - k.strength);

					worstA = a > worstA ? a : worstA;
					worstS = e > worstS ? e : worstS;
					end = k;
				}
				else if (f.dir[0] != held[0] || f.dir[1] != held[1] || f.dir[2] != held[2] ||
					f.strength != heldS)
					same = false;

				for (int j = 0; j < 3; j++)
					held[j] = f.dir[j];

				heldS = f.strength;
			}

			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the drawn direction is each step's target",
				names[s], rate);
			ok(worstA < 1e-3 && sh_angle(start.dir, end.dir) > 30.0, what, sh_milli(worstA), 0);
			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the drawn strength is each step's target",
				names[s], rate);
			ok(worstS < 1e-6, what, sh_milli(worstS * 1000.0), 0);
			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the log's lag stays near 0", names[s], rate);
			ok(f.lag < 1e-3f, what, sh_milli(f.lag), 0);
			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - standing, every step draws the same bits",
				names[s], rate);
			ok(same, what, same ? 1 : 0, 1);
		}
}

/* A change of the lights is blended. P stands; at one second a lamp is
 * removed, a sun added, or a lamp's colour halved. Every later step draws
 * the closed form new + (old - new) exp(-(t - 1) / TAU), t from the last
 * step with the old lights, made unit and held to the limit, and the
 * strength likewise; the step of the change moves no further than the
 * curve's own first step; 30, 60 and 165 steps a second draw the same at
 * the same moments; and the log's lag is 0 before the change, about the
 * whole turn right after it, and under a tenth of it a second and a half
 * later. */
static void shadow_follow_lights()
{
	const float P[3] = { 30000.0f, -2000.0f, 41000.0f };
	const HDShadowLight A = sh_lamp(HD_SHADOW_POINT, P[0] - 700.0f, P[1] - 900.0f, P[2] + 200.0f,
		1.0f, 3000.0f);
	const HDShadowLight B = sh_lamp(HD_SHADOW_POINT, P[0] + 900.0f, P[1] - 600.0f, P[2] - 300.0f,
		0.8f, 2500.0f);
	const HDShadowLight sun = sh_sun(0.5f, 1.0f, 0.2f, 0.7f);
	HDShadowLight half = B;

	half.r = half.g = half.b = 0.4f;

	const HDShadowLight olds[3][2] = { { A, B }, { A, A }, { A, B } };
	const int oldN[3] = { 2, 1, 2 };
	const HDShadowLight news[3][2] = { { A, A }, { A, sun }, { A, half } };
	const int newN[3] = { 1, 2, 2 };
	const char* names[3] = { "a lamp removed", "a sun added", "a lamp's colour halved" };
	const int rates[3] = { 30, 60, 165 };

	/* Whole numbers of steps at all three rates, as the change at one second
	 * is, so the three see it at the same moment. */
	const double times[4] = { 1.2, 1.6, 2.0, 3.0 };
	char what[192];

	for (int c = 0; c < 3; c++)
	{
		HDShadowKey from, to;
		float got[3][4][4];

		HDShadowKeyTarget(olds[c], oldN[c], P, &from, 1.0f);
		HDShadowKeyTarget(news[c], newN[c], P, &to, 1.0f);

		const double turn = sh_angle(from.dir, to.dir);

		for (int r = 0; r < 3; r++)
		{
			const int rate = rates[r];
			const float dt = 1.0f / (float)rate;
			HDShadowFollow f;
			HDShadowKey k;
			double worstA = 0.0, worstS = 0.0, stepA = -1.0, stepWant = 0.0;
			float lagBefore = 0.0f, lagAt = 0.0f;
			float prev[3] = { 0.0f, 0.0f, 0.0f };
			int next = 0;

			memset(&f, 0, sizeof(f));

			for (int i = 0; i <= 3 * rate; i++)
			{
				const double t = (double)i / rate;
				const bool changed = i > rate;

				sh_follow(&f, changed ? news[c] : olds[c], changed ? newN[c] : oldN[c], P,
					i ? dt : 0.0f, &k);

				if (changed)
				{
					float cd[3], cs;

					sh_closed(from, to, exp(-(t - 1.0) / HD_SHADOW_TAU), cd, &cs);

					const double a = sh_angle(f.dir, cd);
					const double e = fabs((double)f.strength - cs);

					worstA = a > worstA ? a : worstA;
					worstS = e > worstS ? e : worstS;

					if (i == rate + 1)
					{
						stepA = sh_angle(f.dir, prev);
						stepWant = sh_angle(from.dir, cd);
					}
				}

				if (i == rate)
				{
					lagBefore = f.lag;
					f.lag = 0.0f;
				}
				else if (i == 2 * rate + rate / 2)
				{
					lagAt = f.lag;
					f.lag = 0.0f;
				}

				for (int j = 0; j < 3; j++)
					prev[j] = f.dir[j];

				while (next < 4 && t >= times[next] - 1e-9)
				{
					for (int j = 0; j < 3; j++)
						got[r][next][j] = f.dir[j];

					got[r][next][3] = f.strength;
					next++;
				}
			}

			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the direction follows the closed form",
				names[c], rate);
			ok(worstA < 0.005 && turn > 3.0, what, sh_milli(worstA), 0);
			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the strength follows the closed form",
				names[c], rate);
			ok(worstS < 1e-5, what, sh_milli(worstS * 1000.0), 0);
			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the change's step is the curve's first step",
				names[c], rate);
			ok(stepA > 0.0 && fabs(stepA - stepWant) < 0.005 && stepA < 0.07 * turn, what,
				sh_milli(stepA), sh_milli(stepWant));
			snprintf(what, sizeof(what),
				"shadow: %s, %d steps a second - the log's lag: 0, the turn, under a tenth",
				names[c], rate);
			ok(lagBefore < 1e-3f && lagAt > 0.8 * turn && lagAt < turn + 1e-3 &&
				f.lag < 0.1 * turn, what, sh_milli(lagAt), sh_milli(turn));
		}

		double worstR = 0.0, worstRS = 0.0;

		for (int r = 1; r < 3; r++)
			for (int s = 0; s < 4; s++)
			{
				const double a = sh_angle(got[r][s], got[0][s]);
				const double e = fabs((double)got[r][s][3] - got[0][s][3]);

				worstR = a > worstR ? a : worstR;
				worstRS = e > worstRS ? e : worstRS;
			}

		snprintf(what, sizeof(what),
			"shadow: %s - 30, 60 and 165 steps a second draw the same at the same moments",
			names[c]);
		ok(worstR < 0.005 && worstRS < 1e-5, what, sh_milli(worstR), 0);
	}
}

/* Both at once: she walks while a lamp is removed. The offset - the drawn
 * values minus the target, the direction before it is made unit - is
 * -jump exp(-(t - t0) / TAU), the jump being the change of the lights
 * measured where she stood at t0, the last step with the old lights; it is
 * the same whichever way she walks on, and the drawn direction is the
 * target plus it. */
static void shadow_follow_both()
{
	const float O[3] = { 30000.0f, -2000.0f, 41000.0f };
	const HDShadowLight A = sh_lamp(HD_SHADOW_POINT, O[0] - 700.0f, O[1] - 900.0f, O[2] + 200.0f,
		1.0f, 3000.0f);
	const HDShadowLight old[2] =
	{
		A, sh_lamp(HD_SHADOW_POINT, O[0] + 900.0f, O[1] - 600.0f, O[2] - 300.0f, 0.8f, 2500.0f)
	};
	const char* names[3] = { "she stops", "she walks on", "she turns back" };
	const int rates[2] = { 60, 165 };
	static float offs[3][400][4];
	char what[192];

	for (int r = 0; r < 2; r++)
	{
		const int rate = rates[r];
		const float dt = 1.0f / (float)rate;

		for (int p = 0; p < 3; p++)
		{
			HDShadowFollow f;
			HDShadowKey k, jf, jt;
			float at0[3] = { 0.0f, 0.0f, 0.0f };
			double worstO = 0.0, worstOS = 0.0, worstC = 0.0, jump = 0.0;

			memset(&f, 0, sizeof(f));

			for (int i = 0; i <= 2 * rate; i++)
			{
				const double t = (double)i / rate;
				const double u = t < 1.0 ? t : 1.0;
				const double v = t > 1.0 ? t - 1.0 : 0.0;
				float at[3] = { O[0] - 800.0f + (float)(900.0 * u), O[1], O[2] + (float)(100.0 * u) };

				if (p == 1)
				{
					at[0] += (float)(900.0 * v);
					at[2] += (float)(100.0 * v);
				}
				else if (p == 2)
				{
					at[0] -= (float)(300.0 * v);
					at[2] -= (float)(700.0 * v);
				}

				if (i > rate)
					sh_follow(&f, &A, 1, at, dt, &k);
				else
					sh_follow(&f, old, 2, at, i ? dt : 0.0f, &k);

				if (i == rate)
				{
					for (int j = 0; j < 3; j++)
						at0[j] = at[j];

					HDShadowKeyTarget(old, 2, at0, &jf, 1.0f);
					HDShadowKeyTarget(&A, 1, at0, &jt, 1.0f);
					jump = sh_angle(jf.dir, jt.dir);
				}

				if (i <= rate)
					continue;

				const double e = exp(-(t - 1.0) / HD_SHADOW_TAU);

				for (int j = 0; j < 3; j++)
				{
					const double want = -((double)jt.dir[j] - jf.dir[j]) * e;
					const double d = fabs(f.offset[j] - want);

					worstO = d > worstO ? d : worstO;
				}

				const double wantS = -((double)jt.strength - jf.strength) * e;
				const double ds = fabs(f.offsetStrength - wantS);

				worstOS = ds > worstOS ? ds : worstOS;

				float sum[3] = { f.last[0] + f.offset[0], f.last[1] + f.offset[1],
					f.last[2] + f.offset[2] };

				hd_shadow_unit(sum);
				HDShadowLimit(sum);

				const double c = sh_angle(sum, f.dir);

				worstC = c > worstC ? c : worstC;

				for (int j = 0; j < 3; j++)
					offs[p][i - rate - 1][j] = f.offset[j];

				offs[p][i - rate - 1][3] = f.offsetStrength;
			}

			snprintf(what, sizeof(what),
				"shadow: walking while a lamp is removed (%s, %d/s) - the offset is -jump exp(-t / TAU)",
				names[p], rate);
			ok(worstO < 1e-5 && jump > 3.0, what, sh_milli(worstO * 1000.0), 0);
			snprintf(what, sizeof(what),
				"shadow: walking while a lamp is removed (%s, %d/s) - the strength's offset too",
				names[p], rate);
			ok(worstOS < 1e-6, what, sh_milli(worstOS * 1000.0), 0);
			snprintf(what, sizeof(what),
				"shadow: walking while a lamp is removed (%s, %d/s) - drawn is target plus offset",
				names[p], rate);
			ok(worstC < 1e-3, what, sh_milli(worstC), 0);
		}

		double worstP = 0.0;

		for (int p = 1; p < 3; p++)
			for (int i = 0; i < rate; i++)
				for (int j = 0; j < 4; j++)
				{
					const double d = fabs((double)offs[p][i][j] - offs[0][i][j]);

					worstP = d > worstP ? d : worstP;
				}

		snprintf(what, sizeof(what),
			"shadow: %d steps a second - the offset does not depend on the way she walks on",
			rate);
		ok(worstP < 1e-6, what, sh_milli(worstP * 1000.0), 0);
	}
}

/* The edges: every snap clears the offset and draws the target; a clock
 * that stands takes a change of the lights without decay, and nothing leaps
 * when it runs again; lights that flicker under a standing clock move
 * nothing; no offset, whatever it points at, leads out of the slant limit or
 * to a NaN; and lights without a direction give no NaN. */
static void shadow_follow_edges()
{
	const float P[3] = { 30000.0f, -2000.0f, 41000.0f };
	const HDShadowLight A = sh_lamp(HD_SHADOW_POINT, P[0] - 700.0f, P[1] - 900.0f, P[2] + 200.0f,
		1.0f, 3000.0f);
	const HDShadowLight old[2] =
	{
		A, sh_lamp(HD_SHADOW_POINT, P[0] + 900.0f, P[1] - 600.0f, P[2] - 300.0f, 0.8f, 2500.0f)
	};
	const float dt = 1.0f / 60.0f;
	HDShadowKey from, to, k, k2;
	HDShadowFollow settled, live;

	HDShadowKeyTarget(old, 2, P, &from, 1.0f);
	HDShadowKeyTarget(&A, 1, P, &to, 1.0f);

	const double turn = sh_angle(from.dir, to.dir);

	memset(&settled, 0, sizeof(settled));

	for (int i = 0; i <= 60; i++)
		sh_follow(&settled, old, 2, P, i ? dt : 0.0f, &k);

	live = settled;
	sh_follow(&live, &A, 1, P, dt, &k);

	const bool held = fabs(live.offset[0]) + fabs(live.offset[1]) + fabs(live.offset[2]) > 0.01;

	// Every snap clears the offset: a gap, a jump, a time that is no number, a reset.
	{
		HDShadowFollow f = live;

		ok(held && sh_follow(&f, &A, 1, P, HD_SHADOW_GAP * 1.5f, &k) && sh_cleared(f, k),
			"shadow: a gap beyond GAP snaps, clears the offset and draws the target",
			sh_milli(sh_angle(f.dir, k.dir)), 0);
	}

	{
		const float far[3] = { P[0] + HD_SHADOW_SPEED / 60.0f + HD_SHADOW_SLACK + 1.0f, P[1], P[2] };
		const float near[3] = { P[0] + HD_SHADOW_SPEED / 60.0f + HD_SHADOW_SLACK - 1.0f, P[1], P[2] };
		HDShadowFollow f = live, g = live;
		const bool snapFar = sh_follow(&f, &A, 1, far, dt, &k);
		const bool snapNear = sh_follow(&g, &A, 1, near, dt, &k2);

		ok(snapFar && sh_cleared(f, k) && !snapNear &&
			fabs(g.offset[0]) + fabs(g.offset[1]) + fabs(g.offset[2]) > 0.005,
			"shadow: a jump further than she can move snaps and clears, one just inside does not",
			sh_milli(sh_angle(g.dir, k2.dir)), 0);
	}

	{
		HDShadowFollow f = live;

		ok(sh_follow(&f, &A, 1, P, sqrtf(-1.0f), &k) && sh_cleared(f, k),
			"shadow: a time that is not a number snaps and clears the offset",
			sh_milli(f.strength), sh_milli(k.strength));
	}

	{
		HDShadowFollow f = live;

		HDShadowFollowReset(&f);
		ok(sh_follow(&f, &A, 1, P, dt, &k) && sh_cleared(f, k) && f.snaps == live.snaps + 1,
			"shadow: the step after a reset snaps and clears the offset", f.snaps, live.snaps + 1);
	}

	/* A change of the lights under a standing clock, dt 0 and dt below 0:
	 * the jump goes into the offset whole, so nothing is drawn differently,
	 * and the next step with the clock running moves exactly the curve's
	 * first step. */
	const float stand[2] = { 0.0f, -0.01f };
	float first[3], firstS;

	sh_closed(from, to, exp(-dt / HD_SHADOW_TAU), first, &firstS);

	const double firstStep = sh_angle(from.dir, first);

	for (int d = 0; d < 2; d++)
	{
		HDShadowFollow f = settled;
		const float before[3] = { f.dir[0], f.dir[1], f.dir[2] };
		const float beforeS = f.strength;
		char what[160];

		sh_follow(&f, &A, 1, P, stand[d], &k);

		double taken = 0.0;

		for (int j = 0; j < 3; j++)
		{
			const double e = fabs(f.offset[j] + ((double)to.dir[j] - from.dir[j]));

			taken = e > taken ? e : taken;
		}

		snprintf(what, sizeof(what),
			"shadow: dt %s - a change of the lights goes into the offset without decay",
			d ? "below 0" : "0");
		ok(sh_angle(f.dir, before) < 1e-4 && fabs(f.strength - beforeS) < 1e-6 && taken < 1e-6,
			what, sh_milli(sh_angle(f.dir, before)), 0);

		const float still[3] = { f.dir[0], f.dir[1], f.dir[2] };

		sh_follow(&f, &A, 1, P, dt, &k);

		const double moved = sh_angle(f.dir, still);

		snprintf(what, sizeof(what),
			"shadow: dt %s - the clock running again moves one step of the curve, no leap",
			d ? "below 0" : "0");
		ok(fabs(moved - firstStep) < 0.005 && moved < 0.05 * turn, what, sh_milli(moved),
			sh_milli(firstStep));
	}

	// Lights that flicker while the clock stands move nothing.
	{
		HDShadowFollow f = settled;
		double worst = 0.0, worstS = 0.0;

		for (int i = 0; i < 50; i++)
		{
			if (i & 1)
				sh_follow(&f, old, 2, P, 0.0f, &k);
			else
				sh_follow(&f, &A, 1, P, 0.0f, &k);

			const double a = sh_angle(f.dir, settled.dir);
			const double e = fabs((double)f.strength - settled.strength);

			worst = a > worst ? a : worst;
			worstS = e > worstS ? e : worstS;
		}

		ok(worst < 1e-4 && worstS < 1e-6 && f.snaps == settled.snaps,
			"shadow: lights that flicker under a standing clock move nothing",
			sh_milli(worst), 0);
	}

	/* Any offset: 400 directions round the sphere at seven lengths from a
	 * thousandth to 1e30, strengths' offsets to 1e30 either way. */
	{
		const float lengths[7] = { 1e-3f, 0.1f, 1.0f, 2.0f, 10.0f, 1e6f, 1e30f };
		int bad = 0;

		for (int i = 0; i < 400; i++)
			for (int m = 0; m < 7; m++)
			{
				const float a = (float)i * 0.731f, b = (float)i * 1.917f;
				const float d[3] = { cosf(a) * sinf(b), cosf(b), sinf(a) * sinf(b) };
				HDShadowFollow f;

				memset(&f, 0, sizeof(f));
				f.primed = 1;

				for (int j = 0; j < 3; j++)
				{
					f.at[j] = P[j];
					f.last[j] = to.dir[j];
					f.offset[j] = lengths[m] * d[j];
				}

				f.lastStrength = to.strength;
				f.offsetStrength = lengths[m] * (float)(i % 3 - 1);
				HDShadowFollowStep(&f, &to, P, dt);

				if (!sh_unit_finite(f.dir) || HDShadowSlant(f.dir) > HD_SHADOW_SLANT_MAX + 1e-3f ||
					!(f.strength >= 0.0f && f.strength <= HD_SHADOW_DARK) || !(f.lag == f.lag))
					bad++;
			}

		ok(bad == 0, "shadow: no offset of 2800, any way and up to 1e30 long, leaves the limit",
			bad, 0);
	}

	{
		HDShadowFollow f;

		memset(&f, 0, sizeof(f));
		f.primed = 1;

		for (int j = 0; j < 3; j++)
		{
			f.at[j] = P[j];
			f.last[j] = to.dir[j];
			f.offset[j] = -to.dir[j];
		}

		f.lastStrength = to.strength;
		HDShadowFollowStep(&f, &to, P, 0.0f);

		const bool against = f.dir[0] == to.dir[0] && f.dir[1] == to.dir[1] &&
			f.dir[2] == to.dir[2];

		f.offset[1] = sqrtf(-1.0f);
		HDShadowFollowStep(&f, &to, P, dt);
		ok(against && sh_cleared(f, to),
			"shadow: an offset straight against the target or not a number draws the target",
			against ? 1 : 0, 1);
	}

	/* No lights, a lamp at P itself, a sun of no direction, a lamp of no
	 * range, and P at the origin, changing every few steps. */
	{
		const float zero[3] = { 0.0f, 0.0f, 0.0f };
		const HDShadowLight odd[4] =
		{
			sh_lamp(HD_SHADOW_POINT, P[0], P[1], P[2], 1.0f, 1000.0f),
			sh_sun(0.0f, 0.0f, 0.0f, 1.0f),
			A,
			sh_lamp(HD_SHADOW_SPOT, P[0], P[1] - 500.0f, P[2], 1.0f, 0.0f)
		};
		HDShadowFollow f;
		int bad = 0;

		memset(&f, 0, sizeof(f));

		for (int i = 0; i < 300; i++)
		{
			const int set = (i / 7) % 5;
			const float* at = (i / 37) & 1 ? zero : P;

			if (set == 4)
				sh_follow(&f, 0, 0, at, dt, &k);
			else
				sh_follow(&f, odd + set, 1, at, (i % 11) ? dt : 0.0f, &k);

			if (!sh_unit_finite(f.dir) || HDShadowSlant(f.dir) > HD_SHADOW_SLANT_MAX + 1e-3f ||
				!(f.strength >= 0.0f && f.strength <= HD_SHADOW_DARK) || !(f.lag == f.lag) ||
				!(f.offset[0] == f.offset[0] && f.offset[1] == f.offset[1] &&
				f.offset[2] == f.offset[2] && f.offsetStrength == f.offsetStrength))
				bad++;
		}

		ok(bad == 0, "shadow: no NaN from no lights, a lamp at P, a zero vector, a zero range",
			bad, 0);
	}
}

/* The box: the basis, the snap, the half-texel rule, the matrices. */
static void shadow_box()
{
	float right[3], up[3], fwd[3];
	int bad = 0;
	double worstStep = 0.0;
	float lastRight[3] = { 0.0f, 0.0f, 0.0f };

	// Every direction of the limit cone's rim and inside it, in steps.
	for (int s = 0; s <= 11; s++)
		for (int a = 0; a <= 720; a++)
		{
			const float slant = (float)s * 5.0f * 3.14159265f / 180.0f;
			const float bearing = (float)a * 0.5f * 3.14159265f / 180.0f;
			const float d[3] = { sinf(slant) * cosf(bearing), cosf(slant),
				sinf(slant) * sinf(bearing) };

			HDShadowBasis(d, right, up, fwd);

			const float cr[3] = { right[1] * up[2] - right[2] * up[1],
				right[2] * up[0] - right[0] * up[2], right[0] * up[1] - right[1] * up[0] };

			if (fabs(hd_shadow_len(right) - 1.0f) > 1e-5f || fabs(hd_shadow_len(up) - 1.0f) > 1e-5f ||
				fabs(hd_shadow_dot(right, up)) > 1e-5f || fabs(hd_shadow_dot(right, fwd)) > 1e-5f ||
				fabs(hd_shadow_dot(up, fwd)) > 1e-5f || sh_angle(cr, fwd) > 1e-2)
				bad++;

			if (a)
			{
				const double turn = sh_angle(right, lastRight);

				worstStep = turn > worstStep ? turn : worstStep;
			}

			for (int i = 0; i < 3; i++)
				lastRight[i] = right[i];
		}

	ok(bad == 0, "shadow: the basis is orthonormal and right-handed inside the limit",
		bad, 0);
	ok(worstStep < 1.0,
		"shadow: half a degree of bearing turns the basis by less than a degree, no flip",
		sh_milli(worstStep), 0);

	// The box round fifteen nodes, standing far from the world's origin.
	float nodes[15 * 3];

	for (int n = 0; n < 15; n++)
	{
		nodes[n * 3 + 0] = 52345.6f + 13.0f * (float)(n % 5);
		nodes[n * 3 + 1] = -3210.4f - 52.0f * (float)n;	// node 0 lowest, node 14 highest
		nodes[n * 3 + 2] = 47890.1f - 9.0f * (float)(n % 3);
	}

	const float dir[3] = { 0.40f, 0.83f, -0.39f };
	float unit[3] = { dir[0], dir[1], dir[2] };
	HDShadowBox box;

	hd_shadow_unit(unit);
	HDShadowLimit(unit);

	const bool made = HDShadowBoxMake(&box, unit, nodes, 15);

	ok(made && box.lowest == nodes[1] && box.highest == nodes[14 * 3 + 1],
		"shadow: the box knows her lowest and highest node", sh_milli(box.lowest),
		sh_milli(nodes[1]));

	const double texel = (double)HD_SHADOW_BOX / HD_SHADOW_MAP;
	const double cr = hd_shadow_dot(box.centre, box.right) / texel;
	const double cu = hd_shadow_dot(box.centre, box.up) / texel;

	/* Within the float rounding of a position 70,000 units from the origin
	 * (a float's step there is 1 / 128 unit, 1 / 512 of a texel). */
	ok(fabs(cr - floor(cr + 0.5)) < 5e-3 && fabs(cu - floor(cu + 0.5)) < 5e-3,
		"shadow: the centre stands on whole texels across the light",
		sh_milli(cr - floor(cr + 0.5)), 0);

	float uvd[3];
	const float midDelta[3] = { box.mid[0] - box.centre[0], box.mid[1] - box.centre[1],
		box.mid[2] - box.centre[2] };

	HDShadowMapUVD(&box, box.centre, uvd);
	ok(fabs(uvd[0] - 0.5f) < 1e-6f && fabs(uvd[1] - 0.5f) < 1e-6f && fabs(uvd[2] - 0.5f) < 1e-6f,
		"shadow: the box's centre lands on u = v = 0.5, depth 0.5", sh_milli(uvd[0] * 1000.0), 500000);
	ok(fabs(hd_shadow_dot(midDelta, box.right)) <= texel * 0.5 + 1e-3 &&
		fabs(hd_shadow_dot(midDelta, box.up)) <= texel * 0.5 + 1e-3 &&
		fabs(hd_shadow_dot(midDelta, box.fwd)) < 1e-2,
		"shadow: her middle is within half a texel of the centre",
		sh_milli(hd_shadow_dot(midDelta, box.right)), 0);

	// One texel to the right is one texel on in u, one up is one back in v.
	{
		float p[3], q[3];

		for (int i = 0; i < 3; i++)
		{
			p[i] = box.centre[i] + box.right[i] * (float)texel;
			q[i] = box.centre[i] + box.up[i] * (float)texel;
		}

		float a[3], b[3];

		HDShadowMapUVD(&box, p, a);
		HDShadowMapUVD(&box, q, b);
		ok(fabs((a[0] - 0.5f) * HD_SHADOW_MAP - 1.0f) < 1e-3f && fabs(a[1] - 0.5f) < 1e-6f &&
			fabs((0.5f - b[1]) * HD_SHADOW_MAP - 1.0f) < 1e-3f && fabs(b[0] - 0.5f) < 1e-6f,
			"shadow: a point one texel to the right lands one texel to the right",
			sh_milli((a[0] - 0.5f) * HD_SHADOW_MAP), 1000);
	}

	// Depth is linear along the light, 0 at the near side, 1 at the far.
	{
		const float steps[5] = { -1024.0f, -400.0f, 0.0f, 512.0f, 1024.0f };
		bool linear = true;

		for (int s = 0; s < 5; s++)
		{
			float p[3], d[3];

			for (int i = 0; i < 3; i++)
				p[i] = box.centre[i] + box.fwd[i] * steps[s];

			HDShadowMapUVD(&box, p, d);

			if (fabs(d[2] - (0.5f + steps[s] / HD_SHADOW_DEPTH)) > 1e-5f)
				linear = false;
		}

		ok(linear, "shadow: depth is linear along the light, 0 to 1 across the box", 0, 0);
	}

	/* The snap holds the outline still: a world point keeps its place inside
	 * its texel while she walks across the light. */
	{
		const float fixed[3] = { 52400.0f, -3100.0f, 47950.0f };
		float first = -1.0f;
		double worst = 0.0;

		for (int step = 0; step < 40; step++)
		{
			float moved[15 * 3];

			for (int n = 0; n < 15; n++)
			{
				moved[n * 3 + 0] = nodes[n * 3 + 0] + 1.37f * (float)step;
				moved[n * 3 + 1] = nodes[n * 3 + 1];
				moved[n * 3 + 2] = nodes[n * 3 + 2] + 0.61f * (float)step;
			}

			HDShadowBox b;
			float m[3];

			HDShadowBoxMake(&b, unit, moved, 15);
			HDShadowMapUVD(&b, fixed, m);

			const double t = m[0] * HD_SHADOW_MAP;
			const float frac = (float)(t - floor(t));

			if (first < 0.0f)
				first = frac;
			else
			{
				double e = fabs(frac - first);

				e = e > 0.5 ? 1.0 - e : e;
				worst = e > worst ? e : worst;
			}
		}

		ok(worst < 0.01, "shadow: with a steady light a point keeps its place in its texel",
			sh_milli(worst), 0);
	}

	/* The two matrices against the one mapping, through a camera far from
	 * the origin: a turned view, view = R p + t. */
	{
		const float yaw = 0.7f, pitch = 0.35f;
		const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
		const float R[9] =
		{
			cy, 0.0f, -sy,
			sy * sp, cp, cy * sp,
			sy * cp, -sp, cy * cp
		};
		const float eye[3] = { 51800.0f, -3900.0f, 47100.0f };
		float t[3];

		for (int i = 0; i < 3; i++)
			t[i] = -(R[i * 3 + 0] * eye[0] + R[i * 3 + 1] * eye[1] + R[i * 3 + 2] * eye[2]);

		float viewToMap[16], cast[16];

		HDShadowViewToMap(&box, R, t, viewToMap);
		HDShadowCastMatrix(&box, R, t, cast);

		double worstMap = 0.0, worstCast = 0.0, worstBack = 0.0;

		for (int s = 0; s < 64; s++)
		{
			const float p[3] =
			{
				box.centre[0] + 700.0f * sinf(0.37f * s), box.centre[1] - 600.0f * cosf(0.71f * s),
				box.centre[2] + 650.0f * sinf(1.13f * s)
			};
			float v[3], back[3], want[3];

			for (int i = 0; i < 3; i++)
				v[i] = R[i * 3 + 0] * p[0] + R[i * 3 + 1] * p[1] + R[i * 3 + 2] * p[2] + t[i];

			HDShadowViewToWorld(R, t, v, back);
			HDShadowMapUVD(&box, p, want);

			for (int i = 0; i < 3; i++)
			{
				const double e = fabs((double)back[i] - p[i]);

				worstBack = e > worstBack ? e : worstBack;
			}

			double m[4], c[4];

			for (int col = 0; col < 4; col++)
			{
				m[col] = v[0] * viewToMap[col] + v[1] * viewToMap[4 + col] +
					v[2] * viewToMap[8 + col] + viewToMap[12 + col];
				c[col] = v[0] * cast[col] + v[1] * cast[4 + col] + v[2] * cast[8 + col] +
					cast[12 + col];
			}

			/* The map in texels and in units of depth, and the rasteriser's
			 * rule: Direct3D 9 puts a pixel's centre on whole screen
			 * coordinates, a texel's centre at (i + 0.5) / MAP. */
			for (int i = 0; i < 2; i++)
			{
				const double e = fabs(m[i] - want[i]) * HD_SHADOW_MAP;

				worstMap = e > worstMap ? e : worstMap;
			}

			worstMap = fabs(m[2] - want[2]) * HD_SHADOW_DEPTH > worstMap
				? fabs(m[2] - want[2]) * HD_SHADOW_DEPTH : worstMap;

			const double sx = (c[0] / c[3] + 1.0) * 0.5 * HD_SHADOW_MAP;
			const double sy2 = (1.0 - c[1] / c[3]) * 0.5 * HD_SHADOW_MAP;
			const double eu = fabs((sx + 0.5) / HD_SHADOW_MAP - want[0]) * HD_SHADOW_MAP;
			const double ev = fabs((sy2 + 0.5) / HD_SHADOW_MAP - want[1]) * HD_SHADOW_MAP;
			const double ez = fabs(c[2] / c[3] - want[2]) * HD_SHADOW_DEPTH;

			worstCast = eu > worstCast ? eu : worstCast;
			worstCast = ev > worstCast ? ev : worstCast;
			worstCast = ez > worstCast ? ez : worstCast;
		}

		ok(worstBack < 0.02, "shadow: view space goes back into the world exactly",
			sh_milli(worstBack), 0);
		ok(worstMap < 0.01,
			"shadow: the view-to-map matrix gives the mapping to a hundredth of a texel",
			sh_milli(worstMap), 0);
		ok(worstCast < 0.01,
			"shadow: the cast matrix rasterises a point into the texel the lookup reads",
			sh_milli(worstCast), 0);
	}
}

/* The gate: every stored value, every reason, in the order asked. */
static void shadow_gate()
{
	int wrong = 0;

	for (long stored = 0; stored <= 7; stored++)
	{
		const int g = HDShadowGate(stored, true, true, true, true);

		if ((stored == HD_SHADOW_MODE_PROJECTED) != (g == HD_SHADOW_GATE_OK))
			wrong++;

		if (stored != HD_SHADOW_MODE_PROJECTED && g != HD_SHADOW_GATE_OPTION)
			wrong++;

		// HD off answers the option first for a classic state, hd_off for 5.
		const int off = HDShadowGate(stored, false, false, false, false);

		if (off != (stored == HD_SHADOW_MODE_PROJECTED ? HD_SHADOW_GATE_HD_OFF
			: HD_SHADOW_GATE_OPTION))
			wrong++;
	}

	ok(wrong == 0, "shadow: only the stored value 5 lets the projected shadow run",
		wrong, 0);

	const long p = HD_SHADOW_MODE_PROJECTED;

	ok(HDShadowGate(p, false, true, true, true) == HD_SHADOW_GATE_HD_OFF &&
		HDShadowGate(p, true, false, true, true) == HD_SHADOW_GATE_CLASSIC_LARA &&
		HDShadowGate(p, true, true, false, true) == HD_SHADOW_GATE_CLASSIC_ROOM &&
		HDShadowGate(p, true, true, true, false) == HD_SHADOW_GATE_TARGET &&
		HDShadowGate(p, true, false, false, false) == HD_SHADOW_GATE_CLASSIC_LARA &&
		HDShadowGate(p, true, true, false, false) == HD_SHADOW_GATE_CLASSIC_ROOM,
		"shadow: each reason in its order - hd_off, classic_lara, classic_room, target",
		HDShadowGate(p, true, true, false, false), HD_SHADOW_GATE_CLASSIC_ROOM);

	ok(!strcmp(HDShadowGateWord(HD_SHADOW_GATE_OK), "ok") &&
		!strcmp(HDShadowGateWord(HD_SHADOW_GATE_OPTION), "option") &&
		!strcmp(HDShadowGateWord(HD_SHADOW_GATE_HD_OFF), "hd_off") &&
		!strcmp(HDShadowGateWord(HD_SHADOW_GATE_CLASSIC_LARA), "classic_lara") &&
		!strcmp(HDShadowGateWord(HD_SHADOW_GATE_CLASSIC_ROOM), "classic_room") &&
		!strcmp(HDShadowGateWord(HD_SHADOW_GATE_TARGET), "target"),
		"shadow: the gate's words are the log's", 0, 0);
}

/* The receive terms: each is 1, 0 and monotone between, at the numbers of
 * the table. */
static void shadow_terms()
{
	// The edge: 1 in the middle and inside the band, 0 on the border and
	// outside, never rising towards the border.
	int bad = 0;
	float last = 2.0f;

	for (int i = 0; i <= 1000; i++)
	{
		const float u = 0.5f - 0.5f * (float)i / 1000.0f;	// the middle to the border
		const float e = HDShadowEdgeTerm(u, 0.5f);

		if (e > last + 1e-6f || e < 0.0f || e > 1.0f)
			bad++;

		last = e;
	}

	ok(bad == 0 && HDShadowEdgeTerm(0.5f, 0.5f) == 1.0f &&
		HDShadowEdgeTerm(0.5f * HD_SHADOW_EDGE + 0.001f, 0.5f) == 1.0f &&
		HDShadowEdgeTerm(0.0f, 0.5f) == 0.0f && HDShadowEdgeTerm(1.0f, 0.3f) == 0.0f &&
		HDShadowEdgeTerm(-0.2f, 0.5f) == 0.0f && HDShadowEdgeTerm(0.5f, 1.3f) == 0.0f,
		"shadow: the edge term is 1 inside, 0 on and past the border, monotone between",
		bad, 0);

	ok(sh_milli(HDShadowEdgeTerm(0.5f * HD_SHADOW_EDGE, 0.5f)) == 1000 &&
		sh_milli(HDShadowEdgeTerm(0.25f * HD_SHADOW_EDGE, 0.5f)) == 500 &&
		sh_milli(HDShadowEdgeTerm(0.5f, 1.0f - 0.25f * HD_SHADOW_EDGE)) == 500,
		"shadow: the edge fades over the outer EDGE of the half box (0.05 of the map's side)",
		sh_milli(HDShadowEdgeTerm(0.25f * HD_SHADOW_EDGE, 0.5f)), 500);

	// The height: 1 above her and down to FULL, 0 from END, monotone.
	bad = 0;
	last = 2.0f;

	for (int i = -200; i <= 3000; i++)
	{
		const float h = HDShadowHeightTerm((float)i);

		if (h > last + 1e-6f || h < 0.0f || h > 1.0f)
			bad++;

		last = h;
	}

	ok(bad == 0 && HDShadowHeightTerm(-500.0f) == 1.0f && HDShadowHeightTerm(0.0f) == 1.0f &&
		HDShadowHeightTerm(HD_SHADOW_FULL) == 1.0f && HDShadowHeightTerm(HD_SHADOW_END) == 0.0f &&
		HDShadowHeightTerm(HD_SHADOW_END + 4096.0f) == 0.0f &&
		sh_milli(HDShadowHeightTerm(0.5f * (HD_SHADOW_FULL + HD_SHADOW_END))) == 500,
		"shadow: the height term is 1 down to FULL, 0 from END, linear and monotone between",
		bad, 0);

	// The facing: 0 turned away, 1 from FACING on, monotone.
	bad = 0;
	last = -1.0f;

	for (int i = -1000; i <= 1000; i++)
	{
		const float f = HDShadowFacingTerm((float)i / 1000.0f);

		if (f < last - 1e-6f || f < 0.0f || f > 1.0f)
			bad++;

		last = f;
	}

	ok(bad == 0 && HDShadowFacingTerm(-1.0f) == 0.0f && HDShadowFacingTerm(0.0f) == 0.0f &&
		HDShadowFacingTerm(HD_SHADOW_FACING) == 1.0f && HDShadowFacingTerm(1.0f) == 1.0f &&
		sh_milli(HDShadowFacingTerm(0.5f * HD_SHADOW_FACING)) == 500,
		"shadow: the facing term is 0 turned away, 1 from FACING on, monotone between",
		bad, 0);
}

/* The lookup's weights and its continuity: a straight edge of casting
 * texels moved under the position by fractions of a texel. */
static void shadow_lookup()
{
	// The weights of one axis sum to 3 for every position in the texel.
	int bad = 0;

	for (int i = 0; i < 100; i++)
	{
		float w[4];

		HDShadowTapWeights((float)i / 100.0f, w);

		if (fabs((double)w[0] + w[1] + w[2] + w[3] - 3.0) > 1e-6 || w[0] < 0.0f || w[3] < 0.0f)
			bad++;
	}

	ok(bad == 0, "shadow: the four tap weights of an axis sum to 3, the block of 16 to 9", bad, 0);

	const int n = 64;
	std::vector<float> all((size_t)(n * n), 0.25f), none((size_t)(n * n), 1.0f);

	ok(sh_milli(HDShadowLookup(&all[0], n, 0.5f, 0.5f, 0.6f)) == 1000 &&
		sh_milli(HDShadowLookup(&none[0], n, 0.5f, 0.5f, 0.6f)) == 0 &&
		sh_milli(HDShadowLookup(&none[0], n, 0.5f, 0.5f, 3.0f)) == 0,
		"shadow: a map cast everywhere gives 1, the clear map 0 - even behind the box",
		sh_milli(HDShadowLookup(&all[0], n, 0.5f, 0.5f, 0.6f)), 1000);

	// The bias: a caster at the pixel's own depth, or within BIAS of it,
	// does not cast; one further than BIAS nearer the light does.
	const float bias = HD_SHADOW_BIAS / HD_SHADOW_DEPTH;
	std::vector<float> at((size_t)(n * n), 0.5f);

	ok(sh_milli(HDShadowLookup(&at[0], n, 0.5f, 0.5f, 0.5f)) == 0 &&
		sh_milli(HDShadowLookup(&at[0], n, 0.5f, 0.5f, 0.5f + 0.5f * bias)) == 0 &&
		sh_milli(HDShadowLookup(&at[0], n, 0.5f, 0.5f, 0.5f + 2.0f * bias)) == 1000,
		"shadow: a receiver takes the shadow only BIAS behind the caster",
		sh_milli(HDShadowLookup(&at[0], n, 0.5f, 0.5f, 0.5f + 2.0f * bias)), 1000);

	/* A straight edge: texels left of column 32 cast. The position walks from
	 * column 26 to column 38 in steps of a hundredth of a texel; the result
	 * falls from 1 to 0, never rises, and moves by at most a little per step
	 * - no jump where a texel ends. */
	std::vector<float> half((size_t)(n * n), 1.0f);

	for (int y = 0; y < n; y++)
		for (int x = 0; x < 32; x++)
			half[(size_t)(y * n + x)] = 0.25f;

	bad = 0;

	float prev = HDShadowLookup(&half[0], n, 26.0f / n, 0.5f, 0.6f);
	double worst = 0.0;

	for (int i = 1; i <= 1200; i++)
	{
		const float u = (26.0f + (float)i * 0.01f) / n;
		const float now = HDShadowLookup(&half[0], n, u, 0.5f, 0.6f);
		const double step = fabs((double)now - prev);

		if (now > prev + 1e-6f)
			bad++;

		worst = step > worst ? step : worst;
		prev = now;
	}

	ok(bad == 0 && worst < 0.01 &&
		sh_milli(HDShadowLookup(&half[0], n, 26.0f / n, 0.5f, 0.6f)) == 1000 &&
		sh_milli(HDShadowLookup(&half[0], n, 38.0f / n, 0.5f, 0.6f)) == 0,
		"shadow: across a straight edge the lookup falls 1 to 0, monotone, under 0.01 per "
		"hundredth of a texel", (long)(worst * 100000.0), 0);

	// And it is symmetric about the edge: half way across it, it is half.
	ok(sh_milli(HDShadowLookup(&half[0], n, 32.0f / n, 0.5f, 0.6f)) == 500,
		"shadow: the lookup is one half exactly on the edge",
		sh_milli(HDShadowLookup(&half[0], n, 32.0f / n, 0.5f, 0.6f)), 500);
}

/* The receive constants: the shader's rows give the one mapping, the height
 * term and the light's direction, through a turned camera far from the
 * origin; and the reach holds every point that can take the shadow. */
static void shadow_receive()
{
	const float d2r = 3.14159265f / 180.0f;
	const float yaw = 37.0f * d2r, pitch = 21.0f * d2r;
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	// Rows: right, down, forward of a camera turned by yaw and pitch.
	const float R[9] =
	{
		cy, 0.0f, -sy,
		-sy * sp, cp, -cy * sp,
		sy * cp, sp, cy * cp
	};
	const float camera[3] = { 61234.0f, -3200.0f, 48211.0f };
	float t[3];

	for (int i = 0; i < 3; i++)
		t[i] = -(R[i * 3 + 0] * camera[0] + R[i * 3 + 1] * camera[1] + R[i * 3 + 2] * camera[2]);

	float nodes[15 * 3];

	for (int k = 0; k < 15; k++)
	{
		nodes[k * 3 + 0] = 61500.0f + 9.0f * (float)k;
		nodes[k * 3 + 1] = -1200.0f + 55.0f * (float)k;
		nodes[k * 3 + 2] = 49000.0f - 4.0f * (float)k;
	}

	float dir[3] = { 0.4f, 1.0f, -0.3f };

	hd_shadow_unit(dir);

	HDShadowBox box;

	HDShadowBoxMake(&box, dir, nodes, 15);

	float regs[HD_SHADOW_RECEIVE_COUNT * 4];

	HDShadowReceiveConstants(&box, R, t, 0.33f, regs);

	double worstMap = 0.0, worstHeight = 0.0;

	for (int k = 0; k < 200; k++)
	{
		const float p[3] =
		{
			box.centre[0] + (float)((k * 37) % 2000) - 1000.0f,
			box.lowest + (float)((k * 53) % 2400) - 300.0f,
			box.centre[2] + (float)((k * 71) % 2000) - 1000.0f
		};
		float v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		for (int i = 0; i < 3; i++)
			v[i] = R[i * 3 + 0] * p[0] + R[i * 3 + 1] * p[1] + R[i * 3 + 2] * p[2] + t[i];

		float want[3];

		HDShadowMapUVD(&box, p, want);

		for (int c = 0; c < 3; c++)
		{
			const double got = (double)v[0] * regs[c * 4] + (double)v[1] * regs[c * 4 + 1] +
				(double)v[2] * regs[c * 4 + 2] + regs[c * 4 + 3];
			const double err = fabs(got - want[c]) * HD_SHADOW_MAP;

			worstMap = err > worstMap ? err : worstMap;
		}

		const double h = (double)v[0] * regs[12] + (double)v[1] * regs[13] +
			(double)v[2] * regs[14] + regs[15];
		const double hs = h < 0.0 ? 0.0 : (h > 1.0 ? 1.0 : h);
		const double err = fabs(hs - HDShadowHeightTerm(p[1] - box.lowest));

		worstHeight = err > worstHeight ? err : worstHeight;
	}

	ok(worstMap < 0.01 && worstHeight < 1e-3,
		"shadow: the receive rows give the one mapping (within 0.01 texel) and the height "
		"term, through a turned camera 60,000 units out", (long)(worstMap * 1000.0), 0);

	// Towards the light, in view space: the light's reverse turned by R.
	float back[3];

	for (int i = 0; i < 3; i++)
		back[i] = -(R[i * 3 + 0] * box.fwd[0] + R[i * 3 + 1] * box.fwd[1] + R[i * 3 + 2] * box.fwd[2]);

	ok(fabs((double)regs[16] - back[0]) < 1e-5 && fabs((double)regs[17] - back[1]) < 1e-5 &&
		fabs((double)regs[18] - back[2]) < 1e-5 &&
		fabs(sqrt((double)regs[16] * regs[16] + (double)regs[17] * regs[17] +
			(double)regs[18] * regs[18]) - 1.0) < 1e-5 &&
		regs[19] == 1.0f / HD_SHADOW_FACING && regs[20] == 0.33f &&
		regs[21] == 1.0f / (0.5f * HD_SHADOW_EDGE) && regs[22] == HD_SHADOW_BIAS / HD_SHADOW_DEPTH &&
		regs[23] == (float)HD_SHADOW_MAP && regs[24] == 1.0f / (float)HD_SHADOW_MAP,
		"shadow: the light's row is unit and points back up the light; the scalars are the "
		"table's", 0, 0);

	/* The reach: every point inside the box's cross-section, on the far side
	 * of her middle's near half and with any height term left, lies in it. */
	float rmin[3], rmax[3];
	int outside = 0;

	HDShadowReach(&box, rmin, rmax);

	for (int k = 0; k < 4000; k++)
	{
		const float a = ((float)((k * 7919) % 1000) / 1000.0f - 0.5f) * HD_SHADOW_BOX;
		const float c = ((float)((k * 104729) % 1000) / 1000.0f - 0.5f) * HD_SHADOW_BOX;
		const float f = -0.5f * HD_SHADOW_DEPTH + (float)((k * 31) % 6000);
		float p[3];

		for (int i = 0; i < 3; i++)
			p[i] = box.centre[i] + a * box.right[i] + c * box.up[i] + f * box.fwd[i];

		if (HDShadowHeightTerm(p[1] - box.lowest) <= 0.0f)
			continue;

		if (!HDShadowBoxesTouch(p, p, rmin, rmax))
			outside++;
	}

	const float far[3] = { box.centre[0], box.lowest + HD_SHADOW_END + 600.0f, box.centre[2] };

	ok(outside == 0 && HDShadowBoxesTouch(box.centre, box.centre, rmin, rmax) &&
		!HDShadowBoxesTouch(far, far, rmin, rmax),
		"shadow: the reach holds every point that can take the shadow, and not a storey "
		"below END", outside, 0);
}

/* The effective mode: the stored values 0..7, HD on and off, and every
 * reason the projected shadow does not run. */
static void shadow_mode()
{
	int bad = 0;

	for (long stored = 0; stored <= 7; stored++)
	{
		const long blob = HDShadowBlobMode(stored);

		if (blob != (stored == 5 ? 3 : stored))
			bad++;

		for (int mask = 0; mask < 16; mask++)
		{
			const bool hd = (mask & 1) != 0, lara = (mask & 2) != 0, room = (mask & 4) != 0,
				target = (mask & 8) != 0;
			const int gate = HDShadowGate(stored, hd, lara, room, target);
			const long effective = HDShadowEffectiveMode(stored, gate);
			const bool runs = stored == 5 && hd && lara && room && target;

			if (effective != (runs ? 5 : blob))
				bad++;
		}
	}

	ok(bad == 0, "shadow: 5 is projected exactly when every gate opens, the faded circle "
		"otherwise; 0..4 and 6, 7 are themselves", bad, 0);

	ok(HDShadowEffectiveMode(5, HD_SHADOW_GATE_HD_OFF) == 3 &&
		HDShadowEffectiveMode(5, HD_SHADOW_GATE_CLASSIC_LARA) == 3 &&
		HDShadowEffectiveMode(5, HD_SHADOW_GATE_CLASSIC_ROOM) == 3 &&
		HDShadowEffectiveMode(5, HD_SHADOW_GATE_TARGET) == 3 &&
		HDShadowEffectiveMode(5, HD_SHADOW_GATE_OK) == 5 &&
		HDShadowEffectiveMode(1, HD_SHADOW_GATE_OPTION) == 1 &&
		HDShadowEffectiveMode(4, HD_SHADOW_GATE_OPTION) == 4,
		"shadow: hd_off, classic_lara, classic_room and target each leave the faded circle",
		HDShadowEffectiveMode(5, HD_SHADOW_GATE_TARGET), 3);

	ok(!strcmp(HDShadowModeWord(1), "original") && !strcmp(HDShadowModeWord(2), "circle") &&
		!strcmp(HDShadowModeWord(3), "faded") && !strcmp(HDShadowModeWord(4), "psx") &&
		!strcmp(HDShadowModeWord(5), "projected") && !strcmp(HDShadowModeWord(0), "circle") &&
		!strcmp(HDShadowModeWord(7), "circle"),
		"shadow: the mode's words are the log's; S_PrintShadow's circle for every other value",
		0, 0);
}

/* Her box's arithmetic written with the table's numbers in place of the
 * box's own extent, depth and map: the functions before the box carried its
 * size. Her box through hd_shadow.h must give these bits exactly. */
static void sh_fixed_centre(HDShadowBox* b)
{
	const double texel = (double)HD_SHADOW_BOX / (double)HD_SHADOW_MAP;
	double cr = 0.0, cu = 0.0, cf = 0.0;

	for (int i = 0; i < 3; i++)
	{
		cr += (double)b->mid[i] * b->right[i];
		cu += (double)b->mid[i] * b->up[i];
		cf += (double)b->mid[i] * b->fwd[i];
	}

	cr = floor(cr / texel + 0.5) * texel;
	cu = floor(cu / texel + 0.5) * texel;

	for (int i = 0; i < 3; i++)
		b->centre[i] = (float)(cr * b->right[i] + cu * b->up[i] + cf * b->fwd[i]);
}

static void sh_fixed_uvd(const HDShadowBox* b, const float* p, float* out)
{
	const float d[3] = { p[0] - b->centre[0], p[1] - b->centre[1], p[2] - b->centre[2] };

	out[0] = 0.5f + hd_shadow_dot(d, b->right) / HD_SHADOW_BOX;
	out[1] = 0.5f - hd_shadow_dot(d, b->up) / HD_SHADOW_BOX;
	out[2] = 0.5f + hd_shadow_dot(d, b->fwd) / HD_SHADOW_DEPTH;
}

static void sh_fixed_view_to_map(const HDShadowBox* b, const float* R, const float* t, float* m)
{
	const float* axis[3] = { b->right, b->up, b->fwd };
	const double scale[3] = { 1.0 / HD_SHADOW_BOX, -1.0 / HD_SHADOW_BOX,
		1.0 / HD_SHADOW_DEPTH };

	for (int i = 0; i < 16; i++)
		m[i] = 0.0f;

	for (int col = 0; col < 3; col++)
	{
		double ra[3];
		double c = 0.0;

		for (int i = 0; i < 3; i++)
		{
			ra[i] = (double)R[i * 3 + 0] * axis[col][0] +
				(double)R[i * 3 + 1] * axis[col][1] + (double)R[i * 3 + 2] * axis[col][2];
			c -= (double)t[i] * ra[i] + (double)b->centre[i] * axis[col][i];
		}

		for (int i = 0; i < 3; i++)
			m[i * 4 + col] = (float)(ra[i] * scale[col]);

		m[12 + col] = (float)(0.5 + c * scale[col]);
	}

	m[15] = 1.0f;
}

static void sh_fixed_cast(const HDShadowBox* b, const float* R, const float* t, float* m)
{
	float uvd[16];

	sh_fixed_view_to_map(b, R, t, uvd);

	for (int row = 0; row < 4; row++)
	{
		m[row * 4 + 0] = 2.0f * uvd[row * 4 + 0];
		m[row * 4 + 1] = -2.0f * uvd[row * 4 + 1];
		m[row * 4 + 2] = uvd[row * 4 + 2];
		m[row * 4 + 3] = uvd[row * 4 + 3];
	}

	m[12] += -1.0f - 1.0f / (float)HD_SHADOW_MAP;
	m[13] += 1.0f + 1.0f / (float)HD_SHADOW_MAP;
}

static void sh_fixed_reach(const HDShadowBox* b, float* mins, float* maxs)
{
	const float half = 0.5f * HD_SHADOW_BOX;
	const float bottom = b->lowest + HD_SHADOW_END;
	const float spread = half * (fabsf(b->right[1]) + fabsf(b->up[1]));
	const float down = b->fwd[1] > 0.1f ? b->fwd[1] : 0.1f;
	const float nearF = -0.5f * HD_SHADOW_DEPTH;
	float farF = (bottom - b->centre[1] + spread) / down;

	farF = farF > nearF ? farF : nearF;

	for (int i = 0; i < 3; i++)
	{
		mins[i] = 3.0e38f;
		maxs[i] = -3.0e38f;
	}

	for (int corner = 0; corner < 8; corner++)
	{
		const float a = (corner & 1) ? half : -half;
		const float c = (corner & 2) ? half : -half;
		const float f = (corner & 4) ? farF : nearF;

		for (int i = 0; i < 3; i++)
		{
			const float p = b->centre[i] + a * b->right[i] + c * b->up[i] + f * b->fwd[i];

			mins[i] = p < mins[i] ? p : mins[i];
			maxs[i] = p > maxs[i] ? p : maxs[i];
		}
	}

	maxs[1] = maxs[1] < bottom ? maxs[1] : bottom;
}

/* A camera turned by `yaw` and `pitch` (degrees) standing at `at`: R rows
 * right, down, forward, and t = -R at. */
static void sh_camera(float yawDeg, float pitchDeg, const float* at, float* R, float* t)
{
	const float d2r = 3.14159265f / 180.0f;
	const float cy = cosf(yawDeg * d2r), sy = sinf(yawDeg * d2r);
	const float cp = cosf(pitchDeg * d2r), sp = sinf(pitchDeg * d2r);
	const float rows[9] =
	{
		cy, 0.0f, -sy,
		-sy * sp, cp, -cy * sp,
		sy * cp, sp, cy * cp
	};

	for (int i = 0; i < 9; i++)
		R[i] = rows[i];

	for (int i = 0; i < 3; i++)
		t[i] = -(R[i * 3 + 0] * at[0] + R[i * 3 + 1] * at[1] + R[i * 3 + 2] * at[2]);
}

/* Fifteen nodes of a standing figure round (x, y, z), node 0 the lowest. */
static void sh_figure(float x, float y, float z, float* nodes)
{
	for (int n = 0; n < 15; n++)
	{
		nodes[n * 3 + 0] = x + 11.0f * (float)(n % 4) - 17.0f;
		nodes[n * 3 + 1] = y - 51.0f * (float)n;
		nodes[n * 3 + 2] = z + 7.0f * (float)(n % 3) - 7.0f;
	}
}

/* The second map's box: the numbers, the centre, the texel, the snap, the
 * depth; and her box through the same functions gives the bits the table's
 * numbers give. */
static void shadow_others_box()
{
	ok(HD_SHADOW_OTHERS_MAP == 2048 && HD_SHADOW_OTHERS_BOX == 8192.0f &&
		HD_SHADOW_OTHERS_DEPTH == 8192.0f && HD_SHADOW_OTHERS_FULL == 1024.0f &&
		HD_SHADOW_OTHERS_END == 1280.0f,
		"shadow others: the table's numbers - map 2048, box 8192, depth 8192, "
			"1024, 1280",
		HD_SHADOW_OTHERS_MAP, 2048);
	ok(HD_SHADOW_OTHERS_BOX / HD_SHADOW_OTHERS_MAP == HD_SHADOW_BOX / HD_SHADOW_MAP &&
		HD_SHADOW_OTHERS_BOX / HD_SHADOW_OTHERS_MAP == 4.0f,
		"shadow others: a texel of the second map is a texel of hers, 4 units",
		sh_milli(HD_SHADOW_OTHERS_BOX / HD_SHADOW_OTHERS_MAP), 4000);

	float nodes[15 * 3];
	float unit[3] = { 0.40f, 0.83f, -0.39f };

	sh_figure(52345.6f, -3210.4f, 47890.1f, nodes);
	hd_shadow_unit(unit);
	HDShadowLimit(unit);

	HDShadowBox her, others;

	HDShadowBoxMake(&her, unit, nodes, 15);
	HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
		HD_SHADOW_OTHERS_MAP);

	ok(her.extent == HD_SHADOW_BOX && her.depth == HD_SHADOW_DEPTH && her.map == HD_SHADOW_MAP &&
		others.extent == HD_SHADOW_OTHERS_BOX && others.depth == HD_SHADOW_OTHERS_DEPTH &&
		others.map == HD_SHADOW_OTHERS_MAP,
		"shadow others: each box carries its own extent, depth and map", others.map,
		HD_SHADOW_OTHERS_MAP);

	ok(!memcmp(her.right, others.right, sizeof(her.right)) &&
		!memcmp(her.up, others.up, sizeof(her.up)) &&
		!memcmp(her.fwd, others.fwd, sizeof(her.fwd)) &&
		!memcmp(her.mid, others.mid, sizeof(her.mid)) &&
		her.lowest == others.lowest && her.highest == others.highest,
		"shadow others: the same light, the same basis, her middle and her heights", 0, 0);

	// With texels of her size the centre is hers to the bit.
	ok(!memcmp(her.centre, others.centre, sizeof(her.centre)),
		"shadow others: with 4-unit texels the second box's centre is hers, bit for bit",
		sh_milli(others.centre[0] - her.centre[0]), 0);

	float uvd[3];

	HDShadowMapUVD(&others, others.centre, uvd);
	ok(fabs(uvd[0] - 0.5f) < 1e-6f && fabs(uvd[1] - 0.5f) < 1e-6f && fabs(uvd[2] - 0.5f) < 1e-6f,
		"shadow others: the centre lands on u = v = 0.5, depth 0.5", sh_milli(uvd[0] * 1000.0),
		500000);

	// One texel, 4 units, to the right is 1 / 2048 on in u; one up is one back in v.
	{
		float p[3], q[3], a[3], b[3];

		for (int i = 0; i < 3; i++)
		{
			p[i] = others.centre[i] + others.right[i] * 4.0f;
			q[i] = others.centre[i] + others.up[i] * 4.0f;
		}

		HDShadowMapUVD(&others, p, a);
		HDShadowMapUVD(&others, q, b);
		ok(fabs((a[0] - 0.5f) * HD_SHADOW_OTHERS_MAP - 1.0f) < 1e-2f && fabs(a[1] - 0.5f) < 1e-6f &&
			fabs((0.5f - b[1]) * HD_SHADOW_OTHERS_MAP - 1.0f) < 1e-2f && fabs(b[0] - 0.5f) < 1e-6f,
			"shadow others: 4 units to the right is one texel of 2048 to the right",
			sh_milli((a[0] - 0.5f) * HD_SHADOW_OTHERS_MAP), 1000);
	}

	// Four sectors to every side: the box's edge is 4096 units from the centre.
	{
		float p[3], q[3], a[3], b[3];

		for (int i = 0; i < 3; i++)
		{
			p[i] = others.centre[i] + others.right[i] * 4096.0f;
			q[i] = others.centre[i] - others.up[i] * 4096.0f;
		}

		HDShadowMapUVD(&others, p, a);
		HDShadowMapUVD(&others, q, b);
		ok(fabs(a[0] - 1.0f) < 1e-5f && fabs(b[1] - 1.0f) < 1e-5f,
			"shadow others: 4096 units across the light is the box's edge, u or v 1",
			sh_milli(a[0] * 1000.0), 1000000);
	}

	// Depth is linear along the light, 0 at -4096, 1 at +4096.
	{
		const float steps[5] = { -4096.0f, -1500.0f, 0.0f, 2048.0f, 4096.0f };
		bool linear = true;

		for (int s = 0; s < 5; s++)
		{
			float p[3], d[3];

			for (int i = 0; i < 3; i++)
				p[i] = others.centre[i] + others.fwd[i] * steps[s];

			HDShadowMapUVD(&others, p, d);

			if (fabs(d[2] - (0.5f + steps[s] / HD_SHADOW_OTHERS_DEPTH)) > 1e-5f)
				linear = false;
		}

		ok(linear, "shadow others: depth is linear along the light, 0 to 1 over 8192 units",
			0, 0);
	}

	/* The snap of a box round her with texels of another size - 16 units, an
	 * 8192 box on a 512 map: its centre stands on whole 16-unit texels, and
	 * within half of one of her middle. */
	{
		HDShadowBox coarse;

		HDShadowBoxAround(&coarse, &her, 8192.0f, 8192.0f, 512);

		const double cr = hd_shadow_dot(coarse.centre, coarse.right) / 16.0;
		const double cu = hd_shadow_dot(coarse.centre, coarse.up) / 16.0;
		const float midDelta[3] = { coarse.mid[0] - coarse.centre[0],
			coarse.mid[1] - coarse.centre[1], coarse.mid[2] - coarse.centre[2] };

		ok(fabs(cr - floor(cr + 0.5)) < 5e-3 && fabs(cu - floor(cu + 0.5)) < 5e-3 &&
			fabs(hd_shadow_dot(midDelta, coarse.right)) <= 8.0 + 1e-3 &&
			fabs(hd_shadow_dot(midDelta, coarse.up)) <= 8.0 + 1e-3 &&
			fabs(hd_shadow_dot(midDelta, coarse.fwd)) < 1e-2,
			"shadow others: a box of 16-unit texels snaps its centre to whole texels of its own",
			sh_milli(cr - floor(cr + 0.5)), 0);
	}

	// The cast matrix lands a point in the texel whose centre its (u, v) is.
	{
		const float at[3] = { 52000.0f, -4700.0f, 46100.0f };
		float R[9], t[3], clip[16];
		int off = 0;

		sh_camera(37.0f, 21.0f, at, R, t);
		HDShadowCastMatrix(&others, R, t, clip);

		for (int k = 0; k < 64; k++)
		{
			const float p[3] =
			{
				others.centre[0] + (float)((k * 977) % 7000) - 3500.0f,
				others.centre[1] + (float)((k * 331) % 3000) - 1500.0f,
				others.centre[2] + (float)((k * 613) % 7000) - 3500.0f
			};
			float v[3], want[3];

			for (int i = 0; i < 3; i++)
				v[i] = R[i * 3 + 0] * p[0] + R[i * 3 + 1] * p[1] + R[i * 3 + 2] * p[2] + t[i];

			HDShadowMapUVD(&others, p, want);

			double c[4];

			for (int col = 0; col < 4; col++)
				c[col] = (double)v[0] * clip[0 * 4 + col] + (double)v[1] * clip[1 * 4 + col] +
					(double)v[2] * clip[2 * 4 + col] + clip[3 * 4 + col];

			const double sx = (c[0] / c[3] + 1.0) * 0.5 * HD_SHADOW_OTHERS_MAP;
			const double sy = (1.0 - c[1] / c[3]) * 0.5 * HD_SHADOW_OTHERS_MAP;

			if (fabs((sx + 0.5) / HD_SHADOW_OTHERS_MAP - want[0]) * HD_SHADOW_OTHERS_MAP > 0.05 ||
				fabs((sy + 0.5) / HD_SHADOW_OTHERS_MAP - want[1]) * HD_SHADOW_OTHERS_MAP > 0.05 ||
				fabs(c[2] / c[3] - want[2]) * HD_SHADOW_OTHERS_DEPTH > 0.5)
				off++;
		}

		ok(off == 0, "shadow others: the cast matrix puts a point into the texel of its (u, v)",
			off, 0);
	}

	// Inside the box: its place's (u, v) and depth within 0..1, edges included.
	{
		float in[3], edge[3], out1[3], out2[3], deep[3];

		for (int i = 0; i < 3; i++)
		{
			in[i] = others.centre[i] + others.right[i] * 3000.0f - others.up[i] * 2000.0f;
			edge[i] = others.centre[i] + others.right[i] * 4095.0f;
			out1[i] = others.centre[i] + others.right[i] * 4097.0f;
			out2[i] = others.centre[i] - others.up[i] * 4200.0f;
			deep[i] = others.centre[i] + others.fwd[i] * 4200.0f;
		}

		volatile float zero = 0.0f;
		const float notANumber[3] = { zero / zero, others.centre[1], others.centre[2] };

		ok(HDShadowBoxHolds(&others, others.centre, 0.0f) && HDShadowBoxHolds(&others, in, 0.0f) &&
			HDShadowBoxHolds(&others, edge, 0.0f) && !HDShadowBoxHolds(&others, out1, 0.0f) &&
			!HDShadowBoxHolds(&others, out2, 0.0f) && !HDShadowBoxHolds(&others, deep, 0.0f) &&
			!HDShadowBoxHolds(&others, notANumber, 0.0f),
			"shadow others: a place inside the box is held, one past its edge or along the "
			"light or not a number is not", 0, 0);
	}

	/* Her box through the box's own size against the table's numbers: the
	 * centre, the mapping of a point, the two matrices, the receive block and
	 * the reach, bit for bit, over many lights, figures and cameras. */
	{
		int differ = 0;

		for (int k = 0; k < 400; k++)
		{
			const float slant = (float)(k % 12) * 4.5f * 3.14159265f / 180.0f;
			const float bearing = (float)k * 0.137f;
			float d[3] = { sinf(slant) * cosf(bearing), cosf(slant), sinf(slant) * sinf(bearing) };
			float fig[15 * 3];

			sh_figure(-60000.0f + 317.3f * (float)k, -8000.0f + 41.7f * (float)(k % 97),
				70000.0f - 211.9f * (float)k, fig);

			HDShadowBox b, old;

			HDShadowBoxMake(&b, d, fig, 15);
			old = b;
			sh_fixed_centre(&old);

			if (memcmp(b.centre, old.centre, sizeof(b.centre)))
				differ++;

			const float at[3] = { fig[0] + 900.0f, fig[1] - 700.0f, fig[2] - 1100.0f };
			float R[9], t[3];

			sh_camera((float)(k * 7 % 360), (float)(k % 50) - 25.0f, at, R, t);

			float m1[16], m2[16], c1[16], c2[16];

			HDShadowViewToMap(&b, R, t, m1);
			sh_fixed_view_to_map(&b, R, t, m2);
			HDShadowCastMatrix(&b, R, t, c1);
			sh_fixed_cast(&b, R, t, c2);

			if (memcmp(m1, m2, sizeof(m1)) || memcmp(c1, c2, sizeof(c1)))
				differ++;

			const float p[3] = { fig[0] + 300.0f * sinf((float)k), fig[1] + 250.0f,
				fig[2] - 280.0f * cosf((float)k) };
			float u1[3], u2[3];

			HDShadowMapUVD(&b, p, u1);
			sh_fixed_uvd(&b, p, u2);

			if (memcmp(u1, u2, sizeof(u1)))
				differ++;

			float regs[HD_SHADOW_RECEIVE_COUNT * 4];

			HDShadowReceiveConstants(&b, R, t, 0.3375f, regs);

			if (regs[22] != HD_SHADOW_BIAS / HD_SHADOW_DEPTH ||
				regs[23] != (float)HD_SHADOW_MAP || regs[24] != 1.0f / (float)HD_SHADOW_MAP)
				differ++;

			for (int c = 0; c < 3; c++)
				if (regs[c * 4 + 0] != m2[0 * 4 + c] || regs[c * 4 + 1] != m2[1 * 4 + c] ||
					regs[c * 4 + 2] != m2[2 * 4 + c] || regs[c * 4 + 3] != m2[3 * 4 + c])
					differ++;

			float r1[6], r2[6];

			HDShadowReach(&b, r1, r1 + 3);
			sh_fixed_reach(&b, r2, r2 + 3);

			if (memcmp(r1, r2, sizeof(r1)))
				differ++;
		}

		ok(differ == 0,
			"shadow others: her box through its own size gives the table's bits - centre, "
			"mapping, matrices, receive block, reach", differ, 0);
	}
}

/* The second box grown by OTHERS_MARGIN, at the fixed slant the game draws
 * with: a place up to the margin beyond any of the four sides across the
 * light, and beyond the near or far side along it, is held, one a few units
 * past it is not. And no
 * part of a caster lies inside the box once its feet have left the grown one:
 * for feet just past the margin anywhere along each side, a head up to 2160
 * units above them projects outside the box's u or v - on the u = 1 and
 * v = 0 sides too, where the upper body lies inward of the feet. Without the
 * margin a 760 unit tall caster with its feet at u 1.0009 still reaches
 * 0.035 into the box there. */
static void shadow_others_margin()
{
	ok(HD_SHADOW_OTHERS_MARGIN == 0.1f, "shadow others: the table's margin, 0.1 of the box",
		sh_milli(HD_SHADOW_OTHERS_MARGIN), 100);

	float nodes[15 * 3];
	float dir[3];

	HDShadowFallback(dir);
	sh_figure(-4321.5f, 1234.25f, 8765.75f, nodes);

	HDShadowBox her, others;

	HDShadowBoxMake(&her, dir, nodes, 15);
	HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
		HD_SHADOW_OTHERS_MAP);

	const float half = 0.5f * HD_SHADOW_OTHERS_BOX;
	const float grown = half + HD_SHADOW_OTHERS_MARGIN * HD_SHADOW_OTHERS_BOX;
	const auto at = [&](float a, float c, float f, float* p)
	{
		for (int i = 0; i < 3; i++)
			p[i] = others.centre[i] + a * others.right[i] + c * others.up[i] + f * others.fwd[i];
	};

	// The four sides, as (right, up) unit steps outward; up = +1 is v = 0.
	const float side[4][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
	int held = 0;

	for (int s = 0; s < 4; s++)
	{
		float in[3], out[3];

		at(side[s][0] * (grown - 4.0f), side[s][1] * (grown - 4.0f), 0.0f, in);
		at(side[s][0] * (grown + 4.0f), side[s][1] * (grown + 4.0f), 0.0f, out);

		if (HDShadowBoxHolds(&others, in, HD_SHADOW_OTHERS_MARGIN) &&
			!HDShadowBoxHolds(&others, out, HD_SHADOW_OTHERS_MARGIN) &&
			!HDShadowBoxHolds(&others, in, 0.0f))
			held++;
	}

	float nearSide[3], farSide[3], within[3];
	const float grownDepth = 0.5f * HD_SHADOW_OTHERS_DEPTH +
		HD_SHADOW_OTHERS_MARGIN * HD_SHADOW_OTHERS_DEPTH;

	at(0.0f, 0.0f, -grownDepth - 4.0f, nearSide);
	at(0.0f, 0.0f, grownDepth + 4.0f, farSide);
	at(0.0f, 0.0f, grownDepth - 4.0f, within);

	ok(held == 4 && !HDShadowBoxHolds(&others, nearSide, HD_SHADOW_OTHERS_MARGIN) &&
		!HDShadowBoxHolds(&others, farSide, HD_SHADOW_OTHERS_MARGIN) &&
		HDShadowBoxHolds(&others, within, HD_SHADOW_OTHERS_MARGIN),
		"shadow others: the grown box holds a place up to the margin past each side, not "
		"one beyond it, along the light as across it", held, 4);

	int reaching = 0, heads = 0;

	for (int s = 0; s < 4; s++)
		for (float along = -grown; along <= grown; along += 128.0f)
			for (float h = 0.0f; h <= 2160.0f; h += 40.0f)
			{
				const float a = side[s][0] != 0.0f ? side[s][0] * (grown + 4.0f) : along;
				const float c = side[s][1] != 0.0f ? side[s][1] * (grown + 4.0f) : along;
				float feet[3], head[3], uvd[3];

				at(a, c, 0.0f, feet);

				for (int i = 0; i < 3; i++)
					head[i] = feet[i];

				head[1] -= h;
				HDShadowMapUVD(&others, head, uvd);
				heads++;

				if (uvd[0] >= 0.0f && uvd[0] <= 1.0f && uvd[1] >= 0.0f && uvd[1] <= 1.0f)
					reaching++;
			}

	ok(heads > 10000 && reaching == 0,
		"shadow others: beyond the grown box no part of a caster up to 2160 units tall reaches "
		"into the box on any side", reaching, 0);

	// The case without the margin: feet at u 1.0009, the head 760 above them.
	float feet[3], head[3], uvd[3];

	at(half + 0.0009f * HD_SHADOW_OTHERS_BOX, 0.0f, 0.0f, feet);

	for (int i = 0; i < 3; i++)
		head[i] = feet[i];

	head[1] -= 760.0f;
	HDShadowMapUVD(&others, head, uvd);

	ok(!HDShadowBoxHolds(&others, feet, 0.0f) &&
		HDShadowBoxHolds(&others, feet, HD_SHADOW_OTHERS_MARGIN) &&
		uvd[0] > 0.96f && uvd[0] < 0.97f,
		"shadow others: a caster with its feet just past the u = 1 side still reaches 0.035 "
		"into the box, and the grown box holds it", sh_milli(uvd[0]), 966);
}

/* Who casts into the second map: the rule as a pure function of its seven
 * facts, every combination. */
static void shadow_others_rule()
{
	int wrong = 0, casters = 0;

	for (int m = 0; m < 128; m++)
	{
		const bool blob = (m & 1) != 0, runs = (m & 2) != 0, step = (m & 4) != 0,
			target = (m & 8) != 0, hd = (m & 16) != 0, mirrored = (m & 32) != 0,
			inside = (m & 64) != 0;
		const bool casts = HDShadowOtherCasts(blob, runs, step, target, hd, mirrored, inside);

		if (casts != (blob && runs && step && target && hd && !mirrored && inside))
			wrong++;

		casters += casts ? 1 : 0;
	}

	ok(wrong == 0 && casters == 1,
		"shadow others: an item casts exactly when it has a blob, is HD, not mirrored, inside "
		"the box, her step ran, the shadow runs and the target exists", wrong, 0);

	// Each fact alone takes the casting away.
	ok(HDShadowOtherCasts(true, true, true, true, true, false, true) &&
		!HDShadowOtherCasts(false, true, true, true, true, false, true) &&
		!HDShadowOtherCasts(true, false, true, true, true, false, true) &&
		!HDShadowOtherCasts(true, true, false, true, true, false, true) &&
		!HDShadowOtherCasts(true, true, true, false, true, false, true) &&
		!HDShadowOtherCasts(true, true, true, true, false, false, true) &&
		!HDShadowOtherCasts(true, true, true, true, true, true, true) &&
		!HDShadowOtherCasts(true, true, true, true, true, false, false),
		"shadow others: no blob, no shadow, no step, no target, classic, mirrored or outside "
		"each leave the item out", 0, 0);
}

/* The blob's gate, the rule's other face: never both, never neither inside
 * the box; outside it the blob stays exactly where it does not give way. */
static void shadow_others_gate()
{
	int both = 0, neither = 0, outside = 0, today = 0;

	for (int m = 0; m < 128; m++)
	{
		const bool blob = (m & 1) != 0, runs = (m & 2) != 0, step = (m & 4) != 0,
			target = (m & 8) != 0, hd = (m & 16) != 0, mirrored = (m & 32) != 0,
			inside = (m & 64) != 0;
		const bool casts = HDShadowOtherCasts(blob, runs, step, target, hd, mirrored, inside);
		const bool drawn = blob && !HDShadowBlobGivesWay(runs, step, target, hd, mirrored);

		if (casts && drawn)
			both++;

		if (blob && inside && !casts && !drawn)
			neither++;

		if (!inside && casts)
			outside++;

		// Without every fact of the frame the blob is drawn as it always was.
		if (blob && !(runs && step && target && hd && !mirrored) && !drawn)
			today++;
	}

	ok(both == 0, "shadow others: no item has its blob and the projected shadow at once",
		both, 0);
	ok(neither == 0,
		"shadow others: inside the box an item with a blob has the one or the other", neither, 0);
	ok(outside == 0, "shadow others: an item outside the box casts nothing", outside, 0);
	ok(today == 0,
		"shadow others: wherever one fact is missing the blob is drawn as before", today, 0);

	ok(!HDShadowBlobGivesWay(false, false, false, false, false) &&
		!HDShadowBlobGivesWay(false, true, true, true, false) &&
		!HDShadowBlobGivesWay(true, false, true, true, false) &&
		!HDShadowBlobGivesWay(true, true, false, true, false) &&
		!HDShadowBlobGivesWay(true, true, true, false, false) &&
		!HDShadowBlobGivesWay(true, true, true, true, true) &&
		HDShadowBlobGivesWay(true, true, true, true, false),
		"shadow others: the blob gives way only to an HD item, not mirrored, with her step, the "
		"shadow and the target", 0, 0);
}

/* The second map's distance term: how far a receiver lies behind a tap's
 * caster along the light. */
static void shadow_others_behind()
{
	int bad = 0;
	float last = 2.0f;

	for (int i = -500; i <= 4000; i++)
	{
		const float b = HDShadowBehindTerm((float)i);

		if (b > last + 1e-6f || b < 0.0f || b > 1.0f)
			bad++;

		last = b;
	}

	ok(bad == 0 && HDShadowBehindTerm(0.0f) == 1.0f &&
		HDShadowBehindTerm(HD_SHADOW_OTHERS_FULL) == 1.0f &&
		HDShadowBehindTerm(HD_SHADOW_OTHERS_END) == 0.0f &&
		HDShadowBehindTerm(HD_SHADOW_OTHERS_END + 4096.0f) == 0.0f &&
		sh_milli(HDShadowBehindTerm(0.5f * (HD_SHADOW_OTHERS_FULL + HD_SHADOW_OTHERS_END))) == 500,
		"shadow others: the distance term is 1 up to OTHERS_FULL, 0 from OTHERS_END, linear and "
		"monotone between", bad, 0);

	/* A creature's head 760 units above its feet, seen along the fixed light
	 * (30 degrees off straight down): the floor where the head's shadow falls
	 * lies 760 / cos 30 = 878 units behind the head along the light, and 878
	 * - 760 cos 30 = 219 behind the creature's place, its feet, from which
	 * the term measures - inside FULL, the whole shadow. */
	const float cs = cosf(HD_SHADOW_FALLBACK_SLANT * 3.14159265f / 180.0f);
	const float headBehind = 760.0f / cs - 760.0f * cs;

	ok(headBehind < HD_SHADOW_OTHERS_FULL && HDShadowBehindTerm(headBehind) == 1.0f,
		"shadow others: a head 760 above the feet at 30 degrees shadows the floor at the feet in "
		"full", (long)headBehind, 219);

	/* The tallest blob carrier, the Roman god, 1546 units tall in its rest
	 * pose (Tr5Level::frameBounds): its top shadows the floor at its feet
	 * 446 behind its place, whole; so does every shorter one. */
	const float godBehind = 1546.0f / cs - 1546.0f * cs;

	ok(HDShadowBehindTerm(godBehind) == 1.0f,
		"shadow others: the Roman god's top (1546 tall) shadows the floor at "
			"its feet in full",
		(long)godBehind, 446);

	/* A floor about a storey under the caster's feet - OTHERS_END, 1280
	 * units below its place along the light, 1108 straight down at 30
	 * degrees - takes nothing of any tap: every tap measures from the place. */
	int lit = 0;

	for (int k = 0; k <= 100; k++)
		if (HDShadowBehindTerm(HD_SHADOW_OTHERS_END + 20.0f * (float)k) > 0.0f)
			lit++;

	ok(lit == 0, "shadow others: a floor OTHERS_END or more behind the feet takes nothing", lit, 0);
}

/* The second map's lookup: hers, tap for tap, with the distance term on each
 * casting tap. */
static void shadow_others_lookup()
{
	const int n = 64;
	const float units = HD_SHADOW_OTHERS_DEPTH;
	std::vector<float> at((size_t)(n * n), 0.25f), none((size_t)(n * n), 1.0f);

	// A map cast everywhere at depth 0.25: the result is the distance term of
	// how far the pixel lies behind, for every distance.
	int bad = 0;

	for (int i = 0; i <= 3200; i += 16)
	{
		const float depth = 0.25f + (float)i / units;
		const float got = HDShadowLookupBehind(&at[0], n, 0.5f, 0.5f, depth, units);
		const float want = (float)i > HD_SHADOW_BIAS ? HDShadowBehindTerm((float)i) : 0.0f;

		if (fabs((double)got - want) > 2e-4)
			bad++;
	}

	ok(bad == 0 && HDShadowLookupBehind(&none[0], n, 0.5f, 0.5f, 0.9f, units) == 0.0f,
		"shadow others: under a caster the lookup is the distance term of how far behind, 0 "
		"within BIAS, and the clear map gives 0", bad, 0);

	/* The same caster with its place - the green channel - 600 units further
	 * along the light than the part the map holds, a head over its feet:
	 * the term measures from the place, so the pixel takes more than the
	 * part's own distance would give, and nothing from OTHERS_END behind the
	 * place on. */
	std::vector<float> feet((size_t)(n * n), 0.25f + 600.0f / units);
	int fromPlace = 0, more = 0;

	for (int i = 0; i <= 3200; i += 16)
	{
		const float depth = 0.25f + (float)i / units;
		const float got = HDShadowLookupBehind(&at[0], n, 0.5f, 0.5f, depth,
			units, &feet[0]);
		const float want = (float)i > HD_SHADOW_BIAS ?
			HDShadowBehindTerm((float)i - 600.0f) : 0.0f;

		if (fabs((double)got - want) > 2e-4)
			fromPlace++;

		if (got > HDShadowLookupBehind(&at[0], n, 0.5f, 0.5f, depth, units) +
			1e-4f)
			more++;
	}

	/* More from OTHERS_FULL behind the part, where the part's own term would
	 * start to fall, to 600 + OTHERS_END, where the place's ends. */
	ok(fromPlace == 0 && more > 0 && HDShadowLookupBehind(&at[0], n, 0.5f,
		0.5f, 0.25f + (600.0f + HD_SHADOW_OTHERS_END) / units, units,
		&feet[0]) == 0.0f,
		"shadow others: each tap measures from its caster's place, and "
		"from OTHERS_END behind the place on gives nothing", fromPlace, 0);

	/* With every caster within FULL the term is 1 on every tap, and the
	 * lookup is her own lookup, bit for bit: the same taps, weights, clamp
	 * and bias (in the same depth units). A straight edge walked over by
	 * hundredths of a texel. */
	std::vector<float> half((size_t)(n * n), 1.0f);

	for (int y = 0; y < n; y++)
		for (int x = 0; x < 32; x++)
			half[(size_t)(y * n + x)] = 0.25f;

	int differ = 0;

	for (int i = 0; i <= 1200; i++)
	{
		const float u = (26.0f + (float)i * 0.01f) / n;
		const float v = (20.0f + (float)(i % 97) * 0.13f) / n;
		const float mine = HDShadowLookupBehind(&half[0], n, u, v, 0.4f, HD_SHADOW_DEPTH);
		const float hers = HDShadowLookup(&half[0], n, u, v, 0.4f);

		if (memcmp(&mine, &hers, sizeof(float)) != 0)
			differ++;
	}

	ok(differ == 0,
		"shadow others: with every caster inside FULL the second lookup is her lookup, bit for bit",
		differ, 0);

	/* Two casters at different heights side by side: texels left of column
	 * 32 hold a caster 1000 units in front of the pixel (term 1), the rest a
	 * caster 1216 in front (term 0.25). Half way across the edge the lookup
	 * is the mean of the two. */
	std::vector<float> two((size_t)(n * n), 0.0f);
	const float depth = 0.6f;

	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			two[(size_t)(y * n + x)] = depth - (x < 32 ? 1000.0f :
				1216.0f) / units;

	const float mid = HDShadowLookupBehind(&two[0], n, 32.0f / n, 0.5f, depth, units);

	ok(sh_milli(HDShadowLookupBehind(&two[0], n, 26.0f / n, 0.5f, depth, units)) == 1000 &&
		sh_milli(HDShadowLookupBehind(&two[0], n, 38.0f / n, 0.5f, depth, units)) == 250 &&
		sh_milli(mid) == 625,
		"shadow others: each tap takes its own caster's distance - 1 and 0.25 either side of an "
		"edge, their mean on it", sh_milli(mid), 625);
}

/* The factor where both maps are read: the darker of the two shadows, never
 * their product, and her own factor where the second gives nothing. */
static void shadow_others_factor()
{
	int notHers = 0, wrong = 0, darker = 0;

	for (int i = 0; i <= 100; i++)
		for (int j = 0; j <= 100; j++)
		{
			const float s = 0.05f + 0.4f * (float)(i % 11) / 10.0f;
			const float hers = (float)i / 100.0f;
			const float theirs = (float)j / 100.0f;
			const float f = HDShadowReceiveFactor(s, hers, theirs);
			const float big = hers > theirs ? hers : theirs;

			if (j == 0)
			{
				const float one = 1.0f - s * hers;

				if (memcmp(&f, &one, sizeof(float)) != 0)
					notHers++;
			}

			if (fabs((double)f - (1.0 - (double)s * big)) > 1e-6)
				wrong++;

			if (f < 1.0f - s - 1e-6f)
				darker++;
		}

	ok(notHers == 0,
		"shadow others: where the second map gives nothing the factor is her own, bit for bit",
		notHers, 0);
	ok(wrong == 0 && darker == 0,
		"shadow others: the factor is 1 - strength * max(hers, theirs), never below 1 - strength",
		wrong + darker, 0);

	// The overlap of two full shadows is as dark as one, not as their product.
	const float one = HDShadowReceiveFactor(0.45f, 1.0f, 0.0f);
	const float both = HDShadowReceiveFactor(0.45f, 1.0f, 1.0f);

	ok(one == both && both == 1.0f - 0.45f && HDShadowReceiveFactor(0.45f, 0.0f, 1.0f) == both,
		"shadow others: two full shadows overlapping give the factor of one", sh_milli(both),
		sh_milli(one));
}

/* The two-map shader's registers: the second box's rows through a turned
 * camera far from the origin, the distance term's line, the second block
 * right after hers, her block silenced, and the second reach. */
static void shadow_others_receive()
{
	const float d2r = 3.14159265f / 180.0f;
	const float yaw = -52.0f * d2r, pitch = 17.0f * d2r;
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	const float R[9] =
	{
		cy, 0.0f, -sy,
		-sy * sp, cp, -cy * sp,
		sy * cp, sp, cy * cp
	};
	const float camera[3] = { 58113.0f, -5120.0f, 71440.0f };
	float t[3];

	for (int i = 0; i < 3; i++)
		t[i] = -(R[i * 3 + 0] * camera[0] + R[i * 3 + 1] * camera[1] + R[i * 3 + 2] * camera[2]);

	float nodes[15 * 3];

	for (int k = 0; k < 15; k++)
	{
		nodes[k * 3 + 0] = 59000.0f + 7.0f * (float)k;
		nodes[k * 3 + 1] = -2300.0f + 52.0f * (float)k;
		nodes[k * 3 + 2] = 70500.0f - 6.0f * (float)k;
	}

	float dir[3];

	HDShadowFallback(dir);

	HDShadowBox her, box;

	HDShadowBoxMake(&her, dir, nodes, 15);
	HDShadowBoxAround(&box, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
		HD_SHADOW_OTHERS_MAP);

	float regs[HD_SHADOW_RECEIVE2_COUNT * 4];

	HDShadowReceiveOthersConstants(&box, R, t, regs);

	double worstMap = 0.0, worstTerm = 0.0;

	for (int k = 0; k < 300; k++)
	{
		const float p[3] =
		{
			box.centre[0] + (float)((k * 37) % 8000) - 4000.0f,
			box.centre[1] + (float)((k * 53) % 6000) - 3000.0f,
			box.centre[2] + (float)((k * 71) % 8000) - 4000.0f
		};
		float v[3];

		for (int i = 0; i < 3; i++)
			v[i] = R[i * 3 + 0] * p[0] + R[i * 3 + 1] * p[1] + R[i * 3 + 2] * p[2] + t[i];

		float want[3];

		HDShadowMapUVD(&box, p, want);

		for (int c = 0; c < 3; c++)
		{
			const double got = (double)v[0] * regs[c * 4] + (double)v[1] * regs[c * 4 + 1] +
				(double)v[2] * regs[c * 4 + 2] + regs[c * 4 + 3];
			const double err = fabs(got - want[c]) * HD_SHADOW_OTHERS_MAP;

			worstMap = err > worstMap ? err : worstMap;
		}

		// The shader's line for the distance term against the function.
		const float tap = want[2] - (float)((k * 113) % 3000) / HD_SHADOW_OTHERS_DEPTH;
		double line = (double)regs[17] - (double)regs[16] * ((double)want[2] - tap);

		line = line < 0.0 ? 0.0 : (line > 1.0 ? 1.0 : line);

		const double err = fabs(line - HDShadowBehindTerm((want[2] - tap) * HD_SHADOW_OTHERS_DEPTH));

		worstTerm = err > worstTerm ? err : worstTerm;
	}

	ok(worstMap < 0.01 && worstTerm < 1e-4,
		"shadow others: the second block's rows give the second box's mapping (within 0.01 texel) "
		"and its line gives the distance term, through a turned camera 70,000 units out",
		(long)(worstMap * 1000.0), 0);

	ok(regs[12] == HD_SHADOW_BIAS / HD_SHADOW_OTHERS_DEPTH && regs[13] == (float)HD_SHADOW_OTHERS_MAP &&
		regs[14] == 1.0f / (float)HD_SHADOW_OTHERS_MAP && regs[15] == 0.0f &&
		regs[18] == 1.0f / HD_SHADOW_OTHERS_FADE && regs[19] == 0.0f,
		"shadow others: the second block's scalars are the second box's bias, side and texel, and "
		"the fade's slope", 0, 0);

	// The second block follows hers without a gap or an overlap, inside the
	// 224 constants of ps_3_0.
	ok(HD_SHADOW_RECEIVE2_FIRST == HD_SHADOW_RECEIVE_FIRST + HD_SHADOW_RECEIVE_COUNT &&
		HD_SHADOW_RECEIVE2_FIRST == 103 &&
		HD_SHADOW_RECEIVE2_FIRST + HD_SHADOW_RECEIVE2_COUNT <= 224,
		"shadow others: the second block is c103 - c107, right after hers",
		HD_SHADOW_RECEIVE2_FIRST, 103);

	/* Her block silenced: u is -1 wherever the pixel is, so her edge term is
	 * 0; nothing else of her block changes. */
	float hersRegs[HD_SHADOW_RECEIVE_COUNT * 4], silent[HD_SHADOW_RECEIVE_COUNT * 4];

	HDShadowReceiveConstants(&her, R, t, 0.33f, hersRegs);
	memcpy(silent, hersRegs, sizeof(silent));
	HDShadowReceiveWithoutHer(silent);

	int edges = 0;

	for (int k = 0; k < 200; k++)
	{
		const float v[3] = { (float)(k * 17 % 900) - 450.0f, (float)(k * 29 % 700) - 350.0f,
			100.0f + (float)(k * 41 % 3000) };
		const float u = v[0] * silent[0] + v[1] * silent[1] + v[2] * silent[2] + silent[3];
		const float vv = v[0] * silent[4] + v[1] * silent[5] + v[2] * silent[6] + silent[7];

		if (HDShadowEdgeTerm(u, vv) != 0.0f)
			edges++;
	}

	ok(edges == 0 && memcmp(silent + 4, hersRegs + 4, sizeof(float) * (HD_SHADOW_RECEIVE_COUNT - 1) * 4) == 0,
		"shadow others: her block silenced gives her edge term 0 everywhere and keeps the rest",
		edges, 0);

	/* The second reach: every point of the second box's cross-section from
	 * its near side to OTHERS_END beyond the farthest place a caster may
	 * have, OTHERS_MARGIN of the depth past its far side, lies in it. */
	float rmin[3], rmax[3];
	int outside = 0;

	HDShadowReachOthers(&box, rmin, rmax);

	for (int k = 0; k < 4000; k++)
	{
		const float a = ((float)((k * 7919) % 1000) / 1000.0f - 0.5f) * HD_SHADOW_OTHERS_BOX;
		const float c = ((float)((k * 104729) % 1000) / 1000.0f - 0.5f) * HD_SHADOW_OTHERS_BOX;
		const float f = -0.5f * HD_SHADOW_OTHERS_DEPTH +
			(float)((k * 31) % 1000) / 1000.0f *
			((1.0f + HD_SHADOW_OTHERS_MARGIN) * HD_SHADOW_OTHERS_DEPTH +
			HD_SHADOW_OTHERS_END);
		float p[3];

		for (int i = 0; i < 3; i++)
			p[i] = box.centre[i] + a * box.right[i] + c * box.up[i] + f * box.fwd[i];

		if (!HDShadowBoxesTouch(p, p, rmin, rmax))
			outside++;
	}

	const float beyond[3] =
	{
		box.centre[0], box.centre[1] + (0.5f + HD_SHADOW_OTHERS_MARGIN) *
			HD_SHADOW_OTHERS_DEPTH + HD_SHADOW_OTHERS_END +
			0.5f * HD_SHADOW_OTHERS_BOX + 100.0f, box.centre[2]
	};
	float hmin[3], hmax[3];

	HDShadowReach(&her, hmin, hmax);

	ok(outside == 0 && !HDShadowBoxesTouch(beyond, beyond, rmin, rmax) &&
		HDShadowBoxesTouch(hmin, hmax, rmin, rmax),
		"shadow others: the second reach holds every point a caster inside the box can shadow, "
		"not one far below it, and meets her reach", outside, 0);
}

/* The fade of the second map along the light: the term itself, the lookup
 * that carries it, the shader's line for it, and a caster walking out of the
 * box along the light through either side. */
static void shadow_others_fade()
{
	ok(HD_SHADOW_OTHERS_FADE == 0.05f, "shadow fade: the table's number, 0.05 of the depth",
		sh_milli(HD_SHADOW_OTHERS_FADE), 50);

	// The term: 0 at both sides, exactly 1 in the middle and from FADE in,
	// rising to the middle and falling after it, never outside 0..1.
	int bad = 0;
	float last = -1.0f;

	for (int i = 0; i <= 10000; i++)
	{
		const float tap = (float)i / 10000.0f;
		const float f = HDShadowFadeTerm(tap);

		if (f < 0.0f || f > 1.0f)
			bad++;

		if (i <= 5000 ? f < last : f > last)
			bad++;

		if (tap >= HD_SHADOW_OTHERS_FADE + 1e-4f && tap <= 1.0f - HD_SHADOW_OTHERS_FADE - 1e-4f &&
			f != 1.0f)
			bad++;

		last = f;
	}

	ok(bad == 0 && HDShadowFadeTerm(0.0f) == 0.0f && HDShadowFadeTerm(1.0f) == 0.0f &&
		HDShadowFadeTerm(0.5f) == 1.0f && HDShadowFadeTerm(-0.25f) == 0.0f &&
		HDShadowFadeTerm(1.25f) == 0.0f &&
		sh_milli(HDShadowFadeTerm(0.5f * HD_SHADOW_OTHERS_FADE)) == 500 &&
		sh_milli(HDShadowFadeTerm(1.0f - 0.5f * HD_SHADOW_OTHERS_FADE)) == 500,
		"shadow fade: 0 at depth 0 and 1, exactly 1 in the middle and from FADE in, linear and "
		"monotone between", bad, 0);

	// The shader's line, saturate(min(tap, 1 - tap) * c107.z), against the term.
	float nodes[15 * 3];
	float dir[3];

	HDShadowFallback(dir);
	sh_figure(1024.0f, -768.0f, 2048.0f, nodes);

	HDShadowBox her, others;

	HDShadowBoxMake(&her, dir, nodes, 15);
	HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
		HD_SHADOW_OTHERS_MAP);

	const float R[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	const float t[3] = { 0, 0, 0 };
	float regs[HD_SHADOW_RECEIVE2_COUNT * 4];
	int line = 0;

	HDShadowReceiveOthersConstants(&others, R, t, regs);

	for (int i = 0; i <= 1000; i++)
	{
		const float tap = (float)i / 1000.0f;
		const float edge = tap < 1.0f - tap ? tap : 1.0f - tap;
		const float shader = hd_shadow_saturate(edge * regs[18]);

		if (shader != HDShadowFadeTerm(tap))
			line++;
	}

	ok(line == 0, "shadow fade: the shader's line on c107.z is the term, bit for bit", line, 0);

	/* The lookup carries it: a map cast everywhere at a depth near either
	 * side gives the distance term times the fade of that depth. */
	const int n = 32;
	int lookups = 0;

	for (int i = 0; i <= 40; i++)
	{
		const float tap = (float)i * 0.0025f;

		for (int side = 0; side < 2; side++)
		{
			const float at = side ? 1.0f - tap : tap;
			std::vector<float> map((size_t)(n * n), at);
			const float depth = at + 1000.0f / HD_SHADOW_OTHERS_DEPTH;
			const float got = HDShadowLookupBehind(&map[0], n, 0.5f, 0.5f, depth,
				HD_SHADOW_OTHERS_DEPTH);
			const float want = HDShadowBehindTerm(1000.0f) * HDShadowFadeTerm(at);

			if (fabs((double)got - want) > 2e-6)
				lookups++;
		}
	}

	ok(lookups == 0, "shadow fade: the second lookup is the distance term times the fade of "
		"each tap's depth", lookups, 0);

	/* A caster 760 units tall walking out along the light at the fixed
	 * slant, its feet held by the grown box (the mark) step by step: while
	 * the mark holds, its strongest part fades smoothly; on the step the
	 * mark drops, every part of it already lies where the fade is 0 - on
	 * both sides. No frame shows a shadow at full strength next to none. */
	const auto walk = [&](int sign, float* lastHeld, float* jump) -> bool
	{
		float before = -1.0f;
		bool dropped = false;
		bool zeroAtDrop = false;

		*jump = 0.0f;
		*lastHeld = -1.0f;

		for (float f = 0.0f; f <= 0.5f * HD_SHADOW_OTHERS_DEPTH + 2000.0f; f += 8.0f)
		{
			float feet[3];

			for (int i = 0; i < 3; i++)
				feet[i] = others.centre[i] + (float)sign * f * others.fwd[i];

			// The strongest fade over the caster, feet to head.
			float strongest = 0.0f;

			for (float h = 0.0f; h <= 760.0f; h += 10.0f)
			{
				float p[3] = { feet[0], feet[1] - h, feet[2] }, uvd[3];

				HDShadowMapUVD(&others, p, uvd);

				const float fade = HDShadowFadeTerm(uvd[2]);

				strongest = fade > strongest ? fade : strongest;
			}

			const bool held = HDShadowBoxHolds(&others, feet, HD_SHADOW_OTHERS_MARGIN);

			if (held)
			{
				if (before >= 0.0f)
				{
					const float d = fabsf(strongest - before);

					*jump = d > *jump ? d : *jump;
				}

				before = strongest;
				*lastHeld = strongest;
			}
			else if (!dropped)
			{
				dropped = true;
				zeroAtDrop = strongest == 0.0f;
			}
		}

		return dropped && zeroAtDrop;
	};

	float nearLast = 0.0f, nearJump = 0.0f, farLast = 0.0f, farJump = 0.0f;
	const bool nearOk = walk(-1, &nearLast, &nearJump);
	const bool farOk = walk(1, &farLast, &farJump);

	ok(nearOk && farOk && nearLast == 0.0f && farLast == 0.0f,
		"shadow fade: a caster 760 tall at 30 degrees lies wholly in the fade's 0 before its mark "
		"drops, through the near and the far side", sh_milli(nearLast + farLast), 0);
	ok(nearJump < 0.025f && farJump < 0.025f,
		"shadow fade: walking out 8 units a step, its strongest part changes by less than 0.025 a "
		"step", sh_milli(nearJump > farJump ? nearJump : farJump), 0);
}

/* The mark's test along the light: the second box grown by the margin on its
 * near and far side as on its four others, the box itself with margin 0. */
static void shadow_others_holds()
{
	float nodes[15 * 3];
	float dir[3];

	HDShadowFallback(dir);
	sh_figure(-9000.0f, 2500.0f, 40000.0f, nodes);

	HDShadowBox her, others;

	HDShadowBoxMake(&her, dir, nodes, 15);
	HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
		HD_SHADOW_OTHERS_MAP);

	const auto along = [&](float f, float* p)
	{
		for (int i = 0; i < 3; i++)
			p[i] = others.centre[i] + f * others.fwd[i] + 300.0f * others.right[i] -
				200.0f * others.up[i];
	};

	const float half = 0.5f * HD_SHADOW_OTHERS_DEPTH;
	const float grown = half + HD_SHADOW_OTHERS_MARGIN * HD_SHADOW_OTHERS_DEPTH;
	int wrong = 0;

	for (int s = -1; s <= 1; s += 2)
	{
		float in[3], out[3], edgeIn[3], edgeOut[3];

		along((float)s * (grown - 4.0f), in);
		along((float)s * (grown + 4.0f), out);
		along((float)s * (half - 4.0f), edgeIn);
		along((float)s * (half + 4.0f), edgeOut);

		wrong += HDShadowBoxHolds(&others, in, HD_SHADOW_OTHERS_MARGIN) ? 0 : 1;
		wrong += HDShadowBoxHolds(&others, out, HD_SHADOW_OTHERS_MARGIN) ? 1 : 0;
		wrong += HDShadowBoxHolds(&others, edgeIn, 0.0f) ? 0 : 1;
		wrong += HDShadowBoxHolds(&others, edgeOut, 0.0f) ? 1 : 0;
		wrong += HDShadowBoxHolds(&others, in, 0.0f) ? 1 : 0;
	}

	ok(wrong == 0,
		"shadow holds: along the light the mark holds up to OTHERS_MARGIN of the depth past the "
		"near and the far side, the box itself only inside them", wrong, 0);

	// Margin 0 is the box: depth 0..1 and u, v 0..1, edges included.
	int differ = 0;

	for (int k = 0; k < 4000; k++)
	{
		const float a = ((float)((k * 7919) % 2001) / 1000.0f - 1.0f) * HD_SHADOW_OTHERS_BOX * 0.6f;
		const float c = ((float)((k * 104729) % 2001) / 1000.0f - 1.0f) * HD_SHADOW_OTHERS_BOX * 0.6f;
		const float f = ((float)((k * 31) % 2001) / 1000.0f - 1.0f) * HD_SHADOW_OTHERS_DEPTH * 0.6f;
		float p[3], uvd[3];

		for (int i = 0; i < 3; i++)
			p[i] = others.centre[i] + a * others.right[i] + c * others.up[i] + f * others.fwd[i];

		HDShadowMapUVD(&others, p, uvd);

		const bool box = uvd[0] >= 0.0f && uvd[0] <= 1.0f && uvd[1] >= 0.0f && uvd[1] <= 1.0f &&
			uvd[2] >= 0.0f && uvd[2] <= 1.0f;

		if (box != HDShadowBoxHolds(&others, p, 0.0f))
			differ++;
	}

	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float notANumber[3] = { nan, others.centre[1], others.centre[2] };

	ok(differ == 0 && !HDShadowBoxHolds(&others, notANumber, HD_SHADOW_OTHERS_MARGIN),
		"shadow holds: with margin 0 it is the box itself, and a place that is not a number is "
		"never held", differ, 0);
}

/* Which maps a model takes: the rule as a pure function of what the entry
 * is, which maps were drawn and whether it is a reflection with its
 * mirror's blocks, every combination; the second block silenced; and the
 * reach's sphere test. */
static void shadow_takes()
{
	const int receivers[4] =
	{
		HD_SHADOW_RECEIVER_NONE, HD_SHADOW_RECEIVER_LARA, HD_SHADOW_RECEIVER_BLOB,
		HD_SHADOW_RECEIVER_OTHER
	};
	int wrong = 0, own = 0, empty = 0, mirrorDiffers = 0, noBlocks = 0;

	for (int r = 0; r < 4; r++)
		for (int m = 0; m < 16; m++)
		{
			const int who = receivers[r];
			const bool herDrawn = (m & 1) != 0, othersDrawn = (m & 2) != 0,
				mirrored = (m & 4) != 0, blocks = (m & 8) != 0;
			const int takes = HDShadowReceiverTakes(who, herDrawn, othersDrawn, mirrored, blocks);

			const bool wantHers = herDrawn && (who == HD_SHADOW_RECEIVER_BLOB ||
				who == HD_SHADOW_RECEIVER_OTHER) && (!mirrored || blocks);
			const bool wantOthers = othersDrawn && (who == HD_SHADOW_RECEIVER_LARA ||
				who == HD_SHADOW_RECEIVER_OTHER) && (!mirrored || blocks);

			if (takes != ((wantHers ? HD_SHADOW_TAKES_HERS : 0) |
				(wantOthers ? HD_SHADOW_TAKES_OTHERS : 0)))
				wrong++;

			// Never its own map: Lara never hers, a caster never the second.
			if ((who == HD_SHADOW_RECEIVER_LARA && (takes & HD_SHADOW_TAKES_HERS)) ||
				(who == HD_SHADOW_RECEIVER_BLOB && (takes & HD_SHADOW_TAKES_OTHERS)))
				own++;

			// Nothing to take without a map drawn, for the ring, or for a
			// reflection without its mirror's blocks.
			if (takes && (!(herDrawn || othersDrawn) || who == HD_SHADOW_RECEIVER_NONE ||
				(mirrored && !blocks)))
				empty++;

			// A reflection with its blocks takes what the thing itself takes.
			if (mirrored && blocks &&
				takes != HDShadowReceiverTakes(who, herDrawn, othersDrawn, false, false))
				mirrorDiffers++;

			if (mirrored && !blocks && takes)
				noBlocks++;
		}

	ok(wrong == 0, "shadow takes: the rule over every class, map and reflection, 64 cases",
		wrong, 0);
	ok(own == 0, "shadow takes: Lara never takes her map, a caster never the map it is cast into",
		own, 0);
	ok(empty == 0 && noBlocks == 0,
		"shadow takes: nothing is drawn again with nothing to take - no map, the ring, a "
		"reflection without its mirror's blocks", empty, 0);
	ok(mirrorDiffers == 0,
		"shadow takes: a reflection takes by the class of what it reflects", mirrorDiffers, 0);

	ok(HDShadowReceiverTakes(HD_SHADOW_RECEIVER_LARA, true, true, false, false) ==
		HD_SHADOW_TAKES_OTHERS &&
		HDShadowReceiverTakes(HD_SHADOW_RECEIVER_BLOB, true, true, false, false) ==
		HD_SHADOW_TAKES_HERS &&
		HDShadowReceiverTakes(HD_SHADOW_RECEIVER_OTHER, true, true, false, false) ==
		(HD_SHADOW_TAKES_HERS | HD_SHADOW_TAKES_OTHERS) &&
		HDShadowReceiverTakes(HD_SHADOW_RECEIVER_LARA, true, false, false, false) == 0 &&
		HDShadowReceiverTakes(HD_SHADOW_RECEIVER_BLOB, false, true, false, false) == 0,
		"shadow takes: Lara the second map, a creature hers, a door both; each alone with its map "
		"not drawn takes nothing", 0, 0);

	/* The second block silenced: its u is -1 wherever the pixel is, so the
	 * second box's edge term is 0; nothing else changes. */
	float nodes[15 * 3];
	float dir[3];

	HDShadowFallback(dir);
	sh_figure(300.0f, -1200.0f, -800.0f, nodes);

	HDShadowBox her, others;

	HDShadowBoxMake(&her, dir, nodes, 15);
	HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
		HD_SHADOW_OTHERS_MAP);

	const float R[9] = { 0.8f, 0.0f, -0.6f, 0.0f, 1.0f, 0.0f, 0.6f, 0.0f, 0.8f };
	const float t[3] = { 120.0f, -40.0f, 900.0f };
	float regs[HD_SHADOW_RECEIVE2_COUNT * 4], silent[HD_SHADOW_RECEIVE2_COUNT * 4];

	HDShadowReceiveOthersConstants(&others, R, t, regs);
	memcpy(silent, regs, sizeof(silent));
	HDShadowReceiveWithoutOthers(silent);

	int edges = 0;

	for (int k = 0; k < 200; k++)
	{
		const float v[3] = { (float)(k * 17 % 9000) - 4500.0f, (float)(k * 29 % 7000) - 3500.0f,
			100.0f + (float)(k * 41 % 30000) };
		const float u = v[0] * silent[0] + v[1] * silent[1] + v[2] * silent[2] + silent[3];
		const float vv = v[0] * silent[4] + v[1] * silent[5] + v[2] * silent[6] + silent[7];

		if (HDShadowEdgeTerm(u, vv) != 0.0f)
			edges++;
	}

	ok(edges == 0 && memcmp(silent + 4, regs + 4,
		sizeof(float) * (HD_SHADOW_RECEIVE2_COUNT - 1) * 4) == 0,
		"shadow takes: the second block silenced gives its edge term 0 everywhere and keeps the "
		"rest", edges, 0);

	// The reach's sphere test: inside, touching a face, off a corner, apart.
	const float mins[3] = { 0.0f, 0.0f, 0.0f }, maxs[3] = { 100.0f, 200.0f, 300.0f };
	const float inside[3] = { 50.0f, 50.0f, 50.0f };
	const float face[3] = { 150.0f, 100.0f, 100.0f };
	const float corner[3] = { 130.0f, 240.0f, 300.0f };
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float notANumber[3] = { nan, 0.0f, 0.0f };

	ok(HDShadowSphereTouches(inside, 1.0f, mins, maxs) &&
		HDShadowSphereTouches(face, 50.0f, mins, maxs) &&
		!HDShadowSphereTouches(face, 49.0f, mins, maxs) &&
		HDShadowSphereTouches(corner, 50.0f, mins, maxs) &&
		!HDShadowSphereTouches(corner, 49.9f, mins, maxs) &&
		!HDShadowSphereTouches(notANumber, 1.0e6f, mins, maxs),
		"shadow takes: a sphere touches the reach when it reaches a face or a corner, and not one "
		"unit short of it", 0, 0);
}

/* One view-space point through the receive's blocks, worked in double out
 * of the float registers as the shader works it in its own floats: her u, v,
 * depth and the height term before its saturate, her facing for a view-space
 * normal, and the second map's u, v and depth. */
struct ShMirrorTake
{
	double u, v, d, height, facing;
	double ou, ov, od;
};

static double sh_row(const float* row, const double* v)
{
	return row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3];
}

static ShMirrorTake sh_mirror_take(const float* hers, const float* others, const double* v,
	const double* n)
{
	ShMirrorTake out;

	out.u = sh_row(hers + 0, v);
	out.v = sh_row(hers + 4, v);
	out.d = sh_row(hers + 8, v);
	out.height = sh_row(hers + 12, v);
	out.facing = (n[0] * hers[16] + n[1] * hers[17] + n[2] * hers[18]) * hers[19];
	out.ou = sh_row(others + 0, v);
	out.ov = sh_row(others + 4, v);
	out.od = sh_row(others + 8, v);
	return out;
}

static void sh_view_of(const float* R, const float* t, const double* p, double* v)
{
	for (int i = 0; i < 3; i++)
		v[i] = R[i * 3 + 0] * p[0] + R[i * 3 + 1] * p[1] + R[i * 3 + 2] * p[2] + t[i];
}

static void sh_turn_of(const float* R, const double* n, double* out)
{
	for (int i = 0; i < 3; i++)
		out[i] = R[i * 3 + 0] * n[0] + R[i * 3 + 1] * n[1] + R[i * 3 + 2] * n[2];
}

/* The largest difference between two takes, u, v and depth of both maps in
 * texels of their own map (her depth in units of her box, the second's in
 * its own), the height term and the facing as they are. */
static double sh_take_gap(const ShMirrorTake& a, const ShMirrorTake& b, double* facing)
{
	double gap = 0.0;
	const double terms[8][2] =
	{
		{ a.u, b.u }, { a.v, b.v }, { a.d, b.d }, { a.height, b.height },
		{ a.ou, b.ou }, { a.ov, b.ov }, { a.od, b.od }, { 0.0, 0.0 }
	};
	const double scale[7] =
	{
		(double)HD_SHADOW_MAP, (double)HD_SHADOW_MAP, (double)HD_SHADOW_DEPTH, 1.0,
		(double)HD_SHADOW_OTHERS_MAP, (double)HD_SHADOW_OTHERS_MAP, (double)HD_SHADOW_OTHERS_DEPTH
	};

	for (int i = 0; i < 7; i++)
	{
		const double g = fabs(terms[i][0] - terms[i][1]) * scale[i];

		gap = g > gap ? g : gap;
	}

	*facing = fabs(a.facing - b.facing);
	return gap;
}

/* The mirror's image takes the maps through the reflected view: a point of
 * the copy through the mirror's blocks gives what its reflection gives
 * through the frame's - her u, v and depth, the height term and the facing
 * of the reflected face, and the second map's u, v and depth - for both
 * axes, both sides, cameras far from the origin and points up to six sectors
 * from the glass; reflecting twice gives the view back; and the glass's
 * plane keeps the copy's side and the camera's side test says which side
 * the camera is on. */
static void shadow_mirror_view()
{
	float nodes[15 * 3];
	float dir[3];
	int worst = 0, facings = 0, twice = 0, planes = 0, faces = 0, cases = 0;
	double worstGap = 0.0, worstFacing = 0.0;

	HDShadowFallback(dir);

	for (int axis = 0; axis <= 2; axis += 2)
		for (int side = -1; side <= 1; side += 2)
			for (int cam = 0; cam < 6; cam++)
			{
				const float plane = cam & 1 ? 33792.0f : -30720.0f + 1024.0f * (float)cam;
				const int k = 2 - axis;
				float at[3] = { 0.0f, -1500.0f - 200.0f * (float)cam, 0.0f };
				float figure[3] = { 0.0f, -700.0f, 0.0f };

				// Camera and figure on the room's side of the glass.
				at[axis] = plane - (float)side * (2500.0f + 300.0f * (float)cam);
				at[k] = 70000.0f - 9000.0f * (float)cam;
				figure[axis] = plane - (float)side * 900.0f;
				figure[k] = at[k] + 1800.0f;
				sh_figure(figure[0], figure[1], figure[2], nodes);

				HDShadowBox her, others;
				float R[9], t[3], Rm[9], tm[3], R2[9], t2[3];
				float hers[HD_SHADOW_RECEIVE_COUNT * 4], oth[HD_SHADOW_RECEIVE2_COUNT * 4];
				float mh[HD_SHADOW_RECEIVE_COUNT * 4], mo[HD_SHADOW_RECEIVE2_COUNT * 4];
				float glass[4];

				HDShadowBoxMake(&her, dir, nodes, 15);
				HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
					HD_SHADOW_OTHERS_MAP);
				sh_camera(37.0f * (float)cam - 80.0f, 10.0f + 6.0f * (float)cam, at, R, t);
				HDShadowReceiveConstants(&her, R, t, 0.45f, hers);
				HDShadowReceiveOthersConstants(&others, R, t, oth);
				HDShadowMirrorView(R, t, axis, plane, Rm, tm);
				HDShadowReceiveConstants(&her, Rm, tm, 0.45f, mh);
				HDShadowReceiveOthersConstants(&others, Rm, tm, mo);
				HDShadowMirrorPlane(R, t, axis, plane, side, glass);

				// Twice is the view itself.
				HDShadowMirrorView(Rm, tm, axis, plane, R2, t2);

				for (int i = 0; i < 9; i++)
					twice += R2[i] != R[i] ? 1 : 0;

				for (int i = 0; i < 3; i++)
					twice += fabs((double)t2[i] - t[i]) > 0.02 ? 1 : 0;

				faces += HDShadowMirrorFaces(R, t, axis, plane, side) ? 0 : 1;

				for (int s = 0; s < 64; s++)
				{
					// A world point in front of the glass and a face's normal there.
					double p[3], n[3] =
					{
						0.3 * (double)((s % 5) - 2), -1.0 + 0.4 * (double)(s % 3),
						0.25 * (double)((s % 7) - 3)
					};

					p[axis] = plane - (double)side * (5.0 + 97.0 * (double)s);
					p[1] = -40.0 * (double)(s % 23);
					p[k] = figure[k] - 3000.0 + 101.0 * (double)s;

					const double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

					for (int i = 0; i < 3; i++)
						n[i] /= nl;

					// Its image behind the glass, and the image of the normal.
					double q[3] = { p[0], p[1], p[2] }, m[3] = { n[0], n[1], n[2] };

					q[axis] = 2.0 * plane - p[axis];
					m[axis] = -n[axis];

					double v[3], vq[3], vn[3], vm[3];

					sh_view_of(R, t, p, v);
					sh_view_of(R, t, q, vq);
					sh_turn_of(R, n, vn);
					sh_turn_of(R, m, vm);

					const ShMirrorTake real = sh_mirror_take(hers, oth, v, vn);
					const ShMirrorTake image = sh_mirror_take(mh, mo, vq, vm);
					double facing = 0.0;
					const double gap = sh_take_gap(real, image, &facing);

					worstGap = gap > worstGap ? gap : worstGap;
					worstFacing = facing > worstFacing ? facing : worstFacing;
					worst += gap > 0.02 ? 1 : 0;
					facings += facing > 1e-4 ? 1 : 0;

					// The plane: the image behind the glass, the point in front.
					planes += sh_row(glass, vq) > 0.0 ? 0 : 1;
					planes += sh_row(glass, v) < 0.0 ? 0 : 1;
					cases++;
				}
			}

	ok(worst == 0, "shadow mirror: a point of the copy through the mirror's blocks gives its "
		"reflection's u, v, depth and height through the frame's, both maps, to 0.02 of a texel",
		worst, 0);
	ok(facings == 0, "shadow mirror: the reflected face takes the facing of the face it reflects",
		facings, 0);
	ok(twice == 0, "shadow mirror: the reflected view reflected again is the frame's view", twice,
		0);
	ok(planes == 0 && cases == 1536, "shadow mirror: the glass's plane keeps the copy and leaves "
		"the room in front", planes, 0);
	// A camera at x 40000, the copy at smaller x than the glass at 33792:
	// behind the glass, seen from the room's side. At x 30000 it is behind.
	const float identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	const float front[3] = { -40000.0f, 0.0f, 0.0f }, back[3] = { -30000.0f, 0.0f, 0.0f };

	ok(faces == 0 && HDShadowMirrorFaces(identity, front, 0, 33792.0f, -1) &&
		!HDShadowMirrorFaces(identity, back, 0, 33792.0f, -1),
		"shadow mirror: a camera on the room's side faces the glass, one behind it does not",
		faces, 0);
	printf("    shadow mirror: largest gap %.5f texel, facing %.2e over %d points\n", worstGap,
		worstFacing, cases);
}

/* The reflected view against the engine's own reflection (game\mirror_math.h):
 * a body point drawn through the camera matrix MirrorView reflects lands at a
 * view position whose take through the mirror's blocks is the point's own
 * take through the frame's - the reflections of Lara and the items take the
 * shadows of what they reflect - and the first of MirrorClipPlanes, the
 * copy's side of the glass, is HDShadowMirrorPlane up to its scale. */
static void shadow_mirror_engine()
{
	float nodes[15 * 3];
	float dir[3];
	int worst = 0, glassOff = 0, cases = 0;

	HDShadowFallback(dir);

	for (int axis = 0; axis <= 2; axis += 2)
		for (int c = 0; c < 4; c++)
		{
			const long plane = axis == 0 ? 33792 : 30720;
			const int side = c & 1 ? 1 : -1;
			const int k = 2 - axis;
			float at[3] = { 0.0f, -1600.0f, 0.0f };
			float figure[3] = { 0.0f, -700.0f, 0.0f };

			at[axis] = (float)plane - (float)side * (3000.0f + 500.0f * (float)c);
			at[k] = 74000.0f;
			figure[axis] = (float)plane - (float)side * 1100.0f;
			figure[k] = 75500.0f;
			sh_figure(figure[0], figure[1], figure[2], nodes);

			HDShadowBox her, others;
			float R[9], t[3], Rm[9], tm[3];
			float hers[HD_SHADOW_RECEIVE_COUNT * 4], oth[HD_SHADOW_RECEIVE2_COUNT * 4];
			float mh[HD_SHADOW_RECEIVE_COUNT * 4], mo[HD_SHADOW_RECEIVE2_COUNT * 4];

			HDShadowBoxMake(&her, dir, nodes, 15);
			HDShadowBoxAround(&others, &her, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
				HD_SHADOW_OTHERS_MAP);
			sh_camera(50.0f * (float)c - 60.0f, 20.0f, at, R, t);
			HDShadowReceiveConstants(&her, R, t, 0.45f, hers);
			HDShadowReceiveOthersConstants(&others, R, t, oth);
			HDShadowMirrorView(R, t, axis, (float)plane, Rm, tm);
			HDShadowReceiveConstants(&her, Rm, tm, 0.45f, mh);
			HDShadowReceiveOthersConstants(&others, Rm, tm, mo);

			// The engine's camera matrix: R's rows with the camera's place.
			float cam[12], mirrored[12];

			for (int r = 0; r < 3; r++)
			{
				for (int j = 0; j < 3; j++)
					cam[r * 4 + j] = R[r * 3 + j];

				cam[r * 4 + 3] = at[r];
			}

			memcpy(mirrored, cam, sizeof(cam));
			MirrorView(mirrored, axis, (float)plane, true);

			for (int s = 0; s < 48; s++)
			{
				const double p[3] =
				{
					figure[0] + 37.0 * (double)((s % 9) - 4), -60.0 * (double)(s % 17),
					figure[2] + 41.0 * (double)((s % 11) - 5)
				};
				const double n[3] = { 0.0, -1.0, 0.0 };
				double v[3], vr[3], vn[3], vrn[3];

				// Through the real camera, and through the reflected one.
				for (int r = 0; r < 3; r++)
				{
					v[r] = 0.0;
					vr[r] = 0.0;
					vn[r] = 0.0;
					vrn[r] = 0.0;

					for (int j = 0; j < 3; j++)
					{
						v[r] += cam[r * 4 + j] * (p[j] - cam[j * 4 + 3]);
						vr[r] += mirrored[r * 4 + j] * (p[j] - mirrored[j * 4 + 3]);
						vn[r] += cam[r * 4 + j] * n[j];
						vrn[r] += mirrored[r * 4 + j] * n[j];
					}
				}

				const ShMirrorTake real = sh_mirror_take(hers, oth, v, vn);
				const ShMirrorTake drawn = sh_mirror_take(mh, mo, vr, vrn);
				double facing = 0.0;

				worst += sh_take_gap(real, drawn, &facing) > 0.02 || facing > 1e-4 ? 1 : 0;
				cases++;
			}

			// The engine's clip plane of the copy's side against the glass's.
			const long mins[3] = { plane - 4000, -3000, 70000 };
			const long maxs[3] = { plane + 4000, 0, 80000 };
			float clip[12], glass[4];
			double scale = 0.0, off = 0.0;

			MirrorClipPlanes(mirrored, axis, plane, side, mins, maxs, clip);
			HDShadowMirrorPlane(R, t, axis, (float)plane, side, glass);

			for (int i = 0; i < 3; i++)
				scale += (double)clip[i] * glass[i];

			for (int i = 0; i < 4; i++)
				off = fmax(off, fabs((double)clip[i] - scale * glass[i]) /
					(i == 3 ? 1.0e4 : 1.0));

			glassOff += scale > 0.999 && scale < 1.001 && off < 1e-4 ? 0 : 1;
		}

	ok(worst == 0 && cases == 384,
		"shadow mirror engine: a body point drawn through MirrorView's reflected camera takes, "
		"through the mirror's blocks, what it takes through the frame's", worst, 0);
	ok(glassOff == 0, "shadow mirror engine: MirrorClipPlanes' copy side is the glass's plane",
		glassOff, 0);
}

/* Who is walked for the second map unseen: the rule over all sixteen
 * combinations of its four facts - a drawn item walks as it always walks,
 * a clipped one only when it is HD, its blob gave way and its place lies
 * inside. */
/* The arrival (hd_shadow.h, HDShadowArrive): the shadow's presence comes in
 * over FADE_IN game seconds along 3 f^2 - 2 f^3, the blobs go out as 1 minus
 * it, and a blob that does not fade keeps its colour to the bit. */
static void shadow_arrival()
{
	ok(HDShadowPresence(0.0f) == 0.0f && HDShadowPresence(-1.0f) == 0.0f &&
		HDShadowPresence(HD_SHADOW_FADE_IN) == 1.0f &&
			HDShadowPresence(10.0f) == 1.0f &&
		HDShadowPresence(0.5f * HD_SHADOW_FADE_IN) == 0.5f,
		"shadow arrival: 0 at the start, 0.5 halfway, exactly 1 from FADE_IN "
			"on",
		sh_milli(HDShadowPresence(0.5f * HD_SHADOW_FADE_IN)), 500);

	int bad = 0;
	float last = -1.0f;

	for (int i = 0; i <= 1000; i++)
	{
		const float p = HDShadowPresence(HD_SHADOW_FADE_IN *
			(float)i / 1000.0f);

		if (p < last || p < 0.0f || p > 1.0f)
			bad++;

		last = p;
	}

	// No slope at either end: a hundredth of the way in, 0.0003 of it.
	ok(bad == 0 && HDShadowPresence(0.01f * HD_SHADOW_FADE_IN) < 0.001f &&
		HDShadowPresence(0.99f * HD_SHADOW_FADE_IN) > 0.999f,
		"shadow arrival: monotone, and flat where it leaves 0 and reaches 1",
			bad, 0);

	/* Frame by frame: the first frame it runs adds nothing (the clock starts
	 * there), a frame it does not run sets it back. The frames with the
	 * presence below 1, at 30, 60 and 165 frames a second. */
	const float rates[3] = { 30.0f, 60.0f, 165.0f };
	const long want[3] = { 8, 15, 42 };
	long frames[3] = { 0, 0, 0 };
	float at[3] = { 0.0f, 0.0f, 0.0f };

	for (int r = 0; r < 3; r++)
	{
		float t = 0.0f;
		float p = HDShadowArrive(&t, true, 0.0f);

		for (int f = 0; f < 1000 && p < 1.0f; f++)
		{
			frames[r]++;
			p = HDShadowArrive(&t, true, 1.0f / rates[r]);

			if (fabsf((float)(f + 1) / rates[r] - 0.1f) < 0.5f / rates[r])
				at[r] = p;
		}
	}

	ok(frames[0] == want[0] && frames[1] == want[1] && frames[2] == want[2],
		"shadow arrival: the frames still coming in - 8 at 30 a second, 15 at "
			"60, 42 at 165",
		frames[1], want[1]);
	ok(fabsf(at[0] - at[1]) < 0.002f && fabsf(at[1] - at[2]) < 0.04f,
		"shadow arrival: a tenth of a second in, the same presence at 30, 60 "
			"and 165 frames a "
		"second (within one 165th of a second's step)", sh_milli(at[1]),
			sh_milli(at[0]));

	float t = 0.2f;
	const float stopped = HDShadowArrive(&t, false, 1.0f / 30.0f);
	const float again = HDShadowArrive(&t, true, 0.0f);
	float paused = 0.1f;
	const float p1 = HDShadowArrive(&paused, true, 0.0f);
	const float p2 = HDShadowArrive(&paused, true, -1.0f);

	ok(stopped == 0.0f && again == 0.0f && t == 0.0f && p1 == p2 &&
		paused == 0.1f,
		"shadow arrival: a frame without the shadow sets it back to 0; a "
			"paused frame adds nothing",
		sh_milli(again), 0);

	/* The blob and the shadow add up to one shadow at every step: a full
	 * blob's alpha byte drawn with the weight 1 - presence, as the blob that
	 * gives way is drawn, plus the presence's share of the byte is the whole
	 * byte, within half a step of its rounding. Which weight the engine hands
	 * a blob (hd_object.cpp, HDObjectBlobWeight; hd_laradraw.cpp,
	 * HDShadowFrameBlobWeight) the suites do not reach. */
	int sum = 0;

	for (int i = 0; i <= 100; i++)
	{
		const float p = HDShadowPresence(HD_SHADOW_FADE_IN * (float)i / 100.0f);
		const unsigned long a = HDShadowBlobAlpha(0xFF000000ul, 1.0f - p) >> 24;

		if (fabsf((float)a + 255.0f * p - 255.0f) > 0.501f)
			sum++;
	}

	ok(sum == 0,
		"shadow arrival: the blob's alpha at 1 - presence and the shadow's "
			"presence sum to one shadow",
		sum, 0);

	// The blob's alpha.
	const unsigned long colours[5] = { 0xFF000000ul, 0x4F000000ul, 0x00000000ul,
		0x80000000ul,
		0x7A123456ul };
	int kept = 0;

	for (int i = 0; i < 5; i++)
		kept += HDShadowBlobAlpha(colours[i], 1.0f) == colours[i] &&
			HDShadowBlobAlpha(colours[i], 2.0f) == colours[i] ? 1 : 0;

	ok(kept == 5 && HDShadowBlobAlpha(0xFF000000ul, 0.5f) == 0x80000000ul &&
		HDShadowBlobAlpha(0x4F000000ul, 0.0f) == 0x00000000ul &&
		HDShadowBlobAlpha(0x7A123456ul, 0.25f) == 0x1F123456ul &&
		HDShadowBlobAlpha(0xFF000000ul, -1.0f) == 0x00000000ul,
		"shadow arrival: a blob's alpha times its weight, rounded, the colour "
			"kept; weight 1 the "
		"colour itself", kept, 5);
}

/* The cut (hd_shadow.h, FULL, END, OTHERS_FULL, OTHERS_END): what a floor
 * straight below the feet takes at the fixed light, her part through her
 * height term - measured from her lowest node, 58.6 units above her soles
 * (ModelProbe -shadowroom: her nodes' lowest y 398.0, her soles at 456.6) -
 * and a creature's through its distance term from its place, its feet: a
 * part h above them darkens a floor d below them d / cos 30 + h (1 / cos 30
 * - cos 30) behind the place along the light. */
static void shadow_cut()
{
	const float c = cosf(HD_SHADOW_FALLBACK_SLANT * 3.14159265f / 180.0f);
	const float node = 58.6f;
	const float below[4] = { 1024.0f, 1536.0f, 2048.0f, 2560.0f };
	const long hers[4] = { 11, 0, 0, 0 };
	const long theirs[4] = { 381, 0, 0, 0 };
	int bad = 0;

	for (int i = 0; i < 4; i++)
	{
		bad += sh_milli(HDShadowHeightTerm(below[i] + node)) != hers[i] ? 1 : 0;
		bad += sh_milli(HDShadowBehindTerm(below[i] / c)) != theirs[i] ? 1 : 0;
	}

	ok(bad == 0,
		"shadow cut: a floor 1024, 1536, 2048, 2560 below the feet takes "
		"0.011, 0, 0, 0 of hers and 0.381, 0, 0, 0 of a creature's feet",
		bad, 0);
	ok(HDShadowHeightTerm(0.0f) == 1.0f &&
		HDShadowHeightTerm(512.0f + node) == 1.0f &&
		sh_milli(HDShadowHeightTerm(832.0f)) == 500 &&
			HDShadowHeightTerm(1030.0f + node) == 0.0f,
		"shadow cut: her part whole down to half a block below her soles, none "
			"from a block and 6 units",
		sh_milli(HDShadowHeightTerm(512.0f + node)), 1000);

	/* A creature's whole outline on its own floor, one step (256) and half a
	 * block (512) below its feet: every blob carrier of the thirteen levels
	 * by its rest pose's height (ModelProbe -surveyslots: 691 to 809 the
	 * men, the sniper 907, the skeleton 1199, the hydra 1249) takes 1 on all
	 * three; the Roman god (1546) 1, 1 and 0.947 - its top, half a block
	 * down. */
	const float tall[5] = { 691.0f, 809.0f, 907.0f, 1199.0f, 1249.0f };
	const float drop[3] = { 0.0f, 256.0f, 512.0f };
	int short_of = 0;

	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 3; j++)
			short_of += HDShadowBehindTerm(drop[j] / c +
				tall[i] * (1.0f / c - c)) < 1.0f ? 1 : 0;

	const float god = HDShadowBehindTerm(512.0f / c + 1546.0f * (1.0f / c - c));

	ok(short_of == 0 &&
		HDShadowBehindTerm(256.0f / c + 1546.0f * (1.0f / c - c)) == 1.0f &&
		sh_milli(god) == 947,
		"shadow cut: every blob carrier's whole outline on its floor, a "
		"step and half a block below; the Roman god's top half a block "
		"down at 0.947", short_of, 0);

	/* Her reach ends where her height term does: a group lying END or more
	 * below her lowest node is not drawn again. */
	HDShadowBox box;
	float nodes[3 * 3] = { 1000.0f, 5000.0f, 2000.0f, 1000.0f, 4500.0f, 2000.0f,
		1000.0f,
		4232.0f, 2000.0f };
	float dir[3];
	float mins[3], maxs[3];

	HDShadowFallback(dir);
	HDShadowBoxMake(&box, dir, nodes, 3);
	HDShadowReach(&box, mins, maxs);
	ok(maxs[1] == 5000.0f + HD_SHADOW_END,
		"shadow cut: her reach ends END below her lowest node",
			(long)maxs[1], 6088);
}

static void shadow_walk()
{
	int wrong = 0, unseen = 0;

	for (int m = 0; m < 16; m++)
	{
		const bool hd = (m & 1) != 0, clipped = (m & 2) != 0, gave = (m & 4) != 0,
			inside = (m & 8) != 0;
		const int walk = HDShadowItemWalk(hd, clipped, gave, inside);
		const int want = !clipped ? HD_SHADOW_WALK_DRAWN
			: hd && gave && inside ? HD_SHADOW_WALK_UNSEEN : HD_SHADOW_WALK_NONE;

		wrong += walk != want ? 1 : 0;
		unseen += walk == HD_SHADOW_WALK_UNSEEN ? 1 : 0;
	}

	ok(wrong == 0 && unseen == 1,
		"shadow walk: a drawn item walks as always, a clipped one unseen only when HD, given way "
		"and inside, and none else", wrong, 0);
	ok(HDShadowItemWalk(false, false, false, false) == HD_SHADOW_WALK_DRAWN &&
		HDShadowItemWalk(true, true, true, false) == HD_SHADOW_WALK_NONE &&
		HDShadowItemWalk(false, true, true, true) == HD_SHADOW_WALK_NONE &&
		HDShadowItemWalk(true, true, false, true) == HD_SHADOW_WALK_NONE &&
		HDShadowItemWalk(true, true, true, true) == HD_SHADOW_WALK_UNSEEN,
		"shadow walk: a classic item, one outside the box and one whose blob stays walk nothing "
		"clipped", 0, 0);
}

/* The flyby's heavy-trigger tests on a skip (game\spotcam_math.h), against a
 * model of one frame of CalculateSpotCams in its own order: the spline's
 * place from the frame-start position, the step, the skip's jump to 0x10000,
 * the camera placed there, an armed test made there, the segment end arming
 * the reached camera's test, the refill to cameras [c-1, c, c+1, c+2] and,
 * past the last camera, the SNAPBACK end with its test or the spline to the
 * chase camera. The cameras stand on a line, x only; the model's spline is
 * linear between knots 1 and 2, exact at 0 and 0x10000 as the engine's is.
 * A sequence starts as SP_SNAPCAMERA starts it: knots [c0, c0, c1, c2]. */
struct SpotSim
{
	std::vector<long> x;	// the cameras' places
	std::vector<int> flags;
	long chase;				// the chase camera's place
	long speed;
	int skipFrom, skipTo;	// the frames the skip is held, [from, to)
	bool rule;				// the end test through SpotEndTestCamera
	bool title;
};

struct SpotTest
{
	long at;
	int cam;
};

static std::vector<SpotTest> spot_run(const SpotSim& m)
{
	std::vector<SpotTest> out;
	const int last = (int)m.x.size() - 1;
	long k[4];
	int cur = 1, armed = (m.flags[0] & SPOT_TESTTRIGGER) ? 0 : -1;
	long pos = 0;
	bool toCamera = false;

	for (int i = 0; i < 4; i++)
		k[i] = m.x[i == 0 ? 0 : (i - 1) % (last + 1)];

	for (int f = 0; f < 100000; f++)
	{
		const long cp = k[1] + (long)((long long)(k[2] - k[1]) * pos / 65536);
		const int skip = f >= m.skipFrom && f < m.skipTo && !m.title;

		pos += m.speed;

		if (skip)
			pos = 0x10000;

		if (armed >= 0)
		{
			out.push_back({ cp, armed });
			armed = -1;
		}

		if (pos <= 0x10000 - m.speed)
			continue;

		if (m.flags[(size_t)cur] & SPOT_TESTTRIGGER)
			armed = cur;

		pos = 0;

		long n[4];
		int next = cur - 1;

		for (int i = 0; i < 4; i++, next++)
			n[i] = m.x[(size_t)std::min(next, last)];

		cur++;

		if (cur <= last)
		{
			std::copy(n, n + 4, k);
			continue;
		}

		if ((m.flags[0] & SPOT_SNAPBACK) || toCamera)
		{
			if (armed >= 0)
			{
				const int at = m.rule ? SpotEndTestCamera(m.flags[0], skip,
					m.title, last) : -1;

				out.push_back({ at >= 0 ? m.x[(size_t)at] : cp, armed });
			}

			return out;
		}

		cur--;
		k[0] = m.x[(size_t)cur - 1];
		k[1] = m.x[(size_t)cur];
		k[2] = k[3] = m.chase;
		toCamera = true;
	}

	return out;
}

static void spotcam_end()
{
	printf("the flyby's trigger tests on a skip\n");

	// Five cameras 1024 apart, SNAPBACK on the first, a heavy test on the
	// last three; 655 a frame is the census's commonest speed.
	SpotSim m;

	m.x = { 0, 1024, 2048, 3072, 4096 };
	m.flags = { SPOT_SNAPBACK, 0, SPOT_TESTTRIGGER, SPOT_TESTTRIGGER,
		SPOT_TESTTRIGGER };
	m.chase = 9000;
	m.speed = 655;
	m.skipFrom = m.skipTo = 0;
	m.rule = true;
	m.title = false;

	// Played through: every test at its camera, the last within one step.
	std::vector<SpotTest> t = spot_run(m);
	const long step = 1024 * 2 * 655 / 65536 + 1;

	ok(t.size() == 3 && t[0].at == 2048 && t[1].at == 3072 &&
		t[2].cam == 4 && labs(t[2].at - 4096) <= step,
		"spotcam: played through, each test at its camera", (long)t.size(),
		3);

	// Skipped from the first frame without the rule: the last test stands a
	// camera short - the cause. With the rule it is at the last camera.
	m.skipTo = 1 << 30;
	m.rule = false;
	t = spot_run(m);
	ok(t.size() == 3 && t[0].at == 2048 && t[1].at == 3072 &&
		t[2].cam == 4 && t[2].at == 3072,
		"spotcam: skipped as before, the last test one camera short",
		t.size() == 3 ? t[2].at : -1, 3072);

	m.rule = true;
	t = spot_run(m);
	ok(t.size() == 3 && t[0].at == 2048 && t[1].at == 3072 &&
		t[2].cam == 4 && t[2].at == 4096,
		"spotcam: skipped, the middle tests where played, the last at it",
		t.size() == 3 ? t[2].at : -1, 4096);

	// The title never skips: the same tests as played through.
	m.title = true;
	t = spot_run(m);
	ok(t.size() == 3 && t[2].cam == 4 && labs(t[2].at - 4096) <= step &&
		t[2].at != 4096,
		"spotcam: the title, skip held, tests as played through",
		t.size() == 3 ? t[2].at : -1, 4096);
	m.title = false;

	// The skip first held halfway through the last segment: the frame
	// starts between the cameras, the test goes to the last.
	m.skipFrom = 350;
	m.rule = false;
	t = spot_run(m);

	const long mid = t.size() == 3 ? t[2].at : -1;

	m.rule = true;
	t = spot_run(m);
	ok(mid > 3072 && mid < 4096 && t.size() == 3 && t[2].at == 4096,
		"spotcam: skipped late, the end test from mid segment to the last",
		mid, 4096);

	// Held and let go before the last segment: that end is played, the
	// rule does not touch it.
	m.skipFrom = 0;
	m.skipTo = 3;
	t = spot_run(m);
	ok(t.size() == 3 && labs(t[2].at - 4096) <= step && t[2].at != 4096,
		"spotcam: skip let go before the end, the end test as played",
		t.size() == 3 ? t[2].at : -1, 4096);

	// Without SNAPBACK the end runs over the spline to the chase camera:
	// its first frame tests at the last camera, the end once more. Skipped,
	// both stand at the last camera with or without the rule.
	m.flags[0] = 0;
	m.skipTo = 1 << 30;
	t = spot_run(m);

	std::vector<SpotTest> old;

	m.rule = false;
	old = spot_run(m);
	ok(t.size() == 4 && t[2].at == 4096 && t[3].at == 4096 &&
		t[3].cam == 4 && old.size() == 4 && old[3].at == t[3].at,
		"spotcam: to the chase camera, skipped, the rule changes nothing",
		t.size() == 4 ? t[3].at : -1, 4096);

	m.skipTo = 0;
	t = spot_run(m);
	ok(t.size() == 4 && t[2].at == 4096 && t[3].cam == 4 &&
		t[3].at > 4096 && t[3].at <= 9000,
		"spotcam: to the chase camera, played, the second test near it",
		t.size() == 4 ? t[3].at : -1, 9000);

	// No heavy test on the last camera: no end test, skipped or not.
	m.flags = { SPOT_SNAPBACK, 0, SPOT_TESTTRIGGER, SPOT_TESTTRIGGER, 0 };
	m.skipTo = 1 << 30;
	m.rule = true;
	t = spot_run(m);
	ok(t.size() == 2 && t[0].at == 2048 && t[1].at == 3072,
		"spotcam: no test on the last camera, none at the end",
		(long)t.size(), 2);

	// The rule alone: only a skipped, non-title SNAPBACK end moves.
	ok(SpotEndTestCamera(SPOT_SNAPBACK, 1, 0, 9) == 9 &&
		SpotEndTestCamera(SPOT_SNAPBACK, 0, 0, 9) == -1 &&
		SpotEndTestCamera(SPOT_SNAPBACK, 1, 1, 9) == -1 &&
		SpotEndTestCamera(0, 1, 0, 9) == -1,
		"spotcam: the end test camera by skip, title and SNAPBACK", 0, 0);

	// The census rule on flag pairs of the classic levels: RICH2 sequence 3
	// (the lift) and RICH3 sequence 6 (the helicopter) at risk; RICH2
	// sequence 0 (no SNAPBACK), RICH3 sequence 7 (TRACK) and RICH2 sequence
	// 10 (no test at the end) not.
	ok(SpotSeqEndsShort(0x1641, 0x4800) && SpotSeqEndsShort(0x4641, 0x4000) &&
		!SpotSeqEndsShort(0x4100, 0x4100) &&
		!SpotSeqEndsShort(0x0249, 0x4000) && !SpotSeqEndsShort(0x0651, 0),
		"spotcam: the census rule on the levels' own flags", 0, 0);
}

/* The water's life (hd_life_rules.h): a body of squares, w by d, from the
 * world's origin, every square `depth` deep and under a ceiling. */
static HDLifeBody life_body(int w, int d, int depth)
{
	HDLifeBody b;

	b.index = 0;
	b.surfaceY = 1000;
	b.flowX = b.flowZ = b.speed = 0.0f;
	b.river = b.channel = 0;
	b.rise = 0.0f;
	b.upMin = b.upMax = 0.0f;

	for (int x = 0; x < w; x++)
		for (int z = 0; z < d; z++)
		{
			HDLifeSquare s = { x * 1024, z * 1024, depth, 1 };

			b.squares.push_back(s);
		}

	return b;
}

static void life_drift()
{
	HDLifeBody b = life_body(3, 3, 2048);
	float x = 1536.0f, z = 1536.0f;

	ok(HDLifeStep(b, &x, &z, 100.0f, 50.0f, kHDLifeEdge, 100.0f) ==
		HD_LIFE_MOVED &&
		x == 1636.0f && z == 1586.0f,
		"life drift: a step in open water is taken whole", (long)x, 1636);

	b.flowX = 1.0f;
	b.speed = 400.0f;

	float vx, vz;

	HDLifeVelocity(b, 7, 2.5f, &vx, &vz);
	ok(fabsf(vx - 400.0f) <= kHDLifeWander + 0.01f &&
		fabsf(vz) <= kHDLifeWander + 0.01f,
		"life drift: the velocity is the current plus at most the wander",
		(long)vx, 400);

	float a1, b1, a2, b2;

	HDLifeWanderAt(7, 9.25f, &a1, &b1);
	HDLifeWanderAt(7, 9.25f, &a2, &b2);
	ok(a1 == a2 && b1 == b2 && fabsf(a1 * a1 + b1 * b1 - 1.0f) < 1.0e-3f,
		"life drift: the wander is a unit way that depends on the float and "
		"the clock alone", (long)(a1 * 1000.0f), (long)(a2 * 1000.0f));

	ok(fabsf(HDLifeTurnToward(0.0f, 1.0f, 0.0f, kHDLifeTurn, 0.1f) -
		kHDLifeTurn * 0.1f) < 1.0e-5f,
		"life drift: the yaw turns toward the way at the turn rate", 0, 0);
	ok(HDLifeCount(1) == 1 && HDLifeCount(3) == 1 && HDLifeCount(4) == 2 &&
		HDLifeCount(44) == kHDLifeBodyMax,
		"life drift: one float for three squares, twelve at most",
		HDLifeCount(44), kHDLifeBodyMax);
}

/* The spring's current (HDLifeSpringPush): straight away from the spring,
 * kHDLifeSpring at it and falling off linearly to nothing at
 * kHDLifeSpringReach, nothing inside kHDLifeSpringCore and nothing in a
 * body without a spring; a float left to the current alone drifts out
 * along its line from the spring, ever slower: from 112 units out it is
 * 1536 - 1424 e^(-20 / 38.4), about 690, after 20 seconds. */
static void life_spring()
{
	HDLifeBody b = life_body(3, 3, 2048);
	float vx = 0.0f, vz = 0.0f;

	HDLifeSpringPush(b, 2048.0f, 1536.0f, &vx, &vz);
	ok(vx == 0.0f && vz == 0.0f, "life spring: no spring, no current",
		(long)vx, 0);

	b.spring = 1;
	b.springX = 1536.0f;
	b.springZ = 1536.0f;

	const float half = 0.5f * kHDLifeSpringReach;

	vx = vz = 0.0f;
	HDLifeSpringPush(b, 1536.0f + half, 1536.0f, &vx, &vz);
	ok(fabsf(vx - 0.5f * kHDLifeSpring) < 1.0e-3f && vz == 0.0f,
		"life spring: half the speed half the reach out, away from it",
		(long)(vx * 100.0f), (long)(50.0f * kHDLifeSpring));

	vx = vz = 0.0f;
	HDLifeSpringPush(b, 1536.0f - 300.0f, 1536.0f - 400.0f, &vx, &vz);
	const float want = kHDLifeSpring * (1.0f - 500.0f / kHDLifeSpringReach);
	ok(fabsf(vx + 0.6f * want) < 1.0e-3f && fabsf(vz + 0.8f * want) < 1.0e-3f,
		"life spring: the way is the line from the spring, any side",
		(long)(vz * 100.0f), (long)(-80.0f * want));

	vx = vz = 1.0f;
	HDLifeSpringPush(b, 1536.0f + kHDLifeSpringReach, 1536.0f, &vx, &vz);
	HDLifeSpringPush(b, 1536.0f + 0.5f * kHDLifeSpringCore, 1536.0f, &vx,
		&vz);
	ok(vx == 1.0f && vz == 1.0f, "life spring: nothing at the reach and "
		"nothing in the core; added to the velocity", (long)vx, 1);

	// Thirty steps a second for 20 s, the current alone.
	float x = 1536.0f + 100.0f, z = 1536.0f + 50.0f;

	for (int s = 0; s < 600; s++)
	{
		vx = vz = 0.0f;
		HDLifeSpringPush(b, x, z, &vx, &vz);
		HDLifeStep(b, &x, &z, vx / 30.0f, vz / 30.0f, kHDLifeEdge, 100.0f);
	}

	const float d = sqrtf((x - 1536.0f) * (x - 1536.0f) +
		(z - 1536.0f) * (z - 1536.0f));

	ok(fabsf(d - 690.0f) < 15.0f &&
		fabsf((z - 1536.0f) * 2.0f - (x - 1536.0f)) < 1.0f,
		"life spring: a float drifts out along its line, about 690 units "
		"in 20 s", (long)d, 690);
}

static void life_bank()
{
	HDLifeBody b = life_body(3, 1, 2048);
	float x = 3072.0f - kHDLifeEdge - 10.0f, z = 512.0f;

	ok(HDLifeStep(b, &x, &z, 50.0f, 0.0f, kHDLifeEdge, 100.0f) ==
		HD_LIFE_BLOCKED &&
		x == 3072.0f - kHDLifeEdge - 10.0f,
		"life bank: a step nearer than the edge to a dry square is refused",
		(long)x, 3072 - (long)kHDLifeEdge - 10);

	x = 3072.0f - kHDLifeEdge - 10.0f;
	z = 512.0f;
	ok(HDLifeStep(b, &x, &z, 50.0f, 30.0f, kHDLifeEdge, 100.0f) ==
		HD_LIFE_SLID &&
		z == 542.0f,
		"life bank: pressed against a bank a float slides along it",
		(long)z, 542);
	ok(!HDLifeOpen(b, 512.0f, 1024.0f - kHDLifeEdge + 1.0f, kHDLifeEdge,
		100.0f) && HDLifeOpen(b, 1024.0f, 512.0f, kHDLifeEdge, 100.0f),
		"life bank: open water ends an edge's width before a dry square and "
		"runs on over a wet one", 0, 0);

	const float bmin[3] = { -163.0f, -302.0f, -163.0f };
	const float bmax[3] = { 163.0f, 56.0f, 163.0f };

	ok(fabsf(HDLifeReach(1, bmin, bmax) - 242.1f) < 0.1f,
		"life bank: a barrel on its side keeps half its diagonal, 242, "
		"from a bank", (long)HDLifeReach(1, bmin, bmax), 242);

	// A corner of one square, the current pressing into it.
	HDLifePool p = {};
	HDLifeEntry e = {};

	p.bodies.push_back(life_body(1, 1, 2048));
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	e.kind = HD_LIFE_BOTTLE;
	e.x = 1024.0f - kHDLifeEdge - 1.0f;
	e.z = 1024.0f - kHDLifeEdge - 1.0f;
	e.index = 3;
	p.bodies[0].flowX = 0.7071f;
	p.bodies[0].flowZ = 0.7071f;
	p.bodies[0].speed = 400.0f;
	p.entries.push_back(e);
	HDLifeFloatTick(p, 0, p.entries[0], 1.0f, 0.1f);
	ok(p.entries[0].banked == 1 && p.entries[0].retry >= 1.0f +
		kHDLifeRetryMin && p.respawned == 0,
		"life bank: a channel's float the bank holds stays and tries again "
		"later", p.entries[0].banked, 1);
}

static void life_spawn()
{
	HDLifeBody b = life_body(5, 1, 2048);

	b.river = 1;
	b.flowX = 1.0f;
	b.speed = 400.0f;
	HDLifeRiverEnds(b);
	ok(b.upMin == 512.0f && b.upMax == 4608.0f,
		"life spawn: a river's ends are its squares' middles along the flow",
		(long)b.upMax, 4608);

	const int sq = HDLifeUpstreamSquare(b, 5, 2, 100.0f);

	ok(sq >= 0 && b.squares[(size_t)sq].x == 0,
		"life spawn: a river float comes back on the upstream row",
		sq >= 0 ? b.squares[(size_t)sq].x : -1, 0);
	ok(HDLifeAtSink(b, 5120.0f + kHDLifeSlotReach, 512.0f) &&
		!HDLifeAtSink(b, 5120.0f + kHDLifeSlotReach - 1.0f, 512.0f),
		"life spawn: the outlet's slot point, past the last row's edge, is "
		"where a river float ends", 0, 0);
	ok(b.inX == -kHDLifeSlotReach && b.inZ == 512.0f && b.outX == 5120.0f +
		kHDLifeSlotReach && b.outZ == 512.0f,
		"life spawn: the slot points lie past the end rows' edges along the "
		"flow", (long)b.outX, (long)(5120.0f + kHDLifeSlotReach));

	const HDLifeLevel level = { "TEST", 8, 0, 1, { 1, 0, 0, 0 }, 0, 0 };

	ok(HDLifeTakes(level, kHDLifeRiverMin, 1, 0) &&
		!HDLifeTakes(level, kHDLifeRiverMin - 1, 1, 0) &&
		!HDLifeTakes(level, 7, 0, 0) && HDLifeTakes(level, 8, 0, 0),
		"life spawn: a river of three squares takes floats, a pond from the "
		"level's least", 0, 0);

	HDLifePool p = {};
	HDLifeEntry e = {};

	p.bodies.push_back(b);
	p.has[HD_LIFE_BOTTLE] = 1;
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	e.kind = HD_LIFE_BOTTLE;
	e.index = 4;
	e.x = 5120.0f + kHDLifeSlotReach - 10.0f;
	e.z = 512.0f;
	p.entries.push_back(e);
	HDLifeFloatTick(p, &level, p.entries[0], 1.0f, 0.1f);
	ok(p.respawned == 1 && p.entries[0].gen == 1 &&
		p.entries[0].x == -kHDLifeSlotReach,
		"life spawn: a float at the outlet's slot comes back at the inlet's",
		(long)p.entries[0].x, (long)-kHDLifeSlotReach);
}

/* The river's ends (HDLifeDiveShare, HDLifeOnRun, HDLifeComeBack): a river
 * of ten squares along x, its floats stepped for a minute at 30 steps a
 * second. A float is wholly under the surface (dive share 1) wherever it
 * lies past the end rows' edges by the slot's reach, it comes back only
 * from the outlet's slot point and only to the inlet's, and on its way it
 * moves no more than the current takes it in a step. */
static void life_ends()
{
	HDLifeBody b = life_body(10, 1, 2048);

	b.river = 1;
	b.flowX = 1.0f;
	b.speed = 1063.0f;	// ANDY3's sink river
	HDLifeRiverEnds(b);
	ok(HDLifeDiveShare(b, b.inX, 512.0f) == 1.0f &&
		HDLifeDiveShare(b, b.outX, 512.0f) == 1.0f &&
		HDLifeDiveShare(b, 5120.0f, 512.0f) == 0.0f &&
		HDLifeDiveShare(b, 1024.0f, 512.0f) == 0.0f &&
		HDLifeDiveShare(b, 9216.0f, 512.0f) == 0.0f,
		"life ends: under at the slot points, afloat from the first square's "
		"end to the last one's start", 0, 0);
	ok(HDLifeDiveShare(b, 0.0f, 512.0f) > 0.5f &&
		HDLifeDiveShare(b, 10240.0f, 512.0f) > 0.5f,
		"life ends: at the end rows' edges a float is more than half way "
		"down", (long)(100.0f * HDLifeDiveShare(b, 0.0f, 512.0f)), 50);
	ok(fabsf(HDLifeDiveDepth(326.0f, 0.45f) - (0.55f * 326.0f +
		kHDLifeSlotTop)) < 0.01f && HDLifeDiveLightShare(1.0f) ==
		kHDLifeDiveLight && HDLifeDiveLightShare(0.0f) == 1.0f,
		"life ends: the dive takes the top under the surface and the light "
		"down to its share", 0, 0);

	const HDLifeLevel level = { "TEST", 8, 0, 1, { 1, 0, 0, 0 }, 0, 0 };
	HDLifePool p = {};

	p.bodies.push_back(b);
	p.has[HD_LIFE_BOTTLE] = 1;
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	p.reach[HD_LIFE_BOTTLE] = 100.0f;

	for (int i = 0; i < 4; i++)
	{
		HDLifeEntry e = {};

		e.kind = HD_LIFE_BOTTLE;
		e.index = i;
		e.x = 1500.0f + 2000.0f * i;
		e.z = 512.0f;
		HDLifeDress(p, e);
		p.entries.push_back(e);
	}

	int backs = 0, badBack = 0, jumps = 0, openPast = 0;

	for (int s = 1; s <= 1800; s++)
		for (size_t i = 0; i < p.entries.size(); i++)
		{
			HDLifeEntry& e = p.entries[i];
			const float x0 = e.x, z0 = e.z;
			const int gen = e.gen;

			HDLifeFloatTick(p, &level, e, s / 30.0f, 1.0f / 30.0f);

			if (e.gen != gen)
			{
				backs++;
				// The step before: one step short of the slot point.
				badBack += HDLifeDiveShare(b, x0, z0) < 0.99f ||
					HDLifeDiveShare(b, e.x, e.z) < 1.0f ||
					fabsf(e.x - b.inX) > 0.5f;
				continue;
			}

			const float step = sqrtf((e.x - x0) * (e.x - x0) + (e.z - z0) *
				(e.z - z0));

			jumps += step > (b.speed + kHDLifeWander) / 30.0f + 0.01f;

			const float a = HDLifeAlong(b, e.x, e.z);

			openPast += (a < -kHDLifeSlotReach || a > 10240.0f +
				kHDLifeSlotReach) && HDLifeDiveShare(b, e.x, e.z) < 1.0f;
		}

	ok(backs > 0 && badBack == 0 && jumps == 0 && openPast == 0,
		"life ends: a minute of a river - floats come back only from slot to "
		"slot, never jump, never afloat past an edge", backs * 1000 +
		badBack * 100 + jumps * 10 + openPast, backs * 1000);
}

/* The drips (HDLifeDropTicks, HDLifeTick): a drop falls as UpdateDrips
 * steps a drip, about as the project's g has it; its ring is due when it
 * arrives, three quarters of a wading Lara's or more; the body's line
 * names the drips' points. */
static void life_drops()
{
	const int ticks = HDLifeDropTicks(3104, 4864);
	// 1,760 units under g = 4,300 units/s^2: sqrt(2 x 1760 / 4300) s.
	const float want = sqrtf(2.0f * 1760.0f / 4300.0f) * 30.0f;

	ok(fabsf((float)ticks - want) <= 1.5f && HDLifeDropTicks(0, 0) == 0,
		"life drops: a drop falls as the project's g has it",
		ticks, (long)(want + 0.5f));

	HDLifePool p = {};
	HDLifeDrip d = {};

	p.bodies.push_back(life_body(2, 2, 2048));
	p.ringAt.assign(1, -1.0e6f);
	d.body = 0;
	d.x = 500.0f;
	d.z = 600.0f;
	d.period = 5.0f;
	d.phase = 1.0f;
	d.last = -1.0f;
	d.ceilY = -1000;
	d.ceilRoom = 3;
	d.fall = 27;
	p.drips.push_back(d);

	std::vector<HDLifeRing> rings;
	std::vector<HDLifeDrop> drops;
	float rang = -1.0f;
	// The pool holds the drop while it falls, and no longer.
	int heldEarly = 0, heldFalling = 0, heldAfter = 0;

	for (int k = 1; k <= 90; k++)
	{
		const size_t had = rings.size();
		const float t = k / 30.0f;

		HDLifeTick(p, 0, t, 1, 500.0f, 600.0f, &rings, 0, 0, &drops);

		if (rings.size() > had && rang < 0.0f)
			rang = t;

		const int held = (int)p.falling.size();

		if (t < 0.99f)
			heldEarly += held;
		else if (t > 1.01f && t < 1.0f + 26.5f / 30.0f)
			heldFalling += held == 1 && p.falling[0].at > 0.99f &&
				p.falling[0].at < 1.01f ? 0 : 1;
		else if (t > 1.0f + 27.5f / 30.0f)
			heldAfter += held;
	}

	ok(heldEarly == 0 && heldFalling == 0 && heldAfter == 0,
		"life drops: the pool holds a drop from its ceiling to the water, "
		"none before, none after", heldEarly * 100 + heldFalling * 10 +
		heldAfter, 0);

	/* Its fall at a frame between two ticks: the steps of HDLifeDropTicks
	 * at whole ticks, straight between them, and the water reached at the
	 * tick HDLifeDropTicks counts. */
	int bad = 0;

	for (int k = 0; k < 40; k++)
	{
		const float a = HDLifeDropFall((float)k, 0);
		const float m = HDLifeDropFall((float)k + 0.5f, 0);
		const float n = HDLifeDropFall((float)k + 1.0f, 0);

		bad += !(a <= m && m <= n && fabsf(m - 0.5f * (a + n)) < 0.01f);
	}

	ok(bad == 0 && HDLifeDropFall(27.0f, 0) >= 1760.0f &&
		HDLifeDropFall(26.0f, 0) < 1760.0f && HDLifeDropTicks(3104, 4864) ==
		27,
		"life drops: the drawn fall follows the ticks, between them too",
		(long)HDLifeDropFall(27.0f, 0), 1760);

	ok(drops.size() == 1 && drops[0].room == 3 && drops[0].y == -1000 +
		kHDLifeDropUnder && drops[0].ticks == 27 && fabsf(rang - (1.0f +
		27.0f / 30.0f)) < 0.05f,
		"life drops: the drop leaves the ceiling at its time, its ring is due "
		"on arrival", (long)(rang * 100.0f), 190);
	ok(rings.size() == 1 && rings[0].size >= kHDLifeDripSize &&
		rings[0].size <= kHDLifeDripSize + 15 && kHDLifeDripSize * 4 >= 112 * 3,
		"life drops: a drip's ring is three quarters of a wading Lara's or "
		"more", rings.empty() ? 0 : rings[0].size, kHDLifeDripSize);

	char tail[256];

	HDLifeBodyTail(p, 0, tail, sizeof(tail));
	ok(strstr(tail, "in=none out=none drips_at=500,600") != 0,
		"life drops: the body's line names its drips' points", 0, 0);
}

/* The rings round a big float (HDLifeFloatRings): a barrel rings every 1
 * to 2 seconds at a size near a swimming Lara's, a bottle never. */
static void life_rings()
{
	HDLifePool p = {};
	HDLifeEntry barrel = {}, bottle = {};

	p.bodies.push_back(life_body(3, 3, 2048));
	p.height[HD_LIFE_BARREL] = 326.0f;
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	barrel.kind = HD_LIFE_BARREL;
	barrel.index = 5;
	barrel.x = barrel.z = 1500.0f;
	barrel.period = 3.0f;
	bottle = barrel;
	bottle.kind = HD_LIFE_BOTTLE;
	bottle.index = 6;

	std::vector<HDLifeRing> a, b;
	std::vector<float> at;
	int offTop = 0;

	barrel.phase = 1.0f;

	for (int k = 1; k <= 600; k++)
	{
		const size_t before = a.size();
		const float t = k / 30.0f;

		HDLifeFloatRings(p, barrel, 0, (k - 1) / 30.0f, t, &a);
		HDLifeFloatRings(p, bottle, 1, (k - 1) / 30.0f, t, &b);

		// A ring comes in the step its bob passes its top: sine -1.
		if (a.size() > before)
		{
			at.push_back(t);
			offTop += sinf(6.2831853f * t / barrel.period + barrel.phase) >
				-0.98f;
		}
	}

	int sizeBad = 0, gapBad = 0;

	for (size_t i = 0; i < a.size(); i++)
		sizeBad += a[i].size < kHDLifeFloatRing || a[i].size >
			kHDLifeFloatRing + kHDLifeFloatRingTop || a[i].size > 255 ||
			a[i].drip != -1;

	for (size_t i = 1; i < at.size(); i++)
		gapBad += fabsf(at[i] - at[i - 1] - barrel.period) > 0.05f;

	ok(a.size() >= 6 && a.size() <= 7 && sizeBad == 0 && gapBad == 0 &&
		offTop == 0 && b.empty(),
		"life rings: a barrel rings once a bob at its top at 240 to 255, a "
		"bottle never", (long)a.size() * 100 + (long)b.size(), 600);
}

/* Floats keep apart (HDLifeClear): a current that drives two floats to a
 * wall leaves them side by side, no nearer than kHDLifeApart of their
 * reaches. */
static void life_apart()
{
	HDLifePool p = {};

	p.bodies.push_back(life_body(2, 4, 2048));
	p.bodies[0].flowZ = 1.0f;
	p.bodies[0].speed = 90.0f;
	p.height[HD_LIFE_BARREL] = 326.0f;
	p.reach[HD_LIFE_BARREL] = 242.0f;

	for (int i = 0; i < 2; i++)
	{
		HDLifeEntry e = {};

		e.kind = HD_LIFE_BARREL;
		e.index = 20 + i;
		e.x = 900.0f + 200.0f * i;
		e.z = 600.0f + 900.0f * i;
		p.entries.push_back(e);
	}

	for (int k = 1; k <= 30 * 120; k++)
		for (size_t i = 0; i < p.entries.size(); i++)
			HDLifeFloatTick(p, 0, p.entries[i], k / 30.0f, 1.0f / 30.0f);

	const float dx = p.entries[0].x - p.entries[1].x;
	const float dz = p.entries[0].z - p.entries[1].z;
	const float apart = sqrtf(dx * dx + dz * dz);

	ok(apart >= kHDLifeApart * 484.0f - 0.5f && p.entries[0].z > 3000.0f &&
		p.entries[1].z > 3000.0f,
		"life apart: two barrels the current drives to the wall lie apart",
		(long)apart, (long)(kHDLifeApart * 484.0f));
}

/* The way on along a bank (HDLifeWayOn): a canal of two squares across
 * running +z, ten long, with a side arm five squares west over its rows 5
 * and 6 - ANDREA2's canal's shape. A float the current holds on the side
 * arm's bank turns east; one at the end wall and one in the open channel
 * do not; three floats on the side arm's bank reach the end row. */
static void life_wayon()
{
	HDLifeBody b = life_body(0, 0, 2048);

	for (int z = 0; z < 10; z++)
		for (int x = 0; x < 7; x++)
			if (x >= 5 || z == 5 || z == 6)
			{
				HDLifeSquare s = { x * 1024, z * 1024, 2048, 1 };

				b.squares.push_back(s);
			}

	b.flowZ = 1.0f;
	b.speed = 90.0f;

	float vx = 0.0f, vz = 90.0f;

	HDLifeWayOn(b, 2500.0f, 7168.0f - 128.0f, 128.0f, 100.0f, &vx, &vz);
	ok(vx == 90.0f && vz == 90.0f,
		"life way on: the side arm's bank turns a float toward the channel",
		(long)vx, 90);

	vx = 0.0f;
	vz = 90.0f;
	HDLifeWayOn(b, 5800.0f, 10240.0f - 128.0f, 128.0f, 100.0f, &vx, &vz);
	HDLifeWayOn(b, 5800.0f, 3072.0f - 100.0f, 128.0f, 100.0f, &vx, &vz);
	ok(vx == 0.0f && vz == 90.0f,
		"life way on: the end wall and the open channel turn nothing",
		(long)vx, 0);

	HDLifePool p = {};

	p.bodies.push_back(b);
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	p.reach[HD_LIFE_BOTTLE] = 70.0f;

	for (int i = 0; i < 3; i++)
	{
		HDLifeEntry e = {};

		e.kind = HD_LIFE_BOTTLE;
		e.index = 40 + i;
		e.x = 800.0f + 1500.0f * i;
		e.z = 6900.0f;
		p.entries.push_back(e);
	}

	int home = 0;

	for (int k = 1; k <= 30 * 180; k++)
		for (size_t i = 0; i < p.entries.size(); i++)
			HDLifeFloatTick(p, 0, p.entries[i], k / 30.0f, 1.0f / 30.0f);

	for (size_t i = 0; i < p.entries.size(); i++)
		home += p.entries[i].z >= 9216.0f;

	ok(home == 3, "life way on: three floats on the side arm's bank reach "
		"the end row in three minutes", home, 3);
}

/* A fish turns to its way (HDLifeFishTurn) and its tail's beat runs on
 * (HDLifeSchool::beat): a school scared at its middle turns no fish by
 * more in a frame than its turn and its fastest wobble allow, though the
 * ways it darts on lie farther than that from its heading. */
static void life_turn()
{
	HDLifePool p = {};
	HDLifeSchool s = {};

	p.bodies.push_back(life_body(4, 4, 2048));
	p.reach[HD_LIFE_FISH] = 89.0f;
	s.cx = 2048.0f;
	s.cz = 2048.0f;
	s.fish = 6;
	p.schools.push_back(s);

	for (int f = 0; f < 6; f++)
	{
		HDLifeEntry e = {};

		e.kind = HD_LIFE_FISH;
		e.index = f;
		e.school = 0;
		e.ring = 200.0f;
		e.angle = 6.2831853f * f / 6.0f;
		p.entries.push_back(e);
	}

	const float dt = 1.0f / 30.0f;
	const float most = kHDLifeFishTurn * dt + kHDLifeWobble * 6.2831853f *
		kHDLifeWobbleHz * kHDLifeDartBeat * dt + 1.0e-3f;
	float worst = 0.0f, wide = 0.0f, last[6];

	for (int k = 0; k <= 30 * 20; k++)
	{
		const float t = k * dt;

		if (k)
			HDLifeTick(p, 0, t, k <= 45 && k >= 30, 2048.0f, 2048.0f, 0);

		for (int f = 0; f < 6; f++)
		{
			const HDLifeEntry& e = p.entries[(size_t)f];
			float x, y, z, yaw, pitch, roll;

			HDLifePose(p, e, t, &x, &y, &z, &yaw, &pitch, &roll);

			if (k)
			{
				float d = yaw - last[f];

				while (d > 3.14159265f)
					d -= 6.2831853f;
				while (d < -3.14159265f)
					d += 6.2831853f;

				worst = fabsf(d) > worst ? fabsf(d) : worst;
			}

			last[f] = yaw;

			if (p.schools[0].darting && k == 31)
			{
				float d = atan2f(e.wayX, e.wayZ) - p.schools[0].heading;

				while (d > 3.14159265f)
					d -= 6.2831853f;
				while (d < -3.14159265f)
					d += 6.2831853f;

				wide = fabsf(d) > wide ? fabsf(d) : wide;
			}
		}
	}

	ok(p.schools[0].darts >= 1 && wide > 2.0f * most && worst <= most,
		"life turn: a dart turns its fish within a few frames, never in one",
		(long)(worst * 1000.0f), (long)(most * 1000.0f));
}

static int life_draw(void* state);

/* A rebuild of the level keeps the pool (HDLifeSameBodies): the same water
 * keeps every entry where it was, other water rolls anew. */
static void life_keep()
{
	HDLifePool p = {};
	const HDLifeLevel level = { "TEST", 8, 0, 0, { 1, 0, 0, 1 }, 0, 0 };
	unsigned state = 7;

	p.bodies.push_back(life_body(4, 4, 2048));
	p.has[HD_LIFE_BOTTLE] = p.has[HD_LIFE_BARREL] = 1;
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	p.height[HD_LIFE_BARREL] = 326.0f;
	HDLifeBuild(p, level, life_draw, &state);
	HDLifeTick(p, &level, 12.0f, 0, 0.0f, 0.0f, 0);

	const HDLifePool before = p;
	std::vector<HDLifeBody> again;

	again.push_back(life_body(4, 4, 2048));

	bool same = HDLifeSameBodies(before.bodies, again);
	int moved = 0;

	// What the engine does then (hd_life.cpp, HDLifeLevelBegin).
	HDLifePool kept = same ? before : HDLifePool();

	for (size_t i = 0; i < kept.entries.size(); i++)
		moved += kept.entries[i].x != before.entries[i].x ||
			kept.entries[i].z != before.entries[i].z;

	again[0].squares[3].depth = 512;

	ok(same && !before.entries.empty() && moved == 0 &&
		kept.entries.size() == before.entries.size() &&
		!HDLifeSameBodies(before.bodies, again),
		"life keep: a rebuild of the same water keeps every entry's place, "
		"other water does not", moved, 0);
}

/* A fish tank (HDLifeAddTank): RICH2's row gives a body of its two
 * squares and a school of 8, which stays in the tank's box, calm and
 * darting from Lara at the glass, and rests while the tank is gone. */
static void life_tank()
{
	HDLifePool p = {};
	unsigned state = 11;
	const HDLifeTank& t = kHDLifeTanks[0];

	p.has[HD_LIFE_FISH] = 1;
	p.reach[HD_LIFE_FISH] = 87.0f;
	p.last = -1.0f;
	HDLifeAddTank(p, t, life_draw, &state);

	int outside = 0, fled = 0, facing = 0, faced = 0;
	float zMin[8], zMax[8];

	for (int f = 0; f < 8; f++)
	{
		zMin[f] = 1.0e30f;
		zMax[f] = -1.0e30f;
	}

	/* A minute; Lara at the glass, within a square of every fish, from 10
	 * to 40 seconds. */
	for (int k = 1; k <= 30 * 60; k++)
	{
		const float s = k / 30.0f;

		HDLifeTick(p, 0, s, s >= 10.0f && s < 40.0f, (float)t.x1 + 200.0f,
			0.5f * (t.z0 + t.z1), 0);
		fled += p.schools[0].darting || p.schools[0].fleeUntil > s;

		for (size_t i = 0; i < p.entries.size(); i++)
		{
			const HDLifeEntry& e = p.entries[i];
			float x, y, z, yaw, pitch, roll, wx, wz;

			HDLifePose(p, e, s, &x, &y, &z, &yaw, &pitch, &roll);
			outside += x < t.x0 + kHDLifeEdge - 1.0f || x > t.x1 -
				kHDLifeEdge + 1.0f || z < t.z0 + kHDLifeTankEnd - 1.0f ||
				z > t.z1 - kHDLifeTankEnd + 1.0f || y < t.top || y > t.bottom;
			zMin[i] = z < zMin[i] ? z : zMin[i];
			zMax[i] = z > zMax[i] ? z : zMax[i];

			/* Half way along its run it faces the way it swims (the model's
			 * head along its yaw, kHDLifeKinds' forward). */
			HDLifeTankAt(p.bodies[0], e, s, &x, &z, &wx, &wz);

			if (fabsf(z - 0.5f * (t.z0 + t.z1)) < 64.0f && wz != 0.0f)
			{
				faced++;
				facing += cosf(e.yaw) * wz > 0.9f;
			}
		}
	}

	float run = 1.0e30f;

	for (int f = 0; f < 8; f++)
		run = zMax[f] - zMin[f] < run ? zMax[f] - zMin[f] : run;

	ok(p.bodies.size() == 1 && p.bodies[0].tank && p.bodies[0].index == -1 &&
		p.bodies[0].squares.size() == 2 && p.fish == t.fish &&
		p.schools[0].darts == 0 && fled == 0 && outside == 0,
		"life tank: eight fish keep inside the tank and never flee, Lara at "
		"the glass for half a minute", outside + fled, 0);

	ok(run >= (float)(t.z1 - t.z0) - 2.0f * kHDLifeTankEnd - 8.0f &&
		faced > 0 && facing == faced,
		"life tank: each fish swims the whole run to and fro in a minute and "
		"turns about at the ends", facing, faced);

	// Eight lines across the tank, none shared, the stop off the glass.
	float across[8];

	for (int f = 0; f < 8; f++)
		across[f] = p.entries[(size_t)f].ring;

	std::sort(across, across + 8);

	float gap = 1.0e30f;

	for (int f = 1; f < 8; f++)
		gap = across[f] - across[f - 1] < gap ? across[f] - across[f - 1] :
			gap;

	ok(gap >= 90.0f && across[0] >= t.x0 + kHDLifeEdge &&
		across[7] <= t.x1 - kHDLifeEdge,
		"life tank: every fish on a line of its own, 96 apart", (long)gap, 96);

	const HDLifePool before = p;

	p.bodies[0].gone = 1;
	HDLifeTick(p, 0, 70.0f, 1, (float)t.x1 + 200.0f, 0.5f * (t.z0 + t.z1),
		0);

	int turned = 0;

	for (size_t i = 0; i < p.entries.size(); i++)
		turned += p.entries[i].yaw != before.entries[i].yaw;

	ok(p.schools[0].cx == before.schools[0].cx &&
		p.schools[0].darts == before.schools[0].darts && turned == 0,
		"life tank: a shot tank's school rests", turned, 0);

	// RICH2's item 33 carries IFL_INVISIBLE (0x100).
	int active = 0, status = 0, water = 0;

	HDLifeTankStart(0x100, &active, &status, &water);
	ok(status == kHDLifeItemInvisible && !active && water == 4096 &&
		HDLifeTankWhole(1, active, status, water),
		"life tank: an invisible FISHTANK at the level's start is whole",
		status, kHDLifeItemInvisible);

	HDLifeTankStart(0, &active, &status, &water);
	ok(HDLifeTankWhole(1, active, status, water) &&
		HDLifeTankWhole(0, 1, kHDLifeItemActive, 0),
		"life tank: an inactive FISHTANK is whole, and no item at all", 0, 0);

	HDLifeTankStart(0x3E00, &active, &status, &water);
	ok(active && !HDLifeTankWhole(1, active, status, water) &&
		!HDLifeTankWhole(1, 0, kHDLifeItemActive, 800) &&
		!HDLifeTankWhole(1, 0, kHDLifeItemDeactivated, 4096) &&
		!HDLifeTankWhole(1, 0, kHDLifeItemInvisible, 4000),
		"life tank: pouring, poured, shot in a loaded game or drained is "
		"gone", 0, 0);
}

static void life_flee()
{
	HDLifeBody b = life_body(4, 4, 2048);

	ok(HDLifeFlees(1000.0f, 1000.0f, 1000.0f + kHDLifeFleeReach - 1.0f,
		1000.0f) && !HDLifeFlees(1000.0f, 1000.0f, 1000.0f +
		kHDLifeFleeReach + 1.0f, 1000.0f),
		"life flee: Lara within a square of a fish scares it", 0, 0);

	// The dart's speed curve: from its start falling off, three squares in
	// the first two seconds.
	ok(HDLifeDartSpeed(0.0f) == kHDLifeDartSpeed &&
		HDLifeDartSpeed(kHDLifeDartFall) < 0.37f * kHDLifeDartSpeed &&
		HDLifeDartDistance(2.0f) > 2.75f * 1024.0f &&
		HDLifeDartDistance(2.0f) < 3.25f * 1024.0f,
		"life flee: a dart falls off with its time and runs about three "
		"squares in two seconds", (long)HDLifeDartDistance(2.0f), 3072);

	// The scatter: every way within its angle of straight away from her.
	float worst = 0.0f;
	float spreadMin = 10.0f, spreadMax = -10.0f, unit = 0.0f;

	for (int i = 0; i < 64; i++)
	{
		float wx, wz;

		HDLifeDartWay(1000.0f, 0.0f, 0.0f, 0.0f, i, 3, 0.0f, &wx, &wz);

		const float a = atan2f(wz, wx);

		worst = fabsf(a) > worst ? fabsf(a) : worst;
		spreadMin = a < spreadMin ? a : spreadMin;
		spreadMax = a > spreadMax ? a : spreadMax;
		unit = fabsf(wx * wx + wz * wz - 1.0f) > unit ?
			fabsf(wx * wx + wz * wz - 1.0f) : unit;
	}

	ok(worst <= kHDLifeDartScatter + 1.0e-4f && spreadMax - spreadMin >
		kHDLifeDartScatter && unit < 1.0e-4f,
		"life flee: 64 fish dart away from her within the scatter and spread "
		"over more than half of it", (long)(worst * 1000.0f),
		(long)(kHDLifeDartScatter * 1000.0f));

	/* A school in a pond of four by four squares, Lara at its middle: every
	 * fish darts off, no fish leaves the deep water or comes nearer than
	 * the edge to a bank, the school is back together within the
	 * flight's time. */
	HDLifePool p = {};
	HDLifeSchool s = {};

	p.bodies.push_back(b);
	p.reach[HD_LIFE_FISH] = 89.0f;
	s.cx = 2048.0f;
	s.cz = 2048.0f;
	s.target = 0;
	s.fish = 6;
	p.schools.push_back(s);

	for (int f = 0; f < 6; f++)
	{
		HDLifeEntry e = {};

		e.kind = HD_LIFE_FISH;
		e.index = f;
		e.school = 0;
		e.ring = 200.0f;
		e.angle = 6.2831853f * f / 6.0f;
		p.entries.push_back(e);
	}

	float far = 0.0f;
	int outside = 0;

	for (int k = 1; k <= 30 * 20; k++)
	{
		const float t = k / 30.0f;

		HDLifeSchoolTick(p, p.schools[0], 0, t, 1.0f / 30.0f, k <= 15, 2048.0f,
			2048.0f);

		for (size_t i = 0; i < p.entries.size(); i++)
		{
			float x, y, z, yaw, pitch, roll;

			HDLifePose(p, p.entries[i], t, &x, &y, &z, &yaw, &pitch, &roll);

			if (k == 60)
				far += sqrtf((x - 2048.0f) * (x - 2048.0f) + (z - 2048.0f) *
					(z - 2048.0f)) / 6.0f;

			outside += x < kHDLifeEdge - 1.0f || z < kHDLifeEdge - 1.0f ||
				x > 4096.0f - kHDLifeEdge + 1.0f || z > 4096.0f -
				kHDLifeEdge + 1.0f;
		}
	}

	ok(far > 1000.0f && outside == 0 && p.schools[0].darting == 0 &&
		p.schools[0].darts == 1,
		"life flee: the fish dart off from her, keep off the banks and come "
		"back together", (long)far * 10 + outside, 10000);

	// A splash on the body's water scares it, one beside it does not.
	HDLifePool q = p;
	const float on[2] = { 1000.0f, 1000.0f }, off[2] = { 5000.0f, 1000.0f };

	HDLifeSchoolTick(q, q.schools[0], 0, 30.0f, 0.1f, 0, 0.0f, 0.0f, off, 1);
	ok(q.schools[0].darting == 0, "life flee: a splash off the body's water "
		"scares nothing", q.schools[0].darting, 0);
	HDLifeSchoolTick(q, q.schools[0], 0, 30.1f, 0.1f, 0, 0.0f, 0.0f, on, 1);
	ok(q.schools[0].darting == 1 && q.schools[0].fleeUntil > 30.1f,
		"life flee: a splash on the body's water scares the school",
		q.schools[0].darting, 1);

	ok(!HDLifeDeepAt(life_body(1, 6, 2048), 200.0f, 3000.0f) &&
		HDLifeDeepAt(life_body(1, 6, 2048), 512.0f, 3000.0f),
		"life flee: a school's middle needs deep water its ring's reach "
		"round it", 0, 0);

	b = life_body(1, 1, kHDLifeDeep - 1);
	ok(HDLifeDeepCount(b) == 0,
		"life flee: water shallower than the fish's depth holds no school",
		HDLifeDeepCount(b), 0);
}

/* A school of `n` fish on a ring of 200 round cx, cz in body `b`. */
static HDLifePool life_school(const HDLifeBody& b, float cx, float cz, int n)
{
	HDLifePool p = {};
	HDLifeSchool s = {};

	p.bodies.push_back(b);
	p.reach[HD_LIFE_FISH] = 87.0f;
	p.last = -1.0f;
	s.cx = cx;
	s.cz = cz;
	s.target = HDLifeSquareAt(b, cx, cz);
	s.retarget = kHDLifeRetarget;
	s.fish = n;
	p.schools.push_back(s);

	for (int f = 0; f < n; f++)
	{
		HDLifeEntry e = {};

		e.kind = HD_LIFE_FISH;
		e.index = f;
		e.school = 0;
		e.ring = 200.0f;
		e.angle = 6.2831853f * f / (float)n;
		e.x = cx;
		e.z = cz;
		p.entries.push_back(e);
	}

	return p;
}

/* The edge in a flight (HDLifeEdgeDistance, HDLifeEdgeWay, kHDLifeEdgeRun):
 * a fish turns off the edge before it meets it and out of it when it is
 * there, a school regroups where its ring keeps off the edge within ten
 * seconds and without a jump, and a Lara who stands still lets it settle. */
static void life_edge()
{
	const HDLifeBody b3 = life_body(3, 3, 2048);
	const float need = (float)kHDLifeDeep;
	float nx = 0.0f, nz = 0.0f;
	const float mid = HDLifeEdgeDistance(b3, 1536.0f, 1536.0f, need, 1);
	const float west = HDLifeEdgeDistance(b3, 100.0f, 1536.0f, need, 1, &nx,
		&nz);
	const float corner = HDLifeEdgeDistance(b3, 3000.0f, 3000.0f, need, 2);

	ok(mid == 1024.0f && west == 100.0f && nx == 1.0f && nz == 0.0f &&
		fabsf(corner - 72.0f) < 0.01f,
		"life edge: the distance to the nearest square that does not hold "
		"the fish, and the way off it", (long)west, 100);

	// Square on to the west bank 300 off: turned to the least share off it.
	int sides[2] = { 0, 0 };
	float worstOff = 1.0f, worstUnit = 0.0f;

	for (int i = 0; i < 16; i++)
	{
		float wx = -1.0f, wz = 0.0f;
		const bool turned = HDLifeEdgeWay(b3, 300.0f, 1536.0f, need, i, 1201u,
			&wx, &wz);
		const float least = (kHDLifeEdgeRun - 300.0f) / kHDLifeEdgeSpan;

		worstOff = turned && wx - least < worstOff ? wx - least : worstOff;
		worstUnit = fabsf(wx * wx + wz * wz - 1.0f) > worstUnit ?
			fabsf(wx * wx + wz * wz - 1.0f) : worstUnit;
		sides[wz > 0.0f ? 1 : 0]++;

		// The same fish and dart turn the same way every time.
		float ax = -1.0f, az = 0.0f;

		HDLifeEdgeWay(b3, 300.0f, 1536.0f, need, i, 1201u, &ax, &az);
		worstUnit += ax != wx || az != wz ? 1.0f : 0.0f;
	}

	float lx = -0.8f, lz = 0.6f;

	HDLifeEdgeWay(b3, 300.0f, 1536.0f, need, 0, 1201u, &lx, &lz);
	ok(fabsf(worstOff) < 1.0e-4f && worstUnit < 1.0e-4f && sides[0] &&
		sides[1] && lz > 0.0f,
		"life edge: square on to a bank a way turns to its least share off "
		"it, to the side of its hash; a leaning way keeps its side",
		sides[1], 8);

	/* A dart square on to the east bank of a pool 4 by 3 squares: Lara on
	 * its west bank. No fish comes within the stop, none stays within
	 * kHDLifeEdgeKeep for two seconds, the way turns at most what the
	 * model turns in a tick, and the school is back within ten seconds. */
	HDLifePool p = life_school(life_body(4, 3, 2048), 1800.0f, 1536.0f, 6);
	const float dt = 1.0f / 30.0f;
	float leastEdge = 1.0e9f, turnMost = 0.0f, moveMost = 0.0f;
	float under[6] = { 0 }, underMost = 0.0f, last[12] = { 0 };
	float ended = -1.0f;

	for (int k = 1; k <= 30 * 20; k++)
	{
		const float t = k * dt;
		float wayX[6], wayZ[6];

		for (int f = 0; f < 6; f++)
		{
			wayX[f] = p.entries[(size_t)f].wayX;
			wayZ[f] = p.entries[(size_t)f].wayZ;
		}

		HDLifeTick(p, 0, t, 1, 700.0f, 1536.0f, 0);

		for (int f = 0; f < 6; f++)
		{
			const HDLifeEntry& e = p.entries[(size_t)f];
			float x, y, z, yaw, pitch, roll;

			HDLifePose(p, e, t, &x, &y, &z, &yaw, &pitch, &roll);

			const float d = HDLifeEdgeDistance(p.bodies[0], x, z, need, 1);

			leastEdge = d < leastEdge ? d : leastEdge;
			under[f] = d < kHDLifeEdgeKeep ? under[f] + dt : 0.0f;
			underMost = under[f] > underMost ? under[f] : underMost;

			// No jump in the flight and over its end, a second after.
			if (k > 1 && (ended < 0.0f || t < ended + 1.0f))
			{
				const float m = sqrtf((x - last[f * 2]) * (x - last[f * 2]) +
					(z - last[f * 2 + 1]) * (z - last[f * 2 + 1]));

				moveMost = m > moveMost ? m : moveMost;
			}

			last[f * 2] = x;
			last[f * 2 + 1] = z;

			// The edge's turn: in a dart, after the tick it started in.
			if (p.schools[0].darting && t - p.schools[0].dartAt > dt &&
				t - p.schools[0].dartAt < kHDLifeDartFor)
			{
				const float c = wayX[f] * e.wayX + wayZ[f] * e.wayZ;
				const float a = acosf(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c);

				turnMost = a > turnMost ? a : turnMost;
			}
		}

		if (ended < 0.0f && p.schools[0].darts && !p.schools[0].darting)
			ended = t;
	}

	ok(leastEdge >= kHDLifeEdge - 1.0f && underMost <= 2.0f &&
		turnMost <= kHDLifeFishTurn * dt + 1.0e-4f &&
		p.schools[0].darts == 1 &&
		ended > 0.0f && ended <= p.schools[0].dartAt + 10.0f &&
		moveMost <= (kHDLifeDartSpeed + kHDLifeSwim + kHDLifeCircle *
		(kHDLifeRingMin + kHDLifeRingSpan)) * dt,
		"life edge: a dart turns along the far bank, no fish stays at it, the "
		"school is back within ten seconds and no fish jumps",
		(long)(underMost * 1000.0f), 2000);

	ok(HDLifeEdgeDistance(p.bodies[0], p.schools[0].cx, p.schools[0].cz,
		need, 1) >= kHDLifeRingClear,
		"life edge: the school regroups where its whole ring keeps off the "
		"edge", (long)HDLifeEdgeDistance(p.bodies[0], p.schools[0].cx,
		p.schools[0].cz, need, 1), (long)kHDLifeRingClear);

	/* A fish the edge holds, a second into its dart, square on to the west
	 * bank at the stop: past kHDLifeEdgeKeep within half a second. */
	HDLifePool q = life_school(life_body(4, 3, 2048), 2048.0f, 1536.0f, 1);
	HDLifeSchool& s = q.schools[0];
	HDLifeEntry& e = q.entries[0];

	s.darting = 1;
	s.darts = 1;
	s.dartAt = 9.0f;
	e.x = kHDLifeEdge;
	e.z = 1536.0f;
	e.wayX = -1.0f;
	e.wayZ = 0.0f;

	for (int k = 1; k <= 15; k++)
		HDLifeSchoolTick(q, s, 0, 10.0f + k * dt, dt, 0, 0.0f, 0.0f);

	ok(HDLifeEdgeDistance(q.bodies[0], e.x, e.z, need, 1) >= kHDLifeEdgeKeep,
		"life edge: a fish the bank held turns out past the keep within a "
		"half a second", (long)HDLifeEdgeDistance(q.bodies[0], e.x, e.z,
		need, 1), (long)kHDLifeEdgeKeep);

	/* Lara standing on the bank: one flight; once she moves a quarter
	 * square, another. */
	HDLifePool w = life_school(life_body(2, 2, 2048), 1024.0f, 1024.0f, 4);

	for (int k = 1; k <= 30 * 30; k++)
		HDLifeTick(w, 0, k * dt, 1, -200.0f, 1024.0f, 0);

	const int still = w.schools[0].darts;
	const int calm = w.schools[0].darting;

	for (int k = 30 * 30 + 1; k <= 30 * 32; k++)
		HDLifeTick(w, 0, k * dt, 1, 1024.0f + kHDLifeStill + 8.0f, 1024.0f,
			0);

	ok(still == 1 && !calm && w.schools[0].darts == 2,
		"life edge: Lara standing still scares a school once; moving a "
		"quarter square, again", still * 10 + w.schools[0].darts, 12);

	/* She swims off out of its reach for 20 seconds and comes back to the
	 * place of that second dart: the place is forgotten, a third. */
	for (int k = 30 * 32 + 1; k <= 30 * 52; k++)
		HDLifeTick(w, 0, k * dt, 1, 9000.0f, 1024.0f, 0);

	const int away = w.schools[0].darts;

	for (int k = 30 * 52 + 1; k <= 30 * 54; k++)
		HDLifeTick(w, 0, k * dt, 1, 1024.0f + kHDLifeStill + 8.0f, 1024.0f,
			0);

	ok(away == 2 && w.schools[0].darts == 3,
		"life edge: a place she has left is forgotten - back at it, she "
		"scares the school again", away * 10 + w.schools[0].darts, 23);
}

/* A body drawn as rows of '#' (2048 deep) and '.' (none), one square a
 * character, x along a row from 0, z down the rows from 0. */
static HDLifeBody life_map(const char* const* rows, int n)
{
	HDLifeBody b = life_body(0, 0, 2048);

	for (int z = 0; z < n; z++)
		for (int x = 0; rows[z][x]; x++)
			if (rows[z][x] == '#')
			{
				HDLifeSquare s = { x * 1024, z * 1024, 2048, 0 };

				b.squares.push_back(s);
			}

	return b;
}

/* The roam of a big body's school (HDLifeRoams, HDLifeRoamPick,
 * HDLifeRoamNext, HDLifeRoamStep): ANDY3's basin's shape - a block of 8 by
 * 3 squares and two arms a square wide - roams, a pond does not; the
 * waypoints and every step keep 512 off the edge; the next way runs over a
 * square never come to; the pace holds; from any square the school sees a
 * third of the basin in a minute and three quarters in five - the least
 * over the 42 starts is 14 and 32, both on the bound; a pond's school
 * cruises as it did. */
static void life_roam()
{
	static const char* const basin[] = {
		".########",
		"#########",
		".########",
		".###....#",
		".#......#",
		".#......#",
		".#......#",
		".#......#",
		".#......#",
		".#......#",
		"........#",
	};
	const HDLifeBody lake = life_map(basin, 11);

	ok(lake.squares.size() == 42 && HDLifeRoams(lake) &&
		HDLifeRoams(life_body(9, 1, 2048)) &&
		!HDLifeRoams(life_body(4, 4, 2048)) &&
		!HDLifeRoams(life_body(3, 7, 2048)) &&
		!HDLifeRoams(life_body(1, 2, 2048)) &&
		!HDLifeRoams(life_body(9, 1, kHDLifeDeep - 1)),
		"life roam: a body whose deep squares span more than 8 squares roams",
		(long)lake.squares.size(), 42);

	// Waypoints from every square, and the way there step by step.
	int picks = 0, bad = 0, near = 0;
	float clear = 1.0e9f;
	const std::vector<int> none;

	for (int from = 0; from < (int)lake.squares.size(); from++)
		for (int leg = 0; leg < 4; leg++)
		{
			const int to = HDLifeRoamPick(lake, 0, leg, from, none, 1);
			std::vector<int> steps;

			HDLifeRoamSteps(lake, from, steps);
			picks++;

			if (to < 0 || to == from || steps[(size_t)to] < 0)
			{
				bad++;
				continue;
			}

			near += steps[(size_t)to] < kHDLifeRoamFar;

			for (int at = from, guard = 0; at != to && guard < 64; guard++)
			{
				const int next = HDLifeRoamNext(lake, at, to);

				if (next < 0 || abs(lake.squares[(size_t)next].x -
					lake.squares[(size_t)at].x) > 1024 ||
					abs(lake.squares[(size_t)next].z -
					lake.squares[(size_t)at].z) > 1024)
				{
					bad++;
					break;
				}

				for (int k = 0; k <= 8; k++)
				{
					const float u = k / 8.0f;
					const HDLifeSquare& a = lake.squares[(size_t)at];
					const HDLifeSquare& c = lake.squares[(size_t)next];
					const float x = 512.0f + a.x * (1.0f - u) + c.x * u;
					const float z = 512.0f + a.z * (1.0f - u) + c.z * u;
					const float d = HDLifeEdgeDistance(lake, x, z,
						(float)kHDLifeDeep, 1);

					clear = d < clear ? d : clear;
				}

				at = next;
			}
		}

	ok(bad == 0 && near == 0 && clear >= 511.0f,
		"life roam: every waypoint is a deep square three steps off or more, "
		"every step of the way there keeps 512 off the edge", bad * 1000 +
		near, 0);

	/* A square the centre never came to, all others come to: from every
	 * square three steps or more off it, the next way runs over it. */
	const int n = (int)lake.squares.size();
	int missed = 0;

	for (int from = 0; from < n; from++)
	{
		std::vector<int> off;

		HDLifeRoamSteps(lake, from, off);

		for (int fresh = 0; fresh < n; fresh++)
		{
			if (off[(size_t)fresh] < kHDLifeRoamFar)
				continue;

			std::vector<int> been((size_t)n, 1);

			been[(size_t)fresh] = 0;

			const int to = HDLifeRoamPick(lake, 0, fresh, from, been, 2);
			int over = 0;

			for (int at = from, guard = 0; to >= 0 && at != to && guard < 64;
				guard++)
			{
				at = HDLifeRoamNext(lake, at, to);
				over |= at == fresh ? 1 : 0;

				if (at < 0)
					break;
			}

			missed += over ? 0 : 1;
		}
	}

	ok(missed == 0, "life roam: the next way runs over a square the school "
		"never came to", missed, 0);

	/* Five minutes of a school of six from every square of the basin: the
	 * pace, the edge, the squares its fish see by one minute and by five. */
	const float dt = 1.0f / 30.0f;
	float fast = 0.0f, least = 1.0e9f;
	int minute = n, five = n, legs = 1000;

	for (int start = 0; start < n; start++)
	{
		HDLifePool p = life_school(lake, lake.squares[(size_t)start].x + 512.0f,
			lake.squares[(size_t)start].z + 512.0f, 6);
		p.schools[0].roam = HDLifeRoams(lake) ? 1 : 0;

		const float pace = kHDLifeRoamLengths * 2.0f * p.reach[HD_LIFE_FISH];
		std::vector<int> seen((size_t)n, 0);
		int saw = 0;

		for (int k = 1; k <= 30 * 300; k++)
		{
			const float t = k * dt;
			const float cx = p.schools[0].cx, cz = p.schools[0].cz;

			HDLifeTick(p, 0, t, 0, 0.0f, 0.0f, 0);

			const float m = sqrtf((p.schools[0].cx - cx) * (p.schools[0].cx -
				cx) + (p.schools[0].cz - cz) * (p.schools[0].cz - cz));
			const float d = HDLifeEdgeDistance(lake, p.schools[0].cx,
				p.schools[0].cz, (float)kHDLifeDeep, 1);

			fast = m - pace * dt > fast ? m - pace * dt : fast;
			least = d < least ? d : least;

			for (size_t i = 0; i < p.entries.size(); i++)
			{
				float x, y, z, yaw, pitch, roll;

				HDLifePose(p, p.entries[i], t, &x, &y, &z, &yaw, &pitch,
					&roll);

				const int at = HDLifeSquareAt(lake, x, z);

				if (at >= 0 && !seen[(size_t)at])
				{
					seen[(size_t)at] = 1;
					saw++;
				}
			}

			if (k == 30 * 60)
				minute = saw < minute ? saw : minute;
		}

		five = saw < five ? saw : five;
		legs = p.schools[0].legs < legs ? p.schools[0].legs : legs;
	}

	ok(fast <= 0.01f && least >= kHDLifeRingClear && minute * 3 >= n &&
		five * 4 >= n * 3 && legs >= 2,
		"life roam: at a length a second, its ring off the edge, from any "
		"square the school sees a third of the basin in a minute and three "
		"quarters in five", minute * 100 + five, 14 * 100 + 32);

	/* A pond's school cruises as it did: its centre and heading tick by
	 * tick as HDLifeSchoolStep and the retarget alone move them. */
	HDLifePool q = life_school(life_body(4, 4, 2048), 1536.0f, 1536.0f, 4);
	HDLifeSchool r = q.schools[0];
	int differ = 0;
	float last = 0.0f;

	q.schools[0].roam = HDLifeRoams(q.bodies[0]) ? 1 : 0;
	HDLifeTick(q, 0, 0.0f, 0, 0.0f, 0.0f, 0);

	for (int k = 1; k <= 30 * 60; k++)
	{
		const float t = k * dt;

		HDLifeTick(q, 0, t, 0, 0.0f, 0.0f, 0);

		// The tick's own step and clock (HDLifeTick).
		const float step = t - last;
		const float at = last + step;
		const bool there = HDLifeSchoolStep(q.bodies[0], &r, kHDLifeSwim,
			step);

		if (there || at >= r.retarget)
		{
			r.target = HDLifeDeepSquare(q.bodies[0], (int)HDLifeHash(7u,
				(unsigned)(int)at));
			r.retarget = at + kHDLifeRetarget;
		}

		last = at;

		differ += r.cx != q.schools[0].cx || r.cz != q.schools[0].cz ||
			r.heading != q.schools[0].heading;
	}

	ok(!q.schools[0].roam && differ == 0,
		"life roam: a pond's school keeps its cruise", differ, 0);
}

static void life_bob()
{
	ok(HDLifeBobAmp(80.0f, 0.5f, 5.9f) == 5.9f &&
		HDLifeBobAmp(16.0f, 0.7f, 5.9f) == 4.0f &&
		HDLifeBobAmp(80.0f, 0.5f, 0.0f) == 0.0f,
		"life bob: a quarter of the model, never more than the sheet rises",
		(long)(HDLifeBobAmp(80.0f, 0.5f, 5.9f) * 10.0f), 59);
	ok(fabsf(HDLifeBobAmp(326.0f, 0.45f, 121.3f) - (0.45f * 326.0f -
		121.3f)) < 0.01f,
		"life bob: on a high sea the bob shrinks to what the draft leaves",
		(long)HDLifeBobAmp(326.0f, 0.45f, 121.3f), 25);
	ok(HDLifeFits(326.0f, 0.45f, 121.3f) && !HDLifeFits(16.0f, 0.7f, 121.3f) &&
		HDLifeFits(16.0f, 0.7f, 5.9f),
		"life bob: a float fits where its draft reaches under the sheet's "
		"fall", 0, 0);

	float most = 0.0f;

	for (int i = 0; i < 400; i++)
	{
		const float v = fabsf(HDLifeBob((float)i * 0.01f, 3.0f, 0.4f, 5.0f));

		most = v > most ? v : most;
	}

	ok(most <= 5.0f && most > 4.9f && HDLifeBob(0.0f, 3.0f, 0.0f, 5.0f) == 0.0f,
		"life bob: the bob is a sine of the clock within its height",
		(long)(most * 100.0f), 500);

	const float mins[3] = { -40.0f, -155.0f, -40.0f };
	const float maxs[3] = { 40.0f, 0.0f, 40.0f };
	float m[16];

	HDLifeMatrix(1, mins, maxs, 0.0f, 0.0f, 0.0f, m);

	// The box's top middle (0, -155, 0) lands half the length to the side.
	const float wx = 0.0f * m[0] + -155.0f * m[4] + 0.0f * m[8] + m[12];
	const float wy = 0.0f * m[1] + -155.0f * m[5] + 0.0f * m[9] + m[13];

	ok(HDLifeHeight(1, mins, maxs) == 80.0f && fabsf(fabsf(wx) - 77.5f) < 0.01f
		&& fabsf(wy) < 0.01f,
		"life bob: a bottle on its side floats 80 high, its middle on the "
		"point", (long)wx, 78);
}

static int life_draw(void* state)
{
	return HDLifeDraw((unsigned*)state);
}

/* A channel's conveyor (HDLifeConveyor): the main channel of ANDREA2's
 * canal from its crossing to its drain - x 55,296 to 57,344, z 43,008 to
 * 50,176, current +z at 90 - with the inlet kHDLifeInlets names on its east
 * bank. Five minutes of twelve floats: the count holds, floats come back
 * only at the inlet's slot point, off the sheet, and only under the
 * surface is a float off the sheet; one that drifts onto the inlet's
 * square without coming out of it floats on. */
static void life_conveyor()
{
	HDLifePool p = {};
	HDLifeBody b = life_body(0, 0, 2048);
	const HDLifeLevel level = { "ANDREA2", 16, 1, 0, { 1, 0, 0, 0 }, 0, 0 };

	for (int x = 55296; x < 57344; x += 1024)
		for (int z = 43008; z < 50176; z += 1024)
		{
			HDLifeSquare s = { x, z, 2048, 1 };

			b.squares.push_back(s);
		}

	b.surfaceY = 4864;
	b.channel = 1;
	b.flowZ = 1.0f;
	b.speed = 90.0f;
	p.bodies.push_back(b);
	p.has[HD_LIFE_BOTTLE] = 1;
	p.height[HD_LIFE_BOTTLE] = 80.0f;
	p.reach[HD_LIFE_BOTTLE] = 92.0f;

	unsigned state = 5;

	HDLifeBuild(p, level, life_draw, &state);

	const HDLifeBody& c = p.bodies[0];

	ok(c.ends && c.inX == 57728.0f && c.inZ == 44032.0f &&
		c.outX == 55808.0f + 512.0f && c.outZ == 50560.0f &&
		c.inWayX == -1.0f && c.inHalf == 1024.0f,
		"life conveyor: ANDREA2's canal comes in at its east bank's slot and "
		"leaves under its +z end", (long)c.inX, 57728);

	const size_t count = p.entries.size();
	int openBack = 0, overOff = 0, backs = 0;
	std::vector<int> gen(count, 0);

	for (int k = 1; k <= 300 * 10; k++)
	{
		const float t = k * 0.1f;

		HDLifeTick(p, &level, t, 0, 0.0f, 0.0f, 0);

		for (size_t i = 0; i < p.entries.size(); i++)
		{
			const HDLifeEntry& e = p.entries[i];
			float x, y, z, yaw, pitch, roll;

			HDLifePose(p, e, t, &x, &y, &z, &yaw, &pitch, &roll);

			const bool on = HDLifeSquareAt(c, x, z) >= 0;

			overOff += !on && y - 0.5f * p.height[e.kind] < (float)c.surfaceY;

			if (e.gen != gen[i])
			{
				openBack += on || x < 57344.0f;
				backs += e.gen - gen[i];
				gen[i] = e.gen;
			}
		}
	}

	ok(p.entries.size() == count && count > 0 && backs > 0 &&
		openBack == 0 && overOff == 0,
		"life conveyor: the floats go round, back only in the inlet's slot, "
		"under the surface off the sheet", openBack * 1000 + overOff, 0);

	ok(HDLifeDiveShare(c, 56900.0f, 44032.0f, 0) == 0.0f &&
		HDLifeDiveShare(c, 56900.0f, 44032.0f, 1) > 0.25f &&
		HDLifeDiveShare(c, 56900.0f, 46000.0f, 1) == 0.0f,
		"life conveyor: only a float out of the inlet rises on its square",
		0, 0);
}

/* The fish's depth (HDLifeFishBand, HDLifeFishCalm): a school in a lake of
 * five by five squares 1,024 deep under a sea's sheet (rise 121.3) for 60
 * s, Lara at its middle from 5 to 7 s and 30 to 31 s. Every fish stays in
 * its band, its pitch within the most and turned no faster than its turn
 * rate; calm, a fish rises and sinks the band's swing; a dart sends fish
 * up and down. */
static void life_fishdepth()
{
	HDLifePool p = {};
	const HDLifeLevel level = { "TEST", 1, 0, 0, { 0, 0, 0, 0 }, 1, 0 };
	unsigned state = 3;
	HDLifeBody b = life_body(5, 5, 1024);

	b.rise = 121.3f;
	p.bodies.push_back(b);
	p.has[HD_LIFE_FISH] = 1;
	p.reach[HD_LIFE_FISH] = 87.0f;
	p.height[HD_LIFE_FISH] = 70.0f;
	HDLifeBuild(p, level, life_draw, &state);

	float top, bottom;

	HDLifeFishBand(p, p.bodies[0], &top, &bottom);
	ok(fabsf(top - 249.3f) < 0.1f && fabsf(bottom - 937.0f) < 0.1f,
		"life fish depth: the band runs from 128 under the sheet's rise to "
		"the fish's reach over the floor", (long)top * 10000 + (long)bottom,
		2490937);

	int outBand = 0, overPitch = 0, overTurn = 0, ups = 0, downs = 0;
	float calmLeast = 1.0e9f, calmMost = -1.0e9f;
	std::vector<float> last(p.entries.size(), 0.0f);
	const float dt = 1.0f / 30.0f;

	for (int k = 1; k <= 60 * 30; k++)
	{
		const float t = k * dt;
		const int lara = (t >= 5.0f && t < 7.0f) || (t >= 30.0f && t < 31.0f);

		HDLifeTick(p, 0, t, lara, p.schools[0].cx, p.schools[0].cz, 0);

		for (size_t i = 0; i < p.entries.size(); i++)
		{
			const HDLifeEntry& e = p.entries[i];
			float x, y, z, yaw, pitch, roll;

			HDLifePose(p, e, t, &x, &y, &z, &yaw, &pitch, &roll);

			const float down = y - (float)p.bodies[0].surfaceY;

			outBand += down < top - 0.01f || down > bottom + 0.01f;
			overPitch += fabsf(e.pitch) > kHDLifeFishPitchMost + 1.0e-4f ||
				roll != -e.pitch;
			overTurn += fabsf(e.pitch - last[i]) > kHDLifeFishTurn * dt +
				1.0e-4f;
			last[i] = e.pitch;

			if (i == 0 && t >= 40.0f && t < 54.0f)
			{
				calmLeast = down < calmLeast ? down : calmLeast;
				calmMost = down > calmMost ? down : calmMost;
			}

			// A tenth of a second into each dart: who climbs, who dives.
			if (k == 5 * 30 + 3 || k == 30 * 30 + 3)
			{
				ups += e.climb > 0.05f && e.pitch > 0.05f;
				downs += e.climb < -0.05f && e.pitch < -0.05f;
			}
		}
	}

	ok(p.fish >= 4 && p.schools[0].darts >= 2 && outBand == 0,
		"life fish depth: calm and darting, no fish leaves its band",
		outBand, 0);
	ok(overPitch == 0 && overTurn == 0,
		"life fish depth: the model's pitch stays within 30 degrees and turns "
		"at the fish's turn rate", overPitch + overTurn, 0);
	ok(calmMost - calmLeast > 2.0f * kHDLifeFishRise - 8.0f,
		"life fish depth: a calm fish rises and sinks the swing in 14 s",
		(long)(calmMost - calmLeast), (long)(2.0f * kHDLifeFishRise));
	ok(ups >= 1 && downs >= 1,
		"life fish depth: a dart sends some fish up and some down, nose first",
		ups * 100 + downs, 101);

	// A band narrower than the swing: the swing shrinks to fit.
	p.bodies[0] = life_body(5, 5, 768);
	p.bodies[0].rise = 200.0f;
	HDLifeFishBand(p, p.bodies[0], &top, &bottom);

	float least = 1.0e9f, most = -1.0e9f;

	for (int k = 0; k < 600; k++)
	{
		const float d = HDLifeFishCalm(p, p.entries[0], k * 0.05f, 0);

		least = d < least ? d : least;
		most = d > most ? d : most;
	}

	ok(least >= top - 0.01f && most <= bottom + 0.01f && most - least >
		bottom - top - 8.0f,
		"life fish depth: in a band of 353 a fish swings over the whole of it",
		(long)(most - least), (long)(bottom - top));
}

static void life_depth()
{
	HDLifePool p = {};
	HDLifeBody b = life_body(3, 1, 256);

	b.rise = 121.3f;
	p.has[HD_LIFE_BARREL] = 1;
	p.height[HD_LIFE_BARREL] = 326.0f;
	p.reach[HD_LIFE_BARREL] = 242.1f;

	// 0.45 x 326 = 146.7, the 25.4 of bob the draft leaves over the sea's
	// 121.3, the tilt's 242.1 x 3 degrees = 12.7.
	const float need = HDLifeNeed(p, HD_LIFE_BARREL, b);

	ok(fabsf(need - 184.8f) < 0.2f,
		"life depth: a barrel on a sea reaches 185 under the surface at its "
		"lowest", (long)need, 185);

	b.squares[0].depth = 0;
	b.squares[2].depth = 128;
	ok(!HDLifeHolds(b, 0, 0, 1.0f) && !HDLifeHolds(b, 2048, 0, need) &&
		HDLifeHolds(b, 1024, 0, need) && HDLifeHoldCount(b, need) == 1 &&
		HDLifeHoldSquare(b, need, 7) == 1,
		"life depth: a floor at the surface holds nothing, shallow water no "
		"barrel", HDLifeHoldCount(b, need), 1);

	float x = 1536.0f, z = 512.0f;

	ok(HDLifeStep(b, &x, &z, 400.0f, 0.0f, kHDLifeEdge, need) ==
		HD_LIFE_BLOCKED && x == 1536.0f,
		"life depth: a float stops before water too shallow for it",
		(long)x, 1536);

	const HDLifeLevel level = { "TEST", 1, 0, 0, { 0, 0, 0, 1 }, 0, 0 };
	unsigned state = 1;

	p.bodies.push_back(b);
	HDLifeBuild(p, level, life_draw, &state);
	ok(p.floats == 1 && p.entries.size() == 1 &&
		HDLifeFloor1024(p.entries[0].x) == 1024,
		"life depth: two squares with water under them take one float, on "
		"the one deep enough", p.floats, 1);

	p.bodies[0] = life_body(3, 1, 0);
	p.bodies[0].rise = 121.3f;
	HDLifeBuild(p, level, life_draw, &state);
	ok(p.floats == 0 && p.unfit == 0,
		"life depth: a sheet over a floor at the surface takes no float",
		p.floats, 0);

	HDLifeBody all = life_body(5, 1, 2048);

	ok(HDLifeHoldSquare(all, need, 13) == 3,
		"life depth: where every square is deep enough the roll names the "
		"square it named before", HDLifeHoldSquare(all, need, 13), 3);

	all.river = 1;
	all.flowX = 1.0f;
	all.speed = 400.0f;
	all.squares[0].depth = 0;
	HDLifeRiverEnds(all);

	const int up = HDLifeUpstreamSquare(all, 5, 2, 40.0f);

	ok(up >= 1 && all.squares[(size_t)up].depth >= 40,
		"life depth: a river float whose upstream row is too shallow comes "
		"back on a square that holds it", up, 1);
}

/* The level load's pool (hd_pagecache.h): the thread-count rule, every item
 * once into its own slot whatever the thread count, the join in group order
 * with every run moved by its place, the first exception on the caller, the
 * byte gate that gives a throwing item's share back, and the FNV-1a 64 step
 * the build's fingerprint is made of. */
// The low 31 bits of a hash, for the report line of a failed check.
static long low(unsigned long long h)
{
	return (long)(h & 0x7FFFFFFF);
}

static unsigned long long hash_bytes(const char* text)
{
	HDFnv64 f;

	f.add(text, strlen(text));
	return f.h;
}

static void load_pool()
{
	// The rule: a wish of 1..16 is kept, above 16 is 16, 0 or less is the
	// machine's own count, clamped the same way.
	int machine = (int)std::thread::hardware_concurrency();

	if (machine < 1)
		machine = 1;

	if (machine > 16)
		machine = 16;

	ok(HDPoolThreads(1) == 1, "pool: 1 thread is 1", HDPoolThreads(1), 1);
	ok(HDPoolThreads(5) == 5, "pool: 5 threads are 5", HDPoolThreads(5), 5);
	ok(HDPoolThreads(16) == 16, "pool: 16 threads are 16",
		HDPoolThreads(16), 16);
	ok(HDPoolThreads(17) == 16, "pool: 17 threads are 16",
		HDPoolThreads(17), 16);
	ok(HDPoolThreads(1000) == 16, "pool: 1000 threads are 16",
		HDPoolThreads(1000), 16);
	ok(HDPoolThreads(0) == machine, "pool: 0 is the machine's count",
		HDPoolThreads(0), machine);
	ok(HDPoolThreads(-3) == machine, "pool: below 0 is the machine's count",
		HDPoolThreads(-3), machine);

	// No item, no call.
	int calls = 0;

	HDPoolRun(0, 8, [&](int) { calls++; });
	ok(calls == 0, "pool: no item, no call", calls, 0);

	/* Every item exactly once, into its own slot, for every thread count -
	 * items of very different sizes, so that they finish out of order. */
	const int items = 997;

	for (int threads = 1; threads <= 16; threads++)
	{
		std::vector<int> seen((size_t)items, 0);
		std::vector<unsigned long long> slot((size_t)items, 0);

		HDPoolRun(items, threads, [&](int at)
		{
			HDFnv64 f;

			for (int k = 0; k < (at * 37) % 2000; k++)
				f.put(k ^ at);

			seen[(size_t)at]++;
			slot[(size_t)at] = f.h;
		});

		int once_each = 0;
		HDFnv64 joined, serial;

		for (int at = 0; at < items; at++)
		{
			once_each += seen[(size_t)at] == 1 ? 1 : 0;
			joined.put(slot[(size_t)at]);

			HDFnv64 f;

			for (int k = 0; k < (at * 37) % 2000; k++)
				f.put(k ^ at);

			serial.put(f.h);
		}

		ok(once_each == items, "pool: every item once", once_each, items);
		ok(joined.h == serial.h,
			"pool: the slots joined in order are the serial answer",
			low(joined.h), low(serial.h));
	}

	/* The join of the index fill: each group makes a run of its own length
	 * with indices of its own, handed out biggest first; the runs appended
	 * in group order, each range's start moved by the run's place, give the
	 * list and the starts of one loop over the groups. */
	const int groups = 61;
	std::vector<unsigned int> one_loop;
	std::vector<unsigned int> one_start((size_t)groups, 0);

	for (int g = 0; g < groups; g++)
	{
		one_start[(size_t)g] = (unsigned int)one_loop.size();

		for (int i = 0; i < (g * 7919) % 300; i++)
			one_loop.push_back((unsigned int)(g * 100000 + i));
	}

	std::vector<int> order((size_t)groups);

	for (int g = 0; g < groups; g++)
		order[(size_t)g] = g;

	std::stable_sort(order.begin(), order.end(), [](int a, int b)
	{
		return (a * 7919) % 300 > (b * 7919) % 300;
	});

	for (int threads = 1; threads <= 16; threads *= 2)
	{
		std::vector<std::vector<unsigned int> > run((size_t)groups);
		std::vector<unsigned int> start((size_t)groups, 0);

		HDPoolRun(groups, threads, [&](int k)
		{
			const int g = order[(size_t)k];
			std::vector<unsigned int>& mine = run[(size_t)g];

			start[(size_t)g] = (unsigned int)mine.size();

			for (int i = 0; i < (g * 7919) % 300; i++)
				mine.push_back((unsigned int)(g * 100000 + i));
		});

		std::vector<unsigned int> joined;
		int starts = 0;

		for (int g = 0; g < groups; g++)
		{
			start[(size_t)g] += (unsigned int)joined.size();
			joined.insert(joined.end(), run[(size_t)g].begin(),
				run[(size_t)g].end());
			starts += start[(size_t)g] == one_start[(size_t)g] ? 1 : 0;
		}

		ok(joined == one_loop, "pool: the runs joined in group order are the "
			"one loop's list", (long)joined.size(), (long)one_loop.size());
		ok(starts == groups, "pool: every start moved by its run's place",
			starts, groups);
	}

	// An item that throws: the caller gets the exception, on one thread and
	// on eight.
	for (int threads = 1; threads <= 8; threads += 7)
	{
		bool caught = false;

		try
		{
			HDPoolRun(100, threads, [&](int at)
			{
				if (at == 41)
					throw 41;
			});
		}
		catch (int thrown)
		{
			caught = thrown == 41;
		}

		ok(caught, "pool: an item's exception reaches the caller", caught, 1);
	}

	/* The byte gate of the index fill (HDByteGate), as the fill holds it:
	 * every item takes a share of 60 under a budget of 96, so no two run at
	 * once. Without a throw, every item runs and the peak stays the one
	 * share. With item 0 throwing while it holds its share, the items that
	 * already wait for room must still start and finish, the exception must
	 * reach the caller, and the gate must hold nothing afterwards; a share
	 * kept by the throw would leave a waiting item waiting and the join
	 * would never return. */
	for (int threads = 1; threads <= 16; threads *= 4)
	{
		for (int throws = 0; throws <= 1; throws++)
		{
			HDByteGate gate(96.0);
			std::mutex seen_lock;
			double peak = 0.0;
			int started = 0, finished = 0;
			bool caught = false;

			try
			{
				HDPoolRun(24, threads, [&](int at)
				{
					const HDByteGate::Share share(gate, 60.0);

					{
						std::lock_guard<std::mutex> lock(seen_lock);

						started++;
						peak = std::max(peak, gate.InFlight());
					}

					if (throws && at == 0)
					{
						std::this_thread::sleep_for(
							std::chrono::milliseconds(20));
						throw std::bad_alloc();
					}

					std::lock_guard<std::mutex> lock(seen_lock);

					finished++;
				});
			}
			catch (const std::bad_alloc&)
			{
				caught = true;
			}

			ok(caught == (throws != 0), "gate: a throw under the gate reaches "
				"the caller", caught, throws);
			ok(started == finished + throws, "gate: every started item but "
				"the thrower finishes", started, finished + throws);
			ok(throws || started == 24, "gate: without a throw every item "
				"runs", started, 24);
			ok(gate.InFlight() == 0.0, "gate: nothing held after the join",
				(long)gate.InFlight(), 0);
			ok(peak == 60.0, "gate: one share of 60 at a time under 96",
				(long)peak, 60);
		}
	}

	// FNV-1a 64: the published values of the empty string, "a" and "foobar",
	// and put() and list() as the bytes they stand for.
	ok(hash_bytes("") == 0xcbf29ce484222325ull, "fnv: the empty string",
		low(hash_bytes("")), low(0xcbf29ce484222325ull));
	ok(hash_bytes("a") == 0xaf63dc4c8601ec8cull, "fnv: \"a\"",
		low(hash_bytes("a")), low(0xaf63dc4c8601ec8cull));
	ok(hash_bytes("foobar") == 0x85944171f73967e8ull, "fnv: \"foobar\"",
		low(hash_bytes("foobar")), low(0x85944171f73967e8ull));

	HDFnv64 by_put, by_bytes;
	const unsigned int word = 0x64636261u;		// "abcd" in memory order

	by_put.put(word);
	by_bytes.add("abcd", 4);
	ok(by_put.h == by_bytes.h, "fnv: put() is the value's bytes",
		low(by_put.h), low(by_bytes.h));

	HDFnv64 by_list, by_hand;
	const std::vector<unsigned short> shorts = { 1, 2, 3 };

	by_list.list(shorts);
	by_hand.put((unsigned int)3);
	by_hand.add(&shorts[0], 6);
	ok(by_list.h == by_hand.h, "fnv: list() is the count, then the elements",
		low(by_list.h), low(by_hand.h));
}

/* ------------------------------------------------------------ the splash -- */

// Lara's dive into deep water (game\effects.cpp, Splash) at a fall speed.
static HDSplashSetup splash_dive(int fall)
{
	HDSplashSetup s;

	s.x = 1000.0f;
	s.y = 500.0f;
	s.z = 2000.0f;
	s.innerRad = 32; s.innerSize = 8; s.innerRadVel = 320;
	s.innerYVel = -40 * fall;
	s.middleRad = 48; s.middleSize = 32; s.middleRadVel = 480;
	s.middleYVel = -20 * fall;
	s.outerRad = 32; s.outerSize = 128; s.outerRadVel = 544;
	return s;
}

/* The water seen from below (hd_water_under.h): the eye's depth under the
 * surface the extinction measures against. A room under a sheet takes the
 * depth under its body's surface; a room no sheet lies over takes the depth
 * that puts every point in the water - but under the deep look its body's
 * surface counts wherever it lies above the eye, the rule that keeps
 * JOBY4's sea surface in sight from room 6 (eye 14,558, the body of room 7
 * at 12,032). Snell's window: 48.6 degrees for water. */
static void water_under()
{
	printf("the water from below: the eye's depth and Snell's window\n");

	const float none = 1.0e6f;

	ok(HDWaterUnderDepth(0, 0, 14786.0f, 12032.0f, none) == 2754.0f,
		"water under: a room under a sheet, its surface's depth", 0, 0);
	ok(HDWaterUnderDepth(0, 1, 14786.0f, 12032.0f, none) == 2754.0f,
		"water under: ...the same under the deep look", 0, 0);
	ok(HDWaterUnderDepth(1, 0, 14558.0f, 12032.0f, none) == none,
		"water under: no sheet over the room, no deep look: all water", 0, 0);
	ok(HDWaterUnderDepth(1, 1, 14558.0f, 12032.0f, none) == 2526.0f,
		"water under: the deep look's sea, its surface over the eye", 0, 0);
	ok(HDWaterUnderDepth(1, 1, 11000.0f, 12032.0f, none) == none,
		"water under: ...not when that surface lies under the eye", 0, 0);
	ok(HDWaterUnderDepth(1, 1, 12032.0f, 12032.0f, none) == none,
		"water under: ...nor at its height", 0, 0);
	ok(HDWaterUnderDepth(0, 0, 11000.0f, 12032.0f, none) == -1032.0f,
		"water under: a room under a sheet keeps its sign", 0, 0);

	const float c = HDWaterSnellCos(1.333f);
	const long deg = (long)floorf(acosf(c) * 57.29578f * 10.0f + 0.5f);

	ok(deg == 486, "water under: Snell's window 48.6 degrees", deg, 486);
	ok(HDWaterSnellCos(1.0f) == 0.0f,
		"water under: no water, the window is the whole sky", 0, 0);
	ok(HDWaterSnellCos(0.5f) == 0.0f,
		"water under: an index under 1 is taken as 1", 0, 0);
}

/* The ripple of the remaster's still water art (hd_splash_math.h,
 * HDArtRipple): the length a fifth of the water's width and never under its
 * class's longest row, the deep-water rate at that length times 0.35 of the
 * class's calm, the travel a tenth of the part's span at the sea's steepness
 * and less for a flatter class. The rows are kWaterClass's (hd_render.cpp):
 * basin 260 / 2 / 0.6, channel 420 / 3 / 0.8, sea 1500 / 38 / 1.0; g 4300. */
static void art_ripple()
{
	printf("the art's ripple: length, rate and travel from the water\n");

	const float g = 4300.0f;
	const float steep = 38.0f / 1500.0f;
	const HDArtRippleWater sea = { 3538.0f, 1500.0f, 38.0f, 1.0f, 1.0f };
	float out[3];

	ok(HDArtRipple(sea, steep, g, 0.996f, out),
		"art ripple: JOBY4's sea ripples", 0, 0);
	ok((long)floorf(1.0f / out[0] + 0.5f) == 12182,
		"art ripple: ...a fifth of its width, 12,182 units",
		(long)floorf(1.0f / out[0] + 0.5f), 12182);
	ok((long)floorf(100.0f / out[2] + 0.5f) == 1205,
		"art ripple: ...a period of 12.05 s",
		(long)floorf(100.0f / out[2] + 0.5f), 1205);
	ok(fabsf(out[1] - 0.0996f) < 1.0e-6f,
		"art ripple: ...a tenth of its span", 0, 0);

	const HDArtRippleWater pot = { 0.3f, 260.0f, 2.0f, 0.6f, 1.0f };

	HDArtRipple(pot, steep, g, 0.357f, out);
	ok((long)floorf(1.0f / out[0] + 0.5f) == 260,
		"art ripple: a small water takes its class's longest row",
		(long)floorf(1.0f / out[0] + 0.5f), 260);
	ok((long)floorf(1000.0f * out[1] / 0.357f + 0.5f) == 30,
		"art ripple: a basin travels 0.30 of the sea's share",
		(long)floorf(1000.0f * out[1] / 0.357f + 0.5f), 30);

	const HDArtRippleWater basin = { 36.0f, 260.0f, 2.0f, 0.6f, 1.0f };
	float big[3];

	HDArtRipple(basin, steep, g, 1.0f, out);
	HDArtRipple(sea, steep, g, 1.0f, big);
	ok(out[0] > big[0] && out[2] > big[2],
		"art ripple: a larger water, a longer and slower wave", 0, 0);
	ok((long)floorf(100.0f / out[2] + 0.5f) == 638,
		"art ripple: ANDREA2's fountain of 36 squares, 6.38 s",
		(long)floorf(100.0f / out[2] + 0.5f), 638);

	HDArtRippleWater calm = basin;
	float half[3];

	calm.calm = 0.3f;
	HDArtRipple(calm, steep, g, 1.0f, half);
	ok(fabsf(half[2] * 2.0f - out[2]) < 1.0e-6f && half[0] == out[0],
		"art ripple: the rate follows the class's calm alone", 0, 0);

	HDArtRippleWater high = sea;

	high.waves = 4.0f;
	HDArtRipple(high, steep, g, 0.5f, out);
	ok(fabsf(out[1] - 0.05f) < 1.0e-6f,
		"art ripple: a row's waves never take it past a tenth", 0, 0);

	HDArtRippleWater flat = sea;

	flat.waves = 0.0f;
	ok(!HDArtRipple(flat, steep, g, 1.0f, out) && out[1] == 0.0f,
		"art ripple: waves 0 holds the art still", 0, 0);
	ok(!HDArtRipple(sea, steep, g, 0.0f, out) && out[0] == 0.0f &&
		out[1] == 0.0f && out[2] == 0.0f,
		"art ripple: no span, nothing moves", 0, 0);

	HDArtRippleWater none = sea;

	none.lambda = 0.0f;
	ok(!HDArtRipple(none, steep, g, 1.0f, out),
		"art ripple: no class row, no ripple", 0, 0);
}

/* The sea seen from below on the game's path (hd_water_under.h). The
 * classic layers stay out under an HD sky that has a cloud sheet or an
 * opaque band, and in a level with the deep look and the sheets on
 * whatever its sky - JOBY4's band alone, one flat colour, keeps them out.
 * The look's surface is the water's top over the dive: JOBY4's -5,120 over
 * an eye at y 14,790 in room 33 is 19,910 units up; an eye over it still
 * meets it a unit up. The look's own numbers stay inside the shader's
 * ranges: a floor between 0 and 1, the march shorter than the shafts'
 * reach under the surface, a rim narrower than the window itself (its
 * cosine runs from the critical 0.661 to 1), so that the window's middle
 * takes the whole light. */
static void water_look()
{
	printf("the sea from below: the layers' rule and the look's numbers\n");

	ok(!HDSkyLayersLeftOut(0, 0, 0),
		"water look: a bare band and no deep look keeps the layers", 0, 0);
	ok(HDSkyLayersLeftOut(1, 0, 0),
		"water look: a cloud sheet stands for the layers", 0, 0);
	ok(HDSkyLayersLeftOut(0, 1, 0),
		"water look: an opaque band stands for the layers", 0, 0);
	ok(HDSkyLayersLeftOut(0, 0, 1),
		"water look: the deep look leaves them out over a bare band", 0, 0);
	ok(HDSkyLayersLeftOut(1, 1, 1),
		"water look: ...and with a sheet as well", 0, 0);
	ok(HDWaterLookDepth(14790.0f, -5120.0f) == 19910.0f,
		"water look: the surface 19,910 units over an eye in room 33",
		(long)HDWaterLookDepth(14790.0f, -5120.0f), 19910);
	ok(HDWaterLookDepth(-6000.0f, -5120.0f) == 1.0f,
		"water look: an eye over the top meets it a unit up", 0, 0);

	float k[HD_LOOK_KNOBS];

	HDWaterLookKnobs(k);
	ok(HD_LOOK_KNOBS == 18, "water look: eighteen numbers", HD_LOOK_KNOBS, 18);
	ok(k[HD_LOOK_FLOOR] > 0.0f && k[HD_LOOK_FLOOR] < 1.0f,
		"water look: the noise's floor between 0 and 1",
		(long)(k[HD_LOOK_FLOOR] * 100.0f), 62);
	ok(k[HD_LOOK_LENGTH] < k[HD_LOOK_DEEP],
		"water look: the march shorter than the shafts' reach",
		(long)k[HD_LOOK_LENGTH], (long)k[HD_LOOK_DEEP]);
	ok(k[HD_LOOK_RIM] > 0.0f &&
		k[HD_LOOK_RIM] < 1.0f - HDWaterSnellCos(1.333f),
		"water look: the rim narrower than the window's span of cosine",
		(long)(k[HD_LOOK_RIM] * 100.0f), 30);
	ok(k[HD_LOOK_CELL] > 0.0f && k[HD_LOOK_WAVES] > 0.0f &&
		k[HD_LOOK_REACH] > 0.0f && k[HD_LOOK_POWER] > 1.0f,
		"water look: the cells, the waves' way, the reach and the power", 0,
		0);
}

/* The sea's sheet in the place of the art of its surface (hd_water_under.h,
 * HDWaterSeaOver, HDWaterSheetPath, HDWaterRippleScale, the knobs): the
 * sheet only with the sheets on, in a level with the deep look, the eye
 * under the water and the stand-in's surface over it; the art giving way
 * only in a frame whose look drew the sheet; the way the extinction
 * counts the room shader's; the ripple held to -1 .. 1; and the knobs'
 * order among themselves (the bands are the pictures', which the record
 * measures; no test here draws one). Numbers of JOBY4's dive:
 * the eye of the straight-up picture 2,065 under the basin, the clear zone
 * 2,048, the art 18,366 over the eye; the sea's two longest rows 38 and 16
 * high. */
static void water_sea()
{
	printf("the sea's sheet: the art's place from below\n");

	const float none = 1.0e6f;

	ok(HDWaterSeaOver(1, 1, 1, 2065.0f, none),
		"sea sheet: an eye under the stand-in's surface takes it", 0, 0);
	ok(!HDWaterSeaOver(0, 1, 1, 2065.0f, none),
		"sea sheet: ...not with the sheets off (hd_water 0)", 0, 0);
	ok(!HDWaterSeaOver(1, 0, 1, 2065.0f, none),
		"sea sheet: ...not without the deep look (every other level)", 0, 0);
	ok(!HDWaterSeaOver(1, 1, 0, 2065.0f, none),
		"sea sheet: ...not with the eye out of the water", 0, 0);
	ok(!HDWaterSeaOver(1, 1, 1, none, none),
		"sea sheet: ...not over the stand-in's surface (the art's own fade)",
		0, 0);
	ok(HDWaterSeaOver(1, 1, 1, HDWaterUnderDepth(1, 1, 12035.0f, 12032.0f,
		none), none),
		"sea sheet: three units under the basin of room 7 it is there", 0, 0);
	ok(!HDWaterSeaOver(1, 1, 1, HDWaterUnderDepth(1, 1, 11941.0f, 12032.0f,
		none), none),
		"sea sheet: 91 units over it, gone, as the art goes", 0, 0);

	ok(!HDWaterSeaGivesWay(true, false, -1.0e30f, -1.0e30f, -1.0e30f),
		"sea sheet: before the look's first sheet the art stays", 0, 0);
	ok(HDWaterSeaGivesWay(true, true, 7.0f, 7.0f, 6.0f),
		"sea sheet: drawn with this frame's clock, the art gives way", 0, 0);
	ok(HDWaterSeaGivesWay(true, true, 6.0f, 7.0f, 6.0f),
		"sea sheet: ...or with the clock before (the sky's bracket)", 0, 0);
	ok(!HDWaterSeaGivesWay(true, true, 5.0f, 7.0f, 6.0f),
		"sea sheet: a sheet two clocks old keeps the art", 0, 0);
	ok(!HDWaterSeaGivesWay(false, true, 7.0f, 7.0f, 6.0f),
		"sea sheet: no sea over the eye, the art stays", 0, 0);

	const float straight = HDWaterSheetPath(18366.0f, 1.0f, 2065.0f, 2048.0f);
	const float slant = HDWaterSheetPath(36732.0f, 0.5f, 2065.0f, 2048.0f);

	ok(straight > 16.9f && straight < 17.1f,
		"sea sheet: straight up the way is the depth less the clear zone",
		(long)straight, 17);
	ok(slant > 2081.9f && slant < 2082.1f,
		"sea sheet: at 30 degrees up twice the depth less the clear zone",
		(long)slant, 2082);
	ok(HDWaterSheetPath(1500.0f, 0.5f, 2065.0f, 2048.0f) == 0.0f,
		"sea sheet: nearer than the clear zone, no way", 0, 0);
	ok(HDWaterSheetPath(30000.0f, 0.0f, 2065.0f, 2048.0f) == 27952.0f,
		"sea sheet: a level ray counts its whole length",
		(long)HDWaterSheetPath(30000.0f, 0.0f, 2065.0f, 2048.0f), 27952);

	const float rows[8] = { 0.0042f, 0.0f, 0.5f, 38.0f, 0.009f, 0.0f, 0.7f,
		16.0f };
	const float flat[8] = {};

	ok(HDWaterRippleScale(rows) * 54.0f > 0.999f &&
		HDWaterRippleScale(rows) * 54.0f < 1.001f,
		"sea sheet: the ripple over the two rows' 54 units", 0, 0);
	ok(HDWaterRippleScale(flat) == 0.0f,
		"sea sheet: rows without a height, no ripple", 0, 0);

	float k[HD_LOOK_KNOBS];

	HDWaterLookKnobs(k);
	ok(k[HD_LOOK_ART] == 0.0f,
		"sea sheet: the art gives way wholly under the sheet", 0, 0);
	ok(k[HD_LOOK_RIPPLE] > 0.0f && k[HD_LOOK_RIPPLE] < 1.0f,
		"sea sheet: the ripple never takes the sheet below black",
		(long)(k[HD_LOOK_RIPPLE] * 100.0f), 80);
	ok(k[HD_LOOK_SEA_DISC] > k[HD_LOOK_DISC] &&
		k[HD_LOOK_SEA_SHAFTS] > k[HD_LOOK_SHAFTS] &&
		k[HD_LOOK_SEA_UNDERSIDE] > k[HD_LOOK_UNDERSIDE],
		"sea sheet: the look stronger where the sheet takes the art's place",
		0, 0);
	ok(k[HD_LOOK_SHEET] > 0.0f && k[HD_LOOK_SHEET] < k[HD_LOOK_SEA_UNDERSIDE],
		"sea sheet: the sheet dimmer than the lit underside",
		(long)(k[HD_LOOK_SHEET] * 100.0f), 10);
	ok(k[HD_LOOK_PATCHES] >= 0.0f,
		"sea sheet: the shafts' noise only lights it", 0, 0);
	ok(kHDWaterSheetEdge > 0.0f,
		"sea sheet: the sheet comes in over a way in from the box's edge",
		(long)kHDWaterSheetEdge, 4096);
}

/* The room whose art is the sea's surface (hd_water_under.h,
 * HDWaterSurfaceRoom), on rooms shaped as JOBY4's and RICH3's: the water
 * room whose art reaches highest, within the reach of the water's top. In
 * JOBY4 room 19's art reaches -4,269 under a top of -5,120 (rooms 11, 50,
 * 90 and 123 carry it), the next water room's -1,280, and the dry room 1's
 * art -4,352, higher than the sea's but no water room's. In RICH3 the one
 * water room's art is its own floor, 4,864 under its top. */
static void water_surface()
{
	printf("the sea's surface: the room whose art is it\n");

	const float reach = kHDWaterSurfaceReach;
	const int wet[5] = { 0, 1, 1, 1, 1 };
	const float ceilTop[5] = { -4352.0f, 15872.0f, -5120.0f, -1280.0f,
		1.0e9f };
	const int art[5] = { 1, 1, 1, 1, 0 };
	const float artTop[5] = { -4352.0f, -4269.0f, 11249.0f, -1280.0f,
		-9000.0f };

	ok(HDWaterSurfaceRoom(wet, ceilTop, art, artTop, 5, reach) == 1,
		"sea surface: the water room whose art reaches highest",
		HDWaterSurfaceRoom(wet, ceilTop, art, artTop, 5, reach), 1);

	// A dry room's art, higher still, is no sea's surface.
	const int allWet[5] = { 1, 1, 1, 1, 1 };

	ok(HDWaterSurfaceRoom(allWet, ceilTop, art, artTop, 5, reach) == 0,
		"sea surface: ...the same room taken as wet wins",
		HDWaterSurfaceRoom(allWet, ceilTop, art, artTop, 5, reach), 0);

	/* Room 4 has no art throughout, so its -9,000 never counts; here room
	 * 1 loses its art as well. */
	const int noArt[5] = { 0, 0, 1, 1, 0 };

	ok(HDWaterSurfaceRoom(wet, ceilTop, noArt, artTop, 5, reach) == -1,
		"sea surface: without room 1's art none, the next 3,840 under",
		HDWaterSurfaceRoom(wet, ceilTop, noArt, artTop, 5, reach), -1);
	ok(HDWaterSurfaceRoom(wet, ceilTop, noArt, artTop, 5, 3839.0f) == -1,
		"sea surface: ...none with a reach a unit short of it",
		HDWaterSurfaceRoom(wet, ceilTop, noArt, artTop, 5, 3839.0f), -1);
	ok(HDWaterSurfaceRoom(wet, ceilTop, noArt, artTop, 5, 3840.0f) == 3,
		"sea surface: the reach counts its own end",
		HDWaterSurfaceRoom(wet, ceilTop, noArt, artTop, 5, 3840.0f), 3);

	// RICH3: one water room, its art its own floor under a top of -4,864.
	const int rWet[2] = { 0, 1 };
	const float rCeil[2] = { -2048.0f, -4864.0f };
	const int rArt[2] = { 1, 1 };
	const float rTop[2] = { -6400.0f, 0.0f };

	ok(HDWaterSurfaceRoom(rWet, rCeil, rArt, rTop, 2, reach) == -1,
		"sea surface: none in a level whose water art is its floor",
		HDWaterSurfaceRoom(rWet, rCeil, rArt, rTop, 2, reach), -1);

	// A tie goes to the lower number; no water room, no surface.
	const float tie[5] = { 0.0f, -4269.0f, -4269.0f, 0.0f, 0.0f };
	const int dry[5] = { 0, 0, 0, 0, 0 };

	ok(HDWaterSurfaceRoom(wet, ceilTop, art, tie, 5, reach) == 1,
		"sea surface: a tie goes to the lower number",
		HDWaterSurfaceRoom(wet, ceilTop, art, tie, 5, reach), 1);
	ok(HDWaterSurfaceRoom(dry, ceilTop, art, artTop, 5, reach) == -1,
		"sea surface: no water room, no surface",
		HDWaterSurfaceRoom(dry, ceilTop, art, artTop, 5, reach), -1);
	ok(HDWaterSurfaceRoom(wet, ceilTop, art, artTop, 0, reach) == -1,
		"sea surface: no room at all", 0, 0);
	ok(kHDWaterSurfaceReach > 851.0f && kHDWaterSurfaceReach < 3840.0f,
		"sea surface: the reach between JOBY4's sea and its next art",
		(long)kHDWaterSurfaceReach, 2048);
}

/* The sky band's caps (hd_sky_cap.h, HDSkyCapBuild), on a band shaped as
 * the remaster's: rings at three heights, eight edges round with the seam
 * twice (u 0 to 255), unlit, in the sky group past the rooms, between a lit
 * cloud sheet and an unlit flat sheet (RICH3 carries one); and a room group
 * of the same shape in front of it, which is no sky. */
static trf::remaster::TrgVertex sky_cap_vertex(int x, int y, int z, int u,
	int v, int light)
{
	trf::remaster::TrgVertex o;

	o.x = (int16_t)x;
	o.y = (int16_t)y;
	o.z = (int16_t)z;
	o.u = (uint8_t)u;
	o.v = (uint8_t)v;
	o.layer = 3;
	o.colour1[0] = o.colour1[1] = o.colour1[2] = (uint8_t)light;
	o.colour2[0] = o.colour2[1] = o.colour2[2] = (uint8_t)light;
	return o;
}

/* One part of `rings` rings round the axis, at heights `ys` and radii
 * `radius`, appended to `trg` and `g` as one run of `pass`. */
static void sky_cap_band(trf::remaster::Trg& trg, trf::remaster::TrgGroup& g,
	int pass, int rings, const int* ys, const int* radius, int light)
{
	const int seg = 8;
	const uint32_t first = (uint32_t)trg.vertices.size();
	trf::remaster::TrgPart part;

	for (int k = 0; k < rings; k++)
		for (int i = 0; i <= seg; i++)
		{
			const double a = 6.283185307179586 * i / seg;

			trg.vertices.push_back(sky_cap_vertex(
				(int)lround(radius[k] * cos(a)), ys[k],
				(int)lround(radius[k] * sin(a)), i * 255 / seg,
				20 + 100 * k, light));
		}

	for (int p = 0; p < 3; p++)
		part.pass[p].start = (uint32_t)trg.indices.size();

	for (int k = 0; k + 1 < rings; k++)
		for (int i = 0; i < seg; i++)
		{
			const uint32_t a = first + (uint32_t)(k * (seg + 1) + i);
			const uint32_t b = a + (uint32_t)(seg + 1);
			const uint32_t t[6] = { a, b, b + 1, a, b + 1, a + 1 };

			trg.indices.insert(trg.indices.end(), t, t + 6);
		}

	part.pass[pass].count = (uint32_t)trg.indices.size() -
		part.pass[pass].start;

	for (int p = pass + 1; p < 3; p++)
		part.pass[p].start = (uint32_t)trg.indices.size();

	g.parts.push_back(part);
}

// Whether the parts tile [0, indices) in part and pass order.
static bool sky_cap_tiles(const trf::remaster::Trg& trg)
{
	uint32_t at = 0;

	for (const trf::remaster::TrgGroup& g : trg.groups)
		for (const trf::remaster::TrgPart& p : g.parts)
			for (const trf::remaster::TrgPart::Pass& run : p.pass)
			{
				if (run.start != at)
					return false;

				at += run.count;
			}

	return at == trg.indices.size();
}

static void sky_cap()
{
	printf("the sky band's caps\n");

	const int ys[3] = { -3956, 0, 3956 };
	const int open[3] = { 1060, 4096, 1060 };
	const int flat[1] = { -2534 };
	const int wide[1] = { 24000 };
	trf::remaster::Trg trg;

	trg.groups.resize(2);
	sky_cap_band(trg, trg.groups[0], 0, 3, ys, open, 0);	// a room
	sky_cap_band(trg, trg.groups[1], 1, 3, ys, open, 90);	// lit rings
	sky_cap_band(trg, trg.groups[1], 1, 3, ys, open, 0);	// the band
	sky_cap_band(trg, trg.groups[1], 0, 1, flat, wide, 0);	// flat, unlit

	const trf::remaster::Trg before = trg;
	const HDSkyCapCount n = HDSkyCapBuild(trg, 1);
	const trf::remaster::TrgPart& band = trg.groups[1].parts[1];
	const uint32_t bandEnd = before.groups[1].parts[1].pass[1].start +
		before.groups[1].parts[1].pass[1].count;

	ok(n.bands == 1 && n.poles == 2,
		"sky cap: one band, both its poles closed", n.bands * 10 + n.poles,
		12);
	ok(n.triangles == 16 && n.vertices == 16,
		"sky cap: a wedge and a centre corner per ring edge, 8 + 8",
		n.triangles * 100 + n.vertices, 1616);
	ok(band.pass[1].count == before.groups[1].parts[1].pass[1].count + 48,
		"sky cap: the wedges join the band's own run",
		(long)band.pass[1].count, (long)before.groups[1].parts[1].pass[1].count
		+ 48);
	ok(sky_cap_tiles(trg) && trg.groups[1].parts[2].pass[0].start ==
		before.groups[1].parts[2].pass[0].start + 48,
		"sky cap: the later parts move up, the parts still tile", 0, 0);
	ok(trg.groups[0].parts[0].pass[0].count ==
		before.groups[0].parts[0].pass[0].count &&
		trg.groups[1].parts[0].pass[1].count ==
		before.groups[1].parts[0].pass[1].count &&
		trg.groups[1].parts[2].pass[0].count ==
		before.groups[1].parts[2].pass[0].count,
		"sky cap: a room, a lit part and a flat sheet get none", 0, 0);

	/* Each wedge: two corners of the pole ring, run the other way round
	 * than the band's triangle on that edge, and a new corner at the ring's
	 * centre in its plane, on the edge's page and row, its u between. */
	int good = 0, closed = 0, dark = 0;

	for (uint32_t t = bandEnd; t < bandEnd + 48; t += 3)
	{
		const uint32_t a = trg.indices[t], b = trg.indices[t + 1];
		const uint32_t c = trg.indices[t + 2];
		const trf::remaster::TrgVertex& va = trg.vertices[a];
		const trf::remaster::TrgVertex& vb = trg.vertices[b];
		const trf::remaster::TrgVertex& vc = trg.vertices[c];
		const int lo = va.u < vb.u ? va.u : vb.u;
		const int hi = va.u < vb.u ? vb.u : va.u;

		good += c >= before.vertices.size() && va.y == vb.y &&
			vc.y == va.y && (va.y == -3956 || va.y == 3956) && vc.x == 0 &&
			vc.z == 0 && vc.v == va.v && vc.layer == va.layer &&
			vc.u >= lo && vc.u <= hi ? 1 : 0;
		dark += vc.colour1[0] | vc.colour1[1] | vc.colour1[2] |
			vc.colour2[0] | vc.colour2[1] | vc.colour2[2] ? 0 : 1;

		for (uint32_t s = band.pass[1].start; s < bandEnd; s += 3)
			for (int k = 0; k < 3; k++)
				closed += trg.indices[s + (uint32_t)k] == b &&
					trg.indices[s + (uint32_t)((k + 1) % 3)] == a ? 1 : 0;
	}

	ok(good == 16, "sky cap: each wedge on the ring, its centre in the "
		"ring's plane, page and row", good, 16);
	ok(closed == 16, "sky cap: each ring edge run once each way round",
		closed, 16);
	ok(dark == 16 && HDSkyCapUnlit(trg, band.pass[1].start,
		band.pass[1].count),
		"sky cap: the wedges as unlit as the band", dark, 16);

	// With every group a room nothing is a sky, and nothing changes.
	trf::remaster::Trg rooms = before;
	const HDSkyCapCount none = HDSkyCapBuild(rooms, 2);

	ok(none.triangles == 0 && rooms.indices == before.indices &&
		rooms.vertices.size() == before.vertices.size(),
		"sky cap: no sky group, no wedge", none.triangles, 0);

	// A pole closed already (its ring at the axis) keeps; the other closes.
	const int pointed[3] = { 0, 4096, 1060 };
	trf::remaster::Trg one;

	one.groups.resize(1);
	sky_cap_band(one, one.groups[0], 1, 3, ys, pointed, 0);

	const HDSkyCapCount half = HDSkyCapBuild(one, 0);

	ok(half.poles == 1 && half.triangles == 8 && sky_cap_tiles(one),
		"sky cap: a pole closed already keeps, the other closes",
		half.poles * 10 + half.triangles, 18);

	// Two rings only: a band, both rings its poles.
	const int two[2] = { -100, 100 };
	const int twoR[2] = { 1000, 1000 };
	trf::remaster::Trg ring;

	ring.groups.resize(1);
	sky_cap_band(ring, ring.groups[0], 1, 2, two, twoR, 0);

	ok(HDSkyCapBuild(ring, 0).triangles == 0,
		"sky cap: two heights are no band (three wanted)", 0, 0);
}

/* Whether a level takes the deep look (hd_water_under.h, HDWaterDeepLook):
 * its water mostly water no sheet lies over, and a sea's surface over the
 * dive. JOBY4: 132 fallback rooms against the 2 under its basin, the
 * surface room 19; RICH3: its one flooded room 190 against none, no
 * surface; RICH2 and ANDY3, the next shares over the fifteen levels, 1.33
 * and 1.22. */
static void water_deep()
{
	printf("the deep look: a sea's share and its surface\n");

	const int share = 4;

	ok(HDWaterDeepLook(132, 2, share, 19), "deep: JOBY4, its share and room 19",
		HDWaterDeepLook(132, 2, share, 19) ? 1 : 0, 1);
	ok(!HDWaterDeepLook(1, 0, share, -1), "deep: not RICH3, no surface",
		HDWaterDeepLook(1, 0, share, -1) ? 1 : 0, 0);
	ok(HDWaterDeepLook(1, 0, share, 190),
		"deep: ...the share alone passed it, 1 against 0",
		HDWaterDeepLook(1, 0, share, 190) ? 1 : 0, 1);
	ok(!HDWaterDeepLook(132, 2, share, -1),
		"deep: JOBY4's share without a surface is no sea",
		HDWaterDeepLook(132, 2, share, -1) ? 1 : 0, 0);
	ok(!HDWaterDeepLook(4, 3, share, 0), "deep: RICH2's 1.33, a surface or not",
		HDWaterDeepLook(4, 3, share, 0) ? 1 : 0, 0);
	ok(!HDWaterDeepLook(11, 9, share, 5), "deep: ANDY3's 1.22",
		HDWaterDeepLook(11, 9, share, 5) ? 1 : 0, 0);
	ok(!HDWaterDeepLook(8, 2, share, 3),
		"deep: four times the sheeted rooms is not more than four",
		HDWaterDeepLook(8, 2, share, 3) ? 1 : 0, 0);
	ok(HDWaterDeepLook(9, 2, share, 0),
		"deep: room 0 is a surface room", HDWaterDeepLook(9, 2, share, 0) ?
		1 : 0, 1);
	ok(!HDWaterDeepLook(0, 0, share, 19), "deep: no water room at all",
		HDWaterDeepLook(0, 0, share, 19) ? 1 : 0, 0);
}

/* The wet floor over a polished floor (hd_water_under.h): a mirror face
 * overlapping a 256-unit cell held a unit in, and a wet point giving its own
 * mirror to the marble floors' where they mirrored its height. The cell at
 * 46,080, 67,584 of RICH2's corridor mouth; a split's crease runs across a
 * cell's corners, so a face along one side only meets it. */
static void wet_polish()
{
	printf("the wet floor over a polished floor: the cells and the mirror\n");

	// A face covering the cell, as a floor's two triangles do.
	const float ax[3] = { 45568.0f, 47104.0f, 45568.0f };
	const float az[3] = { 67072.0f, 67072.0f, 68608.0f };

	ok(HDWetCellOnFace(ax, az, 46080.0f, 67584.0f, 256.0f, 1.0f),
		"polished: a face over the cell overlaps it", 0, 1);

	// A face beside the cell that only meets its edge does not.
	const float bx[3] = { 45824.0f, 46080.0f, 45824.0f };
	const float bz[3] = { 67584.0f, 67584.0f, 67840.0f };

	ok(!HDWetCellOnFace(bx, bz, 46080.0f, 67584.0f, 256.0f, 1.0f),
		"polished: a face meeting the cell's edge does not count", 1, 0);
	ok(HDWetCellOnFace(bx, bz, 46080.0f, 67584.0f, 256.0f, 0.0f),
		"polished: ...it does with the cell not held in", 0, 1);

	/* A face whose box overlaps the cell but whose long edge passes its
	 * corner: the triangle's edge separates them. */
	const float cx[3] = { 45900.0f, 46200.0f, 45900.0f };
	const float cz[3] = { 67400.0f, 67400.0f, 67700.0f };

	ok(!HDWetCellOnFace(cx, cz, 46100.0f, 67600.0f, 256.0f, 1.0f),
		"polished: the face's own edge separates a cell past it", 1, 0);
	ok(HDWetCellOnFace(cx, cz, 46000.0f, 67500.0f, 256.0f, 1.0f),
		"polished: ...a cell in its corner overlaps it", 0, 1);

	// Wound the other way round, the same answers.
	const float rx[3] = { cx[0], cx[2], cx[1] };
	const float rz[3] = { cz[0], cz[2], cz[1] };

	ok(!HDWetCellOnFace(rx, rz, 46100.0f, 67600.0f, 256.0f, 1.0f) &&
		HDWetCellOnFace(rx, rz, 46000.0f, 67500.0f, 256.0f, 1.0f),
		"polished: either winding", 0, 1);

	// A face with no area overlaps nothing.
	const float dx[3] = { 46000.0f, 46200.0f, 46400.0f };
	const float dz[3] = { 67600.0f, 67700.0f, 67800.0f };

	ok(!HDWetCellOnFace(dx, dz, 46080.0f, 67584.0f, 256.0f, 1.0f),
		"polished: a face with no area", 1, 0);

	// The frame's mirrored planes, the most covering first.
	const int planes[3] = { 0, -768, -2560 };

	ok(HDWetGivesWay(0, planes, 2, 3, 16), "gives way: the floor mirrored",
		0, 1);
	ok(HDWetGivesWay(-14, planes, 2, 3, 16),
		"gives way: ...a face's height a few units off it", 0, 1);
	ok(!HDWetGivesWay(-2560, planes, 2, 3, 16),
		"gives way: not where the cap left the height unmirrored", 1, 0);
	ok(!HDWetGivesWay(0, planes, 0, 3, 16),
		"gives way: not where the marble mirrored nothing", 1, 0);
	ok(!HDWetGivesWay(-256, planes, 3, 3, 16),
		"gives way: not a click off every mirrored plane", 1, 0);
	ok(HDWetGivesWay(-2560, planes, 9, 3, 16),
		"gives way: the planes read no further than the list", 0, 1);
}

/* The classic struct stepped as UpdateSplashes steps it: at 0 the rings
 * stand where S_DrawSplashes draws them first (twice the struct's radius
 * and size), the column rises and falls back to the surface, and the life
 * runs out two a tick after that - a deeper dive lasts longer. Between two
 * ticks the HD form lies between them. */
static void splash_motion()
{
	printf("the splash: the classic struct, tick by tick\n");

	const HDSplashSetup s = splash_dive(128);
	const HDSplashNow at0 = HDSplashAt(s, 0.0f);

	ok(at0.alive == 1, "a splash lives at its start", at0.alive, 1);
	ok((long)at0.innerR == 80, "its inner ring stands at 2 * (32 + 8)",
		(long)at0.innerR, 80);
	ok((long)at0.outerR == 320, "its outer ring at 2 * (32 + 128)",
		(long)at0.outerR, 320);
	ok((long)(at0.life * 62.0f + 0.5f) == 62, "its life is 62",
		(long)(at0.life * 62.0f + 0.5f), 62);

	// The column: up, a highest tick, back down; then the life runs out.
	HDSplashTick t = HDSplashStart(s);
	int peak = 0, peakTick = 0, landed = -1, gone = -1;

	for (int i = 1; i < 600 && t.alive; i++)
	{
		HDSplashStep(t);

		if (-t.innerY > peak)
		{
			peak = -t.innerY;
			peakTick = i;
		}

		if (landed < 0 && t.innerY == 0)
			landed = i;

		if (!t.alive)
			gone = i;
	}

	ok(peakTick > 0 && landed > peakTick, "the column rises before it lands",
		landed, peakTick);
	ok(gone == landed + 30, "and the life runs out 31 ticks with the landing",
		gone, landed + 30);

	int deeper = -1;
	HDSplashTick d = HDSplashStart(splash_dive(192));

	for (int i = 1; i < 600 && d.alive; i++)
	{
		HDSplashStep(d);

		if (!d.alive)
			deeper = i;
	}

	ok(deeper > gone, "a faster fall makes a longer splash", deeper, gone);

	const HDSplashNow end = HDSplashAt(s, (float)(gone + 1) / 30.0f);

	ok(end.alive == 0, "after its last tick it is gone", end.alive, 0);

	// Between two ticks: between their values.
	const HDSplashNow a = HDSplashAt(s, 5.0f / 30.0f);
	const HDSplashNow b = HDSplashAt(s, 6.0f / 30.0f);
	const HDSplashNow m = HDSplashAt(s, 5.5f / 30.0f);

	ok(m.outerR >= a.outerR && m.outerR <= b.outerR,
		"half a tick on, the ring lies between the two ticks",
		(long)m.outerR, (long)a.outerR);
	ok(fabsf(m.outerR - 0.5f * (a.outerR + b.outerR)) < 0.01f,
		"...half way", (long)(m.outerR * 100.0f),
		(long)(50.0f * (a.outerR + b.outerR)));
}

/* The cards: 52 at the start (12 + 16 crown, 24 drops), each crown card no
 * taller than its column, the drops gone when they are back at the
 * surface, the same seed the same cards, another seed others. */
static void splash_cards()
{
	printf("the splash: its cards\n");

	const HDSplashSetup s = splash_dive(128);
	HDCard c[kSplashCards], c2[kSplashCards];
	const int n0 = HDSplashCards(s, 7u, 0.0f, c, kSplashCards);

	ok(n0 == kSplashCards, "a new splash has all its cards", n0,
		kSplashCards);

	const float t = 0.2f;
	const int n = HDSplashCards(s, 7u, t, c, kSplashCards);
	const int n2 = HDSplashCards(s, 7u, t, c2, kSplashCards);
	bool same = n == n2;

	for (int i = 0; same && i < n; i++)
		same = c[i].x == c2[i].x && c[i].y == c2[i].y && c[i].alpha ==
			c2[i].alpha;

	ok(same, "the same seed draws the same cards", n2, n);

	const int n3 = HDSplashCards(s, 8u, t, c2, kSplashCards);
	bool other = false;

	for (int i = 0; i < n && i < n3; i++)
		other = other || c[i].x != c2[i].x;

	ok(other, "another seed draws other cards", n3, n);

	// No crown card reaches over its column's whole height at 1.15 pace.
	float highest = 0.0f;

	for (float u = 0.0f; u < 2.0f; u += 1.0f / 60.0f)
	{
		const HDSplashNow now = HDSplashAt(s, u);

		if (now.innerH > highest)
			highest = now.innerH;
	}

	bool under = true;

	for (int i = 0; i < kSplashCrownInner + kSplashCrownMiddle && i < n; i++)
		under = under && s.y - c[i].y <= 0.5f * highest + 0.5f;

	ok(under, "a crown card's middle stands at most half the column up",
		(long)highest, 0);

	int drops = 0;

	for (int i = 0; i < n; i++)
		drops += c[i].page == HD_CARD_DROPS;

	ok(drops > 0, "drops fly while the column stands", drops, 1);

	const int late = HDSplashCards(s, 7u, 0.9f, c, kSplashCards);
	int dropsLate = 0;

	for (int i = 0; i < late; i++)
		dropsLate += c[i].page == HD_CARD_DROPS;

	ok(dropsLate == 0, "and are back in the water at 0.9 s", dropsLate, 0);
}

/* The fade: a crown card's share follows the classic life - whole while
 * the column stands, falling with the life after it lands, nothing when
 * the splash is gone. */
static void splash_fade()
{
	printf("the splash: the fade\n");

	const HDSplashSetup s = splash_dive(128);
	HDCard c[kSplashCards];
	float last = 1.0e9f;
	bool falls = true;
	int steps = 0;

	for (float t = 0.6f; t < 3.0f; t += 0.1f)
	{
		const int n = HDSplashCards(s, 3u, t, c, kSplashCards);

		if (!n)
			break;

		float sum = 0.0f;

		for (int i = 0; i < kSplashCrownInner && i < n; i++)
			sum += c[i].alpha;

		falls = falls && sum <= last + 1.0e-4f;
		last = sum;
		steps++;
	}

	ok(steps > 3 && falls, "the crown's share falls while the life runs out",
		steps, 4);
	ok(HDSplashCards(s, 3u, 5.0f, c, kSplashCards) == 0,
		"and the splash draws nothing at 5 s", 0, 0);

	float ring[4];

	ok(HDSplashRings(s, 0.1f, ring) && ring[1] == 1.0f && ring[3] == 0.6f,
		"the rings start at strengths 1.0 and 0.6", (long)(ring[1] * 10.0f),
		10);
	ok(!HDSplashRings(s, 5.0f, ring), "and are gone with the splash", 0, 0);
}

/* The tank: its spark's card lives as long as the classic spark (14 to 17
 * ticks), rises and grows from a quarter to the whole of its size; the
 * tank runs 105 ticks; the film comes up over 19 ticks while it sprays,
 * dries with the lasers' count after it, and is dry before it. */
static void splash_tank()
{
	printf("the splash: the fish tank\n");

	int longest = 0, shortest = 99;
	bool rises = true, grows = true;

	for (unsigned int seed = 1; seed < 200; seed++)
	{
		HDCard c0, c1;
		int life = 0;

		for (int k = 0; k < 40; k++)
			if (HDTankPuff(0.0f, 0.0f, 0.0f, 64, seed, (float)k / 30.0f, &c1))
				life = k + 1;

		longest = life > longest ? life : longest;
		shortest = life < shortest ? life : shortest;

		if (HDTankPuff(0.0f, 0.0f, 0.0f, 64, seed, 0.0f, &c0) &&
			HDTankPuff(0.0f, 0.0f, 0.0f, 64, seed, 10.0f / 30.0f, &c1))
		{
			rises = rises && c1.y < c0.y;
			grows = grows && c1.half > c0.half;
		}
	}

	ok(shortest == 14 && longest == 17, "a spark's card lives 14 to 17 ticks",
		shortest * 100 + longest, 1417);
	ok(rises, "it rises, as the classic spark does", rises, 1);
	ok(grows, "and grows", grows, 1);

	int running = 1;
	const int f104 = HDTankFlagsAfter(104, &running);

	ok(running == 1 && f104 > 819, "the tank still runs after 104 ticks",
		f104, 820);
	ok(HDTankFlagsAfter(105, &running) == 810 && running == 0,
		"and stops after the 105th, at 810", running, 0);

	ok(HDFilmStrength(0, 4096, 1, 44) == 0.0f, "dry before the tank breaks",
		0, 0);
	ok(HDFilmStrength(1, 4096, 1, 64) == 0.0f, "...and at its first tick", 0,
		0);
	ok(HDFilmStrength(1, 3584, 1, 64) == 0.5f, "half wet at 3584", 1, 1);
	ok(HDFilmStrength(1, 3072, 1, 64) == 1.0f, "wet from 3072 on", 1, 1);

	const int f18 = HDTankFlagsAfter(18, 0);

	ok(f18 > 3072 && HDTankFlagsAfter(19, 0) <= 3072,
		"which the tank reaches on its 19th tick", f18, 3073);
	ok(HDFilmStrength(2, 810, 1, 32) == 0.5f, "half dry at the lasers' 32",
		1, 1);
	ok(HDFilmStrength(2, 810, 1, 0) == 0.0f, "dry at their 0", 0, 0);
	ok(HDFilmStrength(2, 810, 0, 32) == 0.0f, "and once they are gone", 0, 0);
}

/* The tank's mesh, as RICH2's FISHTANK draws it: a few of its 69 vertices
 * (MeshDump on the classic rich2.trc, slot 413) - the top behind the
 * glass, a row of the apron, the foot on the floor. */
static const float kTankMeshSample[] =
{
	-1589.0f, -1277.0f, -545.0f,	551.0f, -1276.0f, -543.0f,
	-1536.0f, -1276.0f, -1540.0f,	512.0f, -1276.0f, -516.0f,
	-1022.0f, -1236.0f, -286.0f,	-2.0f, -930.0f, 115.0f,
	-2106.0f, 6.0f, 82.0f,		1080.0f, 6.0f, 81.0f,
	-512.0f, 6.0f, 350.0f
};

/* The tank breaking: its age out of item_flags[1], its shape out of its
 * mesh and the burst out of its glass. */
static void splash_tank_burst()
{
	printf("the splash: the tank's burst\n");

	bool ticks = HDTankTicksOf(4096) == 0;

	for (int k = 1; k <= 105; k++)
		ticks = ticks && HDTankTicksOf(HDTankFlagsAfter(k, 0)) == k;

	ok(ticks, "the tank's ticks come back out of item_flags[1]", ticks, 1);

	const HDTankShape sh = HDTankShapeOf(kTankMeshSample, 9);

	ok(sh.ok && sh.top == -1277.0f && sh.floor == 6.0f,
		"the shape: the top at -1277 and the floor at 6", (long)sh.top, -1277);
	ok(sh.glassZ == -516.0f && sh.footZ == 350.0f,
		"the glass at -516 and the foot 350 out", (long)sh.glassZ, -516);
	ok(sh.topX0 == -1589.0f && sh.topX1 == 551.0f && sh.footX0 == -2106.0f &&
		sh.footX1 == 1080.0f, "the top 2140 wide, the foot 3186",
		(long)(sh.footX1 - sh.footX0), 3186);
	ok(!HDTankShapeOf(kTankMeshSample, 0).ok, "no mesh, no shape", 0, 0);

	HDCard c[kTankBurstCards];
	bool out = true, over = true, gone = true;
	int most = 0;

	for (float t = 0.0f; t < 1.3f; t += 1.0f / 60.0f)
	{
		const int n = HDTankBurst(sh, 11u, t, c, kTankBurstCards);

		most = n > most ? n : most;

		for (int i = 0; i < n; i++)
		{
			out = out && c[i].z >= sh.glassZ;
			over = over && c[i].y <= sh.floor && c[i].alpha >= 0.0f &&
				c[i].alpha <= 1.0f;
		}
	}

	gone = HDTankBurst(sh, 11u, 1.25f, c, kTankBurstCards) == 0;
	ok(most > kTankBurstCards / 2, "the burst throws most of its cards at once",
		most, kTankBurstCards / 2);
	ok(out, "out of the glass, never behind it", out, 1);
	ok(over, "over the floor, with shares of 0..1", over, 1);
	ok(gone, "and is over after 1.2 s", gone, 1);
	ok(HDTankBurst(sh, 11u, 0.5f, c, 4) <= 4, "within the room it is given",
		4, 4);
}

/* The tank's spray: a spark's card stands on its point (its lower edge
 * there, not half of it under the floor) and is as strong as a crown card
 * of the middle ring at the whole strength. */
static void splash_tank_spray()
{
	printf("the splash: the tank's spray\n");

	bool stands = true;
	float most = 0.0f;

	for (unsigned int seed = 1; seed < 100; seed++)
		for (int k = 0; k < 14; k++)
		{
			HDCard p;
			const float t = (float)k / 30.0f;

			if (!HDTankPuff(0.0f, 0.0f, 0.0f, 64, seed, t, &p))
				continue;

			stands = stands && p.y + p.tall <= 0.5f && p.tall > 0.0f;
			most = p.alpha > most ? p.alpha : most;
		}

	ok(stands, "a spray card stands on its spark's point", stands, 1);
	ok(most == kTankSprayShare && kTankSprayShare == 0.8f,
		"and is 0.8 strong at the whole strength", (long)(most * 10.0f), 8);
}

/* The tank's water itself: the classic scale on the tank's own clock, the
 * apron coming out of the glass, the fade before KillItem, the way down
 * the water and the share by the eye's angle. */
static void splash_tank_dome()
{
	printf("the splash: the tank's water\n");

	const HDTankDome d0 = HDTankDomeAt(0.0f);
	const HDTankDome d1 = HDTankDomeAt(1.0f);
	const HDTankDome d105 = HDTankDomeAt(105.0f / 30.0f);

	ok(d0.alive && d0.scale == 1.0f && d0.share == 0.0f && d0.reach == 0.0f,
		"whole height, nothing drawn yet and still in the glass at 0 s",
		(long)(d0.scale * 100.0f), 100);
	ok(d1.scale == (float)HDTankFlagsAfter(30, 0) / 4096.0f &&
		d1.share == 1.0f && d1.reach == 1.0f,
		"at 1 s the tank's 30th tick, whole and out", (long)(d1.share * 10.0f),
		10);
	ok(d105.alive && d105.scale == 810.0f / 4096.0f && d105.share < 0.1f,
		"at the 105th tick 0.198 high and nearly faded", (long)(d105.share *
		100.0f), 6);
	ok(!HDTankDomeAt(106.0f / 30.0f).alive && !HDTankDomeAt(-0.1f).alive,
		"gone with KillItem's tick and before the break", 0, 0);

	bool smooth = true;
	float last = 1.0f;

	for (float t = 0.0f; t < 3.5f; t += 1.0f / 60.0f)
	{
		const float s = HDTankDomeAt(t).scale;

		smooth = smooth && s <= last && last - s < 0.02f;
		last = s;
	}

	ok(smooth, "it sinks without a step at sixty frames", smooth, 1);

	const HDTankShape sh = HDTankShapeOf(kTankMeshSample, 9);
	HDTankDome half = d1;
	float p[4], top[4], foot[4], back[4];

	half.reach = 0.5f;
	HDTankDomePoint(sh, half, &kTankMeshSample[8 * 3], p);
	HDTankDomePoint(sh, d1, &kTankMeshSample[3 * 3], top);
	HDTankDomePoint(sh, d1, &kTankMeshSample[8 * 3], foot);
	HDTankDomePoint(sh, d1, &kTankMeshSample[2 * 3], back);
	ok(p[2] == sh.glassZ + 0.5f * (350.0f - sh.glassZ) && p[1] ==
		6.0f * d1.scale, "the apron half out at reach 0.5, y scaled",
		(long)p[2], -83);
	ok(fabsf(top[3]) < 1.0f && foot[3] > 0.0f && back[3] < 0.0f,
		"the way down: 0 at the glass's top, more at the foot, less behind",
		(long)foot[3], 1);
	ok(HDTankDomeShare(d1, 1.0f) == kTankDomeBody &&
		HDTankDomeShare(d1, 0.0f) == kTankDomeBody + kTankDomeRim &&
		HDTankDomeShare(d1, -1.0f) == kTankDomeBody,
		"seen through it the body's share, along it the rim's too", 1, 1);
}

/* The wet floor (hd_splash_math.h, HDWetFront and HDWetAt): the water's
 * front out of the glass under the dome and past it, and the wet behind. */
static void splash_tank_wet()
{
	printf("the splash: the tank's wet floor\n");

	const float reach = 866.0f;

	ok(HDWetFront(0.0f, reach) < -1.0e8f && HDWetFront(-1.0f, reach) <
		-1.0e8f, "no water on the floor before the tank breaks", 0, 0);
	ok(HDWetFront(kTankDomeOut * 0.5f, reach) == reach * 0.5f &&
		HDWetFront(kTankDomeOut, reach) == reach,
		"under the dome as its apron comes out: half at half its time",
		(long)HDWetFront(kTankDomeOut, reach), 866);
	ok(fabsf(HDWetFront(kTankDomeOut + 1.0f, reach) - (reach + 321.0f)) <
		0.01f, "past the foot 321 units a second",
		(long)HDWetFront(kTankDomeOut + 1.0f, reach), 1187);
	ok(fabsf(sqrtf(kTankG * 24.0f) - kWetSpread) < 1.0f,
		"321 is sqrt(g h) at the film's 24 units", (long)sqrtf(kTankG * 24.0f),
		321);

	bool rising = true;
	float last = HDWetFront(1.0f / 60.0f, reach);

	for (float t = 2.0f / 60.0f; t < 10.0f; t += 1.0f / 60.0f)
	{
		const float f = HDWetFront(t, reach);

		rising = rising && f >= last && f - last < 3460.0f / 60.0f + 1.0f;
		last = f;
	}

	ok(rising, "the front only runs on, no faster than the apron", rising, 1);
	ok(HDWetAt(1000.0f, 1001.0f) == 0.0f &&
		HDWetAt(1000.0f, 1000.0f - kWetRamp) == 1.0f &&
		HDWetAt(1000.0f, 1000.0f - kWetRamp * 0.5f) == 0.5f,
		"dry ahead of the front, wet a ramp behind it, half between",
		(long)(HDWetAt(1000.0f, 872.0f) * 100.0f), 50);

	/* RICH2's furthest point of tank 182 without the rooms and the radius
	 * (RoomProbe -tankwetall, SPLASHLOG wet far=19749). */
	const float far19749 = (19749.0f - reach) / kWetSpread + kTankDomeOut +
		kWetRamp / kWetSpread;

	ok(HDWetAt(HDWetFront(far19749, reach), 19749.0f) == 1.0f &&
		HDWetAt(HDWetFront(far19749 - 1.0f, reach), 19749.0f) < 1.0f,
		"without the radius the corridor's far end in about a minute",
		(long)far19749, 60);

	/* The radius: half the dome's foot, 3186 units wide, up to the run's
	 * whole squares; the front held there from about 6.6 s on, and a
	 * loaded game's long-done front held at the same place. */
	const float limit = reach + kWetRadius;
	const float held = (kWetRadius) / kWetSpread + kTankDomeOut;
	const float late = HDWetFront(1.0e6f, reach);

	ok(kWetRadius == 1024.0f * ceilf(3186.0f * 0.5f / 1024.0f),
		"the radius is half the foot's width in whole squares",
		(long)kWetRadius, 2048);
	ok(fabsf(HDWetFront(held, reach) - limit) < 0.5f,
		"the front reaches the radius at about 6.6 s",
		(long)(held * 10.0f), 66);
	ok((late < limit ? late : limit) == limit &&
		HDWetAt(limit, limit - kWetRamp) == 1.0f &&
		HDWetAt(limit, limit) == 0.0f,
		"a loaded game wet to the radius, dry past it", (long)limit, 2914);
}

/* The models' mirror's clip planes (hd_splash_math.h, HDWetPlaneTo): a
 * plane carried through a matrix keeps every point's distance - here the
 * box side x >= 41000 through a world-to-view with a turn and a shift and
 * on through a projection with the floor's reflection and x turned, as
 * HDRenderWaterWetModels makes them, at a point inside and one outside. */
static void splash_wet_clip()
{
	printf("the splash: the models' mirror's clip planes\n");

	const float view[16] = { 0.6f, 0.0f, -0.8f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
		0.8f, 0.0f, 0.6f, 0.0f, -2000.0f, 900.0f, 5000.0f, 1.0f };
	const float mirrored[16] = { -1.2f, 0.0f, 0.0f, 0.0f, 0.0f, -1.7f, 0.0f,
		0.0f, 0.0f, 0.4f, 1.0f, 1.0f, 0.0f, 1800.0f, -16.0f, 0.0f };
	const float side[4] = { 1.0f, 0.0f, 0.0f, -41000.0f };
	const float in[4] = { 41300.0f, -600.0f, 67000.0f, 1.0f };
	const float out[4] = { 40500.0f, -600.0f, 67000.0f, 1.0f };
	float v[4], c[4], kept[2];
	bool made = HDWetPlaneTo(view, side, v) && HDWetPlaneTo(mirrored, v, c);

	for (int n = 0; n < 2; n++)
	{
		const float* p = n ? out : in;
		float q[4], r[4];

		for (int k = 0; k < 4; k++)
			q[k] = p[0] * view[k] + p[1] * view[4 + k] + p[2] * view[8 + k] +
				p[3] * view[12 + k];

		for (int k = 0; k < 4; k++)
			r[k] = q[0] * mirrored[k] + q[1] * mirrored[4 + k] +
				q[2] * mirrored[8 + k] + q[3] * mirrored[12 + k];

		kept[n] = c[0] * r[0] + c[1] * r[1] + c[2] * r[2] + c[3] * r[3];
	}

	ok(made && fabsf(kept[0] - 300.0f) < 0.5f && fabsf(kept[1] + 500.0f) < 0.5f,
		"a side 300 inside and 500 outside, in clip space", (long)kept[0],
		300);
}

/* The wet floor over sloping squares (hd_splash_math.h, HDWetSlope,
 * HDWetHeight, HDWetPasses): RICH2's split by the raised block in room 45
 * (corners -768, 0, 0, 0, function 7; RoomProbe -tankslopes) and a tilt
 * of room 71 (0, 256, 0, 256). */
static void splash_tank_slopes()
{
	printf("the splash: the wet floor's slopes\n");

	const int split[4] = { -768, 0, 0, 0 };
	const int tilt[4] = { 0, 256, 0, 256 };

	ok(HDWetSlope(2) && HDWetSlope(7) && HDWetSlope(8) && HDWetSlope(11) &&
		HDWetSlope(14) && !HDWetSlope(0) && !HDWetSlope(3) &&
		!HDWetSlope(9), "the floor entries the water runs over", 1, 1);
	ok(HDWetHeight(7, split, 1024.0f, 1024.0f) == 0.0f &&
		HDWetHeight(7, split, 768.0f, 512.0f) == 0.0f &&
		HDWetHeight(7, split, 0.0f, 0.0f) == -768.0f &&
		HDWetHeight(7, split, 256.0f, 256.0f) == -384.0f,
		"a split: its flat half level, its ramp up to the raised corner",
		(long)HDWetHeight(7, split, 256.0f, 256.0f), -384);
	ok(HDWetHeight(2, tilt, 512.0f, 0.0f) == 128.0f &&
		HDWetHeight(2, tilt, 512.0f, 1024.0f) == 128.0f,
		"a tilt: one plane", (long)HDWetHeight(2, tilt, 512.0f, 0.0f), 128);
	ok(HDWetPasses(0, 0, 0, 0, 256) && HDWetPasses(0, 0, 256, 256, 256) &&
		!HDWetPasses(0, 0, -256, -256, 256) &&
		!HDWetPasses(0, 0, 512, 512, 256),
		"level or a step down, never up, never a drop", 1, 1);
	ok(HDWetPasses(0, 0, 0, 0, 256) && !HDWetPasses(-768, 0, -768, -768,
		256) && -768 < 0 - kWetSlopeRise,
		"into the split's flat edge; not from its ramp onto the block", 1, 1);

	/* The cells over a square, as build_wet_floors lays them: a point dry
	 * where it stands over the flat edge by more than kWetSlopeRise. The
	 * split's flat half is half the square, 16 of its 32 triangles, for
	 * either crease (room 121's function 8 split: 0, 0, -768, 0); a flat
	 * square's cells are halved from a to e, a, b, e first. */
	const int other[4] = { 0, 0, -768, 0 };
	const int flat[4] = { 0, 0, 0, 0 };
	const int* const squares[3] = { split, other, flat };
	const int fns[3] = { 7, 8, 0 };
	int counts[3] = { 0, 0, 0 }, first[6] = { 0 };

	for (int s = 0; s < 3; s++)
	{
		int number[25];

		for (int k = 0; k < 25; k++)
			number[k] = HDWetHeight(fns[s], squares[s], (float)(k % 5 * 256),
				(float)(k / 5 * 256)) < (float)-kWetSlopeRise ? -1 : k;

		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++)
			{
				int tri[6];
				const int n = HDWetCell(fns[s], number[j * 5 + i],
					number[j * 5 + i + 1], number[j * 5 + i + 5],
					number[j * 5 + i + 6], tri);

				for (int k = 0; s == 2 && !i && !j && k < n; k++)
					first[k] = tri[k];

				counts[s] += n / 3;
			}
	}

	ok(counts[0] == 16, "a split's flat half covered whole (crease 1 to 2)",
		counts[0], 16);
	ok(counts[1] == 16, "a split's flat half covered whole (crease 0 to 3)",
		counts[1], 16);
	ok(counts[2] == 32 && first[0] == 0 && first[1] == 1 && first[2] == 6 &&
		first[3] == 0 && first[4] == 6 && first[5] == 5,
		"a flat square: 32 triangles, halved from a to e", counts[2], 32);
}

/* The rings' strength on the sheets (hd_splash_math.h, HDRingStrength, at
 * hd_render.cpp's kRingSlope 0.5 and kRingBody 4): a wading Lara's ring as
 * it always was, a barrel's past the old cap of 128 and four times more. */
static void splash_rings()
{
	printf("the splash: the rings' strength\n");

	bool lara = true;

	for (int size = 112; size <= 128; size++)
		for (float left = 0.0f; left <= 1.0f; left += 0.125f)
		{
			const float first = (float)size / 128.0f;
			const float old = 0.5f * (first < 1.0f ? first : 1.0f) *
				left * left;

			lara = lara && HDRingStrength(0.5f, 4.0f, (float)size, left) ==
				old;
		}

	ok(lara, "a wading Lara's ring (112 to 127) and 128 as before", lara, 1);
	ok(HDRingStrength(0.5f, 4.0f, 240.0f, 1.0f) == 0.5f * 1.875f * 4.0f &&
		HDRingStrength(0.5f, 4.0f, 255.0f, 1.0f) ==
		0.5f * (255.0f / 128.0f) * 4.0f,
		"a barrel's ring of 240 to 255: its size past 128, four times",
		(long)(HDRingStrength(0.5f, 4.0f, 240.0f, 1.0f) * 100.0f), 375);
	ok(HDRingStrength(0.5f, 4.0f, 240.0f, 0.5f) ==
		HDRingStrength(0.5f, 4.0f, 240.0f, 1.0f) * 0.25f &&
		HDRingStrength(0.5f, 4.0f, 240.0f, 0.0f) == 0.0f,
		"and it fades with the square of its life left as every ring", 1, 1);
	ok(HDRingStrength(0.5f, 4.0f, 80.0f, 1.0f) == 0.5f * 80.0f / 128.0f,
		"a small ring (a source of 80) keeps its share", 1, 1);
}

/* The springs (hd_spring.h): the table, the file's `spring` key, the
 * hashed clock of the rings and the bubbles - pure functions of the
 * spring's numbers and the clock, so the same moment gives the same
 * picture every time. */
static HDSpring spring_of(float strength)
{
	HDSpring s = {};

	strcpy(s.stem, "ANDREA2");
	s.room = 194;
	s.x = 55808.0f;
	s.z = 32768.0f;
	s.floorY = 2560;
	s.surfaceY = 256;
	s.strength = strength;
	s.period = 0.7f;
	s.seed = HDSpringSeed(s.stem, s.x, s.z);
	return s;
}

static void spring_table()
{
	printf("the springs: the table\n");

	const HDSpringRow& r = kHDSprings[0];

	ok(!strcmp(r.stem, "ANDREA2") && r.room == 194 && r.x == 55808.0f &&
		r.z == 32768.0f && r.floorY == 2560 && r.strength == 1.0f &&
		r.period == 0.7f,
		"spring table: ANDREA2 194's fountain, the middle of its six "
		"squares, the floor 2304 under the sheet at 256, 0.7 s", r.room, 194);
	ok(HDSpringRingSize(1.0f) == 80.0f && HDSpringRingSize(0.5f) == 40.0f &&
		HDSpringRingSize(0.1f) == kHDSpringRingLeast,
		"spring table: the ring 80 at strength 1, in step below, 16 at the "
		"least", (long)HDSpringRingSize(0.5f), 40);
	ok(HDSpringStemHash("andrea2") == HDSpringStemHash("ANDREA2") &&
		HDSpringStemHash("ANDREA2") != HDSpringStemHash("ANDREA1"),
		"spring table: the level's number is blind to case", 1, 1);
}

static void spring_file()
{
	printf("the springs: the file's key\n");

	char line[] = "ANDREA2 194 - spring 55500 32500 0.5 1.25";
	char* tok[8];
	int n = 0;

	for (char* t = strtok(line, " "); t && n < 8; t = strtok(0, " "))
		tok[n++] = t;

	float key[4];
	const int took = HDSpringParseKey(tok, n, 3, key);

	ok(took == 5 && key[0] == 55500.0f && key[1] == 32500.0f &&
		key[2] == 0.5f && key[3] == 1.25f,
		"spring file: x, z, strength and seconds - five tokens", took, 5);

	char line2[] = "ANDREA2 194 - spring 55500 32500 0.5 foam 2";
	n = 0;

	for (char* t = strtok(line2, " "); t && n < 8; t = strtok(0, " "))
		tok[n++] = t;

	ok(HDSpringParseKey(tok, n, 3, key) == 4 && key[3] == 0.0f,
		"spring file: without the seconds the next key stays a key", 1, 1);

	char line3[] = "ANDREA2 194 - spring 55500 32500 1.5";
	n = 0;

	for (char* t = strtok(line3, " "); t && n < 8; t = strtok(0, " "))
		tok[n++] = t;

	ok(HDSpringParseKey(tok, n, 3, key) == -1,
		"spring file: a strength over 1 is a line that cannot be read", 1, 1);

	std::vector<HDSpring> springs(1, spring_of(1.0f));
	springs[0].how = HD_SPRING_TABLE;

	const float change[4] = { 55500.0f, 32500.0f, 0.5f, 0.0f };

	HDSpringFileApply(springs, "ANDREA2", 194, change);
	ok(springs.size() == 1 && springs[0].x == 55500.0f &&
		springs[0].strength == 0.5f && springs[0].period == 0.7f &&
		springs[0].how == HD_SPRING_FILE,
		"spring file: a row of the table's room changes the entry and keeps "
		"its period", (long)springs.size(), 1);

	const float more[4] = { 50000.0f, 40000.0f, 0.8f, 0.0f };

	HDSpringFileApply(springs, "ANDREA2", 144, more);
	ok(springs.size() == 2 && springs[1].room == 144 &&
		fabsf(springs[1].period - 0.7f / 0.8f) < 1.0e-5f,
		"spring file: a row of another room adds a spring, 0.7 s over its "
		"strength", (long)springs.size(), 2);

	const float off[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	HDSpringFileApply(springs, "ANDREA2", 194, off);
	ok(springs.size() == 1 && springs[0].room == 144,
		"spring file: strength 0 takes the room's spring away",
		(long)springs.size(), 1);
}


/* The springs' rule: candidates on a body of squares, w by d, from the
 * world's origin, every square `depth` deep under a sheet at y 0. */
static HDSpringBody spring_body(int w, int d, int depth)
{
	HDSpringBody body;

	body.index = 0;
	body.surfaceY = 0;
	body.river = 0;
	body.taken = 0;

	for (int x = 0; x < w; x++)
		for (int z = 0; z < d; z++)
		{
			const HDSpringSquare q = { x * 1024, z * 1024, depth, 1 };

			body.squares.push_back(q);
		}

	return body;
}

static void spring_rule()
{
	printf("the springs: the placement rule\n");

	HDSpringBody body = spring_body(8, 8, 1024);
	std::vector<HDSpringBlock> blocks;

	ok(HDSpringCandidates(body, blocks, 0) == 64,
		"spring rule: every deep square of a pool is a candidate",
		HDSpringCandidates(body, blocks, 0), 64);
	ok(HDSpringCandidates(spring_body(8, 8, 512), blocks, 0) == 0,
		"spring rule: none under 768 units of water", 0, 0);

	// An opening in the west wall, x 0, z 3072..4096, down to the floor.
	const HDSpringBlock west = { HD_SPRING_PORTAL, 0.0f, 3072.0f, 0.0f,
		4096.0f, -512.0f, 1024.0f };

	blocks.push_back(west);

	const int open = HDSpringCandidates(body, blocks, 0);

	/* 19 squares lie within 3072 of it, centre to the portal's edge:
	 * 7, 7 and 5 in the three columns next to it. */
	ok(open == 45,
		"spring rule: three squares clear of an opening of the water",
		open, 45);

	// The same portal over the water's top reaches nothing.
	blocks[0].y0 = -2048.0f;
	blocks[0].y1 = 0.0f;
	ok(HDSpringCandidates(body, blocks, 0) == 64,
		"spring rule: a portal over the water is no opening of it", 1, 1);

	// A portal between two rooms of one water, x 4096 across the pool.
	const HDSpringBlock inside = { HD_SPRING_PORTAL, 4096.0f, 0.0f, 4096.0f,
		8192.0f, -512.0f, 1024.0f };

	blocks.assign(1, inside);
	ok(!HDSpringOpening(body, inside) &&
		HDSpringCandidates(body, blocks, 0) == 64,
		"spring rule: a portal inside one water parts no candidate", 1, 1);

	const HDSpringBlock start = { HD_SPRING_START, 512.0f, 512.0f, 512.0f,
		512.0f, -1.0e9f, 1.0e9f };
	const HDSpringBlock statue = { HD_SPRING_STATIC, 7000.0f, 7000.0f,
		7000.0f, 7000.0f, -1.0e9f, 1.0e9f };

	blocks.assign(1, start);
	blocks.push_back(statue);
	ok(HDSpringCandidates(body, blocks, 0) == 64 - 9 - 1,
		"spring rule: three squares clear of Lara's start, none under a "
		"static", HDSpringCandidates(body, blocks, 0), 64 - 9 - 1);

	int pick = -1;
	float strength = 0.0f;

	ok(!HDSpringRulePick("ANDREA1", 3866745u, 5, 0, 0, 1, &pick, &strength) &&
		!HDSpringRulePick("ANDREA1", 3866745u, 6, 1, 0, 1, &pick, &strength) &&
		!HDSpringRulePick("ANDREA1", 3866745u, 6, 0, 1, 1, &pick, &strength) &&
		!HDSpringRulePick("ANDREA1", 3866745u, 6, 0, 0, 0, &pick, &strength),
		"spring rule: no spring with five candidates, in a river, beside a "
		"table's spring or with the key at 0", 1, 1);
	ok(HDSpringBodyKey(spring_body(3, 2, 1024)) == 0u &&
		HDSpringBodyKey(body) == HDSpringBodyKey(spring_body(8, 8, 2048)),
		"spring rule: a body's number is its first square's", 1, 1);
}

/* The rule's guard on the fifteen levels: the bodies with six candidates or
 * more (RoomProbe -springs on the classic levels, SPRINGRULE first= and
 * candidates=), each with what the hash gives it at the key's 1 and 2.
 * Only these five bodies of the fifteen levels hold six; the rest of the
 * levels place no spring of the rule. */
static void spring_levels()
{
	printf("the springs: the rule on the fifteen levels\n");

	struct Fit
	{
		const char* stem;
		unsigned first;
		int candidates;
		int pick1;		// at key 1, -1 for none
		int pick2;		// at key 2
	};

	static const Fit fit[] =
	{
		{ "ANDREA1", 3866745u, 6, 4, -1 },
		{ "ANDREA2", 3473499u, 22, 3, 3 },
		{ "ANDY1", 5111944u, 9, -1, -1 },
		{ "JOBY2", 720953u, 13, -1, -1 },
		{ "JOBY2", 720934u, 14, 13, 13 },
	};
	int wrong = 0, placed = 0, placed2 = 0;

	for (const Fit& f : fit)
		for (int thin = 1; thin <= 2; thin++)
		{
			int pick = -1;
			float strength = 0.0f;
			const bool got = HDSpringRulePick(f.stem, f.first, f.candidates,
				0, 0, thin, &pick, &strength);
			const int want = thin == 1 ? f.pick1 : f.pick2;

			wrong += (got ? pick : -1) != want ||
				(got && (strength < 0.5f || strength > 1.0f)) ? 1 : 0;
			placed += got && thin == 1 ? 1 : 0;
			placed2 += got && thin == 2 ? 1 : 0;
		}

	ok(wrong == 0 && placed == 3 && placed2 == 2,
		"spring levels: three springs of the rule in three levels at the "
		"key's 1 (ANDREA1, ANDREA2, JOBY2), two at 2", placed, 3);
}

/* The crack at a spring's foot (hd_spring.h, HDSpringCrackMake): a hash of
 * the spring's level and place, so the same spring has the same outline in
 * every build; two to four branches a third of the main line long; one
 * simple polygon whatever the hash; on its body's deep squares; and its
 * bubbles born inside it, on its bottom. */
static bool crack_edges_cross(const HDSpringCrackShape& c)
{
	const int n = c.points;

	for (int i = 0; i < n; i++)
		for (int j = i + 2; j < n; j++)
		{
			if (i == 0 && j == n - 1)
				continue;

			const double ax = c.x[i], az = c.z[i];
			const double bx = c.x[(i + 1) % n], bz = c.z[(i + 1) % n];
			const double cx = c.x[j], cz = c.z[j];
			const double dx = c.x[(j + 1) % n], dz = c.z[(j + 1) % n];
			const double d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
			const double d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
			const double d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
			const double d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);

			if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)))
				return true;
		}

	return false;
}

// The outline as a number: FNV-1a over its points in sixteenths of a unit.
static unsigned crack_print(const HDSpringCrackShape& c)
{
	unsigned h = 2166136261u;

	for (int i = 0; i < c.points; i++)
	{
		const long v[2] = { lround(c.x[i] * 16.0f), lround(c.z[i] * 16.0f) };

		for (int k = 0; k < 2; k++)
			for (int s = 0; s < 32; s += 8)
				h = (h ^ (unsigned)((v[k] >> s) & 0xFF)) * 16777619u;
	}

	return h;
}

static void spring_crack()
{
	printf("the springs: the crack\n");

	// The fountain's crack, made twice, and pinned to its outline.
	const HDSpring s = spring_of(1.0f);
	HDSpringCrackShape a, b;

	HDSpringCrackMake(s, 1.0f, &a);
	HDSpringCrackMake(s, 1.0f, &b);
	ok(memcmp(&a, &b, sizeof(a)) == 0, "spring crack: the same spring makes "
		"the same crack", 1, 1);
	ok(crack_print(a) == 0xDA93C1A4u && a.points == 60 && a.branches == 4 &&
		a.round == 0, "spring crack: the fountain's outline as every build "
		"makes it (60 points, 4 branches, the first round)",
		(long)crack_print(a), (long)0xDA93C1A4u);

	// Any spring: the counts and sizes in range, one simple polygon.
	int counts = 0, lengths = 0, sizes = 0, simple = 0, total = 0, later = 0;

	for (int k = 0; k < 4000; k++)
	{
		HDSpring r = s;

		r.x = 1024.0f + (float)(HDSpringHash(77u, (unsigned)k) * 3);
		r.z = 1024.0f + (float)(HDSpringHash(78u, (unsigned)k) * 3);
		r.strength = (float)(k % 11) / 10.0f;
		r.seed = HDSpringSeed(k & 1 ? "JOBY2" : "ANDREA1", r.x, r.z);

		HDSpringCrackShape c;

		HDSpringCrackMake(r, 1.0f, &c);
		total++;
		counts += c.branches >= 2 && c.branches <= 4 &&
			c.points == 2 * kHDSpringCrackSteps + 2 *
			kHDSpringCrackBranchSteps * c.branches ? 1 : 0;

		bool within = true;

		for (int j = 0; j < c.branches; j++)
			within = within && c.branchLength[j] >= c.length / 3.0f * 0.8f -
				0.01f && c.branchLength[j] <= c.length / 3.0f * 1.2f + 0.01f;

		lengths += within ? 1 : 0;

		const float scale = 0.5f + 0.5f * r.strength;

		sizes += c.length >= 256.0f * scale - 0.01f && c.length <= 512.0f *
			scale + 0.01f && c.width >= 32.0f * scale - 0.01f &&
			c.width <= 96.0f * scale + 0.01f && c.depth >= 64.0f * scale -
			0.01f && c.depth <= 128.0f * scale + 0.01f ? 1 : 0;
		simple += crack_edges_cross(c) ? 0 : 1;
		later += c.round > 0 ? 1 : 0;
	}

	ok(counts == total, "spring crack: two to four branches, each in place "
		"of one point as a spike of 2 x 4 + 1", counts, total);
	ok(lengths == total, "spring crack: every branch a third of the main "
		"line long, 0.8 to 1.2 of it", lengths, total);
	ok(sizes == total, "spring crack: 256 to 512 long, 32 to 96 wide, 64 to "
		"128 deep, scaled by the strength from half to whole", sizes, total);
	ok(simple == total, "spring crack: the outline never crosses itself",
		simple, total);
	ok(later == 660, "spring crack: 660 of the 4,000 draw their branches "
		"again in a later round of the hash", later, 660);

	/* The four springs of the classic levels (the table's and the rule's,
	 * RoomProbe -springs): each crack at its whole size, every point of it
	 * on the deep squares its map meets (RoomProbe -springcrackmap). */
	struct Place
	{
		const char* stem;
		float x, z;
		unsigned first;		// the rule's body key, 0 for the table's
		int candidates;
		int squares;
		int sq[2][3];
	};

	static const Place place[] =
	{
		{ "ANDREA2", 55808.0f, 32768.0f, 0u, 0, 2,
			{ { 55296, 31744, 2304 }, { 55296, 32768, 2304 } } },
		{ "ANDREA2", 60928.0f, 40448.0f, 3473499u, 22, 1,
			{ { 60416, 39936, 768 } } },
		{ "ANDREA1", 62976.0f, 64000.0f, 3866745u, 6, 1,
			{ { 62464, 63488, 1792 } } },
		{ "JOBY2", 12800.0f, 37376.0f, 720934u, 14, 1,
			{ { 12288, 36864, 768 } } },
	};
	int laid = 0, on = 0, points = 0;

	for (const Place& p : place)
	{
		HDSpring r = s;
		int pick = -1;

		strcpy(r.stem, p.stem);
		r.x = p.x;
		r.z = p.z;
		r.strength = 1.0f;

		if (p.first)
			HDSpringRulePick(p.stem, p.first, p.candidates, 0, 0, 1, &pick,
				&r.strength);

		r.seed = HDSpringSeed(r.stem, r.x, r.z);

		std::vector<HDSpringSquare> body;

		for (int q = 0; q < p.squares; q++)
		{
			const HDSpringSquare sq = { p.sq[q][0], p.sq[q][1], p.sq[q][2],
				1 };

			body.push_back(sq);
		}

		HDSpringCrackShape c;

		laid += HDSpringCrackChoose(r, body, &c) && c.fit == 1.0f ? 1 : 0;

		for (int i = 0; i < c.points; i++, points++)
			for (const HDSpringSquare& q : body)
				if (c.x[i] >= (float)q.x && c.x[i] < (float)(q.x + 1024) &&
					c.z[i] >= (float)q.z && c.z[i] < (float)(q.z + 1024) &&
					q.depth >= kHDSpringDeep)
				{
					on++;
					break;
				}
	}

	ok(laid == 4, "spring crack: the four springs' cracks lie whole on their "
		"water", laid, 4);
	ok(on == points, "spring crack: every outline point on a deep square of "
		"its body", on, points);

	// The bubbles: born inside the slit, on its bottom, rising out of it.
	a.bottomY = 2641.0f;

	int inside = 0, born = 0, out = 0;

	for (int k = 0; k < 3000; k++)
	{
		float x, z;
		HDSpringBubbleAt at;

		HDSpringCrackBirth(a, s.seed, k, &x, &z);
		inside += HDSpringCrackHolds(a, x, z) ? 1 : 0;
		born += HDSpringBubble(s, k, HDSpringBubbleBorn(s, k) + 1.0e-4, &at,
			&a) && fabsf(at.x - x) < 1.0f && fabsf(at.z - z) < 1.0f &&
			fabsf(at.y - a.bottomY) < 1.0f ? 1 : 0;
		out += HDSpringBubble(s, k, HDSpringBubbleBorn(s, k) + 3.0, &at, &a) &&
			at.y < (float)s.floorY ? 1 : 0;
	}

	ok(inside == 3000, "spring crack: every bubble is born inside the "
		"outline", inside, 3000);
	ok(born == 3000, "spring crack: a bubble starts at its birth place on "
		"the crack's bottom", born, 3000);
	ok(out == 3000, "spring crack: three seconds on every bubble has risen "
		"out of the crack", out, 3000);
}

/* The crack's gate (hd_spring.h, HDSpringCrackSeen) on the eyes of the
 * fan round ANDREA2's rule crack: its outline as RoomProbe -springs lists
 * it, on a slope from 1,229.6 to 1,379.5 with its bottom at 1,446, and
 * the eyes in steps of 64 from 256 over its highest point to 128 under its
 * bottom at four places round it. RoomProbe's pictures there: the crack
 * right at every eye over the bottom that is over the floor, the walls
 * alone in the slit from an eye under the bottom that is over the floor
 * (the hole without its bottom), and nothing but black from an eye inside
 * the rock uphill under the floor. */
static void spring_crack_gate()
{
	printf("the springs: the crack's gate\n");

	static const float rim[44][3] =
	{
		{ 60907.6f, 40227.9f, 1229.6f }, { 60886.6f, 40265.5f, 1244.4f },
		{ 60890.5f, 40299.7f, 1252.1f }, { 60883.4f, 40324.3f, 1260.1f },
		{ 60898.0f, 40343.0f, 1261.1f }, { 60864.5f, 40355.8f, 1272.8f },
		{ 60839.9f, 40384.2f, 1292.1f }, { 60810.6f, 40404.3f, 1316.8f },
		{ 60779.7f, 40421.7f, 1340.8f }, { 60813.1f, 40408.8f, 1317.7f },
		{ 60844.9f, 40393.1f, 1294.1f }, { 60872.1f, 40369.2f, 1274.2f },
		{ 60908.0f, 40360.9f, 1263.1f }, { 60919.1f, 40386.8f, 1266.8f },
		{ 60910.2f, 40408.9f, 1274.6f }, { 60896.2f, 40456.7f, 1300.2f },
		{ 60904.5f, 40490.5f, 1312.9f }, { 60912.0f, 40518.2f, 1323.0f },
		{ 60914.4f, 40548.9f, 1337.1f }, { 60922.5f, 40570.0f, 1343.6f },
		{ 60915.5f, 40600.4f, 1362.2f }, { 60944.7f, 40637.6f, 1366.2f },
		{ 60948.4f, 40668.1f, 1379.5f }, { 60974.0f, 40635.1f, 1350.3f },
		{ 60957.8f, 40599.1f, 1340.5f }, { 60984.6f, 40575.4f, 1315.3f },
		{ 60972.3f, 40539.0f, 1303.3f }, { 60971.0f, 40510.2f, 1289.6f },
		{ 61002.2f, 40523.1f, 1280.4f }, { 61028.0f, 40544.5f, 1279.1f },
		{ 61059.5f, 40556.9f, 1274.3f }, { 61088.3f, 40573.6f, 1271.2f },
		{ 61062.7f, 40552.1f, 1272.3f }, { 61034.3f, 40534.8f, 1275.1f },
		{ 61011.7f, 40508.7f, 1274.2f }, { 60983.6f, 40491.0f, 1276.8f },
		{ 60958.9f, 40483.8f, 1282.5f }, { 60981.5f, 40450.0f, 1267.0f },
		{ 60986.1f, 40402.6f, 1253.9f }, { 60984.6f, 40370.7f, 1246.2f },
		{ 60964.5f, 40355.7f, 1247.5f }, { 60937.0f, 40319.7f, 1245.4f },
		{ 60935.1f, 40294.0f, 1239.4f }, { 60915.6f, 40256.2f, 1234.7f },
	};
	float x[45], z[45], y[45];

	for (int i = 0; i < 44; i++)
	{
		x[i] = rim[i][0];
		z[i] = rim[i][1];
		y[i] = rim[i][2];
	}

	// The middle: the spring's point, the main line's middle's floor.
	x[44] = 60928.0f;
	z[44] = 40448.0f;
	y[44] = 1277.0f;

	const HDSpringCrackFloor f = HDSpringCrackFloorFit(x, z, y, 45);

	ok(fabsf(f.slopeX + 0.397f) < 0.002f && fabsf(f.slopeZ - 0.373f) < 0.002f &&
		f.top == 1229.6f, "spring crack gate: the slope's plane rises 0.397 a "
		"unit along x, falls 0.373 along z, its highest point 1,229.6",
		lround(f.slopeX * 1000.0f), -397);

	/* The fan: four columns - the second camera's own, west and south of
	 * the crack 1,000 away in the water (floors 1,912, 1,537, 1,657), and
	 * 600 uphill, where the floor stands at 1,068. Every water eye sees
	 * it, the two under the bottom less the lift (1,438) without the
	 * bottom; uphill the eyes over the whole outline see it, the lower ones
	 * stand in the rock and do not. */
	struct Eye
	{
		float x, z;
		int seen;		// the first of the ten steps not seen
	};

	static const Eye fan[] =
	{
		{ 59858.0f, 41443.0f, 10 },
		{ 59930.0f, 40450.0f, 10 },
		{ 60930.0f, 41450.0f, 10 },
		{ 61354.3f, 40025.7f, 4 },
	};
	const float lift = 8.0f, bottom = 1446.0f;
	int right = 0, total = 0, floors = 0;

	for (const Eye& e : fan)
		for (int k = 0; k < 10; k++, total++)
		{
			const float eye[3] = { e.x, 973.6f + 64.0f * (float)k, e.z };

			right += HDSpringCrackSeen(eye, f, lift) == (k < e.seen) ? 1 : 0;
			floors += HDSpringCrackBottom(eye[1], bottom, lift) == (k < 8) ?
				1 : 0;
		}

	ok(right == total, "spring crack gate: the fan's forty eyes as the "
		"pictures read - over the floor seen, in the rock not", right, total);
	ok(floors == total, "spring crack gate: the hole draws its bottom for "
		"the eight rows over it, none for the two under it", floors, total);

	/* Two cameras out of a game's camera log at this crack: one over it,
	 * one 141 under its highest point and 75 over its bottom. */
	const float over[3] = { 59528.0f, 562.0f, 41503.0f };
	const float under[3] = { 59858.0f, 1371.0f, 41443.0f };

	ok(HDSpringCrackSeen(over, f, lift) && HDSpringCrackSeen(under, f, lift),
		"spring crack gate: both cameras of the log see the crack, the second "
		"under its highest point", 1, 1);

	/* Three more cameras of the logs, under the crack's bottom on the
	 * falling slope (y 1,558, 1,586 and 1,660): the slit seen, its walls
	 * alone. */
	const float low[3][3] = { { 59461.0f, 1558.0f, 41048.0f },
		{ 60046.0f, 1586.0f, 41665.0f }, { 60252.0f, 1660.0f, 41452.0f } };
	int lows = 0;

	for (int i = 0; i < 3; i++)
		lows += HDSpringCrackSeen(low[i], f, lift) &&
			!HDSpringCrackBottom(low[i][1], bottom, lift) ? 1 : 0;

	ok(lows == 3, "spring crack gate: three cameras under the bottom see the "
		"slit without its bottom", lows, 3);

	/* Under the bottom the level's own floor at the eye's column decides
	 * (HDSpringCrackGround), the heights the .TRG gives there: 1,000 west
	 * of the crack the floors at 1,536.8 and 1,591.3 - the eye at 1,485.6
	 * stands over them and sees, the eye at 1,549.6 stands 13 under the
	 * nearer, in the rock (the plane fitted to the outline puts it 137
	 * over), and does not; the second camera's column (1,912.4, 2,041.1),
	 * the south's (1,657.3) and the three cameras of the logs (1,813.9,
	 * 1,967.8, 1,914.6 nearest) see; no floor, too far to judge, not. */
	const float west[2] = { 1536.8f, 1591.3f };
	const float his[2] = { 1912.4f, 2041.1f };
	const float south = 1657.3f;
	const float logs[3][2] = { { 1813.9f, 1893.7f }, { 1967.8f, 2053.1f },
		{ 1914.6f, 2020.3f } };
	int grounds = 0;

	grounds += HDSpringCrackGround(1485.6f, west, 2, lift) ? 1 : 0;
	grounds += !HDSpringCrackGround(1549.6f, west, 2, lift) ? 1 : 0;
	grounds += HDSpringCrackGround(1549.6f, his, 2, lift) ? 1 : 0;
	grounds += HDSpringCrackGround(1549.6f, &south, 1, lift) ? 1 : 0;

	for (int i = 0; i < 3; i++)
		grounds += HDSpringCrackGround(low[i][1], logs[i], 2, lift) ? 1 : 0;

	grounds += !HDSpringCrackGround(1549.6f, his, 0, lift) ? 1 : 0;

	ok(grounds == 8, "spring crack gate: under the bottom over the level's "
		"floor seen, in the rock or with no floor not", grounds, 8);

	/* A flat floor (the fountain's at 2,560, its bottom at 2,641): the
	 * plane is the floor, and an eye sees the crack exactly when it stands
	 * the lift over it - every eye from 2,000 to 2,700 a unit apart. */
	float fx[4] = { 0.0f, 300.0f, 0.0f, 300.0f };
	float fz[4] = { 0.0f, 0.0f, 60.0f, 60.0f };
	float fy[4] = { 2560.0f, 2560.0f, 2560.0f, 2560.0f };
	const HDSpringCrackFloor flat = HDSpringCrackFloorFit(fx, fz, fy, 4);
	int same = 0, steps = 0;

	for (int h = 2000; h <= 2700; h++, steps++)
	{
		const float eye[3] = { 5000.0f, (float)h, -3000.0f };

		same += HDSpringCrackSeen(eye, flat, lift) ==
			((float)h < 2560.0f - lift) ? 1 : 0;
	}

	ok(flat.slopeX == 0.0f && flat.slopeZ == 0.0f && flat.y == 2560.0f &&
		same == steps, "spring crack gate: on a flat floor the eye must stand "
		"the lift over it, wherever it stands", same, steps);

	// Points on one line span no plane: the flat floor at their mean.
	float lx[3] = { 0.0f, 100.0f, 200.0f }, lz[3] = { 0.0f, 50.0f, 100.0f };
	float ly[3] = { 1000.0f, 1100.0f, 1200.0f };
	const HDSpringCrackFloor line = HDSpringCrackFloorFit(lx, lz, ly, 3);

	ok(line.slopeX == 0.0f && line.slopeZ == 0.0f && line.y == 1100.0f &&
		line.top == 1000.0f, "spring crack gate: points on one line give the "
		"flat floor at their mean height", lround(line.y), 1100);
}

/* The column in a frame (hd_spring.h, HDSpringColumnInFrame and its parts)
 * on the view-space foot and top RoomProbe's measuring build printed at
 * the fountain's spring (ANDREA2, its foot on the crack's bottom at 2,641,
 * the sheet at 256), 960 x 540: three eyes of a game's camera log under the
 * water in room 120 at y 2,305 - two of them while the frame's rooms hold
 * no sheet, so only the column's own question takes the copy -, and eyes
 * of a fan round the column: one at mid height 300 units away looking up
 * 30 degrees, the foot behind it, and two looking along the line. */
static void spring_column()
{
	printf("the springs: the column in a frame\n");

	const HDSpring s = spring_of(1.0f);
	const float depth = 2641.0f - 256.0f, focal = 429.031f;
	const float proj[16] = { 0.89382f, 0, 0, 0, 0, -1.58900f, 0, 0, 0, 0, 1,
		1, 0, 0, 0, 0 };

	struct Eye
	{
		float b[3], t[3];
		int in;
	};

	static const Eye eyes[] =
	{
		// the log's ticks 270 and 287 (no sheet drawn) and 300
		{ { -162.611f, 311.640f, 1228.992f },
			{ -162.611f, -2072.520f, 1182.359f }, 1 },
		{ { -232.332f, 408.523f, 1084.551f },
			{ -232.332f, -1970.831f, 1242.871f }, 1 },
		{ { -358.184f, 564.764f, 891.992f },
			{ -358.184f, -1750.977f, 1460.977f }, 1 },
		// mid height, 300 away, up 30 degrees: the foot behind the eye
		{ { 0.0f, 1182.844f, -336.461f }, { 0.0f, -882.330f, 855.781f }, 0 },
		// straight under and straight over it: along the line, edge-on
		{ { 0.0f, 66.387f, -143.534f }, { 0.0f, 26.840f, 2240.753f }, 0 },
		{ { 0.0f, -25.504f, 2321.358f }, { 0.0f, -65.051f, -62.930f }, 0 },
		// the tick 270 turned away: both ends behind, or far to the side
		{ { 162.611f, 311.640f, -1228.992f },
			{ 162.611f, -2072.520f, -1182.359f }, 0 },
		{ { 3600.0f, 311.640f, 1228.992f },
			{ 3600.0f, -2072.520f, 1182.359f }, 0 },
	};
	int right = 0, total = 0;

	for (const Eye& e : eyes)
	{
		right += HDSpringColumnInFrame(s, depth, e.b, e.t, focal, proj) ==
			(e.in != 0) ? 1 : 0;
		total++;
	}

	ok(right == total, "spring column: in the frame with both ends in "
		"front, on the screen and not edge-on", right, total);

	/* The ends: both in front at the log's tick 270, the foot 336 behind
	 * the eye at the fan's, both behind turned away - the column whole or
	 * left out whole, as the draw takes it. */
	ok(HDSpringColumnFront(eyes[0].b, eyes[0].t) &&
		!HDSpringColumnFront(eyes[3].b, eyes[3].t) &&
		!HDSpringColumnFront(eyes[6].b, eyes[6].t), "spring column: drawn "
		"with both ends in front, left out with one or both behind", 1, 1);

	/* The weight at the log's tick 270: whole, and the quad's way across
	 * the line, ax x mid, of a length over 0. */
	float side[3], sl;
	const float ax[3] = { eyes[0].t[0] - eyes[0].b[0],
		eyes[0].t[1] - eyes[0].b[1], eyes[0].t[2] - eyes[0].b[2] };
	const float mid[3] = { (eyes[0].b[0] + eyes[0].t[0]) * 0.5f,
		(eyes[0].b[1] + eyes[0].t[1]) * 0.5f,
		(eyes[0].b[2] + eyes[0].t[2]) * 0.5f };
	const float w = HDSpringColumnWeight(s.strength, ax, mid,
		HDSpringHalfWidth(s, depth * 0.5f), focal, side, &sl);

	ok(w == 1.0f && sl > 0.0f, "spring column: the weight whole at the "
		"log's tick 270", lround(w * 1000.0f), 1000);
}

static void spring_clock()
{
	printf("the springs: the hashed clock\n");

	const HDSpring s = spring_of(1.0f);
	int order = 0, jitter = 0;

	for (int j = 0; j < 2000; j++)
	{
		const double a = HDSpringRingBorn(s, j);
		const double b = HDSpringRingBorn(s, j + 1);

		order += b > a ? 1 : 0;
		jitter += fabs(a - j * 0.7) <= 0.7 * kHDSpringJitter * 0.5 + 1e-9 ?
			1 : 0;
	}

	ok(order == 2000 && jitter == 2000,
		"spring clock: one ring a period, each within a fifth of its "
		"moment, in order", order, 2000);

	HDSpringBubbleAt a, b;
	int same = 0, alive = 0, pops = 0, inside = 0;

	for (int k = 0; k < 3000; k++)
	{
		const double t = HDSpringBubbleBorn(s, k) + 2.0;
		const bool live = HDSpringBubble(s, k, t, &a);

		same += live == HDSpringBubble(s, k, t, &b) && (!live ||
			(a.x == b.x && a.y == b.y && a.z == b.z)) ? 1 : 0;
		alive += live ? 1 : 0;
		inside += live && a.y < (float)s.floorY && a.y > (float)s.surfaceY &&
			fabsf(a.x - s.x) < 200.0f && fabsf(a.z - s.z) < 200.0f ? 1 : 0;

		float size;

		pops += HDSpringBubblePops(s, k, &size) && size >= 48.0f &&
			size <= 63.0f ? 1 : 0;
	}

	ok(same == 3000, "spring clock: a bubble at a moment is the same every "
		"time", same, 3000);
	ok(alive == 3000 && inside == 3000,
		"spring clock: two seconds old every bubble is in the jet, between "
		"the floor and the sheet, near its line", inside, 3000);
	ok(pops > 900 && pops < 1100,
		"spring clock: one bubble in three leaves a ring of 48 to 63", pops,
		1000);
	ok(fabs(HDSpringBubbleBorn(s, 3000) - 1000.0) < 0.34 &&
		HDSpringBubbleRate(spring_of(0.5f)) == 1.5f,
		"spring clock: three bubbles a second at strength 1, in step below",
		(long)HDSpringBubbleBorn(s, 3000), 1000);
	ok(!HDSpringBubble(s, 0, HDSpringBubbleBorn(s, 0) - 0.01, &a) &&
		!HDSpringBubble(s, 0, HDSpringBubbleBorn(s, 0) + 12.0, &a),
		"spring clock: none before its birth, none once it reached the sheet "
		"(2304 at 200 a second is 11.5 s)", 1, 1);

	/* The column's noise table: the mixer's bytes, every value of a byte
	 * in each channel, means of 127.0 and 128.1 (tenths), neighbours along
	 * a row and the two channels uncorrelated (under 0.03, thousandths). */
	const int side = kHDSpringNoiseSide;
	long sum[2] = { 0, 0 };
	bool seen[2][256] = {};
	int values[2] = { 0, 0 };
	double cross = 0.0, row = 0.0;

	for (int y = 0; y < side; y++)
		for (int x = 0; x < side; x++)
			for (int c = 0; c < 2; c++)
			{
				const int v = HDSpringNoiseTexel(x, y, c);

				sum[c] += v;
				values[c] += seen[c][v] ? 0 : 1;
				seen[c][v] = true;
			}

	const double m0 = (double)sum[0] / (side * side);
	const double m1 = (double)sum[1] / (side * side);
	double v0 = 0.0, v1 = 0.0;

	for (int y = 0; y < side; y++)
		for (int x = 0; x < side; x++)
		{
			const double a0 = HDSpringNoiseTexel(x, y, 0) - m0;
			const double a1 = HDSpringNoiseTexel(x, y, 1) - m1;
			const double n0 = HDSpringNoiseTexel((x + 1) % side, y, 1) - m1;

			v0 += a0 * a0;
			v1 += a1 * a1;
			cross += a0 * a1;
			row += a1 * n0;
		}

	cross /= sqrt(v0 * v1);
	row /= v1;
	ok(values[0] == 256 && values[1] == 256,
		"spring noise: every byte value in each channel", values[0] +
		values[1], 512);
	ok(lround(m0 * 10.0) == 1270 && lround(m1 * 10.0) == 1281,
		"spring noise: the channels' means 127.0 and 128.1",
		lround(m0 * 10.0) * 10000 + lround(m1 * 10.0), 12701281);
	ok(fabs(cross) < 0.03 && fabs(row) < 0.03,
		"spring noise: the channels and the neighbours uncorrelated",
		lround(fabs(cross) * 1000.0) + lround(fabs(row) * 1000.0), 0);
}

/* The marble floors' page column (hd_gloss_math.h, hd_gloss_pages.h): the
 * mirror word read at a line's end, a line of the word alone naming no
 * sheen, the file's word over the table's, and the 37 pages of the column
 * in the table - the lobby's among them, which keep no sheen. */
static void marble_pages()
{
	printf("the marble floors: the page column\n");

	HDGlossPage e;
	bool remove = false;

	ok(HDGlossParseLine("1054 marble mirror", &e, &remove) == 1 &&
		e.cls == HD_GLOSS_MARBLE && e.mirror == 1 && !remove,
		"marble: a class and the mirror word", e.mirror, 1);
	ok(HDGlossParseLine("960 MIRROR # grey slabs", &e, &remove) == 1 &&
		e.cls == HD_GLOSS_ASIS && e.mirror == 1 && e.page == 960,
		"marble: the word alone names no sheen", e.cls, HD_GLOSS_ASIS);
	ok(HDGlossParseLine("1047 nomirror", &e, &remove) == 1 && e.mirror == 0,
		"marble: nomirror", e.mirror, 0);
	ok(HDGlossParseLine("141 metal 0.5", &e, &remove) == 1 && e.mirror == -1,
		"marble: a line without the word leaves the column", e.mirror, -1);
	ok(HDGlossParseLine("-1054 mirror", &e, &remove) == 1 && remove &&
		e.mirror == 1, "marble: a removal keeps its mirror word", e.mirror, 1);
	ok(HDGlossParseLine("mirror", &e, &remove) < 0 &&
		HDGlossParseLine("960 mirror 3", &e, &remove) < 0,
		"marble: a word without a page, a number after it", 0, 0);

	HDGlossFile file;

	HDGlossParseText("960 nomirror\n1029 mirror\n1054 marble 0.5\n"
		"1086 mirror\n1086 nomirror\n", &file);

	bool removes = true;
	const HDGlossPage* asis = file.find(1029, &removes);
	const bool m960 = HDGlossMirror(file.mirror(960), HDGlossTableFind(960));
	const bool m1029 = HDGlossMirror(file.mirror(1029),
		HDGlossTableFind(1029));
	const bool m1054 = HDGlossMirror(file.mirror(1054),
		HDGlossTableFind(1054));
	const bool m1086 = HDGlossMirror(file.mirror(1086),
		HDGlossTableFind(1086));
	const bool m141 = HDGlossMirror(file.mirror(141), HDGlossTableFind(141));

	ok(!asis && !removes, "marble: the file's mirror line is no sheen line",
		asis ? 1 : 0, 0);
	ok(!m960 && m1029 && m1054 && !m1086 && !m141,
		"marble: the file's word, else the table's, the later line wins",
		(m960 ? 1 : 0) + (m1029 ? 2 : 0) + (m1054 ? 4 : 0) +
		(m1086 ? 8 : 0) + (m141 ? 16 : 0), 6);

	static const int kPages[] =
	{
		54, 162, 246, 249, 273, 274, 285, 366, 408, 411, 960, 1002, 1010,
		1029, 1045, 1046, 1047, 1048, 1049, 1053, 1054, 1063, 1070, 1079,
		1081, 1082, 1085, 1086, 1094, 1104, 1105, 1110, 2147, 2149, 2154,
		3314, 3315
	};
	const int count = (int)(sizeof(kPages) / sizeof(kPages[0]));
	int listed = 0, column = 0;

	for (int i = 0; i < count; i++)
		listed += HDGlossTableFind(kPages[i]) &&
			HDGlossTableFind(kPages[i])->mirror == 1 ? 1 : 0;

	for (int i = 0; i < HDGlossTableSize(); i++)
		column += kHDGlossPages[i].mirror;

	ok(listed == 37 && column == 37,
		"marble: the 37 pages of the column and no other", listed * 100 +
		column, 3737);

	// A lobby page in the column names no sheen: HDGlossResolve finds none.
	const HDGloss lobby = HDGlossResolve(0, 0, false,
		HDGlossTableFind(1047), HD_GLOSS_NONE, HDGlossPageBarred(1047));
	const HDGloss slabs = HDGlossResolve(0, 0, false, HDGlossTableFind(960),
		HD_GLOSS_MARBLE, false);

	ok(lobby.strength == 0.0f && slabs.cls == HD_GLOSS_MARBLE &&
		slabs.source == HD_GLOSS_FROM_SEED,
		"marble: a line of the column alone leaves the sheen's sources",
		slabs.source, HD_GLOSS_FROM_SEED);
}

/* The marble floors' rules (hd_splash_math.h): the face rule's flat and
 * up at 0.95 of the normal (y runs down), either winding; the share of the
 * screen a triangle covers, cut to the view; the planes picked by that
 * share, the higher floor first between equals, at most the cap and none
 * that covers nothing. */
static void marble_rules()
{
	printf("the marble floors: the face rule and the planes\n");

	const float a[3] = { 0.0f, 0.0f, 0.0f };
	const float b[3] = { 1024.0f, 0.0f, 0.0f };
	const float c[3] = { 0.0f, 0.0f, 1024.0f };
	const float tilt[3] = { 0.0f, -300.0f, 1024.0f };	// 16.3 degrees
	const float steep[3] = { 0.0f, -400.0f, 1024.0f };	// 21.3 degrees

	ok(HDMarbleFace(a, b, c, -1.0f) && HDMarbleFace(a, c, b, -1.0f) &&
		!HDMarbleFace(a, b, c, 1.0f),
		"marble: a flat face up in either winding, not down", 0, 0);
	ok(HDMarbleFace(a, b, tilt, -0.9f) && !HDMarbleFace(a, b, steep, -0.9f),
		"marble: 16 degrees is flat, 21 is not", 0, 0);

	// A triangle wholly in view, half of it past the right edge, behind.
	const float whole[12] = { -0.5f, -0.5f, 0.5f, 1.0f, 0.5f, -0.5f, 0.5f,
		1.0f, -0.5f, 0.5f, 0.5f, 1.0f };
	const float half[12] = { 0.0f, -1.0f, 0.5f, 1.0f, 2.0f, -1.0f, 0.5f,
		1.0f, 0.0f, 1.0f, 0.5f, 1.0f };
	const float behind[12] = { -0.5f, -0.5f, -0.5f, 1.0f, 0.5f, -0.5f,
		-0.5f, 1.0f, -0.5f, 0.5f, -0.5f, 1.0f };
	const float cw = HDMarbleCoverage(whole);
	const float ch = HDMarbleCoverage(half);

	ok(fabsf(cw - 0.5f) < 1e-4f && fabsf(ch - 1.5f) < 1e-4f &&
		HDMarbleCoverage(behind) == 0.0f,
		"marble: the covered share, cut to the view", lroundf(ch * 1000.0f),
		1500);

	HDMarblePlane p[4] = { { -768, 0.2f }, { -512, 0.9f }, { -2816, 0.2f },
		{ 0, 0.0f } };
	const int n2 = HDMarblePlanesPick(p, 4, 2);
	const bool order = p[0].y == -512 && p[1].y == -2816 && p[2].y == -768;
	HDMarblePlane q[2] = { { 0, 0.0f }, { 256, 0.0f } };

	ok(n2 == 2 && order && HDMarblePlanesPick(p, 4, 8) == 3 &&
		HDMarblePlanesPick(q, 2, 2) == 0,
		"marble: the planes by cover, the higher first, the cap", n2, 2);

	// The level key: a kept load is built again when hd_marble changes.
	HDLevelKey k1, k2;

	memset(&k1, 0, sizeof(k1));
	k1.valid = 1;
	k1.marble = 1;
	k2 = k1;
	k2.marble = 0;

	const char* why = HDLevelKeyDiffers(&k1, &k2);

	ok(why && !strcmp(why, "hd_marble") && !HDLevelKeyDiffers(&k1, &k1),
		"marble: the level key names hd_marble", why ? 1 : 0, 1);
}

/* The marble floors' places (hd_marble_math.h): the pixels' share in the
 * cover's units; places all empty take the best planes whole; a plane that
 * shows nothing leaves at once; a plane outscored fades out a step a frame
 * and its place is then taken from one step up; the hold keeps a plane
 * that scores half the best outside; the cap is kept throughout. */
static void marble_places()
{
	printf("the marble floors: the places and the fade\n");

	ok(fabsf(HDMarbleSeenCover(960 * 540, 960, 540) - 4.0f) < 1e-5f &&
		fabsf(HDMarbleSeenCover(960 * 135, 960, 540) - 1.0f) < 1e-5f &&
		HDMarbleSeenCover(5, 0, 540) == 0.0f,
		"marble places: the pixels' share, 4 the whole screen", 0, 0);

	HDMarbleSlot s[2] = { { INT_MIN, 0.0f }, { INT_MIN, 0.0f } };
	int ys[4] = { 0, -256, -512, -768 };
	float sc[4] = { 0.5f, 0.2f, 0.1f, 0.0f };
	int used = HDMarbleSlotsStep(s, 2, ys, sc, 4, 0.125f, HD_MARBLE_HOLD);

	ok(used == 2 && s[0].y == 0 && s[1].y == -256 && s[0].gain == 1.0f &&
		s[1].gain == 1.0f,
		"marble places: empty places take the best two whole", used, 2);

	// -512 outscores -256 by more than the hold: -256 fades, a step.
	float sc2[4] = { 0.5f, 0.05f, 0.3f, 0.0f };

	HDMarbleSlotsStep(s, 2, ys, sc2, 4, 0.125f, HD_MARBLE_HOLD);
	ok(s[1].y == -256 && fabsf(s[1].gain - 0.875f) < 1e-6f &&
		s[0].gain == 1.0f,
		"marble places: an outscored plane fades a step", lroundf(s[1].gain *
		1000.0f), 875);

	int frames = 1;

	while (s[1].y == -256 && frames < 20)
	{
		HDMarbleSlotsStep(s, 2, ys, sc2, 4, 0.125f, HD_MARBLE_HOLD);
		frames++;
	}

	ok(frames == 8 && s[1].y == -512 && fabsf(s[1].gain - 0.125f) < 1e-6f,
		"marble places: gone after eight steps, the next from one", frames,
		8);

	for (int i = 0; i < 10; i++)
		HDMarbleSlotsStep(s, 2, ys, sc2, 4, 0.125f, HD_MARBLE_HOLD);

	ok(s[1].y == -512 && s[1].gain == 1.0f && s[0].y == 0,
		"marble places: the new plane rises to whole and stays", 0, 0);

	// Half the best outside holds; a little less does not.
	HDMarbleSlot h[1] = { { 0, 1.0f } };
	int hy[2] = { 0, -256 };
	float hold[2] = { 0.5f, 1.0f };
	float lose[2] = { 0.49f, 1.0f };

	HDMarbleSlotsStep(h, 1, hy, hold, 2, 0.125f, HD_MARBLE_HOLD);

	const bool held = h[0].y == 0 && h[0].gain == 1.0f;

	HDMarbleSlotsStep(h, 1, hy, lose, 2, 0.125f, HD_MARBLE_HOLD);
	ok(held && h[0].y == 0 && fabsf(h[0].gain - 0.875f) < 1e-6f,
		"marble places: the hold at half the best outside", 0, 0);

	// What shows nothing leaves at once; a view of new planes is whole.
	HDMarbleSlot g[2] = { { 0, 1.0f }, { -256, 0.5f } };
	int gy[2] = { -1024, -1280 };
	float gs[2] = { 0.4f, 0.3f };

	used = HDMarbleSlotsStep(g, 2, gy, gs, 2, 0.125f, HD_MARBLE_HOLD);
	ok(used == 2 && g[0].y == -1024 && g[1].y == -1280 &&
		g[0].gain == 1.0f && g[1].gain == 1.0f,
		"marble places: a plane gone leaves, a new view is whole", used, 2);

	HDMarbleSlot z[2] = { { 0, 1.0f }, { -256, 1.0f } };
	float zs[4] = { 0.5f, 0.0f, 0.2f, 0.1f };

	HDMarbleSlotsStep(z, 2, ys, zs, 4, 0.125f, HD_MARBLE_HOLD);
	ok(z[0].y == 0 && z[0].gain == 1.0f && z[1].y == -512 &&
		fabsf(z[1].gain - 0.125f) < 1e-6f,
		"marble places: a hidden plane's place taken from one step", 0, 0);

	// The cap: never more places than it, whatever the planes.
	HDMarbleSlot c[8];
	int cy[6] = { 0, -256, -512, -768, -1024, -1280 };
	float cs[6] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };

	for (HDMarbleSlot& one : c)
	{
		one.y = INT_MIN;
		one.gain = 0.0f;
	}

	used = HDMarbleSlotsStep(c, 2, cy, cs, 6, 0.125f, HD_MARBLE_HOLD);
	ok(used == 2 && c[0].y == -1280 && c[1].y == -1024 &&
		c[2].y == INT_MIN,
		"marble places: the cap holds", used, 2);
}

/* The marble floors' counts and fade time (hd_marble_math.h): a plane's
 * score is its count's share of the screen while the count is fresh, its
 * cover with no count, an old one or one from a frame to come; the fade's
 * step is the frame's time over the whole fade, cut at the longest frame,
 * so a whole fade takes the same time at any frame rate. */
static void marble_counts()
{
	printf("the marble floors: the counts and the fade time\n");

	ok(fabsf(HDMarbleScore(960 * 135, 2, 6, 0.3f, 960, 540) - 1.0f) <
		1e-5f, "marble counts: a fresh count is the pixels' share", 0, 0);
	ok(HDMarbleScore(0, 1, 6, 0.3f, 960, 540) == 0.0f,
		"marble counts: a fresh count of nothing scores nothing", 0, 0);
	ok(HDMarbleScore(-1, 0, 6, 0.3f, 960, 540) == 0.3f,
		"marble counts: no count, the cover", 0, 0);
	ok(HDMarbleScore(960 * 135, 6, 6, 0.3f, 960, 540) == 1.0f &&
		HDMarbleScore(960 * 135, 7, 6, 0.3f, 960, 540) == 0.3f,
		"marble counts: six frames old counts, seven does not", 0, 0);
	ok(HDMarbleScore(960 * 135, -1, 6, 0.3f, 960, 540) == 0.3f,
		"marble counts: a count from a later frame does not", 0, 0);

	const double whole = 8000.0 / 60.0;

	ok(lroundf(HDMarbleFadeStep(1000.0 / 60.0, whole, 50.0) * 1000.0f) ==
		125 && lroundf(HDMarbleFadeStep(1000.0 / 30.0, whole, 50.0) *
		1000.0f) == 250,
		"marble counts: an eighth a frame at 60, a quarter at 30",
		lroundf(HDMarbleFadeStep(1000.0 / 30.0, whole, 50.0) * 1000.0f), 250);
	ok(lroundf(HDMarbleFadeStep(500.0, whole, 50.0) * 1000.0f) == 375,
		"marble counts: a halt counts as the longest frame",
		lroundf(HDMarbleFadeStep(500.0, whole, 50.0) * 1000.0f), 375);
	ok(HDMarbleFadeStep(-5.0, whole, 50.0) == 0.0f &&
		HDMarbleFadeStep(10.0, 0.0, 50.0) == 1.0f &&
		HDMarbleFadeStep(400.0, 100.0, 1000.0) == 1.0f,
		"marble counts: no time no step; no fade at once; at most 1", 0, 0);

	// A whole fade in and out at 30, 60 and 144 frames a second.
	int frames[3] = { 0, 0, 0 };
	const double rate[3] = { 30.0, 60.0, 144.0 };

	for (int r = 0; r < 3; r++)
	{
		HDMarbleSlot s[1] = { { 0, 1.0f } };
		int ys[2] = { 0, -256 };
		float sc[2] = { 0.1f, 0.5f };
		const float step = HDMarbleFadeStep(1000.0 / rate[r], whole, 50.0);

		while (frames[r] < 1000 && !(s[0].y == -256 && s[0].gain >= 1.0f))
		{
			HDMarbleSlotsStep(s, 1, ys, sc, 2, step, HD_MARBLE_HOLD);
			frames[r]++;
		}
	}

	const double ms[3] = { frames[0] * 1000.0 / 30.0,
		frames[1] * 1000.0 / 60.0, frames[2] * 1000.0 / 144.0 };

	ok(frames[1] == 15 && fabs(ms[0] - ms[1]) <= 1000.0 / 30.0 &&
		fabs(ms[2] - ms[1]) <= 1000.0 / 30.0,
		"marble counts: out and in, the same time at 30, 60 and 144",
		frames[1], 15);
}

/* The marble floors' rooms over (hd_marble_math.h, HDMarbleOverRooms): a
 * shaft of rooms - 0 drawn, 1 over it, 2 beside it and undrawn, 3 over 2,
 * 4 over 1, 5 over 4 - and a flip table that draws room 1 as group 6. */
static void marble_over()
{
	printf("the marble floors: the rooms over\n");

	std::vector<std::vector<int> > up(7), side(7);
	std::vector<char> drawn(7, 0);
	std::vector<HDMarbleOverRoom> out;

	up[0] = { 1 };
	side[0] = { 2 };
	up[2] = { 3 };
	up[1] = { 4 };
	up[4] = { 5 };
	side[1] = { 3 };
	drawn[0] = 1;

	HDMarbleOverRooms(up, side, 0, 0, drawn, 0, out);
	ok(out.empty(), "marble over: depth 0 takes none", (int)out.size(), 0);

	HDMarbleOverRooms(up, side, 0, 0, drawn, 1, out);
	ok(out.size() == 1 && out[0].group == 1 && out[0].via == 0,
		"marble over: depth 1, the room over the drawn one", (int)out.size(),
		1);

	HDMarbleOverRooms(up, side, 0, 0, drawn, 2, out);
	ok(out.size() == 2 && out[1].group == 3 && out[1].via == 0,
		"marble over: depth 2, over the undrawn room beside, by the drawn",
		(int)out.size(), 2);

	HDMarbleOverRooms(up, side, 0, 0, drawn, 3, out);
	ok(out.size() == 3 && out[2].group == 4 && out[2].via == 0,
		"marble over: depth 3, the room over the room over, once",
		(int)out.size(), 3);

	// Room 2 drawn: it is no room over, and its own room over is depth 1.
	drawn[2] = 1;
	HDMarbleOverRooms(up, side, 0, 0, drawn, 2, out);
	ok(out.size() == 2 && out[0].group == 1 && out[1].group == 3 &&
		out[1].via == 2,
		"marble over: a drawn neighbour's room over is its own", 0, 0);

	// The flip: room 1 drawn now by group 6, whose ceiling is room 5's.
	const short table[7] = { 0, 6, 2, 3, 4, 5, 1 };

	drawn[2] = 0;
	up[6] = { 5 };
	HDMarbleOverRooms(up, side, table, 7, drawn, 3, out);
	ok(out.size() == 3 && out[0].group == 6 && out[2].group == 5,
		"marble over: a flip map names the group that draws the room",
		(int)out.size(), 3);

	// A room out of the table or the groups is none.
	std::vector<std::vector<int> > far(1);
	std::vector<char> one(1, 1);

	far[0] = { 9, -1 };
	HDMarbleOverRooms(far, far, 0, 0, one, 3, out);
	ok(out.empty() && HDMarbleGroupOf(3, table, 2, 7) == -1 &&
		HDMarbleGroupOf(1, table, 7, 7) == 6,
		"marble over: rooms out of range are none", (int)out.size(), 0);
}

/* The marble floors' slopes (hd_marble_math.h): a sloped face's plane, its
 * normal up and of length 1 in either winding, its distance in world space
 * with the group's origin; a level face's plane is the floor's height; two
 * faces of one slope are one plane, a slope beside it or over it not. */
static void marble_slopes()
{
	printf("the marble floors: the sloped planes\n");

	// A ramp of 256 units over a square, rising toward +z (y runs down).
	const float ramp[9] = { 0.0f, 0.0f, 0.0f, 1024.0f, 0.0f, 0.0f, 0.0f,
		-256.0f, 1024.0f };
	const float back[9] = { 0.0f, 0.0f, 0.0f, 0.0f, -256.0f, 1024.0f,
		1024.0f, 0.0f, 0.0f };
	float a[4], b[4];

	ok(HDMarbleTiltPlane(ramp, 0.0f, 0.0f, a) &&
		HDMarbleTiltPlane(back, 0.0f, 0.0f, b) && a[1] < 0.0f && b[1] < 0.0f,
		"marble slopes: the normal up in either winding", 0, 0);
	ok(fabsf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] - 1.0f) < 1e-5f &&
		fabsf(a[2] - b[2]) < 1e-6f && fabsf(a[2] + 0.2425f) < 1e-3f,
		"marble slopes: of length 1, leaning 1 in 4", lroundf(-a[2] * 1e4f),
		2425);

	const float level[9] = { 0.0f, -2816.0f, 0.0f, 1024.0f, -2816.0f, 0.0f,
		0.0f, -2816.0f, 1024.0f };
	float l[4];

	ok(HDMarbleTiltPlane(level, 5000.0f, 7000.0f, l) &&
		fabsf(l[1] + 1.0f) < 1e-6f && fabsf(l[3] - 2816.0f) < 1e-3f,
		"marble slopes: a level face's plane is its height", lroundf(l[3]),
		2816);

	// The ramp's next square, one square on in x, in a group 1024 off.
	const float next[9] = { 0.0f, 0.0f, 0.0f, 1024.0f, 0.0f, 0.0f, 0.0f,
		-256.0f, 1024.0f };
	float n[4];

	HDMarbleTiltPlane(next, 1024.0f, 0.0f, n);
	ok(HDMarbleTiltSame(a, n, 0.001f, 2.0f),
		"marble slopes: one ramp's squares are one plane", lroundf(n[3]),
		lroundf(a[3]));

	// The same lean 256 units higher, and the lean turned to +x.
	const float over[9] = { 0.0f, -256.0f, 0.0f, 1024.0f, -256.0f, 0.0f,
		0.0f, -512.0f, 1024.0f };
	const float turned[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1024.0f,
		1024.0f, -256.0f, 0.0f };
	float o[4], t[4];

	HDMarbleTiltPlane(over, 0.0f, 0.0f, o);
	HDMarbleTiltPlane(turned, 0.0f, 0.0f, t);
	ok(!HDMarbleTiltSame(a, o, 0.001f, 2.0f) &&
		!HDMarbleTiltSame(a, t, 0.001f, 2.0f),
		"marble slopes: a step over or a turned lean is another", 0, 0);

	const float none[9] = { 0.0f, 0.0f, 0.0f, 512.0f, 0.0f, 0.0f, 1024.0f,
		0.0f, 0.0f };

	ok(!HDMarbleTiltPlane(none, 0.0f, 0.0f, o),
		"marble slopes: a face of no area has no plane", 0, 0);
}

/* The level's HD key (hd_levelkey.h): a key equal to itself keeps the HD
 * data, every field changed alone refuses with that field's name, and a
 * load with HD off, with nothing built or after an outfit change refuses
 * before any field is compared. */
// A key's text field, cut to its size.
template <size_t N> static void key_set(char (&out)[N], const char* text)
{
	snprintf(out, N, "%s", text);
}

static void level_key_base(HDLevelKey* k)
{
	memset(k, 0, sizeof(*k));
	k->valid = 1;
	k->level = 12;
	key_set(k->name, "RICH2");
	k->trc.size = 1310720;
	k->trc.written = 134050000000000000LL;
	k->hdm.size = -1;
	k->hdm.written = -1;
	k->trg.size = 47185920;
	k->trg.written = 133900000000000000LL;
	k->glossFile.size = -1;
	k->glossFile.written = -1;
	k->waterFile.size = 2048;
	k->waterFile.written = 134040000000000000LL;
	key_set(k->install, "D:\\Remastered\\5");
	k->air = 2;
	k->water = 1;
	k->objects = 1;
	key_set(k->format, "rgba");
}

static void level_key()
{
	printf("the level key: keep the HD data or build them again\n");

	struct Row
	{
		const char* what;
		void (*change)(HDLevelKey*);
		const char* refused;		// 0 keeps
	};

	static const Row rows[] =
	{
		{ "the same level, nothing changed", [](HDLevelKey*) {}, 0 },
		{ "another level", [](HDLevelKey* k) { k->level = 13; }, "level" },
		{ "another file name", [](HDLevelKey* k)
			{ key_set(k->name, "RICH3"); }, "name" },
		{ "the .TRC rewritten, same size", [](HDLevelKey* k)
			{ k->trc.written++; }, "trc" },
		{ "the .TRC another size", [](HDLevelKey* k)
			{ k->trc.size += 2; }, "trc" },
		{ "a sidecar where there was none", [](HDLevelKey* k)
			{ k->hdm.size = 96; k->hdm.written = 1; }, "hdm" },
		{ "the .TRG replaced", [](HDLevelKey* k)
			{ k->trg.written += 10; }, "trg" },
		{ "hd_gloss.txt written", [](HDLevelKey* k)
			{ k->glossFile.size = 10; k->glossFile.written = 5; },
			"gloss_file" },
		{ "hd_water.txt edited", [](HDLevelKey* k)
			{ k->waterFile.written++; }, "water_file" },
		{ "another installation", [](HDLevelKey* k)
			{ key_set(k->install, "E:\\Remastered\\5"); }, "install" },
		{ "the I-III bundle found", [](HDLevelKey* k)
			{ key_set(k->installTr3, "E:\\I-III\\3"); }, "install_tr3" },
		{ "hd_air 2 to 1", [](HDLevelKey* k) { k->air = 1; }, "hd_air" },
		{ "hd_water off", [](HDLevelKey* k) { k->water = 0; }, "hd_water" },
		{ "hd_objects off", [](HDLevelKey* k) { k->objects = 0; },
			"hd_objects" },
		{ "a texture budget set", [](HDLevelKey* k) { k->budgetMb = 2048; },
			"hd_texture_budget_mb" },
		{ "the pages as dxt5", [](HDLevelKey* k)
			{ key_set(k->format, "dxt5"); }, "hd_texture_format" },
		{ "another outfit chosen", [](HDLevelKey* k)
			{ key_set(k->outfit, "mod10:OUTFIT_LEGEND"); }, "hd_outfit" },
	};

	for (const Row& row : rows)
	{
		HDLevelKey built, next;

		level_key_base(&built);
		level_key_base(&next);
		row.change(&next);

		const char* got = HDLevelKeepRefused(&built, &next, 0);
		const bool same = row.refused ? got && !strcmp(got, row.refused)
			: !got;
		char what[96];

		snprintf(what, sizeof(what), "level key: %s", row.what);
		ok(same, what, got ? 1 : 0, row.refused ? 1 : 0);
	}

	// The three refusals before any field, and their order.
	HDLevelKey built, next;

	level_key_base(&built);
	level_key_base(&next);
	next.valid = 0;
	ok(!strcmp(HDLevelKeepRefused(&built, &next, 0), "no_key"),
		"level key: HD off at this load keeps nothing", 0, 0);

	next.valid = 1;
	built.valid = 0;
	ok(!strcmp(HDLevelKeepRefused(&built, &next, 0), "nothing_built"),
		"level key: built with HD off, or released, keeps nothing", 0, 0);

	built.valid = 1;
	ok(!strcmp(HDLevelKeepRefused(&built, &next, 1), "redressed"),
		"level key: an outfit change in the level builds again", 1, 0);

	next.level = 13;
	ok(!strcmp(HDLevelKeepRefused(&built, &next, 2), "redressed"),
		"level key: the outfit change is named before a field", 2, 0);

	// A key that is not valid is not equal to itself either.
	built.valid = 0;
	ok(!strcmp(HDLevelKeyDiffers(&built, &built), "no_key"),
		"level key: no key is the same as nothing", 0, 0);
}

/* debris_pieces: the pieces of a broken static in HD (hd_debris_rules.h).
 * A triangle of the static's art goes with the classic face nearest its
 * centroid, a place's coincident faces share it by a hash of its number,
 * and its run gives the blend. The faces below are ANDREA2's window as the
 * level holds it: flat in the plane x 1024 of room 132, each quad twice
 * (the pane's two sides), its art 51 units deep around that plane. */
static HDDebrisFace DebrisQuad(float x, float y0, float z0, float y1,
	float z1, bool back)
{
	HDDebrisFace f;
	const float c[4][3] =
	{
		{ x, y0, z0 }, { x, y0, z1 }, { x, y1, z1 }, { x, y1, z0 }
	};

	f.corners = 4;

	for (int k = 0; k < 4; k++)
		for (int a = 0; a < 3; a++)
			f.corner[k][a] = c[back ? 3 - k : k][a];

	return f;
}

static void debris_pieces()
{
	printf("the broken static's pieces: the face rule and the blend\n");

	/* A face of the breaking mesh as the break walks them: the triangles,
	 * then the quads, the corners the turned vertices at the static's place;
	 * a corner past the vertices turned, or past 256, leaves none. */
	{
		const short gt3[5] = { 0, 1, 2, 7, 0 };
		const short gt4[12] = { 0, 1, 2, 3, 9, 0, 3, 2, 1, 256, 9, 0 };
		static short rot[257 * 3];
		HDDebrisFace f;

		for (int v = 0; v < 257; v++)
		{
			rot[v * 3] = (short)v;
			rot[v * 3 + 1] = (short)(-2 * v);
			rot[v * 3 + 2] = (short)(3 * v);
		}

		HDDebrisMeshFace(gt3, 1, gt4, 0, rot, 4, 1000, 2000, 3000, &f);
		ok(f.corners == 3 && f.corner[2][0] == 1002.0f &&
			f.corner[2][1] == 1996.0f && f.corner[2][2] == 3006.0f,
			"debris: face 0 is the triangle, its corners at the place",
			f.corners, 3);
		HDDebrisMeshFace(gt3, 1, gt4, 1, rot, 4, 1000, 2000, 3000, &f);
		ok(f.corners == 4 && f.corner[3][0] == 1003.0f &&
			f.corner[3][2] == 3009.0f,
			"debris: face 1 is the first quad", f.corners, 4);
		HDDebrisMeshFace(gt3, 1, gt4, 1, rot, 3, 1000, 2000, 3000, &f);
		ok(f.corners == 0, "debris: a corner past the vertices turned",
			f.corners, 0);
		HDDebrisMeshFace(gt3, 1, gt4, 2, rot, 257, 0, 0, 0, &f);
		ok(f.corners == 0, "debris: a corner past the 256 the break turns",
			f.corners, 0);
	}

	// The distance: over the middle, past a corner, beside an edge.
	{
		const float a[3] = { 0, 0, 0 }, b[3] = { 100, 0, 0 };
		const float c[3] = { 0, 0, 100 };
		const float over[3] = { 20, -26, 20 }, past[3] = { -30, 0, -40 };
		const float side[3] = { 50, 0, -10 };

		ok(fabsf(HDDebrisPointTriangle(over, a, b, c) - 26.0f) < 0.001f,
			"debris: 26 units over a face is 26 off it",
			(long)HDDebrisPointTriangle(over, a, b, c), 26);
		ok(fabsf(HDDebrisPointTriangle(past, a, b, c) - 50.0f) < 0.001f,
			"debris: past a corner the corner is nearest",
			(long)HDDebrisPointTriangle(past, a, b, c), 50);
		ok(fabsf(HDDebrisPointTriangle(side, a, b, c) - 10.0f) < 0.001f,
			"debris: beside an edge the edge is nearest",
			(long)HDDebrisPointTriangle(side, a, b, c), 10);
	}

	// A quad by both its halves; a face with no corners is never nearest.
	{
		const HDDebrisFace q = DebrisQuad(1024, -4096, 2048, -3840, 2304,
			false);
		const float far[3] = { 1024, -3900, 2100 };
		HDDebrisFace none = q;

		none.corners = 0;
		ok(HDDebrisFaceDistance(q, far) < 0.001f,
			"debris: a point on a quad's second half lies on it",
			(long)HDDebrisFaceDistance(q, far), 0);
		ok(HDDebrisFaceDistance(none, far) > 1.0e37f,
			"debris: a face past the 256 vertices has no distance", 0, 1);
	}

	// One place: the two sides of a pane, a neighbour, a triangle.
	{
		HDDebrisFace f[4];

		f[0] = DebrisQuad(1024, -4096, 2048, -3840, 2304, false);
		f[1] = DebrisQuad(1024, -3840, 2048, -3584, 2304, false);
		f[2] = DebrisQuad(1024, -4096, 2048, -3840, 2304, true);
		f[3] = f[0];
		f[3].corners = 3;

		int place[4];

		HDDebrisPlaces(f, 4, place);
		ok(HDDebrisSamePlace(f[0], f[2]),
			"debris: a pane's back is its front's place", 0, 1);
		ok(!HDDebrisSamePlace(f[0], f[1]),
			"debris: the next pane is a place of its own", 1, 0);
		ok(!HDDebrisSamePlace(f[0], f[3]),
			"debris: a triangle is no quad's place", 1, 0);
		ok(place[0] == 0 && place[1] == 1 && place[2] == 0 && place[3] == 3,
			"debris: places 0 1 0 3", place[2] * 10 + place[3], 3);
	}

	// The hash: fixed, and a fair deal between two sides.
	{
		int side[2] = { 0, 0 };

		for (unsigned int t = 0; t < 2146; t++)
			side[HDDebrisDeal(t, 2)]++;

		ok(HDDebrisHash(1) == HDDebrisHash(1) && HDDebrisHash(1) != 1u,
			"debris: the hash is a function and moves its input", 0, 0);
		ok(HDDebrisDeal(12345, 1) == 0,
			"debris: a face alone takes every triangle",
			HDDebrisDeal(12345, 1), 0);
		ok(side[0] > 960 && side[1] > 960,
			"debris: 2146 triangles dealt to two sides, both near half",
			side[0], 1073);
	}

	/* The window: 8 by 8 panes, each twice, front then back. The art over
	 * every pane lies on that pane's two faces, half and half; with the
	 * boxes and the places worked out ahead the answers are the same. */
	{
		std::vector<HDDebrisFace> f;

		for (int i = 0; i < 8; i++)
			for (int k = 0; k < 8; k++)
				for (int side = 0; side < 2; side++)
					f.push_back(DebrisQuad(1024, -4096.0f + i * 256,
						2048.0f + k * 256, -3840.0f + i * 256,
						2304.0f + k * 256, side == 1));

		const int n = (int)f.size();
		std::vector<float> boxes((size_t)n * 6);
		std::vector<int> place((size_t)n);

		for (int i = 0; i < n; i++)
			HDDebrisFaceBox(f[(size_t)i], &boxes[(size_t)i * 6]);

		HDDebrisPlaces(f.data(), n, place.data());

		int same = 0, onPane = 0, sides[2] = { 0, 0 }, tri = 0;

		for (int i = 0; i < 8; i++)
			for (int k = 0; k < 8; k++)
				for (int j = 0; j < 9; j++, tri++)
				{
					const float c[3] = { 998.0f + (j % 3) * 25.5f,
						-4096.0f + i * 256 + 16 + j * 25,
						2048.0f + k * 256 + 200 - j * 20 };
					const int plain = HDDebrisFaceOf(f.data(), n, c,
						(unsigned int)tri);
					const int quick = HDDebrisFaceOf(f.data(), n, c,
						(unsigned int)tri, boxes.data(), place.data());

					same += plain == quick;
					onPane += plain / 2 == i * 8 + k;
					sides[plain & 1]++;
				}

		ok(same == tri, "debris: the box cut and the places change no answer",
			same, tri);
		ok(onPane == tri, "debris: every triangle goes with its own pane",
			onPane, tri);
		ok(sides[0] > 240 && sides[1] > 240,
			"debris: and the pane's two sides share its triangles", sides[0],
			288);
	}

	// No reach: a triangle far from the window still finds its face.
	{
		HDDebrisFace f[2];

		f[0] = DebrisQuad(1024, -4096, 2048, -3840, 2304, false);
		f[1] = DebrisQuad(1024, -3840, 2048, -3584, 2304, false);

		const float c[3] = { 1175, -3700, 2100 };

		ok(HDDebrisFaceOf(f, 2, c, 0) == 1,
			"debris: 151 units off the window, the nearest face takes it",
			HDDebrisFaceOf(f, 2, c, 0), 1);
		ok(HDDebrisFaceOf(f, 0, c, 0) == -1, "debris: no face, no answer",
			HDDebrisFaceOf(f, 0, c, 0), -1);
	}

	/* The blend, as the room draw reads a destructible part's runs: ANDREA2's
	 * grid 0x800e in the alpha run at param[1] 0.1, ANDREA1's frame 0x8000
	 * opaque and its pane 0x800e additive. */
	{
		const float grid[4] = { 2.0f, 0.1f, 0.0f, 0.0f };
		const float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const float over[4] = { 0.0f, 1.5f, 0.0f, 0.0f };

		ok(HDDebrisKindOf(0x8000u, 0) == HD_DEBRIS_SOLID,
			"debris: an opaque run is solid", HDDebrisKindOf(0x8000u, 0),
			HD_DEBRIS_SOLID);
		ok(HDDebrisKindOf(0x800Eu, 1) == HD_DEBRIS_GLASS,
			"debris: material 14's alpha run is glass",
			HDDebrisKindOf(0x800Eu, 1), HD_DEBRIS_GLASS);
		ok(HDDebrisKindOf(0x8000u, 1) == HD_DEBRIS_CUTOUT,
			"debris: any other alpha run is cut", HDDebrisKindOf(0x8000u, 1),
			HD_DEBRIS_CUTOUT);
		ok(HDDebrisKindOf(0x800Eu, 2) == HD_DEBRIS_GLOW,
			"debris: an additive run is a glow", HDDebrisKindOf(0x800Eu, 2),
			HD_DEBRIS_GLOW);
		ok(HDDebrisSeeThrough(HD_DEBRIS_GLASS, grid) == 0.1f,
			"debris: the grid's glass at its 0.1",
			(long)(HDDebrisSeeThrough(HD_DEBRIS_GLASS, grid) * 1000), 100);
		ok(HDDebrisSeeThrough(HD_DEBRIS_GLASS, none) == 1.0f &&
			HDDebrisSeeThrough(HD_DEBRIS_GLASS, over) == 1.0f,
			"debris: an opacity outside (0, 1] is the page's own", 0, 0);
		ok(HDDebrisSeeThrough(HD_DEBRIS_SOLID, grid) == 1.0f,
			"debris: a solid piece is whole", 0, 0);
	}
}

/* The far plane and the depth curve (specific\zrange_math.h): the plane's
 * number and its reason, the curve on ZFAR_SHIPPED, the depth buffer's
 * step along it, the rain's tint. */
static void far_plane()
{
	printf("the far plane: the number, the depth curve, the rain's tint\n");

	ok(ZFAR_UNITS == 41 * 1024, "far plane: 41 sectors", ZFAR_UNITS, 41 * 1024);

	/* The deepest thing a walk from inside a room lists (RoomProbe
	 * -farsurvey, JOBY2), a square's margin over it, and the smallest whole
	 * sector count that holds both. */
	const long deepest = 40806, margin = 1024;

	ok(ZFAR_UNITS >= deepest + margin,
		"far plane: holds the deepest listed thing and a square",
		ZFAR_UNITS, deepest + margin);
	ok((ZFAR_SECTORS - 1) * 1024 < deepest + margin,
		"far plane: ...and a sector less would not",
		(ZFAR_SECTORS - 1) * 1024, deepest + margin);
	ok(ZFAR_UNITS >= 34 * 1024, "far plane: past the corridor's 34 sectors",
		ZFAR_UNITS, 34 * 1024);

	// InitWindow shifts it by W2V_SHIFT (14) into a long.
	ok(((double)ZFAR_UNITS * 16384.0) < 2147483647.0,
		"far plane: shifted by 14 it fits a long",
		(long)(((double)ZFAR_UNITS * 16384.0) / 1024.0), 2097151);

	/* The curve, bit for bit the expression on f_mzfar 20480: SetupZRange
	 * calls ZDepthCurve. */
	float a = 0.0f, b = 0.0f;
	const float n = 20.0f, s = 20480.0f;
	float oldB = s * n * 0.99F / (n - s);
	const float oldA = 0.005F - oldB / n;

	oldB = -oldB;
	ZDepthCurve(n, &a, &b);
	ok(a == oldA && b == oldB,
		"depth curve: f_a and f_b of the 20480 expression, bit for bit",
		(long)(a * 1.0e7f), (long)(oldA * 1.0e7f));
	ok(fabs(ZDepthAt((double)ZFAR_SHIPPED) - 0.995) < 1.0e-6,
		"depth curve: 0.995 at 20,480 units",
		(long)(ZDepthAt((double)ZFAR_SHIPPED) * 1.0e6), 995000);

	/* The sky's slice from 0.996 (HD_SKY_MINZ, hd_render.cpp): the far plane
	 * and any distance stay under it. */
	ok(ZDepthAt((double)ZFAR_UNITS) < 0.996,
		"depth curve: the far plane under the sky's slice",
		(long)(ZDepthAt((double)ZFAR_UNITS) * 1.0e6), 996000);
	ok((double)a < 0.996, "depth curve: any distance under the sky's slice",
		(long)((double)a * 1.0e6), 996000);

	/* The 24-bit step in hundredths of a unit: 0.30 at 10,000, 2.71 at
	 * 30,000, 5.30 at the far plane. */
	ok((long)(ZDepthStep(10000.0, 24) * 100.0 + 0.5) == 30,
		"depth step: 0.30 units at 10,000",
		(long)(ZDepthStep(10000.0, 24) * 100.0 + 0.5), 30);
	ok((long)(ZDepthStep(30000.0, 24) * 100.0 + 0.5) == 271,
		"depth step: 2.71 units at 30,000",
		(long)(ZDepthStep(30000.0, 24) * 100.0 + 0.5), 271);
	ok((long)(ZDepthStep((double)ZFAR_UNITS, 24) * 100.0 + 0.5) == 530,
		"depth step: 5.30 units at the far plane",
		(long)(ZDepthStep((double)ZFAR_UNITS, 24) * 100.0 + 0.5), 530);

	/* The rain's tint: DoRain's expression with f_mzfar 20480, over every
	 * eighth unit from the eye to past the far plane, both scales. */
	long differ = 0;

	for (int i = 0; i <= 8 * 45000; i++)
	{
		const float mz = (float)i * 0.125f;

		for (int k = 0; k < 2; k++)
		{
			const float scale = k ? 16.0F : 8.0F;
			const long was =
				long((1.0F - (s - mz) * (1.0F / s)) * scale + scale);

			differ += ZRainTint(mz, scale) != was;
		}
	}

	ok(differ == 0, "rain tint: the 20480 expression at every depth",
		differ, 0);
	ok(ZRainTint(0.0f, 8.0F) == 8 && ZRainTint(20480.0f, 8.0F) == 16,
		"rain tint: 8 at the eye, 16 at 20,480 units",
		ZRainTint(20480.0f, 8.0F), 16);
	ok(ZRainTint(10240.0f, 16.0F) == 24, "rain tint: the head's 24 halfway",
		ZRainTint(10240.0f, 16.0F), 24);
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
	key_stick();
	cam_steer();
	firing_run();
	weapons();
	mode_latch();
	grab_family();
	grab_latch();
	sweeps();
	camera_cut_latch();
	camera_cut_reaim();
	camera_orbit();
	camera_speed();
	camera_pitch();
	camera_wall();
	camera_hide();
	camera_seam();
	camera_stamp();
	camera_push_view();
	camera_lead();
	camera_camcut();
	camera_carry();
	camera_pull();
	camera_chord();
	camera_wall_clear();
	camera_flip();
	camera_mouse();
	hd_switch();
	hd_switch_option();
	hd_switch_wiring();
	glass_split();
	hd_build_gate();
	load_keep();
	load_mode();
	load_brake();
	ledge_hang();
	ledge_trigger();
	deferred();
	page_class();
	model_cut();
	stand_draw();
	stand_cover();
	vis_tables();
	vis_plane();
	vis_wall();
	vis_hidden_room();
	vis_static_rule();
	vis_enclosed_rule();
	vis_height_rule();
	vis_box_side();
	vis_enclosed_tables();
	vis_static_covers();
	stand_overlap();
	stand_part();
	stand_shadow();
	static_art();
	static_piece();
	mirror_pose();
	mirror_clip();
	gloss_order();
	gloss_parser();
	gloss_class();
	gloss_seed();
	gloss_field_grid();
	gloss_field_texels();
	gloss_field_build();
	gloss_field_bound();
	objlight_dark();
	softbody_spring();
	softbody_rate();
	softbody_bound();
	softbody_reset();
	softbody_rest();
	softbody_clock();
	softbody_region();
	softbody_mask();
	softbody_constants();
	shadow_key();
	shadow_pull();
	shadow_fixed();
	shadow_follow();
	shadow_follow_move();
	shadow_follow_lights();
	shadow_follow_both();
	shadow_follow_edges();
	shadow_box();
	shadow_gate();
	shadow_terms();
	shadow_lookup();
	shadow_receive();
	shadow_mode();
	shadow_others_box();
	shadow_others_margin();
	shadow_others_rule();
	shadow_others_gate();
	shadow_others_behind();
	shadow_others_lookup();
	shadow_others_factor();
	shadow_others_receive();
	shadow_others_fade();
	shadow_others_holds();
	shadow_takes();
	shadow_mirror_view();
	shadow_mirror_engine();
	shadow_walk();
	shadow_arrival();
	shadow_cut();
	spotcam_end();
	life_drift();
	life_spring();
	life_bank();
	life_spawn();
	life_ends();
	life_drops();
	life_rings();
	life_apart();
	life_wayon();
	life_keep();
	life_tank();
	life_flee();
	life_edge();
	life_roam();
	life_turn();
	life_bob();
	life_depth();
	life_fishdepth();
	life_conveyor();
	load_pool();
	splash_motion();
	splash_cards();
	splash_fade();
	splash_tank();
	splash_tank_burst();
	splash_tank_spray();
	splash_tank_dome();
	splash_tank_wet();
	splash_tank_slopes();
	splash_wet_clip();
	splash_rings();
	water_under();
	art_ripple();
	water_look();
	water_surface();
	sky_cap();
	water_deep();
	water_sea();
	wet_polish();
	spring_table();
	spring_file();
	spring_clock();
	spring_rule();
	spring_levels();
	level_key();
	spring_crack();
	spring_crack_gate();
	spring_column();
	marble_pages();
	marble_rules();
	far_plane();
	marble_places();
	marble_counts();
	marble_over();
	marble_slopes();
	debris_pieces();

	printf("\n%d checks, %d failed\n", g_checks, g_fail);

	return g_fail ? 1 : 0;
}

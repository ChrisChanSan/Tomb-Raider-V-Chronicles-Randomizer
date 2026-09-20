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

/* To the nearest, away from zero at a half - so the scaling below is ODD, and
 * a stick pushed the other way reads the same distance the other way instead
 * of picking up a bias from the sign of its own components. */
static long ModernRound(double v)
{
	return (v < 0.0) ? -(long)floor(-v + 0.5) : (long)floor(v + 0.5);
}

/* THE RADIAL DEAD ZONE - the one piece of stick arithmetic both sticks share
 * (#161, round 161a2b, item 6; the numbers and what they are for are in
 * game\modern_move.h beside MODERN_DEAD_LEFT).
 *
 * Pure on purpose. What it has to promise is a promise about ANGLES - the
 * direction the thumb holds is the direction that comes out - and no play
 * test can read an angle off a screen, so the promise is kept in ModernTest's
 * `deadzone` group instead.
 *
 * Four rules, in the order the code takes them:
 *   inside the SQUARE the hardware's own per-axis zone held, nothing at all
 *                     (`axis_zone`, round 161a2b's fix round - the header
 *                     beside MODERN_DEAD_AXIS_LEFT says why a disc alone let a
 *                     resting pad through in that square's corners);
 *   inside the zone   nothing at all, so a stick at rest cannot creep;
 *   past its rim      the length starts again at nothing and eases up to full
 *                     at the stick's own rim, so crossing it is not a step;
 *   in every case     the output is the INPUT VECTOR scaled, so the angle
 *                     survives to whatever the integer grid can hold.
 *
 * The clamp at the end is the one ModernMagnitude already makes and for the
 * same reason: a diagonal push reads about 181 in our units, because the two
 * axes are gated as a square and not as a circle, and a rate 1.4 times bigger
 * on the diagonal is a camera that turns faster while it also tilts. Clamping
 * the LENGTH and keeping the direction makes the stick round, which is what
 * every caller already takes it to be.
 *
 * `axis_zone` 0 is no square at all: what a caller with no per-axis hardware
 * zone behind it passes, and what the tests of the radial rules alone pass.
 *
 * Returns that length, 0..MODERN_STICK_FULL. */
long ModernStickDeadZone(long zone, long axis_zone, long in_x, long in_y, long* out_x, long* out_y)
{
	const double full = (double)MODERN_STICK_FULL;
	double mag, want;

	*out_x = 0;
	*out_y = 0;

	if (zone < 0)
		zone = 0;

	/* THE HARDWARE'S OWN SQUARE, AND IT IS ASKED FIRST because in the corners it
	 * reaches further than the disc does: both components inside it is exactly
	 * the pair specific\dxshell.cpp used to zero outright, and nothing that was
	 * dead at rest before this round may come alive now. It costs the axis snap
	 * nothing - a snapped push is one the per-axis zone let THROUGH, so it
	 * always has a component outside the square and never meets this gate. */
	if (axis_zone > 0 && ModernAbs(in_x) <= axis_zone && ModernAbs(in_y) <= axis_zone)
		return 0;

	// A zone at or past the rim is a dead stick and would divide by nothing:
	// answered here rather than by the arithmetic below.
	if (zone >= MODERN_STICK_FULL)
		return 0;

	mag = sqrt((double)(in_x * in_x + in_y * in_y));

	if (mag <= (double)zone)
		return 0;

	want = full * (mag - (double)zone) / (full - (double)zone);

	if (want > full)
		want = full;

	*out_x = ModernRound((double)in_x * want / mag);
	*out_y = ModernRound((double)in_y * want / mag);

	return ModernRound(want);
}

/* THE PLAYER'S OWN SHARE OF THAT ZONE (#161, round 161b1, item 5) - "den wie im
 * Remaster toggelbar fuer den User hier einbinden".
 *
 * `base` is one of the eight tuned MODERN_DEAD_* widths (game\modern_move.h),
 * `user` is tomb5.dead_zone, 0..100, and the answer is the width the seam
 * really applies. The whole point of the formula is its MIDDLE:
 *
 *   (base * 50 + 25) / 50 = base + 25/50 = base, exactly, for every base,
 *
 * because integer division throws the remainder away. So the default setting is
 * not "close to" round 161a3's tuning, it IS that tuning, and a player who never
 * touches the row plays the engine the play tests were run on. The +25 is the
 * half-divisor that makes the rest of the bar round to nearest rather than
 * always down: 31 at 40 per cent is 24.8, which answers 25 and not 24.
 *
 * 0 answers 0 for every base - no zone at all - and 100 answers twice the base.
 * The function is monotone in `user` because the numerator is, and it is pure
 * for the same reason ModernStickDeadZone above is: ModernTest is where the
 * three promises are kept, not a play test.
 *
 * Both arguments are guarded rather than trusted. `user` arrives from the
 * registry, which a hand can edit (tomb5\tomb5.cpp clamps it too, but the
 * clamp belongs where the value is USED as well as where it is read), and a
 * negative `base` has no meaning as a width - ModernStickDeadZone reads 0 and
 * below as "no zone", so that is what a caller gets back here. */
long ModernDeadZoneUser(long base, long user)
{
	if (base <= 0)
		return 0;

	if (user < 0)
		user = 0;

	if (user > 100)
		user = 100;

	return (base * user + 25) / 50;
}

/* THE LOOK KEY'S TANK, READ BY SECTOR (#161, round 161a3b, item 1).
 *
 * On the bypass ticks - and on no others - the stick is handed to the CLASSIC
 * assembly in specific\input.cpp, which reads it as two independent per-axis
 * thresholds of 8. That is Core's own arithmetic and it is right behind the
 * hardware's per-axis zone; behind the modern RADIAL zone it is a window of
 * +-3.6 degrees round the exact horizontal, and M1 measured what a thumb does
 * with that: 680 of 962 pushed tank ticks came out LEFT+BACK, because the
 * natural arc of a thumb pushing "left" sits ten to sixteen degrees low.
 * lara_as_stop lets the back bit overwrite the turn goal, so the user's own
 * "ich kann nur mit einem genauen Winkel die Drehung starten ... und sie macht
 * den Schritt zurueck" is that window and nothing about the camera.
 *
 * So exactly one axis comes out of here. The decision is made on the stick's
 * ANGLE, which is the number the thumb actually controls, and the value handed
 * on is the stick's own MAGNITUDE on the surviving axis - not its component,
 * which would shrink towards the sector's edge and could fall back under the
 * classic 8 that this function exists to get past.
 *
 * WHY THE PREVIOUS SECTOR IS AN ARGUMENT AND NOT A STATIC OF THIS FILE: it is
 * one thumb's history on one pad, and the file it lives in is the one that
 * knows when the bypass began and ended (specific\input.cpp clears it there).
 * Keeping it here would make this function a state machine that every test
 * would have to reset, for a memory of exactly one tick.
 *
 * Returns the sector it chose, MODERN_TANK_SECTOR_*, which is MODERNLOG's own
 * `sector=`. 0/0 in is 0/0 out and no sector: the radial dead zone one step
 * earlier is what makes a resting stick that pair, and this function must not
 * invent a direction for it. */
long ModernTankSector(long in_x, long in_y, long prev_sector, long* out_x, long* out_y)
{
	long bound = MODERN_TANK_TURN_SECTOR;
	long heading;
	long off_h;
	long mag;

	if (!out_x || !out_y)
		return MODERN_TANK_SECTOR_NONE;

	*out_x = 0;
	*out_y = 0;

	if (!in_x && !in_y)
		return MODERN_TANK_SECTOR_NONE;

	/* The stick's own heading, in the convention the whole of this file uses:
	 * 0 is straight forward, +0x4000 is right, and stick_y is forward-negative
	 * (specific\dxshell.cpp), which is why it goes in negated - exactly as
	 * ModernTranslate takes it. */
	heading = ModernAngle(in_x, -in_y);

	/* ... and how far that is from the HORIZONTAL, which is the axis this
	 * decision is about: 0x4000 either side of forward. Folding the sign away
	 * first makes left and right one case, which they are. */
	off_h = ModernAbs(ModernAbs(heading) - 0x4000L);

	/* THE HYSTERESIS, AND IT ONLY EVER WIDENS THE SECTOR THE THUMB IS ALREADY
	 * IN. A thumb resting ON the boundary crosses it with noise otherwise, and
	 * at 30 ticks a second that is the turn and the step alternating in the
	 * hand - the one failure this item exists to avoid repeating in another
	 * shape. With no previous sector (the first tick of a hold) the plain
	 * boundary answers, so a fresh push is never biased. */
	if (prev_sector == MODERN_TANK_SECTOR_TURN)
		bound += MODERN_TANK_SECTOR_HYST;
	else if (prev_sector == MODERN_TANK_SECTOR_STEP)
		bound -= MODERN_TANK_SECTOR_HYST;

	mag = ModernRound(sqrt((double)(in_x * in_x + in_y * in_y)));

	if (off_h <= bound)
	{
		*out_x = (in_x < 0) ? -mag : mag;
		return MODERN_TANK_SECTOR_TURN;
	}

	*out_y = (in_y < 0) ? -mag : mag;
	return MODERN_TANK_SECTOR_STEP;
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
	cfg->snap_forward = MODERN_SNAP_FORWARD;
	cfg->snap_forward_stand = MODERN_SNAP_FORWARD_STAND;
	cfg->stand_forward_ticks = MODERN_STAND_FORWARD_TICKS;
	cfg->air_turn_rate = MODERN_AIR_TURN_RATE;
	cfg->stick_full = MODERN_STICK_FULL;
	cfg->swim_turn_rate = MODERN_SWIM_TURN_RATE;
	cfg->surf_turn_rate = MODERN_SURF_TURN_RATE;
	cfg->surf_side_rate = MODERN_SURF_SIDE_RATE;
	cfg->swim_pitch_rate = MODERN_SWIM_PITCH_RATE;
	cfg->swim_pitch_min = MODERN_SWIM_PITCH_MIN;
	cfg->swim_pitch_max = MODERN_SWIM_PITCH_MAX;
	cfg->swim_roll_rate = MODERN_SWIM_ROLL_RATE;
	cfg->water_auto_stroke = MODERN_WATER_AUTO_STROKE;
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
	state->last_course = 0;
	state->have_course = 0;
	state->last_turn = 0;
	state->stand_hold = 0;
}

/* THE STATES THAT STAY CLASSIC (round 161a step 3).
 *
 * Design sections 1.7 and 6.1 of MODERN-CONTROLS-DESIGN.md, checked state by
 * state against game\lara_states.h and against the routine each one runs in
 * game\lara.cpp:40-181 (lara_control_routines[]). TROne's own list stops at
 * 104; TR5 runs to 138, and section 6.1 measured what the extra 34 states do.
 * The rule in one line: a direction bit stays classic wherever the routine
 * reads it as something other than "where do I want to go" - which way to
 * shimmy, whether to let go of a rope, which way to push a block - and
 * everything else (standing, running, walking, the jumps, ducking, crawling)
 * gets the translation, which is why the switch below lists the classic
 * states one by one and lets `default` answer modern for the rest.
 *
 * THE WATER STATES USED TO STAND HERE WITH "modern in A2 (camera pitch)"
 * AGAINST THEM, AND ROUND 161a2 STEP 4 TOOK THEM OUT. A1 had no camera pitch
 * to swim by; the orbit is there now, so the eight states the player really
 * steers in the water - 13/17/18 under it and 33/34/47/48/49 on it - are
 * translated like any other, and ModernWaterMove below is what they get
 * instead of ModernDirectTurn. The six that LOOK like water and are still
 * listed as classic are named where they sit: the three dives and the
 * climb-out are the handover and not the swim, the underwater roll is a fixed
 * maneuver and the underwater death is not steered at all. DOZY's flight
 * (118) makes seven and sits with TR5's own later states, where it reads the
 * direction bits as a direction to fly.
 *
 * ONE DIVERGENCE FROM THIS ROUND'S OWN BUILD BRIEF, MEASURED AND REPORTED IN
 * THE STEP 3 RECORD: the brief's step 3 paragraph lists 25-29 as modern, but
 * design section 1.7 lists TROne's 25/26/27 ("back flip and side flips",
 * added after THEY measured the translation turning Lara onto the stick
 * heading mid-flip) as classic, and ModernTest.cpp already carried a second,
 * independent TROne expectation saying the same thing
 * (modern.shared_state_numbers: "is_classic 10/17/25/26/27/36/57/76"). Rather
 * than trust either text over the other, I read the routines themselves:
 * lara_as_leftjump (:765) sets AS_TWIST when IN_RIGHT is held, lara_as_
 * rightjump (:781) sets it the same way on IN_LEFT, and lara_as_backjump
 * (:799) reads IN_FORWARD | IN_ROLL to end the flip early - exactly the
 * mid-flip redirect TROne's own document says 25/26/27 were added to stop.
 * So 25/26/27 are classic here, matching TROne and not the brief's summary
 * sentence. 28 and 29 (lara_as_upjump, lara_as_fallback) read no direction
 * bit at all and stay modern as the brief says. */
long ModernStateIsClassic(short anim_state)
{
	switch (anim_state)
	{
	/* THE WATER'S OWN HANDOVERS, which look like swimming and are not
	 * steered like it. Round 161a2 step 4 took 13/17/18 and 33/34/47/48/49
	 * out of this list; these four and the two below them stayed, and each
	 * one has a reason of its own.
	 *
	 * 52/53/55 are the entry and the way out: a dive is how she gets INTO
	 * the water and the climb-out is how she leaves it, so both belong to
	 * the handover rather than to the swim. (They are OUR reason and not
	 * TROne's - the second review pass of round 161a measured that their
	 * trone_modern_state_is_classic has 52, 53 and 55 in neither the list
	 * nor the prose above it.) 35 joins them for the same reason and for a
	 * measured one on top: lara_as_dive reads IN_FORWARD as "steepen the
	 * dive" (laraswim.cpp:468), which is a pitch and not a direction, and
	 * the animation is over in a handful of ticks. */
	case 35:	// AS_DIVE, lara_as_dive - the entry, not the swim
	case 52:	// AS_SWANDIVE, lara_as_swandive - the entry, not the swim
	case 53:	// AS_FASTDIVE, lara_as_fastdive - the entry, not the swim
	case 55:	// AS_WATEROUT, lara_as_waterout - the way out, not the swim
		return 1;

	/* The underwater 180 and the underwater death. The roll is a short fixed
	 * maneuver on its own animation, not the held swim/glide loop the orbit
	 * steers; the death is not steered by anybody (the seam's own
	 * hit_points test in game\modern_state.cpp says the same thing a tick
	 * earlier, and this is the state's half of it). */
	case 44:	// AS_UWDEATH, lara_as_uwdeath - her last pose is the animation's
	case 66:	// AS_WATERROLL, lara_as_waterroll
		return 1;

	/* Hang and shimmy: the direction bits pick which way along the ledge,
	 * not where to run. 87/88 are the transitions into and out of it. */
	case 10:	// AS_HANG, lara_as_hang
	case 30:	// AS_HANGLEFT, lara_as_hangleft
	case 31:	// AS_HANGRIGHT, lara_as_hangright
	case 82:	// AS_HANGTURNL, lara_as_hangturnl
	case 83:	// AS_HANGTURNR, lara_as_hangturnr
	case 87:	// AS_HANG2DUCK, lara_as_null - hang-to-duck transition
	case 88:	// AS_CRAWL2HANG, lara_as_null - crawl-to-hang transition
		return 1;

	/* The overhead hang and the monkey swing (1.7's "75-79") - the same
	 * family as the one above, at arm's reach instead of chest height. */
	case 75:	// AS_HANG2, lara_as_hang2
	case 76:	// AS_MONKEYSWING, lara_as_monkeyswing
	case 77:	// AS_MONKEYL, lara_as_monkeyl
	case 78:	// AS_MONKEYR, lara_as_monkeyr
	case 79:	// AS_MONKEY180, lara_as_monkey180
		return 1;

	/* The back flip and the two side flips out of a standing jump. See the
	 * divergence note above the function. */
	case 25:	// AS_BACKJUMP, lara_as_backjump - reads IN_FORWARD | IN_ROLL
	case 26:	// AS_RIGHTJUMP, lara_as_rightjump - reads IN_LEFT into AS_TWIST
	case 27:	// AS_LEFTJUMP, lara_as_leftjump - reads IN_RIGHT into AS_TWIST
		return 1;

	/* Pushing and pulling a block, and the ready stance before a pole: the
	 * bits mean push/pull or which side to mount, not where to run. */
	case 36:	// AS_PUSHBLOCK, lara_as_pushblock
	case 37:	// AS_PULLBLOCK, lara_as_pullblock
	case 38:	// AS_PPREADY, lara_as_ppready
		return 1;

	/* The ladder. */
	case 56:	// AS_CLIMBSTNC, lara_as_climbstnc
	case 57:	// AS_CLIMBING, lara_as_climbing
	case 58:	// AS_CLIMBLEFT, lara_as_climbleft
	case 59:	// AS_CLIMBEND, lara_as_climbend
	case 60:	// AS_CLIMBRIGHT, lara_as_climbright
	case 61:	// AS_CLIMBDOWN, lara_as_climbdown
		return 1;

	/* Section 6.1: during these Lara is Busy and the input mask already
	 * strips everything but the four direction bits, so the choice is
	 * harmless either way - named here so nobody "fixes" a gap that was
	 * never a bug. 98 shares AS_PICKUP's own routine. */
	case 39:	// AS_PICKUP, lara_as_pickup - Busy
	case 40:	// AS_SWITCHON, lara_as_switchon - Busy
	case 41:	// AS_SWITCHOFF, lara_as_switchoff - Busy
	case 42:	// AS_USEKEY, lara_as_usekey - Busy
	case 43:	// AS_USEPUZZLE, lara_as_usepuzzle - Busy
	case 50:	// AS_USEMIDAS, lara_void_func - Busy
	case 51:	// AS_DIEMIDAS, lara_void_func - Busy
	case 67:	// AS_FLAREPICKUP, lara_as_pickupflare - Busy
	case 70:	// AS_DEATHSLIDE, lara_as_deathslide - reads IN_ACTION only, Busy
		return 1;

	/* TROne's "89-104": rope-swing entry, the trap and switch objects a
	 * scripted anim drives, and the pole - the same reasoning as the two
	 * groups above it. */
	case 89:	// AS_CONTROLLED, lara_as_controlled
	case 90:	// AS_ROPELEFT, lara_as_ropel
	case 91:	// AS_ROPERIGHT, lara_as_roper
	case 92:	// AS_BLOCKSWITCH, lara_as_controlled
	case 93:	// AS_LIFTTRAP, lara_as_controlled
	case 94:	// AS_PULLTRAP, lara_as_controlled
	case 95:	// AS_TURNSWITCH, lara_as_controlledl
	case 96:	// AS_COGSWITCH, lara_as_controlledl
	case 97:	// AS_RAILSWITCH, lara_as_controlled
	case 98:	// AS_HIDDENPICKUP, lara_as_pickup - Busy, same routine as 39
	case 99:	// AS_POLESTAT, lara_as_null
	case 100:	// AS_POLEUP, lara_as_null
	case 101:	// AS_POLEDOWN, lara_as_null
	case 102:	// AS_POLELEFT, lara_as_poleleft
	case 103:	// AS_POLERIGHT, lara_as_poleright
	case 104:	// AS_PULLEY, lara_as_pulley
		return 1;

	/* Section 6.1 - everything TR5 added past TROne's own 104, measured one
	 * by one. 105/106 are the exception, not listed here: a crouch turn,
	 * where the direction bits mean "which way" exactly as they do standing,
	 * so they fall through to `default` and come out modern. */
	case 107:	// AS_CORNEREXTL, lara_as_extcornerl - corner shimmy, family of 30/31/82/83
	case 108:	// AS_CORNEREXTR, lara_as_extcornerr
	case 109:	// AS_CORNERINTL, lara_as_intcornerl
	case 110:	// AS_CORNERINTR, lara_as_intcornerr
	case 111:	// AS_ROPE, lara_as_rope - rope swing
	case 112:	// AS_CLIMBROPE, lara_as_climbrope
	case 113:	// AS_SLIDEROPE, lara_as_climbroped
	case 114:	// AS_ROPEFWD, lara_as_rope
	case 115:	// AS_ROPEBACK, lara_as_rope
	case 116:	// AS_MOVE, lara_void_func - scripted
	case 117:	// AS_TWODOOR, lara_as_controlled - scripted
	case 118:	// AS_SWIMCHEAT, lara_as_swimcheat - DOZY flight reads the bits as a direction to fly
	case 119:	// AS_TROPEPOSE, lara_as_trpose - tightrope
	case 120:	// AS_TROPETURN, lara_as_null
	case 121:	// AS_TROPEWALK, lara_as_trwalk
	case 122:	// AS_TROPEFALL_L, lara_as_trfall
	case 123:	// AS_TROPEFALL_R, lara_as_trfall
	case 124:	// AS_TROPEGETON, lara_as_null
	case 125:	// AS_TROPEGETOFF, lara_as_null
	case 126:	// AS_CROWDOVE, lara_as_switchon - tightrope family (6.1's "119-127")
	case 127:	// AS_TROPEUNDOFALL, lara_as_null
	case 128:	// AS_PBSPIN, lara_as_parallelbars - horizontal bar
	case 129:	// AS_PBLEAP, lara_as_pbleapoff
	case 130:	// AS_INTO_ZIP, lara_as_null - zip line
	case 131:	// AS_ZIP, lara_as_null
	case 132:	// AS_OUT_ZIP, lara_as_null
	case 133:	// AS_USECARD, lara_as_null - scripted object states
	case 134:	// AS_LIFT_S, lara_as_null
	case 135:	// AS_AIR, lara_as_null
	case 136:	// AS_SUB_CROW, lara_as_null
	case 137:	// AS_BOX_UP, lara_as_null
	case 138:	// AS_CLIMB2DUCK, lara_as_null - ladder to crouch
		return 1;

	/* Everything else gets the translation: standing, running, walking, the
	 * jumps other than the three flips above (3, 28, 29), turning, falling,
	 * ducking (71/72/105/106), crawling (80/81/84-86), stepping (21/22) and a
	 * handful of void/unused slots (4, 8, 19, 23, 46, 54, 62-64, 68, 69)
	 * whose routines read no input either way. */
	default:
		return 0;
	}
}

/* UNDER THE WATER OR ON IT (round 161a2 step 4). The three states in which
 * she is really swimming: 13 AS_TREAD, 17 AS_SWIM, 18 AS_GLIDE, all three
 * running SwimTurn out of game\laraswim.cpp. They are the ones where the
 * direction bits mean a PITCH and a turn rather than a direction, which is
 * why the water's own move clears them and drives x_rot instead - and they
 * are the ones where the stroke is the jump key. The five surface states are
 * the other half of the water and behave like the land: forward is forward.
 */
long ModernStateIsSwim(short anim_state)
{
	return (anim_state == 13 || anim_state == 17 || anim_state == 18) ? 1 : 0;
}

/* THE WATER'S TURN TABLE, and the predicate that says "this tick is the
 * water's" at the same time: 0 means no water state, anything else is the
 * rate that state turns at in TR5 itself (see the constants in the header for
 * where each number was read out of). One function rather than a bool and a
 * rate, because the seam asks both questions on the same tick and a state
 * that can be steered without a rate does not exist. */
long ModernWaterTurnRate(const MODERN_CFG* cfg, short anim_state)
{
	MODERN_CFG spare_cfg;

	if (!cfg)
	{
		ModernDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	switch (anim_state)
	{
	case 13:	// AS_TREAD, lara_as_tread
	case 17:	// AS_SWIM, lara_as_swim
	case 18:	// AS_GLIDE, lara_as_glide
		return cfg->swim_turn_rate;

	case 33:	// AS_SURFTREAD, lara_as_surftread
	case 34:	// AS_SURFSWIM, lara_as_surfswim
		return cfg->surf_turn_rate;

	case 47:	// AS_SURFBACK, lara_as_surfback
	case 48:	// AS_SURFLEFT, lara_as_surfleft
	case 49:	// AS_SURFRIGHT, lara_as_surfright
		return cfg->surf_side_rate;

	default:
		return 0;
	}
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

/* IS SHE ALREADY RUNNING (round 161a2b, #161 item 7a)? Two states and no more:
 * 1 AS_RUN and 73 AS_DASH, the sprint - the two the direct turn is allowed in
 * where her feet are already carrying her forward at speed.
 *
 * It exists for one rule and one only: there, the direct turn stops withholding
 * IN_FORWARD while the turn finishes. The user played the withholding as a
 * stutter - "sie macht ihre Stumble-Anim, als wuerde ich den Stick nur einmal
 * fuer einen Schritt von Lara nach vorne druecken" - and the log said why: with
 * the stick circling faster than she turns, the error stands over snap_forward
 * for single ticks, the bit drops out, and game\lara.cpp's lara_as_run sees no
 * forward and goes to AS_STOP for that one tick (203 of 937 running ticks in
 * play test 2, 25 visible drops). While she is RUNNING the argument the
 * withholding was built on does not hold anyway: the step it is trying to
 * prevent is a step she is taking regardless, and the turn happens under it.
 * 0 AS_WALK is deliberately NOT here - a walk is slow enough to place a foot
 * wrong - and neither is any standing state, where the withholding is exactly
 * right and keeps its own comment in ModernDirectTurn. */
long ModernStateIsRunning(short anim_state)
{
	return (anim_state == 1 || anim_state == 73) ? 1 : 0;
}

/* IS SHE TURNING ON THE SPOT (round 161a3, #161 item 2c)? The four states in
 * which the classic automaton's own turn-on-the-spot lives and nothing is
 * carrying her anywhere: 2 AS_STOP, 6 AS_TURN_R, 7 AS_TURN_L, 20 AS_FASTTURN.
 *
 * They are the four that get the WIDER IN_FORWARD gate (snap_forward_stand).
 * M4 measured why: 232 ticks in AS_STOP stepping 18 degrees a tick with no
 * direction bit at all, because a circling stick kept more than 45 degrees
 * between her and the bit that starts the run - "dreht sie sich zwar 360 Grad
 * im Kreis, aber ohne ihre echte Animation". With 90 degrees she is running
 * within a few ticks of any reversal, and the run animation is what turns.
 *
 * IT IS NOT ModernStateIsGroundIdle, which also holds 12 AS_SPLAT and 14
 * AS_LAND: those two are a wall and a landing, one or two ticks each with her
 * weight still being caught, and the narrow 45 degrees is right there - the
 * step she would take is a step out of a pose she has not finished. Nor is it
 * ModernStateIsStanding (2/14/15), which answers a different question, "can
 * she jump from here". Three predicates, three questions. */
long ModernStateIsSpotTurn(short anim_state)
{
	return (anim_state == 2 || anim_state == 6 || anim_state == 7 ||
		anim_state == 20) ? 1 : 0;
}

/* WHICH AIRBORNE STATE SHE MAY STEER IN (round 161a3, #161 item 4). One: 3
 * AS_FORWARDJUMP, because it is the one the classic engine itself steers -
 * lara_as_forwardjump is the only routine in game\lara.cpp that reads IN_LEFT
 * or IN_RIGHT while she is off the ground (see MODERN_AIR_TURN_RATE in the
 * header for the measurement, routine by routine, and for why the reach, the
 * dives and the falls are deliberately not here). Widening what the engine
 * already does is item 4; giving her a new ability in a state that never had
 * one is not. */
long ModernStateAllowsAirTurn(short anim_state)
{
	return (anim_state == 3) ? 1 : 0;	// AS_FORWARDJUMP, lara_as_forwardjump
}

/* THE DIRECT TURN, THE ARITHMETIC HALF (design section 1.4 step 6, and the
 * user's decision 5). Everything below is numbers; the one line that writes
 * lara_item->pos.y_rot is game\modern_state.cpp.
 *
 * WHY IT EXISTS AT ALL. The rest of the scheme sets a turn BIT and lets the
 * automaton turn her, which is what makes the whole idea survive a 1:1
 * engine. The automaton's fastest rate is 1456 units a tick, and a half turn
 * on the spot therefore costs about 23 ticks even once AS_FASTTURN is
 * reached. That is the one place where "set the bit and wait" does not feel
 * like the Remaster, so here - and only here - we turn her ourselves.
 *
 * WHICH STATES. TROne allow the write in 0/1/2/6/7/12/14/20/73 and nowhere
 * else: standing, walking, running, the three turns on the spot, the walk
 * into a wall, the landing and the sprint. Never airborne, never on a ladder
 * or a rope, never in the water, never in an interaction pose - in all of
 * those y_rot is the animation's to own, and an outside hand on it is how a
 * pose goes crooked. The numbers below are OUR state list's, checked one by
 * one against game\lara_states.h and the routine each runs:
 *   0 AS_WALK   1 AS_RUN   2 AS_STOP   6 AS_TURN_R   7 AS_TURN_L
 *  12 AS_SPLAT  14 AS_LAND 20 AS_FASTTURN          73 AS_DASH
 * (The first pass of this round wrote 73 as "AS_SPRINT, lara_as_sprint" and
 * 14 as "lara_as_land"; the review's own parse of the two files caught both.
 * TR5 calls the sprint AS_DASH / lara_as_dash, and AS_LAND has no routine of
 * its own at all - lara_control_routines[14] is lara_void_func. Neither
 * changed a classification, and the names are right here now because the
 * comment IS the evidence that the nine were checked.)
 *
 * THE TENTH, ADDED IN ROUND 161a-fix (#161 item 3). TROne's own list stops at
 * these nine ground states and never reaches 15 AS_COMPRESS - in their design
 * the crouch before a standing jump always resolves as a jump, never as
 * running, so there was nothing there for the direct turn to do. Ours can now
 * reach AS_COMPRESS with mode RUN as well: unarmed, ModernTranslate's rule 4
 * above steps aside for rule 6 (the user's decision - no weapon, no flip,
 * just the ordinary turn-and-run), so she can still be turning when the
 * crouch begins, and letting the write continue through it keeps her
 * aligning to the stick into the takeoff instead of freezing her heading for
 * the two or three ticks the crouch lasts. This does NOT reopen the door for
 * an armed flip: ModernDirectTurn below already refuses anything whose mode
 * is not MODERN_MODE_RUN, and AS_COMPRESS is mode JUMP whenever a weapon is
 * drawn (rule 4), so the two cases never overlap. */
long ModernStateAllowsDirectTurn(short anim_state)
{
	switch (anim_state)
	{
	case 0:		// AS_WALK, lara_as_walk
	case 1:		// AS_RUN, lara_as_run
	case 2:		// AS_STOP, lara_as_stop
	case 6:		// AS_TURN_R, lara_as_turn_r
	case 7:		// AS_TURN_L, lara_as_turn_l
	case 12:	// AS_SPLAT, lara_as_splat - she walked into a wall and is still on her feet
	case 14:	// AS_LAND, lara_void_func - the landing pose has no routine of its own
	case 15:	// AS_COMPRESS, lara_as_compress - only reached with mode RUN, i.e. unarmed
	case 20:	// AS_FASTTURN, lara_as_fastturn
	case 73:	// AS_DASH, lara_as_dash - the sprint
		return 1;

	default:
		return 0;
	}
}

/* HOW FAR AN ANGLE MAY MOVE THIS TICK: the error, folded the short way round
 * and clamped to the rate. Both turns in this file are that one sentence -
 * the land's ModernGroundTurn just below and the water's ModernWaterMove
 * further down - so it is written once here and they differ only in which
 * states they allow and which rate they hand it (round 161a2 step 4; the
 * arithmetic is A1's, moved and not changed).
 *
 * The rate is clamped to half a turn as well. Nothing sets it that high, but
 * a rate over 0x8000 would make "clamp the error to the rate" meaningless
 * and the clamp costs one comparison. */
static long ModernStepTowardBiased(long delta, long rate, long bias)
{
	delta = ModernWrap(delta);

	if (rate > 0x4000L)
		rate = 0x4000L;

	/* EXACTLY HALF A TURN IS THE SAME ANGLE EITHER WAY ROUND (round 161a3,
	 * item 2b), and ModernWrap has to answer one of them: it answers -32768,
	 * because that is the only one a short can hold. Left is therefore what
	 * every exact reversal would take by arithmetic accident rather than by a
	 * decision - so the caller's bias, which is the turn's own last direction
	 * (MODERN_STATE::last_turn), gets the casting vote here and the reversal
	 * stays monotone. With no bias at all the arithmetic keeps its answer,
	 * which is a fixed side and is all a tie needs. */
	if (delta == -0x8000L && bias > 0)
		return rate;

	if (delta > rate)
		return rate;

	if (delta < -rate)
		return -rate;

	return delta;
}

static long ModernStepToward(long delta, long rate)
{
	return ModernStepTowardBiased(delta, rate, 0);
}

/* How far she may turn this tick: the step above, and zero wherever the write
 * is not allowed. `rate` 0 gives 0 everywhere, which is the whole feature
 * switched off and the turn bits back in charge - that is what
 * MODERN_DIRECT_TURN is for and why the rate is a parameter and not a
 * constant read in here. */
long ModernGroundTurn(short anim_state, long delta, long rate)
{
	return ModernGroundTurnBiased(anim_state, delta, rate, 0);
}

/* The same, with the tie at exactly half a turn handed to a side (round 161a3,
 * item 2b - see ModernStepTowardBiased for why a tie exists at all). It is a
 * second function rather than a fourth argument on the first because
 * ModernGroundTurn is the shape TROne's own expectations are written in, and a
 * test that has to pass a bias it does not care about is a test about the
 * wrong thing. */
long ModernGroundTurnBiased(short anim_state, long delta, long rate, long bias)
{
	if (rate <= 0 || !ModernStateAllowsDirectTurn(anim_state))
		return 0;

	return ModernStepTowardBiased(delta, rate, bias);
}

/* THE DIRECT TURN'S WHOLE DECISION, in one place so that a test can drive it
 * exactly as the seam does. Returns 1 when it has taken charge of the turning
 * this tick - then `yaw_delta` is what the caller adds to y_rot, the caller
 * zeroes lara.turn_rate so the automaton does not turn her a second time, and
 * `out->bits` has been rewritten below. Returns 0 and a yaw_delta of 0
 * otherwise, and then `out` is untouched and the turn bits stand.
 *
 * WHY THE BITS ARE REWRITTEN. Once we turn her ourselves, a turn bit would
 * turn her twice and would also put her in AS_TURN_L/AS_TURN_R instead of
 * running. So all six direction bits go (IN_WALK stays - the stick still says
 * how fast she is going) and IN_FORWARD comes back only when 45 degrees or
 * less of the turn are left. That last part is not tidiness: with IN_FORWARD
 * set through the whole of a half turn she would take real steps sideways
 * across a ledge before the turn finished.
 *
 * ...AND SINCE ROUND 161a2b (#161 item 7a) NOT WHILE SHE IS RUNNING. See
 * ModernStateIsRunning above for the measurement: there the withheld bit was
 * costing her a one-tick AS_STOP every time the stick outran her turn, which
 * the player sees as a stumble. Every other allowed state keeps the rule, and
 * the four turn-on-the-spot states keep it at twice the width
 * (ModernStateIsSpotTurn, round 161a3 item 2c) plus a second way out that is
 * counted in ticks rather than degrees (MODERN_STAND_FORWARD_TICKS, the fix
 * pass of the same round), for the standing turn that arrives nowhere at all
 * because the thumb is circling at exactly her own rate.
 *
 * THE INSTANT ABOUT-TURN IS GONE (round 161a3, #161 item 2b), and this is
 * where it used to be: past 90 degrees of error, on a tick where the course
 * itself had jumped that far, the whole turn was taken in ONE tick. TROne
 * built it, round 161a2b put the course's own step in front of it so that a
 * circling stick could not trigger it by accumulation, and the second play
 * test then measured what was left and it was still the user's complaint - 36
 * one-tick steps over 30 degrees, 24 of them over 90, "es ist A unnatuerlich
 * und B selbst das Interpolating kommt nicht hinterher". His Remaster video
 * turns the same half circle over six ticks with the run animation playing.
 *
 * SO A REVERSAL IS NOW TURNED AT THE RATE LIKE EVERYTHING ELSE, which needed
 * exactly two things beside deleting the branch: the rate is the Remaster's
 * own 5461 (item 2a), so a half turn costs six ticks and not eighteen, and the
 * tie at exactly 180 degrees is decided rather than left to the wrap
 * (ModernStepTowardBiased and MODERN_STATE::last_turn). What the deletion
 * cannot take away is that a turn now takes TIME: the back flip out of a stand
 * is easier to reach than it was, not harder, because she no longer arrives
 * before the player has BACK and JUMP down - which is the case TROne built the
 * whole rule for in the first place. */
long ModernDirectTurn(const MODERN_CFG* cfg, MODERN_STATE* state, const MODERN_SITUATION* sit,
	MODERN_RESULT* out, long* yaw_delta)
{
	MODERN_CFG spare_cfg;
	long yaw;
	long remaining;
	long spot;
	long forward;
	long hold;

	if (yaw_delta)
		*yaw_delta = 0;

	/* THE STANDING STALL'S COUNTER BELONGS TO ONE UNBROKEN TURN (round 161a3,
	 * fix pass, MODERN_STAND_FORWARD_TICKS). It is taken out of the state here
	 * and written back only on the path that really turns her, so every early
	 * return below - the stick at rest, a classic tick, a state this turn does
	 * not own - ends the episode by simply not writing it again. A count left
	 * standing would otherwise hand IN_FORWARD to the FIRST tick of the next
	 * push, and that one tick is what the 90 degree gate exists for. With no
	 * state at all (a caller that passes none) the local starts at 0 every call
	 * and the rule can never fire, which is the same answer the tie-break gives
	 * such a caller. */
	hold = state ? state->stand_hold : 0;

	if (state)
		state->stand_hold = 0;

	if (!out || !out->active || out->mode != MODERN_MODE_RUN)
		return 0;

	if (!cfg)
	{
		ModernDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	/* No state number, no write: the state list above is the only thing
	 * standing between this and a crooked pose, so a caller that cannot say
	 * where Lara is does not get the direct turn. */
	if (!sit || sit->classic)
		return 0;

	/* THE AIMING GUARD THAT USED TO BE HERE IS GONE (round 161a2b, #161 item
	 * 8), and it is gone because it was wrong rather than because the user
	 * wanted it gone. It refused the write whenever `sit->drawn && sit->action`
	 * on the stated ground that "the game is already turning her onto her
	 * target - two hands on the same heading". The brief asked for that to be
	 * measured before anything moved, and the measurement says nothing of the
	 * sort happens: game\larafire.cpp, game\lara1gun.cpp and game\lara2gun.cpp
	 * never write lara_item->pos.y_rot at all. What aiming writes is
	 * lara.left_arm.y_rot, lara.right_arm.y_rot, lara.torso_y_rot and
	 * lara.head_y_rot - her arms, her chest and her head. Her HEADING moves
	 * only through lara.turn_rate, added in game\lara.cpp's LaraAboveWater, and
	 * that is the turn BITS' own path, which the direct turn replaces in
	 * exactly the same way armed or not. So there was never a second hand on
	 * the heading, and with aim_strafe now off (item 8) this guard would have
	 * left her unable to turn at all while the trigger is held. */

	if (!cfg->direct_turn || cfg->turn_rate <= 0)
		return 0;

	if (!ModernStateAllowsDirectTurn(sit->anim_state))
		return 0;

	yaw = ModernGroundTurnBiased(sit->anim_state, out->delta, cfg->turn_rate,
		state ? state->last_turn : 0);

	/* WRAPPED, and it has to be since the tie-break exists: with a delta of
	 * exactly -32768 taken to the RIGHT, delta minus yaw is 38229, which is
	 * not 150 degrees of anything - it is 210 degrees measured the long way
	 * round. Every other tick has yaw on delta's own side and the wrap costs
	 * nothing. */
	remaining = ModernAbs(ModernWrap((long)out->delta - yaw));

	out->bits &= ~(long)MODERN_DIR_MASK;

	spot = ModernStateIsSpotTurn(sit->anim_state);
	forward = ModernStateIsRunning(sit->anim_state) ||
		remaining <= (spot ? cfg->snap_forward_stand : cfg->snap_forward);

	/* AND THE SECOND QUESTION, ASKED IN TICKS (round 161a3, fix pass,
	 * MODERN_STAND_FORWARD_TICKS - the reviewer of this round confirmed the
	 * case the gate alone leaves standing). Only the turn-on-the-spot states
	 * count: everywhere else she is already moving, so a withheld IN_FORWARD
	 * costs her a step and not an animation, and a state that is not this
	 * turn's own falls through to AS_STOP within a tick or two and is counted
	 * there. The counter is sticky by construction - the tick after it opens,
	 * the gate is still shut, so it counts one higher and stays open until the
	 * run begins or the turn arrives. */
	if (spot && !forward)
	{
		hold++;

		if (state)
			state->stand_hold = hold;

		if (cfg->stand_forward_ticks > 0 && hold >= cfg->stand_forward_ticks)
			forward = 1;
	}

	if (forward)
		out->bits |= MODERN_IN_FORWARD;

	out->turning = (remaining > 0) ? 1 : 0;

	/* The spin is the automaton's way of turning on the spot and we have
	 * just taken the turning away from it. Leaving the flag set would keep
	 * IN_FORWARD off for ticks after the turn is done. */
	if (state)
	{
		state->spin = 0;
		state->last_turn = (yaw > 0) ? 1 : ((yaw < 0) ? -1 : 0);
	}

	if (yaw_delta)
		*yaw_delta = yaw;

	return 1;
}

/* THE SAME THING IN THE AIR (round 161a3, #161 item 4, the user's word 5).
 *
 * "Im Remaster kann sie auch nach dem Sprung in der Luft weiterhin nach links
 * und rechts lenken. Bei uns ist das noch wie in der Panzersteuerung sehr
 * starr. ... Lara kann dadurch einen viel weiteren Radius um eine Kante
 * springen."
 *
 * It is ModernDirectTurn's shape with three differences, and each of them is a
 * measurement rather than a taste:
 *   THE STATE LIST is one state long (ModernStateAllowsAirTurn), because the
 *					classic steers exactly one airborne state and item 4 widens
 *					what the engine does rather than inventing a new ability.
 *   THE BITS		only IN_LEFT and IN_RIGHT go. In the air the rest of the
 *					word is not a direction at all: lara_as_forwardjump reads
 *					IN_BACK as the twist, IN_WALK as the swan dive and
 *					IN_ACTION as the grab, and the user asked for none of them
 *					to change. IN_FORWARD it does not read at all, so leaving
 *					it costs nothing and keeps the bits the same shape the
 *					ground tick before the jump had.
 *   NO FORWARD		there is no step to withhold and nothing to withhold it
 *		GATE		from - her feet are not on anything.
 *
 * The gate is the rate: air_turn_rate 0 is the feature switched off and the
 * classic 1 degree a tick back in charge, which is how a play test bisects
 * "is the jump wrong, or is the steering wrong". Returns 1 when it has taken
 * the tick; then the caller adds `yaw_delta` to y_rot and zeroes
 * lara.turn_rate, exactly as it does on the ground. */
long ModernAirTurn(const MODERN_CFG* cfg, MODERN_STATE* state, const MODERN_SITUATION* sit,
	MODERN_RESULT* out, long* yaw_delta)
{
	MODERN_CFG spare_cfg;
	long yaw;
	long remaining;

	if (yaw_delta)
		*yaw_delta = 0;

	if (!out || !out->active || out->mode != MODERN_MODE_RUN)
		return 0;

	if (!cfg)
	{
		ModernDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	if (!sit || sit->classic)
		return 0;

	if (cfg->air_turn_rate <= 0)
		return 0;

	if (!ModernStateAllowsAirTurn(sit->anim_state))
		return 0;

	yaw = ModernStepTowardBiased(out->delta, cfg->air_turn_rate,
		state ? state->last_turn : 0);
	remaining = ModernAbs(ModernWrap((long)out->delta - yaw));

	out->bits &= ~(long)(MODERN_IN_LEFT | MODERN_IN_RIGHT);
	out->turning = (remaining > 0) ? 1 : 0;

	if (state)
		state->last_turn = (yaw > 0) ? 1 : ((yaw < 0) ? -1 : 0);

	if (yaw_delta)
		*yaw_delta = yaw;

	return 1;
}

/* THE WATER'S OWN MOVE (round 161a2 step 4, the user's observation 7).
 *
 * "Unter Wasser ist der linke Stick wie ein D-Pad, was in alle Richtungen 360
 * Grad auf einem flachen Kreis Lara in die Richtung bewegt. Die Aenderungen im
 * dreidimensionalen Raum gehen ueber den rechten Stick, der ist wie ein
 * 3D-Pointer in eine Richtung, der Lara dann folgt. Beide zusammen ergeben
 * eine fluessige Bewegung durch den Raum."
 *
 * So the water is two independent halves, and this is both of them:
 *   the flat circle	ModernTranslate has already turned the left stick into
 *						a WORLD course, exactly as it does on land, and
 *						out->delta is how far she is from it. This turns her
 *						onto it at the rate TR5 turns at in that state.
 *   the 3D pointer		`target_pitch` is where the orbit is pointing,
 *						converted to a body attitude by the camera's own
 *						ModernCameraSwimPitch. x_rot walks towards it at the
 *						classic rate while the stick is out, and stops where
 *						she is when it is let go - she keeps her attitude and
 *						glides, which is observation 2's rule applied to the
 *						third axis.
 *
 * IT IS ModernDirectTurn's COUNTERPART AND NOT AN EXTENSION OF IT, because
 * the two differ in every line that matters: no instant about-turn (a one-tick
 * half turn under water is the AS_WATERROLL animation's job and looks wrong
 * without it), no IN_FORWARD under water (down there that bit is the PITCH -
 * SwimTurn reads it as x_rot -= 364 - and the pitch is the orbit's now), and
 * a stroke that the land has no notion of. What they share is the one line
 * both of them turn her with, ModernStepToward above.
 *
 * THE SURFACE IS THE LAND WITH A DIFFERENT RATE. There IN_FORWARD really is
 * forward - lara_as_surftread turns it into AS_SURFSWIM - so the bits come
 * out exactly as they do on foot, the stroke is that same bit, and nothing
 * touches x_rot: "auf der Oberflaeche ist der Kurs flach", and the engine
 * keeps her there anyway (larasurf.cpp never writes x_rot except to dive).
 *
 * WHAT IT DOES NOT TAKE: the MODERN_STATE. The land's turn clears state->spin
 * on its way out because the automaton's own turn-on-the-spot would otherwise
 * keep IN_FORWARD off for ticks afterwards; here the bits are rewritten in
 * full every tick and nothing reads spin in the water at all, and rule 6
 * recomputes it from the error on the next tick either way.
 *
 * Returns 1 when it has taken the tick - then `yaw_delta` is what the caller
 * adds to y_rot (and lara.turn_rate goes to zero, exactly as on land),
 * `new_x_rot` is her new attitude, `stroke` says whether to hold the stroke
 * key, and `out->bits` has been rewritten. Returns 0 with everything left as
 * it was found otherwise, and then this is not a water tick at all. */
long ModernWaterMove(const MODERN_CFG* cfg, const MODERN_SITUATION* sit, MODERN_RESULT* out,
	long target_pitch, long x_rot, long* yaw_delta, long* new_x_rot, long* stroke)
{
	MODERN_CFG spare_cfg;
	long rate;
	long yaw;
	long remaining;
	long pitch;
	long step;

	if (yaw_delta)
		*yaw_delta = 0;

	if (new_x_rot)
		*new_x_rot = x_rot;

	if (stroke)
		*stroke = 0;

	if (!cfg)
	{
		ModernDefaults(&spare_cfg);
		cfg = &spare_cfg;
	}

	/* No state number, no water: ModernWaterTurnRate above is the only thing
	 * that knows whether a direction bit down here means a direction at all. */
	if (!sit)
		return 0;

	rate = ModernWaterTurnRate(cfg, sit->anim_state);

	if (!rate)
		return 0;

	/* The stick at rest is not a course. She keeps the heading, the attitude
	 * and the speed she had - which under water is a glide and on the surface
	 * is treading - and the classic bits of specific\input.cpp stand, exactly
	 * as they do when the translation steps aside on land. */
	if (!out || !out->active)
		return 0;

	yaw = ModernStepToward(out->delta, rate);
	remaining = ModernAbs((long)out->delta - yaw);

	/* The turn bits go for the same reason they go on land - we are turning
	 * her ourselves and a bit would turn her twice - and under water they
	 * would do a second wrong thing as well: IN_FORWARD and IN_BACK are
	 * SwimTurn's pitch. IN_WALK is left alone in both, as it is on land. */
	out->bits &= ~(long)MODERN_DIR_MASK;

	if (ModernStateIsSwim(sit->anim_state))
	{
		/* THE STROKE. Held for as long as the stick is out, so that "der
		 * linke Stick bewegt Lara" is true without a second key: AS_TREAD and
		 * AS_GLIDE both take IN_JUMP into AS_SWIM and AS_SWIM stays there
		 * while it is held (game\laraswim.cpp:393, :420, :456). With
		 * water_auto_stroke off nothing is held and the player's own jump key
		 * is the stroke again, which is what TR5 has always done. */
		if (cfg->water_auto_stroke)
		{
			if (stroke)
				*stroke = 1;
		}

		/* THE POINTER. The target is clamped first and the step taken
		 * afterwards, so that the clamp is a place she can be rather than a
		 * wall the step stops at - and so that the difference below can never
		 * approach half a turn, which is what would make a wrap necessary. An
		 * attitude is not a heading: it is bounded by construction (the
		 * engine's own +-15470) and must never be folded. */
		pitch = target_pitch;

		if (pitch < cfg->swim_pitch_min)
			pitch = cfg->swim_pitch_min;
		else if (pitch > cfg->swim_pitch_max)
			pitch = cfg->swim_pitch_max;

		step = pitch - x_rot;

		if (step > cfg->swim_pitch_rate)
			step = cfg->swim_pitch_rate;
		else if (step < -cfg->swim_pitch_rate)
			step = -cfg->swim_pitch_rate;

		if (new_x_rot)
			*new_x_rot = x_rot + step;
	}
	else if (remaining <= cfg->snap_forward)
	{
		/* The surface, and the same 45 degrees the land uses: she swims once
		 * she is nearly facing the way she is going, so a half turn on the
		 * spot does not carry her the wrong way first. */
		out->bits |= MODERN_IN_FORWARD;
	}

	out->turning = (remaining > 0) ? 1 : 0;

	if (yaw_delta)
		*yaw_delta = yaw;

	return 1;
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

/* SPRINT MODE AND DUCK MODE (#161, round 161b2c, item 4; the user's own
 * sentence, "'Sprintmodus' und 'Duckmodus' kann auch mit rein"). One latch,
 * used twice, and deliberately not two functions: the two modes differ in
 * nothing but which bit they carry and what counts as a tick the latch may not
 * stand on, and both of those come in as arguments.
 *
 * HOLD IS THE DEFAULT AND HANDS THE WORD BACK UNTOUCHED - the first branch, and
 * the whole of "byte-identical at the defaults" for this function. The memory
 * is cleared on the way past, so that switching the row to Toggle and back
 * cannot leave a latch standing behind the player's back.
 *
 * `down` is the key or button as the input assembly has it THIS tick, and
 * `drop` is the caller's answer to "may a latch stand at all right now" - dead,
 * in the water, in a menu, and for the sprint also the empty dash bar and a
 * thumb pushing nothing. A DROP TICK IS A HOLD TICK, bit for bit: the latch
 * falls, and the raw key goes through exactly as it would at Hold. That is the
 * conservative answer and it is also the useful one - the binocular zoom reads
 * IN_SPRINT and IN_DUCK out of `inputBusy` (game\camera.cpp), and a rule that
 * forced those bits off while the sight was up would take the zoom away.
 *
 * The key memory is tracked on the drop ticks too, which is the whole of case
 * three in the record: a key still held when a level starts or a menu closes is
 * not a fresh press, and must not latch. */
long ModernModeLatch(long toggle, long down, long drop, long* state, long bit, long bits)
{
	long s;

	if (!toggle)
	{
		if (state)
			*state = 0;

		return bits;
	}

	s = state ? *state : 0;

	if (drop)
		s &= ~(long)MODERN_LATCH_ON;
	else if (down && !(s & MODERN_LATCH_KEY))
		s ^= (long)MODERN_LATCH_ON;

	if (down)
		s |= MODERN_LATCH_KEY;
	else
		s &= ~(long)MODERN_LATCH_KEY;

	if (state)
		*state = s;

	if (drop)
		return bits;

	bits &= ~bit;

	if (s & MODERN_LATCH_ON)
		bits |= bit;

	return bits;
}

/* AND THE ONE THING THE ENGINE HAS TO BE ABLE TO SAY FROM OUTSIDE A TICK: this
 * latch is off now. A level start and a savegame restore go through it
 * (game\gameflow.cpp), and so does flipping the row itself
 * (game\menu_frame.cpp), which is the only way the state could otherwise
 * survive its own option being switched off.
 *
 * It clears the LATCH only and leaves the key memory alone, on purpose - see
 * the paragraph above. */
void ModernModeLatchDrop(long* state)
{
	if (state)
		*state &= ~(long)MODERN_LATCH_ON;
}

/* WHAT AN ACTIVE TICK LEAVES BEHIND (round 161a2b, #161 item 7b). Four of the
 * translation's branches below end with a course in hand - aiming, the
 * directional jump, the step back and the ordinary run - and all four have to
 * leave the same two traces: the mode, for the log, which they each wrote for
 * themselves before, and now the COURSE, so that the next tick can say how far
 * it jumped. One place, because a branch that forgot would look to the direct
 * turn like a thumb holding perfectly still.
 *
 * The two branches that do NOT come through here are rule 0 (a classic state)
 * and rule 1 (the stick at rest). Neither has a course of its own, and neither
 * may wipe the one that is remembered - see MODERN_STATE::last_course for why
 * the memory has to outlive a release. */
static void ModernFinish(MODERN_STATE* state, MODERN_RESULT* out)
{
	out->course_step = state->have_course ?
		ModernWrap((long)out->target - state->last_course) : 0;

	state->last_course = out->target;
	state->have_course = 1;
	state->last_mode = out->mode;
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
	long water;

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
	out->course_step = 0;

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
		no_sit.cam_player_step = 0;
		sit = &no_sit;
	}

	out->mag = ModernMagnitude(cfg, stick_x, stick_y);

	/* IS THIS A TICK IN THE WATER (round 161a2 step 4)? Zero on land, and the
	 * state's own turn rate in the eight states the water steers. Two of the
	 * rules below ask, because each of them would otherwise send out a bit
	 * that means something else in the water; the rest of the translation is
	 * the same function it is on foot, which is the whole point of the user's
	 * "die Steuerung unter Wasser ist genau gleich wie ausserhalb". */
	water = ModernWaterTurnRate(cfg, sit->anim_state);

	/* 0. Hanging, the ladder, the monkey swing, a block, a vehicle, the
	 * object states: there a direction bit does not mean "where do I go",
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

	/* 2a. AND THE CAMERA HE TURNED TAKES THE COURSE WITH IT (round 161a3b,
	 * item 2), before the course is read and before the tree below can decide
	 * anything about it.
	 *
	 * The held course is the rule that stops her circling, and it does that by
	 * NOT consulting the camera between re-latches. That is right for a camera
	 * that moves by itself - the slow return home, an animation's carried
	 * swing, a cut - and it was wrong for the one camera movement that IS an
	 * instruction: the player's own right thumb. M2 measured what it cost.
	 * Steering with the camera while the left stick is held, her course stood
	 * still for as long as his left thumb did and then paid the whole
	 * accumulated angle in a single tick, because the re-latch seats on the
	 * camera of THAT tick: 34 such jumps in the water over 423 ticks, median
	 * 13.6 degrees, and 57 on foot over 617, median 13.2 - each one the turn
	 * rate's whole allowance in one frame. Round 161a3 did not create them; at
	 * the old 6 deg/tick each jump was smeared over several ticks.
	 *
	 * BOTH THE REFERENCE AND THE COURSE, because they are one angle seen twice:
	 * the reference is the camera the course was seated on, and a reference
	 * left behind would make the next re-latch a jump of exactly what this line
	 * just paid. With both carried, a re-latch finds nothing left to jump.
	 *
	 * THE PROTECTION IS UNTOUCHED. `cam_player_step` is the stick's own term
	 * and nothing else (game\modern_camera.cpp keeps it before the swing and
	 * the return run), so a camera moved by the return or by a carried swing
	 * still moves no course at all - which is what the camera-cut sweep and
	 * the return case in ModernTest hold. And the re-latch rule itself is not
	 * touched either: `last_off` is the STICK's own direction and this line
	 * does not go near it.
	 *
	 * ONLY WITH A COURSE IN HAND. Before the first push there is nothing to
	 * carry, and with hold_course off the camera of every tick is the
	 * reference anyway - the branch below seats it fresh. */
	if (state->have_ref && cfg->hold_course && sit->cam_player_step)
	{
		state->ref = ModernWrap((long)state->ref + sit->cam_player_step);
		state->course = ModernWrap((long)state->course + sit->cam_player_step);
	}

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
	 * longer than the shot.
	 *
	 * ... BUT NOT ON THE TICK SHE JUMPS. Aiming comes before the directional
	 * jump below, so with a gun ready and the action held a side flip or a
	 * back flip would come out as a side STEP: game\lara.cpp's lara_as_compress
	 * reads only IN_FORWARD/IN_BACK/IN_LEFT/IN_RIGHT, so IN_LSTEP/IN_RSTEP is
	 * no direction at all and the flip while firing becomes a jump straight up.
	 * TROne answer this one tick before the translation, by clearing aim_strafe
	 * in their glue whenever the situation is a jump (trone_modern_glue.h:473,
	 * OUTSIDE the Remaster profile block above it - which is why their own
	 * expectation passes on a plain glue). aim_strafe is read exactly here and
	 * nowhere else, so clearing it for the call and asking for it here are the
	 * same rule; it is asked here because our seam keeps ONE config and never
	 * copies it, and a copy per tick would be work outside the translation for
	 * a rule the translation can simply state. A tap is enough: the direction
	 * lara_as_compress writes into goal_anim_state on this tick is never taken
	 * back by a later one, so the flip survives the ticks in which she is back
	 * to aiming.
	 *
	 * AND NEVER IN THE WATER (round 161a2 step 4). There is no aiming down
	 * there - she cannot draw a weapon in the water at all - but sit->drawn
	 * can still be 1, because a FLARE is held at LG_READY and the seam's own
	 * test only excludes the torch. The branch's whole reason ("the game
	 * turns her onto her target itself") has no counterpart in the water
	 * either, and the side step it produces is nothing at all under water.
	 * So the question is asked here, once, rather than left to surprise the
	 * water's own move with a mode it cannot take. */
	state->aiming = (cfg->aim_strafe && sit->drawn && sit->action && !sit->jump && !water) ? 1 : 0;

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
		ModernFinish(state, out);
		return;
	}

	delta = ModernWrap((long)target - lara_yaw);
	adelta = ModernAbs(delta);

	/* 4. The directional jump out of a stand - BUT ONLY WITH A WEAPON DRAWN
	 * (round 161a-fix, #161 item 3, the user's own decision from his
	 * Remaster recording, jumps with and without weapons):
	 * "Wenn Lara die Waffen in der Hand hat (Kampfmodus) ist das jetzige
	 * Verhalten richtig. Wenn sie nicht im Kampfmodus ist, macht sie egal wie
	 * ich sie drehe den normalen Sprung aus dem Stand in die angegebene
	 * Richtung." Armed, AS_COMPRESS reads the direction bits as the jump -
	 * forward, the back flip, a side flip - and nothing turns at all.
	 * Unarmed, sit->drawn is 0 and this whole branch steps aside for rule 6
	 * below: she turns onto the held course and runs, so the jump key gets
	 * whatever the classic automaton makes of IN_FORWARD together with
	 * IN_JUMP - the "normal standing jump in the indicated direction" the
	 * user asked for, and it is also what lets him circle her on the stick
	 * and break out of the circle with a jump instead of only ever flipping
	 * sideways.
	 *
	 * The jump key may already be let go by then: AS_COMPRESS lasts several
	 * ticks and a tap is only in the first of them, so the state alone is
	 * enough to keep this branch once she is in it - still behind sit->drawn,
	 * so holstering mid-crouch falls through to rule 6 exactly as it would
	 * have if the trigger had never been pulled. Without the state in the
	 * condition, the ticks after the tap would fall into the run case, set
	 * IN_FORWARD, and turn every back flip into a forward jump. */
	if (cfg->jump_dir && sit->drawn && (sit->jump || sit->anim_state == 15) &&
		ModernStateIsStanding(sit->anim_state))
	{
		bits = ModernRelativeBits(delta, cfg->aim_sector, 0);

		state->spin = 0;
		out->bits = bits;
		out->active = 1;
		out->target = target;
		out->delta = delta;
		out->mode = MODERN_MODE_JUMP;
		ModernFinish(state, out);
		return;
	}

	/* 5. Back on the stick as a step backwards instead of an about-turn -
	 * only when back_turns is switched off, and never in the water: IN_BACK
	 * there is SwimTurn's "pitch her nose up" and not a step at all (round
	 * 161a2 step 4). Nothing ships with back_turns off, so this term changes
	 * no shipping behaviour - it keeps the switch honest for the play test
	 * and for the tests that drive it explicitly. */
	if (!cfg->back_turns && !water && adelta >= 0x8000L - cfg->aim_sector)
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
		ModernFinish(state, out);
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
	ModernFinish(state, out);
}

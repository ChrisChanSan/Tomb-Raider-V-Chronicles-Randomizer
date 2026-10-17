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

/* The arithmetic of the modern control, the movement half: the decision tree
 * only. The header says what it is and where the design comes from. No engine
 * header, no global, no side effect outside the structs the caller hands in. */

/* The stick's direction in the engine's 16-bit angle units. The camera
 * heading it is compared with comes from phd_atan (game\modern_state.cpp),
 * but specific\3dmath.h needs windows.h and d3d9.h and would stop this file
 * compiling for ModernTest, so the C library's atan2 is scaled instead.
 * Measured over all 131769 integer pairs in -181..181, it differs from
 * (short)phd_atan(z, x) by at most 5 of 65536 (0.03 degrees, the tan table's
 * resolution) - far under the smallest threshold, steer_step's 384. */
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
 * no decision - a diagonal reaches 181 and every threshold is under 128 - but
 * it keeps `mag` a number the log can be read against. */
static long ModernMagnitude(const MODERN_CFG* cfg, long x, long y)
{
	const double m = sqrt((double)(x * x + y * y));

	return (m > (double)cfg->stick_full) ? cfg->stick_full : (long)m;
}

/* To the nearest, away from zero at a half, so the scaling below is odd: a
 * stick pushed the other way reads the same distance with no bias from the
 * sign of its components. */
static long ModernRound(double v)
{
	return (v < 0.0) ? -(long)floor(-v + 0.5) : (long)floor(v + 0.5);
}

/* The radial dead zone, shared by both sticks (the numbers are in
 * game\modern_move.h beside MODERN_DEAD_LEFT). Pure, so that ModernTest's
 * `deadzone` group can check that the direction the thumb holds is the
 * direction that comes out.
 *
 *   inside the square the hardware's per-axis zone held (`axis_zone`):
 *                     nothing, so a pad resting in its corners stays dead;
 *   inside the zone   nothing, so a stick at rest cannot creep;
 *   past its rim      the length starts again at nothing and eases up to full
 *                     at the stick's own rim, so crossing it is not a step;
 *   in every case     the output is the input vector scaled, so the angle
 *                     survives as far as the integer grid allows.
 *
 * The length is clamped to full: the axes are gated as a square, so a diagonal
 * reads about 181, and a 1.4 times faster rate on the diagonal would be a
 * camera that turns faster while it tilts. `axis_zone` 0 is no square (no
 * per-axis hardware zone, or a test of the radial rules alone).
 *
 * Returns the length, 0..MODERN_STICK_FULL. */
long ModernStickDeadZone(long zone, long axis_zone, long in_x, long in_y, long* out_x, long* out_y)
{
	const double full = (double)MODERN_STICK_FULL;
	double mag, want;

	*out_x = 0;
	*out_y = 0;

	if (zone < 0)
		zone = 0;

	/* The hardware's square is asked first, because in the corners it reaches
	 * further than the disc: both components inside it is the pair
	 * specific\dxshell.cpp's per-axis zone zeroes, and a stick dead at rest there
	 * must stay dead. A push the per-axis zone lets through always has a
	 * component outside the square, so this gate does not bring the axis snap
	 * back. */
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

/* The player's dead-zone setting applied to one tuned width. `base` is one of
 * the eight MODERN_DEAD_* widths (game\modern_move.h), `user` is
 * tomb5.dead_zone, 0..100; the answer is the width the seam applies.
 *
 *   (base * 50 + 25) / 50 = base, exactly, for every base,
 *
 * because integer division drops the remainder, so the default of 50 is the
 * tuned width itself. The +25 rounds the rest of the bar to nearest (31 at 40
 * per cent is 24.8 -> 25). 0 gives no zone, 100 twice the base; the result is
 * monotone in `user`. ModernTest checks all three.
 *
 * Both arguments are guarded: `user` comes from the registry, which can be
 * edited by hand (tomb5\tomb5.cpp clamps it on reading too), and a negative
 * `base` means "no zone" to ModernStickDeadZone, so 0 is returned. */
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

/* The look key's tank control, read by sector. On the bypass ticks the stick
 * goes to the classic assembly in specific\input.cpp, whose two per-axis
 * thresholds of 8 are, behind the radial zone, a window of +-3.6 degrees round
 * the horizontal; a thumb pushing "left" sits 10 to 16 degrees low, and the
 * back bit then overwrites the turn goal in lara_as_stop (see
 * MODERN_TANK_TURN_SECTOR). So exactly one axis comes out of here.
 *
 * The decision is made on the stick's angle, which the thumb controls, and the
 * value handed on is the stick's magnitude on the surviving axis - not its
 * component, which would shrink towards the sector's edge and could fall back
 * under the classic 8. The previous sector is an argument, not a static:
 * specific\input.cpp knows when the bypass begins and ends and clears it, and
 * the function stays pure for the tests.
 *
 * Returns the sector chosen, MODERN_TANK_SECTOR_* (MODERNLOG's `sector=`). 0/0
 * in is 0/0 out and no sector - a resting stick gets no invented direction. */
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

	/* The stick's heading: 0 is straight forward, +0x4000 is right. stick_y
	 * is forward-negative (specific\dxshell.cpp), hence the negation, exactly as
	 * in ModernTranslate. */
	heading = ModernAngle(in_x, -in_y);

	/* How far that is from the horizontal (0x4000 either side of forward).
	 * Folding the sign away first makes left and right one case. */
	off_h = ModernAbs(ModernAbs(heading) - 0x4000L);

	/* The hysteresis only ever widens the sector the thumb is already in. A
	 * thumb resting on the boundary would otherwise cross it with noise, and
	 * the turn and the step would alternate at tick rate. With no previous
	 * sector (the first tick of a hold) the plain boundary answers. */
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

/* The four movement keys as a stick, and which of the two sources the seam
 * is handed. The keys are a full-deflection stick in the pad's own units and
 * convention - x right-positive, y forward-negative (specific\dxshell.cpp) -
 * each axis 0 or +-MODERN_STICK_FULL, and two opposite keys cancel on their
 * axis. A diagonal is two full axes, 181 long: ModernTranslate clamps the
 * magnitude to full and takes the angle from the pair, which is exactly 45
 * degrees, so nothing is scaled here. No key gives half a deflection; the
 * walk key is the keyboard's walk, as the pad's walk button is the pad's.
 *
 * The pad wins. When its pair is off centre - after the dead zone, so a
 * resting stick reads 0/0 - it is handed on unchanged and the keys are not
 * added: a sum would make a push of the stick with a key held a longer
 * vector at another angle than the stick alone. With the pad at rest the
 * keys' vector is handed on; with both at rest the answer is 0/0 and the
 * scheme steps aside as it does for a resting stick.
 *
 * Pure, so that ModernTest can check the keys against the stick. Returns the
 * source, MODERN_FEED_* (MODERNLOG's `feed=`). */
long ModernKeyStick(long pad_x, long pad_y, long forward, long back, long left, long right,
	long* out_x, long* out_y)
{
	const long full = (long)MODERN_STICK_FULL;
	long x, y;

	if (!out_x || !out_y)
		return MODERN_FEED_NONE;

	if (pad_x || pad_y)
	{
		*out_x = pad_x;
		*out_y = pad_y;
		return MODERN_FEED_PAD;
	}

	x = (right ? full : 0) - (left ? full : 0);
	y = (back ? full : 0) - (forward ? full : 0);

	*out_x = x;
	*out_y = y;

	return (x || y) ? MODERN_FEED_KEYS : MODERN_FEED_NONE;
}

/* A world course, in Lara's own frame, as one direction bit. Four sectors:
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
 * course so that the next push seats a fresh one. The blocked counter is not
 * cleared here - it is the caller's, it is measured from Lara's position and
 * it has its own rule in game\modern_state.cpp. */
static void ModernRelease(MODERN_STATE* state)
{
	state->have_ref = 0;
	state->spin = 0;
	state->aligned = 0;
	state->aiming = 0;
	state->cut_latch = 0;	// no course, nothing to keep a reference for
	state->cut_off = 0;
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
	cfg->cut_latch = MODERN_CUT_LATCH;
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
	state->cut_latch = 0;
	state->cut_off = 0;
}

/* The states that stay classic, checked state by state against
 * game\lara_states.h and the routine each runs (lara_control_routines[] in
 * game\lara.cpp). TROne's list stops at 104; TR5 runs to 138, and the extra
 * states were checked one by one. A direction bit stays classic wherever the
 * routine reads it as something other than "where do I want to go" - which
 * way to shimmy, whether to let go of a rope, which way to push a block;
 * everything else (standing, running, walking, the jumps, ducking, crawling)
 * is translated, so the switch lists the classic states and `default`
 * answers modern.
 *
 * The eight states steered in the water (13/17/18 under it, 33/34/47/48/49
 * on it) are translated and get ModernWaterMove instead of ModernDirectTurn.
 * Those that only look like water stay classic, each for its reason below;
 * DOZY's flight (118) reads the bits as a direction to fly.
 *
 * 25/26/27, the back flip and the side flips, are classic as in TROne (which
 * added them after the translation turned Lara onto the stick heading
 * mid-flip): lara_as_leftjump sets AS_TWIST on IN_RIGHT, lara_as_rightjump on
 * IN_LEFT, and lara_as_backjump reads IN_FORWARD | IN_ROLL to end the flip
 * early. 28 and 29 (lara_as_upjump, lara_as_fallback) read no direction bit
 * and stay modern. */
long ModernStateIsClassic(short anim_state)
{
	switch (anim_state)
	{
	/* The water's handovers, which look like swimming and are not steered
	 * like it. 52/53/55 are the way into the water and out of it (this list's
	 * own choice; TROne's classic list does not name them). 35 also: lara_as_dive
	 * reads IN_FORWARD as "steepen the dive" (laraswim.cpp), a pitch and not a
	 * direction, and the animation is over in a handful of ticks. */
	case 35:	// AS_DIVE, lara_as_dive - the entry, not the swim
	case 52:	// AS_SWANDIVE, lara_as_swandive - the entry, not the swim
	case 53:	// AS_FASTDIVE, lara_as_fastdive - the entry, not the swim
	case 55:	// AS_WATEROUT, lara_as_waterout - the way out, not the swim
		return 1;

	/* The underwater 180 and the underwater death. The roll is a short fixed
	 * maneuver on its own animation, not the held swim/glide loop the orbit
	 * steers; the death is not steered at all (the seam's hit_points test in
	 * game\modern_state.cpp catches it a tick earlier). */
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

	/* The overhead hang and the monkey swing - the same
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

	/* During these Lara is Busy and the input mask already strips everything
	 * but the four direction bits, so the choice is harmless either way; they
	 * are listed so the list is complete. 98 shares AS_PICKUP's routine. */
	case 39:	// AS_PICKUP, lara_as_pickup - Busy
	case 40:	// AS_SWITCHON, lara_as_switchon - Busy
	case 41:	// AS_SWITCHOFF, lara_as_switchoff - Busy
	case 42:	// AS_USEKEY, lara_as_usekey - Busy
	case 43:	// AS_USEPUZZLE, lara_as_usepuzzle - Busy
	case 50:	// AS_USEMIDAS, lara_void_func - Busy
	case 51:	// AS_DIEMIDAS, lara_void_func - Busy
	case 67:	// AS_FLAREPICKUP, lara_as_pickupflare - Busy
	case 70:	// AS_DEATHSLIDE, lara_as_deathslide - reads IN_ACTION_ANY, Busy
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

	/* Everything TR5 has past TROne's 104, checked one by one. 105/106 are the exception, not listed here: a crouch turn,
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
	case 126:	// AS_CROWDOVE, lara_as_switchon - tightrope family (119-127)
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

	/* The ledge hang: the move turns her and lowers her itself (ModernLedgeDecide);
	 * a stick steering her heading would pull the turn off its cardinal. */
	case 139:	// AS_LEDGEHANG, lara_as_ledgehang
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

/* The three states in which she swims under water: 13 AS_TREAD, 17 AS_SWIM,
 * 18 AS_GLIDE, all running SwimTurn (game\laraswim.cpp). There the direction
 * bits mean a pitch and a turn rather than a direction, so the water's move
 * clears them and drives x_rot, and the stroke is the jump key. The five
 * surface states behave like the land: forward is forward.
 */
long ModernStateIsSwim(short anim_state)
{
	return (anim_state == 13 || anim_state == 17 || anim_state == 18) ? 1 : 0;
}

/* The hold-on family for Grab mode (Lara stays hanging after a grab and lets
 * go on the next press): every state in which a cleared IN_ACTION makes her
 * let go, and no other - every routine that reads the bit as "still holding
 * on", found by searching for IN_ACTION and IN_ACTION_ANY.
 *
 *   10  lara_col_hang      -> LaraHangTest       game\lara.cpp
 *   30  lara_col_hangleft  -> LaraHangTest       game\lara.cpp
 *   31  lara_col_hangright -> LaraHangTest       game\lara.cpp
 *   75  lara_as_hang2      IN_ACTION             game\lara.cpp
 *       lara_col_hang2     -> LaraHangTest       game\lara.cpp
 *   76  lara_col_monkeyswing                     game\lara.cpp
 *   77  lara_col_monkeyl                         game\lara.cpp
 *   78  lara_col_monkeyr                         game\lara.cpp
 *   79  lara_col_monkey180 -> lara_col_monkeyswing  game\lara.cpp
 *   82  lara_col_hangturnlr                      game\lara.cpp
 *   83  the same routine
 *   56  lara_col_climbstnc -> LaraCheckForLetGo  game\laraclmb.cpp
 *   57  lara_col_climbing  -> the same           game\laraclmb.cpp
 *   58  lara_col_climbleft -> the same           game\laraclmb.cpp
 *   60  lara_col_climbright-> the same           game\laraclmb.cpp
 *   61  lara_col_climbdown -> the same           game\laraclmb.cpp
 *   99  lara_col_polestat  IN_ACTION_ANY         game\lara.cpp
 *  100  lara_col_poleup                          game\lara.cpp
 *  101  lara_col_poledown                        game\lara.cpp
 *  102  lara_as_poleleft                         game\lara.cpp
 *  103  lara_as_poleright                        game\lara.cpp
 *   90  lara_as_ropel                            game\lara.cpp
 *   91  lara_as_roper                            game\lara.cpp
 *  111  lara_as_rope / lara_col_rope             game\lara.cpp
 *  114  lara_as_rope (AS_ROPEFWD shares it)      game\lara.cpp
 *  115  lara_as_rope (AS_ROPEBACK)               game\lara.cpp
 *  113  lara_as_climbroped -> LaraClimbRope      game\rope.cpp
 *   70  lara_as_deathslide - the zip line        game\lara.cpp
 *  128  lara_as_parallelbars                     game\lara.cpp
 *  139  lara_as_ledgehang  IN_ACTION             game\lara.cpp
 *
 * Six states read nothing and are in the family because they are transitions
 * inside it, where the bit must not blink off: 59 AS_CLIMBEND (the half rung
 * at the top of a ladder), 112 AS_CLIMBROPE (the pull up the rope, which reads
 * IN_ROLL and hands her back to 111), and the corner shimmies 107-110, whose
 * routines call SetCornerAnim and return her to AS_HANG or AS_CLIMBSTNC.
 *
 * 139 AS_LEDGEHANG is the ledge hang's turn and lowering, a transition into
 * AS_HANG. The turn reads IN_ACTION through ModernLedgeDecide and stands her
 * up without it; the lowering reads nothing. The move begins with the grab key
 * held (LaraLedgeTrigger, on the ground, outside the family) and is a grab
 * from there on: on the tick the lowering hands her to AS_HANG, lara_col_hang
 * runs on the word built for 139, and without the bit LaraHangTest drops her.
 *
 * Not in it:
 * - the catches. 11 AS_REACH, 28 AS_UPJUMP, 29 AS_FALLBACK, 2 AS_STOP and the
 *   rest read IN_ACTION as "grab this" (game\lara.cpp, game\objects.cpp,
 *   game\switch.cpp, game\rope.cpp, game\traps.cpp, game\deathsld.cpp). A
 *   latched bit there would make her grab everything she fell past.
 * - 80 AS_ALL4S, where IN_ACTION with IN_BACK starts the crawl-to-hang: a
 *   catch too.
 * - 87 AS_HANG2DUCK and 88 AS_CRAWL2HANG: one-way transitions across the
 *   family's edge that read no input, so membership would change nothing.
 * - 129 AS_PBLEAP, the leap off the bar: she has already let go.
 * - pushing and pulling a block (36/37/38): game\moveblok.cpp reads
 *   IN_INTERACT, not IN_ACTION, and the latch delivers only IN_ACTION, so a
 *   block is pushed with the key held at either setting. Under "One key" a
 *   let-go press clears the whole action word, but only for the rest of that
 *   press; the press that starts a push is a new one, and it pushes. */
long ModernStateIsGrab(short anim_state)
{
	switch (anim_state)
	{
	case 10:	// AS_HANG - the ledge hang
	case 30:	// AS_HANGLEFT - shimmy
	case 31:	// AS_HANGRIGHT
	case 82:	// AS_HANGTURNL
	case 83:	// AS_HANGTURNR
	case 107:	// AS_CORNEREXTL - the four corner shimmies, transitions
	case 108:	// AS_CORNEREXTR
	case 109:	// AS_CORNERINTL
	case 110:	// AS_CORNERINTR
		return 1;

	case 75:	// AS_HANG2 - the overhead hang
	case 76:	// AS_MONKEYSWING
	case 77:	// AS_MONKEYL
	case 78:	// AS_MONKEYR
	case 79:	// AS_MONKEY180
		return 1;

	case 56:	// AS_CLIMBSTNC - the ladder
	case 57:	// AS_CLIMBING
	case 58:	// AS_CLIMBLEFT
	case 59:	// AS_CLIMBEND - a transition, reads nothing
	case 60:	// AS_CLIMBRIGHT
	case 61:	// AS_CLIMBDOWN
		return 1;

	case 99:	// AS_POLESTAT - the pole
	case 100:	// AS_POLEUP
	case 101:	// AS_POLEDOWN
	case 102:	// AS_POLELEFT
	case 103:	// AS_POLERIGHT
		return 1;

	case 90:	// AS_ROPELEFT - the rope
	case 91:	// AS_ROPERIGHT
	case 111:	// AS_ROPE
	case 112:	// AS_CLIMBROPE - a transition, reads IN_ROLL
	case 113:	// AS_SLIDEROPE
	case 114:	// AS_ROPEFWD
	case 115:	// AS_ROPEBACK
		return 1;

	case 70:	// AS_DEATHSLIDE - the zip line
	case 128:	// AS_PBSPIN - the parallel bars
		return 1;

	case 139:	// AS_LEDGEHANG - the ledge hang's turn and lowering, a transition into
			// AS_HANG: the move begins with the grab key held and is a grab from there on
		return 1;

	default:
		return 0;
	}
}

/* The duck-and-crawl family for Duck mode. The duck latch drops on
 * lara.gun_status == LG_HANDS_BUSY (ladder, rope, a crawl-to-hang), but
 * lara_as_all4s sets LG_HANDS_BUSY itself as ANIM_DUCK_TO_ALL4S runs, so the
 * latch would fall the moment she starts to crawl. Inside this family the flag
 * means "she is crawling" and the latch stands; outside it, it still drops.
 *
 * The list is the engine's own "crouched or crawling" (game\newinv2.cpp
 * refuses a long gun in exactly these states) with two differences:
 * - AS_DUCKROLL (72) is added: the roll out of a duck ends in a duck.
 * - AS_CRAWL2HANG (88) is not in it: game\lara.cpp sets LG_HANDS_BUSY there
 *   because she is leaving the floor, and the grab family takes over.
 *
 * The three pickup states are in it because a pickup from the crouch or the
 * crawl sets LG_HANDS_BUSY too (game\pickup.cpp) and must not stand her up.
 * Standing they cost nothing: the latch stands only while she is crouched, and
 * lara.Busy strips IN_DUCK during a pickup anyway (specific\input.cpp).
 * 89 AS_CONTROLLED is not in it: the crawl jump borrows a scripted state that
 * a dozen other objects use (a pulley, a trapdoor). */
long ModernStateIsDuck(short anim_state)
{
	switch (anim_state)
	{
	case 71:	// AS_DUCK - the crouch itself
	case 72:	// AS_DUCKROLL - the roll out of it
	case 105:	// AS_DUCKROTL - the crouch turn
	case 106:	// AS_DUCKROTR
		return 1;

	case 80:	// AS_ALL4S - all fours, and ANIM_DUCK_TO_ALL4S runs in it
	case 81:	// AS_CRAWL - forward
	case 84:	// AS_ALL4TURNL - the crawl turn
	case 85:	// AS_ALL4TURNR
	case 86:	// AS_CRAWLBACK
		return 1;

	case 39:	// AS_PICKUP - a pickup made from the crouch or the crawl
	case 67:	// AS_FLAREPICKUP
	case 98:	// AS_HIDDENPICKUP
		return 1;

	default:
		return 0;
	}
}

/* The water's turn table, and at the same time the test "this tick is the
 * water's": 0 means no water state, anything else is that state's rate (the
 * header's constants say where each comes from). One function, because the
 * seam asks both questions on the same tick. */
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

/* Standing still, or turning on the spot. Here a turn is only reachable
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

/* Is she already running? 1 AS_RUN and 73 AS_DASH (the sprint), the two
 * direct-turn states where her feet already carry her forward at speed.
 * There the direct turn does not withhold IN_FORWARD: with the stick circling
 * faster than she turns, the error stands over snap_forward for single ticks,
 * the bit drops out and lara_as_run goes to AS_STOP for that tick, a visible
 * stumble (measured on 203 of 937 running ticks). The step the gate would
 * prevent is one she is taking anyway. 0 AS_WALK is not here - a walk is slow
 * enough to place a foot wrong - and neither is any standing state. */
long ModernStateIsRunning(short anim_state)
{
	return (anim_state == 1 || anim_state == 73) ? 1 : 0;
}

/* Is she turning on the spot? The four states of the classic turn on the
 * spot, where nothing carries her anywhere: 2 AS_STOP, 6 AS_TURN_R,
 * 7 AS_TURN_L, 20 AS_FASTTURN. They get the wider IN_FORWARD gate
 * (snap_forward_stand, see MODERN_SNAP_FORWARD_STAND): at 45 degrees a
 * circling stick keeps her spinning in AS_STOP without the run animation; at
 * 90 she runs within a few ticks of any reversal and the run turns her.
 *
 * Not the same as ModernStateIsGroundIdle, which also holds 12 AS_SPLAT and
 * 14 AS_LAND - a wall and a landing, a tick or two with her weight still being
 * caught, where the narrow 45 is right. Nor ModernStateIsStanding (2/14/15),
 * which asks "can she jump from here". */
long ModernStateIsSpotTurn(short anim_state)
{
	return (anim_state == 2 || anim_state == 6 || anim_state == 7 ||
		anim_state == 20) ? 1 : 0;
}

/* The airborne state she may steer in: only 3 AS_FORWARDJUMP, the one the
 * classic engine itself steers - lara_as_forwardjump is the only routine in
 * game\lara.cpp that reads IN_LEFT or IN_RIGHT off the ground. The air turn
 * widens what the engine already does and gives no state a new ability (see
 * MODERN_AIR_TURN_RATE for the reach, the dives and the falls). */
long ModernStateAllowsAirTurn(short anim_state)
{
	return (anim_state == 3) ? 1 : 0;	// AS_FORWARDJUMP, lara_as_forwardjump
}

/* The states the direct turn may write y_rot in. The rest of the scheme
 * sets a turn bit and lets the state machine turn her, but its fastest rate is
 * 1456 a tick, so a half turn on the spot costs about 23 ticks; the direct
 * turn writes the heading itself (the write is in game\modern_state.cpp).
 *
 * TROne allows the write in 0/1/2/6/7/12/14/20/73: standing, walking,
 * running, the turns on the spot, the walk into a wall, the landing and the
 * sprint. Never airborne, on a ladder or rope, in the water or in an
 * interaction pose - there y_rot belongs to the animation, and an outside
 * write makes the pose go crooked. Checked against game\lara_states.h and the
 * routine each runs (TR5's sprint is AS_DASH / lara_as_dash; AS_LAND has no
 * routine, lara_control_routines[14] is lara_void_func).
 *
 * 15 AS_COMPRESS is added: unarmed, ModernTranslate's rule 4 steps aside for
 * rule 6 and the crouch before a jump can arrive with mode RUN, so the write
 * continuing through it keeps her aligning into the takeoff instead of
 * freezing her heading for the two or three ticks of the crouch. An armed flip
 * is not affected: AS_COMPRESS is mode JUMP with a weapon drawn, and
 * ModernDirectTurn refuses every mode but MODERN_MODE_RUN. */
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

/* How far an angle may move this tick: the error, folded the short way round
 * and clamped to the rate. The land's, the air's and the water's turns all use
 * it and differ only in the states they allow and the rate they pass.
 *
 * The rate is clamped as well. Nothing sets it that high, but a rate over
 * half a turn would make "clamp the error to the rate" meaningless. */
static long ModernStepTowardBiased(long delta, long rate, long bias)
{
	delta = ModernWrap(delta);

	if (rate > 0x4000L)
		rate = 0x4000L;

	/* Exactly half a turn is the same angle either way round, and ModernWrap
	 * answers -32768, the only one a short can hold - so every exact reversal
	 * would go left by accident. The caller's bias, the turn's own last
	 * direction (MODERN_STATE::last_turn), decides the tie and the reversal
	 * stays in one direction. With no bias the fixed side stands. */
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

/* The same, with the tie at exactly half a turn handed to a side (see
 * ModernStepTowardBiased). A second function rather than a fourth argument,
 * because ModernGroundTurn is the shape TROne's test expectations are written
 * in. */
long ModernGroundTurnBiased(short anim_state, long delta, long rate, long bias)
{
	if (rate <= 0 || !ModernStateAllowsDirectTurn(anim_state))
		return 0;

	return ModernStepTowardBiased(delta, rate, bias);
}

/* The direct turn's whole decision, in one place so that a test can drive it
 * exactly as the seam does. Returns 1 when it takes the turning this tick:
 * `yaw_delta` is what the caller adds to y_rot, the caller zeroes
 * lara.turn_rate so the state machine does not turn her a second time, and
 * `out->bits` is rewritten. Otherwise returns 0 with a yaw_delta of 0 and
 * `out` untouched.
 *
 * With the heading written directly, a turn bit would turn her twice and put
 * her in AS_TURN_L/AS_TURN_R instead of running, so all six direction bits go
 * (IN_WALK stays). IN_FORWARD comes back only when snap_forward or less of the
 * turn is left, or she would take real steps sideways across a ledge during a
 * half turn. Running states always keep it (ModernStateIsRunning); the four
 * turn-on-the-spot states use the wider gate (ModernStateIsSpotTurn) plus a
 * way out counted in ticks (MODERN_STAND_FORWARD_TICKS).
 *
 * A reversal turns at the rate like everything else - 5461, a half turn in
 * six ticks, with the tie at exactly 180 degrees decided by
 * MODERN_STATE::last_turn - and never in one tick, which looks unnatural and
 * outruns the frame interpolation. */
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

	/* The standing-stall counter belongs to one unbroken turn
	 * (MODERN_STAND_FORWARD_TICKS). It is taken out of the state here and
	 * written back only on the path that really turns her, so every early
	 * return below ends the count. A count left standing would hand IN_FORWARD
	 * to the first tick of the next push, the very tick the 90 degree gate is
	 * for. Without a state the count starts at 0 on every call and the rule
	 * never fires. */
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

	/* No aiming guard: aiming does not write the heading.
	 * game\larafire.cpp, game\lara1gun.cpp and game\lara2gun.cpp never write
	 * lara_item->pos.y_rot; they write lara.left_arm.y_rot,
	 * lara.right_arm.y_rot, lara.torso_y_rot and lara.head_y_rot. Her heading
	 * moves only through lara.turn_rate in LaraAboveWater, the turn bits' path
	 * that the direct turn replaces armed or not. With aim_strafe off, a guard
	 * would leave her unable to turn while the trigger is held. */

	if (!cfg->direct_turn || cfg->turn_rate <= 0)
		return 0;

	if (!ModernStateAllowsDirectTurn(sit->anim_state))
		return 0;

	yaw = ModernGroundTurnBiased(sit->anim_state, out->delta, cfg->turn_rate,
		state ? state->last_turn : 0);

	/* Wrapped because of the tie-break: a delta of exactly -32768 taken to the
	 * right leaves delta minus yaw at 38229, 210 degrees the long way round
	 * rather than 150. On every other tick yaw is on delta's side and the wrap
	 * changes nothing. */
	remaining = ModernAbs(ModernWrap((long)out->delta - yaw));

	out->bits &= ~(long)MODERN_DIR_MASK;

	spot = ModernStateIsSpotTurn(sit->anim_state);
	forward = ModernStateIsRunning(sit->anim_state) ||
		remaining <= (spot ? cfg->snap_forward_stand : cfg->snap_forward);

	/* The second question, asked in ticks (MODERN_STAND_FORWARD_TICKS). Only
	 * the turn-on-the-spot states count: elsewhere she is already moving, so a
	 * withheld IN_FORWARD costs a step and not an animation, and any other
	 * state falls through to AS_STOP within a tick or two and is counted there.
	 * Once open it stays open - the gate is still shut on the next tick, so the
	 * count only rises - until the run begins or the turn arrives. */
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

	/* The spin is the state machine's way of turning on the spot, and the
	 * direct turn has just taken the turning over. Leaving the flag set would keep
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

/* The direct turn in the air, so she can keep steering left and right after
 * a jump and reach round a ledge. ModernDirectTurn's shape with three
 * differences:
 *   the state list	is one state long (ModernStateAllowsAirTurn): the
 *					classic steers exactly one airborne state.
 *   the bits		only IN_LEFT and IN_RIGHT go. lara_as_forwardjump reads
 *					IN_BACK as the twist, IN_WALK as the swan dive and
 *					IN_ACTION as the grab, and those stay as they are;
 *					IN_FORWARD it does not read at all.
 *   no forward		there is no step to withhold - her feet are not on
 *		gate		anything.
 *
 * air_turn_rate 0 switches it off and the classic 1 degree a tick is back.
 * Returns 1 when it takes the tick; the caller then adds `yaw_delta` to y_rot
 * and zeroes lara.turn_rate, as on the ground. */
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

/* The water's move. Under water the left stick moves her in a direction on a
 * flat circle and the right stick points the way in three dimensions; the two
 * halves are independent:
 *   the flat circle	ModernTranslate has turned the left stick into a world
 *						course as on land, and out->delta is how far she is
 *						from it. This turns her onto it at that state's rate.
 *   the 3D pointer		`target_pitch` is where the orbit points, converted to
 *						a body attitude by ModernCameraSwimPitch. x_rot walks
 *						towards it while the stick is out and stays when it is
 *						let go, so she keeps her attitude and glides.
 *
 * A counterpart of ModernDirectTurn rather than an extension: under water
 * IN_FORWARD is SwimTurn's pitch (x_rot -= 364) and the pitch is the orbit's,
 * and there is a stroke the land has no notion of. Both share ModernStepToward.
 * On the surface IN_FORWARD is forward (lara_as_surftread -> AS_SURFSWIM), so
 * the bits come out as on foot and x_rot is left alone - larasurf.cpp writes
 * x_rot only to dive. No MODERN_STATE: the bits are rewritten in full every
 * tick and nothing reads spin in the water.
 *
 * Returns 1 when it takes the tick: `yaw_delta` is added to y_rot by the
 * caller (lara.turn_rate zeroed, as on land), `new_x_rot` is her new attitude,
 * `stroke` says whether to hold the stroke key, and `out->bits` is rewritten.
 * Returns 0 with everything as found when this is not a water tick. */
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

	/* The turn bits go as on land - the heading is written directly and a bit
	 * would turn her twice - and under water IN_FORWARD and IN_BACK would also
	 * pitch her through SwimTurn. IN_WALK is left alone, as on land. */
	out->bits &= ~(long)MODERN_DIR_MASK;

	if (ModernStateIsSwim(sit->anim_state))
	{
		/* The stroke, held for as long as the stick is out, so the stick moves
		 * her without a second key: AS_TREAD and AS_GLIDE take IN_JUMP into
		 * AS_SWIM, and AS_SWIM stays while it is held (game\laraswim.cpp). With
		 * water_auto_stroke off the player's jump key is the stroke, as in TR5. */
		if (cfg->water_auto_stroke)
		{
			if (stroke)
				*stroke = 1;
		}

		/* The pointer. The target is clamped first and the step taken after,
		 * so the clamp is a place she can be rather than a wall the step stops
		 * at, and the difference never nears half a turn. An attitude is not a
		 * heading: it is bounded (the engine's +-15470) and is never folded. */
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

/* The weapon half (combat mode), called at the second seam of
 * specific\input.cpp with tomb5.combat_hold as its first argument.
 *
 * Two truths come in. `ready` is a weapon out and shootable (gun_status ==
 * LG_READY, no torch), `holstered` is the hands empty (LG_NO_ARMS). BOTH
 * false means the draw or holster animation is running, or the hands are busy
 * on a ladder or in the water - and then no IN_DRAW goes out in either mode,
 * so an impossible request cannot queue up and fire a second later.
 *
 * Toggle hands the bits back untouched: the draw key already is a toggle,
 * because game\larafire.cpp turns any IN_DRAW at LG_READY into
 * LG_UNDRAW_GUNS. The original behaviour is the Toggle behaviour, bit for bit,
 * which is why Toggle is the default.
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

/* Sprint mode and Duck mode (Hold or Toggle). One latch used for both: the
 * modes differ only in the bit they carry and in the ticks the latch may not
 * stand on, and both come in as arguments.
 *
 * Hold is the default and hands the word back untouched; the memory is cleared
 * on the way, so switching to Toggle and back cannot leave a latch standing.
 *
 * `down` is the key as the input assembly has it this tick; `drop` says a
 * latch may not stand now - dead, in the water, in a menu, and for the sprint
 * an empty dash bar or a stick pushing nothing. A drop tick is a Hold tick bit
 * for bit: the latch falls and the raw key goes through. That also keeps the
 * binocular zoom, which reads IN_SPRINT and IN_DUCK from `inputBusy`
 * (game\camera.cpp). The key memory is tracked on drop ticks too, so a key
 * still held when a level starts or a menu closes does not latch. */
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

/* Grab mode: she stays hanging after a grab and a second press lets go. Not
 * a toggle: she holds on because her state is a hanging one (`family`, from
 * ModernStateIsGrab), so the only memory needed is which press let go.
 *
 * Hold is the default and hands the word back untouched, clearing the memory,
 * as the mode latch does. Otherwise, in the order the code asks:
 * - a press inside the family is a let-go: MODERN_GRAB_RELEASE goes up and the
 *   bits are taken out of the word, so the routine sees a released key.
 * - while that press lasts `release` stays clear wherever she is, or the same
 *   press would catch the next ledge she falls past. Under "One key" the held
 *   let-go press also picks up and operates nothing until the key was up once.
 * - inside the family with no such press, the bit is delivered whether the
 *   key is down or not (a savegame restored in a hang needs no key).
 * - outside the family the word goes through as it came.
 * `drop` (a cutscene, a menu, the title screen, death) is a Hold tick bit for
 * bit; the key memory is tracked across it, so a key still down when a menu
 * closes lets go of nothing.
 *
 * `bit` is the delivery and `release` the letting go, two words: thirteen of
 * the family's states ask IN_ACTION_ANY (the pole, the rope, the zip line,
 * the parallel bars - game\lara.cpp, game\rope.cpp), and under "One key" the
 * action key raises IN_ACTION and IN_INTERACT together (specific\input.cpp's
 * action_bits). Clearing IN_ACTION alone would leave the state reading "held",
 * and on the rope and the bars the cleared test is the only exit. So the
 * caller composes `release` as game\lara.cpp spells the distinction -
 * IN_ACTION_ANY under "One key", IN_ACTION under "Separate" - and `bit` stays
 * IN_ACTION, which satisfies IN_ACTION_ANY without delivering an unasked
 * IN_INTERACT. Under "Separate" the two words are the same value. */
long ModernGrabLatch(long toggle, long down, long family, long drop, long* state, long bit,
	long release, long bits)
{
	long s;

	if (!toggle)
	{
		if (state)
			*state = 0;

		return bits;
	}

	s = state ? *state : 0;

	if (drop || !down)
		s &= ~(long)MODERN_GRAB_RELEASE;
	else if (family && !(s & MODERN_LATCH_KEY))
		s |= MODERN_GRAB_RELEASE;

	if (down)
		s |= MODERN_LATCH_KEY;
	else
		s &= ~(long)MODERN_LATCH_KEY;

	if (state)
		*state = s;

	if (drop)
		return bits;

	if (s & MODERN_GRAB_RELEASE)
		return bits & ~release;

	if (family)
		bits |= bit;

	return bits;
}

/* Switches a latch off from outside a tick: on a level start and a savegame
 * restore (game\gameflow.cpp) and when the option itself is changed
 * (game\menu_frame.cpp). It clears the latch and the grab latch's release bit
 * - both are decisions that must not outlive a level change, and all three
 * latches share the shape - and leaves the key memory alone, so a held key is
 * still not a fresh press. */
void ModernModeLatchDrop(long* state)
{
	if (state)
		*state &= ~(long)(MODERN_LATCH_ON | MODERN_GRAB_RELEASE);
}

/* What an active tick leaves behind. The four branches that end with a
 * course in hand - aiming, the directional jump, the step back and the run -
 * record the mode for the log and the course, so the next tick can say how far
 * the course jumped (course_step). Rule 0 (a classic state) and rule 1 (the
 * stick at rest) do not come through here: they have no course and must not
 * wipe the remembered one (see MODERN_STATE::last_course). */
static void ModernFinish(MODERN_STATE* state, MODERN_RESULT* out)
{
	out->course_step = state->have_course ?
		ModernWrap((long)out->target - state->last_course) : 0;

	state->last_course = out->target;
	state->have_course = 1;
	state->last_mode = out->mode;
}

/* The translation.
 *
 * stick_x is right-positive and stick_y is forward-negative - that is how
 * specific\dxshell.cpp hands the axes over ("joystick_read_y = -ly / 256")
 * and how specific\input.cpp reads them for the classic bits. So the stick's
 * own angle is taken from (stick_x, -stick_y).
 *
 * The held course is the core. The chase camera hangs behind Lara, so its
 * heading is her heading; taken as the reference of every tick, she turns, the
 * camera follows, the target walks on and she circles forever (TROne recorded
 * 600 frames of it). So the state carries a world course, and the camera is
 * consulted only when the player changes direction:
 *   stick pushed from rest		course = camera + stick angle. This is the
 *								camera-relative part - the same stick means a
 *								different world heading under a different
 *								camera.
 *   stick held, angle moved
 *   less than steer_step		nothing. A thumb is never still.
 *   stick held, angle moved
 *   at least steer_step but
 *   under recenter				the course turns by exactly that much. The
 *								camera is not read, so a camera cut leaves her
 *								path alone.
 *   stick angle moved at
 *   least recenter				a new direction was meant: seat the course on
 *								the camera of this tick - or, after a cut
 *								in the same hold, on the reference the
 *								course has (MODERN_CUT_LATCH), as long as
 *								the stick stays within MODERN_CUT_REAIM of
 *								where it pointed at the cut.
 *   stick back at rest			forget it all; the next push starts fresh.
 * (Shipped, recenter equals steer_step, so the middle band is empty.)
 *
 * The order of the tree below matters: the
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
	out->latch_drop = 0;

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
		no_sit.cam_cut = 0;
		sit = &no_sit;
	}

	out->mag = ModernMagnitude(cfg, stick_x, stick_y);

	/* Is this a tick in the water? Zero on land, the state's turn rate in the
	 * eight states the water steers. Two rules below ask, because each would
	 * otherwise send a bit that means something else in the water; the rest of
	 * the translation is the same as on foot. */
	water = ModernWaterTurnRate(cfg, sit->anim_state);

	/* 0. Hanging, the ladder, the monkey swing, a block, a vehicle, the
	 * object states: there a direction bit does not mean "where do I go",
	 * so the classic input stays and the result reports nothing done. The seam
	 * leaves the base bits standing when active is 0. */
	if (sit->classic || ModernStateIsClassic(sit->anim_state))
	{
		ModernRelease(state);
		state->last_mode = out->mode;
		return;
	}

	/* 1. The dead zone. The stick at rest is asked for by name and not left
	 * to the threshold: cfg->dead is 0 because the radial zone is applied one
	 * step earlier, and "mag < 0" is never true - without this
	 * line a centred stick would read as a push straight forward. */
	if ((!stick_x && !stick_y) || out->mag < cfg->dead)
	{
		ModernRelease(state);
		state->last_mode = out->mode;
		return;
	}

	/* 2. The stick's own direction, and the held world course. */
	offset = ModernAngle(stick_x, -stick_y);

	/* 2a. A camera the player turns takes the course with it, before the
	 * course is read. The held course ignores the camera between re-seats,
	 * which is right for a camera that moves by itself (the return home, a
	 * carried swing, a cut) and wrong for the player's own right thumb: the
	 * course would stand still and then pay the whole angle in one tick at the
	 * next re-seat (measured: 34 jumps in 423 water ticks, median 13.6 degrees;
	 * 57 in 617 on foot, median 13.2).
	 * The reference and the course move together - they are one angle seen
	 * twice, and a reference left behind would make the next re-seat jump by
	 * the same amount. `cam_player_step` is the stick's term only
	 * (game\modern_camera.cpp takes it before the swing and the return), so a
	 * camera moving by itself still moves no course, and `last_off`, the
	 * stick's own direction, is untouched. Only with a course in hand and
	 * hold_course on; otherwise the branch below seats it fresh. */
	if (state->have_ref && cfg->hold_course && sit->cam_player_step)
	{
		state->ref = ModernWrap((long)state->ref + sit->cam_player_step);
		state->course = ModernWrap((long)state->course + sit->cam_player_step);
	}

	/* 2b. A cut while a course is held latches its reference
	 * (MODERN_CUT_LATCH). From here until the stick is let go or re-aimed
	 * (2c), camera_yaw is another camera's than the one the course was seated
	 * on, and the re-seat below would turn her by the angle between the two;
	 * with the latch it reads the stick against the reference the course
	 * already has. Without a course there is nothing to keep: the push seats
	 * on the camera of the tick, the new one. The stick's direction of the
	 * arming tick is kept for 2c; a further cut while the latch stands changes
	 * neither. */
	if (state->have_ref && cfg->hold_course && cfg->cut_latch && sit->cam_cut &&
		!state->cut_latch)
	{
		state->cut_latch = 1;
		state->cut_off = offset;
	}

	/* 2c. The re-aim. A stick turned more than MODERN_CUT_REAIM, the short way
	 * round, from where it pointed when the latch was armed means a direction
	 * in the view on screen, so the hold ends on this tick: the latch drops
	 * and the branch below seats the course afresh on the camera of the tick,
	 * exactly as a release followed by a push in this direction would leave
	 * course, reference and latch. Nothing else of a release happens - spin,
	 * aligned and aiming stay as they are, so she does not stop. It runs after
	 * 2b, so on a tick that carries a cut as well the cut is taken first and
	 * the seat is on that tick's new camera. */
	if (state->cut_latch && cfg->hold_course && cfg->cut_latch &&
		ModernAbs(ModernWrap((long)offset - state->cut_off)) > MODERN_CUT_REAIM)
	{
		state->have_ref = 0;
		state->cut_latch = 0;
		state->cut_off = 0;
		out->latch_drop = 1;
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
		 * reference. This is the version that circles, kept so the difference
		 * can be shown with one switch. */
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
			/* The camera of the tick, or under the latch (2b) the reference
			 * the course was seated on. Course minus ref is last_off all
			 * through a hold (a seat sets it, the steer band and 2a keep it),
			 * so the latched re-seat turns the course by exactly the stick's
			 * own change. */
			const short seat = state->cut_latch ? state->ref : camera_yaw;

			state->ref = seat;
			state->course = ModernWrap((long)seat + offset);
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
	 * Not on the tick she jumps: aiming comes before the directional jump, and
	 * lara_as_compress reads only IN_FORWARD/IN_BACK/IN_LEFT/IN_RIGHT, so a side
	 * step bit would turn a flip while firing into a jump straight up. TROne
	 * clears aim_strafe in its glue whenever the situation is a jump
	 * (trone_modern_glue.h, trone_modern_glue_situation, TROne-main copy of
	 * 2026-10-05); aim_strafe is read only here, so asking here is
	 * the same rule without copying the config every tick. A tap is enough: the
	 * direction lara_as_compress writes into goal_anim_state is not taken back
	 * on the following ticks.
	 *
	 * Never in the water: she cannot draw a weapon there, but sit->drawn can be
	 * 1 because a flare is held at LG_READY, and a side step means nothing under
	 * water. */
	state->aiming = (cfg->aim_strafe && sit->drawn && sit->action && !sit->jump && !water) ? 1 : 0;

	if (state->aiming)
	{
		/* No held course here. While she aims, the game turns her onto her
		 * target itself and a held course would fight it, so the direction is
		 * read fresh against the camera every tick and comes out as a side
		 * step - the stick pushes her around the target. */
		target = ModernWrap((long)camera_yaw + offset);
		state->course = target;
		state->last_off = offset;
		state->ref = camera_yaw;
		state->cut_latch = 0;	// the reference is this tick's camera again
		state->cut_off = 0;

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

	/* 4. The directional jump out of a stand, only with a weapon drawn, a
	 * free choice of this project. Armed, AS_COMPRESS reads the direction bits
	 * as the jump - forward, the back flip, a side flip - and nothing turns.
	 * Unarmed, this branch steps aside for rule 6: she turns onto the held
	 * course and runs,
	 * and the jump key gives the normal standing jump in the stick's direction,
	 * so she can break out of a circle with a jump instead of a side flip.
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
	 * there is SwimTurn's "pitch her nose up" and not a step at all.
	 * back_turns ships on; the switch is for the tests that drive it. */
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
	 * way, IN_FORWARD keeps the classic state machine in AS_STOP or AS_SPLAT
	 * and the turn never happens, so a blocked tick sends the turn bit alone and
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

/* One tick of the ledge hang's half turn, its size: the ramp HALF_TURN * t /
 * TICKS in whole units, differenced, so the steps add up to the half turn
 * exactly for any tick count (6 ticks give 5461, 5461, 5462, 5461, 5461,
 * 5462). ModernLedgeDecide gives it the side's sign. 0 outside the turn. */
long ModernLedgeTurnStep(long tick)
{
	if (tick < 0 || tick >= MODERN_LEDGE_TURN_TICKS)
		return 0;

	return (long)MODERN_LEDGE_HALF_TURN * (tick + 1) / MODERN_LEDGE_TURN_TICKS -
		(long)MODERN_LEDGE_HALF_TURN * tick / MODERN_LEDGE_TURN_TICKS;
}

/* The ledge hang, one tick: which phase comes next, how far to turn her, how
 * many frames to step the pull-up down, and whether the move goes on, ends
 * in a stand or hands her to the engine's hang. The state in game\lara.cpp
 * measures and applies; nothing here reads the engine.
 *
 * The turn can be left on every tick: the key released, the jump key, weapons
 * drawn, damage since the first tick, death, a cutscene, or a refusal that
 * turned true (the edge test is asked again each tick). Any of them ends it in
 * a stand where she is. On the first tick a bad entry ends it the same way:
 * a heading farther than the catch's window from a cardinal, or a target that
 * is not the cardinal opposite. Within the window the first step also takes
 * the offset out, so the turn ends on the target to the unit.
 *
 * The lowering is never left half way: she already hangs over the drop by
 * position, and standing her up there would drop her. It runs down to the
 * first frame and hands over; death or a cutscene hands over at once, and the
 * hang's own routines deal with them from there. Its first tick is the one
 * the caller moved her to the hang position on, and shows the last frame
 * unstepped. */
long ModernLedgeDecide(const MODERN_LEDGE_SITUATION* sit, MODERN_LEDGE_RESULT* out)
{
	out->phase = MODERN_LEDGE_NONE;
	out->yaw_step = 0;
	out->frame_step = 0;
	out->verdict = MODERN_LEDGE_QUIET;

	if (!sit->option || sit->phase == MODERN_LEDGE_NONE)
		return out->verdict;

	if (sit->phase == MODERN_LEDGE_TURN)
	{
		out->verdict = MODERN_LEDGE_STAND;

		if ((sit->target & 0x3FFF) || sit->tick < 0 || sit->tick >= MODERN_LEDGE_TURN_TICKS)
			return out->verdict;

		if (sit->tick == 0)
		{
			if (ModernAbs(sit->cardinal_off) > MODERN_LEDGE_SNAP)
				return out->verdict;

			/* The cardinal she starts from, turned half round, is the target. */
			if (ModernWrap((long)sit->y_rot - sit->cardinal_off + MODERN_LEDGE_HALF_TURN - sit->target))
				return out->verdict;
		}

		if (!sit->key || sit->jump || !sit->hands_free || sit->hit_points <= 0 ||
			sit->hit_points < sit->entry_hit_points || sit->control_taken || sit->refusals)
			return out->verdict;

		/* Right is the positive way, the way IN_RIGHT turns her. */
		out->yaw_step = (sit->side < 0) ? -ModernLedgeTurnStep(sit->tick) : ModernLedgeTurnStep(sit->tick);

		if (sit->tick == 0)
			out->yaw_step -= sit->cardinal_off;

		out->phase = (sit->tick == MODERN_LEDGE_TURN_TICKS - 1) ? MODERN_LEDGE_LOWER : MODERN_LEDGE_TURN;
		out->verdict = MODERN_LEDGE_CONTINUE;
		return out->verdict;
	}

	if (sit->phase == MODERN_LEDGE_LOWER)
	{
		out->verdict = MODERN_LEDGE_HANG;

		if (sit->hit_points <= 0 || sit->control_taken ||
			sit->frame <= sit->frame_base || sit->frame > sit->frame_end)
			return out->verdict;

		if (sit->tick > 0)
		{
			out->frame_step = sit->frame - sit->frame_base;

			if (out->frame_step > sit->step && sit->step > 0)
				out->frame_step = sit->step;
		}

		out->phase = MODERN_LEDGE_LOWER;
		out->verdict = MODERN_LEDGE_CONTINUE;
		return out->verdict;
	}

	/* A phase this function does not know. */
	out->verdict = MODERN_LEDGE_STAND;
	return out->verdict;
}

/* The ledge hang's trigger, the arithmetic of it. A quad is the engine's
 * quadrant number of a heading, (ushort)(y_rot + 0x2000) / 0x4000: 0 facing
 * +z, 1 facing +x, 2 facing -z, 3 facing -x (NORTH, EAST, SOUTH, WEST in
 * global\types.h). The quad a ledge function takes is always the heading she
 * hangs with, which faces away from the drop.
 *
 * She runs toward the drop on the cardinal `approach`, and hangs facing the
 * other way: the quad two round from the approach's. */
long ModernLedgeHangQuad(short approach)
{
	return (((unsigned short)(approach + 0x2000) / 0x4000) + 2) & 3;
}

/* Units from (x, z) to the outermost unit of its sector on the drop side,
 * along the axis: 0 on that unit, 1023 on the far one. The drop lies at -z
 * for quad 0, at -x for 1, at +z for 2 and at +x for 3. The standing line is
 * at MODERN_LEDGE_STAND_IN; a quad outside the four gives -1. */
long ModernLedgeEdgeDist(long quad, long x, long z)
{
	switch (quad)
	{
	case 0:
		return z & 1023;

	case 1:
		return x & 1023;

	case 2:
		return 1023 - (z & 1023);

	case 3:
		return 1023 - (x & 1023);
	}

	return -1;
}

/* Moves (x, z) `shift` units along the axis toward the drop, the direction
 * she runs in; the other coordinate is kept. With the shift ModernLedgeFire
 * returns, a point short of the standing line lands on it exactly. */
void ModernLedgeStep(long quad, long shift, long* x, long* z)
{
	switch (quad)
	{
	case 0:
		*z -= shift;
		break;

	case 1:
		*x -= shift;
		break;

	case 2:
		*z += shift;
		break;

	case 3:
		*x += shift;
		break;
	}
}

/* The direct stop: whether the move starts on this tick, and how far forward
 * to put her. `dist` is ModernLedgeEdgeDist of her position after this
 * tick's movement, `speed` her speed along the axis - a run's 47, a walk's
 * 15, 0 for a stand.
 *
 * A moving Lara starts when she would reach the standing line on the next
 * tick (dist - MODERN_LEDGE_STAND_IN <= speed) and is put on it: the shift
 * is what is left to the line, 0 to one step, and never backwards. A stand
 * starts within MODERN_LEDGE_RESIDUAL of the line either side (the walk's own
 * edge stop leaves her 2 short of it). Past the line by more than the
 * residual she does not start, and past the edge is not this sector's edge
 * at all. */
long ModernLedgeFire(long dist, long speed, long* shift)
{
	const long step = (speed > MODERN_LEDGE_RESIDUAL) ? speed : MODERN_LEDGE_RESIDUAL;
	const long off = dist - MODERN_LEDGE_STAND_IN;

	*shift = 0;

	if (dist < 0 || dist > 1023 || off < -MODERN_LEDGE_RESIDUAL || off > step)
		return 0;

	if (off > 0)
		*shift = off;

	return 1;
}

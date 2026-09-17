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
 * THE WATER STATES carry "modern in A2 (camera pitch)" instead of a routine
 * name: A1 has no camera pitch to swim by, so they wait for the flat-circle
 * move and the swim pitch that A2 builds.
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
	/* Water: the flat-circle move and the swim pitch are A2's right stick,
	 * not a translation A1 can do with a fixed chase camera. */
	case 13:	// AS_TREAD, lara_as_tread - modern in A2 (camera pitch)
	case 17:	// AS_SWIM, lara_as_swim - modern in A2 (camera pitch)
	case 18:	// AS_GLIDE, lara_as_glide - modern in A2 (camera pitch)
	case 33:	// AS_SURFTREAD, lara_as_surftread - modern in A2 (camera pitch)
	case 34:	// AS_SURFSWIM, lara_as_surfswim - modern in A2 (camera pitch)
	case 35:	// AS_DIVE, lara_as_dive - modern in A2 (camera pitch)
	case 44:	// AS_UWDEATH, lara_as_uwdeath - modern in A2 (camera pitch)
	case 47:	// AS_SURFBACK, lara_as_surfback - modern in A2 (camera pitch)
	case 48:	// AS_SURFLEFT, lara_as_surfleft - modern in A2 (camera pitch)
	case 49:	// AS_SURFRIGHT, lara_as_surfright - modern in A2 (camera pitch)
	/* The three that stand in the water list without swimming in it: two
	 * dives in the air and the climb out onto the bank. They are classic
	 * because THIS ROUND'S OWN BUILD BRIEF names them in its water list -
	 * NOT because TROne do, which is what this comment said until the second
	 * review pass measured it: their trone_modern_state_is_classic has 52,
	 * 53 and 55 in neither the list nor the prose above it. The reason is
	 * sound either way - a dive is how she gets INTO the water and the climb-
	 * out is how she leaves it, so both belong to the same handover - but it
	 * is not the camera pitch, so the line above must not be read as
	 * promising them one, and it is not TROne's, so they must not be quoted
	 * for it. */
	case 52:	// AS_SWANDIVE, lara_as_swandive - the entry, not the swim
	case 53:	// AS_FASTDIVE, lara_as_fastdive - the entry, not the swim
	case 55:	// AS_WATEROUT, lara_as_waterout - the way out, not the swim
		return 1;

	/* The underwater 180: classic like the water states above (the routine
	 * is next to lara_as_swim/glide in game\laraswim.cpp), but not tagged
	 * for A2 - it is a short fixed maneuver, not the held swim/glide loop
	 * the camera pitch is for. */
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

/* How far she may turn this tick: the error, clamped to the rate, and zero
 * wherever the write is not allowed. `rate` 0 gives 0 everywhere, which is
 * the whole feature switched off and the turn bits back in charge - that is
 * what MODERN_DIRECT_TURN is for and why the rate is a parameter and not a
 * constant read in here.
 *
 * The rate is clamped to half a turn as well. Nothing sets it that high, but
 * a rate over 0x8000 would make "clamp the error to the rate" meaningless
 * and the clamp costs one comparison. */
long ModernGroundTurn(short anim_state, long delta, long rate)
{
	if (rate <= 0 || !ModernStateAllowsDirectTurn(anim_state))
		return 0;

	delta = ModernWrap(delta);

	if (rate > 0x4000L)
		rate = 0x4000L;

	if (delta > rate)
		return rate;

	if (delta < -rate)
		return -rate;

	return delta;
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
 * THE INSTANT ABOUT-TURN. Past 90 degrees of error the whole turn is taken in
 * ONE tick - but only while she is REALLY running. TROne measured why:
 * standing still, a one-tick reversal is spent before the player has both
 * BACK and JUMP down, so the back flip becomes unreachable and every jump
 * goes forward. Standing, the bounded rate therefore stands, and a half turn
 * takes ten ticks - time enough to press the two together. */
long ModernDirectTurn(const MODERN_CFG* cfg, MODERN_STATE* state, const MODERN_SITUATION* sit,
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

	/* No state number, no write: the state list above is the only thing
	 * standing between this and a crooked pose, so a caller that cannot say
	 * where Lara is does not get the direct turn. */
	if (!sit || sit->classic)
		return 0;

	/* Aiming with the strafe switched off comes out as mode RUN, and there
	 * the game is already turning her onto her target - two hands on the
	 * same heading. Mode AIM never reaches this line at all. */
	if (sit->drawn && sit->action)
		return 0;

	if (!cfg->direct_turn || cfg->turn_rate <= 0)
		return 0;

	if (!ModernStateAllowsDirectTurn(sit->anim_state))
		return 0;

	yaw = ModernGroundTurn(sit->anim_state, out->delta, cfg->turn_rate);

	if (ModernAbs(out->delta) > cfg->snap_delta && !ModernStateIsStanding(sit->anim_state))
		yaw = out->delta;

	remaining = ModernAbs((long)out->delta - yaw);

	out->bits &= ~(long)MODERN_DIR_MASK;

	if (remaining <= cfg->snap_forward)
		out->bits |= MODERN_IN_FORWARD;

	out->turning = (remaining > 0) ? 1 : 0;

	/* The spin is the automaton's way of turning on the spot and we have
	 * just taken the turning away from it. Leaving the flag set would keep
	 * IN_FORWARD off for ticks after the turn is done. */
	if (state)
		state->spin = 0;

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
	 * to aiming. */
	state->aiming = (cfg->aim_strafe && sit->drawn && sit->action && !sit->jump) ? 1 : 0;

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

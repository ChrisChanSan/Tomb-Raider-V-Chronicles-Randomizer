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
#pragma once

/* THE MODERN CONTROL - THE PART THAT IS PURE ARITHMETIC (#161, round 161a).
 *
 * The modern control is not a new way of moving. It is a translation: the
 * stick's direction, read against the camera and against a heading this file
 * remembers, becomes exactly the IN_FORWARD / IN_BACK / IN_LEFT / IN_RIGHT /
 * IN_LSTEP / IN_RSTEP / IN_WALK bits the keyboard has produced since 1996.
 * Lara's automaton does not change at all, and neither does her turn rate -
 * we set the turn BIT and game\lara.cpp does the turning. That is the whole
 * reason the idea survives a 1:1 engine, and it is why the file below has no
 * engine in it: it takes eight numbers and fills a result.
 *
 * It includes NO engine header on purpose. It has to compile on its own,
 * because Preparation\Tools\Diagnose\ModernTest.cpp compiles THIS FILE - not
 * a copy of it - and asserts on it, the way InterpTest.cpp does with
 * game\interpolate_math.h. There is no twin to keep in step.
 *
 * THE DESIGN IS TROne'S, RE-DERIVED HERE. TROne (GPLv3) by Todesengel1931,
 * https://github.com/todesengel1931, solved this first and measured why the
 * obvious way round does not work (see the held course below). Our units are
 * not theirs - our stick arrives as about +-128 where theirs is +-32767 -
 * our state list is TR5's own and runs to 138 where theirs stops at 104, and
 * the voice is ours. Nothing is copied. What IS taken across are the test
 * expectations, and ModernTest.cpp says so in its own header.
 *
 * ANGLES are the engine's own 16-bit units: 65536 is a full turn, 0 is +Z,
 * 0x4000 is +X - the convention of item->pos.y_rot and of phd_atan(z, x).
 *
 * WHAT IS NOT HERE. The per-tick state machine, the blocked counter, the
 * direct turn, the log and the seam in specific\input.cpp are
 * game\modern_state.cpp. The table of states that stay classic is below as a
 * declaration only; round 161a step 3 writes it out state by state. */

/* THE INPUT BITS, MIRRORED BY VALUE.
 *
 * global\types.h's input_buttons cannot be included here - it needs the whole
 * Direct3D type set behind it - so the bits this file can write are mirrored
 * by value, exactly as game\interpolate_math.h mirrors camera_type. The
 * static_assert that says they still agree lives with the first engine file
 * that has both headers open, game\modern_state.cpp; if the enum in
 * global\types.h ever moves, the build stops there rather than here. */
enum modern_input_bits
{
	MODERN_IN_FORWARD =	0x1,	// IN_FORWARD, global\types.h
	MODERN_IN_BACK =	0x2,	// IN_BACK
	MODERN_IN_LEFT =	0x4,	// IN_LEFT
	MODERN_IN_RIGHT =	0x8,	// IN_RIGHT
	MODERN_IN_DRAW =	0x20,	// IN_DRAW - the weapon half below, nothing else
	MODERN_IN_WALK =	0x80,	// IN_WALK
	MODERN_IN_LSTEP =	0x400,	// IN_LSTEP
	MODERN_IN_RSTEP =	0x800,	// IN_RSTEP

	/* Exactly the bits ModernTranslate can write, and therefore exactly the
	 * bits the seam clears before it puts a result in (step 4). IN_DRAW is
	 * NOT in it: the weapon half hands its bits back rather than replacing
	 * them, and the movement half never touches the draw key. */
	MODERN_MOVE_MASK =	MODERN_IN_FORWARD | MODERN_IN_BACK | MODERN_IN_LEFT |
						MODERN_IN_RIGHT | MODERN_IN_WALK | MODERN_IN_LSTEP |
						MODERN_IN_RSTEP
};

/* What came out, for the log and for the tests. The translation itself makes
 * no decision from it. */
enum modern_mode
{
	MODERN_MODE_OFF = 0,	// dead zone, or a state that stays classic
	MODERN_MODE_RUN = 1,	// turn and run - the normal case
	MODERN_MODE_AIM = 2,	// weapon up and action held: strafe, do not turn
	MODERN_MODE_JUMP = 3,	// the directional jump or flip out of a stand
	MODERN_MODE_BACK = 4	// the classic step back (back_turns switched off)
};

/* EVERY CONSTANT OF THE SCHEME, WITH WHERE IT COMES FROM.
 *
 * The row numbers are section 3 of Plan Preparation's MODERN-CONTROLS-DESIGN,
 * the taboo table: every number in the modern control was traced to a stated
 * origin there, and nothing in the set comes from a Remaster binary. A row
 * whose verdict is "measure" carries its measurement plan here, because a
 * feel number that nobody can re-measure is a number nobody can change.
 *
 * ModernDefaults() below is the only thing that reads them. */
enum modern_constants
{
	/* Row 2, MEASURE. TROne's dead zone is 8000 of 32767, 24 per cent. Ours
	 * starts at nothing, because XInput's own XINPUT_GAMEPAD_LEFT_THUMB_
	 * DEADZONE (7849 of 32767 - the same 24 per cent) is already applied in
	 * specific\dxshell.cpp before ReadJoystick hands the axes over, and the
	 * DirectInput fallback applies 150 of 1000 there. A second dead zone on
	 * top of the hardware one would only eat travel.
	 * The plan if the user reports drift: MODERNLOG prints mag, raise this in
	 * steps of 4 (3 per cent of full deflection) and read the log again. Note
	 * for that measurement: the classic path ignores anything up to +-8
	 * (specific\input.cpp), so 1..8 is the band where Modern reacts and Tank
	 * does not. */
	MODERN_DEAD = 0,

	/* Row 5. The RULE is TROne's and comes from play - under half deflection
	 * Lara walks - and the number is theirs too: 16000 of 32767. Scaled to
	 * our full deflection that is 16000 * 128 / 32767 = 62.5, so 62. The
	 * verdict on the number is "measure": only the play test says whether
	 * half a stick is where walking should start. */
	MODERN_WALK_BELOW = 62,

	/* Row 6. TROne's anti-chatter window: inside 2.8 degrees of her course
	 * Lara counts as aligned and no turn bit goes out. */
	MODERN_AIM_WINDOW = 0x0400,

	/* Row 7. Once aligned, it takes twice the window to start turning again.
	 * Without the hysteresis the turn bit flickers on the window's edge at
	 * tick rate, which is a visible twitch at small angles. */
	MODERN_ALIGNED_FACTOR = 2,

	/* Rows 8 and 9. Above 112.5 degrees of error she turns on the spot and
	 * does not run; she starts running again below 56.25. Two thresholds and
	 * not one, so a 180 degree flick cannot chatter between them. */
	MODERN_SPIN_ENTER = 0x5000,
	MODERN_SPIN_EXIT = 0x2800,

	/* Row 10. TROne measured this one from a failed acceptance of their own:
	 * standing still, the classic automaton can only express "turn" WITHOUT
	 * IN_FORWARD (game\lara.cpp - lara_as_stop turns IN_LEFT/IN_RIGHT into
	 * AS_TURN_L/AS_TURN_R and a simultaneous IN_FORWARD overwrites the goal
	 * with run or walk), so with a wall in front of her she would stop
	 * responding altogether. The cause is in our lara.cpp too, line for line,
	 * which is why this is not optional. 67.5 degrees, half that to leave. */
	MODERN_STAND_SPIN = 0x3000,

	/* Rows 11 and 12, the two numbers that make the held course work. Under
	 * 2.1 degrees of stick movement nothing happens at all - that is the
	 * noise floor of a real thumb. Between that and 22.5 degrees the held
	 * course turns by exactly as much as the stick did, and the camera is not
	 * consulted, which is what stops a camera cut from bending her path. At
	 * 22.5 degrees and more the player meant a new direction, so the course
	 * is seated on the camera of THIS tick again. */
	MODERN_STEER_STEP = 0x0180,
	MODERN_RECENTER = 0x1000,

	/* Row 13. The quadrant boundary: within 45 degrees of her nose the
	 * direction is "forward", within 45 of her back it is "back", and the
	 * rest is a side step. Geometry, not taste. */
	MODERN_AIM_SECTOR = 0x2000,

	/* Rows 14 and 15, read by game\modern_state.cpp (step 4), not by the
	 * translation. Lara counts as blocked when she has moved less than 4
	 * world units for 6 ticks while she was asking to go forward AND to
	 * turn. Six ticks is a fifth of a second: fewer and the first ticks of a
	 * start from a standstill look like a wall. */
	MODERN_BLOCK_FRAMES = 6,
	MODERN_BLOCK_EPS = 4,

	/* Row 16, MEASURE, and the one number the user will want to change.
	 * TROne write y_rot themselves at 3277 per tick, 18 degrees, because the
	 * game's own fastest rate is not fast enough for the feel they were
	 * after. Ours, measured in game\lara.cpp: run 1456 (8 degrees), walk 728,
	 * turn on the spot 728 rising to 1456 in AS_FASTTURN, back 728, fast back
	 * 1092, jump 546. So 3277 is 2.25 times anything vanilla does.
	 * The other two candidates are written out below on purpose: the play
	 * test picks one of the three and then it is frozen. MODERNLOG prints the
	 * rate in its init line and the turn of each tick in yawd. */
	MODERN_TURN_RATE = 3277,	// candidates: 1456 = vanilla's fastest, 2184, 3277 = TROne
	MODERN_TURN_RATE_VANILLA = 1456,
	MODERN_TURN_RATE_MIDDLE = 2184,

	/* The second gate of the direct turn, the user's decision 5: the write to
	 * y_rot sits behind tomb5.control_modern AND behind this. One is the
	 * scheme, the other is this one mechanism inside it - so a play test can
	 * answer "is modern wrong, or is only the direct turn wrong?" without a
	 * rebuild of the scheme. Step 4 is the only reader. */
	MODERN_DIRECT_TURN = 1,

	/* Rows 17 and 18, read by step 4 as well. Past 90 degrees of error, and
	 * only while she is really running, the whole turn is taken in one tick -
	 * that is the instant about-turn, and it is deliberate. IN_FORWARD only
	 * comes back when 45 degrees or less are left, so she never takes a step
	 * in the wrong direction while the turn finishes. */
	MODERN_SNAP_DELTA = 0x4000,
	MODERN_SNAP_FORWARD = 0x2000,

	/* Full deflection in OUR units. specific\dxshell.cpp scales XInput's
	 * -32768..32767 down by 256, so a fully pushed stick reads about 128; the
	 * DirectInput fallback ranges to -1000..1000 and divides by 8, so about
	 * 125. Only the magnitude is scaled by this - the direction is an angle
	 * and does not care. */
	MODERN_STICK_FULL = 128,

	/* The five switches of the scheme. They are constants and not settings:
	 * the user's decision is that Modern is one thing you turn on, not a page
	 * of sliders. They exist because each of them turns off exactly one rule
	 * of the design, which is how a play test can bisect a complaint.
	 *   hold_course	the world course of section 1.2 (off = the camera of
	 *					every tick is the reference, which is the version
	 *					TROne measured spinning in circles)
	 *   back_turns		pulling back turns her around (off = a classic step
	 *					backwards)
	 *   aim_strafe		weapon up and action held strafes instead of turning
	 *   jump_dir		the standing jump and the flips pick a direction
	 *   block_turn		a blocked forward becomes a pure turn */
	MODERN_HOLD_COURSE = 1,
	MODERN_BACK_TURNS = 1,
	MODERN_AIM_STRAFE = 1,
	MODERN_JUMP_DIR = 1,
	MODERN_BLOCK_TURN = 1
};

/* The thresholds, in one place, filled by ModernDefaults(). Every field is a
 * long so that nothing in the translation has to convert: the angles are
 * magnitudes of 0..32768 and the flags are 0 or 1. */
struct MODERN_CFG
{
	long dead;			// stick magnitude under which nothing happens
	long walk_below;	// magnitude under which IN_WALK goes out as well
	long aim_window;	// the window inside which she counts as aligned
	long aligned_factor;// how much wider that window is once she is aligned
	long spin_enter;	// error at which she stops running and only turns
	long spin_exit;		// error under which she runs again
	long stand_spin;	// the same, standing still; 0 switches the rule off
	long steer_step;	// stick movement under which the course does not move
	long recenter;		// stick movement at which the course re-seats
	long aim_sector;	// the forward/back quadrant boundary
	long block_frames;	// step 4: ticks of not moving that count as blocked
	long block_eps;		// step 4: world units that still count as not moving
	long turn_rate;		// step 4: the direct turn, units per tick
	long direct_turn;	// step 4: the direct turn's own gate
	long snap_delta;	// step 4: error above which the turn is taken at once
	long snap_forward;	// step 4: error under which IN_FORWARD comes back
	long stick_full;	// full deflection in our units
	long hold_course;	// hold a world course instead of following the camera
	long back_turns;	// back on the stick turns her around
	long aim_strafe;	// strafe instead of turning while aiming
	long jump_dir;		// directional jump and flips out of a stand
	long block_turn;	// a blocked forward becomes a pure turn
};

/* What the translation remembers from tick to tick. Nothing here is an engine
 * value; game\modern_state.cpp owns the one instance of it and resets it on a
 * level start, on a death and when the scheme is switched. */
struct MODERN_STATE
{
	long have_ref;		// 0 = the next push of the stick seats a fresh course
	short ref;			// the camera heading the course was seated on
	short course;		// the held WORLD course - the core of the design
	short last_off;		// the stick angle the course was last drawn from
	long spin;			// turning on the spot (hysteresis between the two)
	long aligned;		// the wider window of MODERN_ALIGNED_FACTOR applies
	long aiming;		// the strafe mode held last tick
	long blocked_count;	// step 4's counter of ticks she did not get anywhere
	long last_mode;		// the mode the last tick ended in, for the log
};

/* What the caller knows about Lara this tick. A struct of zeroes gives plain
 * running on land, which is what the tests use when they mean "no situation".
 * TROne carry a `walk` field here as well and never read it - walking is what
 * walk_below decides - so ours does not have one. */
struct MODERN_SITUATION
{
	short anim_state;	// item->current_anim_state
	long classic;		// the caller says: leave this tick classic
	long drawn;			// a weapon is ready (gun_status == LG_READY, no torch)
	long action;		// the action key is down
	long jump;			// the jump key is down
	long blocked;		// step 4's counter says forward is not going anywhere
};

struct MODERN_RESULT
{
	long bits;			// MODERN_IN_* - finished input bits, nothing else
	long active;		// 1 = the stick is out and the bits above apply
	short target;		// the world course she should be on (step 4 turns to it)
	short delta;		// target minus her heading, signed, the short way round
	long mag;			// stick deflection, 0..stick_full
	short reference;	// the camera heading the course was seated on
	long mode;			// modern_mode, for the log and the tests
	long blocked;		// the blocked rule took IN_FORWARD away this tick
	long turning;		// a turn bit is in `bits`
};

void ModernDefaults(MODERN_CFG* cfg);
void ModernResetState(MODERN_STATE* state);
short ModernWrap(long angle);
short ModernAngle(long x, long z);
long ModernStateIsClassic(short anim_state);
long ModernStateIsGroundIdle(short anim_state);
long ModernStateIsStanding(short anim_state);
long ModernWeaponBits(long combat_hold, long ready, long holstered, long held, long bits);
void ModernTranslate(const MODERN_CFG* cfg, MODERN_STATE* state, long stick_x, long stick_y,
	short camera_yaw, short lara_yaw, const MODERN_SITUATION* sit, MODERN_RESULT* out);

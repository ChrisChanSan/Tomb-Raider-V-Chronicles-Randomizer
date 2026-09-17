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
 * WHAT IS NOT HERE. The per-tick state machine, the blocked counter, the log
 * and the seam in specific\input.cpp are game\modern_state.cpp. The DIRECT
 * TURN is split between the two: its arithmetic is here (ModernGroundTurn and
 * ModernDirectTurn, step 4), because a rule that decides how far Lara turns
 * has to be a number a test can check, and only this file is testable; the
 * one line that actually writes lara_item->pos.y_rot is over there, because
 * that is the engine. THE WATER is split on exactly the same seam (round
 * 161a2 step 4): ModernWaterMove here decides the turn, the attitude and the
 * stroke as three numbers, and the three lines that write y_rot, x_rot and
 * z_rot are over there. */

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
	MODERN_IN_JUMP =	0x10,	// IN_JUMP - under water it is the stroke (step 4)
	MODERN_IN_DRAW =	0x20,	// IN_DRAW - the weapon half below, nothing else
	MODERN_IN_WALK =	0x80,	// IN_WALK
	MODERN_IN_LSTEP =	0x400,	// IN_LSTEP
	MODERN_IN_RSTEP =	0x800,	// IN_RSTEP

	/* The six bits that say WHERE she goes. This is what the direct turn
	 * replaces when it takes the turning into its own hand (step 4): it
	 * turns her by writing y_rot, so it must take the turn bits away, but
	 * IN_WALK is deliberately not in here - how far the stick is out still
	 * decides whether she walks, turn or no turn. */
	MODERN_DIR_MASK =	MODERN_IN_FORWARD | MODERN_IN_BACK | MODERN_IN_LEFT |
						MODERN_IN_RIGHT | MODERN_IN_LSTEP | MODERN_IN_RSTEP,

	/* Exactly the bits ModernTranslate can write, and therefore exactly the
	 * bits the seam clears before it puts a result in (step 4). IN_DRAW is
	 * NOT in it: the weapon half hands its bits back rather than replacing
	 * them, and the movement half never touches the draw key. */
	MODERN_MOVE_MASK =	MODERN_DIR_MASK | MODERN_IN_WALK
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
	 * steps of 4 (3 per cent of full deflection) and read the log again.
	 * WHAT THAT MEASUREMENT IS NOT LOOKING FOR, corrected in the second review
	 * pass of this round: a band where Modern moves her and Tank does not.
	 * There is none. The classic path ignores anything up to +-8
	 * (specific\input.cpp), and no joystick path can deliver a reading in
	 * 1..8 at all - XInput zeroes an axis under 7849 and then divides by 256,
	 * so its smallest non-zero reading is 30, and the DirectInput fallback
	 * zeroes under 150 and divides by 8, so its smallest is 18
	 * (specific\dxshell.cpp, both paths). Every reading either scheme ever
	 * sees is one both react to; what Modern USED TO add was the MAGNITUDE
	 * deciding walking from running - round 161a-fix (#161 item 1) took that
	 * back out (see walk_below below), so mag= is read only as a drift/dead-
	 * zone diagnostic now, the way this row always meant it. */
	MODERN_DEAD = 0,

	/* Row 5, MEASURE - ANSWERED BY THE FIRST PLAY TEST (2026-09-17). The RULE
	 * was TROne's and came from play - under half deflection Lara walks -
	 * and the number was theirs too: 16000 of 32767, scaled to our full
	 * deflection as 16000 * 128 / 32767 = 62.5, so 62, which is what this
	 * constant shipped with before this round.
	 * THE LOG (MODIFICATIONS.md, round 161a-fix) measured why a magnitude
	 * threshold cannot answer "is the walk key down" at all from a real
	 * stick: of 32 pushes from rest the FIRST tick read 20-60 of 128 in 19 of
	 * them - the stick is still travelling, not yet at the speed the player
	 * asked for - and the SECOND read 100 or more in 25 of them, so a single
	 * low tick in between latched the automaton into AS_WALK for several
	 * ticks while every tick around it said run: 24 one-tick and 2 two-tick
	 * walk episodes, seen by the player as "Lara sneaks on every direction
	 * change". THE USER'S DECISION: the pad already has its own walk key (R1
	 * in our layout), so the STICK must never be able to set IN_WALK at all -
	 * not a smaller number, zero. The constant and cfg->walk_below both stay,
	 * because TROne's capability is still real and still testable with an
	 * explicit cfg (ModernTest's walk_threshold() does exactly that); it is
	 * simply not what ships. */
	MODERN_WALK_BELOW = 0,

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

	/* Rows 11 and 12, the two numbers that make the held course work - AND
	 * ROW 12's OWN STORY, changed twice by two measurements of the same log
	 * (round 161a-fix, #161 item 2; the first change shipped and is now
	 * superseded, not amended - see MODIFICATIONS.md). Under steer_step (2.1
	 * degrees) nothing happens at all - that is the noise floor of a real
	 * thumb, and it has not moved.
	 *
	 * WHAT RECENTER USED TO BE: TROne's own 0x1000 (22.5 degrees). Under it, a
	 * stick change re-steers the held course by exactly that much and the
	 * camera is not consulted; at or over it, a new direction was meant and
	 * the course re-seats on the camera of THIS tick. The first play test's
	 * log (2026-09-17) measured this window catching ordinary circling - the
	 * user turns the stick through a full rotation in 0.3-0.6 seconds, 20-40
	 * degrees a tick, so nearly every tick of a circle re-seated on a camera
	 * that had meanwhile swung behind her, "circles break off at a certain
	 * angle" - and the first answer tried was to widen the window to 0x2000
	 * (45 degrees).
	 *
	 * A SECOND MEASUREMENT OF THE SAME LOG, one play session later, showed
	 * why widening the window is not the answer either: ticks 470-508
	 * (t=53.0s), the stick turning slowly from right (125/0) to back-right
	 * (85/105) over 39 ticks - 51 degrees of stick rotation, every single
	 * per-tick step small enough to stay under EITHER window - while the
	 * chase camera swung 120 degrees behind her. The course followed only the
	 * STICK's 51 degrees (fine steering never re-seats, no matter how wide
	 * the window catching it is), ending up 50 degrees short of where the
	 * player meant it, because he was reading the stick against the camera he
	 * could see at that instant, not against a course seeded once and merely
	 * nudged since: "solche Bewegungen schräg nach hinten sind bei uns
	 * unmöglich."
	 *
	 * THE REMASTER READS THE STICK AGAINST THE CURRENT CAMERA EVERY TICK, AND
	 * GETS AWAY WITH IT, because its camera does not swing to stay behind Lara
	 * on its own. Ours does - and reading the camera on every tick regardless
	 * of the stick is exactly the feedback TROne measured and built the held
	 * course to stop (section 1.2): a HELD stick would spin her forever,
	 * because turning her swings the camera, which re-reads as a new target,
	 * which turns her further.
	 *
	 * THE COMPROMISE, AND WHAT SHIPS NOW: recenter equals steer_step. Any
	 * stick change at all above the noise floor re-seats on the camera of
	 * that tick - which reads like the Remaster for exactly as long as the
	 * thumb is actually moving, the case the second measurement is about -
	 * and a stick that is NOT changing keeps its held course untouched no
	 * matter how the camera swings under it, the one property the first
	 * measurement (and TROne's own circling finding) demands and that this
	 * change does not touch. The fine-steering band this leaves empty is not
	 * deleted: recenter and steer_step stay two separate cfg fields, so
	 * TROne's own three-band number (0x1000) stays provable under an explicit
	 * cfg (ModernTest's held_course()). */
	MODERN_STEER_STEP = 0x0180,
	MODERN_RECENTER = MODERN_STEER_STEP,

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

	/* THE WATER (round 161a2, step 4), and every rate in it is the ENGINE'S
	 * OWN - measured out of game\laraswim.cpp and game\larasurf.cpp before a
	 * line of this was written, because the user's rule for the water is that
	 * it must feel like the land and not that it must feel like something new:
	 * "Die Steuerung unter Wasser ist genau gleich wie ausserhalb, da ist kein
	 * Unterschied." So the SCHEME changes and the SPEEDS do not.
	 *
	 * Turning. Under water the classic never writes y_rot directly: SwimTurn
	 * (laraswim.cpp:196) ramps lara.turn_rate by 409 a tick and LaraUnderWater
	 * (:581) adds it to y_rot and decays it by 364 - but both of them stop at
	 * +-1092, so 1092 units a tick, 6 degrees, is the fastest TR5 has ever
	 * turned her under water and it is what the direct turn is given here. On
	 * the surface the states write y_rot themselves: 728 in lara_as_surftread
	 * and lara_as_surfswim (larasurf.cpp:207, :315), 364 in the three side and
	 * back strokes (:234, :258, :282). Each state therefore gets its own
	 * number rather than an average of them - ModernWaterTurnRate is the
	 * table. */
	MODERN_SWIM_TURN_RATE = 1092,	// LaraUnderWater's own ceiling on lara.turn_rate
	MODERN_SURF_TURN_RATE = 728,	// lara_as_surftread / lara_as_surfswim
	MODERN_SURF_SIDE_RATE = 364,	// lara_as_surfback / surfleft / surfright

	/* The pitch. SwimTurn's own step for IN_FORWARD/IN_BACK is 364 a tick, 2
	 * degrees, which is also the look camera's rate - so following the orbit
	 * costs exactly as long as pitching by hand used to. The limits are
	 * LaraUnderWater's own clamp on x_rot (laraswim.cpp:593-596), the same
	 * +-15470 the chase camera clamps its elevation to, which is why the
	 * camera can never point anywhere her body cannot follow. */
	MODERN_SWIM_PITCH_RATE = 364,
	MODERN_SWIM_PITCH_MIN = -15470,
	MODERN_SWIM_PITCH_MAX = 15470,

	/* The roll into the turn. SwimTurn banks her by 546 a tick while a turn
	 * bit is held and LaraUnderWater decays it by 364 and clamps at +-4004, so
	 * a held turn settles at the clamp and a released one unwinds. The direct
	 * turn takes the turn BITS away, so it owes the bank they would have made:
	 * this is that same 546, written on the ticks the modern turn really
	 * turns her, and the decay and the clamp are still the engine's. */
	MODERN_SWIM_ROLL_RATE = 546,

	/* THE ONE NUMBER IN THE WATER THAT IS NOT MEASURED BUT DECIDED, and it is
	 * the user's own decision rather than a feel constant of ours: "Unter
	 * Wasser ist der linke Stick wie ein D-Pad, was in alle Richtungen 360
	 * Grad auf einem flachen Kreis Lara in die Richtung BEWEGT." A stick that
	 * moves her cannot also need the jump key held to make her move, so with
	 * this on the stroke is held for exactly as long as the stick is out.
	 * 0 restores the classic "jump = stroke" with the modern course still
	 * steering, which is the one-line answer if the play test dislikes it -
	 * and the reason it is a switch and not a rewrite. The surface is not
	 * affected either way: there the stroke IS the forward bit the course
	 * already produces (lara_as_surftread -> AS_SURFSWIM), and IN_JUMP means
	 * "dive" instead (larasurf.cpp:329 counts ten ticks of it). */
	MODERN_WATER_AUTO_STROKE = 1,

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
	long swim_turn_rate;// step 4: the direct turn under water, units per tick
	long surf_turn_rate;// step 4: the same on the surface, treading and swimming
	long surf_side_rate;// step 4: and in the back and side strokes
	long swim_pitch_rate;// step 4: how fast x_rot follows the orbit's elevation
	long swim_pitch_min;// step 4: the engine's own clamp on x_rot
	long swim_pitch_max;
	long swim_roll_rate;// step 4: the bank the turn bits would have made
	long water_auto_stroke;// step 4: the stroke is held while the stick is out
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
long ModernStateIsSwim(short anim_state);
long ModernWaterTurnRate(const MODERN_CFG* cfg, short anim_state);
long ModernWaterMove(const MODERN_CFG* cfg, const MODERN_SITUATION* sit, MODERN_RESULT* out,
	long target_pitch, long x_rot, long* yaw_delta, long* new_x_rot, long* stroke);
long ModernStateIsGroundIdle(short anim_state);
long ModernStateIsStanding(short anim_state);
long ModernStateAllowsDirectTurn(short anim_state);
long ModernGroundTurn(short anim_state, long delta, long rate);
long ModernDirectTurn(const MODERN_CFG* cfg, MODERN_STATE* state, const MODERN_SITUATION* sit,
	MODERN_RESULT* out, long* yaw_delta);
long ModernWeaponBits(long combat_hold, long ready, long holstered, long held, long bits);
void ModernTranslate(const MODERN_CFG* cfg, MODERN_STATE* state, long stick_x, long stick_y,
	short camera_yaw, short lara_yaw, const MODERN_SITUATION* sit, MODERN_RESULT* out);

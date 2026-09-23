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

/* Modern controls, the pure arithmetic of the movement half.
 *
 * The modern control is a translation, not a new way of moving: the stick's
 * direction, read against the camera and a heading held here, becomes the
 * IN_FORWARD / IN_BACK / IN_LEFT / IN_RIGHT / IN_LSTEP / IN_RSTEP / IN_WALK
 * bits the keyboard produces. Lara's state machine is unchanged. The file
 * includes no engine header so that Preparation\Tools\Diagnose\ModernTest.cpp
 * can compile this very file and assert on it.
 *
 * Origin: the design is re-derived from TROne (GPLv3) by Todesengel1931 -
 * https://github.com/todesengel1931 (the version of 2026-09-13), whose own
 * docs\CONTROLS.md sets out the arithmetic and proves the held-heading rule
 * with a recording of its own. TROne's modern control is its own design, not
 * a reading of any program - its input code cites no disassembler or
 * decompilation - and no code of TROne's is copied here. Units, state list and
 * wording here are TR5's own; ModernTest.cpp takes over TROne's test
 * expectations. Of the 32 constants, 16 carry a stated origin, 9 are measured
 * in play of this game, 5 do not apply and 2 of unknown origin are not used. The only fact from the
 * Remaster is a set of option names read from the player's own COMMON.TXT; no
 * Remaster program is used, and the Remaster's modern control is rebuilt only
 * from how it plays.
 *
 * Angles are the engine's 16-bit units: 65536 is a full turn, 0 is +Z, 0x4000
 * is +X (item->pos.y_rot, phd_atan(z, x)). The per-tick state machine, the
 * blocked counter, the log and the lines that write y_rot / x_rot / z_rot live
 * in game\modern_state.cpp; the arithmetic that decides the turn on land and
 * in water is here, so that a test can check it. */

/* The input bits, mirrored by value: global\types.h's input_buttons needs the
 * whole Direct3D type set and cannot be included here. game\modern_state.cpp
 * sees both headers and static_asserts that the values still agree. */
enum modern_input_bits
{
	MODERN_IN_FORWARD =	0x1,	// IN_FORWARD, global\types.h
	MODERN_IN_BACK =	0x2,	// IN_BACK
	MODERN_IN_LEFT =	0x4,	// IN_LEFT
	MODERN_IN_RIGHT =	0x8,	// IN_RIGHT
	MODERN_IN_JUMP =	0x10,	// IN_JUMP - under water it is the stroke
	MODERN_IN_DRAW =	0x20,	// IN_DRAW - the weapon half below, nothing else
	MODERN_IN_ACTION =	0x40,	// IN_ACTION - Grab mode
	MODERN_IN_WALK =	0x80,	// IN_WALK
	MODERN_IN_LSTEP =	0x400,	// IN_LSTEP
	MODERN_IN_RSTEP =	0x800,	// IN_RSTEP
	/* The two bits the one action key raises beside IN_ACTION under "One key"
	 * (specific\input.cpp's action_bits). Nothing here writes them; they are
	 * mirrored because the caller composes the grab latch's let-go mask from
	 * them and ModernTest builds a real "One key" word to check it. */
	MODERN_IN_INTERACT =	0x4000,		// IN_INTERACT - the world-object half
	MODERN_IN_SELECT =	0x100000,	// IN_SELECT - the menus', never the latch's
	MODERN_IN_DUCK =	0x20000000,	// IN_DUCK - Duck mode, and nothing else here
	MODERN_IN_SPRINT =	0x40000000,	// IN_SPRINT - Sprint mode

	/* The six bits that say where she goes. The direct turn writes y_rot
	 * itself and so takes these away; IN_WALK is not in here, because the
	 * walk decision stands whether she turns or not. */
	MODERN_DIR_MASK =	MODERN_IN_FORWARD | MODERN_IN_BACK | MODERN_IN_LEFT |
						MODERN_IN_RIGHT | MODERN_IN_LSTEP | MODERN_IN_RSTEP,

	/* Exactly the bits ModernTranslate can write, and so the bits the seam
	 * clears before it puts a result in. IN_DRAW is not in it: the weapon half
	 * hands its bits back rather than replacing them. */
	MODERN_MOVE_MASK =	MODERN_DIR_MASK | MODERN_IN_WALK
};

/* The whole memory of the mode latches: two bits per mode in one long the
 * caller owns - the pure function decides, the engine keeps the state.
 *
 * MODERN_LATCH_KEY is the key as it stood last tick and makes the press an
 * edge. It is tracked on every tick the option is on, also where the latch may
 * not stand, so a key held across a level boundary or out of a menu does not
 * arrive as a fresh press. */
enum modern_latch_bits
{
	MODERN_LATCH_ON =	0x1,	// the bit is latched on
	MODERN_LATCH_KEY =	0x2,	// the key or button was down on the previous tick

	/* Grab mode's own latch bit. Whether she is holding on is read from her
	 * state (the grab family table); this bit remembers that the current press
	 * is the one that let go, so the same press cannot grab the next ledge on
	 * the way down. Set on a press inside the family, cleared when that press
	 * ends. MODERN_LATCH_KEY above is shared with the same meaning. */
	MODERN_GRAB_RELEASE =	0x4
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

/* Every constant of the scheme, with where it comes from. ModernDefaults() is
 * the only reader, except for the ledge hang's group at the end. */
enum modern_constants
{
	/* TROne's dead zone is 8000 of 32767 (24 per cent). Here it is 0:
	 * the modern path reads the raw stick pair and puts it through the radial
	 * zone below (ModernStickDeadZone), so a second zone here would only eat
	 * travel. MODERNLOG prints mag; against drift, raise in steps of 4.
	 * The radial zone rescales, so the smallest reading it hands on is 1. Where
	 * the classic assembly steers from the same pair instead (first person, the
	 * look key, a cutscene - specific\input.cpp), anything under +-8 is still
	 * ignored, which adds a few units of dead band just past the rim there. */
	MODERN_DEAD = 0,

	/* The radial dead zones. Both pad paths zero each axis on its own, so a
	 * stick held a few degrees off an axis arrives on that axis: the smaller
	 * component is dropped and the course moves in jumps. Measured direction
	 * histogram: 68 / 41 / 48 / 43 ticks on the four cardinals, about 30
	 * elsewhere, 1 / 2 / 6 / 4 just beside them. A radial zone judges the length
	 * of the pair, never a component, so the direction passes through intact.
	 *
	 * XInput: the hardware's published 7849 (left) and 8689 (right) of 32767,
	 * in this tree's /256 units 30.7 and 33.9, rounded up so the radial zone is
	 * never narrower than the hardware's.
	 * DirectInput: the sticks measured at rest (MODERNLOG pad line, eight
	 * sessions) within 10 of 128 (lx 0..2, ly -9..10, rx 2..5, ry -6..2). 24 is
	 * twice that as headroom and above the left stick's axis gate of 18; the
	 * right stick's per-axis 500 of 1000 would be 62 and eat a third of the
	 * travel for a drift that is not there. Against drift, raise in steps of 8. */
	MODERN_DEAD_LEFT = 31,			// XInput's own 7849 of 32767, in this tree's /256 units
	MODERN_DEAD_RIGHT = 34,			// XInput's own 8689 of 32767, same units
	MODERN_DEAD_LEFT_DINPUT = 24,	// measured rest <= 10 of 128 (MODERNLOG pad), twice that as headroom
	MODERN_DEAD_RIGHT_DINPUT = 24,	// the same pad, the same measurement

	/* The square gate. A per-axis zone of half-width A zeroes the whole square
	 * |x| <= A, |y| <= A; a radial zone of radius A covers only the disc inside
	 * it, so a stick resting in the corners (up to A * 1.41 out) would turn the
	 * camera by itself. Both are asked and the stick is dead in either: the
	 * square is what the hardware held, the disc is what the rescaling needs.
	 * The axis snap is not brought back, because a snapped push always has a
	 * component outside the square.
	 * The numbers are each path's own, divided as that path divides its axes
	 * (specific\dxshell.cpp) and floored - at worst one unit of 128 in the
	 * corners, on the dead side. The DirectInput right stick's 500 is not
	 * mirrored: specific\dxshell.cpp writes joystick_raw_rx/ry before that gate
	 * and the seam reads the raw pair, so the 500 only serves the bindable
	 * stick bits. 20 is the same headroom as the radial 24. */
	MODERN_DEAD_AXIS_LEFT = 30,				// XInput left, 7849 of 32767 in /256 units
	MODERN_DEAD_AXIS_RIGHT = 33,			// XInput right, 8689 of 32767, same
	MODERN_DEAD_AXIS_LEFT_DINPUT = 18,		// DirectInput left, 150 of 1000 in /8 units - the hardware really holds this one
	MODERN_DEAD_AXIS_RIGHT_DINPUT = 20,		// measured rest <= 10 of 128; that path's own 500 no longer reaches the seam

	/* All eight are the middle of the dead-zone slider: its default of 50
	 * hands each through ModernDeadZoneUser unchanged; 0 is no zone for a tight
	 * pad, 100 a wide one for a worn pad that creeps. The slider scales the
	 * square too, since a player who widens the zone wants the corners with it.
	 * No classic zone lies underneath: both paths in specific\dxshell.cpp write
	 * joystick_raw_* before their own per-axis gates, and DIPROP_DEADZONE is
	 * never set on the DirectInput device (only DIPROP_RANGE), so 0 means 0. */

	/* The look key's two sectors. With the look key held the classic assembly
	 * in specific\input.cpp reads the stick through two per-axis thresholds of
	 * 8; behind the radial zone that is a window of +-3.6 degrees round the
	 * horizontal, and anything outside it sends a turn and a step bit at once.
	 * A thumb pushes "left" at a measured median of 10 to 16 degrees below the
	 * horizontal, so of 962 pushed ticks 680 came out as LEFT+BACK and 57 as a
	 * pure side - and lara_as_stop lets IN_BACK overwrite the turn goal, so she
	 * steps back instead of turning.
	 * On those ticks one axis survives: inside the sector the pair is a pure
	 * turn, outside it a pure step. 50 degrees carries the measured arc with
	 * room over and leaves +-40 degrees to the vertical; 8 degrees of hysteresis
	 * is the smallest width a thumb resting on the boundary cannot cross twice
	 * in a tick. Both are tuned in play; MODERNLOG's tank line prints `sector=`
	 * and `out=`. Units: 50 * 32768 / 180 and 8 * 32768 / 180. */
	MODERN_TANK_TURN_SECTOR = 9102,			// 50 degrees either side of the horizontal
	MODERN_TANK_SECTOR_HYST = 1456,			// 8 degrees of hysteresis on that boundary

	/* And the three answers, named rather than spelled 0/1/2 in three files -
	 * MODERNLOG's `sector=` prints exactly these. */
	MODERN_TANK_SECTOR_NONE = 0,
	MODERN_TANK_SECTOR_TURN = 1,
	MODERN_TANK_SECTOR_STEP = 2,

	/* Walk below a deflection: TROne's rule, under half deflection she walks
	 * (16000 of 32767, here 62). It ships as 0 - the stick never sets IN_WALK,
	 * the pad's own walk key does. A magnitude cannot tell a walk from a real
	 * stick: of 32 measured pushes from rest the first tick read 20-60 of 128 in
	 * 19 and the second 100 or more in 25, so one low tick latched AS_WALK for
	 * several ticks mid-run. The field stays so TROne's rule remains testable
	 * with an explicit cfg (ModernTest's walk_threshold()). */
	MODERN_WALK_BELOW = 0,

	/* TROne's anti-chatter window: inside 2.8 degrees of her course Lara
	 * counts as aligned and no turn bit goes out. */
	MODERN_AIM_WINDOW = 0x0400,

	/* Once aligned, it takes twice the window to start turning again. Without
	 * the hysteresis the turn bit flickers on the window's edge at tick rate,
	 * a visible twitch at small angles. */
	MODERN_ALIGNED_FACTOR = 2,

	/* Above 112.5 degrees of error she turns on the spot and does not run;
	 * she runs again below 56.25. Two thresholds, so a 180 degree flick cannot
	 * chatter between them. */
	MODERN_SPIN_ENTER = 0x5000,
	MODERN_SPIN_EXIT = 0x2800,

	/* TROne's rule: standing still, the classic state machine can express a
	 * turn only without IN_FORWARD (lara_as_stop turns IN_LEFT/IN_RIGHT into
	 * AS_TURN_L/AS_TURN_R, and IN_FORWARD overwrites that goal with run or
	 * walk), so with a wall ahead she would stop responding altogether. TR5's
	 * game\lara.cpp has the same cause. 67.5 degrees, half that to leave. */
	MODERN_STAND_SPIN = 0x3000,

	/* The two numbers of the held course. Under steer_step (2.1 degrees, the
	 * noise floor of a real thumb) nothing happens. TROne re-seats the course
	 * on the camera only at a stick change of recenter (0x1000, 22.5 degrees)
	 * and otherwise re-steers the held course by the stick change alone.
	 * Measured here, that fails two ways: a stick circled in 0.3-0.6 s (20-40
	 * degrees a tick) re-seats on a camera that has meanwhile swung behind her,
	 * and a slow stick sweep of 51 degrees over 39 ticks, while the chase camera
	 * swung 120, left the course 50 degrees short of where the player aimed
	 * relative to the camera he could see.
	 * Reading the camera on every tick regardless of the stick is the feedback
	 * TROne built the held course against: a held stick would spin her forever,
	 * because turning her swings the camera, which reads as a new target. So
	 * recenter equals steer_step: any stick change above the noise floor
	 * re-seats on the camera of that tick, and a stick that is not changing
	 * keeps its course however the camera swings. The two stay separate cfg
	 * fields so TROne's 0x1000 remains testable (ModernTest's held_course()). */
	MODERN_STEER_STEP = 0x0180,
	MODERN_RECENTER = MODERN_STEER_STEP,

	/* The quadrant boundary: within 45 degrees of her nose the direction is
	 * "forward", within 45 of her back it is "back", the rest a side step. */
	MODERN_AIM_SECTOR = 0x2000,

	/* Read by game\modern_state.cpp, not by the translation. Lara counts as
	 * blocked when she has moved less than 4 world units for 6 ticks while
	 * asking to go forward and to turn. Six ticks is a fifth of a second: fewer
	 * and the first ticks of a start from a standstill look like a wall. */
	MODERN_BLOCK_FRAMES = 6,
	MODERN_BLOCK_EPS = 4,

	/* The direct turn's rate. TROne writes y_rot itself at 3277 a tick (18
	 * degrees). The game's own rates (game\lara.cpp): run 1456 (8 degrees),
	 * walk 728, turn on the spot 728 rising to 1456 in AS_FASTTURN, back 728,
	 * fast back 1092, jump 546. At 18 degrees a thumb circling at a measured 15
	 * to 30 degrees a tick outruns her: the error grows past 180, the shortest
	 * way flips and she turns the other way or runs into a wall.
	 * The Remaster, timed from video of it: one revolution on the spot in about
	 * 0.42 s (about 28 degrees a tick at 30 ticks a second) and a half turn while
	 * running in 6 ticks (30). 65536/12 = 5461.33, so 5461: a half turn takes
	 * six ticks and a seventh of two units. 5462 would land it in six exactly;
	 * 5461 keeps the timed 30.000 degrees, and two units are a fiftieth of a
	 * degree. The older candidates stay named so a swap is a one-word edit;
	 * MODERNLOG prints the rate in its init line and each tick's turn in yawd. */
	MODERN_TURN_RATE = 5461,	// 30 deg/tick, the Remaster's rate timed from video
	MODERN_TURN_RATE_VANILLA = 1456,
	MODERN_TURN_RATE_MIDDLE = 2184,
	MODERN_TURN_RATE_TRONE = 3277,	// 18 deg/tick, TROne's rate

	/* The second gate of the direct turn: the write to y_rot sits behind
	 * tomb5.control_modern and behind this, so the direct turn can be switched
	 * off on its own while the rest of the scheme stays. game\modern_state.cpp
	 * is the only reader. */
	MODERN_DIRECT_TURN = 1,

	/* IN_FORWARD comes back when this much or less of the turn is left, so
	 * she never steps in a direction the stick points away from. While she is
	 * really running it is not asked at all (the withheld bit would cost her a
	 * one-tick AS_STOP). A reversal turns at the rate like everything else,
	 * with the run animation playing, as in the Remaster - no one-tick 180.
	 * Standing, the gate is 90 degrees: at 45 a circling stick kept her in
	 * AS_STOP turning on the spot with no direction bit (232 ticks measured).
	 * 90 is a safety line, not a feel number: she still never steps toward a
	 * direction the stick points away from, so a ledge behind her stays safe,
	 * and at 5461 an about-turn spends at most three ticks over it. MODERNLOG's
	 * bits= shows it. The one exception is MODERN_STAND_FORWARD_TICKS below. */
	MODERN_SNAP_FORWARD = 0x2000,
	MODERN_SNAP_FORWARD_STAND = 0x4000,

	/* The case no gate width reaches: a stick circling at exactly her turn
	 * rate moves the course as fast as she turns, so the error stays where the
	 * first push seated it and the gate never opens. After this many ticks of
	 * standing, turning and not arriving, IN_FORWARD is handed over anyway and
	 * she runs the rest of the turn out. 8 is more than twice what a held stick
	 * needs - thrown a half turn and held, she is inside the 90 degree gate by
	 * tick 4 (ModernTest's reversal() and spin_sweep()) - so the ledge case
	 * never reaches this rule; only a moving thumb does. MODERNLOG's
	 * stand_hold= is the counter; 0 switches the rule off. */
	MODERN_STAND_FORWARD_TICKS = 8,

	/* Steering in the air. In the classic only lara_as_forwardjump reads a
	 * turn bit in the air: it ramps lara.turn_rate by 409 a tick against a cap
	 * of 546 and LaraAboveWater decays it by 364, so 182 units (1 degree) a
	 * tick survive - measured jumps of 40 ticks turned 1 to 36 degrees in total.
	 * lara_as_reach, lara_as_swandive, lara_as_fastdive, lara_as_upjump,
	 * lara_as_fastfall and lara_as_fallback read no direction bit.
	 * Only state 3 gets the air turn (ModernStateAllowsAirTurn): in the reach
	 * her heading decides which ledge lara_col_reach tests, so turning there
	 * would move the grab; the dives and falls have no classic steering to
	 * widen. 728 is 4 degrees a tick, so a 40-tick jump can turn 160 degrees
	 * without spinning in mid air. Tuned in play; MODERNLOG's `air=` prints the
	 * step of each tick and `lara=` the heading. */
	MODERN_AIR_TURN_RATE = 728,		// 4 deg/tick; the classic forward jump manages 182 (1 deg)

	/* Full deflection in this tree's units. specific\dxshell.cpp scales XInput's
	 * -32768..32767 down by 256, so a fully pushed stick reads about 128; the
	 * DirectInput fallback ranges to -1000..1000 and divides by 8, so about
	 * 125. Only the magnitude is scaled by this - the direction is an angle
	 * and does not care. */
	MODERN_STICK_FULL = 128,

	/* The water. The scheme is the land's; the rates are taken from
	 * game\laraswim.cpp and game\larasurf.cpp, one per state
	 * (ModernWaterTurnRate is the table).
	 *
	 * Under water the classic turns through an accumulator: SwimTurn ramps
	 * lara.turn_rate by 409 a tick, LaraUnderWater adds it to y_rot and decays
	 * it by 364, both clamped at +-1092 (6 degrees). That is the ceiling of an
	 * accumulator the modern path does not use - game\modern_state.cpp writes
	 * y_rot itself and zeroes lara.turn_rate - and at 6 degrees a measured 319
	 * of 452 water ticks ran at the ceiling with a median error of 44 degrees.
	 * Nothing else clamps y_rot (LaraUnderWater's other clamps are x_rot
	 * +-15470 and z_rot +-4004). 2730 is 15 degrees a tick, half the land's
	 * 30, and settles a half turn in 13 ticks instead of 31.
	 *
	 * On the surface the states write y_rot directly, with no accumulator or
	 * clamp: 728 in lara_as_surftread and lara_as_surfswim, 364 in the three
	 * side and back strokes. Those are real rates and are kept.
	 *
	 * The bank: the roll is 546 a tick against LaraUnderWater's 364 of decay,
	 * so she leans by 182 a tick at any rate; a faster turn finishes before the
	 * +-4004 clamp (13 ticks of 182 is 2366), a shallower lean with no snap. */
	MODERN_SWIM_TURN_RATE = 2730,	// 15 deg/tick; 1092 is SwimTurn's accumulator ceiling, not a rate
	MODERN_SURF_TURN_RATE = 728,	// lara_as_surftread / lara_as_surfswim - a real rate, not a lid
	MODERN_SURF_SIDE_RATE = 364,	// lara_as_surfback / surfleft / surfright

	/* The pitch. SwimTurn's step for a held IN_FORWARD/IN_BACK is 364 a tick
	 * (2 degrees); here the body chases the orbit's elevation as a pointer, and
	 * at 364 it needs 43 ticks from level to straight up while the stick gets
	 * there in three. 728 (4 degrees) halves that; tuned in play, MODERNLOG
	 * shows x_rot in `lara=` beside cam_pitch. The limits are LaraUnderWater's
	 * own clamp on x_rot.
	 *
	 * The body's clamp (+-15470) and the camera's (-10923 .. +15470, with the
	 * sphere's top cap cut, see MODERN_CAMERA_PITCH_MIN) differ, and
	 * ModernCameraSwimPitch takes pitch_home out of the elevation, so the
	 * pointer asks for -9103 .. +17290. Upwards the last 1820 units move the
	 * view alone; downwards the steepest dive is 9103 (50 degrees) where Tank's
	 * IN_FORWARD reaches 15470 (85). A deeper dive would come from the water
	 * mapping its own range out of the camera's, not from restoring the cap. */
	MODERN_SWIM_PITCH_RATE = 728,	// 4 deg/tick; SwimTurn's own held-bit step was 364
	MODERN_SWIM_PITCH_MIN = -15470,
	MODERN_SWIM_PITCH_MAX = 15470,

	/* The roll into the turn. SwimTurn banks her by 546 a tick while a turn
	 * bit is held and LaraUnderWater decays it by 364 and clamps at +-4004, so
	 * a held turn settles at the clamp and a released one unwinds. The direct
	 * turn takes the turn BITS away, so it owes the bank they would have made:
	 * this is that same 546, written on the ticks the modern turn really
	 * turns her, and the decay and the clamp are still the engine's. */
	MODERN_SWIM_ROLL_RATE = 546,

	/* Under water the stick moves her in its direction like on land, so it
	 * cannot also need the jump key: with this on, the stroke is held for as
	 * long as the stick is out. 0 restores the classic "jump = stroke" with the
	 * modern course still steering. The surface is unaffected: there the stroke
	 * is the forward bit the course already produces (lara_as_surftread ->
	 * AS_SURFSWIM), and IN_JUMP means "dive" (larasurf.cpp counts ten ticks). */
	MODERN_WATER_AUTO_STROKE = 1,

	/* The five switches of the scheme. They are constants, not settings:
	 * Modern is one option, not a page of sliders. Each turns off exactly one
	 * rule of the design, so a fault can be bisected.
	 *   hold_course	the held world course (off = the camera of every tick
	 *					is the reference, which TROne measured spinning in
	 *					circles)
	 *   back_turns		pulling back turns her around (off = a classic step
	 *					backwards)
	 *   aim_strafe		weapon up and action held strafes instead of turning
	 *   jump_dir		the standing jump and the flips pick a direction
	 *   block_turn		a blocked forward becomes a pure turn
	 *
	 * aim_strafe ships off. On, a ready weapon and the action key put her in
	 * MODE AIM - side steps and a walk - for as long as she fires (TROne's
	 * rule), which leaves her only creeping in a fight. Off, firing steers as
	 * empty hands do and only her arms and torso aim. The rule stays for
	 * ModernTest (aiming(), the AIM half of firing_run()). The flips with a
	 * weapon drawn belong to jump_dir. */
	MODERN_HOLD_COURSE = 1,
	MODERN_BACK_TURNS = 1,
	MODERN_AIM_STRAFE = 0,
	MODERN_JUMP_DIR = 1,
	MODERN_BLOCK_TURN = 1,

	/* The ledge hang (the option tomb5.ledge_hang, the state AS_LEDGEHANG in
	 * game\lara.cpp). These are not cfg fields: ModernLedgeDecide and the
	 * state in game\lara.cpp read them by name, ModernDefaults does not.
	 *
	 * The move turns her half round on the standing line and then plays
	 * TR5's own pull-up (animation 97, 74 frames at rate 4, the same in all
	 * thirteen campaign levels) backwards into the settled hang. The turn is
	 * 8 ticks (22.5 degrees a tick, 0.27 s); the lowering steps 3 frames a
	 * tick, so the 73 frames from the last to the first take 25 ticks (0.83 s)
	 * with the last step cut short to land on the first frame exactly. */
	MODERN_LEDGE_TURN_TICKS = 8,		// the half turn, in ticks
	MODERN_LEDGE_HALF_TURN = 0x8000,	// what the turn covers: she ends facing the wall
	MODERN_LEDGE_FRAME_STEP = 3,		// frames down per tick in the lowering

	/* The angle window of the engine's own catch (lara_col_reach and
	 * lara_col_crawl2hang snap y_rot to a cardinal inside +-0x18E2, 34.6
	 * degrees, and refuse outside it). Every hang routine is cardinal only. */
	MODERN_LEDGE_SNAP = 0x18E2,

	/* The two lines of the move, in units from the sector boundary at the
	 * edge. The catch snaps the hang to `(z | 1023) - 100` / `(z & ~1023) +
	 * 100`: 100 units from the last unit of the sector she hangs in, so 101
	 * from the first unit of the ledge. The pull-up ends 200 units further on
	 * (animation 102's translate), 99 units inside the ledge: the standing
	 * line. A walk stops by itself 101 units short of the edge (the walk's own
	 * edge stop), two units off the line; one walk step, 15 units, is the most
	 * a walk or a stand lies off it. */
	MODERN_LEDGE_HANG_OUT = 100,		// the catch's own snap, outside the boundary
	MODERN_LEDGE_STAND_IN = 99,		// the standing line, inside the boundary
	MODERN_LEDGE_RESIDUAL = 15,		// a walk step: the most a walk or a stand lies off the line

	/* How near the edge ahead LEDGELOG reports the refusal set while the grab
	 * key is held: half a sector, about eleven run steps. */
	MODERN_LEDGE_NEAR = 512
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
	long block_frames;	// ticks of not moving that count as blocked
	long block_eps;		// world units that still count as not moving
	long turn_rate;		// the direct turn, units per tick
	long direct_turn;	// the direct turn's own gate
	long snap_forward;	// error under which IN_FORWARD comes back
	long snap_forward_stand;// the same, in the four turn-on-the-spot states
	long stand_forward_ticks;// ticks of a standing turn that arrives nowhere; 0 switches it off
	long air_turn_rate;	// the forward jump's own turn, units per tick; 0 switches it off
	long stick_full;	// full deflection in this tree's units
	long swim_turn_rate;// the direct turn under water, units per tick
	long surf_turn_rate;// the same on the surface, treading and swimming
	long surf_side_rate;// and in the back and side strokes
	long swim_pitch_rate;// how fast x_rot follows the orbit's elevation
	long swim_pitch_min;// the engine's own clamp on x_rot
	long swim_pitch_max;
	long swim_roll_rate;// the bank the turn bits would have made
	long water_auto_stroke;// the stroke is held while the stick is out
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
	short course;		// the held world course - the core of the design
	short last_off;		// the stick angle the course was last drawn from
	long spin;			// turning on the spot (hysteresis between the two)
	long aligned;		// the wider window of MODERN_ALIGNED_FACTOR applies
	long aiming;		// the strafe mode held last tick
	long blocked_count;	// ticks she did not get anywhere (MODERN_BLOCK_FRAMES)
	long last_mode;		// the mode the last tick ended in, for the log

	/* The course the last active tick ended on, and whether there was one;
	 * together they say how far the course itself jumped this tick (course_step,
	 * MODERNLOG's cstep=). It survives a release of the stick, unlike have_ref:
	 * a flick from forward to back sweeps through the centre, and one or two
	 * ticks read as no push. Only ModernResetState clears it (level start,
	 * death, scheme switched); have_course 0 means there is nothing to compare. */
	short last_course;
	long have_course;

	/* Which way the modern turn last moved her: -1, 0 or 1. Exactly half a
	 * turn wraps to -32768 either way, so without this every exact reversal
	 * would go left by accident and a reversal back at 180 on the next tick
	 * could change its mind halfway. It is the turn's own last direction, not
	 * the stick's, because on the tick a flick crosses the centre the stick
	 * step wraps to -32768 as well. The air turn shares the field, so the swing
	 * stays one direction across a takeoff. */
	long last_turn;

	/* Ticks in a turn-on-the-spot state with the forward gate shut
	 * (MODERN_STAND_FORWARD_TICKS). Every tick the direct turn does not take
	 * resets it, so it counts one unbroken turn. MODERNLOG's stand_hold=. */
	long stand_hold;
};

/* What the caller knows about Lara this tick. A struct of zeroes gives plain
 * running on land, which is what the tests use when they mean "no situation".
 * There is no `walk` field: walking is what walk_below decides. */
struct MODERN_SITUATION
{
	short anim_state;	// item->current_anim_state
	long classic;		// the caller says: leave this tick classic
	long drawn;			// a weapon is ready (gun_status == LG_READY, no torch)
	long action;		// the action key is down
	long jump;			// the jump key is down
	long blocked;		// the blocked counter says forward is not going anywhere

	/* How far the player's own thumb turned the camera last tick, signed; 0
	 * when the orbit did not tick or moved by itself. The held course is a
	 * world course so that a camera moving on its own leaves her path alone;
	 * a camera the player turns must carry it, or the course stands still and
	 * then jumps when the left thumb next shifts (measured: 34 jumps in water,
	 * median 13.6 degrees; 57 on foot, median 13.2). Only the caller can tell
	 * the thumb from the return and the carried swing (game\camera.cpp's
	 * ModernCameraPlayerStep). */
	short cam_player_step;
};

struct MODERN_RESULT
{
	long bits;			// MODERN_IN_* - finished input bits, nothing else
	long active;		// 1 = the stick is out and the bits above apply
	short target;		// the world course she should be on (the direct turn turns to it)
	short delta;		// target minus her heading, signed, the short way round
	long mag;			// stick deflection, 0..stick_full
	short reference;	// the camera heading the course was seated on
	long mode;			// modern_mode, for the log and the tests
	long blocked;		// the blocked rule took IN_FORWARD away this tick
	long turning;		// a turn bit is in `bits`
	short course_step;	// how far the course itself moved this tick - MODERNLOG's `cstep=`
};

/* The ledge hang's phases. The value is what tomb5_save_info keeps, so a
 * savegame made in the middle of the move knows where it stood; a number
 * outside the three is a phase the state does not know. */
enum modern_ledge_phase
{
	MODERN_LEDGE_NONE = 0,	// not in the move
	MODERN_LEDGE_TURN = 1,	// standing on the line, turning half round
	MODERN_LEDGE_LOWER = 2	// at the hang position, the pull-up running backwards
};

/* What ModernLedgeDecide says to do with the tick. */
enum modern_ledge_verdict
{
	MODERN_LEDGE_QUIET = 0,		// nothing to say: the option is off or she is not in the move
	MODERN_LEDGE_CONTINUE = 1,	// apply the two steps and stay in `phase`
	MODERN_LEDGE_STAND = 2,		// abort: she stands where she is
	MODERN_LEDGE_HANG = 3		// hand over to the engine's settled hang
};

/* What the state knows this tick. The caller measures, the function decides;
 * no field is an engine type. */
struct MODERN_LEDGE_SITUATION
{
	long option;		// tomb5.ledge_hang && tomb5.control_modern - 0 and nothing is reported
	long phase;			// modern_ledge_phase, as stored
	long tick;			// ticks already spent in the phase; 0 is its first tick
	long frame;			// the lowering: the current frame of the pull-up
	long frame_base;	// and its first and last frame (anims[97] of the level)
	long frame_end;
	long step;			// frames down per tick: MODERN_LEDGE_FRAME_STEP (a field, so a test can try others)
	short y_rot;		// her heading now
	short target;		// the heading she hangs with, stored at entry (a cardinal)
	short cardinal_off;	// y_rot minus the cardinal nearest to it, signed
	long refusals;		// the refusal bits the caller measured this tick, 0 = none
	long key;			// the grab key is held
	long jump;			// the jump key is down
	long hands_free;	// lara.gun_status == LG_NO_ARMS
	long hit_points;	// now
	long entry_hit_points;// on the turn's first tick
	long control_taken;	// a cutscene or the engine holds her (the player does not steer)
};

struct MODERN_LEDGE_RESULT
{
	long phase;			// the phase to be in after this tick
	long yaw_step;		// add to y_rot this tick (ModernWrap)
	long frame_step;	// frames to step DOWN this tick, 0 or more
	long verdict;		// modern_ledge_verdict, also the return value
};

void ModernDefaults(MODERN_CFG* cfg);
void ModernResetState(MODERN_STATE* state);
short ModernWrap(long angle);
short ModernAngle(long x, long z);
long ModernStickDeadZone(long zone, long axis_zone, long in_x, long in_y, long* out_x, long* out_y);
long ModernDeadZoneUser(long base, long user);
long ModernTankSector(long in_x, long in_y, long prev_sector, long* out_x, long* out_y);
long ModernStateIsClassic(short anim_state);
long ModernStateIsSwim(short anim_state);
long ModernStateIsGrab(short anim_state);
long ModernStateIsDuck(short anim_state);
long ModernWaterTurnRate(const MODERN_CFG* cfg, short anim_state);
long ModernWaterMove(const MODERN_CFG* cfg, const MODERN_SITUATION* sit, MODERN_RESULT* out,
	long target_pitch, long x_rot, long* yaw_delta, long* new_x_rot, long* stroke);
long ModernStateIsGroundIdle(short anim_state);
long ModernStateIsStanding(short anim_state);
long ModernStateIsRunning(short anim_state);
long ModernStateIsSpotTurn(short anim_state);
long ModernStateAllowsDirectTurn(short anim_state);
long ModernStateAllowsAirTurn(short anim_state);
long ModernGroundTurn(short anim_state, long delta, long rate);
long ModernGroundTurnBiased(short anim_state, long delta, long rate, long bias);
long ModernDirectTurn(const MODERN_CFG* cfg, MODERN_STATE* state, const MODERN_SITUATION* sit,
	MODERN_RESULT* out, long* yaw_delta);
long ModernAirTurn(const MODERN_CFG* cfg, MODERN_STATE* state, const MODERN_SITUATION* sit,
	MODERN_RESULT* out, long* yaw_delta);
long ModernWeaponBits(long combat_hold, long ready, long holstered, long held, long bits);
long ModernModeLatch(long toggle, long down, long drop, long* state, long bit, long bits);
long ModernGrabLatch(long toggle, long down, long family, long drop, long* state, long bit,
	long release, long bits);
void ModernModeLatchDrop(long* state);
void ModernTranslate(const MODERN_CFG* cfg, MODERN_STATE* state, long stick_x, long stick_y,
	short camera_yaw, short lara_yaw, const MODERN_SITUATION* sit, MODERN_RESULT* out);
long ModernLedgeTurnStep(long tick);
long ModernLedgeDecide(const MODERN_LEDGE_SITUATION* sit, MODERN_LEDGE_RESULT* out);
long ModernLedgeHangQuad(short approach);
long ModernLedgeEdgeDist(long quad, long x, long z);
void ModernLedgeStep(long quad, long shift, long* x, long* z);
long ModernLedgeFire(long dist, long speed, long* shift);

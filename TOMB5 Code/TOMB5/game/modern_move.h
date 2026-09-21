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
 * THE ORIGIN, IN FULL (written down in audit round A1 because a reader is
 * entitled to the whole chain, not the last link of it).
 *
 *  - What was re-derived here is the TROne of 2026-09-13, the version the
 *    author had in hand for #161.
 *  - TROne's modern control is TROne's OWN design, not a reading of anybody's
 *    program. Its docs\CONTROLS.md sets out the plain arithmetic - stick
 *    direction plus a held reference heading, turned into the game's own tank
 *    bits, with the game logic left 1:1 - and justifies the held-heading rule
 *    with a proof run of its own, 600 frames of its own recording. Its input
 *    code cites no disassembler and no decompilation.
 *  - This project then checked itself rather than taking that on trust. On
 *    2026-09-16 every one of the 32 constants in the modern-control set was
 *    put through a taboo table: 16 had a stated origin and were fine, 9 were
 *    re-measured here by playing the game, 5 did not apply, and 2 whose origin
 *    could not be established were NOT taken. None was derived from a remaster
 *    binary, and no file and no document of the modern control mentions one.
 *  - The single fact in the whole set that comes from the remaster at all is a
 *    set of option NAMES, read out of a plain text file (COMMON.TXT) of the
 *    player's own installation. No binary was opened for it.
 *
 * The user's instruction that started #161 was, in his words, to rebuild the
 * remaster's modern control. It was rebuilt the only lawful way: by playing it
 * and watching what it does. Nothing of the remaster's code is here, and
 * TROne's own docs\LEGAL.md forbids that route as plainly as this project
 * does.
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
	MODERN_IN_ACTION =	0x40,	// IN_ACTION - Grab mode (#161, round 161b2d, item 5)
	MODERN_IN_WALK =	0x80,	// IN_WALK
	MODERN_IN_LSTEP =	0x400,	// IN_LSTEP
	MODERN_IN_RSTEP =	0x800,	// IN_RSTEP
	/* The two bits the ONE action key raises beside IN_ACTION under "One key"
	 * (specific\input.cpp's action_bits). Nothing in this file writes them;
	 * they are mirrored because the grab latch's let-go mask is now a word the
	 * caller composes out of them, and because ModernTest has to be able to
	 * build a real "One key" word to check it (#161, round 161b2d, fix 3). */
	MODERN_IN_INTERACT =	0x4000,		// IN_INTERACT - the world-object half
	MODERN_IN_SELECT =	0x100000,	// IN_SELECT - the menus', never the latch's
	MODERN_IN_DUCK =	0x20000000,	// IN_DUCK - Duck mode, and nothing else here
	MODERN_IN_SPRINT =	0x40000000,	// IN_SPRINT - Sprint mode (#161, round 161b2c, item 4)

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

/* THE TWO MODE LATCHES' WHOLE MEMORY (#161, round 161b2c, item 4). Two bits
 * per mode, in one long the CALLER owns - the same division ModernWeaponBits
 * draws, where the pure function decides and the engine keeps the state.
 *
 * MODERN_LATCH_KEY is the key as it stood LAST tick and is what makes the
 * press an edge; it is tracked on every tick the option is on, including the
 * ticks the latch may not stand, so that a key held across a level boundary or
 * out of a menu cannot arrive as a fresh press. */
enum modern_latch_bits
{
	MODERN_LATCH_ON =	0x1,	// the bit is latched on
	MODERN_LATCH_KEY =	0x2,	// the key or button was down on the previous tick

	/* GRAB MODE'S OWN LATCH BIT (#161, round 161b2d, item 5), and it is a
	 * different kind of memory from MODERN_LATCH_ON, which is why it gets a
	 * bit of its own rather than that one's meaning. The grab latch does not
	 * remember "she is holding on" - the STATE says that, and the family table
	 * below is how it is asked. What it has to remember is the opposite: that
	 * the press under the player's thumb right now is the one that let go, so
	 * that the same press cannot grab the next ledge on the way down. It is
	 * set on a press inside the family and cleared when that press ends.
	 *
	 * The key memory above is shared, unchanged in meaning, and tracked on
	 * every tick the option is on for exactly the same reason. */
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
	/* Row 2, MEASURE - AND ROUND 161a2b MOVED WHERE IT IS ANSWERED. TROne's
	 * dead zone is 8000 of 32767, 24 per cent. Ours still starts at nothing,
	 * but the reason changed with item 6: the modern path no longer reads the
	 * axes through the hardware's PER-AXIS zone at all. It reads the raw pair
	 * and puts it through the RADIAL zone below (ModernStickDeadZone), which
	 * is the dead zone of the scheme now - so a second one here would only eat
	 * travel, exactly as a second one on top of the hardware's used to.
	 * The plan if the user reports drift: MODERNLOG prints mag, raise this in
	 * steps of 4 (3 per cent of full deflection) and read the log again.
	 * WHAT THAT MEASUREMENT IS NOT LOOKING FOR, corrected in the second review
	 * pass of round 161a2: a band where Modern moves her and Tank does not.
	 * There is none - the direction bits are digital and walk_below is 0, so
	 * every reading past the radial zone runs her, whatever its size.
	 * WHAT IS NEW AND IS NOT THAT, named here rather than found later: the
	 * radial zone RESCALES, so the smallest reading it can hand out is 1 and
	 * not the hardware's 18 or 30. Where the seam steps aside and the CLASSIC
	 * assembly steers her from the same pair instead - first person, the look
	 * key, a cutscene (specific\input.cpp) - anything under +-8 is ignored
	 * there as it always was, so those states gained a few units of dead band
	 * just past the rim. Modern's own path never sees it. */
	MODERN_DEAD = 0,

	/* THE RADIAL DEAD ZONES (round 161a2b, item 6) - half of the answer to
	 * "Das kann auch teilweise ein Deadzone-Problem sein".
	 *
	 * WHAT WAS WRONG, measured in the play test (M2 of the round's brief):
	 * both pad paths zero each axis ON ITS OWN, so a stick held nine degrees
	 * off an axis arrives ON that axis - the smaller component falls inside
	 * its own zone and is thrown away while the larger one is kept whole. His
	 * direction histogram is the proof: 68 / 41 / 48 / 43 ticks on the four
	 * cardinals against about 30 everywhere else, and all but empty just
	 * beside them (1 / 2 / 6 / 4). A stick circling evenly therefore moves the
	 * course in JUMPS, which is one half of the spin he filmed. A radial zone
	 * cannot do that: it judges the LENGTH of the pair and never a component,
	 * so the direction it hands on is the direction the thumb is holding.
	 *
	 * THE FIRST TWO NUMBERS ARE THE HARDWARE'S OWN and are not invented here.
	 * XInput publishes 7849 of 32767 for the left stick and 8689 for the right
	 * - 24 and 27 per cent - and those two are exactly what the per-axis zones
	 * on that path already used. In the units this tree carries axes in (about
	 * +-128, see MODERN_STICK_FULL) they are 7849/256 = 30.7 and 8689/256 =
	 * 33.9, rounded UP so that the radial zone is never narrower than the
	 * hardware's own. Both pad paths use them, so the two feel the same.
	 *
	 * AND THE THIRD USED TO BE A GUESS ABOUT THIS USER'S PAD, AND IS NOW THE
	 * PAD ITSELF (round 161a3, item 7 - M9). The DirectInput right stick
	 * (lZ/lRz) was believed to rest far OFF CENTRE, which is why its per-axis
	 * zone is 500 of 1000 where the left stick's is 150 (specific\dxshell.cpp).
	 * How far off it really rests had never been measured, so round 161a2b took
	 * the widest rest that zone could be hiding - 500/8 = 62 - and said so.
	 * MODERNLOG's pad line has now measured it over eight sessions: lx 0..2,
	 * ly -9..10, rx 2..5, ry -6..2, all of 128. The stick rests within TEN
	 * units of centre, not sixty-two, so the wide zone was eating a third of
	 * the travel for a drift that is not there. 24 keeps twice the measured
	 * worst rest as headroom and is still above the axis gate below.
	 * The plan if the user ever reports drift: MODERNLOG prints rs= and pad=;
	 * these three move back up in steps of 8, not straight to 62.
	 *
	 * AND THE LEFT STICK ON THAT PATH GETS ITS OWN (same measurement). The
	 * DirectInput left stick rests inside 10 as well and its axis gate is 18,
	 * so the shared radial 31 was throwing away every real push between 18 and
	 * 31 - travel the hardware had already let through. 24 is above the gate
	 * (nothing that was dead comes alive) and below the old 31. XInput keeps 31
	 * and 34, which are that hardware's own published zones and not a guess. */
	MODERN_DEAD_LEFT = 31,			// XInput's own 7849 of 32767, in this tree's /256 units
	MODERN_DEAD_RIGHT = 34,			// XInput's own 8689 of 32767, same units
	MODERN_DEAD_LEFT_DINPUT = 24,	// measured rest <= 10 of 128 (MODERNLOG pad), twice that as headroom
	MODERN_DEAD_RIGHT_DINPUT = 24,	// the same pad, the same measurement (was 62, a worst case that never happened)

	/* AND THE SQUARE THE HARDWARE ALREADY HELD, KEPT (round 161a2b, fix round;
	 * the first version of item 6 shipped without it and that was a regression).
	 * A per-axis zone of half-width A zeroes the PAIR over the whole SQUARE
	 * |x| <= A and |y| <= A. A radial zone of radius A only covers the disc
	 * inside that square, so every rest position in its corners - up to A * 1.41
	 * away - stopped being dead when the radial zone replaced it, and a pad
	 * resting there would turn the camera by itself with nobody touching it.
	 * That is the one failure the item names as unacceptable. (The worked
	 * example that used to stand here - a right stick resting at (450, -450) of
	 * 1000, silent before and 71 of 128 through a bare radial 62 - was the worst
	 * case that zone was sized for, and round 161a3's M9 measured that it never
	 * happens on this pad. The GATE is still right for the reason above; only
	 * its width moved, below.)
	 * SO BOTH ZONES ARE ASKED and the stick is dead in either: the square is
	 * exactly what the hardware held, the disc is what the rescaling needs. The
	 * axis snap item 6 exists to cure is untouched by the square, because a
	 * snapped push is one the per-axis zone let THROUGH - it always has a
	 * component outside the square, so it never meets this gate at all.
	 * THE NUMBERS ARE THE PATHS' OWN, divided the way that path divides its
	 * axes before this seam sees them (specific\dxshell.cpp) and floored, so the
	 * gate covers every reading the hardware zone could swallow. Flooring can
	 * cost a hair on the other side - an XInput axis one unit past its own zone
	 * still reads 30 after the /256 - which is a unit of 128 in the corners
	 * only, and it errs towards dead, which is the safe side of this one.
	 *
	 * THE DIRECTINPUT RIGHT ONE IS NO LONGER THAT PATH'S 500 (round 161a3,
	 * item 7), and the reason is a measurement of the read itself: since round
	 * 161a2b specific\dxshell.cpp writes joystick_raw_rx/ry BEFORE its per-axis
	 * 500 and the modern seam reads THAT pair (specific\input.cpp:827), so the
	 * 500 never reaches the camera at all - it survives only for the four
	 * bindable stick bits. Mirroring it here therefore held a square nothing
	 * else holds any more, 62 wide on a stick that rests inside 10. 20 is the
	 * same headroom as the radial 24 beside it, rounded to the axis. */
	MODERN_DEAD_AXIS_LEFT = 30,				// XInput left, 7849 of 32767 in /256 units
	MODERN_DEAD_AXIS_RIGHT = 33,			// XInput right, 8689 of 32767, same
	MODERN_DEAD_AXIS_LEFT_DINPUT = 18,		// DirectInput left, 150 of 1000 in /8 units - the hardware really holds this one
	MODERN_DEAD_AXIS_RIGHT_DINPUT = 20,		// measured rest <= 10 of 128; that path's own 500 no longer reaches the seam

	/* AND ALL EIGHT ARE THE MIDDLE OF A SLIDER NOW (#161, round 161b1, item 5).
	 * "Das ist bei uns momentan noch ein fester Wert, den wie im Remaster
	 * toggelbar fuer den User hier einbinden." Nothing above moved: every
	 * number is still the measurement it was, and the row's default of 50 hands
	 * each of them through ModernDeadZoneUser unchanged (the function below
	 * carries the proof). What the slider adds is the two ends - 0 for a pad
	 * whose sticks are tighter than this one's, 100 for a worn pad that creeps.
	 *
	 * IT SCALES THE SQUARE TOO, and that is a decision rather than an oversight.
	 * The square exists because the per-axis zone in specific\dxshell.cpp used
	 * to hold a resting stick in its corners (see the paragraph above), so a
	 * player who widens the zone wants the corners widened with it; a player who
	 * sets 0 has asked for no zone at all and gets exactly that, on both gates.
	 *
	 * AND THERE IS NOTHING UNDER IT. Measured for this item rather than assumed:
	 * both paths in specific\dxshell.cpp write joystick_raw_* BEFORE their own
	 * per-axis gates (the DirectInput left stick's 150 of 1000 at its line 886,
	 * the right stick's 500 at 959, XInput's two published thumb zones at 1022
	 * and 1087), and the seam reads that raw pair, so no classic zone is a floor
	 * under this slider on any path or stick. DIPROP_DEADZONE is never set on
	 * the DirectInput device either - only DIPROP_RANGE. At 0 the smallest
	 * effective zone is therefore 0 everywhere, which is what 0 promises. */

	/* Row 4c, THE LOOK KEY'S OWN TWO SECTORS (#161, round 161a3b, item 1).
	 *
	 * With the look key held the translation stands aside and the stick is read
	 * by specific\input.cpp's classic assembly - two independent per-axis
	 * thresholds of 8. At Tank those 8 sit behind the hardware's own per-axis
	 * zone; in Modern they sit behind the RADIAL zone, so 8 of 128 is a window
	 * of +-3.6 degrees round the exact horizontal, and anything outside it sends
	 * a turn AND a step bit at once. M1 of round 161a3b's brief counted what
	 * that costs on the user's own pad: his thumb pushes "left" at a median of
	 * -100 to -106 degrees, ten to sixteen degrees below the horizontal - the
	 * natural arc of a thumb - so of 962 pushed tank ticks, 680 came out as
	 * LEFT+BACK and only 57 as a pure side. lara_as_stop lets IN_BACK overwrite
	 * the turn goal, which is his "sie macht den Schritt zurueck". The camera
	 * has no part in it; the pattern is the same at every cam_off of the 19
	 * episodes.
	 *
	 * So on those ticks ONE axis survives: inside this much of the horizontal
	 * the pair is a pure turn, outside it a pure step. No diagonals - this mode
	 * exists to place her exactly, and a diagonal is what broke it.
	 *
	 * BOTH ARE MEASURE-OURSELVES NUMBERS. 50 degrees is a boundary chosen to
	 * carry his whole measured arc (16 degrees at the widest) with room over,
	 * while leaving the vertical +-40 degrees of its own, which is more than
	 * any forward push of his log missed by. The hysteresis is the smallest
	 * width that a thumb resting ON the boundary cannot cross twice in a tick
	 * at his measured stick rates. MODERNLOG's tank line prints `sector=` and
	 * `out=` so both can be read back from a play test rather than argued. The
	 * unit is the engine's own angle - 32768 is half a turn - so 50 degrees is
	 * 50 * 32768 / 180 and 8 degrees is 8 * 32768 / 180. */
	MODERN_TANK_TURN_SECTOR = 9102,			// 50 degrees either side of the horizontal
	MODERN_TANK_SECTOR_HYST = 1456,			// 8 degrees of hysteresis on that boundary

	/* And the three answers, named rather than spelled 0/1/2 in three files -
	 * MODERNLOG's `sector=` prints exactly these. */
	MODERN_TANK_SECTOR_NONE = 0,
	MODERN_TANK_SECTOR_TURN = 1,
	MODERN_TANK_SECTOR_STEP = 2,

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

	/* Row 16, MEASURED - AND THE PLAY TEST HAS NOW PICKED IT (round 161a3,
	 * item 2a). TROne write y_rot themselves at 3277 per tick, 18 degrees,
	 * because the game's own fastest rate is not fast enough for the feel they
	 * were after. Ours, measured in game\lara.cpp: run 1456 (8 degrees), walk
	 * 728, turn on the spot 728 rising to 1456 in AS_FASTTURN, back 728, fast
	 * back 1092, jump 546. So 3277 was already 2.25 times anything vanilla
	 * does - and the second play test measured it as a LID THE THUMB RUNS PAST.
	 *
	 * WHAT THE LOG SAID (M2, M3 of the round's brief). His stick circles at 15
	 * to 30 degrees a tick; her heading stepped at exactly 18.0 on 297 of 395
	 * running ticks, so the error grew every tick the circle was faster than
	 * she was, reached -150..-160 degrees, crossed 180 - and the shortest way
	 * flipped: four windows of the log show her turning the OTHER way at 6 to 9
	 * degrees a tick, twice ending in AS_SPLAT against a wall. "Wenn ich
	 * anfange, den Stick schneller im Kreis zu drehen, bricht sie aus der
	 * Rotation aus und laeuft random in eine Richtung oder bleibt stehen."
	 *
	 * AND WHAT THE REMASTER DOES, counted in the frames of his own video and
	 * from no other source: one revolution on the spot in about 0.42 s, which
	 * at 30 ticks a second is about 28 degrees a tick, and a half turn while
	 * running in about 12 frames of 60 = 6 ticks, which is 30. ONE number
	 * answers both, and it is this one: 65536/12 = 5461.33, so 5461.
	 *
	 * WHAT IT COSTS, named rather than discovered: 32768/5461 = 6.0004, so a
	 * half turn is six ticks and a seventh tick of two units. 5462 would land
	 * it in six exactly; 5461 is kept because 30.000 degrees is the number the
	 * video was counted in and two units is a fiftieth of a degree.
	 * The three older candidates stay named below so that swapping one back in
	 * is a one-word edit, and MODERNLOG still prints the rate in its init line
	 * and the turn of each tick in yawd. */
	MODERN_TURN_RATE = 5461,	// 30 deg/tick, the Remaster's measured rate (M2/M3)
	MODERN_TURN_RATE_VANILLA = 1456,
	MODERN_TURN_RATE_MIDDLE = 2184,
	MODERN_TURN_RATE_TRONE = 3277,	// 18 deg/tick - what rounds 161a to 161a2b shipped

	/* The second gate of the direct turn, the user's decision 5: the write to
	 * y_rot sits behind tomb5.control_modern AND behind this. One is the
	 * scheme, the other is this one mechanism inside it - so a play test can
	 * answer "is modern wrong, or is only the direct turn wrong?" without a
	 * rebuild of the scheme. Step 4 is the only reader. */
	MODERN_DIRECT_TURN = 1,

	/* Rows 17 and 18, read by step 4 as well - AND ROW 17 IS GONE (round 161a3,
	 * item 2b). MODERN_SNAP_DELTA used to be the instant about-turn: past 90
	 * degrees of error, on a tick where the COURSE itself had jumped that far,
	 * the whole turn was taken in ONE tick. The user played it and it is the
	 * complaint: "Unsere Drehung ist noch sehr 'direkt', ich kann mich direkt
	 * 180 Grad drehen. Im Remaster ist das ein fluessiger Turn. Das muessen wir
	 * noch tweaken, weil es A unnatuerlich ist und B selbst das Interpolating
	 * nicht hinterherkommt." M3 counted 36 one-tick steps over 30 degrees in
	 * one session, 24 of them over 90, in AS_RUN and AS_DASH alike; his video
	 * of the Remaster turns the same half circle over six ticks with the run
	 * animation still playing. So a reversal is now turned at the rate like
	 * everything else, and the constant is deleted rather than set to something
	 * unreachable - a gate nobody can open is a gate nobody can read.
	 *
	 * ROW 18 STAYS AND GREW A TWIN. IN_FORWARD comes back when this much or
	 * less of the turn is left, so she never takes a step in a direction the
	 * stick points away from. While she is really RUNNING it is not asked at
	 * all (round 161a2b, item 7a - the withheld bit was costing her a one-tick
	 * AS_STOP), and standing it is now the wider of the two:
	 *
	 * WHY 90 DEGREES STANDING (round 161a3, item 2c). M4 measured 232 ticks in
	 * AS_STOP where she stepped 18 degrees a tick with no direction bit at all
	 * and never left the state - a standing pose spinning on the spot, because
	 * a circling stick kept more than 45 degrees of error standing between her
	 * and the IN_FORWARD that would have started the run. 90 is the safety line
	 * and not a feel number: she still never starts a step toward a direction
	 * the stick points AWAY from, so a ledge behind her stays safe when he
	 * pushes away from it, and at 5461 a full about-turn now spends at most
	 * three ticks over it. MODERNLOG's bits= is where that is read back.
	 *
	 * THE ONE EXCEPTION IS MODERN_STAND_FORWARD_TICKS below (added in audit
	 * round A1, which found this paragraph stating the rule without it): a
	 * MOVING thumb that has spent eight ticks turning on the spot and getting
	 * nowhere does open the gate. A HELD stick never reaches that rule, so the
	 * ledge behind her is still safe. */
	MODERN_SNAP_FORWARD = 0x2000,
	MODERN_SNAP_FORWARD_STAND = 0x4000,

	/* AND THE ONE CASE NO WIDTH OF GATE CAN REACH (round 161a3, fix pass, item
	 * 2c's own acceptance criterion). A stick circling at EXACTLY her turn rate
	 * moves the course by as much as she turns, so the error the first push
	 * seated is the error she keeps: 105 degrees stays 105 degrees for ever, the
	 * gate never opens, and she stands in AS_STOP turning with no animation.
	 * That is M4's complaint word for word - item 2a only moved it from 18
	 * degrees a tick up to 30, which is the very top of the 15 to 30 M2
	 * measured his thumb at. No width of gate can cure it, because the gate
	 * asks how much is LEFT and what is wrong here is that nothing is coming
	 * off.
	 *
	 * SO THE SECOND QUESTION IS ASKED IN TICKS: after this many ticks of
	 * standing, turning and not arriving, IN_FORWARD is handed over anyway and
	 * she runs the rest of the turn out. 8 is the brief's own acceptance number
	 * ("she is handed IN_FORWARD within 8 ticks" under a stick circling at up to
	 * 30 deg/tick from rest), and it is safe because it is more than twice what
	 * a HELD stick ever needs: thrown a full half turn and held, she is inside
	 * the 90 degree gate on tick 4 at the latest (reversal() and spin_sweep()
	 * measure both). So the ledge the gate protects - he pushes AWAY from it and
	 * she must not step toward it - never reaches this rule at all; it answers a
	 * MOVING thumb only, where a step into the sweep is what he is asking for.
	 * MODERNLOG's stand_hold= is the counter, and 0 in the cfg switches the rule
	 * off and leaves the gate on its own. */
	MODERN_STAND_FORWARD_TICKS = 8,

	/* SHE STEERS IN THE AIR (round 161a3, item 4), MEASURE. "Im Remaster kann
	 * sie auch nach dem Sprung in der Luft weiterhin nach links und rechts
	 * lenken. Bei uns ist das noch wie in der Panzersteuerung sehr starr. ...
	 * Lara kann dadurch einen viel weiteren Radius um eine Kante springen."
	 *
	 * WHAT THE CLASSIC HAS, measured routine by routine in game\lara.cpp before
	 * this number was picked: lara_as_forwardjump (:851) is the ONLY airborne
	 * state that reads a turn bit at all - it ramps lara.turn_rate by 409 a
	 * tick against a cap of 546, and LaraAboveWater decays it by 364 in the
	 * same tick, so what survives is 182 units, 1.0 degree a tick. M5 counted
	 * it: seven jumps of 40 ticks turned between 1 and 36 degrees in total.
	 * lara_as_reach (:2423), lara_as_swandive (:3186), lara_as_fastdive
	 * (:3215), lara_as_upjump (:2881), lara_as_fastfall (:2159) and
	 * lara_as_fallback (:703) read no direction bit whatsoever.
	 *
	 * SO ONLY STATE 3 GETS IT, and that is a decision per state and not an
	 * oversight (ModernStateAllowsAirTurn carries the list): the reach is the
	 * grab, and where her nose points decides which ledge lara_col_reach tests
	 * against - turning her inside it would move the grab itself, which the
	 * user did not ask for and the Remaster videos do not show. The dives and
	 * the falls have no classic steering to widen either. The twist, the swan
	 * dive and the grab keep reading their own bits untouched.
	 *
	 * 728 is 4 degrees a tick, four times what the classic manages, so a 40
	 * tick jump can turn 160 degrees where it turned 36 - "ein viel weiterer
	 * Radius um eine Kante" without her being able to spin in mid air. It is a
	 * "measure ourselves" number: MODERNLOG's `air=` prints the step of every
	 * tick it is applied, and `lara=` the heading it built. */
	MODERN_AIR_TURN_RATE = 728,		// 4 deg/tick; the classic forward jump manages 182 (1 deg)

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
	 * table.
	 *
	 * AND UNDER WATER THAT ENGINE NUMBER WAS A LID, NOT A SPEED (round 161a3,
	 * item 6). "Der Wendekreis unter Wasser ist noch zu hoch, der ist im
	 * Remaster auch auf der Stelle. Auch die allgemeine Dynamik: es fuehlt sich
	 * noch ein bisschen sluggish an." M7 measured 319 of 452 water ticks
	 * stepping at exactly 6 degrees, with the error while pushed at a median of
	 * 44 and a p90 of 133 degrees - up to 59 ticks in a row over 45. 1092 is
	 * where SwimTurn's RAMP stops (409 a tick into lara.turn_rate, clamped at
	 * +-1092, decayed 364 by LaraUnderWater): the ceiling of an accumulator the
	 * modern path does not use at all, because game\modern_state.cpp writes
	 * y_rot itself and zeroes lara.turn_rate. Nothing else in the water holds
	 * the heading down - measured: LaraUnderWater's only other clamps are on
	 * x_rot (+-15470) and z_rot (+-4004), and neither touches y_rot - so a new
	 * rate really arrives in `lara=`. 2730 is 15 degrees a tick, half the
	 * land's 30 because she is swimming and not running, and it settles a half
	 * turn in 13 ticks where the lid took 31.
	 *
	 * THE SURFACE KEEPS ITS OWN, and that is a measurement too: there the
	 * states write y_rot directly (728 per tick in lara_as_surftread and
	 * lara_as_surfswim, 364 in the three side and back strokes, larasurf.cpp),
	 * with no accumulator and no clamp anywhere. 728 is therefore a RATE and
	 * not a lid, so there is nothing there to lift, and the user's sentence is
	 * about "unter Wasser". If the play test finds treading sluggish too, these
	 * two are where it is answered.
	 *
	 * THE BANK IS UNCHANGED AND STAYS PLAUSIBLE: the roll is 546 a tick while
	 * the turn runs against LaraUnderWater's 364 of decay, so she leans by 182
	 * a tick whatever the rate is, and a faster turn simply finishes before the
	 * +-4004 clamp is reached - 13 ticks of 182 is 2366, a shallower lean and
	 * no snap of z_rot. */
	MODERN_SWIM_TURN_RATE = 2730,	// 15 deg/tick; the old 1092 was SwimTurn's accumulator ceiling
	MODERN_SURF_TURN_RATE = 728,	// lara_as_surftread / lara_as_surfswim - a real rate, not a lid
	MODERN_SURF_SIDE_RATE = 364,	// lara_as_surfback / surfleft / surfright

	/* The pitch. SwimTurn's own step for IN_FORWARD/IN_BACK is 364 a tick, 2
	 * degrees, which is also the look camera's rate - so following the orbit
	 * used to cost exactly as long as pitching by hand does (see the doubling
	 * at the foot of this block). The limits are LaraUnderWater's own clamp on
	 * x_rot (laraswim.cpp:593-596).
	 *
	 * THE TWO CLAMPS ARE NOT THE SAME RANGE, and the first version of this
	 * comment said otherwise (#161, round 161a2, fix round - the review's fifth
	 * finding). Her body is clamped here at +-15470. The CAMERA is clamped at
	 * -10923 .. +15470 since round 161a2b cut the sphere's top cap for the
	 * user's "Wenn ich direkt ueber Lara bin ist es sehr buggy"
	 * (MODERN_CAMERA_PITCH_MIN, item 4a), and ModernCameraSwimPitch takes
	 * pitch_home out of the elevation on top of that - so the pointer really
	 * asks for -9103 .. +17290.
	 *
	 * WHAT THAT COSTS, stated rather than claimed away. Upwards, the last 1820
	 * units of the camera's travel move the view alone, because her nose is
	 * already at its own limit. Downwards, the steepest DIVE a modern swimmer
	 * can ask for is 9103 (50 degrees), where round 161a2 reached 13650 (75)
	 * and Tank's own IN_FORWARD still reaches 15470 (85): the cap was cut for
	 * the LAND camera's sake, and the water pays for it because it borrows that
	 * same elevation as a pointer. If the play test finds diving shallow, HERE
	 * is where it is answered - the water can map its own range out of the
	 * camera's - and not by giving the sphere its cap back.
	 *
	 * AND THE PITCH IS DOUBLED WITH THE TURN (round 161a3, item 6, the second
	 * half of "es fuehlt sich noch ein bisschen sluggish an"). 364 was SwimTurn's
	 * own step for IN_FORWARD/IN_BACK, and it is a real engine rate rather than a
	 * lid - but it is the rate of a bit being HELD, where ours is a pointer the
	 * body chases, and at 364 the body needs 43 ticks to follow the stick from
	 * level to straight up while the stick gets there in three. 728 is 4 degrees
	 * a tick and halves that to 22; it is a "measure ourselves" number, and
	 * MODERNLOG's own x_rot in `lara=` together with cam_pitch is what shows it. */
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
	 *   block_turn		a blocked forward becomes a pure turn
	 *
	 * AIM_STRAFE SHIPS OFF SINCE ROUND 161a2b (#161 item 8), on the user's own
	 * words after the first play test: "Kannst du in dem Workflow auch gleich
	 * das mit dem Laufen beim Schiessen implementieren? Das ist schon echt doof
	 * im Kampf, wenn sie nur schleichen kann." With it on, a weapon ready and
	 * the action key down put her in MODE AIM - side steps and a walk - for as
	 * long as she fires, which is TROne's rule and which he played as "dann ist
	 * dauerhaft der Schleichmodus an". Off, firing steers exactly as empty
	 * hands do: she runs, she turns, and only her arms and torso aim. The rule
	 * itself is not deleted, because it is still the whole of TROne's aiming
	 * design and ModernTest drives it with an explicit cfg (aiming(), and the
	 * AIM half of firing_run()); it is simply not what ships. The flips with a
	 * weapon drawn are a different rule (jump_dir) and are untouched. */
	MODERN_HOLD_COURSE = 1,
	MODERN_BACK_TURNS = 1,
	MODERN_AIM_STRAFE = 0,
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
	long snap_forward;	// step 4: error under which IN_FORWARD comes back
	long snap_forward_stand;// step 4: the same, in the four turn-on-the-spot states
	long stand_forward_ticks;// ticks of a standing turn that arrives nowhere; 0 switches it off
	long air_turn_rate;	// the forward jump's own turn, units per tick; 0 switches it off
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

	/* THE COURSE THE LAST ACTIVE TICK ENDED ON, and whether there was one
	 * (round 161a2b, #161 item 7b). The instant about-turn reads the two as
	 * one number - how far the course itself JUMPED - so that it fires on a
	 * flick of the thumb and not on error that piled up because the stick
	 * circles faster than Lara can turn.
	 *
	 * IT SURVIVES A RELEASE OF THE STICK ON PURPOSE, which is why it is not in
	 * ModernRelease with have_ref and the rest: a real flick from forward to
	 * back sweeps the stick THROUGH the centre, so one or two ticks of it read
	 * as no push at all, and a memory that forgot there would forget exactly
	 * the case the rule is for. Only ModernResetState clears it - a level
	 * start, a death, the scheme switched - and there `have_course` 0 simply
	 * means the first push has nothing to be a flick against. */
	short last_course;
	long have_course;

	/* WHICH WAY THE MODERN TURN LAST MOVED HER, -1, 0 or 1 (round 161a3, #161
	 * item 2b). Exactly half a turn is the same angle to the left and to the
	 * right, and the wrap of a 16-bit angle answers -32768 for both - so
	 * without a memory every exact reversal would go left by arithmetic
	 * accident, and a reversal that arrived at 180 again on the next tick could
	 * change its mind halfway and stand still. This is the tie-break, and it is
	 * the turn's own last direction rather than the stick's for a measured
	 * reason: on the tick a flick crosses the centre BOTH the course step and
	 * the stick step wrap to -32768 as well, so there is nothing in the stick
	 * left to read. It also keeps the swing monotone across a takeoff, because
	 * the air turn reads and writes the same field. */
	long last_turn;

	/* HOW LONG THIS STANDING TURN HAS BEEN GETTING NOWHERE (round 161a3, fix
	 * pass, MODERN_STAND_FORWARD_TICKS above). Ticks in a turn-on-the-spot state
	 * with the forward gate shut; every tick the direct turn does not take puts
	 * it back to 0, so it counts one unbroken turn and never the sum of two.
	 * MODERNLOG's stand_hold=. */
	long stand_hold;
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

	/* HOW FAR THE PLAYER'S OWN THUMB TURNED THE CAMERA LAST TICK (#161, round
	 * 161a3b, item 2), signed, and 0 whenever the orbit did not tick or moved
	 * by itself. The held course below is a WORLD course precisely so that a
	 * camera moving on its own leaves her path alone - and that rule ignored
	 * the camera when the PLAYER moved it too, so her course stood still while
	 * he swung the view and then jumped to the new angle the moment his left
	 * thumb happened to shift. M2 counted 34 such jumps in the water (median
	 * 13.6 degrees, at the water's own 15 deg/tick lid) and 57 on foot (median
	 * 13.2, up to the land's 30), which is the user's "unter Wasser ist es noch
	 * ein bisschen ruppig, als wuerde sie in ihrer Animation zur Seite
	 * stottern". The caller measures it, because only the caller can tell the
	 * thumb from the return and the carried swing (game\camera.cpp's
	 * ModernCameraPlayerStep). */
	short cam_player_step;
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
	short course_step;	// how far the course itself moved this tick - MODERNLOG's `cstep=`
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

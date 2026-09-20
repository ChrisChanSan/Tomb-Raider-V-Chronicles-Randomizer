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
#include "../tomb5/pch.h"
#include "modern_state.h"
#include "modern_move.h"		// the arithmetic - ModernTest.cpp checks that half
#include "control.h"			// GlobalCounter, the game's own logic tick
#include "camera.h"			// camera.pos / camera.target - the heading of the tick
#include "lara.h"			// lara, lara_item
#include "../specific/3dmath.h"		// phd_atan - the engine's own angle
#include "../specific/function_stubs.h"	// Log, gLogEnabled - MODERNLOG (step 5)
#include "../specific/input.h"	// joystick_raw_lx/ly/rx/ry, joystick_is_xinput - the MODERNLOG pad line (round 161a2b, step 5)

/* game\modern_move.h mirrors input_buttons by value, because it must compile
 * without the Direct3D type set behind global\types.h. This is the first file
 * in the engine with both headers open, so the question is asked here - the
 * same way interpolate.cpp asks it about camera_type. */
static_assert(IN_FORWARD == MODERN_IN_FORWARD && IN_BACK == MODERN_IN_BACK &&
	IN_LEFT == MODERN_IN_LEFT && IN_RIGHT == MODERN_IN_RIGHT &&
	IN_DRAW == MODERN_IN_DRAW && IN_WALK == MODERN_IN_WALK &&
	IN_LSTEP == MODERN_IN_LSTEP && IN_RSTEP == MODERN_IN_RSTEP &&
	IN_JUMP == MODERN_IN_JUMP,
	"input_buttons moved - update game\\modern_move.h");

/* THE ONE STATE. There is exactly one Lara and one stick, so this is a file
 * of statics rather than a struct somebody has to own and pass around. */
static MODERN_CFG modern_cfg;
static MODERN_STATE modern_state;

/* The tick bookkeeping. `modern_tick` is the value GlobalCounter had when the
 * scheme last ran, and `modern_seen` says whether it ever has. */
static ushort modern_tick;
static long modern_seen;

/* What the blocked counter measures against: where she was last tick, and
 * whether the last tick asked for forward AND a turn at once. */
static long modern_have_pos;
static long modern_last_x;
static long modern_last_z;
static long modern_want_move;

/* WHERE THE MODERN HALF OF THIS TICK LEFT HER HEADING, and the tick it was
 * left on (round 161a3, item 5). game\camera.cpp reads it a few hundred lines
 * of the SAME tick later and subtracts it from her heading as it stands then;
 * what is left over is the part of the tick no stick asked for - the roll, the
 * jump twist, the underwater roll - which is exactly the flip the camera owes
 * a swing for. `modern_turned_seen` keeps a stamp of 0 from answering. */
static short modern_turned_yaw;
static ushort modern_turned_at;
static long modern_turned_seen;

void ModernResetControls()
{
	ModernDefaults(&modern_cfg);
	ModernResetState(&modern_state);

	modern_tick = 0;
	modern_seen = 0;
	modern_have_pos = 0;
	modern_last_x = 0;
	modern_last_z = 0;
	modern_want_move = 0;
	modern_turned_yaw = 0;
	modern_turned_at = 0;
	modern_turned_seen = 0;
}

/* AND THE ONE QUESTION game\camera.cpp ASKS OF IT (round 161a3, item 5).
 *
 * The stamp is an equality and not the orbit's own "one tick apart", because
 * this is not a tick behind: game\control.cpp runs S_UpdateInput at :296 and
 * CalculateCamera at :677 of the SAME tick body, with GlobalCounter stepped
 * once before both (:292). So the answer is this tick's own or there is none.
 *
 * There is none whenever ModernUpdateInput did not get as far as the turn:
 * no Lara, a repeat call inside one logic tick (the menus and the FMV loop
 * call S_UpdateInput too), a break in the tick chain, and every tick the look
 * key holds the seam back. All four are ticks in which the camera has no
 * baseline to measure a flip against, and a carry seated without one would be
 * a swing for a heading change nobody can account for - so it answers 0 and
 * the camera does nothing at all. */
long ModernHeadingAfterInput(short* yaw)
{
	if (!modern_turned_seen || modern_turned_at != GlobalCounter)
		return 0;

	if (yaw)
		*yaw = modern_turned_yaw;

	return 1;
}

/* THE D-PAD, RETIRED (the user's decision 3, superseded by decision 8:
 * "das D-Pad wird [...] fuer die Binds frei", round 161a2 step 3).
 *
 * A1's `ModernDPadToStick` used to live here: the pad's cross reached this
 * function as the same two axes the stick does (specific\dxshell.cpp writes
 * +-20 into joystick_read_x/y for an XInput D-pad direction or a DirectInput
 * POV hat, OVER whatever the stick said), and since 20 of 128 is under
 * MODERN_WALK_BELOW (62), it boosted a vector that could only be the cross
 * to full deflection so a D-pad push ran rather than walked.
 *
 * Once the medipacks default onto the d-pad and its four directions become
 * bindable buttons (specific\input.cpp, specific\dxshell.cpp), the cross
 * must not ALSO still read as a movement stick, boosted or not - the user's
 * own words are "free for the binds", not "free for the binds AND still
 * walks her". The fix is upstream of this file rather than in it:
 * specific\dxshell.cpp keeps the left stick's OWN two axes beside the pair
 * the d-pad writes over, and specific\input.cpp's seam hands those to the
 * classic assembly and to ModernUpdateInput below, so nothing of the cross
 * reaches here for this function to boost in the first place, and the boost
 * itself is gone with it rather than left dead behind a call that no longer
 * exists.
 *
 * Step 3 put that upstream fix in the wrong file (it erased the +-20
 * signature in specific\input.cpp instead of keeping the stick), and the
 * second fix round moved it: the d-pad OVERWRITES the axes rather than
 * adding to them, so an erasure downstream stopped her outright when the
 * press happened to match the signature and veered her course when it did
 * not - which the medipacks on the d-pad made a thing a player does while
 * running. */

/* THE MODE NAME FOR MODERNLOG's `mode=` (step 5). Nothing else in the tree
 * ever turns a modern_mode into text - the tests compare the number (design
 * section 1.9) - so the mapping lives here, beside the one place that
 * prints it. */
static const char* ModernModeName(long mode)
{
	switch (mode)
	{
	case MODERN_MODE_RUN:	return "run";
	case MODERN_MODE_AIM:	return "aim";
	case MODERN_MODE_JUMP:	return "jump";
	case MODERN_MODE_BACK:	return "back";
	default:				return "off";	// MODERN_MODE_OFF, or a number nothing named yet
	}
}

long ModernUpdateInput(long bits, long stick_x, long stick_y, long action, long jump)
{
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short camera_yaw;	// the measured eye -> target vector - MODERNLOG's `cam=`
	short ref_yaw;		// and what the course is really seated on (161a2b, item 4b) - `ref=`
	short lara_yaw;		// her heading before the direct turn can change it - MODERNLOG's `lara=`
	long cam_dx, cam_dz;
	long dx, dz;
	long moved;
	long yaw;
	long water;		// round 161a2 step 4: the state's own swim turn rate, 0 on land
	long xrot;		// her attitude after the pointer has had its say
	long stroke;		// 1 = hold the stroke key this tick (water_auto_stroke)
	long air;		// round 161a3 item 4: the step the air turn applied - MODERNLOG's `air=`

	if (!lara_item)
		return bits;

	/* ONE CALL PER LOGIC TICK, AND ONLY THE GAME'S OWN.
	 *
	 * S_UpdateInput is not only the game's input function. The game-over menu
	 * (game\control.cpp), the inventory (game\newinv2.cpp), the FMV player
	 * (specific\fmv.cpp), the options and save screens (specific\LoadSave.cpp)
	 * and the wardrobe window (specific\hd\hd_outfitmenu.cpp) all call it in
	 * loops of their own, and in those loops the direction bits steer a MENU.
	 * A translation there would answer the menu with a heading, and the direct
	 * turn would turn Lara behind the picture.
	 *
	 * The honest test is the game's own tick counter: game\control.cpp raises
	 * GlobalCounter once per logic step, immediately before it calls
	 * S_UpdateInput, and nothing else in the tree touches it. So a call that
	 * does not follow a fresh count is not the game's tick and is left alone.
	 *
	 * A BREAK IN THE CHAIN IS A RESET - BUT ONLY THE BREAKS THIS COUNTER CAN
	 * SEE, AND IT CANNOT SEE ALL OF THEM (corrected in the second review pass
	 * of this round, where the claim that stood here was measured and found
	 * false). It sees every break in which ticks really ran while the seam
	 * stood aside: the title screen, first person, a cutscene, a spotcam, the
	 * look key. There the counter runs away from modern_tick while this
	 * function is not being called, and the difference on the way back is more
	 * than one.
	 *
	 * IT CANNOT SEE A LEVEL CHANGE OR A SAVEGAME RESTORE, because between two
	 * levels not one tick runs - game\gameflow.cpp leaves its loop at
	 * gfLevelComplete and the next level's priming ControlPhase is the next
	 * tick there is - so the counter stands still and the boundary arrives
	 * here as an ordinary step of one. That reset is hung where the level
	 * really starts instead, in game\gameflow.cpp beside
	 * InterpInvalidate("level start"), which is the other thing in the engine
	 * that holds state from the previous tick and has to be told the same
	 * news.
	 *
	 * AND THE INVENTORY IS DELIBERATELY NOT A RESET. The ring has no clock of
	 * its own either, so the tick after it is also a step of one - but the
	 * world did not move while it was open and her held course is still the
	 * course she was on, so carrying on is the right answer here rather than a
	 * hole in the guard.
	 *
	 * The tick after a break is skipped as well (a thirtieth of a second), so
	 * that the first tick back is measured against a position from this level
	 * and not against wherever she stood before. */
	if (modern_seen && GlobalCounter == modern_tick)
		return bits;

	if (!modern_seen || (ushort)(GlobalCounter - modern_tick) != 1)
	{
		ModernResetControls();

		/* MODERNLOG INIT (step 5), once per break in the chain. A level start
		 * reaches this branch too, and through the front door rather than by
		 * luck: game\gameflow.cpp calls ModernResetControls() before the
		 * level's priming tick, which puts modern_seen back to 0, and the
		 * first test above is then the one that answers. The other breaks -
		 * first person, a cutscene, the look key - trip the second test
		 * instead (see the guard's own comment) - a config worth
		 * naming once rather than folding into every tick line below.
		 * `scheme=modern` is written out rather than left implied: this line
		 * only ever runs from inside the seam's tomb5.control_modern guard,
		 * so it could only ever say "modern", but a log read without the
		 * source open should not have to know that. `rate` and `direct` are
		 * the two numbers the user's play test picks between (decision 5) -
		 * control_modern is the gate on the whole scheme, MODERN_DIRECT_TURN
		 * the second gate on this one mechanism inside it, and direct is the
		 * one of the two that can change without a rebuild.
		 * `walk_below`, `recenter` and `jump_dir` (round 161a-fix, #161 item
		 * 4) are the first play test's own numbers: walk_below and recenter
		 * are the two the test's log moved off their TROne defaults at all
		 * (game\modern_move.h), so a later log is worth reading back rather
		 * than trusted from memory, and jump_dir is worth the same read now
		 * that it also decides whether AS_COMPRESS can reach mode RUN (item
		 * 3) and therefore whether ModernStateAllowsDirectTurn's tenth state
		 * ever answers - a play test that asks "why did she not turn in the
		 * crouch" has the switch on the same line as the rate that would have
		 * turned her.
		 *
		 * `cam_speed`, `cam_yaw_rate`, `cam_pitch_min`, `cam_pitch_max`,
		 * `aim_strafe`, `dead_l` AND `dead_r` ARE ROUND 161a2b's OWN (item 5,
		 * the play test's second round): the Camera speed slider and its
		 * derived rate, the sphere's two poles now that item 4a moved one of
		 * them, whether firing still runs (item 8, ships off - "off" is the
		 * value, not the absence of the field), and the two radial dead
		 * zones item 6 gave the sticks. `dead_l` and `dead_r` print the ONE
		 * the engine is actually using this session - MODERN_DEAD_LEFT /
		 * MODERN_DEAD_RIGHT on XInput, MODERN_DEAD_LEFT_DINPUT /
		 * MODERN_DEAD_RIGHT_DINPUT on the DirectInput fallback this user's
		 * own pad takes (round 161a3 gave dead_l its own DirectInput
		 * constant, item 7 - before that this line always printed the
		 * XInput one, which is why dead_l never moved with M9's own
		 * measurement until now) - which is also why the MODERNLOG pad line
		 * beside this one carries the raw axes: the DirectInput zone was
		 * never measured against this pad's real rest position, only
		 * guessed wide enough, and the pad line is what the next play test
		 * reads to say whether it can come down.
		 *
		 * air_rate, swim_rate AND swim_pitch_rate ARE ROUND 161a3's OWN
		 * (items 4 and 6): the live cfg's own air_turn_rate, swim_turn_rate
		 * and swim_pitch_rate, read back rather than trusted from the header
		 * for the same reason rate already is - a measure ourselves constant
		 * is only honest in a log if a hand edit of the registry would show
		 * up in it too. */
		if (gLogEnabled)
		{
			/* THE ORBIT'S OWN TWELVE NOW (round 161a2, step 5; four more in
			 * round 161a2b, step 5; two more in round 161a3, step 5), the same
			 * discipline as the five above: the live cfg game\camera.cpp seats,
			 * not the raw enum in game\modern_camera.h, and only the numbers
			 * steps 1, 2, 4 and 5's own reports named as the play test's to move
			 * or to read back - cam_return / cam_return_min, cam_wall_factor,
			 * cam_hide_dist, cam_invert, cam_pitch_return, cam_speed /
			 * cam_yaw_rate / cam_pitch_min / cam_pitch_max, and now
			 * cam_pull_steps / cam_pull_push - item 1's own ray walk, constants
			 * rather than measure ourselves numbers, printed anyway because a
			 * play test that asks why the eye still pumps in and out has the
			 * granularity on the same line as everything else about the orbit.
			 * game\camera.h says which of MODERN_CAMERA_CFG's remaining fields
			 * were left out and why. */
			long cam_return, cam_return_min, cam_wall_factor, cam_hide_dist;
			long cam_invert, cam_pitch_return;
			long cam_speed, cam_yaw_rate, cam_pitch_min, cam_pitch_max;
			long cam_pull_steps, cam_pull_push;
			long dead_l, dead_r;

			ModernCameraLogConfig(&cam_return, &cam_return_min, &cam_wall_factor,
				&cam_hide_dist, &cam_invert, &cam_pitch_return,
				&cam_speed, &cam_yaw_rate, &cam_pitch_min, &cam_pitch_max,
				&cam_pull_steps, &cam_pull_push);

			dead_l = joystick_is_xinput ? MODERN_DEAD_LEFT : MODERN_DEAD_LEFT_DINPUT;
			dead_r = joystick_is_xinput ? MODERN_DEAD_RIGHT : MODERN_DEAD_RIGHT_DINPUT;

			Log("MODERNLOG init scheme=modern rate=%d direct=%d walk_below=%d recenter=%d "
				"jump_dir=%d cam_return=%d cam_return_min=%d cam_wall_factor=%d "
				"cam_hide_dist=%d cam_invert=%d cam_pitch_return=%d "
				"cam_speed=%d cam_yaw_rate=%d cam_pitch_min=%d cam_pitch_max=%d "
				"aim_strafe=%d dead_l=%d dead_r=%d "
				"air_rate=%d swim_rate=%d swim_pitch_rate=%d "
				"cam_pull_steps=%d cam_pull_push=%d",
				modern_cfg.turn_rate, modern_cfg.direct_turn, modern_cfg.walk_below,
				modern_cfg.recenter, modern_cfg.jump_dir, cam_return, cam_return_min,
				cam_wall_factor, cam_hide_dist, cam_invert, cam_pitch_return,
				cam_speed, cam_yaw_rate, cam_pitch_min, cam_pitch_max,
				modern_cfg.aim_strafe, dead_l, dead_r,
				modern_cfg.air_turn_rate, modern_cfg.swim_turn_rate, modern_cfg.swim_pitch_rate,
				cam_pull_steps, cam_pull_push);

			/* MODERNLOG PAD (round 161a2b, item 6 and item 5's own log
			 * obligation), once beside the init line above and for the same
			 * reason: the DirectInput right stick's zone (dead_r above) was
			 * sized from a comment's claim ("500 of 1000 is wide enough"),
			 * never from a measurement of where that axis actually rests -
			 * these four are that measurement, raw and unrounded, in the
			 * same /256 or /8 units dead_l/dead_r are already in
			 * (specific\input.h). `xinput` says which pair of units they are
			 * in without the reader having to cross-check against dead_r's
			 * own choice above. A resting stick reads near 0 on a well
			 * centred pad; a value near MODERN_DEAD_RIGHT_DINPUT's own 62 on
			 * the DirectInput axes is the off-centre rest that constant was
			 * sized for, and is the number to compare it against. */
			Log("MODERNLOG pad lx=%ld ly=%ld rx=%ld ry=%ld xinput=%ld",
				joystick_raw_lx, joystick_raw_ly, joystick_raw_rx, joystick_raw_ry,
				joystick_is_xinput);
		}

		modern_tick = GlobalCounter;
		modern_seen = 1;
		return bits;
	}

	modern_tick = GlobalCounter;

	// ModernDPadToStick used to sit here (round 161a, decision 3) - see the
	// retirement note above this function for why it is gone rather than
	// merely unused: stick_x/stick_y are the left stick's own axes in
	// Modern (specific\dxshell.cpp keeps them beside the pair the d-pad
	// writes over, specific\input.cpp hands them here), so the cross never
	// reaches this call at all and it would never have fired again anyway.

	/* THE CAMERA HEADING OF THIS TICK (design section 5.3).
	 *
	 * From the camera's own two points, not from camera.actual_angle: that
	 * one is lara_item->pos.y_rot plus an offset, so taking it as the
	 * reference would feed her own heading back into her heading - the
	 * feedback loop that made the first draft of this scheme run in circles.
	 *
	 * It is the SIMULATION's camera and not the drawn one, for two reasons
	 * that hold without any help from us: game\control.cpp calls
	 * CalculateCamera after the input, so this reads the camera as the last
	 * tick left it, and game\interpolate.cpp puts the unblended pair back
	 * before the tick begins.
	 *
	 * A camera sitting exactly on its own target has no heading. That happens
	 * for a frame after a cut; falling back to Lara's own heading makes the
	 * scheme behave exactly like Tank for that one tick, which is the
	 * quietest thing it can do. */
	cam_dx = camera.target.x - camera.pos.x;
	cam_dz = camera.target.z - camera.pos.z;
	camera_yaw = (cam_dx || cam_dz) ? (short)phd_atan(cam_dz, cam_dx) : lara_item->pos.y_rot;

	/* AND UNDER THE ORBIT THE REFERENCE IS WHAT THE PLAYER DIALLED, not what
	 * the eye happens to have been pushed to (round 161a2b, item 4b).
	 *
	 * The two lines above are the honest reading for a camera nobody steers, and
	 * they stay the fallback. They stop being honest as soon as the eye is CLOSE
	 * to her, and the play test measured it twice over: at a steep pitch the
	 * horizontal radius is a few dozen units while the eye trails her run by
	 * hundreds, so "eye -> target" becomes her own running direction, the course
	 * runs away from her and she turns 3277 units every tick the stick is off
	 * centre; and a wall that pulls the eye in does the same at any pitch - 57
	 * jumps of more than 45 degrees in a single tick, her course after them
	 * reading 3154, 17126, 26721, -22504 (M4 of the round's brief). Item 3 of
	 * this round brings the eye close far more often, by design, so this is not a
	 * refinement - it is what makes item 3 safe.
	 *
	 * The world yaw has neither failure: it is the angle the ideal is built from,
	 * it is exact at every pitch, and geometry cannot move it. It is one tick
	 * behind, exactly as camera_yaw is and for the same reason (game\control.cpp
	 * runs the input before CalculateCamera).
	 *
	 * WITHOUT THE ORBIT NOTHING CHANGES: a flyby, a cutscene, first person, the
	 * title - ModernCameraActive() is false in every one of them and the measured
	 * vector is the reference, as it has been since A1. So is the one tick before
	 * the orbit's first, where it is not seated yet and has no world angle to
	 * give.
	 *
	 * AND THE RICH2 SNIPER CAMERA IS IN THAT LIST TOO, but it does not get there
	 * through ModernCameraActive() and the first version of this comment said it
	 * did (#161, round 161a2b, the second review pass). That predicate reads the
	 * option, lara_item, the title, the spotcam, the cutscene and first person -
	 * not the camera the engine is actually running. The sniper is the one level
	 * camera Modern keeps rather than discards, so FixedCamera runs, the orbit
	 * does not tick, and ModernCameraActive() is true through all of it. What
	 * answers it is game\camera.cpp's own stamp: ModernCameraWorldYaw declines
	 * unless the orbit ran on the tick before this one, so the line below falls
	 * back to the measured vector for as long as the sniper has the view - which
	 * is the right reference there, being the camera the player can see.
	 *
	 * MODERNLOG keeps printing BOTH: `cam=` is the measured vector above and
	 * `ref=` is what the translation really used (out.reference), so a play test
	 * can see the two part company at a wall instead of having to believe it. */
	if (!(ModernCameraActive() && ModernCameraWorldYaw(&ref_yaw)))
		ref_yaw = camera_yaw;

	/* IS FORWARD GETTING HER ANYWHERE (design section 1.3, finding A).
	 *
	 * Measured from her position and from nothing else, so that a wall, a
	 * crate, a railing and a corner all count the same and no collision
	 * internals are involved. Counted ONLY while the last tick asked for
	 * forward AND a turn together, because that is the one combination the
	 * classic automaton cannot express - a start from a standstill is
	 * aligned and forward alone, and must not be read as a wall. */
	dx = lara_item->pos.x_pos - modern_last_x;
	dz = lara_item->pos.z_pos - modern_last_z;

	if (dx < 0)
		dx = -dx;

	if (dz < 0)
		dz = -dz;

	moved = (!modern_have_pos || dx >= modern_cfg.block_eps || dz >= modern_cfg.block_eps) ? 1 : 0;
	modern_last_x = lara_item->pos.x_pos;
	modern_last_z = lara_item->pos.z_pos;
	modern_have_pos = 1;

	if (moved)
		modern_state.blocked_count = 0;
	else if (modern_want_move && modern_state.blocked_count < 1000)
		modern_state.blocked_count++;

	sit.anim_state = lara_item->current_anim_state;

	/* WADING IS CLASSIC, AND SO IS A DEAD LARA - and the water itself is not,
	 * any more (round 161a2 step 4). A1 wrote this line as "anything but
	 * LW_ABOVE_WATER", because it had no camera pitch to swim by; the orbit is
	 * there now, so LW_UNDERWATER and LW_SURFACE fall through to the state
	 * table, which names the eight states the water really steers and the four
	 * handovers it does not (game\modern_move.cpp).
	 *
	 * LW_WADE stays exactly as A1 left it and is named rather than implied:
	 * there LaraAboveWater runs and the states are the ordinary ground ones,
	 * so letting it through would change the feel of walking in shin-deep
	 * water - a change nobody asked for, in a round that is about the water
	 * she swims in. LW_FLYCHEAT needs no term at all: DOZY forces
	 * AS_SWIMCHEAT (118) and the state table already calls that classic.
	 *
	 * The hit points are the death case the brief asks for: the automaton
	 * stops answering there, and her last pose is the animation's to keep. */
	sit.classic = (lara.water_status == LW_WADE || lara_item->hit_points <= 0) ? 1 : 0;

	/* A weapon out and shootable. A torch is held in the same hand and is not
	 * a weapon - aiming with it would strafe her around nothing. */
	sit.drawn = (lara.gun_status == LG_READY && lara.gun_type != WEAPON_TORCH) ? 1 : 0;
	sit.action = action ? 1 : 0;
	sit.jump = jump ? 1 : 0;
	sit.blocked = (modern_cfg.block_frames > 0 &&
		modern_state.blocked_count >= modern_cfg.block_frames) ? 1 : 0;

	/* IS THIS TICK THE WATER'S, and at what rate (round 161a2 step 4)? Asked
	 * once, here, from the state alone, and answered by the same table the
	 * translation asks - so the branch below and the two rules the water
	 * switches off inside ModernTranslate cannot drift apart. The other two
	 * start as "nothing happened": the water is the only thing that writes
	 * them, and the log line at the foot of this function reads them whether
	 * it ran or not. */
	water = ModernWaterTurnRate(&modern_cfg, sit.anim_state);
	xrot = lara_item->pos.x_rot;
	stroke = 0;
	air = 0;

	lara_yaw = lara_item->pos.y_rot;

	ModernTranslate(&modern_cfg, &modern_state, stick_x, stick_y, ref_yaw,
		lara_yaw, &sit, &out);

	/* THE DIRECT TURN (the user's decision 5). The decision is arithmetic and
	 * lives with the rest of it; this is the one line of engine it needs.
	 * lara.turn_rate goes to zero because game\lara.cpp adds it to y_rot a
	 * moment later in LaraAboveWater - without that, she would turn twice.
	 *
	 * The order is the order of the tick: S_UpdateInput runs before
	 * AnimateLara and before the collision, so a heading written here is the
	 * heading she animates and collides with this tick, not next. The picture
	 * glides on its own - game\interpolate.cpp snapshots her position BEFORE
	 * the input runs and blends y_rot with LerpAngle, so nothing had to be
	 * added there for 60 or 165 Hz.
	 *
	 * IN THE WATER IT IS THE WATER'S OWN MOVE INSTEAD, and the two are an
	 * either/or rather than one calling the other (round 161a2 step 4): the
	 * water turns at the rate the state it is in turns at, takes no instant
	 * about-turn, sends out no IN_FORWARD under water - down there that bit is
	 * SwimTurn's pitch - and answers two things the land has no notion of, the
	 * attitude and the stroke. `water` is the state's rate and the question
	 * "is this the water?" in one, so the branch cannot drift from the table.
	 *
	 * THE POINTER IS ONLY A POINTER WHILE THE ORBIT IS STEERING THE VIEW. Where
	 * it is not - first person, and nowhere else the seam is reachable from -
	 * her own attitude goes in as the target, which asks for no change at all
	 * and leaves the third axis hers. The flat circle and the stroke still
	 * work there, because those two read the camera's HEADING, which first
	 * person has as much as the orbit does. */
	if (water)
	{
		if (ModernWaterMove(&modern_cfg, &sit, &out,
			ModernCameraActive() ? ModernCameraSwimTarget() : lara_item->pos.x_rot,
			lara_item->pos.x_rot, &yaw, &xrot, &stroke))
		{
			lara_item->pos.y_rot = ModernWrap((long)lara_item->pos.y_rot + yaw);
			lara.turn_rate = 0;
			lara_item->pos.x_rot = (short)xrot;

			/* THE BANK THE TURN BITS WOULD HAVE MADE. SwimTurn rolls her by
			 * this much on every tick a turn bit is held (game\laraswim.cpp),
			 * and we have just taken those bits away - so the roll is owed,
			 * not optional, or the modern swimmer would corner flat where the
			 * classic one leans. The decay and the +-4004 clamp are still
			 * LaraUnderWater's own, a few lines later in the same tick, so a
			 * held turn settles exactly where the classic one settles. The
			 * surface has no bank to owe: LaraSurface zeroes z_rot every tick
			 * whatever anybody writes. */
			if (yaw && ModernStateIsSwim(sit.anim_state))
				lara_item->pos.z_rot = (short)(lara_item->pos.z_rot +
					((yaw > 0) ? modern_cfg.swim_roll_rate : -modern_cfg.swim_roll_rate));
		}
	}
	else if (ModernDirectTurn(&modern_cfg, &modern_state, &sit, &out, &yaw))
	{
		lara_item->pos.y_rot = ModernWrap((long)lara_item->pos.y_rot + yaw);
		lara.turn_rate = 0;
	}
	/* AND IN THE AIR THE SAME LINE AGAIN, AT THE JUMP'S OWN RATE (round 161a3,
	 * item 4). "Im Remaster kann sie auch nach dem Sprung in der Luft weiterhin
	 * nach links und rechts lenken. ... Lara kann dadurch einen viel weiteren
	 * Radius um eine Kante springen."
	 *
	 * A THIRD ARM OF THE SAME EITHER/OR, and it can be one because the three
	 * lists do not overlap: ModernWaterTurnRate answers only the eight swim and
	 * surface states, ModernStateAllowsDirectTurn the ten on the ground, and
	 * ModernStateAllowsAirTurn the one in the air (3, AS_FORWARDJUMP - the only
	 * airborne routine in game\lara.cpp that reads a turn bit at all).
	 * ModernTest asserts the three-way exclusivity over all 139 states, so the
	 * order of the arms cannot come to matter later.
	 *
	 * turn_rate goes to zero for the same reason as on the ground:
	 * lara_as_forwardjump ramps it from IN_LEFT / IN_RIGHT and LaraAboveWater
	 * adds it to y_rot a moment later, so without this she would turn twice -
	 * once at our rate and once at the classic's 182 units. ModernAirTurn has
	 * already taken those two bits out of out.bits, which is what the write
	 * below hands on.
	 *
	 * IT IS BEFORE THE PUBLISH LINE ON PURPOSE. game\camera.cpp subtracts the
	 * heading published three lines down from her heading after the animation,
	 * and calls what is left an animation about-turn worth a camera swing (item
	 * 5). A turn written after the publish would be read as exactly that. */
	else if (ModernAirTurn(&modern_cfg, &modern_state, &sit, &out, &air))
	{
		lara_item->pos.y_rot = ModernWrap((long)lara_item->pos.y_rot + air);
		lara.turn_rate = 0;
	}

	/* AND THE HEADING THE STICK IS ACCOUNTABLE FOR IS PUBLISHED HERE (round
	 * 161a3, item 5). It is READ BACK OFF THE ITEM rather than composed out of
	 * lara_yaw and `yaw`, so that a branch above which turns her some other way
	 * - the water's own move, and whatever a later round adds beside it -
	 * cannot drift away from it: whatever this file did to her heading this
	 * tick, this is where it ended. Everything that happens to it AFTER this
	 * line happens in AnimateLara and the collision, which is what
	 * game\camera.cpp is left holding. */
	modern_turned_yaw = lara_item->pos.y_rot;
	modern_turned_at = GlobalCounter;
	modern_turned_seen = 1;

	/* THE ONLY WRITE TO THE INPUT WORD. When the translation says it is not
	 * responsible - a classic state, the stick at rest - the bits the stick
	 * and the keyboard wrote simply stand, which is why the pad still moves
	 * her on a ladder and up a rope without a line of code for it. (The water
	 * used to be in that list; round 161a2 step 4 gave it its own.) */
	if (out.active)
		bits = (bits & ~(long)MODERN_MOVE_MASK) | out.bits;
	else
		modern_state.blocked_count = 0;

	/* THE STROKE IS ADDED AND NEVER TAKEN AWAY (round 161a2 step 4). It is
	 * IN_JUMP, which is not in MODERN_MOVE_MASK and so was not cleared by the
	 * line above - deliberately: the player's own jump key must keep working
	 * as the stroke it has always been, with water_auto_stroke on or off, and
	 * a mask that swallowed it would have made the switch a trap. So this can
	 * only ever turn the bit ON, on the ticks the stick is out under water. */
	if (stroke)
		bits |= MODERN_IN_JUMP;

	/* For the next tick's blocked counter. Note that with the direct turn on,
	 * forward and a turn bit never go out together - the direct turn takes
	 * the turn bits and does the turning itself - so this stays 0 and the
	 * blocked rule sleeps. That is correct rather than accidental: the
	 * lock-up it guards against is the automaton refusing to turn while
	 * IN_FORWARD is held, and with the direct turn there is no automaton in
	 * the loop. Set MODERN_DIRECT_TURN to 0 and the rule wakes up again. */
	modern_want_move = (out.active && (out.bits & MODERN_IN_FORWARD) != 0 &&
		(out.bits & (MODERN_IN_LEFT | MODERN_IN_RIGHT)) != 0) ? 1 : 0;

	/* MODERNLOG (step 5, design section 7 step 5). One line per real tick,
	 * whether or not the translation ended up responsible for it - `mode=off`
	 * is itself the answer to "why did nothing happen here" and earns a line
	 * exactly as much as a running one does, which is also why this sits
	 * after every early return above rather than being reachable from one of
	 * them: 30 a second while the seam runs, at every fps_mode, because it
	 * hangs on the logic tick and not on a drawn frame.
	 *
	 * NEVER MORE - BUT IT CAN BE FEWER, AND A LINE COUNT HAS TO EXPECT THAT.
	 * The reset branch above returns before this line, so the first tick back
	 * from a level start, a savegame, first person, a cutscene or the look key
	 * shows its `init` line and no tick line. The inventory is the one break
	 * that is NOT in that list, because nothing resets for it (the guard's own
	 * comment says why): the tick after the ring closes is an ordinary tick
	 * line. And while the look key is down, or the ring is open, or a cutscene
	 * is playing, the seam is not called at all and neither line appears. So a
	 * gap in `f=` is
	 * the scheme standing aside, not a dropped line - count against the `f=`
	 * numbers, which are GlobalCounter and never lie, rather than against the
	 * wall clock.
	 *
	 * `src` says which half produced the bits this tick (design section 5.9):
	 * "pad" is the stick driving `out`, "keys" is `out` stepping aside and
	 * the base assembly's bits - the classic stick scaling, the keyboard, or
	 * Steam Input's own synthetic arrow keys - standing untouched, so a
	 * Steam report is one grep away.
	 *
	 * `bits` is `out.bits`, the translation's OWN contribution (0 whenever
	 * `mode=off`) - not the merged word this function returns. A classic
	 * tick or a dead stick can still show movement in the game from the
	 * untouched base bits, and that is finding B (section 1.3), not a
	 * contradiction of this line. `lara` reads the local captured above the
	 * direct turn, because by the time this line runs the direct turn may
	 * already have written a new heading.
	 *
	 * `cstep` is how far the COURSE itself moved this tick (round 161a2b, #161
	 * item 7b). It was the instant about-turn's second condition until round
	 * 161a3 deleted that rule (item 2b - a reversal is turned at the rate like
	 * everything else now), and it stays as a LOG FIELD, because it is still
	 * the one number that separates a flick of the thumb from error that piled
	 * up while the stick outran her turn: a smooth circle holds `cstep` near
	 * the stick's own step a tick, and only a flick shows a figure over 16384.
	 *
	 * `cam_world` AND `clip` ARE THE LINE'S LAST TWO, AFTER `cstep` AND NOT
	 * BEFORE IT (round 161a2b, items 1 and 3): cstep already sat at the tick
	 * line's own end when this step started, and both of these read the SAME
	 * one-tick-behind state cam_off/cam_pitch/cam_ret already do, for the
	 * same reason (this line runs before CalculateCamera). `cam_world` is
	 * the orbit's own remembered angle - what item 1 keeps fixed while she
	 * turns under it - 0 before its first seating exactly as `ref=` falls
	 * back to the measured vector until then. `clip` is the flag item 3's
	 * candidate loop writes and the return's own wall_factor reads, not a
	 * second measurement of a wall: 1 on a tick the eye was clipped, which
	 * is also the tick the return runs eight times as fast.
	 *
	 * THREE MORE AFTER `clip` (round 161a3, step 5), each owed by one of the
	 * round's own items and appended rather than worked into any of the seven
	 * above, so a reader who does not know about them still finds the line
	 * exactly where it always ended:
	 * `view_err` is item 1's own proof read back every tick rather than only
	 * inside ModernTest - abs(camera_yaw - cam_world) wrapped the short way
	 * exactly as `err` above already is, in the SAME raw units (0 == on the
	 * ray, 32768 == pointing the opposite way) rather than converted to
	 * degrees, so it reads beside `cam`/`cam_world`/`err`/`yawd` in the unit
	 * they already share instead of a unit of its own.
	 * `air` is item 4's own local, already in this function's scope (0
	 * whenever ModernAirTurn did not run this tick, which is every tick but
	 * the ones in AS_FORWARDJUMP with the stick held off centre).
	 * `carry` is item 5's swing still pending, MODERN_CAMERA_STATE::carry read
	 * back from game\camera.cpp for the same reason cam_off and its
	 * neighbours are - one tick behind, because this line runs before
	 * CalculateCamera pays any of it out. */
	if (gLogEnabled)
	{
		/* THE ORBIT'S OWN EIGHT NOW (round 161a2, step 5; two more in round
		 * 161a2b, step 5; one more in round 161a3, step 5), read back rather
		 * than kept in a local of this function's own: game\camera.cpp owns the
		 * one instance of both, the same division of labour camera_yaw above
		 * already keeps with that file. cam_off/cam_pitch/cam_ret/cam_world/
		 * clip/carry are one tick behind, exactly as camera_yaw is and for the
		 * same reason (this line runs before CalculateCamera); rs_x/rs_y are
		 * already this tick's own (game\camera.cpp's own comment on
		 * ModernCameraLogState says why).
		 * `hide` is the raw measured distance and not a flag, so a play test
		 * can watch it close on MODERN_CAMERA_HIDE_DIST rather than only
		 * learn that it crossed - and it is one tick behind WITH the others
		 * (round 161a2's fix round says so here because the first version of
		 * this comment named only three): it is camera.pos against
		 * camera.target, which are the points the LAST CalculateCamera
		 * left. */
		short cam_off, cam_pitch, cam_world;
		long cam_ret, cam_rs_x, cam_rs_y, clip, carry;
		long view_err;

		ModernCameraLogState(&cam_off, &cam_pitch, &cam_ret, &cam_rs_x, &cam_rs_y,
			&cam_world, &clip, &carry);

		view_err = ModernWrap((long)camera_yaw - (long)cam_world);

		if (view_err < 0)
			view_err = -view_err;

		Log("MODERNLOG f=%ld src=%s stick=%ld/%ld mag=%ld cam=%d ref=%d course=%d lara=%d "
			"err=%d state=%d mode=%s spin=%d aligned=%d still=%d blk=%d "
			"yawd=%d bits=%08lX cam_off=%d cam_pitch=%d cam_ret=%d rs=%ld/%ld hide=%d "
			"water=%d xrot=%d stroke=%d cstep=%d cam_world=%d clip=%d "
			"view_err=%ld air=%ld carry=%ld",
			(long)GlobalCounter, out.active ? "pad" : "keys", stick_x, stick_y, out.mag,
			camera_yaw, out.reference, modern_state.course, lara_yaw,
			out.delta, sit.anim_state, ModernModeName(out.mode), modern_state.spin,
			modern_state.aligned, modern_state.blocked_count, out.blocked,
			yaw, out.bits, cam_off, cam_pitch, cam_ret, cam_rs_x, cam_rs_y,
			ModernLaraEyeDistance(), water, xrot, stroke, out.course_step,
			cam_world, clip, view_err, air, carry);
	}

	return bits;
}

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

/* game\modern_move.h mirrors input_buttons by value, because it must compile
 * without the Direct3D type set behind global\types.h. This is the first file
 * in the engine with both headers open, so the question is asked here - the
 * same way interpolate.cpp asks it about camera_type. */
static_assert(IN_FORWARD == MODERN_IN_FORWARD && IN_BACK == MODERN_IN_BACK &&
	IN_LEFT == MODERN_IN_LEFT && IN_RIGHT == MODERN_IN_RIGHT &&
	IN_DRAW == MODERN_IN_DRAW && IN_WALK == MODERN_IN_WALK &&
	IN_LSTEP == MODERN_IN_LSTEP && IN_RSTEP == MODERN_IN_RSTEP,
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
 * specific\input.cpp's own seam now erases the same +-20 signature from its
 * local copy of the stick before either the classic assembly or
 * ModernUpdateInput below ever sees it, so nothing reaches here for this
 * function to boost in the first place, and the boost itself is gone with
 * it rather than left dead behind a call that no longer exists. */

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
	short camera_yaw;
	short lara_yaw;		// her heading before the direct turn can change it - MODERNLOG's `lara=`
	long cam_dx, cam_dz;
	long dx, dz;
	long moved;
	long yaw;

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
		 * turned her. */
		if (gLogEnabled)
			Log("MODERNLOG init scheme=modern rate=%d direct=%d walk_below=%d recenter=%d jump_dir=%d",
				modern_cfg.turn_rate, modern_cfg.direct_turn, modern_cfg.walk_below,
				modern_cfg.recenter, modern_cfg.jump_dir);

		modern_tick = GlobalCounter;
		modern_seen = 1;
		return bits;
	}

	modern_tick = GlobalCounter;

	// ModernDPadToStick used to sit here (round 161a, decision 3) - see the
	// retirement note above this function for why it is gone rather than
	// merely unused: specific\input.cpp already erases the d-pad's own
	// +-20 signature from stick_x/stick_y before this call, so it would
	// never have fired again anyway.

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

	/* WATER IS CLASSIC IN A1, and so is a dead Lara. The state table already
	 * names every swimming and surface state; the water STATUS catches the
	 * ticks in between - wading, going under, coming up - in one test.
	 * Workflow A2 takes this line out when it gives the water a camera pitch
	 * to swim by. The hit points are the death case the brief asks for: the
	 * automaton stops answering there, and her last pose is the animation's
	 * to keep. */
	sit.classic = (lara.water_status != LW_ABOVE_WATER || lara_item->hit_points <= 0) ? 1 : 0;

	/* A weapon out and shootable. A torch is held in the same hand and is not
	 * a weapon - aiming with it would strafe her around nothing. */
	sit.drawn = (lara.gun_status == LG_READY && lara.gun_type != WEAPON_TORCH) ? 1 : 0;
	sit.action = action ? 1 : 0;
	sit.jump = jump ? 1 : 0;
	sit.blocked = (modern_cfg.block_frames > 0 &&
		modern_state.blocked_count >= modern_cfg.block_frames) ? 1 : 0;

	lara_yaw = lara_item->pos.y_rot;

	ModernTranslate(&modern_cfg, &modern_state, stick_x, stick_y, camera_yaw,
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
	 * added there for 60 or 165 Hz. */
	if (ModernDirectTurn(&modern_cfg, &modern_state, &sit, &out, &yaw))
	{
		lara_item->pos.y_rot = ModernWrap((long)lara_item->pos.y_rot + yaw);
		lara.turn_rate = 0;
	}

	/* THE ONLY WRITE TO THE INPUT WORD. When the translation says it is not
	 * responsible - a classic state, the stick at rest - the bits the stick
	 * and the keyboard wrote simply stand, which is why the pad still moves
	 * her in the water and on a ladder without a line of code for it. */
	if (out.active)
		bits = (bits & ~(long)MODERN_MOVE_MASK) | out.bits;
	else
		modern_state.blocked_count = 0;

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
	 * already have written a new heading. */
	if (gLogEnabled)
		Log("MODERNLOG f=%ld src=%s stick=%ld/%ld mag=%ld cam=%d ref=%d course=%d lara=%d "
			"err=%d state=%d mode=%s spin=%d aligned=%d still=%d blk=%d "
			"yawd=%d bits=%08lX",
			(long)GlobalCounter, out.active ? "pad" : "keys", stick_x, stick_y, out.mag,
			camera_yaw, out.reference, modern_state.course, lara_yaw,
			out.delta, sit.anim_state, ModernModeName(out.mode), modern_state.spin,
			modern_state.aligned, modern_state.blocked_count, out.blocked,
			yaw, out.bits);

	return bits;
}

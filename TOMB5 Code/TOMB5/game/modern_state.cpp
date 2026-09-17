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

/* THE D-PAD, MADE FULL (the user's decision 3).
 *
 * The pad's cross keeps moving Lara in Modern, and it reaches us as the same
 * two axes the stick does: specific\dxshell.cpp writes +-20 into
 * joystick_read_x/y for an XInput D-pad direction and for a DirectInput POV
 * hat, OVER whatever the stick said. Twenty of a hundred and twenty-eight is
 * under MODERN_WALK_BELOW (62), so left alone a D-pad push would make her
 * walk and never run. So a vector that can only be the cross is taken as full
 * deflection in the same direction.
 *
 * "Can only be the cross" is exact on the XInput path: the hardware dead zone
 * (7849 of 32768) is applied before the division by 256, so a real stick axis
 * reads 0 or at least 30 - never 20. On the DirectInput fallback the axis is
 * ranged to +-1000, zeroed under 150 and divided by 8, so a stick parked at
 * exactly 20 (a hair above its own dead zone) on one axis with the other at 0
 * or 20 is possible and would be read as a cross press for that tick: she
 * runs instead of walking until the thumb moves a unit. That is the whole
 * cost, it is named here rather than hidden, and the alternative - a new
 * "this came from the cross" flag out of dxshell - would change a file the
 * byte-identity rule of this round keeps shut. */
static void ModernDPadToStick(long* x, long* y)
{
	const long dpad = 20;	// specific\dxshell.cpp, both joystick paths

	if ((*x != 0 && *x != dpad && *x != -dpad) || (*y != 0 && *y != dpad && *y != -dpad))
		return;

	if (!*x && !*y)
		return;

	if (*x)
		*x = (*x > 0) ? modern_cfg.stick_full : -modern_cfg.stick_full;

	if (*y)
		*y = (*y > 0) ? modern_cfg.stick_full : -modern_cfg.stick_full;
}

long ModernUpdateInput(long bits, long stick_x, long stick_y, long action, long jump)
{
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short camera_yaw;
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
	 * A BREAK IN THE CHAIN IS A RESET, and it is also the level start, the
	 * savegame, the death menu, the inventory, first person, a cutscene and
	 * the look key - each of them stops the seam for more than one tick. The
	 * tick after the break is skipped as well (a thirtieth of a second), so
	 * that the first tick back is measured against a position from this level
	 * and not against wherever she stood before. */
	if (modern_seen && GlobalCounter == modern_tick)
		return bits;

	if (!modern_seen || (ushort)(GlobalCounter - modern_tick) != 1)
	{
		ModernResetControls();
		modern_tick = GlobalCounter;
		modern_seen = 1;
		return bits;
	}

	modern_tick = GlobalCounter;

	ModernDPadToStick(&stick_x, &stick_y);

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

	ModernTranslate(&modern_cfg, &modern_state, stick_x, stick_y, camera_yaw,
		lara_item->pos.y_rot, &sit, &out);

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

	return bits;
}

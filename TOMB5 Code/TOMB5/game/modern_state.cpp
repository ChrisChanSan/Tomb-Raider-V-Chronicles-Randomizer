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
#include "../specific/function_stubs.h"	// Log, gLogEnabled - MODERNLOG
#include "../specific/input.h"	// joystick_raw_lx/ly/rx/ry, joystick_is_xinput - the MODERNLOG pad line
#include "../tomb5/tomb5.h"	// tomb5.dead_zone - the effective zones and dead_user=

/* game\modern_move.h mirrors input_buttons by value, because it must compile
 * without the Direct3D type set behind global\types.h. This is the first file
 * in the engine with both headers open, so the question is asked here - the
 * same way interpolate.cpp asks it about camera_type. */
static_assert(IN_FORWARD == MODERN_IN_FORWARD && IN_BACK == MODERN_IN_BACK &&
	IN_LEFT == MODERN_IN_LEFT && IN_RIGHT == MODERN_IN_RIGHT &&
	IN_DRAW == MODERN_IN_DRAW && IN_WALK == MODERN_IN_WALK &&
	IN_LSTEP == MODERN_IN_LSTEP && IN_RSTEP == MODERN_IN_RSTEP &&
	IN_JUMP == MODERN_IN_JUMP && IN_ACTION == MODERN_IN_ACTION &&
	IN_DUCK == MODERN_IN_DUCK && IN_SPRINT == MODERN_IN_SPRINT &&
	IN_INTERACT == MODERN_IN_INTERACT && IN_SELECT == MODERN_IN_SELECT,
	"input_buttons moved - update game\\modern_move.h");

/* The one state. There is exactly one Lara and one stick, so this is a file
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

/* Where the modern half of this tick left her heading, and the tick it was
 * left on. game\camera.cpp reads it later in the same tick and subtracts it
 * from her heading as it stands then;
 * what is left over is the part of the tick no stick asked for - the roll, the
 * jump twist, the underwater roll - which is exactly the flip the camera owes
 * a swing for. `modern_turned_seen` keeps a stamp of 0 from answering. */
static short modern_turned_yaw;
static ushort modern_turned_at;
static long modern_turned_seen;

/* Which camera the last tick's reference came from: 1 the orbit's world yaw,
 * 0 the measured vector, and `modern_ref_seen` whether there was a last tick.
 * A change between two ticks is MODERN_SITUATION::cam_cut. */
static long modern_ref_orbit;
static long modern_ref_seen;

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
	modern_ref_orbit = 0;
	modern_ref_seen = 0;
}

/* The heading ModernUpdateInput publishes, asked by game\camera.cpp.
 *
 * The stamp is an equality and not the orbit's "one tick apart", because this
 * is not a tick behind: game\control.cpp runs S_UpdateInput and then
 * CalculateCamera in the same tick body, with GlobalCounter stepped once
 * before both. So the answer is this tick's own or there is none.
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

/* The d-pad does not move Lara in Modern: its four directions are bindable
 * buttons (the medipacks by default). specific\dxshell.cpp writes +-20 into
 * joystick_read_x/y for a d-pad direction or a DirectInput POV hat, over
 * whatever the stick said, so it keeps the left stick's own two axes beside
 * that pair, and specific\input.cpp hands those to the classic assembly and to
 * ModernUpdateInput. Nothing of the cross reaches this file. Erasing the +-20
 * signature downstream instead would stop her outright when a press matches
 * it and veer her course when it does not, because the d-pad overwrites the
 * axes rather than adding to them. */

/* The mode name for MODERNLOG's `mode=`. Nothing else turns a modern_mode
 * into text - the tests compare the number - so the mapping lives beside the
 * one place that prints it. */
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

/* The source name for MODERNLOG's `feed=`, beside the one place that prints
 * it, for the same reason as the mode names above. */
static const char* ModernFeedName(long feed)
{
	switch (feed)
	{
	case MODERN_FEED_PAD:	return "pad";
	case MODERN_FEED_KEYS:	return "keys";
	default:				return "none";	// MODERN_FEED_NONE
	}
}

long ModernUpdateInput(long bits, long stick_x, long stick_y, long action, long jump, long feed)
{
	MODERN_SITUATION sit;
	MODERN_RESULT out;
	short camera_yaw;	// the measured eye -> target vector - MODERNLOG's `cam=`
	short ref_yaw;		// and what the course is really seated on - `ref=`
	long ref_orbit;		// 1 = ref_yaw is the orbit's world yaw, 0 = the measured vector
	short lara_yaw;		// her heading before the direct turn can change it - MODERNLOG's `lara=`
	long cam_dx, cam_dz;
	long dx, dz;
	long moved;
	long yaw;
	long water;		// the state's own swim turn rate, 0 on land
	long xrot;		// her attitude after the pointer has had its say
	long stroke;		// 1 = hold the stroke key this tick (water_auto_stroke)
	long air;		// the step the air turn applied - MODERNLOG's `air=`

	if (!lara_item)
		return bits;

	/* One call per logic tick, and only the game's own.
	 *
	 * S_UpdateInput is not only the game's input function. The game-over menu
	 * (game\control.cpp), the inventory (game\newinv2.cpp), the FMV player
	 * (specific\fmv.cpp), the options and save screens (specific\LoadSave.cpp)
	 * and the wardrobe window (specific\hd\hd_outfitmenu.cpp) all call it in
	 * loops of their own, where the direction bits steer a menu. A translation
	 * there would answer the menu with a heading, and the direct turn would
	 * turn Lara behind the picture.
	 *
	 * The test is the game's own tick counter: game\control.cpp raises
	 * GlobalCounter once per logic step, immediately before it calls
	 * S_UpdateInput, and nothing else touches it. A call that does not follow
	 * a fresh count is not the game's tick and is left alone.
	 *
	 * A break in the chain is a reset, but the counter sees only the breaks in
	 * which ticks ran while the seam stood aside: the title screen, first
	 * person, a cutscene, a spotcam, the look key. There the counter runs away
	 * from modern_tick, and the difference on the way back is more than one.
	 *
	 * It cannot see a level change or a savegame restore, because between two
	 * levels not one tick runs - game\gameflow.cpp leaves its loop at
	 * gfLevelComplete and the next level's priming ControlPhase is the next
	 * tick there is - so the counter stands still and the boundary arrives
	 * here as an ordinary step of one. That reset is hung where the level
	 * really starts instead, in game\gameflow.cpp beside
	 * InterpInvalidate("level start"), which is the other thing in the engine
	 * that holds state from the previous tick and has to be told the same
	 * news.
	 *
	 * The inventory is deliberately not a reset. The ring has no clock of its
	 * own either, so the tick after it is also a step of one - but the world
	 * did not move while it was open and her held course is still valid, so
	 * carrying on is right.
	 *
	 * The tick after a break is skipped as well (a thirtieth of a second), so
	 * that the first tick back is measured against a position from this level
	 * and not against wherever she stood before. */
	if (modern_seen && GlobalCounter == modern_tick)
		return bits;

	if (!modern_seen || (ushort)(GlobalCounter - modern_tick) != 1)
	{
		ModernResetControls();

		/* MODERNLOG init, once per break in the chain. A level start reaches
		 * this branch because game\gameflow.cpp calls ModernResetControls()
		 * before the level's priming tick, which puts modern_seen back to 0;
		 * the other breaks - first person, a cutscene, the look key - trip the
		 * second test. The config is printed once here rather than in every
		 * tick line below.
		 *
		 * Fields: `scheme=modern` is written out so a log reads without the
		 * source (this only runs inside the tomb5.control_modern guard).
		 * `rate` is the turn rate and `direct` the direct-turn switch
		 * (MODERN_DIRECT_TURN), the second gate inside control_modern.
		 * `walk_below`, `recenter` and `jump_dir` are tuned values of
		 * game\modern_move.h; walk_below and recenter differ from their TROne
		 * defaults, and jump_dir also decides whether AS_COMPRESS can reach mode
		 * RUN and so whether ModernStateAllowsDirectTurn's tenth state answers.
		 * `cam_speed`, `cam_yaw_rate`, `cam_pitch_min` and `cam_pitch_max` are
		 * the camera speed slider, its derived rate and the sphere's two poles.
		 * `aim_strafe` says whether firing still runs (off by default; "0" is
		 * the value, not an absent field). `dead_l` and `dead_r` are the radial
		 * dead zones the engine uses this session - MODERN_DEAD_LEFT /
		 * MODERN_DEAD_RIGHT on XInput, MODERN_DEAD_LEFT_DINPUT /
		 * MODERN_DEAD_RIGHT_DINPUT on the DirectInput fallback. `air_rate`,
		 * `swim_rate` and `swim_pitch_rate` are the live cfg's air_turn_rate,
		 * swim_turn_rate and swim_pitch_rate. Every value is read from the live
		 * cfg rather than the header, so a hand edit of the registry shows up. */
		if (gLogEnabled)
		{
			/* The orbit's twelve, from the live cfg game\camera.cpp seats rather
			 * than the enum in game\modern_camera.h: cam_return /
			 * cam_return_min, cam_wall_factor, cam_hide_dist, cam_invert,
			 * cam_pitch_return, cam_speed / cam_yaw_rate / cam_pitch_min /
			 * cam_pitch_max, and cam_pull_steps / cam_pull_push, the ray walk's
			 * granularity. game\camera.h says which of MODERN_CAMERA_CFG's
			 * remaining fields are left out and why. */
			long cam_return, cam_return_min, cam_wall_factor, cam_hide_dist;
			long cam_invert, cam_pitch_return;
			long cam_speed, cam_yaw_rate, cam_pitch_min, cam_pitch_max;
			long cam_pull_steps, cam_pull_push;
			long dead_l, dead_r;

			ModernCameraLogConfig(&cam_return, &cam_return_min, &cam_wall_factor,
				&cam_hide_dist, &cam_invert, &cam_pitch_return,
				&cam_speed, &cam_yaw_rate, &cam_pitch_min, &cam_pitch_max,
				&cam_pull_steps, &cam_pull_push);

			/* The effective width, not the constant: the dead zone slider
			 * scales both at the seam (specific\input.cpp), and the slider's
			 * own value is `dead_user` at the end of the line. Only the radial
			 * pair is printed; the square zone moves with the same per cent. */
			dead_l = ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_LEFT : MODERN_DEAD_LEFT_DINPUT,
				(long)tomb5.dead_zone);
			dead_r = ModernDeadZoneUser(joystick_is_xinput ? MODERN_DEAD_RIGHT : MODERN_DEAD_RIGHT_DINPUT,
				(long)tomb5.dead_zone);

			Log("MODERNLOG init scheme=modern rate=%d direct=%d walk_below=%d recenter=%d "
				"jump_dir=%d cam_return=%d cam_return_min=%d cam_wall_factor=%d "
				"cam_hide_dist=%d cam_invert=%d cam_pitch_return=%d "
				"cam_speed=%d cam_yaw_rate=%d cam_pitch_min=%d cam_pitch_max=%d "
				"aim_strafe=%d dead_l=%d dead_r=%d "
				"air_rate=%d swim_rate=%d swim_pitch_rate=%d "
				"cam_pull_steps=%d cam_pull_push=%d stand_ticks=%d "
				"dead_user=%d",	// appended at the end, so the fields before it keep their places
				modern_cfg.turn_rate, modern_cfg.direct_turn, modern_cfg.walk_below,
				modern_cfg.recenter, modern_cfg.jump_dir, cam_return, cam_return_min,
				cam_wall_factor, cam_hide_dist, cam_invert, cam_pitch_return,
				cam_speed, cam_yaw_rate, cam_pitch_min, cam_pitch_max,
				modern_cfg.aim_strafe, dead_l, dead_r,
				modern_cfg.air_turn_rate, modern_cfg.swim_turn_rate, modern_cfg.swim_pitch_rate,
				cam_pull_steps, cam_pull_push, modern_cfg.stand_forward_ticks,
				(long)tomb5.dead_zone);

			/* MODERNLOG pad, once beside the init line: the raw axes at rest,
			 * unrounded, in the same /256 or /8 units as dead_l/dead_r
			 * (specific\input.h), so the dead zones can be checked against
			 * where the sticks really rest. `xinput` says which units apply. A
			 * resting stick reads near 0 on a well centred pad; a value near
			 * MODERN_DEAD_RIGHT_DINPUT's 62 on the DirectInput axes is the
			 * off-centre rest that constant is sized for. */
			Log("MODERNLOG pad lx=%ld ly=%ld rx=%ld ry=%ld xinput=%ld",
				joystick_raw_lx, joystick_raw_ly, joystick_raw_rx, joystick_raw_ry,
				joystick_is_xinput);
		}

		modern_tick = GlobalCounter;
		modern_seen = 1;
		return bits;
	}

	modern_tick = GlobalCounter;

	// stick_x/stick_y are the left stick's own axes, or with that at rest the
	// four movement keys as a full-deflection stick (ModernKeyStick, `feed`);
	// the d-pad never reaches here (see the note above ModernModeName).

	/* The camera heading of this tick.
	 *
	 * From the camera's own two points, not from camera.actual_angle: that
	 * one is lara_item->pos.y_rot plus an offset, so taking it as the
	 * reference would feed her own heading back into her heading and make
	 * her run in circles.
	 *
	 * It is the simulation's camera and not the drawn one, for two reasons:
	 * game\control.cpp calls CalculateCamera after the input, so this reads
	 * the camera as the last tick left it, and game\interpolate.cpp puts the
	 * unblended pair back before the tick begins.
	 *
	 * A camera sitting exactly on its own target has no heading. That happens
	 * for a frame after a cut; falling back to Lara's own heading makes the
	 * scheme behave exactly like Tank for that one tick, which is the
	 * quietest thing it can do. */
	cam_dx = camera.target.x - camera.pos.x;
	cam_dz = camera.target.z - camera.pos.z;
	camera_yaw = (cam_dx || cam_dz) ? (short)phd_atan(cam_dz, cam_dx) : lara_item->pos.y_rot;

	/* Under the orbit the reference is the angle the player dialled, not where
	 * the eye has been pushed to.
	 *
	 * The measured vector above is right for a camera nobody steers and stays
	 * the fallback. It fails once the eye is close to her: at a steep pitch the
	 * horizontal radius is a few dozen units while the eye trails her run by
	 * hundreds, so "eye -> target" becomes her own running direction and she
	 * turns 3277 units every tick the stick is off centre; and a wall that
	 * pulls the eye in does the same at any pitch - 57 jumps of more than 45
	 * degrees in a single tick, her course after them reading 3154, 17126,
	 * 26721, -22504. The wall resolution in game\camera.cpp brings the eye
	 * close often, by design, so this reference is what makes it safe.
	 *
	 * The world yaw has neither failure: it is the angle the ideal is built
	 * from, exact at every pitch, and geometry cannot move it. It is one tick
	 * behind, as camera_yaw is (game\control.cpp runs the input before
	 * CalculateCamera).
	 *
	 * Without the orbit - a flyby, a cutscene, first person, the title -
	 * ModernCameraActive() is false and the measured vector is the reference;
	 * so is the one tick before the orbit's first, when it is not seated yet.
	 *
	 * The rich2 sniper camera falls back as well, but not through
	 * ModernCameraActive(), which reads the option, lara_item, the title, the
	 * spotcam, the cutscene and first person, not the running camera. The
	 * sniper is the one level camera Modern keeps, so FixedCamera runs, the
	 * orbit does not tick, and ModernCameraActive() stays true. game\camera.cpp's
	 * stamp answers it: ModernCameraWorldYaw declines unless the orbit ran on
	 * the tick before, so the measured vector - the camera the player sees - is
	 * the reference while the sniper has the view.
	 *
	 * The same accessor declines on a tick after Core's diagonal-wall push
	 * moved the orbit's eye (The 13th Floor and Escape with the Iris,
	 * game\tower.cpp). That push throws the eye across the dialled ray to the
	 * near side of a 45-degree wall panel (its collision box grown by 384,
	 * tested within 512) and pins it there, so the dialled angle no longer
	 * describes the picture: in a RICH2 MODERNLOG at room 8's south-west panel
	 * the world yaw lay more than 8192 units off the measured view on 322
	 * stick-pushed ticks, every one of them with the eye on that panel's push
	 * plane. The eye is pinned, not trailing her run, so the measured vector
	 * is the steady picture there and is the reference.
	 *
	 * MODERNLOG prints both: `cam=` is the measured vector and `ref=` what the
	 * translation used (out.reference). */
	ref_orbit = (ModernCameraActive() && ModernCameraWorldYaw(&ref_yaw)) ? 1 : 0;

	if (!ref_orbit)
		ref_yaw = camera_yaw;

	/* Whether forward is getting her anywhere.
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

	/* Wading is classic, and so is a dead Lara. LW_UNDERWATER and LW_SURFACE
	 * fall through to the state table, which names the eight states the water
	 * steers by the orbit's pitch and the four handovers it does not
	 * (game\modern_move.cpp).
	 *
	 * In LW_WADE LaraAboveWater runs and the states are the ordinary ground
	 * ones, so letting it through would change the feel of walking in
	 * shin-deep water. LW_FLYCHEAT needs no term: DOZY forces AS_SWIMCHEAT
	 * (118) and the state table already calls that classic.
	 *
	 * At zero hit points the automaton stops answering, and her last pose is
	 * the animation's to keep. */
	sit.classic = (lara.water_status == LW_WADE || lara_item->hit_points <= 0) ? 1 : 0;

	/* A weapon out and shootable. A torch is held in the same hand and is not
	 * a weapon - aiming with it would strafe her around nothing. */
	sit.drawn = (lara.gun_status == LG_READY && lara.gun_type != WEAPON_TORCH) ? 1 : 0;
	sit.action = action ? 1 : 0;
	sit.jump = jump ? 1 : 0;
	sit.blocked = (modern_cfg.block_frames > 0 &&
		modern_state.blocked_count >= modern_cfg.block_frames) ? 1 : 0;

	/* What the right stick did to the view last tick. Asked as `ref_yaw` above
	 * is, behind the same two gates - ModernCameraActive() for "may the orbit
	 * steer the view at all" and the orbit's tick stamp inside the accessor for
	 * "did it really run" - so the two answers cannot disagree about which
	 * camera this tick had. Where either says no the step is 0 and the held
	 * course is left alone: a flyby, a cutscene, first person, the title, the
	 * rich2 sniper camera, the tick before the orbit's first, a tick after the
	 * diagonal-wall push moved the eye. The accessor returns the stick's own
	 * term and not the orbit's whole step, which keeps the return and the
	 * carried swing out of her course. */
	sit.cam_player_step = 0;

	if (ModernCameraActive())
		ModernCameraPlayerStep(&sit.cam_player_step);

	/* Whether the reference changed cameras since the last tick, for the held
	 * course's cut latch (MODERN_CUT_LATCH). The signal is the switch between
	 * the two sources above, not camera.old_type: what the latch protects is
	 * the reference, and the source is what hands it over, whatever camera
	 * type took the view - a sniper camera fired as heavy (old_type 5) switches
	 * it the same way. In the rich2 MODERNLOG through the sniper camera the
	 * two agree: the 405 ticks with the stick at rest show the reference
	 * handed in, the orbit's on all 235 with old_type 0 and the measured
	 * vector on all 170 with old_type 1, and the 2626 ticks replay exactly
	 * with that rule - so the switch falls on the 38 changes of old_type.
	 * The diagonal-wall push switches the source the same way, on the tick
	 * after its first push and on the tick after its last, with old_type 0
	 * throughout, so a stick held through it keeps its course by the same
	 * latch. */
	sit.cam_cut = (modern_ref_seen && ref_orbit != modern_ref_orbit) ? 1 : 0;
	modern_ref_orbit = ref_orbit;
	modern_ref_seen = 1;

	/* Whether this tick is the water's, and at what rate. Asked once, here,
	 * from the state alone, and answered by the same table the
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

	/* The direct turn. The decision is arithmetic and lives in
	 * game\modern_move.cpp; this is the one line of engine it needs.
	 * lara.turn_rate goes to zero because game\lara.cpp adds it to y_rot a
	 * moment later in LaraAboveWater - without that, she would turn twice.
	 *
	 * S_UpdateInput runs before AnimateLara and before the collision, so a
	 * heading written here is the heading she animates and collides with this
	 * tick. game\interpolate.cpp snapshots her position before the input runs
	 * and blends y_rot with LerpAngle, so the picture glides at 60 or 165 Hz
	 * without anything added there.
	 *
	 * In the water it is the water's own move instead, an either/or: the water
	 * turns at the rate of the state it is in, takes no instant about-turn,
	 * sends no IN_FORWARD under water - down there that bit is SwimTurn's
	 * pitch - and answers two things the land has no notion of, the attitude
	 * and the stroke. `water` is the state's rate and the question "is this
	 * the water?" in one, so the branch cannot drift from the table.
	 *
	 * The pointer is only a pointer while the orbit steers the view. Where
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

			/* The bank the turn bits would have made. SwimTurn rolls her by
			 * this much on every tick a turn bit is held (game\laraswim.cpp),
			 * and the translation has taken those bits away - so the roll is
			 * owed here, or the modern swimmer would corner flat where the
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
	/* In the air the same line again, at the jump's own rate: she can still
	 * steer left and right after take-off, a free choice of this project, and
	 * so jump a much wider arc round an edge.
	 *
	 * A third arm of the same either/or, possible because the three lists do
	 * not overlap: ModernWaterTurnRate answers only the eight swim and
	 * surface states, ModernStateAllowsDirectTurn the ten on the ground, and
	 * ModernStateAllowsAirTurn the one in the air (3, AS_FORWARDJUMP - the only
	 * airborne routine in game\lara.cpp that reads a turn bit at all).
	 * ModernTest asserts the three-way exclusivity over all 139 states, so the
	 * order of the arms cannot come to matter later.
	 *
	 * turn_rate goes to zero for the same reason as on the ground:
	 * lara_as_forwardjump ramps it from IN_LEFT / IN_RIGHT and LaraAboveWater
	 * adds it to y_rot a moment later, so without this she would turn twice -
	 * once at the modern rate and once at the classic's 182 units. ModernAirTurn has
	 * already taken those two bits out of out.bits, which is what the write
	 * below hands on.
	 *
	 * It comes before the publish line on purpose. game\camera.cpp subtracts
	 * the heading published below from her heading after the animation, and
	 * treats what is left as an animation about-turn worth a camera swing. A
	 * turn written after the publish would be read as exactly that. */
	else if (ModernAirTurn(&modern_cfg, &modern_state, &sit, &out, &air))
	{
		lara_item->pos.y_rot = ModernWrap((long)lara_item->pos.y_rot + air);
		lara.turn_rate = 0;
	}

	/* The heading the stick is accountable for is published here. It is read
	 * back off the item rather than composed out of lara_yaw and `yaw`, so
	 * that any branch above that turns her another way - the water's own move
	 * - cannot drift from it: whatever this file did to her heading this tick,
	 * this is where it ended. Everything that happens to it after this line
	 * happens in AnimateLara and the collision, which is what game\camera.cpp
	 * measures. */
	modern_turned_yaw = lara_item->pos.y_rot;
	modern_turned_at = GlobalCounter;
	modern_turned_seen = 1;

	/* The only write to the input word. When the translation says it is not
	 * responsible - a classic state, the stick at rest - the bits the stick
	 * and the keyboard wrote simply stand, which is why the pad still moves
	 * her on a ladder and up a rope without a line of code for it. */
	if (out.active)
		bits = (bits & ~(long)MODERN_MOVE_MASK) | out.bits;
	else
		modern_state.blocked_count = 0;

	/* The stroke is added and never taken away. It is IN_JUMP, which is not in
	 * MODERN_MOVE_MASK and so is not cleared by the line above - deliberately:
	 * the player's own jump key keeps working as the stroke, with
	 * water_auto_stroke on or off. This only ever turns the bit on, on the
	 * ticks the stick is out under water. */
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

	/* MODERNLOG: one line per real tick, whether or not the translation was
	 * responsible for it - `mode=off` is itself the answer to "why did nothing
	 * happen here". It sits after every early return above and hangs on the
	 * logic tick, not on a drawn frame: 30 lines a second at every fps_mode.
	 *
	 * Never more, but it can be fewer. The reset branch above returns before
	 * this line, so the first tick back from a level start, a savegame, first
	 * person, a cutscene or the look key shows its `init` line and no tick
	 * line; the tick after the inventory closes is an ordinary tick line,
	 * because nothing resets for it. While the look key is down, the ring is
	 * open or a cutscene plays, the seam is not called and neither line
	 * appears. A gap in `f=` (GlobalCounter) is the scheme standing aside,
	 * not a dropped line.
	 *
	 * `src`: "pad" is the stick driving `out`; "keys" is `out` stepping aside
	 * and the base assembly's bits - the classic stick scaling, the keyboard,
	 * or Steam Input's synthetic arrow keys - standing untouched. The stick
	 * that drives `out` can be the four movement keys as well, so `src`
	 * answers whether the translation wrote the word and `feed`, at the end
	 * of the line, what the stick it was handed came from: "pad" the left
	 * stick, "keys" the four movement keys with the stick at rest, "none"
	 * both at rest.
	 *
	 * `bits` is `out.bits`, the translation's own contribution (0 whenever
	 * `mode=off`), not the merged word this function returns; a classic tick
	 * or a dead stick can still move her through the untouched base bits.
	 * `lara` is the heading captured before the direct turn wrote a new one.
	 *
	 * `cstep` is how far the course itself moved this tick: a smooth circle
	 * holds it near the stick's own step a tick, and only a flick of the
	 * thumb shows a figure over 16384.
	 *
	 * `cam_world` is the orbit's remembered world angle, 0 before its first
	 * seating. `clip` is the flag the wall resolution writes and the return's
	 * wall_factor reads: 1 on a tick the eye was clipped, the tick the return
	 * runs eight times as fast.
	 *
	 * `view_err` is abs(camera_yaw - cam_world), wrapped the short way like
	 * `err`, in raw units (0 == on the ray, 32768 == pointing the opposite
	 * way): it shows every tick that the eye stays on the dialled angle.
	 * `air` is the air turn's step (0 on every tick but those in
	 * AS_FORWARDJUMP with the stick off centre). `carry` is the camera swing
	 * still pending, MODERN_CAMERA_STATE::carry.
	 *
	 * `cam_step` is the step the right stick gave the orbit on the previous
	 * tick, exactly what was added to her held course above; held against
	 * `cstep` it shows the course following the view tick for tick. 0 on every
	 * tick the orbit did not tick, and on every tick the camera moved by
	 * itself.
	 *
	 * `cam_speed` and `old_type` are camera.speed and camera.old_type as the
	 * previous tick's CalculateCamera left them. old_type is the type that
	 * view was built as (0 CHASE, 1 FIXED, 2 LOOK, 3 COMBAT, 5 HEAVY): 0 on a
	 * Camera tile whose level camera the orbit discards, 1 (5 if heavy) where
	 * a level camera takes the view. cam_speed is 10 after every
	 * CalculateCamera pass, a heavy camera the orbit discards included; a
	 * heavy camera the orbit does not discard (in first person, or the sniper
	 * camera fired as heavy) keeps the trigger's speed while its timer runs,
	 * and a flyby writes its own 1 (game\spotcam.cpp).
	 *
	 * `cut` is sit.cam_cut, 1 on the tick the reference changed cameras (the
	 * orbit's world yaw against the measured vector, so one per change of
	 * old_type between 0 and 1, and one where the diagonal-wall push begins or
	 * ends). `latch` is modern_state.cut_latch after the translation: 1 while
	 * a course seated before a cut is held, until the stick is let go or
	 * re-aimed (MODERN_CUT_LATCH).
	 *
	 * `ref_view` is 1 when the modern camera may steer the view and the
	 * reference is nevertheless the measured vector: under the rich2 sniper
	 * camera, under a flyby that tracks her and leaves her the controls (the
	 * X-ray corridor of rich2), whose ticks run no orbit, on the tick before
	 * the orbit's first, and on a tick after the diagonal-wall push moved the
	 * eye (CAMLOG's `diag=` above 0 on the tick before). 0 when the reference
	 * is the orbit's world yaw, and whenever the modern camera is off.
	 *
	 * `latch_drop` is out.latch_drop: 1 on the tick the stick, turned more than
	 * MODERN_CUT_REAIM from where it pointed when the latch was armed, ended
	 * the hold, so `latch` reads 0 and `ref` is this tick's camera; 0 on every
	 * other tick.
	 *
	 * New fields are appended at the end, so the older ones keep their
	 * places. */
	if (gLogEnabled)
	{
		/* The orbit's eight, read back from game\camera.cpp, which owns the
		 * one instance. cam_off/cam_pitch/cam_ret/cam_world/clip/carry and
		 * `hide` are one tick behind, as camera_yaw is, because this line runs
		 * before CalculateCamera; rs_x/rs_y are this tick's own (see
		 * game\camera.cpp's comment on ModernCameraLogState). `hide` is the raw
		 * eye distance, camera.pos against camera.target, not a flag, so it can
		 * be watched closing on MODERN_CAMERA_HIDE_DIST. */
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
			"view_err=%ld air=%ld carry=%ld stand_hold=%ld cam_step=%d "
			"cam_speed=%d old_type=%d cut=%ld latch=%ld feed=%s ref_view=%ld latch_drop=%ld",
			(long)GlobalCounter, out.active ? "pad" : "keys", stick_x, stick_y, out.mag,
			camera_yaw, out.reference, modern_state.course, lara_yaw,
			out.delta, sit.anim_state, ModernModeName(out.mode), modern_state.spin,
			modern_state.aligned, modern_state.blocked_count, out.blocked,
			yaw, out.bits, cam_off, cam_pitch, cam_ret, cam_rs_x, cam_rs_y,
			ModernLaraEyeDistance(), water, xrot, stroke, out.course_step,
			cam_world, clip, view_err, air, carry, modern_state.stand_hold,
			sit.cam_player_step, (int)camera.speed, (int)camera.old_type,
			sit.cam_cut, modern_state.cut_latch, ModernFeedName(feed),
			(ModernCameraActive() && !ref_orbit) ? 1L : 0L, out.latch_drop);
	}

	return bits;
}

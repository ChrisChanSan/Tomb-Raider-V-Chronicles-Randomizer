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

/* The modern camera: an orbit around Lara, in pure arithmetic.
 *
 * The orbit is the engine's own chase camera with its angle taken from a
 * state instead of from Lara alone:
 *
 *     yaw       = world_yaw - a direction in the WORLD, which her turning does
 *                 not move; the engine's slot gets world_yaw - lara.y_rot
 *     elevation = offset_pitch
 *     distance  = camera.target_distance, unchanged
 *
 * so every wall, room change, damping step and snap of game\camera.cpp keeps
 * working. This file owns the two angles the orbit remembers between ticks and
 * the three rules that move them; game\camera.cpp reads them.
 *
 * The rules: the right stick moves the camera around her, 360 degrees on the
 * yaw; a stick at rest keeps the camera where it is in the world, also while
 * she turns; while she moves, the camera falls back behind her so slowly that
 * it is hard to notice, and faster while a wall clips the eye. An eye that
 * comes too close hides her completely. This is the behaviour of the
 * Remaster's camera as seen in gameplay recordings.
 *
 * The state is a world yaw and not an offset from her heading, because an
 * offset turns the view with every degree she turns: a turn of 62 degrees was
 * followed by the eye within 20 ticks and overshot to 80. The offset the
 * engine wants is derived from the world yaw on every tick, and the return is
 * the only thing that brings the camera home.
 *
 * TROne (GPLv3) holds its orbit as a world angle too, and that is the only
 * thing in common: TROne's orbit never returns behind the player, this one
 * falls home at MODERN_CAMERA_RETURN whenever she moves and the stick is at
 * rest. No code of TROne is copied here; the shared idea is that a camera
 * angle is a state and not a derivation.
 *
 * No engine header is included, like game\modern_move.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp compiles this file itself and
 * asserts on its numbers.
 *
 * Angles are the engine's 16-bit units (65536 is a full turn, 0 is +Z), the
 * stick is +-128 (specific\dxshell.cpp scales XInput by 256), and a tick is
 * one logic step, 1/30 s (game\control.cpp counts the loop down in halves of
 * a 60th). How a wall is resolved - only the wanted angle, clipped by mgLOS
 * at any distance, so the distance shrinks instead of the angle jumping -
 * lives in game\camera.cpp. */

/* The classic camera's own numbers, reused so that the orbit asks for no limit
 * the classic camera would take back from it:
 *
 *   elevation clamp        +-15470 (+-84.98 deg)  ChaseCamera
 *   resting elevation      -1820 (-10 deg, above) ChaseCamera
 *   elevation damping      (target - actual) >> 3 UpdateCameraElevation
 *   distance               1536                   game\camera.cpp
 *   wall resolution        the wanted angle, else the nearest of the four
 *                          world cardinals with line of sight; mgLOS push 200,
 *                          CameraCollisionBounds push 384
 *                                                 ChaseCamera
 *   eye damping            pos += (ideal - pos) / speed      MoveCamera
 *   look camera pitch      364 per tick (2 deg), head -6370..+5460
 *                                                 LookUpDown, lara.cpp
 *   near clip plane        20 world units         InitWindow(...,20,20480,...),
 *                                                 specific\gamemain.cpp
 *   Lara's own radius      100..102               lara.cpp
 *   camera cut thresholds  Manhattan > 1024 on the eye and on the view
 *                          direction              INTERP_CAM_SNAP,
 *                                                 game\interpolate_math.h
 *
 * The last row constrains this file: the 60 fps cut detection must not fire on
 * the orbit's own steps. At the default speed the yaw moves 1092 units, 6
 * degrees, in one tick; the ideal eye sits at 1536 * cos(10 deg) = 1512 units,
 * so it travels 2 * 1512 * sin(3 deg) = 158 units, at worst 224 Manhattan. The
 * pitch moves 3 degrees, which lifts the ideal by at most 1536 * sin(3 deg) =
 * 80 units, and the >> 3 damping turns that into 10. Together 304 against a
 * threshold of 1024. At speed 100 the yaw moves 2184 units (12 degrees) and
 * the pitch 6 degrees a tick, so Preparation\Tools\Diagnose\ModernTest.cpp
 * sweeps the whole sphere at both ends of the slider (camera_camcut) against
 * INTERP_CAM_SNAP itself; its swept test measures 309 Manhattan at the
 * default speed and 616 at speed 100, both under 1024.
 *
 * MoveCamera's damping adds no margin: "pos += (ideal - pos) / speed" is a
 * first-order lag, and a lag fed a constant velocity settles at that same
 * velocity for as long as the push lasts.
 *
 * The one thing that can trip the detection at a wall is the classic quadrant
 * snap, which jumps the ideal by 90 degrees; game\camera.cpp does not produce
 * it in Modern, and the orbit never adds to it. */

/* Every constant of the orbit, with its origin. "The classic camera's own" is
 * a number read out of game\camera.cpp, so that the orbit cannot disagree with
 * the camera it steers. A "feel" number is tuned by eye and has a MODERNLOG
 * field to read it back. ModernCameraDefaults() is the only reader. */
enum modern_camera_constants
{
	/* No dead zone of its own, like the left stick's MODERN_DEAD: the zone is
	 * applied before the axes arrive. specific\input.cpp puts the raw right
	 * stick through ModernStickDeadZone (game\modern_move.h) at
	 * MODERN_DEAD_RIGHT, or at MODERN_DEAD_RIGHT_DINPUT on the DirectInput
	 * path, where a stick can rest off centre. A second zone on top of that
	 * would only eat travel. If the camera drifts on its own (MODERNLOG rs=),
	 * the number to raise is the radial zone in game\modern_move.h. */
	MODERN_CAMERA_DEAD = 0,

	/* The speed slider's two ends, its top and its default. The rate is a row
	 * on the Controls page, 0 to 100, like SFX and Music.
	 *
	 * 60 degrees a second at the bottom leaves room for a slower camera; 360 a
	 * second at the top, a full turn in one second, lies past the Remaster's
	 * own rate (a full circle in 2.85 to 2.95 s, about 125 deg/s), so the
	 * slider has room above that reference. At 30 ticks a second, 60 deg/s is
	 * 364.09 units a tick and 360 deg/s is 2184.53, so 364 and 2184.
	 *
	 * The mapping is linear, which puts the default of 40 at exactly
	 * 364 + 40 * 1820 / 100 = 1092. The pitch is half the yaw at every
	 * setting - half a turn of elevation while the yaw goes round once, which
	 * is the whole travel the clamp below allows and a little over.
	 *
	 * Between rest and full deflection the stick is proportional whatever the
	 * slider says: the axis is the rate, and the arithmetic stays one multiply
	 * the test can check. ModernCameraSpeedRates() is the only reader, and it
	 * is checked at 0, 40 and 100. */
	MODERN_CAMERA_YAW_RATE_MIN = 364,	// speed 0:   60 deg/s (364.09 at 30 Hz)
	MODERN_CAMERA_YAW_RATE_MAX = 2184,	// speed 100: 360 deg/s (2184.53)
	MODERN_CAMERA_SPEED_FULL = 100,		// the slider's own top, like SFX and Music
	MODERN_CAMERA_SPEED_DEFAULT = 40,	// = 1092 units a tick, 180 deg/s

	/* Below her, the classic camera's own limit: ChaseCamera clamps
	 * camera.actual_elevation to +-15470 on every tick, 84.98 degrees, so the
	 * orbit asks for nothing the camera would take back from it.
	 *
	 * Above her the cap is cut at -10923, 60 degrees, where the ideal eye's
	 * horizontal radius is still 1536 * cos(60) = 768, half a sector. Over
	 * that cap the radius collapses, the eye ends up nearly on her own axis,
	 * and everything measured from the vector eye -> target loses its
	 * direction - her movement reference above all, which then read her own
	 * running direction and turned her 3277 units every tick while the stick
	 * was off centre. game\modern_state.cpp takes that reference from the
	 * orbit instead; this cap is the other half of the cure. 768 is the same
	 * half sector as game\camera.cpp's MODERN_CAM_ANGLE_RADIUS.
	 *
	 * The cost: the water's pointer is this elevation minus pitch_home
	 * (ModernCameraSwimPitch), so the steepest dive a modern swimmer can ask
	 * for falls from 13650 (75 degrees) to 9103 (50). game\modern_move.h
	 * carries that arithmetic; a shallow dive is answered there. */
	MODERN_CAMERA_PITCH_MIN = -10923,	// 60 deg above her: radius 1536*cos(60) = 768
	MODERN_CAMERA_PITCH_MAX = 15470,	// below her: ChaseCamera's own clamp

	/* The classic camera's own resting elevation. ChaseCamera opens with
	 * "if (!camera.target_elevation) camera.target_elevation = -1820", ten
	 * degrees above her and looking down. The orbit's pitch is that elevation,
	 * not an offset from it, so a reset seats it here. Zero would be dead
	 * level, ten degrees below the game's own camera, and is the value that
	 * line treats as "unset". */
	MODERN_CAMERA_PITCH_HOME = -1820,

	/* Feel number: the offset falls towards zero by this fraction of itself
	 * per tick, out of MODERN_CAMERA_RETURN_ONE. This is the engine's idiom
	 * (UpdateCameraElevation uses a >> 3) written as a fraction, because the
	 * wanted rate lies between two shifts: a 90 degree offset halves in about
	 * four seconds of running, 120 ticks. 1 - 2^(-1/120) = 0.0057595, times
	 * 65536 is 377.46, so 377 - a half life of 120.1 ticks, 4.005 s. A shift of
	 * 7 would give 2.95 s and a shift of 8, 5.9 s. MODERNLOG prints cam_off and
	 * cam_ret every tick; the number is tuned in halves and doubles (188 = 8 s,
	 * 754 = 2 s). */
	MODERN_CAMERA_RETURN = 377,
	MODERN_CAMERA_RETURN_ONE = 65536,

	/* A fraction of a number can be zero, and a decay that can step by zero
	 * never arrives: under 174 units of offset, 377/65536 of it floors to
	 * nothing and the camera would sit one degree off her back for ever.
	 * Four units a tick is 0.02 degrees, 0.66 degrees a second - well under
	 * anything an eye can follow - and it closes that last degree in 44
	 * ticks. It is the floor of the step, not a step of its own: above 174
	 * the fraction is what moves the camera. */
	MODERN_CAMERA_RETURN_MIN = 4,

	/* Feel number: while a wall clips the eye the return runs this many times
	 * faster. A factor and not a second rate, so "faster than the return"
	 * holds by construction when the return above is tuned, and the test can
	 * prove the relation. Eight times 377 is a half life of 14.7 ticks, half a
	 * second: fast enough to read as the wall taking the camera, slow enough
	 * to stay a movement and not a cut. A wall means the eye was clipped this
	 * tick: the camera keeps the angle the player dialled and loses distance
	 * instead. */
	MODERN_CAMERA_WALL_FACTOR = 8,

	/* Feel number: under this eye-to-centre distance she is hidden completely,
	 * at the distance where the classic camera's near clip starts to cut her.
	 * The near plane is 20 world units (InitWindow(0, 0, w, h, 20, 20480, 80,
	 * ...) in specific\gamemain.cpp and winmain.cpp), and her body is a
	 * cylinder of radius 102 (coll->radius in game\lara.cpp, the standing and
	 * running value); at 20 + 102 = 122 the near plane stops being in front of
	 * her and starts being inside her. MODERNLOG prints hide=, the measured
	 * distance. If she shows sliced open before she vanishes, this rises
	 * towards 384 - the camera collision's own push - in steps of 64. */
	MODERN_CAMERA_HIDE_DIST = 122,

	/* The classic camera's own direction: the look camera turns IN_FORWARD
	 * into "look up" (LookUpDown in game\lara.cpp - head_x_rot -= 364, and a
	 * negative x_rot is up). Looking up from an orbit means the camera drops
	 * below her, a positive elevation here, and the stick reads
	 * forward-negative - so the default subtracts the y axis. 1 selects the
	 * inverted convention. */
	MODERN_CAMERA_INVERT_PITCH = 0,

	/* pitch_return: the pitch falls back to the resting elevation the way the
	 * yaw falls back behind her. Off by default: only the yaw returns, and the
	 * height stays where the player put it. A switch turns off exactly one
	 * rule, which is how a complaint is narrowed down; the capability is
	 * testable with an explicit cfg.
	 *
	 * The displacement a wall forces on the camera is deliberately NOT taken
	 * into the offset. ChaseCamera answers a blocked angle with one of the
	 * four world cardinals (the quadrant snap); written into the offset, the
	 * next tick asks for that cardinal plus one stick step, is blocked again,
	 * and the stick is swallowed - in 290 of 855 ticks with the right stick
	 * fully pushed the angle did not advance or ran against the stick, 177 of
	 * them at -400 units or worse, in 14 episodes of 8 to 37 ticks.
	 * game\camera.cpp instead keeps only the wanted angle and clips it with
	 * mgLOS at any distance. */
	MODERN_CAMERA_PITCH_RETURN = 0,

	/* An animation about-turn takes the camera with it. The world yaw does not
	 * move when she turns, which is right while the stick steers her; an
	 * animation that flips her 180 degrees in one tick is not steering. The
	 * ground roll (state 45 -> 23), the jump twist (3 -> 3) and the underwater
	 * roll (66 -> 66) otherwise leave the camera in front of her face (cam_off
	 * about 180), the relative angle is inverted for her next jump, and the
	 * slow return takes back under 30 degrees in 40 ticks.
	 *
	 * The threshold is 150 degrees in one tick. The three flips are all 180;
	 * the modern turn's biggest step is 30 (MODERN_TURN_RATE) and the classic
	 * automaton's is 8, so anything over 150 in a single tick is an animation.
	 * 65536 * 150 / 360 = 27306.67, so 27307.
	 *
	 * The swing is paid out over 20 ticks, 0.67 s, the middle of the 0.6 to
	 * 0.75 s the Remaster's camera takes to sit behind her again. Feel number:
	 * MODERNLOG's `carry=` prints what is still pending every tick.
	 *
	 * The profile is eased, 3u^2 - 2u^3: a linear swing starts and stops at
	 * full speed and reads as two cuts with a pan between them. The eased
	 * profile's steepest tick is 1.5 times the average - 2456 units at the
	 * half turn, about 360 units of eye movement at the orbit's radius, a
	 * third of INTERP_CAM_SNAP, so the interpolator never reads the swing as a
	 * cut. MODERN_CAMERA_CARRY_ONE is the profile's fixed point, in 4096ths:
	 * small enough that total * profile stays inside a long, exact at both
	 * ends. */
	MODERN_CAMERA_CARRY_FLIP = 27307,	// 150 deg in one tick: an animation, never a turn
	MODERN_CAMERA_CARRY_TICKS = 20,		// 0.67 s at 30 Hz - the Remaster's own 0.6 to 0.75
	MODERN_CAMERA_CARRY_ONE = 4096,		// the eased profile's fixed point

	/* How many fractions of the ray the eye is pulled in along, when geometry
	 * clips it.
	 *
	 * Eight, because mgLOS walks the same ray in eighths (game\camera.cpp,
	 * `(target->x - start->x) >> 3` and a loop of 8), so the clipped point it
	 * returns sits on an eighth and the pull-in that follows measures in the
	 * same units instead of a second grid. At the orbit's radius of 1536 that
	 * is 192 units a step.
	 *
	 * The arithmetic is here and the geometry is not: ModernCameraPullAxis
	 * below is the whole rule - one coordinate, one fraction - and
	 * game\camera.cpp's ModernCameraRayBounds asks the floor and the ceiling
	 * about the point. The split lets Preparation\Tools\Diagnose\ModernTest
	 * check that the same fraction is applied to x and to z, so the point
	 * stays on the ray and the picture keeps the angle that movement is
	 * measured against. */
	MODERN_CAMERA_PULL_STEPS = 8,

	/* The clearance that point is asked for: the classic camera collision's
	 * own push. CameraCollisionBounds is called with 384 from both orbit
	 * cameras in game\camera.cpp, and 384 is the engine's closest distance of
	 * the eye to a wall. Only the clearance test is used, not that function's
	 * sector-grid snap. */
	MODERN_CAMERA_PULL_PUSH = 384,

	/* How far the eye may be from her and still be orbiting her: the outer
	 * bound of game\camera.cpp's ModernCameraLeadEye. Eight sectors is five
	 * times the orbit's radius of 1536; past that the eye is arriving from
	 * somewhere - a fixed camera just released, a cut, a room change - and
	 * turning it about her by a stick step would turn the wrong thing. It also
	 * keeps the squares in that function inside a long.
	 *
	 * It is not the bound the cut detection needs: a rotation moves the eye by
	 * a chord that grows with the radius, so that bound is
	 * camera.target_distance and lives in the function.
	 *
	 * It lives in this header because Preparation\Tools\Diagnose\ModernTest
	 * models ModernCameraLeadEye by hand (camera.cpp cannot be linked without
	 * a level) and must read the tree's own number rather than a copy. */
	MODERN_CAMERA_LEAD_MAX = 8192,

	/* How the drawn eye searches the chord between two tick eyes
	 * (ModernCameraChordClamp below): the chord is scanned outward from the
	 * blended point in steps of a thirty-second of its length on both sides,
	 * and each bracket the scan finds is then halved twelve times.
	 *
	 * The longest chord measured beside a pillar is 652 units in one tick
	 * (ANDREA1 room 3, the tick drawn at log t 140625), so a step is 20
	 * units there: an open stretch narrower than that is stepped over, and
	 * the halving puts the answer within 0.005 units of the edge the scan
	 * bracketed. At most 1 + 2 * 32 + 2 * 12 tests, and only on a frame whose
	 * eye fails the room tests. */
	MODERN_CAMERA_CHORD_SCAN = 32,
	MODERN_CAMERA_CHORD_BISECT = 12
};

/* The orbit's thresholds, in one place, filled by ModernCameraDefaults().
 * Every field is a long so that nothing in the update has to convert: the
 * angles are signed 16-bit values widened, the rates are fractions of
 * MODERN_CAMERA_RETURN_ONE and the flags are 0 or 1. */
struct MODERN_CAMERA_CFG
{
	long dead;			// right stick deflection under which nothing happens
	long stick_full;	// full deflection in stick units (MODERN_STICK_FULL)
	long yaw_rate;		// units of yaw per tick at full deflection
	long pitch_rate;	// units of pitch per tick at full deflection
	long pitch_min;		// the elevation clamp, the classic camera's own
	long pitch_max;
	long pitch_home;	// the elevation a reset seats her camera at
	long invert_pitch;	// 0 = stick forward looks up, as the look camera does
	long return_rate;	// per MODERN_CAMERA_RETURN_ONE of what is left, per tick
	long return_min;	// the smallest step, so the return really arrives
	long pitch_return;	// 1 = the pitch falls back to pitch_home as well
	long wall_factor;	// how much faster the return runs while a wall clips the eye
	long hide_dist;		// eye-to-Lara distance under which she is not drawn
	long carry_flip;	// heading change in one tick that counts as an animation about-turn
	long carry_ticks;	// how many ticks the swing that follows it is paid out over; 0 = off
};

/* Where the camera stands and what the last tick did to it. game\camera.cpp
 * owns the one instance and forgets it at a level start, a savegame load and a
 * scheme change, because a camera angle seated in the last level is no angle
 * at all.
 *
 * The state is the world yaw and the offset is derived, because that cannot
 * drift: subtracting a per-tick heading delta accumulates every rounding and
 * loses any tick the orbit does not run (a flyby, a cutscene, the look key,
 * the sniper cameras), whereas a world yaw minus this tick's heading is exact
 * on every tick. It also prints directly as MODERNLOG's cam_world.
 *
 * Seating: a reset leaves world_yaw and `seated` at 0, because a pure
 * function has no Lara to ask; the first update ties the world yaw to the
 * heading it is handed, keeping whatever offset_yaw holds at that moment (0
 * after a reset - behind her back). A caller or a test can therefore seat an
 * offset before the first tick and get a world angle out of it. */
struct MODERN_CAMERA_STATE
{
	short world_yaw;	// where the eye stands IN THE WORLD - the state proper
	short offset_yaw;	// derived: world_yaw - her heading, what the engine's slot wants
	short offset_pitch;	// and how high: the camera elevation itself, clamped
	long seated;		// 0 = world_yaw is not tied to a heading yet
	long wall;			// 1 = the eye was CLIPPED this tick, so the return ran fast
	long ret;			// the return step this tick, signed - MODERNLOG cam_ret
	long steering;		// 1 = the stick was out, so the return stood still

	/* The swing an animation about-turn owes. `carry` is what is still
	 * pending, signed, and is MODERNLOG's `carry=`; the other two tell the
	 * eased profile where it is. All three are cleared together, by the last
	 * tick of the swing or by the stick cancelling it. */
	long carry;			// units of world yaw still to be paid out
	long carry_total;	// the whole swing, so the profile is measured from its start
	long carry_tick;	// how many of carry_ticks have been paid

	/* The yaw step the right stick alone gave this tick, taken before the
	 * swing above pays anything out and before the return falls home. The
	 * translation adds it to her held course, so that a camera the player
	 * turns takes her course with it while a camera that moves by itself does
	 * not. MODERNLOG's `cam_step=`. */
	short stick_step;	// the right stick's own yaw step this tick, signed
};

void ModernCameraDefaults(MODERN_CAMERA_CFG* cfg);
void ModernCameraCarry(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state, long flip);
long ModernCameraCarryStep(const MODERN_CAMERA_CFG* cfg, long total, long tick);
long ModernCameraPullAxis(long target, long eye, long step, long steps);
void ModernCameraSpeedRates(long speed, long* yaw_rate, long* pitch_rate);
void ModernCameraReset(MODERN_CAMERA_STATE* state);
void ModernCameraUpdate(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state,
	long rs_x, long rs_y, long lara_heading, long lara_moving, long wall_clipped,
	short* out_yaw_offset, short* out_pitch);
long ModernCameraHidesLara(const MODERN_CAMERA_CFG* cfg, long distance);
short ModernCameraSwimPitch(const MODERN_CAMERA_CFG* cfg, long pitch);
long ModernCameraTickFollows(unsigned short now, unsigned short stamp);

/* The drawn eye between two ticks.
 *
 * At fps_mode 1 and 2 the interpolator (game\interpolate.cpp) draws the eye
 * on the straight line between the last two tick eyes. Each tick eye stands
 * in open space, because MoveCamera tests it, but the line between them does
 * not have to: beside a pillar or a door jamb it cuts the corner, the drawn
 * eye stands inside the block for two or three frames, the portal walk
 * starts in a room that does not hold it and the frame is the clear colour.
 * And the room the walk starts in is the new tick's, while the drawn eye may
 * still stand beside the old tick eye in the old tick's room. Measured over
 * one ANDREA1 run: eight such black flashes in 35 s of video at 165 Hz, all
 * at room 40's east door and the pillars of rooms 3 and 11.
 *
 * ModernCameraDrawnEye decides what the frame draws, in this order:
 *
 *   NONE      the blended eye opens from the tick's room; nothing changes
 *   PREVROOM  it opens from the previous tick's room: the eye stays, the
 *             walk starts in the room that holds it
 *   CHORD     the nearest open point of the chord is drawn instead (the
 *             nearest in the blend factor, which is the nearest in distance)
 *   TICK      as CHORD, and that point is one of the two tick eyes itself
 *   NOSKY     nothing on the chord opens: the eye stays and the caller
 *             withholds the sky, so the hole is the clear colour, black
 *
 * A point of the chord opens when it opens from either tick's room. Nothing
 * here moves a tick eye; the answer is the picture of one frame. */
enum
{
	MODERN_EYE_FIX_NONE,
	MODERN_EYE_FIX_PREVROOM,
	MODERN_EYE_FIX_CHORD,
	MODERN_EYE_FIX_TICK,
	MODERN_EYE_FIX_NOSKY
};

struct MODERN_EYE_FIX
{
	long fix;		// one of MODERN_EYE_FIX_*
	float alpha;	// the blend factor of the eye drawn; the frame's own unless CHORD or TICK
	long x, y, z;	// the eye drawn
	short room;		// the room the portal walk starts in
	long tests;		// how often the open test ran
};

/* Whether a point stands in open space as seen from `room`: GetFloor walked
 * from that room reaches a sector with a floor and a ceiling, and the point
 * lies between the two. *at is the room the walk ended in, whether the point
 * opens there or not. The engine's test
 * is CameraEyeTest (game\camera.cpp); ModernTest hands in a map of its own. */
typedef long (*MODERN_EYE_OPEN)(void* ctx, long x, long y, long z, short room, short* at);

/* The same question without a room, for the chord search. */
typedef long (*MODERN_CHORD_OPEN)(void* ctx, long x, long y, long z);

/* The nearest point of the chord from -> to that opens, measured from the
 * point at `alpha`: 1 with its blend factor in *out_alpha and the point in
 * out, or 0 when no point of the chord opens. A point is built with
 * InterpLerpLong, so it is exactly the point the interpolator draws at that
 * factor. *tests counts the calls to `open`. */
long ModernCameraChordClamp(const long from[3], const long to[3], float alpha,
	MODERN_CHORD_OPEN open, void* ctx, float* out_alpha, long out[3], long* tests);

/* The rule above for one frame: prev and cur are the two tick eyes with their
 * rooms, alpha the frame's blend factor. prev_room may be -1. */
void ModernCameraDrawnEye(const long prev[3], short prev_room, const long cur[3], short cur_room,
	float alpha, MODERN_EYE_OPEN open, void* ctx, MODERN_EYE_FIX* out);

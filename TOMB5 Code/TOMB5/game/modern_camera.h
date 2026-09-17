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

/* THE MODERN CAMERA - THE ORBIT, AND NOTHING BUT ARITHMETIC (#161, round
 * 161a2, step 1).
 *
 * The modern camera is not a new camera. It is our own chase camera with its
 * angle taken from a state instead of from Lara alone:
 *
 *     yaw       = lara_item->pos.y_rot + offset_yaw
 *     elevation = offset_pitch
 *     distance  = camera.target_distance, exactly as today
 *
 * so every wall, every room change, every damping step and every snap of
 * game\camera.cpp stays what it was. What this file owns is the two angles
 * the orbit remembers between ticks and the three rules that move them; the
 * hunks that read them are game\camera.cpp, and they are step 2.
 *
 * THE MODEL IS THE USER'S, from his own recordings of 2026-09-17, and it is
 * quoted here in his words because every number below is answerable to one of
 * these four sentences:
 *
 *   1 "Es ist, als waere die Kamera eine Kugel um Lara herum, auf der ich sie
 *      mit dem rechten Stick vollstaendig um Lara herum bewegen kann, auf
 *      allen Achsen 360 Grad. Lara ist immer im Zentrum der Kugel."
 *   2 "Die Kamera richtet sich auch nicht selbstaendig wieder hinter Lara, sie
 *      behaelt immer ihre Position, an der ich sie auf der Kugel verschoben
 *      habe - ausser sie wird durch eine Wand beeinflusst."   (standing)
 *   3 "Die Kamera bewegt sich ganz leicht und super smooth wieder hinter Lara.
 *      [...] Das ist fast nicht zu merken. Ausser eine Wand kommt, dann geht
 *      sie mit dem Winkel der Wand schneller zum Ursprung hinter Lara."
 *                                                              (moving)
 *   4 "ist Lara zu nah, wird sie komplett ausgeblendet"         (at a wall)
 *
 * So: the stick moves the offset (1), a stick at rest keeps it (2), running
 * lets it fall back towards zero so slowly that it is hard to notice (3), and
 * geometry that displaces the camera is taken INTO the offset and makes that
 * fall faster while it lasts (3 again). TROne's orbit is NOT the model - it
 * holds a world angle that never returns at all - and nothing of it is copied
 * here; the only thing this file has in common with it is the idea that a
 * camera angle is a state and not a derivation.
 *
 * IT INCLUDES NO ENGINE HEADER, for the same reason game\modern_move.h does
 * not: Preparation\Tools\Diagnose\ModernTest.cpp compiles THIS FILE - not a
 * copy of it - and asserts on it. A feel number that nobody can re-measure is
 * a number nobody can change.
 *
 * ANGLES are the engine's own 16-bit units (65536 is a full turn, 0 is +Z),
 * the STICK is our own +-128 (specific\dxshell.cpp scales XInput by 256) and
 * a TICK is one logic step, 1/30 s (game\control.cpp:261 counts the loop down
 * in halves of a 60th).
 *
 * WHAT IS NOT HERE. The seam in game\camera.cpp and the predicate that hides
 * her are step 2; the pad table that frees the right stick is step 3; the
 * water is step 4; MODERNLOG and the record are step 5. */

/* THE CAMERA CODE'S OWN NUMBERS, MEASURED BEFORE ANYTHING WAS WRITTEN, because
 * an orbit that invents limits the classic camera already has is an orbit that
 * fights it. Every row was read out of the tree, not assumed:
 *
 *   elevation clamp        +-15470 (+-84.98 deg)  ChaseCamera, camera.cpp:1012
 *   resting elevation      -1820 (-10 deg, above) ChaseCamera, camera.cpp:1006
 *   elevation damping      (target - actual) >> 3 UpdateCameraElevation, :857
 *   distance               1536                   camera.cpp:258 and :602
 *   wall resolution        the wanted angle, else the nearest of the four
 *                          world cardinals with line of sight; mgLOS push 200,
 *                          CameraCollisionBounds push 384
 *                                                 ChaseCamera, camera.cpp:1043
 *   eye damping            pos += (ideal - pos) / speed      MoveCamera
 *   look camera pitch      364 per tick (2 deg), head -6370..+5460
 *                                                 LookUpDown, lara.cpp
 *   near clip plane        20 world units         InitWindow(...,20,20480,...),
 *                                                 specific\gamemain.cpp:261
 *   Lara's own radius      100..102               lara.cpp:1470 and :1475
 *   camera cut thresholds  Manhattan > 1024 on the eye and on the view
 *                          direction              INTERP_CAM_SNAP,
 *                                                 game\interpolate_math.h:187
 *
 * THE LAST ROW IS A CONSTRAINT ON THIS FILE and it was checked, not hoped
 * (the round's rule: the 60 fps cut detection must not fire on the orbit's own
 * steps). At full deflection the yaw moves 1092 units, 6 degrees, in one tick;
 * the ideal eye sits at 1536 * cos(10 deg) = 1512 units, so it travels
 * 2 * 1512 * sin(3 deg) = 158 units, at worst 224 measured the Manhattan way.
 * The pitch moves 3 degrees, which lifts the ideal by at most 1536 * sin(3
 * deg) = 80 units, and the engine's own >> 3 damping turns that into 10. The
 * two together are 304 against a threshold of 1024 - a margin of 3.4 - and the
 * real eye moves less again, because MoveCamera only ever walks a fraction of
 * the way to the ideal. The one thing that CAN trip the detection at the wall
 * is the classic quadrant snap, which jumps the ideal by a whole 90 degrees;
 * that is vanilla's, it is there at Tank today, and the orbit does not add to
 * it. */

/* EVERY CONSTANT OF THE ORBIT, WITH WHERE IT COMES FROM.
 *
 * Three origins appear below and they are not the same thing. "The camera
 * code's own" is a number read out of game\camera.cpp and reused so that the
 * orbit cannot disagree with the camera it steers. "The brief's" is a number
 * the user's model fixes directly. "Measure ourselves" is a feel number: it
 * carries the measurement plan and a MODERNLOG field (step 5), because the
 * play test is the only instrument that can read it.
 *
 * ModernCameraDefaults() below is the only thing that reads them. */
enum modern_camera_constants
{
	/* MEASURE, and the same answer as the left stick's MODERN_DEAD: nothing.
	 * The hardware zone is already applied before the axes reach us - XInput
	 * zeroes an axis under 7849 of 32767 and then divides by 256, the
	 * DirectInput fallback zeroes under 150 of 1000 and divides by 8
	 * (specific\dxshell.cpp, both paths) - so a second dead zone on top would
	 * only eat travel. The plan if the user reports the camera drifting on
	 * its own: MODERNLOG prints rs=, raise this in steps of 4 (3 per cent of
	 * full deflection) and read the log again. */
	MODERN_CAMERA_DEAD = 0,

	/* The brief's, and the one number the whole feel of the orbit hangs on:
	 * a fully pushed stick takes the camera once round Lara in about two
	 * seconds. 65536 units of turn in 60 ticks is 1092.27, so 1092 - and the
	 * pitch at half of it, a half turn in the same two seconds, which is the
	 * whole travel the clamp below allows and a little over. Between rest and
	 * full deflection the rate is proportional: the axis IS the rate, which is
	 * how every camera stick since about 2005 has read, and it keeps the
	 * arithmetic one multiply the test can check. */
	MODERN_CAMERA_YAW_RATE = 0x0444,	// 1092 = 360 deg in 2 s at 30 Hz
	MODERN_CAMERA_PITCH_RATE = 0x0222,	// 546 = 180 deg in 2 s

	/* THE CAMERA CODE'S OWN, and the answer to the brief's "find the classic
	 * camera's own elevation limits and use them, or +-85 degrees if it has
	 * none". It has them: ChaseCamera clamps camera.actual_elevation to
	 * +-15470 on every tick (camera.cpp:1012-1015), which is 84.98 degrees -
	 * the brief's own guess to two decimal places, and not a coincidence:
	 * that is what "as far as you like, but never over the pole" costs. Using
	 * the engine's number rather than 85 degrees of our own means the orbit
	 * asks for nothing the camera would silently take back from it. */
	MODERN_CAMERA_PITCH_MIN = -15470,
	MODERN_CAMERA_PITCH_MAX = 15470,

	/* THE CAMERA CODE'S OWN as well: where the chase camera sits when nobody
	 * has touched it. ChaseCamera opens with "if (!camera.target_elevation)
	 * camera.target_elevation = -1820" (camera.cpp:1006), ten degrees above
	 * her and looking down, and that is where TR5 has put the camera since
	 * 1999. The orbit's pitch IS that elevation - it is not an offset from it
	 * - so this is what a reset seats it at. Zero would not be "no opinion",
	 * it would be dead level, ten degrees below the game's own camera, and it
	 * would land exactly on the value that line treats as "unset". */
	MODERN_CAMERA_PITCH_HOME = -1820,

	/* MEASURE, the "fast nicht zu merken" of observation 3. The offset falls
	 * towards zero by this fraction of itself per tick, out of
	 * MODERN_CAMERA_RETURN_ONE - the engine's own idiom (UpdateCameraElevation
	 * does the same thing with a >> 3) but written as a fraction rather than a
	 * shift, because a shift can only halve and the number the brief asks for
	 * is between two shifts: a 90 degree offset is to halve in about four
	 * seconds of running, and 4 s is 120 ticks. 1 - 2^(-1/120) = 0.0057595,
	 * times 65536 is 377.46, so 377 - which gives a half life of 120.1 ticks,
	 * 4.005 s. A shift of 7 would have been 2.95 s and a shift of 8, 5.9 s.
	 * The plan: MODERNLOG prints cam_off and cam_ret every tick, so the play
	 * test reads the real curve and this number moves in halves and doubles
	 * (188 = 8 s, 754 = 2 s) until the user says it is right. */
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

	/* MEASURE, the second half of observation 3: "dann geht sie mit dem
	 * Winkel der Wand SCHNELLER zum Ursprung hinter Lara". A factor and not a
	 * second rate, so that "faster than the return" is structural - it cannot
	 * come apart when the return above is tuned, and the test can prove the
	 * relation rather than compare two numbers that happen to differ. Eight
	 * times 377 is a half life of 14.7 ticks, half a second: fast enough to
	 * read as the wall taking the camera, slow enough to still be a movement
	 * and not a cut. */
	MODERN_CAMERA_WALL_FACTOR = 8,

	/* What counts as a wall at all. The ideal eye is an integer position at a
	 * radius of about 1500 units, so one unit of rounding is 0.04 degrees;
	 * the smallest displacement the camera code can really produce is
	 * CameraCollisionBounds' own 384 units of push, which at that radius is
	 * atan(384/1512) = 14 degrees, and the quadrant snap is 45 degrees or
	 * more. 1.4 degrees therefore sits in the empty middle: everything under
	 * it is arithmetic, everything over it is geometry. */
	MODERN_CAMERA_WALL_DEAD = 0x0100,	// 256 units = 1.4 deg

	/* MEASURE, observation 4: "ist Lara zu nah, wird sie komplett
	 * ausgeblendet" - and the brief's origin for it is "the distance at which
	 * the classic camera's own near clip cuts her". Both halves of that are
	 * in the tree: the near plane is 20 world units (InitWindow(0, 0, w, h,
	 * 20, 20480, 80, ...) in specific\gamemain.cpp:261 and winmain.cpp:277),
	 * and her own body is a cylinder of radius 102 (coll->radius in
	 * game\lara.cpp:1470, the standing and running value). 20 + 102 = 122 is
	 * therefore the exact distance from the eye to her centre at which the
	 * near plane stops being in front of her and starts being inside her.
	 * The plan: MODERNLOG prints hide= and the measured distance, and if the
	 * play test sees her sliced open before she vanishes this rises towards
	 * 384 - the camera collision's own push, the closest the camera is ever
	 * meant to sit to anything - in steps of 64. */
	MODERN_CAMERA_HIDE_DIST = 122,

	/* THE CAMERA CODE'S OWN, again, and a coin flip taken out of our hands.
	 * Which way a pushed stick moves the view is taste in every other engine,
	 * but ours has already answered it once: the classic look camera turns
	 * IN_FORWARD into "look up" (LookUpDown in game\lara.cpp - head_x_rot -=
	 * 364, and a negative x_rot is up). Looking up from an orbit means the
	 * camera drops BELOW her, which is a POSITIVE elevation here, and our
	 * stick reads forward-negative - so the default subtracts the y axis.
	 * The switch exists because half the world calls that inverted, and
	 * because flipping it must not need a rebuild of anything but this line.
	 */
	MODERN_CAMERA_INVERT_PITCH = 0,

	/* The two switches of the orbit, and they are switches for the same
	 * reason A1's five are: each one turns off exactly one rule, which is how
	 * a play test bisects a complaint instead of describing it.
	 *   follow_wall   the displacement geometry forces on the camera is taken
	 *                 INTO the offset, so the offset follows the wall
	 *                 (observation 3's "mit dem Winkel der Wand"); off, the
	 *                 camera fights the wall back to where the stick left it
	 *   pitch_return  the pitch falls back to the resting elevation the way
	 *                 the yaw falls back behind her. It ships OFF: the user
	 *                 described the yaw returning and said nothing at all
	 *                 about the height, and observation 2 says what he did not
	 *                 ask for stays where he put it. The capability is real
	 *                 and testable with an explicit cfg; it is simply not what
	 *                 ships until he asks for it. */
	MODERN_CAMERA_FOLLOW_WALL = 1,
	MODERN_CAMERA_PITCH_RETURN = 0
};

/* The orbit's thresholds, in one place, filled by ModernCameraDefaults().
 * Every field is a long so that nothing in the update has to convert: the
 * angles are signed 16-bit values widened, the rates are fractions of
 * MODERN_CAMERA_RETURN_ONE and the flags are 0 or 1. */
struct MODERN_CAMERA_CFG
{
	long dead;			// right stick deflection under which nothing happens
	long stick_full;	// full deflection in our units (MODERN_STICK_FULL)
	long yaw_rate;		// units of yaw per tick at full deflection
	long pitch_rate;	// units of pitch per tick at full deflection
	long pitch_min;		// the elevation clamp, the classic camera's own
	long pitch_max;
	long pitch_home;	// the elevation a reset seats her camera at
	long invert_pitch;	// 0 = stick forward looks up, as the look camera does
	long return_rate;	// per MODERN_CAMERA_RETURN_ONE of the offset per tick
	long return_min;	// the smallest step, so the return really arrives
	long pitch_return;	// 1 = the pitch falls back to pitch_home as well
	long wall_factor;	// how much faster the return runs while a wall pushes
	long wall_dead;		// a displacement under this is arithmetic, not a wall
	long follow_wall;	// 1 = the wall's displacement is taken into the offset
	long hide_dist;		// eye-to-Lara distance under which she is not drawn
};

/* Where the camera sits on the sphere, and what the last tick did to it.
 * game\camera.cpp (step 2) owns the one instance and resets it wherever
 * ModernResetControls() is called, because a camera offset seated in the last
 * level is no offset at all. */
struct MODERN_CAMERA_STATE
{
	short offset_yaw;	// around her: 0 is behind her back, full 360 either way
	short offset_pitch;	// and how high: the camera elevation itself, clamped
	short wall_push;	// the last displacement geometry forced on the camera
	long wall;			// 1 = that happened THIS tick, so the return ran fast
	long ret;			// the return step this tick, signed - MODERNLOG cam_ret
	long steering;		// 1 = the stick was out, so the return stood still
};

void ModernCameraDefaults(MODERN_CAMERA_CFG* cfg);
void ModernCameraReset(MODERN_CAMERA_STATE* state);
void ModernCameraUpdate(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state,
	long rs_x, long rs_y, long lara_moving, long wall_push_yaw,
	short* out_yaw_offset, short* out_pitch);
long ModernCameraHidesLara(const MODERN_CAMERA_CFG* cfg, long distance);
short ModernCameraSwimPitch(const MODERN_CAMERA_CFG* cfg, long pitch);

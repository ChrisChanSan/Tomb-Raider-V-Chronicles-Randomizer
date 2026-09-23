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
#include "camera.h"
#include "gameflow.h"
#include "../specific/function_stubs.h"
#include "effects.h"
#include "draw.h"
#include "sound.h"
#include "deltapak.h"
#include "control.h"
#include "effect2.h"
#include "objects.h"
#include "delstuff.h"
#include "../specific/3dmath.h"
#include "sphere.h"
#include "debris.h"
#include "tower.h"
#include "larafire.h"
#include "../specific/input.h"
#include "../specific/audio.h"
#include "modern_camera.h"		// the orbit camera's arithmetic
#include "modern_state.h"		// Lara's heading as the modern control left it
#include "lara.h"
#include "tomb4fx.h"
#include "savegame.h"
#include "delstuff.h"
#include "../tomb5/tomb5.h"
#include "spotcam.h"

CAMERA_INFO camera;

SHATTER_ITEM ShatterItem;

GAME_VECTOR ForcedFixedCamera;
char UseForcedFixedCamera;

PHD_VECTOR CamOldPos;

camera_type BinocularOldCamera = CHASE_CAMERA;
long BinocularOn = 0;
long BinocularRange = 0;
long ExittingBinos = 0;
long LaserSight = 0;
long InfraRed = 0;
char SniperOverlay = 0;
char SniperCamActive = 0;

// First person view: whether this code hid Lara's head, whether it set the
// field of view, and the head bone's roll. They stand up here because
// CalculateCamera needs them; the camera itself lives further down next to
// its template BinocularCamera.
static long fpHeadHidden = 0;
static long fpFovSet = 0;
static short fpRoll = 0;

// The smoothed view direction and up vector of the head bone camera.
//
// Lara's head bobs with every step and breathes on the spot; a camera that
// turns with the bone turns that bob into a jolt. The two VECTORS are
// filtered, not the angles: an angle wraps at half a turn and a filter would
// jump the long way round the moment Lara crosses it.
//
// One fixed strength cannot serve both a shiver (a fraction of a degree) and
// a somersault (a quarter turn in a frame), so the strength slides with the
// size of the movement: small movement, heavy filter; large movement, none.
//
// The slow setting damps a shiver of 0.34 degrees a frame, reversing every
// other frame: a filter at 0.30 passes under a fifth of it - well under a
// pixel - while trailing a steady turn by only about three frames. The
// strength is the player's option, because damping harder hides a shake but
// lengthens the tail the camera drags behind a movement that has stopped.
// Off means no damping at all - the bone as it comes.
static const float fpSmoothSlow[4] = { 1.00F, 0.30F, 0.18F, 0.10F };
#define FP_SMOOTH_SLOW	(fpSmoothSlow[tomb5.first_person_smooth & 3])
// A large movement is passed through whole. A partial factor (0.75) would go
// most of the way on the jump and creep the rest afterwards, which shows as a
// second beat at the end of every animation.
#define FP_SMOOTH_FAST	1.00F		// a somersault: followed outright
// The strength is read off how fast the bone itself moves, not off how far
// the view has fallen behind: a slowly drifting but noisy signal keeps that
// distance moderate the whole time. Measured off the head bone's trace (-log,
// FPHEAD), the bone's direction moves per frame:
//
//   standing (anim 103)   0.60 degrees, and it hardly ever reverses
//   walking  (anim 0)     0.70 degrees typical, one hard step of 1.53
//   running  (anim 75)   32     degrees
//
// The shiver moves the bone 0.34 degrees a frame, the player looking around
// moves it 2 to 7, so the boundaries sit at 1 and 6 degrees.
#define FP_RATE_SLOW	0.00015F	// 1 - cos(1 degree): only the shiver below
#define FP_RATE_FAST	0.0055F		// 1 - cos(6 degrees): looking about, follow
// The same for the eye's own travel. The head's offset inside Lara moves up
// to 8 units a frame while walking and reverses constantly - some 8 pixels on
// screen, a shimmer. A jump or a fall moves it far further and comes through.
#define FP_MOVE_SLOW	1.0F		// units per frame: only the bob below
#define FP_MOVE_FAST	12.0F		// units per frame: a real move, follow
static float fpDir[3] = { 0, 0, 0 };
static float fpUp[3] = { 0, 0, 0 };
static float fpRel[3] = { 0, 0, 0 };

// The previous frame's raw reading, to measure how fast the bone is moving.
// It is only compared, not averaged in: an average of two frames steps half
// way on every jump and then the rest, a visible stutter at the end of every
// animation, and the walk is a noisy curve rather than a clean frame-by-frame
// alternation such an average would cancel.
static float fpPrevDir[3] = { 0, 0, 0 };
static float fpPrevUp[3] = { 0, 0, 0 };
static float fpPrevRel[3] = { 0, 0, 0 };
static long fpSmoothValid = 0;
static long fpRanLast = 0;
static short fpPrevEyeRoom = 0;

// Which way the view has been moving lately, and how much it has moved at
// all. Their ratio says whether a movement is GOING somewhere or merely
// shaking: a somersault turns one way for the better part of a second, so
// the two agree and the ratio is near one; a stutter reverses, the signed
// part cancels itself, and the ratio falls towards nothing. Speed alone
// cannot tell them apart: it reads the stumble at the end of an animation
// as large as a real turn.
#define FP_TREND	0.30F		// how quickly the trend follows
static float fpVel[3] = { 0, 0, 0 };
static float fpSpd = 0;
static long fpRawX = 0, fpRawY = 0, fpRawZ = 0;
static float fpLogK = 0;
static long fpLogLos = 0;
static long fpLogLRoom = 0, fpLogERoom = 0, fpLogHeadY = 0;
static long fpLogFloor = 0, fpLogCeil = 0, fpLogClamp = 0, fpLogYBefore = 0;
static long fpLogEyeH = 0, fpLogEyeC = 0, fpLogEyeY = 0, fpLogReal = 0;
static bool FirstPersonEnabled();

// How far the eye sits in front of the head center, and how much air must
// be free in front of it so the renderer's near plane (20, see InitWindow)
// does not stick in the wall.
#define FP_EYE_OFFSET	48
#define FP_MARGIN	48
// How far the eye is held off floor and ceiling. Larger than the near
// plane's twenty, because that plane is a surface and its corners reach
// beyond its centre.
#define FP_CLEAR	48
// The most a clamp may shift the camera. Beyond this the floor and
// ceiling that came back do not describe where the eye is, and moving it
// that far does more harm than the thing being corrected.
#define FP_CLAMP_MAX	128
static char SniperCount = 0;

uchar WeaponDelay = 0;

static long bLaraTorch;

static OLD_CAMERA old_cam;
static GAME_VECTOR last_target;
static GAME_VECTOR last_ideal;
static GAME_VECTOR static_lookcamp;
static GAME_VECTOR static_lookcamt;
static char TargetSnaps = 0;
static char CameraSnaps = 0;

/* ==========================================================================
 * The modern (orbit) camera: the engine's side.
 *
 * The orbit itself is game\modern_camera.cpp: two remembered angles and three
 * rules, no engine in it, checked by Preparation\Tools\Diagnose\ModernTest.cpp.
 * The modern camera is the chase camera with its angle taken from that state
 * instead of from Lara alone; these are the places the engine touches it:
 *
 *   the axes    specific\input.cpp hands the right stick over on every
 *               S_UpdateInput (ModernCameraStick); the orbit reads it once
 *               per logic tick - the menus and the FMV loop call
 *               S_UpdateInput in loops of their own
 *   the tick    ModernCameraOrbit, from ChaseCamera and from CombatCamera,
 *               exactly one of which runs per tick
 *   the angle   camera.target_angle = offset_yaw, which UpdateCameraElevation
 *               already adds to her heading - "yaw = y_rot + offset_yaw"
 *               written into the engine's own slot
 *   the height  camera.target_elevation = offset_pitch, replacing the two
 *               lines ChaseCamera opens with (ModernCameraOrbit says why)
 *   the wall    the candidate loop's answer: in Modern there is one
 *               candidate, the angle the player dialled, clipped by mgLOS at
 *               any distance - the distance shrinks where the angle used to
 *               jump, and "clipped" is what mgLOS returned
 *   the lead    ModernCameraLeadEye, the last thing before MoveCamera: the
 *               eye is turned by this tick's own stick step so that the
 *               damping cannot hold the player's input back
 *   the draw    ModernLaraHidden, asked once per frame in game\draw.cpp
 *   the water   ModernCameraSwimTarget, asked by game\modern_state.cpp once
 *               per tick while she swims: the orbit is the pointer her body
 *               follows, not only the view
 *   the log     ModernCameraLogState / ModernCameraLogConfig, read-only, for
 *               game\modern_state.cpp's MODERNLOG, reached only behind its
 *               gLogEnabled so they cost nothing without -log
 *
 * With tomb5.control_modern off, ModernCameraActive() is false at its first
 * term and every one of these is the vanilla line: the orbit is not ticked,
 * both slots keep the vanilla values, and the draw predicate returns false
 * before it measures anything. MODERNLOG never runs without the option on.
 *
 * The level cameras are discarded exactly as first person discards them,
 * sniper excepted (CalculateCamera). Flybys and cutscenes are untouched in
 * every mode - CalculateCamera is not called during a flyby.
 *
 * MODERN_CAMERA_LEAD_MAX, the bound ModernCameraLeadEye needs, lives in
 * game\modern_camera.h so that ModernTest reads the same number. */

static MODERN_CAMERA_CFG modern_cam_cfg;
static MODERN_CAMERA_STATE modern_cam_state;
static long modern_cam_armed;		// has the cfg above been filled yet?
static long modern_cam_rs_x;		// the right stick, as input.cpp last read it
static long modern_cam_rs_y;
static long modern_cam_clipped;		// did mgLOS have to clip the eye LAST tick?
static ushort modern_cam_clipped_at;	// the tick that measured it - see ModernCameraOrbit
static ushort modern_cam_orbit_at;	// and the tick the orbit itself last ran - ModernCameraWorldYaw
static long modern_cam_lead_yaw;	// what the STICK moved this tick, and not the return
static long modern_cam_lead_pitch;	// - the two steps ModernCameraLeadEye hands the eye
static long modern_cam_have_pos;	// is the position below this level's?
static long modern_cam_x, modern_cam_y, modern_cam_z;

static void ModernCameraArm()
{
	if (!modern_cam_armed)
	{
		ModernCameraDefaults(&modern_cam_cfg);
		ModernCameraReset(&modern_cam_state);
		modern_cam_armed = 1;
	}

	/* The speed slider is applied on every call, not only while arming. Every
	 * accessor below calls this function first, so the Controls row moved from
	 * the pause menu takes hold on the very next tick, like Music and SFX,
	 * rather than at the next level start or scheme switch. tomb5.camera_speed
	 * is the row's stored 0..100; ModernCameraSpeedRates (game\modern_camera.cpp)
	 * turns it into the two rates and touches nothing else of the cfg. */
	ModernCameraSpeedRates(tomb5.camera_speed, &modern_cam_cfg.yaw_rate, &modern_cam_cfg.pitch_rate);
}

/* Forgets the orbit. Called from InitialiseCamera below - once per level,
 * which covers every savegame restore, because a restore is a level start in
 * this engine (game\gameflow.cpp) - and from specific\LoadSave.cpp when the
 * control scheme is switched, because an offset seated under Tank means
 * nothing.
 *
 * It is not hung on ModernResetControls(): the modern control resets itself
 * on every break in its own tick chain (game\modern_state.cpp), and one of
 * those breaks is the look key. Releasing look would then fling the camera
 * back behind her, while a standing Lara must keep the angle the player set. */
void ModernCameraResetOrbit()
{
	modern_cam_armed = 0;
	ModernCameraArm();
	modern_cam_clipped = 0;
	modern_cam_clipped_at = 0;	// harmless either way: a flag of 0 turns any tick it is accepted on into no wall at all
	modern_cam_orbit_at = 0;	// likewise: the state it guards is unseated by the reset above, and unseated answers nothing
	modern_cam_lead_yaw = 0;
	modern_cam_lead_pitch = 0;
	modern_cam_have_pos = 0;
	modern_cam_x = 0;
	modern_cam_y = 0;
	modern_cam_z = 0;
}

/* The right stick, from the one place that knows what a stick is. Stored and
 * not acted on: this is called from S_UpdateInput, which the menus, the
 * inventory and the FMV player also call, and the orbit may only move once
 * per logic tick. The latest reading is the right one to keep - a tick is a
 * thirtieth of a second behind the thumb either way. */
void ModernCameraStick(long rs_x, long rs_y)
{
	modern_cam_rs_x = rs_x;
	modern_cam_rs_y = rs_y;
}

bool ModernCameraActive()
{
	if (!tomb5.control_modern)
		return false;

	if (!lara_item)
		return false;

	/* The same three FirstPersonEnabled() refuses, for the same reasons: the
	 * title's camera runs one whole pass out of InitialiseCamera before the
	 * flyby is armed, and a flyby or a cutscene is not the player's view to
	 * steer. */
	if (gfLevelId == LVL5_TITLE || bUseSpotCam || GLOBAL_playing_cutseq)
		return false;

	/* And where both views are on, first person wins - it replaces the chase
	 * camera outright, so an orbit under it would be a state nobody reads,
	 * and its hide predicate would take her arms away at a distance of
	 * nothing. */
	if (FirstPersonEnabled())
		return false;

	return true;
}

/* Whether Lara is moving: the one thing the orbit cannot work out for itself.
 * Standing, the player's offset is kept; moving, it falls back behind her.
 *
 * Measured from her position alone, so that running, sliding, falling,
 * climbing and being pushed all count the same and no collision internals are
 * involved. Four units is the camera code's own threshold: CalculateCamera
 * treats a target within 4 of the last one as the same target. Her idle
 * animations move meshes and not the item, so standing still reads zero. */
static long ModernCameraLaraMoving()
{
	long dx, dy, dz, moving;

	dx = abs(lara_item->pos.x_pos - modern_cam_x);
	dy = abs(lara_item->pos.y_pos - modern_cam_y);
	dz = abs(lara_item->pos.z_pos - modern_cam_z);
	moving = (modern_cam_have_pos && (dx >= 4 || dy >= 4 || dz >= 4)) ? 1 : 0;

	modern_cam_x = lara_item->pos.x_pos;
	modern_cam_y = lara_item->pos.y_pos;
	modern_cam_z = lara_item->pos.z_pos;
	modern_cam_have_pos = 1;

	return moving;
}

/* Swings the camera after something other than the stick turned Lara, so that
 * the camera turns with her and settles behind her again instead of staying
 * where it was.
 *
 * The measure is the part of her turn this tick that the modern control did
 * not make. game\modern_state.cpp runs at the top of the tick body and
 * publishes her heading as it left it; by the time this runs, AnimateLara and
 * the collision have run. The published heading is read back off the item,
 * so a second way for the stick to turn her would still be subtracted.
 *
 * Not only animations reach it. Beside the animation flips, four writers of
 * lara_item->pos.y_rot can land after the publish: game\box.cpp, where a
 * creature seizes her for the kill and snaps her onto its own heading;
 * game\lifts.cpp and game\tomb4fx.cpp, which seat her on an object's heading
 * half a turn round; and game\traps.cpp, the ceiling trapdoor's half turn.
 * Each leaves the eye in front of her face after a one-tick jump, so each gets
 * the same cure. Two writers cannot reach it: game\mirror.cpp negates y_rot
 * and restores it inside one DrawLara, long after CalculateCamera, and
 * game\laraswim.cpp's 910 is five degrees, a fiftieth of the threshold.
 *
 * It runs before ModernCameraUpdate, so the swing's first instalment is paid
 * out on the tick of the flip; the threshold, the side and the profile are
 * ModernCameraCarry's (game\modern_camera.cpp). A tick with no published
 * heading seats nothing. */
static void ModernCameraCarryFromAnim()
{
	short turned;

	if (!ModernHeadingAfterInput(&turned))
		return;

	ModernCameraCarry(&modern_cam_cfg, &modern_cam_state,
		(short)(lara_item->pos.y_rot - turned));
}

/* The ledge hang turns her half round in eight steps of 22.5 degrees, each
 * far under carry_flip, so ModernCameraCarryFromAnim never sees a flip and
 * she would turn under a standing camera. lara_as_ledgehang asks for the
 * half-turn swing itself, once, on the first tick of the turn.
 *
 * Only while the orbit runs, as ModernCameraCarryFromAnim is only asked from
 * the orbit: under first person or a flyby nothing pays the swing out, so a
 * seated one would wait and swing the camera round when the orbit resumes,
 * and each further ledge hang in the meantime would add another half turn. */
void ModernCameraCarryHalfTurn()
{
	if (!ModernCameraActive())
		return;

	ModernCameraArm();
	ModernCameraCarry(&modern_cam_cfg, &modern_cam_state, 0x8000);
}

/* A ledge-hang turn broken off after its first tick leaves her only part of
 * the way round, so the rest of the half-turn swing would end the camera in
 * front of her; a new turn started on the next tick would add another half
 * turn on top. lara_as_ledgehang drops what is unpaid, exactly as the right
 * stick drops it in ModernCameraUpdate (game\modern_camera.cpp).
 *
 * Not only while the orbit runs: a swing seated before the orbit stopped
 * would otherwise wait and swing the camera round when it resumes. */
void ModernCameraDropSwing()
{
	ModernCameraArm();
	modern_cam_state.carry = 0;
	modern_cam_state.carry_total = 0;
	modern_cam_state.carry_tick = 0;
}

/* One tick of the orbit, and the two camera slots it writes. */
static void ModernCameraOrbit()
{
	short yaw;
	short pitch;
	short was_world;
	short was_pitch;
	long was_seated;
	long clipped;

	ModernCameraArm();

	/* The turn something else gave her, before the tick is paid out. It needs
	 * the cfg, so it comes after the arming; it changes only the three carry
	 * fields of the state, nothing the three reads below want. */
	ModernCameraCarryFromAnim();

	/* A wall counts for one tick, not across an interruption. It is measured
	 * at the end of one orbit tick and answered at the start of the next, but
	 * the orbit stands still through a flyby, a cutscene, first person, the look
	 * key and the four rich2 sniper cameras; a wall seen before such a break
	 * must not be answered after it, against a camera and a world that are no
	 * longer there.
	 *
	 * GlobalCounter is the stamp: game\control.cpp steps it once per logic
	 * tick and calls CalculateCamera once in the same tick body, so two
	 * consecutive orbit ticks are exactly one apart and anything else is a gap.
	 * ModernCameraTickFollows (game\modern_camera.cpp) does that arithmetic,
	 * shared with the orbit's own stamp at the end of this function so the two
	 * cannot disagree; the wrap at 65536 is harmless, as that file explains. */
	clipped = ModernCameraTickFollows(GlobalCounter, modern_cam_clipped_at) ? modern_cam_clipped : 0;

	/* Her heading is handed in, which makes the orbit a world angle: the state
	 * holds where the eye stands in the world, and the offset written into
	 * camera.target_angle is that angle measured against her y_rot of this
	 * tick. ModernCameraActive() has already refused a missing lara_item. */
	was_world = modern_cam_state.world_yaw;
	was_pitch = modern_cam_state.offset_pitch;
	was_seated = modern_cam_state.seated;

	ModernCameraUpdate(&modern_cam_cfg, &modern_cam_state, modern_cam_rs_x,
		modern_cam_rs_y, lara_item->pos.y_rot, ModernCameraLaraMoving(), clipped,
		&yaw, &pitch);
	modern_cam_clipped = 0;		// answered; the candidate loop writes this tick's own

	/* What the stick moved, and only the stick. The eye is damped towards its
	 * ideal by MoveCamera and the elevation by UpdateCameraElevation's >> 3,
	 * which is right for her running, for a wall and for the return, and wrong
	 * for the thumb: damped, the eye needs 18 ticks of a fully pushed stick to
	 * reach 90 per cent of the rate the orbit already turns at. So the stick's
	 * share of this tick is handed to the eye whole, below.
	 *
	 * It is a difference, not a second sum, so the orbit stays the one place
	 * the rules live: the world yaw moved by the stick and by the return, and
	 * the return's step is published as `ret`. The return only runs while the
	 * stick rests (game\modern_camera.cpp), so subtracting it leaves the stick.
	 * With cfg.pitch_return on (it ships off, MODERN_CAMERA_PITCH_RETURN) the
	 * pitch's fall home would be led as well, making it instant, not damped.
	 *
	 * Not on the seating tick: the first update after a reset ties the world
	 * yaw to her heading, so the difference there is her heading and not a
	 * stick step; leading the eye by it would fling it half a turn. */
	if (was_seated)
	{
		modern_cam_lead_yaw = (short)(modern_cam_state.world_yaw - was_world) - modern_cam_state.ret;
		modern_cam_lead_pitch = modern_cam_state.offset_pitch - was_pitch;
	}
	else
	{
		modern_cam_lead_yaw = 0;
		modern_cam_lead_pitch = 0;
	}

	/* target_angle is 0 all game unless a trigger camera has just written one,
	 * and CalculateCamera takes that back at its own end. So the offset goes in
	 * there rather than into a second angle path, and every reader of the
	 * camera's heading (this file and game\modern_state.cpp) keeps reading the
	 * same slot. A camera hint an animation wrote for the classic camera
	 * (game\lara.cpp) is overwritten: in Modern the view is the player's.
	 *
	 * The elevation is replaced, not added to. ChaseCamera seats -1820 into
	 * target_elevation whenever it reads 0 and then adds item->pos.x_rot on
	 * every tick, so in vanilla it accumulates her own pitch on a slope -
	 * absorbed by the >> 3 damping and the +-15470 clamp. In Modern the camera
	 * height is the player's and her slope may not push it about. The orbit's
	 * pitch is also never 0, the value that line reads as "unset"
	 * (game\modern_camera.cpp seats it at -1820 instead). */
	camera.target_angle = yaw;
	camera.target_elevation = pitch;

	/* The pitch step bypasses the damping on the spot. UpdateCameraElevation
	 * runs a few lines below this call in both
	 * orbit cameras and closes an eighth of whatever gap it finds, so a stick
	 * step of 546 units would arrive as 68 this tick. Adding the step to the
	 * DAMPED angle as well leaves the gap exactly as it was, which means the
	 * >> 3 keeps doing its job on her slope, on the wall and on the return and
	 * does nothing at all to the thumb. */
	camera.actual_elevation = (short)(camera.actual_elevation + modern_cam_lead_pitch);

	/* The orbit stamps that it ran. game\modern_state.cpp builds her course
	 * against the world angle this function keeps, and that angle is only the
	 * player's view while this function is the camera. Under a rich2 sniper
	 * camera - the one level camera Modern does not discard - FixedCamera runs
	 * instead and the orbit stands still while the player still steers her.
	 * ModernCameraWorldYaw checks this stamp, one tick apart, like the clip
	 * flag. */
	modern_cam_orbit_at = GlobalCounter;
}

/* Turns the eye by this tick's stick step before MoveCamera's lag sees it.
 *
 * MoveCamera is a first order lag - "camera.pos += (ideal - pos) / speed", with
 * speed 10 behind her and 8 with a weapon out. With the orbit turning at 1066
 * units a tick, the picture needs 18 ticks (0.6 s) to reach nine tenths of it,
 * and no rate cures that: a lag costs the same fraction of a fast push as of a
 * slow one. A faster `speed` would also stop damping her running, the wall's
 * pull and the return, since it is one number for the whole vector. Turning
 * the eye instead moves the lag's starting point by exactly what the ideal
 * moved: the error it closes is untouched, and the stick's step arrives whole.
 * Measured in Preparation\Tools\Diagnose\ModernTest (camera_lead): the first
 * tick of a push moves the eye 99.9 per cent of the orbit's step, against 9.5
 * (speed 10) and 12.0 (speed 8) without the lead.
 *
 * This file cannot be linked without a level, so ModernTest's lead_run()
 * mirrors the body below - the two bounds, the scale, the pitch-then-yaw order
 * and the truncating divisions. Only MODERN_CAMERA_LEAD_MAX is shared
 * (game\modern_camera.h); change one, change the other.
 *
 * The pitch is the same rotation about the horizontal axis through her; its
 * other half goes into camera.actual_elevation in ModernCameraOrbit. It is
 * not applied to an eye that is not orbiting her (MODERN_CAMERA_LEAD_MAX) or
 * to a horizontal radius of nothing, which has no direction to turn.
 *
 * It may not outrun the ideal. Turning by an angle moves the eye along a
 * chord of 2 * r * sin(step / 2), so a far eye moves further than a near one,
 * and the 60 fps interpolator relies on the eye never moving further than the
 * orbit's own step at the orbit's own radius. An eye arriving from a cut or a
 * released fixed camera comes in from thousands of units away; past 3460
 * units the chord alone passes INTERP_CAM_SNAP and the interpolator would cut.
 * So past camera.target_distance the step is scaled down by the ratio the
 * radius is up, which keeps the arc what it is at the orbit's radius (swept in
 * ModernTest, camera_camcut). Inside that radius nothing changes. */
static void ModernCameraLeadEye()
{
	long dx, dy, dz, h, nh, c, s, r, lim, lead_yaw, lead_pitch;

	lead_yaw = modern_cam_lead_yaw;
	lead_pitch = modern_cam_lead_pitch;

	if (!lead_yaw && !lead_pitch)
		return;

	dx = camera.pos.x - camera.target.x;
	dy = camera.pos.y - camera.target.y;
	dz = camera.pos.z - camera.target.z;

	if (abs(dx) > MODERN_CAMERA_LEAD_MAX || abs(dy) > MODERN_CAMERA_LEAD_MAX ||
		abs(dz) > MODERN_CAMERA_LEAD_MAX)
		return;

	h = (long)phd_sqrt((ulong)(SQUARE(dx) + SQUARE(dz)));

	/* Never further than the ideal itself moved. r is the whole radius because
	 * the pitch turns about her in the vertical plane and the yaw in the
	 * horizontal one, and r bounds both; the truncation of the two divisions
	 * falls on the safe side of the threshold. lim guards a target_distance of
	 * nothing, which no state sets but which would be a divide by nothing. */
	r = (long)phd_sqrt((ulong)(SQUARE(h) + SQUARE(dy)));
	lim = camera.target_distance;

	if (lim > 0 && r > lim)
	{
		lead_yaw = lead_yaw * lim / r;
		lead_pitch = lead_pitch * lim / r;
	}

	/* The pitch, the engine's own arithmetic read backwards: the
	 * candidates are built as y = target.y + R * sin(elevation) and a radius of
	 * R * cos(elevation), so raising the elevation by a step turns (h, dy)
	 * through that step and the horizontal pair follows its own length. */
	if (lead_pitch && h)
	{
		c = phd_cos(lead_pitch);
		s = phd_sin(lead_pitch);
		nh = (h * c - dy * s) >> W2V_SHIFT;
		dy = (dy * c + h * s) >> W2V_SHIFT;

		if (nh < 0)
			nh = 0;	// over the pole, which the orbit's own cap keeps her off anyway

		dx = dx * nh / h;
		dz = dz * nh / h;
	}

	if (lead_yaw)
	{
		c = phd_cos(lead_yaw);
		s = phd_sin(lead_yaw);
		nh = (dx * c + dz * s) >> W2V_SHIFT;
		dz = (dz * c - dx * s) >> W2V_SHIFT;
		dx = nh;
	}

	camera.pos.x = camera.target.x + dx;
	camera.pos.y = camera.target.y + dy;
	camera.pos.z = camera.target.z + dz;
}

/* Where the player has put the camera, in the world, for
 * game\modern_state.cpp's movement reference.
 *
 * The measured vector eye -> target is right for a camera nobody steers, but
 * fails twice under the orbit: at a steep pitch the horizontal radius is a
 * few dozen units while the eye trails her run by hundreds, so the measured
 * yaw becomes her own running direction and her course runs away from her;
 * and a wall that pulls the eye in does the same at any pitch (measured: 57
 * jumps of more than 45 degrees in one tick, her course 3154 -> 17126 ->
 * 26721). The world yaw is what the player dialled, exact at any pitch, and
 * does not move when geometry moves the eye.
 *
 * It answers 0, and the caller falls back to the measured vector, in two
 * cases: before the orbit's first tick (`seated`), and on a tick the orbit
 * did not run. ModernCameraActive() does not mean "the orbit is the camera":
 * under a rich2 sniper camera (CalculateCamera's modernDiscard keeps sniperCam
 * on purpose) FixedCamera runs, the orbit stands still, and the player still
 * steers her. A frozen world angle would walk her along the chase camera's
 * angle from before the cut instead of relative to the sniper view. The look
 * key needs no case, because the seam does not run under it
 * (specific\input.cpp's !(input & IN_LOOK)); a flyby, a cutscene, the title
 * and first person are refused by ModernCameraActive(). */
long ModernCameraWorldYaw(short* yaw)
{
	ModernCameraArm();

	if (!modern_cam_state.seated)
		return 0;

	/* The caller is game\modern_state.cpp, reached from S_UpdateInput, which
	 * game\control.cpp runs before CalculateCamera in the same tick body - so
	 * the orbit's own most recent tick is the previous one and
	 * "one apart" is what a live orbit looks like from here, exactly as it is
	 * from the clip flag's side. */
	if (!ModernCameraTickFollows(GlobalCounter, modern_cam_orbit_at))
		return 0;

	if (yaw)
		*yaw = modern_cam_state.world_yaw;

	return 1;
}

/* How far the player's own stick moved the orbit last tick.
 *
 * The same two gates as the world yaw above: an unseated orbit has no step to
 * give, and an orbit that did not tick on the previous tick would hand out the
 * step of some earlier tick, which would be added to her course twice.
 *
 * It is not modern_cam_lead_yaw, which is the world yaw's step less the
 * return and so contains the carried swing after an about-turn. That is right
 * for leading the eye and wrong here: a camera moving by itself - the return
 * home, the carried swing - must leave her course alone, and only a camera the
 * player turns takes it along. So the number is the orbit's `stick_step`,
 * which game\modern_camera.cpp takes from rule 1 before the other two run.
 *
 * It is one tick behind, like the world yaw, because game\control.cpp runs
 * S_UpdateInput before CalculateCamera. Adding it to the course is not double
 * counting: the course was last read against that same previous tick. */
long ModernCameraPlayerStep(short* step)
{
	ModernCameraArm();

	if (!modern_cam_state.seated)
		return 0;

	if (!ModernCameraTickFollows(GlobalCounter, modern_cam_orbit_at))
		return 0;

	if (step)
		*step = modern_cam_state.stick_step;

	return 1;
}

/* How far the eye is from her, measured with the camera's own two points.
 *
 * camera.target is Lara for both orbit cameras: her chest for the chase
 * camera (ConfirmCameraTargetPos), her position for the combat one. So the
 * distance from camera.pos to it is the camera's own idea of how far away she
 * is - it wants to be 1536, and it goes to nothing when geometry has pushed
 * the eye into her. That is what MODERN_CAMERA_HIDE_DIST rests on: the near
 * plane is 20 units and her body a cylinder of radius 102, so 122 is the
 * eye-to-centre distance at which that plane stops being in front of her.
 *
 * It uses the simulation pair, not the interpolated one. The answer is a hard
 * cut, not a fade, so at 60 or 165 Hz the interpolated frames either side of
 * the threshold can only show her one frame longer. */
long ModernLaraEyeDistance()
{
	const long dx = camera.pos.x - camera.target.x;
	const long dy = camera.pos.y - camera.target.y;
	const long dz = camera.pos.z - camera.target.z;

	return (long)phd_sqrt((ulong)(SQUARE(dx) + SQUARE(dy) + SQUARE(dz)));
}

/* The swimming attitude the orbit points at: the orbit's elevation, turned
 * into the body attitude it belongs to by game\modern_camera.cpp. The camera
 * owns both the number and the conversion, so game\modern_state.cpp keeps no
 * copy of either.
 *
 * It is the value the last tick's camera left, like the heading:
 * game\control.cpp runs the input before CalculateCamera, so a thirtieth of a
 * second behind the thumb. */
short ModernCameraSwimTarget()
{
	ModernCameraArm();

	return ModernCameraSwimPitch(&modern_cam_cfg, modern_cam_state.offset_pitch);
}

bool ModernLaraHidden()
{
	ModernCameraArm();

	/* First person asks the opposite question and asks it first: there the eye
	 * is inside her head, and her arms are what the player aims with. It hides
	 * her head itself and draws the rest (specific\drawlara.cpp), so this
	 * predicate must not answer while it runs. ModernCameraActive refuses first
	 * person on the camera's side; FirstPersonActive is the draw's question,
	 * and this is a draw, so both are asked. */
	if (!ModernCameraActive() || FirstPersonActive())
		return false;

	return ModernCameraHidesLara(&modern_cam_cfg, ModernLaraEyeDistance()) != 0;
}

/* The orbit's fields on MODERNLOG's tick line: raw reads of this file's
 * statics, nothing computed from them, so the log shows the curve
 * MODERN_CAMERA_RETURN and MODERN_CAMERA_WALL_FACTOR produce.
 *
 * The state is one tick behind, like camera_yaw: game\modern_state.cpp's
 * tick line runs before CalculateCamera, so offset_yaw, offset_pitch, ret,
 * world_yaw, wall and carry are what the orbit's last tick left - the reset
 * values (0, MODERN_CAMERA_PITCH_HOME, 0, 0, 0, 0) on the first line after a
 * cut. rs_x/rs_y are this tick's: specific\input.cpp calls
 * ModernCameraStick() earlier in the same S_UpdateInput.
 *
 * cam_world= is world_yaw, 0 before the first seating (ModernCameraReset);
 * clip= is state->wall, the same flag the return rate is multiplied by
 * MODERN_CAMERA_WALL_FACTOR for - "the eye was clipped this tick"; carry is
 * the swing an about-turn still owes the offset, signed, 0 when nothing is
 * paying out. */
void ModernCameraLogState(short* off_yaw, short* off_pitch, long* ret, long* rs_x, long* rs_y,
	short* world_yaw, long* wall, long* carry)
{
	ModernCameraArm();

	if (off_yaw)
		*off_yaw = modern_cam_state.offset_yaw;

	if (off_pitch)
		*off_pitch = modern_cam_state.offset_pitch;

	if (ret)
		*ret = modern_cam_state.ret;

	if (rs_x)
		*rs_x = modern_cam_rs_x;

	if (rs_y)
		*rs_y = modern_cam_rs_y;

	if (world_yaw)
		*world_yaw = modern_cam_state.world_yaw;

	if (wall)
		*wall = modern_cam_state.wall;

	if (carry)
		*carry = modern_cam_state.carry;
}

/* The orbit's fields on MODERNLOG's init line: the live cfg this file
 * seats, not the raw enum in game\modern_camera.h, so the log shows what was
 * really running. Every camera number MODERNLOG prints comes through this
 * file.
 *
 *   return_rate, return_min, wall_factor, hide_dist, invert_pitch,
 *   pitch_return   the tunable constants MODERN_CAMERA_RETURN, _RETURN_MIN,
 *                  _WALL_FACTOR, _HIDE_DIST, _INVERT_PITCH, _PITCH_RETURN
 *   speed          tomb5.camera_speed, the slider's raw 0..100
 *   yaw_rate       the rate ModernCameraSpeedRates derived from it - the two
 *                  together tell a wrong slider value from a wrong mapping
 *   pitch_min/max  the sphere's clamp
 *   pull_steps,
 *   pull_push      MODERN_CAMERA_PULL_STEPS / _PULL_PUSH, by which
 *                  ModernCameraRayBounds walks a clipped candidate back
 *                  along the ray */
void ModernCameraLogConfig(long* return_rate, long* return_min, long* wall_factor,
	long* hide_dist, long* invert_pitch, long* pitch_return,
	long* speed, long* yaw_rate, long* pitch_min, long* pitch_max,
	long* pull_steps, long* pull_push)
{
	ModernCameraArm();

	if (return_rate)
		*return_rate = modern_cam_cfg.return_rate;

	if (return_min)
		*return_min = modern_cam_cfg.return_min;

	if (wall_factor)
		*wall_factor = modern_cam_cfg.wall_factor;

	if (hide_dist)
		*hide_dist = modern_cam_cfg.hide_dist;

	if (invert_pitch)
		*invert_pitch = modern_cam_cfg.invert_pitch;

	if (pitch_return)
		*pitch_return = modern_cam_cfg.pitch_return;

	if (speed)
		*speed = tomb5.camera_speed;

	if (yaw_rate)
		*yaw_rate = modern_cam_cfg.yaw_rate;

	if (pitch_min)
		*pitch_min = modern_cam_cfg.pitch_min;

	if (pitch_max)
		*pitch_max = modern_cam_cfg.pitch_max;

	if (pull_steps)
		*pull_steps = MODERN_CAMERA_PULL_STEPS;

	if (pull_push)
		*pull_push = MODERN_CAMERA_PULL_PUSH;
}

void InitialiseCamera()
{
	// Everything the first person camera remembers is forgotten here, once
	// per level. These statics outlive a level: fpFovSet still standing from
	// the level just left would keep the filters from restarting, and
	// fpPrevEyeRoom would carry a room number from the old level into the new
	// one, where GetFloor takes it as its starting room and reads room[] out
	// of range - a crash on a level change with first person on.
	fpFovSet = 0;
	fpRanLast = 0;
	fpSmoothValid = 0;
	fpPrevEyeRoom = lara_item ? lara_item->room_number : 0;
	fpVel[0] = fpVel[1] = fpVel[2] = 0;
	fpSpd = 0;
	fpRoll = 0;

	// And the modern camera's orbit: an offset seated in the level just left
	// means nothing, and the position the "is she moving" test measures
	// against belongs to a world that has been thrown away. Unconditional:
	// nothing under Tank reads these statics, and a reset that depended on the
	// option would leave a stale orbit for a player who switches the scheme on
	// in the middle of a level.
	ModernCameraResetOrbit();

	last_target.x = lara_item->pos.x_pos;
	last_target.y = lara_item->pos.y_pos - 1024;
	last_target.z = lara_item->pos.z_pos;
	last_target.room_number = lara_item->room_number;

	camera.pos.x = last_target.x;
	camera.pos.y = last_target.y;
	camera.pos.z = last_target.z - 100;
	camera.pos.room_number = lara_item->room_number;

	camera.target.x = last_target.x;
	camera.target.y = last_target.y;
	camera.target.z = last_target.z;
	camera.target.room_number = lara_item->room_number;

	camera.target_distance = 1536;
	camera.item = 0;
	camera.number_frames = 1;
	camera.type = CHASE_CAMERA;
	camera.speed = 1;
	camera.flags = 1;
	camera.bounce = 0;
	camera.number = -1;
	camera.fixed_camera = 0;
	AlterFOV(GAME_FOV);
	UseForcedFixedCamera = 0;
	CalculateCamera();
}

// FixedCamera's own countdown, verbatim, run where FixedCamera would have
// run. It has to keep ticking whenever first person or the modern camera
// passes a level camera over: the reset at the end of CalculateCamera leaves
// a HEAVY_CAMERA alone while its timer is not -1, so a skipped heavy trigger
// camera would stay nailed for good - invisible while the other view lasts,
// and springing the moment it ends.
static void DiscardedFixedCameraTimer(long wouldFix)
{
	if (wouldFix && camera.timer)
	{
		camera.timer--;

		if (!camera.timer)
			camera.timer = -1;
	}
}

void CalculateCamera()
{
	ITEM_INFO* item;
	OBJECT_VECTOR* fixed;
	PHD_VECTOR v;
	short* bounds;
	long shift, fixed_camera, y, gotit;
	short change, tilt;
	static char TLFlag = 0;

	CamOldPos.x = camera.pos.x;
	CamOldPos.y = camera.pos.y;
	CamOldPos.z = camera.pos.z;
	SniperOverlay = 0;
	SniperCamActive = 0;

	// First person view: the head-hidden marker is reset every frame and set
	// again only when FirstPersonCamera below really runs - otherwise Lara
	// would stand headless in every fixed camera and every cutscene.
	// CalculateCamera runs before drawing, so the frame sees this pass's state.
	fpHeadHidden = 0;

	// The field of view is given back the same way: the view can end without
	// notice - the option switched off, the binoculars taking over, Lara
	// dying - so it is handed back here every frame and taken again below only
	// when the first person camera runs. The binoculars set theirs during
	// drawing, after this, and a flyby never reaches this function
	// (control.cpp calls CalculateSpotCams instead).
	//
	// fpRanLast records whether the first person camera ran last frame, which
	// is what the filters need to know. It is taken here, before the reset:
	// fpFovSet is set at the end of every first person frame, so a reset
	// inside the block below would clear the filters every frame and they
	// would never filter anything.
	fpRanLast = fpFovSet;

	if (fpFovSet)
	{
		AlterFOV(GAME_FOV);
		fpFovSet = 0;
	}

	if (BinocularRange)
	{
		BinocularOn = 1;
		BinocularCamera(lara_item);

		if (BinocularRange)
			return;
	}

	if (BinocularOn == 1)
		BinocularOn = -8;

	// A forced fixed camera is Core's cinematic hand-off: it nails the view
	// a couple of squares away and shows Lara from OUTSIDE for the length
	// of an animation. Three places use it - lifting a floor trapdoor
	// (traps.cpp), the ceiling trapdoor, and the turn switch (switch.cpp).
	// In first person each of them would throw the player out of Lara's eyes
	// and back in again, so first person overrides it.
	//
	// The fourth setter, CreatureKill (box.cpp), is dead code - TR5 has no
	// Lara animation for it and nothing calls it. Were it ever revived,
	// FirstPersonEnabled() would answer it correctly on its own: with the
	// death camera switched off it returns false for a dead Lara, and the
	// forced camera keeps its old job.
	if (UseForcedFixedCamera && !FirstPersonEnabled())
	{
		camera.type = FIXED_CAMERA;

		if (camera.old_type != FIXED_CAMERA)
			camera.speed = 1;
	}

	if (gfLevelId == LVL5_STREETS_OF_ROME && (!XATrack || XATrack == 13))
		TLFlag = 1;
	else
	{
		if (TLFlag == 1 && camera.underwater)
			camera.underwater = 0;

		TLFlag = 0;
	}

	if (gfLevelId != LVL5_DEEPSEA_DIVE)
	{
		if (room[camera.pos.room_number].flags & ROOM_UNDERWATER)
		{
			SoundEffect(SFX_UNDERWATER, 0, SFX_ALWAYS);

			if (!camera.underwater)
				camera.underwater = 1;
		}
		else if (camera.underwater)
			camera.underwater = 0;
	}

	if (camera.type == CINEMATIC_CAMERA)
	{
		do_new_cutscene_camera();
		return;
	}

	// The modern camera's discard of level cameras is decided here, before the
	// target is built. A trigger that carries a Camera and a Target command
	// makes control.cpp set camera.item, and the lines below then build the
	// whole shot around that item: its bounds, its height, its heading, and no
	// ConfirmCameraTargetPos because it is not Lara. ChaseCamera reads only
	// camera.target, so a discard decided later would orbit the door at 1536
	// units and leave Lara out of the picture.
	//
	// Twenty floor triggers in the campaign carry both commands on one tile -
	// Andrea1 4, Andrea2 1, joby2 1, rich1 4, rich2 3, rich3 7 (CamTargetPairs
	// over the player's .trc files).
	//
	// Discarded, the camera is built as if the trigger had never fired: item
	// is Lara, fixed_camera stays 0 (no SniperCount, no camera.speed of 1, no
	// fixed_camera flip for the interpolator to read as a cut), and the target
	// block below is the ordinary one, ConfirmCameraTargetPos included.
	//
	// wouldFix and sniperCam are also used by the branch further down. They
	// read camera.type, camera.flags, camera.number and camera.fixed[], none of
	// which is written in between: the only assignment is camera.type =
	// LOOK_CAMERA in the target-item block below, and that makes the branch
	// unreachable (it sits inside "camera.type != LOOK_CAMERA").
	const long wouldFix = (camera.type != CHASE_CAMERA && camera.flags != 3);
	const long sniperCam = wouldFix && camera.number >= 0 &&
							   (camera.fixed[camera.number].flags & 2);
	const long modernDiscard = ModernCameraActive() && wouldFix && !sniperCam;

	if (camera.item && (camera.type == FIXED_CAMERA || camera.type == HEAVY_CAMERA) && !modernDiscard)
	{
		item = camera.item;
		fixed_camera = 1;
	}
	else
	{
		item = lara_item;
		fixed_camera = 0;
	}

	bounds = GetBoundsAccurate(item);
	y = ((bounds[2] + bounds[3]) >> 1) + item->pos.y_pos - 256;

	// The head turn that comes with it is discarded too. This block is
	// vanilla's answer to a Target command without a Camera command: turn her
	// head at the thing and, if it is in front of her, hand the view to
	// LOOK_CAMERA. With the discard above clearing fixed_camera, a trigger that
	// carries both commands would otherwise fall in here and take the view away
	// through the look camera instead - the same cutaway. A trigger carrying
	// only a Target is untouched: modernDiscard needs wouldFix, and a Target
	// alone leaves the camera CHASE.
	if (camera.item && !fixed_camera && !modernDiscard)
	{
		shift = phd_sqrt(SQUARE(camera.item->pos.z_pos - item->pos.z_pos) + SQUARE(camera.item->pos.x_pos - item->pos.x_pos));
		gotit = (phd_atan(camera.item->pos.z_pos - item->pos.z_pos, camera.item->pos.x_pos - item->pos.x_pos) - item->pos.y_rot) >> 1;
		bounds = GetBoundsAccurate(camera.item);
		tilt = short(phd_atan(shift, y - (bounds[2] + bounds[3]) / 2 - camera.item->pos.y_pos)) >> 1;

		if (gotit > -9100 && gotit < 9100 && tilt > -15470 && tilt < 15470)
		{
			change = short(gotit - lara.head_y_rot);

			if (change > 728)
				lara.head_y_rot += 728;
			else if (change < -728)
				lara.head_y_rot -= 728;
			else
				lara.head_y_rot = (short)gotit;

			lara.torso_y_rot = lara.head_y_rot;
			change = tilt - lara.head_x_rot;

			if (change > 728)
				lara.head_x_rot += 728;
			else if (change < -728)
				lara.head_x_rot -= 728;
			else
				lara.head_x_rot = tilt;

			lara.torso_x_rot = lara.head_x_rot;
			camera.type = LOOK_CAMERA;
			camera.item->looked_at = 1;
		}
	}

	if (camera.type != LOOK_CAMERA && camera.type != COMBAT_CAMERA)
	{
		last_target.x = camera.target.x;
		last_target.y = camera.target.y;
		last_target.z = camera.target.z;
		last_target.room_number = camera.target.room_number;
		camera.target.room_number = item->room_number;
		camera.target.y = y;
		gotit = 0;

		if (camera.type != CHASE_CAMERA && camera.flags != 3)
		{
			fixed = &camera.fixed[camera.number];
			SniperCamActive = fixed->flags & 3;

			if (fixed->flags & 2)
			{
				v.x = 0;
				v.y = 0;
				v.z = 0;
				GetLaraJointPos(&v, LMX_TORSO);
				camera.target.x = v.x;
				camera.target.y = v.y;
				camera.target.z = v.z;
				gotit = 1;
			}
		}

		if (!gotit)
		{
			shift = (bounds[0] + bounds[1] + bounds[4] + bounds[5]) >> 2;
			camera.target.x = item->pos.x_pos + (shift * phd_sin(item->pos.y_rot) >> W2V_SHIFT);
			camera.target.z = item->pos.z_pos + (shift * phd_cos(item->pos.y_rot) >> W2V_SHIFT);

			if (item->object_number == LARA)
				ConfirmCameraTargetPos();
		}

		if (fixed_camera != camera.fixed_camera)
		{
			SniperCount = 30;
			camera.fixed_camera = 1;
			camera.speed = 1;
		}
		else
		{
			camera.fixed_camera = 0;

			if (camera.speed != 1 && camera.old_type != LOOK_CAMERA && BinocularOn >= 0)
			{
				if (TargetSnaps <= 8)
				{
					camera.target.x = last_target.x + ((camera.target.x - last_target.x) >> 2);
					camera.target.y = last_target.y + ((camera.target.y - last_target.y) >> 2);
					camera.target.z = last_target.z + ((camera.target.z - last_target.z) >> 2);
				}
				else
					TargetSnaps = 0;
			}
		}

		GetFloor(camera.target.x, camera.target.y, camera.target.z, &camera.target.room_number);

		if (abs(last_target.x - camera.target.x) < 4 && abs(last_target.y - camera.target.y) < 4 && abs(last_target.z - camera.target.z) < 4)
		{
			camera.target.x = last_target.x;
			camera.target.y = last_target.y;
			camera.target.z = last_target.z;
		}

		// A trigger's Camera command (TO_CAMERA, control.cpp) nails the view
		// to a camera placed in the level and shows Lara from outside. That is
		// the same yank out of her eyes as the forced fixed camera further up,
		// only here the level asks for it - and it happens a lot: every door
		// shown opening, every lever with a payoff somewhere else (Andrea1
		// triggers 7 and 31, for example). First person wins here, as it does
		// on the look/combat path below.
		//
		// Except the real sniper camera, which is bit 1 alone, not flags & 3.
		// The two bits are different things:
		//
		//   bit 1  the sniper view. FixedCamera aims it at Lara's torso and
		//          sets SniperOverlay, which draws the scope. Skipping the
		//          call would leave the player aiming through nothing.
		//          Measured with CameraCheck: four cameras in the whole game,
		//          all in rich2.
		//   bit 0  only feeds SniperCamActive, which locks the look key and
		//          the inventory (control.cpp, newinv2.cpp). No overlay, no
		//          special targeting - and it is everywhere: 54 cameras, 11 of
		//          andy1's 12 alone.
		//
		// Testing flags & 3 would spare 58 of the game's 92 cameras when four
		// need it (Andrea1 camera 4, flags 0x0000, and camera 1, flags 0x0001,
		// look identical in trview). SniperCamActive keeps its own job either
		// way - it is assigned further up and the locks stay.
		//
		// wouldFix and sniperCam are computed at the top of this function,
		// because the modern discard needs them before the target is built.

		if (FirstPersonEnabled() && !sniperCam)
		{
			FirstPersonCamera(item);
			DiscardedFixedCameraTimer(wouldFix);
		}
		else if (modernDiscard)
		{
			// The modern camera discards level cameras too, with the same
			// sniper exception: a trigger's Camera command would take the
			// orbit out of the player's hands mid-stride.
			//
			// The chase camera runs instead, which is the orbit: ChaseCamera
			// reads camera.target_angle and target_elevation, which
			// ModernCameraOrbit writes. `item` is lara_item here, because the
			// discard was decided at the top of this function.
			ChaseCamera(item);
			DiscardedFixedCameraTimer(wouldFix);
		}
		else if (wouldFix)
			FixedCamera();      // the level's direction
		else
			ChaseCamera(item);
	}
	else
	{
		if (camera.type == COMBAT_CAMERA)
		{
			last_target.x = camera.target.x;
			last_target.y = camera.target.y;
			last_target.z = camera.target.z;
			last_target.room_number = camera.target.room_number;
		}

		camera.target.room_number = item->room_number;

		if (camera.fixed_camera || BinocularOn < 0)
		{
			camera.target.y = y;
			camera.speed = 1;
		}
		else
		{
			camera.target.y += (y - camera.target.y) >> 2;
			camera.speed = camera.type != LOOK_CAMERA ? 8 : 4;
		}

		camera.fixed_camera = 0;

		if (FirstPersonEnabled())
			FirstPersonCamera(item);
		else if (camera.type == LOOK_CAMERA)
			LookCamera(item);
		else
			CombatCamera(item);
	}

	camera.fixed_camera = fixed_camera;
	camera.last = camera.number;

	if (camera.type != HEAVY_CAMERA || camera.timer == -1)
	{
		camera.type = CHASE_CAMERA;
		camera.speed = 10;
		camera.number = -1;
		camera.last_item = camera.item;
		camera.item = 0;
		camera.target_elevation = 0;
		camera.target_angle = 0;
		camera.target_distance = 1536;
		camera.flags = 0;
		camera.lara_node = -1;
	}
}

void LaraTorch(PHD_VECTOR* Soffset, PHD_VECTOR* Eoffset, short yrot, long brightness)
{
	GAME_VECTOR s, d;
	long dx, dy, dz, radius;

	s.x = Soffset->x;
	s.y = Soffset->y;
	s.z = Soffset->z;
	s.room_number = lara_item->room_number;
	d.x = Eoffset->x;
	d.y = Eoffset->y;
	d.z = Eoffset->z;
	TriggerDynamic(s.x, s.y, s.z, 12, brightness, brightness, brightness >> 1);

	if (!LOS(&s, &d))
	{
		dx = s.x - d.x;
		dy = s.y - d.y;
		dz = s.z - d.z;
		dx = phd_sqrt(SQUARE(dx) + SQUARE(dy) + SQUARE(dz));
		radius = 8 + (dx >> 8);

		if (radius > 31)
			radius = 31;

		brightness -= dx >> 8;

		if (brightness >= 0)
			TriggerDynamic(d.x, d.y, d.z, radius, brightness, brightness, brightness >> 1);
	}
}

void ScreenShake(ITEM_INFO* item, short MaxVal, short MaxDist)
{
	long dx, dy, dz;

	dx = item->pos.x_pos - camera.pos.x;
	dy = item->pos.y_pos - camera.pos.y;
	dz = item->pos.z_pos - camera.pos.z;
	dy = phd_sqrt(SQUARE(dx) + SQUARE(dy) + SQUARE(dz));

	if (dy < MaxDist)
	{
		if (MaxDist == -1)
			camera.bounce = MaxVal;
		else
			camera.bounce = -(MaxVal * (MaxDist - dy) / MaxDist);
	}
	else if (MaxDist == -1)
		camera.bounce = MaxVal;
}

long mgLOS(GAME_VECTOR* start, GAME_VECTOR* target, long push)
{
	FLOOR_INFO* floor;
	long x, y, z, h, c, cdiff, hdiff, dx, dy, dz, clipped, nc, lp;
	short room_number, room_number2;

	dx = (target->x - start->x) >> 3;
	dy = (target->y - start->y) >> 3;
	dz = (target->z - start->z) >> 3;
	x = start->x;
	y = start->y;
	z = start->z;
	room_number = start->room_number; 
	room_number2 = room_number;
	nc = 0;
	clipped = 0;

	for (lp = 0; lp < 8; lp++)
	{
		room_number = room_number2;
		floor = GetFloor(x, y, z, &room_number2);
		h = GetHeight(floor, x, y, z);
		c = GetCeiling(floor, x, y, z);

		if (h == NO_HEIGHT || c == NO_HEIGHT || c >= h)
		{
			if (!nc)
			{
				x += dx;
				y += dy;
				z += dz;
				continue;
			}

			clipped = 1;
			break;
		}

		if (y > h)
		{
			hdiff = y - h;

			if (hdiff < push)
				y = h;
			else
			{
				clipped = 1;
				break;
			}
		}

		if (y < c)
		{
			cdiff = c - y;

			if (cdiff < push)
				y = c;
			else
			{
				clipped = 1;
				break;
			}
		}

		nc = 1;

		x += dx;
		y += dy;
		z += dz;
	}

	if (lp)
	{
		x -= dx;
		y -= dy;
		z -= dz;
	}

	target->x = x;
	target->y = y;
	target->z = z;
	GetFloor(x, y, z, &room_number);
	target->room_number = room_number;
	return !clipped;
}

long CameraCollisionBounds(GAME_VECTOR* ideal, long push, long yfirst)
{
	FLOOR_INFO* floor;
	long wx, wy, wz, h, c;
	short room_number;

	wx = ideal->x;
	wy = ideal->y;
	wz = ideal->z;

	if (yfirst)
	{
		room_number = ideal->room_number;
		floor = GetFloor(wx, wy, wz, &room_number);
		h = GetHeight(floor, wx, wy, wz);
		c = GetCeiling(floor, wx, wy, wz);

		if (c > wy - 255 && h < wy + 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
			wy = (h + c) >> 1;
		else if (h < wy + 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
			wy = h - 255;
		else if (c > wy - 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
			wy = c + 255;
	}

	room_number = ideal->room_number;
	floor = GetFloor(wx - push, wy, wz, &room_number);
	h = GetHeight(floor, wx - push, wy, wz);
	c = GetCeiling(floor, wx - push, wy, wz);

	if (h < wy || h == NO_HEIGHT || c == NO_HEIGHT || c >= h || wy < c)
		wx = (wx & ~1023) + push;

	room_number = ideal->room_number;
	floor = GetFloor(wx, wy, wz - push, &room_number);
	h = GetHeight(floor, wx, wy, wz - push);
	c = GetCeiling(floor, wx, wy, wz - push);

	if (h < wy || h == NO_HEIGHT || c == NO_HEIGHT || c >= h || wy < c)
		wz = (wz & ~1023) + push;

	room_number = ideal->room_number;
	floor = GetFloor(wx + push, wy, wz, &room_number);
	h = GetHeight(floor, wx + push, wy, wz);
	c = GetCeiling(floor, wx + push, wy, wz);

	if (h < wy || h == NO_HEIGHT || c == NO_HEIGHT || c >= h || wy < c)
		wx = (wx | 1023) - push;

	room_number = ideal->room_number;
	floor = GetFloor(wx, wy, wz + push, &room_number);
	h = GetHeight(floor, wx, wy, wz + push);
	c = GetCeiling(floor, wx, wy, wz + push);

	if (h < wy || h == NO_HEIGHT || c == NO_HEIGHT || c >= h || wy < c)
		wz = (wz | 1023) - push;

	if (!yfirst)
	{
		room_number = ideal->room_number;
		floor = GetFloor(wx, wy, wz, &room_number);
		h = GetHeight(floor, wx, wy, wz);
		c = GetCeiling(floor, wx, wy, wz);

		if (c > wy - 255 && h < wy + 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
			wy = (h + c) >> 1;
		else if (h < wy + 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
			wy = h - 255;
		else if (c > wy - 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
			wy = c + 255;
	}

	room_number = ideal->room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (h < wy || wy < c || h == NO_HEIGHT || c == NO_HEIGHT || c >= h)
		return 1;

	GetFloor(wx, wy, wz, &ideal->room_number);
	ideal->x = wx;
	ideal->y = wy;
	ideal->z = wz;
	return 0;
}

/* Whether there is a room at this point at all: the five terms
 * CameraCollisionBounds asks five times over, in one place so the two
 * functions below cannot spell them differently. They are its final test, in
 * its order; its four probes ask the same five with two of them swapped. */
static long ModernCameraEyeInRoom(long wx, long wy, long wz, short room)
{
	FLOOR_INFO* floor;
	long h, c;
	short room_number;

	room_number = room;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (h < wy || wy < c || h == NO_HEIGHT || c == NO_HEIGHT || c >= h)
		return 0;

	return 1;
}

/* The half of CameraCollisionBounds the orbit keeps: the floor and the
 * ceiling, verbatim from its yfirst block, returning the y rather than
 * writing it. A ceiling may push the eye down and a floor may push it up;
 * neither moves the picture round her. */
static long ModernCameraEyeY(long wx, long wy, long wz, short room)
{
	FLOOR_INFO* floor;
	long h, c;
	short room_number;

	room_number = room;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (c > wy - 255 && h < wy + 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
		return (h + c) >> 1;

	if (h < wy + 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
		return h - 255;

	if (c > wy - 255 && c < h && c != NO_HEIGHT && h != NO_HEIGHT)
		return c + 255;

	return wy;
}

/* Brings a clipped eye in along the ray, never round her - so that the
 * picture, the stick and her movement stay in step next to a wall.
 *
 * Both orbit cameras call CameraCollisionBounds with push 384 and yfirst 1:
 *
 *   the y against the floor and the ceiling at the point
 *   (wx - 384) has no room  ->  wx = (wx & ~1023) + 384
 *   (wz - 384) has no room  ->  wz = (wz & ~1023) + 384
 *   (wx + 384) has no room  ->  wx = (wx | 1023) - 384
 *   (wz + 384) has no room  ->  wz = (wz | 1023) - 384
 *   the point itself has no room -> return 1, ideal untouched
 *
 * The four middle branches do not push along anything: each destination is
 * the near or far edge of the point's sector plus or minus the push, so a
 * point at the wrong edge moves 1023 - 384 = 639 units sideways, x and z can
 * both fire, and the +x probe reads the wx the -x probe already moved. At the
 * orbit's radius of 1536 that is up to 23 degrees an axis; at the 200 to 400
 * units a wall leaves it is any angle. Measured: 481 of 930 pushed ticks had
 * the picture and the movement reference more than 60 degrees apart, every
 * one on a tick mgLOS had clipped, median 124 degrees at 200 to 400 units
 * and 146 under 200.
 *
 * So in Modern the sideways half goes. mgLOS has already put the eye on the
 * ray at the dialled angle; this walks it further in along the same ray, an
 * eighth at a time, until the point has the clearance the four probes ask
 * for, and keeps the y correction. A floor or a ceiling may move the eye; a
 * wall may only bring it closer.
 *
 * If nothing on the ray clears - her back to a wall - the mgLOS point stands,
 * with its y correction: mgLOS's push of 200 kept it inside a room, and
 * MODERN_CAMERA_HIDE_DIST hides her long before the eye is inside her.
 *
 * The eye never ends on her own axis. mgLOS returns the target itself when
 * the first of its eight steps is already in the wall (`if (lp)` undoes the
 * only step there was); measured: 22 pushed ticks under 200 units, median
 * error 146 degrees. Such an eye has no direction, and ModernCameraLeadEye's
 * `h` guard would refuse to turn it. The ray then answers with
 * MODERN_CAMERA_HIDE_DIST (122, see game\modern_camera.h) along the dialled
 * candidate `want`, passed in unclipped for that. Her collision radius is
 * 102, so that point is at most 20 units past a wall face, and the near plane
 * is 20.
 *
 * The return value keeps CameraCollisionBounds' contract - 1 means "no point
 * was usable, ideal is untouched" - although neither call site reads it. */
static long ModernCameraRayBounds(GAME_VECTOR* ideal, const GAME_VECTOR* want, long push)
{
	long bx, by, bz, wx, wy, wz, step, dist;
	short room;

	bx = ideal->x;
	by = ideal->y;
	bz = ideal->z;
	room = ideal->room_number;

	if (bx == camera.target.x && by == camera.target.y && bz == camera.target.z)
	{
		wx = want->x - camera.target.x;
		wy = want->y - camera.target.y;
		wz = want->z - camera.target.z;
		dist = (long)phd_sqrt((ulong)(SQUARE(wx) + SQUARE(wy) + SQUARE(wz)));

		if (dist > MODERN_CAMERA_HIDE_DIST)
		{
			wx = wx * MODERN_CAMERA_HIDE_DIST / dist;
			wy = wy * MODERN_CAMERA_HIDE_DIST / dist;
			wz = wz * MODERN_CAMERA_HIDE_DIST / dist;
		}

		bx = camera.target.x + wx;
		by = camera.target.y + wy;
		bz = camera.target.z + wz;
	}

	for (step = MODERN_CAMERA_PULL_STEPS; step > 0; step--)
	{
		wx = ModernCameraPullAxis(camera.target.x, bx, step, MODERN_CAMERA_PULL_STEPS);
		wy = ModernCameraPullAxis(camera.target.y, by, step, MODERN_CAMERA_PULL_STEPS);
		wz = ModernCameraPullAxis(camera.target.z, bz, step, MODERN_CAMERA_PULL_STEPS);

		// A fraction of a very short ray truncates onto her own axis, and that
		// is the one point this function may never hand back - see above.
		if (wx == camera.target.x && wy == camera.target.y && wz == camera.target.z)
			continue;

		wy = ModernCameraEyeY(wx, wy, wz, room);

		if (!ModernCameraEyeInRoom(wx - push, wy, wz, room) ||
			!ModernCameraEyeInRoom(wx + push, wy, wz, room) ||
			!ModernCameraEyeInRoom(wx, wy, wz - push, room) ||
			!ModernCameraEyeInRoom(wx, wy, wz + push, room))
			continue;

		if (!ModernCameraEyeInRoom(wx, wy, wz, room))
			continue;

		GetFloor(wx, wy, wz, &ideal->room_number);
		ideal->x = wx;
		ideal->y = wy;
		ideal->z = wz;
		return 0;
	}

	wx = bx;
	wz = bz;
	wy = ModernCameraEyeY(bx, by, bz, room);

	if (!ModernCameraEyeInRoom(wx, wy, wz, room))
		return 1;

	GetFloor(wx, wy, wz, &ideal->room_number);
	ideal->x = wx;
	ideal->y = wy;
	ideal->z = wz;
	return 0;
}

void UpdateCameraElevation()
{
	PHD_VECTOR pos;
	PHD_VECTOR pos1;

	/* In Modern the angle is measured against her heading and nothing else.
	 *
	 * camera.lara_node is the classic camera's way of following an animation
	 * instead of the item: a roll, a corner climb, a pushable block and the
	 * climb out of water set it (game\lara.cpp) and CalculateCamera clears it at
	 * the end of the tick. While it is set the line below adds the direction of
	 * that joint to camera.target_angle instead of her y_rot, so the view swings
	 * with the animation. The orbit keeps a world angle and hands over world
	 * minus her heading, so the engine must add her heading back, or the camera
	 * stands somewhere else for as long as that animation runs. */
	if (camera.lara_node != -1 && !ModernCameraActive())
	{
		pos.x = 0;
		pos.y = 0;
		pos.z = 0;
		GetLaraJointPos(&pos, camera.lara_node);

		pos1.x = 0;
		pos1.y = -256;
		pos1.z = 2048;
		GetLaraJointPos(&pos1, camera.lara_node);

		pos.z = pos1.z - pos.z;
		pos.x = pos1.x - pos.x;
		camera.actual_angle = short(camera.target_angle + phd_atan(pos.z, pos.x));
	}
	else
		camera.actual_angle = lara_item->pos.y_rot + camera.target_angle;

	camera.actual_elevation += (camera.target_elevation - camera.actual_elevation) >> 3;
}

void ConfirmCameraTargetPos()
{
	FLOOR_INFO* floor;
	PHD_VECTOR pos;
	long wx, wy, wz, c, h;
	short room_number;

	pos.z = 0;
	pos.y = 0;
	pos.x = 0;
	GetJointAbsPosition(lara_item, &pos, LM_HEAD);

	if (camera.lara_node != -1)
	{
		camera.target.x = pos.x;
		camera.target.y = pos.y;
		camera.target.z = pos.z;
	}
	else
	{
		camera.target.x = lara_item->pos.x_pos;
		camera.target.y = (camera.target.y + pos.y) >> 1;
		camera.target.z = lara_item->pos.z_pos;
	}

	wx = camera.target.x;
	wy = camera.target.y;
	wz = camera.target.z;
	room_number = camera.target.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (wy < c || h < wy || h <= c || h == NO_HEIGHT || c == NO_HEIGHT)
	{
		camera.target.x = pos.x;
		camera.target.y = pos.y;
		camera.target.z = pos.z;
	}
}

void FixedCamera()
{
	MESH_INFO* StaticMesh;
	OBJECT_VECTOR* fixed;
	GAME_VECTOR ideal;
	GAME_VECTOR t2;
	PHD_VECTOR v;
	long Target, ricochet, HitLara;

	if (UseForcedFixedCamera)
	{
		ideal.x = ForcedFixedCamera.x;
		ideal.y = ForcedFixedCamera.y;
		ideal.z = ForcedFixedCamera.z;
		ideal.room_number = ForcedFixedCamera.room_number;
	}
	else
	{
		fixed = &camera.fixed[camera.number];
		ideal.x = fixed->x;
		ideal.y = fixed->y;
		ideal.z = fixed->z;
		ideal.room_number = fixed->data;

		if (fixed->flags & 2)	//sniper
		{
			if (FlashFader > 2)
				FlashFader = (FlashFader >> 1) & ~1;

			SniperOverlay = 1;
			camera.target.x = (camera.target.x + 2 * last_target.x) / 3;
			camera.target.y = (camera.target.y + 2 * last_target.y) / 3;
			camera.target.z = (camera.target.z + 2 * last_target.z) / 3;

			if (SniperCount)
				SniperCount--;
			else
			{
				t2.x = camera.target.x + ((camera.target.x - ideal.x) >> 1);
				t2.y = camera.target.y + ((camera.target.y - ideal.y) >> 1);
				t2.z = camera.target.z + ((camera.target.z - ideal.z) >> 1);
				ricochet = LOS(&ideal, &t2);
				GetLaraOnLOS = 1;
				Target = ObjectOnLOS2(&ideal, &t2, &v, &StaticMesh);
				HitLara = Target != 999 && Target >= 0 && items[Target].object_number == LARA;

				if (!(GetRandomControl() & 0x3F) || !(GlobalCounter & 0x3F) || HitLara && !(GlobalCounter & 0xF) && GetRandomControl() & 1)
				{
					SoundEffect(SFX_EXPLOSION1, 0, SFX_SETPITCH | 0x5000000);
					SoundEffect(SFX_HK_FIRE, 0, 0);
					FlashFadeR = 192;
					FlashFadeG = (GetRandomControl() & 0x1F) + 160;
					FlashFadeB = 0;
					FlashFader = 24;
					SniperCount = 15;

					if (HitLara && GetRandomControl() & 3)
					{
						DoBloodSplat(v.x, v.y, v.z, (GetRandomControl() & 3) + 3, short(GetRandomControl() << 1), lara_item->room_number);
						lara_item->hit_points -= 100;
					}
					else if (Target < 0)
					{
						if (StaticMesh->static_number >= 50 && StaticMesh->static_number < 58)
						{
							ShatterObject(0, StaticMesh, 128, t2.room_number, 0);
							SmashedMeshRoom[SmashedMeshCount] = t2.room_number;
							SmashedMesh[SmashedMeshCount++] = StaticMesh;
							StaticMesh->Flags &= ~1;
							SoundEffect(ShatterSounds[gfLevelId][StaticMesh->static_number - 50], (PHD_3DPOS*)&StaticMesh->x, 0);
						}

						TriggerRicochetSpark((GAME_VECTOR*)&v, GetRandomControl() << 1, 3, 0);
						TriggerRicochetSpark((GAME_VECTOR*)&v, GetRandomControl() << 1, 3, 0);
					}
					else if (!ricochet)
						TriggerRicochetSpark(&t2, GetRandomControl() << 1, 3, 0);
				}

				GetLaraOnLOS = 0;
			}
		}
	}

	camera.fixed_camera = 1;
	MoveCamera(&ideal, 1);

	if (camera.timer)
	{
		camera.timer--;

		if (!camera.timer)
			camera.timer = -1;
	}
}

void ChaseCamera(ITEM_INFO* item)
{
	FLOOR_INFO* floor;
	GAME_VECTOR ideal;
	GAME_VECTOR ideals[5];
	GAME_VECTOR temp[2];
	GAME_VECTOR want;	// the dialled candidate, kept unclipped for ModernCameraRayBounds
	long distance, dx, dz, farthest, farthestnum, h, c, wx, wy, wz, angle, lp;
	long modern_orbit;
	short room_number;

	// The orbit, or the classic camera's own two lines. Under the option the
	// camera height is the player's and comes out of the orbit's state; the
	// two classic lines are replaced rather than added to (ModernCameraOrbit
	// says why: the second accumulates her slope pitch, the first reads 0 as
	// "unset").
	//
	// Asked once and kept: three places below need the answer, and this
	// function touches none of what the predicate reads (the option,
	// lara_item, the level id, the spotcam and first person).
	modern_orbit = ModernCameraActive() ? 1 : 0;

	if (modern_orbit)
		ModernCameraOrbit();
	else
	{
		if (!camera.target_elevation)
			camera.target_elevation = -1820;

		camera.target_elevation += item->pos.x_rot;
	}

	UpdateCameraElevation();

	if (camera.actual_elevation > 15470)
		camera.actual_elevation = 15470;
	else if (camera.actual_elevation < -15470)
		camera.actual_elevation = -15470;

	distance = camera.target_distance * phd_cos(camera.actual_elevation) >> W2V_SHIFT;
	wx = camera.target.x;
	wy = camera.target.y;
	wz = camera.target.z;
	GetFloor(wx, wy, wz, &camera.target.room_number);
	room_number = camera.target.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (wy < c || wy > h || c >= h || h == NO_HEIGHT || c == NO_HEIGHT)
	{
		TargetSnaps++;
		camera.target.x = last_target.x;
		camera.target.y = last_target.y;
		camera.target.z = last_target.z;
		camera.target.room_number = last_target.room_number;
	}
	else
		TargetSnaps = 0;

	for (lp = 0; lp < 5; lp++)
		ideals[lp].y = camera.target.y + (camera.target_distance * phd_sin(camera.actual_elevation) >> W2V_SHIFT);

	farthest = 0x7FFFFFFF;
	farthestnum = 0;

	for (lp = 0; lp < 5; lp++)
	{
		if (lp)
			angle = (lp - 1) * 0x4000;
		else
			angle = camera.actual_angle;

		ideals[lp].x = camera.target.x - (distance * phd_sin(angle) >> W2V_SHIFT);
		ideals[lp].z = camera.target.z - (distance * phd_cos(angle) >> W2V_SHIFT);
		ideals[lp].room_number = camera.target.room_number;

		/* In Modern the angle stays and the distance goes, so the camera slides
		 * along a wall and the player can keep turning around Lara.
		 *
		 * The four candidates this skips are the four world cardinals: a
		 * blocked angle is answered with a quadrant, so the view snaps to north,
		 * east, south or west and stays there while the stick pushes against it
		 * - measured: 290 of 855 fully pushed ticks did not advance or ran
		 * backwards, in 14 episodes of up to 37 ticks.
		 *
		 * mgLOS walks the line from her to the wanted eye position in eight
		 * steps and leaves the last step still in the room, so a wall does not
		 * move the eye sideways, it brings it in: the distance shrinks to a
		 * close-up, the hide predicate (MODERN_CAMERA_HIDE_DIST) takes Lara out,
		 * and the eye goes back out to full distance past the wall.
		 *
		 * Its return value is the wall: "the eye was clipped this tick", the
		 * signal the orbit reads. The stamp goes with it because the orbit reads
		 * the flag a tick later and may not run for many ticks in between
		 * (ModernCameraOrbit). */
		if (!lp && modern_orbit)
		{
			// The dialled point, before mgLOS cuts it back: ModernCameraRayBounds
			// needs a direction for a ray with nothing on it, and after the
			// call below there is none left in ideals[0].
			want.x = ideals[lp].x;
			want.y = ideals[lp].y;
			want.z = ideals[lp].z;
			want.room_number = ideals[lp].room_number;
			modern_cam_clipped = mgLOS(&camera.target, &ideals[lp], 200) ? 0 : 1;
			modern_cam_clipped_at = GlobalCounter;
			farthestnum = 0;
			break;
		}

		if (mgLOS(&camera.target, &ideals[lp], 200))
		{
			temp[0].x = ideals[lp].x;
			temp[0].y = ideals[lp].y;
			temp[0].z = ideals[lp].z;
			temp[0].room_number = ideals[lp].room_number;
			temp[1].x = camera.pos.x;
			temp[1].y = camera.pos.y;
			temp[1].z = camera.pos.z;
			temp[1].room_number = camera.pos.room_number;

			if (mgLOS(&temp[0], &temp[1], 0) || !lp)
			{
				if (!lp)
				{
					farthestnum = 0;
					break;
				}

				dx = SQUARE(camera.pos.x - ideals[lp].x);
				dz = SQUARE(camera.pos.z - ideals[lp].z);
				dz += dx;

				if (dz < farthest)
				{
					farthest = dz;
					farthestnum = lp;
				}
			}
		}
		else if (!lp)
		{
			temp[0].x = ideals[lp].x;
			temp[0].y = ideals[lp].y;
			temp[0].z = ideals[lp].z;
			temp[0].room_number = ideals[lp].room_number;
			temp[1].x = camera.pos.x;
			temp[1].y = camera.pos.y;
			temp[1].z = camera.pos.z;
			temp[1].room_number = camera.pos.room_number;

			if (mgLOS(&temp[0], &temp[1], 0))
			{
				dx = SQUARE(camera.target.x - ideals[lp].x);
				dz = SQUARE(camera.target.z - ideals[lp].z);
				dz += dx;

				if (dz > 0x90000)
				{
					farthestnum = 0;
					break;
				}
			}
		}
	}

	ideal.x = ideals[farthestnum].x;
	ideal.y = ideals[farthestnum].y;
	ideal.z = ideals[farthestnum].z;
	ideal.room_number = ideals[farthestnum].room_number;

	// In Modern the eye is resolved along the ray: the same clearance and y
	// correction, but the answer never moves the eye round her
	// (ModernCameraRayBounds).
	if (modern_orbit)
		ModernCameraRayBounds(&ideal, &want, MODERN_CAMERA_PULL_PUSH);
	else
		CameraCollisionBounds(&ideal, 384, 1);

	// The stick's own step, handed to the eye: after the resolution, so none
	// of it is changed, and immediately before MoveCamera, whose damping it
	// bypasses.
	if (modern_orbit)
		ModernCameraLeadEye();

	if (camera.old_type == FIXED_CAMERA)
		camera.speed = 1;

	MoveCamera(&ideal, camera.speed);
}

void CombatCamera(ITEM_INFO* item)
{
	FLOOR_INFO* floor;
	GAME_VECTOR ideal;
	GAME_VECTOR ideals[9];
	GAME_VECTOR temp[2];
	GAME_VECTOR want;	// the dialled candidate, kept unclipped for ModernCameraRayBounds
	long distance, dx, dz, farthest, farthestnum, h, c, wx, wy, wz, angle, lp;
	long modern_orbit;
	short room_number;

	camera.target.x = item->pos.x_pos;
	camera.target.z = item->pos.z_pos;

	/* The orbit runs here too, because the chase camera is not what runs with
	 * a weapon out: game\larafire.cpp sets camera.type = COMBAT_CAMERA on every
	 * tick a gun is ready (LG_READY) and while she draws (LG_DRAW_GUNS). An
	 * orbit in ChaseCamera alone would stop the moment she armed herself, and
	 * the classic lines below would aim the camera down her gun barrel. First
	 * person overrides this camera for the same reason.
	 *
	 * Only the two angles change. Her own position as the target, the ceiling
	 * squeeze that may still zero the elevation below and camera.speed 8 stay
	 * the combat camera's own, so a weapon out still feels a little closer and
	 * quicker than a weapon away. */
	modern_orbit = ModernCameraActive() ? 1 : 0;	// once, as in ChaseCamera and for the same reason

	if (modern_orbit)
		ModernCameraOrbit();
	else if (lara.target)
	{
		camera.target_angle = lara.target_angles[0];
		camera.target_elevation = lara.target_angles[1] + item->pos.x_rot;
	}
	else
	{
		camera.target_angle = lara.head_y_rot + lara.torso_y_rot;
		camera.target_elevation = lara.head_x_rot + lara.torso_x_rot + item->pos.x_rot - 2730;
	}

	wx = camera.target.x;
	wy = camera.target.y;
	wz = camera.target.z;
	floor = GetFloor(wx, wy, wz, &camera.target.room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (c + 64 > h - 64 && h != NO_HEIGHT && c != NO_HEIGHT)
	{
		camera.target.y = (c + h) >> 1;
		camera.target_elevation = 0;
	}
	else if (camera.target.y > h - 64 && h != NO_HEIGHT)
	{
		camera.target.y = h - 64;
		camera.target_elevation = 0;
	}
	else if (camera.target.y < c + 64 && c != NO_HEIGHT)
	{
		camera.target.y = c + 64;
		camera.target_elevation = 0;
	}

	wx = camera.target.x;
	wy = camera.target.y;
	wz = camera.target.z;
	GetFloor(wx, wy, wz, &camera.target.room_number);
	room_number = camera.target.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (wy < c || wy > h || c >= h || h == NO_HEIGHT || c == NO_HEIGHT)
	{
		TargetSnaps++;
		camera.target.x = last_target.x;
		camera.target.y = last_target.y;
		camera.target.z = last_target.z;
		camera.target.room_number = last_target.room_number;
	}
	else
		TargetSnaps = 0;

	UpdateCameraElevation();
	camera.target_distance = 1536;
	distance = camera.target_distance * phd_cos(camera.actual_elevation) >> W2V_SHIFT;

	for (lp = 0; lp < 5; lp++)
		ideals[lp].y = camera.target.y + (camera.target_distance * phd_sin(camera.actual_elevation) >> W2V_SHIFT);

	farthest = 0x7FFFFFFF;
	farthestnum = 0;

	for (lp = 0; lp < 5; lp++)
	{
		if (lp)
			angle = (lp - 1) * 0x4000;
		else
			angle = camera.actual_angle;

		ideals[lp].x = camera.target.x - (distance * phd_sin(angle) >> W2V_SHIFT);
		ideals[lp].z = camera.target.z - (distance * phd_cos(angle) >> W2V_SHIFT);
		ideals[lp].room_number = camera.target.room_number;

		// The same wall, resolved the same way: this loop and ChaseCamera's are
		// the same five candidates and the same collision, so a weapon out does
		// not mean a different camera at a wall. ChaseCamera explains it.
		if (!lp && modern_orbit)
		{
			want.x = ideals[lp].x;
			want.y = ideals[lp].y;
			want.z = ideals[lp].z;
			want.room_number = ideals[lp].room_number;
			modern_cam_clipped = mgLOS(&camera.target, &ideals[lp], 200) ? 0 : 1;
			modern_cam_clipped_at = GlobalCounter;
			farthestnum = 0;
			break;
		}

		if (mgLOS(&camera.target, &ideals[lp], 200))
		{
			temp[0].x = ideals[lp].x;
			temp[0].y = ideals[lp].y;
			temp[0].z = ideals[lp].z;
			temp[0].room_number = ideals[lp].room_number;
			temp[1].x = camera.pos.x;
			temp[1].y = camera.pos.y;
			temp[1].z = camera.pos.z;
			temp[1].room_number = camera.pos.room_number;

			if (mgLOS(&temp[0], &temp[1], 0) || !lp)
			{
				if (!lp)
				{
					farthestnum = 0;
					break;
				}

				dx = SQUARE(camera.pos.x - ideals[lp].x);
				dz = SQUARE(camera.pos.z - ideals[lp].z);
				dz += dx;

				if (dz < farthest)
				{
					farthest = dz;
					farthestnum = lp;
				}
			}
		}
		else if (!lp)
		{
			temp[0].x = ideals[lp].x;
			temp[0].y = ideals[lp].y;
			temp[0].z = ideals[lp].z;
			temp[0].room_number = ideals[lp].room_number;
			temp[1].x = camera.pos.x;
			temp[1].y = camera.pos.y;
			temp[1].z = camera.pos.z;
			temp[1].room_number = camera.pos.room_number;

			if (mgLOS(&temp[0], &temp[1], 0))
			{
				dx = SQUARE(camera.target.x - ideals[lp].x);
				dz = SQUARE(camera.target.z - ideals[lp].z);
				dz += dx;

				if (dz > 0x90000)
				{
					farthestnum = 0;
					break;
				}
			}
		}
	}

	ideal.x = ideals[farthestnum].x;
	ideal.y = ideals[farthestnum].y;
	ideal.z = ideals[farthestnum].z;
	ideal.room_number = ideals[farthestnum].room_number;

	// And the same ray, for the same reason.
	if (modern_orbit)
		ModernCameraRayBounds(&ideal, &want, MODERN_CAMERA_PULL_PUSH);
	else
		CameraCollisionBounds(&ideal, 384, 1);

	// And the same lead. The damping here is camera.speed 8 rather than 10,
	// which without the lead still passes only 12.0 per cent of the stick's
	// step on the first tick.
	if (modern_orbit)
		ModernCameraLeadEye();

	if (camera.old_type == FIXED_CAMERA)
		camera.speed = 1;

	MoveCamera(&ideal, camera.speed);
}

void MoveCamera(GAME_VECTOR* ideal, long speed)
{
	FLOOR_INFO* floor;
	GAME_VECTOR temp1;
	GAME_VECTOR temp2;
	long height, ceiling, shake, rndval, wx, wy, wz, dx, dy, dz;
	short room_number;

	if (BinocularOn < 0)
	{
		speed = 1;
		BinocularOn++;
	}

	if (old_cam.pos.x_rot == lara_item->pos.x_rot && old_cam.pos.y_rot == lara_item->pos.y_rot && old_cam.pos.z_rot == lara_item->pos.z_rot &&
		old_cam.pos2.x_rot == lara.head_x_rot && old_cam.pos2.y_rot == lara.head_y_rot && old_cam.pos2.x_pos == lara.torso_x_rot &&
		old_cam.pos2.y_pos == lara.torso_y_rot && old_cam.pos.x_pos == lara_item->pos.x_pos && old_cam.pos.y_pos == lara_item->pos.y_pos &&
		old_cam.pos.z_pos == lara_item->pos.z_pos && old_cam.current_anim_state == lara_item->current_anim_state &&
		old_cam.goal_anim_state == lara_item->goal_anim_state && old_cam.target_distance == camera.target_distance &&
		old_cam.target_elevation == camera.target_elevation && old_cam.actual_elevation == camera.actual_elevation &&
		old_cam.target_angle == camera.actual_angle && old_cam.t.x == camera.target.x && old_cam.t.y == camera.target.y &&
		old_cam.t.z == camera.target.z && camera.old_type == camera.type && !SniperOverlay && BinocularOn >= 0)
	{
		ideal->x = last_ideal.x;
		ideal->y = last_ideal.y;
		ideal->z = last_ideal.z;
		ideal->room_number = last_ideal.room_number;
	}
	else
	{
		old_cam.pos.x_rot = lara_item->pos.x_rot;
		old_cam.pos.y_rot = lara_item->pos.y_rot;
		old_cam.pos.z_rot = lara_item->pos.z_rot;
		old_cam.pos2.x_rot = lara.head_x_rot;
		old_cam.pos2.y_rot = lara.head_y_rot;
		old_cam.pos2.x_pos = lara.torso_x_rot;
		old_cam.pos2.y_pos = lara.torso_y_rot;
		old_cam.pos.x_pos = lara_item->pos.x_pos;
		old_cam.pos.y_pos = lara_item->pos.y_pos;
		old_cam.pos.z_pos = lara_item->pos.z_pos;
		old_cam.current_anim_state = lara_item->current_anim_state;
		old_cam.goal_anim_state = lara_item->goal_anim_state;
		old_cam.target_distance = camera.target_distance;
		old_cam.target_elevation = camera.target_elevation;
		old_cam.actual_elevation = camera.actual_elevation;
		old_cam.target_angle = camera.actual_angle;
		old_cam.t.x = camera.target.x;
		old_cam.t.y = camera.target.y;
		old_cam.t.z = camera.target.z;
		last_ideal.x = ideal->x;
		last_ideal.y = ideal->y;
		last_ideal.z = ideal->z;
		last_ideal.room_number = ideal->room_number;
	}

	camera.pos.x += (ideal->x - camera.pos.x) / speed;
	camera.pos.y += (ideal->y - camera.pos.y) / speed;
	camera.pos.z += (ideal->z - camera.pos.z) / speed;
	camera.pos.room_number = ideal->room_number;

	if (camera.bounce)
	{
		if (camera.bounce <= 0)
		{
			rndval = -camera.bounce;
			shake = rndval >> 1;
			camera.target.x += GetRandomControl() % rndval - shake;
			camera.target.y += GetRandomControl() % rndval - shake;
			camera.target.z += GetRandomControl() % rndval - shake;
			camera.bounce += 5;
		}
		else
		{
			camera.pos.y += camera.bounce;
			camera.target.y += camera.bounce;
			camera.bounce = 0;
		}
	}

	wz = camera.pos.z;
	wy = camera.pos.y;
	wx = camera.pos.x;
	room_number = camera.pos.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	height = GetHeight(floor, wx, wy, wz);
	ceiling = GetCeiling(floor, wx, wy, wz);

	if (wy < ceiling || wy > height)
	{
		mgLOS(&camera.target, &camera.pos, 0);
		dx = abs(camera.pos.x - ideal->x);
		dy = abs(camera.pos.y - ideal->y);
		dz = abs(camera.pos.z - ideal->z);

		if (dx < 768 && dy < 768 && dz < 768)
		{
			temp1.x = camera.pos.x;
			temp1.y = camera.pos.y;
			temp1.z = camera.pos.z;
			temp1.room_number = camera.pos.room_number;
			temp2.x = ideal->x;
			temp2.y = ideal->y;
			temp2.z = ideal->z;
			temp2.room_number = ideal->room_number;

			if (!mgLOS(&temp2, &temp1, 0))
			{
				CameraSnaps++;

				if (CameraSnaps >= 8)
				{
					camera.pos.x = ideal->x;
					camera.pos.y = ideal->y;
					camera.pos.z = ideal->z;
					camera.pos.room_number = ideal->room_number;
					CameraSnaps = 0;
				}
			}
		}
	}

	wz = camera.pos.z;
	wy = camera.pos.y;
	wx = camera.pos.x;
	room_number = camera.pos.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	height = GetHeight(floor, wx, wy, wz);
	ceiling = GetCeiling(floor, wx, wy, wz);

	if (wy - 255 < ceiling && height < wy + 255 && ceiling < height && ceiling != NO_HEIGHT && height != NO_HEIGHT)
		camera.pos.y = (height + ceiling) >> 1;
	else if (height < wy + 255 && ceiling < height && ceiling != NO_HEIGHT && height != NO_HEIGHT)
		camera.pos.y = height - 255;
	else if (wy - 255 < ceiling && ceiling < height && ceiling != NO_HEIGHT && height != NO_HEIGHT)
		camera.pos.y = ceiling + 255;
	else if (ceiling >= height || height == NO_HEIGHT || ceiling == NO_HEIGHT)
	{
		camera.pos.x = ideal->x;
		camera.pos.y = ideal->y;
		camera.pos.z = ideal->z;
		camera.pos.room_number = ideal->room_number;
	}

	if (gfLevelId == LVL5_THIRTEENTH_FLOOR || gfLevelId == LVL5_ESCAPE_WITH_THE_IRIS)
		CheckForRichesIllegalDiagonalWalls();

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &camera.pos.room_number);
	phd_LookAt(camera.pos.x, camera.pos.y, camera.pos.z, camera.target.x, camera.target.y, camera.target.z, 0);
	camera.old_type = camera.type;
}

/*
 * First person view: the camera hangs on Lara's head instead of behind her.
 *
 * The template is BinocularCamera right below - the binoculars are the same
 * idea. Three differences:
 *
 *  - Lara stays movable. The binoculars set lara.Busy and freeze her; here
 *    only the view is replaced, the controls stay as they are.
 *  - Only the head disappears, not the whole body: the binoculars set
 *    mesh_bits = 0 and hide everything, but then the frame lacks the arms
 *    she grabs and shoots with.
 *  - The eye point comes from the real head joint (GetLaraJointPos) instead
 *    of a fixed offset, so it follows crouching, crawling, swimming and
 *    every animation on its own.
 */
bool FirstPersonActive()
{
	// Flybys show Lara - her head must be there.
	//
	// They are the one case where the marker alone does not suffice: during
	// a flyby control.cpp calls CalculateSpotCams instead of CalculateCamera,
	// so the reset there never runs and the marker would keep the last normal
	// frame's state - Lara headless in exactly the cameras that present her.
	// Cutscenes do go through CalculateCamera (CINEMATIC_CAMERA branch,
	// behind the reset), but stand here for clarity.
	if (bUseSpotCam || GLOBAL_playing_cutseq)
		return false;

	return fpHeadHidden != 0;
}

/* The head's own roll, for the frame interpolator.
 *
 * fpRoll is worked out from the head bone's up vector once per logic tick,
 * inside FirstPersonCamera, and the filters that produce it (fpVel, fpSpd,
 * fpRel) are tuned as per-tick exponentials and must not be moved or run
 * twice. The interpolator therefore leaves the camera alone and blends only
 * its output - camera.pos, camera.target and this - between two ticks. It is
 * a short and it wraps, so it is blended the short way round.
 *
 * A getter and not an extern, because everything that writes fpRoll lives in
 * this file. */
short GetFirstPersonRoll()
{
	return fpRoll;
}

static bool FirstPersonEnabled()
{
	if (!tomb5.first_person)
		return false;

	/* The view belongs to the played game only. FirstPersonActive() - what
	 * the draw asks - refuses a flyby and a cutscene, because those show Lara
	 * and she must have a head in them; this is whether the camera itself runs.
	 *
	 * The title needs its own test. DoTitle calls InitialiseCamera, which ends
	 * in CalculateCamera while bUseSpotCam is still 0 - the flyby is triggered
	 * two lines later. One pass of FirstPersonCamera there would move
	 * camera.pos onto the title Lara's head, take the field of view and leave
	 * fpFovSet and fpHeadHidden standing, and nothing would clear them: from
	 * the next frame on the title is a flyby, and control.cpp calls
	 * CalculateSpotCams instead of CalculateCamera, the one place either flag
	 * is handed back.
	 *
	 * gfLevelId is the title's marker (DoTitle sets it first of all), and the
	 * other two are the pair FirstPersonActive tests, so the camera and the
	 * draw answer the same question. */
	if (gfLevelId == LVL5_TITLE || bUseSpotCam || GLOBAL_playing_cutseq)
		return false;

	// Binoculars and laser sight bring their OWN head camera, including
	// their own exit path (ExittingBinos, mesh_bits restore). Interfering
	// in between would break both.
	//
	// Only while they really run (BinocularRange). The tail afterwards
	// (BinocularOn < 0, eight frames) is the normal camera's approach; a
	// nailed-on camera does not need one, and refusing during it would show
	// Lara from outside for a frame or two after leaving the scope.
	if (BinocularRange)
		return false;

	if (!lara_item)
		return false;

	// On death the camera pulls back by default and shows what happened -
	// from Lara's own eyes her death is a pan into nothing. Staying inside
	// is an option ("Death camera" on the first person page); off keeps the
	// vanilla behaviour.
	if (lara_item->hit_points <= 0 && !tomb5.first_person_death)
		return false;

	return true;
}

void FirstPersonCamera(ITEM_INFO* item)
{
	PHD_VECTOR head;
	GAME_VECTOR eye, dest;
	long dist, rndval, shake;
	short yrot, xrot;

	// Start the filters afresh when the view was NOT running last frame.
	// Carrying them across a break - a cutscene, the binoculars, another
	// room - would sweep the view over from wherever it stood a moment ago
	// instead of simply being where it belongs.
	if (!fpRanLast)
	{
		fpSmoothValid = 0;
		fpPrevEyeRoom = item->room_number;
		fpVel[0] = fpVel[1] = fpVel[2] = 0;
		fpSpd = 0;
	}

	// Count down the binoculars' tail. Normally MoveCamera does that - and
	// it never runs here. Without this, BinocularOn would stay negative
	// forever, and everything hanging on it (camera speed, the old_cam
	// comparison) would stay skewed for the rest of the level.
	if (BinocularOn < 0)
		BinocularOn++;

	// View direction: body rotation plus head rotation. The stops are the
	// binoculars' - that far and no further does Lara turn her neck.
	xrot = lara.head_x_rot << 1;
	yrot = lara.head_y_rot;

	if (xrot > 13650)
		xrot = 13650;
	else if (xrot < -13650)
		xrot = -13650;

	if (yrot > 14560)
		yrot = 14560;
	else if (yrot < -14560)
		yrot = -14560;

	xrot += item->pos.x_rot;
	yrot += item->pos.y_rot;

	// Refresh the joint matrices before asking for the head.
	//
	// Without this the camera lags one frame behind: CalcLaraMatrices is the
	// first thing in DrawPhaseGame (draw.cpp), after CalculateCamera, so
	// lara_joint_matrices still carries the previous frame's state here. In
	// fast movement the camera would fall briefly behind Lara and her body
	// would push through the frame. Flag 1 is the set GetLaraJointPos reads,
	// and it wants a unit matrix on the stack, exactly as in DrawPhaseGame.
	phd_PushUnitMatrix();
	CalcLaraMatrices(1);
	phd_PopMatrix();

	// Eye point: the head joint, pushed a bit along the view direction so
	// the camera does not sit in the back of her own head.
	head.x = 0;
	head.y = 0;
	head.z = 0;
	GetLaraJointPos(&head, LMX_HEAD);

	// The raw reading, kept for the FPHEAD trace at the end: everything below
	// filters it, and filtered numbers do not show where a movement comes from.
	fpRawX = head.x - item->pos.x_pos;
	fpRawY = head.y - item->pos.y_pos;
	fpRawZ = head.z - item->pos.z_pos;

	// Damp the bob in position too, when the camera hangs on the bone.
	//
	// The eye sits on the head joint and walks up and down with every step:
	// the horizon moves two to eight pixels per logic frame and reverses on
	// three out of five, a shiver at about fifteen a second. The rotation
	// filter holds an alternation of that speed down to a few per cent, so
	// what remains is the eye's position.
	//
	// The head's offset inside Lara is smoothed, not its place in the world.
	// That offset never leaves her body, so the damped eye stays within a
	// body's width of where it belongs and cannot lag into a wall - her
	// collision radius is 102 - while walking and falling carry it along
	// untouched. The forward offset below still asks mgLOS.
	//
	// Only with the bone camera: without it the view does not turn with the
	// head, and the bob does not show.
	if (tomb5.first_person_head)
	{
		float rx, ry, rz, d, k;

		rx = float(head.x - item->pos.x_pos);
		ry = float(head.y - item->pos.y_pos);
		rz = float(head.z - item->pos.z_pos);

		if (!fpSmoothValid)
		{
			fpRel[0] = rx; fpRel[1] = ry; fpRel[2] = rz;
			fpPrevRel[0] = rx; fpPrevRel[1] = ry; fpPrevRel[2] = rz;
		}
		else
		{
			// How far the head moved since last frame - not how far the
			// filtered eye has fallen behind it: a bobbing signal keeps that
			// lag moderate the whole time and the filter never chooses
			// properly.
			d = sqrtf((rx - fpPrevRel[0]) * (rx - fpPrevRel[0]) +
			          (ry - fpPrevRel[1]) * (ry - fpPrevRel[1]) +
			          (rz - fpPrevRel[2]) * (rz - fpPrevRel[2]));

			fpPrevRel[0] = rx; fpPrevRel[1] = ry; fpPrevRel[2] = rz;

			if (d < FP_MOVE_SLOW) d = FP_MOVE_SLOW;

			k = (d - FP_MOVE_SLOW) / (FP_MOVE_FAST - FP_MOVE_SLOW);

			if (k > 1.0F) k = 1.0F;

			k = FP_SMOOTH_SLOW + (FP_SMOOTH_FAST - FP_SMOOTH_SLOW) * k;

			fpLogK = k;

			fpRel[0] += (rx - fpRel[0]) * k;
			fpRel[1] += (ry - fpRel[1]) * k;
			fpRel[2] += (rz - fpRel[2]) * k;
		}

		// The damped point has to be reachable from the real head.
		//
		// Damping means lagging, and on a fast move the lag lands the eye
		// where the head no longer is. Pulling up over a ledge is the worst
		// case: the head shoots up past the edge in a couple of frames while
		// the damped point is still below it, inside the floor. The room then
		// resolves to whatever lies beyond that surface and the camera is
		// drawn from outside the level.
		//
		// mgLOS walks the short way from the real head to the damped point,
		// stops in front of anything solid and hands back a place that is
		// genuinely there. The head itself is always valid - it sits inside
		// Lara - so the walk always starts somewhere real. Where nothing is
		// in the way the damped point survives untouched, which is nearly
		// always; where something is, the eye simply stays nearer her head
		// for those few frames.
		{
			GAME_VECTOR from, to;

			from.x = head.x;
			from.y = head.y;
			from.z = head.z;
			from.room_number = item->room_number;
			GetFloor(from.x, from.y, from.z, &from.room_number);

			to.x = item->pos.x_pos + long(fpRel[0]);
			to.y = item->pos.y_pos + long(fpRel[1]);
			to.z = item->pos.z_pos + long(fpRel[2]);
			to.room_number = from.room_number;

			mgLOS(&from, &to, 0);

			head.x = to.x;
			head.y = to.y;
			head.z = to.z;
		}
	}

	eye.x = head.x;
	eye.y = head.y - 16;
	eye.z = head.z;

	// Which room the eye is in has to be worked out, not assumed to be
	// Lara's. Pulling up over a ledge, her item is still in the lower room
	// while her head is already above the edge in the upper one; taking her
	// room would make the wall probe start from a room the eye is not in and
	// the floor clamp hold the camera above the floor of the room below
	// (Andrea1, room 89).
	//
	// The head can also end up in no room at all: in a handstand at a ledge
	// it reaches -2194 while the room it resolves to has its ceiling at
	// -1280, and no other room contains the point. From there the renderer
	// draws the void and every height that comes back belongs somewhere else.
	// Her own position always sits in a real room, so in that case mgLOS
	// walks from her body towards the head, following the portals, and hands
	// back a place that exists and the room it is in.
	//
	// mgLOS is a question, not a placement: it always steps one eighth back
	// from the target at the end and overwrites it, clear path or not
	// (`if (lp) { x -= dx; ... } target->x = x;`). Handed the eye directly it
	// would park the camera some fifty units short of the head - at her chin,
	// with her chest in the picture - so it is used only where being short of
	// the head is intended.
	{
		FLOOR_INFO* fl;
		long h, c;
		short rn;

		// The test is whether the head is somewhere real, not whether it can
		// be seen from her chest: pulling up over a ledge, the straight line
		// from chest to head goes through the stone, and a sight-line test
		// would throw the eye back to her chest every time.
		//
		// A room contains the head when the head lies between that room's
		// ceiling and its floor. Pulling up, the head is inside the room
		// above and is kept; in a handstand it sits above the ceiling of the
		// only room going, and is not.
		//
		// Two starting rooms are tried, because GetFloor can only walk the
		// links it is given. In a pull-up the eye stands at -2210 inside room
		// 89 (-4096 to -2048), but from Lara's room 88 at that x/z there is
		// nothing above - solid stone under the ledge - so the walk hands back
		// 88's own surfaces, -1280 to -254. The eye's previous room is tried
		// first, Lara's second; between them they cover the crossing.
		//
		// A starting room out of range would read room[] past its end; the
		// guard keeps a stale room number from bringing a level down.
		if (fpPrevEyeRoom < 0 || fpPrevEyeRoom >= number_rooms)
			fpPrevEyeRoom = item->room_number;

		rn = fpPrevEyeRoom;
		fl = GetFloor(eye.x, eye.y, eye.z, &rn);
		h = GetHeight(fl, eye.x, eye.y, eye.z);
		c = GetCeiling(fl, eye.x, eye.y, eye.z);

		if (!(h != NO_HEIGHT && c != NO_HEIGHT && eye.y < h && eye.y > c))
		{
			rn = item->room_number;
			fl = GetFloor(eye.x, eye.y, eye.z, &rn);
			h = GetHeight(fl, eye.x, eye.y, eye.z);
			c = GetCeiling(fl, eye.x, eye.y, eye.z);
		}

		fpLogEyeH = h;
		fpLogEyeC = c;
		fpLogEyeY = eye.y;
		fpLogReal = (h != NO_HEIGHT && c != NO_HEIGHT && eye.y < h && eye.y > c) ? 1 : 0;

		if (fpLogReal)
		{
			eye.room_number = rn;
		}
		else
		{
			// Nowhere real: back towards her, as far as mgLOS gets. Here
			// being short of the head is the point.
			GAME_VECTOR from;

			from.x = item->pos.x_pos;
			from.y = item->pos.y_pos - 256;		// chest height, inside her
			from.z = item->pos.z_pos;
			from.room_number = item->room_number;

			eye.room_number = from.room_number;
			mgLOS(&from, &eye, 0);
		}
	}

	fpPrevEyeRoom = eye.room_number;

	fpLogLRoom = item->room_number;
	fpLogERoom = eye.room_number;
	fpLogHeadY = head.y;

	// The forward offset asks the wall, with a safety margin, before it is
	// applied. Close to a wall the eye swings around her head, and 48 units
	// are enough to land inside the wall; GetFloor then finds the room beyond
	// it and the renderer draws from there, through the wall.
	//
	// mgLOS walks in eight steps and steps one back - with a 48-unit offset
	// that leaves only six units to the wall. The renderer's near plane lies
	// at 20 (InitWindow in gamemain.cpp), and it is a surface, not a point:
	// its corners reach further out still.
	//
	// So the probe runs over FP_EYE_OFFSET + FP_MARGIN. If the path is
	// free, the eye gets its normal offset; if something is in the way, it
	// stays at the head center - which lies deep inside Lara and thus
	// always far enough from any wall (her collision radius is 102).
	dest.x = eye.x + (phd_sin(yrot) * (FP_EYE_OFFSET + FP_MARGIN) >> W2V_SHIFT);
	dest.y = eye.y;
	dest.z = eye.z + (phd_cos(yrot) * (FP_EYE_OFFSET + FP_MARGIN) >> W2V_SHIFT);
	dest.room_number = eye.room_number;

	dist = mgLOS(&eye, &dest, 0) ? FP_EYE_OFFSET : 0;

	fpLogLos = dist;

	// Second pass onto the real point: it supplies the room number. With
	// dist 0 it falls back to the head center (start equals target), which
	// also yields the right room.
	dest.x = eye.x + (phd_sin(yrot) * dist >> W2V_SHIFT);
	dest.y = eye.y;
	dest.z = eye.z + (phd_cos(yrot) * dist >> W2V_SHIFT);
	dest.room_number = eye.room_number;
	mgLOS(&eye, &dest, 0);

	camera.pos.x = dest.x;
	camera.pos.y = dest.y;
	camera.pos.z = dest.z;
	camera.pos.room_number = dest.room_number;

	// Target point far ahead along the view direction, computed like the
	// binoculars do.
	dist = (0x5100 * phd_cos(xrot)) >> W2V_SHIFT;
	camera.target.x = camera.pos.x + (phd_sin(yrot) * dist >> W2V_SHIFT);
	camera.target.y = camera.pos.y - (0x5100 * phd_sin(xrot) >> W2V_SHIFT);
	camera.target.z = camera.pos.z + (phd_cos(yrot) * dist >> W2V_SHIFT);
	camera.target.room_number = camera.pos.room_number;

	// The head bone camera. Body rotation plus head rotation is not the
	// whole truth: item->pos.x_rot does not turn over during a backflip -
	// that rotation lives in the animation alone - so the view above stays
	// level while Lara goes over. The head bone carries it, and hanging the
	// camera on the bone lets the world turn with her.
	//
	// The matrix is a 3x4 with the local axes in its columns (see
	// GetLaraJointPos in delstuff.cpp, and what phd_TranslateRel does with
	// it): local +Z - the way a TR mesh faces - is (M02, M12, M22), and
	// local -Y is up, Y growing downward.
	//
	// Off by default and on its own option, since it is a matter of taste.
	fpRoll = 0;

	if (tomb5.first_person_head)
	{
		const float* m = &lara_joint_matrices[LMX_HEAD * indices_count];
		float fx, fy, fz, ux, uy, uz, len;

		fx = m[M02]; fy = m[M12]; fz = m[M22];
		ux = -m[M01]; uy = -m[M11]; uz = -m[M21];

		len = sqrtf(fx * fx + fy * fy + fz * fz);

		if (len > 0.001F)
		{
			float rx, rz, rlen, gx, gy, gz;

			fx /= len; fy /= len; fz /= len;

			len = sqrtf(ux * ux + uy * uy + uz * uz);

			if (len > 0.001F)
			{
				ux /= len; uy /= len; uz /= len;
			}

			// Damp the bob. On the first frame there is nothing to damp
			// against, so the filter is loaded with the current reading.
			if (!fpSmoothValid)
			{
				fpDir[0] = fx; fpDir[1] = fy; fpDir[2] = fz;
				fpUp[0] = ux; fpUp[1] = uy; fpUp[2] = uz;
				fpPrevDir[0] = fx; fpPrevDir[1] = fy; fpPrevDir[2] = fz;
				fpPrevUp[0] = ux; fpPrevUp[1] = uy; fpPrevUp[2] = uz;
				fpSmoothValid = 1;
			}
			else
			{
				float k, d, mx, my, mz;

				// How far the bone moved since last frame, and in which
				// direction - both taken before fpPrevDir is overwritten with
				// the current reading; afterwards the difference would always
				// be zero and k would sit at its slow setting.
				d = 1.0F - (fpPrevDir[0] * fx + fpPrevDir[1] * fy + fpPrevDir[2] * fz);

				mx = fx - fpPrevDir[0];
				my = fy - fpPrevDir[1];
				mz = fz - fpPrevDir[2];

				fpPrevDir[0] = fx; fpPrevDir[1] = fy; fpPrevDir[2] = fz;
				fpPrevUp[0] = ux; fpPrevUp[1] = uy; fpPrevUp[2] = uz;


				// How consistent the recent movement has been. The step is
				// folded into a running average of the signed movement and
				// one of its size; where the two agree the movement is going
				// somewhere.
				{
					float ml, t;

					ml = sqrtf(mx * mx + my * my + mz * mz);

					fpVel[0] += (mx - fpVel[0]) * FP_TREND;
					fpVel[1] += (my - fpVel[1]) * FP_TREND;
					fpVel[2] += (mz - fpVel[2]) * FP_TREND;
					fpSpd += (ml - fpSpd) * FP_TREND;

					t = sqrtf(fpVel[0] * fpVel[0] + fpVel[1] * fpVel[1] + fpVel[2] * fpVel[2]);
					t = (fpSpd > 0.00001F) ? t / fpSpd : 0.0F;

					if (t > 1.0F) t = 1.0F;

					if (d < FP_RATE_SLOW) d = FP_RATE_SLOW;

					k = (d - FP_RATE_SLOW) / (FP_RATE_FAST - FP_RATE_SLOW);

					if (k > 1.0F) k = 1.0F;

					// Both must hold: large and going somewhere. Large
					// alone lets a stutter through.
					k = FP_SMOOTH_SLOW + (FP_SMOOTH_FAST - FP_SMOOTH_SLOW) * k * t;
					fpLogK = k;
				}

				fpDir[0] += (fx - fpDir[0]) * k;
				fpDir[1] += (fy - fpDir[1]) * k;
				fpDir[2] += (fz - fpDir[2]) * k;
				fpUp[0] += (ux - fpUp[0]) * k;
				fpUp[1] += (uy - fpUp[1]) * k;
				fpUp[2] += (uz - fpUp[2]) * k;
			}

			// Filtering shortens a vector, so both are made unit again.
			// Skipped if the filter has collapsed one to nothing - halfway
			// through a full turn the old and the new direction can be
			// opposites - and then the unfiltered value stands in.
			len = sqrtf(fpDir[0] * fpDir[0] + fpDir[1] * fpDir[1] + fpDir[2] * fpDir[2]);

			if (len > 0.001F)
			{
				fx = fpDir[0] / len; fy = fpDir[1] / len; fz = fpDir[2] / len;
			}

			len = sqrtf(fpUp[0] * fpUp[0] + fpUp[1] * fpUp[1] + fpUp[2] * fpUp[2]);

			if (len > 0.001F)
			{
				ux = fpUp[0] / len; uy = fpUp[1] / len; uz = fpUp[2] / len;
			}

			camera.target.x = camera.pos.x + long(fx * 0x5100);
			camera.target.y = camera.pos.y + long(fy * 0x5100);
			camera.target.z = camera.pos.z + long(fz * 0x5100);
			camera.target.room_number = camera.pos.room_number;

			// How far the head is tilted sideways, measured against the
			// horizon: cross the view direction with world up to get the
			// horizon's right, and read the head's own up against it.
			// Looking straight up or down there is no horizon to measure
			// from, and the roll is left at zero rather than spun at random.
			rx = fz; rz = -fx;
			rlen = sqrtf(rx * rx + rz * rz);

			if (rlen > 0.001F)
			{
				rx /= rlen; rz /= rlen;
				gx = -rz * fy;
				gy = rz * fx - rx * fz;
				gz = rx * fy;
				fpRoll = short(atan2f(ux * rx + uz * rz,
				                      ux * gx + uy * gy + uz * gz) * 32768.0F / 3.14159265F);
			}
		}
	}

	// Carry the shakes along, or explosions lose all their punch.
	if (camera.bounce && camera.type == camera.old_type)
	{
		if (camera.bounce <= 0)
		{
			rndval = -camera.bounce;
			shake = rndval >> 1;
			camera.target.x += (GetRandomControl() % rndval - shake) << 4;
			camera.target.y += (GetRandomControl() % rndval - shake) << 4;
			camera.target.z += (GetRandomControl() % rndval - shake) << 4;
			camera.bounce += 5;
		}
		else
		{
			camera.bounce = 0;
			camera.target.y += camera.bounce;
		}
	}

	// Remember that first person really ran this frame. Hiding head and
	// braid hangs on this (drawlara.cpp); mesh_bits is deliberately not
	// touched here:
	//
	//  - Lara's skin is skinned. A mesh hidden via mesh_bits no longer
	//    stashes its rim points (both stand in the same bracket), and the
	//    neighbour then skins from stale memory: a stretched triangle like
	//    a third arm.
	//  - Binoculars and laser sight write mesh_bits as well (0 on entry,
	//    -1 on leaving); a second writer on the same field would leave the
	//    fault standing after the scope until a restart.
	//
	// Who gets hidden is decided solely by the draw routine.
	fpHeadHidden = 1;
	AlterFOV(FIRST_PERSON_FOV);
	fpFovSet = 1;

	// Hold the eye clear of floor and ceiling.
	//
	// The probe further up guards the way ahead, not up and down. Pulling up
	// over a ledge puts the eye a few units above the floor it is rising
	// past, and the near plane is a surface at twenty units, not a point -
	// its lower corners then reach through the floor.
	//
	// Only where there is room for it. In a crawlspace the two clamps would
	// fight each other, and being pushed about is worse than the odd glimpse
	// through a floor, so a gap of at least twice the margin is required
	// before either applies.
	{
		FLOOR_INFO* fl;
		long h, c;

		fl = GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &camera.pos.room_number);
		h = GetHeight(fl, camera.pos.x, camera.pos.y, camera.pos.z);
		c = GetCeiling(fl, camera.pos.x, camera.pos.y, camera.pos.z);

		fpLogFloor = h;
		fpLogCeil = c;
		fpLogYBefore = camera.pos.y;
		fpLogClamp = 0;

		// A clamp may nudge; it may never move the camera far. In a handstand
		// at a ledge the eye sits at -2210, the room resolves back from 89 to
		// 88, and 88's ceiling there reads -1280 - an unbounded clamp would
		// push the camera to -1232, nearly a thousand units down, through the
		// floor into the room below. The head is above that ceiling, in no
		// room at all, so the heights that come back belong somewhere else.
		//
		// Grazing a surface is worth a nudge (FP_CLAMP_MAX); anything larger
		// means the reading does not describe where the eye is, and the
		// camera stays where the head put it.
		if (h != NO_HEIGHT && c != NO_HEIGHT && h - c > 2 * FP_CLEAR)
		{
			if (camera.pos.y > h - FP_CLEAR && camera.pos.y - (h - FP_CLEAR) <= FP_CLAMP_MAX)
			{
				camera.pos.y = h - FP_CLEAR;
				fpLogClamp = 1;
			}

			if (camera.pos.y < c + FP_CLEAR && (c + FP_CLEAR) - camera.pos.y <= FP_CLAMP_MAX)
			{
				camera.pos.y = c + FP_CLEAR;
				fpLogClamp = 2;
			}
		}
	}

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &camera.pos.room_number);
	// FPHEAD: one line per frame under -log, only in first person.
	//
	// Both the raw reading and what the camera was given, because one alone
	// cannot show whether a filter is working:
	//
	//   rel / fwd   the bone as the animation delivers it, untouched
	//   srel / sfwd what came out of the filters and reached the camera
	//   k           the strength the position filter chose this frame
	//   los         what the wall probe granted, 48 or 0
	//
	// If fwd jumps about and sfwd is calm, the filter is doing its work and
	// anything still seen comes from somewhere else. If sfwd jumps too, the
	// filter is too weak. And srel against rel says the same for the eye's
	// own bob.
	{
		const float* mh = &lara_joint_matrices[LMX_HEAD * indices_count];
		const float sx = float(camera.target.x - camera.pos.x);
		const float sy = float(camera.target.y - camera.pos.y);
		const float sz = float(camera.target.z - camera.pos.z);
		const float sl = sqrtf(sx * sx + sy * sy + sz * sz) + 0.001F;

		Log("FPHEAD anim %d frame %d state %d | rel %d,%d,%d | fwd %.4f,%.4f,%.4f "
		    "| srel %d,%d,%d | sfwd %.4f,%.4f,%.4f | k %.3f | roll %d | los %d",
		    (int)item->anim_number, (int)item->frame_number, (int)item->current_anim_state,
		    (int)fpRawX, (int)fpRawY, (int)fpRawZ,
		    mh[M02], mh[M12], mh[M22],
		    (int)fpRel[0], (int)fpRel[1], (int)fpRel[2],
		    sx / sl, sy / sl, sz / sl,
		    fpLogK, (int)fpRoll, (int)fpLogLos);
	}

	phd_LookAt(camera.pos.x, camera.pos.y, camera.pos.z, camera.target.x, camera.target.y, camera.target.z, fpRoll);
	camera.old_type = camera.type;
}

void BinocularCamera(ITEM_INFO* item)
{
	FLOOR_INFO* floor;
	PHD_VECTOR pos;
	PHD_VECTOR pos1;
	PHD_VECTOR Soffset;
	PHD_VECTOR Eoffset;
	short* ammo;
	long shake, speed, c, BinocStep, pit, rndval;
	short room_number, hxrot, hyrot;
	char Fire;
	static char LSHKTimer = 0;
	static char LSHKShotsFired = 0;

	if (LSHKTimer)
		LSHKTimer--;

	if (!LaserSight)
	{
		if (inputBusy & IN_DRAW)
			ExittingBinos = 1;
		else if (ExittingBinos)
		{
			ExittingBinos = 0;
			BinocularRange = 0;
			AlterFOV(GAME_FOV);
			lara_item->mesh_bits = -1;
			lara.Busy = 0;
			lara.head_y_rot = 0;
			lara.head_x_rot = 0;
			lara.torso_y_rot = 0;
			lara.torso_x_rot = 0;
			bLaraTorch = 0;
			camera.type = BinocularOldCamera;
			return;
		}
	}

	lara_item->mesh_bits = 0;
	AlterFOV(short(GAME_FOV - (7 * BinocularRange)));
	hxrot = lara.head_x_rot << 1;
	hyrot = lara.head_y_rot;

	if (hxrot > 13650)
		hxrot = 13650;
	else if (hxrot < -13650)
		hxrot = -13650;

	if (hyrot > 14560)
		hyrot = 14560;
	else if (hyrot < -14560)
		hyrot = -14560;

	hyrot += lara_item->pos.y_rot;
	pos.x = lara_item->pos.x_pos;
	pos.y = lara_item->pos.y_pos - 512;
	pos.z = lara_item->pos.z_pos;
	room_number = lara_item->room_number;
	floor = GetFloor(pos.x, pos.y, pos.z, &room_number);
	c = GetCeiling(floor, pos.x, pos.y, pos.z);

	if (c <= pos.y - 256)
		pos.y -= 256;
	else
		pos.y = c + 64;

	speed = (0x5100 * phd_cos(hxrot)) >> W2V_SHIFT;
	pos1.x = pos.x + (phd_sin(hyrot) * speed >> W2V_SHIFT);
	pos1.y = pos.y - (0x5100 * phd_sin(hxrot) >> W2V_SHIFT);
	pos1.z = pos.z + (phd_cos(hyrot) * speed >> W2V_SHIFT);
	camera.pos.x = pos.x;
	camera.pos.y = pos.y;
	camera.pos.z = pos.z;
	camera.pos.room_number = room_number;

	if (camera.old_type == FIXED_CAMERA)
	{
		camera.target.x = pos1.x;
		camera.target.y = pos1.y;
		camera.target.z = pos1.z;
		camera.target.room_number = lara_item->room_number;
	}
	else
	{
		camera.target.x += (pos1.x - camera.target.x) >> 2;
		camera.target.y += (pos1.y - camera.target.y) >> 2;
		camera.target.z += (pos1.z - camera.target.z) >> 2;
		camera.target.room_number = lara_item->room_number;
	}

	if (camera.bounce && camera.type == camera.old_type)
	{
		if (camera.bounce <= 0)
		{
			rndval = -camera.bounce;
			shake = rndval >> 1;
			camera.target.x += (GetRandomControl() % rndval - shake) << 4;
			camera.target.y += (GetRandomControl() % rndval - shake) << 4;
			camera.target.z += (GetRandomControl() % rndval - shake) << 4;
			camera.bounce += 5;
		}
		else
		{
			camera.bounce = 0;
			camera.target.y += camera.bounce;
		}
	}

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &camera.pos.room_number);
	phd_LookAt(camera.pos.x, camera.pos.y, camera.pos.z, camera.target.x, camera.target.y, camera.target.z, 0);
	camera.old_type = camera.type;

	if (inputBusy & IN_WALK)
	{
		BinocStep = 32;
		pit = 0x8000;
	}
	else
	{
		BinocStep = 64;
		pit = 0x10000;
	}

	if (inputBusy & IN_SPRINT)
	{
		BinocularRange -= BinocStep;

		if (BinocularRange < 128)
			BinocularRange = 128;
		else
			SoundEffect(SFX_ZOOM_VIEW_WHIRR, 0, (pit << 8) | SFX_ALWAYS | SFX_SETPITCH);
	}
	else if (inputBusy & IN_DUCK)
	{
		BinocularRange += BinocStep;

		if (BinocularRange > 1536)
			BinocularRange = 1536;
		else
			SoundEffect(SFX_ZOOM_VIEW_WHIRR, 0, (pit << 8) | SFX_ALWAYS | SFX_SETPITCH);
	}

	Soffset.x = camera.pos.x;
	Soffset.y = camera.pos.y;
	Soffset.z = camera.pos.z;
	Eoffset.x = camera.target.x;
	Eoffset.y = camera.target.y;
	Eoffset.z = camera.target.z;

	if (LaserSight)
	{
		Fire = 0;
		ammo = get_current_ammo_pointer(lara.gun_type);

		if (!(inputBusy & IN_ACTION) || WeaponDelay || !ammo[0])
		{
			if (!(inputBusy & IN_ACTION))
			{
				if (lara.gun_type != WEAPON_CROSSBOW)
					WeaponDelay = 0;

				LSHKShotsFired = 0;
				camera.bounce = 0;
			}
		}
		else if (lara.gun_type == WEAPON_REVOLVER)
		{
			Fire = 1;
			WeaponDelay = 16;
			savegame.Game.AmmoUsed++;

			if (ammo[0] != -1)
				ammo[0]--;
		}
		else if (lara.gun_type == WEAPON_CROSSBOW)
		{
			Fire = 1;
			WeaponDelay = 32;
		}
		else if (lara.hk_type_carried & W_AMMO1)//sniper mode!
		{
			WeaponDelay = 12;
			Fire = 1;

			if (lara.hk_type_carried & W_SILENCER)
				SoundEffect(SFX_HK_SILENCED, 0, SFX_DEFAULT);
			else
			{
				SoundEffect(SFX_EXPLOSION1, 0, 0x5000800 | SFX_SETPITCH | SFX_SETVOL);
				SoundEffect(SFX_HK_FIRE, 0, SFX_DEFAULT);
			}

			if (ammo[0] != -1)
				ammo[0]--;
		}
		else if (lara.hk_type_carried & W_AMMO2)//burst mode!
		{
			if (LSHKTimer)
			{
				camera.bounce = -16 - (GetRandomControl() & 0x1F);

				if (lara.hk_type_carried & W_SILENCER)
					SoundEffect(SFX_HK_SILENCED, 0, SFX_DEFAULT);
				else
				{
					SoundEffect(SFX_EXPLOSION1, 0, 0x5000800 | SFX_SETPITCH | SFX_SETVOL);
					SoundEffect(SFX_HK_FIRE, 0, SFX_DEFAULT);
				}
			}
			else
			{
				LSHKShotsFired++;

				if (LSHKShotsFired == 5)
				{
					LSHKShotsFired = 0;
					WeaponDelay = 12;
				}

				LSHKTimer = 4;
				Fire = 1;

				if (lara.hk_type_carried & W_SILENCER)
					SoundEffect(SFX_HK_SILENCED, 0, SFX_DEFAULT);
				else
				{
					SoundEffect(SFX_EXPLOSION1, 0, 0x5000800 | SFX_SETPITCH | SFX_SETVOL);
					SoundEffect(SFX_HK_FIRE, 0, SFX_DEFAULT);
				}

				if (ammo[0] != -1)
					ammo[0]--;
			}
		}
		else//rapid mode!
		{
			if (LSHKTimer)
			{
				if (lara.hk_type_carried & W_SILENCER)
					SoundEffect(SFX_HK_SILENCED, 0, SFX_DEFAULT);
				else
				{
					SoundEffect(SFX_EXPLOSION1, 0, 0x5000800 | SFX_SETPITCH | SFX_SETVOL);
					SoundEffect(SFX_HK_FIRE, 0, SFX_DEFAULT);
				}
			}
			else
			{
				LSHKTimer = 4;
				Fire = 1;

				if (lara.hk_type_carried & W_SILENCER)
					SoundEffect(SFX_HK_SILENCED, 0, SFX_DEFAULT);
				else
				{
					SoundEffect(SFX_EXPLOSION1, 0, 0x5000800 | SFX_SETPITCH | SFX_SETVOL);
					SoundEffect(SFX_HK_FIRE, 0, SFX_DEFAULT);
				}

				if (ammo[0] != -1)
					ammo[0]--;
			}

			camera.bounce = -16 - (GetRandomControl() & 0x1F);
		}

		GetTargetOnLOS(&camera.pos, &camera.target, 1, Fire);
	}
	else
	{
		GetTargetOnLOS(&camera.pos, &camera.target, 0, 0);

		if (!(inputBusy & IN_ACTION) || InfraRed)
			bLaraTorch = 0;
		else
			LaraTorch(&Soffset, &Eoffset, lara.head_y_rot, 192);
	}
}

void LookCamera(ITEM_INFO* item)
{
	GAME_VECTOR ideal;
	PHD_VECTOR pos1;
	PHD_VECTOR pos2;
	PHD_VECTOR pos3;
	FLOOR_INFO* floor;
	long shake, dx, dy, dz, wx, wy, wz, h, c, hxrot, txrot, hyrot, tyrot, rndval, lp;
	short room_number, room_number2;

	hxrot = lara.head_x_rot;
	hyrot = lara.head_y_rot;
	txrot = lara.torso_x_rot;
	tyrot = lara.torso_y_rot;
	lara.torso_x_rot = 0;
	lara.torso_y_rot = 0;
	lara.head_x_rot <<= 1;
	lara.head_y_rot <<= 1;

	if (lara.head_x_rot > 10010)
		lara.head_x_rot = 10010;
	else if (lara.head_x_rot < -13650)
		lara.head_x_rot = -13650;

	if (lara.head_y_rot < -14560)
		lara.head_y_rot = -14560;
	else if (lara.head_y_rot > 14560)
		lara.head_y_rot = 14560;

	if (abs(lara.head_x_rot - old_cam.pos.x_rot) >= 16)
		old_cam.pos.x_rot = (lara.head_x_rot + old_cam.pos.x_rot) >> 1;
	else
		old_cam.pos.x_rot = lara.head_x_rot;

	if (abs(lara.head_y_rot - old_cam.pos.y_rot) >= 16)
		old_cam.pos.y_rot = (lara.head_y_rot + old_cam.pos.y_rot) >> 1;
	else
		old_cam.pos.y_rot = lara.head_y_rot;

	pos1.x = 0;
	pos1.y = 16;
	pos1.z = 64;
	GetLaraJointPos(&pos1, LMX_HEAD);
	room_number = lara_item->room_number;
	floor = GetFloor(pos1.x, pos1.y, pos1.z, &room_number);
	h = GetHeight(floor, pos1.x, pos1.y, pos1.z);
	c = GetCeiling(floor, pos1.x, pos1.y, pos1.z);

	if (h == NO_HEIGHT || c == NO_HEIGHT || c >= h || pos1.y > h || pos1.y < c)
	{
		pos1.x = 0;
		pos1.y = 16;
		pos1.z = 0;
		GetLaraJointPos(&pos1, LMX_HEAD);
		floor = GetFloor(pos1.x, pos1.y, pos1.z, &room_number);
		h = GetHeight(floor, pos1.x, pos1.y, pos1.z);
		c = GetCeiling(floor, pos1.x, pos1.y, pos1.z);

		if (h == NO_HEIGHT || c == NO_HEIGHT || c >= h || pos1.y > h || pos1.y < c)
		{
			pos1.x = 0;
			pos1.y = 16;
			pos1.z = -64;
			GetLaraJointPos(&pos1, LMX_HEAD);
		}
	}

	pos2.x = 0;
	pos2.y = 0;
	pos2.z = -1024;
	GetLaraJointPos(&pos2, LMX_HEAD);
	pos3.x = 0;
	pos3.y = 0;
	pos3.z = 2048;
	GetLaraJointPos(&pos3, LMX_HEAD);
	wy = pos1.y;
	wx = pos1.x;
	wz = pos1.z;
	dx = (pos2.x - pos1.x) >> 3;
	dy = (pos2.y - pos1.y) >> 3;
	dz = (pos2.z - pos1.z) >> 3;
	room_number2 = lara_item->room_number;

	for (lp = 0; lp < 8; lp++)
	{
		room_number = room_number2;
		floor = GetFloor(wx, wy, wz, &room_number2);
		h = GetHeight(floor, wx, wy, wz);
		c = GetCeiling(floor, wx, wy, wz);

		if (h == NO_HEIGHT || c == NO_HEIGHT || c >= h || wy > h || wy < c)
			break;

		wz += dz;
		wx += dx;
		wy += dy;
	}

	if (lp)
	{
		wx -= dx;
		wy -= dy;
		wz -= dz;
	}

	ideal.x = wx;
	ideal.y = wy;
	ideal.z = wz;
	ideal.room_number = room_number;

	if (old_cam.pos.x_rot == lara.head_x_rot && old_cam.pos.y_rot == lara.head_y_rot && old_cam.pos.x_pos == lara_item->pos.x_pos &&
		old_cam.pos.y_pos == lara_item->pos.y_pos && old_cam.pos.z_pos == lara_item->pos.z_pos && 
		old_cam.current_anim_state == lara_item->current_anim_state && old_cam.goal_anim_state == lara_item->goal_anim_state
		&& camera.old_type == LOOK_CAMERA)
	{
		ideal.x = static_lookcamp.x;
		ideal.y = static_lookcamp.y;
		ideal.z = static_lookcamp.z;
		ideal.room_number = static_lookcamp.room_number;
		pos3.x = static_lookcamt.x;
		pos3.y = static_lookcamt.y;
		pos3.z = static_lookcamt.z;
	}
	else
	{
		old_cam.pos.x_pos = lara_item->pos.x_pos;
		old_cam.pos.y_pos = lara_item->pos.y_pos;
		old_cam.pos.z_pos = lara_item->pos.z_pos;
		old_cam.current_anim_state = lara_item->current_anim_state;
		old_cam.goal_anim_state = lara_item->goal_anim_state;
		static_lookcamp.x = wx;
		static_lookcamp.y = wy;
		static_lookcamp.z = wz;
		static_lookcamp.room_number = ideal.room_number;
		static_lookcamt.x = pos3.x;
		static_lookcamt.y = pos3.y;
		static_lookcamt.z = pos3.z;
	}

	CameraCollisionBounds(&ideal, 224, 1);

	if (camera.old_type == FIXED_CAMERA)
	{
		camera.pos.x = ideal.x;
		camera.pos.y = ideal.y;
		camera.pos.z = ideal.z;
		camera.target.x = pos3.x;
		camera.target.y = pos3.y;
		camera.target.z = pos3.z;
	}
	else
	{
		dx = ideal.x - camera.pos.x;
		dy = ideal.y - camera.pos.y;
		dz = ideal.z - camera.pos.z;
		camera.pos.x += dx >> 2;
		camera.pos.y += dy >> 2;
		camera.pos.z += dz >> 2;
		dx = pos3.x - camera.target.x;
		dy = pos3.y - camera.target.y;
		dz = pos3.z - camera.target.z;
		camera.target.x += dx >> 2;
		camera.target.y += dy >> 2;
		camera.target.z += dz >> 2;
	}

	camera.target.room_number = lara_item->room_number;

	if (camera.bounce && camera.type == camera.old_type)
	{
		if (camera.bounce <= 0)
		{
			rndval = -camera.bounce;
			shake = -camera.bounce >> 1;
			camera.target.x += GetRandomControl() % rndval - shake;
			camera.target.y += GetRandomControl() % rndval - shake;
			camera.target.z += GetRandomControl() % rndval - shake;
			camera.bounce += 5;
		}
		else
		{
			camera.pos.y += camera.bounce;
			camera.target.y += camera.bounce;
			camera.bounce = 0;
		}
	}

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &camera.pos.room_number);

	wx = camera.pos.x;
	wy = camera.pos.y;
	wz = camera.pos.z;
	room_number = camera.pos.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (c > wy - 255 && h<wy + 255 && h > c && c != NO_HEIGHT && h != NO_HEIGHT)
		camera.pos.y = (h + c) >> 1;
	else if (h < wy + 255 && h > c && c != NO_HEIGHT && h != NO_HEIGHT)
		camera.pos.y = h - 255;
	else if (c > wy - 255 && h > c && c != NO_HEIGHT && h != NO_HEIGHT)
		camera.pos.y = c + 255;

	wx = camera.pos.x;
	wy = camera.pos.y;
	wz = camera.pos.z;
	room_number = camera.pos.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (wy < c || wy > h || c >= h || h == NO_HEIGHT || c == NO_HEIGHT)
		mgLOS(&camera.target, &camera.pos, 0);

	wx = camera.pos.x;
	wy = camera.pos.y;
	wz = camera.pos.z;
	room_number = camera.pos.room_number;
	floor = GetFloor(wx, wy, wz, &room_number);
	h = GetHeight(floor, wx, wy, wz);
	c = GetCeiling(floor, wx, wy, wz);

	if (wy < c || wy > h || c >= h || h == NO_HEIGHT || c == NO_HEIGHT)
	{
		camera.pos.x = pos1.x;
		camera.pos.y = pos1.y;
		camera.pos.z = pos1.z;
		camera.pos.room_number = lara_item->room_number;
	}

	if (gfLevelId == LVL5_THIRTEENTH_FLOOR || gfLevelId == LVL5_ESCAPE_WITH_THE_IRIS)
		CheckForRichesIllegalDiagonalWalls();

	GetFloor(camera.pos.x, camera.pos.y, camera.pos.z, &camera.pos.room_number);
	phd_LookAt(camera.pos.x, camera.pos.y, camera.pos.z, camera.target.x, camera.target.y, camera.target.z, 0);
	camera.old_type = camera.type;

	lara.head_x_rot = (short)hxrot;
	lara.head_y_rot = (short)hyrot;
	lara.torso_x_rot = (short)txrot;
	lara.torso_y_rot = (short)tyrot;
}

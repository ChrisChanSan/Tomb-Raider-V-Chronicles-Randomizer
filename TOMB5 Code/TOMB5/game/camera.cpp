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

// First person view (#126): marker whether WE hid Lara's head, and the
// question whether the view may run at all. Both stand up here because
// CalculateCamera needs them - the camera itself lives further down next to
// its template BinocularCamera.
static long fpHeadHidden = 0;
static long fpFovSet = 0;
static short fpRoll = 0;

// The smoothed view direction and up vector of the head bone camera (#134).
//
// Lara's head bobs with every step and breathes on the spot. As long as the
// camera only took the head's PLACE that hardly showed; now that it turns
// with the bone as well, the bob became a jolt ("very rough" - the user).
//
// Filtered are the two VECTORS, not the angles: an angle wraps at half a
// turn and a filter would jump the long way round the moment Lara crosses it.
//
// How hard depends on HOW BIG the movement is, and that is the whole point.
// One fixed strength cannot serve both: enough to still the tremble and the
// somersault turns to treacle, enough for the somersault and the tremble
// stays. They differ in size, not in kind - a shiver is a fraction of a
// degree, a backflip is a quarter turn in a frame - so the strength is read
// off the angle between where the view stands and where the bone points.
//
// Small movement, heavy filter; large movement, almost none. Between the two
// it slides, so nothing snaps as Lara goes from standing to tumbling.
// Retuned once the filters actually ran and could be measured. With 0.08 the
// view trailed the truth by nine degrees while walking and by twenty-seven in
// fast motion - "a bit sluggish", and no wonder.
//
// The slow setting no longer has to be near-zero, because the shiver it has
// to hold back is small: 0.34 degrees a frame, reversing every other frame.
// A filter at 0.30 passes under a fifth of an alternation of that speed -
// well under a pixel - while trailing a steady turn by only about three
// frames.
// The strong end is now the player's to set, because which of the two
// nuisances weighs more is a matter of taste and could not be settled by
// argument: damping harder hides a shake, and lengthens the tail the camera
// drags behind a movement that has stopped. One of those is more annoying
// than the other, and only the person looking at it knows which.
//
// Off means no damping at all - the bone as it comes.
static const float fpSmoothSlow[4] = { 1.00F, 0.30F, 0.18F, 0.10F };
#define FP_SMOOTH_SLOW	(fpSmoothSlow[tomb5.first_person_smooth & 3])
// A large movement is passed through WHOLE. At 0.75 the camera went
// three-quarters of the way on the jump and crept the rest afterwards, and
// that crawl is heard as the second beat of the "bub bub bub" at the end of
// every animation. The filter is there to kill small jitter; nothing about a
// real turn needs holding back.
#define FP_SMOOTH_FAST	1.00F		// a somersault: followed outright
// Read off the head bone's own trace (-log, FPHEAD), which is what finally
// told the two apart. Per frame, the bone's direction moves:
//
//   standing (anim 103)   0.60 degrees, and it hardly ever reverses
//   walking  (anim 0)     0.70 degrees typical, one hard step of 1.53
//   running  (anim 75)   32     degrees
//
// A factor of forty between the wobble and the real turn. So the strength is
// read off HOW FAST THE BONE ITSELF MOVES, not off how far the view has
// fallen behind - that was the earlier mistake, and it measured the wrong
// thing: a slowly drifting but noisy signal keeps that distance moderate the
// whole time, so the filter never chose either setting properly.
// And the thresholds came down, because they had the player's own looking
// about on the wrong side of the line. Measured: the shiver moves the bone
// 0.34 degrees a frame, looking around moves it 2 to 7. Two degrees was
// therefore exactly the wrong place to put the boundary.
#define FP_RATE_SLOW	0.00015F	// 1 - cos(1 degree): only the shiver below
#define FP_RATE_FAST	0.0055F		// 1 - cos(6 degrees): looking about, follow
// The same for the eye's own travel, and for the same reason. Off the trace,
// the head's offset inside Lara moves up to 8 units a frame while walking and
// reverses constantly - some 8 pixels on screen, which is a shimmer. A jump
// or a fall moves it far further, and that has to come through.
#define FP_MOVE_SLOW	1.0F		// units per frame: only the bob below
#define FP_MOVE_FAST	12.0F		// units per frame: a real move, follow
static float fpDir[3] = { 0, 0, 0 };
static float fpUp[3] = { 0, 0, 0 };
static float fpRel[3] = { 0, 0, 0 };

// The PREVIOUS frame's raw reading, to measure how fast the bone is moving.
//
// It used to be averaged with the current one as well, meant against an
// alternation from frame to frame. The later measurement found no such thing
// - the walk is a noisy curve, not a clean alternation - and the average
// cost more than it bought: at every jump it stepped half way and then the
// rest, which is the first beat of the "bub bub bub" heard at the end of
// every animation. Removed; only the comparison is kept.
//
// Measured off the user's second recording, which he was good enough to
// record as standing still first and then walking straight: standing, the
// picture drifts smoothly and does not shiver at all. Walking, it jumps
// between two places EVERY logic frame - thirty pixels apart, fifteen times
// a second. A real step bob would be a soft wave over some fifteen frames;
// something alternating frame by frame is not animation.
//
// And the filter could not catch it, because it was built the wrong way
// round for this: its strength grows with the distance between where the
// view stands and where the bone points, and for a movement that reverses
// every frame that distance is ALWAYS large. So it ran permanently at its
// fast setting and chased the shiver instead of damping it - worse than the
// fixed strength it replaced.
//
// The average of two neighbouring frames cancels an alternation of exactly
// this rate to nothing, while a somersault - smooth over about a second -
// passes almost untouched, half a frame late. So the raw reading is paired
// with the one before it BEFORE the filter sees it, and the filter then does
// what it is good at: the slow drift.
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
// part cancels itself, and the ratio falls towards nothing.
//
// Speed alone cannot tell them apart, and that is what was missing. Speed
// said "large, follow it" to the three-beat stumble at the end of an
// animation just as readily as to a real turn.
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

void InitialiseCamera()
{
	// EVERYTHING the first person camera remembers is forgotten here.
	//
	// These are statics, and a static outlives a level. fpFovSet is still
	// standing from the last frame of the level just left, so the guard that
	// starts the filters afresh never fires - and fpPrevEyeRoom then carries
	// a room number from the OLD level into the new one. GetFloor is handed
	// it as its starting room and reads room[] out of range.
	//
	// Five crash logs from the user's playthrough, all of them a level
	// CHANGE - back to the title, Andy1 to Andy3, loading a save, the end of
	// rich3 - and every one of them stops at the same place: the two
	// AlterFOV lines of the fresh level, then nothing. First person was on
	// in all five.
	//
	// InitialiseCamera runs once per level, which is exactly the moment
	// nothing may be carried over.
	fpFovSet = 0;
	fpRanLast = 0;
	fpSmoothValid = 0;
	fpPrevEyeRoom = lara_item ? lara_item->room_number : 0;
	fpVel[0] = fpVel[1] = fpVel[2] = 0;
	fpSpd = 0;
	fpRoll = 0;

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

	// First person view (#126): reset the marker EVERY FRAME. It is only
	// set again when FirstPersonCamera below really runs - otherwise Lara
	// would stand headless in every fixed camera and every cutscene.
	// CalculateCamera runs before drawing, the frame always sees this
	// pass's state.
	fpHeadHidden = 0;

	// And the field of view goes back the same way (#134). It has to be
	// given back whenever the view ends - the option can be switched off,
	// the binoculars take over, Lara can die - and none of those tell us.
	// So it is handed back here every frame and taken again below, only
	// when the first person camera really runs.
	//
	// Safe against the other users of AlterFOV: the binoculars set theirs
	// during DRAWING, which is after this, and a flyby never reaches this
	// function at all (control.cpp calls CalculateSpotCams instead).
	// Did the first person camera run LAST frame? That, and not the handing
	// back of the field of view, is what the filters need to know.
	//
	// The reset used to sit inside the block below, and the block runs every
	// frame the view is on - fpFovSet is set at the end of every one. So the
	// filters were cleared on every single frame and never filtered anything
	// at all: they loaded the raw reading afresh each time and handed it
	// straight on. Proved by the trace, where the filtered numbers came out
	// exactly equal to the raw ones and k read 0.000 throughout. Every
	// attempt at the shiver before this was dead code.
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
	// of an animation. Three places still use it - lifting a floor trapdoor
	// (traps.cpp), the ceiling trapdoor, and the turn switch (switch.cpp).
	// In first person each of them threw the player out of Lara's eyes and
	// back in again (user finding: "lifting floor trapdoors is third
	// person").
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

	if (camera.item && (camera.type == FIXED_CAMERA || camera.type == HEAVY_CAMERA))
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

	if (camera.item && !fixed_camera)
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
		// shown opening, every lever with a payoff somewhere else (user
		// finding, Andrea1 triggers 7 and 31).
		//
		// The look/combat path below has always let first person win. This
		// branch was the odd one out; the two agree now.
		//
		// EXCEPT the real sniper camera - and that is bit 1 alone, not the
		// flags & 3 this first read. The two bits are different things:
		//
		//   bit 1  the sniper view. FixedCamera aims it at Lara's torso and
		//          sets SniperOverlay, which draws the scope. Skipping the
		//          call would leave the player aiming through nothing.
		//          Measured with CameraCheck: FOUR cameras in the whole game,
		//          all in rich2.
		//   bit 0  only feeds SniperCamActive, which locks the look key and
		//          the inventory (control.cpp, newinv2.cpp). No overlay, no
		//          special targeting - and it is everywhere: 54 cameras, 11 of
		//          andy1's 12 alone.
		//
		// Testing flags & 3 therefore spared 58 of the game's 92 cameras when
		// four needed it. The user found it in one glance: two Andrea1
		// triggers that look identical in trview, camera 4 (0x0000) behaving
		// and camera 1 (0x0001) not. SniperCamActive keeps its own job either
		// way - it is assigned further up and the locks stay.
		const long wouldFix = (camera.type != CHASE_CAMERA && camera.flags != 3);
		const long sniperCam = wouldFix && camera.number >= 0 &&
								   (camera.fixed[camera.number].flags & 2);

		if (FirstPersonEnabled() && !sniperCam)
		{
			FirstPersonCamera(item);

			// FixedCamera's own countdown, verbatim and only where it would
			// have run. It has to keep ticking: the reset at the end of this
			// function leaves a HEAVY_CAMERA alone while its timer is not -1,
			// so a heavy trigger camera passed over in silence would stay
			// nailed for good - invisible while first person lasts, and
			// waiting to spring the moment it ends.
			if (wouldFix && camera.timer)
			{
				camera.timer--;

				if (!camera.timer)
					camera.timer = -1;
			}
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

void UpdateCameraElevation()
{
	PHD_VECTOR pos;
	PHD_VECTOR pos1;

	if (camera.lara_node != -1)
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
	long distance, dx, dz, farthest, farthestnum, h, c, wx, wy, wz, angle, lp;
	short room_number;

	if (!camera.target_elevation)
		camera.target_elevation = -1820;

	camera.target_elevation += item->pos.x_rot;
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
	CameraCollisionBounds(&ideal, 384, 1);

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
	long distance, dx, dz, farthest, farthestnum, h, c, wx, wy, wz, angle, lp;
	short room_number;

	camera.target.x = item->pos.x_pos;
	camera.target.z = item->pos.z_pos;

	if (lara.target)
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
	CameraCollisionBounds(&ideal, 384, 1);

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
 * First person view (#126): the camera hangs on Lara's head instead of
 * behind her.
 *
 * The template is BinocularCamera right below - the binoculars are the same
 * idea, so Core already built it. Three differences:
 *
 *  - Lara stays MOVABLE. The binoculars set lara.Busy and freeze her; here
 *    only the view is replaced, the controls stay as they are.
 *  - ONLY THE HEAD disappears, not the whole body (user requirement): the
 *    binoculars set mesh_bits = 0 and hide everything - but then the frame
 *    lacks the arms she grabs and shoots with.
 *  - The eye point comes from the real head joint (GetLaraJointPos) instead
 *    of a fixed offset. That way it follows crouching, crawling, swimming
 *    and every animation on its own.
 *
 * A first attempt, deliberately plain: no collision check for the camera
 * (it sits in Lara's head, which never sticks in a wall anyway) and no
 * smoothing - it is nailed to the head, that was the intent.
 */
bool FirstPersonActive()
{
	// Flybys SHOW Lara - her head must be there.
	//
	// And they are the one case where the marker alone does not suffice:
	// during a flyby, control.cpp does not call CalculateCamera at all but
	// CalculateSpotCams. The reset above thus never runs, and the marker
	// would stay at the last normal frame's state - Lara sat headless in
	// exactly the cameras that present her (photographed by the user).
	// Cutscenes do go through CalculateCamera (CINEMATIC_CAMERA branch,
	// behind the reset), but stand here for clarity.
	if (bUseSpotCam || GLOBAL_playing_cutseq)
		return false;

	return fpHeadHidden != 0;
}

/* THE HEAD'S OWN ROLL (#148, order 2, risk K3).
 *
 * fpRoll is worked out from the head bone's up vector once per LOGIC tick,
 * inside FirstPersonCamera, and the filters that produce it (fpVel, fpSpd,
 * fpRel, #134) are tuned as per-tick exponentials and must not be moved or
 * run twice. The interpolator therefore leaves the whole camera alone and
 * takes only its OUTPUT - camera.pos, camera.target and this - to blend
 * between two ticks. It is a short and it wraps, so it is blended the short
 * way round like any other angle.
 *
 * A getter and not an extern, because fpRoll is deliberately private to this
 * file: everything that writes it lives here. */
short GetFirstPersonRoll()
{
	return fpRoll;
}

static bool FirstPersonEnabled()
{
	if (!tomb5.first_person)
		return false;

	/* THE VIEW IS THE PLAYED GAME'S AND NOTHING ELSE'S (#126, fix round R).
	 *
	 * FirstPersonActive() - which is what the DRAW asks - already refuses a
	 * flyby and a cutscene, because those SHOW Lara and she must have a head
	 * in them. This is the other half of the same rule and it was missing:
	 * whether the camera itself RUNS.
	 *
	 * It ran in the title. DoTitle calls InitialiseCamera, InitialiseCamera
	 * ends in CalculateCamera, and at that moment bUseSpotCam is still 0 -
	 * the flyby is triggered two lines later. So one full pass of
	 * FirstPersonCamera ran on the title level's Lara: it moved camera.pos
	 * onto her head, took the field of view with AlterFOV, and left fpFovSet
	 * and fpHeadHidden standing. Nothing ever cleared them again, because
	 * from the next frame on the title is a flyby and control.cpp calls
	 * CalculateSpotCams instead of CalculateCamera - the one place either
	 * flag is handed back. One FPHEAD line and two ALTERFOV lines in the
	 * user's log of 2026-09-07, at the title, with first person on.
	 *
	 * gfLevelId is the title's own marker (DoTitle sets it first of all), and
	 * the two below are the same pair FirstPersonActive tests, so the camera
	 * and the draw now answer the same question. */
	if (gfLevelId == LVL5_TITLE || bUseSpotCam || GLOBAL_playing_cutseq)
		return false;

	// Binoculars and laser sight bring their OWN head camera, including
	// their own exit path (ExittingBinos, mesh_bits restore). Interfering
	// in between would break both.
	//
	// ONLY while they really run (BinocularRange). The tail afterwards
	// (BinocularOn < 0, eight frames) used to belong here too - then Lara
	// was briefly seen from outside after leaving the scope (user finding:
	// "1-2 frames in third person"). The tail is just the normal camera's
	// approach; a nailed-on camera does not need one.
	if (BinocularRange)
		return false;

	if (!lara_item)
		return false;

	// On death the camera pulls back by default and shows what happened -
	// from Lara's own eyes her death is a pan into nothing. But the user
	// asked to be able to stay in there, so it is a choice ("Death camera"
	// on the first person page), and off keeps the old behaviour.
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

	// REFRESH the joint matrices before asking for the head.
	//
	// Without this the camera lags one frame behind: CalcLaraMatrices is
	// the first thing in DrawPhaseGame (draw.cpp), i.e. AFTER
	// CalculateCamera - lara_joint_matrices normally still carries the
	// PREVIOUS frame's state here. In calm walking it goes unnoticed, in
	// fast movement it does not: the camera stayed briefly behind Lara, her
	// body pushed through the frame, then it caught up again (seen by the
	// user). Flag 1 is the set GetLaraJointPos reads, and it wants a unit
	// matrix on the stack - exactly as in DrawPhaseGame.
	phd_PushUnitMatrix();
	CalcLaraMatrices(1);
	phd_PopMatrix();

	// Eye point: the head joint, pushed a bit along the view direction so
	// the camera does not sit in the back of her own head.
	head.x = 0;
	head.y = 0;
	head.z = 0;
	GetLaraJointPos(&head, LMX_HEAD);

	// The RAW reading, kept for the trace at the end. Everything below
	// filters it, and a trace of filtered numbers says nothing about where
	// the movement comes from.
	fpRawX = head.x - item->pos.x_pos;
	fpRawY = head.y - item->pos.y_pos;
	fpRawZ = head.z - item->pos.z_pos;

	// Damp the bob in POSITION too, when the camera hangs on the bone.
	//
	// Measured off the user's recording (60 fps, the logic at 30, which the
	// doubled values in the trace show): the horizon moves two to eight
	// pixels per logic frame and turns round on three out of five - a
	// shiver at about fifteen a second, "as if Lara were cold". The
	// rotation filter cannot be what is left of that; at its slow setting
	// it holds an alternation of that speed down to a few per cent. What is
	// left is the eye itself. It sits on the head joint and walks up and
	// down with every step, and nothing was damping that.
	//
	// Smoothed is the head's OFFSET INSIDE LARA, not its place in the
	// world. That offset never leaves her body, so the damped eye stays
	// within a body's width of where it belongs and cannot lag into a wall
	// - her collision radius is 102 - while walking and falling carry it
	// along untouched. The forward offset below still asks mgLOS as before.
	//
	// Only with the bone camera: plain first person had this bob all along
	// and nobody minded, because the view did not turn with it.
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
			// How far the HEAD moved since last frame - not how far the
			// filtered eye has fallen behind it. Same correction as for the
			// direction: a bobbing signal keeps that lag moderate the whole
			// time and the filter then never chooses properly.
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

		// The damped point has to be REACHABLE from the real head.
		//
		// Damping means lagging, and on a fast move the lag lands the eye
		// where the head no longer is. Pulling up over a ledge is the worst
		// of them: the head shoots up past the edge in a couple of frames
		// while the damped point is still below it - inside the floor. The
		// room then resolves to whatever lies under or beyond that surface
		// and the camera is drawn from outside the level. Two frames of the
		// building seen from the void, in the user's recording.
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

	// The forward offset must ASK THE WALL. If Lara stands close to one and
	// looks around, the eye swings around her head - and 48 units are
	// enough to land inside the wall. Then the room number is wrong too
	// (GetFloor finds the room BEYOND the wall), and the renderer draws
	// from there: you see through the wall (found by the user, "with the
	// look key at the right tilt").
	//
	// mgLOS probes the short path from the head center to the eye, pulls
	// the point back in front of the obstacle AND sets the room number
	// correctly while at it - it walks the portals instead of guessing. The
	// head center itself is always a valid room: it sits inside Lara.
	eye.x = head.x;
	eye.y = head.y - 16;
	eye.z = head.z;

	// WHICH ROOM the eye is in has to be worked out, not assumed to be
	// Lara's.
	//
	// The two part company exactly where this went wrong. Pulling up over a
	// ledge, Lara still hangs below and her item sits in the LOWER room
	// while her head is already above the edge, in the upper one. Taking her
	// room for the eye then had everything after it reckoning in the wrong
	// place: the probe walked from a room the eye is not in, and the floor
	// clamp obligingly held the camera forty-eight units above the floor of
	// the room BELOW - which is where the user found it, one storey under
	// her (Andrea1, room 89).
	//
	// GetFloor walks there properly. Starting from Lara's room it follows
	// the portals and the room-above and room-below links to whatever
	// actually contains the point, which is the whole purpose of the
	// function.
	// The eye is reached FROM LARA, not placed and then asked where it is.
	//
	// The head can end up nowhere at all. The trace caught it during a
	// handstand at a ledge: the head at -2194 while the room it resolves to
	// has its ceiling at -1280, so the point lies outside that room
	// altogether - and outside any other. Her head swings over a place the
	// engine cannot assign. From there the renderer draws the void, and no
	// amount of correcting afterwards helps, because every height that comes
	// back belongs somewhere else.
	//
	// Her own position always sits in a real room; the engine keeps it so.
	// mgLOS therefore walks from her body up to the head, follows the
	// portals on the way and stops in front of anything solid, handing back
	// a place that exists AND the room it is in. Where the head is fine, and
	// that is nearly always, it arrives untouched; where it is not, the eye
	// stays a little lower, inside her, which is somewhere real.
	//
	// This replaces asking GetFloor about a point that may not be in any
	// room - a question with no honest answer.
	// mgLOS is a QUESTION, not a placement. It always steps one eighth back
	// from the target at the end and overwrites it, clear path or not:
	//
	//     if (lp) { x -= dx; y -= dy; z -= dz; }
	//     target->x = x; ...
	//
	// Handing it the eye directly therefore parked the camera a good fifty
	// units short of the head - at her chin, with her chest in the picture.
	// The older code beside this uses it as a yes-or-no for exactly that
	// reason and works out the point itself; that is what it is for.
	{
		FLOOR_INFO* fl;
		long h, c;
		short rn;

		// The question is whether the head is somewhere REAL, and that is
		// not the same as whether one can see it from her chest.
		//
		// Asking the sight line was wrong, and pulling up over a ledge shows
		// why: her chest is below the edge and her head above it, so the
		// straight line between them goes through the stone. The probe said
		// "blocked" and threw the eye back to her chest - the camera came
		// over the edge for a moment and was yanked back down, every time.
		//
		// A room contains the head when the head lies between that room's
		// ceiling and its floor. That is the whole test, and it separates
		// the two cases cleanly: pulling up, the head really is inside the
		// room above and is kept; in a handstand it sits above the ceiling
		// of the only room going, and is not.
		// Asked from TWO starting rooms, because GetFloor can only walk the
		// links it is given, and from Lara's room there is not always a way.
		//
		// Traced during a pull-up: the eye stands at -2210, room 89 runs
		// from -4096 to -2048, so it is plainly inside. But the walk starts
		// at Lara's room 88, and at that x/z room 88 has nothing above it -
		// solid stone under the ledge - so it hands back 88's own surfaces,
		// -1280 to -254. The eye is above those, the test says "not inside",
		// and it is right about the answer it was given.
		//
		// One frame earlier the eye WAS in room 89. A camera has continuity
		// and may use it: the previous room is tried first, Lara's second.
		// Either may fail at the edge of its reach; between them they cover
		// the moment of crossing.
		// Belt and braces: a starting room out of range would read room[]
		// past its end. It cannot happen now that InitialiseCamera clears
		// the memory, but a camera must not be the thing that brings a
		// level down.
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
			// Nowhere real. Then back towards her, as far as one can get -
			// and mgLOS is a question, not a placement, so what it hands
			// back is only used HERE, where being short of the head is the
			// point.
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

	// Probe with a SAFETY MARGIN first, only then commit.
	//
	// mgLOS walks in eight steps and steps one back - with a 48-unit offset
	// that leaves only six units of distance to the wall. But the
	// renderer's NEAR PLANE lies at 20 (InitWindow in gamemain.cpp), and it
	// is a surface, not a point: at its corners it reaches even further
	// out. Six units were not enough, with some effort you could still see
	// through the wall (user finding).
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

	// Second pass onto the REAL point: it supplies the room number. With
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

	// The somersault (#134). Body rotation plus head rotation is not the
	// whole truth: item->pos.x_rot does NOT turn over during a backflip -
	// that rotation lives in the animation alone - so the view above stays
	// level while Lara goes over. The head BONE carries it, and hanging the
	// camera on the bone lets the world turn with her.
	//
	// The matrix is a 3x4 with the local axes in its COLUMNS (see
	// GetLaraJointPos, delstuff.cpp:25ff, and what phd_TranslateRel does
	// with it): local +Z - the way a TR mesh faces - is (M02, M12, M22),
	// and local -Y is up, Y growing downward.
	//
	// Off by default and on its own switch, because whether it PLAYS well
	// is a matter of taste and can only be judged in motion.
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

			// Damp the bob. First frame there is nothing to damp against,
			// so the filter is simply loaded with where we are.
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

				// How far the BONE moved since last frame, and IN WHICH
				// DIRECTION - both taken before fpPrevDir is written, which
				// is where this went wrong the first time: the difference
				// was computed against a value already overwritten with the
				// current one, so it was always zero, the consistency term
				// was always zero with it, and k sat at its slow setting in
				// every animation. Measured: 0.300 even at seven degrees a
				// frame with two per cent reversals.
				d = 1.0F - (fpPrevDir[0] * fx + fpPrevDir[1] * fy + fpPrevDir[2] * fz);

				mx = fx - fpPrevDir[0];
				my = fy - fpPrevDir[1];
				mz = fz - fpPrevDir[2];

				fpPrevDir[0] = fx; fpPrevDir[1] = fy; fpPrevDir[2] = fz;
				fpPrevUp[0] = ux; fpPrevUp[1] = uy; fpPrevUp[2] = uz;


				// How consistent the recent movement has been. The step
				// itself is folded into a running average of the SIGNED
				// movement and one of its SIZE; where the two agree the
				// movement is going somewhere.
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

					// BOTH must speak for it: large AND going somewhere.
					// Large alone was what let the stutter through.
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

			// How far the head is tilted SIDEWAYS, measured against the
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
	// braid hangs on this (drawlara.cpp) - mesh_bits is DELIBERATELY NOT
	// TOUCHED here:
	//
	//  - Lara's skin is skinned. A mesh hidden via mesh_bits no longer
	//    stashes its rim points (both stand in the same bracket), and the
	//    neighbor then skins from stale memory: the stretched triangle the
	//    user saw as a "third arm".
	//  - Binoculars and laser sight write mesh_bits as well (0 on entry,
	//    -1 on leaving). Two writers on the same field were the reason the
	//    bug persisted after the scope until a restart.
	//
	// Who gets hidden is now decided solely by the draw routine.
	fpHeadHidden = 1;
	AlterFOV(FIRST_PERSON_FOV);
	fpFovSet = 1;

	// Hold the eye clear of floor and ceiling.
	//
	// The probe further up guards the way AHEAD; nothing guarded up and
	// down. Pulling up over a ledge puts the eye a few units above the floor
	// it is rising past, and the near plane is a SURFACE at twenty units,
	// not a point - at its lower corners it reaches further still, and it
	// then reaches through the floor. Which is why it depends so much on the
	// room: where the ledge is thick or the floor below far away nothing
	// shows, and elsewhere it does.
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

		// A clamp may nudge; it may never MOVE the camera.
		//
		// Measured during a handstand at a ledge: the eye sat correctly at
		// -2210, the room resolved back from 89 to 88, and 88's ceiling
		// there reads -1280 - so this pushed the camera to -1232, nearly a
		// thousand units down, through the floor and into the room below.
		// Which is exactly what the user saw, and it was this line doing it.
		//
		// The head was at -2194, ABOVE that ceiling, so the point is not in
		// room 88 at all: during the handstand her head swings out over a
		// place the engine cannot assign to any room, and the heights that
		// come back then belong somewhere else.
		//
		// So a correction is allowed to be small. Grazing a surface is worth
		// a nudge; anything larger means the reading does not describe where
		// the eye is, and then the honest thing is to leave the camera where
		// the head put it. A little geometry seen oddly beats a camera that
		// jumps a room.
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
	// FPHEAD: one line per frame under -log, and only in first person (#134).
	//
	// BOTH the raw reading and what the camera was actually given, because a
	// trace of one alone cannot say whether a filter is working. Read it so:
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

		// FPROOM was a second line here while the eye's room, the floor clamp
		// and the field of view were all in question. All three are settled,
		// so it is gone: two lines a frame is a lot for a log the user reads
		// for other things. What it measured is written down in the memory
		// note for this camera, and it is a dozen lines to put back.
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

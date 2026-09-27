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
#include "../global/types.h"

void InitialiseCamera();
void CalculateCamera();
void LaraTorch(PHD_VECTOR* Soffset, PHD_VECTOR* Eoffset, short yrot, long brightness);
void ScreenShake(ITEM_INFO* item, short MaxVal, short MaxDist);
long mgLOS(GAME_VECTOR* start, GAME_VECTOR* target, long push);
long CameraCollisionBounds(GAME_VECTOR* ideal, long push, long yfirst);
void UpdateCameraElevation();
void ConfirmCameraTargetPos();
void FixedCamera();
void ChaseCamera(ITEM_INFO* item);
void CombatCamera(ITEM_INFO* item);
void MoveCamera(GAME_VECTOR* ideal, long speed);
void BinocularCamera(ITEM_INFO* item);
void LookCamera(ITEM_INFO* item);
void FirstPersonCamera(ITEM_INFO* item);
bool FirstPersonActive();		// is first person running right now? (head and braid stay hidden then)
short GetFirstPersonRoll();		// the head bone's roll, for the frame interpolator

/* The modern (orbit) camera: the engine's side of it. The orbit's arithmetic
 * is game\modern_camera.h; the comment above these definitions in
 * game\camera.cpp says which accessor serves which caller. The last two are
 * read-only, for MODERNLOG under -log. */
bool ModernCameraActive();		// may the orbit steer the view - not "does it": see ModernCameraWorldYaw
void ModernCameraResetOrbit();		// level start, savegame load, scheme change
void ModernCameraCarryHalfTurn();	// the ledge hang's turn: swing the orbit behind her
void ModernCameraDropSwing();		// the ledge hang's turn broken off: drop the swing's unpaid rest
void ModernCameraStick(long rs_x, long rs_y);	// the right stick, from specific\input.cpp
void ModernCameraMouse(long yaw, long pitch);	// the mouse's two steps for the next tick, from the same place
short ModernCameraSwimTarget();		// the swimming attitude the orbit points at
long ModernCameraWorldYaw(short* yaw);	// where the player put it, in the world - 0 = not seated
					// yet, or the orbit did not tick last tick and the angle is
					// stale (under a rich2 sniper camera)
long ModernCameraPlayerStep(short* step);	// how far the player's own stick and mouse moved it last tick
long ModernLaraEyeDistance();		// eye to camera target, the camera's own two points
bool ModernLaraHidden();		// too close to draw her at all
void ChaseCameraLogPick(long* cand, long* clip);	// ChaseCamera's last pick, for ROPELOG draw
void ModernCameraLogState(short* off_yaw, short* off_pitch, long* ret, long* rs_x, long* rs_y,
	short* world_yaw, long* wall, long* carry);	// MODERNLOG tick line
void ModernCameraLogConfig(long* return_rate, long* return_min, long* wall_factor,
	long* hide_dist, long* invert_pitch, long* pitch_return,
	long* speed, long* yaw_rate, long* pitch_min, long* pitch_max,
	long* pull_steps, long* pull_push);	// MODERNLOG init line

/* Whether a camera point stands in open space, for the drawn eye
 * (game\interpolate.cpp) and CAMLOG. Reads the level and writes nothing: the
 * five globals GetHeight sets are put back before it returns. */
enum
{
	CAMERA_EYE_OPEN,	// a floor and a ceiling, the point between them
	CAMERA_EYE_WALL,	// no floor or no ceiling at all, or they meet
	CAMERA_EYE_DOOR,	// the lookup left the room it was asked from and fails where it arrived
	CAMERA_EYE_BELOW,	// under the floor of its own room by `depth`
	CAMERA_EYE_ABOVE	// over the ceiling of its own room by `depth`
};

struct CAMERA_EYE_TEST
{
	long open;		// 1 for CAMERA_EYE_OPEN
	long kind;		// CAMERA_EYE_*
	long depth;		// units under the floor or over the ceiling, else 0
	short room;		// the room GetFloor reached, whatever the kind
};

long CameraEyeTest(long x, long y, long z, short room, CAMERA_EYE_TEST* out);
void CameraEyeKindText(const CAMERA_EYE_TEST* test, char* buf, long size);	// CAMLOG's kind=

extern CAMERA_INFO camera;
extern SHATTER_ITEM ShatterItem;
extern GAME_VECTOR ForcedFixedCamera;
extern char UseForcedFixedCamera;
extern PHD_VECTOR CamOldPos;
extern camera_type BinocularOldCamera;
extern long BinocularOn;
extern long BinocularRange;
extern long ExittingBinos;
extern long LaserSight;
extern long InfraRed;
extern char SniperOverlay;
extern char SniperCamActive;
extern uchar WeaponDelay;

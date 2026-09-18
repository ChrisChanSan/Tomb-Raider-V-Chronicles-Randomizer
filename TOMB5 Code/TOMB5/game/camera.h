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
short GetFirstPersonRoll();		// the head bone's roll, for the interpolator (#148, order 2)

/* THE MODERN CAMERA (#161, round 161a2). The orbit's arithmetic is
 * game\modern_camera.h; these nine are the engine's side of it, and the long
 * comment above their definitions in game\camera.cpp says which is which. The
 * last two are MODERNLOG's own (step 5) and read nothing the other seven do
 * not already keep - round 161a2b's own step 5 widened both of their
 * parameter lists (the world yaw and the clip flag on the tick line, the
 * speed slider and the pitch clamp on the init line) rather than adding an
 * eighth and ninth accessor, for the same reason: nothing they read is new
 * to this file, only new to what a play test gets told about it. */
bool ModernCameraActive();		// is the orbit steering the view this tick?
void ModernCameraResetOrbit();		// level start, savegame load, scheme change
void ModernCameraStick(long rs_x, long rs_y);	// the right stick, from specific\input.cpp
short ModernCameraSwimTarget();		// the attitude the orbit points at (step 4, the water)
long ModernCameraWorldYaw(short* yaw);	// where the player put it, in the world - 0 = not seated yet (161a2b, item 4b)
long ModernLaraEyeDistance();		// eye to camera target, the camera's own two points
bool ModernLaraHidden();		// too close to draw her at all (observation 4)
void ModernCameraLogState(short* off_yaw, short* off_pitch, long* ret, long* rs_x, long* rs_y,
	short* world_yaw, long* wall);	// MODERNLOG tick line (step 5; world_yaw/wall added 161a2b step 5)
void ModernCameraLogConfig(long* return_rate, long* return_min, long* wall_factor,
	long* hide_dist, long* invert_pitch, long* pitch_return,
	long* speed, long* yaw_rate, long* pitch_min, long* pitch_max);	// MODERNLOG init line (step 5; last four added 161a2b step 5)

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

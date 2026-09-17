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
 * game\modern_camera.h; these five are the engine's side of it, and the long
 * comment above their definitions in game\camera.cpp says which is which. */
bool ModernCameraActive();		// is the orbit steering the view this tick?
void ModernCameraResetOrbit();		// level start, savegame load, scheme change
void ModernCameraStick(long rs_x, long rs_y);	// the right stick, from specific\input.cpp
long ModernLaraEyeDistance();		// eye to camera target, the camera's own two points
bool ModernLaraHidden();		// too close to draw her at all (observation 4)

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

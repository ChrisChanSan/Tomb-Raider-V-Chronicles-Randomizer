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

short* GetBoundsAccurate(ITEM_INFO* item);
short* GetBestFrame(ITEM_INFO* item);
void InitInterpolate(long frac, long rate);

/* The same set-up with a factor of its own - the time interpolation asks for
 * the midpoint of two poses, which no (frac, rate) names. See the note over
 * InitInterpolateAlpha in game\draw.cpp. */
void InitInterpolateAlpha(float alpha);
void phd_PopMatrix_I();
void phd_PushMatrix_I();
void phd_RotY_I(short ang);
void phd_RotX_I(short ang);
void phd_RotZ_I(short ang);
void phd_TranslateRel_I(long x, long y, long z);
void phd_TranslateRel_ID(long x, long y, long z, long x2, long y2, long z2);
void phd_RotYXZ_I(short y, short x, short z);
void gar_RotYXZsuperpack_I(short** pprot1, short** pprot2, long skip);
void gar_RotYXZsuperpack(short** pprot, long skip);
void phd_PutPolygons_I(short* ptr, long clip);
void aInterpolateMatrix();
long DrawPhaseGame();
void SkyDrawPhase();
void UpdateSkyLightning();
void CalculateObjectLighting(ITEM_INFO* item, short* frame);
void DrawAnimatingItem(ITEM_INFO* item);
void PrintObjects(short room_number);

/* The corpse fade, once per logic tick. PrintObjects
 * marks the bodies it drew; this counts them up and kills the ones that are
 * done. ControlPhase calls it - it is game state and belongs on the tick. */
void StepAfterDeathFade();

void DrawRooms(short current_room);
void CalculateObjectLightingLara();
long GetFrames(ITEM_INFO* item, short* frm[], long* rate);

/* The pose in time.
 *
 * GetFramesAt is GetFrames' neighbour for the frame that falls between two
 * logic ticks: given what the item wore before the last tick it returns the
 * key frame pair and the float factor whose blend is the midpoint of the two
 * poses the game draws at thirty. 0 means there is nothing to blend (the
 * animation changed or the frame did not step by exactly one - except the
 * ledge hang's lowering, whose backwards steps InterpPoseBack blends) and
 * nothing was written.
 *
 * GetFramesNow is what the draw sites call: GetFrames on a tick frame and at
 * fps_mode 0, the midpoint on a tween frame. 0 = one frame, 1 = vanilla's
 * key frame blend, 2 = a blend in time.
 *
 * "This frame sits on a stored pose" and "the pose blend was refused" both
 * return 0 on purpose - the call sites branch on it, and a third value would
 * send a refused frame down the interpolated path with a pair it has no
 * factor for. The refused frame's root offset travels beside it instead:
 * GetFramesNow leaves the previous tick's root offset in the slot's own byte
 * of g_poseOk, and the draw picks it up through InterpRootNow
 * (game\interpolate.h). */
/* The arm chain's own blend: collapse the two matrix stacks to the body's
 * blended joint and carry on with the arm's own factor. The note at the
 * definition says why an arm needs a second factor at all. */
void InterpArmStack(float alpha);

long GetFramesAt(ITEM_INFO* item, short prevAnim, short prevFrame, float t,
	short* frm[], float* blend);
long GetFramesNow(ITEM_INFO* item, short* frm[], float* blend);

/* Lara's pose frames as her draw and her braid take them: the pair in `frm`
 * (with GetFramesNow's or GetFrames' `frac` and factor `blend`) replaced by
 * turned copies on the frame an early turn180_effect fires on, and by the
 * first key or a key frame built at `blend` on a far key pair (draw.cpp says
 * which and why). `flag` 0 writes the -log lines. */
void LaraPoseFrames(short* frm[], long frac, float blend, long flag);
void SetupSkelebobMeshswaps();
void RestoreLaraMeshswaps();
void RenderIt(short current_room);
void mRotBoundingBoxNoPersp(short* bounds, short* rotatedBounds);
void PrintRooms(short room_number);
void DrawStaticObjects(short room_number);
void InterpolateMatrix();
void InterpolateArmMatrix(long* mx);
void aInterpolateArmMatrix(float* mx);
void DrawEffect(short fx_num);
void calc_animating_item_clip_window(ITEM_INFO* item, short* bounds);
void SetRoomBounds(short* door, long rn, ROOM_INFO* actualRoom);
void GetRoomBounds();

extern STATIC_INFO static_objects[70];

extern float* aIMXPtr;
extern float aIFMStack[768];
extern long* IMptr;
extern long IMstack[768];
extern long IM_rate;
extern long IM_frac;

/* The blend between the two matrix stacks as one number, and
 * the same number in eighths for the integer stack, which rounds by shifting.
 * Set by InitInterpolate / InitInterpolateAlpha / InitInterpolate2. */
extern float IM_alpha;
extern long IM_alpha8;

extern long CurrentRoom;
extern long outside;
extern short SkyPos;
extern short SkyPos2;
extern ushort LightningRGB[3];
extern  ushort LightningRGBs[3];
extern short no_rotation[12];

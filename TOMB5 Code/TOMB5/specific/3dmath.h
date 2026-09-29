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

void AlterFOV(short fov);
void aSetViewMatrix();
void phd_PushMatrix();
void phd_PushUnitMatrix();
void phd_PopMatrix();
void phd_SetTrans(long x, long y, long z);
void phd_TranslateRel(long x, long y, long z);
void phd_TranslateAbs(long x, long y, long z);
void phd_RotX(short angle);
void phd_RotY(short angle);
void phd_RotZ(short angle);
void phd_RotYXZ(short y, short x, short z);
void phd_RotYXZpack(long angles);
void ScaleCurrentMatrix(PHD_VECTOR* vec);
void phd_GetVectorAngles(long x, long y, long z, short* angles);
void phd_GenerateW2V(PHD_3DPOS* viewPos);
void phd_LookAt(long xsrc, long ysrc, long zsec, long xtar, long ytar, long ztar, short roll);

// POISONLOG (-log): who is building the view, and the base a drawn frame
// starts from. The two setters only mark the caller; see 3dmath.cpp.
void PoisonLogRebuild(bool on);
void PoisonLogTickCamera(bool on);
void PoisonLogFrame(float alpha);

// The poison sway's six phases for the next view builds, in the order
// XSoff1, YSoff1, ZSoff1, XSoff2, YSoff2, ZSoff2, copied; NULL returns them
// to the globals. Only the interpolator's frame rebuild sets them.
void PoisonSwayBlend(const short* phases);

void SetupZRange(long znear, long zfar);
void InitWindow(long x, long y, long w, long h, long znear, long zfar, long fov, long a, long b);
long phd_atan(long x, long y);
ulong phd_sqrt(ulong num);
ulong mGetAngle(long x, long z, long x1, long z1);

extern PHD_VECTOR CamPos;
extern SVECTOR CamRot;
extern short CamRoll;	// the roll phd_LookAt last built the view with
extern long ViewSerial;	// how many views phd_LookAt has built

extern float one;
extern float mone;

extern float f_centerx;
extern float f_centery;
extern float f_top;
extern float f_left;
extern float f_bottom;
extern float f_right;
extern float f_znear;
extern float f_zfar;
extern float f_mznear;
extern float f_mzfar;
extern float f_persp;
extern float f_mpersp;
extern float f_oneopersp;
extern float f_moneopersp;
extern float f_perspoznear;
extern float f_mperspoznear;
extern float f_moneoznear;
extern float f_a;
extern float f_b;
extern float f_boo;

extern float fcossin_tbl[65536];

extern float* aMXPtr;
// The world-to-view matrix in floats - rotation in M00..M22, the camera's
// world position in M03 / M13 / M23. The float twin of w2v_matrix below.
extern float aW2VMx[indices_count];
extern float aFMatrixStack[20 * indices_count];

extern long* phd_mxptr;
extern long w2v_matrix[indices_count];
extern long matrix_stack[20 * indices_count];

extern long phd_winheight;
extern long phd_winwidth;
extern long phd_centerx;
extern long phd_centery;
extern long phd_top;
extern long phd_left;
extern long phd_bottom;
extern long phd_right;
extern long phd_znear;
extern long phd_zfar;
extern long phd_persp;
extern short phd_winxmax;
extern short phd_winxmin;
extern short phd_winymax;
extern short phd_winymin;

__inline short phd_sin(long angle)
{
	angle >>= 3;
	return 4 * rcossin_tbl[angle & 0x1FFE];
}

__inline short phd_cos(long angle)
{
	angle >>= 3;
	return 4 * rcossin_tbl[(angle & 0x1FFE) + 1];
}

__inline float fSin(long angle)
{
	return fcossin_tbl[(ushort)angle];
}

__inline float fCos(long angle)
{
	return fcossin_tbl[ushort(angle + 0x4000)];
}

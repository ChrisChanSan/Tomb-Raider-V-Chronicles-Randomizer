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

/* The texture memory the device offered the moment it was made, in bytes
 * (#123, stage S6). Zero when the device did not answer. The HD texture
 * budget is worked out from it; nothing else reads it. See the note in
 * DXCreateD3DDevice for why it is taken there and not later. */
extern ulong DXTextureMemoryAtStart;

void DXReadKeyboard(char* KeyMap);
void DXBitMask2ShiftCnt(ulong mask, uchar* shift, uchar* count);
long DXAttempt(HRESULT r);
long DIAttempt(HRESULT r);
void* AddStruct(void* p, long num, long size);
long DXCreateD3DDevice(LPDIRECT3D9 d3d, UINT adapter, HWND hwnd, D3DPRESENT_PARAMETERS* pp, LPDIRECT3DDEVICE9* device);
void DXSaveScreen(LPDIRECT3DSURFACE9 surf, const char* name);
HRESULT DXShowFrame();
void DXMove(long x, long y);
void DXClose();
long DXCreate(long w, long h, long bpp, long Flags, DXPTR* dxptr, HWND hWnd, long WindowStyle);
/* THE PRESENT AND THE PANEL (#148, order 6).
 *
 * DXVSyncActive() is 1 only when the device that exists was really made with
 * D3DPRESENT_INTERVAL_ONE - the option asked for it AND the adapter offered
 * it. The refresh-bound frame rate hangs on this and not on tomb5.vsync,
 * because without a blocking present nothing would bound the loop.
 *
 * DXDisplayHz() is the adapter's current display mode refresh rate, or 60
 * when it reports something unusable. Both are written by DXCreate. */
long DXVSyncActive();
long DXDisplayHz();

long DXChangeVideoMode();
long DXToggleFullScreen();
long DXGetInfo(DXINFO* dxinfo, HWND hwnd);
void DXFreeInfo(DXINFO* dxinfo);
long DXUpdateJoystick();
void DXInitInput(HWND hwnd, HINSTANCE hinstance);
// Must be called before lpDirectInput is released - the joystick
// enumeration runs in its own thread, see DXInitJoystick.
void DXWaitJoystickInit();
const char* DXGetErrorString(HRESULT hr);
const char* DIGetErrorString(HRESULT hr);

extern DXPTR* G_dxptr;
extern DXINFO* G_dxinfo;
extern LPDIRECT3D9 G_d3d;
extern HWND G_hwnd;
extern char keymap[256];

#pragma once
#include "../global/types.h"

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
long DXChangeVideoMode();
long DXToggleFullScreen();
BOOL __stdcall DXEnumDirectSound(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext);
long DXGetInfo(DXINFO* dxinfo, HWND hwnd);
void DXFreeInfo(DXINFO* dxinfo);
long DXUpdateJoystick();
void DXInitInput(HWND hwnd, HINSTANCE hinstance);
// Muss vor dem Freigeben von lpDirectInput aufgerufen werden - die
// Joystick-Aufzaehlung laeuft in einem eigenen Thread, siehe DXInitJoystick.
void DXWaitJoystickInit();
const char* DXGetErrorString(HRESULT hr);
const char* DIGetErrorString(HRESULT hr);

extern DXPTR* G_dxptr;
extern DXINFO* G_dxinfo;
extern LPDIRECT3D9 G_d3d;
extern HWND G_hwnd;
extern char keymap[256];

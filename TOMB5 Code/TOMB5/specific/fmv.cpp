#include "../tomb5/pch.h"
// Bink's video path (BinkCopyToBuffer/BinkDDSurfaceType) needs a real DirectDrawSurface -
// that's still the least-risky way to feed it, so FMV keeps a small standalone DirectDraw
// object/surface here, decoupled from DXPTR (which is D3D9-only now). Nothing else in the
// game touches DirectDraw. ddraw.h doesn't redeclare any of d3d9.h's types, so it's safe to
// include alongside it.
#include <ddraw.h>
#include "fmv.h"
#include "dxshell.h"
#include "audio.h"
#include "function_stubs.h"
#include "lighting.h"
#include "function_table.h"
#include "winmain.h"
#include "input.h"
#include "3dmath.h"
#include "../game/text.h"
#include "d3dmatrix.h"
#include "dxsound.h"
#include "gamemain.h"
#include "LoadSave.h"
#include "setupdlg.h"

#define GET_DLL_PROC(dll, proc, n) \
{ \
	*(FARPROC *)&(proc) = GetProcAddress((dll), n); \
	if(!proc) throw #proc; \
}

static void(__stdcall* BinkCopyToBuffer)(BINK_STRUCT*, LPVOID, LONG, long, long, long, long);
static void(__stdcall* BinkOpenDirectSound)(ulong);
static void(__stdcall* BinkSetSoundSystem)(LPVOID, LPDIRECTSOUND);
static LPVOID(__stdcall* BinkOpen)(char*, ulong);
static long(__stdcall* BinkDDSurfaceType)(LPDIRECTDRAWSURFACE4);
static long(__stdcall* BinkDoFrame)(BINK_STRUCT*);
static void(__stdcall* BinkNextFrame)(BINK_STRUCT*);
static long(__stdcall* BinkWait)(BINK_STRUCT*);
static void(__stdcall* BinkClose)(BINK_STRUCT*);
static HMODULE hBinkW32;

static long nFmvFrames[9] = { 880, 1826, 3869, 3112, 1903, 1973, 3200, 2799, 1725 };

static BINK_STRUCT* Bink;
static LPDIRECTDRAWSURFACE4 BinkSurface;
static long BinkSurfaceType;
static LPDIRECTDRAW4 g_fmvDD;
// A real D3D9 texture Bink frames get copied into and then drawn as a full-screen quad.
// BinkSurface (above) is kept around purely so BinkDDSurfaceType() can tell Bink what pixel
// format to write - GDI blitting (the original approach here) is invisible while the D3D9
// device holds true exclusive fullscreen, since the display is scanned out directly from the
// D3D backbuffer in that mode; drawing through the device itself works in both modes.
static LPDIRECT3DTEXTURE9 BinkTexture;

bool LoadBinkStuff()
{
	LPDIRECTDRAW pDD;
	DDSURFACEDESC2 desc;

	hBinkW32 = LoadLibrary("binkw32.dll");

	if (!hBinkW32)
		return 0;

	try
	{
		GET_DLL_PROC(hBinkW32, BinkCopyToBuffer, "_BinkCopyToBuffer@28");
		GET_DLL_PROC(hBinkW32, BinkOpenDirectSound, "_BinkOpenDirectSound@4");
		GET_DLL_PROC(hBinkW32, BinkSetSoundSystem, "_BinkSetSoundSystem@8");
		GET_DLL_PROC(hBinkW32, BinkOpen, "_BinkOpen@8");
		GET_DLL_PROC(hBinkW32, BinkDDSurfaceType, "_BinkDDSurfaceType@4");
		GET_DLL_PROC(hBinkW32, BinkDoFrame, "_BinkDoFrame@4");
		GET_DLL_PROC(hBinkW32, BinkNextFrame, "_BinkNextFrame@4");
		GET_DLL_PROC(hBinkW32, BinkWait, "_BinkWait@4");
		GET_DLL_PROC(hBinkW32, BinkClose, "_BinkClose@4");
	}
	catch (LPCTSTR)
	{
		FreeLibrary(hBinkW32);
		hBinkW32 = 0;
		return 0;
	}

	// A small standalone, never-presented DirectDraw object + one plain system-memory
	// offscreen surface, purely so Bink has a real DirectDrawSurface to write video frames
	// into (via the proven BinkCopyToBuffer/BinkDDSurfaceType path). Non-exclusive, no
	// display-mode dependency - completely decoupled from the D3D9 rendering device.
	if (SUCCEEDED(DirectDrawCreate(0, &pDD, 0)))
	{
		pDD->QueryInterface(IID_IDirectDraw4, (void**)&g_fmvDD);
		pDD->Release();
	}

	if (g_fmvDD)
	{
		// LoadBinkStuff() runs before DXCreate(), so App.dx.hWnd isn't set yet - use the
		// WinMain-level App.hWnd (already valid at this point) instead.
		g_fmvDD->SetCooperativeLevel(App.hWnd, DDSCL_NORMAL);
		memset(&desc, 0, sizeof(desc));
		desc.dwSize = sizeof(DDSURFACEDESC2);
		desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		desc.dwWidth = 640;
		desc.dwHeight = 480;
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		g_fmvDD->CreateSurface(&desc, &BinkSurface, 0);
	}

	if (!BinkSurface)
	{
		Log("LoadBinkStuff: failed to create the DirectDraw FMV surface");
		return 0;
	}

	return 1;
}

void FreeBinkStuff()
{
	if (BinkTexture)
	{
		BinkTexture->Release();
		BinkTexture = 0;
	}

	if (BinkSurface)
	{
		BinkSurface->Release();
		BinkSurface = 0;
	}

	if (g_fmvDD)
	{
		g_fmvDD->Release();
		g_fmvDD = 0;
	}

	if (hBinkW32)
	{
		FreeLibrary(hBinkW32);
		hBinkW32 = 0;
	}
}

static void DrawBinkQuad()
{
	D3DTLBUMPVERTEX v[6];
	float w, h;

	w = (float)App.dx.dwRenderWidth;
	h = (float)App.dx.dwRenderHeight;
	memset(v, 0, sizeof(v));

	v[0].sx = 0; v[0].sy = 0; v[0].tu = 0; v[0].tv = 0;
	v[1].sx = w; v[1].sy = 0; v[1].tu = 1; v[1].tv = 0;
	v[2].sx = w; v[2].sy = h; v[2].tu = 1; v[2].tv = 1;
	v[3].sx = 0; v[3].sy = 0; v[3].tu = 0; v[3].tv = 0;
	v[4].sx = w; v[4].sy = h; v[4].tu = 1; v[4].tv = 1;
	v[5].sx = 0; v[5].sy = h; v[5].tu = 0; v[5].tv = 1;

	for (int i = 0; i < 6; i++)
	{
		v[i].sz = 0.5F;
		v[i].rhw = 1.0F;
		v[i].color = 0xFFFFFFFF;
		v[i].specular = 0xFF000000;
	}

	App.dx.lpD3DDevice->SetTexture(0, BinkTexture);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ZENABLE, 0);
	HWDrawPrimitive(D3DPT_TRIANGLELIST, v, 6);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ZENABLE, 1);
}

void ShowBinkFrame()
{
	D3DLOCKED_RECT rect;

	if (!BinkSurface || !BinkTexture)
		return;

	if (FAILED(BinkTexture->LockRect(0, &rect, 0, 0)))
		return;

	BinkCopyToBuffer(Bink, rect.pBits, rect.Pitch, Bink->num, 0, 0, BinkSurfaceType);
	BinkTexture->UnlockRect(0);

	_BeginScene();
	App.dx.lpD3DDevice->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.0F, 0);
	DrawBinkQuad();
	_EndScene();
	DXShowFrame();
}

long PlayFmv(long num)
{
	char name[80];
	char path[80];

	if (MainThread.ended)
		return 0;

	/*
	if ((1 << num) & FmvSceneTriggered)
		return 1;

	FmvSceneTriggered |= 1 << num;
	*/
	S_CDStop();

	if (fmvs_disabled)
		return 0;

	sprintf(name, "movie\\fmv%01d.bik", num);
	memset(path, 0, sizeof(path));
	strcat(path, name);
	Log("PlayFMV %s", path);
	App.fmv = 1;

	// LoadBinkStuff() runs before DXCreate(), so the D3D9 device didn't exist yet - create
	// the playback texture lazily here instead, the first time it's actually needed.
	if (!BinkTexture)
		App.dx.lpD3DDevice->CreateTexture(640, 480, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &BinkTexture, 0);

	Bink = 0;
	BinkSetSoundSystem(BinkOpenDirectSound, App.dx.lpDS);
	Bink = (BINK_STRUCT*)BinkOpen(path, 0);
	BinkSurfaceType = BinkDDSurfaceType(BinkSurface);

	if (Bink)
	{
		Log("Entering Bink Loop");
		BinkDoFrame(Bink);
		S_UpdateInput();

		for (int i = 0; i != nFmvFrames[num]; i++)
		{
			// IN_SELECT added so Cross/Enter also skips FMVs, not just Escape
			// (IN_OPTION) or Draw Weapon (IN_DRAW) - matches Cross already being
			// the confirm button everywhere else in the menus.
			if (input & IN_OPTION || input & IN_DRAW || input & IN_SELECT || MainThread.ended)
				break;

			BinkNextFrame(Bink);

			while (BinkWait(Bink));

			ShowBinkFrame();
			BinkDoFrame(Bink);
			S_UpdateInput();
		}


		BinkClose(Bink);
		Bink = 0;
	}
	else
		Log("FAILED TO CREATE BINK OBJECT");

	DXChangeOutputFormat(sfx_frequencies[SoundQuality], 1);
	HWInitialise();
	ClearSurfaces();
	App.fmv = 0;
	return 0;
}

long PlayFmvNow(long num, long u)
{
	switch (num)
	{
	case 0:
		PlayFmv(0);
		break;

	case 1:
		PlayFmv(1);
		break;

	case 2:
		PlayFmv(2);
		break;

	case 3:
		PlayFmv(3);
		PlayFmv(4);
		break;

	case 4:
		PlayFmv(5);
		PlayFmv(6);
		break;

	case 5:
		PlayFmv(7);
		break;

	case 6:
		PlayFmv(8);
		break;
	}

	return 0;
}

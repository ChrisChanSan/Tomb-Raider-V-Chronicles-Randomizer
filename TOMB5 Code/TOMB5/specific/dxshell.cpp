#include "../tomb5/pch.h"
#include "dxshell.h"
#include "function_stubs.h"
#include "function_table.h"
#include "winmain.h"
#include "input.h"
#include <Xinput.h>

DXPTR* G_dxptr;
DXINFO* G_dxinfo;
LPDIRECT3D9 G_d3d;
HWND G_hwnd;
char keymap[256];

static char tga_header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 1, 0, 1, 16, 0 };

void DXReadKeyboard(char* KeyMap)
{
	while FAILED(G_dxptr->Keyboard->GetDeviceState(256, KeyMap))
	{
		if FAILED(G_dxptr->Keyboard->Acquire())
		{
			memset(KeyMap, 0, 256);
			break;
		}
	}
}

void DXBitMask2ShiftCnt(ulong mask, uchar* shift, uchar* count)
{
	uchar i;

	for (i = 0; !(mask & 1); i++)
		mask >>= 1;

	*shift = i;

	for (i = 0; mask & 1; i++)
		mask >>= 1;

	*count = i;
}

long DXAttempt(HRESULT r)
{
	if (SUCCEEDED(r))
		return DD_OK;

	Log("ERROR : %s", DXGetErrorString(r));
	return DD_FALSE;
}

long DIAttempt(HRESULT r)
{
	if (SUCCEEDED(r))
		return DI_OK;

	Log("ERROR : %s", DIGetErrorString(r));
	return r;
}

void* AddStruct(void* p, long num, long size)
{
	void* ptr;

	if (!num)
		ptr = malloc(size);
	else
		ptr = realloc(p, size * (num + 1));

	memset((char*)ptr + size * num, 0, size);
	return ptr;
}

long DXCreateD3DDevice(LPDIRECT3D9 d3d, UINT adapter, HWND hwnd, D3DPRESENT_PARAMETERS* pp, LPDIRECT3DDEVICE9* device)
{
	D3DCAPS9 caps;
	DWORD flags;

	Log(__FUNCTION__);

	if (FAILED(d3d->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps)))
	{
		Log("DXCreateD3DDevice Failed - GetDeviceCaps");
		return 0;
	}

	flags = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? D3DCREATE_HARDWARE_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	// LoadTextures() (specific/file.cpp, called from LoadLevel on the background
	// LevelLoadingThread) calls CreateTexture/LockRect on this same device while
	// the main thread concurrently renders the loading bar (SetRenderState,
	// BeginScene, DrawPrimitive, Present) on it - a genuine cross-thread D3D9
	// device race with no flag, undefined behavior per the D3D9 docs. cdb caught
	// the main thread stuck for good inside the driver's Present() four separate
	// times, always and only during level loading (the only time a second thread
	// ever touches the device) - this is what was actually happening, not a
	// present-call-rate or DWM issue. D3DCREATE_MULTITHREADED makes D3D9 take an
	// internal critical section around every device call itself, which is the
	// officially supported way to share one device across threads like this.
	flags |= D3DCREATE_MULTITHREADED;

	if (DXAttempt(d3d->CreateDevice(adapter, D3DDEVTYPE_HAL, hwnd, flags, pp, device)) != DD_OK)
	{
		Log("DXCreateD3DDevice Failed");
		return 0;
	}

	Log("DXCreateD3DDevice Successful");
	return 1;
}

void DXSaveScreen(LPDIRECT3DSURFACE9 surf, const char* name)
{
	FILE* file;
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT rect;
	short* pSurf;
	short* pDest;
	char* pM;
	ulong val;
	static long num = 0;
	char buf[16];

	if (FAILED(surf->GetDesc(&desc)))
		return;

	if (FAILED(surf->LockRect(&rect, 0, D3DLOCK_READONLY)))
		return;

	pSurf = (short*)rect.pBits;
	sprintf(buf, "%s%04d.tga", name, num);
	num++;
	file = fopen(buf, "wb");

	if (file)
	{
		*(short*)&tga_header[12] = (short)desc.Width;
		*(short*)&tga_header[14] = (short)desc.Height;
		fwrite(tga_header, sizeof(tga_header), 1, file);
		pM = (char*)malloc(2 * desc.Width * desc.Height);
		pDest = (short*)pM;
		pSurf += desc.Height * (rect.Pitch / 2);

		for (ulong h = 0; h < desc.Height; h++)
		{
			for (ulong w = 0; w < desc.Width; w++)
			{
				val = pSurf[w];
				*pDest++ = (short)val;
			}

			pSurf -= rect.Pitch / 2;
		}

		fwrite(pM, 2 * desc.Width * desc.Height, 1, file);
		fclose(file);
		free(pM);
		buf[7]++;

		if (buf[7] > '9')
		{
			buf[7] = '0';
			buf[6]++;
		}
	}

	surf->UnlockRect();
}

HRESULT DXShowFrame()
{
	HRESULT hr;

	if (keymap[DIK_APOSTROPHE])
	{
		LPDIRECT3DSURFACE9 back, sysMem;
		D3DSURFACE_DESC desc;

		if (SUCCEEDED(G_dxptr->lpD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		{
			back->GetDesc(&desc);

			if (SUCCEEDED(G_dxptr->lpD3DDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sysMem, 0)))
			{
				if (SUCCEEDED(G_dxptr->lpD3DDevice->GetRenderTargetData(back, sysMem)))
					DXSaveScreen(sysMem, "Tomb");

				sysMem->Release();
			}

			back->Release();
		}
	}

	hr = G_dxptr->lpD3DDevice->Present(0, 0, 0, 0);

	if (hr == D3DERR_DEVICELOST)
	{
		// "Fullscreen" now always runs as a border-less windowed device (see DXCreate),
		// which never loses the device on focus changes - this path is only a defensive
		// fallback for other, rarer causes of device loss (display mode changed by
		// another app, monitor unplugged, etc). Kept minimal since it's not the normal
		// case anymore; see git history for the fuller diagnostic version used while
		// tracking down the original exclusive-fullscreen alt-tab bug this replaced.
		HRESULT hrCoopLevel = G_dxptr->lpD3DDevice->TestCooperativeLevel();

		if (hrCoopLevel == D3DERR_DEVICENOTRESET)
		{
			Log("Device lost, resetting");

			HRESULT hrReset = G_dxptr->lpD3DDevice->Reset(&G_dxptr->PresentParams);

			if (SUCCEEDED(hrReset))
			{
				// Reset() implicitly discards all render/sampler states (back to
				// D3D9 defaults) and recreates the swap chain - HWInitialise() is
				// the same call used at startup and after FMV playback to
				// (re)establish the render state this engine relies on. Textures
				// are D3DPOOL_MANAGED (see specific/texture.cpp) so D3D9 restores
				// their contents automatically - nothing to redo there.
				Log("Device reset OK");
				HWInitialise();
			}
			else
				Log("Device reset failed: %s (0x%08X)", DXGetErrorString(hrReset), (unsigned)hrReset);
		}
	}
	else
		DXAttempt(hr);

	return hr;
}

void DXMove(long x, long y)
{
	Log("DXMove : x %d y %d", x, y);

	if (G_dxptr && !(G_dxptr->Flags & DXF_FULLSCREEN))
		SetRect(&G_dxptr->rScreen, x, y, x + G_dxptr->dwRenderWidth, y + G_dxptr->dwRenderHeight);
}

void DXClose()
{
	Log(__FUNCTION__);

	if (!G_dxptr)
		return;

	if (G_dxptr->lpD3DDevice)
	{
		Log("Released %s @ %x - RefCnt = %d", "Direct3DDevice", G_dxptr->lpD3DDevice, G_dxptr->lpD3DDevice->Release());
		G_dxptr->lpD3DDevice = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "Direct3DDevice");
}

// D3D9 owns/manages the swap chain (back buffer + depth/stencil) internally via
// D3DPRESENT_PARAMETERS - no separate DirectDraw primary/back/z surfaces or a
// viewport object are needed anymore, unlike the original DirectDraw4/Direct3D3 path.
long DXCreate(long w, long h, long bpp, long Flags, DXPTR* dxptr, HWND hWnd, long WindowStyle)
{
	DXDISPLAYMODE* dm;
	RECT r;
	UINT adapter;

	Log(__FUNCTION__);
	G_dxptr = dxptr;
	G_dxptr->Flags = Flags;
	G_dxptr->hWnd = hWnd;
	G_dxptr->WindowStyle = WindowStyle;

	DXClose();

	if (!G_d3d)
		return 0;

	G_dxptr->lpD3D = G_d3d;
	adapter = G_dxinfo->DDInfo[G_dxinfo->nDD].AdapterOrdinal;
	dm = &G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].DisplayModes[G_dxinfo->nDisplayMode];

	if (!(Flags & DXF_FULLSCREEN))
	{
		D3DDISPLAYMODE curMode;

		Log("DXCreate: Windowed Mode");
		r.top = 0;
		r.left = 0;

		// The enumerated exclusive-fullscreen mode list (dm->w/h) can be considerably
		// shorter than what a monitor actually supports (seen on multi-monitor systems
		// where the driver reports a much smaller max mode for one output than another,
		// identical, monitor) - a window doesn't need a real exclusive-fullscreen-capable
		// mode to exist, so size it off the adapter's actual current desktop resolution
		// instead, sidestepping that limitation entirely.
		if (SUCCEEDED(G_d3d->GetAdapterDisplayMode(adapter, &curMode)))
		{
			r.right = curMode.Width;
			r.bottom = curMode.Height;
		}
		else
		{
			r.right = dm->w;
			r.bottom = dm->h;
		}

		AdjustWindowRect(&r, WindowStyle, 0);
		SetWindowPos(hWnd, 0, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
		GetClientRect(hWnd, &G_dxptr->rViewport);
		GetClientRect(hWnd, &G_dxptr->rScreen);
		ClientToScreen(hWnd, (LPPOINT)&G_dxptr->rScreen);
		ClientToScreen(hWnd, (LPPOINT)&G_dxptr->rScreen.right);
		G_dxptr->dwRenderWidth = G_dxptr->rViewport.right;
		G_dxptr->dwRenderHeight = G_dxptr->rViewport.bottom;
		Log("w %d h %d", G_dxptr->dwRenderWidth, G_dxptr->dwRenderHeight);
	}
	else
	{
		// Real D3D9 exclusive fullscreen has proven unreliable at recovering from
		// device loss on alt-tab on this system: TestCooperativeLevel() has been
		// observed staying at D3DERR_DEVICELOST forever after tabbing back in, even
		// once the window is genuinely focused/visible/foreground again by every
		// Win32 measure, with or without Windows' fullscreen optimizations enabled
		// (extensively diagnosed via live Present()/TestCooperativeLevel result
		// tracing). Sidestep the whole failure class instead of chasing it further:
		// run a border-less window sized to exactly cover the target monitor at its
		// current desktop resolution, with the D3D9 device created in *windowed*
		// mode - windowed-mode devices never lose the device on focus changes at
		// all, and a border-less window filling the screen is visually
		// indistinguishable from real exclusive fullscreen. WinSetStyle() already
		// strips the window border (WS_POPUP) for DXF_FULLSCREEN, so only
		// positioning/sizing and PresentParams.Windowed (below) need to change here.
		// One behavioural consequence: since there's no real mode-switch anymore,
		// "fullscreen" always renders at the monitor's current desktop resolution
		// regardless of which entry is selected in the resolution dropdown - the
		// same way windowed mode (above) already works.
		Log("DXCreate: Full Screen Mode (borderless windowed)");

		D3DDISPLAYMODE curMode;
		HMONITOR hMonitor = G_d3d->GetAdapterMonitor(adapter);
		MONITORINFO mi;

		mi.cbSize = sizeof(mi);

		if (hMonitor && GetMonitorInfo(hMonitor, &mi))
			r = mi.rcMonitor;
		else
		{
			r.left = 0;
			r.top = 0;
			r.right = dm->w;
			r.bottom = dm->h;
		}

		if (SUCCEEDED(G_d3d->GetAdapterDisplayMode(adapter, &curMode)))
		{
			G_dxptr->dwRenderWidth = curMode.Width;
			G_dxptr->dwRenderHeight = curMode.Height;
		}
		else
		{
			G_dxptr->dwRenderWidth = dm->w;
			G_dxptr->dwRenderHeight = dm->h;
		}

		G_dxptr->rViewport.top = 0;
		G_dxptr->rViewport.left = 0;
		G_dxptr->rViewport.right = G_dxptr->dwRenderWidth;
		G_dxptr->rViewport.bottom = G_dxptr->dwRenderHeight;
		SetWindowPos(hWnd, HWND_TOP, r.left, r.top, G_dxptr->dwRenderWidth, G_dxptr->dwRenderHeight, SWP_SHOWWINDOW);
		ShowWindow(hWnd, SW_SHOW);
		SetForegroundWindow(hWnd);
	}

	memset(&G_dxptr->PresentParams, 0, sizeof(D3DPRESENT_PARAMETERS));
	G_dxptr->PresentParams.Windowed = TRUE;	// always - see the border-less-windowed comment above
	G_dxptr->PresentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
	G_dxptr->PresentParams.hDeviceWindow = hWnd;
	G_dxptr->PresentParams.BackBufferWidth = G_dxptr->dwRenderWidth;
	G_dxptr->PresentParams.BackBufferHeight = G_dxptr->dwRenderHeight;
	G_dxptr->PresentParams.BackBufferFormat = D3DFMT_X8R8G8B8;
	G_dxptr->PresentParams.BackBufferCount = 1;
	G_dxptr->PresentParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	if (Flags & DXF_ZBUFFER)
	{
		G_dxptr->PresentParams.EnableAutoDepthStencil = TRUE;
		G_dxptr->PresentParams.AutoDepthStencilFormat = D3DFMT_D24S8;
	}

	// Leave FullScreen_RefreshRateInHz at 0 (let the driver pick the display's current rate)
	// rather than forcing dm->RefreshRate - on Windows 11, forcing an exact refresh rate is
	// what's actually causing D3DERR_INVALIDCALL from CreateDevice at some resolutions.

	if (!DXCreateD3DDevice(G_dxptr->lpD3D, adapter, hWnd, &G_dxptr->PresentParams, &G_dxptr->lpD3DDevice))
	{
		DXClose();
		return 0;
	}

	return 1;
}

long DXChangeVideoMode()
{
	long val;

	Log(__FUNCTION__);
	G_dxptr->Flags |= DXF_NOFREE;
	val = DXCreate(0, 0, 0, G_dxptr->Flags, G_dxptr, G_dxptr->hWnd, G_dxptr->WindowStyle);
	G_dxptr->Flags ^= DXF_NOFREE;
	Log("Exited DXChangeVideoMode %d", val);
	return val;
}

long DXToggleFullScreen()
{
	DXDISPLAYMODE* dm;

	Log(__FUNCTION__);

	if (G_dxptr->Flags & DXF_WINDOWED)
	{
		Log("Switching To Full Screen");
		G_dxptr->Flags ^= DXF_WINDOWED;
		G_dxptr->Flags |= DXF_NOFREE | DXF_FULLSCREEN;
	}
	else
	{
		Log("Switching To A Window");
		G_dxptr->Flags ^= DXF_FULLSCREEN;
		G_dxptr->Flags |= DXF_NOFREE | DXF_WINDOWED;
	}

	dm = &G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].DisplayModes[G_dxinfo->nDisplayMode];
	DXCreate(dm->w, dm->h, dm->bpp, G_dxptr->Flags, G_dxptr, G_dxptr->hWnd, G_dxptr->WindowStyle);
	WinSetStyle(G_dxptr->Flags & DXF_FULLSCREEN, G_dxptr->WindowStyle);
	G_dxptr->Flags ^= DXF_NOFREE;
	return 1;
}

// D3D9 adapters are enumerated directly (GetAdapterCount/GetAdapterIdentifier/EnumAdapterModes) -
// no more DirectDraw device -> Direct3D-Immediate-Mode device -> display mode 3-level callback
// enumeration. The DXDIRECTDRAWINFO/DXD3DDEVICE/DXDISPLAYMODE tree shape is kept unchanged so
// setupdlg.cpp/registry.cpp don't need to change, only how it's populated (see DXGetInfo below).

BOOL __stdcall DXEnumDirectSound(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext)
{
	DXINFO* dxinfo;
	DXDIRECTSOUNDINFO* DSInfo;
	long nDSInfo;

	Log(__FUNCTION__);
	dxinfo = (DXINFO*)lpContext;
	nDSInfo = dxinfo->nDSInfo;
	dxinfo->DSInfo = (DXDIRECTSOUNDINFO*)AddStruct(dxinfo->DSInfo, nDSInfo, sizeof(DXDIRECTSOUNDINFO));
	DSInfo = &dxinfo->DSInfo[nDSInfo];

	if (lpGuid)
	{
		DSInfo->lpGuid = &DSInfo->Guid;
		DSInfo->Guid = *lpGuid;
	}
	else
		DSInfo->lpGuid = 0;

	lstrcpy(DSInfo->About, lpcstrDescription);
	lstrcpy(DSInfo->Name, lpcstrModule);
	Log("Found - %s %s", lpcstrDescription, lpcstrModule);
	dxinfo->nDSInfo++;
	return DDENUMRET_OK;
}

long DXGetInfo(DXINFO* dxinfo, HWND hwnd)
{
	DXDIRECTDRAWINFO* ddi;
	DXD3DDEVICE* device;
	DXDISPLAYMODE* dm;
	D3DDISPLAYMODE mode;
	D3DCAPS9 caps;
	UINT nAdapters, nModes;

	Log(__FUNCTION__);
	G_hwnd = hwnd;
	G_dxinfo = dxinfo;

	G_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!G_d3d)
	{
		Log("Direct3DCreate9 Failed");
		return 0;
	}

	nAdapters = G_d3d->GetAdapterCount();
	Log("Enumerating Display Adapters");

	for (UINT i = 0; i < nAdapters; i++)
	{
		dxinfo->DDInfo = (DXDIRECTDRAWINFO*)AddStruct(dxinfo->DDInfo, dxinfo->nDDInfo, sizeof(DXDIRECTDRAWINFO));
		ddi = &dxinfo->DDInfo[dxinfo->nDDInfo];
		ddi->AdapterOrdinal = i;
		ddi->bCanRenderWindowed = 1;

		if (FAILED(G_d3d->GetAdapterIdentifier(i, 0, &ddi->DDIdentifier)))
			continue;

		lstrcpy(ddi->Name, ddi->DDIdentifier.Description);
		lstrcpy(ddi->About, ddi->DDIdentifier.Description);
		ddi->MonitorLabel[0] = '\0';

		// On a multi-monitor system, every adapter shows the same GPU name in the setup
		// dialog's dropdown, making them impossible to tell apart. Look up the same display
		// number Windows' own Settings > Display page shows (from the monitor's device name,
		// "\\.\DISPLAYn") so setupdlg.cpp can show it alongside the adapter name.
		{
			HMONITOR hMon;
			MONITORINFOEXA mi;
			char* p;

			hMon = G_d3d->GetAdapterMonitor(i);
			mi.cbSize = sizeof(mi);

			if (hMon && GetMonitorInfoA(hMon, &mi))
			{
				p = mi.szDevice + strlen(mi.szDevice);

				while (p > mi.szDevice && isdigit((uchar)p[-1]))
					p--;

				if (*p)
					wsprintf(ddi->MonitorLabel, " (Windows Anzeige %s)", p);
			}
		}

		Log("Found - %s\nDriver %s Version %d.%d.%d.%d",
			ddi->DDIdentifier.Description,
			ddi->DDIdentifier.Driver,
			HIWORD(ddi->DDIdentifier.DriverVersion.HighPart),
			LOWORD(ddi->DDIdentifier.DriverVersion.HighPart),
			HIWORD(ddi->DDIdentifier.DriverVersion.LowPart),
			LOWORD(ddi->DDIdentifier.DriverVersion.LowPart));

		if (FAILED(G_d3d->GetDeviceCaps(i, D3DDEVTYPE_HAL, &caps)))
			continue;

		Log("Finding Compatible Display Modes");
		ddi->D3DDevices = (DXD3DDEVICE*)AddStruct(ddi->D3DDevices, ddi->nD3DDevices, sizeof(DXD3DDEVICE));
		device = &ddi->D3DDevices[ddi->nD3DDevices];
		strcpy(device->Name, "Direct3D9 Hardware");
		strcpy(device->About, "Direct3D9 Hardware Device");
		device->bHardware = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) ? 1 : 0;

		// everything gets hand-converted to 32bpp ARGB on upload (see texture.cpp) - one
		// representative texture/z-buffer format entry is enough to keep the setup dialog happy.
		device->TextureInfos = (DXTEXTUREINFO*)AddStruct(0, 0, sizeof(DXTEXTUREINFO));
		device->nTextureInfos = 1;
		device->TextureInfos[0].format = D3DFMT_A8R8G8B8;
		device->TextureInfos[0].bpp = 32;
		device->TextureInfos[0].rbpp = 8;
		device->TextureInfos[0].gbpp = 8;
		device->TextureInfos[0].bbpp = 8;
		device->TextureInfos[0].abpp = 8;
		device->TextureInfos[0].rshift = 16;
		device->TextureInfos[0].gshift = 8;
		device->TextureInfos[0].bshift = 0;
		device->TextureInfos[0].ashift = 24;

		device->ZBufferInfos = (DXZBUFFERINFO*)AddStruct(0, 0, sizeof(DXZBUFFERINFO));
		device->nZBufferInfos = 1;
		device->ZBufferInfos[0].format = D3DFMT_D24S8;
		device->ZBufferInfos[0].bpp = 32;

		nModes = G_d3d->GetAdapterModeCount(i, D3DFMT_X8R8G8B8);

		for (UINT m = 0; m < nModes; m++)
		{
			if (FAILED(G_d3d->EnumAdapterModes(i, D3DFMT_X8R8G8B8, m, &mode)))
				continue;

			device->DisplayModes = (DXDISPLAYMODE*)AddStruct(device->DisplayModes, device->nDisplayModes, sizeof(DXDISPLAYMODE));
			dm = &device->DisplayModes[device->nDisplayModes];
			dm->w = mode.Width;
			dm->h = mode.Height;
			dm->bpp = 32;
			dm->RefreshRate = mode.RefreshRate;
			dm->rshift = 16;
			dm->rbpp = 8;
			dm->gshift = 8;
			dm->gbpp = 8;
			dm->bshift = 0;
			dm->bbpp = 8;
			Log("%d x %d - 32 Bit - %d Hz", dm->w, dm->h, dm->RefreshRate);
			device->nDisplayModes++;
		}

		ddi->nD3DDevices = 1;
		dxinfo->nDDInfo++;
	}

	// D3D9 adapter ordinal 0 isn't guaranteed to be the monitor Windows considers "primary"
	// on every multi-monitor setup - default to whichever enumerated adapter actually drives
	// the primary monitor (point (0,0) in virtual-screen coordinates always lies on it),
	// instead of blindly picking index 0 and potentially launching fullscreen on the wrong
	// display.
	dxinfo->nDD = 0;
	{
		HMONITOR primaryMonitor;
		POINT origin;

		origin.x = 0;
		origin.y = 0;
		primaryMonitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

		for (int i = 0; i < dxinfo->nDDInfo; i++)
		{
			if (G_d3d->GetAdapterMonitor(dxinfo->DDInfo[i].AdapterOrdinal) == primaryMonitor)
			{
				dxinfo->nDD = i;
				break;
			}
		}
	}

	dxinfo->nD3D = 0;
	dxinfo->bHardware = 1;

	Log("Enumerating DirectSound Devices");
	DXAttempt(DirectSoundEnumerate(DXEnumDirectSound, dxinfo));
	return 1;
}

void DXFreeInfo(DXINFO* dxinfo)
{
	DXDIRECTDRAWINFO* DDInfo;
	DXD3DDEVICE* d3d;

	Log(__FUNCTION__);

	for (int i = 0; i < dxinfo->nDDInfo; i++)
	{
		DDInfo = &dxinfo->DDInfo[i];

		for (int j = 0; j < DDInfo->nD3DDevices; j++)
		{
			d3d = &DDInfo->D3DDevices[j];
			free(d3d->DisplayModes);
			free(d3d->TextureInfos);
			free(d3d->ZBufferInfos);
		}

		free(DDInfo->D3DDevices);
		free(DDInfo->DisplayModes);
	}

	free(dxinfo->DDInfo);
	free(dxinfo->DSInfo);
}

// Was a complete stub (always zeroed everything, so ReadJoystick()/Key(n>=256) in
// input.cpp never had real data no matter what ControlMethod was set to) - the game's
// own Options -> Controls -> Joystick screen (LoadSave.cpp) and its remapping/"Reset"
// UI were otherwise fully built and working already, just with nothing behind them.
// Polls XInput controller 0 (covers DS4Windows/DualSense in its default Xbox-emulation
// output mode too, not just real Xbox controllers) and maps it into the same shape the
// old DirectInput-joystick-era code already expects, so nothing else needs to change:
//   - joystick_read_x/y: left stick, deadzoned, scaled from XInput's -32768..32767
//     range down to roughly the old -128..127 range S_UpdateInput()'s +-8 thresholds
//     were written against. D-pad is ORed in as a digital alternative.
//   - joystick_read_fire: one bit per physical button, in "Joy 1".."Joy 16" order
//     (bit0="Joy 1" etc) - the exact numbering JoyStickButtons[]/jLayout[] already
//     use, so the existing remapping screen assigns actions to these correctly
//     without needing to know they're specifically Xbox-layout buttons underneath.
static long DXUpdateJoystickDirectInput()
{
	if (!G_dxptr->Joystick)
		return 0;

	DIJOYSTATE2 js;
	HRESULT hr = G_dxptr->Joystick->Poll();

	if (FAILED(hr))
	{
		hr = G_dxptr->Joystick->Acquire();
		while (hr == DIERR_INPUTLOST)
			hr = G_dxptr->Joystick->Acquire();
		G_dxptr->Joystick->Poll();
	}

	if (FAILED(G_dxptr->Joystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
		return 0;

	joystick_read = 1;

	// Axes were ranged to -1000..1000 in DXInitInput via DIPROP_RANGE, so this
	// scales to roughly the same -128..127 ballpark XInput's path produces above.
	long lx = js.lX;
	long ly = js.lY;
	const long deadzone = 150;

	if (lx > -deadzone && lx < deadzone) lx = 0;
	if (ly > -deadzone && ly < deadzone) ly = 0;

	joystick_read_x = lx / 8;
	joystick_read_y = ly / 8;	// DirectInput joystick Y is already "up = negative", matching S_UpdateInput()'s joy_y<0=forward - no invert needed here (unlike the XInput thumbstick above)

	if (LOWORD(js.rgdwPOV[0]) != 0xFFFF)	// POV hat centered reports as -1/0xFFFFFFFF
	{
		DWORD pov = js.rgdwPOV[0];	// hundredths of a degree, 0 = up, clockwise
		if (pov > 31500 || pov < 4500)  joystick_read_y = -20;
		if (pov > 4500 && pov < 13500)  joystick_read_x = 20;
		if (pov > 13500 && pov < 22500) joystick_read_y = 20;
		if (pov > 22500 && pov < 31500) joystick_read_x = -20;
	}

	long fire = 0;

	for (int i = 0; i < 16; i++)
		if (js.rgbButtons[i] & 0x80)
			fire |= 1 << i;	// Joy 1..Joy 16, same bit layout the XInput path and JoyStickButtons[] use

	joystick_read_fire = fire;

	return 1;
}

long DXUpdateJoystick()
{
	XINPUT_STATE state;

	joystick_read = 0;
	joystick_read_x = 0;
	joystick_read_y = 0;
	joystick_read_fire = 0;

	if (XInputGetState(0, &state) != ERROR_SUCCESS)
		return DXUpdateJoystickDirectInput();	// no XInput device (e.g. a PS4/PS5 controller with no Steam Input/DS4Windows wrapper active) - fall back to raw DirectInput

	joystick_read = 1;

	long lx = state.Gamepad.sThumbLX;
	long ly = state.Gamepad.sThumbLY;

	if (lx > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && lx < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		lx = 0;

	if (ly > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && ly < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		ly = 0;

	joystick_read_x = lx / 256;	// -32768..32767 -> ~-128..127
	joystick_read_y = -ly / 256;	// inverted: stick pushed up (ly>0) means forward, which S_UpdateInput() reads as joy_y<0

	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  joystick_read_x = -20;
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) joystick_read_x = 20;
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)    joystick_read_y = -20;
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  joystick_read_y = 20;

	long fire = 0;

	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A)             fire |= 1 << 0;	// Joy 1
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B)             fire |= 1 << 1;	// Joy 2
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_X)             fire |= 1 << 2;	// Joy 3
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y)             fire |= 1 << 3;	// Joy 4
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) fire |= 1 << 4;	// Joy 5
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER)fire |= 1 << 5;	// Joy 6
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)          fire |= 1 << 6;	// Joy 7
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_START)         fire |= 1 << 7;	// Joy 8
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB)    fire |= 1 << 8;	// Joy 9
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)   fire |= 1 << 9;	// Joy 10
	if (state.Gamepad.bLeftTrigger > 30)                       fire |= 1 << 10;	// Joy 11
	if (state.Gamepad.bRightTrigger > 30)                      fire |= 1 << 11;	// Joy 12

	joystick_read_fire = fire;

	return 1;
}

static BOOL CALLBACK DIEnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext)
{
	LPDIRECTINPUT8 dinput = (LPDIRECTINPUT8)pContext;
	LPDIRECTINPUTDEVICE8 joystick = 0;

	if (SUCCEEDED(dinput->CreateDevice(pdidInstance->guidInstance, &joystick, 0)))
	{
		G_dxptr->Joystick = joystick;
		return DIENUM_STOP;
	}

	return DIENUM_CONTINUE;
}

static void DXInitJoystick(HWND hwnd)
{
	G_dxptr->Joystick = 0;

	G_dxptr->lpDirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, DIEnumJoysticksCallback, G_dxptr->lpDirectInput, DIEDFL_ATTACHEDONLY);

	if (!G_dxptr->Joystick)
	{
		Log("No DirectInput joystick found (XInput-only controllers still work)");
		return;
	}

	DXAttempt(G_dxptr->Joystick->SetDataFormat(&c_dfDIJoystick2));
	DXAttempt(G_dxptr->Joystick->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));

	DIPROPRANGE range;
	range.diph.dwSize = sizeof(DIPROPRANGE);
	range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1000;
	range.lMax = 1000;

	range.diph.dwObj = DIJOFS_X;
	G_dxptr->Joystick->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_Y;
	G_dxptr->Joystick->SetProperty(DIPROP_RANGE, &range.diph);

	G_dxptr->Joystick->Acquire();

	Log("DirectInput joystick acquired (fallback for non-XInput controllers)");
}

void DXInitInput(HWND hwnd, HINSTANCE hinstance)
{
	LPDIRECTINPUT8 dinput;
	LPDIRECTINPUTDEVICE8 Keyboard;

#if (DIRECTINPUT_VERSION >= 0x800)
	DXAttempt(DirectInput8Create(hinstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID*)&dinput, 0));
	dinput->QueryInterface(IID_IDirectInput8, (void**)&G_dxptr->lpDirectInput);
#else
	DXAttempt(DirectInputCreate(hinstance, DIRECTINPUT_VERSION, &dinput, 0));
	dinput->QueryInterface(IID_IDirectInput2, (void**)&G_dxptr->lpDirectInput);
#endif

	if (dinput)
	{
		Log("Released %s @ %x - RefCnt = %d", "DirectInput", dinput, dinput->Release());
		dinput = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "DirectInput");

	DXAttempt(G_dxptr->lpDirectInput->CreateDevice(GUID_SysKeyboard, &Keyboard, 0));
#if (DIRECTINPUT_VERSION >= 0x800)
	Keyboard->QueryInterface(IID_IDirectInputDevice8, (void**)&G_dxptr->Keyboard);
#else
	Keyboard->QueryInterface(IID_IDirectInputDevice2, (void**)&G_dxptr->Keyboard);
#endif

	if (Keyboard)
	{
		Log("Released %s @ %x - RefCnt = %d", "Keyboard", Keyboard, Keyboard->Release());
		Keyboard = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "Keyboard");

	DXAttempt(G_dxptr->Keyboard->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
	DXAttempt(G_dxptr->Keyboard->SetDataFormat(&c_dfDIKeyboard));
	DXAttempt(G_dxptr->Keyboard->Acquire());
	memset(keymap, 0, sizeof(keymap));

	DXInitJoystick(hwnd);
}

const char* DXGetErrorString(HRESULT hr)
{
	static char buf[64];

	switch (hr)
	{
	case D3D_OK:
		return "The request completed successfully.";

	case D3DERR_DEVICELOST:
		return "The Direct3D device has been lost and needs to be reset.";

	case D3DERR_DEVICENOTRESET:
		return "The Direct3D device requires a reset, but it cannot be reset right now.";

	case D3DERR_DRIVERINTERNALERROR:
		return "Internal driver error.";

	case D3DERR_INVALIDCALL:
		return "The method call is invalid.";

	case D3DERR_NOTAVAILABLE:
		return "This device does not support the queried technique.";

	case D3DERR_OUTOFVIDEOMEMORY:
		return "Direct3D does not have enough display memory to perform the operation.";

	case D3DERR_NOTFOUND:
		return "The requested item was not found.";

	case D3DERR_WASSTILLDRAWING:
		return "The previous blit operation that is transferring information to or from this surface is incomplete.";

	case E_OUTOFMEMORY:
		return "Ran out of memory.";

	case DSERR_ALLOCATED:
		return "The request failed because resources, such as a priority level, were already in use by another caller. ";

	case DSERR_ALREADYINITIALIZED:
		return "The object is already initialized. ";

	case DSERR_BADFORMAT:
		return "The specified wave format is not supported. ";

	case DSERR_BUFFERLOST:
		return "The buffer memory has been lost and must be restored. ";

	case DSERR_CONTROLUNAVAIL:
		return "The buffer control (volume, pan, and so on) requested by the caller is not available. ";

	case DSERR_INVALIDCALL:
		return "This function is not valid for the current state of this object. ";

	case DSERR_NOAGGREGATION:
		return "The object does not support aggregation. ";

	case DSERR_NODRIVER:
		return "No sound driver is available for use. ";

	case DSERR_NOINTERFACE:
		return "The requested COM interface is not available. ";

	case DSERR_OTHERAPPHASPRIO:
		return "Another application has a higher priority level, preventing this call from succeeding ";

	case DSERR_PRIOLEVELNEEDED:
		return "The caller does not have the priority level required for the function to succeed. ";

	case DSERR_UNINITIALIZED:
		return "The IDirectSound::Initialize method has not been called or has not been called successfully before other methods were called. ";
	}

	wsprintf(buf, "Undefined Error (0x%08X)", hr);
	return buf;
}

const char* DIGetErrorString(HRESULT hr)
{
	switch (hr)
	{
	case DD_OK:
		return "The request completed successfully.";

	case DIERR_OLDDIRECTINPUTVERSION:
	case DIERR_BETADIRECTINPUTVERSION:
		return "The application was written for an unsupported prerelease version of DirectInput.";

	case DIERR_BADDRIVERVER:
		return "The object could not be created due to an incompatible driver version or mismatched or incomplete driver components.";

	case DIERR_DEVICENOTREG:
		return "The device or device instance or effect is not registered with DirectInput.";

	case DIERR_NOTFOUND:
		return "The requested object does not exist.";

	case DIERR_INVALIDPARAM:
		return "An invalid parameter was passed to the returning function,or the object was not in a state that admitted the function to be called.";

	case DIERR_NOINTERFACE:
		return "The specified interface is not supported by the object";

	case DIERR_GENERIC:
		return "An undetermined error occured inside the DInput subsystem";

	case DIERR_OUTOFMEMORY:
		return "The DInput subsystem couldn't allocate sufficient memory to complete the caller's request.";

	case DIERR_UNSUPPORTED:
		return "The function called is not supported at this time";

	case DIERR_NOTINITIALIZED:
		return "This object has not been initialized";

	case DIERR_ALREADYINITIALIZED:
		return "This object is already initialized";

	case DIERR_NOAGGREGATION:
		return "This object does not support aggregation";

	case DIERR_OTHERAPPHASPRIO:
		return "Another app has a higher priority level, preventing this call from succeeding.";

	case DIERR_INPUTLOST:
		return "Access to the device has been lost.  It must be re-acquired.";

	case DIERR_ACQUIRED:
		return "The operation cannot be performed while the device is acquired.";

	case DIERR_NOTACQUIRED:
		return "The operation cannot be performed unless the device is acquired.";

	case DIERR_INSUFFICIENTPRIVS:
		return "Unable to IDirectInputJoyConfig_Acquire because the user does not have sufficient privileges to change the joystick configuration.";

	case DIERR_DEVICEFULL:
		return "The device is full.";

	case DIERR_MOREDATA:
		return "Not all the requested information fit into the buffer.";

	case DIERR_NOTDOWNLOADED:
		return "The effect is not downloaded.";

	case DIERR_HASEFFECTS:
		return " The device cannot be reinitialized because there are still effects attached to it.";

	case DIERR_NOTEXCLUSIVEACQUIRED:
		return " The operation cannot be performed unless the device is acquired in DISCL_EXCLUSIVE mode.";

	case DIERR_INCOMPLETEEFFECT:
		return " The effect could not be downloaded because essential information is missing.";

	case DIERR_NOTBUFFERED:
		return " Attempted to read buffered device data from a device that is not buffered.";

	case DIERR_EFFECTPLAYING:
		return " An attempt was made to modify parameters of an effect while it is playing";

	case DIERR_UNPLUGGED:
		return " The operation could not be completed because the device is not plugged in.";

	case DIERR_REPORTFULL:
		return " SendDeviceData failed because more information was requested to be sent than can be sent to the device.  Some devices have restrictions on how much data can be sent to them";
	}

	return "Undefined Error";
}

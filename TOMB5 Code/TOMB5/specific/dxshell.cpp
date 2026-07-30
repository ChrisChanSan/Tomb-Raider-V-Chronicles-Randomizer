#include "../tomb5/pch.h"
#include "dxshell.h"
#include "function_stubs.h"
#include "winmain.h"
#include "input.h"

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
		LPDIRECT3DSURFACE9 back;

		if (SUCCEEDED(G_dxptr->lpD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		{
			DXSaveScreen(back, "Tomb");
			back->Release();
		}
	}

	hr = G_dxptr->lpD3DDevice->Present(0, 0, 0, 0);

	if (hr == D3DERR_DEVICELOST)
		Log("Present - Device Lost");
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
		Log("DXCreate: Windowed Mode");
		r.top = 0;
		r.left = 0;
		r.right = dm->w;
		r.bottom = dm->h;
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
		Log("DXCreate: Full Screen Mode");
		G_dxptr->dwRenderWidth = dm->w;
		G_dxptr->dwRenderHeight = dm->h;
		G_dxptr->rViewport.top = 0;
		G_dxptr->rViewport.left = 0;
		G_dxptr->rViewport.right = dm->w;
		G_dxptr->rViewport.bottom = dm->h;
	}

	memset(&G_dxptr->PresentParams, 0, sizeof(D3DPRESENT_PARAMETERS));
	G_dxptr->PresentParams.Windowed = !(Flags & DXF_FULLSCREEN);
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

	if (Flags & DXF_FULLSCREEN)
		G_dxptr->PresentParams.FullScreen_RefreshRateInHz = dm->RefreshRate;

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

	dxinfo->nDD = 0;
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

long DXUpdateJoystick()
{
	joystick_read = 0;
	joystick_read_x = 0;
	joystick_read_y = 0;
	joystick_read_fire = 0;
	return 0;
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

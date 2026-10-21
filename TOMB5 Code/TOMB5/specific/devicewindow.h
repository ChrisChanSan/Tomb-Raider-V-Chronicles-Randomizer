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

/* The game window and its swap chain at startup, on Alt+Enter and on a
 * resolution change.
 *
 * DXCreate places the window and fills the present parameters, WinSetStyle
 * gives the window the style of the display state, DXShowFrame and
 * CreateMonoScreen read the back buffer back. Header only, like
 * devicemove.h, so that the engine and RoomProbe -windowcycle take the very
 * same code: the probe drives it on a real window through the start state
 * and two toggles and says what each state is. */
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The style of the two display states; the rest of the window's own bits
// (visible, clip siblings) stay as they are.
inline DWORD DeviceWindowStyleFor(DWORD style, bool fullscreen)
{
	if (fullscreen)
		style = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
	else
		style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;

	return style & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_SYSMENU);
}

inline DWORD DeviceWindowStyle(HWND hWnd, bool fullscreen)
{
	const DWORD style = DeviceWindowStyleFor(
		(DWORD)GetWindowLong(hWnd, GWL_STYLE), fullscreen);

	SetWindowLong(hWnd, GWL_STYLE, (LONG)style);
	return style;
}

/* The windowed state: the client area as large as the adapter's desktop
 * mode (or the enumerated mode when the adapter does not answer), the window
 * grown round it by `style`. The client rectangle comes back in `viewport`
 * and in screen coordinates in `screen`. */
inline void DeviceWindowPlaceWindowed(HWND hWnd, IDirect3D9* d3d, UINT adapter,
	long modeW, long modeH, DWORD style, RECT* viewport, RECT* screen)
{
	D3DDISPLAYMODE mode;
	RECT r;

	r.left = 0;
	r.top = 0;

	if (d3d && SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &mode)))
	{
		r.right = mode.Width;
		r.bottom = mode.Height;
	}
	else
	{
		r.right = modeW;
		r.bottom = modeH;
	}

	AdjustWindowRect(&r, style, 0);
	SetWindowPos(hWnd, 0, 0, 0, r.right - r.left, r.bottom - r.top,
		SWP_NOMOVE | SWP_NOZORDER);
	GetClientRect(hWnd, viewport);
	GetClientRect(hWnd, screen);
	ClientToScreen(hWnd, (LPPOINT)&screen->left);
	ClientToScreen(hWnd, (LPPOINT)&screen->right);
}

/* The full screen state: a window over the whole monitor of the adapter, at
 * the monitor's desktop mode, the device still windowed (see DXCreate).
 * DeviceWindowFullRect says where - the monitor's rectangle in `monitor`
 * (the enumerated mode from 0,0 when the adapter does not name one) and the
 * back buffer's size in `w` and `h` - and DeviceWindowShowFull puts the
 * window there and brings it to the front. */
inline void DeviceWindowFullRect(IDirect3D9* d3d, UINT adapter, long modeW,
	long modeH, RECT* monitor, long* w, long* h)
{
	D3DDISPLAYMODE mode;
	HMONITOR which = d3d ? d3d->GetAdapterMonitor(adapter) : 0;
	MONITORINFO mi;

	mi.cbSize = sizeof(mi);

	if (which && GetMonitorInfo(which, &mi))
		*monitor = mi.rcMonitor;
	else
	{
		monitor->left = 0;
		monitor->top = 0;
		monitor->right = modeW;
		monitor->bottom = modeH;
	}

	if (d3d && SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &mode)))
	{
		*w = mode.Width;
		*h = mode.Height;
	}
	else
	{
		*w = modeW;
		*h = modeH;
	}
}

inline void DeviceWindowShowFull(HWND hWnd, const RECT* monitor, long w,
	long h)
{
	SetWindowPos(hWnd, HWND_TOP, monitor->left, monitor->top, w, h,
		SWP_SHOWWINDOW);
	ShowWindow(hWnd, SW_SHOW);
	SetForegroundWindow(hWnd);
}

// The present parameters of every device DXCreate makes (see the comments
// there for each value).
inline void DevicePresentParams(D3DPRESENT_PARAMETERS* pp, HWND hWnd,
	long w, long h, bool vsync, bool zbuffer)
{
	memset(pp, 0, sizeof(*pp));
	pp->Windowed = TRUE;
	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->hDeviceWindow = hWnd;
	pp->BackBufferWidth = w;
	pp->BackBufferHeight = h;
	pp->BackBufferFormat = D3DFMT_X8R8G8B8;
	pp->BackBufferCount = 1;
	pp->PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE :
		D3DPRESENT_INTERVAL_IMMEDIATE;

	if (zbuffer)
	{
		pp->EnableAutoDepthStencil = TRUE;
		pp->AutoDepthStencilFormat = D3DFMT_D24S8;
	}
}

/* The back buffer read back into a system memory surface: GetBackBuffer,
 * CreateOffscreenPlainSurface in D3DPOOL_SYSTEMMEM, GetRenderTargetData -
 * the engine's apostrophe screenshot and the inventory's mono picture. The
 * HRESULT of the first step that failed comes back, or of the last; *out
 * holds the surface (released by the caller) only on success. */
inline HRESULT DeviceReadBack(IDirect3DDevice9* device, IDirect3DSurface9** out)
{
	IDirect3DSurface9* back = 0;
	IDirect3DSurface9* mem = 0;
	D3DSURFACE_DESC desc;
	HRESULT hr;

	*out = 0;
	hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);

	if (FAILED(hr))
		return hr;

	back->GetDesc(&desc);
	hr = device->CreateOffscreenPlainSurface(desc.Width, desc.Height,
		desc.Format, D3DPOOL_SYSTEMMEM, &mem, 0);

	if (SUCCEEDED(hr))
	{
		hr = device->GetRenderTargetData(back, mem);

		if (SUCCEEDED(hr))
			*out = mem;
		else
			mem->Release();
	}

	back->Release();
	return hr;
}

/* A system memory X8R8G8B8 surface into a 24-bit TGA at `path`. Returns the
 * bytes written, 0 when nothing was.
 *
 * 24-bit TGA out of the D3D9 port's D3DFMT_X8R8G8B8 back buffer: four
 * bytes a pixel in, three out, and the walk starts on the last row, at
 * (Height - 1) * Pitch inside the buffer, since TGA's origin is the
 * bottom left. */
inline long DeviceSaveTga(IDirect3DSurface9* surf, const char* path)
{
	static const unsigned char head[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 64, 1, 0, 1, 16, 0 };
	unsigned char header[18];
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT rect;
	long written = 0;
	FILE* file;

	if (FAILED(surf->GetDesc(&desc)))
		return 0;

	if (FAILED(surf->LockRect(&rect, 0, D3DLOCK_READONLY)))
		return 0;

	file = fopen(path, "wb");

	if (file)
	{
		const unsigned char* src = (const unsigned char*)rect.pBits;
		unsigned char* row = (unsigned char*)malloc(3 * desc.Width);

		memcpy(header, head, sizeof(header));
		header[16] = 24;
		*(short*)&header[12] = (short)desc.Width;
		*(short*)&header[14] = (short)desc.Height;
		fwrite(header, sizeof(header), 1, file);
		written = sizeof(header);

		// TGA has its origin at the bottom left, the surface at the top
		// left - hence backwards from the last row. X8R8G8B8 lies in memory
		// as B,G,R,X - exactly the order TGA wants too.
		src += (desc.Height - 1) * rect.Pitch;

		for (UINT y = 0; row && y < desc.Height; y++)
		{
			for (UINT x = 0; x < desc.Width; x++)
			{
				row[x * 3 + 0] = src[x * 4 + 0];
				row[x * 3 + 1] = src[x * 4 + 1];
				row[x * 3 + 2] = src[x * 4 + 2];
			}

			fwrite(row, 3 * desc.Width, 1, file);
			written += 3 * desc.Width;
			src -= rect.Pitch;
		}

		free(row);
		fclose(file);
	}

	surf->UnlockRect();
	return written;
}

/* What a state of the window and its swap chain is, for the DXLOG window
 * line and for RoomProbe -windowcycle: the window's styles, its rectangle
 * and its client area in screen coordinates, the monitor's rectangle, and
 * the present parameters and creation flags the device itself reports. */
struct DeviceWindowState
{
	DWORD style, exStyle;
	RECT window, client, monitor;
	bool topmost, visible, foreground;
	D3DPRESENT_PARAMETERS pp;
	DWORD behavior;
	bool haveDevice;
};

inline void DeviceWindowMeasure(HWND hWnd, IDirect3DDevice9* device,
	DeviceWindowState* s)
{
	HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	POINT corner;

	memset(s, 0, sizeof(*s));
	s->style = (DWORD)GetWindowLong(hWnd, GWL_STYLE);
	s->exStyle = (DWORD)GetWindowLong(hWnd, GWL_EXSTYLE);
	s->topmost = (s->exStyle & WS_EX_TOPMOST) != 0;
	s->visible = IsWindowVisible(hWnd) != 0;
	s->foreground = GetForegroundWindow() == hWnd;
	GetWindowRect(hWnd, &s->window);
	GetClientRect(hWnd, &s->client);
	corner.x = 0;
	corner.y = 0;
	ClientToScreen(hWnd, &corner);
	OffsetRect(&s->client, corner.x, corner.y);
	mi.cbSize = sizeof(mi);

	if (monitor && GetMonitorInfo(monitor, &mi))
		s->monitor = mi.rcMonitor;

	if (device)
	{
		IDirect3DSwapChain9* chain = 0;
		D3DDEVICE_CREATION_PARAMETERS made;

		if (SUCCEEDED(device->GetSwapChain(0, &chain)) && chain)
		{
			s->haveDevice = SUCCEEDED(chain->GetPresentParameters(&s->pp));
			chain->Release();
		}

		if (SUCCEEDED(device->GetCreationParameters(&made)))
			s->behavior = made.BehaviorFlags;
	}
}

// The state as one line of text (no line end), as the log and the probe
// print it; the same state gives the same text.
inline int DeviceWindowStateText(const DeviceWindowState* s, char* out,
	size_t size)
{
	return _snprintf_s(out, size, _TRUNCATE,
		"style=%08lX ex=%08lX topmost=%d visible=%d foreground=%d "
		"window=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld "
		"monitor=%ld,%ld,%ld,%ld windowed=%d swap=%d format=%d "
		"back=%ux%u count=%u multisample=%d interval=%08lX hz=%u "
		"flags=%08lX depth=%d behavior=%08lX",
		s->style, s->exStyle, (int)s->topmost, (int)s->visible,
		(int)s->foreground, s->window.left, s->window.top, s->window.right,
		s->window.bottom, s->client.left, s->client.top, s->client.right,
		s->client.bottom, s->monitor.left, s->monitor.top,
		s->monitor.right, s->monitor.bottom, (int)s->pp.Windowed,
		(int)s->pp.SwapEffect, (int)s->pp.BackBufferFormat,
		s->pp.BackBufferWidth, s->pp.BackBufferHeight,
		s->pp.BackBufferCount, (int)s->pp.MultiSampleType,
		(unsigned long)s->pp.PresentationInterval,
		s->pp.FullScreen_RefreshRateInHz, s->pp.Flags,
		(int)s->pp.EnableAutoDepthStencil, s->behavior);
}

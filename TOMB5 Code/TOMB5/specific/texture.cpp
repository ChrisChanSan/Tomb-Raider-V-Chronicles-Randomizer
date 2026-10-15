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
#include "../tomb5/pch.h"
#include "texture.h"
#include "function_stubs.h"
#include "dxshell.h"
#include "3dmath.h"
#include "winmain.h"
#include "LoadSave.h"
#include "devicemove.h"
#include "hd/hd_level.h"	// HDLevelDeviceRecreated

// Raised from the original 64 - a Randomizer combining texture pages from
// several source levels can plausibly need more than any single vanilla
// level ever did. See the bounds check at the top of LoadTextures() (file.cpp)
// that validates RTPages+OTPages+BTPages+1 against this before ever writing
// into the array - the original engine had neither the check nor the margin.
TEXTURE Textures[256];
long nTextures;

/* Texture stage 0 between the loading thread and the frames of the load bar.
 * Direct3D keeps one stage 0 per device, not per thread, and the device's own
 * lock (D3DCREATE_MULTITHREADED) covers one call, not a bind and the draws
 * after it. While a level loads, the main thread draws the loading picture
 * and the bar (S_DrawLoadBar) and the loading thread binds pages (FreeLevel,
 * LoadTextures). A bind landing between the picture's bind and its draws
 * shows the picture as a level page or untextured for a frame. So the main
 * thread holds this lock from the frame's first bind to its last draw, and
 * the loading thread holds it around each of its binds. Nobody else waits on
 * it: outside a load one thread draws. */
static struct TextureStageLockCS
{
	CRITICAL_SECTION cs;

	TextureStageLockCS() { InitializeCriticalSection(&cs); }
} g_stage_lock;

void TextureStageLock()
{
	EnterCriticalSection(&g_stage_lock.cs);
}

void TextureStageUnlock()
{
	LeaveCriticalSection(&g_stage_lock.cs);
}

// Destination is always a plain 32-bit D3DFMT_A8R8G8B8 texture now (the old per-adapter
// enumerated pixel format - 5551, 8888, etc. - no longer varies), so the only path that
// actually runs in practice is the 32-bit passthrough (format==1, see LoadTextures() in
// file.cpp, which now always computes format==1 since DXGetInfo always reports 8/8/8/8).
// The generic bit-packing (format==0) and 16-bit passthrough (format==2) paths are kept
// for parity/safety in case a caller ever passes source data in those shapes.
LPDIRECT3DTEXTURE9 CreateTexturePage(long w, long h, long MipMapCount, long* pSrc, rgbfunc RGBM, long format)
{
	LPDIRECT3DTEXTURE9 tex9;
	D3DLOCKED_RECT rect;
	long* lS;
	ulong* lD;
	short* sS;
	uchar* cD;
	ulong c, o, ro, go, bo, ao;
	uchar r, g, b, a;
	UINT levels;

	if (w < 32 || h < 32)
		MipMapCount = 0;

	levels = MipMapCount ? (UINT)(MipMapCount + 1) : 1;

	if (FAILED(App.dx.lpD3DDevice->CreateTexture(w, h, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex9, 0)))
	{
		Log("CreateTexturePage Failed");
		return 0;
	}

	if (!pSrc)
		return tex9;

	if (FAILED(tex9->LockRect(0, &rect, 0, 0)))
	{
		tex9->Release();
		return 0;
	}

	if (!format)
	{
		lS = pSrc;

		for (long y = 0; y < h; y++)
		{
			cD = (uchar*)rect.pBits + y * rect.Pitch;

			for (long x = 0; x < w; x++)
			{
				c = *(lS + x * 256 / w + y * 0x10000 / h);
				r = CLRR(c);
				g = CLRG(c);
				b = CLRB(c);
				a = CLRA(c);

				if (RGBM)
					RGBM(&r, &g, &b);

				ro = r >> 0 << 16;
				go = g >> 0 << 8;
				bo = b >> 0 << 0;
				ao = a >> 0 << 24;
				o = ro | go | bo | ao;
				*(ulong*)cD = o;
				cD += 4;
			}
		}
	}
	else if (format == 2)
	{
		sS = (short*)pSrc;

		for (long y = 0; y < h; y++)
		{
			lD = (ulong*)((uchar*)rect.pBits + y * rect.Pitch);

			for (long x = 0; x < w; x++)
			{
				ushort px = *(ushort*)(sS + x * 256 / w + y * 0x10000 / h);
				r = (uchar)((px >> 10) & 0x1F) << 3;
				g = (uchar)((px >> 5) & 0x1F) << 3;
				b = (uchar)(px & 0x1F) << 3;
				a = (px & 0x8000) ? 0xFF : 0;
				lD[x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		}
	}
	else if (format == 1)
	{
		lS = pSrc;

		for (long y = 0; y < h; y++)
		{
			lD = (ulong*)((uchar*)rect.pBits + y * rect.Pitch);

			for (long x = 0; x < w; x++)
				lD[x] = *(ulong*)(lS + x * 256 / w + y * 0x10000 / h);
		}
	}

	tex9->UnlockRect(0);
	return tex9;
}

void FreeTextures()
{
	TEXTURE* tex;

	Log(__FUNCTION__);

	TextureStageLock();	// on the loading thread under the bar's frames
	DXAttempt(App.dx.lpD3DDevice->SetTexture(0, 0));
	TextureStageUnlock();

	for (int i = 0; i < nTextures; i++)
	{
		tex = &Textures[i];

		if (tex->tex)
		{
			Log("Released %s @ %x - RefCnt = %d", "Texture", tex->tex, tex->tex->Release());
			tex->tex = 0;
		}
		else
			Log("%s Attempt To Release NULL Ptr", "Texture");
	}
}

/* FreeTextures for a load that keeps the level's page blocks (file.cpp,
 * TexturePagesKeep): the entries from `first` up to nTextures go - the misc
 * pages, which LoadTextures makes on every load - and the pages below stay.
 * Entry 0 is never a page. */
void FreeTexturesFrom(long first)
{
	TEXTURE* tex;

	Log(__FUNCTION__);

	TextureStageLock();	// on the loading thread under the bar's frames
	DXAttempt(App.dx.lpD3DDevice->SetTexture(0, 0));
	TextureStageUnlock();

	for (long i = first; i < nTextures; i++)
	{
		tex = &Textures[i];

		if (tex->tex)
		{
			Log("Released %s @ %x - RefCnt = %d", "Texture", tex->tex,
				tex->tex->Release());
			tex->tex = 0;
		}
		else
			Log("%s Attempt To Release NULL Ptr", "Texture");
	}
}

/* After DXCreate made a new device in place of an old one (Alt+Enter, a
 * resolution change): every page of Textures[] and the mono picture are made
 * again on the new device (specific/devicemove.h). Without it the first
 * classic draw binds a page of the released device, and once two or more
 * pages were drawn there that draw dies inside d3d9.dll. The level's own
 * data is not needed: a MANAGED texture keeps its texels in system memory,
 * and LockRect reads them back while the old device is only kept alive by
 * its textures. Runs on the main thread while the game thread is suspended.
 *
 * -log line, new: DXLOG device recreated - pages=<entries of Textures[]>
 * moved=<made again> dropped=<released and set to 0, drawn untextured until
 * the next level> kept=<already on the new device> picture=<1 when the mono
 * picture was there> old_device_refs=<what the old device still holds after
 * the move: 0 is nothing, -1 is no texture of it met> hd=<rebuilt when the
 * HD level was built again on the new device, classic when it was not or
 * there was none> ms=<what that build took, in milliseconds>.
 *
 * The HD level is built after the move, never before: its water frames
 * borrow entries of Textures[] above nTextures, and the move walks the
 * level's own entries below it. */
void MoveTexturesToDevice()
{
	DeviceMoveCount count;
	LARGE_INTEGER start, end, rate;
	long picture;

	DeviceMoveBegin(&count);

	for (int i = 0; i < nTextures; i++)
		DeviceMoveTexture(App.dx.lpD3DDevice, &Textures[i].tex, &count);

	picture = MonoScreenToDevice(App.dx.lpD3DDevice, &count);
	const long left = DeviceMoveEnd(&count);

	QueryPerformanceFrequency(&rate);
	QueryPerformanceCounter(&start);

	const bool hd = HDLevelDeviceRecreated();

	QueryPerformanceCounter(&end);

	Log("DXLOG device recreated - pages=%ld moved=%ld dropped=%ld kept=%ld "
		"picture=%ld old_device_refs=%ld hd=%s ms=%ld", nTextures, count.moved,
		count.dropped, count.kept, picture, left, hd ? "rebuilt" : "classic",
		(long)((end.QuadPart - start.QuadPart) * 1000 / rate.QuadPart));
}

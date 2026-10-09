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

/* Textures carried over to a new device.
 *
 * Alt+Enter and a resolution change release the device and make another
 * (DXCreate). A D3DPOOL_MANAGED texture keeps its own device alive and stays
 * tied to it: bound and drawn on the new device it sends the runtime into
 * the old device's resource bookkeeping, and once two or more pages were
 * drawn on the old one that read is a null pointer inside d3d9.dll (one page
 * draws without a fault). DeviceMoveTexture makes such a texture again on
 * the new device - same size, levels and format, MANAGED - copies every
 * level through LockRect and releases the old one.
 *
 * Header only, so the engine (texture.cpp) and RoomProbe -cycleclassic take
 * the very same code. */
#include <d3d9.h>
#include <string.h>

struct DeviceMoveCount
{
	long moved;			// made again on the new device
	long dropped;		// released and set to 0 - not MANAGED, a format of
						// unknown texel size, or a failed copy
	long kept;			// already on the new device
	IDirect3DDevice9* old;	// held until DeviceMoveEnd, 0 if none was met
};

inline void DeviceMoveBegin(DeviceMoveCount* count)
{
	memset(count, 0, sizeof(*count));
}

// Bytes per texel of the formats a classic page or picture can have; 0 for
// any other, which is then dropped rather than copied with a wrong pitch.
inline UINT DeviceMoveTexelBytes(D3DFORMAT format)
{
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		return 4;

	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
		return 2;

	default:
		return 0;
	}
}

inline void DeviceMoveDrop(IDirect3DTexture9** tex, DeviceMoveCount* count)
{
	(*tex)->Release();
	*tex = 0;
	count->dropped++;
}

inline void DeviceMoveTexture(IDirect3DDevice9* to, IDirect3DTexture9** tex,
	DeviceMoveCount* count)
{
	IDirect3DDevice9* from = 0;
	IDirect3DTexture9* made = 0;
	D3DSURFACE_DESC desc;
	DWORD levels;
	UINT texel;

	if (!to || !tex || !*tex)
		return;

	if (FAILED((*tex)->GetDevice(&from)) || !from)
	{
		DeviceMoveDrop(tex, count);
		return;
	}

	if (from == to)
	{
		from->Release();
		count->kept++;
		return;
	}

	// The first old device met is held, so that DeviceMoveEnd can say what
	// is left on it; any other reference GetDevice handed out goes now.
	if (!count->old)
		count->old = from;
	else
		from->Release();

	levels = (*tex)->GetLevelCount();

	if (FAILED((*tex)->GetLevelDesc(0, &desc)) ||
		desc.Pool != D3DPOOL_MANAGED ||
		!(texel = DeviceMoveTexelBytes(desc.Format)) ||
		FAILED(to->CreateTexture(desc.Width, desc.Height, levels, 0,
			desc.Format, D3DPOOL_MANAGED, &made, 0)))
	{
		DeviceMoveDrop(tex, count);
		return;
	}

	for (DWORD level = 0; level < levels; level++)
	{
		D3DSURFACE_DESC size;
		D3DLOCKED_RECT src, dst;
		bool copied = false;

		if (SUCCEEDED((*tex)->GetLevelDesc(level, &size)) &&
			SUCCEEDED((*tex)->LockRect(level, &src, 0, D3DLOCK_READONLY)))
		{
			if (SUCCEEDED(made->LockRect(level, &dst, 0, 0)))
			{
				for (UINT y = 0; y < size.Height; y++)
					memcpy((BYTE*)dst.pBits + y * dst.Pitch,
						(const BYTE*)src.pBits + y * src.Pitch,
						size.Width * texel);

				made->UnlockRect(level);
				copied = true;
			}

			(*tex)->UnlockRect(level);
		}

		if (!copied)
		{
			made->Release();
			DeviceMoveDrop(tex, count);
			return;
		}
	}

	(*tex)->Release();
	*tex = made;
	count->moved++;
}

/* Releases the hold on the old device and returns the references it still
 * has: 0 when nothing of it is left and it is gone, -1 when no texture of
 * another device was met. */
inline long DeviceMoveEnd(DeviceMoveCount* count)
{
	long left = -1;

	if (count->old)
		left = (long)count->old->Release();

	count->old = 0;
	return left;
}

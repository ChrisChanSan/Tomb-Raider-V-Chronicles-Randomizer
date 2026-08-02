#include "../tomb5/pch.h"
#include "texture.h"
#include "function_stubs.h"
#include "dxshell.h"
#include "3dmath.h"
#include "winmain.h"

// Raised from the original 64 - a Randomizer combining texture pages from
// several source levels can plausibly need more than any single vanilla
// level ever did. See the bounds check at the top of LoadTextures() (file.cpp)
// that validates RTPages+OTPages+BTPages+1 against this before ever writing
// into the array - the original engine had neither the check nor the margin.
TEXTURE Textures[256];
long nTextures;

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

	DXAttempt(App.dx.lpD3DDevice->SetTexture(0, 0));

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

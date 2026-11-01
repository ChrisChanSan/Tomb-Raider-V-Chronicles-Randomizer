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
#include "dxshell.h"
#include "function_stubs.h"
#include "function_table.h"
#include "texture.h"	// MoveTexturesToDevice
#include "hd/hd_level.h"
#include "fmv.h"			// FmvDeviceLost
#include "devicewindow.h"
#include "winmain.h"
#include "input.h"
#include "../tomb5/tomb5.h"	// tomb5.vsync
#include <Xinput.h>
#include <process.h>	// _beginthreadex - joystick init runs in the background

DXPTR* G_dxptr;
DXINFO* G_dxinfo;
LPDIRECT3D9 G_d3d;
HWND G_hwnd;
char keymap[256];
ulong DXTextureMemoryAtStart = 0;	// see dxshell.h
DXWINDOWPLACE DXWindowPlace;		// see dxshell.h
volatile long DXWindowBusy;		// see dxshell.h
volatile long DXDeviceLost;		// see dxshell.h

/* DXResizeToClient's two words to DXCreate: keep the window where it is and
 * take its client as the back buffer's size, and - only when a device of
 * that size was refused - the size to take instead. */
static long dx_keep_window;
static ulong dx_force_w, dx_force_h;

/* The classic pages still live on a released device: a recreate released
 * the old one and could not make the new one, and the next DXCreate has to
 * move them although no device is there to tell it so. */
static bool dx_pages_waiting;

/* VSync state and the display's refresh rate.
 *
 * Both are written by DXCreate and read by the present policy in
 * specific\output.cpp. They are answers about the device that exists, not
 * about what the player asked for: `tomb5.vsync` is the wish, and
 * g_vsyncActive is whether the device was really made with
 * D3DPRESENT_INTERVAL_ONE - a card that does not offer the interval, or a
 * driver panel that overrides it, must not leave the engine believing the
 * present blocks when it does not.
 *
 * The refresh rate is the adapter's current display mode
 * (D3DDISPLAYMODE.RefreshRate). Zero and absurd values are turned into 60, so
 * every reader downstream gets a number it can divide by. */
static long g_vsyncActive = 0;
static long g_displayHz = 60;

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

	/* VSync is asked of the card before it is asked of the device.
	 * DXCreate has already put D3DPRESENT_INTERVAL_ONE or
	 * D3DPRESENT_INTERVAL_IMMEDIATE into pp from tomb5.vsync; this is the one
	 * place that holds the caps, so it is the one place that can say whether
	 * the interval asked for exists at all. D3D9 promises ONE in windowed
	 * mode and this device is always windowed (see DXCreate), so the fallback
	 * below is belt and braces - but a device that fails to be created
	 * because of a present interval would be a black screen with no message,
	 * and this turns it into a log line and a running game.
	 *
	 * g_vsyncActive records what the device ended up with. The refresh-bound
	 * frame rate (fps_mode 2) hangs on it: without a blocking present there
	 * is nothing to bound the rate, and the loop would run uncapped. */
	if (pp->PresentationInterval == D3DPRESENT_INTERVAL_ONE &&
		!(caps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE))
	{
		Log("DXCreateD3DDevice: the adapter does not offer D3DPRESENT_INTERVAL_ONE - VSync off");
		pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	}

	if (DXAttempt(d3d->CreateDevice(adapter, D3DDEVTYPE_HAL, hwnd, flags, pp, device)) != DD_OK)
	{
		Log("DXCreateD3DDevice Failed");
		return 0;
	}

	g_vsyncActive = pp->PresentationInterval == D3DPRESENT_INTERVAL_ONE ? 1 : 0;

	/* How much texture memory this card has. Asked here and nowhere else,
	 * because this is the only moment the answer means what it says: the
	 * device is one instruction old, it owns its back buffer and
	 * its depth buffer and nothing else, and no level has been loaded. Asked
	 * later - at a level start, say - the number is already reduced by
	 * whatever the last level left on the card, and a budget built on it
	 * would shrink a little with every level the player passes through.
	 *
	 * The number is what Direct3D 9 is willing to promise, not what the card
	 * physically holds: it counts the driver's shared system memory too and
	 * it is rounded, and on a large card it stops at four gigabytes because
	 * it is a UINT of bytes. All three are fine for what it is used for -
	 * deciding whether a level's pages fit with room to spare.
	 *
	 * Zero when the device does not answer; the budget then falls back to
	 * "no limit". */
	DXTextureMemoryAtStart = *device ? (*device)->GetAvailableTextureMem() : 0;

	Log("DXCreateD3DDevice Successful - the device offers %.0f MiB of texture memory",
		(double)DXTextureMemoryAtStart / 1048576.0);
	return 1;
}

/* The apostrophe screenshot's file: <name>0000.tga, <name>0001.tga, ... in
 * the working folder, written by DeviceSaveTga (devicewindow.h). The path
 * comes back in `path`; returns the bytes written, 0 when nothing was. */
long DXSaveScreen(LPDIRECT3DSURFACE9 surf, const char* name, char* path,
	size_t size)
{
	static long num = 0;

	_snprintf_s(path, size, _TRUNCATE, "%s%04d.tga", name, num);
	num++;
	return DeviceSaveTga(surf, path);
}

/* The gamma pass: the Display page's Gamma row (tomb5.gamma, 0 to 100) put
 * over everything the engine presents.
 *
 * Why a pass and not the device's gamma ramp. SetGammaRamp acts on a
 * full-screen exclusive swap chain only, and this engine never makes one:
 * DXCreate creates the device windowed in both of its modes, full screen
 * being a border-less window, and Alt+Enter only makes the other of the two
 * - the ramp would do nothing in either. A ramp is also the display's own,
 * not the window's, so one set by hand stays on the desktop if the game
 * dies with it set. The pass touches the frame and nothing outside it.
 *
 * What it is. Just before the present, the back buffer is copied into a
 * texture of its size (StretchRect), and drawn back over it, one quad,
 * through hd_gamma.hlsl's curve. The engine presents in two places:
 * DXShowFrame, which the world, the menus and the inventory over it, the
 * loading bar, the credits and the films end in, and the loading bar's
 * frames while the HD build runs (drawbars.cpp, LoadBarPresentHeld). Both
 * take the same steps (DXGammaStart, DXGammaFinish), so every picture is
 * covered.
 *
 * After the present the copy is put back into the back buffer. The engine
 * reads the last presented frame out of it - the inventory's and the pause
 * menu's picture of the world behind them (CreateMonoScreen) - and draws
 * over it again; without the copy back that picture would carry the curve
 * once and then be given it a second time. So the back buffer between two
 * frames is the engine's own picture, as it is with the row at 50.
 *
 * At 50, the middle, no pass runs at all, and none of the objects below is
 * even made: the picture is the engine's own, byte for byte. A step that
 * fails leaves the frame as it is and says so once on the -log. */
#include "hd/hd_shader/hd_gamma_ps.inc"

#define GAMMA_MIDDLE	50

static IDirect3DTexture9* g_gammaCopy = 0;
static IDirect3DSurface9* g_gammaCopyLevel = 0;
static IDirect3DPixelShader9* g_gammaShader = 0;
static UINT g_gammaW = 0, g_gammaH = 0;
static bool g_gammaRefused = 0;	// one -log line per device, not per frame
static long g_gammaNamed = -1;	// the value the -log line last named

/* With -log, what stage 0 held as the last present's pass drew (DXGammaSeen,
 * the loading screen's LOADFRAME line): 0 no pass drew, 1 its copy, 2
 * another texture or none, which g_gammaSeen names. A name only: it is
 * compared, never drawn with. */
static long g_gammaDrew = 0;
static const void* g_gammaSeen = 0;

// Everything the pass made; the device's own release and reset come here.
static void GammaRelease()
{
	if (g_gammaCopyLevel) { g_gammaCopyLevel->Release(); g_gammaCopyLevel = 0; }
	if (g_gammaCopy) { g_gammaCopy->Release(); g_gammaCopy = 0; }
	if (g_gammaShader) { g_gammaShader->Release(); g_gammaShader = 0; }
	g_gammaW = g_gammaH = 0;
	g_gammaRefused = 0;
}

/* One new -log line, once at the first present and again at each change of
 * the value; a refused step adds itself at the end, once per device:
 *   DXLOG gamma row=<0..100> gamma=<g> power=<1/g> pass=<on|off>
 *   [refused=<copy texture|shader|copy|draw> hr=<0x...>] */
static void GammaLog(ulong row, const char* refused, HRESULT hr)
{
	const double gamma = pow(2.0, ((double)row - GAMMA_MIDDLE) / 50.0);

	if (refused)
		Log("DXLOG gamma row=%lu gamma=%.4f power=%.4f pass=%s refused=%s "
			"hr=0x%08lX", row, gamma, 1.0 / gamma,
			row == GAMMA_MIDDLE ? "off" : "on", refused, (unsigned long)hr);
	else
		Log("DXLOG gamma row=%lu gamma=%.4f power=%.4f pass=%s", row, gamma,
			1.0 / gamma, row == GAMMA_MIDDLE ? "off" : "on");
}

static bool GammaRefuse(ulong row, const char* step, HRESULT hr)
{
	if (!g_gammaRefused)
		GammaLog(row, step, hr);

	g_gammaRefused = 1;
	return 0;
}

/* The pass itself; true when the back buffer holds the curve's picture and
 * the copy the engine's, so that GammaRestore has something to put back.
 * Every state it sets is read first and set back after, so the next frame
 * finds the device as the engine left it - no state block, which would
 * hold references to whatever was bound and stand in the way of a Reset. */
static bool GammaPass(LPDIRECT3DDEVICE9 dev, ulong row)
{
	static const D3DRENDERSTATETYPE rs[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_FOGENABLE,
		D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_COLORWRITEENABLE,
		D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE
	};
	static const DWORD rs_set[] =
	{
		D3DZB_FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, FALSE, FALSE, FALSE,
		0xF, FALSE, 0, D3DFILL_SOLID
	};
	static const D3DSAMPLERSTATETYPE ss[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE
	};
	static const DWORD ss_set[] =
	{
		D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, D3DTADDRESS_CLAMP,
		D3DTADDRESS_CLAMP, FALSE
	};
	const long nrs = sizeof(rs) / sizeof(rs[0]);
	const long nss = sizeof(ss) / sizeof(ss[0]);
	struct { float x, y, z, rhw, u, v; } quad[4];
	DWORD rs_old[sizeof(rs) / sizeof(rs[0])];
	DWORD ss_old[sizeof(ss) / sizeof(ss[0])];
	DWORD tci_old, ttf_old, fvf_old;
	LPDIRECT3DSURFACE9 back = 0, target_old = 0;
	LPDIRECT3DBASETEXTURE9 tex_old = 0;
	LPDIRECT3DPIXELSHADER9 ps_old = 0;
	LPDIRECT3DVERTEXSHADER9 vs_old = 0;
	LPDIRECT3DVERTEXDECLARATION9 decl_old = 0;
	LPDIRECT3DVERTEXBUFFER9 vb_old = 0;
	UINT vb_offset = 0, vb_stride = 0;
	D3DVIEWPORT9 view_old;
	D3DSURFACE_DESC desc;
	float c0_old[4], c0[4] = { (float)row, 0, 0, 0 };
	HRESULT hr;
	long i;

	if (g_gammaRefused)
		return 0;

	hr = dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);

	if (FAILED(hr))
		return GammaRefuse(row, "copy", hr);

	back->GetDesc(&desc);

	// The copy follows the back buffer's size: Alt+Enter and a resolution
	// change make a new device and release this one in DXClose, but the
	// size is asked here as well rather than trusted.
	if (g_gammaCopy && (desc.Width != g_gammaW || desc.Height != g_gammaH))
	{
		g_gammaCopyLevel->Release();
		g_gammaCopy->Release();
		g_gammaCopyLevel = 0;
		g_gammaCopy = 0;
	}

	if (!g_gammaCopy)
	{
		hr = dev->CreateTexture(desc.Width, desc.Height, 1,
			D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &g_gammaCopy,
			0);

		if (SUCCEEDED(hr))
			hr = g_gammaCopy->GetSurfaceLevel(0, &g_gammaCopyLevel);

		if (FAILED(hr))
		{
			back->Release();
			GammaRelease();
			return GammaRefuse(row, "copy texture", hr);
		}

		g_gammaW = desc.Width;
		g_gammaH = desc.Height;
	}

	if (!g_gammaShader)
	{
		hr = dev->CreatePixelShader((const DWORD*)kHDGammaPS, &g_gammaShader);

		if (FAILED(hr))
		{
			back->Release();
			return GammaRefuse(row, "shader", hr);
		}
	}

	hr = dev->StretchRect(back, 0, g_gammaCopyLevel, 0, D3DTEXF_NONE);

	if (FAILED(hr))
	{
		back->Release();
		return GammaRefuse(row, "copy", hr);
	}

	/* From the read of stage 0 to its restore under the stage lock: while
	 * a level loads, the loading thread sets stage 0 too as the level's
	 * pages go (texture.cpp), and a set between the copy's bind and the
	 * draw would fill the whole window with something else. */
	TextureStageLock();

	for (i = 0; i < nrs; i++)
		dev->GetRenderState(rs[i], &rs_old[i]);

	for (i = 0; i < nss; i++)
		dev->GetSamplerState(0, ss[i], &ss_old[i]);

	dev->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &tci_old);
	dev->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf_old);
	dev->GetTexture(0, &tex_old);
	dev->GetPixelShader(&ps_old);
	dev->GetVertexShader(&vs_old);
	dev->GetFVF(&fvf_old);
	dev->GetVertexDeclaration(&decl_old);
	dev->GetStreamSource(0, &vb_old, &vb_offset, &vb_stride);
	dev->GetPixelShaderConstantF(0, c0_old, 1);
	dev->GetRenderTarget(0, &target_old);
	dev->GetViewport(&view_old);

	dev->SetRenderTarget(0, back);	// the viewport becomes the whole of it

	for (i = 0; i < nrs; i++)
		dev->SetRenderState(rs[i], rs_set[i]);

	for (i = 0; i < nss; i++)
		dev->SetSamplerState(0, ss[i], ss_set[i]);

	dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
		D3DTTFF_DISABLE);
	dev->SetTexture(0, g_gammaCopy);
	dev->SetVertexShader(0);
	dev->SetPixelShader(g_gammaShader);
	dev->SetPixelShaderConstantF(0, c0, 1);
	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

	/* Pixel for texel: D3D9 puts a pixel's centre on the integer, so the
	 * quad's corners sit half a pixel up and left and every pixel samples
	 * the centre of its own texel, point filtered. */
	for (i = 0; i < 4; i++)
	{
		quad[i].x = (i & 1 ? (float)desc.Width : 0.0f) - 0.5f;
		quad[i].y = (i & 2 ? (float)desc.Height : 0.0f) - 0.5f;
		quad[i].z = 0.0f;
		quad[i].rhw = 1.0f;
		quad[i].u = i & 1 ? 1.0f : 0.0f;
		quad[i].v = i & 2 ? 1.0f : 0.0f;
	}

	dev->BeginScene();

	if (gLogEnabled)
	{
		LPDIRECT3DBASETEXTURE9 seen = 0;

		dev->GetTexture(0, &seen);
		g_gammaSeen = seen;
		g_gammaDrew = seen == g_gammaCopy ? 1 : 2;

		if (seen)
			seen->Release();
	}

	hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
	dev->EndScene();

	// Back as it was found, in the reverse order. DrawPrimitiveUP leaves
	// stream 0 empty, so that is put back too.
	if (target_old)
	{
		dev->SetRenderTarget(0, target_old);
		target_old->Release();
	}

	dev->SetViewport(&view_old);
	dev->SetPixelShaderConstantF(0, c0_old, 1);
	dev->SetStreamSource(0, vb_old, vb_offset, vb_stride);

	if (fvf_old)
		dev->SetFVF(fvf_old);
	else
		dev->SetVertexDeclaration(decl_old);

	dev->SetVertexShader(vs_old);
	dev->SetPixelShader(ps_old);
	dev->SetTexture(0, tex_old);
	dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, ttf_old);
	dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, tci_old);

	for (i = 0; i < nss; i++)
		dev->SetSamplerState(0, ss[i], ss_old[i]);

	for (i = 0; i < nrs; i++)
		dev->SetRenderState(rs[i], rs_old[i]);

	TextureStageUnlock();

	if (vb_old) vb_old->Release();
	if (decl_old) decl_old->Release();
	if (vs_old) vs_old->Release();
	if (ps_old) ps_old->Release();
	if (tex_old) tex_old->Release();
	back->Release();

	if (FAILED(hr))
	{
		// The draw did not happen, so the back buffer is still the copy.
		return GammaRefuse(row, "draw", hr);
	}

	return 1;
}

// After the present: the engine's own picture back into the back buffer.
static void GammaRestore(LPDIRECT3DDEVICE9 dev)
{
	LPDIRECT3DSURFACE9 back = 0;

	if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
	{
		dev->StretchRect(g_gammaCopyLevel, 0, back, 0, D3DTEXF_NONE);
		back->Release();
	}
}

/* Before a present, in both of the engine's presents (DXShowFrame and the
 * loading bar's held frames, drawbars.cpp's LoadBarPresentHeld): the -log
 * line at the first present and at each change of the value, then the
 * pass. True when DXGammaFinish has the engine's picture to put back. At
 * the middle - and above the top, which the registry read never lets
 * through - no pass runs and nothing is made. */
bool DXGammaStart()
{
	const ulong row = tomb5.gamma;

	g_gammaDrew = 0;

	if ((long)row != g_gammaNamed)
	{
		g_gammaNamed = (long)row;
		GammaLog(row, 0, S_OK);
	}

	if (row != GAMMA_MIDDLE && row <= 100)
		return GammaPass(G_dxptr->lpD3DDevice, row);

	return 0;
}

// After a present: the copy back, unless the device is lost.
void DXGammaFinish(bool curved, HRESULT hr)
{
	if (curved && hr != D3DERR_DEVICELOST)
		GammaRestore(G_dxptr->lpD3DDevice);
}

/* With -log, the last present's pass: 0 no pass drew, 1 it drew with its
 * copy, 2 with what `tex` names instead (see g_gammaDrew). */
long DXGammaSeen(const void** tex)
{
	*tex = g_gammaSeen;
	return g_gammaDrew;
}

HRESULT DXShowFrame()
{
	HRESULT hr;

	// Before the screenshot, so that the file is the picture as shown.
	const bool curved = DXGammaStart();

	/* The engine's screenshot. One new -log line per file: the read-back's
	 * HRESULT (DeviceReadBack: the first step that failed, or
	 * GetRenderTargetData's), the file and its bytes.
	 *   DXLOG screenshot hr=<0x...> file=<TombNNNN.tga or none> bytes=<n> */
	if (keymap[DIK_APOSTROPHE])
	{
		LPDIRECT3DSURFACE9 sysMem;
		char path[MAX_PATH] = "none";
		long bytes = 0;
		const HRESULT got = DeviceReadBack(G_dxptr->lpD3DDevice, &sysMem);

		if (sysMem)
		{
			bytes = DXSaveScreen(sysMem, "Tomb", path, sizeof(path));
			sysMem->Release();
		}

		Log("DXLOG screenshot hr=0x%08lX file=%s bytes=%ld",
			(unsigned long)got, path, bytes);
	}

	hr = G_dxptr->lpD3DDevice->Present(0, 0, 0, 0);

	/* The frame clock, and the only place it can be taken. This function is
	 * the whole engine's one present: S_DumpScreen (the game frame, the
	 * inventory, the title menu), S_DumpScreenFrame (the loading bar and the
	 * credits) and fmv.cpp all end here and nowhere else, so one call here
	 * counts every frame the player ever sees, once. It is deliberately after
	 * Present and before the device-lost branch below - the interval it
	 * measures is present-to-present, which is the frame rate as felt, wait
	 * and driver time included. Without -log it is a bool test and a return
	 * (hd_level.h). */
	HDFramePresented();

	// The back buffer the engine's own picture again (the gamma pass).
	DXGammaFinish(curved, hr);

	/* A lost device. "Full screen" is a windowed device (see DXCreate), which
	 * a focus change never loses; another desktop taking the screen does -
	 * the secure desktop of Ctrl+Alt+Del, the lock screen, a UAC prompt - and
	 * so do a display mode another program sets and a monitor unplugged.
	 * Every present fails until the device is given back, and this thread
	 * cannot give it back: D3D9 refuses a Reset on any thread but the one
	 * that made the device, the window's, and while any D3DPOOL_DEFAULT
	 * object lives (D3DERR_INVALIDCALL in two frames running, in the dump of
	 * a game frozen behind Ctrl+Alt+Del), and a reset here would take the HD
	 * level's objects away in the middle of a frame (HDLevelDeviceLost).
	 * So the window is told, once per loss, and the frames go on drawing
	 * nothing; the window thread makes a new device once the player is back
	 * in the window, with this thread held at the start of its scene
	 * (winmain.cpp, WinDeviceRestore).
	 *   WINLOG device lost coop=<ok|lost|notreset|internal|other> hr=<0x...>
	 *   - TestCooperativeLevel's answer at the loss */
	if (hr == D3DERR_DEVICELOST)
	{
		if (!InterlockedExchange(&DXDeviceLost, 1))
		{
			const HRESULT coop = G_dxptr->lpD3DDevice->TestCooperativeLevel();

			Log("WINLOG device lost coop=%s hr=0x%08lX", DXCoopName(coop),
				(unsigned long)coop);
			PostMessage(G_dxptr->hWnd, WM_APP_DEVICE, 0, 0);
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
		// Every HD object - both buffers, the page textures, the two
		// shaders, the vertex declaration - was made on this device, and a
		// new window size or Alt+Enter comes through here (DXResizeToClient
		// / DXToggleFullScreen call DXCreate, which calls DXClose first).
		// Letting them outlive the device would leave the HD path drawing
		// through released interfaces on the next frame. They go now;
		// DXCreate builds them again on the new device once the classic
		// pages have moved (MoveTexturesToDevice, HDLevelDeviceRecreated).
		// The film's texture was made on this device too and goes with it;
		// the next film makes it again (PlayFmv).
		HDLevelDeviceLost();
		FmvDeviceLost();
		GammaRelease();	// the gamma pass's copy and shader

		Log("Released %s @ %x - RefCnt = %d", "Direct3DDevice", G_dxptr->lpD3DDevice, G_dxptr->lpD3DDevice->Release());
		G_dxptr->lpD3DDevice = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "Direct3DDevice");
}

/* The windowed state's place, at the start and on Alt+Enter: the windowed
 * style, then the window where it was last left when that place still lies
 * on a connected monitor's work area (WinPlaceUsable), maximized again when
 * it was; else a client of the mode chosen in the setup dialog, fitted to
 * the work area of the adapter's monitor and centred on it. The place taken
 * becomes DXWindowPlace, so that the next Alt+Enter brings the window back
 * to it.
 *   DXLOG window place source=<remembered|centre> client=<l,t,r,b in screen
 *   coordinates> maximized=<0|1> monitors=<work areas seen> */
static void DXPlaceWindow(HWND hWnd, UINT adapter, const DXDISPLAYMODE* dm)
{
	const WPRect frame = DeviceWindowFrame(DeviceWindowStyle(hWnd, false));
	DeviceWindowWorks works;
	WPRect client;
	long maximize = 0;
	const char* source = "centre";

	DeviceWindowAllWorks(&works);

	if (DXWindowPlace.have && WinPlaceUsable(DXWindowPlace.client, frame,
		works.area, works.count))
	{
		client = DXWindowPlace.client;
		maximize = DXWindowPlace.maximized;
		source = "remembered";
	}
	else
	{
		const WPRect work = DeviceWindowAdapterWork(G_d3d, adapter);
		long w, h;

		WinPlaceFit(dm->w, dm->h, frame, work, &w, &h);
		client = WinPlaceCentre(w, h, frame, work);
	}

	DeviceWindowPlaceClient(hWnd, client, &G_dxptr->rViewport,
		&G_dxptr->rScreen);
	DXWindowPlace.client = client;
	DXWindowPlace.maximized = maximize;
	DXWindowPlace.have = 1;

	if (maximize)
	{
		ShowWindow(hWnd, SW_SHOWMAXIMIZED);

		const WPRect now = DeviceWindowClient(hWnd);

		SetRect(&G_dxptr->rViewport, 0, 0, WPWidth(now), WPHeight(now));
		SetRect(&G_dxptr->rScreen, now.left, now.top, now.right, now.bottom);
	}

	Log("DXLOG window place source=%s client=%ld,%ld,%ld,%ld maximized=%ld "
		"monitors=%ld", source, client.left, client.top, client.right,
		client.bottom, maximize, works.count);
}

// D3D9 owns/manages the swap chain (back buffer + depth/stencil) internally via
// D3DPRESENT_PARAMETERS - no separate DirectDraw primary/back/z surfaces or a
// viewport object are needed anymore, unlike the original DirectDraw4/Direct3D3 path.
static long DXCreateIn(long w, long h, long bpp, long Flags, DXPTR* dxptr,
	HWND hWnd, long WindowStyle);

long DXCreate(long w, long h, long bpp, long Flags, DXPTR* dxptr, HWND hWnd, long WindowStyle)
{
	Log(__FUNCTION__);

	// Every WM_SIZE this function causes is its own (DXWindowBusy).
	InterlockedIncrement(&DXWindowBusy);
	const long made = DXCreateIn(w, h, bpp, Flags, dxptr, hWnd, WindowStyle);
	InterlockedDecrement(&DXWindowBusy);
	return made;
}

static long DXCreateIn(long w, long h, long bpp, long Flags, DXPTR* dxptr,
	HWND hWnd, long WindowStyle)
{
	DXDISPLAYMODE* dm;
	RECT r;
	UINT adapter;

	G_dxptr = dxptr;
	G_dxptr->Flags = Flags;
	G_dxptr->hWnd = hWnd;
	G_dxptr->WindowStyle = WindowStyle;

	// A device here already is Alt+Enter or a resolution change: the classic
	// pages made on it move to the new one below. So do the pages of a
	// device released by an attempt that could not make the next one
	// (dx_pages_waiting), for the attempt after it.
	const bool recreate = G_dxptr->lpD3DDevice != 0 || dx_pages_waiting;

	DXClose();

	if (!G_d3d)
		return 0;

	G_dxptr->lpD3D = G_d3d;
	adapter = G_dxinfo->DDInfo[G_dxinfo->nDD].AdapterOrdinal;
	dm = &G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].DisplayModes[G_dxinfo->nDisplayMode];

	if (!(Flags & DXF_FULLSCREEN))
	{
		Log("DXCreate: Windowed Mode");

		/* The back buffer is the client's size, whatever size the window
		 * has. A resize (DXResizeToClient) leaves the window where it is and
		 * reads the client; the start and Alt+Enter place the window first
		 * (DXPlaceWindow). */
		if (dx_keep_window)
		{
			const WPRect now = DeviceWindowClient(hWnd);

			SetRect(&G_dxptr->rViewport, 0, 0, WPWidth(now), WPHeight(now));
			SetRect(&G_dxptr->rScreen, now.left, now.top, now.right,
				now.bottom);
		}
		else
			DXPlaceWindow(hWnd, adapter, dm);

		if (dx_force_w && dx_force_h)
			SetRect(&G_dxptr->rViewport, 0, 0, dx_force_w, dx_force_h);

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
		// all (a switch of the desktop, Ctrl+Alt+Del or the lock screen, still
		// loses it: see DXShowFrame), and a border-less window filling the
		// screen is visually
		// indistinguishable from real exclusive fullscreen. WinSetStyle() already
		// strips the window border (WS_POPUP) for DXF_FULLSCREEN, so only
		// positioning/sizing and PresentParams.Windowed (below) need to change here.
		// One behavioural consequence: since there's no real mode-switch anymore,
		// "fullscreen" always renders at the monitor's current desktop resolution
		// regardless of which entry is selected in the resolution dropdown - the
		// same way windowed mode (above) already works.
		Log("DXCreate: Full Screen Mode (borderless windowed)");

		long w, h;

		// From a maximized window the maximized state goes first, and the
		// popup style is set before the window covers the monitor, so that
		// no frame of the windowed state is left round the client.
		if (IsZoomed(hWnd))
			ShowWindow(hWnd, SW_RESTORE);

		DeviceWindowStyle(hWnd, true);
		DeviceWindowFullRect(G_d3d, adapter, dm->w, dm->h, &r, &w, &h);
		G_dxptr->dwRenderWidth = w;
		G_dxptr->dwRenderHeight = h;
		G_dxptr->rViewport.top = 0;
		G_dxptr->rViewport.left = 0;
		G_dxptr->rViewport.right = G_dxptr->dwRenderWidth;
		G_dxptr->rViewport.bottom = G_dxptr->dwRenderHeight;
		DeviceWindowShowFull(hWnd, &r, G_dxptr->dwRenderWidth,
			G_dxptr->dwRenderHeight);
	}

	/* The present parameters (devicewindow.h): windowed always - see the
	 * border-less-windowed comment above - discard, one X8R8G8B8 back buffer
	 * of the size just worked out, D24S8 when the Z buffer is asked for.
	 *
	 * VSync, and the one line the whole option is.
	 *
	 * The engine presented with D3DPRESENT_INTERVAL_IMMEDIATE from the day it
	 * was ported, and the thirty frames a second came from the busy-wait in
	 * S_DumpScreen instead - a `while (!Sync());` spin that burns a core for
	 * the rest of every frame's slot. With the interval at ONE the present
	 * itself waits for the display and the picture is whole rather than torn.
	 *
	 * Where this takes effect, and where it does not: PresentationInterval is
	 * a device-creation parameter, read here, and this function runs at
	 * startup, on Alt+Enter and on a new window size (DXToggleFullScreen
	 * and DXResizeToClient both call DXCreate). A lost device's new one
	 * (DXRestoreDevice) is made here as well, so it follows
	 * too. Switching the menu row over does not change a running device -
	 * the row says so, and the setting is picked up the next time the game
	 * starts. Resetting the device on the spot would work
	 * with the HD art switched off and would tear down every D3DPOOL_DEFAULT
	 * page of it when it is on (see HDLevelDeviceLost), which is a heavy
	 * price for a switch that is set once.
	 *
	 * The display's own rate is read here for the same reason: this is where
	 * the adapter is at hand. The refresh-bound frame rate divides by it. */
	DevicePresentParams(&G_dxptr->PresentParams, hWnd, G_dxptr->dwRenderWidth,
		G_dxptr->dwRenderHeight, tomb5.vsync != 0, (Flags & DXF_ZBUFFER) != 0);

	{
		D3DDISPLAYMODE mode;

		g_displayHz = 60;

		if (SUCCEEDED(G_d3d->GetAdapterDisplayMode(adapter, &mode)) &&
			mode.RefreshRate >= 20 && mode.RefreshRate <= 1000)
			g_displayHz = (long)mode.RefreshRate;

		Log("DXCreate: display %ld Hz, VSync %s", g_displayHz,
			tomb5.vsync ? "on" : "off");
	}

	// Leave FullScreen_RefreshRateInHz at 0 (let the driver pick the display's current rate)
	// rather than forcing dm->RefreshRate - on Windows 11, forcing an exact refresh rate is
	// what's actually causing D3DERR_INVALIDCALL from CreateDevice at some resolutions.

	if (!DXCreateD3DDevice(G_dxptr->lpD3D, adapter, hWnd, &G_dxptr->PresentParams, &G_dxptr->lpD3DDevice))
	{
		DXClose();
		dx_pages_waiting = recreate;
		return 0;
	}

	dx_pages_waiting = 0;

	if (recreate)
		MoveTexturesToDevice();

	return 1;
}

/* Whether the device that exists presents with D3DPRESENT_INTERVAL_ONE -
 * not whether the option is on. Zero before the first DXCreate, which is
 * also the honest answer then. */
long DXVSyncActive()
{
	return g_vsyncActive;
}

// What the display the device was made on runs at, in Hz. Never zero.
long DXDisplayHz()
{
	return g_displayHz;
}

/* The back buffer made the client's size: a drag of the frame has ended,
 * the window was maximized, restored or snapped, or a resolution key gave
 * it a new size. The window stays as it is. A lost device takes the same
 * way back (DXRestoreDevice), at the size it had.
 *
 * The device is made anew, as Alt+Enter makes it: DXCreate releases the HD
 * level with the old device (DXClose, HDLevelDeviceLost) and builds it again
 * on the new one after the classic pages have moved (MoveTexturesToDevice,
 * HDLevelDeviceRecreated), whose DXLOG device recreated line gives the
 * build's cost. A Reset of the same device would also need every
 * D3DPOOL_DEFAULT object released first and the HD level built again after
 * it; the rebuild is the cost either way. The device is the engine's own,
 * so the caller holds the game thread (winmain.cpp).
 *
 * A device of the new size that cannot be made leaves the old size, the
 * picture stretched into the client as it is during a drag.
 *   DXLOG window resize <why> back=<WxH before>-><WxH after> ok=<0|1>
 *   path=recreate ms=<the whole recreate, in milliseconds> */
long DXResizeToClient(const char* why)
{
	const ulong oldW = G_dxptr->dwRenderWidth;
	const ulong oldH = G_dxptr->dwRenderHeight;
	LARGE_INTEGER start, end, rate;
	long val;

	Log(__FUNCTION__);
	QueryPerformanceFrequency(&rate);
	QueryPerformanceCounter(&start);
	dx_keep_window = 1;
	G_dxptr->Flags |= DXF_NOFREE;
	val = DXCreate(0, 0, 0, G_dxptr->Flags, G_dxptr, G_dxptr->hWnd,
		G_dxptr->WindowStyle);

	if (!val)
	{
		dx_force_w = oldW;
		dx_force_h = oldH;
		val = DXCreate(0, 0, 0, G_dxptr->Flags, G_dxptr, G_dxptr->hWnd,
			G_dxptr->WindowStyle);
		dx_force_w = 0;
		dx_force_h = 0;
	}

	G_dxptr->Flags ^= DXF_NOFREE;
	dx_keep_window = 0;
	QueryPerformanceCounter(&end);
	Log("DXLOG window resize %s back=%lux%lu->%lux%lu ok=%ld path=recreate "
		"ms=%ld", why, oldW, oldH, G_dxptr->dwRenderWidth,
		G_dxptr->dwRenderHeight, val,
		(long)((end.QuadPart - start.QuadPart) * 1000 / rate.QuadPart));
	return val;
}

HRESULT DXDeviceCoop()
{
	if (!G_dxptr || !G_dxptr->lpD3DDevice)
		return D3D_OK;

	return G_dxptr->lpD3DDevice->TestCooperativeLevel();
}

const char* DXCoopName(HRESULT hr)
{
	switch (hr)
	{
	case D3D_OK:
		return "ok";

	case D3DERR_DEVICELOST:
		return "lost";

	case D3DERR_DEVICENOTRESET:
		return "notreset";

	case D3DERR_DRIVERINTERNALERROR:
		return "internal";
	}

	return "other";
}

/* A lost device (DXDeviceLost) given back: a new device, made as a new
 * size makes it - DXClose releases the HD level, the film's texture, the
 * gamma pass and the old device, the classic pages move and the HD level
 * is built again on the new device (MoveTexturesToDevice) - rather than a
 * Reset, which every D3DPOOL_DEFAULT object still alive would refuse and
 * which would leave the HD level away until the next level load. The
 * DXLOG window resize line says `device` and gives the cost. */
long DXRestoreDevice()
{
	const long made = DXResizeToClient("device");

	if (made)
		InterlockedExchange(&DXDeviceLost, 0);

	return made;
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
	InterlockedIncrement(&DXWindowBusy);
	DXCreate(dm->w, dm->h, dm->bpp, G_dxptr->Flags, G_dxptr, G_dxptr->hWnd, G_dxptr->WindowStyle);
	WinSetStyle(G_dxptr->Flags & DXF_FULLSCREEN, G_dxptr->WindowStyle);
	InterlockedDecrement(&DXWindowBusy);
	G_dxptr->Flags ^= DXF_NOFREE;
	DXLogWindow("toggle");
	return 1;
}

/* One new -log line with the window and its swap chain as they stand
 * (devicewindow.h): after the start, after every Alt+Enter and after every
 * resolution change, so that the states can be held against each other.
 *   DXLOG window <start|toggle|mode> style=<GWL_STYLE> ex=<GWL_EXSTYLE>
 *   topmost= visible= foreground= window=<l,t,r,b> client=<l,t,r,b in
 *   screen coordinates> monitor=<l,t,r,b> windowed= swap= format= back=<WxH>
 *   count= multisample= interval= hz= flags= depth= behavior=<creation
 *   flags> - the present parameters as the device reports them. */
void DXLogWindow(const char* when)
{
	DeviceWindowState state;
	char line[512];

	if (!gLogEnabled || !G_dxptr || !G_dxptr->hWnd)
		return;

	DeviceWindowMeasure(G_dxptr->hWnd, G_dxptr->lpD3DDevice, &state);
	DeviceWindowStateText(&state, line, sizeof(line));
	Log("DXLOG window %s %s", when, line);
}

// D3D9 adapters are enumerated directly (GetAdapterCount/GetAdapterIdentifier/EnumAdapterModes) -
// no more DirectDraw device -> Direct3D-Immediate-Mode device -> display mode 3-level callback
// enumeration. The DXDIRECTDRAWINFO/DXD3DDEVICE/DXDISPLAYMODE tree shape is kept unchanged so
// setupdlg.cpp/registry.cpp don't need to change, only how it's populated (see DXGetInfo below).

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
					wsprintf(ddi->MonitorLabel, " (Windows display %s)", p);
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

		// One entry per resolution, not per mode. EnumAdapterModes reports every
		// resolution once per refresh rate the monitor can drive it at, so a
		// 165 Hz screen produced seven identical "2560x1440" lines in the setup
		// dialog - it only ever printed width and height.
		//
		// They were not merely ugly, they were seven names for one thing: the
		// rate is never used. Device creation deliberately leaves
		// FullScreen_RefreshRateInHz at 0 so the driver keeps the display at
		// its current rate (see DXCreateD3DDevice), and dm->RefreshRate is
		// carried along for the log alone.
		//
		// In the original engine the same duplication existed for a different
		// reason - the DirectDraw enumeration ran over several colour depths,
		// so a resolution appeared once per bit depth. The port asks for the
		// single format D3DFMT_X8R8G8B8, which is why dm->bpp is now always 32.
		for (UINT m = 0; m < nModes; m++)
		{
			bool seen;

			if (FAILED(G_d3d->EnumAdapterModes(i, D3DFMT_X8R8G8B8, m, &mode)))
				continue;

			seen = false;

			for (long k = 0; k < device->nDisplayModes; k++)
			{
				if (device->DisplayModes[k].w == (long)mode.Width &&
					device->DisplayModes[k].h == (long)mode.Height)
				{
					seen = true;
					break;
				}
			}

			if (seen)
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

	// No sound device enumeration any more. XAudio2 always plays through the
	// system's default endpoint - the engine never picked a device by GUID,
	// and the setup dialog's list only ever fed a combo box whose selection
	// nobody read. Whether sound comes up at all is answered by DXDSCreate,
	// and sound_active carries that answer (see SOUND_Init).
	//
	// With this call goes the last real use of DirectSound, so Dsound.lib
	// leaves the linker line. dsound.h stays in pch.h, for DSBPLAY_LOOPING.
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
//     (bit0="Joy 1" etc) - the exact numbering JoyButtonName()/jLayout[] already
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
	joystick_is_xinput = 0;	// this path delivers raw HID bits (name tables see input.cpp)

	// Axes were ranged to -1000..1000 in DXInitInput via DIPROP_RANGE, so this
	// scales to roughly the same -128..127 ballpark XInput's path produces above.
	long lx = js.lX;
	long ly = js.lY;

	// The stick's raw pair, taken before its per-axis zone. Modern reads
	// this pair and applies its own radial zone to it (ModernStickDeadZone,
	// specific\input.cpp), because the per-axis zeroing two lines down
	// snaps a nearly-axial push onto the axis and loses the direction the
	// thumb was holding - see the matching comment on the XInput path below
	// for why the d-pad handling makes keeping this pair necessary. Written
	// on every poll, read only under tomb5.control_modern.
	joystick_raw_lx = lx / 8;
	joystick_raw_ly = ly / 8;	// DirectInput joystick Y is already "up = negative", like the read pair below

	const long deadzone = 150;

	if (lx > -deadzone && lx < deadzone) lx = 0;
	if (ly > -deadzone && ly < deadzone) ly = 0;

	joystick_read_x = lx / 8;
	joystick_read_y = ly / 8;	// DirectInput joystick Y is already "up = negative", matching S_UpdateInput()'s joy_y<0=forward - no invert needed here (unlike the XInput thumbstick above)

	long fire = 0;	// declared before the POV block below, whose d-pad button bits need it to already exist

	if (LOWORD(js.rgdwPOV[0]) != 0xFFFF)	// POV hat centered reports as -1/0xFFFFFFFF
	{
		// hundredths of a degree, 0 = up, clockwise (4500 = up-right, 9000 = right, ...).
		// Each band is inclusive at both ends so a diagonal angle (4500/13500/22500/31500)
		// falls into its two neighbouring bands at once, setting both axes together -
		// this was previously 4 exclusive 90-degree slices with gaps at the exact
		// diagonal angles, so a diagonal press matched none of them and only one axis
		// (whichever direction got pressed fractionally first) ever moved.
		DWORD pov = js.rgdwPOV[0];
		if (pov <= 4500 || pov >= 31500) joystick_read_y = -20;
		if (pov >= 4500 && pov <= 13500) joystick_read_x = 20;
		if (pov >= 13500 && pov <= 22500) joystick_read_y = 20;
		if (pov >= 22500 && pov <= 31500) joystick_read_x = -20;

		// The d-pad as 4 more digital buttons too (bits 20-23) - same bands,
		// same idea as the XInput path's own twin block below; see its
		// comment for why.
		if (pov <= 4500 || pov >= 31500) fire |= 1 << 20;	// D-Pad Up
		if (pov >= 13500 && pov <= 22500) fire |= 1 << 21;	// D-Pad Down
		if (pov >= 22500 && pov <= 31500) fire |= 1 << 22;	// D-Pad Left
		if (pov >= 4500 && pov <= 13500) fire |= 1 << 23;	// D-Pad Right
	}

	for (int i = 0; i < 16; i++)
		if (js.rgbButtons[i] & 0x80)
			fire |= 1 << i;	// Joy 1..Joy 16, same bit layout the XInput path and the name tables in input.cpp use

	// Right stick tilt as 4 extra digital "buttons" (bits 16-19), same as the XInput
	// path - not used for camera control, just an alternate 4-way bindable input.
	// Reads Z/Rz, not Rx/Ry - a full-axis log dump on the reporting controller
	// showed Rx/Ry permanently pinned at the range minimum (dead/unmapped), while
	// Z/Rz swung across the whole range and idled near its midpoint rather than
	// near 0, matching a stick rather than a trigger axis (X/Y is the left stick).
	long rrx = js.lZ;
	long rry = js.lRz;

	// Also read as axes for the modern camera, divided by 8 like this
	// path's left stick and with no invert, because Rz is already
	// up-negative here - which is what the four bits below say too.
	//
	// Taken before the zone: the camera's own zone is radial and lives at
	// the seam (ModernStickDeadZone, specific\input.cpp). The per-axis 500
	// below is the four bindable bits' own threshold and stays as it is; it
	// was originally set wide to cover a belief that this controller's
	// Z/Rz rest off-centre. Measured (MODERNLOG): the stick rests at rx
	// 2..5, ry -6..2 of 128, close enough to zero that the modern zone on
	// this path uses MODERN_DEAD_RIGHT_DINPUT 24 with a per-axis square of
	// 20, not 62; game\modern_move.h carries the measurement beside both
	// numbers. The two assignments below come before the 500, and the left
	// stick's own pair above comes before the 150, so the modern seam
	// always reads the raw axis; neither per-axis gate is ever in its way.
	joystick_raw_rx = rrx / 8;
	joystick_raw_ry = rry / 8;

	const long rdeadzone = 500;	// wide enough to absorb this axis's off-center rest position after DIPROP_RANGE remapping

	if (rrx > -rdeadzone && rrx < rdeadzone) rrx = 0;
	if (rry > -rdeadzone && rry < rdeadzone) rry = 0;

	if (rry < 0) fire |= 1 << 16;	// Stick R Up
	if (rry > 0) fire |= 1 << 17;	// Stick R Down
	if (rrx < 0) fire |= 1 << 18;	// Stick R Left
	if (rrx > 0) fire |= 1 << 19;	// Stick R Right

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
	joystick_raw_rx = 0;	// the right stick's axes, raw - see below
	joystick_raw_ry = 0;
	joystick_raw_lx = 0;	// the left stick's own axes, without the d-pad and raw - see below
	joystick_raw_ly = 0;

	if (XInputGetState(0, &state) != ERROR_SUCCESS)
		return DXUpdateJoystickDirectInput();	// no XInput device (e.g. a PS4/PS5 controller with no Steam Input/DS4Windows wrapper active) - fall back to raw DirectInput

	joystick_read = 1;
	joystick_is_xinput = 1;	// XInput bit order active (name tables see input.cpp)

	long lx = state.Gamepad.sThumbLX;
	long ly = state.Gamepad.sThumbLY;

	// The stick's raw pair, kept beside the axes the d-pad lines below
	// overwrite. Those four lines do not OR the d-pad into these axes, they
	// assign: a direction press replaces whatever the stick was saying with
	// +-20. Under Tank that is right and is Core's own idea - the d-pad is
	// a second way to push the same stick, and a direction bit has no
	// size. Under Modern the size is the speed and the direction is a
	// course, and an action bound to the d-pad would overwrite the stick
	// with a quarter-push while tapped during a run, stopping her outright
	// when she ran straight and veering her course otherwise. No erasure
	// downstream can undo that, because the stick's own value is already
	// gone by then - so it is kept here instead, and the movement seam
	// reads this pair (specific\input.cpp). Written on every poll and read
	// only under the option, exactly like the right stick's own axes
	// below; nothing in this file needs to know which scheme is running.
	//
	// Taken before the per-axis zone: that zeroing snaps a nearly-axial
	// push onto the axis, because it judges each component on its own.
	// Modern puts this raw pair through a radial zone instead
	// (ModernStickDeadZone), at the same 7849 of 32767 XInput asks for, so
	// the zone is as wide as it ever was and the direction survives it.
	// joystick_read_x/y below keep the per-axis zone unchanged - they are
	// what Tank, the menus and the binding screen read.
	joystick_raw_lx = lx / 256;
	joystick_raw_ly = -ly / 256;	// inverted for the same reason as the read pair below

	if (lx > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && lx < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		lx = 0;

	if (ly > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && ly < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		ly = 0;

	joystick_read_x = lx / 256;	// -32768..32767 -> ~-128..127
	joystick_read_y = -ly / 256;	// inverted: stick pushed up (ly>0) means forward, which S_UpdateInput() reads as joy_y<0

	long fire = 0;	// declared before the d-pad block below, whose button bits need it to already exist

	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  joystick_read_x = -20;
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) joystick_read_x = 20;
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)    joystick_read_y = -20;
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  joystick_read_y = 20;

	// The d-pad as 4 more digital buttons too (bits 20-23, past the right
	// stick's own 16-19 below), same idea as that block and written the
	// same unconditional way - so it can be bound to anything once Modern
	// frees the d-pad from moving her. Tank's own default tables never
	// point at these bits, so nothing here changes what Tank does by
	// default; a player is free to bind onto them in either scheme, same
	// as the right stick's own bits already are.
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)    fire |= 1 << 20;	// D-Pad Up
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  fire |= 1 << 21;	// D-Pad Down
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  fire |= 1 << 22;	// D-Pad Left
	if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) fire |= 1 << 23;	// D-Pad Right

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

	// Right stick tilt as 4 extra digital "buttons" (bits 16-19, past the 16 real
	// physical buttons above) so it can be bound to actions the same way the D-pad
	// already is - not used for camera control, just an alternate 4-way input.
	long rx = state.Gamepad.sThumbRX;
	long ry = state.Gamepad.sThumbRY;

	// The same two axes, also read as axes: the modern camera is a rate
	// and not four directions, so it needs the deflection and not the bits
	// below. Scaled exactly like the left stick a few lines up - divided by
	// 256 into roughly -128..127, y inverted so that forward is negative,
	// which is the convention every reader in this tree already uses
	// (S_UpdateInput's joy_y, game\modern_move.h's stick_y).
	//
	// Written unconditionally and read by nobody at Tank: only the modern
	// camera's seam in specific\input.cpp touches them, and only under
	// tomb5.control_modern. Taken before the per-axis zone - the seam
	// applies XInput's own 8689 of 32767 radially instead
	// (MODERN_DEAD_RIGHT), which is the same width without the axis snap.
	// The zeroing below is left exactly as it was: it belongs to the four
	// bindable bits.
	joystick_raw_rx = rx / 256;
	joystick_raw_ry = -ry / 256;

	if (rx > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && rx < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
		rx = 0;

	if (ry > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && ry < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
		ry = 0;

	if (ry > 0) fire |= 1 << 16;	// Stick R Up
	if (ry < 0) fire |= 1 << 17;	// Stick R Down
	if (rx < 0) fire |= 1 << 18;	// Stick R Left
	if (rx > 0) fire |= 1 << 19;	// Stick R Right

	joystick_read_fire = fire;

	return 1;
}

//----------------------------------------------------------------------------
// The DirectInput initialization runs in the background, see DXInitJoystick.
// Until it is done, G_dxptr->Joystick deliberately stays 0 - the per-frame
// reader DXUpdateJoystickDirectInput checks for that and simply skips the
// controller. That is why the device is NOT written straight to
// G_dxptr->Joystick here: at this point it is still unconfigured (no data
// format, not acquired), and the main thread must not touch it in that
// state. It is published only at the very end.
static LPDIRECTINPUTDEVICE8 g_pendingJoystick = 0;
static HANDLE               g_joystickThread = 0;

static BOOL CALLBACK DIEnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext)
{
	LPDIRECTINPUT8 dinput = (LPDIRECTINPUT8)pContext;
	LPDIRECTINPUTDEVICE8 joystick = 0;

	Log("Joystick: found \"%s\"", pdidInstance->tszProductName);

	if (SUCCEEDED(dinput->CreateDevice(pdidInstance->guidInstance, &joystick, 0)))
	{
		g_pendingJoystick = joystick;
		return DIENUM_STOP;
	}

	return DIENUM_CONTINUE;
}

//----------------------------------------------------------------------------
// Runs in its own thread because EnumDevices takes very long with certain
// device constellations and the game otherwise sits on a black screen.
//
// Measured: exactly 10 seconds, reproducible three times, and identical in
// a standalone 40-line program with no relation to this engine - a timeout
// inside DirectInput itself, not a bug in this code. Cause: the controller
// hangs on the USB cable but is also still paired via Bluetooth, and that
// paired entry does not answer; DirectInput waits it out.
//
// A player's Bluetooth pairing cannot be controlled from here, and
// skipping the enumeration is not an option: a DualSense is no XInput
// device, and without DirectInput it would have no controller at all. So
// this must not sit in the startup path. The game starts immediately, and
// the controller comes on as soon as the enumeration finishes - on an
// unremarkable system that is milliseconds.
//----------------------------------------------------------------------------
static void DXInitJoystick(HWND hwnd)
{
	g_pendingJoystick = 0;

	G_dxptr->lpDirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, DIEnumJoysticksCallback, G_dxptr->lpDirectInput, DIEDFL_ATTACHEDONLY);

	if (!g_pendingJoystick)
	{
		Log("No DirectInput joystick found (XInput-only controllers still work)");
		return;
	}

	DXAttempt(g_pendingJoystick->SetDataFormat(&c_dfDIJoystick2));
	DXAttempt(g_pendingJoystick->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));

	DIPROPRANGE range;
	range.diph.dwSize = sizeof(DIPROPRANGE);
	range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1000;
	range.lMax = 1000;

	range.diph.dwObj = DIJOFS_X;
	g_pendingJoystick->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_Y;
	g_pendingJoystick->SetProperty(DIPROP_RANGE, &range.diph);

	// Right stick, read as 4 extra digital "buttons" in DXUpdateJoystickDirectInput
	// (see joy_fire bits 16-19). Rx/Ry turned out to be dead on this controller
	// (logged permanently pinned at the range minimum, never changing) - Z/Rz
	// confirmed live instead (X/Y is the left stick), identified from a full-axis
	// log dump: they swing across the whole native range and idle near its
	// midpoint rather than near 0, which rules out them being a trigger axis.
	range.diph.dwObj = DIJOFS_Z;
	g_pendingJoystick->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RZ;
	g_pendingJoystick->SetProperty(DIPROP_RANGE, &range.diph);

	g_pendingJoystick->Acquire();

	// Publish only now: from this moment the main thread may use the
	// device, before it would be unconfigured. InterlockedExchangePointer
	// makes sure all earlier writes really become visible before this one.
	InterlockedExchangePointer((void* volatile*)&G_dxptr->Joystick, g_pendingJoystick);

	Log("DirectInput joystick acquired (fallback for non-XInput controllers)");
}

static unsigned __stdcall JoystickInitThread(void* param)
{
	DXInitJoystick((HWND)param);
	return 0;
}

// Called on shutdown BEFORE lpDirectInput is released - otherwise the main
// thread pulls the interface out from under the still-running enumeration
// thread. At a normal game exit the thread is long done and the wait is
// zero.
void DXWaitJoystickInit()
{
	if (!g_joystickThread)
		return;

	if (WaitForSingleObject(g_joystickThread, 15000) == WAIT_TIMEOUT)
		Log("WARNING: joystick init thread still running at shutdown - not waiting further");

	CloseHandle(g_joystickThread);
	g_joystickThread = 0;
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

	// Do not call directly: see the comment at DXInitJoystick. Depending on
	// the device situation the call can take ten seconds, which would show
	// as a black screen before the first video.
	G_dxptr->Joystick = 0;
	g_joystickThread = (HANDLE)_beginthreadex(0, 0, JoystickInitThread, (void*)hwnd, 0, 0);

	if (!g_joystickThread)
	{
		// Getting no thread is so unlikely that falling back to the
		// blocking path is better than no controller at all.
		Log("WARNING: could not start joystick init thread - falling back to blocking init");
		DXInitJoystick(hwnd);
	}
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

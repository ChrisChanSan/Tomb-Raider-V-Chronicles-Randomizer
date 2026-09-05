/*
 * RoomProbe - draw one HD room with the engine's own renderer and write a PNG.
 *
 * Modified 2026 by ChrisChanSan.
 *
 * THE POINT OF THIS TOOL. specific\hd\hd_render.cpp is a second draw path
 * beside the engine's fixed-function one, and until stage S1-2 nothing in the
 * game calls it. A renderer nobody has seen a picture out of is not a
 * renderer, it is a guess - so this tool gives it a hidden window, a real
 * Direct3D 9 device, a render target and a camera, and puts what comes back
 * into a .PNG. Same source files as the engine links, no copy: what the
 * picture shows is what the game will draw.
 *
 * It also prints the two measurements stage S1-1 owes the plan:
 *
 *   - the depth proof. The classic path writes its own depth as
 *     sz = f_a - f_boo * rhw; the HD path lets the device compute it from a
 *     projection matrix. If the two disagree the shared depth buffer is a
 *     fiction. HDRenderSelfTest compares them over 50 view distances, and
 *     HDRenderDepthTruth reads back what the device actually compares with -
 *     ZENABLE, ZFUNC, ZWRITEENABLE and the viewport's MinZ / MaxZ - because
 *     a proof against an assumed state proves nothing.
 *   - the world-matrix proof (stage S1-2, -matrixcheck). InsertRoom hands the
 *     HD path the matrix that is on the engine's stack for a room, and the HD
 *     path multiplies it by a projection matrix and lets the device divide by
 *     w. The classic path multiplies by the same matrix and divides by z
 *     itself. If those two do not put the same room-local point in the same
 *     pixel, every HD wall stands beside its own floor - so both are computed
 *     here, over up to 128 of the room's own vertices, and the worst
 *     difference is printed in PIXELS.
 *   - the page cache in both pools. D3DPOOL_DEFAULT pushes every page through
 *     one reused system-memory staging texture; D3DPOOL_MANAGED lets Direct3D
 *     keep a system copy of all of them. The engine is a 32 bit process, so
 *     the number that decides between them is the peak COMMIT of the process,
 *     and that is what this tool reports (which is also why it is built x86
 *     and not x64 like its neighbours - on x64 the question has no meaning).
 *
 * USAGE
 *
 *   RoomProbe <5-folder> <LEVEL> <room> [x y z yaw pitch] [options]
 *
 *     <5-folder>  the remaster's game 5 folder - the one with DATA and TEX
 *     <LEVEL>     level stem without extension, e.g. ANDREA1
 *     <room>      .TRG group number = room number
 *     x y z       camera, room-local classic units (y counts DOWNWARDS)
 *     yaw pitch   degrees; yaw 0 looks along +Z, 90 along +X,
 *                 pitch > 0 looks down
 *
 *   Without x..pitch the camera is placed in the middle of the room looking
 *   at its longest wall, which is what a first look wants.
 *
 *   Options
 *     -size <w> <h>   render target, default 1280 720
 *     -fov <deg>      default 80, the engine's own
 *     -all            three more PNGs, one per pass (opaque, alpha, additive)
 *     -managed        pages in D3DPOOL_MANAGED instead of D3DPOOL_DEFAULT
 *     -threads <n>    decoding threads, default 8
 *     -rooms <n>      the classic room count, so groups beyond it count as sky
 *     -out <prefix>   output path prefix, default .\roomprobe
 *     -groups         list every group's triangles and ranges, then stop
 *     -nodraw         load and measure only - no window, no picture
 *     -matrixcheck    the world-matrix proof as a table, then stop
 *     -sky            draw the .TRG's sky group(s) behind the room, centred
 *                     on the camera, in their own slice of the depth buffer -
 *                     what the engine does in place of the classic horizon
 *                     mesh (#123, fix round R3a)
 *     -skyonly        the same, with the room left out: the dome alone
 *
 *   Stage S2b - the parity block. Every one of these makes up a piece of
 *   engine state the tool has no engine for, and hands it to the very
 *   structures hd_level.cpp fills in the game.
 *
 *     -light <x y z r g b falloff>
 *                     one dynamic light, room-local position (the printed
 *                     room box says the range), colour 0..1, falloff in
 *                     classic units - a torch. Up to 8, repeat the switch.
 *     -bulb <x y z radius density [r g b]>
 *                     one fog bulb, room-local; density is the engine's own
 *                     bulb->d (1 / den, so 0.02 is a thick one). Colour
 *                     defaults to white. Up to HD_MAX_BULBS (8 since the cap
 *                     was raised from 6), repeat the switch.
 *     -fog <start end r g b>
 *                     distance fog. Only `start` reaches the picture, in
 *                     WORLD units - TR5's fog has no end and no colour: the
 *                     classic path subtracts a linear ramp of 255 over the
 *                     same distance again from the vertex colour and never
 *                     blends towards anything (drawroom.cpp:519). The other
 *                     four are read and reported so the switch matches the
 *                     brief rather than silently meaning something else.
 *     -underwater     the room carries ROOM_UNDERWATER and the camera is in
 *                     it: the WaterTable darkening on the colour and the
 *                     screen wobble on the position.
 *     -shimmer        the whole room shimmers - the selector aimed at the
 *                     constant fourth flag byte, because a tool with no
 *                     classic level behind it has no per-vertex marks.
 *     -wibble <n>     the engine's wibble counter, which drives both. Two
 *                     runs with two values are two moments in the water.
 *     -paritycheck    run the classic formula and the shader formula over up
 *                     to 200 of the room's own vertices on the processor and
 *                     print the worst difference per channel, then stop.
 *
 *   Stage S6 - the texture budget and the small card. These four are the only
 *   way to see the fallbacks work: no machine here has a card that needs them,
 *   and a path nobody has run is a path that does not work.
 *
 *     -budget <MiB>   force a texture budget. Over it the largest pages give
 *                     up their largest mip level, biggest first, until what
 *                     is left fits - never a whole page. 0 means no cap,
 *                     which is what the tool does by default.
 *     -format dxt5    hold the pages as DXT5 instead of A8R8G8B8: a quarter
 *                     of the video memory, a measurable loss of quality.
 *                     `-format rgba` is the default and says so out loud.
 *     -psnr           decode one page both ways on the processor and print
 *                     the peak signal to noise ratio of DXT5 against the
 *                     lossless original, per channel and over all four.
 *     -caps maxtex <n>  pretend the device holds no texture wider than n
 *     -caps noidx32     pretend the device has no 32 bit index buffer, so
 *                     every draw call goes through a 16 bit index and a moved
 *                     vertex stream
 *     -seq <A,B,C>    build those levels one after the other, printing commit
 *                     and free texture memory between each - the proof that a
 *                     level change never holds two levels at once.
 *     -vramhog        fill the card's texture memory with 64 MiB blocks until
 *                     it will take no more, THEN load the level. That is the
 *                     one way to see what happens when CreateTexture fails in
 *                     the middle of a level: the pages stop, the reason is
 *                     said, and the whole level goes back to classic instead
 *                     of half of it drawing without textures.
 *
 * Everything outside the pixels goes to stdout, so a run can be pasted into a
 * report as it stands.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "zlib.h"

/* By path, not through a /I of specific\: that folder holds the engine's own
 * time.h, and putting it on the include path makes <chrono> pick it up
 * instead of the CRT's and fail with two hundred lines about asctime. A
 * quoted include is resolved from THIS file's folder first, so this works
 * from both build scripts. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_render.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_pagecache.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_light.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_dxt5.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_scan.h"
#include "../../../TOMB5 Code/TOMB5/specific/bc7.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")

/* ------------------------------------------------------------------ PNG -- */
/* The same writer DdsProbe uses: 24 bit truecolour, filter 0 on every
 * scanline, deflate through the zlib the engine already links. Small and
 * obviously correct beats small. */

static void PutBE(unsigned char* p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void PngChunk(FILE* f, const char* tag, const unsigned char* data, unsigned long len)
{
	unsigned char head[8];
	unsigned char tail[4];
	unsigned long crc;

	PutBE(head, len);
	memcpy(head + 4, tag, 4);
	fwrite(head, 1, 8, f);

	if (len)
		fwrite(data, 1, len, f);

	crc = crc32(0, (const Bytef*)tag, 4);

	if (len)
		crc = crc32(crc, (const Bytef*)data, len);

	PutBE(tail, crc);
	fwrite(tail, 1, 4, f);
}

static bool WritePng(const char* path, const unsigned long* px, long w, long h)
{
	FILE* f;
	unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char ihdr[13];
	unsigned char* raw;
	unsigned char* zbuf;
	unsigned long rawLen, zLen;
	long x, y;
	unsigned char* q;

	rawLen = (unsigned long)h * (1 + (unsigned long)w * 3);
	raw = (unsigned char*)malloc(rawLen);

	if (!raw)
		return false;

	q = raw;

	for (y = 0; y < h; y++)
	{
		*q++ = 0;

		for (x = 0; x < w; x++)
		{
			const unsigned long c = px[y * w + x];
			*q++ = (unsigned char)(c >> 16);
			*q++ = (unsigned char)(c >> 8);
			*q++ = (unsigned char)c;
		}
	}

	zLen = compressBound(rawLen);
	zbuf = (unsigned char*)malloc(zLen);

	if (!zbuf || compress2(zbuf, &zLen, raw, rawLen, 6) != Z_OK)
	{
		free(raw);
		free(zbuf);
		return false;
	}

	free(raw);
	f = fopen(path, "wb");

	if (!f)
	{
		free(zbuf);
		return false;
	}

	fwrite(sig, 1, 8, f);
	PutBE(ihdr, (unsigned long)w);
	PutBE(ihdr + 4, (unsigned long)h);
	ihdr[8] = 8;		// bit depth
	ihdr[9] = 2;		// truecolour
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;
	PngChunk(f, "IHDR", ihdr, 13);
	PngChunk(f, "IDAT", zbuf, zLen);
	PngChunk(f, "IEND", 0, 0);
	fclose(f);
	free(zbuf);
	return true;
}

/* --------------------------------------------------------------- camera -- */

/* The engine's own view convention (3dmath.cpp, drawroom.cpp): x right,
 * y DOWN, z forward, and a point goes through the matrix as a ROW vector.
 * yaw 0 looks along +Z and grows towards +X - the same sense as a TR5 heading
 * angle - and a positive pitch looks downwards, because down is +y here. */
static void LookAt(double ex, double ey, double ez, double yawDeg, double pitchDeg,
	float* out)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const double yaw = yawDeg * d2r;
	const double pitch = pitchDeg * d2r;

	const double sy = sin(yaw), cy = cos(yaw);
	const double sp = sin(pitch), cp = cos(pitch);

	// forward, right, and down = forward x right (a proper basis: with
	// pitch 0 it comes out as world +y, which is world down).
	const double f[3] = { sy * cp, sp, cy * cp };
	const double r[3] = { cy, 0.0, -sy };
	const double d[3] =
	{
		f[1] * r[2] - f[2] * r[1],
		f[2] * r[0] - f[0] * r[2],
		f[0] * r[1] - f[1] * r[0]
	};

	out[0] = (float)r[0];	out[1] = (float)d[0];	out[2] = (float)f[0];	out[3] = 0;
	out[4] = (float)r[1];	out[5] = (float)d[1];	out[6] = (float)f[1];	out[7] = 0;
	out[8] = (float)r[2];	out[9] = (float)d[2];	out[10] = (float)f[2];	out[11] = 0;
	out[12] = (float)-(ex * r[0] + ey * r[1] + ez * r[2]);
	out[13] = (float)-(ex * d[0] + ey * d[1] + ez * d[2]);
	out[14] = (float)-(ex * f[0] + ey * f[1] + ez * f[2]);
	out[15] = 1;
}

/* AlterFOV (3dmath.cpp:78-98), without the engine: the field of view is
 * measured against a 4:3 width whatever the window really is, which is why a
 * 16:9 picture is horizontally wider and not vertically shorter. Reproduced
 * rather than approximated, so the probe frames a room the way the game
 * will. */
static double EnginePersp(long height, double fovDeg)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const long fovWidth = height * 320 / 240;
	const double half = fovDeg * 0.5 * d2r;

	return (double)(fovWidth / 2) * cos(half) / sin(half);
}

/* ------------------------------------------------------------------ mem -- */

static double PeakCommitMiB()
{
	PROCESS_MEMORY_COUNTERS_EX counters;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);

	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return 0.0;

	return (double)counters.PeakPagefileUsage / 1048576.0;
}

static double CommitMiB()
{
	PROCESS_MEMORY_COUNTERS_EX counters;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);

	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return 0.0;

	return (double)counters.PagefileUsage / 1048576.0;
}

/* ------------------------------------------------------------------ d3d -- */

static HWND g_window = 0;
static IDirect3D9* g_d3d = 0;
static IDirect3DDevice9* g_device = 0;

static bool MakeDevice()
{
	WNDCLASSA cls;
	D3DPRESENT_PARAMETERS pp;

	memset(&cls, 0, sizeof(cls));
	cls.lpfnWndProc = DefWindowProcA;
	cls.hInstance = GetModuleHandleA(0);
	cls.lpszClassName = "RoomProbeWindow";
	RegisterClassA(&cls);

	// Never shown: WS_POPUP without WS_VISIBLE, one pixel off screen. The
	// device still needs a real HWND, a hidden one is enough.
	g_window = CreateWindowA("RoomProbeWindow", "RoomProbe", WS_POPUP,
		-32, -32, 16, 16, 0, 0, cls.hInstance, 0);

	if (!g_window)
	{
		printf("RoomProbe: CreateWindow failed\n");
		return false;
	}

	g_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!g_d3d)
	{
		printf("RoomProbe: Direct3DCreate9 failed\n");
		return false;
	}

	memset(&pp, 0, sizeof(pp));
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferWidth = 16;
	pp.BackBufferHeight = 16;
	pp.hDeviceWindow = g_window;
	pp.EnableAutoDepthStencil = FALSE;

	if (FAILED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &g_device)))
	{
		printf("RoomProbe: CreateDevice(HAL, hardware vertex processing) failed\n");
		return false;
	}

	return true;
}

static void DropDevice()
{
	if (g_device) { g_device->Release(); g_device = 0; }
	if (g_d3d) { g_d3d->Release(); g_d3d = 0; }
	if (g_window) { DestroyWindow(g_window); g_window = 0; }
}

/* --------------------------------------------------------------- proofs -- */

static void PrintZProof(double persp)
{
	HDRenderZSample rows[50];
	double a = 0, b = 0, boo = 0;

	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);

	const double worst = HDRenderSelfTest(20.0, 20480.0, persp, rows, 50);

	printf("\n-- the depth proof ------------------------------------------\n");
	printf("SetupZRange(20, 20480): f_a %.9f  f_b %.9f  f_boo %.9f\n", a, b, boo);
	printf("projection: _33 %.9f  _43 %.9f  _34 1  _44 0\n", a, -b);
	printf("\n  %12s %14s %14s %12s\n", "z_view", "classic sz", "matrix depth", "difference");

	// Five of the fifty, spread over the range: near plane, the two ends of
	// the middle, and the far plane.
	const int show[5] = { 0, 12, 24, 37, 49 };

	for (int i = 0; i < 5; i++)
	{
		const HDRenderZSample& row = rows[show[i]];
		printf("  %12.3f %14.9f %14.9f %12.2e\n",
			row.zView, row.classic, row.matrix, row.diff);
	}

	printf("\nworst of 50 samples: %.3e  (%s)\n", worst,
		worst < 1e-5 ? "PASS - the two paths write the same depth"
			: "FAIL - the shared depth buffer does not hold");
}

static void PrintDepthTruth(const char* when)
{
	unsigned long zenable = 0, zfunc = 0, zwrite = 0;
	float minZ = 0, maxZ = 0;

	HDRenderDepthTruth(g_device, &zenable, &zfunc, &zwrite, &minZ, &maxZ);

	const char* zname = zenable == 0 ? "D3DZB_FALSE"
		: zenable == 1 ? "D3DZB_TRUE"
		: zenable == 2 ? "D3DZB_USEW" : "?";

	static const char* funcs[9] =
	{
		"?", "NEVER", "LESS", "EQUAL", "LESSEQUAL",
		"GREATER", "NOTEQUAL", "GREATEREQUAL", "ALWAYS"
	};

	printf("device depth truth %-14s ZENABLE %lu (%s)  ZFUNC %lu (%s)  "
		"ZWRITE %lu  viewport z %.3f..%.3f\n",
		when, zenable, zname, zfunc,
		zfunc < 9 ? funcs[zfunc] : "?", zwrite, minZ, maxZ);
}

/* ------------------------------------------------------- state bracket -- */
/*
 * HDRenderFrameBegin / HDRenderFrameEnd promise that the device comes out of
 * an HD block in exactly the state it went in - the classic fixed-function
 * path draws straight after it and would show the difference as garbage. A
 * promise is not a proof, so this reads the device twice.
 *
 * The list is deliberately WIDER than the one hd_render saves: every render
 * state the classic path is known to set (function_table.cpp:95-128,
 * polyinsert.cpp per bucket, LoadSave.cpp for the filter switch), plus a few
 * it never touches, so that a state hd_render changes and forgets to save is
 * caught here rather than in the game.
 */
static const D3DRENDERSTATETYPE kWatchedStates[] =
{
	D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_FILLMODE,
	D3DRS_SHADEMODE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC,
	D3DRS_DITHERENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
	D3DRS_BLENDOP, D3DRS_CULLMODE, D3DRS_FOGENABLE, D3DRS_SPECULARENABLE,
	D3DRS_LIGHTING, D3DRS_COLORVERTEX, D3DRS_AMBIENT, D3DRS_CLIPPING,
	D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_COLORWRITEENABLE,
	D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRGBWRITEENABLE,
	D3DRS_MULTISAMPLEANTIALIAS, D3DRS_TEXTUREFACTOR, D3DRS_WRAP0,
	D3DRS_NORMALIZENORMALS, D3DRS_LOCALVIEWER, D3DRS_DEPTHBIAS,
	D3DRS_SLOPESCALEDEPTHBIAS
};

static const D3DSAMPLERSTATETYPE kWatchedSamplers[] =
{
	D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_BORDERCOLOR,
	D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER,
	D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MAXANISOTROPY,
	D3DSAMP_SRGBTEXTURE
};

static const D3DTEXTURESTAGESTATETYPE kWatchedStages[] =
{
	D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
	D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX
};

static const int kStateCount = (int)(sizeof(kWatchedStates) / sizeof(kWatchedStates[0]));
static const int kSamplerCount = (int)(sizeof(kWatchedSamplers) / sizeof(kWatchedSamplers[0]));
static const int kStageCount = (int)(sizeof(kWatchedStages) / sizeof(kWatchedStages[0]));

struct Snapshot
{
	DWORD rs[kStateCount];
	DWORD samp[kSamplerCount];
	DWORD stage[kStageCount];
	DWORD fvf;
	void* vs;
	void* ps;
	void* decl;
	void* stream;
	void* indices;
	void* texture;
	UINT offset, stride;
};

static void Take(Snapshot& shot)
{
	memset(&shot, 0, sizeof(shot));

	for (int i = 0; i < kStateCount; i++)
		g_device->GetRenderState(kWatchedStates[i], &shot.rs[i]);
	for (int i = 0; i < kSamplerCount; i++)
		g_device->GetSamplerState(0, kWatchedSamplers[i], &shot.samp[i]);
	for (int i = 0; i < kStageCount; i++)
		g_device->GetTextureStageState(0, kWatchedStages[i], &shot.stage[i]);

	g_device->GetFVF(&shot.fvf);

	// The Get* calls add a reference; take the pointer as an identity and
	// give the reference straight back, so the comparison cannot itself
	// change what it measures.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* indices = 0;
	IDirect3DBaseTexture9* texture = 0;

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetStreamSource(0, &stream, &shot.offset, &shot.stride);
	g_device->GetIndices(&indices);
	g_device->GetTexture(0, &texture);

	shot.vs = vs; shot.ps = ps; shot.decl = decl;
	shot.stream = stream; shot.indices = indices; shot.texture = texture;

	if (vs) vs->Release();
	if (ps) ps->Release();
	if (decl) decl->Release();
	if (stream) stream->Release();
	if (indices) indices->Release();
	if (texture) texture->Release();
}

static int Diff(const Snapshot& before, const Snapshot& after)
{
	int broken = 0;

	for (int i = 0; i < kStateCount; i++)
		if (before.rs[i] != after.rs[i])
		{
			printf("  BROKEN render state %d: %lu -> %lu\n",
				(int)kWatchedStates[i], before.rs[i], after.rs[i]);
			broken++;
		}

	for (int i = 0; i < kSamplerCount; i++)
		if (before.samp[i] != after.samp[i])
		{
			printf("  BROKEN sampler 0 state %d: %lu -> %lu\n",
				(int)kWatchedSamplers[i], before.samp[i], after.samp[i]);
			broken++;
		}

	for (int i = 0; i < kStageCount; i++)
		if (before.stage[i] != after.stage[i])
		{
			printf("  BROKEN texture stage 0 state %d: %lu -> %lu\n",
				(int)kWatchedStages[i], before.stage[i], after.stage[i]);
			broken++;
		}

	struct { const char* name; const void* a; const void* b; } objects[] =
	{
		{ "vertex shader", before.vs, after.vs },
		{ "pixel shader", before.ps, after.ps },
		{ "vertex declaration", before.decl, after.decl },
		{ "stream 0", before.stream, after.stream },
		{ "index buffer", before.indices, after.indices },
		{ "texture 0", before.texture, after.texture }
	};

	for (int i = 0; i < 6; i++)
		if (objects[i].a != objects[i].b)
		{
			printf("  BROKEN %s: %p -> %p\n",
				objects[i].name, objects[i].a, objects[i].b);
			broken++;
		}

	if (before.fvf != after.fvf)
	{
		printf("  BROKEN FVF: 0x%08lX -> 0x%08lX\n", before.fvf, after.fvf);
		broken++;
	}

	/* Offset and stride only mean anything while a buffer is bound. With
	 * stream 0 unbound Direct3D 9 keeps the last stride it was given -
	 * SetStreamSource(0, NULL, 0, 0) is accepted and GetStreamSource still
	 * answers 20 - so an unbound-before / unbound-after pair is the same
	 * state however the two strides read. Measured here, not assumed: this
	 * check reported it before it was written down. */
	if (before.stream && after.stream &&
		(before.offset != after.offset || before.stride != after.stride))
	{
		printf("  BROKEN stream 0 offset/stride: %u/%u -> %u/%u\n",
			before.offset, before.stride, after.offset, after.stride);
		broken++;
	}
	else if (!before.stream && before.stride != after.stride)
		printf("  (stream 0 unbound, stride reads %u instead of %u - "
			"Direct3D keeps the last one and nothing can read it)\n",
			after.stride, before.stride);

	return broken;
}

/* The device as the classic path leaves it (function_table.cpp:95-128), so
 * that the bracket is tested against the real caller's state and not against
 * Direct3D's fresh defaults - the two are not the same in a single one of
 * these lines. */
static void PoseAsClassicPath()
{
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);

	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	g_device->SetRenderState(D3DRS_SPECULARENABLE, 1);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, 1);
	g_device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	g_device->SetRenderState(D3DRS_DITHERENABLE, 1);
	g_device->SetRenderState(D3DRS_ALPHAREF, 0);
	g_device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_NOTEQUAL);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	g_device->SetRenderState(D3DRS_AMBIENT, 0);
	g_device->SetRenderState(D3DRS_COLORVERTEX, 0);
	g_device->SetRenderState(D3DRS_FOGENABLE, 0);
	g_device->SetRenderState(D3DRS_CLIPPING, FALSE);
	g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2);
}

/* ---------------------------------------------------------------- draw -- */

struct Shot
{
	IDirect3DTexture9* target = 0;
	IDirect3DSurface9* colour = 0;
	IDirect3DSurface9* depth = 0;
	IDirect3DSurface9* readback = 0;
	IDirect3DSurface9* oldColour = 0;
	long w = 0, h = 0;
};

static bool ShotBegin(Shot& shot, long w, long h)
{
	shot.w = w;
	shot.h = h;

	if (FAILED(g_device->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &shot.target, 0)))
	{
		printf("RoomProbe: CreateTexture(render target %ldx%ld) failed\n", w, h);
		return false;
	}

	if (FAILED(shot.target->GetSurfaceLevel(0, &shot.colour)))
		return false;

	if (FAILED(g_device->CreateDepthStencilSurface((UINT)w, (UINT)h, D3DFMT_D24S8,
		D3DMULTISAMPLE_NONE, 0, TRUE, &shot.depth, 0)))
	{
		printf("RoomProbe: CreateDepthStencilSurface failed\n");
		return false;
	}

	if (FAILED(g_device->CreateOffscreenPlainSurface((UINT)w, (UINT)h,
		D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &shot.readback, 0)))
	{
		printf("RoomProbe: CreateOffscreenPlainSurface failed\n");
		return false;
	}

	g_device->GetRenderTarget(0, &shot.oldColour);
	g_device->SetRenderTarget(0, shot.colour);
	g_device->SetDepthStencilSurface(shot.depth);

	/* MAGENTA, not black. A room that draws nothing and a room that draws
	 * black are the same picture on a black background, and the first is a
	 * bug while the second is a dark corner. Anything still magenta in the
	 * result is the background showing through. */
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);

	return true;
}

static bool ShotWrite(Shot& shot, const char* path)
{
	D3DLOCKED_RECT rect;

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)))
	{
		printf("RoomProbe: GetRenderTargetData failed\n");
		return false;
	}

	if (FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		printf("RoomProbe: locking the readback surface failed\n");
		return false;
	}

	unsigned long* pixels = (unsigned long*)malloc((size_t)shot.w * (size_t)shot.h * 4);

	if (!pixels)
	{
		shot.readback->UnlockRect();
		return false;
	}

	// How much of the picture is still the background - the one number that
	// separates "nothing was drawn" from "something dark was drawn".
	long background = 0;
	double sum = 0;

	for (long y = 0; y < shot.h; y++)
	{
		const unsigned long* src =
			(const unsigned long*)((const unsigned char*)rect.pBits + (size_t)y * rect.Pitch);

		for (long x = 0; x < shot.w; x++)
		{
			const unsigned long c = src[x] | 0xFF000000u;
			pixels[y * shot.w + x] = c;

			if ((c & 0x00FFFFFFu) == 0x00FF00FFu)
				background++;

			sum += (double)((c >> 16) & 255) + (double)((c >> 8) & 255) + (double)(c & 255);
		}
	}

	shot.readback->UnlockRect();

	const double texels = (double)shot.w * (double)shot.h;
	const bool ok = WritePng(path, pixels, shot.w, shot.h);
	free(pixels);

	printf("  %-58s %5.1f%% background, mean brightness %.1f/255\n",
		path, 100.0 * (double)background / texels, sum / (texels * 3.0));

	if (!ok)
		printf("RoomProbe: writing %s failed\n", path);

	return ok;
}

static void ShotEnd(Shot& shot)
{
	if (shot.oldColour)
	{
		g_device->SetRenderTarget(0, shot.oldColour);
		shot.oldColour->Release();
		shot.oldColour = 0;
	}

	g_device->SetDepthStencilSurface(0);

	if (shot.readback) { shot.readback->Release(); shot.readback = 0; }
	if (shot.depth) { shot.depth->Release(); shot.depth = 0; }
	if (shot.colour) { shot.colour->Release(); shot.colour = 0; }
	if (shot.target) { shot.target->Release(); shot.target = 0; }
}

/* ------------------------------------------------------- matrix proof -- */

/* THE ENGINE'S TRANSFORM AGAINST THE HD MATRIX, IN PIXELS.
 *
 * The classic path (specific\drawroom.cpp, aRoomletTransformLight) takes a
 * room-local vertex through the matrix on the stack as a ROW vector and then
 * divides by z itself:
 *
 *     v  = pos * M            M = view * world, translation in row 3
 *     sx = v.x * persp / v.z + centerx
 *     sy = v.y * persp / v.z + centery
 *     sz = f_a - f_boo * (2048 / v.z)
 *
 * The HD path hands the device M * projection and lets the hardware divide by
 * w and apply the viewport. Those three lines are reproduced below exactly as
 * they stand in the engine - not approximated - and compared with the matrix
 * that really goes to the device, over the room's own vertices.
 *
 * The same comparison runs inside the game (specific\hd\hd_level.cpp,
 * MatrixCheck) on the first HD frame of a level and writes one HDLOG line.
 * This is that proof without the game. */
static void MatrixProof(int room, const float* view, const D3DMATRIX& projection,
	double persp, double width, double height, double a, double boo)
{
	short xyz[HD_RENDER_SAMPLES * 3];
	const int count = HDRenderGroupSamples(room, xyz, HD_RENDER_SAMPLES);

	printf("\n-- the world matrix -----------------------------------------\n");

	if (count <= 0)
	{
		printf("group %d has no sample vertices - nothing to prove\n", room);
		return;
	}

	/* world * projection, the general 4x4 as hd_render.cpp builds it and as
	 * the vertex shader uses it: mul(float4(pos, 1), gWorldViewProj). The
	 * probe draws the room with world = identity, so the engine's "view *
	 * world" is this view. */
	const float* p = (const float*)&projection;
	double wvp[16];

	for (int row = 0; row < 4; row++)
		for (int col = 0; col < 4; col++)
		{
			double sum = 0;

			for (int k = 0; k < 4; k++)
				sum += (double)view[row * 4 + k] * (double)p[k * 4 + col];

			wvp[row * 4 + col] = sum;
		}

	const double centerX = width * 0.5;
	const double centerY = height * 0.5;
	const double znear = 20.0;		// f_mznear, the engine's own

	double worstX = 0, worstY = 0, worstZ = 0;
	int used = 0, behind = 0, shown = 0;

	printf("%d sample vertices of group %d, camera as above\n", count, room);
	printf("\n  %-24s %-19s %-19s %s\n",
		"room-local x y z", "classic sx, sy", "device sx, sy", "difference (px)");

	for (int i = 0; i < count; i++)
	{
		const float px = (float)xyz[i * 3 + 0];
		const float py = (float)xyz[i * 3 + 1];
		const float pz = (float)xyz[i * 3 + 2];

		// --- the classic path, line for line ---------------------------
		const float vx = px * view[0] + py * view[4] + pz * view[8] + view[12];
		const float vy = px * view[1] + py * view[5] + pz * view[9] + view[13];
		const float vz = px * view[2] + py * view[6] + pz * view[10] + view[14];

		if (vz < (float)znear)
		{
			behind++;
			continue;
		}

		const float zv = (float)persp / vz;
		const double classicX = (double)(vx * zv + (float)centerX);
		const double classicY = (double)(vy * zv + (float)centerY);
		const double classicZ = a - boo * (2048.0 / (double)vz);

		// --- the device -------------------------------------------------
		const double cx = (double)px * wvp[0] + (double)py * wvp[4] + (double)pz * wvp[8] + wvp[12];
		const double cy = (double)px * wvp[1] + (double)py * wvp[5] + (double)pz * wvp[9] + wvp[13];
		const double cz = (double)px * wvp[2] + (double)py * wvp[6] + (double)pz * wvp[10] + wvp[14];
		const double cw = (double)px * wvp[3] + (double)py * wvp[7] + (double)pz * wvp[11] + wvp[15];

		if (cw <= 0.0)
		{
			behind++;
			continue;
		}

		// The viewport this engine never sets: the whole target, MinZ 0,
		// MaxZ 1.
		const double deviceX = (cx / cw + 1.0) * 0.5 * width;
		const double deviceY = (1.0 - cy / cw) * 0.5 * height;
		const double deviceZ = cz / cw;

		const double dx = fabs(deviceX - classicX);
		const double dy = fabs(deviceY - classicY);
		const double dz = fabs(deviceZ - classicZ);

		if (dx > worstX) worstX = dx;
		if (dy > worstY) worstY = dy;
		if (dz > worstZ) worstZ = dz;

		used++;

		// A dozen rows are a table; a hundred are a wall.
		if (shown < 12 && (i % (count / 12 + 1)) == 0)
		{
			printf("  %7.0f %7.0f %7.0f  %8.2f %8.2f  %8.2f %8.2f  %.5f %.5f\n",
				(double)px, (double)py, (double)pz,
				classicX, classicY, deviceX, deviceY, dx, dy);
			shown++;
		}
	}

	printf("\n%d of %d vertices in front of the near plane (%d behind it or "
		"off the near side)\n", used, count, behind);
	printf("worst difference: x %.6f px, y %.6f px, depth %.9f\n",
		worstX, worstY, worstZ);
	printf("verdict: %s\n", (worstX < 1.0 && worstY < 1.0 && worstZ < 1e-5)
		? "PASS - the HD matrix puts the point where the engine puts it"
		: "FAIL - the two paths disagree, an HD room would not line up");
}

/* ------------------------------------------------------- the S2b state -- */

static HDLightSource g_lights[HD_MAX_LIGHTS];
static int g_light_count = 0;
static HDFogBulb g_bulbs[HD_MAX_BULBS];
static int g_bulb_count = 0;
static double g_fog_start = 0;
static bool g_underwater = false;
static bool g_shimmer = false;
static double g_wibble = 0;
static int g_mesh_effect = 0;

/* Stage S5. -time sets the game clock the UV scroll and the wind ride on;
 * -water turns the water surface wobble on for the whole drawn room (through
 * sel1[3], the same fourth flag byte -shimmer uses); -motion off takes all
 * three motions away again, which is what the engine's hd_motion does. */
static double g_time = 0;
static bool g_water = false;
static bool g_motion = true;
static unsigned char g_water_table[22][64][4];

/* The bulbs go in room local and come out in VIEW space, because that is the
 * space HDFogBulb is in - aBuildFogBulbList (drawroom.cpp:738-745) transforms
 * every active bulb by the camera matrix once per frame, and the shader
 * measures the eye ray against it there. Handing a raw room position to the
 * struct would put the bulb somewhere behind the camera and cost an evening. */
static void FillLighting(HDFrameLighting& frame, HDRoomLighting& room,
	float centerX, float centerY, const float* view)
{
	static HDFogBulb viewBulbs[HD_MAX_BULBS];

	for (int i = 0; i < g_bulb_count; i++)
	{
		const HDFogBulb& in = g_bulbs[i];
		HDFogBulb& out = viewBulbs[i];

		out = in;

		if (view)
		{
			out.x = in.x * view[0] + in.y * view[4] + in.z * view[8] + view[12];
			out.y = in.x * view[1] + in.y * view[5] + in.z * view[9] + view[13];
			out.z = in.x * view[2] + in.y * view[6] + in.z * view[10] + view[14];
		}
	}

	memset(&frame, 0, sizeof(frame));
	memset(&room, 0, sizeof(room));

	frame.centerX = centerX;
	frame.centerY = centerY;
	frame.wibble = (float)g_wibble;
	frame.cameraUnderwater = g_underwater ? 1 : 0;
	frame.distanceFogStart = (float)g_fog_start;
	frame.wibbleTable = 0;		// filled by the caller
	frame.bulbs = viewBulbs;
	frame.bulbCount = g_bulb_count;
	frame.clock = (float)g_time;
	frame.motion = g_motion ? 1 : 0;

	if (g_bulb_count && view)
		printf("  bulb 1 in view space: %.0f %.0f %.0f (radius %.0f, density %.4f)\n",
			viewBulbs[0].x, viewBulbs[0].y, viewBulbs[0].z,
			viewBulbs[0].rad, viewBulbs[0].density);

	room.originX = room.originY = room.originZ = 0;	// the tool's world IS room local
	room.surfaceY = -1.0e9f;
	room.underwater = g_underwater ? 1 : 0;
	room.choppy = 1;
	room.surfaceGate = 0;
	room.redAlert = 0;
	room.sel1[3] = (g_shimmer || g_water) ? 1.0f : 0.0f;
	room.waterTable = g_water_table[g_mesh_effect];
	room.lights = g_lights;
	room.lightCount = g_light_count;
}

/* THE PARITY PROOF (stage S2b).
 *
 * Two implementations of one room vertex colour, on the processor:
 * hd_light.cpp's transcription of drawroom.cpp and its transcription of
 * hd_room.hlsl. They are fed the SAME vertex, and the bridge between the two
 * scales is prelight = (colour1 + colour2) / 2 - the remaster's baked light
 * divided back into the classic path's 0..255, which is exactly the inverse
 * of the shader's light * 127.5.
 *
 * So this is not a test of whether the remaster's art matches Core's
 * lighting; it cannot be, they are different art. It is a test of whether
 * every CONTRIBUTION - the dynamic lights, the water table, the distance fog,
 * the 128 split, the fog bulbs - lands in the same place with the same
 * weight. If it does, then switching a room to HD changes the picture by the
 * baked light and by nothing else.
 */
static void ParityProof(int room, const float* view, int wanted)
{
	HDRenderSample samples[HD_RENDER_SAMPLES];
	HDParityVertex verts[HD_RENDER_SAMPLES];
	HDParityScene scene;
	HDFrameLighting frame;
	HDRoomLighting lighting;
	double worstF[3], worstFinal[3];

	const int have = HDRenderGroupSampleData(room, samples, HD_RENDER_SAMPLES);

	printf("\n-- the parity proof -----------------------------------------\n");

	if (!have)
	{
		printf("group %d kept no samples - nothing to compare.\n", room);
		return;
	}

	FillLighting(frame, lighting, 1.0f, 1.0f, view);

	memset(&scene, 0, sizeof(scene));
	memcpy(scene.view, view, sizeof(scene.view));
	scene.originX = lighting.originX;
	scene.originY = lighting.originY;
	scene.originZ = lighting.originZ;
	scene.surfaceY = lighting.surfaceY;
	scene.underwater = lighting.underwater;
	scene.choppy = lighting.choppy;
	scene.surfaceGate = lighting.surfaceGate;
	scene.redAlert = lighting.redAlert;
	scene.sel1[0] = lighting.sel1[0];
	scene.sel1[1] = lighting.sel1[1];
	scene.sel1[2] = lighting.sel1[2];
	scene.sel1[3] = lighting.sel1[3];
	scene.waterTable = lighting.waterTable;
	scene.wibble = frame.wibble;
	scene.distanceFogStart = frame.distanceFogStart;
	scene.lights = g_lights;
	scene.lightCount = g_light_count;
	scene.bulbs = frame.bulbs;
	scene.bulbCount = frame.bulbCount;

	int count = have < wanted ? have : wanted;

	for (int i = 0; i < count; i++)
	{
		const HDRenderSample& in = samples[i];

		verts[i].x = (float)in.x;
		verts[i].y = (float)in.y;
		verts[i].z = (float)in.z;
		verts[i].nx = ((float)in.nx - 127.0f) * (1.0f / 128.0f);
		verts[i].ny = ((float)in.ny - 127.0f) * (1.0f / 128.0f);
		verts[i].nz = ((float)in.nz - 127.0f) * (1.0f / 128.0f);

		const unsigned long r = ((unsigned long)in.c1[0] + in.c2[0]) / 2;
		const unsigned long g = ((unsigned long)in.c1[1] + in.c2[1]) / 2;
		const unsigned long b = ((unsigned long)in.c1[2] + in.c2[2]) / 2;

		verts[i].prelight = (r << 16) | (g << 8) | b;
		verts[i].bits = in.bits;
	}

	{
		/* HOW BRIGHT THE REMASTER BAKES. The classic overbright split only
		 * changes anything above a light of 1, so this says whether it ever
		 * bites in real art. */
		double sum = 0, top = 0;
		int over = 0;

		for (int i = 0; i < count; i++)
			for (int k = 0; k < 3; k++)
			{
				const double L = ((double)samples[i].c1[k] + samples[i].c2[k]) / 255.0;

				sum += L;
				if (L > top) top = L;
				if (L > 1.0) over++;
			}

		printf("baked light over %d vertices: mean %.3f, max %.3f, %d of %d "
			"channels above 1.0\n", count, sum / (count * 3), top, over, count * 3);
	}

	HDLightParity(&scene, verts, count, worstF, worstFinal);

	printf("%d vertices of group %d, lights %d, bulbs %d, fog start %.0f, "
		"underwater %d, shimmer %d, wibble %d\n",
		count, room, g_light_count, g_bulb_count, g_fog_start,
		g_underwater ? 1 : 0, g_shimmer ? 1 : 0, (int)g_wibble);

	printf("worst |classic - shader| of the CONTRIBUTION, in the classic\n"
		"path's own 0..255, before its overbright split - this is the parity:\n");
	printf("  R %.3f   G %.3f   B %.3f\n", worstF[0], worstF[1], worstF[2]);
	printf("and of the FINAL brightness on a white texel, where the split is\n"
		"the deliberate deviation (see the long note in hd_room.hlsl):\n");
	printf("  R %.1f   G %.1f   B %.1f  (of 255)\n",
		worstFinal[0] * 255.0, worstFinal[1] * 255.0, worstFinal[2] * 255.0);

	double worst = 0;

	for (int k = 0; k < 3; k++)
		if (worstF[k] > worst)
			worst = worstF[k];

	printf("verdict: %s (%.3f of 255, budget 2)\n",
		worst <= 2.0 ? "PASS - every contribution lands with the same weight"
			: "FAIL - a contribution drifted", worst);
}

/* ------------------------------------------------------ stage S6 proofs -- */

/* WHAT THE DXT5 SWITCH COSTS, as a number rather than an opinion.
 *
 * One page is decoded from BC7 to A8R8G8B8 - the picture the lossless default
 * puts on the card - then compressed with the engine's own encoder and
 * decoded again with its inverse, which follows the block layout the hardware
 * decodes. The peak signal to noise ratio between the two is what the switch
 * costs, and it is measured over the whole page, not a corner of it.
 */
static bool ReadWholeFile(const char* path, unsigned char** out, unsigned long* size)
{
	FILE* file = fopen(path, "rb");

	*out = 0;
	*size = 0;

	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (length <= 0)
	{
		fclose(file);
		return false;
	}

	*out = (unsigned char*)malloc((size_t)length);

	if (!*out)
	{
		fclose(file);
		return false;
	}

	const size_t got = fread(*out, 1, (size_t)length, file);

	fclose(file);
	*size = (unsigned long)length;
	return got == (size_t)length;
}

static void PrintPsnr(const char* root, int howMany)
{
	printf("\n-- what DXT5 costs ------------------------------------------\n");

	if (HDPageCacheSlots() <= 0)
	{
		printf("no page is loaded - nothing to measure\n");
		return;
	}

	double sum = 0, worst = 999, best = 0;
	int measured = 0;

	for (int slot = 0; slot < HDPageCacheSlots() && measured < howMany; slot++)
	{
		const int page = HDPageCachePageOfSlot(slot);
		char buffer[300];
		unsigned char* file = 0;
		unsigned long size = 0;

		if (page < 0 || !ReadWholeFile(HDScanPagePath(root, page, buffer, sizeof(buffer)),
			&file, &size))
			continue;

		long w = 0, h = 0;
		const char* why = "";
		unsigned long* pixels = BC7DecodeDDS(file, size, 4096, &w, &h, &why);

		free(file);

		if (!pixels)
			continue;

		unsigned char* blocks = (unsigned char*)malloc(DXT5Size(w, h));
		unsigned long* back = (unsigned long*)malloc((size_t)w * (size_t)h * 4);

		if (blocks && back)
		{
			DXT5Encode(pixels, w, h, blocks);
			DXT5Decode(blocks, w, h, back);

			const double db = DXT5Psnr(pixels, back, w * h);

			if (measured < 4)
				printf("page %-5d %4ldx%-4ld  %7.2f dB   %ld KiB -> %ld KiB\n",
					page, w, h, db, (long)(w * h * 4 / 1024),
					(long)(DXT5Size(w, h) / 1024));

			sum += db;
			measured++;

			if (db < worst) worst = db;
			if (db > best) best = db;
		}

		free(blocks);
		free(back);
		free(pixels);
	}

	if (!measured)
	{
		printf("no page could be decoded - nothing to measure\n");
		return;
	}

	printf("%d page(s) measured: mean %.2f dB, worst %.2f dB, best %.2f dB\n",
		measured, sum / measured, worst, best);
	printf("(mip 0 only; a quarter of the video memory is what is bought with it)\n");
}

/* THE LEVEL CHANGE (#123, stage S6). HDLevelBuild frees the previous level's
 * buffers and pages before it builds the new one - so the card never holds two
 * levels at once, and the process never commits two levels' worth of transient
 * pixels. This walks the same road without the game: build, measure, end,
 * measure, build the next. If the free were not happening, the free texture
 * memory after the second level would be lower than after the first by the
 * whole of the first. */
static void RunSequence(const char* root, const char* list, int rooms,
	bool managed, int threads)
{
	char names[512];
	const char* why = "";

	strncpy(names, list, sizeof(names) - 1);
	names[sizeof(names) - 1] = 0;

	printf("\n-- the level change -----------------------------------------\n");
	printf("%-10s %10s %10s %10s %10s %10s\n", "level", "pages MiB",
		"vb+ib MiB", "commit", "peak", "free vram");

	char* save = 0;
	char* one = strtok_s(names, ",;", &save);

	while (one)
	{
		while (*one == ' ')
			one++;

		HDRenderStats stats;

		if (!HDRenderLevelBegin(root, one, rooms, managed, threads, &stats, &why))
		{
			printf("%-10s FAILED - %s\n", one, why);
			HDRenderLevelEnd();
			one = strtok_s(0, ",;", &save);
			continue;
		}

		printf("%-10s %10.1f %10.1f %10.1f %10.1f %10.1f\n", one,
			stats.pages.bytes / 1048576.0,
			(stats.vertexBytes + stats.indexBytes) / 1048576.0,
			CommitMiB(), PeakCommitMiB(),
			(double)g_device->GetAvailableTextureMem() / 1048576.0);

		HDRenderLevelEnd();

		printf("%-10s %10s %10s %10.1f %10.1f %10.1f\n", "  (freed)", "-", "-",
			CommitMiB(), PeakCommitMiB(),
			(double)g_device->GetAvailableTextureMem() / 1048576.0);

		one = strtok_s(0, ",;", &save);
	}

	printf("\nA level's own numbers must repeat when it comes round again: if\n"
		"the free texture memory after the second TITLE is lower than after\n"
		"the first, something of the level in between is still on the card.\n");
}

/* ---------------------------------------------------------------- main -- */

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		printf("usage: RoomProbe <5-folder> <LEVEL> <room> [x y z yaw pitch] [options]\n");
		printf("       -size <w> <h>  -fov <deg>  -all  -managed  -threads <n>\n");
		printf("       -rooms <n>  -out <prefix>  -groups  -nodraw  -matrixcheck\n");
		printf("       -light <x y z r g b falloff>   -bulb <x y z rad den [r g b]>\n");
		printf("       -fog <start end r g b>  -underwater  -shimmer  -wibble <n>\n");
		printf("       -effect <0..21>  -paritycheck\n");
		printf("       -time <seconds>  -water  -nomotion  -animtex\n");
		printf("       -budget <MiB>  -format rgba|dxt5  -psnr\n");
		printf("       -caps maxtex <n>  -caps noidx32  -seq <A,B,C>  -vramhog\n");
		return 1;
	}

	const char* root = argv[1];
	const char* stem = argv[2];
	const int room = atoi(argv[3]);

	double cx = 0, cy = 0, cz = 0, yaw = 0, pitch = 0;
	bool haveCamera = false;
	long width = 1280, height = 720;
	double fov = 80.0;
	bool allPasses = false, managed = false, groupsOnly = false, noDraw = false;
	bool matrixCheck = false;
	bool parityCheck = false;
	bool animTex = false;
	// Fix round R3a: draw the .TRG's sky group(s) behind the room, and only
	// them.
	bool sky = false, skyOnly = false;
	bool wibbleGiven = false;
	int threads = 8, rooms = 0;

	// Stage S6.
	double budgetMiB = 0;
	int pageFormat = HD_PAGE_FORMAT_RGBA;
	bool psnr = false;
	int capMaxTex = 0, capNoIndex32 = 0;
	const char* sequence = 0;
	bool vramHog = false;

	HDLightBuildWaterTable(g_water_table);
	const char* out = "roomprobe";

	int at = 4;

	// The five camera numbers, when they are there, come before the options.
	if (argc >= 9 && argv[4][0] != '-')
	{
		cx = atof(argv[4]);
		cy = atof(argv[5]);
		cz = atof(argv[6]);
		yaw = atof(argv[7]);
		pitch = atof(argv[8]);
		haveCamera = true;
		at = 9;
	}

	for (; at < argc; at++)
	{
		if (!strcmp(argv[at], "-size") && at + 2 < argc)
		{
			width = atol(argv[at + 1]);
			height = atol(argv[at + 2]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-fov") && at + 1 < argc)
			fov = atof(argv[++at]);
		else if (!strcmp(argv[at], "-threads") && at + 1 < argc)
			threads = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-rooms") && at + 1 < argc)
			rooms = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-out") && at + 1 < argc)
			out = argv[++at];
		else if (!strcmp(argv[at], "-all"))
			allPasses = true;
		else if (!strcmp(argv[at], "-managed"))
			managed = true;
		else if (!strcmp(argv[at], "-groups"))
			groupsOnly = true;
		else if (!strcmp(argv[at], "-nodraw"))
			noDraw = true;
		else if (!strcmp(argv[at], "-matrixcheck"))
			matrixCheck = true;
		else if (!strcmp(argv[at], "-paritycheck"))
			parityCheck = true;
		else if (!strcmp(argv[at], "-underwater"))
			g_underwater = true;
		else if (!strcmp(argv[at], "-time") && at + 1 < argc)
		{
			g_time = atof(argv[at + 1]);
			at++;
		}
		else if (!strcmp(argv[at], "-water"))
			g_water = true;
		else if (!strcmp(argv[at], "-nomotion"))
			g_motion = false;
		else if (!strcmp(argv[at], "-sky"))
			sky = true;
		else if (!strcmp(argv[at], "-skyonly"))
		{
			sky = true;
			skyOnly = true;
		}
		else if (!strcmp(argv[at], "-animtex"))
			animTex = true;
		else if (!strcmp(argv[at], "-shimmer"))
			g_shimmer = true;
		else if (!strcmp(argv[at], "-wibble") && at + 1 < argc)
		{
			g_wibble = atof(argv[++at]);
			wibbleGiven = true;
		}
		else if (!strcmp(argv[at], "-effect") && at + 1 < argc)
		{
			g_mesh_effect = atoi(argv[++at]);

			if (g_mesh_effect < 0 || g_mesh_effect > 21)
				g_mesh_effect = 0;
		}
		else if (!strcmp(argv[at], "-light") && at + 7 < argc)
		{
			if (g_light_count < HD_MAX_LIGHTS)
			{
				HDLightSource& l = g_lights[g_light_count++];

				l.x = (float)atof(argv[at + 1]);
				l.y = (float)atof(argv[at + 2]);
				l.z = (float)atof(argv[at + 3]);
				l.r = (float)atof(argv[at + 4]);
				l.g = (float)atof(argv[at + 5]);
				l.b = (float)atof(argv[at + 6]);
				l.falloff = (float)atof(argv[at + 7]);
			}
			else
				printf("RoomProbe: more than %d lights - the extra one is dropped, "
					"as the engine drops it\n", HD_MAX_LIGHTS);

			at += 7;
		}
		else if (!strcmp(argv[at], "-bulb") && at + 5 < argc)
		{
			const bool coloured = at + 8 < argc && argv[at + 6][0] != '-';

			if (g_bulb_count < HD_MAX_BULBS)
			{
				HDFogBulb& b = g_bulbs[g_bulb_count++];

				b.x = (float)atof(argv[at + 1]);
				b.y = (float)atof(argv[at + 2]);
				b.z = (float)atof(argv[at + 3]);
				b.rad = (float)atof(argv[at + 4]);
				b.density = (float)atof(argv[at + 5]);
				b.r = coloured ? (float)atof(argv[at + 6]) : 1.0f;
				b.g = coloured ? (float)atof(argv[at + 7]) : 1.0f;
				b.b = coloured ? (float)atof(argv[at + 8]) : 1.0f;
			}
			else
				printf("RoomProbe: more than %d fog bulbs - the extra one is "
					"dropped\n", HD_MAX_BULBS);

			at += coloured ? 8 : 5;
		}
		else if (!strcmp(argv[at], "-fog") && at + 5 < argc)
		{
			g_fog_start = atof(argv[at + 1]);
			printf("RoomProbe: fog start %.0f world units. TR5 has no fog end "
				"and no fog colour - end %s and colour %s/%s/%s are read and "
				"not used (drawroom.cpp:519 subtracts a ramp of 255 over the "
				"start distance again, from the vertex colour).\n",
				g_fog_start, argv[at + 2], argv[at + 3], argv[at + 4], argv[at + 5]);
			at += 5;
		}
		else if (!strcmp(argv[at], "-budget") && at + 1 < argc)
		{
			budgetMiB = atof(argv[at + 1]);
			at += 1;
		}
		else if (!strcmp(argv[at], "-format") && at + 1 < argc)
		{
			pageFormat = !_stricmp(argv[at + 1], "dxt5")
				? HD_PAGE_FORMAT_DXT5 : HD_PAGE_FORMAT_RGBA;
			at += 1;
		}
		else if (!strcmp(argv[at], "-psnr"))
			psnr = true;
		else if (!strcmp(argv[at], "-caps") && at + 1 < argc &&
			!strcmp(argv[at + 1], "noidx32"))
		{
			capNoIndex32 = 1;
			at += 1;
		}
		else if (!strcmp(argv[at], "-caps") && at + 2 < argc &&
			!strcmp(argv[at + 1], "maxtex"))
		{
			capMaxTex = atoi(argv[at + 2]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-seq") && at + 1 < argc)
		{
			sequence = argv[at + 1];
			at += 1;
		}
		else if (!strcmp(argv[at], "-vramhog"))
			vramHog = true;
		else
			printf("RoomProbe: ignoring unknown argument \"%s\"\n", argv[at]);
	}

	/* ONE KNOB FOR ALL THE MOTION. -time is the game clock the stage S5
	 * scroll and wind ride on; the water wobble and everything of stage S2
	 * ride on the engine's wibble counter, which the engine steps by four per
	 * game frame at 30 Hz and wraps at 256 (control.cpp:599). So unless
	 * -wibble says otherwise, -time sets that counter to the value the engine
	 * would have at that second, and two -time values are two moments of the
	 * whole picture rather than of half of it. */
	if (!wibbleGiven)
		g_wibble = (double)(((int)(g_time * 30.0) * 4) & 0xFC);

	printf("RoomProbe - %s room %d, %s pages, %d decoding threads\n",
		stem, room, managed ? "MANAGED" : "DEFAULT", threads);
	printf("install: %s\n", root);
	printf("commit before anything: %.1f MiB\n", CommitMiB());

	if (!MakeDevice())
	{
		DropDevice();
		return 2;
	}

	const UINT vramBefore = g_device->GetAvailableTextureMem();
	const char* why = "";

	/* STAGE S6 - the small card, before anything reads the device's caps.
	 * HDRenderInit copies the caps once and everything after it reads that
	 * copy, so the overrides have to be in place first. */
	if (capMaxTex || capNoIndex32)
	{
		printf("caps override: max texture %s, 32 bit index buffer %s\n",
			capMaxTex ? "forced down" : "as the device says",
			capNoIndex32 ? "FORCED OFF" : "as the device says");

		if (capMaxTex)
			printf("  MaxTextureWidth/Height := %d\n", capMaxTex);

		HDRenderCapOverride(capMaxTex, capNoIndex32);
	}

	if (!HDRenderInit(g_device, &why))
	{
		printf("RoomProbe: HDRenderInit failed - %s\n", why);
		DropDevice();
		return 3;
	}

	/* The texture policy. The device's own size limit is added inside
	 * HDRenderLevelBegin, exactly as it is in the game - this sets only the
	 * two the user owns. */
	{
		HDPageCachePolicy policy;

		policy.format = pageFormat;
		policy.budgetBytes = budgetMiB > 0 ? budgetMiB * 1048576.0 : 0.0;
		policy.maxSide = 0;
		HDPageCacheConfigure(&policy);

		printf("pages: format %s, budget %s\n",
			pageFormat == HD_PAGE_FORMAT_DXT5 ? "DXT5" : "A8R8G8B8",
			budgetMiB > 0 ? "" : "none");

		if (budgetMiB > 0)
			printf("  budget %.0f MiB\n", budgetMiB);
	}

	/* THE CARD, FILLED UP (#123, stage S6). 4096x4096 A8R8G8B8 is 64 MiB a
	 * block, taken in D3DPOOL_DEFAULT so it really lives on the card, until
	 * CreateTexture says no. Nothing is ever released - the process ends and
	 * Direct3D takes it all back - because the point is to leave the card
	 * full while the level loads. */
	std::vector<IDirect3DTexture9*> hogs;

	if (vramHog)
	{
		printf("\n-- filling the card -----------------------------------------\n");
		printf("free texture memory before: %.0f MiB\n",
			(double)g_device->GetAvailableTextureMem() / 1048576.0);

		for (;;)
		{
			IDirect3DTexture9* block = 0;

			if (FAILED(g_device->CreateTexture(4096, 4096, 1, 0, D3DFMT_A8R8G8B8,
				D3DPOOL_DEFAULT, &block, 0)) || !block)
				break;

			hogs.push_back(block);
		}

		printf("%d block(s) of 64 MiB taken, %.0f MiB of texture memory left\n",
			(int)hogs.size(), (double)g_device->GetAvailableTextureMem() / 1048576.0);
	}

	PrintDepthTruth("(device fresh)");

	if (sequence)
	{
		RunSequence(root, sequence, rooms, managed, threads);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	HDRenderStats stats;
	const double commitBefore = CommitMiB();

	if (!HDRenderLevelBegin(root, stem, rooms, managed, threads, &stats, &why))
	{
		printf("RoomProbe: HDRenderLevelBegin failed - %s\n", why);

		// The page cache's notes before the shutdown empties them: on the way
		// out they are the only place that says which page it was.
		for (int i = 0; i < HDPageCacheNoteCount(); i++)
			printf("  note: %s\n", HDPageCacheNote(i));

		HDRenderShutdown();
		DropDevice();
		return 4;
	}

	const double commitAfter = CommitMiB();
	const UINT vramAfter = g_device->GetAvailableTextureMem();

	printf("\n-- the level ------------------------------------------------\n");

	if (rooms > 0)
		printf(".TRG version %d, %d groups (%d rooms handed in, %d sky, %d empty)\n",
			stats.trgVersion, stats.groups, stats.rooms, stats.skyGroups,
			stats.emptyGroups);
	else
		printf(".TRG version %d, %d groups (no room count given - none counted "
			"as sky), %d empty\n", stats.trgVersion, stats.groups, stats.emptyGroups);
	printf("%d vertices (%.1f MiB), %d indices (%.1f MiB, %d bit), %d ranges%s\n",
		stats.vertices, stats.vertexBytes / 1048576.0,
		stats.indices, stats.indexBytes / 1048576.0, stats.index32 ? 32 : 16,
		stats.ranges, stats.splitRanges ? " (some split by the primitive limit)" : "");
	printf("biggest range %d triangles, %d triangles on the fallback page\n",
		stats.biggestRangeTris, stats.fallbackPageTris);
	printf("S6: widest draw call reaches over %d vertices, widest single "
		"triangle %d (a 16 bit index reaches 65536), device holds textures "
		"up to %d\n",
		stats.indexWindow, stats.triangleWindow, stats.maxTextureSide);
	printf("S5: %d scrolling part(s) in %d draw calls (%d triangles), "
		"%d vertices with a wind weight, %d flipbook part(s) not drawn\n",
		stats.scrollParts, stats.scrollRanges, stats.scrollTris,
		stats.swayVertices, stats.flipbookParts);
	// Fix round A (A6): the wind belongs to material 8, and this says how
	// much of the .TRG's own sway weight was on other materials.
	printf("A6: %d moving part(s) (materials 1, 7, 8); %d vertices kept "
		"their weight, "
		"%d lost it\n", stats.swayParts, stats.swayVertices, stats.swayDropped);
	// Fix round A2: the opaque parts that draw with the alpha test on,
	// which is the three cut-out materials 6, 7 and 8 and nothing else.
	printf("A2: %d opaque cut-out part(s) (materials 6, 7, 8)\n",
		stats.cutoutParts);
	// Fix round R3a: the additive parts whose .TRG material handed them a
	// weight of their own, and the ones that also swing on the game clock.
	printf("R3a: %d additive part(s) weighted (smallest %.3f), %d of them "
		"swinging\n", stats.addWeightParts, stats.addWeightMin,
		stats.addSwingParts);
	printf("read %.0f ms, build %.0f ms\n", stats.readMs, stats.buildMs);

	printf("\n-- the pages ------------------------------------------------\n");
	printf("%d named, %d distinct: %d from 5, %d from 4, %d white, %d failed\n",
		stats.pages.pages, stats.pages.slots, stats.pages.fromGame5,
		stats.pages.fromGame4, stats.pages.fromWhite,
		stats.pages.failed);
	printf("%d mip levels, widest %d, %d past 1024, %.1f MiB of %s "
		"on the device\n", stats.pages.mipLevels, stats.pages.widest,
		stats.pages.bigPages, stats.pages.bytes / 1048576.0,
		stats.pages.formatUsed == HD_PAGE_FORMAT_DXT5 ? "DXT5" : "A8R8G8B8");
	// Fix round A (A2): the pages whose alpha channel really cuts. Those,
	// and only those, make the OPAQUE pass run its alpha test.
	printf("%d of them are cut-out pages (alpha below 128 on at least 2%% "
		"of the top mip)\n", stats.pages.cutoutPages);

	/* WHICH ones, by page number, and how much of each is hole. Without the
	 * list the count alone cannot be held against the .TRG's own materials,
	 * and fix round A2's regression was exactly a page in this list that no
	 * leaf ever touches - see MODIFICATIONS.md, fix round A2. */
	if (stats.pages.cutoutPages > 0)
	{
		printf("   page(hole %%):");

		for (int s = 0; s < HDPageCacheSlots(); s++)
			if (HDPageCacheSlotCutout(s))
				printf(" %d(%d%%)", HDPageCachePageOfSlot(s),
					HDPageCacheSlotCutoutPermille(s) / 10);

		printf("\n");
	}
	printf("search %.0f ms, decode %.0f ms (%d threads), upload %.0f ms, total %.0f ms\n",
		stats.pages.readMs, stats.pages.decodeMs, stats.pages.threads,
		stats.pages.uploadMs, stats.pages.totalMs);
	printf("pool %s: commit %.1f -> %.1f MiB (+%.1f), peak %.1f MiB, "
		"texture memory %.1f -> %.1f MiB (-%.1f)\n",
		managed ? "MANAGED" : "DEFAULT",
		commitBefore, commitAfter, commitAfter - commitBefore, PeakCommitMiB(),
		(double)vramBefore / 1048576.0, (double)vramAfter / 1048576.0,
		((double)vramBefore - (double)vramAfter) / 1048576.0);

	printf("S6 budget %s: need %.1f MiB, already there %.1f, got %.1f - "
		"%d page(s) gave up %d level(s), %d shrunk by the device, format %s, "
		"out of memory %d\n",
		stats.pages.budgetBytes > 0 ? "in force" : "none",
		stats.pages.needBytes / 1048576.0, stats.pages.haveBytes / 1048576.0,
		stats.pages.bytes / 1048576.0, stats.pages.droppedMip0,
		stats.pages.droppedLevels, stats.pages.clampedByCaps,
		stats.pages.formatUsed == HD_PAGE_FORMAT_DXT5 ? "DXT5" : "A8R8G8B8",
		stats.pages.outOfMemory);

	if (stats.pages.compressMs > 0)
		printf("compress %.0f ms (over the same threads as the decode)\n",
			stats.pages.compressMs);

	for (int i = 0; i < HDPageCacheNoteCount(); i++)
		printf("  note: %s\n", HDPageCacheNote(i));

	if (psnr)
		PrintPsnr(root, 16);

	if (groupsOnly)
	{
		printf("\n-- the groups -----------------------------------------------\n");

		for (int g = 0; g < HDRenderGroupCount(); g++)
		{
			float mins[3], maxs[3];

			if (!HDRenderGroupBounds(g, mins, maxs))
				continue;

			printf("  group %3d  tris %6d/%5d/%5d  ranges %3d  "
				"x %6.0f..%-6.0f y %6.0f..%-6.0f z %6.0f..%-6.0f\n",
				g, HDRenderGroupTriangles(g, 0), HDRenderGroupTriangles(g, 1),
				HDRenderGroupTriangles(g, 2), HDRenderGroupRanges(g, -1),
				mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);
		}

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (animTex)
	{
		/* WHAT MOVES IN THIS LEVEL, ROOM BY ROOM (#123, stage S5).
		 *
		 * All three columns come out of the .TRG itself and none of them is
		 * a guess about the classic level:
		 *
		 *   scroll    a part whose material is 1 and whose param.xy is not
		 *             zero - a UV scroll in page units per second. It is the
		 *             HD twin of the classic UVROT branch
		 *             (output.cpp:909-931), and it is drawn.
		 *   sway      vertices whose TrgVertex::sway is not zero - the
		 *             remaster's own wind weight, 0..32767. Drawn.
		 *   flipbook  parts whose material is 13, the remaster's own four
		 *             frame page animation. COUNTED AND NOT DRAWN: a range
		 *             names one page and the animation names four, which is
		 *             a different piece of work. This column is the size of
		 *             that gap, per level, in one number.
		 */
		printf("\n-- what moves ------------------------------------------------\n");
		printf("  %-6s %8s %8s %10s %8s %8s %9s\n",
			"room", "ranges", "scroll", "page/s", "swayV", "swayMax", "flipbook");

		int rooms = 0, scroll = 0, sway = 0, flip = 0;

		for (int g = 0; g < HDRenderGroupCount(); g++)
		{
			HDRenderGroupMotion m;

			if (!HDRenderGroupMotionInfo(g, &m))
				continue;

			if (!m.scrollRanges && !m.swayVertices && !m.flipbookParts)
				continue;

			rooms++;
			scroll += m.scrollRanges;
			sway += m.swayVertices;
			flip += m.flipbookParts;

			printf("  %-6d %8d %8d %5.2f/%-4.2f %8d %8d %9d\n",
				g, HDRenderGroupRanges(g, -1), m.scrollRanges,
				m.scrollU, m.scrollV, m.swayVertices, m.swayMax,
				m.flipbookParts);
		}

		printf("  %d room(s) move: %d scrolling draw calls (%d parts, %d triangles), "
			"%d swaying vertices, %d flipbook parts NOT drawn\n",
			rooms, scroll, stats.scrollParts, stats.scrollTris, sway, flip);

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	const double persp = EnginePersp(height, fov);
	PrintZProof(persp);

	if (noDraw)
	{
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* --- the camera --------------------------------------------------- */
	float mins[3], maxs[3];

	if (!HDRenderGroupBounds(room, mins, maxs))
	{
		printf("\nRoomProbe: group %d draws nothing - no picture to take.\n", room);
		HDRenderShutdown();
		DropDevice();
		return 5;
	}

	printf("\n-- room %d ---------------------------------------------------\n", room);
	printf("triangles %d opaque, %d alpha, %d additive; %d ranges\n",
		HDRenderGroupTriangles(room, 0), HDRenderGroupTriangles(room, 1),
		HDRenderGroupTriangles(room, 2), HDRenderGroupRanges(room, -1));
	printf("room-local box: x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
		mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

	if (!haveCamera)
	{
		/* THE MIDDLE OF THE ROOM, LOOKING AT THE LONGEST WALL. The longest
		 * wall runs along the wider of the two horizontal axes, so the view
		 * goes along the OTHER one, from the near end of it. Eye height is a
		 * quarter of the room down from the ceiling - which is a quarter of
		 * the way from the smallest y, because y counts downwards. */
		const double ex = 0.5 * ((double)mins[0] + (double)maxs[0]);
		const double ez = 0.5 * ((double)mins[2] + (double)maxs[2]);
		const double dx = (double)maxs[0] - (double)mins[0];
		const double dz = (double)maxs[2] - (double)mins[2];

		cy = (double)mins[1] + 0.35 * ((double)maxs[1] - (double)mins[1]);

		if (dx >= dz)
		{
			// The long walls face along z: stand at the near z wall and look
			// across the room at the far one.
			cx = ex;
			cz = (double)mins[2] + 0.12 * dz;
			yaw = 0.0;
		}
		else
		{
			cx = (double)mins[0] + 0.12 * dx;
			cz = ez;
			yaw = 90.0;
		}

		pitch = 0.0;
		printf("camera (auto): %.0f %.0f %.0f  yaw %.0f  pitch %.0f\n",
			cx, cy, cz, yaw, pitch);
	}
	else
		printf("camera: %.0f %.0f %.0f  yaw %.0f  pitch %.0f\n", cx, cy, cz, yaw, pitch);

	printf("persp %.1f at %ldx%ld, fov %.0f deg\n", persp, width, height, fov);

	/* --- the matrices ------------------------------------------------- */
	float view[16];
	D3DMATRIX projection;
	double a = 0, b = 0, boo = 0;

	LookAt(cx, cy, cz, yaw, pitch, view);
	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);
	HDRenderMakeProjection(persp, (double)width * 0.5, (double)height * 0.5,
		a, b, &projection);

	if (matrixCheck)
	{
		MatrixProof(room, view, projection, persp, (double)width, (double)height, a, boo);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	if (parityCheck)
	{
		ParityProof(room, view, 200);
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* --- the picture -------------------------------------------------- */
	printf("\n-- the pictures ---------------------------------------------\n");

	Shot shot;
	char path[512];

	if (!ShotBegin(shot, width, height))
	{
		ShotEnd(shot);
		HDRenderShutdown();
		DropDevice();
		return 6;
	}

	Snapshot before, after;

	PoseAsClassicPath();
	Take(before);

	HDFrameLighting frame;
	HDRoomLighting lighting;
	float wibbleTable[32];

	for (int i = 0; i < 32; i++)
		wibbleTable[i] = (float)(2.0 * sin(i * 0.19634955));

	FillLighting(frame, lighting, (float)width * 0.5f, (float)height * 0.5f, view);
	frame.wibbleTable = wibbleTable;

	printf("S2b: %d light(s), %d bulb(s), fog start %.0f, underwater %d, "
		"shimmer %d, wibble %d, water table row %d\n",
		g_light_count, g_bulb_count, g_fog_start, g_underwater ? 1 : 0,
		g_shimmer ? 1 : 0, (int)g_wibble, g_mesh_effect);
	printf("S5 : motion %d, clock %.2f s, water wobble %d\n",
		g_motion ? 1 : 0, g_time, (g_water || g_shimmer) ? 1 : 0);

	g_device->BeginScene();
	HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
	PrintDepthTruth("(inside the HD frame)");
	/* Fix round R3a: the sky dome, first and camera centred - the classic
	 * path's own place for its horizon mesh. The world matrix is the
	 * translation to the camera and nothing else, which is exactly what
	 * phd_TranslateAbs(camera.pos) leaves on the engine's matrix stack. */
	bool skyDrew = false;

	if (sky)
	{
		D3DMATRIX skyWorld;

		memset(&skyWorld, 0, sizeof(skyWorld));
		skyWorld._11 = skyWorld._22 = skyWorld._33 = skyWorld._44 = 1.0f;
		skyWorld._41 = (float)cx;
		skyWorld._42 = (float)cy;
		skyWorld._43 = (float)cz;
		skyDrew = HDRenderSky(&skyWorld);
	}

	const bool drew = skyOnly ? false : HDRenderRoom(room, 0, &lighting);
	const int calls = HDRenderDrawCalls();
	HDRenderFrameEnd();
	g_device->EndScene();

	Take(after);

	printf("HDRenderRoom returned %s after %d draw calls\n",
		drew ? "true" : "FALSE", calls);

	if (sky)
		printf("R3a: %d sky group(s), %d unlit part(s), HDRenderSky returned %s\n",
			HDRenderSkyGroups(), stats.skyFlatParts, skyDrew ? "true" : "FALSE");

	const int broken = Diff(before, after);
	printf("state bracket: %d of %d watched states changed - %s\n",
		broken, kStateCount + kSamplerCount + kStageCount + 8,
		broken ? "FAIL, the classic path would find a different device"
			: "PASS, the device came back exactly as it went in");

	sprintf(path, "%s_%s_r%d.png", out, stem, room);
	ShotWrite(shot, path);
	ShotEnd(shot);

	if (allPasses)
	{
		static const char* names[3] = { "opaque", "alpha", "additive" };

		for (int pass = 0; pass < 3; pass++)
		{
			if (!ShotBegin(shot, width, height))
			{
				ShotEnd(shot);
				break;
			}

			g_device->BeginScene();
			HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

			if (!skyOnly)
				HDRenderRoomPass(room, pass, 0, &lighting);

			HDRenderFrameEnd();
			g_device->EndScene();

			sprintf(path, "%s_%s_r%d_%d_%s.png", out, stem, room, pass, names[pass]);
			ShotWrite(shot, path);
			ShotEnd(shot);
		}
	}

	printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());

	HDRenderShutdown();
	DropDevice();
	return 0;
}

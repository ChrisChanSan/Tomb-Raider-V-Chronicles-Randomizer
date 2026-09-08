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
 *     -with <group> <dx dy dz>
 *                     draw that group too, at that world offset from the
 *                     room - the difference of the two classic room origins,
 *                     which RoomCheck prints per room (#123, fix round E).
 *                     Up to eight, repeat the switch.
 *     -oldorder       draw the scene room-major, all three passes of one room
 *                     before the next - the order the HD path had before fix
 *                     round E6. Without it the scene is drawn the way
 *                     HDFrameDraw draws it: every room's solid passes, then
 *                     every room's additive one.
 *     -black          clear the picture to black instead of magenta. The
 *                     additive pass alone can only be measured against black
 *                     (fix round E).
 *     -noshore        the swell reaches the water's own edge at full height,
 *                     as it did before fix round E3 - the before picture out
 *                     of the same build.
 *     -noupright      the wall vertices at the waterline keep the surface's
 *                     UV wobble and its caustic, as they did before fix
 *                     round H (H4) - the before picture out of the same
 *                     build.
 *     -noblend        material 14 stays in the alpha TEST it was in before
 *                     fix round H (H2) - the glass drawn as a hard mask of
 *                     itself, the before picture out of the same build.
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
 *     -roomplane <y x0 z0 x1 z1>
 *                     the same, on the OTHER shimmer bit - the answer a
 *                     ROOM_REFLECT + ROOM_REFLECT_CEILING room gives, which
 *                     is what the Rome levels' water carries (#123, fix
 *                     round E, E2). Both switches now also set the room's
 *                     surface height and its selector, as the engine does.
 *     -plane <y x0 z0 x1 z1>
 *                     four SURFACE water marks at the corners of that box,
 *                     room local, as the classic path leaves them on the
 *                     corners of one water quad. Fix round R3b floods the
 *                     plane between them, and the R3b line says how many
 *                     HD vertices the four corners were missing.
 *     -watersurvey <folder holding the classic .TRC>
 *                     the offline twin of hd_level.cpp's BuildWaterSurfaces:
 *                     per reflecting room of the classic level, the faces in
 *                     its minfloor plane and the answer HDRenderGroupFlatArt
 *                     gives for each of them, then the majority the engine
 *                     takes (#123, fix round H, H1). Prints and stops.
 *     -flatart <y x0 z0 x1 z1>
 *                     does the drawn group draw anything in that horizontal
 *                     plane? The question hd_level.cpp asks per classic water
 *                     face of an HD room (#123, fix round H, H1): a NO is a
 *                     plane the remaster left empty, and there the level's own
 *                     water surface is drawn on top of the HD room. Room
 *                     local. Repeat the switch, up to eight.
 *     -noreflect      the gloss of fix round R3b off - the picture fix
 *                     round R3a drew, out of the same build.
 *     -shatter        hide every destructible piece of the drawn room -
 *                     what the room looks like after Lara has shot the
 *                     statics whose art the .TRG baked in (R3b).
 *     -hasgeom <x0 y0 z0 x1 y1 z1>
 *                     does the drawn group draw anything inside that
 *                     room-local box? The question hd_level.cpp asks per
 *                     static mesh; a NO is a static the room seam would
 *                     make vanish, and it is drawn the classic way
 *                     instead (R3b). Repeat the switch, up to eight.
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
#include <set>

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
/* H1: the randomizer's own reader for the CLASSIC level, so that the water
 * survey can hold the level's own faces against the .TRG's art through the
 * engine's own gate. Nothing else in this tool needs a classic level. */
#include "core/Tr5Level.h"
#include <string>

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
	/* #123 group M: the scissor RECTANGLE. D3DRS_SCISSORTESTENABLE has been
	 * in the render state list above since stage S3-1; the rectangle beside
	 * it is a device state of its own, and group M is the first thing in the
	 * HD path that writes it. */
	RECT scissor;
	BOOL scissorOk;
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

	// Group M: the rectangle. A device with no scissor cap fails the Get -
	// then the flag is FALSE in both snapshots and they still compare equal.
	shot.scissorOk = SUCCEEDED(g_device->GetScissorRect(&shot.scissor));

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

	// Group M: the scissor rectangle, which the HD room path now sets per
	// room and hd_render's bracket has to put back with everything else.
	if (before.scissorOk != after.scissorOk ||
		(before.scissorOk &&
			(before.scissor.left != after.scissor.left ||
			before.scissor.top != after.scissor.top ||
			before.scissor.right != after.scissor.right ||
			before.scissor.bottom != after.scissor.bottom)))
	{
		printf("  BROKEN scissor rect: %ld,%ld,%ld,%ld -> %ld,%ld,%ld,%ld\n",
			before.scissor.left, before.scissor.top,
			before.scissor.right, before.scissor.bottom,
			after.scissor.left, after.scissor.top,
			after.scissor.right, after.scissor.bottom);
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

// -black: clear the render target to black instead of magenta - see the note
// in ShotBegin (#123, fix round E).
static bool g_black = false;

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
	 * result is the background showing through.
	 *
	 * -black turns it off, and there is one measurement that needs it (#123,
	 * fix round E): the ADDITIVE pass on its own is blended ONE / ONE over
	 * whatever is already there, so on magenta every covered pixel starts at
	 * 170 of 255 and "how much of white is this glow" cannot be read out of
	 * the picture at all. On black it can. */
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		g_black ? D3DCOLOR_XRGB(0, 0, 0) : D3DCOLOR_XRGB(255, 0, 255),
		1.0f, 0);

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
	double peak = 0;
	long long white = 0;

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

			/* H3: the PEAK and the white share, which is how fix round R3a
			 * quoted a light shaft and how the player looks at one - a mean
			 * says nothing about a fan whose middle is saturated. */
			{
				const unsigned long r = (c >> 16) & 255;
				const unsigned long g = (c >> 8) & 255;
				const unsigned long b = c & 255;
				const unsigned long m = r > g ? (r > b ? r : b) : (g > b ? g : b);

				if ((double)m > peak)
					peak = (double)m;

				if (m >= 230)
					white++;
			}
		}
	}

	shot.readback->UnlockRect();

	const double texels = (double)shot.w * (double)shot.h;
	const bool ok = WritePng(path, pixels, shot.w, shot.h);
	free(pixels);

	printf("  %-58s %5.1f%% background, mean %.1f/255 (%.1f%%), peak %.0f "
		"(%.1f%%), white %.2f%%\n",
		path, 100.0 * (double)background / texels, sum / (texels * 3.0),
		100.0 * sum / (texels * 3.0 * 255.0), peak, 100.0 * peak / 255.0,
		100.0 * (double)white / texels);

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

/* ------------------------------------------------------ scissor proof -- */

/* WHAT WAS DRAWN INSIDE THE WINDOW AND WHAT WAS DRAWN OUTSIDE IT (#123, group
 * M). The picture is read back and every pixel asked one question: is it still
 * the colour ShotBegin cleared it to? A pixel that is not was drawn.
 *
 * The background is MAGENTA unless -black said otherwise, and that matters
 * here more than anywhere else in this tool: on a black background a black
 * pixel of geometry cannot be told from a pixel nobody touched, and the whole
 * count would read zero whether the scissor worked or not. So -black turns
 * the proof into a warning and no number. */
static void ScissorProof(Shot& shot, const RECT& box)
{
	D3DLOCKED_RECT rect;

	if (g_black)
	{
		printf("group M: -scissor cannot be proved with -black - a black pixel "
			"of geometry and an untouched pixel are the same colour.\n");
		return;
	}

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)) ||
		FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		printf("group M: reading the picture back for the scissor proof failed\n");
		return;
	}

	long long inside = 0, outside = 0, insideArea = 0;

	for (long y = 0; y < shot.h; y++)
	{
		const unsigned long* src =
			(const unsigned long*)((const unsigned char*)rect.pBits + (size_t)y * rect.Pitch);

		for (long x = 0; x < shot.w; x++)
		{
			const bool in = x >= box.left && x < box.right &&
				y >= box.top && y < box.bottom;

			if (in)
				insideArea++;

			if ((src[x] & 0x00FFFFFFu) == 0x00FF00FFu)
				continue;		// nobody touched it

			if (in)
				inside++;
			else
				outside++;
		}
	}

	shot.readback->UnlockRect();

	printf("group M: scissor %ld,%ld,%ld,%ld over %ldx%ld - %lld pixel(s) "
		"drawn inside it (%.1f%% of the rectangle), %lld outside - %s\n",
		box.left, box.top, box.right, box.bottom, shot.w, shot.h,
		inside, insideArea ? 100.0 * (double)inside / (double)insideArea : 0.0,
		outside,
		outside ? "FAIL, the scissor test let something through"
			: inside ? "PASS, nothing was drawn outside the window"
			: "INCONCLUSIVE, nothing was drawn at all - move the camera");
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
/* Fix round R3b, -plane: four SURFACE marks at the corners of a box, as
 * the classic path would leave them on the corners of one water quad. The
 * renderer's flood then has to stamp every HD vertex of the group inside
 * that box at that height, and the difference between `stamped` and 4 is
 * what the corner rule of stage S2 was missing. */
static HDWaterMark g_plane_marks[4];
static bool g_plane = false;
/* Fix round R3b, the gloss: tomb5.hd_reflect, on by default here as it
 * is in the game, off with -noreflect so that the before picture can be
 * drawn out of the same build. */
static bool g_reflect = true;
/* Fix round R3b: hide every destructible piece of the drawn room, which
 * is what the engine does once Lara has shot the static that stands
 * there - the after picture of a shattered room, out of the same build. */
static bool g_shatter = false;
/* Fix round R3b, -hasgeom: ask the renderer whether the drawn group has
 * any geometry inside a room-local box - the question hd_level.cpp asks
 * once per static mesh of an HD room. Up to eight boxes per run. */
static float g_geom_box[8][6];
static int g_geom_count = 0;

/* Fix round H (H1), -flatart: ask the renderer whether the drawn group draws
 * anything in one horizontal plane - the question hd_level.cpp asks once per
 * classic water face of an HD room, and the gate that decides whether that
 * face is drawn on top of the HD room or left to the remaster's own art.
 * y, x0, z0, x1, z1, room local. Up to eight per run. */
static int g_flat_box[8][5];
static int g_flat_count = 0;
// H1, -watersurvey: the folder holding the classic .TRC files.
/* #123 GROUP M: the portal window, as a scissor rectangle. -scissor l,t,r,b
 * draws the room with exactly the call the engine makes per room -
 * HDRenderScissor - and then counts, on the picture itself, how many pixels
 * were drawn INSIDE the rectangle and how many outside it. Outside must be
 * zero: that is the whole claim of the group, and it is the one part of it a
 * tool without a portal walk can prove. */
static bool g_scissor = false;
static long g_scissor_box[4] = { 0, 0, 0, 0 };

static std::string g_survey;
/* FIX ROUND T (T1), -roombox <folder holding the classic .TRC>: hand the
 * renderer the rooms' own boxes AND (fix round T2, T2-2) the list of rooms
 * that may be holding another room's art, exactly as hd_level.cpp's
 * BuildRoomBoxes and BuildRoomIntrusions do, so that the split the engine
 * draws with is the split this tool draws with. Without it every group is one
 * piece and HD_WINDOW_OPEN and HD_WINDOW_INTRUDING both draw the whole room -
 * which is also the proof that a level built with no list is unchanged. */
static std::string g_roombox;
static std::vector<HDRoomBox> g_boxes_built;
static std::vector<HDIntrusion> g_intrusions_built;
static int g_window_mode = HD_WINDOW_ALL;

/* -clipgroup <group> <l,t,r,b> (#123, fix round T2, T2-2): ONE group drawn
 * inside a scissor rectangle and every other group drawn whole.
 *
 * `-scissor` puts the rectangle round the whole scene, which is not what the
 * engine ever does: the portal window is a property of ONE room, and the bug
 * of round T is exactly that the wrong room had one. To show that in a
 * picture the tool has to be able to cut one group and leave its neighbour
 * alone - which is what this does, with the engine's own HDRenderScissor
 * round that group's own draw calls. */
static int g_clip_group = -1;
static long g_clip_box[4] = { 0, 0, 0, 0 };

/* -tround <group> <l,t,r,b> (#123, fix round T2, T2-2): draw ONE group the way
 * fix round T drew a room on its overlap list - its INSIDE half inside the
 * rectangle the portal walk gave it, its OVERHANG over the whole screen - and
 * every other group whole. It is the "before" picture of this round, and it
 * is the engine's own code path: HDRenderSplitOverhang puts round T's index
 * layout back for the build, and the two calls are the two DrawRoomWindowed
 * made. */
static int g_tround_group = -1;
static long g_tround_box[4] = { 0, 0, 0, 0 };

/* -walk (#123, fix round T3, T3-2): THE CLASSIC PORTAL WALK, RUN OFFLINE.
 *
 * WHY. The play test's ROOMFLASH lines say a room was in the draw list on the
 * frame before and on the frame after and not on the one in between. The line
 * names the eye but not what refused the portal, and the walk is the one part
 * of the picture no renderer can show: it is arithmetic on the classic level's
 * own portal records. So this runs that arithmetic here, from the classic
 * .TRC alone, with no device, no .TRG and no picture - the two functions
 * copied line for line out of game\draw.cpp (`GetRoomBounds` :2939 and
 * `SetRoomBounds` :2795) with their own names on the numbers.
 *
 * WHAT IT ANSWERS. Given an eye and a seed room it prints the room list, and
 * for a room named with -walkwant the REASON that room is or is not in it:
 *
 *   reached      a portal from a listed room put it in the list
 *   side         `if (x && (x ^ p->normal[0]) < 0)` - the eye is on the far
 *                side of the portal's own plane (game\draw.cpp:3010)
 *   toonear      all four corners fell behind the eye, `tooNear == 4`
 *                (game\draw.cpp:2863)
 *   empty        the rectangle came out with no width or no height after the
 *                clamp against the room it was seen through
 *   covered      the room's window already covers what this portal would give
 *                it - the early out at the top of SetRoomBounds; not a
 *                refusal, the room is in the list already
 *   not reached  no portal to it out of ANY listed room, on any test
 *
 * AND A FAN OVER THE VIEW. The ROOMFLASH line has no camera angle in it -
 * that is what T3-1 adds to the seam - so the answer is asked at every yaw of
 * a fan instead, which also says the thing worth knowing: whether a room is
 * refused at a hair's breadth of one view direction (a portal sliver that
 * crosses the sub pixel threshold, which is ordinary and always has been) or
 * over a whole arc (which would be a rule).
 *
 * The window is -size, so it must be given the play test's own resolution to
 * mean anything. */
namespace walkoff
{
	struct Room
	{
		long x, y, z;
		long ceiling, floor;		// world y, the classic room box
		int xs, zs;
		unsigned flags;
		short left, right, top, bottom;
		short test_left, test_right, test_top, test_bottom;
		char bound_active;
		std::vector<Tr5Level::PortalRecord> doors;
	};

	static std::vector<Room> g_room;
	static long g_list[128];
	static long g_start, g_end;
	static std::vector<short> g_drawn;
	static short g_winxmax, g_winymax;
	static float g_persp, g_centerx, g_centery;
	static float g_view[9];			// the eye's rotation, rows first
	static long g_eye[3];

	// Every portal the walk looked at this run, with what became of it.
	struct Verdict
	{
		int from, to;
		char why[16];
		double n[4];
	};

	static std::vector<Verdict> g_verdict;

	static void Note(int from, int to, const char* why,
		double a, double b, double c, double d)
	{
		Verdict v;

		v.from = from;
		v.to = to;
		strncpy(v.why, why, sizeof(v.why) - 1);
		v.why[sizeof(v.why) - 1] = 0;
		v.n[0] = a;
		v.n[1] = b;
		v.n[2] = c;
		v.n[3] = d;
		g_verdict.push_back(v);
	}

	/* SetRoomBounds (game\draw.cpp:2795), with `aMXPtr` handed in as the nine
	 * rotation terms plus the three translation ones that phd_TranslateAbs
	 * works out for the room the portal is seen FROM. */
	static void SetRoomBounds(const Tr5Level::PortalRecord& p, int rn,
		Room& actualRoom, const float* mx, int fromRoom)
	{
		Room& r = g_room[(size_t)rn];
		float vx[4], vy[4], vz[4];
		float x, y, z, tooNear, tL, tR, tT, tB;

		if (r.left <= actualRoom.test_left && r.right >= actualRoom.test_right &&
			r.top <= actualRoom.test_top && r.bottom >= actualRoom.test_bottom)
		{
			Note(fromRoom, rn, "covered", r.left, r.top, r.right, r.bottom);
			return;
		}

		tL = (float)actualRoom.test_right;
		tR = (float)actualRoom.test_left;
		tB = (float)actualRoom.test_top;
		tT = (float)actualRoom.test_bottom;
		tooNear = 0;

		for (int i = 0; i < 4; i++)
		{
			const float dx = (float)p.v[i][0];
			const float dy = (float)p.v[i][1];
			const float dz = (float)p.v[i][2];

			vx[i] = mx[0] * dx + mx[1] * dy + mx[2] * dz + mx[3];
			vy[i] = mx[4] * dx + mx[5] * dy + mx[6] * dz + mx[7];
			vz[i] = mx[8] * dx + mx[9] * dy + mx[10] * dz + mx[11];
			x = vx[i];
			y = vy[i];
			z = vz[i];

			if (z <= 0)
				tooNear++;
			else
			{
				z /= g_persp;

				if (z)
				{
					x = x / z + g_centerx;
					y = y / z + g_centery;
				}
				else
				{
					x = x < 0 ? 0.0f : (float)g_winxmax;
					y = y < 0 ? 0.0f : (float)g_winymax;
				}

				if (x - 1 < tL) tL = x - 1;
				if (x + 1 > tR) tR = x + 1;
				if (y - 1 < tT) tT = y - 1;
				if (y + 1 > tB) tB = y + 1;
			}
		}

		if (tooNear == 4)
		{
			Note(fromRoom, rn, "toonear", 4, 0, 0, 0);
			return;
		}

		if (tooNear > 0)
		{
			for (int i = 0, last = 3; i < 4; last = i, i++)
			{
				if ((vz[last] <= 0) == (vz[i] <= 0))
					continue;

				if (vx[i] < 0 && vx[last] < 0)
					tL = 0;
				else if (vx[i] > 0 && vx[last] > 0)
					tR = (float)g_winxmax;
				else
				{
					tL = 0;
					tR = (float)g_winxmax;
				}

				if (vy[i] < 0 && vy[last] < 0)
					tT = 0;
				else if (vy[i] > 0 && vy[last] > 0)
					tB = (float)g_winymax;
				else
				{
					tT = 0;
					tB = (float)g_winymax;
				}
			}
		}

		if (tL < actualRoom.test_left) tL = actualRoom.test_left;
		if (tR > actualRoom.test_right) tR = actualRoom.test_right;
		if (tT < actualRoom.test_top) tT = actualRoom.test_top;
		if (tB > actualRoom.test_bottom) tB = actualRoom.test_bottom;

		if (tL >= tR || tT >= tB)
		{
			Note(fromRoom, rn, "empty", tL, tT, tR, tB);
			return;
		}

		if (r.bound_active & 2)
		{
			if (tL < r.test_left) r.test_left = (short)tL;
			if (tT < r.test_top) r.test_top = (short)tT;
			if (tR > r.test_right) r.test_right = (short)tR;
			if (tB > r.test_bottom) r.test_bottom = (short)tB;
			Note(fromRoom, rn, "widened", tL, tT, tR, tB);
		}
		else
		{
			g_list[g_end % 128] = rn;
			g_end++;
			r.bound_active |= 2;
			r.test_left = (short)tL;
			r.test_right = (short)tR;
			r.test_top = (short)tT;
			r.test_bottom = (short)tB;
			Note(fromRoom, rn, "reached", tL, tT, tR, tB);
		}
	}

	/* GetRoomBounds (game\draw.cpp:2939). ROOM_OUTSIDE and the snow flag are
	 * left out - they change no room list. */
	static void GetRoomBounds()
	{
		while (g_start != g_end)
		{
			const long rn = g_list[g_start % 128];
			float mx[12];

			g_start++;

			Room& r = g_room[(size_t)rn];
			r.bound_active -= 2;

			if (r.test_left < r.left) r.left = r.test_left;
			if (r.test_top < r.top) r.top = r.test_top;
			if (r.test_right > r.right) r.right = r.test_right;
			if (r.test_bottom > r.bottom) r.bottom = r.test_bottom;

			if (!(r.bound_active & 1))
			{
				g_drawn.push_back((short)rn);
				r.bound_active |= 1;
			}

			// phd_TranslateAbs(r->x, r->y, r->z) on top of the eye's rotation.
			{
				const float fx = (float)(r.x - g_eye[0]);
				const float fy = (float)(r.y - g_eye[1]);
				const float fz = (float)(r.z - g_eye[2]);

				for (int row = 0; row < 3; row++)
				{
					mx[row * 4 + 0] = g_view[row * 3 + 0];
					mx[row * 4 + 1] = g_view[row * 3 + 1];
					mx[row * 4 + 2] = g_view[row * 3 + 2];
					mx[row * 4 + 3] = fx * g_view[row * 3 + 0] +
						fy * g_view[row * 3 + 1] + fz * g_view[row * 3 + 2];
				}
			}

			for (size_t d = 0; d < r.doors.size(); d++)
			{
				const Tr5Level::PortalRecord& p = r.doors[d];
				const int to = p.toRoom;

				if (to < 0 || to >= (int)g_room.size())
					continue;

				if (p.nx)
				{
					const long x = r.x + p.v[0][0] - g_eye[0];

					if (x && (x ^ p.nx) < 0)
						SetRoomBounds(p, to, r, mx, (int)rn);
					else
					{
						Note((int)rn, to, "side", (double)x, p.nx, 0, 0);
						continue;
					}
				}

				if (p.ny)
				{
					const long y = r.y + p.v[0][1] - g_eye[1];

					if (y && (y ^ p.ny) < 0)
						SetRoomBounds(p, to, r, mx, (int)rn);
					else
					{
						Note((int)rn, to, "side", (double)y, p.ny, 1, 0);
						continue;
					}
				}

				if (p.nz)
				{
					const long z = r.z + p.v[0][2] - g_eye[2];

					if (z && (z ^ p.nz) < 0)
						SetRoomBounds(p, to, r, mx, (int)rn);
					else
					{
						Note((int)rn, to, "side", (double)z, p.nz, 2, 0);
						continue;
					}
				}
			}
		}
	}

	static bool Load(const std::string& folder, const char* stem)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = folder + "\\" + std::string(stem) + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		if (!level.load(wide, error))
		{
			printf("-walk: could not read %s (%s)\n", trc.c_str(), error.c_str());
			return false;
		}

		g_room.resize((size_t)level.roomCount());

		for (int i = 0; i < level.roomCount(); i++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)i];
			Room& r = g_room[(size_t)i];
			unsigned yOrigin = 0;

			level.headerField(i, 24, yOrigin, error);
			r.x = rm.originX;
			r.y = (long)(int)yOrigin;
			r.z = rm.originZ;
			r.xs = rm.numXSectors;
			r.zs = rm.numZSectors;

			{
				unsigned yTop = 0;

				level.headerField(i, 36, yTop, error);
				r.ceiling = (long)(int)yTop;
				r.floor = rm.yBottom;
			}
			r.flags = rm.flags;
			level.portalRecords(i, r.doors, error);
		}

		printf("-walk: %d rooms out of %s\n", (int)g_room.size(), trc.c_str());
		return true;
	}

	// One frame of the walk. Returns the room list.
	static void One(int seed, long ex, long ey, long ez, double yawDeg,
		double pitchDeg, long width, long height, double fov)
	{
		const double d2r = 3.14159265358979323846 / 180.0;
		const double sy = sin(yawDeg * d2r), cy = cos(yawDeg * d2r);
		const double sp = sin(pitchDeg * d2r), cp = cos(pitchDeg * d2r);

		/* The three rows of the eye's rotation, in the sense LookAt uses
		 * above: yaw 0 looks along +Z and grows towards +X, pitch > 0 looks
		 * down. Right, up-as-y-grows-downwards, forward. */
		g_view[0] = (float)cy;          g_view[1] = 0.0f;       g_view[2] = (float)-sy;
		g_view[3] = (float)(-sy * sp);  g_view[4] = (float)cp;  g_view[5] = (float)(-cy * sp);
		g_view[6] = (float)(sy * cp);   g_view[7] = (float)sp;  g_view[8] = (float)(cy * cp);

		g_winxmax = (short)(width - 1);
		g_winymax = (short)(height - 1);
		g_centerx = (float)(width / 2);
		g_centery = (float)(height / 2);
		g_persp = (float)EnginePersp(height, fov);
		g_eye[0] = ex;
		g_eye[1] = ey;
		g_eye[2] = ez;

		for (size_t i = 0; i < g_room.size(); i++)
		{
			Room& r = g_room[i];

			// What PrintObjects (game\draw.cpp:1087) leaves behind.
			r.left = g_winxmax;
			r.top = g_winymax;
			r.right = 0;
			r.bottom = 0;
			r.bound_active = 0;
		}

		g_verdict.clear();
		g_drawn.clear();
		g_start = 0;
		g_end = 1;
		g_list[0] = seed;

		{
			Room& r = g_room[(size_t)seed];

			r.test_left = 0;
			r.test_top = 0;
			r.test_right = g_winxmax;
			r.test_bottom = g_winymax;
			r.bound_active = 2;
		}

		GetRoomBounds();
	}

	// The distance from a point to a room's box, 0 inside it - the same
	// arithmetic as RoomDistance in game\draw.cpp, which is what the
	// ROOMPOP line measures with.
	static long BoxDistance(const Room& r, long x, long y, long z)
	{
		const long x0 = r.x, x1 = r.x + (long)r.xs * 1024;
		const long z0 = r.z, z1 = r.z + (long)r.zs * 1024;
		const long dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0);
		const long dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0);
		const long dy = y < r.ceiling ? r.ceiling - y
			: (y > r.floor ? y - r.floor : 0);

		return (long)sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
	}

	static bool Holds(int which)
	{
		for (size_t i = 0; i < g_drawn.size(); i++)
			if (g_drawn[i] == which)
				return true;

		return false;
	}

	static void Why(int want)
	{
		if (Holds(want))
		{
			for (size_t i = 0; i < g_verdict.size(); i++)
				if (g_verdict[i].to == want && !strcmp(g_verdict[i].why, "reached"))
				{
					printf("      room %d REACHED from room %d, window %.0f,%.0f..%.0f,%.0f\n",
						want, g_verdict[i].from, g_verdict[i].n[0],
						g_verdict[i].n[1], g_verdict[i].n[2], g_verdict[i].n[3]);
					return;
				}

			printf("      room %d is in the list (seed)\n", want);
			return;
		}

		int seen = 0;

		for (size_t i = 0; i < g_verdict.size(); i++)
			if (g_verdict[i].to == want)
			{
				printf("      room %d REFUSED from room %d by %-8s "
					"[%.0f %.0f %.0f %.0f]\n", want, g_verdict[i].from,
					g_verdict[i].why, g_verdict[i].n[0], g_verdict[i].n[1],
					g_verdict[i].n[2], g_verdict[i].n[3]);
				seen++;
			}

		if (!seen)
			printf("      room %d NOT REACHED - no portal to it out of any "
				"listed room\n", want);
	}
}

/* The whole of -walk, from the command line. Returns the process exit code. */
static int RunWalk(const std::string& folder, const char* stem, int seed,
	long ex, long ey, long ez, const double* yawFan, const double* pitchFan,
	int want, long width, long height, double fov, bool verbose)
{
	if (!walkoff::Load(folder, stem))
		return 5;

	if (seed < 0 || seed >= (int)walkoff::g_room.size())
	{
		printf("-walk: room %d is not in this level\n", seed);
		return 5;
	}

	printf("-walk: rooms whose box holds the eye %ld/%ld/%ld:", ex, ey, ez);

	for (size_t i = 0; i < walkoff::g_room.size(); i++)
	{
		const walkoff::Room& r = walkoff::g_room[i];

		if (ex >= r.x && ex <= r.x + (long)r.xs * 1024 &&
			ez >= r.z && ez <= r.z + (long)r.zs * 1024)
			printf(" %d", (int)i);
	}

	printf("\n");

	printf("-walk: %s seed room %d, eye %ld/%ld/%ld, %ldx%ld, fov %.0f\n",
		stem, seed, ex, ey, ez, width, height, fov);

	std::vector<int> tally((size_t)walkoff::g_room.size(), 0);
	int frames = 0, wantIn = 0;
	int mostRooms = 0, fewestRooms = 1 << 30;

	/* T3-2: what became of the wanted room over the whole fan. The
	 * widest window it ever gets is the number that matters - a room
	 * that only ever enters the picture as a sliver a few pixels wide
	 * at the rim is a room whose coming and going costs those pixels
	 * and nothing else. */
	int whyCount[6] = { 0, 0, 0, 0, 0, 0 };
	double widest = 0, tallest = 0, narrowest = 1e9, shortest = 1e9;

	for (double pitch = pitchFan[0]; pitch <= pitchFan[1] + 1e-9;
		pitch += pitchFan[2] > 0 ? pitchFan[2] : 1.0)
	for (double yaw = yawFan[0]; yaw <= yawFan[1] + 1e-9; yaw += yawFan[2])
	{
		walkoff::One(seed, ex, ey, ez, yaw, pitch, width, height, fov);
		frames++;

		if ((int)walkoff::g_drawn.size() > mostRooms)
			mostRooms = (int)walkoff::g_drawn.size();

		if ((int)walkoff::g_drawn.size() < fewestRooms)
			fewestRooms = (int)walkoff::g_drawn.size();

		for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
			tally[(size_t)walkoff::g_drawn[i]]++;

		if (want >= 0)
		{
			if (walkoff::Holds(want))
				wantIn++;

			for (size_t i = 0; i < walkoff::g_verdict.size(); i++)
			{
				const walkoff::Verdict& v = walkoff::g_verdict[i];

				if (v.to != want)
					continue;

				if (!strcmp(v.why, "reached"))
				{
					const double w = v.n[2] - v.n[0];
					const double h = v.n[3] - v.n[1];

					whyCount[0]++;

					if (w > widest) widest = w;
					if (h > tallest) tallest = h;
					if (w < narrowest) narrowest = w;
					if (h < shortest) shortest = h;
				}
				else if (!strcmp(v.why, "side"))     whyCount[1]++;
				else if (!strcmp(v.why, "toonear"))  whyCount[2]++;
				else if (!strcmp(v.why, "empty"))    whyCount[3]++;
				else if (!strcmp(v.why, "covered"))  whyCount[4]++;
				else                                 whyCount[5]++;
			}
		}

		if (verbose)
		{
			printf("  yaw %6.1f  rooms=%d:", yaw, (int)walkoff::g_drawn.size());

			for (size_t i = 0; i < walkoff::g_drawn.size(); i++)
				printf(" %d", (int)walkoff::g_drawn[i]);

			printf("\n");

			if (want >= 0)
				walkoff::Why(want);
		}
	}

	printf("-walk: %d view directions, rooms per frame %d..%d\n",
		frames, fewestRooms, mostRooms);

	if (want >= 0)
	{
		printf("-walk: room %d is in the list on %d of %d directions (%.0f%%)\n",
			want, wantIn, frames, frames ? 100.0 * wantIn / frames : 0.0);
		printf("-walk: room %d  reached=%d side=%d toonear=%d empty=%d "
			"covered=%d widened=%d\n", want, whyCount[0], whyCount[1],
			whyCount[2], whyCount[3], whyCount[4], whyCount[5]);

		if (whyCount[0])
			printf("-walk: room %d window when it IS reached: width %.0f..%.0f "
				"of %ld, height %.0f..%.0f of %ld\n", want, narrowest, widest,
				width, shortest, tallest, height);
	}

	/* T3-3: the rooms the walk reaches, and the rooms it never reaches -
	 * with the distance from the eye and the ROOM_OUTSIDE flag beside
	 * each. The second list is what a "draw the near outside rooms
	 * anyway" rule would be adding, so its length IS that rule's cost. */
	int outNear = 0, outAll = 0, neverNear = 0;

	printf("-walk: how often each room is in the list\n");

	for (size_t i = 0; i < tally.size(); i++)
	{
		const walkoff::Room& r = walkoff::g_room[i];
		const long d = walkoff::BoxDistance(r, ex, ey, ez);
		const bool out = (r.flags & 0x8) != 0;

		if (out)
			outAll++;

		if (out && d <= 16384)
			outNear++;

		if (tally[i])
			printf("    room %3d  %4d of %d   dist %6ld  %s\n", (int)i,
				tally[i], frames, d, out ? "OUTSIDE" : "");
		else if (d <= 16384)
			neverNear++;
	}

	printf("-walk: rooms never reached on any direction and within 16 sectors: %d\n", neverNear);
	printf("-walk: ROOM_OUTSIDE rooms: %d of %d in the level, %d of them "
		"within 16 sectors of this eye\n", outAll, (int)tally.size(), outNear);

	for (size_t i = 0; i < tally.size(); i++)
	{
		const walkoff::Room& r = walkoff::g_room[i];
		const long d = walkoff::BoxDistance(r, ex, ey, ez);

		if (!tally[i] && d <= 16384)
			printf("    never  room %3d  dist %6ld  %s\n", (int)i, d,
				(r.flags & 0x8) ? "OUTSIDE" : "");
	}

	return 0;
}

static int CountIntrusionPairs()
{
	int n = 0;

	for (size_t i = 0; i < g_intrusions_built.size(); i++)
		if (HDRenderIntrusionHits((int)i))
			n++;

	return n;
}

static void ClipFor(int group)
{
	if (g_clip_group >= 0 && group == g_clip_group)
		HDRenderScissor((int)g_clip_box[0], (int)g_clip_box[1],
			(int)g_clip_box[2], (int)g_clip_box[3]);
	else if (g_clip_group >= 0)
		HDRenderScissorOff();
}

/* ONE GROUP, DRAWN AS THE ENGINE WOULD DRAW IT. Ordinarily one call with
 * whatever -window asked for; with -tround naming this group, the two calls
 * fix round T's DrawRoomWindowed made. */
static bool DrawOneGroup(int group, const D3DMATRIX* world,
	const HDRoomLighting* lighting, unsigned int passes)
{
	if (group == g_tround_group)
	{
		HDRenderScissor((int)g_tround_box[0], (int)g_tround_box[1],
			(int)g_tround_box[2], (int)g_tround_box[3]);

		const bool drew = HDRenderRoomMask(group, world, lighting, passes,
			HD_WINDOW_INSIDE);

		HDRenderScissorOff();
		HDRenderRoomMask(group, world, lighting, passes, HD_WINDOW_OVERHANG);
		return drew;
	}

	ClipFor(group);
	return HDRenderRoomMask(group, world, lighting, passes, g_window_mode);
}
static unsigned char g_water_table[22][64][4];

/* Fix round E, -with and -oldorder: THE NEIGHBOUR ROOMS OF ONE SCENE.
 *
 * A glow that writes no depth can only be shown to be eaten by a room drawn
 * after it if there IS a room drawn after it, and until fix round E this tool
 * drew exactly one group. -with names a further group and the world offset it
 * stands at - the difference of the two classic room origins, which RoomCheck
 * prints as "x .. z .." per room - and the scene is then drawn the way the
 * engine draws it: HDFrameDraw's order, out of hd_render's own entry points,
 * so this is the engine's order and not a copy of it.
 *
 * -oldorder puts back the order of stages S1-2 to R3b, room-major: all three
 * passes of the first room, then all three of the second. The two switches
 * out of ONE build are the before and after picture of E6. */
static int g_with_group[8];
static float g_with_offset[8][3];
static int g_with_count = 0;
static bool g_old_order = false;

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
	frame.reflect = g_reflect ? 1 : 0;

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

	/* -plane and -roomplane STAND IN FOR A REAL WATER ROOM (#123, fix round
	 * E). The engine sets three things together for one, and the tool used to
	 * set only the marks: the room's surface height, which is the plane's own
	 * y, and the selector that says which mark drives the shimmer -
	 * FillRoomLighting's sel1[1] with its height gate for a plain water room,
	 * sel1[2] with none for a ROOM_REFLECT + ROOM_REFLECT_CEILING one. Without
	 * the height the shader's atSurface is never true and the swell of fix
	 * round R3b cannot run at all, which is a picture the engine never
	 * draws. */
	if (g_plane)
	{
		room.surfaceY = (float)g_plane_marks[0].y;

		if (g_plane_marks[0].bits & 4)
			room.sel1[2] = 1.0f;
		else
		{
			room.sel1[1] = 1.0f;
			room.surfaceGate = 1;
		}
	}
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
/* THE HD PAGES OF THE CLASSIC WATER FRAMES (#123, fix round N, N2).
 *
 * The classic engine animates a water surface by ROTATING the contents of a
 * range of texinfos once every six game steps (specific/output.cpp,
 * S_AnimateTextures): the face keeps its texinfo number and the tile it shows
 * walks along the range. So the frames of one water surface are the tiles of
 * the range its texinfo belongs to, and the question this answers is what the
 * remaster paints each of those tiles with.
 *
 * `tiles` is the set of texinfo numbers the restored water faces name. Prints
 * one block per range that carries one of them and one summary line, which is
 * the same line the engine writes to HDLOG at level build.
 *
 * ONE THING THE ENGINE DOES THAT THIS DOES NOT, and it is named rather than
 * silently absent: `BuildWaterFrames` skips the first `nAnimUVRanges` ranges
 * while `gfUVRotate` is non-zero, because those are the level's waterfalls -
 * the engine scrolls their v instead of rotating them, and a scrolled v walks
 * out of the cell the HD page paints. This tool has no gameflow and cannot ask
 * `gfUVRotate`, so it does not skip anything. It cannot bite on the shipped
 * game: `gfUVRotate` is the script's `UVROT` command, and in both the vanilla
 * `SCRIPT.DAT` and a randomized one it is 0 for every level with a restored
 * water face (ANDREA1, ANDREA2, ANDY1, ANDY3, JOBY2, JOBY4, JOBY5) and
 * non-zero only for TOM (3) and JOBY3 (8), neither of which restores one. On a
 * level where it were not 0, this tool would list a range the engine drops.
 */
static void WaterFrames(const Tr5Level& level, const char* install,
	const char* stem, const std::vector<int>& tiles)
{
	size_t texAt = 0, animAt = 0;
	int texCount = 0, animShorts = 0;

	if (!level.debugTexinfoBlock(texAt, texCount) ||
		!level.debugAnimTexBlock(animAt, animShorts))
	{
		printf("  no texinfo or animated texture block in %s\n", stem);
		return;
	}

	const std::vector<unsigned char>& raw = level.debugBytes();
	std::vector<unsigned short> hd((size_t)texCount, 0xFFFFu);
	const int got = HDScanLevelTextures(install, stem, &hd[0], texCount);

	printf("\n-- the water frames (N2) ------------------------------------\n");
	printf("  %s: %d texinfo(s), the .TEX names %d of them\n",
		stem, texCount, got);

	/* THE PER-CELL RULE, checked on this level rather than believed. TRForge
	 * measured over the shipped games that every tile with HD art lies wholly
	 * inside one 64x64 cell and that no cell ever wants two different files;
	 * the substitution rests on both, so both are counted here. */
	{
		std::vector<int> key, page;
		int crossing = 0, conflict = 0, withArt = 0;

		for (int t = 0; t < texCount && t < got; t++)
		{
			if (hd[(size_t)t] == 0xFFFF)
				continue;

			const size_t o = texAt + (size_t)t * 40;
			unsigned short tp = 0;
			int u[4], v[4];

			memcpy(&tp, &raw[o + 2], 2);

			for (int c = 0; c < 4; c++)
			{
				unsigned short a = 0, b = 0;

				memcpy(&a, &raw[o + 6 + (size_t)c * 4], 2);
				memcpy(&b, &raw[o + 8 + (size_t)c * 4], 2);
				u[c] = a >> 8;
				v[c] = b >> 8;
			}

			/* TRIANGLE OR QUAD, AND WHICH WORD SAYS SO. In the FILE the bit
			 * sits at the top of the TileAndFlag word (+2, the one this reads
			 * as `tp`), not at the top of the NewFlags word (+4, `fl`); the
			 * runtime TEXTURESTRUCT is the other way round, because
			 * LoadTextureInfos builds its `flag` as the low 15 bits of the
			 * file's flag word with bit 15 of the file's tpage word on top
			 * (`tex.tpage ^ (tex.tpage ^ tex.flag) & 0x7FFF`,
			 * specific/file.cpp), and AdjustUV then tests `flag & 0x8000`. So
			 * the engine side of this round is right and this line has to
			 * read `tp`. The two bits disagree often - JOBY2 has 1,052 tiles
			 * with the one and 400 with the other - and the counts below come
			 * out the same either way, but a proof tool has no business being
			 * accidentally right.
			 *
			 * A triangle's fourth corner is not a corner at all: AdjustUV
			 * touches three of them and the draw reads three. Counting the
			 * fourth turned 127 of JOBY2's 382 tiles into cell crossers that
			 * are nothing of the kind. */
			const int corners = (tp & 0x8000) ? 3 : 4;
			int u0 = u[0], u1 = u[0], v0 = v[0], v1 = v[0];

			for (int c = 1; c < corners; c++)
			{
				if (u[c] < u0) u0 = u[c];
				if (u[c] > u1) u1 = u[c];
				if (v[c] < v0) v0 = v[c];
				if (v[c] > v1) v1 = v[c];
			}

			withArt++;

			if (u0 / 64 != (u1 - 1) / 64 || v0 / 64 != (v1 - 1) / 64)
			{
				crossing++;
				continue;
			}

			const int k = ((tp & 0x7FFF) << 4) | ((v0 / 64) << 2) | (u0 / 64);
			bool seen = false;

			for (size_t i = 0; i < key.size(); i++)
				if (key[i] == k)
				{
					seen = true;

					if (page[i] != (int)hd[(size_t)t])
						conflict++;
				}

			if (!seen)
			{
				key.push_back(k);
				page.push_back((int)hd[(size_t)t]);
			}
		}

		printf("  the cell rule: %d tile(s) with HD art in %d cell(s), "
			"%d crossing a cell edge, %d cell(s) wanting two files\n",
			withArt, (int)key.size(), crossing, conflict);
	}

	if (!got)
		return;

	// One short: the number of ranges. Then per range one short with the
	// number of frames MINUS ONE, and that many plus one texinfo numbers.
	std::vector<short> anim((size_t)animShorts);

	for (int i = 0; i < animShorts; i++)
	{
		short v = 0;
		memcpy(&v, &raw[animAt + (size_t)i * 2], 2);
		anim[(size_t)i] = v;
	}

	int at = 0;
	const int ranges = animShorts ? anim[0] : 0;
	int waterRanges = 0, waterFrames = 0, missing = 0;
	std::vector<int> pages;

	at = 1;

	for (int r = 0; r < ranges && at < animShorts; r++)
	{
		const int frames = anim[(size_t)at] + 1;	// the count is n - 1
		const int first = at + 1;

		at = first + frames;

		if (at > animShorts)
			break;

		bool wanted = false;

		for (int f = 0; f < frames && !wanted; f++)
			for (size_t w = 0; w < tiles.size(); w++)
				if (tiles[w] == anim[(size_t)(first + f)])
				{
					wanted = true;
					break;
				}

		if (!wanted)
			continue;

		waterRanges++;
		printf("  range %2d: %d frame(s)\n", r, frames);

		// N2: the HD page of each frame, in the range's own order, for the
		// rotation below.
		std::vector<int> framePage((size_t)frames, -1);

		for (int f = 0; f < frames; f++)
		{
			const int tile = anim[(size_t)(first + f)];

			if (tile < 0 || tile >= texCount)
				continue;

			const size_t o = texAt + (size_t)tile * 40;
			unsigned short tp = 0;
			int tu[4], tv[4];

			memcpy(&tp, &raw[o + 2], 2);

			for (int c = 0; c < 4; c++)
			{
				unsigned short a = 0, b = 0;

				memcpy(&a, &raw[o + 6 + (size_t)c * 4], 2);
				memcpy(&b, &raw[o + 8 + (size_t)c * 4], 2);
				tu[c] = a >> 8;
				tv[c] = b >> 8;
			}

			// Bit 15 of TileAndFlag (+2), not of NewFlags (+4) - see the note
			// on the same line in the cell rule above.
			const int corners = (tp & 0x8000) ? 3 : 4;
			int ru0 = tu[0], ru1 = tu[0], rv0 = tv[0], rv1 = tv[0];

			for (int c = 1; c < corners; c++)
			{
				if (tu[c] < ru0) ru0 = tu[c];
				if (tu[c] > ru1) ru1 = tu[c];
				if (tv[c] < rv0) rv0 = tv[c];
				if (tv[c] > rv1) rv1 = tv[c];
			}

			const unsigned short u1 = (unsigned short)(ru0 << 8);
			const unsigned short v1 = (unsigned short)(rv0 << 8);

			const int px = (u1 >> 8), py = (v1 >> 8);
			const int page = hd[(size_t)tile];

			waterFrames++;

			if (page == 0xFFFF)
				missing++;
			else
			{
				bool seen = false;

				for (size_t i = 0; i < pages.size(); i++)
					if (pages[i] == page)
						seen = true;

				if (!seen)
					pages.push_back(page);
			}

			/* The rectangle the tile occupies inside its HD page: the cell
			 * is painted at eight times the size, so a classic pixel at
			 * (px % 64) sits at eight times that in the 512 file. This is
			 * the same arithmetic HDWaterFrameFor does in floats. */
			printf("    frame %d: tile %4d on page %3d at (%3d,%3d)..(%3d,%3d), "
				"cell (%d,%d)  ->  %s%d, HD rect (%d,%d) %dx%d\n",
				f, tile, tp & 0x7FFF, ru0, rv0, ru1, rv1, px / 64, py / 64,
				page == 0xFFFF ? "no HD page for tile " : "", page,
				(ru0 % 64) * 8, (rv0 % 64) * 8,
				(ru1 - ru0 + 1) * 8, (rv1 - rv0 + 1) * 8);
			framePage[(size_t)f] = page;
		}

		/* AND THE RUNTIME HALF, run rather than believed. The engine does not
		 * know which frame a texinfo is showing: it matches the CONTENT that
		 * is in it right now against the frames it recorded at load
		 * (hd_level.cpp, HDWaterFrameFor). So the classic rotation is
		 * simulated here - textinfo[range[0]] = textinfo[range[1]] all the
		 * way along and the first one round to the end, which is
		 * S_AnimateTextures' own loop - and after every step the frame that
		 * the range's FIRST texinfo now holds is read off. It has to walk the
		 * range in order and come back round after `frames` steps. */
		{
			std::vector<int> content((size_t)frames);
			bool ok = true;

			for (int i = 0; i < frames; i++)
				content[(size_t)i] = i;

			printf("      the rotation: ");

			for (int step = 0; step < frames * 2 + 1; step++)
			{
				const int show = content[0];
				const int want = step % frames;

				printf("%s%d", step ? " " : "", framePage[(size_t)show]);

				if (show != want)
					ok = false;

				const int keep = content[0];

				for (int i = 0; i + 1 < frames; i++)
					content[(size_t)i] = content[(size_t)(i + 1)];

				content[(size_t)(frames - 1)] = keep;
			}

			printf("  ->  %s\n", ok ? "walks the range and comes back round"
				: "OUT OF STEP");
		}
	}

	std::string list;

	for (size_t i = 0; i < pages.size(); i++)
	{
		char one[32];

		sprintf(one, "%s%d", i ? "," : "", pages[i]);
		list += one;
	}

	printf("  HDLOG level=%s water ranges=%d frames=%d hd pages=%s missing=%d\n",
		stem, waterRanges, waterFrames,
		list.empty() ? "none" : list.c_str(), missing);
}

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
		printf("       -with <group> <dx dy dz>   -oldorder   -black   -noshore\n");
		printf("       -scissor <l,t,r,b>\n");
		printf("       -roombox <folder holding the classic .TRC>\n");
		printf("       -window all|open|intruding   (needs -roombox)\n");
		printf("       -clipgroup <group> <l,t,r,b>   one group's own window\n");
		printf("       -tround <group> <l,t,r,b>      round T's own picture\n");
		printf("       -walk <seed room> <x y z>      the classic portal walk,\n");
		printf("                                      offline (needs -roombox)\n");
		printf("       -walkyaw <from> <to> <step>   default 0 359 15\n");
		printf("       -walkpitch <deg>  -walkpitchfan <from> <to> <step>\n");
		printf("       -walkwant <room>  -walkeach\n");
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

	// T3-2: the portal walk offline. A seed of -1 means the switch was
	// not given and nothing below changes.
	int walkSeed = -1, walkWant = -1;
	long walkEye[3] = { 0, 0, 0 };
	double walkYaw[3] = { 0.0, 359.0, 15.0 };
	double walkPitch[3] = { 0.0, 0.0, 1.0 };
	bool walkEach = false;
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
		else if ((!strcmp(argv[at], "-plane") ||
			!strcmp(argv[at], "-roomplane")) && at + 5 < argc)
		{
			const short y = (short)atoi(argv[at + 1]);
			const short x0 = (short)atoi(argv[at + 2]);
			const short z0 = (short)atoi(argv[at + 3]);
			const short x1 = (short)atoi(argv[at + 4]);
			const short z1 = (short)atoi(argv[at + 5]);
			/* -roomplane is the OTHER shimmer bit (#123, fix round E, E2):
			 * the ROOM_REFLECT + ROOM_REFLECT_CEILING answer, which is what
			 * the Rome levels' water carries and what fix round R3b did not
			 * flood. */
			const unsigned char bits =
				argv[at][1] == 'r' ? (unsigned char)4 : (unsigned char)2;

			for (int c = 0; c < 4; c++)
			{
				g_plane_marks[c].room = (short)room;
				g_plane_marks[c].y = y;
				g_plane_marks[c].x = (c & 1) ? x1 : x0;
				g_plane_marks[c].z = (c & 2) ? z1 : z0;
				g_plane_marks[c].bits = bits;
			}

			g_plane = true;
			at += 5;
		}
		else if (!strcmp(argv[at], "-scissor") && at + 1 < argc)
		{
			/* GROUP M: "l,t,r,b" in backbuffer pixels, left and top the first
			 * pixel drawn and right and bottom the first one that is not -
			 * the same half-open rectangle HDRenderScissor takes and the same
			 * one the engine hands it. */
			if (sscanf(argv[at + 1], "%ld,%ld,%ld,%ld", &g_scissor_box[0],
				&g_scissor_box[1], &g_scissor_box[2], &g_scissor_box[3]) == 4)
				g_scissor = true;
			else
				printf("RoomProbe: -scissor wants l,t,r,b with commas and no "
					"spaces - \"%s\" is not that\n", argv[at + 1]);

			at += 1;
		}
		else if (!strcmp(argv[at], "-nomotion"))
			g_motion = false;
		else if (!strcmp(argv[at], "-black"))
			g_black = true;
		else if (!strcmp(argv[at], "-noshore"))
			HDRenderShoreOverride(1);
		else if (!strcmp(argv[at], "-noblend"))
			HDRenderBlendOverride(1);
		else if (!strcmp(argv[at], "-noupright"))
			HDRenderUprightOverride(1);
		else if (!strcmp(argv[at], "-noreflect"))
			g_reflect = false;
		else if (!strcmp(argv[at], "-shatter"))
			g_shatter = true;
		else if (!strcmp(argv[at], "-with") && at + 4 < argc)
		{
			if (g_with_count < 8)
			{
				g_with_group[g_with_count] = atoi(argv[at + 1]);

				for (int c = 0; c < 3; c++)
					g_with_offset[g_with_count][c] = (float)atof(argv[at + 2 + c]);

				g_with_count++;
			}

			at += 4;
		}
		else if (!strcmp(argv[at], "-oldorder"))
			g_old_order = true;
		else if (!strcmp(argv[at], "-watersurvey") && at + 1 < argc)
		{
			g_survey = argv[at + 1];
			at += 1;
		}
		else if (!strcmp(argv[at], "-roombox") && at + 1 < argc)
		{
			g_roombox = argv[at + 1];
			at += 1;
		}
		else if (!strcmp(argv[at], "-walk") && at + 4 < argc)
		{
			walkSeed = atoi(argv[at + 1]);
			walkEye[0] = atol(argv[at + 2]);
			walkEye[1] = atol(argv[at + 3]);
			walkEye[2] = atol(argv[at + 4]);
			at += 4;
		}
		else if (!strcmp(argv[at], "-walkyaw") && at + 3 < argc)
		{
			walkYaw[0] = atof(argv[at + 1]);
			walkYaw[1] = atof(argv[at + 2]);
			walkYaw[2] = atof(argv[at + 3]);
			at += 3;
		}
		else if (!strcmp(argv[at], "-walkpitch") && at + 1 < argc)
		{
			walkPitch[0] = walkPitch[1] = atof(argv[++at]);
			walkPitch[2] = 1.0;
		}
		else if (!strcmp(argv[at], "-walkpitchfan") && at + 3 < argc)
		{
			walkPitch[0] = atof(argv[at + 1]);
			walkPitch[1] = atof(argv[at + 2]);
			walkPitch[2] = atof(argv[at + 3]);
			at += 3;
		}
		else if (!strcmp(argv[at], "-walkwant") && at + 1 < argc)
			walkWant = atoi(argv[++at]);
		else if (!strcmp(argv[at], "-walkeach"))
			walkEach = true;
		else if (!strcmp(argv[at], "-tround") && at + 2 < argc)
		{
			g_tround_group = atoi(argv[at + 1]);
			sscanf(argv[at + 2], "%ld,%ld,%ld,%ld", &g_tround_box[0],
				&g_tround_box[1], &g_tround_box[2], &g_tround_box[3]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-clipgroup") && at + 2 < argc)
		{
			g_clip_group = atoi(argv[at + 1]);
			sscanf(argv[at + 2], "%ld,%ld,%ld,%ld", &g_clip_box[0],
				&g_clip_box[1], &g_clip_box[2], &g_clip_box[3]);
			at += 2;
		}
		else if (!strcmp(argv[at], "-window") && at + 1 < argc)
		{
			g_window_mode = !strcmp(argv[at + 1], "open") ? HD_WINDOW_OPEN
				: !strcmp(argv[at + 1], "intruding") ? HD_WINDOW_INTRUDING
				: HD_WINDOW_ALL;
			at += 1;
		}
		else if (!strcmp(argv[at], "-flatart") && at + 5 < argc)
		{
			if (g_flat_count < 8)
			{
				for (int c = 0; c < 5; c++)
					g_flat_box[g_flat_count][c] = atoi(argv[at + 1 + c]);

				g_flat_count++;
			}

			at += 5;
		}
		else if (!strcmp(argv[at], "-hasgeom") && at + 6 < argc)
		{
			if (g_geom_count < 8)
			{
				for (int c = 0; c < 6; c++)
					g_geom_box[g_geom_count][c] = (float)atof(argv[at + 1 + c]);

				g_geom_count++;
			}

			at += 6;
		}
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

	/* T3-2: -walk needs no device, no .TRG and no picture - only the
	 * classic level - so it runs here, before the window exists. */
	if (walkSeed >= 0)
	{
		if (g_roombox.empty())
		{
			printf("-walk needs -roombox <folder holding the classic .TRC>\n");
			return 5;
		}

		return RunWalk(g_roombox, stem, walkSeed, walkEye[0], walkEye[1],
			walkEye[2], walkYaw, walkPitch,
			walkWant, width, height, fov, walkEach);
	}

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

	// R3b: the synthetic surface marks of -plane, borrowed for the length
	// of the build exactly as the engine lends its own.
	if (g_plane)
		HDRenderSetWaterMarks(g_plane_marks, 4);

	// T2-2: -tround needs round T's own index layout, and nothing else does.
	if (g_tround_group >= 0)
		HDRenderSplitOverhang(1);

	/* T1: the rooms' own boxes out of the classic level, the same three
	 * lines game\draw.cpp:1977-1981 uses and hd_level.cpp copies. */
	if (!g_roombox.empty())
	{
		Tr5Level level;
		std::string error;
		const std::string trc = g_roombox + "\\" + stem + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		if (!level.load(wide, error))
			printf("RoomProbe: -roombox could not read %s (%s)\n",
				trc.c_str(), error.c_str());
		else
		{
			g_boxes_built.resize((size_t)level.roomCount());

			for (int i = 0; i < level.roomCount(); i++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)i];
				HDRoomBox& b = g_boxes_built[(size_t)i];
				unsigned yTop = 0, yOrigin = 0;

				level.headerField(i, 24, yOrigin, error);
				level.headerField(i, 36, yTop, error);

				b.mins[0] = 1024.0f;
				b.maxs[0] = (float)((rm.numXSectors - 1) * 1024);
				b.mins[1] = (float)((int)yTop - (int)yOrigin);
				b.maxs[1] = (float)(rm.yBottom - (int)yOrigin);
				b.mins[2] = 1024.0f;
				b.maxs[2] = (float)((rm.numZSectors - 1) * 1024);

				for (int axis = 0; axis < 3; axis++)
					if (b.maxs[axis] < b.mins[axis])
						b.maxs[axis] = b.mins[axis];
			}

			HDRenderSetRoomBoxes(&g_boxes_built[0],
				(int)g_boxes_built.size());
			printf("-roombox: %d room boxes handed in from %s\n",
				(int)g_boxes_built.size(), trc.c_str());

			/* T2-2: and the rooms that may hold another room's art. The rule
			 * is BuildRoomIntrusions' (specific\hd\hd_level.cpp), read out
			 * of the same file: not next door by portal or by a vertical
			 * floor link, not a flipmap twin, boxes touching, and the
			 * holder's own inside one sector in from its border ring. */
			{
				const int n = level.roomCount();
				std::vector<std::set<int> > nextDoor((size_t)n);

				for (int r = 0; r < n; r++)
				{
					const std::vector<int>& ports =
						level.rooms()[(size_t)r].portalRooms;

					for (size_t p = 0; p < ports.size(); p++)
						if (ports[p] >= 0 && ports[p] < n)
						{
							nextDoor[(size_t)r].insert(ports[p]);
							nextDoor[(size_t)ports[p]].insert(r);
						}

					std::vector<Tr5Level::SectorInfo> sect;

					if (!level.sectorInfo(r, sect, error))
						continue;

					for (size_t q = 0; q < sect.size(); q++)
					{
						const int up = sect[q].roomAbove;
						const int dn = sect[q].roomBelow;

						if (up >= 0 && up < n)
						{
							nextDoor[(size_t)r].insert(up);
							nextDoor[(size_t)up].insert(r);
						}

						if (dn >= 0 && dn < n)
						{
							nextDoor[(size_t)r].insert(dn);
							nextDoor[(size_t)dn].insert(r);
						}
					}
				}

				for (int a = 0; a < n; a++)
				{
					const Tr5Room& ra = level.rooms()[(size_t)a];
					unsigned aTop = 0, aOrigin = 0;

					level.headerField(a, 24, aOrigin, error);
					level.headerField(a, 36, aTop, error);

					for (int b = 0; b < n; b++)
					{
						if (a == b || nextDoor[(size_t)a].count(b))
							continue;

						if (ra.altRoom == b ||
							level.rooms()[(size_t)b].altRoom == a)
							continue;

						const Tr5Room& rb = level.rooms()[(size_t)b];
						unsigned bTop = 0, bOrigin = 0;

						level.headerField(b, 24, bOrigin, error);
						level.headerField(b, 36, bTop, error);

						if (ra.originX + (long)ra.numXSectors * 1024 <= rb.originX ||
							rb.originX + (long)rb.numXSectors * 1024 <= ra.originX ||
							ra.originZ + (long)ra.numZSectors * 1024 <= rb.originZ ||
							rb.originZ + (long)rb.numZSectors * 1024 <= ra.originZ ||
							ra.yBottom <= (long)(int)bTop ||
							rb.yBottom <= (long)(int)aTop)
							continue;

						HDIntrusion box;

						box.group = b;
						box.holder = a;
						box.mins[0] = (float)(ra.originX + 2048 - rb.originX);
						box.maxs[0] = (float)(ra.originX +
							(long)(ra.numXSectors - 2) * 1024 - rb.originX);
						box.mins[1] = (float)((long)(int)aTop - (long)(int)bOrigin);
						box.maxs[1] = (float)(ra.yBottom - (long)(int)bOrigin);
						box.mins[2] = (float)(ra.originZ + 2048 - rb.originZ);
						box.maxs[2] = (float)(ra.originZ +
							(long)(ra.numZSectors - 2) * 1024 - rb.originZ);

						if (box.maxs[0] <= box.mins[0] ||
							box.maxs[2] <= box.mins[2])
							continue;

						g_intrusions_built.push_back(box);
					}
				}

				if (!g_intrusions_built.empty())
					HDRenderSetIntrusions(&g_intrusions_built[0],
						(int)g_intrusions_built.size());

				printf("-roombox: %d intrusion boxes handed in\n",
					(int)g_intrusions_built.size());
			}
		}
	}

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
	/* T2-2: THE OVERLAP LIST OUT OF THE ENGINE'S OWN BUILD, so that it can be
	 * held line for line against TrgProbe -roomspace, which answers the same
	 * question out of the two files without a renderer. The two lists have to
	 * be the same list; that they were not is what round T's log and round
	 * T's tool disagreed about. */
	if (!g_intrusions_built.empty())
	{
		printf("T2-2: overlap pairs=%d intruding tris=%d in %d groups\n",
			CountIntrusionPairs(), stats.intrudingTris, stats.intrudingGroups);

		for (size_t i = 0; i < g_intrusions_built.size(); i++)
		{
			const int hits = HDRenderIntrusionHits((int)i);

			if (hits)
				printf("  overlap: room %3d holds the art of room %3d - "
					"%d triangles, no portal between them\n",
					g_intrusions_built[i].holder,
					g_intrusions_built[i].group, hits);
		}
	}
	printf("S6: widest draw call reaches over %d vertices, widest single "
		"triangle %d (a 16 bit index reaches 65536), device holds textures "
		"up to %d\n",
		stats.indexWindow, stats.triangleWindow, stats.maxTextureSide);
	printf("S5: %d scrolling part(s) in %d draw calls (%d triangles), "
		"%d vertices with a wind weight\n",
		stats.scrollParts, stats.scrollRanges, stats.scrollTris,
		stats.swayVertices);
	// Fix round R3b: material 13 is drawn now - four pages, one per frame
	// time, the part's own page first. `kept` is the parts whose numbers
	// did not resolve and that stand still, as every one of them did before.
	printf("R3b: %d flipbook part(s), %d of them running in %d draw call(s), "
		"%d kept their own page\n",
		stats.flipbookParts, stats.flipbookDrawn, stats.flipbookRanges,
		stats.flipbookDropped);
	// And the water side of the same round: how many HD vertices carry a
	// surface mark, and how many of those the corner rule of stage S2 never
	// found and the plane flood answered for.
	printf("R3b/E2: %d water mark(s) stamped, %d on the surface, %d on the whole "
		"room, %d of them flooded, %d plane(s) dropped\n",
		stats.markedVertices, stats.markedShimmer, stats.markedRoomShimmer,
		stats.markedFlooded, stats.waterPlanesDropped);
	/* H4: and how many of the marked vertices are not a flat surface at all -
	 * a wall or a pillar standing at the water's own height. The geometry,
	 * the UV wobble and the caustic leave those alone now; the colour term
	 * still has them, as the classic path does. */
	printf("H4: %d marked vertex/vertices whose normal is not vertical "
		"(the wall at the waterline)\n", stats.markedWall);
	// And the gloss: the opaque parts of materials 3 and 17 whose own
	// numbers gave them one (fix round R3b).
	printf("R3b: %d glossy part(s) (materials 3 and 17)\n",
		stats.glossParts);

	/* R3b: the destructible pieces, one line each, with the room they
	 * stand in and their box - the numbers hd_level.cpp pairs against the
	 * classic MESH_INFO list. -shatter then hides the drawn room's. */
	for (int i = 0; i < g_geom_count; i++)
	{
		const float* b = g_geom_box[i];
		const float mins[3] = { b[0], b[1], b[2] };
		const float maxs[3] = { b[3], b[4], b[5] };

		printf("R3b: group %d, box %.0f..%.0f %.0f..%.0f %.0f..%.0f - geometry: %s\n",
			room, mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2],
			HDRenderGroupHasGeometry(room, mins, maxs) ? "YES (baked, stays suppressed)" : "NO (drawn the classic way)");
	}

	/* H1: the horizontal planes of the drawn group, and the answer the
	 * water-surface gate gives for a plane one names. A NO is a plane the
	 * remaster left empty - there the classic water face is drawn on top of
	 * the HD room (hd_level.cpp, BuildWaterSurfaces). */
	printf("H1: %d horizontal plane(s) of art in group %d (%d in the level)\n",
		HDRenderGroupFlatPlanes(room), room, stats.flatPlanes);
	printf("H2: %d see-through part(s) in the alpha pass (material 14), "
		"%d draw call(s)\n", stats.blendParts, stats.blendRanges);

	// N1: the still water surfaces the .TRG builds itself - the same line
	// the engine writes to HDLOG at level build.
	if (stats.waterParts)
		printf("N1: basin parts=%d (%d additive, %d opaque, %d still), "
			"triangles=%d, sub-rect tiles=%d, smallest UV span=%d/255, "
			"ripple capped at %.4f page units\n", stats.waterParts,
			stats.waterAdditiveParts, stats.waterOpaqueParts,
			stats.waterStillParts, stats.waterTris, stats.waterSubRectParts,
			stats.waterSmallestSpan, stats.waterRippleCap);

	for (int i = 0; i < g_flat_count; i++)
	{
		const int* b = g_flat_box[i];

		printf("H1: group %d, plane y %d, box x %d..%d z %d..%d - art: %s\n",
			room, b[0], b[1], b[3], b[2], b[4],
			HDRenderGroupFlatArt(room, b[0], b[1], b[2], b[3], b[4])
				? "YES (the remaster built it, the classic face stays out)"
				: "NO (the classic water face is drawn on top)");
	}

	if (HDRenderDestructibleCount())
	{
		printf("R3b: %d destructible piece(s):\n",
			HDRenderDestructibleCount());

		for (int i = 0; i < HDRenderDestructibleCount(); i++)
		{
			int group = -1;
			unsigned int material = 0;
			float mins[3], maxs[3];

			if (!HDRenderDestructibleInfo(i, &group, &material, mins, maxs))
				continue;

			printf("     %2d room %3d material 0x%04x copy %d  x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n",
				i, group, material, (material >> 8) & 0x7F,
				mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

			if (g_shatter && group == room)
				HDRenderDestructibleHide(i, true);
		}
	}
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

	/* THE WATER SURVEY (#123, fix round H, H1).
	 *
	 * The offline twin of hd_level.cpp's BuildWaterSurfaces: for every
	 * reflecting room of the CLASSIC level, the faces that lie in the room's
	 * own minfloor plane - the plane the classic shimmer branch opens on -
	 * and, per face, the answer of HDRenderGroupFlatArt, which is the very
	 * function the engine gates on. What it cannot do is the shimmer-vertex
	 * predicate, which needs the engine's roomlets; so this is the upper
	 * bound of what the engine will restore, and the decision per room is the
	 * same majority the engine takes.
	 *
	 * The point of it is that the engine's own gate can be run over all
	 * fifteen levels without starting the game. */
	if (!g_survey.empty())
	{
		Tr5Level level;
		std::string error;
		const std::string trc = g_survey + "\\" + stem + ".TRC";
		std::wstring wide;

		for (size_t i = 0; i < trc.size(); i++)
			wide.push_back((wchar_t)(unsigned char)trc[i]);

		printf("\n-- the water survey (H1) ------------------------------------\n");

		if (!level.load(wide, error))
			printf("classic level not loadable: %s\n", error.c_str());
		else
		{
			int rooms = 0, faces = 0, covered = 0, restored = 0, restoredRooms = 0;
			// N1: rooms handed to the .TRG because it builds the water itself.
			int sheetRooms = 0, sheetFaces = 0;
			// N2: the texinfos the restored faces name - the way into the
			// classic animation ranges.
			std::vector<int> waterTiles;

			for (int r = 0; r < level.roomCount() && r < HDRenderGroupCount(); r++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)r];

				// ROOM_MIST 0x100 or ROOM_REFLECT 0x200 - the two flags whose
				// branch ever sets the surface bit (drawroom.cpp:531-545).
				if (!(rm.flags & 0x300))
					continue;

				std::vector<Tr5Level::FaceInfo> flat;

				if (!level.roomFaces(r, flat, error))
					continue;

				/* `inPlane` every classic water face of the room, `hit` how
				 * many of them the .TRG has any flat art for (fix round H's
				 * majority, counted over all of them), `sheet` how many carry
				 * the .TRG's OWN water and are therefore not drawn, `here`
				 * what is left to restore. The engine's own split,
				 * BuildWaterSurfaces in hd_level.cpp. */
				int inPlane = 0, here = 0, hit = 0, sheet = 0;
				std::vector<int> here_tiles;		// N2

				for (size_t f = 0; f < flat.size(); f++)
				{
					const int corners = flat[f].quad ? 4 : 3;
					bool level_face = true;

					for (int c = 1; c < corners; c++)
						if (flat[f].v[c][1] != flat[f].v[0][1])
							level_face = false;

					if (!level_face || (int)flat[f].v[0][1] != rm.yBottom)
						continue;

					int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);

					for (int c = 0; c < corners; c++)
					{
						const int x = (int)flat[f].v[c][0] - rm.originX;
						const int z = (int)flat[f].v[c][2] - rm.originZ;

						if (x < x0) x0 = x;
						if (x > x1) x1 = x;
						if (z < z0) z0 = z;
						if (z > z1) z1 = z;
					}

					inPlane++;

					if (HDRenderGroupFlatArt(r, rm.yBottom, x0, z0, x1, z1))
						hit++;

					/* N1: material 16 here means the remaster lays its own
					 * water over this piece of ground, so the classic face is
					 * not drawn. It stays in `hit`/`inPlane` above, because
					 * the majority is fix round H's and stays fix round H's. */
					if (HDRenderGroupWaterArt(r, rm.yBottom, x0, z0, x1, z1))
					{
						sheet++;
						continue;
					}

					here++;
					here_tiles.push_back(flat[f].texInfo);	// N2
				}

				if (!inPlane)
					continue;

				rooms++;
				faces += inPlane;
				covered += hit;

				const bool restore = here && hit * 2 < inPlane;

				if (restore)
				{
					restored += here;
					restoredRooms++;

					// N2: only a face that is really drawn asks for a frame.
					for (size_t t = 0; t < here_tiles.size(); t++)
					{
						bool seen = false;

						for (size_t w = 0; w < waterTiles.size(); w++)
							if (waterTiles[w] == here_tiles[t])
								seen = true;

						if (!seen)
							waterTiles.push_back(here_tiles[t]);
					}
				}

				if (sheet)
				{
					sheetRooms++;
					sheetFaces += sheet;
				}

				char verdict[96];

				if (restore)
					sprintf(verdict, "RESTORE %d of %d", here, inPlane);
				else
					sprintf(verdict, "leave it to the .TRG");

				printf("  room %3d  y %6d  %3d classic face(s), the .TRG covers "
					"%3d, of them water sheet %3d  ->  %s\n", r, rm.yBottom,
					inPlane, hit, sheet, verdict);
			}

			printf("  %s: %d reflecting room(s), %d face(s) in the minfloor "
				"plane, %d covered by the .TRG, %d face(s) in %d room(s) "
				"restored; %d room(s) with %d face(s) carry the .TRG\'s own "
				"water sheet\n", stem, rooms, faces, covered, restored,
				restoredRooms, sheetRooms, sheetFaces);
			printf("  (the majority is fix round H's, over every face in the "
				"plane; a face under the .TRG's own sheet is then dropped "
				"from what a restored room draws)\n");


			/* N2: AND WHICH HD PAGES THE RESTORED FACES' OWN ANIMATION
			 * FRAMES HAVE. The offline twin of the engine's own HDLOG line -
			 * the classic level's animated texture ranges, the tiles in
			 * them, the 64x64 cell each tile sits in and the .DDS the .TEX
			 * names for that cell. */
			WaterFrames(level, root, stem, waterTiles);
		}

		HDRenderShutdown();
		DropDevice();
		return 0;
	}

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
		 *             frame page animation - the part's own page and the
		 *             three layers its param[0..2] name, one page per
		 *             param[3] seconds. Drawn since fix round R3b; the R3b
		 *             line further up says how many of them resolved.
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
			"%d swaying vertices, %d flipbook parts\n",
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

	/* THE SCENE (#123, fix round E). One room, or a room and the neighbours
	 * -with names, drawn in the engine's own order out of the engine's own
	 * entry points - HDFrameDraw draws every room's HD_PASS_SOLID and then
	 * every room's HD_PASS_ADDITIVE, and -oldorder is the room-major order
	 * that came before it. A neighbour stands at its world offset; its
	 * lighting is the same block, which is right for the picture this proves
	 * (no dynamic light in it) and would not be for a light one. */
	bool drew = false;
	D3DMATRIX withWorld[8];

	/* GROUP M: the portal window, if one was asked for, set with the very
	 * call the engine makes per room. It goes on AFTER the sky, because the
	 * engine's sky is drawn in a bracket of its own and never carries one. */
	if (g_scissor)
	{
		if (!HDRenderScissorAvailable())
			printf("group M: this device reports no D3DPRASTERCAPS_SCISSORTEST "
				"- the rectangle is ignored, exactly as the engine ignores it\n");

		HDRenderScissor((int)g_scissor_box[0], (int)g_scissor_box[1],
			(int)g_scissor_box[2], (int)g_scissor_box[3]);
	}

	for (int i = 0; i < g_with_count; i++)
	{
		memset(&withWorld[i], 0, sizeof(withWorld[i]));
		withWorld[i]._11 = withWorld[i]._22 = withWorld[i]._33 =
			withWorld[i]._44 = 1.0f;
		withWorld[i]._41 = g_with_offset[i][0];
		withWorld[i]._42 = g_with_offset[i][1];
		withWorld[i]._43 = g_with_offset[i][2];
	}

	if (!skyOnly)
	{
		if (g_old_order)
		{
			drew = HDRenderRoom(room, 0, &lighting);

			for (int i = 0; i < g_with_count; i++)
				HDRenderRoom(g_with_group[i], &withWorld[i], &lighting);
		}
		else
		{
			/* T1: -window says which half of every group this run
			 * draws - the whole of it (the default), the inside half or
			 * the overhang. */
			drew = DrawOneGroup(room, 0, &lighting, HD_PASS_SOLID);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_SOLID);

			/* H2: the see-through half of the alpha pass - the glass -
			 * after everything opaque and before the glows, which is the
			 * order HDFrameDrawGlow draws them in. */
			DrawOneGroup(room, 0, &lighting, HD_PASS_BLEND);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_BLEND);

			DrawOneGroup(room, 0, &lighting, HD_PASS_ADDITIVE);

			for (int i = 0; i < g_with_count; i++)
				DrawOneGroup(g_with_group[i], &withWorld[i], &lighting,
					HD_PASS_ADDITIVE);
		}
	}

	const int calls = HDRenderDrawCalls();
	// GROUP M: off again inside the bracket, as the engine's frame does.
	HDRenderScissorOff();
	HDRenderFrameEnd();
	g_device->EndScene();

	Take(after);

	if (g_scissor)
	{
		RECT box;

		box.left = g_scissor_box[0];
		box.top = g_scissor_box[1];
		box.right = g_scissor_box[2];
		box.bottom = g_scissor_box[3];
		ScissorProof(shot, box);
	}

	printf("HDRenderRoom returned %s after %d draw calls\n",
		drew ? "true" : "FALSE", calls);

	if (sky)
		printf("R3a: %d sky group(s), %d unlit part(s), HDRenderSky returned %s\n",
			HDRenderSkyGroups(), stats.skyFlatParts, skyDrew ? "true" : "FALSE");

	const int broken = Diff(before, after);
	// kStateCount + kSamplerCount + kStageCount, plus the FVF, the six bound
	// objects, stream 0's offset/stride, and the scissor rectangle (group M).
	printf("state bracket: %d of %d watched states changed - %s\n",
		broken, kStateCount + kSamplerCount + kStageCount + 9,
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

/*
 * ModelProbe - draw one HD object model with the engine's own renderer and
 * write a PNG, and print how its nodes line up with the classic mesh tree.
 *
 * Modified 2026 by ChrisChanSan.
 *
 * THE POINT OF THIS TOOL. specific\hd\hd_model.cpp loads the remaster's .TRM
 * models and skins them on the card, and until stage S3-2 nothing in the game
 * calls it. A skinning path nobody has seen a picture out of is not a path,
 * it is a hope - so this tool gives it a hidden window, a real Direct3D 9
 * device, a render target and a camera, and puts what comes back into a .PNG.
 * Same source files as the engine links, no copy.
 *
 * It answers the three questions stage S3-1 owes the plan:
 *
 *   - does the model READ? Counts, parts, pages, and the vertex layout with
 *     its three bones and three weights.
 *   - does it SKIN? -pose turns one node of the classic mesh tree and
 *     everything below it. A model that comes back with a turned head and a
 *     body that stayed put has been skinned; one that turns as a lump has
 *     not, and one that tears at the neck has the wrong bind pose.
 *   - does it FIT the classic slot? A .TRM vertex names three bones, and
 *     those numbers are indices into the CLASSIC mesh tree - so TRM node n is
 *     classic mesh n and the question is not "which node is which" but "which
 *     of the slot's nodes does the model actually cover". -nodes prints that
 *     for one slot and -all for every bound slot of a level: the nodes with
 *     no geometry are the ones stage S3-2 has to draw the classic way.
 *
 * USAGE
 *
 *   ModelProbe <5-folder> [what] [options]
 *
 *     <5-folder>  the remaster's game 5 folder - the one with DATA, ITEM, TEX
 *
 *   What to look at (one of):
 *     -name <NAME>        the model <5>\ITEM\<NAME>.TRM, with no level behind
 *                         it: bind pose only, node table without a classic
 *                         column. The sibling 4\ITEM is searched too.
 *     -slot <n>           the model bound to slot n of the level, which needs
 *                         -level
 *     -all                every slot of the level, as a table, then stop
 *     -withrooms <LEVEL>  load that level's ROOM pages into the cache first
 *                         (#123, stage S6). The models are then appended to
 *                         it, exactly as HDLevelBuild does in the game, so
 *                         "pages added" is the marginal cost of the models
 *                         rather than their cost in isolation - a page a room
 *                         and a model both wear is paid for once. Prints the
 *                         level's total page bytes as well.
 *     -lara <LEVEL>       LARA'S COMPOSITE (#123, stage S4-1). Loads the
 *                         wardrobe that level wears - the outfit, the face,
 *                         the braid, the hands and the holsters - against the
 *                         level's OWN Lara skeleton, and draws the lot in
 *                         bind pose. Short for -level <LEVEL> plus the mode.
 *
 *   Lara only:
 *     -outfit <NAME>      wear this one instead of the level's own.
 *                         OUTFIT_VCI, VCI, TR3_COAST or the randomizer's own
 *                         guest id (tr3_london, tr3_jungle, tr3_antarctica,
 *                         tr3_nevada) all work.
 *     -guns <what>        pistols, uzi, shotgun, hk, revolver,
 *                         revolver_laser, grapple, grapple_laser, crowbar or
 *                         none - which slot the meshes at the hand nodes come
 *                         from, exactly as the game's mesh swap sets it.
 *     -holsters full|empty|uzi|revolver
 *                         full = LARA_HOLSTERS_PISTOLS on both thighs (the
 *                         guns are away), empty = LARA_HOLSTERS (they are in
 *                         her hands), uzi and revolver the other two slots the
 *                         engine keeps. -guns implies empty - unless this
 *                         switch is on the line, and then it wins: -guns
 *                         revolver -holsters revolver is the contradictory
 *                         frame of user finding C1, the gun in her hand and
 *                         in the holster at once, which the seam has to
 *                         resolve to an empty thigh.
 *     -flare / -torch     the LEFT hand alone, as laraflar.cpp / flmtorch.cpp
 *                         set it.
 *     -back <what>        shotgun, hk or grapple on her back.
 *     -lara-nodes         the table this stage owes the report: every classic
 *                         node 0..14 against every source, and which .TRM
 *                         part covers it.
 *     -lara-pose <node> <yaw> <pitch>
 *                         the same as -pose, spelled for the composite.
 *     -mirror             draw through a view matrix of NEGATIVE determinant,
 *                         which is what a mirror room does to the winding.
 *     -firstperson        draw her the way the seam draws her while the first
 *                         person camera is on (#126): the head node masked
 *                         off the body, no LARA_HEAD and no braid - all three
 *                         sit where the camera does. The CAMERA moves with it
 *                         (#123, stage S8): it goes where game/camera.cpp
 *                         puts the eye - the head joint of this level's own
 *                         skeleton, 16 units up and 48 forward - and looks
 *                         along +z, which is the way she faces. -pitch is
 *                         then the look-DOWN angle and defaults to 60; -yaw
 *                         turns the head. The near cut below is on by
 *                         default, because that is what the game does.
 *     -fpforward <units>  how far the eye sits in front of the head joint;
 *                         48 is FP_EYE_OFFSET, what the game uses when the
 *                         sight line is clear, and 0 is what it falls back to
 *                         with a wall in the way - the case that puts the
 *                         neck nearest the camera.
 *     -cull none|cw|ccw|back
 *                         which side of her body's triangles is drawn. The
 *                         HD path draws every model double sided; with the
 *                         camera inside her that shows her own inside, so
 *                         first person culls (stage S8). Default: back with
 *                         -firstperson, none otherwise.
 *     -nearcut <units>    the first person NEAR CUT, on her body alone: a
 *                         RADIUS around the eye, and every fragment inside it
 *                         is discarded. 0 draws the BEFORE picture. The
 *                         default with -firstperson is the engine's own
 *                         HD_LARA_FIRST_PERSON_NEAR_CUT.
 *     -neck               the measurement that number comes from: for every
 *                         outfit, how many torso-node vertices sit above the
 *                         shoulder line and how near the nearest of them, of
 *                         the shoulders and of the chest come to the eye -
 *                         at both eye positions the game uses, with a
 *                         PASS/FAIL against the constant.
 *     -braidsim [steps]   THE BRAID'S OWN PHYSICS, run offline (#123, fix
 *                         round F, F2). HairControl's chain loop transcribed
 *                         line for line and driven with this level's Lara
 *                         skeleton, her collision spheres and the HD pack, in
 *                         the bind pose - the pose the user's own two video
 *                         frames were shot in. One line per regime: how far
 *                         the busiest link still travels per step after 1,
 *                         10, 30 and `steps` steps, and how deep a link still
 *                         stands inside the backpack - measured against the
 *                         pack BOX of fix round I whether or not the regime
 *                         has it, so the shipped spheres say how far into the
 *                         pack the braid used to go. A settled braid reads
 *                         zero and stays there; the regimes that read
 *                         anything else are the ones the game had. Default
 *                         60 steps, which is a second of game time.
 *     -classicroot        draw the studio braid from the CLASSIC root, which
 *                         is the picture before fix round I (I2): the plait
 *                         starts 15 units clear of the hair knot. The default
 *                         is the engine's own HD root.
 *     -braidanim [<n>]    THE SAME CHAIN UNDER A REAL ANIMATION (#123, fix
 *                         round G). The bind pose has no anchor motion at
 *                         all; this reads the level's own animation table,
 *                         decodes the frames Lara really wears and rebuilds
 *                         all eight collision spheres and the braid root from
 *                         them once per game frame, the game's own frame
 *                         blending included. One row per variant - the
 *                         classic body alone, the pack as fix round F ships
 *                         it, and three ways of softening it - counting the
 *                         TIP's travel per frame in the four buckets the play
 *                         test's HAIRLOG was counted in. Repeat the switch
 *                         for more animations; with no number it runs 103
 *                         (breathing on the spot), 0 (the run loop) and 1
 *                         (the walk loop). The step count comes from
 *                         -braidsim, or 120 without it.
 *     -nobraidfloor       take the floor clamp out of -braidanim, which is
 *                         how fix round F's bind pose ran.
 *     -laracheck          the two proofs of the Lara seam, as tables, and the
 *                         offline twins of what the engine logs as HDLOG
 *                         laracheck and HDLOG braidcheck on its first HD Lara
 *                         frame: every joint's origin put on the screen twice
 *                         (classic transform against the card's matrix), and
 *                         the braid's chain rule - a link's own frame stepped
 *                         one bone offset has to land on the next link, which
 *                         is how HairControl derives the positions in the
 *                         first place.
 *
 *   The level:
 *     -level <LEVEL>      stem, e.g. ANDREA1. The classic level is read from
 *                         <5-folder>\DATA\<LEVEL>.TRC and the binding from
 *                         <LEVEL>.MAP beside it.
 *     -classic <path>     a classic .TRC by path instead - the user's own
 *                         Steam data, or a randomized level.
 *
 *   The picture:
 *     -size <w> <h>       render target, default 1024 1024
 *     -fov <deg>          default 80, the engine's own
 *     -yaw <deg>          camera around the model, default 180 (from the front)
 *     -pitch <deg>        default 10, looking slightly down
 *     -dist <units>       camera distance; default frames the bounding box
 *     -itemcheck          the matrix proof of the object path, as a table:
 *                         every node's origin put on the screen twice, once
 *                         by the classic transform and once by the matrix the
 *                         card is handed. The engine runs the same check on
 *                         its own matrix stack and logs HDLOG itemcheck; this
 *                         one runs it without the game.
 *     -meshbits <mask>    hide nodes exactly as item->mesh_bits does in the
 *                         game; 0xBFFF takes a SWAT's head off. Decimal or
 *                         0x. Default: every node drawn.
 *     -pose <node> <yaw> <pitch>
 *                         turn that node of the mesh tree and everything
 *                         below it. Repeat the switch for more nodes.
 *     -head               draw the separate HD face as well, if there is one
 *     -onlypage <n>       with -name: build only the triangles that wear that
 *                         .DDS page, and load only that page (#123, fix round
 *                         D). This is the cut the wardrobe uses to take the
 *                         thigh holster rig out of OUTFIT_TR3_CATSUIT
 *                         (page 8205) without its legs, and the switch is how
 *                         one looks at what the cut kept.
 *     -light <x y z r g b range>
 *                         one POINT light in VIEW space (the camera is at the
 *                         origin looking down +z), colour 0..255 and the
 *                         range its falloff reaches zero at. Up to 12.
 *     -shade <0..255>     the flat node AMBIENT, default 128
 *     -out <prefix>       output path prefix, default .\modelprobe
 *
 *   Everything else:
 *     -nodes              print the node table (also printed by -all)
 *     -threads <n>        page decoding threads, default 8
 *     -managed            pages in D3DPOOL_MANAGED instead of D3DPOOL_DEFAULT
 *     -nodraw             load and measure only - no window, no picture
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

#include <string>
#include <vector>

#include "zlib.h"

#include "core/Tr5Level.h"

/* By path, not through a /I of specific\: that folder holds the engine's own
 * time.h, and putting it on the include path makes <chrono> pick it up
 * instead of the CRT's. */
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_render.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_pagecache.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_model.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_lara.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_bind.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_trm.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")

/* ------------------------------------------------------------------ PNG -- */
/* The same writer RoomProbe and DdsProbe use: 24 bit truecolour, filter 0 on
 * every scanline, deflate through the zlib the engine already links. */

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
	unsigned long crc;

	PutBE(head, len);
	memcpy(head + 4, tag, 4);
	fwrite(head, 1, 8, f);

	if (len)
		fwrite(data, 1, len, f);

	crc = crc32(0, (const Bytef*)tag, 4);

	if (len)
		crc = crc32(crc, (const Bytef*)data, len);

	PutBE(head, crc);
	fwrite(head, 1, 4, f);
}

static bool WritePng(const char* path, const unsigned long* px, long w, long h)
{
	FILE* f = fopen(path, "wb");

	if (!f)
		return false;

	static const unsigned char signature[8] =
		{ 137, 'P', 'N', 'G', 13, 10, 26, 10 };

	fwrite(signature, 1, 8, f);

	unsigned char ihdr[13];

	PutBE(ihdr, (unsigned long)w);
	PutBE(ihdr + 4, (unsigned long)h);
	ihdr[8] = 8;
	ihdr[9] = 2;			// truecolour
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	PngChunk(f, "IHDR", ihdr, 13);

	const size_t raw_size = (size_t)h * (1 + (size_t)w * 3);
	unsigned char* raw = (unsigned char*)malloc(raw_size);

	if (!raw)
	{
		fclose(f);
		return false;
	}

	unsigned char* at = raw;

	for (long y = 0; y < h; y++)
	{
		*at++ = 0;

		for (long x = 0; x < w; x++)
		{
			const unsigned long c = px[y * w + x];

			*at++ = (unsigned char)((c >> 16) & 255);
			*at++ = (unsigned char)((c >> 8) & 255);
			*at++ = (unsigned char)(c & 255);
		}
	}

	uLongf packed = (uLongf)compressBound((uLong)raw_size);
	unsigned char* out = (unsigned char*)malloc(packed);
	bool ok = false;

	if (out && compress2(out, &packed, raw, (uLong)raw_size, 9) == Z_OK)
	{
		PngChunk(f, "IDAT", out, packed);
		PngChunk(f, "IEND", 0, 0);
		ok = true;
	}

	free(out);
	free(raw);
	fclose(f);
	return ok;
}

/* --------------------------------------------------------------- camera -- */

/* The engine's own view convention (3dmath.cpp): x right, y DOWN, z forward,
 * a point goes through as a ROW vector. yaw 0 looks along +Z and grows
 * towards +X; a positive pitch looks downwards. Same function as RoomProbe's,
 * because a picture out of the two tools has to be comparable. */
static void LookAt(double ex, double ey, double ez, double yawDeg, double pitchDeg,
	float* out)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const double yaw = yawDeg * d2r;
	const double pitch = pitchDeg * d2r;

	const double sy = sin(yaw), cy = cos(yaw);
	const double sp = sin(pitch), cp = cos(pitch);

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

/* AlterFOV (3dmath.cpp:78-98) without the engine: the field of view is
 * measured against a 4:3 width whatever the window really is. */
static double EnginePersp(long height, double fovDeg)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const long fovWidth = height * 320 / 240;
	const double half = fovDeg * 0.5 * d2r;

	return (double)(fovWidth / 2) * cos(half) / sin(half);
}

/* -------------------------------------------------------------- matrices -- */

/* Row vector convention, the same one D3DMATRIX and the engine use:
 * out = a * b, and a point goes through as p * m. */
static void MatMul(const float* a, const float* b, float* out)
{
	float tmp[16];

	for (int row = 0; row < 4; row++)
		for (int col = 0; col < 4; col++)
		{
			float sum = 0;

			for (int k = 0; k < 4; k++)
				sum += a[row * 4 + k] * b[k * 4 + col];

			tmp[row * 4 + col] = sum;
		}

	memcpy(out, tmp, sizeof(tmp));
}

static void MatIdentity(float* m)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void MatTranslate(float x, float y, float z, float* m)
{
	MatIdentity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
}

static void MatRotY(double deg, float* m)
{
	const double a = deg * 3.14159265358979323846 / 180.0;
	const float c = (float)cos(a), s = (float)sin(a);

	MatIdentity(m);
	m[0] = c;  m[2] = -s;
	m[8] = s;  m[10] = c;
}

static void MatRotX(double deg, float* m)
{
	const double a = deg * 3.14159265358979323846 / 180.0;
	const float c = (float)cos(a), s = (float)sin(a);

	MatIdentity(m);
	m[5] = c;  m[6] = s;
	m[9] = -s; m[10] = c;
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
	cls.lpszClassName = "ModelProbeWindow";
	RegisterClassA(&cls);

	// Never shown: WS_POPUP without WS_VISIBLE, one pixel off screen.
	g_window = CreateWindowA("ModelProbeWindow", "ModelProbe", WS_POPUP,
		-32, -32, 16, 16, 0, 0, cls.hInstance, 0);

	if (!g_window)
	{
		printf("ModelProbe: CreateWindow failed\n");
		return false;
	}

	g_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!g_d3d)
	{
		printf("ModelProbe: Direct3DCreate9 failed\n");
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
		printf("ModelProbe: CreateDevice(HAL, hardware vertex processing) failed\n");
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
		printf("ModelProbe: CreateTexture(render target %ldx%ld) failed\n", w, h);
		return false;
	}

	if (FAILED(shot.target->GetSurfaceLevel(0, &shot.colour)))
		return false;

	if (FAILED(g_device->CreateDepthStencilSurface((UINT)w, (UINT)h, D3DFMT_D24S8,
		D3DMULTISAMPLE_NONE, 0, TRUE, &shot.depth, 0)))
	{
		printf("ModelProbe: CreateDepthStencilSurface failed\n");
		return false;
	}

	if (FAILED(g_device->CreateOffscreenPlainSurface((UINT)w, (UINT)h,
		D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &shot.readback, 0)))
	{
		printf("ModelProbe: CreateOffscreenPlainSurface failed\n");
		return false;
	}

	g_device->GetRenderTarget(0, &shot.oldColour);
	g_device->SetRenderTarget(0, shot.colour);
	g_device->SetDepthStencilSurface(shot.depth);

	/* MAGENTA, not black: a model that draws nothing and a model that draws
	 * black are the same picture on a black background, and the first is a
	 * bug while the second is a dark corner. */
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);

	return true;
}

static bool ShotWrite(Shot& shot, const char* path)
{
	D3DLOCKED_RECT rect;

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)))
	{
		printf("ModelProbe: GetRenderTargetData failed\n");
		return false;
	}

	if (FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		printf("ModelProbe: locking the readback surface failed\n");
		return false;
	}

	unsigned long* pixels = (unsigned long*)malloc((size_t)shot.w * (size_t)shot.h * 4);

	if (!pixels)
	{
		shot.readback->UnlockRect();
		return false;
	}

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
		printf("ModelProbe: writing %s failed\n", path);

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

/* ------------------------------------------------------- the classic side -- */

/* THE BIND POSE AND THE HIERARCHY of one slot, straight out of the .TRC.
 *
 * A TR5 mesh tree is four longs per node after the first - flags, x, y, z -
 * and the flags carry the matrix stack: bit 0 pops, bit 1 pushes, and the
 * push happens BEFORE the translation, so what is pushed is the parent's
 * frame (game\draw.cpp, DrawAnimatingItem). The bind pose is that walk with
 * every rotation left out, which is TROne's bind_pose_of_slot
 * (game_renderer.cpp:1752) and the very thing the .TRM's vertices are
 * measured against.
 */
struct ClassicSlot
{
	int slot = -1;
	int nodes = 0;
	float bind[32][3] = { { 0, 0, 0 } };
	int parent[32] = { 0 };
	long flags[32] = { 0 };
	long delta[32][3] = { { 0, 0, 0 } };
	bool ok = false;
};

static bool ReadClassicSlot(const Tr5Level& level, int slot, ClassicSlot& out)
{
	Tr5Level::MoveableInfo info;

	if (!level.moveableInfo(slot, info))
		return false;

	out.slot = slot;
	out.nodes = info.nmeshes < 32 ? info.nmeshes : 32;
	out.parent[0] = -1;
	out.flags[0] = 0;

	float current[3] = { 0, 0, 0 };
	float stack[32][3];
	int parentStack[32];
	int top = 0;
	int parent = 0;

	memset(out.bind, 0, sizeof(out.bind));

	for (int node = 1; node < out.nodes; node++)
	{
		const long long at = (long long)info.boneIndex + (long long)(node - 1) * 4;
		const long flags = level.debugReadBoneLong(at + 0);
		const float x = (float)level.debugReadBoneLong(at + 1);
		const float y = (float)level.debugReadBoneLong(at + 2);
		const float z = (float)level.debugReadBoneLong(at + 3);

		out.flags[node] = flags;
		out.delta[node][0] = (long)x;
		out.delta[node][1] = (long)y;
		out.delta[node][2] = (long)z;

		if ((flags & 1) && top > 0)
		{
			--top;
			memcpy(current, stack[top], sizeof(current));
			parent = parentStack[top];
		}

		if (flags & 2)
		{
			memcpy(stack[top], current, sizeof(current));
			parentStack[top] = parent;

			if (top < 31)
				++top;
		}

		current[0] += x;
		current[1] += y;
		current[2] += z;

		memcpy(out.bind[node], current, sizeof(current));
		out.parent[node] = parent;
		parent = node;
	}

	out.ok = true;
	return true;
}

/* The node matrices, model space to VIEW space - what the engine's matrix
 * stack holds when it calls phd_PutPolygons for each mesh, and what
 * HDModelDraw wants. `turn` is one rotation per node in degrees (yaw, pitch),
 * applied where -pose asked for it; a node's rotation reaches every node
 * below it because the stack carries it down. */
static void BuildNodeMatrices(const ClassicSlot& classic, const float* view,
	const double turn[32][2], float* out /* nodes * 16 */)
{
	float stack[32][16];
	float current[16];
	int top = 0;

	float rotation[16], tmp[16], step[16];

	MatIdentity(rotation);

	if (turn && (turn[0][0] != 0.0 || turn[0][1] != 0.0))
	{
		MatRotY(turn[0][0], tmp);
		MatRotX(turn[0][1], step);
		MatMul(tmp, step, rotation);
	}

	MatMul(rotation, view, current);
	memcpy(out, current, sizeof(current));

	for (int node = 1; node < classic.nodes; node++)
	{
		if ((classic.flags[node] & 1) && top > 0)
			memcpy(current, stack[--top], sizeof(current));

		if (classic.flags[node] & 2)
		{
			memcpy(stack[top], current, sizeof(current));

			if (top < 31)
				++top;
		}

		MatTranslate((float)classic.delta[node][0], (float)classic.delta[node][1],
			(float)classic.delta[node][2], step);
		MatMul(step, current, tmp);
		memcpy(current, tmp, sizeof(current));

		if (turn && (turn[node][0] != 0.0 || turn[node][1] != 0.0))
		{
			float yawM[16], pitchM[16];

			MatRotY(turn[node][0], yawM);
			MatRotX(turn[node][1], pitchM);
			MatMul(yawM, pitchM, rotation);
			MatMul(rotation, current, tmp);
			memcpy(current, tmp, sizeof(current));
		}

		memcpy(out + node * 16, current, sizeof(current));
	}
}

/* -------------------------------------------------------------- the pages -- */

static bool Exists(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
		return false;

	fclose(f);
	return true;
}

static std::string ParentOf(const std::string& path)
{
	std::string out = path;

	while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
		out.erase(out.size() - 1);

	const size_t cut = out.find_last_of("\\/");

	return cut == std::string::npos ? std::string(".") : out.substr(0, cut);
}

/* Whether a page has a .DDS of its own anywhere in the bundle. The page cache
 * puts WHITE.DDS in its place when it has none, which is a picture and not an
 * error - but for the report the two have to be told apart. */
static bool PageThere(const std::string& root5, int page)
{
	char name[64];

	sprintf(name, "\\TEX\\%d.DDS", page);

	if (Exists(root5 + name))
		return true;

	const std::string bundle = ParentOf(root5);

	return Exists(bundle + "\\4" + name) || Exists(bundle + "\\6" + name);
}

/* ------------------------------------------------------------------ main -- */

static std::wstring ToW(const std::string& s)
{
	std::wstring w;

	for (size_t i = 0; i < s.size(); i++)
		w += (wchar_t)(unsigned char)s[i];

	return w;
}

static void PrintNodeTable(int slot, const ClassicSlot* classic, const char* name)
{
	printf("\n-- nodes: %s, slot %d ----------------------------------------\n",
		name ? name : "?", slot);
	printf("  %-5s %-9s %-24s %-9s %s\n", "node", "TRM vtx", "classic bind (x y z)",
		"parent", "");
	printf("  ----- --------- ------------------------ --------- ------------------\n");

	const int nodes = HDModelNodes(slot);
	int covered = 0;

	for (int node = 0; node < nodes; node++)
	{
		const int vertices = HDModelNodeVertices(slot, node);
		const bool has = HDModelNodeCovered(slot, node);

		if (has)
			covered++;

		char bind[64] = "-";
		char parent[16] = "-";

		if (classic && classic->ok && node < classic->nodes)
		{
			sprintf(bind, "%7.0f %7.0f %7.0f", classic->bind[node][0],
				classic->bind[node][1], classic->bind[node][2]);

			if (node)
				sprintf(parent, "%d", classic->parent[node]);
			else
				sprintf(parent, "root");
		}

		printf("  %-5d %-9d %-24s %-9s %s\n", node, vertices, bind, parent,
			has ? "" : "NO GEOMETRY - classic fallback");
	}

	printf("  %d of %d nodes covered by the model, %d fall back to the "
		"classic mesh\n", covered, nodes, nodes - covered);

	if (HDModelHasHead(slot))
		printf("  a separate HD face rides rigidly on node %d\n", HDModelHeadNode(slot));
}

/* ------------------------------------------------------- the neck stump -- */
/* WHERE THE FIRST PERSON NEAR CUT COMES FROM (#123, stage S8).
 *
 * The classic path hides Lara's head MESH in first person, and for a classic
 * body that is the whole of it - LM_HEAD is head and neck in one piece. An HD
 * body is a single skinned surface: masking node 14 takes the head off, and
 * the NECK STUMP below it stays, because those triangles are weighted to the
 * TORSO. That is what stands in the camera when she looks down (user finding
 * F3). The diving suit puts its whole helmet on the torso node in the same
 * way.
 *
 * This table measures how near that geometry comes to the eye - for every
 * outfit, not only the one the level wears, because the cut is one number for
 * all of them. The eye is the game's: the head joint of THIS level's Lara
 * skeleton, 16 units up and 48 or 0 units forward (game/camera.cpp,
 * FirstPersonCamera - mgLOS decides which). The cut is a RADIUS around that
 * point, so what matters is DISTANCE and not depth: it has to sit above every
 * stump figure and below every shoulder and chest figure in the table.
 */
static std::string TrmPath(const std::string& root5, const char* stem)
{
	const std::string leaf = std::string("\\ITEM\\") + stem + ".TRM";

	if (Exists(root5 + leaf))
		return root5 + leaf;

	const std::string bundle = ParentOf(root5);

	if (Exists(bundle + "\\4" + leaf))
		return bundle + "\\4" + leaf;

	if (Exists(bundle + "\\6" + leaf))
		return bundle + "\\6" + leaf;

	return std::string();
}

/* ------------------------------------------------------- the braid root -- */
/* #123, fix round C, user finding C4: "the braid does not grip the head
 * properly (Todesengel's too)".
 *
 * THE THREE THINGS THAT HAVE TO MEET, all in the classic bind pose, which is
 * the pose every .TRM vertex is written in:
 *
 *   1. THE ROOT the game hangs the braid from. game\hair.cpp:397 walks the
 *      mesh tree to the head bone and translates by (-4, -4, -48) in its own
 *      frame; the result is hairs[0][0], and the seam puts HAIR_OG's bone 0
 *      there. In the bind pose the head frame is axis aligned with the world,
 *      so the root is simply the head joint plus that triple.
 *   2. THE HEAD the braid comes out of - the outfit's own node 14 geometry and
 *      the separate LARA_HEAD that rides rigidly on it, which is where the
 *      hair and the tie are modelled.
 *   3. HAIR_OG's FIRST LINK, whose bone 0 sits at the model's origin: its
 *      vertices say how far the link reaches BACKWARDS out of that origin -
 *      negative z, since the braid's bind chain runs down +z (0, 56, 112 ..).
 *
 * The braid grips when link 0's backward reach covers the distance from the
 * root to the head's surface. This table prints all three per outfit and the
 * distance that is left over, which is the number a constant would have to
 * close - and which is 0 or less when nothing needs closing. */
static void PrintBraidTable(const std::string& root5, const ClassicSlot& body)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\nthe braid root needs this level's Lara tree, which is not "
			"readable here.\n");
		return;
	}

	const double hx = body.bind[HD_LARA_HEAD_NODE][0];
	const double hy = body.bind[HD_LARA_HEAD_NODE][1];
	const double hz = body.bind[HD_LARA_HEAD_NODE][2];
	const double rx = hx + HD_LARA_BRAID_ROOT_X;
	const double ry = hy + HD_LARA_BRAID_ROOT_Y;
	const double rz = hz + HD_LARA_BRAID_ROOT_Z;

	printf("\n-- the braid root, per outfit --------------------------------\n");
	printf("head joint %.0f/%.0f/%.0f, hair.cpp root %.0f/%.0f/%.0f "
		"(offset %d/%d/%d)\n", hx, hy, hz, rx, ry, rz,
		HD_LARA_BRAID_ROOT_X, HD_LARA_BRAID_ROOT_Y, HD_LARA_BRAID_ROOT_Z);

	/* HAIR_OG's first link first - it is the same file for every outfit. */
	double reach = 0.0;
	int linkVerts = 0;
	{
		HDTrmModel og;
		const char* why = "";
		const std::string path = TrmPath(root5, "HAIR_OG");

		if (path.empty() || !HDTrmLoad(path.c_str(), &og, &why))
			printf("HAIR_OG not read - %s\n",
				path.empty() ? "no .TRM in 5 or 4" : why);
		else
		{
			double lo = 1.0e9, hi = -1.0e9;
			/* HOW THICK THE PLAIT IS (#123, fix round I, I1). The push-out of
			 * a BOX needs a margin, and the margin is the braid's own radius:
			 * a link's axis is its bone's +z, so the radius is the furthest a
			 * vertex of that link stands from that axis. Measured per link and
			 * over the whole model, because the plait tapers. */
			double linkR[HD_LARA_HAIR_NODES];
			int linkN[HD_LARA_HAIR_NODES];
			double allR = 0.0;

			for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
			{
				linkR[n] = 0.0;
				linkN[n] = 0;
			}

			for (int i = 0; i < og.vertices; i++)
			{
				const HDTrmVertex& v = og.vertex[i];
				int bone = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					bone = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					bone = v.bone[2];

				const double r = sqrt((double)v.x * v.x + (double)v.y * v.y);

				if (r > allR)
					allR = r;

				if (bone >= 0 && bone < HD_LARA_HAIR_NODES)
				{
					linkN[bone]++;

					if (r > linkR[bone])
						linkR[bone] = r;
				}

				if (bone)
					continue;

				linkVerts++;

				if (v.z < lo) lo = v.z;
				if (v.z > hi) hi = v.z;
			}

			reach = linkVerts ? -lo : 0.0;
			printf("HAIR_OG link 0: %d vertices, z %.1f .. %.1f - it reaches "
				"%.1f units back out of its own origin\n",
				linkVerts, lo, hi, reach);
			printf("HAIR_OG thickness: radius about the bone axis");

			for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
				printf(" %d:%.1f(%d)", n, linkR[n], linkN[n]);

			printf(", worst %.1f\n", allR);
			HDTrmFree(&og);
		}
	}

	printf("\n%-19s %-10s %8s %8s %8s\n", "outfit", "part", "nearest",
		"reach", "left");
	printf("%-19s %-10s %8s %8s %8s\n", "-------------------", "----------",
		"--------", "--------", "--------");

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);

		if (!stem)
			continue;

		/* The outfit's own node 14 and the separate face, in that order. The
		 * face is a facial RIG - its bones are not mesh tree nodes - so every
		 * one of its vertices counts, and it rides on the head joint. */
		for (int which = 0; which < 2; which++)
		{
			const char* name = which ? "LARA_HEAD" : stem;
			HDTrmModel model;
			const char* why = "";
			const std::string path = TrmPath(root5, name);

			if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			{
				printf("%-19s %-10s not read - %s\n", stem, name,
					path.empty() ? "no .TRM in 5 or 4" : why);
				continue;
			}

			double nearest = 1.0e9;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];

				if (!which)
				{
					int node = v.bone[0];

					if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
						node = v.bone[1];
					else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
						node = v.bone[2];

					if (node != HD_LARA_HEAD_NODE)
						continue;
				}

				const double dx = v.x - rx;
				const double dy = v.y - ry;
				const double dz = v.z - rz;
				const double d = sqrt(dx * dx + dy * dy + dz * dz);

				if (d < nearest)
					nearest = d;
			}

			if (nearest > 1.0e8)
				printf("%-19s %-10s %8s %8.1f %8s\n", stem,
					which ? "face" : "body", "-", reach, "-");
			else
				printf("%-19s %-10s %8.1f %8.1f %8.1f\n", stem,
					which ? "face" : "body", nearest, reach, nearest - reach);

			HDTrmFree(&model);
		}
	}

	printf("\n\"nearest\" is the distance from the root to the nearest vertex "
		"of that part,\n\"reach\" how far link 0 of HAIR_OG comes back out of "
		"the root towards it, and\n\"left\" the gap that stays open. A "
		"NEGATIVE left means the link is already\ninside the head and nothing "
		"needs moving.\n");

	/* --- WHERE THE HAIR KNOT IS (#123, fix round I, I2) ------------------ */
	/* The user's finding: "the anchor is still wrong" - the braid starts
	 * behind the skull beside the pack instead of coming out of the knot. Fix
	 * round D measured the gap against the SKULL, which is the outfit's own
	 * node 14; the knot is on LARA_HEAD, and the two are not the same surface.
	 * So the head file is measured on its own: its box, its rearmost point,
	 * and the nearest of its vertices BEHIND the head joint - which is the
	 * knot, since nothing else of a face is back there. */
	{
		HDTrmModel head;
		const char* why = "";
		const std::string path = TrmPath(root5, "LARA_HEAD");

		if (path.empty() || !HDTrmLoad(path.c_str(), &head, &why))
			printf("\nLARA_HEAD not read - %s\n",
				path.empty() ? "no .TRM in 5 or 4" : why);
		else
		{
			double lo[3] = { 1.0e9, 1.0e9, 1.0e9 };
			double hi[3] = { -1.0e9, -1.0e9, -1.0e9 };
			double best = 1.0e9, bestAt[3] = { 0, 0, 0 };
			double rear = 1.0e9, rearAt[3] = { 0, 0, 0 };
			int behind = 0;

			for (int i = 0; i < head.vertices; i++)
			{
				const HDTrmVertex& v = head.vertex[i];
				const double p[3] = { (double)v.x, (double)v.y, (double)v.z };

				for (int a = 0; a < 3; a++)
				{
					if (p[a] < lo[a]) lo[a] = p[a];
					if (p[a] > hi[a]) hi[a] = p[a];
				}

				if (p[2] < rear)
				{
					rear = p[2];
					rearAt[0] = p[0]; rearAt[1] = p[1]; rearAt[2] = p[2];
				}

				// Behind the head JOINT is where the knot and the tie are.
				if (p[2] >= hz)
					continue;

				behind++;

				const double d = sqrt((p[0] - rx) * (p[0] - rx) +
					(p[1] - ry) * (p[1] - ry) + (p[2] - rz) * (p[2] - rz));

				if (d < best)
				{
					best = d;
					bestAt[0] = p[0]; bestAt[1] = p[1]; bestAt[2] = p[2];
				}
			}

			printf("\nLARA_HEAD box   x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f "
				"(%d vertices, %d of them behind the joint)\n",
				lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], head.vertices, behind);
			printf("rearmost vertex %.1f/%.1f/%.1f - the back of the knot\n",
				rearAt[0], rearAt[1], rearAt[2]);
			printf("nearest vertex behind the joint %.1f/%.1f/%.1f, %.1f units "
				"from the root\n", bestAt[0], bestAt[1], bestAt[2], best);
			printf("the root would have to move %.1f in z to sit on it "
				"(root_z %d -> %d)\n", bestAt[2] - rz, HD_LARA_BRAID_ROOT_Z,
				(int)(HD_LARA_BRAID_ROOT_Z + (bestAt[2] - rz) + 0.5));
			HDTrmFree(&head);
		}
	}

	/* --- THE BACKPACK (#123, fix round D, D3b) --------------------------- */
	/* The classic HairControl collides the braid with five spheres, all of
	 * them CLASSIC Lara meshes: hips, torso, head and the two upper arms. An
	 * HD outfit is a different body and its pack stands further out than the
	 * classic torso sphere reaches, so the HD braid walks through it. What
	 * follows is the number an added sphere needs: the outfit's own pack, in
	 * the TORSO NODE'S frame, which is the frame hair.cpp is in when it
	 * places sphere[1].
	 *
	 * "The pack" is defined geometrically rather than by texture page, so
	 * that the rule does not depend on how one outfit's artist split his
	 * materials: the node 7 vertices that sit more than 30 units BEHIND the
	 * torso joint. Nothing else of a torso does. */
	printf("\n-- the backpack, per outfit (torso joint %.0f/%.0f/%.0f) ------\n",
		body.bind[7][0], body.bind[7][1], body.bind[7][2]);
	printf("%-19s %7s %24s %8s\n", "outfit", "verts",
		"centre in the torso frame", "radius");
	printf("%-19s %7s %24s %8s\n", "-------------------", "-------",
		"------------------------", "--------");

	/* THE PACK AS A BOX (#123, fix round I, I1). Fix round G measured that the
	 * pack is a SLAB and that two spheres of radius 36 touch it on the centre
	 * line alone, so the braid lies IN the pack instead of on it. A box needs
	 * six numbers and the spheres gave three, so they are taken here - over the
	 * whole pack point set and, separately, over the middle strip a braid can
	 * actually reach (|x| <= 30), which is the strip round G quoted. */
	double boxLo[3] = { 1.0e9, 1.0e9, 1.0e9 };
	double boxHi[3] = { -1.0e9, -1.0e9, -1.0e9 };
	double stripLo[3] = { 1.0e9, 1.0e9, 1.0e9 };
	double stripHi[3] = { -1.0e9, -1.0e9, -1.0e9 };
	int boxOutfits = 0;
	char boxRows[HD_LARA_OUTFIT_COUNT][192];

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);

		if (!stem)
			continue;

		HDTrmModel model;
		const char* why = "";
		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		double lo[3] = { 1.0e9, 1.0e9, 1.0e9 };
		double hi[3] = { -1.0e9, -1.0e9, -1.0e9 };
		double slo[3] = { 1.0e9, 1.0e9, 1.0e9 };
		double shi[3] = { -1.0e9, -1.0e9, -1.0e9 };
		int count = 0;

		for (int pass = 0; pass < 2; pass++)
		{
			double radius = 0.0;
			const double cx = 0.5 * (lo[0] + hi[0]);
			const double cy = 0.5 * (lo[1] + hi[1]);
			const double cz = 0.5 * (lo[2] + hi[2]);

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != 7)
					continue;

				const double lx = v.x - body.bind[7][0];
				const double ly = v.y - body.bind[7][1];
				const double lz = v.z - body.bind[7][2];

				if (lz > -30.0)
					continue;

				if (!pass)
				{
					count++;

					if (lx < lo[0]) lo[0] = lx;
					if (ly < lo[1]) lo[1] = ly;
					if (lz < lo[2]) lo[2] = lz;
					if (lx > hi[0]) hi[0] = lx;
					if (ly > hi[1]) hi[1] = ly;
					if (lz > hi[2]) hi[2] = lz;

					// The middle strip, which is the width a braid can touch.
					if (lx >= -30.0 && lx <= 30.0)
					{
						if (lx < slo[0]) slo[0] = lx;
						if (ly < slo[1]) slo[1] = ly;
						if (lz < slo[2]) slo[2] = lz;
						if (lx > shi[0]) shi[0] = lx;
						if (ly > shi[1]) shi[1] = ly;
						if (lz > shi[2]) shi[2] = lz;
					}

					continue;
				}

				const double d = sqrt((lx - cx) * (lx - cx) +
					(ly - cy) * (ly - cy) + (lz - cz) * (lz - cz));

				if (d > radius)
					radius = d;
			}

			if (!pass)
			{
				if (!count)
					break;

				continue;
			}

			printf("%-19s %7d %8.0f %7.0f %7.0f %8.0f\n", stem, count,
				cx, cy, cz, radius);
		}

		if (!count)
			printf("%-19s %7s %24s %8s\n", stem, "-", "no pack", "-");
		else if (outfit != HD_LARA_OUTFIT_SUBSUIT)
		{
			/* THE DIVING SUIT IS NOT IN THE BOX (#123, fix round I, I1, and
			 * fix round D said the same of the spheres): what sits behind its
			 * torso is two thrusters, it wears a helmet and no braid, and its
			 * numbers are half as deep again as any pack's. */
			sprintf(boxRows[boxOutfits], "%-19s %7d  %6.0f..%-6.0f %6.0f..%-6.0f "
				"%6.0f..%-6.0f  %6.0f..%-6.0f %6.0f..%-6.0f", stem, count,
				lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
				slo[1], shi[1], slo[2], shi[2]);
			boxOutfits++;

			for (int a = 0; a < 3; a++)
			{
				if (lo[a] < boxLo[a]) boxLo[a] = lo[a];
				if (hi[a] > boxHi[a]) boxHi[a] = hi[a];
				if (slo[a] < stripLo[a]) stripLo[a] = slo[a];
				if (shi[a] > stripHi[a]) stripHi[a] = shi[a];
			}
		}

		HDTrmFree(&model);
	}

	printf("\nthe sphere HairControl adds for an HD Lara is placed from the "
		"torso node\nwith these offsets; HD_LARA_PACK_* in hd_lara.h carries "
		"the numbers.\n");

	/* --- THE SAME PACK AS A BOX (#123, fix round I, I1) ------------------ */
	printf("\n-- the backpack as a BOX, in the torso frame -----------------\n");
	printf("%-19s %7s  %-13s %-13s %-13s  %-13s %-13s\n", "outfit", "verts",
		"x", "y", "z", "y (|x|<=30)", "z (|x|<=30)");
	printf("------------------- -------  ------------- ------------- "
		"-------------  ------------- -------------\n");

	for (int i = 0; i < boxOutfits; i++)
		printf("%s\n", boxRows[i]);

	if (boxOutfits)
	{
		printf("\nthe union over the %d packs: x %.0f..%.0f  y %.0f..%.0f  "
			"z %.0f..%.0f\n", boxOutfits, boxLo[0], boxHi[0], boxLo[1],
			boxHi[1], boxLo[2], boxHi[2]);
		printf("the union over the middle strip: y %.0f..%.0f  z %.0f..%.0f\n",
			stripLo[1], stripHi[1], stripLo[2], stripHi[2]);
		printf("HD_LARA_PACK_BOX_* in hd_lara.h carries one box for all of "
			"them; the braid is\npushed out of it along the smallest "
			"penetration, the rear face preferred.\n");
	}
}

static void PrintNeckTable(const std::string& root5, const ClassicSlot& body)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\n-neck needs the level's Lara skeleton and this one has none.\n");
		return;
	}

	/* THE TWO EYES. game/camera.cpp asks mgLOS whether it may put the eye
	 * FP_EYE_OFFSET = 48 units in front of the head joint, and takes 0 when
	 * something is in the way - a wall, a ledge, a doorway. Both are ordinary
	 * play, and the cut has to hold at both, so both are measured. The
	 * shoulder line is the two inner-arm joints of the level's own mesh tree,
	 * which is where a body stops being neck. */
	const double kForward[2] = { 48.0, 0.0 };
	const double shoulder =
		0.5 * (body.bind[HD_LARA_RINARM][1] + body.bind[HD_LARA_LINARM][1]);

	printf("\n-- the neck stump, per outfit --------------------------------\n");
	printf("head joint %.0f/%.0f/%.0f, eye 16 above it and 48 or 0 in "
		"front\nshoulder line y %.0f (nodes %d and %d)\n\n",
		body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
		body.bind[HD_LARA_HEAD_NODE][2], shoulder, HD_LARA_RINARM,
		HD_LARA_LINARM);
	printf("%-19s %7s   %-24s   %-24s\n", "outfit", "stump",
		"eye 48 forward", "eye at the joint");
	printf("%-19s %7s   %7s %7s %7s   %7s %7s %7s\n", "", "verts",
		"stump", "shldr", "chest", "stump", "shldr", "chest");
	printf("------------------- -------   ------- ------- -------   "
		"------- ------- -------\n");

	double keepMin = 1.0e9, stumpMax = 0.0;

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
		{
			printf("%-19s not read - %s\n", stem,
				path.empty() ? "no .TRM in 5 or 4" : why);
			continue;
		}

		int stumpVerts = 0;
		double nearest[2][3];  // near is a windows.h macro

		for (int e = 0; e < 2; e++)
			nearest[e][0] = nearest[e][1] = nearest[e][2] = 1.0e9;

		for (int i = 0; i < model.vertices; i++)
		{
			const HDTrmVertex& v = model.vertex[i];

			/* The node a vertex BELONGS to is the one with the biggest of its
			 * three weights - the same rule the range builder uses, and the
			 * one that decides which node's bit hides it. */
			int node = v.bone[0];

			if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
				node = v.bone[1];
			else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
				node = v.bone[2];

			int kind = -1;

			if (node == HD_LARA_RINARM || node == HD_LARA_LINARM)
				kind = 1;
			else if (node == HD_LARA_TORSO)
				kind = v.y < shoulder ? 0 : 2;

			if (kind < 0)
				continue;

			if (!kind)
				stumpVerts++;

			for (int e = 0; e < 2; e++)
			{
				const double dx = v.x - body.bind[HD_LARA_HEAD_NODE][0];
				const double dy = v.y - (body.bind[HD_LARA_HEAD_NODE][1] - 16.0);
				const double dz = v.z -
					(body.bind[HD_LARA_HEAD_NODE][2] + kForward[e]);
				const double d = sqrt(dx * dx + dy * dy + dz * dz);

				if (d < nearest[e][kind])
					nearest[e][kind] = d;
			}
		}

		printf("%-19s %7d   %7.1f %7.1f %7.1f   %7.1f %7.1f %7.1f\n", stem,
			stumpVerts, nearest[0][0], nearest[0][1], nearest[0][2],
			nearest[1][0], nearest[1][1], nearest[1][2]);

		for (int e = 0; e < 2; e++)
		{
			if (nearest[e][0] > stumpMax && nearest[e][0] < 1.0e8)
				stumpMax = nearest[e][0];

			for (int k = 1; k < 3; k++)
				if (nearest[e][k] < keepMin)
					keepMin = nearest[e][k];
		}

		HDTrmFree(&model);
	}

	printf("\nthe nearest stump vertex of any outfit at either eye is %.1f "
		"units away;\nthe nearest shoulder or chest vertex - the geometry that "
		"must SURVIVE -\nis %.1f. A radius between the two separates them from "
		"every angle.\n", stumpMax, keepMin);
	printf("HD_LARA_FIRST_PERSON_NEAR_CUT is %.0f: %s\n",
		(double)HD_LARA_FIRST_PERSON_NEAR_CUT,
		HD_LARA_FIRST_PERSON_NEAR_CUT < keepMin
			? "PASS - it touches no shoulder and no chest"
			: "FAIL - it reaches into geometry that has to stay");
	printf("(a cut at a fixed DEPTH cannot do this: measured at 72 units of "
		"depth it\ntook the collarbone with the stump and opened a window "
		"through her chest.)\n");

	/* --- WHAT THE CUT LEAVES OPEN (#123, fix round I, I3) ----------------
	 *
	 * The user's finding: the cut leaves a hole at the collar and one looks
	 * into the body - the inside of her chest and the backpack straps behind
	 * it. So the neck has to be CLOSED below the cut, and the first question
	 * is what shape the thing being closed is.
	 *
	 * The neck AXIS is the line from the torso joint to the head joint; `s` is
	 * how far down it a vertex sits (0 at the head joint, negative towards the
	 * torso) and `r` how far it stands off the axis. A tube reads as a narrow
	 * band of r at every s; where the band bursts open is where the neck stops
	 * being a neck and becomes shoulders.
	 *
	 * The cap this measurement is for is a DOME sunk into that tube, so what it
	 * needs is the tube's radius at the depth the dome sits at, and the
	 * distance of that depth from both eyes - which has to clear the 60 unit
	 * cut, or the dome would be cut away with the neck it is replacing. */
	printf("\n-- the neck as a tube, per outfit (#123, fix round I, I3) ----\n");
	printf("s is the depth down the torso-joint..head-joint axis from the head "
		"joint,\nr the distance off that axis. Only node 7 (the torso) counts, "
		"which is\nwhat the cut leaves standing.\n\n");
	printf("%-19s %5s %6s %8s %8s %8s %8s %8s\n", "outfit", "s", "verts",
		"r min", "r mean", "r max", "eye 48", "eye 0");
	printf("------------------- ----- ------ -------- -------- -------- "
		"-------- --------\n");

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem || outfit == HD_LARA_OUTFIT_SUBSUIT)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		for (int step = 0; step <= 10; step++)
		{
			const double at = step * 10.0;	// down the axis from the head joint
			double lo = 1.0e9, hi = -1.0e9, sum = 0.0;
			int count = 0;
			double eye[2] = { 1.0e9, 1.0e9 };

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != HD_LARA_TORSO)
					continue;

				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

				if (s < at - 5.0 || s > at + 5.0)
					continue;

				const double off[3] =
				{
					d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
				};
				const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]);

				count++;
				sum += r;

				if (r < lo) lo = r;
				if (r > hi) hi = r;

				for (int e = 0; e < 2; e++)
				{
					const double ex = H[0];
					const double ey = H[1] - 16.0;
					const double ez = H[2] + kForward[e];
					const double dd = sqrt((v.x - ex) * (v.x - ex) +
						(v.y - ey) * (v.y - ey) + (v.z - ez) * (v.z - ez));

					if (dd < eye[e])
						eye[e] = dd;
				}
			}

			if (!count)
				continue;

			printf("%-19s %5.0f %6d %8.1f %8.1f %8.1f %8.1f %8.1f\n",
				step ? "" : stem, at, count, lo, sum / count, hi, eye[0],
				eye[1]);
		}

		HDTrmFree(&model);
	}

	printf("\nthe dome that closes the neck sits at the depth where the tube is "
		"still a tube\nand both eye columns are clear of "
		"HD_LARA_FIRST_PERSON_NEAR_CUT.\n");
}

/* ------------------------------------------------- the braid's physics -- */
/*
 * THE BRAID, RUN OFFLINE (#123, fix round F, F2).
 *
 * The user's finding: the HD braid "glitches about restlessly", two frames of
 * his video showing it long and thrown out to the side in one and short and
 * tucked against her back in the next. Fix round D had changed two things in
 * game\hair.cpp - the root moved 15 units forward for an HD Lara, and the HD
 * backpack got two collision spheres - and it said of the second: "this is
 * the one change no offline picture can prove". A picture cannot, but the
 * simulation can be run on its own, and that is what this does.
 *
 * WHAT IS TRANSCRIBED, AND WHAT IS NOT. The loop below is HairControl's, line
 * for line: integrate three quarters of the last velocity, add the ten units
 * of gravity, push the link out of every sphere, point the link above it at
 * where it landed, and put it back at exactly one bone length along that
 * direction, the new velocity being how far it actually moved. Positions are
 * longs, as they are in the game, and the push-out keeps the game's integer
 * division, because that quantisation is the only thing that can leave a
 * chain trembling when everything else has come to rest.
 *
 * Three of the game's terms are left out and none of them can hide a
 * tremble: the WIND (an outdoor room only, and it is a translation of the
 * whole chain, not a force between links), the WATER LINE (an upward clamp)
 * and the FLOOR (a downward clamp). A braid that will not settle standing
 * still in a dry indoor room will not settle anywhere.
 *
 * The pose is the BIND POSE - Lara standing, every joint at rest - which is
 * the pose the user's own two frames were shot in: she stands at a door and
 * does not move, and the braid moves anyway.
 *
 * WHAT COMES OUT is one line per regime: how far the furthest link still
 * travels per step after 10, 30 and `steps` steps. A settled chain goes to
 * zero and stays there. The last column is the deepest a link still sits
 * inside a pack sphere, which is what the pack was added for.
 */
struct BraidSphere
{
	long x, y, z, r;
};

struct BraidRegime
{
	const char* label;
	/* What the two halves of HairControl do. The KEY frame is the half that
	 * runs when the animation is not interpolating - the `else` half, the one
	 * round D changed - and the TWEEN frame the `if (frac)` half it left
	 * alone. `period` is how often a key frame comes round: 1 is "every
	 * frame is a key frame", 4 is one key frame in four, which is what an
	 * ordinary Lara animation with an interpolation rate of 4 does. */
	long keyRootZ, tweenRootZ;
	int keySpheres, tweenSpheres;
	int period;
	// #123 fix round I (I1): does this regime place the pack BOX?
	bool keyBox, tweenBox;
};

/* phd_sqrt is an integer square root; long arithmetic all the way is what
 * the game does, so the tool does it too. */
static long BraidSqrt(long long v)
{
	if (v <= 0)
		return 0;

	long r = (long)sqrt((double)v);

	while ((long long)r * r > v)
		r--;

	while ((long long)(r + 1) * (r + 1) <= v)
		r++;

	return r;
}

/* ---------------------------------------------- the pack, as a box -------
 *
 * The tool's twin of game\hair.cpp's HdPackFrame / HdPackPush (#123, fix round
 * I, I1) - the same six faces, the same per-link margin and the same
 * preference for the rear one, in doubles instead of the engine's floats
 * because the tool has no fixed-point matrix stack to match.
 *
 * `on` false is a variant without the box at all, which is what the old
 * regimes are measured as.
 */
struct BraidBox
{
	bool on;
	double rot[9];		// the torso frame's axes, rot[3 * row + col]
	double org[3];		// the torso joint, in the world
	long zBack;		// the rear face, so a variant can move it
	int bias;		// how much that face is preferred
};

static const long kBraidLinkR[HD_LARA_HAIR_NODES] = HD_LARA_BRAID_RADII;

static void BraidBoxIdentity(BraidBox& box, double x, double y, double z)
{
	box.on = false;

	for (int i = 0; i < 9; i++)
		box.rot[i] = (i % 4) ? 0.0 : 1.0;

	box.org[0] = x;
	box.org[1] = y;
	box.org[2] = z;
	box.zBack = HD_LARA_PACK_BOX_Z_BACK;
	box.bias = HD_LARA_PACK_BOX_BIAS;
}

/* How far into the box the point stands, 0 when it is outside. The same
 * measurement the push uses, so "how deep" and "was it pushed" cannot
 * disagree. */
static long BraidBoxDepth(const BraidBox& box, const long p[3], int link,
	int* faceOut, double* moveOut)
{
	if (link < 1 || link > 6)
		return 0;

	const double dx = (double)p[0] - box.org[0];
	const double dy = (double)p[1] - box.org[1];
	const double dz = (double)p[2] - box.org[2];

	const double lx = dx * box.rot[0] + dy * box.rot[3] + dz * box.rot[6];
	const double ly = dx * box.rot[1] + dy * box.rot[4] + dz * box.rot[7];
	const double lz = dx * box.rot[2] + dy * box.rot[5] + dz * box.rot[8];

	const double r = (double)kBraidLinkR[link - 1];
	const double loX = -(double)HD_LARA_PACK_BOX_X - r;
	const double hiX = (double)HD_LARA_PACK_BOX_X + r;
	const double loY = (double)HD_LARA_PACK_BOX_Y_TOP - r;
	const double hiY = (double)HD_LARA_PACK_BOX_Y_BOTTOM + r;
	const double loZ = (double)box.zBack - r;
	const double hiZ = (double)HD_LARA_PACK_BOX_Z_FRONT + r;

	if (lx <= loX || lx >= hiX || ly <= loY || ly >= hiY ||
		lz <= loZ || lz >= hiZ)
		return 0;

	const double out[6] =
	{
		lx - loX, hiX - lx,
		ly - loY, hiY - ly,
		lz - loZ, hiZ - lz
	};

	int face = 0;
	double best = out[0];

	for (int i = 1; i < 6; i++)
	{
		const double weigh = i == 4 ? out[i] / (double)box.bias : out[i];
		const double against = face == 4 ? best / (double)box.bias : best;

		if (weigh < against)
		{
			best = out[i];
			face = i;
		}
	}

	if (faceOut)
		*faceOut = face;

	if (moveOut)
	{
		moveOut[0] = moveOut[1] = moveOut[2] = 0.0;
		moveOut[face >> 1] = (face & 1) ? best : -best;
	}

	return (long)(best + 0.5);
}

static bool BraidBoxPush(const BraidBox& box, long p[3], int link)
{
	int face = 0;
	double move[3];

	if (!box.on || !BraidBoxDepth(box, p, link, &face, move))
		return false;

	p[0] += (long)(move[0] * box.rot[0] + move[1] * box.rot[1] +
		move[2] * box.rot[2]);
	p[1] += (long)(move[0] * box.rot[3] + move[1] * box.rot[4] +
		move[2] * box.rot[5]);
	p[2] += (long)(move[0] * box.rot[6] + move[1] * box.rot[7] +
		move[2] * box.rot[8]);

	return true;
}

/* One step of HairControl's chain loop. `links` is hairs[0][0..6] - the root
 * and the six moving points - and `vel` their velocities. */
static long BraidStep(long pos[7][3], long vel[7][3], const long root[3],
	const long length[7], const BraidSphere* sphere, int spheres,
	const BraidBox& box, long* deepest, int* packHits)
{
	long moved = 0;

	if (packHits)
		*packHits = 0;

	pos[0][0] = root[0];
	pos[0][1] = root[1];
	pos[0][2] = root[2];

	for (int i = 1; i < 7; i++)
	{
		const long was[3] = { pos[i][0], pos[i][1], pos[i][2] };

		pos[i][0] += 3 * vel[i][0] / 4;
		pos[i][1] += 3 * vel[i][1] / 4;
		pos[i][2] += 3 * vel[i][2] / 4;

		pos[i][1] += 10;	// gravity; no water line and no floor indoors

		for (int j = 0; j < spheres; j++)
		{
			const long x = pos[i][0] - sphere[j].x;
			const long y = pos[i][1] - sphere[j].y;
			const long z = pos[i][2] - sphere[j].z;
			long long d = (long long)x * x + (long long)y * y +
				(long long)z * z;

			if (d >= (long long)sphere[j].r * sphere[j].r)
				continue;

			long dist = BraidSqrt(d);

			if (!dist)
				dist = 1;

			pos[i][0] = sphere[j].x + x * sphere[j].r / dist;
			pos[i][1] = sphere[j].y + y * sphere[j].r / dist;
			pos[i][2] = sphere[j].z + z * sphere[j].r / dist;

			/* The twin of HAIRLOG's own packhits field in the game: how
			 * many links the pack itself had to push this step. */
			if (j >= 6 && packHits)
				(*packHits)++;
		}

		// ... and out of the box, last, exactly as the game does it.
		if (BraidBoxPush(box, pos[i], i) && packHits)
			(*packHits)++;

		/* The chain rule. The game writes the angles of the link ABOVE and
		 * then rebuilds this one from them, one bone offset along its own z;
		 * the angles are the spherical angles of the vector it just walked,
		 * so the rebuilt point is the previous one plus one bone length in
		 * that direction. (game\hair.cpp: phd_atan(dz, dx) and
		 * -phd_atan(dist, dy), then phd_RotYXZ and phd_TranslateRel.) */
		const double dx = (double)(pos[i][0] - pos[i - 1][0]);
		const double dy = (double)(pos[i][1] - pos[i - 1][1]);
		const double dz = (double)(pos[i][2] - pos[i - 1][2]);
		double len = sqrt(dx * dx + dy * dy + dz * dz);

		if (len < 1.0e-6)
			len = 1.0;

		const double step = (double)length[i] / len;

		pos[i][0] = pos[i - 1][0] + (long)floor(dx * step + 0.5);
		pos[i][1] = pos[i - 1][1] + (long)floor(dy * step + 0.5);
		pos[i][2] = pos[i - 1][2] + (long)floor(dz * step + 0.5);

		vel[i][0] = pos[i][0] - was[0];
		vel[i][1] = pos[i][1] - was[1];
		vel[i][2] = pos[i][2] - was[2];

		const long d2 = labs(vel[i][0]) + labs(vel[i][1]) + labs(vel[i][2]);

		if (d2 > moved)
			moved = d2;
	}

	/* HOW DEEP A LINK STANDS INSIDE THE PACK. Measured against the BOX since
	 * fix round I (I1), and whether or not the box was switched on for this
	 * step - so a regime without it says how far the braid walks into the
	 * backpack it cannot feel, which is the whole of user finding I1. */
	if (deepest)
	{
		*deepest = 0;

		for (int i = 1; i < 7; i++)
		{
			const long in = BraidBoxDepth(box, pos[i], i, 0, 0);

			if (in > *deepest)
				*deepest = in;
		}
	}

	return moved;
}

/* WHERE THE SIMULATION LEAVES THE BRAID, so that the PICTURE can be the
 * simulation's and not a studio pose (#123, fix round I, I1).
 *
 * The braid this tool used to draw was a chain of the tool's own invention -
 * "turned out of its bind direction to point downwards and a little
 * backwards" - which is fine for proving that six bones move independently and
 * useless for the one question this round is about: does the plait lie ON the
 * pack or IN it. -braidrest names one of the regimes below, runs it in the
 * bind pose, and hands the settled positions to the draw, which then builds
 * the six matrices exactly as the engine's braid_matrices does.
 *
 * A global rather than a return value because the sim is a printing function
 * and the picture is built six hundred lines further down; the alternative was
 * to thread an out-parameter through both, which buys nothing. */
static long g_braidRest[7][3];
static bool g_braidRestHave = false;
static int g_braidRestWant = -1;

static void PrintBraidSim(const Tr5Level& level, int bodySlot,
	const ClassicSlot& body, const ClassicSlot& hairTree, bool haveHair,
	int steps)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\n-braidsim needs the level's Lara skeleton and this one has "
			"none.\n");
		return;
	}

	Tr5Level::MoveableInfo info;

	if (!level.moveableInfo(bodySlot, info))
	{
		printf("\n-braidsim: %s has no moveable entry.\n", HDSlotName(bodySlot));
		return;
	}

	/* THE FIVE CLASSIC SPHERES plus the neck one, exactly as HairControl
	 * builds them: a mesh's own centre and radius, in the frame of its node.
	 * In the bind pose the node frames carry no rotation, so a node's origin
	 * is ClassicSlot::bind and the mesh offset is a plain addition. The three
	 * hand-tuned constants (-9 and +25 on the torso, -2 on the head) are
	 * Core's own and come along. */
	static const int kNode[5] = { 0, 7, 14, 8, 11 };	// hips torso head arms
	static const int kInto[5] = { 0, 1, 2, 3, 4 };	// which sphere each fills
	BraidSphere sphere[8];
	int16_t mesh[5][6];

	for (int i = 0; i < 5; i++)
		if (!level.debugMeshHeader(info.meshIndex + kNode[i], mesh[i]))
		{
			printf("\n-braidsim: mesh %d of %s is not readable.\n", kNode[i],
				HDSlotName(bodySlot));
			return;
		}

	/* THE TORSO SPHERE HAS TWO SETTINGS and they are not the same size, so
	 * both are run. HairControl anchors the braid differently for the Russia
	 * outfit - the cap and the bomber jacket - than for every other: the
	 * sphere moves 11 left, 16 up and its radius is nailed to 94 instead of
	 * the mesh's own. `anchor` 0 is the ordinary one, 1 the Russia one. */
	long torsoR[2] = { 0, 94 };
	long torsoOff[2][3] = { { -9, 0, 25 }, { -11, -16, 25 } };

	printf("\n-- the braid, simulated in the bind pose ---------------------\n");

	static const char* kSphereName[8] =
	{
		"0 hips", "1 torso", "2 head", "3 r upper arm", "4 l upper arm",
		"5 neck", "6 pack top", "7 pack bottom"
	};

	static const char* kAnchorName[2] =
	{
		"the ordinary torso anchor", "the Russia / bunhead torso anchor"
	};

	long length[7];

	for (int i = 1; i < 7; i++)
	{
		const int n = i < 6 ? i : 5;

		length[i] = haveHair && n < hairTree.nodes
			? (long)(hairTree.bind[n][2] - hairTree.bind[n - 1][2])
			: 56;
	}

	printf("\nlink lengths %ld %ld %ld %ld %ld %ld, %d steps per regime\n",
		length[1], length[2], length[3], length[4], length[5], length[6],
		steps);

	/* THE FOUR REGIMES.
	 *
	 * The two "as round D built it" lines are the ones that matter. Round D
	 * put both of its changes into the ELSE half of HairControl - the half
	 * that runs on a frame the animation does NOT interpolate - and left the
	 * interpolating half untouched. The engine takes one branch or the other
	 * from frame to frame, so what the braid really saw was a root that
	 * jumped between two places and a backpack that came and went, which is
	 * what these two lines run. */
	static const BraidRegime kRegime[8] =
	{
		{ "classic: root -48, 6 spheres",  HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    6, 6, 1, false, false },
		{ "round D as built, 1 key in 4",  HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z,    8, 6, 4, false, false },
		{ "round D as built, every other", HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z,    8, 6, 2, false, false },
		{ "round D as meant: -33, spheres", HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z_HD, 8, 8, 1, false, false },
		{ "round F/G: root -48, spheres",  HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    8, 8, 1, false, false },
		{ "root -48, no pack at all",      HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    6, 6, 1, false, false },
		/* #123 fix round I (I1 and I2): the box, at the classic root and at
		 * the root round F withdrew. The second line is I2's whole question -
		 * round F's limit cycle was the NECK sphere and a pack SPHERE pushing
		 * in turn, and a box does not push the same way. */
		{ "round I: root -48, the BOX",    HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    6, 6, 1, true, true },
		{ "round I: root -33, the BOX",    HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z_HD, 6, 6, 1, true, true }
	};

	char rest[8][256];

	for (int anchor = 0; anchor < 2; anchor++)
	{

	for (int i = 0; i < 5; i++)
	{
		const int n = kNode[i];
		long ox = mesh[i][0], oy = mesh[i][1], oz = mesh[i][2];
		long r = mesh[i][3];

		if (n == 7)
		{
			ox += torsoOff[anchor][0];
			oy += torsoOff[anchor][1];
			oz += torsoOff[anchor][2];

			if (torsoR[anchor])
				r = torsoR[anchor];
		}
		else if (n == 14)
			ox -= 2;
		else if (n == 8 || n == 11)
			r = 4 * r / 3;

		sphere[kInto[i]].x = (long)body.bind[n][0] + ox;
		sphere[kInto[i]].y = (long)body.bind[n][1] + oy;
		sphere[kInto[i]].z = (long)body.bind[n][2] + oz;
		sphere[kInto[i]].r = r;
	}

	sphere[5].x = (2 * sphere[2].x + sphere[1].x) / 3;
	sphere[5].y = (2 * sphere[2].y + sphere[1].y) / 3;
	sphere[5].z = (2 * sphere[2].z + sphere[1].z) / 3;
	sphere[5].r = 5 * sphere[2].r / 4;

	sphere[6].x = (long)body.bind[7][0] + HD_LARA_PACK_X;
	sphere[6].y = (long)body.bind[7][1] + HD_LARA_PACK_Y_TOP;
	sphere[6].z = (long)body.bind[7][2] + HD_LARA_PACK_Z;
	sphere[6].r = HD_LARA_PACK_R;

	sphere[7].x = (long)body.bind[7][0] + HD_LARA_PACK_X;
	sphere[7].y = (long)body.bind[7][1] + HD_LARA_PACK_Y_BOTTOM;
	sphere[7].z = (long)body.bind[7][2] + HD_LARA_PACK_Z;
	sphere[7].r = HD_LARA_PACK_R;

	printf("\n=== %s ===\n", kAnchorName[anchor]);
	printf("%-16s %7s %7s %7s %8s\n", "sphere", "x", "y", "z", "radius");

	for (int i = 0; i < 8; i++)
		printf("%-16s %7ld %7ld %7ld %8ld\n", kSphereName[i], sphere[i].x,
			sphere[i].y, sphere[i].z, sphere[i].r);

	printf("\n%-34s %8s %8s %8s %8s %9s %8s\n", "regime", "step 1", "step 10",
		"step 30", "last", "in a pack", "packhits");
	printf("%-34s %8s %8s %8s %8s %9s %8s\n",
		"----------------------------------",
		"--------", "--------", "--------", "--------", "---------",
		"--------");

	for (int r = 0; r < 8; r++)
	{
		long pos[7][3], vel[7][3] = { { 0, 0, 0 } };
		long deepest = 0, worstDeepest = 0;
		BraidBox box;

		/* THE BOX SITS IN THE TORSO NODE'S FRAME, and in the bind pose that
		 * frame carries no rotation at all - so the identity plus the torso
		 * joint is the whole of it here (#123, fix round I, I1). */
		BraidBoxIdentity(box, body.bind[7][0], body.bind[7][1], body.bind[7][2]);
		long at1 = 0, at10 = 0, at30 = 0, last = 0;
		int hits = 0, worstHits = 0;

		memset(vel, 0, sizeof(vel));

		/* THE SEED IS THE GAME'S OWN first frame: first_hair hangs the chain
		 * straight down from the root, every velocity zero. */
		const long rootBase[3] =
		{
			(long)body.bind[HD_LARA_HEAD_NODE][0] + HD_LARA_BRAID_ROOT_X,
			(long)body.bind[HD_LARA_HEAD_NODE][1] + HD_LARA_BRAID_ROOT_Y,
			(long)body.bind[HD_LARA_HEAD_NODE][2]
		};

		long down = 0;

		for (int i = 0; i < 7; i++)
		{
			pos[i][0] = rootBase[0];
			pos[i][1] = rootBase[1] + down;
			pos[i][2] = rootBase[2] + kRegime[r].keyRootZ;

			if (i < 6)
				down += length[i + 1];
		}

		for (int s = 1; s <= steps; s++)
		{
			/* A key frame every `period` steps, the tween half in between -
			 * which is exactly how the engine alternates between the two
			 * halves of HairControl. */
			const bool key = (s % kRegime[r].period) == 0;
			long root[3] =
			{
				rootBase[0], rootBase[1],
				rootBase[2] + (key ? kRegime[r].keyRootZ
					: kRegime[r].tweenRootZ)
			};
			const int n = key ? kRegime[r].keySpheres
				: kRegime[r].tweenSpheres;

			box.on = key ? kRegime[r].keyBox : kRegime[r].tweenBox;

			last = BraidStep(pos, vel, root, length, sphere, n, box, &deepest,
				&hits);

			if (s > steps - 10 && hits > worstHits)
				worstHits = hits;

			if (s == 1) at1 = last;
			if (s == 10) at10 = last;
			if (s == 30) at30 = last;

			if (s > steps - 10 && deepest > worstDeepest)
				worstDeepest = deepest;
		}

		printf("%-34s %8ld %8ld %8ld %8ld %9ld %8d\n", kRegime[r].label, at1,
			at10, at30, last, worstDeepest, worstHits);

		/* The ordinary anchor's settled chain, for the picture (I1). */
		if (!anchor && r == g_braidRestWant)
		{
			memcpy(g_braidRest, pos, sizeof(g_braidRest));
			g_braidRestHave = true;
		}

		/* Where the six links have come to rest. A regime that settles still
		 * has to settle in the right PLACE - a pack sphere that shoves the
		 * braid out sideways would read zero here and look wrong in the
		 * game. */
		strcpy(&rest[r][0], "");

		for (int i = 1; i < 7; i++)
			sprintf(&rest[r][strlen(rest[r])], " %ld/%ld/%ld", pos[i][0],
				pos[i][1], pos[i][2]);
	}

	printf("\nwhere the six links stand at the end:\n");

	for (int r = 0; r < 8; r++)
		printf("  %-34s%s\n", kRegime[r].label, rest[r]);

	}

	printf("\nthe four numbers are how far the busiest link still moves in one "
		"step\n(|dx|+|dy|+|dz|, in classic units); a settled braid reads 0 and "
		"stays there.\n\"in a pack\" is the deepest any link stands inside the "
		"pack BOX over the last\nten steps, measured whether or not that "
		"regime switched the box on - so a\nregime without it says how far "
		"the braid walks into the backpack.\n");
	printf("\"packhits\" is the twin of the game's own HAIRLOG field: how many "
		"links the\npack pushed out in one step.\n");
	printf("no wind, no water line and no floor: a braid that will not settle "
		"standing\nstill in a dry indoor room will not settle anywhere.\n");
}

/* --------------------------------------- the braid under an animation -- */
/*
 * THE SAME SIMULATION, DRIVEN BY A REAL ANIMATION (#123, fix round G, G1).
 *
 * Fix round F ran the chain in the BIND POSE and found it settles. The user
 * still saw the braid move, and he was right to: a bind pose has no anchor
 * motion at all, and in the game every one of the eight collision spheres
 * rides on Lara's own skeleton. Even standing at a door she breathes -
 * ANIM_BREATH moves her chest and her head a unit or two a frame - and the
 * moment she walks the whole rig travels.
 *
 * So this reads the level's OWN animation table, decodes the frames Lara
 * really wears, and rebuilds HairControl's node walk out of them once per
 * game frame: the hips out of the frame's own offset and node 0's rotation,
 * the torso by bone 7, the two upper arms by bones 8 and 11, the head by bone
 * 14, and the braid root 48 units behind the skull - the same order, the same
 * skips through the packed rotation stream (game\draw.cpp,
 * gar_RotYXZsuperpack), the same three hand-tuned offsets. Frames between two
 * key frames are blended frac/rate, which is what InterpolateMatrix does to
 * the matrix, and the positions are truncated to longs afterwards, which is
 * what `(long)aMXPtr[M03]` does.
 *
 * What comes out is the twin of the game's own HAIRLOG: how far the TIP moved
 * this frame and how far the ROOT moved, in the same four buckets the play
 * test's log was counted in (under 2, 2 to 10, 10 to 40, over 40 units). One
 * row per variant, so the pack's cost is a number and not an opinion.
 *
 * Still left out, and still for the reason fix round F gave: the wind (an
 * outdoor room, and a translation of the whole chain), and the water line. The
 * FLOOR is in, because with a real anchor at a real height it matters - a
 * braid 336 units long hanging from a head 251 units up reaches through the
 * ground she stands on, and the game's clamp (snap y to the floor, put x and z
 * back where they were) is itself a discontinuity that could shake a chain.
 * -braidfloor takes it out again for a comparison.
 */

static void MatRotZ(double deg, float* m)
{
	const double a = deg * 3.14159265358979323846 / 180.0;
	const float c = (float)cos(a), s = (float)sin(a);

	MatIdentity(m);
	m[0] = c;  m[1] = s;
	m[4] = -s; m[5] = c;
}

/* A TR rotation word is 65536 units to the circle. */
static double BraidDeg(int16_t angle)
{
	return (double)angle * 360.0 / 65536.0;
}

/* game\lara_states.h, ANIM_BREATH: Lara standing still and breathing, which
 * is the animation the play test's own two video frames were shot in. */
#define ANIM_BREATH_NUMBER	103

struct BraidAnimRow
{
	uint32_t frameOffset = 0;
	int rate = 1, size = 0, start = 0, end = 0;
	long velocity = 0;
};

static bool BraidReadAnim(const Tr5Level& level, int index, BraidAnimRow& out)
{
	size_t at = 0;
	int32_t n = 0;

	if (!level.debugAnimBlock(at, n) || index < 0 || index >= n)
		return false;

	const std::vector<uint8_t>& bytes = level.debugBytes();

	if (at + (size_t)(index + 1) * 40 > bytes.size())
		return false;

	const uint8_t* e = bytes.data() + at + (size_t)index * 40;
	int16_t s = 0;
	int32_t l = 0;

	memcpy(&out.frameOffset, e + 0, 4);
	out.rate = e[4];
	out.size = e[5];
	memcpy(&l, e + 8, 4);
	out.velocity = l;
	memcpy(&s, e + 24, 2);
	out.start = s;
	memcpy(&s, e + 26, 2);
	out.end = s;

	return out.rate > 0 && out.size > 0 && out.end >= out.start;
}

/* The packed rotation stream of one frame, and a cursor into it. One word when
 * the top two bits name a single axis, two when they are zero and three ten
 * bit angles follow - game\draw.cpp, gar_RotYXZsuperpack. */
struct BraidRotStream
{
	const Tr5Level* level;
	long long at;
};

static void BraidRotSkip(BraidRotStream& r, int skip)
{
	while (skip-- > 0)
	{
		const uint16_t w = (uint16_t)r.level->debugReadFrameShort(r.at);

		r.at += (w & 0xC000) ? 1 : 2;
	}
}

/* Applies the node's rotation to `m` and steps the cursor. `m` is row major
 * and a row vector's, which is the transpose of the engine's - so a local
 * rotation multiplies from the LEFT, and phd_RotYXZ's Y then X then Z order
 * comes out in that order here too. */
static void BraidRotApply(BraidRotStream& r, float* m)
{
	const uint16_t w = (uint16_t)r.level->debugReadFrameShort(r.at);
	float step[16], tmp[16];

	if ((w >> 14) == 0)
	{
		const uint16_t w2 = (uint16_t)r.level->debugReadFrameShort(r.at + 1);
		const uint32_t packed = ((uint32_t)w << 16) | (uint32_t)w2;
		const int16_t ay = (int16_t)(((packed >> 10) & 0x3FF) << 6);
		const int16_t ax = (int16_t)(((packed >> 20) & 0x3FF) << 6);
		const int16_t az = (int16_t)((packed & 0x3FF) << 6);

		if (ay)
		{
			MatRotY(BraidDeg(ay), step);
			MatMul(step, m, tmp);
			memcpy(m, tmp, sizeof(tmp));
		}

		if (ax)
		{
			MatRotX(BraidDeg(ax), step);
			MatMul(step, m, tmp);
			memcpy(m, tmp, sizeof(tmp));
		}

		if (az)
		{
			MatRotZ(BraidDeg(az), step);
			MatMul(step, m, tmp);
			memcpy(m, tmp, sizeof(tmp));
		}

		r.at += 2;
		return;
	}

	const int16_t a = (int16_t)((w & 0xFFF) << 4);

	if ((w >> 14) == 1)
		MatRotX(BraidDeg(a), step);
	else if ((w >> 14) == 2)
		MatRotY(BraidDeg(a), step);
	else
		MatRotZ(BraidDeg(a), step);

	MatMul(step, m, tmp);
	memcpy(m, tmp, sizeof(tmp));
	r.at += 1;
}

static void BraidTranslate(float* m, double x, double y, double z)
{
	float step[16], tmp[16];

	MatTranslate((float)x, (float)y, (float)z, step);
	MatMul(step, m, tmp);
	memcpy(m, tmp, sizeof(tmp));
}

/* The nine points HairControl reads off the mesh tree, in one key frame:
 * sphere 0..4 and 6..7 as the function builds them, plus the braid root. The
 * neck (sphere 5) is derived from two of the others and is left to the caller,
 * because the game derives it from the TRUNCATED longs. */
struct BraidFramePoints
{
	double sphere[8][3];
	double root[3];
	/* THE TORSO NODE'S WHOLE FRAME (#123, fix round I, I1). A sphere needs a
	 * centre and a box needs an orientation, so the pack's frame is carried
	 * out of here as it stands - row major, the tool's own convention, which
	 * is the transpose of the engine's. */
	double torso[16];
};

static void BraidOrigin(const float* m, double* out)
{
	out[0] = m[12];
	out[1] = m[13];
	out[2] = m[14];
}

/* `rootZ` is which of the two braid roots this pose hangs the chain from - the
 * classic -48, or the -33 fix round D wanted and fix round F withdrew. It is a
 * parameter since fix round I (I2), because the question "does the near root
 * settle now that the pack is a box" cannot be asked without it. */
static void BraidPoseAt(const Tr5Level& level, const ClassicSlot& body,
	const int16_t mesh[5][6], const BraidAnimRow& a, int keyframe,
	const long torsoOff[3], long rootZ, BraidFramePoints& out)
{
	const long long base = (long long)(a.frameOffset / 2) +
		(long long)keyframe * a.size;

	BraidRotStream r;
	float m[16], push[16];

	r.level = &level;
	r.at = base + 9;

	MatIdentity(m);
	BraidTranslate(m, level.debugReadFrameShort(base + 6),
		level.debugReadFrameShort(base + 7),
		level.debugReadFrameShort(base + 8));

	BraidRotApply(r, m);			// node 0, the hips

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, mesh[0][0], mesh[0][1], mesh[0][2]);
	BraidOrigin(push, out.sphere[0]);

	BraidTranslate(m, body.delta[7][0], body.delta[7][1], body.delta[7][2]);
	BraidRotSkip(r, 6);			// the six leg nodes
	BraidRotApply(r, m);			// node 7, the torso
	/* lara.torso_?_rot would go here; they are zero for a Lara who is not
	 * aiming, and the play test's braid trembled with her hands empty. */

	// The pack's own frame, before any mesh offset - game\hair.cpp's
	// HdPackFrame stands exactly here (#123, fix round I, I1).
	for (int i = 0; i < 16; i++)
		out.torso[i] = (double)m[i];

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, mesh[1][0] + torsoOff[0], mesh[1][1] + torsoOff[1],
		mesh[1][2] + torsoOff[2]);
	BraidOrigin(push, out.sphere[1]);

	// The HD pack, in the torso node's own frame - game\hair.cpp, HdPackSpheres
	memcpy(push, m, sizeof(push));
	BraidTranslate(push, HD_LARA_PACK_X, HD_LARA_PACK_Y_TOP, HD_LARA_PACK_Z);
	BraidOrigin(push, out.sphere[6]);

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, HD_LARA_PACK_X, HD_LARA_PACK_Y_BOTTOM, HD_LARA_PACK_Z);
	BraidOrigin(push, out.sphere[7]);

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, body.delta[8][0], body.delta[8][1], body.delta[8][2]);
	BraidRotApply(r, push);			// node 8, the right upper arm
	BraidTranslate(push, mesh[3][0], mesh[3][1], mesh[3][2]);
	BraidOrigin(push, out.sphere[3]);

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, body.delta[11][0], body.delta[11][1], body.delta[11][2]);
	BraidRotSkip(r, 2);			// the right forearm and hand
	BraidRotApply(r, push);			// node 11, the left upper arm
	BraidTranslate(push, mesh[4][0], mesh[4][1], mesh[4][2]);
	BraidOrigin(push, out.sphere[4]);

	BraidTranslate(m, body.delta[14][0], body.delta[14][1], body.delta[14][2]);
	BraidRotSkip(r, 2);			// the left forearm and hand
	BraidRotApply(r, m);			// node 14, the head
	/* lara.head_?_rot would go here and are zero for the same reason. */

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, mesh[2][0] - 2, mesh[2][1], mesh[2][2]);
	BraidOrigin(push, out.sphere[2]);

	BraidTranslate(m, HD_LARA_BRAID_ROOT_X, HD_LARA_BRAID_ROOT_Y,
		(double)rootZ);
	BraidOrigin(m, out.root);
}

/* The tool's BraidBox out of a pose's torso frame. The tool's matrices are row
 * major and a row vector's - world[c] = sum_r local[r] * m[4r + c] - and the
 * engine's are the transpose of that, so the box's own R[r][c] is m[4c + r].
 * The identity comes out of both roads as the identity, which is the check
 * that matters. */
static void BraidBoxFromFrame(BraidBox& box, const double* m, double travelZ)
{
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			box.rot[3 * r + c] = m[4 * c + r];

	box.org[0] = m[12];
	box.org[1] = m[13];
	box.org[2] = m[14] + travelZ;
}

/* WHAT THE PACK MAY DO TO A LINK. `spheres` 6 leaves it out of the loop
 * altogether; the rest is what fix round G measured against it. */
struct BraidAnimVariant
{
	const char* label;
	int spheres;		// 6 = the classic body alone, 8 = with the pack
	long packR;		// the pack's radius
	long packZ;		// where its centre sits behind the torso joint
	bool packDamps;		// a pack push-out kills the link's velocity
	// #123 fix round I: the BOX instead, and which root it hangs from.
	bool box;
	long rootZ;
	int bias;		// how much the box prefers its rear face
};

/* One step of the chain with a moving anchor. The same loop BraidStep runs,
 * plus the game's floor clamp, and it reports how far the tip travelled. */
static long BraidAnimStep(long pos[7][3], long vel[7][3], const long root[3],
	const long length[7], const BraidSphere* sphere, const BraidAnimVariant& v,
	const BraidBox& box, long floorY, bool useFloor, int* packHits,
	long* deepest)
{
	const long tipWas[3] = { pos[6][0], pos[6][1], pos[6][2] };

	*packHits = 0;

	pos[0][0] = root[0];
	pos[0][1] = root[1];
	pos[0][2] = root[2];

	for (int i = 1; i < 7; i++)
	{
		const long was[3] = { pos[i][0], pos[i][1], pos[i][2] };
		const long before[3] = { pos[i][0], pos[i][1], pos[i][2] };
		bool packPushed = false;

		pos[i][0] += 3 * vel[i][0] / 4;
		pos[i][1] += 3 * vel[i][1] / 4;
		pos[i][2] += 3 * vel[i][2] / 4;

		pos[i][1] += 10;	// gravity

		/* The floor, as game\hair.cpp clamps it: x and z go back where they
		 * were and y snaps to the ground, but only within one click. */
		if (useFloor && pos[i][1] > floorY)
		{
			pos[i][0] = before[0];

			if (pos[i][1] - floorY <= 256)
				pos[i][1] = floorY;

			pos[i][2] = before[2];
		}

		for (int j = 0; j < v.spheres; j++)
		{
			const long x = pos[i][0] - sphere[j].x;
			const long y = pos[i][1] - sphere[j].y;
			const long z = pos[i][2] - sphere[j].z;
			const long long d = (long long)x * x + (long long)y * y +
				(long long)z * z;

			if (d >= (long long)sphere[j].r * sphere[j].r)
				continue;

			long dist = BraidSqrt(d);

			if (!dist)
				dist = 1;

			pos[i][0] = sphere[j].x + x * sphere[j].r / dist;
			pos[i][1] = sphere[j].y + y * sphere[j].r / dist;
			pos[i][2] = sphere[j].z + z * sphere[j].r / dist;

			if (j >= 6)
			{
				(*packHits)++;
				packPushed = true;
			}
		}

		// ... and out of the box, last, exactly as the game does it (I1).
		if (BraidBoxPush(box, pos[i], i))
		{
			(*packHits)++;
			packPushed = true;
		}

		const double dx = (double)(pos[i][0] - pos[i - 1][0]);
		const double dy = (double)(pos[i][1] - pos[i - 1][1]);
		const double dz = (double)(pos[i][2] - pos[i - 1][2]);
		double len = sqrt(dx * dx + dy * dy + dz * dz);

		if (len < 1.0e-6)
			len = 1.0;

		const double step = (double)length[i] / len;

		pos[i][0] = pos[i - 1][0] + (long)floor(dx * step + 0.5);
		pos[i][1] = pos[i - 1][1] + (long)floor(dy * step + 0.5);
		pos[i][2] = pos[i - 1][2] + (long)floor(dz * step + 0.5);

		if (v.packDamps && packPushed)
		{
			vel[i][0] = 0;
			vel[i][1] = 0;
			vel[i][2] = 0;
		}
		else
		{
			vel[i][0] = pos[i][0] - was[0];
			vel[i][1] = pos[i][1] - was[1];
			vel[i][2] = pos[i][2] - was[2];
		}
	}

	/* How deep a link stands in the pack BOX (#123, fix round I, I1), measured
	 * whether or not this variant switched the box on. */
	if (deepest)
	{
		*deepest = 0;

		for (int i = 1; i < 7; i++)
		{
			const long in = BraidBoxDepth(box, pos[i], i, 0, 0);

			if (in > *deepest)
				*deepest = in;
		}
	}

	return labs(pos[6][0] - tipWas[0]) + labs(pos[6][1] - tipWas[1]) +
		labs(pos[6][2] - tipWas[2]);
}

/* THE TWO ANGLES HairControl WRITES for a link, in degrees.
 *
 * game\hair.cpp gives link n-1 the spherical angles of the vector from it to
 * link n - phd_atan(dz, dx) around y and -phd_atan(sqrt(dz^2+dx^2), dy) around
 * x - and hd_laradraw.cpp's braid_matrices builds the DRAWN bone out of those
 * two. The positions they are taken from are longs, so for a braid hanging
 * straight down dx and dz are single digits and the yaw is whatever the
 * rounding of two of them says: the pitch is stable at -90 and the yaw is not
 * an angle at all but noise. Whether that noise reaches the picture is what
 * the dyaw column below measures.
 */
static void BraidAngles(const long pos[7][3], double* yaw, double* pitch)
{
	for (int i = 0; i < 6; i++)
	{
		const double dx = (double)(pos[i + 1][0] - pos[i][0]);
		const double dy = (double)(pos[i + 1][1] - pos[i][1]);
		const double dz = (double)(pos[i + 1][2] - pos[i][2]);
		const double flat = sqrt(dx * dx + dz * dz);

		yaw[i] = atan2(dx, dz) * 180.0 / 3.14159265358979323846;
		pitch[i] = -atan2(dy, flat) * 180.0 / 3.14159265358979323846;
	}
}

/* WHAT THE DRAWN BONE IS TURNED BY ABOUT ITS OWN AXIS, under four rules
 * (#123, fix round G, G1).
 *
 * A rotation about the bone's own +z moves no vertex out of the braid and
 * lands the next bone in exactly the same place, so the roll is free and the
 * only question is which value of it does not tremble. `eff` comes back as the
 * azimuth the picture really shows, in degrees, so a rule is judged by how far
 * that moves from frame to frame. Lara faces +z in the tool, so her own facing
 * is 0.
 *
 *   NONE    what shipped up to fix round F: whatever phd_atan made of two
 *           single-digit integers.
 *   FACING  every link pulled towards her facing, by a weight that fades out
 *           as the link leaves the vertical.
 *   CHAIN   every link turned to the azimuth of the WHOLE braid, root to tip.
 *           That vector is 336 units long where a link is 56, so its own
 *           horizontal part is ten to thirty times better conditioned, and one
 *           reference for all six bones means they can never twist against
 *           each other - which is the thing a rigid six bone plait cannot
 *           hide.
 *   CHAINW  the same, faded out per link like FACING.
 */
enum
{
	BRAID_ROLL_NONE = 0,
	BRAID_ROLL_FACING,
	BRAID_ROLL_CHAIN,
	BRAID_ROLL_CHAINW,
	BRAID_ROLL_LOCK,
	BRAID_ROLL_FACING_ALL,
	BRAID_ROLL_COUNT
};

static const char* const kBraidRollName[BRAID_ROLL_COUNT] =
{
	"none - the raw yaw, as fix round F ships it",
	"towards her facing, faded per link",
	"the whole chain's azimuth, every link",
	"the whole chain's azimuth, faded per link",
	"the chain's azimuth, held while it is vertical",
	"her facing, every link, always"
};

/* THE HELD REFERENCE of BRAID_ROLL_LOCK. The chain's own azimuth is a good
 * angle while the braid has a horizontal part and noise while it hangs
 * straight; so it is followed when it is worth following and simply held
 * where it is when it is not. One value, and the only state in the whole
 * rule. */
static double g_braidLock = 0.0;
static bool g_braidLockSeen = false;

#define BRAID_LOCK_NEAR	16.0
#define BRAID_LOCK_FAR	48.0

static double BraidWrap(double a)
{
	while (a > 180.0)
		a -= 360.0;

	while (a < -180.0)
		a += 360.0;

	return a;
}

static void BraidRollFix(const long pos[7][3], const double* yaw, double* eff,
	int rule)
{
	const double chainX = (double)(pos[6][0] - pos[0][0]);
	const double chainZ = (double)(pos[6][2] - pos[0][2]);
	double reference = 0.0;

	if (rule == BRAID_ROLL_CHAIN || rule == BRAID_ROLL_CHAINW ||
		rule == BRAID_ROLL_LOCK)
		reference = atan2(chainX, chainZ) * 180.0 / 3.14159265358979323846;

	if (rule == BRAID_ROLL_LOCK)
	{
		const double flat = sqrt(chainX * chainX + chainZ * chainZ);
		double weight;

		if (flat <= BRAID_LOCK_NEAR)
			weight = 0.0;
		else if (flat >= BRAID_LOCK_FAR)
			weight = 1.0;
		else
			weight = (flat - BRAID_LOCK_NEAR) /
				(BRAID_LOCK_FAR - BRAID_LOCK_NEAR);

		if (!g_braidLockSeen)
		{
			g_braidLock = reference;
			g_braidLockSeen = true;
		}
		else
			g_braidLock += BraidWrap(reference - g_braidLock) * weight;

		reference = g_braidLock;
	}

	for (int i = 0; i < 6; i++)
	{
		if (rule == BRAID_ROLL_NONE)
		{
			eff[i] = yaw[i];
			continue;
		}

		if (rule == BRAID_ROLL_CHAIN || rule == BRAID_ROLL_LOCK)
		{
			eff[i] = reference;
			continue;
		}

		if (rule == BRAID_ROLL_FACING_ALL)
		{
			eff[i] = 0.0;	// her facing, and she faces +z in the tool
			continue;
		}

		const double dx = (double)(pos[i + 1][0] - pos[i][0]);
		const double dz = (double)(pos[i + 1][2] - pos[i][2]);
		const double flat = sqrt(dx * dx + dz * dz);
		double weight;

		if (flat <= (double)HD_LARA_BRAID_ROLL_NEAR)
			weight = 1.0;
		else if (flat >= (double)HD_LARA_BRAID_ROLL_FAR)
			weight = 0.0;
		else
			weight = ((double)HD_LARA_BRAID_ROLL_FAR - flat) /
				(double)(HD_LARA_BRAID_ROLL_FAR - HD_LARA_BRAID_ROLL_NEAR);

		eff[i] = yaw[i] + BraidWrap(reference - yaw[i]) * weight;

		if (rule == BRAID_ROLL_FACING)
			reference = eff[i];
	}
}

static double BraidAngleStep(double a, double b)
{
	double d = a - b;

	while (d > 180.0)
		d -= 360.0;

	while (d < -180.0)
		d += 360.0;

	return fabs(d);
}

static void BraidBucket(long step, int* into)
{
	if (step < 2)
		into[0]++;
	else if (step < 10)
		into[1]++;
	else if (step < 40)
		into[2]++;
	else
		into[3]++;
}

static void PrintBraidAnim(const Tr5Level& level, int bodySlot,
	const ClassicSlot& body, const ClassicSlot& hairTree, bool haveHair,
	int steps, const int* anims, int animCount, bool useFloor)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
		return;

	Tr5Level::MoveableInfo info;

	if (!level.moveableInfo(bodySlot, info))
		return;

	Tr5Level::MoveableInfo laraInfo;

	if (!level.moveableInfo(0 /* LARA */, laraInfo))
	{
		printf("\n-braidanim: this level has no LARA entry, so no animations.\n");
		return;
	}

	static const int kNode[5] = { 0, 7, 14, 8, 11 };
	int16_t mesh[5][6];

	for (int i = 0; i < 5; i++)
		if (!level.debugMeshHeader(info.meshIndex + kNode[i], mesh[i]))
		{
			printf("\n-braidanim: mesh %d of %s is not readable.\n", kNode[i],
				HDSlotName(bodySlot));
			return;
		}

	long length[7];

	for (int i = 1; i < 7; i++)
	{
		const int n = i < 6 ? i : 5;

		length[i] = haveHair && n < hairTree.nodes
			? (long)(hairTree.bind[n][2] - hairTree.bind[n - 1][2])
			: 56;
	}

	/* #123 fix round I: f, g and h are the new ones. f is what ships - the box
	 * at the classic root; g asks I2's question, the box with the root moved
	 * up into the hair knot; h is the same box with no preference for its rear
	 * face, which is the measurement HD_LARA_PACK_BOX_BIAS rests on. */
	static const int kVariants = 8;
	static const BraidAnimVariant kVariant[kVariants] =
	{
		{ "a  the classic body alone (6 spheres)", 6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "b  round F/G as shipped (r36 at z-52)", 8, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "c  the pack damps instead of feeding",  8, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, true,  false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "d  a thinner pack, same rear face",     8, 24, -64, false,
		  false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "e  thinner still, same rear face",      8, 18, -70, false,
		  false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "f  round I: the BOX, root -48",         6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, true,  HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_PACK_BOX_BIAS },
		{ "g  round I: the BOX, root -33",         6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, true,  HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_PACK_BOX_BIAS },
		{ "h  the BOX, no rear preference",        6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, true,  HD_LARA_BRAID_ROOT_Z, 1 }
	};

	static const long kTorsoOff[3] = { -9, 0, 25 };

	printf("\n-- the braid, driven by Lara's own animations ----------------\n");
	printf("%d frames per animation, the floor is %s\n", steps,
		useFloor ? "in (y = her feet, as the game clamps it)" : "OUT");
	printf("the four counts are the TIP's travel in one frame, in the same "
		"buckets\nthe play test's HAIRLOG was counted in: under 2, 2 to 10, "
		"10 to 40, over 40.\n");

	for (int ai = 0; ai < animCount; ai++)
	{
		BraidAnimRow a;

		if (!BraidReadAnim(level, laraInfo.animIndex + anims[ai], a))
		{
			printf("\nanim %d: not in this level's table.\n", anims[ai]);
			continue;
		}

		const int frames = a.end - a.start + 1;
		const int keyframes = (a.end - a.start) / a.rate + 1;
		const long move = a.velocity >> 16;

		printf("\n=== anim %d: %d frames, rate %d, frame size %d, %d key "
			"frames, forward %ld/frame ===\n", anims[ai], frames, a.rate,
			a.size, keyframes, move);
		/* The roll rules change no position, so they are the same question
		 * under every physics variant - but the yaw they have to survive is
		 * NOT, because it is read off single-digit integers and those differ.
		 * So the worst of the five is what is reported. */
		double rollWorst[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };
		int rollOver[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };

		printf("%-38s %6s %6s %6s %6s %6s %7s %6s %8s %7s %7s %7s\n",
			"variant", "tip<2", "2-10", "10-40", ">40", "worst", "meantip",
			"inpack", "packhits", "short", "dyaw", "dyaw>30");
		printf("%-38s %6s %6s %6s %6s %6s %7s %6s %8s %7s %7s %7s\n",
			"--------------------------------------", "------", "------",
			"------", "------", "------", "-------", "------", "--------",
			"-------", "-------", "-------");

		int rootBucket[4] = { 0, 0, 0, 0 };
		long rootWorst = 0;

		for (int vi = 0; vi < kVariants; vi++)
		{
			const BraidAnimVariant& v = kVariant[vi];
			BraidBox box;
			BraidSphere sphere[8];
			long pos[7][3], vel[7][3];
			int bucket[4] = { 0, 0, 0, 0 };
			long worst = 0, sum = 0, worstDeep = 0;
			int worstHits = 0;
			bool seeded = false;
			long lastRoot[3] = { 0, 0, 0 };
			double yaw[6], pitch[6], lastYaw[6];
			double eff[BRAID_ROLL_COUNT][6], lastEff[BRAID_ROLL_COUNT][6];
			double worstYaw = 0;
			double worstRule[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };
			int yawJumps = 0;
			int ruleJumps[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };
			long shortest = 1 << 30;

			memset(vel, 0, sizeof(vel));
			memset(pos, 0, sizeof(pos));
			memset(lastYaw, 0, sizeof(lastYaw));
			memset(lastEff, 0, sizeof(lastEff));
			memset(eff, 0, sizeof(eff));
			g_braidLockSeen = false;
			BraidBoxIdentity(box, 0.0, 0.0, 0.0);
			box.on = v.box;
			box.bias = v.bias;

			for (int s = 0; s < steps; s++)
			{
				const int frame = s % frames;
				const int key = frame / a.rate;
				const int frac = frame % a.rate;
				BraidFramePoints A, B;

				BraidPoseAt(level, body, mesh, a, key, kTorsoOff, v.rootZ, A);

				double w = 0.0;

				if (frac && key + 1 < keyframes)
				{
					BraidPoseAt(level, body, mesh, a, key + 1, kTorsoOff,
						v.rootZ, B);
					w = (double)frac / (double)a.rate;
				}
				else
					B = A;

				/* Where the item itself has got to. An animation that carries
				 * Lara forward moves every sphere with her, and that is most
				 * of the anchor motion the play test's log counted. */
				const double travel = (double)move * (double)s;

				for (int j = 0; j < 8; j++)
				{
					sphere[j].x = (long)(A.sphere[j][0] +
						(B.sphere[j][0] - A.sphere[j][0]) * w);
					sphere[j].y = (long)(A.sphere[j][1] +
						(B.sphere[j][1] - A.sphere[j][1]) * w);
					sphere[j].z = (long)(A.sphere[j][2] +
						(B.sphere[j][2] - A.sphere[j][2]) * w + travel);
				}

				sphere[0].r = mesh[0][3];
				sphere[1].r = mesh[1][3];
				sphere[2].r = mesh[2][3];
				sphere[3].r = 4 * mesh[3][3] / 3;
				sphere[4].r = 4 * mesh[4][3] / 3;

				sphere[5].x = (2 * sphere[2].x + sphere[1].x) / 3;
				sphere[5].y = (2 * sphere[2].y + sphere[1].y) / 3;
				sphere[5].z = (2 * sphere[2].z + sphere[1].z) / 3;
				sphere[5].r = 5 * sphere[2].r / 4;

				sphere[6].r = v.packR;
				sphere[7].r = v.packR;
				sphere[6].z += v.packZ - HD_LARA_PACK_Z;
				sphere[7].z += v.packZ - HD_LARA_PACK_Z;

				/* THE PACK BOX'S FRAME (#123, fix round I, I1), blended
				 * between the two key frames component by component - which is
				 * what the engine's own InterpolateMatrix does to a matrix, so
				 * a slightly non-orthonormal blend is not an approximation
				 * here but the thing itself. */
				{
					double frame16[16];

					for (int i = 0; i < 16; i++)
						frame16[i] = A.torso[i] +
							(B.torso[i] - A.torso[i]) * w;

					BraidBoxFromFrame(box, frame16, travel);
				}

				const long root[3] =
				{
					(long)(A.root[0] + (B.root[0] - A.root[0]) * w),
					(long)(A.root[1] + (B.root[1] - A.root[1]) * w),
					(long)(A.root[2] + (B.root[2] - A.root[2]) * w + travel)
				};

				if (!seeded)
				{
					/* first_hair: the chain hangs straight down from the
					 * root, every velocity zero. */
					long down = 0;

					for (int i = 0; i < 7; i++)
					{
						pos[i][0] = root[0];
						pos[i][1] = root[1] + down;
						pos[i][2] = root[2];

						if (i < 6)
							down += length[i + 1];
					}

					seeded = true;
					lastRoot[0] = root[0];
					lastRoot[1] = root[1];
					lastRoot[2] = root[2];
					BraidAngles(pos, lastYaw, pitch);

					for (int r = 0; r < BRAID_ROLL_COUNT; r++)
						BraidRollFix(pos, lastYaw, lastEff[r], r);

					continue;
				}

				int hits = 0;
				long deep = 0;
				const long moved = BraidAnimStep(pos, vel, root, length,
					sphere, v, box, 0 /* her feet */, useFloor, &hits, &deep);

				BraidAngles(pos, yaw, pitch);

				for (int r = 0; r < BRAID_ROLL_COUNT; r++)
					BraidRollFix(pos, yaw, eff[r], r);

				/* The first ten steps are the seed settling, not the
				 * animation - the game has been running for hours by the
				 * time the play test looks at her. */
				if (s < 10)
				{
					lastRoot[0] = root[0];
					lastRoot[1] = root[1];
					lastRoot[2] = root[2];
					memcpy(lastYaw, yaw, sizeof(lastYaw));
					memcpy(lastEff, eff, sizeof(lastEff));
					continue;
				}

				double frameYaw = 0;

				for (int i = 0; i < 6; i++)
				{
					const double d = BraidAngleStep(yaw[i], lastYaw[i]);

					if (d > frameYaw)
						frameYaw = d;
				}

				for (int r = 0; r < BRAID_ROLL_COUNT; r++)
				{
					double worstHere = 0;

					for (int i = 0; i < 6; i++)
					{
						const double e = BraidAngleStep(eff[r][i],
							lastEff[r][i]);

						if (e > worstHere)
							worstHere = e;
					}

					if (worstHere > worstRule[r])
						worstRule[r] = worstHere;

					if (worstHere > 30.0)
						ruleJumps[r]++;
				}

				memcpy(lastYaw, yaw, sizeof(lastYaw));
				memcpy(lastEff, eff, sizeof(lastEff));

				if (frameYaw > worstYaw)
					worstYaw = frameYaw;

				if (frameYaw > 30.0)
					yawJumps++;

				const long span = (long)sqrt(
					(double)(pos[6][0] - pos[0][0]) * (pos[6][0] - pos[0][0]) +
					(double)(pos[6][1] - pos[0][1]) * (pos[6][1] - pos[0][1]) +
					(double)(pos[6][2] - pos[0][2]) * (pos[6][2] - pos[0][2]));

				if (span < shortest)
					shortest = span;

				BraidBucket(moved, bucket);
				sum += moved;

				if (moved > worst)
					worst = moved;

				if (deep > worstDeep)
					worstDeep = deep;

				if (hits > worstHits)
					worstHits = hits;

				if (!vi)
				{
					const long rs = labs(root[0] - lastRoot[0]) +
						labs(root[1] - lastRoot[1]) +
						labs(root[2] - lastRoot[2]);

					BraidBucket(rs, rootBucket);

					if (rs > rootWorst)
						rootWorst = rs;
				}

				lastRoot[0] = root[0];
				lastRoot[1] = root[1];
				lastRoot[2] = root[2];
			}

			const int counted = bucket[0] + bucket[1] + bucket[2] + bucket[3];

			printf("%-38s %6d %6d %6d %6d %6ld %7.2f %6ld %8d %7ld %7.0f "
				"%7d\n",
				v.label, bucket[0], bucket[1], bucket[2], bucket[3], worst,
				counted ? (double)sum / (double)counted : 0.0, worstDeep,
				worstHits, shortest, worstYaw, yawJumps);

			for (int r = 0; r < BRAID_ROLL_COUNT; r++)
			{
				if (worstRule[r] > rollWorst[r])
					rollWorst[r] = worstRule[r];

				if (ruleJumps[r] > rollOver[r])
					rollOver[r] = ruleJumps[r];
			}
		}

		printf("%-38s %6d %6d %6d %6d %6ld\n",
			"   (the ROOT's own travel, for scale)", rootBucket[0],
			rootBucket[1], rootBucket[2], rootBucket[3], rootWorst);

		printf("\n  %-46s %7s %8s\n",
			"the DRAWN bone's twist, per rule (worst variant)", "worst",
			"over 30");

		for (int r = 0; r < BRAID_ROLL_COUNT; r++)
			printf("  %-46s %7.0f %8d%s\n", kBraidRollName[r],
				rollWorst[r], rollOver[r],
				r == BRAID_ROLL_LOCK ? "   <- what ships" : "");
	}

	printf("\na tip that moves no further than its anchor is the braid "
		"following Lara;\nthe question this table answers is whether the two "
		"pack spheres add travel\nof their own, and how much.\n");
	printf("\"short\" is the shortest the chain ever got - root to tip, out of "
		"%ld when it\nhangs straight; anything much under that is the braid "
		"folded up on itself.\n\"dyaw\" is the worst one-frame change of any "
		"link's YAW in degrees and\n\"dyaw>30\" how many frames had one: that "
		"angle is what hd_laradraw turned the\nDRAWN bone by up to fix round "
		"F, and for a braid hanging straight down it is\nread off two "
		"single-digit integers.\n",
		length[1] + length[2] + length[3] + length[4] + length[5] + length[6]);
	printf("\nthe roll table is the cure: a rotation about the bone's OWN axis "
		"moves no\nvertex and lands the next bone in exactly the same place, "
		"so the only\nquestion is which value of it does not tremble. Beware "
		"the last line - Lara\nnever turns in this tool, so \"her facing, "
		"always\" cannot move here and its\nzero is an empty one.\n");
}

/* ---------------------------------------------------- Lara's composite -- */
/* Stage S4-1. Everything the tool needs to know about one wardrobe picture;
 * a struct rather than eighteen arguments. */
/* #123 fix round D (D2): one part's sheen into the lighting block. `on` is
 * false for -nosheen, which is the BEFORE picture. */
static void SetSheen(HDModelLighting& lighting, int part, bool on)
{
	float strength = 0.0f, power = 1.0f;

	HDLaraPartSheen(part, &strength, &power);

	lighting.specStrength = on ? strength : 0.0f;
	lighting.specPower = power;
}

struct LaraOptions
{
	std::string root;
	std::string stem;
	std::string outfit;
	std::string guns;
	std::string holsters;		// "", "full", "empty", "uzi", "revolver"
	std::string look;		// "" = the whole figure, "head" = the head joint
	std::string back;
	std::string out;
	bool holstersFull;
	bool flare;
	bool torch;
	bool nodes;
	bool mirror;
	bool firstPerson;
	bool neck;
	bool braid;			// the braid root table (fix round C, C4)
	int braidSim;			// #123 fix round F (F2): steps, 0 = off
	/* #123 fix round G (G1): the same chain under a real animation. Empty =
	 * off; `braidFloor` is the game's own floor clamp, on by default. */
	std::vector<int> braidAnims;
	bool braidFloor;
	bool classicBraidRoot;		// #123 fix round I (I2): the BEFORE picture
	bool noCap;			// #123 fix round I (I3): the BEFORE picture
	/* #123 fix round I (I4): draw ONE part of the composite and nothing else,
	 * so that a question about one part - "does the sheen reach the hands?" -
	 * is answered by a picture of that part and not by hunting for it in a
	 * picture of the whole of her. -1 draws them all. */
	int onlyPart;
	bool noSheen;			// #123 fix round D (D2): the BEFORE picture
	bool check;
	bool noDraw;
	bool managed;
	int threads;
	long width, height;
	double fov, yaw, pitch, dist, shade;
	double nearCut;			// stage S8, in view units; 0 = off
	double fpForward;		// FP_EYE_OFFSET; 0 is the eye against a wall
	int cull;			// HDModelCull, on her body alone
	bool yawSet, pitchSet;		// did the caller name one?
	const HDObjLight* lights;
	int lightCount;
	const double (*turn)[2];
};

/* The engine's slot number for a name, out of the level that is loaded. Every
 * table in hd_lara speaks names, and this is the one place that turns them
 * back into the numbers the game keeps in LHolster, lara.holster and
 * lara.back_gun. */
static int SlotByName(const Tr5Level& level, const char* want)
{
	const std::vector<int> slots = level.moveableSlots();

	for (size_t i = 0; i < slots.size(); i++)
	{
		const char* name = HDSlotName(slots[i]);

		if (name && !_stricmp(name, want))
			return slots[i];
	}

	return -1;
}

/* The same, WITHOUT asking the level. LHolster, lara.holster and lara.back_gun
 * are plain slot numbers out of the engine's own enum, and the engine writes
 * them from the INVENTORY: setup.cpp puts LARA_HOLSTERS_PISTOLS into both at
 * every level start whether or not the level ever loaded that object. A tool
 * that resolved them through the level's slot list would show an empty thigh
 * in the three VCI levels where the game shows a holster - user finding C3. */
static int SlotNumberByName(const char* want)
{
	for (int slot = 0; slot < HD_SLOT_COUNT; slot++)
	{
		const char* name = HDSlotName(slot);

		if (name && !_stricmp(name, want))
			return slot;
	}

	return -1;
}

/* The classic Lara mesh tree this level really carries. LARA_SKIN is the
 * skinned body the engine draws (laramisc.cpp copies its meshes into LARA at
 * level start), and LARA is the fallback for a level that has no skin - the
 * two trees are identical, which is why TROne accepts either. */
static bool LaraBodySlot(const Tr5Level& level, ClassicSlot& out, int* slotOut)
{
	static const char* kTry[2] = { "LARA_SKIN", "LARA" };

	for (int i = 0; i < 2; i++)
	{
		const int slot = SlotByName(level, kTry[i]);

		if (slot >= 0 && ReadClassicSlot(level, slot, out) &&
			out.nodes >= HD_LARA_NODES)
		{
			if (slotOut)
				*slotOut = slot;

			return true;
		}
	}

	return false;
}

static int RunLara(const LaraOptions& o, const Tr5Level& level)
{
	/* --- the two skeletons the composite is folded against ------------- */
	ClassicSlot body, hair;
	int bodySlot = -1;

	if (!LaraBodySlot(level, body, &bodySlot))
	{
		printf("ModelProbe: this level has no Lara mesh tree - nothing to build\n");
		return 6;
	}

	printf("body    : %s, %d nodes\n", HDSlotName(bodySlot), body.nodes);

	const int hairSlot = SlotByName(level, "HAIR");
	bool haveHair = false;

	if (hairSlot >= 0 && ReadClassicSlot(level, hairSlot, hair) &&
		hair.nodes >= HD_LARA_HAIR_NODES)
	{
		haveHair = true;
		printf("braid   : HAIR, %d nodes, bind", hair.nodes);

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
			printf(" %.0f/%.0f/%.0f", hair.bind[n][0], hair.bind[n][1],
				hair.bind[n][2]);

		printf("\n");
	}
	else
		printf("braid   : this level has no usable HAIR tree - the braid stays classic\n");

	/* --- which outfit --------------------------------------------------- */
	int outfit = HD_LARA_OUTFIT_NONE;
	const char* why = "";

	if (!o.outfit.empty())
	{
		outfit = HDLaraOutfitFromName(o.outfit.c_str());

		if (outfit == HD_LARA_OUTFIT_NONE)
		{
			printf("ModelProbe: %s is no outfit this wardrobe knows\n",
				o.outfit.c_str());
			return 2;
		}

		why = "asked for on the command line";
	}
	else
	{
		/* The engine's own facts, read out of the level the way the engine
		 * reads them:
		 *
		 *   LaraDrawType = data[0] + LARA_NORMAL (specific\file.cpp), so the
		 *   header byte plus one.
		 *
		 *   "she wears the cap" is game\hair.cpp's own test - a Russia level
		 *   with LARA_NORMAL, or LARA_BUNHEAD anywhere. The Russia levels are
		 *   LVL5_BASE .. LVL5_SINKING_SUBMARINE, which are the four JOBY
		 *   files.
		 *
		 *   YOUNG LARA IS THE BYTE AND NOTHING ELSE (#123, stage S7-2). It
		 *   used to be "an ANDY file, or the byte", which was the same
		 *   mistake the engine's own seam carried: the engine builds the
		 *   child body only where the Irish range AND the script flag
		 *   GF_YOUNGLARA meet (game\laramisc.cpp, LaraInitialiseMeshes), the
		 *   randomizer clears that flag and puts an adult skin in the level,
		 *   and every randomized ANDY level was therefore reported classic
		 *   with a grown Lara standing in it. This tool reads the level FILE
		 *   and no SCRIPT.DAT, so the byte is the one fact it has - and it is
		 *   a true one: the randomizer writes the outfit byte and the flag in
		 *   the same breath. Vanilla ANDY1/2/3 carry 1, which is LARA_YOUNG.
		 *
		 *   THE TOMB RAIDER III OUTFIT is data[3] of the level header, passed
		 *   in raw, exactly as specific\hd\hd_laradraw.cpp passes it. 0 in
		 *   every vanilla file. */
		const int drawType = level.laraType() + 1;
		const bool joby = !_strnicmp(o.stem.c_str(), "JOBY", 4);
		const bool andy = !_strnicmp(o.stem.c_str(), "ANDY", 4);
		const bool cap = (joby && drawType == 1) || drawType == 3;

		outfit = HDLaraOutfitFor(drawType, cap, drawType == 2,
			level.hdOutfit(), &why);

		printf("header  : LaraType byte %d -> LaraDrawType %d, data[3] %d%s%s\n",
			level.laraType(), drawType, level.hdOutfit(),
			joby ? ", a Russia level" : "",
			andy ? ", an Ireland level" : "");
	}

	printf("outfit  : %s (%s) - %s\n",
		HDLaraOutfitModel(outfit) ? HDLaraOutfitModel(outfit) : "none",
		HDLaraOutfitLabel(outfit), why);

	if (outfit == HD_LARA_OUTFIT_NONE)
	{
		printf("\nnothing to draw - and that is the answer this level gives.\n");
		return 0;
	}

	/* --- plan, load, build ---------------------------------------------- */
	/* Which of Lara's slots this level really loaded - the same list the
	 * engine would build out of objects[n].loaded, and what keeps the
	 * wardrobe down to the weapons the level can actually hand her. */
	std::vector<int> present;

	{
		const std::vector<int> all = level.moveableSlots();

		for (size_t i = 0; i < all.size(); i++)
			if (HDSlotIsLara(all[i]))
				present.push_back(all[i]);
	}

	HDLaraLevel wardrobe;

	wardrobe.outfit = outfit;
	wardrobe.bodyBind = &body.bind[0][0];
	wardrobe.bodyNodes = HD_LARA_NODES;
	wardrobe.hairBind = haveHair ? &hair.bind[0][0] : 0;
	wardrobe.hairNodes = haveHair ? HD_LARA_HAIR_NODES : 0;
	wardrobe.slots = present.empty() ? 0 : &present[0];
	wardrobe.slotCount = (int)present.size();

	printf("slots   : %d of Lara's own slots loaded in this level\n",
		(int)present.size());

	HDModelSlot wanted[HD_LARA_PART_COUNT];
	const int count = HDLaraPlan(&wardrobe, wanted, HD_LARA_PART_COUNT);

	printf("wardrobe: %d parts planned\n", count);

	HDModelStats stats;
	const char* trouble = "";

	if (!HDModelLevelBegin(g_device, o.root.c_str(), wanted, count, o.managed,
		o.threads, &stats, &trouble))
	{
		printf("ModelProbe: HDModelLevelBegin failed - %s\n", trouble);
		return 5;
	}

	HDLaraStats laraStats;
	const bool ok = HDLaraLevelBuild(&wardrobe, &laraStats);

	printf("\n-- what was loaded -------------------------------------------\n");
	printf("parts              %d wanted, %d on the card, %d missing\n",
		laraStats.wanted, laraStats.loaded, laraStats.missing);
	printf("geometry           %d vertices, %d indices, %d draw calls\n",
		laraStats.vertices, laraStats.indices, laraStats.ranges);
	printf("buffers            %.1f KiB vertices + %.1f KiB indices = %.1f KiB\n",
		laraStats.vertexBytes / 1024.0, laraStats.indexBytes / 1024.0,
		(laraStats.vertexBytes + laraStats.indexBytes) / 1024.0);
	printf("pages              %d distinct in the cache, %.1f MiB, %.0f ms decode\n",
		stats.pages, stats.pageStats.bytes / 1048576.0, stats.pageStats.decodeMs);
	printf("time               %.0f ms reading the .TRM files, %.0f ms building\n",
		stats.readMs, stats.buildMs);
	printf("skeleton drift     %.0f mean over %d skinned parts, worst %.0f "
		"(the limit for an object is %.0f)\n",
		laraStats.driftMean, laraStats.driftParts, laraStats.driftWorst,
		(double)HD_SKELETON_DRIFT_LIMIT);

	for (int i = 0; i < HDModelNoteCount(); i++)
		printf("  note: %s\n", HDModelNote(i));

	for (int i = 0; i < HDLaraNoteCount(); i++)
		printf("  lara: %s\n", HDLaraNote(i));

	/* Part by part, because a mean drift hides the one part that does not
	 * belong - and because the coverage column is the answer to "which node
	 * does this file actually paint". */
	printf("\n%-26s %-28s %7s %7s %7s %s\n", "part", "model", "verts",
		"drift", "worst", "nodes");
	printf("-------------------------- ---------------------------- "
		"------- ------- ------- ------------------------\n");

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		if (!HDLaraPartModel(outfit, part) || !HDLaraPartPlanned(part))
			continue;

		const int pslot = HDLaraPartSlot(part);

		if (!HDLaraHasPart(part))
		{
			printf("%-26s %-28s %7s %7s %7s %s\n", HDLaraPartLabel(part),
				HDLaraPartModel(outfit, part), "-", "-", "-", "NOT LOADED");
			continue;
		}

		/* No number means there was no PAIR of nodes to measure - a part that
		 * covers one node, or the rigid face. The drift is a pairwise
		 * measurement and one node has no pair. */
		float mean = -1, worst = -1;
		char driftText[16] = "-", worstText[16] = "-";

		if (HDModelBindDrift(pslot, &mean, &worst))
		{
			sprintf(driftText, "%.0f", mean);
			sprintf(worstText, "%.0f", worst);
		}

		std::string cover;

		for (int node = 0; node < 32; node++)
			if (HDLaraPartCoverage(part) & (1u << node))
			{
				char text[8];

				sprintf(text, "%s%d", cover.empty() ? "" : ",", node);
				cover += text;
			}

		printf("%-26s %-28s %7d %7s %7s %s\n", HDLaraPartLabel(part),
			HDModelName(pslot), HDModelVertices(pslot), driftText, worstText,
			cover.c_str());
	}

	if (!ok)
	{
		printf("\nHDLaraLevelBuild says classic - see the notes above.\n");
		HDModelLevelEnd();
		return 6;
	}

	/* --- the frame ------------------------------------------------------ */
	const int slotLara = SlotByName(level, "LARA");
	const int slotHolstersEmpty = SlotNumberByName("LARA_HOLSTERS");
	const int slotHolstersFull = SlotNumberByName("LARA_HOLSTERS_PISTOLS");
	const int slotFlare = SlotByName(level, "FLARE_ANIM");
	const int slotTorch = SlotByName(level, "TORCH_ANIM");

	static const struct { const char* word; const char* slot; } kGun[] =
	{
		{ "pistols",        "PISTOLS_ANIM" },
		{ "uzi",            "UZI_ANIM" },
		{ "shotgun",        "SHOTGUN_ANIM" },
		{ "hk",             "HK_ANIM" },
		{ "revolver",       "REVOLVER_ANIM" },
		{ "revolver_laser", "LARA_REVOLVER_LASER" },
		{ "grapple",        "CROSSBOW_ANIM" },
		{ "grapple_laser",  "LARA_CROSSBOW_LASER" },
		{ "crowbar",        "CROWBAR_ANIM" }
	};

	HDLaraFrame now;

	now.rightHandSlot = slotLara;
	now.leftHandSlot = slotLara;
	now.leftHolsterSlot = o.holstersFull ? slotHolstersFull : slotHolstersEmpty;

	/* -holsters names one of the engine's four holster slots by hand, so that
	 * a frame the engine can produce but the tool would never build on its own
	 * can be photographed - see the note further down. */
	if (!o.holsters.empty() && !_stricmp(o.holsters.c_str(), "uzi"))
		now.leftHolsterSlot = SlotNumberByName("LARA_HOLSTERS_UZIS");
	else if (!o.holsters.empty() && !_stricmp(o.holsters.c_str(), "revolver"))
		now.leftHolsterSlot = SlotNumberByName("LARA_HOLSTERS_REVOLVER");

	now.rightHolsterSlot = now.leftHolsterSlot;
	now.backGunSlot = -1;

	if (!o.guns.empty() && _stricmp(o.guns.c_str(), "none"))
	{
		int found = -1;

		for (int i = 0; i < (int)(sizeof(kGun) / sizeof(kGun[0])); i++)
			if (!_stricmp(o.guns.c_str(), kGun[i].word))
				found = SlotByName(level, kGun[i].slot);

		if (found < 0)
			printf("ModelProbe: -guns %s is not a weapon this level has\n",
				o.guns.c_str());
		else
		{
			now.rightHandSlot = found;
			now.leftHandSlot = found;

			/* The gun is in her hands, so the holsters are empty - which is
			 * what the engine does too (draw_pistol_meshes: LHolster and
			 * lara.holster both go back to LARA_HOLSTERS).
			 *
			 * Unless the caller said otherwise. A -holsters spelled out ON
			 * THE COMMAND LINE beats this, because the engine's bookkeeping
			 * really can end up in the contradictory state and the seam's
			 * rule against it needs a way to be photographed: -guns revolver
			 * -holsters revolver is the frame of user finding C1, the gun in
			 * her hand and in the holster at once. */
			if (o.holsters.empty())
			{
				now.leftHolsterSlot = slotHolstersEmpty;
				now.rightHolsterSlot = slotHolstersEmpty;
			}
		}
	}

	if (o.flare && slotFlare >= 0)
		now.leftHandSlot = slotFlare;

	if (o.torch && slotTorch >= 0)
		now.leftHandSlot = slotTorch;

	if (!o.back.empty())
	{
		static const struct { const char* word; const char* slot; } kBack[] =
		{
			{ "shotgun", "SHOTGUN_ANIM" },
			{ "hk",      "HK_ANIM" },
			{ "grapple", "CROSSBOW_ANIM" }
		};

		for (int i = 0; i < (int)(sizeof(kBack) / sizeof(kBack[0])); i++)
			if (!_stricmp(o.back.c_str(), kBack[i].word))
				now.backGunSlot = SlotNumberByName(kBack[i].slot);
	}

	HDLaraDraw list[8];
	const int pieces = HDLaraFrameParts(&now, list, 8);

	printf("\n-- this frame ------------------------------------------------\n");
	printf("hands   : right from %s, left from %s\n",
		HDSlotName(now.rightHandSlot) ? HDSlotName(now.rightHandSlot) : "-",
		HDSlotName(now.leftHandSlot) ? HDSlotName(now.leftHandSlot) : "-");
	printf("holsters: left %s, right %s\n",
		HDSlotName(now.leftHolsterSlot) ? HDSlotName(now.leftHolsterSlot) : "-",
		HDSlotName(now.rightHolsterSlot) ? HDSlotName(now.rightHolsterSlot) : "-");
	printf("back    : %s\n",
		HDSlotName(now.backGunSlot) ? HDSlotName(now.backGunSlot) : "nothing");
	printf("drawn   : %s%s%s", HDLaraOutfitModel(outfit),
		HDLaraHasPart(HD_LARA_PART_FACE) ? " + LARA_HEAD" : "",
		HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG) ? " + holster rig" : "");

	for (int i = 0; i < pieces; i++)
		printf(" + %s(%08x)", HDModelName(HDLaraPartSlot(list[i].part)),
			list[i].nodeMask);

	if (HDLaraHasPart(HD_LARA_PART_BRAID))
		printf(" + HAIR_OG");

	printf("\n");

	/* --- the node table -------------------------------------------------- */
	if (o.nodes)
	{
		static const char* kNodeName[HD_LARA_NODES] =
		{
			"hips", "l thigh", "l shin", "l foot", "r thigh", "r shin",
			"r foot", "torso", "r inarm", "r outarm", "r hand", "l inarm",
			"l outarm", "l hand", "head"
		};

		/* The columns are the SOURCES a node's mesh can come from in the
		 * classic path: the skin, the weapon slot the mesh swap put at the
		 * hands, the holster slot at the thighs, the flare in the left hand,
		 * and the weapon on the back. The cell is the .TRM that covers the
		 * node when the source is that one. */
		struct Column { const char* head; int part; };

		Column column[6];
		int columns = 0;

		column[columns].head = "skin";
		column[columns++].part = HD_LARA_PART_BODY;
		column[columns].head = "face";
		column[columns++].part = HD_LARA_PART_FACE;

		/* The weapon column is the RIGHT hand's part - the one the game
		 * decides through WeaponObjectMesh - and the flare column the left
		 * hand's own model, which is the only one that can differ. */
		int weapon = HDLaraPartForSlot(now.rightHandSlot, HD_LARA_ROLE_HAND);

		if (weapon < 0)
			weapon = HD_LARA_PART_HAND_REST;

		int back = -1;

		for (int part = HD_LARA_PART_BACK_SHOTGUN;
			part <= HD_LARA_PART_BACK_GRAPPLE && back < 0; part++)
			if (HDLaraHasPart(part))
				back = part;

		column[columns].head = "weapon";
		column[columns++].part = weapon;
		column[columns].head = "holster";
		column[columns++].part = HD_LARA_PART_HOLSTER_PISTOL;
		column[columns].head = "flare";
		column[columns++].part = HD_LARA_PART_HAND_FLARE;
		column[columns].head = "back";
		column[columns++].part = back;

		printf("\n-- every Lara node against every source -----------------------\n");
		printf("%-5s %-9s %-24s %-4s %-6s", "node", "name",
			"classic bind (x y z)", "flag", "parent");

		for (int c = 0; c < columns; c++)
			printf(" %-26s", column[c].head);

		printf("\n");

		for (int node = 0; node < HD_LARA_NODES; node++)
		{
			printf("%-5d %-9s %7.0f %7.0f %7.0f %-4ld %-6d", node,
				kNodeName[node], body.bind[node][0], body.bind[node][1],
				body.bind[node][2], body.flags[node], body.parent[node]);

			for (int c = 0; c < columns; c++)
			{
				const int part = column[c].part;
				const bool has = HDLaraHasPart(part) &&
					(HDLaraPartCoverage(part) & (1u << node)) != 0;

				printf(" %-26s", has
					? HDModelName(HDLaraPartSlot(part))
					: (HDLaraHasPart(part) ? "-" : "(not loaded)"));
			}

			printf("\n");
		}

		printf("\nthe braid is its own six node chain and covers no body node.\n");
	}

	if (o.neck)
		PrintNeckTable(o.root, body);

	if (o.braid)
		PrintBraidTable(o.root, body);

	if (o.braidSim > 0)
		PrintBraidSim(level, bodySlot, body, hair, haveHair, o.braidSim);

	if (!o.braidAnims.empty())
		PrintBraidAnim(level, bodySlot, body, hair, haveHair,
			o.braidSim > 0 ? o.braidSim : 120, o.braidAnims.data(),
			(int)o.braidAnims.size(), o.braidFloor);

	if (o.noDraw)
	{
		HDModelLevelEnd();
		return 0;
	}

	/* --- the camera ------------------------------------------------------ */
	/* THE HEAD JOINT of this level's own Lara skeleton, which is where the
	 * first person camera hangs (stage S8). Node 14 of the bind pose. */
	const float* const classic14 = body.bind[HD_LARA_HEAD_NODE];

	/* The composite's box is the union of every part's, because the outfit
	 * alone has an empty skull and a naked wrist. */
	float mins[3] = { 0, 0, 0 }, maxs[3] = { 0, 0, 0 };
	bool first = true;

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		if (!HDLaraHasPart(part) || part == HD_LARA_PART_BRAID)
			continue;

		float lo[3], hi[3];

		if (!HDModelBounds(HDLaraPartSlot(part), lo, hi))
			continue;

		for (int i = 0; i < 3; i++)
		{
			if (first || lo[i] < mins[i]) mins[i] = lo[i];
			if (first || hi[i] > maxs[i]) maxs[i] = hi[i];
		}

		first = false;
	}

	printf("\nbounds  : x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
		mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

	const double persp = EnginePersp(o.height, o.fov);

	/* -look head aims the camera at the HEAD JOINT instead of the middle of
	 * the whole figure, which is the only way to get a picture of the one
	 * junction user finding C4 is about - the braid against the back of the
	 * skull. Without it the camera frames hips to boots and the head sits off
	 * the top edge. */
	const bool lookHead = o.look == "head";
	const double cx = lookHead ? classic14[0] : 0.5 * (mins[0] + maxs[0]);
	const double cy = lookHead ? classic14[1] : 0.5 * (mins[1] + maxs[1]);
	const double cz = lookHead ? classic14[2] : 0.5 * (mins[2] + maxs[2]);
	const double radius = 0.5 * sqrt(
		(maxs[0] - mins[0]) * (maxs[0] - mins[0]) +
		(maxs[1] - mins[1]) * (maxs[1] - mins[1]) +
		(maxs[2] - mins[2]) * (maxs[2] - mins[2]));

	double dist = o.dist;

	if (dist <= 0.0)
		dist = persp * radius / (0.45 * (double)o.height);

	const double d2r = 3.14159265358979323846 / 180.0;
	const double f[3] =
	{
		sin(o.yaw * d2r) * cos(o.pitch * d2r),
		sin(o.pitch * d2r),
		cos(o.yaw * d2r) * cos(o.pitch * d2r)
	};

	float view[16];

	/* THE EYE OF THE FIRST PERSON CAMERA (#123, stage S8), where
	 * game/camera.cpp puts it and nowhere else:
	 *
	 *     GetLaraJointPos(&head, LMX_HEAD)   the head joint
	 *     eye.y = head.y - 16                sixteen units up
	 *     + FP_EYE_OFFSET (48) along yrot    forward, when mgLOS lets it
	 *
	 * The joint is the bind pose's node 14 here, and that is the same point:
	 * the joint is the ORIGIN of the head bone, and turning the head about it
	 * does not move it. So a bind-pose picture taken from this eye is the
	 * picture the game gives, minus the animation - and the neck stump this
	 * stage is about does not move with the animation either, because it
	 * hangs on the torso.
	 *
	 * Looking FORWARD is yaw 0: a TR mesh faces +z, and the tool's own
	 * default of 180 is the camera standing in front of her. The pitch is the
	 * look-DOWN angle; the game clamps head_x_rot to 75 degrees and
	 * item->pos.x_rot can add to that while swimming, so 60 is a plain look
	 * at one's own feet and 75 the steepest a standing Lara reaches. */
	if (o.firstPerson)
	{
		const double ex = classic14[0];
		const double ey = classic14[1] - 16.0;
		const double ez = classic14[2] + o.fpForward;
		const double fpYaw = o.yawSet ? o.yaw : 0.0;
		const double fpPitch = o.pitchSet ? o.pitch : 60.0;

		LookAt(ex, ey, ez, fpYaw, fpPitch, view);

		printf("eye     : %.0f/%.0f/%.0f - the head joint %.0f/%.0f/%.0f, "
			"16 up and %.0f forward; yaw %.0f, pitch %.0f down\n" ,
			ex, ey, ez, classic14[0], classic14[1], classic14[2],
			o.fpForward, fpYaw, fpPitch);
	}
	else
		LookAt(cx - f[0] * dist, cy - f[1] * dist, cz - f[2] * dist, o.yaw,
			o.pitch, view);

	/* THE MIRROR (#123, stage S4-1 proof). A mirror room turns the world
	 * inside out, and a matrix that does that has a NEGATIVE determinant:
	 * every triangle's winding flips, and a renderer that culls by winding
	 * loses the whole body. This one negates the view space x, which is the
	 * cheapest such matrix there is, and the picture that comes back says
	 * whether the HD path minds. It does not - hd_render's state bracket sets
	 * D3DCULL_NONE, the same thing the classic engine does globally
	 * (specific\function_table.cpp:105), and TR5's own mirror does not even
	 * need it: game\mirror.cpp mirrors Lara by MOVING her (z_pos = plane - z,
	 * the three angles negated), which is a rotation with determinant +1, and
	 * switches the SOFTWARE backface test with SetCullCW/SetCullCCW. */
	if (o.mirror)
	{
		float flip[16], mirrored[16];

		MatIdentity(flip);
		flip[0] = -1.0f;
		MatMul(view, flip, mirrored);
		memcpy(view, mirrored, sizeof(view));

		printf("mirror  : the view is flipped in x - determinant negative\n");
	}

	D3DMATRIX projection;
	double a = 0, b = 0, boo = 0;

	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);
	HDRenderMakeProjection(persp, (double)o.width * 0.5, (double)o.height * 0.5,
		a, b, &projection);

	printf("camera  : %.0f units away, yaw %.0f, pitch %.0f, persp %.1f at %ldx%ld\n",
		dist, o.yaw, o.pitch, persp, o.width, o.height);

	/* --- the joint matrices ----------------------------------------------- */
	/* The body: the very walk DrawAnimatingItem does, with the animation left
	 * out - which is the bind pose, and which is what lara_matrices holds
	 * when the game calls phd_PutPolygons for each of the fifteen. */
	float matrices[HD_LARA_NODES * 16];

	BuildNodeMatrices(body, view, o.turn, matrices);

	/* THE BRAID. In the game the six links come from hairs[0][n], which the
	 * simulation writes as ABSOLUTE world positions and angles - there is no
	 * mesh tree walk for the braid at all, which is why the classic HAIR
	 * object's own tree is a straight line along +z (measured: 0, 56, 112,
	 * 168, 224, 280 in every adult level, half that for young Lara's
	 * pigtails) and the .TRM is drawn in exactly that pose.
	 *
	 * A studio picture has no simulation, so the tool builds the chain the
	 * way HairControl would have it hang at rest: the whole braid turned out
	 * of its +z bind direction to point downwards and a little backwards, and
	 * each further link bent a few degrees more, so that the picture proves
	 * what it is meant to prove - the six bones move independently and the
	 * chain hangs together. The angles are the TOOL'S, and only the tool's;
	 * stage S4-2 takes the real ones out of hairs[0][n].
	 *
	 * The chain is built the way a chain is: link n's frame is a bend, then a
	 * step of one link's length along the previous link's own z, then the
	 * previous frame. */
	const double kBraidRest = -100.0;	// +z -> down and slightly behind her
	const double kBraidBend = -5.0;		// per link, so the six are visible

	float hairMatrices[HD_LARA_HAIR_NODES * 16];
	float chain[16];

	{
		float rest[16], anchor[16], base[16];

		/* THE ROOT IS NOT THE HEAD JOINT (#123, fix round C, user finding C4).
		 * game\hair.cpp:397 walks to the head bone and then translates by
		 * (-4, -4, -48) in the head's OWN frame before it writes hairs[0][0];
		 * that offset - up a little and 48 units behind the joint, which is
		 * the back of the skull - is where the braid hangs from. Only the two
		 * ANGLES below are the tool's; the root is the game's own formula, so
		 * that a studio picture shows the same junction the game shows. */
		/* Round D moved it 15 units nearer the skull for an HD Lara, fix round
		 * F withdrew that and fix round I (I2) put it back once the pack was a
		 * box - see HD_LARA_BRAID_ROOT_Z_HD for all three. This tool draws an
		 * HD Lara and nothing else, so the HD root is the default and
		 * -classicroot is the BEFORE picture: the gap of user finding I2. */
		MatTranslate((float)HD_LARA_BRAID_ROOT_X, (float)HD_LARA_BRAID_ROOT_Y,
			(float)(o.classicBraidRoot ? HD_LARA_BRAID_ROOT_Z
				: HD_LARA_BRAID_ROOT_Z_HD), anchor);
		MatMul(anchor, &matrices[HD_LARA_HEAD_NODE * 16], base);

		MatRotX(kBraidRest, rest);
		MatMul(rest, base, chain);
	}

	memcpy(&hairMatrices[0], chain, sizeof(chain));

	for (int n = 1; n < HD_LARA_HAIR_NODES; n++)
	{
		const float length = haveHair
			? hair.bind[n][2] - hair.bind[n - 1][2]
			: 56.0f;

		float bend[16], step[16], tmp[16];

		MatRotX(kBraidBend, bend);
		MatTranslate(0.0f, 0.0f, length, step);
		MatMul(bend, step, tmp);
		MatMul(tmp, chain, &hairMatrices[n * 16]);
		memcpy(chain, &hairMatrices[n * 16], sizeof(chain));
	}

	/* THE SETTLED CHAIN INSTEAD, when -braidrest asked for one (#123, fix
	 * round I, I1). The six matrices are built the way specific\hd\
	 * hd_laradraw.cpp's braid_matrices builds them and not the way the studio
	 * chain above is: a translation to the link's own position, then the two
	 * angles HairControl derives from the chain, then the roll of fix round G.
	 *
	 * The order is the engine's read through the transpose this file works in.
	 * phd_TranslateAbs, phd_RotY, phd_RotX, phd_RotZ builds M = W . Ry . Rx .
	 * Rz on a column vector; the same thing on a ROW vector is Rz then Rx then
	 * Ry then the view, because (A.B.C) transposed is C'.B'.A' - and each of
	 * MatRotY/X/Z here is already the transpose of the engine's own. */
	if (g_braidRestHave)
	{
		double yaw[6], pitch[6], eff[6];

		BraidAngles(g_braidRest, yaw, pitch);
		g_braidLockSeen = false;
		BraidRollFix(g_braidRest, yaw, eff, BRAID_ROLL_LOCK);

		printf("braid   : the SETTLED chain of regime %d, not a studio pose\n",
			g_braidRestWant);

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			float ry[16], rx[16], rz[16], rot[16], move[16], placed[16];
			float* out = &hairMatrices[n * 16];

			MatRotZ(eff[n] - yaw[n], rz);
			MatRotX(pitch[n], rx);
			MatRotY(yaw[n], ry);
			MatMul(rz, rx, rot);
			MatMul(rot, ry, rot);
			MatMul(rot, view, out);

			/* phd_TranslateAbs replaces the translation column and leaves the
			 * rotation where it is. */
			MatTranslate((float)g_braidRest[n][0], (float)g_braidRest[n][1],
				(float)g_braidRest[n][2], move);
			MatMul(move, view, placed);

			out[12] = placed[12];
			out[13] = placed[13];
			out[14] = placed[14];
		}
	}

	/* --- the two proofs of the seam ------------------------------------- */
	/* Offline twins of what specific\hd\hd_laradraw.cpp logs as HDLOG
	 * laracheck and HDLOG braidcheck on the first HD Lara frame of a level.
	 * The engine's run is the one that counts - it measures its own matrix
	 * stack and its own braid simulation; this one runs the same two
	 * questions with no game around it, which is what makes a wrong answer
	 * findable before the game is ever started. */
	if (o.check)
	{
		const float* pm = (const float*)&projection;
		double worstX = 0, worstY = 0;
		int used = 0, behind = 0;

		printf("\n-- laracheck: the joint origins, both roads --------------------\n");
		printf("%5s %-9s %12s %12s %12s %10s %10s %8s %8s\n", "node", "name",
			"view x", "view y", "view z", "classic x", "device x", "dx", "dy");

		static const char* kName[HD_LARA_NODES] =
		{
			"hips", "l thigh", "l shin", "l foot", "r thigh", "r shin",
			"r foot", "torso", "r inarm", "r outarm", "r hand", "l inarm",
			"l outarm", "l hand", "head"
		};

		for (int node = 0; node < HD_LARA_NODES; node++)
		{
			const float* m = &matrices[node * 16];
			const double vx = m[12], vy = m[13], vz = m[14];

			if (vz < 20.0)
			{
				behind++;
				continue;
			}

			const double zv = persp / vz;
			const double classicX = vx * zv + (double)o.width * 0.5;
			const double classicY = vy * zv + (double)o.height * 0.5;

			const double cxx = vx * pm[0] + vy * pm[4] + vz * pm[8] + pm[12];
			const double cyy = vx * pm[1] + vy * pm[5] + vz * pm[9] + pm[13];
			const double cww = vx * pm[3] + vy * pm[7] + vz * pm[11] + pm[15];

			if (cww <= 0.0)
			{
				behind++;
				continue;
			}

			const double deviceX = (cxx / cww + 1.0) * 0.5 * (double)o.width;
			const double deviceY = (1.0 - cyy / cww) * 0.5 * (double)o.height;
			const double dx = fabs(deviceX - classicX);
			const double dy = fabs(deviceY - classicY);

			if (dx > worstX) worstX = dx;
			if (dy > worstY) worstY = dy;

			used++;

			printf("%5d %-9s %12.1f %12.1f %12.1f %10.3f %10.3f %8.5f %8.5f\n",
				node, kName[node], vx, vy, vz, classicX, deviceX, dx, dy);
		}

		printf("\n%d joints compared, %d behind the near plane; "
			"worst dx %.5f px, worst dy %.5f px\n", used, behind, worstX, worstY);
		printf("%s\n", worstX < 1.0 && worstY < 1.0
			? "PASS - both roads put every joint origin in the same pixel"
			: "FAIL - the projection and the classic transform disagree");

		/* THE BRAID'S CHAIN RULE, which is the one thing about this seam that
		 * a still picture cannot show. TR5 builds a matrix for links 1, 3 and
		 * 5 only (game\hair.cpp, DrawHair) and draws 0, 2 and 4 as skinned
		 * connectors out of stashed vertices; HAIR_OG.TRM has six bones, so
		 * the seam has to build all six - and nothing in the game has ever
		 * used the angles of the three it adds.
		 *
		 * They are usable, and this is the proof. HairControl derives every
		 * link's POSITION by putting the link before it at its own position
		 * and angles - phd_SetTrans, then phd_RotYXZ(y_rot, x_rot, 0), which
		 * is phd_RotY followed by phd_RotX (specific\3dmath.cpp:435) - and
		 * stepping one bone offset along that frame. The seam builds the same
		 * frame with phd_TranslateAbs, phd_RotY, phd_RotX, in that order. So
		 * frame(n) stepped by the offset must land on position(n + 1).
		 *
		 * The chain below is built by the FIRST of those two rules and the
		 * frames by the SECOND, written apart, so that a swapped rotation
		 * order or a translation that accumulates instead of being replaced
		 * comes out as a gap and not as agreement. */
		printf("\n-- braidcheck: the six links' chain rule -----------------------\n");

		double yawChain[HD_LARA_HAIR_NODES];
		double pitchChain[HD_LARA_HAIR_NODES];
		float chainPos[HD_LARA_HAIR_NODES][3];
		float step[HD_LARA_HAIR_NODES][3];

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			// Nothing round or symmetric, so that no sign can cancel itself.
			yawChain[n] = 25.0 - 11.0 * n;
			pitchChain[n] = -97.0 - 6.0 * n;

			step[n][0] = 0.0f;
			step[n][1] = 0.0f;
			step[n][2] = haveHair && n + 1 < hair.nodes
				? hair.bind[n + 1][2] - hair.bind[n][2]
				: 56.0f;
		}

		chainPos[0][0] = 0.0f;
		chainPos[0][1] = 0.0f;
		chainPos[0][2] = 0.0f;

		for (int n = 0; n + 1 < HD_LARA_HAIR_NODES; n++)
		{
			float ry[16], rx[16], rot[16];

			MatRotY(yawChain[n], ry);
			MatRotX(pitchChain[n], rx);
			MatMul(ry, rx, rot);		// RotY then RotX - phd_RotYXZ(y, x, 0)

			for (int a = 0; a < 3; a++)
				chainPos[n + 1][a] = chainPos[n][a] +
					step[n][0] * rot[0 * 4 + a] +
					step[n][1] * rot[1 * 4 + a] +
					step[n][2] * rot[2 * 4 + a];
		}

		float frame[HD_LARA_HAIR_NODES][16];

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			float ry[16], rx[16], rot[16], move[16], placed[16];

			MatRotY(yawChain[n], ry);
			MatRotX(pitchChain[n], rx);
			MatMul(ry, rx, rot);
			MatMul(rot, view, frame[n]);

			/* phd_TranslateAbs REPLACES the translation column and leaves the
			 * rotation alone, so the frame's origin is the link's own view
			 * position and not the rotated camera's. */
			MatTranslate(chainPos[n][0], chainPos[n][1], chainPos[n][2], move);
			MatMul(move, view, placed);

			frame[n][12] = placed[12];
			frame[n][13] = placed[13];
			frame[n][14] = placed[14];
		}

		printf("%5s %8s %12s %12s %12s %10s\n", "link", "step",
			"stepped x", "stepped y", "stepped z", "gap");

		double worstGap = 0;

		for (int n = 0; n + 1 < HD_LARA_HAIR_NODES; n++)
		{
			const float* m = frame[n];
			const double px = step[n][0] * m[0] + step[n][1] * m[4] +
				step[n][2] * m[8] + m[12];
			const double py = step[n][0] * m[1] + step[n][1] * m[5] +
				step[n][2] * m[9] + m[13];
			const double pz = step[n][0] * m[2] + step[n][1] * m[6] +
				step[n][2] * m[10] + m[14];

			const double dx = px - frame[n + 1][12];
			const double dy = py - frame[n + 1][13];
			const double dz = pz - frame[n + 1][14];
			const double gap = sqrt(dx * dx + dy * dy + dz * dz);

			if (gap > worstGap)
				worstGap = gap;

			printf("%5d %8.0f %12.2f %12.2f %12.2f %10.5f\n", n,
				step[n][2], px, py, pz, gap);
		}

		printf("\nworst gap %.5f units over %d joints\n", worstGap,
			HD_LARA_HAIR_NODES - 1);
		printf("%s\n", worstGap < 0.01
			? "PASS - the six rebuilt frames are the chain HairControl walked"
			: "FAIL - a link's frame does not reach the next link");
	}

	float light[HD_LARA_NODES * 3];

	for (int i = 0; i < HD_LARA_NODES * 3; i++)
		light[i] = (float)o.shade;

	HDModelLighting lighting;

	lighting.lights = o.lightCount ? o.lights : 0;
	lighting.lightCount = o.lightCount;
	lighting.sunScale = 0.75f;
	lighting.specStrength = 0.0f;
	lighting.specPower = 1.0f;

	/* THE SHEEN, part by part (#123, fix round D, D2), out of the same table
	 * the engine reads - so a picture from here is the picture the game
	 * draws. -nosheen sets every one of them to zero, which is the BEFORE
	 * picture of that fix. */
	printf("sheen   : ");

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		float st = 0.0f, pw = 1.0f, held = 0.0f, hp = 1.0f;

		HDLaraPartSheen(part, &st, &pw);
		HDLaraHeldSheen(part, &held, &hp);

		if (!HDLaraHasPart(part) || st <= 0.0f)
			continue;

		printf("%s %.2f/%.0f", HDLaraPartLabel(part), o.noSheen ? 0.0f : st, pw);

		/* And what is IN the hand, on its own pages (#123, fix round I, I4). */
		if (held > 0.0f && HDModelPageSheenCount(HDLaraPartSlot(part)))
			printf(" [held %.2f/%.0f on %d page(s)]", o.noSheen ? 0.0f : held,
				hp, HDModelPageSheenCount(HDLaraPartSlot(part)));

		printf("  ");
	}

	printf("%s\n", o.noSheen ? "  (-nosheen: all off)" : "");

	/* #123 fix round I (I4): -onlypart draws one part and leaves the rest
	 * out, which is how a question about ONE part is answered by a picture
	 * of that part. A lambda rather than six copies of the same test. */
	const auto draws = [&o](int part) { return o.onlyPart < 0 || o.onlyPart == part; };

	/* --- draw -------------------------------------------------------------- */
	printf("\n-- the picture ------------------------------------------------\n");

	Shot shot;

	if (!ShotBegin(shot, o.width, o.height))
	{
		ShotEnd(shot);
		HDModelLevelEnd();
		return 7;
	}

	HDFrameLighting frame;
	float wibble[32];

	memset(&frame, 0, sizeof(frame));

	for (int i = 0; i < 32; i++)
		wibble[i] = 0.0f;

	frame.centerX = (float)o.width * 0.5f;
	frame.centerY = (float)o.height * 0.5f;
	frame.distanceFogStart = 1.0e9f;
	frame.wibbleTable = wibble;

	g_device->BeginScene();
	HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
	HDModelFrameBegin(&projection, 0);

	int drawn = 0;

	/* FIRST PERSON (#126), the same three omissions the seam makes: the head
	 * node off the body, no separate face, no braid. All three sit exactly
	 * where the camera does. */
	unsigned int bodyMask = ~0u;

	if (o.firstPerson)
	{
		bodyMask &= ~(1u << HD_LARA_HEAD_NODE);
		printf("firstperson: head node %d masked, no LARA_HEAD, no braid\n",
			HD_LARA_HEAD_NODE);
	}

	/* THE NEAR CUT (stage S8). The BODY carries it and nothing else, exactly
	 * as specific/hd/hd_laradraw.cpp pushes it: the hands and the holsters are
	 * their own models and must never be cut away. */
	printf("nearcut : %.0f units of radius round the eye, body alone%s\n", o.nearCut,
		o.nearCut > 0.0 ? "" : " - off, this is the BEFORE picture");
	printf("cull    : %s\n", o.cull == HD_MODEL_CULL_CW ? "clockwise"
		: o.cull == HD_MODEL_CULL_CCW ? "counter-clockwise"
		: "none - both sides drawn, as every other HD model is");

	/* THE SHEEN, part by part (#123, fix round D, D2): the same table the
	 * engine reads, so this picture is the game's picture. */
	const bool sheenOn = !o.noSheen;

	/* -nosheen has to take the PER PAGE materials off as well (#123, fix round
	 * I, I4), or the BEFORE picture would still carry the gun's highlight. */
	if (!sheenOn)
		for (int part = 0; part < HD_LARA_PART_COUNT; part++)
		{
			const int pslot = HDLaraPartSlot(part);

			if (!HDLaraHasPart(part))
				continue;

			for (int i = 0; i < HDModelPageCount(pslot); i++)
				HDModelSetPageSheen(pslot, HDModelPage(pslot, i), 0.0f, 1.0f);
		}

	SetSheen(lighting, HD_LARA_PART_BODY, sheenOn);

	if (draws(HD_LARA_PART_BODY) && HDModelDraw(HDLaraPartSlot(HD_LARA_PART_BODY), matrices, HD_LARA_NODES,
		light, bodyMask, &lighting, (float)o.nearCut, o.cull))
		drawn++;

	SetSheen(lighting, HD_LARA_PART_FACE, sheenOn);

	if (!o.firstPerson && draws(HD_LARA_PART_FACE) &&
		HDModelDraw(HDLaraPartSlot(HD_LARA_PART_FACE), matrices, HD_LARA_NODES,
		light, bodyMask, &lighting))
		drawn++;

	/* THE NECK CAP (#123, fix round I, I3), in the same place and with the
	 * same three rules the seam has: first person only, no near cut of its
	 * own, and drawn on both sides. */
	SetSheen(lighting, HD_LARA_PART_NECK_CAP, sheenOn);

	if (o.firstPerson && !o.noCap && draws(HD_LARA_PART_NECK_CAP) &&
		HDLaraHasPart(HD_LARA_PART_NECK_CAP) &&
		HDModelDraw(HDLaraPartSlot(HD_LARA_PART_NECK_CAP), matrices,
			HD_LARA_NODES, light,
			HDLaraPartCoverage(HD_LARA_PART_NECK_CAP) & bodyMask, &lighting))
		drawn++;

	/* THE HOLSTER RIG (#123, fix round D, D4), in the same place and with the
	 * same guard specific\hd\hd_laradraw.cpp puts it: before the pieces,
	 * because it belongs to the outfit and not to the weapon. */
	SetSheen(lighting, HD_LARA_PART_HOLSTER_RIG, sheenOn);

	if (draws(HD_LARA_PART_HOLSTER_RIG) && HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG) &&
		HDModelDraw(HDLaraPartSlot(HD_LARA_PART_HOLSTER_RIG), matrices,
			HD_LARA_NODES, light,
			HDLaraPartCoverage(HD_LARA_PART_HOLSTER_RIG) & bodyMask, &lighting))
		drawn++;

	for (int i = 0; i < pieces; i++)
	{
		SetSheen(lighting, list[i].part, sheenOn);

		if (draws(list[i].part) &&
			HDModelDraw(HDLaraPartSlot(list[i].part), matrices, HD_LARA_NODES,
			light, list[i].nodeMask & bodyMask, &lighting))
			drawn++;
	}

	if (!o.firstPerson && draws(HD_LARA_PART_BRAID) &&
		HDLaraHasPart(HD_LARA_PART_BRAID))
	{
		float hairLight[HD_LARA_HAIR_NODES * 3];

		for (int i = 0; i < HD_LARA_HAIR_NODES * 3; i++)
			hairLight[i] = (float)o.shade;

		SetSheen(lighting, HD_LARA_PART_BRAID, sheenOn);

		if (HDModelDraw(HDLaraPartSlot(HD_LARA_PART_BRAID), hairMatrices,
			HD_LARA_HAIR_NODES, hairLight, ~0u, &lighting))
			drawn++;
	}

	const int calls = HDModelDrawCalls();

	HDRenderFrameEnd();
	g_device->EndScene();

	printf("%d models drawn in %d draw calls\n", drawn, calls);

	char path[512];

	char cut[32] = "";

	if (o.firstPerson)
		sprintf(cut, "_p%d_f%d_cut%d_%s", (int)(o.pitchSet ? o.pitch : 60.0),
			(int)o.fpForward, (int)(o.nearCut + 0.5),
			o.cull == HD_MODEL_CULL_CW ? "cw"
			: o.cull == HD_MODEL_CULL_CCW ? "ccw" : "nocull");

	sprintf(path, "%s_lara_%s%s%s%s.png", o.out.c_str(),
		HDLaraOutfitModel(outfit), o.firstPerson ? "_firstperson" : "", cut,
		o.mirror ? "_mirror" : "");
	ShotWrite(shot, path);
	ShotEnd(shot);

	HDModelLevelEnd();
	return 0;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: ModelProbe <5-folder> [-name <NAME> | -slot <n> | -all]"
			" [-level <LEVEL>] [options]\n"
			"       see the head of ModelProbe.cpp for the whole list\n");
		return 2;
	}

	std::string root = argv[1];
	std::string name, levelStem, classicPath, out = ".\\modelprobe";
	/* Stage S6: load that level's ROOM pages first, so the model page count
	 * that comes out is the one the engine really pays - most of a level's
	 * model pages are pages its rooms already wear, and appending them to a
	 * cache that already holds the rooms is exactly what HDLevelBuild does. */
	std::string withRooms;
	int slot = -1;
	bool all = false, nodes = false, noDraw = false, managed = false, head = false;
	bool itemcheck = false;

	/* Lara's composite - stage S4-1. */
	int onlyPage = -1;		// #123 fix round D (D4): -name with one page
	bool lara = false, laraNodes = false, mirror = false;
	bool laraFirstPerson = false, laraCheck = false, laraNeck = false;
	bool classicBraidRoot = false;	// #123 fix round I (I2): the BEFORE picture
	bool noCap = false;		// #123 fix round I (I3): the BEFORE picture
	int onlyPart = -1;		// #123 fix round I (I4): one part alone
	bool noSheen = false;		// #123 fix round D (D2): the BEFORE picture
	bool laraBraid = false;
	int laraBraidSim = 0;		// #123 fix round F (F2): the braid's physics
	/* #123 fix round G (G1): the same chain under a real animation. */
	std::vector<int> laraBraidAnims;
	bool laraBraidFloor = true;
	/* Stage S8. -1 means "the engine's own constant when -firstperson is on",
	 * which is what makes the tool's default picture the game's picture; a
	 * number given on the line overrides it, and 0 is the BEFORE picture. */
	double nearCut = -1.0;
	double fpForward = 48.0;
	int cullMode = -1;		// -1 = "what -firstperson would use"
	bool yawSet = false, pitchSet = false;
	std::string outfitName, gunsName, backName, holstersName, lookName;
	bool holstersFull = true, flare = false, torch = false;
	int threads = 8;
	long width = 1024, height = 1024;
	double fov = 80.0, yaw = 180.0, pitch = 10.0, dist = 0.0;
	double shade = 128.0;

	double turn[32][2];
	memset(turn, 0, sizeof(turn));

	HDObjLight lights[HD_MAX_OBJ_LIGHTS];
	int lightCount = 0;
	unsigned int nodeMask = ~0u;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-name") && i + 1 < argc)
			name = argv[++i];
		else if (!_stricmp(argv[i], "-slot") && i + 1 < argc)
			slot = atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-level") && i + 1 < argc)
			levelStem = argv[++i];
		else if (!_stricmp(argv[i], "-classic") && i + 1 < argc)
			classicPath = argv[++i];
		else if (!_stricmp(argv[i], "-withrooms") && i + 1 < argc)
			withRooms = argv[++i];
		else if (!_stricmp(argv[i], "-all"))
			all = true;
		else if (!_stricmp(argv[i], "-lara") && i + 1 < argc)
		{
			lara = true;
			levelStem = argv[++i];
		}
		else if (!_stricmp(argv[i], "-outfit") && i + 1 < argc)
		{
			lara = true;
			outfitName = argv[++i];
		}
		else if (!_stricmp(argv[i], "-guns") && i + 1 < argc)
			gunsName = argv[++i];
		else if (!_stricmp(argv[i], "-back") && i + 1 < argc)
			backName = argv[++i];
		else if (!_stricmp(argv[i], "-holsters") && i + 1 < argc)
		{
			holstersName = argv[++i];
			holstersFull = !_stricmp(holstersName.c_str(), "full");
		}
		else if (!_stricmp(argv[i], "-look") && i + 1 < argc)
			lookName = argv[++i];
		else if (!_stricmp(argv[i], "-flare"))
			flare = true;
		else if (!_stricmp(argv[i], "-torch"))
			torch = true;
		else if (!_stricmp(argv[i], "-lara-nodes"))
		{
			lara = true;
			laraNodes = true;
		}
		else if (!_stricmp(argv[i], "-mirror"))
			mirror = true;
		else if (!_stricmp(argv[i], "-firstperson"))
		{
			lara = true;
			laraFirstPerson = true;
		}
		else if (!_stricmp(argv[i], "-nearcut") && i + 1 < argc)
			nearCut = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-fpforward") && i + 1 < argc)
			fpForward = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-cull") && i + 1 < argc)
		{
			const char* what = argv[++i];

			cullMode = !_stricmp(what, "cw") ? HD_MODEL_CULL_CW
				: !_stricmp(what, "ccw") ? HD_MODEL_CULL_CCW
				: !_stricmp(what, "back") ? HD_MODEL_CULL_BACK
				: HD_MODEL_CULL_NONE;
		}
		else if (!_stricmp(argv[i], "-neck"))
		{
			lara = true;
			laraNeck = true;
		}
		else if (!_stricmp(argv[i], "-classicroot"))
			classicBraidRoot = true;
		/* #123 fix round I (I3): the first person picture WITHOUT the neck
		 * cap, which is what the near cut of stage S8 leaves on its own. */
		else if (!_stricmp(argv[i], "-nocap"))
			noCap = true;
		else if (!_stricmp(argv[i], "-onlypart") && i + 1 < argc)
		{
			lara = true;
			onlyPart = atoi(argv[++i]);
		}
		/* #123 fix round I (I1): draw the braid where the SIMULATION leaves it
		 * rather than in a studio pose. The number is a -braidsim regime; with
		 * none it is 7, which is what the engine ships (the box, root -33).
		 * Regime 4 is the picture before this round - root -48 and the two
		 * pack spheres of fix rounds D to G. */
		else if (!_stricmp(argv[i], "-braidrest"))
		{
			lara = true;
			g_braidRestWant = 7;

			if (i + 1 < argc && argv[i + 1][0] != '-')
				g_braidRestWant = atoi(argv[++i]);

			if (g_braidRestWant < 0 || g_braidRestWant > 7)
				g_braidRestWant = 7;

			if (laraBraidSim <= 0)
				laraBraidSim = 60;
		}
		else if (!_stricmp(argv[i], "-nosheen"))
			noSheen = true;
		else if (!_stricmp(argv[i], "-braid"))
		{
			lara = true;
			laraBraid = true;
		}
		/* #123 fix round F (F2): the braid's own physics, run offline. The
		 * step count is optional - 60 is a second of game time. */
		else if (!_stricmp(argv[i], "-braidsim"))
		{
			lara = true;
			laraBraidSim = 60;

			if (i + 1 < argc && argv[i + 1][0] != '-')
				laraBraidSim = atoi(argv[++i]);

			if (laraBraidSim < 1)
				laraBraidSim = 60;
		}
		/* #123 fix round G (G1): the same chain driven by a real animation.
		 * Repeat the switch for more of them; with no number it runs the
		 * three the play test spends its time in - breathing on the spot,
		 * the run loop and the walk loop. */
		else if (!_stricmp(argv[i], "-braidanim"))
		{
			lara = true;

			if (i + 1 < argc && argv[i + 1][0] != '-')
				laraBraidAnims.push_back(atoi(argv[++i]));
			else
			{
				laraBraidAnims.push_back(ANIM_BREATH_NUMBER);
				laraBraidAnims.push_back(0);	// the run loop
				laraBraidAnims.push_back(1);	// the walk loop
			}
		}
		else if (!_stricmp(argv[i], "-nobraidfloor"))
			laraBraidFloor = false;
		else if (!_stricmp(argv[i], "-laracheck"))
		{
			lara = true;
			laraCheck = true;
		}
		else if (!_stricmp(argv[i], "-nodes"))
			nodes = true;
		else if (!_stricmp(argv[i], "-nodraw"))
			noDraw = true;
		else if (!_stricmp(argv[i], "-managed"))
			managed = true;
		else if (!_stricmp(argv[i], "-onlypage") && i + 1 < argc)
			onlyPage = (int)strtol(argv[++i], 0, 0);
		else if (!_stricmp(argv[i], "-head"))
			head = true;
		else if (!_stricmp(argv[i], "-itemcheck"))
			itemcheck = true;
		else if (!_stricmp(argv[i], "-threads") && i + 1 < argc)
			threads = atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-out") && i + 1 < argc)
			out = argv[++i];
		else if (!_stricmp(argv[i], "-size") && i + 2 < argc)
		{
			width = atol(argv[++i]);
			height = atol(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-fov") && i + 1 < argc)
			fov = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-yaw") && i + 1 < argc)
		{
			yaw = atof(argv[++i]);
			yawSet = true;
		}
		else if (!_stricmp(argv[i], "-pitch") && i + 1 < argc)
		{
			pitch = atof(argv[++i]);
			pitchSet = true;
		}
		else if (!_stricmp(argv[i], "-dist") && i + 1 < argc)
			dist = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-shade") && i + 1 < argc)
			shade = atof(argv[++i]);
		else if ((!_stricmp(argv[i], "-pose") || !_stricmp(argv[i], "-lara-pose"))
			&& i + 3 < argc)
		{
			const int node = atoi(argv[++i]);
			const double y = atof(argv[++i]);
			const double p = atof(argv[++i]);

			if (node >= 0 && node < 32)
			{
				turn[node][0] = y;
				turn[node][1] = p;
			}
		}
		else if (!_stricmp(argv[i], "-meshbits") && i + 1 < argc)
			nodeMask = (unsigned int)strtoul(argv[++i], 0, 0);
		else if (!_stricmp(argv[i], "-light") && i + 7 < argc && lightCount < HD_MAX_OBJ_LIGHTS)
		{
			/* x y z r g b range - a POINT light in view space, which is what
			 * the object shader wants since stage S3-2. The studio camera
			 * looks down -z at the model, so "0 0 -2000" is in front of it. */
			HDObjLight& light = lights[lightCount++];

			light.x = (float)atof(argv[++i]);
			light.y = (float)atof(argv[++i]);
			light.z = (float)atof(argv[++i]);
			light.kind = (float)HD_OBJLIGHT_POINT;
			light.r = (float)atof(argv[++i]);
			light.g = (float)atof(argv[++i]);
			light.b = (float)atof(argv[++i]);

			const float range = (float)atof(argv[++i]);

			light.invRange = range > 0.0f ? 1.0f / range : 0.0f;
		}
		else
			printf("ModelProbe: ignoring %s\n", argv[i]);
	}

	if (name.empty() && slot < 0 && !all && !lara)
	{
		printf("ModelProbe: nothing to look at - give -name, -slot, -all or -lara\n");
		return 2;
	}

	printf("install : %s\n", root.c_str());

	/* --- the classic level, when there is one -------------------------- */
	Tr5Level level;
	bool haveLevel = false;

	if (!levelStem.empty() || !classicPath.empty())
	{
		const std::string path = classicPath.empty()
			? root + "\\DATA\\" + levelStem + ".TRC"
			: classicPath;
		std::string error;

		if (!level.load(ToW(path), error))
			printf("ModelProbe: %s is not loadable: %s\n", path.c_str(), error.c_str());
		else
		{
			haveLevel = true;
			printf("classic : %s\n", path.c_str());
		}

		if (!levelStem.empty())
		{
			const std::string map = root + "\\DATA\\" + levelStem + ".MAP";

			if (HDMapLoad(map.c_str()))
				printf("map     : %s, %d lines\n", map.c_str(), HDMapCount());
			else
				printf("map     : %s is not there - every slot binds by name\n",
					map.c_str());
		}
	}

	if ((all || slot >= 0 || lara) && !haveLevel)
	{
		printf("ModelProbe: -slot, -all and -lara need a level "
			"(-lara <LEVEL>, -level or -classic)\n");
		return 2;
	}

	/* --- the device and the two HD modules ----------------------------- */
	if (!MakeDevice())
		return 3;

	const char* why = "";

	if (!HDRenderInit(g_device, &why))
	{
		printf("ModelProbe: HDRenderInit failed - %s\n", why);
		DropDevice();
		return 4;
	}

	if (!HDModelInit(g_device, &why))
	{
		printf("ModelProbe: HDModelInit failed - %s\n", why);
		HDRenderShutdown();
		DropDevice();
		return 4;
	}

	/* --- Lara's composite (#123, stage S4-1) ---------------------------- */
	if (lara)
	{
		LaraOptions o;

		o.root = root;
		o.stem = levelStem;
		o.outfit = outfitName;
		o.guns = gunsName;
		o.back = backName;
		o.out = out;
		o.holstersFull = holstersFull;
		o.holsters = holstersName;
		o.look = lookName;
		o.flare = flare;
		o.torch = torch;
		o.nodes = laraNodes;
		o.mirror = mirror;
		o.firstPerson = laraFirstPerson;
		o.classicBraidRoot = classicBraidRoot;
		o.noCap = noCap;
		o.onlyPart = onlyPart;
		o.noSheen = noSheen;
		o.neck = laraNeck;
		o.braid = laraBraid;
		o.braidSim = laraBraidSim;
		o.braidAnims = laraBraidAnims;
		o.braidFloor = laraBraidFloor;
		o.check = laraCheck;
		o.noDraw = noDraw;
		o.managed = managed;
		o.threads = threads;
		o.width = width;
		o.height = height;
		o.fov = fov;
		o.yaw = yaw;
		o.pitch = pitch;
		o.dist = dist;
		o.shade = shade;
		o.nearCut = nearCut < 0.0
			? (laraFirstPerson ? (double)HD_LARA_FIRST_PERSON_NEAR_CUT : 0.0)
			: nearCut;
		o.fpForward = fpForward;
		o.cull = cullMode < 0
			? (laraFirstPerson ? HD_MODEL_CULL_BACK : HD_MODEL_CULL_NONE)
			: cullMode;
		o.yawSet = yawSet;
		o.pitchSet = pitchSet;
		o.lights = lights;
		o.lightCount = lightCount;
		o.turn = turn;

		const int result = RunLara(o, level);

		HDLaraLevelEnd();
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());
		return result;
	}

	/* --- which slots ---------------------------------------------------- */
	std::vector<HDModelSlot> wanted;
	std::vector<ClassicSlot> classic;
	std::vector<int> order;

	if (!name.empty())
	{
		/* A bare model, no level: 32 nodes, no bind pose, and the name
		 * handed straight to hd_model. The picture is the bind pose. */
		ClassicSlot empty;

		empty.nodes = 32;
		classic.push_back(empty);

		HDModelSlot one;

		one.slot = 0;
		one.nodes = 32;
		one.bind = 0;
		one.name = name.c_str();
		one.onlyPage = onlyPage;	// #123 fix round D (D4)
		wanted.push_back(one);
		order.push_back(0);
		slot = 0;
	}
	else
	{
		const std::vector<int> slots = level.moveableSlots();

		classic.resize(slots.size());

		for (size_t i = 0; i < slots.size(); i++)
		{
			if (!all && slots[i] != slot)
				continue;

			ReadClassicSlot(level, slots[i], classic[i]);

			HDModelSlot one;

			one.slot = slots[i];
			one.nodes = classic[i].nodes;
			one.bind = &classic[i].bind[0][0];
			/* The mesh TREE as well since #123 fix round B (B4): a separate
			 * HD face's neck bone follows the head node's PARENT, and this
			 * tool has walked the tree for its own node table anyway. */
			one.parent = classic[i].parent;
			one.name = 0;
			wanted.push_back(one);
			order.push_back((int)i);
		}

		if (wanted.empty())
		{
			printf("ModelProbe: slot %d is not in this level\n", slot);
			HDModelShutdown();
			HDRenderShutdown();
			DropDevice();
			return 5;
		}
	}

	/* --- the rooms first, if asked (#123, stage S6) --------------------- */
	double roomsBytes = 0;

	if (!withRooms.empty())
	{
		HDRenderStats roomStats;
		const char* roomWhy = "";

		if (HDRenderLevelBegin(root.c_str(), withRooms.c_str(), 0, managed,
			threads, &roomStats, &roomWhy))
		{
			roomsBytes = roomStats.pages.bytes;
			printf("rooms first        %s: %d page(s), %.1f MiB - the models "
				"are appended to that cache\n", withRooms.c_str(),
				roomStats.pages.slots, roomsBytes / 1048576.0);
		}
		else
			printf("rooms first        %s FAILED - %s\n", withRooms.c_str(), roomWhy);
	}

	/* --- load ----------------------------------------------------------- */
	HDModelStats stats;

	if (!HDModelLevelBegin(g_device, root.c_str(), &wanted[0], (int)wanted.size(),
		managed, threads, &stats, &why))
	{
		printf("ModelProbe: HDModelLevelBegin failed - %s\n", why);
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 5;
	}

	printf("\n-- what was loaded -------------------------------------------\n");
	printf("slots asked for   %d\n", stats.slots);
	printf("models on the card %d   (5: %d, 4: %d)   heads %d\n",
		stats.loaded, stats.fromGame5, stats.fromGame4, stats.heads);
	printf("not bound          lara %d, dummy %d, no name %d, missing %d, refused %d\n",
		stats.lara, stats.dummy, stats.noName, stats.missing, stats.refused);
	printf("nodes              %d over %d slots, %d covered, %d fall back\n",
		stats.nodesTotal, stats.fullSlots + stats.partialSlots, stats.nodesCovered,
		stats.nodesFallback);
	printf("slots              %d complete, %d partial\n",
		stats.fullSlots, stats.partialSlots);
	printf("geometry           %d vertices, %d indices, %d draw calls, "
		"%d mixed-page triangles\n",
		stats.vertices, stats.indices, stats.ranges, stats.mixedPageTriangles);
	printf("buffers            %.1f KiB vertices + %.1f KiB indices = %.1f KiB\n",
		stats.vertexBytes / 1024.0, stats.indexBytes / 1024.0,
		(stats.vertexBytes + stats.indexBytes) / 1024.0);
	printf("pages              %d added, %.1f MiB of texture, %.0f ms decode\n",
		stats.pages, stats.pageStats.bytes / 1048576.0, stats.pageStats.decodeMs);
	/* #123, fix round B (B2): how many of the models' pages are CUT-OUT
	 * sheets - a hole share over HD_CUTOUT_PERMILLE of the top mip. The
	 * opaque pass has to switch the alpha test on for exactly these, which is
	 * what hd_render has done for the rooms since fix round A (A2). */
	printf("cut-out pages      %d of %d in the cache carry holes\n",
		HDPageCacheCutoutPages(), HDPageCacheSlots());

	for (int s = 0; s < HDPageCacheSlots(); s++)
		printf("  page %5d  holes %4.1f %%%s\n", HDPageCachePageOfSlot(s),
			HDPageCacheSlotCutoutPermille(s) / 10.0,
			HDPageCacheSlotCutout(s) ? "  <- counted as a cut-out sheet" : "");
	printf("time               %.0f ms reading the .TRM files, %.0f ms building\n",
		stats.readMs, stats.buildMs);

	if (!withRooms.empty())
		printf("level total        %.1f MiB of pages on the card "
			"(%.1f rooms + %.1f models and wardrobe)\n",
			HDPageCacheBytes() / 1048576.0, roomsBytes / 1048576.0,
			(HDPageCacheBytes() - roomsBytes) / 1048576.0);

	for (int i = 0; i < HDModelNoteCount(); i++)
		printf("  note: %s\n", HDModelNote(i));

	/* --- the table ------------------------------------------------------ */
	if (all)
	{
		/* THE SKELETON COLUMNS (#123, stage S3-2). `maxb` is the highest node
		 * a weighted vertex of the .TRM names and `drift` is how far the
		 * centre of a node's own vertices sits from that node in the SLOT's
		 * bind pose, mean over the covered nodes. The engine draws HD only
		 * when maxb is inside the slot's node count and the drift is under
		 * its limit - which is why the two numbers stand here beside the slot
		 * that is really loaded, randomised or not. */
		printf("\n-- every slot ------------------------------------------------\n");
		printf("%-5s %-24s %-24s %5s %5s %5s %5s %6s %5s %s\n", "slot",
			"slot name", "model", "nodes", "maxb", "cover", "drift", "worst",
			"pages", "notes");
		printf("----- ------------------------ ------------------------ "
			"----- ----- ----- ----- ------ ----- ------------------\n");

		int complete = 0, partial = 0, classicOnly = 0;
		const float kProbeDriftLimit = HD_SKELETON_DRIFT_LIMIT;
		int mismatchNodes = 0, mismatchDrift = 0;
		float worstGood = 0, bestBad = 1e9f;

		for (size_t i = 0; i < wanted.size(); i++)
		{
			const int here = wanted[i].slot;
			const char* slotName = HDSlotName(here);

			if (!HDModelHas(here))
			{
				HDBindKind kind = HD_BIND_NONE;
				bool fromMap = false;
				const char* asked = HDBindSlot(here, &kind, &fromMap);

				classicOnly++;
				printf("%-5d %-24s %-24s %5d %5s %5s %5s %6s %5s %s\n", here,
					slotName ? slotName : "?",
					kind == HD_BIND_LARA ? "(Lara - stage S4)"
						: (kind == HD_BIND_DUMMY ? "(DUMMY on purpose)"
							: (asked ? asked : "?")),
					wanted[i].nodes, "-", "-", "-", "-", "-",
					kind == HD_BIND_MODEL ? "no .TRM - classic" : "");
				continue;
			}

			int covered = 0;
			std::string fallback;

			for (int node = 0; node < HDModelNodes(here); node++)
			{
				if (HDModelNodeCovered(here, node))
					covered++;
				else
				{
					char text[16];

					sprintf(text, "%s%d", fallback.empty() ? "" : ",", node);
					fallback += text;
				}
			}

			int missingPages = 0;

			for (int p = 0; p < HDModelPageCount(here); p++)
				if (!PageThere(root, HDModelPage(here, p)))
					missingPages++;

			if (covered >= HDModelNodes(here))
				complete++;
			else
				partial++;

			char note[128] = "";

			if (!fallback.empty())
				sprintf(note, "classic nodes %.60s", fallback.c_str());

			if (missingPages)
				sprintf(note + strlen(note), "%s%d page(s) WHITE",
					*note ? "; " : "", missingPages);

			if (HDModelHasHead(here))
				sprintf(note + strlen(note), "%shead on node %d",
					*note ? "; " : "", HDModelHeadNode(here));

			const int maxBone = HDModelMaxBone(here);
			float drift = -1, worst = -1;

			HDModelBindDrift(here, &drift, &worst);

			/* The engine's own verdict, spelled out here so that the table and
			 * the game cannot drift apart. */
			const bool badNodes = maxBone >= HDModelNodes(here);
			const bool badDrift = !badNodes && drift > kProbeDriftLimit;

			if (badNodes)
			{
				mismatchNodes++;
				sprintf(note + strlen(note), "%sSKELETON: names node %d of %d",
					*note ? "; " : "", maxBone, HDModelNodes(here));
			}
			else if (badDrift)
			{
				mismatchDrift++;
				sprintf(note + strlen(note), "%sSKELETON: drift",
					*note ? "; " : "");
			}

			if (drift >= 0)
			{
				if (badNodes || badDrift)
				{
					if (drift < bestBad)
						bestBad = drift;
				}
				else if (drift > worstGood)
					worstGood = drift;
			}

			char maxText[16], driftText[16], worstText[16];

			sprintf(maxText, "%d", maxBone);
			sprintf(driftText, drift >= 0 ? "%.0f" : "-", drift);
			sprintf(worstText, worst >= 0 ? "%.0f" : "-", worst);

			printf("%-5d %-24s %-24s %5d %5s %5d %5s %6s %5d %s\n", here,
				slotName ? slotName : "?", HDModelName(here),
				HDModelNodes(here), maxText, covered, driftText, worstText,
				HDModelPageCount(here), note);
		}

		printf("\n%d slots complete, %d partial (an uncovered node draws "
			"nothing), %d classic only\n", complete, partial, classicOnly);
		printf("skeleton: %d refused on the node count, %d on the drift; "
			"worst drift among the models that fit %.0f (limit %.0f)\n",
			mismatchNodes, mismatchDrift, worstGood, kProbeDriftLimit);

		if (bestBad < 1e8f)
			printf("          the smallest drift among the refused is %.0f\n",
				bestBad);

		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());
		return 0;
	}

	/* --- one model ------------------------------------------------------ */
	if (!HDModelHas(slot))
	{
		printf("\nModelProbe: slot %d has no HD model - nothing to draw\n", slot);
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 6;
	}

	const ClassicSlot* here = 0;

	for (size_t i = 0; i < order.size(); i++)
		if (wanted[i].slot == slot)
			here = &classic[(size_t)order[i]];

	printf("\nmodel   : %s.TRM, %d vertices, %d indices, %d draw calls, %d page(s)\n",
		HDModelName(slot), HDModelVertices(slot), HDModelIndices(slot),
		HDModelRanges(slot), HDModelPageCount(slot));

	for (int p = 0; p < HDModelPageCount(slot); p++)
		printf("          page %d%s\n", HDModelPage(slot, p),
			PageThere(root, HDModelPage(slot, p)) ? "" : "  - no .DDS, WHITE instead");

	if (nodes || !all)
		PrintNodeTable(slot, here, HDModelName(slot));

	float mins[3], maxs[3];

	HDModelBounds(slot, mins, maxs);

	/* A separate HD face reaches past the body's box - the body has an empty
	 * collar and the face IS the head - so the camera has to frame both or it
	 * cuts the head off. */
	if (head && HDModelHasHead(slot))
	{
		float headMins[3], headMaxs[3];

		if (HDModelHeadBounds(slot, headMins, headMaxs))
			for (int i = 0; i < 3; i++)
			{
				if (headMins[i] < mins[i]) mins[i] = headMins[i];
				if (headMaxs[i] > maxs[i]) maxs[i] = headMaxs[i];
			}
	}

	printf("\nbounds  : x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
		mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

	if (noDraw)
	{
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* --- the camera ----------------------------------------------------- */
	const double persp = EnginePersp(height, fov);
	const double cx = 0.5 * (mins[0] + maxs[0]);
	const double cy = 0.5 * (mins[1] + maxs[1]);
	const double cz = 0.5 * (mins[2] + maxs[2]);
	const double radius = 0.5 * sqrt(
		(maxs[0] - mins[0]) * (maxs[0] - mins[0]) +
		(maxs[1] - mins[1]) * (maxs[1] - mins[1]) +
		(maxs[2] - mins[2]) * (maxs[2] - mins[2]));

	/* The classic projection puts a point at persp * x / z pixels off centre,
	 * so a model of the given radius fills 0.45 of the picture's height at
	 * this distance - close enough to look at, far enough that a turned limb
	 * still has room. */
	if (dist <= 0.0)
		dist = persp * radius / (0.45 * (double)height);

	const double d2r = 3.14159265358979323846 / 180.0;
	const double f[3] =
	{
		sin(yaw * d2r) * cos(pitch * d2r),
		sin(pitch * d2r),
		cos(yaw * d2r) * cos(pitch * d2r)
	};

	float view[16];

	LookAt(cx - f[0] * dist, cy - f[1] * dist, cz - f[2] * dist, yaw, pitch, view);

	D3DMATRIX projection;
	double a = 0, b = 0, boo = 0;

	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);
	HDRenderMakeProjection(persp, (double)width * 0.5, (double)height * 0.5,
		a, b, &projection);

	printf("camera  : %.0f units away, yaw %.0f, pitch %.0f, persp %.1f at %ldx%ld\n",
		dist, yaw, pitch, persp, width, height);

	/* --- the node matrices ---------------------------------------------- */
	ClassicSlot flat;

	if (!here)
	{
		flat.nodes = 32;
		flat.ok = true;
		here = &flat;
	}

	std::vector<float> matrices((size_t)here->nodes * 16);

	BuildNodeMatrices(*here, view, turn, &matrices[0]);

	/* THE MATRIX PROOF OF THE OBJECT PATH (#123, stage S3-2), without the
	 * game. A node matrix carries the node's ORIGIN into view space all by
	 * itself - its translation IS that origin's view position - so the two
	 * roads to a pixel can be compared on it directly:
	 *
	 *   classic   sx = vx * f_persp / vz + centre, as aTransformLightClipMesh
	 *             computes it for every vertex of every mesh
	 *   device    the row vector through the projection matrix, the w
	 *             division, and the viewport this engine never sets - the
	 *             whole back buffer, MinZ 0, MaxZ 1
	 *
	 * The engine runs the identical comparison on the FIRST HD item of every
	 * level, on the matrices its own stack produced, and writes HDLOG
	 * itemcheck. This is the same arithmetic on the same kind of matrices
	 * with no game around it. */
	if (itemcheck)
	{
		const float* pm = (const float*)&projection;
		double worstX = 0, worstY = 0;
		int used = 0, behind = 0;

		printf("\n-- itemcheck: the node origins, both roads ---------------------\n");
		printf("%5s %12s %12s %12s %10s %10s %8s %8s\n", "node",
			"view x", "view y", "view z", "classic x", "device x", "dx", "dy");

		for (int node = 0; node < here->nodes; node++)
		{
			const float* m = &matrices[(size_t)node * 16];
			const double vx = m[12], vy = m[13], vz = m[14];

			if (vz < 20.0)
			{
				behind++;
				continue;
			}

			const double zv = persp / vz;
			const double classicX = vx * zv + (double)width * 0.5;
			const double classicY = vy * zv + (double)height * 0.5;

			const double cxx = vx * pm[0] + vy * pm[4] + vz * pm[8] + pm[12];
			const double cyy = vx * pm[1] + vy * pm[5] + vz * pm[9] + pm[13];
			const double cww = vx * pm[3] + vy * pm[7] + vz * pm[11] + pm[15];

			if (cww <= 0.0)
			{
				behind++;
				continue;
			}

			const double deviceX = (cxx / cww + 1.0) * 0.5 * (double)width;
			const double deviceY = (1.0 - cyy / cww) * 0.5 * (double)height;
			const double dx = fabs(deviceX - classicX);
			const double dy = fabs(deviceY - classicY);

			if (dx > worstX) worstX = dx;
			if (dy > worstY) worstY = dy;

			used++;

			printf("%5d %12.1f %12.1f %12.1f %10.3f %10.3f %8.5f %8.5f\n",
				node, vx, vy, vz, classicX, deviceX, dx, dy);
		}

		printf("\n%d nodes compared, %d behind the near plane; "
			"worst dx %.5f px, worst dy %.5f px\n", used, behind, worstX, worstY);
		printf("%s\n", worstX < 1.0 && worstY < 1.0
			? "PASS - both roads put every node origin in the same pixel"
			: "FAIL - the projection and the classic transform disagree");
	}

	std::vector<float> light((size_t)here->nodes * 3, (float)shade);

	HDModelLighting lighting;

	lighting.lights = lightCount ? lights : 0;
	lighting.lightCount = lightCount;
	lighting.sunScale = 0.75f;
	// #123 fix round D (D2): the sheen is Lara's alone; an object gets none.
	lighting.specStrength = 0.0f;
	lighting.specPower = 1.0f;

	/* --- draw ------------------------------------------------------------ */
	printf("\n-- the picture ------------------------------------------------\n");

	Shot shot;

	if (!ShotBegin(shot, width, height))
	{
		ShotEnd(shot);
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 7;
	}

	HDFrameLighting frame;
	float wibble[32];

	memset(&frame, 0, sizeof(frame));

	for (int i = 0; i < 32; i++)
		wibble[i] = 0.0f;

	frame.centerX = (float)width * 0.5f;
	frame.centerY = (float)height * 0.5f;
	frame.distanceFogStart = 1.0e9f;	// no fog in a studio picture
	frame.wibbleTable = wibble;

	g_device->BeginScene();
	HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
	HDModelFrameBegin(&projection, 0);

	const bool drew = HDModelDraw(slot, &matrices[0], here->nodes,
		&light[0], nodeMask, &lighting);

	bool drewHead = false;

	if (head)
		drewHead = HDModelDrawHead(slot, &matrices[0], here->nodes,
			&light[0], nodeMask, &lighting);

	const int calls = HDModelDrawCalls();

	HDRenderFrameEnd();
	g_device->EndScene();

	printf("HDModelDraw returned %s after %d draw calls%s\n",
		drew ? "true" : "FALSE", calls,
		drewHead ? " (the separate face came along)" : "");

	char path[512];

	sprintf(path, "%s_%s.png", out.c_str(), HDModelName(slot));
	ShotWrite(shot, path);
	ShotEnd(shot);

	printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());

	HDModelShutdown();
	HDRenderShutdown();
	DropDevice();
	return 0;
}

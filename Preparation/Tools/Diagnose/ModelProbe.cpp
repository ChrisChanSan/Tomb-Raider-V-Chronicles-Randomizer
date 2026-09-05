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
 *                         column. 4\ITEM and 6\ITEM are searched too.
 *     -slot <n>           the model bound to slot n of the level, which needs
 *                         -level
 *     -all                every slot of the level, as a table, then stop
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
 *     -pose <node> <yaw> <pitch>
 *                         turn that node of the mesh tree and everything
 *                         below it. Repeat the switch for more nodes.
 *     -head               draw the separate HD face as well, if there is one
 *     -light <x y z r g b falloff>
 *                         one dynamic light in VIEW space (the camera is at
 *                         the origin looking down +z). Up to 8.
 *     -shade <0..255>     the flat node colour, default 128
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
	int slot = -1;
	bool all = false, nodes = false, noDraw = false, managed = false, head = false;
	int threads = 8;
	long width = 1024, height = 1024;
	double fov = 80.0, yaw = 180.0, pitch = 10.0, dist = 0.0;
	double shade = 128.0;

	double turn[32][2];
	memset(turn, 0, sizeof(turn));

	HDLightSource lights[HD_MAX_LIGHTS];
	int lightCount = 0;

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
		else if (!_stricmp(argv[i], "-all"))
			all = true;
		else if (!_stricmp(argv[i], "-nodes"))
			nodes = true;
		else if (!_stricmp(argv[i], "-nodraw"))
			noDraw = true;
		else if (!_stricmp(argv[i], "-managed"))
			managed = true;
		else if (!_stricmp(argv[i], "-head"))
			head = true;
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
			yaw = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-pitch") && i + 1 < argc)
			pitch = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-dist") && i + 1 < argc)
			dist = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-shade") && i + 1 < argc)
			shade = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-pose") && i + 3 < argc)
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
		else if (!_stricmp(argv[i], "-light") && i + 7 < argc && lightCount < HD_MAX_LIGHTS)
		{
			HDLightSource& light = lights[lightCount++];

			light.x = (float)atof(argv[++i]);
			light.y = (float)atof(argv[++i]);
			light.z = (float)atof(argv[++i]);
			light.r = (float)atof(argv[++i]);
			light.g = (float)atof(argv[++i]);
			light.b = (float)atof(argv[++i]);
			light.falloff = (float)atof(argv[++i]);
		}
		else
			printf("ModelProbe: ignoring %s\n", argv[i]);
	}

	if (name.empty() && slot < 0 && !all)
	{
		printf("ModelProbe: nothing to look at - give -name, -slot or -all\n");
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

	if ((all || slot >= 0) && !haveLevel)
	{
		printf("ModelProbe: -slot and -all need a level (-level or -classic)\n");
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
	printf("models on the card %d   (5: %d, 4: %d, 6: %d)   heads %d\n",
		stats.loaded, stats.fromGame5, stats.fromGame4, stats.fromGame6, stats.heads);
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
	printf("time               %.0f ms reading the .TRM files, %.0f ms building\n",
		stats.readMs, stats.buildMs);

	for (int i = 0; i < HDModelNoteCount(); i++)
		printf("  note: %s\n", HDModelNote(i));

	/* --- the table ------------------------------------------------------ */
	if (all)
	{
		printf("\n-- every slot ------------------------------------------------\n");
		printf("%-5s %-26s %-28s %5s %5s %5s %6s %s\n", "slot", "slot name",
			"model", "nodes", "cover", "fall", "pages", "notes");
		printf("----- -------------------------- ---------------------------- "
			"----- ----- ----- ------ ------------------\n");

		int complete = 0, partial = 0, classicOnly = 0;

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
				printf("%-5d %-26s %-28s %5d %5s %5s %6s %s\n", here,
					slotName ? slotName : "?",
					kind == HD_BIND_LARA ? "(Lara - stage S4)"
						: (kind == HD_BIND_DUMMY ? "(DUMMY on purpose)"
							: (asked ? asked : "?")),
					wanted[i].nodes, "-", "-", "-",
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

			printf("%-5d %-26s %-28s %5d %5d %5d %6d %s\n", here,
				slotName ? slotName : "?", HDModelName(here),
				HDModelNodes(here), covered, HDModelNodes(here) - covered,
				HDModelPageCount(here), note);
		}

		printf("\n%d slots complete, %d partial (per-mesh fallback), "
			"%d classic only\n", complete, partial, classicOnly);

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

	std::vector<float> light((size_t)here->nodes * 3, (float)shade);

	HDModelLighting lighting;

	lighting.lights = lightCount ? lights : 0;
	lighting.lightCount = lightCount;

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
		&light[0], &lighting);

	bool drewHead = false;

	if (head)
		drewHead = HDModelDrawHead(slot, &matrices[0], here->nodes,
			&light[0], &lighting);

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

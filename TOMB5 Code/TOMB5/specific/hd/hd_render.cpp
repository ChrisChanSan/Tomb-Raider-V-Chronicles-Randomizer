/*
 * Modified 2026 by ChrisChanSan.
 *
 * The structure of the level build (read the .TRG, one buffer for the whole
 * level, a range table per group, three passes per room) and the draw rule
 * are carried over from TROne (Copyright 2026 Todesengel, GPLv3,
 * host\render\game_renderer.cpp, hd_load 1919-2256 and hd_draw_room
 * 2259-2312). The renderer under it is new: TROne is OpenGL 3.3 with vertex
 * arrays, texture arrays and runtime GLSL, this is Direct3D 9 with a vertex
 * declaration, one texture per page and compiled bytecode, beside a
 * fixed-function neighbour that must find its state exactly as it left it.
 * See THIRD-PARTY.md.
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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "hd_render.h"
#include "hd_pagecache.h"

#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include "hd_shader/hd_room_vs.inc"
#include "hd_shader/hd_room_ps.inc"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	/* THE VERTEX ON THE CARD IS THE VERTEX ON DISK - 20 bytes, same fields,
	 * same meaning. Written out field by field rather than memcpy'd from
	 * TrgVertex, because that struct belongs to the vendored reader and its
	 * layout is its business, not ours.
	 *
	 * The ONE thing that is not a straight copy is the colour byte order.
	 * D3DDECLTYPE_D3DCOLOR reads the four bytes as a little-endian
	 * 0xAARRGGBB DWORD, so the byte at offset 0 of the pair is BLUE and the
	 * byte at offset 3 is alpha. Writing (b, g, r, coordinate) here is what
	 * lets hd_room.hlsl read the triple as plain .rgb and the coordinate as
	 * .a. Swapping HERE and swapping AGAIN in the shader cancels out and
	 * paints the level with red and blue exchanged - the swap happens once,
	 * and it happens in this struct. */
	struct HDVertex
	{
		short x, y, z, sway;
		unsigned char nx, ny, nz, layer;
		unsigned char c1b, c1g, c1r, u;		// D3DCOLOR order: B, G, R, A
		unsigned char c2b, c2g, c2r, v;
		/* Stage S2b's four flag bytes - see HDWaterMark in the header. They
		 * are 0 or 1, not 0 or 255: D3DDECLTYPE_UBYTE4 is UNnormalised, so
		 * whatever is written here is what the shader adds up. The fourth is
		 * the constant 1, which lets a selector address a whole room without
		 * a mark on every vertex - see gSel1 in hd_room.hlsl. */
		unsigned char wChoppy, wShimmer, wRoomShimmer, wSpare;
	};

	// One draw call: a run of indices that all name the same page.
	struct HDRange
	{
		unsigned int start = 0;
		unsigned int count = 0;
		int pageSlot = -1;
	};

	struct HDGroup
	{
		std::vector<HDRange> pass[3];
		std::vector<HDRenderSample> samples;
		unsigned int triangles[3] = { 0, 0, 0 };
		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };
		bool hasBounds = false;
	};

	/* WHAT THE CLASSIC PATH LEFT ON THE DEVICE. Read back, not remembered
	 * from a table of what it "should" be: the fixed-function path changes
	 * its own states per bucket (polyinsert.cpp) and per level (function
	 * table), and a hard-coded restore would put back a state the caller
	 * never had. */
	struct SavedState
	{
		IDirect3DVertexShader9* vs = 0;
		IDirect3DPixelShader9* ps = 0;
		IDirect3DVertexDeclaration9* decl = 0;
		IDirect3DVertexBuffer9* stream = 0;
		IDirect3DIndexBuffer9* indices = 0;
		IDirect3DBaseTexture9* texture = 0;
		UINT streamOffset = 0;
		UINT streamStride = 0;
		DWORD fvf = 0;
		DWORD rs[32] = {};
		DWORD samp[16] = {};
	};

	const D3DRENDERSTATETYPE kSavedRenderStates[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
		D3DRS_ALPHATESTENABLE, D3DRS_ALPHAFUNC, D3DRS_ALPHAREF,
		D3DRS_CULLMODE, D3DRS_CLIPPING, D3DRS_SPECULARENABLE, D3DRS_FOGENABLE,
		D3DRS_LIGHTING, D3DRS_COLORVERTEX, D3DRS_SHADEMODE, D3DRS_FILLMODE,
		D3DRS_DITHERENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_SRGBWRITEENABLE, D3DRS_MULTISAMPLEANTIALIAS
	};

	const D3DSAMPLERSTATETYPE kSavedSamplerStates[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW,
		D3DSAMP_MAXANISOTROPY, D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL,
		D3DSAMP_SRGBTEXTURE
	};

	const int kRenderStateCount = (int)(sizeof(kSavedRenderStates) / sizeof(kSavedRenderStates[0]));
	const int kSamplerStateCount = (int)(sizeof(kSavedSamplerStates) / sizeof(kSavedSamplerStates[0]));

	IDirect3DDevice9* g_device = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	IDirect3DVertexDeclaration9* g_decl = 0;
	IDirect3DVertexBuffer9* g_vb = 0;
	IDirect3DIndexBuffer9* g_ib = 0;

	std::vector<HDGroup> g_groups;
	int g_room_groups = 0;
	int g_vertex_count = 0;
	bool g_index32 = false;
	D3DCAPS9 g_caps;

	SavedState g_saved;
	bool g_in_frame = false;
	int g_draw_calls = 0;

	/* --- stage S2b ----------------------------------------------------- */

	// WATERTAB (global	ypes.h:2485). Two signed bytes and two unsigned ones,
	// and the mixture is not an accident: init_water_table writes -3 into the
	// unsigned `abs` (gamemain.cpp:79), aRoomletTransformLight reads it back
	// as 253, and the shimmer is 253 brighter for that table row. Copied, not
	// corrected.
	struct HDWaterTab
	{
		signed char shimmer;
		signed char choppy;
		unsigned char random;
		unsigned char abs;
	};

	const HDWaterMark* g_marks = 0;
	int g_mark_count = 0;

	const void* g_water_table_up = 0;	// what is on the card right now
	int g_frame_bulbs = 0;
	bool g_frame_has_light = false;

	const float kIdentity[16] =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	// Identity to start with, so that a FrameBegin(0, 0) - which only the
	// tests do - draws in raw clip space instead of through whatever was in
	// this memory.
	float g_view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	float g_proj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	float g_viewproj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

	std::string g_error;

	double now_ms()
	{
		return std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::string join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return b;
		std::string out = a;
		while (!out.empty() && (out.back() == '\\' || out.back() == '/'))
			out.pop_back();
		out += '\\';
		out += b;
		return out;
	}

	// Row vector convention, the same one D3DMATRIX and the engine use.
	void multiply(const float* a, const float* b, float* out)
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

		std::memcpy(out, tmp, sizeof(tmp));
	}

	/* A shader constant register holds a COLUMN of the matrix (fxc packs a
	 * float4x4 column major by default, and the generated code confirms it:
	 * dp4 o.x, position, c0). A D3DMATRIX holds rows. So the transpose goes
	 * to the device - the single most common way to get a black screen out of
	 * an otherwise correct Direct3D 9 shader path. */
	void set_matrix_constant(int reg, const float* rowMajor)
	{
		float columns[16];

		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		g_device->SetVertexShaderConstantF((UINT)reg, columns, 4);
	}

	/* The classic D3DMView for this room, as three columns rather than four.
	 * The shader declares it float4x3 and only wants the xyz of the product;
	 * a fourth register here would land on one of fxc's own def constants and
	 * overwrite a literal - which is silent, and looks like a maths error
	 * three files away. */
	void set_worldview_constant(const float* rowMajor)
	{
		float columns[12];

		for (int col = 0; col < 3; col++)
			for (int row = 0; row < 4; row++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		g_device->SetVertexShaderConstantF(5, columns, 3);
	}

	/* --- the parity constants ------------------------------------------
	 *
	 * The register table lives in hd_room.hlsl and is repeated nowhere:
	 * these two functions are the only writers, and they write every one of
	 * their registers on every call - a constant left over from the room
	 * before is the whole reason this is not done "only when it changes",
	 * with the single exception of the 64 register water table, which is a
	 * kilobyte and is keyed on the pointer the engine hands over. */
	void apply_room_lighting(const HDRoomLighting* r)
	{
		float origin[4] = { 0, 0, 0, -1.0e9f };	// a surface no vertex sits on
		float mode[4] = { 0, 0, 0, 0 };
		float sel[4] = { 0, 0, 0, 0 };
		float counts[4] = { 0, (float)g_frame_bulbs, 0, 0 };
		float lights[HD_MAX_LIGHTS * 2 * 4] = { 0 };
		const void* table = 0;

		if (r)
		{
			origin[0] = r->originX;
			origin[1] = r->originY;
			origin[2] = r->originZ;
			origin[3] = r->surfaceY;

			mode[0] = r->underwater ? 1.0f : 0.0f;
			mode[1] = r->choppy ? 1.0f : 0.0f;
			mode[2] = r->surfaceGate ? 1.0f : 0.0f;
			mode[3] = r->redAlert ? 1.0f : 0.0f;

			sel[0] = r->sel1[0];
			sel[1] = r->sel1[1];
			sel[2] = r->sel1[2];
			sel[3] = r->sel1[3];

			int count = r->lights ? r->lightCount : 0;

			if (count > HD_MAX_LIGHTS)
				count = HD_MAX_LIGHTS;

			for (int i = 0; i < count; i++)
			{
				const HDLightSource& l = r->lights[i];

				lights[i * 8 + 0] = l.x;
				lights[i * 8 + 1] = l.y;
				lights[i * 8 + 2] = l.z;
				lights[i * 8 + 3] = l.falloff;
				lights[i * 8 + 4] = l.r;
				lights[i * 8 + 5] = l.g;
				lights[i * 8 + 6] = l.b;
				lights[i * 8 + 7] = l.falloff != 0 ? 1.0f / l.falloff : 0.0f;
			}

			counts[0] = (float)count;
			table = r->waterTable;
		}

		if (table != g_water_table_up)
		{
			float rows[64 * 4];
			const HDWaterTab* in = (const HDWaterTab*)table;

			for (int i = 0; i < 64; i++)
			{
				rows[i * 4 + 0] = in ? (float)in[i].shimmer : 0.0f;
				rows[i * 4 + 1] = in ? (float)in[i].choppy : 0.0f;
				rows[i * 4 + 2] = in ? (float)in[i].random : 0.0f;
				rows[i * 4 + 3] = in ? (float)in[i].abs : 0.0f;
			}

			g_device->SetVertexShaderConstantF(32, rows, 64);
			g_water_table_up = table;
		}

		g_device->SetVertexShaderConstantF(10, origin, 1);
		g_device->SetVertexShaderConstantF(11, mode, 1);
		g_device->SetVertexShaderConstantF(12, sel, 1);
		g_device->SetPixelShaderConstantF(17, counts, 1);
		g_device->SetPixelShaderConstantF(20, lights, HD_MAX_LIGHTS * 2);
	}

	void apply_frame_lighting(const HDFrameLighting* f)
	{
		float screen[4] = { 1, 1, 0, 0 };
		float fog[4] = { 1.0e30f, 0, 0, 0 };	// nothing is ever behind it
		float wibble[32 * 4] = { 0 };
		float bulbs[HD_MAX_BULBS * 2 * 4] = { 0 };

		g_frame_bulbs = 0;
		g_frame_has_light = f != 0;

		if (f)
		{
			screen[0] = f->centerX != 0 ? f->centerX : 1.0f;
			screen[1] = f->centerY != 0 ? f->centerY : 1.0f;
			screen[2] = f->wibble;
			screen[3] = f->cameraUnderwater ? 1.0f : 0.0f;

			if (f->distanceFogStart > 0)
			{
				fog[0] = f->distanceFogStart;
				fog[1] = 255.0f / f->distanceFogStart;
			}

			if (f->wibbleTable)
				for (int i = 0; i < 32; i++)
					wibble[i * 4] = f->wibbleTable[i];

			int count = f->bulbs ? f->bulbCount : 0;

			if (count > HD_MAX_BULBS)
				count = HD_MAX_BULBS;

			for (int j = 0; j < count; j++)
			{
				const HDFogBulb& b = f->bulbs[j];

				bulbs[j * 8 + 0] = b.x;
				bulbs[j * 8 + 1] = b.y;
				bulbs[j * 8 + 2] = b.z;
				bulbs[j * 8 + 3] = b.rad;
				bulbs[j * 8 + 4] = b.r;
				bulbs[j * 8 + 5] = b.g;
				bulbs[j * 8 + 6] = b.b;
				bulbs[j * 8 + 7] = b.density;
			}

			g_frame_bulbs = count;
		}

		g_device->SetVertexShaderConstantF(9, screen, 1);
		g_device->SetVertexShaderConstantF(96, wibble, 32);
		g_device->SetPixelShaderConstantF(16, fog, 1);
		g_device->SetPixelShaderConstantF(40, bulbs, HD_MAX_BULBS * 2);

		// And a room with no lighting of its own: neutral, never the last
		// room's.
		apply_room_lighting(0);
	}

	void release_buffers()
	{
		if (g_vb) { g_vb->Release(); g_vb = 0; }
		if (g_ib) { g_ib->Release(); g_ib = 0; }

		g_groups.clear();
		g_room_groups = 0;
		g_vertex_count = 0;
		g_index32 = false;
	}
}

/* ---------------------------------------------------------------- set up -- */

bool HDRenderInit(IDirect3DDevice9* device, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	HDRenderShutdown();

	if (!device)
	{
		*why = "no device";
		return false;
	}

	g_device = device;
	std::memset(&g_caps, 0, sizeof(g_caps));

	if (FAILED(device->GetDeviceCaps(&g_caps)))
	{
		*why = "GetDeviceCaps failed";
		g_device = 0;
		return false;
	}

	/* What the HD path needs of the card, all of it 2004 hardware:
	 * vs_3_0 / ps_3_0, and the two packed declaration types the 20 byte
	 * vertex is made of. */
	if (g_caps.VertexShaderVersion < D3DVS_VERSION(3, 0) ||
		g_caps.PixelShaderVersion < D3DPS_VERSION(3, 0))
	{
		*why = "the device has no vertex/pixel shader 3.0";
		g_device = 0;
		return false;
	}

	/* The room shader is 313 ps_3_0 instruction slots. Shader model 3
	 * guarantees 512 and every card that reports ps_3_0 has to, but a device
	 * that answers less would fail CreatePixelShader below with nothing but
	 * an HRESULT, so the number is read here and named in the log instead. */
	if (g_caps.PS20Caps.NumInstructionSlots &&
		g_caps.PS20Caps.NumInstructionSlots < 320)
	{
		*why = "the device allows too few pixel shader instruction slots";
		g_device = 0;
		return false;
	}

	if (!(g_caps.DeclTypes & D3DDTCAPS_UBYTE4))
	{
		*why = "the device has no UBYTE4 vertex element";
		g_device = 0;
		return false;
	}

	static const D3DVERTEXELEMENT9 elements[] =
	{
		{ 0,  0, D3DDECLTYPE_SHORT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0,  8, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },
		{ 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
		{ 0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    1 },
		{ 0, 20, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
		D3DDECL_END()
	};

	if (FAILED(device->CreateVertexDeclaration(elements, &g_decl)))
	{
		*why = "CreateVertexDeclaration failed";
		g_decl = 0;
		g_device = 0;
		return false;
	}

	if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomVS, &g_vs)))
	{
		*why = "CreateVertexShader failed";
		HDRenderShutdown();
		return false;
	}

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDRoomPS, &g_ps)))
	{
		*why = "CreatePixelShader failed";
		HDRenderShutdown();
		return false;
	}

	return true;
}

void HDRenderShutdown()
{
	release_buffers();
	HDPageCacheDestroy();

	if (g_ps) { g_ps->Release(); g_ps = 0; }
	if (g_vs) { g_vs->Release(); g_vs = 0; }
	if (g_decl) { g_decl->Release(); g_decl = 0; }

	g_device = 0;
	g_in_frame = false;
	g_draw_calls = 0;
	g_error.clear();
}

bool HDRenderReady()
{
	return g_device && g_vs && g_ps && g_decl;
}

/* ---------------------------------------------------------------- level -- */

void HDRenderSetWaterMarks(const HDWaterMark* marks, int count)
{
	g_marks = count > 0 ? marks : 0;
	g_mark_count = marks ? count : 0;
}

bool HDRenderLevelBegin(const char* installRoot, const char* stem, int numRooms,
	bool managedPages, int threads, HDRenderStats* stats, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;
	if (!why)
		why = &dummy;
	*why = nothing;

	release_buffers();
	HDPageCacheDestroy();

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!HDRenderReady())
	{
		*why = "HDRenderInit was not called or failed";
		return false;
	}

	if (!installRoot || !*installRoot || !stem || !*stem)
	{
		*why = "no install or no level name";
		return false;
	}

	const std::string trg_path = join(join(installRoot, "DATA"), std::string(stem) + ".TRG");

	const double read_started = now_ms();

	trf::remaster::Trg trg;
	std::string error;

	if (!trf::remaster::read_trg(trg_path, trg, error))
	{
		g_error = error;
		*why = g_error.c_str();
		return false;
	}

	const double read_ms = now_ms() - read_started;
	const double build_started = now_ms();

	g_vertex_count = (int)trg.vertices.size();
	g_room_groups = numRooms > 0 && numRooms < (int)trg.groups.size()
		? numRooms : (int)trg.groups.size();

	if (g_vertex_count <= 0 || trg.indices.empty())
	{
		*why = "the .TRG holds no geometry";
		return false;
	}

	/* 32 bit indices unless the level is small enough not to need them.
	 * ANDREA2 has 5,471,013 indices over 1.1 M vertices; there is no 16 bit
	 * reading of that. Every Direct3D 9 HAL device supports 32 bit indices,
	 * but the cap is asked rather than assumed. */
	g_index32 = g_vertex_count > 65535;

	if (g_index32 && g_caps.MaxVertexIndex <= 0xFFFF)
	{
		*why = "the device cannot address more than 65535 vertices";
		return false;
	}

	/* The HIGHEST index, not the count: MaxVertexIndex is a DWORD and a card
	 * that reports 0xFFFFFFFF makes "count > MaxVertexIndex + 1" wrap to
	 * "count > 0" and refuses every level there is. */
	if ((unsigned)(g_vertex_count - 1) > g_caps.MaxVertexIndex)
	{
		*why = "the level has more vertices than the device can index";
		return false;
	}

	/* --- the pages ---------------------------------------------------- */
	std::vector<int> pages;
	pages.reserve(trg.textures.size());

	for (unsigned short page : trg.textures)
		pages.push_back((int)page);

	HDPageCacheStats page_stats;

	if (!HDPageCacheCreate(g_device, installRoot, pages.empty() ? 0 : &pages[0],
		(int)pages.size(), managedPages, threads, &page_stats, why))
		return false;

	/* --- which group owns which vertex --------------------------------
	 *
	 * Only needed for the water marks, and only built when there are any. A
	 * .TRG names its vertices per group through the index runs and nowhere
	 * else, so the ownership is read out of the runs - the same walk TROne
	 * makes for its own water flag (game_renderer.cpp:2113-2124). A vertex
	 * named by two groups keeps the last one; the marks are per room and a
	 * shared vertex sits on a portal, where both answers agree. */
	std::vector<int> vertex_group;
	std::unordered_map<unsigned long long, unsigned char> mark_map;

	if (g_marks && g_mark_count > 0)
	{
		vertex_group.assign((size_t)g_vertex_count, -1);

		for (size_t g = 0; g < trg.groups.size(); g++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned int at = 0; at < run.count; at++)
					{
						const unsigned int at_index = run.start + at;

						if (at_index >= trg.indices.size())
							break;

						const unsigned int v = trg.indices[at_index];

						if (v < (unsigned int)g_vertex_count)
							vertex_group[v] = (int)g;
					}
				}

		mark_map.reserve((size_t)g_mark_count * 2);

		for (int i = 0; i < g_mark_count; i++)
		{
			const HDWaterMark& m = g_marks[i];
			const unsigned long long key =
				((unsigned long long)(unsigned short)m.room << 48) |
				((unsigned long long)(unsigned short)m.x << 32) |
				((unsigned long long)(unsigned short)m.y << 16) |
				(unsigned long long)(unsigned short)m.z;

			mark_map[key] |= m.bits;
		}
	}

	int marked_vertices = 0;
	int marked_choppy = 0;
	int marked_shimmer = 0;
	int marked_room_shimmer = 0;

	/* --- the vertex buffer -------------------------------------------- */
	if (FAILED(g_device->CreateVertexBuffer((UINT)(g_vertex_count * (int)sizeof(HDVertex)),
		D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &g_vb, 0)))
	{
		*why = "CreateVertexBuffer failed";
		release_buffers();
		return false;
	}

	{
		void* raw = 0;

		if (FAILED(g_vb->Lock(0, 0, &raw, 0)))
		{
			*why = "locking the vertex buffer failed";
			release_buffers();
			return false;
		}

		HDVertex* out = (HDVertex*)raw;

		for (int i = 0; i < g_vertex_count; i++)
		{
			const trf::remaster::TrgVertex& in = trg.vertices[(size_t)i];

			out[i].x = in.x;
			out[i].y = in.y;
			out[i].z = in.z;
			out[i].sway = in.sway;
			out[i].nx = in.nx;
			out[i].ny = in.ny;
			out[i].nz = in.nz;
			out[i].layer = in.layer;
			out[i].c1b = in.colour1[2];
			out[i].c1g = in.colour1[1];
			out[i].c1r = in.colour1[0];
			out[i].u = in.u;
			out[i].c2b = in.colour2[2];
			out[i].c2g = in.colour2[1];
			out[i].c2r = in.colour2[0];
			out[i].v = in.v;

			unsigned char bits = 0;

			if (!vertex_group.empty() && vertex_group[(size_t)i] >= 0)
			{
				const unsigned long long key =
					((unsigned long long)(unsigned short)vertex_group[(size_t)i] << 48) |
					((unsigned long long)(unsigned short)in.x << 32) |
					((unsigned long long)(unsigned short)in.y << 16) |
					(unsigned long long)(unsigned short)in.z;

				std::unordered_map<unsigned long long, unsigned char>::const_iterator
					found = mark_map.find(key);

				if (found != mark_map.end())
					bits = found->second;
			}

			out[i].wChoppy = (unsigned char)(bits & 1);
			out[i].wShimmer = (unsigned char)((bits >> 1) & 1);
			out[i].wRoomShimmer = (unsigned char)((bits >> 2) & 1);
			out[i].wSpare = 1;

			if (bits)
				marked_vertices++;

			marked_choppy += out[i].wChoppy;
			marked_shimmer += out[i].wShimmer;
			marked_room_shimmer += out[i].wRoomShimmer;
		}

		g_vb->Unlock();
	}

	/* --- the index buffer, sorted -------------------------------------
	 *
	 * The .TRG's index stream is not sorted by page: walking ANDREA1 as it
	 * lies costs 32,291 texture changes. Bucketing every group's triangles by
	 * page, stably, brings that down to 1,859 ranges - and a range is exactly
	 * one draw call. The sort is stable so that two triangles on the same
	 * page keep the order the file gave them, which keeps coplanar surfaces
	 * drawing in the artist's order.
	 *
	 * The page a triangle belongs to is its FIRST corner's layer. No triangle
	 * in any of the 15 TR5 levels has three corners that disagree (measured
	 * in stage S0), so this is a lookup, not a decision. */
	std::vector<unsigned int> sorted;
	sorted.reserve(trg.indices.size());

	g_groups.assign(trg.groups.size(), HDGroup());

	int split_ranges = 0;
	int biggest = 0;
	int fallback_tris = 0;
	const unsigned int max_tris = g_caps.MaxPrimitiveCount ? g_caps.MaxPrimitiveCount : 0xFFFFFFFFu;

	std::vector<std::pair<int, unsigned int> > tris;	// page slot, first index

	for (size_t g = 0; g < trg.groups.size(); g++)
	{
		HDGroup& group = g_groups[g];
		const trf::remaster::TrgGroup& source = trg.groups[g];
		const size_t group_first_index = sorted.size();

		for (int pass = 0; pass < 3; pass++)
		{
			tris.clear();

			for (const trf::remaster::TrgPart& part : source.parts)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned int first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					const unsigned int corner = trg.indices[first];
					int slot = -1;

					if (corner < trg.vertices.size())
					{
						const unsigned char layer = trg.vertices[corner].layer;

						if (layer && (size_t)layer <= trg.textures.size())
							slot = HDPageCacheSlotOf((int)trg.textures[layer - 1]);
						else if (!trg.textures.empty())
						{
							/* Layer 0 means "no page named", and TROne's rule
							 * for it is the level's FIRST page rather than
							 * nothing (game_renderer.cpp, hd_load). Dropping
							 * the triangle instead would leave a hole that
							 * looks exactly like a rendering bug, and the
							 * count below says how often it happens. */
							slot = HDPageCacheSlotOf((int)trg.textures[0]);
							fallback_tris++;
						}
					}

					tris.push_back(std::make_pair(slot, first));
				}
			}

			if (tris.empty())
				continue;

			std::stable_sort(tris.begin(), tris.end(),
				[](const std::pair<int, unsigned int>& a, const std::pair<int, unsigned int>& b)
				{ return a.first < b.first; });

			group.triangles[pass] = (unsigned int)tris.size();

			HDRange range;
			range.pageSlot = tris[0].first;
			range.start = (unsigned int)sorted.size();
			range.count = 0;

			for (size_t i = 0; i < tris.size(); i++)
			{
				if (tris[i].first != range.pageSlot || range.count / 3 >= max_tris)
				{
					if (range.count)
					{
						if (tris[i].first == range.pageSlot)
							split_ranges++;

						group.pass[pass].push_back(range);

						if ((int)(range.count / 3) > biggest)
							biggest = (int)(range.count / 3);
					}

					range.pageSlot = tris[i].first;
					range.start = (unsigned int)sorted.size();
					range.count = 0;
				}

				const unsigned int first = tris[i].second;

				for (int corner = 0; corner < 3; corner++)
				{
					const unsigned int index = trg.indices[first + (unsigned int)corner];
					sorted.push_back(index);

					if (index < trg.vertices.size())
					{
						const trf::remaster::TrgVertex& v = trg.vertices[index];
						const float p[3] = { (float)v.x, (float)v.y, (float)v.z };

						if (!group.hasBounds)
						{
							group.hasBounds = true;
							group.mins[0] = group.maxs[0] = p[0];
							group.mins[1] = group.maxs[1] = p[1];
							group.mins[2] = group.maxs[2] = p[2];
						}
						else
							for (int axis = 0; axis < 3; axis++)
							{
								if (p[axis] < group.mins[axis]) group.mins[axis] = p[axis];
								if (p[axis] > group.maxs[axis]) group.maxs[axis] = p[axis];
							}
					}
				}

				range.count += 3;
			}

			if (range.count)
			{
				group.pass[pass].push_back(range);

				if ((int)(range.count / 3) > biggest)
					biggest = (int)(range.count / 3);
			}
		}

		/* THE SAMPLES FOR THE MATRIX PROOF. The .TRG dies with this function
		 * and the vertex buffer is WRITEONLY, so the only chance to keep a
		 * few of a room's own vertices is here. Evenly spaced over the
		 * group's index run rather than the first hundred in a row, so the
		 * proof reaches the far corners of the room and not one wall. */
		{
			const size_t span = sorted.size() - group_first_index;

			if (span)
			{
				const size_t want = span < (size_t)HD_RENDER_SAMPLES
					? span : (size_t)HD_RENDER_SAMPLES;

				group.samples.reserve(want);

				for (size_t s = 0; s < want; s++)
				{
					const unsigned int index = sorted[group_first_index + s * span / want];

					if (index >= trg.vertices.size())
						continue;

					const trf::remaster::TrgVertex& v = trg.vertices[index];
					HDRenderSample sample;

					sample.x = v.x;
					sample.y = v.y;
					sample.z = v.z;
					sample.nx = v.nx;
					sample.ny = v.ny;
					sample.nz = v.nz;
					sample.c1[0] = v.colour1[0];
					sample.c1[1] = v.colour1[1];
					sample.c1[2] = v.colour1[2];
					sample.c2[0] = v.colour2[0];
					sample.c2[1] = v.colour2[1];
					sample.c2[2] = v.colour2[2];
					sample.bits = 0;

					if (!vertex_group.empty())
					{
						const unsigned long long key =
							((unsigned long long)(unsigned short)g << 48) |
							((unsigned long long)(unsigned short)v.x << 32) |
							((unsigned long long)(unsigned short)v.y << 16) |
							(unsigned long long)(unsigned short)v.z;

						std::unordered_map<unsigned long long, unsigned char>::const_iterator
							found = mark_map.find(key);

						if (found != mark_map.end())
							sample.bits = found->second;
					}

					group.samples.push_back(sample);
				}
			}
		}
	}

	const size_t index_bytes = sorted.size() * (g_index32 ? 4u : 2u);

	if (sorted.empty())
	{
		// Every part of every group was empty. CreateIndexBuffer(0) would
		// fail with a less helpful message than this one.
		*why = "the .TRG names no triangle in any group";
		release_buffers();
		return false;
	}

	if (FAILED(g_device->CreateIndexBuffer((UINT)index_bytes, D3DUSAGE_WRITEONLY,
		g_index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_ib, 0)))
	{
		*why = "CreateIndexBuffer failed";
		release_buffers();
		return false;
	}

	{
		void* raw = 0;

		if (FAILED(g_ib->Lock(0, 0, &raw, 0)))
		{
			*why = "locking the index buffer failed";
			release_buffers();
			return false;
		}

		if (g_index32)
			std::memcpy(raw, sorted.empty() ? 0 : &sorted[0], index_bytes);
		else
		{
			unsigned short* out = (unsigned short*)raw;

			for (size_t i = 0; i < sorted.size(); i++)
				out[i] = (unsigned short)sorted[i];
		}

		g_ib->Unlock();
	}

	if (stats)
	{
		stats->trgVersion = trg.version;
		stats->groups = (int)trg.groups.size();
		stats->rooms = numRooms;
		stats->skyGroups = (int)trg.groups.size() > numRooms
			? (int)trg.groups.size() - numRooms : 0;
		stats->vertices = g_vertex_count;
		stats->indices = (int)sorted.size();
		stats->index32 = g_index32 ? 1 : 0;
		stats->waterMarks = g_mark_count;
		stats->markedVertices = marked_vertices;
		stats->markedChoppy = marked_choppy;
		stats->markedShimmer = marked_shimmer;
		stats->markedRoomShimmer = marked_room_shimmer;
		stats->splitRanges = split_ranges;
		stats->biggestRangeTris = biggest;
		stats->fallbackPageTris = fallback_tris;
		stats->vertexBytes = (double)g_vertex_count * (double)sizeof(HDVertex);
		stats->indexBytes = (double)index_bytes;
		stats->readMs = read_ms;
		stats->pages = page_stats;

		for (int g = 0; g < g_room_groups; g++)
		{
			unsigned int here = 0;

			for (int pass = 0; pass < 3; pass++)
			{
				stats->ranges += (int)g_groups[(size_t)g].pass[pass].size();
				here += g_groups[(size_t)g].triangles[pass];
			}

			if (!here)
				stats->emptyGroups++;
		}

		stats->buildMs = now_ms() - build_started;
	}

	return true;
}

void HDRenderLevelEnd()
{
	release_buffers();
	HDPageCacheDestroy();
}

/* ---------------------------------------------------------------- frame -- */

/* THE STATE BRACKET, WITH NO ROOM IN IT (#123, stage S3-1). Everything the
 * classic path set or relies on, saved and put back; everything the HD path
 * needs, set. It sits here rather than in each HD module because there is ONE
 * HD path and there must be ONE list of states - hd_model.cpp draws inside
 * this very bracket, and a second list would be a second chance to forget a
 * state and to spend an evening looking at garbage. */
void HDRenderStateSave()
{
	if (!g_device)
		return;

	/* --- save ---------------------------------------------------------- */
	g_saved = SavedState();

	g_device->GetVertexShader(&g_saved.vs);
	g_device->GetPixelShader(&g_saved.ps);
	g_device->GetVertexDeclaration(&g_saved.decl);
	g_device->GetFVF(&g_saved.fvf);
	g_device->GetStreamSource(0, &g_saved.stream, &g_saved.streamOffset, &g_saved.streamStride);
	g_device->GetIndices(&g_saved.indices);
	g_device->GetTexture(0, &g_saved.texture);

	for (int i = 0; i < kRenderStateCount; i++)
		g_device->GetRenderState(kSavedRenderStates[i], &g_saved.rs[i]);

	for (int i = 0; i < kSamplerStateCount; i++)
		g_device->GetSamplerState(0, kSavedSamplerStates[i], &g_saved.samp[i]);

	/* --- the HD path's own state --------------------------------------- */
	// Real geometry has to be clipped; the classic path switched clipping off
	// because it hands the device vertices it has already clipped itself.
	g_device->SetRenderState(D3DRS_CLIPPING, TRUE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);	// the shader clips
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	// A pixel shader gets no fixed-function fog: with ps_3_0 the fog factor
	// would have to come out of the vertex shader, and ours writes none.
	// Distance fog returns as shader arithmetic in stage S2b.
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	g_device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);

	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	// Mip filtering, unlike the classic path: an HD page is 512x512 where the
	// classic one was 256x256, and a wall at a distance without mips crawls.
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
	g_device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
	g_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

void HDRenderFrameBegin(const D3DMATRIX* view, const D3DMATRIX* projection,
	const HDFrameLighting* frame)
{
	if (!HDRenderReady() || g_in_frame)
		return;

	g_in_frame = true;
	g_draw_calls = 0;

	if (view)
		std::memcpy(g_view, view, sizeof(g_view));
	if (projection)
		std::memcpy(g_proj, projection, sizeof(g_proj));

	multiply(g_view, g_proj, g_viewproj);

	HDRenderStateSave();

	/* The room path's own buffers and shaders. Everything above is shared
	 * with hd_model.cpp; these five lines are what makes the bracket a ROOM
	 * bracket, and hd_model sets its own in their place. */
	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);
	g_device->SetStreamSource(0, g_vb, 0, sizeof(HDVertex));
	g_device->SetIndices(g_ib);

	/* The stage S2b constants. They are set here, inside the bracket, and
	 * nowhere else: with hd_enable off this function is not reached and not
	 * one SetShaderConstant is made. */
	g_water_table_up = (const void*)~(size_t)0;	// force the first upload
	apply_frame_lighting(frame);
}

void HDRenderStateRestore()
{
	if (!g_device)
		return;

	for (int i = 0; i < kRenderStateCount; i++)
		g_device->SetRenderState(kSavedRenderStates[i], g_saved.rs[i]);

	for (int i = 0; i < kSamplerStateCount; i++)
		g_device->SetSamplerState(0, kSavedSamplerStates[i], g_saved.samp[i]);

	g_device->SetTexture(0, g_saved.texture);
	g_device->SetIndices(g_saved.indices);

	/* The classic path has NO stream bound - it draws with DrawPrimitiveUP -
	 * so this usually unbinds stream 0 again. One quirk to know about, found
	 * by RoomProbe's state-bracket check and not by reading: after
	 * SetStreamSource(0, NULL, 0, 0) the runtime still answers 20 to
	 * GetStreamSource's stride. It keeps the last stride it was given and
	 * there is no way to put the old one back - which costs nothing, because
	 * the stride of an unbound stream is read by nobody. */
	g_device->SetStreamSource(0, g_saved.stream, g_saved.streamOffset, g_saved.streamStride);

	/* The declaration goes back through the FVF whenever there was one - that
	 * is the shape the classic path set it in (function_table.cpp,
	 * SetFVF(FVF)), and putting the FVF back is what makes GetFVF answer
	 * again for whoever asks next. A caller that really had a declaration of
	 * its own gets that instead. */
	if (g_saved.fvf)
		g_device->SetFVF(g_saved.fvf);
	else
		g_device->SetVertexDeclaration(g_saved.decl);

	g_device->SetVertexShader(g_saved.vs);
	g_device->SetPixelShader(g_saved.ps);

	if (g_saved.texture) { g_saved.texture->Release(); g_saved.texture = 0; }
	if (g_saved.indices) { g_saved.indices->Release(); g_saved.indices = 0; }
	if (g_saved.stream) { g_saved.stream->Release(); g_saved.stream = 0; }
	if (g_saved.decl) { g_saved.decl->Release(); g_saved.decl = 0; }
	if (g_saved.vs) { g_saved.vs->Release(); g_saved.vs = 0; }
	if (g_saved.ps) { g_saved.ps->Release(); g_saved.ps = 0; }
}

void HDRenderFrameEnd()
{
	if (!g_in_frame)
		return;

	g_in_frame = false;
	HDRenderStateRestore();
}

namespace
{
	void draw_pass(const HDGroup& group, int pass, const float* wvp)
	{
		if (group.pass[pass].empty())
			return;

		/* gPass: x switches the alpha test on, y the additive weighting,
		 * z is the room shadow (1 until shadow maps exist), w the specular
		 * add - the classic path switches D3DRS_SPECULARENABLE off for its
		 * additive draw type (polyinsert.cpp:72) and the overbright half of
		 * the room colour goes off with it. */
		const float constants[4] =
		{
			pass == 1 ? 1.0f : 0.0f,
			pass == 2 ? 1.0f : 0.0f,
			1.0f,
			pass == 2 ? 0.0f : 1.0f
		};

		g_device->SetVertexShaderConstantF(4, constants, 1);
		g_device->SetPixelShaderConstantF(4, constants, 1);

		if (pass == 2)
		{
			// The additive pass paints light, not surface: it must not hide
			// what is behind it, so it writes no depth.
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
			g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		}

		for (const HDRange& range : group.pass[pass])
		{
			IDirect3DTexture9* page = HDPageCacheSlotTexture(range.pageSlot);

			if (!page)
				continue;

			g_device->SetTexture(0, page);
			g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
				(UINT)g_vertex_count, range.start, range.count / 3);
			g_draw_calls++;
		}

		if (pass == 2)
		{
			g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
			g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		}

		(void)wvp;
	}

	bool prepare(int group, const D3DMATRIX* world, const HDRoomLighting* lighting,
		float* wvp)
	{
		if (!g_in_frame || !g_vb || !g_ib)
			return false;

		if (group < 0 || group >= (int)g_groups.size())
			return false;

		const float* row = world ? (const float*)world : kIdentity;

		/* THE VIEW SPACE THE FOG AND THE BULBS LIVE IN. The classic
		 * "stash" is the vertex through D3DMView, which is view * world - so
		 * that is what goes up, not the world matrix alone. In the engine the
		 * two are the same thing (the caller hands the finished D3DMView in
		 * and the view is identity); in the diagnostic tool they are not (the
		 * world is identity and the view is real), and a fog that only worked
		 * in one of the two would be a fog nobody could measure. */
		float worldview[16];

		multiply(row, g_view, worldview);
		multiply(row, g_viewproj, wvp);
		set_matrix_constant(0, wvp);
		set_worldview_constant(worldview);
		apply_room_lighting(lighting);
		return true;
	}
}

bool HDRenderRoom(int group, const D3DMATRIX* world, const HDRoomLighting* lighting)
{
	float wvp[16];

	if (!prepare(group, world, lighting, wvp))
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.triangles[0] && !room.triangles[1] && !room.triangles[2])
		return false;	// the .TRG has nothing here - that room stays classic

	for (int pass = 0; pass < 3; pass++)
		draw_pass(room, pass, wvp);

	return true;
}

bool HDRenderRoomPass(int group, int pass, const D3DMATRIX* world,
	const HDRoomLighting* lighting)
{
	float wvp[16];

	if (pass < 0 || pass > 2 || !prepare(group, world, lighting, wvp))
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.triangles[pass])
		return false;

	draw_pass(room, pass, wvp);
	return true;
}

int HDRenderGroupCount()
{
	return (int)g_groups.size();
}

int HDRenderGroupTriangles(int group, int pass)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	if (pass >= 0 && pass <= 2)
		return (int)g_groups[(size_t)group].triangles[pass];

	return (int)(g_groups[(size_t)group].triangles[0] +
		g_groups[(size_t)group].triangles[1] +
		g_groups[(size_t)group].triangles[2]);
}

int HDRenderGroupRanges(int group, int pass)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	if (pass >= 0 && pass <= 2)
		return (int)g_groups[(size_t)group].pass[pass].size();

	return (int)(g_groups[(size_t)group].pass[0].size() +
		g_groups[(size_t)group].pass[1].size() +
		g_groups[(size_t)group].pass[2].size());
}

int HDRenderGroupSampleData(int group, HDRenderSample* out, int maxVerts)
{
	if (group < 0 || group >= (int)g_groups.size() || !out || maxVerts <= 0)
		return 0;

	const HDGroup& g = g_groups[(size_t)group];
	const int count = (int)g.samples.size() < maxVerts ? (int)g.samples.size() : maxVerts;

	for (int i = 0; i < count; i++)
		out[i] = g.samples[(size_t)i];

	return count;
}

int HDRenderGroupSampleCount(int group)
{
	if (group < 0 || group >= (int)g_groups.size())
		return 0;

	return (int)(g_groups[(size_t)group].samples.size() / 3);
}

int HDRenderGroupSamples(int group, short* xyz, int maxVerts)
{
	if (group < 0 || group >= (int)g_groups.size() || !xyz || maxVerts <= 0)
		return 0;

	const HDGroup& g = g_groups[(size_t)group];
	const int count = (int)g.samples.size() < maxVerts ? (int)g.samples.size() : maxVerts;

	for (int i = 0; i < count; i++)
	{
		xyz[i * 3 + 0] = g.samples[(size_t)i].x;
		xyz[i * 3 + 1] = g.samples[(size_t)i].y;
		xyz[i * 3 + 2] = g.samples[(size_t)i].z;
	}

	return count;
}

bool HDRenderGroupBounds(int group, float* mins, float* maxs)
{
	if (group < 0 || group >= (int)g_groups.size())
		return false;

	const HDGroup& room = g_groups[(size_t)group];

	if (!room.hasBounds)
		return false;

	if (mins)
		std::memcpy(mins, room.mins, sizeof(room.mins));
	if (maxs)
		std::memcpy(maxs, room.maxs, sizeof(room.maxs));

	return true;
}

int HDRenderDrawCalls()
{
	return g_draw_calls;
}

/* ----------------------------------------------------------- projection -- */

void HDRenderZConstants(double mznear, double mzfar, double* a, double* b, double* boo)
{
	/* SetupZRange (specific\3dmath.cpp), line for line and in single
	 * precision, because that is what the classic path writes into the depth
	 * buffer and the point of the exercise is to match it, not to be more
	 * accurate than it. */
	const float f_mznear = (float)mznear;
	const float f_mzfar = (float)mzfar;

	float f_b = f_mzfar * f_mznear * 0.99f / (f_mznear - f_mzfar);
	const float f_a = 0.005f - f_b / f_mznear;
	f_b = -f_b;
	const float f_boo = f_b / 2048.0f;	// mone

	if (a) *a = f_a;
	if (b) *b = f_b;
	if (boo) *boo = f_boo;
}

void HDRenderMakeProjection(double persp, double centerX, double centerY,
	double a, double b, D3DMATRIX* out)
{
	if (!out)
		return;

	std::memset(out, 0, sizeof(*out));

	/* X AND Y. The classic path writes screen pixels straight into an
	 * XYZRHW vertex: sx = x * persp / z + centerx. The transformed path goes
	 * through the viewport instead: sx = (ndc + 1) * width / 2. With
	 * centerx = width / 2 the two are the same line as long as
	 * _11 = persp / centerx, and there is no half pixel anywhere - Direct3D 9
	 * puts pre-transformed and transformed vertices into the SAME screen
	 * space, so a shift here would be a shift against the classic path.
	 *
	 * Y is negated because screen y counts downwards and normalised device y
	 * counts upwards. */
	out->_11 = (float)(persp / centerX);
	out->_22 = (float)(-persp / centerY);

	/* Z. This is the whole reason the two paths can share one depth buffer:
	 *
	 *     classic   sz    = f_a - f_b / z
	 *     matrix    z_clip = f_a * z - f_b,  w_clip = z
	 *               depth  = z_clip / w_clip = f_a - f_b / z
	 *
	 * identical, not approximately. The classic range is 0.005 at the near
	 * plane and 0.995 at the far one - Core's own 1 % margin - and the matrix
	 * inherits it, so a viewport with MinZ 0 and MaxZ 1 needs no adjustment
	 * either. */
	out->_33 = (float)a;
	out->_43 = (float)-b;
	out->_34 = 1.0f;
	out->_44 = 0.0f;
}

double HDRenderSelfTest(double mznear, double mzfar, double persp,
	HDRenderZSample* rows, int rowCount)
{
	double a = 0, b = 0, boo = 0;
	HDRenderZConstants(mznear, mzfar, &a, &b, &boo);

	const float f_persp = (float)persp;
	const float f_moneopersp = 2048.0f / f_persp;
	const float f_a = (float)a;
	const float f_boo = (float)boo;

	const int samples = 50;
	double worst = 0;

	for (int i = 0; i < samples; i++)
	{
		/* Spread over the reciprocal, not over z: that is where a depth
		 * buffer's resolution lives, and where a mismatch would show first. */
		const double t = (double)i / (double)(samples - 1);
		const double inv = (1.0 / mznear) + t * ((1.0 / mzfar) - (1.0 / mznear));
		const double z = 1.0 / inv;

		// The classic path, in its own arithmetic (drawroom.cpp:379 for the
		// rhw, polyinsert.cpp:434 for the sz).
		const float zv = f_persp / (float)z;
		const float rhw = zv * f_moneopersp;
		const float classic = f_a - f_boo * rhw;

		// The device: z_clip / w_clip, then the viewport's MinZ 0 / MaxZ 1.
		const float z_clip = (float)a * (float)z + (float)(-b);
		const float w_clip = (float)z;
		const float matrix = z_clip / w_clip;

		const double diff = fabs((double)classic - (double)matrix);

		if (diff > worst)
			worst = diff;

		if (rows && i < rowCount)
		{
			rows[i].zView = z;
			rows[i].classic = classic;
			rows[i].matrix = matrix;
			rows[i].diff = diff;
		}
	}

	return worst;
}

unsigned long HDRenderPixelSlots()
{
	return (unsigned long)g_caps.PS20Caps.NumInstructionSlots;
}

void HDRenderDepthTruth(IDirect3DDevice9* device, unsigned long* zenable,
	unsigned long* zfunc, unsigned long* zwrite, float* minZ, float* maxZ)
{
	DWORD value = 0;
	D3DVIEWPORT9 viewport;

	if (zenable) *zenable = 0;
	if (zfunc) *zfunc = 0;
	if (zwrite) *zwrite = 0;
	if (minZ) *minZ = 0;
	if (maxZ) *maxZ = 0;

	if (!device)
		return;

	if (zenable && SUCCEEDED(device->GetRenderState(D3DRS_ZENABLE, &value)))
		*zenable = value;
	if (zfunc && SUCCEEDED(device->GetRenderState(D3DRS_ZFUNC, &value)))
		*zfunc = value;
	if (zwrite && SUCCEEDED(device->GetRenderState(D3DRS_ZWRITEENABLE, &value)))
		*zwrite = value;

	std::memset(&viewport, 0, sizeof(viewport));

	if (SUCCEEDED(device->GetViewport(&viewport)))
	{
		if (minZ) *minZ = viewport.MinZ;
		if (maxZ) *maxZ = viewport.MaxZ;
	}
}

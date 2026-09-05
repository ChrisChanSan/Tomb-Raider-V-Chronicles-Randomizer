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
#include <cmath>
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

	/* One draw call: a run of indices that all name the same page AND the same
	 * UV scroll. The scroll is stage S5: a .TRG part carries a material and
	 * four parameters, and material 1 is a scroll whose param.xy is a speed in
	 * page units per second (Trg.h has the part layout; hd_room.hlsl has the
	 * calibration against the classic UVROT branch). It has to be part of the
	 * sort key, because a range is one SetTexture and one constant and a
	 * triangle cannot have half of each. Costs nothing where nothing scrolls:
	 * over all 15 levels only 5 to 65 parts per level carry one. */
	/* THE THREE .TRG MATERIALS THIS FILE KNOWS BY NUMBER. Measured over all
	 * 15 TR5 levels (Preparation\Tools\Diagnose and the notes in
	 * MODIFICATIONS.md); everything else is drawn the same way and needs no
	 * name here.
	 *
	 *    1  the FLOW material: a UV scroll, speed in param.xy - waterfalls,
	 *       rivers, conveyor belts. Its vertices may also carry a bend, and
	 *       that bend is the water's own (fix round A, A6)
	 *    7  a WIND material, param (alphaRef 0.5, strength, scale, scale)
	 *    8  the other WIND material, the same param shape - foliage, ivy,
	 *       banners, kelp. 7 and 8 are the same thing in two numbers: no
	 *       level carries both, ANDREA3 has only 7 and JOBY4 only 8, and
	 *       TITLE - which is a montage of every level - has both
	 *    6  the plain CUT-OUT material - gratings, nets, railings, ladders.
	 *       param[0] is its alpha reference and is 0.5 in 10 of the 11
	 *       tuples the game has (ANDY1 carries one 0.503)
	 *   13  the remaster's own four-frame flipbook, the other three pages in
	 *       param[0..2]. Counted and NOT drawn - see MODIFICATIONS.md */
#define HD_TRG_MATERIAL_FLOW		1
#define HD_TRG_MATERIAL_MASK		6
#define HD_TRG_MATERIAL_WIND		8
#define HD_TRG_MATERIAL_WIND2		7
#define HD_TRG_MATERIAL_FLIPBOOK	13

	/* THE ALPHA REFERENCE OF ONE PART (#123, fix round A2).
	 *
	 * THE BUG THIS REPLACES. Fix round A switched the opaque pass's alpha
	 * test on per PAGE, out of a measured share of texels below alpha 128,
	 * on the argument that an ordinary opaque page's alpha channel is a flat
	 * 255 and a test on it changes nothing. The argument is false, and the
	 * play test said where: ANDREA1's stucco facades (page 201, 95 % of it
	 * below 128, and page 211, 96 %), JOBY2's snow (page 480, 99 %), JOBY3's
	 * bulkheads (pages 3219 to 4823, 41 to 71 %) and ANDY1's gravel (pages
	 * 3268 to 3272, 60 to 88 %) all carry a MASK in their alpha channel -
	 * gloss, wetness, whatever the remaster's own shader reads it as - while
	 * their RGB is a whole surface. Testing it threw away most of a Roman
	 * shop front, the wall under the Caffe Recco's awning, and the puddles
	 * of the crane yard.
	 *
	 * WHAT SAYS IT INSTEAD. The material, and the material alone. Measured
	 * over the 15 .TRG files, the distinct (material, param) tuples:
	 *
	 *   material 6   param[0] 0.500 (once 0.503), opaque only, 513,647 tris
	 *   material 7   param[0] 0.500 in all 7 tuples, opaque, 88,726 tris
	 *   material 8   param[0] 0.500 in 10 of 11 tuples, the odd one 0.120
	 *                and in the ALPHA pass, 1,308,742 tris
	 *   material 3   param[0] 1, 2, 4, 26, 32, 35, 39, 56, 69, 78, 107,
	 *                109, 110, 141, 161, 191 - a specular exponent
	 *   material 17  param[0] 22, 25, 36, 38, 63, 70, 72 - the same shape
	 *   material 13  param[0..2] are page numbers, material 14 likewise
	 *   material 2   param[0] 0.5, 1.0, 1.5, but ADDITIVE in every one of
	 *                its 20 tuples, where no alpha test runs at all
	 *
	 * So 6, 7 and 8 are the three materials whose param[0] is an alpha
	 * reference, and every other material's param[0] is measurably
	 * something else. The test is switched on for those three and reads its
	 * threshold out of the file rather than assuming 0.5 - which is what
	 * makes ANDY1's 0.503 right as well.
	 *
	 * Zero means "no test": the shader clips on texel.a - gPass.x, and an
	 * alpha is never below zero.
	 *
	 * COMPARED WHOLE, NOT BY ITS LOW BYTE. A destructible static carries its
	 * material with bit 15 set and a counter in bits 8 to 14 - 0x8003 and
	 * 0x820E are material 3 and material 14 of RICH3 - so masking would be
	 * the more general test. It is not done, because it would be a guess
	 * about an encoding to no measurable end: over the 15 files the only low
	 * bytes that ever appear with the high bit are 0, 3 and 14, and none of
	 * the three is a cut-out material. */
	inline float MaterialAlphaRef(const trf::remaster::TrgPart& part)
	{
		if (part.material != HD_TRG_MATERIAL_MASK &&
			part.material != HD_TRG_MATERIAL_WIND &&
			part.material != HD_TRG_MATERIAL_WIND2)
			return 0.0f;

		/* A reference outside 0..1 would clip everything or nothing; no part
		 * in the game has one, and this says so rather than trusting it. */
		return part.param[0] > 0.0f && part.param[0] < 1.0f
			? part.param[0] : 0.5f;
	}

	struct HDRange
	{
		unsigned int start = 0;
		unsigned int count = 0;
		int pageSlot = -1;
		float scrollU = 0.0f;
		float scrollV = 0.0f;
		/* The alpha reference of the part this range came out of, or 0 for
		 * "no test" - see MaterialAlphaRef above (#123, fix round A2). Part
		 * of the sort key with the page and the scroll, for the same reason
		 * they are: it is one pixel shader constant per draw call and a
		 * triangle cannot have half of it. */
		float alphaRef = 0.0f;
		/* The lowest vertex number this range names and how many it reaches
		 * over (#123, stage S6). With a 32 bit index buffer they are only a
		 * measurement; with a 16 bit one they ARE the draw call - the indices
		 * are written relative to `baseVertex` and the vertex stream is moved
		 * to that vertex before the draw, which is how 16 bit indices address
		 * a level with more than 65536 vertices. */
		unsigned int baseVertex = 0;
		unsigned int vertexSpan = 0;
	};

	struct HDGroup
	{
		std::vector<HDRange> pass[3];
		std::vector<HDRenderSample> samples;
		unsigned int triangles[3] = { 0, 0, 0 };
		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };
		bool hasBounds = false;
		HDRenderGroupMotion motion = {};
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

	/* The two caps the diagnostic tool is allowed to bend (#123, stage S6).
	 * Zero means "leave the device's own answer alone", which is what the
	 * engine always uses. */
	int g_cap_max_side = 0;
	int g_cap_no_index32 = 0;

	SavedState g_saved;
	bool g_in_frame = false;
	int g_draw_calls = 0;
	// A2: draw calls of the opaque pass that ran with the alpha test on
	// because their part is a cut-out material. Reset with g_draw_calls.
	int g_cutout_calls = 0;
	// A2, once per level: the opaque parts of materials 6, 7 and 8.
	int g_cutout_parts = 0;

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

	/* Stage S5. The register the three motions ride on, kept here because its
	 * first two components change per DRAW CALL and the other two per frame -
	 * so the frame writes into this array and every range rewrites register
	 * 13 out of it. `g_wrapping` remembers whether the sampler is currently in
	 * WRAP, so a level with no scroll in it never touches the state at all. */
	float g_motion[4] = { 0, 0, 0, 0 };
	bool g_wrapping = false;
	int g_scroll_parts = 0;

	/* Set at the top of every pass, so the first draw call of a room always
	 * writes register 13 even when its offset happens to equal the last
	 * room's. It has to: the MODEL shader's gJoints occupy c8 to c103
	 * (hd_model.hlsl), so an object drawn between two rooms overwrites this
	 * register, and a skipped upload would then hand the room shader a joint
	 * matrix where it expects a clock. Within one room the compare still
	 * saves the upload, which is where the thousand draw calls are. */
	bool g_motion_dirty = true;

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

		/* Stage S5: the clock and the switch. The scroll offset in xy belongs
		 * to a draw call and is written by draw_pass; here only the two the
		 * whole frame shares. With motion off both are zero and every term in
		 * the shader that reads them is multiplied out. */
		g_motion[0] = 0.0f;
		g_motion[1] = 0.0f;
		g_motion[2] = f ? f->clock : 0.0f;
		g_motion[3] = (f && f->motion) ? 1.0f : 0.0f;

		g_device->SetVertexShaderConstantF(9, screen, 1);
		g_device->SetVertexShaderConstantF(13, g_motion, 1);
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

	/* The overrides, if a diagnostic tool asked for them. They are applied to
	 * the copy of the caps, so everything below - the level build, the page
	 * budget, the draw calls - reads the small card's answer and not the real
	 * one. */
	if (g_cap_max_side > 0)
	{
		g_caps.MaxTextureWidth = (DWORD)g_cap_max_side;
		g_caps.MaxTextureHeight = (DWORD)g_cap_max_side;
	}

	if (g_cap_no_index32)
		g_caps.MaxVertexIndex = 0xFFFF;

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

void HDRenderCapOverride(int maxTextureSide, int noIndex32)
{
	g_cap_max_side = maxTextureSide > 0 ? maxTextureSide : 0;
	g_cap_no_index32 = noIndex32 ? 1 : 0;
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
	g_cutout_calls = 0;
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

	/* 32 bit indices unless the level is small enough not to need them, or
	 * the device cannot do them. ANDREA2 has 5,471,013 indices over 1.1 M
	 * vertices; there is no 16 bit reading of that AS ONE STREAM.
	 *
	 * A device that answers 65535 to MaxVertexIndex is not turned away any
	 * more (#123, stage S6). Every draw call in this renderer is one range,
	 * and a range reaches over far fewer vertices than the level has - so the
	 * 16 bit path rebases each range's indices on the lowest vertex it names
	 * and moves the STREAM to that vertex instead, with SetStreamSource's
	 * byte offset. Sixteen bit indices then address 65536 vertices from
	 * wherever the range sits, and the level's 1.1 million are reached a
	 * window at a time. Whether every range really fits in such a window is a
	 * measurement, taken below and refused out loud when it fails.
	 *
	 * The stream offset was chosen over DrawIndexedPrimitive's BaseVertexIndex
	 * on purpose: a device that reports 65535 is telling us what it can
	 * address, and adding a base to the index does not obviously stay under
	 * that ceiling, while a moved stream plainly does - it never shows the
	 * device more than 65536 vertices at once. */
	const bool device_index32 = g_caps.MaxVertexIndex > 0xFFFFu;

	g_index32 = g_vertex_count > 65535 && device_index32;

	/* The HIGHEST index, not the count: MaxVertexIndex is a DWORD and a card
	 * that reports 0xFFFFFFFF makes "count > MaxVertexIndex + 1" wrap to
	 * "count > 0" and refuses every level there is. Only the 32 bit path has
	 * to pass it; the 16 bit path never offers the device more than one
	 * window. */
	if (g_index32 && (unsigned)(g_vertex_count - 1) > g_caps.MaxVertexIndex)
	{
		*why = "the level has more vertices than the device can index";
		return false;
	}

	/* --- the pages ----------------------------------------------------
	 *
	 * THE DEVICE'S SIZE LIMIT GOES INTO THE POLICY HERE (#123, stage S6) and
	 * not in hd_level: the format and the budget are the user's settings, but
	 * how wide a texture this card can hold is a fact of the device, and this
	 * is the one place that has both the caps and the cache in hand. Eleven
	 * remaster pages are 4096x4096 and a card that stops at 2048 gets them
	 * one level down instead of not at all. */
	{
		HDPageCachePolicy policy;

		HDPageCachePolicyNow(&policy);
		policy.maxSide = g_caps.MaxTextureWidth && g_caps.MaxTextureHeight
			? (int)(g_caps.MaxTextureWidth < g_caps.MaxTextureHeight
				? g_caps.MaxTextureWidth : g_caps.MaxTextureHeight)
			: 0;
		HDPageCacheConfigure(&policy);
	}

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

	/* --- THE WIND BELONGS TO MATERIAL 8 (#123, fix round A, A6) --------
	 *
	 * TrgVertex::sway is a weight and nothing else - it says how far a vertex
	 * may travel, not that it travels at all. Stage S5 read it as permission,
	 * and the result is the reported bug: "the lamp post must not move at
	 * all", and with it a long list of statics and animatings that sway.
	 *
	 * MEASURED over all 15 levels, vertices with a non-zero sway, by the
	 * material of the part that names them:
	 *
	 *     material  8   416,153     material  0   160,620
	 *     material  1    92,896     material 14     2,016
	 *     material  7    46,611     material  3       867
	 *                               material 16       594
	 *                               material  6       222
	 *
	 * The left column moves and the right column must not, and the parts
	 * themselves say which is which:
	 *
	 *   8 and 7 are the WIND materials, and they are the same thing under two
	 *     numbers - param (alphaRef 0.5, strength, scale, scale) in both, the
	 *     cut-out sheets of leaves A2 is about, and no level carries both
	 *     (ANDREA3 has only 7, JOBY4 only 8; TITLE, a montage of every level,
	 *     has both). Ivy, banners, grass, kelp.
	 *   1 is the FLOW material, the UV scroll of a river or a waterfall, and
	 *     its bend is the water's own - JOBY5's sub pens are 63,198 of those
	 *     92,896. Taking it away would be the OTHER reported bug, the one
	 *     that says water animation is missing in places.
	 *   0, 3, 6, 14 and 16 are ordinary geometry, glass, glow and light
	 *     shafts. In ANDREA1 - the level the lamp post stands in - material 0
	 *     alone is 61,208 of the level's 84,408 swaying vertices, FOUR FIFTHS
	 *     of them, and that is the reported bug.
	 *
	 * The weight is therefore taken away from every vertex that no part of
	 * material 1, 7 or 8 names, once, here, before the vertex buffer is
	 * filled - so the shader keeps its one multiplication, the log's counters
	 * and HDRenderGroupMotionInfo report what really moves, and nothing
	 * downstream has to know about materials.
	 *
	 * A vertex named by BOTH a moving part and a still one keeps its weight:
	 * it is part of the moving mesh, and the alternative would be to split
	 * it. */
	int wind_parts = 0;
	int wind_vertices = 0;
	int sway_dropped = 0;

	{
		std::vector<unsigned char> wind_ok(trg.vertices.size(), 0);

		for (const trf::remaster::TrgGroup& group : trg.groups)
			for (const trf::remaster::TrgPart& part : group.parts)
			{
				if (part.material != HD_TRG_MATERIAL_WIND &&
					part.material != HD_TRG_MATERIAL_WIND2 &&
					part.material != HD_TRG_MATERIAL_FLOW)
					continue;

				wind_parts++;

				for (int p = 0; p < 3; p++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[p];

					for (unsigned int at = run.start;
						at < run.start + run.count && at < trg.indices.size(); at++)
					{
						const unsigned int v = trg.indices[at];

						if (v < wind_ok.size() && !wind_ok[v])
						{
							wind_ok[v] = 1;
							wind_vertices++;
						}
					}
				}
			}

		for (size_t i = 0; i < trg.vertices.size(); i++)
			if (trg.vertices[i].sway && !wind_ok[i])
			{
				trg.vertices[i].sway = 0;
				sway_dropped++;
			}
	}

	int marked_vertices = 0;
	int marked_choppy = 0;
	int sway_vertices = 0;
	g_scroll_parts = 0;
	g_cutout_parts = 0;
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

			// Stage S5 reads this one in the shader; the count is for the log
			// and for the tool, and it is the whole level in one number.
			if (in.sway)
				sway_vertices++;

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

	/* page slot, the part's scroll, first index - the sort key of a draw call
	 * and the triangle it names. */
	struct HDTri
	{
		int slot;
		float scrollU, scrollV;
		float alphaRef;			// fix round A2, see MaterialAlphaRef
		unsigned int first;
	};

	std::vector<HDTri> tris;
	std::vector<unsigned char> sway_seen(trg.vertices.size(), 0);

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

				/* MATERIAL 1 IS THE UV SCROLL (stage S5). Everything else
				 * draws still; material 13 - the remaster's own four frame
				 * page flipbook - is counted here and not drawn, because a
				 * range names one page and swapping it per frame is a
				 * different piece of work (MODIFICATIONS.md says so out
				 * loud). */
				const bool scrolls = (part.material == HD_TRG_MATERIAL_FLOW) &&
					(part.param[0] != 0.0f || part.param[1] != 0.0f);
				const float su = scrolls ? part.param[0] : 0.0f;
				const float sv = scrolls ? part.param[1] : 0.0f;
				/* A2: the cut-out materials only, and only where the test
				 * belongs - the alpha pass has its own reference and the
				 * additive pass has none. */
				const float aref = pass == 0 ? MaterialAlphaRef(part) : 0.0f;

				if (pass == 0 && aref != 0.0f)
					g_cutout_parts++;

				if (pass == 0 && part.material == HD_TRG_MATERIAL_FLIPBOOK)
					group.motion.flipbookParts++;

				if (pass == 0 && scrolls)
					g_scroll_parts++;

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

					HDTri tri;

					tri.slot = slot;
					tri.scrollU = su;
					tri.scrollV = sv;
					tri.alphaRef = aref;
					tri.first = first;
					tris.push_back(tri);
				}
			}

			if (tris.empty())
				continue;

			std::stable_sort(tris.begin(), tris.end(),
				[](const HDTri& a, const HDTri& b)
				{
					if (a.slot != b.slot) return a.slot < b.slot;
					if (a.scrollU != b.scrollU) return a.scrollU < b.scrollU;
					if (a.scrollV != b.scrollV) return a.scrollV < b.scrollV;
					return a.alphaRef < b.alphaRef;
				});

			group.triangles[pass] = (unsigned int)tris.size();

			HDRange range;
			range.pageSlot = tris[0].slot;
			range.scrollU = tris[0].scrollU;
			range.scrollV = tris[0].scrollV;
			range.alphaRef = tris[0].alphaRef;
			range.start = (unsigned int)sorted.size();
			range.count = 0;

			for (size_t i = 0; i < tris.size(); i++)
			{
				const bool same = tris[i].slot == range.pageSlot &&
					tris[i].scrollU == range.scrollU &&
					tris[i].scrollV == range.scrollV &&
					tris[i].alphaRef == range.alphaRef;

				if (!same || range.count / 3 >= max_tris)
				{
					if (range.count)
					{
						if (same)
							split_ranges++;

						group.pass[pass].push_back(range);

						if (range.scrollU != 0.0f || range.scrollV != 0.0f)
						{
							group.motion.scrollRanges++;
							group.motion.scrollTris += (int)(range.count / 3);

							if (fabsf(range.scrollU) + fabsf(range.scrollV) >
								fabsf(group.motion.scrollU) + fabsf(group.motion.scrollV))
							{
								group.motion.scrollU = range.scrollU;
								group.motion.scrollV = range.scrollV;
							}
						}

						if ((int)(range.count / 3) > biggest)
							biggest = (int)(range.count / 3);
					}

					range.pageSlot = tris[i].slot;
					range.scrollU = tris[i].scrollU;
					range.scrollV = tris[i].scrollV;
					range.alphaRef = tris[i].alphaRef;
					range.start = (unsigned int)sorted.size();
					range.count = 0;
				}

				const unsigned int first = tris[i].first;

				for (int corner = 0; corner < 3; corner++)
				{
					const unsigned int index = trg.indices[first + (unsigned int)corner];
					sorted.push_back(index);

					if (index < trg.vertices.size())
					{
						const trf::remaster::TrgVertex& v = trg.vertices[index];
						const float p[3] = { (float)v.x, (float)v.y, (float)v.z };

						/* Stage S5, per room: which of this room's vertices
						 * carry a wind weight and how strong the strongest
						 * is. Counted here because this is the one place a
						 * vertex and its group are both in hand, and counted
						 * once per vertex rather than once per triangle -
						 * hence the seen flag. */
						if (v.sway && index < sway_seen.size() && !sway_seen[index])
						{
							const int weight = v.sway < 0 ? -(int)v.sway : (int)v.sway;

							sway_seen[index] = 1;
							group.motion.swayVertices++;

							if (weight > group.motion.swayMax)
								group.motion.swayMax = weight;
						}

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

				if (range.scrollU != 0.0f || range.scrollV != 0.0f)
				{
					group.motion.scrollRanges++;
					group.motion.scrollTris += (int)(range.count / 3);

					if (fabsf(range.scrollU) + fabsf(range.scrollV) >
						fabsf(group.motion.scrollU) + fabsf(group.motion.scrollV))
					{
						group.motion.scrollU = range.scrollU;
						group.motion.scrollV = range.scrollV;
					}
				}

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

	if (sorted.empty())
	{
		// Every part of every group was empty. CreateIndexBuffer(0) would
		// fail with a less helpful message than this one.
		*why = "the .TRG names no triangle in any group";
		release_buffers();
		return false;
	}

	/* --- THE VERTEX WINDOW OF EVERY DRAW CALL (#123, stage S6) ---------
	 *
	 * The lowest and highest vertex number each range names. On a normal card
	 * this is a number for the log; on a card with no 32 bit index buffer it
	 * is the whole question, because a 16 bit index reaches 65536 vertices
	 * from wherever the stream is moved to. One pass over the sorted stream,
	 * a few milliseconds on the largest level, and it answers both. */
	int index_window = 0;
	int triangle_window = 0;

	for (HDGroup& group : g_groups)
		for (int pass = 0; pass < 3; pass++)
			for (HDRange& range : group.pass[pass])
			{
				unsigned int lo = 0xFFFFFFFFu;
				unsigned int hi = 0;

				for (unsigned int at = 0; at + 2 < range.count; at += 3)
				{
					const unsigned int a = sorted[range.start + at];
					const unsigned int b = sorted[range.start + at + 1];
					const unsigned int c = sorted[range.start + at + 2];

					unsigned int tlo = a < b ? a : b;
					unsigned int thi = a > b ? a : b;

					if (c < tlo) tlo = c;
					if (c > thi) thi = c;

					if ((int)(thi - tlo + 1) > triangle_window)
						triangle_window = (int)(thi - tlo + 1);

					if (tlo < lo) lo = tlo;
					if (thi > hi) hi = thi;
				}

				if (!range.count)
					continue;

				range.baseVertex = lo;
				range.vertexSpan = hi - lo + 1;

				if ((int)range.vertexSpan > index_window)
					index_window = (int)range.vertexSpan;
			}

	/* --- WHAT A CARD WITH NO 32 BIT INDEX BUFFER CAN DO (#123, stage S6) --
	 *
	 * Nothing, on this art, and that is a measurement rather than a shrug.
	 * The obvious rescue is to cut a draw call into pieces that each stay
	 * inside a 65536 vertex window and move the vertex stream to each piece;
	 * the floor under that is ONE TRIANGLE, which cannot be cut. Measured
	 * over all fifteen levels, the widest single triangle reaches over
	 * 33,585 vertices in RICHCUT2 and between 235,507 (JOBY4) and 1,391,696
	 * (RICH2) in every other one - the .TRG deduplicates its vertices over
	 * the whole level rather than writing them room by room, so the three
	 * corners of one wall tile can sit a million entries apart.
	 *
	 * So RICHCUT2 draws in HD on such a card and the other fourteen levels
	 * stay classic, with the number in the log. The way to change that is to
	 * give the 16 bit path a vertex buffer of its own, compacted per room -
	 * a second buffer layout, for hardware that has not shipped since about
	 * 2002. It is not built, and MODIFICATIONS.md says so.
	 */
	if (!g_index32 && triangle_window > 65536)
	{
		char line[220];

		std::snprintf(line, sizeof(line),
			"the device has no 32 bit index buffer and one triangle of this "
			"level reaches over %d vertices - no cut can go below that",
			triangle_window);
		g_error = line;
		*why = g_error.c_str();
		release_buffers();
		return false;
	}

	if (!g_index32 && index_window > 65536)
	{
		/* Cannot be reached while the test above stands - a range is at least
		 * as wide as its widest triangle - but a .TRG that broke that would
		 * otherwise write indices no card could follow. */
		g_error = "one draw call reaches over more than 65536 vertices and the "
			"device has no 32 bit index buffer";
		*why = g_error.c_str();
		release_buffers();
		return false;
	}

	/* THE REBASE. With 16 bit indices every range's numbers are written
	 * relative to the lowest vertex it names, and the draw call moves the
	 * stream to that vertex - see HDRenderRoomPass. Done in place because
	 * `sorted` is not read again after this. */
	if (!g_index32)
		for (const HDGroup& group : g_groups)
			for (int pass = 0; pass < 3; pass++)
				for (const HDRange& range : group.pass[pass])
					for (unsigned int at = 0; at < range.count; at++)
						sorted[range.start + at] -= range.baseVertex;

	const size_t index_bytes = sorted.size() * (g_index32 ? 4u : 2u);

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
		stats->indexWindow = index_window;
		stats->triangleWindow = triangle_window;
		stats->maxTextureSide = (int)(g_caps.MaxTextureWidth < g_caps.MaxTextureHeight
			? g_caps.MaxTextureWidth : g_caps.MaxTextureHeight);
		stats->waterMarks = g_mark_count;
		stats->markedVertices = marked_vertices;
		stats->markedChoppy = marked_choppy;
		stats->markedShimmer = marked_shimmer;
		stats->markedRoomShimmer = marked_room_shimmer;
		stats->splitRanges = split_ranges;
		stats->biggestRangeTris = biggest;
		stats->fallbackPageTris = fallback_tris;
		stats->scrollParts = g_scroll_parts;
		stats->cutoutParts = g_cutout_parts;	// A2
		stats->scrollRanges = 0;
		stats->scrollTris = 0;
		stats->swayVertices = sway_vertices;
		stats->swayParts = wind_parts;
		(void)wind_vertices;
		stats->swayDropped = sway_dropped;
		stats->flipbookParts = 0;

		for (size_t g = 0; g < g_groups.size(); g++)
		{
			stats->scrollRanges += g_groups[g].motion.scrollRanges;
			stats->scrollTris += g_groups[g].motion.scrollTris;
			stats->flipbookParts += g_groups[g].motion.flipbookParts;
		}
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
	g_wrapping = false;			// stage S5 switches this per range
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
	g_cutout_calls = 0;

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

		/* gPass: x is the ALPHA REFERENCE - a texel below it is thrown away
		 * and zero throws nothing away - y the additive weighting, z is the
		 * room shadow (1 until shadow maps exist), w the specular add - the
		 * classic path switches D3DRS_SPECULARENABLE off for its additive
		 * draw type (polyinsert.cpp:72) and the overbright half of the room
		 * colour goes off with it.
		 *
		 * The alpha pass's 0.5 is the classic path's own reference
		 * (function_table.cpp, D3DRS_ALPHAREF 128 over a 0..255 alpha); the
		 * opaque pass starts at zero and is raised per range below. */
		float constants[4] =
		{
			pass == 1 ? 0.5f : 0.0f,
			pass == 2 ? 1.0f : 0.0f,
			1.0f,
			pass == 2 ? 0.0f : 1.0f
		};

		g_device->SetVertexShaderConstantF(4, constants, 1);
		g_device->SetPixelShaderConstantF(4, constants, 1);

		/* A2: the opaque pass turns the alpha test on for the ranges whose
		 * .TRG MATERIAL is one of the three cut-out materials, and for no
		 * others - see MaterialAlphaRef at the top of this file for what
		 * says so and what the per-page answer of fix round A got wrong.
		 * The constant is written only when it changes, and the ranges are
		 * sorted by it, so a room pays for it once per run. */
		float alpha_ref = constants[0];
		const bool cutout_pass = pass == 0;

		g_motion_dirty = true;		// see the note beside g_motion_dirty

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

			if (cutout_pass)
			{
				if (range.alphaRef != alpha_ref)
				{
					alpha_ref = range.alphaRef;
					constants[0] = range.alphaRef;
					g_device->SetPixelShaderConstantF(4, constants, 1);
				}

				g_cutout_calls += range.alphaRef != 0.0f ? 1 : 0;
			}

			/* STAGE S5, THE SCROLL OF THIS DRAW CALL. The offset is taken
			 * modulo one page here rather than in the shader: a game running
			 * for an hour would otherwise hand a float a number in the
			 * thousands and spend its whole mantissa on the integer part,
			 * and the texture would visibly quantise. fmodf keeps it in
			 * (-1, 1), which is where a wrapped coordinate belongs.
			 *
			 * The address mode follows the scroll and nothing else: an HD
			 * page is one whole texture, not an atlas, so WRAP is safe, but
			 * a still surface whose UV sits exactly on 0 or 1 filters
			 * differently under the two modes - so the mode is switched only
			 * for the ranges that need it, and switched back. */
			const bool scroll = g_motion[3] != 0.0f &&
				(range.scrollU != 0.0f || range.scrollV != 0.0f);

			if (scroll != g_wrapping)
			{
				const DWORD mode = scroll ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;

				g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, mode);
				g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, mode);
				g_wrapping = scroll;
			}

			const float u = scroll ? fmodf(range.scrollU * g_motion[2], 1.0f) : 0.0f;
			const float v = scroll ? fmodf(range.scrollV * g_motion[2], 1.0f) : 0.0f;

			if (g_motion_dirty || u != g_motion[0] || v != g_motion[1])
			{
				g_motion[0] = u;
				g_motion[1] = v;
				g_motion_dirty = false;
				g_device->SetVertexShaderConstantF(13, g_motion, 1);
			}

			g_device->SetTexture(0, page);

			if (g_index32)
				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
					(UINT)g_vertex_count, range.start, range.count / 3);
			else
			{
				/* THE 16 BIT PATH (#123, stage S6). The indices of this range
				 * were written relative to its own lowest vertex, so the
				 * stream is moved there and the device is shown that window
				 * and no more - which is the whole point on a card that says
				 * it cannot address more than 65535 vertices. On a level
				 * small enough to fit in 16 bits anyway the base is simply
				 * the first vertex the range names and this costs one extra
				 * SetStreamSource per draw call. */
				g_device->SetStreamSource(0, g_vb,
					range.baseVertex * (UINT)sizeof(HDVertex), sizeof(HDVertex));
				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
					range.vertexSpan, range.start, range.count / 3);
			}

			g_draw_calls++;
		}

		if (g_wrapping)
		{
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			g_wrapping = false;
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

bool HDRenderGroupMotionInfo(int group, HDRenderGroupMotion* out)
{
	if (!out || group < 0 || group >= (int)g_groups.size())
		return false;

	*out = g_groups[(size_t)group].motion;
	return true;
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

// A2, for the log: of those, how many turned the opaque pass's alpha test on.
int HDRenderCutoutCalls()
{
	return g_cutout_calls;
}

int HDRenderDrawCalls()
{
	return g_draw_calls;
}

int HDRenderVertexBytes()
{
	return (int)sizeof(HDVertex);
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

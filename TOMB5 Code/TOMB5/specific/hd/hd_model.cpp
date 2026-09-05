/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the object path are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3, host\render\game_renderer.cpp) - see hd_model.h for the
 * list, function by function. The renderer under them is new. The .TRM format
 * itself is TRForge's measurement (MIT) and is documented in hd_trm.h.
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

#include "hd_model.h"
#include "hd_bind.h"
#include "hd_trm.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "hd_shader/hd_model_vs.inc"
#include "hd_shader/hd_model_ps.inc"

namespace
{
	/* THE VERTEX ON THE CARD IS THE VERTEX ON DISK - all 24 bytes of it, in
	 * the same order, with the same meaning. A .TRM vertex needs no repacking
	 * at all; it needs a declaration that reads its bytes right. The layout
	 * is written out in hd_trm.h and again in hd_model.hlsl.
	 *
	 * The only reason this struct exists is the stride and the compiler's
	 * assurance that it is 24. HDTrmVertex has exactly this shape, so the
	 * fill below is a memcpy in disguise - but written out, because
	 * HDTrmVertex belongs to the reader and its layout is the reader's
	 * business, not ours. */
	struct HDModelVertex
	{
		float x, y, z;
		unsigned char nx, ny, nz, layer;
		unsigned char bone0, bone1, bone2, u;
		unsigned char weight0, weight1, weight2, v;
	};

	// One draw call: a run of indices whose triangles all wear the same page.
	struct Range
	{
		unsigned int start = 0;
		unsigned int count = 0;
		int pageSlot = -1;
	};

	struct Model
	{
		IDirect3DVertexBuffer9* vb = 0;
		IDirect3DIndexBuffer9* ib = 0;
		std::vector<Range> pass[3];
		std::vector<int> pages;			// .DDS numbers, distinct

		std::string name;
		HDModelOrigin origin = HD_MODEL_ORIGIN_NONE;

		int vertices = 0;
		int indices = 0;
		int nodes = 0;				// classic nmeshes
		int nodeVertices[32] = { 0 };
		unsigned int covered = 0;		// bit per node with geometry

		float mins[3] = { 0, 0, 0 };
		float maxs[3] = { 0, 0, 0 };

		float bind[32][3] = { { 0, 0, 0 } };
		bool hasBind = false;

		int rigidNode = -1;			// >= 0 for a <NAME>_HEAD.TRM
	};

	const int kJointBudget = 32;

	IDirect3DDevice9* g_device = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	IDirect3DVertexDeclaration9* g_decl = 0;

	std::map<int, Model> g_models;		// slot -> body
	std::map<int, Model> g_heads;		// slot -> its separate HD face
	std::vector<std::string> g_notes;
	std::string g_install;

	bool g_in_frame = false;
	int g_draw_calls = 0;
	int g_frame_bulbs = 0;

	/* --------------------------------------------------------- helpers -- */

	std::string join(const std::string& a, const char* b)
	{
		std::string out = a;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		out += '\\';
		out += b;
		return out;
	}

	// The folder above the game 5 folder - the bundle that holds 4, 5 and 6.
	std::string parent_of(const std::string& path)
	{
		std::string out = path;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		const size_t cut = out.find_last_of("\\/");

		return cut == std::string::npos ? std::string(".") : out.substr(0, cut);
	}

	bool file_there(const std::string& path)
	{
		return HDTrmExists(path.c_str());
	}

	/* WHERE A MODEL LIVES. Game 5 first, then the siblings - and the siblings
	 * are not a courtesy: SAS, FIREROPE, BRIDGE_TILT1, RAISING_BLOCK1 and 2
	 * and EXPANDING_PLATFORM are in 4\ITEM and in no TR5 folder, because the
	 * remaster ships one model per NAME across the bundle and TR5 inherited
	 * those slots from TR4. Every substitution is written into the notes, so
	 * the log says which model came out of which game. */
	std::string find_model(const std::string& name, HDModelOrigin* origin)
	{
		const std::string bundle = parent_of(g_install);
		const std::string file = name + ".TRM";

		std::string path = join(join(g_install, "ITEM"), file.c_str());

		if (file_there(path))
		{
			if (origin)
				*origin = HD_MODEL_ORIGIN_5;
			return path;
		}

		static const char* siblings[2] = { "4", "6" };
		static const HDModelOrigin kinds[2] = { HD_MODEL_ORIGIN_4, HD_MODEL_ORIGIN_6 };

		for (int i = 0; i < 2; i++)
		{
			path = join(join(join(bundle, siblings[i]), "ITEM"), file.c_str());

			if (file_there(path))
			{
				if (origin)
					*origin = kinds[i];
				return path;
			}
		}

		if (origin)
			*origin = HD_MODEL_ORIGIN_NONE;

		return std::string();
	}

	void note(const char* format, ...)
	{
		char line[512];
		va_list args;

		va_start(args, format);
		std::vsnprintf(line, sizeof(line), format, args);
		va_end(args);

		g_notes.push_back(line);
	}

	double now_ms()
	{
		LARGE_INTEGER frequency, counter;

		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);

		return frequency.QuadPart
			? 1000.0 * (double)counter.QuadPart / (double)frequency.QuadPart
			: 0.0;
	}

	/* A shader constant register holds a COLUMN (fxc packs column major by
	 * default and the generated listing confirms it: dp4 o.x, position, c0),
	 * while a D3DMATRIX holds rows. The transpose goes to the device - the
	 * single most common way to get a black screen out of an otherwise
	 * correct Direct3D 9 shader path. */
	void set_matrix_constant(int reg, const float* rowMajor)
	{
		float columns[16];

		for (int row = 0; row < 4; row++)
			for (int col = 0; col < 4; col++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		g_device->SetVertexShaderConstantF((UINT)reg, columns, 4);
	}

	/* --------------------------------------------------------- building -- */

	void free_model(Model& model)
	{
		if (model.vb) { model.vb->Release(); model.vb = 0; }
		if (model.ib) { model.ib->Release(); model.ib = 0; }

		model.pass[0].clear();
		model.pass[1].clear();
		model.pass[2].clear();
		model.pages.clear();
	}

	/* The three index runs of one part, in pass order. Measured over all 566
	 * models of game 5: opaqueEnd == start + opaque and alphaEnd == opaqueEnd
	 * + alpha in every part of every file, so the three runs tile the part
	 * back to front without a gap. They are read as they stand all the same -
	 * the counts are what the remaster's own draw uses. */
	void part_runs(const HDTrmPart& part, unsigned int start[3], unsigned int count[3])
	{
		start[0] = part.start;
		count[0] = part.opaque;
		start[1] = part.opaqueEnd;
		count[1] = part.alpha;
		start[2] = part.alphaEnd;
		count[2] = part.additive;
	}

	/* Builds one model's buffers out of a .TRM. `rigidNode` >= 0 makes every
	 * vertex hang off that one node - a facial-rig head, whose own bone
	 * numbers belong to the rig and not to the mesh tree.
	 *
	 * The index list is REBUILT rather than uploaded as it stands: the
	 * triangles of a pass are bucketed by page, so a draw call is one page
	 * and one SetTexture. That is the same road hd_render takes for a room,
	 * and the reason is the same - Direct3D 9 has no texture arrays. */
	bool build(const HDTrmModel& trm, Model& model, int rigidNode,
		int* mixedTriangles, const char** why)
	{
		const int vertices = trm.vertices;

		std::vector<HDModelVertex> buffer((size_t)vertices);

		for (int i = 0; i < vertices; i++)
		{
			const HDTrmVertex& in = trm.vertex[i];
			HDModelVertex& out = buffer[(size_t)i];

			out.x = in.x;
			out.y = in.y;
			out.z = in.z;
			out.nx = in.nx;
			out.ny = in.ny;
			out.nz = in.nz;
			out.layer = in.layer;

			if (rigidNode >= 0)
			{
				out.bone0 = out.bone1 = out.bone2 = (unsigned char)rigidNode;
				out.weight0 = 255;
				out.weight1 = 0;
				out.weight2 = 0;
			}
			else
			{
				/* The palette is 32 and the measurement says the highest
				 * weighted bone over all of game 5 is 28, so this clamp never
				 * fires on the remaster's own files. It is here for a
				 * hand-made .TRM and for the day a sixth game ships. */
				out.bone0 = (unsigned char)(in.bone[0] < kJointBudget ? in.bone[0] : kJointBudget - 1);
				out.bone1 = (unsigned char)(in.bone[1] < kJointBudget ? in.bone[1] : kJointBudget - 1);
				out.bone2 = (unsigned char)(in.bone[2] < kJointBudget ? in.bone[2] : kJointBudget - 1);
				out.weight0 = in.weight[0];
				out.weight1 = in.weight[1];
				out.weight2 = in.weight[2];
			}

			out.u = in.u;
			out.v = in.v;

			if (i == 0)
			{
				model.mins[0] = model.maxs[0] = in.x;
				model.mins[1] = model.maxs[1] = in.y;
				model.mins[2] = model.maxs[2] = in.z;
			}
			else
			{
				if (in.x < model.mins[0]) model.mins[0] = in.x;
				if (in.y < model.mins[1]) model.mins[1] = in.y;
				if (in.z < model.mins[2]) model.mins[2] = in.z;
				if (in.x > model.maxs[0]) model.maxs[0] = in.x;
				if (in.y > model.maxs[1]) model.maxs[1] = in.y;
				if (in.z > model.maxs[2]) model.maxs[2] = in.z;
			}

			/* THE NODE COVERAGE, which is the whole of the mapping question:
			 * a bone index IS a classic mesh number, so what a slot needs is
			 * not a permutation but the list of nodes the model has geometry
			 * for. A weight of zero does not count - a vertex names three
			 * bones whatever it uses. */
			for (int b = 0; b < 3; b++)
			{
				const int node = rigidNode >= 0 ? rigidNode : (int)in.bone[b];
				const int weight = rigidNode >= 0 ? (b == 0 ? 255 : 0) : (int)in.weight[b];

				if (weight && node < kJointBudget)
				{
					model.nodeVertices[node]++;
					model.covered |= 1u << node;
				}
			}
		}

		/* The pages, in the order the model names them, and the page of every
		 * triangle. The layer is 1-based; layer 0 or a layer past the texture
		 * list would be a page nobody loaded, and over all 566 models neither
		 * ever happens. */
		std::vector<int> page_of_triangle;

		page_of_triangle.reserve((size_t)(trm.indices / 3));

		for (int i = 0; i + 2 < trm.indices; i += 3)
		{
			const int a = HDTrmPageOf(&trm, &trm.vertex[trm.index[i + 0]]);
			const int b = HDTrmPageOf(&trm, &trm.vertex[trm.index[i + 1]]);
			const int c = HDTrmPageOf(&trm, &trm.vertex[trm.index[i + 2]]);

			if (mixedTriangles && (a != b || b != c))
				(*mixedTriangles)++;

			page_of_triangle.push_back(a);

			if (a >= 0 && std::find(model.pages.begin(), model.pages.end(), a) == model.pages.end())
				model.pages.push_back(a);
		}

		/* The index list, bucketed by (pass, page). std::stable_sort keeps the
		 * order inside a page, so a model's triangles come off the card in
		 * the order the remaster wrote them - which matters for the additive
		 * pass, where the draw order is the blend order. */
		std::vector<unsigned short> indices;

		indices.reserve((size_t)trm.indices);

		for (int pass = 0; pass < 3; pass++)
		{
			std::vector<int> triangles;	// triangle numbers of this pass

			for (int p = 0; p < trm.parts; p++)
			{
				unsigned int start[3], count[3];

				part_runs(trm.part[p], start, count);

				for (unsigned int i = 0; i + 2 < count[pass]; i += 3)
				{
					const unsigned int at = start[pass] + i;

					if (at + 2 < (unsigned int)trm.indices)
						triangles.push_back((int)(at / 3));
				}
			}

			if (triangles.empty())
				continue;

			std::stable_sort(triangles.begin(), triangles.end(),
				[&page_of_triangle](int a, int b)
				{
					return page_of_triangle[(size_t)a] < page_of_triangle[(size_t)b];
				});

			Range range;
			int page = -2;

			for (size_t i = 0; i < triangles.size(); i++)
			{
				const int triangle = triangles[i];
				const int here = page_of_triangle[(size_t)triangle];

				if (here != page)
				{
					if (range.count)
						model.pass[pass].push_back(range);

					range.start = (unsigned int)indices.size();
					range.count = 0;
					range.pageSlot = here < 0 ? -1 : HDPageCacheSlotOf(here);
					page = here;
				}

				indices.push_back(trm.index[(size_t)triangle * 3 + 0]);
				indices.push_back(trm.index[(size_t)triangle * 3 + 1]);
				indices.push_back(trm.index[(size_t)triangle * 3 + 2]);
				range.count += 3;
			}

			if (range.count)
				model.pass[pass].push_back(range);
		}

		if (indices.empty())
		{
			*why = "has no triangle in any pass";
			return false;
		}

		model.vertices = vertices;
		model.indices = (int)indices.size();

		const UINT vertex_bytes = (UINT)(buffer.size() * sizeof(HDModelVertex));
		const UINT index_bytes = (UINT)(indices.size() * sizeof(unsigned short));

		/* D3DPOOL_MANAGED, unlike the room buffers. A model is small - the
		 * biggest of the 566 is a quarter of a megabyte - and there are
		 * dozens of them, so the system-memory copy that survives a device
		 * reset costs little and saves a reload of every model. */
		if (FAILED(g_device->CreateVertexBuffer(vertex_bytes, D3DUSAGE_WRITEONLY,
			0, D3DPOOL_MANAGED, &model.vb, 0)))
		{
			*why = "CreateVertexBuffer failed";
			return false;
		}

		if (FAILED(g_device->CreateIndexBuffer(index_bytes, D3DUSAGE_WRITEONLY,
			D3DFMT_INDEX16, D3DPOOL_MANAGED, &model.ib, 0)))
		{
			*why = "CreateIndexBuffer failed";
			return false;
		}

		void* target = 0;

		if (SUCCEEDED(model.vb->Lock(0, vertex_bytes, &target, 0)))
		{
			std::memcpy(target, &buffer[0], vertex_bytes);
			model.vb->Unlock();
		}

		if (SUCCEEDED(model.ib->Lock(0, index_bytes, &target, 0)))
		{
			std::memcpy(target, &indices[0], index_bytes);
			model.ib->Unlock();
		}

		return true;
	}

	/* WHICH NODE A SEPARATE HD FACE RIDES ON. The remaster ships some faces
	 * as their own file - VON_CROY.TRM is a body with an empty collar and its
	 * face is VON_CROY_HEAD.TRM - and draws them rigidly on the creature's
	 * head node. Which node that is, is measured and not hard coded: it is
	 * the bind-pose node nearest the face geometry's own centre (TROne,
	 * game_renderer.cpp:2056-2100 - node 21 in a 25 mesh tree, node 14 in a
	 * 15 mesh one). */
	int head_node_of(const HDTrmModel& head, const Model& body)
	{
		if (!body.hasBind || !head.vertices)
			return -1;

		double cx = 0, cy = 0, cz = 0;

		for (int i = 0; i < head.vertices; i++)
		{
			cx += head.vertex[i].x;
			cy += head.vertex[i].y;
			cz += head.vertex[i].z;
		}

		cx /= (double)head.vertices;
		cy /= (double)head.vertices;
		cz /= (double)head.vertices;

		int best = 0;
		double closest = 1e30;

		for (int node = 0; node < body.nodes && node < kJointBudget; node++)
		{
			const double dx = body.bind[node][0] - cx;
			const double dy = body.bind[node][1] - cy;
			const double dz = body.bind[node][2] - cz;
			const double d = dx * dx + dy * dy + dz * dz;

			if (d < closest)
			{
				closest = d;
				best = node;
			}
		}

		return best;
	}

	/* ---------------------------------------------------------- drawing -- */

	/* THE BIND FOLD. A .TRM vertex is in the model's bind pose, so the node's
	 * own bind offset has to come off before the node matrix goes on:
	 *
	 *     (p - bind) * M  =  p * R + t - bind * R  =  p * M'
	 *
	 * with M' the same 3x3 and t' = t - bind * R. TROne subtracts the offset
	 * in the shader with a second constant array of 32 vectors; folding it
	 * here costs three dot products per node per object and saves 32
	 * registers and three subtractions per VERTEX.
	 *
	 * Written straight into the three constant COLUMNS the shader reads. */
	void set_joint(int node, const float* rowMajor, const float* bind)
	{
		float columns[12];

		float tx = rowMajor[12];
		float ty = rowMajor[13];
		float tz = rowMajor[14];

		if (bind)
		{
			tx -= bind[0] * rowMajor[0] + bind[1] * rowMajor[4] + bind[2] * rowMajor[8];
			ty -= bind[0] * rowMajor[1] + bind[1] * rowMajor[5] + bind[2] * rowMajor[9];
			tz -= bind[0] * rowMajor[2] + bind[1] * rowMajor[6] + bind[2] * rowMajor[10];
		}

		for (int col = 0; col < 3; col++)
		{
			columns[col * 4 + 0] = rowMajor[0 * 4 + col];
			columns[col * 4 + 1] = rowMajor[1 * 4 + col];
			columns[col * 4 + 2] = rowMajor[2 * 4 + col];
		}

		columns[0 * 4 + 3] = tx;
		columns[1 * 4 + 3] = ty;
		columns[2 * 4 + 3] = tz;

		g_device->SetVertexShaderConstantF((UINT)(8 + node * 3), columns, 3);
	}

	void draw_model(const Model& model, const float* nodeMatrix, int nodeCount,
		const float* nodeLight, const HDModelLighting* lighting)
	{
		const int nodes = nodeCount < kJointBudget ? nodeCount : kJointBudget;

		/* ALL 32 JOINTS GO UP, not just the ones the caller has. A .TRM is
		 * the remaster's file and the classic node count is ours, and the two
		 * disagreeing by one would let a vertex read a joint left over from
		 * the object before - a limb somewhere else in the room, and only
		 * sometimes. The nodes past the caller's count get the last one it
		 * gave, which is TROne's rule for a node it never saw
		 * (fill_unseen_joints, game_renderer.cpp:1389): a slightly wrong limb
		 * beats a collapsed one. */
		for (int node = 0; node < kJointBudget; node++)
		{
			const int from = node < nodes ? node : nodes - 1;

			set_joint(node, nodeMatrix + from * 16,
				model.hasBind ? model.bind[from] : 0);
		}

		/* One colour per node, in the classic 0..255 space. A caller with
		 * nothing to say gets a flat 128, which is the classic path's own
		 * "no light list" ambient and draws the model at its plain texture
		 * brightness. */
		float light[kJointBudget * 4];

		for (int node = 0; node < kJointBudget; node++)
		{
			const int from = node < nodes ? node : nodes - 1;

			light[node * 4 + 0] = nodeLight ? nodeLight[from * 3 + 0] : 128.0f;
			light[node * 4 + 1] = nodeLight ? nodeLight[from * 3 + 1] : 128.0f;
			light[node * 4 + 2] = nodeLight ? nodeLight[from * 3 + 2] : 128.0f;
			light[node * 4 + 3] = 0.0f;
		}

		g_device->SetVertexShaderConstantF(104, light, (UINT)kJointBudget);

		/* c17: dynamic lights for this object, fog bulbs for this frame.
		 * c20: the lights themselves, in VIEW space - see hd_model.hlsl. */
		const int count = lighting && lighting->lights
			? (lighting->lightCount < HD_MAX_LIGHTS ? lighting->lightCount : HD_MAX_LIGHTS)
			: 0;

		float counts[4] = { (float)count, (float)g_frame_bulbs, 0, 0 };
		float lights[HD_MAX_LIGHTS * 2 * 4] = { 0 };

		for (int i = 0; i < count; i++)
		{
			const HDLightSource& source = lighting->lights[i];

			lights[i * 8 + 0] = source.x;
			lights[i * 8 + 1] = source.y;
			lights[i * 8 + 2] = source.z;
			lights[i * 8 + 3] = source.falloff;
			lights[i * 8 + 4] = source.r;
			lights[i * 8 + 5] = source.g;
			lights[i * 8 + 6] = source.b;
			lights[i * 8 + 7] = source.falloff > 0.0f ? 1.0f / source.falloff : 0.0f;
		}

		g_device->SetPixelShaderConstantF(17, counts, 1);
		g_device->SetPixelShaderConstantF(20, lights, HD_MAX_LIGHTS * 2);

		g_device->SetStreamSource(0, model.vb, 0, sizeof(HDModelVertex));
		g_device->SetIndices(model.ib);

		for (int pass = 0; pass < 3; pass++)
		{
			if (model.pass[pass].empty())
				continue;

			/* gPass: x switches the alpha test on, y the additive weighting,
			 * z is unused here (a room's baked pair needs it, a model has
			 * none), w the overbright add. The PIXEL constant file only -
			 * the vertex shader does not read gPass and fxc has put its own
			 * literals in that register there. */
			float constants[4] =
			{
				pass == 1 ? 1.0f : 0.0f,
				pass == 2 ? 1.0f : 0.0f,
				0.0f,
				pass == 2 ? 0.0f : 1.0f
			};

			g_device->SetPixelShaderConstantF(4, constants, 1);

			if (pass == 2)
			{
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
			}

			for (size_t i = 0; i < model.pass[pass].size(); i++)
			{
				const Range& range = model.pass[pass][i];

				g_device->SetTexture(0, range.pageSlot >= 0
					? (IDirect3DBaseTexture9*)HDPageCacheSlotTexture(range.pageSlot)
					: 0);

				g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
					0, (UINT)model.vertices, range.start, range.count / 3);

				g_draw_calls++;
			}

			if (pass == 2)
			{
				g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
				g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
			}
		}
	}

	const Model* find(const std::map<int, Model>& where, int slot)
	{
		const std::map<int, Model>::const_iterator at = where.find(slot);

		return at == where.end() ? 0 : &at->second;
	}
}

/* ------------------------------------------------------------- the module -- */

bool HDModelInit(IDirect3DDevice9* device, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	HDModelShutdown();

	if (!device)
	{
		*why = "no device";
		return false;
	}

	if (!HDRenderReady())
	{
		// The state bracket lives there - see hd_model.h.
		*why = "the HD room renderer is not up";
		return false;
	}

	g_device = device;

	static const D3DVERTEXELEMENT9 elements[] =
	{
		{ 0,  0, D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0 },
		{ 0, 12, D3DDECLTYPE_UBYTE4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0 },
		{ 0, 16, D3DDECLTYPE_UBYTE4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
		{ 0, 20, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0 },
		D3DDECL_END()
	};

	if (FAILED(device->CreateVertexDeclaration(elements, &g_decl)))
	{
		*why = "CreateVertexDeclaration failed";
		HDModelShutdown();
		return false;
	}

	if (FAILED(device->CreateVertexShader((const DWORD*)kHDModelVS, &g_vs)))
	{
		*why = "CreateVertexShader failed";
		HDModelShutdown();
		return false;
	}

	if (FAILED(device->CreatePixelShader((const DWORD*)kHDModelPS, &g_ps)))
	{
		*why = "CreatePixelShader failed";
		HDModelShutdown();
		return false;
	}

	return true;
}

void HDModelShutdown()
{
	HDModelLevelEnd();

	if (g_decl) { g_decl->Release(); g_decl = 0; }
	if (g_vs) { g_vs->Release(); g_vs = 0; }
	if (g_ps) { g_ps->Release(); g_ps = 0; }

	g_device = 0;
	g_in_frame = false;
}

bool HDModelReady()
{
	return g_device && g_vs && g_ps && g_decl;
}

bool HDModelLevelBegin(IDirect3DDevice9* device, const char* installRoot,
	const HDModelSlot* slots, int count, bool managedPages, int threads,
	HDModelStats* stats, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	HDModelLevelEnd();

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!HDModelReady() || !device)
	{
		*why = "the object path is not up";
		return false;
	}

	if (!installRoot || !*installRoot)
	{
		*why = "no install";
		return false;
	}

	g_device = device;
	g_install = installRoot;

	const double started = now_ms();
	double read_ms = 0;

	if (stats)
		stats->slots = count;

	/* PASS ONE: read every slot's .TRM and build what does not need the
	 * device. The pages are collected as we go, because they all have to be
	 * in the cache before a single range can name a page slot. */
	struct Pending
	{
		int slot;
		HDTrmModel trm;
		HDTrmModel head;
		bool hasHead;
		std::string name;
		HDModelOrigin origin;
	};

	std::vector<Pending> pending;
	std::vector<int> pages;

	for (int i = 0; i < count; i++)
	{
		const int slot = slots[i].slot;
		HDBindKind kind = HD_BIND_MODEL;
		bool fromMap = false;
		const char* model = slots[i].name;

		// A name the caller insists on is not asked about again - not even
		// for Lara, whose composite is stage S4 but whose parts a diagnostic
		// tool may well want to look at one at a time.
		if (!model)
			model = HDBindSlot(slot, &kind, &fromMap);

		if (kind == HD_BIND_LARA)
		{
			if (stats)
				stats->lara++;
			continue;
		}

		if (kind == HD_BIND_DUMMY)
		{
			if (stats)
				stats->dummy++;
			continue;
		}

		if (kind != HD_BIND_MODEL || !model)
		{
			if (stats)
				stats->noName++;
			note("slot %d has no name in this engine - classic", slot);
			continue;
		}

		HDModelOrigin origin = HD_MODEL_ORIGIN_NONE;
		const std::string path = find_model(model, &origin);

		if (path.empty())
		{
			if (stats)
				stats->missing++;
			note("%s (slot %d) asks for %s.TRM, which is in no ITEM folder - classic",
				HDSlotName(slot) ? HDSlotName(slot) : "?", slot, model);
			continue;
		}

		Pending here;

		here.slot = slot;
		here.hasHead = false;
		here.name = model;
		here.origin = origin;
		std::memset(&here.trm, 0, sizeof(here.trm));
		std::memset(&here.head, 0, sizeof(here.head));

		const double read_started = now_ms();
		const char* trouble = "";
		const bool read = HDTrmLoad(path.c_str(), &here.trm, &trouble);

		read_ms += now_ms() - read_started;

		if (!read)
		{
			if (stats)
				stats->refused++;
			note("%s (slot %d): %s.TRM %s - classic",
				HDSlotName(slot) ? HDSlotName(slot) : "?", slot, model, trouble);
			continue;
		}

		if (origin != HD_MODEL_ORIGIN_5)
			note("%s (slot %d): %s.TRM comes from the sibling game %d, not from 5",
				HDSlotName(slot) ? HDSlotName(slot) : "?", slot, model,
				origin == HD_MODEL_ORIGIN_4 ? 4 : 6);

		/* The separate HD face, if the remaster ships one for this model. */
		HDModelOrigin head_origin = HD_MODEL_ORIGIN_NONE;
		const std::string head_path = find_model(std::string(model) + "_HEAD", &head_origin);

		if (!head_path.empty())
		{
			const char* head_trouble = "";
			const double head_started = now_ms();

			here.hasHead = HDTrmLoad(head_path.c_str(), &here.head, &head_trouble);
			read_ms += now_ms() - head_started;

			if (!here.hasHead)
				note("%s (slot %d): %s_HEAD.TRM %s - the body draws without it",
					HDSlotName(slot) ? HDSlotName(slot) : "?", slot, model, head_trouble);
		}

		for (int t = 0; t < here.trm.textures; t++)
			pages.push_back((int)here.trm.texture[t]);

		if (here.hasHead)
			for (int t = 0; t < here.head.textures; t++)
				pages.push_back((int)here.head.texture[t]);

		pending.push_back(here);
	}

	/* The pages, ADDED to what the rooms already loaded: a page a room and a
	 * model both wear keeps one texture and one slot number. */
	std::sort(pages.begin(), pages.end());
	pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

	if (!pages.empty())
	{
		HDPageCacheStats page_stats;
		const char* page_why = "";

		if (!HDPageCacheAppend(device, installRoot, pages.empty() ? 0 : &pages[0],
			(int)pages.size(), managedPages, threads, &page_stats, &page_why))
		{
			note("the model pages could not be loaded: %s", page_why);
		}
		else if (stats)
		{
			stats->pageStats = page_stats;
			stats->pages = page_stats.slots;
		}
	}

	for (int i = 0; i < HDPageCacheNoteCount(); i++)
		note("%s", HDPageCacheNote(i));

	/* PASS TWO: the buffers. */
	int mixed = 0;

	for (size_t i = 0; i < pending.size(); i++)
	{
		Pending& here = pending[i];
		Model model;

		model.name = here.name;
		model.origin = here.origin;
		model.nodes = 0;

		for (int k = 0; k < count; k++)
			if (slots[k].slot == here.slot)
			{
				model.nodes = slots[k].nodes;

				if (slots[k].bind)
				{
					const int use = model.nodes < kJointBudget ? model.nodes : kJointBudget;

					for (int node = 0; node < use; node++)
					{
						model.bind[node][0] = slots[k].bind[node * 3 + 0];
						model.bind[node][1] = slots[k].bind[node * 3 + 1];
						model.bind[node][2] = slots[k].bind[node * 3 + 2];
					}

					model.hasBind = true;
				}
				break;
			}

		const char* trouble = "";

		if (!build(here.trm, model, -1, &mixed, &trouble))
		{
			if (stats)
				stats->refused++;
			note("%s (slot %d): %s", HDSlotName(here.slot) ? HDSlotName(here.slot) : "?",
				here.slot, trouble);
			free_model(model);
			HDTrmFree(&here.trm);
			if (here.hasHead)
				HDTrmFree(&here.head);
			continue;
		}

		g_models[here.slot] = model;

		if (stats)
		{
			stats->loaded++;
			stats->vertices += model.vertices;
			stats->indices += model.indices;
			stats->ranges += (int)(model.pass[0].size() + model.pass[1].size() +
				model.pass[2].size());
			stats->vertexBytes += (double)model.vertices * sizeof(HDModelVertex);
			stats->indexBytes += (double)model.indices * 2.0;

			if (here.origin == HD_MODEL_ORIGIN_4)
				stats->fromGame4++;
			else if (here.origin == HD_MODEL_ORIGIN_6)
				stats->fromGame6++;
			else
				stats->fromGame5++;
		}

		HDTrmFree(&here.trm);
	}

	if (stats)
		stats->mixedPageTriangles = mixed;

	/* PASS THREE: the separate faces, rigidly on ONE node - measured, not
	 * hard coded: the bind-pose node nearest the face geometry's own centre.
	 * Without a bind pose there is nothing to measure against and the face
	 * rides on node 0, which is what the diagnostic tool sees when it is
	 * pointed at a bare .TRM. */
	for (size_t i = 0; i < pending.size(); i++)
	{
		Pending& here = pending[i];

		if (!here.hasHead)
			continue;

		if (!g_models.count(here.slot))
		{
			HDTrmFree(&here.head);
			continue;
		}

		const Model& body = g_models[here.slot];
		Model head;

		head.name = here.name + "_HEAD";
		head.origin = here.origin;
		head.nodes = body.nodes;
		head.hasBind = body.hasBind;
		std::memcpy(head.bind, body.bind, sizeof(head.bind));

		const int node = head_node_of(here.head, body);

		head.rigidNode = node < 0 ? 0 : node;

		const char* trouble = "";
		int ignore = 0;

		if (!build(here.head, head, head.rigidNode, &ignore, &trouble))
		{
			note("%s_HEAD (slot %d): %s", here.name.c_str(), here.slot, trouble);
			free_model(head);
			HDTrmFree(&here.head);
			continue;
		}

		g_heads[here.slot] = head;

		note("%s (slot %d): the HD face %s rides on node %d of %d",
			HDSlotName(here.slot) ? HDSlotName(here.slot) : "?", here.slot,
			head.name.c_str(), head.rigidNode, head.nodes);

		if (stats)
		{
			stats->heads++;
			stats->vertices += head.vertices;
			stats->indices += head.indices;
			stats->ranges += (int)(head.pass[0].size() + head.pass[1].size() +
				head.pass[2].size());
			stats->vertexBytes += (double)head.vertices * sizeof(HDModelVertex);
			stats->indexBytes += (double)head.indices * 2.0;
		}

		HDTrmFree(&here.head);
	}

	/* The coverage count. A slot whose model has geometry for every classic
	 * node is FULL; one with a gap is PARTIAL, and stage S3-2 draws the gap
	 * the classic way. */
	if (stats)
	{
		for (std::map<int, Model>::const_iterator at = g_models.begin();
			at != g_models.end(); ++at)
		{
			const Model& model = at->second;
			int covered = 0;

			for (int node = 0; node < model.nodes && node < kJointBudget; node++)
				if (model.covered & (1u << node))
					covered++;

			stats->nodesTotal += model.nodes;
			stats->nodesCovered += covered;

			if (covered >= model.nodes)
				stats->fullSlots++;
			else
				stats->partialSlots++;
		}

		stats->nodesFallback = stats->nodesTotal - stats->nodesCovered;
		stats->readMs = read_ms;
		stats->buildMs = now_ms() - started - read_ms;
	}

	return true;
}

void HDModelLevelEnd()
{
	for (std::map<int, Model>::iterator at = g_models.begin(); at != g_models.end(); ++at)
		free_model(at->second);

	for (std::map<int, Model>::iterator at = g_heads.begin(); at != g_heads.end(); ++at)
		free_model(at->second);

	g_models.clear();
	g_heads.clear();
	g_notes.clear();
}

void HDModelFrameBegin(const D3DMATRIX* projection, int bulbCount)
{
	if (!HDModelReady())
		return;

	g_in_frame = true;
	g_draw_calls = 0;
	g_frame_bulbs = bulbCount < HD_MAX_BULBS ? bulbCount : HD_MAX_BULBS;

	if (projection)
		set_matrix_constant(0, (const float*)projection);
}

bool HDModelDraw(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, const HDModelLighting* lighting)
{
	if (!g_in_frame || !nodeMatrix || nodeCount <= 0)
		return false;

	const Model* model = find(g_models, slot);

	if (!model || !model->vb)
		return false;

	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);

	draw_model(*model, nodeMatrix, nodeCount, nodeLight, lighting);
	return true;
}

bool HDModelDrawHead(int slot, const float* nodeMatrix, int nodeCount,
	const float* nodeLight, const HDModelLighting* lighting)
{
	if (!g_in_frame || !nodeMatrix || nodeCount <= 0)
		return false;

	std::map<int, Model>::iterator at = g_heads.find(slot);

	if (at == g_heads.end() || !at->second.vb)
		return false;

	g_device->SetVertexShader(g_vs);
	g_device->SetPixelShader(g_ps);
	g_device->SetVertexDeclaration(g_decl);

	draw_model(at->second, nodeMatrix, nodeCount, nodeLight, lighting);
	return true;
}

int HDModelDrawCalls()
{
	return g_draw_calls;
}

/* ------------------------------------------------------------ what is up -- */

bool HDModelHas(int slot)
{
	return find(g_models, slot) != 0;
}

const char* HDModelName(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->name.c_str() : 0;
}

HDModelOrigin HDModelOriginOf(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->origin : HD_MODEL_ORIGIN_NONE;
}

int HDModelNodes(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->nodes : 0;
}

int HDModelNodeVertices(int slot, int node)
{
	const Model* model = find(g_models, slot);

	if (!model || node < 0 || node >= kJointBudget)
		return 0;

	return model->nodeVertices[node];
}

bool HDModelNodeCovered(int slot, int node)
{
	const Model* model = find(g_models, slot);

	if (!model || node < 0 || node >= kJointBudget)
		return false;

	return (model->covered & (1u << node)) != 0;
}

int HDModelVertices(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->vertices : 0;
}

int HDModelIndices(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? model->indices : 0;
}

int HDModelRanges(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? (int)(model->pass[0].size() + model->pass[1].size() +
		model->pass[2].size()) : 0;
}

int HDModelPageCount(int slot)
{
	const Model* model = find(g_models, slot);

	return model ? (int)model->pages.size() : 0;
}

int HDModelPage(int slot, int at)
{
	const Model* model = find(g_models, slot);

	if (!model || at < 0 || at >= (int)model->pages.size())
		return -1;

	return model->pages[(size_t)at];
}

bool HDModelHasHead(int slot)
{
	return find(g_heads, slot) != 0;
}

int HDModelHeadNode(int slot)
{
	const Model* head = find(g_heads, slot);

	return head ? head->rigidNode : -1;
}

namespace
{
	bool bounds_of(const Model* model, float* mins, float* maxs)
	{
		if (!model)
			return false;

		if (mins)
			std::memcpy(mins, model->mins, sizeof(model->mins));
		if (maxs)
			std::memcpy(maxs, model->maxs, sizeof(model->maxs));

		return true;
	}
}

bool HDModelHeadBounds(int slot, float* mins, float* maxs)
{
	return bounds_of(find(g_heads, slot), mins, maxs);
}

bool HDModelBounds(int slot, float* mins, float* maxs)
{
	const Model* model = find(g_models, slot);

	if (!model)
		return false;

	if (mins)
		std::memcpy(mins, model->mins, sizeof(model->mins));
	if (maxs)
		std::memcpy(maxs, model->maxs, sizeof(model->maxs));

	return true;
}

int HDModelNoteCount()
{
	return (int)g_notes.size();
}

const char* HDModelNote(int at)
{
	return (at >= 0 && at < (int)g_notes.size()) ? g_notes[(size_t)at].c_str() : "";
}

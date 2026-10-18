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

/* The pieces of a broken static in HD (hd_debris_rules.h has the rule).
 *
 * The classic break stays the engine's own: ShatterObject and TriggerDebris
 * fill the debris array, UpdateDebris flies it, DrawDebris draws it. This
 * keeps a side table beside that array - per debris slot the triangles of
 * the static's HD art the piece carries - and draws those triangles with
 * the room's own shaders on the slot's matrix. A slot with no HD look is
 * drawn the classic way, as before.
 *
 * Engine-free: the engine hands it the level and the frame (hd_level.cpp)
 * and the break (debris.cpp, ShatterObject); RoomProbe -debris hands it the
 * same out of the files. No Log here: the callers write the lines out of
 * HDDebrisBreakInfo and HDDebrisLevelInfo.
 *
 * The definitions stand at the end of this header, behind HD_DEBRIS_BODY:
 * one file of each program defines it before the include (hd_level.cpp in
 * the engine, RoomProbe.cpp in the tool) and so holds them, every other
 * includer sees the declarations alone. A header and not a source file of
 * its own, so that the engine's project and the tool's build script stay
 * as they are. */

#include <d3d9.h>

#include "hd_debris_rules.h"

struct HDFrameLighting;

/* The level. Reads every destructible part (material bit 15) of
 * <installRoot>\DATA\<stem>.TRG: the file's head, the parts, and only the
 * index runs and vertices those parts name, by seeking - not the tens of
 * megabytes the room build reads. Returns the parts read; 0 for a level
 * that has none or no file. Free gives the memory back and forgets every
 * slot's look; SlotsForget forgets the looks alone, for a level whose HD
 * data a load keeps (hd_levelkey.h) and whose debris array starts empty
 * again. None of the three touches the device. */
int HDDebrisLevelRead(const char* installRoot, const char* stem);
void HDDebrisLevelFree();
void HDDebrisSlotsForget();

struct HDDebrisLevelStats
{
	int parts;		// destructible parts read
	int triangles;		// their triangles, all three runs
	double ms;		// the read's own time
	char stem[64];		// the level the last read was asked for
};

void HDDebrisLevelInfo(HDDebrisLevelStats* out);

/* One destructible part a broken static is drawn by: its group (the room
 * whose art holds it), its material and that room's origin in world units,
 * the art being room local. */
struct HDDebrisPart
{
	int group;
	unsigned int material;
	float originX, originZ;
};

/* The break. Begin takes the static's parts and its classic faces in the
 * order ShatterObject walks them (the triangles, then the quads), deals
 * every triangle of the parts to a face (HDDebrisFaceOf), and stays open
 * until End; Note gives debris slot `slot` the look of face `face` (-1 or
 * a closed break: none), the piece born at x, y, z. */
bool HDDebrisBreakBegin(const HDDebrisPart* parts, int partCount,
	const HDDebrisFace* faces, int faceCount);

/* Begin with the faces of the breaking mesh itself: its triangles `gt3`
 * and quads `gt4` at the static's place x, y, z, out of the `vertices`
 * vertices the break turned (HDDebrisMeshFace) - the call ShatterObject
 * and RoomProbe -debris both make. */
bool HDDebrisBreakMesh(const HDDebrisPart* parts, int partCount,
	const short* gt3, int ngt3, const short* gt4, int ngt4,
	const short* rotated, int vertices, long x, long y, long z);
void HDDebrisBreakEnd();
void HDDebrisNote(int slot, int face, long x, long y, long z);

struct HDDebrisBreakStats
{
	int parts;		// parts the break found
	int faces;		// classic faces handed in
	int triangles;		// triangles of those parts
	int places;		// faces at a place of their own
	int facesWithArt;	// faces that took a triangle
	int pieces;		// slots noted with a face
	int piecesWithArt;	// of them, slots that carry a triangle
	int trianglesFlying;	// triangles the noted slots carry
	double ms;		// Begin's time
};

void HDDebrisBreakInfo(HDDebrisBreakStats* out);

/* The frame. FrameBegin forgets the last frame's list; Drawn is asked by
 * DrawDebris for every live slot with the slot's own matrix (the classic
 * float matrix, M00 .. M23, view space) and answers true when HD draws it,
 * noting the matrix; Draw draws the noted slots, phase 0 the solid and the
 * cut-out triangles, phase 1 the glass and the glows, with the projection
 * and the frame's lighting the room draw was given. Draw is called last in
 * a bracket of HDRenderFrameBegin; it puts back every device state it
 * sets but the shader constants, which the next bracket writes again. */
void HDDebrisFrameBegin();
bool HDDebrisDrawn(int slot, const float* view);
void HDDebrisDraw(IDirect3DDevice9* device, const D3DMATRIX* projection,
	const HDFrameLighting* frame, int phase);

struct HDDebrisFrameStats
{
	int slots;		// slots noted this frame
	int triangles;		// triangles drawn
	int calls;		// draw calls, both phases
};

void HDDebrisFrameInfo(HDDebrisFrameStats* out);

// Lets the shaders and the declaration go, with the device.
void HDDebrisRelease();

/* The glass's hue in the room draw, defined with it in hd_render.cpp: the
 * level's classic faces the classic adds, handed once the room build is
 * done, matched against the places of the art's glass (HDGlassAtPlace)
 * and turned into each draw call's tint; returns the draw calls that took
 * a hue. Info answers for the `at`-th draw call that may be drawn as
 * glass, false past the last. */
int HDRenderGlassTint(const HDGlassFace* faces, int count);

struct HDGlassTintInfo
{
	int group;		// the room group
	int pass;		// 1 the alpha pass, 2 the additive one
	int page;		// its page
	int triangles;
	float blend;		// the alpha pass's see-through weight
	bool split;		// drawn as a frame and its glass (GlassSplits)
	bool whole;		// added whole (AddGlass)
	int faces;		// the classic faces at its place
	float area;		// their area
	float colour[3];	// their added colour, by area
	float tint[4];		// the hue it takes, w 1 where it takes one
};

bool HDRenderGlassTintInfo(int at, HDGlassTintInfo* out);

#ifdef HD_DEBRIS_BODY

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#include "hd_pagecache.h"
#include "hd_render.h"

/* The room's own shaders, compiled once more into this file: the pieces are
 * drawn by RoomVS and RoomPS, so their light, their fog and their blend are
 * the room's lines and not a copy of them that could drift. */
#include "hd_shader/hd_room_vs.inc"
#include "hd_shader/hd_room_ps.inc"

/* The definitions' own names, in a namespace of their own so that they meet
 * nothing of the file that holds them; each function below opens it. */
namespace hd_debris_body
{
	/* One triangle of a destructible part, as the room's vertex buffer
	 * carries it (hd_render.cpp, ComposeVertex): the position room local,
	 * the byte normal with the layer, and the two light triples with u and
	 * v in the alpha bytes, red read from the file's first byte. */
	struct ArtTri
	{
		short p[3][3];
		unsigned int normal[3];
		unsigned int c0[3];
		unsigned int c1[3];
		int page;
		int kind;
	};

	struct Art
	{
		int group;
		unsigned int material;
		float param[4];
		std::vector<ArtTri> tris;
	};

	std::vector<Art> g_art;
	HDDebrisLevelStats g_level_stats;

	// The open break: per classic face the triangles it took.
	struct Ref
	{
		int art;
		int tri;
	};

	std::vector<std::vector<Ref> > g_face_tris;
	std::vector<float> g_art_origin;	// per art: x, z, or none
	bool g_break_open = false;
	HDDebrisBreakStats g_break_stats;

	// A triangle a piece carries, offset from the piece's birth place.
	struct FlyTri
	{
		float p[3][3];
		unsigned int normal[3];
		unsigned int c0[3];
		unsigned int c1[3];
		int page;
		int kind;
		float see;
	};

	struct Slot
	{
		bool hd;
		bool listed;
		float view[12];
		std::vector<FlyTri> tris;
	};

	const int kSlots = 256;
	Slot g_slots[kSlots];
	int g_list[kSlots];
	int g_listed = 0;
	HDDebrisFrameStats g_frame_stats;

	IDirect3DDevice9* g_dev = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_ps = 0;
	IDirect3DVertexDeclaration9* g_decl = 0;

	/* The vertex the room shader reads, with the position a float already
	 * in view space: the slot's matrix is applied here, so that every piece
	 * of a frame goes in one draw call per page and kind. w is the wind
	 * weight, 0; the flags are the room's (no water mark, then the 1). */
	struct Vertex
	{
		float x, y, z, w;
		DWORD normal;
		DWORD c0;
		DWORD c1;
		DWORD flags;
	};

	double Now()
	{
		LARGE_INTEGER c, f;

		QueryPerformanceCounter(&c);
		QueryPerformanceFrequency(&f);
		return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
	}

	bool ReadAt(FILE* f, long long at, void* out, size_t bytes)
	{
		return _fseeki64(f, at, SEEK_SET) == 0 &&
			fread(out, 1, bytes, f) == bytes;
	}

	unsigned int U32(const unsigned char* p)
	{
		return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
			((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
	}

	struct PartHead
	{
		int group;
		unsigned int material;
		float param[4];
		unsigned int start[3], count[3];
	};

	/* The .TRG's layout (vendor\trforge, Trg.h): 'TRG', the version, the
	 * sector count and 24 bytes a sector, the sun (24), the volume counts
	 * (4) and the fog and box volumes (40 and 28 each), the groups (a part
	 * count, 44 bytes a part, a flags word, a light count and 28 bytes a
	 * light), the page list padded to four, then the index and the vertex
	 * counts, the indices (4 bytes) and the vertices (20). Only the parts
	 * with bit 15 are kept; their runs and vertices are read where they
	 * lie. */
	bool ReadTrg(FILE* f, std::vector<Art>& out)
	{
		unsigned char head[8];

		if (!ReadAt(f, 0, head, 8) || memcmp(head, "TRG", 3) || head[3] != 3)
			return false;

		long long at = 8 + (long long)U32(head + 4) * 24 + 24;
		unsigned char counts[4];

		if (!ReadAt(f, at, counts, 4))
			return false;

		at += 4 + (long long)counts[2] * 40 + (long long)counts[3] * 28;

		unsigned char word[4];

		if (!ReadAt(f, at, word, 4))
			return false;

		const int groups = (int)U32(word);
		std::vector<PartHead> heads;
		std::vector<unsigned char> block;

		at += 4;

		for (int g = 0; g < groups; g++)
		{
			if (!ReadAt(f, at, word, 4))
				return false;

			const unsigned int parts = U32(word);

			if (parts > 65536)
				return false;

			block.resize((size_t)parts * 44 + 8);

			if (!ReadAt(f, at + 4, block.data(), block.size()))
				return false;

			for (unsigned int k = 0; k < parts; k++)
			{
				const unsigned char* p = block.data() + k * 44;
				PartHead h;

				h.material = U32(p);

				if (!(h.material & 0x8000u))
					continue;

				h.group = g;

				for (int i = 0; i < 4; i++)
					memcpy(&h.param[i], p + 4 + i * 4, 4);

				for (int i = 0; i < 3; i++)
				{
					h.start[i] = U32(p + 20 + i * 8);
					h.count[i] = U32(p + 24 + i * 8);
				}

				heads.push_back(h);
			}

			const unsigned int lights = U32(block.data() + parts * 44 + 4);

			at += 4 + (long long)parts * 44 + 8 + (long long)lights * 28;
		}

		if (!ReadAt(f, at, word, 4))
			return false;

		const unsigned int pageCount = (unsigned int)word[0] |
			((unsigned int)word[1] << 8);
		std::vector<unsigned short> pages(pageCount ? pageCount : 1, 0);

		if (pageCount && !ReadAt(f, at + 4, pages.data(), pageCount * 2))
			return false;

		at += 4 + (long long)pageCount * 2;
		at = (at + 3) & ~3LL;

		unsigned char sizes[8];

		if (!ReadAt(f, at, sizes, 8))
			return false;

		const unsigned int indexCount = U32(sizes);
		const unsigned int vertexCount = U32(sizes + 4);
		const long long indexAt = at + 8;
		const long long vertexAt = indexAt + (long long)indexCount * 4;

		for (const PartHead& h : heads)
		{
			Art art;

			art.group = h.group;
			art.material = h.material;
			memcpy(art.param, h.param, sizeof(art.param));

			for (int pass = 0; pass < 3; pass++)
			{
				const unsigned int n = h.count[pass] - h.count[pass] % 3;

				if (!n || h.start[pass] + n > indexCount)
					continue;

				std::vector<unsigned int> index(n);

				if (!ReadAt(f, indexAt + (long long)h.start[pass] * 4,
					index.data(), (size_t)n * 4))
					return false;

				// The vertices the run names, one read over their span.
				unsigned int lo = 0xFFFFFFFFu, hi = 0;

				for (unsigned int v : index)
				{
					lo = v < lo ? v : lo;
					hi = v > hi ? v : hi;
				}

				if (hi >= vertexCount)
					return false;

				std::vector<unsigned char> verts((size_t)(hi - lo + 1) * 20);

				if (!ReadAt(f, vertexAt + (long long)lo * 20, verts.data(),
					verts.size()))
					return false;

				const int kind = HDDebrisKindOf(h.material, pass);

				for (unsigned int t = 0; t < n; t += 3)
				{
					ArtTri tri;

					for (int k = 0; k < 3; k++)
					{
						const unsigned char* v =
							verts.data() + (size_t)(index[t + k] - lo) * 20;

						for (int a = 0; a < 3; a++)
							memcpy(&tri.p[k][a], v + a * 2, 2);

						tri.normal[k] = (unsigned int)v[8] |
							((unsigned int)v[9] << 8) |
							((unsigned int)v[10] << 16) |
							((unsigned int)v[11] << 24);
						tri.c0[k] = ((unsigned int)v[15] << 24) |
							((unsigned int)v[12] << 16) |
							((unsigned int)v[13] << 8) | v[14];
						tri.c1[k] = ((unsigned int)v[19] << 24) |
							((unsigned int)v[16] << 16) |
							((unsigned int)v[17] << 8) | v[18];
					}

					// The page: the first corner's layer, counted from one.
					const unsigned int layer = tri.normal[0] >> 24;

					tri.page = layer >= 1 && layer <= pageCount ?
						(int)pages[layer - 1] : -1;
					tri.kind = kind;
					art.tris.push_back(tri);
				}
			}

			out.push_back(art);
		}

		return true;
	}

	bool Ready(IDirect3DDevice9* device)
	{
		if (g_dev == device && g_vs && g_ps && g_decl)
			return true;

		HDDebrisRelease();

		static const D3DVERTEXELEMENT9 elements[] =
		{
			{ 0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT,
				D3DDECLUSAGE_POSITION, 0 },
			{ 0, 16, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT,
				D3DDECLUSAGE_NORMAL,   0 },
			{ 0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT,
				D3DDECLUSAGE_COLOR,    0 },
			{ 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT,
				D3DDECLUSAGE_COLOR,    1 },
			{ 0, 28, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT,
				D3DDECLUSAGE_TEXCOORD, 0 },
			D3DDECL_END()
		};

		if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomVS,
				&g_vs)) ||
			FAILED(device->CreatePixelShader((const DWORD*)kHDRoomPS, &g_ps)) ||
			FAILED(device->CreateVertexDeclaration(elements, &g_decl)))
		{
			HDDebrisRelease();
			return false;
		}

		g_dev = device;
		return true;
	}

	void Transposed(int reg, const float* rowMajor, int rows,
		IDirect3DDevice9* device)
	{
		float columns[16];

		for (int col = 0; col < rows; col++)
			for (int row = 0; row < 4; row++)
				columns[col * 4 + row] = rowMajor[row * 4 + col];

		device->SetVertexShaderConstantF((UINT)reg, columns, (UINT)rows);
	}

	void ForgetSlots()
	{
		for (int i = 0; i < kSlots; i++)
		{
			g_slots[i].hd = false;
			g_slots[i].listed = false;
			std::vector<FlyTri>().swap(g_slots[i].tris);
		}

		g_listed = 0;
	}
}

int HDDebrisLevelRead(const char* installRoot, const char* stem)
{
	using namespace hd_debris_body;

	const double t0 = Now();

	HDDebrisLevelFree();

	if (stem)
		strncpy_s(g_level_stats.stem, stem, _TRUNCATE);

	if (!installRoot || !stem || !*installRoot || !*stem)
		return 0;

	const std::string path = std::string(installRoot) + "\\DATA\\" + stem +
		".TRG";
	FILE* f = 0;

	if (fopen_s(&f, path.c_str(), "rb") || !f)
		return 0;

	std::vector<Art> read;

	if (ReadTrg(f, read))
		g_art.swap(read);

	fclose(f);

	g_level_stats.parts = (int)g_art.size();

	for (const Art& a : g_art)
		g_level_stats.triangles += (int)a.tris.size();

	g_level_stats.ms = Now() - t0;
	return (int)g_art.size();
}

void HDDebrisLevelFree()
{
	using namespace hd_debris_body;

	HDDebrisBreakEnd();
	std::vector<Art>().swap(g_art);
	memset(&g_level_stats, 0, sizeof(g_level_stats));
	ForgetSlots();
}

void HDDebrisSlotsForget()
{
	using namespace hd_debris_body;

	HDDebrisBreakEnd();
	ForgetSlots();
}

void HDDebrisLevelInfo(HDDebrisLevelStats* out)
{
	using namespace hd_debris_body;

	if (out)
		*out = g_level_stats;
}

bool HDDebrisBreakBegin(const HDDebrisPart* parts, int partCount,
	const HDDebrisFace* faces, int faceCount)
{
	using namespace hd_debris_body;

	const double t0 = Now();

	HDDebrisBreakEnd();
	memset(&g_break_stats, 0, sizeof(g_break_stats));
	g_break_stats.faces = faceCount;

	if (!parts || partCount <= 0 || !faces || faceCount <= 0)
		return false;

	std::vector<float> boxes((size_t)faceCount * 6);
	std::vector<int> place((size_t)faceCount);

	for (int i = 0; i < faceCount; i++)
		HDDebrisFaceBox(faces[i], &boxes[(size_t)i * 6]);

	HDDebrisPlaces(faces, faceCount, place.data());

	for (int i = 0; i < faceCount; i++)
		g_break_stats.places += place[(size_t)i] == i ? 1 : 0;

	g_face_tris.assign((size_t)faceCount, std::vector<Ref>());
	g_art_origin.assign(g_art.size() * 2, 0.0f);

	for (int k = 0; k < partCount; k++)
		for (size_t a = 0; a < g_art.size(); a++)
		{
			const Art& art = g_art[a];

			if (art.group != parts[k].group ||
				art.material != parts[k].material)
				continue;

			g_break_stats.parts++;
			g_art_origin[a * 2] = parts[k].originX;
			g_art_origin[a * 2 + 1] = parts[k].originZ;

			for (size_t t = 0; t < art.tris.size(); t++)
			{
				const ArtTri& tri = art.tris[t];
				float c[3];

				for (int i = 0; i < 3; i++)
					c[i] = ((float)tri.p[0][i] + (float)tri.p[1][i] +
						(float)tri.p[2][i]) / 3.0f;

				c[0] += parts[k].originX;
				c[2] += parts[k].originZ;

				const int face = HDDebrisFaceOf(faces, faceCount, c,
					(unsigned int)t, boxes.data(), place.data());

				if (face < 0)
					continue;

				Ref ref;

				ref.art = (int)a;
				ref.tri = (int)t;
				g_face_tris[(size_t)face].push_back(ref);
				g_break_stats.triangles++;
			}
		}

	for (int i = 0; i < faceCount; i++)
		g_break_stats.facesWithArt += g_face_tris[(size_t)i].empty() ? 0 : 1;

	g_break_open = g_break_stats.parts > 0;
	g_break_stats.ms = Now() - t0;
	return g_break_open;
}

bool HDDebrisBreakMesh(const HDDebrisPart* parts, int partCount,
	const short* gt3, int ngt3, const short* gt4, int ngt4,
	const short* rotated, int vertices, long x, long y, long z)
{
	const int count = ngt3 + ngt4;

	if (!rotated || count <= 0 || (ngt3 > 0 && !gt3) || (ngt4 > 0 && !gt4))
	{
		HDDebrisBreakBegin(parts, partCount, 0, 0);
		return false;
	}

	std::vector<HDDebrisFace> faces((size_t)count);

	for (int f = 0; f < count; f++)
		HDDebrisMeshFace(gt3, ngt3, gt4, f, rotated, vertices, x, y, z,
			&faces[(size_t)f]);

	return HDDebrisBreakBegin(parts, partCount, faces.data(), count);
}

void HDDebrisBreakEnd()
{
	using namespace hd_debris_body;

	g_break_open = false;
	g_face_tris.clear();
}

void HDDebrisNote(int slot, int face, long x, long y, long z)
{
	using namespace hd_debris_body;

	if (slot < 0 || slot >= kSlots)
		return;

	Slot& s = g_slots[slot];

	s.tris.clear();
	s.hd = g_break_open && face >= 0 && face < (int)g_face_tris.size();

	if (!s.hd)
		return;

	g_break_stats.pieces++;

	for (const Ref& ref : g_face_tris[(size_t)face])
	{
		const Art& art = g_art[(size_t)ref.art];
		const ArtTri& tri = art.tris[(size_t)ref.tri];
		FlyTri fly;

		for (int k = 0; k < 3; k++)
		{
			fly.p[k][0] = (float)tri.p[k][0] +
				g_art_origin[(size_t)ref.art * 2] - (float)x;
			fly.p[k][1] = (float)tri.p[k][1] - (float)y;
			fly.p[k][2] = (float)tri.p[k][2] +
				g_art_origin[(size_t)ref.art * 2 + 1] - (float)z;
			fly.normal[k] = tri.normal[k];
			fly.c0[k] = tri.c0[k];
			fly.c1[k] = tri.c1[k];
		}

		fly.page = tri.page;
		fly.kind = tri.kind;
		fly.see = HDDebrisSeeThrough(tri.kind, art.param);
		s.tris.push_back(fly);
	}

	g_break_stats.trianglesFlying += (int)s.tris.size();
	g_break_stats.piecesWithArt += s.tris.empty() ? 0 : 1;
}

void HDDebrisBreakInfo(HDDebrisBreakStats* out)
{
	using namespace hd_debris_body;

	if (out)
		*out = g_break_stats;
}

void HDDebrisFrameBegin()
{
	using namespace hd_debris_body;

	for (int i = 0; i < g_listed; i++)
		g_slots[g_list[i]].listed = false;

	g_listed = 0;
	memset(&g_frame_stats, 0, sizeof(g_frame_stats));
}

bool HDDebrisDrawn(int slot, const float* view)
{
	using namespace hd_debris_body;

	if (slot < 0 || slot >= kSlots || !g_slots[slot].hd || !view)
		return false;

	Slot& s = g_slots[slot];

	memcpy(s.view, view, sizeof(s.view));

	if (!s.listed)
	{
		s.listed = true;
		g_list[g_listed++] = slot;
		g_frame_stats.slots++;
	}

	return true;
}

void HDDebrisDraw(IDirect3DDevice9* device, const D3DMATRIX* projection,
	const HDFrameLighting* frame, int phase)
{
	using namespace hd_debris_body;

	if (!g_listed || !device || !projection || !Ready(device))
		return;

	// The kinds this phase draws, in this order.
	const int kinds[2][2] =
	{
		{ HD_DEBRIS_SOLID, HD_DEBRIS_CUTOUT },
		{ HD_DEBRIS_GLASS, HD_DEBRIS_GLOW }
	};
	const int* drawn = kinds[phase ? 1 : 0];

	// One list of vertices per kind and page, in view space.
	struct Bucket
	{
		int kind;
		int page;
		float see;
		std::vector<Vertex> v;
	};

	std::vector<Bucket> buckets;

	for (int i = 0; i < g_listed; i++)
	{
		const Slot& s = g_slots[g_list[i]];
		const float* m = s.view;

		for (const FlyTri& t : s.tris)
		{
			if (t.kind != drawn[0] && t.kind != drawn[1])
				continue;

			Bucket* b = 0;

			for (Bucket& o : buckets)
				if (o.kind == t.kind && o.page == t.page && o.see == t.see)
				{
					b = &o;
					break;
				}

			if (!b)
			{
				buckets.push_back(Bucket());
				b = &buckets.back();
				b->kind = t.kind;
				b->page = t.page;
				b->see = t.see;
			}

			for (int k = 0; k < 3; k++)
			{
				const float* p = t.p[k];
				Vertex v;

				v.x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
				v.y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
				v.z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
				v.w = 0.0f;
				v.normal = t.normal[k];
				v.c0 = t.c0[k];
				v.c1 = t.c1[k];
				v.flags = 0x01000000u;
				b->v.push_back(v);
			}
		}
	}

	if (buckets.empty())
		return;

	// What this draw sets, to be put back after it.
	IDirect3DVertexShader9* keptVs = 0;
	IDirect3DPixelShader9* keptPs = 0;
	IDirect3DVertexDeclaration9* keptDecl = 0;
	IDirect3DBaseTexture9* keptTex = 0;
	IDirect3DVertexBuffer9* keptVb = 0;
	UINT keptOffset = 0, keptStride = 0;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_CULLMODE,
		D3DRS_ALPHATESTENABLE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	DWORD kept[sizeof(states) / sizeof(states[0])];

	device->GetVertexShader(&keptVs);
	device->GetPixelShader(&keptPs);
	device->GetVertexDeclaration(&keptDecl);
	device->GetTexture(0, &keptTex);
	device->GetStreamSource(0, &keptVb, &keptOffset, &keptStride);

	for (int i = 0; i < stateCount; i++)
		device->GetRenderState(states[i], &kept[i]);

	device->SetVertexShader(g_vs);
	device->SetPixelShader(g_ps);
	device->SetVertexDeclaration(g_decl);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

	/* The vertex side: the projection alone (the view is in the vertices),
	 * the identity as the view, and every switch of RoomVS off - no water,
	 * no wind, no shimmer, no flat light - but the frame's screen, which
	 * the wibble under the water reads. Registers the shader defines
	 * itself (c4, c8, c17, c19 and others) are not written. */
	const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		0, 0, 0, 1 };
	const float zero[4] = { 0, 0, 0, 0 };
	float screen[4] = { 1, 1, 0, 0 };
	float motion[4] = { 0, 0, 0, 0 };

	if (frame)
	{
		screen[0] = frame->centerX != 0 ? frame->centerX : 1.0f;
		screen[1] = frame->centerY != 0 ? frame->centerY : 1.0f;
		screen[2] = frame->wibble;
		screen[3] = frame->cameraUnderwater ? 1.0f : 0.0f;
		motion[2] = frame->clock;
	}

	Transposed(0, (const float*)projection, 4, device);
	Transposed(5, identity, 3, device);
	device->SetVertexShaderConstantF(9, screen, 1);

	for (int reg = 10; reg <= 16; reg++)
		device->SetVertexShaderConstantF((UINT)reg,
			reg == 13 ? motion : zero, 1);

	device->SetVertexShaderConstantF(18, zero, 1);

	/* The pixel side: the frame's fog and fog bulbs, no dynamic light, no
	 * ripple, no gloss; the pass and the see-through weight per bucket. */
	float fog[4] = { 1.0e30f, 0, 0, 0 };
	float counts[4] = { 0, 0, 0, 0 };
	float bulbs[HD_MAX_BULBS * 2 * 4] = { 0 };
	float add[4] = { 1, 0, 0, 0 };

	if (frame)
	{
		if (frame->distanceFogStart > 0)
		{
			fog[0] = frame->distanceFogStart;
			fog[1] = 255.0f / frame->distanceFogStart;
		}

		int n = frame->bulbs ? frame->bulbCount : 0;

		n = n > HD_MAX_BULBS ? HD_MAX_BULBS : n;

		for (int j = 0; j < n; j++)
		{
			const HDFogBulb& b = frame->bulbs[j];

			bulbs[j * 8 + 0] = b.x;
			bulbs[j * 8 + 1] = b.y;
			bulbs[j * 8 + 2] = b.z;
			bulbs[j * 8 + 3] = b.rad;
			bulbs[j * 8 + 4] = b.r;
			bulbs[j * 8 + 5] = b.g;
			bulbs[j * 8 + 6] = b.b;
			bulbs[j * 8 + 7] = b.density;
		}

		counts[1] = (float)n;
		add[1] = frame->clock;
	}

	device->SetPixelShaderConstantF(10, zero, 1);
	device->SetPixelShaderConstantF(16, fog, 1);
	device->SetPixelShaderConstantF(17, counts, 1);
	device->SetPixelShaderConstantF(18, add, 1);
	device->SetPixelShaderConstantF(36, zero, 1);
	device->SetPixelShaderConstantF(40, bulbs, HD_MAX_BULBS * 2);

	for (int pick = 0; pick < 2; pick++)
		for (const Bucket& b : buckets)
		{
			if (b.kind != drawn[pick])
				continue;

			IDirect3DTexture9* page = b.page >= 0 ?
				HDPageCacheTexture(b.page) : 0;

			if (!page)
				continue;

			const bool glow = b.kind == HD_DEBRIS_GLOW;
			const bool glass = b.kind == HD_DEBRIS_GLASS;
			const float pass[4] =
			{
				b.kind == HD_DEBRIS_CUTOUT ? 0.5f : 0.0f,
				glow ? 1.0f : 0.0f, 0.0f, glow ? 0.0f : 1.0f
			};
			const float spec[4] = { 0, 0, 0, b.see };

			device->SetPixelShaderConstantF(4, pass, 1);
			device->SetPixelShaderConstantF(19, spec, 1);
			device->SetRenderState(D3DRS_ZWRITEENABLE,
				glow || glass ? FALSE : TRUE);
			device->SetRenderState(D3DRS_ALPHABLENDENABLE,
				glow || glass ? TRUE : FALSE);
			device->SetRenderState(D3DRS_SRCBLEND,
				glow ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
			device->SetRenderState(D3DRS_DESTBLEND,
				glow ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
			device->SetTexture(0, page);
			device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
				(UINT)(b.v.size() / 3), b.v.data(), sizeof(Vertex));
			g_frame_stats.triangles += (int)(b.v.size() / 3);
			g_frame_stats.calls++;
		}

	device->SetVertexShader(keptVs);
	device->SetPixelShader(keptPs);
	device->SetVertexDeclaration(keptDecl);
	device->SetTexture(0, keptTex);
	device->SetStreamSource(0, keptVb, keptOffset, keptStride);

	for (int i = 0; i < stateCount; i++)
		device->SetRenderState(states[i], kept[i]);

	if (keptVs)
		keptVs->Release();

	if (keptPs)
		keptPs->Release();

	if (keptDecl)
		keptDecl->Release();

	if (keptTex)
		keptTex->Release();

	if (keptVb)
		keptVb->Release();
}

void HDDebrisFrameInfo(HDDebrisFrameStats* out)
{
	using namespace hd_debris_body;

	if (out)
		*out = g_frame_stats;
}

void HDDebrisRelease()
{
	using namespace hd_debris_body;

	if (g_vs)
		g_vs->Release();

	if (g_ps)
		g_ps->Release();

	if (g_decl)
		g_decl->Release();

	g_vs = 0;
	g_ps = 0;
	g_decl = 0;
	g_dev = 0;
}

#endif	// HD_DEBRIS_BODY

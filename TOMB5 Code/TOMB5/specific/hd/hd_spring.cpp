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

/* The springs (hd_spring.h holds their rules): a level's springs built on
 * its water bodies, their rings handed to the sheets once a frame, their
 * cracks laid on the floor the level's .TRG draws and drawn before the
 * sheets, and the column and the bubbles drawn after the sheets with the
 * water's cards.
 * The first part reads the level only through hd_render.h and the .TRG
 * and is linked by RoomProbe as well (compiled there with HD_SPRING_TOOL);
 * the last part is the engine's: the level's begin and end, the frame's
 * three calls and the device's loss. With tomb5.hd_water 0 no sheet is
 * built and nothing of this runs; with tomb5.hd_motion 0 the bubbles hold
 * where they are, the column stands and the sheets draw no ring. */
#ifndef HD_SPRING_TOOL
#include "../../tomb5/pch.h"
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#endif

#include "hd_spring.h"
#include "hd_render.h"
#include "hd_pagecache.h"		// HDPageCacheTexture - the floor's page

#include "hd_shader/hd_room_card_vs.inc"
#include "hd_shader/hd_spring_column_ps.inc"
#include "hd_shader/hd_spring_bubble_ps.inc"
#include "hd_shader/hd_spring_crack_ps.inc"
#include "hd_shader/hd_spring_hole_ps.inc"

#include <algorithm>
#include <stdio.h>
#include <string>

namespace
{
	std::vector<HDSpring> g_springs;
	// Per spring: the clock up to which its rings went to the sheets.
	std::vector<double> g_fed;
	// The clock the bubbles stand at while tomb5.hd_motion is off.
	double g_held = 0.0;
	// The level's counts for its SPRINGLOG line.
	char g_stem[16] = "";
	int g_rows = 0;
	int g_dropped = 0;
	/* The rule's counts: the bodies it saw, those neither river nor taken
	 * with kHDSpringSquares candidates or more (fit), the candidate squares
	 * of all bodies, and the key's value; one line per body for RoomProbe. */
	int g_bodies = 0;
	int g_fit = 0;
	int g_candidates = 0;
	int g_thin = 0;
	std::vector<std::string> g_rule_lines;

	IDirect3DDevice9* g_made_on = 0;
	IDirect3DVertexShader9* g_vs = 0;
	IDirect3DPixelShader9* g_column_ps = 0;
	IDirect3DPixelShader9* g_bubble_ps = 0;
	IDirect3DPixelShader9* g_crack_ps = 0;
	IDirect3DPixelShader9* g_hole_ps = 0;
	IDirect3DTexture9* g_noise = 0;

	/* A spring's crack as its build left it (HDSpringCrackBuild): why it
	 * draws or not, its outline, and per outline point the floor's height
	 * and light under it; per edge - point i to the next - the floor's map
	 * of world x, z to its page (u = m0 + m1 x + m2 z, v = m3 + m4 x + m5 z,
	 * page units) and the page. */
	enum
	{
		CRACK_DRAWN,
		CRACK_OFF,		// the key hd_springcrack at 0
		CRACK_FIT,		// no size of it lies on deep water
		CRACK_FILE,		// the level's .TRG not read
		CRACK_FLOOR,		// a point with no floor under it
		CRACK_ROUGH		// its floor's heights too far apart
	};

	struct Crack
	{
		int state;
		HDSpringCrackShape shape;
		float rimY[kHDSpringCrackMax];
		float light[kHDSpringCrackMax][3];
		float map[kHDSpringCrackMax][6];
		int page[kHDSpringCrackMax];
		// Per edge: the slit's width across from it (slit_gap).
		float gap[kHDSpringCrackMax];
		float middleY;		// the floor at the main line's middle
		int room;		// the room whose floor it opens
	};

	/* The cracks, one for each spring in g_springs' order, and what the
	 * level's build was told: the key hd_springcrack. */
	std::vector<Crack> g_cracks;
	int g_crack_key = 1;
	int g_drawn_cracks = 0;
	double g_crack_ms = -1.0;

	// RoomProbe's timing of the draw, and what the last draw drew.
	bool g_timing = false;
	double g_gpu_ms = -1.0;
	int g_drawn_columns = 0;
	int g_drawn_bubbles = 0;
	// RoomProbe -springparts: 1 the columns, 2 the bubbles, 3 both.
	int g_parts = 3;

	/* How far a quad fades into what stands behind it, units: the
	 * cards' 64 halved for a bubble, a free choice; the column's soft
	 * edge where it meets the floor or the sheet behind it the same. */
	const float kSoft = 32.0f;
	/* The ratio's floor (hd_spring.hlsl, ColumnPS): a dark texel moved
	 * next to a black one would make a ratio of thousands; at 0.02 of
	 * white the bend of a dark floor stays a bend. And the pattern's cell,
	 * 24 units across the jet's line and 96 along it: two to ten cells
	 * across the column, streaks that rise (hd_spring.h, the rising
	 * current). The column's share of the moved picture is whole from half
	 * its weight up. Free choices, set by eye on RoomProbe pictures. */
	const float kRatioFloor = 0.02f;
	const float kCellAcross = 24.0f;
	const float kCellAlong = 96.0f;
	const float kShareFrom = 2.0f;	// one over the weight
	/* The bubbles' share over the water's own, and how far the rings
	 * go back when a spring first rings or the clock jumps: past the
	 * rings' 3.5 s life (hd_render.cpp, kRingLife), so a picture at a
	 * moment shows every ring alive then. */
	const float kBubbleShare = 0.9f;
	const double kRingBack = 4.0;

	// A card's corner, as hd_render.cpp's CardVertex: CardVS reads it.
	struct Vertex
	{
		float x, y, z;
		float u, v, cx, cz;
		float r, g, b, a;
		float light;
	};

	const DWORD kFVF = D3DFVF_XYZ | D3DFVF_TEX3 | D3DFVF_TEXCOORDSIZE4(0) |
		D3DFVF_TEXCOORDSIZE4(1) | D3DFVF_TEXCOORDSIZE1(2);

	void release_shaders()
	{
		if (g_vs) { g_vs->Release(); g_vs = 0; }
		if (g_column_ps) { g_column_ps->Release(); g_column_ps = 0; }
		if (g_bubble_ps) { g_bubble_ps->Release(); g_bubble_ps = 0; }
		if (g_crack_ps) { g_crack_ps->Release(); g_crack_ps = 0; }
		if (g_hole_ps) { g_hole_ps->Release(); g_hole_ps = 0; }
		if (g_noise) { g_noise->Release(); g_noise = 0; }
		g_made_on = 0;
	}

	// The shaders on the frame's device, made again when the device is new.
	bool shaders(IDirect3DDevice9* device)
	{
		if (device != g_made_on)
		{
			release_shaders();

			if (FAILED(device->CreateVertexShader((const DWORD*)kHDRoomCardVS,
				&g_vs)))
				g_vs = 0;

			if (FAILED(device->CreatePixelShader(
				(const DWORD*)kHDSpringColumnPS, &g_column_ps)))
				g_column_ps = 0;

			if (FAILED(device->CreatePixelShader(
				(const DWORD*)kHDSpringBubblePS, &g_bubble_ps)))
				g_bubble_ps = 0;

			if (FAILED(device->CreatePixelShader(
				(const DWORD*)kHDSpringCrackPS, &g_crack_ps)))
				g_crack_ps = 0;

			if (FAILED(device->CreatePixelShader(
				(const DWORD*)kHDSpringHolePS, &g_hole_ps)))
				g_hole_ps = 0;

			/* The column's noise table, managed like the classic pages: it
			 * survives a reset, and only a released device takes it. */
			const int n = kHDSpringNoiseSide;
			D3DLOCKED_RECT r;

			if (FAILED(device->CreateTexture(n, n, 1, 0, D3DFMT_A8R8G8B8,
				D3DPOOL_MANAGED, &g_noise, 0)))
				g_noise = 0;
			else if (SUCCEEDED(g_noise->LockRect(0, &r, 0, 0)))
			{
				for (int y = 0; y < n; y++)
				{
					DWORD* row = (DWORD*)((char*)r.pBits + y * r.Pitch);

					for (int x = 0; x < n; x++)
						row[x] = 0xFF000000u |
							((DWORD)HDSpringNoiseTexel(x, y, 0) << 16) |
							((DWORD)HDSpringNoiseTexel(x, y, 1) << 8);
				}

				g_noise->UnlockRect(0);
			}
			else
			{
				g_noise->Release();
				g_noise = 0;
			}

			g_made_on = device;
		}

		return g_vs && g_column_ps && g_bubble_ps && g_noise;
	}

	// Row vectors: a point through a 4x4 that keeps w at one.
	void through(const float* m, const float* p, float* out)
	{
		for (int k = 0; k < 3; k++)
			out[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] +
				m[12 + k];
	}

	// a times b, row vectors.
	void multiply(const float* a, const float* b, float* out)
	{
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] +
					a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
	}

	// The world point a row-vector world-to-view matrix sends to the eye.
	void eye_of(const float* m, float* eye)
	{
		const float a = m[0], b = m[1], c = m[2];
		const float d = m[4], e = m[5], f = m[6];
		const float g = m[8], h = m[9], i = m[10];
		const float det = a * (e * i - f * h) - b * (d * i - f * g) +
			c * (d * h - e * g);

		if (fabsf(det) < 1.0e-12f)
		{
			eye[0] = eye[1] = eye[2] = 0.0f;
			return;
		}

		const float inv[9] =
		{
			(e * i - f * h) / det, (c * h - b * i) / det,
			(b * f - c * e) / det, (f * g - d * i) / det,
			(a * i - c * g) / det, (c * d - a * f) / det,
			(d * h - e * g) / det, (b * g - a * h) / det,
			(a * e - b * d) / det
		};

		for (int j = 0; j < 3; j++)
			eye[j] = -(m[12] * inv[j] + m[13] * inv[3 + j] +
				m[14] * inv[6 + j]);
	}

	/* The square that holds world x, z and draws a sheet: its body,
	 * surface and depth. Where sheets of two heights lie over the place -
	 * ANDREA2's fountain over its sewer - the one in `room` wins, else the
	 * highest. */
	bool square_under(float x, float z, int room, int* body, int* surface,
		int* depth)
	{
		std::vector<HDRenderWaterSquare> squares;
		bool found = false;
		bool own = false;

		for (int b = 0; b < HDRenderWaterBodies(); b++)
		{
			const int n = HDRenderWaterBodySquares(b, 0, 0, 0);

			if (n <= 0)
				continue;

			squares.resize((size_t)n);
			HDRenderWaterBodySquares(b, &squares[0], n, 0);

			for (const HDRenderWaterSquare& q : squares)
			{
				if (!q.sheet || x < (float)q.x || x >= (float)(q.x + 1024) ||
					z < (float)q.z || z >= (float)(q.z + 1024))
					continue;

				const bool mine = room >= 0 && q.room == room;

				if (found && (own || (!mine && q.y >= *surface)))
					continue;

				*body = b;
				*surface = q.y;
				*depth = q.depth;
				found = true;
				own = mine;
			}
		}

		return found;
	}

	/* A spring onto its water: its body, the sheet over it and the floor
	 * under it (a table row's own floor where it gives one, else the
	 * square's). A ripple row of hd_water.txt whose source lies within a
	 * quarter of a square of the spring rings that place already: the
	 * spring then rings no more, so one place does not ring twice. False
	 * where no sheet lies over the place. */
	bool place(HDSpring& s, std::vector<int>& marked)
	{
		int body = -1, surface = 0, depth = 0;

		if (!square_under(s.x, s.z, s.room, &body, &surface, &depth))
			return false;

		s.body = body;
		s.surfaceY = surface;

		if (s.floorY <= surface)
			s.floorY = surface + depth;

		HDRenderWaterBody info;

		s.rings = 1;

		if (HDRenderWaterBodyInfo(body, &info) && info.spring &&
			std::find(marked.begin(), marked.end(), body) == marked.end())
		{
			const float dx = info.springAt[0] - s.x;
			const float dz = info.springAt[1] - s.z;

			s.rings = dx * dx + dz * dz <= 256.0f * 256.0f ? 0 : 1;
		}

		if (!info.spring)
		{
			HDRenderWaterBodySpring(body, s.x, s.z);
			marked.push_back(body);
		}

		return true;
	}

	// Spring i's crack while it has one that draws, else 0.
	const HDSpringCrackShape* crack_of(size_t i)
	{
		return i < g_cracks.size() && g_cracks[i].state == CRACK_DRAWN ?
			&g_cracks[i].shape : 0;
	}
}

void HDSpringClear()
{
	g_springs.clear();
	g_fed.clear();
	g_cracks.clear();
	g_crack_key = 1;
	g_held = 0.0;
	g_stem[0] = 0;
	g_rows = 0;
	g_dropped = 0;
	g_bodies = g_fit = g_candidates = g_thin = 0;
	g_rule_lines.clear();
}

/* The level's springs: the table's rows for this level, then the `spring`
 * rows of hd_water.txt (HDSpringFileApply), each laid onto its water; a
 * spring whose place has no sheet is left out and counted. Then the rule
 * (hd_spring.h, HDSpringRulePick) on every body none of them is in: its
 * squares, their candidates against `blocks`, one spring at the middle of
 * the candidate the hash picks, on the floor under it. */
void HDSpringBuild(const char* stem, const std::vector<HDSpringBlock>& blocks,
	int thin)
{
	HDSpringClear();

	if (!stem)
		return;

	strncpy(g_stem, stem, sizeof(g_stem) - 1);
	g_stem[sizeof(g_stem) - 1] = 0;

	std::vector<HDSpring> springs;

	for (const HDSpringRow& r : kHDSprings)
	{
		if (_stricmp(r.stem, stem))
			continue;

		HDSpring s = {};

		strncpy(s.stem, stem, sizeof(s.stem) - 1);
		s.room = r.room;
		s.x = r.x;
		s.z = r.z;
		s.floorY = r.floorY;
		s.strength = r.strength;
		s.period = r.period;
		s.body = -1;
		s.how = HD_SPRING_TABLE;
		s.seed = HDSpringSeed(stem, s.x, s.z);
		springs.push_back(s);
	}

	g_rows = HDRenderWaterSpringRows(0, 0);

	std::vector<HDRenderSpringRow> rows((size_t)g_rows);

	if (g_rows > 0)
		HDRenderWaterSpringRows(&rows[0], g_rows);

	for (const HDRenderSpringRow& r : rows)
		HDSpringFileApply(springs, stem, r.room, r.key);

	std::vector<int> marked;

	for (HDSpring& s : springs)
		if (place(s, marked))
			g_springs.push_back(s);
		else
			g_dropped++;

	g_thin = thin;

	std::vector<HDRenderWaterSquare> squares;

	for (int b = 0; b < HDRenderWaterBodies(); b++)
	{
		HDRenderWaterBody info;
		const int n = HDRenderWaterBodySquares(b, 0, 0, 0);

		if (n <= 0 || !HDRenderWaterBodyInfo(b, &info))
			continue;

		squares.resize((size_t)n);
		HDRenderWaterBodySquares(b, &squares[0], n, 0);

		HDSpringBody body;

		body.index = b;
		body.surfaceY = info.surfaceY;
		body.river = info.cls == HD_WATER_RIVER || info.speed > 0.0f ||
			info.sinks > 0;
		body.taken = 0;

		for (const HDSpring& s : g_springs)
			body.taken |= s.body == b ? 1 : 0;

		for (const HDRenderWaterSquare& q : squares)
		{
			const HDSpringSquare sq = { q.x, q.z, q.depth, q.sheet };

			body.squares.push_back(sq);
		}

		std::vector<int> cand;
		const int c = HDSpringCandidates(body, blocks, &cand);
		const unsigned key = HDSpringBodyKey(body);
		int pick = -1;
		float strength = 0.0f;
		const bool chosen = HDSpringRulePick(stem, key, c, body.river,
			body.taken, thin, &pick, &strength);

		g_bodies++;
		g_candidates += c;
		g_fit += !body.river && !body.taken && c >= kHDSpringSquares ? 1 : 0;

		char line[160];

		snprintf(line, sizeof(line), "SPRINGRULE level=%s body=%d squares=%d "
			"candidates=%d river=%d taken=%d first=%u hash=%u picked=%d", stem,
			b, n, c, body.river, body.taken, key,
			HDSpringHash(HDSpringStemHash(stem), key), chosen ? pick : -1);
		g_rule_lines.push_back(line);

		if (!chosen)
			continue;

		const HDRenderWaterSquare& q = squares[(size_t)cand[(size_t)pick]];
		HDSpring s = {};

		strncpy(s.stem, stem, sizeof(s.stem) - 1);
		s.room = q.room;
		s.x = (float)q.x + 512.0f;
		s.z = (float)q.z + 512.0f;
		s.floorY = q.y + q.depth;
		s.strength = strength;
		s.period = HDSpringRulePeriod(strength);
		s.body = -1;
		s.how = HD_SPRING_RULE;
		s.seed = HDSpringSeed(stem, s.x, s.z);

		if (place(s, marked))
			g_springs.push_back(s);
		else
			g_dropped++;
	}

	g_fed.assign(g_springs.size(), -1.0e30);
}

// RoomProbe -springs: what the rule saw in each body (`at` 0 up).
bool HDSpringRuleLine(int at, char* out, size_t size)
{
	if (at < 0 || at >= (int)g_rule_lines.size())
		return false;

	snprintf(out, size, "%s", g_rule_lines[(size_t)at].c_str());
	return true;
}

/* SPRINGLOG, with -log, at the level's build: one line for the level and
 * one for each spring (`at` -1 and 0 up; false past the last).
 *
 *   SPRINGLOG level=<stem> springs=<n> table=<n> file=<n> rows=<n>
 *             dropped=<n> rule=<n> bodies=<n> fit=<n> candidates=<n>
 *             key=<n>
 *   SPRINGLOG level=<stem> spring=<i> room=<r> x=<x> z=<z> floor=<y>
 *             surface=<y> strength=<s> period=<seconds> body=<b>
 *             how=table|file|rule rings=own|row crack=<crack>
 *
 * springs the level's springs and how many came from each source, rows the
 * file's `spring` rows of the level, dropped the springs left out for want
 * of a sheet over them, the rule's springs, the bodies it saw, those that
 * could hold one (fit), their candidate squares and the key hd_springs as
 * the level took it; per spring the room its row names (the rule's: its
 * square's),
 * the place, the floor it rises from and the sheet over it (world y), its
 * strength and ring period, its water body, where it came from and whether
 * it rings the sheet itself or a ripple row rings its place; its crack as
 * length,width,depth,branches,bottom y,page or why it has none: off (the
 * key hd_springcrack at 0), fit (no size lies on deep water), file (the
 * .TRG not read), floor (a point with no floor under it), rough (the
 * floor's heights too far apart), none (no crack built). */
bool HDSpringLogLine(int at, char* out, size_t size)
{
	static const char* kHow[3] = { "table", "file", "rule" };

	if (at < 0)
	{
		int n[3] = { 0, 0, 0 };

		for (const HDSpring& s : g_springs)
			n[s.how]++;

		snprintf(out, size, "SPRINGLOG level=%s springs=%d table=%d file=%d "
			"rows=%d dropped=%d rule=%d bodies=%d fit=%d candidates=%d key=%d",
			g_stem, (int)g_springs.size(), n[0], n[1], g_rows, g_dropped, n[2],
			g_bodies, g_fit, g_candidates, g_thin);
		return true;
	}

	if (at >= (int)g_springs.size())
		return false;

	const HDSpring& s = g_springs[(size_t)at];
	static const char* kWhy[6] = { "drawn", "off", "fit", "file", "floor",
		"rough" };
	char crack[96];

	if ((size_t)at >= g_cracks.size())
		snprintf(crack, sizeof(crack), "none");
	else if (g_cracks[(size_t)at].state != CRACK_DRAWN)
		snprintf(crack, sizeof(crack), "%s",
			kWhy[g_cracks[(size_t)at].state]);
	else
	{
		const HDSpringCrackShape& c = g_cracks[(size_t)at].shape;

		snprintf(crack, sizeof(crack), "%.0f,%.0f,%.0f,%d,%.0f,%d",
			c.length, c.width, c.depth, c.branches, c.bottomY,
			g_cracks[(size_t)at].page[0]);
	}

	snprintf(out, size, "SPRINGLOG level=%s spring=%d room=%d x=%.0f z=%.0f "
		"floor=%d surface=%d strength=%.2f period=%.2f body=%d how=%s "
		"rings=%s crack=%s", g_stem, at, s.room, s.x, s.z, s.floorY,
		s.surfaceY, s.strength, s.period, s.body, kHow[s.how],
		s.rings ? "own" : "row", crack);
	return true;
}

// RoomProbe -spring: one more spring at world x, z, laid as the others.
bool HDSpringAdd(const char* stem, float x, float z, float strength)
{
	HDSpring s = {};
	std::vector<int> marked;

	strncpy(s.stem, stem ? stem : "", sizeof(s.stem) - 1);
	s.room = -1;
	s.x = x;
	s.z = z;
	s.strength = strength;
	s.period = HDSpringRulePeriod(strength);
	s.body = -1;
	s.how = HD_SPRING_FILE;
	s.seed = HDSpringSeed(s.stem, x, z);

	if (!place(s, marked))
		return false;

	g_springs.push_back(s);
	g_fed.push_back(-1.0e30);
	return true;
}

int HDSpringCount()
{
	return (int)g_springs.size();
}

const HDSpring* HDSpringAt(int i)
{
	return i >= 0 && i < (int)g_springs.size() ? &g_springs[(size_t)i] : 0;
}

/* The rings of the frame, once a frame before the sheets draw: every ring
 * a spring within kHDSpringRingReach of the eye started since the last
 * frame, and the small ring of each bubble that reached the sheet since
 * then and pops (HDSpringBubblePops), on the frame's clock. Nothing while
 * tomb5.hd_motion is off: the sheets draw no ring then. */
void HDSpringRings(float eyeX, float eyeY, float eyeZ)
{
	HDRenderSpringFrame f;

	if (g_springs.empty() || !HDRenderWaterSpringFrame(&f) || !f.motion)
		return;

	const double t = f.clock;

	for (size_t i = 0; i < g_springs.size(); i++)
	{
		const HDSpring& s = g_springs[i];
		double& fed = g_fed[i];

		if (fed > t + 0.1 || fed < t - kRingBack)
			fed = t - kRingBack;

		const float dx = s.x - eyeX, dz = s.z - eyeZ;
		const float dy = (float)s.surfaceY - eyeY;

		if (dx * dx + dy * dy + dz * dz >
			kHDSpringRingReach * kHDSpringRingReach)
		{
			fed = t;
			continue;
		}

		for (int j = (int)floor(fed / s.period) - 1;
			s.rings && j <= (int)floor(t / s.period) + 1; j++)
		{
			const double born = HDSpringRingBorn(s, j);

			if (born > fed && born <= t)
				HDRenderWaterRingAdd(s.x, (float)s.surfaceY, s.z,
					HDSpringRingSize(s.strength), born, 0);
		}

		const float rate = HDSpringBubbleRate(s);
		const HDSpringCrackShape* crack = crack_of(i);
		const double longest = (double)((crack ? crack->bottomY :
			(float)s.floorY) - (float)s.surfaceY) / kHDSpringRiseMin;

		for (int k = (int)floor((fed - longest) * rate) - 1;
			rate > 0.0f && k <= (int)floor(t * rate) + 1; k++)
		{
			HDSpringBubbleAt b;
			float size = 0.0f;

			HDSpringBubble(s, k, t, &b, crack);

			const double top = HDSpringBubbleBorn(s, k) + b.top;

			if (top <= fed || top > t || !HDSpringBubblePops(s, k, &size) ||
				!HDSpringBubble(s, k, top - 1.0e-3, &b, crack))
				continue;

			HDRenderWaterRingAdd(b.x, (float)s.surfaceY, b.z, size, top, 1);
		}

		fed = t;
	}
}

void HDSpringTiming(int on, double* ms)
{
	if (on >= 0)
		g_timing = on != 0;

	if (ms)
		*ms = g_gpu_ms;
}

void HDSpringParts(int mask)
{
	g_parts = mask;
}

void HDSpringDrawn(int* columns, int* bubbles)
{
	if (columns)
		*columns = g_drawn_columns;

	if (bubbles)
		*bubbles = g_drawn_bubbles;
}

/* The column and the bubbles of every spring in view, after the sheets:
 * `toFrame` takes the world into the frame's world, as for the cards
 * (hd_render.cpp, HDRenderWaterCardsDraw). The column needs the frame's
 * copy; without it only the bubbles draw. */
void HDSpringFrameDraw(const float* toFrame)
{
	HDRenderSpringFrame f;

	g_drawn_columns = g_drawn_bubbles = 0;
	g_gpu_ms = -1.0;

	if (g_springs.empty() || !toFrame || !HDRenderWaterSpringFrame(&f) ||
		!f.width || !f.height || !shaders(f.device))
		return;

	if (f.motion)
		g_held = f.clock;

	const double t = f.motion ? (double)f.clock : g_held;
	float m[16];
	float eye[3];

	multiply(toFrame, f.view, m);
	eye_of(m, eye);

	const float focal = f.proj[0] * 0.5f * (float)f.width;
	std::vector<Vertex> column, bubble;

	for (size_t i = 0; i < g_springs.size(); i++)
	{
		/* With a crack the jet rises from the crack's bottom: the column
		 * stands on it and the bubbles are born there. */
		const HDSpring& s = g_springs[i];
		const HDSpringCrackShape* crack = crack_of(i);
		const float foot = crack ? crack->bottomY : (float)s.floorY;
		const float depth = foot - (float)s.surfaceY;
		const float bw[3] = { s.x, foot, s.z };
		const float tw[3] = { s.x, (float)s.surfaceY, s.z };
		float b[3], top[3];

		through(m, bw, b);
		through(m, tw, top);

		/* The column, a quad standing on the jet's line and turned to the
		 * eye, in eight rows up its height; left out where an end lies
		 * behind the eye. It fades as the eye looks along its line, where
		 * the quad turns edge-on, and as its half-width at mid height
		 * falls from kHDSpringFadeFrom to kHDSpringFadeTo pixels. */
		if ((g_parts & 1) && f.copy && depth > 0.0f && b[2] > 1.0f &&
			top[2] > 1.0f)
		{
			const float ax[3] = { top[0] - b[0], top[1] - b[1],
				top[2] - b[2] };
			const float mid[3] = { (b[0] + top[0]) * 0.5f,
				(b[1] + top[1]) * 0.5f, (b[2] + top[2]) * 0.5f };
			float side[3] = { ax[1] * mid[2] - ax[2] * mid[1],
				ax[2] * mid[0] - ax[0] * mid[2],
				ax[0] * mid[1] - ax[1] * mid[0] };
			const float sl = sqrtf(side[0] * side[0] + side[1] * side[1] +
				side[2] * side[2]);
			const float al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] +
				ax[2] * ax[2]);
			const float ml = sqrtf(mid[0] * mid[0] + mid[1] * mid[1] +
				mid[2] * mid[2]);
			const float sine = al > 0.0f && ml > 0.0f ? sl / (al * ml) : 0.0f;
			const float across = HDSpringHalfWidth(s, depth * 0.5f) * focal /
				mid[2];
			float weight = s.strength *
				fminf(fmaxf((sine - 0.2f) / 0.3f, 0.0f), 1.0f) *
				fminf(fmaxf((across - kHDSpringFadeTo) /
				(kHDSpringFadeFrom - kHDSpringFadeTo), 0.0f), 1.0f);

			if (weight > 0.0f && sl > 0.0f)
			{
				for (int k = 0; k < 3; k++)
					side[k] /= sl;

				Vertex row[9][2];

				for (int r = 0; r <= 8; r++)
				{
					const float along = (float)r / 8.0f;
					const float h = depth * along;
					const float hw = HDSpringHalfWidth(s, h);

					for (int e = 0; e < 2; e++)
					{
						const float u = e ? 1.0f : -1.0f;
						Vertex& v = row[r][e];

						v.x = b[0] + ax[0] * along + side[0] * hw * u;
						v.y = b[1] + ax[1] * along + side[1] * hw * u;
						v.z = b[2] + ax[2] * along + side[2] * hw * u;
						v.u = u;
						v.v = along;
						v.cx = hw * u;
						v.cz = h;
						v.r = kHDSpringBend * s.strength * focal /
							fmaxf(v.z, 1.0f);
						v.g = weight;
						v.b = (float)fmod(t * kHDSpringJet,
							(double)kHDSpringNoiseSide * kCellAlong);
						v.a = kHDSpringNear;
						v.light = 1.0f;
					}
				}

				for (int r = 0; r < 8; r++)
				{
					const Vertex quad[6] = { row[r][0], row[r][1],
						row[r + 1][1], row[r][0], row[r + 1][1],
						row[r + 1][0] };

					column.insert(column.end(), quad, quad + 6);
				}

				g_drawn_columns++;
			}
		}

		// The bubbles alive at the clock, each a card facing the eye.
		const float rate = HDSpringBubbleRate(s);

		if (!(g_parts & 2) || rate <= 0.0f || depth <= 0.0f)
			continue;

		float tint[3] = { 1.0f, 1.0f, 1.0f }, light = 1.0f;
		float absorb[3] = { 0.0f, 0.0f, 0.0f };

		HDRenderWaterSpringLook(s.body, tint, &light, absorb);

		const double longest = (double)depth / kHDSpringRiseMin;

		for (int k = (int)floor((t - longest) * rate) - 1;
			k <= (int)floor(t * rate) + 1; k++)
		{
			HDSpringBubbleAt at;

			if (!HDSpringBubble(s, k, t, &at, crack))
				continue;

			const float w[3] = { at.x, at.y, at.z };
			float p[3];

			through(m, w, p);

			if (p[2] < 1.0f || at.radius * focal / p[2] < 0.75f)
				continue;

			/* Seen from over the water, the water over the bubble on the
			 * eye's way to it takes its share per channel, as the sheet's
			 * absorption takes the floor's (hd_render.cpp, gWaterAbsorb);
			 * from under the water the shader's extinction does. */
			float keep[3] = { 1.0f, 1.0f, 1.0f };

			if (eye[1] < (float)s.surfaceY)
			{
				const float ex = at.x - eye[0], ey = at.y - eye[1],
					ez = at.z - eye[2];
				const float path = sqrtf(ex * ex + ey * ey + ez * ez) *
					(at.y - (float)s.surfaceY) / fmaxf(ey, 1.0f);

				for (int c = 0; c < 3; c++)
					keep[c] = expf(-path * absorb[c]);
			}

			static const float corner[6][2] = { { -1, -1 }, { 1, -1 },
				{ 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

			for (int c = 0; c < 6; c++)
			{
				Vertex v;

				v.x = p[0] + corner[c][0] * at.radius;
				v.y = p[1] + corner[c][1] * at.radius;
				v.z = p[2];
				v.u = v.v = 0.0f;
				v.cx = corner[c][0];
				v.cz = corner[c][1];
				v.r = tint[0] * keep[0];
				v.g = tint[1] * keep[1];
				v.b = tint[2] * keep[2];
				v.a = at.share * kBubbleShare * (keep[0] + keep[1] +
					keep[2]) / 3.0f;
				v.light = light;
				bubble.push_back(v);
			}

			g_drawn_bubbles++;
		}
	}

	if (column.empty() && bubble.empty())
		return;

	IDirect3DDevice9* d = f.device;

	// The device's state as the frame left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex[3] = { 0, 0, 0 };
	IDirect3DVertexBuffer9* stream = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
		D3DRS_BLENDOP, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_FOGENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE
	};
	const D3DSAMPLERSTATETYPE samps[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE
	};
	const DWORD stage[3] = { 5, 6, 7 };
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	const int sampCount = (int)(sizeof(samps) / sizeof(samps[0]));
	DWORD saved[stateCount], savedSamp[3][sampCount];

	d->GetVertexShader(&vs);
	d->GetPixelShader(&ps);
	d->GetVertexDeclaration(&decl);
	d->GetFVF(&fvf);
	d->GetStreamSource(0, &stream, &streamOffset, &streamStride);

	for (int i = 0; i < stateCount; i++)
		d->GetRenderState(states[i], &saved[i]);

	for (int k = 0; k < 3; k++)
	{
		d->GetTexture(stage[k], &tex[k]);

		for (int i = 0; i < sampCount; i++)
			d->GetSamplerState(stage[k], samps[i], &savedSamp[k][i]);
	}

	IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };

	if (g_timing &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3])))
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	// The projection alone in c0 - c3, transposed as the renderer does.
	float columns[16];

	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			columns[c * 4 + r] = f.proj[r * 4 + c];

	const bool soft = f.depth != 0;
	const float screen[4] = { 1.0f / (float)f.width, 1.0f / (float)f.height,
		f.proj[10], -f.proj[14] };
	const float mode[4] = { soft ? 1.0f : 0.0f, 1.0f / kSoft, kRatioFloor,
		kShareFrom };
	const float cell[4] = { 1.0f / kCellAcross, 1.0f / kCellAlong,
		1.0f / (float)kHDSpringNoiseSide, 0.0f };

	d->SetVertexShader(g_vs);
	d->SetFVF(kFVF);
	d->SetVertexShaderConstantF(0, columns, 4);
	d->SetPixelShaderConstantF(92, screen, 1);
	d->SetPixelShaderConstantF(93, mode, 1);
	d->SetPixelShaderConstantF(94, cell, 1);
	d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	d->SetRenderState(D3DRS_FOGENABLE, FALSE);
	d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	d->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
	d->SetRenderState(D3DRS_STENCILENABLE, FALSE);

	// The copy and the noise filtered, the depth not; the noise repeats.
	for (int k = 0; k < 3; k++)
	{
		const DWORD filter = k == 1 ? D3DTEXF_POINT : D3DTEXF_LINEAR;
		const DWORD address = k == 2 ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;

		d->SetSamplerState(stage[k], D3DSAMP_MINFILTER, filter);
		d->SetSamplerState(stage[k], D3DSAMP_MAGFILTER, filter);
		d->SetSamplerState(stage[k], D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		d->SetSamplerState(stage[k], D3DSAMP_ADDRESSU, address);
		d->SetSamplerState(stage[k], D3DSAMP_ADDRESSV, address);
		d->SetSamplerState(stage[k], D3DSAMP_SRGBTEXTURE, FALSE);
	}

	d->SetTexture(5, f.copy);
	d->SetTexture(6, f.depth);
	d->SetTexture(7, g_noise);

	if (!column.empty())
	{
		d->SetPixelShader(g_column_ps);
		d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
		d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(column.size() / 3),
			&column[0], sizeof(Vertex));
	}

	if (!bubble.empty())
	{
		d->SetPixelShader(g_bubble_ps);
		d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(bubble.size() / 3),
			&bubble[0], sizeof(Vertex));
	}

	if (q[3])
	{
		UINT64 a = 0, b = 0, freq = 0;
		BOOL disjoint = TRUE;

		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		q[3]->Issue(D3DISSUE_END);

		while (q[0]->GetData(&disjoint, sizeof(disjoint),
			D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[1]->GetData(&a, sizeof(a), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[2]->GetData(&b, sizeof(b), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[3]->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
			S_FALSE) {}

		if (!disjoint && freq && b > a)
			g_gpu_ms = (double)(b - a) * 1000.0 / (double)freq;
	}

	for (int i = 0; i < 4; i++)
		if (q[i])
			q[i]->Release();

	for (int i = 0; i < stateCount; i++)
		d->SetRenderState(states[i], saved[i]);

	for (int k = 0; k < 3; k++)
	{
		for (int i = 0; i < sampCount; i++)
			d->SetSamplerState(stage[k], samps[i], savedSamp[k][i]);

		d->SetTexture(stage[k], tex[k]);

		if (tex[k])
			tex[k]->Release();
	}

	d->SetStreamSource(0, stream, streamOffset, streamStride);
	d->SetFVF(fvf);
	d->SetVertexDeclaration(decl);
	d->SetVertexShader(vs);
	d->SetPixelShader(ps);

	if (stream) stream->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
}

/* ------------------------------------------------------------- the crack
 *
 * Each spring's crack (hd_spring.h, HDSpringCrackMake) laid on the floor
 * the level's .TRG draws under it: per outline point the floor's height,
 * its light and, per edge, its page and its map of world x, z to the page.
 * A wall stands under every edge from the floor's height down to the
 * bottom, a flat plane kHDSpringCrackDepth (scaled) under the outline's
 * lowest point, and carries the floor's page as if the floor were folded
 * over the lip: a place d units down the wall takes the page of the floor d
 * units further into the crack, so the floor's pattern runs on over the
 * lip and down. */
namespace
{
	/* How far the crack's volume reaches over the floor and under its
	 * bottom (see "the hole"): enough for the depth buffer's step at the
	 * far end of a view (24 bits of a - b / z), little enough that nothing
	 * that stands on the floor beside it is taken. A free choice. */
	const float kCrackLift = 8.0f;
	/* The outline's heights may part by this much, a click's three
	 * quarters: over it the floor under the outline is no floor for a
	 * slit (a step, a rock) and the spring keeps no crack. A free choice
	 * against the four springs' floors (the rule's ANDREA2 spring lies on
	 * a slope of 1,194 to 1,450). */
	const float kCrackRough = 192.0f;

	// A floor triangle: world corners, page places, light, page, room.
	struct FloorTri
	{
		float p[3][3];
		float uv[3][2];
		float light[3][3];
		int page;
		int room;
	};

	// A .TRG vertex as the file holds it (vendor/trforge, Trg.h).
	struct TrgCorner
	{
		short x, y, z, sway;
		unsigned char nx, ny, nz, layer;
		unsigned char c1[3], u;
		unsigned char c2[3], v;
	};

	static_assert(sizeof(TrgCorner) == 20, "a .TRG vertex is 20 bytes");

	/* The floor-facing triangles of the opaque pass of the rooms whose box
	 * meets the world box `box` (x0, z0, x1, z1) at heights yLo .. yHi,
	 * out of the level's .TRG, read in pieces by the layout TRForge's
	 * reader documents (vendor/trforge, MIT, trf_remaster/Trg.h): the
	 * header and the groups' part tables in order, then these rooms' index
	 * runs alone and the span of vertices they name - a few kilobytes of
	 * the tens of megabytes. Group i is room i; its vertices are room local
	 * in x and z. Material 16, the still water's art, is left out. A
	 * triangle faces up where its normal (b - a) x (c - a) has half its
	 * length or more in +y, the winding of the floors under the four
	 * springs. */
	bool read_floors(const char* path, const std::vector<HDSpringRoom>& rooms,
		const float* box, float yLo, float yHi, std::vector<FloorTri>& out)
	{
		FILE* f = 0;

		if (!path || fopen_s(&f, path, "rb") || !f)
			return false;

		bool good = true;
		auto get = [&](void* p, size_t n)
		{
			good = good && fread(p, 1, n, f) == n;
		};
		auto skip = [&](long long n)
		{
			good = good && _fseeki64(f, n, SEEK_CUR) == 0;
		};
		auto at = [&](long long n)
		{
			good = good && _fseeki64(f, n, SEEK_SET) == 0;
		};
		unsigned char head[4] = {}, volumes[4] = {};
		int sectors = 0, groups = 0;

		get(head, 4);
		good = good && head[0] == 'T' && head[1] == 'R' && head[2] == 'G' &&
			head[3] == 3;
		get(&sectors, 4);
		good = good && sectors >= 0;
		skip((long long)sectors * 24 + 24);
		get(volumes, 4);
		skip((long long)volumes[2] * 40 + (long long)volumes[3] * 28);
		get(&groups, 4);
		good = good && groups >= 0 && groups < 65536;

		std::vector<int> want(good ? (size_t)groups : 0, -1);

		for (size_t r = 0; r < rooms.size(); r++)
		{
			const HDSpringRoom& q = rooms[r];

			if (q.room >= 0 && q.room < (int)want.size() && q.x1 >= box[0] &&
				q.x0 <= box[2] && q.z1 >= box[1] && q.z0 <= box[3])
				want[(size_t)q.room] = (int)r;
		}

		struct Run
		{
			int group;
			unsigned start, count;
		};

		std::vector<Run> runs;
		std::vector<unsigned char> part;

		for (int g = 0; good && g < groups; g++)
		{
			int parts = 0, lights = 0;

			get(&parts, 4);
			good = good && parts >= 0 && parts < (1 << 20);

			if (good && want[(size_t)g] >= 0 && parts > 0)
			{
				part.resize((size_t)parts * 44);
				get(&part[0], part.size());

				for (int p = 0; good && p < parts; p++)
				{
					unsigned material = 0, start = 0, count = 0;

					memcpy(&material, &part[(size_t)p * 44], 4);
					memcpy(&start, &part[(size_t)p * 44 + 20], 4);
					memcpy(&count, &part[(size_t)p * 44 + 24], 4);

					if (material != 16 && count >= 3)
						runs.push_back({ g, start, count });
				}
			}
			else
				skip((long long)parts * 44);

			skip(4);
			get(&lights, 4);
			good = good && lights >= 0;
			skip((long long)lights * 28);
		}

		unsigned short textures[2] = {};

		get(textures, 4);

		std::vector<unsigned short> pages(textures[0]);

		if (!pages.empty())
			get(&pages[0], pages.size() * 2);

		const long long here = good ? _ftelli64(f) : 0;

		skip((4 - here % 4) % 4);

		unsigned counts[2] = {};

		get(counts, 8);

		const long long indexAt = good ? _ftelli64(f) : 0;
		const long long vertexAt = indexAt + (long long)counts[0] * 4;
		std::vector<unsigned> index;
		std::vector<TrgCorner> corner;

		for (size_t r = 0; good && r < runs.size(); r++)
		{
			const Run& run = runs[r];

			if ((unsigned long long)run.start + run.count > counts[0])
				continue;

			index.resize(run.count);
			at(indexAt + (long long)run.start * 4);
			get(&index[0], index.size() * 4);

			unsigned lo = 0xFFFFFFFFu, hi = 0;

			for (unsigned v : index)
			{
				lo = v < lo ? v : lo;
				hi = v > hi ? v : hi;
			}

			if (!good || hi >= counts[1] || hi - lo >= (1u << 20))
				continue;

			corner.resize((size_t)(hi - lo + 1));
			at(vertexAt + (long long)lo * 20);
			get(&corner[0], corner.size() * 20);

			const HDSpringRoom& room = rooms[(size_t)want[(size_t)run.group]];

			for (unsigned t = 0; good && t + 2 < run.count; t += 3)
			{
				FloorTri tri;
				float mins[3] = { 1.0e9f, 1.0e9f, 1.0e9f };
				float maxs[3] = { -1.0e9f, -1.0e9f, -1.0e9f };

				for (int k = 0; k < 3; k++)
				{
					const TrgCorner& c = corner[index[t + (unsigned)k] - lo];

					tri.p[k][0] = (float)c.x + room.x0;
					tri.p[k][1] = (float)c.y;
					tri.p[k][2] = (float)c.z + room.z0;
					tri.uv[k][0] = (float)c.u / 255.0f;
					tri.uv[k][1] = (float)c.v / 255.0f;

					for (int j = 0; j < 3; j++)
					{
						tri.light[k][j] = (float)(c.c1[j] + c.c2[j]) / 255.0f;
						mins[j] = fminf(mins[j], tri.p[k][j]);
						maxs[j] = fmaxf(maxs[j], tri.p[k][j]);
					}

					if (k == 0)
						tri.page = c.layer >= 1 && c.layer <= pages.size() ?
							(int)pages[c.layer - 1] : -1;

					tri.room = room.room;
				}

				const float a[3] = { tri.p[1][0] - tri.p[0][0],
					tri.p[1][1] - tri.p[0][1], tri.p[1][2] - tri.p[0][2] };
				const float b[3] = { tri.p[2][0] - tri.p[0][0],
					tri.p[2][1] - tri.p[0][1], tri.p[2][2] - tri.p[0][2] };
				const float n[3] = { a[1] * b[2] - a[2] * b[1],
					a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
				const float len = sqrtf(n[0] * n[0] + n[1] * n[1] +
					n[2] * n[2]);
				const float midY = (tri.p[0][1] + tri.p[1][1] +
					tri.p[2][1]) / 3.0f;

				if (len <= 0.0f || n[1] < 0.5f * len || maxs[0] < box[0] ||
					mins[0] > box[2] || maxs[2] < box[1] || mins[2] > box[3] ||
					midY < yLo || midY > yHi)
					continue;

				out.push_back(tri);
			}
		}

		fclose(f);
		return good;
	}

	// The weights of a triangle's corners at world x, z; false outside.
	bool weights(const FloorTri& t, double x, double z, double* w)
	{
		const double x0 = t.p[0][0], z0 = t.p[0][2];
		const double x1 = t.p[1][0], z1 = t.p[1][2];
		const double x2 = t.p[2][0], z2 = t.p[2][2];
		const double d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);

		if (fabs(d) < 1.0e-6)
			return false;

		w[0] = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d;
		w[1] = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d;
		w[2] = 1.0 - w[0] - w[1];
		return w[0] >= -1.0e-4 && w[1] >= -1.0e-4 && w[2] >= -1.0e-4;
	}

	/* The floor under world x, z: of the triangles that hold it, the one
	 * whose height there lies nearest `aim` (the spring's own floor); -1
	 * for none. */
	int floor_under(const std::vector<FloorTri>& tris, float x, float z,
		float aim, double* w)
	{
		int best = -1;
		double gap = 1.0e30;

		for (size_t i = 0; i < tris.size(); i++)
		{
			double v[3];

			if (!weights(tris[i], x, z, v))
				continue;

			const double y = v[0] * tris[i].p[0][1] + v[1] * tris[i].p[1][1] +
				v[2] * tris[i].p[2][1];

			if (fabs(y - aim) < gap)
			{
				gap = fabs(y - aim);
				best = (int)i;
				w[0] = v[0];
				w[1] = v[1];
				w[2] = v[2];
			}
		}

		return best;
	}

	/* A triangle's map of world x, z to its page, about the point cx, cz:
	 * u = m0 + m1 (x - cx) + m2 (z - cz), v the same with m3 - m5. */
	void page_map(const FloorTri& t, double cx, double cz, float* m)
	{
		for (int c = 0; c < 2; c++)
		{
			double w0[3], wx[3], wz[3];

			weights(t, cx, cz, w0);
			weights(t, cx + 1.0, cz, wx);
			weights(t, cx, cz + 1.0, wz);

			double u0 = 0.0, ux = 0.0, uz = 0.0;

			for (int k = 0; k < 3; k++)
			{
				u0 += w0[k] * t.uv[k][c];
				ux += wx[k] * t.uv[k][c];
				uz += wz[k] * t.uv[k][c];
			}

			m[c * 3 + 0] = (float)u0;
			m[c * 3 + 1] = (float)(ux - u0);
			m[c * 3 + 2] = (float)(uz - u0);
		}
	}

	/* The slit's width across from edge p: from its middle along its inward
	 * normal to the nearest other edge, at most the crack's width. */
	float slit_gap(const HDSpringCrackShape& c, int p, float sense)
	{
		const int q = (p + 1) % c.points;
		const float ex = c.x[q] - c.x[p], ez = c.z[q] - c.z[p];
		const float el = sqrtf(ex * ex + ez * ez);

		if (el <= 0.0f)
			return c.width;

		const float dx = -ez / el * sense, dz = ex / el * sense;
		const float ox = 0.5f * (c.x[p] + c.x[q]);
		const float oz = 0.5f * (c.z[p] + c.z[q]);
		float best = c.width;

		for (int i = 0; i < c.points; i++)
		{
			const int j = (i + 1) % c.points;

			if (i == p)
				continue;

			// The ray o + t d against the edge a + s (b - a), 0 <= s <= 1.
			const float ax = c.x[i], az = c.z[i];
			const float bx = c.x[j] - ax, bz = c.z[j] - az;
			const float den = dx * bz - dz * bx;

			if (fabsf(den) < 1.0e-6f)
				continue;

			const float t = ((ax - ox) * bz - (az - oz) * bx) / den;
			const float s = ((ax - ox) * dz - (az - oz) * dx) / den;

			if (t > 0.0f && s >= 0.0f && s <= 1.0f && t < best)
				best = t;
		}

		return best;
	}

	// The outline's turn: 1 when its inside lies left of each edge.
	float turn_of(const HDSpringCrackShape& c)
	{
		double area = 0.0;

		for (int i = 0; i < c.points; i++)
		{
			const int j = (i + 1) % c.points;

			area += (double)c.x[i] * c.z[j] - (double)c.x[j] * c.z[i];
		}

		return area > 0.0 ? 1.0f : -1.0f;
	}
}

/* The cracks of the level's springs, after HDSpringBuild (and RoomProbe's
 * -spring): for each, the first size that lies on its body's deep squares,
 * the floor under every point of its outline out of `trg`, and its bottom
 * kHDSpringCrackDepth (scaled) under the outline's lowest point. A spring
 * whose crack cannot be laid keeps none and says why (SPRINGLOG crack=). */
void HDSpringCrackBuild(const char* trg, const std::vector<HDSpringRoom>& rooms,
	int on)
{
	g_cracks.assign(g_springs.size(), Crack());
	g_crack_key = on;

	std::vector<HDRenderWaterSquare> squares;

	for (size_t i = 0; i < g_springs.size(); i++)
	{
		const HDSpring& s = g_springs[i];
		Crack& k = g_cracks[i];

		memset(&k, 0, sizeof(k));
		k.state = on ? CRACK_FIT : CRACK_OFF;

		if (!on)
			continue;

		std::vector<HDSpringSquare> body;
		const int n = s.body >= 0 ? HDRenderWaterBodySquares(s.body, 0, 0, 0) :
			0;

		if (n > 0)
		{
			squares.resize((size_t)n);
			HDRenderWaterBodySquares(s.body, &squares[0], n, 0);

			for (const HDRenderWaterSquare& q : squares)
			{
				const HDSpringSquare sq = { q.x, q.z, q.depth, q.sheet };

				body.push_back(sq);
			}
		}

		HDSpringCrackShape& c = k.shape;

		if (!HDSpringCrackChoose(s, body, &c))
			continue;

		const float sense = turn_of(c);

		for (int p = 0; p < c.points; p++)
			k.gap[p] = slit_gap(c, p, sense);

		float box[4] = { 1.0e9f, 1.0e9f, -1.0e9f, -1.0e9f };

		for (int p = 0; p < c.points; p++)
		{
			box[0] = fminf(box[0], c.x[p] - 64.0f);
			box[1] = fminf(box[1], c.z[p] - 64.0f);
			box[2] = fmaxf(box[2], c.x[p] + 64.0f);
			box[3] = fmaxf(box[3], c.z[p] + 64.0f);
		}

		std::vector<FloorTri> tris;

		if (!read_floors(trg, rooms, box, (float)s.surfaceY + 64.0f,
			(float)s.floorY + 768.0f, tris))
		{
			k.state = CRACK_FILE;
			continue;
		}

		k.state = CRACK_DRAWN;

		float lo = 1.0e9f, hi = -1.0e9f;
		double w[3];

		for (int p = 0; p < c.points && k.state == CRACK_DRAWN; p++)
		{
			const int t = floor_under(tris, c.x[p], c.z[p], (float)s.floorY,
				w);

			if (t < 0)
			{
				k.state = CRACK_FLOOR;
				break;
			}

			const FloorTri& f = tris[(size_t)t];

			k.rimY[p] = (float)(w[0] * f.p[0][1] + w[1] * f.p[1][1] +
				w[2] * f.p[2][1]);

			for (int j = 0; j < 3; j++)
				k.light[p][j] = (float)(w[0] * f.light[0][j] +
					w[1] * f.light[1][j] + w[2] * f.light[2][j]);

			lo = fminf(lo, k.rimY[p]);
			hi = fmaxf(hi, k.rimY[p]);

			// The edge to the next point: the floor under its middle.
			const int q = (p + 1) % c.points;
			const float mx = 0.5f * (c.x[p] + c.x[q]);
			const float mz = 0.5f * (c.z[p] + c.z[q]);
			int e = floor_under(tris, mx, mz, (float)s.floorY, w);

			e = e < 0 ? t : e;
			page_map(tris[(size_t)e], s.x, s.z, k.map[p]);
			k.page[p] = tris[(size_t)e].page;
		}

		const int half = kHDSpringCrackSteps / 2;
		const int t = k.state == CRACK_DRAWN ? floor_under(tris,
			c.midX[half], c.midZ[half], (float)s.floorY, w) : -1;

		if (k.state == CRACK_DRAWN && t < 0)
			k.state = CRACK_FLOOR;

		if (k.state != CRACK_DRAWN)
			continue;

		const FloorTri& f = tris[(size_t)t];

		k.middleY = (float)(w[0] * f.p[0][1] + w[1] * f.p[1][1] +
			w[2] * f.p[2][1]);
		k.room = f.room;
		lo = fminf(lo, k.middleY);
		hi = fmaxf(hi, k.middleY);

		if (hi - lo > kCrackRough)
		{
			k.state = CRACK_ROUGH;
			continue;
		}

		c.bottomY = hi + c.depth;
	}
}

const HDSpringCrackShape* HDSpringCrackAt(int i)
{
	return i >= 0 ? crack_of((size_t)i) : 0;
}

/* RoomProbe -springs: spring i's crack, a line for the whole (`at` -1)
 * and one for each point of its outline (`at` 0 up); false past the last.
 *
 *   SPRINGCRACK spring=<i> state=<why> fit=<f> length=<l> width=<w>
 *               depth=<d> branches=<b> points=<n> bottom=<y> middle=<y>
 *               round=<r>
 *   SPRINGCRACK spring=<i> point=<p> x=<x> z=<z> floor=<y> page=<page>
 *               u=<u> v=<v> light=<r>,<g>,<b>
 *
 * the share of its size it fits at, its sizes, the floor under the main
 * line's middle and the round of the branches' hash (HDSpringCrackMake);
 * per point its world place, the floor's height under it, the page of the
 * edge to the next point and that page's place under the point. */
bool HDSpringCrackLine(int i, int at, char* out, size_t size)
{
	static const char* kWhy[6] = { "drawn", "off", "fit", "file", "floor",
		"rough" };

	if (i < 0 || (size_t)i >= g_cracks.size())
		return false;

	const Crack& k = g_cracks[(size_t)i];
	const HDSpringCrackShape& c = k.shape;
	const HDSpring& s = g_springs[(size_t)i];

	if (at < 0)
	{
		snprintf(out, size, "SPRINGCRACK spring=%d state=%s fit=%.2f "
			"length=%.0f width=%.0f depth=%.0f branches=%d points=%d "
			"bottom=%.0f middle=%.0f round=%d", i, kWhy[k.state], c.fit,
			c.length, c.width, c.depth, c.branches, c.points, c.bottomY,
			k.middleY, c.round);
		return true;
	}

	if (k.state != CRACK_DRAWN || at >= c.points)
		return false;

	const float* m = k.map[at];
	const float dx = c.x[at] - s.x, dz = c.z[at] - s.z;

	snprintf(out, size, "SPRINGCRACK spring=%d point=%d x=%.1f z=%.1f "
		"floor=%.1f page=%d u=%.3f v=%.3f light=%.2f,%.2f,%.2f", i, at,
		c.x[at], c.z[at], k.rimY[at], k.page[at], m[0] + m[1] * dx + m[2] * dz,
		m[3] + m[4] * dx + m[5] * dz, k.light[at][0], k.light[at][1],
		k.light[at][2]);
	return true;
}

void HDSpringCrackNumbers(int* drawn, double* ms)
{
	if (drawn)
		*drawn = g_drawn_cracks;

	if (ms)
		*ms = g_crack_ms;
}

/* The cracks in view, where the opaque frame is whole and the blend phase
 * not begun (hd_level.cpp, HDFrameDrawGlow; RoomProbe the same), so the
 * frame's copy the sheets bend and the depth they read both hold them. Per
 * crack, with the stencil's bit kCrackBit (the receives keep bits 1 and 2,
 * the creatures' mark bit 4):
 *
 *   the volume  the outline from kCrackLift over the floor to kCrackLift
 *               under the bottom: a fan of the outline on top and below,
 *               a quad down each edge
 *   1 clear     the volume, the bit set to 0 wherever it lies
 *   2 mark      the volume against the frame's depth, every face in front
 *               of what was drawn turning the bit over: an odd count is a
 *               pixel whose surface lies inside the volume - the floor
 *               where the crack opens it, and nothing that stands over the
 *               floor (fans over a concave outline cancel out the same way)
 *   3 the hole  the volume again on the marked pixels alone, black, at the
 *               depth of the bottom's plane along the pixel's ray (HolePS),
 *               the bit back to 0
 *   4 walls     each edge's wall with the depth test and write, the
 *               floor's page on it (CrackPS): where the hole is open they
 *               lie in front of the bottom, elsewhere behind the floor
 *
 * The bottom the walls leave open is the hole's own black. Nothing when
 * the frame's depth has no stencil, the eye is under the floor or the
 * volume reaches the near plane, where the count would be cut; and only
 * while `drawn` says the room of the crack's floor is drawn this frame:
 * a ray to the crack then meets the floor or what stands in front of it,
 * never an empty place of the frame whose depth the count would meet
 * inside the volume. */
void HDSpringCrackFrameDraw(const float* toFrame, bool (*drawn)(int room))
{
	HDRenderSpringFrame f;

	g_drawn_cracks = 0;
	g_crack_ms = -1.0;

	if (g_cracks.empty() || !toFrame || !HDRenderWaterSpringFrame(&f) ||
		!f.width || !f.height || !shaders(f.device) || !g_crack_ps ||
		!g_hole_ps)
		return;

	IDirect3DDevice9* d = f.device;
	IDirect3DSurface9* ds = 0;
	D3DSURFACE_DESC dd = {};
	const D3DFORMAT kIntz = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');
	const bool stencil = SUCCEEDED(d->GetDepthStencilSurface(&ds)) && ds &&
		SUCCEEDED(ds->GetDesc(&dd)) && (dd.Format == D3DFMT_D24S8 ||
		dd.Format == D3DFMT_D24FS8 || dd.Format == kIntz);

	if (ds)
		ds->Release();

	if (!stencil)
		return;

	const DWORD kCrackBit = 8;
	float m[16];
	float eye[3];

	multiply(toFrame, f.view, m);
	eye_of(m, eye);

	/* The near plane, where the depth a - b / z is 0, and the far one at
	 * 0.995 (HDRenderMakeProjection): what the frame leaves empty there
	 * holds the sky's or the cleared depth, and a crack whose volume reaches
	 * it would count its faces against that and open a black hole in the
	 * sky. Half the far plane's distance is the farthest a crack is drawn:
	 * the fog has taken the floor long before (RoomPS). */
	const float a = f.proj[10], b = -f.proj[14];
	const float nearZ = a > 0.0f ? b / a : 1.0f;
	const float farZ = a > 0.995f ? b / (a - 0.995f) : 1.0e30f;
	const float focal = f.proj[0] * 0.5f * (float)f.width;

	struct Draw
	{
		size_t crack;
		std::vector<Vertex> volume, walls;
		std::vector<int> pages;		// one per wall triangle pair
		float plane[4];
		float look[4];
	};

	std::vector<Draw> draws;

	for (size_t i = 0; i < g_cracks.size() && i < g_springs.size(); i++)
	{
		const Crack& k = g_cracks[i];
		const HDSpringCrackShape& c = k.shape;

		if (k.state != CRACK_DRAWN || c.points < 3 || (drawn &&
			!drawn(k.room)))
			continue;

		float lowest = k.middleY;

		for (int p = 0; p < c.points; p++)
			lowest = fminf(lowest, k.rimY[p]);

		// The eye over the floor (y down), else the crack is not seen.
		if (eye[1] >= lowest - kCrackLift)
			continue;

		Draw dr;
		const int half = kHDSpringCrackSteps / 2;
		const float top = -kCrackLift, under = c.bottomY + kCrackLift;
		float nearest = 1.0e30f, farthest = 0.0f;

		auto put = [&](std::vector<Vertex>& list, float x, float y, float z,
			float u, float v, float down, float h, float gap,
			const float* light)
		{
			const float w[3] = { x, y, z };
			Vertex vx;
			float p[3];

			through(m, w, p);
			nearest = fminf(nearest, p[2]);
			farthest = fmaxf(farthest, p[2]);
			vx.x = p[0];
			vx.y = p[1];
			vx.z = p[2];
			vx.u = u;
			vx.v = v;
			vx.cx = down;
			vx.cz = h;
			vx.r = light ? light[0] : 0.0f;
			vx.g = light ? light[1] : 0.0f;
			vx.b = light ? light[2] : 0.0f;
			vx.a = gap;
			vx.light = 1.0f;
			list.push_back(vx);
		};

		// The volume: the two fans and the quads down the edges.
		for (int p = 0; p < c.points; p++)
		{
			const int q = (p + 1) % c.points;
			const float pt = k.rimY[p] + top, qt = k.rimY[q] + top;
			const float tri[12][3] =
			{
				{ c.midX[half], k.middleY + top, c.midZ[half] },
				{ c.x[p], pt, c.z[p] }, { c.x[q], qt, c.z[q] },
				{ c.midX[half], under, c.midZ[half] },
				{ c.x[p], under, c.z[p] }, { c.x[q], under, c.z[q] },
				{ c.x[p], pt, c.z[p] }, { c.x[q], qt, c.z[q] },
				{ c.x[q], under, c.z[q] }, { c.x[p], pt, c.z[p] },
				{ c.x[q], under, c.z[q] }, { c.x[p], under, c.z[p] }
			};

			for (int v = 0; v < 12; v++)
				put(dr.volume, tri[v][0], tri[v][1], tri[v][2], 0, 0, 0, 0, 0,
					0);
		}

		/* The walls: the edge from p to q at the floor down to the bottom,
		 * the page's place at a point d units down taken from the floor d
		 * units further in, along the edge's inward normal; with the way
		 * down, the height over the bottom and the slit's width across
		 * from the edge for CrackPS's shade. */
		const float sense = turn_of(c);

		for (int p = 0; p < c.points; p++)
		{
			const int q = (p + 1) % c.points;
			const float ex = c.x[q] - c.x[p], ez = c.z[q] - c.z[p];
			const float el = sqrtf(ex * ex + ez * ez);

			if (el <= 0.0f)
				continue;

			const float ix = -ez / el * sense, iz = ex / el * sense;
			const float* map = k.map[p];
			const int ends[2] = { p, q };
			const int order[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 },
				{ 1, 1 }, { 0, 1 } };

			for (int v = 0; v < 6; v++)
			{
				const int at = ends[order[v][0]];
				const bool low = order[v][1] != 0;
				const float down = low ? c.bottomY - k.rimY[at] : 0.0f;
				const float x = c.x[at] + ix * down - g_springs[i].x;
				const float z = c.z[at] + iz * down - g_springs[i].z;

				put(dr.walls, c.x[at], low ? c.bottomY : k.rimY[at], c.z[at],
					map[0] + map[1] * x + map[2] * z,
					map[3] + map[4] * x + map[5] * z, down,
					low ? 0.0f : c.bottomY - k.rimY[at], k.gap[p], k.light[at]);
			}

			dr.pages.push_back(k.page[p]);
		}

		/* Cut by the near plane the count is wrong; too far, see farZ; too
		 * small to be seen. */
		if (nearest < 2.0f * nearZ || farthest > 0.5f * farZ ||
			c.length * focal / nearest < 2.0f)
			continue;

		// Off the screen: the volume wholly to one side of it.
		float lo[2] = { 1.0e30f, 1.0e30f }, hi[2] = { -1.0e30f, -1.0e30f };

		for (const Vertex& v : dr.volume)
		{
			const float sx = f.proj[0] * v.x / v.z + f.proj[8];
			const float sy = f.proj[5] * v.y / v.z + f.proj[9];

			lo[0] = fminf(lo[0], sx);
			hi[0] = fmaxf(hi[0], sx);
			lo[1] = fminf(lo[1], sy);
			hi[1] = fmaxf(hi[1], sy);
		}

		if (hi[0] < -1.0f || lo[0] > 1.0f || hi[1] < -1.0f || lo[1] > 1.0f)
			continue;

		/* The bottom's plane in view space out of three of its points, its
		 * normal turned so that the eye lies on its near side. */
		const float w0[3] = { c.midX[half], c.bottomY, c.midZ[half] };
		const float w1[3] = { c.midX[half] + 256.0f, c.bottomY,
			c.midZ[half] };
		const float w2[3] = { c.midX[half], c.bottomY,
			c.midZ[half] + 256.0f };
		float p0[3], p1[3], p2[3];

		through(m, w0, p0);
		through(m, w1, p1);
		through(m, w2, p2);

		const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
		const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

		if (nl <= 0.0f)
			continue;

		float dist = (n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]) / nl;

		for (int j = 0; j < 3; j++)
			n[j] /= nl;

		if (dist < 0.0f)
		{
			dist = -dist;

			for (int j = 0; j < 3; j++)
				n[j] = -n[j];
		}

		dr.crack = i;
		dr.plane[0] = n[0];
		dr.plane[1] = n[1];
		dr.plane[2] = n[2];
		dr.plane[3] = dist;
		dr.look[0] = c.depth > 0.0f ? 1.0f / c.depth : 1.0f;
		dr.look[1] = dr.look[2] = dr.look[3] = 0.0f;
		draws.push_back(std::move(dr));
	}

	if (draws.empty())
		return;

	// The device's state as the frame left it, put back below.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DBaseTexture9* tex0 = 0;
	IDirect3DVertexBuffer9* stream = 0;
	UINT streamOffset = 0, streamStride = 0;
	DWORD fvf = 0;
	const D3DRENDERSTATETYPE states[] =
	{
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,
		D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_FOGENABLE, D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE,
		D3DRS_TWOSIDEDSTENCILMODE, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
		D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS,
		D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_SCISSORTESTENABLE,
		D3DRS_SRGBWRITEENABLE
	};
	const D3DSAMPLERSTATETYPE samps[] =
	{
		D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE
	};
	const int stateCount = (int)(sizeof(states) / sizeof(states[0]));
	const int sampCount = (int)(sizeof(samps) / sizeof(samps[0]));
	DWORD saved[stateCount], savedSamp[sampCount];
	float savedPixel[6][4], savedVertex[4][4];

	d->GetVertexShader(&vs);
	d->GetPixelShader(&ps);
	d->GetVertexDeclaration(&decl);
	d->GetFVF(&fvf);
	d->GetStreamSource(0, &stream, &streamOffset, &streamStride);
	d->GetTexture(0, &tex0);
	d->GetPixelShaderConstantF(92, &savedPixel[0][0], 6);
	d->GetVertexShaderConstantF(0, &savedVertex[0][0], 4);

	for (int s = 0; s < stateCount; s++)
		d->GetRenderState(states[s], &saved[s]);

	for (int s = 0; s < sampCount; s++)
		d->GetSamplerState(0, samps[s], &savedSamp[s]);

	IDirect3DQuery9* q[4] = { 0, 0, 0, 0 };

	if (g_timing &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &q[0])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[1])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q[2])) &&
		SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &q[3])))
	{
		q[0]->Issue(D3DISSUE_BEGIN);
		q[1]->Issue(D3DISSUE_END);
	}

	// The projection alone in c0 - c3, transposed as the renderer does.
	float columns[16];

	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			columns[c * 4 + r] = f.proj[r * 4 + c];

	/* A pixel's ray at view depth 1 (HDRenderMakeProjection: x' = _11 x /
	 * z + _31, y' = _22 y / z + _32; the pixel at vpos lies at x' = 2 vpos.x
	 * / width - 1, y' = 1 - 2 vpos.y / height). */
	const float screen[4] = { 1.0f / (float)f.width, 1.0f / (float)f.height,
		a, b };
	const float ray[4] = { 2.0f / ((float)f.width * f.proj[0]),
		-2.0f / ((float)f.height * f.proj[5]),
		(-1.0f - f.proj[8]) / f.proj[0], (1.0f - f.proj[9]) / f.proj[5] };

	d->SetVertexShader(g_vs);
	d->SetFVF(kFVF);
	d->SetVertexShaderConstantF(0, columns, 4);
	d->SetPixelShaderConstantF(92, screen, 1);
	d->SetPixelShaderConstantF(95, ray, 1);
	d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	d->SetRenderState(D3DRS_FOGENABLE, FALSE);
	d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	d->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
	d->SetRenderState(D3DRS_STENCILMASK, kCrackBit);
	d->SetRenderState(D3DRS_STENCILWRITEMASK, kCrackBit);
	d->SetRenderState(D3DRS_STENCILREF, kCrackBit);
	d->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	d->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	// The page as the rooms sample it, repeating: the floor's pattern runs
	// on past the page's edge as the floor's squares repeat it.
	d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

	const UINT stride = sizeof(Vertex);

	for (const Draw& dr : draws)
	{
		const UINT tris = (UINT)(dr.volume.size() / 3);

		d->SetPixelShaderConstantF(96, dr.plane, 1);
		d->SetPixelShaderConstantF(97, dr.look, 1);
		d->SetRenderState(D3DRS_STENCILENABLE, TRUE);

		// 1 clear: the bit to 0 over the volume.
		d->SetPixelShader(g_crack_ps);
		d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
		d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		d->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		d->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
		d->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_ZERO);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, &dr.volume[0], stride);

		// 2 mark: every face in front of the frame turns the bit over.
		d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		d->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INVERT);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, &dr.volume[0], stride);

		// 3 the hole on the marked pixels, the bit back to 0.
		d->SetPixelShader(g_hole_ps);
		d->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED |
			D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
		d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
		d->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		d->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
		d->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_ZERO);
		d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, &dr.volume[0], stride);

		// 4 the walls, a draw for each run of one page.
		d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		d->SetPixelShader(g_crack_ps);
		d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

		for (size_t w = 0; w < dr.pages.size(); )
		{
			size_t end = w + 1;

			while (end < dr.pages.size() && dr.pages[end] == dr.pages[w])
				end++;

			IDirect3DTexture9* page = dr.pages[w] >= 0 ?
				HDPageCacheTexture(dr.pages[w]) : 0;

			if (page)
			{
				d->SetTexture(0, page);
				d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)((end - w) * 2),
					&dr.walls[w * 6], stride);
			}

			w = end;
		}

		g_drawn_cracks++;
	}

	if (q[3])
	{
		UINT64 t0 = 0, t1 = 0, freq = 0;
		BOOL disjoint = TRUE;

		q[2]->Issue(D3DISSUE_END);
		q[0]->Issue(D3DISSUE_END);
		q[3]->Issue(D3DISSUE_END);

		while (q[0]->GetData(&disjoint, sizeof(disjoint),
			D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[1]->GetData(&t0, sizeof(t0), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[2]->GetData(&t1, sizeof(t1), D3DGETDATA_FLUSH) == S_FALSE) {}
		while (q[3]->GetData(&freq, sizeof(freq), D3DGETDATA_FLUSH) ==
			S_FALSE) {}

		if (!disjoint && freq && t1 > t0)
			g_crack_ms = (double)(t1 - t0) * 1000.0 / (double)freq;
	}

	for (int i = 0; i < 4; i++)
		if (q[i])
			q[i]->Release();

	for (int s = 0; s < stateCount; s++)
		d->SetRenderState(states[s], saved[s]);

	for (int s = 0; s < sampCount; s++)
		d->SetSamplerState(0, samps[s], savedSamp[s]);

	d->SetTexture(0, tex0);

	if (tex0)
		tex0->Release();

	d->SetPixelShaderConstantF(92, &savedPixel[0][0], 6);
	d->SetVertexShaderConstantF(0, &savedVertex[0][0], 4);
	d->SetStreamSource(0, stream, streamOffset, streamStride);
	d->SetFVF(fvf);
	d->SetVertexDeclaration(decl);
	d->SetVertexShader(vs);
	d->SetPixelShader(ps);

	if (stream) stream->Release();
	if (decl) decl->Release();
	if (vs) vs->Release();
	if (ps) ps->Release();
}

void HDSpringRelease()
{
	release_shaders();
}

#ifndef HD_SPRING_TOOL
/* ------------------------------------------------------------ the engine */
#include "hd_level.h"			// HDLevelActive
#include "../function_stubs.h"		// Log, gLogEnabled
#include "../../tomb5/tomb5.h"		// tomb5.hd_water
#include "../../game/camera.h"		// camera.pos, the eye
#include "../../game/objects.h"		// LARA
#include "../../game/control.h"		// items, level_items
#include "../registry.h"		// REG_PeekLong - the key hd_springs
#include "../3dmath.h"			// the engine's matrix - the view
#include "hd_install.h"			// HDInstallRoot - the level's .TRG
#include "../../game/draw.h"		// RoomOnDrawList - the frame's rooms

// Whether the frame's walk drew a room (game\draw.cpp).
static bool RoomDrawn(int room)
{
	return room >= 0 && room < number_rooms &&
		RoomOnDrawList((short)room);
}

/* What the rule keeps clear of, out of the level as loaded - the build
 * runs before a saved game is restored, so Lara's item stands at the
 * level's start: every portal through a wall (normal y 0) of every room,
 * its corners from the room's origin; Lara's start; every static. */
static void LevelBlocks(std::vector<HDSpringBlock>& out)
{
	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];
		const short* door = info->door;

		for (int i = 0, count = door ? *door++ : 0; i < count; i++,
			door += 16)
		{
			if (door[2])
				continue;

			HDSpringBlock b = { HD_SPRING_PORTAL, 1.0e9f, 1.0e9f, -1.0e9f,
				-1.0e9f, 1.0e9f, -1.0e9f };

			for (int k = 0; k < 4; k++)
			{
				const float x = (float)(info->x + door[4 + k * 3]);
				const float y = (float)(info->y + door[5 + k * 3]);
				const float z = (float)(info->z + door[6 + k * 3]);

				b.x0 = fminf(b.x0, x);
				b.x1 = fmaxf(b.x1, x);
				b.y0 = fminf(b.y0, y);
				b.y1 = fmaxf(b.y1, y);
				b.z0 = fminf(b.z0, z);
				b.z1 = fmaxf(b.z1, z);
			}

			out.push_back(b);
		}

		for (int m = 0; info->mesh && m < info->num_meshes; m++)
		{
			const float x = (float)info->mesh[m].x;
			const float z = (float)info->mesh[m].z;
			const HDSpringBlock b = { HD_SPRING_STATIC, x, z, x, z,
				-1.0e9f, 1.0e9f };

			out.push_back(b);
		}
	}

	for (int i = 0; i < level_items; i++)
		if (items[i].object_number == LARA)
		{
			const float x = (float)items[i].pos.x_pos;
			const float z = (float)items[i].pos.z_pos;
			const HDSpringBlock b = { HD_SPRING_START, x, z, x, z,
				-1.0e9f, 1.0e9f };

			out.push_back(b);
		}
}

/* The rooms for the cracks' floor: each room's box in world x and z, its
 * origin the box's least corner; x runs over y_size squares and z over
 * x_size, TR5's own naming (hd_level.cpp, BuildRoomBoxes). */
static void LevelRooms(std::vector<HDSpringRoom>& out)
{
	for (int r = 0; r < number_rooms; r++)
	{
		const ROOM_INFO* info = &room[r];
		const HDSpringRoom q = { r, (float)info->x, (float)info->z,
			(float)(info->x + ((long)info->y_size << 10)),
			(float)(info->z + ((long)info->x_size << 10)) };

		out.push_back(q);
	}
}

/* At the level's build, after the sheets and before the life, which
 * reads the springs as its bodies' springs (HDRenderWaterBody::spring).
 * The key hd_springs (a DWORD beside the options, read here, never
 * written): absent the rule's own one body in kHDSpringEvery, 0 no spring
 * of the rule, n one in kHDSpringEvery times n. The key hd_springcrack
 * the same way: absent or 1 every spring has its crack, 0 none. */
void HDSpringLevelBegin(const char* stem)
{
	HDSpringClear();

	if (!HDLevelActive() || !tomb5.hd_water || !HDRenderWaterSheetsBuilt())
		return;

	ulong thin = 1, crack = 1;
	std::vector<HDSpringBlock> blocks;
	std::vector<HDSpringRoom> rooms;

	if (!REG_PeekLong("tomb5", "hd_springs", thin))
		thin = 1;

	if (!REG_PeekLong("tomb5", "hd_springcrack", crack))
		crack = 1;

	LevelBlocks(blocks);
	HDSpringBuild(stem, blocks, (int)(thin > 1000 ? 1000 : thin));
	LevelRooms(rooms);

	std::string trg = HDInstallRoot();

	if (!trg.empty() && trg.back() != '\\' && trg.back() != '/')
		trg += '\\';

	trg += std::string("DATA\\") + (stem ? stem : "") + ".TRG";
	HDSpringCrackBuild(trg.c_str(), rooms, crack ? 1 : 0);

	char line[256];

	for (int i = -1; gLogEnabled && HDSpringLogLine(i, line, sizeof(line));
		i++)
		Log("%s", line);
}

void HDSpringLevelEnd()
{
	HDSpringClear();
}

// Once a frame, after the game's ripples went to the sheets.
void HDSpringFeed()
{
	if (!HDLevelActive())
		return;

	HDSpringRings((float)camera.pos.x, (float)camera.pos.y,
		(float)camera.pos.z);
}

// After the water's cards, with their matrix (hd_level.cpp, WorldToFrame).
void HDSpringDraw(const float* toFrame)
{
	if (!HDLevelActive())
		return;

	HDSpringFrameDraw(toFrame);
}

/* The cracks, at the start of the blend phase (hd_level.cpp,
 * HDFrameDrawGlow): the world into the HD frame's view space through the
 * engine's own matrix, which in the HD frame is the view itself, as
 * hd_level.cpp's WorldToFrame takes it for the cards. */
void HDSpringCrackDraw()
{
	if (!HDLevelActive() || g_cracks.empty())
		return;

	float m[16];

	phd_PushMatrix();
	phd_TranslateAbs(0, 0, 0);
	m[0] = aMXPtr[M00];
	m[1] = aMXPtr[M10];
	m[2] = aMXPtr[M20];
	m[3] = 0.0f;
	m[4] = aMXPtr[M01];
	m[5] = aMXPtr[M11];
	m[6] = aMXPtr[M21];
	m[7] = 0.0f;
	m[8] = aMXPtr[M02];
	m[9] = aMXPtr[M12];
	m[10] = aMXPtr[M22];
	m[11] = 0.0f;
	m[12] = aMXPtr[M03];
	m[13] = aMXPtr[M13];
	m[14] = aMXPtr[M23];
	m[15] = 1.0f;
	phd_PopMatrix();
	HDSpringCrackFrameDraw(m, RoomDrawn);
}

// The shaders belong to the device that is going away.
void HDSpringDeviceLost()
{
	HDSpringRelease();
}
#endif

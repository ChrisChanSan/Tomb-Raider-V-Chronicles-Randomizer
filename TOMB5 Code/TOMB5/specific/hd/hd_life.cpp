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

/* The water's life in the game: the pool of hd_life_rules.h built out of
 * this level's water bodies (hd_render.h, HDRenderWaterBodyInfo and
 * HDRenderWaterBodySquares), ticked and drawn once a frame, its drips
 * written into the game's ripples so the sheets ring. The models are the
 * remaster's own files, found by name through the model resolver like any
 * object's. */

#include "../../tomb5/pch.h"
#include "hd_life.h"
#include "hd_life_rules.h"
#include "hd_model.h"
#include "hd_object.h"
#include "hd_render.h"
#include "../3dmath.h"
#include "../function_stubs.h"
#include "../../tomb5/tomb5.h"
#include "../../game/control.h"
#include "../../game/lara.h"
#include "../../game/tomb4fx.h"

#include <algorithm>
#include <string.h>

namespace
{
	HDLifePool g_pool;
	const HDLifeLevel* g_row = 0;
	char g_stem[16] = { 0 };
	float g_box[HD_LIFE_KINDS][6];
	// The light each body's models take, classic 0..255 (128 is one).
	std::vector<float> g_light;
	// The per-second LIFELOG line's counts.
	int g_second = -1;
	int g_drawn = 0, g_skipped = 0, g_rings = 0, g_frames = 0;
	int g_ripple_full = 0;

	int draw_random(void*)
	{
		return (int)GetRandomDraw();
	}

	bool life_on()
	{
		return tomb5.hd_water && HDRenderWaterSheetsBuilt();
	}

	/* A drip's ring into the game's ripples, with the fields SetupRipple
	 * (game\effect2.cpp) writes: flags 0x10 (a drip, which the sheets take
	 * as a ring - hd_level.cpp, RippleIsRing), size, life 48 and up to 15
	 * more, init 1. Not SetupRipple itself: that draws its life from
	 * GetRandomControl, and the game's own random sequence must not depend
	 * on what is drawn. The life's 0..15 comes from the pool's hash. False
	 * when all 128 slots are in use. */
	bool put_ripple(const HDLifeRing& r)
	{
		for (int i = 0; i < MAX_RIPPLES; i++)
		{
			RIPPLE_STRUCT& s = ripples[i];

			if (s.flags & 1)
				continue;

			s.flags = 0x10 | 1;
			s.size = (uchar)r.size;
			s.life = (uchar)(48 + (HDLifeHash((unsigned)r.drip,
				(unsigned)r.size) & 15));
			s.init = 1;
			s.x = (long)r.x;
			s.y = (long)r.y;
			s.z = (long)r.z;
			return true;
		}

		return false;
	}

	// a x b, 4x4 row-major, row vectors.
	void mul44(const float* a, const float* b, float* out)
	{
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] +
					a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] +
					a[r * 4 + 3] * b[3 * 4 + c];
	}

	/* The view of a world point: the stack's base - the world-to-view matrix
	 * with the shake phd_GenerateW2V puts on it, as Lara's braid takes it
	 * (hd_laradraw.cpp, braid_matrices) - translated to the point, row-major
	 * for row vectors as the HD objects take their nodes. */
	void view_at(float x, float y, float z, float* out)
	{
		phd_PushMatrix();

		for (int i = 0; i < indices_count; i++)
		{
			aMXPtr[i] = aFMatrixStack[i];
			phd_mxptr[i] = matrix_stack[i];
		}

		phd_TranslateAbs((long)x, (long)y, (long)z);

		const float* m = aMXPtr;

		out[0] = m[M00];  out[1] = m[M10];  out[2] = m[M20];  out[3] = 0.0f;
		out[4] = m[M01];  out[5] = m[M11];  out[6] = m[M21];  out[7] = 0.0f;
		out[8] = m[M02];  out[9] = m[M12];  out[10] = m[M22]; out[11] = 0.0f;
		out[12] = m[M03]; out[13] = m[M13]; out[14] = m[M23]; out[15] = 1.0f;
		phd_PopMatrix();
	}

	int slot_of(int kind)
	{
		return HD_LIFE_SLOT_BASE + kind;
	}
}

void HDLifeModelPlan(const char* stem, std::vector<HDModelSlot>& wanted)
{
	const HDLifeLevel* row = HDLifeLevelOf(stem);

	if (!row || !life_on())
		return;

	for (int k = 0; k < HD_LIFE_KINDS; k++)
	{
		const bool used = k == HD_LIFE_FISH ? row->fish != 0 :
			row->weight[k] > 0;

		if (!used)
			continue;

		HDModelSlot one;

		one.slot = slot_of(k);
		one.nodes = 1;
		one.name = kHDLifeKinds[k].model;
		one.rigidNode = 0;
		wanted.push_back(one);
	}
}

void HDLifeLevelEnd()
{
	g_pool = HDLifePool();
	g_light.clear();
	g_row = 0;
	g_stem[0] = 0;
	g_second = -1;
}

void HDLifeLevelBegin(const char* stem)
{
	HDLifeLevelEnd();

	const HDLifeLevel* row = HDLifeLevelOf(stem);

	if (!row || !life_on())
		return;

	g_row = row;
	strncpy(g_stem, stem, sizeof(g_stem) - 1);

	char models[160] = { 0 };

	for (int k = 0; k < HD_LIFE_KINDS; k++)
	{
		float* box = g_box[k];

		g_pool.has[k] = HDModelHas(slot_of(k)) &&
			HDModelBounds(slot_of(k), box, box + 3) ? 1 : 0;
		g_pool.height[k] = g_pool.has[k] ?
			HDLifeHeight(kHDLifeKinds[k].rest, box, box + 3) : 0.0f;

		if (g_pool.has[k])
		{
			strncat(models, models[0] ? "," : "", sizeof(models) -
				strlen(models) - 1);
			strncat(models, kHDLifeKinds[k].name, sizeof(models) -
				strlen(models) - 1);
		}
	}

	std::vector<HDRenderWaterSquare> squares;

	for (int w = 0; w < HDRenderWaterBodies(); w++)
	{
		HDRenderWaterBody info;
		float rise = 0.0f;
		const int n = HDRenderWaterBodySquares(w, 0, 0, 0);

		if (n <= 0 || !HDRenderWaterBodyInfo(w, &info) || info.noSheet)
			continue;

		squares.resize((size_t)n);
		HDRenderWaterBodySquares(w, &squares[0], n, &rise);

		HDLifeBody b;

		b.index = w;
		b.surfaceY = info.surfaceY;
		b.flowX = info.flow[0];
		b.flowZ = info.flow[1];
		b.speed = info.speed;
		b.river = info.cls == HD_WATER_RIVER ? 1 : 0;
		b.channel = info.cls == HD_WATER_CHANNEL ? 1 : 0;
		b.rise = rise;
		b.upMin = b.upMax = 0.0f;

		for (int i = 0; i < n; i++)
			if (squares[(size_t)i].sheet)
			{
				HDLifeSquare s;

				s.x = squares[(size_t)i].x;
				s.z = squares[(size_t)i].z;
				s.depth = squares[(size_t)i].depth;
				s.closed = squares[(size_t)i].open ? 0 : 1;
				b.squares.push_back(s);
			}

		if (!HDLifeTakes(*row, (int)b.squares.size(), b.river, b.channel))
			continue;

		g_pool.bodies.push_back(b);

		/* The body's light, the one its sheet's colours are lit by, as the
		 * models' ambient: 255 for a light of one. A free choice, set by
		 * eye on the round's RoomProbe pictures. */
		for (int k = 0; k < 3; k++)
			g_light.push_back(info.light[k] * 255.0f);
	}

	HDLifeBuild(g_pool, *row, draw_random, 0);

	if (!gLogEnabled)
		return;

	/* LIFELOG, the level line: the bodies the life took, its floats and
	 * fish, the drips, the floats a body's rise or a missing model left
	 * out (skipped), the pool's bytes and the models that loaded. */
	const size_t bytes = sizeof(g_pool) +
		g_pool.entries.size() * sizeof(HDLifeEntry) +
		g_pool.schools.size() * sizeof(HDLifeSchool) +
		g_pool.drips.size() * sizeof(HDLifeDrip) +
		g_pool.bodies.size() * sizeof(HDLifeBody);
	size_t squaresBytes = 0;

	for (size_t i = 0; i < g_pool.bodies.size(); i++)
		squaresBytes += g_pool.bodies[i].squares.size() *
			sizeof(HDLifeSquare);

	Log("LIFELOG level=%s bodies=%d floats=%d fish=%d drips=%d skipped=%d "
		"bytes=%d models=%s", g_stem, (int)g_pool.bodies.size(),
		g_pool.floats, g_pool.fish, (int)g_pool.drips.size(), g_pool.unfit,
		(int)(bytes + squaresBytes), models[0] ? models : "none");

	/* And a line a body: the water body it is, its class (river,
	 * channel, other), its sheet squares, the rise its sheet's vertices
	 * take (units), its current, its floats, fish and drips, and its
	 * squares deep enough for fish and under a ceiling. */
	for (size_t bi = 0; bi < g_pool.bodies.size(); bi++)
	{
		const HDLifeBody& b = g_pool.bodies[bi];
		int floats = 0, fish = 0, drips = 0, closed = 0;

		for (size_t i = 0; i < g_pool.entries.size(); i++)
			if (g_pool.entries[i].body == (int)bi)
				(g_pool.entries[i].kind == HD_LIFE_FISH ? fish : floats)++;

		for (size_t i = 0; i < g_pool.drips.size(); i++)
			drips += g_pool.drips[i].body == (int)bi ? 1 : 0;

		for (size_t i = 0; i < b.squares.size(); i++)
			closed += b.squares[i].closed;

		Log("LIFELOG level=%s body=%d water=%d class=%s squares=%d "
			"rise=%.1f flow=%.2f,%.2f,%.0f floats=%d fish=%d drips=%d "
			"deep=%d closed=%d", g_stem, (int)bi, b.index,
			b.river ? "river" : b.channel ? "channel" : "other",
			(int)b.squares.size(), b.rise, b.flowX, b.flowZ, b.speed, floats,
			fish, drips, HDLifeDeepCount(b), closed);
	}
}

void HDLifeFrame(double clock)
{
	if (g_pool.entries.empty() && g_pool.drips.empty())
		return;

	if (!life_on())
		return;

	const float t = (float)clock;
	const int laraOn = lara_item ? 1 : 0;
	const float lx = laraOn ? (float)lara_item->pos.x_pos : 0.0f;
	const float lz = laraOn ? (float)lara_item->pos.z_pos : 0.0f;
	std::vector<HDLifeRing> rings;

	HDLifeTick(g_pool, g_row, t, laraOn, lx, lz, &rings);

	for (size_t i = 0; i < rings.size(); i++)
		if (put_ripple(rings[i]))
			g_rings++;
		else
			g_ripple_full++;

	/* The draw: the entries nearest the eye first, at most kHDLifeDrawMax
	 * of them; an entry behind the eye is not pushed. */
	const float ex = (float)w2v_matrix[M03];
	const float ey = (float)w2v_matrix[M13];
	const float ez = (float)w2v_matrix[M23];
	std::vector<std::pair<float, int> > order;

	for (size_t i = 0; i < g_pool.entries.size(); i++)
	{
		const HDLifeEntry& e = g_pool.entries[i];

		if (!g_pool.has[e.kind])
			continue;

		float x, y, z, yaw, pitch, roll;

		HDLifePose(g_pool, e, t, &x, &y, &z, &yaw, &pitch, &roll);

		const float dx = x - ex, dy = y - ey, dz = z - ez;

		order.push_back(std::make_pair(dx * dx + dy * dy + dz * dz, (int)i));
	}

	std::sort(order.begin(), order.end());

	int drawn = 0, skipped = 0;

	for (size_t k = 0; k < order.size(); k++)
	{
		if (drawn >= kHDLifeDrawMax)
		{
			skipped++;
			continue;
		}

		const HDLifeEntry& e = g_pool.entries[(size_t)order[k].second];
		float x, y, z, yaw, pitch, roll;

		HDLifePose(g_pool, e, t, &x, &y, &z, &yaw, &pitch, &roll);

		float view[16], local[16], node[16];

		view_at(x, y, z, view);

		// Behind the eye: its point's view depth.
		if (view[14] < 0.0f)
			continue;

		const float* box = g_box[e.kind];

		HDLifeMatrix(kHDLifeKinds[e.kind].rest, box, box + 3, yaw, pitch,
			roll, local);
		mul44(local, view, node);

		const float share = HDLifeLightShare(e.kind);
		const float light[3] = { g_light[(size_t)e.body * 3] * share,
			g_light[(size_t)e.body * 3 + 1] * share,
			g_light[(size_t)e.body * 3 + 2] * share };

		if (HDObjectPush(slot_of(e.kind), node, 1, light, ~0u, 0, 0, 0.75f))
			drawn++;
		else
			skipped++;
	}

	g_drawn += drawn;
	g_skipped += skipped;
	g_frames++;

	/* LIFELOG, once a second of the clock with -log: the frames, the
	 * entries drawn and skipped over them (summed), the most drawn in one
	 * frame cannot pass kHDLifeDrawMax; the drip rings written, those the
	 * full ripple table refused, the river floats come back upstream, and
	 * the schools fleeing now. */
	if (gLogEnabled && (int)t != g_second)
	{
		int fleeing = 0;

		for (size_t i = 0; i < g_pool.schools.size(); i++)
			fleeing += g_pool.schools[i].fleeUntil > t ? 1 : 0;

		if (g_second >= 0)
			Log("LIFELOG level=%s second=%d frames=%d drawn=%d skipped=%d "
				"rings=%d full=%d respawned=%d fleeing=%d", g_stem, g_second,
				g_frames, g_drawn, g_skipped, g_rings, g_ripple_full,
				g_pool.respawned, fleeing);

		g_second = (int)t;
		g_drawn = g_skipped = g_rings = g_frames = g_ripple_full = 0;
	}
}

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
#include "../gamemain.h"
#include "../output.h"
#include "../specificfx.h"
#include "../function_stubs.h"
#include "../../tomb5/tomb5.h"
#include "../../game/control.h"
#include "../../game/lara.h"
#include "../../game/tomb4fx.h"
#include "../../game/objects.h"

#include <stdlib.h>

#include <algorithm>
#include <string.h>

namespace
{
	HDLifePool g_pool;
	const HDLifeLevel* g_row = 0;
	char g_stem[16] = { 0 };
	float g_box[HD_LIFE_KINDS][6];
	/* The light each body's models take, the classic 0..255 triple
	 * HDObjectPush reads (its neutral light is 128); a body light of one
	 * gives 255 here (HDLifeLevelBegin). */
	std::vector<float> g_light;
	// Per body, a tank's FISHTANK item (tank_item), -1 for none.
	std::vector<int> g_tank_item;
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

	/* The ceiling over world x, z from the room `at` up: the ceiling byte
	 * of the square's sector, through the room above (sky_room) where the
	 * ceiling is an opening, as GetCeiling walks it - the flat byte, a
	 * sloped ceiling's tilt left out. False outside the rooms. */
	bool ceiling_at(int at, int x, int z, int* y, int* in)
	{
		for (int guard = 0; guard < 16 && at >= 0 && at < number_rooms;
			guard++)
		{
			const ROOM_INFO* r = &room[at];
			const int sx = (x - (int)r->x) >> 10, sz = (z - (int)r->z) >> 10;

			if (sx < 0 || sz < 0 || sx >= r->y_size || sz >= r->x_size)
				return false;

			const FLOOR_INFO* f = &r->floor[sz + sx * r->x_size];

			if (f->sky_room == NO_ROOM)
			{
				*y = f->ceiling * 256;
				*in = at;
				return true;
			}

			at = f->sky_room;
		}

		return false;
	}

	/* The FISHTANK item in front of a tank: within two squares of its
	 * middle, its room or not (a broken tank's item was taken off its room
	 * by KillItem); -1 when the level has none. */
	int tank_item(const HDLifeBody& b)
	{
		if (b.squares.empty())
			return -1;

		const int mx = b.squares.front().x / 2 + (b.squares.back().x +
			1024) / 2;
		const int mz = b.squares.front().z / 2 + (b.squares.back().z +
			1024) / 2;

		for (int i = 0; i < level_items; i++)
		{
			const ITEM_INFO& it = items[i];

			if (it.object_number == FISHTANK && abs(it.pos.x_pos - mx) < 2048 &&
				abs(it.pos.z_pos - mz) < 2048)
				return i;
		}

		return -1;
	}

	static_assert(ITEM_ACTIVE == kHDLifeItemActive &&
		ITEM_DEACTIVATED == kHDLifeItemDeactivated &&
		ITEM_INVISIBLE == kHDLifeItemInvisible &&
		IFL_INVISIBLE == kHDLifeFlagInvisible &&
		IFL_CODEBITS == kHDLifeFlagCodebits,
		"the tank rule reads the game's item states");

	/* Whether a tank is whole: its FISHTANK item has not been set off
	 * (HDLifeTankWhole). A tank without one cannot break. */
	bool tank_whole(int item)
	{
		if (item < 0)
			return true;

		const ITEM_INFO& it = items[item];

		return HDLifeTankWhole(1, it.active, it.status, it.item_flags[1]);
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

	/* The drops falling at clock t (HDLifePool::falling), each drawn as
	 * DrawDrips (specific\specificfx.cpp) draws a drip: a line of the sort
	 * list from its head to half a tick's move over it, the tail at half
	 * the head's colour, as wide as GetFixedScale(1), not drawn past 0x5000
	 * units of view depth. Nothing of the game's drips is read or written
	 * (hd_life_rules.h, the drips). Called before the frame's sort list is
	 * sorted and drawn (S_OutputPolyList). */
	void draw_drops(float t)
	{
		TEXTURESTRUCT tex;
		D3DTLVERTEX v[2];

		memset(v, 0, sizeof(v));
		tex.drawtype = 2;
		tex.tpage = 0;
		tex.flag = 0;

		for (size_t i = 0; i < g_pool.falling.size(); i++)
		{
			const HDLifeDrop& o = g_pool.falling[i];
			const float ticks = (t - o.at) * 30.0f;

			if (ticks < 0.0f || ticks >= (float)o.ticks)
				continue;

			float step, view[16];
			const float fall = HDLifeDropFall(ticks, &step);
			const float lift = 0.5f * step < fall ? 0.5f * step : fall;

			view_at((float)o.x, (float)o.y + fall, (float)o.z, view);

			const float z0 = view[14], z1 = view[14] - view[6] * lift;

			if (z0 <= 0.0f || z1 <= 0.0f || z0 > (float)0x5000)
				continue;

			long x0 = long(view[12] * f_persp / z0 + f_centerx);
			long y0 = long(view[13] * f_persp / z0 + f_centery);
			long x1 = long((view[12] - view[4] * lift) * f_persp / z1 +
				f_centerx);
			long y1 = long((view[13] - view[5] * lift) * f_persp / z1 +
				f_centery);

			if (!ClipLine(x0, y0, (long)z0, x1, y1, (long)z1, phd_winxmin,
				phd_winymin, phd_winxmax, phd_winymax))
				continue;

			const long r = kHDLifeDropColour[0], g = kHDLifeDropColour[1],
				b = kHDLifeDropColour[2];

			v[0].sx = (float)x0;
			v[0].sy = (float)y0;
			v[0].sz = z0;
			v[0].rhw = f_mpersp / v[0].sz * f_moneopersp;
			v[0].color = RGBA(r, g, b, 0xFF);
			v[0].specular = 0xFF000000;
			v[1].sx = (float)x1;
			v[1].sy = (float)y1;
			v[1].sz = z1;
			v[1].rhw = f_mpersp / v[1].sz * f_moneopersp;
			v[1].color = RGBA(r >> 1, g >> 1, b >> 1, 0xFF);
			v[1].specular = 0xFF000000;
			clipflags[0] = 0;
			clipflags[1] = 0;
			clipflags[2] = 0;
			clipflags[3] = 0;
			AddPolyLine(v, &tex, (float)GetFixedScale(1));
		}
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
	g_tank_item.clear();
	g_row = 0;
	g_stem[0] = 0;
	g_second = -1;
}

void HDLifeLevelBegin(const char* stem)
{
	/* The same level built again - after a lost device, as HDLevelUnload
	 * alone ends a level (HDLifeLevelEnd): its pool is kept if its water
	 * comes out the same (HDLifeSameBodies, below). */
	HDLifePool kept;
	const bool again = !g_pool.bodies.empty() && stem &&
		!_stricmp(stem, g_stem);

	if (again)
		kept = g_pool;

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
		g_pool.reach[k] = g_pool.has[k] ?
			HDLifeReach(kHDLifeKinds[k].rest, box, box + 3) : 0.0f;

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
		b.spring = info.spring;
		b.springX = info.springAt[0];
		b.springZ = info.springAt[1];

		for (int i = 0; i < n; i++)
			if (squares[(size_t)i].sheet)
			{
				HDLifeSquare s;

				s.x = squares[(size_t)i].x;
				s.z = squares[(size_t)i].z;
				s.depth = squares[(size_t)i].depth;
				s.closed = squares[(size_t)i].open ? 0 : 1;
				s.room = squares[(size_t)i].room;
				b.squares.push_back(s);
			}

		if (!HDLifeTakes(*row, (int)b.squares.size(), b.river, b.channel))
			continue;

		g_pool.bodies.push_back(b);

		/* The body's light, the one its sheet's colours are lit by, as the
		 * models' ambient: 255 for a body light of one, twice the push's
		 * neutral 128. A free choice, set by eye on the round's RoomProbe
		 * pictures. */
		for (int k = 0; k < 3; k++)
			g_light.push_back(info.light[k] * 255.0f);
	}

	std::vector<HDLifeBody> keptWater;

	for (size_t i = 0; i < kept.bodies.size(); i++)
		if (!kept.bodies[i].tank)
			keptWater.push_back(kept.bodies[i]);

	const bool keep = again && HDLifeSameBodies(keptWater, g_pool.bodies);

	if (keep)
	{
		// Its models' sizes as loaded now; everything else as it was.
		for (int k = 0; k < HD_LIFE_KINDS; k++)
		{
			kept.has[k] = g_pool.has[k];
			kept.height[k] = g_pool.height[k];
			kept.reach[k] = g_pool.reach[k];
		}

		g_pool = kept;
	}
	else
	{
		HDLifeBuild(g_pool, *row, draw_random, 0);

		for (size_t i = 0; i < sizeof(kHDLifeTanks) / sizeof(kHDLifeTanks[0]);
			i++)
			if (!_stricmp(kHDLifeTanks[i].stem, stem))
				HDLifeAddTank(g_pool, kHDLifeTanks[i], draw_random, 0);
	}

	/* A tank's light (kHDLifeTankLight) and the FISHTANK item in front of
	 * it, which a shot sets off (tank_whole). */
	g_tank_item.assign(g_pool.bodies.size(), -1);

	for (size_t bi = 0; bi < g_pool.bodies.size(); bi++)
	{
		if (!g_pool.bodies[bi].tank)
			continue;

		for (int k = 0; k < 3; k++)
			g_light.push_back(kHDLifeTankLight);

		g_tank_item[bi] = tank_item(g_pool.bodies[bi]);
	}

	// The drips' ceilings and their drops' fall (HDLifeDropTicks).
	for (size_t i = 0; i < g_pool.drips.size(); i++)
	{
		HDLifeDrip& d = g_pool.drips[i];

		if (ceiling_at(d.room, (int)d.x, (int)d.z, &d.ceilY, &d.ceilRoom))
			d.fall = HDLifeDropTicks(d.ceilY + kHDLifeDropUnder,
				g_pool.bodies[(size_t)d.body].surfaceY);
	}

	if (!gLogEnabled)
		return;

	/* LIFELOG, the level line: the bodies the life took, its floats and
	 * fish, the drips, the floats a body's rise, its depth or a missing
	 * model left out (skipped), the pool's bytes, the models that loaded
	 * and 1 when the pool was kept through a rebuild of the level. */
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
		"bytes=%d models=%s kept=%d", g_stem, (int)g_pool.bodies.size(),
		g_pool.floats, g_pool.fish, (int)g_pool.drips.size(), g_pool.unfit,
		(int)(bytes + squaresBytes), models[0] ? models : "none",
		keep ? 1 : 0);

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

		char tail[512];

		HDLifeBodyTail(g_pool, (int)bi, tail, sizeof(tail));
		Log("LIFELOG level=%s body=%d water=%d class=%s squares=%d "
			"rise=%.1f flow=%.2f,%.2f,%.0f floats=%d fish=%d drips=%d "
			"deep=%d closed=%d %s", g_stem, (int)bi, b.index,
			b.tank ? "tank" : b.river ? "river" : b.channel ? "channel" :
			"other",
			(int)b.squares.size(), b.rise, b.flowX, b.flowZ, b.speed, floats,
			fish, drips, HDLifeDeepCount(b), closed, tail);
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
	/* The game's live splashes (SetupSplash, game\effect2.cpp: a dive, a
	 * thing falling in), read only: one on a body's water scares its
	 * school. */
	float wet[8];
	int wets = 0;

	for (int i = 0; i < 4; i++)
		if (splashes[i].flags & 1)
		{
			wet[wets * 2] = (float)splashes[i].x;
			wet[wets * 2 + 1] = (float)splashes[i].z;
			wets++;
		}

	// A shot tank's fish are gone.
	for (size_t bi = 0; bi < g_pool.bodies.size() && bi < g_tank_item.size();
		bi++)
		if (g_pool.bodies[bi].tank)
			g_pool.bodies[bi].gone = tank_whole(g_tank_item[bi]) ? 0 : 1;

	HDLifeTick(g_pool, g_row, t, laraOn, lx, lz, &rings, wet, wets);

	for (size_t i = 0; i < rings.size(); i++)
		if (put_ripple(rings[i]))
			g_rings++;
		else
			g_ripple_full++;

	draw_drops(t);

	/* The draw: the entries nearest the eye first, at most kHDLifeDrawMax
	 * of them; an entry behind the eye is not pushed. */
	const float ex = (float)w2v_matrix[M03];
	const float ey = (float)w2v_matrix[M13];
	const float ez = (float)w2v_matrix[M23];
	std::vector<std::pair<float, int> > order;

	for (size_t i = 0; i < g_pool.entries.size(); i++)
	{
		const HDLifeEntry& e = g_pool.entries[i];

		if (!g_pool.has[e.kind] || g_pool.bodies[(size_t)e.body].gone)
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

		/* Its kind's share of the body's light, and a river's or a
		 * conveyor's float's fade into its slots (HDLifeDiveLightShare) on
		 * the light and the sun. */
		const float fade = HDLifeDiveLightShare(HDLifeDiveShare(
			g_pool.bodies[(size_t)e.body], e.x, e.z, e.entering));
		const float share = HDLifeLightShare(e.kind) * fade;
		const float light[3] = { g_light[(size_t)e.body * 3] * share,
			g_light[(size_t)e.body * 3 + 1] * share,
			g_light[(size_t)e.body * 3 + 2] * share };

		/* No engine lights of its own; the sun at 0.75, the share the
		 * game's own objects take in play (hd_object.cpp, fill_lights). */
		if (HDObjectPush(slot_of(e.kind), node, 1, light, ~0u, 0, 0,
			0.75f * fade))
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

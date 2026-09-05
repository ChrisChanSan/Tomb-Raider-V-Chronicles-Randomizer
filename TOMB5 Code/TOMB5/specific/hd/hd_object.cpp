/*
 * Modified 2026 by ChrisChanSan.
 *
 * The routing rules are carried over from TROne (Copyright 2026 Todesengel,
 * GPLv3) - see hd_object.h for the list, function by function. The seam under
 * them, the skeleton test and the light conversion are new. See THIRD-PARTY.md.
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
#include "../../tomb5/pch.h"
#include "hd_object.h"
#include "hd_model.h"
#include "hd_bind.h"
#include "hd_install.h"
#include "hd_render.h"
#include "hd_level.h"
#include "../function_stubs.h"
#include "../d3dmatrix.h"
#include "../3dmath.h"
#include "../lighting.h"
#include "../winmain.h"
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// items, bones, room
#include "../../game/draw.h"		// InterpolateMatrix
#include "../../game/effect2.h"		// dynamics
#include "../../game/newinv2.h"		// InventoryActive - the sun is doubled in there

#include <math.h>
#include <vector>

/* A classic TR5 moveable has at most 32 meshes and the joint palette in the
 * shader is 32 registers wide, so 32 is the number everywhere. */
#define HD_OBJ_NODES	32

/* How many HD items one frame can hold. draw_rooms holds 100 rooms and every
 * one of them can hold items, so this is not a bound anybody can reason about
 * from the level data - it is a fixed array with a counter, and the counter
 * goes into the log if it ever bites. 128 items is 1.1 MiB of static, which
 * is the price of a frame path that cannot fail. */
#define HD_OBJ_ITEMS	128

namespace
{
	/* WHAT ONE SLOT IS ALLOWED TO DO, decided once at level build. */
	struct Verdict
	{
		unsigned char hd;	// 1 = this slot's items draw HD
		const char* why;	// why not, for the log
		int nodes;		// the classic mesh tree's node count
		int maxBone;		// the highest node the .TRM names
		float drift;		// the bind drift, mean over the covered nodes
		float worst;
		unsigned int covered;
	};

	Verdict g_slot[NUMBER_OBJECTS];
	bool g_active = false;
	char g_stem[64] = { 0 };

	int g_hd_slots = 0;
	int g_classic_slots = 0;

	/* One item, waiting for HDObjectFrameDraw. */
	struct FrameItem
	{
		int slot;
		int nodes;
		unsigned int mask;		// item->mesh_bits
		float matrix[HD_OBJ_NODES * 16];
		float light[HD_OBJ_NODES * 3];
		HDObjLight lights[HD_MAX_OBJ_LIGHTS];
		int lightCount;
		float sunScale;
	};

	FrameItem g_frame[HD_OBJ_ITEMS];
	int g_count = 0;
	int g_dropped = 0;

	/* The item the engine is walking right now. */
	bool g_open = false;
	int g_open_slot = -1;
	int g_open_nodes = 0;
	unsigned int g_open_mask = 0;
	float g_raw[HD_OBJ_NODES * 12];		// the engine's own 3x4, per node
	unsigned int g_seen = 0;

	/* Per FRAME, emptied by HDObjectFrameReset - they go into a line that
	 * says "frame", so they have to mean one. */
	int g_items_classic = 0;
	int g_swap_ignored = 0;
	int g_mask_used = 0;

	/* Per LEVEL: how often a light had to be turned away at the shader's
	 * twelve. Once is worth knowing, every frame is noise. */
	int g_clipped_lights = 0;
	int g_draw_calls = 0;
	bool g_checked = false;
	bool g_frame_logged = false;

	/* THE THRESHOLD OF THE BIND DRIFT lives in hd_model.h, because the
	 * diagnostic tool has to judge a slot by the same number the game does.
	 * How it was measured is in docs\Engine-Manual.md: ModelProbe -all over
	 * all fourteen randomized levels and their vanilla twins, 700-odd bound
	 * slots. The highest drift of a model that belongs where it stands is
	 * 239 and the lowest of one that does not is 308, and 256 sits in that
	 * gap - nearer the accepted end on purpose, because refusing a good model
	 * costs a classic-looking door and accepting a bad one costs a torn body.
	 *
	 * It is only the SECOND test. The first - does the .TRM name a node the
	 * slot does not have - is a certainty rather than a number, and it is the
	 * one that catches FIREROPE in eleven of the fourteen. */
	const float kDriftLimit = HD_SKELETON_DRIFT_LIMIT;

	/* ------------------------------------------------------ the bind pose -- */

	/* THE SLOT'S BIND POSE, out of the engine's own mesh tree. Four longs a
	 * node after the first - flags, x, y, z - and the flags carry the matrix
	 * stack: bit 0 pops, bit 1 pushes, and the push happens BEFORE the
	 * translation, so what is pushed is the parent's frame. It is
	 * DrawAnimatingItem's walk with every rotation left out, which is what a
	 * .TRM's vertices are measured against.
	 *
	 * The same walk stands in ModelProbe (ReadClassicSlot) against the .TRC
	 * on disk; this one reads the loaded level, so what the game binds is
	 * what the tool measured. */
	void bind_pose_of(int slot, float* out /* nodes * 3 */, int nodes)
	{
		const OBJECT_INFO& object = objects[slot];
		float current[3] = { 0, 0, 0 };
		float stack[HD_OBJ_NODES][3];
		int top = 0;

		memset(out, 0, sizeof(float) * 3 * nodes);

		if (!bones)
			return;

		for (int node = 1; node < nodes; node++)
		{
			const long* bone = &bones[object.bone_index + (node - 1) * 4];
			const long flags = bone[0];

			if ((flags & 1) && top > 0)
				memcpy(current, stack[--top], sizeof(current));

			if (flags & 2)
			{
				memcpy(stack[top], current, sizeof(current));

				if (top < HD_OBJ_NODES - 1)
					++top;
			}

			current[0] += (float)bone[1];
			current[1] += (float)bone[2];
			current[2] += (float)bone[3];

			out[node * 3 + 0] = current[0];
			out[node * 3 + 1] = current[1];
			out[node * 3 + 2] = current[2];
		}
	}

	/* ---------------------------------------------------------- the light -- */

	/* WORLD -> VIEW. aW2VMx holds the camera's rotation in M00..M22 and its
	 * world position in M03 / M13 / M23, so a point is R * (p - c) - which is
	 * the very line phd_TranslateAbs computes (specific\3dmath.cpp:243-250).
	 * A direction is the same without the subtraction. */
	void world_to_view(float x, float y, float z, bool direction, float* out)
	{
		if (!direction)
		{
			x -= aW2VMx[M03];
			y -= aW2VMx[M13];
			z -= aW2VMx[M23];
		}

		out[0] = aW2VMx[M00] * x + aW2VMx[M01] * y + aW2VMx[M02] * z;
		out[1] = aW2VMx[M10] * x + aW2VMx[M11] * y + aW2VMx[M12] * z;
		out[2] = aW2VMx[M20] * x + aW2VMx[M21] * y + aW2VMx[M22] * z;
	}

	bool add_light(FrameItem& out, const HDObjLight& light)
	{
		if (out.lightCount >= HD_MAX_OBJ_LIGHTS)
		{
			g_clipped_lights++;
			return false;
		}

		out.lights[out.lightCount++] = light;
		return true;
	}

	/* THE OBJECT'S LIGHTS, from the very lists the classic path reads.
	 *
	 * SuperSetupLight (specific\lighting.cpp:79-182) turns each of them into a
	 * direction in the NODE's model space and a falloff measured from the
	 * node's world position. Both of those are per node and the shader can do
	 * better than either, so what travels here is the light itself: a sun's
	 * direction, everything else's position, all in view space. The activity,
	 * the fade and the range test are NOT redone - `Active` and the faded r/g/b
	 * are the engine's own answer, worked out in CreateLightList a moment ago
	 * inside CalculateObjectLighting.
	 *
	 * The order is on purpose. Suns first because there are at most a handful
	 * and they light the whole body; then the dynamics, because a torch, a
	 * flare and a gun flash are what a player watches for; then the room's own
	 * points and spots. Only past twelve does anything fall off the end, and
	 * then it is the least of them.
	 */
	void fill_lights(FrameItem& out, ITEM_INFO* lit)
	{
		HDObjLight light;
		void* lists[2];
		long counts[2];

		out.lightCount = 0;
		out.sunScale = InventoryActive ? 2.0f : 0.75f;

		lists[0] = lit ? lit->il.pCurrentLights : 0;
		lists[1] = lit ? lit->il.pPrevLights : 0;
		counts[0] = lit ? lit->il.nCurrentLights : 0;
		counts[1] = lit ? lit->il.nPrevLights : 0;

		/* --- the suns ---------------------------------------------------- */
		for (int list = 0; list < 2; list++)
		{
			PCLIGHT* source = (PCLIGHT*)lists[list];

			for (long i = 0; source && i < counts[list]; i++)
			{
				if (!source[i].Active || source[i].Type != LIGHT_SUN)
					continue;

				/* MINUS one, because the level files carry the direction
				 * inverted - SuperSetupLight says so in the original comment
				 * and this is the same number. */
				const float length = (float)sqrt(SQUARE(source[i].nx) +
					SQUARE(source[i].ny) + SQUARE(source[i].nz));

				if (length <= 0.0f)
					continue;

				const float scale = -1.0f / length;
				float view[3];

				world_to_view(source[i].nx * scale, source[i].ny * scale,
					source[i].nz * scale, true, view);

				light.x = view[0];
				light.y = view[1];
				light.z = view[2];
				light.kind = (float)HD_OBJLIGHT_SUN;
				light.r = source[i].r * 255.0f;
				light.g = source[i].g * 255.0f;
				light.b = source[i].b * 255.0f;
				light.invRange = 0.0f;

				add_light(out, light);
			}
		}

		/* --- the dynamics ------------------------------------------------ */
		for (int i = 0; i < 32; i++)
		{
			DYNAMIC* dynamic = &dynamics[i];

			if (!dynamic->on)
				continue;

			/* SuperSetupDynamicLight's own falloff, which is NOT the room
			 * path's: an object uses falloff - (falloff >> 3), a roomlet
			 * (falloff >> 1) + (falloff >> 3). */
			const float falloff = (float)(dynamic->falloff - (dynamic->falloff >> 3));

			if (falloff <= 0.0f)
				continue;

			/* The engine tests the distance at each NODE and drops the light
			 * for that node alone. Here it is tested once against the item,
			 * with a node's reach of slack, and the shader's own falloff
			 * throws away what is left over - so the pixel is the same and
			 * only the upload is one longer. */
			const float dx = (float)dynamic->x - (float)(lit ? lit->il.item_pos.x : 0);
			const float dy = (float)dynamic->y - (float)(lit ? lit->il.item_pos.y : 0);
			const float dz = (float)dynamic->z - (float)(lit ? lit->il.item_pos.z : 0);

			if (sqrt(SQUARE(dx) + SQUARE(dy) + SQUARE(dz)) > falloff + 1024.0f)
				continue;

			float view[3];

			world_to_view((float)dynamic->x, (float)dynamic->y, (float)dynamic->z,
				false, view);

			light.x = view[0];
			light.y = view[1];
			light.z = view[2];
			light.kind = (float)HD_OBJLIGHT_POINT;
			light.r = (float)dynamic->r;
			light.g = (float)dynamic->g;
			light.b = (float)dynamic->b;
			light.invRange = 1.0f / falloff;

			add_light(out, light);
		}

		/* --- the room's points and spots --------------------------------- */
		for (int list = 0; list < 2; list++)
		{
			PCLIGHT* source = (PCLIGHT*)lists[list];

			for (long i = 0; source && i < counts[list]; i++)
			{
				if (!source[i].Active)
					continue;

				/* A shadow light darkens the ambient and casts nothing; the
				 * engine has already taken it off aAmbient for us. */
				if (source[i].Type != LIGHT_POINT && source[i].Type != LIGHT_SPOT)
					continue;

				const bool spot = source[i].Type == LIGHT_SPOT;
				const float range = spot ? source[i].Cutoff : source[i].Outer;

				if (range <= 0.0f)
					continue;

				float view[3];

				world_to_view(source[i].x, source[i].y, source[i].z, false, view);

				light.x = view[0];
				light.y = view[1];
				light.z = view[2];
				light.kind = (float)(spot ? HD_OBJLIGHT_SPOT : HD_OBJLIGHT_POINT);
				light.r = source[i].r * 255.0f;
				light.g = source[i].g * 255.0f;
				light.b = source[i].b * 255.0f;
				light.invRange = 1.0f / range;

				add_light(out, light);
			}
		}
	}

	/* ------------------------------------------------------- the self-test -- */

	/* THE TWO PROOFS, run on the first HD item of a level and written into the
	 * log. Neither of them is a self-test of a formula: both run the ENGINE's
	 * own code on one side.
	 *
	 * itemcheck - a node's origin lands in the same pixel on both roads. The
	 *   node matrix already carries the origin into view space (its
	 *   translation IS the view position of the node), so the classic side is
	 *   the three lines of aTransformLightClipMesh and the HD side is the
	 *   projection matrix the card is handed, through the viewport rule the
	 *   card really applies.
	 *
	 * lightcheck - the shader's light arithmetic gives the classic answer. The
	 *   classic side is aTransformLightClipMesh evaluated on the engine's own
	 *   SunLights / PointLights / SpotLights, which InitObjectLighting filled
	 *   a moment ago for THIS item at node 0; the HD side is the same
	 *   arithmetic on the view-space list this file just built, with the
	 *   sample normal turned into view space by the node's own matrix. If the
	 *   two disagree, the conversion is wrong - which is the one thing in the
	 *   whole object path that cannot be seen in a picture, only measured.
	 */
	void self_test(const FrameItem& entry)
	{
		if (!gLogEnabled || g_checked)
			return;

		g_checked = true;

		/* --- the matrix ---------------------------------------------------- */
		const double width = 2.0 * (double)f_centerx;
		const double height = 2.0 * (double)f_centery;

		D3DMATRIX projection;

		HDRenderMakeProjection((double)f_persp, (double)f_centerx, (double)f_centery,
			(double)f_a, (double)f_b, &projection);

		const float* p = (const float*)&projection;
		double worst_x = 0, worst_y = 0;
		int used = 0;

		for (int node = 0; node < entry.nodes; node++)
		{
			const float* m = entry.matrix + node * 16;
			const double vx = m[12];
			const double vy = m[13];
			const double vz = m[14];

			if (vz < (double)f_mznear)
				continue;

			const double zv = (double)f_persp / vz;
			const double classic_x = vx * zv + (double)f_centerx;
			const double classic_y = vy * zv + (double)f_centery;

			const double cx = vx * p[0] + vy * p[4] + vz * p[8] + p[12];
			const double cy = vx * p[1] + vy * p[5] + vz * p[9] + p[13];
			const double cw = vx * p[3] + vy * p[7] + vz * p[11] + p[15];

			if (cw <= 0.0)
				continue;

			const double dx = fabs((cx / cw + 1.0) * 0.5 * width - classic_x);
			const double dy = fabs((1.0 - cy / cw) * 0.5 * height - classic_y);

			if (dx > worst_x) worst_x = dx;
			if (dy > worst_y) worst_y = dy;

			used++;
		}

		Log("HDLOG itemcheck level=%s slot=%d(%s) nodes=%d used=%d "
			"maxdx=%.5f maxdy=%.5f",
			g_stem, entry.slot, HDSlotName(entry.slot) ? HDSlotName(entry.slot) : "?",
			entry.nodes, used, worst_x, worst_y);

		/* --- the light ------------------------------------------------------ */
		/* One normal that is not axis aligned, so that a swapped row or a
		 * dropped sign cannot cancel itself out. */
		const float nx = 0.4243f, ny = 0.5657f, nz = 0.7071f;

		float classic[3] = { (float)aAmbientR, (float)aAmbientG, (float)aAmbientB };

		for (long i = 0; i < NumPointLights; i++)
		{
			float val = (PointLights[i].vec.x * nx + PointLights[i].vec.y * ny +
				PointLights[i].vec.z * nz + 1.0f) * 0.5f;

			val *= val;

			if (val > 0)
			{
				val *= PointLights[i].rad;
				classic[0] += val * PointLights[i].r;
				classic[1] += val * PointLights[i].g;
				classic[2] += val * PointLights[i].b;
			}
		}

		for (long i = 0; i < NumSpotLights; i++)
		{
			float val = SpotLights[i].vec.x * nx + SpotLights[i].vec.y * ny +
				SpotLights[i].vec.z * nz;

			if (val > 0)
			{
				val *= SpotLights[i].rad;
				classic[0] += val * SpotLights[i].r;
				classic[1] += val * SpotLights[i].g;
				classic[2] += val * SpotLights[i].b;
			}
		}

		for (long i = 0; i < NumSunLights; i++)
		{
			float val = SunLights[i].vec.x * nx + SunLights[i].vec.y * ny +
				SunLights[i].vec.z * nz;

			if (val > 0)
			{
				val *= InventoryActive ? 2.0f : 0.75f;
				classic[0] += val * SunLights[i].r;
				classic[1] += val * SunLights[i].g;
				classic[2] += val * SunLights[i].b;
			}
		}

		/* The same normal in view space, and the same node's view position -
		 * the two things the shader has. */
		const float* m = entry.matrix;
		const float vnx = nx * m[0] + ny * m[4] + nz * m[8];
		const float vny = nx * m[1] + ny * m[5] + nz * m[9];
		const float vnz = nx * m[2] + ny * m[6] + nz * m[10];
		const float px = m[12], py = m[13], pz = m[14];

		float hd[3] = { entry.light[0], entry.light[1], entry.light[2] };

		for (int i = 0; i < entry.lightCount; i++)
		{
			const HDObjLight& light = entry.lights[i];
			float lx = light.x, ly = light.y, lz = light.z;
			float dist = 0;

			if (light.kind >= 0.5f)
			{
				lx -= px;
				ly -= py;
				lz -= pz;
				dist = (float)sqrt(SQUARE(lx) + SQUARE(ly) + SQUARE(lz));

				if (dist > 1e-6f)
				{
					lx /= dist;
					ly /= dist;
					lz /= dist;
				}
			}

			const float ndl = vnx * lx + vny * ly + vnz * lz;
			const float rad = 1.0f - dist * light.invRange > 0.0f
				? 1.0f - dist * light.invRange : 0.0f;
			float val;

			if (light.kind < 0.5f)
				val = ndl > 0 ? ndl * (InventoryActive ? 2.0f : 0.75f) : 0.0f;
			else if (light.kind < 1.5f)
			{
				const float wrap = (ndl + 1.0f) * 0.5f;

				val = wrap * wrap * rad;
			}
			else
				val = (ndl > 0 ? ndl : 0.0f) * rad;

			hd[0] += val * light.r;
			hd[1] += val * light.g;
			hd[2] += val * light.b;
		}

		Log("HDLOG lightcheck level=%s slot=%d classic=%.1f,%.1f,%.1f "
			"hd=%.1f,%.1f,%.1f diff=%.1f,%.1f,%.1f engine=%dsun/%dpoint/%dspot "
			"hd_lights=%d ambient=%d,%d,%d",
			g_stem, entry.slot, classic[0], classic[1], classic[2],
			hd[0], hd[1], hd[2],
			hd[0] - classic[0], hd[1] - classic[1], hd[2] - classic[2],
			(int)NumSunLights, (int)NumPointLights, (int)NumSpotLights,
			entry.lightCount, (int)aAmbientR, (int)aAmbientG, (int)aAmbientB);
	}
}

/* ---------------------------------------------------------- the level -- */

void HDObjectLevelBuild(const char* installRoot, const char* stem)
{
	std::vector<HDModelSlot> wanted;
	std::vector<float> binds;
	HDModelStats stats;
	const char* why = "";

	HDObjectLevelEnd();

	for (int i = 0; i < (int)sizeof(g_stem) - 1; i++)
	{
		g_stem[i] = stem ? stem[i] : 0;

		if (!g_stem[i])
			break;
	}

	g_stem[sizeof(g_stem) - 1] = 0;

	if (!tomb5.hd_objects || !installRoot || !*installRoot)
		return;

	// The two shaders and the vertex declaration, made once and kept as long
	// as the device lives. A card that refuses them says so here, once, and
	// every object stays classic for the rest of the session.
	if (!HDModelReady() && !HDModelInit(App.dx.lpD3DDevice, &why))
	{
		Log("HDLOG level=%s objects refused reason=%s", g_stem, why);
		return;
	}

	/* The bind poses first and in one block, because HDModelLevelBegin keeps
	 * a pointer into it while it runs and a std::vector that grows moves. */
	std::vector<int> slots;

	for (int slot = 0; slot < NUMBER_OBJECTS; slot++)
	{
		if (!objects[slot].loaded || objects[slot].nmeshes <= 0)
			continue;

		if (HDSlotIsLara(slot))
			continue;	// stage S4 - and HDBindSlot would say so anyway

		slots.push_back(slot);
	}

	binds.resize(slots.size() * HD_OBJ_NODES * 3, 0.0f);

	for (size_t i = 0; i < slots.size(); i++)
	{
		const int slot = slots[i];
		const int nodes = objects[slot].nmeshes < HD_OBJ_NODES
			? objects[slot].nmeshes : HD_OBJ_NODES;

		bind_pose_of(slot, &binds[i * HD_OBJ_NODES * 3], nodes);

		HDModelSlot one;

		one.slot = slot;
		one.nodes = nodes;
		one.bind = &binds[i * HD_OBJ_NODES * 3];
		one.name = 0;
		wanted.push_back(one);
	}

	if (wanted.empty())
		return;

	const DWORD started = GetTickCount();

	if (!HDModelLevelBegin(App.dx.lpD3DDevice, installRoot, &wanted[0],
		(int)wanted.size(), false, 1, &stats, &why))
	{
		Log("HDLOG level=%s objects refused reason=%s", g_stem, why);
		return;
	}

	Log("HDLOG level=%s models ms=%d slots=%d loaded=%d (5:%d 4:%d 6:%d) "
		"heads=%d lara=%d dummy=%d missing=%d refused=%d verts=%d idx=%d "
		"ranges=%d pages=%d vb_kib=%.0f ib_kib=%.0f read_ms=%d build_ms=%d",
		g_stem, (int)(GetTickCount() - started), stats.slots, stats.loaded,
		stats.fromGame5, stats.fromGame4, stats.fromGame6, stats.heads,
		stats.lara, stats.dummy, stats.missing, stats.refused, stats.vertices,
		stats.indices, stats.ranges, stats.pages, stats.vertexBytes / 1024.0,
		stats.indexBytes / 1024.0, (int)(stats.readMs + 0.5),
		(int)(stats.buildMs + 0.5));

	for (int i = 0; i < HDModelNoteCount(); i++)
		Log("HDLOG level=%s model note: %s", g_stem, HDModelNote(i));

	/* THE VERDICT PER SLOT. One line each, so that a wrong picture in the game
	 * can be looked up rather than guessed at. */
	g_hd_slots = 0;
	g_classic_slots = 0;

	for (size_t i = 0; i < wanted.size(); i++)
	{
		const int slot = wanted[i].slot;
		Verdict& verdict = g_slot[slot];

		verdict.nodes = wanted[i].nodes;
		verdict.maxBone = HDModelMaxBone(slot);
		verdict.covered = 0;
		verdict.drift = 0;
		verdict.worst = 0;
		verdict.hd = 0;
		verdict.why = "no_model";

		if (!HDModelHas(slot))
		{
			g_classic_slots++;
			continue;
		}

		for (int node = 0; node < wanted[i].nodes; node++)
			if (HDModelNodeCovered(slot, node))
				verdict.covered |= 1u << node;

		HDModelBindDrift(slot, &verdict.drift, &verdict.worst);

		/* THE SKELETON TEST, in the order of certainty.
		 *
		 * 1. A bone number the slot does not have. The randomizer puts
		 *    foreign creatures into slots - ANDREA1's SAS holds a 25 node
		 *    tree and SAS.TRM was drawn for 15 - and a vertex that names node
		 *    20 of a 15 node tree is not this skeleton's, full stop.
		 * 2. No geometry at all. Cannot happen with a model the reader
		 *    accepted, and is a guard and not a hope.
		 * 3. The bind drift, for a foreign model with the right node count:
		 *    the vertices of a node sit around that node in the tree they
		 *    were drawn for and nowhere near it in another. */
		if (verdict.maxBone >= wanted[i].nodes)
			verdict.why = "skeleton_nodes";
		else if (!verdict.covered)
			verdict.why = "no_geometry";
		else if (verdict.drift > kDriftLimit)
			verdict.why = "skeleton_drift";
		else
		{
			verdict.hd = 1;
			verdict.why = "ok";
		}

		if (verdict.hd)
			g_hd_slots++;
		else
			g_classic_slots++;

		int covered = 0;

		for (int node = 0; node < wanted[i].nodes; node++)
			if (verdict.covered & (1u << node))
				covered++;

		Log("HDLOG slot=%d name=%s model=%s nodes=%d maxbone=%d cover=%d "
			"drift=%.0f/%.0f head=%d hd=%d reason=%s",
			slot, HDSlotName(slot) ? HDSlotName(slot) : "?",
			HDModelName(slot) ? HDModelName(slot) : "-", wanted[i].nodes,
			verdict.maxBone, covered, verdict.drift, verdict.worst,
			HDModelHasHead(slot) ? HDModelHeadNode(slot) : -1,
			(int)verdict.hd, verdict.why);
	}

	Log("HDLOG level=%s objects hd=%d classic=%d (of %d loaded slots), "
		"drift limit %.0f units",
		g_stem, g_hd_slots, g_classic_slots, (int)wanted.size(), kDriftLimit);

	g_active = g_hd_slots > 0;

	if (!g_active)
	{
		Log("HDLOG level=%s no slot draws HD - the models are released again", g_stem);
		HDModelLevelEnd();
	}
}

void HDObjectRelease()
{
	/* Device work: buffers and the models' share of the page cache. It runs
	 * on the main thread and only there, immediately before the room path
	 * empties the cache these models' page slots point into. */
	g_active = false;
	g_count = 0;
	g_open = false;
	HDModelLevelEnd();
}

void HDObjectLevelEnd()
{
	/* NOT A SINGLE DIRECT3D CALL. HDLevelUnload runs at the top of LoadLevel
	 * on the LOAD thread while the main thread presents the loading bar - the
	 * two-threads-one-device window that froze this game twice. The buffers
	 * go in HDObjectRelease, on the thread that owns the device. */
	memset(g_slot, 0, sizeof(g_slot));
	g_active = false;
	g_count = 0;
	g_dropped = 0;
	g_open = false;
	g_checked = false;
	g_items_classic = 0;
	g_clipped_lights = 0;
	g_swap_ignored = 0;
	g_mask_used = 0;
	g_draw_calls = 0;
	g_frame_logged = false;
	g_hd_slots = 0;
	g_classic_slots = 0;
}

void HDObjectDeviceLost()
{
	HDObjectRelease();
	HDObjectLevelEnd();
	HDModelShutdown();
}

bool HDObjectActive()
{
	return g_active;
}

/* ----------------------------------------------------------- the frame -- */

void HDObjectFrameReset()
{
	g_count = 0;
	g_dropped = 0;
	g_open = false;
	g_items_classic = 0;
	g_swap_ignored = 0;
	g_mask_used = 0;
}

bool HDObjectBegin(ITEM_INFO* item, OBJECT_INFO* obj)
{
	// The guard. With hd_objects off, or before a level built its models,
	// this is where DrawAnimatingItem turns round and draws what it always
	// drew.
	if (!g_active || !tomb5.hd_objects || !item || !obj)
		return false;

	const int slot = (int)item->object_number;

	if (slot < 0 || slot >= NUMBER_OBJECTS || !g_slot[slot].hd)
	{
		g_items_classic++;
		return false;
	}

	if (g_count >= HD_OBJ_ITEMS)
	{
		g_dropped++;
		return false;
	}

	// The frame's field of view, if no HD room has taken it yet - an item can
	// be the first HD thing in a frame whose rooms all stayed classic.
	HDFrameCaptureProjection();

	g_open = true;
	g_open_slot = slot;
	g_open_nodes = g_slot[slot].nodes;
	g_open_mask = (unsigned int)item->mesh_bits;
	g_seen = 0;

	/* THE MESH SWAP IS IGNORED, AND THAT IS TROne'S ANSWER TOO. A swap
	 * replaces ONE node's mesh with the pair's second pointer - a guard's
	 * hands (bits 10 and 13, set for every soldier the moment he is
	 * initialised), the cyborg's twenty damage panels. A .TRM is one whole
	 * model and carries no second geometry for a node, so there is nothing to
	 * swap TO; TROne walked into the same wall, tried drawing the swap mesh
	 * classically over the HD body and took it out again after user reports
	 * of classic patches on an HD model (game_renderer.cpp:1825-1830, where
	 * `covered` is forced to ~0u for exactly this).
	 *
	 * So the model is what the remaster drew - and what the remaster drew is
	 * the swapped look, because the remaster's own picture of a SWAT is the
	 * SWAT with his weapon. What is really lost is the cyborg's panels
	 * opening; it is counted here and named in the manual. */
	if (item->meshswap_meshbits && (unsigned int)item->meshswap_meshbits & g_slot[slot].covered)
		g_swap_ignored++;

	if (g_open_mask != 0xFFFFFFFFu)
		g_mask_used++;

	return true;
}

void HDObjectNode(int node, bool interpolated)
{
	if (!g_open || node < 0 || node >= g_open_nodes || node >= HD_OBJ_NODES)
		return;

	if (interpolated)
	{
		/* The matrix that DRAWS in the frac branch is the interpolated one,
		 * which phd_PutPolygons_I makes inside a push of its own
		 * (game\draw.cpp:296-301). The same push, the same call, the same
		 * pop - so what is recorded is the matrix the classic mesh would
		 * have been drawn with, and not the one before the blend. */
		phd_PushMatrix();
		InterpolateMatrix();
		memcpy(g_raw + node * 12, aMXPtr, 48);
		phd_PopMatrix();
	}
	else
		memcpy(g_raw + node * 12, aMXPtr, 48);

	g_seen |= 1u << node;
}

void HDObjectEnd()
{
	if (!g_open)
		return;

	g_open = false;

	if (!g_seen || g_count >= HD_OBJ_ITEMS)
		return;

	FrameItem& entry = g_frame[g_count];

	entry.slot = g_open_slot;
	entry.nodes = g_open_nodes;
	entry.mask = g_open_mask;

	/* The engine's 3x4 into the row-major 4x4 the shader wants - the same
	 * conversion SetD3DMatrix does for the fixed-function pipe, and the one
	 * stage S1-2 proved for the rooms.
	 *
	 * A node the walk never reached keeps the last one that was: TROne's
	 * fill_unseen_joints (game_renderer.cpp:1391), and the reason is that a
	 * slightly wrong limb beats a limb collapsed onto the camera. It cannot
	 * happen from DrawAnimatingItem, which walks every node - but
	 * HDObjectNode is a public call and a zero matrix is not a picture, it is
	 * a divide by zero. */
	int last = 0;

	for (int node = 0; node < entry.nodes && node < HD_OBJ_NODES; node++)
	{
		if (g_seen & (1u << node))
			last = node;

		const float* in = g_raw + last * 12;
		float* out = entry.matrix + node * 16;

		out[0] = in[M00];  out[1] = in[M10];  out[2] = in[M20];  out[3] = 0.0f;
		out[4] = in[M01];  out[5] = in[M11];  out[6] = in[M21];  out[7] = 0.0f;
		out[8] = in[M02];  out[9] = in[M12];  out[10] = in[M22]; out[11] = 0.0f;
		out[12] = in[M03]; out[13] = in[M13]; out[14] = in[M23]; out[15] = 1.0f;
	}

	/* THE LIGHT, out of the classic path's own setup.
	 *
	 * These three lines are the first three of phd_PutPolygons with the draw
	 * left out: the view matrix of the node (aSetViewMatrix, minus the
	 * SetTransform that only the fixed-function pipe needs), the reset that
	 * builds aLightMatrix and the node's world position out of it, and the
	 * setup that fills SunLights / PointLights / SpotLights and leaves the
	 * ambient in aAmbientR/G/B.
	 *
	 * Node 0 and not each node in turn, because the only per-node part of the
	 * classic answer is the light DIRECTION and the falloff, and the shader
	 * works both out from the surface point instead. The ambient really is
	 * per object: the shadow lights that lower it read `light->Range`, which
	 * CreateLightList measured at the ITEM.
	 *
	 * current_item and not `item`: CalculateObjectLighting has just set it,
	 * and for an item with a shade value it points at the static-mesh light
	 * item instead - which is the classic path's own decision and not ours. */
	SetD3DMatrix(&D3DMView, g_raw);
	SuperResetLights();
	InitObjectLighting(current_item);

	for (int node = 0; node < entry.nodes && node < HD_OBJ_NODES; node++)
	{
		entry.light[node * 3 + 0] = (float)aAmbientR;
		entry.light[node * 3 + 1] = (float)aAmbientG;
		entry.light[node * 3 + 2] = (float)aAmbientB;
	}

	fill_lights(entry, current_item);

	/* The two proofs, on the first HD item of the level and while the
	 * engine's own light arrays still hold THIS item's answer. */
	self_test(entry);

	g_count++;
}

int HDObjectFrameCount()
{
	return g_count;
}

int HDObjectDrawCalls()
{
	return g_draw_calls;
}

void HDObjectFrameDraw(const void* projection, int bulbs)
{
	if (!g_count)
		return;

	HDModelFrameBegin((const D3DMATRIX*)projection, bulbs);

	for (int i = 0; i < g_count; i++)
	{
		const FrameItem& entry = g_frame[i];
		HDModelLighting lighting;

		lighting.lights = entry.lights;
		lighting.lightCount = entry.lightCount;
		lighting.sunScale = entry.sunScale;

		HDModelDraw(entry.slot, entry.matrix, entry.nodes, entry.light,
			entry.mask, &lighting);

		/* THE SEPARATE HD FACE, right after the body and on the same
		 * matrices. VON_CROY.TRM is a body with an empty collar and
		 * VON_CROY_HEAD.TRM is the face that fills it, so the two are one
		 * object and the second is not optional. */
		HDModelDrawHead(entry.slot, entry.matrix, entry.nodes, entry.light,
			entry.mask, &lighting);
	}

	g_draw_calls = HDModelDrawCalls();

	if (gLogEnabled && !g_frame_logged)
	{
		g_frame_logged = true;
		Log("HDLOG frame objects hd=%d classic=%d dropped=%d calls=%d "
			"swap_ignored=%d meshbits=%d clipped_lights=%d(level)",
			g_count, g_items_classic, g_dropped, g_draw_calls,
			g_swap_ignored, g_mask_used, g_clipped_lights);
	}

	g_count = 0;
}

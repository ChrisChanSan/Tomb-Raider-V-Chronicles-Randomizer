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

/* The light field of a level: where the highlight of the sheen comes from.
 *
 * The sheen of a room surface (hd_room.hlsl, "The sheen") reads what lights
 * a pixel out of the pixel's WORLD position, and of nothing that is chosen
 * per draw call or per room - so two draw calls that meet on one plane, two
 * rooms that meet at a doorway and two rooms whose art lies on the same
 * plane read the same light at the same place. The field is that light,
 * worked out once when the level's HD data is built:
 *
 *   a grid over the level, one cell per floor square across and one square
 *   up (HD_GLOSS_FIELD_CELL), over the part of the rooms' boxes that a lamp
 *   can reach;
 *   per cell, the .TRG's point lights within HD_GLOSS_LAMP_REACH summed into
 *   ONE direction towards the light - the light probe's dominant direction,
 *   the weighted sum of the unit vectors towards the lamps - and how
 *   one-sided the light is (the length of that sum against the sum of the
 *   weights: 1 for one lamp, near 0 between two equal lamps on either side),
 *   and one colour, the weighted sum of the lamps' colours, bounded below one
 *   (HDGlossFieldExposure).
 *
 * Two volume textures carry it to the pixel shader, which reads them with
 * linear filtering, so the light is continuous everywhere: the direction in
 * one (biased into bytes, the one-sidedness in alpha) and the colour in the
 * other (the bounded colour, its strength in alpha). The shader forms one
 * Blinn-Phong highlight out of the two.
 *
 * No light is tied to the camera, and nothing in the field asks which room a
 * lamp hangs in: a lamp lights every cell within its reach. Whether a lamp
 * behind a wall may reach a cell is answered in the shader, not here - the
 * sheen is bounded by the surface's own baked light (hd_gloss_math.h,
 * HDGlossSheenBound), and a surface the bake left dark stays dark. TrgProbe
 * -glossfield measures how much of the field's light crosses into rooms that
 * are no neighbour of the lamp's own.
 *
 * This header includes no engine header, like hd_gloss_math.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp asserts on it without a device,
 * and TrgProbe builds a level's field with the very code the engine runs.
 * hd_render.cpp is the engine's one caller. */

#include <math.h>
#include <string.h>

#include <vector>

/* The cell: one floor square across and the same up. A finer cell up would
 * follow the direction to a lamp more closely over a floor - the direction
 * from a square under a lamp hung 2,000 units up turns by 27 degrees across
 * one square - and cost twice the memory; the filtering between two cells
 * is linear in any case. The grid's origin is a multiple of the cell, so the
 * cells' edges lie on the squares' edges, where the walls stand. */
#define HD_GLOSS_FIELD_CELL	1024.0f

/* How far a lamp reaches: six squares, over which its weight fades out
 * smoothly (HDGlossFieldFalloff). Chosen: the .TRG's point lights carry no
 * radius of their own (Trg.h, TrgGroup::PointLight). */
#define HD_GLOSS_LAMP_REACH	6144.0f

/* A lamp's colour to the field's scale. The .TRG's colours are unnormalised:
 * the largest channel of a lamp has a median of 10 over the fifteen levels'
 * 1,306 lamps with a colour, a 90th percentile of 20 and a maximum of 120
 * (ANDREA2), and a level's own median runs from 2.0 (ANDY1) to 25.0
 * (ANDREA3) (TrgProbe -glossfield). A lamp at the 90th percentile gives 1 at
 * full weight before the exposure below, 0.63 after it. Every lamp is taken
 * whatever its kind: no kind stands for one sort of place - 35 of the 41
 * medians per level and kind of a lamp's height over the floor under it lie
 * 1,100 to 3,300 units up (TrgProbe -glosslight) - and the shader's N.L keeps
 * a lamp under a floor off every surface that faces up. */
#define HD_GLOSS_LAMP_NORM	(1.0f / 20.0f)

/* The most cells a field may have: 4,194,304 cells, 32 MiB in its two
 * textures. The largest level needs 151,807 cells (RICH3, 1.16 MiB), and
 * 524,288 where the device asks for powers of two (ANDY2 and TITLE, 4 MiB;
 * TrgProbe -glossfield). A level past the limit is drawn without a field -
 * the sheen keeps its environment term and loses its highlight - and
 * GLOSSLOG's field= says so. */
#define HD_GLOSS_FIELD_CELLS_MAX	(4u << 20)

struct HDGlossLamp
{
	float pos[3];		// world units
	float rgb[3];		// the .TRG's own colour
};

/* The grid: the world position of its low corner and its cells along x, y
 * and z - the volume textures' width, height and depth. */
struct HDGlossFieldGrid
{
	float origin[3];
	int dims[3];
};

/* What a build made: the cells any lamp reached, the brightest cell's colour
 * sum before the exposure (its largest channel) and its cell, the lamps
 * used, and the cells whose light is less than one-third one-sided - the
 * places between lamps where the highlight is weak. */
struct HDGlossFieldStats
{
	int lamps;
	int lit;
	int weak;
	float brightest;
	int brightestCell[3];
};

/* A lamp's weight at squared distance d2: (1 - d2 / R^2)^2 inside the reach,
 * 0 outside it - smooth at the rim, 1 at the lamp. */
inline float HDGlossFieldFalloff(float d2)
{
	const float reach2 = HD_GLOSS_LAMP_REACH * HD_GLOSS_LAMP_REACH;

	if (!(d2 < reach2))
		return 0.0f;

	const float f = 1.0f - d2 / reach2;

	return f * f;
}

/* The colour's bound: 1 - e^-m, the textbook exposure curve - m for a small
 * m, and below one whatever many lamps sum to. Applied to the largest
 * channel; the colour keeps its hue. */
inline float HDGlossFieldExposure(float m)
{
	return m > 0.0f ? 1.0f - expf(-m) : 0.0f;
}

/* The cell a world position lies in, or false outside the grid. */
inline bool HDGlossFieldCellOf(const HDGlossFieldGrid& grid, const float p[3], int cell[3])
{
	for (int axis = 0; axis < 3; axis++)
	{
		cell[axis] = (int)floorf((p[axis] - grid.origin[axis]) / HD_GLOSS_FIELD_CELL);

		if (cell[axis] < 0 || cell[axis] >= grid.dims[axis])
			return false;
	}

	return true;
}

// The index of a cell in the build's arrays: x fastest, then y, then z.
inline size_t HDGlossFieldIndex(const HDGlossFieldGrid& grid, int x, int y, int z)
{
	return ((size_t)z * (size_t)grid.dims[1] + (size_t)y) * (size_t)grid.dims[0] + (size_t)x;
}

/* One texel of D3DFMT_A8R8G8B8 - alpha in the top byte, blue in the lowest.
 * Each value in 0..1 is rounded to a byte. */
inline unsigned int HDGlossFieldTexel(float r, float g, float b, float a)
{
	const float v[4] = { r, g, b, a };
	unsigned int byte[4];

	for (int i = 0; i < 4; i++)
	{
		const float c = v[i] < 0.0f ? 0.0f : v[i] > 1.0f ? 1.0f : v[i];

		byte[i] = (unsigned int)(c * 255.0f + 0.5f);
	}

	return (byte[3] << 24) | (byte[0] << 16) | (byte[1] << 8) | byte[2];
}

/* A direction's texel: the unit vector biased into bytes, (u + 1) / 2, and
 * the one-sidedness in alpha. The shader reads it back as texel * 2 - 1. */
inline unsigned int HDGlossFieldPackDir(const float u[3], float oneSided)
{
	return HDGlossFieldTexel((u[0] + 1.0f) * 0.5f, (u[1] + 1.0f) * 0.5f,
		(u[2] + 1.0f) * 0.5f, oneSided);
}

inline void HDGlossFieldUnpackDir(unsigned int texel, float u[3], float* oneSided)
{
	u[0] = (float)((texel >> 16) & 255u) / 255.0f * 2.0f - 1.0f;
	u[1] = (float)((texel >> 8) & 255u) / 255.0f * 2.0f - 1.0f;
	u[2] = (float)(texel & 255u) / 255.0f * 2.0f - 1.0f;

	if (oneSided)
		*oneSided = (float)(texel >> 24) / 255.0f;
}

/* The grid over a level.
 *
 * `boxes` holds `boxCount` world boxes, six floats each (min x, y, z, then
 * max x, y, z) - the rooms' own boxes. The grid covers the part of their
 * union that some lamp's reach box covers, grown to whole cells on a
 * multiple of the cell, and one cell more on every side so that the
 * filtering at the rooms' outer walls reads a cell of the field and not the
 * texture's edge. `pow2` grows every side to a power of two (a device that
 * asks for it, D3DPTEXTURECAPS_VOLUMEMAP_POW2); `maxSide` is the device's
 * MaxVolumeExtent, 0 for no limit. False when no lamp lies in a room's
 * reach, or when the grid would pass `maxSide` or HD_GLOSS_FIELD_CELLS_MAX. */
inline bool HDGlossFieldLayout(const float* boxes, int boxCount,
	const HDGlossLamp* lamps, int lampCount, bool pow2, int maxSide,
	HDGlossFieldGrid* out)
{
	float roomLo[3] = { 1e30f, 1e30f, 1e30f }, roomHi[3] = { -1e30f, -1e30f, -1e30f };
	float lampLo[3] = { 1e30f, 1e30f, 1e30f }, lampHi[3] = { -1e30f, -1e30f, -1e30f };

	memset(out, 0, sizeof(*out));

	for (int b = 0; b < boxCount; b++)
		for (int axis = 0; axis < 3; axis++)
		{
			if (boxes[b * 6 + axis] < roomLo[axis]) roomLo[axis] = boxes[b * 6 + axis];
			if (boxes[b * 6 + 3 + axis] > roomHi[axis]) roomHi[axis] = boxes[b * 6 + 3 + axis];
		}

	for (int i = 0; i < lampCount; i++)
		for (int axis = 0; axis < 3; axis++)
		{
			const float lo = lamps[i].pos[axis] - HD_GLOSS_LAMP_REACH;
			const float hi = lamps[i].pos[axis] + HD_GLOSS_LAMP_REACH;

			if (lo < lampLo[axis]) lampLo[axis] = lo;
			if (hi > lampHi[axis]) lampHi[axis] = hi;
		}

	size_t cells = 1;

	for (int axis = 0; axis < 3; axis++)
	{
		const float lo = roomLo[axis] > lampLo[axis] ? roomLo[axis] : lampLo[axis];
		const float hi = roomHi[axis] < lampHi[axis] ? roomHi[axis] : lampHi[axis];

		if (!(lo <= hi))
			return false;

		const int first = (int)floorf(lo / HD_GLOSS_FIELD_CELL) - 1;
		const int last = (int)floorf(hi / HD_GLOSS_FIELD_CELL) + 1;
		int side = last - first + 1;

		if (pow2)
		{
			int p = 1;

			while (p < side)
				p <<= 1;

			side = p;
		}

		if (maxSide > 0 && side > maxSide)
			return false;

		out->origin[axis] = (float)first * HD_GLOSS_FIELD_CELL;
		out->dims[axis] = side;
		cells *= (size_t)side;
	}

	if (cells > (size_t)HD_GLOSS_FIELD_CELLS_MAX)
	{
		memset(out, 0, sizeof(*out));
		return false;
	}

	return true;
}

/* A sight test by the rooms, for TrgProbe's measurement of what the field
 * carries through walls: a lamp lights a cell only where a room whose box
 * holds the cell's centre is `nearby` the lamp's room - within two portals
 * of it, say - or where no room's box holds the centre. `boxes` are the
 * rooms' world boxes as HDGlossFieldLayout takes them, `lampRoom` the room of
 * each lamp, and `nearby` a rooms x rooms table, `nearby[a * rooms + b]`
 * non-zero where a lamp of room b may light room a. The engine builds its
 * field without one (see the note at the top). */
struct HDGlossFieldSight
{
	const float* boxes;
	int rooms;
	const int* lampRoom;
	const unsigned char* nearby;
};

/* The field itself: two texels per cell, in HDGlossFieldIndex order, into
 * `dir` and `rgb`. Each lamp's colour is scaled by HD_GLOSS_LAMP_NORM; a cell
 * at distance d from it takes the colour times HDGlossFieldFalloff(d^2), and
 * the unit vector towards it weighted by that times the colour's mean. A
 * lamp closer to the cell's centre than one unit names no direction. `sight`
 * is null in the engine. */
inline void HDGlossFieldBuild(const HDGlossFieldGrid& grid, const HDGlossLamp* lamps,
	int lampCount, std::vector<unsigned int>& dir, std::vector<unsigned int>& rgb,
	HDGlossFieldStats* stats, const HDGlossFieldSight* sight = 0)
{
	const size_t cells = (size_t)grid.dims[0] * grid.dims[1] * grid.dims[2];
	std::vector<float> sum(cells * 7, 0.0f);	// vector x y z, weight, r g b
	const float none[3] = { 0.0f, 0.0f, 0.0f };

	memset(stats, 0, sizeof(*stats));
	dir.assign(cells, HDGlossFieldPackDir(none, 0.0f));
	rgb.assign(cells, 0u);

	if (!cells)
		return;

	const int reach = (int)ceilf(HD_GLOSS_LAMP_REACH / HD_GLOSS_FIELD_CELL);

	// The rooms whose boxes hold each cell's centre, for the sight test.
	std::vector<std::vector<int> > holders(sight ? cells : 0);

	for (int r = 0; sight && r < sight->rooms; r++)
	{
		const float* b = &sight->boxes[r * 6];
		int lo[3], hi[3];

		for (int axis = 0; axis < 3; axis++)
		{
			lo[axis] = (int)ceilf((b[axis] - grid.origin[axis]) / HD_GLOSS_FIELD_CELL - 0.5f);
			hi[axis] = (int)floorf((b[3 + axis] - grid.origin[axis]) / HD_GLOSS_FIELD_CELL - 0.5f);
			lo[axis] = lo[axis] < 0 ? 0 : lo[axis];
			hi[axis] = hi[axis] >= grid.dims[axis] ? grid.dims[axis] - 1 : hi[axis];
		}

		for (int z = lo[2]; z <= hi[2]; z++)
			for (int y = lo[1]; y <= hi[1]; y++)
				for (int x = lo[0]; x <= hi[0]; x++)
					holders[HDGlossFieldIndex(grid, x, y, z)].push_back(r);
	}

	for (int i = 0; i < lampCount; i++)
	{
		const HDGlossLamp& lamp = lamps[i];
		const float c[3] = { lamp.rgb[0] * HD_GLOSS_LAMP_NORM,
			lamp.rgb[1] * HD_GLOSS_LAMP_NORM, lamp.rgb[2] * HD_GLOSS_LAMP_NORM };
		const float mean = (c[0] + c[1] + c[2]) / 3.0f;

		if (!(mean > 0.0f))
			continue;

		int at[3];
		int lo[3], hi[3];

		for (int axis = 0; axis < 3; axis++)
		{
			at[axis] = (int)floorf((lamp.pos[axis] - grid.origin[axis]) / HD_GLOSS_FIELD_CELL);
			lo[axis] = at[axis] - reach < 0 ? 0 : at[axis] - reach;
			hi[axis] = at[axis] + reach >= grid.dims[axis] ? grid.dims[axis] - 1 : at[axis] + reach;
		}

		stats->lamps++;

		for (int z = lo[2]; z <= hi[2]; z++)
			for (int y = lo[1]; y <= hi[1]; y++)
				for (int x = lo[0]; x <= hi[0]; x++)
				{
					const float centre[3] =
					{
						grid.origin[0] + ((float)x + 0.5f) * HD_GLOSS_FIELD_CELL,
						grid.origin[1] + ((float)y + 0.5f) * HD_GLOSS_FIELD_CELL,
						grid.origin[2] + ((float)z + 0.5f) * HD_GLOSS_FIELD_CELL
					};
					const float d[3] = { lamp.pos[0] - centre[0], lamp.pos[1] - centre[1],
						lamp.pos[2] - centre[2] };
					const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
					const float f = HDGlossFieldFalloff(d2);

					if (!(f > 0.0f))
						continue;

					const size_t k = HDGlossFieldIndex(grid, x, y, z);

					if (sight && !holders[k].empty())
					{
						bool seen = false;

						for (size_t h = 0; h < holders[k].size() && !seen; h++)
							seen = sight->nearby[(size_t)holders[k][h] * (size_t)sight->rooms +
								(size_t)sight->lampRoom[i]] != 0;

						if (!seen)
							continue;
					}

					float* s = &sum[k * 7];
					const float len = sqrtf(d2);

					if (len >= 1.0f)
					{
						const float w = f * mean / len;

						s[0] += d[0] * w;
						s[1] += d[1] * w;
						s[2] += d[2] * w;
					}

					s[3] += f * mean;
					s[4] += f * c[0];
					s[5] += f * c[1];
					s[6] += f * c[2];
				}
	}

	for (int z = 0; z < grid.dims[2]; z++)
		for (int y = 0; y < grid.dims[1]; y++)
			for (int x = 0; x < grid.dims[0]; x++)
			{
				const size_t k = HDGlossFieldIndex(grid, x, y, z);
				const float* s = &sum[k * 7];
				float m = s[4] > s[5] ? s[4] : s[5];

				m = s[6] > m ? s[6] : m;

				if (!(m > 0.0f) || !(s[3] > 0.0f))
					continue;

				const float len = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
				const float oneSided = len / s[3];
				const float bounded = HDGlossFieldExposure(m) / m;
				float u[3] = { 0.0f, 0.0f, 0.0f };

				if (len > 0.0f)
					for (int axis = 0; axis < 3; axis++)
						u[axis] = s[axis] / len;

				dir[k] = HDGlossFieldPackDir(u, oneSided);
				rgb[k] = HDGlossFieldTexel(s[4] * bounded, s[5] * bounded, s[6] * bounded,
					HDGlossFieldExposure(m));

				stats->lit++;
				stats->weak += oneSided < 1.0f / 3.0f ? 1 : 0;

				if (m > stats->brightest)
				{
					stats->brightest = m;
					stats->brightestCell[0] = x;
					stats->brightestCell[1] = y;
					stats->brightestCell[2] = z;
				}
			}
}

/* The field at a world position as the pixel shader reads it: both
 * textures filtered linearly between the eight nearest cell centres, clamped
 * at the grid's edges, the direction unpacked and its filtered length kept
 * (`len`, below one where the cells around point different ways). `colour`
 * is the filtered colour; `oneSided` the filtered alpha of the direction. */
inline void HDGlossFieldSample(const HDGlossFieldGrid& grid,
	const std::vector<unsigned int>& dir, const std::vector<unsigned int>& rgb,
	const float p[3], float colour[3], float u[3], float* oneSided, float* len)
{
	float t[3];
	int i0[3], i1[3];

	for (int axis = 0; axis < 3; axis++)
	{
		const float c = (p[axis] - grid.origin[axis]) / HD_GLOSS_FIELD_CELL - 0.5f;
		const float lo = floorf(c);

		t[axis] = c - lo;
		i0[axis] = (int)lo;
		i1[axis] = i0[axis] + 1;
		i0[axis] = i0[axis] < 0 ? 0 : i0[axis] >= grid.dims[axis] ? grid.dims[axis] - 1 : i0[axis];
		i1[axis] = i1[axis] < 0 ? 0 : i1[axis] >= grid.dims[axis] ? grid.dims[axis] - 1 : i1[axis];
	}

	float d[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, c[3] = { 0.0f, 0.0f, 0.0f };

	for (int corner = 0; corner < 8; corner++)
	{
		const int x = corner & 1 ? i1[0] : i0[0];
		const int y = corner & 2 ? i1[1] : i0[1];
		const int z = corner & 4 ? i1[2] : i0[2];
		const float w = (corner & 1 ? t[0] : 1.0f - t[0]) * (corner & 2 ? t[1] : 1.0f - t[1]) *
			(corner & 4 ? t[2] : 1.0f - t[2]);
		const size_t k = HDGlossFieldIndex(grid, x, y, z);
		float v[3], a = 0.0f;

		HDGlossFieldUnpackDir(dir[k], v, &a);

		for (int axis = 0; axis < 3; axis++)
			d[axis] += w * v[axis];

		d[3] += w * a;
		c[0] += w * (float)((rgb[k] >> 16) & 255u) / 255.0f;
		c[1] += w * (float)((rgb[k] >> 8) & 255u) / 255.0f;
		c[2] += w * (float)(rgb[k] & 255u) / 255.0f;
	}

	const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

	for (int axis = 0; axis < 3; axis++)
	{
		u[axis] = l > 1e-3f ? d[axis] / l : 0.0f;
		colour[axis] = c[axis];
	}

	*oneSided = d[3];
	*len = l;
}

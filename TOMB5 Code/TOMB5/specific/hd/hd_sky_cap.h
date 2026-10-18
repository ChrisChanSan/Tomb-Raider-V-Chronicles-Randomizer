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

#include <stdint.h>
#include <vector>

#include "../../vendor/trforge/src/trf_remaster/Trg.h"

/* The sky band's caps, the arithmetic without a device (hd_render.cpp, "The
 * sky dome"). ModernTest checks it (sky_cap).
 *
 * The remaster's sky band is a sphere of 21 rings that ends 68 to 79
 * degrees under the horizon and 75 to 79 degrees over it (RoomProbe
 * -skycensus, all thirteen sky levels), so both of its poles are open: a
 * ring of 32 edges, 1,060 units out from the axis, and nothing inside it.
 * Seen from below the water in JOBY4, where the band is the whole sky, the
 * open top pole shows the frame's clear. The caps close each pole of the
 * band as part of the band itself: every edge of a triangle that lies on
 * the band's highest or lowest ring gets one triangle to the ring's centre,
 * in the ring's plane - a flat disc of 32 wedges. Each wedge carries the
 * edge's own page, colours, normal and texture row (the ring's v), its
 * centre corner the middle of the edge's u, so that the disc shows the
 * band's last texel row drawn inward: one colour
 * where the page is one colour, alpha 0 where the band's alpha test cuts
 * the row away (the sky levels with cloud sheets, whose band is clear above
 * about 27 degrees). The wedge runs its edge the other way round, so the
 * band and its caps are one closed surface facing the same way.
 *
 * Which runs: of the groups past `roomGroups` (the sky's), every pass run of
 * a part with no baked light on any corner (the band; PartIsUnlit in
 * hd_render.cpp) whose corners stand at three heights or more - a flat
 * cloud sheet of one height is no band - and whose pole ring stands at
 * least a unit off its centre (a pole that is closed already has none).
 * The wedges' indices go in right after the run's own, so the run grows and
 * stays one draw call; every run that started there or later moves up by
 * as many, so the parts still tile the index list. The centre corners are
 * appended to the vertex list. */
struct HDSkyCapCount
{
	int bands;			// pass runs that got a cap
	int poles;			// poles closed
	int triangles;		// wedges added
	int vertices;		// centre corners added
};

// Whether every corner of the run carries no baked light.
inline bool HDSkyCapUnlit(const trf::remaster::Trg& trg, uint32_t start,
	uint32_t count)
{
	for (uint32_t at = start; at < start + count; at++)
	{
		if (at >= trg.indices.size() ||
			trg.indices[at] >= trg.vertices.size())
			return false;

		const trf::remaster::TrgVertex& v = trg.vertices[trg.indices[at]];

		if (v.colour1[0] | v.colour1[1] | v.colour1[2] |
			v.colour2[0] | v.colour2[1] | v.colour2[2])
			return false;
	}

	return true;
}

/* The wedges of one pole of one run: for each triangle edge whose two
 * corners stand at height `poleY`, the edge reversed and a new centre
 * corner, appended to `tris` (three indices each) and to `added` (the
 * centre corners, their indices counted from `firstNew`). Returns the
 * wedges, 0 where the pole is closed or there is no ring. */
inline int HDSkyCapPole(const trf::remaster::Trg& trg, uint32_t start,
	uint32_t count, int16_t poleY, uint32_t firstNew,
	std::vector<uint32_t>& tris,
	std::vector<trf::remaster::TrgVertex>& added)
{
	std::vector<uint32_t> edges;

	for (uint32_t t = start; t + 2 < start + count; t += 3)
	{
		for (int k = 0; k < 3; k++)
		{
			const uint32_t a = trg.indices[t + (uint32_t)k];
			const uint32_t b = trg.indices[t + (uint32_t)((k + 1) % 3)];

			if (trg.vertices[a].y == poleY && trg.vertices[b].y == poleY)
			{
				edges.push_back(a);
				edges.push_back(b);
			}
		}
	}

	if (edges.size() < 6)
		return 0;

	int minX = 32767, maxX = -32768, minZ = 32767, maxZ = -32768;

	for (uint32_t e : edges)
	{
		const trf::remaster::TrgVertex& v = trg.vertices[e];

		minX = v.x < minX ? v.x : minX;
		maxX = v.x > maxX ? v.x : maxX;
		minZ = v.z < minZ ? v.z : minZ;
		maxZ = v.z > maxZ ? v.z : maxZ;
	}

	// The ring's centre: the middle of its box, which a regular ring has.
	const int cx = (minX + maxX) / 2;
	const int cz = (minZ + maxZ) / 2;

	if (maxX - minX < 2 && maxZ - minZ < 2)
		return 0;

	int wedges = 0;

	for (size_t e = 0; e + 1 < edges.size(); e += 2)
	{
		const trf::remaster::TrgVertex& a = trg.vertices[edges[e]];
		const trf::remaster::TrgVertex& b = trg.vertices[edges[e + 1]];
		trf::remaster::TrgVertex c = a;

		c.x = (int16_t)cx;
		c.z = (int16_t)cz;
		c.u = (uint8_t)(((int)a.u + (int)b.u) / 2);
		tris.push_back(edges[e + 1]);
		tris.push_back(edges[e]);
		tris.push_back(firstNew + (uint32_t)added.size());
		added.push_back(c);
		wedges++;
	}

	return wedges;
}

/* Both caps of every band of the sky's groups, put into `trg` in place
 * (see above). */
inline HDSkyCapCount HDSkyCapBuild(trf::remaster::Trg& trg, int roomGroups)
{
	HDSkyCapCount n = { 0, 0, 0, 0 };

	for (size_t g = roomGroups > 0 ? (size_t)roomGroups : 0;
		g < trg.groups.size(); g++)
		for (size_t p = 0; p < trg.groups[g].parts.size(); p++)
			for (int pass = 0; pass < 3; pass++)
			{
				const trf::remaster::TrgPart::Pass run =
					trg.groups[g].parts[p].pass[pass];

				if (run.count < 3 || run.start + run.count >
					trg.indices.size() ||
					!HDSkyCapUnlit(trg, run.start, run.count))
					continue;

				int16_t top = 32767, bottom = -32768;
				std::vector<int16_t> heights;

				for (uint32_t at = run.start; at < run.start + run.count;
					at++)
				{
					const int16_t y = trg.vertices[trg.indices[at]].y;
					bool seen = false;

					for (int16_t h : heights)
						seen = seen || h == y;

					if (!seen && heights.size() < 3)
						heights.push_back(y);

					top = y < top ? y : top;
					bottom = y > bottom ? y : bottom;
				}

				if (heights.size() < 3)
					continue;

				std::vector<uint32_t> tris;
				std::vector<trf::remaster::TrgVertex> added;
				const uint32_t firstNew = (uint32_t)trg.vertices.size();
				int poles = 0;

				poles += HDSkyCapPole(trg, run.start, run.count, top,
					firstNew, tris, added) ? 1 : 0;
				poles += HDSkyCapPole(trg, run.start, run.count, bottom,
					firstNew, tris, added) ? 1 : 0;

				if (tris.empty())
					continue;

				const uint32_t at = run.start + run.count;
				const uint32_t more = (uint32_t)tris.size();

				for (trf::remaster::TrgGroup& group : trg.groups)
					for (trf::remaster::TrgPart& part : group.parts)
						for (trf::remaster::TrgPart::Pass& other : part.pass)
							if (other.start >= at)
								other.start += more;

				trg.groups[g].parts[p].pass[pass].count += more;
				trg.indices.insert(trg.indices.begin() + at, tris.begin(),
					tris.end());
				trg.vertices.insert(trg.vertices.end(), added.begin(),
					added.end());
				n.bands++;
				n.poles += poles;
				n.triangles += (int)(more / 3);
				n.vertices += (int)added.size();
			}

	return n;
}

/* The game's caps (hd_render.cpp): built into each level's .TRG as it is
 * read, before the build, while HDRenderSkyCapSet leaves them on (the game
 * always; RoomProbe -skycap 0 for the picture without them). The count of
 * the last level read. */
void HDRenderSkyCapSet(bool on);
HDSkyCapCount HDRenderSkyCapLast();
void HDRenderSkyCaps(trf::remaster::Trg& trg, int numRooms);

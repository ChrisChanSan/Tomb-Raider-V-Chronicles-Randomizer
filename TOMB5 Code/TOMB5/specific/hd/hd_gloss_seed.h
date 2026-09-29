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

/* The seed of the sheen's page classes, walked over a whole .TRG - the one
 * part of hd_gloss_math.h that needs the level's geometry. hd_render.cpp
 * runs it once per level build and TrgProbe -gloss / -glossseed run the very
 * same walk, so the tool's table is the engine's.
 *
 * Every triangle of the surface passes (opaque and alpha) of every room
 * group votes through HDGlossSeedVote: an up-facing triangle on a floor
 * square within HD_GLOSS_SEED_REACH of its floor height counts for the
 * square's footstep material, under the page it wears. The glass (material
 * 14) and the water sheets (16) do not vote - neither is the floor the
 * footstep material names - and neither does a triangle that names no page
 * (layer 0). Every page the walk meets gets an entry, with its triangles
 * counted whether they vote or not; HDGlossSeedClass then turns a page's
 * votes and its triangle count into a class. */

#include "hd_gloss_math.h"
#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include <unordered_map>

struct HDGlossVotes
{
	int perFx[16];
	int tris;		// the page's surface-pass triangles in the rooms
};

// The page's seed class - HDGlossSeedClass over its votes.
inline int HDGlossSeedOf(const std::unordered_map<int, HDGlossVotes>& votes, int page)
{
	const std::unordered_map<int, HDGlossVotes>::const_iterator at = votes.find(page);

	return at == votes.end() ? HD_GLOSS_NONE :
		HDGlossSeedClass(at->second.perFx, at->second.tris);
}

inline void HDGlossSeedWalk(const trf::remaster::Trg& trg, int roomGroups,
	const HDRoomGrid* grids, int gridCount, const HDSectorFloor* sectors,
	int sectorCount, std::unordered_map<int, HDGlossVotes>& votes)
{
	votes.clear();

	if (!grids || !sectors || gridCount <= 0 || sectorCount <= 0)
		return;

	const size_t groups = trg.groups.size();

	for (size_t g = 0; g < groups && (int)g < roomGroups && (int)g < gridCount; g++)
		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
		{
			const unsigned int base = HDGlossBaseMaterial(part.material);

			if (base == 14 || base == 16)
				continue;

			for (int pass = 0; pass < 2; pass++)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned int first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex* v[3];
					bool ok = true;

					for (int k = 0; k < 3; k++)
					{
						const unsigned int index = trg.indices[first + (unsigned int)k];

						ok = ok && index < trg.vertices.size();
						v[k] = ok ? &trg.vertices[index] : 0;
					}

					if (!ok || !v[0]->layer || (size_t)v[0]->layer > trg.textures.size())
						continue;

					const int page = (int)trg.textures[v[0]->layer - 1];
					const float a[3] = { (float)v[0]->x, (float)v[0]->y, (float)v[0]->z };
					const float b[3] = { (float)v[1]->x, (float)v[1]->y, (float)v[1]->z };
					const float c[3] = { (float)v[2]->x, (float)v[2]->y, (float)v[2]->z };
					const float ny = (v[0]->normal_y() + v[1]->normal_y() +
						v[2]->normal_y()) / 3.0f;
					std::unordered_map<int, HDGlossVotes>::iterator it = votes.find(page);

					if (it == votes.end())
					{
						HDGlossVotes fresh;

						for (int k = 0; k < 16; k++)
							fresh.perFx[k] = 0;

						fresh.tris = 0;
						it = votes.insert(std::make_pair(page, fresh)).first;
					}

					it->second.tris++;
					HDGlossSeedVote(a, b, c, ny, grids[g], sectors, sectorCount,
						it->second.perFx);
				}
			}
		}
}

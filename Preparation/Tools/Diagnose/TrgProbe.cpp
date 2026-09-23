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

// TrgProbe - what a Remaster level's .TRG contains, and what its pages cost
// to decode.
//
// The HD room renderer issues one draw call per (room, pass, page). Two
// numbers decide whether that scales: how many such draw calls a level
// really needs, and how long the engine's own BC7 decoder (specific\bc7.cpp)
// takes over every page of a level with its whole mip chain. This measures
// both, over the real files, with the same code the engine runs - hd_scan.cpp
// is compiled in, not reimplemented.
//
// Usage:
//   TrgProbe <path to the game 5 folder> [level ...]
//   TrgProbe <path to the game 5 folder> -all
//   TrgProbe <path to the game 5 folder> -bc7 [threads]   decode timing only
//   TrgProbe -find [override folder]                      run the discovery
//
// -find runs the engine's own search for an installed Tomb Raider IV-VI
// Remastered (specific\hd\hd_install.cpp is compiled in): the registry
// override first, then every Steam library. It prints what it found and the
// diagnostics it would put in the log.
//
// "<path to the game 5 folder>" is the "5" inside an installed Tomb Raider
// IV-VI Remastered, the one with DATA, TEX and ITEM in it. Nothing is written
// and nothing is copied - the asset rule stands (see docs\LEGAL of TROne and
// THIRD-PARTY.md here): the Remaster's files stay where they are.
//
// The room count comes from a classic level folder handed in with -classic,
// read with the randomizer's own Tr5Level, so "groups against rooms" is
// measured and not assumed. It cannot come out of the Remaster's own DATA
// folder: the .TRC there is TRX5, a different container, and Tr5Level refuses
// it - measured, the whole table came back with 0 rooms before -classic
// existed. Without -classic the room column stays 0 and the sky column simply
// repeats the group count.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

#include "core/Tr5Level.h"
#include "hd_install.h"
#include "hd_scan.h"
#include "trf_remaster/Trg.h"

// The engine keeps these in tomb5\tomb5.cpp, filled from the registry values
// hd_install and hd_install_tr3. Here the command line fills the first - the
// tool has no registry of its own and must not write into the game's - and
// the second stays empty, because nothing this tool measures is in the I-III
// bundle.
char tomb5_hd_install[260];
char tomb5_hd_install_tr3[260];

namespace
{
	const char* const kLevels[] =
	{
		"ANDREA1", "ANDREA2", "ANDREA3", "ANDY1", "ANDY2", "ANDY3",
		"JOBY2", "JOBY3", "JOBY4", "JOBY5",
		"RICH1", "RICH2", "RICH3", "RICHCUT2", "TITLE"
	};

	std::wstring toW(const std::string& s)
	{
		std::wstring w;
		for (size_t i = 0; i < s.size(); ++i)
			w.push_back((wchar_t)(unsigned char)s[i]);
		return w;
	}

	/* The distinct (material, param) tuples of a level.
	 *
	 * Every rule the HD path takes out of a .TRG part - a cut-out threshold,
	 * an additive weight and period, a flipbook layer - is a reading of one
	 * of the four floats beside the material, and each one is argued from
	 * the distribution of those floats over the fifteen files. This prints
	 * it, so a reading can be checked against the file rather than against a
	 * comment: one line per distinct tuple, with the triangles it draws in
	 * each of the three passes and the groups it appears in.
	 *
	 * It reads the .TRG straight - no page is decoded, nothing is uploaded,
	 * so it runs in well under a second per level. */
	struct MatKey
	{
		uint32_t material;
		float p[4];

		bool operator<(const MatKey& o) const
		{
			if (material != o.material) return material < o.material;
			for (int i = 0; i < 4; i++)
				if (p[i] != o.p[i]) return p[i] < o.p[i];
			return false;
		}
	};

	struct MatCount
	{
		long long tris[3] = { 0, 0, 0 };
		int parts = 0;
		int groups = 0;
		int lastGroup = -1;
		std::vector<int> where;		// the first twelve groups, for the camera
	};

	void dumpMaterials(const std::string& root, const std::string& stem, int only)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		std::map<MatKey, MatCount> table;

		for (size_t g = 0; g < trg.groups.size(); g++)
		{
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			{
				if (only >= 0 && (int)part.material != only)
					continue;

				MatKey key;
				key.material = part.material;
				for (int i = 0; i < 4; i++)
					key.p[i] = part.param[i];

				MatCount& c = table[key];

				for (int pass = 0; pass < 3; pass++)
					c.tris[pass] += part.pass[pass].count / 3;

				c.parts++;

				if (c.lastGroup != (int)g)
				{
					c.lastGroup = (int)g;
					c.groups++;

					if (c.where.size() < 12)
						c.where.push_back((int)g);
				}
			}
		}

		for (std::map<MatKey, MatCount>::const_iterator at = table.begin();
			at != table.end(); ++at)
		{
			std::printf("%-9s mat %5u  p %8.4f %8.4f %8.4f %8.4f   "
				"tris %7lld /%7lld /%7lld  parts %4d  groups %3d :",
				stem.c_str(), at->first.material,
				at->first.p[0], at->first.p[1], at->first.p[2], at->first.p[3],
				at->second.tris[0], at->second.tris[1], at->second.tris[2],
				at->second.parts, at->second.groups);

			for (size_t i = 0; i < at->second.where.size(); i++)
				std::printf(" %d", at->second.where[i]);

			std::printf("%s\n", at->second.groups > 12 ? " ..." : "");
		}
	}

	/* The baked light at a portal.
	 *
	 * A step in the wall brightness along a straight vertical line at a
	 * room boundary can come from three places: a dynamic light the HD path
	 * culls where the classic path keeps it, the additive pass drawn in the
	 * wrong order, or the remaster's own baked light, which is per room and
	 * could simply differ on the two sides of the seam.
	 *
	 * This measures the third. Two groups and the world offset between their
	 * room origins go in; every vertex of the first that lands on a vertex of
	 * the second comes out, with what the two carry as light - colour1 +
	 * colour2, which is what the shader multiplies the texel by. A seam whose
	 * pairs agree cannot be the step; one whose pairs differ is the step, and
	 * it is the art's and not the engine's.
	 *
	 * The offset is the difference of the two classic room origins, which
	 * RoomCheck prints per room as "x .. z ..". */
	void seam(const std::string& root, const std::string& stem, int ga, int gb,
		int dx, int dy, int dz)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		if (ga < 0 || gb < 0 || ga >= (int)trg.groups.size() ||
			gb >= (int)trg.groups.size())
		{
			std::printf("%-9s group out of range (0..%d)\n", stem.c_str(),
				(int)trg.groups.size() - 1);
			return;
		}

		/* Which vertices belong to which group: a group's parts name index
		 * runs, and an index names a vertex. Both sides are gathered as
		 * "world position -> mean light", and the world position of the
		 * second side is its own plus the offset. */
		std::map<long long, std::pair<double, int> > side[2];

		for (int s = 0; s < 2; s++)
		{
			const trf::remaster::TrgGroup& g = trg.groups[(size_t)(s ? gb : ga)];

			for (const trf::remaster::TrgPart& part : g.parts)
				for (int pass = 0; pass < 3; pass++)
					for (uint32_t i = 0; i < part.pass[pass].count; i++)
					{
						const uint32_t index = trg.indices[part.pass[pass].start + i];

						if (index >= trg.vertices.size())
							continue;

						const trf::remaster::TrgVertex& v = trg.vertices[index];
						const long long key =
							((long long)((int)v.x + (s ? dx : 0) + 32768) << 34) |
							((long long)((int)v.y + (s ? dy : 0) + 32768) << 17) |
							(long long)((int)v.z + (s ? dz : 0) + 32768);
						double light = 0.0;

						for (int c = 0; c < 3; c++)
							light += ((double)v.colour1[c] + (double)v.colour2[c]) /
								255.0;

						std::pair<double, int>& at = side[s][key];
						at.first += light / 3.0;
						at.second++;
					}
		}

		int pairs = 0, over = 0;
		double worst = 0.0, total = 0.0;

		for (std::map<long long, std::pair<double, int> >::const_iterator at =
			side[1].begin(); at != side[1].end(); ++at)
		{
			std::map<long long, std::pair<double, int> >::const_iterator here =
				side[0].find(at->first);

			if (here == side[0].end())
				continue;

			const double a = here->second.first / here->second.second;
			const double b = at->second.first / at->second.second;
			const double d = a > b ? a - b : b - a;

			pairs++;
			total += d;

			/* Above 1 the HD path and the classic path part company on
			 * purpose (hd_room.hlsl, the parity block): the classic one pins
			 * its multiply at 1 and carries the rest as an add, ours goes on
			 * multiplying. A seam whose sides straddle 1 therefore steps
			 * further here than it did in the original game, and this counts
			 * those. */
			if (a > 1.0 || b > 1.0)
				over++;
			if (d > worst)
				worst = d;
		}

		std::printf("%-9s groups %d and %d, offset %d %d %d: %d shared "
			"vertex position(s), mean light difference %.4f, worst %.4f, "
			"%d pair(s) with a side above 1 "
			"(light is colour1 + colour2, 0..2)\n",
			stem.c_str(), ga, gb, dx, dy, dz, pairs,
			pairs ? total / pairs : 0.0, worst, over);
	}

	/* The horizontal surfaces of one group.
	 *
	 * A TR5 water surface is a flat quad at the room's water height, and the
	 * .TRG marks it as nothing at all. So the only way to ask "which page is
	 * our water wearing" is to find the flat surfaces geometrically and read
	 * the page off them.
	 *
	 * One line per (height, page, material, pass): the triangles that lie in
	 * that plane, their x/z box, and the layer the .TRG gave them - because
	 * layer 0 means "no page named" and the renderer then falls back to the
	 * level's first page, which is a wall texture and not water. */
	struct FlatKey
	{
		int y;
		int layer;
		uint32_t material;
		int pass;

		bool operator<(const FlatKey& o) const
		{
			if (y != o.y) return y < o.y;
			if (layer != o.layer) return layer < o.layer;
			if (material != o.material) return material < o.material;
			return pass < o.pass;
		}
	};

	struct FlatCount
	{
		long long tris = 0;
		/* World coordinates run past a short, so the sentinels have to as
		 * well - a 32767 here quietly pins every x0 of a room past the
		 * middle of the map. */
		int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
		int up = 0, down = 0;
	};

	void dumpFlat(const std::string& root, const std::string& stem, int group)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		if (group < 0 || group >= (int)trg.groups.size())
		{
			std::printf("%-9s group out of range (0..%d)\n", stem.c_str(),
				(int)trg.groups.size() - 1);
			return;
		}

		std::map<FlatKey, FlatCount> table;
		long long total = 0;

		for (const trf::remaster::TrgPart& part : trg.groups[(size_t)group].parts)
			for (int pass = 0; pass < 3; pass++)
				for (uint32_t at = 0; at + 2 < part.pass[pass].count; at += 3)
				{
					const uint32_t first = part.pass[pass].start + at;

					if (first + 2 >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex& a = trg.vertices[trg.indices[first]];
					const trf::remaster::TrgVertex& b = trg.vertices[trg.indices[first + 1]];
					const trf::remaster::TrgVertex& c = trg.vertices[trg.indices[first + 2]];

					total++;

					if (a.y != b.y || b.y != c.y)
						continue;

					FlatKey key;
					key.y = a.y;
					key.layer = a.layer;
					key.material = part.material;
					key.pass = pass;

					FlatCount& f = table[key];
					f.tris++;

					if (a.ny > 160) f.up++;
					else if (a.ny < 94) f.down++;

					const int xs[3] = { a.x, b.x, c.x };
					const int zs[3] = { a.z, b.z, c.z };

					for (int i = 0; i < 3; i++)
					{
						if (xs[i] < f.x0) f.x0 = xs[i];
						if (xs[i] > f.x1) f.x1 = xs[i];
						if (zs[i] < f.z0) f.z0 = zs[i];
						if (zs[i] > f.z1) f.z1 = zs[i];
					}
				}

		std::printf("%-9s group %d: %lld triangles, %d texture layer(s), "
			"first page %d\n", stem.c_str(), group, total,
			(int)trg.textures.size(),
			trg.textures.empty() ? -1 : (int)trg.textures[0]);
		std::printf("        y  layer   page   mat  pass   tris    x0    x1    z0    z1  up down\n");

		for (std::map<FlatKey, FlatCount>::const_iterator at = table.begin();
			at != table.end(); ++at)
		{
			const int layer = at->first.layer;
			const int page = (layer && (size_t)layer <= trg.textures.size())
				? (int)trg.textures[layer - 1]
				: (trg.textures.empty() ? -1 : (int)trg.textures[0]);

			std::printf("%9d %6d %6d%s %5u %5d %6lld %5d %5d %5d %5d %4d %4d\n",
				at->first.y, layer, page, layer ? " " : "*",
				at->first.material, at->first.pass, at->second.tris,
				at->second.x0, at->second.x1, at->second.z0, at->second.z1,
				at->second.up, at->second.down);
		}

		std::printf("        (* = layer 0, no page named - the renderer falls back "
			"to the level's first page)\n");
	}

	/* Every part of one group: material, parameters, the page its first
	 * triangle wears, the triangles per pass and the room-local box each
	 * pass covers. The -mat table says what a level's materials are; this
	 * says where in one room they sit. */
	void dumpParts(const std::string& root, const std::string& stem, int group)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		if (group < 0 || group >= (int)trg.groups.size())
		{
			std::printf("%-9s group out of range (0..%d)\n", stem.c_str(),
				(int)trg.groups.size() - 1);
			return;
		}

		std::printf("%-9s group %d: %d part(s)\n", stem.c_str(), group,
			(int)trg.groups[(size_t)group].parts.size());
		std::printf(" part   mat      p0       p1       p2       p3 pass   tris  page"
			"     x0    x1    y0    y1    z0    z1\n");

		int nr = 0;

		for (const trf::remaster::TrgPart& part : trg.groups[(size_t)group].parts)
		{
			for (int pass = 0; pass < 3; pass++)
			{
				if (!part.pass[pass].count)
					continue;

				int box[6] = { 1 << 30, -(1 << 30), 1 << 30, -(1 << 30), 1 << 30, -(1 << 30) };
				int page = -1;

				for (uint32_t i = 0; i < part.pass[pass].count; i++)
				{
					const uint32_t index = part.pass[pass].start + i;

					if (index >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex& v = trg.vertices[trg.indices[index]];

					if (page < 0 && v.layer && (size_t)v.layer <= trg.textures.size())
						page = (int)trg.textures[v.layer - 1];

					if (v.x < box[0]) box[0] = v.x;
					if (v.x > box[1]) box[1] = v.x;
					if (v.y < box[2]) box[2] = v.y;
					if (v.y > box[3]) box[3] = v.y;
					if (v.z < box[4]) box[4] = v.z;
					if (v.z > box[5]) box[5] = v.z;
				}

				std::printf("%5d %5u %8.4f %8.4f %8.4f %8.4f %4d %6u %5d %6d %5d %5d %5d %5d %5d",
					nr, part.material, part.param[0], part.param[1],
					part.param[2], part.param[3], pass,
					part.pass[pass].count / 3, page,
					box[0], box[1], box[2], box[3], box[4], box[5]);

				/* Every page the pass touches, not only its first. A part is
				 * one material, not one sheet: the glass wall of ANDREA2 wears
				 * four. */
				{
					std::map<int, long long> pages;

					for (uint32_t i = 0; i < part.pass[pass].count; i += 3)
					{
						const uint32_t index = part.pass[pass].start + i;

						if (index >= trg.indices.size())
							break;

						const trf::remaster::TrgVertex& v = trg.vertices[trg.indices[index]];

						if (v.layer && (size_t)v.layer <= trg.textures.size())
							pages[(int)trg.textures[v.layer - 1]]++;
					}

					std::printf("  pages:");

					int shown = 0;

					for (std::map<int, long long>::const_iterator at = pages.begin();
						at != pages.end() && shown < 10; ++at, ++shown)
						std::printf(" %d(%lld)", at->first, at->second);

					std::printf("%s\n", pages.size() > 10 ? " ..." : "");
				}
			}

			nr++;
		}
	}

	/* How much of a level is on layer 0. Per level: vertices with layer 0,
	 * triangles whose first corner carries it - which is the corner
	 * hd_render.cpp reads - and the material those triangles belong to. The
	 * renderer counts the triangles as fallbackPageTris; this says which
	 * materials and which groups they are, without a device. */
	void dumpLayer0(const std::string& root, const std::string& stem)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		long long zero_vertices = 0;

		for (size_t v = 0; v < trg.vertices.size(); v++)
			if (!trg.vertices[v].layer)
				zero_vertices++;

		std::map<uint32_t, long long> by_material;
		std::map<int, long long> by_group;
		long long tris = 0, total = 0;

		for (size_t g = 0; g < trg.groups.size(); g++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
					for (uint32_t at = 0; at + 2 < part.pass[pass].count; at += 3)
					{
						const uint32_t first = part.pass[pass].start + at;

						if (first >= trg.indices.size())
							break;

						total++;

						if (trg.vertices[trg.indices[first]].layer)
							continue;

						tris++;
						by_material[part.material]++;
						by_group[(int)g]++;
					}

		std::printf("%-9s vertices %lld of %lld on layer 0, triangles %lld of %lld, "
			"groups %d, first page %d\n", stem.c_str(), zero_vertices,
			(long long)trg.vertices.size(), tris, total, (int)by_group.size(),
			trg.textures.empty() ? -1 : (int)trg.textures[0]);

		for (std::map<uint32_t, long long>::const_iterator at = by_material.begin();
			at != by_material.end(); ++at)
			std::printf("          material %5u : %lld triangle(s)\n",
				at->first, at->second);

		int shown = 0;

		for (std::map<int, long long>::const_iterator at = by_group.begin();
			at != by_group.end() && shown < 12; ++at, ++shown)
			std::printf("          group %4d : %lld triangle(s)\n",
				at->first, at->second);
	}

	/* One material, part by part, against the classic room.
	 *
	 * Two trees read material 16 two ways: ours as an additive sheet whose
	 * weight sits in param[2] (hd_render.cpp, MaterialAddWeight), TROne's as
	 * the still-water surface with param[0..2] as noise scale, distortion
	 * and time scale. Neither reading was ever held against the geometry,
	 * and the geometry is the one thing that can decide it. A water surface
	 * is one horizontal plane; it sits at the height the classic room draws
	 * its water at; its normals point up; and it is tessellated far finer
	 * than a wall needs to be, because a wave has to be able to move its
	 * corners. A light shaft is none of those things.
	 *
	 * So this prints, per part of the material: the pass the file sorts it
	 * into, the triangle count, the y span and how many distinct heights are
	 * in it, the world box, the normal directions, the pages, the sway, the
	 * two baked colours, the UV range, and whether a single one of its
	 * vertices is shared with a part of another material. With -classic it
	 * adds the room flags and every horizontal plane the classic room draws,
	 * so "is the sheet in the water plane" is a subtraction and not an
	 * impression - the water plane being the room's lowest horizontal one,
	 * which is what drawroom.cpp compares against as r->minfloor.
	 */
	struct M16Pass
	{
		long long tris = 0;
		long long verts = 0;
		int y0 = 1 << 30, y1 = -(1 << 30);
		std::map<int, long long> yTris;
		int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
		int sway0 = 1 << 30, sway1 = -(1 << 30);
		long long up = 0, down = 0, side = 0;
		long long swayNonZero = 0;
		long long c1sum = 0, c2sum = 0;
		int c1lo = 255, c1hi = 0, c2lo = 255, c2hi = 0;
		int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
		std::map<int, long long> pages;
		long long shared = 0;
		std::set<unsigned int> vset;
	};

	void dumpMat16(const std::string& root, const std::string& stem,
		const std::string& folder, int wanted)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		/* Which materials touch which vertex, over the whole file: the
		 * "never shares a vertex with another material" claim is a fact
		 * about the index buffer and nothing else, so it is read there. */
		std::vector<unsigned char> firstMat(trg.vertices.size(), 0xFF);
		std::vector<unsigned char> mixed(trg.vertices.size(), 0);

		for (size_t g = 0; g < trg.groups.size(); g++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			{
				const unsigned char base = (unsigned char)(part.material & 0xFFu);

				for (int pass = 0; pass < 3; pass++)
					for (uint32_t i = 0; i < part.pass[pass].count; i++)
					{
						const uint32_t index = part.pass[pass].start + i;

						if (index >= trg.indices.size())
							break;

						const uint32_t v = trg.indices[index];

						if (v >= firstMat.size())
							continue;

						if (firstMat[v] == 0xFF)
							firstMat[v] = base;
						else if (firstMat[v] != base)
							mixed[v] = 1;
					}
			}

		Tr5Level level;
		bool haveClassic = false;

		if (!folder.empty())
		{
			const std::string trc = folder + "\\" + stem + ".TRC";
			haveClassic = level.load(toW(trc), error);

			if (!haveClassic)
				std::printf("%-9s classic level not loadable: %s\n",
					stem.c_str(), error.c_str());
		}

		long long total[3] = { 0, 0, 0 };
		int partCount = 0;

		for (size_t g = 0; g < trg.groups.size(); g++)
		{
			int nr = 0;

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			{
				const int base = (int)(part.material & 0xFFu);
				const int here = nr++;

				if (base != wanted)
					continue;

				for (int pass = 0; pass < 3; pass++)
				{
					if (!part.pass[pass].count)
						continue;

					M16Pass s;

					for (uint32_t i = 0; i < part.pass[pass].count; i++)
					{
						const uint32_t index = part.pass[pass].start + i;

						if (index >= trg.indices.size())
							break;

						const uint32_t vi = trg.indices[index];

						if (vi >= trg.vertices.size())
							continue;

						const trf::remaster::TrgVertex& v = trg.vertices[vi];

						s.vset.insert(vi);

						if (vi < mixed.size() && mixed[vi])
							s.shared++;

						if (v.x < s.x0) s.x0 = v.x;
						if (v.x > s.x1) s.x1 = v.x;
						if (v.y < s.y0) s.y0 = v.y;
						if (v.y > s.y1) s.y1 = v.y;
						if (v.z < s.z0) s.z0 = v.z;
						if (v.z > s.z1) s.z1 = v.z;
						if (v.sway < s.sway0) s.sway0 = v.sway;
						if (v.sway > s.sway1) s.sway1 = v.sway;
						if (v.sway) s.swayNonZero++;

						const int c1 = ((int)v.colour1[0] + v.colour1[1] + v.colour1[2]) / 3;
						const int c2 = ((int)v.colour2[0] + v.colour2[1] + v.colour2[2]) / 3;

						s.c1sum += c1;
						s.c2sum += c2;

						if (c1 < s.c1lo) s.c1lo = c1;
						if (c1 > s.c1hi) s.c1hi = c1;
						if (c2 < s.c2lo) s.c2lo = c2;
						if (c2 > s.c2hi) s.c2hi = c2;

						if (v.u < s.u0) s.u0 = v.u;
						if (v.u > s.u1) s.u1 = v.u;
						if (v.v < s.v0) s.v0 = v.v;
						if (v.v > s.v1) s.v1 = v.v;

						if (v.ny > 160) s.up++;
						else if (v.ny < 94) s.down++;
						else s.side++;

						s.verts++;

						if (v.layer && (size_t)v.layer <= trg.textures.size())
							s.pages[(int)trg.textures[v.layer - 1]]++;
						else
							s.pages[-1]++;
					}

					for (uint32_t at = 0; at + 2 < part.pass[pass].count; at += 3)
					{
						const uint32_t first = part.pass[pass].start + at;

						if (first + 2 >= trg.indices.size())
							break;

						const trf::remaster::TrgVertex& a = trg.vertices[trg.indices[first]];
						const trf::remaster::TrgVertex& b = trg.vertices[trg.indices[first + 1]];
						const trf::remaster::TrgVertex& c = trg.vertices[trg.indices[first + 2]];

						s.tris++;

						if (a.y == b.y && b.y == c.y)
							s.yTris[(int)a.y]++;
						else
							s.yTris[1 << 29]++;	/* not horizontal */
					}

					total[pass] += s.tris;
					partCount++;

					int ox = 0, oz = 0;
					unsigned flags = 0;
					bool haveRoom = false;

					if (haveClassic && (int)g < level.roomCount())
					{
						const Tr5Room& rm = level.rooms()[g];
						ox = rm.originX;
						oz = rm.originZ;
						flags = rm.flags;
						haveRoom = true;
					}

					std::printf("%-9s grp %4d part %3d pass %d  mat %5u  "
						"p %8.4f %8.4f %8.4f %8.4f  tris %7lld  verts %6lld\n",
						stem.c_str(), (int)g, here, pass, part.material,
						part.param[0], part.param[1], part.param[2],
						part.param[3], s.tris, (long long)s.vset.size());

					std::printf("            y %6d..%-6d  x %6d..%-6d  z %6d..%-6d"
						"  (world x %6d..%-6d z %6d..%-6d)\n",
						s.y0, s.y1, s.x0, s.x1, s.z0, s.z1,
						s.x0 + ox, s.x1 + ox, s.z0 + oz, s.z1 + oz);

					{
						const long long flat = s.yTris.count(1 << 29)
							? s.tris - s.yTris[1 << 29] : s.tris;
						const int planes = (int)s.yTris.size()
							- (s.yTris.count(1 << 29) ? 1 : 0);

						std::printf("            horizontal %lld of %lld triangle(s) "
							"in %d plane(s):", flat, s.tris, planes);

						int shown = 0;

						for (std::map<int, long long>::const_iterator at = s.yTris.begin();
							at != s.yTris.end() && shown < 8; ++at)
						{
							if (at->first == (1 << 29))
								continue;

							std::printf(" %d(%lld)", at->first, at->second);
							shown++;
						}

						std::printf("%s\n", planes > 8 ? " ..." : "");
					}

					/* Normals are named the way a player sees them. The .TRG's
					 * y counts downwards, as the classic level's does, so a
					 * normal whose y byte is above 127 points down in the
					 * world - it is the underside of a surface, the face an
					 * underwater camera sees. Measured on a floor: every one
					 * of the 604 horizontal triangles in ANDREA1 group 59's
					 * street plane (y = 256) has ny < 94. */
					std::printf("            normals facing up %lld / down %lld / sideways %lld"
						"   sway %d..%d (%lld non-zero)   shared verts with another material %lld of %lld\n",
						s.down, s.up, s.side, s.sway0, s.sway1, s.swayNonZero,
						s.shared, s.verts);

					std::printf("            colour1 %d..%d mean %lld   colour2 %d..%d mean %lld"
						"   u %d..%d  v %d..%d\n",
						s.c1lo, s.c1hi, s.verts ? s.c1sum / s.verts : 0,
						s.c2lo, s.c2hi, s.verts ? s.c2sum / s.verts : 0,
						s.u0, s.u1, s.v0, s.v1);

					std::printf("            pages:");

					{
						int shown = 0;

						for (std::map<int, long long>::const_iterator at = s.pages.begin();
							at != s.pages.end() && shown < 10; ++at, ++shown)
							std::printf(" %d(%lld)", at->first, at->second);

						std::printf("%s\n", s.pages.size() > 10 ? " ..." : "");
					}

					/* The sector area and the tessellation. A plain remesh
					 * gives two triangles per floor square; a wave grid gives
					 * hundreds. */
					{
						const long long sx = ((long long)(s.x1 - s.x0) + 1023) / 1024;
						const long long sz = ((long long)(s.z1 - s.z0) + 1023) / 1024;
						const long long squares = (sx > 0 ? sx : 1) * (sz > 0 ? sz : 1);

						std::printf("            box %lld x %lld square(s) = %lld,"
							" %.1f triangle(s) per square\n",
							sx, sz, squares, (double)s.tris / (double)squares);
					}

					if (haveRoom)
					{
						std::vector<Tr5Level::FaceInfo> faces;
						std::map<int, int> rows;

						if (level.roomFaces((int)g, faces, error))
							for (size_t f = 0; f < faces.size(); f++)
							{
								const int corners = faces[f].quad ? 4 : 3;
								bool flat = true;

								for (int c = 1; c < corners; c++)
									if (faces[f].v[c][1] != faces[f].v[0][1])
										flat = false;

								if (flat)
									rows[(int)faces[f].v[0][1]]++;
							}

						std::printf("            classic room %d flags 0x%04X%s%s%s%s"
							"  yBottom %d  horizontal planes:", (int)g, flags,
							(flags & 0x0001) ? " UNDERWATER" : "",
							(flags & 0x0100) ? " MIST" : "",
							(flags & 0x0200) ? " REFLECT" : "",
							(flags & 0x0008) ? " OUTSIDE" : "",
							level.rooms()[g].yBottom);

						int shown = 0;

						for (std::map<int, int>::const_iterator at = rows.begin();
							at != rows.end() && shown < 10; ++at, ++shown)
							std::printf(" %d(%d)%s", at->first, at->second,
								(at->first == s.y0 && s.y0 == s.y1) ? "<<" : "");

						std::printf("%s\n", rows.size() > 10 ? " ..." : "");

						/* The classic water plane of that room: its lowest
						 * horizontal one, which is drawroom.cpp's minfloor. */
						if (!rows.empty())
						{
							const int minfloor = rows.rbegin()->first;
							const int d = s.y0 - minfloor;

							std::printf("            classic minfloor %d, "
								"the sheet is %d unit(s) %s it\n", minfloor,
								d >= 0 ? d : -d,
								s.y0 == minfloor ? "ON" : (s.y0 < minfloor ? "ABOVE" : "BELOW"));
						}

						/* And the rooms next door, because a still-water
						 * surface belongs to two rooms at once. */
						std::printf("            portals to:");

						for (size_t p = 0; p < level.rooms()[g].portalRooms.size() && p < 12; p++)
						{
							const int other = level.rooms()[g].portalRooms[p];

							if (other < 0 || other >= level.roomCount())
								continue;

							std::printf(" %d(0x%X)", other,
								level.rooms()[(size_t)other].flags);
						}

						std::printf("\n");
					}
				}
			}
		}

		std::printf("%-9s material %d: %d part/pass block(s), triangles "
			"opaque %lld / alpha %lld / additive %lld\n\n",
			stem.c_str(), wanted, partCount, total[0], total[1], total[2]);
	}

	/* Which rooms stand inside one another, and which art leaves its room.
	 *
	 * The renderer scissors every HD room to the portal window the classic
	 * walk gave it. That window is correct for geometry that stays inside
	 * the room's own volume - which classic geometry always does, because a
	 * classic room is its sectors - and wrong for the remaster's, which
	 * carves reliefs, cornices and pillars that stand out past the room's
	 * outer wall. Both halves of that measurement are computed from the same
	 * two boxes the engine has:
	 *
	 *   the full box      x .. x + numX * 1024, z .. z + numZ * 1024,
	 *                     maxceiling .. minfloor - the room including its
	 *                     border ring of wall sectors.
	 *   the inner box     the engine's own, game\draw.cpp:
	 *                     x + 1024 .. x + (numX - 1) * 1024, likewise z,
	 *                     the same y. It is the box TR5 itself asks "does
	 *                     this item fit wholly inside the room" with, and
	 *                     two rooms that are only neighbours never overlap
	 *                     in it - their border rings are what they share.
	 *
	 * The overlap list is asked of the inner boxes, so a portal neighbour is
	 * not on it and a room standing bodily inside another one is. The
	 * overhang count is asked of the full box, because a wall's own art
	 * belongs to its room and only what leaves the room's footprint is
	 * overhang. */
	struct RoomSpace
	{
		long x0, x1, y0, y1, z0, z1;		// full box, world
		long ix0, ix1, iz0, iz1;		// inner box, world (y as above)
		long ox, oy, oz;			// the origin the .TRG rides on
		int numX, numZ;
		int meshes;
	};

	long boxOverlap(long a0, long a1, long b0, long b1)
	{
		const long lo = a0 > b0 ? a0 : b0;
		const long hi = a1 < b1 ? a1 : b1;

		return hi > lo ? hi - lo : 0;
	}

	void dumpRoomSpace(const std::string& root, const std::string& stem,
		const std::string& folder, int focus, bool quiet, int shareMin,
		long slack, bool staticList, int staticSlot, long deep,
		const long* nearAt, long nearRadius)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = folder + "\\" + stem + ".TRC";
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;

		if (!level.load(toW(trc), error))
		{
			std::printf("%-9s classic level not loadable: %s\n", stem.c_str(),
				error.c_str());
			return;
		}

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s .TRG FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		const int rooms = level.roomCount();
		std::vector<RoomSpace> box((size_t)rooms);

		for (int r = 0; r < rooms; r++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)r];
			RoomSpace& b = box[(size_t)r];
			unsigned yTop = 0, yOrigin = 0, lightsMeshes = 0;

			level.headerField(r, 24, yOrigin, error);
			level.headerField(r, 36, yTop, error);
			level.headerField(r, 48, lightsMeshes, error);

			b.numX = rm.numXSectors;
			b.numZ = rm.numZSectors;
			b.ox = rm.originX;
			b.oy = (long)(int)yOrigin;
			b.oz = rm.originZ;
			b.meshes = (int)(lightsMeshes >> 16);
			b.x0 = rm.originX;
			b.x1 = rm.originX + (long)rm.numXSectors * 1024;
			b.z0 = rm.originZ;
			b.z1 = rm.originZ + (long)rm.numZSectors * 1024;
			b.y0 = (long)(int)yTop;			// maxceiling, the smaller y
			b.y1 = rm.yBottom;			// minfloor
			b.ix0 = b.x0 + 1024;
			b.ix1 = b.x0 + (long)(rm.numXSectors - 1) * 1024;
			b.iz0 = b.z0 + 1024;
			b.iz1 = b.z0 + (long)(rm.numZSectors - 1) * 1024;
		}

		/* The overlapping pairs, asked of the art and not of the boxes.
		 *
		 * A box test cannot answer this. Two rooms next door reach one sector
		 * into each other by construction - the wall ring of each stands in
		 * the other's footprint, which is how TR5 builds every doorway - and
		 * an L-shaped room's box covers whatever stands in the notch. Asked
		 * of the boxes, 233 of ANDREA1's 242 rooms come out "overlapping",
		 * which is no answer at all.
		 *
		 * So the question is asked of the geometry that is actually drawn:
		 * every group's own triangles are dropped into 256-unit world cells -
		 * a quarter of a floor square on a side, the same grid
		 * HDRenderGroupHasGeometry already builds per group - and two rooms
		 * overlap when their art shares cells. Art in the same place is
		 * exactly the thing the depth buffer cannot sort out and the thing
		 * the portal window exists to hide.
		 *
		 * A flipmap pair is left out: the two are the same room at two
		 * moments and are never both drawn. */
		std::vector<std::vector<unsigned long long> > cells(trg.groups.size());
		std::vector<std::vector<unsigned long long> > shell(trg.groups.size());

		for (size_t g = 0; g < trg.groups.size() && (int)g < rooms; g++)
		{
			const RoomSpace& b = box[g];
			std::vector<unsigned long long>& out = cells[g];
			std::vector<unsigned long long>& outShell = shell[g];

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						/* The overhang is not asked about. A triangle whose
						 * centroid stands outside its own room's box is art
						 * the remaster hung on the outside - a facade, a
						 * cornice, a whole street front - and it is drawn
						 * without a window by the fix below. So it must not
						 * put its room on the overlap list either, or Rome's
						 * facades would window half the level. */
						long qx = 0, qy = 0, qz = 0;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];

							qx += v.x;
							qy += v.y;
							qz += v.z;
						}

						qx = b.ox + qx / 3;
						qy = b.oy + qy / 3;
						qz = b.oz + qz / 3;

						/* The same slack the renderer tags a triangle with
						 * (hd_render.cpp, HD_OVERHANG_SLACK): a wall face
						 * sits exactly on the box's edge and its relief a
						 * little past it, and both belong to the room. */
						const bool interior =
							qx >= b.ix0 - slack && qx <= b.ix1 + slack &&
							qy >= b.y0 - slack && qy <= b.y1 + slack &&
							qz >= b.iz0 - slack && qz <= b.iz1 + slack;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];
							const unsigned long long cx =
								(unsigned long long)(((b.ox + v.x) >> 8) + 32768);
							const unsigned long long cy =
								(unsigned long long)(((b.oy + v.y) >> 8) + 32768);
							const unsigned long long cz =
								(unsigned long long)(((b.oz + v.z) >> 8) + 32768);
							const unsigned long long key =
								(cx << 34) | (cy << 17) | cz;

							if (interior)
								out.push_back(key);
							else
								outShell.push_back(key);
						}
					}
				}

			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
			std::sort(outShell.begin(), outShell.end());
			outShell.erase(std::unique(outShell.begin(), outShell.end()),
				outShell.end());
		}

		/* Who is next door. A room's own portal list, plus the vertical links
		 * out of the floor data - a stacked pair is joined through those and
		 * has no portal record at all. Both directions, because a portal is
		 * written on both sides but a floor link need not be. */
		std::vector<std::set<int> > nextDoor((size_t)rooms);

		for (int r = 0; r < rooms; r++)
		{
			const std::vector<int>& ports = level.rooms()[(size_t)r].portalRooms;

			for (size_t p = 0; p < ports.size(); p++)
				if (ports[p] >= 0 && ports[p] < rooms)
				{
					nextDoor[(size_t)r].insert(ports[p]);
					nextDoor[(size_t)ports[p]].insert(r);
				}

			std::vector<Tr5Level::SectorInfo> sect;

			if (!level.sectorInfo(r, sect, error))
				continue;

			for (size_t s = 0; s < sect.size(); s++)
			{
				const int up = sect[s].roomAbove, dn = sect[s].roomBelow;

				if (up >= 0 && up < rooms)
				{
					nextDoor[(size_t)r].insert(up);
					nextDoor[(size_t)up].insert(r);
				}

				if (dn >= 0 && dn < rooms)
				{
					nextDoor[(size_t)r].insert(dn);
					nextDoor[(size_t)dn].insert(r);
				}
			}
		}

		/* The test itself: does a triangle of room b stand inside the inside
		 * of room a, with no portal between the two that would explain it?
		 *
		 * The answer is asked of a point - the triangle's centroid - because
		 * a cell-based test overcounts: counting a 256-unit cell that merely
		 * touches the box (`p + 255 < mins` is the reject in
		 * HDRenderGroupCellsIn) gives one ring of 256 units as 91 cells
		 * against 45 in ANDREA1's air shaft, and four pairs against one over
		 * the level. The centroid test agrees exactly with the same box
		 * BuildRoomIntrusions (specific\hd\hd_level.cpp) hands the renderer:
		 * TR5's own inside of a room, one sector further in on every side
		 * because a room's border ring is one sector thick.
		 *
		 * A neighbour's art reaches into A as well - the reveal of its own
		 * doorway does - and that is not the bug: it is seen through the
		 * opening it belongs to. The portal list and the floor data's
		 * vertical links say who is next door.
		 *
		 * Only the intruder is windowed: windowing both rooms of an
		 * overlapping pair puts a black band across ANDREA2's courtyard,
		 * where 134 and 203 are both holders and both would be scissored to
		 * rectangles that meet along their shared boundary. */
		/* Room local and in floats, because that is what the engine holds
		 * them in: hd_render.cpp averages the three .TRG vertices as floats
		 * and compares them with a float box in the group's own frame. An
		 * integer divide in world units disagrees with that by one triangle
		 * in ANDREA1's 207/53 and by seven in ANDREA2's 58/175 - a
		 * truncation against a box edge - so this averages in floats to
		 * agree with the engine exactly. */
		std::vector<std::vector<float> > centre(trg.groups.size());

		for (size_t g = 0; g < trg.groups.size() && (int)g < rooms; g++)
		{
			std::vector<float>& out = centre[g];

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						long qx = 0, qy = 0, qz = 0;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];

							qx += v.x;
							qy += v.y;
							qz += v.z;
						}

						out.push_back((float)qx / 3.0f);
						out.push_back((float)qy / 3.0f);
						out.push_back((float)qz / 3.0f);
					}
				}
		}

		int pairs = 0;
		long long intruding = 0;
		std::vector<unsigned char> windowed((size_t)rooms, 0);
		/* One flag per triangle and not a sum over the pairs: a triangle can
		 * stand inside two holders at once - ANDY2 is full of them - and the
		 * engine's own counter (HDGroup::intruding) counts the TRIANGLE once
		 * however many boxes caught it. Summing the per-pair hits instead read
		 * 70,934 for ANDY2 against the engine's 59,766, and the two lists are
		 * only the same list if they are counted the same way. */
		std::vector<std::vector<unsigned char> > tagged(trg.groups.size());

		for (size_t g = 0; g < trg.groups.size(); g++)
			tagged[g].assign(centre[g].size() / 3, 0);

		for (int a = 0; a < rooms; a++)
			for (int b = 0; b < rooms && (size_t)b < centre.size(); b++)
			{
				if (a == b)
					continue;

				if (level.rooms()[(size_t)a].altRoom == b ||
					level.rooms()[(size_t)b].altRoom == a)
					continue;

				if (nextDoor[(size_t)a].count(b))
					continue;

				const RoomSpace& A = box[(size_t)a];

				if (!boxOverlap(A.x0, A.x1, box[(size_t)b].x0, box[(size_t)b].x1) ||
					!boxOverlap(A.z0, A.z1, box[(size_t)b].z0, box[(size_t)b].z1) ||
					!boxOverlap(A.y0, A.y1, box[(size_t)b].y0, box[(size_t)b].y1))
					continue;

				/* A's inside, one sector in from its border ring, and that
				 * second sector is the whole of the rule's discrimination.
				 *
				 * A room's border ring is one sector thick, so two rooms that
				 * merely share a wall with no door between them reach into
				 * each other by construction. Asked with the ring counted
				 * (-deep -1024) ANDREA1 comes out with 158 pairs, among them
				 * 2 and 197 at the Piazza dell'Opera. Asked one sector
				 * further in it has seven.
				 *
				 * Rejected: deciding the pair with this box and then
				 * windowing every triangle of b inside a's plain inside. It
				 * would have hidden the last 26 % of ANDREA1's 207/53 bleed
				 * (see the renders) - and it windows 7,250 triangles of room
				 * 103 inside room 65, which is that pair's shared wall, seen
				 * through the wrong side of the same overlap this rule is
				 * meant to catch. What is left un-hidden is art of the
				 * intruder standing within one sector of the holder's wall,
				 * and there is no way to tell that from a shared wall with a
				 * box.
				 *
				 * `deep` is an extra shrink for re-measuring the rule and is
				 * 0 by default. */
				const RoomSpace& B = box[(size_t)b];
				const float x0 = (float)(A.ix0 + 1024 + deep - B.ox);
				const float x1 = (float)(A.ix1 - 1024 - deep - B.ox);
				const float z0 = (float)(A.iz0 + 1024 + deep - B.oz);
				const float z1 = (float)(A.iz1 - 1024 - deep - B.oz);
				const float y0 = (float)(A.y0 - B.oy);
				const float y1 = (float)(A.y1 - B.oy);

				if (x1 <= x0 || z1 <= z0)
					continue;	// a room too small to have an inside

				int hits = 0;

				for (size_t c = 0; c + 2 < centre[(size_t)b].size(); c += 3)
				{
					const float cx = centre[(size_t)b][c];
					const float cy = centre[(size_t)b][c + 1];
					const float cz = centre[(size_t)b][c + 2];

					if (cx >= x0 && cx <= x1 && cz >= z0 && cz <= z1 &&
						cy >= y0 && cy <= y1)
						hits++;
				}

				if (hits < shareMin)
					continue;

				pairs++;
				windowed[(size_t)b] = 1;		// the INTRUDER only

				for (size_t c = 0, t = 0; c + 2 < centre[(size_t)b].size();
					c += 3, t++)
				{
					const float cx = centre[(size_t)b][c];
					const float cy = centre[(size_t)b][c + 1];
					const float cz = centre[(size_t)b][c + 2];

					if (cx >= x0 && cx <= x1 && cz >= z0 && cz <= z1 &&
						cy >= y0 && cy <= y1)
						tagged[(size_t)b][t] = 1;
				}

				if (!quiet)
					std::printf("  overlap: room %3d holds the art of room "
						"%3d - %d triangles, no portal between them\n", a, b,
						hits);
			}

		int windowedRooms = 0;

		for (int r = 0; r < rooms && (size_t)r < tagged.size(); r++)
		{
			long long here = 0;

			for (size_t t = 0; t < tagged[(size_t)r].size(); t++)
				here += tagged[(size_t)r][t];

			if (here)
			{
				windowedRooms++;
				intruding += here;
			}
		}

		/* The overhang. One triangle at a time, its centroid in world units
		 * against its own room's full box; `far` is the largest distance any
		 * corner of it reaches past that box. */
		long long tris_total = 0, tris_out = 0;
		long long win_total = 0, win_out = 0;	// windowed rooms alone
		long worst = 0;
		int worst_group = -1;
		long long hist[5] = { 0, 0, 0, 0, 0 };
		long long axis[6] = { 0, 0, 0, 0, 0, 0 };	// -x +x -y +y -z +z
		std::vector<long long> outPerGroup((size_t)trg.groups.size(), 0);
		std::vector<long long> outAllPerGroup((size_t)trg.groups.size(), 0);
		std::vector<long long> triPerGroup((size_t)trg.groups.size(), 0);

		for (size_t g = 0; g < trg.groups.size(); g++)
		{
			if ((int)g >= rooms)
				break;			// the sky groups have no room

			const RoomSpace& b = box[g];

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						long cx = 0, cy = 0, cz = 0;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];

							cx += v.x;
							cy += v.y;
							cz += v.z;
						}

						cx = b.ox + cx / 3;
						cy = b.oy + cy / 3;
						cz = b.oz + cz / 3;

						tris_total++;
						triPerGroup[g]++;

						if (windowed[g])
							win_total++;

						long d = 0;

						if (cx < b.ix0) { axis[0]++; if (b.ix0 - cx > d) d = b.ix0 - cx; }
						if (cx > b.ix1) { axis[1]++; if (cx - b.ix1 > d) d = cx - b.ix1; }
						if (cy < b.y0) { axis[2]++; if (b.y0 - cy > d) d = b.y0 - cy; }
						if (cy > b.y1) { axis[3]++; if (cy - b.y1 > d) d = cy - b.y1; }
						if (cz < b.iz0) { axis[4]++; if (b.iz0 - cz > d) d = b.iz0 - cz; }
						if (cz > b.iz1) { axis[5]++; if (cz - b.iz1 > d) d = cz - b.iz1; }

						if (d)
							outAllPerGroup[g]++;

						if (d <= slack)
							continue;

						tris_out++;
						outPerGroup[g]++;

						if (windowed[g])
							win_out++;

						hist[d <= 16 ? 0 : d <= 64 ? 1 : d <= 256 ? 2 :
							d <= 1024 ? 3 : 4]++;

						if (d > worst)
						{
							worst = d;
							worst_group = (int)g;
						}
					}
				}
		}

		std::printf("%-9s rooms=%d groups=%d  overlap pairs=%d intruding "
			"tris=%lld in %d groups  overhang tris=%lld of %lld  worst=%ld "
			"(group %d)\n",
			stem.c_str(), rooms, (int)trg.groups.size(), pairs, intruding,
			windowedRooms, tris_out, tris_total, worst, worst_group);
		std::printf("%-9s   overhang by distance: <=16 %lld, <=64 %lld, "
			"<=256 %lld, <=1024 %lld, >1024 %lld\n",
			stem.c_str(), hist[0], hist[1], hist[2], hist[3], hist[4]);
		std::printf("%-9s   overhang by side: -x %lld +x %lld -y %lld +y %lld "
			"-z %lld +z %lld\n", stem.c_str(), axis[0], axis[1], axis[2],
			axis[3], axis[4], axis[5]);
		std::printf("%-9s   slack=%d  in the WINDOWED groups: %lld of %lld "
			"triangles stand outside their own room (%.1f%%)\n", stem.c_str(),
			slack, win_out, win_total,
			win_total ? 100.0 * (double)win_out / (double)win_total : 0.0);

		/* A group that draws nothing inside its own room. The engine turns a
		 * room over to the HD path when its group has any triangle at all
		 * (hd_level.cpp: `tris` non-zero). ANDREA2's room 124 - the ground
		 * floor of the courtyard with the palm - has four, and all four of
		 * them stand outside the room. The classic roomlets are dropped for
		 * it and nothing takes their place, which on the screen is a black
		 * hole where the doorway should be. */
		{
			int hollow = 0, thin = 0;

			for (int r = 0; r < rooms && (size_t)r < triPerGroup.size(); r++)
			{
				const long long total = triPerGroup[(size_t)r];
				const long long inside = total - outAllPerGroup[(size_t)r];

				if (!total)
					continue;

				if (!inside)
				{
					hollow++;

					if (!quiet)
						std::printf("    HOLLOW room %3d: %lld triangles, "
							"NONE of them inside the room\n", r, total);
				}
				else if (inside < 32)
				{
					thin++;

					if (!quiet)
						std::printf("    thin   room %3d: %lld triangles, "
							"%lld inside the room\n", r, total, inside);
				}
			}

			std::printf("%-9s   groups that draw NOTHING inside their own "
				"room: %d, fewer than 32: %d\n", stem.c_str(), hollow, thin);
		}

		if (!quiet)
		{
			std::printf("  the ten groups with the most overhang:\n");

			std::vector<std::pair<long long, int> > order;

			for (size_t g = 0; g < outPerGroup.size(); g++)
				if (outPerGroup[g])
					order.push_back(std::make_pair(outPerGroup[g], (int)g));

			std::sort(order.begin(), order.end());
			std::reverse(order.begin(), order.end());

			for (size_t i = 0; i < order.size() && i < 10; i++)
				std::printf("    group %3d  %6lld of %6lld  windowed=%d\n",
					order[i].second, order[i].first,
					triPerGroup[(size_t)order[i].second],
					(int)windowed[(size_t)order[i].second]);
		}

		/* Whose art stands at a given place in the world, -near <x> <y> <z>
		 * <radius>.
		 *
		 * Pure remaster art can stand far outside whatever room's box it
		 * belongs to and pop into the picture as Lara walks up to it - a
		 * brick tower with a barred window at the corner of the Piazza
		 * dell'Opera is not in trview at all. Before anything can be done
		 * about it the room has to be named, and a room's own box cannot
		 * name it: the art is nowhere near the box.
		 *
		 * So this asks the triangles. Every group's overhang triangles - the
		 * ones standing outside their own room - are held against a sphere in
		 * world units, and what comes back is which room's group has art
		 * there and how far that art is from its own room. */
		if (nearAt)
		{
			std::printf("%-9s   art within %ld units of %ld/%ld/%ld:\n",
				stem.c_str(), nearRadius, nearAt[0], nearAt[1], nearAt[2]);

			struct Hit { long long tris; long long out; long dist; int group; };
			std::vector<Hit> found;

			for (size_t g = 0; g < trg.groups.size() && (int)g < rooms; g++)
			{
				const RoomSpace& b = box[g];
				Hit hit;

				hit.tris = 0;
				hit.out = 0;
				hit.dist = 0;
				hit.group = (int)g;

				for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
					for (int pass = 0; pass < 3; pass++)
					{
						const trf::remaster::TrgPart::Pass& run = part.pass[pass];

						for (unsigned at = 0; at + 2 < run.count; at += 3)
						{
							const unsigned first = run.start + at;

							if (first + 2 >= trg.indices.size())
								break;

							long qx = 0, qy = 0, qz = 0;

							for (int c = 0; c < 3; c++)
							{
								const trf::remaster::TrgVertex& v =
									trg.vertices[trg.indices[first + c]];

								qx += v.x;
								qy += v.y;
								qz += v.z;
							}

							const long wx = b.ox + qx / 3;
							const long wy = b.oy + qy / 3;
							const long wz = b.oz + qz / 3;
							// long long, not long: three squared distances
							// of 37,000 units overflow a 32 bit signed sum and
							// the sphere then catches the whole level.
							const long long dx = wx - nearAt[0];
							const long long dy = wy - nearAt[1];
							const long long dz = wz - nearAt[2];

							if (dx * dx + dy * dy + dz * dz >
								(long long)nearRadius * nearRadius)
								continue;

							hit.tris++;

							// how far this triangle is from its own room box
							long d = 0;

							if (wx < b.x0) d = b.x0 - wx;
							if (wx > b.x1 && wx - b.x1 > d) d = wx - b.x1;
							if (wy < b.y0 && b.y0 - wy > d) d = b.y0 - wy;
							if (wy > b.y1 && wy - b.y1 > d) d = wy - b.y1;
							if (wz < b.z0 && b.z0 - wz > d) d = b.z0 - wz;
							if (wz > b.z1 && wz - b.z1 > d) d = wz - b.z1;

							if (d > 0)
								hit.out++;

							if (d > hit.dist)
								hit.dist = d;
						}
					}

				if (hit.tris)
					found.push_back(hit);
			}

			for (size_t i = 0; i < found.size(); i++)
				for (size_t j = i + 1; j < found.size(); j++)
					if (found[j].tris > found[i].tris)
					{
						const Hit t = found[i];
						found[i] = found[j];
						found[j] = t;
					}

			for (size_t i = 0; i < found.size() && i < 16; i++)
				std::printf("    group %3d (room box x %ld..%ld y %ld..%ld "
					"z %ld..%ld): %lld triangle(s) here, %lld of them outside "
					"their own room, worst %ld units out\n",
					found[i].group,
					box[(size_t)found[i].group].x0, box[(size_t)found[i].group].x1,
					box[(size_t)found[i].group].y0, box[(size_t)found[i].group].y1,
					box[(size_t)found[i].group].z0, box[(size_t)found[i].group].z1,
					found[i].tris, found[i].out, found[i].dist);
		}

		/* The static meshes of every room, and whether the .TRG draws
		 * anything where each one stands. The engine asks exactly this
		 * question in hd_level.cpp, BuildStaticTables, and suppresses the
		 * classic draw where the answer is yes; here it is asked offline,
		 * out of the same two files, so "the palm is baked into the room
		 * art" is a number rather than a guess.
		 *
		 * The room blob is a ROOM_INFO written out (specific\alexstuff.cpp,
		 * aFixUpRoom), so `mesh` at +16 is the blob-relative offset of the
		 * MESH_INFO array and `num_meshes` sits at +50. The visibility box of
		 * a slot comes out of the level's own static block, which follows the
		 * moveables. */
		if (staticList)
		{
			size_t objAt = 0; int objN = 0, frN = 0; size_t frAt = 0;
			short vis[70][6];
			bool have[70] = { false };

			std::memset(vis, 0, sizeof(vis));

			if (level.debugModelBlocks(objAt, objN, frAt, frN))
			{
				const std::vector<unsigned char>& raw = level.debugBytes();
				size_t at = objAt + (size_t)objN * 20;
				int n = 0;

				std::memcpy(&n, raw.data() + at, 4);
				at += 4;

				for (int i = 0; i < n; i++, at += 32)
				{
					int slot = 0;

					std::memcpy(&slot, raw.data() + at, 4);

					if (slot < 0 || slot >= 70)
						continue;

					std::memcpy(vis[slot], raw.data() + at + 6, 12);
					have[slot] = true;
				}
			}

			for (int r = 0; r < rooms; r++)
			{
				const Tr5Room& rm = level.rooms()[(size_t)r];
				unsigned meshOfs = 0, lightsMeshes = 0;

				if (!rm.headerOffset ||
					!level.headerField(r, 16, meshOfs, error) ||
					!level.headerField(r, 48, lightsMeshes, error))
					continue;

				const int nMesh = (int)(lightsMeshes >> 16);

				if (!nMesh)
					continue;

				const std::vector<unsigned char>& raw = level.debugBytes();

				for (int m = 0; m < nMesh; m++)
				{
					const size_t at = rm.headerOffset + 208 + meshOfs +
						(size_t)m * 20;

					if (at + 20 > raw.size())
						break;

					int mx = 0, my = 0, mz = 0;
					short rot = 0, shade = 0, flags = 0, slot = 0;

					std::memcpy(&mx, raw.data() + at, 4);
					std::memcpy(&my, raw.data() + at + 4, 4);
					std::memcpy(&mz, raw.data() + at + 8, 4);
					std::memcpy(&rot, raw.data() + at + 12, 2);
					std::memcpy(&shade, raw.data() + at + 14, 2);
					std::memcpy(&flags, raw.data() + at + 16, 2);
					std::memcpy(&slot, raw.data() + at + 18, 2);

					if (staticSlot >= 0 && slot != staticSlot)
						continue;

					if (focus >= 0 && r != focus)
						continue;

					/* How much of the group's art stands in the static's own
					 * visibility box - the engine turns any at all into
					 * "suppress the classic draw". */
					int hits = 0;
					long bx0 = 0, bx1 = 0, bz0 = 0, bz1 = 0, by0 = 0, by1 = 0;

					if (slot >= 0 && slot < 70 && have[slot])
					{
						by0 = my + vis[slot][2];
						by1 = my + vis[slot][3];
						bx0 = mx + vis[slot][0];
						bx1 = mx + vis[slot][1];
						bz0 = mz + vis[slot][4];
						bz1 = mz + vis[slot][5];

						if ((size_t)r < cells.size())
							for (int half = 0; half < 2; half++)
							{
								const std::vector<unsigned long long>& src =
									half ? shell[(size_t)r] : cells[(size_t)r];

								for (size_t c = 0; c < src.size(); c++)
								{
									const unsigned long long key = src[c];
									const long qx = ((long)(key >> 34) - 32768) << 8;
									const long qy = ((long)((key >> 17) & 0x1FFFF) - 32768) << 8;
									const long qz = ((long)(key & 0x1FFFF) - 32768) << 8;

									if (qx + 255 >= bx0 && qx <= bx1 &&
										qy + 255 >= by0 && qy <= by1 &&
										qz + 255 >= bz0 && qz <= bz1)
										hits++;
								}
							}
					}

					const long boxCells =
						(((bx1 >> 8) - (bx0 >> 8)) + 1) *
						(((by1 >> 8) - (by0 >> 8)) + 1) *
						(((bz1 >> 8) - (bz0 >> 8)) + 1);

					std::printf("  static room=%3d mesh=%2d slot=%2d at "
						"%ld/%ld/%ld rot=%d flags=%d box x %ld..%ld y %ld..%ld "
						"z %ld..%ld  art cells in the box=%d of %ld (%.1f%%)%s\n",
						r, m, (int)slot, (long)mx, (long)my, (long)mz,
						(int)rot, (int)flags, bx0, bx1, by0, by1, bz0, bz1,
						hits, boxCells,
						boxCells ? 100.0 * (double)hits / (double)boxCells : 0.0,
						hits ? " -> the engine SUPPRESSES it"
							: " -> the engine keeps the CLASSIC draw");
				}
			}
		}

		if (focus < 0 || focus >= rooms)
			return;

		const RoomSpace& f = box[(size_t)focus];

		std::printf("\n  ROOM %d of %s: origin %ld/%ld/%ld  sectors %dx%d  "
			"full box x %ld..%ld  y %ld..%ld  z %ld..%ld  statics=%d\n",
			focus, stem.c_str(), f.ox, f.oy, f.oz, f.numX, f.numZ,
			f.x0, f.x1, f.y0, f.y1, f.z0, f.z1, f.meshes);
		std::printf("  inner box x %ld..%ld  z %ld..%ld   group tris=%lld "
			"overhang=%lld\n", f.ix0, f.ix1, f.iz0, f.iz1,
			(size_t)focus < triPerGroup.size() ? triPerGroup[(size_t)focus] : -1,
			(size_t)focus < outPerGroup.size() ? outPerGroup[(size_t)focus] : -1);

		/* Whose art stands inside this room. Every other room's cells held
		 * against this room's inner box - the volume TR5 itself calls the
		 * inside of the room (game\draw.cpp). A neighbour's doorway reveal
		 * reaches a cell or two; a room that really stands inside this one
		 * fills it. */
		{
			std::printf("  art of other rooms inside room %d's inner box:\n", focus);

			for (int b = 0; b < rooms && (size_t)b < cells.size(); b++)
			{
				if (b == focus)
					continue;

				int inside = 0;

				for (size_t c = 0; c < cells[(size_t)b].size(); c++)
				{
					const unsigned long long key = cells[(size_t)b][c];
					const long cx = (long)((key >> 34) - 32768) << 8;
					const long cy = (long)(((key >> 17) & 0x1FFFF) - 32768) << 8;
					const long cz = (long)((key & 0x1FFFF) - 32768) << 8;

					if (cx >= f.ix0 && cx < f.ix1 && cz >= f.iz0 && cz < f.iz1 &&
						cy >= f.y0 && cy < f.y1)
						inside++;
				}

				if (inside)
					std::printf("    room %3d: %4d of %4d cells inside\n", b,
						inside, (int)cells[(size_t)b].size());
			}
		}

		std::vector<Tr5Level::PortalRecord> ports;

		if (level.portalRecords(focus, ports, error))
			for (size_t p = 0; p < ports.size(); p++)
			{
				const Tr5Level::PortalRecord& pr = ports[p];
				long tris = 0;

				if (pr.toRoom >= 0 && (size_t)pr.toRoom < triPerGroup.size())
					tris = (long)triPerGroup[(size_t)pr.toRoom];

				std::printf("    portal -> room %3d  normal %d/%d/%d  "
					"corners (%d %d %d) (%d %d %d) (%d %d %d) (%d %d %d)  "
					"that room's .TRG tris=%ld\n",
					pr.toRoom, pr.nx, pr.ny, pr.nz,
					pr.v[0][0], pr.v[0][1], pr.v[0][2],
					pr.v[1][0], pr.v[1][1], pr.v[1][2],
					pr.v[2][0], pr.v[2][1], pr.v[2][2],
					pr.v[3][0], pr.v[3][1], pr.v[3][2], tris);
			}
		else
			std::printf("    portals: %s\n", error.c_str());

		std::vector<Tr5Level::SectorInfo> sectors;

		if (level.sectorInfo(focus, sectors, error))
		{
			std::set<int> above, below;

			for (size_t s = 0; s < sectors.size(); s++)
			{
				if (sectors[s].roomAbove >= 0) above.insert(sectors[s].roomAbove);
				if (sectors[s].roomBelow >= 0) below.insert(sectors[s].roomBelow);
			}

			std::printf("    room above:");
			for (std::set<int>::const_iterator i = above.begin(); i != above.end(); ++i)
				std::printf(" %d", *i);
			std::printf("   room below:");
			for (std::set<int>::const_iterator i = below.begin(); i != below.end(); ++i)
				std::printf(" %d", *i);
			std::printf("\n");
		}

		std::printf("\n");
	}

	/* The classic room's flat faces beside the .TRG's.
	 *
	 * In trview a vanilla water face wears a wave texture and the HD one has
	 * no assignment. The two meshes can be held against each other because
	 * a TR5 room vertex carries its y in world units (drawroom.cpp compares
	 * pos.y with r->minfloor straight) and so does a .TRG vertex - only x
	 * and z are room local, and the room's origin is known. So both sides
	 * are listed as "horizontal faces per height", and a height the classic
	 * side has and the HD side does not is a surface the remaster did not
	 * build. */
	void dumpSurface(const std::string& root, const std::string& stem,
		const std::string& folder, int roomNr)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = folder + "\\" + stem + ".TRC";

		if (!level.load(toW(trc), error))
		{
			std::printf("%-9s classic level not loadable: %s\n", stem.c_str(),
				error.c_str());
			return;
		}

		if (roomNr < 0 || roomNr >= level.roomCount())
		{
			std::printf("%-9s room out of range (0..%d)\n", stem.c_str(),
				level.roomCount() - 1);
			return;
		}

		const Tr5Room& rm = level.rooms()[(size_t)roomNr];
		std::vector<Tr5Level::FaceInfo> faces;

		if (!level.roomFaces(roomNr, faces, error))
		{
			std::printf("%-9s room %d: no faces (%s)\n", stem.c_str(), roomNr,
				error.c_str());
			return;
		}

		std::printf("%-9s classic room %d: flags 0x%04X, origin x %d z %d, "
			"yBottom %d, %d face(s)\n", stem.c_str(), roomNr, rm.flags,
			rm.originX, rm.originZ, rm.yBottom, (int)faces.size());

		/* Per height: how many horizontal faces, which texinfos, the box. */
		struct Row
		{
			int faces = 0;
			std::map<int, int> tex;
			int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
		};

		std::map<int, Row> rows;

		for (size_t f = 0; f < faces.size(); f++)
		{
			const Tr5Level::FaceInfo& face = faces[f];
			const int corners = face.quad ? 4 : 3;
			bool flat = true;

			for (int c = 1; c < corners; c++)
				if (face.v[c][1] != face.v[0][1])
					flat = false;

			if (!flat)
				continue;

			Row& row = rows[(int)face.v[0][1]];
			row.faces++;
			row.tex[face.texInfo]++;

			for (int c = 0; c < corners; c++)
			{
				if ((int)face.v[c][0] < row.x0) row.x0 = (int)face.v[c][0];
				if ((int)face.v[c][0] > row.x1) row.x1 = (int)face.v[c][0];
				if ((int)face.v[c][2] < row.z0) row.z0 = (int)face.v[c][2];
				if ((int)face.v[c][2] > row.z1) row.z1 = (int)face.v[c][2];
			}
		}

		std::printf("  classic horizontal faces:\n");
		std::printf("        y  faces  texinfos      x0      x1      z0      z1\n");

		for (std::map<int, Row>::const_iterator at = rows.begin();
			at != rows.end(); ++at)
		{
			std::printf("%9d %6d %9d %7d %7d %7d %7d  :",
				at->first, at->second.faces, (int)at->second.tex.size(),
				at->second.x0, at->second.x1, at->second.z0, at->second.z1);

			int shown = 0;

			for (std::map<int, int>::const_iterator t = at->second.tex.begin();
				t != at->second.tex.end() && shown < 8; ++t, ++shown)
				std::printf(" %d(%d)", t->first, t->second);

			std::printf("%s\n", at->second.tex.size() > 8 ? " ..." : "");
		}

		/* And the same heights out of the .TRG, in world x and z. */
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		if (roomNr >= (int)trg.groups.size())
		{
			std::printf("  .TRG has no group %d\n", roomNr);
			return;
		}

		std::map<FlatKey, FlatCount> table;

		for (const trf::remaster::TrgPart& part : trg.groups[(size_t)roomNr].parts)
			for (int pass = 0; pass < 3; pass++)
				for (uint32_t at = 0; at + 2 < part.pass[pass].count; at += 3)
				{
					const uint32_t first = part.pass[pass].start + at;

					if (first + 2 >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex& a = trg.vertices[trg.indices[first]];
					const trf::remaster::TrgVertex& b = trg.vertices[trg.indices[first + 1]];
					const trf::remaster::TrgVertex& c = trg.vertices[trg.indices[first + 2]];

					if (a.y != b.y || b.y != c.y)
						continue;

					FlatKey key;
					key.y = a.y;
					key.layer = a.layer;
					key.material = part.material;
					key.pass = pass;

					FlatCount& f = table[key];
					f.tris++;

					if (a.ny > 160) f.up++;
					else if (a.ny < 94) f.down++;

					const int xs[3] = { a.x + rm.originX, b.x + rm.originX, c.x + rm.originX };
					const int zs[3] = { a.z + rm.originZ, b.z + rm.originZ, c.z + rm.originZ };

					for (int i = 0; i < 3; i++)
					{
						if (xs[i] < f.x0) f.x0 = xs[i];
						if (xs[i] > f.x1) f.x1 = xs[i];
						if (zs[i] < f.z0) f.z0 = zs[i];
						if (zs[i] > f.z1) f.z1 = zs[i];
					}
				}

		std::printf("  .TRG group %d horizontal triangles (world x/z):\n", roomNr);
		std::printf("        y  layer   page   mat  pass   tris      x0      x1      z0      z1\n");

		for (std::map<FlatKey, FlatCount>::const_iterator at = table.begin();
			at != table.end(); ++at)
		{
			const int layer = at->first.layer;
			const int page = (layer && (size_t)layer <= trg.textures.size())
				? (int)trg.textures[layer - 1] : -1;

			std::printf("%9d %6d %6d %5u %5d %6lld %7d %7d %7d %7d\n",
				at->first.y, layer, page, at->first.material, at->first.pass,
				at->second.tris, at->second.x0, at->second.x1,
				at->second.z0, at->second.z1);
		}
	}

	/* The missing water surface, over a whole level.
	 *
	 * The classic engine draws a water surface as ordinary room faces at the
	 * room's own minfloor, and the shimmer branch that makes them move opens
	 * with `pos.y == CurrentRoomPtr->minfloor` in a room carrying
	 * ROOM_REFLECT or ROOM_MIST (drawroom.cpp). So for every such
	 * room: the lowest plane of horizontal faces is the water, and the
	 * question is whether the .TRG's group for that room draws anything
	 * there. It is asked per plane, in world coordinates, and answered in
	 * triangles - two per classic quad is what a plain remesh would give.
	 */
	void dumpWaterGap(const std::string& root, const std::string& stem,
		const std::string& folder)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = folder + "\\" + stem + ".TRC";

		if (!level.load(toW(trc), error))
		{
			std::printf("%-9s classic level not loadable: %s\n", stem.c_str(),
				error.c_str());
			return;
		}

		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		int rooms = 0, planes = 0, gone = 0, classicFaces = 0, hdTris = 0;

		for (int r = 0; r < level.roomCount(); r++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)r];

			/* 0x200 ROOM_REFLECT, 0x100 ROOM_MIST - the two the shimmer
			 * branch opens on. An underwater room's own top face, where it
			 * has one, belongs to the room above. */
			if (!(rm.flags & 0x300))
				continue;

			std::vector<Tr5Level::FaceInfo> faces;

			if (!level.roomFaces(r, faces, error))
				continue;

			// The lowest horizontal plane of the room: minfloor.
			int floorY = -(1 << 30);
			bool any = false;

			for (size_t f = 0; f < faces.size(); f++)
			{
				const int corners = faces[f].quad ? 4 : 3;
				bool flat = true;

				for (int c = 1; c < corners; c++)
					if (faces[f].v[c][1] != faces[f].v[0][1])
						flat = false;

				if (!flat)
					continue;

				any = true;

				if ((int)faces[f].v[0][1] > floorY)
					floorY = (int)faces[f].v[0][1];
			}

			if (!any)
				continue;

			int here = 0;
			int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
			std::map<int, int> tex;

			for (size_t f = 0; f < faces.size(); f++)
			{
				const int corners = faces[f].quad ? 4 : 3;
				bool flat = true;

				for (int c = 1; c < corners; c++)
					if (faces[f].v[c][1] != faces[f].v[0][1])
						flat = false;

				if (!flat || (int)faces[f].v[0][1] != floorY)
					continue;

				here++;
				tex[faces[f].texInfo]++;

				for (int c = 0; c < corners; c++)
				{
					if ((int)faces[f].v[c][0] < x0) x0 = (int)faces[f].v[c][0];
					if ((int)faces[f].v[c][0] > x1) x1 = (int)faces[f].v[c][0];
					if ((int)faces[f].v[c][2] < z0) z0 = (int)faces[f].v[c][2];
					if ((int)faces[f].v[c][2] > z1) z1 = (int)faces[f].v[c][2];
				}
			}

			if (here < 2 || r >= (int)trg.groups.size())
				continue;

			// The .TRG's triangles in that plane and that box.
			int mine = 0;

			for (const trf::remaster::TrgPart& part : trg.groups[(size_t)r].parts)
				for (int pass = 0; pass < 3; pass++)
					for (uint32_t at = 0; at + 2 < part.pass[pass].count; at += 3)
					{
						const uint32_t first = part.pass[pass].start + at;

						if (first + 2 >= trg.indices.size())
							break;

						bool in = true;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];
							const int wx = v.x + rm.originX;
							const int wz = v.z + rm.originZ;

							if (v.y != floorY || wx < x0 || wx > x1 || wz < z0 || wz > z1)
								in = false;
						}

						if (in)
							mine++;
					}

			rooms++;
			planes++;
			classicFaces += here;
			hdTris += mine;

			if (mine * 2 < here)
				gone++;

			{
				std::printf("%-9s room %3d  y %6d  classic %3d face(s), %d texinfo(s), "
					"HD %3d triangle(s)  x %d..%d z %d..%d\n",
					stem.c_str(), r, floorY, here, (int)tex.size(), mine,
					x0, x1, z0, z1);
			}
		}

		std::printf("%-9s %d reflecting room(s), %d classic surface face(s), "
			"%d HD triangle(s) in the same planes, %d plane(s) with less than "
			"half the art\n\n", stem.c_str(), rooms, classicFaces, hdTris, gone);
	}

	int classicRooms(const std::string& folder, const std::string& stem)
	{
		if (folder.empty())
			return 0;
		Tr5Level level;
		std::string error;
		const std::string path = folder + "\\" + stem + ".TRC";
		if (!level.load(toW(path), error))
			return 0;
		return level.roomCount();
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: TrgProbe <...\\5> [level ...] [-all] [-bc7 [threads]]\n");
		std::printf("       [-classic <folder holding the classic .TRC>]  for the room count\n");
		std::printf("       [-pages]  which page numbers have no .DDS in TEX\n");
		std::printf("       [-mat [material]]  the distinct (material, param) tuples,\n");
		std::printf("                          with triangles per pass and groups\n");
		std::printf("       [-seam <groupA> <groupB> <dx> <dy> <dz>]  the baked light\n");
		std::printf("                          where two rooms share vertex positions\n");
		std::printf("       [-flat <group>]  the horizontal surfaces of one group, with\n");
		std::printf("                        the page each of them wears (the water survey)\n");
		std::printf("       [-layer0]  how much of a level names no page at all\n");
		std::printf("       [-matparts <material>]  every part of ONE material with its\n");
		std::printf("                          geometry, and (with -classic) the classic\n");
		std::printf("                          room's own horizontal planes beside it\n");
		return 1;
	}

	if (!_stricmp(argv[1], "-find"))
	{
		if (argc > 2)
		{
			strncpy(tomb5_hd_install, argv[2], sizeof(tomb5_hd_install) - 1);
			tomb5_hd_install[sizeof(tomb5_hd_install) - 1] = 0;
		}
		std::printf("override: %s\n", HDInstallOverride()[0] ? HDInstallOverride() : "(none)");
		std::printf("found   : %s\n", HDInstallRoot()[0] ? HDInstallRoot() : "(nothing)");
		std::printf("why     :\n%s\n", HDInstallDiagnostics());
		return HDInstallRoot()[0] ? 0 : 1;
	}

	const std::string root = argv[1];

	std::vector<std::string> levels;
	std::string classic;
	bool bc7_only = false;
	bool list_pages = false;
	bool list_materials = false;
	int only_material = -1;
	int threads = 0;
	int seam_a = -1, seam_b = -1, seam_d[3] = { 0, 0, 0 };
	int flat_group = -1;
	int parts_group = -1;
	int surface_room = -1;
	bool water_gap = false;
	bool layer_zero = false;
	int mat_parts = -1;
	bool room_space = false;
	int room_focus = -1;
	int room_share = 1;
	long room_slack = 0;
	long room_deep = 0;
	bool room_statics = false;
	int room_slot = -1;
	long near_at[3] = { 0, 0, 0 };
	long near_radius = 0;
	bool near_given = false;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-roomspace"))
			room_space = true;
		else if (!_stricmp(argv[i], "-share") && i + 1 < argc)
			room_share = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-slack") && i + 1 < argc)
			room_slack = std::atol(argv[++i]);
		else if (!_stricmp(argv[i], "-deep") && i + 1 < argc)
			room_deep = std::atol(argv[++i]);
		else if (!_stricmp(argv[i], "-near") && i + 4 < argc)
		{
			room_space = true;
			near_given = true;
			near_at[0] = std::atol(argv[i + 1]);
			near_at[1] = std::atol(argv[i + 2]);
			near_at[2] = std::atol(argv[i + 3]);
			near_radius = std::atol(argv[i + 4]);
			i += 4;
		}
		else if (!_stricmp(argv[i], "-statics"))
		{
			room_space = true;
			room_statics = true;
		}
		else if (!_stricmp(argv[i], "-slot") && i + 1 < argc)
		{
			room_space = true;
			room_statics = true;
			room_slot = std::atoi(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-room") && i + 1 < argc)
		{
			room_space = true;
			room_focus = std::atoi(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-matparts") && i + 1 < argc)
			mat_parts = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-watergap"))
			water_gap = true;
		else if (!_stricmp(argv[i], "-parts") && i + 1 < argc)
			parts_group = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-surface") && i + 1 < argc)
			surface_room = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-flat") && i + 1 < argc)
			flat_group = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-layer0"))
			layer_zero = true;
		else if (!_stricmp(argv[i], "-seam") && i + 5 < argc)
		{
			seam_a = std::atoi(argv[i + 1]);
			seam_b = std::atoi(argv[i + 2]);

			for (int c = 0; c < 3; c++)
				seam_d[c] = std::atoi(argv[i + 3 + c]);

			i += 5;
		}
		else if (!_stricmp(argv[i], "-mat"))
		{
			list_materials = true;

			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				only_material = std::atoi(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-classic") && i + 1 < argc)
			classic = argv[++i];
		else if (!_stricmp(argv[i], "-all"))
		{
			for (const char* name : kLevels)
				levels.push_back(name);
		}
		else if (!_stricmp(argv[i], "-pages"))
			list_pages = true;
		else if (!_stricmp(argv[i], "-bc7"))
		{
			bc7_only = true;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				threads = std::atoi(argv[++i]);
		}
		else
			levels.push_back(argv[i]);
	}

	if (levels.empty())
		for (const char* name : kLevels)
			levels.push_back(name);

	const int cores = (int)std::thread::hardware_concurrency();
	std::printf("install : %s\n", root.c_str());
	std::printf("cores   : %d\n\n", cores);

	if (room_space)
	{
		if (classic.empty())
		{
			std::printf("-roomspace needs -classic <folder holding the .TRC>\n");
			return 1;
		}

		for (const std::string& stem : levels)
			dumpRoomSpace(root, stem, classic, room_focus, levels.size() > 1,
				room_share, room_slack, room_statics, room_slot, room_deep,
				near_given ? near_at : 0, near_radius);

		return 0;
	}

	if (mat_parts >= 0)
	{
		for (const std::string& stem : levels)
			dumpMat16(root, stem, classic, mat_parts);

		return 0;
	}

	if (surface_room >= 0)
	{
		if (classic.empty())
		{
			std::printf("-surface needs -classic <folder holding the .TRC>\n");
			return 1;
		}

		for (const std::string& stem : levels)
			dumpSurface(root, stem, classic, surface_room);

		return 0;
	}

	if (water_gap)
	{
		if (classic.empty())
		{
			std::printf("-watergap needs -classic <folder holding the .TRC>\n");
			return 1;
		}

		for (const std::string& stem : levels)
			dumpWaterGap(root, stem, classic);

		return 0;
	}

	if (parts_group >= 0)
	{
		for (const std::string& stem : levels)
			dumpParts(root, stem, parts_group);

		return 0;
	}

	if (flat_group >= 0)
	{
		for (const std::string& stem : levels)
			dumpFlat(root, stem, flat_group);

		return 0;
	}

	if (layer_zero)
	{
		for (const std::string& stem : levels)
			dumpLayer0(root, stem);

		return 0;
	}

	if (seam_a >= 0)
	{
		for (const std::string& stem : levels)
			seam(root, stem, seam_a, seam_b, seam_d[0], seam_d[1], seam_d[2]);

		return 0;
	}

	if (list_materials)
	{
		for (const std::string& stem : levels)
			dumpMaterials(root, stem, only_material);

		return 0;
	}

	if (list_pages)
	{
		// Which page a level names and the TEX folder does not have.
		// The .TEX sidecar of all 15 levels names only pages that
		// exist - measured - but the .TRG names a wider set, and
		// RICH3 was the level where the decode run reported a gap.
		for (const std::string& stem : levels)
		{
			HDScanStats stats;
			const char* why = "";
			if (!HDScanLevel(root.c_str(), stem.c_str(), 0, 0, 0, false, 1, &stats, &why))
			{
				std::printf("%-9s FAILED %s\n", stem.c_str(), why);
				continue;
			}

			int gone = 0;
			char path[300];
			for (int i = 0; i < HDScanPageCount(); i++)
			{
				const int page = HDScanPage(i);
				HDScanPagePath(root.c_str(), page, path, sizeof(path));
				std::FILE* f = std::fopen(path, "rb");
				if (f) { std::fclose(f); continue; }
				if (!gone)
					std::printf("%-9s pages with no .DDS:", stem.c_str());
				std::printf(" %d", page);
				gone++;
			}
			if (gone)
				std::printf("   (%d of %d)\n", gone, HDScanPageCount());
			else
				std::printf("%-9s all %d pages present\n", stem.c_str(), HDScanPageCount());
		}
		return 0;
	}

	if (bc7_only)
	{
		const int want = threads > 0 ? threads : cores;
		std::printf("%-9s %6s %8s %8s %10s %10s %8s\n",
			"level", "pages", "mips", "texels", "1 core ms", "N cores ms", "speedup");
		std::printf("%-9s %6s %8s %8s %10s %10s %8s\n",
			"---------", "------", "--------", "--------", "----------", "----------", "--------");

		for (const std::string& stem : levels)
		{
			HDScanStats stats;
			const char* why = "";
			if (!HDScanLevel(root.c_str(), stem.c_str(), 0, 0, 0, false, 1, &stats, &why))
			{
				std::printf("%-9s FAILED %s\n", stem.c_str(), why);
				continue;
			}

			std::vector<int> pages;
			for (int i = 0; i < HDScanPageCount(); i++)
				pages.push_back(HDScanPage(i));
			if (pages.empty())
				continue;

			double texels1 = 0, texelsN = 0;
			int mips1 = 0, mipsN = 0, failed1 = 0, failedN = 0;
			const double one = HDScanDecodePages(root.c_str(), &pages[0], (int)pages.size(),
				1, &texels1, &mips1, &failed1);
			const double many = HDScanDecodePages(root.c_str(), &pages[0], (int)pages.size(),
				want, &texelsN, &mipsN, &failedN);

			std::printf("%-9s %6d %8d %8.0f %10.0f %10.0f %8.2f%s\n",
				stem.c_str(), (int)pages.size(), mips1, texels1,
				one, many, many > 0 ? one / many : 0.0,
				failed1 ? "  (pages missing!)" : "");
		}
		return 0;
	}

	std::printf("%-9s %3s %7s %6s %4s %6s %6s %9s %10s %8s %8s %6s\n",
		"level", "v", "groups", "rooms", "sky", "empty", "pages", "vertices",
		"indices", "ranges", "changes", "mixed");
	std::printf("%-9s %3s %7s %6s %4s %6s %6s %9s %10s %8s %8s %6s\n",
		"---------", "---", "-------", "------", "----", "------", "------",
		"---------", "----------", "--------", "--------", "------");

	long long sum_ranges = 0, sum_changes = 0;

	for (const std::string& stem : levels)
	{
		const int rooms = classicRooms(classic, stem);

		HDScanStats stats;
		const char* why = "";
		if (!HDScanLevel(root.c_str(), stem.c_str(), rooms, 0, 0, false, 1, &stats, &why))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), why);
			continue;
		}

		std::printf("%-9s %3d %7d %6d %4d %6d %6d %9d %10d %8d %8d %6d\n",
			stem.c_str(), stats.trgVersion, stats.groups, stats.rooms,
			stats.skyGroups, stats.emptyGroups, stats.pages, stats.vertices,
			stats.indices, stats.ranges, stats.pageChanges,
			stats.mixedPageTriangles);

		sum_ranges += stats.ranges;
		sum_changes += stats.pageChanges;
	}

	std::printf("\nsorted into %lld ranges, against %lld page changes unsorted",
		sum_ranges, sum_changes);
	if (sum_ranges)
		std::printf(" - a factor of %.1f", (double)sum_changes / (double)sum_ranges);
	std::printf("\n");
	return 0;
}

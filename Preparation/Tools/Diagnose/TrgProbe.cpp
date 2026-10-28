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
//   TrgProbe -flyby <classic folder> [level ...]          the flyby census
//
// -find runs the engine's own search for an installed Tomb Raider IV-VI
// Remastered (specific\hd\hd_install.cpp is compiled in): the registry
// override first, then every Steam library. It prints what it found and the
// diagnostics it would put in the log.
//
// "<path to the game 5 folder>" is the "5" inside an installed Tomb Raider
// IV-VI Remastered, the one with DATA, TEX and ITEM in it. Nothing is written
// and nothing is copied - this project ships no game data (THIRD-PARTY.md,
// "No game data"): the Remaster's files stay where they are.
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
#include <chrono>
#include <iterator>
#include <map>
#include <set>
#include <array>
#include <cmath>
#include <filesystem>
#include <unordered_map>

#include "core/Tr5Level.h"
#include "hd_install.h"
// The .TRM reader, for -trmnormals.
#include "hd_trm.h"
// HDPairFace and HDPairFaceDistance - the renderer's own face test, inline.
#include "hd_render.h"
#include "hd_scan.h"
#include "trf_remaster/Trg.h"
// The sheen's page classes - the engine's own seed walk, table and resolver.
#include "hd_gloss_pages.h"
#include "hd_gloss_seed.h"
// And the light field its highlight reads - the engine's own layout and build.
#include "hd_gloss_field.h"
// The visibility path's tables (-vistable), out of the engine's own builder.
#include "VisInput.h"
// The engine's slot names, for the items a flyby's trigger moves (-flyby).
#include "hd_bind.h"
// The census rule of the sequences a skip ends short (-flyby).
#include "../../../TOMB5 Code/TOMB5/game/spotcam_math.h"

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
		std::set<int> bookPages;	// material 15: the first page of each part's passes
		const trf::remaster::TrgPart* bookPart = 0;	// and one part, for its run
	};

	/* The first page of one pass of a part: the page the first corner of
	 * that pass's run names, which is the corner hd_render.cpp's
	 * RunFirstPage reads for the same run, or -1 for an empty run or a layer
	 * that names no page. Per pass, because the engine begins each pass's
	 * book on that pass's own page. */
	int partFirstPage(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part, int pass)
	{
		const uint32_t start = part.pass[pass].start;

		if (!part.pass[pass].count || start >= trg.indices.size())
			return -1;

		const uint32_t corner = trg.indices[start];

		if (corner >= trg.vertices.size())
			return -1;

		const uint8_t layer = trg.vertices[corner].layer;

		return layer && (size_t)layer <= trg.textures.size()
			? (int)trg.textures[layer - 1] : -1;
	}

	/* The run of a material 15 book: param[1] pages from `page0` on by page
	 * number, param[2] seconds each, starting at page param[0] - and how many
	 * of those pages the file's own texture list holds. The engine loads
	 * every page of that list and nothing else, so the book turns only when
	 * all of them are there; with one missing the part keeps its own page. */
	void printBook(const trf::remaster::Trg& trg,
		const trf::remaster::TrgPart& part, int page0)
	{
		const int count = (int)part.param[1];
		int listed = 0;

		for (int k = 0; k < count && page0 >= 0; k++)
			for (size_t i = 0; i < trg.textures.size(); i++)
				if ((int)trg.textures[i] == page0 + k)
				{
					listed++;
					break;
				}

		std::printf("        book: pages %d..%d, %d page(s) at %.3f s, phase %d, "
			"%d of %d in the file's list\n", page0, page0 + count - 1, count,
			part.param[2], (int)part.param[0], listed, count);
	}

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

				if (part.material == 15)
				{
					for (int pass = 0; pass < 3; pass++)
						if (part.pass[pass].count)
							c.bookPages.insert(partFirstPage(trg, part, pass));

					c.bookPart = &part;
				}

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

			// A material 15 tuple is a book: one run per first page.
			if (at->second.bookPart)
				for (int page0 : at->second.bookPages)
					printBook(trg, *at->second.bookPart, page0);
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

				/* A material 15 part is a book of pages: its own page and
				 * the pages after it by number (hd_render.cpp,
				 * MaterialBookTime). */
				if (part.material == 15)
					printBook(trg, part, page);
			}

			nr++;
		}
	}

	/* The height of one triangle's plane at x, z, where the triangle's x/z
	 * projection covers that point: barycentric weights, an edge counting as
	 * inside to 1e-6 of a weight. False where the point lies outside or the
	 * triangle stands edge on (no area in x/z). */
	bool heightAt(const trf::remaster::TrgVertex& a,
		const trf::remaster::TrgVertex& b, const trf::remaster::TrgVertex& c,
		double px, double pz, double& y)
	{
		const double d = (double)(b.z - c.z) * (a.x - c.x) +
			(double)(c.x - b.x) * (a.z - c.z);

		if (std::fabs(d) < 1e-9)
			return false;

		const double l0 = ((b.z - c.z) * (px - c.x) +
			(c.x - b.x) * (pz - c.z)) / d;
		const double l1 = ((c.z - a.z) * (px - c.x) +
			(a.x - c.x) * (pz - c.z)) / d;
		const double l2 = 1.0 - l0 - l1;

		if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6)
			return false;

		y = l0 * a.y + l1 * b.y + l2 * c.y;
		return true;
	}

	/* -cell: every triangle of one group whose x/z projection covers the
	 * room-local point x, z - its part, material, pass and page, its height
	 * at the point, the y of its geometric normal, its first corner's two
	 * colours and UV, its box and its three heights. What lies on one spot
	 * of a floor, in every pass, when a picture shows a hole there. */
	void dumpCell(const std::string& root, const std::string& stem,
		int group, int px, int pz)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error) || group < 0 ||
			group >= (int)trg.groups.size())
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		std::printf("cell %s group %d at x=%d z=%d\n", stem.c_str(), group,
			px, pz);
		int nr = 0;

		for (const trf::remaster::TrgPart& part :
			trg.groups[(size_t)group].parts)
		{
			for (int pass = 0; pass < 3; pass++)
				for (uint32_t i = 0; i + 2 < part.pass[pass].count; i += 3)
				{
					const uint32_t index = part.pass[pass].start + i;

					if (index + 2 >= trg.indices.size())
						break;

					const trf::remaster::TrgVertex& a =
						trg.vertices[trg.indices[index]];
					const trf::remaster::TrgVertex& b =
						trg.vertices[trg.indices[index + 1]];
					const trf::remaster::TrgVertex& c =
						trg.vertices[trg.indices[index + 2]];
					double y = 0.0;

					if (!heightAt(a, b, c, px, pz, y))
						continue;

					const double ux = b.x - a.x, uy = b.y - a.y,
						uz = b.z - a.z;
					const double vx = c.x - a.x, vy = c.y - a.y,
						vz = c.z - a.z;
					const double nx = uy * vz - uz * vy;
					const double ny = uz * vx - ux * vz;
					const double nz = ux * vy - uy * vx;
					const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
					const int page = a.layer &&
						(size_t)a.layer <= trg.textures.size() ?
						(int)trg.textures[a.layer - 1] : -1;
					const int bx0 = std::min(std::min(a.x, b.x), c.x);
					const int bx1 = std::max(std::max(a.x, b.x), c.x);
					const int bz0 = std::min(std::min(a.z, b.z), c.z);
					const int bz1 = std::max(std::max(a.z, b.z), c.z);

					std::printf("  part %2d mat %2u pass %d tri %5u page %5d"
						" y %8.1f ny %+.3f col1 %3u %3u %3u col2 %3u %3u %3u"
						" uv %3u,%3u box x %d..%d z %d..%d ys %d %d %d\n",
						nr, part.material, pass, i / 3, page, y,
						nl > 0 ? ny / nl : 0.0, a.colour1[0], a.colour1[1],
						a.colour1[2], a.colour2[0], a.colour2[1],
						a.colour2[2], a.u, a.v, bx0, bx1, bz0, bz1, a.y, b.y,
						c.y);
				}

			nr++;
		}
	}

	/* -box: every part of one group, or of every group for -1, with a
	 * triangle corner in a room-local box - per part and pass the triangles
	 * with a corner inside and those with their centroid inside, the corners'
	 * heights (each with its count, the most frequent first, twelve at most),
	 * the pages those corners wear and the means of their two colours. What
	 * the art holds where a static or an item stands, when the static table
	 * or the map says the art has it, and what colour the art is where a
	 * picture shows black. The box is inclusive on every side. */
	void dumpBox(const std::string& root, const std::string& stem,
		int group, int x0, int x1, int y0, int y1, int z0, int z1)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error) ||
			group >= (int)trg.groups.size())
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		std::printf("box %s group %d x %d..%d y %d..%d z %d..%d\n",
			stem.c_str(), group, x0, x1, y0, y1, z0, z1);

		const auto inside = [&](double x, double y, double z)
		{
			return x >= x0 && x <= x1 && y >= y0 && y <= y1 &&
				z >= z0 && z <= z1;
		};
		const int g0 = group < 0 ? 0 : group;
		const int g1 = group < 0 ? (int)trg.groups.size() - 1 : group;

		for (int g = g0; g <= g1; g++)
		{
			int nr = 0;

			for (const trf::remaster::TrgPart& part :
				trg.groups[(size_t)g].parts)
			{
				for (int pass = 0; pass < 3; pass++)
				{
					int corner = 0, centroid = 0, corners = 0;
					long long c1[3] = { 0, 0, 0 }, c2[3] = { 0, 0, 0 };
					std::map<int, int> heights;
					std::map<int, int> pages;

					for (uint32_t i = 0; i + 2 < part.pass[pass].count; i += 3)
					{
						const uint32_t index = part.pass[pass].start + i;

						if (index + 2 >= trg.indices.size())
							break;

						double cx = 0.0, cy = 0.0, cz = 0.0;
						int in = 0;

						for (int k = 0; k < 3; k++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[index + (uint32_t)k]];

							cx += v.x;
							cy += v.y;
							cz += v.z;

							if (!inside(v.x, v.y, v.z))
								continue;

							in++;
							corners++;
							heights[v.y]++;
							pages[v.layer && (size_t)v.layer <= trg.textures.size() ?
								(int)trg.textures[v.layer - 1] : -1]++;

							for (int c = 0; c < 3; c++)
							{
								c1[c] += v.colour1[c];
								c2[c] += v.colour2[c];
							}
						}

						if (in)
							corner++;

						if (inside(cx / 3.0, cy / 3.0, cz / 3.0))
							centroid++;
					}

					if (!corner && !centroid)
						continue;

					std::printf("  group %3d part %2d mat %2u pass %d"
						" tris corner %d centroid %d of %u  corners %d"
						"  c1 mean %3lld %3lld %3lld  c2 mean %3lld %3lld %3lld\n",
						g, nr, part.material, pass, corner, centroid,
						part.pass[pass].count / 3, corners,
						corners ? c1[0] / corners : 0, corners ? c1[1] / corners : 0,
						corners ? c1[2] / corners : 0, corners ? c2[0] / corners : 0,
						corners ? c2[1] / corners : 0, corners ? c2[2] / corners : 0);

					std::vector<std::pair<int, int> > byCount(heights.begin(),
						heights.end());

					std::sort(byCount.begin(), byCount.end(),
						[](const std::pair<int, int>& a, const std::pair<int, int>& b)
						{
							return a.second != b.second ? a.second > b.second :
								a.first < b.first;
						});
					std::printf("            heights:");

					for (size_t h = 0; h < byCount.size() && h < 12; h++)
						std::printf(" %d(%d)", byCount[h].first, byCount[h].second);

					std::printf("%s\n", byCount.size() > 12 ? " ..." : "");
					std::printf("            pages:");

					for (std::map<int, int>::const_iterator at = pages.begin();
						at != pages.end(); ++at)
						std::printf(" %d(%d)", at->first, at->second);

					std::printf("\n");
				}

				nr++;
			}
		}
	}

	/* -boxes: many boxes out of a file, one `label group x0 x1 y0 y1 z0 z1`
	 * a line (group -1: every group), the file read once; per line one
	 * `label group corner centroid corners` - the triangles of that group
	 * with a corner in the box, those with their centroid in it, and the
	 * corners - the groups with nothing in the box left out. The sweep over
	 * every item of a level asks this: does the art stand where the item
	 * stands? */
	void dumpBoxes(const std::string& root, const std::string& stem,
		const char* file)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		FILE* in = std::fopen(file, "r");

		if (!in)
		{
			std::printf("%-9s FAILED cannot open %s\n", stem.c_str(), file);
			return;
		}

		char line[512];

		while (std::fgets(line, sizeof(line), in))
		{
			char label[128];
			int group, box[6];

			if (std::sscanf(line, "%127s %d %d %d %d %d %d %d", label, &group,
				&box[0], &box[1], &box[2], &box[3], &box[4], &box[5]) != 8)
				continue;

			if (group >= (int)trg.groups.size())
				continue;

			const auto inside = [&](double x, double y, double z)
			{
				return x >= box[0] && x <= box[1] && y >= box[2] &&
					y <= box[3] && z >= box[4] && z <= box[5];
			};
			const int g0 = group < 0 ? 0 : group;
			const int g1 = group < 0 ? (int)trg.groups.size() - 1 : group;

			for (int g = g0; g <= g1; g++)
			{
				int corner = 0, centroid = 0, corners = 0;

				for (const trf::remaster::TrgPart& part :
					trg.groups[(size_t)g].parts)
					for (int pass = 0; pass < 3; pass++)
						for (uint32_t i = 0; i + 2 < part.pass[pass].count; i += 3)
						{
							const uint32_t index = part.pass[pass].start + i;

							if (index + 2 >= trg.indices.size())
								break;

							double cx = 0.0, cy = 0.0, cz = 0.0;
							int in = 0;

							for (int k = 0; k < 3; k++)
							{
								const trf::remaster::TrgVertex& v =
									trg.vertices[trg.indices[index + (uint32_t)k]];

								cx += v.x;
								cy += v.y;
								cz += v.z;

								if (inside(v.x, v.y, v.z))
									in++;
							}

							corners += in;

							if (in)
								corner++;

							if (inside(cx / 3.0, cy / 3.0, cz / 3.0))
								centroid++;
						}

				if (corner || centroid)
					std::printf("%s %d %d %d %d\n", label, g, corner, centroid,
						corners);
			}
		}

		std::fclose(in);
	}

	/* -layers: the file's texture list - which page each one-based layer
	 * number names. A vertex names its page by layer, and so does a part
	 * whose material carries layer numbers in its parameters (material 13's
	 * flipbook frames); this is the table both read. */
	void dumpLayers(const std::string& root, const std::string& stem)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		std::printf("layers %s: %u\n", stem.c_str(),
			(unsigned)trg.textures.size());

		for (size_t i = 0; i < trg.textures.size(); i++)
			std::printf("  layer %3u -> page %d\n", (unsigned)(i + 1),
				(int)trg.textures[i]);
	}

	/* -squares: per floor square of one group, per part and pass, how many
	 * of 8 x 8 sample points (the centres of a 128-unit grid in the 1024
	 * square) a triangle covers whose height at the point lies within
	 * y0..y1, the lowest and highest such height and the pages - whether a
	 * square is wholly covered at a height, and by what. Squares sx0..sx1,
	 * sz0..sz1, room local. */
	void dumpSquares(const std::string& root, const std::string& stem,
		int group, int y0, int y1, int sx0, int sx1, int sz0, int sz1)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error) || group < 0 ||
			group >= (int)trg.groups.size())
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		std::printf("squares %s group %d heights %d..%d\n", stem.c_str(),
			group, y0, y1);

		for (int sx = sx0; sx <= sx1; sx++)
			for (int sz = sz0; sz <= sz1; sz++)
			{
				int nr = 0;
				bool any = false;

				std::printf(" square %d,%d:", sx, sz);

				for (const trf::remaster::TrgPart& part :
					trg.groups[(size_t)group].parts)
				{
					for (int pass = 0; pass < 3; pass++)
					{
						int hits = 0;
						double lo = 1e9, hi = -1e9;
						std::map<int, int> pages;

						for (int k = 0; k < 64; k++)
						{
							const double px = sx * 1024 + 64 + (k & 7) * 128;
							const double pz = sz * 1024 + 64 + (k >> 3) * 128;
							bool hit = false;

							for (uint32_t i = 0;
								i + 2 < part.pass[pass].count && !hit; i += 3)
							{
								const uint32_t index =
									part.pass[pass].start + i;

								if (index + 2 >= trg.indices.size())
									break;

								const trf::remaster::TrgVertex& a =
									trg.vertices[trg.indices[index]];
								const trf::remaster::TrgVertex& b =
									trg.vertices[trg.indices[index + 1]];
								const trf::remaster::TrgVertex& c =
									trg.vertices[trg.indices[index + 2]];
								double y = 0.0;

								if (!heightAt(a, b, c, px, pz, y) || y < y0 ||
									y > y1)
									continue;

								hit = true;
								lo = y < lo ? y : lo;
								hi = y > hi ? y : hi;

								if (a.layer &&
									(size_t)a.layer <= trg.textures.size())
									pages[(int)trg.textures[a.layer - 1]]++;
							}

							if (hit)
								hits++;
						}

						if (!hits)
							continue;

						any = true;
						std::printf(" [p%d m%u s%d %d/64 y%.0f..%.0f pg", nr,
							part.material, pass, hits, lo, hi);

						for (const std::pair<const int, int>& pg : pages)
							std::printf(" %d", pg.first);

						std::printf("]");
					}

					nr++;
				}

				std::printf("%s\n", any ? "" : " none");
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
	 * Material 16 could be an additive sheet whose weight sits in param[2]
	 * or a still-water surface. The geometry is the one thing that can
	 * decide it (hd_render.cpp, HDWaterArt, has the answer). A water surface
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
		/* And the heights its corners span, two per triangle, room local:
		 * the air is asked of them (HDRoomAirReaches), as AirHostsOf in
		 * hd_render.cpp asks it. */
		std::vector<std::vector<float> > span(trg.groups.size());

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
						long lo = 0, hi = 0;

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[first + c]];

							qx += v.x;
							qy += v.y;
							qz += v.z;

							if (c == 0 || v.y < lo)
								lo = v.y;

							if (c == 0 || v.y > hi)
								hi = v.y;
						}

						out.push_back((float)qx / 3.0f);
						out.push_back((float)qy / 3.0f);
						out.push_back((float)qz / 3.0f);
						span[g].push_back((float)lo);
						span[g].push_back((float)hi);
					}
				}
		}

		/* Every room's air (HDRoomAir in specific\hd\hd_render.h), as
		 * BuildRoomAir hands it to the renderer: per floor square its floor
		 * and ceiling bytes and its floor data words from its index on,
		 * read by HDSectorAir. And the same squares read the randomizer's
		 * way - Tr5Level::cornerFloor and cornerCeiling at the four corners,
		 * a portal square out - the randomizer's reading, held against
		 * HDSectorAir; `airDiffer` is the squares where the two part. */
		std::vector<HDRoomAir> air((size_t)rooms);
		std::vector<std::vector<float> > airSquares((size_t)rooms);
		int airSquareCount = 0, airWith = 0, airDiffer = 0;
		// The rooms the air is not asked about: next door, a floor portal.
		std::vector<std::set<int> > airLinked = nextDoor;

		for (int r = 0; r < rooms; r++)
		{
			const RoomSpace& b = box[(size_t)r];
			std::vector<Tr5Level::SectorInfo> sect;
			std::vector<float>& squares = airSquares[(size_t)r];

			squares.clear();

			for (int q = 0; q < b.numX * b.numZ; q++)
			{
				squares.push_back(HD_AIR_NONE);
				squares.push_back(-HD_AIR_NONE);
			}

			if (level.sectorInfo(r, sect, error))
				for (size_t q = 0; q < sect.size(); q++)
				{
					const Tr5Level::SectorInfo& si = sect[q];
					const size_t at = ((size_t)si.sx * b.numZ + si.sz) * 2;
					std::vector<Tr5FloorEntry> chain;
					std::vector<unsigned short> words;
					bool readable = true;

					if (si.fdIndex && !level.readChain(si.fdIndex, chain))
						readable = false;

					for (size_t e = 0; e < chain.size(); e++)
						for (size_t w = 0; w < chain[e].words.size(); w++)
							words.push_back(chain[e].words[w]);

					float top = HD_AIR_NONE, bottom = -HD_AIR_NONE;
					int door = -1;

					if (readable && at + 1 < squares.size())
						HDSectorAir(si.floor, si.ceiling,
							words.empty() ? 0 : &words[0], (int)words.size(),
							&top, &bottom, &door);

					if (door >= 0 && door < rooms)
					{
						airLinked[(size_t)r].insert(door);
						airLinked[(size_t)door].insert(r);
					}

					if (at + 1 < squares.size())
					{
						squares[at] = top;
						squares[at + 1] = bottom;
					}

					// The randomizer's reading of the same square.
					long f[4], c[4];
					bool corners = si.doorTo < 0;

					for (int k = 0; k < 4 && corners; k++)
						corners = level.cornerFloor(r, si.sx, si.sz,
							k < 2 ? 0 : 1024, (k & 1) ? 1024 : 0, f[k]) &&
							level.cornerCeiling(r, si.sx, si.sz,
							k < 2 ? 0 : 1024, (k & 1) ? 1024 : 0, c[k]);

					float top2 = HD_AIR_NONE, bottom2 = -HD_AIR_NONE;

					if (corners)
					{
						const long fMin = std::min(std::min(f[0], f[1]),
							std::min(f[2], f[3]));
						const long fMax = std::max(std::max(f[0], f[1]),
							std::max(f[2], f[3]));
						const long cMin = std::min(std::min(c[0], c[1]),
							std::min(c[2], c[3]));
						const long cMax = std::max(std::max(c[0], c[1]),
							std::max(c[2], c[3]));

						if (fMax > cMin)
						{
							top2 = (float)cMax;
							bottom2 = (float)fMin;
						}
					}

					airSquareCount++;
					airWith += top < bottom ? 1 : 0;
					airDiffer += top != top2 || bottom != bottom2 ||
						door != si.doorTo ? 1 : 0;
				}

			air[(size_t)r].x = (float)b.ox;
			air[(size_t)r].y = (float)b.oy;
			air[(size_t)r].z = (float)b.oz;
			air[(size_t)r].nx = b.numX;
			air[(size_t)r].nz = b.numZ;
			air[(size_t)r].sectors = squares.empty() ? 0 : &squares[0];
		}

		/* The pairs not asked, row by the art's room, as BuildRoomAir makes
		 * them: the same room, next door, a floor portal, flipmap twins, and
		 * closed over the twins. */
		std::vector<unsigned char> airLink((size_t)rooms * (size_t)rooms, 0);
		std::vector<int> airTwin((size_t)rooms, -1);

		for (int b = 0; b < rooms; b++)
			for (int a = 0; a < rooms; a++)
				if (a == b || airLinked[(size_t)a].count(b) ||
					level.rooms()[(size_t)a].altRoom == b ||
					level.rooms()[(size_t)b].altRoom == a)
					airLink[(size_t)b * rooms + a] = 1;

		for (int r = 0; r < rooms; r++)
		{
			const int alt = level.rooms()[(size_t)r].altRoom;

			if (alt >= 0 && alt < rooms)
			{
				airTwin[(size_t)r] = alt;
				airTwin[(size_t)alt] = r;
			}
		}

		{
			const std::vector<unsigned char> direct = airLink;

			for (int b = 0; b < rooms; b++)
				for (int a = 0; a < rooms; a++)
				{
					const int tb = airTwin[(size_t)b];
					const int ta = airTwin[(size_t)a];

					if ((ta >= 0 && direct[(size_t)b * rooms + ta]) ||
						(tb >= 0 && direct[(size_t)tb * rooms + a]) ||
						(ta >= 0 && tb >= 0 && direct[(size_t)tb * rooms + ta]))
						airLink[(size_t)b * rooms + a] = 1;
				}
		}

		/* Whether triangle t of room b reaches into the air of room a that
		 * b has no link with (HDRoomAirReaches): the index fill's second
		 * way into a's box, and the air test of air_tris below. */
		const auto airReach = [&](int b, size_t t, int a) -> bool
		{
			if (airLink[(size_t)b * rooms + (size_t)a])
				return false;

			const float* c = &centre[(size_t)b][t * 3];
			const float* s = &span[(size_t)b][t * 2];

			return HDRoomAirReaches(air[(size_t)a], c[0] + air[(size_t)b].x,
				s[0] + air[(size_t)b].y, s[1] + air[(size_t)b].y,
				c[2] + air[(size_t)b].z, HD_AIR_MARGIN);
		};

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

		/* The classic faces the intruding triangles belong to - see
		 * HDPairFace in specific\hd\hd_render.h - per intruder room, and
		 * what they claimed, counted the way ReportRoomIntrusions counts
		 * it for the engine's log. */
		std::vector<std::vector<HDPairFace> > faces((size_t)rooms);
		std::vector<unsigned char> facesRead((size_t)rooms, 0);
		std::vector<std::set<size_t> > claimed((size_t)rooms);
		long long boundTris = 0, unboundTris = 0;
		// The ones no face claims outside the smaller box: the open half's.
		long long bandTris = 0;

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

				/* A's inside - TR5's own box, one sector in from the grid's
				 * edge - as BuildRoomIntrusions hands it to the renderer.
				 *
				 * A room's border ring is one sector thick, so two rooms that
				 * merely share a wall with no door between them reach into
				 * each other by construction: ANDREA1 has 158 pairs with this
				 * box, among them 2 and 197 at the Piazza dell'Opera, against
				 * seven with one sector more taken off. The box alone does not
				 * tell a shared wall from a room standing inside another; the
				 * classic faces the art belongs to decide per frame which of
				 * it is drawn whole (HDPairFace, DecidePairs in hd_level.cpp),
				 * and this listing says which faces those are.
				 *
				 * `deep` is an extra shrink for re-measuring the rule and is
				 * 0 by default; -deep 1024 asks the box one sector smaller,
				 * which windows every triangle it holds. */
				const RoomSpace& B = box[(size_t)b];
				const float x0 = (float)(A.ix0 + deep - B.ox);
				const float x1 = (float)(A.ix1 - deep - B.ox);
				const float z0 = (float)(A.iz0 + deep - B.oz);
				const float z1 = (float)(A.iz1 - deep - B.oz);
				const float y0 = (float)(A.y0 - B.oy);
				const float y1 = (float)(A.y1 - B.oy);

				if (x1 <= x0 || z1 <= z0)
					continue;	// a room too small to have an inside

				int hits = 0;

				/* In the box by the centroid, or by the centroid's x and z
				 * with the triangle reaching into a's air: the engine's
				 * index fill takes both (hd_render.cpp, the pair_at loop). */
				const auto holds = [&](size_t c) -> bool
				{
					const float cx = centre[(size_t)b][c];
					const float cy = centre[(size_t)b][c + 1];
					const float cz = centre[(size_t)b][c + 2];

					if (!(cx >= x0 && cx <= x1 && cz >= z0 && cz <= z1))
						return false;

					return (cy >= y0 && cy <= y1) || airReach(b, c / 3, a);
				};

				for (size_t c = 0; c + 2 < centre[(size_t)b].size(); c += 3)
					if (holds(c))
						hits++;

				if (hits < shareMin)
					continue;

				pairs++;
				windowed[(size_t)b] = 1;		// the INTRUDER only

				/* b's classic faces in its own frame, read once per room,
				 * the whole list in the roomlets' order. The engine hands
				 * the renderer only those within HD_PAIR_FACE_REACH of one
				 * of b's boxes, which cannot change which face is nearest
				 * to a centroid inside a box. */
				if (!facesRead[(size_t)b])
				{
					facesRead[(size_t)b] = 1;

					std::vector<Tr5Level::FaceInfo> info;

					if (level.roomFaces(b, info, error))
						for (size_t f = 0; f < info.size(); f++)
						{
							HDPairFace face;

							face.group = b;
							face.corners = info[f].quad ? 4 : 3;

							for (int c = 0; c < 4; c++)
							{
								const int at = c < face.corners ? c : 0;

								face.v[c][0] = (float)(info[f].v[at][0] - B.ox);
								face.v[c][1] = (float)info[f].v[at][1];
								face.v[c][2] = (float)(info[f].v[at][2] - B.oz);
							}

							faces[(size_t)b].push_back(face);
						}
				}

				/* A triangle belongs to the first pair whose box holds it,
				 * as in the engine's build, and there to the nearest
				 * classic face of b that claims it. */
				std::set<size_t> pairFaces;
				int pairBound = 0;

				for (size_t c = 0, t = 0; c + 2 < centre[(size_t)b].size();
					c += 3, t++)
				{
					const float cx = centre[(size_t)b][c];
					const float cy = centre[(size_t)b][c + 1];
					const float cz = centre[(size_t)b][c + 2];

					if (!holds(c))
						continue;

					if (tagged[(size_t)b][t])
						continue;

					tagged[(size_t)b][t] = 1;

					const float p[3] = { cx, cy, cz };
					const std::vector<HDPairFace>& list = faces[(size_t)b];
					double nearest = 0.0;
					long at = -1;

					for (size_t f = 0; f < list.size(); f++)
					{
						const double d = HDPairFaceDistance(list[f], p);

						if (d >= 0.0 && d <= HD_PAIR_FACE_REACH &&
							(at < 0 || d < nearest))
						{
							at = (long)f;
							nearest = d;
						}
					}

					/* No face claims it: windowed only when it also stands
					 * in the box one sector smaller in x and z, as the
					 * engine's index fill asks; otherwise the pair has it
					 * and draws it open (2). */
					if (at < 0)
					{
						if (cx < (float)(A.ix0 + 1024 - B.ox) ||
							cx > (float)(A.ix1 - 1024 - B.ox) ||
							cz < (float)(A.iz0 + 1024 - B.oz) ||
							cz > (float)(A.iz1 - 1024 - B.oz))
						{
							tagged[(size_t)b][t] = 2;
							bandTris++;
						}
						else
							unboundTris++;

						continue;
					}

					pairFaces.insert((size_t)at);
					pairBound++;
					boundTris++;
					claimed[(size_t)b].insert((size_t)at);
				}

				if (!quiet)
					std::printf("  overlap: room %3d holds the art of room "
						"%3d - %d triangles, no portal between them "
						"faces=%d bound=%d\n", a, b, hits,
						(int)pairFaces.size(), pairBound);
			}

		int windowedRooms = 0;

		for (int r = 0; r < rooms && (size_t)r < tagged.size(); r++)
		{
			long long here = 0;

			for (size_t t = 0; t < tagged[(size_t)r].size(); t++)
				here += tagged[(size_t)r][t] == 1 ? 1 : 0;

			// Windowed is what the engine windows: a room with a tagged triangle.
			windowed[(size_t)r] = here ? 1 : 0;

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

		int facesClaimed = 0;

		for (size_t r = 0; r < claimed.size(); r++)
			facesClaimed += (int)claimed[r].size();

		/* The holder token names the box the pairs were measured with: the
		 * engine's own inside, or with -deep the box that many units in from
		 * it - inside-1024 is the box one sector smaller - so that a listing
		 * made with -deep is never mistaken for the engine's. */
		char holder[32] = "inside";

		if (deep)
			std::snprintf(holder, sizeof holder, "inside%+ld", -deep);

		/* band_unclaimed: the triangles no face claims that stand outside
		 * the smaller box, drawn with the open half - the engine's HDLOG
		 * level line ends with the same number. */
		/* Art in the air of a room its own room has no link with, not even
		 * through a flipmap twin (HDRoomAir), as the engine's build asks it of
		 * every triangle of every room's group: per (group, host) the
		 * triangles - the HDLOG unlinked lines, in the same order -, and
		 * each triangle once for air_tris. `outside` is the art no entry's
		 * box holds, which the build makes pieces of its own; listed per
		 * pair with the reason. `squares` are the floor squares read and
		 * `differ` those where HDSectorAir and the randomizer's corners
		 * part. `air_boxed` counts the triangles of air_tris some entry's
		 * box holds - the first pair above has them - as the build counts
		 * them before a stand range or a static's box takes any: the art
		 * the engine holds to its own room's window. */
		long long airTris = 0, outsideTris = 0, airBoxed = 0;
		std::vector<long long> outsidePerGroup(centre.size(), 0);
		std::vector<long long> coveredPerGroup(centre.size(), 0);
		std::map<std::pair<int, int>, long long> airPairs, outsidePairs;
		std::set<int> airGroupSet;

		for (size_t g = 0; g < centre.size() && (int)g < rooms; g++)
			for (size_t c = 0; c + 2 < centre[g].size(); c += 3)
			{
				bool any = false, missed = false, covered = false;

				for (int a = 0; a < rooms; a++)
				{
					if (!airReach((int)g, c / 3, a))
						continue;

					any = true;
					airPairs[std::make_pair((int)g, a)]++;

					// The entry's box, as BuildRoomIntrusions makes it.
					const RoomSpace& A = box[(size_t)a];
					const RoomSpace& B = box[g];
					const bool entry =
						boxOverlap(A.x0, A.x1, B.x0, B.x1) &&
						boxOverlap(A.z0, A.z1, B.z0, B.z1) &&
						boxOverlap(A.y0, A.y1, B.y0, B.y1) &&
						A.ix1 > A.ix0 && A.iz1 > A.iz0;
					// In a's air, the box holds it by its centroid's x and z.
					const float cx = centre[g][c];
					const float cz = centre[g][c + 2];
					const bool held = entry &&
						cx >= (float)(A.ix0 - B.ox) &&
						cx <= (float)(A.ix1 - B.ox) &&
						cz >= (float)(A.iz0 - B.oz) &&
						cz <= (float)(A.iz1 - B.oz);

					if (!held)
					{
						missed = true;
						outsidePairs[std::make_pair((int)g, a)]++;

						// The host's own art in the centroid's 256-unit cell.
						const unsigned long long key =
							((unsigned long long)(((long)floorf(cx +
							(float)B.ox) >> 8) + 32768) << 34) |
							((unsigned long long)(((long)floorf(
							centre[g][c + 1] + (float)B.oy) >> 8) + 32768)
							<< 17) |
							(unsigned long long)(((long)floorf(cz +
							(float)B.oz) >> 8) + 32768);

						if (std::binary_search(cells[(size_t)a].begin(),
							cells[(size_t)a].end(), key) ||
							std::binary_search(shell[(size_t)a].begin(),
							shell[(size_t)a].end(), key))
							covered = true;
					}
				}

				if (any)
				{
					airTris++;
					airGroupSet.insert((int)g);

					if (g < tagged.size() && c / 3 < tagged[g].size() &&
						tagged[g][c / 3])
						airBoxed++;
				}

				outsideTris += missed ? 1 : 0;
				outsidePerGroup[g] += missed ? 1 : 0;
				coveredPerGroup[g] += covered ? 1 : 0;
			}

		for (std::map<std::pair<int, int>, long long>::const_iterator it =
			airPairs.begin(); it != airPairs.end(); ++it)
			std::printf("unlinked level=%s group=%d host=%d tris=%lld\n",
				stem.c_str(), it->first.first, it->first.second, it->second);

		/* air_tris air_groups air_pairs air_boxed: the engine's level line
		 * ends with the same four (ReportRoomIntrusions) and air=, the
		 * setting the level took; its air_ms is a time and has no
		 * counterpart here. */
		std::printf("%-9s rooms=%d groups=%d  overlap pairs=%d intruding "
			"tris=%lld in %d groups  overhang tris=%lld of %lld  worst=%ld "
			"(group %d)  holder=%s faces=%d bound=%lld unbound=%lld "
			"band_unclaimed=%lld air_tris=%lld air_groups=%d air_pairs=%d "
			"air_boxed=%lld\n",
			stem.c_str(), rooms, (int)trg.groups.size(), pairs, intruding,
			windowedRooms, tris_out, tris_total, worst, worst_group, holder,
			facesClaimed, boundTris, unboundTris, bandTris, airTris,
			(int)airGroupSet.size(), (int)airPairs.size(), airBoxed);

		{
			std::printf("%-9s   unlinked air outside every entry's box: "
				"tris=%lld pairs=%d  squares=%d with_air=%d differ=%d\n",
				stem.c_str(),
				outsideTris, (int)outsidePairs.size(), airSquareCount, airWith,
				airDiffer);

			for (std::map<std::pair<int, int>, long long>::const_iterator it =
				outsidePairs.begin(); it != outsidePairs.end(); ++it)
			{
				const RoomSpace& A = box[(size_t)it->first.second];
				const RoomSpace& B = box[(size_t)it->first.first];
				const char* why =
					!boxOverlap(A.y0, A.y1, B.y0, B.y1) ? "the boxes part in y"
					: !(A.ix1 > A.ix0 && A.iz1 > A.iz0) ? "no inside"
					: "outside the inside";

				std::printf("  unlinked outside: group %3d host %3d tris %lld "
					"(%s)\n", it->first.first, it->first.second, it->second,
					why);
			}

			/* The hull census, per group with art in unlinked air outside
			 * every entry's box: its triangles, those whose centroid stands
			 * outside its own room's inside (the box below, no slack), and
			 * that art - what tomb5.hd_air 2 asks of a group (the engine's
			 * hull line, BuildRoomAir). */
			for (size_t g = 0; g < outsidePerGroup.size() &&
				g < triPerGroup.size(); g++)
				if (outsidePerGroup[g])
					std::printf("  hull census: level=%s group=%d tris=%lld "
						"outside=%lld share=%.3f unboxed_air=%lld "
						"host_art=%lld\n",
						stem.c_str(), (int)g, triPerGroup[g],
						outAllPerGroup[g], triPerGroup[g] ? (double)
						outAllPerGroup[g] / (double)triPerGroup[g] : 0.0,
						outsidePerGroup[g], coveredPerGroup[g]);
		}
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

	/* The classic floor squares of a level, as hd_level.cpp's
	 * BuildRoomSectors hands them to the renderer: x-major per room, which
	 * is the file's own order. False without a -classic folder or a file. */
	bool loadGrid(const std::string& folder, const std::string& stem,
		std::vector<HDRoomGrid>& grids, std::vector<HDSectorFloor>& sectors)
	{
		grids.clear();
		sectors.clear();

		if (folder.empty())
			return false;

		Tr5Level level;
		std::string error;

		if (!level.load(toW(folder + "\\" + stem + ".TRC"), error))
			return false;

		grids.resize((size_t)level.roomCount());

		for (int i = 0; i < level.roomCount(); i++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)i];
			std::vector<Tr5Level::SectorInfo> squares;
			HDRoomGrid& grid = grids[(size_t)i];

			grid.first = (int)sectors.size();
			grid.xSectors = 0;
			grid.zSectors = 0;
			grid.originX = (float)rm.originX;
			grid.originZ = (float)rm.originZ;

			if (!level.sectorInfo(i, squares, error) ||
				squares.size() != (size_t)rm.numXSectors * rm.numZSectors)
				continue;

			grid.xSectors = rm.numXSectors;
			grid.zSectors = rm.numZSectors;

			for (size_t s = 0; s < squares.size(); s++)
			{
				HDSectorFloor f;

				f.floorY = (short)(squares[s].floor * 256);
				f.fx = (unsigned char)(squares[s].box & 15);
				f.wall = squares[s].floor == -127 ? 1 : 0;
				sectors.push_back(f);
			}
		}

		return true;
	}

	/* -gloss and -glossseed: the sheen's page classes of a level, with the
	 * engine's own seed walk (hd_gloss_seed.h), page table (hd_gloss_pages.h)
	 * and resolver (hd_gloss_math.h).
	 *
	 * -gloss prints every page of the rooms' surface passes that shines, by
	 * where its gloss comes from - the material, the file, the seed or the
	 * table - with the numbers the sheen draws it with, and the triangles.
	 * -glossseed prints the seed's evidence instead: per page with a vote,
	 * the up-facing triangles over each kind of floor square and the verdict,
	 * then the pages numbered within three of each metal, marble or ice
	 * candidate, which is where a level keeps the rest of a surface's set
	 * (the crate's side next to its front) - the list the page table was
	 * first written from.
	 *
	 * What the tool cannot see is the engine's second bar, a page part-way
	 * clear in every texel (HD_PAGE_CLEAR), which needs the page decoded:
	 * those are the floors of the alpha pass and never glossy there. */
	struct GlossUse
	{
		int tris = 0;		// surface-pass triangles on the page
		int matTris = 0;	// of those, on a material 3 or 17 part
		HDGloss mat;
		unsigned int matBase = 0;
	};

	void dumpGloss(const std::string& root, const std::string& stem,
		const std::string& classic, bool seedOnly, const HDGlossFile& file)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		std::vector<HDRoomGrid> grids;
		std::vector<HDSectorFloor> sectors;
		const bool haveGrid = loadGrid(classic, stem, grids, sectors);
		const int rooms = haveGrid ? (int)grids.size() : (int)trg.groups.size();
		std::unordered_map<int, HDGlossVotes> votes;

		if (haveGrid)
			HDGlossSeedWalk(trg, rooms, &grids[0], (int)grids.size(),
				sectors.empty() ? 0 : &sectors[0], (int)sectors.size(), votes);

		std::map<int, GlossUse> use;

		for (size_t g = 0; g < trg.groups.size() && (int)g < rooms; g++)
			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			{
				const unsigned int base = HDGlossBaseMaterial(part.material);
				HDGloss mat;
				const bool hasMat = HDGlossFromMaterial(part.material, part.param, &mat);

				for (int pass = 0; pass < 2; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned int at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned int index = trg.indices[run.start + at];

						if (index >= trg.vertices.size())
							continue;

						const unsigned char layer = trg.vertices[index].layer;

						if (!layer || (size_t)layer > trg.textures.size())
							continue;

						GlossUse& u = use[(int)trg.textures[layer - 1]];

						u.tris++;

						if (hasMat)
						{
							u.matTris++;
							u.mat = mat;
							u.matBase = base;
						}
					}
				}
			}

		long lamps = 0;
		int voted = 0;

		for (size_t g = 0; g < trg.groups.size() && (int)g < rooms; g++)
			lamps += (long)trg.groups[g].volumes.size();

		for (const auto& kv : votes)
		{
			int any = 0;

			for (int k = 0; k < 16; k++)
				any += kv.second.perFx[k];

			voted += any ? 1 : 0;
		}

		std::printf("%s: %d surface page(s), %d with a seed vote, table %d page(s), "
			"file %s (%zu page(s), %d bad line(s)), lamps %ld, cubes %zu for %zu "
			"floor squares%s\n", stem.c_str(), (int)use.size(), voted,
			HDGlossTableSize(), file.present ? "read" : "none", file.entries.size(),
			file.bad, lamps, trg.light_cubes.size(), sectors.size(),
			haveGrid ? "" : " (no -classic: no seed, no cubes)");

		if (seedOnly)
		{
			std::set<int> candidates;

			std::printf("  page   tris  metal marble   ice  other  seed     table\n");

			for (const auto& kv : use)
			{
				const std::unordered_map<int, HDGlossVotes>::const_iterator v =
					votes.find(kv.first);

				if (v == votes.end())
					continue;

				const int* fx = v->second.perFx;
				const int metal = fx[8] + fx[13], marble = fx[9], ice = fx[4];
				int other = 0;

				for (int k = 0; k < 16; k++)
					other += fx[k];

				other -= metal + marble + ice;

				if (!metal && !marble && !ice)
					continue;

				const int cls = HDGlossSeedClass(fx, v->second.tris);
				const HDGlossPage* t = HDGlossTableFind(kv.first);

				std::printf("  %5d %6d %6d %6d %5d %6d  %-7s  %s%s\n", kv.first,
					kv.second.tris, metal, marble, ice, other, HDGlossClassName(cls),
					t ? HDGlossClassName(t->cls) : "-",
					HDGlossPageBarred(kv.first) ? "  (barred)" : "");

				if (cls != HD_GLOSS_NONE)
					candidates.insert(kv.first);
			}

			std::printf("  the pages numbered within three of a candidate:\n");

			for (const int page : candidates)
			{
				std::printf("  %5d:", page);

				for (int near = page - 3; near <= page + 3; near++)
				{
					const std::map<int, GlossUse>::const_iterator u = use.find(near);

					if (near == page || u == use.end())
						continue;

					std::printf(" %d(%d tris%s%s)", near, u->second.tris,
						u->second.matTris ? ", material" : "",
						HDGlossSeedOf(votes, near) != HD_GLOSS_NONE ? ", seed" : "");
				}

				std::printf("\n");
			}

			return;
		}

		int bySource[5] = { 0 };

		std::printf("  page   tris  source class     strength  power     F0  (material tris)\n");

		for (const auto& kv : use)
		{
			const int page = kv.first;
			bool removes = false;
			const HDGlossPage* f = file.find(page, &removes);
			const int seed = HDGlossSeedOf(votes, page);
			const HDGlossPage* t = HDGlossTableFind(page);
			const bool barred = HDGlossPageBarred(page);
			const HDGloss plain = HDGlossResolve(0, f, removes, t, seed, barred);

			if (kv.second.tris > kv.second.matTris && plain.strength > 0.0f)
			{
				std::printf("  %5d %6d  %-6s %-8s  %8.3f %6.0f %6.3f\n", page,
					kv.second.tris - kv.second.matTris, HDGlossSourceName(plain.source),
					HDGlossClassName(plain.cls), plain.strength, plain.power, plain.f0);
				bySource[plain.source]++;
			}

			if (kv.second.matTris)
			{
				const HDGloss m = HDGlossResolve(&kv.second.mat, f, removes, t, seed, barred);

				std::printf("  %5d %6d  %-6s %-8s  %8.3f %6.0f %6.3f  (material %u)\n", page,
					kv.second.matTris, HDGlossSourceName(m.source), HDGlossClassName(m.cls),
					m.strength, m.power, m.f0, kv.second.matBase);
				bySource[m.source]++;
			}
		}

		std::printf("  pages by source: mat %d, file %d, seed %d, table %d\n",
			bySource[HD_GLOSS_FROM_MAT], bySource[HD_GLOSS_FROM_FILE],
			bySource[HD_GLOSS_FROM_SEED], bySource[HD_GLOSS_FROM_TABLE]);
	}

	/* -glosslight: the measurements the sheen's lighting rests on
	 * (hd_gloss_math.h and hd_room.hlsl name them), over the .TRG's light
	 * cubes, its point lights and its sun against the classic floor:
	 *
	 *   order  how the cube list runs: the correlation of each floor square's
	 *          cube brightness with the baked light of the .TRG's up-facing
	 *          triangles on that square, the cubes read x-major (the order
	 *          of ROOM_INFO::floor) and z-major; and how many wall squares
	 *          and floor squares carry an all-black cube.
	 *   gain   the median over floor squares of that baked light against
	 *          the cube's mean face (the light over the cube, per channel).
	 *   faces  the correlation of each face's brightness, less the cube's
	 *          mean, with the direction to each lamp between 700 and 4096
	 *          units away from the square (x, y, z of the unit vector).
	 *   kinds  per lamp kind, the height over the floor square under the
	 *          lamp (median, 10th and 90th percentile) and the colour.
	 *   sun    for a level with a sun colour, over the cubes lit from above
	 *          (up face 1.3 times the down face), the mean of each face's
	 *          brightness against its normal along four readings of the
	 *          file's vector: (x, y, z), (-x, y, -z), (x, -y, z), (-x, -y, -z).
	 *
	 * The last line gives the median of the levels' gain medians. */
	double correlation(const std::vector<double>& a, const std::vector<double>& b)
	{
		const size_t n = a.size() < b.size() ? a.size() : b.size();

		if (!n)
			return 0.0;

		double ma = 0.0, mb = 0.0;

		for (size_t i = 0; i < n; i++)
		{
			ma += a[i];
			mb += b[i];
		}

		ma /= n;
		mb /= n;

		double ab = 0.0, aa = 0.0, bb = 0.0;

		for (size_t i = 0; i < n; i++)
		{
			ab += (a[i] - ma) * (b[i] - mb);
			aa += (a[i] - ma) * (a[i] - ma);
			bb += (b[i] - mb) * (b[i] - mb);
		}

		return ab / std::sqrt(aa * bb + 1e-12);
	}

	double glossLight(const std::string& root, const std::string& stem,
		const std::string& classic)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return -1.0;
		}

		std::vector<HDRoomGrid> grids;
		std::vector<HDSectorFloor> sectors;

		if (!loadGrid(classic, stem, grids, sectors))
		{
			std::printf("%-9s needs -classic <folder holding the .TRC>\n", stem.c_str());
			return -1.0;
		}

		const std::vector<std::array<uint8_t, 24> >& cubes = trg.light_cubes;
		std::vector<double> light, xMajor, zMajor, ratio;
		long wallBlack = 0, walls = 0, floorBlack = 0, floors = 0;

		for (size_t g = 0; g < grids.size() && g < trg.groups.size(); g++)
		{
			const HDRoomGrid& room = grids[g];
			const int nx = room.xSectors, nz = room.zSectors;
			std::vector<double> sum((size_t)nx * nz, 0.0);
			std::vector<int> count((size_t)nx * nz, 0);

			for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[0];

				for (unsigned int at = 0; at + 2 < run.count; at += 3)
				{
					const trf::remaster::TrgVertex* v[3];

					for (int k = 0; k < 3; k++)
						v[k] = &trg.vertices[trg.indices[run.start + at + (unsigned int)k]];

					if ((v[0]->normal_y() + v[1]->normal_y() + v[2]->normal_y()) / 3.0f > -0.9f)
						continue;

					const int s = HDGlossSectorAt(room, (v[0]->x + v[1]->x + v[2]->x) / 3.0f,
						(v[0]->z + v[1]->z + v[2]->z) / 3.0f);

					if (s < 0)
						continue;

					double l = 0.0;

					for (int k = 0; k < 3; k++)
						for (int c = 0; c < 3; c++)
							l += v[k]->colour1[c] + v[k]->colour2[c];

					sum[(size_t)(s - room.first)] += l / 9.0;
					count[(size_t)(s - room.first)]++;
				}
			}

			for (int sx = 0; sx < nx; sx++)
				for (int sz = 0; sz < nz; sz++)
				{
					const size_t k = (size_t)room.first + (size_t)sx * nz + sz;
					const size_t k2 = (size_t)room.first + (size_t)sz * nx + sx;

					if (k >= cubes.size() || k2 >= cubes.size() || k >= sectors.size())
						continue;

					double c1 = 0.0, c2 = 0.0;

					for (int q = 0; q < 24; q++)
						if (q % 4 != 3)
						{
							c1 += cubes[k][(size_t)q];
							c2 += cubes[k2][(size_t)q];
						}

					if (sectors[k].wall)
					{
						walls++;
						wallBlack += c1 == 0.0 ? 1 : 0;
					}
					else
					{
						floors++;
						floorBlack += c1 == 0.0 ? 1 : 0;
					}

					const int n = count[(size_t)sx * nz + sz];

					if (!n)
						continue;

					light.push_back(sum[(size_t)sx * nz + sz] / n);
					xMajor.push_back(c1 / 18.0);
					zMajor.push_back(c2 / 18.0);

					if (c1 > 0.0)
						ratio.push_back(light.back() / (c1 / 18.0));
				}
		}

		std::sort(ratio.begin(), ratio.end());

		const double gain = ratio.empty() ? 0.0 : ratio[ratio.size() / 2];

		std::printf("%-9s order x-major %.3f z-major %.3f | black cubes: walls %ld of %ld, "
			"floors %ld of %ld | gain median %.2f (p25 %.2f, p75 %.2f)\n", stem.c_str(),
			correlation(light, xMajor), correlation(light, zMajor), wallBlack, walls,
			floorBlack, floors, gain, ratio.empty() ? 0.0 : ratio[ratio.size() / 4],
			ratio.empty() ? 0.0 : ratio[ratio.size() * 3 / 4]);

		// The faces against the direction to the lamps, and the kinds.
		std::vector<double> dir[3], face[6];
		std::map<unsigned int, std::vector<double> > height, colour;
		std::map<unsigned int, int> away;

		for (size_t g = 0; g < grids.size() && g < trg.groups.size(); g++)
		{
			const HDRoomGrid& room = grids[g];

			for (size_t li = 0; li < trg.groups[g].volumes.size(); li++)
			{
				const trf::remaster::TrgGroup::PointLight lamp = trg.groups[g].light(li);
				const int under = HDGlossSectorAt(room, lamp.x - room.originX,
					lamp.z - room.originZ);

				colour[lamp.kind].push_back((lamp.r + lamp.g + lamp.b) / 3.0);

				if (under < 0 || (size_t)under >= sectors.size() || sectors[(size_t)under].wall)
					away[lamp.kind]++;
				else
					height[lamp.kind].push_back(sectors[(size_t)under].floorY - lamp.y);

				if (lamp.r + lamp.g + lamp.b <= 0.0f)
					continue;

				for (int sx = 0; sx < room.xSectors; sx++)
					for (int sz = 0; sz < room.zSectors; sz++)
					{
						const size_t k = (size_t)room.first + (size_t)sx * room.zSectors + sz;

						if (k >= sectors.size() || k >= cubes.size() || sectors[k].wall)
							continue;

						const double px = room.originX + sx * 1024 + 512;
						const double pz = room.originZ + sz * 1024 + 512;
						const double py = sectors[k].floorY - 512.0;
						const double dx = lamp.x - px, dy = lamp.y - py, dz = lamp.z - pz;
						const double d = std::sqrt(dx * dx + dy * dy + dz * dz);

						if (d < 700.0 || d > 4096.0)
							continue;

						double lum[6], mean = 0.0;

						for (int f = 0; f < 6; f++)
						{
							lum[f] = (double)cubes[k][(size_t)f * 4] + cubes[k][(size_t)f * 4 + 1] +
								cubes[k][(size_t)f * 4 + 2];
							mean += lum[f] / 6.0;
						}

						dir[0].push_back(dx / d);
						dir[1].push_back(dy / d);
						dir[2].push_back(dz / d);

						for (int f = 0; f < 6; f++)
							face[f].push_back(lum[f] - mean);
					}
			}
		}

		std::printf("          faces against the lamps (%zu squares):", dir[0].size());

		for (int f = 0; f < 6; f++)
			std::printf("  f%d x%+.2f y%+.2f z%+.2f", f, correlation(face[f], dir[0]),
				correlation(face[f], dir[1]), correlation(face[f], dir[2]));

		std::printf("\n");

		for (auto& kv : colour)
		{
			std::vector<double>& h = height[kv.first];
			std::vector<double>& c = kv.second;

			std::sort(h.begin(), h.end());
			std::sort(c.begin(), c.end());
			std::printf("          kind %u: %zu over a floor (+%d in a wall or outside), height "
				"median %.0f p10 %.0f p90 %.0f | colour median %.2f max %.2f\n", kv.first,
				h.size(), away[kv.first], h.empty() ? 0.0 : h[h.size() / 2],
				h.empty() ? 0.0 : h[h.size() / 10], h.empty() ? 0.0 : h[h.size() * 9 / 10],
				c[c.size() / 2], c.back());
		}

		// The sun, where there is one.
		if (trg.sun_colour[0] > 0.0f || trg.sun_colour[1] > 0.0f || trg.sun_colour[2] > 0.0f)
		{
			static const double n[6][3] =
			{
				{ 1, 0, 0 }, { -1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
			};
			const double s[3] = { trg.sun_direction[0], trg.sun_direction[1], trg.sun_direction[2] };
			const double reading[4][3] =
			{
				{ s[0], s[1], s[2] }, { -s[0], s[1], -s[2] },
				{ s[0], -s[1], s[2] }, { -s[0], -s[1], -s[2] }
			};
			double score[4] = { 0.0, 0.0, 0.0, 0.0 };
			long used = 0;

			for (const std::array<uint8_t, 24>& c : cubes)
			{
				double lum[6], mean = 0.0;

				for (int f = 0; f < 6; f++)
				{
					lum[f] = (double)c[(size_t)f * 4] + c[(size_t)f * 4 + 1] + c[(size_t)f * 4 + 2];
					mean += lum[f] / 6.0;
				}

				if (mean <= 0.0 || !(lum[2] > 1.3 * lum[3]))
					continue;

				used++;

				for (int h = 0; h < 4; h++)
				{
					double a = 0.0;

					for (int f = 0; f < 6; f++)
						a += (lum[f] - mean) * (n[f][0] * reading[h][0] + n[f][1] * reading[h][1] +
							n[f][2] * reading[h][2]);

					score[h] += a / mean;
				}
			}

			std::printf("          sun (%.2f %.2f %.2f) colour (%.2f %.2f %.2f), %ld cubes lit from "
				"above: (x,y,z) %+.3f  (-x,y,-z) %+.3f  (x,-y,z) %+.3f  (-x,-y,-z) %+.3f\n",
				s[0], s[1], s[2], trg.sun_colour[0], trg.sun_colour[1], trg.sun_colour[2], used,
				used ? score[0] / used : 0.0, used ? score[1] / used : 0.0,
				used ? score[2] / used : 0.0, used ? score[3] / used : 0.0);
		}

		return gain;
	}

	/* -glossfield: the light field of a level (hd_gloss_field.h), built with
	 * the engine's own layout and build out of the .TRG's point lights and
	 * the rooms' boxes of the classic level - the boxes hd_level.cpp's
	 * BuildRoomBoxes hands the renderer, made world. Per level:
	 *
	 *   lamps  the point lights of the rooms with a colour, and the largest
	 *          channel of their colour: median, 90th percentile, maximum;
	 *   grid   the cells along x, y and z, the memory of the two textures
	 *          (and the same for a device that asks for powers of two), the
	 *          cells a lamp reaches, the weak ones (less than one-third
	 *          one-sided), the brightest cell's colour sum before the
	 *          exposure, and the build's time on this machine;
	 *   leak   the light the field carries through walls: over every cell
	 *          inside a room (over a floor square of it, between its ceiling
	 *          and its floor), the share of the light that comes from lamps
	 *          of rooms more than two portals away from that room (a
	 *          flipmap partner counts as the room itself), and the cells that
	 *          take more from those lamps than from the near ones;
	 *   weak   the room cell between lamps with the most light and the least
	 *          direction - a place to look at the cancelling case.
	 *
	 * The last lines give the lamps' largest channel over every level. */
	struct FieldTotals
	{
		std::vector<double> channel;
	};

	void glossField(const std::string& root, const std::string& stem,
		const std::string& classic, FieldTotals& totals)
	{
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		Tr5Level level;

		if (classic.empty() || !level.load(toW(classic + "\\" + stem + ".TRC"), error))
		{
			std::printf("%-9s needs -classic <folder holding the .TRC>\n", stem.c_str());
			return;
		}

		const int rooms = level.roomCount();
		std::vector<float> boxes((size_t)rooms * 6);

		for (int i = 0; i < rooms; i++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)i];
			unsigned yTop = 0, yOrigin = 0;
			float* b = &boxes[(size_t)i * 6];

			level.headerField(i, 24, yOrigin, error);
			level.headerField(i, 36, yTop, error);
			b[0] = (float)rm.originX + 1024.0f;
			b[1] = (float)((int)yTop - (int)yOrigin);
			b[2] = (float)rm.originZ + 1024.0f;
			b[3] = (float)rm.originX + (float)((rm.numXSectors - 1) * 1024);
			b[4] = (float)(rm.yBottom - (int)yOrigin);
			b[5] = (float)rm.originZ + (float)((rm.numZSectors - 1) * 1024);

			for (int axis = 0; axis < 3; axis++)
				if (b[3 + axis] < b[axis])
					b[3 + axis] = b[axis];
		}

		std::vector<HDGlossLamp> lamps;
		std::vector<int> lampRoom;
		std::vector<double> channel;

		for (size_t g = 0; g < trg.groups.size() && (int)g < rooms; g++)
			for (size_t li = 0; li < trg.groups[g].volumes.size(); li++)
			{
				const trf::remaster::TrgGroup::PointLight light = trg.groups[g].light(li);
				HDGlossLamp lamp = { { light.x, light.y, light.z }, { light.r, light.g, light.b } };

				if (!(light.r > 0.0f || light.g > 0.0f || light.b > 0.0f))
					continue;

				lamps.push_back(lamp);
				lampRoom.push_back((int)g);
				channel.push_back(std::max(light.r, std::max(light.g, light.b)));
			}

		totals.channel.insert(totals.channel.end(), channel.begin(), channel.end());
		std::sort(channel.begin(), channel.end());

		HDGlossFieldGrid grid, grid2;
		HDGlossFieldStats stats;
		std::vector<unsigned int> dir, rgb;
		const auto started = std::chrono::steady_clock::now();
		const bool laid = HDGlossFieldLayout(&boxes[0], rooms, lamps.empty() ? 0 : &lamps[0],
			(int)lamps.size(), false, 0, &grid);

		if (laid)
			HDGlossFieldBuild(grid, &lamps[0], (int)lamps.size(), dir, rgb, &stats);

		const double ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - started).count();
		const bool laid2 = HDGlossFieldLayout(&boxes[0], rooms, lamps.empty() ? 0 : &lamps[0],
			(int)lamps.size(), true, 0, &grid2);
		const double cells = laid ? (double)grid.dims[0] * grid.dims[1] * grid.dims[2] : 0.0;
		const double cells2 = laid2 ? (double)grid2.dims[0] * grid2.dims[1] * grid2.dims[2] : 0.0;

		std::printf("%-9s lamps %zu, largest channel median %.1f p90 %.1f max %.1f\n",
			stem.c_str(), lamps.size(), channel.empty() ? 0.0 : channel[channel.size() / 2],
			channel.empty() ? 0.0 : channel[channel.size() * 9 / 10],
			channel.empty() ? 0.0 : channel.back());

		if (!laid)
		{
			std::printf("          no field (no lamp reaches a room, or past the cell limit)\n");
			return;
		}

		std::printf("          grid %d x %d x %d = %.0f cells, %.2f MiB (powers of two %d x %d x "
			"%d, %.2f MiB), lit %d, weak %d, brightest %.2f at cell %d %d %d, build %.1f ms\n",
			grid.dims[0], grid.dims[1], grid.dims[2], cells, cells * 8.0 / 1048576.0,
			grid2.dims[0], grid2.dims[1], grid2.dims[2], cells2 * 8.0 / 1048576.0, stats.lit,
			stats.weak, stats.brightest, stats.brightestCell[0], stats.brightestCell[1],
			stats.brightestCell[2], ms);

		// The rooms within two portals of each room, a flipmap partner as the room.
		std::vector<std::vector<int> > partners((size_t)rooms);
		std::vector<std::set<int> > nearRooms((size_t)rooms);

		for (int r = 0; r < rooms; r++)
		{
			const int alt = level.rooms()[(size_t)r].altRoom;

			if (alt >= 0 && alt < rooms)
			{
				partners[(size_t)r].push_back(alt);
				partners[(size_t)alt].push_back(r);
			}
		}

		for (int r = 0; r < rooms; r++)
		{
			std::set<int>& near = nearRooms[(size_t)r];
			std::vector<int> front(1, r);

			near.insert(r);
			near.insert(partners[(size_t)r].begin(), partners[(size_t)r].end());
			front.insert(front.end(), partners[(size_t)r].begin(), partners[(size_t)r].end());

			for (int step = 0; step < 2; step++)
			{
				std::vector<int> next;

				for (const int at : front)
					for (const int to : level.rooms()[(size_t)at].portalRooms)
					{
						if (to < 0 || to >= rooms)
							continue;

						if (near.insert(to).second)
							next.push_back(to);

						for (const int alt : partners[(size_t)to])
							if (near.insert(alt).second)
								next.push_back(alt);
					}

				front.swap(next);
			}
		}

		double nearLight = 0.0, farLight = 0.0;
		long roomCells = 0, farCells = 0;
		double weakBest = 0.0;
		int weakRoom = -1;
		float weakAt[3] = { 0.0f, 0.0f, 0.0f };

		for (int r = 0; r < rooms; r++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)r];
			std::vector<Tr5Level::SectorInfo> squares;

			if (!level.sectorInfo(r, squares, error) ||
				squares.size() != (size_t)rm.numXSectors * rm.numZSectors)
				continue;

			for (int sx = 0; sx < rm.numXSectors; sx++)
				for (int sz = 0; sz < rm.numZSectors; sz++)
				{
					const Tr5Level::SectorInfo& q = squares[(size_t)sx * rm.numZSectors + sz];

					if (q.floor == -127)
						continue;

					const float floorY = (float)q.floor * 256.0f;
					const float ceilingY = (float)q.ceiling * 256.0f;
					const float p[3] = { (float)rm.originX + sx * 1024.0f + 512.0f, 0.0f,
						(float)rm.originZ + sz * 1024.0f + 512.0f };
					int cell[3];

					for (int y = 0; y < grid.dims[1]; y++)
					{
						const float cy = grid.origin[1] + ((float)y + 0.5f) * HD_GLOSS_FIELD_CELL;

						if (cy < ceilingY || cy > floorY)
							continue;

						const float c[3] = { p[0], cy, p[2] };

						if (!HDGlossFieldCellOf(grid, c, cell))
							continue;

						double nearHere = 0.0, farHere = 0.0;

						for (size_t li = 0; li < lamps.size(); li++)
						{
							const float d[3] = { lamps[li].pos[0] - c[0], lamps[li].pos[1] - c[1],
								lamps[li].pos[2] - c[2] };
							const float f = HDGlossFieldFalloff(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

							if (!(f > 0.0f))
								continue;

							const double e = f * (lamps[li].rgb[0] + lamps[li].rgb[1] +
								lamps[li].rgb[2]) / 3.0;

							if (nearRooms[(size_t)r].count(lampRoom[li]))
								nearHere += e;
							else
								farHere += e;
						}

						if (nearHere + farHere <= 0.0)
							continue;

						roomCells++;
						farCells += farHere > nearHere ? 1 : 0;
						nearLight += nearHere;
						farLight += farHere;

						float u[3], oneSided = 0.0f;
						const unsigned int texel =
							rgb[HDGlossFieldIndex(grid, cell[0], cell[1], cell[2])];

						HDGlossFieldUnpackDir(dir[HDGlossFieldIndex(grid, cell[0], cell[1],
							cell[2])], u, &oneSided);

						const double strength = (double)(texel >> 24) / 255.0;

						if (oneSided < 1.0f / 3.0f && strength * (1.0 - oneSided) > weakBest)
						{
							weakBest = strength * (1.0 - oneSided);
							weakRoom = r;
							weakAt[0] = c[0] - (float)rm.originX;
							weakAt[1] = c[1];
							weakAt[2] = c[2] - (float)rm.originZ;
						}
					}
				}
		}

		std::printf("          leak: %ld room cells lit, %.1f %% of their light from lamps more "
			"than two portals away, %ld cells take more from those than from the near ones\n",
			roomCells, nearLight + farLight > 0.0 ? 100.0 * farLight / (nearLight + farLight) : 0.0,
			farCells);

		/* And what reaches the floors, where the shader reads it: the field
		 * built again with the sight test by the rooms (HDGlossFieldSight,
		 * the lamps of rooms within two portals), both sampled as the pixel
		 * shader samples them, 64 units over the middle of every floor square.
		 * The highlight's strength there is the colour's largest channel
		 * times the one-sidedness times the filtered direction's length. */
		std::vector<unsigned char> nearTable((size_t)rooms * rooms, 0);

		for (int r = 0; r < rooms; r++)
			for (const int o : nearRooms[(size_t)r])
				nearTable[(size_t)r * rooms + o] = 1;

		const HDGlossFieldSight sight = { &boxes[0], rooms, &lampRoom[0], &nearTable[0] };
		std::vector<unsigned int> dirSight, rgbSight;
		HDGlossFieldStats statsSight;
		const auto sightStarted = std::chrono::steady_clock::now();

		HDGlossFieldBuild(grid, &lamps[0], (int)lamps.size(), dirSight, rgbSight, &statsSight,
			&sight);

		const double sightMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - sightStarted).count();
		double strengthAll = 0.0, strengthLost = 0.0, strengthWon = 0.0, turned = 0.0;
		long floors = 0, visible = 0, stronger = 0;
		double worst = 0.0, worstFrom = 0.0;
		int worstRoom = -1;
		float worstAt[3] = { 0.0f, 0.0f, 0.0f };

		for (int r = 0; r < rooms; r++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)r];
			std::vector<Tr5Level::SectorInfo> squares;

			if (!level.sectorInfo(r, squares, error) ||
				squares.size() != (size_t)rm.numXSectors * rm.numZSectors)
				continue;

			for (int sx = 0; sx < rm.numXSectors; sx++)
				for (int sz = 0; sz < rm.numZSectors; sz++)
				{
					const Tr5Level::SectorInfo& q = squares[(size_t)sx * rm.numZSectors + sz];

					if (q.floor == -127)
						continue;

					const float p[3] = { (float)rm.originX + sx * 1024.0f + 512.0f,
						(float)q.floor * 256.0f - 64.0f, (float)rm.originZ + sz * 1024.0f + 512.0f };
					float c0[3], u0[3], a0, l0, c1[3], u1[3], a1, l1;

					HDGlossFieldSample(grid, dir, rgb, p, c0, u0, &a0, &l0);
					HDGlossFieldSample(grid, dirSight, rgbSight, p, c1, u1, &a1, &l1);

					const double h0 = std::max(c0[0], std::max(c0[1], c0[2])) * a0 * std::min(l0, 1.0f);
					const double h1 = std::max(c1[0], std::max(c1[1], c1[2])) * a1 * std::min(l1, 1.0f);
					const double cosine = u0[0] * u1[0] + u0[1] * u1[1] + u0[2] * u1[2];

					floors++;
					strengthAll += h0;
					strengthLost += h0 > h1 ? h0 - h1 : 0.0;
					strengthWon += h1 > h0 ? h1 - h0 : 0.0;
					turned += h0 * std::acos(std::max(-1.0, std::min(1.0, cosine))) * 57.29578;
					visible += h0 - h1 > 0.1 ? 1 : 0;
					stronger += h1 - h0 > 0.1 ? 1 : 0;

					if (h0 - h1 > worst)
					{
						worst = h0 - h1;
						worstFrom = h0;
						worstRoom = r;
						worstAt[0] = p[0] - (float)rm.originX;
						worstAt[1] = p[1];
						worstAt[2] = p[2] - (float)rm.originZ;
					}
				}
		}

		std::printf("          floors: %ld squares; with the sight test (built in %.1f ms) the "
			"highlight's strength there falls by %.1f %% of its sum (by more than 0.1 on %ld) "
			"and rises by %.1f %% (by more than 0.1 on %ld) - the far lamps' share of the "
			"direction gone - and its direction turns by %.1f degrees on the strength-weighted "
			"mean\n", floors, sightMs,
			strengthAll > 0.0 ? 100.0 * strengthLost / strengthAll : 0.0, visible,
			strengthAll > 0.0 ? 100.0 * strengthWon / strengthAll : 0.0, stronger,
			strengthAll > 0.0 ? turned / strengthAll : 0.0);

		if (worstRoom >= 0)
			std::printf("          the most light through walls: room %d at room-local %.0f %.0f "
				"%.0f, strength %.2f without the test and %.2f with it\n", worstRoom, worstAt[0],
				worstAt[1], worstAt[2], worstFrom, worstFrom - worst);

		if (weakRoom >= 0)
			std::printf("          weak: room %d at room-local %.0f %.0f %.0f (strength x "
				"(1 - one-sidedness) %.2f)\n", weakRoom, weakAt[0], weakAt[1], weakAt[2], weakBest);
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

	/* -classicart: the rooms the engine draws classic, and the HD art of other
	 * rooms that stands where their faces are.
	 *
	 * A room is drawn classic when the .TRG has no group for it, an empty
	 * group, or a hollow one - every triangle's centroid outside the room's
	 * inside grown by the renderer's HD_OVERHANG_SLACK of 128 units
	 * (hd_level.cpp, the loop that fills g_group_hd; hd_render.cpp tags the
	 * overhang). Such a room's classic roomlets are drawn whenever it is on the
	 * frame's list, and the art of every HD group is drawn whenever that group
	 * is - so where another group's art stands in the classic room's place, a
	 * frame with both on the list draws the place twice.
	 *
	 * Per classic room this lists every HD group with a triangle centroid in
	 * the room's WHOLE box - its grid including the wall ring of border
	 * sectors, maxceiling to minfloor - split into the inside (one sector in
	 * from the grid's edge, the box BuildRoomIntrusions hands the renderer)
	 * and the ring around it, and whether the pair is on the engine's
	 * intrusion list (`rel=pair`: no portal or vertical link between the two
	 * and not a flipmap twin) or a neighbour (`rel=next`).
	 *
	 * The coverage test, per classic face of the room (Tr5Level::roomFaces,
	 * the faces InsertRoomlet draws): the face is sampled at the centroids of
	 * a regular subdivision of each of its triangles, no sample farther than
	 * `step` units from the next, none on an edge. A sample is covered when a
	 * segment along the face's normal, `tol` units to each side, meets an art
	 * triangle that faces the same way as the face (the side the classic
	 * face is drawn from; `-artfacing any` drops that condition). A face is
	 * covered when all of its samples are, and a room is `covered` by a set of
	 * groups when every one of its faces is; one uncovered sample anywhere is
	 * `not_covered`. `tol` defaults to 128, HD_PAIR_FACE_REACH: the art of a
	 * face stands within 86 units of it where that was measured.
	 *
	 * The control line holds the test against rooms whose answer is known:
	 * HD rooms of the same level against their own group, which draws them in
	 * the game. A test that covers those well is a test that can be believed
	 * where it says a classic room is not covered. */
	typedef HDCoverTri ArtTri;
	typedef HDCoverSet ArtGrid;
	typedef HDCoverCount ArtCover;

	/* The coverage of one room's classic faces by the art in `grid` -
	 * HDCoverFaces of specific/hd/hd_cover.h, the test hd_level.cpp asks of
	 * the stand art, on the faces of the .TRC. */
	HDCoverFace toCoverFace(const Tr5Level::FaceInfo& face)
	{
		HDCoverFace out;

		out.corners = face.quad ? 4 : 3;
		out.doubleSided = face.doubleSided ? 1 : 0;

		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 3; k++)
				out.v[c][k] = (double)face.v[c < out.corners ? c : 0][k];

		return out;
	}

	ArtCover coverFaces(const std::vector<Tr5Level::FaceInfo>& faces,
		const ArtGrid& grid, double tol, double step, int facing)
	{
		std::vector<HDCoverFace> list;

		for (const Tr5Level::FaceInfo& face : faces)
			list.push_back(toCoverFace(face));

		return HDCoverFaces(list.empty() ? 0 : &list[0], (int)list.size(), grid,
			tol, step, facing);
	}

	/* -classicart -perface: the rule per classic face, as hd_level.cpp's
	 * BuildStandRanges reaches it from the renderer's tag.
	 *
	 * The tag first, as hd_render.cpp makes it: every triangle of every HD
	 * group goes to the first holder in room order - a classic-drawn room
	 * with a classic face, not the group's own room or its flipmap twin -
	 * one of whose faces it stands on (HDCoverOverlaps). Per holder then the
	 * coverage of every face by each group's tagged triangles, the carrier
	 * (HDCoverCarrierBetter: most faces covered, then most triangles, then
	 * the lowest group), the room's state (HDCoverRoomState) and the two
	 * guards. One `perface room=` line per holder; one `perface face=` line
	 * per face: covered by the carrier or kept, the share of its samples the
	 * carrier, all the tagged art and all of it facing either way cover (a
	 * kept face with no share at all is bare, otherwise mixed), whether it
	 * is a face Lara can stand on (HDCoverFaceFloor) and its upward normal.
	 * One `perface tris=` line per holder with a carrier: the carrier's
	 * tagged triangles by kind (lying on the faces whole or reaching onto
	 * them) and by the faces they stand on - covered ones only (cov), kept
	 * ones only (kept) or both.
	 *
	 * And per classic-drawn room without a classic face, a `perface
	 * noface=` line: its sectors - walls, floors that open into the room
	 * below, solid floors - the rooms below and whether each is drawn HD, and
	 * the HD art whose centroid stands in its box: where Lara can be in such
	 * a room and what is under her. */
	void dumpPerFace(const Tr5Level& level, const trf::remaster::Trg& trg,
		const std::string& stem, const std::vector<std::vector<ArtTri> >& art,
		const std::vector<const char*>& reason, const std::vector<RoomSpace>& box,
		int rooms, int groups, double tol, double step, int facing)
	{
		std::string error;
		std::vector<std::vector<HDCoverFace> > faces((size_t)rooms);
		std::vector<std::vector<float> > faceBox((size_t)rooms);
		std::vector<int> holders;

		const auto drawsHD = [&](int g)
		{
			return g >= 0 && g < rooms && g < groups && !reason[(size_t)g];
		};

		for (int h = 0; h < rooms; h++)
		{
			if (!reason[(size_t)h])
				continue;

			std::vector<Tr5Level::FaceInfo> info;

			level.roomFaces(h, info, error);

			for (const Tr5Level::FaceInfo& face : info)
			{
				const HDCoverFace one = toCoverFace(face);

				faces[(size_t)h].push_back(one);

				for (int axis = 0; axis < 3; axis++)
				{
					double lo = one.v[0][axis], hi = one.v[0][axis];

					for (int c = 1; c < one.corners; c++)
					{
						if (one.v[c][axis] < lo) lo = one.v[c][axis];
						if (one.v[c][axis] > hi) hi = one.v[c][axis];
					}

					faceBox[(size_t)h].push_back((float)(lo - tol));
					faceBox[(size_t)h].push_back((float)(hi + tol));
				}
			}

			if (!faces[(size_t)h].empty())
				holders.push_back(h);
		}

		// The tag: the first holder in room order takes the triangle.
		std::vector<std::vector<ArtTri> > tagged((size_t)rooms);

		for (int g = 0; g < groups && g < rooms; g++)
		{
			if (!drawsHD(g))
				continue;

			const int twin = level.rooms()[(size_t)g].altRoom;

			for (const ArtTri& t : art[(size_t)g])
			{
				bool done = false;

				for (int h : holders)
				{
					if (done)
						break;

					if (h == g || h == twin || level.rooms()[(size_t)h].altRoom == g)
						continue;

					const std::vector<HDCoverFace>& list = faces[(size_t)h];
					const std::vector<float>& fb = faceBox[(size_t)h];

					for (size_t f = 0; f < list.size() && !done; f++)
					{
						const float* b = &fb[f * 6];

						if (t.hi[0] < b[0] || t.lo[0] > b[1] || t.hi[1] < b[2] ||
							t.lo[1] > b[3] || t.hi[2] < b[4] || t.lo[2] > b[5])
							continue;

						if (HDCoverOverlaps(t, list[f], tol))
						{
							tagged[(size_t)h].push_back(t);
							done = true;
						}
					}
				}
			}
		}

		int states[4] = { 0, 0, 0, 0 };

		for (int h : holders)
		{
			const std::vector<HDCoverFace>& list = faces[(size_t)h];
			const std::vector<ArtTri>& all = tagged[(size_t)h];
			const Tr5Room& hr = level.rooms()[(size_t)h];

			if (all.empty())
			{
				std::printf("classicart: %s perface room=%d faces=%d stand=0 state=classic\n",
					stem.c_str(), h, (int)list.size());
				states[0]++;
				continue;
			}

			std::set<int> groupSet;

			for (const ArtTri& t : all)
				groupSet.insert(t.group);

			int carrier = -1, carrierFaces = 0, carrierTris = 0;
			HDCoverCount carrierCover;
			std::string per;

			for (int g : groupSet)
			{
				ArtGrid one;

				for (const ArtTri& t : all)
					if (t.group == g)
						one.tris.push_back(t);

				one.build();

				const HDCoverCount cover = HDCoverFaces(&list[0], (int)list.size(), one,
					tol, step, facing);
				const int n = (int)one.tris.size();

				per += (per.empty() ? "" : ",") + std::to_string(g) + ":" +
					std::to_string(cover.facesCovered) + "f/" + std::to_string(n) + "t";

				if (HDCoverCarrierBetter(cover.facesCovered, n, g, carrierFaces,
					carrierTris, carrier))
				{
					carrier = g;
					carrierFaces = cover.facesCovered;
					carrierTris = n;
					carrierCover = cover;
				}
			}

			ArtGrid every;

			every.tris = all;
			every.build();

			const HDCoverCount same = HDCoverFaces(&list[0], (int)list.size(), every, tol,
				step, facing);
			const HDCoverCount any = HDCoverFaces(&list[0], (int)list.size(), every, tol,
				step, 0);
			int areaFaces = 0;

			for (size_t f = 0; f < list.size(); f++)
				if (any.faceShare.size() > f && HDCoverSamples(list[f], step,
					[](const double*, const double*) {}) > 0)
					areaFaces++;

			const int state = carrier < 0 ? HD_STAND_ROOM_CLASSIC :
				HDCoverRoomState(areaFaces, carrierFaces);
			bool flips = false;

			if (carrier >= 0)
			{
				flips = level.rooms()[(size_t)carrier].altRoom >= 0;

				for (int r = 0; r < rooms; r++)
					flips = flips || level.rooms()[(size_t)r].altRoom == carrier;
			}

			const bool water = (hr.flags & (0x100 | 0x200)) != 0;
			const bool guarded = carrier >= 0 && (flips || water);
			const int drawn = guarded ? HD_STAND_ROOM_CLASSIC : state;
			int kept = 0, keptFloors = 0, bare = 0;

			for (size_t f = 0; f < list.size(); f++)
			{
				const bool area = HDCoverSamples(list[f], step,
					[](const double*, const double*) {}) > 0;
				const bool covered = drawn != HD_STAND_ROOM_CLASSIC &&
					f < carrierCover.faceShare.size() &&
					HDCoverFaceCovered(carrierCover.faceShare[f]);

				if (!covered && area)
				{
					kept++;

					if (HDCoverFaceFloor(list[f]))
						keptFloors++;

					if (any.faceShare[f] <= 0.0)
						bare++;
				}
			}

			states[drawn + 1]++;
			std::printf("classicart: %s perface room=%d faces=%d area=%d stand=%d "
				"groups=%s carrier=%d covered=%d kept=%d kept_bare=%d kept_floors=%d "
				"state=%s guard=%s flags=0x%04x shadow=%d\n", stem.c_str(), h,
				(int)list.size(), areaFaces, (int)all.size(), per.c_str(), carrier,
				drawn == HD_STAND_ROOM_CLASSIC ? 0 : carrierFaces, kept, bare, keptFloors,
				drawn == HD_STAND_ROOM_COVERED ? "covered" :
				drawn == HD_STAND_ROOM_PART ? "part" : "classic",
				!guarded ? "-" : flips ? "flip" : "water", hr.flags,
				HDCoverRoomShadowHD(false, drawn, keptFloors, (int)list.size()) ? 1 : 0);

			for (size_t f = 0; f < list.size(); f++)
			{
				const HDCoverFace& face = list[f];
				double c[3] = { 0, 0, 0 };

				for (int k = 0; k < face.corners; k++)
					for (int axis = 0; axis < 3; axis++)
						c[axis] += face.v[k][axis] / face.corners;

				const bool area = HDCoverSamples(face, step,
					[](const double*, const double*) {}) > 0;
				const double mine = f < carrierCover.faceShare.size() ?
					carrierCover.faceShare[f] : 0.0;
				const bool covered = drawn != HD_STAND_ROOM_CLASSIC &&
					HDCoverFaceCovered(mine);

				std::printf("classicart: %s perface room=%d face=%d corners=%d ds=%d "
					"centre=%.0f,%.0f,%.0f up=%.2f floor=%d carrier=%.1f%% stand=%.1f%% "
					"any=%.1f%% %s\n", stem.c_str(), h, (int)f, face.corners,
					face.doubleSided, c[0], c[1], c[2], HDCoverFaceUp(face),
					HDCoverFaceFloor(face) ? 1 : 0, 100.0 * mine,
					100.0 * same.faceShare[f], 100.0 * any.faceShare[f],
					!area ? "noarea" : covered ? "covered" :
					any.faceShare[f] <= 0.0 ? "kept_bare" : "kept_mixed");
			}

			if (carrier < 0)
				continue;

			// The carrier's tagged triangles by kind and by the faces they stand on.
			int count[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };

			for (const ArtTri& t : all)
			{
				if (t.group != carrier)
					continue;

				bool onCovered = false, onKept = false;

				for (size_t f = 0; f < list.size(); f++)
				{
					if (!HDCoverOverlaps(t, list[f], tol))
						continue;

					if (drawn != HD_STAND_ROOM_CLASSIC &&
						HDCoverFaceCovered(carrierCover.faceShare[f]))
						onCovered = true;
					else
						onKept = true;
				}

				const int kind = HDCoverTriOnFaces(t, &list[0], (int)list.size(), tol) ? 1 : 0;

				count[kind][onCovered && onKept ? 2 : onCovered ? 0 : 1]++;
			}

			std::printf("classicart: %s perface room=%d tris carrier=%d "
				"whole: cov=%d kept=%d both=%d  reaching: cov=%d kept=%d both=%d\n",
				stem.c_str(), h, carrier, count[1][0], count[1][1], count[1][2],
				count[0][0], count[0][1], count[0][2]);
		}

		std::printf("classicart: %s perface holders=%d without stand art=%d classic=%d "
			"part=%d covered=%d\n", stem.c_str(), (int)holders.size(), states[0],
			states[1], states[2], states[3]);

		// The classic-drawn rooms without a classic face.
		for (int r = 0; r < rooms; r++)
		{
			if (!reason[(size_t)r] || !faces[(size_t)r].empty())
				continue;

			std::vector<Tr5Level::SectorInfo> sect;
			int walls = 0, open = 0, solid = 0;
			std::map<int, int> below;

			if (!level.sectorInfo(r, sect, error))
				continue;

			for (const Tr5Level::SectorInfo& s : sect)
			{
				if (s.floor == -127)
					walls++;
				else if (s.roomBelow != 255)
				{
					open++;
					below[s.roomBelow]++;
				}
				else
					solid++;
			}

			const RoomSpace& H = box[(size_t)r];
			int artTris = 0;
			std::set<int> artGroups;

			for (int g = 0; g < groups && g < rooms; g++)
			{
				if (g == r || !drawsHD(g))
					continue;

				for (const ArtTri& t : art[(size_t)g])
				{
					const float cx = t.a[0] + (t.e1[0] + t.e2[0]) / 3.0f;
					const float cy = t.a[1] + (t.e1[1] + t.e2[1]) / 3.0f;
					const float cz = t.a[2] + (t.e1[2] + t.e2[2]) / 3.0f;

					if (cx < H.x0 || cx > H.x1 || cz < H.z0 || cz > H.z1 ||
						cy < H.y0 || cy > H.y1)
						continue;

					artTris++;
					artGroups.insert(g);
				}
			}

			/* The engine's own question (hd_level.cpp, the faceless rooms of
			 * the shadow): a sector she can stand on, and no vertex of an HD
			 * group but the flipmap twin's in a 256-unit cell that meets the
			 * room's whole box - the grid with its wall ring, maxceiling to
			 * minfloor, the box art_tris counts centroids in. */
			bool bare = solid > 0;
			const int twin = level.rooms()[(size_t)r].altRoom;

			for (int g = 0; g < groups && g < rooms && bare; g++)
			{
				if (g == r || !drawsHD(g) || g == twin ||
					level.rooms()[(size_t)g].altRoom == r)
					continue;

				for (size_t t = 0; t < art[(size_t)g].size() && bare; t++)
				{
					const ArtTri& one = art[(size_t)g][t];

					for (int c = 0; c < 3 && bare; c++)
					{
						double p[3];
						bool in = true;

						for (int axis = 0; axis < 3; axis++)
							p[axis] = one.a[axis] + (c == 1 ? one.e1[axis] : 0.0) +
								(c == 2 ? one.e2[axis] : 0.0);

						const double lo[3] = { (double)H.x0, (double)H.y0, (double)H.z0 };
						const double hi[3] = { (double)H.x1, (double)H.y1, (double)H.z1 };

						for (int axis = 0; axis < 3; axis++)
						{
							const double cell = std::floor(p[axis] / 256.0) * 256.0;

							if (cell + 255.0 < lo[axis] || cell > hi[axis])
								in = false;
						}

						if (in)
							bare = false;
					}
				}
			}

			std::string under;

			for (const auto& one : below)
				under += (under.empty() ? "" : ",") + std::to_string(one.first) + "x" +
					std::to_string(one.second) + (drawsHD(one.first) ? "hd" : "classic");

			std::string names;

			for (int g : artGroups)
				names += (names.empty() ? "" : ",") + std::to_string(g);

			std::printf("classicart: %s perface noface=%d reason=%s sectors=%d walls=%d "
				"open_down=%d solid=%d below=%s art_tris=%d art_groups=%s flags=0x%04x "
				"water=%d bare=%d shadow=%d\n", stem.c_str(), r, reason[(size_t)r],
				(int)sect.size(), walls, open, solid, under.empty() ? "-" : under.c_str(),
				artTris, names.empty() ? "-" : names.c_str(),
				level.rooms()[(size_t)r].flags,
				(level.rooms()[(size_t)r].flags & 1) ? 1 : 0, bare ? 1 : 0,
				HDCoverRoomShadowHD(false, HD_STAND_ROOM_CLASSIC, 0, 0, bare) ? 1 : 0);
		}
	}

	/* -vistable: the visibility path's three tables (hd_vis.h) as the
	 * engine builds them, with the counts they rest on. Per level:
	 *
	 *   vistable: <stem> rooms= groups= art_rooms= walls= bare= void=
	 *             guests= guest_tris= portals= wide= ms=
	 *   vistable: <stem> reach=<r> covered=<share of the art rooms' wall
	 *             samples> faces=<walls> bare0= bare25= bare50= bare75=
	 *             bare100= all=   (faces by their bare share: 0, under a
	 *             quarter, under a half, under three quarters, under all,
	 *             all bare), once per -visreach value
	 *   vistable: <stem> guests by triangles in the inside: >=1 >=8 >=64
	 *             >=512, and the art box's reach into other rooms' whole
	 *             boxes (box_pairs=) and insides (inside_pairs=)
	 *
	 * and with -room <n> that room's guests, openings and wides. */
	void dumpVisTable(const std::string& root, const std::string& stem,
		const std::string& folder, int focus,
		const std::vector<double>& reaches)
	{
		Tr5Level level;
		std::string error;
		trf::remaster::Trg trg;

		if (!level.load(toW(folder + "\\" + stem + ".TRC"), error))
		{
			std::printf("vistable: %s classic level not loadable: %s\n",
				stem.c_str(), error.c_str());
			return;
		}

		if (!trf::remaster::read_trg(root + "\\DATA\\" + stem + ".TRG", trg,
			error))
		{
			std::printf("vistable: %s .TRG FAILED %s\n", stem.c_str(),
				error.c_str());
			return;
		}

		std::vector<HDVisRoom> rooms;
		std::vector<HDCoverFace> faces;
		std::vector<HDVisPortal> portals;
		std::vector<int> links;

		VisInputFromLevel(level, rooms, faces, portals, links);

		const HDVisParams base = HDVisDefaults();
		HDVisTables t;

		HDVisBuild(trg, rooms.empty() ? 0 : &rooms[0], (int)rooms.size(),
			faces.empty() ? 0 : &faces[0], (int)faces.size(),
			portals.empty() ? 0 : &portals[0], (int)portals.size(),
			links.empty() ? 0 : &links[0], (int)links.size(), base, &t);

		std::printf("vistable: %s rooms=%d groups=%d art_rooms=%d walls=%d "
			"bare=%d void=%d guests=%d guest_tris=%lld portals=%d wide=%d "
			"ms=%.0f guests_unlinked=%d openings=%d openings_unlinked=%d "
			"openings_twin=%d wide_mirror=%d\n",
			stem.c_str(), (int)rooms.size(), (int)trg.groups.size(), t.artRooms,
			t.wallFaces, t.bareFaces, t.voidFaces, t.guestPairs, t.guestTris,
			(int)portals.size(), t.widePortals, t.ms, t.guestsUnlinked,
			(int)t.openings.size(), t.openingsUnlinked, t.openingsTwin,
			t.wideMirrored);

		/* The faces the art plainly covers: wholly covered at the largest
		 * reach asked. `plain=` on each reach's line is the share of them
		 * that reach covers wholly too. */
		std::vector<std::vector<float> > bares;

		for (double reach : reaches)
		{
			HDVisParams p = base;
			HDVisTables r;

			p.reach = reach;
			HDVisBuild(trg, rooms.empty() ? 0 : &rooms[0], (int)rooms.size(),
				faces.empty() ? 0 : &faces[0], (int)faces.size(),
				portals.empty() ? 0 : &portals[0], (int)portals.size(),
			links.empty() ? 0 : &links[0], (int)links.size(), p, &r);
			bares.push_back(r.faceBare);
		}

		for (size_t at = 0; at < reaches.size(); at++)
		{
			HDVisParams p = base;
			HDVisTables r;
			int bucket[6] = { 0, 0, 0, 0, 0, 0 };
			int plain = 0, plainHere = 0;
			const double reach = reaches[at];

			for (size_t f = 0; f < bares.back().size(); f++)
				if (bares.back()[f] == 0.0f)
				{
					plain++;
					plainHere += bares[at][f] == 0.0f ? 1 : 0;
				}

			p.reach = reach;
			HDVisBuild(trg, rooms.empty() ? 0 : &rooms[0], (int)rooms.size(),
				faces.empty() ? 0 : &faces[0], (int)faces.size(),
				portals.empty() ? 0 : &portals[0], (int)portals.size(),
			links.empty() ? 0 : &links[0], (int)links.size(), p, &r);
			std::printf("vistable: %s reach=%.0f plain=%d/%d\n", stem.c_str(),
				reach, plainHere, plain);

			for (float b : r.faceBare)
			{
				if (b < 0.0f)
					continue;

				bucket[b <= 0.0f ? 0 : b < 0.25f ? 1 : b < 0.5f ? 2 :
					b < 0.75f ? 3 : b < 1.0f ? 4 : 5]++;
			}

			std::printf("vistable: %s reach=%.0f covered=%.4f art_rooms=%d "
				"faces=%d bare0=%d bare25=%d bare50=%d bare75=%d bare100=%d "
				"all=%d openings=%d void=%d wide=%d\n", stem.c_str(), reach,
				r.samples ? (double)r.samplesCovered / (double)r.samples : 0.0,
				r.artRooms, r.wallFaces, bucket[0], bucket[1], bucket[2],
				bucket[3], bucket[4], bucket[5], r.bareFaces, r.voidFaces,
				r.widePortals);
		}

		// The guests at every threshold, out of one build that keeps all.
		{
			HDVisParams p = base;
			HDVisTables g;
			int by[4] = { 0, 0, 0, 0 };
			const int at[4] = { 1, 8, 64, 512 };

			p.guestTris = 1;
			HDVisBuild(trg, rooms.empty() ? 0 : &rooms[0], (int)rooms.size(),
				faces.empty() ? 0 : &faces[0], (int)faces.size(),
				portals.empty() ? 0 : &portals[0], (int)portals.size(),
			links.empty() ? 0 : &links[0], (int)links.size(), p, &g);

			for (const HDVisGuest& one : g.guests)
				for (int k = 0; k < 4; k++)
					if (one.tris >= at[k])
						by[k]++;

			/* The art box of every group against the other rooms' whole
			 * boxes and insides, flipmap twins left out: how many rooms
			 * a box test alone would name. */
			int boxPairs = 0, insidePairs = 0;

			for (size_t gi = 0; gi < trg.groups.size() && gi < rooms.size();
				gi++)
			{
				float lo[3] = { 1e30f, 1e30f, 1e30f };
				float hi[3] = { -1e30f, -1e30f, -1e30f };
				const HDVisRoom& own = rooms[gi];

				for (const trf::remaster::TrgPart& part : trg.groups[gi].parts)
					for (int pass = 0; pass < 2; pass++)
						for (unsigned k = 0; k < part.pass[pass].count; k++)
						{
							const unsigned idx = part.pass[pass].start + k;

							if (idx >= trg.indices.size())
								break;

							const trf::remaster::TrgVertex& v =
								trg.vertices[trg.indices[idx]];
							const float w[3] = { (float)v.x + own.x,
								(float)v.y + own.y, (float)v.z + own.z };

							for (int a = 0; a < 3; a++)
							{
								lo[a] = std::min(lo[a], w[a]);
								hi[a] = std::max(hi[a], w[a]);
							}
						}

				if (lo[0] > hi[0])
					continue;

				for (size_t h = 0; h < rooms.size(); h++)
				{
					const HDVisRoom& r = rooms[h];

					if (h == gi || r.twin == (int)gi || own.twin == (int)h)
						continue;

					const float b0[3] = { (float)r.x, (float)r.top,
						(float)r.z };
					const float b1[3] = { (float)(r.x + ((long)r.xs << 10)),
						(float)r.bottom, (float)(r.z + ((long)r.zs << 10)) };
					const float i0[3] = { b0[0] + 1024.0f, b0[1],
						b0[2] + 1024.0f };
					const float i1[3] = { b1[0] - 1024.0f, b1[1],
						b1[2] - 1024.0f };
					bool box = true, inside = i0[0] < i1[0] && i0[2] < i1[2];

					for (int a = 0; a < 3; a++)
					{
						box = box && lo[a] < b1[a] && hi[a] > b0[a];
						inside = inside && lo[a] < i1[a] && hi[a] > i0[a];
					}

					boxPairs += box ? 1 : 0;
					insidePairs += inside ? 1 : 0;
				}
			}

			std::printf("vistable: %s guests tris>=1 %d >=8 %d >=64 %d "
				">=512 %d box_pairs=%d inside_pairs=%d\n", stem.c_str(), by[0],
				by[1], by[2], by[3], boxPairs, insidePairs);
		}

		if (focus < 0 || focus >= t.rooms)
			return;

		for (int k = 0; k < t.guestCount[(size_t)focus]; k++)
		{
			const HDVisGuest& g =
				t.guests[(size_t)(t.guestFirst[(size_t)focus] + k)];

			std::printf("vistable: %s room=%d guest=%d tris=%d box %.0f..%.0f "
				"%.0f..%.0f %.0f..%.0f\n", stem.c_str(), focus, g.group, g.tris,
				g.lo[0], g.hi[0], g.lo[1], g.hi[1], g.lo[2], g.hi[2]);
		}

		for (int k = 0; k < t.openCount[(size_t)focus]; k++)
		{
			const HDVisOpening& o =
				t.openings[(size_t)(t.openFirst[(size_t)focus] + k)];

			std::printf("vistable: %s room=%d opening face=%d bare=%.2f "
				"behind=%d n=%d,%d,%d v0=%d,%d,%d v2=%d,%d,%d\n", stem.c_str(),
				focus, o.face, o.bare, o.behind, o.n[0], o.n[1], o.n[2],
				o.v[0][0], o.v[0][1], o.v[0][2], o.v[2][0], o.v[2][1],
				o.v[2][2]);
		}

		for (int k = 0; k < t.wideCount[(size_t)focus]; k++)
		{
			const HDVisWide& w =
				t.wides[(size_t)(t.wideFirst[(size_t)focus] + k)];
			const HDVisPortal& p = portals[(size_t)(
				rooms[(size_t)focus].firstPortal + w.portal)];

			std::printf("vistable: %s room=%d wide portal=%d to=%d portal "
				"%d,%d,%d..%d,%d,%d opening %d,%d,%d..%d,%d,%d\n", stem.c_str(),
				focus, w.portal, p.to, p.v[0][0], p.v[0][1], p.v[0][2],
				p.v[2][0], p.v[2][1], p.v[2][2], w.v[0][0], w.v[0][1],
				w.v[0][2], w.v[2][0], w.v[2][1], w.v[2][2]);
		}
	}

	void dumpClassicArt(const std::string& root, const std::string& stem,
		const std::string& folder, int focus, double tol, double step,
		int facing, int controls, bool perFace)
	{
		Tr5Level level;
		std::string error;
		const std::string trc = folder + "\\" + stem + ".TRC";
		const std::string path = root + "\\DATA\\" + stem + ".TRG";
		trf::remaster::Trg trg;

		if (!level.load(toW(trc), error))
		{
			std::printf("classicart: %s classic level not loadable: %s\n",
				stem.c_str(), error.c_str());
			return;
		}

		if (!trf::remaster::read_trg(path, trg, error))
		{
			std::printf("classicart: %s .TRG FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		const int rooms = level.roomCount();
		const int groups = (int)trg.groups.size();
		std::vector<RoomSpace> box((size_t)rooms);

		for (int r = 0; r < rooms; r++)
		{
			const Tr5Room& rm = level.rooms()[(size_t)r];
			RoomSpace& b = box[(size_t)r];
			unsigned yTop = 0, yOrigin = 0;

			level.headerField(r, 24, yOrigin, error);
			level.headerField(r, 36, yTop, error);
			b.numX = rm.numXSectors;
			b.numZ = rm.numZSectors;
			b.ox = rm.originX;
			b.oy = (long)(int)yOrigin;
			b.oz = rm.originZ;
			b.meshes = 0;
			b.x0 = rm.originX;
			b.x1 = rm.originX + (long)rm.numXSectors * 1024;
			b.z0 = rm.originZ;
			b.z1 = rm.originZ + (long)rm.numZSectors * 1024;
			b.y0 = (long)(int)yTop;
			b.y1 = rm.yBottom;
			b.ix0 = b.x0 + 1024;
			b.ix1 = b.x0 + (long)(rm.numXSectors - 1) * 1024;
			b.iz0 = b.z0 + 1024;
			b.iz1 = b.z0 + (long)(rm.numZSectors - 1) * 1024;
		}

		// Next door: the portals and the vertical links, as -roomspace asks.
		std::vector<std::set<int> > nextDoor((size_t)rooms);

		for (int r = 0; r < rooms; r++)
		{
			for (int p : level.rooms()[(size_t)r].portalRooms)
				if (p >= 0 && p < rooms)
				{
					nextDoor[(size_t)r].insert(p);
					nextDoor[(size_t)p].insert(r);
				}

			std::vector<Tr5Level::SectorInfo> sect;

			if (!level.sectorInfo(r, sect, error))
				continue;

			for (const Tr5Level::SectorInfo& s : sect)
				for (int other : { s.roomAbove, s.roomBelow })
					if (other >= 0 && other < rooms)
					{
						nextDoor[(size_t)r].insert(other);
						nextDoor[(size_t)other].insert(r);
					}
		}

		/* Every group's triangles in world units, and the engine's two
		 * questions of them: how many there are, and how many stand outside
		 * the room's inside by more than the slack. */
		std::vector<std::vector<ArtTri> > art((size_t)groups);
		std::vector<int> tris((size_t)rooms, 0), overhang((size_t)rooms, 0);

		for (int g = 0; g < groups && g < rooms; g++)
		{
			const RoomSpace& b = box[(size_t)g];
			const float mins[3] = { 1024.0f, (float)(b.y0 - b.oy), 1024.0f };
			float maxs[3] = { (float)((b.numX - 1) * 1024), (float)(b.y1 - b.oy),
				(float)((b.numZ - 1) * 1024) };

			for (int k = 0; k < 3; k++)
				if (maxs[k] < mins[k])
					maxs[k] = mins[k];

			for (const trf::remaster::TrgPart& part : trg.groups[(size_t)g].parts)
				for (int pass = 0; pass < 3; pass++)
				{
					const trf::remaster::TrgPart::Pass& run = part.pass[pass];

					for (unsigned at = 0; at + 2 < run.count; at += 3)
					{
						const unsigned first = run.start + at;

						if (first + 2 >= trg.indices.size())
							break;

						float v[3][3];

						for (int c = 0; c < 3; c++)
						{
							const trf::remaster::TrgVertex& tv =
								trg.vertices[trg.indices[first + c]];

							v[c][0] = (float)tv.x;
							v[c][1] = (float)tv.y;
							v[c][2] = (float)tv.z;
						}

						const float centre[3] =
						{
							(v[0][0] + v[1][0] + v[2][0]) / 3.0f,
							(v[0][1] + v[1][1] + v[2][1]) / 3.0f,
							(v[0][2] + v[1][2] + v[2][2]) / 3.0f
						};

						tris[(size_t)g]++;

						for (int k = 0; k < 3; k++)
							if (centre[k] < mins[k] - 128.0f || centre[k] > maxs[k] + 128.0f)
							{
								overhang[(size_t)g]++;
								break;
							}

						ArtTri t;
						const float off[3] = { (float)b.ox, (float)b.oy, (float)b.oz };

						if (!HDCoverMakeTri(v, off, g, &t))
							continue;	// counted, but it covers nothing

						art[(size_t)g].push_back(t);
					}
				}
		}

		std::vector<const char*> reason((size_t)rooms, (const char*)0);
		int noGroup = 0, empty = 0, hollow = 0;

		for (int r = 0; r < rooms; r++)
		{
			if (r >= groups)
			{
				reason[(size_t)r] = "no_group";
				noGroup++;
			}
			else if (!tris[(size_t)r])
			{
				reason[(size_t)r] = "empty_group";
				empty++;
			}
			else if (overhang[(size_t)r] >= tris[(size_t)r])
			{
				reason[(size_t)r] = "hollow_group";
				hollow++;
			}
		}

		const auto drawsHD = [&](int g)
		{
			return g >= 0 && g < rooms && g < groups && !reason[(size_t)g];
		};

		/* The control: HD rooms against their own group, evenly spread over
		 * the level, under the three facing rules. */
		if (controls > 0)
		{
			int done = 0;
			long long s[3] = { 0, 0, 0 }, c[3] = { 0, 0, 0 };
			int f[3] = { 0, 0, 0 }, fc[3] = { 0, 0, 0 };

			for (int at = 0; at < controls; at++)
			{
				const int r = (int)((long long)at * rooms / controls);

				if (!drawsHD(r))
					continue;

				std::vector<Tr5Level::FaceInfo> faces;

				if (!level.roomFaces(r, faces, error) || faces.empty())
					continue;

				ArtGrid grid;

				grid.tris = art[(size_t)r];
				grid.build();
				done++;

				for (int k = 0; k < 3; k++)
				{
					const ArtCover cover = coverFaces(faces, grid, tol, step, k == 0 ? 1 : (k == 1 ? -1 : 0));

					s[k] += cover.samples;
					c[k] += cover.samplesCovered;
					f[k] += cover.faces;
					fc[k] += cover.facesCovered;
				}
			}

			std::printf("classicart: %s control rooms=%d own group covers samples "
				"same-facing %.1f%% (faces %d/%d) against %.1f%% (faces %d/%d) any "
				"%.1f%% (faces %d/%d)\n", stem.c_str(), done,
				s[0] ? 100.0 * c[0] / s[0] : 0.0, fc[0], f[0],
				s[1] ? 100.0 * c[1] / s[1] : 0.0, fc[1], f[1],
				s[2] ? 100.0 * c[2] / s[2] : 0.0, fc[2], f[2]);
		}

		int holders = 0;

		for (int h = 0; h < rooms; h++)
		{
			if (!reason[(size_t)h])
				continue;

			const RoomSpace& H = box[(size_t)h];
			const Tr5Room& hr = level.rooms()[(size_t)h];
			std::vector<int> inside((size_t)groups, 0), ring((size_t)groups, 0);
			bool any = false;

			for (int g = 0; g < groups && g < rooms; g++)
			{
				if (g == h || !drawsHD(g))
					continue;

				if (hr.altRoom == g || level.rooms()[(size_t)g].altRoom == h)
					continue;	// a flipmap twin is never drawn with it

				for (const ArtTri& t : art[(size_t)g])
				{
					const float cx = t.a[0] + (t.e1[0] + t.e2[0]) / 3.0f;
					const float cy = t.a[1] + (t.e1[1] + t.e2[1]) / 3.0f;
					const float cz = t.a[2] + (t.e1[2] + t.e2[2]) / 3.0f;

					if (cx < H.x0 || cx > H.x1 || cz < H.z0 || cz > H.z1 ||
						cy < H.y0 || cy > H.y1)
						continue;

					if (cx >= H.ix0 && cx <= H.ix1 && cz >= H.iz0 && cz <= H.iz1)
						inside[(size_t)g]++;
					else
						ring[(size_t)g]++;

					any = true;
				}
			}

			std::vector<Tr5Level::FaceInfo> faces;

			level.roomFaces(h, faces, error);

			if (!any)
			{
				if (focus == h)
					std::printf("classicart: %s room=%d reason=%s faces=%d no foreign "
						"art in its box\n", stem.c_str(), h, reason[(size_t)h],
						(int)faces.size());
				continue;
			}

			holders++;

			const auto artIn = [&](const std::vector<int>& which)
			{
				ArtGrid grid;

				for (int g : which)
					for (const ArtTri& t : art[(size_t)g])
						if (t.hi[0] >= H.x0 - tol && t.lo[0] <= H.x1 + tol &&
							t.hi[1] >= H.y0 - tol && t.lo[1] <= H.y1 + tol &&
							t.hi[2] >= H.z0 - tol && t.lo[2] <= H.z1 + tol)
							grid.tris.push_back(t);

				grid.build();
				return grid;
			};

			std::printf("classicart: %s room=%d reason=%s faces=%d box=%ld..%ld,"
				"%ld..%ld,%ld..%ld\n", stem.c_str(), h, reason[(size_t)h],
				(int)faces.size(), H.x0, H.x1, H.y0, H.y1, H.z0, H.z1);

			std::vector<int> pairs, all;

			for (int g = 0; g < groups && g < rooms; g++)
			{
				if (!inside[(size_t)g] && !ring[(size_t)g])
					continue;

				const bool next = nextDoor[(size_t)h].count(g) != 0;
				const ArtCover cover = coverFaces(faces, artIn(std::vector<int>(1, g)),
					tol, step, facing);

				all.push_back(g);

				if (!next)
					pairs.push_back(g);

				std::printf("classicart: %s room=%d art=%d rel=%s inside=%d ring=%d "
					"faces_covered=%d/%d samples=%lld/%lld (%.1f%%)%s\n", stem.c_str(),
					h, g, next ? "next" : "pair", inside[(size_t)g], ring[(size_t)g],
					cover.facesCovered, cover.faces, cover.samplesCovered,
					cover.samples, cover.samples ? 100.0 * cover.samplesCovered /
					cover.samples : 0.0, cover.faces &&
					cover.facesCovered == cover.faces ? " alone=covered" : "");
			}

			/* The art a build could tag by its centroid alone: the triangles
			 * of every group above whose centroid lies within a face's
			 * outline and `tol` of its plane (HDPairFaceDistance, the
			 * renderer's own face claim) and that face the way the face
			 * does. `union=claimed` is what those triangles cover; where it
			 * falls short of `union=all`, a triangle that covers the face
			 * has its centroid off it. */
			ArtGrid claimedGrid;

			{
				std::vector<HDPairFace> outline;
				std::vector<std::array<double, 3> > normal;

				for (const Tr5Level::FaceInfo& face : faces)
				{
					HDPairFace one;

					one.group = h;
					one.corners = face.quad ? 4 : 3;

					for (int c = 0; c < 4; c++)
						for (int axis = 0; axis < 3; axis++)
							one.v[c][axis] = (float)face.v[c < one.corners ? c : 0][axis];

					const double e1[3] = { (double)one.v[1][0] - one.v[0][0],
						(double)one.v[1][1] - one.v[0][1], (double)one.v[1][2] - one.v[0][2] };
					const double e2[3] = { (double)one.v[2][0] - one.v[0][0],
						(double)one.v[2][1] - one.v[0][1], (double)one.v[2][2] - one.v[0][2] };

					outline.push_back(one);
					normal.push_back({ e1[1] * e2[2] - e1[2] * e2[1],
						e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] });
				}

				const ArtGrid every = artIn(all);
				int claimedTris = 0;

				for (const ArtTri& t : every.tris)
				{
					const float p[3] =
					{
						t.a[0] + (t.e1[0] + t.e2[0]) / 3.0f,
						t.a[1] + (t.e1[1] + t.e2[1]) / 3.0f,
						t.a[2] + (t.e1[2] + t.e2[2]) / 3.0f
					};

					for (size_t f = 0; f < outline.size(); f++)
					{
						const double d = HDPairFaceDistance(outline[f], p);
						const double side = normal[f][0] * t.n[0] +
							normal[f][1] * t.n[1] + normal[f][2] * t.n[2];

						if (d >= 0.0 && d <= tol &&
							(faces[f].doubleSided || facing == 0 ||
							(facing > 0 ? side > 0.0 : side < 0.0)))
						{
							claimedGrid.tris.push_back(t);
							claimedTris++;
							break;
						}
					}
				}

				claimedGrid.build();

				const ArtCover cover = coverFaces(faces, claimedGrid, tol, step, facing);

				std::printf("classicart: %s room=%d union=claimed tris=%d "
					"faces_covered=%d/%d samples=%lld/%lld (%.1f%%)\n",
					stem.c_str(), h, claimedTris, cover.facesCovered, cover.faces,
					cover.samplesCovered, cover.samples, cover.samples ?
					100.0 * cover.samplesCovered / cover.samples : 0.0);
			}

			/* The stand art: the triangles of every HD group but this
			 * room and its flipmap twin that stand on one of its faces
			 * (HDCoverOverlaps, the renderer's own tag), and what they
			 * cover with the engine's threshold. `verdict` is the union's;
			 * `carrier` the group that covers the room alone (most
			 * triangles, then the lowest group) and `engine` what
			 * hd_level.cpp makes of it - the line the engine's
			 * `HDLOG room=... stand= covered= carrier=` answers, from the
			 * .TRC. */
			{
				std::vector<HDCoverFace> list;

				for (const Tr5Level::FaceInfo& face : faces)
					list.push_back(toCoverFace(face));

				ArtGrid standGrid;
				std::set<int> standGroups;

				for (int g = 0; g < groups && g < rooms; g++)
				{
					if (g == h || !drawsHD(g) || hr.altRoom == g ||
						level.rooms()[(size_t)g].altRoom == h)
						continue;

					for (const ArtTri& t : art[(size_t)g])
					{
						if (t.hi[0] < H.x0 - tol || t.lo[0] > H.x1 + tol ||
							t.hi[1] < H.y0 - tol || t.lo[1] > H.y1 + tol ||
							t.hi[2] < H.z0 - tol || t.lo[2] > H.z1 + tol)
							continue;

						for (const HDCoverFace& face : list)
							if (HDCoverOverlaps(t, face, tol))
							{
								standGrid.tris.push_back(t);
								standGroups.insert(g);
								break;
							}
					}
				}

				standGrid.build();

				const ArtCover cover = HDCoverFaces(list.empty() ? 0 : &list[0],
					(int)list.size(), standGrid, tol, step, facing);
				std::string names;

				int carrier = -1, carrierTris = 0;

				for (int g : standGroups)
				{
					ArtGrid alone;

					names += (names.empty() ? "" : ",") + std::to_string(g);

					for (const ArtTri& t : standGrid.tris)
						if (t.group == g)
							alone.tris.push_back(t);

					alone.build();

					const ArtCover one = HDCoverFaces(list.empty() ? 0 : &list[0],
						(int)list.size(), alone, tol, step, facing);
					const int n = (int)alone.tris.size();

					if (HDCoverRoomCovered(one.faces, one.facesCovered) &&
						(carrier < 0 || n > carrierTris || (n == carrierTris && g < carrier)))
					{
						carrier = g;
						carrierTris = n;
					}
				}

				std::printf("classicart: %s room=%d union=stand groups=%s tris=%d "
					"faces_covered=%d/%d samples=%lld/%lld (%.1f%%) verdict=%s "
					"carrier=%d engine=%s\n",
					stem.c_str(), h, names.empty() ? "-" : names.c_str(),
					(int)standGrid.tris.size(), cover.facesCovered, cover.faces,
					cover.samplesCovered, cover.samples, cover.samples ?
					100.0 * cover.samplesCovered / cover.samples : 0.0,
					!cover.faces ? "no_faces" : HDCoverRoomCovered(cover.faces,
					cover.facesCovered) ? "covered" : "not_covered", carrier,
					carrier >= 0 ? "as_art" : "classic");
			}

			for (int which = 0; which < 2; which++)
			{
				const std::vector<int>& set = which ? all : pairs;

				if (set.empty())
					continue;

				const ArtCover cover = coverFaces(faces, artIn(set), tol, step, facing);
				std::string names;

				for (int g : set)
					names += (names.empty() ? "" : ",") + std::to_string(g);

				std::printf("classicart: %s room=%d union=%s groups=%s "
					"faces_covered=%d/%d samples=%lld/%lld (%.1f%%) verdict=%s\n",
					stem.c_str(), h, which ? "all" : "pairs", names.c_str(),
					cover.facesCovered, cover.faces, cover.samplesCovered,
					cover.samples, cover.samples ? 100.0 * cover.samplesCovered /
					cover.samples : 0.0, !cover.faces ? "no_faces" :
					(cover.facesCovered == cover.faces ? "covered" : "not_covered"));

				/* With -room naming this room, the faces the set leaves
				 * uncovered: where they are and how much of each is bare. */
				if (focus == h)
				{
					size_t at = 0;

					for (size_t f = 0; f < faces.size(); f++)
					{
						const Tr5Level::FaceInfo& face = faces[f];
						const int corners = face.quad ? 4 : 3;
						double c[3] = { 0, 0, 0 };

						for (int k = 0; k < corners; k++)
							for (int axis = 0; axis < 3; axis++)
								c[axis] += (double)face.v[k][axis] / corners;

						if (at >= cover.faceShare.size())
							break;

						const double share = cover.faceShare[at++];

						if (share < 1.0)
							std::printf("classicart: %s room=%d union=%s face=%d "
								"corners=%d centre=%.0f,%.0f,%.0f covered=%.1f%%\n",
								stem.c_str(), h, which ? "all" : "pairs", (int)f,
								corners, c[0], c[1], c[2], 100.0 * share);
					}
				}
			}
		}

		if (perFace)
			dumpPerFace(level, trg, stem, art, reason, box, rooms, groups, tol, step,
				facing);

		std::printf("classicart: %s rooms=%d groups=%d classic=%d (no_group %d, "
			"empty_group %d, hollow_group %d) holding foreign art=%d tol=%.0f "
			"step=%.0f facing=%s\n", stem.c_str(), rooms, groups,
			noGroup + empty + hollow, noGroup, empty, hollow, holders, tol, step,
			facing > 0 ? "same" : (facing < 0 ? "against" : "any"));
	}

	/* -------------------------------------------- the HD rules on our own data
	 *
	 * Four counts that hold a rule of the HD path against the level files
	 * themselves: how the two colour triples of a .TRG vertex make the room
	 * light (-shadeorder), which two bytes of the vertex are the texture
	 * coordinates (-uvbytes), how the normal bytes of a .TRG and a .TRM read
	 * (-normals, -trmnormals), and what sizes the pages in TEX are
	 * (-ddssizes). Each prints a table per level and a total line. */

	/* One classic room vertex: the room-local position as the engine
	 * truncates it (hd_level.cpp, BuildWaterMarks: a short of the file's
	 * float) and the prelight drawroom.cpp lights it with
	 * (aRoomletTransformLight, ROOMLET_VERTEX::prelight, the dword after the
	 * normal - the colour the classic path draws a room vertex with when no
	 * dynamic light reaches it). */
	struct ShadeVertex
	{
		short p[3];
		int rgb[3];
	};

	/* One classic room face: its corners' room-local positions and the
	 * texinfo it wears. */
	struct ShadeFace
	{
		int corners;
		int texInfo;
		short p[4][3];
	};

	/* The vertices and faces of one classic room, read straight out of the
	 * .TRC with the walk Tr5Level::roomFaces makes: 28-byte vertices (three
	 * floats of position, three of normal, the prelight dword), faces of
	 * n corner indices, the texture word and the effects word. */
	bool classicRoomData(const Tr5Level& level, int room,
		std::vector<ShadeVertex>* verts, std::vector<ShadeFace>* faces)
	{
		const std::vector<uint8_t>& bytes = level.debugBytes();
		const Tr5Room& r = level.rooms()[(size_t)room];

		if (!r.headerOffset)
			return false;

		const size_t hdr = r.headerOffset, body = hdr + 208;
		int32_t nRoomlets = 0;
		uint32_t oRoomlets = 0, oVtx = 0, oFace = 0;

		std::memcpy(&nRoomlets, &bytes[hdr + 168], 4);
		std::memcpy(&oRoomlets, &bytes[hdr + 172], 4);
		std::memcpy(&oVtx, &bytes[hdr + 176], 4);
		std::memcpy(&oFace, &bytes[hdr + 180], 4);

		if (nRoomlets <= 0 || nRoomlets > 4096)
			return true;

		size_t facAt = body + oFace;
		size_t vtxAt = body + oVtx;

		for (int i = 0; i < nRoomlets; i++)
		{
			const size_t rl = body + oRoomlets + (size_t)i * 56;

			if (rl + 56 > bytes.size())
				return false;

			int16_t nVtx, nQuad, nTri;
			std::memcpy(&nVtx, &bytes[rl], 2);
			std::memcpy(&nQuad, &bytes[rl + 6], 2);
			std::memcpy(&nTri, &bytes[rl + 8], 2);

			if (vtxAt + (size_t)nVtx * 28 > bytes.size())
				return false;

			auto position = [&](int idx, short* out)
			{
				float f[3];
				std::memcpy(f, &bytes[vtxAt + (size_t)idx * 28], 12);
				out[0] = (short)f[0];
				out[1] = (short)f[1];
				out[2] = (short)f[2];
			};

			if (verts)
				for (int v = 0; v < nVtx; v++)
				{
					ShadeVertex sv;
					uint32_t col;

					position(v, sv.p);
					std::memcpy(&col, &bytes[vtxAt + (size_t)v * 28 + 24], 4);
					sv.rgb[0] = (int)((col >> 16) & 0xFF);
					sv.rgb[1] = (int)((col >> 8) & 0xFF);
					sv.rgb[2] = (int)(col & 0xFF);
					verts->push_back(sv);
				}

			for (int q = 0; q < nQuad + nTri; q++)
			{
				const int n = q < nQuad ? 4 : 3;

				if (facAt + (size_t)(n + 2) * 2 > bytes.size())
					return false;

				ShadeFace face;
				uint16_t tw;
				bool good = true;

				face.corners = n;
				std::memcpy(&tw, &bytes[facAt + (size_t)n * 2], 2);
				face.texInfo = tw & 0x3FFF;

				for (int k = 0; k < n; k++)
				{
					uint16_t w;
					std::memcpy(&w, &bytes[facAt + (size_t)k * 2], 2);

					if (w >= (uint16_t)nVtx)
						good = false;
					else
						position(w, face.p[k]);
				}

				if (good && faces)
					faces->push_back(face);

				facAt += (size_t)(n + 2) * 2;
			}

			vtxAt += (size_t)nVtx * 28;
		}

		return true;
	}

	uint64_t positionKey(const short* p)
	{
		return ((uint64_t)(uint16_t)p[0] << 32) | ((uint64_t)(uint16_t)p[1] << 16) |
			(uint64_t)(uint16_t)p[2];
	}

	uint64_t positionKey(const trf::remaster::TrgVertex& v)
	{
		const short p[3] = { v.x, v.y, v.z };
		return positionKey(p);
	}

	/* The triangles of one .TRG group, all three passes, as index triples. */
	void groupTriangles(const trf::remaster::Trg& trg, size_t g,
		std::vector<std::array<uint32_t, 3>>& out)
	{
		out.clear();

		for (const trf::remaster::TrgPart& part : trg.groups[g].parts)
			for (int pass = 0; pass < 3; pass++)
				for (uint32_t i = 0; i + 2 < part.pass[pass].count; i += 3)
				{
					const uint32_t at = part.pass[pass].start + i;

					if (at + 2 >= trg.indices.size())
						break;

					out.push_back({ trg.indices[at], trg.indices[at + 1], trg.indices[at + 2] });
				}
	}

	/* Average ranks, ties shared - the ranks a Spearman correlation takes. */
	std::vector<double> averageRanks(const std::vector<double>& v)
	{
		std::vector<size_t> order(v.size());

		for (size_t i = 0; i < order.size(); i++)
			order[i] = i;

		std::sort(order.begin(), order.end(),
			[&](size_t a, size_t b) { return v[a] < v[b]; });

		std::vector<double> rank(v.size());

		for (size_t i = 0; i < order.size();)
		{
			size_t j = i;

			while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]])
				j++;

			const double mean = 0.5 * (double)(i + j) + 1.0;

			for (size_t k = i; k <= j; k++)
				rank[order[k]] = mean;

			i = j + 1;
		}

		return rank;
	}

	double pearson(const std::vector<double>& a, const std::vector<double>& b)
	{
		const size_t n = a.size();

		if (n < 2)
			return 0.0;

		double ma = 0, mb = 0;

		for (size_t i = 0; i < n; i++)
		{
			ma += a[i];
			mb += b[i];
		}

		ma /= (double)n;
		mb /= (double)n;

		double sab = 0, saa = 0, sbb = 0;

		for (size_t i = 0; i < n; i++)
		{
			sab += (a[i] - ma) * (b[i] - mb);
			saa += (a[i] - ma) * (a[i] - ma);
			sbb += (b[i] - mb) * (b[i] - mb);
		}

		return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 0.0;
	}

	double spearman(const std::vector<double>& a, const std::vector<double>& b)
	{
		return pearson(averageRanks(a), averageRanks(b));
	}

	/* A fixed sequence, so a run is repeatable to the count. */
	struct Lcg
	{
		uint64_t s = 0x2545F4914F6CDD1DULL;

		size_t next(size_t n)
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			return (size_t)((s >> 33) % (uint64_t)n);
		}
	};

	/* The classic shade against the .TRG's two colour triples.
	 *
	 * The classic room vertices of room g are paired with the vertices of
	 * .TRG group g at the same room-local position - the pairing
	 * hd_level.cpp's BuildWaterMarks and hd_render.cpp make for the water
	 * marks. Where several .TRG vertices of the group stand at one position
	 * their triples are averaged. Per paired vertex five brightnesses, each
	 * the sum of three channels: the classic prelight, colour1, colour2,
	 * colour1 + colour2, and colour1 * colour2 / 255 per channel.
	 *
	 * Two measures of whether a combination orders the vertices the way the
	 * classic shade does: the Spearman rank correlation with the classic
	 * shade, and a sample of 200,000 vertex pairs with different classic
	 * shades, counted as agreeing (the combination puts the two the same way
	 * round), disagreeing, or tied. The decisive count is the last one: of
	 * the sampled pairs that the sum and the product put in OPPOSITE order,
	 * how many the classic shade puts the sum's way and how many the
	 * product's. */
	struct ShadeTotals
	{
		long long classic = 0, paired = 0;
		long long agree[4] = {}, disagree[4] = {}, tie[4] = {};
		long long split = 0, splitSum = 0, splitProduct = 0;
		long long channels = 0, aboveOne = 0;
		std::vector<double> all[5];
	};

	void dumpShadeOrder(const std::string& root, const std::string& stem,
		const std::string& folder, ShadeTotals& totals, bool header)
	{
		static const char* const kName[4] = { "c1+c2", "c1*c2", "c1", "c2" };

		Tr5Level level;
		std::string error;

		if (!level.load(toW(folder + "\\" + stem + ".TRC"), error))
		{
			std::printf("%-9s classic level not loadable: %s\n", stem.c_str(), error.c_str());
			return;
		}

		trf::remaster::Trg trg;

		if (!trf::remaster::read_trg(root + "\\DATA\\" + stem + ".TRG", trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		if (header)
		{
			std::printf("%-9s %8s %8s  %-6s %7s %8s %8s %8s\n", "level", "classic",
				"paired", "rule", "rho", "agree", "disagree", "tie");
			std::printf("%-9s %8s %8s  %-6s %7s %8s %8s %8s\n", "---------", "--------",
				"--------", "------", "-------", "--------", "--------", "--------");
		}

		/* shade, sum, product, c1, c2 */
		std::vector<double> col[5];
		long long classicCount = 0;
		const size_t groups = std::min((size_t)level.roomCount(), trg.groups.size());
		std::vector<std::array<uint32_t, 3>> tris;

		for (size_t g = 0; g < groups; g++)
		{
			std::vector<ShadeVertex> verts;

			if (!classicRoomData(level, (int)g, &verts, 0))
				continue;

			classicCount += (long long)verts.size();
			groupTriangles(trg, g, tris);

			struct Acc { double c1[3] = {}, c2[3] = {}; int n = 0; };
			std::unordered_map<uint64_t, Acc> at;
			std::set<uint32_t> seen;

			for (const auto& t : tris)
				for (int k = 0; k < 3; k++)
				{
					if (!seen.insert(t[k]).second || t[k] >= trg.vertices.size())
						continue;

					const trf::remaster::TrgVertex& v = trg.vertices[t[k]];
					Acc& a = at[positionKey(v)];

					for (int c = 0; c < 3; c++)
					{
						a.c1[c] += v.colour1[c];
						a.c2[c] += v.colour2[c];
					}

					a.n++;
				}

			for (const ShadeVertex& sv : verts)
			{
				auto it = at.find(positionKey(sv.p));

				if (it == at.end())
					continue;

				const Acc& a = it->second;
				double c1 = 0, c2 = 0, product = 0;

				for (int c = 0; c < 3; c++)
				{
					const double x = a.c1[c] / a.n, y = a.c2[c] / a.n;
					c1 += x;
					c2 += y;
					product += x * y / 255.0;
				}

				col[0].push_back((double)(sv.rgb[0] + sv.rgb[1] + sv.rgb[2]));
				col[1].push_back(c1 + c2);
				col[2].push_back(product);
				col[3].push_back(c1);
				col[4].push_back(c2);
			}
		}

		const size_t n = col[0].size();
		long long agree[4] = {}, disagree[4] = {}, tie[4] = {};
		long long split = 0, splitSum = 0, splitProduct = 0;
		Lcg lcg;

		if (n >= 2)
			for (int s = 0; s < 200000; s++)
			{
				const size_t a = lcg.next(n), b = lcg.next(n);
				const double d = col[0][a] - col[0][b];

				if (d == 0.0)
				{
					s--;
					continue;
				}

				for (int r = 0; r < 4; r++)
				{
					const double e = col[r + 1][a] - col[r + 1][b];

					if (e == 0.0)
						tie[r]++;
					else if ((e > 0) == (d > 0))
						agree[r]++;
					else
						disagree[r]++;
				}

				const double es = col[1][a] - col[1][b];
				const double ep = col[2][a] - col[2][b];

				if (es != 0.0 && ep != 0.0 && (es > 0) != (ep > 0))
				{
					split++;

					if ((es > 0) == (d > 0))
						splitSum++;
					else
						splitProduct++;
				}
			}

		for (int r = 0; r < 4; r++)
		{
			const double rho = n >= 2 ? spearman(col[0], col[r + 1]) : 0.0;

			std::printf("%-9s %8lld %8zu  %-6s %7.4f %8lld %8lld %8lld\n", stem.c_str(),
				classicCount, n, kName[r], rho, agree[r], disagree[r], tie[r]);

			totals.agree[r] += agree[r];
			totals.disagree[r] += disagree[r];
			totals.tie[r] += tie[r];
		}

		std::printf("%-9s sum and product in opposite order: %lld pair(s); the classic "
			"shade sides with the sum in %lld, with the product in %lld\n", stem.c_str(),
			split, splitSum, splitProduct);

		/* The range of the sum the room shader multiplies with, over every
		 * vertex of the file: each channel of colour1 + colour2 over 255. */
		long long channels = 0, aboveOne = 0;
		int widest = 0;

		for (const trf::remaster::TrgVertex& v : trg.vertices)
			for (int c = 0; c < 3; c++)
			{
				const int sum = v.colour1[c] + v.colour2[c];

				channels++;
				aboveOne += sum > 255 ? 1 : 0;
				widest = std::max(widest, sum);
			}

		std::printf("%-9s c1+c2 over 255: largest %.3f, above 1 in %lld of %lld channels "
			"(%.1f %%)\n", stem.c_str(), widest / 255.0, aboveOne, channels,
			channels ? 100.0 * aboveOne / channels : 0.0);

		totals.channels += channels;
		totals.aboveOne += aboveOne;

		totals.classic += classicCount;
		totals.paired += (long long)n;
		totals.split += split;
		totals.splitSum += splitSum;
		totals.splitProduct += splitProduct;

		for (int c = 0; c < 5; c++)
			totals.all[c].insert(totals.all[c].end(), col[c].begin(), col[c].end());
	}

	void printShadeTotals(const ShadeTotals& totals)
	{
		static const char* const kName[4] = { "c1+c2", "c1*c2", "c1", "c2" };

		std::printf("\nall levels: %lld classic room vertices, %lld paired with a .TRG "
			"vertex at the same room-local position\n", totals.classic, totals.paired);

		for (int r = 0; r < 4; r++)
			std::printf("all       %-6s rho %7.4f   agree %8lld  disagree %8lld  tie %8lld\n",
				kName[r], spearman(totals.all[0], totals.all[r + 1]), totals.agree[r],
				totals.disagree[r], totals.tie[r]);

		std::printf("all       sum and product in opposite order: %lld pair(s); classic "
			"with the sum %lld, with the product %lld\n", totals.split, totals.splitSum,
			totals.splitProduct);
		std::printf("all       c1+c2 above 1 in %lld of %lld channels (%.1f %%)\n",
			totals.aboveOne, totals.channels,
			totals.channels ? 100.0 * totals.aboveOne / totals.channels : 0.0);
	}

	/* Which two bytes of a .TRG vertex are the texture coordinates.
	 *
	 * The layout read here puts u in the fourth byte of colour1 (offset 15)
	 * and v in the fourth byte of colour2 (offset 19). Three counts hold it
	 * against the files:
	 *
	 *   the value ranges of the two bytes and of the layer byte, which has to
	 *   lie in 1 .. the file's texture count;
	 *   the .TRG vertices that lie inside one classic face of their room (in
	 *   its plane, inside one of its two triangles with a margin): the
	 *   classic u and v there, interpolated from the face's texinfo corners,
	 *   held against the bytes. Per face, on the vertices of its commonest
	 *   layer: every two vertices counted as agreeing when a byte puts them
	 *   the same way round as the classic u (or v) does, for the reading
	 *   (u = +15, v = +19) and for the swapped one; and a straight-line fit
	 *   of each byte on the classic (u, v), whose R squared says whether the
	 *   byte is a texture coordinate of that tile at all;
	 *   and the consistency test of a texture coordinate: every classic face
	 *   that wears one texinfo must put that tile's corners at the same place
	 *   on the same page - the fit, evaluated at the texinfo's four corners,
	 *   must agree between two faces within three steps. A light byte - the
	 *   red of colour1 - goes through the same fit and the same test as the
	 *   control. */
	struct UvTotals
	{
		long long faces = 0, vertices = 0;
		long long agree[4] = {}, disagree[4] = {}, tie[4] = {};
		long long fitted = 0, goodFit[3] = {};
		long long keys = 0, keysSame[2] = {};
		std::vector<double> cu, cv, b15, b19;
	};

	/* Least squares of y on (1, u, v); false when the three are degenerate.
	 * r2 is the share of y's variance the line explains. */
	bool fitPlane(const std::vector<double>& u, const std::vector<double>& v,
		const std::vector<double>& y, double c[3], double* r2)
	{
		const size_t n = y.size();
		double m[3][3] = {}, r[3] = {};

		for (size_t i = 0; i < n; i++)
		{
			const double x[3] = { 1.0, u[i], v[i] };

			for (int a = 0; a < 3; a++)
			{
				r[a] += x[a] * y[i];

				for (int b = 0; b < 3; b++)
					m[a][b] += x[a] * x[b];
			}
		}

		const double det =
			m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
			m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
			m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

		if (std::fabs(det) < 1e-6)
			return false;

		for (int k = 0; k < 3; k++)
		{
			double t[3][3];

			for (int a = 0; a < 3; a++)
				for (int b = 0; b < 3; b++)
					t[a][b] = b == k ? r[a] : m[a][b];

			c[k] = (t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) -
				t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) +
				t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0])) / det;
		}

		double mean = 0, all = 0, rest = 0;

		for (size_t i = 0; i < n; i++)
			mean += y[i];

		mean /= (double)n;

		for (size_t i = 0; i < n; i++)
		{
			const double f = c[0] + c[1] * u[i] + c[2] * v[i];
			all += (y[i] - mean) * (y[i] - mean);
			rest += (y[i] - f) * (y[i] - f);
		}

		*r2 = all > 0 ? 1.0 - rest / all : (rest < 1e-9 ? 1.0 : 0.0);
		return true;
	}

	void dumpUvBytes(const std::string& root, const std::string& stem,
		const std::string& folder, UvTotals& totals)
	{
		Tr5Level level;
		std::string error;

		if (!level.load(toW(folder + "\\" + stem + ".TRC"), error))
		{
			std::printf("%-9s classic level not loadable: %s\n", stem.c_str(), error.c_str());
			return;
		}

		trf::remaster::Trg trg;

		if (!trf::remaster::read_trg(root + "\\DATA\\" + stem + ".TRG", trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		size_t texAt = 0;
		int32_t texCount = 0;
		level.debugTexinfoBlock(texAt, texCount);
		const std::vector<uint8_t>& bytes = level.debugBytes();

		/* The ranges, over every vertex of the file. */
		int lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
		std::set<int> distinct[3];
		long long layerBad = 0;

		for (const trf::remaster::TrgVertex& v : trg.vertices)
		{
			const int b[3] = { v.u, v.v, v.layer };

			for (int i = 0; i < 3; i++)
			{
				lo[i] = std::min(lo[i], b[i]);
				hi[i] = std::max(hi[i], b[i]);
				distinct[i].insert(b[i]);
			}

			if (!v.layer || v.layer > trg.textures.size())
				layerBad++;
		}

		std::printf("%-9s vertices %zu  u(+15) %d..%d %zu distinct  v(+19) %d..%d %zu "
			"distinct  layer(+11) %d..%d of %zu textures, %lld outside 1..%zu\n",
			stem.c_str(), trg.vertices.size(), lo[0], hi[0], distinct[0].size(), lo[1],
			hi[1], distinct[1].size(), lo[2], hi[2], trg.textures.size(), layerBad,
			trg.textures.size());

		/* The classic texinfo's corner coordinates, in pixels of its 256 page. */
		auto classicUv = [&](int ti, int k, double* u, double* v) -> bool
		{
			if (ti < 0 || ti >= texCount || !texAt)
				return false;

			const size_t at = texAt + (size_t)ti * 40 + 6 + (size_t)k * 4;
			uint16_t wu, wv;

			if (at + 4 > bytes.size())
				return false;

			std::memcpy(&wu, &bytes[at], 2);
			std::memcpy(&wv, &bytes[at + 2], 2);
			*u = (double)(wu >> 8);
			*v = (double)(wv >> 8);
			return true;
		};

		long long faceCount = 0, vertexCount = 0, fitted = 0, goodFit[3] = {};
		long long agree[4] = {}, disagree[4] = {}, tie[4] = {};
		/* texinfo -> the first face's page and its fit of u, v and the control
		 * at the texinfo's corners; whether a later face agreed */
		struct Seen { int page; int corners; double at[4][3]; int face; bool other; bool same[2]; };
		std::map<int, Seen> seen;
		const size_t groups = std::min((size_t)level.roomCount(), trg.groups.size());
		std::vector<std::array<uint32_t, 3>> list;

		for (size_t g = 0; g < groups; g++)
		{
			std::vector<ShadeFace> faces;

			if (!classicRoomData(level, (int)g, 0, &faces) || faces.empty())
				continue;

			/* Each face's plane, its classic corner (u, v) and its box, and a
			 * grid of 512 units over x and z that names the faces whose box
			 * reaches into a cell. */
			struct FacePlane
			{
				double n[3], d;
				double cu[4], cv[4];
				double lo[3], hi[3];
				bool good;
			};

			std::vector<FacePlane> plane(faces.size());
			std::unordered_map<uint64_t, std::vector<int>> grid;

			for (size_t f = 0; f < faces.size(); f++)
			{
				const ShadeFace& face = faces[f];
				FacePlane& fp = plane[f];
				double e1[3], e2[3];

				for (int x = 0; x < 3; x++)
				{
					e1[x] = (double)face.p[1][x] - face.p[0][x];
					e2[x] = (double)face.p[2][x] - face.p[0][x];
				}

				fp.n[0] = e1[1] * e2[2] - e1[2] * e2[1];
				fp.n[1] = e1[2] * e2[0] - e1[0] * e2[2];
				fp.n[2] = e1[0] * e2[1] - e1[1] * e2[0];
				const double nl = std::sqrt(fp.n[0] * fp.n[0] + fp.n[1] * fp.n[1] +
					fp.n[2] * fp.n[2]);
				fp.good = nl > 1e-6;

				for (int k = 0; k < face.corners && fp.good; k++)
					fp.good = classicUv(face.texInfo, k, &fp.cu[k], &fp.cv[k]);

				if (!fp.good)
					continue;

				for (int x = 0; x < 3; x++)
					fp.n[x] /= nl;

				fp.d = fp.n[0] * face.p[0][0] + fp.n[1] * face.p[0][1] + fp.n[2] * face.p[0][2];

				for (int x = 0; x < 3; x++)
				{
					fp.lo[x] = fp.hi[x] = face.p[0][x];

					for (int k = 1; k < face.corners; k++)
					{
						fp.lo[x] = std::min(fp.lo[x], (double)face.p[k][x]);
						fp.hi[x] = std::max(fp.hi[x], (double)face.p[k][x]);
					}
				}

				for (int cx = (int)std::floor((fp.lo[0] - 4) / 512); cx <= (int)std::floor((fp.hi[0] + 4) / 512); cx++)
					for (int cz = (int)std::floor((fp.lo[2] - 4) / 512); cz <= (int)std::floor((fp.hi[2] + 4) / 512); cz++)
						grid[((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz].push_back((int)f);
			}

			/* The group's own vertices, each once. */
			groupTriangles(trg, g, list);
			std::set<uint32_t> own;

			for (const auto& t : list)
				for (int k = 0; k < 3; k++)
					if (t[k] < trg.vertices.size())
						own.insert(t[k]);

			/* face -> its vertices: classic u, v; bytes +15, +19, colour1 red;
			 * layer */
			struct Sample { double cu, cv, b15, b19, red; int layer; };
			std::vector<std::vector<Sample>> inside(faces.size());

			for (const uint32_t index : own)
			{
				const trf::remaster::TrgVertex& v = trg.vertices[index];
				const double p[3] = { (double)v.x, (double)v.y, (double)v.z };
				const int cx = (int)std::floor(p[0] / 512), cz = (int)std::floor(p[2] / 512);
				auto cell = grid.find(((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz);

				if (cell == grid.end())
					continue;

				int hit = -1, hits = 0;
				double hu = 0, hv = 0;

				for (const int f : cell->second)
				{
					const FacePlane& fp = plane[(size_t)f];
					const ShadeFace& face = faces[(size_t)f];

					if (!fp.good ||
						std::fabs(fp.n[0] * p[0] + fp.n[1] * p[1] + fp.n[2] * p[2] - fp.d) > 2.0)
						continue;

					/* The face's two triangles, (0, 1, 2) and (0, 2, 3). */
					for (int tri = 0; tri < (face.corners == 4 ? 2 : 1); tri++)
					{
						const int k[3] = { 0, tri ? 2 : 1, tri ? 3 : 2 };
						double w[3];
						double area = 0;

						for (int j = 0; j < 3; j++)
						{
							const short* a = face.p[k[(j + 1) % 3]];
							const short* b = face.p[k[(j + 2) % 3]];
							const double ea[3] = { a[0] - p[0], a[1] - p[1], a[2] - p[2] };
							const double eb[3] = { b[0] - p[0], b[1] - p[1], b[2] - p[2] };
							const double c[3] =
							{
								ea[1] * eb[2] - ea[2] * eb[1],
								ea[2] * eb[0] - ea[0] * eb[2],
								ea[0] * eb[1] - ea[1] * eb[0]
							};
							w[j] = c[0] * fp.n[0] + c[1] * fp.n[1] + c[2] * fp.n[2];
							area += w[j];
						}

						if (std::fabs(area) < 1e-6)
							continue;

						bool in = true;

						for (int j = 0; j < 3; j++)
						{
							w[j] /= area;
							in = in && w[j] >= 0.02;
						}

						if (!in)
							continue;

						hits++;
						hit = f;
						hu = w[0] * fp.cu[k[0]] + w[1] * fp.cu[k[1]] + w[2] * fp.cu[k[2]];
						hv = w[0] * fp.cv[k[0]] + w[1] * fp.cv[k[1]] + w[2] * fp.cv[k[2]];
					}
				}

				if (hits != 1)
					continue;

				inside[(size_t)hit].push_back({ hu, hv, (double)v.u, (double)v.v,
					(double)v.colour1[0], v.layer });
			}

			for (size_t f = 0; f < faces.size(); f++)
			{
				std::vector<Sample>& s = inside[f];

				if (s.size() < 4)
					continue;

				/* The commonest layer of the face, and only its vertices. */
				std::map<int, int> layers;

				for (const Sample& x : s)
					layers[x.layer]++;

				int layer = 0, most = 0;

				for (const auto& kv : layers)
					if (kv.second > most)
					{
						most = kv.second;
						layer = kv.first;
					}

				std::vector<Sample> keep;

				for (const Sample& x : s)
					if (x.layer == layer)
						keep.push_back(x);

				if (keep.size() < 4)
					continue;

				faceCount++;
				vertexCount += (long long)keep.size();

				const size_t cap = std::min(keep.size(), (size_t)64);

				for (size_t a = 0; a < cap; a++)
				{
					totals.cu.push_back(keep[a].cu);
					totals.cv.push_back(keep[a].cv);
					totals.b15.push_back(keep[a].b15);
					totals.b19.push_back(keep[a].b19);

					for (size_t b = a + 1; b < cap; b++)
					{
						const double du = keep[a].cu - keep[b].cu, dv = keep[a].cv - keep[b].cv;
						const double d15 = keep[a].b15 - keep[b].b15;
						const double d19 = keep[a].b19 - keep[b].b19;
						const double cl[4] = { du, du, dv, dv };
						const double hd[4] = { d15, d19, d19, d15 };

						for (int r = 0; r < 4; r++)
						{
							if (std::fabs(cl[r]) < 1.0)
								continue;

							if (hd[r] == 0.0)
								tie[r]++;
							else if ((hd[r] > 0) == (cl[r] > 0))
								agree[r]++;
							else
								disagree[r]++;
						}
					}
				}

				std::vector<double> u, v, y[3];

				for (const Sample& x : keep)
				{
					u.push_back(x.cu);
					v.push_back(x.cv);
					y[0].push_back(x.b15);
					y[1].push_back(x.b19);
					y[2].push_back(x.red);
				}

				double c[3][3], r2[3];
				bool ok = true;

				for (int i = 0; i < 3 && ok; i++)
					ok = fitPlane(u, v, y[i], c[i], &r2[i]);

				if (!ok)
					continue;

				fitted++;

				for (int i = 0; i < 3; i++)
					goodFit[i] += r2[i] >= 0.95 ? 1 : 0;

				/* Where the fit puts the texinfo's corners: u, v and the
				 * control. A tile mapped once on the page lands every face
				 * that wears it on the same place. */
				Seen now;
				now.page = layer >= 1 && (size_t)layer <= trg.textures.size()
					? (int)trg.textures[(size_t)layer - 1] : -1;
				now.corners = faces[f].corners;
				now.face = (int)f;
				now.other = false;
				now.same[0] = now.same[1] = true;

				for (int k = 0; k < faces[f].corners; k++)
					for (int i = 0; i < 3; i++)
						now.at[k][i] = c[i][0] + c[i][1] * plane[f].cu[k] +
							c[i][2] * plane[f].cv[k];

				auto put = seen.insert({ faces[f].texInfo, now });

				if (!put.second && put.first->second.face != (int)f)
				{
					Seen& was = put.first->second;
					bool uv = was.page == now.page && was.corners == now.corners;
					bool red = was.corners == now.corners;

					for (int k = 0; k < now.corners; k++)
					{
						uv = uv && std::fabs(was.at[k][0] - now.at[k][0]) <= 3.0 &&
							std::fabs(was.at[k][1] - now.at[k][1]) <= 3.0;
						red = red && std::fabs(was.at[k][2] - now.at[k][2]) <= 3.0;
					}

					was.other = true;
					was.same[0] = was.same[0] && uv;
					was.same[1] = was.same[1] && red;
				}
			}
		}

		long long keys = 0, keysSame[2] = {};

		for (const auto& kv : seen)
			if (kv.second.other)
			{
				keys++;
				keysSame[0] += kv.second.same[0] ? 1 : 0;
				keysSame[1] += kv.second.same[1] ? 1 : 0;
			}

		static const char* const kPair[4] =
		{ "u ~ +15", "u ~ +19", "v ~ +19", "v ~ +15" };

		std::printf("%-9s classic faces with .TRG vertices inside %lld, vertices %lld, "
			"fitted %lld: R2 >= 0.95 for +15 %lld, +19 %lld, colour1 red %lld\n",
			stem.c_str(), faceCount, vertexCount, fitted, goodFit[0], goodFit[1],
			goodFit[2]);

		for (int r = 0; r < 4; r++)
			std::printf("%-9s   pair order %s: agree %lld disagree %lld tie %lld\n",
				stem.c_str(), kPair[r], agree[r], disagree[r], tie[r]);

		std::printf("%-9s   texinfos met on two faces or more %lld: corners on the same page "
			"within 3 steps %lld, colour1 red within 3 (control) %lld\n", stem.c_str(), keys,
			keysSame[0], keysSame[1]);

		totals.faces += faceCount;
		totals.vertices += vertexCount;
		totals.fitted += fitted;

		for (int i = 0; i < 3; i++)
			totals.goodFit[i] += goodFit[i];

		for (int r = 0; r < 4; r++)
		{
			totals.agree[r] += agree[r];
			totals.disagree[r] += disagree[r];
			totals.tie[r] += tie[r];
		}

		totals.keys += keys;
		totals.keysSame[0] += keysSame[0];
		totals.keysSame[1] += keysSame[1];
	}

	void printUvTotals(const UvTotals& totals)
	{
		static const char* const kPair[4] =
		{ "u ~ +15", "u ~ +19", "v ~ +19", "v ~ +15" };

		std::printf("\nall levels: %lld classic faces with .TRG vertices inside, %lld vertices, "
			"%lld fitted: R2 >= 0.95 for +15 %lld, +19 %lld, colour1 red %lld\n",
			totals.faces, totals.vertices, totals.fitted, totals.goodFit[0],
			totals.goodFit[1], totals.goodFit[2]);

		for (int r = 0; r < 4; r++)
			std::printf("all   pair order %s: agree %lld disagree %lld tie %lld\n",
				kPair[r], totals.agree[r], totals.disagree[r], totals.tie[r]);

		std::printf("all   Spearman over %zu vertices: u~+15 %.4f  u~+19 %.4f  "
			"v~+19 %.4f  v~+15 %.4f\n", totals.cu.size(), spearman(totals.cu, totals.b15),
			spearman(totals.cu, totals.b19), spearman(totals.cv, totals.b19),
			spearman(totals.cv, totals.b15));
		std::printf("all   texinfos met on two faces or more %lld: corners on the same page "
			"within 3 steps %lld, colour1 red within 3 (control) %lld\n", totals.keys,
			totals.keysSame[0], totals.keysSame[1]);
	}

	/* A vertex normal against the plane of the triangle it belongs to, read
	 * two ways: biased by 127 ((n - 127) / 128) and as signed bytes. Counted
	 * per corner of every triangle with an area: within 10 degrees of the
	 * face normal, either way round, and within 10 degrees on the side the
	 * winding gives. */
	struct NormalCount
	{
		long long corners = 0;
		long long within[2] = {}, sameSide[2] = {};
	};

	void countNormal(const float* p0, const float* p1, const float* p2,
		const unsigned char* n, NormalCount& out)
	{
		const double e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
		const double e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
		double f[3] =
		{
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0]
		};
		const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);

		if (fl < 1e-3)
			return;

		out.corners++;

		for (int reading = 0; reading < 2; reading++)
		{
			double m[3];

			for (int c = 0; c < 3; c++)
				m[c] = reading == 0 ? ((double)n[c] - 127.0) / 128.0 : (double)(signed char)n[c];

			const double ml = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);

			if (ml < 1e-9)
				continue;

			const double cosine = (m[0] * f[0] + m[1] * f[1] + m[2] * f[2]) / (ml * fl);
			const double ten = std::cos(10.0 * 3.14159265358979 / 180.0);

			if (std::fabs(cosine) >= ten)
				out.within[reading]++;

			if (cosine >= ten)
				out.sameSide[reading]++;
		}
	}

	void printNormalCount(const char* what, const NormalCount& c)
	{
		const double n = c.corners ? (double)c.corners : 1.0;

		std::printf("%-9s corners %10lld  biased-127: within 10 deg %10lld (%5.1f %%), "
			"same side %10lld (%5.1f %%)  signed: within %10lld (%5.1f %%), same side "
			"%10lld (%5.1f %%)\n", what, c.corners, c.within[0], 100.0 * c.within[0] / n,
			c.sameSide[0], 100.0 * c.sameSide[0] / n, c.within[1], 100.0 * c.within[1] / n,
			c.sameSide[1], 100.0 * c.sameSide[1] / n);
	}

	void dumpTrgNormals(const std::string& root, const std::string& stem, NormalCount& all)
	{
		trf::remaster::Trg trg;
		std::string error;

		if (!trf::remaster::read_trg(root + "\\DATA\\" + stem + ".TRG", trg, error))
		{
			std::printf("%-9s FAILED %s\n", stem.c_str(), error.c_str());
			return;
		}

		NormalCount here;
		std::vector<std::array<uint32_t, 3>> list;

		for (size_t g = 0; g < trg.groups.size(); g++)
		{
			groupTriangles(trg, g, list);

			for (const auto& t : list)
			{
				if (t[0] >= trg.vertices.size() || t[1] >= trg.vertices.size() ||
					t[2] >= trg.vertices.size())
					continue;

				float p[3][3];

				for (int j = 0; j < 3; j++)
				{
					const trf::remaster::TrgVertex& v = trg.vertices[t[j]];
					p[j][0] = v.x;
					p[j][1] = v.y;
					p[j][2] = v.z;
				}

				for (int j = 0; j < 3; j++)
				{
					const trf::remaster::TrgVertex& v = trg.vertices[t[j]];
					const unsigned char n[3] = { v.nx, v.ny, v.nz };
					countNormal(p[0], p[1], p[2], n, here);
				}
			}
		}

		printNormalCount(stem.c_str(), here);

		/* And the other field the same header documents beside the normal:
		 * the sway weight, the fourth short of the position. */
		int swayLo = 32767, swayHi = -32768;
		long long swayed = 0;

		for (const trf::remaster::TrgVertex& v : trg.vertices)
		{
			swayLo = std::min(swayLo, (int)v.sway);
			swayHi = std::max(swayHi, (int)v.sway);
			swayed += v.sway ? 1 : 0;
		}

		std::printf("%-9s sway %d..%d, non-zero on %lld of %zu vertices\n", stem.c_str(),
			swayLo, swayHi, swayed, trg.vertices.size());

		all.corners += here.corners;

		for (int i = 0; i < 2; i++)
		{
			all.within[i] += here.within[i];
			all.sameSide[i] += here.sameSide[i];
		}
	}

	void dumpTrmNormals(const std::string& root)
	{
		NormalCount all;
		int files = 0, failed = 0;
		const std::string folder = root + "\\ITEM";

		for (const auto& entry : std::filesystem::directory_iterator(folder))
		{
			std::string ext = entry.path().extension().string();

			for (char& c : ext)
				c = (char)toupper((unsigned char)c);

			if (ext != ".TRM")
				continue;

			HDTrmModel model;
			const char* why = "";

			if (!HDTrmLoad(entry.path().string().c_str(), &model, &why))
			{
				std::printf("refused %s: %s\n", entry.path().filename().string().c_str(), why);
				failed++;
				continue;
			}

			files++;

			for (int i = 0; i + 2 < model.indices; i += 3)
			{
				const unsigned short* t = &model.index[i];

				if (t[0] >= model.vertices || t[1] >= model.vertices || t[2] >= model.vertices)
					continue;

				const HDTrmVertex* v[3] = { &model.vertex[t[0]], &model.vertex[t[1]],
					&model.vertex[t[2]] };
				const float p[3][3] =
				{
					{ v[0]->x, v[0]->y, v[0]->z },
					{ v[1]->x, v[1]->y, v[1]->z },
					{ v[2]->x, v[2]->y, v[2]->z }
				};

				for (int j = 0; j < 3; j++)
				{
					const unsigned char n[3] = { v[j]->nx, v[j]->ny, v[j]->nz };
					countNormal(p[0], p[1], p[2], n, all);
				}
			}

			HDTrmFree(&model);
		}

		std::printf("%s: %d .TRM file(s) read, %d refused\n", folder.c_str(), files, failed);
		printNormalCount("ITEM", all);
	}

	/* The sizes of the pages: every .DDS in this game's TEX and in the
	 * sibling games' TEX folders beside it, counted by the width and height
	 * of its header (offsets 16 and 12 after the four-byte magic), and the
	 * names that carry a _1024 or _2048 suffix beside a plain number. */
	void dumpDdsSizes(const std::string& root)
	{
		const std::filesystem::path five(root);
		const std::filesystem::path parent = five.parent_path();

		for (const auto& game : std::filesystem::directory_iterator(parent))
		{
			const std::filesystem::path tex = game.path() / "TEX";

			if (!game.is_directory() || !std::filesystem::is_directory(tex))
				continue;

			std::map<std::pair<int, int>, int> sizes;
			int files = 0, unread = 0, suffixed = 0;

			for (const auto& entry : std::filesystem::directory_iterator(tex))
			{
				std::string name = entry.path().filename().string();
				std::string upper = name;

				for (char& c : upper)
					c = (char)toupper((unsigned char)c);

				if (upper.size() < 4 || upper.substr(upper.size() - 4) != ".DDS")
					continue;

				files++;

				if (upper.find("_1024.DDS") != std::string::npos ||
					upper.find("_2048.DDS") != std::string::npos)
					suffixed++;

				std::FILE* f = std::fopen(entry.path().string().c_str(), "rb");
				unsigned char head[20];

				if (!f || std::fread(head, 1, 20, f) != 20 || std::memcmp(head, "DDS ", 4))
				{
					unread++;

					if (f)
						std::fclose(f);

					continue;
				}

				std::fclose(f);
				uint32_t h, w;
				std::memcpy(&h, head + 12, 4);
				std::memcpy(&w, head + 16, 4);
				sizes[{ (int)w, (int)h }]++;
			}

			std::printf("%s: %d .DDS, %d unreadable, %d named <n>_1024 or <n>_2048\n",
				tex.string().c_str(), files, unread, suffixed);

			for (const auto& kv : sizes)
				std::printf("    %5d x %-5d %6d\n", kv.first.first, kv.first.second, kv.second);
		}
	}
}

/* -flyby: the census of the camera flybys and of the heavy triggers they
 * test.
 *
 * A flyby is a run of SPOTCAM records (global/types.h, 40 bytes) sharing one
 * sequence number, read right behind the fixed cameras (LoadCameras,
 * specific/file.cpp). Tr5Level parses past them without keeping their
 * place, so the block is found by the table it follows: the fixed camera
 * count and its first and last record as cameraRecords() reads them, then
 * every count from the spotcams to the animated textures in range where the
 * layout (Tr5Level::findItems) puts it, and the "TEX" marker behind them.
 * Exactly one match is required. */
struct FlySpot
{
	int32_t x, y, z, tx, ty, tz;
	uint8_t seq, cam;
	int16_t fov, roll, timer, speed;
	uint16_t flags;
	int16_t room;
};

static bool FlyFind(const Tr5Level& lv, std::vector<FlySpot>& out,
	size_t& at, int& matches, std::string& error)
{
	std::vector<Tr5Level::CameraRecord> cams;

	out.clear();
	matches = 0;
	at = 0;

	if (!lv.cameraRecords(cams, error))
		return false;

	const std::vector<uint8_t>& b = lv.debugBytes();
	const int32_t n = (int32_t)cams.size();
	auto rd = [&](size_t p, int32_t& v)
	{
		if (p + 4 > b.size())
			return false;

		std::memcpy(&v, &b[p], 4);
		return true;
	};
	auto same = [&](size_t p, const Tr5Level::CameraRecord& c)
	{
		int32_t x, y, z;
		int16_t room, flags;

		if (p + 16 > b.size())
			return false;

		std::memcpy(&x, &b[p], 4);
		std::memcpy(&y, &b[p + 4], 4);
		std::memcpy(&z, &b[p + 8], 4);
		std::memcpy(&room, &b[p + 12], 2);
		std::memcpy(&flags, &b[p + 14], 2);
		return x == c.x && y == c.y && z == c.z && room == c.room &&
			(uint16_t)flags == (uint16_t)c.flags;
	};

	for (size_t p = lv.mainBlockOffset(); p + 4 <= b.size(); p++)
	{
		int32_t v, s, m, boxes;

		if (!rd(p, v) || v != n)
			continue;

		if (n > 0 && (!same(p + 4, cams[0]) ||
			!same(p + 4 + 16 * (size_t)(n - 1), cams[(size_t)n - 1])))
			continue;

		const size_t q = p + 4 + 16 * (size_t)n;

		if (!rd(q, s) || s < 0 || s > 256)
			continue;

		const size_t r = q + 4 + 40 * (size_t)s;

		if (!rd(r, m) || m < 0 || m > 4096)
			continue;

		const size_t bx = r + 4 + 16 * (size_t)m;

		if (!rd(bx, boxes) || boxes <= 0 || boxes > 65535)
			continue;

		// Overlaps, the zones (ten shorts per box), the animated texture
		// words, the one UV range byte, then the "TEX" marker.
		const size_t ov = bx + 4 + 8 * (size_t)boxes;
		int32_t o, a;

		if (!rd(ov, o) || o < 0 || o > 1 << 20)
			continue;

		const size_t an = ov + 4 + 2 * (size_t)o + 20 * (size_t)boxes;

		if (!rd(an, a) || a < 0 || a > 1 << 16)
			continue;

		const size_t tex = an + 4 + 2 * (size_t)a + 1;

		if (tex + 3 > b.size() || std::memcmp(&b[tex], "TEX", 3))
			continue;

		matches++;
		at = q;
	}

	if (matches != 1)
	{
		error = "spotcam block not located uniquely";
		return false;
	}

	int32_t s = 0;

	rd(at, s);

	for (int32_t i = 0; i < s; i++)
	{
		FlySpot f;

		std::memcpy(&f, &b[at + 4 + 40 * (size_t)i], 38);
		out.push_back(f);
	}

	return true;
}

/* The sector whose floordata a test at (x, y, z) in `room` reads, the way
 * GetFloor and GetHeight (game/control.cpp) walk: the door chain in the
 * room first (the edge rows clamped as GetFloor clamps them), then down
 * every pit room to the lowest floor, where GetHeight takes trigger_index.
 * The no-collision triangles that stop either walk early are not modelled;
 * a camera place over one is named by its sector's floordata anyway. */
static bool FlySector(const Tr5Level& lv, long x, long z, int room,
	int& outRoom, int& outSx, int& outSz, int& outFd)
{
	const std::vector<Tr5Room>& rooms = lv.rooms();
	std::vector<Tr5Level::SectorInfo> info;
	std::string error;
	bool door = true;

	for (int hop = 0; hop < 64; hop++)
	{
		if (room < 0 || room >= (int)rooms.size())
			return false;

		const Tr5Room& r = rooms[(size_t)room];
		long zs = (z - r.originZ) >> 10, xs = (x - r.originX) >> 10;

		if (!door)
		{
			if (zs < 0 || zs >= r.numZSectors || xs < 0 ||
				xs >= r.numXSectors)
				return true;
		}
		else if (zs <= 0 || zs >= r.numZSectors - 1)
		{
			zs = zs <= 0 ? 0 : r.numZSectors - 1;
			xs = std::max(1L, std::min(xs, (long)r.numXSectors - 2));
		}
		else
			xs = std::max(0L, std::min(xs, (long)r.numXSectors - 1));

		if (!lv.sectorInfo(room, info, error))
			return false;

		const Tr5Level::SectorInfo& s =
			info[(size_t)(xs * r.numZSectors + zs)];

		if (door && s.doorTo >= 0)
		{
			room = s.doorTo;
			continue;
		}

		door = false;
		outRoom = room;
		outSx = (int)xs;
		outSz = (int)zs;
		outFd = s.fdIndex;

		if (s.roomBelow == 255)
			return true;

		room = s.roomBelow;
	}

	return false;
}

static const char* FlyKind(int kind)
{
	static const char* const k[] =
	{
		"TRIGGER", "PAD", "SWITCH", "KEY", "PICKUP", "HEAVY", "ANTIPAD",
		"COMBAT", "DUMMY", "ANTITRIGGER", "HEAVYSWITCH",
		"HEAVYANTITRIGGER", "MONKEY", "SKELETON", "TIGHTROPE",
		"CRAWLDUCK", "CLIMB"
	};

	return kind >= 0 && kind < 17 ? k[kind] : "?";
}

/* One sector's trigger as _TestTriggers reads it: the TRIGGER entry, its
 * flags word, the reference word of SWITCH, KEY and PICKUP, then the
 * commands up to the end bit - for a camera or a flyby the end bit of the
 * extra word that follows, as the engine reads it. The words are the
 * chain's own, in file order, out of readChain. `kind` is -1 when the
 * sector has no trigger. */
struct FlyCmd
{
	int type, value, extra;
};

static void FlyTrigger(const Tr5Level& lv, int fd, int& kind, int& flags,
	std::vector<FlyCmd>& cmds)
{
	std::vector<Tr5FloorEntry> chain;
	std::vector<uint16_t> w;

	kind = -1;
	flags = 0;
	cmds.clear();

	if (fd <= 0 || !lv.readChain(fd, chain))
		return;

	for (size_t e = 0; e < chain.size(); e++)
		w.insert(w.end(), chain[e].words.begin(), chain[e].words.end());

	size_t i = 0;

	while (i < w.size())
	{
		const uint16_t h = w[i++];

		if ((h & 0x1F) != 4)
		{
			if (h & 0x8000)
				return;

			i += (size_t)Tr5Floor::extraWords(h & 0x1F);
			continue;
		}

		kind = (h >> 8) & 0x3F;

		if (i >= w.size())
			return;

		flags = w[i++];

		if (Tr5Floor::hasRefWord(kind))
		{
			if (i >= w.size())
				return;

			FlyCmd c = { -1, w[i++] & 0x3FF, 0 };

			cmds.push_back(c);
		}

		while (i < w.size())
		{
			uint16_t t = w[i++];
			FlyCmd c = { Tr5Floor::cmdType(t), t & 0x3FF, 0 };

			if ((c.type == 1 || c.type == 12) && i < w.size())
			{
				t = w[i++];
				c.extra = t;
			}

			cmds.push_back(c);

			if (t & 0x8000)
				return;
		}

		return;
	}
}

static std::string FlyFlags(unsigned f)
{
	static const char* const n[16] =
	{
		"snap", "vignette", "loop", "track", "nodrawlara", "targetlara",
		"snapback", "jumpto", "quake", "nobreak", "nolaractl", "laractl",
		"fadein", "fadeout", "TEST", "oneshot"
	};
	std::string s;

	for (int i = 0; i < 16; i++)
		if (f & (1u << i))
			s += (s.empty() ? "" : "|") + std::string(n[i]);

	return s.empty() ? "-" : s;
}

/* What a sector's trigger does, one line: the kind, whether a heavy test
 * with HeavyFlags 0 acts on it, and every command with its target - an
 * item by number, slot and slot name, its room and place. */
static std::string FlyDescribe(const Tr5Level& lv, int fd)
{
	static const char* const to[] =
	{
		"object", "camera", "sink", "flipmap", "flipon", "flipoff",
		"target", "finish", "cd", "flipeffect", "secret", "bodybag",
		"flyby", "cutscene"
	};
	int kind, flags;
	std::vector<FlyCmd> cmds;
	char buf[256];

	FlyTrigger(lv, fd, kind, flags, cmds);

	if (kind < 0)
		return "no trigger";

	const bool acts = kind == 5 || kind == 11;
	std::string s;

	std::snprintf(buf, sizeof(buf), "%s%s timer=%d mask=%02X%s:",
		FlyKind(kind), acts ? " (heavy test acts)" : " (heavy test: no)",
		flags & 0xFF, (flags >> 9) & 0x1F, flags & 0x100 ? " oneshot" : "");
	s = buf;

	for (size_t i = 0; i < cmds.size(); i++)
	{
		const FlyCmd& c = cmds[i];
		Tr5Item it;

		if (c.type < 0)
			std::snprintf(buf, sizeof(buf), " ref#%d", c.value);
		else if (c.type == 0 && lv.getItem(c.value, it))
		{
			const char* name = HDSlotName(it.objectNumber);

			std::snprintf(buf, sizeof(buf),
				" object#%d=%s(slot %d,room %d,%d,%d,%d,ocb %d)", c.value,
				name ? name : "?", it.objectNumber, it.roomNumber, it.x,
				it.y, it.z, it.triggerFlags);
		}
		else if (c.type == 1 || c.type == 12)
			std::snprintf(buf, sizeof(buf), " %s#%d(word %04X)",
				to[c.type], c.value, c.extra);
		else
			std::snprintf(buf, sizeof(buf), " %s#%d",
				c.type < 14 ? to[c.type] : "?", c.value);

		s += buf;
	}

	return s;
}

static int FlybyCensus(const std::string& folder,
	std::vector<std::string> levels)
{
	if (levels.empty())
		levels.assign(std::begin(kLevels), std::end(kLevels));

	int failed = 0;

	for (const std::string& name : levels)
	{
		Tr5Level lv;
		std::string error;
		std::vector<FlySpot> sc;
		size_t at = 0;
		int matches = 0;
		const std::string path = folder + "\\" + name + ".TRC";

		if (!lv.load(toW(path), error) ||
			!FlyFind(lv, sc, at, matches, error))
		{
			std::printf("%s: %s (matches %d)\n", name.c_str(),
				error.c_str(), matches);
			failed++;
			continue;
		}

		int nseq = 0, nrisk = 0, ntest = 0, nway = 0;

		std::printf("level %s spotcams %d at 0x%zX\n", name.c_str(),
			(int)sc.size(), at);

		for (size_t i = 0; i < sc.size(); )
		{
			size_t j = i;

			while (j + 1 < sc.size() && sc[j + 1].seq == sc[i].seq)
				j++;

			nseq++;

			// The end branch takes the first camera's flags (s->flags).
			const unsigned first = sc[i].flags, last = sc[j].flags;
			const bool open = !(first & 0x0C);
			const bool risk = SpotSeqEndsShort((int)first, (int)last);
			const bool way = open && !(first & 0x40) && (last & 0x4000);

			nrisk += risk ? 1 : 0;
			nway += way ? 1 : 0;
			std::printf("seq %d cams %zu..%zu (%zu) first %04X last %04X "
				"end %s%s\n", sc[i].seq, i, j, j - i + 1, first, last,
				!open ? "loop/track" :
				(first & 0x40) ? "snapback" : "spline_to_camera",
				risk ? " AT-RISK" : way ? " last-tests-twice" : "");

			for (size_t k = i; k <= j; k++)
			{
				const FlySpot& f = sc[k];

				std::printf("  cam %zu (%d) flags %04X %s pos %d,%d,%d "
					"room %d timer %d speed %d\n", k, f.cam,
					(unsigned)f.flags, FlyFlags(f.flags).c_str(), f.x, f.y,
					f.z, f.room, f.timer, f.speed);

				if (!(f.flags & 0x4000))
					continue;

				int r = -1, sx = 0, sz = 0, fd = 0;

				ntest++;

				if (!FlySector(lv, f.x, f.z, f.room, r, sx, sz, fd))
				{
					std::printf("    test: sector not found\n");
					continue;
				}

				const int alt = lv.rooms()[(size_t)r].altRoom;

				std::printf("    test%s: room %d sector %d,%d fd %d%s -> %s\n",
					k == j ? " (last)" : "", r, sx, sz, fd,
					alt >= 0 ? " (flip twin)" : "",
					FlyDescribe(lv, fd).c_str());
			}

			// Where the end test lands when the skip ends the sequence: the
			// frame stands at the camera before the last.
			if (risk && j > i)
			{
				int r = -1, sx = 0, sz = 0, fd = 0;
				const FlySpot& f = sc[j - 1];

				if (FlySector(lv, f.x, f.z, f.room, r, sx, sz, fd))
					std::printf("    skip tests at cam %zu instead: room %d "
						"sector %d,%d fd %d -> %s\n", j - 1, r, sx, sz, fd,
						FlyDescribe(lv, fd).c_str());
			}

			i = j + 1;
		}

		// The floor triggers that start a flyby or a CUTSEQ cutscene, one
		// line per floordata chain with the sectors that share it.
		std::map<int, std::string> starts;

		for (size_t r = 0; r < lv.rooms().size(); r++)
		{
			std::vector<Tr5Level::SectorInfo> info;

			if (!lv.sectorInfo((int)r, info, error))
				continue;

			for (const Tr5Level::SectorInfo& s : info)
			{
				int kind, flags;
				std::vector<FlyCmd> cmds;
				char buf[64];

				FlyTrigger(lv, s.fdIndex, kind, flags, cmds);

				bool hit = false;

				for (const FlyCmd& c : cmds)
					hit = hit || c.type == 12 || c.type == 13;

				if (!hit)
					continue;

				std::snprintf(buf, sizeof(buf), " %zu:%d,%d", r, s.sx, s.sz);
				starts[s.fdIndex] += buf;
			}
		}

		for (const auto& kv : starts)
			std::printf("start fd %d -> %s; at%s\n", kv.first,
				FlyDescribe(lv, kv.first).c_str(), kv.second.c_str());

		std::printf("summary %s: %d sequences, %d cameras, %d test "
			"cameras, %d at risk, %d last-tests-twice, %d start chains\n",
			name.c_str(), nseq, (int)sc.size(), ntest, nrisk, nway,
			(int)starts.size());
	}

	return failed ? 1 : 0;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: TrgProbe <...\\5> [level ...] [-all] [-bc7 [threads]]\n");
		std::printf("       [-classic <folder holding the classic .TRC>]  for the room count\n");
		std::printf("       [-pages]  which page numbers have no .DDS in TEX\n");
		std::printf("       [-mat [material]]  the distinct (material, param) tuples,\n");
		std::printf("                          with triangles per pass and groups;\n");
		std::printf("                          a material 15 tuple adds its books'\n");
		std::printf("                          page runs\n");
		std::printf("       [-parts <group>]  every part of one group with its box and\n");
		std::printf("                          pages; a material 15 part adds its book\n");
		std::printf("       [-seam <groupA> <groupB> <dx> <dy> <dz>]  the baked light\n");
		std::printf("                          where two rooms share vertex positions\n");
		std::printf("       [-flat <group>]  the horizontal surfaces of one group, with\n");
		std::printf("                        the page each of them wears (the water survey)\n");
		std::printf("       [-cell <group> <x> <z>]  every triangle of one"
			" group over\n");
		std::printf("                        one room-local point, in"
			" every pass\n");
		std::printf("       [-box <group> <x0> <x1> <y0> <y1> <z0> <z1>]\n");
		std::printf("                        every part of one group (-1: of"
			" every group)\n");
		std::printf("                        with corners in a room-local"
			" box: the\n");
		std::printf("                        triangles with a corner or their"
			" centroid\n");
		std::printf("                        inside, the corners' heights and"
			" colours\n");
		std::printf("       [-boxes <file>]  the same over many boxes, one"
			" `label group\n");
		std::printf("                        x0 x1 y0 y1 z0 z1` a line, as"
			" counts\n");
		std::printf("       [-layers]  the texture list: which page each"
			" layer names\n");
		std::printf("       [-squares <group> <y0> <y1> <sx0> <sx1> <sz0>"
			" <sz1>]\n");
		std::printf("                        per floor square, how much"
			" of it each\n");
		std::printf("                        part covers at a height in"
			" y0..y1\n");
		std::printf("       [-layer0]  how much of a level names no page at all\n");
		std::printf("       [-gloss]  every page that shines, with its source (material,\n");
		std::printf("                 file, seed, table) and the sheen's numbers;\n");
		std::printf("                 the seed needs -classic\n");
		std::printf("       [-glossseed]  the seed's evidence per page and the pages\n");
		std::printf("                     numbered next to each candidate (needs -classic)\n");
		std::printf("       [-glossfile <path>]  an hd_gloss.txt to resolve with\n");
		std::printf("       [-glosslight]  what the sheen's lighting rests on: the light\n");
		std::printf("                      cubes' order, faces and gain, the lamps' kinds\n");
		std::printf("                      and the sun (needs -classic)\n");
		std::printf("       [-glossfield]  the light field the sheen's highlight reads:\n");
		std::printf("                      the lamps, the grid, its memory and build time,\n");
		std::printf("                      the light it carries through walls and its\n");
		std::printf("                      weakest place (needs -classic)\n");
		std::printf("       [-matparts <material>]  every part of ONE material with its\n");
		std::printf("                          geometry, and (with -classic) the classic\n");
		std::printf("                          room's own horizontal planes beside it\n");
		std::printf("       [-classicart]  the rooms drawn classic (no, empty or hollow\n");
		std::printf("                      group), the HD art of other groups in their\n");
		std::printf("                      whole box (inside / wall ring, pair or next\n");
		std::printf("                      door) and how much of their classic faces it\n");
		std::printf("                      covers (needs -classic); -room <n> lists the\n");
		std::printf("                      uncovered faces, -arttol <units> (128),\n");
		std::printf("                      -artstep <units> (64), -artfacing\n");
		std::printf("                      same|against|any, -artcontrol <rooms> (12);\n");
		std::printf("                      -perface adds the rule per face: the\n");
		std::printf("                      renderer's tag, each holder's carrier and\n");
		std::printf("                      state, every face covered or kept (bare or\n");
		std::printf("                      mixed, floor or not), the carrier's\n");
		std::printf("                      triangles by kind, and the sectors of the\n");
		std::printf("                      classic rooms without a face\n");
		std::printf("       [-shadeorder]  the classic room vertices' own shade against\n");
		std::printf("                      the .TRG colour triples at the same position:\n");
		std::printf("                      rank correlation and pair order of c1+c2,\n");
		std::printf("                      c1*c2, c1 and c2 (needs -classic)\n");
		std::printf("       [-uvbytes]  which bytes are u and v: value ranges, corner\n");
		std::printf("                   order against the classic texinfo and the\n");
		std::printf("                   one-texinfo-one-place test (needs -classic)\n");
		std::printf("       [-normals]  the .TRG vertex normal against its triangle,\n");
		std::printf("                   biased by 127 and as signed bytes\n");
		std::printf("       [-trmnormals]  the same over every .TRM of the ITEM folder\n");
		std::printf("       [-ddssizes]  every .DDS of this game's and its siblings'\n");
		std::printf("                    TEX by the size in its header\n");
		std::printf("usage: TrgProbe -flyby <classic folder> [level ...]"
			"  the flybys and their heavy triggers\n");
		return 1;
	}

	if (!_stricmp(argv[1], "-flyby") && argc > 2)
		return FlybyCensus(argv[2],
			std::vector<std::string>(argv + 3, argv + argc));

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
	int cell_group = -1, cell_x = 0, cell_z = 0;
	int box_group = -2, box_args[6] = {};
	const char* boxes_file = 0;
	bool list_layers = false;
	int sq_group = -1, sq_args[7] = {};
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
	bool gloss = false;
	bool gloss_seed = false;
	bool gloss_light = false;
	bool gloss_field = false;
	std::string gloss_file;
	bool classic_art = false;
	bool vis_table = false;
	int vis_room = -1;
	std::vector<double> vis_reaches;
	double art_tol = 128.0;
	double art_step = 64.0;
	int art_facing = 1;
	int art_controls = 12;
	bool art_per_face = false;
	bool shade_order = false;
	bool uv_bytes = false;
	bool trg_normals = false;
	bool trm_normals = false;
	bool dds_sizes = false;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-shadeorder"))
			shade_order = true;
		else if (!_stricmp(argv[i], "-uvbytes"))
			uv_bytes = true;
		else if (!_stricmp(argv[i], "-normals"))
			trg_normals = true;
		else if (!_stricmp(argv[i], "-trmnormals"))
			trm_normals = true;
		else if (!_stricmp(argv[i], "-ddssizes"))
			dds_sizes = true;
		else if (!_stricmp(argv[i], "-classicart"))
			classic_art = true;
		else if (!_stricmp(argv[i], "-vistable"))
			vis_table = true;
		else if (!_stricmp(argv[i], "-visroom") && i + 1 < argc)
			vis_room = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-visreach") && i + 1 < argc)
		{
			vis_reaches.clear();

			for (const char* s = argv[++i]; *s; )
			{
				vis_reaches.push_back(std::atof(s));

				while (*s && *s != ',')
					s++;

				if (*s == ',')
					s++;
			}
		}
		else if (!_stricmp(argv[i], "-perface"))
			art_per_face = true;
		else if (!_stricmp(argv[i], "-arttol") && i + 1 < argc)
			art_tol = std::atof(argv[++i]);
		else if (!_stricmp(argv[i], "-artstep") && i + 1 < argc)
			art_step = std::atof(argv[++i]);
		else if (!_stricmp(argv[i], "-artcontrol") && i + 1 < argc)
			art_controls = std::atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-artfacing") && i + 1 < argc)
		{
			++i;
			art_facing = !_stricmp(argv[i], "any") ? 0 :
				(!_stricmp(argv[i], "against") ? -1 : 1);
		}
		else if (!_stricmp(argv[i], "-gloss"))
			gloss = true;
		else if (!_stricmp(argv[i], "-glosslight"))
			gloss_light = true;
		else if (!_stricmp(argv[i], "-glossfield"))
			gloss_field = true;
		else if (!_stricmp(argv[i], "-glossseed"))
			gloss_seed = true;
		else if (!_stricmp(argv[i], "-glossfile") && i + 1 < argc)
			gloss_file = argv[++i];
		else if (!_stricmp(argv[i], "-roomspace"))
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
		else if (!_stricmp(argv[i], "-cell") && i + 3 < argc)
		{
			cell_group = std::atoi(argv[i + 1]);
			cell_x = std::atoi(argv[i + 2]);
			cell_z = std::atoi(argv[i + 3]);
			i += 3;
		}
		else if (!_stricmp(argv[i], "-box") && i + 7 < argc)
		{
			box_group = std::atoi(argv[i + 1]);

			for (int k = 0; k < 6; k++)
				box_args[k] = std::atoi(argv[i + 2 + k]);

			i += 7;
		}
		else if (!_stricmp(argv[i], "-boxes") && i + 1 < argc)
			boxes_file = argv[++i];
		else if (!_stricmp(argv[i], "-layers"))
			list_layers = true;
		else if (!_stricmp(argv[i], "-squares") && i + 7 < argc)
		{
			for (int k = 0; k < 7; k++)
				sq_args[k] = std::atoi(argv[i + 1 + k]);
			sq_group = sq_args[0];
			i += 7;
		}
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

	if (shade_order || uv_bytes)
	{
		if (classic.empty())
		{
			std::printf("-shadeorder and -uvbytes need -classic <folder holding the .TRC>\n");
			return 1;
		}

		if (shade_order)
		{
			ShadeTotals totals;
			bool first = true;

			for (const std::string& stem : levels)
			{
				dumpShadeOrder(root, stem, classic, totals, first);
				first = false;
			}

			printShadeTotals(totals);
		}

		if (uv_bytes)
		{
			UvTotals totals;

			for (const std::string& stem : levels)
				dumpUvBytes(root, stem, classic, totals);

			printUvTotals(totals);
		}

		return 0;
	}

	if (trg_normals)
	{
		NormalCount all;

		for (const std::string& stem : levels)
			dumpTrgNormals(root, stem, all);

		printNormalCount("all", all);
		return 0;
	}

	if (trm_normals)
	{
		dumpTrmNormals(root);
		return 0;
	}

	if (dds_sizes)
	{
		dumpDdsSizes(root);
		return 0;
	}

	if (gloss_light)
	{
		std::vector<double> gains;

		for (const std::string& stem : levels)
		{
			const double gain = glossLight(root, stem, classic);

			if (gain > 0.0)
				gains.push_back(gain);
		}

		std::sort(gains.begin(), gains.end());

		if (!gains.empty())
			std::printf("\nthe median of the levels' gain medians: %.2f over %zu level(s)\n",
				gains[gains.size() / 2], gains.size());

		return 0;
	}

	if (gloss_field)
	{
		FieldTotals totals;

		for (const std::string& stem : levels)
			glossField(root, stem, classic, totals);

		std::sort(totals.channel.begin(), totals.channel.end());

		if (!totals.channel.empty())
			std::printf("\nevery level's lamps: %zu, largest channel median %.1f p90 %.1f "
				"max %.1f\n", totals.channel.size(), totals.channel[totals.channel.size() / 2],
				totals.channel[totals.channel.size() * 9 / 10], totals.channel.back());

		return 0;
	}

	if (gloss || gloss_seed)
	{
		/* The override file as the engine reads it (hd_render.cpp): the
		 * whole text through HDGlossParseText. Without -glossfile there is
		 * none, which is the engine without an hd_gloss.txt. */
		HDGlossFile file;

		if (!gloss_file.empty())
		{
			std::FILE* f = std::fopen(gloss_file.c_str(), "rb");
			std::string text;

			if (f)
			{
				char buffer[4096];
				size_t got;

				while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
					text.append(buffer, got);

				std::fclose(f);
				HDGlossParseText(text.c_str(), &file);
			}
			else
				std::printf("-glossfile: %s cannot be read - no file\n", gloss_file.c_str());
		}

		for (const std::string& stem : levels)
			dumpGloss(root, stem, classic, gloss_seed, file);

		return 0;
	}

	if (vis_table)
	{
		if (classic.empty())
		{
			std::printf("-vistable needs -classic <folder holding the .TRC>\n");
			return 1;
		}

		for (const std::string& stem : levels)
			dumpVisTable(root, stem, classic, vis_room, vis_reaches);

		return 0;
	}

	if (classic_art)
	{
		if (classic.empty())
		{
			std::printf("-classicart needs -classic <folder holding the .TRC>\n");
			return 1;
		}

		for (const std::string& stem : levels)
			dumpClassicArt(root, stem, classic, room_focus, art_tol, art_step,
				art_facing, art_controls, art_per_face);

		return 0;
	}

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

	if (sq_group >= 0)
	{
		// -squares <group> <y0> <y1> <sx0> <sx1> <sz0> <sz1>
		for (const std::string& stem : levels)
			dumpSquares(root, stem, sq_group, sq_args[1], sq_args[2],
				sq_args[3], sq_args[4], sq_args[5], sq_args[6]);

		return 0;
	}

	// -layers
	if (list_layers)
	{
		for (const std::string& stem : levels)
			dumpLayers(root, stem);

		return 0;
	}

	// -boxes <file>
	if (boxes_file)
	{
		for (const std::string& stem : levels)
			dumpBoxes(root, stem, boxes_file);

		return 0;
	}

	// -box <group> <x0> <x1> <y0> <y1> <z0> <z1>
	if (box_group >= -1)
	{
		for (const std::string& stem : levels)
			dumpBox(root, stem, box_group, box_args[0], box_args[1],
				box_args[2], box_args[3], box_args[4], box_args[5]);

		return 0;
	}

	// -cell <group> <x> <z>
	if (cell_group >= 0)
	{
		for (const std::string& stem : levels)
			dumpCell(root, stem, cell_group, cell_x, cell_z);

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

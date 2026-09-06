// TrgProbe - what a Remaster level's .TRG contains, and what its pages cost
// to decode (#123, stage S0).
//
// Occasion: stage S1 will draw the .TRG's rooms, one draw call per
// (room, pass, page). Two numbers decide whether that plan holds, and neither
// of them is a number anybody has: how many such draw calls a level really
// needs, and how long our own BC7 decoder (specific\bc7.cpp) takes over every
// page of a level with its whole mip chain. This measures both, over the real
// files, with the SAME code the engine runs - hd_scan.cpp is compiled in, not
// reimplemented.
//
// Usage:
//   TrgProbe <path to the game 5 folder> [level ...]
//   TrgProbe <path to the game 5 folder> -all
//   TrgProbe <path to the game 5 folder> -bc7 [threads]   decode timing only
//   TrgProbe -find [override folder]                      run the discovery
//
// -find runs the ENGINE's own search for an installed Tomb Raider IV-VI
// Remastered (specific\hd\hd_install.cpp is compiled in): the registry
// override first, then every Steam library. It prints what it found and the
// diagnostics it would put in the log.
//
// "<path to the game 5 folder>" is the "5" inside an installed Tomb Raider
// IV-VI Remastered, the one with DATA, TEX and ITEM in it. Nothing is written
// and nothing is copied - the asset rule stands (see docs\LEGAL of TROne and
// THIRD-PARTY.md here): the Remaster's files stay where they are.
//
// The room count comes from a CLASSIC level folder handed in with -classic,
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
#include <map>

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

	/* THE DISTINCT (MATERIAL, PARAM) TUPLES OF A LEVEL (#123, fix round E).
	 *
	 * Every rule the HD path takes out of a .TRG part - the cut-out threshold
	 * of fix round A2, the additive weight and period of R3a, the flipbook
	 * layers of R3b - is a reading of one of the four floats beside the
	 * material, and every one of them was argued from the DISTRIBUTION of
	 * those floats over the fifteen files. Until fix round E that
	 * distribution was measured by hand, once, and written into a comment.
	 * This prints it, so the next reading can be checked against the file
	 * rather than against the comment: one line per distinct tuple, with the
	 * triangles it draws in each of the three passes and the groups it
	 * appears in.
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

	/* THE BAKED LIGHT AT A PORTAL (#123, fix round E, E7).
	 *
	 * The player reports a step in the wall brightness along a straight
	 * vertical line at a room boundary. Three things could put it there: a
	 * dynamic light the HD path culls where the classic path keeps it, the
	 * additive pass drawn in the wrong order (fix round E6), or the
	 * remaster's own baked light, which is per room and could simply differ
	 * on the two sides of the seam.
	 *
	 * This measures the third. Two groups and the world offset between their
	 * room origins go in; every vertex of the first that lands on a vertex of
	 * the second comes out, with what the two carry as light - colour1 +
	 * colour2, which is what the shader multiplies the texel by. A seam whose
	 * pairs agree cannot be the step; one whose pairs differ IS the step, and
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
			 * purpose (hd_room.hlsl, THE PARITY BLOCK): the classic one pins
			 * its multiply at 1 and carries the rest as an add, ours goes on
			 * multiplying. A seam whose sides straddle 1 therefore steps
			 * further here than it did in 2000, and this counts those. */
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

	/* THE HORIZONTAL SURFACES OF ONE GROUP (#123, fix round H, H1).
	 *
	 * A TR5 water surface is a flat quad at the room's water height, and the
	 * .TRG marks it as nothing at all - stage S5 measured that and fix round
	 * R3b confirmed it. So the only way to ask "which page is our water
	 * wearing" is to find the flat surfaces geometrically and read the page
	 * off them.
	 *
	 * One line per (height, page, material, pass): the triangles that lie in
	 * that plane, their x/z box, and the LAYER the .TRG gave them - because
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

	/* EVERY PART OF ONE GROUP (#123, fix round H, H1): material, parameters,
	 * the page its first triangle wears, the triangles per pass and the
	 * room-local box each pass covers. The -mat table says what a level's
	 * materials are; this says where in ONE room they sit. */
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

				/* EVERY page the pass touches, not only its first (#123, fix
				 * round H, H2). A part is one material, not one sheet: the
				 * glass wall of ANDREA2 wears four. */
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

	/* HOW MUCH OF A LEVEL IS ON LAYER 0 (#123, fix round H, H1). Per level:
	 * vertices with layer 0, triangles whose FIRST corner carries it - which
	 * is the corner hd_render.cpp reads - and the material those triangles
	 * belong to. The renderer counts the triangles as fallbackPageTris; this
	 * says which materials and which groups they are, without a device. */
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

	/* THE CLASSIC ROOM'S FLAT FACES BESIDE THE .TRG's (#123, fix round H, H1).
	 *
	 * The player's finding in trview is that a vanilla water face wears a wave
	 * texture and the HD one "has no assignment". The two meshes can be held
	 * against each other because a TR5 room vertex carries its y in WORLD
	 * units (drawroom.cpp:531 compares pos.y with r->minfloor straight) and so
	 * does a .TRG vertex - only x and z are room local, and the room's origin
	 * is known. So both sides are listed as "horizontal faces per height", and
	 * a height the classic side has and the HD side does not is a surface the
	 * remaster did not build. */
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

		/* And the same heights out of the .TRG, in WORLD x and z. */
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

	/* THE MISSING WATER SURFACE, OVER A WHOLE LEVEL (#123, fix round H, H1).
	 *
	 * The classic engine draws a water surface as ordinary room faces at the
	 * room's own minfloor, and the shimmer branch that makes them move opens
	 * with `pos.y == CurrentRoomPtr->minfloor` in a room carrying
	 * ROOM_REFLECT or ROOM_MIST (drawroom.cpp:531-545). So for every such
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
		std::printf("                        the page each of them wears (H1, the water)\n");
		std::printf("       [-layer0]  how much of a level names no page at all\n");
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

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-watergap"))
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
		// WHICH PAGE A LEVEL NAMES AND THE TEX FOLDER DOES NOT HAVE.
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

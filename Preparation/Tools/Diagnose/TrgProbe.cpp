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

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-seam") && i + 5 < argc)
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

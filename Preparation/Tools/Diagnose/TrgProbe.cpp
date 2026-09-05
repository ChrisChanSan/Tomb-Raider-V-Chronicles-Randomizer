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

#include "core/Tr5Level.h"
#include "hd_install.h"
#include "hd_scan.h"

// The engine keeps this in tomb5\tomb5.cpp, filled from the registry value
// hd_install. Here the command line fills it - the tool has no registry of
// its own and must not write into the game's.
char tomb5_hd_install[260];

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
	int threads = 0;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-classic") && i + 1 < argc)
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

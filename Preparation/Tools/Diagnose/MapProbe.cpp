// MapProbe - which .TRM the Remaster draws for each of a level's slots
// (#123, stage S0).
//
// Occasion: the HD path replaces an object's mesh with a model of its own,
// one file per slot in <5>\ITEM. Which file that is comes from the level's
// own LEVELn.MAP where it names one, and from the slot's name otherwise -
// plus our hand table for the slots this project added beyond the original
// 460 (BLUE_GUARD2, ROMAN_GOD2, CYBORG, the MESHSWAP_CROSS_* warehouses).
// Whether that rule actually finds a file is a measurement, and a randomized
// level is the case that matters: it carries slots no vanilla level has.
//
// This tool runs the ENGINE's binding (specific\hd\hd_bind.cpp is compiled
// in), so what it reports is what the game will bind.
//
// Usage:
//   MapProbe <path to the game 5 folder> <level.trc> [more.trc ...]
//   MapProbe <path to the game 5 folder> -all          the 15 shipped levels
//   MapProbe ... -v                                    list every binding
//   MapProbe ... -alt <path to the game 4 folder>      also look there for a
//                                                      missing model
//   MapProbe ... -map <STEM>                           use that level's .MAP
//                                                      for every file named
//
// -map is what a randomized level needs: its file is called whatever the run
// called it, but the .MAP that belongs to it is the origin level's. Without
// it every slot binds by name alone and the missing count is meaningless.
//
// The slot list comes from the .TRC handed in, read with the randomizer's own
// Tr5Level - so pointing it at a randomized level is the whole point.
// Nothing is written and nothing is copied out of the Remaster.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/Tr5Level.h"
#include "hd_bind.h"
#include "hd_trm.h"

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

	std::string stemOf(const std::string& path)
	{
		size_t start = path.find_last_of("\\/");
		start = (start == std::string::npos) ? 0 : start + 1;
		size_t dot = path.find_last_of('.');
		if (dot == std::string::npos || dot < start)
			dot = path.size();
		std::string out = path.substr(start, dot - start);
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = (char)toupper((unsigned char)out[i]);
		return out;
	}

	bool exists(const std::string& path)
	{
		return HDTrmExists(path.c_str());
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf("usage: MapProbe <...\\5> <level.trc> [...] | -all\n");
		std::printf("       [-v] [-alt <...\\4>] [-map <STEM>]\n");
		return 1;
	}

	const std::string root = argv[1];
	const std::string item = root + "\\ITEM";
	std::string alt;
	std::string mapStem;
	bool verbose = false;
	std::vector<std::string> targets;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-v"))
			verbose = true;
		else if (!_stricmp(argv[i], "-alt") && i + 1 < argc)
			alt = argv[++i];
		else if (!_stricmp(argv[i], "-map") && i + 1 < argc)
			mapStem = argv[++i];
		else if (!_stricmp(argv[i], "-all"))
			for (const char* name : kLevels)
				targets.push_back(root + "\\DATA\\" + name + ".TRC");
		else
			targets.push_back(argv[i]);
	}

	std::printf("install : %s\n\n", root.c_str());
	std::printf("%-28s %5s %5s %6s %6s %6s %6s %6s %6s\n",
		"level", "map", "slots", "bound", "miss", "dummy", "lara", "heads", "alt");
	std::printf("%-28s %5s %5s %6s %6s %6s %6s %6s %6s\n",
		"----------------------------", "-----", "-----", "------", "------",
		"------", "------", "------", "------");

	for (const std::string& path : targets)
	{
		Tr5Level level;
		std::string error;
		if (!level.load(toW(path), error))
		{
			std::printf("%-28s not loadable: %s\n", stemOf(path).c_str(), error.c_str());
			continue;
		}

		const std::string stem = stemOf(path);
		const std::string mapPath = root + "\\DATA\\" +
			(mapStem.empty() ? stem : mapStem) + ".MAP";
		HDMapLoad(mapPath.c_str());

		const std::vector<int> slots = level.moveableSlots();
		int bound = 0, miss = 0, dummy = 0, lara = 0, heads = 0, recovered = 0;
		std::vector<std::string> missing;

		for (size_t k = 0; k < slots.size(); ++k)
		{
			const int slot = slots[k];
			HDBindKind kind = HD_BIND_NONE;
			bool fromMap = false;
			const char* model = HDBindSlot(slot, &kind, &fromMap);
			const char* slotName = HDSlotName(slot);

			if (kind == HD_BIND_LARA) { lara++; continue; }
			if (kind == HD_BIND_DUMMY) { dummy++; continue; }
			if (kind != HD_BIND_MODEL || !model)
			{
				std::printf("  slot %d has no name in this engine\n", slot);
				continue;
			}

			const std::string trm = item + "\\" + model + ".TRM";
			const bool there = exists(trm);
			bool altThere = false;

			if (!there && !alt.empty())
				altThere = exists(alt + "\\ITEM\\" + std::string(model) + ".TRM");

			if (there)
			{
				bound++;
				if (exists(item + "\\" + model + "_HEAD.TRM"))
					heads++;
			}
			else
			{
				miss++;
				if (altThere)
					recovered++;
				missing.push_back(std::string(slotName ? slotName : "?") + " -> " +
					model + (altThere ? ".TRM (but the 4 folder has it)" : ".TRM"));
			}

			if (verbose)
			{
				HDTrmInfo info;
				const char* why = "";
				const bool read = there && HDTrmProbe(trm.c_str(), &info, &why);
				std::printf("    %-4d %-26s -> %-30s %s%s%s\n",
					slot, slotName ? slotName : "?", model,
					there ? (read ? "ok" : "UNREADABLE: ") : "MISSING",
					(there && !read) ? why : "",
					(there && read && info.facialRig) ? " (facial rig)" : "");
			}
		}

		std::printf("%-28s %5d %5d %6d %6d %6d %6d %6d %6d\n",
			stem.c_str(), HDMapCount(), (int)slots.size(), bound, miss, dummy,
			lara, heads, recovered);

		for (size_t k = 0; k < missing.size(); ++k)
			std::printf("      missing: %s\n", missing[k].c_str());

		HDMapFree();
	}
	return 0;
}

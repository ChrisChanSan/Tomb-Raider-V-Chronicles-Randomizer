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
//   MapProbe ... -hdm                                  the .hdm sidecar of a
//                                                      randomized level, one
//                                                      decision per line
//   MapProbe ... -tr3 <path to the I-III game 3 folder>
//
// -map is what a randomized level needs: its file is called whatever the run
// called it, but the .MAP that belongs to it is the origin level's. Without
// it every slot binds by name alone and the missing count is meaningless.
//
// -hdm IS THE PROOF OF STAGE S7-2 without starting the game. The sidecar
// beside a randomized level says which rooms the run reshaped and which slots
// hold a model from another level file; this reads it with the ENGINE'S OWN
// reader (specific\hd\hd_markers.cpp is compiled in) and answers each line
// the way the engine will answer it - a room classic with the reason
// "marker", a donor slot through the DONOR'S .MAP, a Tomb Raider III guest
// through the four-line table and the other bundle. The one thing it cannot
// answer is the skeleton test, which needs the model on a card; that is
// ModelProbe's job and it says so per line.
//
// -tr3 names the game 3 folder of an installed Tomb Raider I-III Remastered,
// so that a guest's .TRM can be looked for. Without it a guest line says
// "I-III not given" and the engine's own answer for that case is classic.
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
#include "hd_lara.h"			// the outfit numbers of data[3]
#include "hd_markers.h"			// the .hdm reader the engine uses
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

	/* ---------------------------------------------------- the .hdm proof -- */

	/* Where a model is looked for, in the engine's own order: the level's
	 * bundle is game 5 then the sibling 4, the other bundle is game 3
	 * then 1 and 2 (specific\hd\hd_model.cpp, find_model). Returns the folder
	 * it was found in, or an empty string. */
	std::string where_model(const std::string& root, const std::string& alt,
		const std::string& tr3, const std::string& name, bool fromTr3)
	{
		if (fromTr3)
		{
			if (tr3.empty())
				return std::string();

			if (exists(tr3 + "\\ITEM\\" + name + ".TRM"))
				return tr3;

			return std::string();
		}

		if (exists(root + "\\ITEM\\" + name + ".TRM"))
			return root;

		if (!alt.empty() && exists(alt + "\\ITEM\\" + name + ".TRM"))
			return alt;

		return std::string();
	}

	/* One randomized level's sidecar, line by line, with the answer the
	 * engine will give. Everything printed here is a decision and not a
	 * description: a reader should be able to hold this table against the
	 * HDLOG of a real run and find the same words. */
	void report_hdm(const std::string& path, const std::string& stem,
		const Tr5Level& level, const std::string& root, const std::string& alt,
		const std::string& tr3)
	{
		std::printf("\n%s\n", stem.c_str());

		if (!HDMarkersLoad(path.c_str()))
		{
			std::printf("  no markers   %s  (the engine draws this level "
				"exactly as it did before stage S7-2)\n", HDMarkersError());
			return;
		}

		std::printf("  sidecar      %s\n", HDMarkersPath());
		std::printf("  version      %d, %d rooms, %d donors\n",
			HDMarkersVersion(), HDMarkersRoomCount(), HDMarkersDonorCount());

		/* THE OUTFIT. data[3] of the level header is what the engine reads;
		 * the sidecar repeats it for a person, and a disagreement means one
		 * of the two halves was written by an older run. */
		const int data3 = level.hdOutfit();
		const char* model = HDLaraOutfitModel(data3);

		std::printf("  outfit       data[3]=%d sidecar=%d -> %s%s\n",
			data3, HDMarkersOutfit(),
			(data3 >= HD_LARA_OUTFIT_TR3_ANTARC && data3 <= HD_LARA_OUTFIT_TR3_CATSUIT)
				? (model ? model : "?")
				: "the level's own LaraType decides",
			data3 != HDMarkersOutfit() ? "   MISMATCH - data[3] wins" : "");

		if (data3 >= HD_LARA_OUTFIT_TR3_ANTARC && data3 <= HD_LARA_OUTFIT_TR3_CATSUIT)
		{
			const std::string found = where_model(root, alt, tr3, model, false);

			std::printf("               %s.TRM %s\n", model,
				found.empty() ? "IS IN NO ITEM FOLDER - Lara stays classic"
					: ("found in " + found + "\\ITEM").c_str());
		}

		for (int i = 0; i < HDMarkersRoomCount(); i++)
			std::printf("  room %-6d classic, reason=marker (the run changed "
				"its shape; the Remaster's art is the old one)\n",
				HDMarkersRoom(i));

		for (int i = 0; i < HDMarkersDonorCount(); i++)
		{
			const HDMarkerDonor* donor = HDMarkersDonor(i);
			const bool guest = _stricmp(donor->level, "TR3") == 0;
			const char* slotName = HDSlotName(donor->slot);
			HDBindKind kind = HD_BIND_MODEL;
			bool fromMap = false;
			const char* name = 0;
			const char* how = "";

			if (guest)
			{
				name = HDBindTr3Guest(donor->slot);
				how = name ? "tr3_guest" : "tr3_slot_unknown";
			}
			else
			{
				name = HDBindDonorSlot(root.c_str(), donor->level,
					donor->fromSlot, &kind, &fromMap, 0);

				if (kind == HD_BIND_DUMMY) { name = 0; how = "donor_dummy"; }
				else if (kind != HD_BIND_MODEL || !name) { name = 0; how = "donor_no_name"; }
				else how = fromMap ? "donor_map" : "donor_slot_name";
			}

			std::string found;

			if (name)
				found = where_model(root, alt, tr3, name, guest);

			std::printf("  donor %-4d %-24s <- %s:%d  %-28s %s (%s)\n",
				donor->slot, slotName ? slotName : "?", donor->level,
				donor->fromSlot, name ? name : "-",
				!name ? "classic"
					: (found.empty()
						? (guest && tr3.empty() ? "classic - I-III not given"
							: "classic - no such .TRM")
						: "HD if the skeleton fits"),
				how);
		}

		HDMarkersFree();
		HDDonorMapsFree();
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
	std::string tr3;
	std::string mapStem;
	bool verbose = false;
	bool hdm = false;
	std::vector<std::string> targets;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-v"))
			verbose = true;
		else if (!_stricmp(argv[i], "-hdm"))
			hdm = true;
		else if (!_stricmp(argv[i], "-alt") && i + 1 < argc)
			alt = argv[++i];
		else if (!_stricmp(argv[i], "-tr3") && i + 1 < argc)
			tr3 = argv[++i];
		else if (!_stricmp(argv[i], "-map") && i + 1 < argc)
			mapStem = argv[++i];
		else if (!_stricmp(argv[i], "-all"))
			for (const char* name : kLevels)
				targets.push_back(root + "\\DATA\\" + name + ".TRC");
		else
			targets.push_back(argv[i]);
	}

	std::printf("install : %s\n", root.c_str());

	if (hdm)
	{
		std::printf("I-III   : %s\n",
			tr3.empty() ? "not given (-tr3) - every Tomb Raider III guest "
				"stays classic" : tr3.c_str());

		for (const std::string& path : targets)
		{
			Tr5Level level;
			std::string error;
			const std::string stem = stemOf(path);

			if (!level.load(toW(path), error))
			{
				std::printf("\n%s\n  not loadable: %s\n", stem.c_str(), error.c_str());
				continue;
			}

			/* The level's OWN .MAP, exactly as the engine loads it - a donor
			 * slot is only asked about the donor's map, and every other
			 * answer in the table comes out of this one. */
			HDMapLoad((root + "\\DATA\\" +
				(mapStem.empty() ? stem : mapStem) + ".MAP").c_str());

			report_hdm(path, stem, level, root, alt, tr3);

			HDMapFree();
		}

		return 0;
	}

	std::printf("\n");
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
			const char* model = HDBindSlot(root.c_str(), slot, &kind, &fromMap, 0);
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

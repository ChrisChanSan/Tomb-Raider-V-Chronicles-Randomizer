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

// MapProbe - which .TRM the install assigns to each of a level's slots.
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
// -hdm proves what the HD room-cut and donor-slot machinery decided, without
// starting the game. The sidecar beside a randomized level says which rooms
// the run reshaped, which openings it cut, which slots hold a model from
// another level file and which items the run added; this reads it with the
// engine's own reader (specific\hd\hd_markers.cpp is compiled in) and
// answers each line the way the engine will answer it - a room classic with
// the reason "marker", a cut box with the volume it takes out of every
// room's art, a donor slot through the donor's own .MAP, a Tomb Raider III
// guest through the four-line table and the other bundle, an added item
// through its slot's binding, and classic where the map calls the slot
// DUMMY, since the art carries such a slot's objects where Core placed them
// only. Two things it cannot answer: the skeleton test, which needs
// the model on a card (ModelProbe's job, and it says so per line), and how
// many triangles a cut box really catches, which needs the level built
// (RoomProbe -hdm, and the engine's own "HDLOG ... cut N" line).
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

/* hd_install.cpp reads the two registry overrides out of these, which the
 * engine defines in tomb5\tomb5.cpp. A tool that links hd_install.cpp - and
 * this one does, because hd_wardrobe.cpp is on bmap's build line and the
 * wardrobe asks the installer where the remasters are - has to bring its own
 * pair; ModelProbe, SfxProbe and TrgProbe do the same. Left empty, so the
 * search runs exactly as it does for a player who never set the registry
 * value; this tool is handed its folders on the command line. */
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
				"as it draws any level without markers)\n", HDMarkersError());
			return;
		}

		std::printf("  sidecar      %s\n", HDMarkersPath());
		std::printf("  version      %d, %d rooms, %d cut boxes, %d donors, "
			"%d added items\n", HDMarkersVersion(), HDMarkersRoomCount(),
			HDMarkersCutCount(), HDMarkersDonorCount(), HDMarkersItemCount());

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
			std::printf("  room %-6d classic, reason=marker (the run lifted "
				"or rebuilt it; nothing of the Remaster's art is kept)\n",
				HDMarkersRoom(i));

		/* A cut box takes no room away - it takes a box of triangles out of
		 * every room's art, and which rooms lose how much is a question only
		 * the build can answer (the engine's own log line
		 * "HDLOG level=... cut N ..." says it, and RoomProbe -hdm draws it).
		 * What this tool can say is the box, and whether it looks like one
		 * opening: exactly one of the three spans reads 1024, and that is
		 * the opening's normal (2 * 512 over a raw box that is flat there).
		 * The other two are the opening's own size less 2 * 128, the inward
		 * step that keeps the passage's own floor and ceiling out of the
		 * box. */
		for (int i = 0; i < HDMarkersCutCount(); i++)
		{
			const HDMarkerCut* cut = HDMarkersCut(i);

			if (!cut)
				continue;

			std::printf("  cut %-7d x %d..%d  y %d..%d  z %d..%d  "
				"(%d x %d x %d) - the HD art inside it is dropped, "
				"the rooms stay HD\n", i,
				cut->mins[0], cut->maxs[0], cut->mins[1], cut->maxs[1],
				cut->mins[2], cut->maxs[2],
				cut->maxs[0] - cut->mins[0], cut->maxs[1] - cut->mins[1],
				cut->maxs[2] - cut->mins[2]);
		}

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

		/* The items the run added, with the slot each holds in the level
		 * file. Such an item draws as its slot draws every item - HD where
		 * a model binds, classic where none does - and classic where the
		 * map calls the slot DUMMY, because the art carries that slot's
		 * objects where Core placed them only (hd_object.cpp,
		 * begin_decision; the game's own line is "HDLOG level=... added
		 * item=N"). A donor slot is never baked, so its line above
		 * answers for it. */
		for (int i = 0; i < HDMarkersItemCount(); i++)
		{
			const int index = HDMarkersItem(i);
			Tr5Item it;

			if (!level.getItem(index, it))
			{
				std::printf("  item %-5d not in the level file (%d items) - "
					"no item to draw\n", index, (int)level.itemCount());
				continue;
			}

			const int slot = it.objectNumber;
			const char* slotName = HDSlotName(slot);
			HDBindKind kind = HD_BIND_NONE;
			bool fromMap = false;
			const char* name = HDMarkersDonorOfSlot(slot) ? 0
				: HDBindSlot(root.c_str(), slot, &kind, &fromMap, 0);
			const char* answer;

			if (HDMarkersDonorOfSlot(slot))
				answer = "as the slot's donor line says";
			else if (kind == HD_BIND_LARA)
				answer = "Lara's composite";
			else if (kind == HD_BIND_DUMMY && fromMap)
				answer = "classic - the map calls the slot DUMMY and the art "
					"carries it where Core placed it only";
			else if (kind != HD_BIND_MODEL || !name)
				answer = "classic";
			else if (where_model(root, alt, tr3, name, false).empty())
				answer = "classic - no such .TRM";
			else
				answer = "HD if the skeleton fits";

			std::printf("  item %-5d %-24s slot %-4d room %-4d %-28s %s\n",
				index, slotName ? slotName : "?", slot, (int)it.roomNumber,
				kind == HD_BIND_MODEL && name ? name : "-", answer);
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

/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The .hdm grammar is this project's own (TR5Randomizer\src\core\ApplyPatch.h,
 * Patch::writeHdMarkers); the reference reader it was agreed against is
 * Preparation\Tools\Diagnose\HdmDump.cpp.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_markers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	struct Donor
	{
		int slot;
		int fromSlot;
		std::string level;
	};

	bool g_present = false;
	int g_version = 0;
	int g_outfit = 0;
	std::string g_error;
	std::string g_path;
	std::vector<int> g_rooms;
	std::vector<HDMarkerCut> g_cuts;
	std::vector<Donor> g_donors;
	std::vector<std::string> g_unknown;
	// The sound ids this run imported from another game.
	std::vector<int> g_sfx_imports;
	// The items this run added, by index - sorted once the file is read.
	std::vector<int> g_items;

	// Exported through HDMarkersDonor as a plain struct, so the strings the
	// caller sees stay owned by this file and alive until HDMarkersFree.
	std::vector<HDMarkerDonor> g_donor_view;

	void rebuild_view()
	{
		g_donor_view.clear();
		g_donor_view.reserve(g_donors.size());

		for (size_t i = 0; i < g_donors.size(); i++)
		{
			HDMarkerDonor one;

			one.slot = g_donors[i].slot;
			one.fromSlot = g_donors[i].fromSlot;
			one.level = g_donors[i].level.c_str();
			g_donor_view.push_back(one);
		}
	}

	/* "DATA\ANDREA1.TRC" -> "DATA\ANDREA1.hdm". The extension is cut only
	 * when the dot belongs to the file name and not to a folder above it -
	 * the same rule the randomizer's writer uses, and the reason a path like
	 * "C:\Games\Tomb 5.0\data\rich1" cannot lose its folder. */
	std::string sidecar_of(const char* levelPath)
	{
		std::string out = levelPath ? levelPath : "";
		const size_t slash = out.find_last_of("/\\");
		const size_t dot = out.find_last_of('.');

		if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
			out.erase(dot);

		return out + ".hdm";
	}
}

bool HDMarkersLoad(const char* levelPath)
{
	HDMarkersFree();

	g_path = sidecar_of(levelPath);

	std::FILE* file = std::fopen(g_path.c_str(), "rb");

	if (!file)
	{
		/* Not an error. A vanilla installation has no sidecar, and so has a
		 * folder the randomizer put back with "restore original". */
		g_error = "no sidecar file";
		return false;
	}

	char line[512];

	while (std::fgets(line, sizeof(line), file))
	{
		/* Tolerate \r. The file is written with plain \n, but a user may have
		 * opened it in an editor and saved it back. */
		size_t length = std::strlen(line);

		while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
			line[--length] = 0;

		if (!length)
			continue;

		int a = 0, b = 0;
		char word[64] = { 0 };

		if (std::sscanf(line, "hdm %d", &a) == 1)
		{
			g_version = a;
			continue;
		}

		if (std::sscanf(line, "outfit %d", &a) == 1)
		{
			g_outfit = a;
			continue;
		}

		if (std::sscanf(line, "room %d", &a) == 1)
		{
			g_rooms.push_back(a);
			continue;
		}

		/* Six integers, ascending corners already - but sorted
		 * here all the same, because a box the wrong way round would match
		 * no triangle at all and would do it silently, and the one thing
		 * this reader must never do is turn a marker into nothing. */
		{
			int box[6] = { 0, 0, 0, 0, 0, 0 };

			if (std::sscanf(line, "cut %d %d %d %d %d %d",
				&box[0], &box[1], &box[2], &box[3], &box[4], &box[5]) == 6)
			{
				HDMarkerCut cut;

				for (int axis = 0; axis < 3; axis++)
				{
					cut.mins[axis] = box[axis] < box[axis + 3]
						? box[axis] : box[axis + 3];
					cut.maxs[axis] = box[axis] < box[axis + 3]
						? box[axis + 3] : box[axis];
				}

				g_cuts.push_back(cut);
				continue;
			}
		}

		/* The sound ids this run imported, all on one line because they belong
		 * to one fact - "these numbers are not the remaster's sounds" - and a
		 * line per id would say the same thing twenty-one times in a run with
		 * all four guests. The count is not written down anywhere: the line is
		 * read until it stops holding numbers, which lets a future import add
		 * a fifth guest without this reader knowing about it. */
		if (!std::strncmp(line, "sfximport ", 10))
		{
			const char* at = line + 10;

			while (*at)
			{
				char* end = 0;
				const long id = std::strtol(at, &end, 10);

				if (end == at)
					break;

				if (id >= 0 && id < 0x7FFF)
					g_sfx_imports.push_back((int)id);

				at = end;

				while (*at == ' ' || *at == '\t')
					at++;
			}

			continue;
		}

		if (std::sscanf(line, "donor %d %63s %d", &a, word, &b) == 3)
		{
			Donor one;

			one.slot = a;
			one.level = word;
			one.fromSlot = b;
			g_donors.push_back(one);
			continue;
		}

		/* An item the run added. A negative index names no item and is not
		 * kept; one past the level's last item is kept and never asked. */
		if (std::sscanf(line, "item %d", &a) == 1)
		{
			if (a >= 0)
				g_items.push_back(a);

			continue;
		}

		g_unknown.push_back(line);
	}

	std::fclose(file);

	/* Sorted and each index once, whatever order the file had them in, so
	 * the draw path's question is a binary search. */
	std::sort(g_items.begin(), g_items.end());
	g_items.erase(std::unique(g_items.begin(), g_items.end()), g_items.end());

	/* The version is a refusal and not a warning. An unknown line can be
	 * ignored safely - it says something this engine does not act on - but an
	 * unknown VERSION may have changed the meaning of the lines that are
	 * there, and acting on a room number that no longer means a room number
	 * would draw the wrong half of the level classic. Refusing the file falls
	 * back to "no markers", which is exactly what an engine older than the
	 * run should do.
	 *
	 * Two versions pass, and both are read the same way: 2 only adds the
	 * `cut` line, it changes the meaning of nothing that was there before, so
	 * a version 1 file is a version 2 file with no cut in it. Sidecars
	 * written before the `cut` line existed therefore go on working exactly
	 * as they did. */
	if (g_version != 1 && g_version != 2)
	{
		char why[96];

		std::snprintf(why, sizeof(why),
			"version %d is neither the 1 nor the 2 this engine knows",
			g_version);
		g_error = why;
		g_rooms.clear();
		g_cuts.clear();
		g_donors.clear();
		g_unknown.clear();
		g_sfx_imports.clear();
		g_items.clear();
		g_outfit = 0;
		return false;
	}

	rebuild_view();
	g_present = true;
	return true;
}

void HDMarkersFree()
{
	g_present = false;
	g_version = 0;
	g_outfit = 0;
	g_error.clear();
	g_path.clear();
	g_rooms.clear();
	g_cuts.clear();
	g_donors.clear();
	g_donor_view.clear();
	g_unknown.clear();
	g_sfx_imports.clear();
	g_items.clear();
}

bool HDMarkersPresent()
{
	return g_present;
}

int HDMarkersVersion()
{
	return g_version;
}

const char* HDMarkersError()
{
	return g_error.c_str();
}

const char* HDMarkersPath()
{
	return g_path.c_str();
}

int HDMarkersOutfit()
{
	return g_outfit;
}

bool HDMarkersRoomTouched(int room)
{
	if (!g_present)
		return false;

	for (size_t i = 0; i < g_rooms.size(); i++)
		if (g_rooms[i] == room)
			return true;

	return false;
}

int HDMarkersRoomCount()
{
	return (int)g_rooms.size();
}

int HDMarkersRoom(int at)
{
	if (at < 0 || at >= (int)g_rooms.size())
		return -1;

	return g_rooms[at];
}

int HDMarkersCutCount()
{
	return g_present ? (int)g_cuts.size() : 0;
}

const HDMarkerCut* HDMarkersCut(int at)
{
	if (!g_present || at < 0 || at >= (int)g_cuts.size())
		return 0;

	return &g_cuts[(size_t)at];
}

int HDMarkersDonorCount()
{
	return (int)g_donor_view.size();
}

const HDMarkerDonor* HDMarkersDonor(int at)
{
	if (at < 0 || at >= (int)g_donor_view.size())
		return 0;

	return &g_donor_view[at];
}

const HDMarkerDonor* HDMarkersDonorOfSlot(int slot)
{
	if (!g_present)
		return 0;

	for (size_t i = 0; i < g_donor_view.size(); i++)
		if (g_donor_view[i].slot == slot)
			return &g_donor_view[i];

	return 0;
}

int HDMarkersItemCount()
{
	return g_present ? (int)g_items.size() : 0;
}

int HDMarkersItem(int at)
{
	if (!g_present || at < 0 || at >= (int)g_items.size())
		return -1;

	return g_items[(size_t)at];
}

bool HDMarkersItemAdded(int item)
{
	return g_present && item >= 0 &&
		std::binary_search(g_items.begin(), g_items.end(), item);
}

int HDMarkersSoundImportCount()
{
	return (int)g_sfx_imports.size();
}

int HDMarkersSoundImport(int at)
{
	if (at < 0 || at >= (int)g_sfx_imports.size())
		return -1;

	return g_sfx_imports[at];
}

bool HDMarkersSoundImported(int soundId)
{
	for (size_t i = 0; i < g_sfx_imports.size(); i++)
		if (g_sfx_imports[i] == soundId)
			return true;

	return false;
}

int HDMarkersUnknownCount()
{
	return (int)g_unknown.size();
}

const char* HDMarkersUnknown(int at)
{
	if (at < 0 || at >= (int)g_unknown.size())
		return "";

	return g_unknown[at].c_str();
}

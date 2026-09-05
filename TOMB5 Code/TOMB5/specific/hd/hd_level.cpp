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
#include "../../tomb5/pch.h"
#include "hd_level.h"
#include "hd_install.h"
#include "hd_bind.h"
#include "hd_scan.h"
#include "../function_stubs.h"
#include "../../tomb5/tomb5.h"
#include "../../game/objects.h"
#include "../../game/control.h"		// number_rooms

// The generated slot name table in hd_bind.cpp has to be the enum, entry for
// entry. This is the line that notices when a slot was added to objects.h and
// not to that table - at build time, not in a picture.
static_assert(HD_SLOT_COUNT == NUMBER_OBJECTS,
	"hd_bind.cpp's slot name table and enum object_types have drifted apart - "
	"regenerate the table (see the comment above it) and update HD_SLOT_COUNT");
static_assert(HAIR == 30,
	"HDSlotIsLara() covers slots 0..30 because HAIR is 30 - it no longer is");

bool HDEnabled()
{
	// The guard. With hd_enable off nothing below this line ever runs, no
	// Remaster is looked for, no file is opened, and the frame is what it was
	// before #123 existed - byte for byte.
	if (!tomb5.hd_enable)
		return false;

	return HDInstallRoot()[0] != 0;
}

// "DATA\ANDREA1.TRC" -> "ANDREA1". Not HDScreenLevelName(), which maps
// RICHCUT2 to RICH3 because the Remaster ships no loading picture for the
// cutscene - the .TRG of RICHCUT2 exists and is its own.
static void HDLevelStem(const char* wadName, char* out, int size)
{
	const char* start;
	const char* dot;
	int length;

	out[0] = 0;

	if (!wadName || !*wadName)
		return;

	start = strrchr(wadName, '\\');
	start = start ? start + 1 : wadName;

	if (const char* slash = strrchr(start, '/'))
		start = slash + 1;

	dot = strrchr(start, '.');
	length = dot ? (int)(dot - start) : (int)strlen(start);

	if (length > size - 1)
		length = size - 1;

	for (int i = 0; i < length; i++)
		out[i] = (char)toupper((uchar)start[i]);

	out[length] = 0;
}

void HDLevelLoad(const char* wadName)
{
	static unsigned char loaded[NUMBER_OBJECTS];
	HDScanStats stats;
	const char* why;
	char stem[64];

	if (!HDEnabled())
		return;

	HDLevelStem(wadName, stem, sizeof(stem));

	if (!stem[0])
		return;

	for (int i = 0; i < NUMBER_OBJECTS; i++)
		loaded[i] = objects[i].loaded ? 1 : 0;

	// The whole page decode is the measurement stage S0 exists for, and it
	// costs seconds. It is tied to -log because the line it feeds only exists
	// with -log: a normal run with hd_enable on reads the .TRG and the .MAP
	// and nothing else.
	if (!HDScanLevel(HDInstallRoot(), stem, number_rooms, loaded, NUMBER_OBJECTS,
		gLogEnabled, 1, &stats, &why))
	{
		Log("HDLOG level=%s FAILED %s", stem, why);
		return;
	}

	Log("HDLOG level=%s trg=v%d groups=%d rooms=%d sky=%d empty=%d pages=%d "
		"verts=%d idx=%d ranges=%d changes=%d mixed=%d map=%d slots=%d "
		"trm_bound=%d trm_missing=%d trm_dummy=%d trm_lara=%d heads=%d "
		"bc7_ms=%d bc7_pages=%d bc7_mips=%d bc7_texels=%d",
		stem, stats.trgVersion, stats.groups, stats.rooms, stats.skyGroups,
		stats.emptyGroups, stats.pages, stats.vertices, stats.indices,
		stats.ranges, stats.pageChanges, stats.mixedPageTriangles,
		stats.mapEntries, stats.slotsLoaded, stats.trmBound, stats.trmMissing,
		stats.trmDummy, stats.trmLara, stats.headsBound,
		(int)(stats.bc7Ms + 0.5), stats.bc7Pages, stats.bc7Mips,
		(int)stats.bc7Texels);

	for (int i = 0; i < HDScanMissingCount(); i++)
		Log("HDLOG level=%s missing slot=%s model=%s.TRM",
			stem, HDScanMissingSlot(i), HDScanMissingModel(i));

	if (stats.groups < stats.rooms)
		Log("HDLOG level=%s WARNING the .TRG has %d groups for %d rooms - "
			"the rooms above %d stay classic",
			stem, stats.groups, stats.rooms, stats.groups - 1);
}

void HDLevelUnload()
{
	// Stage S0 holds nothing between levels: HDLevelLoad frees its .TRG, its
	// map and every decoded page before it returns. The call sits in
	// FreeLevel from the start so that stage S1 has one place to put the
	// vertex buffers, the page textures and the room table, and so that the
	// seam is proven by the level bracket before anything hangs on it.
	if (!HDEnabled())
		return;

	Log("HDLOG unload");
}

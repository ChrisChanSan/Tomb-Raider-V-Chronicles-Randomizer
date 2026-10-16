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
#pragma once

/*
 * The identity of a level's HD build. A savegame loaded inside a level reads
 * the classic level again, and the HD data made from it - the room art, its
 * pages, the models - come out the same byte for byte as long as nothing they
 * are made of has changed: the level's three files, the two text files the
 * build reads, the settings that shape it and the outfit. The key names all
 * of them; two equal keys mean the build of the one is the build of the
 * other, and the load keeps the HD data (hd_level.cpp, HDLevelUnload)
 * instead of releasing them and building them again.
 *
 * Pure, without an engine header, so that ModernTest checks the engine's own
 * comparison; hd_level.cpp fills the key and holds the state.
 */

#include <string.h>

// One file as the key sees it. Both fields are -1 when the file is not there.
struct HDLevelFileStamp
{
	long long size;		// bytes
	long long written;	// last write time, FILETIME (100 ns since 1601)
};

#define HD_LEVEL_KEY_PATH	260	// MAX_PATH
#define HD_LEVEL_KEY_OUTFIT	192	// HD_WARDROBE_KEY

struct HDLevelKey
{
	int valid;			// 0: no key - no HD build, or no level name
	int level;			// gfLevelId, the level's identity
	char name[32];			// the level file's name without .TRC
	HDLevelFileStamp trc;		// the classic level, as LoadLevel opens it
	HDLevelFileStamp hdm;		// its sidecar beside it
	HDLevelFileStamp trg;		// the Remaster's art of the level
	HDLevelFileStamp glossFile;	// hd_gloss.txt, read by the room build
	HDLevelFileStamp waterFile;	// hd_water.txt, read by the water sheets
	char install[HD_LEVEL_KEY_PATH];	// the Remaster's game 5 folder
	char installTr3[HD_LEVEL_KEY_PATH];	// the I-III bundle's game 3, or ""
	int air;			// tomb5.hd_air
	int water;			// tomb5.hd_water
	int objects;			// tomb5.hd_objects
	long budgetMb;			// tomb5.hd_texture_budget_mb
	char format[16];		// tomb5_hd_texture_format
	char outfit[HD_LEVEL_KEY_OUTFIT];	// the registry's hd_outfit, or ""
	/* tomb5.hd_marble: the marble floors' faces and opaque clear floors are
	 * the build's (hd_render.cpp, "the marble floors"). */
	int marble;
};

inline int HDLevelStampSame(const HDLevelFileStamp* a,
	const HDLevelFileStamp* b)
{
	return a->size == b->size && a->written == b->written;
}

/* The first field in which two keys differ, as the log names it, or 0 when
 * they name the same build. A key that is not valid differs from every key,
 * itself included: without a level there is nothing to keep. */
inline const char* HDLevelKeyDiffers(const HDLevelKey* a, const HDLevelKey* b)
{
	if (!a->valid || !b->valid)
		return "no_key";

	if (a->level != b->level)
		return "level";

	if (strcmp(a->name, b->name))
		return "name";

	if (!HDLevelStampSame(&a->trc, &b->trc))
		return "trc";

	if (!HDLevelStampSame(&a->hdm, &b->hdm))
		return "hdm";

	if (!HDLevelStampSame(&a->trg, &b->trg))
		return "trg";

	if (!HDLevelStampSame(&a->glossFile, &b->glossFile))
		return "gloss_file";

	if (!HDLevelStampSame(&a->waterFile, &b->waterFile))
		return "water_file";

	if (strcmp(a->install, b->install))
		return "install";

	if (strcmp(a->installTr3, b->installTr3))
		return "install_tr3";

	if (a->air != b->air)
		return "hd_air";

	if (a->water != b->water)
		return "hd_water";

	if (a->objects != b->objects)
		return "hd_objects";

	if (a->budgetMb != b->budgetMb)
		return "hd_texture_budget_mb";

	if (strcmp(a->format, b->format))
		return "hd_texture_format";

	if (strcmp(a->outfit, b->outfit))
		return "hd_outfit";

	if (a->marble != b->marble)
		return "hd_marble";

	return 0;
}

/* Whether a load keeps the HD data that are there, and if not why: 0 keeps
 * them. `built` is the key of the data in memory (not valid when none were
 * built, or when they were released or lost with the device), `next` the
 * key of the level about to be read, `redressed` the outfit changes made in
 * the level since its build - each one appends pages a fresh build would not
 * have, and the allowance that bounds them starts again at every build, so a
 * level that was re-dressed is built again. */
inline const char* HDLevelKeepRefused(const HDLevelKey* built,
	const HDLevelKey* next, int redressed)
{
	if (!next->valid)
		return "no_key";

	if (!built->valid)
		return "nothing_built";

	if (redressed)
		return "redressed";

	return HDLevelKeyDiffers(built, next);
}

// The engine's half, in hd_level.cpp.

/* The key of the level LoadLevel is about to read (specific\file.cpp, on the
 * load thread, before FreeLevel), for HDLevelUnload's decision. With the
 * build gate off (HDEnabled: hd_build 0 or no installation) it stores no
 * key and reads no file. The draw - the HD textures row, hd_enable - is no
 * part of the key: the data are built whatever is drawn, so a step of the
 * row or a press of the HD/SD switch never makes the next load of the same
 * level build them again. */
void HDLevelKeyNext(const char* wadName);

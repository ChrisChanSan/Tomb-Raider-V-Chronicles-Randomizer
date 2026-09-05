/*
 * Modified 2026 by ChrisChanSan.
 *
 * Carried over from TROne (Copyright 2026 Todesengel, GPLv3) - see hd_lara.h
 * for the list of functions and THIRD-PARTY.md for the licence.
 *
 * WHAT IS NOT TROne'S, and must not be made to follow it: which outfit a
 * section wears. TROne's TR5 glue (games\tr5\glue\hostglue_file5.cpp,
 * RegisterLaraWardrobe) gives OUTFIT_RUSSIA to the levels whose file names
 * begin with ANDY and treats the JOBY ones as young Lara. In this game it is
 * the other way round and the level files say so: the JOBY files are the
 * Russian submarine section (gfLevelId LVL5_BASE .. LVL5_SINKING_SUBMARINE,
 * which is what game\hair.cpp's own braid test asks about) and the ANDY files
 * are Ireland, where game\laramisc.cpp swaps in the child torso. Measured
 * over all fifteen level files with ModelProbe -lara. HDLaraOutfitFor below
 * therefore asks the engine's own questions and never a file name.
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
#include "hd_lara.h"
#include "hd_bind.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	/* ------------------------------------------------------- the wardrobe -- */

	/* THE OUTFITS. Every one of these files was read and counted before it was
	 * written down here (ModelProbe -name <NAME> -nodraw); the node column is
	 * the measurement, not the plan.
	 *
	 *   model              nodes with geometry                     vertices
	 *   OUTFIT_CLASSIC     0..14                                     27349
	 *   OUTFIT_RUSSIA      0..14                                     22647
	 *   OUTFIT_SUBSUIT     0,1,2,4,5,7..14  - no feet                15631
	 *   OUTFIT_VCI         0..14                                     22306
	 *   OUTFIT_TR3_ANTARC  0..14                                     19348
	 *   OUTFIT_TR3_NEVADA  0..14                                     15498
	 *   OUTFIT_TR3_COAST   0..14                                     15327
	 *   OUTFIT_TR3_CATSUIT 0..14                                     18633
	 *
	 * `hands`, `braid` and `holsters` say whether the outfit takes the rest of
	 * the wardrobe. The diving suit takes none of it: its own nodes 10 and 13
	 * carry 1352 vertices each - the gauntlets ARE the suit - it wears a
	 * helmet instead of a braid, and DrawLara__5 draws no holsters at all. */
	struct Outfit
	{
		const char* model;
		const char* label;
		bool hands;
		bool braid;
		bool holsters;
	};

	const Outfit kOutfit[HD_LARA_OUTFIT_COUNT] =
	{
		{ 0,                    "classic (no HD body)",   false, false, false },
		{ "OUTFIT_CLASSIC",     "the Rome outfit",        true,  true,  true  },
		{ "OUTFIT_RUSSIA",      "the Russia outfit",      true,  true,  true  },
		{ "OUTFIT_SUBSUIT",     "the diving suit",        false, false, false },
		{ "OUTFIT_VCI",         "the VCI catsuit",        true,  true,  true  },
		{ "OUTFIT_TR3_ANTARC",  "Antarctica (TR3)",       true,  true,  true  },
		{ "OUTFIT_TR3_NEVADA",  "Nevada (TR3)",           true,  true,  true  },
		{ "OUTFIT_TR3_COAST",   "Jungle (TR3)",           true,  true,  true  },
		{ "OUTFIT_TR3_CATSUIT", "London (TR3)",           true,  true,  true  }
	};

	/* THE OTHER PARTS. `kind` decides which outfits take the part at all:
	 *   0 always, 1 only an outfit with hands, 2 only one with a braid,
	 *   3 only one with holsters.
	 * `rigid` >= 0 means the file is not skinned to the mesh tree but nailed
	 * to that one node - LARA_HEAD is a facial rig whose bones run to 31 and
	 * mean nothing at all to Lara's 15 node tree.
	 * `nodes` is the coverage measured over 4\ITEM, written down so that the
	 * masks in HDLaraFrameParts can be read against it. */
	struct Part
	{
		const char* model;
		const char* label;
		int kind;
		int rigid;
		const char* nodes;
	};

	const Part kPart[HD_LARA_PART_COUNT] =
	{
		{ 0,                          "the body",                 0, -1, "0..14" },
		{ "LARA_HEAD",                "the face",                 0, HD_LARA_HEAD_NODE, "14 (rigid)" },
		{ "HAIR_OG",                  "the braid",                2, -1, "0..5 of its own chain" },

		{ "HAND_BARE_REST",           "empty hands",              1, -1, "8,9,10,11,12,13" },
		{ "HAND_BARE_PISTOL",         "the pistols",              1, -1, "9,10,12,13" },
		{ "HAND_BARE_UZI",            "the uzis",                 1, -1, "9,10,12,13" },
		{ "HAND_BARE_SHOTGUN",        "the shotgun",              1, -1, "9,10,12,13" },
		{ "HAND_BARE_HK",             "the HK",                   1, -1, "9,10,12,13" },
		{ "HAND_BARE_REVOLVER",       "the revolver",             1, -1, "9,10,12,13" },
		{ "HAND_BARE_REVOLVER_LASER", "the revolver with sight",  1, -1, "9,10,12,13" },
		{ "HAND_BARE_GRAPPLE",        "the grappling gun",        1, -1, "9,10,12,13" },
		{ "HAND_BARE_CROSSBOW_LASER", "the grappling gun, sight", 1, -1, "9,10,12,13" },
		{ "HAND_BARE_FLARE",          "the flare",                1, -1, "12,13" },
		{ "HAND_BARE_TORCH",          "the torch",                1, -1, "12,13" },
		{ "HAND_BARE_CROWBAR",        "the crowbar",              1, -1, "9,10" },

		{ "HOLSTER_PISTOL",           "the pistol holsters",      3, -1, "1,4" },
		{ "HOLSTER_UZI",              "the uzi holsters",         3, -1, "1,4" },
		{ "HOLSTER_REVOLVER",         "the revolver holster",     3, -1, "4" },

		{ "HOLSTER_SHOTGUN",          "the shotgun on the back",  3, -1, "7" },
		{ "HOLSTER_HK",               "the HK on the back",       3, -1, "7" },
		{ "HOLSTER_GRAPPLE",          "the grapple on the back",  3, -1, "7" }
	};

	/* WHICH CLASSIC SLOT STANDS FOR WHICH PART. By NAME, out of hd_bind's
	 * table, because a slot is a hand slot for being called PISTOLS_ANIM and
	 * not for being number 1 - and because the tools have no engine enum.
	 *
	 * LARA_HOLSTERS is in here with no part on purpose. It is the EMPTY
	 * holster - the game switches to it the moment a gun is drawn - and the
	 * remaster ships no "empty holster" model, because the straps are painted
	 * into every OUTFIT_ already. Drawing nothing there is therefore not a
	 * gap; it is the picture. (TROne reaches the same result by registering
	 * the slot as SUPPRESS with an empty model name.)
	 *
	 * TR5's CROSSBOW_ANIM is the GRAPPLING GUN, not a crossbow - the slot
	 * kept its Tomb Raider IV name and the weapon changed. HAND_BARE_GRAPPLE
	 * and HOLSTER_GRAPPLE are the models the remaster draws there, which is
	 * also what TROne's TR5 glue picks. The laser-sight variant is the one
	 * loose end: the wardrobe has HAND_BARE_CROSSBOW_LASER and no grapple
	 * equivalent, so the slot LARA_CROSSBOW_LASER takes the crossbow's - a
	 * picture the user has to judge, and the note says so. */
	struct SlotRole
	{
		const char* slot;
		int role;
		int part;
	};

	const SlotRole kSlotRole[] =
	{
		{ "LARA",                 HD_LARA_ROLE_BODY,     HD_LARA_PART_HAND_REST },
		{ "LARA_SKIN",            HD_LARA_ROLE_BODY,     HD_LARA_PART_HAND_REST },
		{ "PISTOLS_ANIM",         HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_PISTOL },
		{ "UZI_ANIM",             HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_UZI },
		{ "SHOTGUN_ANIM",         HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_SHOTGUN },
		{ "HK_ANIM",              HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_HK },
		{ "REVOLVER_ANIM",        HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_REVOLVER },
		{ "LARA_REVOLVER_LASER",  HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_REVOLVER_LASER },
		{ "CROSSBOW_ANIM",        HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_GRAPPLE },
		{ "LARA_CROSSBOW_LASER",  HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_GRAPPLE_LASER },
		{ "FLARE_ANIM",           HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_FLARE },
		{ "TORCH_ANIM",           HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_TORCH },
		{ "CROWBAR_ANIM",         HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_CROWBAR },
		{ "LARA_HOLSTERS",        HD_LARA_ROLE_HOLSTER,  -1 },
		{ "LARA_HOLSTERS_PISTOLS", HD_LARA_ROLE_HOLSTER, HD_LARA_PART_HOLSTER_PISTOL },
		{ "LARA_HOLSTERS_UZIS",   HD_LARA_ROLE_HOLSTER,  HD_LARA_PART_HOLSTER_UZI },
		{ "LARA_HOLSTERS_REVOLVER", HD_LARA_ROLE_HOLSTER, HD_LARA_PART_HOLSTER_REVOLVER },
		{ "HAIR",                 HD_LARA_ROLE_HAIR,     HD_LARA_PART_BRAID },
		{ "LARA_SKIN_JOINTS",     HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_SCREAM",          HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_EXTRA_MESH1",     HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_EXTRA_MESH2",     HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_EXTRA_MESH3",     HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_SPEECH_HEAD1",    HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_SPEECH_HEAD2",    HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_SPEECH_HEAD3",    HD_LARA_ROLE_SUPPRESS, -1 },
		{ "LARA_SPEECH_HEAD4",    HD_LARA_ROLE_SUPPRESS, -1 }
	};

	/* THE WEAPON ON THE BACK is the same slot seen a second time. lara.back_gun
	 * holds SHOTGUN_ANIM, HK_ANIM or CROSSBOW_ANIM and DrawLara draws mesh 14
	 * of that slot on the torso; the wardrobe answers with a HOLSTER_ model on
	 * node 7. */
	const SlotRole kBackRole[] =
	{
		{ "SHOTGUN_ANIM",  HD_LARA_ROLE_BACK, HD_LARA_PART_BACK_SHOTGUN },
		{ "HK_ANIM",       HD_LARA_ROLE_BACK, HD_LARA_PART_BACK_HK },
		{ "CROSSBOW_ANIM", HD_LARA_ROLE_BACK, HD_LARA_PART_BACK_GRAPPLE }
	};

	/* ------------------------------------------------------------- state -- */

	int g_outfit = HD_LARA_OUTFIT_NONE;
	bool g_active = false;
	unsigned int g_coverage[HD_LARA_PART_COUNT] = { 0 };
	bool g_has[HD_LARA_PART_COUNT] = { false };
	bool g_planned[HD_LARA_PART_COUNT] = { false };
	std::vector<std::string> g_notes;

	void note(const char* format, ...)
	{
		char line[512];
		va_list args;

		va_start(args, format);
		std::vsnprintf(line, sizeof(line), format, args);
		va_end(args);

		g_notes.push_back(line);
	}

	bool same(const char* a, const char* b)
	{
		if (!a || !b)
			return false;

		for (; *a && *b; a++, b++)
		{
			const char ca = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
			const char cb = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;

			if (ca != cb)
				return false;
		}

		return !*a && !*b;
	}

	/* WHICH PARTS THIS LEVEL CAN ASK FOR. The body, the face, the braid and
	 * the rest hands always; a weapon or holster part only when the level
	 * really loaded the slot it hangs off - see HDLaraLevel::slots for why
	 * that is safe. Without a slot list everything the outfit takes is
	 * planned, which is what a diagnostic run with no level wants.
	 *
	 * Declared before its two callers so both plan the identical set - a
	 * build that counted a part the plan never asked for would report it
	 * missing every time. */
	void plan_mask(const HDLaraLevel* level, bool* useful)
	{
		const bool all = !level->slots || level->slotCount <= 0;

		for (int i = 0; i < HD_LARA_PART_COUNT; i++)
			useful[i] = all;

		useful[HD_LARA_PART_BODY] = true;
		useful[HD_LARA_PART_FACE] = true;
		useful[HD_LARA_PART_BRAID] = true;
		useful[HD_LARA_PART_HAND_REST] = true;	// LARA is always loaded

		for (int i = 0; !all && i < level->slotCount; i++)
		{
			static const int kRole[3] =
				{ HD_LARA_ROLE_HAND, HD_LARA_ROLE_HOLSTER, HD_LARA_ROLE_BACK };

			for (int r = 0; r < 3; r++)
			{
				const int part = HDLaraPartForSlot(level->slots[i], kRole[r]);

				if (part >= 0 && part < HD_LARA_PART_COUNT)
					useful[part] = true;
			}
		}
	}

	bool outfit_takes(int outfit, int part)
	{
		if (outfit <= HD_LARA_OUTFIT_NONE || outfit >= HD_LARA_OUTFIT_COUNT)
			return false;

		if (part < 0 || part >= HD_LARA_PART_COUNT)
			return false;

		if (part == HD_LARA_PART_BODY)
			return true;

		switch (kPart[part].kind)
		{
		case 1: return kOutfit[outfit].hands;
		case 2: return kOutfit[outfit].braid;
		case 3: return kOutfit[outfit].holsters;
		default: return true;
		}
	}
}

/* ------------------------------------------------------------- outfits -- */

const char* HDLaraOutfitModel(int outfit)
{
	if (outfit <= HD_LARA_OUTFIT_NONE || outfit >= HD_LARA_OUTFIT_COUNT)
		return 0;

	return kOutfit[outfit].model;
}

const char* HDLaraOutfitLabel(int outfit)
{
	if (outfit < 0 || outfit >= HD_LARA_OUTFIT_COUNT)
		return "?";

	return kOutfit[outfit].label;
}

int HDLaraOutfitFromName(const char* name)
{
	if (!name || !*name)
		return HD_LARA_OUTFIT_NONE;

	/* The randomizer's own guest ids, so that a wardrobe roll can be named
	 * with the word the randomizer wrote into its report (#122,
	 * TR5Randomizer\src\core\Tr3Source.cpp: outfits()).
	 *
	 * The two that are not word-for-word: the randomizer's "tr3_london" is
	 * ROOFS.TR2, Lara's standard London outfit of that game, and the remaster
	 * calls it OUTFIT_TR3_CATSUIT; "tr3_jungle" is CRASH.TR2, the South
	 * Pacific outfit, which the remaster calls OUTFIT_TR3_COAST. Both are a
	 * measurement by picture and the proof is in the report. */
	static const struct { const char* id; int outfit; } kGuest[] =
	{
		{ "tr3_london",     HD_LARA_OUTFIT_TR3_CATSUIT },
		{ "tr3_jungle",     HD_LARA_OUTFIT_TR3_COAST },
		{ "tr3_antarctica", HD_LARA_OUTFIT_TR3_ANTARC },
		{ "tr3_nevada",     HD_LARA_OUTFIT_TR3_NEVADA }
	};

	for (int i = 0; i < (int)(sizeof(kGuest) / sizeof(kGuest[0])); i++)
		if (same(name, kGuest[i].id))
			return kGuest[i].outfit;

	for (int i = HD_LARA_OUTFIT_NONE + 1; i < HD_LARA_OUTFIT_COUNT; i++)
	{
		const char* model = kOutfit[i].model;

		if (same(name, model))
			return i;

		// The bare tail, so "-outfit VCI" and "-outfit TR3_COAST" work.
		if (model && same(name, model + 7))
			return i;
	}

	return HD_LARA_OUTFIT_NONE;
}

int HDLaraOutfitFor(int laraDrawType, bool russiaCap, bool youngLara,
	int tr3Outfit, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;

	/* THE ORDER MATTERS, and it is the order of certainty.
	 *
	 * 1. Young Lara first, and before anything else can claim her. The user
	 *    decided she stays classic (plan section 9), and she is recognised
	 *    three ways over - the script flag, the Irish level range and the
	 *    LaraType byte - because the randomizer moves outfits between levels
	 *    and any one of the three alone has a hole in it.
	 * 2. A Tomb Raider III guest, because the randomizer put it there and the
	 *    level's own byte says nothing about it.
	 * 3. The diving suit and the catsuit, which have a LaraType of their own
	 *    and their own draw path in the engine (DrawLara__5 / __4).
	 * 4. The cap, by the engine's own test - see the header.
	 * 5. Everything else is the Rome outfit. That includes LARA_PONYTAIL,
	 *    which the randomizer writes for "the standard body in a Russia
	 *    level" precisely so that it is NOT read as the cap. */
	if (youngLara)
	{
		*why = "young Lara stays classic (the user's decision)";
		return HD_LARA_OUTFIT_NONE;
	}

	if (tr3Outfit > HD_LARA_OUTFIT_NONE && tr3Outfit < HD_LARA_OUTFIT_COUNT)
	{
		*why = "a Tomb Raider III outfit the randomizer rolled";
		return tr3Outfit;
	}

	/* LARA_DIVESUIT and LARA_CATSUIT, by value. The enum lives in the
	 * engine's global\types.h, which this file must not include - it is built
	 * without the engine so the tools can use it - so the two numbers stand
	 * here with the enum written out beside them: LARA_NONE 0, LARA_NORMAL 1,
	 * LARA_YOUNG 2, LARA_BUNHEAD 3, LARA_CATSUIT 4, LARA_DIVESUIT 5,
	 * LARA_PONYTAIL 6. The level header's byte is one LESS than these
	 * (file.cpp: LaraDrawType = data[0] + LARA_NORMAL), which is why the
	 * caller passes LaraDrawType and not the byte. */
	if (laraDrawType == 5)
	{
		*why = "the diving suit";
		return HD_LARA_OUTFIT_SUBSUIT;
	}

	if (laraDrawType == 4)
	{
		*why = "the VCI catsuit";
		return HD_LARA_OUTFIT_VCI;
	}

	if (russiaCap)
	{
		*why = "the cap, by the engine's own braid test";
		return HD_LARA_OUTFIT_RUSSIA;
	}

	*why = "the standard body";
	return HD_LARA_OUTFIT_CLASSIC;
}

/* --------------------------------------------------------------- parts -- */

int HDLaraPartSlot(int part)
{
	if (part < 0 || part >= HD_LARA_PART_COUNT)
		return -1;

	return HD_LARA_SLOT_BASE + part;
}

const char* HDLaraPartModel(int outfit, int part)
{
	if (!outfit_takes(outfit, part))
		return 0;

	if (part == HD_LARA_PART_BODY)
		return kOutfit[outfit].model;

	return kPart[part].model;
}

const char* HDLaraPartLabel(int part)
{
	if (part < 0 || part >= HD_LARA_PART_COUNT)
		return "?";

	return kPart[part].label;
}

int HDLaraRoleOfSlot(int slot)
{
	const char* name = HDSlotName(slot);

	if (!name)
		return HD_LARA_ROLE_NONE;

	for (int i = 0; i < (int)(sizeof(kSlotRole) / sizeof(kSlotRole[0])); i++)
		if (same(name, kSlotRole[i].slot))
			return kSlotRole[i].role;

	return HD_LARA_ROLE_NONE;
}

int HDLaraPartForSlot(int slot, int role)
{
	const char* name = HDSlotName(slot);

	if (!name)
		return -1;

	if (role == HD_LARA_ROLE_BACK)
	{
		for (int i = 0; i < (int)(sizeof(kBackRole) / sizeof(kBackRole[0])); i++)
			if (same(name, kBackRole[i].slot))
				return kBackRole[i].part;

		return -1;
	}

	for (int i = 0; i < (int)(sizeof(kSlotRole) / sizeof(kSlotRole[0])); i++)
		if (same(name, kSlotRole[i].slot))
		{
			/* A body slot answers a HAND question with the rest hands - the
			 * engine puts LARA's own mesh back at LM_RHAND when a gun is put
			 * away - and answers nothing else. */
			if (kSlotRole[i].role == HD_LARA_ROLE_BODY)
				return role == HD_LARA_ROLE_HAND ? kSlotRole[i].part : -1;

			return kSlotRole[i].role == role ? kSlotRole[i].part : -1;
		}

	return -1;
}

/* ------------------------------------------------------- the level -- */

int HDLaraPlan(const HDLaraLevel* level, HDModelSlot* out, int max)
{
	if (!level || !out || max <= 0)
		return 0;

	if (level->outfit <= HD_LARA_OUTFIT_NONE ||
		level->outfit >= HD_LARA_OUTFIT_COUNT)
		return 0;

	if (!level->bodyBind || level->bodyNodes < HD_LARA_NODES)
		return 0;

	bool useful[HD_LARA_PART_COUNT];

	plan_mask(level, useful);

	int written = 0;

	for (int part = 0; part < HD_LARA_PART_COUNT && written < max; part++)
	{
		const char* model = HDLaraPartModel(level->outfit, part);

		if (!model || !useful[part])
			continue;

		/* The braid is the one part with a skeleton of its own: six links,
		 * not fifteen limbs, and its bind pose is the HAIR slot's mesh tree.
		 * Without one there is nothing to fold the model against and the
		 * braid is left out - the classic one still draws. */
		const bool braid = part == HD_LARA_PART_BRAID;

		if (braid && (!level->hairBind || level->hairNodes < HD_LARA_HAIR_NODES))
			continue;

		HDModelSlot& one = out[written++];

		one.slot = HDLaraPartSlot(part);
		one.nodes = braid ? HD_LARA_HAIR_NODES : HD_LARA_NODES;
		one.bind = braid ? level->hairBind : level->bodyBind;
		one.name = model;
		one.rigidNode = kPart[part].rigid;
	}

	return written;
}

bool HDLaraLevelBuild(const HDLaraLevel* level, HDLaraStats* stats)
{
	HDLaraLevelEnd();

	if (stats)
		std::memset(stats, 0, sizeof(*stats));

	if (!level)
		return false;

	g_outfit = level->outfit;

	if (stats)
		stats->outfit = level->outfit;

	if (level->outfit <= HD_LARA_OUTFIT_NONE ||
		level->outfit >= HD_LARA_OUTFIT_COUNT)
	{
		note("Lara stays classic - no outfit chosen");
		return false;
	}

	double drift_sum = 0;
	float drift_worst = 0;
	int drift_parts = 0;

	plan_mask(level, g_planned);

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		const char* model = HDLaraPartModel(level->outfit, part);

		if (!model || !g_planned[part])
		{
			g_planned[part] = false;
			continue;
		}

		if (stats)
			stats->wanted++;

		const int slot = HDLaraPartSlot(part);

		if (!HDModelHas(slot))
		{
			if (stats)
				stats->missing++;

			note("%s: %s.TRM did not load - that part stays classic",
				HDLaraPartLabel(part), model);
			continue;
		}

		g_has[part] = true;

		const int nodes = HDModelNodes(slot);

		g_coverage[part] = 0;

		for (int node = 0; node < nodes && node < 32; node++)
			if (HDModelNodeCovered(slot, node))
				g_coverage[part] |= 1u << node;

		if (stats)
		{
			stats->loaded++;
			stats->vertices += HDModelVertices(slot);
			stats->indices += HDModelIndices(slot);
			stats->ranges += HDModelRanges(slot);
			stats->pages += HDModelPageCount(slot);
			stats->vertexBytes += (double)HDModelVertices(slot) * 24.0;
			stats->indexBytes += (double)HDModelIndices(slot) * 2.0;
		}

		/* THE DRIFT, the same measurement stage S3-2 judges a randomized
		 * enemy slot by: the distances between the nodes of the model against
		 * the distances between the same nodes of the classic bind pose. A
		 * wardrobe part that belongs on Lara's tree keeps it small; one that
		 * does not - a TR4 outfit skinned to a TR5 skeleton, say - does not.
		 * A rigid part has no skinning to measure and a one-node part no pair
		 * of nodes, so both come back false and are simply not counted. */
		float mean = 0, worst = 0;

		if (HDModelBindDrift(slot, &mean, &worst))
		{
			drift_sum += mean;
			drift_parts++;

			if (worst > drift_worst)
				drift_worst = worst;

			if (mean > HD_SKELETON_DRIFT_LIMIT)
				note("%s: %s drifts %.0f against Lara's own skeleton - "
					"over the limit of %.0f",
					HDLaraPartLabel(part), model, mean,
					(double)HD_SKELETON_DRIFT_LIMIT);
		}
	}

	if (stats && drift_parts)
	{
		stats->driftMean = (float)(drift_sum / drift_parts);
		stats->driftWorst = drift_worst;
		stats->driftParts = drift_parts;
	}

	if (!g_has[HD_LARA_PART_BODY])
	{
		note("Lara stays classic - %s.TRM is the body and it is not there",
			kOutfit[level->outfit].model);
		return false;
	}

	/* The one loose end worth a line in every log rather than a comment
	 * nobody reads: the grappling gun with a laser sight wears the crossbow's
	 * hands, because the wardrobe has no grapple variant of that model. */
	if (g_has[HD_LARA_PART_HAND_GRAPPLE_LASER])
		note("the grappling gun with a sight wears HAND_BARE_CROSSBOW_LASER - "
			"the wardrobe ships no grapple variant");

	g_active = true;
	return true;
}

void HDLaraLevelEnd()
{
	g_outfit = HD_LARA_OUTFIT_NONE;
	g_active = false;
	g_notes.clear();

	for (int i = 0; i < HD_LARA_PART_COUNT; i++)
	{
		g_has[i] = false;
		g_planned[i] = false;
		g_coverage[i] = 0;
	}
}

bool HDLaraActive()
{
	return g_active;
}

int HDLaraOutfitNow()
{
	return g_outfit;
}

bool HDLaraHasPart(int part)
{
	return part >= 0 && part < HD_LARA_PART_COUNT && g_has[part];
}

bool HDLaraPartPlanned(int part)
{
	return part >= 0 && part < HD_LARA_PART_COUNT && g_planned[part];
}

unsigned int HDLaraPartCoverage(int part)
{
	return part >= 0 && part < HD_LARA_PART_COUNT ? g_coverage[part] : 0u;
}

/* --------------------------------------------------------- the frame -- */

int HDLaraFrameParts(const HDLaraFrame* now, HDLaraDraw* out, int max)
{
	if (!now || !out || max <= 0 || !g_active)
		return 0;

	const unsigned int kRightArm =
		(1u << HD_LARA_RINARM) | (1u << HD_LARA_ROUTARM) | (1u << HD_LARA_RHAND);
	const unsigned int kLeftArm =
		(1u << HD_LARA_LINARM) | (1u << HD_LARA_LOUTARM) | (1u << HD_LARA_LHAND);

	int written = 0;

	/* One entry, if the part is on the card at all. A macro rather than a
	 * lambda so that the file keeps the plain C shape the rest of
	 * specific\hd is in. */
	#define HD_LARA_ADD(which_, mask_) \
		do { \
			const int p_ = (which_); \
			const unsigned int m_ = (mask_); \
			if (p_ >= 0 && written < max && HDLaraHasPart(p_)) \
			{ \
				out[written].part = p_; \
				out[written].nodeMask = m_; \
				written++; \
			} \
		} while (0)

	/* THE HANDS. Both sides are asked separately and only split into two
	 * draws when they disagree - see the rule in hd_lara.h. A slot that names
	 * no part at all (a mesh swap the randomizer put there, say) falls back
	 * to the rest hands, which is a hand and not a hole. */
	int right = HDLaraPartForSlot(now->rightHandSlot, HD_LARA_ROLE_HAND);
	int left = HDLaraPartForSlot(now->leftHandSlot, HD_LARA_ROLE_HAND);

	if (right < 0)
		right = HD_LARA_PART_HAND_REST;

	if (left < 0)
		left = HD_LARA_PART_HAND_REST;

	if (right == left)
		HD_LARA_ADD(right, ~0u);
	else
	{
		HD_LARA_ADD(right, kRightArm);
		HD_LARA_ADD(left, kLeftArm);
	}

	/* THE THIGH HOLSTERS. Left is node 1, right is node 4 - the two slots the
	 * engine keeps in LHolster and lara.holster, and DrawLara puts them on
	 * LMX_THIGH_L and LMX_THIGH_R in that order. An empty holster names no
	 * part: the straps are painted into the outfit. */
	const int leftHolster =
		HDLaraPartForSlot(now->leftHolsterSlot, HD_LARA_ROLE_HOLSTER);
	const int rightHolster =
		HDLaraPartForSlot(now->rightHolsterSlot, HD_LARA_ROLE_HOLSTER);

	if (leftHolster >= 0 && leftHolster == rightHolster)
		HD_LARA_ADD(leftHolster, ~0u);
	else
	{
		HD_LARA_ADD(leftHolster, 1u << HD_LARA_LTHIGH);
		HD_LARA_ADD(rightHolster, 1u << HD_LARA_RTHIGH);
	}

	/* THE WEAPON ON THE BACK, on the torso node. */
	HD_LARA_ADD(HDLaraPartForSlot(now->backGunSlot, HD_LARA_ROLE_BACK), ~0u);

	#undef HD_LARA_ADD

	return written;
}

/* ----------------------------------------------------------- the log -- */

int HDLaraNoteCount()
{
	return (int)g_notes.size();
}

const char* HDLaraNote(int at)
{
	if (at < 0 || at >= (int)g_notes.size())
		return "";

	return g_notes[(size_t)at].c_str();
}

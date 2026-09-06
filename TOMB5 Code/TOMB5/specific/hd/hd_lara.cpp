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
	 * helmet instead of a braid, and DrawLara__5 draws no holsters at all.
	 *
	 * `gloves` picks the HAND_GLOVES_ file instead of the HAND_BARE_ one
	 * (#123, fix round C, user finding C2: "OUTFIT_RUSSIA wears gloves"). An
	 * outfit model covers all fifteen nodes including the two hand nodes, but
	 * what it carries THERE is the sleeve - the hand itself is the separate
	 * HAND_ model, which is the whole reason the remaster ships three sets of
	 * them. The Russia suit's sleeve ends in a glove cuff and a bare hand came
	 * out of it. Only that one outfit is set: nobody has reported a wrong hand
	 * on any other, and each is a picture the user has to judge rather than a
	 * thing this file can measure.
	 *
	 * The third set, HAND_XRAY_*, belongs to a remaster feature this engine
	 * has not got - the x-ray view - and is deliberately unused here. */
	struct Outfit
	{
		const char* model;
		const char* label;
		bool hands;
		bool braid;
		bool holsters;
		bool gloves;
		/* Does the outfit's OWN body carry the thigh holster rig - the pouch,
		 * the strap and the belt the guns hang in (#123, fix round D, D4)?
		 * Measured over the eight wardrobe bodies, triangles on nodes 1 and 4
		 * by texture page:
		 *
		 *   OUTFIT_CLASSIC      8017: 1090/1090   + 8018: 56/56    yes
		 *   OUTFIT_RUSSIA       8017: 1106/1114   + 8018: 56/56    yes
		 *   OUTFIT_TR3_COAST    8205:  354/354                     yes
		 *   OUTFIT_TR3_CATSUIT  8205:  354/354                     yes
		 *   OUTFIT_VCI          -                                  NO
		 *   OUTFIT_TR3_ANTARC   -                                  NO
		 *   OUTFIT_TR3_NEVADA   -                                  NO
		 *   OUTFIT_SUBSUIT      -                          holsters none anyway
		 *
		 * false here means HD_LARA_PART_HOLSTER_RIG is worn on top. */
		bool thighRig;

		/* THE BODY'S SHEEN (#123, fix round D, D2): the Blinn-Phong strength
		 * and exponent for the OUTFIT model. See kPartSheen below for the
		 * other parts and for why these are a material table and not a
		 * measurement. */
		float sheen;
		float sheenPower;
	};

	const Outfit kOutfit[HD_LARA_OUTFIT_COUNT] =
	{
		{ 0,                    "classic (no HD body)",   false, false, false, false, false, 0.00f,  1.0f },
		{ "OUTFIT_CLASSIC",     "the Rome outfit",        true,  true,  true,  false, true,  0.10f, 14.0f },
		{ "OUTFIT_RUSSIA",      "the Russia outfit",      true,  true,  true,  true,  true,  0.12f, 16.0f },
		{ "OUTFIT_SUBSUIT",     "the diving suit",        false, false, false, false, true,  0.30f, 22.0f },
		{ "OUTFIT_VCI",         "the VCI catsuit",        true,  true,  true,  false, false, 0.45f, 26.0f },
		{ "OUTFIT_TR3_ANTARC",  "Antarctica (TR3)",       true,  true,  true,  false, false, 0.10f, 14.0f },
		{ "OUTFIT_TR3_NEVADA",  "Nevada (TR3)",           true,  true,  true,  false, false, 0.08f, 12.0f },
		{ "OUTFIT_TR3_COAST",   "Jungle (TR3)",           true,  true,  true,  false, true,  0.08f, 12.0f },
		{ "OUTFIT_TR3_CATSUIT", "London (TR3)",           true,  true,  true,  false, true,  0.45f, 26.0f }
	};

	/* THE OTHER PARTS. `kind` decides which outfits take the part at all:
	 *   0 always, 1 only an outfit with hands, 2 only one with a braid,
	 *   3 only one with holsters.
	 * `rigid` >= 0 means the file is not skinned to the mesh tree but nailed
	 * to that one node - LARA_HEAD is a facial rig whose bones run to 31 and
	 * mean nothing at all to Lara's 15 node tree.
	 * `nodes` is the coverage measured over 4\ITEM, written down so that the
	 * masks in HDLaraFrameParts can be read against it.
	 * `gloves` is the same file for a gloved outfit, or 0 when the wardrobe
	 * ships no gloved twin - then the bare one is worn and the log says so.
	 * All twelve hand parts have one; the column is there so that the day a
	 * thirteenth has not, the picture is a hand and not a hole. */
	struct Part
	{
		const char* model;
		const char* gloves;
		const char* label;
		int kind;
		int rigid;
		const char* nodes;
		// #123 fix round D (D4): -1 is the whole file, a .DDS number cuts the
		// part out of a bigger model. See HDModelSlot::onlyPage.
		int onlyPage;
	};

	const Part kPart[HD_LARA_PART_COUNT] =
	{
		{ 0,                          0,
		  "the body",                 0, -1, "0..14", -1 },
		{ "LARA_HEAD",                0,
		  "the face",                 0, HD_LARA_HEAD_NODE, "14 (rigid)", -1 },
		{ "HAIR_OG",                  0,
		  "the braid",                2, -1, "0..5 of its own chain", -1 },

		{ "HAND_BARE_REST",           "HAND_GLOVES_REST",
		  "empty hands",              1, -1, "8,9,10,11,12,13", -1 },
		{ "HAND_BARE_PISTOL",         "HAND_GLOVES_PISTOL",
		  "the pistols",              1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_UZI",            "HAND_GLOVES_UZI",
		  "the uzis",                 1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_SHOTGUN",        "HAND_GLOVES_SHOTGUN",
		  "the shotgun",              1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_HK",             "HAND_GLOVES_HK",
		  "the HK",                   1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_DEAGLE",         "HAND_GLOVES_DEAGLE",
		  "the Desert Eagle",         1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_DEAGLE_LASER",   "HAND_GLOVES_DEAGLE_LASER",
		  "the Desert Eagle, sight",  1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_GRAPPLE",        "HAND_GLOVES_GRAPPLE",
		  "the grappling gun",        1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_CROSSBOW_LASER", "HAND_GLOVES_CROSSBOW_LASER",
		  "the grappling gun, sight", 1, -1, "9,10,12,13", -1 },
		{ "HAND_BARE_FLARE",          "HAND_GLOVES_FLARE",
		  "the flare",                1, -1, "12,13", -1 },
		{ "HAND_BARE_TORCH",          "HAND_GLOVES_TORCH",
		  "the torch",                1, -1, "12,13", -1 },
		{ "HAND_BARE_CROWBAR",        "HAND_GLOVES_CROWBAR",
		  "the crowbar",              1, -1, "9,10", -1 },

		{ "HOLSTER_PISTOL",           0,
		  "the pistol holsters",      3, -1, "1,4", -1 },
		{ "HOLSTER_UZI",              0,
		  "the uzi holsters",         3, -1, "1,4", -1 },
		{ "HOLSTER_DEAGLE",           0,
		  "the Desert Eagle holster", 3, -1, "4", -1 },

		/* The pouches themselves, cut out of the London catsuit by page (D4).
		 * kind 4 is "worn, and only by an outfit whose body has no rig". */
		{ "OUTFIT_TR3_CATSUIT",       0,
		  "the holster rig",          4, -1, "0,1,4", 8205 },

		{ "HOLSTER_SHOTGUN",          0,
		  "the shotgun on the back",  3, -1, "7", -1 },
		{ "HOLSTER_HK",               0,
		  "the HK on the back",       3, -1, "7", -1 },
		{ "HOLSTER_GRAPPLE",          0,
		  "the grapple on the back",  3, -1, "7", -1 }
	};

	/* THE SHEEN OF EVERY PART THAT IS NOT THE BODY (#123, fix round D, D2).
	 *
	 * WHAT THIS TABLE IS. TR5's own lighting has no specular term at all -
	 * aTransformLightClipMesh adds an ambient and a diffuse cosine and stops -
	 * so every HD outfit came out matt beside the remaster's own pictures,
	 * where the VCI catsuit and the TR3 London one read as LATEX and the skin
	 * carries a soft sheen. The user's finding D2, with the reference shots.
	 *
	 * WHY IT IS A TABLE AND NOT A MEASUREMENT, which matters because
	 * everything else in this file is measured. The obvious source would have
	 * been the pages' ALPHA channel: fix round A2 found it to be a gloss mask
	 * on the room pages, and TROne says the same of the item ones in so many
	 * words (hostender\game_renderer.cpp:373). It is not one here. Measured
	 * with DdsProbe -alpha over the wardrobe's pages:
	 *
	 *     page 8001 (skin trim)   mean 254.9   0 texels below 128
	 *     page 8008 (leggings)    mean 254.8   0
	 *     page 8016 (Rome shorts) mean 254.8   0
	 *     page 8018 (backpack)    mean 254.9   0
	 *     page 8027 (VCI suit)    mean 255.0   0
	 *     page 8028 (VCI trim)    mean 254.6   0
	 *     page 8205 (holster rig) mean 254.8   0
	 *
	 * Every one of them has 100 % of its texels in the 240..255 bucket: a
	 * flat opaque channel with BC7 quantisation noise on it. There is no mask
	 * to read. (The one page that is not flat is 8002, Lara's hair, 15.2 %
	 * below 128 - and that is coverage.) So the material has to be named, and
	 * naming it in one table with the numbers in it is the honest way to do
	 * that: a picture the user does not like is one line to change.
	 *
	 * THE NUMBERS. strength is how much white the highlight adds at its
	 * brightest, power the Blinn-Phong exponent - low is a broad satin sheen,
	 * high a tight wet point.
	 *
	 *   latex     0.45 / 26   the two catsuits, the reference look
	 *   rubber    0.30 / 22   the diving suit
	 *   skin      0.12 / 10   a soft wide sheen, never a point
	 *   hair      0.14 / 18
	 *   leather   0.18 / 20   the holster rig and the worn pouches
	 *   gunmetal  0.35 / 34   a hard small highlight on a weapon
	 *   cloth     0.10 / 14   the Rome top and the rest
	 *
	 * A part not named here takes 0 and does not shine. */
	struct PartSheen
	{
		int part;
		float strength;
		float power;
	};

	const PartSheen kPartSheen[] =
	{
		{ HD_LARA_PART_FACE,               0.12f, 10.0f },	// skin
		{ HD_LARA_PART_BRAID,              0.14f, 18.0f },	// hair

		{ HD_LARA_PART_HAND_REST,          0.12f, 10.0f },	// skin
		{ HD_LARA_PART_HAND_PISTOL,        0.35f, 34.0f },	// gunmetal
		{ HD_LARA_PART_HAND_UZI,           0.35f, 34.0f },
		{ HD_LARA_PART_HAND_SHOTGUN,       0.30f, 30.0f },
		{ HD_LARA_PART_HAND_HK,            0.30f, 30.0f },
		{ HD_LARA_PART_HAND_DEAGLE,        0.35f, 34.0f },
		{ HD_LARA_PART_HAND_DEAGLE_LASER,  0.35f, 34.0f },
		{ HD_LARA_PART_HAND_GRAPPLE,       0.30f, 30.0f },
		{ HD_LARA_PART_HAND_GRAPPLE_LASER, 0.30f, 30.0f },
		{ HD_LARA_PART_HAND_FLARE,         0.12f, 10.0f },
		{ HD_LARA_PART_HAND_TORCH,         0.12f, 10.0f },
		{ HD_LARA_PART_HAND_CROWBAR,       0.30f, 30.0f },

		{ HD_LARA_PART_HOLSTER_PISTOL,     0.35f, 34.0f },
		{ HD_LARA_PART_HOLSTER_UZI,        0.35f, 34.0f },
		{ HD_LARA_PART_HOLSTER_DEAGLE,     0.35f, 34.0f },
		{ HD_LARA_PART_HOLSTER_RIG,        0.18f, 20.0f },	// leather

		{ HD_LARA_PART_BACK_SHOTGUN,       0.30f, 30.0f },
		{ HD_LARA_PART_BACK_HK,            0.30f, 30.0f },
		{ HD_LARA_PART_BACK_GRAPPLE,       0.30f, 30.0f }
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
	 * TR5's REVOLVER_ANIM is a DESERT EAGLE for the same reason, and the
	 * remaster ships both guns: HAND_BARE_REVOLVER / HOLSTER_REVOLVER are
	 * Tomb Raider IV's six-shooter, HAND_BARE_DEAGLE / HOLSTER_DEAGLE the
	 * automatic TR5 puts in that slot. Measured with ModelProbe -name: the
	 * two holsters cover the same node 4 and differ only in the gun in them
	 * (1703 against 1270 vertices), so the swap is a swap of names and
	 * nothing else. Until fix round C the wardrobe drew the six-shooter -
	 * user finding C1, "the revolver is unused in the rando".
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
		{ "REVOLVER_ANIM",        HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_DEAGLE },
		{ "LARA_REVOLVER_LASER",  HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_DEAGLE_LASER },
		{ "CROSSBOW_ANIM",        HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_GRAPPLE },
		{ "LARA_CROSSBOW_LASER",  HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_GRAPPLE_LASER },
		{ "FLARE_ANIM",           HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_FLARE },
		{ "TORCH_ANIM",           HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_TORCH },
		{ "CROWBAR_ANIM",         HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_CROWBAR },
		{ "LARA_HOLSTERS",        HD_LARA_ROLE_HOLSTER,  -1 },
		{ "LARA_HOLSTERS_PISTOLS", HD_LARA_ROLE_HOLSTER, HD_LARA_PART_HOLSTER_PISTOL },
		{ "LARA_HOLSTERS_UZIS",   HD_LARA_ROLE_HOLSTER,  HD_LARA_PART_HOLSTER_UZI },
		{ "LARA_HOLSTERS_REVOLVER", HD_LARA_ROLE_HOLSTER, HD_LARA_PART_HOLSTER_DEAGLE },
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

	/* WHICH PARTS THIS LEVEL CAN ASK FOR. The body, the face, the braid, the
	 * rest hands and everything WORN always; a HAND part only when the level
	 * really loaded the slot it hangs off. Without a slot list everything the
	 * outfit takes is planned, which is what a diagnostic run with no level
	 * wants.
	 *
	 * THE HANDS AND THE WORN PARTS ARE NOT THE SAME QUESTION (#123, fix round
	 * C, user finding C3: "the VCI Lara's holsters are missing"). A hand part
	 * is reachable only through lara.mesh_ptrs, and a mesh can only sit at the
	 * hand node if its slot is loaded - so filtering hands by the slot list
	 * throws nothing away. The holsters and the weapon on the back are named
	 * by SLOT NUMBER in three of the engine's own variables (LHolster,
	 * lara.holster, lara.back_gun), and those numbers are written from the
	 * INVENTORY, not from what the level loaded: setup.cpp:115 puts
	 * LARA_HOLSTERS_PISTOLS on both thighs at every level start and
	 * LaraInitialiseMeshes sets lara.back_gun from shotgun_type_carried.
	 *
	 * Measured over the shipped files: the three VCI levels load NO holster
	 * slot at all (ModelProbe -lara RICH1/2/3 against vanilla: "holsters: left
	 * -, right -", 6, 7 and 9 parts planned), because Core drew no thigh
	 * holsters on the catsuit and left the objects out. The randomizer hands
	 * that Lara pistols anyway, so the engine says "pistols on the thighs" and
	 * the plan had loaded nothing to draw them with. Planning them by the
	 * OUTFIT rather than by the slot list is the rule the user asked for - the
	 * holster follows the weapon she carries, not the slot the level shipped -
	 * and it costs three thigh models and three back models per level.
	 *
	 * It cannot invent a holster she has not got: a takeaway of the pistols
	 * runs NailInvItem, which puts LARA_HOLSTERS back into both variables
	 * (newinv2.cpp:2853), and an empty holster names no part.
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

		/* kind 3 is "worn": the three thigh holsters and the three weapons on
		 * the back. outfit_takes still decides whether this outfit has any at
		 * all - the diving suit has none - and HDLaraPartModel answers 0 for
		 * a part the outfit does not take, so the plan loop skips it. */
		for (int i = 0; i < HD_LARA_PART_COUNT; i++)
			if (kPart[i].kind == 3 || kPart[i].kind == 4)
				useful[i] = true;

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
		/* The holster rig (#123, fix round D, D4): worn by an outfit that
		 * holsters at all and whose own body has no pouches built in. An
		 * outfit that has them would wear two. */
		case 4: return kOutfit[outfit].holsters && !kOutfit[outfit].thighRig;
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
	 *    decided she stays classic (plan section 9), and the caller works the
	 *    question out the way the ENGINE works it out - see the header, and
	 *    the note there about the level range, which is the one answer that
	 *    was wrong.
	 * 2. A Tomb Raider III guest, out of data[3] of the level header, because
	 *    the randomizer put it there and the LaraType byte cannot say which.
	 * 3. The diving suit and the catsuit, which have a LaraType of their own
	 *    and their own draw path in the engine (DrawLara__5 / __4).
	 * 4. LARA_PONYTAIL, which used to mean two things and, with step 2 above
	 *    it, now means one - see below.
	 * 5. The cap, by the engine's own test - see the header.
	 * 6. Everything else is the Rome outfit. */
	if (youngLara)
	{
		*why = "young Lara stays classic (the user's decision)";
		return HD_LARA_OUTFIT_NONE;
	}

	/* 5..8 AND NOTHING ELSE. The four Tomb Raider III outfits are the top of
	 * the enum precisely so that this test can be a range: 1..4 are the
	 * vanilla bodies, and a byte that says one of those did not come from the
	 * randomizer's wardrobe - it came from a file written by something else,
	 * or from a header word that was never meant to carry an outfit at all.
	 * Letting such a byte through would let a stale file overrule the level's
	 * own LaraType, which is the one mistake in this whole path that nobody
	 * would be able to see the cause of. */
	if (tr3Outfit >= HD_LARA_OUTFIT_TR3_ANTARC && tr3Outfit <= HD_LARA_OUTFIT_TR3_CATSUIT)
	{
		*why = "a Tomb Raider III outfit the randomizer rolled (data[3])";
		return tr3Outfit;
	}

	/* Anything else in that argument - a vanilla outfit number, a byte out of
	 * a file this engine has never met - falls through to the level's own
	 * facts below, exactly as a 0 would. The caller writes the raw byte into
	 * its own log line, so a garbled one is visible there rather than here. */

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

	/* LARA_PONYTAIL, THE BYTE THAT MEANT TWO THINGS. The randomizer writes 5
	 * in two quite different cases (TR5Randomizer\src\core\Randomizer.cpp,
	 * the wardrobe roll):
	 *
	 *   - the Rome body inside a Russia level, where LARA_NORMAL would send
	 *     hair.cpp and laraskin.cpp down the cap's braid handling, and
	 *   - EVERY Tomb Raider III guest outfit (#122), for the same reason -
	 *     the guest's head comes from ANDREA1 and wants the standard braid.
	 *
	 * Until stage S7 nothing in the level file separated the two, so this
	 * said "classic" for both: a right classic body beats a wrong HD one.
	 * data[3] now says it, and it has already been read above - so by the
	 * time execution reaches this line the guest case is gone and what is
	 * left is the other one: the Rome body, wearing a ponytail because the
	 * level around it is a Russia level. OUTFIT_CLASSIC is the body that
	 * belongs to it (the user's decision, 2026-09-05).
	 *
	 * THE ONE CASE THIS GETS WRONG, and it is worth naming: a level file
	 * written by a run OLDER than stage S7-1 carries the ponytail byte and a
	 * data[3] of 0 for a guest as well, and now shows a Rome Lara where that
	 * run rolled a Nevada one. Nothing in the file can tell the two apart -
	 * that is what the marker was added for - and a level randomized again
	 * with the current version says it. */
	if (laraDrawType == 6)
	{
		*why = "the ponytail byte with no Tomb Raider III marker beside it: "
			"the Rome body in a Russia level";
		return HD_LARA_OUTFIT_CLASSIC;
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

	/* THE GLOVED TWIN (#123, fix round C, user finding C2). A gloved outfit
	 * wears the HAND_GLOVES_ file; a hand part the wardrobe ships no gloved
	 * twin for falls back to the bare one, which HDLaraLevelBuild writes into
	 * the log. Nothing but a hand part has a twin, so the test is the field
	 * and not the kind. */
	if (kOutfit[outfit].gloves && kPart[part].gloves)
		return kPart[part].gloves;

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
		one.onlyPage = kPart[part].onlyPage;
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

		/* A gloved outfit with a hand part the wardrobe has no gloved twin
		 * for - none today, and the line is here so the day there is one it
		 * is in the log and not a surprise in a screenshot. */
		if (kOutfit[level->outfit].gloves && kPart[part].kind == 1 &&
			!kPart[part].gloves)
			note("%s: no gloved variant in the wardrobe - %s is worn bare",
				HDLaraPartLabel(part), model);

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

void HDLaraPartSheen(int part, float* strength, float* power)
{
	if (strength)
		*strength = 0.0f;

	if (power)
		*power = 1.0f;

	if (part < 0 || part >= HD_LARA_PART_COUNT)
		return;

	/* The BODY's sheen belongs to the outfit and not to the part: the same
	 * HD_LARA_PART_BODY is a Rome top in one level and a catsuit in the
	 * next. */
	if (part == HD_LARA_PART_BODY)
	{
		if (g_outfit <= HD_LARA_OUTFIT_NONE || g_outfit >= HD_LARA_OUTFIT_COUNT)
			return;

		if (strength)
			*strength = kOutfit[g_outfit].sheen;

		if (power)
			*power = kOutfit[g_outfit].sheenPower;

		return;
	}

	for (int i = 0; i < (int)(sizeof(kPartSheen) / sizeof(kPartSheen[0])); i++)
		if (kPartSheen[i].part == part)
		{
			if (strength)
				*strength = kPartSheen[i].strength;

			if (power)
				*power = kPartSheen[i].power;

			return;
		}
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
	int leftHolster =
		HDLaraPartForSlot(now->leftHolsterSlot, HD_LARA_ROLE_HOLSTER);
	int rightHolster =
		HDLaraPartForSlot(now->rightHolsterSlot, HD_LARA_ROLE_HOLSTER);
	int back = HDLaraPartForSlot(now->backGunSlot, HD_LARA_ROLE_BACK);

	/* A GUN IN THE HAND IS NOT ON THE BODY (#123, fix round C, user finding
	 * C1: "the revolver is in her hand AND in the holster at once").
	 *
	 * The engine keeps three separate variables for what Lara wears -
	 * LHolster, lara.holster, lara.back_gun - and they are bookkeeping, not
	 * a fact: every draw and undraw animation writes them at its OWN frame,
	 * and the paths do not agree with each other. Measured in this tree:
	 *
	 *   game\lara2gun.cpp:70 draw_pistol_meshes  clears lara.holster for
	 *       every weapon and LHolster for all but the revolver;
	 *   game\lara1gun.cpp:43 draw_shotgun_meshes clears lara.back_gun and
	 *       NEITHER holster - and larafire.cpp:475 InitialiseNewWeapon sends
	 *       the revolver down THAT path while the pistols go down the other,
	 *       so a revolver drawn through it keeps the holster the undraw
	 *       animation filled (undraw_pistol_mesh_right, lara2gun.cpp:107).
	 *
	 * Rather than reach into the engine's state machine - which the classic
	 * path lives on too - the seam states the rule the picture needs: what
	 * one of the hands is holding is not drawn a second time on the thigh or
	 * on the back. It costs one lookup per frame and it is right whichever
	 * of the paths above ran last. The reverse case needs no rule: a holster
	 * cannot fill itself. */
	{
		static const struct { int hand; int worn; } kWorn[] =
		{
			{ HD_LARA_PART_HAND_PISTOL,        HD_LARA_PART_HOLSTER_PISTOL },
			{ HD_LARA_PART_HAND_UZI,           HD_LARA_PART_HOLSTER_UZI },
			{ HD_LARA_PART_HAND_DEAGLE,        HD_LARA_PART_HOLSTER_DEAGLE },
			{ HD_LARA_PART_HAND_DEAGLE_LASER,  HD_LARA_PART_HOLSTER_DEAGLE },
			{ HD_LARA_PART_HAND_SHOTGUN,       HD_LARA_PART_BACK_SHOTGUN },
			{ HD_LARA_PART_HAND_HK,            HD_LARA_PART_BACK_HK },
			{ HD_LARA_PART_HAND_GRAPPLE,       HD_LARA_PART_BACK_GRAPPLE },
			{ HD_LARA_PART_HAND_GRAPPLE_LASER, HD_LARA_PART_BACK_GRAPPLE }
		};

		for (int i = 0; i < (int)(sizeof(kWorn) / sizeof(kWorn[0])); i++)
		{
			if (right != kWorn[i].hand && left != kWorn[i].hand)
				continue;

			if (leftHolster == kWorn[i].worn)
				leftHolster = -1;

			if (rightHolster == kWorn[i].worn)
				rightHolster = -1;

			if (back == kWorn[i].worn)
				back = -1;
		}
	}

	if (leftHolster >= 0 && leftHolster == rightHolster)
		HD_LARA_ADD(leftHolster, ~0u);
	else
	{
		HD_LARA_ADD(leftHolster, 1u << HD_LARA_LTHIGH);
		HD_LARA_ADD(rightHolster, 1u << HD_LARA_RTHIGH);
	}

	/* THE WEAPON ON THE BACK, on the torso node. */
	HD_LARA_ADD(back, ~0u);

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

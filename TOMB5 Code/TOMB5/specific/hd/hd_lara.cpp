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
#include "hd_wardrobe.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	/* ------------------------------------------------------- the wardrobe -- */

	/* THE NINE THE LEVEL'S OWN FACTS CAN REACH. Every one of these files was
	 * read and counted before it was written down here (ModelProbe -name
	 * <NAME> -nodraw); the node column is the measurement, not the plan.
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
	 * WHAT THIS TABLE STILL IS, AND WHAT IT IS NOT (#153, round M2). It is the
	 * enum's two words: which FILE an HD_LARA_OUTFIT_* number stands for and
	 * what to call it in the log. It is no longer the list - the list is a
	 * scan (hd_wardrobe.h) that finds a hundred outfits in the two installs
	 * and whatever a mod brings - and it is no longer the properties either:
	 * hands, braid, holsters, gloves, the holster rig, the sheen and the skin
	 * pages moved to hd_wardrobe.cpp's table, with the measurements that
	 * produced them, so that ONE answer serves the engine, the tools and the
	 * randomizer. `props_of` below is how this file asks for them.
	 *
	 * The nine numbers stay because they are written into level headers
	 * (data[3]) and into .hdm files and because HDLaraOutfitFor reaches them
	 * out of the level's own facts. Everything past them is worn because the
	 * player asked for it (hd_laradraw.cpp, the registry's hd_outfit) and is
	 * named HD_LARA_OUTFIT_WEAR + a scan index. */
	struct Outfit
	{
		const char* model;
		const char* label;
	};

	const Outfit kOutfit[HD_LARA_OUTFIT_COUNT] =
	{
		{ 0,                    "classic (no HD body)" },
		{ "OUTFIT_CLASSIC",     "the Rome outfit" },
		{ "OUTFIT_RUSSIA",      "the Russia outfit" },
		{ "OUTFIT_SUBSUIT",     "the diving suit" },
		{ "OUTFIT_VCI",         "the VCI catsuit" },
		{ "OUTFIT_TR3_ANTARC",  "Antarctica (TR3)" },
		{ "OUTFIT_TR3_NEVADA",  "Nevada (TR3)" },
		{ "OUTFIT_TR3_COAST",   "Jungle (TR3)" },
		{ "OUTFIT_TR3_CATSUIT", "London (TR3)" }
	};

	/* THE PROPERTIES OF WHATEVER IS BEING WORN (#153, round M2).
	 *
	 * One scan entry, found two ways: a wear number carries its index, and one
	 * of the nine is matched to its entry BY FILE NAME out of the IV-VI
	 * install - which is where all eight bodies live and where their
	 * measurements were made.
	 *
	 * A null answer means the scan has no entry for it, which happens when the
	 * install is not there at all. Every caller then falls back to what the
	 * wardrobe's own defaults are, because a body that draws with the right
	 * geometry and a plain material is a picture and a body that draws nothing
	 * is a hole. */
	/* THE PAGE NUMBER BEHIND A CACHE KEY (#153, round M2).
	 *
	 * A model's pages are offered to the cache under a key, not under the
	 * number the .TRM wrote: a model out of the Tomb Raider I-III bundle
	 * carries HD_PAGE_ALT_BASE (#123, S7-2) and one a mod supplied carries
	 * that mod's namespace (#153, M1), because two bundles and any number of
	 * mods each have a page 8001 and they are different pictures.
	 *
	 * The namespace is an ALLOCATION; the number is the material's NAME, and
	 * every rule in this file that asks "is this page her skin" or "is this
	 * page the gun rather than the hand" is asking about the name. So the
	 * allocation is stripped here, in one place, rather than in each of them:
	 * the wardrobe's TRX outfits are the case that proved it - their pages
	 * are 8415, 8001 and 8002 of THAT bundle, and a comparison against the
	 * key found none of them. */
	int page_number_of(int key)
	{
		if (HDPageModIndex(key) >= 0)
			return HDPageModNumber(key);

		return key >= HD_PAGE_ALT_BASE ? key - HD_PAGE_ALT_BASE : key;
	}

	int wear_index_of(int outfit)
	{
		const int wear = HDLaraWearOfOutfit(outfit);

		if (wear >= 0)
			return wear;

		if (outfit <= HD_LARA_OUTFIT_NONE || outfit >= HD_LARA_OUTFIT_COUNT)
			return -1;

		return HDWardrobeFindFile(kOutfit[outfit].model, HD_WARDROBE_SOURCE_46);
	}

	/* REMEMBERED, BECAUSE IT IS ASKED EVERY FRAME (#153, round M2b).
	 *
	 * `wear_index_of` ends in HDWardrobeFindFile, which walks the whole scan -
	 * 56 entries on this machine, more with mods - and builds a std::string
	 * per entry to compare with. That is fine once a level; it is not fine
	 * where it really sits, which is HDLaraPartSheen, called once per PART per
	 * DRAW, and HDLaraSkinPage, called once per page of the body.
	 *
	 * One answer is remembered and it is keyed by the outfit, so a caller
	 * asking about a different one gets the walk and the right answer. What it
	 * rests on - the scan's list - is fixed for the session, and the memory is
	 * dropped in HDLaraLevelEnd, which every level build calls first, so a
	 * tool that re-scans with another Mods folder between levels is safe too. */
	int g_props_outfit = HD_LARA_OUTFIT_NONE;
	const HDWardrobeEntry* g_props = 0;

	const HDWardrobeEntry* props_of(int outfit)
	{
		if (outfit == g_props_outfit)
			return g_props;

		g_props_outfit = outfit;
		g_props = HDWardrobeAt(wear_index_of(outfit));

		return g_props;
	}

	// The file an outfit number wears - the enum's own name, or the scan
	// entry's. Null when neither has one.
	const char* model_of(int outfit)
	{
		const int wear = HDLaraWearOfOutfit(outfit);

		if (wear >= 0)
		{
			const HDWardrobeEntry* e = HDWardrobeAt(wear);

			return e ? e->file : 0;
		}

		return outfit > HD_LARA_OUTFIT_NONE && outfit < HD_LARA_OUTFIT_COUNT
			? kOutfit[outfit].model : 0;
	}

	// Is this a number a body can be found for at all?
	bool outfit_real(int outfit)
	{
		return model_of(outfit) != 0;
	}

	/* THE FIVE FLAGS AND THE TWO MATERIALS, from the scan where there is one
	 * and from the wardrobe's own defaults where there is not. The defaults
	 * are repeated here rather than reached for, because this file must go on
	 * drawing a body when hd_wardrobe has found no install to scan - and they
	 * are the SAME defaults, which the one-line comment beside each says. */
	bool takes_hands(int outfit)
	{
		const HDWardrobeEntry* e = props_of(outfit);

		return e ? e->hands : true;	// kDefaultHands
	}

	bool takes_braid(int outfit)
	{
		const HDWardrobeEntry* e = props_of(outfit);

		return e ? e->braid : true;	// kDefaultBraid
	}

	bool takes_holsters(int outfit)
	{
		const HDWardrobeEntry* e = props_of(outfit);

		return e ? e->holsters : true;	// kDefaultHolsters
	}

	/* THE GLOVES ARE NEVER WORN (#153, round M2b - the user's decision).
	 *
	 * "In every level with every outfit Lara has had TR5's bare hands for the
	 * weapons; we never used the gloves - force it, then it is uniform."
	 *
	 * So this answers false for everything and the wardrobe's `gloves` flag is
	 * a value nothing reads: the table's column, `outfit.txt`'s gloves= line
	 * and the default all go on being parsed, printed and logged - they are
	 * what a reader of a wardrobe line sees and taking them out would only
	 * make the line lie about what the file says - and none of them reaches a
	 * model name any more. The one thing that changes on the screen is the
	 * Russia outfit and the mods that inherit its row: JOBY1 to JOBY5 drew
	 * HAND_GLOVES_ until this round.
	 *
	 * The `gloves` column of kPart below is what stops being read, and it
	 * stays there for the same reason: it is the wardrobe's measurement of
	 * which file is which hand's twin, and it is true whether or not anything
	 * asks for it. */
	bool takes_gloves(int)
	{
		return false;
	}

	// What the wardrobe WOULD have said, for the one log line that reports the
	// override (#153, round M2b).
	bool asked_for_gloves(int outfit)
	{
		const HDWardrobeEntry* e = props_of(outfit);

		return e ? e->gloves : false;	// kDefaultGloves
	}

	bool takes_rig(int outfit)
	{
		const HDWardrobeEntry* e = props_of(outfit);

		return e ? e->wearsRig : false;	// kDefaultRig
	}

	/* THE OTHER PARTS. `kind` decides which outfits take the part at all:
	 *   0 always, 1 only an outfit with hands, 2 only one with a braid,
	 *   3 only one with holsters, 4 only the outfits the table names.
	 * `rigid` >= 0 means the file is not skinned to the mesh tree but nailed
	 * to that one node - LARA_HEAD is a facial rig whose bones run to 31 and
	 * mean nothing at all to Lara's 15 node tree.
	 * `nodes` is the coverage measured over 4\ITEM, written down so that the
	 * masks in HDLaraFrameParts can be read against it.
	 * `gloves` is the same file for a gloved outfit, or 0 when the wardrobe
	 * ships no gloved twin. All twelve hand parts have one.
	 *
	 * NOTHING READS IT SINCE #153, ROUND M2b, and that is the user's decision
	 * and not an oversight: the wardrobe wears the BARE hands in every level
	 * with every outfit, so takes_gloves above answers false for everything
	 * and HDLaraPartModel never reaches this column. It stays because it is a
	 * measurement - which HAND_GLOVES_ file is which HAND_BARE_ file's twin -
	 * and a measurement is true whether or not anything asks for it; the day
	 * the decision is revisited, the column is what it needs. */
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

		/* Not a file: a disc built in the neck cut's own plane (#123, fix
		 * round S, S1). The name is what the log and the plan call it. */
		{ "NECK_CAP",                 0,
		  "the neck lid",             0, -1, "14 (rigid)", -1 },

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
		 * kind 4 is "worn by the outfits the table names" - today OUTFIT_VCI
		 * and no other (fix round F, F1).
		 *
		 * The nodes column is the TRIANGLES: 791, 526 and 505 vertices on
		 * nodes 0, 1 and 4. The page also keeps 207 vertices whose dominant
		 * bone is node 14, which no surviving triangle uses - they belong to
		 * triangles the cut dropped because their other corners sit on
		 * another page. HDModelNodeCovered counts vertices, so the coverage
		 * mask has bit 14 set as well; it draws nothing, and it is written
		 * down here so that a reader of the HDLOG part line does not go
		 * looking for a piece of London on Lara's head. */
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
	 * words (host\render\game_renderer.cpp:373). It is not one here. Measured
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
	 * A part not named here takes 0 and does not shine.
	 *
	 * WHAT FIX ROUND I (I4) CHANGED, and why the numbers below did not.
	 *
	 * The user's finding: "the sheen is on the body and the head; the hand
	 * parts go empty". Measured with ModelProbe -onlypart, drawing one part
	 * with the sheen on and off and differencing the two pictures: it is NOT
	 * empty - the highlight is there, on the Desert Eagle's barrel and along
	 * the fingers - it is a LINE. A Blinn-Phong exponent of 34 on a hand-sized
	 * object at arm's length is a highlight one or two pixels wide, and beside
	 * a catsuit whose whole chest lights up it reads as nothing.
	 *
	 * Two things really were wrong, and both are about ONE number having to
	 * describe TWO materials:
	 *
	 *   1. A HAND_ model is a hand AND the thing in it, on two pages of one
	 *      file (measured: HAND_BARE_REST has 1 page, HAND_BARE_DEAGLE 2,
	 *      HAND_GLOVES_REST 3, HAND_GLOVES_DEAGLE 4 - the gun is always the
	 *      page the rest hands have not got). The entries below are the
	 *      WEAPON's material and were being worn by the skin as well. They are
	 *      the weapon's alone now: the draw's own sheen is the HAND's and the
	 *      pages that are not the rest hands' get these numbers through
	 *      HDModelSetPageSheen.
	 *   2. A gloved outfit wears HAND_GLOVES_ and shone like bare skin. The
	 *      hand's material follows the outfit now - skin for a bare hand,
	 *      leather for a gloved one - which is the same rule the BODY has had
	 *      since fix round D.
	 */
	struct PartSheen
	{
		int part;
		float strength;
		float power;
	};

	/* THE HAND'S MATERIAL, out of the table above. There were two of these
	 * until #153 round M2b - skin for a bare hand, leather for a gloved one -
	 * and the gloved one went with the gloves: the wardrobe wears bare hands
	 * everywhere now, so the leather pair had no reader left. Leather is still
	 * the holster rig's material and stands in kPartSheen below. */
	const float kSheenSkin[2] = { 0.12f, 10.0f };

	const PartSheen kPartSheen[] =
	{
		{ HD_LARA_PART_FACE,               0.12f, 10.0f },	// skin
		{ HD_LARA_PART_BRAID,              0.14f, 18.0f },	// hair
		{ HD_LARA_PART_NECK_CAP,           0.12f, 10.0f },	// skin (S1)

		{ HD_LARA_PART_HAND_REST,          0.12f, 10.0f },	// skin
		{ HD_LARA_PART_HAND_PISTOL,        0.35f, 34.0f },	// gunmetal
		{ HD_LARA_PART_HAND_UZI,           0.35f, 34.0f },
		{ HD_LARA_PART_HAND_SHOTGUN,       0.30f, 30.0f },
		{ HD_LARA_PART_HAND_HK,            0.30f, 30.0f },
		{ HD_LARA_PART_HAND_DEAGLE,        0.35f, 34.0f },
		{ HD_LARA_PART_HAND_DEAGLE_LASER,  0.35f, 34.0f },
		{ HD_LARA_PART_HAND_GRAPPLE,       0.30f, 30.0f },
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
	 * also what TROne's TR5 glue picks. LARA_CROSSBOW_LASER is the SAME
	 * gun (#123, fix round O2, user finding): TR5 has no laser sight for
	 * the grappling gun - the W_LASERSIGHT bit is a Tomb Raider IV leftover
	 * that the engine forces onto the weapon at pickup in the VCI levels
	 * (game\newinv2.cpp, CROSSBOW_ITEM: `W_PRESENT | W_LASERSIGHT |
	 * W_AMMO1`; laramisc.cpp does the same at a level start) and the
	 * combine that would clear it is never reachable, so in Lara's hand the
	 * gun is ALWAYS drawn from LARA_CROSSBOW_LASER. Until this round that
	 * slot wore HAND_BARE_CROSSBOW_LASER, Tomb Raider IV's crossbow with a
	 * scope, because the wardrobe ships no grapple-with-sight model - the
	 * play test saw the crossbow in her hand and the grappling gun on her
	 * back and on the floor. Both slots now wear HAND_BARE_GRAPPLE. */
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
		{ "LARA_CROSSBOW_LASER",  HD_LARA_ROLE_HAND,     HD_LARA_PART_HAND_GRAPPLE },	// fix round O2
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
	// #153 round M2b: the mirror fold's bar, the engine's own unless a
	// diagnostic tool has asked for the picture from before the round.
	int g_fold_bar = HD_LARA_HAND_FOLD_BAR;
	// #153 round M2b: the install's hands under a mod's gun, unless a tool has
	// asked for the picture from before the round.
	bool g_hand_twin = true;
	unsigned int g_coverage[HD_LARA_PART_COUNT] = { 0 };
	bool g_has[HD_LARA_PART_COUNT] = { false };
	// #153 round M2b: the install's own hands loaded beside a mod's gun.
	bool g_twin[HD_LARA_PART_COUNT] = { false };
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
		/* The lid hangs off no slot at all - it is cut out of the body's own
		 * neck (#123, fix round S, S1), so it is wanted wherever the body is. */
		useful[HD_LARA_PART_NECK_CAP] = true;

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
		if (!outfit_real(outfit))
			return false;

		if (part < 0 || part >= HD_LARA_PART_COUNT)
			return false;

		if (part == HD_LARA_PART_BODY)
			return true;

		switch (kPart[part].kind)
		{
		case 1: return takes_hands(outfit);
		case 2: return takes_braid(outfit);
		case 3: return takes_holsters(outfit);
		/* The holster rig (#123, fix round D, D4; the rule corrected in fix
		 * round F, F1): NAMED in the wardrobe's table, not derived from the
		 * geometry. Round D asked "has this body a rig of its own?" and gave
		 * the London rig to every body that had not - which put it on the
		 * Antarctica parka and the Nevada vest, where it clips. See
		 * hd_wardrobe.cpp's `wearsRig` for the measurement and for why it is
		 * not the rule. The holsters test stays in front of it so that a table
		 * entry alone can never dress an outfit that draws no holsters at
		 * all. */
		case 4: return takes_holsters(outfit) && takes_rig(outfit);
		default: return true;
		}
	}
}

/* ------------------------------------------------------------- outfits -- */

const char* HDLaraOutfitModel(int outfit)
{
	return model_of(outfit);
}

const char* HDLaraOutfitLabel(int outfit)
{
	const int wear = HDLaraWearOfOutfit(outfit);

	if (wear >= 0)
	{
		const HDWardrobeEntry* e = HDWardrobeAt(wear);

		// The scan's display name, which is what the player picked it by -
		// and its key would be the same word twice in the line.
		return e ? e->name : "?";
	}

	if (outfit < 0 || outfit >= HD_LARA_OUTFIT_COUNT)
		return "?";

	return kOutfit[outfit].label;
}

const char* HDLaraOutfitKey(int outfit)
{
	const int wear = HDLaraWearOfOutfit(outfit);

	if (wear >= 0)
	{
		const HDWardrobeEntry* e = HDWardrobeAt(wear);

		return e ? e->key : "";
	}

	/* One of the nine has a key too - it is a file of the IV-VI install like
	 * any other - and saying so is what lets the player's chosen key and a
	 * level header's data[3] be compared as the same kind of thing. Empty when
	 * the install is not on this machine, in which case nothing was drawn out
	 * of it anyway. */
	const HDWardrobeEntry* e = props_of(outfit);

	return e ? e->key : "";
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

	/* AND ANY ENTRY OF THE SCAN, BY ITS KEY (#153, round M2) - which is the
	 * one spelling that can name all hundred of them, because two of the
	 * installs' files and any number of mods' may share a .TRM stem. Asked
	 * last, so that the eight names above go on meaning the number the level
	 * header writes and nothing about the scan can move them. */
	{
		const int wear = HDWardrobeFind(name);

		if (wear >= 0)
			return HDLaraOutfitOfWear(wear);
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

int HDLaraPartTwinSlot(int part)
{
	if (part < 0 || part >= HD_LARA_PART_COUNT || kPart[part].kind != 1)
		return -1;

	return HD_LARA_TWIN_SLOT_BASE + part;
}

/* THE TWO SIDES OF A HAND FILE. The right arm's two bones against the left
 * arm's two, in that order, because hd_trm breaks an exact tie by the sign of
 * the vertex's x and side `a` is the positive one - measured: the install's
 * HAND_BARE_REST carries 1,932 vertices on bones 9/10 at x 47..77 and 1,932 on
 * 12/13 at x -77..-47, and every hand file of both remasters and of the user's
 * mods is that way round. */
const HDTrmMirrorPair* HDLaraHandMirror(int* pairs)
{
	static const HDTrmMirrorPair kMirror[2] =
	{
		{ (unsigned char)HD_LARA_ROUTARM, (unsigned char)HD_LARA_LOUTARM },
		{ (unsigned char)HD_LARA_RHAND,   (unsigned char)HD_LARA_LHAND }
	};

	if (pairs)
		*pairs = 2;

	return kMirror;
}

void HDLaraHandFoldBar(int perMille)
{
	g_fold_bar = perMille;
}

void HDLaraHandsFromInstall(bool on)
{
	g_hand_twin = on;
}

const char* HDLaraPartModel(int outfit, int part)
{
	if (!outfit_takes(outfit, part))
		return 0;

	if (part == HD_LARA_PART_BODY)
		return model_of(outfit);

	/* THE GLOVED TWIN, AND WHY IT IS NEVER TAKEN (#123, fix round C, user
	 * finding C2; dead by decision since #153, round M2b). A gloved outfit
	 * WORE the HAND_GLOVES_ file. It does not any more: takes_gloves answers
	 * false for every outfit, because the user's decision is that the wardrobe
	 * wears the bare hands in every level with every outfit, so this branch is
	 * never taken and the column below it is never read. The test is kept as
	 * it stands - the field and not the kind, nothing but a hand part has a
	 * twin - so that revisiting the decision is the one line takes_gloves is
	 * and not a rebuild of this function. HDLaraLevelBuild writes one note per
	 * level build when an outfit asked for gloves and got bare hands. */
	if (takes_gloves(outfit) && kPart[part].gloves)
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

/* --------------------------------------------- the inner skin (K1) -- */
/*
 * WHAT CLOSES THE HOLE THE FIRST PERSON CUT OPENS. The draw is in
 * hd_laradraw.cpp and hd_model.cpp; this is the one thing it needs measured -
 * which texel of which page of the outfit is Lara's own throat.
 *
 * Fix rounds I and J built a CAP here instead, a dome of 32 spokes cut to the
 * outfit's collar ring, and the play test sent it back as a flat plate wearing
 * the backpack straps. hd_lara.h carries what it was and why it went.
 */
namespace
{
	/* The level's bind pose, kept from HDLaraPlan so the measurement can find
	 * the two joints. It runs from a plain function pointer with no argument
	 * for it, and threading one through hd_model for a single caller would be
	 * a worse trade than a file-static that is written once per level. */
	float g_neck_bind[HD_LARA_NODES * 3];
	bool g_neck_bind_ok = false;

	int dominant_node(const HDTrmVertex& v)
	{
		int node = v.bone[0];

		if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
			node = v.bone[1];
		else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
			node = v.bone[2];

		return node;
	}

	/* --------------------------------------- her throat (K1) ----------- */
	/* WHICH TEXEL IS LARA'S NECK. It was the one colour every fragment of fix
	 * round K's inner skin wore; that draw is withdrawn (#123, fix round S,
	 * S1(4)) and the LID wears the same texel now, for the same reason - what
	 * closes the top of a neck should be the colour of that neck.
	 *
	 * WHERE IT IS TAKEN: THE PAGE THAT RUNS THE WHOLE HEIGHT OF THE NECK.
	 * The node 7 vertices inside the cut's own cylinder are counted by texture
	 * page, and the page whose vertices span the greatest DEPTH down the neck
	 * axis wins, among those that carry at least HD_LARA_NECK_SKIN_MIN of them.
	 * A throat runs from the head joint to the shoulders because it is the
	 * neck; a collar, a zip and a hairline are BANDS across it.
	 *
	 * TWO SIMPLER RULES WERE TRIED AND ARE WRONG, and the table ModelProbe
	 * -neck prints ("the neck stump BY PAGE") is why:
	 *
	 *   the TOP RING answers 8002, which is Lara's HAIR - the nape hairline
	 *   sits on the torso node from s = -1.6 to 5.6 in every outfit and is the
	 *   topmost thing on it;
	 *   the MAJORITY OF THE STUMP answers 8042 for OUTFIT_RUSSIA (276 vertices
	 *   of coat against 51 of throat) and 8320 for the Antarctica parka (506
	 *   against 75). A coat has more of it than a neck has.
	 *
	 * The span rule answers 8001 for the three game 5 bodies and 8201 for the
	 * four Tomb Raider III guests - in every one of them the same throat
	 * column, 51 to 78 vertices from s = 2.3 to s = 30 - and 8047 for the
	 * diving suit, which has no throat at all and whose stump is the HELMET.
	 * Grey is the honest answer there and the note says so.
	 *
	 * A PLACE INSIDE A PAINTED TRIANGLE, which is fix round J's finding (b)
	 * carried one step further: not the mean of the coordinates (that is a
	 * point in the middle of the atlas page, and the middle of a page is
	 * empty), and not a real VERTEX either (that sits on the EDGE of its
	 * texture island, where the paint stops - measured, it gave the four Tomb
	 * Raider III guests a dark brown). The centroid of the biggest triangle
	 * whose three corners are all on the chosen page is inside the paint by
	 * construction. */
	int g_inner_page = -1;
	unsigned char g_inner_u = 0, g_inner_v = 0;
	int g_inner_ring = 0;		// vertices on the chosen page, for the log
	double g_inner_span = 0.0;	// and how far down the axis they reached

	void measure_neck_skin(const HDTrmModel* source)
	{
		g_inner_page = -1;
		g_inner_ring = 0;
		g_inner_span = 0.0;

		if (!source || !g_neck_bind_ok)
			return;

		const double H[3] =
		{
			g_neck_bind[HD_LARA_HEAD_NODE * 3 + 0],
			g_neck_bind[HD_LARA_HEAD_NODE * 3 + 1],
			g_neck_bind[HD_LARA_HEAD_NODE * 3 + 2]
		};
		const double T[3] =
		{
			g_neck_bind[HD_LARA_TORSO * 3 + 0],
			g_neck_bind[HD_LARA_TORSO * 3 + 1],
			g_neck_bind[HD_LARA_TORSO * 3 + 2]
		};

		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		double len = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];

		if (len < 1.0)
			return;

		len = std::sqrt(len);

		for (int i = 0; i < 3; i++)
			axis[i] /= len;

		/* The stump, by page: how many vertices and how far down they reach.
		 * A wardrobe body wears four or five pages here, so the linear search
		 * over the list is the whole of it. */
		struct Page
		{
			int page;
			int count;
			double lo, hi;
		};

		std::vector<Page> pages;
		std::vector<int> mine;

		for (int i = 0; i < source->vertices; i++)
		{
			const HDTrmVertex& v = source->vertex[i];

			if (dominant_node(v) != HD_LARA_TORSO)
				continue;

			const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
			const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

			if (s < 0.0 || s > HD_LARA_NECK_SKIN_S)
				continue;

			const double off[3] =
			{
				d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
			};
			const double r = std::sqrt(off[0] * off[0] + off[1] * off[1] +
				off[2] * off[2]);

			if (r > HD_LARA_NECK_SKIN_R)
				continue;

			const int page = HDTrmPageOf(source, &v);

			if (page < 0)
				continue;

			size_t at = 0;

			for (; at < pages.size(); at++)
				if (pages[at].page == page)
					break;

			if (at == pages.size())
			{
				Page one = { page, 0, s, s };

				pages.push_back(one);
			}

			pages[at].count++;

			if (s < pages[at].lo)
				pages[at].lo = s;

			if (s > pages[at].hi)
				pages[at].hi = s;

			mine.push_back(i);
		}

		int best = -1;
		double bestSpan = -1.0;

		for (size_t i = 0; i < pages.size(); i++)
		{
			const double span = pages[i].hi - pages[i].lo;

			if (pages[i].count < HD_LARA_NECK_SKIN_MIN || span <= bestSpan)
				continue;

			bestSpan = span;
			best = pages[i].page;
			g_inner_ring = pages[i].count;
		}

		if (best < 0)
			return;

		/* THE MIDDLE OF THE BIGGEST TRIANGLE ON THAT PAGE, and this is the
		 * third rule of the three (fix round J's finding (b), one step
		 * further).
		 *
		 * Round J learned not to use the MEAN of a set of texture
		 * coordinates, because the mean of points scattered over an atlas is
		 * a point in the middle of the page and the middle of a page is
		 * usually empty. The obvious repair - the real VERTEX nearest that
		 * mean - was what fix round K first shipped, and the picture said it
		 * is not enough either: for the four Tomb Raider III guests it landed
		 * on 8201 at 117/169, which reads (63, 38, 21), a dark brown. A
		 * vertex sits on the EDGE of its texture island, where the artist's
		 * paint stops and the atlas's black begins, and a bilinear sample
		 * there is half black.
		 *
		 * The middle of a triangle is not. So the biggest triangle of the
		 * neck whose three corners all sit on the chosen page is taken and
		 * its centroid used: a point strictly inside a painted triangle,
		 * as far from any edge as the mesh allows. `mark` is the lookup that
		 * makes "is this corner one of the neck's own vertices?" a test and
		 * not a search. */
		std::vector<unsigned char> mark((size_t)source->vertices, 0);

		for (size_t i = 0; i < mine.size(); i++)
			if (HDTrmPageOf(source, &source->vertex[mine[i]]) == best)
				mark[(size_t)mine[i]] = 1;

		double widest = -1.0;

		for (int i = 0; i + 2 < source->indices; i += 3)
		{
			const unsigned short a = source->index[i + 0];
			const unsigned short b = source->index[i + 1];
			const unsigned short c = source->index[i + 2];

			if (a >= source->vertices || b >= source->vertices ||
				c >= source->vertices)
				continue;

			if (!mark[a] || !mark[b] || !mark[c])
				continue;

			const HDTrmVertex& va = source->vertex[a];
			const HDTrmVertex& vb = source->vertex[b];
			const HDTrmVertex& vc = source->vertex[c];

			/* Twice the triangle's area in texture space - the cross product
			 * of two of its edges. No square root and no halving: only the
			 * ordering matters. */
			const double e1u = (double)vb.u - va.u, e1v = (double)vb.v - va.v;
			const double e2u = (double)vc.u - va.u, e2v = (double)vc.v - va.v;
			const double area = std::fabs(e1u * e2v - e1v * e2u);

			if (area <= widest)
				continue;

			widest = area;
			g_inner_u = (unsigned char)(((int)va.u + vb.u + vc.u) / 3);
			g_inner_v = (unsigned char)(((int)va.v + vb.v + vc.v) / 3);
		}

		if (widest < 0.0)
			return;	// no triangle of the neck lies wholly on one page

		g_inner_page = best;
		g_inner_span = bestSpan;
	}

	/* ------------------------------------------- the lid (S1, U1) ------ */
	/* WHAT CLOSES THE HOLE THE HEAD CUT LEAVES (#123, fix round S, S1; built
	 * on the RIM since fix round U, U1).
	 *
	 * A FAN OVER THE BODY'S OWN RIM. The cut discards every fragment whose
	 * head weight reaches HD_LARA_HEAD_CUT_WEIGHT; the RIM is the ring of
	 * vertices that do not reach it but share a triangle with one that does,
	 * and that ring is exactly the edge of the hole. This copies it - the
	 * position, the three bones, the three weights and the texture coordinate
	 * of every one of them - sorts it by angle about the neck axis and fills
	 * it in from the middle.
	 *
	 * COPYING THE WEIGHTS IS THE WHOLE FIX. Every lid before this one was
	 * rigid on ONE node while the skin it met was skinned to two, so the two
	 * parted company as she moved and the round after had to chase the gap.
	 * A lid whose edge is skinned by the same sum over the same joints as the
	 * skin it meets lands on the same point in every pose, to the bit, and
	 * there is nothing left to chase.
	 *
	 * IT WEARS THE RIM'S OWN TEXTURE, page and coordinates both, so the lid is
	 * the top of whatever the outfit really has at the neck - skin, coat or
	 * diving suit - and joins it at the seam without a line. The middle wears
	 * the average of the rim.
	 *
	 * ITS NORMAL POINTS HEAD-WARD - up out of the neck, towards the camera
	 * that looks down at it - so it takes the light the top of her shoulders
	 * takes and not the darkness of a face turned away. */
	int g_cap_segments = 0;		// triangles, for the log
	int g_cap_rim = 0;		// rim vertices it was built on
	int g_cap_rings = 0;
	int g_cap_page = -1;
	double g_cap_rlo = 0.0, g_cap_rhi = 0.0;	// the rim off the axis
	double g_cap_slo = 0.0, g_cap_shi = 0.0;	// and down it

	unsigned char pack_normal(double n)
	{
		const double v = n * 128.0 + 127.0;

		return (unsigned char)(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v));
	}

	/* One rim vertex, with the two numbers the sort and the fan need. */
	struct CapRim
	{
		int at;			// into the source model
		double angle;		// about the neck axis
	};

	bool cap_rim_order(const CapRim& a, const CapRim& b)
	{
		return a.angle < b.angle;
	}

	bool build_neck_cap(const HDTrmModel* source, HDTrmModel* out,
		const char** why)
	{
		*why = "";
		g_cap_segments = 0;
		g_cap_rim = 0;
		g_cap_rings = 0;
		g_cap_page = -1;
		g_cap_rlo = g_cap_rhi = g_cap_slo = g_cap_shi = 0.0;

		if (!source || !out || !g_neck_bind_ok)
		{
			*why = "the neck lid has no bind pose to measure against";
			return false;
		}

		/* THE AXIS IS ONLY FOR THE SORT. Where the lid's points ARE comes out
		 * of the model; the axis says in which order to walk round them and
		 * which way is up. */
		const double H[3] =
		{
			g_neck_bind[HD_LARA_HEAD_NODE * 3 + 0],
			g_neck_bind[HD_LARA_HEAD_NODE * 3 + 1],
			g_neck_bind[HD_LARA_HEAD_NODE * 3 + 2]
		};
		const double T[3] =
		{
			g_neck_bind[HD_LARA_TORSO * 3 + 0],
			g_neck_bind[HD_LARA_TORSO * 3 + 1],
			g_neck_bind[HD_LARA_TORSO * 3 + 2]
		};

		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		double len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		if (len < 1.0)
		{
			*why = "the torso and the head joint sit on top of each other";
			return false;
		}

		for (int i = 0; i < 3; i++)
			axis[i] /= len;

		/* Two axes across it. The cross product with the world's z is safe:
		 * the neck runs down y and nothing in a bind pose turns it. */
		double u[3] = { axis[1], -axis[0], 0.0 };
		double uLen = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);

		if (uLen < 1.0e-6)
		{
			*why = "the neck axis lies along the world z";
			return false;
		}

		for (int i = 0; i < 3; i++)
			u[i] /= uLen;

		const double w[3] =
		{
			axis[1] * u[2] - axis[2] * u[1],
			axis[2] * u[0] - axis[0] * u[2],
			axis[0] * u[1] - axis[1] * u[0]
		};

		/* --- who is cut, and who stands on the edge of it ----------------- */
		const int bar = (int)(HD_LARA_HEAD_CUT_WEIGHT * 255.0f + 0.5f);
		std::vector<unsigned char> head((size_t)source->vertices, 0);

		for (int i = 0; i < source->vertices; i++)
		{
			const HDTrmVertex& v = source->vertex[i];
			int sum = 0;

			for (int k = 0; k < 3; k++)
				if (v.bone[k] == HD_LARA_HEAD_NODE)
					sum += v.weight[k];

			head[(size_t)i] = (unsigned char)(sum > 255 ? 255 : sum);
		}

		std::vector<unsigned char> onRim((size_t)source->vertices, 0);

		for (int t = 0; t + 2 < source->indices; t += 3)
		{
			const int tri[3] =
			{
				source->index[t + 0], source->index[t + 1], source->index[t + 2]
			};
			bool cut = false;

			for (int k = 0; k < 3; k++)
				if (tri[k] < source->vertices &&
					(int)head[(size_t)tri[k]] >= bar)
					cut = true;

			if (!cut)
				continue;

			for (int k = 0; k < 3; k++)
				if (tri[k] < source->vertices &&
					(int)head[(size_t)tri[k]] < bar)
					onRim[(size_t)tri[k]] = 1;
		}

		/* --- the ring, sorted round the neck ------------------------------ */
		std::vector<CapRim> ring;
		double mid[3] = { 0.0, 0.0, 0.0 };

		for (int i = 0; i < source->vertices; i++)
		{
			if (!onRim[(size_t)i])
				continue;

			const HDTrmVertex& v = source->vertex[i];
			const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
			const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
			const double off[3] =
			{
				d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
			};
			const double x = off[0] * u[0] + off[1] * u[1] + off[2] * u[2];
			const double y = off[0] * w[0] + off[1] * w[1] + off[2] * w[2];
			const double r = std::sqrt(x * x + y * y);

			/* THE OUTLIER GUARD. Anything this far off the neck axis is not
			 * part of a neck, and a spoke of the fan reaching out to it would
			 * lie across her chest. It never fires on the wardrobe as it
			 * stands - the widest rim vertex over the eight bodies is 27.6 -
			 * and it is there for the ninth. */
			if (r > HD_LARA_NECK_CAP_RMAX)
				continue;

			CapRim one;

			one.at = i;
			one.angle = std::atan2(y, x);
			ring.push_back(one);

			if (!g_cap_rim || r < g_cap_rlo) g_cap_rlo = r;
			if (!g_cap_rim || r > g_cap_rhi) g_cap_rhi = r;
			if (!g_cap_rim || s < g_cap_slo) g_cap_slo = s;
			if (!g_cap_rim || s > g_cap_shi) g_cap_shi = s;

			g_cap_rim++;
			mid[0] += v.x;
			mid[1] += v.y;
			mid[2] += v.z;
		}

		if ((int)ring.size() < 3)
		{
			*why = "no rim at the head cut to build a lid on";
			return false;
		}

		for (int k = 0; k < 3; k++)
			mid[k] /= (double)ring.size();

		std::sort(ring.begin(), ring.end(), &cap_rim_order);

		/* --- which page it wears ------------------------------------------ */
		/* The one most of the rim wears. A .TRM part is one page and one page
		 * only - measured over all 566 files of the remaster's game 5 - so
		 * the lid has to pick one; at the threshold this round takes, all
		 * eight wardrobe bodies have a rim that is on a single page anyway. */
		int page = -1, best = 0;

		for (size_t i = 0; i < ring.size(); i++)
		{
			const int here = HDTrmPageOf(source, &source->vertex[ring[i].at]);
			int count = 0;

			for (size_t k = 0; k < ring.size(); k++)
				if (HDTrmPageOf(source, &source->vertex[ring[k].at]) == here)
					count++;

			if (count > best)
			{
				best = count;
				page = here;
			}
		}

		if (page < 0)
		{
			*why = "the rim of the head cut wears no page at all";
			return false;
		}

		/* --- EVERY SPOKE ITS OWN TEXEL (#123, fix round U, U1b) ----------- */
		/* THE LID INVENTS NO TEXTURE COORDINATE ANYWHERE. Each spoke of the
		 * fan wears the texel of the RIM vertex it hangs from, unchanged, all
		 * the way in - the middle included, which is why the middle is a COPY
		 * PER SPOKE and not one shared vertex. A triangle of the lid therefore
		 * spans two texels that are NEIGHBOURS on her skin, and nothing on it
		 * ever samples a part of the page the neck does not stand on.
		 *
		 * WHAT THAT REPLACES, AND WHY IT HAD TO GO. The first cut of this
		 * round gave the one shared middle the AVERAGE of the rim's texels,
		 * and the play test came back with "a dark point in the middle of the
		 * cap, running dark towards it like a cone" - on the four TR3 guest
		 * bodies only. Measured (ModelProbe -headw, the rim's texels at the
		 * shipping threshold):
		 *
		 *   OUTFIT_CLASSIC   page 8001   u 235..241  v  70..168   average 238/119
		 *   OUTFIT_RUSSIA    page 8042   u 119..125  v  38..255   average 120/150
		 *   OUTFIT_SUBSUIT   page 8047   u  16.. 40  v  48.. 84   average  28/68
		 *   the four TR3      page 8201   u  60..170  v 154..202   average 112/178
		 *
		 * On every TR5 page the neck is ONE NARROW COLUMN six texels wide, and
		 * the average of a column is in the column - skin. On the TR3 guests'
		 * page it is TWO SEPARATE BANDS, one at v 198..202 and one at v
		 * 154..158, and the average lands in the gap BETWEEN them, which is
		 * whatever the page has there. That is the dark point exactly, and the
		 * cone is the interpolation running into it.
		 *
		 * Picking a better single texel would only make the lottery smaller:
		 * the interior of the lid interpolates from each rim texel TOWARDS the
		 * middle's, so a rim at u 60 and a middle at u 112 sweeps the fifty
		 * texels between them whatever is written there. Not inventing one at
		 * all is the only rule with nothing left to measure. */
		int fallbackAt = -1;

		for (size_t i = 0; i < ring.size() && fallbackAt < 0; i++)
			if (HDTrmPageOf(source, &source->vertex[ring[i].at]) == page)
				fallbackAt = (int)i;

		/* --- how many rings ----------------------------------------------- */
		/* Radially, and never round the outline: the outline is the body's own
		 * rim and adding points to it can only spoil the match. What the
		 * rings buy is LIGHT - the HD path lights per vertex, and a fan of one
		 * middle and twenty rim points shades as twenty flat wedges. */
		const int spokes = (int)ring.size();
		int rings = 1;

		while (spokes + (rings - 1) * 2 * spokes < HD_LARA_NECK_CAP_SEGMENTS)
			rings++;

		/* THE MIDDLE IS `spokes` VERTICES AND NOT ONE (#123, fix round U,
		 * U1b). They stand on the same point, carry the same normal and the
		 * same skinning, so they move as one and no crack can open between
		 * them; they differ in the TEXEL alone, each wearing its own spoke's.
		 * The triangle count is unchanged - a fan is a fan - and the seam
		 * between two neighbouring wedges at the exact centre is a POINT of
		 * zero area between two texels that are neighbours on her skin. */
		const int vertices = (rings + 1) * spokes;
		const int triangles = spokes + (rings - 1) * 2 * spokes;

		std::memset(out, 0, sizeof(*out));
		out->version = 2;
		out->parts = 1;
		out->textures = 1;
		out->indices = triangles * 3;
		out->vertices = vertices;
		out->part = new HDTrmPart[1];
		out->texture = new unsigned short[1];
		out->index = new unsigned short[out->indices];
		out->vertex = new HDTrmVertex[vertices];

		std::memset(out->part, 0, sizeof(HDTrmPart));
		out->part[0].opaque = (unsigned int)out->indices;
		out->part[0].opaqueEnd = (unsigned int)out->indices;
		out->part[0].alphaEnd = (unsigned int)out->indices;
		out->texture[0] = (unsigned short)page;

		/* --- the middle, once per spoke ----------------------------------- */
		/* THEIR SKINNING IS THE RIM'S AVERAGE - the same average for all of
		 * them - so they stay one point in every pose and the fan moves as a
		 * piece rather than stretching between a fixed centre and a moving
		 * edge. Two bones are enough for that average: the head, which is all
		 * the rim has of the cut side, and the torso, which is the rest.
		 *
		 * They differ in the TEXEL and in nothing else (#123, fix round U,
		 * U1b): copy number k wears spoke k's own rim texel, so the wedge
		 * between spoke k and spoke k+1 is bounded by two texels that are
		 * neighbours on her skin and samples nothing between them. */
		int meanHead = 0;

		for (size_t i = 0; i < ring.size(); i++)
			meanHead += head[(size_t)ring[i].at];

		meanHead = (int)(meanHead / (int)ring.size());

		if (meanHead > 255)
			meanHead = 255;

		/* --- the rings ------------------------------------------------------ */
		/* Ring `g` runs from the middle (g = 0, the copies above) out to the rim
		 * (g = rings). Its points are the rim's, moved that fraction of the way
		 * in; the SKINNING is the rim's own, unchanged, all the way in, so that
		 * a spoke of the fan follows the rim vertex it belongs to exactly and
		 * the fan cannot shear against the skin. The TEXEL is the rim's own too,
		 * unchanged all the way in, which is what leaves the lid nothing to
		 * invent. */
		for (int g = 0; g <= rings; g++)
		{
			const double f = (double)g / (double)rings;

			for (int k = 0; k < spokes; k++)
			{
				const HDTrmVertex& src = source->vertex[ring[(size_t)k].at];
				HDTrmVertex& v = out->vertex[g * spokes + k];

				std::memset(&v, 0, sizeof(v));

				/* THE OUTERMOST RING IS THE RIM ITSELF, to the bit: it takes
				 * the source vertex's own coordinates rather than a lerp that
				 * would land a rounding away from them. That, and the copied
				 * weights below, are what make the seal exact in every pose -
				 * the lid's edge and the skin's edge are the same input put
				 * through the same sum. */
				if (g == rings)
				{
					v.x = src.x;
					v.y = src.y;
					v.z = src.z;
				}
				else
				{
					v.x = (float)(mid[0] + ((double)src.x - mid[0]) * f);
					v.y = (float)(mid[1] + ((double)src.y - mid[1]) * f);
					v.z = (float)(mid[2] + ((double)src.z - mid[2]) * f);
				}

				v.nx = pack_normal(-axis[0]);
				v.ny = pack_normal(-axis[1]);
				v.nz = pack_normal(-axis[2]);

				v.layer = 1;	// counted from one - the model's only page

				/* A rim vertex on ANOTHER page cannot lend its texel, because
				 * the lid is one page; it borrows the first rim vertex that IS
				 * on the page instead - a real texel of her neck and not an
				 * invented one. At the threshold this round takes there are none
				 * of those in the wardrobe: all eight bodies have a rim on a
				 * single page. */
				const HDTrmVertex& tex = HDTrmPageOf(source, &src) == page ? src
					: source->vertex[ring[(size_t)fallbackAt].at];

				v.u = tex.u;
				v.v = tex.v;

				/* The middle's copies share one skinning; every ring outside it
				 * keeps the rim vertex's own. */
				if (g == 0)
				{
					v.bone[0] = (unsigned char)HD_LARA_TORSO;
					v.bone[1] = (unsigned char)HD_LARA_HEAD_NODE;
					v.bone[2] = (unsigned char)HD_LARA_TORSO;
					v.weight[0] = (unsigned char)(255 - meanHead);
					v.weight[1] = (unsigned char)meanHead;
					v.weight[2] = 0;
				}
				else
				{
					v.bone[0] = src.bone[0];
					v.bone[1] = src.bone[1];
					v.bone[2] = src.bone[2];
					v.weight[0] = src.weight[0];
					v.weight[1] = src.weight[1];
					v.weight[2] = src.weight[2];
				}
			}
		}

		/* --- the triangles --------------------------------------------------- */
		/* The innermost band is a FAN and not a strip: its inner edge is the
		 * `spokes` copies of one point, so a strip would put a degenerate
		 * triangle beside every real one. Wedge k takes middle copy k, which is
		 * the copy wearing its own texel. */
		int at = 0;

		for (int k = 0; k < spokes; k++)
		{
			const int a = spokes + k;
			const int b = spokes + (k + 1) % spokes;

			out->index[at++] = (unsigned short)k;
			out->index[at++] = (unsigned short)a;
			out->index[at++] = (unsigned short)b;
		}

		for (int g = 1; g < rings; g++)
			for (int k = 0; k < spokes; k++)
			{
				const int in0 = g * spokes + k;
				const int in1 = g * spokes + (k + 1) % spokes;
				const int out0 = (g + 1) * spokes + k;
				const int out1 = (g + 1) * spokes + (k + 1) % spokes;

				out->index[at++] = (unsigned short)in0;
				out->index[at++] = (unsigned short)out0;
				out->index[at++] = (unsigned short)out1;

				out->index[at++] = (unsigned short)in0;
				out->index[at++] = (unsigned short)out1;
				out->index[at++] = (unsigned short)in1;
			}

		g_cap_segments = triangles;
		g_cap_rings = rings;
		g_cap_page = page;

		return true;
	}
}
/* ------------------------------------------------------- the level -- */

int HDLaraPlan(const HDLaraLevel* level, HDModelSlot* out, int max)
{
	if (!level || !out || max <= 0)
		return 0;

	if (!outfit_real(level->outfit))
		return 0;

	if (!level->bodyBind || level->bodyNodes < HD_LARA_NODES)
		return 0;

	bool useful[HD_LARA_PART_COUNT];

	plan_mask(level, useful);

	/* The throat measurement and the LID both want this level's own bind pose
	 * and neither is handed one (#123, fix round K, K1; fix round U, U1), so
	 * it is kept here for them - once per level, before anything can call
	 * either. */
	std::memcpy(g_neck_bind, level->bodyBind, sizeof(g_neck_bind));
	g_neck_bind_ok = true;

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

		/* THE MIRRORED WEIGHTS OF A HAND FILE (#153, round M2b). Every part
		 * of kind 1 and no other: those are the files that cover nodes 9, 10,
		 * 12 and 13, and a file drawn once and mirrored is the only place the
		 * two sides can be confused. The bar and the measurement behind it are
		 * in hd_lara.h. */
		if (kPart[part].kind == 1)
		{
			one.mirror = HDLaraHandMirror(&one.mirrors);
			one.mirrorBar = g_fold_bar;

			/* AND THE INSTALL'S OWN HANDS UNDER A MOD'S GUN (#153, round
			 * M2b). Offered for every hand part and taken for almost none:
			 * hd_model loads the twin only when the part's own model came out
			 * of a mod AND that mod paints the hands with a page of another
			 * bundle. The rest hands are what say which of the install's own
			 * pages are skin - every page they have is skin and nothing else,
			 * which is the measurement the held-item rule below rests on. */
			one.twinSlot = g_hand_twin ? HDLaraPartTwinSlot(part) : -1;
			one.twinSkinOf = kPart[HD_LARA_PART_HAND_REST].model;
		}

		/* The BODY is looked at while it is still a file (#123, fix round K,
		 * K1): the inner skin's texel is a place on a page of the outfit, and
		 * after the build there is no page and no vertex left to ask. */
		if (part == HD_LARA_PART_BODY)
		{
			one.measure = &measure_neck_skin;

			/* AND THE SCAN SAYS WHERE IT IS (#153, round M2). The model
			 * resolver could only ever have found the eight: an outfit of the
			 * Tomb Raider I-III bundle sits in its 1\ITEM, which is on no
			 * search path, and a mod's outfit is refused by the resolver on
			 * purpose. The wardrobe has the path and the namespace, so it
			 * hands both over and nothing is searched for twice.
			 *
			 * Nothing happens when this machine has no scan entry - the eight
			 * then resolve by name exactly as they did before this round,
			 * which is what keeps an installation without the I-III bundle
			 * drawing the same Lara it always did. */
			const int at = wear_index_of(level->outfit);
			const HDWardrobeEntry* e = HDWardrobeAt(at);

			if (e)
			{
				one.path = HDWardrobePath(at);
				one.mod = e->mod;
				one.fromTr3 = e->source == HD_WARDROBE_SOURCE_13;
			}
		}

		/* And the LID is cut out of it (#123, fix round S, S1). It stands
		 * after the body in this array - the enum puts it there - which is
		 * what lets hd_model hand the maker the body it has just read. */
		if (part == HD_LARA_PART_NECK_CAP)
		{
			one.synthFrom = HDLaraPartSlot(HD_LARA_PART_BODY);
			one.synth = &build_neck_cap;
		}
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

	if (!outfit_real(level->outfit))
	{
		note("Lara stays classic - no outfit chosen");
		return false;
	}

	/* THE GLOVES, ASKED FOR AND NOT GIVEN (#153, round M2b). One line per
	 * level build, and only when something really asked - so a log with no
	 * such line is a log in which nothing wanted gloves, rather than one in
	 * which the question was never put. */
	if (asked_for_gloves(level->outfit))
		note("%s asks for gloves and wears the bare hands anyway - the "
			"wardrobe wears bare hands everywhere (the user's decision, "
			"round M2b)", HDLaraOutfitLabel(level->outfit));

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

		/* A HAND PART THE WARDROBE HAS NO GLOVED TWIN FOR - none today, and
		 * the line is here so the day there is one it is in the log and not a
		 * surprise in a screenshot. ITS GATE IS NOW WHAT WAS ASKED FOR (#153,
		 * round M2b, the fix round): it used to be takes_gloves, which has
		 * answered false for every outfit since round M2b, so the line could
		 * not fire at all any more. asked_for_gloves is the same question the
		 * line was written to answer - does this body want gloves - and one
		 * that can still be yes, so the seam goes on saying what it said. */
		if (asked_for_gloves(level->outfit) && kPart[part].kind == 1 &&
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

		/* THE INSTALL'S OWN HANDS, IF hd_model LOADED THEM (#153, round M2b).
		 * It decides - see HDModelSlot::twinSlot - and this is where the draw
		 * finds out. Its own note says which pages went which way. */
		if (kPart[part].kind == 1 && HDModelHas(HDLaraPartTwinSlot(part)))
		{
			g_twin[part] = true;

			/* AND IT IS IN EVERY COLUMN OF THE BUDGET, not only in the count
			 * (#153, round M2b, the fix round). The twin is a second model on
			 * the card with its own vertex and index buffer and its own page,
			 * so a line that counted its vertices but not its bytes described
			 * two different sets of models in one breath - about 155 KiB of
			 * buffer for the install's HAND_BARE_UZI under the Uzi mod's gun.
			 * The 24 and the 2 are hd_model's own vertex and index widths,
			 * the same two numbers the part below is measured with. */
			if (stats)
			{
				const int twin = HDLaraPartTwinSlot(part);

				stats->loaded++;
				stats->twins++;
				stats->vertices += HDModelVertices(twin);
				stats->indices += HDModelIndices(twin);
				stats->ranges += HDModelRanges(twin);
				stats->pages += HDModelPageCount(twin);
				stats->vertexBytes += (double)HDModelVertices(twin) * 24.0;
				stats->indexBytes += (double)HDModelIndices(twin) * 2.0;
			}

			note("%s: the install's own %s is drawn beside it for the hands, "
				"cut to its %d skin page%s - the mod's file keeps the gun and "
				"%d page%s of its own", HDLaraPartLabel(part), model,
				HDModelPageCount(HDLaraPartTwinSlot(part)),
				HDModelPageCount(HDLaraPartTwinSlot(part)) == 1 ? "" : "s",
				HDModelPageCount(slot),
				HDModelPageCount(slot) == 1 ? "" : "s");
		}

		/* WHAT THE MIRROR FOLD FOUND IN THIS FILE (#153, round M2b). One line
		 * per hand part that had anything to fold, and silence for one that
		 * had not - which is every file of the remaster's own wardrobe, whose
		 * crossed vertices all sit below the bar. */
		if (kPart[part].kind == 1)
		{
			HDTrmFoldCounts fold;

			if (HDModelFoldCounts(slot, &fold) &&
				(fold.folded || fold.renormed))
				note("%s: %s crossed=%d folded=%d tied=%d renorm=%d - %d of its "
					"vertices name both hands, the worst with %d per mille on "
					"the far one; %d were folded back (bar %d) and %d had "
					"weights that did not sum to 255",
					HDLaraPartLabel(part), model, fold.crossed, fold.folded,
					fold.tied, fold.renormed, fold.crossed, fold.worstPermille,
					fold.folded, g_fold_bar, fold.renormed);
		}

		/* THE INNER SKIN'S ONE TEXEL (#123, fix round K, K1). Measured off
		 * the outfit's own top neck ring while the .TRM was still a file;
		 * this hands it to the draw. A model whose ring could not be found
		 * gets none, and the inner draw then falls back to the plain cull -
		 * which is the picture before this round and not a hole. */
		if (part == HD_LARA_PART_BODY)
		{
			/* AND LARA'S OWN SKIN SHINES LIKE SKIN (#123, fix round K, K2).
			 *
			 * The body draws with ONE material and part of it is her, not her
			 * clothes - so a bare forearm wore the outfit's sheen while the
			 * hand model over it wore the skin's, and the difference between
			 * the two was multiplied by the room's ambient. The pages are a
			 * measured table on the outfit; hd_wardrobe.cpp carries it and the
			 * measurement behind it. Nothing happens for an outfit that shows
			 * no skin.
			 *
			 * AND FOR AN OUTFIT NOBODY HAS LOOKED AT, THE MEASUREMENT ANSWERS
			 * (#153, round M2). The throat measured two paragraphs down is the
			 * page that runs the whole height of the neck, and for seven of
			 * the eight known bodies it IS the first page of the table's row -
			 * checked one by one, the agreement and the two disagreements are
			 * written out in hd_wardrobe.cpp. So a scanned entry the table has
			 * no row for takes it, and one the table does have a row for keeps
			 * the row: the diving suit's empty list is a decision and the
			 * measurement would overrule it with the HELMET. */
			float skinS = 0.0f, skinP = 1.0f;
			int named = 0;

			HDLaraSkinSheen(&skinS, &skinP);

			if (g_inner_page >= 0 &&
				HDWardrobeSkinMeasured(wear_index_of(level->outfit), g_inner_page))
				note("HDLOG wardrobe %s: nobody has named her skin, so the "
					"throat page %d is taken - measured off this file",
					model, g_inner_page);

			const HDWardrobeEntry* props = props_of(level->outfit);

			for (int i = 0; props && i < HD_WARDROBE_SKIN && props->skin[i]; i++)
			{
				const int page = props->skin[i];
				bool there = false;

				/* THE PAGE NUMBER, NOT THE CACHE KEY (#153, round M2). A model
				 * a mod supplied names its pages in that mod's own namespace
				 * (hd_pagecache.h), so HDModelPage gives back raw + pageBias
				 * and a plain 8001 would match nothing at all - and the skin
				 * would silently keep the body's material, which is the very
				 * seam fix round K was about. The namespace is an allocation
				 * and the page number is the material's name; the held-item
				 * rule below strips it for the same reason and in the same
				 * words. */
				for (int j = 0; j < HDModelPageCount(slot); j++)
				{
					const int key = HDModelPage(slot, j);

					if (page_number_of(key) == page)
					{
						there = true;
						HDModelSetPageSheen(slot, key, skinS, skinP);
						named++;
						break;
					}
				}

				if (!there)
					note("%s: page %d is named as her skin and the model does "
						"not wear it - the table and the art disagree", model,
						page);
			}

			if (named)
				note("her skin: %d page%s of %s %s the skin material %.2f/"
					"%.0f, the one the hands have - so the wrist seam is one "
					"material and not two", named, named == 1 ? "" : "s",
					model, named == 1 ? "takes" : "take", skinS, skinP);

			/* The throat texel (#123, fix round K, K1). It closed the neck
			 * hole from the inside until fix round S and was the lid's one
			 * colour until fix round U; the lid wears the rim's own texture
			 * now, and this stays as the line that says WHICH page of an
			 * outfit is her throat - which is how a lid that came out on the
			 * coat can be told from one that came out wrong. */
			if (g_inner_page >= 0)
				note("her throat: page %d at %d/%d, off the neck's "
					"own column (%d vertices spanning %.0f down the neck "
					"axis)", g_inner_page,
					(int)g_inner_u, (int)g_inner_v,
					g_inner_ring, g_inner_span);
			else
				note("her throat: no neck ring found in %s - the lid does not "
					"depend on it, but nothing here says what her skin is",
					model);
		}

		/* THE LID, IN NUMBERS (#123, fix round S, S1; the rim's own since fix
		 * round U, U1). How many vertices the cut left standing on the edge of
		 * its hole, where they are - off the neck axis and down it - and what
		 * the fan built on them came to. A rim of three or four, or one whose
		 * radius runs to the guard at 34, is a lid that has found something
		 * that is not a neck. */
		if (part == HD_LARA_PART_NECK_CAP && g_cap_segments > 0)
			note("the neck lid: %d rim vertices at r %.1f..%.1f, s %.1f..%.1f, "
				"cut at head weight %.2f - %d triangles in %d ring%s on page "
				"%d", g_cap_rim, g_cap_rlo, g_cap_rhi, g_cap_slo, g_cap_shi,
				(double)HD_LARA_HEAD_CUT_WEIGHT, g_cap_segments, g_cap_rings,
				g_cap_rings == 1 ? "" : "s", g_cap_page);

		/* WHAT IS IN THE HAND GETS ITS OWN MATERIAL (#123, fix round I, I4).
		 *
		 * The rule is a measurement and not a guess: HAND_BARE_REST is the
		 * hands and NOTHING else, so every page a weapon-hand model has that
		 * the rest hands have not got is the weapon. Measured over the
		 * wardrobe: HAND_BARE_REST 1 page, HAND_BARE_DEAGLE 2,
		 * HAND_GLOVES_REST 3, HAND_GLOVES_DEAGLE 4 - one extra page every
		 * time, whichever gun and whether or not she wears gloves.
		 *
		 * The draw's own sheen stays the HAND's, so a page that is not named
		 * here keeps it. */
		if (kPart[part].kind == 1 && part != HD_LARA_PART_HAND_REST)
		{
			const int rest = HDLaraPartSlot(HD_LARA_PART_HAND_REST);
			float held = 0.0f, heldPower = 1.0f;
			int named = 0;

			HDLaraHeldSheen(part, &held, &heldPower);

			/* THE PAGE NUMBER, NOT THE CACHE KEY (#153, round M1; the strip
			 * itself moved into page_number_of in round M2). A model a mod
			 * supplied names its pages in that mod's own namespace
			 * (hd_pagecache.h), so mod2's 8001 and the install's 8001 are two
			 * different keys - and this rule would then find NO page in
			 * common between a modded weapon hand and the vanilla rest hands
			 * and hand the skin the gunmetal material along with the gun.
			 *
			 * A mod that keeps the install's numbering (the HK USP and the
			 * Spas-12 both wear its 8001 for skin) is read exactly right, and
			 * a mod that renumbers is read right as soon as it ships its own
			 * HAND_BARE_REST - the two are then in one namespace again. */
			for (int i = 0; held > 0.0f && i < HDModelPageCount(slot); i++)
			{
				const int page = HDModelPage(slot, i);
				const int number = page_number_of(page);
				bool onHand = false;

				for (int j = 0; j < HDModelPageCount(rest); j++)
					if (page_number_of(HDModelPage(rest, j)) == number)
					{
						onHand = true;
						break;
					}

				if (onHand)
					continue;

				HDModelSetPageSheen(slot, page, held, heldPower);
				named++;
			}

			if (!named && HDModelPageCount(rest))
				note("%s: every page of it is a page of the rest hands - what "
					"it holds keeps the hand's own sheen",
					HDLaraPartLabel(part));
		}

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
			model_of(level->outfit));
		return false;
	}

	g_active = true;
	return true;
}

void HDLaraLevelEnd()
{
	g_outfit = HD_LARA_OUTFIT_NONE;
	g_active = false;
	g_notes.clear();

	// #153 round M2b: the wardrobe entry props_of remembers. Dropped here and
	// not in props_of, so that a re-scan between levels cannot be answered out
	// of the old list.
	g_props_outfit = HD_LARA_OUTFIT_NONE;
	g_props = 0;

	for (int i = 0; i < HD_LARA_PART_COUNT; i++)
	{
		g_has[i] = false;
		g_twin[i] = false;
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

bool HDLaraHasPartTwin(int part)
{
	return part >= 0 && part < HD_LARA_PART_COUNT && g_twin[part];
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
		const HDWardrobeEntry* props = props_of(g_outfit);

		if (!props)
			return;

		if (strength)
			*strength = props->sheen;

		if (power)
			*power = props->sheenPower;

		return;
	}

	/* AND A HAND PART'S SHEEN IS THE HAND'S, WHICH ALSO BELONGS TO THE OUTFIT
	 * (#123, fix round I, I4). What is in the hand is a page of the same file
	 * and gets its own material through HDModelSetPageSheen; this is the skin
	 * or the glove around it, and until this round a gloved Lara's leather
	 * shone like bare skin. */
	if (kPart[part].kind == 1)
	{
		/* AND IT IS ALWAYS THE SKIN'S (#153, round M2b). takes_gloves answers
		 * false for every outfit now, so the leather branch this line used to
		 * have could never be taken, and the pair it read went with it.
		 * Leather is still the holster rig's material and stands as 0.18/20
		 * in kPartSheen below. */
		if (strength)
			*strength = kSheenSkin[0];

		if (power)
			*power = kSheenSkin[1];

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

/* LARA'S SKIN (#123, fix round K, K1), straight out of the material table at
 * the top of this file. The inner skin draw is her own throat's colour, so it
 * shines like skin whatever the outfit over it is made of. */
void HDLaraSkinSheen(float* strength, float* power)
{
	if (strength)
		*strength = kSheenSkin[0];

	if (power)
		*power = kSheenSkin[1];
}

bool HDLaraSkinPage(int page)
{
	const HDWardrobeEntry* props = props_of(g_outfit);

	if (page < 0 || !props)
		return false;

	/* The caller hands a cache key (ModelProbe passes HDModelPage straight
	 * in), so the namespace comes off first - see page_number_of. */
	const int number = page_number_of(page);

	for (int i = 0; i < HD_WARDROBE_SKIN; i++)
		if (props->skin[i] == number)
			return true;

	return false;
}

/* WHAT IS IN THE HAND, as opposed to the hand (#123, fix round I, I4): the
 * kPartSheen entry, which is the weapon's own material. Both 0 for a part that
 * holds nothing. */
void HDLaraHeldSheen(int part, float* strength, float* power)
{
	if (strength)
		*strength = 0.0f;

	if (power)
		*power = 1.0f;

	if (part < 0 || part >= HD_LARA_PART_COUNT || kPart[part].kind != 1)
		return;

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

/* THE HEAD CUT, IN THE TWO NUMBERS THE SHADER NEEDS (#123, fix round U, U1).
 *
 * One function, so that what the shader discards, what the lid is built on and
 * what the diagnostic tool draws cannot drift apart: all three ask here.
 *
 * There is nothing to work out. Fix round S's plane had to be measured against
 * this level's own bind pose - the neck axis and a depth down it - and every
 * shape before it had to as well; a bone weight is written in the .TRM and is
 * the same number in every pose, at every camera angle and in every level. So
 * the only question left is whether there is an HD body to cut at all. */
bool HDLaraHeadCut(int* node, float* bar)
{
	if (!node || !bar || !g_active)
		return false;

	if (!HDLaraHasPart(HD_LARA_PART_BODY))
		return false;

	*node = HD_LARA_HEAD_NODE;
	*bar = HD_LARA_HEAD_CUT_WEIGHT;

	return true;
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
			{ HD_LARA_PART_HAND_GRAPPLE,       HD_LARA_PART_BACK_GRAPPLE }
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

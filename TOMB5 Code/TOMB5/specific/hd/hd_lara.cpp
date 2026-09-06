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

#include <cmath>
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
		/* Does this outfit wear the separate HOLSTER RIG - the belt, the two
		 * pouches and the two thigh straps cut out of the London catsuit by
		 * page (#123, fix round D, D4; the rule corrected in fix round F,
		 * F1)?
		 *
		 * THE MEASUREMENT ROUND D MADE ITS RULE. Triangles on nodes 1 and 4
		 * by texture page, over the eight wardrobe bodies - does the body
		 * carry a rig of its OWN?
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
		 * AND THE RULE WAS WRONG ON THE PICTURE. "No rig of its own, so wear
		 * the London one" put black leather straps over the Antarctica
		 * parka: the belt disappears inside the padded jacket, the two
		 * pouches stand off the hip at the width the London catsuit's thin
		 * thighs want, and the straps cut through the coat (user finding F1
		 * of the play test of 2026-09-06 - "that may only concern the VCI
		 * catsuit"). Nevada is the same body shape and the same picture.
		 *
		 * A BODY WITHOUT A RIG IS NOT A BODY ASKING FOR ONE. The three are
		 * without one for two different reasons. OUTFIT_VCI has none because
		 * vanilla TR5 hangs pistols on that catsuit and the remaster left the
		 * pouches to the holster file, so its thighs are bare where guns
		 * belong. Antarctica and Nevada have none because in TR3 those two
		 * outfits never wore a thigh rig at all: the geometry is finished as
		 * it stands, and coat and cargo pocket are modelled to sit flush.
		 * Only the first of the three is a hole; the other two are a design.
		 *
		 * So this is a TABLE and not a rule derived from geometry - one
		 * outfit, the one whose picture the user judged. A wardrobe body
		 * added later gets a false here, and a true only after somebody has
		 * looked at the picture. */
		bool wearsRig;

		/* THE BODY'S SHEEN (#123, fix round D, D2): the Blinn-Phong strength
		 * and exponent for the OUTFIT model. See kPartSheen below for the
		 * other parts and for why these are a material table and not a
		 * measurement. */
		float sheen;
		float sheenPower;
	};

	/*                                                    hands  braid holsters
	 *                                                                  gloves
	 *                                                                wearsRig */
	const Outfit kOutfit[HD_LARA_OUTFIT_COUNT] =
	{
		{ 0,                    "classic (no HD body)",   false, false, false, false, false, 0.00f,  1.0f },
		{ "OUTFIT_CLASSIC",     "the Rome outfit",        true,  true,  true,  false, false, 0.10f, 14.0f },
		{ "OUTFIT_RUSSIA",      "the Russia outfit",      true,  true,  true,  true,  false, 0.12f, 16.0f },
		{ "OUTFIT_SUBSUIT",     "the diving suit",        false, false, false, false, false, 0.30f, 22.0f },
		{ "OUTFIT_VCI",         "the VCI catsuit",        true,  true,  true,  false, true,  0.45f, 26.0f },
		{ "OUTFIT_TR3_ANTARC",  "Antarctica (TR3)",       true,  true,  true,  false, false, 0.10f, 14.0f },
		{ "OUTFIT_TR3_NEVADA",  "Nevada (TR3)",           true,  true,  true,  false, false, 0.08f, 12.0f },
		{ "OUTFIT_TR3_COAST",   "Jungle (TR3)",           true,  true,  true,  false, false, 0.08f, 12.0f },
		{ "OUTFIT_TR3_CATSUIT", "London (TR3)",           true,  true,  true,  false, false, 0.45f, 26.0f }
	};

	/* THE OTHER PARTS. `kind` decides which outfits take the part at all:
	 *   0 always, 1 only an outfit with hands, 2 only one with a braid,
	 *   3 only one with holsters, 4 only the outfits the table names.
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

		/* Not a file: built out of the outfit's own collar (#123, fix round I,
		 * I3). The name is what the log and the plan call it. */
		{ "NECK_CAP",                 0,
		  "the neck cap",             0, -1, "7", -1 },

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

	/* The two hand materials, out of the table above. */
	const float kSheenSkin[2] = { 0.12f, 10.0f };
	const float kSheenLeather[2] = { 0.18f, 20.0f };

	const PartSheen kPartSheen[] =
	{
		{ HD_LARA_PART_FACE,               0.12f, 10.0f },	// skin
		{ HD_LARA_PART_BRAID,              0.14f, 18.0f },	// hair
		{ HD_LARA_PART_NECK_CAP,           0.12f, 10.0f },	// skin (I3)

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
		/* The neck cap hangs off no slot at all - it is cut from the body
		 * (#123, fix round I, I3), so it is wanted wherever the body is. */
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
		/* The holster rig (#123, fix round D, D4; the rule corrected in fix
		 * round F, F1): NAMED in the outfit table, not derived from the
		 * geometry. Round D asked "has this body a rig of its own?" and gave
		 * the London rig to every body that had not - which put it on the
		 * Antarctica parka and the Nevada vest, where it clips. See wearsRig
		 * above for the measurement and for why it is not the rule. The
		 * holsters test stays in front of it so that a table entry alone can
		 * never dress an outfit that draws no holsters at all. */
		case 4: return kOutfit[outfit].holsters && kOutfit[outfit].wearsRig;
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

/* ------------------------------------------------ the neck cap (I3) -- */
/*
 * THE LID THAT CLOSES THE NECK IN FIRST PERSON. What it is for and where its
 * numbers come from is at HD_LARA_NECK_CAP_R in hd_lara.h; this is the maker.
 *
 * IT IS BUILT FROM THE OUTFIT'S OWN BODY, which is why it goes through
 * HDModelSlot::synth rather than being a file: the rim is the outfit's own
 * vertices, at their own positions, with their own texture page and their own
 * texture coordinates. Nothing is invented but the surface between them.
 *
 * THE STEPS.
 *   1. The neck AXIS: the line from the torso joint to the head joint, both
 *      out of the level's own bind pose. `s` is the distance down it from the
 *      head joint, `r` the distance off it. It is the SAME axis the cut is
 *      built about (hd_model.hlsl, gNeck), which is why the two fit.
 *   2. THE RING: of the node 7 vertices that SURVIVE the cut, the one nearest
 *      the cut's own edge in each of SPOKES sectors. The cut's edge is where
 *      the neck stops being cut away, so that ring IS the mouth of the hole -
 *      the body's own vertices, at their own positions, all round it.
 *      (Fix round I took "the vertex furthest off the axis" in a band at
 *      s = 50 instead, and at that depth the body is a chest: the rim came
 *      out the outline of her whole torso. See hd_lara.h for the three
 *      things in the picture that followed from it.)
 *   3. THE PAGE: the one most of those vertices wear. A model draws one page
 *      per call, so a rim vertex on another page takes the texture
 *      coordinates of the nearest rim vertex that is on the chosen one - which
 *      is a colour and not a position, and the count goes in the log.
 *   4. THE DOME: RINGS rings from the rim inwards, each ring its rim vertex's
 *      own offset scaled by a factor that is NEVER above one, so no part of
 *      the cap can stand further off the axis than the skin it hangs from,
 *      and the middle sunk SINK units further down. The texture coordinates
 *      run from the rim's own to ONE rim vertex's - the middlemost of them on
 *      the page - so the seam is exact where it is seen and the middle wears
 *      a texel the artist really painted.
 *   5. THE NORMALS are accumulated from the triangles and normalised - the
 *      "soft normals" a curved surface wants, and the reason the cap shades
 *      like a body and not like a plate.
 */
namespace
{
	/* The level's bind pose, kept from HDLaraPlan so the maker can find the
	 * two joints. The maker is a plain function pointer with no argument for
	 * it, and threading one through hd_model for a single caller would be a
	 * worse trade than a file-static that is written once per level. */
	float g_neck_bind[HD_LARA_NODES * 3];
	bool g_neck_bind_ok = false;
	int g_neck_cap_page = -1;	// for the log
	int g_neck_cap_rehomed = 0;	// rim vertices that had to borrow a UV
	/* THE RIM, IN NUMBERS (#123, fix round J, J1). The whole of the user's
	 * finding is "the cap is too big and it pokes out", so the log carries the
	 * ring the cap was built on - how far off the axis and how far down it -
	 * and a self-check: how many of the dome's own vertices stand further off
	 * the axis than the rim vertex of their own spoke. That last number must
	 * be 0, and the clamp in the maker is what makes it 0. */
	double g_neck_cap_rim[6] = { 0, 0, 0, 0, 0, 0 };	// r min/mean/max, s ditto
	int g_neck_cap_outside = 0;

	int dominant_node(const HDTrmVertex& v)
	{
		int node = v.bone[0];

		if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
			node = v.bone[1];
		else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
			node = v.bone[2];

		return node;
	}

	unsigned char pack_normal(double n)
	{
		const double v = n * 128.0 + 127.0;

		return (unsigned char)(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v));
	}

	bool build_neck_cap(const HDTrmModel* source, HDTrmModel* out,
		const char** why)
	{
		const int kSpokes = HD_LARA_NECK_CAP_SPOKES;
		const int kRings = HD_LARA_NECK_CAP_RINGS;

		*why = "";
		g_neck_cap_page = -1;
		g_neck_cap_rehomed = 0;
		g_neck_cap_outside = 0;

		for (int i = 0; i < 6; i++)
			g_neck_cap_rim[i] = 0.0;

		if (!source || !out || !g_neck_bind_ok)
		{
			*why = "the neck cap has no bind pose to measure against";
			return false;
		}

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
		{
			*why = "the torso and the head joint sit on top of each other";
			return false;
		}

		len = std::sqrt(len);

		for (int i = 0; i < 3; i++)
			axis[i] /= len;

		/* Two axes across it, for the sectors. The cross product with the
		 * world's z is safe: the neck runs down y and nothing turns it. */
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

		/* --- the ring ---------------------------------------------------- */
		/* THE CANDIDATES ARE THE VERTICES THE CUT LEAVES STANDING, near its
		 * own edge. `keep` is the shader's own number - how far outside the
		 * cylinder a point is, which is the larger of "past its bottom" and
		 * "past its side" - so a candidate with keep >= 0 survives the cut and
		 * the smallest keep in a sector is the vertex closest to the mouth of
		 * the hole. That is the rim: the body's own geometry, at the one place
		 * the cap has to meet it.
		 *
		 * SMAX keeps the search in the neck. Without it a sector with no
		 * vertex near the mouth would answer with something on the hip, which
		 * is a smaller `keep` than nothing at all.
		 *
		 * A few hundred candidates, one pass per spoke over them: a few
		 * thousand comparisons at level load and nothing per frame. */
		std::vector<int> band;
		std::vector<double> bandKeep;
		std::vector<double> bandA;

		for (int i = 0; i < source->vertices; i++)
		{
			const HDTrmVertex& v = source->vertex[i];

			if (dominant_node(v) != HD_LARA_TORSO)
				continue;

			const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
			const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

			if (s < 0.0 || s > HD_LARA_NECK_CAP_SMAX)
				continue;

			const double off[3] =
			{
				d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
			};
			const double r = std::sqrt(off[0] * off[0] + off[1] * off[1] +
				off[2] * off[2]);
			const double byBottom = s - HD_LARA_NECK_CUT_S;
			const double bySide = r - HD_LARA_NECK_CAP_R;
			const double keep = byBottom > bySide ? byBottom : bySide;

			if (keep < 0.0)
				continue;	// the cut takes this one - it is not a rim

			band.push_back(i);
			bandKeep.push_back(keep);
			bandA.push_back(std::atan2(
				off[0] * w[0] + off[1] * w[1] + off[2] * w[2],
				off[0] * u[0] + off[1] * u[1] + off[2] * u[2]));
		}

		if ((int)band.size() < kSpokes)
		{
			*why = "the mouth of the neck holds fewer vertices than the cap "
				"has spokes";
			return false;
		}

		/* ONE VERTEX PER SPOKE: the one nearest the cut's edge within half a
		 * sector of the spoke's own direction. A sector with nothing in it is
		 * not an error and must not be - a mesh is not evenly tessellated -
		 * so the window widens until it finds one. It is written as a widening
		 * window rather than as "the nearest in that direction over the whole
		 * ring", which would let one very good vertex answer for three
		 * sectors and pinch the rim into a triangle. */
		const double kPi = 3.14159265358979323846;
		int pick[HD_LARA_NECK_CAP_SPOKES];

		for (int k = 0; k < kSpokes; k++)
		{
			const double want = -kPi + (k + 0.5) * 2.0 * kPi / kSpokes;
			double window = kPi / kSpokes;

			pick[k] = -1;

			for (int tries = 0; tries < 6 && pick[k] < 0; tries++, window *= 2.0)
			{
				double bestKeep = 1.0e30;

				for (size_t i = 0; i < band.size(); i++)
				{
					double da = bandA[i] - want;

					while (da > kPi)
						da -= 2.0 * kPi;

					while (da < -kPi)
						da += 2.0 * kPi;

					if (da < -window || da > window)
						continue;

					if (bandKeep[i] < bestKeep)
					{
						bestKeep = bandKeep[i];
						pick[k] = band[i];
					}
				}
			}

			if (pick[k] < 0)
			{
				*why = "the neck's mouth has a gap no widening would close";
				return false;
			}
		}

		/* --- the page most of the ring wears ------------------------------ */
		int best = -1, bestCount = 0;

		for (int k = 0; k < kSpokes; k++)
		{
			const int page = HDTrmPageOf(source, &source->vertex[pick[k]]);
			int count = 0;

			if (page < 0)
				continue;

			for (int j = 0; j < kSpokes; j++)
				if (HDTrmPageOf(source, &source->vertex[pick[j]]) == page)
					count++;

			if (count > bestCount)
			{
				bestCount = count;
				best = page;
			}
		}

		if (best < 0)
		{
			*why = "no ring vertex names a texture page";
			return false;
		}

		g_neck_cap_page = best;

		/* A rim vertex on another page keeps its POSITION and borrows the
		 * nearest same-page rim vertex's texture coordinates. */
		unsigned char rimU[HD_LARA_NECK_CAP_SPOKES];
		unsigned char rimV[HD_LARA_NECK_CAP_SPOKES];

		for (int k = 0; k < kSpokes; k++)
		{
			const HDTrmVertex& v = source->vertex[pick[k]];

			if (HDTrmPageOf(source, &v) == best)
			{
				rimU[k] = v.u;
				rimV[k] = v.v;
				continue;
			}

			g_neck_cap_rehomed++;

			double near2 = 1.0e30;
			int from = -1;

			for (int j = 0; j < kSpokes; j++)
			{
				const HDTrmVertex& c = source->vertex[pick[j]];

				if (HDTrmPageOf(source, &c) != best)
					continue;

				const double dx = c.x - v.x, dy = c.y - v.y, dz = c.z - v.z;
				const double d2 = dx * dx + dy * dy + dz * dz;

				if (d2 < near2)
				{
					near2 = d2;
					from = j;
				}
			}

			rimU[k] = source->vertex[pick[from]].u;
			rimV[k] = source->vertex[pick[from]].v;
		}

		/* WHERE THE MIDDLE OF THE CAP SAMPLES ITS COLOUR (#123, fix round J,
		 * J1(b)).
		 *
		 * Fix round I converged the inner rings on the AVERAGE of the 32 rim
		 * coordinates, and the user's picture is what that gives: a black
		 * smear across the middle of the cap. An atlas page is not a colour
		 * ramp - the rim's coordinates are scattered over it, their average is
		 * a point in the middle of the page, and the middle of page 8310 is
		 * empty. The average of a set of coordinates is a coordinate; it is
		 * not the average of their colours and there is no reason it should
		 * even be painted.
		 *
		 * So the middle wears ONE RIM VERTEX'S coordinates instead - the one
		 * whose coordinates lie nearest all the others, which is a real place
		 * on the page with real paint on it, and the nearest thing to "the
		 * middle of them" that is guaranteed to exist. The seam is still
		 * exact, because the RIM keeps its own coordinates either way. */
		int home = 0;
		double homeCost = 1.0e30;

		for (int k = 0; k < kSpokes; k++)
		{
			double cost = 0.0;

			for (int j = 0; j < kSpokes; j++)
			{
				const double du = (double)rimU[k] - (double)rimU[j];
				const double dv = (double)rimV[k] - (double)rimV[j];

				cost += std::sqrt(du * du + dv * dv);
			}

			if (cost < homeCost)
			{
				homeCost = cost;
				home = k;
			}
		}

		const double meanU = rimU[home];
		const double meanV = rimV[home];

		/* --- the dome ----------------------------------------------------- */
		const int vertices = kSpokes * kRings + 1;
		const int triangles = kSpokes * (2 * (kRings - 1) + 1);

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
		out->texture[0] = (unsigned short)best;

		/* The rim, decomposed once: how far down the axis each spoke's vertex
		 * sits and how far off it, and the two means. The means are what the
		 * dome rounds TOWARDS - the rim itself is the outfit's own vertices,
		 * warts and all, and a dome built straight off them would carry every
		 * dent of the collar all the way to its middle. So the further in a
		 * ring is, the more it is a circle: exact at the seam, where it has to
		 * be, and smooth where it is looked at. */
		double sMean = 0, rMean = 0;
		double rimS[HD_LARA_NECK_CAP_SPOKES];
		double rimOff[HD_LARA_NECK_CAP_SPOKES][3];
		double rimR[HD_LARA_NECK_CAP_SPOKES];

		for (int k = 0; k < kSpokes; k++)
		{
			const HDTrmVertex& v = source->vertex[pick[k]];
			const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };

			rimS[k] = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

			for (int a = 0; a < 3; a++)
				rimOff[k][a] = d[a] - rimS[k] * axis[a];

			rimR[k] = std::sqrt(rimOff[k][0] * rimOff[k][0] +
				rimOff[k][1] * rimOff[k][1] + rimOff[k][2] * rimOff[k][2]);

			/* AND ONTO THE CUT'S OWN EDGE (#123, fix round J, J1(c)).
			 *
			 * The hole the cap closes is bounded by the cut, and the cut's
			 * boundary is never deeper than S nor further off the axis than R
			 * - that is what the two numbers MEAN. So a rim vertex past either
			 * is a sector in which the body's surface never crosses the cut at
			 * all: the mouth of a neck is not a circle, and where it opens
			 * into a shoulder the nearest vertex outside the cylinder can be
			 * fifty units off the axis. Left alone it drags one spoke of the
			 * cap out over the collarbone and the two strips beside it become
			 * the slivers the user calls "random polygons".
			 *
			 * Pulled back onto the boundary it is still inside her - the
			 * body's own surface in that sector is further out, and still
			 * drawn - and the ring comes back round. Where the body DOES cross
			 * the cut, which is all round the neck itself, the clamp does
			 * nothing and the rim is the outfit's own vertex, unmoved. */
			if (rimS[k] > HD_LARA_NECK_CUT_S)
				rimS[k] = HD_LARA_NECK_CUT_S;

			/* AND NOT HUNG FROM THE TOP OF THE CYLINDER EITHER. A sector whose
			 * surface crosses the cylinder's SIDE high up - the diving suit's
			 * helmet does, at s = 0 - would hang the lid from there and make a
			 * funnel of it instead of a lid, with the seam torn open where the
			 * two ends of the funnel meet. The cut's boundary in such a sector
			 * runs from that crossing all the way DOWN the side to the bottom,
			 * so the bottom end of it is just as much the edge of the hole and
			 * is the end a lid belongs on. LIP is how far above the bottom a
			 * rim vertex may still sit: it is 8 because the seven wardrobe
			 * bodies with a neck read s = 27.8 to 30 and never need it, and
			 * the diving suit reads 0.2 to 30 and needs all of it. */
			if (rimS[k] < HD_LARA_NECK_CUT_S - HD_LARA_NECK_CAP_LIP)
				rimS[k] = HD_LARA_NECK_CUT_S - HD_LARA_NECK_CAP_LIP;

			if (rimR[k] > HD_LARA_NECK_CAP_R)
			{
				const double pull = HD_LARA_NECK_CAP_R / rimR[k];

				for (int a = 0; a < 3; a++)
					rimOff[k][a] *= pull;

				rimR[k] = HD_LARA_NECK_CAP_R;
			}

			sMean += rimS[k];
			rMean += rimR[k];
		}

		sMean /= kSpokes;
		rMean /= kSpokes;

		/* The ring, for the log (#123, fix round J, J1). */
		g_neck_cap_rim[0] = g_neck_cap_rim[2] = rimR[0];
		g_neck_cap_rim[3] = g_neck_cap_rim[5] = rimS[0];
		g_neck_cap_rim[1] = rMean;
		g_neck_cap_rim[4] = sMean;

		for (int k = 0; k < kSpokes; k++)
		{
			if (rimR[k] < g_neck_cap_rim[0]) g_neck_cap_rim[0] = rimR[k];
			if (rimR[k] > g_neck_cap_rim[2]) g_neck_cap_rim[2] = rimR[k];
			if (rimS[k] < g_neck_cap_rim[3]) g_neck_cap_rim[3] = rimS[k];
			if (rimS[k] > g_neck_cap_rim[5]) g_neck_cap_rim[5] = rimS[k];
		}

		const double kHalfPi = 1.57079632679489661923;

		for (int j = 0; j < kRings; j++)
		{
			const double f = (double)j / (double)kRings;
			const double rad = std::cos(f * kHalfPi);
			const double sink = HD_LARA_NECK_CAP_SINK * std::sin(f * kHalfPi);

			for (int k = 0; k < kSpokes; k++)
			{
				HDTrmVertex& v = out->vertex[j * kSpokes + k];
				const double at = rimS[k] * (1.0 - f) + sMean * f + sink;
				/* AND NEVER MORE THAN ONE (#123, fix round J, J1(a)). The
				 * rounding pulls a wide spoke IN towards the ring's mean and
				 * must never push a narrow one OUT: `round` above one is a
				 * ring standing further off the axis than the body's own skin
				 * at that spoke, which is the flat plate the user saw poking
				 * through her collar. Round I had no such clamp, and with a
				 * rim that ran from 20 to 82 units the narrow spokes were
				 * pushed out by a quarter of their own radius. */
				double round = rimR[k] > 1.0e-6
					? (1.0 - f) + f * rMean / rimR[k] : 1.0;

				if (round > 1.0)
					round = 1.0;

				const double off[3] =
				{
					rimOff[k][0] * round, rimOff[k][1] * round,
					rimOff[k][2] * round
				};

				std::memset(&v, 0, sizeof(v));
				v.x = (float)(H[0] + at * axis[0] + rad * off[0]);
				v.y = (float)(H[1] + at * axis[1] + rad * off[1]);
				v.z = (float)(H[2] + at * axis[2] + rad * off[2]);
				v.layer = 1;
				v.bone[0] = (unsigned char)HD_LARA_TORSO;
				v.weight[0] = 255;
				/* THE TEXTURE IS THE RIM'S AT THE RIM and the middlemost rim
				 * vertex's in the middle, which is what makes the seam exact -
				 * the one place a mismatch would be seen - and leaves the
				 * middle a smear of a texel that is really painted.
				 *
				 * The alternative was tried and is worse: holding each spoke's
				 * own texture coordinates all the way in leaves every strip
				 * sampling a line ACROSS the page between two neighbours, and
				 * where the outfit's own atlas seam runs through the collar
				 * those two are half a page apart - the picture came back with
				 * two hard streaks through her chest. Converging is a soft
				 * radial gradient instead, and it cannot cross the page. */
				/* THE RIM RING KEEPS ITS OWN AND EVERY RING INSIDE IT WEARS THE
				 * ONE TEXEL. A gradient that runs all the way to the middle
				 * stretches the page along every spoke, and the picture comes
				 * back with the star of streaks the user calls "random"
				 * (finding J1(c)). One strip of gradient at the seam, where
				 * the surface is nearly edge on to the eye and nothing can be
				 * read off it anyway, and a flat colour over the rest. */
				const double t = j ? 1.0 : 0.0;

				v.u = (unsigned char)(rimU[k] * (1.0 - t) + meanU * t + 0.5);
				v.v = (unsigned char)(rimV[k] * (1.0 - t) + meanV * t + 0.5);

				/* THE SELF-CHECK BEHIND FINDING J1(a). A ring vertex further
				 * off the axis than the rim vertex it hangs from is a piece of
				 * cap outside the body's own skin, which is the plate the user
				 * photographed. The count goes in the log and must be 0. */
				if (std::sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]) * rad > rimR[k] + 0.01)
					g_neck_cap_outside++;
			}
		}

		{
			HDTrmVertex& mid = out->vertex[kSpokes * kRings];

			std::memset(&mid, 0, sizeof(mid));
			mid.x = (float)(H[0] + (sMean + HD_LARA_NECK_CAP_SINK) * axis[0]);
			mid.y = (float)(H[1] + (sMean + HD_LARA_NECK_CAP_SINK) * axis[1]);
			mid.z = (float)(H[2] + (sMean + HD_LARA_NECK_CAP_SINK) * axis[2]);
			mid.layer = 1;
			mid.bone[0] = (unsigned char)HD_LARA_TORSO;
			mid.weight[0] = 255;
			mid.u = (unsigned char)(meanU + 0.5);
			mid.v = (unsigned char)(meanV + 0.5);
		}

		/* The triangles: a quad band between two rings, then a fan into the
		 * middle. The winding is the one that leaves the outward face looking
		 * UP the axis, towards the eye; the draw is double sided anyway, which
		 * is the belt to this brace. */
		int at = 0;

		for (int j = 0; j + 1 < kRings; j++)
			for (int k = 0; k < kSpokes; k++)
			{
				const int k2 = (k + 1) % kSpokes;
				const unsigned short a0 = (unsigned short)(j * kSpokes + k);
				const unsigned short b0 = (unsigned short)(j * kSpokes + k2);
				const unsigned short a1 = (unsigned short)((j + 1) * kSpokes + k);
				const unsigned short b1 = (unsigned short)((j + 1) * kSpokes + k2);

				out->index[at++] = a0;
				out->index[at++] = a1;
				out->index[at++] = b1;
				out->index[at++] = a0;
				out->index[at++] = b1;
				out->index[at++] = b0;
			}

		for (int k = 0; k < kSpokes; k++)
		{
			const int k2 = (k + 1) % kSpokes;

			out->index[at++] = (unsigned short)((kRings - 1) * kSpokes + k);
			out->index[at++] = (unsigned short)(kSpokes * kRings);
			out->index[at++] = (unsigned short)((kRings - 1) * kSpokes + k2);
		}

		/* --- soft normals -------------------------------------------------- */
		std::vector<double> acc((size_t)vertices * 3, 0.0);

		for (int i = 0; i + 2 < out->indices; i += 3)
		{
			const HDTrmVertex& a = out->vertex[out->index[i + 0]];
			const HDTrmVertex& b = out->vertex[out->index[i + 1]];
			const HDTrmVertex& c = out->vertex[out->index[i + 2]];
			const double e1[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
			const double e2[3] = { c.x - a.x, c.y - a.y, c.z - a.z };
			const double n[3] =
			{
				e1[1] * e2[2] - e1[2] * e2[1],
				e1[2] * e2[0] - e1[0] * e2[2],
				e1[0] * e2[1] - e1[1] * e2[0]
			};

			for (int corner = 0; corner < 3; corner++)
				for (int a2 = 0; a2 < 3; a2++)
					acc[(size_t)out->index[i + corner] * 3 + a2] += n[a2];
		}

		for (int i = 0; i < vertices; i++)
		{
			double n[3] = { acc[(size_t)i * 3], acc[(size_t)i * 3 + 1],
				acc[(size_t)i * 3 + 2] };
			double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

			if (l < 1.0e-9)
			{
				n[0] = -axis[0];
				n[1] = -axis[1];
				n[2] = -axis[2];
				l = 1.0;
			}

			out->vertex[i].nx = pack_normal(n[0] / l);
			out->vertex[i].ny = pack_normal(n[1] / l);
			out->vertex[i].nz = pack_normal(n[2] / l);
		}

		return true;
	}
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

	/* The maker of the neck cap has no argument for a bind pose (#123, fix
	 * round I, I3), so it is kept here for it - once per level, before
	 * anything can call it. */
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

		/* The one part that is BUILT (#123, fix round I, I3), out of the body
		 * that stands first in this very list - HD_LARA_PART_BODY is part 0,
		 * so the source is always read before the maker runs. */
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

			for (int i = 0; held > 0.0f && i < HDModelPageCount(slot); i++)
			{
				const int page = HDModelPage(slot, i);
				bool onHand = false;

				for (int j = 0; j < HDModelPageCount(rest); j++)
					if (HDModelPage(rest, j) == page)
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

		/* What the maker made (#123, fix round I, I3), so that a picture the
		 * user does not like can be looked up: which page the cap wears and
		 * how many of its rim vertices had to borrow their texture
		 * coordinates from a neighbour because they wore another one. */
		if (part == HD_LARA_PART_NECK_CAP)
			note("the neck cap: %d spokes, %d rings, page %d, %d rim vertices "
				"re-homed, %d triangles; the rim r %.1f/%.1f/%.1f s "
				"%.1f/%.1f/%.1f, %d vertices outside their own spoke",
				HD_LARA_NECK_CAP_SPOKES, HD_LARA_NECK_CAP_RINGS,
				g_neck_cap_page, g_neck_cap_rehomed, HDModelIndices(slot) / 3,
				g_neck_cap_rim[0], g_neck_cap_rim[1], g_neck_cap_rim[2],
				g_neck_cap_rim[3], g_neck_cap_rim[4], g_neck_cap_rim[5],
				g_neck_cap_outside);

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

	/* AND A HAND PART'S SHEEN IS THE HAND'S, WHICH ALSO BELONGS TO THE OUTFIT
	 * (#123, fix round I, I4). What is in the hand is a page of the same file
	 * and gets its own material through HDModelSetPageSheen; this is the skin
	 * or the glove around it, and until this round a gloved Lara's leather
	 * shone like bare skin. */
	if (kPart[part].kind == 1)
	{
		const bool gloved = g_outfit > HD_LARA_OUTFIT_NONE &&
			g_outfit < HD_LARA_OUTFIT_COUNT && kOutfit[g_outfit].gloves &&
			kPart[part].gloves;
		const float* was = gloved ? kSheenLeather : kSheenSkin;

		if (strength)
			*strength = was[0];

		if (power)
			*power = was[1];

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

/* THE CUT'S CYLINDER, out of the same bind pose the cap was built from (#123,
 * fix round J, J1). One function so that the shape the shader carves and the
 * shape hd_lara.cpp's maker sewed a lid onto cannot drift apart: both are the
 * axis from the head joint to the torso joint of THIS level's own Lara. */
bool HDLaraNeckCut(HDModelNeckCut* out)
{
	if (!out || !g_neck_bind_ok)
		return false;

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
		return false;

	for (int i = 0; i < 3; i++)
		axis[i] /= len;

	out->on = 1.0f;
	out->depth = HD_LARA_NECK_CUT_S;
	out->radius = HD_LARA_NECK_CUT_R;
	out->nodes = HD_LARA_NECK_CUT_NODES;

	for (int i = 0; i < 3; i++)
	{
		out->head[i] = (float)H[i];
		out->axis[i] = (float)axis[i];
	}

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

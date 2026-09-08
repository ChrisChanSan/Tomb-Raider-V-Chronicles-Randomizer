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

		/* WHICH PAGES OF THE BODY ARE LARA'S OWN SKIN (#123, fix round K, K2).
		 *
		 * THE USER'S FINDING: "the hands react differently to the room's
		 * intensity - in five rooms the hand is now brighter, now darker than
		 * the forearm; a difference in the TEXTURE would be the same
		 * everywhere, so it is LIGHT."
		 *
		 * IT IS THE SHEEN, and everything else at that seam is provably
		 * identical: hd_laradraw.cpp pushes the body and the hand parts with
		 * the SAME nodeLight array, the same light list, the same sun factor
		 * and the same matrices, and HDObjectPush copies all four per draw.
		 * The one thing that differs is the material - fix round D gives the
		 * whole body the OUTFIT's, fix round I gives the hand the SKIN's - and
		 * where the room's own light list is empty (an ordinary room lit by
		 * its baked vertex colours, which is most of the game) the shader's
		 * highlight is `light * strength * pow(N.V, power) / 255`. So the
		 * difference between two materials is MULTIPLIED BY THE ROOM'S
		 * AMBIENT: bright in a bright room, gone in a dark one, and its sign
		 * follows the outfit. That is the user's five rooms, and it is why a
		 * texture difference was ruled out correctly.
		 *
		 * MEASURED (ModelProbe -handlight: the body's pages on nodes 8..13
		 * with the hand's beside them, and what each adds at N.V = 1 in a room
		 * of 255):
		 *
		 *     outfit         the arm wears        the hand wears   difference
		 *     CLASSIC        8001  25.5 (cloth)   8001  30.6         5.1
		 *     TR3_NEVADA     8202  20.4           8001  30.6        10.2
		 *     TR3_COAST      8202  20.4           8001  30.6        10.2
		 *     TR3_ANTARC     8202  25.5           8001  30.6         5.1
		 *     TR3_CATSUIT    8202 114.8 (latex)   8001  30.6       -84.2
		 *     VCI            8027 114.8 (latex)   8001  30.6       -84.2
		 *     RUSSIA         8041  30.6 (coat)    8001  45.9 (glove) 15.3
		 *
		 * and the Rome case is the plainest of all: the forearm and the hand
		 * are the SAME TEXTURE PAGE, 8001, shaded by two different materials.
		 *
		 * WHICH OF THOSE ARE FAULTS AND WHICH ARE THE ART. A catsuit sleeve
		 * meeting a bare hand IS a material boundary and must keep one; a coat
		 * sleeve meeting a leather glove likewise. The faults are the rows
		 * where the surface on both sides is Lara's own SKIN and only the
		 * material disagrees - CLASSIC and the four Tomb Raider III guests,
		 * whose arms are bare.
		 *
		 * SO IT IS A TABLE, for the reason the sheen itself is one and
		 * wearsRig above is one: geometry cannot tell skin from latex. Two
		 * measurements decide each row, both from ModelProbe -handlight and
		 * -neck: the page the body's ARM geometry wears, and the page its
		 * THROAT column wears. 8001 is Lara's skin in the three game 5 bodies
		 * (in OUTFIT_CLASSIC it carries her arms, her midriff and her neck at
		 * once); the four Tomb Raider III guests carry theirs on 8201, the
		 * neck, and 8202, the arms - the same two pages in all four, because
		 * it is the same Lara underneath. 0 ends the list.
		 *
		 * OUTFIT_SUBSUIT has none on purpose: the diving suit shows no skin
		 * anywhere, and its throat page 8047 is the SUIT. Naming it here would
		 * make the whole suit matt. OUTFIT_RUSSIA and OUTFIT_VCI name 8001
		 * because their necks and (for the catsuit) chest are bare; neither
		 * has skin at the ARM, so neither wrist seam moves. */
		int skin[3];
	};

	/*                                                    hands  braid holsters
	 *                                                                  gloves
	 *                                                                wearsRig */
	const Outfit kOutfit[HD_LARA_OUTFIT_COUNT] =
	{
		{ 0,                    "classic (no HD body)",   false, false, false, false, false, 0.00f,  1.0f, { 0, 0, 0 } },
		{ "OUTFIT_CLASSIC",     "the Rome outfit",        true,  true,  true,  false, false, 0.10f, 14.0f, { 8001, 0, 0 } },
		{ "OUTFIT_RUSSIA",      "the Russia outfit",      true,  true,  true,  true,  false, 0.12f, 16.0f, { 8001, 0, 0 } },
		{ "OUTFIT_SUBSUIT",     "the diving suit",        false, false, false, false, false, 0.30f, 22.0f, { 0, 0, 0 } },
		{ "OUTFIT_VCI",         "the VCI catsuit",        true,  true,  true,  false, true,  0.45f, 26.0f, { 8001, 0, 0 } },
		{ "OUTFIT_TR3_ANTARC",  "Antarctica (TR3)",       true,  true,  true,  false, false, 0.10f, 14.0f, { 8201, 8202, 0 } },
		{ "OUTFIT_TR3_NEVADA",  "Nevada (TR3)",           true,  true,  true,  false, false, 0.08f, 12.0f, { 8201, 8202, 0 } },
		{ "OUTFIT_TR3_COAST",   "Jungle (TR3)",           true,  true,  true,  false, false, 0.08f, 12.0f, { 8201, 8202, 0 } },
		{ "OUTFIT_TR3_CATSUIT", "London (TR3)",           true,  true,  true,  false, false, 0.45f, 26.0f, { 8201, 8202, 0 } }
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

	/* The two hand materials, out of the table above. */
	const float kSheenSkin[2] = { 0.12f, 10.0f };
	const float kSheenLeather[2] = { 0.18f, 20.0f };

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

	if (level->outfit <= HD_LARA_OUTFIT_NONE ||
		level->outfit >= HD_LARA_OUTFIT_COUNT)
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

		/* The BODY is looked at while it is still a file (#123, fix round K,
		 * K1): the inner skin's texel is a place on a page of the outfit, and
		 * after the build there is no page and no vertex left to ask. */
		if (part == HD_LARA_PART_BODY)
			one.measure = &measure_neck_skin;

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
			 * measured table on the outfit; kOutfit above carries it and the
			 * measurement behind it. Nothing happens for an outfit that shows
			 * no skin. */
			float skinS = 0.0f, skinP = 1.0f;
			int named = 0;

			HDLaraSkinSheen(&skinS, &skinP);

			for (int i = 0; i < 3 && kOutfit[level->outfit].skin[i]; i++)
			{
				const int page = kOutfit[level->outfit].skin[i];
				bool there = false;

				for (int j = 0; j < HDModelPageCount(slot); j++)
					if (HDModelPage(slot, j) == page)
					{
						there = true;
						break;
					}

				if (!there)
				{
					note("%s: page %d is named as her skin and the model does "
						"not wear it - the table and the art disagree", model,
						page);
					continue;
				}

				HDModelSetPageSheen(slot, page, skinS, skinP);
				named++;
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
	if (page < 0 || g_outfit <= HD_LARA_OUTFIT_NONE ||
		g_outfit >= HD_LARA_OUTFIT_COUNT)
		return false;

	for (int i = 0; i < 3; i++)
		if (kOutfit[g_outfit].skin[i] == page)
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

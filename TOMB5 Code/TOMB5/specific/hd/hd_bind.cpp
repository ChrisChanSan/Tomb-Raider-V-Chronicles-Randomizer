/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The LEVELn.MAP format - KEY=VALUE a line, CRLF, no sections and no comments
 * in any shipped file - is TRForge's measurement (MIT, trf_remaster\Remaster.h,
 * load_object_map). That file could not be vendored, because it drags in
 * TRForge's own level library - see vendor\trforge\VERSION.txt - so what
 * stands here was written against the description instead.
 *
 * One passage is the exception: the line splitter and KEY=VALUE parser
 * inside read_map_file follow TRForge's own load_object_map closely. What
 * differs is that the file is read in chunks here rather than in one piece,
 * and that a tab joins the characters stripped from the end of a line. That
 * is lawful, TRForge being MIT, and its notice travels with this tree in
 * vendor\trforge\LICENSE.
 *
 * bind_against's third rule, the EXTRA prefix, is written against a
 * description of two things neither of which is copied: TRForge's
 * generic slot binding (MIT, vendor\trforge\src\trf_remaster\ItemModels.cpp,
 * bind_level_items) and TROne's EXTRA<game>_/EXTRA_ prefix pass and its
 * Effect-category exclusion (Copyright 2026 Todesengel, GPLv3,
 * host\render\game_renderer.cpp). The order of the three prefixes and the
 * rule that the hand table is asked first are this project's own reading of
 * that description; the measured table of which slot resolves to which file
 * on the installed bundle, and the EXTRA4_ tier TROne cannot reach at all
 * running as game 5, are new. See THIRD-PARTY.md.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_bind.h"
#include "hd_mods.h"		// the EXTRA prefix rule counts a mod's file
#include "hd_folderlist.h"	// a model's file found by a folder listing

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
	/* GENERATED from game\objects.h - the enum object_types, in order, one
	 * string per entry. It is written out rather than derived because this
	 * file is built without the engine as well, for the diagnostic tools
	 * TrgProbe and MapProbe, so that what they measure is what the game
	 * binds. hd_level.cpp holds the static_assert against NUMBER_OBJECTS,
	 * which is what catches a new slot that was added and not entered here. */
	const char* const slot_names[] =
	{
	"LARA", "PISTOLS_ANIM", "UZI_ANIM", "SHOTGUN_ANIM", "CROSSBOW_ANIM", "HK_ANIM",
	"REVOLVER_ANIM", "FLARE_ANIM", "LARA_SKIN", "LARA_SKIN_JOINTS", "LARA_SCREAM",
	"LARA_CROSSBOW_LASER", "LARA_REVOLVER_LASER", "LARA_HOLSTERS", "LARA_HOLSTERS_PISTOLS",
	"LARA_HOLSTERS_UZIS", "LARA_HOLSTERS_REVOLVER", "LARA_SPEECH_HEAD1", "LARA_SPEECH_HEAD2",
	"LARA_SPEECH_HEAD3", "LARA_SPEECH_HEAD4", "ACTOR1_SPEECH_HEAD1", "ACTOR1_SPEECH_HEAD2",
	"ACTOR2_SPEECH_HEAD1", "ACTOR2_SPEECH_HEAD2", "LARA_EXTRA_MESH1", "LARA_EXTRA_MESH2",
	"LARA_EXTRA_MESH3", "CROWBAR_ANIM", "TORCH_ANIM", "HAIR", "SAS", "SAS_MIP", "SWAT", "SWAT_MIP",
	"SWAT_PLUS", "SWAT_PLUS_MIP", "BLUE_GUARD", "BLUE_GUARD_MIP", "TWOGUN", "TWOGUN_MIP", "DOG",
	"DOG_MIP", "CROW", "CROW_MIP", "LARSON", "LARSON_MIP", "PIERRE", "PIERRE_MIP", "MAFIA",
	"MAFIA_MIP", "MAFIA2", "MAFIA2_MIP", "SAILOR", "SAILOR_MIP", "CRANE_GUY", "CRANE_GUY_MIP",
	"LION", "LION_MIP", "GLADIATOR", "GLADIATOR_MIP", "ROMAN_GOD", "ROMAN_GOD_MIP", "HYDRA",
	"HYDRA_MIP", "GUARDIAN", "GUARDIAN_MIP", "HITMAN", "HITMAN_MIP", "SCIENTIST", "SCIENTIST_MIP",
	"WILLOWISP", "WILLOWISP_MIP", "SKELETON", "SKELETON_MIP", "REAPER", "REAPER_MIP",
	"MAZE_MONSTER", "MAZE_MONSTER_MIP", "GREEN_TEETH", "GREEN_TEETH_MIP", "ATTACK_SUB",
	"ATTACK_SUB_MIP", "SNIPER", "SNIPER_MIP", "HUSKIE", "HUSKIE_MIP", "CHEF", "CHEF_MIP", "IMP",
	"IMP_MIP", "GUNSHIP", "GUNSHIP_MIP", "BAT", "RAT", "SPIDER", "SPIDER_GENERATOR", "AUTOGUN",
	"ELECTRICAL_CABLES", "DARTS", "DART_EMITTER", "HOMING_DART_EMITTER", "FALLING_CEILING",
	"FALLING_BLOCK", "FALLING_BLOCK2", "CRUMBLING_FLOOR", "TRAPDOOR1", "TRAPDOOR2", "TRAPDOOR3",
	"FLOOR_TRAPDOOR1", "FLOOR_TRAPDOOR2", "CEILING_TRAPDOOR1", "CEILING_TRAPDOOR2",
	"SCALING_TRAPDOOR", "ROLLINGBALL", "ROLLINGBARREL", "SPIKEY_FLOOR", "TEETH_SPIKES",
	"ROME_HAMMER", "HAMMER2", "FLAME", "FLAME_EMITTER", "FLAME_EMITTER2", "FLAME_EMITTER3",
	"COOKER_FLAME", "BURNING_ROOTS", "ROPE", "FIREROPE", "POLEROPE", "PROPELLER_H", "PROPELLER_V",
	"GRAPPLING_TARGET", "ONEBLOCK_PLATFORM", "TWOBLOCK_PLATFORM", "RAISING_BLOCK1",
	"RAISING_BLOCK2", "EXPANDING_PLATFORM", "PUSHABLE_OBJECT1", "PUSHABLE_OBJECT2",
	"PUSHABLE_OBJECT3", "PUSHABLE_OBJECT4", "PUSHABLE_OBJECT5", "WRECKING_BALL", "DEATH_SLIDE",
	"TORPEDO", "CHAFF", "SATCHEL_BOMB", "ELECTRIC_FENCE", "LIFT", "EXPLOSION", "IRIS_LIGHTNING",
	"MONITOR_SCREEN", "SECURITY_SCREENS", "MOTION_SENSORS", "TIGHT_ROPE", "PARALLEL_BARS",
	"XRAY_CONTROLLER", "CUTSCENE_ROPE", "PORTAL", "GEN_SLOT1", "GEN_SLOT2", "GEN_SLOT3",
	"GEN_SLOT4", "IMP_ROCK", "SEARCH_OBJECT1", "SEARCH_OBJECT1_MIP", "SEARCH_OBJECT2",
	"SEARCH_OBJECT2_MIP", "SEARCH_OBJECT3", "SEARCH_OBJECT3_MIP", "SEARCH_OBJECT4",
	"SEARCH_OBJECT4_MIP", "PUZZLE_ITEM1", "PUZZLE_ITEM2", "PUZZLE_ITEM3", "PUZZLE_ITEM4",
	"PUZZLE_ITEM5", "PUZZLE_ITEM6", "PUZZLE_ITEM7", "PUZZLE_ITEM8", "PUZZLE_ITEM1_COMBO1",
	"PUZZLE_ITEM1_COMBO2", "PUZZLE_ITEM2_COMBO1", "PUZZLE_ITEM2_COMBO2", "PUZZLE_ITEM3_COMBO1",
	"PUZZLE_ITEM3_COMBO2", "PUZZLE_ITEM4_COMBO1", "PUZZLE_ITEM4_COMBO2", "PUZZLE_ITEM5_COMBO1",
	"PUZZLE_ITEM5_COMBO2", "PUZZLE_ITEM6_COMBO1", "PUZZLE_ITEM6_COMBO2", "PUZZLE_ITEM7_COMBO1",
	"PUZZLE_ITEM7_COMBO2", "PUZZLE_ITEM8_COMBO1", "PUZZLE_ITEM8_COMBO2", "KEY_ITEM1", "KEY_ITEM2",
	"KEY_ITEM3", "KEY_ITEM4", "KEY_ITEM5", "KEY_ITEM6", "KEY_ITEM7", "KEY_ITEM8",
	"KEY_ITEM1_COMBO1", "KEY_ITEM1_COMBO2", "KEY_ITEM2_COMBO1", "KEY_ITEM2_COMBO2",
	"KEY_ITEM3_COMBO1", "KEY_ITEM3_COMBO2", "KEY_ITEM4_COMBO1", "KEY_ITEM4_COMBO2",
	"KEY_ITEM5_COMBO1", "KEY_ITEM5_COMBO2", "KEY_ITEM6_COMBO1", "KEY_ITEM6_COMBO2",
	"KEY_ITEM7_COMBO1", "KEY_ITEM7_COMBO2", "KEY_ITEM8_COMBO1", "KEY_ITEM8_COMBO2", "PICKUP_ITEM1",
	"PICKUP_ITEM2", "PICKUP_ITEM3", "PICKUP_ITEM4", "PICKUP_ITEM1_COMBO1", "PICKUP_ITEM1_COMBO2",
	"PICKUP_ITEM2_COMBO1", "PICKUP_ITEM2_COMBO2", "PICKUP_ITEM3_COMBO1", "PICKUP_ITEM3_COMBO2",
	"PICKUP_ITEM4_COMBO1", "PICKUP_ITEM4_COMBO2", "EXAMINE1", "EXAMINE2", "EXAMINE3", "BOTTLE",
	"CLOTH", "WET_CLOTH", "COSH", "HAMMER_ITEM", "CROWBAR_ITEM", "BURNING_TORCH_ITEM",
	"PUZZLE_HOLE1", "PUZZLE_HOLE2", "PUZZLE_HOLE3", "PUZZLE_HOLE4", "PUZZLE_HOLE5", "PUZZLE_HOLE6",
	"PUZZLE_HOLE7", "PUZZLE_HOLE8", "PUZZLE_DONE1", "PUZZLE_DONE2", "PUZZLE_DONE3", "PUZZLE_DONE4",
	"PUZZLE_DONE5", "PUZZLE_DONE6", "PUZZLE_DONE7", "PUZZLE_DONE8", "KEY_HOLE1", "KEY_HOLE2",
	"KEY_HOLE3", "KEY_HOLE4", "KEY_HOLE5", "KEY_HOLE6", "KEY_HOLE7", "KEY_HOLE8", "SWITCH_TYPE1",
	"SWITCH_TYPE2", "SWITCH_TYPE3", "SWITCH_TYPE4", "SWITCH_TYPE5", "SWITCH_TYPE6", "SWITCH_TYPE7",
	"SWITCH_TYPE8", "AIRLOCK_SWITCH", "UNDERWATER_SWITCH1", "UNDERWATER_SWITCH2", "TURN_SWITCH",
	"COG_SWITCH", "LEVER_SWITCH", "JUMP_SWITCH", "CROWBAR_SWITCH", "PULLEY", "CROWDOVE_SWITCH",
	"DOOR_TYPE1", "DOOR_TYPE1_MIP", "DOOR_TYPE2", "DOOR_TYPE2_MIP", "DOOR_TYPE3", "DOOR_TYPE3_MIP",
	"DOOR_TYPE4", "DOOR_TYPE4_MIP", "DOOR_TYPE5", "DOOR_TYPE5_MIP", "DOOR_TYPE6", "DOOR_TYPE6_MIP",
	"DOOR_TYPE7", "DOOR_TYPE7_MIP", "DOOR_TYPE8", "DOOR_TYPE8_MIP", "CLOSED_DOOR1",
	"CLOSED_DOOR1_MIP", "CLOSED_DOOR2", "CLOSED_DOOR2_MIP", "CLOSED_DOOR3", "CLOSED_DOOR3_MIP",
	"CLOSED_DOOR4", "CLOSED_DOOR4_MIP", "CLOSED_DOOR5", "CLOSED_DOOR5_MIP", "CLOSED_DOOR6",
	"CLOSED_DOOR6_MIP", "LIFT_DOORS1", "LIFT_DOORS1_MIP", "LIFT_DOORS2", "LIFT_DOORS2_MIP",
	"PUSHPULL_DOOR1", "PUSHPULL_DOOR1_MIP", "PUSHPULL_DOOR2", "PUSHPULL_DOOR2_MIP", "KICK_DOOR1",
	"KICK_DOOR1_MIP", "KICK_DOOR2", "KICK_DOOR2_MIP", "UNDERWATER_DOOR", "UNDERWATER_DOOR_MIP",
	"DOUBLE_DOORS", "DOUBLE_DOORS_MIP", "SEQUENCE_DOOR1", "SEQUENCE_SWITCH1", "SEQUENCE_SWITCH2",
	"SEQUENCE_SWITCH3", "STEEL_DOOR", "GOD_HEAD", "PISTOLS_ITEM", "PISTOLS_AMMO_ITEM", "UZI_ITEM",
	"UZI_AMMO_ITEM", "SHOTGUN_ITEM", "SHOTGUN_AMMO1_ITEM", "SHOTGUN_AMMO2_ITEM", "CROSSBOW_ITEM",
	"CROSSBOW_AMMO1_ITEM", "CROSSBOW_AMMO2_ITEM", "CROSSBOW_BOLT", "HK_ITEM", "HK_AMMO_ITEM",
	"REVOLVER_ITEM", "REVOLVER_AMMO_ITEM", "BIGMEDI_ITEM", "SMALLMEDI_ITEM", "LASERSIGHT_ITEM",
	"BINOCULARS_ITEM", "SILENCER_ITEM", "FLARE_ITEM", "FLARE_INV_ITEM", "COMPASS_ITEM",
	"MEMCARD_LOAD_INV_ITEM", "MEMCARD_SAVE_INV_ITEM", "PC_LOAD_INV_ITEM", "PC_SAVE_INV_ITEM",
	"DC_LOAD_INV_ITEM", "DC_SAVE_INV_ITEM", "SMOKE_EMITTER_WHITE", "SMOKE_EMITTER_BLACK",
	"STEAM_EMITTER", "EARTHQUAKE", "BUBBLES", "WATERFALLMIST", "GUNSHELL", "SHOTGUNSHELL",
	"GUN_FLASH", "COLOURED_LIGHT", "BLINKING_LIGHT", "PULSE_LIGHT", "STROBE_LIGHT",
	"ELECTRICAL_LIGHT", "LENS_FLARE", "AI_GUARD", "AI_AMBUSH", "AI_PATROL1", "AI_MODIFY",
	"AI_FOLLOW", "AI_PATROL2", "AI_X1", "AI_X2", "LARA_START_POS", "TELEPORTER", "LIFT_TELEPORTER",
	"RAISING_COG", "LASERS", "STEAM_LASERS", "FLOOR_LASERS", "KILL_ALL_TRIGGERS",
	"TRIGGER_TRIGGERER", "HIGH_OBJECT1", "HIGH_OBJECT2", "SMASH_OBJECT1", "SMASH_OBJECT2",
	"SMASH_OBJECT3", "SMASH_OBJECT4", "SMASH_OBJECT5", "SMASH_OBJECT6", "SMASH_OBJECT7",
	"SMASH_OBJECT8", "MESHSWAP1", "MESHSWAP2", "MESHSWAP3", "BODY_PART", "CAMERA_TARGET",
	"WATERFALL1", "WATERFALL2", "WATERFALL3", "FISHTANK", "WATERFALLSS1", "WATERFALLSS2",
	"ANIMATING1", "ANIMATING1_MIP", "ANIMATING2", "ANIMATING2_MIP", "ANIMATING3", "ANIMATING3_MIP",
	"ANIMATING4", "ANIMATING4_MIP", "ANIMATING5", "ANIMATING5_MIP", "ANIMATING6", "ANIMATING6_MIP",
	"ANIMATING7", "ANIMATING7_MIP", "ANIMATING8", "ANIMATING8_MIP", "ANIMATING9", "ANIMATING9_MIP",
	"ANIMATING10", "ANIMATING10_MIP", "ANIMATING11", "ANIMATING11_MIP", "ANIMATING12",
	"ANIMATING12_MIP", "ANIMATING13", "ANIMATING13_MIP", "ANIMATING14", "ANIMATING14_MIP",
	"ANIMATING15", "ANIMATING15_MIP", "ANIMATING16", "ANIMATING16_MIP", "BRIDGE_FLAT",
	"BRIDGE_FLAT_MIP", "BRIDGE_TILT1", "BRIDGE_TILT1_MIP", "BRIDGE_TILT2", "BRIDGE_TILT2_MIP",
	"HORIZON", "SKY_GRAPHICS", "BINOCULAR_GRAPHICS", "TARGET_GRAPHICS", "DEFAULT_SPRITES",
	"MISC_SPRITES", "BLUE_GUARD2", "ROMAN_GOD2", "MESHSWAP_CROSS_SWAT", "MESHSWAP_CROSS_SWATPLUS",
	"MESHSWAP_CROSS_TWOGUN", "MESHSWAP_CROSS_GUARD", "MESHSWAP_CROSS_MAFIA",
	"MESHSWAP_CROSS_GUARD2", "MESHSWAP_CROSS_HITMAN", "CYBORG", "MESHSWAP_CROSS_CRANEGUY",
	"BRAWLER", "DRONE", "SCIENTIST_ARMED"
	};

	static_assert(sizeof(slot_names) / sizeof(slot_names[0]) == HD_SLOT_COUNT,
		"HD_SLOT_COUNT and the generated slot name table have drifted apart");

	/* The hand table: eleven slots this project added beyond the original
	 * 460 and three vanilla slots the remaster leaves unnamed: ROMAN_GOD and
	 * the two revolver pickups.
	 *
	 * For the eleven the remaster has all the models; what it does not have is
	 * our slot numbers, because no vanilla level uses them. Each of those
	 * lines is a measurement that can only be confirmed by picture - which
	 * model the remaster really draws for them is only certain once it is
	 * on screen. Two are already certain from the shipped .MAP files: the
	 * joby levels say BLUE_GUARD=BLUE_GUARD_JOBY, ANDREA3 says
	 * ROMAN_GOD=ROMAN_GOD_ANDREA_HAMMER.
	 *
	 * The MESHSWAP_CROSS_* slots are warehouses: they never hold an item, and
	 * a mesh swap takes single meshes out of them. Binding them to the model
	 * of the family they belong to is what makes those meshes findable at
	 * all; whether the HD model's mesh numbering lines up with the classic
	 * one is a separate question and is not answered here.
	 *
	 * The twelfth line is a different animal: ROMAN_GOD, slot 61, is vanilla,
	 * and there is no ROMAN_GOD.TRM. Measured over the shipped files: 5\ITEM
	 * has ROMAN_GOD_ANDREA_SWORD and ROMAN_GOD_ANDREA_HAMMER (and eight
	 * numbered ROMAN_GOD_ANDREA2_n / ANDREA3_n), and of the fifteen .MAP
	 * files exactly two mention the slot - ANDREA2 says SWORD, ANDREA3 says
	 * HAMMER. Every other level that carries the slot therefore falls through
	 * to its own name, finds nothing, and draws the god classic. The
	 * randomizer puts him in levels whose .MAP never expected him, so that
	 * case is now the common one. The sword is the default because ANDREA2 is
	 * where the vanilla game first shows him.
	 *
	 * The rule this line follows, and the reason it can live in the same
	 * table: the hand table is only ever asked when the level's .MAP is
	 * silent. ANDREA2 and ANDREA3 still get their own answer. */
	struct HandBound
	{
		const char* slot;
		const char* model;
		/* Where the line holds: 0 for every level, else the stems of the
		 * levels it is for, space-separated ("RICH1 RICH2 RICH3"), spelled
		 * as the .MAP files are and compared without regard to case - the
		 * engine hands the stem in as the sidecar spells it ("Rich2"), a
		 * tool as typed. A line for a level the bind is not asked for is
		 * skipped, and the search goes on to the next line and then to
		 * rules 3 and 4 as if the line were not there. */
		const char* levels;
	};

	/* The three prefixes, in trial order. See hd_bind.h. */
	struct PrefixRule { const char* prefix; HDBindVia via; };

	const PrefixRule extra_prefixes[] =
	{
		{ "EXTRA5_", HD_BIND_VIA_PREFIX5 },
		{ "EXTRA_",  HD_BIND_VIA_PREFIX  },
		{ "EXTRA4_", HD_BIND_VIA_PREFIX4 }
	};

	const HandBound hand_table[] =
	{
		{ "BLUE_GUARD2",             "BLUE_GUARD_JOBY",         0 },
		{ "ROMAN_GOD2",              "ROMAN_GOD_ANDREA_HAMMER", 0 },
		{ "CYBORG",                  "HITMAN",                  0 },
		{ "MESHSWAP_CROSS_SWAT",     "SWAT",                    0 },
		{ "MESHSWAP_CROSS_SWATPLUS", "SWAT_PLUS",               0 },
		{ "MESHSWAP_CROSS_TWOGUN",   "TWOGUN",                  0 },
		{ "MESHSWAP_CROSS_GUARD",    "BLUE_GUARD",              0 },
		{ "MESHSWAP_CROSS_MAFIA",    "MAFIA",                   0 },
		{ "MESHSWAP_CROSS_GUARD2",   "BLUE_GUARD_JOBY",         0 },
		{ "MESHSWAP_CROSS_HITMAN",   "HITMAN",                  0 },
		{ "MESHSWAP_CROSS_CRANEGUY", "CRANE_GUY",               0 },

		/* BRAWLER (471) has no line here, on purpose: his meshes come out
		 * of JOBY2's slot 439, and the sidecar's donor line says so, so
		 * JOBY2's .MAP answers for him (ANIMATING12_MIP_JOBY2) like it
		 * does for any model the randomizer carried out of a level.
		 * SCIENTIST_ARMED (473) has none either: his donor line names
		 * RICH1's slot 69, and RICH1's .MAP answers SCIENTIST. */

		{ "ROMAN_GOD",               "ROMAN_GOD_ANDREA_SWORD",  0 },

		/* The Desert Eagle pickup. Two more slots whose model exists under no
		 * plain name: the remaster puts every pickup under an EXTRA prefix -
		 * EXTRA5_REVOLVER_ITEM, EXTRA5_REVOLVER_ITEM_JOBY,
		 * EXTRA5_REVOLVER_AMMO_ITEM - and not one of the fifteen .MAP files
		 * mentions either slot, so both fall through to their own name, find
		 * nothing and draw classic.
		 *
		 * The JOBY file and not the plain one, measured twice: the picture
		 * (EXTRA5_REVOLVER_ITEM is Tomb Raider IV's six-shooter with a laser
		 * sight, EXTRA5_REVOLVER_ITEM_JOBY the Desert Eagle) and the
		 * randomizer, which copies JOBY2's slots 6, 12, 16, 347 and 348 into
		 * every level and replaces what is there, by rule
		 * (TR5Randomizer\src\core\Randomizer.cpp, kWeaponRules): the revolver
		 * is replaced with the Desert Eagle entirely. A level played without
		 * that copy - the randomizer's "all weapons" switch off, or a
		 * vanilla file - keeps its six-shooter classic and gets the Deagle
		 * in HD; that is the one case this line is wrong in, and it is the
		 * same case the wardrobe's DEAGLE hands are wrong in.
		 *
		 * The rest of the EXTRA family - the pistols, uzi, shotgun, HK,
		 * grapple, medipack, flare, lasersight, binocular and crowbar
		 * pickups - is reached generically below: the EXTRA prefix rule
		 * tries the EXTRA5_/EXTRA_/EXTRA4_ prefix in front of the plain slot
		 * name whenever this table and the .MAP are both silent. It cannot
		 * reach REVOLVER_ITEM's Deagle variant, which is why these two
		 * lines stay - the generic rule finds EXTRA5_REVOLVER_ITEM (Tomb
		 * Raider IV's six-shooter) first and never gets to try the _JOBY
		 * suffix, and the hand table is asked before the generic rule for
		 * exactly that reason (see bind_against). */
		{ "REVOLVER_ITEM",           "EXTRA5_REVOLVER_ITEM_JOBY", 0 },
		{ "REVOLVER_AMMO_ITEM",      "EXTRA5_REVOLVER_AMMO_ITEM", 0 },

		/* The VCI levels' own pickups: two slots whose remaster model is not
		 * the one rule 3 finds.
		 *
		 * BINOCULARS_ITEM is the HEADSET in the VCI levels - the same slot and
		 * the same classic mesh name, renamed by the inventory alone
		 * (game/newinv2.cpp, TXT_Headset). Rule 3 finds
		 * 4/ITEM/EXTRA_BINOCULARS_ITEM.TRM, the binoculars, in every level; the
		 * remaster keeps the headset one folder up, as
		 * 5/ITEM/EXTRA5_BINOCULARS_ITEM_RICH.TRM, a name no prefix rule builds.
		 *
		 * HK_ITEM in RICH2 is the scripted second HK, a mesh of its own that
		 * the inventory turns by angles of its own (game/newinv2.cpp rewrites
		 * INV_HK_ITEM1's xrot/yrot/zrot, flags and yoff for
		 * LVL5_ESCAPE_WITH_THE_IRIS, and the pickup display lifts it 70 units
		 * there). Rule 3 finds EXTRA5_HK_ITEM.TRM, every other level's HK,
		 * modelled in the other levels' frame - which RICH2's ring then turns
		 * by RICH2's angles, producing a visibly wrong rotation. The
		 * remaster ships that mesh's own HD twin as EXTRA5_HK_ITEM_RICH2.TRM.
		 * (It ships an EXTRA5_HK_ITEM_RICH2_CUT.TRM as well, which no line
		 * here uses: RICHCUT2 carries no HK_ITEM slot at all, only Lara's
		 * HK_ANIM, and that one is the wardrobe's - measured with
		 * ModelProbe -all, which lists no BINOCULARS_ITEM there either.)
		 *
		 * Hand-table lines rather than a fourth prefix rule, because the
		 * suffixes follow no one pattern (_RICH for three levels, _RICH2 for
		 * one) and the .MAP files are silent on both slots in all fifteen
		 * levels (measured). The lines hold in the named levels only;
		 * everywhere else both slots take rule 3 as before. */
		{ "BINOCULARS_ITEM",         "EXTRA5_BINOCULARS_ITEM_RICH", "RICH1 RICH2 RICH3" },
		{ "HK_ITEM",                 "EXTRA5_HK_ITEM_RICH2",        "RICH2" },

		/* The submarine's diving suits, JOBY3 and JOBY5. Each crew cabin's
		 * glass case hangs the suit in two halves on chains, and JOBY3's
		 * room 40 has a third pair: the legs standing and the top hanging
		 * with the console on its chest. The .MAP reaches none of them -
		 * DOG_MIP's line names DOG_MIP_JOBY3_JOBY5.TRM, a file in no ITEM
		 * folder, and the others it does not mention - and the install keeps
		 * the suit as EXTRA5_SUBSUIT_* files no rule builds a name for.
		 *
		 * Which file is which slot's, read off the classic mesh trees
		 * (ModelProbe -slot -model -bones, the box of each bone against the
		 * classic mesh of its node):
		 *
		 *   DOG_MIP, 10 meshes: the top on its chains - torso on node 0,
		 *     the arms on 1-3 and 4-6, the chains on 7, the two air tanks
		 *     on 8 and 9 - with the console filled in its torso mesh.
		 *     TOP_CHAIN_JOBY5: arms and chains 4 to 38 units off their
		 *     nodes, and the console with both slots filled.
		 *   ANIMATING10_MIP, 10 meshes, the same tree without the console:
		 *     the top at room 40's console, whose console is the two puzzle
		 *     holes on its chest, DUMMY in the map, with no room art in
		 *     their box. TOP_CHAIN, the empty bezel with its plate, is the
		 *     console before the player's items are in; the guests' table
		 *     below names the two files it changes to as they go in.
		 *   ANIMATING12_MIP, 8 meshes: the legs on their chains - hips on
		 *     node 0, legs on 1-6, chains on 7. BOTTOM_CHAIN, 9 to 31 units.
		 *   ANIMATING10, 7 meshes: the legs standing. BOTTOM, 9 to 31 units.
		 *
		 * The three tops carry their torso on bone 8, the node of the right
		 * tank, which hangs off node 0 with no offset; no keyframe and no
		 * cutscene frame turns it (CutseqProbe -turns 5 2 and 32 2: 0 frames),
		 * so it draws where node 0 does. The map's DOG_MIP line names a file
		 * that does not exist, and such a line gives way to a level-bound line
		 * here - see bind_against. */
		{ "DOG_MIP",                 "EXTRA5_SUBSUIT_TOP_CHAIN_JOBY5", "JOBY3 JOBY5" },
		{ "ANIMATING12_MIP",         "EXTRA5_SUBSUIT_BOTTOM_CHAIN",    "JOBY3 JOBY5" },
		{ "ANIMATING10_MIP",         "EXTRA5_SUBSUIT_TOP_CHAIN",       "JOBY3" },
		{ "ANIMATING10",             "EXTRA5_SUBSUIT_BOTTOM",          "JOBY3" },

		/* And the suit in the cutscenes that dress Lara in it. The actors
		 * are slots of their own (cutseq.bin, CutseqProbe -actors):
		 *
		 *   GLADIATOR_MIP, 7 meshes: the legs, actor 3 of JOBY3's last
		 *     cutscene (5) and of JOBY5's first (32). The tree and meshes
		 *     of ANIMATING10, and BOTTOM fits it as it fits that one.
		 *     From frame 542 of cutscene 5 its root is hers (CutseqProbe
		 *     -frames 5): her legs hidden, these are the suit's on her.
		 *   SWAT_PLUS_MIP in JOBY3 and CROW_MIP in JOBY4, 17 meshes: Lara
		 *     in the whole suit, her own fifteen-node tree and two empty
		 *     nodes - actor 1 of cutscene 5, who takes her place at frame
		 *     855 behind a camera cut, and of JOBY4's cutscene 40, which
		 *     hides her and puts her speech heads on its node 14
		 *     (game\deltapak.cpp, joby7).
		 *     FULL is the suit on her tree - every bone of it within 40
		 *     units of its node's mesh, the drift 6 - and the suit she
		 *     wears in JOBY4 as well: every vertex of OUTFIT_SUBSUIT but 31
		 *     of the torso's lies within half a unit of one of FULL's. The
		 *     face in its helmet is hers (hd_model.cpp, the face table). */
		{ "GLADIATOR_MIP",           "EXTRA5_SUBSUIT_BOTTOM",          "JOBY3 JOBY5" },
		{ "SWAT_PLUS_MIP",           "EXTRA5_SUBSUIT_FULL",            "JOBY3" },
		{ "CROW_MIP",                "EXTRA5_SUBSUIT_FULL",            "JOBY4" },

		/* The props four cutscenes put into Lara's hand (game\deltapak.cpp,
		 * do_catapult_meshswap and its three siblings): her hand node takes
		 * one node of a holder slot - a slot of her fifteen-node tree with
		 * no item and no line in the .MAP (ItemCheck, MapProbe), whose mesh
		 * there is the hand holding the thing (ModelProbe -meshes). The
		 * install has each as a hand file of its own: CROW_MIP's mesh 13,
		 * the catapult, in ANDY1's cutscene 14; MESHSWAP2's mesh 10, the
		 * clanger, in its cutscene 9; MESHSWAP3's mesh 10, the hammer, in
		 * RICH1's 20; MAFIA_MIP's mesh 10, the chalk, in ANDY3's 38. The
		 * files and the four pairs are TROne's measurement (TROne-main, copy
		 * of 2026-10-23, commit a3c0c633, games\tr5\glue\hostglue_file5.cpp,
		 * TroneRegisterCutPropHands); each file's hand bone lies 10 to 25
		 * units from the holder's mesh (ModelProbe -bones). Bound to the
		 * holder, the hand is that slot's node, and hd_laradraw.cpp draws a
		 * borrowed node from the slot's model (hd_object.cpp, "a cutscene's
		 * borrowed meshes"). */
		{ "CROW_MIP",                "HAND_BARE_CATAPULT",             "ANDY1" },
		{ "MESHSWAP2",               "HAND_BARE_IRON_CLAPPER",         "ANDY1" },
		{ "MESHSWAP3",               "HAND_BARE_HAMMER",               "RICH1" },
		{ "MAFIA_MIP",               "HAND_BARE_CHALK",                "ANDY3" }
	};

	/* The DUMMY slots whose look another slot's model carries (hd_bind.h,
	 * HDBindDummyHost). JOBY3's console: PUZZLE_HOLE3 and PUZZLE_HOLE4, items
	 * 89 and 91 of room 40, stand at 52736, 4864, 79360, the very place of
	 * the suit top ANIMATING10_MIP (item 87); their frame boxes lie on its
	 * chest, the room art holds no triangle there (TrgProbe -boxes), and the
	 * top's model carries the console on its torso. The done states take
	 * the holes' places once the player's items are in.
	 *
	 * And the host's model follows them (hd_bind.h, HDBindGuestStates). A
	 * hole becomes its done slot when its item goes in (game\pickup.cpp,
	 * PuzzleDone, object_number + 8); PUZZLE_HOLE3 takes puzzle item 3, the
	 * suit's console, and PUZZLE_HOLE4 puzzle item 4, its battery, whose
	 * OCB of 1029 starts cutscene 5 as it goes in (PuzzleHoleCollision,
	 * hole + PUZZLE_ITEM1). The classic meshes are the four looks: HOLE3's
	 * the empty frame, HOLE4's DONE3's 76 vertices with every face on a
	 * patch of texels that are all transparent - it draws nothing - DONE3's
	 * the console with the battery's slot empty, DONE4's the console with
	 * the battery. The three top files are the same three looks on the
	 * chest: every vertex of TOP_CHAIN, the bezel round a plate, is in the
	 * other two, which add the console into the bezel - TOP_CHAIN_HALF 825
	 * vertices with the battery's slot empty, TOP_CHAIN_JOBY5 1297 with it
	 * filled, all in the holes' frame box. So the empty frame is in the
	 * file, and no state draws a hole's classic mesh beside it. The last
	 * done guest in the table's order decides, which is the classic look
	 * in either order the items go in: DONE4's mesh holds the console too.
	 */
	struct DummyHost
	{
		const char* slot;
		const char* host;
		const char* levels;	// as in the hand table
		const char* done;	// what the guest becomes, or 0
		const char* model;	// the host's model from then on, or 0
	};

	const DummyHost dummy_hosts[] =
	{
		{ "PUZZLE_HOLE3", "ANIMATING10_MIP", "JOBY3", "PUZZLE_DONE3",
			"EXTRA5_SUBSUIT_TOP_CHAIN_HALF" },
		{ "PUZZLE_HOLE4", "ANIMATING10_MIP", "JOBY3", "PUZZLE_DONE4",
			"EXTRA5_SUBSUIT_TOP_CHAIN_JOBY5" },
		{ "PUZZLE_DONE3", "ANIMATING10_MIP", "JOBY3", 0, 0 },
		{ "PUZZLE_DONE4", "ANIMATING10_MIP", "JOBY3", 0, 0 }
	};

	// The slot number of an engine slot name, or -1.
	int slot_of_name(const char* name)
	{
		if (!name)
			return -1;

		for (int slot = 0; slot < HD_SLOT_COUNT; slot++)
			if (slot_names[slot] && _stricmp(slot_names[slot], name) == 0)
				return slot;

		return -1;
	}

	/* Does a hand-table line hold for `level`? A line with no level list holds
	 * everywhere; one with a list holds where a whole token of it equals the
	 * stem, case aside. No level in hand - a tool asking with no map loaded,
	 * a donor bind with no donor - makes every listed line miss. */
	bool hand_applies(const char* levels, const char* level)
	{
		if (!levels)
			return true;
		if (!level || !*level)
			return false;

		const size_t want = strlen(level);
		const char* at = levels;

		while (*at)
		{
			while (*at == ' ')
				at++;

			const char* stop = at;

			while (*stop && *stop != ' ')
				stop++;

			if ((size_t)(stop - at) == want && _strnicmp(at, level, want) == 0)
				return true;

			at = stop;
		}

		return false;
	}

	/* The level stem a .MAP path names: "...\DATA\RICH2.MAP" -> "RICH2". Empty
	 * for no path. Only ever asked for the level-bound hand-table lines, so a
	 * path spelled some other way costs those lines and nothing else. */
	std::string stem_of_path(const char* mapPath)
	{
		if (!mapPath)
			return std::string();

		const char* name = mapPath;

		for (const char* at = mapPath; *at; at++)
			if (*at == '\\' || *at == '/')
				name = at + 1;

		const char* dot = strrchr(name, '.');

		return dot ? std::string(name, dot) : std::string(name);
	}

	/* The EXTRA prefix exclusions. Every one of these sits in the same
	 * block of the object enum as the two shell casings (game\objects.h,
	 * SMOKE_EMITTER_WHITE .. LENS_FLARE) but, unlike them, is never a
	 * standalone level item with its own picture: it is a light, a smoke or
	 * mist puff, or a sprite drawn as part of another draw call.
	 * GUN_FLASH is the one line here that is a measurement and not caution -
	 * 4\ITEM\EXTRA_GUN_FLASH.TRM exists, and TROne's own source calls this
	 * whole family "Effect category". The measurement is not a bracket to
	 * tear, though: SetGunFlash (game\tomb4fx.cpp) only ever records a
	 * matrix, called from draw.cpp after DrawLara's own node walk has
	 * already returned, and the flash itself is painted afterwards by
	 * DrawGunflashes - a flat phd_PutPolygons call against the classic
	 * GUN_FLASH mesh, outside any HDObjectBegin/HDObjectEnd bracket and
	 * outside the whole per-item HD path this file feeds. There is
	 * therefore no bracket for an HD flash to open in the middle of, and no
	 * tear it could cause - GUN_FLASH simply never reaches this file's path
	 * at all, so a bound name would sit unused and cost nothing but a page.
	 * Excluded anyway, the same caution as the twelve light/mist slots
	 * beside it: nothing here reads Lara's draw code to notice if a future
	 * engine change ever routed the flash through an item bracket, and the
	 * cost of excluding a slot that would have been harmless is one unused
	 * page, not a bug. The shell casings (GUNSHELL, SHOTGUNSHELL) are not
	 * here - they have their own EXTRA_ model (4\ITEM\EXTRA_GUNSHELL.TRM,
	 * EXTRA_SHOTGUNSHELL.TRM, both measured) and stay in the pool. Measured
	 * with the same care as GUN_FLASH, though, and the same answer comes
	 * back: they are not level items either. DrawGunshells (game\tomb4fx.cpp)
	 * walks its own Gunshells[24] table and paints
	 * meshes[objects[..].mesh_index] with phd_PutPolygons, from draw.cpp
	 * outside any HDObjectBegin bracket - so their bound model is built and
	 * never drawn, exactly as GUN_FLASH's would be. FLARE_ITEM and
	 * CROSSBOW_BOLT are the same case one step further out: game\setup.cpp
	 * gives them a draw_routine of their own (DrawFlareInAir,
	 * DrawWeaponMissile) instead of DrawAnimatingItem, and both paint the
	 * classic mesh. Their model costs a vertex buffer and, on the shipped
	 * bundle, no page of its own - EXTRA_GUNSHELL shares page 8523 with the
	 * uzi, EXTRA_SHOTGUNSHELL page 8534 with the shotgun ammo and the
	 * crowbar, EXTRA_FLARE_ITEM page 8519 with EXTRA_FLARE_INV_ITEM, which
	 * is an ordinary pickup and does draw. Left in the pool: moving them
	 * here would be a behaviour change. Nothing below GUN_FLASH had a
	 * matching file on the installed bundle at the time of this
	 * measurement; they are excluded anyway so that a future remaster
	 * patch adding one does not silently start drawing something nobody
	 * asked for. */
	const char* const extra_excluded[] =
	{
		"GUN_FLASH",
		"SMOKE_EMITTER_WHITE", "SMOKE_EMITTER_BLACK", "STEAM_EMITTER",
		"EARTHQUAKE", "BUBBLES", "WATERFALLMIST",
		"COLOURED_LIGHT", "BLINKING_LIGHT", "PULSE_LIGHT", "STROBE_LIGHT",
		"ELECTRICAL_LIGHT", "LENS_FLARE"
	};

	bool extra_is_excluded(const char* name)
	{
		for (const char* one : extra_excluded)
			if (_stricmp(one, name) == 0)
				return true;

		return false;
	}

	/* Existence only, cheap - no read beyond the file system's own stat. Kept
	 * as its own copy of find_model's 5-then-4 rule (hd_model.cpp) rather than
	 * a call into it: this file is built without the engine, for the
	 * diagnostic tools TrgProbe and MapProbe (see the file header), and
	 * neither drags in hd_model.cpp's Direct3D dependency for six lines. */
	bool file_exists(const std::string& path)
	{
		// Out of one listing of the folder; opened where it cannot be listed.
		const int listed = HDFolderHas(path, false);

		if (listed >= 0)
			return listed != 0;

		std::FILE* file = std::fopen(path.c_str(), "rb");

		if (!file)
			return false;

		std::fclose(file);
		return true;
	}

	std::string bind_join(const std::string& a, const char* b)
	{
		std::string out = a;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		out += '\\';
		out += b;
		return out;
	}

	std::string bind_parent_of(const std::string& path)
	{
		std::string out = path;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		const size_t cut = out.find_last_of("\\/");

		return cut == std::string::npos ? std::string(".") : out.substr(0, cut);
	}

	/* 5\ITEM, then 4\ITEM - the same order find_model loads by. `name` already
	 * carries its EXTRA5_/EXTRA_/EXTRA4_ prefix. */
	bool extra_model_exists(const std::string& installRoot, const std::string& name)
	{
		/* A mod counts as there. This rule picks the prefix
		 * tier a pickup's model is named under, and it picks by whether the
		 * file exists - so a mod that brings an EXTRA_ model the install does
		 * not have would be looked for under the wrong name without this.
		 * Asked before installRoot is even checked, because a mod's file is
		 * in the mod and does not need an installation behind it. */
		{
			char found[512];
			int mod = -1;

			if (*HDModsFindModel(name.c_str(), &mod, found, sizeof(found)))
				return true;
		}

		if (installRoot.empty())
			return false;

		std::string path = bind_join(bind_join(installRoot, "ITEM"), (name + ".TRM").c_str());

		if (file_exists(path))
			return true;

		const std::string bundle = bind_parent_of(installRoot);

		path = bind_join(bind_join(bind_join(bundle, "4"), "ITEM"), (name + ".TRM").c_str());

		return file_exists(path);
	}

	/* The answer's storage, for the one rule (3, the EXTRA prefix) whose
	 * result is not a pointer into something that already lives somewhere -
	 * the level's map, the hand table, the slot name table.
	 *
	 * A single shared std::string, overwritten on every call, would be
	 * safe for a caller who reads the pointer before asking again, which is
	 * what HDLOG bind and every probe tool do - but HDObjectLevelBuild's
	 * donor pass does not: it resolves every donor slot's
	 * `DonorPlan::model` first and reads them all back later, once bind
	 * poses are built (hd_object.cpp). A shared buffer would then hold
	 * whichever slot was asked last, and any donor slot that hit rule 3
	 * anywhere in between (which is what an undonated pickup like
	 * PISTOLS_ITEM always does - no shipped .MAP names a weapon pickup)
	 * would have its name silently swapped for a different slot's. A cache
	 * keyed by slot avoids this the same way rules 1, 2 and 4 are already
	 * safe: each slot's answer lives in its own map node, which - unlike a
	 * std::string's - std::map never moves or reuses once written, so a
	 * pointer taken from it stays correct no matter how many other slots
	 * are asked afterwards. `root` records which installRoot the entry was
	 * computed for, since the disk it was asked against could in principle
	 * change; a mismatch recomputes rather than trusting a stale answer.
	 * Deliberately not cleared by HDMapFree/HDDonorMapsFree: rule 3 never
	 * reads the level's or a donor's .MAP, only the slot's fixed name and
	 * the fixed set of files under installRoot, so the same answer is
	 * correct for the next level too and there is no reason to re-stat the
	 * disk for it. */
	struct AltAnswer
	{
		std::string root;	// installRoot this answer was resolved against
		std::string model;	// empty + via NONE = every prefix tier missed
		HDBindVia via = HD_BIND_VIA_NONE;
	};
	std::map<int, AltAnswer> g_alt_cache;

	/* The guests out of Tomb Raider III. The header says what this is and
	 * how the four names were arrived at; the numbers on the left are TR5
	 * slots the randomizer installs a guest into, chosen because no vanilla
	 * level uses them.
	 *
	 * The bird needs a second look: the randomizer takes it out of THE LOST
	 * ARTIFACT's chunnel.TR2, not out of Tomb Raider III itself - the
	 * game's own vulture flies in the desert levels and the one the
	 * randomizer ships is the Gold bird
	 * (TR5Randomizer\src\core\Randomizer.cpp, donor "tr3gold", and
	 * Config.h: "chunnel.TR2 carries the vulture"). The remaster keeps the
	 * two apart under exactly that rule: 3\ITEM of the fresh Tomb Raider
	 * I-III Remastered has VULTURE.TRM and VULTURE_CHUNNEL.TRM side by
	 * side, and it is the second that belongs to this guest. */
	struct Tr3Guest { int slot; const char* model; };

	const Tr3Guest tr3_guests[] =
	{
		{ 31, "TREX"             },	// SAS
		{ 43, "VULTURE_CHUNNEL"  },	// CROW - see below
		{ 53, "COMPY"            },	// SAILOR
		{ 75, "RAPTOR"           }	// REAPER
	};

	/* LARA .. HAIR. hd_level.cpp checks this against the enum. */
	const int lara_block_last = 30;

	struct MapEntry { std::string key, value; };
	std::vector<MapEntry> map_entries;
	std::string map_level;	// the stem map_entries belong to, "" for none

	/* The donor maps, kept apart from the level's own -
	 * the level's is asked for every slot of every frame's worth of building,
	 * and a donor's only for the handful of slots the sidecar names. One
	 * entry per donor level, read once and kept until the level ends. */
	struct DonorMap
	{
		std::string level;
		bool read;			// the file was there
		std::vector<MapEntry> entries;
	};

	/* POINTERS, not values. HDBindDonorSlot hands back a pointer INTO one of
	 * these maps and the caller keeps it until it has built its slot list, by
	 * which time other donors have been asked for - and a std::vector that
	 * grows moves its elements, which would leave the earlier answers
	 * pointing at freed strings. Each map is allocated once and never moves;
	 * HDDonorMapsFree gives them back. */
	std::vector<DonorMap*> donor_maps;

	bool same(const std::string& a, const char* b)
	{
		return _stricmp(a.c_str(), b) == 0;
	}

	/* KEY=VALUE a line. The format is TRForge's measurement and the reasons
	 * for every step of it stand at HDMapLoad, which called this its body
	 * until the donor maps needed the same parse against a store of their
	 * own. */
	bool read_map_file(const char* mapPath, std::vector<MapEntry>& out)
	{
		out.clear();

		if (!mapPath || !*mapPath)
			return false;

		std::FILE* file = std::fopen(mapPath, "rb");

		if (!file)
			return false;

		std::string text;
		char chunk[4096];
		size_t got;

		while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
			text.append(chunk, got);

		std::fclose(file);

		size_t at = 0;

		while (at < text.size())
		{
			size_t end = text.find('\n', at);

			if (end == std::string::npos)
				end = text.size();

			std::string line = text.substr(at, end - at);
			at = end + 1;

			/* The last line of several shipped files has no line break at all
			 * (JOBY4 ends on CHAFF=PUZZLE_ITEM2_5), so trailing whitespace is
			 * stripped rather than assumed away. */
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
				line.pop_back();

			if (line.empty())
				continue;

			const size_t equals = line.find('=');

			if (equals == std::string::npos)
				continue;

			MapEntry entry;

			entry.key = line.substr(0, equals);
			entry.value = line.substr(equals + 1);
			out.push_back(entry);
		}

		return true;
	}

	const char* value_in(const std::vector<MapEntry>& entries, const char* key)
	{
		if (!key)
			return 0;

		for (const MapEntry& entry : entries)
			if (same(entry.key, key))
				return entry.value.c_str();

		return 0;
	}

	/* The one binding rule, against WHICHEVER map is in hand: the map wins,
	 * then the hand table, then the EXTRA prefix, then the slot's own name.
	 * HDBindSlot asks it with the level's map and HDBindDonorSlot with a
	 * donor's, which is the whole of what those two differ in.
	 *
	 * `installRoot` may be empty - passing none still gets rules 1, 2 and 4
	 * exactly as before; only rule 3 needs a disk to ask. `level` is the
	 * stem `entries` belong to - the level's own or the donor's - and may
	 * be 0 or empty, in which case every level-bound hand-table line
	 * misses. */
	const char* bind_against(const std::vector<MapEntry>& entries,
		const std::string& installRoot, const char* level, int slot,
		HDBindKind* kind, bool* fromMap, HDBindVia* via)
	{
		HDBindKind local_kind = HD_BIND_NONE;
		bool local_from_map = false;
		HDBindVia local_via = HD_BIND_VIA_NONE;
		const char* result = 0;

		const char* name = HDSlotName(slot);

		if (!name)
			local_kind = HD_BIND_NONE;
		else if (HDSlotIsLara(slot))
			local_kind = HD_BIND_LARA;
		else
		{
			const char* mapped = value_in(entries, name);

			if (mapped)
			{
				local_from_map = true;
				local_via = HD_BIND_VIA_MAP;

				if (_stricmp(mapped, "DUMMY") == 0)
					local_kind = HD_BIND_DUMMY;
				else
				{
					local_kind = HD_BIND_MODEL;
					result = mapped;

					/* A map line whose file is in no ITEM folder gives way
					 * to a hand-table line bound to this level, if there is
					 * one: JOBY3's DOG_MIP=DOG_MIP_JOBY3_JOBY5 names a file
					 * the remaster does not ship, and the suit top it stands
					 * for is EXTRA5_SUBSUIT_TOP_CHAIN_JOBY5 (the hand table,
					 * "The submarine's diving suits"). Lines with no
					 * level list are not asked: they name this project's own
					 * slots, which no map mentions. Without a disk to ask,
					 * the map's word stands. */
					if (!installRoot.empty() &&
						!extra_model_exists(installRoot, std::string(mapped)))
						for (const HandBound& bound : hand_table)
							if (bound.levels && _stricmp(bound.slot, name) == 0 &&
								hand_applies(bound.levels, level))
							{
								result = bound.model;
								local_via = HD_BIND_VIA_HAND;
								local_from_map = false;
								break;
							}
				}
			}
			else
			{
				local_kind = HD_BIND_MODEL;
				result = name;
				local_via = HD_BIND_VIA_CLASSIC;

				bool hand_hit = false;

				for (const HandBound& bound : hand_table)
					if (_stricmp(bound.slot, name) == 0 && hand_applies(bound.levels, level))
					{
						result = bound.model;
						local_via = HD_BIND_VIA_HAND;
						hand_hit = true;
						break;
					}

				/* Rule 3, the EXTRA prefix - only when neither the map nor
				 * the hand table had an answer, the slot is not one of the
				 * excluded ones, and a disk was given to ask. See
				 * hd_bind.h for the order and hd_bind.cpp's extra_excluded
				 * table for what is left out and why.
				 *
				 * Answered from `g_alt_cache`, one entry per slot, kept for
				 * as long as the process runs rather than rebuilt on every
				 * call - see the cache's own comment above for why a
				 * shared scratch buffer would be a stale-pointer bug in a
				 * caller that keeps the answer past the next bind_against
				 * call, which the donor pass always does. */
				if (!hand_hit && !installRoot.empty() && !extra_is_excluded(name))
				{
					auto found = g_alt_cache.find(slot);

					if (found == g_alt_cache.end() || found->second.root != installRoot)
					{
						AltAnswer answer;

						answer.root = installRoot;

						for (const PrefixRule& one : extra_prefixes)
						{
							std::string candidate = one.prefix;
							candidate += name;

							if (extra_model_exists(installRoot, candidate))
							{
								answer.model = candidate;
								answer.via = one.via;
								break;
							}
						}

						found = g_alt_cache.insert_or_assign(slot, std::move(answer)).first;
					}

					if (found->second.via != HD_BIND_VIA_NONE)
					{
						result = found->second.model.c_str();
						local_via = found->second.via;
					}
				}
			}
		}

		if (kind)
			*kind = local_kind;
		if (fromMap)
			*fromMap = local_from_map;
		if (via)
			*via = local_via;

		return result;
	}
}

const char* HDSlotName(int slot)
{
	if (slot < 0 || slot >= HD_SLOT_COUNT)
		return 0;
	return slot_names[slot];
}

bool HDSlotIsLara(int slot)
{
	return slot >= 0 && slot <= lara_block_last;
}

/* hd_bind.h carries the rule; HDBindModOption asks the same test. A name
 * that is only "_ITEM" has no base to build an OPTION name on and is no
 * slot of this engine either, so it is refused here as well. */
bool HDSlotIsPickup(int slot)
{
	const char* name = HDSlotName(slot);

	if (!name || HDSlotIsLara(slot))
		return false;

	static const char kItem[] = "_ITEM";
	const size_t length = strlen(name);
	const size_t tail = sizeof(kItem) - 1;

	return length > tail && _stricmp(name + length - tail, kItem) == 0;
}

bool HDMapLoad(const char* mapPath)
{
	HDMapFree();
	map_level = stem_of_path(mapPath);
	return read_map_file(mapPath, map_entries);
}

bool HDMapLoadForLevel(const char* installRoot, const char* stem)
{
	HDMapFree();

	if (!installRoot || !*installRoot || !stem || !*stem)
		return false;

	map_level = stem;

	std::string path = installRoot;

	if (path[path.size() - 1] != '\\' && path[path.size() - 1] != '/')
		path += '\\';

	path += "DATA\\";
	path += stem;
	path += ".MAP";

	return read_map_file(path.c_str(), map_entries);
}

void HDMapFree()
{
	map_entries.clear();
	map_level.clear();
}

int HDMapCount()
{
	return (int)map_entries.size();
}

const char* HDMapValue(const char* key)
{
	return value_in(map_entries, key);
}

const char* HDMapLevel()
{
	return map_level.c_str();
}

const char* HDBindSlot(const char* installRoot, int slot, HDBindKind* kind,
	bool* fromMap, HDBindVia* via)
{
	return bind_against(map_entries, installRoot ? installRoot : "",
		map_level.c_str(), slot, kind, fromMap, via);
}

int HDBindDummyHost(int slot)
{
	const char* name = HDSlotName(slot);
	const char* mapped = name ? value_in(map_entries, name) : 0;

	if (!mapped || _stricmp(mapped, "DUMMY") != 0)
		return -1;

	for (const DummyHost& line : dummy_hosts)
	{
		if (_stricmp(line.slot, name) != 0 ||
			!hand_applies(line.levels, map_level.c_str()))
			continue;

		return slot_of_name(line.host);
	}

	return -1;
}

int HDBindGuestStates(int host, HDGuestState* out, int max)
{
	const char* name = HDSlotName(host);
	int count = 0;

	if (!name || !out)
		return 0;

	for (const DummyHost& line : dummy_hosts)
	{
		if (count >= max || !line.done || !line.model ||
			_stricmp(line.host, name) != 0 ||
			!hand_applies(line.levels, map_level.c_str()))
			continue;

		const char* mapped = value_in(map_entries, line.slot);

		// The guests are DUMMY in the level's own map, or no guests at all.
		if (!mapped || _stricmp(mapped, "DUMMY") != 0)
			continue;

		out[count].guest = slot_of_name(line.slot);
		out[count].done = slot_of_name(line.done);
		out[count].model = line.model;

		if (out[count].guest >= 0 && out[count].done >= 0)
			count++;
	}

	return count;
}

const char* HDBindGuestDone(int guest, const char** model)
{
	const char* name = HDSlotName(guest);

	if (model)
		*model = 0;

	if (!name)
		return 0;

	for (const DummyHost& line : dummy_hosts)
		if (line.done && _stricmp(line.slot, name) == 0 &&
			hand_applies(line.levels, map_level.c_str()))
		{
			if (model)
				*model = line.model;

			return line.done;
		}

	return 0;
}

const char* HDBindDonorSlot(const char* installRoot, const char* donorLevel,
	int sourceSlot, HDBindKind* kind, bool* fromMap, HDBindVia* via)
{
	static const std::vector<MapEntry> nothing;
	const std::string root = installRoot ? installRoot : "";

	if (!donorLevel || !*donorLevel)
		return bind_against(nothing, root, "", sourceSlot, kind, fromMap, via);

	DonorMap* have = 0;

	for (DonorMap* one : donor_maps)
		if (_stricmp(one->level.c_str(), donorLevel) == 0)
		{
			have = one;
			break;
		}

	if (!have)
	{
		DonorMap& fresh = *new DonorMap;

		fresh.level = donorLevel;
		fresh.read = false;

		/* "<installRoot>\DATA\<LEVEL>.MAP" - the same place hd_scan.cpp
		 * builds the level's own path from. The stem is spelled as the
		 * sidecar spells it ("Andrea1") and the files are upper case, which
		 * costs nothing: fopen on Windows does not care. */
		if (installRoot && *installRoot)
		{
			std::string path = installRoot;

			if (!path.empty() && path[path.size() - 1] != '\\' && path[path.size() - 1] != '/')
				path += '\\';

			path += "DATA\\";
			path += donorLevel;
			path += ".MAP";

			fresh.read = read_map_file(path.c_str(), fresh.entries);
		}

		donor_maps.push_back(&fresh);
		have = &fresh;
	}

	return bind_against(have->entries, root, donorLevel, sourceSlot, kind,
		fromMap, via);
}

void HDDonorMapsFree()
{
	for (DonorMap* one : donor_maps)
		delete one;

	donor_maps.clear();
}

int HDDonorMapCount()
{
	int count = 0;

	for (const DonorMap* one : donor_maps)
		if (one->read)
			count++;

	return count;
}

int HDDonorMapEntries()
{
	int count = 0;

	for (const DonorMap* one : donor_maps)
		count += (int)one->entries.size();

	return count;
}

const char* HDBindTr3Guest(int tr5Slot)
{
	for (const Tr3Guest& guest : tr3_guests)
		if (guest.slot == tr5Slot)
			return guest.model;

	return 0;
}

const char* HDBindModOption(int slot, char* buffer, int size)
{
	static const char* nothing = "";

	if (!buffer || size <= 0)
		return nothing;

	*buffer = 0;

	/* The ending is the whole rule - HDSlotIsPickup, which is where it is
	 * written down, because the floor rule asks the same question of the
	 * same slots and two spellings of one rule are one too many. */
	if (!HDSlotIsPickup(slot))
		return nothing;

	const char* name = HDSlotName(slot);
	static const char kItem[] = "_ITEM";
	const size_t length = strlen(name);
	const size_t tail = sizeof(kItem) - 1;

	/* The option model is asked for first, and the order is a measurement.
	 *
	 * Each of these questions walks every active mod's 5\ITEM and 4\ITEM, and
	 * this function is asked of every loaded slot at every level build - the
	 * shape of thing that can turn a level build into twenty-five seconds.
	 * Asking the three EXTRA prefixes first meant four walks for every
	 * _ITEM slot in the level; asking the one name that decides anything first
	 * means one walk for all but the handful that really have an OPTION model.
	 * Measured over ANDREA1's 130 slots with nine mods installed: 24.6 ms
	 * the other way round, 7.7 ms this way (the median of five), and 0.11 ms
	 * with no mods at all. */
	std::string option(name, length - tail);
	char found[512];
	int mod = -1;

	option += "_OPTION";

	if (!*HDModsFindModel(option.c_str(), &mod, found, sizeof(found)))
		return nothing;

	/* A mod's own EXTRA_ model wins. It is the IV-VI name, so a mod that
	 * ships one has said which of the two conventions it is written in, and
	 * the ordinary resolver already draws it - here and on the floor. */
	for (const PrefixRule& one : extra_prefixes)
	{
		std::string candidate = one.prefix;

		candidate += name;

		if (*HDModsFindModel(candidate.c_str(), &mod, found, sizeof(found)))
			return nothing;
	}

	if ((int)option.size() >= size)
		return nothing;

	memcpy(buffer, option.c_str(), option.size() + 1);
	return buffer;
}

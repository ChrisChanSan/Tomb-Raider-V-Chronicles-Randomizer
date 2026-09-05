/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The LEVELn.MAP format - KEY=VALUE a line, CRLF, no sections and no comments
 * in any shipped file - is TRForge's measurement (MIT, trf_remaster\Remaster.h,
 * load_object_map). The reader below is written against that description
 * rather than copied, because TRForge's own file drags in its level library.
 * See vendor\trforge\VERSION.txt.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_bind.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
	"MESHSWAP_CROSS_GUARD2", "MESHSWAP_CROSS_HITMAN", "CYBORG", "MESHSWAP_CROSS_CRANEGUY"
	};

	static_assert(sizeof(slot_names) / sizeof(slot_names[0]) == HD_SLOT_COUNT,
		"HD_SLOT_COUNT and the generated slot name table have drifted apart");

	/* THE HAND TABLE for the slots this project added beyond the original 460
	 * (#105, #111, #146). The remaster has all of these models; what it does
	 * not have is our slot numbers, because no vanilla level uses them.
	 *
	 * Each line is a measurement waiting to be confirmed by picture in stage
	 * S7 - which model the remaster really draws for these is only certain
	 * once it is on screen. Two of them are already certain from the shipped
	 * .MAP files: the joby levels say BLUE_GUARD=BLUE_GUARD_JOBY, ANDREA3
	 * says ROMAN_GOD=ROMAN_GOD_ANDREA_HAMMER.
	 *
	 * The MESHSWAP_CROSS_* slots are warehouses: they never hold an item, and
	 * a mesh swap takes single meshes out of them. Binding them to the model
	 * of the family they belong to is what makes those meshes findable at
	 * all; whether the HD model's mesh numbering lines up with the classic
	 * one is a stage S3 question and is not answered here. */
	struct HandBound { const char* slot; const char* model; };

	const HandBound hand_table[] =
	{
		{ "BLUE_GUARD2",             "BLUE_GUARD_JOBY"          },
		{ "ROMAN_GOD2",              "ROMAN_GOD_ANDREA_HAMMER"  },
		{ "CYBORG",                  "HITMAN"                   },
		{ "MESHSWAP_CROSS_SWAT",     "SWAT"                     },
		{ "MESHSWAP_CROSS_SWATPLUS", "SWAT_PLUS"                },
		{ "MESHSWAP_CROSS_TWOGUN",   "TWOGUN"                   },
		{ "MESHSWAP_CROSS_GUARD",    "BLUE_GUARD"               },
		{ "MESHSWAP_CROSS_MAFIA",    "MAFIA"                    },
		{ "MESHSWAP_CROSS_GUARD2",   "BLUE_GUARD_JOBY"          },
		{ "MESHSWAP_CROSS_HITMAN",   "HITMAN"                   },
		{ "MESHSWAP_CROSS_CRANEGUY", "CRANE_GUY"                }
	};

	/* LARA .. HAIR. hd_level.cpp checks this against the enum. */
	const int lara_block_last = 30;

	struct MapEntry { std::string key, value; };
	std::vector<MapEntry> map_entries;

	bool same(const std::string& a, const char* b)
	{
		return _stricmp(a.c_str(), b) == 0;
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

bool HDMapLoad(const char* mapPath)
{
	HDMapFree();

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
		map_entries.push_back(entry);
	}
	return true;
}

void HDMapFree()
{
	map_entries.clear();
}

int HDMapCount()
{
	return (int)map_entries.size();
}

const char* HDMapValue(const char* key)
{
	if (!key)
		return 0;
	for (const MapEntry& entry : map_entries)
		if (same(entry.key, key))
			return entry.value.c_str();
	return 0;
}

const char* HDBindSlot(int slot, HDBindKind* kind, bool* fromMap)
{
	HDBindKind local_kind = HD_BIND_NONE;
	bool local_from_map = false;
	const char* result = 0;

	const char* name = HDSlotName(slot);

	if (!name)
		local_kind = HD_BIND_NONE;
	else if (HDSlotIsLara(slot))
		local_kind = HD_BIND_LARA;
	else
	{
		const char* mapped = HDMapValue(name);

		if (mapped)
		{
			local_from_map = true;
			if (_stricmp(mapped, "DUMMY") == 0)
				local_kind = HD_BIND_DUMMY;
			else
			{
				local_kind = HD_BIND_MODEL;
				result = mapped;
			}
		}
		else
		{
			local_kind = HD_BIND_MODEL;
			result = name;

			for (const HandBound& bound : hand_table)
				if (_stricmp(bound.slot, name) == 0)
				{
					result = bound.model;
					break;
				}
		}
	}

	if (kind)
		*kind = local_kind;
	if (fromMap)
		*fromMap = local_from_map;
	return result;
}

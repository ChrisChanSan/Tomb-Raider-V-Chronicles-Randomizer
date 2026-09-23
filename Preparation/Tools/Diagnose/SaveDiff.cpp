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

/* SaveDiff - compares two TOMB5 savegames field by field and names what
 * differs.
 *
 * The simulation steps thirty times a second regardless of display rate,
 * and the draw path's own RNG (`rand_2`, GetRandomDraw) never feeds back
 * into it. So the same level, seed and inputs must reach the same game
 * state at 30 and at 60 fps - a savegame is that state written down, and
 * two savegames byte for byte identical is the proof. Recipe: play the same
 * short, repeatable stretch at each frame rate into the same save slot,
 * then run SaveDiff <save A> <save B> (full steps in docs\Engine-Manual.md).
 *
 * "IDENTICAL" means the two runs matched; otherwise every differing field is
 * printed by name with both values. A human replay never matches exactly -
 * clock fields, `save_counter` and Lara's position drift a little - so what
 * matters is whether the simulation's shape (kills, ammo, hit points,
 * flags, secrets) held.
 *
 * The field table is built from the engine's own header (global\types.h)
 * via offsetof, so it tracks the engine's layout; built for Win32 because
 * the structs are `#pragma pack(1)` but carry pointers, four bytes in the
 * engine.
 */

#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <mmeapi.h>
#include <mmreg.h>
#include <d3d9.h>
#include <dinput.h>
#include <dsound.h>

#include "../../../TOMB5 Code/TOMB5/global/types.h"

/* The file layout, exactly as S_SaveGame writes it (specific\gamemain.cpp):
 *
 *   75  the level's name, zero padded
 *    4  SaveCounter
 *    2  days      (Game.Timer / 30 / 86400)
 *    2  hours
 *    2  minutes
 *    2  seconds
 *   ..  SAVEGAME_INFO
 *    7  tomb5_save_info
 */
#define OFF_NAME	0
#define OFF_COUNTER	75
#define OFF_DAYS	79
#define OFF_HOURS	81
#define OFF_MINUTES	83
#define OFF_SECONDS	85
#define OFF_SAVEGAME	87
#define OFF_TOMB5	(OFF_SAVEGAME + (int)sizeof(SAVEGAME_INFO))
#define SIZE_EXPECTED	(OFF_TOMB5 + (int)sizeof(tomb5_save_info))

enum FieldKind
{
	F_BYTES,	// shown as hex, byte by byte
	F_TEXT,		// shown as a string
	F_U,		// unsigned decimal
	F_S		// signed decimal
};

struct Field
{
	const char* name;
	int offset;	// in the FILE
	int size;
	int kind;
};

#define SG(member)	(OFF_SAVEGAME + (int)offsetof(SAVEGAME_INFO, member))
#define LARA(member)	(SG(Lara) + (int)offsetof(LARA_INFO, member))
#define STAT(which, member) (SG(which) + (int)offsetof(STATS, member))
#define ARM(side, member) (LARA(side) + (int)offsetof(LARA_ARM, member))
#define T5(member)	(OFF_TOMB5 + (int)offsetof(tomb5_save_info, member))

static const Field kField[] =
{
	{ "level_name",			OFF_NAME,	75, F_TEXT },
	{ "save_counter",		OFF_COUNTER,	4,  F_U },
	{ "clock_days",			OFF_DAYS,	2,  F_U },
	{ "clock_hours",		OFF_HOURS,	2,  F_U },
	{ "clock_minutes",		OFF_MINUTES,	2,  F_U },
	{ "clock_seconds",		OFF_SECONDS,	2,  F_U },

	{ "Checksum",			SG(Checksum),	2,  F_S },
	{ "AutoTarget",			SG(AutoTarget),	1,  F_U },

	/* --- Lara, in the order of the struct ---------------------------- */
	{ "Lara.item_number",		LARA(item_number),	2, F_S },
	{ "Lara.gun_status",		LARA(gun_status),	2, F_S },
	{ "Lara.gun_type",		LARA(gun_type),		2, F_S },
	{ "Lara.request_gun_type",	LARA(request_gun_type),	2, F_S },
	{ "Lara.last_gun_type",		LARA(last_gun_type),	2, F_S },
	{ "Lara.calc_fallspeed",	LARA(calc_fallspeed),	2, F_S },
	{ "Lara.water_status",		LARA(water_status),	2, F_S },
	{ "Lara.climb_status",		LARA(climb_status),	2, F_S },
	{ "Lara.pose_count",		LARA(pose_count),	2, F_S },
	{ "Lara.hit_frame",		LARA(hit_frame),	2, F_S },
	{ "Lara.hit_direction",		LARA(hit_direction),	2, F_S },
	{ "Lara.air",			LARA(air),		2, F_S },
	{ "Lara.dive_count",		LARA(dive_count),	2, F_S },
	{ "Lara.death_count",		LARA(death_count),	2, F_S },
	{ "Lara.current_active",	LARA(current_active),	2, F_S },
	{ "Lara.current_xvel",		LARA(current_xvel),	2, F_S },
	{ "Lara.current_yvel",		LARA(current_yvel),	2, F_S },
	{ "Lara.current_zvel",		LARA(current_zvel),	2, F_S },
	{ "Lara.spaz_effect_count",	LARA(spaz_effect_count),2, F_S },
	{ "Lara.flare_age",		LARA(flare_age),	2, F_S },
	{ "Lara.BurnCount",		LARA(BurnCount),	2, F_S },
	{ "Lara.weapon_item",		LARA(weapon_item),	2, F_S },
	{ "Lara.back_gun",		LARA(back_gun),		2, F_S },
	{ "Lara.flare_frame",		LARA(flare_frame),	2, F_S },
	{ "Lara.poisoned",		LARA(poisoned),		2, F_S },
	{ "Lara.dpoisoned",		LARA(dpoisoned),	2, F_S },
	{ "Lara.Anxiety",		LARA(Anxiety),		1, F_U },
	{ "Lara.wet",			LARA(wet),		15, F_BYTES },
	// The sixteen one-bit flags (look, burn, IsDucked, Busy, ...) live in
	// the two bytes between Anxiety's array and water_surface_dist. offsetof
	// cannot name a bit field, so they are compared as the word they are.
	{ "Lara.flags16",		LARA(wet) + 15,		2, F_BYTES },
	{ "Lara.water_surface_dist",	LARA(water_surface_dist),4, F_S },
	{ "Lara.last_pos",		LARA(last_pos),		12, F_BYTES },
	{ "Lara.spaz_effect",		LARA(spaz_effect),	4, F_U },
	{ "Lara.mesh_effects",		LARA(mesh_effects),	4, F_U },
	{ "Lara.mesh_ptrs",		LARA(mesh_ptrs),	60, F_BYTES },
	{ "Lara.target",		LARA(target),		4, F_U },
	{ "Lara.target_angles",		LARA(target_angles),	4, F_BYTES },
	{ "Lara.turn_rate",		LARA(turn_rate),	2, F_S },
	{ "Lara.move_angle",		LARA(move_angle),	2, F_S },
	{ "Lara.head_y_rot",		LARA(head_y_rot),	2, F_S },
	{ "Lara.head_x_rot",		LARA(head_x_rot),	2, F_S },
	{ "Lara.head_z_rot",		LARA(head_z_rot),	2, F_S },
	{ "Lara.torso_y_rot",		LARA(torso_y_rot),	2, F_S },
	{ "Lara.torso_x_rot",		LARA(torso_x_rot),	2, F_S },
	{ "Lara.torso_z_rot",		LARA(torso_z_rot),	2, F_S },
	{ "Lara.left_arm.frame_base",	ARM(left_arm, frame_base),	4, F_U },
	{ "Lara.left_arm.frame_number",	ARM(left_arm, frame_number),	2, F_S },
	{ "Lara.left_arm.anim_number",	ARM(left_arm, anim_number),	2, F_S },
	{ "Lara.left_arm.lock",		ARM(left_arm, lock),		2, F_S },
	{ "Lara.left_arm.y_rot",	ARM(left_arm, y_rot),		2, F_S },
	{ "Lara.left_arm.x_rot",	ARM(left_arm, x_rot),		2, F_S },
	{ "Lara.left_arm.z_rot",	ARM(left_arm, z_rot),		2, F_S },
	{ "Lara.left_arm.flash_gun",	ARM(left_arm, flash_gun),	2, F_S },
	{ "Lara.right_arm.frame_base",	ARM(right_arm, frame_base),	4, F_U },
	{ "Lara.right_arm.frame_number",ARM(right_arm, frame_number),	2, F_S },
	{ "Lara.right_arm.anim_number",	ARM(right_arm, anim_number),	2, F_S },
	{ "Lara.right_arm.lock",	ARM(right_arm, lock),		2, F_S },
	{ "Lara.right_arm.y_rot",	ARM(right_arm, y_rot),		2, F_S },
	{ "Lara.right_arm.x_rot",	ARM(right_arm, x_rot),		2, F_S },
	{ "Lara.right_arm.z_rot",	ARM(right_arm, z_rot),		2, F_S },
	{ "Lara.right_arm.flash_gun",	ARM(right_arm, flash_gun),	2, F_S },
	{ "Lara.holster",		LARA(holster),		2, F_U },
	{ "Lara.creature",		LARA(creature),		4, F_U },
	{ "Lara.CornerX",		LARA(CornerX),		4, F_S },
	{ "Lara.CornerZ",		LARA(CornerZ),		4, F_S },
	{ "Lara.RopeSegment",		LARA(RopeSegment),	1, F_S },
	{ "Lara.RopeDirection",		LARA(RopeDirection),	1, F_S },
	{ "Lara.RopeArcFront",		LARA(RopeArcFront),	2, F_S },
	{ "Lara.RopeArcBack",		LARA(RopeArcBack),	2, F_S },
	{ "Lara.RopeLastX",		LARA(RopeLastX),	2, F_S },
	{ "Lara.RopeMaxXForward",	LARA(RopeMaxXForward),	2, F_S },
	{ "Lara.RopeMaxXBackward",	LARA(RopeMaxXBackward),	2, F_S },
	{ "Lara.RopeDFrame",		LARA(RopeDFrame),	4, F_S },
	{ "Lara.RopeFrame",		LARA(RopeFrame),	4, F_S },
	{ "Lara.RopeFrameRate",		LARA(RopeFrameRate),	2, F_U },
	{ "Lara.RopeY",			LARA(RopeY),		2, F_U },
	{ "Lara.RopePtr",		LARA(RopePtr),		4, F_S },
	{ "Lara.GeneralPtr",		LARA(GeneralPtr),	4, F_U },
	{ "Lara.RopeOffset",		LARA(RopeOffset),	4, F_S },
	{ "Lara.RopeDownVel",		LARA(RopeDownVel),	4, F_U },
	{ "Lara.RopeFlag",		LARA(RopeFlag),		1, F_S },
	{ "Lara.MoveCount",		LARA(MoveCount),	1, F_S },
	{ "Lara.RopeCount",		LARA(RopeCount),	4, F_S },
	{ "Lara.skelebob",		LARA(skelebob),		1, F_S },
	{ "Lara.pistols_type_carried",	LARA(pistols_type_carried),	1, F_S },
	{ "Lara.uzis_type_carried",	LARA(uzis_type_carried),	1, F_S },
	{ "Lara.shotgun_type_carried",	LARA(shotgun_type_carried),	1, F_S },
	{ "Lara.crossbow_type_carried",	LARA(crossbow_type_carried),	1, F_S },
	{ "Lara.hk_type_carried",	LARA(hk_type_carried),		1, F_S },
	{ "Lara.sixshooter_type_carried",LARA(sixshooter_type_carried),	1, F_S },
	{ "Lara.lasersight",		LARA(lasersight),	1, F_S },
	{ "Lara.silencer",		LARA(silencer),		1, F_S },
	{ "Lara.binoculars",		LARA(binoculars),	1, F_S },
	{ "Lara.crowbar",		LARA(crowbar),		1, F_S },
	{ "Lara.examine1",		LARA(examine1),		1, F_S },
	{ "Lara.examine2",		LARA(examine2),		1, F_S },
	{ "Lara.examine3",		LARA(examine3),		1, F_S },
	{ "Lara.wetcloth",		LARA(wetcloth),		1, F_S },
	{ "Lara.bottle",		LARA(bottle),		1, F_S },
	{ "Lara.puzzleitems",		LARA(puzzleitems),	12, F_BYTES },
	{ "Lara.puzzleitemscombo",	LARA(puzzleitemscombo),	2, F_U },
	{ "Lara.keyitems",		LARA(keyitems),		8, F_BYTES },
	{ "Lara.keyitemscombo",		LARA(keyitemscombo),	2, F_U },
	{ "Lara.pickupitems",		LARA(pickupitems),	2, F_U },
	{ "Lara.pickupitemscombo",	LARA(pickupitemscombo),	2, F_U },
	{ "Lara.num_small_medipack",	LARA(num_small_medipack),	2, F_S },
	{ "Lara.num_large_medipack",	LARA(num_large_medipack),	2, F_S },
	{ "Lara.num_flares",		LARA(num_flares),	2, F_S },
	{ "Lara.num_pistols_ammo",	LARA(num_pistols_ammo),	2, F_S },
	{ "Lara.num_uzi_ammo",		LARA(num_uzi_ammo),	2, F_S },
	{ "Lara.num_revolver_ammo",	LARA(num_revolver_ammo),2, F_S },
	{ "Lara.num_shotgun_ammo1",	LARA(num_shotgun_ammo1),2, F_S },
	{ "Lara.num_shotgun_ammo2",	LARA(num_shotgun_ammo2),2, F_S },
	{ "Lara.num_hk_ammo1",		LARA(num_hk_ammo1),	2, F_S },
	{ "Lara.num_crossbow_ammo1",	LARA(num_crossbow_ammo1),2, F_S },
	{ "Lara.num_crossbow_ammo2",	LARA(num_crossbow_ammo2),2, F_S },
	{ "Lara.location",		LARA(location),		1, F_S },
	{ "Lara.highest_location",	LARA(highest_location),	1, F_S },
	{ "Lara.locationPad",		LARA(locationPad),	1, F_S },
	{ "Lara.TightRopeOnCount",	LARA(TightRopeOnCount),	1, F_U },
	{ "Lara.TightRopeOff",		LARA(TightRopeOff),	1, F_U },
	{ "Lara.TightRopeFall",		LARA(TightRopeFall),	1, F_U },
	{ "Lara.ChaffTimer",		LARA(ChaffTimer),	1, F_U },

	/* --- the two stat blocks. Level.Timer is the 30 Hz proof ---------- */
	{ "Level.Timer",		STAT(Level, Timer),	4, F_U },
	{ "Level.Distance",		STAT(Level, Distance),	4, F_U },
	{ "Level.AmmoUsed",		STAT(Level, AmmoUsed),	4, F_U },
	{ "Level.AmmoHits",		STAT(Level, AmmoHits),	4, F_U },
	{ "Level.Kills",		STAT(Level, Kills),	2, F_U },
	{ "Level.Secrets",		STAT(Level, Secrets),	1, F_U },
	{ "Level.HealthUsed",		STAT(Level, HealthUsed),1, F_U },
	{ "Game.Timer",			STAT(Game, Timer),	4, F_U },
	{ "Game.Distance",		STAT(Game, Distance),	4, F_U },
	{ "Game.AmmoUsed",		STAT(Game, AmmoUsed),	4, F_U },
	{ "Game.AmmoHits",		STAT(Game, AmmoHits),	4, F_U },
	{ "Game.Kills",			STAT(Game, Kills),	2, F_U },
	{ "Game.Secrets",		STAT(Game, Secrets),	1, F_U },
	{ "Game.HealthUsed",		STAT(Game, HealthUsed),	1, F_U },

	{ "WeaponObject",		SG(WeaponObject),	2, F_S },
	{ "WeaponAnim",			SG(WeaponAnim),		2, F_S },
	{ "WeaponFrame",		SG(WeaponFrame),	2, F_S },
	{ "WeaponCurrent",		SG(WeaponCurrent),	2, F_S },
	{ "WeaponGoal",			SG(WeaponGoal),		2, F_S },
	{ "CutSceneTriggered1",		SG(CutSceneTriggered1),	4, F_U },
	{ "CutSceneTriggered2",		SG(CutSceneTriggered2),	4, F_U },
	{ "GameComplete",		SG(GameComplete),	1, F_S },
	{ "CurrentLevel",		SG(CurrentLevel),	1, F_U },
	{ "CampaignSecrets",		SG(CampaignSecrets),	4, F_BYTES },
	{ "TLCount",			SG(TLCount),		1, F_U },

	{ "tomb5.LHolster",		T5(LHolster),		2, F_U },
	{ "tomb5.dash_timer",		T5(dash_timer),		2, F_U },
	{ "tomb5.ledge_phase",		T5(ledge_phase),	1, F_U },
	{ "tomb5.ledge_heading",	T5(ledge_heading),	2, F_S }
};

static const int kFieldCount = (int)(sizeof(kField) / sizeof(kField[0]));

static unsigned char* Slurp(const char* path, long* size)
{
	FILE* f = 0;
	unsigned char* data;
	long n;

	if (fopen_s(&f, path, "rb") || !f)
		return 0;

	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);

	data = (unsigned char*)malloc(n > 0 ? n : 1);

	if (!data || (n > 0 && (long)fread(data, 1, n, f) != n))
	{
		free(data);
		fclose(f);
		return 0;
	}

	fclose(f);
	*size = n;
	return data;
}

static unsigned __int64 ReadU(const unsigned char* p, int size)
{
	unsigned __int64 v = 0;

	for (int i = size - 1; i >= 0; i--)
		v = (v << 8) | p[i];

	return v;
}

static __int64 ReadS(const unsigned char* p, int size)
{
	unsigned __int64 v = ReadU(p, size);
	const unsigned __int64 sign = 1ull << (size * 8 - 1);

	if (v & sign)
		return (__int64)v - (__int64)(sign << 1);

	return (__int64)v;
}

static void ShowBytes(const unsigned char* p, int size, char* out, int cap)
{
	int at = 0;

	for (int i = 0; i < size && at + 3 < cap; i++)
		at += sprintf_s(out + at, cap - at, "%02X", p[i]);

	out[at] = 0;
}

static void ShowText(const unsigned char* p, int size, char* out, int cap)
{
	int n = 0;

	while (n < size && n < cap - 1 && p[n])
	{
		out[n] = (p[n] >= 32 && p[n] < 127) ? (char)p[n] : '.';
		n++;
	}

	out[n] = 0;
}

static void Show(const Field* f, const unsigned char* base, char* out, int cap)
{
	const unsigned char* p = base + f->offset;

	switch (f->kind)
	{
	case F_TEXT:	ShowText(p, f->size, out, cap); break;
	case F_U:	sprintf_s(out, cap, "%llu", ReadU(p, f->size)); break;
	case F_S:	sprintf_s(out, cap, "%lld", ReadS(p, f->size)); break;
	default:	ShowBytes(p, f->size, out, cap); break;
	}
}

int main(int argc, char** argv)
{
	unsigned char* a;
	unsigned char* b;
	long sa = 0, sb = 0;
	int named = 0, stream = 0;
	int buffer_at, buffer_size;

	printf("SaveDiff - two TOMB5 savegames, field by field\n");
	printf("layout: SAVEGAME_INFO=%d bytes, whole file=%d bytes\n\n",
		(int)sizeof(SAVEGAME_INFO), SIZE_EXPECTED);

	if (argc < 3)
	{
		printf("usage: SaveDiff <save A> <save B>\n\n"
			"The savegames live in the game folder under savegame\\ and are\n"
			"called savegame.0 .. savegame.14 - slot 1 is savegame.0.\n");
		return 2;
	}

	a = Slurp(argv[1], &sa);

	if (!a)
	{
		printf("cannot read %s\n", argv[1]);
		return 2;
	}

	b = Slurp(argv[2], &sb);

	if (!b)
	{
		printf("cannot read %s\n", argv[2]);
		free(a);
		return 2;
	}

	printf("A %s  %ld bytes\n", argv[1], sa);
	printf("B %s  %ld bytes\n\n", argv[2], sb);

	if (sa != sb)
	{
		printf("DIFFERENT LENGTH. One of the two was written by another build\n"
			"of the engine - sizeof(SAVEGAME_INFO) is part of the file, and\n"
			"the engine rejects a save of the wrong length on load as well.\n");
		free(a);
		free(b);
		return 1;
	}

	if (sa != SIZE_EXPECTED)
		printf("NOTE: %ld bytes, but this build's layout says %d. The field\n"
			"names below are therefore not to be trusted - rebuild SaveDiff\n"
			"against the engine that wrote these files.\n\n", sa,
			SIZE_EXPECTED);

	if (!memcmp(a, b, sa))
	{
		printf("IDENTICAL - every one of the %ld bytes.\n", sa);
		free(a);
		free(b);
		return 0;
	}

	for (int i = 0; i < kFieldCount; i++)
	{
		const Field* f = &kField[i];
		char va[256], vb[256];

		if (f->offset + f->size > sa)
			continue;

		if (!memcmp(a + f->offset, b + f->offset, f->size))
			continue;

		Show(f, a, va, sizeof(va));
		Show(f, b, vb, sizeof(vb));
		printf("%-32s A=%-20s B=%s\n", f->name, va, vb);
		named++;
	}

	/* The savegame stream: SAVEGAME_INFO::buffer is not a struct, it is what
	 * WriteSG (game\savegame.cpp) packed into it - the items, the flip maps,
	 * the cameras, the spot cams, every item's position and flags, in a
	 * variable-length and conditional order that only that file knows. There
	 * is no honest way to name a field inside it from here, so what is
	 * reported is where it differs and by how much, in runs. */
	buffer_at = SG(buffer);
	buffer_size = (int)sizeof(((SAVEGAME_INFO*)0)->buffer);

	if (buffer_at + buffer_size <= sa)
	{
		int at = 0;

		while (at < buffer_size)
		{
			int run;

			if (a[buffer_at + at] == b[buffer_at + at])
			{
				at++;
				continue;
			}

			run = 0;

			while (at + run < buffer_size &&
				a[buffer_at + at + run] != b[buffer_at + at + run])
				run++;

			if (stream < 40)
				printf("savegame stream            +%-7d %d byte%s differ "
					"(A=%02X B=%02X ...)\n", at, run, run == 1 ? "" : "s",
					a[buffer_at + at], b[buffer_at + at]);

			stream++;
			at += run;
		}

		if (stream > 40)
			printf("... and %d more runs in the savegame stream\n",
				stream - 40);
	}

	printf("\n%d named field%s and %d run%s in the savegame stream differ.\n",
		named, named == 1 ? "" : "s", stream, stream == 1 ? "" : "s");
	printf("The savegame stream is written by WriteSG in game\\savegame.cpp -\n"
		"items, flip maps, cameras and spot cams, in that order.\n");

	free(a);
	free(b);
	return 1;
}

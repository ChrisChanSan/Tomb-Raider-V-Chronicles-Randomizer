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
#include "../tomb5/pch.h"
#include "savegame.h"
#include "objects.h"
#include "traps.h"
#include "items.h"
#include "rope.h"
#include "control.h"
#include "pickup.h"
#include "lot.h"
#include "gameflow.h"
#include "interpolate.h"		// the loaded world is no neighbour of the old one
#include "newinv2.h"
#include "../specific/file.h"
#include "camera.h"
#include "spotcam.h"
#include "../specific/function_stubs.h"
#include "switch.h"
#include "lara.h"
#include "rat.h"
#include "bat.h"
#include "spider.h"
#include "lara2gun.h"
#include "save_pointer_rule.h"	// the save's two pointers into the pool
#include "../specific/drawroom.h"	// mesh_vtxbuf - Lara's meshes by number
#include "../global/disk_layout.h"	// Lara in the save's 32-bit layout

SAVEGAME_INFO savegame;
static char* SGpoint = 0;
static long SGcount = 0;

tomb5_save_info tomb5_save;
ulong tomb5_save_size;
ulong savegame_bytes_read;
bool  savegame_checksum_ok;	// built in S_LoadGame, see there

/* The first four bytes of savegame.buffer: the format of the item block
 * behind them. The file's size did not change when the animation went from
 * a byte to a short, so the size check cannot tell an older save; the mark
 * can. An older save holds FmvSceneTriggered there, which nothing sets any
 * more (fmv.cpp), so it reads 0. A new mark for every change of the block.
 * Format 2: the animation as a short (#331). Format 3: a body that lies
 * there is written whole (SAVE_PACKED_BODY below, #333). */
#define SAVE_FORMAT_MARK	0x03355653	// "SV5" and format 3

/* The item block's word for a body that lies there: both the "killed" bit
 * (0x2000) and the "written" bit (0x8000). Format 2 never wrote the two
 * together - the killed branch writes 0x2000 alone, the other branch never
 * sets 0x2000 - so the pair is free for it. */
#define SAVE_PACKED_BODY	0xA000

// The 22 bytes a save writes of a creature end with the four of its enemy's
// field, which save_pointer_rule.h fills - a long, whatever the width of the
// pointer behind it.
static_assert(offsetof(CREATURE_INFO, enemy) + sizeof(long) == 22,
	"the savegame's 22 bytes of CREATURE_INFO end with enemy's field");

/* An address as save_pointer_rule.h reads it: whole. */
static SavePtrAddr SaveAddress(const void* p)
{
	return (SavePtrAddr)(uintptr_t)p;
}

static_assert(MAX_LOT <= SAVE_PTR_SLOTS_MAX,
	"save_pointer_rule.h holds an item number for every creature slot");

/* Which item each creature slot holds now, as save_pointer_rule.h reads
 * it: a save names an ai_target by its creature's item, and a load finds
 * the slot that item holds in this run - asked again before each enemy
 * is decoded, since a load deals the slots one creature at a time. */
static void SavePointerSlots(SavePtrPool* pool)
{
	for (int i = 0; i < SAVE_PTR_SLOTS_MAX; i++)
		pool->slotItem[i] = (baddie_slots && i < MAX_LOT) ?
			baddie_slots[i].item_num : (short)-1;
}

/* The game_malloc pool as save_pointer_rule.h reads it: this level's
 * items[] and baddie_slots, their distances from the pool's start. */
static SavePtrPool SavePointerPool()
{
	SavePtrPool pool;

	pool.base = SaveAddress(malloc_buffer);
	pool.size = (unsigned long)malloc_size;	// + the Asan build's red zones
	pool.items = (unsigned long)(SaveAddress(items) - pool.base);
	pool.itemSize = sizeof(ITEM_INFO);
	pool.maxItems = MAX_ITEMS;
	pool.slots = (unsigned long)(SaveAddress(baddie_slots) - pool.base);
	pool.slotSize = sizeof(CREATURE_INFO);
	pool.maxSlots = MAX_LOT;
	pool.aiOffset = offsetof(CREATURE_INFO, ai_target);
	SavePointerSlots(&pool);
	return pool;
}

/* SAVELOG's pool line: the pool and its layout at a save or a load. */
static void SavePointerPoolLog(const char* when, const SavePtrPool* pool)
{
	char line[128];

	if (!gLogEnabled)
		return;

	SavePtrPoolLine(line, sizeof(line), when, pool);
	Log("%s", line);
}

void WriteSG(void* pointer, long size)
{
	char* data;

	SGcount += size;

	// The original TR5 engine (unlike TR2's - see TR2Main's WriteSG, which
	// preserves the original game's own "SG_Count >= sizeof(buffer) ->
	// S_ExitSystem(...)" guard) has no protection here at all: a level with
	// enough active/triggered items to exceed savegame.buffer's fixed size
	// silently writes past its end into whatever memory follows it. Cross-
	// checked directly against TR2Main before adding this - same fail-fast
	// philosophy (stop immediately with a clear message, rather than let a
	// save silently corrupt or crash somewhere unrelated later), adapted to
	// this engine's own idioms (Log()/MessageBox instead of S_ExitSystem).
	// Deliberately does NOT resize savegame.buffer itself - that would
	// change sizeof(SAVEGAME_INFO) and break compatibility with existing
	// save files (see game/savegame.cpp's WriteFile/ReadFile call sites,
	// which serialize the whole struct by sizeof()) - a separate, bigger
	// decision.
	if (SGcount >= (long)sizeof(savegame.buffer))
	{
		Log("FATAL: savegame buffer overflow - SGcount=%d, buffer size=%d", SGcount, (int)sizeof(savegame.buffer));

		char msg[256];
		sprintf(msg, "Savegame data is too large to fit in the save buffer (%d / %d bytes).\r\n\r\nThe game cannot continue safely and must close.",
			SGcount, (int)sizeof(savegame.buffer));
		MessageBox(0, msg, "Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	for (data = (char*)pointer; size > 0; size--)
	{
		*SGpoint = *data;
		data++;
		SGpoint++;
	}
}

void ReadSG(void* pointer, long size)
{
	char* data;

	SGcount += size;

	for (data = (char*)pointer; size > 0; size--)
	{
		*data = *SGpoint;
		data++;
		SGpoint++;
	}
}

static void save_tomb5_data()
{
	tomb5_save.LHolster = LHolster;
	tomb5_save.dash_timer = DashTimer;
	LaraLedgeSave(&tomb5_save.ledge_phase, &tomb5_save.ledge_heading);
}

static void load_tomb5_data()
{
	if (tomb5_save_size <= offsetof(tomb5_save_info, LHolster))
		LHolster = lara.holster;
	else
		LHolster = tomb5_save.LHolster;

	if (tomb5_save_size > offsetof(tomb5_save_info, dash_timer))
		DashTimer = tomb5_save.dash_timer;

	LaraLedgeLoad(tomb5_save.ledge_phase, tomb5_save_size > offsetof(tomb5_save_info, ledge_phase),
		tomb5_save.ledge_heading, tomb5_save_size > offsetof(tomb5_save_info, ledge_heading));
}

/* One of Lara's mesh pointers as the save holds it: its number in
 * mesh_vtxbuf under the mesh tag (save_pointer_rule.h), or - in no table -
 * its distance from mesh_base, as every save held it before. */
static uint32_t SaveLaraMesh(short* mesh)
{
	for (long k = 0; k < num_level_meshes; k++)
	{
		if ((short*)mesh_vtxbuf[k] == mesh)
			return SavePtrMeshField(k, 0);
	}

	return SavePtrMeshField(-1, (unsigned long)((uintptr_t)mesh - (uintptr_t)mesh_base));
}

/* The mesh of Lara's joint `joint` a load gives her: by number out of this
 * level's table; a distance as before; a number the level does not have,
 * her own mesh of that joint. */
static short* LoadLaraMesh(uint32_t field, int joint)
{
	const long k = SavePtrMeshNumber(field, num_level_meshes);

	if (k >= 0)
		return (short*)mesh_vtxbuf[k];

	if (k == -1)
		return (short*)((char*)mesh_base + (int32_t)field);

	return meshes[objects[LARA].mesh_index + 2 * joint];
}

/* An arm's frame_base as the save holds it: its byte distance from the
 * pistols' frames, in the frames block on either build; and back. */
static uint32_t SaveArmFrames(short* frame_base)
{
	return (uint32_t)((uintptr_t)frame_base - (uintptr_t)objects[PISTOLS_ANIM].frame_base);
}

static short* LoadArmFrames(uint32_t field)
{
	return (short*)((char*)objects[PISTOLS_ANIM].frame_base + (int32_t)field);
}

void SaveLaraData()
{
	ITEM_INFO* item;
	SavePtrPool pool;
	SavePtrAddr general;
	unsigned long side;
	long field;

	/* Lara goes into the save in the 32-bit layout (LARA_INFO_SAVE,
	 * types.h): every byte that is no pointer as it is, each pointer as a
	 * four-byte value no address goes into - the meshes by number, the
	 * arms' frames by distance, spaz_effect, target and creature, which
	 * every load sets to null, as 0. lara's own values are never touched. */
	LaraCopyRuns(&lara, &savegame.Lara, 1);
	savegame.Lara.spaz_effect = 0;
	savegame.Lara.target = 0;
	savegame.Lara.creature = 0;

	for (int i = 0; i < 15; i++)
		savegame.Lara.mesh_ptrs[i] = SaveLaraMesh(lara.mesh_ptrs[i]);

	savegame.Lara.left_arm.frame_base = SaveArmFrames(lara.left_arm.frame_base);
	savegame.Lara.right_arm.frame_base = SaveArmFrames(lara.right_arm.frame_base);

	/* GeneralPtr goes into the copy by save_pointer_rule.h, and its item's
	 * number beside it into the copy's target, which every load sets to 0;
	 * lara's own value is never touched. */
	pool = SavePointerPool();
	general = SaveAddress(lara.GeneralPtr);
	field = SavePtrGeneralField(&pool, general);
	side = SavePtrGeneralSide(&pool, general, field);
	savegame.Lara.GeneralPtr = (uint32_t)field;

	if (side)
		savegame.Lara.target = (uint32_t)side;

	if (gLogEnabled)
		Log("SAVELOG save general stored=0x%08lX side=0x%08lX%s", (unsigned long)field, side,
			SavePtrOutside(general, field) ? " kind=outside" : "");

	if (lara.weapon_item != NO_ITEM)
	{
		item = &items[lara.weapon_item];
		savegame.WeaponObject = item->object_number;
		savegame.WeaponAnim = item->anim_number;
		savegame.WeaponFrame = item->frame_number;
		savegame.WeaponCurrent = item->current_anim_state;
		savegame.WeaponGoal = item->goal_anim_state;
	}

	savegame.CutSceneTriggered1 = _CutSceneTriggered1;
	savegame.CutSceneTriggered2 = _CutSceneTriggered2;
	save_tomb5_data();
}

void RestoreLaraData(long FullSave)
{
	ITEM_INFO* item;
	SavePtrPool pool;
	SavePtrDecoded general;
	char line[128];
	char flag;

	if (!FullSave)
	{
		savegame.Lara.item_number = lara.item_number;

		if (savegame.Lara.IsMoving)
		{
			savegame.Lara.IsMoving = 0;
			savegame.Lara.gun_status = LG_NO_ARMS;
		}
	}

	/* Lara out of the save's 32-bit layout: the bytes that are no pointer
	 * as they are, each pointer built here; target, spaz_effect and
	 * creature are null after a load. */
	LaraCopyRuns(&lara, &savegame.Lara, 0);
	lara.target = 0;
	lara.spaz_effect = 0;
	lara.creature = 0;
	lara.left_arm.frame_base = LoadArmFrames(savegame.Lara.left_arm.frame_base);
	lara.right_arm.frame_base = LoadArmFrames(savegame.Lara.right_arm.frame_base);

	/* GeneralPtr out of the field and the item number beside it in the save's
	 * target (save_pointer_rule.h): built from this run's items[], or 0. */
	pool = SavePointerPool();
	general = SavePtrDecodeGeneral(&pool, (long)savegame.Lara.GeneralPtr, savegame.Lara.target);
	lara.GeneralPtr = (void*)(uintptr_t)general.value;

	if (gLogEnabled)
	{
		SavePtrGeneralLine(line, sizeof(line), (long)savegame.Lara.GeneralPtr, savegame.Lara.target, &general);
		Log("%s", line);
	}

	if (lara.burn)
	{
		flag = 0;
		lara.burn = 0;

		if (lara.BurnSmoke)
		{
			lara.BurnSmoke = 0;
			flag = 1;
		}

		LaraBurn();

		if (flag)
			lara.BurnSmoke = 1;
	}

	if (lara.weapon_item != -1)
	{
		lara.weapon_item = CreateItem();
		item = &items[lara.weapon_item];
		item->object_number = savegame.WeaponObject;
		item->anim_number = savegame.WeaponAnim;
		item->frame_number = savegame.WeaponFrame;
		item->current_anim_state = savegame.WeaponCurrent;
		item->goal_anim_state = savegame.WeaponGoal;
		item->status = ITEM_ACTIVE;
		item->room_number = NO_ROOM;
	}

	for (int i = 0; i < 15; i++)
		lara.mesh_ptrs[i] = LoadLaraMesh(savegame.Lara.mesh_ptrs[i], i);

	_CutSceneTriggered1 = savegame.CutSceneTriggered1;
	_CutSceneTriggered2 = savegame.CutSceneTriggered2;
	load_tomb5_data();
}

/* A dead enemy that lies there (#333). CreatureDie (box.cpp) sets
 * IFL_CLEARBODY on it, takes it off the active chain, frees its AI slot and
 * drops what it carried, but leaves it in its room's item list, where the
 * room draws it while `after_death` counts 1..127 (draw.cpp's
 * StepAfterDeathFade kills it at 128; the Corpses switch off holds it at 1).
 * Format 2 wrote it as killed and the load ran KillItem on it: the body was
 * gone after a load in either setting. A killed item carries the same flag,
 * but KillItem has taken it out of its room's list, and an enemy shredded by
 * CreatureDie(.., 1) can keep the count it had (the skeleton's rise timer):
 * the list is what tells the two apart. Neither the writer nor the reader
 * looks at the switch: a body saved fading goes on fading after a load with
 * the switch on, and lies there with it off, as it would have in play. */
static bool SaveBodyLies(short item_number)
{
	ITEM_INFO* item = &items[item_number];

	if (!(item->flags & IFL_CLEARBODY) || item->after_death < 1 ||
		item->after_death > 127 || item->room_number < 0 ||
		item->room_number >= number_rooms)
		return false;

	for (short n = room[item->room_number].item_number; n != NO_ITEM;
		n = items[n].next_item)
	{
		if (n == item_number)
			return true;
	}

	return false;
}

/* The flag bits and the bit fields, packed as the long the other items'
 * branch writes (without its creature bit: CreatureDie freed the data). */
static ulong SaveBodyFlags(ITEM_INFO* item)
{
	ulong flags = (ushort)item->flags;

	flags |= item->active << 16;
	flags |= item->status << 17;
	flags |= item->gravity_status << 19;
	flags |= item->hit_status << 20;
	flags |= item->collidable << 21;
	flags |= item->looked_at << 22;
	flags |= item->dynamic_light << 23;
	flags |= item->poisoned << 24;
	flags |= item->ai_bits << 25;
	flags |= item->really_active << 30;
	return flags;
}

/* After SAVE_PACKED_BODY: everything the body is drawn and tested by, at
 * full width, whatever the object's save_* switches say (a body needs them
 * all; 56 bytes). */
static void SaveBody(ITEM_INFO* item, OBJECT_INFO* obj)
{
	ulong flags;
	short rel;
	uchar byte;

	WriteSG(&item->pos.x_pos, sizeof(long));
	WriteSG(&item->pos.y_pos, sizeof(long));
	WriteSG(&item->pos.z_pos, sizeof(long));
	WriteSG(&item->room_number, sizeof(short));
	WriteSG(&item->pos.x_rot, sizeof(short));
	WriteSG(&item->pos.y_rot, sizeof(short));
	WriteSG(&item->pos.z_rot, sizeof(short));
	byte = (uchar)item->current_anim_state;
	WriteSG(&byte, sizeof(uchar));
	byte = (uchar)item->goal_anim_state;
	WriteSG(&byte, sizeof(uchar));
	byte = (uchar)item->required_anim_state;
	WriteSG(&byte, sizeof(uchar));
	rel = item->anim_number - obj->anim_index;	// as format 2's short
	WriteSG(&rel, sizeof(short));
	WriteSG(&item->frame_number, sizeof(short));
	byte = (uchar)item->after_death;	// 1..127 (SaveBodyLies): a byte
	WriteSG(&byte, sizeof(uchar));
	WriteSG(&item->hit_points, sizeof(short));
	flags = SaveBodyFlags(item);
	WriteSG(&flags, sizeof(ulong));
	WriteSG(item->item_flags, 4 * sizeof(short));
	WriteSG(&item->timer, sizeof(short));
	WriteSG(&item->trigger_flags, sizeof(short));
	WriteSG(&item->carried_item, sizeof(short));
	WriteSG(&item->mesh_bits, sizeof(ulong));
	WriteSG(&item->meshswap_meshbits, sizeof(ulong));

	if (gLogEnabled)
		Log("SAVELOG body save item=%d after_death=%d room=%d",
			(int)(item - items), (int)item->after_death,
			(int)item->room_number);
}

/* SaveBody's fields back onto the item the level load set up. The body
 * stays off the active chain (InitialiseItem may have put a triggered enemy
 * there) and gets no AI slot: no EnableBaddieAI, no CreatureDie, so the
 * pickups it dropped before the save are not dropped again (they are items
 * of their own, written in their own place). Its room's list keeps it, as
 * CreatureDie left it. */
static void RestoreBody(short item_number, ITEM_INFO* item, OBJECT_INFO* obj)
{
	FLOOR_INFO* floor;
	ulong flags;
	short rel, room_number;
	uchar byte;

	ReadSG(&item->pos.x_pos, sizeof(long));
	ReadSG(&item->pos.y_pos, sizeof(long));
	ReadSG(&item->pos.z_pos, sizeof(long));
	ReadSG(&room_number, sizeof(short));
	ReadSG(&item->pos.x_rot, sizeof(short));
	ReadSG(&item->pos.y_rot, sizeof(short));
	ReadSG(&item->pos.z_rot, sizeof(short));
	ReadSG(&byte, sizeof(uchar));
	item->current_anim_state = byte;
	ReadSG(&byte, sizeof(uchar));
	item->goal_anim_state = byte;
	ReadSG(&byte, sizeof(uchar));
	item->required_anim_state = byte;
	ReadSG(&rel, sizeof(short));
	item->anim_number = obj->anim_index + rel;
	ReadSG(&item->frame_number, sizeof(short));
	ReadSG(&byte, sizeof(uchar));
	item->after_death = byte;
	ReadSG(&item->hit_points, sizeof(short));
	ReadSG(&flags, sizeof(ulong));
	ReadSG(item->item_flags, 4 * sizeof(short));
	ReadSG(&item->timer, sizeof(short));
	ReadSG(&item->trigger_flags, sizeof(short));
	ReadSG(&item->carried_item, sizeof(short));
	ReadSG(&item->mesh_bits, sizeof(ulong));
	ReadSG(&item->meshswap_meshbits, sizeof(ulong));

	if (item->room_number != room_number)
		ItemNewRoom(item_number, room_number);

	RemoveActiveItem(item_number);	// returns at once if not on it
	item->flags = (short)flags;
	item->active = 0;
	item->status = (flags >> 17) & 3;
	item->gravity_status = (flags >> 19) & 1;
	item->hit_status = (flags >> 20) & 1;
	item->collidable = 0;			// as CreatureDie left it
	item->looked_at = (flags >> 22) & 1;
	item->dynamic_light = (flags >> 23) & 1;
	item->poisoned = (flags >> 24) & 1;
	item->ai_bits = (flags >> 25) & 31;
	item->really_active = (flags >> 30) & 1;
	item->speed = 0;
	item->fallspeed = 0;
	item->data = 0;				// as DisableBaddieAI left it

	if (obj->shadow_size)
	{
		floor = GetFloor(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos,
			&room_number);
		item->floor = GetHeight(floor, item->pos.x_pos, item->pos.y_pos,
			item->pos.z_pos);
	}

	if (gLogEnabled)
		Log("SAVELOG body load item=%d after_death=%d room=%d",
			(int)item_number, (int)item->after_death,
			(int)item->room_number);
}

void SaveLevelData(long FullSave)
{
	ITEM_INFO* item;
	ROOM_INFO* r;
	OBJECT_INFO* obj;
	RAT_STRUCT* rat;
	BAT_STRUCT* bat;
	MESH_INFO* mesh;
	CREATURE_INFO* creature;
	SavePtrPool pool;
	ulong flags;
	SavePtrAddr enemy, ownAi;
	long k, flare_age, field;
	int kind;
	bool outside;
	ushort packed;
	short pos, word, objnum, maxval;
	uchar byte;
	char lflags;
	char head[22];	// a creature's head as the save writes it

	pool = SavePointerPool();
	SavePointerPoolLog("save", &pool);
	WriteSG(&FmvSceneTriggered, sizeof(long));
	WriteSG(&GLOBAL_lastinvitem, sizeof(long));
	word = 0;

	for (int i = 0; i < 16; i++)
	{
		if (flip_stats[i])
			word |= (1 << i);
	}

	WriteSG(&word, sizeof(short));

	for (int i = 0; i < 255; i++)
	{
		word = (short)(flipmap[i] >> 8);
		WriteSG(&word, sizeof(short));
	}

	WriteSG(&flipeffect, sizeof(long));
	WriteSG(&fliptimer, sizeof(long));
	WriteSG(&flip_status, sizeof(long));
	WriteSG(cd_flags, 136);
	WriteSG(&CurrentAtmosphere, sizeof(uchar));
	word = 0;
	k = 0;

	for (int i = 0; i < number_rooms; i++)
	{
		r = &room[i];

		for (int j = 0; j < r->num_meshes; j++)
		{
			mesh = &r->mesh[j];

			if (mesh->static_number >= 50 && mesh->static_number <= 59)
			{
				word |= ((mesh->Flags & 1) << k);
				k++;

				if (k == 16)
				{
					WriteSG(&word, sizeof(short));
					k = 0;
					word = 0;
				}
			}
		}
	}

	if (k)
		WriteSG(&word, sizeof(short));

	WriteSG(&CurrentSequence, sizeof(uchar));
	byte = 0;

	for (int i = 0; i < 6; i++)
		byte |= SequenceUsed[i] << i;

	WriteSG(&byte, sizeof(uchar));
	WriteSG(Sequences, 3);

	for (int i = 0; i < number_cameras; i++)
		WriteSG(&camera.fixed[i].flags, sizeof(short));

	for (int i = 0; i < number_spotcams; i++)
		WriteSG(&SpotCam[i].flags, sizeof(short));

	for (int i = 0; i < level_items; i++)
	{
		item = &items[i];
		obj = &objects[item->object_number];
		packed = 0;

		if (item->flags & -0x8000)
		{
			if (SaveBodyLies((short)i))
			{
				packed = SAVE_PACKED_BODY;
				WriteSG(&packed, sizeof(ushort));
				SaveBody(item, obj);
			}
			else
			{
				packed = 0x2000;
				WriteSG(&packed, sizeof(ushort));
			}
		}
		else
		{
			if (item->flags & (IFL_CODEBITS | IFL_INVISIBLE | IFL_TRIGGERED) || item->object_number == LARA && FullSave)
			{
				packed = 0x8000;

				if (item->pos.x_rot)
					packed |= 1;

				if (item->pos.z_rot)
					packed |= 2;

				if (item->pos.x_pos & 1)
					packed |= 4;

				if (item->pos.y_pos & 1)
					packed |= 8;

				if (item->pos.z_pos & 1)
					packed |= 0x10;

				if (item->speed)
					packed |= 0x20;

				if (item->fallspeed)
					packed |= 0x40;

				if (item->item_flags[0])
					packed |= 0x80;

				if (item->item_flags[1])
					packed |= 0x100;

				if (item->item_flags[2])
					packed |= 0x200;

				if (item->item_flags[3])
					packed |= 0x400;

				if (item->timer)
					packed |= 0x800;

				if (item->trigger_flags)
					packed |= 0x1000;

				if (obj->save_hitpoints)
					packed |= 0x4000;

				WriteSG(&packed, sizeof(ushort));

				if (obj->save_position)
				{
					pos = (short)(item->pos.x_pos >> 1);
					WriteSG(&pos, sizeof(short));
					pos = (short)(item->pos.y_pos >> 1);
					WriteSG(&pos, sizeof(short));
					pos = (short)(item->pos.z_pos >> 1);
					WriteSG(&pos, sizeof(short));
					byte = (uchar)item->room_number;
					WriteSG(&byte, sizeof(uchar));
					WriteSG(&item->pos.y_rot, sizeof(short));

					if (packed & 1)
						WriteSG(&item->pos.x_rot, sizeof(short));

					if (packed & 2)
						WriteSG(&item->pos.z_rot, sizeof(short));

					if (packed & 0x20)
						WriteSG(&item->speed, sizeof(short));

					if (packed & 0x40)
						WriteSG(&item->fallspeed, sizeof(short));
				}

				if (obj->save_anim)
				{
					byte = (uchar)(item->current_anim_state);
					WriteSG(&byte, sizeof(uchar));
					byte = (uchar)(item->goal_anim_state);
					WriteSG(&byte, sizeof(uchar));
					byte = (uchar)(item->required_anim_state);
					WriteSG(&byte, sizeof(uchar));

					/* A short relative to the object's own anim_index: a
					 * stealth cutscene (deltapak.cpp's stealth3_end) gives a
					 * guard SWAT's animation, hundreds of animations away,
					 * which a byte cut. SAVE_FORMAT_MARK says the item
					 * block holds the short. */
					if (item->object_number != LARA)
					{
						pos = item->anim_number - obj->anim_index;
						WriteSG(&pos, sizeof(short));
					}
					else
						WriteSG(&item->anim_number, sizeof(short));

					WriteSG(&item->frame_number, sizeof(short));
				}

				if (packed & 0x4000)
					WriteSG(&item->hit_points, sizeof(short));

				if (obj->save_flags)
				{
					//flags var has item->flags and the bitfield of the item. (flags | bitfield) EXCEPT FOR InDrawRoom, for some reason
					flags = item->flags;
				//	flags |= (*(ulong*)((char*)item + 0x15EA) & 0x7FFF) << 16;	//messy since we can't take bitfield address
					//doing it manually for clarity:
					flags |= item->active << 16;
					flags |= item->status << 17;
					flags |= item->gravity_status << 19;
					flags |= item->hit_status << 20;
					flags |= item->collidable << 21;
					flags |= item->looked_at << 22;
					flags |= item->dynamic_light << 23;
					flags |= item->poisoned << 24;
					flags |= item->ai_bits << 25;
					flags |= item->really_active << 30;

					if (obj->intelligent && item->data)
						flags |= 0x80000000;

					WriteSG(&flags, sizeof(ulong));

					if (packed & 0x80)
						WriteSG(&item->item_flags[0], sizeof(short));

					if (packed & 0x100)
						WriteSG(&item->item_flags[1], sizeof(short));

					if (packed & 0x200)
						WriteSG(&item->item_flags[2], sizeof(short));

					if (packed & 0x400)
						WriteSG(&item->item_flags[3], sizeof(short));

					if (packed & 0x800)
						WriteSG(&item->timer, sizeof(short));

					if (packed & 0x1000)
						WriteSG(&item->trigger_flags, sizeof(short));

					if (obj->intelligent || (item->object_number >= SEARCH_OBJECT1 && item->object_number <= SEARCH_OBJECT3))
						WriteSG(&item->carried_item, sizeof(short));

					if (flags & 0x80000000)
					{
						/* The enemy goes into a copy of the head by
						 * save_pointer_rule.h, who it was into bits 5 and 6
						 * of the LOT flags below; the creature itself is
						 * never touched. */
						creature = (CREATURE_INFO*)item->data;
						enemy = SaveAddress(creature->enemy);
						ownAi = SaveAddress(&creature->ai_target);
						field = SavePtrEnemyField(&pool, enemy);
						kind = SavePtrEnemyKind(enemy, SaveAddress(lara_item), ownAi);
						outside = SavePtrOutside(enemy, field);

						// What the rule cannot name leaves as null.
						if (outside)
							kind = SAVE_PTR_KIND_NULL;

						memcpy(head, creature, sizeof(head));
						memcpy(head + offsetof(CREATURE_INFO, enemy), &field, sizeof(field));
						WriteSG(head, sizeof(head));
						WriteSG(&creature->ai_target.object_number, sizeof(short));
						WriteSG(&creature->ai_target.room_number, sizeof(short));
						WriteSG(&creature->ai_target.box_number, sizeof(ushort));
						WriteSG(&creature->ai_target.flags, sizeof(short));
						WriteSG(&creature->ai_target.trigger_flags, sizeof(short));
						WriteSG(&creature->ai_target.pos, sizeof(PHD_3DPOS));
						lflags = creature->LOT.can_jump;
						lflags |= creature->LOT.can_monkey << 1;
						lflags |= creature->LOT.is_amphibious << 2;
						lflags |= creature->LOT.is_jumping << 3;
						lflags |= creature->LOT.is_monkeying << 4;
						lflags |= kind << SAVE_PTR_KIND_SHIFT;
						WriteSG(&lflags, sizeof(char));

						if (gLogEnabled)
							Log("SAVELOG save item=%d stored=0x%08lX kind=%s", i, (unsigned long)field,
								outside ? "outside" : SavePtrKindName(kind));
					}
				}

				if (obj->save_mesh)
				{
					WriteSG(&item->mesh_bits, sizeof(ulong));
					WriteSG(&item->meshswap_meshbits, sizeof(ulong));
				}
			}
			else
				WriteSG(&packed, sizeof(ushort));
		}
	}

	if (FullSave)
	{
		byte = 0;
		item = &items[level_items];

		for (int i = level_items; i < MAX_ITEMS; i++)
		{
			if (item->active && (item->object_number == FLARE_ITEM || item->object_number == BURNING_TORCH_ITEM ||
				item->object_number == CHAFF ||item->object_number == TORPEDO || item->object_number == CROSSBOW_BOLT))
				byte++;

			item++;
		}

		WriteSG(&byte, sizeof(uchar));
		item = &items[level_items];

		for (int i = level_items; i < MAX_ITEMS; i++)
		{
			if (item->active && (item->object_number == FLARE_ITEM || item->object_number == BURNING_TORCH_ITEM ||
				item->object_number == CHAFF || item->object_number == TORPEDO || item->object_number == CROSSBOW_BOLT))
			{
				if (item->object_number == FLARE_ITEM)
					byte = 0;
				else if (item->object_number == BURNING_TORCH_ITEM)
					byte = 1;
				else if (item->object_number == CHAFF)
					byte = 2;
				else if (item->object_number == TORPEDO)
					byte = 3;
				else
					byte = 4;

				WriteSG(&byte, sizeof(char));
				WriteSG(&item->pos, sizeof(PHD_3DPOS));
				WriteSG(&item->room_number, sizeof(short));
				WriteSG(&item->speed, sizeof(short));
				WriteSG(&item->fallspeed, sizeof(short));

				if (item->object_number == FLARE_ITEM)
				{
					flare_age = (long)(intptr_t)item->data;
					WriteSG(&flare_age, sizeof(long));
				}
				else if (item->object_number == BURNING_TORCH_ITEM)
					WriteSG(&item->item_flags[3], sizeof(short));
				else if (item->object_number == CHAFF || item->object_number == TORPEDO)
				{
					WriteSG(&item->item_flags[0], sizeof(short));
					WriteSG(&item->item_flags[1], sizeof(short));

					if (item->object_number == TORPEDO)
					{
						WriteSG(&item->current_anim_state, sizeof(short));
						WriteSG(&item->goal_anim_state, sizeof(short));
						WriteSG(&item->required_anim_state, sizeof(short));
					}
				}
			}

			item++;
		}

		for (int i = 0; i < 2; i++)
		{
			if (i)
			{
				rat = (RAT_STRUCT*)Spiders;
				objnum = SPIDER;
				maxval = 64;
			}
			else
			{
				rat = Rats;
				objnum = RAT;
				maxval = 32;
			}

			obj = &objects[objnum];

			if (obj->loaded)
			{
				byte = 0;

				for (int j = 0; j < maxval; j++)
				{
					if (rat[j].On)
						byte++;
				}

				WriteSG(&byte, sizeof(char));		//number of rats/spiders

				for (int j = 0; j < maxval; j++)
				{
					if (rat[j].On)
					{
						word = rat[j].room_number << 8;

						if (rat[j].pos.x_pos & 1)
							word |= 1;

						if (rat[j].pos.y_pos & 1)
							word |= 2;

						if (rat[j].pos.z_pos & 1)
							word |= 4;

						if (rat[j].pos.x_rot)
							word |= 8;

						WriteSG(&word, sizeof(short));
						pos = (short)(rat[j].pos.x_pos >> 1);
						WriteSG(&pos, sizeof(short));
						pos = (short)(rat[j].pos.y_pos >> 1);
						WriteSG(&pos, sizeof(short));
						pos = (short)(rat[j].pos.z_pos >> 1);
						WriteSG(&pos, sizeof(short));
						WriteSG(&rat[j].pos.y_rot, sizeof(short));

						if (word & 8)
							WriteSG(&rat[j].pos.x_rot, sizeof(short));

						WriteSG(&rat[j].flags, sizeof(uchar));
					}
				}
			}
		}

		obj = &objects[BAT];

		if (obj->loaded)
		{
			bat = Bats;
			byte = 0;

			for (int i = 0; i < 64; i++)
			{
				if (bat[i].On)
					byte++;
			}

			WriteSG(&byte, sizeof(uchar));	//number of bats

			for (int i = 0; i < 64; i++)
			{
				if (bat[i].On)
				{
					word = bat[i].room_number << 8;

					if (bat[i].pos.x_pos & 1)
						word |= 1;

					if (bat[i].pos.y_pos & 1)
						word |= 2;

					if (bat[i].pos.z_pos & 1)
						word |= 4;

					if (bat[i].pos.x_rot)
						word |= 8;

					WriteSG(&word, sizeof(short));	//what have we saved?
					pos = (short)(bat[i].pos.x_pos >> 1);
					WriteSG(&pos, sizeof(short));
					pos = (short)(bat[i].pos.y_pos >> 1);
					WriteSG(&pos, sizeof(short));
					pos = (short)(bat[i].pos.z_pos >> 1);
					WriteSG(&pos, sizeof(short));
					WriteSG(&bat[i].pos.y_rot, sizeof(short));

					if (word & 8)
						WriteSG(&bat[i].pos.x_rot, sizeof(short));

					WriteSG(&bat[i].Counter, sizeof(short));
				}
			}
		}

		if (lara.RopePtr != -1)
			SaveRope();
	}
}

void RestoreLevelData(long FullSave)
{
	ROOM_INFO* r;
	ITEM_INFO* item;
	CREATURE_INFO* creature;
	FLOOR_INFO* floor;
	OBJECT_INFO* obj;
	RAT_STRUCT* rat;
	BAT_STRUCT* bat;
	MESH_INFO* mesh;
	SavePtrPool pool;
	SavePtrDecoded decoded;
	ulong flags;
	long k, flare_age, field;
	ushort word, packed, uroom_number, uword;
	short sword, item_number, room_number, req, goal, current, objnum;
	uchar numberof;
	short anim;
	char byte, lflags;
	char line[128];

	pool = SavePointerPool();
	SavePointerPoolLog("load", &pool);
	ReadSG(&FmvSceneTriggered, sizeof(long));
	ReadSG(&GLOBAL_lastinvitem, sizeof(long));
	ReadSG(&sword, sizeof(short));	//FlipMap sets flip_stats

	for (int i = 0; i < 16; i++)
	{
		if (sword & (1 << i))
			FlipMap(i);

		ReadSG(&uword, sizeof(ushort));
		flipmap[i] = uword << 8;
	}

	for (int i = 0; i < 255 - 16; i++)	//align without corrupting flipmap array
		ReadSG(&uword, sizeof(ushort));

	ReadSG(&flipeffect, sizeof(long));
	ReadSG(&fliptimer, sizeof(long));
	ReadSG(&flip_status, sizeof(long));
	ReadSG(cd_flags, 136);
	ReadSG(&CurrentAtmosphere, sizeof(uchar));
	k = 16;

	for (int i = 0; i < number_rooms; i++)
	{
		r = &room[i];

		for (int j = 0; j < r->num_meshes; j++)
		{
			mesh = &r->mesh[j];

			if (mesh->static_number >= 50 && mesh->static_number <= 59)
			{
				if (k == 16)
				{
					ReadSG(&uword, sizeof(ushort));
					k = 0;
				}

				mesh->Flags ^= (uword ^ mesh->Flags) & 1;

				if (!mesh->Flags)
				{
					room_number = (short)i;
					floor = GetFloor(mesh->x, mesh->y, mesh->z, &room_number);
					GetHeight(floor, mesh->x, mesh->y, mesh->z);
					TestTriggers(trigger_index, 1, 0);
					floor->stopper = 0;
				}

				uword >>= 1;
				k++;
			}
		}
	}

	ReadSG(&CurrentSequence, sizeof(uchar));
	ReadSG(&byte, sizeof(char));

	for (int i = 0; i < 6; i++)
	{
		SequenceUsed[i] = byte & 1;
		byte >>= 1;
	}

	ReadSG(Sequences, 3);

	for (int i = 0; i < number_cameras; i++)
		ReadSG(&camera.fixed[i].flags, sizeof(short));

	for (int i = 0; i < number_spotcams; i++)
		ReadSG(&SpotCam[i].flags, sizeof(short));

	for (int i = 0; i < level_items; i++)
	{
		item = &items[i];
		obj = &objects[item->object_number];
		ReadSG(&packed, sizeof(ushort));

		if ((packed & SAVE_PACKED_BODY) == SAVE_PACKED_BODY)
			RestoreBody((short)i, item, obj);
		else if (packed & 0x2000)
		{
			KillItem((short)i);
			item->status = ITEM_DEACTIVATED;
			item->flags |= IFL_INVISIBLE;
		}
		else if (packed & 0x8000)
		{
			if (obj->save_position)
			{
				uroom_number = 0;
				ReadSG(&word, sizeof(ushort));
				item->pos.x_pos = (word << 1) | (packed >> 2) & 1;
				ReadSG(&sword, sizeof(short));
				item->pos.y_pos = (sword << 1) | (packed >> 3) & 1;
				ReadSG(&word, sizeof(ushort));
				item->pos.z_pos = (word << 1) | (packed >> 4) & 1;
				ReadSG(&uroom_number, sizeof(uchar));
				ReadSG(&item->pos.y_rot, sizeof(short));

				if (packed & 1)
					ReadSG(&item->pos.x_rot, sizeof(short));
				else
					item->pos.x_rot = 0;

				if (packed & 2)
					ReadSG(&item->pos.z_rot, sizeof(short));
				else
					item->pos.z_rot = 0;

				if (packed & 0x20)
					ReadSG(&item->speed, sizeof(short));
				else
					item->speed = 0;

				if (packed & 0x40)
					ReadSG(&item->fallspeed, sizeof(short));
				else
					item->fallspeed = 0;

				if (item->room_number != uroom_number)
					ItemNewRoom((short)i, uroom_number);

				if (obj->shadow_size)
				{
					floor = GetFloor(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos, (short*)&uroom_number);
					item->floor = GetHeight(floor, item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
				}
			}

			if (obj->save_anim)
			{
				current = 0;
				goal = 0;
				req = 0;
				ReadSG(&current, sizeof(char));
				ReadSG(&goal, sizeof(char));
				ReadSG(&req, sizeof(char));
				item->current_anim_state = current;
				item->goal_anim_state = goal;
				item->required_anim_state = req;

				if (item->object_number != LARA)
				{
					ReadSG(&anim, sizeof(short));
					item->anim_number = obj->anim_index + anim;
				}
				else
					ReadSG(&item->anim_number, sizeof(short));

				ReadSG(&item->frame_number, sizeof(short));
			}

			if (packed & 0x4000)
				ReadSG(&item->hit_points, sizeof(short));

			if (obj->save_flags)
			{
				ReadSG(&flags, sizeof(ulong));
				item->flags = (short)flags;

				if (packed & 0x80)
					ReadSG(&item->item_flags[0], sizeof(short));
				else
					item->item_flags[0] = 0;

				if (packed & 0x100)
					ReadSG(&item->item_flags[1], sizeof(short));
				else
					item->item_flags[1] = 0;

				if (packed & 0x200)
					ReadSG(&item->item_flags[2], sizeof(short));
				else
					item->item_flags[2] = 0;

				if (packed & 0x400)
					ReadSG(&item->item_flags[3], sizeof(short));
				else
					item->item_flags[3] = 0;

				if (packed & 0x800)
					ReadSG(&item->timer, sizeof(short));
				else
					item->timer = 0;

				if (packed & 0x1000)
					ReadSG(&item->trigger_flags, sizeof(short));
				else
					item->trigger_flags = 0;

				if (obj->intelligent || (item->object_number >= SEARCH_OBJECT1 && item->object_number <= SEARCH_OBJECT3))
					ReadSG(&item->carried_item, sizeof(short));

				if (flags & 0x10000 && !item->active)
					AddActiveItem((short)i);

				//*(ushort*)((char*)item + 0x15EA) = flags >> 16;	//doing it manually for clarity:
				item->active = (flags >> 16) & 1;
				item->status = (flags >> 17) & 3;
				item->gravity_status = (flags >> 19) & 1;
				item->hit_status = (flags >> 20) & 1;
				item->collidable = (flags >> 21) & 1;
				item->looked_at = (flags >> 22) & 1;
				item->dynamic_light = (flags >> 23) & 1;
				item->poisoned = (flags >> 24) & 1;
				item->ai_bits = (flags >> 25) & 31;
				item->really_active = (flags >> 30) & 1;

				if (flags & 0x80000000)
				{
					EnableBaddieAI((short)i, 1);
					creature = (CREATURE_INFO*)item->data;

					if (creature)
					{
						/* The enemy's four bytes are set aside and decoded
						 * once the LOT flags byte with its kind is read
						 * (save_pointer_rule.h). Adding this run's pool start
						 * to them, as before, gave a null enemy back as the
						 * difference of two starts - and the pool lies
						 * elsewhere at every start of the game. The guard
						 * the original had, `(long)enemy < 0`, would blank
						 * every address above 2 GB under /LARGEADDRESSAWARE;
						 * the rule asks the pool's bounds, unsigned. */
						ReadSG(creature, 22);
						memcpy(&field, (char*)creature + offsetof(CREATURE_INFO, enemy), sizeof(field));
						creature->enemy = 0;
						ReadSG(&creature->ai_target.object_number, sizeof(short));
						ReadSG(&creature->ai_target.room_number, sizeof(short));
						ReadSG(&creature->ai_target.box_number, sizeof(ushort));
						ReadSG(&creature->ai_target.flags, sizeof(short));
						ReadSG(&creature->ai_target.trigger_flags, sizeof(short));
						ReadSG(&creature->ai_target.pos, sizeof(PHD_3DPOS));
						ReadSG(&lflags, sizeof(char));
						creature->LOT.can_jump = (lflags & 1) == 1;
						creature->LOT.can_monkey = (lflags & 2) == 2;
						creature->LOT.is_amphibious = (lflags & 4) == 4;
						creature->LOT.is_jumping = (lflags & 8) == 8;
						creature->LOT.is_monkeying = (lflags & 16) == 16;
						SavePointerSlots(&pool);
						decoded = SavePtrDecodeEnemy(&pool, field, SavePtrKindOf(lflags), lara.item_number, i, SaveAddress(&creature->ai_target));
						creature->enemy = (ITEM_INFO*)(uintptr_t)decoded.value;

						if (gLogEnabled)
						{
							SavePtrEnemyLine(line, sizeof(line), i, field, &decoded);
							Log("%s", line);
						}
					}
					else
						SGpoint += 51;
				}

				if (item->object_number == ROPE)
					StraightenRope(item);
			}

			if (obj->save_mesh)
			{
				ReadSG(&item->mesh_bits, sizeof(ulong));
				ReadSG(&item->meshswap_meshbits, sizeof(ulong));
			}

			if (obj->collision == PuzzleHoleCollision)
			{
				if (item->status == ITEM_DEACTIVATED || item->status == ITEM_ACTIVE)
				{
					item->object_number += 8;
					item->anim_number = objects[item->object_number].anim_index + anim;
				}
			}

			if (item->object_number >= SMASH_OBJECT1 && item->object_number <= SMASH_OBJECT8 && item->flags & IFL_INVISIBLE)
				item->mesh_bits = 0x100;

			if (item->object_number == RAISING_BLOCK1 && item->item_flags[1])
			{
				if (item->trigger_flags == -1)
					AlterFloorHeight(item, -255);
				else if (item->trigger_flags == -3)
					AlterFloorHeight(item, -1023);
				else
					AlterFloorHeight(item, -1024);
			}

			if (item->object_number == RAISING_BLOCK2 && item->item_flags[1])
				AlterFloorHeight(item, -2048);
		}
	}

	if (FullSave)
	{
		ReadSG(&numberof, sizeof(uchar));

		for (int i = 0; i < numberof; i++)
		{
			item_number = CreateItem();
			item = &items[item_number];
			ReadSG(&byte, sizeof(char));

			if (!byte)
				item->object_number = FLARE_ITEM;
			else if (byte == 1)
				item->object_number = BURNING_TORCH_ITEM;
			else if (byte == 2)
				item->object_number = CHAFF;
			else if (byte == 3)
				item->object_number = TORPEDO;
			else
				item->object_number = CROSSBOW_BOLT;

			ReadSG(&item->pos, sizeof(PHD_3DPOS));
			ReadSG(&item->room_number, sizeof(short));
			req = item->pos.x_rot;
			goal = item->pos.y_rot;
			current = item->pos.z_rot;
			InitialiseItem(item_number);
			item->pos.x_rot = req;
			item->pos.y_rot = goal;
			item->pos.z_rot = current;
			ReadSG(&item->speed, sizeof(short));
			ReadSG(&item->fallspeed, sizeof(short));
			AddActiveItem(item_number);

			switch (item->object_number)
			{
			case BURNING_TORCH_ITEM:
				ReadSG(&item->item_flags[3], 2);
				break;

			case FLARE_ITEM:
				ReadSG(&flare_age, sizeof(long));
				item->data = (void*)(intptr_t)flare_age;
				break;

			case CHAFF:
			case TORPEDO:
				ReadSG(&item->item_flags[0], sizeof(short));
				ReadSG(&item->item_flags[1], sizeof(short));

				if (item->object_number == TORPEDO)
				{
					ReadSG(&item->current_anim_state, sizeof(short));
					ReadSG(&item->goal_anim_state, sizeof(short));
					ReadSG(&item->required_anim_state, sizeof(short));
				}

				break;
			}
		}

		for (int i = 0; i < 2; i++)
		{
			if (i)
			{
				rat = (RAT_STRUCT*)Spiders;
				objnum = SPIDER;
			}
			else
			{
				rat = Rats;
				objnum = RAT;
			}

			obj = &objects[objnum];

			if (obj->loaded)
			{
				ReadSG(&numberof, sizeof(uchar));

				for (int j = 0; j < numberof; j++)
				{
					ReadSG(&sword, sizeof(short));
					ReadSG(&uword, sizeof(ushort));
					rat[j].pos.x_pos = uword << 1;
					rat[j].pos.x_pos |= sword & 1;
					ReadSG(&req, sizeof(short));
					rat[j].pos.y_pos = req << 1;
					rat[j].pos.y_pos |= (sword >> 1) & 1;
					ReadSG(&uword, sizeof(ushort));
					rat[j].pos.z_pos = uword << 1;
					rat[j].pos.z_pos |= (sword >> 2) & 1;
					ReadSG(&rat[j].pos.y_rot, sizeof(short));

					if (sword & 8)
						ReadSG(&rat[j].pos.x_rot, sizeof(short));

					ReadSG(&rat[j].flags, sizeof(uchar));
					rat[j].On = 1;
					rat[j].room_number = (sword >> 8) & 0xFF;
				}
			}
		}

		obj = &objects[BAT];

		if (obj->loaded)
		{
			bat = Bats;
			ReadSG(&numberof, sizeof(uchar));

			for (int i = 0; i < numberof; i++)
			{
				ReadSG(&sword, sizeof(short));
				ReadSG(&uword, sizeof(ushort));
				bat[i].pos.x_pos = uword << 1;
				bat[i].pos.x_pos |= sword & 1;
				ReadSG(&word, sizeof(ushort));
				bat[i].pos.y_pos = word << 1;
				bat[i].pos.y_pos |= (sword >> 1) & 1;
				ReadSG(&uword, sizeof(ushort));
				bat[i].pos.z_pos = uword << 1;
				bat[i].pos.z_pos |= (sword >> 2) & 1;
				ReadSG(&bat[i].pos.y_rot, sizeof(short));

				if (sword & 8)
					ReadSG(&bat[i].pos.x_rot, sizeof(short));

				ReadSG(&bat[i].Counter, sizeof(short));
				bat[i].On = 1;
				bat[i].room_number = (sword >> 8) & 0xFF;
			}
		}

		if (lara.RopePtr != -1)
			LoadRope();
	}

	JustLoaded = 1;

	/* The whole world just moved. Every item, the braid and the camera have
	 * been written out of the save file; the state the interpolator holds
	 * is from wherever the player was standing when they pressed load, and
	 * blending into the loaded position would draw one frame of the entire
	 * level sliding into place. */
	InterpInvalidate("savegame loaded");
}

void sgSaveGame()
{
	char* ptr;
	long mark;
	char sum;

	SGcount = 0;
	SGpoint = savegame.buffer;
	savegame.Game.Timer = GameTimer;
	savegame.CurrentLevel = gfCurrentLevel;
	mark = SAVE_FORMAT_MARK;
	WriteSG(&mark, sizeof(long));
	SaveLevelData(1);
	SaveLaraData();
	savegame.Checksum = 0;
	ptr = (char*)&savegame;
	sum = 0;

	for (int i = 0; i < sizeof(SAVEGAME_INFO); i++)
	{
		sum += *ptr;
		ptr++;
	}

	savegame.Checksum = -sum;
}

void sgRestoreGame()
{
	long mark;

	// Validate the loaded savegame before trusting any of it - see
	// savegame_bytes_read (set in specific\gamemain.cpp's S_LoadGame) and
	// the buffer[] resize comment in global\types.h. Two independent,
	// deterministic checks:
	//  1. Size: an old-format save (written before buffer[] was resized)
	//     reads fewer bytes than sizeof(SAVEGAME_INFO) - Win32 ReadFile
	//     doesn't error on a short read, it just leaves the rest of the
	//     destination untouched, so without this check the unread tail of
	//     buffer[] would be whatever stale memory happened to be there and
	//     RestoreLevelData/ReadSG would decode it as if it were real data.
	//  2. Checksum: recompute the same additive byte-sum sgSaveGame() used
	//     (with Checksum temporarily zeroed, exactly matching how it was
	//     originally computed) and compare against the stored short value -
	//     catches a same-size but corrupted/tampered file, which the size
	//     check alone wouldn't. Deliberately compares as a typed short, not
	//     by re-summing Checksum's own 2 stored bytes as part of a flat byte
	//     scan: Checksum is a signed short holding a sign-extended signed
	//     char (-sum), so its high byte is 0xFF whenever that char is
	//     negative - summing that byte as its own signed char (0xFF -> -1)
	//     instead of accounting for the sign-extension gives the wrong total
	//     about half the time, wrongly rejecting a healthy save.
	// Both failure modes are unrecoverable here - DoLevel() (game\gameflow.cpp,
	// the only caller) has no error-recovery path for a save-load failure at
	// all, so - matching WriteSG's own overflow guard - this fails loudly and
	// immediately rather than attempt an unproven partial recovery.
	bool boValid = (savegame_bytes_read >= sizeof(SAVEGAME_INFO));

	// The sum is no longer computed here but in S_LoadGame right after
	// reading (specific\gamemain.cpp) - there the bytes are still the
	// file's. Between reading and this point lie the fade and the level
	// reload, and the game loop (ControlPhase) keeps writing into savegame
	// during that time. Computing the sum here would discard healthy
	// savegames.
	if (boValid)
		boValid = savegame_checksum_ok;

#ifdef _WIN64
	// A save written before Lara's meshes went in by number holds them as
	// distances into a 32-bit build's MESH_DATA (save_pointer_rule.h), which
	// name other places here: such a save is refused like a damaged one.
	if (boValid)
	{
		for (int i = 0; i < 15; i++)
		{
			if (SavePtrMeshNumber(savegame.Lara.mesh_ptrs[i], num_level_meshes) == -1)
				boValid = 0;
		}

		if (!boValid)
			Log("SAVELOG load refused: Lara's meshes as distances of a 32-bit build");
	}
#endif

	if (!boValid)
	{
		Log("FATAL: incompatible or corrupted savegame - bytes_read=%d, expected=%d", (int)savegame_bytes_read, (int)sizeof(SAVEGAME_INFO));

		MessageBox(0,
			"This savegame is from an incompatible or older version of the game "
			"(or the file is corrupted) and cannot be loaded safely.\r\n\r\n"
			"The game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	// Same size, sound checksum, older item block: refused by the mark
	// before a single item is read.
	memcpy(&mark, savegame.buffer, sizeof(long));

	if (mark != SAVE_FORMAT_MARK)
	{
		Log("FATAL: savegame format mark 0x%08lX, this version writes 0x%08lX",
			(unsigned long)mark, (unsigned long)SAVE_FORMAT_MARK);

		MessageBox(0,
			"This savegame was written by an older version of the game. "
			"This version uses a newer savegame format and cannot load "
			"it.\r\n\r\nThe game cannot continue and must close.",
			"Tomb Raider V - Fatal Error", MB_ICONSTOP);

		ExitProcess(1);
	}

	SGcount = 0;
	SGpoint = savegame.buffer;
	ReadSG(&mark, sizeof(long));
	GameTimer = savegame.Game.Timer;
	gfCurrentLevel = savegame.CurrentLevel;
	RestoreLevelData(1);
	RestoreLaraData(1);
}

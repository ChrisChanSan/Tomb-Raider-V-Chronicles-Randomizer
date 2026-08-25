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

SAVEGAME_INFO savegame;
static char* SGpoint = 0;
static long SGcount = 0;

tomb5_save_info tomb5_save;
ulong tomb5_save_size;
ulong savegame_bytes_read;
bool  savegame_checksum_ok;	// in S_LoadGame gebildet, siehe dort

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
}

static void load_tomb5_data()
{
	if (tomb5_save_size <= offsetof(tomb5_save_info, LHolster))
		LHolster = lara.holster;
	else
		LHolster = tomb5_save.LHolster;

	if (tomb5_save_size > offsetof(tomb5_save_info, dash_timer))
		DashTimer = tomb5_save.dash_timer;
}

void SaveLaraData()
{
	ITEM_INFO* item;

	for (int i = 0; i < 15; i++)
		lara.mesh_ptrs[i] = (short*)((long)lara.mesh_ptrs[i] - (long)mesh_base);

	lara.left_arm.frame_base = (short*)((long)lara.left_arm.frame_base - (long)objects[PISTOLS_ANIM].frame_base);
	lara.right_arm.frame_base = (short*)((long)lara.right_arm.frame_base - (long)objects[PISTOLS_ANIM].frame_base);
	lara.GeneralPtr = (void*)((long)lara.GeneralPtr - (long)malloc_buffer);
	memcpy(&savegame.Lara, &lara, sizeof(savegame.Lara));

	for (int i = 0; i < 15; i++)
		lara.mesh_ptrs[i] = (short*)((long)lara.mesh_ptrs[i] + (long)mesh_base);

	lara.left_arm.frame_base = (short*)((long)lara.left_arm.frame_base + (long)objects[PISTOLS_ANIM].frame_base);
	lara.right_arm.frame_base = (short*)((long)lara.right_arm.frame_base + (long)objects[PISTOLS_ANIM].frame_base);
	lara.GeneralPtr = (void*)((long)lara.GeneralPtr + (long)malloc_buffer);

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

	memcpy(&lara, &savegame.Lara, sizeof(lara));
	lara.target = 0;
	lara.spaz_effect = 0;
	lara.left_arm.frame_base = (short*)((long)lara.left_arm.frame_base + (long)objects[PISTOLS_ANIM].frame_base);
	lara.right_arm.frame_base = (short*)((long)lara.right_arm.frame_base + (long)objects[PISTOLS_ANIM].frame_base);
	lara.GeneralPtr = (void*)((long)lara.GeneralPtr + (long)malloc_buffer);

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
		lara.mesh_ptrs[i] = (short*)((long)lara.mesh_ptrs[i] + (long)mesh_base);

	_CutSceneTriggered1 = savegame.CutSceneTriggered1;
	_CutSceneTriggered2 = savegame.CutSceneTriggered2;
	load_tomb5_data();
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
	ulong flags;
	long k, flare_age;
	ushort packed;
	short pos, word, objnum, maxval;
	uchar byte;
	char lflags;

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
			packed = 0x2000;
			WriteSG(&packed, sizeof(ushort));
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

					if (item->object_number != LARA)
					{
						byte = item->anim_number - obj->anim_index;
						WriteSG(&byte, sizeof(uchar));
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
						creature = (CREATURE_INFO*)item->data;
						creature->enemy = (ITEM_INFO*)((long)creature->enemy - (long)malloc_buffer);
						WriteSG(item->data, 22);
						creature->enemy = (ITEM_INFO*)((long)creature->enemy + (long)malloc_buffer);
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
						WriteSG(&lflags, sizeof(char));
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
					flare_age = (long)item->data;
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
	ulong flags;
	long k, flare_age;
	ushort word, packed, uroom_number, uword;
	short sword, item_number, room_number, req, goal, current, objnum;
	uchar numberof;
	char byte, anim, lflags;

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
					room_number = i;
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

		if (packed & 0x2000)
		{
			KillItem(i);
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
					ItemNewRoom(i, uroom_number);

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
					ReadSG(&anim, sizeof(char));
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
					AddActiveItem(i);

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
					EnableBaddieAI(i, 1);
					creature = (CREATURE_INFO*)item->data;

					if (creature)
					{
						ReadSG(creature, 22);
						creature->enemy = (ITEM_INFO*)((long)creature->enemy + (long)malloc_buffer);

						if (creature->enemy < 0)
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
				item->data = (void*)flare_age;
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
}

void sgSaveGame()
{
	char* ptr;
	char sum;

	SGcount = 0;
	SGpoint = savegame.buffer;
	savegame.Game.Timer = GameTimer;
	savegame.CurrentLevel = gfCurrentLevel;
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
	//     about half the time. Hit exactly this bug during testing (a
	//     same-build save/load round-trip was wrongly rejected) before
	//     switching to this comparison.
	// Both failure modes are unrecoverable here - DoLevel() (game\gameflow.cpp,
	// the only caller) has no error-recovery path for a save-load failure at
	// all, so - matching WriteSG's own overflow guard - this fails loudly and
	// immediately rather than attempt an unproven partial recovery.
	bool boValid = (savegame_bytes_read >= sizeof(SAVEGAME_INFO));

	// The sum is NO LONGER computed here but in S_LoadGame right after
	// reading (specific\gamemain.cpp) - there the bytes are still the
	// file's. Between reading and this point lie the fade and the level
	// reload, and the game loop keeps writing into savegame during that
	// time (control.cpp:642). Whoever computes the sum here discards
	// healthy savegames.
	if (boValid)
		boValid = savegame_checksum_ok;

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

	SGcount = 0;
	SGpoint = savegame.buffer;
	GameTimer = savegame.Game.Timer;
	gfCurrentLevel = savegame.CurrentLevel;
	RestoreLevelData(1);
	RestoreLaraData(1);
}

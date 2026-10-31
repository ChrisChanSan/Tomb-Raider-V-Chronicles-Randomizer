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
#include "balance.h"
#include "swampy.h"
#include "box.h"
#include "objects.h"
#include "newinv2.h"
#include "../specific/3dmath.h"
#include "effects.h"
#include "control.h"
#include "lara.h"

static BITE_INFO swampy_hitright = { 0, 0, 0, 7 };

void InitialiseSwampy(short item_number)
{
	ITEM_INFO* item;

	item = &items[item_number];
	InitialiseCreature(item_number);
	item->anim_number = objects[GREEN_TEETH].anim_index + 1;
	item->frame_number = anims[item->anim_number].frame_base;
	item->current_anim_state = 2;
	item->goal_anim_state = 2;
	item->pos.y_pos += 512;
}

void SwampyControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* swampy;
	AI_INFO info;
	short angle, torso_y, torso_x, head;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	swampy = (CREATURE_INFO*)item->data;
	torso_x = 0;
	torso_y = 0;
	head = 0;

	// The swamp witch has 100 hit points and takes damage (HitEffect in
	// setup.cpp), but SwampyControl never checked hit_points, so there
	// was no exit from states 1-3 and she could not die. In vanilla she
	// only wakes once Lara has the coin (the check right below), and the
	// level is nearly over by then; since the coin can travel, Lara may
	// face her much earlier and armed. She has no death animation, so
	// CreatureDie(item_number, 1) shreds her instead, like the Imp and
	// the maze monster: it throws her own meshes as BODY_PART scraps and
	// handles KillItem, loot, AI and the invisible flag for the savegame
	// in one go.
	if (item->hit_points <= 0)
	{
		CreatureDie(item_number, 1);
		return;
	}

	if (have_i_got_object(PUZZLE_ITEM2))
	{
		item->ai_bits = 0;
		swampy->enemy = lara_item;
	}

	if (item->ai_bits)
		GetAITarget(swampy);
	else if (swampy->hurt_by_lara)
		swampy->enemy = lara_item;

	CreatureAIInfo(item, &info);

	if (swampy->enemy != lara_item)
		phd_atan(lara_item->pos.z_pos - item->pos.z_pos, lara_item->pos.x_pos - item->pos.x_pos);

	GetCreatureMood(item, &info, 1);
	CreatureMood(item, &info, 1);
	angle = CreatureTurn(item, swampy->maximum_turn);

	if (info.ahead)
	{
		torso_y = info.angle >> 1;
		head = info.angle >> 1;
		torso_x = info.x_angle;
	}

	swampy->maximum_turn = 0;

	switch (item->current_anim_state)
	{
	case 1:
		swampy->maximum_turn = 728;

		if (info.distance < 0x100000)
			item->goal_anim_state = 2;

		break;

	case 2:
		swampy->flags = 0;
		swampy->maximum_turn = 364;

		if (info.distance < 0x90000)
			item->goal_anim_state = 3;
		else if (info.distance > 0x100000)
			item->goal_anim_state = 1;
		else
			item->goal_anim_state = 2;

		break;

	case 3:
		swampy->maximum_turn = 364;

		// Refused while a scene holds Lara (scene_hold.h).
		if (!swampy->flags && (item->touch_bits & 0x3C3C0) && item->frame_number > anims[item->anim_number].frame_base + 29 &&
			!SceneHoldRefuses(EnemyDamage(balance.dmgSwampMonster), item->object_number, "swampy"))
		{
			lara_item->hit_points -= EnemyDamage(balance.dmgSwampMonster);
			lara_item->hit_status = 1;
			CreatureEffectT(item, &swampy_hitright, 10, item->pos.y_rot, DoBloodSplat);
			swampy->flags = 1;
		}

		break;
	}

	if (swampy->reached_goal)
	{
		if (swampy->enemy)
		{
			if (swampy->enemy->flags & 2)
				item->item_flags[3] = (item->TOSSPAD & 0xFF) - 1;

			item->item_flags[3]++;
			swampy->reached_goal = 0;
			swampy->enemy = 0;
		}
	}

	CreatureTilt(item, 0);
	CreatureJoint(item, 0, torso_y);
	CreatureJoint(item, 1, torso_x);
	CreatureJoint(item, 2, head);
	CreatureAnimation(item_number, angle, 0);
	CreatureUnderwater(item, 341);
}

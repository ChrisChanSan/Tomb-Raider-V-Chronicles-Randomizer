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
#include "crow.h"
#include "box.h"
#include "objects.h"
#include "effects.h"
#include "control.h"
#include "lara.h"

static BITE_INFO crow_bite = {2, 10, 60, 14};

void InitialiseCrow(short item_number)
{
	ITEM_INFO* item;

	InitialiseCreature(item_number);
	item = &items[item_number];

	// This slot plays the vulture animation set, not the crow's.
	//
	// The knowledge below is read off tomb3, Troye's Tomb Raider III
	// decompilation <https://github.com/Trxyebeep/tomb3> (GPLv3, the same
	// license as this project).
	//
	// In Tomb Raider 3, one function controls both birds and distinguishes
	// by object_number (tomb3 bird.cpp:19-32): the crow starts with
	// anim_index + 14 in the feeding state, the vulture with + 5 in the
	// standing state. TR5 only kept the crow branch - there is no vulture
	// there.
	//
	// The slot is safe to repurpose: CROW has a model in none of the 16 TR5
	// levels (measured against all level files). This function runs only
	// for the imported TR3 vulture, which has just 12 animations - with
	// + 14 it would index past the end of that set.
	item->anim_number = objects[CROW].anim_index + 5;
	item->frame_number = anims[item->anim_number].frame_base;
	item->goal_anim_state = 1;                 // BIRD_STOP
	item->current_anim_state = 1;
}

void CrowControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* crow;
	AI_INFO info;
	short angle;

	angle = 0;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	crow = (CREATURE_INFO*)item->data;

	if (item->hit_points <= 0)
	{
		if (item->current_anim_state == 4)
		{
			if (item->pos.y_pos >= item->floor)
			{
				item->pos.y_pos = item->floor;
				item->fallspeed = 0;
				item->gravity_status = 0;
				item->goal_anim_state = 5;
			}
		}
		else if (item->current_anim_state == 5)
			item->pos.y_pos = item->floor;
		else
		{
			// Fall animation: crow + 1, vulture + 8 (tomb3 bird.cpp:65-68).
			// See the reasoning in InitialiseCrow.
			item->anim_number = objects[CROW].anim_index + 8;
			item->frame_number = anims[item->anim_number].frame_base;
			item->current_anim_state = 4;
			item->gravity_status = 1;
			item->speed = 0;
		}

		item->pos.x_rot = 0;
	}
	else
	{
		CreatureAIInfo(item, &info);
		GetCreatureMood(item, &info, 0);
		CreatureMood(item, &info, 0);
		angle = CreatureTurn(item, 910);

		switch (item->current_anim_state)
		{
		case 1:
			crow->flags = 0;

			if (item->required_anim_state)
				item->goal_anim_state = item->required_anim_state;

			if (crow->mood == BORED_MOOD)
				item->goal_anim_state = 2;
			else if (info.ahead && info.distance < 0x40000)
				item->goal_anim_state = 6;
			else
				item->goal_anim_state = 3;

			break;

		case 2:
			item->pos.y_pos = item->floor;

			if (crow->mood != BORED_MOOD)
				item->goal_anim_state = 1;

			break;

		case 3:

			if (crow->mood == BORED_MOOD)
			{
				item->required_anim_state = 2;
				item->goal_anim_state = 1;
			}
			else if (info.ahead && info.distance < 0x40000)
				item->goal_anim_state = 6;

			break;

		case 6:

			if (!crow->flags && item->touch_bits)
			{
				lara_item->hit_points -= EnemyDamage(balance.dmgCrowStrike);
				lara_item->hit_status = 1;
				CreatureEffect(item, &crow_bite, DoBloodSplat);
				crow->flags = 1;
			}

			break;

		case 7:
			item->pos.y_pos = item->floor;

			if (crow->mood != BORED_MOOD)
				item->goal_anim_state = 1;

			break;
		}
	}

	CreatureAnimation(item_number, angle, 0);
}

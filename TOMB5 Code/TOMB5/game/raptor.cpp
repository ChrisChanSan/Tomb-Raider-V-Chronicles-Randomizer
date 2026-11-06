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
/*
 * Raptor from Tomb Raider 3, ported to TOMB5.
 *
 * Taken from tomb3 by Troye <https://github.com/Trxyebeep/tomb3>
 * (game/raptor.cpp, GPLv3) - the same license as TOMB5 and
 * this project. The code is deliberately taken over directly instead of
 * rewritten: rewriting would obscure its origin.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 *
 * ----------------------------------------------------------------------------
 * The AI substructure is the same between the two games (CreatureAIInfo,
 * GetCreatureMood, CreatureMood, CreatureTurn, CreatureTilt, CreatureJoint,
 * CreatureAnimation), so the control keeps running almost word for word.
 * In TR5 the Raptor sits on slot REAPER (75); none of the 16 levels carries
 * a model there (checked against all of them with ModelOffset), exactly as
 * with SAS for the T-Rex and CROW for the vulture.
 *
 * Two deviations from tomb3:
 *
 * The prey search is gone - the Raptor only hunts Lara. In TR3 the search
 * walks all active enemy slots and targets the nearest, which works
 * because the Raptor's neighbours there are mercenaries who fight Lara
 * rather than other Raptors; in TR5's cross enemy mix, everything at the
 * Raptor's side also hunts Lara, so the same search would always find an
 * ally instead of Lara.
 *
 * tomb3 additionally checks in the run state
 * `raptor->enemy->object_number != ANIMATING6`. In TR3, ANIMATING6 is a
 * target object for exactly this spot; in TR5 the same slot carries
 * something entirely different, so the check would be pointless to
 * harmful. It is dropped - consequence: the Raptor may show his warning
 * growl even where TR3 suppressed it. That is behavior, not function.
 * The pointer is checked against null in the process, which tomb3 does
 * not do there (its flow always sets it).
 *
 * Sounds come via the model's anim commands as with the T-Rex; no call is
 * needed here. The Raptor attacks normally, so there is no CreatureKill
 * gap as with the T-Rex.
 */
#include "../tomb5/pch.h"
#include "balance.h"
#include "raptor.h"
#include "box.h"
#include "objects.h"
#include "effects.h"
#include "control.h"
#include "lara.h"
#include "lot.h"
#include "items.h"
#include "../specific/function_stubs.h"

static BITE_INFO raptor_bite = { 0, 66, 318, 22 };

void RaptorControl(short item_number)
{
	ITEM_INFO* item;
	ITEM_INFO* enemy;
	CREATURE_INFO* raptor;
	AI_INFO info;
	short tilt, angle, neck, head;

	item = &items[item_number];
	enemy = 0;

	if (item->status == ITEM_INVISIBLE)
	{
		if (!EnableBaddieAI(item_number, 0))
			return;

		item->status = ITEM_ACTIVE;
	}

	raptor = (CREATURE_INFO*)item->data;
	tilt = 0;
	angle = 0;
	neck = 0;
	head = 0;

	if (item->hit_points <= 0)
	{
		if (item->current_anim_state != RAPTOR_DEATH)
		{
			if (GetRandomControl() > 0x4000)
				item->anim_number = objects[item->object_number].anim_index + 9;
			else
				item->anim_number = objects[item->object_number].anim_index + 10;

			item->frame_number = anims[item->anim_number].frame_base;
			item->current_anim_state = RAPTOR_DEATH;
		}
	}
	else
	{
		// The Raptor only targets Lara.
		//
		// tomb3's prey search stood here: it walks all active enemy slots,
		// takes the nearest as the target and only spares its own kind
		// (1 in 32, and only at short range). That works in TR3, where the
		// Raptor's neighbours are mercenaries fighting Lara rather than
		// other Raptors.
		//
		// Here that third faction does not exist: the cross enemy mix puts
		// guards, SWATs and other dinos at the Raptor's side, all of whom
		// also go after Lara, so the search would always find an ally and
		// the Raptor would maul its own reinforcements instead of hunting
		// Lara.
		//
		// An animal follows no AI marker: a Raptor under the human enemies'
		// `if (ai_bits) GetAITarget` line would walk to a guard's marker or
		// stand there until hurt or shot at. Its control is not one of the
		// readers (ai_marker_rule.h), so
		// GetAIPickups hands it no bits and the marker stays for others -
		// as with the T-Rex, compy, dog, lion and vulture. No TR5 level
		// carries a Raptor. The bite effect on a non-Lara enemy further
		// down stays as the port wrote it; the enemy is always Lara now.
		raptor->enemy = lara_item;

		CreatureAIInfo(item, &info);

		if (info.ahead)
			head = info.angle;

		GetCreatureMood(item, &info, 1);
		CreatureMood(item, &info, 1);

		if (raptor->mood == BORED_MOOD)
			raptor->maximum_turn >>= 1;

		angle = CreatureTurn(item, raptor->maximum_turn);
		neck = -6 * angle;

		switch (item->current_anim_state)
		{
		case RAPTOR_STOP:
			raptor->flags &= ~1;
			raptor->maximum_turn = 0;

			if (item->required_anim_state)
				item->goal_anim_state = item->required_anim_state;
			else if (raptor->flags & 2)
			{
				raptor->flags &= ~2;
				item->goal_anim_state = RAPTOR_WARNING;
			}
			else if (item->touch_bits & 0xFF7C00 || info.distance < 0x538D1 && info.bite)
				item->goal_anim_state = RAPTOR_ATTACK3;
			else if (info.bite && info.distance < 0x240000)
				item->goal_anim_state = RAPTOR_ATTACK1;
			else if (raptor->mood == ESCAPE_MOOD && lara.target != item && info.ahead && !item->hit_status)
				item->goal_anim_state = RAPTOR_STOP;
			else if (raptor->mood == BORED_MOOD)
				item->goal_anim_state = RAPTOR_WALK;
			else
				item->goal_anim_state = RAPTOR_RUN;

			break;

		case RAPTOR_WALK:
			raptor->flags &= ~1;
			raptor->maximum_turn = 364;

			if (raptor->mood != BORED_MOOD)
				item->goal_anim_state = RAPTOR_STOP;
			else if (info.ahead && GetRandomControl() < 128)
			{
				item->goal_anim_state = RAPTOR_STOP;
				item->required_anim_state = RAPTOR_WARNING;
				raptor->flags &= ~2;
			}

			break;

		case RAPTOR_RUN:
			raptor->flags &= ~1;
			raptor->maximum_turn = 728;
			tilt = angle;

			if (item->touch_bits & 0xFF7C00)
				item->goal_anim_state = RAPTOR_STOP;
			else if (raptor->flags & 2)
			{
				item->goal_anim_state = RAPTOR_STOP;
				item->required_anim_state = RAPTOR_WARNING;
				raptor->flags &= ~2;
			}
			else if (info.bite && info.distance < 0x240000)
			{
				if (item->goal_anim_state == RAPTOR_RUN)
					item->goal_anim_state = (short)(GetRandomControl() < 0x2000 ? RAPTOR_STOP : RAPTOR_ATTACK2);
			}
			// Without the ANIMATING6 comparison, but checked against null -
			// see the file header.
			else if (info.ahead && raptor->mood != ESCAPE_MOOD && GetRandomControl() < 128 && raptor->enemy)
			{
				item->goal_anim_state = RAPTOR_STOP;
				item->required_anim_state = RAPTOR_WARNING;
			}
			else if (raptor->mood == BORED_MOOD || raptor->mood == ESCAPE_MOOD && lara.target != item && info.ahead)
				item->goal_anim_state = RAPTOR_STOP;

			break;

		case RAPTOR_ATTACK1:
		case RAPTOR_ATTACK3:
			raptor->maximum_turn = 364;
			tilt = angle;
			enemy = raptor->enemy;

			if (enemy == lara_item)
			{
				// Refused while a scene holds Lara (scene_hold.h).
				if (!(raptor->flags & 1) && item->touch_bits & 0xFF7C00 &&
					!SceneHoldRefuses(EnemyDamage(balance.dmgRaptorBite), item->object_number, "raptor-bite"))
				{
					raptor->flags |= 1;
					CreatureEffect(item, &raptor_bite, DoBloodSplat);

					if (lara_item->hit_points <= 0)
						raptor->flags |= 2;

					lara_item->hit_points -= EnemyDamage(balance.dmgRaptorBite);
					lara_item->hit_status = 1;
					item->required_anim_state = RAPTOR_STOP;
				}
			}
			else if (!(raptor->flags & 1) && enemy)
			{
				if (abs(enemy->pos.x_pos - item->pos.x_pos) < 512 &&
					abs(enemy->pos.y_pos - item->pos.y_pos) < 512 &&
					abs(enemy->pos.z_pos - item->pos.z_pos) < 512)
				{
					enemy->hit_points -= 25;
					enemy->hit_status = 1;

					if (enemy->hit_status <= 0)
						raptor->flags |= 2;

					raptor->flags |= 1;
					CreatureEffect(item, &raptor_bite, DoBloodSplat);
				}
			}

			break;

		case RAPTOR_ATTACK2:
			raptor->maximum_turn = 364;
			tilt = angle;
			enemy = raptor->enemy;

			if (enemy == lara_item)
			{
				// Refused while a scene holds Lara (scene_hold.h).
				if (!(raptor->flags & 1) && item->touch_bits & 0xFF7C00 &&
					!SceneHoldRefuses(EnemyDamage(balance.dmgRaptorJump), item->object_number, "raptor-jump"))
				{
					raptor->flags |= 1;
					CreatureEffect(item, &raptor_bite, DoBloodSplat);
					lara_item->hit_points -= EnemyDamage(balance.dmgRaptorJump);
					lara_item->hit_status = 1;

					if (lara_item->hit_points <= 0)
						raptor->flags |= 2;

					item->required_anim_state = RAPTOR_RUN;
				}
			}
			else if (!(raptor->flags & 1) && enemy)
			{
				if (abs(enemy->pos.x_pos - item->pos.x_pos) < 512 &&
					abs(enemy->pos.y_pos - item->pos.y_pos) < 512 &&
					abs(enemy->pos.z_pos - item->pos.z_pos) < 512)
				{
					enemy->hit_points -= 25;
					enemy->hit_status = 1;

					if (enemy->hit_status <= 0)
						raptor->flags |= 2;

					raptor->flags |= 1;
					CreatureEffect(item, &raptor_bite, DoBloodSplat);
				}
			}

			break;
		}
	}

	CreatureTilt(item, tilt);
	CreatureJoint(item, 0, head >> 1);
	CreatureJoint(item, 1, head >> 1);
	CreatureJoint(item, 2, neck);
	CreatureJoint(item, 3, neck);
	CreatureAnimation(item_number, angle, tilt);
}

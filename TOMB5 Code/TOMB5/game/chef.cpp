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
#include "chef.h"
#include "box.h"
#include "objects.h"
#include "people.h"
#include "../specific/3dmath.h"
#include "sound.h"
#include "effects.h"
#include "control.h"
#include "lara.h"

static BITE_INFO knife = { 0, 200, 0, 13 };

void InitialiseChef(short item_number)
{
	ITEM_INFO* item;

	item = &items[item_number];
	InitialiseCreature(item_number);
	item->anim_number = objects[CHEF].anim_index;
	item->frame_number = anims[objects[CHEF].anim_index].frame_base;
	item->current_anim_state = 1;
	item->goal_anim_state = 1;
	item->pos.x_pos += 192 * phd_sin(item->pos.y_rot) >> W2V_SHIFT;
	item->pos.z_pos += 192 * phd_cos(item->pos.y_rot) >> W2V_SHIFT;
}

void ChefControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* chef;
	AI_INFO info;
	AI_INFO larainfo;
	long dx, dz;
	short angle, head, torso_y, torso_x;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	chef = (CREATURE_INFO*)item->data;
	head = 0;
	torso_y = 0;
	torso_x = 0;

	// The chef has hit points and takes damage (HitEffect in setup.cpp),
	// but ChefControl never checked them, so states 1-5 had no way out.
	// Vanilla never exposes this because joby3 disarms Lara before he can
	// be shot; the randomizer can leave enemies armed against him, so a
	// death path is needed here.
	//
	// His own animation set has no death animation: +11 (state 6) is a
	// 1-frame stub that calls KillItem with no transition. His skeleton is
	// bone-identical with the soldier family (0 of 14 bones deviating), so
	// he borrows their gunshot death instead: anim +16, state 8, 64 frames
	// including the death scream and a final KillItem, so the corpse stays
	// down like any soldier's. The donor chain matches the one in
	// setup.cpp - SWAT, then guard 460, then guard 37 - and +16 exists in
	// all three sets (the rich set's dead animations only start at +19).
	// The randomizer always includes a guard when the chef is placed as a
	// pool enemy; with no donor loaded at all, the vanilla stub is used.
	if (item->hit_points <= 0)
	{
		item->hit_points = 0;

		if (item->current_anim_state != 6 && item->current_anim_state != 8)
		{
			if (objects[SWAT].loaded)
				item->anim_number = objects[SWAT].anim_index + 16;
			else if (objects[BLUE_GUARD2].loaded)
				item->anim_number = objects[BLUE_GUARD2].anim_index + 16;
			else if (objects[BLUE_GUARD].loaded)
				item->anim_number = objects[BLUE_GUARD].anim_index + 16;
			else
				item->anim_number = objects[CHEF].anim_index + 11;

			item->frame_number = anims[item->anim_number].frame_base;
			// Take the state from the anim table (8 for the borrowed soldier
			// death, 6 for his own stub) - exactly as AnimateItem would carry
			// it over when advancing.
			item->current_anim_state = anims[item->anim_number].current_anim_state;
			item->goal_anim_state = item->current_anim_state;
		}

		CreatureAnimation(item_number, 0, 0);
		return;
	}

	if (item->ai_bits)
		GetAITarget(chef);
	else if (chef->hurt_by_lara)
		chef->enemy = lara_item;

	CreatureAIInfo(item, &info);

	if (chef->enemy == lara_item)
	{
		larainfo.angle = info.angle;
		larainfo.distance = info.distance;
	}
	else
	{
		dx = lara_item->pos.x_pos - item->pos.x_pos;
		dz = lara_item->pos.z_pos - item->pos.z_pos;
		larainfo.angle = short(phd_atan(dz, dx) - item->pos.y_rot);
		larainfo.ahead = larainfo.angle > -0x4000 && larainfo.angle < 0x4000;
		larainfo.distance = SQUARE(dz) + SQUARE(dx);
	}

	GetCreatureMood(item, &info, 1);
	CreatureMood(item, &info, 1);
	angle = CreatureTurn(item, chef->maximum_turn);

	if (info.ahead)
	{
		torso_y = info.angle >> 1;
		head = info.angle >> 1;
		torso_x = info.x_angle;
	}

	chef->maximum_turn = 0;

	switch (item->current_anim_state)
	{
	case 1:

		// State 1 is him working at the cutting board. In vanilla he only
		// puts the knife down when Lara comes within one and a half
		// sectors (0x240000 is the square of 1536), stands at the same
		// height, and becomes visible, loud, or physical in the process -
		// the kitchen in joby3 is built for exactly that moment.
		//
		// OCB 1 makes him a hunting chef: placed outside his kitchen by
		// the randomizer, he has no reason to wait, so he leaves the work
		// animation as soon as his trigger wakes him and seeks Lara like
		// any other enemy. No target needs to be delivered for that -
		// CreatureAIInfo targets Lara by itself when the enemy has no foe.
		//
		// The vanilla chef (joby3 item 186) carries OCB 0 and stays with
		// his vegetables; curation keeps him fixed in place regardless, so
		// he can never lose that OCB.
		if ((item->trigger_flags & 1) ||
			(abs(lara_item->pos.y_pos - item->pos.y_pos) < 1024 && info.distance < 0x240000 &&
			(item->touch_bits || lara_item->speed > 15 || item->hit_status || TargetVisible(item, &larainfo))))
		{
			item->goal_anim_state = 2;
			chef->alerted = 1;
			item->ai_bits = 0;
		}

		break;

	case 2:
		chef->maximum_turn = 0;

		if (info.angle < 0)
			item->pos.y_rot += 364;
		else
			item->pos.y_rot -= 364;

		if (item->frame_number == anims[item->anim_number].frame_end)
			item->pos.y_rot += 0x8000;

		break;

	case 3:
		chef->maximum_turn = 0;

		if (abs(info.angle) < 364)
			item->pos.y_rot += info.angle;
		else if (info.angle < 0)
			item->pos.y_rot -= 364;
		else
			item->pos.y_rot += 364;

		if (!chef->flags && item->touch_bits & 0x2000 && item->frame_number > anims[item->anim_number].frame_base + 10)
		{
			lara_item->hit_points -= EnemyDamage(balance.dmgChefKnife);
			lara_item->hit_status = 1;
			CreatureEffectT(item, &knife, 20, item->pos.y_rot, DoBloodSplat);
			SoundEffect(SFX_LARA_THUD, &item->pos, SFX_DEFAULT);
			chef->flags = 1;
		}

		break;

	case 4:
		chef->flags = 0;
		chef->maximum_turn = 364;

		if (info.distance < 0x718E4)
		{
			if (info.bite)
				item->goal_anim_state = 3;
		}
		else if (info.angle > 0x5000 || info.angle < -0x5000)
			item->goal_anim_state = 2;
		else if (chef->mood == ATTACK_MOOD)
			item->goal_anim_state = 5;

		break;

	case 5:
		chef->maximum_turn = 1274;

		if (info.distance < 0x718E4 || info.angle > 0x5000 || info.angle < -0x5000 || chef->mood != ATTACK_MOOD)
			item->goal_anim_state = 4;

		break;
	}

	CreatureTilt(item, 0);
	CreatureJoint(item, 0, torso_y);
	CreatureJoint(item, 1, torso_x);
	CreatureJoint(item, 2, head);
	CreatureAnimation(item_number, angle, 0);
}

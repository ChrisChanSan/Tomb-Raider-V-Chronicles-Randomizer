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

	// Der Koch war unsterblich: er hat Lebenspunkte und nimmt Schaden
	// (HitEffect in setup.cpp), aber ChefControl fragte hit_points nie ab -
	// es gab keinen Weg aus den Zustaenden 1-5 heraus. Vanilla faellt das
	// nicht auf, weil joby3 Lara vorher alle Waffen abnimmt; mit dem
	// Randomizer stehen dort Gegner und Lara ist bewaffnet, also braucht
	// er einen Tod.
	//
	// Sein eigener Satz hat aber keine ECHTE Sterbeanimation: +11 (Zustand
	// 6) ist ein 1-Frame-Stummel mit KillItem - er klappte uebergangslos
	// zusammen (#124, Nutzerfund). Sein Skelett ist knochengleich mit der
	// Soldatenfamilie (BonePruef: 0 von 14 Knochen abweichend), also leiht
	// er sich ihren Schusstod: Anim +16, Zustand 8, 64 Frames samt
	// Sterbeschrei und KillItem am Ende - die Leiche bleibt liegen wie bei
	// jedem Soldaten. Dieselbe Alias-Mechanik, mit der die Stummel-Familie
	// ihren ganzen Satz leiht, nur auf den Tod beschraenkt; die Kette ist
	// dieselbe wie in setup.cpp: SWAT -> Wachmann (460) -> Guard (37), und
	// +16 ist in allen drei Saetzen gesund (AnimMap; die toten Erben des
	// rich-Satzes beginnen erst bei +19). Als Pool-Gegner bringt der
	// Randomizer immer den Wachmann mit (kCross basisSlot 460); ganz ohne
	// Spender bleibt der Vanilla-Stummel.
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
			// Zustand aus der Anim-Tabelle uebernehmen (8 beim geliehenen
			// Soldatentod, 6 beim eigenen Stummel) - genau wie AnimateItem
			// ihn beim Weiterschalten nachziehen wuerde.
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

		if (abs(lara_item->pos.y_pos - item->pos.y_pos) < 1024 && info.distance < 0x240000 &&
			(item->touch_bits || lara_item->speed > 15 || item->hit_status || TargetVisible(item, &larainfo)))
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
			lara_item->hit_points -= GegnerSchaden(balance.sKochMesser);
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

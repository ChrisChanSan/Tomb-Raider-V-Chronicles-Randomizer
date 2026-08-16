/*
 * Raptor aus Tomb Raider 3, portiert 2026 nach TOMB5 (#97).
 *
 * Uebernommen aus tomb3 (game/raptor.cpp, GPLv3) - dieselbe Lizenz wie TOMB5
 * und dieses Projekt. Der Code wird bewusst direkt uebernommen statt
 * umgeschrieben: Umschreiben wuerde die Herkunft verschleiern.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 *
 * ----------------------------------------------------------------------------
 * Der KI-Unterbau ist zwischen beiden Spielen derselbe (CreatureAIInfo,
 * GetCreatureMood, CreatureMood, CreatureTurn, CreatureTilt, CreatureJoint,
 * CreatureAnimation) - deshalb laeuft die Steuerung nahezu wortgleich weiter.
 * Der Raptor sitzt in TR5 auf Slot REAPER (75); in keinem der 16 Level
 * steckt dort ein Modell (mit MissMod an allen nachgemessen), genau wie bei
 * SAS fuer den T-Rex und CROW fuer den Geier.
 *
 * ZWEI ABWEICHUNGEN gegenueber tomb3, beide hier begruendet:
 *
 * 1. Die Rudelsuche vergleicht in TR3 gegen RAPTOR, um zu verhindern, dass
 *    Raptoren sich gegenseitig als Beute waehlen. Hier steht dafuer REAPER -
 *    dasselbe in unserer Slotbelegung.
 *
 * 2. tomb3 prueft im Laufzustand zusaetzlich
 *    `raptor->enemy->object_number != ANIMATING6`. ANIMATING6 ist in TR3 ein
 *    Zielobjekt fuer genau diese Stelle; in TR5 traegt derselbe Slot etwas
 *    voellig anderes, die Abfrage waere also sinnlos bis schaedlich. Sie
 *    entfaellt - Folge: der Raptor darf auch dort sein Warnknurren zeigen, wo
 *    TR3 es unterdrueckte. Das ist Verhalten, keine Funktion.
 *    Der Zeiger wird dabei gegen null geprueft, was tomb3 an der Stelle nicht
 *    tut (dort ist er durch den Ablauf immer gesetzt) - siehe die Lehre aus
 *    dem T-Rex: beim Weglassen einer Bedingung pruefen, was sie trug.
 *
 * Klaenge kommen wie beim T-Rex ueber die Anim-Kommandos des Modells; hier ist
 * kein Aufruf noetig. Der Raptor greift normal an, es gibt also keine
 * CreatureKill-Luecke wie beim T-Rex.
 */
#include "../tomb5/pch.h"
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
	ITEM_INFO* candidate;
	CREATURE_INFO* raptor;
	AI_INFO info;
	long x, y, z, dist, best_dist, lp;
	short tilt, angle, neck, head;

	item = &items[item_number];
	enemy = 0;
	best_dist = 0x7FFFFFFF;

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
		if (!raptor->enemy || !(GetRandomControl() & 0x7F))
		{
			for (lp = 0; lp < MAX_LOT; lp++)
			{
				if (baddie_slots[lp].item_num == NO_ITEM || baddie_slots[lp].item_num == item_number)
					continue;

				candidate = &items[baddie_slots[lp].item_num];
				x = (candidate->pos.x_pos - item->pos.x_pos) >> 6;
				y = (candidate->pos.y_pos - item->pos.y_pos) >> 6;
				z = (candidate->pos.z_pos - item->pos.z_pos) >> 6;
				dist = SQUARE(x) + SQUARE(y) + SQUARE(z);

				if (dist < best_dist && item->hit_points > 0)
				{
					enemy = candidate;
					best_dist = dist;
				}
			}

			// REAPER statt RAPTOR: unser Slot. Damit waehlen sich Raptoren
			// untereinander nicht als Beute.
			if (enemy && (enemy->object_number != REAPER || GetRandomControl() < 0x400 && best_dist < 0x400000))
				raptor->enemy = enemy;

			//Favor Lara if she's closer
			x = (lara_item->pos.x_pos - item->pos.x_pos) >> 6;
			y = (lara_item->pos.y_pos - item->pos.y_pos) >> 6;
			z = (lara_item->pos.z_pos - item->pos.z_pos) >> 6;
			dist = SQUARE(x) + SQUARE(y) + SQUARE(z);

			if (dist < best_dist)
				raptor->enemy = lara_item;
		}

		if (item->ai_bits)
			GetAITarget(raptor);

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
					item->goal_anim_state = GetRandomControl() < 0x2000 ? RAPTOR_STOP : RAPTOR_ATTACK2;
			}
			// Ohne den ANIMATING6-Vergleich, dafuer gegen null geprueft -
			// siehe Abweichung 2 im Dateikopf.
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
				if (!(raptor->flags & 1) && item->touch_bits & 0xFF7C00)
				{
					raptor->flags |= 1;
					CreatureEffect(item, &raptor_bite, DoBloodSplat);

					if (lara_item->hit_points <= 0)
						raptor->flags |= 2;

					lara_item->hit_points -= 100;
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
				if (!(raptor->flags & 1) && item->touch_bits & 0xFF7C00)
				{
					raptor->flags |= 1;
					CreatureEffect(item, &raptor_bite, DoBloodSplat);
					lara_item->hit_points -= 100;
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
